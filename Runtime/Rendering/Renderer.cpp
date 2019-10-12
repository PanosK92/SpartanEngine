/*
Copyright(c) 2016-2019 Panos Karabelas

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
#include "Font/Font.h"
#include "Shaders/ShaderBuffered.h"
#include "Utilities/Sampling.h"
#include "../Profiling/Profiler.h"
#include "../Resource/ResourceCache.h"
#include "Gizmos/Grid.h"
#include "Gizmos/Transform_Gizmo.h"
#include "../Core/Engine.h"
#include "../Core/Timer.h"
#include "../World/Entity.h"
#include "../World/Components/Renderable.h"
#include "../World/Components/Camera.h"
#include "../RHI/RHI_Device.h"
#include "../RHI/RHI_PipelineCache.h"
#include "../RHI/RHI_CommandList.h"
//=========================================

//= NAMESPACES ===============
using namespace std;
using namespace Spartan::Math;
//============================

namespace Spartan
{
	Renderer::Renderer(Context* context) : ISubsystem(context)
	{
		m_flags		|= Render_Debug_Transform;
		m_flags		|= Render_Debug_Grid;
		m_flags		|= Render_Debug_Lights;
		m_flags		|= Render_Debug_Physics;
		m_flags		|= Render_Bloom;
        m_flags     |= Render_VolumetricLighting;
		m_flags		|= Render_SSAO;
        m_flags     |= Render_SSCS;
		m_flags		|= Render_MotionBlur;
		m_flags		|= Render_AntiAliasing_TAA;
        m_flags     |= Render_SSR;
		//m_flags	|= Render_PostProcess_FXAA;                 // Disabled by default: TAA is superior.
		//m_flags	|= Render_PostProcess_Sharpening;		    // Disabled by default: TAA's blurring is taken core of with an always on sharpen pass specifically for it.
		//m_flags	|= Render_PostProcess_Dithering;			// Disabled by default: It's only needed in very dark scenes to fix smooth color gradients.
		//m_flags	|= Render_PostProcess_ChromaticAberration;	// Disabled by default: It doesn't improve the image quality, it's more of a stylistic effect.	

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
        m_resource_cache    = m_context->GetSubsystem<ResourceCache>().get();
        m_profiler          = m_context->GetSubsystem<Profiler>().get();

        // Create device
        m_rhi_device = make_shared<RHI_Device>(m_context);
        if (!m_rhi_device->IsInitialized())
        {
            LOG_ERROR("Failed to create device");
            return false;
        }

        // Create swap chain
        {
            const WindowData& window_data = m_context->m_engine->GetWindowData();

            m_swap_chain = make_shared<RHI_SwapChain>
            (
                window_data.handle,
                m_rhi_device,
                static_cast<uint32_t>(window_data.width),
                static_cast<uint32_t>(window_data.height),
                Format_R8G8B8A8_UNORM,
                2,
                Present_Immediate | Swap_Flip_Discard
            );

            if (!m_swap_chain->IsInitialized())
            {
                LOG_ERROR("Failed to create swap chain");
                return false;
            }
        }

        // Create pipeline cache
        m_pipeline_cache = make_shared<RHI_PipelineCache>(m_rhi_device);

        // Create command list
        m_cmd_list = make_shared<RHI_CommandList>(m_rhi_device, m_profiler);

		// Editor specific
		m_gizmo_grid		= make_unique<Grid>(m_rhi_device);
		m_gizmo_transform	= make_unique<Transform_Gizmo>(m_context);

		// Create a constant buffer that will be used for most shaders
		m_uber_buffer = make_shared<RHI_ConstantBuffer>(m_rhi_device);
		m_uber_buffer->Create<UberBuffer>();

		// Line buffer
		m_vertex_buffer_lines = make_shared<RHI_VertexBuffer>(m_rhi_device);

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

	const shared_ptr<Entity>& Renderer::SnapTransformGizmoTo(const shared_ptr<Entity>& entity) const
	{
		return m_gizmo_transform->SetSelectedEntity(entity);
	}

    void Renderer::SetShadowResolution(uint32_t resolution)
    {
        resolution = Clamp(resolution, m_resolution_shadow_min, m_max_resolution);

        if (resolution == m_resolution_shadow)
            return;

        m_resolution_shadow = resolution;

        const auto& light_entities = m_entities[Renderer_Object_Light];
        for (const auto& light_entity : light_entities)
        {
            auto& light = light_entity->GetComponent<Light>();
            if (light->GetCastShadows())
            {
                light->CreateShadowMap(true);
            }
        }
    }

    void Renderer::SetAnisotropy(uint32_t anisotropy)
    {
        uint32_t min = 0;
        uint32_t max = 16;
        m_anisotropy = Math::Clamp(anisotropy, min, max);
    }

    void Renderer::Tick(float delta_time)
	{
#ifdef API_GRAPHICS_VULKAN
		return;
#endif
		if (!m_rhi_device || !m_rhi_device->IsInitialized())
			return;

		// If there is no camera, do nothing
		if (!m_camera)
		{
			m_cmd_list->ClearRenderTarget(m_render_targets[RenderTarget_Composition_Ldr]->GetResource_RenderTarget(), Vector4(0.0f, 0.0f, 0.0f, 1.0f));
			return;
		}

		// If there is nothing to render clear to camera's color and present
		if (m_entities.empty())
		{
			m_cmd_list->ClearRenderTarget(m_render_targets[RenderTarget_Composition_Ldr]->GetResource_RenderTarget(), m_camera->GetClearColor());
			return;
		}

		m_frame_num++;
		m_is_odd_frame = (m_frame_num % 2) == 1;

		// Get camera matrices
		{
			m_near_plane	= m_camera->GetNearPlane();
			m_far_plane		= m_camera->GetFarPlane();
			m_view			= m_camera->GetViewMatrix();
			m_view_base		= m_camera->GetBaseViewMatrix();
			m_projection	= m_camera->GetProjectionMatrix();

			// TAA - Generate jitter
			if (IsFlagSet(Render_AntiAliasing_TAA))
			{
				m_taa_jitter_previous = m_taa_jitter;

				// Halton(2, 3) * 16 seems to work nice
				const uint64_t samples	= 16;
				const uint64_t index	= m_frame_num % samples;
				m_taa_jitter			= Utility::Sampling::Halton2D(index, 2, 3) * 2.0f - 1.0f;
				m_taa_jitter.x			= m_taa_jitter.x / m_resolution.x;
				m_taa_jitter.y			= m_taa_jitter.y / m_resolution.y;
				m_projection			*= Matrix::CreateTranslation(Vector3(m_taa_jitter.x, m_taa_jitter.y, 0.0f));
			}
			else
			{
				m_taa_jitter			= Vector2::Zero;
				m_taa_jitter_previous	= Vector2::Zero;		
			}

			m_view_projection				= m_view * m_projection;
			m_view_projection_inv			= Matrix::Invert(m_view_projection);
			m_projection_orthographic		= Matrix::CreateOrthographicLH(m_resolution.x, m_resolution.y, m_near_plane, m_far_plane);
			m_view_projection_orthographic	= m_view_base * m_projection_orthographic;
		}

		m_is_rendering = true;
		Pass_Main();
		m_is_rendering = false;
	}

	void Renderer::SetResolution(uint32_t width, uint32_t height)
	{
		// Return if resolution is invalid
		if (width == 0 || width > m_max_resolution || height == 0 || height > m_max_resolution)
		{
			LOGF_WARNING("%dx%d is an invalid resolution", width, height);
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

		// Log
		LOGF_INFO("Resolution set to %dx%d", width, height);
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

	void Renderer::DrawBox(const BoundingBox& box, const Vector4& color, const bool depth /*= true*/)
	{
		const auto& min = box.GetMin();
		const auto& max = box.GetMax();
	
		DrawLine(Vector3(min.x, min.y, min.z), Vector3(max.x, min.y, min.z), color, depth);
		DrawLine(Vector3(max.x, min.y, min.z), Vector3(max.x, max.y, min.z), color, depth);
		DrawLine(Vector3(max.x, max.y, min.z), Vector3(min.x, max.y, min.z), color, depth);
		DrawLine(Vector3(min.x, max.y, min.z), Vector3(min.x, min.y, min.z), color, depth);
		DrawLine(Vector3(min.x, min.y, min.z), Vector3(min.x, min.y, max.z), color, depth);
		DrawLine(Vector3(max.x, min.y, min.z), Vector3(max.x, min.y, max.z), color, depth);
		DrawLine(Vector3(max.x, max.y, min.z), Vector3(max.x, max.y, max.z), color, depth);
		DrawLine(Vector3(min.x, max.y, min.z), Vector3(min.x, max.y, max.z), color, depth);
		DrawLine(Vector3(min.x, min.y, max.z), Vector3(max.x, min.y, max.z), color, depth);
		DrawLine(Vector3(max.x, min.y, max.z), Vector3(max.x, max.y, max.z), color, depth);
		DrawLine(Vector3(max.x, max.y, max.z), Vector3(min.x, max.y, max.z), color, depth);
		DrawLine(Vector3(min.x, max.y, max.z), Vector3(min.x, min.y, max.z), color, depth);
	}

    bool Renderer::UpdateUberBuffer(const uint32_t resolution_width, const uint32_t resolution_height, const Matrix& mvp)
	{
		auto buffer = static_cast<UberBuffer*>(m_uber_buffer->Map());
		if (!buffer)
		{
			LOGF_ERROR("Failed to map buffer");
			return false;
		}

        float light_directional_intensity = 0.0f;
        if (!m_entities[Renderer_Object_LightDirectional].empty())
        {
            if (Entity * entity = m_entities[Renderer_Object_LightDirectional].front())
            {
                if (shared_ptr<Light>& light = entity->GetComponent<Light>())
                {
                    light_directional_intensity = light->GetIntensity();
                }
            }
        }

		buffer->m_mvp					    = mvp;
		buffer->m_view					    = m_view;
		buffer->m_projection			    = m_projection;
		buffer->m_projection_ortho		    = m_projection_orthographic;
		buffer->m_view_projection		    = m_view_projection;
		buffer->m_view_projection_inv	    = m_view_projection_inv;
		buffer->m_view_projection_ortho	    = m_view_projection_orthographic;
		buffer->camera_position			    = m_camera->GetTransform()->GetPosition();
		buffer->camera_near				    = m_camera->GetNearPlane();
		buffer->camera_far				    = m_camera->GetFarPlane();
		buffer->resolution				    = Vector2(static_cast<float>(resolution_width), static_cast<float>(resolution_height));
		buffer->fxaa_sub_pixel			    = m_fxaa_sub_pixel;
		buffer->fxaa_edge_threshold		    = m_fxaa_edge_threshold;
		buffer->fxaa_edge_threshold_min	    = m_fxaa_edge_threshold_min;
		buffer->bloom_intensity			    = m_bloom_intensity;
		buffer->sharpen_strength		    = m_sharpen_strength;
		buffer->sharpen_clamp			    = m_sharpen_clamp;
		buffer->taa_jitter_offset		    = m_taa_jitter - m_taa_jitter_previous;
		buffer->motion_blur_strength	    = m_motion_blur_intensity;
		buffer->delta_time				    = static_cast<float>(m_context->GetSubsystem<Timer>()->GetDeltaTimeSmoothedSec());
        buffer->time                        = static_cast<float>(m_context->GetSubsystem<Timer>()->GetTimeSec());
		buffer->tonemapping				    = static_cast<float>(m_tonemapping);
		buffer->exposure				    = m_exposure;
		buffer->gamma					    = m_gamma;
        buffer->directional_light_intensity = light_directional_intensity;
        buffer->ssr_enabled                 = IsFlagSet(Render_SSR) ? 1.0f : 0.0f;
        buffer->shadow_resolution           = static_cast<float>(m_resolution_shadow);

		return m_uber_buffer->Unmap();
	}

	void Renderer::RenderablesAcquire(const Variant& entities_variant)
	{
		TIME_BLOCK_START_CPU(m_profiler);

		// Clear previous state
		m_entities.clear();
		m_camera = nullptr;

		vector<shared_ptr<Entity>> entities = entities_variant.Get<vector<shared_ptr<Entity>>>();
		for (const auto& entity : entities)
		{
			if (!entity || !entity->IsActive())
				continue;

			// Get all the components we are interested in
			auto& renderable    = entity->GetComponent<Renderable>();
			auto& light		    = entity->GetComponent<Light>();
			auto& camera		= entity->GetComponent<Camera>();

			if (renderable)
			{
				const auto is_transparent = !renderable->HasMaterial() ? false : renderable->GetMaterial()->GetColorAlbedo().w < 1.0f;
                m_entities[is_transparent ? Renderer_Object_Transparent : Renderer_Object_Opaque].emplace_back(entity.get());
			}

			if (light)
			{
				m_entities[Renderer_Object_Light].emplace_back(entity.get());

                if (light->GetLightType() == LightType_Directional) m_entities[Renderer_Object_LightDirectional].emplace_back(entity.get());
                if (light->GetLightType() == LightType_Point)       m_entities[Renderer_Object_LightPoint].emplace_back(entity.get());
                if (light->GetLightType() == LightType_Spot)        m_entities[Renderer_Object_LightSpot].emplace_back(entity.get());
			}

			if (camera)
			{
				m_entities[Renderer_Object_Camera].emplace_back(entity.get());
				m_camera = camera;
			}
		}

		RenderablesSort(&m_entities[Renderer_Object_Opaque]);
		RenderablesSort(&m_entities[Renderer_Object_Transparent]);

		TIME_BLOCK_END(m_profiler);
	}

	void Renderer::RenderablesSort(vector<Entity*>* renderables)
	{
		if (!m_camera || renderables->size() <= 2)
			return;

		auto render_hash = [this](Entity* entity)
		{
			// Get renderable
			auto renderable = entity->GetRenderable_PtrRaw();
			if (!renderable)
				return 0.0f;

			// Get material
			const auto material = renderable->GetMaterial();
			if (!material)
				return 0.0f;

			const auto num_depth    = (renderable->GetAabb().GetCenter() - m_camera->GetTransform()->GetPosition()).LengthSquared();
			const auto num_material = static_cast<float>(material->GetId());

			return stof(to_string(num_depth) + "-" + to_string(num_material));
		};

		// Sort by depth (front to back), then sort by material		
		sort(renderables->begin(), renderables->end(), [&render_hash](Entity* a, Entity* b)
		{
            return render_hash(a) < render_hash(b);
		});
	}

	shared_ptr<RHI_RasterizerState>& Renderer::GetRasterizerState(const RHI_Cull_Mode cull_mode, const RHI_Fill_Mode fill_mode)
	{
		if (cull_mode == Cull_Back)		return (fill_mode == Fill_Solid) ? m_rasterizer_cull_back_solid		: m_rasterizer_cull_back_wireframe;
		if (cull_mode == Cull_Front)	return (fill_mode == Fill_Solid) ? m_rasterizer_cull_front_solid	: m_rasterizer_cull_front_wireframe;
		if (cull_mode == Cull_None)		return (fill_mode == Fill_Solid) ? m_rasterizer_cull_none_solid		: m_rasterizer_cull_none_wireframe;

		return m_rasterizer_cull_back_solid;
	}

    void* Renderer::GetEnvironmentTexture_GpuResource()
    {
        if (const shared_ptr<RHI_Texture>& environment_texture = GetEnvironmentTexture())
        {
            return environment_texture->GetResource_Texture();
        }

        return m_tex_white->GetResource_Texture();
    }

    const std::shared_ptr<Spartan::RHI_Texture>& Renderer::GetEnvironmentTexture()
    {
        if (m_render_targets.find(RenderTarget_Brdf_Prefiltered_Environment) != m_render_targets.end())
            return m_render_targets[RenderTarget_Brdf_Prefiltered_Environment];

        return m_tex_white;
    }

    void Renderer::SetEnvironmentTexture(const shared_ptr<RHI_Texture>& texture)
    {
        m_render_targets[RenderTarget_Brdf_Prefiltered_Environment] = texture;
    }
}
