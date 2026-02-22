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
#include "Renderer.h"
#include "Material.h"
#include "GeometryBuffer.h"
#include "ThreadPool.h"
#include "../Profiling/RenderDoc.h"
#include "../Profiling/Profiler.h"
#include "../Core/Debugging.h"
#include "../Core/Window.h"
#include "../Core/Timer.h"
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

        void dynamic_resolution()
        {
            if (cvar_dynamic_resolution.GetValue() != 0.0f)
            {
                float gpu_time_target   = 16.67f;                                               // target for 60 FPS
                float adjustment_factor = static_cast<float>(0.05f * Timer::GetDeltaTimeSec()); // how aggressively to adjust screen percentage
                float screen_percentage = cvar_resolution_scale.GetValue();
                float gpu_time          = Profiler::GetTimeGpuLast();

                if (gpu_time < gpu_time_target) // gpu is under target, increase resolution
                {
                    screen_percentage += adjustment_factor * (gpu_time_target - gpu_time);
                }
                else // gpu is over target, decrease resolution
                {
                    screen_percentage -= adjustment_factor * (gpu_time - gpu_time_target);
                }

                // clamp screen_percentage to a reasonable range
                screen_percentage = clamp(screen_percentage, 0.5f, 1.0f);

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

            // default wind
            {
                float rotation_y      = 120.0f * math::deg_to_rad;
                const float intensity = 3.0f; // meters per second
                SetWind(Vector3(sin(rotation_y), 0.0f, cos(rotation_y)) * intensity);
            }
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

        RHI_VendorTechnology::NRD_Shutdown();

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

        {
            swapchain->AcquireNextImage();
            RHI_Device::Tick(frame_num);
            RHI_VendorTechnology::Tick(&m_cb_frame_cpu, GetResolutionRender(), GetResolutionOutput(), cvar_resolution_scale.GetValue());
            dynamic_resolution();

            // breadcrumbs
            if (Debugging::IsBreadcrumbsEnabled())
            {
                Breadcrumbs::StartFrame();
            }
        }
        
        // recreate optional render targets when feature cvars change
        if (m_initialized_resources)
        {
            static uint32_t options_hash = 0;
            uint32_t options_hash_new    = (cvar_ssao.GetValueAs<bool>() << 0) | (cvar_ray_traced_reflections.GetValueAs<bool>() << 1) | (cvar_restir_pt.GetValueAs<bool>() << 2);
            
            if (options_hash_new != options_hash)
            {
                RHI_Device::QueueWaitAll(true);
                RHI_Device::DeletionQueueParse();
                UpdateOptionalRenderTargets();
                RHI_Device::DeletionQueueParse();
                options_hash = options_hash_new;
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
            if (!is_loading)
            {
                GeometryBuffer::BuildIfDirty();
            }

            // geometry buffer rebuild invalidates blas device addresses
            if (GeometryBuffer::WasRebuilt())
            {
                DestroyAccelerationStructures();
            }

            // rotate per-frame buffers to avoid cpu-gpu races
            RotateFrameBuffers();

            UpdateDrawCalls(m_cmd_list_present);

            if (!is_loading)
            {
                UpdateAccelerationStructures(m_cmd_list_compute);
            }
    
            // periodic resource cleanup
            {
                m_resource_index++;
                bool is_sync_point = m_resource_index == renderer_resource_frame_lifetime;
                if (is_sync_point)
                {
                    m_resource_index = 0;
    
                    if (RHI_Device::DeletionQueueNeedsToParse())
                    {
                        RHI_Device::QueueWaitAll();
                        RHI_Device::DeletionQueueParse();
                    }
    
                    GetBuffer(Renderer_Buffer::ConstantFrame)->ResetOffset();
                }
            }
    
            // bindless resource updates
            if (!is_loading)
            {
                bool initialize = GetFrameNumber() == 0;

                // lights
                if (initialize || World::HaveLightsChangedThisFrame())
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
                    RHI_Device::UpdateBindlessAABBs(GetBuffer(Renderer_Buffer::AABBs));
                }

                // draw data
                {
                    if (m_draw_data_count > 0)
                    {
                        RHI_Buffer* buffer = GetBuffer(Renderer_Buffer::DrawData);
                        buffer->ResetOffset();
                        buffer->Update(m_cmd_list_present, &m_draw_data_cpu[0], buffer->GetStride() * m_draw_data_count);
                    }

                    // descriptor must follow the rotated buffer
                    RHI_Device::UpdateBindlessDrawData(GetBuffer(Renderer_Buffer::DrawData));
                }

                // indirect draw buffers
                if (m_indirect_draw_count > 0)
                {
                    RHI_Buffer* args_buffer = GetBuffer(Renderer_Buffer::IndirectDrawArgs);
                    args_buffer->ResetOffset();
                    args_buffer->Update(m_cmd_list_present, &m_indirect_draw_args[0], args_buffer->GetStride() * m_indirect_draw_count);

                    RHI_Buffer* data_buffer = GetBuffer(Renderer_Buffer::IndirectDrawData);
                    data_buffer->ResetOffset();
                    data_buffer->Update(m_cmd_list_present, &m_indirect_draw_data[0], data_buffer->GetStride() * m_indirect_draw_count);

                    // reset count, the cull shader atomically increments it
                    uint32_t zero = 0;
                    RHI_Buffer* count_buffer = GetBuffer(Renderer_Buffer::IndirectDrawCount);
                    count_buffer->ResetOffset();
                    count_buffer->Update(m_cmd_list_present, &zero, sizeof(uint32_t));
                }
            }
    
            UpdateFrameConstantBuffer(m_cmd_list_present);
            UpdatePersistentLines();
            AddLinesToBeRendered();
        }

        // xr
        bool xr_should_render = false;
        if (Xr::IsSessionRunning())
        {
            xr_should_render = Xr::BeginFrame();
        }

        {
            if (can_render)
            {
                ProduceFrame(m_cmd_list_present, m_cmd_list_compute);
            }
        }

        if (xr_should_render && can_render)
        {
            BlitToXrSwapchain(m_cmd_list_present, GetRenderTarget(Renderer_RenderTarget::frame_output));
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
                m_cb_frame_cpu.view_inv            = Matrix::Invert(m_cb_frame_cpu.view);
                m_cb_frame_cpu.projection_previous = m_cb_frame_cpu.projection;
                m_cb_frame_cpu.projection          = camera->GetProjectionMatrix();
                m_cb_frame_cpu.projection_inv      = Matrix::Invert(m_cb_frame_cpu.projection);
            }

            if (dirty_orthographic_projection)
            { 
                // near = 0 for ortho (avoids NaN in [3,2] element)
                Matrix projection_ortho              = Matrix::CreateOrthographicLH(m_viewport.width, m_viewport.height, 0.0f, far_plane);
                m_cb_frame_cpu.view_projection_ortho = Matrix::CreateLookAtLH(Vector3(0, 0, -near_plane), Vector3::Forward, Vector3::Up) * projection_ortho;
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
        m_cb_frame_cpu.view_projection_inv      = Matrix::Invert(m_cb_frame_cpu.view_projection);
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
        m_cb_frame_cpu.resolution_scale    = cvar_resolution_scale.GetValue();
        m_cb_frame_cpu.hdr_enabled         = cvar_hdr.GetValueAs<bool>() ? 1.0f : 0.0f;
        m_cb_frame_cpu.hdr_max_nits        = Display::GetLuminanceMax();
        m_cb_frame_cpu.gamma               = cvar_gamma.GetValue();
        m_cb_frame_cpu.camera_exposure     = World::GetCamera() ? World::GetCamera()->GetExposure() : 1.0f;

        m_cb_frame_cpu.cloud_coverage = cvar_cloud_coverage.GetValue();
        m_cb_frame_cpu.cloud_shadows  = cvar_cloud_shadows.GetValue();
        // feature bits (must match common_resources.hlsl)
        m_cb_frame_cpu.set_bit(cvar_ray_traced_reflections.GetValueAs<bool>(), 1 << 0);
        m_cb_frame_cpu.set_bit(cvar_ssao.GetValueAs<bool>(),                   1 << 1);
        m_cb_frame_cpu.set_bit(cvar_ray_traced_shadows.GetValueAs<bool>(),     1 << 2);
        m_cb_frame_cpu.set_bit(cvar_restir_pt.GetValueAs<bool>(),              1 << 3);

        GetBuffer(Renderer_Buffer::ConstantFrame)->Update(cmd_list, &m_cb_frame_cpu);
    }

    const Vector3& Renderer::GetWind()
    {
        return m_cb_frame_cpu.wind;
    }

    void Renderer::SetWind(const math::Vector3& wind)
    {
        m_cb_frame_cpu.wind = wind;
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
            SetViewport(static_cast<float>(width), static_cast<float>(height));

            width_previous_output  = static_cast<uint32_t>(m_viewport.width);
            height_previous_output = static_cast<uint32_t>(m_viewport.height);
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
                m_cmd_list_present->InsertBarrier(swapchain->GetRhiRt(), swapchain->GetFormat(), 0, 1, 1, RHI_Image_Layout::Present_Source);
                
                m_cmd_list_present->Submit(swapchain->GetImageAcquiredSemaphore(), false, swapchain->GetRenderingCompleteSemaphore());
                swapchain->Present(m_cmd_list_present);
            }
            else
            {
                m_cmd_list_present->Submit(nullptr, true);
            }
        }
        Profiler::TimeBlockEnd();
    }

    RHI_Api_Type Renderer::GetRhiApiType()
    {
        return RHI_Context::api_type;
    }

    uint64_t Renderer::GetFrameNumber()
    {
        return frame_num;
    }

    bool Renderer::IsCpuDrivenDraw(const Renderer_DrawCall& draw_call, Material* material)
    {
        bool is_tessellated  = material->GetProperty(MaterialProperty::Tessellation) > 0.0f;
        bool is_instanced    = draw_call.instance_count > 1;
        bool is_alpha_tested = material->IsAlphaTested();
        bool is_non_standard_cull = static_cast<RHI_CullMode>(material->GetProperty(MaterialProperty::CullMode)) != RHI_CullMode::Back;
        return is_tessellated || is_instanced || is_alpha_tested || is_non_standard_cull;
    }

    void Renderer::SetCommonTextures(RHI_CommandList* cmd_list)
    {
        // gbuffer
        cmd_list->SetTexture(Renderer_BindingsSrv::gbuffer_albedo,   GetRenderTarget(Renderer_RenderTarget::gbuffer_color));
        cmd_list->SetTexture(Renderer_BindingsSrv::gbuffer_normal,   GetRenderTarget(Renderer_RenderTarget::gbuffer_normal));
        cmd_list->SetTexture(Renderer_BindingsSrv::gbuffer_material, GetRenderTarget(Renderer_RenderTarget::gbuffer_material));
        cmd_list->SetTexture(Renderer_BindingsSrv::gbuffer_velocity, GetRenderTarget(Renderer_RenderTarget::gbuffer_velocity));
        cmd_list->SetTexture(Renderer_BindingsSrv::gbuffer_depth,    GetRenderTarget(Renderer_RenderTarget::gbuffer_depth));

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
        entry.padding            = 0;

        // write directly to the mapped gpu buffer
        RHI_Buffer* buffer = GetBuffer(Renderer_Buffer::DrawData);
        if (void* mapped = buffer->GetMappedData())
        {
            void* dst = static_cast<char*>(mapped) + index * sizeof(Sb_DrawData);
            memcpy(dst, &entry, sizeof(Sb_DrawData));
        }

        return index;
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
                properties[count].tiling_uv.x           = material->GetProperty(MaterialProperty::TextureTilingX);
                properties[count].tiling_uv.y           = material->GetProperty(MaterialProperty::TextureTilingY);
                properties[count].offset_uv.x           = material->GetProperty(MaterialProperty::TextureOffsetX);
                properties[count].offset_uv.y           = material->GetProperty(MaterialProperty::TextureOffsetY);
                properties[count].invert_uv.x           = material->GetProperty(MaterialProperty::TextureInvertX);
                properties[count].invert_uv.y           = material->GetProperty(MaterialProperty::TextureInvertY);
                properties[count].roughness_mul         = material->GetProperty(MaterialProperty::Roughness);
                properties[count].metallic_mul          = material->GetProperty(MaterialProperty::Metalness);
                properties[count].normal_mul            = material->GetProperty(MaterialProperty::Normal);
                properties[count].height_mul            = material->GetProperty(MaterialProperty::Height);
                properties[count].anisotropic           = material->GetProperty(MaterialProperty::Anisotropic);
                properties[count].anisotropic_rotation  = material->GetProperty(MaterialProperty::AnisotropicRotation);
                properties[count].clearcoat             = material->GetProperty(MaterialProperty::Clearcoat);
                properties[count].clearcoat_roughness   = material->GetProperty(MaterialProperty::Clearcoat_Roughness);
                properties[count].sheen                 = material->GetProperty(MaterialProperty::Sheen);
                properties[count].subsurface_scattering = material->GetProperty(MaterialProperty::SubsurfaceScattering);
                properties[count].world_space_uv        = material->GetProperty(MaterialProperty::WorldSpaceUv);

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
            for (Entity* entity : World::GetEntities())
            {
                if (entity->GetActive())
                {
                    if (Renderable* renderable = entity->GetComponent<Renderable>())
                    {
                        if (Material* material = renderable->GetMaterial())
                        {
                            update_material(material);
                        }
                    }
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
        const Entity* camera_entity = World::GetCamera() ? World::GetCamera()->GetEntity() : nullptr;
        const Vector3 camera_pos    = camera_entity ? camera_entity->GetPosition() : Vector3::Zero;
    
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
                light_buffer_entry.view_projection[i] = light_component->GetViewProjectionMatrix(i);
            }
    
            light_buffer_entry.screen_space_shadows_slice_index  = light_component->GetScreenSpaceShadowsSliceIndex();
            light_buffer_entry.intensity                         = light_component->GetIntensityWatt();
            light_buffer_entry.range                             = light_component->GetRange();
            light_buffer_entry.angle                             = light_component->GetAngle();
            light_buffer_entry.color                             = light_component->GetColor();
            light_buffer_entry.position                          = light_component->GetEntity()->GetPosition();
            light_buffer_entry.direction                         = light_component->GetEntity()->GetForward();
            light_buffer_entry.area_width                        = light_component->GetAreaWidth();
            light_buffer_entry.area_height                       = light_component->GetAreaHeight();
            light_buffer_entry.flags                             = 0;
            light_buffer_entry.flags                            |= light_component->GetLightType() == LightType::Directional ? (1 << 0) : 0;
            light_buffer_entry.flags                            |= light_component->GetLightType() == LightType::Point       ? (1 << 1) : 0;
            light_buffer_entry.flags                            |= light_component->GetLightType() == LightType::Spot        ? (1 << 2) : 0;
            light_buffer_entry.flags                            |= light_component->GetFlag(LightFlags::Shadows)             ? (1 << 3) : 0;
            light_buffer_entry.flags                            |= light_component->GetFlag(LightFlags::ShadowsScreenSpace)  ? (1 << 4) : 0;
            light_buffer_entry.flags                            |= light_component->GetFlag(LightFlags::Volumetric)          ? (1 << 5) : 0;
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
    
                if (light_component->GetIntensityWatt() <= 0.0f)
                    continue;
    
                if (Camera* camera = World::GetCamera())
                {
                    if (!camera->IsInViewFrustum(light_component->GetBoundingBox()))
                        continue;
                }
    
                if (light_component->GetLightType() != LightType::Directional)
                {
                    const float distance_squared      = Vector3::DistanceSquared(light_component->GetEntity()->GetPosition(), camera_pos);
                    const float draw_distance_squared = light_component->GetDrawDistance() * light_component->GetDrawDistance();
                    if (distance_squared > draw_distance_squared)
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
    }

    void Renderer::UpdateBoundingBoxes(RHI_CommandList* cmd_list)
    {
        m_bindless_aabbs.fill(Sb_Aabb());

        // prepass aabbs (must match the indexing in indirect_cull.hlsl)
        for (uint32_t i = 0; i < m_draw_calls_prepass_count; i++)
        {
            const Renderer_DrawCall& draw_call = m_draw_calls_prepass[i];
            Renderable* renderable             = draw_call.renderable;
            const BoundingBox& aabb            = renderable->GetBoundingBox();
            m_bindless_aabbs[i].min            = aabb.GetMin();
            m_bindless_aabbs[i].max            = aabb.GetMax();
            m_bindless_aabbs[i].is_occluder    = draw_call.is_occluder;
        }

        // indirect draw aabbs (stored right after prepass aabbs)
        {
            uint32_t indirect_idx = 0;
            for (uint32_t i = 0; i < m_draw_call_count && indirect_idx < m_indirect_draw_count; i++)
            {
                const Renderer_DrawCall& dc = m_draw_calls[i];
                Material* material          = dc.renderable->GetMaterial();

                if (!material || material->IsTransparent())
                    continue;
                if (IsCpuDrivenDraw(dc, material))
                    continue;

                uint32_t aabb_slot = m_draw_calls_prepass_count + indirect_idx;
                if (aabb_slot < rhi_max_array_size)
                {
                    const BoundingBox& aabb       = dc.renderable->GetBoundingBox();
                    m_bindless_aabbs[aabb_slot].min = aabb.GetMin();
                    m_bindless_aabbs[aabb_slot].max = aabb.GetMax();
                }
                indirect_idx++;
            }
        }

        // gpu upload
        uint32_t total_aabb_count = m_draw_calls_prepass_count + m_indirect_draw_count;
        RHI_Buffer* buffer = GetBuffer(Renderer_Buffer::AABBs);
        buffer->ResetOffset();
        buffer->Update(cmd_list, &m_bindless_aabbs[0], buffer->GetStride() * total_aabb_count);
    }

    void Renderer::UpdateDrawCalls(RHI_CommandList* cmd_list)
    {
        m_draw_call_count          = 0;
        m_draw_calls_prepass_count = 0;
        m_draw_data_count          = 0;
        m_transparents_present     = false;
        if (ProgressTracker::IsLoading())
            return;

        // collect draw calls
        {
            for (Entity* entity : World::GetEntities())
            {
                if (!entity->GetActive())
                    continue;

                if (Renderable* renderable = entity->GetComponent<Renderable>())
                {
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
        {
            m_indirect_draw_count = 0;
            for (uint32_t i = 0; i < m_draw_call_count; i++)
            {
                const Renderer_DrawCall& dc = m_draw_calls[i];
                Renderable* renderable      = dc.renderable;
                Material* material          = renderable->GetMaterial();

                if (!material || material->IsTransparent())
                    continue;
                if (IsCpuDrivenDraw(dc, material))
                    continue;

                uint32_t idx = m_indirect_draw_count++;
                if (idx >= rhi_max_array_size)
                    break;

                Sb_IndirectDrawArgs& args = m_indirect_draw_args[idx];
                args.index_count          = renderable->GetIndexCount(dc.lod_index);
                args.instance_count       = dc.instance_count;
                args.first_index          = renderable->GetIndexOffset(dc.lod_index);
                args.vertex_offset        = static_cast<int32_t>(renderable->GetVertexOffset(dc.lod_index));
                args.first_instance       = dc.instance_index;

                // per-draw data (aabb_index sits after prepass aabbs)
                Sb_DrawData& data       = m_indirect_draw_data[idx];
                Entity* entity          = renderable->GetEntity();
                data.transform          = entity->GetMatrix();
                data.transform_previous = entity->GetMatrixPrevious();
                data.material_index     = material->GetIndex();
                data.is_transparent     = 0;
                data.aabb_index         = m_draw_calls_prepass_count + idx;
                data.padding            = 0;
            }
        }

        // select occluders (top N by screen area, with temporal hysteresis)
        {
            static unordered_set<Renderable*> previous_occluders;

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
                Renderable* renderable = draw_call.renderable;
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
        {
            uint32_t blas_built   = 0;
            uint32_t blas_skipped = 0;
            for (Entity* entity : World::GetEntities())
            {
                if (!entity->GetActive())
                    continue;

                if (Renderable* renderable = entity->GetComponent<Renderable>())
                {
                    if (!renderable->HasAccelerationStructure())
                    {
                        renderable->BuildAccelerationStructure(cmd_list);
                        if (renderable->HasAccelerationStructure())
                        {
                            blas_built++;
                        }
                        else
                        { 
                            blas_skipped++;
                        }
                    }
                }
            }
            
            if (blas_built > 0 || blas_skipped > 0)
            {
                SP_LOG_INFO("Ray tracing: built %u BLAS, skipped %u (no sub-meshes)", blas_built, blas_skipped);
            }
        }

        // tlas
        {
            if (!m_tlas)
            {
                m_tlas = make_unique<RHI_AccelerationStructure>(RHI_AccelerationStructureType::Top, "world_tlas");
            }

            constexpr uint32_t RHI_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT = 0x00000002; // VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR

            static vector<RHI_AccelerationStructureInstance> instances; // static to avoid per-frame heap alloc
            static vector<Sb_GeometryInfo> geometry_infos;
            instances.clear();
            geometry_infos.clear();

            for (Entity* entity : World::GetEntities())
            {
                if (!entity->GetActive())
                    continue;
    
               if (Renderable* renderable = entity->GetComponent<Renderable>())
                {
                    if (Material* material = renderable->GetMaterial())
                    {
                        uint64_t device_address = renderable->GetAccelerationStructureDeviceAddress();
                        if (device_address == 0)
                            continue;

                        RHI_Buffer* vertex_buffer = renderable->GetVertexBuffer();
                        RHI_Buffer* index_buffer  = renderable->GetIndexBuffer();
                        if (!vertex_buffer || !index_buffer)
                            continue;

                        RHI_CullMode cull_mode = static_cast<RHI_CullMode>(material->GetProperty(MaterialProperty::CullMode));

                        RHI_AccelerationStructureInstance instance           = {};
                        instance.instance_custom_index                       = material->GetIndex(); // for hit shader material lookup
                        instance.mask                                        = 0xFF;                 // visible to all rays
                        instance.instance_shader_binding_table_record_offset = 0;                    // sbt hit group offset
                        instance.flags                                       = cull_mode == RHI_CullMode::None ? RHI_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT : 0;
                        instance.device_address                              = device_address;

                        // row-major 3x4 transform (transpose 3x3 because vulkan uses column vectors)
                        const Matrix& m = renderable->GetEntity()->GetMatrix();
                        instance.transform[0]  = m.m00; instance.transform[1]  = m.m10; instance.transform[2]  = m.m20; instance.transform[3]  = m.m30;
                        instance.transform[4]  = m.m01; instance.transform[5]  = m.m11; instance.transform[6]  = m.m21; instance.transform[7]  = m.m31;
                        instance.transform[8]  = m.m02; instance.transform[9]  = m.m12; instance.transform[10] = m.m22; instance.transform[11] = m.m32;

                        instances.push_back(instance);

                        Sb_GeometryInfo geo_info       = {};
                        geo_info.vertex_buffer_address = vertex_buffer->GetDeviceAddress();
                        geo_info.index_buffer_address  = index_buffer->GetDeviceAddress();
                        geo_info.vertex_offset         = renderable->GetVertexOffset(0);
                        geo_info.index_offset          = renderable->GetIndexOffset(0);
                        geo_info.vertex_count          = renderable->GetVertexCount(0);
                        geo_info.index_count           = renderable->GetIndexCount(0);
                        geometry_infos.push_back(geo_info);
                    }
                }
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
        }
    }

    void Renderer::UpdateShadowAtlas()
    {
        const uint32_t resolution_atlas = GetRenderTarget(Renderer_RenderTarget::shadow_atlas)->GetWidth();
        const uint32_t min_slice_res    = 256;

        // collect slices
        m_shadow_slices.clear();
        for (const auto& entity : World::GetEntitiesLights())
        {
            Light* light                = entity->GetComponent<Light>();
            light->ClearAtlasRectangles();
            if (light->GetIndex() == numeric_limits<uint32_t>::max())
                continue;
            for (uint32_t i = 0; i < light->GetSliceCount(); ++i)
            {
                m_shadow_slices.emplace_back(light, i, 0, math::Rectangle::Zero);
            }
        }
        if (m_shadow_slices.empty())
            return;
    
        uint32_t border = 8;
        auto can_fit = [&](uint32_t test_res, uint32_t num_slices) -> bool
        {
            if (test_res > resolution_atlas)
                return false;
    
            uint32_t x     = 0;
            uint32_t y     = 0;
            uint32_t row_h = 0;
    
            for (uint32_t i = 0; i < num_slices; ++i)
            {
                uint32_t left_pad = (x == 0) ? 0 : border;
                uint32_t placed_x = x + left_pad;
    
                if (placed_x + test_res > resolution_atlas)
                {
                    y        += row_h + border;
                    x         = 0;
                    row_h     = 0;
                    placed_x  = 0;
                }
    
                if (placed_x + test_res > resolution_atlas)
                    return false;
    
                uint32_t placed_y       = y;
                if (placed_y + test_res > resolution_atlas)
                    return false;
    
                x     = placed_x + test_res;
                row_h = max(row_h, test_res);
            }
    
            return true;
        };
    
        // binary search for max uniform slice resolution
        uint32_t max_slice_res = resolution_atlas;
        if (m_shadow_slices.size() > 1)
        {
            uint32_t low  = min_slice_res;
            uint32_t high = resolution_atlas;
            while (low < high)
            {
                uint32_t mid = (low + high + 1) / 2;
                if (can_fit(mid, static_cast<uint32_t>(m_shadow_slices.size())))
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
    
        for (auto& slice : m_shadow_slices)
        {
            slice.res = max_slice_res;
        }
    
        // pack slices
        uint32_t x     = 0;
        uint32_t y     = 0;
        uint32_t row_h = 0;
        for (auto& slice : m_shadow_slices)
        {
            uint32_t left_pad = (x == 0) ? 0 : border;
            uint32_t placed_x = x + left_pad;
    
            if (placed_x + slice.res > resolution_atlas)
            {
                y        += row_h + border;
                x         = 0;
                row_h     = 0;
                placed_x  = 0;
            }
    
            slice.rect = math::Rectangle(
                static_cast<float>(placed_x),
                static_cast<float>(y),
                static_cast<float>(slice.res),
                static_cast<float>(slice.res)
            );
    
            x     = placed_x + slice.res;
            row_h = max(row_h, slice.res);
        }
    
        for (const auto& slice : m_shadow_slices)
        {
            slice.light->SetAtlasRectangle(slice.slice_index, slice.rect);
        }
    }

    void Renderer::Screenshot()
    {
        static uint32_t screenshot_index = 0;

        RHI_Texture* frame_output = GetRenderTarget(Renderer_RenderTarget::frame_output);
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

        uint32_t index = screenshot_index++;
        string exr_path = "screenshot_" + to_string(index) + ".exr";
        string png_path = "screenshot_" + to_string(index) + ".png";

        spartan::ThreadPool::AddTask([=]()
        {
            SP_LOG_INFO("Saving screenshots...");

            ImageImporter::Save(exr_path, width, height, channel_count, bits_per_channel, mapped_data);

            ImageImporter::SaveSdr(png_path, width, height, channel_count, bits_per_channel, mapped_data, is_hdr);

            SP_LOG_INFO("Screenshots saved as '%s' and '%s'", exr_path.c_str(), png_path.c_str());
        });
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
