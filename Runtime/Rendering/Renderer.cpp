/*
Copyright(c) 2016-2020 Panos Karabelas

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

//= INCLUDES ==============================
#include "Spartan.h"
#include "Renderer.h"
#include "Model.h"
#include "Font/Font.h"
#include "Gizmos/Grid.h"
#include "Gizmos/Transform_Gizmo.h"
#include "../Utilities/Sampling.h"
#include "../Profiling/Profiler.h"
#include "../Resource/ResourceCache.h"
#include "../World/Entity.h"
#include "../World/Components/Transform.h"
#include "../World/Components/Renderable.h"
#include "../World/Components/Camera.h"
#include "../World/Components/Light.h"
#include "../RHI/RHI_Device.h"
#include "../RHI/RHI_PipelineCache.h"
#include "../RHI/RHI_ConstantBuffer.h"
#include "../RHI/RHI_CommandList.h"
#include "../RHI/RHI_Texture2D.h"
#include "../RHI/RHI_SwapChain.h"
#include "../RHI/RHI_VertexBuffer.h"
#include "../RHI/RHI_DescriptorCache.h"
#include "../RHI/RHI_Implementation.h"
#include "../Display/Display.h"
//=========================================

//= NAMESPACES ===============
using namespace std;
using namespace Spartan::Math;
//============================

namespace Spartan
{
    Renderer::Renderer(Context* context) : ISubsystem(context)
    {
        // Options
        m_options |= Render_ReverseZ;
        m_options |= Render_Debug_Transform;
        m_options |= Render_Debug_Grid;
        m_options |= Render_Debug_Lights;
        m_options |= Render_Debug_Physics;
        m_options |= Render_Bloom;
        m_options |= Render_DepthOfField;
        m_options |= Render_VolumetricLighting;
        m_options |= Render_MotionBlur;
        m_options |= Render_Hbao;
        m_options |= Render_ScreenSpaceShadows;
        m_options |= Render_ScreenSpaceReflections;
        m_options |= Render_AntiAliasing_Taa;
        m_options |= Render_Sharpening_LumaSharpen;
        m_options |= Render_FilmGrain;
        m_options |= Render_ChromaticAberration;
        m_options |= Render_Ssgi;

        // Option values
        m_option_values[Option_Value_Anisotropy]        = 16.0f;
        m_option_values[Option_Value_ShadowResolution]  = 2048.0f;
        m_option_values[Option_Value_Tonemapping]       = static_cast<float>(Renderer_ToneMapping_ACES);
        m_option_values[Option_Value_Gamma]             = 2.2f;
        m_option_values[Option_Value_Sharpen_Strength]  = 1.0f;
        m_option_values[Option_Value_Bloom_Intensity]   = 0.1f;
        m_option_values[Option_Value_Fog]               = 0.1f;

        // Subscribe to events
        SUBSCRIBE_TO_EVENT(EventType::WorldResolved,    EVENT_HANDLER_VARIANT(RenderablesAcquire));
        SUBSCRIBE_TO_EVENT(EventType::WorldUnload,      EVENT_HANDLER(ClearEntities));
    }

    Renderer::~Renderer()
    {
        // Unsubscribe from events
        UNSUBSCRIBE_FROM_EVENT(EventType::WorldResolved, EVENT_HANDLER_VARIANT(RenderablesAcquire));

        m_entities.clear();
        m_camera = nullptr;

        // Log to file as the renderer is no more
        LOG_TO_FILE(true);
    }

    bool Renderer::Initialize()
    {
        // Get required systems        
        m_resource_cache    = m_context->GetSubsystem<ResourceCache>();
        m_profiler          = m_context->GetSubsystem<Profiler>();

        // Resolution, viewport and swapchain default to whatever the window size is
        const WindowData& window_data = m_context->m_engine->GetWindowData();

        // Set resolution
        m_resolution.x = window_data.width;
        m_resolution.y = window_data.height;

        // Set viewport
        m_viewport.width    = window_data.width;
        m_viewport.height   = window_data.height;

        // Create device
        m_rhi_device = make_shared<RHI_Device>(m_context);
        if (!m_rhi_device->IsInitialized())
        {
            LOG_ERROR("Failed to create device");
            return false;
        }

        // Create pipeline cache
        m_pipeline_cache = make_shared<RHI_PipelineCache>(m_rhi_device.get());

        // Create descriptor cache
        m_descriptor_cache = make_shared<RHI_DescriptorCache>(m_rhi_device.get());

        // Create swap chain
        {
            m_swap_chain = make_shared<RHI_SwapChain>
            (
                window_data.handle,
                m_rhi_device,
                static_cast<uint32_t>(m_viewport.width),
                static_cast<uint32_t>(m_viewport.height),
                RHI_Format_R8G8B8A8_Unorm,
                m_swap_chain_buffer_count,
                RHI_Present_Immediate | RHI_Swap_Flip_Discard,
                "swapchain_main"
            );

            if (!m_swap_chain->IsInitialized())
            {
                LOG_ERROR("Failed to create swap chain");
                return false;
            }
        }

        // Full-screen quad
        m_viewport_quad = Math::Rectangle(0, 0, m_viewport.width, m_viewport.height);
        m_viewport_quad.CreateBuffers(this);

        // Line buffer
        m_vertex_buffer_lines = make_shared<RHI_VertexBuffer>(m_rhi_device);

        // Editor specific
        m_gizmo_grid = make_unique<Grid>(m_rhi_device);
        m_gizmo_transform = make_unique<Transform_Gizmo>(m_context);

        CreateConstantBuffers();
        CreateShaders();
        CreateDepthStencilStates();
        CreateRasterizerStates();
        CreateBlendStates();
        CreateRenderTextures();
        CreateFonts();    
        CreateSamplers();
        CreateTextures();

        if (!m_initialized)
        {
            // Log on-screen as the renderer is ready
            LOG_TO_FILE(false); 
            m_initialized = true;
        }

        return true;
    }

    std::weak_ptr<Spartan::Entity> Renderer::SnapTransformGizmoTo(const shared_ptr<Entity>& entity) const
    {
        return m_gizmo_transform->SetSelectedEntity(entity);
    }

    void Renderer::Tick(float delta_time)
    {
        if (!m_rhi_device || !m_rhi_device->IsInitialized())
            return;

        // Don't do any work if the swapchain is not presenting
        if (m_swap_chain && !m_swap_chain->PresentEnabled())
            return;

        RHI_CommandList* cmd_list = m_swap_chain->GetCmdList();

        // If there is no camera, clear to black
        if (!m_camera)
        {
            cmd_list->ClearRenderTarget(m_render_targets[RendererRt::Frame_Ldr].get(), 0, 0, false, Vector4(0.0f, 0.0f, 0.0f, 1.0f));
            return;
        }
        
        // If there is not camera but no other entities to render, clear to camera's color
        if (m_entities[Renderer_Object_Opaque].empty() && m_entities[Renderer_Object_Transparent].empty() && m_entities[Renderer_Object_Light].empty())
        {
            cmd_list->ClearRenderTarget(m_render_targets[RendererRt::Frame_Ldr].get(), 0, 0, false, m_camera->GetClearColor());
            return;
        }

        // Reset dynamic buffer indices when the swapchain resets to first buffer/command list
        if (m_swap_chain->GetCmdIndex() == 0)
        {
            m_buffer_uber_offset_index      = 0;
            m_buffer_object_offset_index    = 0;
            m_buffer_frame_offset_index     = 0;
            m_buffer_light_offset_index     = 0;
            m_buffer_material_offset_index  = 0;
        }

        // Update frame buffer
        {
            if (m_update_ortho_proj || m_near_plane != m_camera->GetNearPlane() || m_far_plane != m_camera->GetFarPlane())
            {
                m_buffer_frame_cpu.projection_ortho         = Matrix::CreateOrthographicLH(m_viewport.width, m_viewport.height, m_near_plane, m_far_plane);
                m_buffer_frame_cpu.view_projection_ortho    = Matrix::CreateLookAtLH(Vector3(0, 0, -m_near_plane), Vector3::Forward, Vector3::Up) * m_buffer_frame_cpu.projection_ortho;
                m_update_ortho_proj                         = false;
            }

            m_near_plane                    = m_camera->GetNearPlane();
            m_far_plane                     = m_camera->GetFarPlane();
            m_buffer_frame_cpu.view         = m_camera->GetViewMatrix();
            m_buffer_frame_cpu.projection   = m_camera->GetProjectionMatrix();

            // TAA - Generate jitter
            if (GetOption(Render_AntiAliasing_Taa))
            {
                m_taa_jitter_previous = m_taa_jitter;
                
                const float scale               = 1.0f;
                const uint64_t samples          = 16;
                const uint64_t index            = m_frame_num % samples;
                m_taa_jitter                    = (Utility::Sampling::Halton2D(index, 2, 3) * 2.0f - 1.0f);
                m_taa_jitter.x                  = (m_taa_jitter.x / m_resolution.x) * scale;
                m_taa_jitter.y                  = (m_taa_jitter.y / m_resolution.y) * scale;
                m_buffer_frame_cpu.projection   *= Matrix::CreateTranslation(Vector3(m_taa_jitter.x, m_taa_jitter.y, 0.0f));
            }
            else
            {
                m_taa_jitter            = Vector2::Zero;
                m_taa_jitter_previous   = Vector2::Zero;
            }

            // Update the remaining of the frame buffer
            m_buffer_frame_cpu.view_projection              = m_buffer_frame_cpu.view * m_buffer_frame_cpu.projection;
            m_buffer_frame_cpu.view_projection_inv          = Matrix::Invert(m_buffer_frame_cpu.view_projection);   
            m_buffer_frame_cpu.view_projection_unjittered   = m_buffer_frame_cpu.view * m_camera->GetProjectionMatrix();
            m_buffer_frame_cpu.camera_aperture              = m_camera->GetAperture();
            m_buffer_frame_cpu.camera_shutter_speed         = m_camera->GetShutterSpeed();
            m_buffer_frame_cpu.camera_iso                   = m_camera->GetIso();
            m_buffer_frame_cpu.camera_near                  = m_camera->GetNearPlane();
            m_buffer_frame_cpu.camera_far                   = m_camera->GetFarPlane();
            m_buffer_frame_cpu.camera_position              = m_camera->GetTransform()->GetPosition();
            m_buffer_frame_cpu.camera_direction             = m_camera->GetTransform()->GetForward();
            m_buffer_frame_cpu.bloom_intensity              = m_option_values[Option_Value_Bloom_Intensity];
            m_buffer_frame_cpu.sharpen_strength             = m_option_values[Option_Value_Sharpen_Strength];
            m_buffer_frame_cpu.fog                          = m_option_values[Option_Value_Fog];
            m_buffer_frame_cpu.taa_jitter_offset_previous   = m_buffer_frame_cpu_previous.taa_jitter_offset;
            m_buffer_frame_cpu.taa_jitter_offset            = m_taa_jitter - m_taa_jitter_previous;
            m_buffer_frame_cpu.delta_time                   = static_cast<float>(m_context->GetSubsystem<Timer>()->GetDeltaTimeSmoothedSec());
            m_buffer_frame_cpu.time                         = static_cast<float>(m_context->GetSubsystem<Timer>()->GetTimeSec());
            m_buffer_frame_cpu.tonemapping                  = m_option_values[Option_Value_Tonemapping];
            m_buffer_frame_cpu.gamma                        = m_option_values[Option_Value_Gamma];
            m_buffer_frame_cpu.ssr_enabled                  = GetOption(Render_ScreenSpaceReflections) ? 1.0f : 0.0f;
            m_buffer_frame_cpu.shadow_resolution            = GetOptionValue<float>(Option_Value_ShadowResolution);
            m_buffer_frame_cpu.frame                        = static_cast<uint32_t>(m_frame_num);
        }

        m_is_rendering = true;
        Pass_Main(cmd_list);
        m_is_rendering = false;

        DrawDebugTick(delta_time);

        m_frame_num++;
        m_is_odd_frame = (m_frame_num % 2) == 1;
    }

    void Renderer::SetViewport(float width, float height, float offset_x /*= 0*/, float offset_y /*= 0*/)
    {
        if (m_viewport.width != width || m_viewport.height != height)
        {
            Flush(); // viewport quad might be in use

            m_brdf_specular_lut_rendered = false; // todo, Vulkan needs to re-renderer it, it shouldn't, what am I missing ?

            // Update viewport
            m_viewport.width    = width;
            m_viewport.height   = height;

            // Update full-screen quad
            m_viewport_quad = Math::Rectangle(0, 0, width, height);
            m_viewport_quad.CreateBuffers(this);

            m_update_ortho_proj = true;
        }

        m_viewport_editor_offset.x = offset_x;
        m_viewport_editor_offset.y = offset_y;
    }

    void Renderer::SetResolution(uint32_t width, uint32_t height)
    {
        // Return if resolution is invalid
        if (!m_rhi_device->ValidateResolution(width, height))
        {
            LOG_WARNING("%dx%d is an invalid resolution", width, height);
            return;
        }

        // Make sure we are pixel perfect
        width   -= (width    % 2 != 0) ? 1 : 0;
        height  -= (height    % 2 != 0) ? 1 : 0;

        // Silently return if resolution is already set
        if (m_resolution.x == width && m_resolution.y == height)
            return;

        // Set resolution
        m_resolution.x = static_cast<float>(width);
        m_resolution.y = static_cast<float>(height);

        // Register display mode (in case it doesn't exist)
        const DisplayMode& display_mode = Display::GetActiveDisplayMode();
        Display::SetActiveDisplayMode(DisplayMode(width, height, display_mode.numerator, display_mode.denominator));

        // Re-create render textures
        CreateRenderTextures();

        FIRE_EVENT(EventType::FrameResolutionChanged);

        // Log
        LOG_INFO("Resolution set to %dx%d", width, height);
    }

    template<typename T>
    bool update_dynamic_buffer(RHI_CommandList* cmd_list, RHI_ConstantBuffer* buffer_gpu, T& buffer_cpu, T& buffer_cpu_previous, uint32_t& offset_index)
    {
        // Only update if needed
        if (buffer_cpu == buffer_cpu_previous)
            return true;

        offset_index++;

        // Re-allocate buffer with double size (if needed)
        if (buffer_gpu->IsDynamic())
        {
            if (offset_index >= buffer_gpu->GetOffsetCount())
            {
                cmd_list->Flush();
                const uint32_t new_size = Math::Helper::NextPowerOfTwo(offset_index + 1);
                if (!buffer_gpu->Create<T>(new_size))
                {
                    LOG_ERROR("Failed to re-allocate %s buffer with %d offsets", buffer_gpu->GetName().c_str(), new_size);
                    return false;
                }
                LOG_INFO("Increased %s buffer offsets to %d, that's %d kb", buffer_gpu->GetName().c_str(), new_size, (new_size * buffer_gpu->GetStride()) / 1000);
            }
        }

        // Set new buffer offset
        if (buffer_gpu->IsDynamic())
        {
            buffer_gpu->SetOffsetIndexDynamic(offset_index);
        }

        // Map  
        T* buffer = static_cast<T*>(buffer_gpu->Map());
        if (!buffer)
        {
            LOG_ERROR("Failed to map buffer");
            return false;
        }

        const uint64_t size   = buffer_gpu->GetStride();
        const uint64_t offset = offset_index * size;

        // Update
        if (buffer_gpu->IsDynamic())
        {
            memcpy(reinterpret_cast<std::byte*>(buffer) + offset, reinterpret_cast<std::byte*>(&buffer_cpu), size);
        }
        else
        {
            *buffer = buffer_cpu;
        }
        buffer_cpu_previous = buffer_cpu;

        // Unmap
        return buffer_gpu->Unmap(offset, size);
    }

    bool Renderer::UpdateFrameBuffer(RHI_CommandList* cmd_list)
    {
        // Update directional light intensity, just grab the first one
        for (const auto& entity : m_entities[Renderer_Object_Light])
        {
            if (Light* light = entity->GetComponent<Light>())
            {
                if (light->GetLightType() == LightType::Directional)
                {
                    m_buffer_frame_cpu.directional_light_intensity = light->GetIntensity();
                }
            }
        }

        if (!cmd_list)
        {
            LOG_ERROR("Invalid command list");
            return false;
        }

        if (!update_dynamic_buffer<BufferFrame>(cmd_list, m_buffer_frame_gpu.get(), m_buffer_frame_cpu, m_buffer_frame_cpu_previous, m_buffer_frame_offset_index))
            return false;

        // Dynamic buffers with offsets have to be rebound whenever the offset changes
        return cmd_list->SetConstantBuffer(0, RHI_Shader_Vertex | RHI_Shader_Pixel | RHI_Shader_Compute, m_buffer_frame_gpu);
    }

    bool Renderer::UpdateMaterialBuffer(RHI_CommandList* cmd_list)
    {
        if (!cmd_list)
        {
            LOG_ERROR("Invalid command list");
            return false;
        }

        // Update
        for (uint32_t i = 0; i < m_max_material_instances; i++)
        {
            Material* material = m_material_instances[i];
            if (!material)
                continue;

            m_buffer_material_cpu.mat_clearcoat_clearcoatRough_anis_anisRot[i].x = material->GetProperty(Material_Clearcoat);
            m_buffer_material_cpu.mat_clearcoat_clearcoatRough_anis_anisRot[i].y = material->GetProperty(Material_Clearcoat_Roughness);
            m_buffer_material_cpu.mat_clearcoat_clearcoatRough_anis_anisRot[i].z = material->GetProperty(Material_Anisotropic);
            m_buffer_material_cpu.mat_clearcoat_clearcoatRough_anis_anisRot[i].w = material->GetProperty(Material_Anisotropic_Rotation);
            m_buffer_material_cpu.mat_sheen_sheenTint_pad[i].x = material->GetProperty(Material_Sheen);
            m_buffer_material_cpu.mat_sheen_sheenTint_pad[i].y = material->GetProperty(Material_Sheen_Tint);
        }

        if (!update_dynamic_buffer<BufferMaterial>(cmd_list, m_buffer_material_gpu.get(), m_buffer_material_cpu, m_buffer_material_cpu_previous, m_buffer_material_offset_index))
            return false;

        // Dynamic buffers with offsets have to be rebound whenever the offset changes
        return cmd_list->SetConstantBuffer(1, RHI_Shader_Pixel, m_buffer_material_gpu);
    }

    bool Renderer::UpdateUberBuffer(RHI_CommandList* cmd_list)
    {
        if (!cmd_list)
        {
            LOG_ERROR("Invalid command list");
            return false;
        }

        if (!update_dynamic_buffer<BufferUber>(cmd_list, m_buffer_uber_gpu.get(), m_buffer_uber_cpu, m_buffer_uber_cpu_previous, m_buffer_uber_offset_index))
            return false;

        // Dynamic buffers with offsets have to be rebound whenever the offset changes
        return cmd_list->SetConstantBuffer(2, RHI_Shader_Vertex | RHI_Shader_Pixel | RHI_Shader_Compute, m_buffer_uber_gpu);
    }

    bool Renderer::UpdateObjectBuffer(RHI_CommandList* cmd_list)
    {
        if (!cmd_list)
        {
            LOG_ERROR("Invalid command list");
            return false;
        }

        if (!update_dynamic_buffer<BufferObject>(cmd_list, m_buffer_object_gpu.get(), m_buffer_object_cpu, m_buffer_object_cpu_previous, m_buffer_object_offset_index))
            return false;

        // Dynamic buffers with offsets have to be rebound whenever the offset changes
        return cmd_list->SetConstantBuffer(3, RHI_Shader_Vertex | RHI_Shader_Compute, m_buffer_object_gpu);
    }

    bool Renderer::UpdateLightBuffer(RHI_CommandList* cmd_list, const Light* light)
    {
        if (!cmd_list)
        {
            LOG_ERROR("Invalid command list");
            return false;
        }

        if (!light)
        {
            LOG_ERROR("Invalid light");
            return false;
        }

        for (uint32_t i = 0; i < light->GetShadowArraySize(); i++)
        {
            m_buffer_light_cpu.view_projection[i] = light->GetViewMatrix(i) * light->GetProjectionMatrix(i);
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

        m_buffer_light_cpu.intensity_range_angle_bias   = Vector4(luminous_intensity, light->GetRange(), light->GetAngle(), GetOption(Render_ReverseZ) ? light->GetBias() : -light->GetBias());
        m_buffer_light_cpu.color                        = light->GetColor();
        m_buffer_light_cpu.normal_bias                  = light->GetNormalBias();
        m_buffer_light_cpu.position                     = light->GetTransform()->GetPosition();
        m_buffer_light_cpu.direction                    = light->GetDirection();

        if (!update_dynamic_buffer<BufferLight>(cmd_list, m_buffer_light_gpu.get(), m_buffer_light_cpu, m_buffer_light_cpu_previous, m_buffer_light_offset_index))
            return false;

        // Dynamic buffers with offsets have to be rebound whenever the offset changes
        return cmd_list->SetConstantBuffer(4, RHI_Shader_Pixel, m_buffer_light_gpu);
    }

    void Renderer::RenderablesAcquire(const Variant& entities_variant)
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

            // Get all the components we are interested in
            Renderable* renderable  = entity->GetComponent<Renderable>();
            Light* light            = entity->GetComponent<Light>();
            Camera* camera          = entity->GetComponent<Camera>();

            if (renderable)
            {
                bool is_transparent = false;

                if (const Material* material = renderable->GetMaterial())
                {
                    is_transparent = material->GetColorAlbedo().w < 1.0f;
                }

                m_entities[is_transparent ? Renderer_Object_Transparent : Renderer_Object_Opaque].emplace_back(entity.get());
            }

            if (light)
            {
                m_entities[Renderer_Object_Light].emplace_back(entity.get());
            }

            if (camera)
            {
                m_entities[Renderer_Object_Camera].emplace_back(entity.get());
                m_camera = camera->GetPtrShared<Camera>();
            }
        }

        RenderablesSort(&m_entities[Renderer_Object_Opaque]);
        RenderablesSort(&m_entities[Renderer_Object_Transparent]);
    }

    void Renderer::RenderablesSort(vector<Entity*>* renderables)
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

    void Renderer::ClearEntities()
    {
        m_rhi_device->Queue_WaitAll();

        // light depth buffers might be used by the command list
        if (!m_swap_chain->GetCmdList()->Reset())
        {
            LOG_ERROR("Failed to reset command pool");
            return;
        }

        m_entities.clear();
    }

    const shared_ptr<Spartan::RHI_Texture>& Renderer::GetEnvironmentTexture()
    {
        if (m_render_targets.find(RendererRt::Brdf_Prefiltered_Environment) != m_render_targets.end())
            return m_render_targets[RendererRt::Brdf_Prefiltered_Environment];

        return m_default_tex_white;
    }

    void Renderer::SetEnvironmentTexture(const shared_ptr<RHI_Texture>& texture)
    {
        m_render_targets[RendererRt::Brdf_Prefiltered_Environment] = texture;
    }

    void Renderer::SetOption(Renderer_Option option, bool enable)
    {
        if (enable && !GetOption(option))
        {
            m_options |= option;
        }
        else if (!enable && GetOption(option))
        {
            m_options &= ~option;
        }
        else
        {
            return;
        }
    }

    void Renderer::SetOptionValue(Renderer_Option_Value option, float value)
    {
        if (!m_rhi_device || !m_rhi_device->GetContextRhi())
            return;

        if (option == Option_Value_Anisotropy)
        {
            value = Helper::Clamp(value, 0.0f, 16.0f);
        }
        else if (option == Option_Value_ShadowResolution)
        {
            value = Helper::Clamp(value, static_cast<float>(m_resolution_shadow_min), static_cast<float>(m_rhi_device->GetContextRhi()->rhi_max_texture_dimension_2d));
        }

        if (m_option_values[option] == value)
            return;

        m_option_values[option] = value;

        // Shadow resolution handling
        if (option == Option_Value_ShadowResolution)
        {
            const auto& light_entities = m_entities[Renderer_Object_Light];
            for (const auto& light_entity : light_entities)
            {
                auto light = light_entity->GetComponent<Light>();
                if (light->GetShadowsEnabled())
                {
                    light->CreateShadowMap();
                }
            }
        }
    }

    bool Renderer::Flush()
    {
        if (!m_swap_chain || !m_swap_chain->GetCmdList())
            return false;

        if (!m_swap_chain->GetCmdList()->Flush())
        {
            LOG_ERROR("Failed to flush");
            return false;
        }

        return true;
    }

    uint32_t Renderer::GetMaxResolution() const
    {
        return m_rhi_device->GetContextRhi()->rhi_max_texture_dimension_2d;
    }

    void Renderer::SetGlobalShaderObjectTransform(RHI_CommandList* cmd_list, const Math::Matrix& transform)
    {
        m_buffer_object_cpu.object = transform;
        UpdateObjectBuffer(cmd_list);
    }
}
