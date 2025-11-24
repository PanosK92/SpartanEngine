/*
Copyright(c) 2015-2025 Panos Karabelas

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

//= INCLUDES ================================
#include "pch.h"
#include "Renderer.h"
#include "Material.h"
#include "ThreadPool.h"
#include "../Profiling/RenderDoc.h"
#include "../Profiling/Profiler.h"
#include "../Core/Debugging.h"
#include "../Core/Window.h"
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
#include "../Core/ProgressTracker.h"
#include "../Math/Rectangle.h"
#include "../Resource/Import/ImageImporter.h"
//===========================================

//= NAMESPACES ===============
using namespace std;
using namespace spartan::math;
//============================

namespace spartan
{
    // constant and push constant buffers
    Cb_Frame Renderer::m_cb_frame_cpu;
    Pcb_Pass Renderer::m_pcb_pass_cpu;

    // line and icon rendering
    shared_ptr<RHI_Buffer> Renderer::m_lines_vertex_buffer;
    vector<RHI_Vertex_PosCol> Renderer::m_lines_vertices;
    vector<tuple<RHI_Texture*, math::Vector3>> Renderer::m_icons;

    // misc
    uint32_t Renderer::m_resource_index            = 0;
    atomic<bool> Renderer::m_initialized_resources = false;
    bool Renderer::m_transparents_present          = false;
    bool Renderer::m_bindless_samplers_dirty       = true;
    RHI_CommandList* Renderer::m_cmd_list_present  = nullptr;
    vector<ShadowSlice> Renderer::m_shadow_slices;
    array<RHI_Texture*, rhi_max_array_size> Renderer::m_bindless_textures;
    array<Sb_Light, rhi_max_array_size> Renderer::m_bindless_lights;
    array<Sb_Aabb, rhi_max_array_size> Renderer::m_bindless_aabbs;
    unique_ptr<RHI_AccelerationStructure> tlas;

    namespace
    {
        // resolution & viewport
        math::Vector2 m_resolution_render = math::Vector2::Zero;
        math::Vector2 m_resolution_output = math::Vector2::Zero;
        RHI_Viewport m_viewport           = RHI_Viewport(0, 0, 0, 0);

        // rhi resources
        shared_ptr<RHI_SwapChain> swapchain;
        const uint8_t swap_chain_buffer_count = 2;

        // misc
        unordered_map<Renderer_Option, float> m_options;
        uint64_t frame_num                   = 0;
        math::Vector2 jitter_offset          = math::Vector2::Zero;
        const uint32_t resolution_shadow_min = 128;
        float near_plane                     = 0.0f;
        float far_plane                      = 1.0f;
        bool dirty_orthographic_projection   = true;
 

        void dynamic_resolution()
        {
            if (Renderer::GetOption<float>(Renderer_Option::DynamicResolution) != 0.0f)
            {
                float gpu_time_target   = 16.67f;                                               // target for 60 FPS
                float adjustment_factor = static_cast<float>(0.05f * Timer::GetDeltaTimeSec()); // how aggressively to adjust screen percentage
                float screen_percentage = Renderer::GetOption<float>(Renderer_Option::ResolutionScale);
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

                Renderer::SetOption(Renderer_Option::ResolutionScale, screen_percentage);
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

        // options
        {
            bool low_quality = RHI_Device::GetPrimaryPhysicalDevice()->IsBelowMinimumRequirements();

            m_options.clear();
            SetOption(Renderer_Option::WhitePoint,                  350.0f);
            SetOption(Renderer_Option::Tonemapping,                 static_cast<float>(Renderer_Tonemapping::Max));
            SetOption(Renderer_Option::Bloom,                       1.0f);  // non-zero values activate it and control the intensity
            SetOption(Renderer_Option::MotionBlur,                  1.0f);
            SetOption(Renderer_Option::DepthOfField,                1.0f);
            SetOption(Renderer_Option::ScreenSpaceAmbientOcclusion, 1.0f);
            SetOption(Renderer_Option::ScreenSpaceReflections,      1.0f);
            SetOption(Renderer_Option::RayTracedReflections,        RHI_Device::IsSupportedRayTracing() ? 0.0f : 0.0f);
            SetOption(Renderer_Option::Anisotropy,                  16.0f);
            SetOption(Renderer_Option::Sharpness,                   0.0f);  // becomes the upscaler's sharpness as well
            SetOption(Renderer_Option::Fog,                         1.0);   // controls the intensity of the distance/height and volumetric fog, it's the particle density
            SetOption(Renderer_Option::AntiAliasing_Upsampling,     static_cast<float>(Renderer_AntiAliasing_Upsampling::AA_Fsr_Upscale_Fsr));
            SetOption(Renderer_Option::ResolutionScale,             1.0f);
            SetOption(Renderer_Option::VariableRateShading,         0.0f);
            SetOption(Renderer_Option::Vsync,                       0.0f);
            SetOption(Renderer_Option::TransformHandle,             1.0f);
            SetOption(Renderer_Option::SelectionOutline,            1.0f);
            SetOption(Renderer_Option::Grid,                        1.0f);
            SetOption(Renderer_Option::Lights,                      1.0f);
            SetOption(Renderer_Option::AudioSources,                1.0f);
            SetOption(Renderer_Option::Physics,                     0.0f);
            SetOption(Renderer_Option::PerformanceMetrics,          1.0f);
            SetOption(Renderer_Option::Dithering,                   0.0f);
            SetOption(Renderer_Option::Gamma,                       Display::GetGamma());
            SetOption(Renderer_Option::AutoExposureAdaptationSpeed, 0.5f);

            // set wind direction and strength
            {
                float rotation_y      = 120.0f * math::deg_to_rad;
                const float intensity = 3.0f; // meters per second
                SetWind(Vector3(sin(rotation_y), 0.0f, cos(rotation_y)) * intensity);
            }
        }

        // resolution
        {
            // note #1: settings can override default resolutions based on loaded XML configurations
            // note #2: if settings are absent, the editor will set the render/viewport resolutions to it's viewport size

            uint32_t width  = Window::GetWidth();
            uint32_t height = Window::GetHeight();

            // the resolution of the output frame (we can upscale to that linearly or with fsr)
            SetResolutionOutput(width, height, false);

            // set the render resolution to something smaller than the output resolution
            // this is done because FSR is not good at doing TAA if the render resolution is the same as the output resolution
            SetResolutionRender(1920, 1080, false);

            // the resolution/size of the editor's viewport, this is overridden by the editor based on the actual viewport size
            SetViewport(static_cast<float>(width), static_cast<float>(height));
        }

        // in case of breadcrumb support, anything that uses a command list can use RHI_FidelityFX
        // so we need to initialize even before the swapchain which can use a copy queue etc.
        RHI_VendorTechnology::Initialize();

        // swap chain
        {
            swapchain = make_shared<RHI_SwapChain>
            (
                Window::GetHandleSDL(),
                Window::GetWidth(),
                Window::GetHeight(),
                // present mode: for v-sync, we could mailbox for lower latency, but fifo is always supported, so we'll assume that
                // note: fifo is not supported on linux, it will be ignored
                GetOption<bool>(Renderer_Option::Vsync) ? RHI_Present_Mode::Fifo : RHI_Present_Mode::Immediate,
                swap_chain_buffer_count,
                Display::GetHdr(),
                "renderer"
            );

            SetOption(Renderer_Option::Hdr, swapchain->IsHdr() ? 1.0f : 0.0f);
        }

        // tonemapping
        if (!swapchain->IsHdr())
        {
            SetOption(Renderer_Option::Tonemapping, static_cast<float>(Renderer_Tonemapping::AcesNautilus));
        }

        // load/create resources
        {
            // reduce startup time by doing expensive operations in another thread
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

        // handle edge cases
        {
            if (Debugging::IsBreadcrumbsEnabled())
            { 
                SP_ASSERT_MSG(RHI_Device::GetPrimaryPhysicalDevice()->IsAmd(), "Breadcrumbs are only supported on AMD GPUs");
            }

            if (RHI_Device::GetPrimaryPhysicalDevice()->IsBelowMinimumRequirements())
            {
                SP_WARNING_WINDOW("The GPU does not meet the minimum requirements for running the engine. The engine might be missing features and it won't perform as expected.");
            }
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

        // wait for all commands list, from all queues, to finish executing
        RHI_Device::QueueWaitAll();

        RHI_CommandList::ImmediateExecutionShutdown();

        // manually destroy everything so that RHI_Device::ParseDeletionQueue() frees memory
        {
            DestroyResources();
            swapchain             = nullptr;
            m_lines_vertex_buffer = nullptr;
            tlas                  = nullptr;
            m_std_reflections     = nullptr;
        }

        RHI_VendorTechnology::Shutdown();
        RenderDoc::Shutdown();
        RHI_Device::Destroy();
    }

    void Renderer::Tick()
    {
        // acquire next swapchain image and update RHI
        {
            swapchain->AcquireNextImage();
            RHI_Device::Tick(frame_num);
            RHI_VendorTechnology::Tick(&m_cb_frame_cpu, GetResolutionRender(), GetResolutionOutput(), GetOption<float>(Renderer_Option::ResolutionScale));
            dynamic_resolution();
        }
    
        // begin the primary graphics command list
        {
            RHI_Queue* queue_graphics = RHI_Device::GetQueue(RHI_Queue_Type::Graphics);
            m_cmd_list_present = queue_graphics->NextCommandList();
            m_cmd_list_present->Begin();
        }

        // update CPU and GPU resources
        {
            // fill draw call list and determine ideal occluders
            UpdateDrawCalls(m_cmd_list_present);

            // update tlas
            UpdateAccelerationStructures(m_cmd_list_present);
    
            // handle dynamic buffers and resource deletion
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
    
            // update bindless resources
            {
                // we always update on the first frame so the buffers are bound and we don't get graphics api issues
                bool initialize = GetFrameNumber() == 0;

                // lights
                if (initialize || World::HaveLightsChangedThisFrame())
                {
                    UpdateShadowAtlas();
                    UpdateLights(m_cmd_list_present);
                    RHI_Device::UpdateBindlessResources(nullptr, nullptr, GetBuffer(Renderer_Buffer::LightParameters), nullptr, nullptr);
                }

                // materials
                if (initialize || World::HaveMaterialsChangedThisFrame())
                {
                    UpdateMaterials(m_cmd_list_present);
                    RHI_Device::UpdateBindlessResources(&m_bindless_textures, GetBuffer(Renderer_Buffer::MaterialParameters), nullptr, nullptr, nullptr);
                }

                // samplers
                if (m_bindless_samplers_dirty)
                {
                    RHI_Device::UpdateBindlessResources(nullptr, nullptr, nullptr, &Renderer::GetSamplers(), nullptr);
                    m_bindless_samplers_dirty = false;
                }

                // world-space aabbs, always update those as they reflect in-game entites
                {
                    UpdatedBoundingBoxes(m_cmd_list_present);
                    RHI_Device::UpdateBindlessResources(nullptr, nullptr, nullptr, nullptr, GetBuffer(Renderer_Buffer::AABBs));
                }
            }
    
            // update frame constant buffer and add lines to render
            UpdateFrameConstantBuffer(m_cmd_list_present);
            AddLinesToBeRendered();
        }
    
        // produce the frame if window is not minimized
        {
            if (!Window::IsMinimized() && m_initialized_resources)
            {
                ProduceFrame(m_cmd_list_present, nullptr);
            }
        }
    
        // blit to back buffer when standalone
        {
            bool is_standalone = !Engine::IsFlagSet(EngineMode::EditorVisible);
            if (is_standalone)
            {
                BlitToBackBuffer(m_cmd_list_present, GetRenderTarget(Renderer_RenderTarget::frame_output));
            }
        }
    
        // present frame when standalone
        {
            bool is_standalone = !Engine::IsFlagSet(EngineMode::EditorVisible);
            if (is_standalone)
            {
                SubmitAndPresent();
            }
        }
    
        // clear per-frame data
        {
            m_lines_vertices.clear();
            m_icons.clear();
        }
    
        // increment frame counter and trigger first-frame event
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

    void Renderer::SetResolutionRender(uint32_t width, uint32_t height, bool recreate_resources /*= true*/)
    {
        if (!RHI_Device::IsValidResolution(width, height))
        {
            SP_LOG_WARNING("Can't set %dx% as it's an invalid resolution", width, height);
            return;
        }

        if (m_resolution_render.x == width && m_resolution_render.y == height)
            return;

        m_resolution_render.x = static_cast<float>(width);
        m_resolution_render.y = static_cast<float>(height);

        if (recreate_resources)
        {
            // if frames are in-flight, wait for them to finish before resizing
            if (m_cb_frame_cpu.frame > 1)
            {
                bool flush = true;
                RHI_Device::QueueWaitAll(flush);
            }

            CreateRenderTargets(true, false, true);
            CreateSamplers();
        }

        SP_LOG_INFO("Render resolution has been set to %dx%d", width, height);
    }

    const Vector2& Renderer::GetResolutionOutput()
    {
        return m_resolution_output;
    }

    void Renderer::SetResolutionOutput(uint32_t width, uint32_t height, bool recreate_resources /*= true*/)
    {
        if (!RHI_Device::IsValidResolution(width, height))
        {
            SP_LOG_WARNING("%dx%d is an invalid resolution", width, height);
            return;
        }

        if (m_resolution_output.x == width && m_resolution_output.y == height)
            return;

        m_resolution_output.x = static_cast<float>(width);
        m_resolution_output.y = static_cast<float>(height);

        if (recreate_resources)
        {
            // if frames are in-flight, wait for them to finish before resizing
            if (m_cb_frame_cpu.frame > 1)
            {
                bool flush = true;
                RHI_Device::QueueWaitAll(flush);
            }

            CreateRenderTargets(false, true, true);
            CreateSamplers();
        }

        // register this resolution as a display mode so it shows up in the editor's render options (it won't happen if already registered)
        Display::RegisterDisplayMode(static_cast<uint32_t>(width), static_cast<uint32_t>(height), Timer::GetFpsLimit(), Display::GetId());

        SP_LOG_INFO("Output resolution output has been set to %dx%d", width, height);
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
                // near clip does not affect depth accuracy in orthographic projection, so set it to 0 to avoid problems which can result an infinitely small [3,2] (NaN) after the multiplication below
                Matrix projection_ortho              = Matrix::CreateOrthographicLH(m_viewport.width, m_viewport.height, 0.0f, far_plane);
                m_cb_frame_cpu.view_projection_ortho = Matrix::CreateLookAtLH(Vector3(0, 0, -near_plane), Vector3::Forward, Vector3::Up) * projection_ortho;
                dirty_orthographic_projection        = false;
            }
        }

        // generate jitter samples in case of fsr or xess
        Renderer_AntiAliasing_Upsampling upsampling_mode = GetOption<Renderer_AntiAliasing_Upsampling>(Renderer_Option::AntiAliasing_Upsampling);
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

        // update the remaining of the frame buffer
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
        m_cb_frame_cpu.resolution_scale    = GetOption<float>(Renderer_Option::ResolutionScale);
        m_cb_frame_cpu.hdr_enabled         = GetOption<bool>(Renderer_Option::Hdr) ? 1.0f : 0.0f;
        m_cb_frame_cpu.hdr_max_nits        = Display::GetLuminanceMax();
        m_cb_frame_cpu.hdr_white_point     = GetOption<float>(Renderer_Option::WhitePoint);
        m_cb_frame_cpu.gamma               = GetOption<float>(Renderer_Option::Gamma);
        m_cb_frame_cpu.camera_exposure     = World::GetCamera() ? World::GetCamera()->GetExposure() : 1.0f;

        // these must match what common_buffer.hlsl is reading
        m_cb_frame_cpu.set_bit(GetOption<bool>(Renderer_Option::ScreenSpaceReflections),      1 << 0);
        m_cb_frame_cpu.set_bit(GetOption<bool>(Renderer_Option::ScreenSpaceAmbientOcclusion), 1 << 1);
        m_cb_frame_cpu.set_bit(GetOption<bool>(Renderer_Option::Fog),                         1 << 2);

        // set
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
    
    void Renderer::SetOption(Renderer_Option option, float value)
    {
        // clamp value
        {
            // anisotropy
            if (option == Renderer_Option::Anisotropy)
            {
                value = clamp(value, 0.0f, 16.0f);
            }
            else if (option == Renderer_Option::ResolutionScale)
            {
                value = clamp(value, 0.5f, 1.0f);
            }
        }

        // early exit if the value is already set
        if ((m_options.find(option) != m_options.end()) && m_options[option] == value)
            return;

        // reject changes (if needed)
        {
            if (option == Renderer_Option::Hdr)
            {
                if (value == 1.0f)
                {
                    if (!Display::GetHdr())
                    { 
                        SP_LOG_WARNING("This display doesn't support HDR");
                        return;
                    }
                }
            }
            else if (option == Renderer_Option::VariableRateShading)
            {
                if (value == 1.0f)
                {
                    if (!RHI_Device::IsSupportedVrs())
                    { 
                        SP_LOG_WARNING("This GPU doesn't support variable rate shading");
                        return;
                    }
                }
            }
            else if (option == Renderer_Option::AntiAliasing_Upsampling)
            {
                if (value == static_cast<float>(Renderer_AntiAliasing_Upsampling::AA_Xess_Upscale_Xess))
                {
                    if (!RHI_Device::IsSupportedXess())
                    { 
                        SP_LOG_WARNING("This GPU doesn't support XeSS");
                        return;
                    }
                }
            }
        }

        // set new value
        m_options[option] = value;

        // handle cascading changes
        {
            // upsampling and anti-aliasing
            if (option == Renderer_Option::AntiAliasing_Upsampling)
            {
                // reset history for temporal filters
                if (value == static_cast<float>(Renderer_AntiAliasing_Upsampling::AA_Fsr_Upscale_Fsr) || value == static_cast<float>(Renderer_AntiAliasing_Upsampling::AA_Xess_Upscale_Xess))
                {
                    RHI_VendorTechnology::ResetHistory();
                }
            }
            else if (option == Renderer_Option::Hdr)
            {
                if (swapchain)
                { 
                    swapchain->SetHdr(value == 1.0f);
                }
            }
            else if (option == Renderer_Option::Vsync)
            {
                if (swapchain)
                {
                    swapchain->SetVsync(value == 1.0f);
                }
            }
            else if (option == Renderer_Option::PerformanceMetrics)
            {
                static bool enabled = false;
                if (!enabled && value == 1.0f)
                {
                    Profiler::ClearMetrics();
                }

                enabled = value != 0.0f;
            }
        }
    }

    unordered_map<Renderer_Option, float>& Renderer::GetOptions()
    {
        return m_options;
    }

    void Renderer::SetOptions(const unordered_map<Renderer_Option, float>& options)
    {
        m_options = options;
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

    void Renderer::SubmitAndPresent()
    {
        Profiler::TimeBlockStart("submit_and_present", TimeBlockType::Cpu, nullptr);
        {
            SP_ASSERT(m_cmd_list_present->GetState() == RHI_CommandListState::Recording);
            m_cmd_list_present->InsertBarrier(swapchain->GetRhiRt(), swapchain->GetFormat(), 0, 1, 1, RHI_Image_Layout::Present_Source);
            m_cmd_list_present->Submit(swapchain->GetImageAcquiredSemaphore(), false);
            swapchain->Present(m_cmd_list_present);
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

    void Renderer::SetCommonTextures(RHI_CommandList* cmd_list)
    {
        // gbuffer
        cmd_list->SetTexture(Renderer_BindingsSrv::gbuffer_albedo,   GetRenderTarget(Renderer_RenderTarget::gbuffer_color));
        cmd_list->SetTexture(Renderer_BindingsSrv::gbuffer_normal,   GetRenderTarget(Renderer_RenderTarget::gbuffer_normal));
        cmd_list->SetTexture(Renderer_BindingsSrv::gbuffer_material, GetRenderTarget(Renderer_RenderTarget::gbuffer_material));
        cmd_list->SetTexture(Renderer_BindingsSrv::gbuffer_velocity, GetRenderTarget(Renderer_RenderTarget::gbuffer_velocity));
        cmd_list->SetTexture(Renderer_BindingsSrv::gbuffer_depth,    GetRenderTarget(Renderer_RenderTarget::gbuffer_depth));

        // other
        cmd_list->SetTexture(Renderer_BindingsSrv::ssao, GetRenderTarget(Renderer_RenderTarget::ssao));
    }

    void Renderer::UpdateMaterials(RHI_CommandList* cmd_list)
    {
        static array<Sb_Material, rhi_max_array_size> properties; // mapped to the gpu as a structured properties buffer
        static unordered_set<uint64_t> unique_material_ids;
        static uint32_t count = 0;
    
        auto update_material = [](Material* material)
        {
            // check if the material's ID is already processed
            if (unique_material_ids.find(material->GetObjectId()) != unique_material_ids.end())
                return;
    
            // if not, add it to the list
            unique_material_ids.insert(material->GetObjectId());
    
            // properties
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
                // when changing the bit flags, ensure that you also update the Surface struct in common_structs.hlsl, so that it reads those flags as expected
            }
    
            // textures
            {
                // iterate through all texture types and their slots
                for (uint32_t type = 0; type < static_cast<uint32_t>(MaterialTextureType::Max); type++)
                {
                    for (uint32_t slot = 0; slot < Material::slots_per_texture; slot++)
                    {
                        // calculate the final index in the bindless array
                        uint32_t bindless_index = count + (type * Material::slots_per_texture) + slot;
                        
                        // get the texture from the material using type and slot
                        m_bindless_textures[bindless_index] = material->GetTexture(static_cast<MaterialTextureType>(type), slot);
                    }
                }
            }
    
            material->SetIndex(count);

            // update index increment to account for all texture slots
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
            // clear
            properties.fill(Sb_Material{});
            m_bindless_textures.fill(nullptr);
            unique_material_ids.clear();
            update_entities();
        }
    
        // gpu
        {
            // material properties
            RHI_Buffer* buffer = Renderer::GetBuffer(Renderer_Buffer::MaterialParameters);
            buffer->ResetOffset();
            buffer->Update(cmd_list, &properties[0], buffer->GetStride() * count);
        }

        count = 0;
    }

    void Renderer::UpdateLights(RHI_CommandList* cmd_list)
    {
        const Entity* camera_entity = World::GetCamera() ? World::GetCamera()->GetEntity() : nullptr;
        const Vector3 camera_pos    = camera_entity ? camera_entity->GetPosition() : Vector3::Zero;
    
        m_bindless_lights.fill(Sb_Light());
        static uint32_t count;
        count = 0;
        Light* first_directional = nullptr;
    
        auto fill_light = [&](Light* light_component)
        {
            //SP_LOG_INFO("Processing light %s", light_component->GetEntity()->GetObjectName().c_str());

            const uint32_t index = count++;
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
            light_buffer_entry.flags                             = 0;
            light_buffer_entry.flags                            |= light_component->GetLightType() == LightType::Directional ? (1 << 0) : 0;
            light_buffer_entry.flags                            |= light_component->GetLightType() == LightType::Point       ? (1 << 1) : 0;
            light_buffer_entry.flags                            |= light_component->GetLightType() == LightType::Spot        ? (1 << 2) : 0;
            light_buffer_entry.flags                            |= light_component->GetFlag(LightFlags::Shadows)             ? (1 << 3) : 0;
            light_buffer_entry.flags                            |= light_component->GetFlag(LightFlags::ShadowsScreenSpace)  ? (1 << 4) : 0;
            light_buffer_entry.flags                            |= light_component->GetFlag(LightFlags::Volumetric)          ? (1 << 5) : 0;
    
            for (uint32_t i = 0; i < 6; i++)
            {
                if (i < light_component->GetSliceCount())
                {
                    light_buffer_entry.atlas_offsets[i]     = light_component->GetAtlasOffset(i);
                    light_buffer_entry.atlas_scales[i]      = light_component->GetAtlasScale(i);
                    const math::Rectangle& rect             = light_component->GetAtlasRectangle(i);
                    light_buffer_entry.atlas_texel_sizes[i] = Vector2(1.0f / rect.width, 1.0f / rect.height);
                }
                else
                {
                    light_buffer_entry.atlas_offsets[i]     = Vector2::Zero;
                    light_buffer_entry.atlas_scales[i]      = Vector2::Zero;
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
                    fill_light(light_component);
                    break;
                }
            }
        }
    
        // fill remaining lights (skip disabled or out-of-range)
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
    
        // upload to gpu
        RHI_Buffer* buffer = GetBuffer(Renderer_Buffer::LightParameters);
        buffer->ResetOffset();
        buffer->Update(cmd_list, &m_bindless_lights[0], buffer->GetStride() * World::GetLightCount());
    }

    void Renderer::UpdatedBoundingBoxes(RHI_CommandList* cmd_list)
    {
        // clear
        m_bindless_aabbs.fill(Sb_Aabb());
        uint32_t count = 0;

        // cpu
        for (uint32_t i = 0; i < m_draw_call_count; i++)
        {
            const Renderer_DrawCall& draw_call   = m_draw_calls[i];
            Renderable* renderable               = draw_call.renderable;
            const BoundingBox& aabb              = renderable->GetBoundingBox();
            m_bindless_aabbs[count].min          = aabb.GetMin();
            m_bindless_aabbs[count].max          = aabb.GetMax();
            m_bindless_aabbs[count].is_occluder  = draw_call.is_occluder;
            count++;
        }

        // gpu
        RHI_Buffer* buffer = GetBuffer(Renderer_Buffer::AABBs);
        buffer->ResetOffset();
        buffer->Update(cmd_list, &m_bindless_aabbs[0], buffer->GetStride() * count);
    }

    void Renderer::UpdateDrawCalls(RHI_CommandList* cmd_list)
    {
        m_draw_call_count          = 0;
        m_draw_calls_prepass_count = 0;
        m_transparents_present     = false;
        if (ProgressTracker::IsLoading())
            return;

        // build draw calls and sort them for g-buffer (transparency -> material -> depth)
        {
            for (Entity* entity : World::GetEntities())
            {
                if (!entity->GetActive())
                    continue;

                if (Renderable* renderable = entity->GetComponent<Renderable>())
                {
                    // skip renderables with no material, can happen when loading a world and the material is not yet loaded
                    if (!renderable->GetMaterial())
                        continue;

                    if (renderable->GetMaterial()->IsTransparent())
                    {
                        m_transparents_present = true;
                    }

                    Renderer_DrawCall& draw_call = m_draw_calls[m_draw_call_count++];
                    draw_call.renderable         = renderable;
                    draw_call.distance_squared   = renderable->GetDistanceSquared();
                    draw_call.lod_index          = renderable->GetLodIndex();
                    draw_call.is_occluder        = false;
                    draw_call.camera_visible     = renderable->IsVisible();
                    draw_call.instance_index     = 0;
                    draw_call.instance_count     = renderable->GetInstanceCount();
                }
            }

            // sort by transparency, material id, and distance (front-to-back for opaque, back-to-front for transparent)
            sort(m_draw_calls.begin(), m_draw_calls.begin() + m_draw_call_count, [](const Renderer_DrawCall& a, const Renderer_DrawCall& b)
            {
                // step 1: sort by transparency (opaque before transparent)
                bool a_transparent = a.renderable->GetMaterial()->IsTransparent();
                bool b_transparent = b.renderable->GetMaterial()->IsTransparent();
                if (a_transparent != b_transparent)
                {
                    return !a_transparent; // false (opaque) before true (transparent)
                }

                // step 2: sort by material id within each transparency group
                uint64_t a_material_id = a.renderable->GetMaterial()->GetObjectId();
                uint64_t b_material_id = b.renderable->GetMaterial()->GetObjectId();
                if (a_material_id != b_material_id)
                {
                    return a_material_id < b_material_id; // lower material ids first
                }

                // step 3: sort by distance within each material group
                if (!a_transparent) // both are opaque
                {
                    return a.distance_squared < b.distance_squared; // front-to-back
                }
                else // both are transparent
                {
                    return a.distance_squared > b.distance_squared; // back-to-front
                }
            });
        }

        // build prepass calls: opaques only, sorted by alpha test (non-alpha first), then depth front-to-back
        {
            for (uint32_t i = 0; i < m_draw_call_count; ++i)
            {
                const Renderer_DrawCall& dc = m_draw_calls[i];
                if (!dc.renderable->GetMaterial()->IsTransparent() && dc.camera_visible)
                {
                    m_draw_calls_prepass[m_draw_calls_prepass_count++] = dc;
                }
            }

            // sort prepass by alpha test flag, then distance_squared (front-to-back)
            sort(m_draw_calls_prepass.begin(), m_draw_calls_prepass.begin() + m_draw_calls_prepass_count, [](const Renderer_DrawCall& a, const Renderer_DrawCall& b)
            {
                bool a_alpha = a.renderable->GetMaterial()->IsAlphaTested();
                bool b_alpha = b.renderable->GetMaterial()->IsAlphaTested();
                if (a_alpha != b_alpha)
                {
                    return !a_alpha; // non-alpha before alpha-tested
                }
                return a.distance_squared < b.distance_squared;
            });
        }

        // select occluders by finding the top n largest screen-space bounding boxes
        {
            // lambda to compute screen-space area of a bounding box
            auto compute_screen_space_area = [&](const BoundingBox& aabb_world) -> float
            {
                // project aabb to screen space using camera function
                float area = 0.0f;
                if (Camera* camera = World::GetCamera())
                {
                    math::Rectangle rect_screen = World::GetCamera()->WorldToScreenCoordinates(aabb_world);

                    // compute screen-space dimensions
                    area = clamp(rect_screen.width * rect_screen.height, 0.0f, numeric_limits<float>::max());
                }

                return area;
            };

            // temporary storage for draw call areas
            struct DrawCallArea
            {
                uint32_t index;
                float area;
            };
            static vector<DrawCallArea> areas;
            areas.clear(); // clear old data
            areas.reserve(m_draw_calls_prepass_count); // ensure enough capacity

            // collect screen-space areas for eligible draw calls from prepass
            for (uint32_t i = 0; i < m_draw_calls_prepass_count; i++)
            {
                Renderer_DrawCall& draw_call = m_draw_calls_prepass[i];
                Renderable* renderable = draw_call.renderable;
                Material* material = renderable->GetMaterial();

                // skip any draw calls that have a mesh that you can see through (transparent, instanced, non-solid)
                if (!material || material->IsTransparent() || renderable->HasInstancing() || !draw_call.camera_visible)
                    continue;

                // get bounding box
                const BoundingBox& aabb_world = renderable->GetBoundingBox();

                // compute screen-space area and store it
                float screen_area = compute_screen_space_area(aabb_world);
                areas.push_back({ i, screen_area });
            }

            // sort draw calls by screen-space area (descending)
            sort(areas.begin(), areas.end(), [](const DrawCallArea& a, const DrawCallArea& b)
            {
                return a.area > b.area;
            });

            // select the top n occluders
            const uint32_t max_occluders = 64;
            uint32_t occluder_count = min(max_occluders, static_cast<uint32_t>(areas.size()));
            for (uint32_t i = 0; i < occluder_count; i++)
            {
                m_draw_calls_prepass[areas[i].index].is_occluder = true;
            }
        }
    }

    void Renderer::UpdateAccelerationStructures(RHI_CommandList* cmd_list)
    {
        return;

        // validate ray tracing and command list
        if (!RHI_Device::IsSupportedRayTracing() || !cmd_list)
        {
            SP_LOG_WARNING("Ray tracing or command list invalid, skipping update");
            return;
        }

        // bottom-level acceleration structures
        {
            for (Entity* entity : World::GetEntities())
            {
                if (!entity->GetActive())
                    continue;

                if (Renderable* renderable = entity->GetComponent<Renderable>())
                {
                    if (!renderable->HasAccelerationStructure())
                    {
                        renderable->BuildAccelerationStructure(cmd_list);
                    }
                }
            }
        }

        // top-level acceleration structure
        {
            // create or rebuild tlas
            if (!tlas)
            {
                tlas = make_unique<RHI_AccelerationStructure>(RHI_AccelerationStructureType::Top, "world_tlas");
            }

            // temp till we make rhi enum
            constexpr uint32_t RHI_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT = 0x00000002; // matches VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR

            vector<RHI_AccelerationStructureInstance> instances;
            for (Entity* entity : World::GetEntities())
            {
                if (!entity->GetActive())
                    continue;
    
               if (Renderable* renderable = entity->GetComponent<Renderable>())
                {
                    if (Material* material = renderable->GetMaterial())
                    {
                        RHI_CullMode cull_mode = static_cast<RHI_CullMode>(material->GetProperty(MaterialProperty::CullMode));

                        RHI_AccelerationStructureInstance instance           = {};
                        instance.instance_custom_index                       = material->GetIndex(); // for hit shader material lookup
                        instance.mask                                        = 0xFF;                 // visible to all rays
                        instance.instance_shader_binding_table_record_offset = 0;                    // sbt hit group offset
                        instance.flags                                       = cull_mode == RHI_CullMode::None ? RHI_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT : 0;
                        instance.device_address                              = renderable->GetAccelerationStructureDeviceAddress();
                        Matrix world_matrix                                  = renderable->GetEntity()->GetMatrix().Transposed();
                        copy(world_matrix.Data(), world_matrix.Data() + 12, instance.transform.begin()); // convert column-major 4x4 to row-major 3x4

                        instances.push_back(instance);
                    }
                }
            }
    
            if (!instances.empty())
            {
                tlas->BuildTopLevel(cmd_list, instances);
            }
        }
    }

    void Renderer::UpdateShadowAtlas()
    {
        const uint32_t resolution_atlas = GetRenderTarget(Renderer_RenderTarget::shadow_atlas)->GetWidth();
        const uint32_t min_slice_res    = 256;

        // assume atlas is square, width == height
    
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
    
        // lambda to check if slices of given res can fit with borders
        uint32_t border  = 8; // pixels between slices, none at edges
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
    
                // wrap to next row if overflow
                if (placed_x + test_res > resolution_atlas)
                {
                    y        += row_h + border;
                    x         = 0;
                    row_h     = 0;
                    placed_x  = 0;
                }
    
                // check if too wide after wrap
                if (placed_x + test_res > resolution_atlas)
                    return false;
    
                uint32_t placed_y       = y;
                if (placed_y + test_res > resolution_atlas)
                    return false;
    
                // simulate placement
                x     = placed_x + test_res;
                row_h = max(row_h, test_res);
            }
    
            return true;
        };
    
        // binary search for max uniform slice res
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
        max_slice_res = max(max_slice_res, min_slice_res); // clamp to min
    
        // assign res to all slices (uniform)
        for (auto& slice : m_shadow_slices)
        {
            slice.res = max_slice_res;
        }
    
        // pack slices in scanline order
        uint32_t x     = 0;
        uint32_t y     = 0;
        uint32_t row_h = 0;
        for (auto& slice : m_shadow_slices)
        {
            uint32_t left_pad = (x == 0) ? 0 : border;
            uint32_t placed_x = x + left_pad;
    
            // wrap to next row if needed
            if (placed_x + slice.res > resolution_atlas)
            {
                y        += row_h + border;
                x         = 0;
                row_h     = 0;
                placed_x  = 0;
            }
    
            // no overflow checks here, as can_fit validated
            // assert(placed_x + slice.res <= resolution_atlas && y + slice.res <= resolution_atlas);
    
            slice.rect = math::Rectangle(
                static_cast<float>(placed_x),
                static_cast<float>(y),
                static_cast<float>(slice.res),
                static_cast<float>(slice.res)
            );
    
            // advance
            x     = placed_x + slice.res;
            row_h = max(row_h, slice.res);
        }
    
        // assign rects back to lights
        for (const auto& slice : m_shadow_slices)
        {
            slice.light->SetAtlasRectangle(slice.slice_index, slice.rect);
        }
    }

    void Renderer::Screenshot()
    {
        RHI_Texture* frame_output = GetRenderTarget(Renderer_RenderTarget::frame_output);
        uint32_t width            = frame_output->GetWidth();
        uint32_t height           = frame_output->GetHeight();
        RHI_Format format         = frame_output->GetFormat();
        uint32_t bits_per_channel = frame_output->GetBitsPerChannel();
        uint32_t channel_count    = frame_output->GetChannelCount();
        size_t data_size          = static_cast<size_t>(width) * height * (bits_per_channel / 8) * channel_count;
        
        // create staging buffer (linear: element_count=1, stride=data_size; mappable=true for coherent host-visible)
        auto staging = make_unique<RHI_Buffer>(RHI_Buffer_Type::Constant, data_size, 1, nullptr, true, "screenshot_staging");
        
        // copy image to buffer
        if (RHI_CommandList* cmd_list = RHI_CommandList::ImmediateExecutionBegin(RHI_Queue_Type::Graphics))
        {
            cmd_list->CopyTextureToBuffer(frame_output, staging.get());
            RHI_CommandList::ImmediateExecutionEnd(cmd_list);
        }
        
        // read mapped data (coherent, so direct access post-submit)
        void* mapped_data = staging->GetMappedData();
        SP_ASSERT_MSG(mapped_data, "Staging buffer not mappable");

        spartan::ThreadPool::AddTask([width, height, channel_count, bits_per_channel, mapped_data]()
        {
            SP_LOG_INFO("Saving screenshot...");
            ImageImporter::Save("screenshot.exr", width, height, channel_count, bits_per_channel, mapped_data);
            SP_LOG_INFO("Screenshot saved as 'screenshot.exr'");
        });
    }

    RHI_AccelerationStructure* Renderer::GetTopLevelAccelerationStructure()
    {
        return tlas.get();
    }
}
