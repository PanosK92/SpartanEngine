/*
Copyright(c) 2016-2023 Panos Karabelas

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

//= INCLUDES ===================================
#include "pch.h"
#include "Renderer.h"
#include "../Profiling/RenderDoc.h"
#include "../Core/Window.h"
#include "../Input/Input.h"
#include "../Display/Display.h"
#include "../RHI/RHI_Device.h"
#include "../RHI/RHI_SwapChain.h"
#include "../RHI/RHI_CommandPool.h"
#include "../RHI/RHI_ConstantBuffer.h"
#include "../RHI/RHI_Implementation.h"
#include "../RHI/RHI_FidelityFX.h"
#include "../RHI/RHI_StructuredBuffer.h"
#include "../World/Entity.h"
#include "../World/Components/Transform.h"
#include "../World/Components/Light.h"
#include "../World/Components/Camera.h"
#include "../World/Components/AudioSource.h"
#include "../World/Components/ReflectionProbe.h"
//==============================================

//= NAMESPACES ===============
using namespace std;
using namespace Spartan::Math;
//============================

namespace Spartan
{
    unordered_map<Renderer_Entity, vector<shared_ptr<Entity>>> Renderer::m_renderables;
    Cb_Frame Renderer::m_cb_frame_cpu;
    Pcb_Pass Renderer::m_cb_pass_cpu;
    Cb_Light Renderer::m_cb_light_cpu;
    Cb_Material Renderer::m_cb_material_cpu;
    shared_ptr<RHI_VertexBuffer> Renderer::m_vertex_buffer_lines;
    vector<RHI_Vertex_PosCol> Renderer::m_line_vertices;
    vector<float> Renderer::m_lines_duration;
    uint32_t Renderer::m_lines_index_depth_off;
    uint32_t Renderer::m_lines_index_depth_on;
    bool Renderer::m_brdf_specular_lut_rendered;
    RHI_CommandPool* Renderer::m_cmd_pool = nullptr;
    shared_ptr<Camera> Renderer::m_camera = nullptr;
    uint32_t Renderer::m_resource_index = 0;

    namespace
    {
        // resolution & viewport
        Math::Vector2 m_resolution_render = Math::Vector2::Zero;
        Math::Vector2 m_resolution_output = Math::Vector2::Zero;
        RHI_Viewport m_viewport           = RHI_Viewport(0, 0, 0, 0);

        // swapchain
        const uint8_t swap_chain_buffer_count = 2;
        shared_ptr<RHI_SwapChain> swap_chain;

        // mip generation
        mutex mutex_mip_generation;
        vector<RHI_Texture*> textures_mip_generation;

        // rhi resources
        RHI_CommandList* cmd_current = nullptr;

        // misc
        unordered_map<Renderer_Option, float> m_options;
        mutex mutex_entity_addition;
        vector<shared_ptr<Entity>> m_entities_to_add;
        uint64_t frame_num                       = 0;
        Math::Vector2 jitter_offset              = Math::Vector2::Zero;
        const uint32_t resolution_shadow_min     = 128;
        float near_plane                         = 0.0f;
        float far_plane                          = 1.0f;
        bool dirty_orthographic_projection       = true;

        void sort_renderables(Camera* camera, vector<shared_ptr<Entity>>* renderables, const bool are_transparent)
        {
            if (!camera || renderables->size() <= 2)
                return;

            auto comparison_op = [camera](shared_ptr<Entity> entity)
            {
                auto renderable = entity->GetComponent<Renderable>();
                if (!renderable)
                    return 0.0f;

                return (renderable->GetBoundingBox().GetCenter() - camera->GetTransform()->GetPosition()).LengthSquared();
            };

            // sort by depth
            sort(renderables->begin(), renderables->end(), [&comparison_op, &are_transparent](shared_ptr<Entity> a, shared_ptr<Entity> b)
            {
                if (are_transparent)
                {
                    return comparison_op(a) > comparison_op(b); // back-to-front for transparent
                }
                else
                {
                    return comparison_op(a) < comparison_op(b); // front-to-back for opaque
                }
            });
        }
    }

    void Renderer::Initialize()
    {
        m_brdf_specular_lut_rendered = false;

        Display::DetectDisplayModes();

        // rhi initialization
        {
            if (RHI_Context::renderdoc)
            {
                RenderDoc::OnPreDeviceCreation();
            }

            RHI_Device::Initialize();
        }

        // resolution
        {
            uint32_t width  = Window::GetWidth();
            uint32_t height = Window::GetHeight();

            // the resolution of the output frame (we can upscale to that linearly or with fsr 2)
            SetResolutionOutput(width, height, false);

            // the resolution of the actual rendering
            SetResolutionRender(width, height, false);

            // the resolution/size of the editor's viewport. This is overridden by the editor based on the actual viewport size
            SetViewport(static_cast<float>(width), static_cast<float>(height));

            // note #1: if the editor is active, it will set the render and viewport resolution to what the actual viewport is
            // note #2: settings can override the render and output resolution (if an xml file was loaded)
        }

        // swap chain
        swap_chain = make_shared<RHI_SwapChain>
        (
            Window::GetHandleSDL(),
            static_cast<uint32_t>(m_resolution_output.x),
            static_cast<uint32_t>(m_resolution_output.y),
            // present mode: for v-sync, we could mailbox for lower latency, but fifo is always supported, so we'll assume that
            GetOption<bool>(Renderer_Option::Vsync) ? RHI_Present_Mode::Fifo : RHI_Present_Mode::Immediate,
            swap_chain_buffer_count,
            "renderer"
        );

        // command pool
        m_cmd_pool = RHI_Device::CommandPoolAllocate("renderer", swap_chain->GetObjectId(), RHI_Queue_Type::Graphics);

        // fidelityfx suite
        RHI_FidelityFX::Initialize();

        // options
        m_options.clear();
        SetOption(Renderer_Option::Hdr,                           swap_chain->IsHdr() ? 1.0f : 0.0f);                    // hdr is enabled by default if the swapchain is hdr
        SetOption(Renderer_Option::Bloom,                         0.03f);                                                // non-zero values activate it and define the blend factor
        SetOption(Renderer_Option::MotionBlur,                    1.0f);                                                 
        SetOption(Renderer_Option::ScreenSpaceGlobalIllumination, 1.0f);                                                 
        SetOption(Renderer_Option::ScreenSpaceShadows,            static_cast<float>(Renderer_ScreenspaceShadow::Bend)); 
        SetOption(Renderer_Option::ScreenSpaceReflections,        1.0f);                                                 
        SetOption(Renderer_Option::Anisotropy,                    16.0f);                                                
        SetOption(Renderer_Option::ShadowResolution,              4096.0f);                                              
        SetOption(Renderer_Option::Tonemapping,                   static_cast<float>(Renderer_Tonemapping::Aces));       
        SetOption(Renderer_Option::Gamma,                         2.2f);                                                 
        SetOption(Renderer_Option::Exposure,                      1.0f);                                                 
        SetOption(Renderer_Option::Sharpness,                     1.0f);                                                 
        SetOption(Renderer_Option::Fog,                           20.0f);                                                 
        SetOption(Renderer_Option::Antialiasing,                  static_cast<float>(Renderer_Antialiasing::Taa));       // this is using fsr 2 for taa
        SetOption(Renderer_Option::Upsampling,                    static_cast<float>(Renderer_Upsampling::FSR2));
        SetOption(Renderer_Option::Vsync,                         0.0f);
        SetOption(Renderer_Option::Debanding,                     0.0f);
        SetOption(Renderer_Option::Debug_TransformHandle,         1.0f);
        SetOption(Renderer_Option::Debug_SelectionOutline,        1.0f);
        SetOption(Renderer_Option::Debug_Grid,                    1.0f);
        SetOption(Renderer_Option::Debug_ReflectionProbes,        1.0f);
        SetOption(Renderer_Option::Debug_Lights,                  1.0f);
        SetOption(Renderer_Option::Debug_Physics,                 0.0f);
        SetOption(Renderer_Option::Debug_PerformanceMetrics,      1.0f);

        // resources
        CreateConstantBuffers();
        CreateShaders();
        CreateDepthStencilStates();
        CreateRasterizerStates();
        CreateBlendStates();
        CreateRenderTextures(true, true, true, true);
        CreateFonts();
        CreateSamplers(false);
        CreateStructuredBuffers();
        CreateStandardTextures();
        CreateStandardMeshes();

        // events
        {
            // subscribe
            SP_SUBSCRIBE_TO_EVENT(EventType::WorldResolved,           SP_EVENT_HANDLER_VARIANT_STATIC(OnWorldResolved));
            SP_SUBSCRIBE_TO_EVENT(EventType::WorldClear,              SP_EVENT_HANDLER_STATIC(OnClear));
            SP_SUBSCRIBE_TO_EVENT(EventType::WindowFullScreenToggled, SP_EVENT_HANDLER_STATIC(OnFullScreenToggled));

            // fire
            SP_FIRE_EVENT(EventType::RendererOnInitialized);
        }
    }

    void Renderer::Shutdown()
    {
        SP_FIRE_EVENT(EventType::RendererOnShutdown);

        // manually invoke the deconstructors so that ParseDeletionQueue(), releases their rhi resources.
        {
            DestroyResources();

            m_entities_to_add.clear();
            m_renderables.clear();
            swap_chain            = nullptr;
            m_vertex_buffer_lines = nullptr;
        }

        RenderDoc::Shutdown();
        RHI_Device::QueueWaitAll();
        RHI_FidelityFX::Destroy();
        RHI_Device::DeletionQueueParse();
        RHI_Device::Destroy();
    }

    void Renderer::Tick()
    {
        // don't produce frames if the window is minimized
        if (Window::IsMinimised())
            return;

        if (frame_num == 1)
        {
            SP_FIRE_EVENT(EventType::RendererOnFirstFrameCompleted);
        }

        // delete any RHI resources that have accumulated
        if (RHI_Device::DeletionQueueNeedsToParse())
        {
            RHI_Device::QueueWaitAll();
            RHI_Device::DeletionQueueParse();
            SP_LOG_INFO("Parsed deletion queue");
        }

        // reset buffer offsets
        {
            m_resource_index++;

            if (m_resource_index == resources_frame_lifetime)
            {
                m_resource_index = 0;

                for (shared_ptr<RHI_ConstantBuffer> constant_buffer : GetConstantBuffers())
                {
                    constant_buffer->ResetOffset();
                }
                GetStructuredBuffer()->ResetOffset();
            }
        }

        RHI_Device::Tick(frame_num);

        // begin
        m_cmd_pool->Tick();
        cmd_current = m_cmd_pool->GetCurrentCommandList();
        cmd_current->Begin();

        OnFrameStart(cmd_current);

        Pass_Frame(cmd_current);

        // blit to back buffer when in full screen
        if (!Engine::IsFlagSet(EngineMode::Editor))
        {
            cmd_current->BeginMarker("copy_to_back_buffer");
            cmd_current->Blit(GetRenderTarget(Renderer_RenderTexture::frame_output).get(), swap_chain.get());
            cmd_current->EndMarker();
        }

        OnFrameEnd(cmd_current);

        // submit
        cmd_current->End();
        cmd_current->Submit();

        // track frame
        frame_num++;
    }

    void Renderer::PostTick()
    {
        if (!Engine::IsFlagSet(EngineMode::Editor))
        {
            Present();
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

        if (width > m_resolution_output.x || height > m_resolution_output.y)
        {
            SP_LOG_WARNING("Can't set %dx%d as it's larger then the output resolution %dx%d", width, height, m_resolution_output.x, m_resolution_output.y);
            return;
        }

        if (m_resolution_render.x == width && m_resolution_render.y == height)
            return;

        // set resolution
        m_resolution_render.x = static_cast<float>(width);
        m_resolution_render.y = static_cast<float>(height);

        if (recreate_resources)
        {
            // re-create render textures
            CreateRenderTextures(true, false, false, true);

            // re-create samplers
            CreateSamplers(true);
        }

        // register this resolution as a display mode so it shows up in the editor's render options (it won't happen if already registered)
        Display::RegisterDisplayMode(static_cast<uint32_t>(width), static_cast<uint32_t>(height), Display::GetRefreshRate(), Display::GetIndex());

        // log
        SP_LOG_INFO("Render resolution has been set to %dx%d", width, height);
    }

    const Vector2& Renderer::GetResolutionOutput()
    {
        return m_resolution_output;
    }

    void Renderer::SetResolutionOutput(uint32_t width, uint32_t height, bool recreate_resources /*= true*/)
    {
        // return if resolution is invalid
        if (!RHI_Device::IsValidResolution(width, height))
        {
            SP_LOG_WARNING("%dx%d is an invalid resolution", width, height);
            return;
        }

        // silently return if resolution is already set
        if (m_resolution_output.x == width && m_resolution_output.y == height)
            return;

        // set resolution
        m_resolution_output.x = static_cast<float>(width);
        m_resolution_output.y = static_cast<float>(height);

        if (recreate_resources)
        {
            // re-create render textures
            CreateRenderTextures(false, true, false, true);

            // re-create samplers
            CreateSamplers(true);
        }

        // log
        SP_LOG_INFO("Output resolution output has been set to %dx%d", width, height);
    }

    void Renderer::UpdateConstantBufferFrame(RHI_CommandList* cmd_list, const bool set /*= true*/)
    {
        // update struct
        {
            // matrices
            {
                if (m_camera)
                {
                    if (near_plane != m_camera->GetNearPlane() || far_plane != m_camera->GetFarPlane())
                    {
                        near_plane                    = m_camera->GetNearPlane();
                        far_plane                     = m_camera->GetFarPlane();
                        dirty_orthographic_projection = true;
                    }

                    m_cb_frame_cpu.view       = m_camera->GetViewMatrix();
                    m_cb_frame_cpu.projection = m_camera->GetProjectionMatrix();
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
            if (upsampling_mode == Renderer_Upsampling::FSR2 || GetOption<Renderer_Antialiasing>(Renderer_Option::Antialiasing) == Renderer_Antialiasing::Taa)
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
            if (m_camera)
            {
                m_cb_frame_cpu.view_projection_unjittered = m_cb_frame_cpu.view * m_camera->GetProjectionMatrix();
                m_cb_frame_cpu.camera_near                = m_camera->GetNearPlane();
                m_cb_frame_cpu.camera_far                 = m_camera->GetFarPlane();
                m_cb_frame_cpu.camera_position_previous   = m_cb_frame_cpu.camera_position;
                m_cb_frame_cpu.camera_position            = m_camera->GetTransform()->GetPosition();
                m_cb_frame_cpu.camera_direction           = m_camera->GetTransform()->GetForward();
            }
            m_cb_frame_cpu.resolution_output   = m_resolution_output;
            m_cb_frame_cpu.resolution_render   = m_resolution_render;
            m_cb_frame_cpu.taa_jitter_previous = m_cb_frame_cpu.taa_jitter_current;
            m_cb_frame_cpu.taa_jitter_current  = jitter_offset;
            m_cb_frame_cpu.time                = static_cast<float>(Timer::GetTimeSec());
            m_cb_frame_cpu.delta_time          = static_cast<float>(Timer::GetDeltaTimeSmoothedSec()); // removes stutters from motion related code
            m_cb_frame_cpu.frame               = static_cast<uint32_t>(frame_num);
            m_cb_frame_cpu.gamma               = GetOption<float>(Renderer_Option::Gamma);
            m_cb_frame_cpu.water_level         = 10.0f; // needs to be read from the terrain component

            // these must match what common_buffer.hlsl is reading
            m_cb_frame_cpu.set_bit(GetOption<bool>(Renderer_Option::ScreenSpaceReflections),        1 << 0);
            m_cb_frame_cpu.set_bit(GetOption<bool>(Renderer_Option::ScreenSpaceGlobalIllumination), 1 << 1);
            m_cb_frame_cpu.set_bit(GetOption<bool>(Renderer_Option::ScreenSpaceShadows),            1 << 2);
            m_cb_frame_cpu.set_bit(GetOption<bool>(Renderer_Option::Fog),                           1 << 3);
            m_cb_frame_cpu.set_bit(GetOption<bool>(Renderer_Option::FogVolumetric),                 1 << 4);
        }

        GetConstantBuffer(Renderer_ConstantBuffer::Frame)->Update(&m_cb_frame_cpu);

        // set by default as the offset has changed
        if (set)
        {
            cmd_list->SetConstantBuffer(Renderer_BindingsCb::frame, GetConstantBuffer(Renderer_ConstantBuffer::Frame));
        }
    }

    void Renderer::UpdateConstantBufferLight(RHI_CommandList* cmd_list, shared_ptr<Light> light)
    {
        if (RHI_Texture* texture = light->GetDepthTexture())
        {
            for (uint32_t i = 0; i < texture->GetArrayLength(); i++)
            {
                m_cb_light_cpu.view_projection[i] = light->GetViewMatrix(i) * light->GetProjectionMatrix(i);
            }
        }

        m_cb_light_cpu.intensity    = light->GetIntensityWatt(m_camera.get());
        m_cb_light_cpu.range        = light->GetRange();
        m_cb_light_cpu.angle        = light->GetAngle();
        m_cb_light_cpu.bias         = light->GetBias();
        m_cb_light_cpu.color        = light->GetColor();
        m_cb_light_cpu.normal_bias  = light->GetNormalBias();
        m_cb_light_cpu.position     = light->GetTransform()->GetPosition();
        m_cb_light_cpu.direction    = light->GetTransform()->GetForward();
        m_cb_light_cpu.options      = 0;
        m_cb_light_cpu.options     |= light->GetLightType() == LightType::Directional ? (1 << 0) : 0;
        m_cb_light_cpu.options     |= light->GetLightType() == LightType::Point       ? (1 << 1) : 0;
        m_cb_light_cpu.options     |= light->GetLightType() == LightType::Spot        ? (1 << 2) : 0;
        m_cb_light_cpu.options     |= light->GetShadowsEnabled()                      ? (1 << 3) : 0;
        m_cb_light_cpu.options     |= light->GetShadowsTransparentEnabled()           ? (1 << 4) : 0;
        m_cb_light_cpu.options     |= light->GetVolumetricEnabled()                   ? (1 << 5) : 0;

        GetConstantBuffer(Renderer_ConstantBuffer::Light)->Update(&m_cb_light_cpu);
        cmd_list->SetConstantBuffer(Renderer_BindingsCb::light, GetConstantBuffer(Renderer_ConstantBuffer::Light));
    }

    void Renderer::UpdateConstantBufferMaterial(RHI_CommandList* cmd_list, Material* material)
    {
        m_cb_material_cpu.properties = 0;

        // set
        m_cb_material_cpu.world_space_height    = material->GetProperty(MaterialProperty::WorldSpaceHeight);
        m_cb_material_cpu.color.x               = material->GetProperty(MaterialProperty::ColorR);
        m_cb_material_cpu.color.y               = material->GetProperty(MaterialProperty::ColorG);
        m_cb_material_cpu.color.z               = material->GetProperty(MaterialProperty::ColorB);
        m_cb_material_cpu.color.w               = material->GetProperty(MaterialProperty::ColorA);
        m_cb_material_cpu.tiling_uv.x           = material->GetProperty(MaterialProperty::TextureTilingX);
        m_cb_material_cpu.tiling_uv.y           = material->GetProperty(MaterialProperty::TextureTilingY);
        m_cb_material_cpu.offset_uv.x           = material->GetProperty(MaterialProperty::TextureOffsetX);
        m_cb_material_cpu.offset_uv.y           = material->GetProperty(MaterialProperty::TextureOffsetY);
        m_cb_material_cpu.roughness_mul         = material->GetProperty(MaterialProperty::MultiplierRoughness);
        m_cb_material_cpu.metallic_mul          = material->GetProperty(MaterialProperty::MultiplierMetalness);
        m_cb_material_cpu.normal_mul            = material->GetProperty(MaterialProperty::MultiplierNormal);
        m_cb_material_cpu.height_mul            = material->GetProperty(MaterialProperty::MultiplierHeight);
        m_cb_material_cpu.anisotropic           = material->GetProperty(MaterialProperty::Anisotropic);
        m_cb_material_cpu.anisitropic_rotation  = material->GetProperty(MaterialProperty::AnisotropicRotation);
        m_cb_material_cpu.clearcoat             = material->GetProperty(MaterialProperty::Clearcoat);
        m_cb_material_cpu.clearcoat_roughness   = material->GetProperty(MaterialProperty::Clearcoat_Roughness);
        m_cb_material_cpu.sheen                 = material->GetProperty(MaterialProperty::Sheen);
        m_cb_material_cpu.sheen_tint            = material->GetProperty(MaterialProperty::SheenTint);
        m_cb_material_cpu.properties           |= material->GetProperty(MaterialProperty::SingleTextureRoughnessMetalness) ? (1U << 0) : 0;
        m_cb_material_cpu.properties           |= material->HasTexture(MaterialTexture::Height)                            ? (1U << 1) : 0;
        m_cb_material_cpu.properties           |= material->HasTexture(MaterialTexture::Normal)                            ? (1U << 2) : 0;
        m_cb_material_cpu.properties           |= material->HasTexture(MaterialTexture::Color)                             ? (1U << 3) : 0;
        m_cb_material_cpu.properties           |= material->HasTexture(MaterialTexture::Roughness)                         ? (1U << 4) : 0;
        m_cb_material_cpu.properties           |= material->HasTexture(MaterialTexture::Metalness)                         ? (1U << 5) : 0;
        m_cb_material_cpu.properties           |= material->HasTexture(MaterialTexture::AlphaMask)                         ? (1U << 6) : 0;
        m_cb_material_cpu.properties           |= material->HasTexture(MaterialTexture::Emission)                          ? (1U << 7) : 0;
        m_cb_material_cpu.properties           |= material->HasTexture(MaterialTexture::Occlusion)                         ? (1U << 8) : 0;
        m_cb_material_cpu.properties           |= material->GetProperty(MaterialProperty::TextureSlopeBased)               ? (1U << 9) : 0;
        m_cb_material_cpu.properties           |= material->GetProperty(MaterialProperty::TextureAnimate)                  ? (1U << 10) : 0;
        m_cb_material_cpu.properties           |= material->GetProperty(MaterialProperty::VertexAnimateWind)               ? (1U << 11) : 0;
        m_cb_material_cpu.properties           |= material->GetProperty(MaterialProperty::VertexAnimateWater)              ? (1U << 12) : 0;

        GetConstantBuffer(Renderer_ConstantBuffer::Material)->Update(&m_cb_material_cpu);
        cmd_list->SetConstantBuffer(Renderer_BindingsCb::material, GetConstantBuffer(Renderer_ConstantBuffer::Material));
    }

    void Renderer::PushPassConstants(RHI_CommandList* cmd_list)
    {
        cmd_list->PushConstants(0, sizeof(Pcb_Pass), &m_cb_pass_cpu);
    }

	void Renderer::OnWorldResolved(sp_variant data)
    {
        // note: m_renderables is a vector of shared pointers.
        // this ensures that if any entities are deallocated by the world.
        // we'll still have some valid pointers until the are overridden by m_renderables_world.

        vector<shared_ptr<Entity>> entities = get<vector<shared_ptr<Entity>>>(data);

        lock_guard lock(mutex_entity_addition);
        m_entities_to_add.clear();

        for (shared_ptr<Entity> entity : entities)
        {
            SP_ASSERT_MSG(entity != nullptr, "Entity is null");

            if (entity->IsActiveRecursively())
            {
                m_entities_to_add.emplace_back(entity);
            }
        }
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

    void Renderer::OnFrameStart(RHI_CommandList* cmd_list)
    {
        // acquire renderables
        if (!m_entities_to_add.empty())
        {
            // clear previous state
            m_renderables.clear();
            m_camera = nullptr;

            for (shared_ptr<Entity> entity : m_entities_to_add)
            {
                if (shared_ptr<Renderable> renderable = entity->GetComponent<Renderable>())
                {
                    bool is_transparent = false;
                    bool is_visible     = true;

                    if (const Material* material = renderable->GetMaterial())
                    {
                        is_transparent = material->GetProperty(MaterialProperty::ColorA) < 1.0f;
                        is_visible     = material->GetProperty(MaterialProperty::ColorA) != 0.0f;
                    }

                    if (is_visible)
                    {
                        if (is_transparent)
                        {
                            m_renderables[renderable->HasInstancing() ? Renderer_Entity::GeometryTransparentInstanced : Renderer_Entity::GeometryTransparent].emplace_back(entity);
                        }
                        else
                        {
                            m_renderables[renderable->HasInstancing() ? Renderer_Entity::GeometryInstanced : Renderer_Entity::Geometry].emplace_back(entity);
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
                    m_camera = camera;
                }

                if (shared_ptr<ReflectionProbe> reflection_probe = entity->GetComponent<ReflectionProbe>())
                {
                    m_renderables[Renderer_Entity::ReflectionProbe].emplace_back(entity);
                }

                if (shared_ptr<AudioSource> audio_source = entity->GetComponent<AudioSource>())
                {
                    m_renderables[Renderer_Entity::AudioSource].emplace_back(entity);
                }
            }

            // sort them by distance
            sort_renderables(m_camera.get(), &m_renderables[Renderer_Entity::Geometry], false);
            sort_renderables(m_camera.get(), &m_renderables[Renderer_Entity::GeometryTransparent], true);

            m_entities_to_add.clear();
        }

        // generate mips
        {
            lock_guard lock(mutex_mip_generation);
            for (RHI_Texture* texture : textures_mip_generation)
            {
                Pass_GenerateMips(cmd_list, texture);
            }
            textures_mip_generation.clear();
        }

        Lines_OneFrameStart();
    }

    void Renderer::OnFrameEnd(RHI_CommandList* cmd_list)
    {
        Lines_OnFrameEnd();
    }

	void Renderer::DrawString(const string& text, const Vector2& position_screen_percentage)
	{
        GetFont()->AddText(text, position_screen_percentage);
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
        }

        // early exit if the value is already set
        if ((m_options.find(option) != m_options.end()) && m_options[option] == value)
            return;

        // reject changes (if needed)
        {
            if (option == Renderer_Option::Hdr)
            {
                if (value == 1.0f && !Display::GetHdr())
                {
                    SP_LOG_INFO("This display doesn't support HDR");
                    return;
                }
            }
        }

        // set new value
        m_options[option] = value;

        // handle cascading changes
        {
            // aAntialiasing
            if (option == Renderer_Option::Antialiasing)
            {
                bool taa_enabled = value == static_cast<float>(Renderer_Antialiasing::Taa) || value == static_cast<float>(Renderer_Antialiasing::TaaFxaa);
                bool fsr_enabled = GetOption<Renderer_Upsampling>(Renderer_Option::Upsampling) == Renderer_Upsampling::FSR2;

                if (taa_enabled)
                {
                    // implicitly enable FSR since it's doing TAA.
                    if (!fsr_enabled)
                    {
                        m_options[Renderer_Option::Upsampling] = static_cast<float>(Renderer_Upsampling::FSR2);
                        RHI_FidelityFX::FSR2_ResetHistory();
                        SP_LOG_INFO("Enabled FSR 2.0 since it's used for TAA.");
                    }
                }
                else
                {
                    // Implicitly disable FSR since it's doing TAA
                    if (fsr_enabled)
                    {
                        m_options[Renderer_Option::Upsampling] = static_cast<float>(Renderer_Upsampling::Linear);
                        SP_LOG_INFO("Disabed FSR 2.0 since it's used for TAA.");
                    }
                }
            }
            // Upsampling
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
                else if (value == static_cast<float>(Renderer_Upsampling::FSR2))
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
            // Shadow resolution
            else if (option == Renderer_Option::ShadowResolution)
            {
                const auto& light_entities = m_renderables[Renderer_Entity::Light];
                for (const auto& light_entity : light_entities)
                {
                    auto light = light_entity->GetComponent<Light>();
                    if (light->GetShadowsEnabled())
                    {
                        light->CreateShadowMap();
                    }
                }
            }
            else if (option == Renderer_Option::Hdr)
            {
                swap_chain->SetHdr(value == 1.0f);
            }
            else if (option == Renderer_Option::Vsync)
            {
                swap_chain->SetVsync(value == 1.0f);
            }
        }
    }

    unordered_map<Renderer_Option, float>& Renderer::GetOptions()
    {
        return m_options;
    }

    void Renderer::SetOptions(const std::unordered_map<Renderer_Option, float>& options)
    {
        m_options = options;
    }

    RHI_SwapChain* Renderer::GetSwapChain()
    {
        return swap_chain.get();
    }
    
    void Renderer::Present()
    {
        SP_ASSERT(swap_chain->GetLayout() == RHI_Image_Layout::Present_Source);

        if (Window::IsMinimised())
        {
            SP_LOG_WARNING("Ignoring call, don't call present if the window is minimized");
            return;
        }

        swap_chain->Present();

        SP_FIRE_EVENT(EventType::RendererPostPresent);
    }

    void Renderer::AddTextureForMipGeneration(RHI_Texture* texture)
    {
        lock_guard<mutex> guard(mutex_mip_generation);
        textures_mip_generation.push_back(texture);
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
        return GetRenderTarget(Renderer_RenderTexture::frame_output).get();
    }

    uint64_t Renderer::GetFrameNum()
    {
        return frame_num;
    }

    shared_ptr<Camera> Renderer::GetCamera()
    {
        return m_camera;
    }

    unordered_map<Renderer_Entity, vector<shared_ptr<Entity>>>& Renderer::GetEntities()
    {
        return m_renderables;
    }

	void Renderer::SetTexturesGfbuffer(RHI_CommandList* cmd_list)
    {
        cmd_list->SetTexture(Renderer_BindingsSrv::gbuffer_albedo,            GetRenderTarget(Renderer_RenderTexture::gbuffer_color));
        cmd_list->SetTexture(Renderer_BindingsSrv::gbuffer_normal,            GetRenderTarget(Renderer_RenderTexture::gbuffer_normal));
        cmd_list->SetTexture(Renderer_BindingsSrv::gbuffer_depth,             GetRenderTarget(Renderer_RenderTexture::gbuffer_depth));
        cmd_list->SetTexture(Renderer_BindingsSrv::gbuffer_material,          GetRenderTarget(Renderer_RenderTexture::gbuffer_material));
        cmd_list->SetTexture(Renderer_BindingsSrv::gbuffer_material_2,        GetRenderTarget(Renderer_RenderTexture::gbuffer_material_2));
        cmd_list->SetTexture(Renderer_BindingsSrv::gbuffer_velocity,          GetRenderTarget(Renderer_RenderTexture::gbuffer_velocity));
        cmd_list->SetTexture(Renderer_BindingsSrv::gbuffer_velocity_previous, GetRenderTarget(Renderer_RenderTexture::gbuffer_velocity_previous));
    }

    void Renderer::SetTexturesMaterial(RHI_CommandList* cmd_list, Material* material)
    {
        cmd_list->SetTexture(Renderer_BindingsSrv::material_albedo,    material->GetTexture(MaterialTexture::Color));
        cmd_list->SetTexture(Renderer_BindingsSrv::material_albedo2,   material->GetTexture(MaterialTexture::Color2));
        cmd_list->SetTexture(Renderer_BindingsSrv::material_roughness, material->GetTexture(MaterialTexture::Roughness));
        cmd_list->SetTexture(Renderer_BindingsSrv::material_metallic,  material->GetTexture(MaterialTexture::Metalness));
        cmd_list->SetTexture(Renderer_BindingsSrv::material_normal,    material->GetTexture(MaterialTexture::Normal));
        cmd_list->SetTexture(Renderer_BindingsSrv::material_normal2,   material->GetTexture(MaterialTexture::Normal2));
        cmd_list->SetTexture(Renderer_BindingsSrv::material_height,    material->GetTexture(MaterialTexture::Height));
        cmd_list->SetTexture(Renderer_BindingsSrv::material_occlusion, material->GetTexture(MaterialTexture::Occlusion));
        cmd_list->SetTexture(Renderer_BindingsSrv::material_emission,  material->GetTexture(MaterialTexture::Emission));
        cmd_list->SetTexture(Renderer_BindingsSrv::material_mask,      material->GetTexture(MaterialTexture::AlphaMask));
    }

    void Renderer::Screenshot(const string& file_path)
    {
        GetFrameTexture()->SaveAsImage(file_path);
    }
}
