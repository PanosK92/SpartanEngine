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
#include "Material.h"
#include "Model.h"
#include "ShaderBuffered.h"
#include "Font/Font.h"
#include "../Profiling/Profiler.h"
#include "../Resource/IResource.h"
#include "Deferred/ShaderVariation.h"
#include "Deferred/ShaderLight.h"
#include "Gizmos/Grid.h"
#include "Gizmos/Transform_Gizmo.h"
#include "../RHI/RHI_VertexBuffer.h"
#include "../RHI/RHI_ConstantBuffer.h"
#include "../RHI/RHI_Texture.h"
#include "../RHI/RHI_Sampler.h"
#include "../RHI/RHI_CommandList.h"
#include "../World/Entity.h"
#include "../World/Components/Renderable.h"
#include "../World/Components/Transform.h"
#include "../World/Components/Skybox.h"
#include "../World/Components/Light.h"
#include "../World/Components/Camera.h"
//=========================================

//= NAMESPACES ===============
using namespace std;
using namespace Spartan::Math;
//============================

static const float GIZMO_MAX_SIZE = 5.0f;
static const float GIZMO_MIN_SIZE = 0.1f;

namespace Spartan
{
	void Renderer::Pass_Main()
	{
#ifdef API_GRAPHICS_VULKAN
        // For the time being, when using Vulkan, do simple stuff so I can debug
        m_cmd_list->Begin("Pass_Main");
        Pass_LightDepth();
        m_cmd_list->End();
        m_cmd_list->Submit();
        return;
#endif
		m_cmd_list->Begin("Pass_Main");

		Pass_LightDepth();

		Pass_GBuffer();

		Pass_PreLight
		(
			m_render_tex_half_ssao,	    // IN:	
			m_render_tex_half_shadows,	// OUT: Shadows
			m_render_tex_full_ssao      // OUT: SSAO
		);

		Pass_Light
		(
			m_render_tex_half_shadows,	// IN:	Shadows
            m_render_tex_full_ssao,	    // IN:	SSAO
			m_render_tex_full_light		// Out: Result
		);

		Pass_Transparent(m_render_tex_full_light);

		Pass_PostLight
		(
			m_render_tex_full_light,	// IN:	Light pass result
			m_render_tex_full_final		// OUT: Result
		);

        Pass_Lines(m_render_tex_full_final);

        Pass_Gizmos(m_render_tex_full_final);

		Pass_DebugBuffer(m_render_tex_full_final);

		Pass_PerformanceMetrics(m_render_tex_full_final);

		m_cmd_list->End();
		m_cmd_list->Submit();

		m_render_tex_full_light.swap(m_render_tex_full_light_previous);
	}

	void Renderer::Pass_LightDepth()
	{
		// Acquire shader
		const auto& shader_depth = m_shaders[Shader_Depth_V];
		if (!shader_depth->IsCompiled())
			return;

		uint32_t light_directional_count = 0;
		const auto& light_entities = m_entities[Renderable_Light];
		for (const auto& light_entity : light_entities)
		{
			const auto& light = light_entity->GetComponent<Light>();

			// Skip if it doesn't need to cast shadows
			if (!light->GetCastShadows())
				continue;

			// Acquire light's shadow map
			const auto& shadow_map = light->GetShadowMap();
			if (!shadow_map)
				continue;

			// Get opaque renderable entities
			const auto& entities = m_entities[Renderable_ObjectOpaque];
			if (entities.empty())
				continue;

			// Begin command list
			m_cmd_list->Begin("Pass_LightDepth");
			m_cmd_list->SetShaderPixel(nullptr);
			m_cmd_list->SetBlendState(m_blend_disabled);
			m_cmd_list->SetDepthStencilState(m_depth_stencil_enabled);
			m_cmd_list->SetRasterizerState(m_rasterizer_cull_back_solid);
			m_cmd_list->SetPrimitiveTopology(PrimitiveTopology_TriangleList);
			m_cmd_list->SetShaderVertex(shader_depth);
			m_cmd_list->SetInputLayout(shader_depth->GetInputLayout());
			m_cmd_list->SetViewport(shadow_map->GetViewport());

			// Tracking
			uint32_t currently_bound_geometry = 0;

			for (uint32_t i = 0; i < light->GetShadowMap()->GetArraySize(); i++)
			{
				const auto cascade_depth_stencil = shadow_map->GetResource_DepthStencil(i);

				m_cmd_list->Begin("Array_" + to_string(i + 1));
				m_cmd_list->ClearDepthStencil(cascade_depth_stencil, Clear_Depth, GetClearDepth());
				m_cmd_list->SetRenderTarget(nullptr, cascade_depth_stencil);

				auto light_view_projection = light->GetViewMatrix(i) * light->GetProjectionMatrix(i);

				for (const auto& entity : entities)
				{
					// Acquire renderable component
					const auto& renderable = entity->GetRenderable_PtrRaw();
					if (!renderable)
						continue;

					// Acquire material
					const auto& material = renderable->GetMaterial();
					if (!material)
						continue;

					// Acquire geometry
					const auto& model = renderable->GeometryModel();
					if (!model || !model->GetVertexBuffer() || !model->GetIndexBuffer())
						continue;

					// Skip meshes that don't cast shadows
					if (!renderable->GetCastShadows())
						continue;

					// Skip transparent meshes (for now)
					if (material->GetColorAlbedo().w < 1.0f)
						continue;

					// Bind geometry
					if (currently_bound_geometry != model->GetId())
					{
						m_cmd_list->SetBufferIndex(model->GetIndexBuffer());
						m_cmd_list->SetBufferVertex(model->GetVertexBuffer());
						currently_bound_geometry = model->GetId();
					}

					// Accumulate directional light direction
					if (light->GetLightType() == LightType_Directional)
					{
						m_directional_light_avg_dir += light->GetDirection();
						light_directional_count++;
					}

					// Update constant buffer
					const auto& transform = entity->GetTransform_PtrRaw();
					transform->UpdateConstantBufferLight(m_rhi_device, light_view_projection, i);
					m_cmd_list->SetConstantBuffer(1, Buffer_VertexShader, transform->GetConstantBufferLight(i));

					m_cmd_list->DrawIndexed(renderable->GeometryIndexCount(), renderable->GeometryIndexOffset(), renderable->GeometryVertexOffset());
				}
				m_cmd_list->End(); // end of cascade
			}
			m_cmd_list->End();
			m_cmd_list->Submit();
		}

		// Compute average directional light direction
		m_directional_light_avg_dir /= static_cast<float>(light_directional_count);
	}

