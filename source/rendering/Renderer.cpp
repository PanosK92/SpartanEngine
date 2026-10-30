/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

//= INCLUDES ===================================
#include "pch.h"
#include <mutex>
#include <unordered_set>
#include <unordered_map>
#include <future>
#include <cmath>
#include <immintrin.h>
#include "Renderer_Internal.h"
#include "Material.h"
#include "IesProfile.h"
#include "OceanShore.h"
#include "../../data/shaders/shared_lighting.h"
#include "GeometryBuffer.h"
#include "ThreadPool.h"
#include "../profiling/RenderDoc.h"
#include "../profiling/Profiler.h"
#include "../core/Window.h"
#include "../core/Timer.h"
#include "../file_system/FileSystem.h"
#include "../input/Input.h"
#include "../display/Display.h"
#include "../rhi/RHI_Device.h"
#include "../rhi/RHI_TextureStreaming.h"
#include "../rhi/RHI_SwapChain.h"
#include "../rhi/RHI_Queue.h"
#include "../rhi/RHI_Implementation.h"
#include "../rhi/RHI_Buffer.h"
#include "../rhi/RHI_Shader.h"
#include "../rhi/RHI_CommandList.h"
#include "../rhi/RHI_VendorTechnology.h"
#include "../rhi/RHI_FrameGeneration.h"
#include "../rhi/RHI_AccelerationStructure.h"
#include "../world/Entity.h"
#include "../world/components/Light.h"
#include "../world/components/Camera.h"
#include "../world/components/Volume.h"
#include "../world/components/Render.h"
#include "../world/components/Water.h"
#include "../world/components/Terrain.h"
#include "../world/components/Spline.h"
#include "../world/Weather.h"
#include "SurfaceWater.h"
#include "../core/ProgressTracker.h"
#include "../math/Rectangle.h"
#include "../resource/import/ImageImporter.h"
#include "../commands/console/ConsoleCommands.h"
#include "../error_handling/Breadcrumbs.h"
#include "../memory/FrameAllocator.h"
#include "../xr/Xr.h"
//==============================================

//= NAMESPACES ===============
using namespace std;
using namespace spartan::math;
//============================

namespace spartan
{
    Renderer::State::~State() = default;

    Renderer::State& Renderer::state()
    {
        static Renderer::State s;
        return s;
    }

    namespace
    {
        struct QueuedTextureUpload
        {
            shared_ptr<RHI_Texture> texture;
            uint32_t x = 0, y = 0, width = 0, height = 0;
            vector<std::byte> data;
        };
        mutex queued_texture_uploads_mutex;
        vector<QueuedTextureUpload> queued_texture_uploads;

        // set by TickUploadMaterials, consumed by UpdateAccelerationStructures
        bool materials_uploaded_this_frame = false;
        // Residency swaps replace SRVs without changing material slots or ray tracing geometry.
        bool bindless_textures_dirty = false;
    }

    namespace
    {
        const uint8_t  swap_chain_buffer_count    = 2;

        struct screenshot_request
        {
            string file_path;
            string png_path;
            string exr_path;
            ImageColorSpace exr_color_space = ImageColorSpace::LinearRec709;
            bool save_exr = false;
            bool pending  = false;
            bool ready    = false;
            bool secondary_view = false;
            bool ui = false;
            uint64_t secondary_generation = 0;
        };

        mutex screenshot_mutex;
        screenshot_request screenshot;
        // captures of the secondary view queue here, apart from the main view, so an asset preview being
        // written never refuses a viewport screenshot and a viewport screenshot never strands a preview
        screenshot_request secondary_capture;
        shared_ptr<RHI_Texture> screenshot_ui_target;
        uint32_t screenshot_index = 0;
        atomic<uint32_t> screenshot_saves_in_flight = 0;
        atomic<uint32_t> screenshot_save_failures   = 0;
        bool clean_capture       = false;
        bool camera_cut_pending  = false;
        Entity* secondary_camera_request = nullptr;
        Entity* secondary_render_root_request = nullptr;
        Entity* secondary_render_root_active = nullptr;
        Renderer_SecondaryViewMode secondary_view_mode_request =
            Renderer_SecondaryViewMode::Solid;
        Renderer_SecondaryViewMode secondary_view_mode_active =
            Renderer_SecondaryViewMode::Solid;
        Renderer_SecondaryViewBackdrop secondary_view_backdrop_request =
            Renderer_SecondaryViewBackdrop::Sky;
        Renderer_SecondaryViewBackdrop secondary_view_backdrop_active =
            Renderer_SecondaryViewBackdrop::Sky;
        uint32_t secondary_view_width_request = 1;
        uint32_t secondary_view_height_request = 1;
        uint64_t secondary_view_request_generation = 0;
        uint64_t secondary_view_active_generation = 0;
        uint64_t secondary_view_ready_generation = 0;
        shared_ptr<RHI_Texture> secondary_view_output;
        Renderer::ViewState primary_view;
        Renderer::ViewState preview_view;
        Renderer::ViewState* active_view = &primary_view;
        bool secondary_view_ready = false;
        // the temporal frame index of the primary camera, nrd treats any gap in it as a camera cut
        // and throws its history away, so a frame lent to a secondary view must not advance it

        // loading is read once per frame, worker threads flip it at any moment, and a frame that enabled
        // tracing before the flip and then skipped the tlas update after it traced against stale structures
        bool frame_is_loading = false;

        bool is_secondary_view_entity(Entity* entity)
        {
            return
                !secondary_render_root_active ||
                (
                    entity &&
                    (
                        entity == secondary_render_root_active ||
                        entity->IsDescendantOf(
                            secondary_render_root_active
                        )
                    )
                );
        }

        // the nee pool derives radiance from the flat material color, a color texture or terrain
        // layers would make it disagree with what a ray hit sees, those emitters stay on brdf hits
        bool is_pooled_emitter_material(Material* material)
        {
            return
                material &&
                material->GetProperty(MaterialProperty::EmissiveFromAlbedo) > 0.0f &&
                !material->HasTextureOfType(MaterialTextureType::Color) &&
                material->GetProperty(MaterialProperty::IsTerrain) == 0.0f;
        }

        // the occupied car's parts, their rain droplets answer its g forces
        bool is_drops_vehicle(Entity* entity)
        {
            const uint64_t vehicle_id = Renderer::GetSurfaceWater().entity_id;
            if (!vehicle_id) return false;
            // Membership only needs ancestor IDs, not a locked world lookup for
            // every draw and LOD of every object in the scene.
            for (Entity* ancestor = entity; ancestor; ancestor = ancestor->GetParent())
                if (ancestor->GetObjectId() == vehicle_id) return true;
            return false;
        }

        // a secondary view borrows the frame from the primary camera, the entity history
        // belongs to that other camera so reusing it writes bogus velocity, which shows up
        // as motion blur and taa smear on the preview
        const math::Matrix& matrix_previous_for_velocity(Entity* entity)
        {
            return secondary_render_root_active
                ? entity->GetMatrix()
                : entity->GetMatrixPrevious();
        }

        // the subtree of the last secondary view, its materials stay in the shared layout while its targets live
        uint64_t secondary_root_id = 0;
        uint32_t secondary_idle_frames = 0;
        constexpr uint32_t secondary_release_frames = 120;
        vector<Entity*> secondary_entities;
        vector<Entity*> secondary_render_entities;
        vector<Entity*> secondary_light_entities;

        void collect_secondary_entities()
        {
            secondary_entities.clear();
            secondary_render_entities.clear();
            secondary_light_entities.clear();
            Entity* root = secondary_root_id ? World::GetEntityById(secondary_root_id) : nullptr;
            if (!root)
            {
                return;
            }

            secondary_entities.push_back(root);
            root->GetDescendants(&secondary_entities);
            for (Entity* entity : secondary_entities)
            {
                if (entity->GetComponent<Render>())
                {
                    secondary_render_entities.push_back(entity);
                }
                if (entity->GetComponent<Light>())
                {
                    secondary_light_entities.push_back(entity);
                }
            }
        }

        // preview roots are intentionally outside the world's active render list
        const vector<Entity*>& render_entities()
        {
            return secondary_render_root_active
                ? secondary_render_entities
                : World::GetEntitiesWithRender();
        }

        // scene records are pooled but scattered, scans over every render are bound by these misses
        void prefetch_scene_data(const vector<const RenderSceneData*>& entries, size_t index, size_t end)
        {
            if (index < end)
            {
                const char* data = reinterpret_cast<const char*>(entries[index]);
                _mm_prefetch(data, _MM_HINT_T0);
                _mm_prefetch(data + 64, _MM_HINT_T0);
            }
        }

        const vector<const RenderSceneData*>& render_scene_data(bool filter_secondary = true)
        {
            if (!secondary_render_root_active)
            {
                return World::GetRenderSceneData();
            }

            static vector<const RenderSceneData*> preview;
            preview.clear();
            for (Entity* entity : secondary_render_entities)
            {
                if (!filter_secondary || entity->GetActive())
                {
                    preview.push_back(&entity->GetComponent<Render>()->GetSceneData());
                }
            }
            return preview;
        }

        const vector<Entity*>& light_entities()
        {
            return secondary_render_root_active
                ? secondary_light_entities
                : World::GetEntitiesLights();
        }

        vector<Render*> ray_tracing_renders;
        vector<Render*> ray_tracing_build_work;
        vector<Render*> ray_tracing_instanced_renders;
        double ray_tracing_prepared_at = -1.0;
        bool ray_tracing_pending_blas = false;
        bool ray_tracing_membership_dirty = false;

        void prepare_ray_tracing_renders()
        {
            SP_PROFILE_CPU();
            ray_tracing_prepared_at = Timer::GetTimeMs();
            static vector<Render*> previous_renders;
            previous_renders.swap(ray_tracing_renders);
            ray_tracing_renders.clear();
            ray_tracing_build_work.clear();
            ray_tracing_instanced_renders.clear();
            ray_tracing_pending_blas = false;
            Camera* camera = Renderer::GetViewCamera();
            const Vector3 camera_position = camera ? camera->GetEntity()->GetPosition() : Vector3::Zero;
            const auto& entries = render_scene_data();
            struct Candidates { vector<Render*> renders; vector<Render*> builds; vector<Render*> instanced; bool missing = false; };
            static array<Candidates, 32> batches;
            for (auto& batch : batches)
            {
                batch.renders.clear();
                batch.builds.clear();
                batch.instanced.clear();
                batch.missing = false;
            }
            const uint32_t jobs = entries.size() >= 256 ? static_cast<uint32_t>(batches.size()) : 1u;
            auto collect = [&](uint32_t first, uint32_t last)
            {
                auto& batch = batches[first];
                const size_t end = entries.size() * last / jobs;
                for (size_t i = entries.size() * first / jobs; i < end; ++i)
                {
                    prefetch_scene_data(entries, i + 8, end);
                    const RenderSceneData& data = *entries[i];
                    if (!data.active.load(memory_order_relaxed) || (!Renderer::IsPathTracingActive() && (data.flags & RenderFlags::ExcludeFromRayTracing))) continue;
                    Render* render = data.render;
                    // Prepare resident geometry independently of TLAS visibility. Otherwise
                    // travelling across a culling boundary starts another BLAS burst and
                    // temporarily disables ray-traced lighting for the whole frame.
                    const bool missing = !render->HasAccelerationStructure();
                    batch.missing |= missing;
                    if (missing) batch.builds.push_back(render);
                    // Keep the same off-screen shadow/reflection visibility budget.
                    if (camera && data.instances.empty() && !Renderer::IsPathTracingActive())
                    {
                        const float distance = max(data.max_distance_render, data.max_distance_shadow);
                        if (distance >= 0.0f && distance < numeric_limits<float>::max() * 0.5f)
                        {
                            const BoundingBox& bounds = data.bounding_box;
                            const float radius = distance + 25.0f;
                            if (Vector3::DistanceSquared(camera_position, bounds.GetClosestPoint(camera_position)) > radius * radius)
                                continue;
                        }
                    }
                    batch.renders.push_back(render);
                    if (!data.instances.empty()) batch.instanced.push_back(render);
                    if (!missing && render->NeedsBlasRefit()) batch.builds.push_back(render);
                }
            };
            SP_PROFILE_CPU_START("rt_collect");
            if (jobs == 1) collect(0, 1);
            else ThreadPool::ParallelLoop(collect, jobs);
            SP_PROFILE_CPU_END();
            // Stable source order preserves the TLAS membership comparison.
            for (const auto& batch : batches)
            {
                ray_tracing_renders.insert(ray_tracing_renders.end(), batch.renders.begin(), batch.renders.end());
                ray_tracing_build_work.insert(ray_tracing_build_work.end(), batch.builds.begin(), batch.builds.end());
                ray_tracing_instanced_renders.insert(ray_tracing_instanced_renders.end(), batch.instanced.begin(), batch.instanced.end());
                ray_tracing_pending_blas |= batch.missing;
            }
            ray_tracing_membership_dirty |= ray_tracing_renders != previous_renders;
        }

        // the texture the editor samples, it outlives the secondary render targets so a panel never shows a freed image
        bool ensure_secondary_view_output(uint32_t width, uint32_t height)
        {
            RHI_Texture* source = Renderer::GetRenderTarget(Renderer_RenderTarget::frame_output);
            if (!source)
            {
                return false;
            }

            RHI_Texture* output = secondary_view_output.get();
            if (!output || output->GetWidth() != width || output->GetHeight() != height || output->GetFormat() != source->GetFormat())
            {
                const uint32_t flags  = RHI_Texture_Srv | RHI_Texture_Uav | RHI_Texture_Rtv | RHI_Texture_ClearBlit;
                secondary_view_output = make_shared<RHI_Texture>(RHI_Texture_Type::Type2D, width, height, 1, 1, source->GetFormat(), flags, "secondary_view_output");
                secondary_view_ready  = false;
            }
            return secondary_view_output->GetRhiResource() != nullptr;
        }

        // parks the renderer globals of the active view in its ViewState and loads the other view's, calling it
        // again swaps them back, so the primary's camera history survives every secondary frame untouched
        void swap_view_state(Renderer::ViewState& other)
        {
            using namespace Renderer;
            PassState& pass      = m_pass_state;
            ViewPassHistory& h   = other.history;
            swap(pass.cleared_reflections,     h.cleared_reflections);
            swap(pass.cleared_rt_reflections,  h.cleared_rt_reflections);
            swap(pass.cleared_rt_shadows,      h.cleared_rt_shadows);
            swap(pass.depth_history_cleared,   h.depth_history_cleared);
            swap(pass.hiz_history_valid,       h.hiz_history_valid);
            swap(pass.restir,                  h.restir);
            swap(pass.cloud_history,           h.cloud_history);
            swap(pass.ssao_history,            h.ssao_history);
            swap(pass.fog_history,             h.fog_history);
            swap(pass.fog_source,              h.fog_source);
            swap(pass.fog_water_source,        h.fog_water_source);
            swap(pass.particle_volume_history, h.particle_volume_history);
            swap(pass.vrs_last_cleared_texture, h.vrs_last_cleared_texture);
            swap(m_resolution_render,      other.resolution_render);
            swap(m_resolution_output,      other.resolution_output);
            swap(m_taau_reset_history,     other.taau_reset_history);
            swap(m_is_hiz_suppressed,      other.hiz_suppressed);
            swap(m_shadow_slices,          other.shadow_slices);
            swap(m_count_active_lights,    other.light_count);
            swap(m_volumetric_light_count, other.volumetric_light_count);
        }

        void reset_view_history(Renderer::ViewState& state)
        {
            state.history       = {};
            state.exposure      = {};
            state.temporal_frame = 0;
            state.taau_reset_history = true;
            state.dirty_orthographic_projection = true;
        }

        float sanitize_resolution_scale(float scale)
        {
            return clamp(scale, 0.25f, 1.0f);
        }

        shared_ptr<RHI_Buffer> copy_texture_to_staging(RHI_Texture* texture)
        {
            if (!texture)
            {
                return nullptr;
            }

            auto staging = make_shared<RHI_Buffer>(RHI_Buffer_Type::Readback, texture->GetObjectSize(), 1, nullptr, true, "screenshot_staging");
            if (RHI_CommandList* cmd_list = RHI_CommandList::ImmediateExecutionBegin(RHI_Queue_Type::Graphics))
            {
                RHI_CommandList::CopyTextureToBuffer(texture, staging.get());
                RHI_CommandList::ImmediateExecutionEnd(cmd_list);
            }

            return staging;
        }

        void ensure_screenshot_directory_exists(const string& file_path)
        {
            string directory = FileSystem::GetDirectoryFromFilePath(file_path);
            if (!directory.empty() && !FileSystem::Exists(directory))
            {
                FileSystem::CreateDirectory_(directory);
            }
        }

        void save_screenshot_async(
            const screenshot_request& request,
            shared_ptr<RHI_Buffer> sdr_staging,
            shared_ptr<RHI_Buffer> exr_staging,
            uint32_t width,
            uint32_t height,
            uint32_t channel_count,
            uint32_t bits_per_channel
        )
        {
            SP_ASSERT_MSG(sdr_staging && sdr_staging->GetMappedData(), "Staging buffer not mappable");

            screenshot_saves_in_flight++;
            ThreadPool::AddTask([request, sdr_staging, width, height, channel_count, bits_per_channel]()
            {
                struct in_flight_guard
                {
                    ~in_flight_guard() { screenshot_saves_in_flight--; }
                } guard;

                void* sdr_data = sdr_staging->GetMappedData();

                // imgui blending leaves destination alpha at zero under chrome, a window capture is always opaque
                if (request.ui && channel_count == 4 && bits_per_channel == 8)
                {
                    uint8_t* pixels = static_cast<uint8_t*>(sdr_data);
                    const size_t count = static_cast<size_t>(width) * height;
                    for (size_t i = 0; i < count; i++)
                    {
                        pixels[i * 4 + 3] = 255;
                    }
                }

                if (!request.file_path.empty())
                {
                    ensure_screenshot_directory_exists(request.png_path);

                    string temp_file_path = request.png_path + ".tmp";
                    if (FileSystem::Exists(temp_file_path))
                    {
                        FileSystem::Delete(temp_file_path);
                    }

                    SP_LOG_INFO("Saving screenshot to '%s'...", request.png_path.c_str());
                    ImageImporter::SaveSdr(temp_file_path, width, height, channel_count, bits_per_channel, sdr_data);
                    if (FileSystem::Exists(request.png_path))
                    {
                        FileSystem::Delete(request.png_path);
                    }
                    FileSystem::Rename(temp_file_path, request.png_path);
                    if (!FileSystem::Exists(request.png_path))
                    {
                        screenshot_save_failures++;
                        SP_LOG_ERROR("Screenshot '%s' was not written, is the disk full?", request.png_path.c_str());
                        return;
                    }
                    SP_LOG_INFO("Screenshot saved as '%s'", request.png_path.c_str());
                    return;
                }

                SP_LOG_INFO("Saving SDR screenshot to '%s'...", request.png_path.c_str());
                ImageImporter::SaveSdr(request.png_path, width, height, channel_count, bits_per_channel, sdr_data);
                SP_LOG_INFO("SDR screenshot saved as '%s'", request.png_path.c_str());
            });

            if (request.save_exr && exr_staging && exr_staging->GetMappedData())
            {
                ThreadPool::AddTask([request, exr_staging, width, height, channel_count, bits_per_channel]()
                {
                    void* exr_data = exr_staging->GetMappedData();

                    SP_LOG_INFO("Saving HDR screenshot to '%s'...", request.exr_path.c_str());
                    ImageImporter::Save(request.exr_path, width, height, channel_count, bits_per_channel, exr_data, request.exr_color_space);
                    SP_LOG_INFO("HDR screenshot saved as '%s'", request.exr_path.c_str());
                });
            }
        }

        screenshot_request make_screenshot_request(
            const string& file_path,
            const bool secondary_view = false
        )
        {
            screenshot_request request;
            request.file_path = file_path;
            request.pending   = true;
            request.secondary_view = secondary_view;
            request.ui             = !secondary_view && cvar_screenshot_ui.GetValueAs<bool>();
            request.secondary_generation =
                secondary_view
                    ? secondary_view_request_generation
                    : 0;

            if (!file_path.empty())
            {
                request.png_path = file_path;
                return request;
            }

            uint32_t index  = screenshot_index++;
            request.save_exr = !request.ui;
            request.exr_path = "screenshot_" + to_string(index) + ".exr";
            request.png_path = "screenshot_" + to_string(index) + ".png";
            return request;
        }

        // pack the render's resolved uv state into any struct that exposes the standard uv fields
        // raster and ray tracing both call this so they always agree on per-render uv overrides
        template<typename T>
        void fill_uv_draw_fields_from_render(T& out, const Render* render)
        {
            if (render)
            {
                out.uv_tiling      = math::Vector2(render->ResolveUvTilingX(), render->ResolveUvTilingY());
                out.uv_offset      = math::Vector2(render->ResolveUvOffsetX(), render->ResolveUvOffsetY());
                out.uv_invert      = math::Vector2(render->ResolveUvInvertX(), render->ResolveUvInvertY());
                out.uv_rotation    = render->ResolveUvRotation();
                out.uv_world_space = render->ResolveUvWorldSpace();
            }
            else
            {
                out.uv_tiling      = math::Vector2(1.0f, 1.0f);
                out.uv_offset      = math::Vector2::Zero;
                out.uv_invert      = math::Vector2::Zero;
                out.uv_rotation    = 0.0f;
                out.uv_world_space = 0.0f;
            }
        }

        void fill_decal_draw_fields(Sb_DrawData& out, const Render* render)
        {
            using namespace Renderer;
            out.decal_offset = out.decal_count = 0;
            if (!render || render->HasInstancing() || render->GetDecals().empty()) return;
            auto cached = m_decal_ranges.find(render);
            if (cached == m_decal_ranges.end())
            {
                const uint32_t first = static_cast<uint32_t>(m_decal_data.size());
                const auto inverse = render->GetEntity()->GetMatrix().Inverted();
                for (const auto& local : render->GetDecals())
                {
                    if (m_decal_data.size() == renderer_max_decals) break;
                    auto decal = local.parameters;
                    decal.world_to_decal = inverse * local.parameters.world_to_decal;
                    decal.source_material = local.source_material ? local.source_material->GetIndex() : 0xffffffffu;
                    m_decal_data.push_back(decal);
                }
                cached = m_decal_ranges.emplace(render, std::make_pair(
                    m_frame_resource_index * renderer_max_decals + first, static_cast<uint32_t>(m_decal_data.size()) - first)).first;
            }
            out.decal_offset = cached->second.first;
            out.decal_count = cached->second.second;
        }

        void sanitize_vendor_upscaler_resolution()
        {
            const Renderer_AntiAliasing_Upsampling mode =
                cvar_antialiasing_upsampling.GetValueAs<Renderer_AntiAliasing_Upsampling>();
            if (mode != Renderer_AntiAliasing_Upsampling::AA_Dlss_Upscale_Dlss &&
                mode != Renderer_AntiAliasing_Upsampling::AA_Xess_Upscale_Xess)
            {
                return;
            }

            // stereo falls back to taau, the eye resolution is owned by Xr
            if (Renderer::IsStereoView())
            {
                return;
            }

            const uint32_t out_w = static_cast<uint32_t>(Renderer::GetResolutionOutput().x);
            const uint32_t out_h = static_cast<uint32_t>(Renderer::GetResolutionOutput().y);
            const uint32_t in_w  = static_cast<uint32_t>(Renderer::GetResolutionRender().x);
            const uint32_t in_h  = static_cast<uint32_t>(Renderer::GetResolutionRender().y);
            if (out_w < 8 || out_h < 8 || in_w < 8 || in_h < 8)
            {
                return;
            }

            const float out_aspect = static_cast<float>(out_w) / static_cast<float>(out_h);
            const float in_aspect  = static_cast<float>(in_w) / static_cast<float>(in_h);
            const float scale      = static_cast<float>(in_w) / static_cast<float>(out_w);
            // dlss ultra performance is 1/3, ngx rejects lower scales and mismatched aspects
            const float min_scale      = 0.33f;
            const float max_scale      = 1.0f;
            const float aspect_epsilon = 0.01f;
            const bool aspect_bad = abs(in_aspect / out_aspect - 1.0f) > aspect_epsilon;
            const bool scale_bad  = scale < min_scale || scale > max_scale;
            if (!aspect_bad && !scale_bad)
            {
                return;
            }

            const float fitted_scale = clamp(scale, min_scale, max_scale);
            uint32_t new_w = static_cast<uint32_t>(static_cast<float>(out_w) * fitted_scale + 0.5f);
            uint32_t new_h = static_cast<uint32_t>(static_cast<float>(out_h) * fitted_scale + 0.5f);
            new_w = max(new_w, 64u) & ~1u;
            new_h = max(new_h, 64u) & ~1u;
            if (new_w == in_w && new_h == in_h)
            {
                return;
            }

            SP_LOG_INFO(
                "vendor upscaler: render %ux%u is invalid for output %ux%u, using %ux%u",
                in_w,
                in_h,
                out_w,
                out_h,
                new_w,
                new_h
            );
            Renderer::SetResolutionRender(new_w, new_h);
        }

        void tick_dynamic_resolution_scale()
        {
            // in stereo every scale change rebuilds the eye targets
            if (cvar_dynamic_resolution.GetValue() == 0.0f || Renderer::IsStereoView())
            {
                return;
            }

            const float gpu_time_target   = 16.67f;
            const float adjustment_factor = static_cast<float>(0.05f * Timer::GetDeltaTimeRealSec());
            float       screen_percentage = Renderer::GetResolutionScale();
            const float gpu_time          = Profiler::GetTimeGpuLast();

            if (gpu_time < gpu_time_target)
            {
                screen_percentage += adjustment_factor * (gpu_time_target - gpu_time);
            }
            else
            {
                screen_percentage -= adjustment_factor * (gpu_time - gpu_time_target);
            }

            screen_percentage = sanitize_resolution_scale(screen_percentage);
            ConsoleRegistry::Get().SetValueFromString("r.resolution_scale", to_string(screen_percentage));
        }
    }

    void Renderer::Initialize()
    {
        if (cvar_debug_renderdoc.GetValue())
        {
            RenderDoc::OnPreDeviceCreation();
        }
        RHI_Device::Initialize();
        RHI_Device::SetPipelineBoundCallback([](RHI_CommandList* cmd_list)
        {
            RHI_Device::Bind(cmd_list);
            SetStandardResources(cmd_list);
        });
        RHI_Device::SetDefaultPushConstants(&m_pcb_pass_cpu, sizeof(m_pcb_pass_cpu));
        RHI_Device::SetScaleDimensionCallback([](uint32_t dimension, float scale)
        {
            return GetScaledDimension(dimension, scale);
        });

        if (cvar_debug_breadcrumbs.GetValue())
        {
            Breadcrumbs::Initialize();
        }

        ConsoleRegistry::Get().SetValueFromString("r.gamma",       to_string(Display::GetGamma()));
        ConsoleRegistry::Get().SetValueFromString("r.tonemapping", to_string(static_cast<float>(Renderer_Tonemapping::GranTurismo7)));

        {
            uint32_t width  = Window::GetWidthInPixels();
            uint32_t height = Window::GetHeightInPixels();
            SetResolutionOutput(width, height, false);
            SetResolutionRender(1920, 1080, false); // lower than output so taau works well
            SetViewport(static_cast<float>(width), static_cast<float>(height));
        }

        // must init before swapchain so breadcrumbs are available for the swapchain command lists
        RHI_VendorTechnology::Initialize();

        RHI_Device::CreateSwapChain(
            Window::GetHandleSDL(),
            Window::GetWidthInPixels(),
            Window::GetHeightInPixels(),
            cvar_vsync.GetValueAs<bool>() ? RHI_Present_Mode::Fifo : RHI_Present_Mode::Immediate,
            swap_chain_buffer_count,
            Display::GetHdr(),
            "renderer"
        );
        ConsoleRegistry::Get().SetValueFromString("r.hdr", RHI_Device::GetSwapChain()->IsHdr() ? "1" : "0");
        if (cvar_frame_generation.GetValueAs<bool>())
        {
            RHI_Device::GetSwapChain()->SetFrameGeneration(true);
            ConsoleRegistry::Get().SetValueFromString("r.frame_generation", RHI_Device::GetSwapChain()->GetFrameGeneration() ? "1" : "0");
        }

        ThreadPool::AddTask([]()
        {
            m_initialized_resources = false;

            // meshes and textures are independent, overlap them
            future<void> meshes_future = ThreadPool::AddTask([]()
            {
                CreateStandardMeshes();
            });
            CreateStandardTextures();
            meshes_future.get();

            CreateStandardMaterials();
            CreateFonts();
            CreateShaders();
            m_initialized_resources = true;
        });

        CreateBuffers();
        RHI_Device::SetDummyVertexBuffer(GetBuffer(Renderer_Buffer::DummyInstance));
        CreateDepthStencilStates();
        CreateRasterizerStates();
        CreateBlendStates();
        CreateRenderTargets(true, true, true);
        CreateSamplers();

        // pre-size to fit typical large worlds without a mid-load rebuild
        GeometryBuffer::Reserve(
            4u  * 1024u * 1024u, // vertices, ~128mb at 32b each
            12u * 1024u * 1024u, // indices,  ~48mb
            64u * 1024u,         // meshlet bounds
            4u  * 1024u * 1024u, // meshlet unique verts (~index/3)
            12u * 1024u * 1024u, // meshlet micro indices (~index count)
            512u * 1024u         // instances, lush biome props exceed the old 16k floor
        );

        if (RHI_Device::GetPrimaryPhysicalDevice()->IsBelowMinimumRequirements())
        {
            Window::SetSplashScreenVisible(false);
            SP_WARNING_WINDOW("The GPU does not meet the minimum requirements for running the engine. The engine might be missing features and it won't perform as expected.");
            Window::SetSplashScreenVisible(true);
        }

        SP_SUBSCRIBE_TO_EVENT(EventType::WindowFullScreenToggled, SP_EVENT_HANDLER_STATIC(OnFullScreenToggled));
        SP_SUBSCRIBE_TO_EVENT(EventType::WorldUnloading, [](const sp_variant&) {
            m_pass_state.ResetWorld();
            primary_view.exposure.Reset();
            reset_view_history(preview_view);
            secondary_root_id = 0;
            secondary_entities.clear();
            secondary_render_entities.clear();
            secondary_light_entities.clear();
            SetSurfaceInteraction({});
            SetSurfaceWater({});
            camera_cut_pending = true;
            m_taau_reset_history = true;
        });
    }

    void Renderer::Shutdown()
    {
        SP_FIRE_EVENT(EventType::RendererOnShutdown);

        RHI_TextureStreaming::Shutdown();
        RHI_Device::QueueWaitAll();

        RHI_CommandList::ImmediateExecutionShutdown();

        {
            DestroyResources();
            GeometryBuffer::Shutdown();
            ies::shutdown();
            ocean_shore::shutdown();
            RHI_Device::DestroySwapChain();
            m_icons_vertex_buffer = nullptr;
            m_tlas                = nullptr;
            secondary_view_output.reset();
            ReleaseSecondaryRenderTargets();
            preview_view = {};
            primary_view = {};
            active_view = &primary_view;
            secondary_root_id = 0;
            screenshot_ui_target.reset();
            secondary_camera_request = nullptr;
            secondary_render_root_request = nullptr;
            secondary_render_root_active = nullptr;
            secondary_view_ready = false;
            secondary_view_request_generation = 0;
            secondary_view_active_generation = 0;
            secondary_view_ready_generation = 0;
        }

        RHI_VendorTechnology::Shutdown();
        RHI_FrameGeneration::Shutdown();
        RenderDoc::Shutdown();

        // breadcrumbs
        if (cvar_debug_breadcrumbs.GetValue())
        {
            Breadcrumbs::Shutdown();
        }

        // release the gpu compression pool buffers before the device goes away
        RHI_Texture::ShutdownCompressionPool();

        RHI_Device::Destroy();
    }

    void Renderer::Tick()
    {
        // process deferred fullscreen toggle at a safe point with no command lists in flight
        if (Window::IsFullScreenTogglePending())
        {
            RHI_Device::QueueWaitAll();
            Window::ProcessFullScreenToggle();
        }

        sanitize_vendor_upscaler_resolution();
        tick_dynamic_resolution_scale();
        // Jitter generation and vendor dispatch must use this frame's active render size.
        RHI_VendorTechnology::Tick(&Renderer::view().frame, GetResolutionRender(), GetResolutionOutput(), GetResolutionScale());
        if (cvar_debug_breadcrumbs.GetValue())
        {
            Breadcrumbs::StartFrame();
        }

        TickRecreateOptionalRenderTargetsIfNeeded();

        const uint32_t min_render_dimension = 64;
        const bool resolution_valid         = m_resolution_render.x >= min_render_dimension && m_resolution_render.y >= min_render_dimension;
        const bool can_render               = !Window::IsMinimized() && m_initialized_resources && resolution_valid;
        // Keep presenting the last viewport image and the loading UI, but don't
        // rebuild/draw a half-published world or compete with its bulk uploads.
        const bool render_world             = can_render && !World::IsLoadingFromFile();

        // the swapchain image is acquired late, right before the back buffer blit, so the cpu doesn't
        // block on scanout before it has even recorded the frame
        if (can_render)
        {
            RotateFrameBuffers();
        }
        else if (m_frame_num > 0)
        {
            RHI_Device::GetQueue(RHI_Queue_Type::Graphics)->Wait();
        }
        RHI_Device::Tick(m_frame_num);
        SP_PROFILE_CPU_START("texture_streaming");
        if (RHI_TextureStreaming::Tick(m_frame_num))
        {
            bindless_textures_dirty = true;
        }
        SP_PROFILE_CPU_END();
        RHI_Device::BeginFrame(can_render);
        RHI_Device::Bind(RHI_Frame_List::Graphics);

        // a file load publishes gigabytes of geometry, streaming it out keeps it from piling up in the upload tail
        if (World::IsLoadingFromFile() && GeometryBuffer::GetPendingUploadBytes() >= 256ull * 1024 * 1024)
        {
            GeometryBuffer::BuildIfDirty();
        }

        m_draw_data_count      = 0;
        // Only scene rendering has a bulk upload later in this frame. During
        // loading, ImGui must upload its projection immediately; otherwise it
        // reads stale transforms from the rotating frame slots and draws can
        // disappear, exposing old swapchain contents.
        m_draw_data_gpu_synced = !render_world;
        Entity* secondary_camera_entity = nullptr;
        Entity* secondary_render_root_entity = nullptr;
        bool render_secondary_view = false;

        if (render_world)
        {
            TickUpdateHiZSuppressionState();

            // Batch the file load, then upload published geometry even while
            // terrain preparation is pending. After an arena reset there are no
            // old GPU buffers to render from during that first committed frame.
            if (!World::IsLoadingFromFile())
            {
                GeometryBuffer::BuildIfDirty();
            }

            // Arena growth preserves geometry and offsets. Completed BLAS own their
            // geometry; input vertex/index addresses are only consumed during builds.
            // RefitBlas resolves the current buffers on each call. Actual mesh edits
            // invalidate their own BLAS, and world teardown still destroys everything.

            const bool secondary_request_pending =
                secondary_camera_request ||
                secondary_render_root_request;
            const bool secondary_request_consumed =
                secondary_request_pending;
            if (secondary_request_consumed)
            {
                secondary_camera_entity =
                    secondary_camera_request;
                secondary_camera_request = nullptr;
                secondary_render_root_entity =
                    secondary_render_root_request;
                secondary_render_root_request = nullptr;
            }
            const uint32_t secondary_width =
                secondary_view_width_request;
            const uint32_t secondary_height =
                secondary_view_height_request;
            secondary_view_mode_active =
                secondary_view_mode_request;
            secondary_view_backdrop_active =
                secondary_view_backdrop_request;
            secondary_view_active_generation =
                secondary_view_request_generation;
            if (
                secondary_camera_entity &&
                secondary_render_root_entity &&
                secondary_camera_entity->GetComponent<Camera>() &&
                ensure_secondary_view_output(
                    secondary_width,
                    secondary_height
                )
            )
            {
                if (secondary_root_id != secondary_render_root_entity->GetObjectId())
                {
                    secondary_root_id = secondary_render_root_entity->GetObjectId();
                    reset_view_history(preview_view);
                }
                secondary_idle_frames = 0;
                render_secondary_view = true;
            }
            else if (secondary_request_consumed)
            {
                secondary_view_ready = false;
                lock_guard<mutex> lock(screenshot_mutex);
                if (
                    secondary_capture.pending &&
                    secondary_capture.secondary_generation <=
                        secondary_view_active_generation
                )
                {
                    // the generation it waited for came and went without rendering, dropping it here
                    // matters because a pending request blocks every later capture
                    secondary_capture = {};
                }
            }

            if (!render_secondary_view && HasSecondaryRenderTargets() && ++secondary_idle_frames >= secondary_release_frames)
            {
                ReleaseSecondaryRenderTargets();
                secondary_root_id = 0;
                reset_view_history(preview_view);
            }

            // one material layout serves both views, so neither invalidates the other's indices
            collect_secondary_entities();
            TickUploadMaterials();
            TickAdvanceFrameConstantBufferRing();

            if (render_secondary_view)
            {
                RenderSecondaryView(secondary_camera_entity, secondary_render_root_entity, secondary_width, secondary_height);
            }

            UpdateDrawCalls();
            TickUploadBindlessDependencies();
            UpdatePersistentLines();
            AddLinesToBeRendered();
        }

        // xrBeginFrame must precede UpdateFrameConstantBuffer so per eye matrices reflect this frame's predicted pose, paired with xrEndFrame below
        bool xr_should_render = false;
        if (Xr::IsSessionRunning())
        {
            xr_should_render = Xr::BeginFrame();
        }

        if (render_world)
        {
            UpdateFrameConstantBuffer();
            ProduceFrame();

            // lighting through post is recorded, hand it to the gpu now, otherwise it waits for the
            // editor ui and the swapchain acquire, the final list only blits, draws the ui and presents
            if (!Xr::IsSessionRunning() && RHI_Device::IsRecording(RHI_Frame_List::Graphics))
            {
                RHI_Device::Submit(
                    RHI_Frame_List::Graphics,
                    nullptr,
                    false,
                    nullptr,
                    m_cross_queue_sync.pending_compute_timeline,
                    m_cross_queue_sync.pending_compute_timeline_value
                );
                RHI_Device::Bind(RHI_Frame_List::Graphics);
            }
        }

        if (xr_should_render && can_render)
        {
            RHI_Texture* stereo_output = GetRenderTarget(Renderer_RenderTarget::frame_output_stereo);
            RHI_CommandList::BeginMarker("blit_to_xr_swapchain");
            RHI_CommandList::BlitToXrSwapchain(stereo_output ? stereo_output : GetRenderTarget(Renderer_RenderTarget::frame_output));
            RHI_CommandList::EndMarker();
        }

        // submit gpu work before xrEndFrame so the compositor does not read a stale swapchain image
        if (m_present_in_renderer)
        {
            RHI_Device::AcquireSwapChainImage();
            if (
                can_render &&
                RHI_Device::GetSwapChain() &&
                RHI_Device::GetSwapChain()->IsImageAcquired()
            )
            {
                RHI_Device::BlitToBackBuffer(
                    GetRenderTarget(
                        Renderer_RenderTarget::frame_output
                    )
                );
            }
            {
                RHI_Work submitted = RHI_Device::EndFrame(
                    m_cross_queue_sync.pending_compute_timeline,
                    m_cross_queue_sync.pending_compute_timeline_value
                );
                FrameResource& frame_resource =
                    m_frame_resources[m_frame_resource_index];
                frame_resource.completion_timeline =
                    submitted.timeline.get();
                frame_resource.completion_value =
                    submitted.value;
                m_cross_queue_sync.pending_compute_timeline       = nullptr;
                m_cross_queue_sync.pending_compute_timeline_value = 0;
            }
            FinalizeScreenshotReadback();
            EndXrFrame();
        }
        else if (Xr::IsSessionRunning())
        {
            if (xr_should_render && can_render)
            {
                // editor ui would otherwise delay endframe by a full imgui frame, that is the hmd judder
                RHI_Work submitted = RHI_Device::Submit(
                    RHI_Frame_List::Graphics,
                    nullptr,
                    false,
                    nullptr,
                    m_cross_queue_sync.pending_compute_timeline,
                    m_cross_queue_sync.pending_compute_timeline_value
                );

                FrameResource& frame_resource =
                    m_frame_resources[m_frame_resource_index];
                frame_resource.completion_timeline =
                    submitted.timeline.get();
                frame_resource.completion_value =
                    submitted.value;

                // editor submit samples frame_output, wait for this graphics work first
                m_cross_queue_sync.pending_compute_timeline =
                    frame_resource.completion_timeline;
                m_cross_queue_sync.pending_compute_timeline_value =
                    frame_resource.completion_value;

                EndXrFrame();
            }
            else
            {
                EndXrFrame();
            }
        }

        m_lines_vertices.clear();
        m_debug_triangles_vertices.clear();
        m_icons.clear();

        if (can_render)
        {
            m_frame_num++;
            if (m_frame_num == 1)
            {
                SP_FIRE_EVENT(EventType::RendererOnFirstFrameCompleted);
            }

            TickLogClusterOverflowRateLimited();
        }

        RenderDoc::Tick();
    }

