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
#include "Deferred/ShaderVariation.h"
#include "Deferred/ShaderLight.h"
#include "Gizmos/Grid.h"
#include "Gizmos/Transform_Gizmo.h"
#include "Font/Font.h"
#include "../Profiling/Profiler.h"
#include "../Resource/IResource.h"
#include "../RHI/RHI_Device.h"
#include "../RHI/RHI_VertexBuffer.h"
#include "../RHI/RHI_RenderTexture.h"
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

//= NAMESPACES ================
using namespace std;
using namespace Directus::Math;
using namespace Helper;
//=============================

#define GIZMO_MAX_SIZE 5.0f
#define GIZMO_MIN_SIZE 0.1f

namespace Directus
{
	void Renderer::Pass_DepthDirectionalLight(Light* light_directional)
	{
		// Validate light
		if (!light_directional || !light_directional->GetCastShadows())
			return;

		// Validate light's shadow map
		auto& shadow_map = light_directional->GetShadowMap();
		if (!shadow_map)
			return;

		// Validate entities
		auto& entities = m_entities[Renderable_ObjectOpaque];
		if (entities.empty())
			return;

		TIME_BLOCK_START_MULTI(m_profiler);
		m_rhi_device->EventBegin("Pass_DepthDirectionalLight");

		// Set common states	
		SetDefaultPipelineState();
		m_rhi_pipeline->SetShader(m_vps_depth);
		m_rhi_pipeline->SetViewport(shadow_map->GetViewport());
		m_rhi_pipeline->SetDepthStencilState(m_depth_stencil_enabled);

		// Variables that help reduce state changes
		unsigned int currently_bound_geometry = 0;
		for (unsigned int i = 0; i < light_directional->GetShadowMap()->GetArraySize(); i++)
		{
			m_rhi_device->EventBegin("Pass_DepthDirectionalLight " + to_string(i));
			m_rhi_pipeline->SetRenderTarget(shadow_map->GetRenderTargetView(i), shadow_map->GetDepthStencilView(), true);

			for (const auto& entity : entities)
			{
				// Acquire renderable component
				auto renderable = entity->GetRenderable_PtrRaw();
				if (!renderable)
					continue;
	
				// Acquire material
				auto material = renderable->MaterialPtr();
				if (!material)
					continue;

				// Acquire geometry
				auto model = renderable->GeometryModel();
				if (!model || !model->GetVertexBuffer() || !model->GetIndexBuffer())
					continue;

				// Skip meshes that don't cast shadows
				if (!renderable->GetCastShadows())
					continue;

				// Skip transparent meshes (for now)
				if (material->GetColorAlbedo().w < 1.0f)
					continue;

				// Bind geometry
				if (currently_bound_geometry != model->GetResourceId())
				{
					m_rhi_pipeline->SetIndexBuffer(model->GetIndexBuffer());
					m_rhi_pipeline->SetVertexBuffer(model->GetVertexBuffer());
					currently_bound_geometry = model->GetResourceId();
				}

				SetDefaultBuffer(
					static_cast<unsigned int>(m_resolution.x),
					static_cast<unsigned int>(m_resolution.y),
					entity->GetTransform_PtrRaw()->GetMatrix() * light_directional->GetViewMatrix() * light_directional->ShadowMap_GetProjectionMatrix(i)
				);
				m_rhi_pipeline->DrawIndexed(renderable->GeometryIndexCount(), renderable->GeometryIndexOffset(), renderable->GeometryVertexOffset());
			}
			m_rhi_device->EventEnd();
		}

		m_rhi_device->EventEnd();
		TIME_BLOCK_END_MULTI(m_profiler);
	}

	void Renderer::Pass_GBuffer()
	{
		if (!m_rhi_device)
			return;

		m_cmd_list->Begin("Pass_GBuffer");

		auto& viewport		= m_g_buffer_albedo->GetViewport();
		const auto depth	= Settings::Get().GetReverseZ() ? 1.0f - m_viewport.GetMaxDepth() : m_viewport.GetMaxDepth();
		Vector4 clear_color = Vector4(0, 0, 0, 0);
		Vector4 clear_depth = Vector4(depth, depth, 0, 0);
		
		// If there is nothing to render, just clear
		if (m_entities[Renderable_ObjectOpaque].empty())
		{
			m_cmd_list->ClearRenderTarget(m_g_buffer_albedo->GetRenderTargetView(), clear_color);
			m_cmd_list->ClearRenderTarget(m_g_buffer_normal->GetRenderTargetView(), clear_color);
			m_cmd_list->ClearRenderTarget(m_g_buffer_material->GetRenderTargetView(), clear_color); // zeroed material buffer causes sky sphere to render
			m_cmd_list->ClearRenderTarget(m_g_buffer_velocity->GetRenderTargetView(), clear_color);
			m_cmd_list->ClearDepthStencil(m_g_buffer_depth->GetDepthStencilView(), Clear_Depth, depth);
			m_cmd_list->End();
			m_cmd_list->Submit();
			m_cmd_list->Clear();
			return;
		}

		// Prepare resources
		SetDefaultBuffer(static_cast<unsigned int>(m_resolution.x), static_cast<unsigned int>(m_resolution.y), Matrix::Identity, 0.0f, Vector2::Zero, false);
		vector<void*> textures(8);
		vector<void*> render_targets
		{
			m_g_buffer_albedo->GetRenderTargetView(),
			m_g_buffer_normal->GetRenderTargetView(),
			m_g_buffer_material->GetRenderTargetView(),
			m_g_buffer_velocity->GetRenderTargetView(),
			m_g_buffer_depth->GetRenderTargetView(),
		};
	
		// Star command list
		m_cmd_list->SetRasterizerState(m_rasterizer_cull_back_solid);
		m_cmd_list->SetBlendState(m_blend_disabled);
		m_cmd_list->SetPrimitiveTopology(PrimitiveTopology_TriangleList);
		m_cmd_list->SetDepthStencilState(m_depth_stencil_enabled);
		m_cmd_list->SetRenderTargets(render_targets, m_g_buffer_depth->GetDepthStencilView());
		m_cmd_list->ClearRenderTarget(render_targets[0], clear_color);
		m_cmd_list->ClearRenderTarget(render_targets[1], clear_color);
		m_cmd_list->ClearRenderTarget(render_targets[2], clear_color);
		m_cmd_list->ClearRenderTarget(render_targets[3], clear_color);
		m_cmd_list->ClearDepthStencil(m_g_buffer_depth->GetDepthStencilView(), Clear_Depth, depth);
		m_cmd_list->SetViewport(viewport);
		m_cmd_list->SetShaderVertex(m_vs_gbuffer);
		m_cmd_list->SetInputLayout(m_vs_gbuffer->GetInputLayout());
		m_cmd_list->SetConstantBuffer(0, Buffer_Global, m_buffer_global);
		m_cmd_list->SetSampler(0, m_sampler_anisotropic_wrap);	
		
		// Variables that help reduce state changes
		unsigned int currently_bound_geometry	= 0;
		unsigned int currently_bound_shader		= 0;
		unsigned int currently_bound_material	= 0;

		for (auto entity : m_entities[Renderable_ObjectOpaque])
		{
			// Get renderable and material
			auto renderable = entity->GetRenderable_PtrRaw();
			auto material	= renderable ? renderable->MaterialPtr().get() : nullptr;

			if (!renderable || !material)
				continue;

			// Get shader and geometry
			auto shader = material->GetShader();
			auto model	= renderable->GeometryModel();

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
			if (currently_bound_geometry != model->GetResourceId())
			{
				m_cmd_list->SetBufferIndex(model->GetIndexBuffer());
				m_cmd_list->SetBufferVertex(model->GetVertexBuffer());
				currently_bound_geometry = model->GetResourceId();
			}

			// Bind shader
			if (currently_bound_shader != shader->RHI_GetID())
			{
				m_cmd_list->SetShaderPixel(static_pointer_cast<RHI_Shader>(shader));
				currently_bound_shader = shader->RHI_GetID();
			}

			// Bind textures
			if (currently_bound_material != material->GetResourceId())
			{
				textures[0] = material->GetTextureShaderResourceByType(TextureType_Albedo);
				textures[1] = material->GetTextureShaderResourceByType(TextureType_Roughness);
				textures[2] = material->GetTextureShaderResourceByType(TextureType_Metallic);
				textures[3] = material->GetTextureShaderResourceByType(TextureType_Normal);
				textures[4] = material->GetTextureShaderResourceByType(TextureType_Height);
				textures[5] = material->GetTextureShaderResourceByType(TextureType_Occlusion);
				textures[6] = material->GetTextureShaderResourceByType(TextureType_Emission);
				textures[7] = material->GetTextureShaderResourceByType(TextureType_Mask);

				m_cmd_list->SetTextures(0, textures);
				currently_bound_material = material->GetResourceId();
			}

			// UPDATE PER OBJECT BUFFER
			shader->UpdatePerObjectBuffer(entity->GetTransform_PtrRaw(), material, m_view, m_projection);
			m_cmd_list->SetConstantBuffer(1, Buffer_Global, shader->GetConstantBuffer());

			// Render	
			m_cmd_list->DrawIndexed(renderable->GeometryIndexCount(), renderable->GeometryIndexOffset(), renderable->GeometryVertexOffset());
			m_profiler->m_renderer_meshes_rendered++;

		} // ENTITY/MESH ITERATION

		m_cmd_list->End();
		m_cmd_list->Submit();
		m_cmd_list->Clear();
	}

