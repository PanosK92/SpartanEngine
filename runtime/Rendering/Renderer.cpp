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
#include "../World/Entity.h"                    
#include "../World/Components/Transform.h"      
#include "../World/Components/Renderable.h"     
#include "../World/Components/Camera.h"         
#include "../World/Components/Light.h"          
#include "../World/Components/ReflectionProbe.h"
#include "../RHI/RHI_ConstantBuffer.h"          
#include "../RHI/RHI_StructuredBuffer.h"        
#include "../RHI/RHI_Implementation.h"          
#include "../RHI/RHI_CommandPool.h"
#include "../RHI/RHI_FSR2.h"
#include "../RHI/RHI_RenderDoc.h"
#include "../Core/Window.h"                     
#include "../Input/Input.h"                     
#include "../World/Components/Environment.h"    
#include "Material.h"
#include "Renderer_ConstantBuffers.h"
#include "Font/Font.h"
#include "Grid.h"
#include "../RHI/RHI_SwapChain.h"
//==============================================

//= NAMESPACES ===============
using namespace std;
using namespace Spartan::Math;
//============================

// Macro to work around the verboseness of some C++ concepts.
#define render_target(enum_rt) GetRenderTargets()[static_cast<uint8_t>(enum_rt)]

namespace Spartan
{
    //= BUFFERS =============================================
    extern shared_ptr<RHI_StructuredBuffer> m_sb_spd_counter;
    
    extern Cb_Frame m_cb_frame_cpu;
    extern shared_ptr<RHI_ConstantBuffer> m_cb_frame_gpu;
    
    extern Cb_Uber m_cb_uber_cpu;
    extern shared_ptr<RHI_ConstantBuffer> m_cb_uber_gpu;
    
    extern Cb_Light m_cb_light_cpu;
    extern shared_ptr<RHI_ConstantBuffer> m_cb_light_gpu;
    
    extern Cb_Material m_cb_material_cpu;
    extern shared_ptr<RHI_ConstantBuffer> m_cb_material_gpu;
    //=======================================================

    extern shared_ptr<RHI_VertexBuffer> m_quad_vertex_buffer;
    extern shared_ptr<RHI_IndexBuffer>  m_quad_index_buffer;
    extern shared_ptr<RHI_VertexBuffer> m_sphere_vertex_buffer;
    extern shared_ptr<RHI_IndexBuffer>  m_sphere_index_buffer;
    extern shared_ptr<RHI_VertexBuffer> m_vertex_buffer_lines;

    // Standard textures
    extern shared_ptr<RHI_Texture> m_tex_default_noise_normal;
    extern shared_ptr<RHI_Texture> m_tex_default_noise_blue;
    extern shared_ptr<RHI_Texture> m_tex_default_white;
    extern shared_ptr<RHI_Texture> m_tex_default_black;
    extern shared_ptr<RHI_Texture> m_tex_default_transparent;
    extern shared_ptr<RHI_Texture> m_tex_gizmo_light_directional;
    extern shared_ptr<RHI_Texture> m_tex_gizmo_light_point;
    extern shared_ptr<RHI_Texture> m_tex_gizmo_light_spot;
    
    // Misc
    extern array<shared_ptr<RHI_Texture>, 26> m_render_targets;
    extern array<shared_ptr<RHI_Shader>, 47> m_shaders;
    extern bool m_ffx_fsr2_reset;
    extern unique_ptr<Font> m_font;
    extern unique_ptr<Grid> m_world_grid;

    // Resolution & Viewport
    Math::Vector2 m_resolution_render = Math::Vector2::Zero;
    Math::Vector2 m_resolution_output = Math::Vector2::Zero;
    RHI_Viewport m_viewport           = RHI_Viewport(0, 0, 0, 0);
    
    // Environment texture
    shared_ptr<RHI_Texture> m_environment_texture;
    bool m_environment_texture_dirty = false;
    
    // Options
    array<float, 34> m_options;
    
    // Misc
    Math::Vector2 m_jitter_offset = Math::Vector2::Zero;
    float m_near_plane            = 0.0f;
    float m_far_plane             = 1.0f;
    uint64_t m_frame_num          = 0;
    bool m_is_odd_frame           = false;
    array<Material*, m_max_material_instances> m_material_instances;
    
    // Constants
    const uint32_t m_resolution_shadow_min = 128;
    
    // Resource management
    vector<weak_ptr<RHI_Texture>> m_textures_mip_generation;
    
    // States
    atomic<bool> m_is_rendering_allowed  = true;
    atomic<bool> m_flush_requested       = false;
    bool m_dirty_orthographic_projection = true;
    
    // RHI Core
    RHI_CommandPool* m_cmd_pool    = nullptr;
    RHI_CommandList* m_cmd_current = nullptr;
    
    // Swapchain
    const uint8_t m_swap_chain_buffer_count = 2;
    shared_ptr<RHI_SwapChain> m_swap_chain;
    