    void Renderer::RenderSecondaryView(Entity* camera_entity, Entity* root, const uint32_t width, const uint32_t height)
    {
        SP_PROFILE_CPU();

        Camera* camera = camera_entity->GetComponent<Camera>();
        camera->SetAspectRatioOverride(static_cast<float>(width) / static_cast<float>(height));
        camera->Tick();
        root->SetActive(true);
        secondary_render_root_active = root;
        preview_view.camera          = camera;
        active_view                  = &preview_view;

        // the view writes the secondary slot of every per frame buffer, the primary's slot of this frame is left alone
        const uint32_t primary_frame_resource = m_frame_resource_index;
        swap_view_state(preview_view);
        SetViewRenderTargets(true);
        BindFrameResource(primary_frame_resource + renderer_draw_data_buffer_count);
        m_cpu_indirect_draw_arg_count = 0;
        m_resolution_render           = Vector2(static_cast<float>(width), static_cast<float>(height));
        m_resolution_output           = m_resolution_render;
        if (EnsureSecondaryRenderTargets())
        {
            swap_view_state(preview_view);
            reset_view_history(preview_view);
            swap_view_state(preview_view);
        }

        for (Entity* entity : secondary_render_entities)
        {
            Render* render = entity->GetComponent<Render>();
            render->UpdateAabb();
            render->SetVisible(true);
            render->UpdateLodIndices(camera);
        }
        m_is_hiz_suppressed    = true;
        m_draw_data_count      = 0;
        m_draw_data_gpu_synced = false;

        UpdateDrawCalls();
        TickUploadBindlessDependencies();
        UpdateFrameConstantBuffer();
        ProduceFrame();

        // runs on the display ready frame, after post process, so the backdrop stays flat
        // and the wires stay crisp instead of being blurred by depth of field
        RHI_Texture* frame_output = GetRenderTarget(Renderer_RenderTarget::frame_output);
        Pass_PreviewStudio(frame_output);
        Pass_Blit(frame_output, secondary_view_output.get(), false);

        // the primary's work is queued behind everything this view recorded, on both queues, so the gpu
        // scratch the views share (light clusters, cull counters, blur) is never rewritten under a reader
        RHI_Device::Bind(RHI_Frame_List::Graphics);
        RHI_Device::Submit(RHI_Frame_List::Graphics, nullptr, false, nullptr, m_cross_queue_sync.pending_compute_timeline, m_cross_queue_sync.pending_compute_timeline_value);
        RHI_Device::Bind(RHI_Frame_List::Graphics);
        m_cross_queue_sync.pending_compute_timeline       = nullptr;
        m_cross_queue_sync.pending_compute_timeline_value = 0;
        RHI_Device::SetFrameWait(nullptr, 0);
        RHI_Device::BeginComputeLists();

        swap_view_state(preview_view);
        SetViewRenderTargets(false);
        BindFrameResource(primary_frame_resource);
        m_cpu_indirect_draw_arg_count = 0;
        m_draw_data_count             = 0;
        m_draw_data_gpu_synced        = false;
        active_view                   = &primary_view;
        root->SetActive(false);
        secondary_render_root_active = nullptr;

        secondary_view_ready            = true;
        secondary_view_ready_generation = secondary_view_active_generation;
        lock_guard<mutex> lock(screenshot_mutex);
        // any render at or after the requested generation satisfies the capture, an exact
        // match would strand the request whenever the preview panel refreshed in between
        if (secondary_capture.pending && secondary_capture.secondary_generation <= secondary_view_active_generation)
        {
            secondary_capture.pending = false;
            secondary_capture.ready   = true;
        }
    }

    void Renderer::TickRecreateOptionalRenderTargetsIfNeeded()
    {
        if (!m_initialized_resources)
        {
            return;
        }

        static uint32_t options_hash  = 0;
        static float restir_scale_old = -1.0f;

        const uint32_t options_hash_new = (cvar_ssao.GetValueAs<bool>() << 0) | (cvar_ray_traced_reflections.GetValueAs<bool>() << 1) | (cvar_restir_pt.GetValueAs<bool>() << 2) | (cvar_restir_pt_async.GetValueAs<bool>() << 3);
        const float    restir_scale_new = cvar_restir_pt_scale.GetValue();

        if (options_hash_new != options_hash || restir_scale_new != restir_scale_old)
        {
            RHI_Device::QueueWaitAll(true);
            RHI_Device::DeletionQueueParse();
            UpdateOptionalRenderTargets();
            RHI_Device::DeletionQueueParse();
            options_hash     = options_hash_new;
            restir_scale_old = restir_scale_new;
        }
    }

    void Renderer::TickUpdateHiZSuppressionState()
    {
        // suppress hi-z while loading and for a grace window after, draw calls and acceleration structures stabilize first
        static uint32_t post_load_frames = 0;
        static bool was_loading          = true;
        const bool is_loading            = ProgressTracker::IsLoading();
        frame_is_loading                 = is_loading;

        if (is_loading)
        {
            was_loading = true;
        }
        else if (was_loading)
        {
            was_loading      = false;
            post_load_frames = 30;
        }

        if (post_load_frames > 0)
        {
            post_load_frames--;
        }

        m_is_hiz_suppressed = is_loading || post_load_frames > 0;
    }

    void Renderer::TickAdvanceFrameConstantBufferRing()
    {
        m_frame_cb_ring_slot++;
        if (m_frame_cb_ring_slot == renderer_draw_data_buffer_count)
        {
            m_frame_cb_ring_slot = 0;
            GetBuffer(Renderer_Buffer::ConstantFrame)->ResetOffset();
        }
    }

    void Renderer::TickUploadMaterials()
    {
        SP_PROFILE_CPU();
        // Spread demand collection over eight frames; mip residency does not need
        // a full island traversal every frame. Publish the maximum over the whole
        // sweep so shared materials do not oscillate between their near/far users.
        Camera* streaming_camera = Renderer::GetViewCamera();
        constexpr uint64_t demand_frames = 8;
        struct MaterialDemand
        {
            weak_ptr<Material> material;
            float pixels = 0.0f;
            float full_resolution_pixels = 0.0f;
            uint32_t revision = 0;
        };
        static unordered_map<uint64_t, MaterialDemand> material_pixels;
        static uint64_t demand_cycle = numeric_limits<uint64_t>::max();
        if (demand_cycle != m_frame_num / demand_frames)
        {
            material_pixels.clear();
            demand_cycle = m_frame_num / demand_frames;
        }
        const Vector3 streaming_position = streaming_camera ? streaming_camera->GetEntity()->GetPosition() : Vector3::Zero;
        const float streaming_focal_length = streaming_camera ? GetViewport().height / (2.0f * tan(streaming_camera->GetFovVerticalRad() * 0.5f)) : 0.0f;
        const auto& streaming_entries = render_scene_data(false);
        SP_PROFILE_CPU_START("material_demand");
        for (size_t i = m_frame_num % demand_frames; i < streaming_entries.size(); i += demand_frames)
        {
            const RenderSceneData& data = *streaming_entries[i];
            Render* render = data.render;
            Material* material = data.material;
            if (!material) continue;
            auto [it, inserted] = material_pixels.try_emplace(material->GetObjectId());
            if (inserted)
            {
                it->second.material = static_pointer_cast<Material>(material->weak_from_this().lock());
            }
            if (inserted || it->second.revision != material->GetRevision())
            {
                it->second.revision = material->GetRevision();
                it->second.full_resolution_pixels = 0.0f;
                // Request() reserves an extra mip: once twice the projected
                // pixels cover every source texture, all requests are mip zero.
                // Further instances cannot increase this material's residency.
                for (RHI_Texture* texture : material->GetTextures())
                    if (texture)
                        it->second.full_resolution_pixels = max(it->second.full_resolution_pixels,
                            0.5f * static_cast<float>(max(texture->GetWidth(), texture->GetHeight())));
            }
            float& demand = it->second.pixels;
            const float full_resolution_pixels = it->second.full_resolution_pixels;
            if (demand >= full_resolution_pixels) continue;

            const float repeat_x = max(abs(render->ResolveUvTilingX()), 0.001f);
            const float repeat_y = max(abs(render->ResolveUvTilingY()), 0.001f);
            const bool world_space_uv = render->ResolveUvWorldSpace() != 0.0f;
            auto projected_pixels = [&](const BoundingBox& bounds)
            {
                if (!streaming_camera || bounds.IsInfinite()) return numeric_limits<float>::infinity();
                const Vector3& camera_position = streaming_position;
                if (bounds.Contains(camera_position)) return numeric_limits<float>::infinity();
                if (world_space_uv)
                {
                    if (streaming_camera->GetProjectionType() != Projection_Perspective)
                        return numeric_limits<float>::infinity();
                    // World UV tiling is repeats per metre. Use the nearest point on a
                    // bounding sphere for a conservative upper bound on projected density.
                    const float distance = max(streaming_camera->GetNearPlane(),
                        Vector3::Distance(camera_position, bounds.GetCenter()) - bounds.GetExtents().Length());
                    return streaming_focal_length / (max(distance, 0.001f) * min(repeat_x, repeat_y));
                }
                const math::Rectangle rect = streaming_camera->WorldToScreenCoordinates(bounds);
                return max(abs(rect.width) / repeat_x, abs(rect.height) / repeat_y);
            };
            // Cached instance groups bound all their instances without scanning every
            // transform. Off-screen users retain demand for shadows and reflections.
            float pixels = 0.0f;
            if (render->HasInstancing() && !render->GetInstanceBoundsGroups().empty())
            {
                for (const auto& group : render->GetInstanceBoundsGroups())
                {
                    pixels = max(pixels, projected_pixels(group.bounds));
                    if (pixels >= full_resolution_pixels) break;
                }
            }
            else
                pixels = projected_pixels(data.bounding_box);
            demand = max(demand, pixels);
        }
        SP_PROFILE_CPU_END();
        SP_PROFILE_CPU_START("material_requests");
        if (m_frame_num % demand_frames == demand_frames - 1)
        for (const auto& [id, demand] : material_pixels)
        {
            // The world can change during a sweep. Never retain or dereference a
            // removed material, and never keep GPU resources alive past shutdown.
            if (auto material = demand.material.lock())
            for (RHI_Texture* texture : material->GetTextures())
            {
                if (!texture) continue;
                RHI_TextureStreaming::Request(texture, demand.pixels, m_frame_num);
            }
        }
        // Terrain layers and scatter materials can exist outside the entity list.
        auto request_full_material = [](Material* material)
        {
            if (!material) return;
            for (RHI_Texture* texture : material->GetTextures())
                RHI_TextureStreaming::Request(texture, numeric_limits<float>::infinity(), m_frame_num);
        };
        if (m_pass_state.terrain_enabled)
        {
            request_full_material(m_pass_state.terrain.surface);
            for (Material* layer : m_pass_state.terrain.layer_materials)
                request_full_material(layer);
        }
        for (const PassState::GpuScatterSlot& slot : m_pass_state.gpu_scatter)
            if (slot.enabled) request_full_material(slot.material);
        // the bindless slot a material owns is handed out here and a draw carries that slot as an index,
        // so this has to run before the draw data is written, doing it afterwards leaves the frame
        // sampling whatever material now sits where the index used to point, which reads as the wrong
        // texture and lands on the checkerboard whenever it resolves to the standard material

        // the secondary subtree's materials follow the world's in the layout, the world's change tracking does
        // not see them, so their own signature decides, it also drops them once the subtree is gone
        uint64_t secondary_signature = 0;
        for (Entity* entity : secondary_render_entities)
        {
            Material* material = entity->GetComponent<Render>()->GetMaterial();
            if (!material)
            {
                continue;
            }
            const uint64_t value = material->GetObjectId() ^ (static_cast<uint64_t>(material->GetRevision()) << 32) ^ static_cast<uint64_t>(material->GetResourceState());
            secondary_signature  = (secondary_signature ^ value) * 0x100000001b3ull + 0x9e3779b97f4a7c15ull;
            request_full_material(material);
        }
        static uint64_t uploaded_secondary_signature = 0;
        const bool secondary_changed = secondary_signature != uploaded_secondary_signature;
        SP_PROFILE_CPU_END();

        // Consume renderer-owned scene input changes before deciding whether to upload.
        const bool world_changed =
            HaveMaterialsChangedThisFrame();
        // terrain rules and the grass material are packed into the material buffer without touching
        // a Material, so the material revision cannot see them
        const bool bindless_changed = m_pass_state.bindless_materials_dirty;
        const bool update_materials = GetFrameNumber() == 0 || secondary_changed || world_changed || bindless_changed;
        if (!update_materials && !bindless_textures_dirty)
        {
            return;
        }

        ++m_pass_state.path_tracing.scene_revision;

        // a secondary view's materials are not part of the primary's scene, so they never cost it its gi history
        // a texture residency swap is the same image at another mip count and leaves gi alone, texture
        // streaming swaps every few frames while the camera moves and clearing on it kept restir at one sample
        // a material edit or a render switching material (a flickering fixture) fades the history out
        // instead of clearing it, the reservoirs re-trace visibility and re-shade on reuse so only the
        // cached emission of what changed goes stale
        static bool restir_invalidation_pending = false;
        static bool restir_fade_pending         = false;
        restir_invalidation_pending |= GetFrameNumber() == 0 || bindless_changed;
        restir_fade_pending         |= world_changed;
        if (restir_invalidation_pending || restir_fade_pending)
        {
            m_pass_state.restir.accumulation_valid = false;
            if (restir_invalidation_pending)
            {
                m_pass_state.restir.history_invalid = true;
            }
            else
            {
                m_pass_state.restir.history_fade_frames = restir_history_fade_frames;
            }
            restir_invalidation_pending = false;
            restir_fade_pending         = false;
        }

        if (update_materials)
        {
            m_pass_state.bindless_materials_dirty = false;
            uploaded_secondary_signature = secondary_signature;
            materials_uploaded_this_frame = true;
            UpdateMaterials();
        }
        bindless_textures_dirty = false;
        SP_PROFILE_CPU_START("material_bindless");
        // a residency swap only touches the swapped textures, the full table is prepared with the materials
        const vector<shared_ptr<RHI_Texture>> streamed = RHI_TextureStreaming::TakePublished();
        if (update_materials)
        {
            RHI_CommandList::PrepareTexturesForSampling(&m_bindless_textures);
        }
        else
        {
            RHI_CommandList::PrepareTexturesForSampling(streamed);
        }
        RHI_Device::UpdateBindlessMaterials(
            &m_bindless_textures,
            update_materials ? GetBuffer(Renderer_Buffer::MaterialParameters) : nullptr
        );
        SP_PROFILE_CPU_END();

        // null srvs write the 1m checkerboard, keep retrying until gpu prep finishes
        for (RHI_Texture* texture : m_bindless_textures)
        {
            if (texture && !texture->GetRhiSrv())
            {
                m_pass_state.bindless_materials_dirty = true;
                break;
            }
        }
    }

    void Renderer::TickUploadBindlessDependencies()
    {
        SP_PROFILE_CPU();
        // run during loading so newly published entities pick up materials and lights as they arrive
        const bool initialize     = GetFrameNumber() == 0;
        const bool secondary      = secondary_render_root_active != nullptr;
        // the world's change tracking belongs to the primary, a secondary view packs its few lights every frame,
        // the atlas packing skips lights without an index, so they are indexed first
        const bool lights_changed = initialize || (!secondary && HaveLightsChanged());

        if (secondary)
        {
            // its lights are inactive while the world ticks, so their matrices are fitted here, to its own camera and atlas
            UpdateLights();
            UpdateShadowAtlas();
            for (Entity* entity : light_entities())
            {
                entity->GetComponent<Light>()->UpdateMatrices();
            }
        }
        else if (lights_changed)
        {
            UpdateShadowAtlas();
        }

        // frustum and draw distance membership depend on the camera, rebuild every frame
        SP_PROFILE_CPU_START("bindless_lights");
        UpdateLights();
        if (lights_changed)
        {
            ++m_pass_state.path_tracing.scene_revision;
            RHI_Device::UpdateBindlessLights(GetBuffer(Renderer_Buffer::LightParameters));
        }
        SP_PROFILE_CPU_END();

        if (m_bindless_samplers_dirty)
        {
            RHI_Device::UpdateBindlessSamplers(Renderer::GetSamplers().data(), static_cast<uint32_t>(Renderer::GetSamplers().size()));
            m_bindless_samplers_dirty = false;
        }

        // aabbs change every frame with entity transforms
        SP_PROFILE_CPU_START("bindless_aabbs");
        UpdateBoundingBoxes();
        SP_PROFILE_CPU_END();
        SP_PROFILE_CPU_START("bindless_frame_uploads");
        static bool aabbs_descriptor_set = false;
        if (!aabbs_descriptor_set)
        {
            RHI_Device::UpdateBindlessAABBs(GetBuffer(Renderer_Buffer::AABBs));
            aabbs_descriptor_set = true;
        }

        // draw data, single descriptor for all frame slots avoids races where a host visible descriptor swap could overlap a gpu read
        if (m_draw_data_count > 0)
        {
            RHI_Buffer* buffer               = GetBuffer(Renderer_Buffer::DrawData);
            const uint32_t frame_byte_offset = m_frame_resource_index * renderer_max_draw_calls * static_cast<uint32_t>(sizeof(Sb_DrawData));
            const uint32_t upload_size       = static_cast<uint32_t>(sizeof(Sb_DrawData)) * m_draw_data_count;
            if (void* mapped = buffer->GetMappedData())
            {
                // Stream the scene's contiguous records in one copy instead of
                // interleaving thousands of small write-combined GPU writes
                // with pointer chasing and material sorting on the CPU.
                ThreadPool::ParallelCopy(static_cast<char*>(mapped) + frame_byte_offset, m_draw_data_cpu.data(), upload_size);
            }
            else
            {
                RHI_CommandList::UpdateBuffer(buffer, frame_byte_offset, upload_size, &m_draw_data_cpu[0]);
            }
        }
        if (!m_decal_data.empty())
        {
            RHI_Buffer* buffer = GetBuffer(Renderer_Buffer::Decals);
            const uint32_t offset = m_frame_resource_index * renderer_max_decals * sizeof(DecalParameters);
            const uint32_t size = static_cast<uint32_t>(m_decal_data.size() * sizeof(DecalParameters));
            if (void* mapped = buffer->GetMappedData())
                memcpy(static_cast<char*>(mapped) + offset, m_decal_data.data(), size);
            else
                RHI_CommandList::UpdateBuffer(buffer, offset, size, m_decal_data.data());
        }
        if (Weather::IsShelterActive())
        {
            RHI_Buffer* buffer = GetBuffer(Renderer_Buffer::RainOcclusion);
            const uint32_t size = Weather::occlusion_resolution * Weather::occlusion_resolution * sizeof(float);
            const uint32_t offset = m_frame_resource_index * size;
            if (void* mapped = buffer->GetMappedData())
                memcpy(static_cast<char*>(mapped) + offset, Weather::GetOcclusionHeights(), size);
            else
                RHI_CommandList::UpdateBuffer(buffer, offset, size, Weather::GetOcclusionHeights());
        }
        m_pass_state.surface_water.dirt_texels_staged = 0;
        if (Renderer::GetSurfaceWater().active && !secondary_render_root_active)
        {
            auto upload = [](RHI_Buffer* buffer, uint32_t offset, uint32_t size, const void* data)
            {
                if (size == 0)
                    return;
                if (void* mapped = buffer->GetMappedData())
                    memcpy(static_cast<char*>(mapped) + offset, data, size);
                else
                    RHI_CommandList::UpdateBuffer(buffer, offset, size, data);
            };

            const auto& drops = Renderer::GetSurfaceWater().drops;
            const uint32_t drop_count           = min(static_cast<uint32_t>(drops.size()), SurfaceWater::drops_max);
            upload(GetBuffer(Renderer_Buffer::CarRainDrops), (m_frame_resource_index % renderer_draw_data_buffer_count) * SurfaceWater::drops_max * sizeof(SurfaceWaterDrop), drop_count * sizeof(SurfaceWaterDrop), drops.data());

            const auto& texels = Renderer::GetSurfaceWater().texels;
            const uint32_t texel_count            = min(static_cast<uint32_t>(texels.size()), SurfaceWater::texels_max);
            upload(GetBuffer(Renderer_Buffer::CarRainTexels), m_frame_resource_index * SurfaceWater::texels_max * sizeof(SurfaceWaterTexel), texel_count * sizeof(SurfaceWaterTexel), texels.data());

            const auto& dirt_texels      = Renderer::GetSurfaceWater().dirt_texels;
            const uint32_t dirt_count    = min(static_cast<uint32_t>(dirt_texels.size()), SurfaceWater::dirt_texels_max);
            upload(GetBuffer(Renderer_Buffer::CarDirtTexels), m_frame_resource_index * SurfaceWater::dirt_texels_max * sizeof(SurfaceDirtTexel), dirt_count * sizeof(SurfaceDirtTexel), dirt_texels.data());
            m_pass_state.surface_water.dirt_texels_staged = dirt_count;
        }
        // mark synced even when empty so later imgui/editor WriteDrawData can stage mid-frame on d3d12
        m_draw_data_gpu_synced = true;
        static bool draw_data_descriptor_set = false;
        if (!draw_data_descriptor_set)
        {
            RHI_Device::UpdateBindlessDrawData(GetBuffer(Renderer_Buffer::DrawData));
            draw_data_descriptor_set = true;
        }

        // geometry buffers, vertex pulling via bindless structured buffers
        // the bindless set is shared by every frame in flight and written in place, a buffer that grew mid session
        // must not be swapped under frames still executing, they would read the new buffer before this frame copies into it
        static uint64_t last_vertex_buffer_id   = 0;
        static uint64_t last_index_buffer_id    = 0;
        static uint64_t last_instance_buffer_id = 0;
        RHI_Buffer* current_vertex   = GeometryBuffer::GetVertexBuffer();
        RHI_Buffer* current_index    = GeometryBuffer::GetIndexBuffer();
        RHI_Buffer* current_instance = GeometryBuffer::GetInstanceBuffer();
        if (!current_instance)
        {
            current_instance = GetBuffer(Renderer_Buffer::DummyInstance);
        }
        const bool vertex_swapped   = current_vertex && current_vertex->GetObjectId() != last_vertex_buffer_id;
        const bool index_swapped    = current_index && current_index->GetObjectId() != last_index_buffer_id;
        const bool instance_swapped = current_instance && current_instance->GetObjectId() != last_instance_buffer_id;
        if ((vertex_swapped && last_vertex_buffer_id != 0) || (index_swapped && last_index_buffer_id != 0) || (instance_swapped && last_instance_buffer_id != 0))
        {
            RHI_Device::QueueWaitAll();
        }
        if (vertex_swapped)
        {
            RHI_Device::UpdateBindlessGeometryVertices(current_vertex);
            last_vertex_buffer_id = current_vertex->GetObjectId();
        }
        if (index_swapped)
        {
            RHI_Device::UpdateBindlessGeometryIndices(current_index);
            last_index_buffer_id = current_index->GetObjectId();
        }

        // global instance buffer for instanced indirect draws, falls back to dummy until the global buffer exists
        if (instance_swapped)
        {
            RHI_Device::UpdateBindlessInstances(current_instance);
            last_instance_buffer_id = current_instance->GetObjectId();
        }

        // indirect draw args, slot 0 opaque slot 1 alpha-tested, slots 2 and 3 their late ranges, layout matches VkDrawIndirectCommand on the first 16 bytes
        // index_count aliases vertex_count and is bumped 3 at a time by the triangle cull, instance_count fixed at 1
        Sb_IndirectDrawArgs draw_args[4] = {};
        for (Sb_IndirectDrawArgs& args : draw_args)
        {
            args.instance_count = 1;
        }
        RHI_Buffer* args_buffer = GetBuffer(Renderer_Buffer::IndirectDrawArgs);
        args_buffer->ResetOffset();
        args_buffer->Update(&draw_args[0], sizeof(draw_args));

        // indirect dispatch args, slot 0 opaque (or full list for vs path), slot 1 alpha for mesh path, slots 2 and 3 their late ranges
        // group_count_x is bumped by the meshlet cull, group_count_y/z fixed at 1
        Sb_IndirectDispatchArgs dispatch_args[4] = {};
        RHI_Buffer* dispatch_args_buffer         = GetBuffer(Renderer_Buffer::TriangleDispatchArgs);
        dispatch_args_buffer->ResetOffset();
        dispatch_args_buffer->Update(&dispatch_args[0], sizeof(dispatch_args));

        // single slot indirect dispatch args for the meshlet cull (phase b), group_count_x bumped by the instance cull (phase a)
        Sb_IndirectDispatchArgs instance_dispatch = {};
        instance_dispatch.group_count_y          = 1;
        instance_dispatch.group_count_z          = 1;
        RHI_Buffer* instance_dispatch_buffer     = GetBuffer(Renderer_Buffer::InstanceDispatchArgs);
        instance_dispatch_buffer->ResetOffset();
        instance_dispatch_buffer->Update(&instance_dispatch, sizeof(Sb_IndirectDispatchArgs));

        if (m_indirect_draw_count > 0)
        {
            RHI_Buffer* data_buffer = GetBuffer(Renderer_Buffer::IndirectDrawData);
            data_buffer->ResetOffset();
            data_buffer->Update(&m_indirect_draw_data[0], data_buffer->GetStride() * m_indirect_draw_count);
        }

        if (m_cull_task_count > 0)
        {
            // This frame slot has completed its previous GPU use. Grow its mapped
            // task buffer instead of dropping draws when the island fills the view.
            FrameResource& frame = m_frame_resources[m_frame_resource_index];
            if (frame.cull_tasks->GetElementCount() < m_cull_task_count)
            {
                uint32_t capacity = frame.cull_tasks->GetElementCount();
                while (capacity < m_cull_task_count) capacity *= 2;
                frame.cull_tasks = make_shared<RHI_Buffer>(RHI_Buffer_Type::Storage, sizeof(Sb_CullTask),
                    capacity, nullptr, true, (string("cull_tasks_") + to_string(m_frame_resource_index)).c_str());
                BindFrameResource(m_frame_resource_index);
            }
            RHI_Buffer* tasks_buffer = frame.cull_tasks.get();
            tasks_buffer->ResetOffset();
            tasks_buffer->Update(&m_cull_tasks[0], tasks_buffer->GetStride() * m_cull_task_count);
        }
        SP_PROFILE_CPU_END();
    }

    void Renderer::TickLogClusterOverflowRateLimited()
    {
        // rate limit, a constantly overflowing scene must not spam the log
        static double next_overflow_log_time = 0.0;
        const uint32_t overflow_count        = GetClusterOverflowCount();
        const double now                     = Timer::GetTimeSec();
        if (overflow_count > 0 && now >= next_overflow_log_time)
        {
            SP_LOG_WARNING("Clustered lighting: %u clusters exceeded the %u light cap last frame, lights were dropped", overflow_count, static_cast<uint32_t>(CLUSTER_MAX_LIGHTS));
            next_overflow_log_time = now + 5.0;
        }
    }

    const RHI_Viewport& Renderer::GetViewport()
    {
        return m_viewport;
    }

    void Renderer::SetViewport(float width, float height)
    {
        SP_ASSERT_MSG(width  != 0, "Width can't be zero");
        SP_ASSERT_MSG(height != 0, "Height can't be zero");

        if (m_viewport.width != width || m_viewport.height != height)
        {
            m_viewport.width              = width;
            m_viewport.height             = height;
            primary_view.dirty_orthographic_projection = true;
            preview_view.dirty_orthographic_projection = true;
        }
    }

    bool Renderer::RequestSecondaryView(
        Entity* camera_entity,
        Entity* render_root,
        const uint32_t width,
        const uint32_t height,
        const Renderer_SecondaryViewMode mode,
        const Renderer_SecondaryViewBackdrop backdrop
    )
    {
        if (
            !camera_entity ||
            !render_root ||
            !camera_entity->GetComponent<Camera>() ||
            width == 0 ||
            height == 0
        )
        {
            return false;
        }
        secondary_camera_request = camera_entity;
        secondary_render_root_request = render_root;
        secondary_view_width_request = width;
        secondary_view_height_request = height;
        secondary_view_mode_request = mode;
        secondary_view_backdrop_request = backdrop;
        secondary_view_request_generation++;
        secondary_view_ready = false;
        return true;
    }

    RHI_Texture* Renderer::GetSecondaryViewOutput()
    {
        return secondary_view_output.get();
    }

    bool Renderer::IsSecondaryViewReady()
    {
        return secondary_view_ready;
    }

    bool Renderer::IsSecondaryViewActive()
    {
        return secondary_render_root_active != nullptr;
    }

    bool Renderer::IsRayTracedShadowsActive()
    {
        // Rendering invariant: ray-traced shadows and raster shadow maps are
        // mutually exclusive per view. Do not add a near-cascade, foliage or
        // quality fallback alongside RT. Fix missing RT features in the RT
        // path; a hybrid mode requires explicit user approval.
        // same bit the shaders branch on, so cpu and gpu agree on which path owns the frame
        return (Renderer::view().frame.options & (1u << 2)) != 0;
    }

    bool Renderer::IsSecondaryScreenshotPending()
    {
        lock_guard<mutex> lock(screenshot_mutex);
        return secondary_capture.pending || secondary_capture.ready;
    }

    void Renderer::InvalidateSecondaryView()
    {
        secondary_camera_request = nullptr;
        secondary_render_root_request = nullptr;
        secondary_view_ready = false;
        preview_view.exposure = {};
        preview_view.taau_reset_history = true;
        secondary_view_request_generation++;

        lock_guard<mutex> lock(screenshot_mutex);
        // one already read back is being saved from its own staging copy, only a pending one is dropped
        if (secondary_capture.pending)
        {
            secondary_capture = {};
        }
    }

    uint64_t Renderer::GetSecondaryViewGeneration()
    {
        return secondary_view_ready_generation;
    }

    uint64_t Renderer::GetSecondaryViewRequestGeneration()
    {
        return secondary_view_request_generation;
    }

    Renderer_SecondaryViewMode
    Renderer::GetSecondaryViewMode()
    {
        return secondary_render_root_active
            ? secondary_view_mode_active
            : Renderer_SecondaryViewMode::Solid;
    }

    void Renderer::Pass_PreviewStudio(
        RHI_Texture* tex_out
    )
    {
        const bool non_solid =
            secondary_view_mode_active !=
            Renderer_SecondaryViewMode::Solid;
        const bool replace_sky =
            secondary_view_backdrop_active !=
            Renderer_SecondaryViewBackdrop::Sky;
        const bool recolour_wires = non_solid;

        RHI_Shader* shader_c =
            GetShader(Renderer_Shader::preview_studio_c);
        if (
            !tex_out ||
            !shader_c ||
            !shader_c->IsCompiled() ||
            (!replace_sky && !recolour_wires)
        )
        {
            return;
        }

        // display referred values, this runs after tonemapping and gamma
        Vector3 tint = Vector3(0.055f, 0.058f, 0.066f);
        switch (secondary_view_backdrop_active)
        {
            case Renderer_SecondaryViewBackdrop::Slate:
                tint = Vector3(0.22f, 0.23f, 0.25f);
                break;
            case Renderer_SecondaryViewBackdrop::Paper:
                tint = Vector3(0.86f, 0.86f, 0.88f);
                break;
            default:
                break;
        }

        // a bright backdrop needs dark wires, a dark one needs bright wires, the sky counts as
        // bright since it usually is and a dark wire is the safer read against it
        const bool wires_on_light =
            !replace_sky ||
            secondary_view_backdrop_active ==
            Renderer_SecondaryViewBackdrop::Paper;
        const float wire_mode =
            recolour_wires
                ? (wires_on_light ? 2.0f : 1.0f)
                : 0.0f;

        RHI_CommandList::BeginPass("preview_studio");
        SetCommonTextures();
        {
            RHI_CommandList::SetShader(shader_c);
            RHI_CommandList::SetTexture(Renderer_BindingsUav::tex, tex_out);
            m_pcb_pass_cpu.set(pass_preview_studio::backdrop_tint, tint);
            m_pcb_pass_cpu.set(pass_preview_studio::replace_sky, replace_sky);
            m_pcb_pass_cpu.set(pass_preview_studio::wire_mode, wire_mode);
            RHI_CommandList::Dispatch(tex_out);
        }
        RHI_CommandList::EndPass();
    }

    const Vector2& Renderer::GetResolutionRender()
    {
        return m_resolution_render;
    }

    bool Renderer::SetResolution(math::Vector2& current, uint32_t width, uint32_t height, bool recreate_resources,
                                 bool create_render, bool create_output, const char* label)
    {
        if (!RHI_Device::IsValidResolution(width, height))
        {
            SP_LOG_WARNING("%dx%d is an invalid resolution", width, height);
            return false;
        }

        if (current.x == width && current.y == height)
        {
            return false;
        }

        current.x = static_cast<float>(width);
        current.y = static_cast<float>(height);

        if (recreate_resources)
        {
            if (Renderer::view().frame.frame > 1)
            {
                bool flush = true;
                RHI_Device::QueueWaitAll(flush);
            }

            CreateRenderTargets(create_render, create_output, true);

            // age based deletion, never flush here while a command list may still reference old targets

            // invalidate gpu state after recreating the resources
            for (uint32_t i = 0; i < static_cast<uint32_t>(Renderer_RenderTarget::max); i++)
            {
                if (RHI_Texture* rt = GetRenderTarget(static_cast<Renderer_RenderTarget>(i)))
                {
                    rt->InvalidateGpuState();
                }
            }

            // recreate one shot targets after gpu resources change
            m_pass_state.brdf_lut_produced       = false;
            m_pass_state.atmosphere_lut_produced = false;
            m_pass_state.cloud_noise_produced    = false;
            m_pass_state.sky.first_frame         = true;
            m_pass_state.cloud_history.Reset();
            m_pass_state.cloud_environment.environment_dirty = true;
            m_pass_state.ssao_history.Reset();
            m_pass_state.fog_history.Reset();
            m_pass_state.particle_volume_history.Reset();
            m_taau_reset_history                 = true;

            CreateSamplers();
        }

        SP_LOG_INFO("%s resolution has been set to %dx%d", label, width, height);
        return true;
    }

    void Renderer::SetResolutionRender(uint32_t width, uint32_t height, bool recreate_resources /*= true*/)
    {
        SetResolution(m_resolution_render, width, height, recreate_resources, true, false, "Render");
    }

    const Vector2& Renderer::GetResolutionOutput()
    {
        return m_resolution_output;
    }

    void Renderer::SetResolutionOutput(uint32_t width, uint32_t height, bool recreate_resources /*= true*/)
    {
        if (SetResolution(m_resolution_output, width, height, recreate_resources, false, true, "Output"))
        {
            Display::RegisterDisplayMode(width, height, Timer::GetFpsLimit(), Display::GetId());
        }
    }