	void Renderer::Pass_GBuffer()
	{
		if (!m_rhi_device)
			return;

		m_cmd_list->Begin("Pass_GBuffer");

		const auto& clear_color= Vector4::Zero;
		
		// If there is nothing to render, just clear
		if (m_entities[Renderable_ObjectOpaque].empty())
		{
			m_cmd_list->ClearRenderTarget(m_g_buffer_albedo->GetResource_RenderTarget(), clear_color);
			m_cmd_list->ClearRenderTarget(m_g_buffer_normal->GetResource_RenderTarget(), clear_color);
			m_cmd_list->ClearRenderTarget(m_g_buffer_material->GetResource_RenderTarget(), Vector4::Zero); // zeroed material buffer causes sky sphere to render
			m_cmd_list->ClearRenderTarget(m_g_buffer_velocity->GetResource_RenderTarget(), clear_color);
			m_cmd_list->ClearDepthStencil(m_g_buffer_depth->GetResource_DepthStencil(), Clear_Depth, GetClearDepth());
			m_cmd_list->End();
			m_cmd_list->Submit();
			return;
		}

		const auto& shader_gbuffer = m_shaders[Shader_Gbuffer_V];
        if (!shader_gbuffer->IsCompiled())
            return;

        // Pack render targets
		const vector<void*> render_targets
		{
			m_g_buffer_albedo->GetResource_RenderTarget(),
			m_g_buffer_normal->GetResource_RenderTarget(),
			m_g_buffer_material->GetResource_RenderTarget(),
			m_g_buffer_velocity->GetResource_RenderTarget()
		};

		SetDefaultBuffer(static_cast<uint32_t>(m_resolution.x), static_cast<uint32_t>(m_resolution.y));
	
		// Star command list
		m_cmd_list->SetRasterizerState(m_rasterizer_cull_back_solid);
		m_cmd_list->SetBlendState(m_blend_disabled);
		m_cmd_list->SetPrimitiveTopology(PrimitiveTopology_TriangleList);
		m_cmd_list->SetDepthStencilState(m_depth_stencil_enabled);
		m_cmd_list->SetViewport(m_g_buffer_albedo->GetViewport());
		m_cmd_list->SetRenderTargets(render_targets, m_g_buffer_depth->GetResource_DepthStencil());
		m_cmd_list->ClearRenderTargets(render_targets, clear_color);
		m_cmd_list->ClearDepthStencil(m_g_buffer_depth->GetResource_DepthStencil(), Clear_Depth, GetClearDepth());
		m_cmd_list->SetShaderVertex(shader_gbuffer);
		m_cmd_list->SetInputLayout(shader_gbuffer->GetInputLayout());
		m_cmd_list->SetConstantBuffer(0, Buffer_Global, m_buffer_global);
		m_cmd_list->SetSampler(0, m_sampler_anisotropic_wrap);	
		
		// Variables that help reduce state changes
		uint32_t currently_bound_geometry	= 0;
		uint32_t currently_bound_shader		= 0;
		uint32_t currently_bound_material	= 0;

		for (const auto& entity : m_entities[Renderable_ObjectOpaque])
		{
			// Get renderable
			const auto& renderable = entity->GetRenderable_PtrRaw();
			if (!renderable)
				continue;

			// Get material
			const auto& material = renderable->GetMaterial();
			if (!material)
				continue;

			// Get shader and geometry
			const auto& shader = material->GetShader();
			const auto& model = renderable->GeometryModel();

			// Validate shader
			if (!shader || shader->GetCompilationState() != Shader_Compiled)
				continue;

			// Validate geometry
			if (!model || !model->GetVertexBuffer() || !model->GetIndexBuffer())
				continue;

			// Skip objects outside of the view frustum
			if (!m_camera->IsInViewFrustrum(renderable))
				continue;

			// Set face culling (changes only if required)
			m_cmd_list->SetRasterizerState(GetRasterizerState(material->GetCullMode(), Fill_Solid));

			// Bind geometry
			if (currently_bound_geometry != model->GetId())
			{
				m_cmd_list->SetBufferIndex(model->GetIndexBuffer());
				m_cmd_list->SetBufferVertex(model->GetVertexBuffer());
				currently_bound_geometry = model->GetId();
			}

			// Bind shader
			if (currently_bound_shader != shader->GetId())
			{
				m_cmd_list->SetShaderPixel(static_pointer_cast<RHI_Shader>(shader));
				currently_bound_shader = shader->GetId();
			}

			// Bind material
			if (currently_bound_material != material->GetId())
			{
				// Bind material textures		
				m_cmd_list->SetTextures(0, material->GetResources(), 8);

				// Bind material buffer
				material->UpdateConstantBuffer();
				m_cmd_list->SetConstantBuffer(1, Buffer_PixelShader, material->GetConstantBuffer());

				currently_bound_material = material->GetId();
			}

			// Bind object buffer
			const auto& transform = entity->GetTransform_PtrRaw();
			transform->UpdateConstantBuffer(m_rhi_device, m_view_projection);
			m_cmd_list->SetConstantBuffer(2, Buffer_VertexShader, transform->GetConstantBuffer());

			// Render	
			m_cmd_list->DrawIndexed(renderable->GeometryIndexCount(), renderable->GeometryIndexOffset(), renderable->GeometryVertexOffset());
			m_profiler->m_renderer_meshes_rendered++;

		} // ENTITY/MESH ITERATION

		m_cmd_list->End();
		m_cmd_list->Submit();
	}

	void Renderer::Pass_PreLight(shared_ptr<RHI_Texture>& tex_ssao, shared_ptr<RHI_Texture>& tex_shadows_out, shared_ptr<RHI_Texture>& tex_ssao_out)
	{
		m_cmd_list->Begin("Pass_PreLight");
		m_cmd_list->SetDepthStencilState(m_depth_stencil_disabled);
		m_cmd_list->SetRasterizerState(m_rasterizer_cull_back_solid);
		m_cmd_list->SetBlendState(m_blend_disabled);
		m_cmd_list->SetPrimitiveTopology(PrimitiveTopology_TriangleList);
		m_cmd_list->SetBufferVertex(m_quad.GetVertexBuffer());
		m_cmd_list->SetBufferIndex(m_quad.GetIndexBuffer());
		m_cmd_list->ClearRenderTarget(tex_shadows_out->GetResource_RenderTarget(), Vector4::One);

		// shadow mapping + blur
		auto shadow_mapped = false;
		auto& lights = m_entities[Renderable_Light];
		for (uint32_t i = 0; i < lights.size(); i++)
		{
			auto light = lights[i]->GetComponent<Light>().get();

			// Skip lights that don't cast shadows
			if (!light->GetCastShadows())
				continue;

			Pass_ShadowMapping(tex_shadows_out, light);
			shadow_mapped = true;
		}
		if (!shadow_mapped)
		{
			m_cmd_list->ClearRenderTarget(tex_shadows_out->GetResource_RenderTarget(), Vector4::One);
		}

		// SSAO
		if (m_flags & Render_PostProcess_SSAO)
		{
            // Actual ssao
			Pass_SSAO(tex_ssao);

            // Bilateral blur
			const auto sigma		= 1.0f;
			const auto pixel_stride	= 1.0f;
			Pass_BlurBilateralGaussian(tex_ssao, m_render_tex_half_ssao_blurred, sigma, pixel_stride);

            // Upscale to full size
            Pass_Upsample(m_render_tex_half_ssao_blurred, tex_ssao_out);
		}

		m_cmd_list->End();
	}

	void Renderer::Pass_Light(shared_ptr<RHI_Texture>& tex_shadows, shared_ptr<RHI_Texture>& tex_ssao, shared_ptr<RHI_Texture>& tex_out)
	{
        // Acquire shader
		const auto& shader_light = static_pointer_cast<ShaderLight>(m_shaders[Shader_Light_Vp]);
		if (shader_light->GetCompilationState() != Shader_Compiled)
			return;

		m_cmd_list->Begin("Pass_Light");

		// Update constant buffers
		SetDefaultBuffer(static_cast<uint32_t>(m_resolution.x), static_cast<uint32_t>(m_resolution.y));
		shader_light->UpdateConstantBuffer
		(
			m_view_projection_orthographic,
			m_view,
			m_projection,
			m_entities[Renderable_Light],
			Flags_IsSet(Render_PostProcess_SSR)
		);

		// Prepare resources
		const auto shader						= static_pointer_cast<RHI_Shader>(shader_light);
		const vector<void*> samplers			= { m_sampler_bilinear_clamp->GetResource(), m_sampler_trilinear_clamp->GetResource(), m_sampler_point_clamp->GetResource() };
		const vector<void*> constant_buffers	= { m_buffer_global->GetResource(),  shader_light->GetConstantBuffer()->GetResource() };
		void* textures[] =
		{
			m_g_buffer_albedo->GetResource_Texture(),																		// Albedo	
			m_g_buffer_normal->GetResource_Texture(),																		// Normal
			m_g_buffer_depth->GetResource_Texture(),																		// Depth
			m_g_buffer_material->GetResource_Texture(),																		// Material
			tex_shadows->GetResource_Texture(),																				// Shadows
			Flags_IsSet(Render_PostProcess_SSAO) ? tex_ssao->GetResource_Texture() : m_tex_white->GetResource_Texture(),	// SSAO
			m_render_tex_full_light_previous->GetResource_Texture(),														// Previous frame
			m_skybox ? m_skybox->GetTexture()->GetResource_Texture() : m_tex_white->GetResource_Texture(),					// Environment
			m_tex_lut_ibl->GetResource_Texture()																			// LutIBL
		};

		// Setup command list
		m_cmd_list->SetDepthStencilState(m_depth_stencil_disabled);
		m_cmd_list->SetRasterizerState(m_rasterizer_cull_back_solid);
		m_cmd_list->SetBlendState(m_blend_disabled);
		m_cmd_list->SetPrimitiveTopology(PrimitiveTopology_TriangleList);
		m_cmd_list->SetViewport(tex_out->GetViewport());
		m_cmd_list->SetRenderTarget(tex_out);
		m_cmd_list->SetShaderVertex(shader);
		m_cmd_list->SetShaderPixel(shader);
		m_cmd_list->SetInputLayout(shader->GetInputLayout());
		m_cmd_list->SetSamplers(0, samplers);
		m_cmd_list->SetTextures(0, textures, 9);
		m_cmd_list->SetConstantBuffers(0, Buffer_Global, constant_buffers);
		m_cmd_list->SetBufferIndex(m_quad.GetIndexBuffer());
		m_cmd_list->SetBufferVertex(m_quad.GetVertexBuffer());
		m_cmd_list->DrawIndexed(Rectangle::GetIndexCount(), 0, 0);
		m_cmd_list->End();
		m_cmd_list->Submit();
	}

