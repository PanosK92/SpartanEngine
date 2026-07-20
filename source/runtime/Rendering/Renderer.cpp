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
#include "Renderer.h"
#include "Material.h"
#include "GeometryBuffer.h"
#include "ThreadPool.h"
#include "../Profiling/RenderDoc.h"
#include "../Profiling/Profiler.h"
#include "../Core/Debugging.h"
#include "../Core/Window.h"
#include "../Core/Timer.h"
#include "../FileSystem/FileSystem.h"
#include "../Input/Input.h"
#include "../Display/Display.h"
#include "../RHI/RHI_Device.h"
#include "../RHI/RHI_SwapChain.h"
#include "../RHI/RHI_Queue.h"
#include "../RHI/RHI_Implementation.h"
#include "../RHI/RHI_Buffer.h"
#include "../RHI/RHI_Shader.h"
#include "../RHI/RHI_VendorTechnology.h"
#include "../RHI/RHI_AccelerationStructure.h"
#include "../World/Entity.h"
#include "../World/Components/Light.h"
#include "../World/Components/Camera.h"
#include "../World/Components/Volume.h"
#include "../World/Components/Render.h"
#include "../World/Components/Water.h"
#include "../Core/ProgressTracker.h"
#include "../Math/Rectangle.h"
#include "../Resource/Import/ImageImporter.h"
#include "../Commands/Console/ConsoleCommands.h"
#include "../Profiling/Breadcrumbs.h"
#include "../XR/Xr.h"
//==============================================

//= NAMESPACES ===============
using namespace std;
using namespace spartan::math;
//============================

namespace spartan
{
    // constant and push constant buffers
    Cb_Frame Renderer::m_cb_frame_cpu;
    Pcb_Pass Renderer::m_pcb_pass_cpu;
    math::Matrix Renderer::m_view_projection_previous_right           = math::Matrix::Identity;
    math::Matrix Renderer::m_view_projection_previous_unjittered_left = math::Matrix::Identity;
    Renderer::PassState Renderer::m_pass_state;

    // bindless draw data
    array<Sb_DrawData, renderer_max_draw_calls> Renderer::m_draw_data_cpu;
    uint32_t Renderer::m_draw_data_count = 0;
    bool Renderer::m_draw_data_gpu_synced = false;

    // per-frame rotated buffers
    array<Renderer::FrameResource, renderer_draw_data_buffer_count> Renderer::m_frame_resources;
    uint32_t Renderer::m_frame_resource_index = 0;
    uint32_t Renderer::m_cpu_indirect_draw_arg_count = 0;

    // line and icon rendering
    shared_ptr<RHI_Buffer> Renderer::m_lines_vertex_buffer;
    vector<RHI_Vertex_PosCol> Renderer::m_lines_vertices;
    vector<PersistentLine> Renderer::m_persistent_lines;
    vector<tuple<RHI_Texture*, math::Vector3>> Renderer::m_icons;

    // misc
    uint32_t Renderer::m_frame_cb_ring_slot        = 0;
    atomic<bool> Renderer::m_initialized_resources = false;
    bool Renderer::m_transparents_present          = false;
    bool Renderer::m_is_hiz_suppressed             = false;
    bool Renderer::m_taau_reset_history            = true;
    bool Renderer::m_bindless_samplers_dirty       = true;
    RHI_CommandList* Renderer::m_cmd_list_present  = nullptr;
    RHI_CommandList* Renderer::m_cmd_list_compute  = nullptr;
    Renderer::CrossQueueSync Renderer::m_cross_queue_sync;
    vector<ShadowSlice> Renderer::m_shadow_slices;
    array<RHI_Texture*, rhi_max_array_size> Renderer::m_bindless_textures;
    array<Sb_Light, rhi_max_array_size> Renderer::m_bindless_lights;
    array<Sb_Aabb, rhi_max_array_size> Renderer::m_bindless_aabbs;
    unique_ptr<RHI_AccelerationStructure> Renderer::m_tlas;
    uint32_t Renderer::m_count_active_lights    = 0;
    uint32_t Renderer::m_volumetric_light_count = 0;