    float Renderer::GetResolutionScale()
    {
        // the secondary targets are created at the size the preview is shown at and upscale linearly
        if (secondary_render_root_active)
        {
            return 1.0f;
        }

        // stereo bakes r.resolution_scale into the eye targets and always resolves with taau, so no subrect
        if (IsStereoView())
        {
            return 1.0f;
        }

        // dlss and xess reject a render subrect below their minimum input and leave the output untouched,
        // so the effective scale stops there, the cvar keeps what the user asked for
        float scale_min = 0.0f;
        const Renderer_AntiAliasing_Upsampling upsampling_mode = cvar_antialiasing_upsampling.GetValueAs<Renderer_AntiAliasing_Upsampling>();
        if (upsampling_mode == Renderer_AntiAliasing_Upsampling::AA_Dlss_Upscale_Dlss)
        {
            scale_min = RHI_VendorTechnology::DLSS_GetResolutionScaleMin(m_resolution_render, m_resolution_output);
        }
        else if (upsampling_mode == Renderer_AntiAliasing_Upsampling::AA_Xess_Upscale_Xess)
        {
            scale_min = RHI_VendorTechnology::XeSS_GetResolutionScaleMin(m_resolution_render, m_resolution_output);
        }

        return sanitize_resolution_scale(max(cvar_resolution_scale.GetValue(), scale_min));
    }

    uint32_t Renderer::GetScaledDimension(uint32_t dimension, float scale /*= -1.0f*/)
    {
        scale = scale < 0.0f ? GetResolutionScale() : sanitize_resolution_scale(scale);
        return max(static_cast<uint32_t>(static_cast<float>(dimension) * scale), 1u);
    }

    void Renderer::RecreateRenderTargets()
    {
        if (Renderer::view().frame.frame > 1)
        {
            bool flush = true;
            RHI_Device::QueueWaitAll(flush);
        }

        CreateRenderTargets(true, true, true);
        m_pass_state.ResetResolutionHistory();
        CreateSamplers();
    }

    void Renderer::ResetTaauHistory()
    {
        m_taau_reset_history = true;
    }

    void Renderer::UpdateFrameCb_CameraAndProjectionHistory()
    {
        if (Camera* camera = Renderer::GetViewCamera())
        {
            if (view().near_plane != camera->GetNearPlane() || view().far_plane != camera->GetFarPlane())
            {
                view().near_plane                    = camera->GetNearPlane();
                view().far_plane                     = camera->GetFarPlane();
                view().dirty_orthographic_projection = true;
            }

            Renderer::view().frame.view_previous       = Renderer::view().frame.view;
            Renderer::view().frame.view                = camera->GetViewMatrix();
            Renderer::view().frame.view_inverted       = Matrix::Invert(Renderer::view().frame.view);
            Renderer::view().frame.projection_previous = Renderer::view().frame.projection;
            Renderer::view().frame.projection          = camera->GetProjectionMatrix();
            Renderer::view().frame.projection_inverted = Matrix::Invert(Renderer::view().frame.projection);
        }

        if (view().dirty_orthographic_projection)
        {
            // ortho near is 0 to avoid NaN in the [3,2] element
            Matrix projection_ortho                     = Matrix::CreateOrthographicLH(m_viewport.width, m_viewport.height, 0.0f, view().far_plane);
            Renderer::view().frame.view_projection_orthographic = Matrix::CreateLookAtLH(Vector3(0, 0, -view().near_plane), Vector3::Forward, Vector3::Up) * projection_ortho;
            view().dirty_orthographic_projection               = false;
        }
    }

    void Renderer::UpdateFrameCb_ProjectionJitter()
    {
        const Renderer_AntiAliasing_Upsampling upsampling_mode = cvar_antialiasing_upsampling.GetValueAs<Renderer_AntiAliasing_Upsampling>();

        // stereo overwrites the projection with unjittered per eye matrices, keep the advertised jitter at zero
        // so shaders do not unjitter velocities and uvs with an offset that was never applied
        if (IsStereoView() || IsPathTracingActive())
        {
            Renderer::view().jitter_offset = Vector2::Zero;
            return;
        }

        // a secondary view resolves without a temporal upscaler, a sub pixel offset that
        // nothing resolves away would just shift the preview off centre
        if (secondary_render_root_active)
        {
            Renderer::view().jitter_offset = Vector2::Zero;
            return;
        }

        if (upsampling_mode == Renderer_AntiAliasing_Upsampling::AA_Taau_Upscale_Taau)
        {
            // halton-2,3 jitter, taau pass uses it to reconstruct sub-pixel detail
            auto halton = [](uint32_t index, uint32_t base) -> float
            {
                float result = 0.0f;
                float bk     = 1.0f;
                while (index > 0)
                {
                    bk     /= static_cast<float>(base);
                    result += static_cast<float>(index % base) * bk;
                    index  /= base;
                }
                return result;
            };

            static const uint32_t phase_count = 16;
            static uint32_t phase_index       = 0;
            phase_index                       = (phase_index + 1) % phase_count;

            const float jx = halton(phase_index + 1, 2) - 0.5f;
            const float jy = halton(phase_index + 1, 3) - 0.5f;

            // jitter sized to active render area, m_resolution_render * scale, not the full target, sub pixel coverage shrinks otherwise
            const float scale    = GetResolutionScale();
            const float render_w = static_cast<float>(GetScaledDimension(static_cast<uint32_t>(m_resolution_render.x), scale));
            const float render_h = static_cast<float>(GetScaledDimension(static_cast<uint32_t>(m_resolution_render.y), scale));
            Renderer::view().jitter_offset.x = 2.0f * jx / render_w;
            Renderer::view().jitter_offset.y = -2.0f * jy / render_h;

            Renderer::view().frame.projection *= Matrix::CreateTranslation(Vector3(Renderer::view().jitter_offset.x, Renderer::view().jitter_offset.y, 0.0f));
        }
        else if (upsampling_mode == Renderer_AntiAliasing_Upsampling::AA_Xess_Upscale_Xess)
        {
            RHI_VendorTechnology::XeSS_GenerateJitterSample(&Renderer::view().jitter_offset.x, &Renderer::view().jitter_offset.y);
            Renderer::view().frame.projection *= Matrix::CreateTranslation(Vector3(Renderer::view().jitter_offset.x, Renderer::view().jitter_offset.y, 0.0f));
        }
        else if (upsampling_mode == Renderer_AntiAliasing_Upsampling::AA_Dlss_Upscale_Dlss)
        {
            RHI_VendorTechnology::DLSS_GenerateJitterSample(&Renderer::view().jitter_offset.x, &Renderer::view().jitter_offset.y);
            Renderer::view().frame.projection *= Matrix::CreateTranslation(Vector3(Renderer::view().jitter_offset.x, Renderer::view().jitter_offset.y, 0.0f));
        }
        else
        {
            Renderer::view().jitter_offset = Vector2::Zero;
        }
    }

    void Renderer::UpdateFrameCb_ViewProjectionAndCameraFields()
    {
        Renderer::view().frame.view_projection_previous = Renderer::view().frame.view_projection;
        Renderer::view().frame.view_projection          = Renderer::view().frame.view * Renderer::view().frame.projection;
        Renderer::view().frame.view_projection_inverted = Matrix::Invert(Renderer::view().frame.view_projection);

        // the camera view translates by -camera_position, so relative to it only the rotation is left
        Matrix view_rotation = Renderer::view().frame.view;
        view_rotation.m30    = 0.0f;
        view_rotation.m31    = 0.0f;
        view_rotation.m32    = 0.0f;
        Renderer::view().frame.view_projection_previous_relative = Renderer::view().frame.view_projection_relative;
        Renderer::view().frame.view_projection_relative          = view_rotation * Renderer::view().frame.projection;

        if (Camera* camera = Renderer::GetViewCamera())
        {
            Renderer::view().frame.view_projection_previous_unjittered = Renderer::view().frame.view_projection_unjittered;
            Renderer::view().frame.view_projection_unjittered          = Renderer::view().frame.view * camera->GetProjectionMatrix();
            Renderer::view().frame.camera_near                         = camera->GetNearPlane();
            Renderer::view().frame.camera_far                          = camera->GetFarPlane();
            Renderer::view().frame.camera_position_previous            = Renderer::view().frame.camera_position;
            Renderer::view().frame.camera_position                     = camera->GetEntity()->GetPosition();
            Renderer::view().frame.camera_forward                      = camera->GetEntity()->GetForward();
            Renderer::view().frame.camera_right                        = camera->GetEntity()->GetRight();
            Renderer::view().frame.camera_fov                          = camera->GetFovHorizontalRad();
            Renderer::view().frame.camera_aperture                     = camera->GetAperture();
            Renderer::view().frame.camera_last_movement_time           = (Renderer::view().frame.camera_position - Renderer::view().frame.camera_position_previous).LengthSquared() != 0.0f
                ? static_cast<float>(Timer::GetTimeSec()) : Renderer::view().frame.camera_last_movement_time;
        }
    }

    void Renderer::UpdateFrameCb_ScalarFields()
    {
        Renderer::view().frame.resolution_output     = m_resolution_output;
        Renderer::view().frame.resolution_render     = m_resolution_render;
        Renderer::view().frame.taa_jitter_previous   = Renderer::view().frame.taa_jitter_current;
        Renderer::view().frame.taa_jitter_current    = Renderer::view().jitter_offset;
        Renderer::view().frame.time                  = Timer::GetSimTimeSec();
        Renderer::view().frame.delta_time            = static_cast<float>(Timer::GetDeltaTimeSec());
        Renderer::view().frame.delta_time_real       = static_cast<float>(Timer::GetDeltaTimeRealSec());
        Renderer::view().frame.frame                 = ++view().temporal_frame;
        Renderer::view().frame.resolution_scale      = GetResolutionScale();
        Renderer::view().frame.restir_pt_scale       = cvar_restir_pt_scale.GetValue();
        // 0 = sdr, 1 = hdr10 pq, 2 = hdr scrgb (d3d12 windowed)
        Renderer::view().frame.hdr_enabled = 0.0f;
        if (RHI_SwapChain* swapchain = RHI_Device::GetSwapChain())
        {
            if (swapchain->IsHdr())
            {
                Renderer::view().frame.hdr_enabled = (swapchain->GetFormat() == RHI_Format::R16G16B16A16_Float) ? 2.0f : 1.0f;
            }
        }
        Renderer::view().frame.hdr_max_nits       = Display::GetLuminanceMax();
        Renderer::view().frame.hdr_sdr_white_nits = Display::GetSdrWhiteNits();

        // the whole hdr encode hangs off these three, log them once per change so a washed out image can be told
        // apart from a wrong nits level without a gpu capture
        {
            static float logged_mode       = -1.0f;
            static float logged_max_nits   = -1.0f;
            static float logged_white_nits = -1.0f;
            if (Renderer::view().frame.hdr_enabled        != logged_mode      ||
                Renderer::view().frame.hdr_max_nits       != logged_max_nits  ||
                Renderer::view().frame.hdr_sdr_white_nits != logged_white_nits)
            {
                logged_mode       = Renderer::view().frame.hdr_enabled;
                logged_max_nits   = Renderer::view().frame.hdr_max_nits;
                logged_white_nits = Renderer::view().frame.hdr_sdr_white_nits;
                SP_LOG_INFO(
                    "hdr encode: mode %.0f (0 sdr, 1 pq, 2 scrgb), max %.0f nits, sdr white %.0f nits, tonemapper %.0f",
                    logged_mode,
                    logged_max_nits,
                    logged_white_nits,
                    cvar_tonemapping.GetValue()
                );
            }
        }

        Renderer::view().frame.gamma              = cvar_gamma.GetValue();
        Camera* camera                    = Renderer::GetViewCamera();
        Renderer::view().frame.camera_exposure    =
            camera ?
            camera->GetExposure() :
            1.0f;
        bool camera_uses_auto_exposure =
            camera &&
            camera->GetExposureMode() == CameraExposureMode::automatic;
        Renderer::view().frame.camera_exposure_mode =
            camera_uses_auto_exposure ?
            1.0f :
            0.0f;
        Renderer::view().frame.restir_pt_light_count = static_cast<float>(m_count_active_lights);
        Renderer::view().frame.wind                  = World::GetWind();
        Renderer::view().frame.puddliness            = Weather::GetPuddliness();
        Renderer::view().frame.weather               = Vector4(Weather::GetRain(), Weather::GetWetness(), World::GetPuddliness(), Weather::GetRainPuddliness());
        {
            const Vector2 occlusion_min = Weather::GetOcclusionMin();
            const uint32_t slice        = m_frame_resource_index * Weather::occlusion_resolution * Weather::occlusion_resolution;
            Renderer::view().frame.rain_occlusion = Vector4(occlusion_min.x, occlusion_min.y, Weather::occlusion_cell_size, static_cast<float>(slice));

            Renderer::view().frame.rain_vehicle = Vector4(GetSurfaceWater().wetness, 0.0f, 0.0f, 0.0f);
            for (uint32_t plane = 0; plane < 3; plane++)
            {
                Renderer::view().frame.rain_vehicle_axis[plane] = Vector4(GetSurfaceWater().axes[plane], 0.0f);
            }

            Entity* car           = World::GetEntityById(Renderer::GetSurfaceWater().entity_id);
            const bool car_water  = car && Renderer::GetSurfaceWater().active && !secondary_render_root_active;
            Renderer::view().frame.rain_car_origin = Vector4(car ? car->GetPosition() : Vector3::Zero, Renderer::GetSurfaceWater().clock);
            Renderer::view().frame.rain_car_box    = Vector4(Renderer::GetSurfaceWater().box_min, Renderer::GetSurfaceWater().texel_size);
            Renderer::view().frame.rain_car_atlas  = Vector4(
                static_cast<float>(Renderer::GetSurfaceWater().width),
                static_cast<float>(Renderer::GetSurfaceWater().height),
                static_cast<float>((m_frame_resource_index % renderer_draw_data_buffer_count) * SurfaceWater::drops_max),
                car_water ? 1.0f : 0.0f
            );
            Renderer::view().frame.rain_car_micro = Vector4(Renderer::GetSurfaceWater().micro_life, Renderer::GetSurfaceWater().residue_mass, 0.0f, 0.0f);
            for (uint32_t face = 0; face < 6; face++)
            {
                Renderer::view().frame.rain_car_faces[face] = Renderer::GetSurfaceWater().faces[face];
            }
            // the targets are rebuilt in the car rain pass, until then they are placeholders the atlas coordinates would read far outside of
            bool car_dirt = car_water && Renderer::GetSurfaceWater().dirt_active;
            for (Renderer_RenderTarget target : { Renderer_RenderTarget::car_dirt_color, Renderer_RenderTarget::car_dirt_state, Renderer_RenderTarget::car_dirt_wear })
            {
                RHI_Texture* texture = GetRenderTarget(target);
                car_dirt             = car_dirt && texture && texture->GetWidth() == Renderer::GetSurfaceWater().width && texture->GetHeight() == Renderer::GetSurfaceWater().height;
            }
            Renderer::view().frame.car_dirt = Vector4(Renderer::GetSurfaceWater().dirt_clock, car_dirt ? 1.0f : 0.0f, 0.0f, 0.0f);
        }
        Renderer::view().frame.cloud_coverage        = Environment::GetCloudCoverageEffective();
        Renderer::view().frame.cloud_seed_offset     = World::GetCloudSeedOffset();
        {
            // match the directional light's day cycle source so stars lock to the same clock as the sun
            const bool use_real_world_time = World::GetDirectionalLight() && World::GetDirectionalLight()->GetFlag(LightFlags::RealTimeCycle);
            Renderer::view().frame.time_of_day = World::GetTimeOfDay(use_real_world_time);
            const auto& environment = World::GetEnvironment();
            Renderer::view().frame.celestial_moon = Vector4(environment.moon, environment.moon_fraction);
            Renderer::view().frame.celestial_sun = Vector4(environment.sun, environment.moon_radius);
            Renderer::view().frame.equatorial_x = Vector4(environment.equatorial_x, environment.sun_radius);
            Renderer::view().frame.equatorial_y = Vector4(environment.equatorial_y, 0);
            Renderer::view().frame.equatorial_z = Vector4(environment.equatorial_z, 0);
        }

        // fft ocean, geometry samples these to displace and shade the water surface, a preview subtree
        // sits at the origin where the sea would otherwise cut it with a waterline and underwater fog
        const Water* water = secondary_render_root_active ? nullptr : m_pass_state.ocean;
        if (water)
        {
            const float* lengths                    = water->GetCascadeLengths();
            Renderer::view().frame.ocean_cascade_length     = Vector4(lengths[0], lengths[1], lengths[2], lengths[3]);
            Renderer::view().frame.ocean_sea_level          = water->GetSeaLevel();
            Renderer::view().frame.ocean_choppiness         = water->GetChoppiness();
            Renderer::view().frame.ocean_displacement_scale = water->GetDisplacementScale();
            Renderer::view().frame.ocean_normal_strength    = water->GetNormalStrength();
            Renderer::view().frame.ocean_cascade_count      = water->GetCascadeCount();
            Renderer::view().frame.ocean_enabled            = 1.0f;
            Renderer::view().frame.ocean_turbidity          = water->GetTurbidity();
            Renderer::view().frame.ocean_caustics_intensity = water->GetCausticsIntensity();
            Renderer::view().frame.ocean_shore_mapping      = ocean_shore::get_mapping();
            Renderer::view().frame.ocean_shore_wave         = ocean_shore::get_wave();
            Renderer::view().frame.ocean_shore_swell        = ocean_shore::get_swell();
        }
        else
        {
            Renderer::view().frame.ocean_enabled    = 0.0f;
            Renderer::view().frame.ocean_shore_wave = Vector4::Zero;
        }

        Renderer::view().frame.terrain_height_mapping = Vector4::Zero;
        Renderer::view().frame.terrain_height_y       = 0.0f;
        Renderer::view().frame.terrain_height_enabled = 0.0f;
        Renderer::view().frame.terrain_maps_enabled   = 0.0f;
        if (m_pass_state.terrain_enabled && m_pass_state.terrain.height_map)
        {
            RHI_Texture* height = m_pass_state.terrain.height_map;
            if (height->GetRhiResource() &&
                height->GetResourceState() == ResourceState::PreparedForGpu)
            {
                Vector4 mapping = m_pass_state.terrain.world_mapping;
                float y         = 0.0f;
                if (Terrain* terrain = Terrain::FindActive())
                {
                    if (Entity* entity = terrain->GetEntity())
                    {
                        const Vector3 translation = entity->GetMatrix().GetTranslation();
                        mapping.x += translation.x;
                        mapping.y += translation.z;
                        y          = translation.y;
                    }
                }

                Renderer::view().frame.terrain_height_mapping = mapping;
                Renderer::view().frame.terrain_height_y       = y;
                Renderer::view().frame.terrain_height_enabled = 1.0f;
            }
        }

        Renderer::view().frame.terrain_detail_subdivisions = IsTerrainDetailReady() ? static_cast<float>(m_pass_state.terrain.detail_subdivisions) : 0.0f;

        // ground blending, the bindless slot is handed out by TickUploadMaterials which runs before
        // this, so the index is the one this frame's draws will sample
        Renderer::view().frame.terrain_blend_height   = 0.0f;
        Renderer::view().frame.terrain_blend_material = 0;
        Renderer::view().frame.terrain_blend_tiling   = 0.0f;
        if (Renderer::view().frame.terrain_height_enabled > 0.5f && m_pass_state.terrain.surface)
        {
            Renderer::view().frame.terrain_blend_height   = m_pass_state.terrain.blend_height;
            Renderer::view().frame.terrain_blend_material = m_pass_state.terrain.surface->GetIndex();
            Renderer::view().frame.terrain_blend_tiling   = m_pass_state.terrain.surface->GetProperty(MaterialProperty::TextureTilingX);
        }

        if (m_pass_state.terrain_enabled &&
            m_pass_state.terrain.map_a &&
            m_pass_state.terrain.map_b &&
            m_pass_state.terrain.map_a->GetResourceState() == ResourceState::PreparedForGpu &&
            m_pass_state.terrain.map_b->GetResourceState() == ResourceState::PreparedForGpu)
        {
            Renderer::view().frame.terrain_maps_enabled = 1.0f;
        }

        // the carpet is only drawn on the terrain surface, so it needs the heightfield mapping as well
        const PassState::GrassFar& grass_far = m_pass_state.grass_far;
        const bool grass_far_ready =
            Renderer::view().frame.terrain_height_enabled > 0.5f &&
            grass_far.prop_mask &&
            grass_far.prop_mask->GetResourceState() == ResourceState::PreparedForGpu;
        Renderer::view().frame.grass_far_tint  = grass_far_ready ? grass_far.tint : Vector4::Zero;
        Renderer::view().frame.grass_far_fade  = grass_far.fade;
        Renderer::view().frame.grass_far_gate  = grass_far.gate;
        Renderer::view().frame.grass_far_patch = grass_far.patch;
    }

    void Renderer::UpdateFrameCb_ClusterLighting()
    {
        // log(z) slicing with the camera near/far, near clamped to avoid log(0), z scale collapses when near equals far
        const float cluster_near = std::max(view().near_plane, 1e-3f);
        const float cluster_far  = std::max(view().far_plane, cluster_near + 1e-3f);
        const float log_range    = std::log(cluster_far / cluster_near);
        const float z_scale      = log_range > 1e-6f ? static_cast<float>(CLUSTER_COUNT_Z) / log_range : 0.0f;
        const float z_bias       = -std::log(cluster_near) * z_scale;

        Renderer::view().frame.cluster_count_x        = CLUSTER_COUNT_X;
        Renderer::view().frame.cluster_count_y        = CLUSTER_COUNT_Y;
        Renderer::view().frame.cluster_count_z        = CLUSTER_COUNT_Z;
        Renderer::view().frame.cluster_light_count    = m_count_active_lights;
        Renderer::view().frame.cluster_z_scale        = z_scale;
        Renderer::view().frame.cluster_z_bias         = z_bias;
        Renderer::view().frame.volumetric_light_count = m_volumetric_light_count;
        Renderer::view().frame.cluster_padding1       = 0.0f;
    }

    void Renderer::UpdateFrameCb_FeatureBits()
    {
        // bit positions are shader abi, must match common_resources.hlsl
        // a secondary view is absent from the tlas, so every ray traced feature is off for it
        const bool ray_tracing_allowed = !secondary_render_root_active;
        const bool tlas_available      = RHI_Device::IsSupportedRayTracing() && GetTopLevelAccelerationStructure() != nullptr && ray_tracing_allowed;

        // Initial preparation needs a TLAS before enabling tracing. Once one exists,
        // spawning geometry must not switch lighting modes for the entire scene.
        bool pending_blas = frame_is_loading;
        if (ray_tracing_allowed && RHI_Device::IsSupportedRayTracing() && (!frame_is_loading || tlas_available))
        {
            prepare_ray_tracing_renders();
            pending_blas = ray_tracing_pending_blas;
        }

        // Background imports also set ProgressTracker::IsLoading(). Once a TLAS
        // exists, keep lighting ownership while those jobs run: disabling ReSTIR
        // briefly exposes raster/IBL lighting and invalidates all reservoirs on
        // both transitions. Resident AS updates continue without changing the estimator.
        // World teardown destroys the TLAS, so initial/replacement worlds still
        // wait for loading and the first resident BLAS set to finish.
        const bool ray_tracing_ready = ray_tracing_allowed &&
            (tlas_available || (!frame_is_loading && !pending_blas));
        Renderer::view().frame.set_bit(cvar_ray_traced_reflections.GetValueAs<bool>() && ray_tracing_ready, 1 << 0);
        Renderer::view().frame.set_bit(cvar_ssao.GetValueAs<bool>() && !IsStereoView(),                     1 << 1);
        Renderer::view().frame.set_bit(cvar_ray_traced_shadows.GetValueAs<bool>() && tlas_available && ray_tracing_ready, 1 << 2);
        Renderer::view().frame.set_bit(cvar_restir_pt.GetValueAs<bool>() && ray_tracing_ready,              1 << 3);

        // the reservoirs hold a different integrand with and without direct light, so switching
        // ownership must drop history instead of letting stale samples resolve into the new one, a secondary
        // view traces nothing and gets the primary's bits back afterwards, so it changes no ownership
        const bool restir_direct = cvar_restir_pt.GetValueAs<bool>() && cvar_restir_pt_direct.GetValueAs<bool>() && ray_tracing_ready;
        if (!secondary_render_root_active && restir_direct != ((Renderer::view().frame.options & (1u << 4)) != 0))
        {
            m_pass_state.restir.history_invalid    = true;
            m_pass_state.restir.accumulation_valid = false;
        }
        Renderer::view().frame.set_bit(restir_direct, 1 << 4);
    }

    void Renderer::UpdateFrameCb_StereoXr()
    {
        const uint32_t multiview_previous = Renderer::view().frame.is_multiview;
        if (IsStereoView())
        {
            Renderer::view().frame.view                     = Xr::GetViewMatrix(0);
            Renderer::view().frame.view_inverted            = Matrix::Invert(Renderer::view().frame.view);
            Renderer::view().frame.projection               = Xr::GetProjectionMatrix(0);
            Renderer::view().frame.projection_inverted      = Matrix::Invert(Renderer::view().frame.projection);
            Renderer::view().frame.view_projection          = Renderer::view().frame.view * Renderer::view().frame.projection;
            Renderer::view().frame.view_projection_inverted = Matrix::Invert(Renderer::view().frame.view_projection);
            Renderer::view().frame.camera_position          = Renderer::view().frame.view_inverted.GetTranslation();

            // vr does not jitter the projection, jittered equals unjittered, replace mono fields so consumers see eye consistent data
            Renderer::view().frame.view_projection_previous_unjittered = Renderer::view().view_projection_previous_unjittered_left;
            Renderer::view().frame.view_projection_unjittered          = Renderer::view().frame.view_projection;

            Renderer::view().frame.view_right                                = Xr::GetViewMatrix(1);
            Renderer::view().frame.view_inverted_right                       = Matrix::Invert(Renderer::view().frame.view_right);
            Renderer::view().frame.projection_right                          = Xr::GetProjectionMatrix(1);
            Renderer::view().frame.projection_inverted_right                 = Matrix::Invert(Renderer::view().frame.projection_right);
            Renderer::view().frame.view_projection_right                     = Renderer::view().frame.view_right * Renderer::view().frame.projection_right;
            Renderer::view().frame.view_projection_inverted_right            = Matrix::Invert(Renderer::view().frame.view_projection_right);
            Renderer::view().frame.view_projection_previous_right            = Renderer::view().view_projection_previous_right;
            Renderer::view().frame.view_projection_unjittered_right          = Renderer::view().frame.view_projection_right;
            Renderer::view().frame.view_projection_previous_unjittered_right = Renderer::view().view_projection_previous_right;
            Renderer::view().frame.camera_position_right                     = Renderer::view().frame.view_inverted_right.GetTranslation();
            Renderer::view().frame.is_multiview                              = 1;

            // record per-eye view projection so next frame's right-eye history exists, mono path tracks left eye via shared view_projection
            // eye views are not built around camera_position, fold it in with doubles so the km scale terms cancel exactly
            auto relative_to = [](const Matrix& m, const Vector3& origin) -> Matrix
            {
                Matrix result = m;
                result.m30 = static_cast<float>(static_cast<double>(origin.x) * m.m00 + static_cast<double>(origin.y) * m.m10 + static_cast<double>(origin.z) * m.m20 + m.m30);
                result.m31 = static_cast<float>(static_cast<double>(origin.x) * m.m01 + static_cast<double>(origin.y) * m.m11 + static_cast<double>(origin.z) * m.m21 + m.m31);
                result.m32 = static_cast<float>(static_cast<double>(origin.x) * m.m02 + static_cast<double>(origin.y) * m.m12 + static_cast<double>(origin.z) * m.m22 + m.m32);
                result.m33 = static_cast<float>(static_cast<double>(origin.x) * m.m03 + static_cast<double>(origin.y) * m.m13 + static_cast<double>(origin.z) * m.m23 + m.m33);
                return result;
            };
            Renderer::view().frame.view_projection_relative                = relative_to(Renderer::view().frame.view_projection, Renderer::view().frame.camera_position);
            Renderer::view().frame.view_projection_previous_relative       = relative_to(Renderer::view().frame.view_projection_previous_unjittered, Renderer::view().frame.camera_position_previous);
            Renderer::view().frame.view_projection_relative_right          = relative_to(Renderer::view().frame.view_projection_right, Renderer::view().frame.camera_position);
            Renderer::view().frame.view_projection_previous_relative_right = relative_to(Renderer::view().frame.view_projection_previous_right, Renderer::view().frame.camera_position_previous);

            Renderer::view().view_projection_previous_right           = Renderer::view().frame.view_projection_right;
            Renderer::view().view_projection_previous_unjittered_left = Renderer::view().frame.view_projection;
        }
        else
        {
            Renderer::view().frame.is_multiview                              = 0;
            Renderer::view().frame.view_projection_previous_right            = Matrix::Identity;
            Renderer::view().frame.view_projection_unjittered_right          = Matrix::Identity;
            Renderer::view().frame.view_projection_previous_unjittered_right = Matrix::Identity;
            Renderer::view().view_projection_previous_right                         = Matrix::Identity;
            Renderer::view().view_projection_previous_unjittered_left               = Matrix::Identity;
        }

        // entering or leaving stereo invalidates taau history, projection setup changes
        if (multiview_previous != Renderer::view().frame.is_multiview)
        {
            m_taau_reset_history = true;
        }
    }

    void Renderer::UpdateFrameCb_RadialBlurHubs()
    {
        uint32_t count = 0;
        unordered_map<uint32_t, uint32_t> written_masks;

        // Grows the analytic wheel cylinder (radius, half width) that the shader uses to blur
        // the gaps between spokes. Face centres of a disc's mesh box lie on its rim and its axis,
        // so they measure the disc under any spin, unlike the corners or a world-space box.
        auto expand_extents = [](Render* render, uint32_t index)
        {
            const BoundingBox& box = render->GetBoundingBoxMesh();
            if (box.IsInfinite())
                return;
            const Matrix& transform = render->GetEntity()->GetMatrix();
            const Vector3 box_center = box.GetCenter();
            const Vector3 box_extents = box.GetExtents();
            const Vector3 pivot = Vector3(Renderer::view().frame.radial_blur_hubs[index].x, Renderer::view().frame.radial_blur_hubs[index].y, Renderer::view().frame.radial_blur_hubs[index].z);
            const Vector3 axis = Vector3(Renderer::view().frame.radial_blur_axes[index].x, Renderer::view().frame.radial_blur_axes[index].y, Renderer::view().frame.radial_blur_axes[index].z);
            Vector4& extents = Renderer::view().frame.radial_blur_extents[index];
            for (uint32_t face = 0; face < 6; face++)
            {
                Vector3 offset = Vector3::Zero;
                const float sign = (face & 1) ? -1.0f : 1.0f;
                if (face < 2)
                    offset.x = box_extents.x * sign;
                else if (face < 4)
                    offset.y = box_extents.y * sign;
                else
                    offset.z = box_extents.z * sign;
                const Vector3 d = transform * (box_center + offset) - pivot;
                const float axial = d.Dot(axis);
                extents.x = max(extents.x, (d - axis * axial).Length());
                extents.y = max(extents.y, fabsf(axial));
            }
        };

        for (Entity* entity : render_entities())
        {
            Render* render = entity->GetComponent<Render>();
            if (!render || !render->IsVisible())
                continue;

            Material* material = render->GetMaterial();
            if (!render->GetMotionBlurId() || !material ||
                (!render->GetMotionBlurGroup() && material->GetProperty(MaterialProperty::MotionBlurRadial) == 0.0f))
                continue;

            if (auto written = written_masks.find(render->GetMotionBlurId()); written != written_masks.end())
            {
                expand_extents(render, written->second);
                continue;
            }
            if (count >= 64)
                continue;

            Entity* motion_entity = render->GetMotionBlurGroup() ? World::GetEntityById(render->GetMotionBlurGroup()) : entity;
            if (!motion_entity)
                continue;
            const Matrix& current = motion_entity->GetMatrix();
            const Matrix& previous = motion_entity->GetMatrixPrevious();
            if (!current.IsFinite() || !previous.IsFinite())
                continue;

            Vector3 axis;
            float angle;
            if (render->HasMotionBlurAngularVelocity())
            {
                axis = render->GetMotionBlurAngularVelocity();
                if (!axis.IsFinite())
                    continue;
                angle = axis.Length() * Renderer::view().frame.delta_time;
            }
            else
            {
                // Generic animated meshes have only pose history; physics wheels supply actual spin.
                Quaternion delta = current.GetRotation() * previous.GetRotation().Inverse();
                angle = 2.0f * acosf(min(fabs(delta.w), 1.0f));
                axis = Vector3(delta.x, delta.y, delta.z) * (delta.w < 0.0f ? -1.0f : 1.0f);
            }
            if (!Engine::IsFlagSet(EngineMode::Playing) || Engine::IsFlagSet(EngineMode::Paused) || angle < 0.0001f || axis.LengthSquared() < 1e-12f)
                continue;
            axis.Normalize();

            // Transform the same local pivot in both frames; world AABB centers drift during rotation.
            const Vector3 local_center = render->HasMotionBlurAngularVelocity() ? render->GetMotionBlurPivotLocal() : render->GetBoundingBoxMesh().GetCenter();
            const Vector3 center = current * local_center;
            Renderer::view().frame.radial_blur_hubs[count] = Vector4(center.x, center.y, center.z, angle);
            Renderer::view().frame.radial_blur_axes[count] = Vector4(axis.x, axis.y, axis.z, static_cast<float>(render->GetMotionBlurId()));
            Renderer::view().frame.radial_blur_extents[count] = Vector4(0.0f, 0.0f, 0.0f, 1.0f);
            expand_extents(render, count);
            written_masks[render->GetMotionBlurId()] = count;
            count++;
        }
        Renderer::view().frame.radial_blur_hub_count = static_cast<float>(count);
    }

    void Renderer::UpdateFrameConstantBuffer()
    {
        UpdateFrameCb_CameraAndProjectionHistory();
        UpdateFrameCb_ProjectionJitter();
        UpdateFrameCb_ViewProjectionAndCameraFields();
        UpdateFrameCb_ScalarFields();
        UpdateFrameCb_ClusterLighting();
        UpdateFrameCb_FeatureBits();
        UpdateFrameCb_StereoXr();
        UpdateFrameCb_RadialBlurHubs();
        UpdateFrameCb_VegetationInteraction();
        UpdateFrameCb_TerrainDeformation();
        UpdateFrameCb_OceanWake();

        // a secondary view renders a different camera into the primary frame, keeping the
        // primary history here means every pixel reports a huge velocity, which reads as
        // motion blur and taa smear over the whole preview
        const bool camera_cut = camera_cut_pending && !secondary_render_root_active;
        camera_cut_pending    = camera_cut_pending && secondary_render_root_active;
        if (camera_cut)
        {
            m_taau_reset_history = true;
            m_pass_state.ResetCameraHistory();
        }
        if (secondary_render_root_active || camera_cut)
        {
            Renderer::view().frame.view_previous                       = Renderer::view().frame.view;
            Renderer::view().frame.projection_previous                 = Renderer::view().frame.projection;
            Renderer::view().frame.view_projection_previous            = Renderer::view().frame.view_projection;
            Renderer::view().frame.view_projection_previous_unjittered = Renderer::view().frame.view_projection_unjittered;
            Renderer::view().frame.camera_position_previous            = Renderer::view().frame.camera_position;
            Renderer::view().frame.taa_jitter_previous                 = Renderer::view().frame.taa_jitter_current;
            Renderer::view().frame.view_projection_previous_relative       = Renderer::view().frame.view_projection_relative;
            Renderer::view().frame.view_projection_previous_relative_right = Renderer::view().frame.view_projection_relative_right;
        }

        // emissive triangle nee pool, must precede the cb upload because it writes the count
        // into Renderer::view().frame, the buffer upload itself piggybacks on the same cmd_list
        BuildEmissiveTriangleNeePool();

        GetBuffer(Renderer_Buffer::ConstantFrame)->Update(&Renderer::view().frame);
    }

