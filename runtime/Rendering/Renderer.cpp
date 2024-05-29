/*
Copyright(c) 2016-2024 Panos Karabelas

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

//= INCLUDES ===============================
#include "pch.h"
#include "Renderer.h"
#include "ThreadPool.h"
#include "ProgressTracker.h"
#include "../Profiling/Profiler.h"
#include "../Core/Window.h"
#include "../Input/Input.h"
#include "../Display/Display.h"
#include "../RHI/RHI_Device.h"
#include "../RHI/RHI_SwapChain.h"
#include "../RHI/RHI_Queue.h"
#include "../RHI/RHI_ConstantBuffer.h"
#include "../RHI/RHI_Implementation.h"
#include "../RHI/RHI_FidelityFX.h"
#include "../RHI/RHI_StructuredBuffer.h"
#include "../World/Entity.h"
#include "../World/Components/Light.h"
#include "../World/Components/Camera.h"
#include "../World/Components/AudioSource.h"
//==========================================

//= NAMESPACES ===============
using namespace std;
using namespace Spartan::Math;
//============================

namespace Spartan
{
    // constant and push constant buffers
    Cb_Frame Renderer::m_cb_frame_cpu;
    Pcb_Pass Renderer::m_pcb_pass_cpu;

    // line rendering
    shared_ptr<RHI_VertexBuffer> Renderer::m_vertex_buffer_lines;
    vector<RHI_Vertex_PosCol> Renderer::m_line_vertices;
    vector<float> Renderer::m_lines_duration;
    uint32_t Renderer::m_lines_index_depth_off;
    uint32_t Renderer::m_lines_index_depth_on;

    // misc
    uint32_t Renderer::m_resource_index                           = 0;
    atomic<bool> Renderer::m_resources_created                    = false;
    atomic<uint32_t> Renderer::m_environment_mips_to_filter_count = 0;
    unordered_map<Renderer_Entity, vector<shared_ptr<Entity>>> Renderer::m_renderables;
    mutex Renderer::m_mutex_renderables;

    namespace
    {
        // resolution & viewport
        Math::Vector2 m_resolution_render = Math::Vector2::Zero;
        Math::Vector2 m_resolution_output = Math::Vector2::Zero;
        RHI_Viewport m_viewport           = RHI_Viewport(0, 0, 0, 0);

        // rhi resources
        shared_ptr<RHI_SwapChain> swap_chain;
        const uint8_t swap_chain_buffer_count = 2;
        RHI_CommandList* cmd_current          = nullptr;

        // bindless
        static array<RHI_Texture*, rhi_max_array_size> bindless_textures;
        bool bindless_materials_dirty = true;

        // misc
        unordered_map<Renderer_Option, float> m_options;
        uint64_t frame_num                   = 0;
        Math::Vector2 jitter_offset          = Math::Vector2::Zero;
        const uint32_t resolution_shadow_min = 128;
        float near_plane                     = 0.0f;
        float far_plane                      = 1.0f;
        bool dirty_orthographic_projection   = true;

        float get_directional_light_intensity_lumens(const vector<shared_ptr<Entity>>& lights)
        {
            float intensity = 0.0f;
            for (const shared_ptr<Entity>& entity : lights)
            {
                if (const shared_ptr<Light>& light = entity->GetComponent<Light>())
                {
                    if (light->GetLightType() == LightType::Directional)
                    {
                        intensity = light->GetIntensityWatt();
                        break;
                    }
                }
            }

            return intensity;
        }
    }

    void Renderer::Initialize()
    {
        RHI_Device::Initialize();

        // resolution
        {
            // note #1: settings can override default resolutions based on loaded XML configurations
            // note #2: if settings are absent, the editor will set the render/viewport resolutions to it's viewport size

            uint32_t width  = Window::GetWidth();
            uint32_t height = Window::GetHeight();

            // the resolution of the output frame (we can upscale to that linearly or with fsr 2)
            SetResolutionOutput(width, height, false);

            // set the render resolution to something smaller than the output resolution
            // this is done because FSR 2 is not good at doing TAA if the render resolution is the same as the output resolution
            SetResolutionRender(1920, 1080, false);

            // the resolution/size of the editor's viewport. This is overridden by the editor based on the actual viewport size
            SetViewport(static_cast<float>(width), static_cast<float>(height));
        }

        // swap chain
        swap_chain = make_shared<RHI_SwapChain>
        (
            Window::GetHandleSDL(),
            Window::GetWidth(),
            Window::GetHeight(),
            // present mode: for v-sync, we could mailbox for lower latency, but fifo is always supported, so we'll assume that
            GetOption<bool>(Renderer_Option::Vsync) ? RHI_Present_Mode::Fifo : RHI_Present_Mode::Immediate,
            swap_chain_buffer_count,
            Display::GetHdr(),
            "renderer"
        );

        // fidelityfx suite
        RHI_FidelityFX::Initialize();

        // load/create resources
        {
            // reduce startup time by doing expensive operations in another thread
            ThreadPool::AddTask([]()
            {
                m_resources_created = false;
                CreateStandardMeshes();
                CreateStandardTextures();
                CreateStandardMaterials();
                CreateFonts();
                CreateShaders();
                m_resources_created = true;
            });

            CreateBuffers();
            CreateDepthStencilStates();
            CreateRasterizerStates();
            CreateBlendStates();
            CreateRenderTargets(true, true, true);
            CreateSamplers();
        }

        // events
        {
            // subscribe
            SP_SUBSCRIBE_TO_EVENT(EventType::WorldClear,              SP_EVENT_HANDLER_STATIC(OnClear));
            SP_SUBSCRIBE_TO_EVENT(EventType::WindowFullScreenToggled, SP_EVENT_HANDLER_STATIC(OnFullScreenToggled));
            SP_SUBSCRIBE_TO_EVENT(EventType::MaterialOnChanged,       SP_EVENT_HANDLER_STATIC(BindlessUpdateMaterials));
            SP_SUBSCRIBE_TO_EVENT(EventType::LightOnChanged,          SP_EVENT_HANDLER_STATIC(BindlessUpdateLights));

            // fire
            SP_FIRE_EVENT(EventType::RendererOnInitialized);
        }

        // options
        m_options.clear();
        SetOption(Renderer_Option::Hdr,                           swap_chain->IsHdr() ? 1.0f : 0.0f);
        SetOption(Renderer_Option::WhitePoint,                    350.0f);
        SetOption(Renderer_Option::Tonemapping,                   static_cast<float>(Renderer_Tonemapping::Max));
        SetOption(Renderer_Option::Bloom,                         0.03f);                                                // non-zero values activate it and define the blend factor
        SetOption(Renderer_Option::MotionBlur,                    1.0f);
        SetOption(Renderer_Option::ScreenSpaceGlobalIllumination, 1.0f);
        SetOption(Renderer_Option::ScreenSpaceShadows,            static_cast<float>(Renderer_ScreenspaceShadow::Bend));
        SetOption(Renderer_Option::ScreenSpaceReflections,        1.0f);
        SetOption(Renderer_Option::Anisotropy,                    16.0f);
        SetOption(Renderer_Option::ShadowResolution,              2048.0f);
        SetOption(Renderer_Option::Exposure,                      1.0f);
        SetOption(Renderer_Option::Sharpness,                     0.5f);                                                 // becomes the upsampler's sharpness as well
        SetOption(Renderer_Option::Fog,                           0.3f);                                                 // controls the intensity of the volumetric fog as well
        SetOption(Renderer_Option::FogVolumetric,                 1.0f);                                                 // these is only a toggle for the volumetric fog
        SetOption(Renderer_Option::Antialiasing,                  static_cast<float>(Renderer_Antialiasing::Taa));       // this is using fsr 2 for taa
        SetOption(Renderer_Option::Upsampling,                    static_cast<float>(Renderer_Upsampling::Fsr2));
        SetOption(Renderer_Option::ResolutionScale,               1.0f);
        SetOption(Renderer_Option::VariableRateShading,           0.0f);
        SetOption(Renderer_Option::Vsync,                         0.0f);
        SetOption(Renderer_Option::TransformHandle,               1.0f);
        SetOption(Renderer_Option::SelectionOutline,              1.0f);
        SetOption(Renderer_Option::Grid,                          1.0f);
        SetOption(Renderer_Option::Lights,                        1.0f);
        SetOption(Renderer_Option::Physics,                       0.0f);
        SetOption(Renderer_Option::PerformanceMetrics,            1.0f);
        SetOption(Renderer_Option::OcclusionCulling,              0.0f); // disabled by default as it's a WIP (you can see the query delays)
    }

    void Renderer::Shutdown()
    {
        SP_FIRE_EVENT(EventType::RendererOnShutdown);

        // manually invoke the deconstructors so that ParseDeletionQueue()
        // releases their rhi resources before device destruction
        {
            DestroyResources();

            m_renderables.clear();
            swap_chain            = nullptr;
            m_vertex_buffer_lines = nullptr;
        }

        RHI_FidelityFX::Destroy();
        RHI_Device::Destroy();
    }

    void Renderer::Tick()
    {
        // don't waste cpu/gpu time if nothing can be seen
        if (Window::IsMinimized() || !m_resources_created)
            return;

        if (frame_num == 1)
        {
            SP_FIRE_EVENT(EventType::RendererOnFirstFrameCompleted);
        }

        RHI_Device::Tick(frame_num);

        // begin command list
        RHI_Queue* queue = RHI_Device::GetQueue(RHI_Queue_Type::Graphics);
        cmd_current      = queue->GetCommandList();
        cmd_current->Begin(queue);

        OnSyncPoint(cmd_current);
        ProduceFrame(cmd_current);

        // blit to back buffer when not in editor mode
        bool is_standalone = !Engine::IsFlagSet(EngineMode::Editor);
        if (is_standalone)
        {
            BlitToBackBuffer(cmd_current, GetRenderTarget(Renderer_RenderTarget::frame_output).get());
        }

        // present
        if (is_standalone)
        {
            Present();
        }

        frame_num++;
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
        Display::RegisterDisplayMode(static_cast<uint32_t>(width), static_cast<uint32_t>(height), static_cast<uint32_t>(Timer::GetFpsLimit()), Display::GetIndex());

        SP_LOG_INFO("Output resolution output has been set to %dx%d", width, height);
    }

    void Renderer::UpdateConstantBufferFrame(RHI_CommandList* cmd_list)
    {
        // matrices
        {
            if (shared_ptr<Camera> camera = GetCamera())
            {
                if (near_plane != camera->GetNearPlane() || far_plane != camera->GetFarPlane())
                {
                    near_plane                    = camera->GetNearPlane();
                    far_plane                     = camera->GetFarPlane();
                    dirty_orthographic_projection = true;
                }

                m_cb_frame_cpu.view       = camera->GetViewMatrix();
                m_cb_frame_cpu.projection = camera->GetProjectionMatrix();
            }

            if (dirty_orthographic_projection)
            { 
                // near clip does not affect depth accuracy in orthographic projection, so set it to 0 to avoid problems which can result an infinitely small [3,2] (NaN) after the multiplication below.
                Matrix projection_ortho              = Matrix::CreateOrthographicLH(m_viewport.width, m_viewport.height, 0.0f, far_plane);
                m_cb_frame_cpu.view_projection_ortho = Matrix::CreateLookAtLH(Vector3(0, 0, -near_plane), Vector3::Forward, Vector3::Up) * projection_ortho;
                dirty_orthographic_projection        = false;
            }
        }

        // generate jitter sample in case FSR (which also does TAA) is enabled
        Renderer_Upsampling upsampling_mode = GetOption<Renderer_Upsampling>(Renderer_Option::Upsampling);
        if (upsampling_mode == Renderer_Upsampling::Fsr2 || GetOption<Renderer_Antialiasing>(Renderer_Option::Antialiasing) == Renderer_Antialiasing::Taa)
        {
            RHI_FidelityFX::FSR2_GenerateJitterSample(&jitter_offset.x, &jitter_offset.y);
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
        if (shared_ptr<Camera> camera = GetCamera())
        {
            m_cb_frame_cpu.view_projection_unjittered = m_cb_frame_cpu.view * camera->GetProjectionMatrix();
            m_cb_frame_cpu.camera_near                = camera->GetNearPlane();
            m_cb_frame_cpu.camera_far                 = camera->GetFarPlane();
            m_cb_frame_cpu.camera_position_previous   = m_cb_frame_cpu.camera_position;
            m_cb_frame_cpu.camera_position            = camera->GetEntity()->GetPosition();
            m_cb_frame_cpu.camera_direction           = camera->GetEntity()->GetForward();
            m_cb_frame_cpu.camera_last_movement_time  = (m_cb_frame_cpu.camera_position - m_cb_frame_cpu.camera_position_previous).LengthSquared() != 0.0f
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
        m_cb_frame_cpu.directional_light_intensity = get_directional_light_intensity_lumens(m_renderables[Renderer_Entity::Light]);

        // these must match what common_buffer.hlsl is reading
        m_cb_frame_cpu.set_bit(GetOption<bool>(Renderer_Option::ScreenSpaceReflections),        1 << 0);
        m_cb_frame_cpu.set_bit(GetOption<bool>(Renderer_Option::ScreenSpaceGlobalIllumination), 1 << 1);
        m_cb_frame_cpu.set_bit(GetOption<bool>(Renderer_Option::Fog),                           1 << 2);

        // set
        shared_ptr<RHI_ConstantBuffer>& buffer = GetConstantBufferFrame();
        buffer->Update(&m_cb_frame_cpu);
    }

    void Renderer::SetEntities(vector<shared_ptr<Entity>>& entities)
    {
        m_mutex_renderables.lock();

        // clear previous state
        m_renderables.clear();

        for (shared_ptr<Entity>& entity : entities)
        {
            if (!entity->IsActive())
                continue;

            if (shared_ptr<Renderable> renderable = entity->GetComponent<Renderable>())
            {
                if (Material* material = renderable->GetMaterial())
                {
                    if (material->IsVisible())
                    {
                        // a mesh can be uninitialized if it's currently loading in a different thread
                        // but we don't keep anything uninitialized in what the renderer is processing
                        if (renderable->GetVertexBuffer() && renderable->GetIndexBuffer())
                        { 
                            m_renderables[Renderer_Entity::Mesh].emplace_back(entity);
                        }
                    }
                }
            }

            if (shared_ptr<Light> light = entity->GetComponent<Light>())
            {
                m_renderables[Renderer_Entity::Light].emplace_back(entity);
            }

            if (shared_ptr<Camera> camera = entity->GetComponent<Camera>())
            {
                m_renderables[Renderer_Entity::Camera].emplace_back(entity);
            }

            if (shared_ptr<AudioSource> audio_source = entity->GetComponent<AudioSource>())
            {
                m_renderables[Renderer_Entity::AudioSource].emplace_back(entity);
            }
        }

        m_mutex_renderables.unlock();

        // update bindless resources
        BindlessUpdateMaterials();
        BindlessUpdateLights();
    }

    bool Renderer::CanUseCmdList()
    {
        RHI_CommandList* cmd_list = RHI_Device::GetQueue(RHI_Queue_Type::Graphics)->GetCommandList();
        return cmd_list->GetState() == RHI_CommandListState::Recording;
    }

    void Renderer::OnClear()
    {
        m_renderables.clear();
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

    void Renderer::OnSyncPoint(RHI_CommandList* cmd_list)
    {
        // is_sync_point: the command pool has exhausted its command lists and 
        // is about to reset them, this is an opportune moment for us to perform
        // certain operations, knowing that no rendering commands are currently
        // executing and no resources are being used by any command list
        m_resource_index++;
        bool is_sync_point = m_resource_index == resources_frame_lifetime;
        if (is_sync_point)
        {
            m_resource_index = 0;

            // delete any rhi resources that have accumulated
            if (RHI_Device::DeletionQueueNeedsToParse())
            {
                RHI_Device::QueueWaitAll();
                RHI_Device::DeletionQueueParse();
                SP_LOG_INFO("Parsed deletion queue");
            }

            // reset dynamic buffer offsets
            GetStructuredBuffer(Renderer_StructuredBuffer::Spd)->ResetOffset();
            GetConstantBufferFrame()->ResetOffset();

            if (bindless_materials_dirty)
            {
                RHI_Device::UpdateBindlessResources(nullptr, &bindless_textures);
                bindless_materials_dirty = false;
            }
        }

        UpdateConstantBufferFrame(cmd_current);
        AddLinesToBeRendered();

        // filter environment on directional light change
        {
            static Quaternion rotation;
            static float intensity;
            static Color color;

            for (const shared_ptr<Entity>& entity : m_renderables[Renderer_Entity::Light])
            {
                if (const shared_ptr<Light>& light = entity->GetComponent<Light>())
                {
                    if (light->GetLightType() == LightType::Directional)
                    {
                        if (light->GetEntity()->GetRotation() != rotation ||
                            light->GetIntensityLumens() != intensity ||
                            light->GetColor() != color
                            )
                        {
                            rotation  = light->GetEntity()->GetRotation();
                            intensity = light->GetIntensityLumens();
                            color     = light->GetColor();

                            m_environment_mips_to_filter_count = GetRenderTarget(Renderer_RenderTarget::skysphere)->GetMipCount() - 1;
                        }
                    }
                }
            }
        }
    }

    void Renderer::DrawString(const string& text, const Vector2& position_screen_percentage)
	{
        if (shared_ptr<Font>& font = GetFont())
        {
            font->AddText(text, position_screen_percentage);
        }
	}
    
    void Renderer::SetOption(Renderer_Option option, float value)
    {
        // clamp value
        {
            // anisotropy
            if (option == Renderer_Option::Anisotropy)
            {
                value = Helper::Clamp(value, 0.0f, 16.0f);
            }
            // shadow resolution
            else if (option == Renderer_Option::ShadowResolution)
            {
                value = Helper::Clamp(value, static_cast<float>(resolution_shadow_min), static_cast<float>(RHI_Device::PropertyGetMaxTexture2dDimension()));
            }
            else if (option == Renderer_Option::ResolutionScale)
            {
                value = Helper::Clamp(value, 0.5f, 1.0f);
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
                bool fsr_enabled = GetOption<Renderer_Upsampling>(Renderer_Option::Upsampling) == Renderer_Upsampling::Fsr2;

                if (taa_enabled)
                {
                    // implicitly enable FSR since it's doing TAA.
                    if (!fsr_enabled)
                    {
                        m_options[Renderer_Option::Upsampling] = static_cast<float>(Renderer_Upsampling::Fsr2);
                        RHI_FidelityFX::FSR2_ResetHistory();
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
                    // Implicitly disable TAA since FSR 2.0 is doing it
                    if (taa_enabled)
                    {
                        m_options[Renderer_Option::Antialiasing] = static_cast<float>(Renderer_Antialiasing::Disabled);
                        SP_LOG_INFO("Disabled TAA since it's done by FSR 2.0.");
                    }
                }
                else if (value == static_cast<float>(Renderer_Upsampling::Fsr2))
                {
                    // Implicitly enable TAA since FSR 2.0 is doing it
                    if (!taa_enabled)
                    {
                        m_options[Renderer_Option::Antialiasing] = static_cast<float>(Renderer_Antialiasing::Taa);
                        RHI_FidelityFX::FSR2_ResetHistory();
                        SP_LOG_INFO("Enabled TAA since FSR 2.0 does it.");
                    }
                }
            }
            // shadow resolution
            else if (option == Renderer_Option::ShadowResolution)
            {
                const auto& light_entities = m_renderables[Renderer_Entity::Light];
                for (const auto& light_entity : light_entities)
                {
                    auto light = light_entity->GetComponent<Light>();
                    if (light->IsFlagSet(LightFlags::Shadows))
                    {
                        light->RefreshShadowMap();
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
            else if (option == Renderer_Option::FogVolumetric || option == Renderer_Option::ScreenSpaceShadows)
            {
                SP_FIRE_EVENT(EventType::LightOnChanged);
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
        return swap_chain.get();
    }
    
    void Renderer::BlitToBackBuffer(RHI_CommandList* cmd_list, RHI_Texture* texture)
    {
        cmd_list->BeginMarker("blit_to_back_buffer");
        cmd_list->Blit(texture, swap_chain.get());
        cmd_list->EndMarker();
    }

    void Renderer::Present()
    {
        // submit
        RHI_Queue* queue          = RHI_Device::GetQueue(RHI_Queue_Type::Graphics);
        RHI_CommandList* cmd_list = queue->GetCommandList();
        if (cmd_list->GetState() == RHI_CommandListState::Recording)
        { 
            cmd_list->Submit(queue, swap_chain->GetObjectId());
        }

        // present
        swap_chain->Present();
    }

    RHI_CommandList* Renderer::GetCmdList()
    {
        return cmd_current;
    }

    RHI_Api_Type Renderer::GetRhiApiType()
    {
        return RHI_Context::api_type;
    }

    RHI_Texture* Renderer::GetFrameTexture()
    {
        return GetRenderTarget(Renderer_RenderTarget::frame_output).get();
    }

    uint64_t Renderer::GetFrameNum()
    {
        return frame_num;
    }

    shared_ptr<Camera> Renderer::GetCamera()
    {
        if (m_renderables[Renderer_Entity::Camera].empty())
            return nullptr;

        return m_renderables[Renderer_Entity::Camera].front()->GetComponent<Camera>();
    }

    unordered_map<Renderer_Entity, vector<shared_ptr<Entity>>>& Renderer::GetEntities()
    {
        return m_renderables;
    }

    vector<shared_ptr<Entity>> Renderer::GetEntitiesLights()
    {
        return m_renderables[Renderer_Entity::Light];
    }
    
    void Renderer::SetGbufferTextures(RHI_CommandList* cmd_list)
    {
        cmd_list->SetTexture(Renderer_BindingsSrv::gbuffer_albedo,         GetRenderTarget(Renderer_RenderTarget::gbuffer_color));
        cmd_list->SetTexture(Renderer_BindingsSrv::gbuffer_normal,         GetRenderTarget(Renderer_RenderTarget::gbuffer_normal));
        cmd_list->SetTexture(Renderer_BindingsSrv::gbuffer_material,       GetRenderTarget(Renderer_RenderTarget::gbuffer_material));
        cmd_list->SetTexture(Renderer_BindingsSrv::gbuffer_velocity,       GetRenderTarget(Renderer_RenderTarget::gbuffer_velocity));
        cmd_list->SetTexture(Renderer_BindingsSrv::gbuffer_depth,          GetRenderTarget(Renderer_RenderTarget::gbuffer_depth));
        cmd_list->SetTexture(Renderer_BindingsSrv::gbuffer_depth_backface, GetRenderTarget(Renderer_RenderTarget::gbuffer_depth_backface));
        cmd_list->SetTexture(Renderer_BindingsSrv::gbuffer_depth_opaque,   GetRenderTarget(Renderer_RenderTarget::gbuffer_depth_opaque));
    }
    
    void Renderer::BindlessUpdateMaterials()
    {
        static array<Sb_Material, rhi_max_array_size> properties; // mapped to the gpu as a structured properties buffer
        static unordered_set<uint64_t> unique_material_ids;
        static uint32_t index = 0;

        auto update_material = [](Material* material)
        {
            // check if the material's ID is already processed
            if (unique_material_ids.find(material->GetObjectId()) != unique_material_ids.end())
                return;

            // if not, add it to the list
            unique_material_ids.insert(material->GetObjectId());

            // properties
            {
                SP_ASSERT(index < rhi_max_array_size);

                properties[index].world_space_height     = material->GetProperty(MaterialProperty::WorldSpaceHeight);
                properties[index].color.x                = material->GetProperty(MaterialProperty::ColorR);
                properties[index].color.y                = material->GetProperty(MaterialProperty::ColorG);
                properties[index].color.z                = material->GetProperty(MaterialProperty::ColorB);
                properties[index].color.w                = material->GetProperty(MaterialProperty::ColorA);
                properties[index].tiling_uv.x            = material->GetProperty(MaterialProperty::TextureTilingX);
                properties[index].tiling_uv.y            = material->GetProperty(MaterialProperty::TextureTilingY);
                properties[index].offset_uv.x            = material->GetProperty(MaterialProperty::TextureOffsetX);
                properties[index].offset_uv.y            = material->GetProperty(MaterialProperty::TextureOffsetY);
                properties[index].roughness_mul          = material->GetProperty(MaterialProperty::Roughness);
                properties[index].metallic_mul           = material->GetProperty(MaterialProperty::Metalness);
                properties[index].normal_mul             = material->GetProperty(MaterialProperty::Normal);
                properties[index].height_mul             = material->GetProperty(MaterialProperty::Height);
                properties[index].anisotropic            = material->GetProperty(MaterialProperty::Anisotropic);
                properties[index].anisotropic_rotation   = material->GetProperty(MaterialProperty::AnisotropicRotation);
                properties[index].clearcoat              = material->GetProperty(MaterialProperty::Clearcoat);
                properties[index].clearcoat_roughness    = material->GetProperty(MaterialProperty::Clearcoat_Roughness);
                properties[index].sheen                  = material->GetProperty(MaterialProperty::Sheen);
                properties[index].sheen_tint             = material->GetProperty(MaterialProperty::SheenTint);
                properties[index].subsurface_scattering  = material->GetProperty(MaterialProperty::SubsurfaceScattering);
                properties[index].ior                    = material->GetProperty(MaterialProperty::Ior);
                properties[index].flags                 |= material->GetProperty(MaterialProperty::SingleTextureRoughnessMetalness) ? (1U << 0) : 0;
                properties[index].flags                 |= material->HasTexture(MaterialTexture::Height)               ? (1U << 1)  : 0;
                properties[index].flags                 |= material->HasTexture(MaterialTexture::Normal)               ? (1U << 2)  : 0;
                properties[index].flags                 |= material->HasTexture(MaterialTexture::Color)                ? (1U << 3)  : 0;
                properties[index].flags                 |= material->HasTexture(MaterialTexture::Roughness)            ? (1U << 4)  : 0;
                properties[index].flags                 |= material->HasTexture(MaterialTexture::Metalness)            ? (1U << 5)  : 0;
                properties[index].flags                 |= material->HasTexture(MaterialTexture::AlphaMask)            ? (1U << 6)  : 0;
                properties[index].flags                 |= material->HasTexture(MaterialTexture::Emission)             ? (1U << 7)  : 0;
                properties[index].flags                 |= material->HasTexture(MaterialTexture::Occlusion)            ? (1U << 8)  : 0;
                properties[index].flags                 |= material->GetProperty(MaterialProperty::TextureSlopeBased)  ? (1U << 9)  : 0;
                properties[index].flags                 |= material->GetProperty(MaterialProperty::VertexAnimateWind)  ? (1U << 10) : 0;
                properties[index].flags                 |= material->GetProperty(MaterialProperty::VertexAnimateWater) ? (1U << 11) : 0;
                properties[index].flags                 |= material->IsTessellated()                                   ? (1U << 12) : 0;
                // when changing the bit flags, ensure that you also update the Surface struct in common_structs.hlsl, so that it reads those flags as expected
            }

            // textures
            {

                for (uint32_t type = 0; type < material_texture_type_count; type++)
                {
                    for (uint32_t variation = 0; variation < material_texture_slots_per_type; variation++)
                    {
                        uint32_t texture_index                   = type * material_texture_slots_per_type + variation;
                        MaterialTexture textureType              = static_cast<MaterialTexture>(texture_index);
                        bindless_textures[index + texture_index] = material->GetTexture(static_cast<MaterialTexture>(texture_index));
                    }
                }

            }

            material->SetIndex(index);
            index += static_cast<uint32_t>(MaterialTexture::Max);
        };

        auto update_entities = [update_material](vector<shared_ptr<Entity>>& entities)
        {
            for (shared_ptr<Entity> entity : entities)
            {
                if (entity)
                {
                    if (shared_ptr<Renderable> renderable = entity->GetComponent<Renderable>())
                    {
                        if (Material* material = renderable->GetMaterial())
                        {
                            update_material(material);
                        }
                    }
                }
            }
        };

        if (ProgressTracker::IsLoading())
            return;

        lock_guard lock(m_mutex_renderables);

        // cpu
        {
            // clear
            properties.fill(Sb_Material{});
            bindless_textures.fill(nullptr);
            unique_material_ids.clear();
            index = 0;
            update_entities(m_renderables[Renderer_Entity::Mesh]);
        }

        // gpu
        {
            // material properties
            Renderer::GetStructuredBuffer(Renderer_StructuredBuffer::Materials)->ResetOffset();
            uint32_t update_size = static_cast<uint32_t>(sizeof(Sb_Material)) * index;
            Renderer::GetStructuredBuffer(Renderer_StructuredBuffer::Materials)->Update(&properties[0], update_size);

            // material textures
            bindless_materials_dirty = true;
        }
    }

    void Renderer::BindlessUpdateLights()
    {
        static array<Sb_Light, rhi_max_array_size_lights> properties;

        if (ProgressTracker::IsLoading())
            return;

        lock_guard lock(m_mutex_renderables);
        uint32_t index = 0;

        // cpu
        {
            // clear
            properties.fill(Sb_Light{});

            // go through each light
            for (shared_ptr<Entity>& entity : m_renderables[Renderer_Entity::Light])
            {
                if (Light* light = entity->GetComponent<Light>().get())
                {
                    light->SetIndex(index);

                    // set light properties
                    if (RHI_Texture* texture = light->GetDepthTexture())
                    {
                        for (uint32_t i = 0; i < texture->GetArrayLength(); i++)
                        {
                            if (light->GetLightType() == LightType::Point)
                            {
                                // we do paraboloid projection in the vertex shader so we only want the view here
                                properties[index].view_projection[i] = light->GetViewMatrix(i);
                            }
                            else
                            { 
                                properties[index].view_projection[i] = light->GetViewMatrix(i) * light->GetProjectionMatrix(i);
                            }
                        }
                    }
                    properties[index].intensity  = light->GetIntensityWatt();
                    properties[index].range      = light->GetRange();
                    properties[index].angle      = light->GetAngle();
                    properties[index].color      = light->GetColor();
                    properties[index].position   = light->GetEntity()->GetPosition();
                    properties[index].direction  = light->GetEntity()->GetForward();
                    properties[index].flags      = 0;
                    properties[index].flags     |= light->GetLightType() == LightType::Directional  ? (1 << 0) : 0;
                    properties[index].flags     |= light->GetLightType() == LightType::Point        ? (1 << 1) : 0;
                    properties[index].flags     |= light->GetLightType() == LightType::Spot         ? (1 << 2) : 0;
                    properties[index].flags     |= light->IsFlagSet(LightFlags::Shadows)            ? (1 << 3) : 0;
                    properties[index].flags     |= light->IsFlagSet(LightFlags::ShadowsTransparent) ? (1 << 4) : 0;
                    properties[index].flags     |= (light->IsFlagSet(LightFlags::ShadowsScreenSpace) && GetOption<bool>(Renderer_Option::ScreenSpaceShadows)) ? (1 << 5) : 0;
                    properties[index].flags     |= (light->IsFlagSet(LightFlags::Volumetric) && GetOption<bool>(Renderer_Option::FogVolumetric)) ? (1 << 6) : 0;
                    // when changing the bit flags, ensure that you also update the Light struct in common_structs.hlsl, so that it reads those flags as expected

                    index++;
                }
            }
        }

        // cpu to gpu
        uint32_t update_size = static_cast<uint32_t>(sizeof(Sb_Light)) * index;
        RHI_StructuredBuffer* buffer = GetStructuredBuffer(Renderer_StructuredBuffer::Lights).get();
        buffer->ResetOffset();
        buffer->Update(&properties[0], update_size);
    }

    void Renderer::Screenshot(const string& file_path)
    {
        GetFrameTexture()->SaveAsImage(file_path);
    }
}