	void Renderer::Pass_PreLight(shared_ptr<RHI_RenderTexture>& tex_in, shared_ptr<RHI_RenderTexture>& tex_shadows_out, shared_ptr<RHI_RenderTexture>& tex_ssao_out)
	{
		m_rhi_device->EventBegin("Pass_PreLight");

		SetDefaultPipelineState();
		m_rhi_pipeline->SetIndexBuffer(m_quad.GetIndexBuffer());
		m_rhi_pipeline->SetVertexBuffer(m_quad.GetVertexBuffer());

		// Shadow mapping + Blur
		auto shadow_mapped = false;
		if (auto light_dir = GetLightDirectional())
		{
			if (light_dir->GetCastShadows())
			{
				Pass_ShadowMapping(tex_in, GetLightDirectional());
				const auto sigma		= 1.0f;
				const auto pixel_stride	= 1.0f;
				Pass_BlurBilateralGaussian(tex_in, tex_shadows_out, sigma, pixel_stride);
				shadow_mapped = true;
			}
		}
		if (!shadow_mapped)
		{
			tex_shadows_out->Clear(1, 1, 1, 1);
		}

		// SSAO + Blur
		if (m_flags & Render_PostProcess_SSAO)
		{
			Pass_SSAO(tex_in);
			const auto sigma		= 1.0f;
			const auto pixel_stride	= 1.0f;
			Pass_BlurBilateralGaussian(tex_in, tex_ssao_out, sigma, pixel_stride);
		}

		m_rhi_device->EventEnd();
	}

	void Renderer::Pass_Light(shared_ptr<RHI_RenderTexture>& tex_shadows, shared_ptr<RHI_RenderTexture>& tex_ssao, shared_ptr<RHI_RenderTexture>& tex_out)
	{
		if (m_vps_light->GetCompilationState() != Shader_Compiled)
			return;

		m_cmd_list->Begin("Pass_Light");

		// Update constant buffers
		SetDefaultBuffer(static_cast<unsigned int>(m_resolution.x), static_cast<unsigned int>(m_resolution.y), Matrix::Identity, 0.0f, Vector2::Zero, false);
		m_vps_light->UpdateConstantBuffer
		(
			m_view_projection_orthographic,
			m_view,
			m_projection,
			m_entities[Renderable_Light],
			Flags_IsSet(Render_PostProcess_SSR)
		);

		// Prepare resources
		auto shader						= static_pointer_cast<RHI_Shader>(m_vps_light);
		vector<void*> samplers			= { m_sampler_trilinear_clamp->GetBuffer(), m_sampler_point_clamp->GetBuffer() };
		vector<void*> constant_buffers	= { m_buffer_global->GetBuffer(),  m_vps_light->GetConstantBuffer()->GetBuffer() };
		vector<void*> textures =
		{
			m_g_buffer_albedo->GetShaderResource(),																		// Albedo	
			m_g_buffer_normal->GetShaderResource(),																		// Normal
			m_g_buffer_depth->GetShaderResource(),																		// Depth
			m_g_buffer_material->GetShaderResource(),																	// Material
			tex_shadows->GetShaderResource(),																			// Shadows
			Flags_IsSet(Render_PostProcess_SSAO) ? tex_ssao->GetShaderResource() : m_tex_white->GetShaderResource(),	// SSAO
			m_render_tex_full_hdr_light2->GetShaderResource(),															// Previous frame
			m_skybox ? m_skybox->GetTexture()->GetShaderResource() : m_tex_white->GetShaderResource(),					// Environment
			m_tex_lut_ibl->GetShaderResource()																			// LutIBL
		};

		// Setup command list
		m_cmd_list->SetDepthStencilState(m_depth_stencil_disabled);
		m_cmd_list->SetRasterizerState(m_rasterizer_cull_back_solid);
		m_cmd_list->SetBlendState(m_blend_disabled);
		m_cmd_list->SetPrimitiveTopology(PrimitiveTopology_TriangleList);
		m_cmd_list->SetViewport(tex_out->GetViewport());
		m_cmd_list->SetRenderTarget(tex_out->GetRenderTargetView());
		m_cmd_list->SetShaderVertex(shader);
		m_cmd_list->SetShaderPixel(shader);
		m_cmd_list->SetInputLayout(shader->GetInputLayout());
		m_cmd_list->SetSamplers(0, samplers);
		m_cmd_list->SetTextures(0, textures);
		m_cmd_list->SetConstantBuffers(0, Buffer_Global, constant_buffers);
		m_cmd_list->DrawIndexed(m_quad.GetIndexCount(), 0, 0);
		m_cmd_list->End();
		m_cmd_list->Submit();
		m_cmd_list->Clear();
	}