    void Renderer::BuildEmissiveTriangleNeePool()
    {
        // skip when restir off, the buffer stays at whatever data it had previously, the count
        // is set to zero so the shader treats the pool as empty regardless of buffer contents
        // the pool persists across frames and is only rebuilt when an emitter changes, pulling lod 0
        // geometry of every emitter each frame cost seconds per frame on large worlds
        static uint64_t   pool_signature = 0;
        static RHI_Buffer* pool_buffer   = nullptr;
        static uint32_t   pool_count     = 0;

        // a secondary view does not path trace, and signing its subtree here would read as a scene change
        // to the primary on both the way in and the way out, clearing the primary's reservoirs each time
        if (secondary_render_root_active)
        {
            Renderer::view().frame.restir_pt_emissive_tri_count = 0.0f;
            return;
        }

        if (!cvar_restir_pt.GetValueAs<bool>())
        {
            Renderer::view().frame.restir_pt_emissive_tri_count = 0.0f;
            m_pass_state.restir.history_invalid = true;
            pool_signature = 0;
            return;
        }

        const uint64_t fnv_basis  = 14695981039346656037ull;
        uint64_t scene_signature  = fnv_basis;
        uint64_t motion_signature = fnv_basis;
        uint64_t emitter_signature = fnv_basis;
        auto mix = [](uint64_t& signature, uint64_t value) { signature = (signature ^ value) * 1099511628211ull; };
        auto mix_float = [&mix](uint64_t& signature, float value)
        {
            uint32_t bits = 0;
            memcpy(&bits, &value, sizeof(bits));
            mix(signature, bits);
        };

        // authored emitters only, matching the bit 15 flag in UpdateMaterials
        // an emission texture is deliberately not accepted here, the radiance below is derived
        // from the flat material color and cannot see the texture, so an imported asset that
        // merely ships a black emission map would enter as a full brightness emitter, that both
        // injects light from surfaces that emit nothing and floods the pool past its cap, the
        // closest hit shader samples those textures properly so brdf sampling already covers them
        auto emitter_material = [](Entity* entity, Render*& render_out) -> Material*
        {
            if (!is_secondary_view_entity(entity))
            {
                return nullptr;
            }
            render_out = entity->GetComponent<Render>();
            Material* material = render_out ? render_out->GetMaterial() : nullptr;
            return is_pooled_emitter_material(material) ? material : nullptr;
        };

        // This pool represents constant radiance on non-instanced meshes. Textured
        // emitters carry no pooled flag, so ray hits keep their emission and they are
        // simply left out. An instanced mesh shares the pooled flag with every copy the
        // pool cannot enumerate, so one of those leaves all emission to BRDF sampling,
        // otherwise hits on the missing copies would be suppressed.
        bool pool_representable = true;
        for (Entity* entity : render_entities())
        {
            if (!is_secondary_view_entity(entity) || !entity->GetComponent<Render>())
            {
                continue;
            }

            mix(scene_signature, entity->GetObjectId());
            mix(motion_signature, entity->GetTransformRevision());
            mix(motion_signature, entity->GetChildDataRevision());

            Render* render     = nullptr;
            Material* material = emitter_material(entity, render);
            if (!material)
            {
                continue;
            }

            if (render->HasInstancing())
            {
                pool_representable = false;
            }

            mix(emitter_signature, entity->GetObjectId());
            mix(emitter_signature, entity->GetTransformRevision());
            mix(emitter_signature, reinterpret_cast<uintptr_t>(render->GetMesh()));
            mix(emitter_signature, render->GetSubMeshIndex());
            mix_float(emitter_signature, material->GetProperty(MaterialProperty::EmissiveFromAlbedo));
            mix_float(emitter_signature, material->GetProperty(MaterialProperty::ColorR));
            mix_float(emitter_signature, material->GetProperty(MaterialProperty::ColorG));
            mix_float(emitter_signature, material->GetProperty(MaterialProperty::ColorB));
        }
        if (!cvar_restir_pt_emissive_pool.GetValueAs<bool>())
        {
            pool_representable = false;
        }
        mix(emitter_signature, pool_representable ? 1u : 0u);

        // a change in the set of renderables fades the reservoirs out, motion only restarts the progressive
        // accumulation, a moving car would otherwise leave restir without temporal reuse every frame
        // worlds that stream geometry around the camera change the set on most frames while walking, so
        // clearing here left restir with no temporal reuse, reuse re-traces visibility and new surfaces
        // have no history of their own, so only radiance cached from removed geometry goes stale
        if (scene_signature != m_pass_state.restir.scene_signature)
        {
            m_pass_state.restir.accumulation_valid  = false;
            m_pass_state.restir.history_fade_frames = restir_history_fade_frames;
        }
        if (motion_signature != m_pass_state.restir.motion_signature)
        {
            m_pass_state.restir.accumulation_valid = false;
        }
        m_pass_state.restir.scene_signature  = scene_signature;
        m_pass_state.restir.motion_signature = motion_signature;

        RHI_Buffer* emissive_triangles_buffer = GetBuffer(Renderer_Buffer::EmissiveTriangles);
        if (emitter_signature == pool_signature && emissive_triangles_buffer == pool_buffer)
        {
            Renderer::view().frame.restir_pt_emissive_tri_count = static_cast<float>(pool_count);
            return;
        }

        // Rebuilding changes triangle indices, source PDFs and cached emission.
        // A confidence fade cannot make an old endpoint valid in the new pool.
        // Reset history even when only the backing buffer changed: that also
        // invalidates the pool represented by the cached endpoint descriptors.
        m_pass_state.restir.accumulation_valid = false;
        m_pass_state.restir.history_invalid = true;

        // statics avoid heap thrash on rebuilds, the capacity ratchets up to the largest pool seen so far
        static vector<Sb_EmissiveTriangle>      tris;
        static vector<uint32_t>                 indices;
        static vector<RHI_Vertex_PosTexNorTan>  vertices;
        tris.clear();

        // flickering fixtures and streaming chunks rebuild the pool many times a second, and cpu
        // geometry is usually evicted, so restoring it per emitter per rebuild dropped frames to
        // single digits, the local space triangles of each mesh are read once and only transformed
        struct local_triangles
        {
            vector<Vector3> positions;
            uint64_t        rebuild_seen = 0;
        };
        static unordered_map<uint64_t, local_triangles> geometry_cache;
        static uint64_t rebuild_index = 0;
        rebuild_index++;

        for (Entity* entity : render_entities())
        {
            if (!pool_representable)
            {
                break;
            }

            Render* render     = nullptr;
            Material* material = emitter_material(entity, render);
            if (!material)
            {
                continue;
            }

            Vector3 emission(
                clamp(material->GetProperty(MaterialProperty::ColorR), 0.0f, 1.0f),
                clamp(material->GetProperty(MaterialProperty::ColorG), 0.0f, 1.0f),
                clamp(material->GetProperty(MaterialProperty::ColorB), 0.0f, 1.0f)
            );

            // nits calibration matching light_composition, otherwise emitters glow on screen but bounce no light
            const float luminous_efficacy = lighting::lighting_luminous_efficacy;
            const float nits              = material->GetProperty(MaterialProperty::EmissiveFromAlbedo) * lighting::lighting_emissive_nits_from_albedo;
            emission *= nits / luminous_efficacy;

            float emission_lum = 0.299f * emission.x + 0.587f * emission.y + 0.114f * emission.z;
            if (emission_lum <= 0.0f)
            {
                continue;
            }

            Mesh* mesh = render->GetMesh();
            if (!mesh)
            {
                continue;
            }
            const uint64_t geometry_key = (mesh->GetObjectId() * 1099511628211ull) ^ render->GetSubMeshIndex();
            local_triangles& local = geometry_cache[geometry_key];
            if (local.positions.empty())
            {
                // lod 0 geometry, flattened to three positions per triangle, a mesh still loading
                // comes back empty and is read again on the next rebuild
                indices.clear();
                vertices.clear();
                render->GetGeometry(&indices, &vertices);
                if (!indices.empty() && !vertices.empty() && (indices.size() % 3u) == 0)
                {
                    local.positions.reserve(indices.size());
                    for (size_t i = 0; i + 2 < indices.size(); i += 3)
                    {
                        const uint32_t i0 = indices[i + 0];
                        const uint32_t i1 = indices[i + 1];
                        const uint32_t i2 = indices[i + 2];
                        if (i0 >= vertices.size() || i1 >= vertices.size() || i2 >= vertices.size())
                        {
                            continue;
                        }
                        local.positions.emplace_back(vertices[i0].pos[0], vertices[i0].pos[1], vertices[i0].pos[2]);
                        local.positions.emplace_back(vertices[i1].pos[0], vertices[i1].pos[1], vertices[i1].pos[2]);
                        local.positions.emplace_back(vertices[i2].pos[0], vertices[i2].pos[1], vertices[i2].pos[2]);
                    }
                }
            }
            local.rebuild_seen = rebuild_index;
            if (local.positions.empty())
            {
                continue;
            }

            const Matrix& transform = entity->GetMatrix();

            const size_t tri_count = local.positions.size() / 3u;
            for (size_t i = 0; i < tri_count; i++)
            {
                const Vector3 p0 = transform * local.positions[i * 3u + 0u];
                const Vector3 p1 = transform * local.positions[i * 3u + 1u];
                const Vector3 p2 = transform * local.positions[i * 3u + 2u];

                Vector3 e1     = p1 - p0;
                Vector3 e2     = p2 - p0;
                Vector3 cross  = Vector3::Cross(e1, e2);
                float   nx_len = cross.Length();
                if (nx_len <= 1e-12f)
                {
                    continue;
                }

                float   area   = nx_len * 0.5f;
                Vector3 normal = cross / nx_len;

                Sb_EmissiveTriangle tri = {};
                tri.v0       = p0;
                tri.v1       = p1;
                tri.v2       = p2;
                tri.normal   = normal;
                tri.area     = area;
                tri.emission = emission;
                tri.weight   = area * emission_lum;
                tri.cdf      = 0.0f;
                tris.push_back(tri);
            }
        }

        for (auto it = geometry_cache.begin(); it != geometry_cache.end();)
        {
            it = it->second.rebuild_seen == rebuild_index ? next(it) : geometry_cache.erase(it);
        }

        // build the prefix sum over picking weight, the last entry's cdf is the total weight
        // and the shader normalizes a uniform xi against it to area sample a triangle
        float total_weight = 0.0f;
        for (auto& t : tris)
        {
            total_weight += t.weight;
            t.cdf         = total_weight;
        }

        pool_count = 0;
        if (pool_representable && !tris.empty() && total_weight > 0.0f)
        {
            EnsureEmissiveTriangleCapacity(static_cast<uint32_t>(tris.size()));
            emissive_triangles_buffer = GetBuffer(Renderer_Buffer::EmissiveTriangles);
            const uint64_t upload_size = static_cast<uint64_t>(tris.size()) * sizeof(Sb_EmissiveTriangle);
            if (RHI_Device::IsRecording())
            {
                RHI_CommandList::UpdateBuffer(emissive_triangles_buffer, 0, upload_size, tris.data(), false);
                m_cross_queue_sync.gi_upload_hazard = true;
            }
            else
            {
                emissive_triangles_buffer->UploadSubRegion(tris.data(), 0, upload_size);
            }
            pool_count = static_cast<uint32_t>(tris.size());
        }
        pool_signature = emitter_signature;
        pool_buffer    = emissive_triangles_buffer;
        Renderer::view().frame.restir_pt_emissive_tri_count = static_cast<float>(pool_count);
    }

    void Renderer::EnableGpuScatter(
        uint32_t slot,
        Mesh* mesh,
        Material* material,
        RHI_Texture* terrain_heightmap,
        const GpuScatterParams& params,
        RHI_Texture* terrain_prop_mask
    )
    {
        if (slot >= renderer_max_gpu_scatter_slots)
        {
            return;
        }

        PassState::GpuScatterSlot& state = m_pass_state.gpu_scatter[slot];

        // every required input must be present, otherwise the populate compute will read garbage
        // from a freed heightmap, the indirect args would point past the global geometry buffer, etc
        if (!mesh || !material || !terrain_heightmap)
        {
            state.enabled = false;
            return;
        }

        state.mesh      = mesh;
        state.material  = material;
        state.heightmap = terrain_heightmap;
        state.prop_mask = terrain_prop_mask;
        state.params    = params;
        state.enabled   = true;

        // Render::SetMaterial normally derives these, gpu scatter has no Render component and the blade vs divides by zero without them
        {
            const std::vector<RHI_Vertex_PosTexNorTan>& mesh_vertices = mesh->GetVertices();
            if (!mesh_vertices.empty())
            {
                const SubMesh& sm = mesh->GetSubMesh(0);
                if (!sm.lods.empty())
                {
                    const MeshLod& lod0 = sm.lods[0];
                    float min_x =  FLT_MAX;
                    float max_x = -FLT_MAX;
                    float min_y =  FLT_MAX;
                    float max_y = -FLT_MAX;
                    const uint32_t vb_end = lod0.vertex_offset + lod0.vertex_count;
                    for (uint32_t v = lod0.vertex_offset; v < vb_end && v < mesh_vertices.size(); v++)
                    {
                        const RHI_Vertex_PosTexNorTan& vert = mesh_vertices[v];
                        min_x = std::min(min_x, vert.pos[0]);
                        max_x = std::max(max_x, vert.pos[0]);
                        min_y = std::min(min_y, vert.pos[1]);
                        max_y = std::max(max_y, vert.pos[1]);
                    }
                    material->SetProperty(MaterialProperty::WorldWidth,  max_x - min_x);
                    material->SetProperty(MaterialProperty::WorldHeight, max_y - min_y);
                }
            }
        }

        // bake static portions of the per-lod indirect args from the mesh's lod layout in the global geometry buffer
        // instance_count is dynamic and written each frame by grass_indirect_args.hlsl, the rest stays frozen here
        const SubMesh& sub      = mesh->GetSubMesh(0);
        const uint32_t lod_cap  = renderer_max_gpu_scatter_lods;
        const uint32_t lod_have = static_cast<uint32_t>(sub.lods.size());
        const uint32_t lod_use  = std::min(lod_cap, lod_have);
        if (lod_have == 0)
        {
            state.enabled = false;
            return;
        }

        for (uint32_t i = 0; i < lod_cap; i++)
        {
            // the mesh may have fewer than 3 lods on load races, fall back to lod 0 so we never index out of bounds
            const uint32_t lod_index = i < lod_use ? i : 0u;
            const MeshLod& lod       = sub.lods[lod_index];

            Sb_IndirectDrawArgs& args = state.indirect_args_static[i];
            args.index_count          = lod.index_count;
            args.instance_count       = 0; // filled by grass_indirect_args_c each frame
            args.first_index          = mesh->GetGlobalIndexOffset() + lod.index_offset;
            args.vertex_offset        = static_cast<int32_t>(mesh->GetGlobalVertexOffset() + lod.vertex_offset);
            args.first_instance       = 0; // lod_base is fed via push constant instead, sv_instanceid is per-draw zero-based
        }

        state.args_baked = false; // commit on the first frame after enable

        // the scatter material belongs to no entity, only the material upload hands it a bindless slot,
        // so it has to be asked for or the instances sample whatever material already sits at that index
        m_pass_state.bindless_materials_dirty = true;
    }

    void Renderer::DisableGpuScatter(uint32_t slot)
    {
        if (slot >= renderer_max_gpu_scatter_slots)
        {
            return;
        }

        m_pass_state.gpu_scatter[slot]        = PassState::GpuScatterSlot();
        m_pass_state.bindless_materials_dirty = true;
    }

    void Renderer::DisableGpuScatter()
    {
        m_pass_state.grass_interaction.valid = false;
        m_pass_state.grass_interaction.wheel_valid.fill(false);
        for (uint32_t slot = 0; slot < renderer_max_gpu_scatter_slots; slot++)
        {
            m_pass_state.gpu_scatter[slot] = PassState::GpuScatterSlot();
        }

        m_pass_state.bindless_materials_dirty = true;
    }

    bool Renderer::IsGpuScatterEnabled()
    {
        for (uint32_t slot = 0; slot < renderer_max_gpu_scatter_slots; slot++)
        {
            if (m_pass_state.gpu_scatter[slot].enabled)
            {
                return true;
            }
        }

        return false;
    }

    void Renderer::QueueTextureRegionUpload(const shared_ptr<RHI_Texture>& texture, uint32_t x, uint32_t y, uint32_t width, uint32_t height, const void* data)
    {
        if (!texture || !data || width == 0 || height == 0)
        {
            return;
        }

        QueuedTextureUpload upload;
        upload.texture = texture;
        upload.x       = x;
        upload.y       = y;
        upload.width   = width;
        upload.height  = height;
        const std::byte* bytes = static_cast<const std::byte*>(data);
        upload.data.assign(bytes, bytes + static_cast<size_t>(width) * height * texture->GetBytesPerPixel());

        lock_guard lock(queued_texture_uploads_mutex);
        queued_texture_uploads.push_back(move(upload));
    }

    void Renderer::SetTerrain(const TerrainParams& params)
    {
        if (!params.surface)
        {
            return;
        }

        // only pushed when something actually changed, the terrain never calls this per frame
        m_pass_state.terrain                  = params;
        m_pass_state.terrain_enabled          = true;
        m_pass_state.bindless_materials_dirty = true;
    }

    bool Renderer::IsTerrainDetailReady()
    {
        if (!m_pass_state.terrain_enabled || m_pass_state.terrain.detail_subdivisions == 0)
        {
            return false;
        }

        const RHI_Texture* table = m_pass_state.terrain.detail_table;
        const RHI_Texture* atlas = m_pass_state.terrain.detail_atlas;
        return table && atlas &&
            table->GetResourceState() == ResourceState::PreparedForGpu && table->GetRhiSrv() &&
            atlas->GetResourceState() == ResourceState::PreparedForGpu && atlas->GetRhiSrv();
    }

    void Renderer::ClearTerrain(Material* surface)
    {
        // only the terrain that registered may clear, otherwise a second terrain being destroyed
        // takes the live one's binding with it
        if (surface && m_pass_state.terrain.surface != surface)
        {
            return;
        }

        m_pass_state.terrain                  = TerrainParams();
        m_pass_state.terrain_enabled          = false;
        m_pass_state.bindless_materials_dirty = true;
    }

    void Renderer::EnableOcean(
        Water* water,
        const bool spectrum_dirty
    )
    {
        if (!water)
        {
            return;
        }

        const bool ocean_changed = m_pass_state.ocean != water;
        if (ocean_changed)
        {
            m_pass_state.ocean_displacement_produced = false;
            m_pass_state.ocean_history.Reset();
            ResetOceanHeightReadback();
        }

        m_pass_state.ocean = water;
        if (ocean_changed || spectrum_dirty)
        {
            m_pass_state.ocean_spectrum_dirty = true;
            if (!ocean_changed)
            {
                ResetOceanHeightReadback();
            }
        }
    }

    void Renderer::DisableOcean(Water* water)
    {
        if (m_pass_state.ocean != water)
        {
            return;
        }

        m_pass_state.ocean                       = nullptr;
        m_pass_state.ocean_spectrum_dirty        = true;
        m_pass_state.ocean_displacement_produced = false;
        m_pass_state.ocean_history.Reset();
        ResetOceanHeightReadback();
        ocean_shore::reset();
        OceanWake::Clear();
    }

    bool Renderer::IsOceanEnabled()
    {
        return m_pass_state.ocean != nullptr;
    }

    void Renderer::OnFullScreenToggled()
    {
        static float    width_previous_viewport  = 0;
        static float    height_previous_viewport = 0;
        static uint32_t width_previous_output    = 0;
        static uint32_t height_previous_output   = 0;

        if (Window::IsFullScreen())
        {
            uint32_t width  = Window::GetWidthInPixels();
            uint32_t height = Window::GetHeightInPixels();

            width_previous_viewport  = m_viewport.width;
            height_previous_viewport = m_viewport.height;
            width_previous_output    = static_cast<uint32_t>(GetResolutionOutput().x);
            height_previous_output   = static_cast<uint32_t>(GetResolutionOutput().y);

            SetViewport(static_cast<float>(width), static_cast<float>(height));
            SetResolutionOutput(width, height);
        }
        else
        {
            SetViewport(width_previous_viewport, height_previous_viewport);
            SetResolutionOutput(width_previous_output, height_previous_output);
        }

        Input::SetMouseCursorVisible(!Window::IsFullScreen());
    }

    void Renderer::DrawString(const char* text, const Vector2& position_screen_percentage)
    {
        if (shared_ptr<Font>& font = GetFont())
        {
            font->AddText(text, position_screen_percentage);
        }
    }

    void Renderer::DrawCinematicText(const char* text, const Vector2& position_screen_percentage, float alpha, bool large, const Vector3& color)
    {
        shared_ptr<Font>& font = GetFont(large ? Renderer_Font::CinematicLarge : Renderer_Font::CinematicSmall);
        if (!font || !text || alpha <= 0.0f)
        {
            return;
        }

        // centered on the position, one line per newline, with a soft drop shadow so it reads over a bright sky
        // sized in 1080p pixels and scaled to the output height
        const float width_px    = static_cast<float>(GetViewport().width);
        const float height_px   = static_cast<float>(GetViewport().height);
        const float unit        = height_px / 1080.0f;
        const float scale       = unit * (large ? 84.0f : 40.0f) / font->GetPixelSize();
        const float line_height = font->GetLineHeight() * scale * 1.15f;
        uint32_t line_count     = 1;
        for (const char* p = text; *p; ++p)
        {
            line_count += *p == '\n' ? 1 : 0;
        }
        float y               = height_px * position_screen_percentage.y - line_height * static_cast<float>(line_count) * 0.5f;
        const float shadow    = (large ? 4.0f : 2.5f) * unit;
        const Color shadow_color(0.0f, 0.0f, 0.0f, clamp(alpha, 0.0f, 1.0f) * 0.55f);
        const Color text_color(color.x, color.y, color.z, clamp(alpha, 0.0f, 1.0f));

        string line;
        const char* cursor = text;
        while (true)
        {
            const char* end = cursor;
            while (*end && *end != '\n')
            {
                ++end;
            }
            line.assign(cursor, end);
            const float x = width_px * position_screen_percentage.x - font->GetTextWidth(line.c_str(), false, 0.0f, scale) * 0.5f;
            font->AddText(line.c_str(), Vector2(x + shadow, y + shadow), shadow_color, false, 0.0f, scale);
            font->AddText(line.c_str(), Vector2(x, y), text_color, false, 0.0f, scale);
            y += line_height;
            if (!*end)
            {
                break;
            }
            cursor = end + 1;
        }
    }

    void Renderer::DrawCinematicTitle(const CinematicTitle& title)
    {
        if (!title.text)
        {
            return;
        }
        if (title.style == CinematicTitleStyle::Plain)
        {
            const float fade  = max(title.fade, 1e-3f);
            const float alpha = title.fade > 0.0f ? min({ 1.0f, title.age / fade, title.remaining / fade }) : 1.0f;
            DrawCinematicText(title.text, title.position, alpha, title.large, title.color);
            return;
        }

        shared_ptr<Font>& font_back     = GetFont(Renderer_Font::CinematicSmall);
        shared_ptr<Font>& font_large    = GetFont(Renderer_Font::CinematicLarge);
        shared_ptr<Font>& font_label    = GetFont(Renderer_Font::CinematicLabel);
        shared_ptr<Font>& font_headline = GetFont(Renderer_Font::CinematicHeadline);
        if (!font_back || !font_large || !font_label || !font_headline)
        {
            return;
        }

        auto saturate     = [](float x) { return clamp(x, 0.0f, 1.0f); };
        auto ease_out     = [](float x) { x = clamp(x, 0.0f, 1.0f); return 1.0f - (1.0f - x) * (1.0f - x) * (1.0f - x); };
        auto smooth       = [](float x) { x = clamp(x, 0.0f, 1.0f); return x * x * (3.0f - 2.0f * x); };
        const float fade  = max(title.fade, 0.05f);
        const float enter = title.age;
        const float life  = saturate(title.age / max(title.age + title.remaining, 1e-3f));
        const float out   = smooth(title.remaining / fade);

        // the first line leads, the rest is the tagline or body
        string headline = title.text;
        string body;
        if (const size_t split = headline.find('\n'); split != string::npos)
        {
            body     = headline.substr(split + 1);
            headline = headline.substr(0, split);
        }
        const string kicker = title.kicker ? title.kicker : "";

        // laid out in 1080p pixels and scaled to the output height
        const float width_px  = static_cast<float>(GetViewport().width);
        const float height_px = static_cast<float>(GetViewport().height);
        const float unit      = height_px / 1080.0f;
        const Color white(title.color.x, title.color.y, title.color.z, 1.0f);
        const Color accent(title.accent.x, title.accent.y, title.accent.z, 1.0f);
        auto with_alpha = [](Color color, float alpha) { color.a = clamp(alpha, 0.0f, 1.0f); return color; };

        if (title.style == CinematicTitleStyle::Trailer)
        {
            // every element arrives a beat after the one before it and they all leave together
            const float in_kicker  = smooth((enter - 0.0f) / fade);
            const float in_main    = smooth((enter - fade * 0.25f) / fade);
            const float in_rule    = ease_out((enter - fade * 0.45f) / (fade * 1.4f));
            const float in_tagline = smooth((enter - fade * 0.8f) / fade);

            Font& large          = *font_large;
            Font& label          = *font_label;
            const float em       = 84.0f * unit;
            const float s_main   = em / large.GetPixelSize();
            const float s_kicker = 22.0f * unit / label.GetPixelSize();
            const float s_tag    = 22.0f * unit / label.GetPixelSize();
            // the headline tracks out over its whole life, a slow push that reads as motion even on a held frame
            const float tracking = em * (0.2f + 0.06f * life + 0.12f * (1.0f - in_main));
            const float main_w   = large.GetTextWidth(headline.c_str(), false, tracking, s_main);
            const float center_x = width_px * title.position.x;
            const float center_y = height_px * title.position.y;
            const float main_h   = large.GetLineHeight() * s_main;
            const float main_top = center_y - main_h * 0.5f + (1.0f - in_main) * 12.0f * unit;

            // a soft band behind the block so it reads over a bright frame without a visible box
            {
                const float band_h = main_h * 3.4f;
                const int strips   = 32;
                for (int i = 0; i < strips; i++)
                {
                    const float f0 = static_cast<float>(i) / strips;
                    const float f1 = static_cast<float>(i + 1) / strips;
                    const float w  = sinf(3.14159265f * (f0 + f1) * 0.5f);
                    font_back->AddRect(Vector2(0.0f, center_y - band_h * 0.5f + band_h * f0), Vector2(width_px, center_y - band_h * 0.5f + band_h * f1), Color(0.0f, 0.0f, 0.0f, 0.3f * w * w * in_main * out));
                }
            }

            if (!kicker.empty())
            {
                const float k_tracking = 22.0f * unit * 0.6f;
                const float k_w        = label.GetTextWidth(kicker.c_str(), false, k_tracking, s_kicker);
                const float k_y        = main_top - label.GetLineHeight() * s_kicker - main_h * 0.1f;
                label.AddText(kicker.c_str(), Vector2(center_x - k_w * 0.5f, k_y), with_alpha(accent, in_kicker * out), false, k_tracking, s_kicker);
            }

            const float shadow = 3.0f * unit;
            large.AddText(headline.c_str(), Vector2(center_x - main_w * 0.5f, main_top + shadow), Color(0.0f, 0.0f, 0.0f, 0.35f * in_main * out), false, tracking, s_main);
            large.AddText(headline.c_str(), Vector2(center_x - main_w * 0.5f, main_top), with_alpha(white, in_main * out), false, tracking, s_main);

            // a hairline grows out from the center under the headline
            const float rule_y = main_top + main_h * 1.06f;
            const float rule_w = max(main_w * 0.3f, 140.0f * unit) * in_rule * (0.6f + 0.4f * out);
            label.AddRect(Vector2(center_x - rule_w * 0.5f, rule_y), Vector2(center_x + rule_w * 0.5f, rule_y + max(2.0f * unit, 1.0f)), with_alpha(accent, 0.9f * out * min(in_rule * 3.0f, 1.0f)));

            if (!body.empty())
            {
                const float b_tracking = 22.0f * unit * 0.45f;
                float y                = rule_y + main_h * 0.3f;
                size_t cursor          = 0;
                while (cursor <= body.size())
                {
                    const size_t end  = min(body.find('\n', cursor), body.size());
                    const string line = body.substr(cursor, end - cursor);
                    const float w     = label.GetTextWidth(line.c_str(), false, b_tracking, s_tag);
                    label.AddText(line.c_str(), Vector2(center_x - w * 0.5f, y), with_alpha(white, 0.85f * in_tagline * out), false, b_tracking, s_tag);
                    y      += label.GetLineHeight() * s_tag * 1.5f;
                    cursor  = end + 1;
                }
            }
            return;
        }

        // legend: an accent bar wipes down, the kicker and headline slide in beside it, body lines follow
        Font& label          = *font_label;
        Font& head           = *font_headline;
        const float in_bar   = ease_out(enter / (fade * 0.8f));
        const float in_kick  = ease_out((enter - fade * 0.15f) / fade);
        const float in_head  = ease_out((enter - fade * 0.3f) / fade);
        const float in_body  = ease_out((enter - fade * 0.6f) / fade);
        const float s_kick   = 20.0f * unit / label.GetPixelSize();
        const float s_head   = 50.0f * unit / head.GetPixelSize();
        const float s_body   = 25.0f * unit / label.GetPixelSize();
        const float slide    = 46.0f * unit;
        const float drift    = (1.0f - out) * -18.0f * unit; // leaving, everything eases back towards the bar
        const float left     = width_px * title.position.x;
        const float top      = height_px * title.position.y;
        const float gap      = 24.0f * unit;
        const float k_track  = 20.0f * unit * 0.5f;
        const float h_track  = 50.0f * unit * 0.03f;
        const float b_track  = 25.0f * unit * 0.02f;
        const float kick_h   = label.GetLineHeight() * s_kick;
        const float head_h   = head.GetLineHeight() * s_head;
        const float body_h   = label.GetLineHeight() * s_body * 1.3f;

        vector<string> lines;
        for (size_t cursor = 0; !body.empty() && cursor <= body.size();)
        {
            const size_t end = min(body.find('\n', cursor), body.size());
            lines.push_back(body.substr(cursor, end - cursor));
            cursor = end + 1;
        }

        float block_w        = head.GetTextWidth(headline.c_str(), false, h_track, s_head);
        const float kicker_h = kicker.empty() ? 0.0f : kick_h + 12.0f * unit;
        float block_h        = kicker_h + head_h;
        if (!kicker.empty())
        {
            block_w = max(block_w, label.GetTextWidth(kicker.c_str(), false, k_track, s_kick));
        }
        if (!lines.empty())
        {
            block_h += 8.0f * unit;
        }
        for (const string& line : lines)
        {
            block_h += body_h;
            block_w  = max(block_w, label.GetTextWidth(line.c_str(), false, b_track, s_body));
        }

        // a feathered shadow pool behind the text, dark at the text and gone at every edge, never a visible box
        {
            const float x_min  = max(left - 140.0f * unit, 0.0f);
            const float x_max  = left + gap + block_w + 220.0f * unit;
            const float pad    = 70.0f * unit;
            const float y_min  = top - pad;
            const float y_max  = top + block_h + pad;
            const int columns  = 36;
            const int rows     = 14;
            for (int r = 0; r < rows; r++)
            {
                const float v0 = static_cast<float>(r) / rows;
                const float v1 = static_cast<float>(r + 1) / rows;
                const float wv = sinf(3.14159265f * (v0 + v1) * 0.5f);
                for (int c = 0; c < columns; c++)
                {
                    const float u0 = static_cast<float>(c) / columns;
                    const float u1 = static_cast<float>(c + 1) / columns;
                    const float uc = (u0 + u1) * 0.5f;
                    // ramps in quickly from the left, stays flat behind the text, fades out slowly to the right
                    const float wu = smooth(uc / 0.15f) * (1.0f - smooth((uc - 0.55f) / 0.45f));
                    const float a  = 0.32f * wu * wv * wv * in_bar * out;
                    font_back->AddRect(Vector2(x_min + (x_max - x_min) * u0, y_min + (y_max - y_min) * v0), Vector2(x_min + (x_max - x_min) * u1, y_min + (y_max - y_min) * v1), Color(0.0f, 0.0f, 0.0f, a));
                }
            }
        }

        // the bar
        label.AddRect(Vector2(left, top + 2.0f * unit), Vector2(left + 4.0f * unit, top + 2.0f * unit + block_h * in_bar), with_alpha(accent, out));

        const float text_x = left + gap + drift;
        const float shadow = 2.5f * unit;
        float y            = top;
        if (!kicker.empty())
        {
            label.AddText(kicker.c_str(), Vector2(text_x - (1.0f - in_kick) * slide, y), with_alpha(accent, in_kick * out), false, k_track, s_kick);
            y += kicker_h;
        }
        head.AddText(headline.c_str(), Vector2(text_x - (1.0f - in_head) * slide + shadow, y + shadow), Color(0.0f, 0.0f, 0.0f, 0.4f * in_head * out), false, h_track, s_head);
        head.AddText(headline.c_str(), Vector2(text_x - (1.0f - in_head) * slide, y), with_alpha(white, in_head * out), false, h_track, s_head);
        y += head_h + 8.0f * unit;
        for (size_t i = 0; i < lines.size(); i++)
        {
            // each body line lands a little after the one above it
            const float in_line = ease_out((enter - fade * (0.6f + 0.12f * static_cast<float>(i))) / fade);
            label.AddText(lines[i].c_str(), Vector2(text_x - (1.0f - in_line) * slide * 0.6f + shadow * 0.6f, y + shadow * 0.6f), Color(0.0f, 0.0f, 0.0f, 0.35f * in_line * in_body * out), false, b_track, s_body);
            label.AddText(lines[i].c_str(), Vector2(text_x - (1.0f - in_line) * slide * 0.6f, y), with_alpha(white, 0.88f * in_line * in_body * out), false, b_track, s_body);
            y += body_h;
        }
    }

    void Renderer::SetPresentInRenderer(const bool enabled)
    {
        m_present_in_renderer = enabled;
    }

    void Renderer::EndXrFrame()
    {
        if (!Xr::IsSessionRunning())
        {
            return;
        }

        Xr::ReleaseSwapchainImage();
        Xr::EndFrame();
    }

    RHI_Api_Type Renderer::GetRhiApiType()
    {
        return RHI_Context::api_type;
    }

    uint64_t Renderer::GetFrameNumber()
    {
        return m_frame_num;
    }

    void Renderer::SetCommonTextures(uint32_t eye_layer /*= rhi_all_mips*/, bool bind_ssao /*= true*/)
    {
        // gbuffer (when eye_layer is specified, bind per-layer 2d views for compute passes)
        RHI_CommandList::SetTexture("tex_albedo",   GetRenderTarget(Renderer_RenderTarget::gbuffer_color),    rhi_all_mips, 0, eye_layer);
        RHI_CommandList::SetTexture("tex_normal",   GetRenderTarget(Renderer_RenderTarget::gbuffer_normal),   rhi_all_mips, 0, eye_layer);
        RHI_CommandList::SetTexture("tex_material", GetRenderTarget(Renderer_RenderTarget::gbuffer_material), rhi_all_mips, 0, eye_layer);
        RHI_CommandList::SetTexture("tex_emissive", GetRenderTarget(Renderer_RenderTarget::gbuffer_emissive), rhi_all_mips, 0, eye_layer);
        RHI_CommandList::SetTexture("tex_velocity", GetRenderTarget(Renderer_RenderTarget::gbuffer_velocity), rhi_all_mips, 0, eye_layer);
        RHI_CommandList::SetTexture("tex_depth",    GetRenderTarget(Renderer_RenderTarget::gbuffer_depth),    rhi_all_mips, 0, eye_layer);

        // ssao is written on async compute, skip binding it during graphics phase 2
        // stereo skips the ssao pass, bind white so left eye occlusion does not hit the right eye
        if (bind_ssao)
        {
            const bool xr_stereo = IsStereoView();
            RHI_Texture* tex_ssao = (!xr_stereo) ? GetRenderTarget(Renderer_RenderTarget::ssao) : nullptr;
            RHI_CommandList::SetTexture("tex_ssao", tex_ssao ? tex_ssao : GetStandardTexture(Renderer_StandardTexture::White));
        }
    }

    namespace
    {
        // stable lsd radix sort by key, bytes that every key shares are skipped, so short keys cost few passes
        void radix_sort_keys(vector<pair<uint64_t, uint32_t>>& keys)
        {
            const size_t count = keys.size();
            if (count < 64)
            {
                stable_sort(keys.begin(), keys.end(), [](const pair<uint64_t, uint32_t>& a, const pair<uint64_t, uint32_t>& b) { return a.first < b.first; });
                return;
            }

            static vector<pair<uint64_t, uint32_t>> scratch;
            scratch.resize(count);
            uint32_t histograms[8][256] = {};
            for (const pair<uint64_t, uint32_t>& key : keys)
            {
                for (uint32_t pass = 0; pass < 8; ++pass)
                {
                    histograms[pass][(key.first >> (pass * 8)) & 0xff]++;
                }
            }

            pair<uint64_t, uint32_t>* source      = keys.data();
            pair<uint64_t, uint32_t>* destination = scratch.data();
            for (uint32_t pass = 0; pass < 8; ++pass)
            {
                uint32_t* histogram = histograms[pass];
                if (histogram[(source[0].first >> (pass * 8)) & 0xff] == count)
                {
                    continue;
                }

                uint32_t offset = 0;
                for (uint32_t bucket = 0; bucket < 256; ++bucket)
                {
                    const uint32_t bucket_count = histogram[bucket];
                    histogram[bucket]           = offset;
                    offset                     += bucket_count;
                }
                for (size_t i = 0; i < count; ++i)
                {
                    destination[histogram[(source[i].first >> (pass * 8)) & 0xff]++] = source[i];
                }
                swap(source, destination);
            }

            if (source != keys.data())
            {
                memcpy(keys.data(), source, count * sizeof(pair<uint64_t, uint32_t>));
            }
        }

        // Reads only once the render's decal range is staged, so scene draws can be filled on worker threads.
        void fill_draw_data(Sb_DrawData& entry, const math::Matrix& transform, const math::Matrix& transform_previous, uint32_t material_index, uint32_t is_transparent, const Render* render)
        {
            entry.transform          = transform;
            entry.transform_previous = transform_previous;
            entry.motion_blur_id     = render ? render->GetMotionBlurId() | (render->GetMotionBlurGroup() ? 0x80000000u : 0u) : 0;
            entry.material_index     = material_index;
            entry.is_transparent     = is_transparent;
            entry.aabb_index         = 0;
            entry.lod_first_index    = 0;
            entry.flags              = render && render->ExcludesTerrainBlend() ? (1u << 6) : 0u;
            entry.flags             |= render && is_drops_vehicle(render->GetEntity()) ? (1u << 7) : 0u;
            entry.instance_offset    = 0;
            entry.instance_index     = 0;
            entry.lod_vertex_offset  = 0;
            entry.previous_vertex_offset = render ? GeometryBuffer::GetPreviousVertexOffset(render->GetVertexOffset()) : 0;
            entry.meshlet_vertex_base = 0;
            entry.meshlet_micro_base = 0;

            fill_uv_draw_fields_from_render(entry, render);
            fill_decal_draw_fields(entry, render);
        }
    }