	void Renderer::Pass_Transparent(shared_ptr<RHI_Texture>& tex_out)
	{
		auto& entities_transparent = m_entities[Renderable_ObjectTransparent];
		if (entities_transparent.empty())
			return;

		// Prepare resources
		const auto& shader_transparent = static_pointer_cast<ShaderBuffered>(m_shaders[Shader_GizmoTransform_Vp]);
		void* textures[] = { m_g_buffer_depth->GetResource_Texture(), m_skybox ? m_skybox->GetTexture()->GetResource_Texture() : nullptr };

		// Begin command list
		m_cmd_list->Begin("Pass_Transparent");
		m_cmd_list->SetPrimitiveTopology(PrimitiveTopology_TriangleList);
		m_cmd_list->SetBlendState(m_blend_enabled);	
		m_cmd_list->SetDepthStencilState(m_depth_stencil_enabled);
		m_cmd_list->SetRenderTarget(tex_out, m_g_buffer_depth->GetResource_DepthStencil());
		m_cmd_list->SetViewport(tex_out->GetViewport());
		m_cmd_list->SetTextures(0, textures, 2);
		m_cmd_list->SetSampler(0, m_sampler_bilinear_clamp);
		m_cmd_list->SetShaderVertex(shader_transparent);
		m_cmd_list->SetInputLayout(shader_transparent->GetInputLayout());
		m_cmd_list->SetShaderPixel(shader_transparent);

		for (auto& entity : entities_transparent)
		{
			// Get renderable and material
			const auto renderable	= entity->GetRenderable_PtrRaw();
			auto material			= renderable ? renderable->GetMaterial().get() : nullptr;

			if (!renderable || !material)
				continue;

			// Get geometry
			const auto model = renderable->GeometryModel();
			if (!model || !model->GetVertexBuffer() || !model->GetIndexBuffer())
				continue;

			// Skip objects outside of the view frustum
			if (!m_camera->IsInViewFrustrum(renderable))
				continue;

			// Set the following per object
			m_cmd_list->SetRasterizerState(GetRasterizerState(material->GetCullMode(), Fill_Solid));
			m_cmd_list->SetBufferIndex(model->GetIndexBuffer());
			m_cmd_list->SetBufferVertex(model->GetVertexBuffer());

			// Constant buffer - TODO: Make per object
			auto buffer = Struct_Transparency
			(
				entity->GetTransform_PtrRaw()->GetMatrix(),
				m_view,
				m_projection,
				material->GetColorAlbedo(),
				m_camera->GetTransform()->GetPosition(),
				m_directional_light_avg_dir,
				material->GetMultiplier(TextureType_Roughness)
			);
			shader_transparent->UpdateBuffer(&buffer);
			m_cmd_list->SetConstantBuffer(1, Buffer_Global, shader_transparent->GetConstantBuffer());
			m_cmd_list->DrawIndexed(renderable->GeometryIndexCount(), renderable->GeometryIndexOffset(), renderable->GeometryVertexOffset());

			m_profiler->m_renderer_meshes_rendered++;

		} // ENTITY/MESH ITERATION

		m_cmd_list->End();
		m_cmd_list->Submit();
	}

	void Renderer::Pass_ShadowMapping(shared_ptr<RHI_Texture>& tex_out, Light* light)
	{
		if (!light || !light->GetCastShadows())
			return;

		// Get appropriate pixel shader
		shared_ptr<ShaderBuffered> pixel_shader;
		if (light->GetLightType() == LightType_Directional)
		{
			pixel_shader = static_pointer_cast<ShaderBuffered>(m_shaders[Shader_ShadowDirectional_Vp]);
		}
		else if (light->GetLightType() == LightType_Point)
		{
			pixel_shader = static_pointer_cast<ShaderBuffered>(m_shaders[Shader_ShadowPoint_P]);
		}
		else if (light->GetLightType() == LightType_Spot)
		{
			pixel_shader = static_pointer_cast<ShaderBuffered>(m_shaders[Shader_ShadowSpot_P]);
		}

		if (!pixel_shader->IsCompiled())
			return;

		m_cmd_list->Begin("Pass_ShadowMapping");
		
		// Prepare resources
		SetDefaultBuffer(tex_out->GetWidth(), tex_out->GetHeight(), m_view_projection_orthographic);
		auto buffer = Struct_ShadowMapping((m_view_projection).Inverted(), light);
		pixel_shader->UpdateBuffer(&buffer);
		vector<void*> constant_buffers	= { m_buffer_global->GetResource(), pixel_shader->GetConstantBuffer()->GetResource() };
		vector<void*> samplers			= { m_sampler_compare_depth->GetResource(), m_sampler_bilinear_clamp->GetResource() };
		void* textures[] =
		{
			m_g_buffer_normal->GetResource_Texture(),
			m_g_buffer_depth->GetResource_Texture(),
			light->GetLightType() == LightType_Directional	? light->GetShadowMap()->GetResource_Texture() : nullptr,
			light->GetLightType() == LightType_Point		? light->GetShadowMap()->GetResource_Texture() : nullptr,
			light->GetLightType() == LightType_Spot			? light->GetShadowMap()->GetResource_Texture() : nullptr
		};

		const auto& shader_vertex = m_shaders[Shader_ShadowDirectional_Vp];

		m_cmd_list->SetRenderTarget(tex_out);
		m_cmd_list->SetBlendState(m_blend_color_min);
		m_cmd_list->SetViewport(tex_out->GetViewport());
		m_cmd_list->SetShaderVertex(shader_vertex);
		m_cmd_list->SetInputLayout(shader_vertex->GetInputLayout());
		m_cmd_list->SetShaderPixel(pixel_shader);
		m_cmd_list->SetTextures(0, textures, 5);
		m_cmd_list->SetSamplers(0, samplers);
		m_cmd_list->SetConstantBuffers(0, Buffer_Global, constant_buffers);
		m_cmd_list->DrawIndexed(Rectangle::GetIndexCount(), 0, 0);
		m_cmd_list->End();
		m_cmd_list->Submit();
	}

	void Renderer::Pass_PostLight(shared_ptr<RHI_Texture>& tex_in, shared_ptr<RHI_Texture>& tex_out)
	{
		// Acquire shader
		const auto& shader_quad = m_shaders[Shader_Quad_V];
		if (!shader_quad->IsCompiled())
			return;

		// All post-process passes share the following, so set them once here
		m_cmd_list->Begin("Pass_PostLight");
		m_cmd_list->SetDepthStencilState(m_depth_stencil_disabled);
		m_cmd_list->SetRasterizerState(m_rasterizer_cull_back_solid);
		m_cmd_list->SetBlendState(m_blend_disabled);
		m_cmd_list->SetPrimitiveTopology(PrimitiveTopology_TriangleList);
		m_cmd_list->SetBufferVertex(m_quad.GetVertexBuffer());
		m_cmd_list->SetBufferIndex(m_quad.GetIndexBuffer());
		m_cmd_list->SetShaderVertex(shader_quad);
		m_cmd_list->SetInputLayout(shader_quad->GetInputLayout());

		// Render target swapping
		const auto swap_targets = [this, &tex_in, &tex_out]() { m_cmd_list->Submit(); tex_out.swap(tex_in); };

		// TAA	
		if (Flags_IsSet(Render_PostProcess_TAA))
		{
			Pass_TAA(tex_in, tex_out);
			swap_targets();
		}

		// Bloom
		if (Flags_IsSet(Render_PostProcess_Bloom))
		{
			Pass_Bloom(tex_in, tex_out);
			swap_targets();
		}

		// Motion Blur
		if (Flags_IsSet(Render_PostProcess_MotionBlur))
		{
			Pass_MotionBlur(tex_in, tex_out);
			swap_targets();
		}

		// Dithering
		if (Flags_IsSet(Render_PostProcess_Dithering))
		{
			Pass_Dithering(tex_in, tex_out);
			swap_targets();
		}

		// Tone-Mapping
		if (m_tonemapping != ToneMapping_Off)
		{
			Pass_ToneMapping(tex_in, tex_out);
			swap_targets();
		}

		// FXAA
		if (Flags_IsSet(Render_PostProcess_FXAA))
		{
			Pass_FXAA(tex_in, tex_out);
			swap_targets();
		}

		// Sharpening
		if (Flags_IsSet(Render_PostProcess_Sharpening))
		{
			Pass_Sharpening(tex_in, tex_out);
			swap_targets();
		}

		// Chromatic aberration
		if (Flags_IsSet(Render_PostProcess_ChromaticAberration))
		{
			Pass_ChromaticAberration(tex_in, tex_out);
			swap_targets();
		}

		// Gamma correction
		Pass_GammaCorrection(tex_in, tex_out);

		m_cmd_list->End();
		m_cmd_list->Submit();
	}

    void Renderer::Pass_SSAO(shared_ptr<RHI_Texture>& tex_out)
	{
		// Acquire shaders
		const auto& shader_quad = m_shaders[Shader_Quad_V];
		const auto& shader_ssao = m_shaders[Shader_Ssao_P];
		if (!shader_quad->IsCompiled() || !shader_ssao->IsCompiled())
			return;

		m_cmd_list->Begin("Pass_SSAO");

		// Prepare resources	
		void* textures[] = { m_g_buffer_normal->GetResource_Texture(), m_g_buffer_depth->GetResource_Texture(), m_tex_noise_normal->GetResource_Texture() };
		vector<void*> samplers = { m_sampler_bilinear_clamp->GetResource() /*SSAO (clamp) */, m_sampler_bilinear_wrap->GetResource() /*SSAO noise texture (wrap)*/};
		SetDefaultBuffer(tex_out->GetWidth(), tex_out->GetHeight());

		m_cmd_list->ClearTextures(); // avoids d3d11 warning where the render target is already bound as an input texture (from some previous pass)
		m_cmd_list->SetBlendState(m_blend_disabled);
		m_cmd_list->SetRenderTarget(tex_out);	
		m_cmd_list->SetViewport(tex_out->GetViewport());
		m_cmd_list->SetShaderVertex(shader_quad);
		m_cmd_list->SetInputLayout(shader_quad->GetInputLayout());
		m_cmd_list->SetShaderPixel(shader_ssao);
		m_cmd_list->SetTextures(0, textures, 3);
		m_cmd_list->SetSamplers(0, samplers);
		m_cmd_list->SetConstantBuffer(0, Buffer_Global, m_buffer_global);
		m_cmd_list->DrawIndexed(Rectangle::GetIndexCount(), 0, 0);
		m_cmd_list->End();
		m_cmd_list->Submit();
	}