    math::Vector2             Renderer::m_resolution_render = math::Vector2::Zero;
    math::Vector2             Renderer::m_resolution_output = math::Vector2::Zero;
    RHI_Viewport              Renderer::m_viewport          = RHI_Viewport(0, 0, 0, 0);
    shared_ptr<RHI_SwapChain> Renderer::m_swapchain;
    uint64_t                  Renderer::m_frame_num         = 0;
    math::Vector2             Renderer::m_jitter_offset     = math::Vector2::Zero;

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
        };

        mutex screenshot_mutex;
        screenshot_request screenshot;
        uint32_t screenshot_index = 0;

        float sanitize_resolution_scale(float scale)
        {
            return clamp(scale, 0.5f, 1.0f);
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
                cmd_list->CopyTextureToBuffer(texture, staging.get());
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

        screenshot_request make_screenshot_request(const string& file_path)
        {
            screenshot_request request;
            request.file_path = file_path;
            request.pending   = true;

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

        // pack the renderable's resolved uv state into any struct that exposes the standard uv fields
        // raster and ray tracing both call this so they always agree on per-renderable uv overrides
        template<typename T>
        void fill_uv_draw_fields_from_renderable(T& out, const Render* renderable)
        {
            if (renderable)
            {
                out.uv_tiling      = math::Vector2(renderable->ResolveUvTilingX(), renderable->ResolveUvTilingY());
                out.uv_offset      = math::Vector2(renderable->ResolveUvOffsetX(), renderable->ResolveUvOffsetY());
                out.uv_invert      = math::Vector2(renderable->ResolveUvInvertX(), renderable->ResolveUvInvertY());
                out.uv_rotation    = renderable->ResolveUvRotation();
                out.uv_world_space = renderable->ResolveUvWorldSpace();
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

        m_swapchain = make_shared<RHI_SwapChain>
        (
            Window::GetHandleSDL(),
            Window::GetWidthInPixels(),
            Window::GetHeightInPixels(),
            cvar_vsync.GetValueAs<bool>() ? RHI_Present_Mode::Fifo : RHI_Present_Mode::Immediate,
            swap_chain_buffer_count,
            Display::GetHdr(),
            "renderer"
        );
        ConsoleRegistry::Get().SetValueFromString("r.hdr", m_swapchain->IsHdr() ? "1" : "0");

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
            16u * 1024u          // instances
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
            m_swapchain           = nullptr;
            m_lines_vertex_buffer = nullptr;
            m_tlas                = nullptr;
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
        Profiler::FrameStart();

        // process deferred fullscreen toggle at a safe point with no command lists in flight
        if (Window::IsFullScreenTogglePending())
        {
            RHI_Device::QueueWaitAll();
            Window::ProcessFullScreenToggle();
        }

        m_swapchain->AcquireNextImage();
        RHI_Device::Tick(m_frame_num);
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

        // prevent write after present hazards when idle, skip first frame
        if (!can_render && m_frame_num > 0)
        {
            RHI_Device::GetQueue(RHI_Queue_Type::Graphics)->Wait();
        }

        m_cmd_list_present = RHI_Device::GetQueue(RHI_Queue_Type::Graphics)->NextCommandList();
        m_cmd_list_present->Begin();

        m_cmd_list_compute = nullptr;
        if (can_render)
        {
            m_cmd_list_compute = RHI_Device::GetQueue(RHI_Queue_Type::Compute)->NextCommandList();
            m_cmd_list_compute->Begin();
        }

        m_draw_data_count      = 0;
        m_draw_data_gpu_synced = false;

        if (can_render)
        {
            TickUpdateHiZSuppressionState();

            // rebuild geometry buffer when meshes arrive, growth routes old buffers through the deletion queue
            GeometryBuffer::BuildIfDirty();

            // geometry buffer rebuild invalidates blas device addresses, free old gpu memory before rebuilding to avoid a peak
            if (GeometryBuffer::WasRebuilt())
            {
                DestroyAccelerationStructures();
                RHI_Device::DeletionQueueParse();
            }

            RotateFrameBuffers();
            UpdateDrawCalls(m_cmd_list_present);

            // frame based retirement, no gpu stall required
            if (RHI_Device::DeletionQueueNeedsToParse())
            {
                RHI_Device::DeletionQueueParse();
            }

            TickAdvanceFrameConstantBufferRing();
            TickUploadBindlessDependencies(m_cmd_list_present);

            UpdatePersistentLines();
            AddLinesToBeRendered();
        }

        // xrBeginFrame must precede UpdateFrameConstantBuffer so per eye matrices reflect this frame's predicted pose, paired with xrEndFrame below
        bool xr_should_render = false;
        if (Xr::IsSessionRunning())
        {
            xr_should_render = Xr::BeginFrame();
        }

        if (can_render)
        {
            UpdateFrameConstantBuffer(m_cmd_list_present);
            ProduceFrame(m_cmd_list_present, m_cmd_list_compute);
        }

        if (xr_should_render && can_render)
        {
            RHI_Texture* stereo_output = GetRenderTarget(Renderer_RenderTarget::frame_output_stereo);
            BlitToXrSwapchain(m_cmd_list_present, stereo_output ? stereo_output : GetRenderTarget(Renderer_RenderTarget::frame_output));
        }

        if (Xr::IsSessionRunning())
        {
            Xr::EndFrame();
        }

        const bool is_standalone = !Engine::IsFlagSet(EngineMode::EditorVisible);
        if (is_standalone && can_render)
        {
            BlitToBackBuffer(m_cmd_list_present, GetRenderTarget(Renderer_RenderTarget::frame_output));
        }
        if (is_standalone)
        {
            SubmitAndPresent();
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

    void Renderer::TickUploadBindlessDependencies(RHI_CommandList* cmd_list)
    {
        // run during loading so newly published entities pick up materials and lights as they arrive
        const bool initialize     = GetFrameNumber() == 0;
        const bool lights_changed = initialize || World::HaveLightsChanged();

        if (lights_changed)
        {
            UpdateShadowAtlas();
        }

        // frustum and draw distance membership depend on the camera, rebuild every frame
        UpdateLights(cmd_list);
        if (lights_changed)
        {
            RHI_Device::UpdateBindlessLights(GetBuffer(Renderer_Buffer::LightParameters));
        }

        const bool materials_changed = initialize || World::HaveMaterialsChangedThisFrame();
        if (materials_changed)
        {
            UpdateMaterials(cmd_list);
            cmd_list->PrepareTexturesForSampling(&m_bindless_textures);
            RHI_Device::UpdateBindlessMaterials(cmd_list, &m_bindless_textures, GetBuffer(Renderer_Buffer::MaterialParameters));
        }

        if (m_bindless_samplers_dirty)
        {
            RHI_Device::UpdateBindlessSamplers(&Renderer::GetSamplers());
            m_bindless_samplers_dirty = false;
        }

        // aabbs change every frame with entity transforms
        UpdateBoundingBoxes(cmd_list);
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
                cmd_list->UpdateBuffer(buffer, frame_byte_offset, upload_size, &m_draw_data_cpu[0]);
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
        args_buffer->Update(cmd_list, &draw_args[0], sizeof(draw_args));

        // single slot indirect dispatch args for triangle cull, group_count_x bumped by the meshlet cull
        Sb_IndirectDispatchArgs dispatch_args = {};
        dispatch_args.group_count_y           = 1;
        dispatch_args.group_count_z           = 1;
        RHI_Buffer* dispatch_args_buffer      = GetBuffer(Renderer_Buffer::TriangleDispatchArgs);
        dispatch_args_buffer->ResetOffset();
        dispatch_args_buffer->Update(cmd_list, &dispatch_args, sizeof(Sb_IndirectDispatchArgs));

        // single slot indirect dispatch args for the meshlet cull (phase b), group_count_x bumped by the instance cull (phase a)
        Sb_IndirectDispatchArgs instance_dispatch = {};
        instance_dispatch.group_count_y          = 1;
        instance_dispatch.group_count_z          = 1;
        RHI_Buffer* instance_dispatch_buffer     = GetBuffer(Renderer_Buffer::InstanceDispatchArgs);
        instance_dispatch_buffer->ResetOffset();
        instance_dispatch_buffer->Update(cmd_list, &instance_dispatch, sizeof(Sb_IndirectDispatchArgs));

        if (m_indirect_draw_count > 0)
        {
            RHI_Buffer* data_buffer = GetBuffer(Renderer_Buffer::IndirectDrawData);
            data_buffer->ResetOffset();
            data_buffer->Update(cmd_list, &m_indirect_draw_data[0], data_buffer->GetStride() * m_indirect_draw_count);
        }

        if (m_cull_task_count > 0)
        {
            RHI_Buffer* tasks_buffer = GetBuffer(Renderer_Buffer::CullTasks);
            tasks_buffer->ResetOffset();
            tasks_buffer->Update(cmd_list, &m_cull_tasks[0], tasks_buffer->GetStride() * m_cull_task_count);
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
            m_pass_state.cloud_history_valid     = false;
            m_pass_state.cloud_environment_dirty = true;
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
        m_pass_state.cloud_history_valid     = false;
        m_pass_state.cloud_environment_dirty = true;
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
        m_cb_frame_cpu.restir_pt_w_clamp     = std::max(cvar_restir_pt_w_clamp.GetValue(), 100.0f);
        // 0 = sdr, 1 = hdr10 pq (vulkan), 2 = hdr scrgb (d3d12 windowed)
        m_cb_frame_cpu.hdr_enabled = 0.0f;
        if (m_swapchain && m_swapchain->IsHdr())
        {
            m_cb_frame_cpu.hdr_enabled = (m_swapchain->GetFormat() == RHI_Format::R16G16B16A16_Float) ? 2.0f : 1.0f;
        }
        m_cb_frame_cpu.hdr_max_nits      = Display::GetLuminanceMax();
        m_cb_frame_cpu.hdr_sdr_white_nits = Display::GetSdrWhiteNits();
        m_cb_frame_cpu.gamma                 = cvar_gamma.GetValue();
        m_cb_frame_cpu.camera_exposure       = World::GetCamera() ? World::GetCamera()->GetExposure() : 1.0f;
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
        const bool tlas_available = RHI_Device::IsSupportedRayTracing() && GetTopLevelAccelerationStructure() != nullptr;
        m_cb_frame_cpu.set_bit(cvar_ray_traced_reflections.GetValueAs<bool>(),               1 << 0);
        m_cb_frame_cpu.set_bit(cvar_ssao.GetValueAs<bool>(),                                 1 << 1);
        m_cb_frame_cpu.set_bit(cvar_ray_traced_shadows.GetValueAs<bool>() && tlas_available, 1 << 2);
        m_cb_frame_cpu.set_bit(cvar_restir_pt.GetValueAs<bool>(),                            1 << 3);
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

        for (Entity* entity : World::GetEntitiesRenderables())
        {
            if (count >= 8)
            {
                break;
            }

            Render* renderable = entity->GetComponent<Render>();
            if (!renderable)
            {
                continue;
            }

            Material* material = renderable->GetMaterial();
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

            const math::BoundingBox& aabb = renderable->GetBoundingBox();
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

    void Renderer::UpdateFrameConstantBuffer(RHI_CommandList* cmd_list)
    {
        UpdateFrameCb_CameraAndProjectionHistory();
        UpdateFrameCb_ProjectionJitter();
        UpdateFrameCb_ViewProjectionAndCameraFields();
        UpdateFrameCb_ScalarFields();
        UpdateFrameCb_ClusterLighting();
        UpdateFrameCb_FeatureBits();
        UpdateFrameCb_StereoXr();
        UpdateFrameCb_RadialBlurHubs();

        // emissive triangle nee pool, must precede the cb upload because it writes the count
        // into m_cb_frame_cpu, the buffer upload itself piggybacks on the same cmd_list
        BuildEmissiveTriangleNeePool(cmd_list);

        GetBuffer(Renderer_Buffer::ConstantFrame)->Update(cmd_list, &m_cb_frame_cpu);
    }

    void Renderer::BuildEmissiveTriangleNeePool(RHI_CommandList* cmd_list)
    {
        // skip when restir off, the buffer stays at whatever data it had previously, the count
        // is set to zero so the shader treats the pool as empty regardless of buffer contents
        if (!cvar_restir_pt.GetValueAs<bool>())
        {
            m_cb_frame_cpu.restir_pt_emissive_tri_count = 0.0f;
            return;
        }

        // statics avoid per frame heap thrash, the vectors are reused across frames and the
        // capacity ratchets up to the largest emissive renderable seen so far
        static vector<Sb_EmissiveTriangle>      tris;
        static vector<uint32_t>                 indices;
        static vector<RHI_Vertex_PosTexNorTan>  vertices;
        tris.clear();
        bool truncated = false;

        for (Entity* entity : World::GetEntitiesRenderables())
        {
            Render* renderable = entity->GetComponent<Render>();
            if (!renderable)
            {
                continue;
            }

            Material* material = renderable->GetMaterial();
            if (!material)
            {
                continue;
            }

            // emission test, accept either the synthetic albedo->emission path or an explicit
            // emission texture, the radiance estimate uses the material base color which is
            // the same heuristic used by probe_emission_estimate in the existing path tracer
            // threshold matches the bit 15 flag set in UpdateMaterials, any nonzero strength counts
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
            renderable->GetGeometry(&indices, &vertices);
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
                cmd_list,
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

    void Renderer::EnableProceduralGrass(Mesh* grass_mesh, Material* grass_material, RHI_Texture* terrain_heightmap, const ProceduralGrassParams& params)
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
        m_pass_state.grass_params    = params;
        m_pass_state.grass_enabled   = true;

        // derive WorldWidth and WorldHeight from the lod0 vertex bounds, the grass blade vs uses
        // these to compute width_percent and height_percent for the camera-bias and wind logic.
        // normal renderables get this done by Render::SetMaterial, but procedural grass has no Render component,
        // without it the vs divides by zero on width and gets a saturated zero on height, the blade collapses to a point
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
    }

    void Renderer::DisableProceduralGrass()
    {
        m_pass_state.grass_enabled    = false;
        m_pass_state.grass_mesh       = nullptr;
        m_pass_state.grass_material   = nullptr;
        m_pass_state.grass_heightmap  = nullptr;
        m_pass_state.grass_args_baked = false;
    }

    bool Renderer::IsProceduralGrassEnabled()
    {
        return m_pass_state.grass_enabled;
    }

    void Renderer::EnableOcean(Water* water)
    {
        m_pass_state.ocean                = water;
        m_pass_state.ocean_spectrum_dirty = true; // re-seed the spectrum whenever parameters change
    }

    void Renderer::DisableOcean()
    {
        m_pass_state.ocean = nullptr;
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

    RHI_SwapChain* Renderer::GetSwapChain()
    {
        return m_swapchain.get();
    }

    void Renderer::BlitToBackBuffer(RHI_CommandList* cmd_list, RHI_Texture* texture)
    {
        cmd_list->BeginMarker("blit_to_back_buffer");
        cmd_list->Blit(texture, m_swapchain.get());
        cmd_list->EndMarker();
    }

    void Renderer::BlitToXrSwapchain(RHI_CommandList* cmd_list, RHI_Texture* texture)
    {
        cmd_list->BeginMarker("blit_to_xr_swapchain");
        cmd_list->BlitToXrSwapchain(texture);
        cmd_list->EndMarker();
    }

    void Renderer::SubmitAndPresent()
    {
        Profiler::TimeBlockStart("submit_and_present", TimeBlockType::Cpu, nullptr);
        {
            SP_ASSERT(m_cmd_list_present->GetState() == RHI_CommandListState::Recording);

            if (m_swapchain->IsImageAcquired())
            {
                m_cmd_list_present->RenderPassEnd();
                m_cmd_list_present->PrepareForPresent(m_swapchain.get());

                m_cmd_list_present->Submit(
                    m_swapchain->GetImageAcquiredSemaphore(),
                    false,
                    m_swapchain->GetRenderingCompleteSemaphore(),
                    m_cross_queue_sync.pending_compute_timeline,
                    m_cross_queue_sync.pending_compute_timeline_value);
                m_swapchain->Present(m_cmd_list_present);
            }
            else
            {
                m_cmd_list_present->Submit(
                    nullptr,
                    true,
                    nullptr,
                    m_cross_queue_sync.pending_compute_timeline,
                    m_cross_queue_sync.pending_compute_timeline_value);
            }

            m_cross_queue_sync.pending_compute_timeline       = nullptr;
            m_cross_queue_sync.pending_compute_timeline_value = 0;
        }
        Profiler::TimeBlockEnd(TimeBlockType::Cpu);

        FinalizeScreenshotReadback();
    }

    RHI_Api_Type Renderer::GetRhiApiType()
    {
        return RHI_Context::api_type;
    }

    uint64_t Renderer::GetFrameNumber()
    {
        return m_frame_num;
    }

    void Renderer::SetCommonTextures(RHI_CommandList* cmd_list, uint32_t eye_layer /*= rhi_all_mips*/, bool bind_ssao /*= true*/)
    {
        // gbuffer (when eye_layer is specified, bind per-layer 2d views for compute passes)
        cmd_list->SetTexture(static_cast<uint32_t>(Renderer_BindingsSrv::gbuffer_color),    GetRenderTarget(Renderer_RenderTarget::gbuffer_color),    rhi_all_mips, 0, false, eye_layer);
        cmd_list->SetTexture(static_cast<uint32_t>(Renderer_BindingsSrv::gbuffer_normal),   GetRenderTarget(Renderer_RenderTarget::gbuffer_normal),   rhi_all_mips, 0, false, eye_layer);
        cmd_list->SetTexture(static_cast<uint32_t>(Renderer_BindingsSrv::gbuffer_material), GetRenderTarget(Renderer_RenderTarget::gbuffer_material), rhi_all_mips, 0, false, eye_layer);
        cmd_list->SetTexture(static_cast<uint32_t>(Renderer_BindingsSrv::gbuffer_velocity), GetRenderTarget(Renderer_RenderTarget::gbuffer_velocity), rhi_all_mips, 0, false, eye_layer);
        cmd_list->SetTexture(static_cast<uint32_t>(Renderer_BindingsSrv::gbuffer_depth),    GetRenderTarget(Renderer_RenderTarget::gbuffer_depth),    rhi_all_mips, 0, false, eye_layer);

        // ssao is written on async compute, skip binding it during graphics phase 2
        if (bind_ssao)
        {
            RHI_Texture* tex_ssao = GetRenderTarget(Renderer_RenderTarget::ssao);
            cmd_list->SetTexture(Renderer_BindingsSrv::ssao, tex_ssao ? tex_ssao : GetStandardTexture(Renderer_StandardTexture::White));
        }
    }

    uint32_t Renderer::WriteDrawData(const math::Matrix& transform, const math::Matrix& transform_previous, uint32_t material_index, uint32_t is_transparent, const Render* renderable)
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

        fill_uv_draw_fields_from_renderable(entry, renderable);

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
            if (RHI_CommandList* cmd_list = GetCommandListPresent())
            {
                if (cmd_list->GetState() == RHI_CommandListState::Recording)
                {
                    cmd_list->UpdateBuffer(buffer, global_index * sizeof(Sb_DrawData), sizeof(Sb_DrawData), &entry);
                }
            }
        }

        return global_index;
    }

    void Renderer::UpdateMaterials(RHI_CommandList* cmd_list)
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
                // it is per-renderable and lives on Sb_DrawData (see WriteDrawData) and Sb_GeometryInfo for rt
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
            for (Entity* entity : World::GetEntitiesRenderables())
            {
                Render* renderable = entity->GetComponent<Render>();
                if (!renderable)
                {
                    continue;
                }

                if (Material* material = renderable->GetMaterial())
                {
                    update_material(material);
                }
            }
        };
    
        properties.fill(Sb_Material{});
        m_bindless_textures.fill(nullptr);
        unique_material_ids.clear();
        update_entities();

        // procedural grass material is not attached to any entity, register it here so it lands in the
        // bindless table and material_index can be pushed into the grass raster passes via the push constant
        if (m_pass_state.grass_enabled && m_pass_state.grass_material)
        {
            update_material(m_pass_state.grass_material);
        }

        RHI_Buffer* buffer = Renderer::GetBuffer(Renderer_Buffer::MaterialParameters);
        buffer->ResetOffset();
        buffer->Update(cmd_list, &properties[0], buffer->GetStride() * count);
    }

    void Renderer::UpdateLights(RHI_CommandList* cmd_list)
    {
        m_bindless_lights.fill(Sb_Light());

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

            // build the compact volumetric index list, the light shader scans this instead of every light
            // slot 0 is the directional sun which is evaluated unconditionally in the first evaluate_light call,
            // including it here would double count its volumetric contribution
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
        for (Entity* entity : World::GetEntitiesLights())
        {
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

        for (Entity* entity : World::GetEntitiesLights())
        {
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
    
        // gpu upload
        RHI_Buffer* buffer = GetBuffer(Renderer_Buffer::LightParameters);
        buffer->ResetOffset();
        
        if (m_count_active_lights > 0)
        {
            buffer->Update(cmd_list, &m_bindless_lights[0], buffer->GetStride() * m_count_active_lights);
        }

        // upload the compact volumetric light index list, count flows through buffer_frame.volumetric_light_count
        m_volumetric_light_count = volumetric_count;
        if (volumetric_count > 0)
        {
            RHI_Buffer* vol_buffer = GetBuffer(Renderer_Buffer::VolumetricLightIndices);
            vol_buffer->ResetOffset();
            vol_buffer->Update(cmd_list, &volumetric_indices[0], vol_buffer->GetStride() * volumetric_count);
        }
    }

    void Renderer::UpdateBoundingBoxes(RHI_CommandList* cmd_list)
    {
        m_bindless_aabbs.fill(Sb_Aabb());

        // prepass aabbs come first in the buffer, slot index is the prepass draw index
        for (uint32_t i = 0; i < m_draw_calls_prepass_count; i++)
        {
            const Renderer_DrawCall& draw_call = m_draw_calls_prepass[i];
            Render* renderable                 = draw_call.renderable;
            const BoundingBox& aabb            = renderable->GetBoundingBox();
            m_bindless_aabbs[i].min            = aabb.GetMin();
            m_bindless_aabbs[i].max            = aabb.GetMax();
            m_bindless_aabbs[i].is_occluder    = draw_call.is_occluder;
        }

        // indirect draw aabbs, the slot is taken straight from m_indirect_draw_data[].aabb_index so the writes
        // here always land on the slot the cull shader will read for that draw, no filter divergence can drift it
        const uint32_t aabb_frame_offset = m_frame_resource_index * rhi_max_array_size;
        for (uint32_t i = 0; i < m_indirect_draw_count; i++)
        {
            Render* renderable               = m_indirect_renderables[i];
            const Sb_DrawData& data          = m_indirect_draw_data[i];
            const uint32_t aabb_slot_global  = data.aabb_index;
            if (aabb_slot_global < aabb_frame_offset)
            {
                continue;
            }
            const uint32_t aabb_slot = aabb_slot_global - aabb_frame_offset;
            if (aabb_slot >= rhi_max_array_size)
            {
                continue;
            }
            const BoundingBox& aabb         = renderable->GetBoundingBox();
            m_bindless_aabbs[aabb_slot].min = aabb.GetMin();
            m_bindless_aabbs[aabb_slot].max = aabb.GetMax();
        }

        // upload covers both the prepass region and the trailing indirect region, the indirect aabb slots start at m_draw_calls_prepass_count
        const uint32_t total_aabb_count = m_draw_calls_prepass_count + m_indirect_renderable_count;
        if (total_aabb_count > 0)
        {
            RHI_Buffer* buffer         = GetBuffer(Renderer_Buffer::AABBs);
            uint32_t frame_byte_offset = m_frame_resource_index * rhi_max_array_size * static_cast<uint32_t>(sizeof(Sb_Aabb));
            uint32_t upload_size       = static_cast<uint32_t>(sizeof(Sb_Aabb)) * total_aabb_count;
            cmd_list->UpdateBuffer(buffer, frame_byte_offset, upload_size, &m_bindless_aabbs[0]);
        }
    }

    void Renderer::UpdateDrawCalls_ResetCounts()
    {
        m_draw_call_count           = 0;
        m_draw_calls_prepass_count  = 0;
        m_draw_data_count           = 0;
        m_draw_data_gpu_synced      = false;
        m_indirect_draw_count       = 0;
        m_indirect_renderable_count = 0;
        m_cull_task_count           = 0;
        m_transparents_present      = false;
    }

    void Renderer::UpdateDrawCalls_CollectAndSort()
    {
        for (Entity* entity : World::GetEntitiesRenderables())
        {
            if (!entity || !entity->GetActive())
            {
                continue;
            }

            // a worker may still be assigning the Render component, the mesh or the material, so guard every step
            Render* renderable = entity->GetComponent<Render>();
            if (!renderable || !renderable->GetMesh())
            {
                continue;
            }

            Material* material = renderable->GetMaterial();
            if (!material)
            {
                continue;
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
                entity->GetMatrixPrevious(),
                material->GetIndex(),
                material->IsTransparent() ? 1 : 0,
                renderable
            );
            if (draw_data_index == numeric_limits<uint32_t>::max())
            {
                break;
            }

            Renderer_DrawCall& draw_call = m_draw_calls[m_draw_call_count++];
            draw_call.renderable         = renderable;
            draw_call.distance_squared   = renderable->GetDistanceSquared();
            draw_call.lod_index          = renderable->GetLodIndex();
            draw_call.is_occluder        = false;
            draw_call.camera_visible     = renderable->IsVisible();
            draw_call.instance_index     = 0;
            draw_call.instance_count     = renderable->GetInstanceCount();
            draw_call.draw_data_index    = draw_data_index;
        }

        // opaque before transparent, then by material id, then by distance
        sort(m_draw_calls.begin(), m_draw_calls.begin() + m_draw_call_count, [](const Renderer_DrawCall& a, const Renderer_DrawCall& b)
        {
            const bool a_transparent = a.renderable->GetMaterial()->IsTransparent();
            const bool b_transparent = b.renderable->GetMaterial()->IsTransparent();
            if (a_transparent != b_transparent)
            {
                return !a_transparent;
            }

            const uint64_t a_material_id = a.renderable->GetMaterial()->GetObjectId();
            const uint64_t b_material_id = b.renderable->GetMaterial()->GetObjectId();
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
            if (!dc.renderable->GetMaterial()->IsTransparent() && dc.camera_visible)
            {
                m_draw_calls_prepass[m_draw_calls_prepass_count++] = dc;
            }
        }

        sort(m_draw_calls_prepass.begin(), m_draw_calls_prepass.begin() + m_draw_calls_prepass_count, [](const Renderer_DrawCall& a, const Renderer_DrawCall& b)
        {
            const bool a_alpha = a.renderable->GetMaterial()->IsAlphaTested();
            const bool b_alpha = b.renderable->GetMaterial()->IsAlphaTested();
            if (a_alpha != b_alpha)
            {
                return !a_alpha;
            }
            return a.distance_squared < b.distance_squared;
        });
    }

    void Renderer::UpdateDrawCalls_BuildIndirectAndCullTasks()
    {
        // one draw entry per renderable lod, one instance cull task per (renderable, instance) tuple
        // phase a compacts visible instances, phase b expands their meshlets, the triangle cull then feeds the indirect draw
        m_indirect_draw_count       = 0;
        m_indirect_renderable_count = 0;
        m_cull_task_count           = 0;
        const uint32_t aabb_frame_offset = m_frame_resource_index * rhi_max_array_size;
        const uint32_t indirect_draw_capacity = GetBuffer(Renderer_Buffer::IndirectDrawData)->GetElementCount();
        const uint32_t cull_task_capacity = GetBuffer(Renderer_Buffer::CullTasks)->GetElementCount();
        const uint32_t meshlet_instance_capacity = GetBuffer(Renderer_Buffer::MeshletInstances)->GetElementCount();

        // diagnostics, the cull pipeline silently drops survivors once a budget is hit which manifests as
        // distant terrain, leaves or rocks failing to draw, the worst case survivor count is tracked here
        // so the one-shot log below can name the renderable that pushed the engine over the cliff
        uint64_t expected_survivors_worst_case = 0;
        uint32_t cull_task_overflow_renderables = 0;

        for (uint32_t i = 0; i < m_draw_call_count; i++)
        {
            const Renderer_DrawCall& dc = m_draw_calls[i];
            Render* renderable          = dc.renderable;
            Material* material          = renderable->GetMaterial();

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

            const uint32_t lod_index_count = renderable->GetIndexCount(dc.lod_index);
            if (lod_index_count == 0)
            {
                continue;
            }

            const uint32_t lod_meshlet_count = renderable->GetMeshletCount(dc.lod_index);
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
                cull_task_overflow_renderables++;
                continue;
            }

            // worst case survivor accounting, every instance visible and emitting all of its meshlets
            expected_survivors_worst_case += static_cast<uint64_t>(inst_n) * static_cast<uint64_t>(lod_meshlet_count);

            const uint32_t renderable_aabb_slot = aabb_frame_offset + m_draw_calls_prepass_count + m_indirect_renderable_count;
            const uint32_t base_first_index     = renderable->GetIndexOffset(dc.lod_index);
            const uint32_t vertex_offset        = renderable->GetVertexOffset(dc.lod_index);
            const uint32_t base_meshlet_index   = renderable->GetGlobalMeshletOffset() + renderable->GetMeshletOffset(dc.lod_index);

            Entity* entity = renderable->GetEntity();
            Mesh* mesh     = renderable->GetMesh();

            // flags bit 0 skinned, bit 1 per instance, bit 3 two sided material, bit 4 alpha tested (bit 2 retired with the hw-instancing fallback)
            const bool is_skinned       = mesh->IsSkinned() && !cvar_meshlet_cull_skinned.GetValueAs<bool>();
            const bool use_per_instance = is_instanced;
            const bool is_two_sided     = static_cast<RHI_CullMode>(material->GetProperty(MaterialProperty::CullMode)) != RHI_CullMode::Back;
            const bool is_alpha_tested  = material->IsAlphaTested();
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

            const uint32_t draw_idx        = m_indirect_draw_count++;
            Sb_DrawData& data              = m_indirect_draw_data[draw_idx];
            data.transform                 = entity->GetMatrix();
            data.transform_previous        = entity->GetMatrixPrevious();
            data.material_index            = material->GetIndex();
            data.is_transparent            = 0;
            data.aabb_index                = renderable_aabb_slot;
            data.lod_first_index           = base_first_index;
            data.flags                     = base_flags;
            data.instance_offset           = renderable->GetGlobalInstanceOffset();
            data.instance_index            = 0;
            data.lod_vertex_offset         = vertex_offset;
            data.lod_meshlet_offset        = base_meshlet_index;
            data.lod_meshlet_count         = lod_meshlet_count;
            fill_uv_draw_fields_from_renderable(data, renderable);

            // lod-local aabb, must match the one build_meshlets quantized the compressed meshlet bounds against
            // diag is precomputed length(extent), the cull shader uses it to dequantize radius without a sqrt
            const BoundingBox& lod_aabb_local = renderable->GetLodAabb(dc.lod_index);
            const Vector3 lod_extent          = lod_aabb_local.GetMax() - lod_aabb_local.GetMin();
            data.lod_aabb_min                 = lod_aabb_local.GetMin();
            data.lod_aabb_extent              = lod_extent;
            data.lod_aabb_diag                = lod_extent.Length();

            // per-instance distance cull on the gpu, zero disables the check (used when max_distance is FLT_MAX or non-finite)
            // squaring once on the cpu avoids a sqrt per cull task on the gpu, the cull shader compares against length squared
            // this is the gpu-side counterpart to the cpu Render::UpdateFrustumAndDistanceCulling, the cpu check uses the renderable
            // bounding box which is the world for consolidated entities, so the per-instance gpu test below is the only thing that
            // stops a 6 km world of forest props from dumping every instance into the cull pipeline regardless of artist intent
            const float max_distance          = renderable->GetMaxRenderDistance();
            const bool  finite_distance       = max_distance > 0.0f && max_distance < numeric_limits<float>::max() * 0.5f;
            data.max_render_distance_squared  = finite_distance ? (max_distance * max_distance) : 0.0f;

            // parallel renderable handle, UpdateBoundingBoxes uses this to write each aabb at exactly the slot the cull shader will read
            m_indirect_renderables[draw_idx] = renderable;

            // one instance cull task per instance, phase a tests each instance's bounds, phase b expands the survivors' meshlets
            for (uint32_t inst = 0; inst < inst_n; inst++)
            {
                Sb_CullTask& task   = m_cull_tasks[m_cull_task_count++];
                task.draw_index     = draw_idx;
                task.meshlet_index  = 0; // unused in phase a, the meshlet range lives on DrawData
                task.instance_index = inst;
                task.instance_count = 1;
            }

            m_indirect_renderable_count++;
        }

        // one-shot diagnostics, fires when the worst case survivor count exceeds the buffer or when the cull task budget rejected renderables
        // the previous failure mode was silent, the wave atomic add would clamp group_count_x and every task above the cap skipped its writes,
        // leaving the user with only whichever renderables won the atomic race drawn, both conditions are reported once per session here
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
        if (!s_logged_cull_task_overflow && cull_task_overflow_renderables > 0)
        {
            SP_LOG_WARNING(
                "instance cull task budget exhausted, %u renderable lods rejected, current capacity is %u",
                cull_task_overflow_renderables,
                cull_task_capacity
            );
            s_logged_cull_task_overflow = true;
        }
    }

    void Renderer::UpdateDrawCalls_SelectOccluders()
    {
        // top n by screen area with temporal hysteresis, the prior occluder set gets a 1.5x area bonus
        static unordered_set<Render*> previous_occluders;

        auto compute_screen_space_area = [](const BoundingBox& aabb_world) -> float
        {
            if (Camera* camera = World::GetCamera())
            {
                math::Rectangle rect_screen = camera->WorldToScreenCoordinates(aabb_world);
                return clamp(rect_screen.width * rect_screen.height, 0.0f, numeric_limits<float>::max());
            }
            return 0.0f;
        };

        struct DrawCallArea { uint32_t index; float area; };
        static vector<DrawCallArea> areas;
        areas.clear();
        areas.reserve(m_draw_calls_prepass_count);

        for (uint32_t i = 0; i < m_draw_calls_prepass_count; i++)
        {
            Renderer_DrawCall& draw_call = m_draw_calls_prepass[i];
            Render* renderable           = draw_call.renderable;
            Material* material           = renderable->GetMaterial();

            if (!material || material->IsTransparent() || renderable->HasInstancing() || !draw_call.camera_visible)
            {
                continue;
            }

            float screen_area = compute_screen_space_area(renderable->GetBoundingBox());
            if (previous_occluders.find(renderable) != previous_occluders.end())
            {
                screen_area *= 1.5f;
            }

            areas.push_back({ i, screen_area });
        }

        sort(areas.begin(), areas.end(), [](const DrawCallArea& a, const DrawCallArea& b) { return a.area > b.area; });

        const uint32_t max_occluders  = 64;
        const uint32_t occluder_count = min(max_occluders, static_cast<uint32_t>(areas.size()));

        previous_occluders.clear();
        for (uint32_t i = 0; i < occluder_count; i++)
        {
            m_draw_calls_prepass[areas[i].index].is_occluder = true;
            previous_occluders.insert(m_draw_calls_prepass[areas[i].index].renderable);
        }
    }

    void Renderer::UpdateDrawCalls(RHI_CommandList* cmd_list)
    {
        UpdateDrawCalls_ResetCounts();
        UpdateDrawCalls_CollectAndSort();
        UpdateDrawCalls_BuildPrepass();
        UpdateDrawCalls_BuildIndirectAndCullTasks();
        UpdateDrawCalls_SelectOccluders();
    }

    void Renderer::UpdateAccelerationStructures(RHI_CommandList* cmd_list)
    {
        bool ray_tracing_enabled = cvar_ray_traced_reflections.GetValueAs<bool>() || cvar_ray_traced_shadows.GetValueAs<bool>() || cvar_restir_pt.GetValueAs<bool>();
        if (!ray_tracing_enabled)
        {
            return;
        }

        if (!RHI_Device::IsSupportedRayTracing() || !cmd_list)
        {
            SP_LOG_WARNING("Ray tracing or command list invalid, skipping update");
            return;
        }

        // blas
        // built incrementally with a per-frame cap so big scenes (forest has 2148 unique meshes)
        // don't hit driver tdr or peak gpu memory by recording all builds onto one command list
        bool blas_burst_done = false;
        {
            cmd_list->BeginMarker("blas_build");

            constexpr uint32_t blas_builds_per_frame = 64;

            uint32_t blas_built     = 0;
            uint32_t blas_remaining = 0;
            uint32_t blas_total     = 0;
            for (Entity* entity : World::GetEntitiesRenderables())
            {
                if (!entity || !entity->GetActive())
                {
                    continue;
                }

                Render* renderable = entity->GetComponent<Render>();
                if (!renderable)
                {
                    continue;
                }

                // skip the ray tracing path for renderables that opt out (foliage, anything with millions of instances)
                // these never become a blas and never enter the tlas, so the big instance buffers don't drag blas memory along with them
                if (renderable->HasFlag(RenderableFlags::ExcludeFromRayTracing))
                {
                    continue;
                }

                blas_total++;

                if (!renderable->HasAccelerationStructure())
                {
                    if (blas_built < blas_builds_per_frame)
                    {
                        renderable->BuildAccelerationStructure(cmd_list);
                        if (renderable->HasAccelerationStructure())
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
                if (renderable->NeedsBlasRefit() && renderable->HasAccelerationStructure())
                {
                    renderable->RefitAccelerationStructure(cmd_list);
                    renderable->SetNeedsBlasRefit(false);
                }
            }

            blas_burst_done = (blas_remaining == 0);

            // free the shared static scratch only once the burst fully completes
            // freeing mid-burst would force a reallocation on next frame
            if (blas_burst_done && blas_built > 0)
            {
                RHI_AccelerationStructure::FreeSharedBlasScratch();
                SP_LOG_INFO("Ray tracing: BLAS build burst complete (last frame built %u, total %u)", blas_built, blas_total);
            }

            cmd_list->EndMarker();
        }

        // skip tlas build until all blas are ready so we don't keep rebuilding it with an incomplete set
        // also skip while the world is still loading, async loading adds new renderables every frame
        // which would force the instance, staging, scratch buffers and the as itself to reallocate
        // each rebuild, defer to a single build once loading completes
        if (!blas_burst_done || ProgressTracker::IsLoading())
        {
            return;
        }

        // tlas
        {
            cmd_list->BeginMarker("tlas_build");

            if (!m_tlas)
            {
                m_tlas = make_unique<RHI_AccelerationStructure>(RHI_AccelerationStructureType::Top, "world_tlas");
            }

            constexpr uint32_t RHI_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT = 0x00000002; // VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR

            static vector<RHI_AccelerationStructureInstance> instances; // static to avoid per-frame heap alloc
            static vector<Sb_GeometryInfo> geometry_infos;
            instances.clear();
            geometry_infos.clear();

            for (Entity* entity : World::GetEntitiesRenderables())
            {
                if (!entity || !entity->GetActive())
                {
                    continue;
                }

                Render* renderable = entity->GetComponent<Render>();
                if (!renderable)
                {
                    continue;
                }

                // same opt-out as the blas loop above, keep the tlas instance list in sync with the blas set so we don't try to register a renderable that has no blas
                if (renderable->HasFlag(RenderableFlags::ExcludeFromRayTracing))
                {
                    continue;
                }

                Material* material = renderable->GetMaterial();
                if (!material)
                {
                    continue;
                }

                uint64_t device_address = renderable->GetAccelerationStructureDeviceAddress();
                if (device_address == 0)
                {
                    continue;
                }

                RHI_Buffer* vertex_buffer = renderable->GetVertexBuffer();
                RHI_Buffer* index_buffer  = renderable->GetIndexBuffer();
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
                const Matrix& m = renderable->GetEntity()->GetMatrix();
                instance.transform[0]  = m.m00; instance.transform[1]  = m.m10; instance.transform[2]  = m.m20; instance.transform[3]  = m.m30;
                instance.transform[4]  = m.m01; instance.transform[5]  = m.m11; instance.transform[6]  = m.m21; instance.transform[7]  = m.m31;
                instance.transform[8]  = m.m02; instance.transform[9]  = m.m12; instance.transform[10] = m.m22; instance.transform[11] = m.m32;

                instances.push_back(instance);

                Sb_GeometryInfo geo_info = {};
                geo_info.vertex_offset  = renderable->GetVertexOffset(0);
                geo_info.index_offset   = renderable->GetIndexOffset(0);
                fill_uv_draw_fields_from_renderable(geo_info, renderable);
                geometry_infos.push_back(geo_info);
            }
    
            static uint32_t last_instance_count = 0;
            if (!instances.empty())
            {
                if (instances.size() != last_instance_count)
                {
                    SP_LOG_INFO("Ray tracing: building TLAS with %zu instances", instances.size());
                    last_instance_count = static_cast<uint32_t>(instances.size());
                }
                m_tlas->BuildTopLevel(cmd_list, instances);

                GetBuffer(Renderer_Buffer::GeometryInfo)->Update(cmd_list, geometry_infos.data(), static_cast<uint32_t>(geometry_infos.size() * sizeof(Sb_GeometryInfo)));
            }
            else if (last_instance_count != 0)
            {
                SP_LOG_INFO("Ray tracing: destroying TLAS (world changed)");
                m_tlas = nullptr;
                last_instance_count = 0;
            }

            cmd_list->EndMarker();
        }
    }

    RHI_AccelerationStructure* Renderer::GetTopLevelAccelerationStructure()
    {
        return m_tlas.get();
    }

    void Renderer::DestroyAccelerationStructures()
    {
        RHI_Device::QueueWaitAll();

        m_tlas = nullptr;

        // invalidate every blas, they hold device addresses into the previous global vertex/index buffers,
        // those buffers are about to be freed via DeletionQueueParse so leaving stale blas in place would have
        // future trace rays read freed gpu memory, dedup by mesh because many renderables share one mesh,
        // e.g. terrain has one mesh with hundreds of sub-meshes and matching renderables
        std::unordered_set<Mesh*> meshes;
        for (Entity* entity : World::GetEntitiesRenderables())
        {
            if (Render* renderable = entity->GetComponent<Render>())
            {
                if (Mesh* mesh = renderable->GetMesh())
                {
                    meshes.insert(mesh);
                }
            }
        }
        for (Mesh* mesh : meshes)
        {
            mesh->InvalidateAllBlas();
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
        for (const auto& entity : World::GetEntitiesLights())
        {
            Light* light = entity->GetComponent<Light>();
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

        // row-based packing: lays out uniform-sized slices left-to-right, wrapping to the next row.
        // when rects is null it only tests whether the layout fits; when non-null it writes the rectangles.
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
                    (*rects)[i].res  = slice_res;
                    (*rects)[i].rect = math::Rectangle(
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

    void Renderer::Pass_Screenshot(RHI_CommandList* cmd_list, RHI_Texture* tex_pre_tonemap)
    {
        {
            lock_guard<mutex> lock(screenshot_mutex);
            if (!screenshot.pending || screenshot.ready || !tex_pre_tonemap)
            {
                return;
            }
        }

        RHI_Texture* tex_sdr = GetRenderTarget(Renderer_RenderTarget::screenshot_sdr);
        RHI_Texture* tex_ping = GetRenderTarget(Renderer_RenderTarget::screenshot_sdr_2);
        if (!tex_sdr || !tex_ping)
        {
            return;
        }

        cmd_list->BeginMarker("screenshot_sdr");
        {
            Pass_Tonemap(cmd_list, tex_pre_tonemap, tex_sdr, true);

            RHI_Texture* tex_in  = tex_sdr;
            RHI_Texture* tex_out = tex_ping;
            Pass_PostProcess_DisplayEffects(cmd_list, tex_in, tex_out, false);

            if (tex_in != tex_sdr)
            {
                cmd_list->Copy(tex_in, tex_sdr, false);
            }

            Pass_PostProcess_EditorOverlays(cmd_list, tex_sdr);
            Pass_Text(cmd_list, tex_sdr);
        }
        cmd_list->EndMarker();

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

        RHI_Texture* tex_sdr = GetRenderTarget(Renderer_RenderTarget::screenshot_sdr);
        if (!tex_sdr)
        {
            return;
        }

        shared_ptr<RHI_Buffer> sdr_staging = copy_texture_to_staging(tex_sdr);
        shared_ptr<RHI_Buffer> exr_staging;
        if (request.save_exr)
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
        // best effort readback, the buffer is host visible so we read the previous frame's accumulated value
        // gpu writes commit via the cross queue timeline before the cpu samples here, on integrated/uma gpus
        // this is exact, on discrete gpus the value may lag by a frame which is fine for a debug warning
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
    array<Renderer_DrawCall, renderer_max_draw_calls> Renderer::m_draw_calls;
    uint32_t Renderer::m_draw_call_count;
    array<Renderer_DrawCall, renderer_max_draw_calls> Renderer::m_draw_calls_prepass;
    uint32_t Renderer::m_draw_calls_prepass_count;
    array<Sb_DrawData, renderer_max_indirect_draws> Renderer::m_indirect_draw_data;
    array<Render*,     renderer_max_indirect_draws> Renderer::m_indirect_renderables;
    uint32_t Renderer::m_indirect_draw_count       = 0;
    uint32_t Renderer::m_indirect_renderable_count = 0;
    array<Sb_CullTask, renderer_max_cull_tasks> Renderer::m_cull_tasks;
    uint32_t Renderer::m_cull_task_count = 0;

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
        if (cloud_state_changed)
        {
            m_pass_state.sky_frames_remaining   = temporal_convergence_frames;
            m_pass_state.cloud_history_valid     = false;
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

        // progressive average blend, the n-th warmup frame weighs 1/n so the first frame fully
        // replaces the panorama, no ghost of the previous cloudscape survives a coverage or sun
        // change, and the following frames converge to the exact mean of the jittered bakes
        const uint32_t warmup_frame_index = temporal_convergence_frames - m_pass_state.sky_frames_remaining;
        m_pass_state.sky_warmup_blend     = 1.0f / static_cast<float>(warmup_frame_index + 1);

        if (m_pass_state.sky_frames_remaining > 0)
        {
            m_pass_state.sky_frames_remaining--;
        }

        m_pass_state.sky_first_frame           = false;
        m_pass_state.sky_had_directional_light = has_directional_light;
        return has_directional_light || light_changed || cloud_state_changed;
    }

    void Renderer::SetStandardResources(RHI_CommandList* cmd_list)
    {
        cmd_list->SetConstantBuffer(Renderer_BindingsCb::frame, GetBuffer(Renderer_Buffer::ConstantFrame));
        cmd_list->SetTexture(Renderer_BindingsSrv::tex_perlin, GetStandardTexture(Renderer_StandardTexture::Noise_perlin));

        bool is_graphics_queue = cmd_list->GetQueue() && cmd_list->GetQueue()->GetType() == RHI_Queue_Type::Graphics;
        RHI_Texture* tex_wind  = GetRenderTarget(Renderer_RenderTarget::wind_field);
        if (is_graphics_queue && tex_wind)
        {
            cmd_list->SetTexture(Renderer_BindingsSrv::tex_wind_field, tex_wind);
        }

        RHI_Texture* tex_ocean_disp = GetRenderTarget(Renderer_RenderTarget::ocean_displacement);
        RHI_Texture* tex_ocean_norm = GetRenderTarget(Renderer_RenderTarget::ocean_normal);
        if (is_graphics_queue && tex_ocean_disp)
        {
            cmd_list->SetTexture(Renderer_BindingsSrv::ocean_displacement, tex_ocean_disp);
        }
        if (is_graphics_queue && tex_ocean_norm)
        {
            cmd_list->SetTexture(Renderer_BindingsSrv::ocean_normal, tex_ocean_norm);
        }
    }

    void Renderer::Pass_VariableRateShading(RHI_CommandList* cmd_list)
    {
        if (!cvar_variable_rate_shading.GetValueAs<bool>())
            return;

        RHI_Shader* shader_c = GetShader(Renderer_Shader::variable_rate_shading_c);
        RHI_Texture* tex_in  = GetRenderTarget(Renderer_RenderTarget::frame_output);
        RHI_Texture* tex_out = GetRenderTarget(Renderer_RenderTarget::shading_rate);
        if (!shader_c || !shader_c->IsCompiled() || !tex_in || !tex_out)
            return;

        // clear to full rate on first use or after render target recreation
        if (tex_out != m_pass_state.vrs_last_cleared_texture)
        {
            cmd_list->ClearTexture(tex_out, Color(0.0f, 0.0f, 0.0f, 0.0f));
            m_pass_state.vrs_last_cleared_texture = tex_out;
        }

        cmd_list->BeginTimeblock("variable_rate_shading");
        {
            RHI_PipelineState pso;
            pso.name             = "variable_rate_shading";
            pso.shaders[Compute] = shader_c;
            cmd_list->SetPipelineState(pso);

            cmd_list->SetTexture(Renderer_BindingsSrv::tex, tex_in);
            cmd_list->SetTexture(Renderer_BindingsUav::tex_uint, tex_out);
            cmd_list->Dispatch(tex_out);
        }
        cmd_list->EndTimeblock();
    }

    void Renderer::Pass_ComputeBatchA(RHI_CommandList* cmd_list, bool update_skysphere, Light* directional_light)
    {
        cmd_list->BeginMarker("compute_batch_a");

        // accel structures first so batch b's rt passes inherit the tlas via compute queue order
        UpdateAccelerationStructures(cmd_list);

        // light cluster assign lives here, it only needs the camera frustum and the light list,
        // not the gbuffer, so it runs in parallel with graphics phase 1 instead of waiting for it
        // like the rest of batch b does, this widens batch a so it overlaps the entire gbuffer pass
        // and shortens batch b so ray-traced shadows can start sooner
        Pass_LightClusterAssign(cmd_list);

        if (!m_pass_state.brdf_lut_produced)
        {
            Pass_Lut_BrdfSpecular(cmd_list);
            m_pass_state.brdf_lut_produced = true;
        }

        if (update_skysphere)
        {
            if (!m_pass_state.atmosphere_lut_produced)
            {
                Pass_Lut_AtmosphericScattering(cmd_list);
                m_pass_state.atmosphere_lut_produced = true;
            }
            if (!m_pass_state.cloud_noise_produced)
            {
                Pass_CloudNoise(cmd_list);
                m_pass_state.cloud_noise_produced = true;
            }
            Pass_Skysphere(cmd_list);
        }

        cmd_list->EndMarker();
    }

    void Renderer::Pass_ComputeBatchB(RHI_CommandList* cmd_list)
    {
        cmd_list->BeginMarker("compute_batch_b");
        Pass_ScreenSpaceAmbientOcclusion(cmd_list);
        Pass_ScreenSpaceShadows(cmd_list);
        Pass_RayTracedShadows(cmd_list);
        Pass_ReSTIR_PathTracing(cmd_list);
        Pass_ReSTIR_Denoising(cmd_list);
        cmd_list->EndMarker();
    }

    void Renderer::Pass_GraphicsPhase1_Geometry(RHI_CommandList* cmd_list)
    {
        Pass_HiZ(cmd_list);
        Pass_IndirectCull(cmd_list);
        // populate the gpu procedural grass ring before the geometry rasters that consume it
        // safe to run unconditionally, the pass early-outs when grass is disabled
        Pass_Grass_Populate(cmd_list);
        Pass_Depth_Prepass(cmd_list);
        Pass_GBuffer(cmd_list, false);
        Pass_MeshletVisualize(cmd_list);
    }

    void Renderer::Pass_GraphicsPhase2_ShadowsAndRT(RHI_CommandList* cmd_list)
    {
        Pass_ShadowMaps(cmd_list);
    }

    void Renderer::ProduceFrame_PerEye(RHI_CommandList* cmd_list, uint32_t eye, uint32_t eye_layer)
    {
        // opaque per-eye lighting runs on the graphics queue here, on purpose, because the
        // graphics queue would otherwise be idle for the duration of pass_light (the heaviest
        // compute shader in the frame) while waiting for the compute queue to finish, doing it
        // on graphics keeps both queues busy: compute runs batches a + b in parallel, graphics
        // runs phase 1 + phase 2 + this lighting + post-process, which is a much better balance
        Pass_Light(cmd_list, false, eye_layer);
        Pass_Light_Composition(cmd_list, false, eye_layer);

        const bool clouds_prepared = Pass_Clouds_Prepare(cmd_list, eye_layer);
        cmd_list->Blit(GetRenderTarget(Renderer_RenderTarget::frame_render), GetRenderTarget(Renderer_RenderTarget::frame_render_opaque), false);
        if (clouds_prepared)
        {
            Pass_Clouds_Composite(cmd_list, eye_layer, GetRenderTarget(Renderer_RenderTarget::frame_render_opaque));
        }

        if (eye == 0)
        {
            Pass_LightClusterVisualize(cmd_list);
        }

        if (m_transparents_present)
        {
            Pass_GBuffer(cmd_list, true);
            Pass_Light(cmd_list, true, eye_layer);
            Pass_Light_Composition(cmd_list, true, eye_layer);
        }

        // trace after transparent gbuffer so glass pixels own their reflection rays instead of whatever was behind them
        Pass_Reflections_Trace(cmd_list, eye_layer);

        Pass_Light_Ibl(cmd_list, eye_layer);
        Pass_Reflections_Shade(cmd_list, eye_layer);
        Pass_Reflections_Denoise(cmd_list, eye_layer);

        Pass_Reflections_Apply(cmd_list, eye_layer);
        Pass_LightFlares(cmd_list, eye_layer);
        if (clouds_prepared)
        {
            const bool xr_stereo = Xr::IsSessionRunning() && Xr::GetStereoMode();
            Pass_Clouds(cmd_list, eye_layer, !xr_stereo || eye == Xr::eye_count - 1);
        }

        // particles remain foreground content and composite after world space clouds
        if (eye == 0)
        {
            Pass_Particles(cmd_list);
        }

        Pass_AA_Upscale(cmd_list, eye_layer);
        Pass_PostProcess(cmd_list, eye_layer);

        if (Xr::IsSessionRunning() && Xr::GetStereoMode())
        {
            cmd_list->BlitToArrayLayer(
                GetRenderTarget(Renderer_RenderTarget::frame_output),
                GetRenderTarget(Renderer_RenderTarget::frame_output_stereo),
                eye
            );
        }
    }

    void Renderer::ProduceFrame(RHI_CommandList* cmd_list_graphics_present, RHI_CommandList* cmd_list_compute)
    {
        SP_PROFILE_CPU();

        // wait until every shader has finished compiling, null entries are safe to skip
        for (const auto& shader : GetShaders())
        {
            if (!shader)
            {
                continue;
            }
            const RHI_ShaderCompilationState state = shader->GetCompilationState();
            if (state == RHI_ShaderCompilationState::Idle || state == RHI_ShaderCompilationState::Compiling)
                return;
        }

        RHI_Texture* rt_output         = GetRenderTarget(Renderer_RenderTarget::frame_output);
        const bool update_skysphere    = UpdateSkysphereConvergenceState();
        Light* directional_light       = World::GetDirectionalLight();
        RHI_Queue* queue_graphics      = RHI_Device::GetQueue(RHI_Queue_Type::Graphics);

        // submit uploads before compute batch a
        cmd_list_graphics_present->Submit(nullptr, false);
        RHI_SyncPrimitive* uploads_timeline = cmd_list_graphics_present->GetTimelineSemaphore();
        const uint64_t uploads_value        = cmd_list_graphics_present->GetLastTimelineSignalValue();

        cmd_list_graphics_present = queue_graphics->NextCommandList();
        cmd_list_graphics_present->Begin();
        m_cmd_list_present = cmd_list_graphics_present;

        // compute batch a, view independent prep, runs alongside graphics phase 1
        Pass_ComputeBatchA(cmd_list_compute, update_skysphere, directional_light);

        // submit batch a after the upload flush so light cluster assign reads this frame's camera matrices
        // we submit before recording any graphics phase 1 work so the compute queue starts skysphere,
        // tlas update and light cluster assign as early as possible, maximising overlap with the gbuffer pass
        cmd_list_compute->Submit(nullptr, false, nullptr, uploads_timeline, uploads_value);
        RHI_SyncPrimitive* batch_a_timeline = cmd_list_compute->GetTimelineSemaphore();
        const uint64_t batch_a_value        = cmd_list_compute->GetLastTimelineSignalValue();

        // wind field stays on graphics queue, must precede gbuffer for vertex animation sampling
        // recorded after the compute submit so the gpu compute queue is already kicked off by the time we record it
        Pass_WindField(cmd_list_graphics_present);

        // fft ocean runs on the graphics queue too, its displacement must be ready for the depth prepass and gbuffer
        Pass_Ocean(cmd_list_graphics_present);

        if (Camera* camera = World::GetCamera())
        {
            Pass_VariableRateShading(cmd_list_graphics_present);
            Pass_GraphicsPhase1_Geometry(cmd_list_graphics_present);

            // submit phase 1, signal gbuffer ready
            cmd_list_graphics_present->Submit(nullptr, false);
            const uint64_t gfx_phase1_timeline_value = cmd_list_graphics_present->GetLastTimelineSignalValue();
            RHI_SyncPrimitive* gfx_timeline          = cmd_list_graphics_present->GetTimelineSemaphore();

            // compute batch b, gbuffer consumers, waits on phase 1, overlaps with graphics phase 2
            // batch b's outputs are read by phase 3 on the graphics queue, the present submit in
            // SubmitAndPresent will wait on batch b's timeline to make those writes visible
            RHI_Queue* queue_compute            = RHI_Device::GetQueue(RHI_Queue_Type::Compute);
            RHI_CommandList* cmd_list_compute_b = queue_compute->NextCommandList();
            cmd_list_compute_b->Begin();
            Pass_ComputeBatchB(cmd_list_compute_b);
            cmd_list_compute_b->Submit(nullptr, false, nullptr, gfx_timeline, gfx_phase1_timeline_value);
            RHI_SyncPrimitive* compute_b_timeline = cmd_list_compute_b->GetTimelineSemaphore();
            const uint64_t compute_b_value        = cmd_list_compute_b->GetLastTimelineSignalValue();

            // graphics phase 2, runs parallel to batch b, waits on batch a so tlas is ready for the reflections trace that follows in per-eye
            cmd_list_graphics_present = queue_graphics->NextCommandList();
            cmd_list_graphics_present->Begin();
            m_cmd_list_present        = cmd_list_graphics_present;
            Pass_GraphicsPhase2_ShadowsAndRT(cmd_list_graphics_present);
            cmd_list_graphics_present->Submit(nullptr, false, nullptr, batch_a_timeline, batch_a_value);

            // graphics phase 3, opaque lighting + post-process, present cmd list waits on batch b in SubmitAndPresent
            // pass_light lives here on the graphics queue (not on compute) on purpose, because the
            // graphics queue is otherwise mostly idle for the duration of the lighting dispatch,
            // running it here keeps both queues busy and produces a better overall frame time
            cmd_list_graphics_present = queue_graphics->NextCommandList();
            cmd_list_graphics_present->Begin();
            m_cmd_list_present                                = cmd_list_graphics_present;
            m_cross_queue_sync.pending_compute_timeline       = compute_b_timeline;
            m_cross_queue_sync.pending_compute_timeline_value = compute_b_value;

            const bool xr_stereo     = Xr::IsSessionRunning() && Xr::GetStereoMode();
            const uint32_t eye_count = xr_stereo ? Xr::eye_count : 1;
            for (uint32_t eye = 0; eye < eye_count; eye++)
            {
                const uint32_t eye_layer = xr_stereo ? eye : rhi_all_mips;
                // tag push constant eye index so per eye selectors in common_resources.hlsl pick the right matrices
                m_pcb_pass_cpu.eye_index = xr_stereo ? eye : 0;
                ProduceFrame_PerEye(cmd_list_graphics_present, eye, eye_layer);
            }
            // reset eye index for non per eye passes that follow
            m_pcb_pass_cpu.eye_index = 0;
        }
        else
        {
            cmd_list_graphics_present->ClearTexture(rt_output, Color::standard_black);
        }

        Pass_Text(cmd_list_graphics_present, rt_output);

        // swap the gbuffer depth and normal history slots on the cpu, after this point any
        // GetRenderTarget(gbuffer_depth/normal) call resolves to the slot the next frame will
        // overwrite and the *_previous slots resolve to this frame's data, which is exactly
        // what restir's temporal validity gate needs to read next frame
        Pass_ReSTIR_SwapGBufferHistory();
    }
}