	void Renderer::Pass_Transparent(shared_ptr<RHI_RenderTexture>& tex_out)
	{
		if (!GetLightDirectional())
			return;

		auto& entities_transparent = m_entities[Renderable_ObjectTransparent];
		if (entities_transparent.empty())
			return;

		TIME_BLOCK_START_MULTI(m_profiler);
		m_rhi_device->EventBegin("Pass_Transparent");
		SetDefaultPipelineState();

		m_rhi_pipeline->SetBlendState(m_blend_enabled);	
		m_rhi_pipeline->SetDepthStencilState(m_depth_stencil_enabled);
		m_rhi_pipeline->SetRenderTarget(tex_out, m_g_buffer_depth->GetDepthStencilView());
		m_rhi_pipeline->SetTexture(m_g_buffer_depth);
		m_rhi_pipeline->SetTexture(m_skybox ? m_skybox->GetTexture() : nullptr);
		m_rhi_pipeline->SetSampler(m_sampler_bilinear_clamp);
		m_rhi_pipeline->SetShader(m_vps_transparent);

		for (auto& entity : entities_transparent)
		{
			// Get renderable and material
			auto renderable	= entity->GetRenderable_PtrRaw();
			auto material	= renderable ? renderable->MaterialPtr().get() : nullptr;

			if (!renderable || !material)
				continue;

			// Get geometry
			auto model = renderable->GeometryModel();
			if (!model || !model->GetVertexBuffer() || !model->GetIndexBuffer())
				continue;

			// Skip objects outside of the view frustum
			if (!m_camera->IsInViewFrustrum(renderable))
				continue;

			// Set the following per object
			m_rhi_pipeline->SetRasterizerState(GetRasterizerState(material->GetCullMode(), Fill_Solid));
			m_rhi_pipeline->SetIndexBuffer(model->GetIndexBuffer());
			m_rhi_pipeline->SetVertexBuffer(model->GetVertexBuffer());

			// Constant buffer
			auto buffer = Struct_Transparency(
				entity->GetTransform_PtrRaw()->GetMatrix(),
				m_view,
				m_projection,
				material->GetColorAlbedo(),
				m_camera->GetTransform()->GetPosition(),
				GetLightDirectional()->GetDirection(),
				material->GetRoughnessMultiplier()
			);
			m_vps_transparent->UpdateBuffer(&buffer);
			m_rhi_pipeline->SetConstantBuffer(m_vps_transparent->GetConstantBuffer(), 1, Buffer_Global);
			m_rhi_pipeline->DrawIndexed(renderable->GeometryIndexCount(), renderable->GeometryIndexOffset(), renderable->GeometryVertexOffset());

			m_profiler->m_renderer_meshes_rendered++;

		} // ENTITY/MESH ITERATION

		m_rhi_device->EventEnd();
		TIME_BLOCK_END_MULTI(m_profiler);
	}

	void Renderer::Pass_PostLight(shared_ptr<RHI_RenderTexture>& tex_in, shared_ptr<RHI_RenderTexture>& tex_out)
	{
		m_rhi_device->EventBegin("Pass_PostLight");

		// All post-process passes share the following, so set them once here
		SetDefaultPipelineState();
		m_rhi_pipeline->SetVertexBuffer(m_quad.GetVertexBuffer());
		m_rhi_pipeline->SetIndexBuffer(m_quad.GetIndexBuffer());
		m_rhi_pipeline->SetVertexShader(m_vs_quad);

		// Render target swapping
		const auto swap_targets = [&tex_in, &tex_out]() { tex_out.swap(tex_in); };

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

		m_rhi_device->EventEnd();
	}

	void Renderer::Pass_ShadowMapping(shared_ptr<RHI_RenderTexture>& tex_out, Light* light_directional_in)
	{
		if (!light_directional_in)
			return;

		if (!light_directional_in->GetCastShadows())
			return;

		TIME_BLOCK_START_MULTI(m_profiler);
		m_rhi_device->EventBegin("Pass_Shadowing");

		SetDefaultPipelineState();
		m_rhi_pipeline->SetRenderTarget(tex_out);
		m_rhi_pipeline->SetViewport(tex_out->GetViewport());
		m_rhi_pipeline->SetShader(m_vps_shadow_mapping);
		m_rhi_pipeline->SetTexture(m_g_buffer_normal);
		m_rhi_pipeline->SetTexture(m_g_buffer_depth);
		m_rhi_pipeline->SetTexture(light_directional_in->GetShadowMap()); // Texture2DArray
		m_rhi_pipeline->SetSampler(m_sampler_compare_depth);
		m_rhi_pipeline->SetSampler(m_sampler_bilinear_clamp);
		SetDefaultBuffer(tex_out->GetWidth(), tex_out->GetHeight(), m_view_projection_orthographic);
		auto buffer = Struct_ShadowMapping((m_view_projection).Inverted(), light_directional_in, m_camera.get());
		m_vps_shadow_mapping->UpdateBuffer(&buffer);
		m_rhi_pipeline->SetConstantBuffer(m_vps_shadow_mapping->GetConstantBuffer(), 1, Buffer_Global);
		m_rhi_pipeline->DrawIndexed(Rectangle::GetIndexCount(), 0, 0);

		m_rhi_device->EventEnd();
		TIME_BLOCK_END_MULTI(m_profiler);
	}

	void Renderer::Pass_SSAO(shared_ptr<RHI_RenderTexture>& tex_out)
	{
		TIME_BLOCK_START_MULTI(m_profiler);
		m_rhi_device->EventBegin("Pass_SSAO");

		SetDefaultPipelineState();
		SetDefaultBuffer(tex_out->GetWidth(), tex_out->GetHeight());
		m_rhi_pipeline->SetRenderTarget(tex_out);
		m_rhi_pipeline->SetTexture(m_g_buffer_normal);
		m_rhi_pipeline->SetTexture(m_g_buffer_depth);
		m_rhi_pipeline->SetTexture(m_tex_noise_normal);
		m_rhi_pipeline->SetViewport(tex_out->GetViewport());
		m_rhi_pipeline->SetShader(m_vps_ssao);
		m_rhi_pipeline->SetSampler(m_sampler_bilinear_clamp);	// SSAO (clamp)
		m_rhi_pipeline->SetSampler(m_sampler_bilinear_wrap);	// SSAO noise texture (wrap)
		auto buffer = Struct_Matrix_Matrix
		(
			m_view_projection_orthographic,
			(m_view_projection).Inverted()
		);
		m_vps_ssao->UpdateBuffer(&buffer);
		m_rhi_pipeline->SetConstantBuffer(m_vps_ssao->GetConstantBuffer(), 1, Buffer_Global);
		m_rhi_pipeline->DrawIndexed(Rectangle::GetIndexCount(), 0, 0);

		m_rhi_device->EventEnd();
		TIME_BLOCK_END_MULTI(m_profiler);
	}