    void Renderer::Pass_Upsample(shared_ptr<RHI_Texture>& tex_in, shared_ptr<RHI_Texture>& tex_out)
    {
        // Acquire shader
        const auto& shader_vertex   = m_shaders[Shader_Quad_V];
        const auto& shader_pixel    = m_shaders[Shader_UpsampleBox_P];
        if (!shader_vertex->IsCompiled() || !shader_pixel->IsCompiled())
            return;

        m_cmd_list->Begin("Upscale");
        SetDefaultBuffer(tex_out->GetWidth(), tex_out->GetHeight());
        m_cmd_list->SetRenderTarget(tex_out);
        m_cmd_list->SetViewport(tex_out->GetViewport());
        m_cmd_list->SetShaderVertex(shader_vertex);
        m_cmd_list->SetShaderPixel(shader_pixel);
        m_cmd_list->SetTexture(0, tex_in);
        m_cmd_list->SetSampler(0, m_sampler_bilinear_clamp);
        m_cmd_list->SetConstantBuffer(0, Buffer_Global, m_buffer_global);
        m_cmd_list->DrawIndexed(Rectangle::GetIndexCount(), 0, 0);
        m_cmd_list->End();
    }

	void Renderer::Pass_BlurBox(shared_ptr<RHI_Texture>& tex_in, shared_ptr<RHI_Texture>& tex_out, const float sigma)
	{
		// Acquire shader
		const auto& shader_blurBox = m_shaders[Shader_BlurBox_P];
		if (!shader_blurBox->IsCompiled())
			return;

		m_cmd_list->Begin("Pass_BlurBox");

		SetDefaultBuffer(tex_out->GetWidth(), tex_out->GetHeight());

		m_cmd_list->SetRenderTarget(tex_out);
		m_cmd_list->SetViewport(tex_out->GetViewport());
		m_cmd_list->SetShaderPixel(shader_blurBox);
		m_cmd_list->SetTexture(0, tex_in); // Shadows are in the alpha channel
		m_cmd_list->SetSampler(0, m_sampler_trilinear_clamp);
		m_cmd_list->SetConstantBuffer(0, Buffer_Global, m_buffer_global);
		m_cmd_list->DrawIndexed(Rectangle::GetIndexCount(), 0, 0);
		m_cmd_list->End();
		m_cmd_list->Submit();
	}

	void Renderer::Pass_BlurGaussian(shared_ptr<RHI_Texture>& tex_in, shared_ptr<RHI_Texture>& tex_out, const float sigma, const float pixel_stride)
	{
		if (tex_in->GetWidth() != tex_out->GetWidth() || tex_in->GetHeight() != tex_out->GetHeight() || tex_in->GetFormat() != tex_out->GetFormat())
		{
			LOG_ERROR("Invalid parameters, textures must match because they will get swapped");
			return;
		}

		SetDefaultBuffer(tex_in->GetWidth(), tex_in->GetHeight());

		auto shader_gaussian = static_pointer_cast<ShaderBuffered>(m_shaders[Shader_BlurGaussian_P]);

		// Start command list
		m_cmd_list->Begin("Pass_BlurGaussian");
		m_cmd_list->SetViewport(tex_out->GetViewport());
		m_cmd_list->SetShaderPixel(shader_gaussian);
		m_cmd_list->SetSampler(0, m_sampler_bilinear_clamp);
		m_cmd_list->SetConstantBuffer(0, Buffer_Global, m_buffer_global);

		// Horizontal Gaussian blur	
		m_cmd_list->Begin("Pass_BlurGaussian_Horizontal");
		{
			const auto direction	= Vector2(pixel_stride, 0.0f);
			auto buffer				= Struct_Blur(direction, sigma);
			shader_gaussian->UpdateBuffer(&buffer, 0);

			m_cmd_list->ClearTextures(); // avoids d3d11 warning where the render target is already bound as an input texture (from previous pass)
			m_cmd_list->SetRenderTarget(tex_out);
			m_cmd_list->SetTexture(0, tex_in);
			m_cmd_list->SetConstantBuffer(1, Buffer_PixelShader, shader_gaussian->GetConstantBuffer(0));
			m_cmd_list->DrawIndexed(Rectangle::GetIndexCount(), 0, 0);
		}
		m_cmd_list->End();

		// Vertical Gaussian blur
		m_cmd_list->Begin("Pass_BlurGaussian_Horizontal");
		{
			const auto direction	= Vector2(0.0f, pixel_stride);
			auto buffer				= Struct_Blur(direction, sigma);
			shader_gaussian->UpdateBuffer(&buffer, 1);

			m_cmd_list->ClearTextures(); // avoids d3d11 warning where the render target is already bound as an input texture (from previous pass)
			m_cmd_list->SetRenderTarget(tex_in);
			m_cmd_list->SetTexture(0, tex_out);
			m_cmd_list->SetConstantBuffer(1, Buffer_PixelShader, shader_gaussian->GetConstantBuffer(1));
			m_cmd_list->DrawIndexed(Rectangle::GetIndexCount(), 0, 0);
		}
		m_cmd_list->End();

		m_cmd_list->End();
		m_cmd_list->Submit();

		// Swap textures
		tex_in.swap(tex_out);
	}

	void Renderer::Pass_BlurBilateralGaussian(shared_ptr<RHI_Texture>& tex_in, shared_ptr<RHI_Texture>& tex_out, const float sigma, const float pixel_stride)
	{
		if (tex_in->GetWidth() != tex_out->GetWidth() || tex_in->GetHeight() != tex_out->GetHeight() || tex_in->GetFormat() != tex_out->GetFormat())
		{
			LOG_ERROR("Invalid parameters, textures must match because they will get swapped.");
			return;
		}

		// Acquire shaders
		const auto& shader_quad = m_shaders[Shader_Quad_V];
		auto shader_gaussianBilateral = static_pointer_cast<ShaderBuffered>(m_shaders[Shader_BlurGaussianBilateral_P]);
		if (!shader_quad->IsCompiled() || !shader_gaussianBilateral->IsCompiled())
			return;

		SetDefaultBuffer(tex_in->GetWidth(), tex_in->GetHeight());

		// Start command list
		m_cmd_list->Begin("Pass_BlurBilateralGaussian");
		m_cmd_list->SetViewport(tex_out->GetViewport());	
		m_cmd_list->SetShaderVertex(shader_quad);
		m_cmd_list->SetInputLayout(shader_quad->GetInputLayout());
		m_cmd_list->SetShaderPixel(shader_gaussianBilateral);	
		m_cmd_list->SetSampler(0, m_sampler_bilinear_clamp);
		m_cmd_list->SetConstantBuffer(0, Buffer_Global, m_buffer_global);

		// Horizontal Gaussian blur
		m_cmd_list->Begin("Pass_BlurBilateralGaussian_Horizontal");
		{
			// Prepare resources
			const auto direction	= Vector2(pixel_stride, 0.0f);
			auto buffer				= Struct_Blur(direction, sigma);
			shader_gaussianBilateral->UpdateBuffer(&buffer, 0);
			void* textures[] = { tex_in->GetResource_Texture(), m_g_buffer_depth->GetResource_Texture(), m_g_buffer_normal->GetResource_Texture() };
			
			m_cmd_list->ClearTextures(); // avoids d3d11 warning where render target is also bound as texture (from Pass_PreLight)
			m_cmd_list->SetRenderTarget(tex_out);
			m_cmd_list->SetTextures(0, textures, 3);
			m_cmd_list->SetConstantBuffer(1, Buffer_PixelShader, shader_gaussianBilateral->GetConstantBuffer(0));
			m_cmd_list->DrawIndexed(Rectangle::GetIndexCount(), 0, 0);
		}
		m_cmd_list->End();

		// Vertical Gaussian blur
		m_cmd_list->Begin("Pass_BlurBilateralGaussian_Vertical");
		{
			// Prepare resources
			const auto direction	= Vector2(0.0f, pixel_stride);
			auto buffer				= Struct_Blur(direction, sigma);
			shader_gaussianBilateral->UpdateBuffer(&buffer, 1);
			void* textures[] = { tex_out->GetResource_Texture(), m_g_buffer_depth->GetResource_Texture(), m_g_buffer_normal->GetResource_Texture() };

			m_cmd_list->ClearTextures(); // avoids d3d11 warning where render target is also bound as texture (from above pass)
			m_cmd_list->SetRenderTarget(tex_in);
			m_cmd_list->SetTextures(0, textures, 3);
			m_cmd_list->SetConstantBuffer(1, Buffer_PixelShader, shader_gaussianBilateral->GetConstantBuffer(1));
			m_cmd_list->DrawIndexed(Rectangle::GetIndexCount(), 0, 0);
		}
		m_cmd_list->End();

		m_cmd_list->End();
		m_cmd_list->Submit();

		tex_in.swap(tex_out);
	}