    uint32_t Renderer::WriteDrawData(const math::Matrix& transform, const math::Matrix& transform_previous, uint32_t material_index, uint32_t is_transparent, const Render* render, bool is_ui)
    {
        // A full scene must still leave room for the UI projection. Otherwise
        // ImGui clears the back buffer and then drops every draw, presenting black.
        // Apply the scene limit to editor geometry too (grids and selection outlines).
        const uint32_t draw_limit = is_ui ? renderer_max_draw_calls : renderer_max_draw_calls - renderer_reserved_ui_draw_calls;
        if (m_draw_data_count >= draw_limit)
        {
            static bool logged = false;
            if (!logged)
            {
                SP_LOG_WARNING("draw data budget exhausted (%u), dropping further draws this frame (UI capacity is reserved)", draw_limit);
                logged = true;
            }
            return numeric_limits<uint32_t>::max();
        }

        uint32_t index = m_draw_data_count++;

        Sb_DrawData& entry = m_draw_data_cpu[index];
        fill_draw_data(entry, transform, transform_previous, material_index, is_transparent, render);

        // the draw data buffer is a single large allocation partitioned into per-frame regions;
        // each frame writes to its own region so there is no write-after-read race with the gpu
        uint32_t global_index = m_frame_resource_index * renderer_max_draw_calls + index;

        if (!m_draw_data_gpu_synced)
        {
            // Scene records are uploaded together before any geometry pass.
            return global_index;
        }
        RHI_Buffer* buffer = GetBuffer(Renderer_Buffer::DrawData);
        if (void* mapped = buffer->GetMappedData())
        {
            void* dst = static_cast<char*>(mapped) + global_index * sizeof(Sb_DrawData);
            memcpy(dst, &entry, sizeof(Sb_DrawData));
        }
        else
        {
            // d3d12 storage buffers are not persistently mapped, scene draws are bulk-uploaded in
            // TickUploadBindlessDependencies, imgui and editor overlays written after that must stage here
            if (RHI_Device::IsRecording())
            {
                RHI_CommandList::UpdateBuffer(buffer, global_index * sizeof(Sb_DrawData), sizeof(Sb_DrawData), &entry);
            }
        }

        return global_index;
    }

    void Renderer::UpdateMaterials()
    {
        SP_PROFILE_CPU();
        static array<Sb_Material, rhi_max_array_size> properties;
        static uint32_t previous_count = 0;
        static bool capacity_warning_logged = false;
        uint32_t count = 0;
        const uint64_t stamp = Material::NewVisitStamp();
        // Materials and textures have independent indices. Share descriptors for reused
        // textures and reserve slot 0 for absent maps, including textureless grass.
        unordered_map<RHI_Texture*, uint32_t> texture_indices;
        uint32_t texture_count = 1;
        // Keep this order in sync with common_resources_buffers.hlsl.
        static constexpr array gpu_texture_types = {
            MaterialTextureType::Color, MaterialTextureType::Normal,
            MaterialTextureType::Emission, MaterialTextureType::Packed
        };
        static_assert(gpu_texture_types.size() * Material::slots_per_texture == 16);

        auto should_decode_as_srgb = [](RHI_Texture* texture)
        {
            if (!texture)
            {
                return false;
            }

            if (texture->GetFlags() & RHI_Texture_Srgb)
            {
                return true;
            }

            if (texture->IsGrayscale())
            {
                return false;
            }

            const RHI_Format format = texture->GetFormat();
            return format == RHI_Format::R8G8B8A8_Unorm ||
                   format == RHI_Format::B8R8G8A8_Unorm ||
                   format == RHI_Format::BC1_Unorm ||
                   format == RHI_Format::BC3_Unorm ||
                   format == RHI_Format::BC7_Unorm;
        };
    
        auto update_material = [&count, &should_decode_as_srgb, &texture_indices, &texture_count, stamp](Material* material)
        {
            if (!material->Visit(stamp))
            {
                return;
            }

            if (count >= rhi_max_array_size)
            {
                material->SetIndex(0);
                if (!capacity_warning_logged)
                {
                    SP_LOG_ERROR("material bindless capacity exceeded, overflowing materials will use index 0");
                    capacity_warning_logged = true;
                }
                return;
            }

            {
                // uv state (tiling, offset, invert, rotation, world_space_uv) intentionally not uploaded here,
                // it is per-render and lives on Sb_DrawData (see WriteDrawData) and Sb_GeometryInfo for rt
                properties[count].local_width           = material->GetProperty(MaterialProperty::WorldWidth);
                properties[count].local_height          = material->GetProperty(MaterialProperty::WorldHeight);
                properties[count].emissive_strength     = material->GetProperty(MaterialProperty::EmissiveFromAlbedo);
                properties[count].color.x               = material->GetProperty(MaterialProperty::ColorR);
                properties[count].color.y               = material->GetProperty(MaterialProperty::ColorG);
                properties[count].color.z               = material->GetProperty(MaterialProperty::ColorB);
                properties[count].color.w               = material->GetProperty(MaterialProperty::ColorA);
                properties[count].roughness             = material->GetProperty(MaterialProperty::Roughness);
                properties[count].metalness             = material->GetProperty(MaterialProperty::Metalness);
                properties[count].normal                = material->GetProperty(MaterialProperty::Normal);
                properties[count].height                = material->GetProperty(MaterialProperty::Height);
                properties[count].anisotropic           = material->GetProperty(MaterialProperty::Anisotropic);
                properties[count].anisotropic_rotation  = material->GetProperty(MaterialProperty::AnisotropicRotation);
                properties[count].clearcoat             = material->GetProperty(MaterialProperty::Clearcoat);
                properties[count].clearcoat_roughness   = material->GetProperty(MaterialProperty::Clearcoat_Roughness);
                properties[count].flake_strength        = material->GetProperty(MaterialProperty::FlakeStrength);
                properties[count].flake_scale           = material->GetProperty(MaterialProperty::FlakeScale);
                properties[count].pearl_strength        = material->GetProperty(MaterialProperty::PearlStrength);
                properties[count].pearl_color.x         = material->GetProperty(MaterialProperty::PearlColorR);
                properties[count].pearl_color.y         = material->GetProperty(MaterialProperty::PearlColorG);
                properties[count].pearl_color.z         = material->GetProperty(MaterialProperty::PearlColorB);
                properties[count].pearl_color.w         = 1.0f;
                properties[count].coat_tint.x           = material->GetProperty(MaterialProperty::CoatTintR);
                properties[count].coat_tint.y           = material->GetProperty(MaterialProperty::CoatTintG);
                properties[count].coat_tint.z           = material->GetProperty(MaterialProperty::CoatTintB);
                properties[count].coat_tint.w           = material->GetProperty(MaterialProperty::CoatTintStrength);
                properties[count].iridescence           = material->GetProperty(MaterialProperty::Iridescence);
                properties[count].iridescence_thickness = material->GetProperty(MaterialProperty::IridescenceThickness);
                {
                    const float emissive_r       = material->GetProperty(MaterialProperty::EmissiveColorR);
                    const float emissive_g       = material->GetProperty(MaterialProperty::EmissiveColorG);
                    const float emissive_b       = material->GetProperty(MaterialProperty::EmissiveColorB);
                    const float emissive_scale   = std::max(material->GetProperty(MaterialProperty::EmissiveIntensity), 0.0f);
                    const bool has_emissive_tint = std::max(std::max(emissive_r, emissive_g), emissive_b) > 0.0f;
                    properties[count].emissive_color.x = (has_emissive_tint ? emissive_r : 1.0f) * emissive_scale;
                    properties[count].emissive_color.y = (has_emissive_tint ? emissive_g : 1.0f) * emissive_scale;
                    properties[count].emissive_color.z = (has_emissive_tint ? emissive_b : 1.0f) * emissive_scale;
                    properties[count].emissive_color.w = has_emissive_tint && emissive_scale > 0.0f ? 1.0f : 0.0f;
                }
                properties[count].ior                   = material->GetProperty(MaterialProperty::Ior);
                properties[count].absorption            = material->GetProperty(MaterialProperty::Absorption);
                properties[count].thickness             = material->GetProperty(MaterialProperty::Thickness);
                properties[count].sheen                 = material->GetProperty(MaterialProperty::Sheen);
                properties[count].sheen_color.x         = material->GetProperty(MaterialProperty::SheenColorR);
                properties[count].sheen_color.y         = material->GetProperty(MaterialProperty::SheenColorG);
                properties[count].sheen_color.z         = material->GetProperty(MaterialProperty::SheenColorB);
                properties[count].sheen_color.w         = material->GetProperty(MaterialProperty::SheenRoughness);
                properties[count].alpha_cutoff          = material->GetProperty(MaterialProperty::AlphaCutoff);
                properties[count].alpha_blend           = material->GetProperty(MaterialProperty::AlphaBlend);
                properties[count].subsurface_scattering = material->GetProperty(MaterialProperty::SubsurfaceScattering);
                properties[count].terrain_blend         = material->GetProperty(MaterialProperty::TerrainBlend);
                properties[count].terrain_blend_sharpness =
                    material->GetProperty(MaterialProperty::TerrainBlendSharpness);
                properties[count].terrain_coating = material->GetProperty(MaterialProperty::TerrainCoating);
                properties[count].terrain_coating_scale = material->GetProperty(MaterialProperty::TerrainCoatingScale);

                // flags
                properties[count].flags  = material->HasTextureOfType(MaterialTextureType::Height)             ? (1U << 0)  : 0;
                properties[count].flags |= material->HasTextureOfType(MaterialTextureType::Normal)             ? (1U << 1)  : 0;
                properties[count].flags |= material->HasTextureOfType(MaterialTextureType::Color)              ? (1U << 2)  : 0;
                properties[count].flags |= material->HasTextureOfType(MaterialTextureType::Roughness)          ? (1U << 3)  : 0;
                properties[count].flags |= material->HasTextureOfType(MaterialTextureType::Metalness)          ? (1U << 4)  : 0;
                properties[count].flags |= material->HasTextureOfType(MaterialTextureType::AlphaMask)          ? (1U << 5)  : 0;
                properties[count].flags |= material->HasTextureOfType(MaterialTextureType::Emission)           ? (1U << 6)  : 0;
                properties[count].flags |= material->HasTextureOfType(MaterialTextureType::Occlusion)          ? (1U << 7)  : 0;
                properties[count].flags |= material->GetProperty(MaterialProperty::IsTerrain)                  ? (1U << 8)  : 0;
                properties[count].flags |= material->GetProperty(MaterialProperty::WindAnimation)              ? (1U << 9)  : 0;
                properties[count].flags |= material->GetProperty(MaterialProperty::ColorVariationFromInstance) ? (1U << 10) : 0;
                properties[count].flags |= material->GetProperty(MaterialProperty::IsGrassBlade)               ? (1U << 11) : 0;
                properties[count].flags |= material->GetProperty(MaterialProperty::IsFlower)                   ? (1U << 12) : 0;
                properties[count].flags |= material->GetProperty(MaterialProperty::IsWater)                    ? (1U << 13) : 0;
                properties[count].flags |= material->GetProperty(MaterialProperty::Tessellation)               ? (1U << 14) : 0;
                properties[count].flags |= material->GetProperty(MaterialProperty::EmissiveFromAlbedo)         ? (1U << 15) : 0;
                properties[count].flags |= material->IsAlphaTested()                                          ? (1U << 16) : 0;
                properties[count].flags |= should_decode_as_srgb(material->GetTexture(MaterialTextureType::Color))    ? (1U << 17) : 0;
                properties[count].flags |= should_decode_as_srgb(material->GetTexture(MaterialTextureType::Emission)) ? (1U << 18) : 0;
                properties[count].flags |= material->GetProperty(MaterialProperty::MotionBlurRadial)          ? (1U << 19) : 0;
                properties[count].flags |= material->GetProperty(MaterialProperty::IsSkidMark)                ? (1U << 20) : 0;
                properties[count].flags |= material->GetProperty(MaterialProperty::IsFoliage)                 ? (1U << 21) : 0;
                properties[count].flags |= material->GetProperty(MaterialProperty::IsRoadSurface)             ? (1U << 22) : 0;
                properties[count].flags |= material->GetProperty(MaterialProperty::IsRoadPaint)               ? (1U << 23) : 0;
                properties[count].flags |= is_pooled_emitter_material(material)                                ? (1U << 24) : 0;
                properties[count].flags |= material->GetProperty(MaterialProperty::PaintPreset) > 0.0f        ? (1U << 25) : 0;
                // quarter metres in bits 26-31, read by road_half_width in common_road.hlsl
                const float road_half_width = std::min(std::max(material->GetProperty(MaterialProperty::RoadHalfWidth), 0.0f), 15.75f);
                properties[count].flags |= static_cast<uint32_t>(road_half_width * 4.0f + 0.5f) << 26;
                // keep in sync with Surface struct in common_structs.hlsl
            }
    
            // textures
            {
                for (uint32_t type = 0; type < gpu_texture_types.size(); type++)
                {
                    const MaterialTextureType texture_type = gpu_texture_types[type];

                    for (uint32_t slot = 0; slot < Material::slots_per_texture; slot++)
                    {
                        RHI_Texture* texture = material->GetTexture(texture_type, slot);
                        if (!texture)
                            continue;

                        auto it = texture_indices.find(texture);
                        if (it == texture_indices.end())
                        {
                            if (texture_count >= rhi_max_array_size)
                            {
                                if (!capacity_warning_logged)
                                {
                                    SP_LOG_ERROR("material texture descriptor capacity exceeded");
                                    capacity_warning_logged = true;
                                }
                                continue;
                            }
                            it = texture_indices.emplace(texture, texture_count).first;
                            m_bindless_textures[texture_count++] = texture;
                        }
                        properties[count].texture_indices[type * Material::slots_per_texture + slot] = it->second;
                    }
                }
            }
    
            material->SetIndex(count);

            ++count;
        };
    
        auto update_entities = [&update_material]()
        {
            for (Entity* entity : render_entities())
            {
                Render* render = entity->GetComponent<Render>();
                if (!render)
                {
                    continue;
                }

                if (Material* material = render->GetMaterial())
                {
                    update_material(material);
                }
            }
        };
    
        // slots past the previous upload were never written since their last reset
        fill(properties.begin(), properties.begin() + previous_count, Sb_Material{});
        m_bindless_textures.fill(nullptr);

        // terrain goes first so the enabled layers land as one contiguous block, the surface
        // material then only has to carry the base index and the shader can walk the rest
        // a layer whose folder was missing has a null material and is simply skipped, its weight
        // redistributes across the layers that do exist
        if (m_pass_state.terrain_enabled && m_pass_state.terrain.surface)
        {
            const TerrainParams& terrain = m_pass_state.terrain;

            uint32_t layer_base  = count;
            uint32_t layer_count = 0;
            for (uint32_t i = 0; i < terrain_layer_max; i++)
            {
                Material* layer = terrain.layer_materials[i];
                if (!layer || terrain.layer_rules[i].weight_bias <= 0.0f)
                {
                    continue;
                }

                const uint32_t index_before = count;
                update_material(layer);
                if (count == index_before)
                {
                    // already registered through an entity, which would break contiguity
                    continue;
                }

                const TerrainLayerRule& rule = terrain.layer_rules[i];
                Sb_Material& entry           = properties[index_before];

                entry.terrain_slope_range         = Vector2(rule.slope_min * math::deg_to_rad, rule.slope_max * math::deg_to_rad);
                entry.terrain_height_range        = Vector2(rule.height_min, rule.height_max);
                entry.terrain_curvature_influence = rule.curvature_influence;
                entry.terrain_flow_influence      = rule.flow_influence;
                entry.terrain_occlusion_influence = rule.occlusion_influence;
                entry.terrain_insolation_influence= rule.insolation_influence;
                entry.terrain_wear_influence      = rule.wear_influence;
                entry.terrain_deposition_influence= rule.deposition_influence;
                entry.terrain_talus_influence     = rule.talus_influence;
                entry.terrain_weight_bias         = rule.weight_bias;
                entry.terrain_tiling_scale        = rule.tiling_scale;
                entry.terrain_blend_contrast      = rule.blend_contrast;
                entry.terrain_porosity            = rule.porosity;
                entry.terrain_macro_strength      = rule.macro_strength;
                entry.terrain_flags               = rule.flags;
                if (rule.surface_cover)
                    entry.terrain_flags |= TerrainLayerFlags_Cover;
                if (rule.sand)
                    entry.terrain_flags |= TerrainLayerFlags_Sand;

                layer_count++;
            }

            // the surface material follows the block, its own entry points back at the base
            const uint32_t surface_index = count;
            update_material(terrain.surface);
            if (count != surface_index)
            {
                Sb_Material& entry = properties[surface_index];

                // the mapping is authored in terrain local xz and the shader samples with world xz,
                // same shift the height mapping and the grass get, or the biomes slide off the ground
                Vector4 world_mapping = terrain.world_mapping;
                if (Terrain* terrain_component = Terrain::FindActive())
                {
                    if (Entity* entity = terrain_component->GetEntity())
                    {
                        const Vector3 translation = entity->GetMatrix().GetTranslation();
                        world_mapping.x += translation.x;
                        world_mapping.y += translation.z;
                    }
                }

                entry.terrain_world_mapping = world_mapping;
                entry.terrain_sea_level     = terrain.sea_level;
                entry.terrain_snow_level    = terrain.snow_level;
                entry.terrain_layer_base    = layer_base;
                entry.terrain_layer_count   = layer_count;
                entry.terrain_layer_stride  = 1;
                entry.terrain_snow_amount   = terrain.snow_amount;
                entry.terrain_wetness       = terrain.wetness;
                entry.terrain_flags         = (terrain.map_a && terrain.map_b) ? TerrainLayerFlags_HasMaps : 0u;

                // quality rides in the bits above the flag range, 1 to 4 layers per pixel, and the
                // debug view sits above that, see terrain_layer_quality in shared_buffers.h
                entry.terrain_flags |= (min(max(terrain.quality, 1u), 4u) << 8);
                entry.terrain_flags |= (min(terrain.debug_view, 15u) << 12);
            }
        }

        update_entities();
        for (Entity* entity : secondary_render_entities)
        {
            if (Material* material = entity->GetComponent<Render>()->GetMaterial())
            {
                update_material(material);
            }
        }

        // a gpu scatter material is not attached to any entity, register it here so it lands in the
        // bindless table and material_index can be pushed into the scatter raster passes via the push constant
        for (const PassState::GpuScatterSlot& slot : m_pass_state.gpu_scatter)
        {
            if (slot.enabled && slot.material)
            {
                update_material(slot.material);
            }
        }

        previous_count = count;
        RHI_Buffer* buffer = Renderer::GetBuffer(Renderer_Buffer::MaterialParameters);
        RHI_CommandList::UpdateBuffer(buffer, 0, buffer->GetStride() * max(count, 1u), &properties[0]);
    }

    void Renderer::UpdateLights()
    {
        // only clear slots we wrote last frame, full 16k fill is pure cpu waste
        static uint32_t prev_light_count = 0;
        for (uint32_t i = 0; i < prev_light_count; i++)
        {
            m_bindless_lights[i] = Sb_Light();
        }

        m_count_active_lights         = 0;
        uint32_t volumetric_count     = 0;
        Light* first_directional      = nullptr;
        static array<uint32_t, rhi_max_array_size> volumetric_indices;
    
        auto fill_light = [&](Light* light_component)
        {
            const uint32_t index = m_count_active_lights++;
            
            light_component->SetIndex(index);
            Sb_Light& light_buffer_entry = m_bindless_lights[index];
    
            for (uint32_t i = 0; i < light_component->GetSliceCount(); i++)
            {
                light_buffer_entry.transform[i] = light_component->GetViewProjectionMatrix(i);
            }

            // distance based lod, directional lights are always considered effective
            const bool shadows_effective    = light_component->IsShadowEffective();
            const bool volumetric_effective = light_component->IsVolumetricEffective();

            const bool has_screen_space_shadows                  = light_component->GetLightType() == LightType::Directional &&
                                                                    shadows_effective &&
                                                                    light_component->GetFlag(LightFlags::ShadowsScreenSpace);

            light_buffer_entry.screen_space_shadow_slice_index   = has_screen_space_shadows ? light_component->GetScreenSpaceShadowsSliceIndex() : 0;
            light_buffer_entry.intensity                         = light_component->GetIntensityRadiometric();
            light_buffer_entry.range                             = light_component->GetRange();
            light_buffer_entry.angle                             = light_component->GetAngle();
            light_buffer_entry.color                             = light_component->GetColorEffective();
            light_buffer_entry.position                          = light_component->GetEntity()->GetPosition();
            light_buffer_entry.direction                         = light_component->GetEntity()->GetForward();
            light_buffer_entry.direction_right                   = light_component->GetEntity()->GetRight();
            light_buffer_entry.area_width                        = light_component->GetAreaWidth();
            light_buffer_entry.area_height                       = light_component->GetAreaHeight();
            light_buffer_entry.flags                             = 0;
            light_buffer_entry.flags                            |= light_component->GetLightType() == LightType::Directional ? (1 << 0) : 0;
            light_buffer_entry.flags                            |= light_component->GetLightType() == LightType::Point       ? (1 << 1) : 0;
            light_buffer_entry.flags                            |= light_component->GetLightType() == LightType::Spot        ? (1 << 2) : 0;
            light_buffer_entry.flags                            |= shadows_effective                                         ? (1 << 3) : 0;
            light_buffer_entry.flags                            |= has_screen_space_shadows                                  ? (1 << 4) : 0;
            light_buffer_entry.flags                            |= volumetric_effective                                      ? (1 << 5) : 0;
            light_buffer_entry.flags                            |= light_component->GetLightType() == LightType::Area        ? (1 << 6) : 0;
            light_buffer_entry.flags                            |= (light_component->GetIesSlot() & 0xFFu) << 16;
            // bit 7 is set by the caller for flare-only lights past draw distance

            // compact volumetric index list, slot 0 is skipped because the sun is already evaluated unconditionally
            if (volumetric_effective && index > 0 && volumetric_count < rhi_max_array_size)
            {
                volumetric_indices[volumetric_count++] = index;
            }

            for (uint32_t i = 0; i < 6; i++)
            {
                if (i < light_component->GetSliceCount())
                {
                    light_buffer_entry.atlas_offsets[i]      = light_component->GetAtlasOffset(i);
                    light_buffer_entry.atlas_scales[i]       = light_component->GetAtlasScale(i);
                    const math::Rectangle& rect              = light_component->GetAtlasRectangle(i);
                    light_buffer_entry.atlas_texel_sizes[i] = rect.IsDefined() ? Vector2(1.0f / rect.width, 1.0f / rect.height) : Vector2::Zero;
                }
                else
                {
                    light_buffer_entry.atlas_offsets[i]      = Vector2::Zero;
                    light_buffer_entry.atlas_scales[i]       = Vector2::Zero;
                    light_buffer_entry.atlas_texel_sizes[i] = Vector2::Zero;
                }
            }
        };
    
        // directional light always goes in slot 0
        for (Entity* entity : light_entities())
        {
            if (!is_secondary_view_entity(entity))
            {
                continue;
            }
            if (Light* light_component = entity->GetComponent<Light>())
            {
                if (light_component->GetLightType() == LightType::Directional)
                {
                    first_directional = light_component;
    
                    // slot 0 is always the sun, even if disabled
                    fill_light(light_component);
                    if (!light_component->GetEntity()->GetActive())
                    {
                        m_bindless_lights[0].intensity = 0.0f;
                    }
                    break;
                }
            }
        }
    
        // remaining lights
        Camera* camera = Renderer::GetViewCamera();
        const Vector3 camera_pos = camera ? camera->GetEntity()->GetPosition() : Vector3::Zero;
        const float flare_max_distance = 2000.0f; // flare-only lights stay visible this far past the lighting draw distance
        const float flare_max_distance_sq = flare_max_distance * flare_max_distance;
        // Indirect transport can reach lights outside the camera frustum and
        // raster draw distance. Keep their physical range, but not camera culling.
        const bool retain_transport_lights = cvar_restir_pt.GetValueAs<bool>() || IsPathTracingActive();

        for (Entity* entity : light_entities())
        {
            if (!is_secondary_view_entity(entity))
            {
                continue;
            }
            if (Light* light_component = entity->GetComponent<Light>())
            {
                if (light_component == first_directional)
                {
                    continue;
                }
    
                light_component->SetIndex(numeric_limits<uint32_t>::max());
    
                if (!light_component->GetEntity()->GetActive())
                {
                    continue;
                }
    
                if (light_component->GetIntensityRadiometric() <= 0.0f)
                {
                    continue;
                }

                if (m_count_active_lights >= rhi_max_array_size)
                {
                    continue;
                }

                const bool within_draw_distance = retain_transport_lights || light_component->IsActiveByDistance();
                if (!within_draw_distance)
                {
                    // past lighting draw distance, keep the light only for distant coronas
                    if (flare_max_distance <= 0.0f || !camera)
                    {
                        continue;
                    }

                    const float distance_sq = Vector3::DistanceSquared(light_component->GetEntity()->GetPosition(), camera_pos);
                    if (distance_sq > flare_max_distance_sq)
                    {
                        continue;
                    }

                    // point test so a tiny lighting aabb does not frustum-cull a visible distant bulb
                    const Vector3 light_pos = light_component->GetEntity()->GetPosition();
                    const BoundingBox flare_bounds(light_pos - Vector3(1.0f, 1.0f, 1.0f), light_pos + Vector3(1.0f, 1.0f, 1.0f));
                    if (!camera->IsInViewFrustum(flare_bounds))
                    {
                        continue;
                    }

                    fill_light(light_component);
                    m_bindless_lights[light_component->GetIndex()].flags |= (1u << 7);
                    continue;
                }
    
                if (!retain_transport_lights && camera && !camera->IsInViewFrustum(light_component->GetBoundingBox()))
                {
                    continue;
                }
    
                fill_light(light_component);
            }
        }

        // nrd sigma has one history per slot, pick the strongest local shadowed lights
        {
            uint32_t chosen[nrd_local_shadow_max];
            float scores[nrd_local_shadow_max];
            uint32_t chosen_count = 0;
            for (uint32_t i = 1; i < m_count_active_lights; i++)
            {
                const uint32_t flags = m_bindless_lights[i].flags;
                if ((flags & (1u << 3)) == 0 || (flags & (1u << 0)) != 0)
                {
                    continue;
                }

                float score = m_bindless_lights[i].intensity;
                if (flags & (1u << 6))
                {
                    score *= 100.0f;
                }

                uint32_t insert = chosen_count;
                for (uint32_t s = 0; s < chosen_count; s++)
                {
                    if (score > scores[s])
                    {
                        insert = s;
                        break;
                    }
                }

                if (insert >= nrd_local_shadow_max)
                {
                    continue;
                }

                uint32_t last = min(chosen_count, static_cast<uint32_t>(nrd_local_shadow_max) - 1);
                for (uint32_t s = last; s > insert; s--)
                {
                    chosen[s] = chosen[s - 1];
                    scores[s] = scores[s - 1];
                }
                chosen[insert] = i;
                scores[insert] = score;
                if (chosen_count < nrd_local_shadow_max)
                {
                    chosen_count++;
                }
            }

            for (uint32_t s = 0; s < chosen_count; s++)
            {
                m_bindless_lights[chosen[s]].flags |= (s + 1) << 8;
            }
        }

        // Keep the empty editor usable before a world is loaded. An empty visible-light list
        // in an authored world is legitimate (all lights may be culled, disabled or absent).
        // Adding an unshadowed sun there lights enclosed dark rooms and changes the sky as
        // the camera turns away from the last visible local light.
        if (World::GetFilePath().empty() && light_entities().empty() &&
            !first_directional && m_count_active_lights == 0)
        {
            Sb_Light& sun         = m_bindless_lights[0];
            sun.color             = Color(1.0f, 1.0f, 1.0f, 1.0f);
            sun.intensity         = 85000.0f / 683.0f; // lux to radiometric, matches Light::GetIntensityRadiometric
            sun.direction         = Vector3(0.35f, -0.82f, -0.45f).Normalized();
            sun.direction_right   = Vector3(0.79f, 0.0f, 0.61f).Normalized();
            sun.flags             = 1 << 0; // directional, deliberately without shadows or volumetrics
            m_count_active_lights = 1;
        }

        // gpu upload
        RHI_Buffer* buffer = GetBuffer(Renderer_Buffer::LightParameters);


        // Slot 0 is also read directly by the sky and primary-light shaders. Clear it on
        // the GPU when the last light disappears instead of leaving the previous sun live.
        if (m_count_active_lights == 0)
        {
            m_bindless_lights[0] = Sb_Light();
            // Atmosphere shaders still use this direction even at zero intensity.
            m_bindless_lights[0].direction       = Vector3(0.0f, -1.0f, 0.0f);
            m_bindless_lights[0].direction_right = Vector3(1.0f, 0.0f, 0.0f);
        }
        // Progressive GI must react to changes in light intensity, placement and shape.
        // only the fields that shape emitted radiance are compared, the shadow matrices and atlas
        // placement follow the camera, comparing them wiped all restir history on every camera move
        // a light that changes restarts the progressive accumulation but keeps the reservoirs, the
        // temporal confidence cap ages stale radiance out within a few frames, a change in the light set
        // (a light culled, switched off or added) fades them out faster, point and spot samples find their
        // light again by position when its index moved, see restir_resolve_dirac_light, clearing instead
        // restarted every pixel from one sample whenever a light left the frustum
        // a secondary view uploads its own studio lights, those are not a change to the primary's set
        if (cvar_restir_pt.GetValueAs<bool>() && !secondary_render_root_active)
        {
            struct light_transport
            {
                Color    color;
                Vector3  position;
                float    intensity;
                Vector3  direction;
                float    range;
                float    angle;
                uint32_t flags;
                float    area_width;
                float    area_height;
            };
            auto transport_of = [](const Sb_Light& light)
            {
                light_transport transport = {};
                transport.color       = light.color;
                transport.position    = light.position;
                transport.intensity   = light.intensity;
                transport.direction   = light.direction;
                transport.range       = light.range;
                transport.angle       = light.angle;
                transport.flags       = light.flags;
                transport.area_width  = light.area_width;
                transport.area_height = light.area_height;
                return transport;
            };

            static vector<light_transport> previous_lights;
            const uint32_t count = max(m_count_active_lights, 1u);
            if (previous_lights.size() != count)
            {
                m_pass_state.restir.accumulation_valid  = false;
                m_pass_state.restir.history_fade_frames = restir_history_fade_frames;
            }
            else
            {
                for (uint32_t i = 0; i < count; i++)
                {
                    const light_transport current = transport_of(m_bindless_lights[i]);
                    if (memcmp(&current, &previous_lights[i], sizeof(light_transport)) != 0)
                    {
                        m_pass_state.restir.accumulation_valid = false;
                        break;
                    }
                }
            }
            previous_lights.resize(count);
            for (uint32_t i = 0; i < count; i++)
            {
                previous_lights[i] = transport_of(m_bindless_lights[i]);
            }
        }
        RHI_CommandList::UpdateBuffer(buffer, 0, buffer->GetStride() * max(m_count_active_lights, 1u), &m_bindless_lights[0]);

        // upload the compact volumetric light index list, count flows through buffer_frame.volumetric_light_count
        m_volumetric_light_count = volumetric_count;
        if (volumetric_count > 0)
        {
            RHI_Buffer* vol_buffer = GetBuffer(Renderer_Buffer::VolumetricLightIndices);
            RHI_CommandList::UpdateBuffer(vol_buffer, 0, vol_buffer->GetStride() * volumetric_count, &volumetric_indices[0]);
        }

        prev_light_count = m_count_active_lights;
    }

    void Renderer::UpdateBoundingBoxes()
    {
        SP_ASSERT(m_draw_calls_prepass_count + m_indirect_render_count <= renderer_max_aabbs);
        // the slots were written by draw preparation, prepass draws first, then one per indirect render,
        // the indirect slot is the one its draw records carry in aabb_index, so the cull shader reads it

        // upload covers both the prepass region and the trailing indirect region, the indirect aabb slots start at m_draw_calls_prepass_count
        const uint32_t total_aabb_count = m_draw_calls_prepass_count + m_indirect_render_count;
        if (total_aabb_count > 0)
        {
            RHI_Buffer* buffer         = GetBuffer(Renderer_Buffer::AABBs);
            uint32_t frame_byte_offset = m_frame_resource_index * renderer_max_aabbs * static_cast<uint32_t>(sizeof(Sb_Aabb));
            uint32_t upload_size       = static_cast<uint32_t>(sizeof(Sb_Aabb)) * total_aabb_count;
            RHI_CommandList::UpdateBuffer(buffer, frame_byte_offset, upload_size, &m_bindless_aabbs[0]);
        }
    }

    void Renderer::UpdateDrawCalls_ResetCounts()
    {
        m_decal_data.clear();
        m_decal_ranges.clear();
        m_draw_call_count           = 0;
        m_draw_calls_prepass_count  = 0;
        m_draw_data_count           = 0;
        m_draw_data_gpu_synced      = false;
        m_indirect_draw_count       = 0;
        m_indirect_render_count     = 0;
        m_cull_task_count           = 0;
        m_transparents_present      = false;
        m_tessellated_draws.clear();
        m_tessellated_gpu_culled    = false;
    }

    void Renderer::UpdateDrawCalls_CollectAndSort()
    {
        SP_PROFILE_CPU();
        // Ray-traced shadows use the TLAS exclusively. Raster casters are
        // only needed for the shadow-map fallback (including secondary views).
        const bool shadow_maps_required = !IsRayTracedShadowsActive() && !GetViewLightEntities().empty();
        const Vector3 cull_camera_position = GetViewCamera() ? GetViewCamera()->GetEntity()->GetPosition() : Vector3::Zero;
        auto needs_shadow_caster = [&](const RenderSceneData& data)
        {
            return shadow_maps_required && (data.flags & RenderFlags::CastsShadows) &&
                Vector3::DistanceSquared(cull_camera_position,
                    data.bounding_box.GetClosestPoint(cull_camera_position)) <= data.max_distance_shadow * data.max_distance_shadow;
        };
        // the gpu culls tessellated draws itself, so they are kept off screen too
        const bool tessellation_gpu_driven = cvar_tessellation_gpu_driven.GetValueAs<bool>();
        auto is_gpu_culled_tessellated = [tessellation_gpu_driven](const Material* material)
        {
            return tessellation_gpu_driven && !material->IsTransparent() && material->GetProperty(MaterialProperty::Tessellation) > 0.0f;
        };

        // Candidates are classified on the workers, so the serial staging below reads one compact list.
        enum : uint8_t
        {
            candidate_drawn   = 1 << 0, // visible, or kept off screen for a shadow cascade or the gpu tessellation cull
            candidate_decals  = 1 << 1, // receiver data is staged even off screen, a reflection may still see it
            candidate_grouped = 1 << 2  // shares one motion blur stencil value with its group (all wheel parts)
        };
        struct Candidate
        {
            Render* render = nullptr;
            uint8_t flags  = 0;
        };
        struct Candidates { vector<Candidate> renders; bool transparent = false; };
        static array<Candidates, 32> batches;
        for (auto& batch : batches)
        {
            batch.renders.clear();
            batch.transparent = false;
        }
        const auto& entries = render_scene_data();
        SP_PROFILE_CPU_START("draw_collect");
        const uint32_t jobs = entries.size() >= 256 ? static_cast<uint32_t>(batches.size()) : 1u;
        auto collect = [&](uint32_t first, uint32_t last)
        {
            auto& batch = batches[first];
            const size_t end = entries.size() * last / jobs;
            for (size_t i = entries.size() * first / jobs; i < end; ++i)
            {
                prefetch_scene_data(entries, i + 8, end);
                const RenderSceneData& data = *entries[i];
                if (!data.active.load(memory_order_relaxed) || !data.mesh || !data.material) continue;
                // meshes published by a loader thread after this frame's geometry upload are drawn from the next frame
                if (!data.mesh->IsGpuResident()) continue;
                Render* render = data.render;
                if (!data.is_visible && !needs_shadow_caster(data) &&
                    (!data.has_decals || !data.instances.empty()) && !is_gpu_culled_tessellated(data.material)) continue;

                Material* material = render->GetMaterial();
                uint8_t flags      = 0;
                if (render->IsVisible() || needs_shadow_caster(render->GetSceneData()) || is_gpu_culled_tessellated(material))
                {
                    flags |= candidate_drawn;
                }
                if (!render->GetDecals().empty() && !render->HasInstancing())
                {
                    flags |= candidate_decals;
                }
                if (render->GetMotionBlurGroup())
                {
                    flags |= candidate_grouped;
                }
                // off screen glass would push the reflection trace behind lighting for nothing
                batch.transparent |= render->IsVisible() && material->IsTransparent();
                batch.renders.push_back({ render, flags });
            }
        };
        if (jobs == 1) collect(0, 1);
        else ThreadPool::ParallelLoop(collect, jobs);
        SP_PROFILE_CPU_END();

        SP_PROFILE_CPU_START("draw_stage");
        // Merge in source order so capacity limits choose the same geometry. Only decal
        // receivers and motion groups touch shared staging here.
        // Motion IDs must be assigned before either CPU or indirect draw records are written.
        // Normal finite half-float patterns encode 30719 IDs exactly; overflow stays unclassified.
        // Groups take the first IDs, every other draw takes a unique one after them in the write below.
        constexpr uint32_t motion_id_max     = 30719;
        constexpr uint32_t motion_id_ungrouped = UINT32_MAX;
        uint32_t motion_group_count = 0;
        pmr::unordered_map<uint64_t, uint32_t> motion_groups(FrameAllocator::GetResource());
        static vector<Render*> accepted;
        static vector<uint32_t> accepted_motion;
        accepted.clear();
        accepted_motion.clear();
        for (const auto& batch : batches)
        {
            m_transparents_present |= batch.transparent;
            for (const Candidate& candidate : batch.renders)
            {
                Render* render = candidate.render;
                if (candidate.flags & candidate_decals)
                {
                    Sb_DrawData decal_range;
                    fill_decal_draw_fields(decal_range, render);
                }
                if (!(candidate.flags & candidate_drawn))
                {
                    continue;
                }

                uint32_t motion = motion_id_ungrouped;
                if (candidate.flags & candidate_grouped)
                {
                    uint32_t& group_id = motion_groups.try_emplace(render->GetMotionBlurGroup(), 0).first->second;
                    if (group_id == 0)
                    {
                        group_id = ++motion_group_count;
                    }
                    motion = group_id <= motion_id_max ? group_id : 0;
                }
                accepted.push_back(render);
                accepted_motion.push_back(motion);
            }
        }
        SP_PROFILE_CPU_END();
        SP_PROFILE_CPU_START("draw_write");

        // A full scene must still leave room for the UI projection.
        const uint32_t draw_data_limit = renderer_max_draw_calls - renderer_reserved_ui_draw_calls;
        const uint32_t draw_call_room  = renderer_max_draw_calls - min(m_draw_call_count, renderer_max_draw_calls);
        const uint32_t draw_data_room  = draw_data_limit - min(m_draw_data_count, draw_data_limit);
        const uint32_t write_count     = min(static_cast<uint32_t>(accepted.size()), min(draw_call_room, draw_data_room));
        if (write_count < accepted.size() && write_count == draw_data_room)
        {
            static bool logged = false;
            if (!logged)
            {
                SP_LOG_WARNING("draw data budget exhausted (%u), dropping further draws this frame (UI capacity is reserved)", draw_data_limit);
                logged = true;
            }
        }

        // opaque before transparent, then by material, then by distance (front to back, transparent back to front)
        auto draw_sort_key = [](bool transparent, uint32_t material_index, float distance_squared)
        {
            uint32_t distance_bits = 0;
            memcpy(&distance_bits, &distance_squared, sizeof(distance_bits));
            distance_bits = transparent ? ~distance_bits : distance_bits;
            return (static_cast<uint64_t>(transparent) << 63) | (static_cast<uint64_t>(material_index & 0x7fffffffu) << 32) | distance_bits;
        };
        static vector<pair<uint64_t, uint32_t>> sort_keys;

        const uint32_t draw_call_base = m_draw_call_count;
        const uint32_t draw_data_base = m_draw_data_count;
        m_draw_call_count += write_count;
        m_draw_data_count += write_count;
        sort_keys.resize(m_draw_call_count);
        for (uint32_t i = 0; i < draw_call_base; ++i)
        {
            const Renderer_DrawCall& dc = m_draw_calls[i];
            sort_keys[i] = { draw_sort_key(dc.is_transparent, dc.render->GetMaterial()->GetIndex(), dc.distance_squared), i };
        }
        auto write_draws = [&](uint32_t first, uint32_t last)
        {
            for (uint32_t i = first; i < last; ++i)
            {
                Render* render     = accepted[i];
                Entity* entity     = render->GetEntity();
                Material* material = render->GetMaterial();
                const uint32_t data_index = draw_data_base + i;

                const uint32_t motion = accepted_motion[i] != motion_id_ungrouped ? accepted_motion[i] : motion_group_count + 1 + i;
                render->SetMotionBlurId(motion <= motion_id_max ? motion : 0);

                fill_draw_data(m_draw_data_cpu[data_index], entity->GetMatrix(), matrix_previous_for_velocity(entity), material->GetIndex(), material->IsTransparent() ? 1 : 0, render);

                Renderer_DrawCall& draw_call = m_draw_calls[draw_call_base + i];
                draw_call.render             = render;
                draw_call.material_id        = material->GetObjectId();
                draw_call.is_transparent     = material->IsTransparent();
                draw_call.is_alpha_tested    = material->IsAlphaTested();
                // Off-screen casters no longer receive a per-frame visibility tick.
                draw_call.distance_squared   = Vector3::DistanceSquared(cull_camera_position,
                    render->GetBoundingBox().GetClosestPoint(cull_camera_position));
                draw_call.lod_index          = render->GetLodIndex();
                draw_call.camera_visible     = render->IsVisible();
                draw_call.instance_index     = 0;
                draw_call.instance_count     = render->GetInstanceCount();
                draw_call.draw_data_index    = m_frame_resource_index * renderer_max_draw_calls + data_index;
                sort_keys[draw_call_base + i] = { draw_sort_key(draw_call.is_transparent, material->GetIndex(), draw_call.distance_squared), draw_call_base + i };
            }
        };
        if (write_count >= 512)
        {
            ThreadPool::ParallelLoop(write_draws, write_count);
        }
        else
        {
            write_draws(0, write_count);
        }
        SP_PROFILE_CPU_END();
        SP_PROFILE_CPU_START("draw_sort");

        // sorting packed keys and gathering once is far cheaper than swapping whole draw calls
        radix_sort_keys(sort_keys);
        static vector<Renderer_DrawCall> sorted_draw_calls;
        sorted_draw_calls.resize(m_draw_call_count);
        for (uint32_t i = 0; i < m_draw_call_count; ++i)
        {
            sorted_draw_calls[i] = m_draw_calls[sort_keys[i].second];
        }
        copy(sorted_draw_calls.begin(), sorted_draw_calls.end(), m_draw_calls.begin());
        SP_PROFILE_CPU_END();
    }