	void Renderer::Pass_BlurBox(shared_ptr<RHI_RenderTexture>& tex_in, shared_ptr<RHI_RenderTexture>& tex_out, const float sigma)
	{
		m_rhi_device->EventBegin("Pass_Blur");

		SetDefaultPipelineState();
		SetDefaultBuffer(tex_out->GetWidth(), tex_out->GetHeight(), Matrix::Identity, sigma);
		m_rhi_pipeline->SetRenderTarget(tex_out);
		m_rhi_pipeline->SetViewport(tex_out->GetViewport());
		m_rhi_pipeline->SetVertexShader(m_vs_quad);
		m_rhi_pipeline->SetPixelShader(m_ps_blur_box);
		m_rhi_pipeline->SetTexture(tex_in); // Shadows are in the alpha channel
		m_rhi_pipeline->SetSampler(m_sampler_trilinear_clamp);
		m_rhi_pipeline->DrawIndexed(Rectangle::GetIndexCount(), 0, 0);

		m_rhi_device->EventEnd();
	}

	void Renderer::Pass_BlurGaussian(shared_ptr<RHI_RenderTexture>& tex_in, shared_ptr<RHI_RenderTexture>& tex_out, const float sigma, const float pixel_stride)
	{
		if (tex_in->GetWidth() != tex_out->GetWidth() ||
			tex_in->GetHeight() != tex_out->GetHeight() ||
			tex_in->GetFormat() != tex_out->GetFormat())
		{
			LOG_ERROR("Invalid parameters, textures must match because they will get swapped");
			return;
		}

		m_rhi_device->EventBegin("Pass_BlurGaussian");

		// Set common states
		m_rhi_pipeline->SetVertexShader(m_vs_quad);
		m_rhi_pipeline->SetPixelShader(m_ps_blur_gaussian);

		// Horizontal Gaussian blur	
		auto direction = Vector2(pixel_stride, 0.0f);
		SetDefaultPipelineState();	
		SetDefaultBuffer(tex_in->GetWidth(), tex_in->GetHeight(), Matrix::Identity, sigma, direction);
		m_rhi_pipeline->SetRenderTarget(tex_out);
		m_rhi_pipeline->SetViewport(tex_out->GetViewport());
		m_rhi_pipeline->SetSampler(m_sampler_bilinear_clamp);
		m_rhi_pipeline->SetTexture(tex_in);
		m_rhi_pipeline->DrawIndexed(Rectangle::GetIndexCount(), 0, 0);

		// Vertical Gaussian blur
		direction = Vector2(0.0f, pixel_stride);
		SetDefaultPipelineState();
		SetDefaultBuffer(tex_in->GetWidth(), tex_in->GetHeight(), Matrix::Identity, sigma, direction);
		m_rhi_pipeline->SetRenderTarget(tex_in);
		m_rhi_pipeline->SetViewport(tex_in->GetViewport());
		m_rhi_pipeline->SetSampler(m_sampler_bilinear_clamp);
		m_rhi_pipeline->SetTexture(tex_out);
		m_rhi_pipeline->DrawIndexed(Rectangle::GetIndexCount(), 0, 0);

		// Swap textures
		tex_in.swap(tex_out);

		m_rhi_device->EventEnd();
	}

	void Renderer::Pass_BlurBilateralGaussian(shared_ptr<RHI_RenderTexture>& tex_in, shared_ptr<RHI_RenderTexture>& tex_out, const float sigma, const float pixel_stride)
	{
		if (tex_in->GetWidth() != tex_out->GetWidth() ||
			tex_in->GetHeight() != tex_out->GetHeight() ||
			tex_in->GetFormat() != tex_out->GetFormat())
		{
			LOG_ERROR("Invalid parameters, textures must match because they will get swapped.");
			return;
		}

		m_rhi_device->EventBegin("Pass_BlurBilateralGaussian");

		// Set common states
		m_rhi_pipeline->SetVertexShader(m_vs_quad);
		m_rhi_pipeline->SetPixelShader(m_ps_blur_gaussian_bilateral);

		// Horizontal Gaussian blur
		SetDefaultPipelineState();	
		m_rhi_pipeline->SetRenderTarget(tex_out);
		m_rhi_pipeline->SetViewport(tex_out->GetViewport());
		m_rhi_pipeline->SetTexture(tex_in);
		m_rhi_pipeline->SetTexture(m_g_buffer_depth);
		m_rhi_pipeline->SetTexture(m_g_buffer_normal);
		m_rhi_pipeline->SetSampler(m_sampler_bilinear_clamp);
		auto direction = Vector2(pixel_stride, 0.0f);
		SetDefaultBuffer(tex_in->GetWidth(), tex_in->GetHeight(), Matrix::Identity, sigma, direction);
		m_rhi_pipeline->DrawIndexed(Rectangle::GetIndexCount(), 0, 0);

		// Vertical Gaussian blur
		SetDefaultPipelineState();
		m_rhi_pipeline->SetRenderTarget(tex_in);
		m_rhi_pipeline->SetViewport(tex_in->GetViewport());
		m_rhi_pipeline->SetTexture(tex_out);
		m_rhi_pipeline->SetTexture(m_g_buffer_depth);
		m_rhi_pipeline->SetTexture(m_g_buffer_normal);
		m_rhi_pipeline->SetSampler(m_sampler_bilinear_clamp);
		direction = Vector2(0.0f, pixel_stride);
		SetDefaultBuffer(tex_in->GetWidth(), tex_in->GetHeight(), Matrix::Identity, sigma, direction);
		m_rhi_pipeline->DrawIndexed(Rectangle::GetIndexCount(), 0, 0);

		tex_in.swap(tex_out);

		m_rhi_device->EventEnd();
	}

