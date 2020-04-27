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
#include "Renderer.h"
#include "Model.h"
#include "ShaderGBuffer.h"
#include "Font/Font.h"
#include "Gizmos/Grid.h"
#include "Gizmos/Transform_Gizmo.h"
#include "../Utilities/Sampling.h"
#include "../Profiling/Profiler.h"
#include "../Resource/ResourceCache.h"
#include "../Core/Engine.h"
#include "../Core/Timer.h"
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
#include "../RHI/RHI_Implementation.h"
#include "../RHI/RHI_DescriptorCache.h"
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
        //m_options |= Render_DepthPrepass;
        m_options |= Render_Debug_Transform;
        //m_options |= Render_Debug_SelectionOutline;
        m_options |= Render_Debug_Grid;
        m_options |= Render_Debug_Lights;
        m_options |= Render_Debug_Physics;
        m_options |= Render_Bloom;
        m_options |= Render_VolumetricLighting;
        m_options |= Render_MotionBlur;
        m_options |= Render_ScreenSpaceAmbientOcclusion;
        m_options |= Render_ScreenSpaceShadows;
        m_options |= Render_ScreenSpaceReflections;	
        m_options |= Render_AntiAliasing_Taa;
        m_options |= Render_Sharpening_LumaSharpen;             // Helps with TAA induced blurring
        //m_options |= Render_PostProcess_FXAA;                 // Disabled by default: TAA is superior.
        //m_options |= Render_PostProcess_Dithering;            // Disabled by default: It's only needed in very dark scenes to fix smooth color gradients.
        //m_options |= Render_PostProcess_ChromaticAberration;	// Disabled by default: It doesn't improve the image quality, it's more of a stylistic effect.	

        // Option values
        m_option_values[Option_Value_Anisotropy]              = 16.0f;
        m_option_values[Option_Value_ShadowResolution]        = 4098.0f;
        m_option_values[Option_Value_Tonemapping]             = static_cast<float>(Renderer_ToneMapping_ACES);
        m_option_values[Option_Value_Exposure]                = 0.0f;
        m_option_values[Option_Value_Gamma]                   = 2.2f;
        m_option_values[Option_Value_Sharpen_Strength]        = 1.0f;
        m_option_values[Option_Value_Sharpen_Clamp]           = 0.35f;
        m_option_values[Option_Value_Bloom_Intensity]         = 0.3f;
        m_option_values[Option_Value_Motion_Blur_Intensity]   = 0.01f;

		// Subscribe to events
		SUBSCRIBE_TO_EVENT(Event_World_Resolve_Complete,    EVENT_HANDLER_VARIANT(RenderablesAcquire));
        SUBSCRIBE_TO_EVENT(Event_World_Unload,              EVENT_HANDLER(ClearEntities));
	}

	Renderer::~Renderer()
	{
		// Unsubscribe from events
		UNSUBSCRIBE_FROM_EVENT(Event_World_Resolve_Complete, EVENT_HANDLER_VARIANT(RenderablesAcquire));

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
            const WindowData& window_data = m_context->m_engine->GetWindowData();

            m_swap_chain = make_shared<RHI_SwapChain>
            (
                window_data.handle,
                m_rhi_device,
                static_cast<uint32_t>(window_data.width),
                static_cast<uint32_t>(window_data.height),
                RHI_Format_R8G8B8A8_Unorm,
                2,
                RHI_Present_Immediate | RHI_Swap_Flip_Discard
            );

            if (!m_swap_chain->IsInitialized())
            {
                LOG_ERROR("Failed to create swap chain");
                return false;
            }
        }

		// Editor specific
		m_gizmo_grid		= make_unique<Grid>(m_rhi_device);
		m_gizmo_transform	= make_unique<Transform_Gizmo>(m_context);

		// Line buffer
		m_vertex_buffer_lines = make_shared<RHI_VertexBuffer>(m_rhi_device);

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

        RHI_CommandList* cmd_list = m_swap_chain->GetCmdList();

		// If there is no camera, do nothing
		if (!m_camera)
		{
            //cmd_list->ClearRenderTarget(m_render_targets[RenderTarget_Composition_Ldr]->GetResource_RenderTarget(), Vector4(0.0f, 0.0f, 0.0f, 1.0f));
			return;
		}

		// If there is nothing to render clear to camera's color and present
		if (m_entities.empty())
		{
            //cmd_list->ClearRenderTarget(m_render_targets[RenderTarget_Composition_Ldr]->GetResource_RenderTarget(), m_camera->GetClearColor());
			return;
		}

		m_frame_num++;
		m_is_odd_frame = (m_frame_num % 2) == 1;

		// Get camera matrices
		{
            if (m_update_ortho_proj || m_near_plane != m_camera->GetNearPlane() || m_far_plane != m_camera->GetFarPlane())
            {
                m_buffer_frame_cpu.projection_ortho         = Matrix::CreateOrthographicLH(m_resolution.x, m_resolution.y, m_near_plane, m_far_plane);
                m_buffer_frame_cpu.view_projection_ortho    = Matrix::CreateLookAtLH(Vector3(0, 0, -m_near_plane), Vector3::Forward, Vector3::Up) * m_buffer_frame_cpu.projection_ortho;
                m_update_ortho_proj                         = false;
            }
            m_near_plane	                            = m_camera->GetNearPlane();
            m_far_plane		                            = m_camera->GetFarPlane();
            m_buffer_frame_cpu.view			            = m_camera->GetViewMatrix();
            m_buffer_frame_cpu.projection	            = m_camera->GetProjectionMatrix();

			// TAA - Generate jitter
			if (GetOption(Render_AntiAliasing_Taa))
			{
				m_taa_jitter_previous = m_taa_jitter;

                const float scale               = 1.0f;
				const uint64_t samples	        = 16;
				const uint64_t index	        = m_frame_num % samples;
				m_taa_jitter			        = (Utility::Sampling::Halton2D(index, 2, 3) * 2.0f - 1.0f);
				m_taa_jitter.x			        = (m_taa_jitter.x / m_resolution.x) * scale;
				m_taa_jitter.y			        = (m_taa_jitter.y / m_resolution.y) * scale;
                m_buffer_frame_cpu.projection   *= Matrix::CreateTranslation(Vector3(m_taa_jitter.x, m_taa_jitter.y, 0.0f));
			}
			else
			{
				m_taa_jitter			= Vector2::Zero;
				m_taa_jitter_previous	= Vector2::Zero;		
			}

            // Compute some TAA affected matrices
            m_buffer_frame_cpu.view_projection              = m_buffer_frame_cpu.view * m_buffer_frame_cpu.projection;
            m_buffer_frame_cpu.view_projection_inv          = Matrix::Invert(m_buffer_frame_cpu.view_projection);   
            m_buffer_frame_cpu.view_projection_unjittered   = m_buffer_frame_cpu.view * m_camera->GetProjectionMatrix();
		}

		m_is_rendering = true;
		Pass_Main(cmd_list);
		m_is_rendering = false;
	}

	void Renderer::SetResolution(uint32_t width, uint32_t height)
	{
		// Return if resolution is invalid
        const uint32_t max_res = m_rhi_device->GetContextRhi()->max_texture_dimension_2d;
		if (width == 0 || width > max_res || height == 0 || height > max_res)
		{
			LOG_WARNING("%dx%d is an invalid resolution", width, height);
			return;
		}

		// Make sure we are pixel perfect
		width	-= (width	% 2 != 0) ? 1 : 0;
		height	-= (height	% 2 != 0) ? 1 : 0;

        // Silently return if resolution is already set
        if (m_resolution.x == width && m_resolution.y == height)
            return;

		// Set resolution
		m_resolution.x = static_cast<float>(width);
		m_resolution.y = static_cast<float>(height);

		// Re-create render textures
		CreateRenderTextures();

        m_update_ortho_proj = true;

        FIRE_EVENT(Event_Frame_Resolution_Changed);

		// Log
		LOG_INFO("Resolution set to %dx%d", width, height);
	}

	void Renderer::DrawLine(const Vector3& from, const Vector3& to, const Vector4& color_from, const Vector4& color_to, const bool depth /*= true*/)
	{
		if (depth)
		{
			m_lines_list_depth_enabled.emplace_back(from, color_from);
			m_lines_list_depth_enabled.emplace_back(to, color_to);
		}
		else
		{
			m_lines_list_depth_disabled.emplace_back(from, color_from);
			m_lines_list_depth_disabled.emplace_back(to, color_to);
		}
	}

	void Renderer::DrawRectangle(const Math::Rectangle& rectangle, const Math::Vector4& color /*= DebugColor*/, bool depth /*= true*/)
	{
        const float cam_z = m_camera->GetTransform()->GetPosition().z + m_camera->GetNearPlane() + 5.0f;

        DrawLine(Vector3(rectangle.left,    rectangle.top,      cam_z), Vector3(rectangle.right,    rectangle.top,      cam_z), color, color, depth);
        DrawLine(Vector3(rectangle.right,   rectangle.top,      cam_z), Vector3(rectangle.right,    rectangle.bottom,   cam_z), color, color, depth);
        DrawLine(Vector3(rectangle.right,   rectangle.bottom,   cam_z), Vector3(rectangle.left,     rectangle.bottom,   cam_z), color, color, depth);
        DrawLine(Vector3(rectangle.left,    rectangle.bottom,   cam_z), Vector3(rectangle.left,     rectangle.top,      cam_z), color, color, depth);
	}

	void Renderer::DrawBox(const BoundingBox& box, const Vector4& color, const bool depth /*= true*/)
	{
		const auto& min = box.GetMin();
		const auto& max = box.GetMax();
	
		DrawLine(Vector3(min.x, min.y, min.z), Vector3(max.x, min.y, min.z), color, color, depth);
        DrawLine(Vector3(max.x, min.y, min.z), Vector3(max.x, max.y, min.z), color, color, depth);
        DrawLine(Vector3(max.x, max.y, min.z), Vector3(min.x, max.y, min.z), color, color, depth);
        DrawLine(Vector3(min.x, max.y, min.z), Vector3(min.x, min.y, min.z), color, color, depth);
        DrawLine(Vector3(min.x, min.y, min.z), Vector3(min.x, min.y, max.z), color, color, depth);
        DrawLine(Vector3(max.x, min.y, min.z), Vector3(max.x, min.y, max.z), color, color, depth);
        DrawLine(Vector3(max.x, max.y, min.z), Vector3(max.x, max.y, max.z), color, color, depth);
        DrawLine(Vector3(min.x, max.y, min.z), Vector3(min.x, max.y, max.z), color, color, depth);
        DrawLine(Vector3(min.x, min.y, max.z), Vector3(max.x, min.y, max.z), color, color, depth);
        DrawLine(Vector3(max.x, min.y, max.z), Vector3(max.x, max.y, max.z), color, color, depth);
        DrawLine(Vector3(max.x, max.y, max.z), Vector3(min.x, max.y, max.z), color, color, depth);
        DrawLine(Vector3(min.x, max.y, max.z), Vector3(min.x, min.y, max.z), color, color, depth);
	}

    bool Renderer::UpdateFrameBuffer()
    {
        // Map
        BufferFrame* buffer = static_cast<BufferFrame*>(m_buffer_frame_gpu->Map());
        if (!buffer)
        {
            LOG_ERROR("Failed to map buffer");
            return false;
        }

        // Struct is updated automatically here as per frame data are (by definition) known ahead of time
        m_buffer_frame_cpu.camera_near                  = m_camera->GetNearPlane();
        m_buffer_frame_cpu.camera_far                   = m_camera->GetFarPlane();
        m_buffer_frame_cpu.camera_position              = m_camera->GetTransform()->GetPosition();
        m_buffer_frame_cpu.camera_direction             = m_camera->GetTransform()->GetForward();
        m_buffer_frame_cpu.bloom_intensity              = m_option_values[Option_Value_Bloom_Intensity];
        m_buffer_frame_cpu.sharpen_strength             = m_option_values[Option_Value_Sharpen_Strength];
        m_buffer_frame_cpu.sharpen_clamp                = m_option_values[Option_Value_Sharpen_Clamp];
        m_buffer_frame_cpu.taa_jitter_offset_previous   = m_buffer_frame_cpu.taa_jitter_offset;
        m_buffer_frame_cpu.taa_jitter_offset            = m_taa_jitter - m_taa_jitter_previous;
        m_buffer_frame_cpu.motion_blur_strength         = m_option_values[Option_Value_Motion_Blur_Intensity];
        m_buffer_frame_cpu.delta_time                   = static_cast<float>(m_context->GetSubsystem<Timer>()->GetDeltaTimeSmoothedSec());
        m_buffer_frame_cpu.time                         = static_cast<float>(m_context->GetSubsystem<Timer>()->GetTimeSec());
        m_buffer_frame_cpu.tonemapping                  = m_option_values[Option_Value_Tonemapping];
        m_buffer_frame_cpu.exposure                     = m_option_values[Option_Value_Exposure];
        m_buffer_frame_cpu.gamma                        = m_option_values[Option_Value_Gamma];
        m_buffer_frame_cpu.ssr_enabled                  = GetOption(Render_ScreenSpaceReflections) ? 1.0f : 0.0f;
        m_buffer_frame_cpu.shadow_resolution            = GetOptionValue<float>(Option_Value_ShadowResolution);

        // Update directional light intensity, just grab the first one
        for (const auto& entity : m_entities[Renderer_Object_Light])
            if (Light* light = entity->GetComponent<Light>())
            {
                if (light->GetLightType() == LightType_Directional)
                {
                    m_buffer_frame_cpu.directional_light_intensity = light->GetIntensity();
                }
            }

        // Update
        *buffer = m_buffer_frame_cpu;

        // Unmap
        return m_buffer_frame_gpu->Unmap();
    }

    bool Renderer::UpdateMaterialBuffer()
    {
        // Map
        BufferMaterial* buffer = static_cast<BufferMaterial*>(m_buffer_material_gpu->Map());
        if (!buffer)
        {
            LOG_ERROR("Failed to map buffer");
            return false;
        }

        // Update
        for (uint32_t mat_id = 1; mat_id < m_max_materials; mat_id++)
        {
            // mat_id of 0 is reserved for the skysphere

            if (!m_materials[mat_id])
                break;

            buffer->mat_clearcoat_clearcoatRough_anis_anisRot[mat_id].x = m_materials[mat_id]->GetProperty(Material_Clearcoat);
            buffer->mat_clearcoat_clearcoatRough_anis_anisRot[mat_id].y = m_materials[mat_id]->GetProperty(Material_Clearcoat_Roughness);
            buffer->mat_clearcoat_clearcoatRough_anis_anisRot[mat_id].z = m_materials[mat_id]->GetProperty(Material_Anisotropic);
            buffer->mat_clearcoat_clearcoatRough_anis_anisRot[mat_id].w = m_materials[mat_id]->GetProperty(Material_Anisotropic_Rotation);
            buffer->mat_sheen_sheenTint_pad[mat_id].x                   = m_materials[mat_id]->GetProperty(Material_Sheen);
            buffer->mat_sheen_sheenTint_pad[mat_id].y                   = m_materials[mat_id]->GetProperty(Material_Sheen_Tint);
        }

        // Unmap
        return m_buffer_material_gpu->Unmap();
    }

    bool Renderer::UpdateUberBuffer()
	{
        // Only update if needed
        if (m_buffer_uber_cpu == m_buffer_uber_cpu_previous)
            return false;

        // Map
        BufferUber* buffer = static_cast<BufferUber*>(m_buffer_uber_gpu->Map());
		if (!buffer)
		{
			LOG_ERROR("Failed to map buffer");
			return false;
		}

        // Update
        *buffer = m_buffer_uber_cpu;
        m_buffer_uber_cpu_previous = m_buffer_uber_cpu;

        // Unmap
		return m_buffer_uber_gpu->Unmap();
	}

    bool Renderer::UpdateObjectBuffer(RHI_CommandList* cmd_list, const uint32_t entity_index /*= 0*/)
    {
        // Only update if needed
        const bool same_content   = m_buffer_object_cpu == m_buffer_object_cpu_previous;
        const bool same_offset    = m_buffer_object_gpu->GetOffsetIndexDynamic() == entity_index;
        if (same_content && same_offset)
            return true;

        const uint32_t entity_count = entity_index + 1;

        // Re-allocate buffer with double size (if needed)
        if (entity_count >= m_buffer_object_gpu->GetElementCount())
        {
            const uint32_t new_size = Math::Helper::NextPowerOfTwo(entity_count);
            if (!m_buffer_object_gpu->Create<BufferObject>(new_size))
            {
                LOG_ERROR("Failed to re-allocate buffer with %d offsets", new_size);
                return false;
            }
        }

        // Set new buffer offset
        m_buffer_object_gpu->SetOffsetIndexDynamic(entity_index);

        // Dynamic buffers with offsets have to be rebound whenever the offset changes
        if (cmd_list)
        {
            cmd_list->SetConstantBuffer(3, RHI_Shader_Vertex, m_buffer_object_gpu);
        }

        // Map  
        BufferObject* buffer = static_cast<BufferObject*>(m_buffer_object_gpu->Map(entity_index));
        if (!buffer)
        {
            LOG_ERROR("Failed to map buffer");
            return false;
        }

        // Update
        *buffer = m_buffer_object_cpu;
        m_buffer_object_cpu_previous = m_buffer_object_cpu;

        // Unmap
        return m_buffer_object_gpu->Unmap();
    }

    bool Renderer::UpdateLightBuffer(const Light* light)
    {
        if (!light)
            return false;

        // Only update if needed
        if (m_buffer_light_cpu == m_buffer_light_cpu_previous)
            return true;

        // Map
        BufferLight* buffer = static_cast<BufferLight*>(m_buffer_light_gpu->Map());
        if (!buffer)
        {
            LOG_ERROR("Failed to map buffer");
            return false;
        }

        const bool volumetric         = static_cast<float>(m_options & Render_VolumetricLighting);
        const bool contact_shadows    = static_cast<float>(m_options & Render_ScreenSpaceShadows);

        for (uint32_t i = 0; i < light->GetShadowArraySize(); i++) { m_buffer_light_cpu.view_projection[i] = light->GetViewMatrix(i) * light->GetProjectionMatrix(i); }
        m_buffer_light_cpu.intensity_range_angle_bias   = Vector4(light->GetIntensity(), light->GetRange(), light->GetAngle(), GetOption(Render_ReverseZ) ? light->GetBias() : -light->GetBias());
        m_buffer_light_cpu.color                        = light->GetColor();
        m_buffer_light_cpu.normal_bias                  = light->GetNormalBias();
        m_buffer_light_cpu.position                     = light->GetTransform()->GetPosition();
        m_buffer_light_cpu.direction                    = light->GetDirection();

        // Update
        *buffer = m_buffer_light_cpu;
        m_buffer_light_cpu_previous = m_buffer_light_cpu;

        // Unmap
        return m_buffer_light_gpu->Unmap();
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

    const shared_ptr<Spartan::RHI_Texture>& Renderer::GetEnvironmentTexture()
    {
        if (m_render_targets.find(RenderTarget_Brdf_Prefiltered_Environment) != m_render_targets.end())
            return m_render_targets[RenderTarget_Brdf_Prefiltered_Environment];

        return m_tex_white;
    }

    void Renderer::SetEnvironmentTexture(const shared_ptr<RHI_Texture>& texture)
    {
        m_render_targets[RenderTarget_Brdf_Prefiltered_Environment] = texture;
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
        if (option == Option_Value_Anisotropy)
        {
            value = Helper::Clamp(value, 0.0f, 16.0f);
        }
        else if (option == Option_Value_ShadowResolution)
        {
            value = Helper::Clamp(value, static_cast<float>(m_resolution_shadow_min), static_cast<float>(m_rhi_device->GetContextRhi()->max_texture_dimension_2d));
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

    uint32_t Renderer::GetMaxResolution() const
    {
        return m_rhi_device->GetContextRhi()->max_texture_dimension_2d;
    }
}
