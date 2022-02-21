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
#include "../Utilities/Sampling.h"
#include "../Profiling/Profiler.h"
#include "../Resource/ResourceCache.h"
#include "../World/Entity.h"
#include "../World/Components/Transform.h"
#include "../World/Components/Renderable.h"
#include "../World/Components/Camera.h"
#include "../World/Components/Light.h"
#include "../World/Components/ReflectionProbe.h"
#include "../RHI/RHI_Device.h"
#include "../RHI/RHI_PipelineCache.h"
#include "../RHI/RHI_ConstantBuffer.h"
#include "../RHI/RHI_StructuredBuffer.h"
#include "../RHI/RHI_CommandList.h"
#include "../RHI/RHI_Texture2D.h"
#include "../RHI/RHI_SwapChain.h"
#include "../RHI/RHI_VertexBuffer.h"
#include "../RHI/RHI_DescriptorSetLayoutCache.h"
#include "../RHI/RHI_Implementation.h"
#include "../RHI/RHI_Semaphore.h"
#include "../Core/Window.h"
#include "../Input/Input.h"
#include "../World/Components/Environment.h"
//==============================================

//= NAMESPACES ===============
using namespace std;
using namespace Spartan::Math;
//============================

#define RENDER_TARGET(rt_enum) m_render_targets[static_cast<uint8_t>(rt_enum)]

namespace Spartan
{
    Renderer::Renderer(Context* context) : ISubsystem(context)
    {
        // Options
        m_options |= Renderer::Option::ReverseZ;
        m_options |= Renderer::Option::Transform_Handle;
        m_options |= Renderer::Option::Debug_Grid;
        m_options |= Renderer::Option::Debug_ReflectionProbes;
        m_options |= Renderer::Option::Debug_Lights;
        m_options |= Renderer::Option::Debug_Physics;
        m_options |= Renderer::Option::Bloom;
        m_options |= Renderer::Option::VolumetricFog;
        m_options |= Renderer::Option::MotionBlur;
        m_options |= Renderer::Option::Ssao;
        m_options |= Renderer::Option::Ssao_Gi;
        m_options |= Renderer::Option::ScreenSpaceShadows;
        m_options |= Renderer::Option::ScreenSpaceReflections;
        m_options |= Renderer::Option::AntiAliasing_Taa;
        m_options |= Renderer::Option::Sharpening_AMD_FidelityFX_ContrastAdaptiveSharpening;
        m_options |= Renderer::Option::DepthOfField;
        m_options |= Renderer::Option::Debanding;
        //m_options |= Render_DepthPrepass; // todo: fix for vulkan

        // Option values.
        m_option_values[Renderer::OptionValue::Anisotropy]       = 16.0f;
        m_option_values[Renderer::OptionValue::ShadowResolution] = 2048.0f;
        m_option_values[Renderer::OptionValue::Tonemapping]      = static_cast<float>(Tonemapping::Renderer_ToneMapping_Off);
        m_option_values[Renderer::OptionValue::Gamma]            = 1.5f;
        m_option_values[Renderer::OptionValue::Sharpen_Strength] = 1.0f;
        m_option_values[Renderer::OptionValue::Bloom_Intensity]  = 0.2f;
        m_option_values[Renderer::OptionValue::Fog]              = 0.08f;

        // Subscribe to events.
        SP_SUBSCRIBE_TO_EVENT(EventType::WorldResolved,             SP_EVENT_HANDLER_VARIANT(OnRenderablesAcquire));
        SP_SUBSCRIBE_TO_EVENT(EventType::WorldPreClear,             SP_EVENT_HANDLER(OnClear));
        SP_SUBSCRIBE_TO_EVENT(EventType::WorldLoadEnd,              SP_EVENT_HANDLER(OnWorldLoaded));
        SP_SUBSCRIBE_TO_EVENT(EventType::WindowOnFullScreenToggled, SP_EVENT_HANDLER(OnFullScreenToggled));

        // Get thread id.
        m_render_thread_id = this_thread::get_id();

        m_material_instances.fill(nullptr);
    }