    void Renderer::UpdateDrawCalls_BuildPrepass()
    {
        SP_PROFILE_CPU();
        // opaque before alpha tested, then front to back
        static vector<pair<uint64_t, uint32_t>> prepass_keys;
        prepass_keys.clear();
        for (uint32_t i = 0; i < m_draw_call_count; ++i)
        {
            const Renderer_DrawCall& dc = m_draw_calls[i];
            if (!dc.is_transparent && dc.camera_visible)
            {
                uint32_t distance_bits = 0;
                memcpy(&distance_bits, &dc.distance_squared, sizeof(distance_bits));
                prepass_keys.push_back({ (static_cast<uint64_t>(dc.is_alpha_tested) << 32) | distance_bits, i });
            }
        }
        radix_sort_keys(prepass_keys);

        // the prepass aabbs lead the bindless aabb buffer, the slot is the prepass draw index
        m_draw_calls_prepass_count = static_cast<uint32_t>(prepass_keys.size());
        auto gather = [&](uint32_t first, uint32_t last)
        {
            for (uint32_t i = first; i < last; ++i)
            {
                const Renderer_DrawCall& dc = m_draw_calls[prepass_keys[i].second];
                m_draw_calls_prepass[i]     = dc;
                const BoundingBox& aabb     = dc.render->GetBoundingBox();
                m_bindless_aabbs[i].min     = aabb.GetMin();
                m_bindless_aabbs[i].max     = aabb.GetMax();
            }
        };
        if (m_draw_calls_prepass_count >= 512)
        {
            ThreadPool::ParallelLoop(gather, m_draw_calls_prepass_count);
        }
        else if (m_draw_calls_prepass_count > 0)
        {
            gather(0, m_draw_calls_prepass_count);
        }
    }

    void Renderer::UpdateDrawCalls_BuildIndirectAndCullTasks()
    {
        SP_PROFILE_CPU();
        // one draw entry per render lod, instanced meshes with several lods emit every lod and the
        // instance cull keeps each instance on the lod its screen coverage wants
        // one range task per 64 instances at a draw LOD, phase b expands surviving meshlets
        m_indirect_draw_count   = 0;
        m_indirect_render_count = 0;
        m_cull_task_count       = 0;
        m_draw_lods.clear();
        m_draw_lods_uploaded = false;
        const bool use_gpu_lod = cvar_gpu_lod.GetValueAs<bool>();
        // Repeated scene instances share immutable per-mesh LOD data. Emit one
        // table per submesh/fallback LOD, rather than rebuilding it per entity.
        pmr::unordered_map<const SubMesh*, array<uint32_t, 8>> lod_tables(FrameAllocator::GetResource());
        const uint32_t aabb_frame_offset = m_frame_resource_index * renderer_max_aabbs;
        const bool reduced_capacity = cvar_debug_renderdoc.GetValue() || m_frame_resource_index >= renderer_draw_data_buffer_count;
        const uint32_t indirect_draw_capacity = reduced_capacity ? 32 * 1024 : renderer_max_indirect_draws;

        // the cull pipeline drops survivors silently once a budget is hit, tracked so the log below can name the culprit
        uint64_t expected_survivors_worst_case = 0;
        uint64_t expected_instances_worst_case = 0;

        // Per-draw state is resolved in parallel. Slots and the shared LOD tables are then
        // assigned serially in submission order, and the records they describe filled in parallel.
        struct IndirectPlan
        {
            const Renderer_DrawCall* dc  = nullptr;
            const MeshImpostor* impostor = nullptr;
            uint32_t lod_first           = 0;
            uint32_t lod_last            = 0;
            uint32_t lod_valid_mask      = 0;
            uint32_t lods_emit           = 0;
            uint32_t meshlets_worst      = 0;
            uint32_t draw_base           = 0;
            uint32_t task_base           = 0;
            uint32_t inst_n              = 0;
            uint32_t render_aabb_slot    = 0;
            uint32_t base_flags          = 0;
            uint32_t mesh_lod_count      = 0;
            uint32_t gpu_lod_count       = 0;
            uint32_t lod_draw_offsets    = 0;
            uint32_t lod_table_offset    = 0;
            float max_distance_sq        = 0.0f;
            bool valid                   = false;
            bool gpu_lod                 = false;
            bool gpu_lod_patch           = false;
        };
        static vector<IndirectPlan> draw_plans;
        static vector<uint32_t> plans;
        draw_plans.resize(m_draw_call_count);
        plans.clear();
        uint32_t cull_task_total = 0;

        SP_PROFILE_CPU_START("indirect_plan");
        auto plan_draw = [&](uint32_t i)
        {
            IndirectPlan& plan = draw_plans[i];
            plan.valid = false;
            const Renderer_DrawCall& dc = m_draw_calls[i];
            Render* render              = dc.render;
            Material* material          = render->GetMaterial();

            if (!material || material->IsTransparent())
            {
                return;
            }
            if (!dc.camera_visible)
            {
                return;
            }
            if (material->GetProperty(MaterialProperty::Tessellation) > 0.0f)
            {
                return;
            }

            Entity* entity = render->GetEntity();
            Mesh* mesh     = render->GetMesh();
            if (!mesh)
            {
                return;
            }

            const bool is_instanced = render->HasInstancing();
            const uint32_t inst_n   = is_instanced ? dc.instance_count : 1;
            const bool is_skinned   =
                render->HasBoundingBoxOverride() ||
                (mesh->IsSkinned() && !cvar_meshlet_cull_skinned.GetValueAs<bool>());

            // instanced tiles share one cpu lod, so a forest next to the camera pins every tree at lod 0,
            // submit every lod and let instance cull keep each instance on the lod its screen coverage wants
            // foliage with a baked impostor gets it as one extra gpu lod after the mesh chain, the count must fit in three flag bits
            const uint32_t mesh_lod_count = max(render->GetLodCount(), 1u);
            const MeshImpostor* impostor  = render->UsesImpostor() ? render->GetImpostor() : nullptr;
            // Single trees use the same candidate LODs as scattered trees when an impostor exists.
            const bool gpu_lod            = !is_skinned && ((is_instanced && mesh_lod_count > 1u) || impostor);
            const uint32_t gpu_lod_count  = mesh_lod_count + (impostor ? 1u : 0u);
            // a single draw that the instance cull rewrites to the lod its coverage wants, the lod count must fit three flag bits
            const bool gpu_lod_patch      = !gpu_lod && !is_instanced && !is_skinned && mesh_lod_count > 1u && mesh_lod_count <= 7u && use_gpu_lod;
            auto lod_valid = [render](uint32_t lod)
            {
                return render->GetMeshletCount(lod) != 0 && render->GetIndexCount(lod) != 0;
            };
            uint32_t lod_first = gpu_lod ? 0u : dc.lod_index;
            uint32_t lod_last  = gpu_lod ? gpu_lod_count : (dc.lod_index + 1u);

            uint32_t lods_emit      = 0;
            uint32_t meshlets_worst = 0;
            for (uint32_t lod = lod_first; lod < lod_last; lod++)
            {
                if (!lod_valid(lod))
                {
                    continue;
                }
                lods_emit++;
                meshlets_worst = max(meshlets_worst, render->GetMeshletCount(lod));
            }
            if (gpu_lod_patch)
            {
                // any lod can be picked, so budget for the densest one and start from a valid one
                for (uint32_t lod = 0; lod < mesh_lod_count; lod++)
                {
                    if (!lod_valid(lod))
                    {
                        continue;
                    }
                    meshlets_worst = max(meshlets_worst, render->GetMeshletCount(lod));
                    if (lods_emit == 0)
                    {
                        lod_first = lod;
                        lod_last  = lod + 1u;
                        lods_emit = 1;
                    }
                }
            }
            if (lods_emit == 0)
            {
                return;
            }

            const bool is_two_sided    = static_cast<RHI_CullMode>(material->GetProperty(MaterialProperty::CullMode)) != RHI_CullMode::Back;
            const bool is_alpha_tested = material->IsAlphaTested();
            const bool skip_hiz        = entity && entity->GetTimeSinceLastTransform() <= 0.1f;
            // The CPU record already resolved hierarchy, UV and decal state for
            // this same render/view. Reuse it for every indirect LOD.
            const Sb_DrawData& base_draw = m_draw_data_cpu[dc.draw_data_index - m_frame_resource_index * renderer_max_draw_calls];
            uint32_t base_flags = base_draw.flags;
            if (is_skinned)
            {
                base_flags |= 1u;
            }
            if (is_instanced)
            {
                base_flags |= 2u;
            }
            if (is_two_sided)
            {
                base_flags |= 8u;
            }
            if (is_alpha_tested)
            {
                base_flags |= 16u;
            }
            if (skip_hiz)
            {
                base_flags |= 32u;
            }

            if (impostor)
            {
                base_flags |= 1u << 15;
            }

            const float max_distance    = render->GetMaxRenderDistance();
            const bool  finite_distance = max_distance > 0.0f && max_distance < numeric_limits<float>::max() * 0.5f;
            const float max_distance_sq = finite_distance ? (max_distance * max_distance) : 0.0f;

            plan.valid            = true;
            plan.dc               = &dc;
            plan.impostor         = impostor;
            plan.lod_first        = lod_first;
            plan.lod_last         = lod_last;
            plan.lods_emit        = lods_emit;
            plan.meshlets_worst   = meshlets_worst;
            plan.inst_n           = inst_n;
            plan.base_flags       = base_flags;
            plan.mesh_lod_count   = mesh_lod_count;
            plan.gpu_lod_count    = gpu_lod_count;
            plan.max_distance_sq  = max_distance_sq;
            plan.gpu_lod          = gpu_lod;
            plan.gpu_lod_patch    = gpu_lod_patch;
            plan.lod_draw_offsets = 0;
            plan.lod_table_offset = 0;
            plan.lod_valid_mask   = 0;
            for (uint32_t lod = 0; lod < max(lod_last, mesh_lod_count) && lod < 32u; lod++)
            {
                plan.lod_valid_mask |= lod_valid(lod) ? (1u << lod) : 0u;
            }
        };
        if (m_draw_call_count >= 256)
        {
            ThreadPool::ParallelLoop([&](uint32_t first, uint32_t last) { for (uint32_t i = first; i < last; i++) plan_draw(i); }, m_draw_call_count);
        }
        else
        {
            for (uint32_t i = 0; i < m_draw_call_count; i++)
            {
                plan_draw(i);
            }
        }

        for (uint32_t i = 0; i < m_draw_call_count; i++)
        {
            IndirectPlan& plan = draw_plans[i];
            if (!plan.valid || m_indirect_draw_count + plan.lods_emit > indirect_draw_capacity)
            {
                continue;
            }
            expected_survivors_worst_case += static_cast<uint64_t>(plan.inst_n) * static_cast<uint64_t>(plan.meshlets_worst);
            expected_instances_worst_case += plan.inst_n;

            plan.render_aabb_slot = aabb_frame_offset + m_draw_calls_prepass_count + m_indirect_render_count;
            m_indirect_render_count++;
            plan.draw_base = m_indirect_draw_count;
            plan.task_base = cull_task_total;

            for (uint32_t lod = plan.lod_first; lod < plan.lod_last; lod++)
            {
                if (!(plan.lod_valid_mask & (1u << lod)))
                {
                    continue;
                }

                const uint32_t draw_idx = m_indirect_draw_count++;
                if (plan.gpu_lod && lod < 7u)
                {
                    plan.lod_draw_offsets |= (draw_idx - plan.draw_base) << (lod * 3u);
                }

                if (plan.gpu_lod_patch)
                {
                    Render* render         = plan.dc->render;
                    const SubMesh* submesh = &render->GetMesh()->GetSubMesh(render->GetSubMeshIndex());
                    auto [table, inserted] = lod_tables.try_emplace(submesh);
                    if (inserted) table->second.fill(UINT32_MAX);
                    uint32_t& table_offset = table->second[lod];
                    if (table_offset == UINT32_MAX)
                    {
                        table_offset = static_cast<uint32_t>(m_draw_lods.size());
                        for (uint32_t table_lod = 0; table_lod < plan.mesh_lod_count; table_lod++)
                        {
                            const uint32_t source          = (plan.lod_valid_mask & (1u << table_lod)) ? table_lod : lod;
                            const BoundingBox& source_aabb = render->GetLodAabb(source);
                            const Vector3 source_extent    = source_aabb.GetMax() - source_aabb.GetMin();
                            Sb_DrawLod& entry              = m_draw_lods.emplace_back();
                            entry.first_index              = render->GetIndexOffset(source);
                            entry.vertex_offset            = render->GetVertexOffset(source);
                            entry.meshlet_offset           = render->GetGlobalMeshletOffset() + render->GetMeshletOffset(source);
                            entry.meshlet_count            = render->GetMeshletCount(source);
                            entry.aabb_min                 = source_aabb.GetMin();
                            entry.aabb_diag                = source_extent.Length();
                            entry.aabb_extent              = source_extent;
                            entry.previous_vertex_offset   = GeometryBuffer::GetPreviousVertexOffset(entry.vertex_offset);
                        }
                    }
                    plan.lod_table_offset = table_offset;
                }
            }

            cull_task_total += (plan.inst_n + INSTANCE_CULL_BATCH_SIZE - 1) / INSTANCE_CULL_BATCH_SIZE;
            plans.push_back(i);
        }
        m_cull_task_count = cull_task_total;
        // never shrunk, so steady frames reuse the storage without clearing it
        if (m_cull_tasks.size() < cull_task_total)
        {
            m_cull_tasks.resize(cull_task_total);
        }
        SP_PROFILE_CPU_END();

        SP_PROFILE_CPU_START("indirect_fill");
        auto fill_plans = [&](uint32_t first, uint32_t last)
        {
            for (uint32_t p = first; p < last; p++)
            {
                const IndirectPlan& plan     = draw_plans[plans[p]];
                const Renderer_DrawCall& dc  = *plan.dc;
                Render* render               = dc.render;
                Material* material           = render->GetMaterial();
                const MeshImpostor* impostor = plan.impostor;
                const uint32_t mesh_lod_count = plan.mesh_lod_count;
                const uint32_t gpu_lod_count  = plan.gpu_lod_count;
                const uint32_t base_flags     = plan.base_flags;
                const uint32_t render_aabb_slot = plan.render_aabb_slot;
                const float max_distance_sq   = plan.max_distance_sq;
                const Sb_DrawData& base_draw  = m_draw_data_cpu[dc.draw_data_index - m_frame_resource_index * renderer_max_draw_calls];
                const Matrix& transform_prev  = base_draw.transform_previous;
                uint32_t draw_idx             = plan.draw_base;

                // every lod of this render shares one aabb slot, after the prepass slots
                const uint32_t aabb_slot = render_aabb_slot - aabb_frame_offset;
                if (aabb_slot < renderer_max_aabbs)
                {
                    const BoundingBox& aabb         = render->GetBoundingBox();
                    m_bindless_aabbs[aabb_slot].min = aabb.GetMin();
                    m_bindless_aabbs[aabb_slot].max = aabb.GetMax();
                }

                for (uint32_t lod = plan.lod_first; lod < plan.lod_last; lod++)
                {
                    const uint32_t lod_meshlet_count = render->GetMeshletCount(lod);
                    if (lod_meshlet_count == 0 || render->GetIndexCount(lod) == 0)
                    {
                        continue;
                    }

                    Sb_DrawData& draw_data  = m_indirect_draw_data[draw_idx];
                    m_indirect_renders[draw_idx] = render;
                    draw_idx++;
                    draw_data.transform          = base_draw.transform;
                    draw_data.transform_previous = transform_prev;
                    draw_data.motion_blur_id     = base_draw.motion_blur_id;
                    draw_data.material_index     = material->GetIndex();
                    draw_data.is_transparent     = 0;
                    draw_data.aabb_index         = render_aabb_slot;
                    draw_data.lod_first_index    = render->GetIndexOffset(lod);
                    draw_data.flags              = base_flags | ((lod & 7u) << 8u) | ((gpu_lod_count & 7u) << 11u);
                    if (impostor && lod == mesh_lod_count)
                    {
                        // the card turns to face the camera, so winding and meshlet cones mean nothing for it
                        // its silhouette is cut per texel, so an opaque trunk card still takes the alpha tested path
                        draw_data.flags                |= (1u << 14) | 8u | 16u;
                        draw_data.impostor_texel_offset = impostor->texel_offset;
                        draw_data.impostor_layout       = mesh_impostor_frames | (impostor->resolution << 8) | (mesh_impostor_layers << 24);
                        draw_data.impostor_center       = impostor->center;
                        draw_data.impostor_radius       = impostor->radius;
                        draw_data.impostor_uv_min       = impostor->uv_min;
                        draw_data.impostor_uv_scale     = impostor->uv_scale;
                    }
                    draw_data.instance_offset        = render->GetGlobalInstanceOffset();
                    draw_data.instance_index         = 0;
                    draw_data.lod_vertex_offset      = render->GetVertexOffset(lod);
                    draw_data.previous_vertex_offset = GeometryBuffer::GetPreviousVertexOffset(draw_data.lod_vertex_offset);
                    draw_data.lod_meshlet_offset     = render->GetGlobalMeshletOffset() + render->GetMeshletOffset(lod);
                    draw_data.meshlet_vertex_base    = render->GetMesh()->GetGlobalMeshletVertexOffset();
                    draw_data.meshlet_micro_base     = render->GetMesh()->GetGlobalMeshletMicroOffset();
                    draw_data.lod_meshlet_count      = lod_meshlet_count;
                    if (plan.gpu_lod_patch)
                    {
                        draw_data.flags                 = (draw_data.flags & ~(7u << 11u)) | ((mesh_lod_count & 7u) << 11u) | draw_flag_gpu_lod;
                        draw_data.impostor_texel_offset = plan.lod_table_offset;
                    }
                    draw_data.uv_tiling      = base_draw.uv_tiling;
                    draw_data.uv_offset      = base_draw.uv_offset;
                    draw_data.uv_invert      = base_draw.uv_invert;
                    draw_data.uv_rotation    = base_draw.uv_rotation;
                    draw_data.uv_world_space = base_draw.uv_world_space;
                    draw_data.decal_offset   = base_draw.decal_offset;
                    draw_data.decal_count    = base_draw.decal_count;

                    const BoundingBox& lod_aabb_local     = render->GetLodAabb(lod);
                    const Vector3 lod_extent              = lod_aabb_local.GetMax() - lod_aabb_local.GetMin();
                    draw_data.lod_aabb_min                = lod_aabb_local.GetMin();
                    draw_data.lod_aabb_extent             = lod_extent;
                    draw_data.lod_aabb_diag               = lod_extent.Length();
                    draw_data.max_render_distance_squared = max_distance_sq;
                }

                // Cull each instance once, then select its draw from the LOD chain.
                // Missing LODs map to the first valid draw rather than leaving a hole.
                const uint32_t lod_draw_offsets = plan.gpu_lod ? (plan.lod_draw_offsets | 0x80000000u) : 0u;
                Sb_CullTask* task = m_cull_tasks.data() + plan.task_base;
                for (uint32_t inst = 0; inst < plan.inst_n; inst += INSTANCE_CULL_BATCH_SIZE, ++task)
                {
                    task->draw_index               = plan.draw_base;
                    task->lod_reference_draw_index = plan.draw_base;
                    task->instance_index           = inst;
                    task->instance_count           = min(INSTANCE_CULL_BATCH_SIZE, plan.inst_n - inst);
                    task->lod_draw_offsets         = lod_draw_offsets;
                }
            }
        };
        if (plans.size() >= 64)
        {
            ThreadPool::ParallelLoop(fill_plans, static_cast<uint32_t>(plans.size()));
        }
        else
        {
            fill_plans(0, static_cast<uint32_t>(plans.size()));
        }
        SP_PROFILE_CPU_END();

        // Report the conservative pre-cull meshlet bound once per session.
        EnsureCullingBufferCapacity(m_indirect_draw_count, expected_instances_worst_case, expected_survivors_worst_case);
        const uint32_t meshlet_instance_capacity = GetBuffer(Renderer_Buffer::MeshletInstances)->GetElementCount();
        static bool s_logged_survivor_overflow   = false;
        if (!s_logged_survivor_overflow && expected_survivors_worst_case > meshlet_instance_capacity)
        {
            SP_LOG_WARNING(
                "meshlet cull survivor buffer is too small, worst case %llu > capacity %u, distant or later-submitted geometry may be silently dropped",
                static_cast<unsigned long long>(expected_survivors_worst_case),
                meshlet_instance_capacity
            );
            s_logged_survivor_overflow = true;
        }

    }

    void Renderer::UpdateDrawCalls()
    {
        SP_PROFILE_CPU();
        UpdateDrawCalls_ResetCounts();
        GeometryBuffer::SnapshotVertexHistory();
        UpdateDrawCalls_CollectAndSort();
        UpdateDrawCalls_BuildPrepass();
        UpdateDrawCalls_BuildIndirectAndCullTasks();
    }

    // an instance transform that is not finite, or large enough to swamp the scene bounds, gives that
    // instance an effectively infinite aabb, the bvh built around it degenerates and every ray then
    // tests every instance, which reads as a hang and gets the device killed by the driver watchdog
    static bool is_transform_traceable(const math::Matrix& m)
    {
        const float limit = 1e9f;
        const float values[12] =
        {
            m.m00, m.m10, m.m20, m.m30,
            m.m01, m.m11, m.m21, m.m31,
            m.m02, m.m12, m.m22, m.m32
        };

        for (float value : values)
        {
            if (!std::isfinite(value) || fabsf(value) > limit)
            {
                return false;
            }
        }

        return true;
    }

    // a skinned mesh whose deformed vertices went bad takes its blas with it, the world box is the
    // cheapest place to notice, checking it per instance costs nothing next to checking every vertex
    static bool is_bounding_box_traceable(const math::BoundingBox& box)
    {
        const float limit        = 1e9f;
        const math::Vector3& min = box.GetMin();
        const math::Vector3& max = box.GetMax();
        const float values[6]    = { min.x, min.y, min.z, max.x, max.y, max.z };

        for (float value : values)
        {
            if (!std::isfinite(value) || fabsf(value) > limit)
            {
                return false;
            }
        }

        return true;
    }

    // one tlas instance and its hit shader record for a world matrix
    // bit 0 = opaque, bit 1 = transparent, bit 2 = grass, bit 3 = casts no shadow
    // Grass casts through screen-space depth only. Authored blades and renders with shadow
    // casting turned off get their own bits so shadow rays (masks 0x01 and 0x03) skip them
    // while reflection and GI surface rays (0xFF) still see them.
    // GPU procedural grass has no entities and never enters this TLAS.
    static uint32_t tlas_instance_mask(Render* render, Material* material)
    {
        if (material->GetProperty(MaterialProperty::IsGrassBlade) != 0.0f)
        {
            return 0x04;
        }

        if (!render->HasFlag(RenderFlags::CastsShadows))
        {
            return 0x08;
        }

        return material->IsTransparent() ? 0x02 : 0x01;
    }

    static void build_tlas_instance(
        Render* render,
        Material* material,
        const Matrix& world,
        uint64_t blas_address,
        RHI_AccelerationStructureInstance& instance,
        Sb_GeometryInfo& geometry_info
    )
    {
        constexpr uint32_t RHI_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT = 0x00000002; // VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR
        constexpr uint32_t RHI_GEOMETRY_INSTANCE_FORCE_OPAQUE_BIT                 = 0x00000004; // VK_GEOMETRY_INSTANCE_FORCE_OPAQUE_BIT_KHR
        constexpr uint32_t RHI_GEOMETRY_INSTANCE_FORCE_NO_OPAQUE_BIT              = 0x00000008; // VK_GEOMETRY_INSTANCE_FORCE_NO_OPAQUE_BIT_KHR

        const RHI_CullMode cull_mode = static_cast<RHI_CullMode>(material->GetProperty(MaterialProperty::CullMode));

        const uint32_t instance_mask = tlas_instance_mask(render, material);

        // Mesh BLAS geometry is opaque by default. Cutouts must explicitly
        // override it, otherwise inline queries never get to test their alpha.
        uint32_t flags = cull_mode == RHI_CullMode::None ? RHI_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT : 0;
        flags |= material->IsAlphaTested() ? RHI_GEOMETRY_INSTANCE_FORCE_NO_OPAQUE_BIT : RHI_GEOMETRY_INSTANCE_FORCE_OPAQUE_BIT;

        instance                                             = {};
        instance.instance_custom_index                       = material->GetIndex(); // for hit shader material lookup
        instance.mask                                        = instance_mask;
        instance.instance_shader_binding_table_record_offset = 0;                    // sbt hit group offset
        instance.flags                                       = flags;
        instance.device_address                              = blas_address;

        // row-major 3x4 transform (transpose 3x3 because vulkan uses column vectors)
        const Matrix& m = world;
        instance.transform[0]  = m.m00; instance.transform[1]  = m.m10; instance.transform[2]  = m.m20; instance.transform[3]  = m.m30;
        instance.transform[4]  = m.m01; instance.transform[5]  = m.m11; instance.transform[6]  = m.m21; instance.transform[7]  = m.m31;
        instance.transform[8]  = m.m02; instance.transform[9]  = m.m12; instance.transform[10] = m.m22; instance.transform[11] = m.m32;

        geometry_info                = {};
        geometry_info.vertex_offset  = render->GetVertexOffset(0);
        geometry_info.index_offset   = render->GetIndexOffset(0);
        geometry_info.material_index = instance.instance_custom_index;
        const Matrix inverse = m.Inverted();
        geometry_info.object_to_world_0 = Vector4(m.m00, m.m01, m.m02, m.m30);
        geometry_info.object_to_world_1 = Vector4(m.m10, m.m11, m.m12, m.m31);
        geometry_info.object_to_world_2 = Vector4(m.m20, m.m21, m.m22, m.m32);
        geometry_info.world_to_object_0 = Vector4(inverse.m00, inverse.m01, inverse.m02, 0.0f);
        geometry_info.world_to_object_1 = Vector4(inverse.m10, inverse.m11, inverse.m12, 0.0f);
        geometry_info.world_to_object_2 = Vector4(inverse.m20, inverse.m21, inverse.m22, 0.0f);
        static_assert(sizeof(Sb_GeometryInfo) == 152, "RT geometry buffer layout must match HLSL");
        fill_uv_draw_fields_from_render(geometry_info, render);
    }

    // instanced renders (terrain scatter) enter the tlas one instance each, culled around the
    // camera so a forest does not become hundreds of thousands of tlas entries. the culled set
    // is cached, a tlas rebuild for a moving entity only copies it
    struct InstancedTlasCache
    {
        uint64_t render_id       = 0;
        uint64_t transform_revision = 0;
        uint64_t instance_revision  = 0;
        Vector3 cull_center;
        float cull_radius       = 0.0f;
        uint64_t blas_address   = 0;
        uint32_t material_index = 0;
        uint32_t instance_count = 0;
        uint32_t group_count    = 0;
        uint32_t mask           = 0;
        vector<RHI_AccelerationStructureInstance> instances;
        vector<Sb_GeometryInfo> geometry_infos;
    };
    static vector<InstancedTlasCache> instanced_tlas_caches;

    struct SingleTlasCache
    {
        uint64_t entity_id = 0;
        uint64_t transform_revision = 0;
        bool transform_traceable = false;
        bool valid = false;
        bool invalid_transform = false;
        bool instanced = false;
        RHI_AccelerationStructureInstance instance = {};
        Sb_GeometryInfo geometry = {};
    };
    static vector<SingleTlasCache> single_tlas_caches;


    // scatter beyond its shadow distance casts nothing in either path, the margin lets the
    // camera travel before the set has to be rebuilt
    constexpr float instanced_tlas_hysteresis = 25.0f;

    // one bounds group of a stale instanced cache, groups are rebuilt in parallel and then appended in order
    struct InstancedTlasGroupWork
    {
        uint32_t cache_index = 0;
        Render* render       = nullptr;
        Material* material   = nullptr;
        const Render::InstanceBoundsGroup* group = nullptr;
        vector<RHI_AccelerationStructureInstance> instances;
        vector<Sb_GeometryInfo> geometry_infos;
    };

    // true when the cache must be rebuilt, its header is then updated and its instance lists cleared
    static bool begin_instanced_tlas_refresh(InstancedTlasCache& cache, Render* render, Material* material, const Vector3& camera_position)
    {
        constexpr float hysteresis = instanced_tlas_hysteresis;
        constexpr float radius_max = 500.0f;
        const float radius = Renderer::IsPathTracingActive() ? 1e15f : min(render->GetMaxShadowDistance(), radius_max);

        Entity* entity               = render->GetEntity();
        const uint64_t blas_address  = render->GetAccelerationStructureDeviceAddress();
        const bool stale =
            cache.render_id != render->GetObjectId() ||
            cache.blas_address != blas_address ||
            cache.material_index != material->GetIndex() ||
            cache.mask != tlas_instance_mask(render, material) ||
            cache.instance_count != render->GetInstanceCount() ||
            cache.group_count != static_cast<uint32_t>(render->GetInstanceBoundsGroups().size()) ||
            cache.cull_radius != radius ||
            cache.transform_revision != entity->GetTransformRevision() ||
            cache.instance_revision != render->GetInstanceRevision() ||
            Vector3::DistanceSquared(cache.cull_center, camera_position) > hysteresis * hysteresis;
        if (!stale)
        {
            return false;
        }

        cache.cull_center    = camera_position;
        cache.render_id      = render->GetObjectId();
        cache.transform_revision = entity->GetTransformRevision();
        cache.instance_revision  = render->GetInstanceRevision();
        cache.cull_radius    = radius;
        cache.blas_address   = blas_address;
        cache.material_index = material->GetIndex();
        cache.mask           = tlas_instance_mask(render, material);
        cache.instance_count = render->GetInstanceCount();
        cache.group_count    = static_cast<uint32_t>(render->GetInstanceBoundsGroups().size());
        cache.instances.clear();
        cache.geometry_infos.clear();
        return true;
    }

    static void build_instanced_tlas_group(InstancedTlasGroupWork& work, const InstancedTlasCache& cache)
    {
        work.instances.clear();
        work.geometry_infos.clear();
        Render* render              = work.render;
        const Vector3& center       = cache.cull_center;
        const float cull_radius     = cache.cull_radius + instanced_tlas_hysteresis;
        const float cull_radius_sq  = cull_radius * cull_radius;
        const Render::InstanceBoundsGroup& group = *work.group;
        for (uint32_t j = group.offset; j < group.offset + group.count; ++j)
        {
            const uint32_t index      = render->GetGroupedInstanceIndex(j);
            const BoundingBox& bounds = render->GetInstanceBounds(index);
            if (Vector3::DistanceSquared(center, bounds.GetClosestPoint(center)) > cull_radius_sq)
            {
                continue;
            }

            const Matrix world = render->GetInstance(index, true);
            if (!is_transform_traceable(world) || !is_bounding_box_traceable(bounds))
            {
                continue;
            }

            build_tlas_instance(render, work.material, world, cache.blas_address, work.instances.emplace_back(), work.geometry_infos.emplace_back());
        }
    }