	void Renderer::Pass_TAA(shared_ptr<RHI_RenderTexture>& tex_in, shared_ptr<RHI_RenderTexture>& tex_out)
	{
		TIME_BLOCK_START_MULTI(m_profiler);
		m_rhi_device->EventBegin("Pass_TAA");
		SetDefaultPipelineState();

		// Resolve
		SetDefaultBuffer(m_render_tex_full_taa_current->GetWidth(), m_render_tex_full_taa_current->GetHeight());
		m_rhi_pipeline->SetRenderTarget(m_render_tex_full_taa_current);
		m_rhi_pipeline->SetViewport(m_render_tex_full_taa_current->GetViewport());
		m_rhi_pipeline->SetPixelShader(m_ps_taa);
		m_rhi_pipeline->SetSampler(m_sampler_bilinear_clamp);
		m_rhi_pipeline->SetTexture(m_render_tex_full_taa_history);
		m_rhi_pipeline->SetTexture(tex_in);
		m_rhi_pipeline->SetTexture(m_g_buffer_velocity);
		m_rhi_pipeline->SetTexture(m_g_buffer_depth);
		m_rhi_pipeline->DrawIndexed(Rectangle::GetIndexCount(), 0, 0);

		// Output to texOut
		SetDefaultBuffer(tex_out->GetWidth(), tex_out->GetHeight());
		m_rhi_pipeline->SetRenderTarget(tex_out);
		m_rhi_pipeline->SetViewport(tex_out->GetViewport());
		m_rhi_pipeline->SetPixelShader(m_ps_texture);
		m_rhi_pipeline->SetSampler(m_sampler_point_clamp);
		m_rhi_pipeline->SetTexture(m_render_tex_full_taa_current);
		m_rhi_pipeline->DrawIndexed(Rectangle::GetIndexCount(), 0, 0);

		// Swap textures so current becomes history
		m_render_tex_full_taa_current.swap(m_render_tex_full_taa_history);

		m_rhi_device->EventEnd();
		TIME_BLOCK_END_MULTI(m_profiler);
	}

	void Renderer::Pass_Bloom(shared_ptr<RHI_RenderTexture>& tex_in, shared_ptr<RHI_RenderTexture>& tex_out)
	{
		TIME_BLOCK_START_MULTI(m_profiler);
		m_rhi_device->EventBegin("Pass_Bloom");

		// Downsample
		SetDefaultPipelineState();
		SetDefaultBuffer(m_render_tex_quarter_blur1->GetWidth(), m_render_tex_quarter_blur1->GetHeight());
		m_rhi_pipeline->SetRenderTarget(m_render_tex_quarter_blur1);
		m_rhi_pipeline->SetViewport(m_render_tex_quarter_blur1->GetViewport());
		m_rhi_pipeline->SetTexture(tex_in);
		m_rhi_pipeline->SetSampler(m_sampler_bilinear_clamp);
		m_rhi_pipeline->SetPixelShader(m_ps_downsample_box);
		m_rhi_pipeline->DrawIndexed(Rectangle::GetIndexCount(), 0, 0);

		// Bright pass
		SetDefaultPipelineState();
		SetDefaultBuffer(m_render_tex_quarter_blur2->GetWidth(), m_render_tex_quarter_blur2->GetHeight());
		m_rhi_pipeline->SetRenderTarget(m_render_tex_quarter_blur2);
		m_rhi_pipeline->SetViewport(m_render_tex_quarter_blur2->GetViewport());
		m_rhi_pipeline->SetTexture(m_render_tex_quarter_blur1);
		m_rhi_pipeline->SetSampler(m_sampler_bilinear_clamp);
		m_rhi_pipeline->SetPixelShader(m_ps_bloom_bright);
		m_rhi_pipeline->DrawIndexed(Rectangle::GetIndexCount(), 0, 0);

		const auto sigma = 2.0f;
		Pass_BlurGaussian(m_render_tex_quarter_blur2, m_render_tex_quarter_blur1, sigma);

		// Additive blending
		SetDefaultPipelineState();	
		SetDefaultBuffer(tex_out->GetWidth(), tex_out->GetHeight());
		m_rhi_pipeline->SetRenderTarget(tex_out);
		m_rhi_pipeline->SetViewport(tex_out->GetViewport());
		m_rhi_pipeline->SetTexture(tex_in);
		m_rhi_pipeline->SetTexture(m_render_tex_quarter_blur1);
		m_rhi_pipeline->SetSampler(m_sampler_bilinear_clamp);
		m_rhi_pipeline->SetPixelShader(m_ps_bloom_blend);
		m_rhi_pipeline->DrawIndexed(Rectangle::GetIndexCount(), 0, 0);

		m_rhi_device->EventEnd();
		TIME_BLOCK_END_MULTI(m_profiler);
	}

	void Renderer::Pass_ToneMapping(std::shared_ptr<RHI_RenderTexture>& tex_in, std::shared_ptr<RHI_RenderTexture>& tex_out)
	{
		TIME_BLOCK_START_MULTI(m_profiler);
		m_rhi_device->EventBegin("Pass_ToneMapping");

		SetDefaultPipelineState();
		SetDefaultBuffer(tex_out->GetWidth(), tex_out->GetHeight());
		m_rhi_pipeline->SetRenderTarget(tex_out);
		m_rhi_pipeline->SetTexture(tex_in);
		m_rhi_pipeline->SetSampler(m_sampler_point_clamp);
		m_rhi_pipeline->SetViewport(tex_out->GetViewport());
		m_rhi_pipeline->SetPixelShader(m_ps_tone_mapping);
		m_rhi_pipeline->DrawIndexed(Rectangle::GetIndexCount(), 0, 0);

		m_rhi_device->EventEnd();
		TIME_BLOCK_END_MULTI(m_profiler);
	}

	void Renderer::Pass_GammaCorrection(std::shared_ptr<RHI_RenderTexture>& tex_in, std::shared_ptr<RHI_RenderTexture>& tex_out)
	{
		TIME_BLOCK_START_MULTI(m_profiler);
		m_rhi_device->EventBegin("Pass_GammaCorrection");

		SetDefaultPipelineState();
		SetDefaultBuffer(tex_out->GetWidth(), tex_out->GetHeight());
		m_rhi_pipeline->SetRenderTarget(tex_out);
		m_rhi_pipeline->SetTexture(tex_in);
		m_rhi_pipeline->SetSampler(m_sampler_point_clamp);
		m_rhi_pipeline->SetViewport(tex_out->GetViewport());
		m_rhi_pipeline->SetPixelShader(m_ps_gamma_correction);
		m_rhi_pipeline->DrawIndexed(Rectangle::GetIndexCount(), 0, 0);

		m_rhi_device->EventEnd();
		TIME_BLOCK_END_MULTI(m_profiler);
	}

	void Renderer::Pass_FXAA(shared_ptr<RHI_RenderTexture>& tex_in, shared_ptr<RHI_RenderTexture>& tex_out)
	{
		TIME_BLOCK_START_MULTI(m_profiler);
		m_rhi_device->EventBegin("Pass_FXAA");

		// Common states
		SetDefaultPipelineState();
		SetDefaultBuffer(tex_out->GetWidth(), tex_out->GetHeight());
		m_rhi_pipeline->SetSampler(m_sampler_bilinear_clamp);

		// Luma
		m_rhi_pipeline->SetRenderTarget(tex_out);
		m_rhi_pipeline->SetViewport(tex_out->GetViewport());
		m_rhi_pipeline->SetPixelShader(m_ps_luma);
		m_rhi_pipeline->SetTexture(tex_in);
		m_rhi_pipeline->DrawIndexed(Rectangle::GetIndexCount(), 0, 0);

		// FXAA
		m_rhi_pipeline->SetRenderTarget(tex_in);
		m_rhi_pipeline->SetViewport(tex_in->GetViewport());
		m_rhi_pipeline->SetPixelShader(m_ps_fxaa);
		m_rhi_pipeline->SetTexture(tex_out);
		m_rhi_pipeline->DrawIndexed(Rectangle::GetIndexCount(), 0, 0);

		// Swap the textures
		tex_in.swap(tex_out);

		m_rhi_device->EventEnd();
		TIME_BLOCK_END_MULTI(m_profiler);
	}