	void Renderer::Pass_TAA(shared_ptr<RHI_Texture>& tex_in, shared_ptr<RHI_Texture>& tex_out)
	{
		// Acquire shaders
		const auto& shader_taa = m_shaders[Shader_Taa_P];
		const auto& shader_texture = m_shaders[Shader_Texture_P];
		if (!shader_taa->IsCompiled() || !shader_texture->IsCompiled())
			return;

		m_cmd_list->Begin("Pass_TAA");

		// Resolve
		{
			// Prepare resources
			SetDefaultBuffer(m_render_tex_full_taa_current->GetWidth(), m_render_tex_full_taa_current->GetHeight());
			void* textures[] = { m_render_tex_full_taa_history->GetResource_Texture(), tex_in->GetResource_Texture(), m_g_buffer_velocity->GetResource_Texture(), m_g_buffer_depth->GetResource_Texture() };

			m_cmd_list->ClearTextures(); // avoids d3d11 warning where the render target is already bound as an input texture (from some previous pass)
			m_cmd_list->SetRenderTarget(m_render_tex_full_taa_current);
			m_cmd_list->SetViewport(m_render_tex_full_taa_current->GetViewport());
			m_cmd_list->SetShaderPixel(shader_taa);
			m_cmd_list->SetSampler(0, m_sampler_bilinear_clamp);
			m_cmd_list->SetTextures(0, textures, 3);
			m_cmd_list->SetConstantBuffer(0, Buffer_Global, m_buffer_global);
			m_cmd_list->DrawIndexed(Rectangle::GetIndexCount(), 0, 0);
		}

		// Output to texOut
		{
			// Prepare resources
			SetDefaultBuffer(tex_out->GetWidth(), tex_out->GetHeight());

			m_cmd_list->SetRenderTarget(tex_out);
			m_cmd_list->SetViewport(tex_out->GetViewport());
			m_cmd_list->SetShaderPixel(shader_texture);
			m_cmd_list->SetSampler(0, m_sampler_point_clamp);
			m_cmd_list->SetTexture(0, m_render_tex_full_taa_current);
			m_cmd_list->SetConstantBuffer(0, Buffer_Global, m_buffer_global);
			m_cmd_list->DrawIndexed(Rectangle::GetIndexCount(), 0, 0);
		}

		m_cmd_list->End();
		m_cmd_list->Submit();

		// Swap textures so current becomes history
		m_render_tex_full_taa_current.swap(m_render_tex_full_taa_history);
	}

	void Renderer::Pass_Bloom(shared_ptr<RHI_Texture>& tex_in, shared_ptr<RHI_Texture>& tex_out)
	{
		// Acquire shaders		
		const auto& shader_bloomBright		= m_shaders[Shader_BloomLuminance_P];
		const auto& shader_bloomBlend		= m_shaders[Shader_BloomBlend_P];
		const auto& shader_downsampleBox	= m_shaders[Shader_DownsampleBox_P];
		const auto& shader_upsampleBox		= m_shaders[Shader_UpsampleBox_P];
		if (!shader_downsampleBox->IsCompiled() || !shader_bloomBright->IsCompiled() || !shader_upsampleBox->IsCompiled() || !shader_downsampleBox->IsCompiled())
			return;

		m_cmd_list->Begin("Pass_Bloom");
		m_cmd_list->SetSampler(0, m_sampler_bilinear_clamp);

        m_cmd_list->Begin("Luminance");
        {
            // Prepare resources
            SetDefaultBuffer(m_render_tex_bloom[0]->GetWidth(), m_render_tex_bloom[0]->GetHeight());

            m_cmd_list->SetRenderTarget(m_render_tex_bloom[0]);
            m_cmd_list->SetViewport(m_render_tex_bloom[0]->GetViewport());
            m_cmd_list->SetShaderPixel(shader_bloomBright);
            m_cmd_list->SetTexture(0, tex_in);
            m_cmd_list->SetConstantBuffer(0, Buffer_Global, m_buffer_global);
            m_cmd_list->DrawIndexed(Rectangle::GetIndexCount(), 0, 0);
        }
        m_cmd_list->End();

        auto downsample = [this, &shader_downsampleBox](shared_ptr<RHI_Texture>& tex_in, shared_ptr<RHI_Texture>& tex_out)
        {
		    m_cmd_list->Begin("Downsample");
		    {
		    	// Prepare resources
		    	SetDefaultBuffer(tex_out->GetWidth(), tex_out->GetHeight()); 

		    	m_cmd_list->SetRenderTarget(tex_out);
		    	m_cmd_list->SetViewport(tex_out->GetViewport());
		    	m_cmd_list->SetShaderPixel(shader_downsampleBox);
		    	m_cmd_list->SetTexture(0, tex_in);
		    	m_cmd_list->SetConstantBuffer(0, Buffer_Global, m_buffer_global);
		    	m_cmd_list->DrawIndexed(Rectangle::GetIndexCount(), 0, 0);
		    }
		    m_cmd_list->End();
            m_cmd_list->Submit(); // we have to submit because all downsample passes are using the same buffer
        };

        auto upsample = [this, &shader_upsampleBox](shared_ptr<RHI_Texture>& tex_in, shared_ptr<RHI_Texture>& tex_out)
        {
            m_cmd_list->Begin("Upsample");
            {
                SetDefaultBuffer(tex_out->GetWidth(), tex_out->GetHeight());

                m_cmd_list->SetBlendState(m_blend_color_max);
                m_cmd_list->SetRenderTarget(tex_out);
                m_cmd_list->SetViewport(tex_out->GetViewport());
                m_cmd_list->SetShaderPixel(shader_upsampleBox);
                m_cmd_list->SetTexture(0, tex_in);
                m_cmd_list->SetConstantBuffer(0, Buffer_Global, m_buffer_global);
                m_cmd_list->DrawIndexed(Rectangle::GetIndexCount(), 0, 0);
            }
            m_cmd_list->End();
            m_cmd_list->Submit(); // we have to submit because all upsample passes are using the same buffer
        };

        // Downsample
        // The last bloom texture is the same size as the previous one (it's used for the Gaussian pass below), so we skip it
        for (int i = 0; i < static_cast<int>(m_render_tex_bloom.size() - 1); i++)
        {
            downsample(m_render_tex_bloom[i], m_render_tex_bloom[i + 1]);
        }

		// Upsample + blend
        for (int i = static_cast<int>(m_render_tex_bloom.size() - 1); i > 0; i--)
        {
            upsample(m_render_tex_bloom[i], m_render_tex_bloom[i - 1]);
        }
		
		m_cmd_list->Begin("Additive_Blending");
		{
			// Prepare resources
			SetDefaultBuffer(tex_out->GetWidth(), tex_out->GetHeight());
			void* textures[] = { tex_in->GetResource_Texture(), m_render_tex_bloom.front()->GetResource_Texture() };

            m_cmd_list->SetBlendState(m_blend_disabled);
			m_cmd_list->SetRenderTarget(tex_out);
			m_cmd_list->SetViewport(tex_out->GetViewport());
			m_cmd_list->SetShaderPixel(shader_bloomBlend);
			m_cmd_list->SetTextures(0, textures, 2);
			m_cmd_list->SetConstantBuffer(0, Buffer_Global, m_buffer_global);
			m_cmd_list->DrawIndexed(Rectangle::GetIndexCount(), 0, 0);
		}
		m_cmd_list->End();

		m_cmd_list->End();
		m_cmd_list->Submit();
	}

	void Renderer::Pass_ToneMapping(shared_ptr<RHI_Texture>& tex_in, shared_ptr<RHI_Texture>& tex_out)
	{
		// Acquire shader
		const auto& shader_toneMapping = m_shaders[Shader_ToneMapping_P];
		if (!shader_toneMapping->IsCompiled())
			return;

		m_cmd_list->Begin("Pass_ToneMapping");

		// Prepare resources
		SetDefaultBuffer(tex_out->GetWidth(), tex_out->GetHeight());

		m_cmd_list->ClearTextures(); // avoids d3d11 warning where the render target is already bound as an input texture (from previous pass)
		m_cmd_list->SetRenderTarget(tex_out);
		m_cmd_list->SetViewport(tex_out->GetViewport());
		m_cmd_list->SetShaderPixel(shader_toneMapping);
		m_cmd_list->SetTexture(0, tex_in);
		m_cmd_list->SetSampler(0, m_sampler_point_clamp);
		m_cmd_list->SetConstantBuffer(0, Buffer_Global, m_buffer_global);
		m_cmd_list->DrawIndexed(Rectangle::GetIndexCount(), 0, 0);
		m_cmd_list->End();
		m_cmd_list->Submit();
	}

