/*
Copyright(c) 2016-2025 Panos Karabelas

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
FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.IN NO EVENT SHALL THE AUTHORS OR
COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/

//= INCLUDES ===========================
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
#include "../RHI/RHI_AMD_FFX.h"
#include "../RHI/RHI_OpenImageDenoise.h"
#include "../World/Entity.h"
#include "../World/Components/Light.h"
#include "../World/Components/Camera.h"
#include "../Core/ProgressTracker.h"
//======================================

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
    bool Renderer::m_bindless_abbs_dirty           = true;
    bool Renderer::m_bindless_materials_dirty      = true;
    bool Renderer::m_bindless_lights_dirty         = true;
    RHI_CommandList* Renderer::m_cmd_list_present  = nullptr;
    array<RHI_Texture*, rhi_max_array_size> Renderer::m_bindless_textures;
    array<Sb_Light, rhi_max_array_size> Renderer::m_bindless_lights;
    array<Sb_Aabb, rhi_max_array_size> Renderer::m_bindless_aabbs;

    namespace
    {
        // resolution & viewport
        math::Vector2 m_resolution_render = math::Vector2::Zero;
        math::Vector2 m_resolution_output = math::Vector2::Zero;
        RHI_Viewport m_viewport           = RHI_Viewport(0, 0, 0, 0);

        // rhi resources
        shared_ptr<RHI_SwapChain> swap_chain;
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
            SetOption(Renderer_Option::Bloom,                       1.0f);                                                          // non-zero values activate it and control the intensity
            SetOption(Renderer_Option::MotionBlur,                  1.0f);
            SetOption(Renderer_Option::DepthOfField,                1.0f);
            SetOption(Renderer_Option::ScreenSpaceAmbientOcclusion, 1.0f);
            SetOption(Renderer_Option::ScreenSpaceReflections,      RHI_Device::GetPrimaryPhysicalDevice()->IsAmd() ? 1.0f : 0.0f); // temp workaround until I fix ssr for nvidia
            SetOption(Renderer_Option::GlobalIllumination,          0.0f);                                                          // disabled by default because it sucks - options are 0.25f - 25%, 0.5f - 50%, 0.75f - 75% and 1.0f - 100%)
            SetOption(Renderer_Option::Anisotropy,                  16.0f);                                                         
            SetOption(Renderer_Option::ShadowResolution,            4096.0f);                                                       
            SetOption(Renderer_Option::Sharpness,                   0.0f);                                                          // becomes the upsampler's sharpness as well
            SetOption(Renderer_Option::Fog,                         5.0);                                                           // controls the intensity of the distance/height and volumetric fog, it's the particle density
            SetOption(Renderer_Option::Antialiasing,                static_cast<float>(Renderer_Antialiasing::Taa));                // this is using fsr 3 for taa
            SetOption(Renderer_Option::Upsampling,                  static_cast<float>(Renderer_Upsampling::Fsr3));
            SetOption(Renderer_Option::ResolutionScale,             1.0f);
            SetOption(Renderer_Option::VariableRateShading,         0.0f);
            SetOption(Renderer_Option::Vsync,                       0.0f);
            SetOption(Renderer_Option::TransformHandle,             1.0f);
            SetOption(Renderer_Option::SelectionOutline,            1.0f);
            SetOption(Renderer_Option::Grid,                        1.0f);
            SetOption(Renderer_Option::Lights,                      1.0f);
            SetOption(Renderer_Option::Physics,                     0.0f);
            SetOption(Renderer_Option::PerformanceMetrics,          1.0f);
            SetOption(Renderer_Option::Dithering,                   0.0f);
            SetOption(Renderer_Option::Gamma,                       Display::GetGamma());

            SetWind(Vector3(1.0f, 0.0f, 0.5f));
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
        RHI_AMD_FFX::Initialize();
        RHI_OpenImageDenoise::Initialize();

        // swap chain
        {
            swap_chain = make_shared<RHI_SwapChain>
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

            SetOption(Renderer_Option::Hdr, swap_chain->IsHdr() ? 1.0f : 0.0f);
        }

        // tonemapping
        if (!swap_chain->IsHdr())
        {
            SetOption(Renderer_Option::Tonemapping, static_cast<float>(Renderer_Tonemapping::NautilusACES));
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
                SP_WARNING_WINDOW("The GPU does not meet the minimum requirements for running the engine. The engine may not function correctly.");
            }
        }

        // events
        {
            // subscribe
            SP_SUBSCRIBE_TO_EVENT(EventType::WindowFullScreenToggled, SP_EVENT_HANDLER_STATIC(OnFullScreenToggled));
            SP_SUBSCRIBE_TO_EVENT(EventType::MaterialOnChanged,       SP_EVENT_HANDLER_EXPRESSION_STATIC( m_bindless_materials_dirty = true; ));
            SP_SUBSCRIBE_TO_EVENT(EventType::LightOnChanged,          SP_EVENT_HANDLER_EXPRESSION_STATIC( m_bindless_lights_dirty    = true; ));

            // fire
            SP_FIRE_EVENT(EventType::RendererOnInitialized);
        }
    }

    void Renderer::Shutdown()
    {
        SP_FIRE_EVENT(EventType::RendererOnShutdown);

        // manually invoke the deconstructor so that ParseDeletionQueue()
        // releases their rhi resources before device destruction
        {
            DestroyResources();

            swap_chain            = nullptr;
            m_lines_vertex_buffer = nullptr;
        }

        RHI_OpenImageDenoise::Shutdown();
        RHI_AMD_FFX::Shutdown();
        RenderDoc::Shutdown();
        RHI_Device::Destroy();
    }

    void Renderer::Tick()
    {
        // update logic
        GetSwapChain()->AcquireNextImage();
        RHI_Device::Tick(frame_num);
        RHI_AMD_FFX::Tick(&m_cb_frame_cpu);
        dynamic_resolution();

        // begin a the main/present command list
        RHI_Queue* queue_graphics = RHI_Device::GetQueue(RHI_Queue_Type::Graphics);
        m_cmd_list_present        = queue_graphics->NextCommandList();
        m_cmd_list_present->Begin();

        // build draw calls and determine occluders
        BuildDrawCallsAndOccluders(m_cmd_list_present);

        // update GPU buffers (needs to happen after draw call and occluder building)
        UpdateBuffers(m_cmd_list_present);

        // produce a frame (or not, if minimized), this is where the expensive work happens
        if (!Window::IsMinimized() && m_initialized_resources)
        { 
            RHI_CommandList* cmd_list_graphics_secondary = nullptr;//queue_graphics->NextCommandList();
            ProduceFrame(m_cmd_list_present, cmd_list_graphics_secondary);
        }

        // blit to back buffer when not in editor mode
        bool is_standalone = !Engine::IsFlagSet(EngineMode::EditorVisible);
        if (is_standalone)
        {
            BlitToBackBuffer(m_cmd_list_present, GetRenderTarget(Renderer_RenderTarget::frame_output));
        }

        // present
        if (is_standalone)
        {
            SubmitAndPresent();
        }

        // clear per frame data
        {
            m_lines_vertices.clear();
            m_icons.clear();
        }

        frame_num++;

        if (frame_num == 1)
        {
            SP_FIRE_EVENT(EventType::RendererOnFirstFrameCompleted);
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

        // generate jitter sample in case FSR (which also does TAA) is enabled
        Renderer_Upsampling upsampling_mode = GetOption<Renderer_Upsampling>(Renderer_Option::Upsampling);
        if (upsampling_mode == Renderer_Upsampling::Fsr3 || GetOption<Renderer_Antialiasing>(Renderer_Option::Antialiasing) == Renderer_Antialiasing::Taa)
        {
            RHI_AMD_FFX::FSR3_GenerateJitterSample(&jitter_offset.x, &jitter_offset.y);
            m_cb_frame_cpu.projection *= Matrix::CreateTranslation(Vector3(jitter_offset.x, jitter_offset.y, 0.0f));
        }
        else
        {
            jitter_offset = Vector2::Zero;
        }
        
        // update the remaining of the frame buffer
        m_cb_frame_cpu.view_projection_previous = m_cb_frame_cpu.view_projection;
        m_cb_frame_cpu.view_projection          = m_cb_frame_cpu.view * m_cb_frame_cpu.projection;
        m_cb_frame_cpu.view_projection_inv      = Matrix::Invert(m_cb_frame_cpu.view_projection);
        if (Camera* camera = World::GetCamera())
        {
            m_cb_frame_cpu.view_projection_previous_unjittered =  m_cb_frame_cpu.view_projection_unjittered;
            m_cb_frame_cpu.view_projection_unjittered          = m_cb_frame_cpu.view * camera->GetProjectionMatrix();
            m_cb_frame_cpu.camera_near                         = camera->GetNearPlane();
            m_cb_frame_cpu.camera_far                          = camera->GetFarPlane();
            m_cb_frame_cpu.camera_position_previous            = m_cb_frame_cpu.camera_position;
            m_cb_frame_cpu.camera_position                     = camera->GetEntity()->GetPosition();
            m_cb_frame_cpu.camera_forward                      = camera->GetEntity()->GetForward();
            m_cb_frame_cpu.camera_right                        = camera->GetEntity()->GetRight();
            m_cb_frame_cpu.camera_last_movement_time           = (m_cb_frame_cpu.camera_position - m_cb_frame_cpu.camera_position_previous).LengthSquared() != 0.0f
                ? static_cast<float>(Timer::GetTimeSec()) : m_cb_frame_cpu.camera_last_movement_time;
        }
        m_cb_frame_cpu.resolution_output           = m_resolution_output;
        m_cb_frame_cpu.resolution_render           = m_resolution_render;
        m_cb_frame_cpu.taa_jitter_previous         = m_cb_frame_cpu.taa_jitter_current;
        m_cb_frame_cpu.taa_jitter_current          = jitter_offset;
        m_cb_frame_cpu.time                        = Timer::GetTimeSec();
        m_cb_frame_cpu.delta_time                  = static_cast<float>(Timer::GetDeltaTimeSec());
        m_cb_frame_cpu.frame                       = static_cast<uint32_t>(frame_num);
        m_cb_frame_cpu.resolution_scale            = GetOption<float>(Renderer_Option::ResolutionScale);
        m_cb_frame_cpu.hdr_enabled                 = GetOption<bool>(Renderer_Option::Hdr) ? 1.0f : 0.0f;
        m_cb_frame_cpu.hdr_max_nits                = Display::GetLuminanceMax();
        m_cb_frame_cpu.hdr_white_point             = GetOption<float>(Renderer_Option::WhitePoint);
        m_cb_frame_cpu.gamma                       = GetOption<float>(Renderer_Option::Gamma);
        m_cb_frame_cpu.directional_light_intensity = World::GetDirectionalLight() ? World::GetDirectionalLight()->GetIntensityWatt() : 1.0f;
        m_cb_frame_cpu.camera_exposure             = World::GetCamera() ? World::GetCamera()->GetExposure() : 1.0f;

        // these must match what common_buffer.hlsl is reading
        m_cb_frame_cpu.set_bit(GetOption<bool>(Renderer_Option::ScreenSpaceReflections),      1 << 0);
        m_cb_frame_cpu.set_bit(GetOption<bool>(Renderer_Option::ScreenSpaceAmbientOcclusion), 1 << 1);
        m_cb_frame_cpu.set_bit(GetOption<bool>(Renderer_Option::Fog),                         1 << 2);

        // set
        GetBuffer(Renderer_Buffer::ConstantFrame)->Update(cmd_list, &m_cb_frame_cpu);
    }

    void Renderer::SetEntities(vector<shared_ptr<Entity>>& entities)
    {
        m_draw_calls.fill(Renderer_DrawCall());
        m_draw_call_count          = 0;
        m_bindless_materials_dirty = true;
        m_bindless_lights_dirty    = true;
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

    void Renderer::UpdateBuffers(RHI_CommandList* cmd_list)
    {
        // reset dynamic buffers and parse deletion queue
        {
            m_resource_index++;
            bool is_sync_point = m_resource_index == renderer_resource_frame_lifetime;
            if (is_sync_point)
            {
                m_resource_index = 0;

                // delete any rhi resources that have accumulated
                if (RHI_Device::DeletionQueueNeedsToParse())
                {
                    RHI_Device::QueueWaitAll();
                    RHI_Device::DeletionQueueParse();
                }

                // reset dynamic buffer offsets
                GetBuffer(Renderer_Buffer::ConstantFrame)->ResetOffset();
            }
        }

        // frame constant buffer (just a single buffer for the entire frame)
        UpdateFrameConstantBuffer(cmd_list);

        // line rendering buffer (used for debugging)
        AddLinesToBeRendered();

        UpdateBindlessBuffers(cmd_list);
    }

    void Renderer::DrawString(const string& text, const Vector2& position_screen_percentage)
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
            // shadow resolution
            else if (option == Renderer_Option::ShadowResolution)
            {
                value = clamp(value, static_cast<float>(resolution_shadow_min), static_cast<float>(RHI_Device::PropertyGetMaxTexture2dDimension()));
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
                        SP_LOG_INFO("This display doesn't support HDR");
                        return;
                    }
                }
            }
            else if (option == Renderer_Option::VariableRateShading)
            {
                if (value == 1.0f)
                {
                    if (!RHI_Device::PropertyIsShadingRateSupported())
                    { 
                        SP_LOG_INFO("This GPU doesn't support variable rate shading");
                        return;
                    }
                }
            }
        }

        // set new value
        m_options[option] = value;

        // handle cascading changes
        {
            // antialiasing
            if (option == Renderer_Option::Antialiasing)
            {
                bool taa_enabled = value == static_cast<float>(Renderer_Antialiasing::Taa) || value == static_cast<float>(Renderer_Antialiasing::TaaFxaa);
                bool fsr_enabled = GetOption<Renderer_Upsampling>(Renderer_Option::Upsampling) == Renderer_Upsampling::Fsr3;

                if (taa_enabled)
                {
                    if (!fsr_enabled)
                    {
                        m_options[Renderer_Option::Upsampling] = static_cast<float>(Renderer_Upsampling::Fsr3);
                        RHI_AMD_FFX::FSR3_ResetHistory();
                    }
                }
                else
                {
                    // implicitly disable FSR since it's doing TAA
                    if (fsr_enabled)
                    {
                        m_options[Renderer_Option::Upsampling] = static_cast<float>(Renderer_Upsampling::Linear);
                    }
                }
            }
            // upsampling
            else if (option == Renderer_Option::Upsampling)
            {
                bool taa_enabled = GetOption<Renderer_Antialiasing>(Renderer_Option::Antialiasing) == Renderer_Antialiasing::Taa;

                if (value == static_cast<float>(Renderer_Upsampling::Linear))
                {
                    if (taa_enabled)
                    {
                        m_options[Renderer_Option::Antialiasing] = static_cast<float>(Renderer_Antialiasing::Disabled);
                    }

                    RHI_AMD_FFX::Shutdown(AMD_FFX_Pass::Fsr);
                }
                else if (value == static_cast<float>(Renderer_Upsampling::Fsr3))
                {
                    if (!taa_enabled)
                    {
                        m_options[Renderer_Option::Antialiasing] = static_cast<float>(Renderer_Antialiasing::Taa);
                        RHI_AMD_FFX::FSR3_ResetHistory();
                    }
                }
            }
            else if (option == Renderer_Option::Hdr)
            {
                if (swap_chain)
                { 
                    swap_chain->SetHdr(value == 1.0f);
                }
            }
            else if (option == Renderer_Option::Vsync)
            {
                if (swap_chain)
                {
                    swap_chain->SetVsync(value == 1.0f);
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
            else if (option == Renderer_Option::GlobalIllumination)
            {
                if (value == 0.0)
                {
                    //RHI_FidelityFX::Shutdown(FidelityFX::BrixelizerGi);
                }
                else
                { 
                    //RHI_FidelityFX::BrixelizerGI_SetResolutionPercentage(value);
                }
            }
            else if (option == Renderer_Option::ScreenSpaceReflections)
            {
                if (value == 0.0)
                {
                    //RHI_FidelityFX::Shutdown(FidelityFX::Sssr);
                }
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
        return swap_chain.get();
    }
    
    void Renderer::BlitToBackBuffer(RHI_CommandList* cmd_list, RHI_Texture* texture)
    {
        cmd_list->BeginMarker("blit_to_back_buffer");
        cmd_list->Blit(texture, swap_chain.get());
        cmd_list->EndMarker();
    }

    void Renderer::SubmitAndPresent()
    {
        Profiler::TimeBlockStart("submit_and_present", TimeBlockType::Cpu, nullptr);
        {
            if (m_cmd_list_present->GetState() == RHI_CommandListState::Recording)
            {
                m_cmd_list_present->Submit(swap_chain->GetObjectId());
            }
            
            swap_chain->Present(m_cmd_list_present);
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

    void Renderer::SetGbufferTextures(RHI_CommandList* cmd_list)
    {
        cmd_list->SetTexture(Renderer_BindingsSrv::gbuffer_albedo,   GetRenderTarget(Renderer_RenderTarget::gbuffer_color));
        cmd_list->SetTexture(Renderer_BindingsSrv::gbuffer_normal,   GetRenderTarget(Renderer_RenderTarget::gbuffer_normal));
        cmd_list->SetTexture(Renderer_BindingsSrv::gbuffer_material, GetRenderTarget(Renderer_RenderTarget::gbuffer_material));
        cmd_list->SetTexture(Renderer_BindingsSrv::gbuffer_velocity, GetRenderTarget(Renderer_RenderTarget::gbuffer_velocity));
        cmd_list->SetTexture(Renderer_BindingsSrv::gbuffer_depth,    GetRenderTarget(Renderer_RenderTarget::gbuffer_depth));
    }

    void Renderer::UpdateBindlessBuffers(RHI_CommandList* cmd_list)
    {
        bool initialize = GetFrameNumber() == 0;
        if (initialize || !ProgressTracker::IsLoading())
        { 
            if (m_bindless_materials_dirty)
            {
                BindlessUpdateMaterialsParameters(cmd_list);
                RHI_Device::UpdateBindlessResources(&m_bindless_textures, GetBuffer(Renderer_Buffer::MaterialParameters), nullptr, nullptr, nullptr);
                m_bindless_materials_dirty = false;
            }
            
            if (m_bindless_lights_dirty)
            {
                BindlessUpdateLights(cmd_list);
                RHI_Device::UpdateBindlessResources(nullptr, nullptr, GetBuffer(Renderer_Buffer::LightParameters), nullptr, nullptr);
                m_bindless_lights_dirty = false;
            }
            
            if (m_bindless_abbs_dirty)
            {
                BindlessUpdateOccludersAndOccludes(cmd_list);
                RHI_Device::UpdateBindlessResources(nullptr, nullptr, nullptr, nullptr, GetBuffer(Renderer_Buffer::AABBs));
                m_bindless_abbs_dirty = true; // world space bounding boxes always need to update
            }
        }

        if (m_bindless_samplers_dirty)
        { 
            RHI_Device::UpdateBindlessResources(nullptr, nullptr, nullptr, &Renderer::GetSamplers(), nullptr);
            m_bindless_samplers_dirty = false;
        }
    }

    
    void Renderer::BindlessUpdateMaterialsParameters(RHI_CommandList* cmd_list)
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
                properties[count].ior                   = material->GetProperty(MaterialProperty::Ior);
                properties[count].world_space_uv        = material->GetProperty(MaterialProperty::WorldSpaceUv);

                // flags
                properties[count].flags  = material->HasTextureOfType(MaterialTextureType::Height)    ? (1U << 0)  : 0;
                properties[count].flags |= material->HasTextureOfType(MaterialTextureType::Normal)    ? (1U << 1)  : 0;
                properties[count].flags |= material->HasTextureOfType(MaterialTextureType::Color)     ? (1U << 2)  : 0;
                properties[count].flags |= material->HasTextureOfType(MaterialTextureType::Roughness) ? (1U << 3)  : 0;
                properties[count].flags |= material->HasTextureOfType(MaterialTextureType::Metalness) ? (1U << 4)  : 0;
                properties[count].flags |= material->HasTextureOfType(MaterialTextureType::AlphaMask) ? (1U << 5)  : 0;
                properties[count].flags |= material->HasTextureOfType(MaterialTextureType::Emission)  ? (1U << 6)  : 0;
                properties[count].flags |= material->HasTextureOfType(MaterialTextureType::Occlusion) ? (1U << 7)  : 0;
                properties[count].flags |= material->GetProperty(MaterialProperty::IsTerrain)         ? (1U << 8)  : 0;
                properties[count].flags |= material->GetProperty(MaterialProperty::WindAnimation)     ? (1U << 9)  : 0;
                properties[count].flags |= material->GetProperty(MaterialProperty::ColorFromPosition) ? (1U << 10) : 0;
                properties[count].flags |= material->GetProperty(MaterialProperty::IsGrassBlasde)     ? (1U << 11) : 0;
                properties[count].flags |= material->GetProperty(MaterialProperty::IsWater)           ? (1U << 12) : 0;
                properties[count].flags |= material->GetProperty(MaterialProperty::Tessellation)      ? (1U << 13) : 0;
                // when changing the bit flags, ensure that you also update the Surface struct in common_structs.hlsl, so that it reads those flags as expected
            }
    
            // textures
            {
                // iterate through all texture types and their slots
                for (uint32_t type = 0; type < static_cast<uint32_t>(MaterialTextureType::Max); type++)
                {
                    for (uint32_t slot = 0; slot < Material::slots_per_texture_type; slot++)
                    {
                        // calculate the final index in the bindless array
                        uint32_t bindless_index = count + (type * Material::slots_per_texture_type) + slot;
                        
                        // get the texture from the material using type and slot
                        m_bindless_textures[bindless_index] = material->GetTexture(static_cast<MaterialTextureType>(type), slot);
                    }
                }
            }
    
            material->SetIndex(count);

            // update index increment to account for all texture slots
            count += static_cast<uint32_t>(MaterialTextureType::Max) * Material::slots_per_texture_type;
        };
    
        auto update_entities = [update_material]()
        {
            for (const shared_ptr<Entity>& entity : World::GetEntities())
            {
                if (entity->IsActive())
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

    void Renderer::BindlessUpdateLights(RHI_CommandList* cmd_list)
    {
        uint32_t count = 0;

        // cpu
        {
            // clear
            m_bindless_lights.fill(Sb_Light());

            // go through each light
            for (const shared_ptr<Entity>& entity : World::GetEntities())
            {
                if (Light* light = entity->GetComponent<Light>())
                {
                    light->SetIndex(count);

                    // set light properties
                    if (RHI_Texture* texture = light->GetDepthTexture())
                    {
                        for (uint32_t i = 0; i < texture->GetDepth(); i++)
                        {
                            if (light->GetLightType() == LightType::Point)
                            {
                                // we do paraboloid projection in the vertex shader so we only want the view here
                                m_bindless_lights[count].view_projection[i] = light->GetViewMatrix(i);
                            }
                            else
                            { 
                                m_bindless_lights[count].view_projection[i] = light->GetViewMatrix(i) * light->GetProjectionMatrix(i);
                            }
                        }
                    }

                    m_bindless_lights[count].intensity  = light->GetIntensityWatt();
                    m_bindless_lights[count].range      = light->GetRange();
                    m_bindless_lights[count].angle      = light->GetAngle();
                    m_bindless_lights[count].color      = light->GetColor();
                    m_bindless_lights[count].position   = light->GetEntity()->GetPosition();
                    m_bindless_lights[count].direction  = light->GetEntity()->GetForward();
                    m_bindless_lights[count].flags      = 0;
                    m_bindless_lights[count].flags     |= light->GetLightType() == LightType::Directional ? (1 << 0) : 0;
                    m_bindless_lights[count].flags     |= light->GetLightType() == LightType::Point       ? (1 << 1) : 0;
                    m_bindless_lights[count].flags     |= light->GetLightType() == LightType::Spot        ? (1 << 2) : 0;
                    m_bindless_lights[count].flags     |= light->GetFlag(LightFlags::Shadows)             ? (1 << 3) : 0;
                    m_bindless_lights[count].flags     |= light->GetFlag(LightFlags::ShadowsScreenSpace)  ? (1 << 4) : 0;
                    m_bindless_lights[count].flags     |= light->GetFlag(LightFlags::Volumetric)          ? (1 << 5) : 0;
                    // when changing the bit flags, ensure that you also update the Light struct in common_structs.hlsl, so that it reads those flags as expected

                    count++;
                }
            }
        }

        // gpu
        RHI_Buffer* buffer = GetBuffer(Renderer_Buffer::LightParameters);
        buffer->ResetOffset();
        buffer->Update(cmd_list, &m_bindless_lights[0], buffer->GetStride() * count);
    }

    void Renderer::BindlessUpdateOccludersAndOccludes(RHI_CommandList* cmd_list)
    {
        // clear
        m_bindless_aabbs.fill(Sb_Aabb());
        uint32_t count = 0;

        // cpu
        for (uint32_t i = 0; i < m_draw_call_count; i++)
        {
            const Renderer_DrawCall& draw_call   = m_draw_calls[i];
            Renderable* renderable               = draw_call.renderable;
            const BoundingBox& aabb              = renderable->HasInstancing() ? renderable->GetBoundingBoxInstanceGroup(draw_call.instance_group_index) : renderable->GetBoundingBox();
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

    void Renderer::Screenshot(const string& file_path)
    {
        GetRenderTarget(Renderer_RenderTarget::frame_output)->SaveAsImage(file_path);
    }

    void Renderer::BuildDrawCallsAndOccluders(RHI_CommandList* cmd_list)
    {
        m_draw_call_count = 0;

        if (ProgressTracker::IsLoading())
            return;

        cmd_list->BeginTimeblock("build_draw_calls_and_occluders", false, false);
        {
            // build draw calls and sort them
            {  
                for (const shared_ptr<Entity>& entity : World::GetEntities())
                {
                    if (!entity->IsActive())
                        continue;

                    if (Renderable* renderable = entity->GetComponent<Renderable>())
                    {
                        if (renderable->GetMaterial() && renderable->GetMaterial()->IsTransparent())
                        {
                            m_transparents_present = true;
                        }
                
                        if (renderable->HasInstancing())
                        {
                            for (uint32_t group_index = 0; group_index < renderable->GetInstanceGroupCount(); group_index++)
                            {
                                if (renderable->IsVisible(group_index))
                                {
                                    uint32_t instance_start_index = renderable->GetInstanceGroupStartIndex(group_index);
                                    uint32_t instance_count       = renderable->GetInstanceGroupCount(group_index);
                                    instance_count                = min(instance_count, renderable->GetInstanceCount() - instance_start_index);
                                    if (instance_count == 0)
                                        continue;

                                    Renderer_DrawCall& draw_call   = m_draw_calls[m_draw_call_count++];
                                    draw_call.renderable           = renderable;
                                    draw_call.instance_group_index = group_index;
                                    draw_call.distance_squared     = renderable->GetDistanceSquared(group_index);
                                    draw_call.instance_start_index = instance_start_index;
                                    draw_call.instance_count       = instance_count;
                                    draw_call.lod_index            = min(renderable->GetLodIndex(group_index), renderable->GetLodCount() - 1);
                                    draw_call.is_occluder          = false;
                                }
                            }
                        }
                        else if (renderable->IsVisible())
                        {
                            Renderer_DrawCall& draw_call = m_draw_calls[m_draw_call_count++];
                            draw_call.renderable         = renderable;
                            draw_call.distance_squared   = renderable->GetDistanceSquared();
                            draw_call.lod_index          = min(renderable->GetLodIndex(), renderable->GetLodCount() - 1);
                            draw_call.is_occluder        = false;
                        }
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

            // select occluders by finding the top n largest screen-space bounding boxes
            {
                // lambda to compute screen-space area of a bounding box
                auto compute_screen_space_area = [&](const BoundingBox& aabb_world) -> float
                {
                    // project aabb to screen space using camera function
                    math::Rectangle rect_screen = World::GetCamera()->WorldToScreenCoordinates(aabb_world);
                
                    // compute screen-space dimensions using rectangle's left, top, right, bottom
                    float width  = rect_screen.right - rect_screen.left;
                    float height = rect_screen.bottom - rect_screen.top;
                
                    // compute area and ensure it's non-negative
                    float area = width * height;
                    return area > 0.0f ? area : 0.0f;
                };
            
                // temporary storage for draw call areas
                struct DrawCallArea {
                    uint32_t index;
                    float area;
                };
                vector<DrawCallArea> areas;
                areas.reserve(m_draw_call_count); // pre-allocate to avoid reallocations
            
                // collect screen-space areas for eligible draw calls
                for (uint32_t i = 0; i < m_draw_call_count; i++)
                {
                    Renderer_DrawCall& draw_call = m_draw_calls[i];
                    Renderable* renderable       = draw_call.renderable;
                    Material* material           = renderable->GetMaterial();
            
                    // skip any draw calls that have a mesh that you can see through (transparent, instanced, non-solid)
                    bool is_solid = material->GetProperty(MaterialProperty::IsTerrain) || renderable->IsSolid(); // IsSolid() is still unreliable for some meshes, like terrain, temp hack
                    if (!material || material->IsTransparent() || renderable->HasInstancing() || !is_solid)
                        continue;
            
                    // get bounding box
                    const BoundingBox& aabb_world = renderable->GetBoundingBox();

                    // compute screen-space area and store it
                    float screen_area = compute_screen_space_area(aabb_world);
                    areas.push_back({i, screen_area});
                }
            
                // sort draw calls by screen-space area (descending)
                sort(areas.begin(), areas.end(), [](const DrawCallArea& a, const DrawCallArea& b)
                {
                    return a.area > b.area;
                });
            
                // select the top n occluders
                const uint32_t max_occluders = 32;
                uint32_t occluder_count      = min(max_occluders, static_cast<uint32_t>(areas.size()));
                for (uint32_t i = 0; i < occluder_count; i++)
                {
                    m_draw_calls[areas[i].index].is_occluder = true;
                }
            }
        }
        cmd_list->EndTimeblock();
    }
}