	void Renderer::Pass_ChromaticAberration(shared_ptr<RHI_RenderTexture>& tex_in, shared_ptr<RHI_RenderTexture>& tex_out)
	{
		TIME_BLOCK_START_MULTI(m_profiler);
		m_rhi_device->EventBegin("Pass_ChromaticAberration");

		SetDefaultPipelineState();
		SetDefaultBuffer(tex_out->GetWidth(), tex_out->GetHeight());
		m_rhi_pipeline->SetSampler(m_sampler_bilinear_clamp);
		m_rhi_pipeline->SetRenderTarget(tex_out);
		m_rhi_pipeline->SetViewport(tex_out->GetViewport());
		m_rhi_pipeline->SetPixelShader(m_ps_chromatic_aberration);
		m_rhi_pipeline->SetTexture(tex_in);
		m_rhi_pipeline->DrawIndexed(Rectangle::GetIndexCount(), 0, 0);

		m_rhi_device->EventEnd();
		TIME_BLOCK_END_MULTI(m_profiler);
	}

	void Renderer::Pass_MotionBlur(shared_ptr<RHI_RenderTexture>& tex_in, shared_ptr<RHI_RenderTexture>& tex_out)
	{
		TIME_BLOCK_START_MULTI(m_profiler);
		m_rhi_device->EventBegin("Pass_MotionBlur");

		SetDefaultPipelineState();
		SetDefaultBuffer(tex_out->GetWidth(), tex_out->GetHeight());
		m_rhi_pipeline->SetRenderTarget(tex_out);
		m_rhi_pipeline->SetViewport(tex_out->GetViewport());
		m_rhi_pipeline->SetSampler(m_sampler_bilinear_clamp);
		m_rhi_pipeline->SetPixelShader(m_ps_motion_blur);
		m_rhi_pipeline->SetTexture(tex_in);
		m_rhi_pipeline->SetTexture(m_g_buffer_velocity);
		m_rhi_pipeline->DrawIndexed(Rectangle::GetIndexCount(), 0, 0);

		m_rhi_device->EventEnd();
		TIME_BLOCK_END_MULTI(m_profiler);
	}

	void Renderer::Pass_Dithering(shared_ptr<RHI_RenderTexture>& tex_in, shared_ptr<RHI_RenderTexture>& tex_out)
	{
		TIME_BLOCK_START_MULTI(m_profiler);
		m_rhi_device->EventBegin("Pass_Dithering");

		SetDefaultPipelineState();
		SetDefaultBuffer(tex_out->GetWidth(), tex_out->GetHeight());
		m_rhi_pipeline->SetSampler(m_sampler_point_clamp);
		m_rhi_pipeline->SetRenderTarget(tex_out);
		m_rhi_pipeline->SetViewport(tex_out->GetViewport());
		m_rhi_pipeline->SetPixelShader(m_ps_dithering);
		m_rhi_pipeline->SetTexture(tex_in);
		m_rhi_pipeline->DrawIndexed(Rectangle::GetIndexCount(), 0, 0);

		m_rhi_device->EventEnd();
		TIME_BLOCK_END_MULTI(m_profiler);
	}

	void Renderer::Pass_Sharpening(shared_ptr<RHI_RenderTexture>& tex_in, shared_ptr<RHI_RenderTexture>& tex_out)
	{
		TIME_BLOCK_START_MULTI(m_profiler);
		m_rhi_device->EventBegin("Pass_Sharpening");

		SetDefaultPipelineState();
		SetDefaultBuffer(tex_out->GetWidth(), tex_out->GetHeight());
		m_rhi_pipeline->SetRenderTarget(tex_out);
		m_rhi_pipeline->SetTexture(tex_in);
		m_rhi_pipeline->SetSampler(m_sampler_bilinear_clamp);
		m_rhi_pipeline->SetViewport(tex_out->GetViewport());
		m_rhi_pipeline->SetPixelShader(m_ps_sharpening);
		m_rhi_pipeline->DrawIndexed(Rectangle::GetIndexCount(), 0, 0);

		m_rhi_device->EventEnd();
		TIME_BLOCK_END_MULTI(m_profiler);
	}