    // Entities
    vector<shared_ptr<Entity>> m_renderables_pending;
    bool m_add_new_entities = false;
    unordered_map<RendererEntityType, vector<shared_ptr<Entity>>> m_renderables;
    shared_ptr<Camera> m_camera;
    Environment* m_environment = nullptr;
    
    // Sync objects
    thread::id m_render_thread_id;
    mutex m_mutex_entity_addition;
    mutex m_mutex_mip_generation;
    mutex m_mutex_environment_texture;

    void Renderer::Initialize()
    {
        m_render_thread_id = this_thread::get_id();
        Display::DetectDisplayModes();

        // RHI Initialization
        {
            RHI_Context::Initialize();

            if (RHI_Context::renderdoc)
            {
                RHI_RenderDoc::OnPreDeviceCreation();
            }

            RHI_Device::Initialize();
        }

        // Create swap chain
        m_swap_chain = make_shared<RHI_SwapChain>
        (
            Window::GetHandleSDL(),
            Window::GetWidth(),
            Window::GetHeight(),
            Display::GetHdr(),
            // Present mode: For v-sync, we could Mailbox for lower latency, but Fifo is always supported, so we'll assume that
            GetOption<bool>(RendererOption::Vsync) ? RHI_Present_Mode::Fifo : RHI_Present_Mode::Immediate,
            m_swap_chain_buffer_count,
            "renderer"
        );

        // Create command pool
        m_cmd_pool = RHI_Device::AllocateCommandPool("renderer", m_swap_chain->GetObjectId());
        m_cmd_pool->AllocateCommandLists(RHI_Queue_Type::Graphics, 2, 2);

        // Adjust render option to reflect whether the swapchain is HDR or not
        SetOption(RendererOption::Hdr, m_swap_chain->IsHdr());

        // Set the output and viewport resolution to the display resolution.
        // If the editor is running, it will set the viewport resolution to whatever the viewport is.
        SetViewport(static_cast<float>(Window::GetWidth()), static_cast<float>(Window::GetHeight()));
        SetResolutionRender(Display::GetWidth(), Display::GetHeight(), false);
        SetResolutionOutput(Display::GetWidth(), Display::GetHeight(), false);

        // Default options
        m_options.fill(0.0f);
        SetOption(RendererOption::Bloom,                  0.2f); // Non-zero values activate it and define the blend factor.
        SetOption(RendererOption::MotionBlur,             1.0f);
        SetOption(RendererOption::Ssao,                   1.0f);
        SetOption(RendererOption::Ssao_Gi,                1.0f);
        SetOption(RendererOption::ScreenSpaceShadows,     1.0f);
        SetOption(RendererOption::ScreenSpaceReflections, 1.0f);
        SetOption(RendererOption::Anisotropy,             16.0f);
        SetOption(RendererOption::ShadowResolution,       2048.0f);
        SetOption(RendererOption::Tonemapping,            static_cast<float>(TonemappingMode::Disabled));
        SetOption(RendererOption::Gamma,                  2.2f);
        SetOption(RendererOption::Exposure,               1.0f);
        SetOption(RendererOption::PaperWhite,             150.0f); // nits
        SetOption(RendererOption::Sharpness,              0.5f);
        SetOption(RendererOption::Fog,                    0.0f);
        SetOption(RendererOption::Antialiasing,           static_cast<float>(AntialiasingMode::TaaFxaa)); // This is using FSR 2 for TAA
        SetOption(RendererOption::Upsampling,             static_cast<float>(UpsamplingMode::FSR2));
        // Debug
        SetOption(RendererOption::Debug_TransformHandle,    1.0f);
        SetOption(RendererOption::Debug_SelectionOutline,   1.0f);
        SetOption(RendererOption::Debug_Grid,               1.0f);
        SetOption(RendererOption::Debug_ReflectionProbes,   1.0f);
        SetOption(RendererOption::Debug_Lights,             1.0f);
        SetOption(RendererOption::Debug_Physics,            0.0f);
        SetOption(RendererOption::Debug_PerformanceMetrics, 1.0f);
        SetOption(RendererOption::Vsync,                    0.0f);
        //SetOption(RendererOption::DepthOfField,        1.0f); // This is depth of field from ALDI, so until I improve it, it should be disabled by default.
        //SetOption(RendererOption::Render_DepthPrepass, 1.0f); // Depth-pre-pass is not always faster, so by default, it's disabled.
        //SetOption(RendererOption::Debanding,           1.0f); // Disable debanding as we shouldn't be seeing banding to begin with.
        //SetOption(RendererOption::VolumetricFog,       1.0f); // Disable by default because it's not that great, I need to do it with a voxelised approach.

        // Create all the resources
        CreateConstantBuffers();
        CreateShaders();
        CreateDepthStencilStates();
        CreateRasterizerStates();
        CreateBlendStates();
        CreateRenderTextures(true, true, true, true);
        CreateFonts();
        CreateMeshes();
        CreateSamplers(false);
        CreateStructuredBuffers();
        CreateTextures();

        // Subscribe to events
        SP_SUBSCRIBE_TO_EVENT(EventType::WorldResolved,             SP_EVENT_HANDLER_VARIANT_STATIC(OnAddRenderables));
        SP_SUBSCRIBE_TO_EVENT(EventType::WorldClear,                SP_EVENT_HANDLER_STATIC(OnClear));
        SP_SUBSCRIBE_TO_EVENT(EventType::WindowOnFullScreenToggled, SP_EVENT_HANDLER_STATIC(OnFullScreenToggled));

        // Fire event
        SP_FIRE_EVENT(EventType::RendererOnInitialized);
    }