    void Renderer::UpdateAccelerationStructures()
    {
        SP_PROFILE_CPU();

        bool ray_tracing_enabled = IsPathTracingActive() || cvar_ray_traced_reflections.GetValueAs<bool>() || cvar_ray_traced_shadows.GetValueAs<bool>() || cvar_restir_pt.GetValueAs<bool>();
        if (!ray_tracing_enabled)
        {
            return;
        }

        if (!RHI_Device::IsSupportedRayTracing() || !RHI_Device::IsRecording())
        {
            SP_LOG_WARNING("Ray tracing or command list invalid, skipping update");
            return;
        }

        // Background loading must not freeze transforms/material indices in an
        // existing TLAS. Continue the budgeted resident-scene update; only the
        // first TLAS waits for loading to finish.
        if (frame_is_loading && !m_tlas)
        {
            return;
        }

        // a secondary view only sees its own subtree, building the tlas from that would throw
        // away the primary camera's instances and force a full rebuild on the next frame, the
        // preview does not ray trace so the primary structures are left untouched instead
        if (secondary_render_root_active)
        {
            return;
        }

        // World animation has already marked deformable meshes before frame preparation.
        if (ray_tracing_prepared_at != Timer::GetTimeMs()) prepare_ray_tracing_renders();

        m_pass_state.skip_rt_trace = false;
        // blas builds are capped per frame, recording thousands onto one command list hits driver tdr
        bool blas_burst_done = true;
        bool blas_refit_done = false;
        bool blas_built_this_frame = false;
        if (!ray_tracing_build_work.empty())
        {
            SP_PROFILE_CPU_START("rt_blas_update");
            RHI_CommandList::BeginMarker("blas_build");

            constexpr uint32_t blas_builds_per_frame = 64;
            constexpr uint32_t blas_triangles_per_frame = 250000;
            // Finish initial preparation faster; runtime additions keep the small budget.
            const double blas_cpu_budget_ms = m_tlas ? 2.0 : 8.0;
            const auto blas_start = chrono::steady_clock::now();
            uint64_t blas_triangles = 0;

            uint32_t blas_built     = 0;
            uint32_t blas_remaining = 0;
            const uint32_t blas_total = static_cast<uint32_t>(ray_tracing_renders.size());
            for (Render* render : ray_tracing_build_work)
            {
                if (!render->HasAccelerationStructure())
                {
                    const uint32_t triangles = render->GetIndexCount() / 3;
                    const double elapsed_ms = chrono::duration<double, milli>(chrono::steady_clock::now() - blas_start).count();
                    // Always allow one build to make progress, even for a large mesh.
                    if (blas_built == 0 || (blas_built < blas_builds_per_frame &&
                        blas_triangles + triangles <= blas_triangles_per_frame && elapsed_ms < blas_cpu_budget_ms))
                    {
                        render->BuildAccelerationStructure();
                        if (render->HasAccelerationStructure())
                        {
                            blas_built++;
                            blas_triangles += triangles;
                        }
                    }
                    else
                    {
                        blas_remaining++;
                    }
                }

                // refit blas for deformable meshes (cloth, skinned, etc.)
                if (render->NeedsBlasRefit() && render->HasAccelerationStructure())
                {
                    render->RefitAccelerationStructure();
                    render->SetNeedsBlasRefit(false);
                    blas_refit_done = true;
                }
            }

            static bool blas_was_backlogged = false;
            if (blas_remaining > 0)
            {
                blas_was_backlogged = true;
            }

            blas_burst_done = (blas_remaining == 0);
            blas_built_this_frame = blas_built > 0;

            // Both backends barrier BLAS writes before TLAS construction, then TLAS
            // writes before tracing. Compute batch B follows A on the same queue.
            // Only initial preparation needs a blackout; runtime additions are safe
            // to trace this frame, with still-unbuilt meshes omitted below.
            m_pass_state.skip_rt_trace = !m_tlas && (blas_built_this_frame || !blas_burst_done);

            // free the shared static scratch only once the burst fully completes
            // freeing mid-burst would force a reallocation on next frame
            if (blas_burst_done && blas_built > 0)
            {
                RHI_AccelerationStructure::FreeSharedBlasScratch();
                // runtime additions (streamed terrain detail, spawned props) build in a frame or two, only a backlog is news
                if (blas_was_backlogged)
                {
                    SP_LOG_INFO("Ray tracing: BLAS build burst complete (last frame built %u, total %u)", blas_built, blas_total);
                }
                blas_was_backlogged = false;
            }

            RHI_CommandList::EndMarker();
            SP_PROFILE_CPU_END();
        }

        // Wait for the initial resident set, but keep an existing scene current while
        // newly spawned geometry is budgeted in. Never retain invalidated BLAS references.
        Terrain* preparing_terrain = Terrain::FindActive();
        // no tlas references the initial burst yet, compacting as builds retire keeps the whole scene from sitting uncompacted at once
        if (!m_tlas && !blas_burst_done)
        {
            RHI_CommandList::BeginMarker("blas_compact");
            RHI_AccelerationStructure::CompactBottomLevels();
            RHI_CommandList::EndMarker();
        }
        if ((!blas_burst_done && !m_tlas) || Spline::HasPendingRoadWork() || (preparing_terrain && preparing_terrain->IsGenerating()))
        {
            m_pass_state.skip_rt_trace = true;
            return;
        }

        if (materials_uploaded_this_frame) instanced_tlas_caches.clear();

        // every path below rebuilds or drops the tlas, so it never keeps pointing at pre-compaction storage
        RHI_CommandList::BeginMarker("blas_compact");
        const bool blas_compacted = RHI_AccelerationStructure::CompactBottomLevels();
        RHI_CommandList::EndMarker();

        // static scenes keep a valid tlas, only rebuild when transforms, materials, or blas move
        {
            const bool structural_tlas_rebuild =
                !m_tlas ||
                ray_tracing_membership_dirty ||
                ray_tracing_pending_blas ||
                materials_uploaded_this_frame ||
                blas_refit_done ||
                blas_built_this_frame ||
                blas_compacted;
            materials_uploaded_this_frame = false;

            bool needs_tlas_rebuild = structural_tlas_rebuild;

            // instanced renders are culled around the camera, a stale set is as good as a moved entity
            {
                const Vector3 camera_position = Renderer::view().frame.camera_position;
                SP_PROFILE_CPU_START("rt_instanced_cache");
                // Both lists follow the same stable render order. Identity checks
                // invalidate shifted slots after membership changes or entity reuse.
                needs_tlas_rebuild |= instanced_tlas_caches.size() != ray_tracing_instanced_renders.size();
                instanced_tlas_caches.resize(ray_tracing_instanced_renders.size());
                static vector<InstancedTlasGroupWork> group_work;
                uint32_t group_work_count = 0;
                const float cull_margin   = instanced_tlas_hysteresis;
                for (size_t i = 0; i < ray_tracing_instanced_renders.size(); ++i)
                {
                    Render* render = ray_tracing_instanced_renders[i];
                    InstancedTlasCache& cache = instanced_tlas_caches[i];
                    Material* material = render->GetMaterial();
                    if (!material || render->GetAccelerationStructureDeviceAddress() == 0)
                    {
                        needs_tlas_rebuild |= !cache.instances.empty();
                        cache = {};
                        continue;
                    }

                    if (!begin_instanced_tlas_refresh(cache, render, material, camera_position))
                    {
                        continue;
                    }
                    needs_tlas_rebuild = true;

                    const float cull_radius    = cache.cull_radius + cull_margin;
                    const float cull_radius_sq = cull_radius * cull_radius;
                    for (const Render::InstanceBoundsGroup& group : render->GetInstanceBoundsGroups())
                    {
                        if (Vector3::DistanceSquared(camera_position, group.bounds.GetClosestPoint(camera_position)) > cull_radius_sq)
                        {
                            continue;
                        }
                        if (group_work.size() <= group_work_count)
                        {
                            group_work.emplace_back();
                        }
                        InstancedTlasGroupWork& work = group_work[group_work_count++];
                        work.cache_index = static_cast<uint32_t>(i);
                        work.render      = render;
                        work.material    = material;
                        work.group       = &group;
                    }
                }

                if (group_work_count > 0)
                {
                    auto build_groups = [&](uint32_t first, uint32_t last)
                    {
                        for (uint32_t w = first; w < last; ++w)
                        {
                            build_instanced_tlas_group(group_work[w], instanced_tlas_caches[group_work[w].cache_index]);
                        }
                    };
                    if (group_work_count >= 4)
                    {
                        ThreadPool::ParallelLoop(build_groups, group_work_count);
                    }
                    else
                    {
                        build_groups(0, group_work_count);
                    }

                    // work items are in cache then group order, so appending keeps the serial layout
                    for (uint32_t w = 0; w < group_work_count; ++w)
                    {
                        InstancedTlasGroupWork& work = group_work[w];
                        InstancedTlasCache& cache    = instanced_tlas_caches[work.cache_index];
                        cache.instances.insert(cache.instances.end(), work.instances.begin(), work.instances.end());
                        cache.geometry_infos.insert(cache.geometry_infos.end(), work.geometry_infos.begin(), work.geometry_infos.end());
                    }
                }
                SP_PROFILE_CPU_END();
            }

            if (!needs_tlas_rebuild)
            {
                for (size_t i = 0; i < ray_tracing_renders.size(); ++i)
                {
                    Render* render = ray_tracing_renders[i];
                    if (render->HasInstancing()) continue; // checked by the instanced cache above
                    Entity* entity = render->GetEntity();
                    const SingleTlasCache& cache = single_tlas_caches[i];
                    // Compare with the pose actually submitted, rather than rebuilding
                    // for a time window after movement (or missing an older pending edit).
                    if (cache.entity_id != entity->GetObjectId() ||
                        cache.transform_revision != entity->GetTransformRevision())
                    {
                        needs_tlas_rebuild = true;
                        break;
                    }
                }
            }

            if (!needs_tlas_rebuild)
            {
                return;
            }

            // moving geometry must match this frame's gbuffer: tracing against last frame's
            // transforms causes alternating self-intersections in shadows and reflections.
        }

        // tlas
        {
            SP_PROFILE_CPU_START("rt_instance_prepare");
            RHI_CommandList::BeginMarker("tlas_build");

            if (!m_tlas)
            {
                m_tlas = make_unique<RHI_AccelerationStructure>(RHI_AccelerationStructureType::Top, "world_tlas");
            }

            // static to avoid per-frame heap alloc, never cleared since compaction overwrites every slot it keeps
            static vector<RHI_AccelerationStructureInstance> instances;
            static vector<Sb_GeometryInfo> geometry_infos;

            single_tlas_caches.resize(ray_tracing_renders.size());
            uint32_t invalid_transforms   = 0;
            const char* first_invalid_name = nullptr;

            // Each render owns a stable output slot. Preparation only reads scene state;
            // the main thread compacts the results and submits GPU work afterwards.
            const uint32_t render_count = static_cast<uint32_t>(ray_tracing_renders.size());
            const uint32_t jobs = min(render_count, 32u);
            if (jobs > 0)
            ThreadPool::ParallelLoop([&](uint32_t first_job, uint32_t last_job)
            {
                const uint32_t begin = render_count * first_job / jobs;
                const uint32_t end = render_count * last_job / jobs;
                for (uint32_t i = begin; i < end; ++i)
                {
                    Render* render = ray_tracing_renders[i];
                    SingleTlasCache& cache = single_tlas_caches[i];
                    cache.valid = false;
                    cache.invalid_transform = false;
                    cache.instanced = render->HasInstancing();
                    if (cache.instanced) continue;
                    Entity* entity = render->GetEntity();
                    Material* material = render->GetMaterial();
                    if (!material) continue;
                    const uint64_t device_address = render->GetAccelerationStructureDeviceAddress();
                    if (!device_address || !render->GetVertexBuffer() || !render->GetIndexBuffer()) continue;
                    const Matrix& m = entity->GetMatrix();
                    const uint64_t transform_revision = entity->GetTransformRevision();
                    const bool transform_changed = cache.entity_id != entity->GetObjectId() ||
                        cache.transform_revision != transform_revision;
                    if (transform_changed)
                        cache.transform_traceable = is_transform_traceable(m);
                    // Geometry bounds can change independently through skinning;
                    // static matrix validation only needs repeating after a pose edit.
                    if (!cache.transform_traceable || !is_bounding_box_traceable(render->GetBoundingBox()))
                    {
                        cache.invalid_transform = true;
                        continue;
                    }
                    if (transform_changed ||
                        cache.instance.device_address != device_address ||
                        cache.instance.instance_custom_index != material->GetIndex() ||
                        cache.instance.flags != ((static_cast<RHI_CullMode>(material->GetProperty(MaterialProperty::CullMode)) == RHI_CullMode::None ? 2u : 0u) | (material->IsAlphaTested() ? 8u : 4u)) ||
                        cache.instance.mask != tlas_instance_mask(render, material) ||
                        cache.geometry.vertex_offset != render->GetVertexOffset(0) ||
                        cache.geometry.index_offset != render->GetIndexOffset(0))
                    {
                        cache.entity_id = entity->GetObjectId();
                        cache.transform_revision = transform_revision;
                        build_tlas_instance(render, material, m, device_address, cache.instance, cache.geometry);
                    }
                    fill_uv_draw_fields_from_render(cache.geometry, render);
                    // decal ranges were staged by draw preparation earlier this frame and are only read here
                    cache.geometry.decal_offset = 0;
                    cache.geometry.decal_count  = 0;
                    if (!render->GetDecals().empty())
                    {
                        const auto range = m_decal_ranges.find(render);
                        if (range != m_decal_ranges.end())
                        {
                            cache.geometry.decal_offset = range->second.first;
                            cache.geometry.decal_count  = range->second.second;
                        }
                    }
                    cache.valid = true;
                }
            }, jobs);
            SP_PROFILE_CPU_START("rt_compact");

            // each render's output slot comes from a prefix sum, then the copies run in parallel
            static vector<uint32_t> output_offsets;
            static vector<uint32_t> instanced_slots;
            output_offsets.resize(render_count);
            instanced_slots.resize(render_count);
            uint32_t output_count    = 0;
            uint32_t instanced_index = 0;
            uint64_t emitter_instance_signature = 14695981039346656037ull;
            auto mix_emitter_instance = [&](uint64_t value)
            {
                emitter_instance_signature = (emitter_instance_signature ^ value) * 1099511628211ull;
            };
            for (uint32_t i = 0; i < render_count; ++i)
            {
                const SingleTlasCache& cache = single_tlas_caches[i];
                output_offsets[i] = output_count;
                if (cache.instanced)
                {
                    instanced_slots[i] = instanced_index;
                    output_count += static_cast<uint32_t>(instanced_tlas_caches[instanced_index++].instances.size());
                }
                else if (cache.valid)
                {
                    // Terminal BSDF emitter paths cache InstanceIndex/PrimitiveIndex.
                    // Compaction can renumber them when unrelated geometry changes.
                    if (is_pooled_emitter_material(ray_tracing_renders[i]->GetMaterial()))
                    {
                        mix_emitter_instance(cache.entity_id);
                        mix_emitter_instance(output_count);
                    }
                    output_count++;
                }
                else if (cache.invalid_transform)
                {
                    ++invalid_transforms;
                    if (!first_invalid_name) first_invalid_name = ray_tracing_renders[i]->GetEntity()->GetObjectName().c_str();
                }
            }
            static uint64_t previous_emitter_instance_signature = 0;
            if (emitter_instance_signature != previous_emitter_instance_signature)
            {
                m_pass_state.restir.history_invalid = true;
                m_pass_state.restir.accumulation_valid = false;
                previous_emitter_instance_signature = emitter_instance_signature;
            }
            instances.resize(output_count);
            geometry_infos.resize(output_count);
            auto copy_outputs = [&](uint32_t first, uint32_t last)
            {
                for (uint32_t i = first; i < last; ++i)
                {
                    const SingleTlasCache& cache = single_tlas_caches[i];
                    const uint32_t offset        = output_offsets[i];
                    if (cache.instanced)
                    {
                        const InstancedTlasCache& instanced = instanced_tlas_caches[instanced_slots[i]];
                        copy(instanced.instances.begin(), instanced.instances.end(), instances.begin() + offset);
                        copy(instanced.geometry_infos.begin(), instanced.geometry_infos.end(), geometry_infos.begin() + offset);
                    }
                    else if (cache.valid)
                    {
                        instances[offset]      = cache.instance;
                        geometry_infos[offset] = cache.geometry;
                    }
                }
            };
            if (render_count >= 256)
            {
                ThreadPool::ParallelLoop(copy_outputs, render_count);
            }
            else if (render_count > 0)
            {
                copy_outputs(0, render_count);
            }

            SP_PROFILE_CPU_END();
            // log only when the count moves so a persistent bad entity does not spam every frame
            static uint32_t last_invalid_transforms = 0;
            if (invalid_transforms != last_invalid_transforms)
            {
                if (invalid_transforms > 0)
                {
                    SP_LOG_WARNING(
                        "Ray tracing: %u instances have a non finite or out of range transform and were excluded from the tlas, first is '%s'",
                        invalid_transforms,
                        first_invalid_name ? first_invalid_name : "unknown"
                    );
                }

                last_invalid_transforms = invalid_transforms;
            }

            // the table is written and read in lockstep with the instances, so a scene that outgrows the
            // buffer drops the tail of both rather than writing past the end of the allocation
            RHI_Buffer* geometry_info_buffer =
                GetBuffer(Renderer_Buffer::GeometryInfo);
            const size_t geometry_capacity =
                geometry_info_buffer->GetObjectSize() /
                sizeof(Sb_GeometryInfo);
            if (geometry_infos.size() > geometry_capacity)
            {
                SP_LOG_WARNING(
                    "Ray tracing: %zu geometries exceed the geometry info buffer's %zu, dropping the rest",
                    geometry_infos.size(),
                    geometry_capacity
                );
                geometry_infos.resize(geometry_capacity);
                instances.resize(geometry_capacity);
            }

            static uint32_t last_instance_count = 0;
            if (!instances.empty())
            {
                // Camera culling changes the count routinely while driving. Log
                // creation, not every normal rebuild/refit of the moving scene.
                if (last_instance_count == 0)
                {
                    SP_LOG_INFO("Ray tracing: building TLAS with %zu instances", instances.size());
                }
                last_instance_count = static_cast<uint32_t>(instances.size());
                SP_PROFILE_CPU_END();
                SP_PROFILE_CPU_START("rt_tlas_build");
                m_tlas->BuildTopLevel(instances);
                ++m_pass_state.path_tracing.scene_revision;

                // this is a full overwrite of the table, not a ring push, and the hit shaders read it at
                // the offset it was bound with. without the rewind every rebuild walked the offset one
                // stride further until the write ran off the end of the buffer, and in the meantime the
                // passes bound at zero and read whatever an earlier rebuild had left there
                geometry_info_buffer->ResetOffset();
                geometry_info_buffer->Update(geometry_infos.data(), static_cast<uint32_t>(geometry_infos.size() * sizeof(Sb_GeometryInfo)));
                ray_tracing_membership_dirty = false;
            }
            else if (last_instance_count != 0)
            {
                SP_LOG_INFO("Ray tracing: destroying TLAS (world changed)");
                m_tlas = nullptr;
                ++m_pass_state.path_tracing.scene_revision;
                last_instance_count = 0;
                ray_tracing_membership_dirty = false;
            }

            RHI_CommandList::EndMarker();
            SP_PROFILE_CPU_END();
        }
    }

    RHI_AccelerationStructure* Renderer::GetTopLevelAccelerationStructure()
    {
        return m_tlas.get();
    }

    void Renderer::DestroyAccelerationStructures()
    {
        RHI_Device::QueueWaitAll();
        RHI_AccelerationStructure::FreeSharedBlasScratch();

        m_tlas = nullptr;
        instanced_tlas_caches.clear();
        single_tlas_caches.clear();

        // World teardown invalidates every mesh. Dedup since many renders share one.
        std::unordered_set<Mesh*> meshes;
        for (Entity* entity : render_entities())
        {
            if (!entity)
            {
                continue;
            }

            if (Render* render = entity->GetComponent<Render>())
            {
                if (Mesh* mesh = render->GetMesh())
                {
                    meshes.insert(mesh);
                }
            }
        }
        for (Mesh* mesh : meshes)
        {
            if (mesh)
            {
                mesh->InvalidateAllBlas();
            }
        }

        SP_LOG_INFO("Acceleration structures destroyed for world change");
    }

    void Renderer::UpdateShadowAtlas()
    {
        const uint32_t resolution_atlas = GetRenderTarget(Renderer_RenderTarget::shadow_atlas)->GetWidth();
        const uint32_t min_slice_res    = 256;
        const uint32_t border           = 8;

        // collect slices
        m_shadow_slices.clear();
        for (const auto& entity : light_entities())
        {
            if (!is_secondary_view_entity(entity))
            {
                continue;
            }
            Light* light = entity->GetComponent<Light>();
            if (!light)
            {
                continue;
            }
            light->ClearAtlasRectangles();
            if (IsRayTracedShadowsActive())
            {
                continue;
            }
            if (light->GetIndex() == numeric_limits<uint32_t>::max())
            {
                continue;
            }
            // skip lights that are out of shadow distance, the shader and shadow pass
            // both rely on rect.IsDefined() so leaving them unallocated is enough
            if (!light->IsShadowEffective())
            {
                continue;
            }
            for (uint32_t i = 0; i < light->GetSliceCount(); ++i)
            {
                m_shadow_slices.emplace_back(light, i, 0, math::Rectangle::Zero);
            }
        }
        if (m_shadow_slices.empty())
        {
            return;
        }

        // row-based packing: lays out uniform-sized slices left-to-right, wrapping to the next row
        // when rects is null it only tests whether the layout fits; when non-null it writes the rectangles
        auto pack_row = [&](uint32_t slice_res, uint32_t num_slices, vector<ShadowSlice>* rects) -> bool
        {
            if (slice_res > resolution_atlas)
            {
                return false;
            }

            uint32_t x = 0, y = 0, row_h = 0;
            for (uint32_t i = 0; i < num_slices; ++i)
            {
                uint32_t left_pad = (x == 0) ? 0 : border;
                uint32_t placed_x = x + left_pad;

                if (placed_x + slice_res > resolution_atlas)
                {
                    y        += row_h + border;
                    x         = 0;
                    row_h     = 0;
                    placed_x  = 0;
                }

                if (placed_x + slice_res > resolution_atlas || y + slice_res > resolution_atlas)
                {
                    return false;
                }

                if (rects)
                {
                    (*rects)[i].resolution = slice_res;
                    (*rects)[i].rect       = math::Rectangle(
                        static_cast<float>(placed_x), static_cast<float>(y),
                        static_cast<float>(slice_res), static_cast<float>(slice_res));
                }

                x     = placed_x + slice_res;
                row_h = max(row_h, slice_res);
            }
            return true;
        };

        // binary search for max uniform slice resolution
        uint32_t max_slice_res = resolution_atlas;
        uint32_t num_slices    = static_cast<uint32_t>(m_shadow_slices.size());
        if (num_slices > 1)
        {
            uint32_t low  = min_slice_res;
            uint32_t high = resolution_atlas;
            while (low < high)
            {
                uint32_t mid = (low + high + 1) / 2;
                if (pack_row(mid, num_slices, nullptr))
                {
                    low = mid;
                }
                else
                {
                    high = mid - 1;
                }
            }
            max_slice_res = low;
        }
        max_slice_res = max(max_slice_res, min_slice_res);

        // assign rectangles
        pack_row(max_slice_res, num_slices, &m_shadow_slices);

        for (const auto& slice : m_shadow_slices)
        {
            slice.light->SetAtlasRectangle(slice.slice_index, slice.rect);
        }
    }

    bool Renderer::Screenshot()
    {
        return Screenshot("");
    }

    bool Renderer::Screenshot(const string& file_path)
    {
        lock_guard<mutex> lock(screenshot_mutex);
        if (screenshot.pending || screenshot.ready)
        {
            SP_LOG_WARNING("Screenshot already pending");
            return false;
        }

        screenshot = make_screenshot_request(file_path);
        return true;
    }

    uint32_t Renderer::GetScreenshotSavesInFlight()
    {
        return screenshot_saves_in_flight.load();
    }

    uint32_t Renderer::GetScreenshotSaveFailures()
    {
        return screenshot_save_failures.load();
    }

    void Renderer::SetCleanCapture(bool enabled)
    {
        clean_capture = enabled;
    }

    void Renderer::NotifyCameraCut()
    {
        camera_cut_pending = true;
    }

    bool Renderer::ScreenshotSecondary(
        const string& file_path
    )
    {
        lock_guard<mutex> lock(screenshot_mutex);
        if (secondary_view_request_generation == 0)
        {
            SP_LOG_WARNING(
                "Secondary screenshot requested with no preview to capture"
            );
            return false;
        }

        // a request that has been rendered is waiting for its readback this frame, so it has to be left
        // alone. one that is only pending has not been touched by the gpu yet and the newer request is the
        // one the caller wants, superseding it is what stops a single stranded capture from refusing every
        // capture taken afterwards
        if (secondary_capture.ready)
        {
            SP_LOG_WARNING(
                "Secondary screenshot is still being written, try again"
            );
            return false;
        }

        secondary_capture =
            make_screenshot_request(file_path, true);
        return true;
    }

    RHI_Texture* Renderer::GetUiScreenshotTarget(uint32_t width, uint32_t height)
    {
        lock_guard<mutex> lock(screenshot_mutex);
        if (!screenshot.pending || screenshot.ready || !screenshot.ui || width == 0 || height == 0)
        {
            return nullptr;
        }

        if (!screenshot_ui_target || screenshot_ui_target->GetWidth() != width || screenshot_ui_target->GetHeight() != height)
        {
            screenshot_ui_target = make_shared<RHI_Texture>(
                RHI_Texture_Type::Type2D, width, height, 1, 1,
                RHI_Format::R8G8B8A8_Unorm, RHI_Texture_Srv | RHI_Texture_Rtv | RHI_Texture_ClearBlit, "screenshot_ui"
            );
        }

        return screenshot_ui_target->GetRhiResource() ? screenshot_ui_target.get() : nullptr;
    }

    void Renderer::SetUiScreenshotRecorded()
    {
        lock_guard<mutex> lock(screenshot_mutex);
        if (screenshot.pending && screenshot.ui)
        {
            screenshot.exr_color_space = ImageColorSpace::Srgb;
            screenshot.pending         = false;
            screenshot.ready           = true;
        }
    }

    void Renderer::Pass_Screenshot(RHI_Texture* tex_pre_tonemap)
    {
        {
            lock_guard<mutex> lock(screenshot_mutex);
            if (
                !screenshot.pending ||
                screenshot.ready ||
                screenshot.ui ||
                secondary_render_root_active ||
                !tex_pre_tonemap
            )
            {
                return;
            }
        }

        RHI_Texture* tex_sdr =
            GetRenderTarget(
                Renderer_RenderTarget::screenshot_sdr
            );
        RHI_Texture* tex_ping = GetRenderTarget(Renderer_RenderTarget::screenshot_sdr_2);
        if (!tex_sdr || !tex_ping)
        {
            EnsureScreenshotTargets();
            tex_sdr  = GetRenderTarget(Renderer_RenderTarget::screenshot_sdr);
            tex_ping = GetRenderTarget(Renderer_RenderTarget::screenshot_sdr_2);
        }
        if (!tex_sdr || !tex_ping)
        {
            return;
        }

        RHI_CommandList::BeginMarker("screenshot_sdr");
        {
            Pass_Tonemap(tex_pre_tonemap, tex_sdr, true);

            RHI_Texture* tex_in  = tex_sdr;
            RHI_Texture* tex_out = tex_ping;
            Pass_PostProcess_DisplayEffects(tex_in, tex_out, false, true);

            if (tex_in != tex_sdr)
            {
                RHI_CommandList::Copy(tex_in, tex_sdr, false);
            }

            if (!secondary_render_root_active && !clean_capture)
            {
                Pass_PostProcess_EditorOverlays(tex_sdr);
                Pass_Text(tex_sdr, false);
            }
            else if (!secondary_render_root_active)
            {
                Pass_Text(tex_sdr, false, true);
            }
        }
        RHI_CommandList::EndMarker();

        lock_guard<mutex> lock(screenshot_mutex);
        if (screenshot.pending)
        {
            // Snapshot the encoding of this frame before another view changes constants.
            screenshot.exr_color_space = Renderer::view().frame.hdr_enabled > 1.5f ? ImageColorSpace::LinearRec709 :
                (Renderer::view().frame.hdr_enabled > 0.0f ? ImageColorSpace::Hdr10 : ImageColorSpace::Srgb);
            screenshot.pending = false;
            screenshot.ready   = true;
        }
    }

    void Renderer::Pass_ScreenshotXr()
    {
        {
            lock_guard<mutex> lock(screenshot_mutex);
            if (
                !screenshot.pending ||
                screenshot.ready ||
                screenshot.ui ||
                secondary_render_root_active
            )
            {
                return;
            }
        }

        RHI_Texture* tex_stereo = GetRenderTarget(Renderer_RenderTarget::frame_output_stereo);
        RHI_Texture* tex_sdr    = GetRenderTarget(Renderer_RenderTarget::screenshot_sdr);
        if (!tex_sdr)
        {
            EnsureScreenshotTargets();
            tex_sdr = GetRenderTarget(Renderer_RenderTarget::screenshot_sdr);
        }
        if (!tex_stereo || !tex_sdr)
        {
            return;
        }

        // both eyes side by side like the desktop mirror, srgb encoded for the png
        RHI_CommandList::BeginMarker("screenshot_xr");
        {
            RHI_CommandList::SetShader(GetShader(Renderer_Shader::xr_mirror_c), "screenshot_xr");
            RHI_CommandList::SetTexture(Renderer_BindingsSrv::tex, tex_stereo, rhi_all_mips, 0, 0);
            RHI_CommandList::SetTexture(Renderer_BindingsSrv::tex2, tex_stereo, rhi_all_mips, 0, 1);
            RHI_CommandList::SetTexture(Renderer_BindingsUav::tex, tex_sdr);
            m_pcb_pass_cpu.set(pass_xr_mirror::display_size, Vector2(static_cast<float>(tex_sdr->GetWidth()), static_cast<float>(tex_sdr->GetHeight())));
            m_pcb_pass_cpu.set(pass_xr_mirror::encode_srgb, true);
            RHI_CommandList::Dispatch(tex_sdr);
        }
        RHI_CommandList::EndMarker();

        lock_guard<mutex> lock(screenshot_mutex);
        if (screenshot.pending)
        {
            screenshot.pending = false;
            screenshot.ready   = true;
        }
    }

    void Renderer::Pass_XrMirror()
    {
        RHI_Texture* tex_stereo = GetRenderTarget(Renderer_RenderTarget::frame_output_stereo);
        RHI_Texture* tex_output = GetRenderTarget(Renderer_RenderTarget::frame_output);
        if (!tex_stereo || !tex_output)
        {
            return;
        }

        // frame_output is eye sized in stereo but shown stretched over the desktop viewport
        Vector2 display_size = Vector2(static_cast<float>(tex_output->GetWidth()), static_cast<float>(tex_output->GetHeight()));
        uint32_t output_w = 0, output_h = 0, render_w = 0, render_h = 0;
        float viewport_w = 0.0f, viewport_h = 0.0f;
        if (Xr::TryGetPersistedDesktopResolution(output_w, output_h, render_w, render_h, viewport_w, viewport_h) && viewport_w > 0.0f && viewport_h > 0.0f)
        {
            display_size = Vector2(viewport_w, viewport_h);
        }

        RHI_CommandList::BeginPass("xr_mirror");
        {
            RHI_CommandList::SetShader(GetShader(Renderer_Shader::xr_mirror_c));
            RHI_CommandList::SetTexture(Renderer_BindingsSrv::tex, tex_stereo, rhi_all_mips, 0, 0);
            RHI_CommandList::SetTexture(Renderer_BindingsSrv::tex2, tex_stereo, rhi_all_mips, 0, 1);
            RHI_CommandList::SetTexture(Renderer_BindingsUav::tex, tex_output);
            m_pcb_pass_cpu.set(pass_xr_mirror::display_size, display_size);
            m_pcb_pass_cpu.set(pass_xr_mirror::encode_srgb, false);
            RHI_CommandList::Dispatch(tex_output);
        }
        RHI_CommandList::EndPass();
    }

    void Renderer::FinalizeScreenshotReadback()
    {
        array<screenshot_request, 2> requests;
        uint32_t request_count = 0;
        {
            lock_guard<mutex> lock(screenshot_mutex);
            if (screenshot.ready)
            {
                requests[request_count++] = screenshot;
                screenshot = {};
            }
            if (secondary_capture.ready)
            {
                requests[request_count++] = secondary_capture;
                secondary_capture = {};
            }
        }
        if (request_count == 0)
        {
            return;
        }

        // present submitted the capture work, wait once so every readback sees finished pixels
        RHI_Device::QueueWaitAll(true);

        for (uint32_t i = 0; i < request_count; i++)
        {
            const screenshot_request& request = requests[i];
            RHI_Texture* tex_sdr =
                request.ui
                    ? screenshot_ui_target.get()
                    : request.secondary_view
                        ? secondary_view_output.get()
                        : GetRenderTarget(
                            Renderer_RenderTarget::screenshot_sdr
                        );
            if (!tex_sdr)
            {
                continue;
            }

            shared_ptr<RHI_Buffer> sdr_staging = copy_texture_to_staging(tex_sdr);
            shared_ptr<RHI_Buffer> exr_staging;
            if (request.save_exr && !request.secondary_view && !request.ui && !Xr::IsSessionRunning())
            {
                exr_staging = copy_texture_to_staging(GetRenderTarget(Renderer_RenderTarget::frame_output));
            }

            save_screenshot_async(
                request,
                sdr_staging,
                exr_staging,
                tex_sdr->GetWidth(),
                tex_sdr->GetHeight(),
                tex_sdr->GetChannelCount(),
                tex_sdr->GetBitsPerChannel()
            );
        }
    }

    uint32_t Renderer::GetClusterOverflowCount()
    {
        // best effort readback of a host visible buffer, the value can lag a frame on discrete gpus
        RHI_Buffer* buffer = GetBuffer(Renderer_Buffer::ClusterStats);
        if (buffer)
        {
            if (void* mapped = buffer->GetMappedData())
            {
                return *static_cast<const uint32_t*>(mapped);
            }
        }
        return 0;
    }

    // draw calls

    bool Renderer::UpdateSkysphereConvergenceState()
    {
        const uint32_t temporal_convergence_frames = 8;
        Light* directional_light         = World::GetDirectionalLight();
        const bool has_directional_light = directional_light != nullptr;
        const Quaternion light_rotation  = has_directional_light && directional_light->GetEntity() ? directional_light->GetEntity()->GetRotation() : Quaternion::Identity;
        const float light_intensity      = has_directional_light ? directional_light->GetIntensityPhotometric() : 0.0f;
        const float cloud_coverage       = Environment::GetCloudCoverageEffective();
        const Vector3 wind               = World::GetWind();
        const Vector2 cloud_seed_offset  = World::GetCloudSeedOffset();
        const double expected_time       = m_pass_state.cloud_environment.time + static_cast<double>(Renderer::view().frame.delta_time);
        const bool time_discontinuous    = !m_pass_state.sky.first_frame && abs(Renderer::view().frame.time - expected_time) > 0.25;
        const bool camera_teleported     = (Renderer::view().frame.camera_position - Renderer::view().frame.camera_position_previous).LengthSquared() > 250000.0f;
        // Normal daylight/weather animation is handled by the rolling panorama
        // and scheduled LUT updates. Exact rotation comparisons (and absolute
        // lux deltas) restarted all eight full-resolution warmup frames forever.
        // Only a discontinuity needs to discard the temporal history.
        const bool light_changed         = directional_light != m_pass_state.cloud_environment.light ||
                                           abs(Quaternion::Dot(light_rotation, m_pass_state.cloud_environment.light_rotation)) < 0.99999f ||
                                           abs(light_intensity - m_pass_state.cloud_environment.light_intensity) > max(1.0f, abs(m_pass_state.cloud_environment.light_intensity) * 0.05f) ||
                                           abs(cloud_coverage - m_pass_state.cloud_environment.coverage) > 0.1f;
        const bool wind_changed          = (wind - m_pass_state.cloud_environment.wind).LengthSquared() > 4.0f;
        const bool seed_changed          = cloud_seed_offset != m_pass_state.cloud_environment.seed_offset;
        const auto environment = World::GetEnvironment();
        const bool celestial_changed = (environment.moon - m_pass_state.cloud_environment.sky_moon).LengthSquared() > 0.0001f ||
            (environment.equatorial_x - m_pass_state.cloud_environment.sky_equatorial_x).LengthSquared() > 0.0001f;
        const bool cloud_state_changed   = celestial_changed || m_pass_state.sky.first_frame || light_changed || wind_changed || seed_changed || time_discontinuous || camera_teleported;
        m_pass_state.sky.state_changed_this_frame =
            cloud_state_changed;
        if (cloud_state_changed)
        {
            m_pass_state.sky.frames_remaining     = temporal_convergence_frames;
            m_pass_state.cloud_history.valid      = false;
            m_pass_state.cloud_environment.environment_dirty = true;
        }
        m_pass_state.cloud_environment.sky_moon = environment.moon;
        m_pass_state.cloud_environment.sky_equatorial_x = environment.equatorial_x;
        m_pass_state.cloud_environment.light           = directional_light;
        m_pass_state.cloud_environment.light_rotation  = light_rotation;
        m_pass_state.cloud_environment.light_intensity = light_intensity;
        m_pass_state.cloud_environment.coverage        = cloud_coverage;
        m_pass_state.cloud_environment.wind            = wind;
        m_pass_state.cloud_environment.seed_offset     = cloud_seed_offset;
        m_pass_state.cloud_environment.time            = Renderer::view().frame.time;

        // capture this frame's warmup status before we decrement, so Pass_Skysphere can pick
        // between the full-burst and the partial-dispatch mode on the same frame
        m_pass_state.sky.warmup_this_frame = m_pass_state.sky.frames_remaining > 0;

        // progressive average, the n-th warmup frame weighs 1/n so the first one fully replaces the panorama
        const uint32_t warmup_frame_index = temporal_convergence_frames - m_pass_state.sky.frames_remaining;
        m_pass_state.sky.warmup_blend     = 1.0f / static_cast<float>(warmup_frame_index + 1);

        if (m_pass_state.sky.frames_remaining > 0)
        {
            m_pass_state.sky.frames_remaining--;
        }

        m_pass_state.sky.first_frame           = false;
        m_pass_state.sky.had_directional_light = has_directional_light;

        // a world without a directional light still has a sun in light slot 0, the default one
        // UpdateLights writes, so the panorama keeps refreshing instead of stalling on black
        return true;
    }

