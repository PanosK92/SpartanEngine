/*
Copyright(c) 2015-2026 Panos Karabelas

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and / or sell
copies of the Software, and to permit persons to whom the Software is furnished
to do so, subject to the following conditions :

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/

//= INCLUDES ===================================
#include "pch.h"
#include <mutex>
#include <unordered_set>
#include <future>
#include <cmath>
#include "Renderer_Internal.h"
#include "Material.h"
#include "GeometryBuffer.h"
#include "ThreadPool.h"
#include "../profiling/RenderDoc.h"
#include "../profiling/Profiler.h"
#include "../core/Debugging.h"
#include "../core/Window.h"
#include "../core/Timer.h"
#include "../file_system/FileSystem.h"
#include "../input/Input.h"
#include "../display/Display.h"
#include "../rhi/RHI_Device.h"
#include "../rhi/RHI_SwapChain.h"
#include "../rhi/RHI_Queue.h"
#include "../rhi/RHI_Implementation.h"
#include "../rhi/RHI_Buffer.h"
#include "../rhi/RHI_Shader.h"
#include "../rhi/RHI_CommandList.h"
#include "../rhi/RHI_VendorTechnology.h"
#include "../rhi/RHI_AccelerationStructure.h"
#include "../world/Entity.h"
#include "../world/components/Light.h"
#include "../world/components/Camera.h"
#include "../world/components/Volume.h"
#include "../world/components/Render.h"
#include "../world/components/Water.h"
#include "../world/components/Terrain.h"
#include "../core/ProgressTracker.h"
#include "../math/Rectangle.h"
#include "../resource/import/ImageImporter.h"
#include "../commands/console/ConsoleCommands.h"
#include "../profiling/Breadcrumbs.h"
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
        bool s_common_bind = false;
        bool s_common_ssao = true;
        uint32_t s_common_eye = rhi_all_mips;

        void reset_common_bind()
        {
            s_common_bind = false;
            s_common_ssao = true;
            s_common_eye = rhi_all_mips;
        }

        // set by TickUploadMaterials, consumed by UpdateAccelerationStructures
        bool materials_uploaded_this_frame = false;
    }

    // constant and push constant buffers

    // bindless draw data

    // per-frame rotated buffers

    // line and icon rendering

    // misc


    namespace
    {
        const uint8_t  swap_chain_buffer_count    = 2;
        const uint32_t resolution_shadow_min      = 128;
        float          near_plane                 = 0.0f;
        float          far_plane                  = 1.0f;
        bool           dirty_orthographic_projection = true;

        struct screenshot_request
        {
            string file_path;
            string png_path;
            string exr_path;
            bool save_exr = false;
            bool pending  = false;
            bool ready    = false;
            bool secondary_view = false;
            uint64_t secondary_generation = 0;
        };

        mutex screenshot_mutex;
        screenshot_request screenshot;
        uint32_t screenshot_index = 0;
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
        shared_ptr<RHI_Texture> secondary_view_primary_backup;
        shared_ptr<RHI_Texture> secondary_view_exposure;
        shared_ptr<RHI_Texture> secondary_view_exposure_primary;
        shared_ptr<RHI_Texture> secondary_view_exposure_primary_previous;
        bool secondary_view_exposure_valid = false;
        bool secondary_view_ready = false;
        uint32_t secondary_view_recovery_frames = 0;

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

        // a secondary view borrows the frame from the primary camera, the entity history
        // belongs to that other camera so reusing it writes bogus velocity, which shows up
        // as motion blur and taa smear on the preview
        const math::Matrix& matrix_previous_for_velocity(Entity* entity)
        {
            return secondary_render_root_active
                ? entity->GetMatrix()
                : entity->GetMatrixPrevious();
        }

        const vector<Entity*>& render_entities()
        {
            return secondary_render_root_active
                ? World::GetEntities()
                : World::GetEntitiesWithRender();
        }

        const vector<Entity*>& light_entities()
        {
            return secondary_render_root_active
                ? World::GetEntities()
                : World::GetEntitiesLights();
        }

        bool ensure_secondary_view_targets(
            const uint32_t width,
            const uint32_t height
        )
        {
            RHI_Texture* source =
                Renderer::GetRenderTarget(
                    Renderer_RenderTarget::frame_output
                );
            RHI_Texture* exposure =
                Renderer::GetRenderTarget(
                    Renderer_RenderTarget::auto_exposure
                );
            RHI_Texture* exposure_previous =
                Renderer::GetRenderTarget(
                    Renderer_RenderTarget::auto_exposure_previous
                );
            if (!source || !exposure || !exposure_previous)
            {
                return false;
            }
            const bool recreate =
                !secondary_view_output ||
                secondary_view_output->GetWidth() != width ||
                secondary_view_output->GetHeight() != height ||
                secondary_view_output->GetFormat() != source->GetFormat();
            const bool recreate_backup =
                !secondary_view_primary_backup ||
                secondary_view_primary_backup->GetWidth() !=
                    source->GetWidth() ||
                secondary_view_primary_backup->GetHeight() !=
                    source->GetHeight() ||
                secondary_view_primary_backup->GetFormat() !=
                    source->GetFormat();
            const bool recreate_exposure =
                !secondary_view_exposure ||
                secondary_view_exposure->GetFormat() !=
                    exposure_previous->GetFormat() ||
                !secondary_view_exposure_primary ||
                secondary_view_exposure_primary->GetFormat() !=
                    exposure->GetFormat() ||
                !secondary_view_exposure_primary_previous ||
                secondary_view_exposure_primary_previous->GetFormat() !=
                    exposure_previous->GetFormat();
            if (
                !recreate &&
                !recreate_backup &&
                !recreate_exposure
            )
            {
                return true;
            }

            const uint32_t output_flags =
                RHI_Texture_Srv |
                RHI_Texture_Uav |
                RHI_Texture_ClearBlit;
            if (recreate)
            {
                secondary_view_output =
                    make_shared<RHI_Texture>(
                        RHI_Texture_Type::Type2D,
                        width,
                        height,
                        1,
                        1,
                        source->GetFormat(),
                        output_flags,
                        "secondary_view_output"
                    );
            }
            if (recreate_backup)
            {
                const uint32_t backup_flags =
                    RHI_Texture_Srv |
                    RHI_Texture_Rtv |
                    RHI_Texture_ClearBlit;
                secondary_view_primary_backup =
                    make_shared<RHI_Texture>(
                    RHI_Texture_Type::Type2D,
                    source->GetWidth(),
                    source->GetHeight(),
                    1,
                    1,
                    source->GetFormat(),
                    backup_flags,
                    "secondary_view_primary_backup"
                );
            }
            if (recreate_exposure)
            {
                const uint32_t exposure_flags =
                    RHI_Texture_Srv |
                    RHI_Texture_Uav |
                    RHI_Texture_ClearBlit;
                secondary_view_exposure =
                    make_shared<RHI_Texture>(
                        RHI_Texture_Type::Type2D,
                        1,
                        1,
                        1,
                        1,
                        exposure_previous->GetFormat(),
                        exposure_flags,
                        "secondary_view_exposure"
                    );
                secondary_view_exposure_primary =
                    make_shared<RHI_Texture>(
                        RHI_Texture_Type::Type2D,
                        1,
                        1,
                        1,
                        1,
                        exposure->GetFormat(),
                        exposure_flags,
                        "secondary_view_exposure_primary"
                    );
                secondary_view_exposure_primary_previous =
                    make_shared<RHI_Texture>(
                        RHI_Texture_Type::Type2D,
                        1,
                        1,
                        1,
                        1,
                        exposure_previous->GetFormat(),
                        exposure_flags,
                        "secondary_view_exposure_primary_previous"
                    );
                secondary_view_exposure_valid = false;
            }
            secondary_view_ready = false;
            return
                secondary_view_output->GetRhiResource() &&
                secondary_view_primary_backup->GetRhiResource() &&
                secondary_view_exposure->GetRhiResource() &&
                secondary_view_exposure_primary->GetRhiResource() &&
                secondary_view_exposure_primary_previous->GetRhiResource();
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
            if (!sdr_staging || !sdr_staging->GetMappedData())
            {
                SP_LOG_ERROR("Failed to map SDR screenshot staging buffer");
                return;
            }

            ThreadPool::AddTask([request, sdr_staging, width, height, channel_count, bits_per_channel]()
            {
                void* sdr_data = sdr_staging->GetMappedData();

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
                    ImageImporter::Save(request.exr_path, width, height, channel_count, bits_per_channel, exr_data);
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
            request.save_exr = true;
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

        void tick_dynamic_resolution_scale()
        {
            if (cvar_dynamic_resolution.GetValue() == 0.0f)
            {
                return;
            }

            const float gpu_time_target   = 16.67f;
            const float adjustment_factor = static_cast<float>(0.05f * Timer::GetDeltaTimeSec());
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
        if (Debugging::IsRenderdocEnabled())
        {
            RenderDoc::OnPreDeviceCreation();
        }
        RHI_Device::Initialize();
        RHI_Device::SetPipelineBoundCallback([](RHI_CommandList* cmd_list)
        {
            RHI_Device::Bind(cmd_list);
            SetStandardResources(cmd_list);
        });
        RHI_Device::SetDefaultPushConstantsCallback([](RHI_CommandList* cmd_list)
        {
            RHI_CommandList::PushConstants(cmd_list, m_pcb_pass_cpu);
        });
        RHI_Device::SetScaleDimensionCallback([](uint32_t dimension, float scale)
        {
            return GetScaledDimension(dimension, scale);
        });
        RHI_Device::SetPassResetCallback(reset_common_bind);

        if (Debugging::IsBreadcrumbsEnabled())
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
        SP_FIRE_EVENT(EventType::RendererOnInitialized);
    }

    void Renderer::Shutdown()
    {
        SP_FIRE_EVENT(EventType::RendererOnShutdown);

        RHI_Device::QueueWaitAll();

        RHI_CommandList::ImmediateExecutionShutdown();

        {
            DestroyResources();
            GeometryBuffer::Shutdown();
            RHI_Device::DestroySwapChain();
            m_lines_vertex_buffer = nullptr;
            m_icons_vertex_buffer = nullptr;
            m_tlas                = nullptr;
            secondary_view_output.reset();
            secondary_view_primary_backup.reset();
            secondary_view_exposure.reset();
            secondary_view_exposure_primary.reset();
            secondary_view_exposure_primary_previous.reset();
            secondary_view_exposure_valid = false;
            secondary_camera_request = nullptr;
            secondary_render_root_request = nullptr;
            secondary_render_root_active = nullptr;
            secondary_view_ready = false;
            secondary_view_request_generation = 0;
            secondary_view_active_generation = 0;
            secondary_view_ready_generation = 0;
        }

        RHI_VendorTechnology::Shutdown();
        RenderDoc::Shutdown();

        // breadcrumbs
        if (Debugging::IsBreadcrumbsEnabled())
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

        RHI_VendorTechnology::Tick(&m_cb_frame_cpu, GetResolutionRender(), GetResolutionOutput(), GetResolutionScale());
        tick_dynamic_resolution_scale();
        if (Debugging::IsBreadcrumbsEnabled())
        {
            Breadcrumbs::StartFrame();
        }

        TickRecreateOptionalRenderTargetsIfNeeded();

        const uint32_t min_render_dimension = 64;
        const bool resolution_valid         = m_resolution_render.x >= min_render_dimension && m_resolution_render.y >= min_render_dimension;
        const bool can_render               = !Window::IsMinimized() && m_initialized_resources && resolution_valid;

        if (can_render)
        {
            RotateFrameBuffers();
            RHI_Device::AcquireSwapChainImage();
        }
        else if (m_frame_num > 0)
        {
            RHI_Device::GetQueue(RHI_Queue_Type::Graphics)->Wait();
        }
        RHI_Device::Tick(m_frame_num);
        RHI_Device::BeginFrame(can_render);
        RHI_Device::Bind(RHI_Frame_List::Graphics);

        m_draw_data_count      = 0;
        m_draw_data_gpu_synced = false;
        Entity* primary_camera_entity = nullptr;
        Entity* secondary_camera_entity = nullptr;
        Entity* secondary_render_root_entity = nullptr;
        bool render_secondary_view = false;
        Cb_Frame cb_frame_primary = {};
        float secondary_wireframe_previous =
            cvar_wireframe.GetValue();

        if (can_render)
        {
            TickUpdateHiZSuppressionState();
            if (secondary_view_recovery_frames > 0)
            {
                m_is_hiz_suppressed = true;
                secondary_view_recovery_frames--;
            }

            // batch world geometry into one gpu upload after loading
            if (!ProgressTracker::IsLoading())
            {
                GeometryBuffer::BuildIfDirty();
            }

            // geometry buffer rebuild invalidates blas device addresses, free old gpu memory before rebuilding to avoid a peak
            if (GeometryBuffer::WasRebuilt())
            {
                DestroyAccelerationStructures();
            }

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
                ensure_secondary_view_targets(
                    secondary_width,
                    secondary_height
                )
            )
            {
                if (Camera* primary_camera = World::GetCamera())
                {
                    primary_camera_entity =
                        primary_camera->GetEntity();
                }
                RHI_CommandList::Copy(
                    GetRenderTarget(
                        Renderer_RenderTarget::frame_output
                    ),
                    secondary_view_primary_backup.get(),
                    false
                );
                RHI_Texture* exposure =
                    GetRenderTarget(
                        Renderer_RenderTarget::auto_exposure
                    );
                RHI_Texture* exposure_previous =
                    GetRenderTarget(
                        Renderer_RenderTarget::auto_exposure_previous
                    );
                RHI_CommandList::Copy(
                    exposure,
                    secondary_view_exposure_primary.get(),
                    false
                );
                RHI_CommandList::Copy(
                    exposure_previous,
                    secondary_view_exposure_primary_previous.get(),
                    false
                );
                if (!secondary_view_exposure_valid)
                {
                    RHI_CommandList::Copy(
                        exposure_previous,
                        secondary_view_exposure.get(),
                        false
                    );
                    secondary_view_exposure_valid = true;
                }
                RHI_CommandList::Copy(
                    secondary_view_exposure.get(),
                    exposure_previous,
                    false
                );

                // this frame belongs to the preview camera, the primary never renders it,
                // so its history is put back once the preview is done, otherwise the next
                // primary frame measures velocity against the preview camera
                cb_frame_primary = m_cb_frame_cpu;
                World::SetActiveCamera(
                    secondary_camera_entity
                );
                Camera* secondary_camera =
                    secondary_camera_entity
                        ->GetComponent<Camera>();
                secondary_camera->SetAspectRatioOverride(
                    static_cast<float>(secondary_width) /
                    static_cast<float>(secondary_height)
                );
                secondary_camera->Tick();
                secondary_render_root_entity->SetActive(true);
                secondary_render_root_active =
                    secondary_render_root_entity;
                for (
                    Entity* entity :
                    render_entities()
                )
                {
                    if (
                        !entity ||
                        (
                            entity != secondary_render_root_entity &&
                            !entity->IsDescendantOf(
                                secondary_render_root_entity
                            )
                        )
                    )
                    {
                        continue;
                    }
                    if (
                        Render* render =
                            entity->GetComponent<Render>()
                    )
                    {
                        render->UpdateAabb();
                        render->SetVisible(true);
                        render->UpdateLodIndices();
                    }
                }
                m_is_hiz_suppressed = true;
                cvar_wireframe.SetValue(
                    secondary_view_mode_active ==
                    Renderer_SecondaryViewMode::Wireframe
                        ? 1.0f
                        : 0.0f
                );
                render_secondary_view = true;
            }
            else if (secondary_request_consumed)
            {
                secondary_view_ready = false;
                lock_guard<mutex> lock(screenshot_mutex);
                if (
                    screenshot.pending &&
                    screenshot.secondary_view &&
                    screenshot.secondary_generation <=
                        secondary_view_active_generation
                )
                {
                    // the generation it waited for came and went without rendering, dropping it here
                    // matters because a pending request blocks every later screenshot
                    screenshot = {};
                }
            }

            TickUploadMaterials();
            UpdateDrawCalls();

            TickAdvanceFrameConstantBufferRing();
            TickUploadBindlessDependencies();

            if (!render_secondary_view)
            {
                UpdatePersistentLines();
                AddLinesToBeRendered();
            }
        }

        // xrBeginFrame must precede UpdateFrameConstantBuffer so per eye matrices reflect this frame's predicted pose, paired with xrEndFrame below
        bool xr_should_render = false;
        if (Xr::IsSessionRunning())
        {
            xr_should_render = Xr::BeginFrame();
        }

        if (can_render)
        {
            UpdateFrameConstantBuffer();
            ProduceFrame();
            if (render_secondary_view)
            {
                // runs on the display ready frame, after post process, so the backdrop stays flat
                // and the wires stay crisp instead of being blurred by depth of field
                Pass_PreviewStudio(
                    GetRenderTarget(
                        Renderer_RenderTarget::frame_output
                    )
                );
                Pass_Blit(
                    GetRenderTarget(
                        Renderer_RenderTarget::frame_output
                    ),
                    secondary_view_output.get(),
                    false
                );
                RHI_CommandList::Copy(
                    secondary_view_primary_backup.get(),
                    GetRenderTarget(
                        Renderer_RenderTarget::frame_output
                    ),
                    false
                );
                RHI_Texture* exposure =
                    GetRenderTarget(
                        Renderer_RenderTarget::auto_exposure
                    );
                RHI_Texture* exposure_previous =
                    GetRenderTarget(
                        Renderer_RenderTarget::auto_exposure_previous
                    );
                RHI_CommandList::Copy(
                    exposure_previous,
                    secondary_view_exposure.get(),
                    false
                );
                RHI_CommandList::Copy(
                    secondary_view_exposure_primary.get(),
                    exposure,
                    false
                );
                RHI_CommandList::Copy(
                    secondary_view_exposure_primary_previous.get(),
                    exposure_previous,
                    false
                );
                World::SetActiveCamera(
                    primary_camera_entity
                );
                m_cb_frame_cpu = cb_frame_primary;
                secondary_render_root_entity->SetActive(false);
                secondary_render_root_active = nullptr;
                secondary_view_ready = true;
                secondary_view_ready_generation =
                    secondary_view_active_generation;
                {
                    lock_guard<mutex> lock(
                        screenshot_mutex
                    );
                    // any render at or after the requested generation satisfies the capture, an exact
                    // match would strand the request whenever the preview panel refreshed in between
                    if (
                        screenshot.pending &&
                        screenshot.secondary_view &&
                        screenshot.secondary_generation <=
                            secondary_view_active_generation
                    )
                    {
                        screenshot.pending = false;
                        screenshot.ready = true;
                    }
                }
                secondary_view_recovery_frames = 0;
                cvar_wireframe.SetValue(
                    secondary_wireframe_previous
                );
            }
        }

        if (xr_should_render && can_render)
        {
            RHI_Texture* stereo_output = GetRenderTarget(Renderer_RenderTarget::frame_output_stereo);
            BlitToXrSwapchain(stereo_output ? stereo_output : GetRenderTarget(Renderer_RenderTarget::frame_output));
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
                    submitted.timeline;
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
                    submitted.timeline;
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

    void Renderer::TickRecreateOptionalRenderTargetsIfNeeded()
    {
        if (!m_initialized_resources)
        {
            return;
        }

        static uint32_t options_hash  = 0;
        static float restir_scale_old = -1.0f;

        const uint32_t options_hash_new = (cvar_ssao.GetValueAs<bool>() << 0) | (cvar_ray_traced_reflections.GetValueAs<bool>() << 1) | (cvar_restir_pt.GetValueAs<bool>() << 2);
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
        // the bindless slot a material owns is handed out here and a draw carries that slot as an index,
        // so this has to run before the draw data is written, doing it afterwards leaves the frame
        // sampling whatever material now sits where the index used to point, which reads as the wrong
        // texture and lands on the checkerboard whenever it resolves to the standard material

        // a secondary view assigns the slots over a different entity set than the primary one, so a
        // layout is only valid for the kind of view that built it, coming back to a view whose materials
        // happen not to have changed would otherwise reuse indices into a foreign layout
        static bool uploaded_for_secondary = false;
        const bool is_secondary = secondary_render_root_active != nullptr;
        const bool view_changed = is_secondary != uploaded_for_secondary;

        // consume the world side flag unconditionally, it clears its own change tracking on read
        const bool world_changed =
            World::HaveMaterialsChangedThisFrame();
        // terrain rules and the grass material are packed into the material buffer without touching
        // a Material, so the world side revision cannot see them
        const bool bindless_changed = m_pass_state.bindless_materials_dirty;
        if (
            GetFrameNumber() != 0 &&
            !view_changed &&
            !world_changed &&
            !bindless_changed
        )
        {
            return;
        }
        m_pass_state.bindless_materials_dirty = false;
        uploaded_for_secondary = is_secondary;
        materials_uploaded_this_frame = true;

        UpdateMaterials();
        RHI_CommandList::PrepareTexturesForSampling(&m_bindless_textures);
        RHI_Device::UpdateBindlessMaterials(
            &m_bindless_textures,
            GetBuffer(Renderer_Buffer::MaterialParameters)
        );

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
        // run during loading so newly published entities pick up materials and lights as they arrive
        const bool initialize     = GetFrameNumber() == 0;
        const bool lights_changed = initialize || World::HaveLightsChanged();

        if (lights_changed)
        {
            UpdateShadowAtlas();
        }

        // frustum and draw distance membership depend on the camera, rebuild every frame
        UpdateLights();
        if (lights_changed)
        {
            RHI_Device::UpdateBindlessLights(GetBuffer(Renderer_Buffer::LightParameters));
        }

        if (m_bindless_samplers_dirty)
        {
            RHI_Device::UpdateBindlessSamplers(Renderer::GetSamplers().data(), static_cast<uint32_t>(Renderer::GetSamplers().size()));
            m_bindless_samplers_dirty = false;
        }

        // aabbs change every frame with entity transforms
        UpdateBoundingBoxes();
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
            if (!buffer->GetMappedData())
            {
                RHI_CommandList::UpdateBuffer(buffer, frame_byte_offset, upload_size, &m_draw_data_cpu[0]);
            }
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
        static RHI_Buffer* last_vertex_buffer = nullptr;
        if (RHI_Buffer* current_vertex = GeometryBuffer::GetVertexBuffer(); current_vertex && current_vertex != last_vertex_buffer)
        {
            RHI_Device::UpdateBindlessGeometryVertices(current_vertex);
            last_vertex_buffer = current_vertex;
        }
        static RHI_Buffer* last_index_buffer = nullptr;
        if (RHI_Buffer* current_index = GeometryBuffer::GetIndexBuffer(); current_index && current_index != last_index_buffer)
        {
            RHI_Device::UpdateBindlessGeometryIndices(current_index);
            last_index_buffer = current_index;
        }

        // global instance buffer for instanced indirect draws, falls back to dummy until the global buffer exists
        static RHI_Buffer* last_instance_buffer = nullptr;
        RHI_Buffer* current_instance            = GeometryBuffer::GetInstanceBuffer();
        if (!current_instance)
        {
            current_instance = GetBuffer(Renderer_Buffer::DummyInstance);
        }
        if (current_instance != last_instance_buffer)
        {
            RHI_Device::UpdateBindlessInstances(current_instance);
            last_instance_buffer = current_instance;
        }

        // two slot indirect draw args, slot 0 opaque slot 1 alpha-tested, layout matches VkDrawIndirectCommand on the first 16 bytes
        // index_count aliases vertex_count and is bumped 3 at a time by the triangle cull, instance_count fixed at 1
        Sb_IndirectDrawArgs draw_args[2] = {};
        draw_args[0].instance_count      = 1;
        draw_args[1].instance_count      = 1;
        RHI_Buffer* args_buffer          = GetBuffer(Renderer_Buffer::IndirectDrawArgs);
        args_buffer->ResetOffset();
        args_buffer->Update(&draw_args[0], sizeof(draw_args));

        // two-slot indirect dispatch args, slot 0 opaque (or full list for vs path), slot 1 alpha for mesh path
        // group_count_x is bumped by the meshlet cull, group_count_y/z fixed at 1
        Sb_IndirectDispatchArgs dispatch_args[2] = {};
        dispatch_args[0].group_count_y           = 1;
        dispatch_args[0].group_count_z           = 1;
        dispatch_args[1].group_count_y           = 1;
        dispatch_args[1].group_count_z           = 1;
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
            RHI_Buffer* tasks_buffer = GetBuffer(Renderer_Buffer::CullTasks);
            tasks_buffer->ResetOffset();
            tasks_buffer->Update(&m_cull_tasks[0], tasks_buffer->GetStride() * m_cull_task_count);
        }
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
            dirty_orthographic_projection = true;
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

    bool Renderer::IsSecondaryScreenshotPending()
    {
        lock_guard<mutex> lock(screenshot_mutex);
        return
            screenshot.secondary_view &&
            (screenshot.pending || screenshot.ready);
    }

    void Renderer::InvalidateSecondaryView()
    {
        secondary_camera_request = nullptr;
        secondary_render_root_request = nullptr;
        secondary_view_ready = false;
        secondary_view_exposure_valid = false;
        secondary_view_request_generation++;

        lock_guard<mutex> lock(screenshot_mutex);
        if (screenshot.secondary_view)
        {
            screenshot = {};
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

        Renderer::BeginPass("preview_studio", rhi_all_mips);
        {
            RHI_CommandList::SetShader(shader_c);
            RHI_CommandList::SetTexture(static_cast<uint32_t>(Renderer_BindingsUav::tex), tex_out, rhi_all_mips, 0, true);
            m_pcb_pass_cpu.set_f3_value(tint);
            m_pcb_pass_cpu.set_f3_value2(
                replace_sky ? 1.0f : 0.0f,
                wire_mode,
                0.0f
            );
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
            if (m_cb_frame_cpu.frame > 1)
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
            m_pass_state.sky_first_frame         = true;
            m_pass_state.cloud_history.Reset();
            m_pass_state.cloud_environment_dirty = true;
            m_pass_state.ssao_history.Reset();
            m_pass_state.fog_history.Reset();
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
        return sanitize_resolution_scale(cvar_resolution_scale.GetValue());
    }

    uint32_t Renderer::GetScaledDimension(uint32_t dimension, float scale /*= -1.0f*/)
    {
        scale = scale < 0.0f ? GetResolutionScale() : sanitize_resolution_scale(scale);
        return max(static_cast<uint32_t>(static_cast<float>(dimension) * scale), 1u);
    }

    void Renderer::RecreateRenderTargets()
    {
        if (m_cb_frame_cpu.frame > 1)
        {
            bool flush = true;
            RHI_Device::QueueWaitAll(flush);
        }

        CreateRenderTargets(true, true, true);
        m_pass_state.cloud_history.Reset();
        m_pass_state.cloud_environment_dirty = true;
        m_pass_state.ssao_history.Reset();
        m_pass_state.fog_history.Reset();
        CreateSamplers();
    }

    void Renderer::ResetTaauHistory()
    {
        m_taau_reset_history = true;
    }

    void Renderer::UpdateFrameCb_CameraAndProjectionHistory()
    {
        if (Camera* camera = World::GetCamera())
        {
            if (near_plane != camera->GetNearPlane() || far_plane != camera->GetFarPlane())
            {
                near_plane                    = camera->GetNearPlane();
                far_plane                     = camera->GetFarPlane();
                dirty_orthographic_projection = true;
            }

            m_cb_frame_cpu.view_previous       = m_cb_frame_cpu.view;
            m_cb_frame_cpu.view                = camera->GetViewMatrix();
            m_cb_frame_cpu.view_inverted       = Matrix::Invert(m_cb_frame_cpu.view);
            m_cb_frame_cpu.projection_previous = m_cb_frame_cpu.projection;
            m_cb_frame_cpu.projection          = camera->GetProjectionMatrix();
            m_cb_frame_cpu.projection_inverted = Matrix::Invert(m_cb_frame_cpu.projection);
        }

        if (dirty_orthographic_projection)
        {
            // ortho near is 0 to avoid NaN in the [3,2] element
            Matrix projection_ortho                     = Matrix::CreateOrthographicLH(m_viewport.width, m_viewport.height, 0.0f, far_plane);
            m_cb_frame_cpu.view_projection_orthographic = Matrix::CreateLookAtLH(Vector3(0, 0, -near_plane), Vector3::Forward, Vector3::Up) * projection_ortho;
            dirty_orthographic_projection               = false;
        }
    }

    void Renderer::UpdateFrameCb_ProjectionJitter()
    {
        const Renderer_AntiAliasing_Upsampling upsampling_mode = cvar_antialiasing_upsampling.GetValueAs<Renderer_AntiAliasing_Upsampling>();

        // stereo overwrites the projection with unjittered per eye matrices, keep the advertised jitter at zero
        // so shaders do not unjitter velocities and uvs with an offset that was never applied
        if (Xr::IsSessionRunning() && Xr::GetStereoMode())
        {
            m_jitter_offset = Vector2::Zero;
            return;
        }

        // a secondary view resolves without a temporal upscaler, a sub pixel offset that
        // nothing resolves away would just shift the preview off centre
        if (secondary_render_root_active)
        {
            m_jitter_offset = Vector2::Zero;
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
            m_jitter_offset.x = 2.0f * jx / render_w;
            m_jitter_offset.y = -2.0f * jy / render_h;

            m_cb_frame_cpu.projection *= Matrix::CreateTranslation(Vector3(m_jitter_offset.x, m_jitter_offset.y, 0.0f));
        }
        else if (upsampling_mode == Renderer_AntiAliasing_Upsampling::AA_Xess_Upscale_Xess)
        {
            RHI_VendorTechnology::XeSS_GenerateJitterSample(&m_jitter_offset.x, &m_jitter_offset.y);
            m_cb_frame_cpu.projection *= Matrix::CreateTranslation(Vector3(m_jitter_offset.x, m_jitter_offset.y, 0.0f));
        }
        else if (upsampling_mode == Renderer_AntiAliasing_Upsampling::AA_Dlss_Upscale_Dlss)
        {
            RHI_VendorTechnology::DLSS_GenerateJitterSample(&m_jitter_offset.x, &m_jitter_offset.y);
            m_cb_frame_cpu.projection *= Matrix::CreateTranslation(Vector3(m_jitter_offset.x, m_jitter_offset.y, 0.0f));
        }
        else
        {
            m_jitter_offset = Vector2::Zero;
        }
    }

    void Renderer::UpdateFrameCb_ViewProjectionAndCameraFields()
    {
        m_cb_frame_cpu.view_projection_previous = m_cb_frame_cpu.view_projection;
        m_cb_frame_cpu.view_projection          = m_cb_frame_cpu.view * m_cb_frame_cpu.projection;
        m_cb_frame_cpu.view_projection_inverted = Matrix::Invert(m_cb_frame_cpu.view_projection);

        if (Camera* camera = World::GetCamera())
        {
            m_cb_frame_cpu.view_projection_previous_unjittered = m_cb_frame_cpu.view_projection_unjittered;
            m_cb_frame_cpu.view_projection_unjittered          = m_cb_frame_cpu.view * camera->GetProjectionMatrix();
            m_cb_frame_cpu.camera_near                         = camera->GetNearPlane();
            m_cb_frame_cpu.camera_far                          = camera->GetFarPlane();
            m_cb_frame_cpu.camera_position_previous            = m_cb_frame_cpu.camera_position;
            m_cb_frame_cpu.camera_position                     = camera->GetEntity()->GetPosition();
            m_cb_frame_cpu.camera_forward                      = camera->GetEntity()->GetForward();
            m_cb_frame_cpu.camera_right                        = camera->GetEntity()->GetRight();
            m_cb_frame_cpu.camera_fov                          = camera->GetFovHorizontalRad();
            m_cb_frame_cpu.camera_aperture                     = camera->GetAperture();
            m_cb_frame_cpu.camera_last_movement_time           = (m_cb_frame_cpu.camera_position - m_cb_frame_cpu.camera_position_previous).LengthSquared() != 0.0f
                ? static_cast<float>(Timer::GetTimeSec()) : m_cb_frame_cpu.camera_last_movement_time;
        }
    }

    void Renderer::UpdateFrameCb_ScalarFields()
    {
        m_cb_frame_cpu.resolution_output     = m_resolution_output;
        m_cb_frame_cpu.resolution_render     = m_resolution_render;
        m_cb_frame_cpu.taa_jitter_previous   = m_cb_frame_cpu.taa_jitter_current;
        m_cb_frame_cpu.taa_jitter_current    = m_jitter_offset;
        m_cb_frame_cpu.time                  = Timer::GetTimeSec();
        m_cb_frame_cpu.delta_time            = static_cast<float>(Timer::GetDeltaTimeSec());
        m_cb_frame_cpu.frame                 = static_cast<uint32_t>(m_frame_num);
        m_cb_frame_cpu.resolution_scale      = GetResolutionScale();
        m_cb_frame_cpu.restir_pt_scale       = cvar_restir_pt_scale.GetValue();
        // 0 = sdr, 1 = hdr10 pq (vulkan), 2 = hdr scrgb (d3d12 windowed)
        m_cb_frame_cpu.hdr_enabled = 0.0f;
        if (RHI_SwapChain* swapchain = RHI_Device::GetSwapChain())
        {
            if (swapchain->IsHdr())
            {
                m_cb_frame_cpu.hdr_enabled = (swapchain->GetFormat() == RHI_Format::R16G16B16A16_Float) ? 2.0f : 1.0f;
            }
        }
        m_cb_frame_cpu.hdr_max_nits       = Display::GetLuminanceMax();
        m_cb_frame_cpu.hdr_sdr_white_nits = Display::GetSdrWhiteNits();

        // the whole hdr encode hangs off these three, log them once per change so a washed out image can be told
        // apart from a wrong nits level without a gpu capture
        {
            static float logged_mode       = -1.0f;
            static float logged_max_nits   = -1.0f;
            static float logged_white_nits = -1.0f;
            if (m_cb_frame_cpu.hdr_enabled        != logged_mode      ||
                m_cb_frame_cpu.hdr_max_nits       != logged_max_nits  ||
                m_cb_frame_cpu.hdr_sdr_white_nits != logged_white_nits)
            {
                logged_mode       = m_cb_frame_cpu.hdr_enabled;
                logged_max_nits   = m_cb_frame_cpu.hdr_max_nits;
                logged_white_nits = m_cb_frame_cpu.hdr_sdr_white_nits;
                SP_LOG_INFO(
                    "hdr encode: mode %.0f (0 sdr, 1 pq, 2 scrgb), max %.0f nits, sdr white %.0f nits, tonemapper %.0f",
                    logged_mode,
                    logged_max_nits,
                    logged_white_nits,
                    cvar_tonemapping.GetValue()
                );
            }
        }

        m_cb_frame_cpu.gamma              = cvar_gamma.GetValue();
        Camera* camera                    = World::GetCamera();
        m_cb_frame_cpu.camera_exposure    =
            camera ?
            camera->GetExposure() :
            1.0f;
        bool camera_uses_auto_exposure =
            camera &&
            camera->GetExposureMode() == CameraExposureMode::automatic;
        m_cb_frame_cpu.camera_exposure_mode =
            camera_uses_auto_exposure ?
            1.0f :
            0.0f;
        m_cb_frame_cpu.restir_pt_light_count = static_cast<float>(m_count_active_lights);
        m_cb_frame_cpu.wind                  = World::GetWind();
        m_cb_frame_cpu.cloud_coverage        = World::GetDirectionalLight() ? World::GetDirectionalLight()->GetCloudCoverage() : 0.0f;
        {
            // match the directional light's day cycle source so stars lock to the same clock as the sun
            const bool use_real_world_time = World::GetDirectionalLight() && World::GetDirectionalLight()->GetFlag(LightFlags::RealTimeCycle);
            m_cb_frame_cpu.time_of_day = World::GetTimeOfDay(use_real_world_time);
        }

        // fft ocean, geometry samples these to displace and shade the water surface
        if (const Water* water = m_pass_state.ocean)
        {
            const float* lengths                    = water->GetCascadeLengths();
            m_cb_frame_cpu.ocean_cascade_length     = Vector4(lengths[0], lengths[1], lengths[2], lengths[3]);
            m_cb_frame_cpu.ocean_sea_level          = water->GetSeaLevel();
            m_cb_frame_cpu.ocean_choppiness         = water->GetChoppiness();
            m_cb_frame_cpu.ocean_displacement_scale = water->GetDisplacementScale();
            m_cb_frame_cpu.ocean_normal_strength    = water->GetNormalStrength();
            m_cb_frame_cpu.ocean_cascade_count      = water->GetCascadeCount();
            m_cb_frame_cpu.ocean_enabled            = 1.0f;
            m_cb_frame_cpu.ocean_turbidity          = water->GetTurbidity();
            m_cb_frame_cpu.ocean_caustics_intensity = water->GetCausticsIntensity();
        }
        else
        {
            m_cb_frame_cpu.ocean_enabled = 0.0f;
        }

        m_cb_frame_cpu.terrain_height_mapping = Vector4::Zero;
        m_cb_frame_cpu.terrain_height_y       = 0.0f;
        m_cb_frame_cpu.terrain_height_enabled = 0.0f;
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

                m_cb_frame_cpu.terrain_height_mapping = mapping;
                m_cb_frame_cpu.terrain_height_y       = y;
                m_cb_frame_cpu.terrain_height_enabled = 1.0f;
            }
        }
    }

    void Renderer::UpdateFrameCb_ClusterLighting()
    {
        // log(z) slicing with the camera near/far, near clamped to avoid log(0), z scale collapses when near equals far
        const float cluster_near = std::max(near_plane, 1e-3f);
        const float cluster_far  = std::max(far_plane, cluster_near + 1e-3f);
        const float log_range    = std::log(cluster_far / cluster_near);
        const float z_scale      = log_range > 1e-6f ? static_cast<float>(CLUSTER_COUNT_Z) / log_range : 0.0f;
        const float z_bias       = -std::log(cluster_near) * z_scale;

        m_cb_frame_cpu.cluster_count_x        = CLUSTER_COUNT_X;
        m_cb_frame_cpu.cluster_count_y        = CLUSTER_COUNT_Y;
        m_cb_frame_cpu.cluster_count_z        = CLUSTER_COUNT_Z;
        m_cb_frame_cpu.cluster_light_count    = m_count_active_lights;
        m_cb_frame_cpu.cluster_z_scale        = z_scale;
        m_cb_frame_cpu.cluster_z_bias         = z_bias;
        m_cb_frame_cpu.volumetric_light_count = m_volumetric_light_count;
        m_cb_frame_cpu.cluster_padding1       = 0.0f;
    }

    void Renderer::UpdateFrameCb_FeatureBits()
    {
        // bit positions are shader abi, must match common_resources.hlsl
        // a secondary view is absent from the tlas, so every ray traced feature is off for it
        const bool ray_tracing_allowed = !secondary_render_root_active;
        const bool tlas_available      = RHI_Device::IsSupportedRayTracing() && GetTopLevelAccelerationStructure() != nullptr && ray_tracing_allowed;
        m_cb_frame_cpu.set_bit(cvar_ray_traced_reflections.GetValueAs<bool>() && ray_tracing_allowed, 1 << 0);
        m_cb_frame_cpu.set_bit(cvar_ssao.GetValueAs<bool>(),                                          1 << 1);
        m_cb_frame_cpu.set_bit(cvar_ray_traced_shadows.GetValueAs<bool>() && tlas_available,          1 << 2);
        m_cb_frame_cpu.set_bit(cvar_restir_pt.GetValueAs<bool>() && ray_tracing_allowed,              1 << 3);
    }

    void Renderer::UpdateFrameCb_StereoXr()
    {
        const uint32_t multiview_previous = m_cb_frame_cpu.is_multiview;
        if (Xr::IsSessionRunning() && Xr::GetStereoMode())
        {
            m_cb_frame_cpu.view                     = Xr::GetViewMatrix(0);
            m_cb_frame_cpu.view_inverted            = Matrix::Invert(m_cb_frame_cpu.view);
            m_cb_frame_cpu.projection               = Xr::GetProjectionMatrix(0);
            m_cb_frame_cpu.projection_inverted      = Matrix::Invert(m_cb_frame_cpu.projection);
            m_cb_frame_cpu.view_projection          = m_cb_frame_cpu.view * m_cb_frame_cpu.projection;
            m_cb_frame_cpu.view_projection_inverted = Matrix::Invert(m_cb_frame_cpu.view_projection);
            m_cb_frame_cpu.camera_position          = m_cb_frame_cpu.view_inverted.GetTranslation();

            // vr does not jitter the projection, jittered equals unjittered, replace mono fields so consumers see eye consistent data
            m_cb_frame_cpu.view_projection_previous_unjittered = m_view_projection_previous_unjittered_left;
            m_cb_frame_cpu.view_projection_unjittered          = m_cb_frame_cpu.view_projection;

            m_cb_frame_cpu.view_right                                = Xr::GetViewMatrix(1);
            m_cb_frame_cpu.view_inverted_right                       = Matrix::Invert(m_cb_frame_cpu.view_right);
            m_cb_frame_cpu.projection_right                          = Xr::GetProjectionMatrix(1);
            m_cb_frame_cpu.projection_inverted_right                 = Matrix::Invert(m_cb_frame_cpu.projection_right);
            m_cb_frame_cpu.view_projection_right                     = m_cb_frame_cpu.view_right * m_cb_frame_cpu.projection_right;
            m_cb_frame_cpu.view_projection_inverted_right            = Matrix::Invert(m_cb_frame_cpu.view_projection_right);
            m_cb_frame_cpu.view_projection_previous_right            = m_view_projection_previous_right;
            m_cb_frame_cpu.view_projection_unjittered_right          = m_cb_frame_cpu.view_projection_right;
            m_cb_frame_cpu.view_projection_previous_unjittered_right = m_view_projection_previous_right;
            m_cb_frame_cpu.camera_position_right                     = m_cb_frame_cpu.view_inverted_right.GetTranslation();
            m_cb_frame_cpu.is_multiview                              = 1;

            // record per-eye view projection so next frame's right-eye history exists, mono path tracks left eye via shared view_projection
            m_view_projection_previous_right           = m_cb_frame_cpu.view_projection_right;
            m_view_projection_previous_unjittered_left = m_cb_frame_cpu.view_projection;
        }
        else
        {
            m_cb_frame_cpu.is_multiview                              = 0;
            m_cb_frame_cpu.view_projection_previous_right            = Matrix::Identity;
            m_cb_frame_cpu.view_projection_unjittered_right          = Matrix::Identity;
            m_cb_frame_cpu.view_projection_previous_unjittered_right = Matrix::Identity;
            m_view_projection_previous_right                         = Matrix::Identity;
            m_view_projection_previous_unjittered_left               = Matrix::Identity;
        }

        // entering or leaving stereo invalidates taau history, projection setup changes
        if (multiview_previous != m_cb_frame_cpu.is_multiview)
        {
            m_taau_reset_history = true;
        }
    }

    void Renderer::UpdateFrameCb_RadialBlurHubs()
    {
        // wheel hubs for radial motion blur, computed here because entity previous matrices
        // are snapshotted (overwritten) right after the opaque g-buffer pass
        uint32_t count = 0;
        const Matrix& view_projection = m_cb_frame_cpu.view_projection_unjittered;
        const Vector2 resolution      = m_resolution_output;
        const Vector3 camera_forward  = m_cb_frame_cpu.camera_forward;
        const Vector3 camera_right    = m_cb_frame_cpu.camera_right;

        auto project_to_uv = [&view_projection](const Vector3& position_world, Vector2& uv)
        {
            Vector4 clip = Vector4(position_world.x, position_world.y, position_world.z, 1.0f) * view_projection;
            if (clip.w <= 0.0001f)
            {
                return false;
            }
            uv.x = (clip.x / clip.w) * 0.5f + 0.5f;
            uv.y = (clip.y / clip.w) * -0.5f + 0.5f;
            return true;
        };

        for (Entity* entity : render_entities())
        {
            if (count >= 8)
            {
                break;
            }

            Render* render = entity->GetComponent<Render>();
            if (!render)
            {
                continue;
            }

            Material* material = render->GetMaterial();
            if (!material || material->GetProperty(MaterialProperty::MotionBlurRadial) == 0.0f)
            {
                continue;
            }

            // per-frame rotation delta in world space, immune to the velocity aliasing that
            // makes fast wheels produce useless screen-space motion vectors
            Quaternion q_curr  = entity->GetMatrix().GetRotation();
            Quaternion q_prev  = entity->GetMatrixPrevious().GetRotation();
            Quaternion q_delta = q_curr * q_prev.Inverse();
            float w            = min(fabs(q_delta.w), 1.0f);
            float angle        = 2.0f * acosf(w);
            Vector3 axis       = Vector3(q_delta.x, q_delta.y, q_delta.z) * (q_delta.w < 0.0f ? -1.0f : 1.0f);
            if (angle < 0.005f || axis.LengthSquared() < 1e-12f)
            {
                continue;
            }
            axis.Normalize();

            const math::BoundingBox& aabb = render->GetBoundingBox();
            const Vector3 center          = aabb.GetCenter();
            const Vector3 extents         = aabb.GetExtents();
            const float radius_world      = max(extents.x, max(extents.y, extents.z));

            Vector2 uv_center;
            Vector2 uv_edge_x;
            Vector2 uv_edge_y;
            const Vector3 camera_up = Vector3::Cross(camera_forward, camera_right);
            if (!project_to_uv(center, uv_center) || !project_to_uv(center + camera_right * radius_world, uv_edge_x) || !project_to_uv(center + camera_up * radius_world, uv_edge_y))
            {
                continue;
            }
            const float radius_pixels = max(((uv_edge_x - uv_center) * resolution).Length(), ((uv_edge_y - uv_center) * resolution).Length());
            if (radius_pixels < 2.0f)
            {
                continue;
            }

            // resolve the on-screen rotation direction by projecting a rotated test point,
            // this avoids baking in any handedness or projection convention assumptions
            Vector3 perpendicular = Vector3::Cross(axis, camera_forward);
            if (perpendicular.LengthSquared() < 1e-6f)
            {
                perpendicular = Vector3::Cross(axis, Vector3::Up);
            }
            if (perpendicular.LengthSquared() < 1e-6f)
            {
                perpendicular = Vector3::Cross(axis, Vector3::Right);
            }
            perpendicular.Normalize();

            Vector2 uv_p0;
            Vector2 uv_p1;
            const Vector3 rotated = Quaternion::FromAxisAngle(axis, 0.05f) * perpendicular;
            if (!project_to_uv(center + perpendicular * radius_world, uv_p0) || !project_to_uv(center + rotated * radius_world, uv_p1))
            {
                continue;
            }
            const Vector2 d0  = uv_p0 - uv_center;
            const Vector2 d1  = uv_p1 - uv_center;
            const float cross = d0.x * d1.y - d0.y * d1.x;
            const float sign  = cross >= 0.0f ? 1.0f : -1.0f;

            m_cb_frame_cpu.radial_blur_hubs[count] = Vector4(uv_center.x, uv_center.y, angle * sign, radius_pixels);
            count++;
        }

        m_cb_frame_cpu.radial_blur_hub_count = static_cast<float>(count);
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

        // a secondary view renders a different camera into the primary frame, keeping the
        // primary history here means every pixel reports a huge velocity, which reads as
        // motion blur and taa smear over the whole preview
        if (secondary_render_root_active)
        {
            m_cb_frame_cpu.view_previous                       = m_cb_frame_cpu.view;
            m_cb_frame_cpu.projection_previous                 = m_cb_frame_cpu.projection;
            m_cb_frame_cpu.view_projection_previous            = m_cb_frame_cpu.view_projection;
            m_cb_frame_cpu.view_projection_previous_unjittered = m_cb_frame_cpu.view_projection_unjittered;
            m_cb_frame_cpu.camera_position_previous            = m_cb_frame_cpu.camera_position;
            m_cb_frame_cpu.taa_jitter_previous                 = m_cb_frame_cpu.taa_jitter_current;
        }

        // emissive triangle nee pool, must precede the cb upload because it writes the count
        // into m_cb_frame_cpu, the buffer upload itself piggybacks on the same cmd_list
        BuildEmissiveTriangleNeePool();

        GetBuffer(Renderer_Buffer::ConstantFrame)->Update(&m_cb_frame_cpu);
    }

    void Renderer::BuildEmissiveTriangleNeePool()
    {
        // skip when restir off, the buffer stays at whatever data it had previously, the count
        // is set to zero so the shader treats the pool as empty regardless of buffer contents
        if (!cvar_restir_pt.GetValueAs<bool>())
        {
            m_cb_frame_cpu.restir_pt_emissive_tri_count = 0.0f;
            return;
        }

        // statics avoid per frame heap thrash, the vectors are reused across frames and the
        // capacity ratchets up to the largest emissive render seen so far
        static vector<Sb_EmissiveTriangle>      tris;
        static vector<uint32_t>                 indices;
        static vector<RHI_Vertex_PosTexNorTan>  vertices;
        tris.clear();
        bool truncated = false;

        for (Entity* entity : render_entities())
        {
            if (!is_secondary_view_entity(entity))
            {
                continue;
            }
            Render* render = entity->GetComponent<Render>();
            if (!render)
            {
                continue;
            }

            Material* material = render->GetMaterial();
            if (!material)
            {
                continue;
            }

            // accepts the synthetic albedo to emission path or an explicit emission texture, matching the bit 15 flag in UpdateMaterials
            bool has_emission =
                material->GetProperty(MaterialProperty::EmissiveFromAlbedo) > 0.0f ||
                material->HasTextureOfType(MaterialTextureType::Emission);
            if (!has_emission)
            {
                continue;
            }

            Vector3 emission(
                material->GetProperty(MaterialProperty::ColorR),
                material->GetProperty(MaterialProperty::ColorG),
                material->GetProperty(MaterialProperty::ColorB)
            );

            // nits calibration matching light_composition, otherwise emitters glow on screen but bounce no light
            const float emissive_from_albedo = material->GetProperty(MaterialProperty::EmissiveFromAlbedo);
            const float luminous_efficacy    = 683.0f;
            const float nits                 = emissive_from_albedo > 0.0f ? emissive_from_albedo * 100000.0f : 10000.0f;
            emission *= nits / luminous_efficacy;

            float emission_lum = 0.299f * emission.x + 0.587f * emission.y + 0.114f * emission.z;
            if (emission_lum <= 0.0f)
            {
                continue;
            }

            // pull lod 0 geometry, GetGeometry copies into the static vectors so the inner
            // loop reads from contiguous memory without further indirection
            indices.clear();
            vertices.clear();
            render->GetGeometry(&indices, &vertices);
            if (indices.empty() || vertices.empty() || (indices.size() % 3u) != 0)
            {
                continue;
            }

            const Matrix& transform = entity->GetMatrix();

            uint32_t tri_count = static_cast<uint32_t>(indices.size() / 3u);
            for (uint32_t i = 0; i < tri_count; i++)
            {
                if (tris.size() >= restir_emissive_tri_max)
                {
                    truncated = true;
                    break;
                }

                uint32_t i0 = indices[i * 3u + 0u];
                uint32_t i1 = indices[i * 3u + 1u];
                uint32_t i2 = indices[i * 3u + 2u];
                if (i0 >= vertices.size() || i1 >= vertices.size() || i2 >= vertices.size())
                {
                    continue;
                }

                Vector3 p0(vertices[i0].pos[0], vertices[i0].pos[1], vertices[i0].pos[2]);
                Vector3 p1(vertices[i1].pos[0], vertices[i1].pos[1], vertices[i1].pos[2]);
                Vector3 p2(vertices[i2].pos[0], vertices[i2].pos[1], vertices[i2].pos[2]);

                p0 = transform * p0;
                p1 = transform * p1;
                p2 = transform * p2;

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

            if (truncated)
            {
                break;
            }
        }

        // a truncated pool would zero emission at vertices it cannot sample, brdf sampling is unbiased so fall back to it
        if (truncated)
        {
            static bool warned = false;
            if (!warned)
            {
                SP_LOG_WARNING("emissive triangle count exceeds the nee pool cap, falling back to brdf sampled emission");
                warned = true;
            }
            m_cb_frame_cpu.restir_pt_emissive_tri_count = 0.0f;
            return;
        }

        // build the prefix sum over picking weight, the last entry's cdf is the total weight
        // and the shader normalizes a uniform xi against it to area sample a triangle
        float total_weight = 0.0f;
        for (auto& t : tris)
        {
            total_weight += t.weight;
            t.cdf         = total_weight;
        }

        if (!tris.empty() && total_weight > 0.0f)
        {
            RHI_Buffer* emissive_triangles_buffer = GetBuffer(Renderer_Buffer::EmissiveTriangles);
            emissive_triangles_buffer->ResetOffset();
            emissive_triangles_buffer->Update(
                tris.data(),
                static_cast<uint32_t>(tris.size() * sizeof(Sb_EmissiveTriangle))
            );
            m_cb_frame_cpu.restir_pt_emissive_tri_count = static_cast<float>(tris.size());
        }
        else
        {
            m_cb_frame_cpu.restir_pt_emissive_tri_count = 0.0f;
        }
    }

    const Vector3& Renderer::GetWind()
    {
        return World::GetWind();
    }

    void Renderer::SetWind(const math::Vector3& wind)
    {
        World::SetWind(wind);
    }

    void Renderer::EnableProceduralGrass(
        Mesh* grass_mesh,
        Material* grass_material,
        RHI_Texture* terrain_heightmap,
        const ProceduralGrassParams& params,
        RHI_Texture* terrain_prop_mask
    )
    {
        // every required input must be present, otherwise the populate compute will read garbage
        // from a freed heightmap, the indirect args would point past the global geometry buffer, etc
        if (!grass_mesh || !grass_material || !terrain_heightmap)
        {
            m_pass_state.grass_enabled = false;
            return;
        }

        m_pass_state.grass_mesh      = grass_mesh;
        m_pass_state.grass_material  = grass_material;
        m_pass_state.grass_heightmap = terrain_heightmap;
        m_pass_state.grass_prop_mask = terrain_prop_mask;
        m_pass_state.grass_params    = params;
        m_pass_state.grass_enabled   = true;

        // Render::SetMaterial normally derives these, procedural grass has no Render component and the blade vs divides by zero without them
        {
            const std::vector<RHI_Vertex_PosTexNorTan>& mesh_vertices = grass_mesh->GetVertices();
            if (!mesh_vertices.empty())
            {
                const SubMesh& sm = grass_mesh->GetSubMesh(0);
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
                    grass_material->SetProperty(MaterialProperty::WorldWidth,  max_x - min_x);
                    grass_material->SetProperty(MaterialProperty::WorldHeight, max_y - min_y);
                }
            }
        }

        // bake static portions of the per-lod indirect args from the mesh's lod layout in the global geometry buffer
        // instance_count is dynamic and written each frame by grass_indirect_args.hlsl, the rest stays frozen here
        const SubMesh& sub      = grass_mesh->GetSubMesh(0);
        const uint32_t lod_cap  = renderer_max_grass_lod_count;
        const uint32_t lod_have = static_cast<uint32_t>(sub.lods.size());
        const uint32_t lod_use  = std::min(lod_cap, lod_have);

        for (uint32_t i = 0; i < lod_cap; i++)
        {
            // grass mesh may have fewer than 3 lods on load races, fall back to lod 0 so we never index out of bounds
            const uint32_t lod_index = i < lod_use ? i : 0u;
            const MeshLod& lod       = sub.lods[lod_index];

            Sb_IndirectDrawArgs& args = m_pass_state.grass_indirect_args_static[i];
            args.index_count          = lod.index_count;
            args.instance_count       = 0; // filled by grass_indirect_args_c each frame
            args.first_index          = grass_mesh->GetGlobalIndexOffset() + lod.index_offset;
            args.vertex_offset        = static_cast<int32_t>(grass_mesh->GetGlobalVertexOffset() + lod.vertex_offset);
            args.first_instance       = 0; // lod_base is fed via push constant instead, sv_instanceid is per-draw zero-based
        }

        m_pass_state.grass_args_baked = false; // commit on the first frame after enable

        // the grass material belongs to no entity, only the material upload hands it a bindless slot,
        // so it has to be asked for or the blades sample whatever material already sits at that index
        m_pass_state.bindless_materials_dirty = true;
    }

    void Renderer::DisableProceduralGrass()
    {
        m_pass_state.grass_enabled            = false;
        m_pass_state.grass_mesh               = nullptr;
        m_pass_state.grass_material           = nullptr;
        m_pass_state.grass_heightmap          = nullptr;
        m_pass_state.grass_prop_mask          = nullptr;
        m_pass_state.grass_args_baked         = false;
        m_pass_state.bindless_materials_dirty = true;
    }

    bool Renderer::IsProceduralGrassEnabled()
    {
        return m_pass_state.grass_enabled;
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

    void Renderer::DrawIcon(RHI_Texture* icon, const math::Vector2& position_screen_percentage)
    {
        Vector3 world_position = World::GetCamera()->ScreenToWorldCoordinates(position_screen_percentage, 0.5f);

        if (icon)
        {
            m_icons.emplace_back(make_tuple(icon, world_position));
        }
    }

    void Renderer::SetPresentInRenderer(const bool enabled)
    {
        m_present_in_renderer = enabled;
    }

    void Renderer::BlitToXrSwapchain(RHI_Texture* texture)
    {
        RHI_CommandList::BeginMarker("blit_to_xr_swapchain");
        RHI_CommandList::BlitToXrSwapchain(texture);
        RHI_CommandList::EndMarker();
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

    void Renderer::BeginPass(const char* name, uint32_t eye_layer, bool bind_ssao)
    {
        RHI_CommandList::BeginPass(name);
        s_common_bind = true;
        s_common_eye = eye_layer;
        s_common_ssao = bind_ssao;
    }

    void Renderer::SetPass(const char* name, uint32_t eye_layer, bool bind_ssao)
    {
        RHI_CommandList::SetPass(name);
        s_common_bind = true;
        s_common_eye = eye_layer;
        s_common_ssao = bind_ssao;
    }

    void Renderer::SetCommonTextures(uint32_t eye_layer /*= rhi_all_mips*/, bool bind_ssao /*= true*/)
    {
        // gbuffer (when eye_layer is specified, bind per-layer 2d views for compute passes)
        RHI_CommandList::SetTexture("tex_albedo",   GetRenderTarget(Renderer_RenderTarget::gbuffer_color),    rhi_all_mips, 0, eye_layer);
        RHI_CommandList::SetTexture("tex_normal",   GetRenderTarget(Renderer_RenderTarget::gbuffer_normal),   rhi_all_mips, 0, eye_layer);
        RHI_CommandList::SetTexture("tex_material", GetRenderTarget(Renderer_RenderTarget::gbuffer_material), rhi_all_mips, 0, eye_layer);
        RHI_CommandList::SetTexture("tex_velocity", GetRenderTarget(Renderer_RenderTarget::gbuffer_velocity), rhi_all_mips, 0, eye_layer);
        RHI_CommandList::SetTexture("tex_depth",    GetRenderTarget(Renderer_RenderTarget::gbuffer_depth),    rhi_all_mips, 0, eye_layer);

        // ssao is written on async compute, skip binding it during graphics phase 2
        // stereo skips the ssao pass, bind white so left eye occlusion does not hit the right eye
        if (bind_ssao)
        {
            const bool xr_stereo = Xr::IsSessionRunning() && Xr::GetStereoMode();
            RHI_Texture* tex_ssao = (!xr_stereo) ? GetRenderTarget(Renderer_RenderTarget::ssao) : nullptr;
            RHI_CommandList::SetTexture("tex_ssao", tex_ssao ? tex_ssao : GetStandardTexture(Renderer_StandardTexture::White));
        }
    }

    uint32_t Renderer::WriteDrawData(const math::Matrix& transform, const math::Matrix& transform_previous, uint32_t material_index, uint32_t is_transparent, const Render* render)
    {
        // soft fail, world draws and imgui share this buffer so a busy scene plus a dense asset
        // browser can hit the ceiling, asserting here crashed the editor on folder navigation
        if (m_draw_data_count >= renderer_max_draw_calls)
        {
            static bool logged = false;
            if (!logged)
            {
                SP_LOG_WARNING("draw data budget exhausted (%u), dropping further draws this frame", renderer_max_draw_calls);
                logged = true;
            }
            return numeric_limits<uint32_t>::max();
        }

        uint32_t index = m_draw_data_count++;

        Sb_DrawData& entry       = m_draw_data_cpu[index];
        entry.transform          = transform;
        entry.transform_previous = transform_previous;
        entry.material_index     = material_index;
        entry.is_transparent     = is_transparent;
        entry.aabb_index         = 0;
        entry.lod_first_index    = 0;
        entry.flags              = 0;
        entry.instance_offset    = 0;
        entry.instance_index     = 0;
        entry.lod_vertex_offset  = 0;

        fill_uv_draw_fields_from_render(entry, render);

        // the draw data buffer is a single large allocation partitioned into per-frame regions;
        // each frame writes to its own region so there is no write-after-read race with the gpu
        uint32_t global_index = m_frame_resource_index * renderer_max_draw_calls + index;

        RHI_Buffer* buffer = GetBuffer(Renderer_Buffer::DrawData);
        if (void* mapped = buffer->GetMappedData())
        {
            void* dst = static_cast<char*>(mapped) + global_index * sizeof(Sb_DrawData);
            memcpy(dst, &entry, sizeof(Sb_DrawData));
        }
        else if (m_draw_data_gpu_synced)
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
        static array<Sb_Material, rhi_max_array_size> properties;
        static unordered_set<uint64_t> unique_material_ids;
        static bool capacity_warning_logged = false;
        uint32_t count = 0;
        const uint32_t material_slot_count = static_cast<uint32_t>(MaterialTextureType::Max) * Material::slots_per_texture;

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
    
        auto update_material = [&count, &should_decode_as_srgb, material_slot_count](Material* material)
        {
            if (unique_material_ids.find(material->GetObjectId()) != unique_material_ids.end())
            {
                return;
            }

            if (count + material_slot_count > rhi_max_array_size)
            {
                material->SetIndex(0);
                if (!capacity_warning_logged)
                {
                    SP_LOG_ERROR("material bindless capacity exceeded, overflowing materials will use index 0");
                    capacity_warning_logged = true;
                }
                return;
            }

            unique_material_ids.insert(material->GetObjectId());
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
                properties[count].ior                   = material->GetProperty(MaterialProperty::Ior);
                properties[count].absorption            = material->GetProperty(MaterialProperty::Absorption);
                properties[count].thickness             = material->GetProperty(MaterialProperty::Thickness);
                properties[count].sheen                 = material->GetProperty(MaterialProperty::Sheen);
                properties[count].subsurface_scattering = material->GetProperty(MaterialProperty::SubsurfaceScattering);

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
                // keep in sync with Surface struct in common_structs.hlsl
            }
    
            // textures
            {
                for (uint32_t type = 0; type < static_cast<uint32_t>(MaterialTextureType::Max); type++)
                {
                    for (uint32_t slot = 0; slot < Material::slots_per_texture; slot++)
                    {
                        uint32_t bindless_index = count + (type * Material::slots_per_texture) + slot;
                        m_bindless_textures[bindless_index] = material->GetTexture(static_cast<MaterialTextureType>(type), slot);
                    }
                }
            }
    
            material->SetIndex(count);

            count += material_slot_count;
        };
    
        auto update_entities = [update_material]()
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
    
        properties.fill(Sb_Material{});
        m_bindless_textures.fill(nullptr);
        unique_material_ids.clear();

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

                layer_count++;
            }

            // the surface material follows the block, its own entry points back at the base
            const uint32_t surface_index = count;
            update_material(terrain.surface);
            if (count != surface_index)
            {
                Sb_Material& entry = properties[surface_index];

                entry.terrain_world_mapping = terrain.world_mapping;
                entry.terrain_sea_level     = terrain.sea_level;
                entry.terrain_snow_level    = terrain.snow_level;
                entry.terrain_layer_base    = layer_base;
                entry.terrain_layer_count   = layer_count;
                entry.terrain_layer_stride  = material_slot_count;
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

        // procedural grass material is not attached to any entity, register it here so it lands in the
        // bindless table and material_index can be pushed into the grass raster passes via the push constant
        if (m_pass_state.grass_enabled && m_pass_state.grass_material)
        {
            update_material(m_pass_state.grass_material);
        }

        RHI_Buffer* buffer = Renderer::GetBuffer(Renderer_Buffer::MaterialParameters);
        buffer->ResetOffset();
        buffer->Update(&properties[0], buffer->GetStride() * count);
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
                    light_buffer_entry.atlas_texel_sizes[i] = Vector2(1.0f / rect.width, 1.0f / rect.height);
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
        Camera* camera = World::GetCamera();
        const Vector3 camera_pos = camera ? camera->GetEntity()->GetPosition() : Vector3::Zero;
        const float flare_max_distance = cvar_light_flares.GetValueAs<bool>() ? max(cvar_light_flares_max_distance.GetValue(), 0.0f) : 0.0f;
        const float flare_max_distance_sq = flare_max_distance * flare_max_distance;

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

                const bool within_draw_distance = light_component->IsActiveByDistance();
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
    
                if (camera && !camera->IsInViewFrustum(light_component->GetBoundingBox()))
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

        // the atmosphere is driven entirely by slot 0, so a world with no lights at all leaves the
        // sky panorama black, a neutral default sun keeps a viewport usable before anything is
        // loaded, worlds that deliberately light with point lights only are left untouched
        if (!first_directional && m_count_active_lights == 0)
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
        buffer->ResetOffset();
        
        if (m_count_active_lights > 0)
        {
            buffer->Update(&m_bindless_lights[0], buffer->GetStride() * m_count_active_lights);
        }

        // upload the compact volumetric light index list, count flows through buffer_frame.volumetric_light_count
        m_volumetric_light_count = volumetric_count;
        if (volumetric_count > 0)
        {
            RHI_Buffer* vol_buffer = GetBuffer(Renderer_Buffer::VolumetricLightIndices);
            vol_buffer->ResetOffset();
            vol_buffer->Update(&volumetric_indices[0], vol_buffer->GetStride() * volumetric_count);
        }

        prev_light_count = m_count_active_lights;
    }

    void Renderer::UpdateBoundingBoxes()
    {
        static uint32_t prev_aabb_count = 0;
        for (uint32_t i = 0; i < prev_aabb_count; i++)
        {
            m_bindless_aabbs[i] = Sb_Aabb();
        }

        // prepass aabbs come first in the buffer, slot index is the prepass draw index
        for (uint32_t i = 0; i < m_draw_calls_prepass_count; i++)
        {
            const Renderer_DrawCall& draw_call = m_draw_calls_prepass[i];
            Render* render                     = draw_call.render;
            const BoundingBox& aabb            = render->GetBoundingBox();
            m_bindless_aabbs[i].min            = aabb.GetMin();
            m_bindless_aabbs[i].max            = aabb.GetMax();
            m_bindless_aabbs[i].is_occluder    = draw_call.is_occluder;
        }

        // indirect draw aabbs, the slot is taken straight from m_indirect_draw_data[].aabb_index so the writes
        // here always land on the slot the cull shader will read for that draw, no filter divergence can drift it
        const uint32_t aabb_frame_offset = m_frame_resource_index * rhi_max_array_size;
        for (uint32_t i = 0; i < m_indirect_draw_count; i++)
        {
            Render* render                   = m_indirect_renders[i];
            const Sb_DrawData& draw_data     = m_indirect_draw_data[i];
            const uint32_t aabb_slot_global  = draw_data.aabb_index;
            if (aabb_slot_global < aabb_frame_offset)
            {
                continue;
            }
            const uint32_t aabb_slot = aabb_slot_global - aabb_frame_offset;
            if (aabb_slot >= rhi_max_array_size)
            {
                continue;
            }
            const BoundingBox& aabb         = render->GetBoundingBox();
            m_bindless_aabbs[aabb_slot].min = aabb.GetMin();
            m_bindless_aabbs[aabb_slot].max = aabb.GetMax();
        }

        // upload covers both the prepass region and the trailing indirect region, the indirect aabb slots start at m_draw_calls_prepass_count
        const uint32_t total_aabb_count = m_draw_calls_prepass_count + m_indirect_render_count;
        if (total_aabb_count > 0)
        {
            RHI_Buffer* buffer         = GetBuffer(Renderer_Buffer::AABBs);
            uint32_t frame_byte_offset = m_frame_resource_index * rhi_max_array_size * static_cast<uint32_t>(sizeof(Sb_Aabb));
            uint32_t upload_size       = static_cast<uint32_t>(sizeof(Sb_Aabb)) * total_aabb_count;
            RHI_CommandList::UpdateBuffer(buffer, frame_byte_offset, upload_size, &m_bindless_aabbs[0]);
        }
        prev_aabb_count = total_aabb_count;
    }

    void Renderer::UpdateDrawCalls_ResetCounts()
    {
        m_draw_call_count           = 0;
        m_draw_calls_prepass_count  = 0;
        m_draw_data_count           = 0;
        m_draw_data_gpu_synced      = false;
        m_indirect_draw_count       = 0;
        m_indirect_render_count     = 0;
        m_cull_task_count           = 0;
        m_transparents_present      = false;
    }

    void Renderer::UpdateDrawCalls_CollectAndSort()
    {
        const bool tlas_available =
            RHI_Device::IsSupportedRayTracing() &&
            GetTopLevelAccelerationStructure() != nullptr &&
            !IsSecondaryViewActive();
        const bool shadow_maps_required =
            World::GetLightCount() > 0 &&
            !(
                cvar_ray_traced_shadows.GetValueAs<bool>() &&
                tlas_available
            );

        for (Entity* entity : render_entities())
        {
            if (!entity || !entity->GetActive())
            {
                continue;
            }
            if (
                secondary_render_root_active &&
                entity != secondary_render_root_active &&
                !entity->IsDescendantOf(
                    secondary_render_root_active
                )
            )
            {
                continue;
            }

            // a worker may still be assigning the Render component, the mesh or the material, so guard every step
            Render* render = entity->GetComponent<Render>();
            if (!render || !render->GetMesh())
            {
                continue;
            }

            Material* material = render->GetMaterial();
            if (!material)
            {
                continue;
            }

            // off-screen geometry is only kept when classic shadow maps need the caster
            if (!render->IsVisible())
            {
                if (material->IsTransparent())
                {
                    m_transparents_present = true;
                }

                if (
                    !shadow_maps_required ||
                    !render->HasFlag(RenderFlags::CastsShadows)
                )
                {
                    continue;
                }
            }

            if (material->IsTransparent())
            {
                m_transparents_present = true;
            }

            if (m_draw_call_count >= renderer_max_draw_calls)
            {
                break;
            }

            uint32_t draw_data_index = WriteDrawData(
                entity->GetMatrix(),
                matrix_previous_for_velocity(entity),
                material->GetIndex(),
                material->IsTransparent() ? 1 : 0,
                render
            );
            if (draw_data_index == numeric_limits<uint32_t>::max())
            {
                break;
            }

            Renderer_DrawCall& draw_call = m_draw_calls[m_draw_call_count++];
            draw_call.render             = render;
            draw_call.distance_squared   = render->GetDistanceSquared();
            draw_call.lod_index          = render->GetLodIndex();
            draw_call.is_occluder        = false;
            draw_call.camera_visible     = render->IsVisible();
            draw_call.instance_index     = 0;
            draw_call.instance_count     = render->GetInstanceCount();
            draw_call.draw_data_index    = draw_data_index;
        }

        // opaque before transparent, then by material id, then by distance
        sort(m_draw_calls.begin(), m_draw_calls.begin() + m_draw_call_count, [](const Renderer_DrawCall& a, const Renderer_DrawCall& b)
        {
            const bool a_transparent = a.render->GetMaterial()->IsTransparent();
            const bool b_transparent = b.render->GetMaterial()->IsTransparent();
            if (a_transparent != b_transparent)
            {
                return !a_transparent;
            }

            const uint64_t a_material_id = a.render->GetMaterial()->GetObjectId();
            const uint64_t b_material_id = b.render->GetMaterial()->GetObjectId();
            if (a_material_id != b_material_id)
            {
                return a_material_id < b_material_id;
            }

            return a_transparent ? a.distance_squared > b.distance_squared : a.distance_squared < b.distance_squared;
        });
    }

    void Renderer::UpdateDrawCalls_BuildPrepass()
    {
        for (uint32_t i = 0; i < m_draw_call_count; ++i)
        {
            const Renderer_DrawCall& dc = m_draw_calls[i];
            if (!dc.render->GetMaterial()->IsTransparent() && dc.camera_visible)
            {
                m_draw_calls_prepass[m_draw_calls_prepass_count++] = dc;
            }
        }

        sort(m_draw_calls_prepass.begin(), m_draw_calls_prepass.begin() + m_draw_calls_prepass_count, [](const Renderer_DrawCall& a, const Renderer_DrawCall& b)
        {
            const bool a_alpha = a.render->GetMaterial()->IsAlphaTested();
            const bool b_alpha = b.render->GetMaterial()->IsAlphaTested();
            if (a_alpha != b_alpha)
            {
                return !a_alpha;
            }
            return a.distance_squared < b.distance_squared;
        });
    }

    void Renderer::UpdateDrawCalls_BuildIndirectAndCullTasks()
    {
        // one draw entry per render lod, one instance cull task per (render, instance) tuple
        // phase a compacts visible instances, phase b expands their meshlets, the triangle cull then feeds the indirect draw
        m_indirect_draw_count       = 0;
        m_indirect_render_count = 0;
        m_cull_task_count           = 0;
        const uint32_t aabb_frame_offset = m_frame_resource_index * rhi_max_array_size;
        const uint32_t indirect_draw_capacity = GetBuffer(Renderer_Buffer::IndirectDrawData)->GetElementCount();
        const uint32_t cull_task_capacity = GetBuffer(Renderer_Buffer::CullTasks)->GetElementCount();
        const uint32_t meshlet_instance_capacity = GetBuffer(Renderer_Buffer::MeshletInstances)->GetElementCount();

        // the cull pipeline drops survivors silently once a budget is hit, tracked so the log below can name the culprit
        uint64_t expected_survivors_worst_case = 0;
        uint32_t cull_task_overflow_renders = 0;

        for (uint32_t i = 0; i < m_draw_call_count; i++)
        {
            const Renderer_DrawCall& dc = m_draw_calls[i];
            Render* render              = dc.render;
            Material* material          = render->GetMaterial();

            if (!material || material->IsTransparent())
            {
                continue;
            }
            if (!dc.camera_visible)
            {
                continue;
            }
            if (material->GetProperty(MaterialProperty::Tessellation) > 0.0f)
            {
                continue;
            }

            const uint32_t lod_index_count = render->GetIndexCount(dc.lod_index);
            if (lod_index_count == 0)
            {
                continue;
            }

            const uint32_t lod_meshlet_count = render->GetMeshletCount(dc.lod_index);
            if (lod_meshlet_count == 0)
            {
                continue;
            }

            const bool is_instanced = dc.instance_count > 1;
            const uint32_t inst_n   = is_instanced ? dc.instance_count : 1;

            // one instance cull task per instance (phase a), phase b expands the meshlets of the survivors,
            // so the task budget scales with instance count, not meshlets x instances as the old single-phase path did
            const uint32_t tasks_add = inst_n;

            if (m_indirect_draw_count + 1 > indirect_draw_capacity)
            {
                continue;
            }
            if (m_cull_task_count + tasks_add > cull_task_capacity)
            {
                cull_task_overflow_renders++;
                continue;
            }

            // worst case survivor accounting, every instance visible and emitting all of its meshlets
            expected_survivors_worst_case += static_cast<uint64_t>(inst_n) * static_cast<uint64_t>(lod_meshlet_count);

            const uint32_t render_aabb_slot     = aabb_frame_offset + m_draw_calls_prepass_count + m_indirect_render_count;
            const uint32_t base_first_index     = render->GetIndexOffset(dc.lod_index);
            const uint32_t vertex_offset        = render->GetVertexOffset(dc.lod_index);
            const uint32_t base_meshlet_index   = render->GetGlobalMeshletOffset() + render->GetMeshletOffset(dc.lod_index);

            Entity* entity = render->GetEntity();
            Mesh* mesh     = render->GetMesh();

            // flags bit 0 skinned, bit 1 per instance, bit 3 two sided, bit 4 alpha tested, bit 5 skip hi-z
            // override means cpu already wrote a deformed world aabb, use the skinned cull path so phase a does not test lod_aabb * entity
            const bool is_skinned       =
                render->HasBoundingBoxOverride() ||
                (mesh->IsSkinned() && !cvar_meshlet_cull_skinned.GetValueAs<bool>());
            const bool use_per_instance = is_instanced;
            const bool is_two_sided     = static_cast<RHI_CullMode>(material->GetProperty(MaterialProperty::CullMode)) != RHI_CullMode::Back;
            const bool is_alpha_tested  = material->IsAlphaTested();
            const bool skip_hiz         = entity && entity->GetTimeSinceLastTransform() <= 0.1f;
            uint32_t base_flags         = 0u;
            if (is_skinned)
            {
                base_flags |= 1u;
            }
            if (use_per_instance)
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

            const uint32_t draw_idx        = m_indirect_draw_count++;
            Sb_DrawData& draw_data         = m_indirect_draw_data[draw_idx];
            draw_data.transform            = entity->GetMatrix();
            draw_data.transform_previous   = matrix_previous_for_velocity(entity);
            draw_data.material_index       = material->GetIndex();
            draw_data.is_transparent       = 0;
            draw_data.aabb_index           = render_aabb_slot;
            draw_data.lod_first_index      = base_first_index;
            draw_data.flags                = base_flags;
            draw_data.instance_offset      = render->GetGlobalInstanceOffset();
            draw_data.instance_index       = 0;
            draw_data.lod_vertex_offset    = vertex_offset;
            draw_data.lod_meshlet_offset   = base_meshlet_index;
            draw_data.lod_meshlet_count    = lod_meshlet_count;
            fill_uv_draw_fields_from_render(draw_data, render);

            // lod-local aabb, must match the one build_meshlets quantized the compressed meshlet bounds against
            // diag is precomputed length(extent), the cull shader uses it to dequantize radius without a sqrt
            const BoundingBox& lod_aabb_local = render->GetLodAabb(dc.lod_index);
            const Vector3 lod_extent          = lod_aabb_local.GetMax() - lod_aabb_local.GetMin();
            draw_data.lod_aabb_min            = lod_aabb_local.GetMin();
            draw_data.lod_aabb_extent         = lod_extent;
            draw_data.lod_aabb_diag           = lod_extent.Length();

            // squared once here so the cull shader skips a sqrt, zero disables the check, this is the only per-instance distance test for consolidated entities
            const float max_distance          = render->GetMaxRenderDistance();
            const bool  finite_distance       = max_distance > 0.0f && max_distance < numeric_limits<float>::max() * 0.5f;
            draw_data.max_render_distance_squared = finite_distance ? (max_distance * max_distance) : 0.0f;

            // parallel render handle, UpdateBoundingBoxes uses this to write each aabb at exactly the slot the cull shader will read
            m_indirect_renders[draw_idx] = render;

            // one instance cull task per instance, phase a tests each instance's bounds, phase b expands the survivors' meshlets
            for (uint32_t inst = 0; inst < inst_n; inst++)
            {
                Sb_CullTask& task   = m_cull_tasks[m_cull_task_count++];
                task.draw_index     = draw_idx;
                task.meshlet_index  = 0; // unused in phase a, the meshlet range lives on DrawData
                task.instance_index = inst;
                task.instance_count = 1;
            }

            m_indirect_render_count++;
        }

        // reported once per session, either the survivor count exceeded the buffer or the cull task budget rejected renders
        static bool s_logged_survivor_overflow   = false;
        static bool s_logged_cull_task_overflow  = false;
        if (!s_logged_survivor_overflow && expected_survivors_worst_case > meshlet_instance_capacity)
        {
            SP_LOG_WARNING(
                "meshlet cull survivor buffer is too small, worst case %llu > capacity %u, distant or later-submitted geometry may be silently dropped",
                static_cast<unsigned long long>(expected_survivors_worst_case),
                meshlet_instance_capacity
            );
            s_logged_survivor_overflow = true;
        }
        if (!s_logged_cull_task_overflow && cull_task_overflow_renders > 0)
        {
            SP_LOG_WARNING(
                "instance cull task budget exhausted, %u render lods rejected, current capacity is %u",
                cull_task_overflow_renders,
                cull_task_capacity
            );
            s_logged_cull_task_overflow = true;
        }
    }

    void Renderer::UpdateDrawCalls_SelectOccluders()
    {
        // top n by screen area with temporal hysteresis, the prior occluder set gets a 1.5x area bonus
        // recently moved meshes are excluded, otherwise a rotating prop writes hi-z that meshlet-culls itself next frame
        static unordered_set<Render*> previous_occluders;

        Camera* camera = World::GetCamera();
        if (!camera)
        {
            return;
        }

        const float move_window_sec = max(
            static_cast<float>(Timer::GetDeltaTimeSec()) * 2.0f,
            0.05f
        );

        auto compute_screen_space_area = [&](const BoundingBox& aabb_world) -> float
        {
            math::Rectangle rect_screen = camera->WorldToScreenCoordinates(aabb_world);
            return clamp(rect_screen.width * rect_screen.height, 0.0f, numeric_limits<float>::max());
        };

        struct DrawCallArea { uint32_t index; float area; };
        static vector<DrawCallArea> areas;
        areas.clear();
        areas.reserve(m_draw_calls_prepass_count);

        for (uint32_t i = 0; i < m_draw_calls_prepass_count; i++)
        {
            Renderer_DrawCall& draw_call = m_draw_calls_prepass[i];
            Render* render               = draw_call.render;
            Material* material           = render->GetMaterial();

            if (!material || material->IsTransparent() || render->HasInstancing() || !draw_call.camera_visible)
            {
                continue;
            }

            Entity* entity = render->GetEntity();
            if (entity && entity->GetTimeSinceLastTransform() <= move_window_sec)
            {
                continue;
            }

            float screen_area = compute_screen_space_area(render->GetBoundingBox());
            if (previous_occluders.find(render) != previous_occluders.end())
            {
                screen_area *= 1.5f;
            }

            areas.push_back({ i, screen_area });
        }

        const uint32_t max_occluders = 64;
        if (areas.size() > max_occluders)
        {
            partial_sort(
                areas.begin(),
                areas.begin() + max_occluders,
                areas.end(),
                [](const DrawCallArea& a, const DrawCallArea& b) { return a.area > b.area; }
            );
            areas.resize(max_occluders);
        }
        else
        {
            sort(areas.begin(), areas.end(), [](const DrawCallArea& a, const DrawCallArea& b) { return a.area > b.area; });
        }

        const uint32_t occluder_count = static_cast<uint32_t>(areas.size());

        previous_occluders.clear();
        for (uint32_t i = 0; i < occluder_count; i++)
        {
            m_draw_calls_prepass[areas[i].index].is_occluder = true;
            previous_occluders.insert(m_draw_calls_prepass[areas[i].index].render);
        }
    }

    void Renderer::UpdateDrawCalls()
    {
        UpdateDrawCalls_ResetCounts();
        UpdateDrawCalls_CollectAndSort();
        UpdateDrawCalls_BuildPrepass();
        UpdateDrawCalls_BuildIndirectAndCullTasks();
        UpdateDrawCalls_SelectOccluders();
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

    void Renderer::UpdateAccelerationStructures()
    {
        bool ray_tracing_enabled = cvar_ray_traced_reflections.GetValueAs<bool>() || cvar_ray_traced_shadows.GetValueAs<bool>() || cvar_restir_pt.GetValueAs<bool>();
        if (!ray_tracing_enabled)
        {
            return;
        }

        if (!RHI_Device::IsSupportedRayTracing() || !RHI_Device::IsRecording())
        {
            SP_LOG_WARNING("Ray tracing or command list invalid, skipping update");
            return;
        }

        if (ProgressTracker::IsLoading())
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

        // blas builds are capped per frame, recording thousands onto one command list hits driver tdr
        bool blas_burst_done = false;
        bool blas_refit_done = false;
        bool blas_built_this_frame = false;
        {
            RHI_CommandList::BeginMarker("blas_build");

            constexpr uint32_t blas_builds_per_frame = 64;

            uint32_t blas_built     = 0;
            uint32_t blas_remaining = 0;
            uint32_t blas_total     = 0;
            for (Entity* entity : render_entities())
            {
                if (!entity || !entity->GetActive())
                {
                    continue;
                }
                if (!is_secondary_view_entity(entity))
                {
                    continue;
                }

                Render* render = entity->GetComponent<Render>();
                if (!render)
                {
                    continue;
                }

                // skip the ray tracing path for render components that opt out (foliage, anything with millions of instances)
                // these never become a blas and never enter the tlas, so the big instance buffers don't drag blas memory along with them
                if (render->HasFlag(RenderFlags::ExcludeFromRayTracing))
                {
                    continue;
                }

                blas_total++;

                if (!render->HasAccelerationStructure())
                {
                    if (blas_built < blas_builds_per_frame)
                    {
                        render->BuildAccelerationStructure();
                        if (render->HasAccelerationStructure())
                        {
                            blas_built++;
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

            blas_burst_done = (blas_remaining == 0);
            blas_built_this_frame = blas_built > 0;

            // free the shared static scratch only once the burst fully completes
            // freeing mid-burst would force a reallocation on next frame
            if (blas_burst_done && blas_built > 0)
            {
                RHI_AccelerationStructure::FreeSharedBlasScratch();
                SP_LOG_INFO("Ray tracing: BLAS build burst complete (last frame built %u, total %u)", blas_built, blas_total);
            }

            RHI_CommandList::EndMarker();
        }

        // skip tlas build until all blas are ready so we don't keep rebuilding it with an incomplete set
        if (!blas_burst_done)
        {
            return;
        }

        // static scenes keep a valid tlas, only rebuild when transforms, materials, or blas move
        {
            bool needs_tlas_rebuild =
                !m_tlas ||
                materials_uploaded_this_frame ||
                blas_refit_done ||
                blas_built_this_frame;
            materials_uploaded_this_frame = false;

            if (!needs_tlas_rebuild)
            {
                const float move_window_sec = max(
                    static_cast<float>(Timer::GetDeltaTimeSec()) * 2.0f,
                    0.05f
                );

                for (Entity* entity : render_entities())
                {
                    if (!entity || !entity->GetActive())
                    {
                        continue;
                    }
                    if (!is_secondary_view_entity(entity))
                    {
                        continue;
                    }

                    Render* render = entity->GetComponent<Render>();
                    if (
                        !render ||
                        render->HasFlag(RenderFlags::ExcludeFromRayTracing)
                    )
                    {
                        continue;
                    }

                    if (entity->GetTimeSinceLastTransform() <= move_window_sec)
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
        }

        // tlas
        {
            RHI_CommandList::BeginMarker("tlas_build");

            if (!m_tlas)
            {
                m_tlas = make_unique<RHI_AccelerationStructure>(RHI_AccelerationStructureType::Top, "world_tlas");
            }

            constexpr uint32_t RHI_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT = 0x00000002; // VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR

            static vector<RHI_AccelerationStructureInstance> instances; // static to avoid per-frame heap alloc
            static vector<Sb_GeometryInfo> geometry_infos;
            instances.clear();
            geometry_infos.clear();

            uint32_t invalid_transforms   = 0;
            const char* first_invalid_name = nullptr;

            for (Entity* entity : render_entities())
            {
                if (!entity || !entity->GetActive())
                {
                    continue;
                }
                if (!is_secondary_view_entity(entity))
                {
                    continue;
                }

                Render* render = entity->GetComponent<Render>();
                if (!render)
                {
                    continue;
                }

                // same opt-out as the blas loop above, keep the tlas instance list in sync with the blas set so we don't try to register a render component that has no blas
                if (render->HasFlag(RenderFlags::ExcludeFromRayTracing))
                {
                    continue;
                }

                Material* material = render->GetMaterial();
                if (!material)
                {
                    continue;
                }

                uint64_t device_address = render->GetAccelerationStructureDeviceAddress();
                if (device_address == 0)
                {
                    continue;
                }

                RHI_Buffer* vertex_buffer = render->GetVertexBuffer();
                RHI_Buffer* index_buffer  = render->GetIndexBuffer();
                if (!vertex_buffer || !index_buffer)
                {
                    continue;
                }

                RHI_CullMode cull_mode = static_cast<RHI_CullMode>(material->GetProperty(MaterialProperty::CullMode));

                RHI_AccelerationStructureInstance instance           = {};
                instance.instance_custom_index                       = material->GetIndex();             // for hit shader material lookup
                instance.mask                                        = material->IsTransparent() ? 0x02 : 0x01; // bit 0 = opaque, bit 1 = transparent, lets shadow rays exclude transparents
                instance.instance_shader_binding_table_record_offset = 0;                                // sbt hit group offset
                instance.flags                                       = cull_mode == RHI_CullMode::None ? RHI_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT : 0;
                instance.device_address                              = device_address;

                // row-major 3x4 transform (transpose 3x3 because vulkan uses column vectors)
                const Matrix& m = render->GetEntity()->GetMatrix();

                // one bad matrix is enough to wreck traversal for the whole scene, drop the instance
                // rather than let it into the tlas, the entity loses its ray traced shadow and nothing else
                if (!is_transform_traceable(m) || !is_bounding_box_traceable(render->GetBoundingBox()))
                {
                    invalid_transforms++;
                    if (!first_invalid_name)
                    {
                        first_invalid_name = entity->GetObjectName().c_str();
                    }
                    continue;
                }

                instance.transform[0]  = m.m00; instance.transform[1]  = m.m10; instance.transform[2]  = m.m20; instance.transform[3]  = m.m30;
                instance.transform[4]  = m.m01; instance.transform[5]  = m.m11; instance.transform[6]  = m.m21; instance.transform[7]  = m.m31;
                instance.transform[8]  = m.m02; instance.transform[9]  = m.m12; instance.transform[10] = m.m22; instance.transform[11] = m.m32;

                instances.push_back(instance);

                Sb_GeometryInfo geo_info = {};
                geo_info.vertex_offset  = render->GetVertexOffset(0);
                geo_info.index_offset   = render->GetIndexOffset(0);
                fill_uv_draw_fields_from_render(geo_info, render);
                geometry_infos.push_back(geo_info);
            }
    
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
                if (instances.size() != last_instance_count)
                {
                    SP_LOG_INFO("Ray tracing: building TLAS with %zu instances", instances.size());
                    last_instance_count = static_cast<uint32_t>(instances.size());
                }
                m_tlas->BuildTopLevel(instances);

                // this is a full overwrite of the table, not a ring push, and the hit shaders read it at
                // the offset it was bound with. without the rewind every rebuild walked the offset one
                // stride further until the write ran off the end of the buffer, and in the meantime the
                // passes bound at zero and read whatever an earlier rebuild had left there
                geometry_info_buffer->ResetOffset();
                geometry_info_buffer->Update(geometry_infos.data(), static_cast<uint32_t>(geometry_infos.size() * sizeof(Sb_GeometryInfo)));
            }
            else if (last_instance_count != 0)
            {
                SP_LOG_INFO("Ray tracing: destroying TLAS (world changed)");
                m_tlas = nullptr;
                last_instance_count = 0;
            }

            RHI_CommandList::EndMarker();
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

        // every blas holds device addresses into the global buffers about to be freed, dedup by mesh since many renders share one
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

        // a request that has been read back is mid save and its staging buffer is still in use, so it has to
        // be left alone. one that is only pending has not been touched by the gpu yet and the newer request
        // is the one the caller wants, superseding it is what stops a single stranded capture from refusing
        // every screenshot taken afterwards
        if (screenshot.ready)
        {
            SP_LOG_WARNING(
                "Secondary screenshot is still being written, try again"
            );
            return false;
        }
        if (screenshot.pending && !screenshot.secondary_view)
        {
            SP_LOG_WARNING(
                "A main view screenshot is pending, try again"
            );
            return false;
        }

        screenshot =
            make_screenshot_request(file_path, true);
        return true;
    }

    void Renderer::Pass_Screenshot(RHI_Texture* tex_pre_tonemap)
    {
        {
            lock_guard<mutex> lock(screenshot_mutex);
            if (
                !screenshot.pending ||
                screenshot.ready ||
                screenshot.secondary_view ||
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
            return;
        }

        RHI_CommandList::BeginMarker("screenshot_sdr");
        {
            Pass_Tonemap(tex_pre_tonemap, tex_sdr, true);

            RHI_Texture* tex_in  = tex_sdr;
            RHI_Texture* tex_out = tex_ping;
            Pass_PostProcess_DisplayEffects(tex_in, tex_out, false);

            if (tex_in != tex_sdr)
            {
                RHI_CommandList::Copy(tex_in, tex_sdr, false);
            }

            if (!secondary_render_root_active)
            {
                Pass_PostProcess_EditorOverlays(tex_sdr);
                Pass_Text(tex_sdr);
            }
        }
        RHI_CommandList::EndMarker();

        lock_guard<mutex> lock(screenshot_mutex);
        if (screenshot.pending)
        {
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
                screenshot.secondary_view ||
                secondary_render_root_active
            )
            {
                return;
            }
        }

        RHI_Texture* tex_stereo = GetRenderTarget(Renderer_RenderTarget::frame_output_stereo);
        RHI_Texture* tex_sdr    = GetRenderTarget(Renderer_RenderTarget::screenshot_sdr);
        if (!tex_stereo || !tex_sdr)
        {
            return;
        }

        // stereo buffer is already tonemapped per eye, copy left eye for an hmd matched debug shot
        RHI_CommandList::BeginMarker("screenshot_xr");
        {
            RHI_CommandList::SetShader(GetShader(Renderer_Shader::blit_c), "screenshot_xr_left");
            RHI_CommandList::SetTexture(static_cast<uint32_t>(Renderer_BindingsUav::tex), tex_sdr, rhi_all_mips, 0, true);
            RHI_CommandList::SetTexture(static_cast<uint32_t>(Renderer_BindingsSrv::tex), tex_stereo, rhi_all_mips, 0, 0);
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

    void Renderer::FinalizeScreenshotReadback()
    {
        screenshot_request request;
        {
            lock_guard<mutex> lock(screenshot_mutex);
            if (!screenshot.ready)
            {
                return;
            }

            request    = screenshot;
            screenshot = {};
        }

        // present submitted the capture work, wait so the readback sees finished pixels
        RHI_Device::QueueWaitAll(true);

        RHI_Texture* tex_sdr =
            request.secondary_view
                ? secondary_view_output.get()
                : GetRenderTarget(
                    Renderer_RenderTarget::screenshot_sdr
                );
        if (!tex_sdr)
        {
            return;
        }

        shared_ptr<RHI_Buffer> sdr_staging = copy_texture_to_staging(tex_sdr);
        shared_ptr<RHI_Buffer> exr_staging;
        if (request.save_exr && !request.secondary_view && !Xr::IsSessionRunning())
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
        const float cloud_coverage       = has_directional_light ? directional_light->GetCloudCoverage() : 0.0f;
        const Vector3 wind               = World::GetWind();
        const double expected_time       = m_pass_state.cloud_time + static_cast<double>(m_cb_frame_cpu.delta_time);
        const bool time_discontinuous    = !m_pass_state.sky_first_frame && abs(m_cb_frame_cpu.time - expected_time) > 0.25;
        const bool camera_teleported     = (m_cb_frame_cpu.camera_position - m_cb_frame_cpu.camera_position_previous).LengthSquared() > 250000.0f;
        const bool light_changed         = directional_light != m_pass_state.cloud_light ||
                                           light_rotation != m_pass_state.cloud_light_rotation ||
                                           abs(light_intensity - m_pass_state.cloud_light_intensity) > 0.01f ||
                                           abs(cloud_coverage - m_pass_state.cloud_coverage) > 0.001f;
        const bool wind_changed          = (wind - m_pass_state.cloud_wind).LengthSquared() > 0.0001f;
        const bool cloud_state_changed   = m_pass_state.sky_first_frame || light_changed || wind_changed || time_discontinuous || camera_teleported;
        m_pass_state.sky_state_changed_this_frame =
            cloud_state_changed;
        if (cloud_state_changed)
        {
            m_pass_state.sky_frames_remaining     = temporal_convergence_frames;
            m_pass_state.cloud_history.valid      = false;
            m_pass_state.cloud_environment_dirty = true;
        }
        m_pass_state.cloud_light           = directional_light;
        m_pass_state.cloud_light_rotation  = light_rotation;
        m_pass_state.cloud_light_intensity = light_intensity;
        m_pass_state.cloud_coverage        = cloud_coverage;
        m_pass_state.cloud_wind            = wind;
        m_pass_state.cloud_time            = m_cb_frame_cpu.time;

        // capture this frame's warmup status before we decrement, so Pass_Skysphere can pick
        // between the full-burst and the partial-dispatch mode on the same frame
        m_pass_state.sky_warmup_this_frame = m_pass_state.sky_frames_remaining > 0;

        // progressive average, the n-th warmup frame weighs 1/n so the first one fully replaces the panorama
        const uint32_t warmup_frame_index = temporal_convergence_frames - m_pass_state.sky_frames_remaining;
        m_pass_state.sky_warmup_blend     = 1.0f / static_cast<float>(warmup_frame_index + 1);

        if (m_pass_state.sky_frames_remaining > 0)
        {
            m_pass_state.sky_frames_remaining--;
        }

        m_pass_state.sky_first_frame           = false;
        m_pass_state.sky_had_directional_light = has_directional_light;

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
        RHI_CommandList::SetConstantBuffer(0u, GetBuffer(Renderer_Buffer::ConstantFrame));
        RHI_CommandList::SetTexture("tex_perlin", GetStandardTexture(Renderer_StandardTexture::Noise_perlin));

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

        if (s_common_bind)
        {
            SetCommonTextures(s_common_eye, s_common_ssao);
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
            RHI_CommandList::SetTexture(static_cast<uint32_t>(Renderer_BindingsSrv::tex), tex_in);
            RHI_CommandList::SetTexture(static_cast<uint32_t>(Renderer_BindingsUav::tex_uint), tex_out, rhi_all_mips, 0, true);
            RHI_CommandList::Dispatch(tex_out);
        }
        RHI_CommandList::EndPass();
    }

    void Renderer::Pass_ComputeBatchA(
        bool update_skysphere
    )
    {
        RHI_CommandList::BeginMarker("compute_batch_a");

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
            Pass_Skysphere();
        }

        RHI_CommandList::EndMarker();
    }

    void Renderer::Pass_ComputeBatchB()
    {
        RHI_CommandList::BeginMarker("compute_batch_b");
        Pass_ScreenSpaceAmbientOcclusion();
        Pass_ScreenSpaceShadows();
        Pass_RayTracedShadows();
        Pass_ReSTIR_PathTracing();
        Pass_ReSTIR_Denoising();
        RHI_CommandList::EndMarker();
    }

    void Renderer::Pass_GraphicsPhase1_Geometry()
    {
        Pass_HiZ();
        Pass_IndirectCull();
        // populate the gpu procedural grass ring before the geometry rasters that consume it
        // safe to run unconditionally, the pass early-outs when grass is disabled
        Pass_Grass_Populate();
        Pass_Depth_Prepass();
        Pass_GBuffer(false);
        Pass_MeshletVisualize();
    }

    void Renderer::Pass_GraphicsPhase2_ShadowsAndRT()
    {
        Pass_ShadowMaps();
    }

    void Renderer::ProduceFrame_PerEye(uint32_t eye, uint32_t eye_layer)
    {
        // on graphics on purpose, the graphics queue would otherwise idle for the whole of pass_light
        Pass_Fog(eye, eye_layer);
        Pass_Light(false, eye_layer);
        Pass_Light_Composition(false, eye_layer);

        const bool clouds_prepared = Pass_Clouds_Prepare(eye_layer);
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
        Pass_Reflections_Trace(eye_layer);

        Pass_Light_Ibl(eye_layer);
        Pass_Reflections_Shade(eye_layer);
        Pass_Reflections_Denoise(eye_layer);

        Pass_Reflections_Apply(eye_layer);
        Pass_LightFlares(eye_layer);
        if (clouds_prepared)
        {
            const bool xr_stereo = Xr::IsSessionRunning() && Xr::GetStereoMode();
            Pass_Clouds(eye_layer, !xr_stereo || eye == Xr::eye_count - 1);
        }

        // particles remain foreground content and composite after world space clouds
        if (eye == 0)
        {
            Pass_Particles();
        }

        Pass_AA_Upscale(eye_layer);
        Pass_PostProcess(eye_layer);

        if (Xr::IsSessionRunning() && Xr::GetStereoMode())
        {
            RHI_CommandList::BlitToArrayLayer(
                GetRenderTarget(Renderer_RenderTarget::frame_output),
                GetRenderTarget(Renderer_RenderTarget::frame_output_stereo),
                eye
            );
        }
    }

    void Renderer::ProduceFrame()
    {
        SP_PROFILE_CPU();

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
        RHI_Work uploads = RHI_Device::Submit(RHI_Frame_List::Graphics, nullptr, false);

        RHI_Device::Bind(RHI_Frame_List::ComputeA);
        Pass_ComputeBatchA(update_skysphere);
        RHI_Work batch_a = RHI_Device::Submit(
            RHI_Frame_List::ComputeA,
            nullptr,
            false,
            nullptr,
            uploads.timeline,
            uploads.value
        );
        RHI_SyncPrimitive* batch_a_timeline = batch_a.timeline;
        const uint64_t batch_a_value        = batch_a.value;
        m_cross_queue_sync.pending_compute_timeline       = batch_a_timeline;
        m_cross_queue_sync.pending_compute_timeline_value = batch_a_value;
        RHI_Device::SetFrameWait(batch_a_timeline, batch_a_value);

        RHI_Device::Bind(RHI_Frame_List::Graphics);
        Pass_WindField();
        Pass_Ocean();

        if (Camera* camera = World::GetCamera())
        {
            Pass_VariableRateShading();
            Pass_GraphicsPhase1_Geometry();

            RHI_Work phase1 = RHI_Device::Submit(RHI_Frame_List::Graphics, nullptr, false);

            RHI_Device::Bind(RHI_Frame_List::ComputeB);
            Pass_ComputeBatchB();
            RHI_Work batch_b = RHI_Device::Submit(
                RHI_Frame_List::ComputeB,
                nullptr,
                false,
                nullptr,
                phase1.timeline,
                phase1.value
            );
            RHI_SyncPrimitive* compute_b_timeline = batch_b.timeline;
            const uint64_t compute_b_value        = batch_b.value;

            const bool ray_traced_shadows =
                cvar_ray_traced_shadows.GetValueAs<bool>();
            const bool tlas_available =
                RHI_Device::IsSupportedRayTracing() &&
                GetTopLevelAccelerationStructure() != nullptr &&
                !IsSecondaryViewActive();
            const bool shadow_maps_required =
                World::GetLightCount() > 0 &&
                !(ray_traced_shadows && tlas_available);
            RHI_Device::Bind(RHI_Frame_List::Graphics);
            if (shadow_maps_required)
            {
                Pass_GraphicsPhase2_ShadowsAndRT();
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

            m_cross_queue_sync.pending_compute_timeline       = compute_b_timeline;
            m_cross_queue_sync.pending_compute_timeline_value = compute_b_value;
            RHI_Device::SetFrameWait(compute_b_timeline, compute_b_value);

            RHI_Texture* tex_exposure_previous =
                GetRenderTarget(Renderer_RenderTarget::auto_exposure_previous);
            const bool auto_exposure_enabled =
                camera->GetExposureMode() == CameraExposureMode::automatic;
            m_pass_state.exposure_history_reset =
                !secondary_render_root_active &&
                auto_exposure_enabled &&
                (
                    camera != m_pass_state.exposure_camera ||
                    tex_exposure_previous !=
                    m_pass_state.exposure_history_texture ||
                    !m_pass_state.exposure_was_automatic
                );
            if (m_pass_state.exposure_history_reset)
            {
                RHI_CommandList::ClearTexture(
                    tex_exposure_previous,
                    Color::standard_black
                );
            }

            const bool xr_stereo     = Xr::IsSessionRunning() && Xr::GetStereoMode();
            const uint32_t eye_count = xr_stereo ? Xr::eye_count : 1;
            for (uint32_t eye = 0; eye < eye_count; eye++)
            {
                const uint32_t eye_layer = xr_stereo ? eye : rhi_all_mips;
                m_pcb_pass_cpu.eye_index = xr_stereo ? eye : 0;
                ProduceFrame_PerEye(eye, eye_layer);
            }

            if (xr_stereo)
            {
                Pass_ScreenshotXr();
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
            Pass_Text(rt_output);
        }

        if (!secondary_render_root_active)
        {
            Pass_ReSTIR_SwapGBufferHistory();
        }
    }
}