    void Renderer::Shutdown()
    {
        // Fire event
        SP_FIRE_EVENT(EventType::RendererOnShutdown);

        // Manually invoke the deconstructors so that ParseDeletionQueue(), releases their RHI resources.
        m_renderables_pending.clear();
        m_renderables.clear();
        m_render_targets.fill(nullptr);
        m_shaders.fill(nullptr);
        m_textures_mip_generation.clear();
        m_world_grid.reset();
        m_font.reset();
        m_swap_chain                  = nullptr;
        m_quad_vertex_buffer          = nullptr;
        m_quad_index_buffer           = nullptr;
        m_sphere_vertex_buffer        = nullptr;
        m_sphere_index_buffer         = nullptr;
        m_vertex_buffer_lines         = nullptr;
        m_sb_spd_counter              = nullptr;
        m_cb_frame_gpu                = nullptr;
        m_cb_uber_gpu                 = nullptr;
        m_cb_light_gpu                = nullptr;
        m_cb_material_gpu             = nullptr;
        m_environment_texture         = nullptr;
        m_tex_default_noise_normal    = nullptr;
        m_tex_default_noise_blue      = nullptr;
        m_tex_default_white           = nullptr;
        m_tex_default_black           = nullptr;
        m_tex_default_transparent     = nullptr;
        m_tex_gizmo_light_directional = nullptr;
        m_tex_gizmo_light_point       = nullptr;
        m_tex_gizmo_light_spot        = nullptr;

        // Delete all remaining RHI resources
        RHI_Device::ParseDeletionQueue();

        Log::SetLogToFile(true); // console doesn't render anymore, log to file

        RHI_RenderDoc::Shutdown();
        RHI_FSR2::Destroy();
        RHI_Device::Destroy();
    }