    Renderer::~Renderer()
    {
        // Unsubscribe from events
        SP_UNSUBSCRIBE_FROM_EVENT(EventType::WorldResolved,             SP_EVENT_HANDLER_VARIANT(OnRenderablesAcquire));
        SP_UNSUBSCRIBE_FROM_EVENT(EventType::WorldPreClear,             SP_EVENT_HANDLER(OnClear));
        SP_UNSUBSCRIBE_FROM_EVENT(EventType::WorldLoadEnd,              SP_EVENT_HANDLER(OnWorldLoaded));
        SP_UNSUBSCRIBE_FROM_EVENT(EventType::WindowOnFullScreenToggled, SP_EVENT_HANDLER(OnFullScreenToggled));

        // Log to file as the renderer is no more
        LOG_TO_FILE(true);
    }

    bool Renderer::OnInitialise()
    {
        m_initialised = false;

        // Get window subsystem (required in order to know a windows size and also create a swapchain for it).
        Window* window = m_context->GetSubsystem<Window>();
        if (!window)
        {
            LOG_ERROR("The Renderer subsystem requires a Window subsystem.");
            return false;
        }

        // Get resource cache subsystem (required in order to know from which paths to load shaders, textures and fonts).
        m_resource_cache = m_context->GetSubsystem<ResourceCache>();
        if (!m_resource_cache)
        {
            LOG_ERROR("The Renderer subsystem requires a ResourceCache subsystem.");
            return false;
        }

        // Get profiler subsystem (used to profile things but not required)
        m_profiler = m_context->GetSubsystem<Profiler>();

        // Create device
        m_rhi_device = make_shared<RHI_Device>(m_context);
        if (!m_rhi_device->IsInitialised())
        {
            LOG_ERROR("Failed to create device.");
            return false;
        }

        // Create pipeline cache
        m_pipeline_cache = make_shared<RHI_PipelineCache>(m_rhi_device.get());

        // Create descriptor set layout cache
        m_descriptor_set_layout_cache = make_shared<RHI_DescriptorSetLayoutCache>(m_rhi_device.get());

        // Create command lists
        for (uint32_t i = 0; i < m_swap_chain_buffer_count; i++)
        {
            m_cmd_lists.emplace_back(make_shared<RHI_CommandList>(m_context));
        }

        // Line buffer
        m_vertex_buffer_lines = make_shared<RHI_VertexBuffer>(m_rhi_device);

        // World grid
        m_gizmo_grid = make_unique<Grid>(m_rhi_device);

        // Get window size
        uint32_t window_width  = window->GetWidth();
        uint32_t window_height = window->GetHeight();

        // Create swap chain
        {
            m_swap_chain = make_shared<RHI_SwapChain>
            (
                window->GetHandle(),
                m_rhi_device,
                window_width,
                window_height,
                RHI_Format_R8G8B8A8_Unorm,
                m_swap_chain_buffer_count,
                RHI_Present_Immediate | RHI_Swap_Flip_Discard,
                "swapchain_renderer"
             );

            if (!m_swap_chain->IsInitialised())
            {
                LOG_ERROR("Failed to create swap chain.");
                return false;
            }
        }

        // Full-screen quad
        m_viewport_quad = Math::Rectangle(0, 0, static_cast<float>(window_width), static_cast<float>(window_height));
        m_viewport_quad.CreateBuffers(this);

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

        // Log on-screen as the renderer is ready
        LOG_TO_FILE(false);

        m_initialised = true;
        return true;
    }