    void Renderer::SetStandardResources(RHI_CommandList* cmd_list)
    {
        if (cmd_list)
        {
            RHI_Device::Bind(cmd_list);
        }
        if (!RHI_Device::IsRecording())
        {
            return;
        }
        RHI_CommandList::SetConstantBuffer(Renderer_BindingsCb::frame, GetBuffer(Renderer_Buffer::ConstantFrame));
        RHI_CommandList::SetTexture("tex_perlin", GetStandardTexture(Renderer_StandardTexture::Noise_perlin));
        RHI_Texture* tex_ies = ies::get_atlas();
        RHI_CommandList::SetTexture("tex_ies", tex_ies && tex_ies->GetRhiSrv() ? tex_ies : GetStandardTexture(Renderer_StandardTexture::Black));
        RHI_CommandList::SetBuffer(Renderer_BindingsUav::decals, GetBuffer(Renderer_Buffer::Decals));
        RHI_CommandList::SetBuffer(Renderer_BindingsUav::rain_occlusion, GetBuffer(Renderer_Buffer::RainOcclusion));
        if (RHI_Buffer* impostor_texels = GeometryBuffer::GetImpostorTexelBuffer() ? GeometryBuffer::GetImpostorTexelBuffer() : GeometryBuffer::GetMeshletVertexBuffer())
        {
            RHI_CommandList::SetBuffer(Renderer_BindingsUav::impostor_texels, impostor_texels);
        }

        RHI_Texture* tex_exposure = GetRenderTarget(Renderer_RenderTarget::auto_exposure_previous);
        if (tex_exposure)
        {
            RHI_CommandList::SetTexture("tex_effective_exposure", tex_exposure);
        }

        bool is_graphics_queue = RHI_Device::GetBoundQueueType() == RHI_Queue_Type::Graphics;
        RHI_Texture* tex_wind  = GetRenderTarget(Renderer_RenderTarget::wind_field);
        if (is_graphics_queue && tex_wind)
        {
            RHI_CommandList::SetTexture("tex_wind_field", tex_wind);
        }

        // pushed grass, shrubs and branches, sampled by every vegetation vertex path
        if (is_graphics_queue)
        {
            const auto& vegetation = m_pass_state.vegetation_interaction;
            const bool readable    = vegetation.valid && !vegetation.writing && vegetation.fields[0];
            RHI_Texture* black     = GetStandardTexture(Renderer_StandardTexture::Black);
            RHI_CommandList::SetTexture("tex_vegetation_interaction", readable ? vegetation.fields[vegetation.current].get() : black);
            RHI_CommandList::SetTexture("tex_vegetation_interaction_previous", readable ? vegetation.fields[1 - vegetation.current].get() : black);

            // tire ruts, read by the terrain tessellation, the g-buffer and the grass
            RHI_Texture* deformation = m_pass_state.terrain_deformation.texture.get();
            RHI_CommandList::SetTexture("tex_terrain_deformation", IsTerrainDeformationReady() ? deformation : black);

            // interactive ocean wake, read by the water vertices, the g-buffer and the foam in reflections_apply
            const auto& wake = m_pass_state.ocean_wake;
            const bool wake_ready = IsOceanWakeReady();
            RHI_CommandList::SetTexture("tex_ocean_wake", wake_ready ? wake.textures[wake.current].get() : black);
            RHI_CommandList::SetTexture("tex_ocean_wake_previous", wake_ready ? wake.textures[1 - wake.current].get() : black);
        }

        // the occupied car's water, read by the g-buffer
        if (is_graphics_queue)
        {
            RHI_CommandList::SetTexture("tex_car_rain_surface", GetRenderTarget(Renderer_RenderTarget::car_rain_surface));
            RHI_CommandList::SetTexture("tex_car_rain_micro", GetRenderTarget(Renderer_RenderTarget::car_rain_micro));
            RHI_CommandList::SetTexture("tex_car_rain_ids", GetRenderTarget(Renderer_RenderTarget::car_rain_ids));
            RHI_CommandList::SetBuffer("car_rain_drops", GetBuffer(Renderer_Buffer::CarRainDrops));
            RHI_CommandList::SetTexture("tex_car_dirt_color", GetRenderTarget(Renderer_RenderTarget::car_dirt_color));
            RHI_CommandList::SetTexture("tex_car_dirt_state", GetRenderTarget(Renderer_RenderTarget::car_dirt_state));
            RHI_CommandList::SetTexture("tex_car_dirt_wear", GetRenderTarget(Renderer_RenderTarget::car_dirt_wear));
        }

        // terrain analysis maps, bound globally because the raster, reflection and gi paths all
        // evaluate the same terrain surface and must agree on it
        // the slots always get a descriptor, the shader gates the read on the has_maps flag but the
        // binding itself must stay valid whether or not a terrain exists
        {
            RHI_Texture* map_a = m_pass_state.terrain_enabled ? m_pass_state.terrain.map_a : nullptr;
            RHI_Texture* map_b = m_pass_state.terrain_enabled ? m_pass_state.terrain.map_b : nullptr;
            const bool ready =
                map_a && map_b &&
                map_a->GetResourceState() == ResourceState::PreparedForGpu &&
                map_b->GetResourceState() == ResourceState::PreparedForGpu;

            RHI_Texture* fallback = GetStandardTexture(Renderer_StandardTexture::Noise_perlin);
            RHI_CommandList::SetTexture("tex_terrain_map_a", ready ? map_a : fallback);
            RHI_CommandList::SetTexture("tex_terrain_map_b", ready ? map_b : fallback);

            RHI_Texture* height = m_pass_state.terrain_enabled ? m_pass_state.terrain.height_map : nullptr;
            const bool height_ready =
                height &&
                height->GetResourceState() == ResourceState::PreparedForGpu;
            RHI_CommandList::SetTexture(
                "tex_terrain_height",
                height_ready ? height : fallback
            );

            const bool detail_ready = IsTerrainDetailReady();
            RHI_CommandList::SetTexture("tex_terrain_detail_table", detail_ready ? m_pass_state.terrain.detail_table : fallback);
            RHI_CommandList::SetTexture("tex_terrain_detail_atlas", detail_ready ? m_pass_state.terrain.detail_atlas : fallback);

            RHI_Texture* shore = ocean_shore::get_texture();
            RHI_CommandList::SetTexture("tex_ocean_shore", shore ? shore : fallback);

            RHI_Texture* prop_mask = m_pass_state.grass_far.prop_mask;
            const bool prop_mask_ready =
                prop_mask &&
                prop_mask->GetResourceState() == ResourceState::PreparedForGpu;
            RHI_CommandList::SetTexture("tex_terrain_prop_mask", prop_mask_ready ? prop_mask : fallback);
        }

        Renderer_RenderTarget ocean_displacement_current = m_pass_state.ocean_history.SelectWrite(
            Renderer_RenderTarget::ocean_displacement,
            Renderer_RenderTarget::ocean_displacement_previous
        );
        Renderer_RenderTarget ocean_displacement_previous =
            m_pass_state.ocean_history.valid ?
            m_pass_state.ocean_history.SelectRead(
                Renderer_RenderTarget::ocean_displacement,
                Renderer_RenderTarget::ocean_displacement_previous
            ) :
            ocean_displacement_current;

        RHI_Texture* tex_ocean_disp = GetRenderTarget(
            ocean_displacement_current
        );
        RHI_Texture* tex_ocean_disp_previous = GetRenderTarget(
            ocean_displacement_previous
        );
        RHI_Texture* tex_ocean_norm = GetRenderTarget(
            Renderer_RenderTarget::ocean_normal
        );
        if (tex_ocean_disp)
        {
            RHI_CommandList::SetTexture("tex_ocean_displacement", tex_ocean_disp);
        }
        if (tex_ocean_disp_previous)
        {
            RHI_CommandList::SetTexture("tex_ocean_displacement_previous", tex_ocean_disp_previous);
        }
        if (tex_ocean_norm)
        {
            RHI_CommandList::SetTexture("tex_ocean_normal", tex_ocean_norm);
        }
    }

    void Renderer::Pass_VariableRateShading()
    {
        if (
            !cvar_variable_rate_shading.GetValueAs<bool>() ||
            IsSecondaryViewActive()
        )
        {
            return;
        }

        RHI_Shader* shader_c = GetShader(Renderer_Shader::variable_rate_shading_c);
        RHI_Texture* tex_in  = GetRenderTarget(Renderer_RenderTarget::frame_output);
        RHI_Texture* tex_out = GetRenderTarget(Renderer_RenderTarget::shading_rate);
        if (!shader_c || !shader_c->IsCompiled() || !tex_in || !tex_out)
        {
            return;
        }

        // clear to full rate on first use or after render target recreation
        if (tex_out != m_pass_state.vrs_last_cleared_texture)
        {
            RHI_CommandList::ClearTexture(tex_out, Color(0.0f, 0.0f, 0.0f, 0.0f));
            m_pass_state.vrs_last_cleared_texture = tex_out;
        }

        RHI_CommandList::BeginPass("variable_rate_shading");
        {
            RHI_CommandList::SetShader(shader_c);
            RHI_CommandList::SetTexture(Renderer_BindingsSrv::tex, tex_in);
            RHI_CommandList::SetTexture(Renderer_BindingsUav::tex_uint, tex_out);
            RHI_CommandList::Dispatch(tex_out);
        }
        RHI_CommandList::EndPass();
    }

    void Renderer::Pass_ComputeBatchA(
        bool update_skysphere
    )
    {
        RHI_CommandList::BeginMarker("compute_batch_a");

        m_pass_state.skip_rt_trace = false;

        // accel structures first so batch b's rt passes inherit the tlas via compute queue order
        UpdateAccelerationStructures();

        // only needs the camera frustum and the light list, so it overlaps the gbuffer pass instead of waiting in batch b
        Pass_LightClusterAssign();

        {
            RHI_Shader* lut_shader = GetShader(Renderer_Shader::light_integration_brdf_specular_lut_c);
            const uint64_t lut_hash = lut_shader ? lut_shader->GetHash() : 0;
            if (lut_shader && lut_shader->IsCompiled() &&
                (!m_pass_state.brdf_lut_produced || m_pass_state.brdf_lut_shader_hash != lut_hash))
            {
                Pass_Lut_BrdfSpecular();
                m_pass_state.brdf_lut_produced    = true;
                m_pass_state.brdf_lut_shader_hash = lut_hash;
            }
        }

        if (update_skysphere)
        {
            if (!m_pass_state.atmosphere_lut_produced)
            {
                Pass_Lut_AtmosphericScattering();
                m_pass_state.atmosphere_lut_produced = true;
            }
            if (!m_pass_state.cloud_noise_produced)
            {
                Pass_CloudNoise();
                m_pass_state.cloud_noise_produced = true;
            }
        }

        RHI_CommandList::EndMarker();
    }

    void Renderer::Pass_ComputeBatchB(bool include_gi)
    {
        RHI_CommandList::BeginMarker("compute_batch_b");
        Pass_ScreenSpaceAmbientOcclusion();
        Pass_ScreenSpaceShadows();
        Pass_RayTracedShadows();
        if (include_gi)
        {
            Pass_ReSTIR_PathTracing();
            Pass_ReSTIR_Denoising();
        }
        RHI_CommandList::EndMarker();
    }

    bool Renderer::Pass_ReSTIR_CopyGBuffer()
    {
        // two sets of copies are needed, so restir has to be live, ray traced and mono, and overlapping a time sliced
        // compute queue only adds a frame of latency, so the copies are skipped there
        const bool async_wanted = cvar_restir_pt_async.GetValueAs<bool>() && RHI_Device::IsAsyncComputeConcurrent();
        if (!async_wanted || secondary_render_root_active || IsStereoView() || Window::IsMinimized() || !cvar_restir_pt.GetValueAs<bool>() || !RHI_Device::IsSupportedRayTracing())
        {
            m_pass_state.restir.gbuffer_copy_frames = 0;
            return false;
        }

        const uint32_t index = m_pass_state.restir.gbuffer_copy_index ^ 1;
        const uint32_t base  = static_cast<uint32_t>(index == 0 ? Renderer_RenderTarget::restir_gbuffer_color_0 : Renderer_RenderTarget::restir_gbuffer_color_1);
        const Renderer_RenderTarget sources[] =
        {
            Renderer_RenderTarget::gbuffer_color,
            Renderer_RenderTarget::gbuffer_normal,
            Renderer_RenderTarget::gbuffer_material,
            Renderer_RenderTarget::gbuffer_emissive,
            Renderer_RenderTarget::gbuffer_velocity,
            Renderer_RenderTarget::gbuffer_depth
        };
        for (uint32_t i = 0; i < 6; i++)
        {
            RHI_Texture* source      = GetRenderTarget(sources[i]);
            RHI_Texture* destination = GetRenderTarget(static_cast<Renderer_RenderTarget>(base + i));
            if (!source || !destination || source->GetWidth() != destination->GetWidth() || source->GetHeight() != destination->GetHeight())
            {
                m_pass_state.restir.gbuffer_copy_frames = 0;
                return false;
            }
        }

        RHI_CommandList::BeginMarker("restir_gbuffer_copy");
        for (uint32_t i = 0; i < 6; i++)
        {
            RHI_CommandList::Copy(GetRenderTarget(sources[i]), GetRenderTarget(static_cast<Renderer_RenderTarget>(base + i)), false);
        }
        RHI_CommandList::EndMarker();

        m_pass_state.restir.gbuffer_copy_index = index;
        m_pass_state.restir.gbuffer_copy_frames++;
        return true;
    }

    void Renderer::Pass_ReSTIR_Async()
    {
        // the passes resolve the gbuffer through the render target table, point it at this frame's
        // copy and its history at last frame's copy, graphics is free to overwrite the real ones
        const uint32_t current  = m_pass_state.restir.gbuffer_copy_index;
        const uint32_t base     = static_cast<uint32_t>(current == 0 ? Renderer_RenderTarget::restir_gbuffer_color_0 : Renderer_RenderTarget::restir_gbuffer_color_1);
        const uint32_t base_old = static_cast<uint32_t>(current == 0 ? Renderer_RenderTarget::restir_gbuffer_color_1 : Renderer_RenderTarget::restir_gbuffer_color_0);
        const pair<Renderer_RenderTarget, uint32_t> redirects[] =
        {
            { Renderer_RenderTarget::gbuffer_color,             base + 0 },
            { Renderer_RenderTarget::gbuffer_normal,            base + 1 },
            { Renderer_RenderTarget::gbuffer_material,          base + 2 },
            { Renderer_RenderTarget::gbuffer_emissive,          base + 3 },
            { Renderer_RenderTarget::gbuffer_velocity,          base + 4 },
            { Renderer_RenderTarget::gbuffer_depth,             base + 5 },
            { Renderer_RenderTarget::gbuffer_normal_previous,   base_old + 1 },
            { Renderer_RenderTarget::gbuffer_velocity_previous, base_old + 4 },
            { Renderer_RenderTarget::gbuffer_depth_previous,    base_old + 5 }
        };
        auto& render_targets = GetRenderTargets();
        auto redirect = [&]()
        {
            for (const auto& [target, copy] : redirects)
            {
                swap(render_targets[static_cast<uint32_t>(target)], render_targets[copy]);
            }
        };

        redirect();
        RHI_CommandList::BeginMarker("restir_async");
        Pass_ReSTIR_PathTracing();
        Pass_ReSTIR_Denoising();
        RHI_CommandList::EndMarker();
        redirect();
    }

    void Renderer::Pass_GraphicsPhase1_Geometry(bool scatter_prepared_async)
    {
        // the async path ran the early cull already, its grass populate reads the early hi-z
        if (!scatter_prepared_async)
        {
            Pass_Cull_Early();
        }
        Pass_Cull_Late();
        // populate the gpu procedural grass ring before the geometry rasters that consume it
        // safe to run unconditionally, the pass early-outs when grass is disabled
        if (!scatter_prepared_async)
        {
            Pass_Grass_Populate();
            Pass_Grass_Interaction();
        }
        if (scatter_prepared_async)
        {
            // Depth does not consume procedural scatter. Submit it without the
            // scatter wait; the subsequent g-buffer submission waits for it.
            RHI_Device::Submit(RHI_Frame_List::Graphics, nullptr, false);
            RHI_Device::Bind(RHI_Frame_List::Graphics);
        }
        Pass_GBuffer(false, scatter_prepared_async);
        Pass_MeshletVisualize();
    }

    void Renderer::Pass_GraphicsPhase2_ShadowsAndRT()
    {
        // Select one shadow technique before entering the raster pass.
        if (!IsRayTracedShadowsActive())
        {
            Pass_ShadowMaps();
        }
    }

    void Renderer::WaitForAsyncFog()
    {
        if (!m_cross_queue_sync.fog_timeline)
        {
            return;
        }

        // everything recorded so far only needed compute b, the rest of the frame also reads the fog
        RHI_Device::Submit(RHI_Frame_List::Graphics, nullptr, false, nullptr, m_cross_queue_sync.pending_compute_timeline, m_cross_queue_sync.pending_compute_timeline_value);
        RHI_Device::Bind(RHI_Frame_List::Graphics);
        m_cross_queue_sync.pending_compute_timeline       = m_cross_queue_sync.fog_timeline;
        m_cross_queue_sync.pending_compute_timeline_value = m_cross_queue_sync.fog_timeline_value;
        RHI_Device::SetFrameWait(m_cross_queue_sync.fog_timeline, m_cross_queue_sync.fog_timeline_value);
        m_cross_queue_sync.fog_timeline       = nullptr;
        m_cross_queue_sync.fog_timeline_value = 0;
    }

    void Renderer::ProduceFrame_PerEye(uint32_t eye, uint32_t eye_layer, bool early_work_recorded, bool clouds_prepared_early)
    {
        Pass_Light(false, eye_layer);
        Pass_Light_Composition(false, eye_layer);
        // the refraction background is copied below, so opaque ambient has to be in it already,
        // otherwise anything behind glass that is not in direct light reads as black
        Pass_Light_Ibl(false, eye_layer);

        const bool clouds_prepared = early_work_recorded ? clouds_prepared_early : Pass_Clouds_Prepare(eye_layer);
        RHI_CommandList::Blit(GetRenderTarget(Renderer_RenderTarget::frame_render), GetRenderTarget(Renderer_RenderTarget::frame_render_opaque), false);
        if (clouds_prepared)
        {
            Pass_Clouds_Composite(eye_layer, GetRenderTarget(Renderer_RenderTarget::frame_render_opaque));
        }

        if (eye == 0)
        {
            Pass_LightClusterVisualize();
        }

        if (m_transparents_present)
        {
            Pass_GBuffer(true);
            Pass_Light(true, eye_layer);
            Pass_Light_Composition(true, eye_layer);
        }

        // trace after transparent gbuffer so glass pixels own their reflection rays instead of whatever was behind them
        if (!early_work_recorded || m_transparents_present)
        {
            Pass_Reflections_Trace(eye_layer);
        }

        if (m_transparents_present)
        {
            Pass_Light_Ibl(true, eye_layer);
        }
        WaitForAsyncFog();
        Pass_Reflections_Shade(eye_layer);
        Pass_Reflections_Denoise(eye_layer);

        Pass_Reflections_Apply(eye_layer);
        Pass_LightFlares(eye_layer);
        if (clouds_prepared)
        {
            const bool xr_stereo = IsStereoView();
            Pass_Clouds(eye_layer, !xr_stereo || eye == Xr::eye_count - 1);
        }

        // Compose camera transport once, including sky, clouds, IBL and reflections.
        Pass_Fog_Composite(eye_layer);

        // particles remain foreground content and composite after world space clouds
        Pass_Upscaler_Reactivity(eye_layer);
        // particles simulate as they draw, a second view would step the world's emitters twice
        if (eye == 0 && !secondary_render_root_active)
        {
            Pass_Particles();
        }

        Pass_AA_Upscale(eye_layer);
        Pass_PostProcess(eye_layer);

        if (IsStereoView())
        {
            RHI_CommandList::BlitToArrayLayer(
                GetRenderTarget(Renderer_RenderTarget::frame_output),
                GetRenderTarget(Renderer_RenderTarget::frame_output_stereo),
                eye
            );
        }
    }

    bool Renderer::IsPathTracingActive()
    {
        return cvar_path_tracing.GetValueAs<bool>() && RHI_Device::IsSupportedRayTracing() &&
            !secondary_render_root_active && !IsStereoView();
    }

    uint32_t Renderer::GetPathTracingSampleCount()
    {
        return m_pass_state.path_tracing.samples;
    }

    void Renderer::ResetPathTracing()
    {
        m_pass_state.path_tracing.samples = 0;
    }

    void Renderer::Pass_PathTracing(bool sky_changed)
    {
        auto& history = m_pass_state.path_tracing;
        RHI_Texture* output = GetRenderTarget(Renderer_RenderTarget::frame_output);
        const bool has_scene = m_tlas && m_tlas->GetRhiResource();
        RHI_Shader* shader = GetShader(has_scene ? Renderer_Shader::path_tracing_c : Renderer_Shader::path_tracing_sky_c);
        if (!GetViewCamera() || frame_is_loading || ray_tracing_pending_blas || m_pass_state.skip_rt_trace || !shader || !shader->IsCompiled())
        {
            history.samples = 0;
            RHI_CommandList::ClearTexture(output, Color::standard_black);
            return;
        }

        // Trace at output resolution: pixel filtering is stochastic, no raster AA/upscaler is involved.
        if (!history.accumulation[0] || history.accumulation[0]->GetWidth() != output->GetWidth() ||
            history.accumulation[0]->GetHeight() != output->GetHeight())
        {
            for (uint32_t i = 0; i < 2; ++i)
                history.accumulation[i] = make_shared<RHI_Texture>(RHI_Texture_Type::Type2D,
                    output->GetWidth(), output->GetHeight(), 1, 1, RHI_Format::R32G32B32A32_Float,
                    RHI_Texture_Srv | RHI_Texture_Uav, i == 0 ? "path_tracing_a" : "path_tracing_b");
            history.samples = 0;
        }
        const float requested_bounces = cvar_path_tracing_bounces.GetValue();
        const uint32_t bounces = static_cast<uint32_t>(std::isfinite(requested_bounces) ? std::clamp(requested_bounces, 1.0f, 32.0f) : 8.0f);
        const Matrix& camera = view().frame.view_projection_unjittered;
        if (history.camera != camera || history.accumulated_revision != history.scene_revision ||
            history.shader_hash != shader->GetHash() || history.bounces != bounces || sky_changed)
            history.samples = 0;
        history.camera = camera;
        history.accumulated_revision = history.scene_revision;
        history.shader_hash = shader->GetHash();
        history.bounces = bounces;

        RHI_Texture* target = history.accumulation[history.current].get();
        // Stop before the sample count loses integer precision in the FP32 running mean.
        if (history.samples < (1u << 24))
        {
            target = history.accumulation[history.current ^ 1u].get();
            RHI_CommandList::BeginPass("path_tracing");
            SetCommonTextures();
            RHI_CommandList::SetShader(shader);
            if (has_scene)
            {
                RHI_CommandList::SetAccelerationStructure(Renderer_BindingsSrv::tlas, m_tlas.get());
                GetBuffer(Renderer_Buffer::GeometryInfo)->ResetOffset();
                RHI_CommandList::SetBuffer(Renderer_BindingsUav::geometry_info, GetBuffer(Renderer_Buffer::GeometryInfo));
            }
            RHI_CommandList::SetTexture(Renderer_BindingsSrv::tex3, GetRenderTarget(Renderer_RenderTarget::skysphere));
            RHI_CommandList::SetTexture(Renderer_BindingsSrv::tex, history.accumulation[history.current].get());
            RHI_CommandList::SetTexture(Renderer_BindingsUav::tex, target);
            m_pcb_pass_cpu.set(pass_path_tracing::samples, history.samples);
            m_pcb_pass_cpu.set(pass_path_tracing::bounces, bounces);
            m_pcb_pass_cpu.set(pass_path_tracing::has_scene, has_scene);
            RHI_CommandList::Dispatch(target);
            RHI_CommandList::EndPass();
            history.current ^= 1u;
            ++history.samples;
        }

        // Exposure and display encoding are still required to display linear radiance.
        Pass_AutoExposure(target);
        Pass_Tonemap(target, output);
        RHI_CommandList::Blit(GetRenderTarget(Renderer_RenderTarget::auto_exposure),
            GetRenderTarget(Renderer_RenderTarget::auto_exposure_previous), false);
        Pass_Screenshot(target);
        m_taau_reset_history = true;
        m_pass_state.hiz_history_valid = false;
        m_pass_state.restir.history_invalid = true;
        m_pass_state.ResetCameraHistory();
    }

    void Renderer::ProduceFrame()
    {
        SP_PROFILE_CPU();

        // hot reloaded shaders swap in here, before anything this frame builds a pipeline from them
        for (const auto& shader : GetShaders())
        {
            if (shader)
            {
                shader->ApplyReload();
            }
        }

        // wait until every shader has finished compiling, null entries are safe to skip
        static bool shaders_ready = false;
        if (!shaders_ready)
        {
            for (const auto& shader : GetShaders())
            {
                if (!shader)
                {
                    continue;
                }
                const RHI_ShaderCompilationState state = shader->GetCompilationState();
                if (
                    state == RHI_ShaderCompilationState::Idle ||
                    state == RHI_ShaderCompilationState::Compiling
                )
                {
                    if (RHI_Device::IsRecording(RHI_Frame_List::ComputeA))
                    {
                        RHI_Device::Submit(RHI_Frame_List::ComputeA, nullptr, false);
                    }
                    if (RHI_Device::IsRecording(RHI_Frame_List::ComputeB))
                    {
                        RHI_Device::Submit(RHI_Frame_List::ComputeB, nullptr, false);
                    }
                    return;
                }
            }
            shaders_ready = true;
        }

        RHI_Texture* rt_output = GetRenderTarget(Renderer_RenderTarget::frame_output);
        const bool update_skysphere =
            secondary_render_root_active
                ? false
                : UpdateSkysphereConvergenceState();

        RHI_Device::Bind(RHI_Frame_List::Graphics);
        {
            vector<QueuedTextureUpload> texture_uploads;
            {
                lock_guard lock(queued_texture_uploads_mutex);
                texture_uploads.swap(queued_texture_uploads);
            }
            for (const QueuedTextureUpload& upload : texture_uploads)
            {
                ++m_pass_state.path_tracing.scene_revision;
                upload.texture->UpdateRegion(upload.x, upload.y, upload.width, upload.height, upload.data.data(), true);
            }
        }
        // the emissive pool is rewritten in place, last frame's async gi may still be sampling it
        RHI_SyncPrimitive* uploads_wait_timeline = m_cross_queue_sync.gi_upload_hazard ? m_cross_queue_sync.gi_timeline : nullptr;
        const uint64_t uploads_wait_value        = uploads_wait_timeline ? m_cross_queue_sync.gi_timeline_value : 0;
        m_cross_queue_sync.gi_upload_hazard      = false;
        RHI_Work uploads = RHI_Device::Submit(RHI_Frame_List::Graphics, nullptr, false, nullptr, uploads_wait_timeline, uploads_wait_value);

        RHI_Device::Bind(RHI_Frame_List::ComputeA);
        Pass_ComputeBatchA(update_skysphere);
        RHI_Work batch_a = RHI_Device::Submit(
            RHI_Frame_List::ComputeA,
            nullptr,
            false,
            nullptr,
            uploads.timeline.get(),
            uploads.value
        );
        RHI_SyncPrimitive* batch_a_timeline = batch_a.timeline.get();
        const uint64_t batch_a_value        = batch_a.value;
        m_cross_queue_sync.pending_compute_timeline       = batch_a_timeline;
        m_cross_queue_sync.pending_compute_timeline_value = batch_a_value;
        RHI_Device::SetFrameWait(batch_a_timeline, batch_a_value);

        if (IsPathTracingActive())
        {
            // Keep the normal queue dependency on compute A (AS builds and sky LUTs).
            // Everything after this branch is the hybrid raster pipeline.
            if (RHI_Device::IsRecording(RHI_Frame_List::ComputeB))
                RHI_Device::Submit(RHI_Frame_List::ComputeB, nullptr, false);
            RHI_Device::Bind(RHI_Frame_List::Graphics);
            if (update_skysphere)
                Pass_Skysphere();
            Pass_PathTracing(update_skysphere);
            Pass_Text(rt_output, true);
            return;
        }
        if (!secondary_render_root_active)
        {
            m_pass_state.path_tracing.accumulation[0].reset();
            m_pass_state.path_tracing.accumulation[1].reset();
            m_pass_state.path_tracing.samples = 0;
        }

        // the sky is first read by the reflection trace, on its own list the scatter work the gbuffer waits on queues ahead of it
        RHI_CommandList* sky_commands = nullptr;
        RHI_Work sky_work;
        if (update_skysphere)
        {
            sky_commands = RHI_Device::GetQueue(RHI_Queue_Type::Compute)->NextCommandList();
            sky_commands->Begin();
            RHI_Device::Bind(sky_commands);
            Pass_Skysphere();
        }
        auto submit_sky = [&]()
        {
            if (!sky_commands)
            {
                return;
            }
            sky_commands->Submit(nullptr, false);
            sky_work     = sky_commands->GetWork();
            sky_commands = nullptr;
            m_cross_queue_sync.pending_compute_timeline       = sky_work.timeline.get();
            m_cross_queue_sync.pending_compute_timeline_value = sky_work.value;
            RHI_Device::SetFrameWait(sky_work.timeline.get(), sky_work.value);
        };

        // the wind, the car's rain and the sea belong to the world, they advance once per frame with the primary
        RHI_Device::Bind(RHI_Frame_List::Graphics);
        if (!secondary_render_root_active)
        {
            Pass_WindField();
            Pass_Vegetation_Interaction();
            Pass_TerrainDeformation();
            Pass_OceanWake();
            Pass_CarRain();
            Pass_Ocean();
        }

        if (Camera* camera = Renderer::GetViewCamera())
        {
            // Fog injection reads the current ocean, sky and TLAS, but not the
            // g-buffer. Run it while graphics builds depth and shades geometry.
            // Atlas shadows and stereo still prepare fog per eye after shadows.
            const bool fog_prepared_async = IsRayTracedShadowsActive() &&
                !Xr::IsSessionRunning() && !IsSecondaryViewActive();
            RHI_Work scatter_work;
            RHI_Work surface;
            RHI_CommandList* fog_commands = nullptr;
            // the shading rate reads last frame's output and both depth prepasses consume it
            Pass_VariableRateShading();
            if (fog_prepared_async)
            {
                Pass_Cull_Early();
                surface = RHI_Device::Submit(RHI_Frame_List::Graphics, nullptr, false);
                RHI_CommandList* scatter_commands = RHI_Device::GetQueue(RHI_Queue_Type::Compute)->NextCommandList();
                scatter_commands->Begin();
                RHI_Device::Bind(scatter_commands);
                Pass_Grass_Populate();
                Pass_Grass_Interaction();
                scatter_commands->Submit(nullptr, false, nullptr, surface.timeline.get(), surface.value);
                scatter_work = scatter_commands->GetWork();
                submit_sky();
                // submitted after compute b, lighting waits on b alone and fog is only needed from reflections on
                fog_commands = RHI_Device::GetQueue(RHI_Queue_Type::Compute)->NextCommandList();
                fog_commands->Begin();
                RHI_Device::Bind(fog_commands);
                Pass_Fog(0, rhi_all_mips);
                RHI_Device::Bind(RHI_Frame_List::Graphics);
            }
            submit_sky();

            // restir's history targets are render targets only, a compute list can't clear them on d3d12
            if (!m_pass_state.restir.reservoirs_initialized && cvar_restir_pt.GetValueAs<bool>() && !secondary_render_root_active)
            {
                if (RHI_Texture* normal_previous = GetRenderTarget(Renderer_RenderTarget::gbuffer_normal_previous))
                {
                    RHI_CommandList::ClearTexture(normal_previous, Color::standard_black);
                }
                if (RHI_Texture* velocity_previous = GetRenderTarget(Renderer_RenderTarget::gbuffer_velocity_previous))
                {
                    RHI_CommandList::ClearTexture(velocity_previous, Color::standard_black);
                }
            }

            Pass_GraphicsPhase1_Geometry(fog_prepared_async);

            RHI_Work phase1 = RHI_Device::Submit(RHI_Frame_List::Graphics, nullptr, false,
                nullptr, scatter_work.timeline.get(), scatter_work.value);

            // restir traces from a copy of the opaque gbuffer on its own compute list, so it keeps running through
            // lighting, post and the start of the next frame, the copy waits for the previous gi to stop reading it
            RHI_Work gbuffer_copy;
            const bool gbuffer_history = m_pass_state.restir.gbuffer_copy_frames > 0;
            RHI_Device::Bind(RHI_Frame_List::Graphics);
            if (Pass_ReSTIR_CopyGBuffer())
            {
                gbuffer_copy = RHI_Device::Submit(RHI_Frame_List::Graphics, nullptr, false,
                    nullptr, m_cross_queue_sync.gi_timeline, m_cross_queue_sync.gi_timeline_value);
                m_cross_queue_sync.gi_timeline       = nullptr;
                m_cross_queue_sync.gi_timeline_value = 0;
            }
            const bool restir_async =
                gbuffer_copy.timeline &&
                gbuffer_history &&
                cvar_restir_pt_async.GetValueAs<bool>() &&
                m_pass_state.restir.reservoirs_initialized;
            m_pass_state.restir.async_this_frame = restir_async;

            RHI_Device::Bind(RHI_Frame_List::ComputeB);
            Pass_ComputeBatchB(!restir_async);
            RHI_Work batch_b = RHI_Device::Submit(
                RHI_Frame_List::ComputeB,
                nullptr,
                false,
                nullptr,
                phase1.timeline.get(),
                phase1.value
            );
            RHI_SyncPrimitive* compute_b_timeline = batch_b.timeline.get();
            const uint64_t compute_b_value        = batch_b.value;

            // queue order makes the fog signal cover compute b as well
            if (fog_commands)
            {
                fog_commands->Submit(nullptr, false, nullptr, surface.timeline.get(), surface.value);
                const RHI_Work fog_work = fog_commands->GetWork();
                m_cross_queue_sync.fog_timeline       = fog_work.timeline.get();
                m_cross_queue_sync.fog_timeline_value = fog_work.value;
            }

            if (restir_async)
            {
                RHI_CommandList* gi_commands = RHI_Device::GetQueue(RHI_Queue_Type::Compute)->NextCommandList();
                gi_commands->Begin();
                RHI_Device::Bind(gi_commands);
                Pass_ReSTIR_Async();
                gi_commands->Submit(nullptr, false, nullptr, gbuffer_copy.timeline.get(), gbuffer_copy.value);
                const RHI_Work gi_work = gi_commands->GetWork();
                m_cross_queue_sync.gi_timeline       = gi_work.timeline.get();
                m_cross_queue_sync.gi_timeline_value = gi_work.value;
            }

            // Keep the animated near sun cascade alongside ray-traced shadows.
            const bool shadow_maps_required = !light_entities().empty();
            RHI_Device::Bind(RHI_Frame_List::Graphics);
            if (shadow_maps_required)
            {
                Pass_GraphicsPhase2_ShadowsAndRT();
            }

            // clouds and the opaque reflection trace read only depth, the gbuffer, the tlas and the sky, none of
            // which compute b writes, so they fill the graphics queue while it waits for shadows and occlusion
            const bool early_work_recorded = !IsStereoView();
            bool clouds_prepared_early     = false;
            if (early_work_recorded)
            {
                m_pcb_pass_cpu.eye_index = 0;
                clouds_prepared_early    = Pass_Clouds_Prepare(rhi_all_mips);
            }

            if (shadow_maps_required || early_work_recorded)
            {
                RHI_Device::Submit(
                    RHI_Frame_List::Graphics,
                    nullptr,
                    false,
                    nullptr,
                    batch_a_timeline,
                    batch_a_value
                );
                RHI_Device::Bind(RHI_Frame_List::Graphics);
            }

            // the trace misses into the sky, so it is the first graphics work that has to wait for it
            if (early_work_recorded && !m_transparents_present)
            {
                Pass_Reflections_Trace(rhi_all_mips);
            }
            if (sky_work.timeline)
            {
                RHI_Device::Submit(RHI_Frame_List::Graphics, nullptr, false, nullptr, sky_work.timeline.get(), sky_work.value);
                RHI_Device::Bind(RHI_Frame_List::Graphics);
            }

            m_cross_queue_sync.pending_compute_timeline       = compute_b_timeline;
            m_cross_queue_sync.pending_compute_timeline_value = compute_b_value;
            RHI_Device::SetFrameWait(compute_b_timeline, compute_b_value);

            RHI_Texture* tex_exposure_previous =
                GetRenderTarget(Renderer_RenderTarget::auto_exposure_previous);
            const bool auto_exposure_enabled =
                camera->GetExposureMode() == CameraExposureMode::automatic;
            Renderer::view().exposure.history_reset =
                auto_exposure_enabled &&
                (
                    camera != Renderer::view().exposure.camera ||
                    tex_exposure_previous !=
                    Renderer::view().exposure.history_texture ||
                    !Renderer::view().exposure.was_automatic
                );
            if (Renderer::view().exposure.history_reset)
            {
                RHI_CommandList::ClearTexture(
                    tex_exposure_previous,
                    Color::standard_black
                );
            }

            const bool xr_stereo     = IsStereoView();
            const uint32_t eye_count = xr_stereo ? Xr::eye_count : 1;
            for (uint32_t eye = 0; eye < eye_count; eye++)
            {
                const uint32_t eye_layer = xr_stereo ? eye : rhi_all_mips;
                m_pcb_pass_cpu.eye_index = xr_stereo ? eye : 0;
                if (!fog_prepared_async)
                    Pass_Fog(eye, eye_layer);
                ProduceFrame_PerEye(eye, eye_layer, early_work_recorded, clouds_prepared_early);
            }
            WaitForAsyncFog();

            if (xr_stereo)
            {
                Pass_ScreenshotXr();
                Pass_XrMirror();
            }

            if (auto_exposure_enabled)
            {
                RHI_CommandList::Blit(
                    GetRenderTarget(Renderer_RenderTarget::auto_exposure),
                    tex_exposure_previous,
                    false
                );
            }

            m_pcb_pass_cpu.eye_index = 0;
        }
        else
        {
            submit_sky();
            if (RHI_Device::IsRecording(RHI_Frame_List::ComputeB))
            {
                RHI_Device::Submit(RHI_Frame_List::ComputeB, nullptr, false);
            }
            RHI_Device::Bind(RHI_Frame_List::Graphics);
            RHI_CommandList::ClearTexture(rt_output, Color::standard_black);
        }

        RHI_Device::Bind(RHI_Frame_List::Graphics);
        if (!secondary_render_root_active)
        {
            Pass_Text(rt_output, true);
        }

        if (!secondary_render_root_active)
        {
            Pass_ReSTIR_SwapGBufferHistory();
        }
    }
}

namespace spartan::Renderer
{
    ViewState& view() { return *active_view; }
    Camera* GetViewCamera() { return active_view == &preview_view ? preview_view.camera : World::GetCamera(); }
    bool IsViewWireframe()
    {
        return active_view == &preview_view ? secondary_view_mode_active == Renderer_SecondaryViewMode::Wireframe : cvar_wireframe.GetValueAs<bool>();
    }
    const vector<Entity*>& GetViewLightEntities()
    {
        return light_entities();
    }

    bool IsStereoView()
    {
        return Xr::IsSessionRunning() && Xr::GetStereoMode() && !secondary_render_root_active;
    }
}