	void Renderer::Pass_Lines(shared_ptr<RHI_RenderTexture>& tex_out)
	{
		const bool draw_picking_ray = m_flags & Render_Gizmo_PickingRay;
		const bool draw_aabb		= m_flags & Render_Gizmo_AABB;
		const bool draw_grid		= m_flags & Render_Gizmo_Grid;
		const auto draw_lines		= !m_lines_list_depth_enabled.empty() || !m_lines_list_depth_disabled.empty(); // Any kind of lines, physics, user debug, etc.
		const auto draw				= draw_picking_ray || draw_aabb || draw_grid || draw_lines;
		if (!draw)
			return;

		TIME_BLOCK_START_MULTI(m_profiler);
		m_rhi_device->EventBegin("Pass_Lines");

		// Generate lines for debug primitives offered by the renderer
		{
			// Picking ray
			if (draw_picking_ray)
			{
				const Ray& ray = m_camera->GetPickingRay();
				DrawLine(ray.GetStart(), ray.GetStart() + ray.GetDirection() * m_camera->GetFarPlane(), Vector4(0, 1, 0, 1));
			}

			// AABBs
			if (draw_aabb)
			{
				for (const auto& entity : m_entities[Renderable_ObjectOpaque])
				{
					if (auto renderable = entity->GetRenderable_PtrRaw())
					{
						DrawBox(renderable->GeometryAabb(), Vector4(0.41f, 0.86f, 1.0f, 1.0f));
					}
				}

				for (const auto& entity : m_entities[Renderable_ObjectTransparent])
				{
					if (auto renderable = entity->GetRenderable_PtrRaw())
					{
						DrawBox(renderable->GeometryAabb(), Vector4(0.41f, 0.86f, 1.0f, 1.0f));
					}
				}
			}
		}

		// Set common states
		SetDefaultPipelineState();
		m_rhi_pipeline->SetPrimitiveTopology(PrimitiveTopology_LineList);
		m_rhi_pipeline->SetShader(m_vps_color);
		m_rhi_pipeline->SetSampler(m_sampler_point_clamp);
		m_rhi_pipeline->SetRasterizerState(m_rasterizer_cull_back_wireframe);
		
		// unjittered matrix to avoid TAA jitter due to lack of motion vectors (line rendering is anti-aliased by D3D11, decently)
		const auto view_projection_unjittered = m_camera->GetViewMatrix() * m_camera->GetProjectionMatrix();

		// Draw lines that require depth
		m_rhi_pipeline->SetDepthStencilState(m_depth_stencil_enabled);
		m_rhi_pipeline->SetRenderTarget(tex_out, m_g_buffer_depth->GetDepthStencilView());
		{
			// Grid
			if (draw_grid)
			{
				m_rhi_pipeline->SetIndexBuffer(m_gizmo_grid->GetIndexBuffer());
				m_rhi_pipeline->SetVertexBuffer(m_gizmo_grid->GetVertexBuffer());
				m_rhi_pipeline->SetBlendState(m_blend_enabled);
				SetDefaultBuffer(static_cast<unsigned int>(m_resolution.x), static_cast<unsigned int>(m_resolution.y), m_gizmo_grid->ComputeWorldMatrix(m_camera->GetTransform()) * view_projection_unjittered);
				m_rhi_pipeline->DrawIndexed(m_gizmo_grid->GetIndexCount(), 0, 0);
			}

			// Lines
			const auto line_vertex_buffer_size = static_cast<unsigned int>(m_lines_list_depth_enabled.size());
			if (line_vertex_buffer_size != 0)
			{
				// Grow vertex buffer (if needed)
				if (line_vertex_buffer_size > m_vertex_buffer_lines->GetVertexCount())
				{
					m_vertex_buffer_lines->CreateDynamic(sizeof(RHI_Vertex_PosCol), line_vertex_buffer_size);
				}

				// Update vertex buffer
				const auto buffer = static_cast<RHI_Vertex_PosCol*>(m_vertex_buffer_lines->Map());
				copy(m_lines_list_depth_enabled.begin(), m_lines_list_depth_enabled.end(), buffer);
				m_vertex_buffer_lines->Unmap();

				// Set pipeline state
				m_rhi_pipeline->SetVertexBuffer(m_vertex_buffer_lines);
				SetDefaultBuffer(static_cast<unsigned int>(m_resolution.x), static_cast<unsigned int>(m_resolution.y), view_projection_unjittered);
				m_rhi_pipeline->Draw(line_vertex_buffer_size);

				m_lines_list_depth_enabled.clear();
			}
		}

		// Draw lines that don't require depth
		m_rhi_pipeline->SetRenderTarget(tex_out, nullptr);
		{
			// Lines
			const auto line_vertex_buffer_size = static_cast<unsigned int>(m_lines_list_depth_disabled.size());
			if (line_vertex_buffer_size != 0)
			{
				// Grow vertex buffer (if needed)
				if (line_vertex_buffer_size > m_vertex_buffer_lines->GetVertexCount())
				{
					m_vertex_buffer_lines->CreateDynamic(sizeof(RHI_Vertex_PosCol), line_vertex_buffer_size);
				}

				// Update vertex buffer
				const auto buffer = static_cast<RHI_Vertex_PosCol*>(m_vertex_buffer_lines->Map());
				copy(m_lines_list_depth_disabled.begin(), m_lines_list_depth_disabled.end(), buffer);
				m_vertex_buffer_lines->Unmap();

				// Set pipeline state
				m_rhi_pipeline->SetVertexBuffer(m_vertex_buffer_lines);
				SetDefaultBuffer(static_cast<unsigned int>(m_resolution.x), static_cast<unsigned int>(m_resolution.y), view_projection_unjittered);
				m_rhi_pipeline->Draw(line_vertex_buffer_size);

				m_lines_list_depth_disabled.clear();
			}
		}

		m_rhi_device->EventEnd();
		TIME_BLOCK_END_MULTI(m_profiler);
	}

	void Renderer::Pass_Gizmos(shared_ptr<RHI_RenderTexture>& tex_out)
	{
		bool render_lights		= m_flags & Render_Gizmo_Lights;
		bool render_transform	= m_flags & Render_Gizmo_Transform;
		bool render				= render_lights || render_transform;
		if (!render)
			return;

		TIME_BLOCK_START_MULTI(m_profiler);
		m_rhi_device->EventBegin("Pass_Gizmos");

		// Set shared states
		SetDefaultPipelineState();
		m_rhi_pipeline->SetRasterizerState(m_rasterizer_cull_back_solid);
		m_rhi_pipeline->SetBlendState(m_blend_enabled);
		m_rhi_pipeline->SetRenderTarget(tex_out, nullptr);

		if (render_lights)
		{
			auto& lights = m_entities[Renderable_Light];
			if (!lights.empty())
			{
				m_rhi_device->EventBegin("Gizmo_Lights");
				m_rhi_pipeline->SetVertexShader(m_vs_quad);
				m_rhi_pipeline->SetPixelShader(m_ps_texture);
				m_rhi_pipeline->SetSampler(m_sampler_bilinear_clamp);

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
					auto rectangle = Rectangle(position_light_screen.x - tex_width * 0.5f, position_light_screen.y - tex_height * 0.5f, tex_width, tex_height);
					if (rectangle != m_gizmo_light_rect)
					{
						m_gizmo_light_rect = rectangle;
						m_gizmo_light_rect.CreateBuffers(this);
					}

					SetDefaultBuffer(static_cast<unsigned int>(tex_width), static_cast<unsigned int>(tex_width), m_view_projection_orthographic);
					m_rhi_pipeline->SetTexture(light_tex);
					m_rhi_pipeline->SetIndexBuffer(m_gizmo_light_rect.GetIndexBuffer());
					m_rhi_pipeline->SetVertexBuffer(m_gizmo_light_rect.GetVertexBuffer());
					m_rhi_pipeline->DrawIndexed(Rectangle::GetIndexCount(), 0, 0);
				}

				m_rhi_device->EventEnd();
			}
		}