    void Renderer::OnTick(double delta_time)
    {
        SP_ASSERT(m_rhi_device != nullptr);
        SP_ASSERT(m_rhi_device->IsInitialised());
        SP_ASSERT(m_swap_chain != nullptr);

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

        // Acquire appropriate command list
        m_cmd_index     = (m_cmd_index + 1) % static_cast<uint32_t>(m_cmd_lists.size());
        m_cmd_current   = m_cmd_index < static_cast<uint32_t>(m_cmd_lists.size()) ? m_cmd_lists[m_cmd_index].get() : nullptr;

        // Reset dynamic buffer indices when we come back to the first command list
        if (m_cmd_index == 0)
        {
            m_cb_uber_offset_index     = 0;
            m_cb_frame_offset_index    = 0;
            m_cb_light_offset_index    = 0;
            m_cb_material_offset_index = 0;
        }

        // Begin
        m_cmd_current->Begin();

        // If there is no camera, clear to black
        if (!m_camera)
        {
            m_cmd_current->ClearRenderTarget(RENDER_TARGET(RenderTarget::Frame_Output).get(), 0, 0, false, Vector4(0.0f, 0.0f, 0.0f, 1.0f));
            return;
        }

        // If there is not camera but no other entities to render, clear to camera's color
        if (m_entities[ObjectType::GeometryOpaque].empty() && m_entities[ObjectType::GeometryTransparent].empty() && m_entities[ObjectType::Light].empty())
        {
            m_cmd_current->ClearRenderTarget(RENDER_TARGET(RenderTarget::Frame_Output).get(), 0, 0, false, m_camera->GetClearColor());
            return;
        }

        // Update viewport
        if (m_dirty_viewport)
        {
            // Update viewport
            m_viewport.width  = m_viewport_size_pending.x;
            m_viewport.height = m_viewport_size_pending.y;

            // Update quad
            m_viewport_quad = Math::Rectangle(0, 0, m_viewport.width, m_viewport.height);
            m_viewport_quad.CreateBuffers(this);

            // Update orthographic projection
            m_dirty_orthographic_projection = true;

            m_dirty_viewport = false;
        }

        // Update frame buffer
        {
            // Matrices
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

            // TAA - Generate jitter
            if (GetOption(Renderer::Option::AntiAliasing_Taa))
            {
                m_taa_jitter_previous = m_taa_jitter;
                
                const uint8_t samples     = 16;
                const uint8_t index       = m_frame_num % samples;
                m_taa_jitter              = Utility::Sampling::Halton2D(index, 2, 3) * 2.0f - 1.0f;
                m_taa_jitter.x            = (m_taa_jitter.x / m_resolution_render.x);
                m_taa_jitter.y            = (m_taa_jitter.y / m_resolution_render.y);
                m_cb_frame_cpu.projection *= Matrix::CreateTranslation(Vector3(m_taa_jitter.x, m_taa_jitter.y, 0.0f));
            }
            else
            {
                m_taa_jitter          = Vector2::Zero;
                m_taa_jitter_previous = Vector2::Zero;
            }
            
            // Update the remaining of the frame buffer
            m_cb_frame_cpu.view_projection_previous   = m_cb_frame_cpu.view_projection;
            m_cb_frame_cpu.view_projection            = m_cb_frame_cpu.view * m_cb_frame_cpu.projection;
            m_cb_frame_cpu.view_projection_inv        = Matrix::Invert(m_cb_frame_cpu.view_projection);
            m_cb_frame_cpu.view_projection_unjittered = m_cb_frame_cpu.view * m_camera->GetProjectionMatrix();
            m_cb_frame_cpu.camera_aperture            = m_camera->GetAperture();
            m_cb_frame_cpu.camera_shutter_speed       = m_camera->GetShutterSpeed();
            m_cb_frame_cpu.camera_iso                 = m_camera->GetIso();
            m_cb_frame_cpu.camera_near                = m_camera->GetNearPlane();
            m_cb_frame_cpu.camera_far                 = m_camera->GetFarPlane();
            m_cb_frame_cpu.camera_position            = m_camera->GetTransform()->GetPosition();
            m_cb_frame_cpu.camera_direction           = m_camera->GetTransform()->GetForward();
            m_cb_frame_cpu.resolution_output          = m_resolution_output;
            m_cb_frame_cpu.resolution_render          = m_resolution_render;
            m_cb_frame_cpu.taa_jitter_current         = m_taa_jitter;
            m_cb_frame_cpu.taa_jitter_previous        = m_taa_jitter_previous;
            m_cb_frame_cpu.delta_time                 = static_cast<float>(m_context->GetSubsystem<Timer>()->GetDeltaTimeSmoothedSec());
            m_cb_frame_cpu.time                       = static_cast<float>(m_context->GetSubsystem<Timer>()->GetTimeSec());
            m_cb_frame_cpu.bloom_intensity            = GetOptionValue<float>(Renderer::OptionValue::Bloom_Intensity);
            m_cb_frame_cpu.sharpen_strength           = GetOptionValue<float>(Renderer::OptionValue::Sharpen_Strength);
            m_cb_frame_cpu.fog                        = GetOptionValue<float>(Renderer::OptionValue::Fog);
            m_cb_frame_cpu.tonemapping                = GetOptionValue<float>(Renderer::OptionValue::Tonemapping);
            m_cb_frame_cpu.gamma                      = GetOptionValue<float>(Renderer::OptionValue::Gamma);
            m_cb_frame_cpu.shadow_resolution          = GetOptionValue<float>(Renderer::OptionValue::ShadowResolution);
            m_cb_frame_cpu.frame                      = static_cast<uint32_t>(m_frame_num);
            m_cb_frame_cpu.frame_mip_count            = RENDER_TARGET(RenderTarget::Frame_Render)->GetMipCount();
            m_cb_frame_cpu.ssr_mip_count              = RENDER_TARGET(RenderTarget::Ssr)->GetMipCount();
            m_cb_frame_cpu.resolution_environment     = Vector2(GetEnvironmentTexture()->GetWidth(), GetEnvironmentTexture()->GetHeight());

            // These must match what Common_Buffer.hlsl is reading
            m_cb_frame_cpu.set_bit(GetOption(Renderer::Option::ScreenSpaceReflections), 1 << 0);
            m_cb_frame_cpu.set_bit(GetOption(Renderer::Option::Upsample_TAA),           1 << 1);
            m_cb_frame_cpu.set_bit(GetOption(Renderer::Option::Ssao),                   1 << 2);
            m_cb_frame_cpu.set_bit(GetOption(Renderer::Option::VolumetricFog),          1 << 3);
            m_cb_frame_cpu.set_bit(GetOption(Renderer::Option::ScreenSpaceShadows),     1 << 4);
            m_cb_frame_cpu.set_bit(GetOption(Renderer::Option::Ssao_Gi),                1 << 5);
        }

        Lines_PreMain();
        Pass_Main(m_cmd_current);
        Lines_PostMain(delta_time);

        m_frame_num++;
        m_is_odd_frame = (m_frame_num % 2) == 1;
    }
    