    void Renderer::Tick()
    {
        // Don't bother producing frames if the window is minimized
        if (Window::IsMinimised())
            return;

        // After the first frame has completed, we know the renderer is working.
        // We stop logging to a file and we start logging to the on-screen console.
        if (m_frame_num == 1)
        {
            Log::SetLogToFile(false);
            SP_FIRE_EVENT(EventType::RendererOnFirstFrameCompleted);
        }

        // Happens when core resources are created/destroyed
        if (m_flush_requested)
        {
            Flush();
        }

        // Resize swapchain to window size (if needed)
        {
            // Passing zero dimensions will cause the swapchain to not present at all
            uint32_t width  = Window::GetWidth();
            uint32_t height = Window::GetHeight();

            if (m_swap_chain->GetWidth() != width || m_swap_chain->GetHeight() != height)
            {
                if (m_swap_chain->Resize(width, height))
                {
                    SP_LOG_INFO("Swapchain resolution has been set to %dx%d", width, height);
                }
            }
        }

        if (!m_is_rendering_allowed)
            return;

        // Tick command pool
        bool reset = m_cmd_pool->Step() || (RHI_Context::api_type == RHI_Api_Type::D3d11);

        // Begin
        m_cmd_current = m_cmd_pool->GetCurrentCommandList();
        m_cmd_current->Begin();

        if (reset)
        {
            // Reset dynamic buffer indices
            m_cb_uber_gpu->ResetOffset();
            m_cb_frame_gpu->ResetOffset();
            m_cb_light_gpu->ResetOffset();
            m_cb_material_gpu->ResetOffset();
            m_sb_spd_counter->ResetOffset();

            // Perform operations which might modify, create or destroy resources
            OnResourceSafe(m_cmd_current);
        }

        // Update frame buffer
        {
            // Matrices
            {
                if (m_camera)
                {
                    if (m_near_plane != m_camera->GetNearPlane() || m_far_plane != m_camera->GetFarPlane())
                    {
                        m_near_plane                    = m_camera->GetNearPlane();
                        m_far_plane                     = m_camera->GetFarPlane();
                        m_dirty_orthographic_projection = true;
                    }

                    m_cb_frame_cpu.view                = m_camera->GetViewMatrix();
                    m_cb_frame_cpu.projection          = m_camera->GetProjectionMatrix();
                    m_cb_frame_cpu.projection_inverted = Matrix::Invert(m_cb_frame_cpu.projection);
                }

                if (m_dirty_orthographic_projection)
                { 
                    // Near clip does not affect depth accuracy in orthographic projection, so set it to 0 to avoid problems which can result an infinitely small [3,2] after the multiplication below.
                    m_cb_frame_cpu.projection_ortho      = Matrix::CreateOrthographicLH(m_viewport.width, m_viewport.height, 0.0f, m_far_plane);
                    m_cb_frame_cpu.view_projection_ortho = Matrix::CreateLookAtLH(Vector3(0, 0, -m_near_plane), Vector3::Forward, Vector3::Up) * m_cb_frame_cpu.projection_ortho;
                    m_dirty_orthographic_projection      = false;
                }
            }

            // Generate jitter sample in case FSR (which also does TAA) is enabled. D3D11 only receives FXAA so it's ignored at this point.
            UpsamplingMode upsampling_mode = GetOption<UpsamplingMode>(RendererOption::Upsampling);
            if ((upsampling_mode == UpsamplingMode::FSR2 || GetOption<AntialiasingMode>(RendererOption::Antialiasing) == AntialiasingMode::Taa) && RHI_Context::api_type != RHI_Api_Type::D3d11)
            {
                RHI_FSR2::GenerateJitterSample(&m_jitter_offset.x, &m_jitter_offset.y);
                m_jitter_offset.x          = (m_jitter_offset.x / m_resolution_render.x);
                m_jitter_offset.y          = (m_jitter_offset.y / m_resolution_render.y);
                m_cb_frame_cpu.projection *= Matrix::CreateTranslation(Vector3(m_jitter_offset.x, m_jitter_offset.y, 0.0f));
            }
            else
            {
                m_jitter_offset = Vector2::Zero;
            }
            
            // Update the remaining of the frame buffer
            m_cb_frame_cpu.view_projection_previous = m_cb_frame_cpu.view_projection;
            m_cb_frame_cpu.view_projection          = m_cb_frame_cpu.view * m_cb_frame_cpu.projection;
            m_cb_frame_cpu.view_projection_inv      = Matrix::Invert(m_cb_frame_cpu.view_projection);
            if (m_camera)
            {
                m_cb_frame_cpu.view_projection_unjittered = m_cb_frame_cpu.view * m_camera->GetProjectionMatrix();
                m_cb_frame_cpu.camera_aperture            = m_camera->GetAperture();
                m_cb_frame_cpu.camera_shutter_speed       = m_camera->GetShutterSpeed();
                m_cb_frame_cpu.camera_iso                 = m_camera->GetIso();
                m_cb_frame_cpu.camera_near                = m_camera->GetNearPlane();
                m_cb_frame_cpu.camera_far                 = m_camera->GetFarPlane();
                m_cb_frame_cpu.camera_position            = m_camera->GetTransform()->GetPosition();
                m_cb_frame_cpu.camera_direction           = m_camera->GetTransform()->GetForward();
            }
            m_cb_frame_cpu.resolution_output      = m_resolution_output;
            m_cb_frame_cpu.resolution_render      = m_resolution_render;
            m_cb_frame_cpu.taa_jitter_previous    = m_cb_frame_cpu.taa_jitter_current;
            m_cb_frame_cpu.taa_jitter_current     = m_jitter_offset;
            m_cb_frame_cpu.delta_time             = static_cast<float>(Timer::GetDeltaTimeSmoothedSec());
            m_cb_frame_cpu.time                   = static_cast<float>(Timer::GetTimeSec());
            m_cb_frame_cpu.bloom_intensity        = GetOption<float>(RendererOption::Bloom);
            m_cb_frame_cpu.sharpness              = GetOption<float>(RendererOption::Sharpness);
            m_cb_frame_cpu.fog                    = GetOption<float>(RendererOption::Fog);
            m_cb_frame_cpu.tonemapping            = GetOption<float>(RendererOption::Tonemapping);
            m_cb_frame_cpu.gamma                  = GetOption<float>(RendererOption::Gamma);
            m_cb_frame_cpu.exposure               = GetOption<float>(RendererOption::Exposure);
            m_cb_frame_cpu.luminance_min          = Display::GetLuminanceMin();
            m_cb_frame_cpu.luminance_max          = Display::GetLuminanceMax();
            m_cb_frame_cpu.paper_white            = Renderer::GetOption<bool>(RendererOption::Hdr) ? GetOption<float>(RendererOption::PaperWhite) : 1.0f;
            m_cb_frame_cpu.shadow_resolution      = GetOption<float>(RendererOption::ShadowResolution);
            m_cb_frame_cpu.frame                  = static_cast<uint32_t>(m_frame_num);
            m_cb_frame_cpu.frame_mip_count        = render_target(RendererTexture::frame_render)->GetMipCount();
            m_cb_frame_cpu.ssr_mip_count          = render_target(RendererTexture::ssr)->GetMipCount();
            m_cb_frame_cpu.resolution_environment = Vector2(GetEnvironmentTexture()->GetWidth(), GetEnvironmentTexture()->GetHeight());

            // These must match what Common_Buffer.hlsl is reading
            m_cb_frame_cpu.set_bit(GetOption<bool>(RendererOption::ScreenSpaceReflections), 1 << 0);
            m_cb_frame_cpu.set_bit(GetOption<bool>(RendererOption::Ssao),                   1 << 1);
            m_cb_frame_cpu.set_bit(GetOption<bool>(RendererOption::VolumetricFog),          1 << 2);
            m_cb_frame_cpu.set_bit(GetOption<bool>(RendererOption::ScreenSpaceShadows),     1 << 3);
            m_cb_frame_cpu.set_bit(GetOption<bool>(RendererOption::Ssao_Gi),                1 << 4);
        }

        Lines_PreMain();
        Pass_Main(m_cmd_current);
        Lines_PostMain();

        if (Window::IsFullScreen())
        {
            Pass_CopyToBackbuffer();
        }

        // Submit
        m_cmd_current->End();
        m_cmd_current->Submit();

        // Update frame tracking
        m_frame_num++;
        m_is_odd_frame = (m_frame_num % 2) == 1;
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
            m_viewport.width                = width;
            m_viewport.height               = height;
            m_dirty_orthographic_projection = true;
        }
    }

    const Vector2& Renderer::GetResolutionRender()
    {
        return m_resolution_render;
    }

    void Renderer::SetResolutionRender(uint32_t width, uint32_t height, bool recreate_resources /*= true*/)
    {
        // Return if resolution is invalid
        if (!RHI_Device::IsValidResolution(width, height))
        {
            SP_LOG_WARNING("%dx%d is an invalid resolution", width, height);
            return;
        }

        // Silently return if resolution is already set
        if (m_resolution_render.x == width && m_resolution_render.y == height)
            return;

        // Set resolution
        m_resolution_render.x = static_cast<float>(width);
        m_resolution_render.y = static_cast<float>(height);

        if (recreate_resources)
        {
            // Re-create render textures
            CreateRenderTextures(true, false, false, true);

            // Re-create samplers
            CreateSamplers(true);
        }

        // Log
        SP_LOG_INFO("Render resolution has been set to %dx%d", width, height);
    }

    const Vector2& Renderer::GetResolutionOutput()
    {
        return m_resolution_output;
    }

    void Renderer::SetResolutionOutput(uint32_t width, uint32_t height, bool recreate_resources /*= true*/)
    {
        // Return if resolution is invalid
        if (!RHI_Device::IsValidResolution(width, height))
        {
            SP_LOG_WARNING("%dx%d is an invalid resolution", width, height);
            return;
        }

        // Silently return if resolution is already set
        if (m_resolution_output.x == width && m_resolution_output.y == height)
            return;

        // Set resolution
        m_resolution_output.x = static_cast<float>(width);
        m_resolution_output.y = static_cast<float>(height);

        if (recreate_resources)
        {
            // Re-create render textures
            CreateRenderTextures(false, true, false, true);

            // Re-create samplers
            CreateSamplers(true);
        }

        // Log
        SP_LOG_INFO("Output resolution output has been set to %dx%d", width, height);
    }

    void Renderer::Update_Cb_Frame(RHI_CommandList* cmd_list)
    {
        // Update directional light intensity, just grab the first one
        for (const auto& entity : m_renderables[RendererEntityType::light])
        {
            if (Light* light = entity->GetComponent<Light>())
            {
                if (light->GetLightType() == LightType::Directional)
                {
                    m_cb_frame_cpu.directional_light_intensity = light->GetIntensityForShader(m_camera.get());
                }
            }
        }

        m_cb_frame_gpu->Update(&m_cb_frame_cpu);

        // Bind because the offset just changed
        cmd_list->SetConstantBuffer(RendererBindingsCb::frame, RHI_Shader_Vertex | RHI_Shader_Pixel | RHI_Shader_Compute, m_cb_frame_gpu);
    }

    void Renderer::Update_Cb_Uber(RHI_CommandList* cmd_list)
    {
        m_cb_uber_gpu->Update(&m_cb_uber_cpu);

        // Bind because the offset just changed
        cmd_list->SetConstantBuffer(RendererBindingsCb::uber, RHI_Shader_Vertex | RHI_Shader_Pixel | RHI_Shader_Compute, m_cb_uber_gpu);
    }

    void Renderer::Update_Cb_Light(RHI_CommandList* cmd_list, const Light* light, const RHI_Shader_Type scope)
    {
        for (uint32_t i = 0; i < light->GetShadowArraySize(); i++)
        {
            m_cb_light_cpu.view_projection[i] = light->GetViewMatrix(i) * light->GetProjectionMatrix(i);
        }

        m_cb_light_cpu.intensity_range_angle_bias = Vector4
        (
            light->GetIntensityForShader(m_camera.get()),
            light->GetRange(), light->GetAngle(),
            light->GetBias()
        );

        m_cb_light_cpu.color                      = light->GetColor();
        m_cb_light_cpu.normal_bias                = light->GetNormalBias();
        m_cb_light_cpu.position                   = light->GetTransform()->GetPosition();
        m_cb_light_cpu.direction                  = light->GetTransform()->GetForward();
        m_cb_light_cpu.options                    = 0;
        m_cb_light_cpu.options                    |= light->GetLightType() == LightType::Directional ? (1 << 0) : 0;
        m_cb_light_cpu.options                    |= light->GetLightType() == LightType::Point       ? (1 << 1) : 0;
        m_cb_light_cpu.options                    |= light->GetLightType() == LightType::Spot        ? (1 << 2) : 0;
        m_cb_light_cpu.options                    |= light->GetShadowsEnabled()                      ? (1 << 3) : 0;
        m_cb_light_cpu.options                    |= light->GetShadowsTransparentEnabled()           ? (1 << 4) : 0;
        m_cb_light_cpu.options                    |= light->GetShadowsScreenSpaceEnabled()           ? (1 << 5) : 0;
        m_cb_light_cpu.options                    |= light->GetVolumetricEnabled()                   ? (1 << 6) : 0;

        m_cb_light_gpu->Update(&m_cb_light_cpu);

        // Bind because the offset just changed
        cmd_list->SetConstantBuffer(RendererBindingsCb::light, scope, m_cb_light_gpu);
    }

    void Renderer::Update_Cb_Material(RHI_CommandList* cmd_list)
    {
        // Update
        for (uint32_t i = 0; i < m_max_material_instances; i++)
        {
            Material* material = m_material_instances[i];
            if (!material)
                continue;

            m_cb_material_cpu.mat_clearcoat_clearcoatRough_anis_anisRot[i].x = material->GetProperty(MaterialProperty::Clearcoat);
            m_cb_material_cpu.mat_clearcoat_clearcoatRough_anis_anisRot[i].y = material->GetProperty(MaterialProperty::Clearcoat_Roughness);
            m_cb_material_cpu.mat_clearcoat_clearcoatRough_anis_anisRot[i].z = material->GetProperty(MaterialProperty::Anisotropic);
            m_cb_material_cpu.mat_clearcoat_clearcoatRough_anis_anisRot[i].w = material->GetProperty(MaterialProperty::AnisotropicRotation);
            m_cb_material_cpu.mat_sheen_sheenTint_pad[i].x                   = material->GetProperty(MaterialProperty::Sheen);
            m_cb_material_cpu.mat_sheen_sheenTint_pad[i].y                   = material->GetProperty(MaterialProperty::SheenTint);
        }

        m_cb_material_gpu->Update(&m_cb_material_cpu);

        // Bind because the offset just changed
        cmd_list->SetConstantBuffer(RendererBindingsCb::material, RHI_Shader_Pixel, m_cb_material_gpu);
    }

    void Renderer::OnAddRenderables(const Variant& renderables)
    {
        // note: m_renderables is a vector of shared pointers.
        // this ensures that if any entities are deallocated by the world.
        // we'll still have some valid pointers until the are overridden by m_renderables_world.

        lock_guard lock(m_mutex_entity_addition);

        m_renderables_pending.clear();

        vector<shared_ptr<Entity>> entities = renderables.Get<vector<shared_ptr<Entity>>>();
        for (shared_ptr<Entity> entity : entities)
        {
            SP_ASSERT_MSG(entity != nullptr, "Entity is null");

            if (entity->IsActiveRecursively())
            {
                m_renderables_pending.emplace_back(entity);
            }
        }

        m_add_new_entities = true;
    }

    void Renderer::OnClear()
    {
        // Flush to remove references to entity resources that will be deallocated
        Flush();
        m_renderables.clear();
    }

    void Renderer::OnFullScreenToggled()
    {
        static float width_previous  = 0;
        static float height_previous = 0;

        if (Window::IsFullScreen())
        {
            width_previous  = m_viewport.width;
            height_previous = m_viewport.height;
            SetViewport(static_cast<float>(Window::GetWidth()), static_cast<float>(Window::GetHeight()));
        }
        else
        {
            SetViewport(width_previous, height_previous);
        }

        Input::SetMouseCursorVisible(!Window::IsFullScreen());
    }

    void Renderer::OnResourceSafe(RHI_CommandList* cmd_list)
    {
        // Acquire renderables
        if (m_add_new_entities)
        {
            // Clear previous state
            m_renderables.clear();
            m_camera = nullptr;

            for (shared_ptr<Entity> entity : m_renderables_pending)
            {
                if (Renderable* renderable = entity->GetComponent<Renderable>())
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
                        m_renderables[is_transparent ? RendererEntityType::geometry_transparent : RendererEntityType::geometry_opaque].emplace_back(entity);
                    }
                }

                if (Light* light = entity->GetComponent<Light>())
                {
                    m_renderables[RendererEntityType::light].emplace_back(entity);
                }

                if (shared_ptr<Camera> camera = entity->GetComponentShared<Camera>())
                {
                    m_renderables[RendererEntityType::camera].emplace_back(entity);
                    m_camera = camera;
                }

                if (ReflectionProbe* reflection_probe = entity->GetComponent<ReflectionProbe>())
                {
                    m_renderables[RendererEntityType::reflection_probe].emplace_back(entity);
                }
            }

            // Sort them by distance
            SortRenderables(&m_renderables[RendererEntityType::geometry_opaque]);
            SortRenderables(&m_renderables[RendererEntityType::geometry_transparent]);

            m_renderables_pending.clear();
            m_add_new_entities = false;
        }

        // Handle environment texture assignment requests
        if (m_environment_texture_dirty)
        {
            lock_guard lock(m_mutex_environment_texture);
            m_environment_texture       = m_environment->GetTexture();
            m_environment_texture_dirty = false;
        }

        // Handle texture mip generation requests
        {
            lock_guard<mutex> guard(m_mutex_mip_generation);

            // Generate mips for any pending texture requests
            for (weak_ptr<RHI_Texture> tex : m_textures_mip_generation)
            {
                if (shared_ptr<RHI_Texture> texture = tex.lock())
                {
                    // Downsample
                    Pass_Ffx_Spd(m_cmd_current, texture.get());

                    // Set all generated mips to read only optimal
                    texture->SetLayout(RHI_Image_Layout::Shader_Read_Only_Optimal, cmd_list, 0, texture->GetMipCount());

                    // Destroy per mip resource views since they are no longer needed
                    {
                        // Remove unnecessary flags from texture (were only needed for the downsampling)
                        uint32_t flags = texture->GetFlags();
                        flags &= ~RHI_Texture_PerMipViews;
                        flags &= ~RHI_Texture_Uav;
                        texture->SetFlags(flags);

                        // Destroy the resources associated with those flags
                        {
                            const bool destroy_main     = false;
                            const bool destroy_per_view = true;
                            texture->RHI_DestroyResource(destroy_main, destroy_per_view);
                        }
                    }
                }
            }

            m_textures_mip_generation.clear();
        }

        RHI_Device::ParseDeletionQueue();
    }

    void Renderer::SortRenderables(vector<shared_ptr<Entity>>* renderables)
    {
        if (!m_camera || renderables->size() <= 2)
            return;

        auto comparison_op = [](shared_ptr<Entity> entity)
        {
            auto renderable = entity->GetRenderable();
            if (!renderable)
                return 0.0f;

            return (renderable->GetAabb().GetCenter() - m_camera->GetTransform()->GetPosition()).LengthSquared();
        };

        // Sort by depth (front to back)
        sort(renderables->begin(), renderables->end(), [&comparison_op](shared_ptr<Entity> a, shared_ptr<Entity> b)
            {
                return comparison_op(a) < comparison_op(b);
            });
    }

    bool Renderer::IsCallingFromOtherThread()
    {
        return m_render_thread_id != this_thread::get_id();
    }

    const shared_ptr<RHI_Texture> Renderer::GetEnvironmentTexture()
    {
        return m_environment_texture ? m_environment_texture : m_tex_default_black;
    }

    void Renderer::SetEnvironment(Environment* environment)
    {
        lock_guard lock(m_mutex_environment_texture);

        m_environment = environment;
        m_environment_texture_dirty = true;
    }

    void Renderer::SetOption(RendererOption option, float value)
    {
        // Clamp value
        {
            // Anisotropy
            if (option == RendererOption::Anisotropy)
            {
                value = Helper::Clamp(value, 0.0f, 16.0f);
            }
            // Shadow resolution
            else if (option == RendererOption::ShadowResolution)
            {
                value = Helper::Clamp(value, static_cast<float>(m_resolution_shadow_min), static_cast<float>(RHI_Device::GetMaxTexture2dDimension()));
            }
        }

        // Early exit if the value is already set
        if (m_options[static_cast<uint32_t>(option)] == value)
            return;

        // Reject changes (if needed)
        {
            bool is_d3d11 = RHI_Context::api_type == RHI_Api_Type::D3d11;

            if (is_d3d11 && option == RendererOption::Antialiasing && (value == static_cast<float>(AntialiasingMode::Taa) || value == static_cast<float>(AntialiasingMode::TaaFxaa)))
            {
                SP_LOG_WARNING("TAA is not supported on D3D11");
                return;
            }

            if (is_d3d11 && option == RendererOption::Upsampling && value == static_cast<float>(UpsamplingMode::FSR2))
            {
                SP_LOG_WARNING("FSR 2.0 is not supported on D3D11");
                return;
            }

            if (option == RendererOption::Hdr)
            {
                if (value == 1.0f && !Display::GetHdr())
                {
                    SP_LOG_INFO("This display doesn't support HDR");
                    return;
                }
            }
        }

        // Set new value
        m_options[static_cast<uint32_t>(option)] = value;

        // Handle cascading changes
        {
            // Antialiasing
            if (option == RendererOption::Antialiasing)
            {
                bool taa_enabled = value == static_cast<float>(AntialiasingMode::Taa) || value == static_cast<float>(AntialiasingMode::TaaFxaa);
                bool fsr_enabled = GetOption<UpsamplingMode>(RendererOption::Upsampling) == UpsamplingMode::FSR2;

                if (taa_enabled)
                {
                    // Implicitly enable FSR since it's doing TAA.
                    if (!fsr_enabled)
                    {
                        m_options[static_cast<uint32_t>(RendererOption::Upsampling)] = static_cast<float>(UpsamplingMode::FSR2);
                        m_ffx_fsr2_reset = true;
                        SP_LOG_INFO("Enabled FSR 2.0 since it's used for TAA.");
                    }
                }
                else
                {
                    // Implicitly disable FSR since it's doing TAA
                    if (fsr_enabled)
                    {
                        m_options[static_cast<uint32_t>(RendererOption::Upsampling)] = static_cast<float>(UpsamplingMode::Linear);
                        SP_LOG_INFO("Disabed FSR 2.0 since it's used for TAA.");
                    }
                }
            }
            // Upsampling
            else if (option == RendererOption::Upsampling)
            {
                bool taa_enabled = GetOption<AntialiasingMode>(RendererOption::Antialiasing) == AntialiasingMode::Taa;

                if (value == static_cast<float>(UpsamplingMode::Linear))
                {
                    // Implicitly disable TAA since FSR 2.0 is doing it
                    if (taa_enabled)
                    {
                        m_options[static_cast<uint32_t>(RendererOption::Antialiasing)] = static_cast<float>(AntialiasingMode::Disabled);
                        SP_LOG_INFO("Disabled TAA since it's done by FSR 2.0.");
                    }
                }
                else if (value == static_cast<float>(UpsamplingMode::FSR2))
                {
                    // Implicitly enable TAA since FSR 2.0 is doing it
                    if (!taa_enabled)
                    {
                        m_options[static_cast<uint32_t>(RendererOption::Antialiasing)] = static_cast<float>(AntialiasingMode::Taa);
                        m_ffx_fsr2_reset = true;
                        SP_LOG_INFO("Enabled TAA since FSR 2.0 does it.");
                    }
                }
            }
            // Shadow resolution
            else if (option == RendererOption::ShadowResolution)
            {
                const auto& light_entities = m_renderables[RendererEntityType::light];
                for (const auto& light_entity : light_entities)
                {
                    auto light = light_entity->GetComponent<Light>();
                    if (light->GetShadowsEnabled())
                    {
                        light->CreateShadowMap();
                    }
                }
            }
            else if (option == RendererOption::Hdr)
            {
                m_swap_chain->SetHdr(value == 1.0f);
            }
            else if (option == RendererOption::Vsync)
            {
                m_swap_chain->SetVsync(value == 1.0f);
            }
        }
    }

    array<float, 34>& Renderer::GetOptions()
    {
        return m_options;
    }

    void Renderer::SetOptions(array<float, 34> options)
    {
        m_options = options;
    }

    RHI_SwapChain* Renderer::GetSwapChain()
    {
        return m_swap_chain.get();
    }
    
    void Renderer::Present()
    {
        if (!m_is_rendering_allowed)
            return;

        if (Window::IsMinimised())
        {
            SP_LOG_INFO("Skipping present, the window is minimzed");
            return;
        }

        if (m_swap_chain->GetLayout() != RHI_Image_Layout::Present_Src)
        {
            SP_LOG_INFO("Skipping present, Pass_CopyToBackbuffer() was not called.");
            return;
        }

        m_swap_chain->Present();
        SP_FIRE_EVENT(EventType::RendererPostPresent);
    }

    void Renderer::Flush()
    {
        // The external thread requests a flush from the renderer thread (to avoid a myriad of thread issues and Vulkan errors)
        if (IsCallingFromOtherThread())
        {
            m_is_rendering_allowed = false;
            m_flush_requested      = true;

            while (m_flush_requested)
            {
                SP_LOG_INFO("External thread is waiting for the renderer thread to flush...");
                this_thread::sleep_for(chrono::milliseconds(16));
            }

            return;
        }

        // Flushing
        if (!m_is_rendering_allowed)
        {
            SP_LOG_INFO("Renderer thread is flushing...");
            RHI_Device::QueueWaitAll();
        }

        m_flush_requested = false;
    }

    RHI_CommandList* Renderer::GetCmdList()
    {
        return m_cmd_current;
    }

    RHI_Api_Type Renderer::GetRhiApiType()
    {
        return RHI_Context::api_type;
    }

    void Renderer::RequestTextureMipGeneration(shared_ptr<RHI_Texture> texture)
    {
        SP_ASSERT(texture != nullptr);
        SP_ASSERT(texture->GetRhiSrv() != nullptr);
        SP_ASSERT(texture->HasMips());        // Ensure the texture requires mips
        SP_ASSERT(texture->HasPerMipViews()); // Ensure that the texture has per mip views since they are required for GPU downsampling.
        SP_ASSERT(texture->IsReadyForUse());  // Ensure that any loading and resource creation has finished

        lock_guard<mutex> guard(m_mutex_mip_generation);
        m_textures_mip_generation.push_back(texture);
    }

    RHI_Texture* Renderer::GetFrameTexture()
    {
        return GetRenderTarget(RendererTexture::frame_output).get();
    }

    uint64_t Renderer::GetFrameNum()
    {
        return m_frame_num;
    }

    shared_ptr<Camera> Renderer::GetCamera()
    {
        return m_camera;
    }

    unordered_map<RendererEntityType, vector<shared_ptr<Entity>>>& Renderer::GetEntities()
    {
        return m_renderables;
    }
}