		// Transform
		if (render_transform)
		{
			if (m_gizmo_transform->Update(m_camera.get(), m_gizmo_transform_size, m_gizmo_transform_speed))
			{
				m_rhi_device->EventBegin("Gizmo_Transform");

				m_rhi_pipeline->SetShader(m_vps_gizmo_transform);
				m_rhi_pipeline->SetIndexBuffer(m_gizmo_transform->GetIndexBuffer());
				m_rhi_pipeline->SetVertexBuffer(m_gizmo_transform->GetVertexBuffer());
				SetDefaultBuffer(static_cast<unsigned int>(m_resolution.x), static_cast<unsigned int>(m_resolution.y));

				// Axis - X
				auto buffer = Struct_Matrix_Vector3(m_gizmo_transform->GetHandle().GetTransform(Vector3::Right), m_gizmo_transform->GetHandle().GetColor(Vector3::Right));
				m_vps_gizmo_transform->UpdateBuffer(&buffer);
				m_rhi_pipeline->SetConstantBuffer(m_vps_gizmo_transform->GetConstantBuffer(), 1, Buffer_Global);
				m_rhi_pipeline->DrawIndexed(m_gizmo_transform->GetIndexCount(), 0, 0);

				// Axis - Y
				buffer = Struct_Matrix_Vector3(m_gizmo_transform->GetHandle().GetTransform(Vector3::Up), m_gizmo_transform->GetHandle().GetColor(Vector3::Up));
				m_vps_gizmo_transform->UpdateBuffer(&buffer);
				m_rhi_pipeline->SetConstantBuffer(m_vps_gizmo_transform->GetConstantBuffer(), 1, Buffer_Global);
				m_rhi_pipeline->DrawIndexed(m_gizmo_transform->GetIndexCount(), 0, 0);

				// Axis - Z
				buffer = Struct_Matrix_Vector3(m_gizmo_transform->GetHandle().GetTransform(Vector3::Forward), m_gizmo_transform->GetHandle().GetColor(Vector3::Forward));
				m_vps_gizmo_transform->UpdateBuffer(&buffer);
				m_rhi_pipeline->SetConstantBuffer(m_vps_gizmo_transform->GetConstantBuffer(), 1, Buffer_Global);
				m_rhi_pipeline->DrawIndexed(m_gizmo_transform->GetIndexCount(), 0, 0);

				// Axes - XYZ
				if (m_gizmo_transform->DrawXYZ())
				{
					buffer = Struct_Matrix_Vector3(m_gizmo_transform->GetHandle().GetTransform(Vector3::One), m_gizmo_transform->GetHandle().GetColor(Vector3::One));
					m_vps_gizmo_transform->UpdateBuffer(&buffer);
					m_rhi_pipeline->SetConstantBuffer(m_vps_gizmo_transform->GetConstantBuffer(), 1, Buffer_Global);
					m_rhi_pipeline->DrawIndexed(m_gizmo_transform->GetIndexCount(), 0, 0);
				}

				m_rhi_device->EventEnd();
			}
		}

		m_rhi_device->EventEnd();
		TIME_BLOCK_END_MULTI(m_profiler);
	}

	void Renderer::Pass_PerformanceMetrics(shared_ptr<RHI_RenderTexture>& tex_out)
	{
		const bool draw = m_flags & Render_Gizmo_PerformanceMetrics;
		if (!draw)
			return;

		TIME_BLOCK_START_MULTI(m_profiler);
		m_rhi_device->EventBegin("Pass_PerformanceMetrics");
		SetDefaultPipelineState();

		// Updated text
		const auto text_pos = Vector2(-static_cast<int>(m_viewport.GetWidth()) * 0.5f + 1.0f, static_cast<int>(m_viewport.GetHeight()) * 0.5f);
		m_font->SetText(m_profiler->GetMetrics(), text_pos);
		// Updated constant buffer
		auto buffer = Struct_Matrix_Vector4(m_view_projection_orthographic, m_font->GetColor());
		m_vps_font->UpdateBuffer(&buffer);

		m_rhi_pipeline->SetRenderTarget(tex_out);
		m_rhi_pipeline->SetTexture(m_font->GetTexture());
		m_rhi_pipeline->SetBlendState(m_blend_enabled);
		m_rhi_pipeline->SetIndexBuffer(m_font->GetIndexBuffer());
		m_rhi_pipeline->SetVertexBuffer(m_font->GetVertexBuffer());	
		m_rhi_pipeline->SetSampler(m_sampler_bilinear_clamp);
		m_rhi_pipeline->SetShader(m_vps_font);
		m_rhi_pipeline->SetConstantBuffer(m_vps_font->GetConstantBuffer(), 0, Buffer_Global);
		m_rhi_pipeline->DrawIndexed(m_font->GetIndexCount(), 0, 0);

		m_rhi_device->EventEnd();
		TIME_BLOCK_END_MULTI(m_profiler);
	}

	bool Renderer::Pass_DebugBuffer(shared_ptr<RHI_RenderTexture>& tex_out)
	{
		if (m_debug_buffer == RendererDebug_None)
			return true;

		TIME_BLOCK_START_MULTI(m_profiler);
		m_rhi_device->EventBegin("Pass_DebugBuffer");
		SetDefaultPipelineState();
		SetDefaultBuffer(tex_out->GetWidth(), tex_out->GetHeight(), m_view_projection_orthographic);
		m_rhi_pipeline->SetVertexShader(m_vs_quad);

		// Bind correct texture & shader pass
		if (m_debug_buffer == RendererDebug_Albedo)
		{
			m_rhi_pipeline->SetTexture(m_g_buffer_albedo);
			m_rhi_pipeline->SetPixelShader(m_ps_texture);
		}

		if (m_debug_buffer == RendererDebug_Normal)
		{
			m_rhi_pipeline->SetTexture(m_g_buffer_normal);
			m_rhi_pipeline->SetPixelShader(m_ps_debug_normal_);
		}

		if (m_debug_buffer == RendererDebug_Material)
		{
			m_rhi_pipeline->SetTexture(m_g_buffer_material);
			m_rhi_pipeline->SetPixelShader(m_ps_texture);
		}

		if (m_debug_buffer == RendererDebug_Velocity)
		{
			m_rhi_pipeline->SetTexture(m_g_buffer_velocity);
			m_rhi_pipeline->SetPixelShader(m_ps_debug_velocity);
		}

		if (m_debug_buffer == RendererDebug_Depth)
		{
			m_rhi_pipeline->SetTexture(m_g_buffer_depth);
			m_rhi_pipeline->SetPixelShader(m_ps_debug_depth);
		}

		if ((m_debug_buffer == RendererDebug_SSAO))
		{
			if (Flags_IsSet(Render_PostProcess_SSAO))
			{
				m_rhi_pipeline->SetTexture(m_render_tex_half_ssao);
			}
			else
			{
				m_rhi_pipeline->SetTexture(m_tex_white);
			}
			m_rhi_pipeline->SetPixelShader(m_ps_debug_ssao);
		}

		m_rhi_pipeline->SetRenderTarget(tex_out);
		m_rhi_pipeline->SetViewport(tex_out->GetViewport());
		m_rhi_pipeline->SetVertexBuffer(m_quad.GetVertexBuffer());
		m_rhi_pipeline->SetIndexBuffer(m_quad.GetIndexBuffer());
		m_rhi_pipeline->SetPrimitiveTopology(PrimitiveTopology_TriangleList);
		m_rhi_pipeline->SetRasterizerState(m_rasterizer_cull_back_solid);
		m_rhi_pipeline->SetInputLayout(m_ps_texture->GetInputLayout());
		m_rhi_pipeline->SetSampler(m_sampler_bilinear_clamp);
		m_rhi_pipeline->DrawIndexed(Rectangle::GetIndexCount(), 0, 0);

		m_rhi_device->EventEnd();
		TIME_BLOCK_END_MULTI(m_profiler);

		return true;
	}
}