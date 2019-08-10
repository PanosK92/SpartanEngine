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
#include "Font/Font.h"
#include "../Profiling/Profiler.h"
#include "../Resource/IResource.h"
#include "Shaders/ShaderBuffered.h"
#include "Shaders/ShaderVariation.h"
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

        Pass_BrdfSpecularLut(); // only happens once
		Pass_LightDepth();
		Pass_GBuffer();
		Pass_Ssao();
        Pass_Ssr();
        Pass_Light();
        Pass_Composition(m_render_tex_composition);

		Pass_PostComposision
		(
			m_render_tex_composition,	// IN:	Light pass result
			m_render_tex_final		    // OUT: Result
		);

        m_render_tex_composition.swap(m_render_tex_composition_previous);

        Pass_Lines(m_render_tex_final);
        Pass_Gizmos(m_render_tex_final);
		Pass_DebugBuffer(m_render_tex_final);
		Pass_PerformanceMetrics(m_render_tex_final);

		m_cmd_list->End();
		m_cmd_list->Submit();
	}

	void Renderer::Pass_LightDepth()
	{
		// Acquire shader
		const auto& shader_depth = m_shaders[Shader_Depth_V];
		if (!shader_depth->IsCompiled())
			return;

        // Get opaque entities
        const auto& entities_opaque = m_entities[Renderer_Object_Opaque];
        if (entities_opaque.empty())
            return;

        // Get light entities
		const auto& entities_light = m_entities[Renderer_Object_Light];

		for (const auto& light_entity : entities_light)
		{
			const auto& light = light_entity->GetComponent<Light>();

			// Skip if it doesn't need to cast shadows
			if (!light->GetCastShadows())
				continue;

			// Acquire light's shadow map
			const auto& shadow_map = light->GetShadowMap();
			if (!shadow_map)
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
			uint32_t currently_bound_geometry   = 0;

			for (uint32_t i = 0; i < light->GetShadowMap()->GetArraySize(); i++)
			{
				const auto cascade_depth_stencil = shadow_map->GetResource_DepthStencil(i);

				m_cmd_list->Begin("Array_" + to_string(i + 1));
				m_cmd_list->ClearDepthStencil(cascade_depth_stencil, Clear_Depth, GetClearDepth());
				m_cmd_list->SetRenderTarget(nullptr, cascade_depth_stencil);

				auto light_view_projection = light->GetViewMatrix(i) * light->GetProjectionMatrix(i);

				for (const auto& entity : entities_opaque)
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
	}

	void Renderer::Pass_GBuffer()
	{
		if (!m_rhi_device)
			return;

		m_cmd_list->Begin("Pass_GBuffer");

		const auto& clear_color= Vector4::Zero;
		
		// If there is nothing to render, just clear
		if (m_entities[Renderer_Object_Opaque].empty())
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

		UpdateUberBuffer(static_cast<uint32_t>(m_resolution.x), static_cast<uint32_t>(m_resolution.y));
	
		// Variables that help reduce state changes
		uint32_t currently_bound_geometry	= 0;
		uint32_t currently_bound_shader		= 0;
		uint32_t currently_bound_material	= 0;

        auto draw_entity = [this, &currently_bound_geometry, &currently_bound_shader, &currently_bound_material](Entity* entity)
        {
            // Get renderable
            const auto& renderable = entity->GetRenderable_PtrRaw();
            if (!renderable)
                return;

            // Get material
            const auto& material = renderable->GetMaterial();
            if (!material)
                return;

            // Get shader and geometry
            const auto& shader = material->GetShader();
            const auto& model = renderable->GeometryModel();

            // Validate shader
            if (!shader || shader->GetCompilationState() != Shader_Compiled)
                return;

            // Validate geometry
            if (!model || !model->GetVertexBuffer() || !model->GetIndexBuffer())
                return;

            // Skip objects outside of the view frustum
            if (!m_camera->IsInViewFrustrum(renderable))
                return;

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
        };

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
        m_cmd_list->SetConstantBuffer(0, Buffer_Global, m_uber_buffer);
        m_cmd_list->SetSampler(0, m_sampler_anisotropic_wrap);

        // Draw opaque
		for (const auto& entity : m_entities[Renderer_Object_Opaque])
		{
			draw_entity(entity);
		}

        // Draw transparent (transparency of the poor)
        m_cmd_list->SetBlendState(m_blend_color_add);
        for (const auto& entity : m_entities[Renderer_Object_Transparent])
        {
            draw_entity(entity);
        }

		m_cmd_list->End();
		m_cmd_list->Submit();
	}

	void Renderer::Pass_Ssao()
	{
        // Acquire shaders
        const auto& shader_quad = m_shaders[Shader_Quad_V];
        const auto& shader_ssao = m_shaders[Shader_Ssao_P];
        if (!shader_quad->IsCompiled() || !shader_ssao->IsCompiled())
            return;

		m_cmd_list->Begin("Pass_Ssao");	
		m_cmd_list->ClearRenderTarget(m_render_tex_half_ssao->GetResource_RenderTarget(), Vector4::One);
        m_cmd_list->ClearRenderTarget(m_render_tex_ssao->GetResource_RenderTarget(), Vector4::One);

		if (m_flags & Render_PostProcess_SSAO)
		{
            // Prepare resources	
            void* textures[] = { m_g_buffer_normal->GetResource_Texture(), m_g_buffer_depth->GetResource_Texture(), m_tex_noise_normal->GetResource_Texture() };
            vector<void*> samplers = { m_sampler_bilinear_clamp->GetResource() /*SSAO (clamp) */, m_sampler_bilinear_wrap->GetResource() /*SSAO noise texture (wrap)*/ };
            UpdateUberBuffer(m_render_tex_half_ssao->GetWidth(), m_render_tex_half_ssao->GetHeight());

            m_cmd_list->ClearTextures(); // avoids d3d11 warning where the render target is already bound as an input texture (from some previous pass)
            m_cmd_list->SetDepthStencilState(m_depth_stencil_disabled);
            m_cmd_list->SetRasterizerState(m_rasterizer_cull_back_solid);
            m_cmd_list->SetBlendState(m_blend_disabled);
            m_cmd_list->SetPrimitiveTopology(PrimitiveTopology_TriangleList);
            m_cmd_list->SetBufferVertex(m_quad.GetVertexBuffer());
            m_cmd_list->SetBufferIndex(m_quad.GetIndexBuffer());
            m_cmd_list->SetRenderTarget(m_render_tex_half_ssao);
            m_cmd_list->SetViewport(m_render_tex_half_ssao->GetViewport());
            m_cmd_list->SetShaderVertex(shader_quad);
            m_cmd_list->SetInputLayout(shader_quad->GetInputLayout());
            m_cmd_list->SetShaderPixel(shader_ssao);
            m_cmd_list->SetTextures(0, textures, 3);
            m_cmd_list->SetSamplers(0, samplers);
            m_cmd_list->SetConstantBuffer(0, Buffer_Global, m_uber_buffer);
            m_cmd_list->DrawIndexed(Rectangle::GetIndexCount(), 0, 0);
            m_cmd_list->Submit();

            // Bilateral blur
            const auto sigma = 2.0f;
            const auto pixel_stride = 2.0f;
            Pass_BlurBilateralGaussian(m_render_tex_half_ssao, m_render_tex_half_ssao_blurred, sigma, pixel_stride);

            // Upscale to full size
            Pass_Upsample(m_render_tex_half_ssao_blurred, m_render_tex_ssao);
		}

		m_cmd_list->End();
	}

    void Renderer::Pass_Ssr()
    {
        // Acquire shaders
        const auto& shader_quad = m_shaders[Shader_Quad_V];
        const auto& shader_ssr  = m_shaders[Shader_Ssr_P];
        if (!shader_quad->IsCompiled() || !shader_ssr->IsCompiled())
            return;

        m_cmd_list->Begin("Pass_Ssr");
        m_cmd_list->ClearRenderTarget(m_render_tex_ssr->GetResource_RenderTarget(), Vector4::Zero);

        if (m_flags & Render_PostProcess_SSR)
        {
            // Pack textures
            void* textures[] =
            {
                m_g_buffer_normal->GetResource_Texture(),
                m_g_buffer_depth->GetResource_Texture(),
                m_g_buffer_material->GetResource_Texture(),
                m_render_tex_composition_previous->GetResource_Texture()
            };

            // Pack samplers
            vector<void*> samplers =
            {
                m_sampler_point_clamp->GetResource(),
                m_sampler_bilinear_clamp->GetResource()
            };

            // Update uber
            UpdateUberBuffer(m_render_tex_ssr->GetWidth(), m_render_tex_ssr->GetHeight());

            m_cmd_list->ClearTextures(); // avoids d3d11 warning where the render target is already bound as an input texture (from some previous pass)
            m_cmd_list->SetDepthStencilState(m_depth_stencil_disabled);
            m_cmd_list->SetRasterizerState(m_rasterizer_cull_back_solid);
            m_cmd_list->SetBlendState(m_blend_disabled);
            m_cmd_list->SetPrimitiveTopology(PrimitiveTopology_TriangleList);
            m_cmd_list->SetBufferVertex(m_quad.GetVertexBuffer());
            m_cmd_list->SetBufferIndex(m_quad.GetIndexBuffer());
            m_cmd_list->SetRenderTarget(m_render_tex_ssr);
            m_cmd_list->SetViewport(m_render_tex_ssr->GetViewport());
            m_cmd_list->SetShaderVertex(shader_quad);
            m_cmd_list->SetInputLayout(shader_quad->GetInputLayout());
            m_cmd_list->SetShaderPixel(shader_ssr);
            m_cmd_list->SetTextures(0, textures, 4);
            m_cmd_list->SetSamplers(0, samplers);
            m_cmd_list->SetConstantBuffer(0, Buffer_Global, m_uber_buffer);
            m_cmd_list->DrawIndexed(Rectangle::GetIndexCount(), 0, 0);
        }

        m_cmd_list->End();
        m_cmd_list->Submit();
    }

    void Renderer::Pass_Light()
    {
        // Acquire shaders
        const auto& shader_quad                 = m_shaders[Shader_Quad_V];
        const auto& shader_light_directional    = m_shaders[Shader_LightDirectional_P];
        const auto& shader_light_point          = m_shaders[Shader_LightPoint_P];
        const auto& shader_light_spot           = m_shaders[Shader_LightSpot_P];
        if (!shader_quad->IsCompiled() || !shader_light_directional->IsCompiled() || !shader_light_point->IsCompiled() || !shader_light_spot->IsCompiled())
            return;

        // Pack render targets
        const vector<void*> render_targets
        {
            m_render_tex_light_diffuse->GetResource_RenderTarget(),
            m_render_tex_light_specular->GetResource_RenderTarget()
        };

        // Pack samplers
        vector<void*> samplers = { m_sampler_point_clamp->GetResource(), m_sampler_compare_depth->GetResource(), m_sampler_bilinear_clamp->GetResource() };

        // Begin
        m_cmd_list->Begin("Pass_Light");
        m_cmd_list->SetDepthStencilState(m_depth_stencil_disabled);
        m_cmd_list->ClearRenderTargets(render_targets, Vector4::Zero);
        m_cmd_list->SetRenderTargets(render_targets);
        m_cmd_list->SetViewport(m_render_tex_light_diffuse->GetViewport());    
        m_cmd_list->SetRasterizerState(m_rasterizer_cull_back_solid);       
        m_cmd_list->SetPrimitiveTopology(PrimitiveTopology_TriangleList);
        m_cmd_list->SetBufferVertex(m_quad.GetVertexBuffer());
        m_cmd_list->SetBufferIndex(m_quad.GetIndexBuffer());
        m_cmd_list->SetShaderVertex(shader_quad);
        m_cmd_list->SetInputLayout(shader_quad->GetInputLayout());
        m_cmd_list->SetSamplers(0, samplers);
        m_cmd_list->SetBlendState(m_blend_color_add); // light accumulation

        // Update uber
        UpdateUberBuffer(m_render_tex_light_diffuse->GetWidth(), m_render_tex_light_diffuse->GetHeight());

        auto draw_lights = [this, &shader_light_directional, &shader_light_point, &shader_light_spot](Renderer_Object_Type type)
        {
            if (m_entities[type].empty())
                return;

            // Choose correct shader
            ShaderBuffered* shader = nullptr;
            if (type == Renderer_Object_LightDirectional)   shader = static_cast<ShaderBuffered*>(shader_light_directional.get());
            else if (type == Renderer_Object_LightPoint)    shader = static_cast<ShaderBuffered*>(shader_light_point.get());
            else if (type == Renderer_Object_LightSpot)     shader = static_cast<ShaderBuffered*>(shader_light_spot.get());

            // Draw
            for (const auto& entity : m_entities[type])
            {
                Light* light = entity->GetComponent<Light>().get();

                // Pack textures
                void* textures[] =
                {
                    m_g_buffer_normal->GetResource_Texture(),
                    m_g_buffer_material->GetResource_Texture(),
                    m_g_buffer_depth->GetResource_Texture(),
                    m_render_tex_ssao->GetResource_Texture(),
                    light->GetCastShadows() ? (light->GetLightType() == LightType_Directional  ? light->GetShadowMap()->GetResource_Texture() : nullptr) : nullptr,
                    light->GetCastShadows() ? (light->GetLightType() == LightType_Point        ? light->GetShadowMap()->GetResource_Texture() : nullptr) : nullptr,
                    light->GetCastShadows() ? (light->GetLightType() == LightType_Spot         ? light->GetShadowMap()->GetResource_Texture() : nullptr) : nullptr
                };

                // Update light buffer   
                light->UpdateConstantBuffer();
                const vector<void*> constant_buffers = { m_uber_buffer->GetResource(), light->GetConstantBuffer()->GetResource() };

                m_cmd_list->SetConstantBuffers(0, Buffer_Global, constant_buffers);
                m_cmd_list->SetTextures(0, textures, 7);
                m_cmd_list->SetShaderPixel(shader);
                m_cmd_list->DrawIndexed(Rectangle::GetIndexCount(), 0, 0);
                m_cmd_list->Submit();
            }
        };

        // Draw lights
        draw_lights(Renderer_Object_LightDirectional);
        draw_lights(Renderer_Object_LightPoint);
        draw_lights(Renderer_Object_LightSpot);

        m_cmd_list->End();
        m_cmd_list->Submit();
    }

	void Renderer::Pass_Composition(shared_ptr<RHI_Texture>& tex_out)
	{
        // Acquire shaders
        const auto& shader_quad         = m_shaders[Shader_Quad_V];
		const auto& shader_composition  = m_shaders[Shader_Composition_P];
		if (!shader_quad->IsCompiled() || !shader_composition->IsCompiled())
			return;

        // Begin command list
		m_cmd_list->Begin("Pass_Composition");

		// Update constant buffer
		UpdateUberBuffer(tex_out->GetWidth(), tex_out->GetHeight());

		// Pack resources
        void* skybox_texture    = m_skybox ? (m_skybox->GetTexture() ? m_skybox->GetTexture()->GetResource_Texture() : nullptr) : m_tex_white->GetResource_Texture();
        void* shadow_ssao       = m_render_tex_ssao ? m_render_tex_ssao->GetResource_Texture() : nullptr;
		void* textures[] =
		{
            m_g_buffer_albedo->GetResource_Texture(),			// Albedo	
            m_g_buffer_normal->GetResource_Texture(),			// Normal
            m_g_buffer_depth->GetResource_Texture(),			// Depth
            m_g_buffer_material->GetResource_Texture(),			// Material
            m_render_tex_light_diffuse->GetResource_Texture(),	// Diffuse
            m_render_tex_light_specular->GetResource_Texture(),	// Specular
            m_render_tex_ssr->GetResource_Texture(),            // SSR
			skybox_texture,	                                    // Environment
			m_tex_brdf_specular_lut->GetResource_Texture()		// LutIBL
		};
        const vector<void*> samplers =
        {
            m_sampler_bilinear_clamp->GetResource(),
            m_sampler_trilinear_clamp->GetResource(),
            m_sampler_point_clamp->GetResource()
        };

		// Setup command list
		m_cmd_list->SetDepthStencilState(m_depth_stencil_disabled);
		m_cmd_list->SetRasterizerState(m_rasterizer_cull_back_solid);
		m_cmd_list->SetBlendState(m_blend_disabled);
		m_cmd_list->SetPrimitiveTopology(PrimitiveTopology_TriangleList);
		m_cmd_list->SetViewport(tex_out->GetViewport());
		m_cmd_list->SetRenderTarget(tex_out);
		m_cmd_list->SetShaderVertex(shader_quad);
		m_cmd_list->SetInputLayout(shader_quad->GetInputLayout());
        m_cmd_list->SetShaderPixel(shader_composition);
		m_cmd_list->SetSamplers(0, samplers);
		m_cmd_list->SetTextures(0, textures, 9);
		m_cmd_list->SetBufferIndex(m_quad.GetIndexBuffer());
		m_cmd_list->SetBufferVertex(m_quad.GetVertexBuffer());
		m_cmd_list->DrawIndexed(Rectangle::GetIndexCount(), 0, 0);
		m_cmd_list->End();
		m_cmd_list->Submit();
	}

	void Renderer::Pass_PostComposision(shared_ptr<RHI_Texture>& tex_in, shared_ptr<RHI_Texture>& tex_out)
	{
		// Acquire shader
		const auto& shader_quad = m_shaders[Shader_Quad_V];
		if (!shader_quad->IsCompiled())
			return;

		// All post-process passes share the following, so set them once here
		m_cmd_list->Begin("Pass_PostComposision");
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
		if (FlagEnabled(Render_PostProcess_TAA))
		{
			Pass_TAA(tex_in, tex_out);
			swap_targets();
		}

		// Bloom
		if (FlagEnabled(Render_PostProcess_Bloom))
		{
			Pass_Bloom(tex_in, tex_out);
			swap_targets();
		}

		// Motion Blur
		if (FlagEnabled(Render_PostProcess_MotionBlur))
		{
			Pass_MotionBlur(tex_in, tex_out);
			swap_targets();
		}

		// Dithering
		if (FlagEnabled(Render_PostProcess_Dithering))
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
		if (FlagEnabled(Render_PostProcess_FXAA))
		{
			Pass_FXAA(tex_in, tex_out);
			swap_targets();
		}

		// Sharpening
		if (FlagEnabled(Render_PostProcess_Sharpening))
		{
			Pass_Sharpening(tex_in, tex_out);
			swap_targets();
		}

		// Chromatic aberration
		if (FlagEnabled(Render_PostProcess_ChromaticAberration))
		{
			Pass_ChromaticAberration(tex_in, tex_out);
			swap_targets();
		}

		// Gamma correction
		Pass_GammaCorrection(tex_in, tex_out);

		m_cmd_list->End();
		m_cmd_list->Submit();
	}

    void Renderer::Pass_Upsample(shared_ptr<RHI_Texture>& tex_in, shared_ptr<RHI_Texture>& tex_out)
    {
        // Acquire shader
        const auto& shader_vertex   = m_shaders[Shader_Quad_V];
        const auto& shader_pixel    = m_shaders[Shader_Upsample_P];
        if (!shader_vertex->IsCompiled() || !shader_pixel->IsCompiled())
            return;

        m_cmd_list->Begin("Upscale");
        UpdateUberBuffer(tex_out->GetWidth(), tex_out->GetHeight());
        m_cmd_list->SetDepthStencilState(m_depth_stencil_disabled);
        m_cmd_list->SetRenderTarget(tex_out);
        m_cmd_list->SetViewport(tex_out->GetViewport());
        m_cmd_list->SetShaderVertex(shader_vertex);
        m_cmd_list->SetShaderPixel(shader_pixel);
        m_cmd_list->SetTexture(0, tex_in);
        m_cmd_list->SetSampler(0, m_sampler_bilinear_clamp);
        m_cmd_list->SetConstantBuffer(0, Buffer_Global, m_uber_buffer);
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

		UpdateUberBuffer(tex_out->GetWidth(), tex_out->GetHeight());

		m_cmd_list->SetRenderTarget(tex_out);
        m_cmd_list->SetDepthStencilState(m_depth_stencil_disabled);
		m_cmd_list->SetViewport(tex_out->GetViewport());
		m_cmd_list->SetShaderPixel(shader_blurBox);
		m_cmd_list->SetTexture(0, tex_in); // Shadows are in the alpha channel
		m_cmd_list->SetSampler(0, m_sampler_trilinear_clamp);
		m_cmd_list->SetConstantBuffer(0, Buffer_Global, m_uber_buffer);
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

		UpdateUberBuffer(tex_in->GetWidth(), tex_in->GetHeight());

		auto shader_gaussian = static_pointer_cast<ShaderBuffered>(m_shaders[Shader_BlurGaussian_P]);

		// Start command list
		m_cmd_list->Begin("Pass_BlurGaussian");
        m_cmd_list->SetDepthStencilState(m_depth_stencil_disabled);
		m_cmd_list->SetViewport(tex_out->GetViewport());
		m_cmd_list->SetShaderPixel(shader_gaussian);
		m_cmd_list->SetSampler(0, m_sampler_bilinear_clamp);
		m_cmd_list->SetConstantBuffer(0, Buffer_Global, m_uber_buffer);

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

		UpdateUberBuffer(tex_in->GetWidth(), tex_in->GetHeight());

		// Start command list
		m_cmd_list->Begin("Pass_BlurBilateralGaussian");
        m_cmd_list->SetDepthStencilState(m_depth_stencil_disabled);
        m_cmd_list->SetBlendState(m_blend_disabled);
		m_cmd_list->SetViewport(tex_out->GetViewport());	
		m_cmd_list->SetShaderVertex(shader_quad);
		m_cmd_list->SetInputLayout(shader_quad->GetInputLayout());
		m_cmd_list->SetShaderPixel(shader_gaussianBilateral);	
		m_cmd_list->SetSampler(0, m_sampler_bilinear_clamp);
		m_cmd_list->SetConstantBuffer(0, Buffer_Global, m_uber_buffer);

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
        m_cmd_list->SetDepthStencilState(m_depth_stencil_disabled);

		// Resolve
		{
			// Prepare resources
			UpdateUberBuffer(m_render_tex_taa_current->GetWidth(), m_render_tex_taa_current->GetHeight());
			void* textures[] = { m_render_tex_taa_history->GetResource_Texture(), tex_in->GetResource_Texture(), m_g_buffer_velocity->GetResource_Texture(), m_g_buffer_depth->GetResource_Texture() };

			m_cmd_list->ClearTextures(); // avoids d3d11 warning where the render target is already bound as an input texture (from some previous pass)
			m_cmd_list->SetRenderTarget(m_render_tex_taa_current);
			m_cmd_list->SetViewport(m_render_tex_taa_current->GetViewport());
			m_cmd_list->SetShaderPixel(shader_taa);
			m_cmd_list->SetSampler(0, m_sampler_bilinear_clamp);
			m_cmd_list->SetTextures(0, textures, 3);
			m_cmd_list->SetConstantBuffer(0, Buffer_Global, m_uber_buffer);
			m_cmd_list->DrawIndexed(Rectangle::GetIndexCount(), 0, 0);
		}

		// Output to texOut
		{
			// Prepare resources
			UpdateUberBuffer(tex_out->GetWidth(), tex_out->GetHeight());

			m_cmd_list->SetRenderTarget(tex_out);
			m_cmd_list->SetViewport(tex_out->GetViewport());
			m_cmd_list->SetShaderPixel(shader_texture);
			m_cmd_list->SetSampler(0, m_sampler_point_clamp);
			m_cmd_list->SetTexture(0, m_render_tex_taa_current);
			m_cmd_list->SetConstantBuffer(0, Buffer_Global, m_uber_buffer);
			m_cmd_list->DrawIndexed(Rectangle::GetIndexCount(), 0, 0);
		}

		m_cmd_list->End();
		m_cmd_list->Submit();

		// Swap textures so current becomes history
		m_render_tex_taa_current.swap(m_render_tex_taa_history);
	}

	void Renderer::Pass_Bloom(shared_ptr<RHI_Texture>& tex_in, shared_ptr<RHI_Texture>& tex_out)
	{
		// Acquire shaders		
		const auto& shader_bloomBright	= m_shaders[Shader_BloomDownsampleLuminance_P];
		const auto& shader_bloomBlend	= m_shaders[Shader_BloomBlend_P];
		const auto& shader_downsample	= m_shaders[Shader_BloomDownsample_P];
		const auto& shader_upsample		= m_shaders[Shader_Upsample_P];
		if (!shader_downsample->IsCompiled() || !shader_bloomBright->IsCompiled() || !shader_upsample->IsCompiled() || !shader_downsample->IsCompiled())
			return;

		m_cmd_list->Begin("Pass_Bloom");
		m_cmd_list->SetSampler(0, m_sampler_bilinear_clamp);
        m_cmd_list->SetDepthStencilState(m_depth_stencil_disabled);
        m_cmd_list->SetBlendState(m_blend_disabled);

        m_cmd_list->Begin("DownscaleLuminance");
        {
            UpdateUberBuffer(m_render_tex_bloom[0]->GetWidth(), m_render_tex_bloom[0]->GetHeight());
            m_cmd_list->SetRenderTarget(m_render_tex_bloom[0]);
            m_cmd_list->SetViewport(m_render_tex_bloom[0]->GetViewport());
            m_cmd_list->SetShaderPixel(shader_bloomBright);
            m_cmd_list->SetTexture(0, tex_in);
            m_cmd_list->SetConstantBuffer(0, Buffer_Global, m_uber_buffer);
            m_cmd_list->DrawIndexed(Rectangle::GetIndexCount(), 0, 0);
        }
        m_cmd_list->End();

        auto downsample = [this, &shader_downsample](shared_ptr<RHI_Texture>& tex_in, shared_ptr<RHI_Texture>& tex_out)
        {
		    m_cmd_list->Begin("Downsample");
		    {
		    	UpdateUberBuffer(tex_out->GetWidth(), tex_out->GetHeight()); 
		    	m_cmd_list->SetRenderTarget(tex_out);
		    	m_cmd_list->SetViewport(tex_out->GetViewport());
		    	m_cmd_list->SetShaderPixel(shader_downsample);
		    	m_cmd_list->SetTexture(0, tex_in);
		    	m_cmd_list->SetConstantBuffer(0, Buffer_Global, m_uber_buffer);
		    	m_cmd_list->DrawIndexed(Rectangle::GetIndexCount(), 0, 0);
		    }
		    m_cmd_list->End();
            m_cmd_list->Submit(); // we have to submit because all downsample passes are using the same buffer
        };

        // Downsample
        // The last bloom texture is the same size as the previous one (it's used for the Gaussian pass below), so we skip it
        for (int i = 0; i < static_cast<int>(m_render_tex_bloom.size() - 1); i++)
        {
            downsample(m_render_tex_bloom[i], m_render_tex_bloom[i + 1]);
        }

        auto upsample = [this, &shader_upsample](shared_ptr<RHI_Texture>& tex_in, shared_ptr<RHI_Texture>& tex_out)
        {
            m_cmd_list->Begin("Upsample");
            {
                UpdateUberBuffer(tex_out->GetWidth(), tex_out->GetHeight());
                m_cmd_list->SetBlendState(m_blend_bloom); // blend with previous
                m_cmd_list->SetRenderTarget(tex_out);
                m_cmd_list->SetViewport(tex_out->GetViewport());
                m_cmd_list->SetShaderPixel(shader_upsample);
                m_cmd_list->SetTexture(0, tex_in);
                m_cmd_list->SetConstantBuffer(0, Buffer_Global, m_uber_buffer);
                m_cmd_list->DrawIndexed(Rectangle::GetIndexCount(), 0, 0);
            }
            m_cmd_list->End();
            m_cmd_list->Submit(); // we have to submit because all upsample passes are using the same buffer
        };

		// Upsample + blend
        for (int i = static_cast<int>(m_render_tex_bloom.size() - 1); i > 0; i--)
        {
            upsample(m_render_tex_bloom[i], m_render_tex_bloom[i - 1]);
        }
		
		m_cmd_list->Begin("Additive_Blending");
		{
			// Prepare resources
			UpdateUberBuffer(tex_out->GetWidth(), tex_out->GetHeight());
			void* textures[] = { tex_in->GetResource_Texture(), m_render_tex_bloom.front()->GetResource_Texture() };

            m_cmd_list->SetBlendState(m_blend_disabled);
			m_cmd_list->SetRenderTarget(tex_out);
			m_cmd_list->SetViewport(tex_out->GetViewport());
			m_cmd_list->SetShaderPixel(shader_bloomBlend);
			m_cmd_list->SetTextures(0, textures, 2);
			m_cmd_list->SetConstantBuffer(0, Buffer_Global, m_uber_buffer);
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
		UpdateUberBuffer(tex_out->GetWidth(), tex_out->GetHeight());

		m_cmd_list->ClearTextures(); // avoids d3d11 warning where the render target is already bound as an input texture (from previous pass)
        m_cmd_list->SetDepthStencilState(m_depth_stencil_disabled);
		m_cmd_list->SetRenderTarget(tex_out);
		m_cmd_list->SetViewport(tex_out->GetViewport());
		m_cmd_list->SetShaderPixel(shader_toneMapping);
		m_cmd_list->SetTexture(0, tex_in);
		m_cmd_list->SetSampler(0, m_sampler_point_clamp);
		m_cmd_list->SetConstantBuffer(0, Buffer_Global, m_uber_buffer);
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
		UpdateUberBuffer(tex_out->GetWidth(), tex_out->GetHeight());

		m_cmd_list->ClearTextures(); // avoids d3d11 warning where the render target is already bound as an input texture (from previous pass)
        m_cmd_list->SetDepthStencilState(m_depth_stencil_disabled);
		m_cmd_list->SetRenderTarget(tex_out);
		m_cmd_list->SetViewport(tex_out->GetViewport());
		m_cmd_list->SetShaderPixel(shader_gammaCorrection);
		m_cmd_list->SetTexture(0, tex_in);
		m_cmd_list->SetSampler(0, m_sampler_point_clamp);
		m_cmd_list->SetConstantBuffer(0, Buffer_Global, m_uber_buffer);
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
		UpdateUberBuffer(tex_out->GetWidth(), tex_out->GetHeight());

		m_cmd_list->ClearTextures(); // avoids d3d11 warning where the render target is already bound as an input texture (from previous pass)
        m_cmd_list->SetDepthStencilState(m_depth_stencil_disabled);
		m_cmd_list->SetRenderTarget(tex_out);
		m_cmd_list->SetViewport(tex_out->GetViewport());
		m_cmd_list->SetSampler(0, m_sampler_bilinear_clamp);
		m_cmd_list->SetConstantBuffer(0, Buffer_Global, m_uber_buffer);

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
		UpdateUberBuffer(tex_out->GetWidth(), tex_out->GetHeight());

		m_cmd_list->ClearTextures(); // avoids d3d11 warning where the render target is already bound as an input texture (from previous pass)
        m_cmd_list->SetDepthStencilState(m_depth_stencil_disabled);
		m_cmd_list->SetRenderTarget(tex_out);
		m_cmd_list->SetViewport(tex_out->GetViewport());
		m_cmd_list->SetShaderPixel(shader_chromaticAberration);
		m_cmd_list->SetTexture(0, tex_in);
		m_cmd_list->SetSampler(0, m_sampler_bilinear_clamp);
		m_cmd_list->SetConstantBuffer(0, Buffer_Global, m_uber_buffer);
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
		UpdateUberBuffer(tex_out->GetWidth(), tex_out->GetHeight());

		m_cmd_list->ClearTextures(); // avoids d3d11 warning where the render target is already bound as an input texture (from previous pass)
        m_cmd_list->SetDepthStencilState(m_depth_stencil_disabled);
		m_cmd_list->SetRenderTarget(tex_out);
		m_cmd_list->SetViewport(tex_out->GetViewport());
		m_cmd_list->SetShaderPixel(shader_motionBlur);
		m_cmd_list->SetSampler(0, m_sampler_bilinear_clamp);
		m_cmd_list->SetTextures(0, textures, 2);
		m_cmd_list->SetConstantBuffer(0, Buffer_Global, m_uber_buffer);
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
		UpdateUberBuffer(tex_out->GetWidth(), tex_out->GetHeight());

		m_cmd_list->ClearTextures(); // avoids d3d11 warning where the render target is already bound as an input texture (from previous pass)
        m_cmd_list->SetDepthStencilState(m_depth_stencil_disabled);
		m_cmd_list->SetRenderTarget(tex_out);
		m_cmd_list->SetViewport(tex_out->GetViewport());
		m_cmd_list->SetShaderPixel(shader_dithering);
		m_cmd_list->SetSampler(0, m_sampler_point_clamp);
		m_cmd_list->SetTexture(0, tex_in);
		m_cmd_list->SetConstantBuffer(0, Buffer_Global, m_uber_buffer);
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
		UpdateUberBuffer(tex_out->GetWidth(), tex_out->GetHeight());
	
		m_cmd_list->ClearTextures(); // avoids d3d11 warning where the render target is already bound as an input texture (from previous pass)
        m_cmd_list->SetDepthStencilState(m_depth_stencil_disabled);
		m_cmd_list->SetRenderTarget(tex_out);
		m_cmd_list->SetViewport(tex_out->GetViewport());		
		m_cmd_list->SetShaderPixel(shader_sharperning);
		m_cmd_list->SetTexture(0, tex_in);
		m_cmd_list->SetSampler(0, m_sampler_bilinear_clamp);
		m_cmd_list->SetConstantBuffer(0, Buffer_Global, m_uber_buffer);
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
				for (const auto& entity : m_entities[Renderer_Object_Opaque])
				{
					if (auto renderable = entity->GetRenderable_PtrRaw())
					{
						DrawBox(renderable->GetAabb(), Vector4(0.41f, 0.86f, 1.0f, 1.0f));
					}
				}

				for (const auto& entity : m_entities[Renderer_Object_Transparent])
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
				UpdateUberBuffer
				(
					static_cast<uint32_t>(m_resolution.x),
					static_cast<uint32_t>(m_resolution.y),
					m_gizmo_grid->ComputeWorldMatrix(m_camera->GetTransform()) * view_projection_unjittered
				);
				m_cmd_list->SetBufferIndex(m_gizmo_grid->GetIndexBuffer());
				m_cmd_list->SetBufferVertex(m_gizmo_grid->GetVertexBuffer());
				m_cmd_list->SetBlendState(m_blend_enabled);
				m_cmd_list->SetConstantBuffer(0, Buffer_Global, m_uber_buffer);
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

				UpdateUberBuffer(static_cast<uint32_t>(m_resolution.x), static_cast<uint32_t>(m_resolution.y), view_projection_unjittered);
				m_cmd_list->SetBufferVertex(m_vertex_buffer_lines);
				m_cmd_list->SetConstantBuffer(0, Buffer_Global, m_uber_buffer);
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
				UpdateUberBuffer(static_cast<uint32_t>(m_resolution.x), static_cast<uint32_t>(m_resolution.y), view_projection_unjittered);
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

		auto& lights = m_entities[Renderer_Object_Light];
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

				UpdateUberBuffer(static_cast<uint32_t>(tex_width), static_cast<uint32_t>(tex_width), m_view_projection_orthographic);

				m_cmd_list->SetShaderVertex(shader_quad);
				m_cmd_list->SetInputLayout(shader_quad->GetInputLayout());
				m_cmd_list->SetShaderPixel(m_shaders[Shader_Texture_P]);
				m_cmd_list->SetSampler(0, m_sampler_bilinear_clamp);
				m_cmd_list->SetConstantBuffer(0, Buffer_Global, m_uber_buffer);
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

			UpdateUberBuffer(static_cast<uint32_t>(m_resolution.x), static_cast<uint32_t>(m_resolution.y), m_view_projection_orthographic);

			auto const& shader_gizmoTransform = static_pointer_cast<ShaderBuffered>(m_shaders[Shader_GizmoTransform_Vp]);

			m_cmd_list->SetShaderVertex(shader_gizmoTransform);
			m_cmd_list->SetShaderPixel(shader_gizmoTransform);
			m_cmd_list->SetInputLayout(shader_gizmoTransform->GetInputLayout());
			m_cmd_list->SetBufferIndex(m_gizmo_transform->GetIndexBuffer());
			m_cmd_list->SetBufferVertex(m_gizmo_transform->GetVertexBuffer());
			m_cmd_list->SetConstantBuffer(0, Buffer_Global, m_uber_buffer);

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
        // Early exit cases
        const bool draw         = m_flags & Render_Gizmo_PerformanceMetrics;
        const bool empty        = m_profiler->GetMetrics().empty();
        const auto& shader_font = static_pointer_cast<ShaderBuffered>(m_shaders[Shader_Font_Vp]);
        if (!draw || empty || !shader_font->IsCompiled())
            return;

		m_cmd_list->Begin("Pass_PerformanceMetrics");

		// Update text
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
		if (m_debug_buffer == Renderer_Buffer_None)
			return true;

		// Bind correct texture & shader pass
        shared_ptr<RHI_Texture> texture;
        Shader_Type shader_type;
		if (m_debug_buffer == Renderer_Buffer_Albedo)
		{
			texture     = m_g_buffer_albedo;
			shader_type = Shader_Texture_P;
		}

		if (m_debug_buffer == Renderer_Buffer_Normal)
		{
			texture     = m_g_buffer_normal;
			shader_type = Shader_DebugNormal_P;
		}

		if (m_debug_buffer == Renderer_Buffer_Material)
		{
			texture     = m_g_buffer_material;
			shader_type = Shader_Texture_P;
		}

        if (m_debug_buffer == Renderer_Buffer_Diffuse)
        {
            texture     = m_render_tex_light_diffuse;
            shader_type = Shader_DebugChannelRgbGammaCorrect_P;
        }

        if (m_debug_buffer == Renderer_Buffer_Specular)
        {
            texture     = m_render_tex_light_specular;
            shader_type = Shader_DebugChannelRgbGammaCorrect_P;
        }

		if (m_debug_buffer == Renderer_Buffer_Velocity)
		{
			texture     = m_g_buffer_velocity;
			shader_type = Shader_DebugVelocity_P;
		}

		if (m_debug_buffer == Renderer_Buffer_Depth)
		{
			texture     = m_g_buffer_depth;
			shader_type = Shader_DebugChannelR_P;
		}

		if (m_debug_buffer == Renderer_Buffer_SSAO)
		{
			texture     = m_flags & Render_PostProcess_SSAO ? m_render_tex_ssao : m_tex_white;
			shader_type = Shader_DebugChannelR_P;
		}

        if (m_debug_buffer == Renderer_Buffer_SSR)
        {
            texture     = m_render_tex_ssr;
            shader_type = Shader_DebugChannelRgbGammaCorrect_P;
        }

        if (m_debug_buffer == Renderer_Buffer_Bloom)
        {
            texture     = m_render_tex_bloom.front();
            shader_type = Shader_DebugChannelRgbGammaCorrect_P;
        }

        if (m_debug_buffer == Renderer_Buffer_Shadows)
        {
            texture     = m_render_tex_light_diffuse;
            shader_type = Shader_DebugChannelA_P;
        }

        // Acquire shaders
        const auto& shader_quad     = m_shaders[Shader_Quad_V];
        const auto& shader_pixel    = m_shaders[shader_type];
        if (!shader_quad->IsCompiled() || !shader_pixel->IsCompiled())
            return false;

        // Draw
        m_cmd_list->Begin("Pass_DebugBuffer");
        UpdateUberBuffer(tex_out->GetWidth(), tex_out->GetHeight(), m_view_projection_orthographic);
		m_cmd_list->SetDepthStencilState(m_depth_stencil_disabled);
		m_cmd_list->SetRasterizerState(m_rasterizer_cull_back_solid);
		m_cmd_list->SetBlendState(m_blend_disabled);
		m_cmd_list->SetPrimitiveTopology(PrimitiveTopology_TriangleList);
		m_cmd_list->SetRenderTarget(tex_out);
		m_cmd_list->SetViewport(tex_out->GetViewport());
		m_cmd_list->SetShaderVertex(shader_quad);
		m_cmd_list->SetInputLayout(shader_quad->GetInputLayout());
        m_cmd_list->SetShaderPixel(shader_pixel);
        m_cmd_list->SetTexture(0, texture);
		m_cmd_list->SetSampler(0, m_sampler_bilinear_clamp);
		m_cmd_list->SetConstantBuffer(0, Buffer_Global, m_uber_buffer);
		m_cmd_list->SetBufferVertex(m_quad.GetVertexBuffer());
		m_cmd_list->SetBufferIndex(m_quad.GetIndexBuffer());
		m_cmd_list->DrawIndexed(Rectangle::GetIndexCount(), 0, 0);
		m_cmd_list->End();
		m_cmd_list->Submit();

		return true;
	}

    void Renderer::Pass_BrdfSpecularLut()
    {
        if (m_brdf_specular_lut_rendered)
            return;

        // Acquire shaders
        const auto& shader_quad                 = m_shaders[Shader_Quad_V];
        const auto& shader_brdf_specular_lut    = m_shaders[Shader_BrdfSpecularLut];
        if (!shader_quad->IsCompiled() || !shader_brdf_specular_lut->IsCompiled())
            return;

        m_cmd_list->Begin("Pass_BrdfSpecularLut");
        UpdateUberBuffer(m_tex_brdf_specular_lut->GetWidth(), m_tex_brdf_specular_lut->GetHeight());
        m_cmd_list->SetDepthStencilState(m_depth_stencil_disabled);
        m_cmd_list->SetRasterizerState(m_rasterizer_cull_back_solid);
        m_cmd_list->SetBlendState(m_blend_disabled);
        m_cmd_list->SetPrimitiveTopology(PrimitiveTopology_TriangleList);
        m_cmd_list->SetBufferVertex(m_quad.GetVertexBuffer());
        m_cmd_list->SetBufferIndex(m_quad.GetIndexBuffer());
        m_cmd_list->SetRenderTarget(m_tex_brdf_specular_lut);
        m_cmd_list->SetViewport(m_tex_brdf_specular_lut->GetViewport());
        m_cmd_list->SetShaderVertex(shader_quad);
        m_cmd_list->SetInputLayout(shader_quad->GetInputLayout());
        m_cmd_list->SetShaderPixel(shader_brdf_specular_lut);
        m_cmd_list->SetConstantBuffer(0, Buffer_Global, m_uber_buffer);
        m_cmd_list->DrawIndexed(Rectangle::GetIndexCount(), 0, 0);
        m_cmd_list->End();
        m_cmd_list->Submit();

        m_brdf_specular_lut_rendered = true;
    }
}
