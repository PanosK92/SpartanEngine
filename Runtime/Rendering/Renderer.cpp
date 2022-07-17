/*
Copyright(c) 2016-2022 Panos Karabelas

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
#include "Spartan.h"                            
#include "Renderer.h"                           
#include "Model.h"                              
#include "Grid.h"                               
#include "Font/Font.h"                          
#include "../Profiling/Profiler.h"              
#include "../Resource/ResourceCache.h"          
#include "../World/Entity.h"                    
#include "../World/Components/Transform.h"      
#include "../World/Components/Renderable.h"     
#include "../World/Components/Camera.h"         
#include "../World/Components/Light.h"          
#include "../World/Components/ReflectionProbe.h"
#include "../RHI/RHI_Device.h"                  
#include "../RHI/RHI_ConstantBuffer.h"          
#include "../RHI/RHI_StructuredBuffer.h"        
#include "../RHI/RHI_CommandList.h"             
#include "../RHI/RHI_Texture2D.h"               
#include "../RHI/RHI_SwapChain.h"               
#include "../RHI/RHI_VertexBuffer.h"            
#include "../RHI/RHI_Implementation.h"          
#include "../RHI/RHI_Semaphore.h"
#include "../RHI/RHI_CommandPool.h"
#include "../Core/Window.h"                     
#include "../Input/Input.h"                     
#include "../World/Components/Environment.h"    
#include "../RHI/RHI_FSR.h"
//==============================================

//= NAMESPACES ===============
using namespace std;
using namespace Spartan::Math;
//============================

#define render_target(rt_enum) m_render_targets[static_cast<uint8_t>(rt_enum)]

namespace Spartan
{
    Renderer::Renderer(Context* context) : Subsystem(context)
    {
        // Default options
        m_options.fill(0.0f);
        SetOption(RendererOption::ReverseZ, 1.0f);
        SetOption(RendererOption::Transform_Handle, 1.0f);
        SetOption(RendererOption::Debug_Grid, 1.0f);
        SetOption(RendererOption::Debug_ReflectionProbes, 1.0f);
        SetOption(RendererOption::Debug_Lights, 1.0f);
        SetOption(RendererOption::Debug_Physics, 1.0f);
        SetOption(RendererOption::Bloom, 0.2f); // Non-zero values activate it and define the blend factor.
        SetOption(RendererOption::VolumetricFog, 1.0f);
        SetOption(RendererOption::MotionBlur, 1.0f);
        SetOption(RendererOption::Ssao, 1.0f);
        SetOption(RendererOption::Ssao_Gi, 1.0f);
        SetOption(RendererOption::ScreenSpaceShadows, 1.0f);
        SetOption(RendererOption::ScreenSpaceReflections, 1.0f);
        SetOption(RendererOption::Antialiasing, static_cast<float>(AntialiasingMode::Taa));
        SetOption(RendererOption::Ffx_Cas, 1.0f);
        SetOption(RendererOption::Anisotropy, 16.0f);
        SetOption(RendererOption::ShadowResolution, 2048.0f);
        SetOption(RendererOption::Tonemapping, static_cast<float>(TonemappingMode::Disabled));
        SetOption(RendererOption::Gamma, 1.5f);
        SetOption(RendererOption::Sharpen_Strength, 1.0f);
        SetOption(RendererOption::Fog, 0.08f);
        //SetOption(RendererOption::DepthOfField, 1.0f);        // This is depth of field from ALDI, so until I improve it, it should be disabled by default.
        //SetOption(RendererOption::Render_DepthPrepass, 1.0f); // Depth-pre-pass is not always faster, so by default, it's disabled.
        //SetOption(RendererOption::Debanding, 1.0f);           // Disable debanding as we shouldn't be seeing debanding to begin with.

        // Subscribe to events.
        SP_SUBSCRIBE_TO_EVENT(EventType::WorldResolved, SP_EVENT_HANDLER_VARIANT(OnRenderablesAcquire));
        SP_SUBSCRIBE_TO_EVENT(EventType::WorldPreClear, SP_EVENT_HANDLER(OnClear));
        SP_SUBSCRIBE_TO_EVENT(EventType::WorldLoadEnd, SP_EVENT_HANDLER(OnWorldLoaded));
        SP_SUBSCRIBE_TO_EVENT(EventType::WindowOnFullScreenToggled, SP_EVENT_HANDLER(OnFullScreenToggled));

        // Get thread id.
        m_render_thread_id = this_thread::get_id();

        m_material_instances.fill(nullptr);
    }

    Renderer::~Renderer()
    {
        RHI_FSR::Destroy();

        // Unsubscribe from events
        SP_UNSUBSCRIBE_FROM_EVENT(EventType::WorldResolved,             SP_EVENT_HANDLER_VARIANT(OnRenderablesAcquire));
        SP_UNSUBSCRIBE_FROM_EVENT(EventType::WorldPreClear,             SP_EVENT_HANDLER(OnClear));
        SP_UNSUBSCRIBE_FROM_EVENT(EventType::WorldLoadEnd,              SP_EVENT_HANDLER(OnWorldLoaded));
        SP_UNSUBSCRIBE_FROM_EVENT(EventType::WindowOnFullScreenToggled, SP_EVENT_HANDLER(OnFullScreenToggled));

        // Log to file as the renderer is no more
        Log::m_log_to_file = true;
    }

    void Renderer::OnInitialize()
    {
        // Get window subsystem (required in order to know a windows size and also create a swapchain for it).
        Window* window = m_context->GetSubsystem<Window>();
        SP_ASSERT_MSG(window, "The Renderer subsystem requires a Window subsystem.");

        // Get resource cache subsystem (required in order to know from which paths to load shaders, textures and fonts).
        m_resource_cache = m_context->GetSubsystem<ResourceCache>();
        SP_ASSERT_MSG(window, "The Renderer subsystem requires a ResourceCache subsystem.");

        // Get profiler subsystem (used to profile things but not required)
        m_profiler = m_context->GetSubsystem<Profiler>();

        // Create device
        m_rhi_device = make_shared<RHI_Device>(m_context);

        // Line buffer
        m_vertex_buffer_lines = make_shared<RHI_VertexBuffer>(m_rhi_device, true, "renderer_lines");

        // World grid
        m_gizmo_grid = make_unique<Grid>(m_rhi_device);

        // Get window size
        uint32_t window_width  = window->GetWidth();
        uint32_t window_height = window->GetHeight();

        // Create swap chain
        m_swap_chain = make_shared<RHI_SwapChain>
        (
            window->GetHandle(),
            m_rhi_device,
            window_width,
            window_height,
            RHI_Format_R8G8B8A8_Unorm,
            m_swap_chain_buffer_count,
            RHI_Present_Immediate | RHI_Swap_Flip_Discard,
            "renderer"
         );

        // Create command pool
        m_cmd_pool = m_rhi_device->AllocateCommandPool("renderer", m_swap_chain->GetObjectId());

        // Create command lists
        m_cmd_pool->AllocateCommandLists(m_swap_chain_buffer_count);

        // Set render, output and viewport resolution/size to whatever the window is (initially)
        SetResolutionRender(window_width, window_height, false);
        SetResolutionOutput(window_width, window_height, false);
        SetViewport(static_cast<float>(window_width), static_cast<float>(window_height));

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
    }

    void Renderer::OnTick(double delta_time)
    {
        // After the first 100 frames, stop logging to a file.
        // In other words if this function is still running, it means that the renderer/imgui can log on screen.
        if (m_frame_num == 100 && Log::m_log_to_file)
        {
           Log::m_log_to_file = false;
        }

        // Happens when core resources are created/destroyed
        if (m_flush_requested)
        {
            Flush();
        }

        // Resize swapchain to window size (if needed)
        {
            // Passing zero dimensions will cause the swapchain to not present at all
            Window* window  = m_context->GetSubsystem<Window>();
            uint32_t width  = static_cast<uint32_t>(window->IsMinimised() ? 0 : window->GetWidth());
            uint32_t height = static_cast<uint32_t>(window->IsMinimised() ? 0 : window->GetHeight());

            if ((m_swap_chain->GetWidth() != width || m_swap_chain->GetHeight() != height) || !m_swap_chain->PresentEnabled())
            {
                if (m_swap_chain->Resize(width, height))
                {
                    LOG_INFO("Swapchain resolution has been set to %dx%d", width, height);
                }
            }
        }

        if (!m_swap_chain->PresentEnabled() || !m_is_rendering_allowed)
            return;

        m_frame_num++;
        m_is_odd_frame = (m_frame_num % 2) == 1;

        // Begin
        bool command_pool_reset = m_cmd_pool->Tick();
        m_cmd_current = m_cmd_pool->GetCurrentCommandList();
        m_cmd_current->Begin();

        // Reset
        if (command_pool_reset)
        {
            // Reset dynamic buffer indices
            m_cb_uber_gpu->ResetOffset();
            m_cb_frame_gpu->ResetOffset();
            m_cb_light_gpu->ResetOffset();
            m_cb_material_gpu->ResetOffset();

            // Handle requests (they can come from different threads)
            m_reading_requests = true;
            {
                // Handle environment texture assignment requests
                if (m_environment_texture_temp)
                {
                    m_environment_texture      = m_environment_texture_temp;
                    m_environment_texture_temp = nullptr;
                }

                // Handle texture mip generation requests
                {
                    // Clear any previously processed textures
                    if (!m_textures_mip_generation.empty())
                    {
                        for (shared_ptr<RHI_Texture> texture : m_textures_mip_generation)
                        {
                            // Remove unnecessary flags from texture (were only needed for the downsampling)
                            uint32_t flags = texture->GetFlags();
                            flags &= ~RHI_Texture_PerMipViews;
                            flags &= ~RHI_Texture_Uav;
                            texture->SetFlags(flags);

                            // Destroy the resources associated with those flags
                            {
                                const bool destroy_main = false;
                                const bool destroy_per_view = true;
                                texture->RHI_DestroyResource(destroy_main, destroy_per_view);
                            }
                        }

                        m_textures_mip_generation.clear();
                    }

                    // Add any newly requested textures
                    if (!m_textures_mip_generation_pending.empty())
                    {
                        m_textures_mip_generation.insert(m_textures_mip_generation.end(), m_textures_mip_generation_pending.begin(), m_textures_mip_generation_pending.end());
                        m_textures_mip_generation_pending.clear();
                    }

                    // Generate mips for any pending texture requests
                    Pass_Generate_Mips(m_cmd_current);
                }
            }
            m_reading_requests = false;
        }

        // Update frame buffer
        {
            // Matrices
            if (m_camera)
            {
                if (m_dirty_orthographic_projection || m_near_plane != m_camera->GetNearPlane() || m_far_plane != m_camera->GetFarPlane())
                {
                    m_near_plane = m_camera->GetNearPlane();
                    m_far_plane  = m_camera->GetFarPlane();

                    // Near clip does not affect depth accuracy in orthographic projection, so set it to 0 to avoid problems which can result an infinitely small [3,2] after the multiplication below.
                    m_cb_frame_cpu.projection_ortho      = Matrix::CreateOrthographicLH(m_viewport.width, m_viewport.height, 0.0f, m_far_plane);
                    m_cb_frame_cpu.view_projection_ortho = Matrix::CreateLookAtLH(Vector3(0, 0, -m_near_plane), Vector3::Forward, Vector3::Up) * m_cb_frame_cpu.projection_ortho;

                    m_dirty_orthographic_projection = false;
                }

                m_cb_frame_cpu.view                = m_camera->GetViewMatrix();
                m_cb_frame_cpu.projection          = m_camera->GetProjectionMatrix();
                m_cb_frame_cpu.projection_inverted = Matrix::Invert(m_cb_frame_cpu.projection);
            }

            // Generate jitter sample in case FSR (which also does TAA) is enabled. D3D11 only receives FXAA so it's ignored at this point.
            UpsamplingMode upsampling_mode = GetOption<UpsamplingMode>(RendererOption::Upsampling);
            if ((upsampling_mode == UpsamplingMode::FSR || GetOption<AntialiasingMode>(RendererOption::Antialiasing) == AntialiasingMode::Taa) && RHI_Device::GetApiType() != RHI_Api_Type::D3d11)
            {
                RHI_FSR::GenerateJitterSample(&m_taa_jitter.x, &m_taa_jitter.y);
                m_taa_jitter.x            = (m_taa_jitter.x / m_resolution_render.x);
                m_taa_jitter.y            = (m_taa_jitter.y / m_resolution_render.y);
                m_cb_frame_cpu.projection *= Matrix::CreateTranslation(Vector3(m_taa_jitter.x, m_taa_jitter.y, 0.0f));
            }
            else
            {
                m_taa_jitter = Vector2::Zero;
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
            m_cb_frame_cpu.taa_jitter_current     = m_taa_jitter;
            m_cb_frame_cpu.delta_time             = static_cast<float>(m_context->GetSubsystem<Timer>()->GetDeltaTimeSmoothedSec());
            m_cb_frame_cpu.time                   = static_cast<float>(m_context->GetSubsystem<Timer>()->GetTimeSec());
            m_cb_frame_cpu.bloom_intensity        = GetOption<float>(RendererOption::Bloom);
            m_cb_frame_cpu.sharpen_strength       = GetOption<float>(RendererOption::Sharpen_Strength);
            m_cb_frame_cpu.fog                    = GetOption<float>(RendererOption::Fog);
            m_cb_frame_cpu.tonemapping            = GetOption<float>(RendererOption::Tonemapping);
            m_cb_frame_cpu.gamma                  = GetOption<float>(RendererOption::Gamma);
            m_cb_frame_cpu.shadow_resolution      = GetOption<float>(RendererOption::ShadowResolution);
            m_cb_frame_cpu.frame                  = static_cast<uint32_t>(m_frame_num);
            m_cb_frame_cpu.frame_mip_count        = render_target(RendererTexture::Frame_Render)->GetMipCount();
            m_cb_frame_cpu.ssr_mip_count          = render_target(RendererTexture::Ssr)->GetMipCount();
            m_cb_frame_cpu.resolution_environment = Vector2(GetEnvironmentTexture()->GetWidth(), GetEnvironmentTexture()->GetHeight());

            // These must match what Common_Buffer.hlsl is reading
            m_cb_frame_cpu.set_bit(GetOption<bool>(RendererOption::ScreenSpaceReflections), 1 << 0);
            m_cb_frame_cpu.set_bit(GetOption<bool>(RendererOption::Ssao), 1 << 1);
            m_cb_frame_cpu.set_bit(GetOption<bool>(RendererOption::VolumetricFog), 1 << 2);
            m_cb_frame_cpu.set_bit(GetOption<bool>(RendererOption::ScreenSpaceShadows), 1 << 3);
            m_cb_frame_cpu.set_bit(GetOption<bool>(RendererOption::Ssao_Gi), 1 << 4);
        }

        Lines_PreMain();
        Pass_Main(m_cmd_current);
        Lines_PostMain(delta_time);

        // Submit
        m_cmd_current->End();
        m_cmd_current->Submit();
    }
    
    void Renderer::SetViewport(float width, float height)
    {
        if (IsCallingFromOtherThread())
        {
            while (m_reading_requests)
            {
                LOG_INFO("External thread is waiting for the renderer thread...");
                this_thread::sleep_for(chrono::milliseconds(16));
            }
        }

        if (m_viewport.width != width || m_viewport.height != height)
        {
            m_viewport.width = width;
            m_viewport.height = height;

            m_dirty_orthographic_projection = true;
        }
    }

    void Renderer::SetResolutionRender(uint32_t width, uint32_t height, bool recreate_resources /*= true*/)
    {
        // Return if resolution is invalid
        if (!m_rhi_device->IsValidResolution(width, height))
        {
            LOG_WARNING("%dx%d is an invalid resolution", width, height);
            return;
        }

        // Silently return if resolution is already set
        if (m_resolution_render.x == width && m_resolution_render.y == height)
            return;

        // Set resolution
        m_resolution_render.x = static_cast<float>(width);
        m_resolution_render.y = static_cast<float>(height);

        // Set as active display mode
        DisplayMode display_mode = Display::GetActiveDisplayMode();
        display_mode.width       = width;
        display_mode.height      = height;
        Display::SetActiveDisplayMode(display_mode);

        // Register display mode (in case it doesn't exist) but maintain the fps limit
        bool update_fps_limit_to_highest_hz = false;
        Display::RegisterDisplayMode(display_mode, update_fps_limit_to_highest_hz, m_context);

        if (recreate_resources)
        {
            // Re-create render textures
            CreateRenderTextures(true, false, false, true);

            // Re-create samplers
            CreateSamplers(true);
        }

        // Log
        LOG_INFO("Render resolution has been set to %dx%d", width, height);
    }

    void Renderer::SetResolutionOutput(uint32_t width, uint32_t height, bool recreate_resources /*= true*/)
    {
        // Return if resolution is invalid
        if (!m_rhi_device->IsValidResolution(width, height))
        {
            LOG_WARNING("%dx%d is an invalid resolution", width, height);
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
        LOG_INFO("Output resolution output has been set to %dx%d", width, height);
    }

    void Renderer::Update_Cb_Frame(RHI_CommandList* cmd_list)
    {
        // Update directional light intensity, just grab the first one
        for (const auto& entity : m_entities[RendererEntityType::Light])
        {
            if (Light* light = entity->GetComponent<Light>())
            {
                if (light->GetLightType() == LightType::Directional)
                {
                    m_cb_frame_cpu.directional_light_intensity = light->GetIntensity();
                }
            }
        }

        bool reallocated = m_cb_frame_gpu->AutoUpdate<Cb_Frame>( m_cb_frame_cpu, m_cb_frame_cpu_mapped);

        if (reallocated)
        {
            cmd_list->Discard();
        }

        // Bind because the offset just changed
        cmd_list->SetConstantBuffer(RendererBindingsCb::frame, RHI_Shader_Vertex | RHI_Shader_Pixel | RHI_Shader_Compute, m_cb_frame_gpu);
    }

    void Renderer::Update_Cb_Uber(RHI_CommandList* cmd_list)
    {
        bool reallocated = m_cb_uber_gpu->AutoUpdate<Cb_Uber>(m_cb_uber_cpu, m_cb_uber_cpu_mapped);

        if (reallocated)
        {
            cmd_list->Discard();
        }

        // Bind because the offset just changed
        cmd_list->SetConstantBuffer(RendererBindingsCb::uber, RHI_Shader_Vertex | RHI_Shader_Pixel | RHI_Shader_Compute, m_cb_uber_gpu);
    }

    void Renderer::Update_Cb_Light(RHI_CommandList* cmd_list, const Light* light, const RHI_Shader_Type scope)
    {
        for (uint32_t i = 0; i < light->GetShadowArraySize(); i++)
        {
            m_cb_light_cpu.view_projection[i] = light->GetViewMatrix(i) * light->GetProjectionMatrix(i);
        }

        // Convert luminous power to luminous intensity
        float luminous_intensity = light->GetIntensity() * m_camera->GetExposure();
        if (light->GetLightType() == LightType::Point)
        {
            luminous_intensity /= Math::Helper::PI_4; // lumens to candelas
            luminous_intensity *= 255.0f; // this is a hack, must fix whats my color units
        }
        else if (light->GetLightType() == LightType::Spot)
        {
            luminous_intensity /= Math::Helper::PI; // lumens to candelas
            luminous_intensity *= 255.0f; // this is a hack, must fix whats my color units
        }

        m_cb_light_cpu.intensity_range_angle_bias = Vector4(luminous_intensity, light->GetRange(), light->GetAngle(), GetOption<bool>(RendererOption::ReverseZ) ? light->GetBias() : -light->GetBias());
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

        bool reallocated = m_cb_light_gpu->AutoUpdate<Cb_Light>(m_cb_light_cpu, m_cb_light_cpu_mapped);

        if (reallocated)
        {
            cmd_list->Discard();
        }

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

        bool reallocated = m_cb_material_gpu->AutoUpdate<Cb_Material>(m_cb_material_cpu, m_cb_material_cpu_mapped);

        if (reallocated)
        {
            cmd_list->Discard();
        }

        // Bind because the offset just changed
        cmd_list->SetConstantBuffer(RendererBindingsCb::material, RHI_Shader_Pixel, m_cb_material_gpu);
    }

    void Renderer::OnRenderablesAcquire(const Variant& entities_variant)
    {
        SCOPED_TIME_BLOCK(m_profiler);

        // Clear previous state
        m_entities.clear();
        m_camera = nullptr;

        vector<shared_ptr<Entity>> entities = entities_variant.Get<vector<shared_ptr<Entity>>>();
        for (const auto& entity : entities)
        {
            if (!entity || !entity->IsActive())
                continue;

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
                    m_entities[is_transparent ? RendererEntityType::GeometryTransparent : RendererEntityType::GeometryOpaque].emplace_back(entity.get());
                }
            }

            if (Light* light = entity->GetComponent<Light>())
            {
                m_entities[RendererEntityType::Light].emplace_back(entity.get());
            }

            if (Camera* camera = entity->GetComponent<Camera>())
            {
                m_entities[RendererEntityType::Camera].emplace_back(entity.get());
                m_camera = camera->GetPtrShared<Camera>();
            }

            if (ReflectionProbe* reflection_probe = entity->GetComponent<ReflectionProbe>())
            {
                m_entities[RendererEntityType::ReflectionProbe].emplace_back(entity.get());
            }
        }

        SortRenderables(&m_entities[RendererEntityType::GeometryOpaque]);
        SortRenderables(&m_entities[RendererEntityType::GeometryTransparent]);
    }

    void Renderer::OnClear()
    {
        // Flush to remove references to entity resources that will be deallocated
        Flush();
        m_entities.clear();
    }

    void Renderer::OnWorldLoaded()
    {
        m_is_rendering_allowed = true;
    }

    void Renderer::OnFullScreenToggled()
    {
        Window* window            = m_context->GetSubsystem<Window>();
        Input* input              = m_context->GetSubsystem<Input>();
        const bool is_full_screen = window->IsFullScreen();

        if (is_full_screen)
        {
            m_viewport_previous          = m_viewport;
            m_resolution_output_previous = m_resolution_output;
            
            SetViewport(static_cast<float>(window->GetWidth()), static_cast<float>(window->GetHeight()));
            SetResolutionOutput(window->GetWidth(), window->GetHeight());
        }
        else
        {
            SetViewport(m_viewport_previous.x, m_viewport_previous.y);
            SetResolutionOutput(static_cast<uint32_t>(m_resolution_output_previous.x), static_cast<uint32_t>(m_resolution_output_previous.y));
        }

        input->SetMouseCursorVisible(!is_full_screen);
    }

    void Renderer::SortRenderables(vector<Entity*>* renderables)
    {
        if (!m_camera || renderables->size() <= 2)
            return;

        auto comparison_op = [this](Entity* entity)
        {
            auto renderable = entity->GetRenderable();
            if (!renderable)
                return 0.0f;

            return (renderable->GetAabb().GetCenter() - m_camera->GetTransform()->GetPosition()).LengthSquared();
        };

        // Sort by depth (front to back)
        sort(renderables->begin(), renderables->end(), [&comparison_op](Entity* a, Entity* b)
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

    void Renderer::SetEnvironmentTexture(shared_ptr<RHI_Texture> texture)
    {
        if (IsCallingFromOtherThread())
        {
            while (m_reading_requests)
            {
                LOG_INFO("External thread is waiting for the renderer thread...");
                this_thread::sleep_for(chrono::milliseconds(16));
            }
        }

        lock_guard<mutex> guard(m_environment_texture_mutex);
        m_environment_texture_temp = texture;
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
            else if (option == RendererOption::ShadowResolution && m_rhi_device)
            {
                value = Helper::Clamp(value, static_cast<float>(m_resolution_shadow_min), static_cast<float>(m_rhi_device->GetMaxTexture2dDimension()));
            }
        }

        // Early exit if the value is already set
        if (m_options[static_cast<uint32_t>(option)] == value)
            return;

        // Set new value
        m_options[static_cast<uint32_t>(option)] = value;

        // Handle cascading changes for any options that require it
        {
            // Reverse-z
            if (option == RendererOption::ReverseZ)
            {
                if (m_rhi_device)
                {
                    CreateDepthStencilStates();
                }

                if (m_camera)
                {
                    m_camera->MakeDirty();
                }
            }
            // TAA
            else if (option == RendererOption::Antialiasing)
            {
                if (value == static_cast<float>(AntialiasingMode::Taa) || value == static_cast<float>(AntialiasingMode::TaaFxaa))
                {
                    // We lo longer maintain a custom TAA pass as FSR 2.0's TAA is superior, so fall back to FXAA.
                    if (RHI_Device::GetApiType() == RHI_Api_Type::D3d11)
                    {
                        SetOption(RendererOption::Antialiasing, static_cast<float>(AntialiasingMode::Fxaa));
                        LOG_WARNING("TAA is not supported for D3D11, switching to FXAA.");
                    }
                    else if (GetOption<UpsamplingMode>(RendererOption::Upsampling) != UpsamplingMode::FSR)
                    {
                        SetOption(RendererOption::Upsampling, static_cast<float>(UpsamplingMode::FSR));
                        LOG_INFO("Enabled FSR 2.0 since it's used for TAA.");
                    }
                }
                else
                {
                    if (GetOption<UpsamplingMode>(RendererOption::Upsampling) == UpsamplingMode::FSR)
                    {
                        SetOption(RendererOption::Upsampling, static_cast<float>(UpsamplingMode::Linear));
                        LOG_INFO("Disabed FSR 2.0 since it's used for TAA.");
                    }
                }
            }
            // Shadow resolution
            else if (option == RendererOption::ShadowResolution)
            {
                const auto& light_entities = m_entities[RendererEntityType::Light];
                for (const auto& light_entity : light_entities)
                {
                    auto light = light_entity->GetComponent<Light>();
                    if (light->GetShadowsEnabled())
                    {
                        light->CreateShadowMap();
                    }
                }
            }
            // Upsampling
            else if (option == RendererOption::Upsampling)
            {
                if (value == static_cast<float>(UpsamplingMode::Linear))
                {
                    if (GetOption<AntialiasingMode>(RendererOption::Antialiasing) == AntialiasingMode::Taa)
                    {
                        SetOption(RendererOption::Antialiasing, static_cast<float>(AntialiasingMode::Disabled));
                        LOG_INFO("Disabled TAA since it's done by FSR 2.0");
                    }
                }
                else if (value == static_cast<float>(UpsamplingMode::FSR) && RHI_Device::GetApiType() != RHI_Api_Type::D3d11)
                {
                    SetOption(RendererOption::Antialiasing, static_cast<float>(AntialiasingMode::Taa));
                    LOG_INFO("Enabled TAA since FSR 2.0 does it.");
                }
            }
        }
    }

    void Renderer::Present()
    {
        if (!m_swap_chain->PresentEnabled())
            return;

        m_swap_chain->Present();

        // Notify subsystems that need to calculate things after presenting, like the profiler.
        SP_FIRE_EVENT(EventType::PostPresent);
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
                LOG_INFO("External thread is waiting for the renderer thread to flush...");
                this_thread::sleep_for(chrono::milliseconds(16));
            }

            return;
        }

        // Flushing
        {
            if (!m_is_rendering_allowed)
            {
                LOG_INFO("Renderer thread is flushing...");

                if (!m_rhi_device->QueueWaitAll())
                {
                    LOG_ERROR("Failed to flush GPU");
                }
            }

            if (m_cmd_current)
            {
                m_cmd_current->Discard();
            }
        }

        m_flush_requested = false;
    }

    RHI_Api_Type Renderer::GetApiType()
    {
        return RHI_Context::api_type;
    }

    void Renderer::RequestTextureMipGeneration(shared_ptr<RHI_Texture> texture)
    {
        if (IsCallingFromOtherThread())
        {
            while (m_reading_requests)
            {
                LOG_INFO("External thread is waiting for the renderer thread...");
                this_thread::sleep_for(chrono::milliseconds(16));
            }
        }

        // Validate
        SP_ASSERT(texture != nullptr);
        SP_ASSERT(texture->GetRhiSrv() != nullptr);
        SP_ASSERT(texture->HasMips());        // Ensure the texture requires mips
        SP_ASSERT(texture->HasPerMipViews()); // Ensure that the texture has per mip views since they are required for GPU downsampling.

        lock_guard<mutex> guard(m_texture_mip_generation_mutex);
        m_textures_mip_generation_pending.push_back(texture);
    }

    uint32_t Renderer::GetCmdIndex() const
    {
        return m_cmd_pool->GetCommandListIndex();
    }
}