    void Renderer::SetViewport(float width, float height)
    {
        if (m_viewport.width != width || m_viewport.height != height)
        {
            m_viewport_size_pending.x = width;
            m_viewport_size_pending.y = height;

            m_dirty_viewport = true;
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

        // Make sure we are pixel perfect
        width  -= (width   % 2 != 0) ? 1 : 0;
        height -= (height  % 2 != 0) ? 1 : 0;

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

        // Make sure we are pixel perfect
        width  -= (width % 2 != 0) ? 1 : 0;
        height -= (height % 2 != 0) ? 1 : 0;

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

    template<typename T>
    bool update_dynamic_buffer(RHI_CommandList* cmd_list, RHI_ConstantBuffer* buffer_gpu, T& buffer_cpu, T& buffer_cpu_previous, uint32_t& offset_index)
    {
        SP_ASSERT(cmd_list != nullptr);

        // Only update if needed
        if (buffer_cpu == buffer_cpu_previous)
            return true;

        offset_index++;

        // Re-allocate buffer with double size (if needed)
        if (buffer_gpu->IsDynamic())
        {
            if (offset_index >= buffer_gpu->GetOffsetCount())
            {
                cmd_list->Flush(true);
                const uint32_t new_size = Math::Helper::NextPowerOfTwo(offset_index + 1);
                if (!buffer_gpu->Create<T>(new_size))
                {
                    LOG_ERROR("Failed to re-allocate %s buffer with %d offsets", buffer_gpu->GetObjectName().c_str(), new_size);
                    return false;
                }
                LOG_INFO("Increased %s buffer offsets to %d, that's %d kb", buffer_gpu->GetObjectName().c_str(), new_size, (new_size * buffer_gpu->GetStride()) / 1000);
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

    bool Renderer::Update_Cb_Frame(RHI_CommandList* cmd_list)
    {
        SP_ASSERT(cmd_list != nullptr);

        // Update directional light intensity, just grab the first one
        for (const auto& entity : m_entities[ObjectType::Light])
        {
            if (Light* light = entity->GetComponent<Light>())
            {
                if (light->GetLightType() == LightType::Directional)
                {
                    m_cb_frame_cpu.directional_light_intensity = light->GetIntensity();
                }
            }
        }

        if (!update_dynamic_buffer<Cb_Frame>(cmd_list, m_cb_frame_gpu.get(), m_cb_frame_cpu, m_cb_frame_cpu_previous, m_cb_frame_offset_index))
            return false;

        // Dynamic buffers with offsets have to be rebound whenever the offset changes
        cmd_list->SetConstantBuffer(Renderer::Bindings_Cb::frame, RHI_Shader_Vertex | RHI_Shader_Pixel | RHI_Shader_Compute, m_cb_frame_gpu);

        return true;
    }

    bool Renderer::Update_Cb_Uber(RHI_CommandList* cmd_list)
    {
        SP_ASSERT(cmd_list != nullptr);

        if (!update_dynamic_buffer<Cb_Uber>(cmd_list, m_cb_uber_gpu.get(), m_cb_uber_cpu, m_cb_uber_cpu_previous, m_cb_uber_offset_index))
            return false;

        // Dynamic buffers with offsets have to be rebound whenever the offset changes
        cmd_list->SetConstantBuffer(Renderer::Bindings_Cb::uber, RHI_Shader_Vertex | RHI_Shader_Pixel | RHI_Shader_Compute, m_cb_uber_gpu);

        return true;
    }

    bool Renderer::Update_Cb_Light(RHI_CommandList* cmd_list, const Light* light, const RHI_Shader_Type scope)
    {
        SP_ASSERT(cmd_list != nullptr);

        if (!light)
        {
            LOG_ERROR("Invalid light");
            return false;
        }

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
            luminous_intensity /= Math::Helper::PI; // lumen s to candelas
            luminous_intensity *= 255.0f; // this is a hack, must fix whats my color units
        }

        m_cb_light_cpu.intensity_range_angle_bias = Vector4(luminous_intensity, light->GetRange(), light->GetAngle(), GetOption(Renderer::Option::ReverseZ) ? light->GetBias() : -light->GetBias());
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

        if (!update_dynamic_buffer<Cb_Light>(cmd_list, m_cb_light_gpu.get(), m_cb_light_cpu, m_cb_light_cpu_previous, m_cb_light_offset_index))
            return false;

        // Dynamic buffers with offsets have to be rebound whenever the offset changes
        cmd_list->SetConstantBuffer(Renderer::Bindings_Cb::light, scope, m_cb_light_gpu);

        return true;
    }

    bool Renderer::Update_Cb_Material(RHI_CommandList* cmd_list)
    {
        SP_ASSERT(cmd_list != nullptr);

        // Update
        for (uint32_t i = 0; i < m_max_material_instances; i++)
        {
            Material* material = m_material_instances[i];
            if (!material)
                continue;

            m_cb_material_cpu.mat_clearcoat_clearcoatRough_anis_anisRot[i].x = material->GetProperty(Material_Clearcoat);
            m_cb_material_cpu.mat_clearcoat_clearcoatRough_anis_anisRot[i].y = material->GetProperty(Material_Clearcoat_Roughness);
            m_cb_material_cpu.mat_clearcoat_clearcoatRough_anis_anisRot[i].z = material->GetProperty(Material_Anisotropic);
            m_cb_material_cpu.mat_clearcoat_clearcoatRough_anis_anisRot[i].w = material->GetProperty(Material_Anisotropic_Rotation);
            m_cb_material_cpu.mat_sheen_sheenTint_pad[i].x = material->GetProperty(Material_Sheen);
            m_cb_material_cpu.mat_sheen_sheenTint_pad[i].y = material->GetProperty(Material_Sheen_Tint);
        }

        if (!update_dynamic_buffer<Cb_Material>(cmd_list, m_cb_material_gpu.get(), m_cb_material_cpu, m_cb_material_cpu_previous, m_cb_material_offset_index))
            return false;

        // Dynamic buffers with offsets have to be rebound whenever the offset changes
        cmd_list->SetConstantBuffer(Renderer::Bindings_Cb::material, RHI_Shader_Pixel, m_cb_material_gpu);

        return true;
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
                    is_transparent = material->GetColorAlbedo().w < 1.0f;
                    is_visible     = material->GetColorAlbedo().w != 0.0f;
                }

                if (is_visible)
                {
                    m_entities[is_transparent ? ObjectType::GeometryTransparent : ObjectType::GeometryOpaque].emplace_back(entity.get());
                }
            }

            if (Light* light = entity->GetComponent<Light>())
            {
                m_entities[ObjectType::Light].emplace_back(entity.get());
            }

            if (Camera* camera = entity->GetComponent<Camera>())
            {
                m_entities[ObjectType::Camera].emplace_back(entity.get());
                m_camera = camera->GetPtrShared<Camera>();
            }

            if (ReflectionProbe* reflection_probe = entity->GetComponent<ReflectionProbe>())
            {
                m_entities[ObjectType::ReflectionProbe].emplace_back(entity.get());
            }

            if (Environment* environment = entity->GetComponent<Environment>())
            {
                m_entities[ObjectType::Environment].emplace_back(entity.get());
            }
        }

        SortRenderables(&m_entities[ObjectType::GeometryOpaque]);
        SortRenderables(&m_entities[ObjectType::GeometryTransparent]);
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

    const shared_ptr<RHI_Texture> Renderer::GetEnvironmentTexture()
    {
        if (Entity* entity = m_entities[ObjectType::Environment].front())
        {
            if (Environment* environment = entity->GetComponent<Environment>())
            {
                if (environment->GetTexture())
                {
                    return environment->GetTexture();
                }
            }
        }

        return m_tex_default_transparent;
    }

    void Renderer::SetOption(Renderer::Option option, bool enable)
    {
        bool toggled = false;

        if (enable && !GetOption(option))
        {
            m_options |= option;
            toggled   = true;
        }
        else if (!enable && GetOption(option))
        {
            m_options &= ~option;
            toggled   = true;
        }

        if (!toggled)
            return;

        if (option == Renderer::Option::Upsample_TAA || option == Renderer::Option::Upsample_AMD_FidelityFX_SuperResolution)
        {
            CreateRenderTextures(false, false, false, true);
        }

        if (option == Renderer::Option::ReverseZ)
        {
            CreateDepthStencilStates();

            if (m_camera)
            {
                m_camera->MakeDirty();
            }
        }
    }

    void Renderer::SetOptionValue(Renderer::OptionValue option, float value)
    {
        if (!m_rhi_device || !m_rhi_device->GetContextRhi())
            return;

        if (option == Renderer::OptionValue::Anisotropy)
        {
            value = Helper::Clamp(value, 0.0f, 16.0f);
        }
        else if (option == Renderer::OptionValue::ShadowResolution)
        {
            value = Helper::Clamp(value, static_cast<float>(m_resolution_shadow_min), static_cast<float>(m_rhi_device->GetMaxTexture2dDimension()));
        }

        if (m_option_values[option] == value)
            return;

        m_option_values[option] = value;

        // Shadow resolution handling
        if (option == Renderer::OptionValue::ShadowResolution)
        {
            const auto& light_entities = m_entities[ObjectType::Light];
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

    bool Renderer::Present(RHI_CommandList* cmd_list)
    {
        // Finalise command list
        if (cmd_list->GetState() == RHI_CommandListState::Recording)
        {
            cmd_list->End();
            cmd_list->Submit(m_swap_chain->GetImageAcquiredSemaphore());
        }

        if (!m_swap_chain->PresentEnabled())
            return false;

        // Wait semaphore (null for D3D11)
        RHI_Semaphore* wait_semaphore = cmd_list->GetProcessedSemaphore();
        if (wait_semaphore)
        {
            wait_semaphore = wait_semaphore->GetState() == RHI_Semaphore_State::Signaled ? wait_semaphore : nullptr;
        }

        return m_swap_chain->Present(wait_semaphore);
    }

	void Renderer::Flush()
    {
        // The external thread requests a flush from the renderer thread (to avoid a myriad of thread issues and Vulkan errors)
        bool flushing_from_different_thread = m_render_thread_id != this_thread::get_id();
        if (flushing_from_different_thread)
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
                if (!m_cmd_current->Flush(false))
                {
                    LOG_ERROR("Failed to flush command list");
                }
            }
        }

        m_flush_requested = false;
    }

    void Renderer::SetCbUberTransform(RHI_CommandList* cmd_list, const Matrix& transform)
    {
        m_cb_uber_cpu.transform = transform;
        Update_Cb_Uber(cmd_list);
    }

    void Renderer::SetCbUberTextureVisualisationOptions(RHI_CommandList* cmd_list, const uint32_t options)
    {
        m_cb_uber_cpu.options_texture_visualisation = options;
        Update_Cb_Uber(cmd_list);
    }

    RHI_Api_Type Renderer::GetApiType() const
    {
        return m_rhi_device->GetContextRhi()->api_type;
    }
    
    void Renderer::RequestTextureMipGeneration(RHI_Texture* texture)
    {
        SP_ASSERT(texture != nullptr);

        // Ensure the texture requires mips
        SP_ASSERT(texture->HasMips());

        // Ensure that the texture has per mip views since they are required for GPU downsampling.
        SP_ASSERT(texture->HasPerMipViews());

        // Ensure the texture hasn't already been added to the vector
        for (const RHI_Texture* texture_it : m_textures_mip_generation)
        {
            SP_ASSERT(texture_it->GetObjectId() != texture->GetObjectId());
        }

        // Wait
        while (m_is_generating_mips)
        {
            LOG_INFO("Waiting for the renderer to finish the currently active mip generation pass...");
            this_thread::sleep_for(chrono::milliseconds(16));
        }

        m_textures_mip_generation.push_back(texture);
    }
}
