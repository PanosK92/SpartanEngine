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
#include <unordered_set>
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
#include "../RHI/RHI_VendorTechnology.h"
#include "../RHI/RHI_AccelerationStructure.h"
#include "../World/Entity.h"
#include "../World/Components/Light.h"
#include "../World/Components/Camera.h"
#include "../World/Components/Volume.h"
#include "../Core/ProgressTracker.h"
#include "../Math/Rectangle.h"
#include "../Resource/Import/ImageImporter.h"
#include "../Commands/Console/ConsoleCommands.h"
#include "../Core/Breadcrumbs.h"
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

    // per-frame rotated buffers
    array<Renderer::FrameResource, renderer_draw_data_buffer_count> Renderer::m_frame_resources;
    uint32_t Renderer::m_frame_resource_index = 0;

    // line and icon rendering
    shared_ptr<RHI_Buffer> Renderer::m_lines_vertex_buffer;
    vector<RHI_Vertex_PosCol> Renderer::m_lines_vertices;
    vector<PersistentLine> Renderer::m_persistent_lines;
    vector<tuple<RHI_Texture*, math::Vector3>> Renderer::m_icons;

    // misc
    uint32_t Renderer::m_resource_index            = 0;
    atomic<bool> Renderer::m_initialized_resources = false;
    bool Renderer::m_transparents_present          = false;
    bool Renderer::m_is_hiz_suppressed             = false;
    bool Renderer::m_bindless_samplers_dirty       = true;
    RHI_CommandList* Renderer::m_cmd_list_present  = nullptr;
    RHI_CommandList* Renderer::m_cmd_list_compute  = nullptr;
    vector<ShadowSlice> Renderer::m_shadow_slices;
    array<RHI_Texture*, rhi_max_array_size> Renderer::m_bindless_textures;
    array<Sb_Light, rhi_max_array_size> Renderer::m_bindless_lights;
    array<Sb_Aabb, rhi_max_array_size> Renderer::m_bindless_aabbs;
    unique_ptr<RHI_AccelerationStructure> m_tlas;
    uint32_t Renderer::m_count_active_lights = 0;

    namespace
    {
        // resolution & viewport
        math::Vector2 m_resolution_render = math::Vector2::Zero;
        math::Vector2 m_resolution_output = math::Vector2::Zero;
        RHI_Viewport m_viewport           = RHI_Viewport(0, 0, 0, 0);

        // rhi resources
        shared_ptr<RHI_SwapChain> swapchain;
        const uint8_t swap_chain_buffer_count = 2;

        uint64_t frame_num                   = 0;
        math::Vector2 jitter_offset          = math::Vector2::Zero;
        const uint32_t resolution_shadow_min = 128;
        float near_plane                     = 0.0f;
        float far_plane                      = 1.0f;
        bool dirty_orthographic_projection   = true;

        float sanitize_resolution_scale(float scale)
        {
            return clamp(scale, 0.5f, 1.0f);
        }

        void dynamic_resolution()
        {
            if (cvar_dynamic_resolution.GetValue() != 0.0f)
            {
                float gpu_time_target   = 16.67f;                                               // target for 60 FPS
                float adjustment_factor = static_cast<float>(0.05f * Timer::GetDeltaTimeSec()); // how aggressively to adjust screen percentage
                float screen_percentage = Renderer::GetResolutionScale();
                float gpu_time          = Profiler::GetTimeGpuLast();

                if (gpu_time < gpu_time_target) // gpu is under target, increase resolution
                {
                    screen_percentage += adjustment_factor * (gpu_time_target - gpu_time);
                }
                else // gpu is over target, decrease resolution
                {
                    screen_percentage -= adjustment_factor * (gpu_time - gpu_time_target);
                }

                screen_percentage = sanitize_resolution_scale(screen_percentage);
                ConsoleRegistry::Get().SetValueFromString("r.resolution_scale", to_string(screen_percentage));
            }
        }
    }

    void Renderer::Initialize()
    {
        // device
        {
            if (Debugging::IsRenderdocEnabled())
            {
                RenderDoc::OnPreDeviceCreation();
            }

            RHI_Device::Initialize();
        }

        // breadcrumbs
        if (Debugging::IsBreadcrumbsEnabled())
        {
            Breadcrumbs::Initialize();
        }

        // runtime cvar overrides
        {
            // gamma from display
            ConsoleRegistry::Get().SetValueFromString("r.gamma", to_string(Display::GetGamma()));
            
            // default tonemapping
            ConsoleRegistry::Get().SetValueFromString("r.tonemapping", to_string(static_cast<float>(Renderer_Tonemapping::GranTurismo7)));

            // wind is owned by World, initialized in World::Initialize()
        }

        // resolution (settings or editor may override later)
        {
            uint32_t width  = Window::GetWidth();
            uint32_t height = Window::GetHeight();

            SetResolutionOutput(width, height, false);
            SetResolutionRender(1920, 1080, false); // lower than output so fsr/taa works well
            SetViewport(static_cast<float>(width), static_cast<float>(height));
        }

        // must init before swapchain since breadcrumbs need it for command lists
        RHI_VendorTechnology::Initialize();

        // swapchain
        {
            swapchain = make_shared<RHI_SwapChain>
            (
                Window::GetHandleSDL(),
                Window::GetWidth(),
                Window::GetHeight(),
                cvar_vsync.GetValueAs<bool>() ? RHI_Present_Mode::Fifo : RHI_Present_Mode::Immediate,
                swap_chain_buffer_count,
                Display::GetHdr(),
                "renderer"
            );

            ConsoleRegistry::Get().SetValueFromString("r.hdr", swapchain->IsHdr() ? "1" : "0");
        }

        // resources (heavy ops on background thread)
        {
            ThreadPool::AddTask([]()
            {
                m_initialized_resources = false;
                CreateStandardMeshes();
                CreateStandardTextures();
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

            // pre-size the global geometry buffer so first build allocates large enough capacity
            // to fit typical large worlds (sponza/forest) without triggering a mid-load rebuild
            GeometryBuffer::Reserve(
                4u * 1024u * 1024u,  // ~4M vertices  (~128 MB at 32B/vertex)
                12u * 1024u * 1024u, // ~12M indices  (~48 MB)
                64u * 1024u,         // ~64K meshlet bounds
                16u * 1024u          // ~16K instances
            );
        }

        if (RHI_Device::GetPrimaryPhysicalDevice()->IsBelowMinimumRequirements())
        {
            Window::SetSplashScreenVisible(false);
            SP_WARNING_WINDOW("The GPU does not meet the minimum requirements for running the engine. The engine might be missing features and it won't perform as expected.");
            Window::SetSplashScreenVisible(true);
        }

        // events
        {
            SP_SUBSCRIBE_TO_EVENT(EventType::WindowFullScreenToggled, SP_EVENT_HANDLER_STATIC(OnFullScreenToggled));
            SP_FIRE_EVENT(EventType::RendererOnInitialized);
        }
    }

    void Renderer::Shutdown()
    {
        SP_FIRE_EVENT(EventType::RendererOnShutdown);

        RHI_Device::QueueWaitAll();

        RHI_CommandList::ImmediateExecutionShutdown();

        {
            DestroyResources();
            GeometryBuffer::Shutdown();
            swapchain             = nullptr;
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

        RHI_Device::Destroy();
    }

    void Renderer::Tick()
    {
        Profiler::FrameStart();

        // process deferred fullscreen toggle at a safe point where no command lists are in flight
        if (Window::IsFullScreenTogglePending())
        {
            RHI_Device::QueueWaitAll();
            Window::ProcessFullScreenToggle();
        }

        {
            swapchain->AcquireNextImage();
            RHI_Device::Tick(frame_num);

            if (RHI_Context::api_type != RHI_Api_Type::D3d12)
            {
                RHI_VendorTechnology::Tick(&m_cb_frame_cpu, GetResolutionRender(), GetResolutionOutput(), GetResolutionScale());
                dynamic_resolution();
            }

            // breadcrumbs
            if (Debugging::IsBreadcrumbsEnabled())
            {
                Breadcrumbs::StartFrame();
            }
        }

        
        // recreate optional render targets when feature cvars change
        if (m_initialized_resources)
        {
            static uint32_t options_hash  = 0;
            static float restir_scale_old = -1.0f;
            uint32_t options_hash_new     = (cvar_ssao.GetValueAs<bool>() << 0) | (cvar_ray_traced_reflections.GetValueAs<bool>() << 1) | (cvar_restir_pt.GetValueAs<bool>() << 2);
            float restir_scale_new        = cvar_restir_pt_scale.GetValue();

            if (options_hash_new != options_hash || restir_scale_new != restir_scale_old)
            {
                RHI_Device::QueueWaitAll(true);
                RHI_Device::DeletionQueueParse();
                UpdateOptionalRenderTargets();
                RHI_Device::DeletionQueueParse();
                options_hash    = options_hash_new;
                restir_scale_old = restir_scale_new;
            }
        }
    
        const uint32_t min_render_dimension = 64;
        bool resolution_valid = m_resolution_render.x >= min_render_dimension && m_resolution_render.y >= min_render_dimension;
        bool can_render = !Window::IsMinimized() && m_initialized_resources && resolution_valid;

        // prevent write-after-present hazards when idle (skip first frame, nothing to wait for)
        if (!can_render && frame_num > 0)
        {
            RHI_Device::GetQueue(RHI_Queue_Type::Graphics)->Wait();
        }

        {
            m_cmd_list_present = RHI_Device::GetQueue(RHI_Queue_Type::Graphics)->NextCommandList();
            m_cmd_list_present->Begin();
        }

        m_cmd_list_compute = nullptr;
        if (can_render)
        {
            m_cmd_list_compute = RHI_Device::GetQueue(RHI_Queue_Type::Compute)->NextCommandList();
            m_cmd_list_compute->Begin();
        }

        m_draw_data_count = 0;

        if (can_render)
        {
            // skip heavy gpu work during loading to avoid contention with texture uploads
            bool is_loading = ProgressTracker::IsLoading();

            // suppress hi-z for a grace period after loading while draw calls stabilize
            {
                static uint32_t post_load_frames = 0;
                static bool was_loading           = true;

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

                m_is_hiz_suppressed = post_load_frames > 0;
            }

            // rebuild geometry buffer if new meshes arrived
            // safe to run during loading because growth routes the old buffers through the deletion queue
            // so meshes appear progressively as they finish importing
            GeometryBuffer::BuildIfDirty();

            // geometry buffer rebuild invalidates blas device addresses
            if (GeometryBuffer::WasRebuilt())
            {
                DestroyAccelerationStructures();

                // free released blas/tlas gpu memory before rebuilding to avoid a peak
                RHI_Device::DeletionQueueParse();
            }

            // rotate per-frame buffers to avoid cpu-gpu races
            RotateFrameBuffers();

            UpdateDrawCalls(m_cmd_list_present);

            if (!is_loading)
            {
                UpdateAccelerationStructures(m_cmd_list_compute);
            }
    
            // resource cleanup - frame-based retirement, no gpu stall required
            if (RHI_Device::DeletionQueueNeedsToParse())
            {
                RHI_Device::DeletionQueueParse();
            }

            // reset constant buffer offset periodically
            {
                m_resource_index++;
                if (m_resource_index == renderer_draw_data_buffer_count)
                {
                    m_resource_index = 0;
                    GetBuffer(Renderer_Buffer::ConstantFrame)->ResetOffset();
                }
            }
    
            // bindless resource updates
            if (!is_loading)
            {
                bool initialize = GetFrameNumber() == 0;

                // lights
                if (initialize || World::HaveLightsChanged())
                {
                    UpdateShadowAtlas();
                    UpdateLights(m_cmd_list_present);
                    RHI_Device::UpdateBindlessLights(GetBuffer(Renderer_Buffer::LightParameters));
                }

                // materials
                if (initialize || World::HaveMaterialsChangedThisFrame())
                {
                    UpdateMaterials(m_cmd_list_present);
                    RHI_Device::UpdateBindlessMaterials(&m_bindless_textures, GetBuffer(Renderer_Buffer::MaterialParameters));
                }

                // samplers
                if (m_bindless_samplers_dirty)
                {
                    RHI_Device::UpdateBindlessSamplers(&Renderer::GetSamplers());
                    m_bindless_samplers_dirty = false;
                }

                // aabbs (always, they change with entity transforms)
                {
                    UpdateBoundingBoxes(m_cmd_list_present);

                    static bool aabbs_descriptor_set = false;
                    if (!aabbs_descriptor_set)
                    {
                        RHI_Device::UpdateBindlessAABBs(GetBuffer(Renderer_Buffer::AABBs));
                        aabbs_descriptor_set = true;
                    }
                }

                // draw data
                {
                    if (m_draw_data_count > 0)
                    {
                        RHI_Buffer* buffer = GetBuffer(Renderer_Buffer::DrawData);
                        uint32_t frame_byte_offset = m_frame_resource_index * renderer_max_draw_calls * static_cast<uint32_t>(sizeof(Sb_DrawData));
                        uint32_t upload_size       = static_cast<uint32_t>(sizeof(Sb_DrawData)) * m_draw_data_count;
                        m_cmd_list_present->UpdateBuffer(buffer, frame_byte_offset, upload_size, &m_draw_data_cpu[0]);
                    }

                    // the descriptor points to a single large buffer that holds all frames' draw data
                    // at different offsets, so it only needs to be set once; this eliminates the race
                    // where vkUpdateDescriptorSets (host-side, instantly visible under UPDATE_AFTER_BIND)
                    // would change the buffer pointer while the previous frame's phase 3 transparent pass
                    // was still reading from it on the gpu
                    static bool draw_data_descriptor_set = false;
                    if (!draw_data_descriptor_set)
                    {
                        RHI_Device::UpdateBindlessDrawData(GetBuffer(Renderer_Buffer::DrawData));
                        draw_data_descriptor_set = true;
                    }
                }

                // geometry buffers (vertex pulling via bindless structured buffers)
                {
                    static RHI_Buffer* last_vertex_buffer = nullptr;
                    RHI_Buffer* current_vertex = GeometryBuffer::GetVertexBuffer();
                    if (current_vertex && current_vertex != last_vertex_buffer)
                    {
                        RHI_Device::UpdateBindlessGeometryVertices(current_vertex);
                        last_vertex_buffer = current_vertex;
                    }

                    static RHI_Buffer* last_index_buffer = nullptr;
                    RHI_Buffer* current_index = GeometryBuffer::GetIndexBuffer();
                    if (current_index && current_index != last_index_buffer)
                    {
                        RHI_Device::UpdateBindlessGeometryIndices(current_index);
                        last_index_buffer = current_index;
                    }
                }

                // global instance buffer (vertex pulling for instanced indirect draws)
                // falls back to the dummy buffer until the global one exists, rebinds whenever the global buffer is recreated
                {
                    static RHI_Buffer* last_instance_buffer = nullptr;
                    RHI_Buffer* current_instance            = GeometryBuffer::GetInstanceBuffer();
                    if (current_instance == nullptr)
                    {
                        current_instance = GetBuffer(Renderer_Buffer::DummyInstance);
                    }
                    if (current_instance != last_instance_buffer)
                    {
                        RHI_Device::UpdateBindlessInstances(current_instance);
                        last_instance_buffer = current_instance;
                    }
                }

                // indirect draw buffers
                // slot 0 of indirect_draw_args is the consolidated draw args, layout matches VkDrawIndirectCommand for the first 16 bytes
                // index_count aliases vertex_count and is bumped in 3-vertex steps by the triangle cull shader
                // instance_count is fixed at 1, the indirect draw is non-instanced and the vertex shader keys directly off sv_vertexid
                {
                    Sb_IndirectDrawArgs single_args = {};
                    single_args.index_count         = 0;
                    single_args.instance_count      = 1;
                    single_args.first_index         = 0;
                    single_args.vertex_offset       = 0;
                    single_args.first_instance      = 0;

                    RHI_Buffer* args_buffer = GetBuffer(Renderer_Buffer::IndirectDrawArgs);
                    args_buffer->ResetOffset();
                    args_buffer->Update(m_cmd_list_present, &single_args, sizeof(Sb_IndirectDrawArgs));
                }

                // single-slot indirect dispatch args for the triangle cull, group_count_x is bumped by the meshlet cull
                {
                    Sb_IndirectDispatchArgs dispatch_args = {};
                    dispatch_args.group_count_x           = 0;
                    dispatch_args.group_count_y           = 1;
                    dispatch_args.group_count_z           = 1;

                    RHI_Buffer* dispatch_args_buffer = GetBuffer(Renderer_Buffer::TriangleDispatchArgs);
                    dispatch_args_buffer->ResetOffset();
                    dispatch_args_buffer->Update(m_cmd_list_present, &dispatch_args, sizeof(Sb_IndirectDispatchArgs));
                }

                if (m_indirect_draw_count > 0)
                {
                    RHI_Buffer* data_buffer = GetBuffer(Renderer_Buffer::IndirectDrawData);
                    data_buffer->ResetOffset();
                    data_buffer->Update(m_cmd_list_present, &m_indirect_draw_data[0], data_buffer->GetStride() * m_indirect_draw_count);
                }

                if (m_cull_task_count > 0)
                {
                    RHI_Buffer* tasks_buffer = GetBuffer(Renderer_Buffer::CullTasks);
                    tasks_buffer->ResetOffset();
                    tasks_buffer->Update(m_cmd_list_present, &m_cull_tasks[0], tasks_buffer->GetStride() * m_cull_task_count);
                }
            }
    
            UpdatePersistentLines();
            AddLinesToBeRendered();
        }

        // xr: begin the frame before updating the frame constant buffer so the per-eye
        // pose/projection matrices used for shading reflect the predicted pose for this
        // frame rather than the previous one. xrBeginFrame must pair with xrEndFrame below.
        bool xr_should_render = false;
        if (Xr::IsSessionRunning())
        {
            xr_should_render = Xr::BeginFrame();
        }

        if (can_render)
        {
            UpdateFrameConstantBuffer(m_cmd_list_present);
        }

        {
            if (can_render)
            {
                ProduceFrame(m_cmd_list_present, m_cmd_list_compute);
            }
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
    
        bool is_standalone = !Engine::IsFlagSet(EngineMode::EditorVisible);

        if (is_standalone && can_render)
        {
            BlitToBackBuffer(m_cmd_list_present, GetRenderTarget(Renderer_RenderTarget::frame_output));
        }

        if (is_standalone)
        {
            SubmitAndPresent();
        }
    
        {
            m_lines_vertices.clear();
            m_icons.clear();
        }
    
        // only count frames that actually rendered
        if (can_render)
        {
            frame_num++;
            if (frame_num == 1)
            {
                SP_FIRE_EVENT(EventType::RendererOnFirstFrameCompleted);
            }
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
            return false;

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

            RHI_Device::DeletionQueueFlush();

            // after recreation the gpu is idle and all images sit in their initial layout
            // (general or undefined), but the per-texture layout tracking may still hold the
            // layout from the last frame (e.g. shader_read).  resetting to Max (unknown)
            // forces the next InsertBarrier to emit an Undefined -> target transition, which
            // the spec guarantees is always valid regardless of the actual gpu-side layout.
            for (uint32_t i = 0; i < static_cast<uint32_t>(Renderer_RenderTarget::max); i++)
            {
                if (RHI_Texture* rt = GetRenderTarget(static_cast<Renderer_RenderTarget>(i)))
                {
                    rt->ClearLayouts();
                }
            }

            // the layout reset above invalidates one-shot render targets (luts, cloud noise,
            // skysphere) because the Undefined transition discards their contents
            m_pass_state.brdf_lut_produced      = false;
            m_pass_state.atmosphere_lut_produced = false;
            m_pass_state.cloud_noise_produced    = false;
            m_pass_state.sky_first_frame         = true;

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
        CreateSamplers();
    }

    void Renderer::UpdateFrameConstantBuffer(RHI_CommandList* cmd_list)
    {
        // matrices
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
                // near = 0 for ortho (avoids NaN in [3,2] element)
                Matrix projection_ortho              = Matrix::CreateOrthographicLH(m_viewport.width, m_viewport.height, 0.0f, far_plane);
                m_cb_frame_cpu.view_projection_orthographic = Matrix::CreateLookAtLH(Vector3(0, 0, -near_plane), Vector3::Forward, Vector3::Up) * projection_ortho;
                dirty_orthographic_projection        = false;
            }
        }

        // taa jitter
        Renderer_AntiAliasing_Upsampling upsampling_mode = cvar_antialiasing_upsampling.GetValueAs<Renderer_AntiAliasing_Upsampling>();
        {
            if (upsampling_mode == Renderer_AntiAliasing_Upsampling::AA_Fsr_Upscale_Fsr)
            {
                RHI_VendorTechnology::FSR3_GenerateJitterSample(&jitter_offset.x, &jitter_offset.y);
                m_cb_frame_cpu.projection *= Matrix::CreateTranslation(Vector3(jitter_offset.x, jitter_offset.y, 0.0f));
            }
            else if (upsampling_mode == Renderer_AntiAliasing_Upsampling::AA_Xess_Upscale_Xess)
            {
                RHI_VendorTechnology::XeSS_GenerateJitterSample(&jitter_offset.x, &jitter_offset.y);
                m_cb_frame_cpu.projection *= Matrix::CreateTranslation(Vector3(jitter_offset.x, jitter_offset.y, 0.0f));
            }
            else
            {
                jitter_offset = Vector2::Zero;
            }
        }

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
        m_cb_frame_cpu.resolution_output   = m_resolution_output;
        m_cb_frame_cpu.resolution_render   = m_resolution_render;
        m_cb_frame_cpu.taa_jitter_previous = m_cb_frame_cpu.taa_jitter_current;
        m_cb_frame_cpu.taa_jitter_current  = jitter_offset;
        m_cb_frame_cpu.time                = Timer::GetTimeSec();
        m_cb_frame_cpu.delta_time          = static_cast<float>(Timer::GetDeltaTimeSec());
        m_cb_frame_cpu.frame               = static_cast<uint32_t>(frame_num);
        m_cb_frame_cpu.resolution_scale    = GetResolutionScale();
        m_cb_frame_cpu.restir_pt_scale     = cvar_restir_pt_scale.GetValue();
        m_cb_frame_cpu.restir_pt_debug_mode = cvar_restir_pt_debug_mode.GetValue();
        m_cb_frame_cpu.hdr_enabled         = cvar_hdr.GetValueAs<bool>() ? 1.0f : 0.0f;
        m_cb_frame_cpu.hdr_max_nits        = Display::GetLuminanceMax();
        m_cb_frame_cpu.gamma               = cvar_gamma.GetValue();
        m_cb_frame_cpu.camera_exposure     = World::GetCamera() ? World::GetCamera()->GetExposure() : 1.0f;

        m_cb_frame_cpu.cloud_coverage = cvar_cloud_coverage.GetValue();
        m_cb_frame_cpu.cloud_shadows  = cvar_cloud_shadows.GetValue();
        m_cb_frame_cpu.restir_pt_light_count = static_cast<float>(m_count_active_lights);
        m_cb_frame_cpu.wind           = World::GetWind();
        // feature bits (must match common_resources.hlsl)
        // ray traced shadows require a valid tlas so the shader's inline ray query has something to trace against
        bool tlas_available = RHI_Device::IsSupportedRayTracing() && GetTopLevelAccelerationStructure() != nullptr;
        m_cb_frame_cpu.set_bit(cvar_ray_traced_reflections.GetValueAs<bool>(),                    1 << 0);
        m_cb_frame_cpu.set_bit(cvar_ssao.GetValueAs<bool>(),                                      1 << 1);
        m_cb_frame_cpu.set_bit(cvar_ray_traced_shadows.GetValueAs<bool>() && tlas_available,      1 << 2);
        m_cb_frame_cpu.set_bit(cvar_restir_pt.GetValueAs<bool>(),                                 1 << 3);

        // vr stereo: override primary matrices with the left eye and populate the right eye
        // so that every shader helper can pick the correct per-eye matrices via eye_index
        if (Xr::IsSessionRunning() && Xr::GetStereoMode())
        {
            // left eye -> primary matrices
            m_cb_frame_cpu.view                     = Xr::GetViewMatrix(0);
            m_cb_frame_cpu.view_inverted            = Matrix::Invert(m_cb_frame_cpu.view);
            m_cb_frame_cpu.projection               = Xr::GetProjectionMatrix(0);
            m_cb_frame_cpu.projection_inverted      = Matrix::Invert(m_cb_frame_cpu.projection);
            m_cb_frame_cpu.view_projection          = m_cb_frame_cpu.view * m_cb_frame_cpu.projection;
            m_cb_frame_cpu.view_projection_inverted = Matrix::Invert(m_cb_frame_cpu.view_projection);
            m_cb_frame_cpu.camera_position          = m_cb_frame_cpu.view_inverted.GetTranslation();

            // vr has no taa jitter applied to the projection, so jittered == unjittered.
            // these fields were computed earlier in the function using the mono camera, so
            // replace them now with the left-eye xr matrices so motion-blur sky reprojection
            // and any other unjittered-vp consumer sees eye-consistent data.
            m_cb_frame_cpu.view_projection_previous_unjittered = m_view_projection_previous_unjittered_left;
            m_cb_frame_cpu.view_projection_unjittered          = m_cb_frame_cpu.view_projection;

            // right eye
            m_cb_frame_cpu.view_right                                 = Xr::GetViewMatrix(1);
            m_cb_frame_cpu.view_inverted_right                        = Matrix::Invert(m_cb_frame_cpu.view_right);
            m_cb_frame_cpu.projection_right                           = Xr::GetProjectionMatrix(1);
            m_cb_frame_cpu.projection_inverted_right                  = Matrix::Invert(m_cb_frame_cpu.projection_right);
            m_cb_frame_cpu.view_projection_right                      = m_cb_frame_cpu.view_right * m_cb_frame_cpu.projection_right;
            m_cb_frame_cpu.view_projection_inverted_right             = Matrix::Invert(m_cb_frame_cpu.view_projection_right);
            m_cb_frame_cpu.view_projection_previous_right             = m_view_projection_previous_right;
            m_cb_frame_cpu.view_projection_unjittered_right           = m_cb_frame_cpu.view_projection_right;
            m_cb_frame_cpu.view_projection_previous_unjittered_right  = m_view_projection_previous_right;
            m_cb_frame_cpu.camera_position_right                      = m_cb_frame_cpu.view_inverted_right.GetTranslation();
            m_cb_frame_cpu.is_multiview                               = 1;

            // store the current per-eye view-projection so the next frame can use it as the
            // previous-frame matrix (the mono path only tracks the left eye through the
            // shared view_projection, so the right eye needs a dedicated history slot)
            m_view_projection_previous_right            = m_cb_frame_cpu.view_projection_right;
            m_view_projection_previous_unjittered_left  = m_cb_frame_cpu.view_projection;
        }
        else
        {
            m_cb_frame_cpu.is_multiview                               = 0;
            m_cb_frame_cpu.view_projection_previous_right             = Matrix::Identity;
            m_cb_frame_cpu.view_projection_unjittered_right           = Matrix::Identity;
            m_cb_frame_cpu.view_projection_previous_unjittered_right  = Matrix::Identity;
            m_view_projection_previous_right                          = Matrix::Identity;
            m_view_projection_previous_unjittered_left                = Matrix::Identity;
        }

        GetBuffer(Renderer_Buffer::ConstantFrame)->Update(cmd_list, &m_cb_frame_cpu);
    }

    const Vector3& Renderer::GetWind()
    {
        return World::GetWind();
    }

    void Renderer::SetWind(const math::Vector3& wind)
    {
        World::SetWind(wind);
    }

    void Renderer::OnFullScreenToggled()
    {
        static float    width_previous_viewport  = 0;
        static float    height_previous_viewport = 0;
        static uint32_t width_previous_output    = 0;
        static uint32_t height_previous_output   = 0;

        if (Window::IsFullScreen())
        {
            uint32_t width  = Window::GetWidth();
            uint32_t height = Window::GetHeight();

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
        return swapchain.get();
    }
    
    void Renderer::BlitToBackBuffer(RHI_CommandList* cmd_list, RHI_Texture* texture)
    {
        cmd_list->BeginMarker("blit_to_back_buffer");
        cmd_list->Blit(texture, swapchain.get());
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

            if (swapchain->IsImageAcquired())
            {
                #if defined(API_GRAPHICS_D3D12)
                if (RHI_Context::api_type == RHI_Api_Type::D3d12)
                {
                    m_cmd_list_present->RenderPassEnd();

                    ID3D12GraphicsCommandList* cmd_list = static_cast<ID3D12GraphicsCommandList*>(m_cmd_list_present->GetRhiResource());
                    ID3D12Resource* backbuffer          = static_cast<ID3D12Resource*>(swapchain->GetRhiRt());

                    D3D12_RESOURCE_BARRIER barrier = {};
                    barrier.Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
                    barrier.Transition.pResource   = backbuffer;
                    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
                    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
                    barrier.Transition.StateAfter  = D3D12_RESOURCE_STATE_PRESENT;
                    cmd_list->ResourceBarrier(1, &barrier);

                    m_cmd_list_present->Submit(swapchain->GetImageAcquiredSemaphore(), false, swapchain->GetRenderingCompleteSemaphore());
                    swapchain->Present(m_cmd_list_present);
                }
                else
                #endif
                {
                    m_cmd_list_present->InsertBarrier(swapchain->GetRhiRt(), swapchain->GetFormat(), 0, 1, 1, RHI_Image_Layout::Present_Source);
                    
                    m_cmd_list_present->Submit(swapchain->GetImageAcquiredSemaphore(), false, swapchain->GetRenderingCompleteSemaphore());
                    swapchain->Present(m_cmd_list_present);
                }
            }
            else
            {
                m_cmd_list_present->Submit(nullptr, true);
            }
        }
        Profiler::TimeBlockEnd(TimeBlockType::Cpu);
    }

    RHI_Api_Type Renderer::GetRhiApiType()
    {
        return RHI_Context::api_type;
    }

    uint64_t Renderer::GetFrameNumber()
    {
        return frame_num;
    }

    void Renderer::SetCommonTextures(RHI_CommandList* cmd_list, uint32_t eye_layer /*= rhi_all_mips*/)
    {
        // gbuffer (when eye_layer is specified, bind per-layer 2d views for compute passes)
        cmd_list->SetTexture(static_cast<uint32_t>(Renderer_BindingsSrv::gbuffer_albedo),   GetRenderTarget(Renderer_RenderTarget::gbuffer_color),    rhi_all_mips, 0, false, eye_layer);
        cmd_list->SetTexture(static_cast<uint32_t>(Renderer_BindingsSrv::gbuffer_normal),   GetRenderTarget(Renderer_RenderTarget::gbuffer_normal),   rhi_all_mips, 0, false, eye_layer);
        cmd_list->SetTexture(static_cast<uint32_t>(Renderer_BindingsSrv::gbuffer_material), GetRenderTarget(Renderer_RenderTarget::gbuffer_material), rhi_all_mips, 0, false, eye_layer);
        cmd_list->SetTexture(static_cast<uint32_t>(Renderer_BindingsSrv::gbuffer_velocity), GetRenderTarget(Renderer_RenderTarget::gbuffer_velocity), rhi_all_mips, 0, false, eye_layer);
        cmd_list->SetTexture(static_cast<uint32_t>(Renderer_BindingsSrv::gbuffer_depth),    GetRenderTarget(Renderer_RenderTarget::gbuffer_depth),    rhi_all_mips, 0, false, eye_layer);

        // ssao (white = no occlusion when disabled)
        RHI_Texture* tex_ssao = GetRenderTarget(Renderer_RenderTarget::ssao);
        cmd_list->SetTexture(Renderer_BindingsSrv::ssao, tex_ssao ? tex_ssao : GetStandardTexture(Renderer_StandardTexture::White));
    }

    uint32_t Renderer::WriteDrawData(const math::Matrix& transform, const math::Matrix& transform_previous, uint32_t material_index, uint32_t is_transparent)
    {
        SP_ASSERT(m_draw_data_count < renderer_max_draw_calls);
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

        // the draw data buffer is a single large allocation partitioned into per-frame regions;
        // each frame writes to its own region so there is no write-after-read race with the gpu
        uint32_t global_index = m_frame_resource_index * renderer_max_draw_calls + index;

        RHI_Buffer* buffer = GetBuffer(Renderer_Buffer::DrawData);
        if (void* mapped = buffer->GetMappedData())
        {
            void* dst = static_cast<char*>(mapped) + global_index * sizeof(Sb_DrawData);
            memcpy(dst, &entry, sizeof(Sb_DrawData));
        }

        return global_index;
    }

    void Renderer::UpdateMaterials(RHI_CommandList* cmd_list)
    {
        static array<Sb_Material, rhi_max_array_size> properties;
        static unordered_set<uint64_t> unique_material_ids;
        uint32_t count = 0;
    
        auto update_material = [&count](Material* material)
        {
            if (unique_material_ids.find(material->GetObjectId()) != unique_material_ids.end())
                return;
    
            unique_material_ids.insert(material->GetObjectId());
            {
                SP_ASSERT(count < rhi_max_array_size);

                properties[count].local_width           = material->GetProperty(MaterialProperty::WorldWidth);
                properties[count].local_height          = material->GetProperty(MaterialProperty::WorldHeight);
                properties[count].color.x               = material->GetProperty(MaterialProperty::ColorR);
                properties[count].color.y               = material->GetProperty(MaterialProperty::ColorG);
                properties[count].color.z               = material->GetProperty(MaterialProperty::ColorB);
                properties[count].color.w               = material->GetProperty(MaterialProperty::ColorA);
                properties[count].tiling.x              = material->GetProperty(MaterialProperty::TextureTilingX);
                properties[count].tiling.y              = material->GetProperty(MaterialProperty::TextureTilingY);
                properties[count].offset.x              = material->GetProperty(MaterialProperty::TextureOffsetX);
                properties[count].offset.y              = material->GetProperty(MaterialProperty::TextureOffsetY);
                properties[count].invert_uv.x           = material->GetProperty(MaterialProperty::TextureInvertX);
                properties[count].invert_uv.y           = material->GetProperty(MaterialProperty::TextureInvertY);
                properties[count].roughness             = material->GetProperty(MaterialProperty::Roughness);
                properties[count].metallness            = material->GetProperty(MaterialProperty::Metalness);
                properties[count].normal                = material->GetProperty(MaterialProperty::Normal);
                properties[count].height                = material->GetProperty(MaterialProperty::Height);
                properties[count].anisotropic           = material->GetProperty(MaterialProperty::Anisotropic);
                properties[count].anisotropic_rotation  = material->GetProperty(MaterialProperty::AnisotropicRotation);
                properties[count].clearcoat             = material->GetProperty(MaterialProperty::Clearcoat);
                properties[count].clearcoat_roughness   = material->GetProperty(MaterialProperty::Clearcoat_Roughness);
                properties[count].sheen                 = material->GetProperty(MaterialProperty::Sheen);
                properties[count].subsurface_scattering = material->GetProperty(MaterialProperty::SubsurfaceScattering);
                properties[count].world_space_uv        = material->GetProperty(MaterialProperty::WorldSpaceUv);
                properties[count].uv_rotation           = material->GetProperty(MaterialProperty::TextureRotation);

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

            count += static_cast<uint32_t>(MaterialTextureType::Max) * Material::slots_per_texture;
        };
    
        auto update_entities = [update_material]()
        {
            for (Entity* entity : World::GetEntitiesRenderables())
            {
                if (Material* material = entity->GetComponent<Render>()->GetMaterial())
                {
                    update_material(material);
                }
            }
        };
    
        // cpu
        {
            properties.fill(Sb_Material{});
            m_bindless_textures.fill(nullptr);
            unique_material_ids.clear();
            update_entities();
        }
    
        // gpu
        {
            RHI_Buffer* buffer = Renderer::GetBuffer(Renderer_Buffer::MaterialParameters);
            buffer->ResetOffset();
            buffer->Update(cmd_list, &properties[0], buffer->GetStride() * count);
        }
    }

    void Renderer::UpdateLights(RHI_CommandList* cmd_list)
    {
        m_bindless_lights.fill(Sb_Light());
        
        m_count_active_lights    = 0; 
        Light* first_directional = nullptr;
    
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
            light_buffer_entry.color                             = light_component->GetColor();
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
        for (Entity* entity : World::GetEntitiesLights())
        {
            if (Light* light_component = entity->GetComponent<Light>())
            {
                if (light_component == first_directional)
                    continue;
    
                light_component->SetIndex(numeric_limits<uint32_t>::max());
    
                if (!light_component->GetEntity()->GetActive())
                    continue;
    
                if (light_component->GetIntensityRadiometric() <= 0.0f)
                    continue;
    
                if (Camera* camera = World::GetCamera())
                {
                    if (!camera->IsInViewFrustum(light_component->GetBoundingBox()))
                        continue;
                }
    
                if (!light_component->IsActiveByDistance())
                    continue;
    
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
    }

    void Renderer::UpdateBoundingBoxes(RHI_CommandList* cmd_list)
    {
        m_bindless_aabbs.fill(Sb_Aabb());

        // prepass aabbs (must match the indexing in indirect_cull.hlsl)
        for (uint32_t i = 0; i < m_draw_calls_prepass_count; i++)
        {
            const Renderer_DrawCall& draw_call = m_draw_calls_prepass[i];
            Render* renderable             = draw_call.renderable;
            const BoundingBox& aabb            = renderable->GetBoundingBox();
            m_bindless_aabbs[i].min            = aabb.GetMin();
            m_bindless_aabbs[i].max            = aabb.GetMax();
            m_bindless_aabbs[i].is_occluder    = draw_call.is_occluder;
        }

        // indirect draw aabbs (stored right after prepass aabbs, one per renderable not per meshlet)
        {
            uint32_t indirect_idx = 0;
            for (uint32_t i = 0; i < m_draw_call_count && indirect_idx < m_indirect_renderable_count; i++)
            {
                const Renderer_DrawCall& dc = m_draw_calls[i];
                Render* renderable          = dc.renderable;
                Material* material          = renderable->GetMaterial();

                if (!material || material->IsTransparent())
                    continue;
                if (material->GetProperty(MaterialProperty::Tessellation) > 0.0f)
                    continue;
                if (renderable->GetIndexCount(dc.lod_index) == 0)
                    continue;
                if (!dc.camera_visible)
                    continue;

                uint32_t aabb_slot = m_draw_calls_prepass_count + indirect_idx;
                if (aabb_slot < rhi_max_array_size)
                {
                    const BoundingBox& aabb         = renderable->GetBoundingBox();
                    m_bindless_aabbs[aabb_slot].min = aabb.GetMin();
                    m_bindless_aabbs[aabb_slot].max = aabb.GetMax();
                }
                indirect_idx++;
            }
        }

        // gpu upload to the current frame's region within the shared aabb buffer
        uint32_t total_aabb_count = m_draw_calls_prepass_count + m_indirect_renderable_count;
        if (total_aabb_count > 0)
        {
            RHI_Buffer* buffer         = GetBuffer(Renderer_Buffer::AABBs);
            uint32_t frame_byte_offset = m_frame_resource_index * rhi_max_array_size * static_cast<uint32_t>(sizeof(Sb_Aabb));
            uint32_t upload_size       = static_cast<uint32_t>(sizeof(Sb_Aabb)) * total_aabb_count;
            cmd_list->UpdateBuffer(buffer, frame_byte_offset, upload_size, &m_bindless_aabbs[0]);
        }
    }

    void Renderer::UpdateDrawCalls(RHI_CommandList* cmd_list)
    {
        // reset every counter before the loading early out so indirect passes never read stale bindless geometry
        m_draw_call_count           = 0;
        m_draw_calls_prepass_count  = 0;
        m_draw_data_count           = 0;
        m_indirect_draw_count       = 0;
        m_indirect_renderable_count = 0;
        m_cull_task_count           = 0;
        m_transparents_present      = false;
        if (ProgressTracker::IsLoading())
            return;

        // collect draw calls
        {
            for (Entity* entity : World::GetEntitiesRenderables())
            {
                Render* renderable = entity->GetComponent<Render>();
                Material* material = renderable->GetMaterial();
                if (!material)
                    continue;

                if (material->IsTransparent())
                {
                    m_transparents_present = true;
                }

                uint32_t draw_data_index = WriteDrawData(
                    entity->GetMatrix(),
                    entity->GetMatrixPrevious(),
                    material->GetIndex(),
                    material->IsTransparent() ? 1 : 0
                );

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

            // sort: opaque before transparent, then material, then distance
            sort(m_draw_calls.begin(), m_draw_calls.begin() + m_draw_call_count, [](const Renderer_DrawCall& a, const Renderer_DrawCall& b)
            {
                bool a_transparent = a.renderable->GetMaterial()->IsTransparent();
                bool b_transparent = b.renderable->GetMaterial()->IsTransparent();
                if (a_transparent != b_transparent)
                {
                    return !a_transparent;
                }

                uint64_t a_material_id = a.renderable->GetMaterial()->GetObjectId();
                uint64_t b_material_id = b.renderable->GetMaterial()->GetObjectId();
                if (a_material_id != b_material_id)
                {
                    return a_material_id < b_material_id;
                }

                if (!a_transparent)
                {
                    return a.distance_squared < b.distance_squared;
                }
                else
                {
                    return a.distance_squared > b.distance_squared;
                }
            });
        }

        // prepass: visible opaques, sorted by alpha test then distance
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
                bool a_alpha = a.renderable->GetMaterial()->IsAlphaTested();
                bool b_alpha = b.renderable->GetMaterial()->IsAlphaTested();
                if (a_alpha != b_alpha)
                {
                    return !a_alpha;
                }
                return a.distance_squared < b.distance_squared;
            });
        }

        // indirect draw buffers (gpu-driven path)
        // one input draw entry per renderable lod, one cull task per (renderable, meshlet) tuple
        // surviving meshlets are atomically compacted by the cull shader into meshlet_instances and rendered with a single indirect draw
        {
            m_indirect_draw_count       = 0;
            m_indirect_renderable_count = 0;
            m_cull_task_count           = 0;
            uint32_t aabb_frame_offset  = m_frame_resource_index * rhi_max_array_size;

            for (uint32_t i = 0; i < m_draw_call_count; i++)
            {
                const Renderer_DrawCall& dc = m_draw_calls[i];
                Render* renderable          = dc.renderable;
                Material* material          = renderable->GetMaterial();

                if (!material || material->IsTransparent())
                    continue;
                if (!dc.camera_visible)
                    continue;
                if (material->GetProperty(MaterialProperty::Tessellation) > 0.0f)
                    continue;

                uint32_t lod_index_count = renderable->GetIndexCount(dc.lod_index);
                if (lod_index_count == 0)
                    continue;

                uint32_t lod_meshlet_count = renderable->GetMeshletCount(dc.lod_index);
                if (lod_meshlet_count == 0)
                    continue;

                bool is_instanced  = dc.instance_count > 1;
                uint32_t inst_n    = is_instanced ? dc.instance_count : 1;

                // hw-instancing fallback when per-instance fanout would overflow the cull-task budget
                // the cull task carries instance_count and the cull shader emits N MeshletInstances on survival
                uint32_t per_instance_tasks_add = lod_meshlet_count * inst_n;
                uint32_t hw_instanced_tasks_add = lod_meshlet_count;
                bool use_hw_instancing          = is_instanced && (m_cull_task_count + per_instance_tasks_add > renderer_max_cull_tasks);
                uint32_t actual_tasks_add       = use_hw_instancing ? hw_instanced_tasks_add : per_instance_tasks_add;

                if (m_indirect_draw_count + 1 > renderer_max_indirect_draws)
                    continue;
                if (m_cull_task_count + actual_tasks_add > renderer_max_cull_tasks)
                    continue;

                uint32_t renderable_aabb_slot = aabb_frame_offset + m_draw_calls_prepass_count + m_indirect_renderable_count;
                uint32_t base_first_index     = renderable->GetIndexOffset(dc.lod_index);
                uint32_t vertex_offset        = renderable->GetVertexOffset(dc.lod_index);
                uint32_t base_meshlet_index   = renderable->GetGlobalMeshletOffset() + renderable->GetMeshletOffset(dc.lod_index);

                Entity* entity = renderable->GetEntity();
                Mesh* mesh     = renderable->GetMesh();

                // flags bit 0 skinned (cull falls back to per-renderable aabb, triangle pass skips backface), bit 1 per-instance (cone+sphere fan out via task.instance_index), bit 2 hw instancing (single task fans into N writes, cull falls back to per-renderable aabb), bit 3 two-sided material (triangle pass skips backface)
                bool is_skinned        = mesh->IsSkinned() && cvar_meshlet_cull_skinned.GetValueAs<bool>() == false;
                bool use_per_instance  = is_instanced && !use_hw_instancing;
                bool is_two_sided      = static_cast<RHI_CullMode>(material->GetProperty(MaterialProperty::CullMode)) != RHI_CullMode::Back;
                uint32_t base_flags    = 0u;
                if (is_skinned)        base_flags |= 1u;
                if (use_per_instance)  base_flags |= 2u;
                if (use_hw_instancing) base_flags |= 4u;
                if (is_two_sided)      base_flags |= 8u;

                uint32_t draw_idx       = m_indirect_draw_count++;
                Sb_DrawData& data       = m_indirect_draw_data[draw_idx];
                data.transform          = entity->GetMatrix();
                data.transform_previous = entity->GetMatrixPrevious();
                data.material_index     = material->GetIndex();
                data.is_transparent     = 0;
                data.aabb_index         = renderable_aabb_slot;
                data.lod_first_index    = base_first_index;
                data.flags              = base_flags;
                data.instance_offset    = renderable->GetGlobalInstanceOffset();
                data.instance_index     = 0;
                data.lod_vertex_offset  = vertex_offset;

                // emit one cull task per meshlet, fanning out per-instance tasks unless we fell back to hw-instancing
                uint32_t instances_per_task = use_hw_instancing ? inst_n : 1u;
                uint32_t task_instance_iter = use_hw_instancing ? 1u    : inst_n;

                for (uint32_t m = 0; m < lod_meshlet_count; m++)
                {
                    uint32_t global_meshlet = base_meshlet_index + m;
                    for (uint32_t inst = 0; inst < task_instance_iter; inst++)
                    {
                        Sb_CullTask& task   = m_cull_tasks[m_cull_task_count++];
                        task.draw_index     = draw_idx;
                        task.meshlet_index  = global_meshlet;
                        task.instance_index = inst;
                        task.instance_count = instances_per_task;
                    }
                }

                m_indirect_renderable_count++;
            }
        }

        // select occluders (top N by screen area, with temporal hysteresis)
        {
            static unordered_set<Render*> previous_occluders;

            auto compute_screen_space_area = [&](const BoundingBox& aabb_world) -> float
            {
                float area = 0.0f;
                if (Camera* camera = World::GetCamera())
                {
                    math::Rectangle rect_screen = camera->WorldToScreenCoordinates(aabb_world);
                    area = clamp(rect_screen.width * rect_screen.height, 0.0f, numeric_limits<float>::max());
                }
                return area;
            };

            struct DrawCallArea
            {
                uint32_t index;
                float area;
            };
            static vector<DrawCallArea> areas;
            areas.clear();
            areas.reserve(m_draw_calls_prepass_count);

            for (uint32_t i = 0; i < m_draw_calls_prepass_count; i++)
            {
                Renderer_DrawCall& draw_call = m_draw_calls_prepass[i];
                Render* renderable = draw_call.renderable;
                Material* material = renderable->GetMaterial();

                if (!material || material->IsTransparent() || renderable->HasInstancing() || !draw_call.camera_visible)
                    continue;

                float screen_area = compute_screen_space_area(renderable->GetBoundingBox());

                // temporal hysteresis: bonus for previous occluders
                if (previous_occluders.find(renderable) != previous_occluders.end())
                {
                    screen_area *= 1.5f;
                }

                areas.push_back({ i, screen_area });
            }

            sort(areas.begin(), areas.end(), [](const DrawCallArea& a, const DrawCallArea& b)
            {
                return a.area > b.area;
            });

            const uint32_t max_occluders = 64;
            uint32_t occluder_count = min(max_occluders, static_cast<uint32_t>(areas.size()));

            previous_occluders.clear();
            for (uint32_t i = 0; i < occluder_count; i++)
            {
                m_draw_calls_prepass[areas[i].index].is_occluder = true;
                previous_occluders.insert(m_draw_calls_prepass[areas[i].index].renderable);
            }
        }
    }

    void Renderer::UpdateAccelerationStructures(RHI_CommandList* cmd_list)
    {
        bool ray_tracing_enabled = cvar_ray_traced_reflections.GetValueAs<bool>() || cvar_ray_traced_shadows.GetValueAs<bool>() || cvar_restir_pt.GetValueAs<bool>();
        if (!ray_tracing_enabled)
            return;

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
                Render* renderable = entity->GetComponent<Render>();
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
        if (!blas_burst_done)
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
                Render* renderable = entity->GetComponent<Render>();
                Material* material = renderable->GetMaterial();
                if (!material)
                    continue;

                uint64_t device_address = renderable->GetAccelerationStructureDeviceAddress();
                if (device_address == 0)
                    continue;

                RHI_Buffer* vertex_buffer = renderable->GetVertexBuffer();
                RHI_Buffer* index_buffer  = renderable->GetIndexBuffer();
                if (!vertex_buffer || !index_buffer)
                    continue;

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
                continue;
            // skip lights that are out of shadow distance, the shader and shadow pass
            // both rely on rect.IsDefined() so leaving them unallocated is enough
            if (!light->IsShadowEffective())
                continue;
            for (uint32_t i = 0; i < light->GetSliceCount(); ++i)
            {
                m_shadow_slices.emplace_back(light, i, 0, math::Rectangle::Zero);
            }
        }
        if (m_shadow_slices.empty())
            return;

        // row-based packing: lays out uniform-sized slices left-to-right, wrapping to the next row.
        // when rects is null it only tests whether the layout fits; when non-null it writes the rectangles.
        auto pack_row = [&](uint32_t slice_res, uint32_t num_slices, vector<ShadowSlice>* rects) -> bool
        {
            if (slice_res > resolution_atlas)
                return false;

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
                    return false;

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
                    low = mid;
                else
                    high = mid - 1;
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

    static void screenshot_internal(string file_path = "")
    {
        RHI_Texture* frame_output = Renderer::GetRenderTarget(Renderer_RenderTarget::frame_output);
        uint32_t width            = frame_output->GetWidth();
        uint32_t height           = frame_output->GetHeight();
        uint32_t bits_per_channel = frame_output->GetBitsPerChannel();
        uint32_t channel_count    = frame_output->GetChannelCount();
        size_t data_size          = static_cast<size_t>(width) * height * (bits_per_channel / 8) * channel_count;

        bool is_hdr = cvar_hdr.GetValueAs<bool>();

        auto staging = make_shared<RHI_Buffer>(RHI_Buffer_Type::Constant, data_size, 1, nullptr, true, "screenshot_staging");

        if (RHI_CommandList* cmd_list = RHI_CommandList::ImmediateExecutionBegin(RHI_Queue_Type::Graphics))
        {
            cmd_list->CopyTextureToBuffer(frame_output, staging.get());
            RHI_CommandList::ImmediateExecutionEnd(cmd_list);
        }

        void* mapped_data = staging->GetMappedData();
        SP_ASSERT_MSG(mapped_data, "Staging buffer not mappable");

        spartan::ThreadPool::AddTask([=]()
        {
            if (!file_path.empty())
            {
                string directory = FileSystem::GetDirectoryFromFilePath(file_path);
                if (!directory.empty() && !FileSystem::Exists(directory))
                {
                    FileSystem::CreateDirectory_(directory);
                }

                string temp_file_path = file_path + ".tmp";
                if (FileSystem::Exists(temp_file_path))
                {
                    FileSystem::Delete(temp_file_path);
                }

                SP_LOG_INFO("Saving screenshot to '%s'...", file_path.c_str());
                ImageImporter::SaveSdr(temp_file_path, width, height, channel_count, bits_per_channel, mapped_data, is_hdr);
                if (FileSystem::Exists(file_path))
                {
                    FileSystem::Delete(file_path);
                }
                FileSystem::Rename(temp_file_path, file_path);
                SP_LOG_INFO("Screenshot saved as '%s'", file_path.c_str());
                return;
            }

            static uint32_t screenshot_index = 0;
            uint32_t index = screenshot_index++;
            string exr_path = "screenshot_" + to_string(index) + ".exr";
            string png_path = "screenshot_" + to_string(index) + ".png";

            SP_LOG_INFO("Saving screenshots...");
            ImageImporter::Save(exr_path, width, height, channel_count, bits_per_channel, mapped_data);
            ImageImporter::SaveSdr(png_path, width, height, channel_count, bits_per_channel, mapped_data, is_hdr);
            SP_LOG_INFO("Screenshots saved as '%s' and '%s'", exr_path.c_str(), png_path.c_str());
        });
    }

    void Renderer::Screenshot()
    {
        screenshot_internal();
    }

    void Renderer::Screenshot(const string& file_path)
    {
        screenshot_internal(file_path);
    }

    RHI_AccelerationStructure* Renderer::GetTopLevelAccelerationStructure()
    {
        return m_tlas.get();
    }

    void Renderer::DestroyAccelerationStructures()
    {
        RHI_Device::QueueWaitAll();

        m_tlas = nullptr;

        SP_LOG_INFO("Acceleration structures destroyed for world change");
    }
}
