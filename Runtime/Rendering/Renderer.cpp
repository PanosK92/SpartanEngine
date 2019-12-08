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
        m_options |= Render_Debug_Grid;
        m_options |= Render_Debug_Lights;
        m_options |= Render_Debug_Physics;
        m_options |= Render_Bloom;
        m_options |= Render_VolumetricLighting;
        m_options |= Render_MotionBlur;
        m_options |= Render_SSAO;
        m_options |= Render_SSCS;
        m_options |= Render_SSR;	
        m_options |= Render_AntiAliasing_TAA;
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
        m_option_values[Option_Value_Fxaa_Sub_Pixel]          = 1.25f;
        m_option_values[Option_Value_Fxaa_Edge_Threshold]     = 0.125f;
        m_option_values[Option_Value_Fxaa_Edge_Threshold_Min] = 0.0312f;
        m_option_values[Option_Value_Bloom_Intensity]         = 0.005f;
        m_option_values[Option_Value_Sharpen_Strength]        = 1.0f;
        m_option_values[Option_Value_Sharpen_Clamp]           = 0.35f;
        m_option_values[Option_Value_Bloom_Intensity]         = 0.005f;
        m_option_values[Option_Value_Motion_Blur_Intensity]   = 0.01f;
        m_option_values[Option_Value_Ssao_Scale]              = 1.0f;

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

        // Create pipeline cache
        m_pipeline_cache = make_shared<RHI_PipelineCache>(m_rhi_device);

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
			//LOG_TO_FILE(false);
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

        if (resolution == GetOptionValue<uint32_t>(Option_Value_ShadowResolution))
            return;

        SetOptionValue(Option_Value_ShadowResolution, static_cast<float>(resolution));

        const auto& light_entities = m_entities[Renderer_Object_Light];
        for (const auto& light_entity : light_entities)
        {
            auto& light = light_entity->GetComponent<Light>();
            if (light->GetCastShadows())
            {
                light->CreateShadowMap();
            }
        }
    }

    void Renderer::Tick(float delta_time)
	{
		if (!m_rhi_device || !m_rhi_device->IsInitialized())
			return;

        RHI_CommandList* cmd_list = m_swap_chain->GetCmdList();

		// If there is no camera, do nothing
		if (!m_camera)
		{
            cmd_list->ClearRenderTarget(m_render_targets[RenderTarget_Composition_Ldr]->GetResource_RenderTarget(), Vector4(0.0f, 0.0f, 0.0f, 1.0f));
			return;
		}

		// If there is nothing to render clear to camera's color and present
		if (m_entities.empty())
		{
            cmd_list->ClearRenderTarget(m_render_targets[RenderTarget_Composition_Ldr]->GetResource_RenderTarget(), m_camera->GetClearColor());
			return;
		}

		m_frame_num++;
		m_is_odd_frame = (m_frame_num % 2) == 1;

		// Get camera matrices
		{
			m_near_plane	                            = m_camera->GetNearPlane();
			m_far_plane		                            = m_camera->GetFarPlane();
			m_buffer_frame_cpu.view			            = m_camera->GetViewMatrix();
			m_buffer_frame_cpu.projection	            = m_camera->GetProjectionMatrix();
            m_buffer_frame_cpu.projection_ortho         = Matrix::CreateOrthographicLH(m_resolution.x, m_resolution.y, m_near_plane, m_far_plane);
            m_buffer_frame_cpu.view_projection_ortho    = Matrix::CreateLookAtLH(Vector3(0, 0, -m_near_plane), Vector3::Forward, Vector3::Up) * m_buffer_frame_cpu.projection_ortho;

			// TAA - Generate jitter
			if (GetOptionValue(Render_AntiAliasing_TAA))
			{
				m_taa_jitter_previous = m_taa_jitter;

				// Halton(2, 3) * 16 seems to work nice
				const uint64_t samples	        = 16;
				const uint64_t index	        = m_frame_num % samples;
				m_taa_jitter			        = Utility::Sampling::Halton2D(index, 2, 3) * 2.0f - 1.0f;
				m_taa_jitter.x			        = m_taa_jitter.x / m_resolution.x;
				m_taa_jitter.y			        = m_taa_jitter.y / m_resolution.y;
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
		if (width == 0 || width > m_max_resolution || height == 0 || height > m_max_resolution)
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

    bool Renderer::UpdateFrameBuffer()
    {
        // Map
        FrameBuffer* buffer = static_cast<FrameBuffer*>(m_buffer_frame_gpu->Map());
        if (!buffer)
        {
            LOG_ERROR("Failed to map buffer");
            return false;
        }

        float light_directional_intensity = 0.0f;
        if (!m_entities[Renderer_Object_LightDirectional].empty())
        {
            if (Entity* entity = m_entities[Renderer_Object_LightDirectional].front())
            {
                if (shared_ptr<Light>& light = entity->GetComponent<Light>())
                {
                    light_directional_intensity = light->GetIntensity();
                }
            }
        }

        // Struct is updated automatically here as per frame data are (by definition) known ahead of time
        m_buffer_frame_cpu.camera_near                  = m_camera->GetNearPlane();
        m_buffer_frame_cpu.camera_far                   = m_camera->GetFarPlane();
        m_buffer_frame_cpu.camera_position              = m_camera->GetTransform()->GetPosition();
        m_buffer_frame_cpu.fxaa_sub_pixel               = m_option_values[Option_Value_Fxaa_Sub_Pixel];
        m_buffer_frame_cpu.fxaa_edge_threshold          = m_option_values[Option_Value_Fxaa_Edge_Threshold];
        m_buffer_frame_cpu.fxaa_edge_threshold_min      = m_option_values[Option_Value_Fxaa_Edge_Threshold_Min];
        m_buffer_frame_cpu.bloom_intensity              = m_option_values[Option_Value_Bloom_Intensity];
        m_buffer_frame_cpu.sharpen_strength             = m_option_values[Option_Value_Sharpen_Strength];
        m_buffer_frame_cpu.sharpen_clamp                = m_option_values[Option_Value_Sharpen_Clamp];
        m_buffer_frame_cpu.taa_jitter_offset            = m_taa_jitter - m_taa_jitter_previous;
        m_buffer_frame_cpu.motion_blur_strength         = m_option_values[Option_Value_Motion_Blur_Intensity];
        m_buffer_frame_cpu.delta_time                   = static_cast<float>(m_context->GetSubsystem<Timer>()->GetDeltaTimeSmoothedSec());
        m_buffer_frame_cpu.time                         = static_cast<float>(m_context->GetSubsystem<Timer>()->GetTimeSec());
        m_buffer_frame_cpu.tonemapping                  = m_option_values[Option_Value_Tonemapping];
        m_buffer_frame_cpu.exposure                     = m_option_values[Option_Value_Exposure];
        m_buffer_frame_cpu.gamma                        = m_option_values[Option_Value_Gamma];
        m_buffer_frame_cpu.directional_light_intensity  = light_directional_intensity;
        m_buffer_frame_cpu.ssr_enabled                  = GetOptionValue(Render_SSR) ? 1.0f : 0.0f;
        m_buffer_frame_cpu.shadow_resolution            = GetOptionValue<float>(Option_Value_ShadowResolution);
        m_buffer_frame_cpu.ssao_scale                   = m_option_values[Option_Value_Ssao_Scale];
        m_buffer_frame_cpu.padding                      = 0.0f;

        // Update
        *buffer = m_buffer_frame_cpu;

        // Unmap
        return m_buffer_frame_gpu->Unmap();
    }

    bool Renderer::UpdateUberBuffer()
	{
        // Only update if needed
        if (m_buffer_uber_cpu == m_buffer_uber_cpu_previous)
            return false;

        // Map
        UberBuffer* buffer = static_cast<UberBuffer*>(m_buffer_uber_gpu->Map());
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

    bool Renderer::UpdateLightBuffer(const vector<Entity*>& entities)
    {
        if (entities.empty())
            return false;

        // Only update if needed
        if (m_buffer_light_cpu == m_buffer_light_cpu_previous)
            return true;

        // Map
        LightBuffer* buffer = static_cast<LightBuffer*>(m_buffer_light_gpu->Map());
        if (!buffer)
        {
            LOG_ERROR("Failed to map buffer");
            return false;
        }

        for (uint32_t i = 0; i < entities.size(); i++)
        {
            if (Light* light = entities[i]->GetComponent<Light>().get())
            {
                for (int j = 0; j < g_cascade_count; j++)                   { m_buffer_light_cpu.view_projection[i][j] = light->GetViewMatrix(j) * light->GetProjectionMatrix(j); }
                m_buffer_light_cpu.intensity_range_angle_bias[i]            = Vector4(light->GetIntensity(), light->GetRange(), light->GetAngle(), GetOptionValue(Render_ReverseZ) ? light->GetBias() : -light->GetBias());
                m_buffer_light_cpu.normalBias_shadow_volumetric_contact[i]  = Vector4(light->GetNormalBias(), light->GetCastShadows(), static_cast<float>(m_options & Render_VolumetricLighting), static_cast<float>(m_options & Render_SSCS));
                m_buffer_light_cpu.color[i]                                 = light->GetColor();
                m_buffer_light_cpu.position[i]                              = light->GetTransform()->GetPosition();
                m_buffer_light_cpu.direction[i]                             = light->GetDirection();
            }
        }
        m_buffer_light_cpu.light_count = static_cast<float>(entities.size());

        // Update
        *buffer = m_buffer_light_cpu;
        m_buffer_light_cpu_previous = m_buffer_light_cpu;

        // Unmap
        return m_buffer_light_gpu->Unmap();
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
		if (cull_mode == RHI_Cull_Back)		return (fill_mode == RHI_Fill_Solid) ? m_rasterizer_cull_back_solid		: m_rasterizer_cull_back_wireframe;
		if (cull_mode == RHI_Cull_Front)	return (fill_mode == RHI_Fill_Solid) ? m_rasterizer_cull_front_solid	: m_rasterizer_cull_front_wireframe;
		if (cull_mode == RHI_Cull_None)		return (fill_mode == RHI_Fill_Solid) ? m_rasterizer_cull_none_solid		: m_rasterizer_cull_none_wireframe;

		return m_rasterizer_cull_back_solid;
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

    void Renderer::SetOptionValue(Renderer_Option_Value option, float value, float min /*= numeric_limits<float>::lowest()*/, float max /*= numeric_limits<float>::max()*/)
    {
        value = Clamp(value, min, max);

        if (m_option_values[option] == value)
            return;

        if (option == Option_Value_Ssao_Scale)
        {
            value = Clamp(value, 0.25f, 1.0f);
            uint32_t width = static_cast<uint32_t>(m_resolution.x * value);
            uint32_t height = static_cast<uint32_t>(m_resolution.y * value);
            m_render_targets[RenderTarget_Ssao_Raw] = make_unique<RHI_Texture2D>(m_context, width, height, m_render_targets[RenderTarget_Ssao_Raw]->GetFormat());
            m_render_targets[RenderTarget_Ssao_Blurred] = make_unique<RHI_Texture2D>(m_context, width, height, m_render_targets[RenderTarget_Ssao_Raw]->GetFormat());
        }

        m_option_values[option] = value;
    }
}