	void Renderer::Pass_GammaCorrection(shared_ptr<RHI_Texture>& tex_in, shared_ptr<RHI_Texture>& tex_out)
	{
		// Acquire shader
		const auto& shader_gammaCorrection = m_shaders[Shader_GammaCorrection_P];
		if (!shader_gammaCorrection->IsCompiled())
			return;

		m_cmd_list->Begin("Pass_GammaCorrection");

		// Prepare resources
		SetDefaultBuffer(tex_out->GetWidth(), tex_out->GetHeight());

		m_cmd_list->ClearTextures(); // avoids d3d11 warning where the render target is already bound as an input texture (from previous pass)
		m_cmd_list->SetRenderTarget(tex_out);
		m_cmd_list->SetViewport(tex_out->GetViewport());
		m_cmd_list->SetShaderPixel(shader_gammaCorrection);
		m_cmd_list->SetTexture(0, tex_in);
		m_cmd_list->SetSampler(0, m_sampler_point_clamp);
		m_cmd_list->SetConstantBuffer(0, Buffer_Global, m_buffer_global);
		m_cmd_list->DrawIndexed(Rectangle::GetIndexCount(), 0, 0);
		m_cmd_list->End();
		m_cmd_list->Submit();
	}

	void Renderer::Pass_FXAA(shared_ptr<RHI_Texture>& tex_in, shared_ptr<RHI_Texture>& tex_out)
	{
		// Acquire shaders
		const auto& shader_luma = m_shaders[Shader_Luma_P];
		const auto& shader_fxaa = m_shaders[Shader_Fxaa_P];
		if (!shader_luma->IsCompiled() || !shader_fxaa->IsCompiled())
			return;

		m_cmd_list->Begin("Pass_FXAA");

		// Prepare resources
		SetDefaultBuffer(tex_out->GetWidth(), tex_out->GetHeight());

		m_cmd_list->ClearTextures(); // avoids d3d11 warning where the render target is already bound as an input texture (from previous pass)
		m_cmd_list->SetRenderTarget(tex_out);
		m_cmd_list->SetViewport(tex_out->GetViewport());
		m_cmd_list->SetSampler(0, m_sampler_bilinear_clamp);
		m_cmd_list->SetConstantBuffer(0, Buffer_Global, m_buffer_global);

		// Luma
		m_cmd_list->SetRenderTarget(tex_out);	
		m_cmd_list->SetShaderPixel(shader_luma);
		m_cmd_list->SetTexture(0, tex_in);
		m_cmd_list->DrawIndexed(Rectangle::GetIndexCount(), 0, 0);

		// FXAA
		m_cmd_list->SetRenderTarget(tex_in);
		m_cmd_list->SetShaderPixel(shader_fxaa);
		m_cmd_list->SetTexture(0, tex_out);
		m_cmd_list->DrawIndexed(Rectangle::GetIndexCount(), 0, 0);

		m_cmd_list->End();
		m_cmd_list->Submit();

		// Swap the textures
		tex_in.swap(tex_out);
	}

	void Renderer::Pass_ChromaticAberration(shared_ptr<RHI_Texture>& tex_in, shared_ptr<RHI_Texture>& tex_out)
	{
		// Acquire shader
		const auto& shader_chromaticAberration = m_shaders[Shader_ChromaticAberration_P];
		if (!shader_chromaticAberration->IsCompiled())
			return;

		m_cmd_list->Begin("Pass_ChromaticAberration");

		// Prepare resources
		SetDefaultBuffer(tex_out->GetWidth(), tex_out->GetHeight());

		m_cmd_list->ClearTextures(); // avoids d3d11 warning where the render target is already bound as an input texture (from previous pass)
		m_cmd_list->SetRenderTarget(tex_out);
		m_cmd_list->SetViewport(tex_out->GetViewport());
		m_cmd_list->SetShaderPixel(shader_chromaticAberration);
		m_cmd_list->SetTexture(0, tex_in);
		m_cmd_list->SetSampler(0, m_sampler_bilinear_clamp);
		m_cmd_list->SetConstantBuffer(0, Buffer_Global, m_buffer_global);
		m_cmd_list->DrawIndexed(Rectangle::GetIndexCount(), 0, 0);
		m_cmd_list->End();
		m_cmd_list->Submit();
	}

	void Renderer::Pass_MotionBlur(shared_ptr<RHI_Texture>& tex_in, shared_ptr<RHI_Texture>& tex_out)
	{
		// Acquire shader
		const auto& shader_motionBlur = m_shaders[Shader_MotionBlur_P];
		if (!shader_motionBlur->IsCompiled())
			return;

		m_cmd_list->Begin("Pass_MotionBlur");

		// Prepare resources
		void* textures[] = { tex_in->GetResource_Texture(), m_g_buffer_velocity->GetResource_Texture(), m_g_buffer_depth->GetResource_Texture() };
		SetDefaultBuffer(tex_out->GetWidth(), tex_out->GetHeight());

		m_cmd_list->ClearTextures(); // avoids d3d11 warning where the render target is already bound as an input texture (from previous pass)
		m_cmd_list->SetRenderTarget(tex_out);
		m_cmd_list->SetViewport(tex_out->GetViewport());
		m_cmd_list->SetShaderPixel(shader_motionBlur);
		m_cmd_list->SetSampler(0, m_sampler_bilinear_clamp);
		m_cmd_list->SetTextures(0, textures, 2);
		m_cmd_list->SetConstantBuffer(0, Buffer_Global, m_buffer_global);
		m_cmd_list->DrawIndexed(Rectangle::GetIndexCount(), 0, 0);
		m_cmd_list->End();
		m_cmd_list->Submit();
	}

	void Renderer::Pass_Dithering(shared_ptr<RHI_Texture>& tex_in, shared_ptr<RHI_Texture>& tex_out)
	{
		// Acquire shader
		const auto& shader_dithering = m_shaders[Shader_Dithering_P];
		if (!shader_dithering->IsCompiled())
			return;

		m_cmd_list->Begin("Pass_Dithering");

		// Prepare resources
		SetDefaultBuffer(tex_out->GetWidth(), tex_out->GetHeight());

		m_cmd_list->ClearTextures(); // avoids d3d11 warning where the render target is already bound as an input texture (from previous pass)
		m_cmd_list->SetRenderTarget(tex_out);
		m_cmd_list->SetViewport(tex_out->GetViewport());
		m_cmd_list->SetShaderPixel(shader_dithering);
		m_cmd_list->SetSampler(0, m_sampler_point_clamp);
		m_cmd_list->SetTexture(0, tex_in);
		m_cmd_list->SetConstantBuffer(0, Buffer_Global, m_buffer_global);
		m_cmd_list->DrawIndexed(Rectangle::GetIndexCount(), 0, 0);
		m_cmd_list->End();
		m_cmd_list->Submit();
	}

	void Renderer::Pass_Sharpening(shared_ptr<RHI_Texture>& tex_in, shared_ptr<RHI_Texture>& tex_out)
	{
		// Acquire shader
		const auto& shader_sharperning = m_shaders[Shader_Sharperning_P];
		if (!shader_sharperning->IsCompiled())
			return;

		m_cmd_list->Begin("Pass_Sharpening");

		// Prepare resources
		SetDefaultBuffer(tex_out->GetWidth(), tex_out->GetHeight());
	
		m_cmd_list->ClearTextures(); // avoids d3d11 warning where the render target is already bound as an input texture (from previous pass)
		m_cmd_list->SetRenderTarget(tex_out);
		m_cmd_list->SetViewport(tex_out->GetViewport());		
		m_cmd_list->SetShaderPixel(shader_sharperning);
		m_cmd_list->SetTexture(0, tex_in);
		m_cmd_list->SetSampler(0, m_sampler_bilinear_clamp);
		m_cmd_list->SetConstantBuffer(0, Buffer_Global, m_buffer_global);
		m_cmd_list->DrawIndexed(Rectangle::GetIndexCount(), 0, 0);
		m_cmd_list->End();
		m_cmd_list->Submit();
	}

	void Renderer::Pass_Lines(shared_ptr<RHI_Texture>& tex_out)
	{
		const bool draw_picking_ray = m_flags & Render_Gizmo_PickingRay;
		const bool draw_aabb		= m_flags & Render_Gizmo_AABB;
		const bool draw_grid		= m_flags & Render_Gizmo_Grid;
		const auto draw_lines		= !m_lines_list_depth_enabled.empty() || !m_lines_list_depth_disabled.empty(); // Any kind of lines, physics, user debug, etc.
		const auto draw				= draw_picking_ray || draw_aabb || draw_grid || draw_lines;
		if (!draw)
			return;

		m_cmd_list->Begin("Pass_Lines");

		// Generate lines for debug primitives offered by the renderer
		{
			// Picking ray
			if (draw_picking_ray)
			{
				const auto& ray = m_camera->GetPickingRay();
				DrawLine(ray.GetStart(), ray.GetStart() + ray.GetDirection() * m_camera->GetFarPlane(), Vector4(0, 1, 0, 1));
			}

			// AABBs
			if (draw_aabb)
			{
				for (const auto& entity : m_entities[Renderable_ObjectOpaque])
				{
					if (auto renderable = entity->GetRenderable_PtrRaw())
					{
						DrawBox(renderable->GetAabb(), Vector4(0.41f, 0.86f, 1.0f, 1.0f));
					}
				}

				for (const auto& entity : m_entities[Renderable_ObjectTransparent])
				{
					if (auto renderable = entity->GetRenderable_PtrRaw())
					{
						DrawBox(renderable->GetAabb(), Vector4(0.41f, 0.86f, 1.0f, 1.0f));
					}
				}
			}
		}

		const auto& shader_color = m_shaders[Shader_Color_Vp];

		// Begin command list
		m_cmd_list->SetViewport(tex_out->GetViewport());
		m_cmd_list->SetRasterizerState(m_rasterizer_cull_back_wireframe);
		m_cmd_list->SetBlendState(m_blend_disabled);
		m_cmd_list->SetPrimitiveTopology(PrimitiveTopology_LineList);
		m_cmd_list->SetShaderVertex(shader_color);
		m_cmd_list->SetShaderPixel(shader_color);
		m_cmd_list->SetInputLayout(shader_color->GetInputLayout());
		m_cmd_list->SetSampler(0, m_sampler_point_clamp);

        // unjittered matrix to avoid TAA jitter due to lack of motion vectors (line rendering is anti-aliased by m_rasterizer_cull_back_wireframe, decently)
        const auto view_projection_unjittered = m_camera->GetViewMatrix() * m_camera->GetProjectionMatrix();

		// Draw lines that require depth
		m_cmd_list->SetDepthStencilState(m_depth_stencil_enabled);
		m_cmd_list->SetRenderTarget(tex_out, m_g_buffer_depth->GetResource_DepthStencil());
		{
			// Grid
			if (draw_grid)
			{
				SetDefaultBuffer
				(
					static_cast<uint32_t>(m_resolution.x),
					static_cast<uint32_t>(m_resolution.y),
					m_gizmo_grid->ComputeWorldMatrix(m_camera->GetTransform()) * view_projection_unjittered
				);
				m_cmd_list->SetBufferIndex(m_gizmo_grid->GetIndexBuffer());
				m_cmd_list->SetBufferVertex(m_gizmo_grid->GetVertexBuffer());
				m_cmd_list->SetBlendState(m_blend_enabled);
				m_cmd_list->SetConstantBuffer(0, Buffer_Global, m_buffer_global);
				m_cmd_list->DrawIndexed(m_gizmo_grid->GetIndexCount(), 0, 0);
			}

			// Lines
			const auto line_vertex_buffer_size = static_cast<uint32_t>(m_lines_list_depth_enabled.size());
			if (line_vertex_buffer_size != 0)
			{
				// Grow vertex buffer (if needed)
				if (line_vertex_buffer_size > m_vertex_buffer_lines->GetVertexCount())
				{
					m_vertex_buffer_lines->CreateDynamic<RHI_Vertex_PosCol>(line_vertex_buffer_size);
				}

				// Update vertex buffer
				const auto buffer = static_cast<RHI_Vertex_PosCol*>(m_vertex_buffer_lines->Map());
				copy(m_lines_list_depth_enabled.begin(), m_lines_list_depth_enabled.end(), buffer);
				m_vertex_buffer_lines->Unmap();

				SetDefaultBuffer(static_cast<uint32_t>(m_resolution.x), static_cast<uint32_t>(m_resolution.y), view_projection_unjittered);
				m_cmd_list->SetBufferVertex(m_vertex_buffer_lines);
				m_cmd_list->SetConstantBuffer(0, Buffer_Global, m_buffer_global);
				m_cmd_list->Draw(line_vertex_buffer_size);

				m_lines_list_depth_enabled.clear();
			}
		}

		// Draw lines that don't require depth
		m_cmd_list->SetDepthStencilState(m_depth_stencil_disabled);
		m_cmd_list->SetRenderTarget(tex_out);
		{
			// Lines
			const auto line_vertex_buffer_size = static_cast<uint32_t>(m_lines_list_depth_disabled.size());
			if (line_vertex_buffer_size != 0)
			{
				// Grow vertex buffer (if needed)
				if (line_vertex_buffer_size > m_vertex_buffer_lines->GetVertexCount())
				{
					m_vertex_buffer_lines->CreateDynamic<RHI_Vertex_PosCol>(line_vertex_buffer_size);
				}

				// Update vertex buffer
				const auto buffer = static_cast<RHI_Vertex_PosCol*>(m_vertex_buffer_lines->Map());
				copy(m_lines_list_depth_disabled.begin(), m_lines_list_depth_disabled.end(), buffer);
				m_vertex_buffer_lines->Unmap();

				// Set pipeline state
				m_cmd_list->SetBufferVertex(m_vertex_buffer_lines);
				SetDefaultBuffer(static_cast<uint32_t>(m_resolution.x), static_cast<uint32_t>(m_resolution.y), view_projection_unjittered);
				m_cmd_list->Draw(line_vertex_buffer_size);

				m_lines_list_depth_disabled.clear();
			}
		}

		m_cmd_list->End();
		m_cmd_list->Submit();
	}

	void Renderer::Pass_Gizmos(shared_ptr<RHI_Texture>& tex_out)
	{
		bool render_lights		= m_flags & Render_Gizmo_Lights;
		bool render_transform	= m_flags & Render_Gizmo_Transform;
		auto render				= render_lights || render_transform;
		if (!render)
			return;

		// Acquire shader
		const auto& shader_quad = m_shaders[Shader_Quad_V];
		if (!shader_quad->IsCompiled())
			return;

		m_cmd_list->Begin("Pass_Gizmos");
		m_cmd_list->SetDepthStencilState(m_depth_stencil_disabled);
		m_cmd_list->SetRasterizerState(m_rasterizer_cull_back_solid);
		m_cmd_list->SetBlendState(m_blend_enabled);
		m_cmd_list->SetPrimitiveTopology(PrimitiveTopology_TriangleList);
		m_cmd_list->SetViewport(tex_out->GetViewport());	
		m_cmd_list->SetRenderTarget(tex_out);

		auto& lights = m_entities[Renderable_Light];
		if (render_lights && !lights.empty())
		{
			m_cmd_list->Begin("Pass_Gizmos_Lights");

			for (const auto& entity : lights)
			{
				auto position_light_world		= entity->GetTransform_PtrRaw()->GetPosition();
				auto position_camera_world		= m_camera->GetTransform()->GetPosition();
				auto direction_camera_to_light	= (position_light_world - position_camera_world).Normalized();
				auto v_dot_l					= Vector3::Dot(m_camera->GetTransform()->GetForward(), direction_camera_to_light);

				// Don't bother drawing if out of view
				if (v_dot_l <= 0.5f)
					continue;

				// Compute light screen space position and scale (based on distance from the camera)
				auto position_light_screen	= m_camera->WorldToScreenPoint(position_light_world);
				auto distance				= (position_camera_world - position_light_world).Length() + M_EPSILON;
				auto scale					= GIZMO_MAX_SIZE / distance;
				scale						= Clamp(scale, GIZMO_MIN_SIZE, GIZMO_MAX_SIZE);

				// Choose texture based on light type
				shared_ptr<RHI_Texture> light_tex = nullptr;
				auto type = entity->GetComponent<Light>()->GetLightType();
				if (type == LightType_Directional)	light_tex = m_gizmo_tex_light_directional;
				else if (type == LightType_Point)	light_tex = m_gizmo_tex_light_point;
				else if (type == LightType_Spot)	light_tex = m_gizmo_tex_light_spot;

				// Construct appropriate rectangle
				auto tex_width = light_tex->GetWidth() * scale;
				auto tex_height = light_tex->GetHeight() * scale;
				auto rectangle = Math::Rectangle(position_light_screen.x - tex_width * 0.5f, position_light_screen.y - tex_height * 0.5f, tex_width, tex_height);
				if (rectangle != m_gizmo_light_rect)
				{
					m_gizmo_light_rect = rectangle;
					m_gizmo_light_rect.CreateBuffers(this);
				}

				SetDefaultBuffer(static_cast<uint32_t>(tex_width), static_cast<uint32_t>(tex_width), m_view_projection_orthographic);

				m_cmd_list->SetShaderVertex(shader_quad);
				m_cmd_list->SetInputLayout(shader_quad->GetInputLayout());
				m_cmd_list->SetShaderPixel(m_shaders[Shader_Texture_P]);
				m_cmd_list->SetSampler(0, m_sampler_bilinear_clamp);
				m_cmd_list->SetConstantBuffer(0, Buffer_Global, m_buffer_global);
				m_cmd_list->SetTexture(0, light_tex);
				m_cmd_list->SetBufferIndex(m_gizmo_light_rect.GetIndexBuffer());
				m_cmd_list->SetBufferVertex(m_gizmo_light_rect.GetVertexBuffer());
				m_cmd_list->DrawIndexed(Rectangle::GetIndexCount(), 0, 0);			
				m_cmd_list->Submit();
			}
			m_cmd_list->End();
		}

		// Transform
		if (render_transform && m_gizmo_transform->Update(m_camera.get(), m_gizmo_transform_size, m_gizmo_transform_speed))
		{
			m_cmd_list->Begin("Pass_Gizmos_Transform");

			SetDefaultBuffer(static_cast<uint32_t>(m_resolution.x), static_cast<uint32_t>(m_resolution.y), m_view_projection_orthographic);

			auto const& shader_gizmoTransform = static_pointer_cast<ShaderBuffered>(m_shaders[Shader_GizmoTransform_Vp]);

			m_cmd_list->SetShaderVertex(shader_gizmoTransform);
			m_cmd_list->SetShaderPixel(shader_gizmoTransform);
			m_cmd_list->SetInputLayout(shader_gizmoTransform->GetInputLayout());
			m_cmd_list->SetBufferIndex(m_gizmo_transform->GetIndexBuffer());
			m_cmd_list->SetBufferVertex(m_gizmo_transform->GetVertexBuffer());
			m_cmd_list->SetConstantBuffer(0, Buffer_Global, m_buffer_global);

			// Axis - X
			auto buffer = Struct_Matrix_Vector3(m_gizmo_transform->GetHandle().GetTransform(Vector3::Right), m_gizmo_transform->GetHandle().GetColor(Vector3::Right));
			shader_gizmoTransform->UpdateBuffer(&buffer, 0);
			m_cmd_list->SetConstantBuffer(1, Buffer_Global, shader_gizmoTransform->GetConstantBuffer(0));
			m_cmd_list->DrawIndexed(m_gizmo_transform->GetIndexCount(), 0, 0);

			// Axis - Y
			buffer = Struct_Matrix_Vector3(m_gizmo_transform->GetHandle().GetTransform(Vector3::Up), m_gizmo_transform->GetHandle().GetColor(Vector3::Up));
			shader_gizmoTransform->UpdateBuffer(&buffer, 1);
			m_cmd_list->SetConstantBuffer(1, Buffer_Global, shader_gizmoTransform->GetConstantBuffer(1));
			m_cmd_list->DrawIndexed(m_gizmo_transform->GetIndexCount(), 0, 0);

			// Axis - Z
			buffer = Struct_Matrix_Vector3(m_gizmo_transform->GetHandle().GetTransform(Vector3::Forward), m_gizmo_transform->GetHandle().GetColor(Vector3::Forward));
			shader_gizmoTransform->UpdateBuffer(&buffer, 2);
			m_cmd_list->SetConstantBuffer(1, Buffer_Global, shader_gizmoTransform->GetConstantBuffer(2));
			m_cmd_list->DrawIndexed(m_gizmo_transform->GetIndexCount(), 0, 0);

			// Axes - XYZ
			if (m_gizmo_transform->DrawXYZ())
			{
				buffer = Struct_Matrix_Vector3(m_gizmo_transform->GetHandle().GetTransform(Vector3::One), m_gizmo_transform->GetHandle().GetColor(Vector3::One));
				shader_gizmoTransform->UpdateBuffer(&buffer, 3);
				m_cmd_list->SetConstantBuffer(1, Buffer_Global, shader_gizmoTransform->GetConstantBuffer(3));
				m_cmd_list->DrawIndexed(m_gizmo_transform->GetIndexCount(), 0, 0);
			}

			m_cmd_list->End();
		}

		m_cmd_list->End();
		m_cmd_list->Submit();
	}

	void Renderer::Pass_PerformanceMetrics(shared_ptr<RHI_Texture>& tex_out)
	{
		const bool draw = m_flags & Render_Gizmo_PerformanceMetrics;
		if (!draw)
			return;

		const auto& shader_font = static_pointer_cast<ShaderBuffered>(m_shaders[Shader_Font_Vp]);

		m_cmd_list->Begin("Pass_PerformanceMetrics");

		// Updated text
		const auto text_pos = Vector2(-static_cast<int>(m_viewport.width) * 0.5f + 1.0f, static_cast<int>(m_viewport.height) * 0.5f);
		m_font->SetText(m_profiler->GetMetrics(), text_pos);
		auto buffer = Struct_Matrix_Vector4(m_view_projection_orthographic, m_font->GetColor());
		shader_font->UpdateBuffer(&buffer);
	
		m_cmd_list->SetDepthStencilState(m_depth_stencil_disabled);
		m_cmd_list->SetRasterizerState(m_rasterizer_cull_back_solid);
		m_cmd_list->SetPrimitiveTopology(PrimitiveTopology_TriangleList);
		m_cmd_list->SetRenderTarget(tex_out);	
		m_cmd_list->SetViewport(tex_out->GetViewport());
		m_cmd_list->SetBlendState(m_blend_enabled);	
		m_cmd_list->SetTexture(0, m_font->GetAtlas());
		m_cmd_list->SetSampler(0, m_sampler_bilinear_clamp);
		m_cmd_list->SetConstantBuffer(0, Buffer_Global, shader_font->GetConstantBuffer());
		m_cmd_list->SetShaderVertex(shader_font);
		m_cmd_list->SetShaderPixel(shader_font);
		m_cmd_list->SetInputLayout(shader_font->GetInputLayout());	
		m_cmd_list->SetBufferIndex(m_font->GetIndexBuffer());
		m_cmd_list->SetBufferVertex(m_font->GetVertexBuffer());
		m_cmd_list->DrawIndexed(m_font->GetIndexCount(), 0, 0);
		m_cmd_list->End();
		m_cmd_list->Submit();
	}

	bool Renderer::Pass_DebugBuffer(shared_ptr<RHI_Texture>& tex_out)
	{
		if (m_debug_buffer == RendererDebug_None)
			return true;

		// Acquire shader
		const auto& shader_quad = m_shaders[Shader_Quad_V];
		if (!shader_quad->IsCompiled())
			return false;

		m_cmd_list->Begin("Pass_DebugBuffer");

		SetDefaultBuffer(tex_out->GetWidth(), tex_out->GetHeight(), m_view_projection_orthographic);

		// Bind correct texture & shader pass
		if (m_debug_buffer == RendererDebug_Albedo)
		{
			m_cmd_list->SetTexture(0, m_g_buffer_albedo);
			m_cmd_list->SetShaderPixel(m_shaders[Shader_Texture_P]);
		}

		if (m_debug_buffer == RendererDebug_Normal)
		{
			m_cmd_list->SetTexture(0, m_g_buffer_normal);
			m_cmd_list->SetShaderPixel(m_shaders[Shader_DebugNormal_P]);
		}

		if (m_debug_buffer == RendererDebug_Material)
		{
			m_cmd_list->SetTexture(0, m_g_buffer_material);
			m_cmd_list->SetShaderPixel(m_shaders[Shader_Texture_P]);
		}

		if (m_debug_buffer == RendererDebug_Velocity)
		{
			m_cmd_list->SetTexture(0, m_g_buffer_velocity);
			m_cmd_list->SetShaderPixel(m_shaders[Shader_DebugVelocity_P]);
		}

		if (m_debug_buffer == RendererDebug_Depth)
		{
			m_cmd_list->SetTexture(0, m_g_buffer_depth);
			m_cmd_list->SetShaderPixel(m_shaders[Shader_DebugDepth_P]);
		}

		if ((m_debug_buffer == RendererDebug_SSAO))
		{
			if (Flags_IsSet(Render_PostProcess_SSAO))
			{
				m_cmd_list->SetTexture(0, m_render_tex_full_ssao);
			}
			else
			{
				m_cmd_list->SetTexture(0, m_tex_white);
			}
			m_cmd_list->SetShaderPixel(m_shaders[Shader_DebugSsao_P]);
		}

		m_cmd_list->SetDepthStencilState(m_depth_stencil_disabled);
		m_cmd_list->SetRasterizerState(m_rasterizer_cull_back_solid);
		m_cmd_list->SetBlendState(m_blend_disabled);
		m_cmd_list->SetPrimitiveTopology(PrimitiveTopology_TriangleList);
		m_cmd_list->SetRenderTarget(tex_out);
		m_cmd_list->SetViewport(tex_out->GetViewport());	
		m_cmd_list->SetShaderVertex(shader_quad);
		m_cmd_list->SetInputLayout(shader_quad->GetInputLayout());
		m_cmd_list->SetSampler(0, m_sampler_bilinear_clamp);
		m_cmd_list->SetConstantBuffer(0, Buffer_Global, m_buffer_global);
		m_cmd_list->SetBufferVertex(m_quad.GetVertexBuffer());
		m_cmd_list->SetBufferIndex(m_quad.GetIndexBuffer());
		m_cmd_list->DrawIndexed(Rectangle::GetIndexCount(), 0, 0);
		m_cmd_list->End();
		m_cmd_list->Submit();

		return true;
	}
}
