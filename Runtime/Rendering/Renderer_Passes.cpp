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
#include "ShaderVariation.h"
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
#include "../World/Components/Environment.h"
#include "../World/Components/Light.h"
#include "../World/Components/Camera.h"
//=========================================

//= NAMESPACES ===============
using namespace std;
using namespace Spartan::Math;
//============================

namespace Spartan
{
    void Renderer::Pass_Setup()
    {
#ifdef API_GRAPHICS_VULKAN
        return;
#endif

        m_cmd_list->Begin("Pass_Setup");

        // Bind the buffers we will be using thought the frame
        {
            m_cmd_list->SetConstantBuffer(0, Buffer_Global, m_buffer_frame_gpu);
            m_cmd_list->SetConstantBuffer(1, Buffer_Global, m_buffer_uber_gpu);
            m_cmd_list->SetConstantBuffer(2, Buffer_PixelShader, m_buffer_light_gpu);
        }
        
        // Set the samplers we will be using thought the frame
        {
            vector<void*> samplers =
            {
                m_sampler_compare_depth->GetResource(),
                m_sampler_point_clamp->GetResource(),
                m_sampler_bilinear_clamp->GetResource(),
                m_sampler_bilinear_wrap->GetResource(),
                m_sampler_trilinear_clamp->GetResource(),
                m_sampler_anisotropic_wrap->GetResource(),
            };
            m_cmd_list->SetSamplers(0, samplers);
        }

        m_cmd_list->End();
        m_cmd_list->Submit();
    }

    void Renderer::Pass_Main()
	{
        // Validate RHI device as it's required almost everywhere
        if (!m_rhi_device)
            return;

#ifdef API_GRAPHICS_VULKAN
        return;
#endif
		m_cmd_list->Begin("Pass_Main");

        // Update the frame buffer (doesn't change thought the frame)
        m_cmd_list->Begin("UpdateFrameBuffer");
        {
            UpdateFrameBuffer();
        }
        m_cmd_list->End();
        Pass_BrdfSpecularLut(); // only happens once
		Pass_LightDepth();
        if (GetOptionValue(Render_DepthPrepass))
        {
            Pass_DepthPrePass();
        }
		Pass_GBuffer();
		Pass_Ssao();
        Pass_Ssr();
        Pass_Light();
        Pass_Composition();
		Pass_PostProcess();
        Pass_Lines(m_render_targets[RenderTarget_Composition_Ldr]);
        Pass_Gizmos(m_render_targets[RenderTarget_Composition_Ldr]);
		Pass_DebugBuffer(m_render_targets[RenderTarget_Composition_Ldr]);
		Pass_PerformanceMetrics(m_render_targets[RenderTarget_Composition_Ldr]);

		m_cmd_list->End();
		m_cmd_list->Submit();
	}

	void Renderer::Pass_LightDepth()
	{
        // Description: All the opaque meshes are rendered (from the lights point of view),
        // outputting just their depth information into a depth map.

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

        m_cmd_list->Begin("Pass_LightDepth");

        for (uint32_t light_index = 0; light_index < entities_light.size(); light_index++)
        {
			const Light* light = entities_light[light_index]->GetComponent<Light>().get();

            // Light can be null if it just got removed and our buffer doesn't update till the next frame
            if (!light)
                break;

			// Acquire light's shadow map
			const auto& shadow_map = light->GetShadowMap();
			if (!shadow_map)
				continue;

            // Begin command list
            m_cmd_list->Begin("Light");	
			m_cmd_list->SetBlendState(m_blend_disabled);
			m_cmd_list->SetDepthStencilState(m_depth_stencil_enabled_write);
			m_cmd_list->SetPrimitiveTopology(PrimitiveTopology_TriangleList);
            m_cmd_list->SetShaderPixel(nullptr);
			m_cmd_list->SetShaderVertex(shader_depth);
			m_cmd_list->SetInputLayout(shader_depth->GetInputLayout());
			m_cmd_list->SetViewport(shadow_map->GetViewport());

            // Set appropriate rasterizer state
            if (light->GetLightType() == LightType_Directional)
            {
                // "Pancaking" - https://www.gamedev.net/forums/topic/639036-shadow-mapping-and-high-up-objects/
                // It's basically a way to capture the silhouettes of potential shadow casters behind the light's view point.
                // Of course we also have to make sure that the light doesn't cull them in the first place (this is done automatically by the light)
                m_cmd_list->SetRasterizerState(m_rasterizer_cull_back_solid_no_clip);
            }
            else
            {
                m_cmd_list->SetRasterizerState(m_rasterizer_cull_back_solid);
            }

			// Tracking
			uint32_t currently_bound_geometry = 0;

			for (uint32_t i = 0; i < shadow_map->GetArraySize(); i++)
			{
				const auto cascade_depth_stencil    = shadow_map->GetResource_DepthStencil(i);
                const Matrix& view_projection       = light->GetViewMatrix(i) * light->GetProjectionMatrix(i);

				m_cmd_list->Begin("Array_" + to_string(i + 1));
                m_cmd_list->SetRenderTarget(nullptr, cascade_depth_stencil);
				m_cmd_list->ClearDepthStencil(cascade_depth_stencil, Clear_Depth, GetClearDepth());

                // Skip if it doesn't need to cast shadows
                if (!light->GetCastShadows())
                {
                    m_cmd_list->End(); // end of array
                    continue;
                }

				for (const auto& entity : entities_opaque)
				{
					// Acquire renderable component
					const auto& renderable = entity->GetRenderable_PtrRaw();
					if (!renderable)
						continue;

                    // Skip objects outside of the view frustum
                    if (!light->IsInViewFrustrum(renderable, i))
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

                    // Update uber buffer with cascade transform
                    m_buffer_uber_cpu.transform = entity->GetTransform_PtrRaw()->GetMatrix() * view_projection;
                    UpdateUberBuffer(); // only updates if needed

					m_cmd_list->DrawIndexed(renderable->GeometryIndexCount(), renderable->GeometryIndexOffset(), renderable->GeometryVertexOffset());
                    m_cmd_list->Submit();
				}
				m_cmd_list->End(); // end of array
			}
            m_cmd_list->End(); // end light
		}

        m_cmd_list->End(); // end lights
        m_cmd_list->Submit();
	}

    void Renderer::Pass_DepthPrePass()
    {
        // Description: All the opaque meshes are rendered, outputting
        // just their depth information into a depth map.

        // Acquire required resources/data
        const auto& shader_depth    = m_shaders[Shader_Depth_V];
        const auto& tex_depth       = m_render_targets[RenderTarget_Gbuffer_Depth];
        const auto& entities        = m_entities[Renderer_Object_Opaque];

        // Ensure the shader has compiled
        if (!shader_depth->IsCompiled())
            return;

        // Star command list
        m_cmd_list->Begin("Pass_DepthPrePass");
        m_cmd_list->ClearDepthStencil(tex_depth->GetResource_DepthStencil(), Clear_Depth, GetClearDepth());

        if (!entities.empty())
        {
            m_cmd_list->SetRasterizerState(m_rasterizer_cull_back_solid);
            m_cmd_list->SetBlendState(m_blend_disabled);
            m_cmd_list->SetDepthStencilState(m_depth_stencil_enabled_write);
            m_cmd_list->SetViewport(tex_depth->GetViewport());
            m_cmd_list->SetRenderTarget(nullptr, tex_depth->GetResource_DepthStencil());
            m_cmd_list->SetShaderVertex(shader_depth);
            m_cmd_list->SetShaderPixel(nullptr);
            m_cmd_list->SetPrimitiveTopology(PrimitiveTopology_TriangleList);
            m_cmd_list->SetInputLayout(shader_depth->GetInputLayout());

            // Variables that help reduce state changes
            uint32_t currently_bound_geometry = 0;

            // Draw opaque
            for (const auto& entity : entities)
            {
                // Get renderable
                const auto& renderable = entity->GetRenderable_PtrRaw();
                if (!renderable)
                    continue;

                // Get geometry
                const auto& model = renderable->GeometryModel();
                if (!model || !model->GetVertexBuffer() || !model->GetIndexBuffer())
                    continue;

                // Skip objects outside of the view frustum
                if (!m_camera->IsInViewFrustrum(renderable))
                    continue;

                // Bind geometry
                if (currently_bound_geometry != model->GetId())
                {
                    m_cmd_list->SetBufferIndex(model->GetIndexBuffer());
                    m_cmd_list->SetBufferVertex(model->GetVertexBuffer());
                    currently_bound_geometry = model->GetId();
                }

                // Update uber buffer with entity transform
                if (Transform* transform = entity->GetTransform_PtrRaw())
                {
                    // Update uber buffer with cascade transform
                    m_buffer_uber_cpu.transform = transform->GetMatrix() * m_buffer_frame_cpu.view_projection;
                    UpdateUberBuffer(); // only updates if needed
                }

                // Draw	
                m_cmd_list->DrawIndexed(renderable->GeometryIndexCount(), renderable->GeometryIndexOffset(), renderable->GeometryVertexOffset());
                m_cmd_list->Submit();
            }
        }

        m_cmd_list->End();
        m_cmd_list->Submit();
    }

	void Renderer::Pass_GBuffer()
	{
        // Acquire required resources/shaders
        const auto& tex_albedo      = m_render_targets[RenderTarget_Gbuffer_Albedo];
        const auto& tex_normal      = m_render_targets[RenderTarget_Gbuffer_Normal];
        const auto& tex_material    = m_render_targets[RenderTarget_Gbuffer_Material];
        const auto& tex_velocity    = m_render_targets[RenderTarget_Gbuffer_Velocity];
        const auto& tex_depth       = m_render_targets[RenderTarget_Gbuffer_Depth];
        const auto& clear_color     = Vector4::Zero;
        const auto& shader_gbuffer  = m_shaders[Shader_Gbuffer_V];

        // Validate that the shader has compiled
        if (!shader_gbuffer->IsCompiled())
            return;

        // Pack render targets
        void* render_targets[]
        {
            tex_albedo->GetResource_RenderTarget(),
            tex_normal->GetResource_RenderTarget(),
            tex_material->GetResource_RenderTarget(),
            tex_velocity->GetResource_RenderTarget()
        };

        // Star command list
        m_cmd_list->Begin("Pass_GBuffer");
        m_cmd_list->ClearRenderTarget(tex_albedo->GetResource_RenderTarget(),   clear_color);
        m_cmd_list->ClearRenderTarget(tex_normal->GetResource_RenderTarget(),   clear_color);
        m_cmd_list->ClearRenderTarget(tex_material->GetResource_RenderTarget(), Vector4::Zero); // zeroed material buffer causes sky sphere to render
        m_cmd_list->ClearRenderTarget(tex_velocity->GetResource_RenderTarget(), clear_color);
        if (!GetOptionValue(Render_DepthPrepass))
        {
            m_cmd_list->ClearDepthStencil(tex_depth->GetResource_DepthStencil(), Clear_Depth, GetClearDepth());
        }

        if (!m_entities[Renderer_Object_Opaque].empty())
        {
            m_cmd_list->SetRasterizerState(m_rasterizer_cull_back_solid);
            m_cmd_list->SetBlendState(m_blend_disabled);
            m_cmd_list->SetDepthStencilState(GetOptionValue(Render_DepthPrepass) ? m_depth_stencil_enabled_no_write : m_depth_stencil_enabled_write);
            m_cmd_list->SetPrimitiveTopology(PrimitiveTopology_TriangleList);
            m_cmd_list->SetViewport(tex_albedo->GetViewport());
            m_cmd_list->SetRenderTargets(render_targets, 4, tex_depth->GetResource_DepthStencil());
            m_cmd_list->SetShaderVertex(shader_gbuffer);
            m_cmd_list->SetInputLayout(shader_gbuffer->GetInputLayout());

            // Variables that help reduce state changes
            uint32_t currently_bound_geometry   = 0;
            uint32_t currently_bound_shader     = 0;
            uint32_t currently_bound_material   = 0;

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

                // Get shader
                const auto& shader = material->GetShader();
                if (!shader || !shader->IsCompiled())
                    return;

                // Get geometry
                const auto& model = renderable->GeometryModel();
                if (!model || !model->GetVertexBuffer() || !model->GetIndexBuffer())
                    return;

                // Skip objects outside of the view frustum
                if (!m_camera->IsInViewFrustrum(renderable))
                    return;

                // Set face culling (changes only if required)
                m_cmd_list->SetRasterizerState(GetRasterizerState(material->GetCullMode(), !GetOptionValue(Render_Debug_Wireframe) ? Fill_Solid : Fill_Wireframe));

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

                    // Update uber buffer with material properties
                    m_buffer_uber_cpu.mat_albedo        = material->GetColorAlbedo();
                    m_buffer_uber_cpu.mat_tiling_uv     = material->GetTiling();
                    m_buffer_uber_cpu.mat_offset_uv     = material->GetOffset();
                    m_buffer_uber_cpu.mat_roughness_mul = material->GetMultiplier(TextureType_Roughness);
                    m_buffer_uber_cpu.mat_metallic_mul  = material->GetMultiplier(TextureType_Metallic);
                    m_buffer_uber_cpu.mat_normal_mul    = material->GetMultiplier(TextureType_Normal);
                    m_buffer_uber_cpu.mat_height_mul    = material->GetMultiplier(TextureType_Height);
                    m_buffer_uber_cpu.mat_shading_mode  = static_cast<float>(material->GetShadingMode());

                    currently_bound_material = material->GetId();
                }

                // Update uber buffer with entity transform
                if (Transform* transform = entity->GetTransform_PtrRaw())
                {
                    m_buffer_uber_cpu.transform     = transform->GetMatrix();
                    m_buffer_uber_cpu.wvp_current   = transform->GetMatrix() * m_buffer_frame_cpu.view_projection;
                    m_buffer_uber_cpu.wvp_previous  = transform->GetWvpLastFrame();
                    transform->SetWvpLastFrame(m_buffer_uber_cpu.wvp_current);
                }

                // Only happens if needed
                UpdateUberBuffer();

                // Render	
                m_cmd_list->DrawIndexed(renderable->GeometryIndexCount(), renderable->GeometryIndexOffset(), renderable->GeometryVertexOffset());
                m_profiler->m_renderer_meshes_rendered++;

                m_cmd_list->Submit();
            };

            // Draw opaque
            for (const auto& entity : m_entities[Renderer_Object_Opaque])
            {
                draw_entity(entity);
            }

            // Draw transparent (transparency of the poor)
            m_cmd_list->SetBlendState(m_blend_enabled);
            m_cmd_list->SetDepthStencilState(m_depth_stencil_disabled);
            for (const auto& entity : m_entities[Renderer_Object_Transparent])
            {
                draw_entity(entity);
            }
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

        // Acquire render targets
        auto& tex_ssao_raw      = m_render_targets[RenderTarget_Ssao_Raw];
        auto& tex_ssao_blurred  = m_render_targets[RenderTarget_Ssao_Blurred];
        auto& tex_ssao          = m_render_targets[RenderTarget_Ssao];

		m_cmd_list->Begin("Pass_Ssao");
		m_cmd_list->ClearRenderTarget(tex_ssao_raw->GetResource_RenderTarget(), Vector4::One);
        m_cmd_list->ClearRenderTarget(tex_ssao->GetResource_RenderTarget(),     Vector4::One);

		if (m_options & Render_SSAO)
		{
            // Pack resources	
            void* textures[] =
            {
                m_render_targets[RenderTarget_Gbuffer_Depth]->GetResource_Texture(),
                m_render_targets[RenderTarget_Gbuffer_Normal]->GetResource_Texture(),
                m_tex_noise_normal->GetResource_Texture()
            };

            // Update uber buffer
            m_buffer_uber_cpu.resolution = Vector2(tex_ssao_raw->GetWidth(), tex_ssao_raw->GetHeight());
            UpdateUberBuffer();

            m_cmd_list->ClearTextures(); // avoids d3d11 warning where the render target is already bound as an input texture (from some previous pass)
            m_cmd_list->SetDepthStencilState(m_depth_stencil_disabled);
            m_cmd_list->SetRasterizerState(m_rasterizer_cull_back_solid);
            m_cmd_list->SetBlendState(m_blend_disabled);
            m_cmd_list->SetPrimitiveTopology(PrimitiveTopology_TriangleList);
            m_cmd_list->SetBufferVertex(m_quad.GetVertexBuffer());
            m_cmd_list->SetBufferIndex(m_quad.GetIndexBuffer());
            m_cmd_list->SetRenderTarget(tex_ssao_raw);
            m_cmd_list->SetViewport(tex_ssao_raw->GetViewport());
            m_cmd_list->SetShaderVertex(shader_quad);
            m_cmd_list->SetInputLayout(shader_quad->GetInputLayout());
            m_cmd_list->SetShaderPixel(shader_ssao);
            m_cmd_list->SetTextures(0, textures, 3);
            m_cmd_list->DrawIndexed(Rectangle::GetIndexCount(), 0, 0);
            m_cmd_list->Submit();

            // Bilateral blur
            const auto sigma = 2.0f;
            const auto pixel_stride = 2.0f;
            Pass_BlurBilateralGaussian(tex_ssao_raw, tex_ssao_blurred, sigma, pixel_stride);

            // Upscale to full size
            float ssao_scale = m_option_values[Option_Value_Ssao_Scale];
            if (ssao_scale < 1.0f)
            {
                Pass_Upsample(tex_ssao_blurred, tex_ssao);
            }
            else if (ssao_scale > 1.0f)
            {
                Pass_Downsample(tex_ssao_blurred, tex_ssao, Shader_Downsample_P);
            }
            else
            {
                tex_ssao_blurred.swap(tex_ssao);
            }
		}

		m_cmd_list->End();
        m_cmd_list->Submit();
	}

    void Renderer::Pass_Ssr()
    {
        // Acquire shaders
        const auto& shader_quad = m_shaders[Shader_Quad_V];
        const auto& shader_ssr  = m_shaders[Shader_Ssr_P];
        if (!shader_quad->IsCompiled() || !shader_ssr->IsCompiled())
            return;

        // Acquire render targets
        auto& tex_ssr         = m_render_targets[RenderTarget_Ssr];
        auto& tex_ssr_blurred = m_render_targets[RenderTarget_Ssr_Blurred];

        m_cmd_list->Begin("Pass_Ssr");
        
        if (m_options & Render_SSR)
        {
            // Pack textures
            void* textures[] =
            {
                m_render_targets[RenderTarget_Gbuffer_Normal]->GetResource_Texture(),
                m_render_targets[RenderTarget_Gbuffer_Depth]->GetResource_Texture(),
                m_render_targets[RenderTarget_Gbuffer_Material]->GetResource_Texture(),
                m_render_targets[RenderTarget_Composition_Ldr_2]->GetResource_Texture()
            };

            // Update uber buffer
            m_buffer_uber_cpu.resolution = Vector2(tex_ssr->GetWidth(), tex_ssr->GetHeight());
            UpdateUberBuffer();

            m_cmd_list->ClearTextures(); // avoids d3d11 warning where the render target is already bound as an input texture (from some previous pass)
            m_cmd_list->SetDepthStencilState(m_depth_stencil_disabled);
            m_cmd_list->SetRasterizerState(m_rasterizer_cull_back_solid);
            m_cmd_list->SetBlendState(m_blend_disabled);
            m_cmd_list->SetPrimitiveTopology(PrimitiveTopology_TriangleList);
            m_cmd_list->SetBufferVertex(m_quad.GetVertexBuffer());
            m_cmd_list->SetBufferIndex(m_quad.GetIndexBuffer());
            m_cmd_list->SetRenderTarget(tex_ssr);
            m_cmd_list->SetViewport(tex_ssr->GetViewport());
            m_cmd_list->SetShaderVertex(shader_quad);
            m_cmd_list->SetInputLayout(shader_quad->GetInputLayout());
            m_cmd_list->SetShaderPixel(shader_ssr);
            m_cmd_list->SetTextures(0, textures, 4);
            m_cmd_list->DrawIndexed(Rectangle::GetIndexCount(), 0, 0);
            m_cmd_list->Submit();

            // Bilateral blur
            const auto sigma = 1.0f;
            const auto pixel_stride = 1.0f;
            Pass_BlurGaussian(tex_ssr, tex_ssr_blurred, sigma, pixel_stride);
        }
        else
        {
            m_cmd_list->ClearRenderTarget(tex_ssr->GetResource_RenderTarget(), Vector4(0.0f, 0.0f, 0.0f, 1.0f));
            m_cmd_list->ClearRenderTarget(tex_ssr_blurred->GetResource_RenderTarget(), Vector4(0.0f, 0.0f, 0.0f, 1.0f));
            m_cmd_list->Submit();
        }

        m_cmd_list->End();
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

        // Acquire render targets
        auto& tex_diffuse       = m_render_targets[RenderTarget_Light_Diffuse];
        auto& tex_specular      = m_render_targets[RenderTarget_Light_Specular];
        auto& tex_volumetric    = m_render_targets[RenderTarget_Light_Volumetric];

        // Pack render targets
        void* render_targets[]
        {
            tex_diffuse->GetResource_RenderTarget(),
            tex_specular->GetResource_RenderTarget(),
            tex_volumetric->GetResource_RenderTarget()
        };

        // Begin
        m_cmd_list->Begin("Pass_Light");

        // Update uber buffer
        m_buffer_uber_cpu.resolution = Vector2(static_cast<float>(tex_diffuse->GetWidth()), static_cast<float>(tex_diffuse->GetHeight()));
        UpdateUberBuffer();

        m_cmd_list->ClearRenderTarget(render_targets[0], Vector4::Zero);
        m_cmd_list->ClearRenderTarget(render_targets[1], Vector4::Zero);
        m_cmd_list->ClearRenderTarget(render_targets[2], Vector4::Zero);
        m_cmd_list->SetRenderTargets(render_targets, 3);
        m_cmd_list->SetDepthStencilState(m_depth_stencil_disabled);
        m_cmd_list->SetViewport(tex_diffuse->GetViewport());
        m_cmd_list->SetRasterizerState(m_rasterizer_cull_back_solid);       
        m_cmd_list->SetPrimitiveTopology(PrimitiveTopology_TriangleList);
        m_cmd_list->SetBufferVertex(m_quad.GetVertexBuffer());
        m_cmd_list->SetBufferIndex(m_quad.GetIndexBuffer());
        m_cmd_list->SetShaderVertex(shader_quad);
        m_cmd_list->SetInputLayout(shader_quad->GetInputLayout());
        m_cmd_list->SetBlendState(m_blend_color_add); // light accumulation
        
        auto draw_lights = [this, &shader_light_directional, &shader_light_point, &shader_light_spot](Renderer_Object_Type type)
        {
            const vector<Entity*>& entities = m_entities[type];
            if (entities.empty())
                return;

            // Choose correct shader
            RHI_Shader* shader = nullptr;
            if (type == Renderer_Object_LightDirectional)   shader = shader_light_directional.get();
            else if (type == Renderer_Object_LightPoint)    shader = shader_light_point.get();
            else if (type == Renderer_Object_LightSpot)     shader = shader_light_spot.get();

            // Update light buffer   
            UpdateLightBuffer(entities);
           
            // Draw
            for (const auto& entity : entities)
            {
                if (Light* light = entity->GetComponent<Light>().get())
                {
                    if (RHI_Texture* shadow_map = light->GetShadowMap().get())
                    {
                        // Pack textures
                        void* textures[] =
                        {
                            m_render_targets[RenderTarget_Gbuffer_Normal]->GetResource_Texture(),
                            m_render_targets[RenderTarget_Gbuffer_Material]->GetResource_Texture(),
                            m_render_targets[RenderTarget_Gbuffer_Depth]->GetResource_Texture(),
                            m_render_targets[RenderTarget_Ssao]->GetResource_Texture(),
                            light->GetCastShadows() ? (light->GetLightType() == LightType_Directional   ? shadow_map->GetResource_Texture() : nullptr) : nullptr,
                            light->GetCastShadows() ? (light->GetLightType() == LightType_Point         ? shadow_map->GetResource_Texture() : nullptr) : nullptr,
                            light->GetCastShadows() ? (light->GetLightType() == LightType_Spot          ? shadow_map->GetResource_Texture() : nullptr) : nullptr
                        };
                    
                        m_cmd_list->SetTextures(0, textures, 7);
                        m_cmd_list->SetShaderPixel(shader);
                        m_cmd_list->DrawIndexed(Rectangle::GetIndexCount(), 0, 0);
                        m_cmd_list->Submit();
                    }
                }
            }
        };

        // Draw lights
        draw_lights(Renderer_Object_LightDirectional);
        draw_lights(Renderer_Object_LightPoint);
        draw_lights(Renderer_Object_LightSpot);

        m_cmd_list->Submit();

        // If we are doing volumetric lighting, blur it
        if (m_options & Render_VolumetricLighting)
        {
            const auto sigma = 2.0f;
            const auto pixel_stride = 2.0f;
            Pass_BlurGaussian(tex_volumetric, m_render_targets[RenderTarget_Light_Volumetric_Blurred], sigma, pixel_stride);
        }

        m_cmd_list->End();
    }

	void Renderer::Pass_Composition()
	{
        // Acquire shaders
        const auto& shader_quad         = m_shaders[Shader_Quad_V];
		const auto& shader_composition  = m_shaders[Shader_Composition_P];
		if (!shader_quad->IsCompiled() || !shader_composition->IsCompiled())
			return;

        // Acquire render target
        auto& tex_out = m_render_targets[RenderTarget_Composition_Hdr];

        // Begin command list
		m_cmd_list->Begin("Pass_Composition");

        // Update uber buffer
        m_buffer_uber_cpu.resolution = Vector2(static_cast<float>(tex_out->GetWidth()), static_cast<float>(tex_out->GetHeight()));
        UpdateUberBuffer();

        // Pack textures
		void* textures[] =
		{
            m_render_targets[RenderTarget_Gbuffer_Albedo]->GetResource_Texture(),
            m_render_targets[RenderTarget_Gbuffer_Normal]->GetResource_Texture(),
            m_render_targets[RenderTarget_Gbuffer_Depth]->GetResource_Texture(),
            m_render_targets[RenderTarget_Gbuffer_Material]->GetResource_Texture(),
            m_render_targets[RenderTarget_Light_Diffuse]->GetResource_Texture(),
            m_render_targets[RenderTarget_Light_Specular]->GetResource_Texture(),
            (m_options & Render_VolumetricLighting) ? m_render_targets[RenderTarget_Light_Volumetric_Blurred]->GetResource_Texture() : m_tex_black->GetResource_Texture(),
            m_render_targets[RenderTarget_Ssr_Blurred]->GetResource_Texture(),
            GetEnvironmentTexture_GpuResource(),
            m_render_targets[RenderTarget_Brdf_Specular_Lut]->GetResource_Texture(),
            m_render_targets[RenderTarget_Ssao]->GetResource_Texture()
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
		m_cmd_list->SetTextures(0, textures, 11);
		m_cmd_list->SetBufferIndex(m_quad.GetIndexBuffer());
		m_cmd_list->SetBufferVertex(m_quad.GetVertexBuffer());
		m_cmd_list->DrawIndexed(Rectangle::GetIndexCount(), 0, 0);
		m_cmd_list->End();
		m_cmd_list->Submit();
	}

	void Renderer::Pass_PostProcess()
	{
        // IN:  RenderTarget_Composition_Hdr
        // OUT: RenderTarget_Composition_Ldr

		// Acquire shader
		const auto& shader_quad = m_shaders[Shader_Quad_V];
		if (!shader_quad->IsCompiled())
			return;

		// All post-process passes share the following, so set them once here
		m_cmd_list->Begin("Pass_PostProcess");
		m_cmd_list->SetDepthStencilState(m_depth_stencil_disabled);
		m_cmd_list->SetRasterizerState(m_rasterizer_cull_back_solid);
		m_cmd_list->SetBlendState(m_blend_disabled);
		m_cmd_list->SetPrimitiveTopology(PrimitiveTopology_TriangleList);
		m_cmd_list->SetBufferVertex(m_quad.GetVertexBuffer());
		m_cmd_list->SetBufferIndex(m_quad.GetIndexBuffer());
		m_cmd_list->SetShaderVertex(shader_quad);
		m_cmd_list->SetInputLayout(shader_quad->GetInputLayout());

        // Acquire render targets
        auto& tex_in_hdr    = m_render_targets[RenderTarget_Composition_Hdr];
        auto& tex_out_hdr   = m_render_targets[RenderTarget_Composition_Hdr_2];
        auto& tex_in_ldr    = m_render_targets[RenderTarget_Composition_Ldr];
        auto& tex_out_ldr   = m_render_targets[RenderTarget_Composition_Ldr_2];

		// Render target swapping
		const auto swap_targets_hdr = [this, &tex_in_hdr, &tex_out_hdr]() { m_cmd_list->Submit(); tex_in_hdr.swap(tex_out_hdr); };
        const auto swap_targets_ldr = [this, &tex_in_ldr, &tex_out_ldr]() { m_cmd_list->Submit(); tex_in_ldr.swap(tex_out_ldr); };

		// TAA	
        if (GetOptionValue(Render_AntiAliasing_TAA))
        {
            Pass_TAA(tex_in_hdr, tex_out_hdr);
            swap_targets_hdr();
        }

        // Motion Blur
        if (GetOptionValue(Render_MotionBlur))
        {
            Pass_MotionBlur(tex_in_hdr, tex_out_hdr);
            swap_targets_hdr();
        }

		// Bloom
		if (GetOptionValue(Render_Bloom))
		{
			Pass_Bloom(tex_in_hdr, tex_out_hdr);
            swap_targets_hdr();
		}

		// Tone-Mapping
		if (m_option_values[Option_Value_Tonemapping] != 0)
		{
			Pass_ToneMapping(tex_in_hdr, tex_in_ldr); // HDR -> LDR
		}
        else
        {
            Pass_Copy(tex_in_hdr, tex_in_ldr);
        }

        // Dithering
        if (GetOptionValue(Render_Dithering))
        {
            Pass_Dithering(tex_in_ldr, tex_out_ldr);
            swap_targets_ldr();
        }

		// FXAA
		if (GetOptionValue(Render_AntiAliasing_FXAA))
		{
			Pass_FXAA(tex_in_ldr, tex_out_ldr);
            swap_targets_ldr();
		}

        // Sharpening - TAA controlled
        if (GetOptionValue(Render_AntiAliasing_TAA))
        {
            Pass_TaaSharpen(tex_in_ldr, tex_out_ldr);
            swap_targets_ldr();
        }

		// Sharpening - User controlled
		if (GetOptionValue(Render_Sharpening_LumaSharpen))
		{
			Pass_LumaSharpen(tex_in_ldr, tex_out_ldr);
            swap_targets_ldr();
		}

		// Chromatic aberration
		if (GetOptionValue(Render_ChromaticAberration))
		{
			Pass_ChromaticAberration(tex_in_ldr, tex_out_ldr);
            swap_targets_ldr();
		}

		// Gamma correction
		Pass_GammaCorrection(tex_in_ldr, tex_out_ldr);
        swap_targets_ldr();

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

        m_cmd_list->Begin("Pass_Upsample");

        m_buffer_uber_cpu.resolution = Vector2(static_cast<float>(tex_out->GetWidth()), static_cast<float>(tex_out->GetHeight()));
        UpdateUberBuffer();

        m_cmd_list->SetBufferVertex(m_quad.GetVertexBuffer());
        m_cmd_list->SetBufferIndex(m_quad.GetIndexBuffer());
        m_cmd_list->SetDepthStencilState(m_depth_stencil_disabled);
        m_cmd_list->SetRenderTarget(tex_out);
        m_cmd_list->SetViewport(tex_out->GetViewport());
        m_cmd_list->SetShaderVertex(shader_vertex);
        m_cmd_list->SetShaderPixel(shader_pixel);
        m_cmd_list->SetTexture(0, tex_in);
        m_cmd_list->DrawIndexed(m_quad.GetIndexCount(), 0, 0);
        m_cmd_list->End();
        m_cmd_list->Submit();
    }

    void Renderer::Pass_Downsample(shared_ptr<RHI_Texture>& tex_in, shared_ptr<RHI_Texture>& tex_out, Renderer_Shader_Type pixel_shader)
    {
        // Acquire shader
        const auto& shader_vertex   = m_shaders[Shader_Quad_V];
        const auto& shader_pixel    = m_shaders[pixel_shader];
        if (!shader_vertex->IsCompiled() || !shader_pixel->IsCompiled())
            return;

        m_cmd_list->Begin("Pass_Downsample");

        // Update uber buffer
        m_buffer_uber_cpu.resolution = Vector2(static_cast<float>(tex_out->GetWidth()), static_cast<float>(tex_out->GetHeight()));
        UpdateUberBuffer();

        m_cmd_list->SetBufferVertex(m_quad.GetVertexBuffer());
        m_cmd_list->SetBufferIndex(m_quad.GetIndexBuffer());
        m_cmd_list->SetDepthStencilState(m_depth_stencil_disabled);
        m_cmd_list->SetRenderTarget(tex_out);
        m_cmd_list->SetViewport(tex_out->GetViewport());
        m_cmd_list->SetShaderVertex(shader_vertex);
        m_cmd_list->SetShaderPixel(shader_pixel);
        m_cmd_list->SetTexture(0, tex_in);
        m_cmd_list->DrawIndexed(m_quad.GetIndexCount(), 0, 0);
        m_cmd_list->End();
        m_cmd_list->Submit();
    }

	void Renderer::Pass_BlurBox(shared_ptr<RHI_Texture>& tex_in, shared_ptr<RHI_Texture>& tex_out, const float sigma)
	{
		// Acquire shader
		const auto& shader_blurBox = m_shaders[Shader_BlurBox_P];
		if (!shader_blurBox->IsCompiled())
			return;

		m_cmd_list->Begin("Pass_BlurBox");

        // Update uber buffer
        m_buffer_uber_cpu.resolution = Vector2(static_cast<float>(tex_out->GetWidth()), static_cast<float>(tex_out->GetHeight()));
		UpdateUberBuffer();

        m_cmd_list->SetBufferVertex(m_quad.GetVertexBuffer());
        m_cmd_list->SetBufferIndex(m_quad.GetIndexBuffer());
		m_cmd_list->SetRenderTarget(tex_out);
        m_cmd_list->SetDepthStencilState(m_depth_stencil_disabled);
		m_cmd_list->SetViewport(tex_out->GetViewport());
		m_cmd_list->SetShaderPixel(shader_blurBox);
		m_cmd_list->SetTexture(0, tex_in); // Shadows are in the alpha channel
		m_cmd_list->DrawIndexed(m_quad.GetIndexCount(), 0, 0);
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

        // Acquire shaders
        const auto& shader_quad     = m_shaders[Shader_Quad_V];
        const auto& shader_gaussian = m_shaders[Shader_BlurGaussian_P];
        if (!shader_quad->IsCompiled() || !shader_gaussian->IsCompiled())
            return;

		// Start command list
		m_cmd_list->Begin("Pass_BlurGaussian");
        m_cmd_list->SetDepthStencilState(m_depth_stencil_disabled);
        m_cmd_list->SetBlendState(m_blend_disabled);
        m_cmd_list->SetViewport(tex_out->GetViewport());
        m_cmd_list->SetShaderVertex(shader_quad);
        m_cmd_list->SetInputLayout(shader_quad->GetInputLayout());
        m_cmd_list->SetShaderPixel(shader_gaussian);
   
		// Horizontal Gaussian blur	
		{
            // Update uber buffer
            m_buffer_uber_cpu.resolution        = Vector2(static_cast<float>(tex_in->GetWidth()), static_cast<float>(tex_in->GetHeight()));
            m_buffer_uber_cpu.blur_direction    = Vector2(pixel_stride, 0.0f);
            m_buffer_uber_cpu.blur_sigma        = sigma;
            UpdateUberBuffer();

			m_cmd_list->ClearTextures(); // avoids d3d11 warning where the render target is already bound as an input texture (from previous pass)
			m_cmd_list->SetRenderTarget(tex_out);
			m_cmd_list->SetTexture(0, tex_in);
			m_cmd_list->DrawIndexed(Rectangle::GetIndexCount(), 0, 0);
            m_cmd_list->Submit();
		}

		// Vertical Gaussian blur
		{
            m_buffer_uber_cpu.blur_direction    = Vector2(0.0f, pixel_stride);
            m_buffer_uber_cpu.blur_sigma        = sigma;
            UpdateUberBuffer();

			m_cmd_list->ClearTextures(); // avoids d3d11 warning where the render target is already bound as an input texture (from previous pass)
			m_cmd_list->SetRenderTarget(tex_in);
			m_cmd_list->SetTexture(0, tex_out);
			m_cmd_list->DrawIndexed(Rectangle::GetIndexCount(), 0, 0);
            m_cmd_list->Submit();
		}

		m_cmd_list->End();
		
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
		const auto& shader_quad                 = m_shaders[Shader_Quad_V];
        const auto& shader_gaussianBilateral    = m_shaders[Shader_BlurGaussianBilateral_P];
		if (!shader_quad->IsCompiled() || !shader_gaussianBilateral->IsCompiled())
			return;

        // Acquire render targets
        auto& tex_depth     = m_render_targets[RenderTarget_Gbuffer_Depth];
        auto& tex_normal    = m_render_targets[RenderTarget_Gbuffer_Normal];

		// Start command list
        m_cmd_list->Begin("Pass_BlurBilateralGaussian");
        m_cmd_list->SetBufferVertex(m_quad.GetVertexBuffer());
        m_cmd_list->SetBufferIndex(m_quad.GetIndexBuffer());
        m_cmd_list->SetDepthStencilState(m_depth_stencil_disabled);
        m_cmd_list->SetBlendState(m_blend_disabled);
		m_cmd_list->SetViewport(tex_out->GetViewport());	
		m_cmd_list->SetShaderVertex(shader_quad);
		m_cmd_list->SetInputLayout(shader_quad->GetInputLayout());
		m_cmd_list->SetShaderPixel(shader_gaussianBilateral);	

		// Horizontal Gaussian blur
		{
            // Update uber buffer
            m_buffer_uber_cpu.resolution        = Vector2(static_cast<float>(tex_in->GetWidth()), static_cast<float>(tex_in->GetHeight()));
            m_buffer_uber_cpu.blur_direction    = Vector2(pixel_stride, 0.0f);
            m_buffer_uber_cpu.blur_sigma        = sigma;
            UpdateUberBuffer();

            // Pack textures
			void* textures[] = { tex_in->GetResource_Texture(), tex_depth->GetResource_Texture(), tex_normal->GetResource_Texture() };
			
			m_cmd_list->ClearTextures(); // avoids d3d11 warning where render target is also bound as texture (from Pass_PreLight)
			m_cmd_list->SetRenderTarget(tex_out);
			m_cmd_list->SetTextures(0, textures, 3);
			m_cmd_list->DrawIndexed(m_quad.GetIndexCount(), 0, 0);
            m_cmd_list->Submit();
		}

		// Vertical Gaussian blur
		{
            // Update uber
            m_buffer_uber_cpu.blur_direction    = Vector2(0.0f, pixel_stride);
            m_buffer_uber_cpu.blur_sigma        = sigma;
            UpdateUberBuffer();

            // Pack textures
			void* textures[] = { tex_out->GetResource_Texture(), tex_depth->GetResource_Texture(), tex_normal->GetResource_Texture() };

			m_cmd_list->ClearTextures(); // avoids d3d11 warning where render target is also bound as texture (from above pass)
			m_cmd_list->SetRenderTarget(tex_in);
			m_cmd_list->SetTextures(0, textures, 3);
			m_cmd_list->DrawIndexed(m_quad.GetIndexCount(), 0, 0);
            m_cmd_list->Submit();
		}

		m_cmd_list->End();	
		tex_in.swap(tex_out);
	}

	void Renderer::Pass_TAA(shared_ptr<RHI_Texture>& tex_in, shared_ptr<RHI_Texture>& tex_out)
	{
		// Acquire shaders
		const auto& shader_taa      = m_shaders[Shader_Taa_P];
		const auto& shader_texture  = m_shaders[Shader_Texture_P];
		if (!shader_taa->IsCompiled() || !shader_texture->IsCompiled())
			return;

        // Acquire render targets
        auto& tex_history   = m_render_targets[RenderTarget_Composition_Hdr_History];
        auto& tex_history_2 = m_render_targets[RenderTarget_Composition_Hdr_History_2];

		m_cmd_list->Begin("Pass_TAA");
        m_cmd_list->SetDepthStencilState(m_depth_stencil_disabled);

		// Resolve and accumulate to history texture
		{
			// Pack textures
			void* textures[] =
            {
                tex_history->GetResource_Texture(),
                tex_in->GetResource_Texture(),
                m_render_targets[RenderTarget_Gbuffer_Velocity]->GetResource_Texture(),
                m_render_targets[RenderTarget_Gbuffer_Depth]->GetResource_Texture()
            };

            // Update uber buffer
            m_buffer_uber_cpu.resolution = Vector2(static_cast<float>(tex_out->GetWidth()), static_cast<float>(tex_out->GetHeight()));
            UpdateUberBuffer();

			m_cmd_list->ClearTextures(); // avoids d3d11 warning where the render target is already bound as an input texture (from some previous pass)
			m_cmd_list->SetRenderTarget(tex_history_2);
			m_cmd_list->SetViewport(tex_out->GetViewport());
			m_cmd_list->SetShaderPixel(shader_taa);
			m_cmd_list->SetTextures(0, textures, 4);
			m_cmd_list->DrawIndexed(Rectangle::GetIndexCount(), 0, 0);
		}

		// Copy
        Pass_Copy(tex_history_2, tex_out);

		m_cmd_list->End();
		m_cmd_list->Submit();

		// Swap history texture so the above works again in the next frame
        tex_history.swap(tex_history_2);
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
        m_cmd_list->SetDepthStencilState(m_depth_stencil_disabled);
        m_cmd_list->SetBlendState(m_blend_disabled);

        m_cmd_list->Begin("Downsample_And_Luminance");
        {
            // Update uber buffer
            m_buffer_uber_cpu.resolution = Vector2(static_cast<float>(m_render_tex_bloom[0]->GetWidth()), static_cast<float>(m_render_tex_bloom[0]->GetHeight()));
            UpdateUberBuffer();

            m_cmd_list->SetRenderTarget(m_render_tex_bloom[0]);
            m_cmd_list->SetViewport(m_render_tex_bloom[0]->GetViewport());
            m_cmd_list->SetShaderPixel(shader_bloomBright);
            m_cmd_list->SetTexture(0, tex_in);
            m_cmd_list->DrawIndexed(Rectangle::GetIndexCount(), 0, 0);
        }
        m_cmd_list->End();

        // Downsample
        // The last bloom texture is the same size as the previous one (it's used for the Gaussian pass below), so we skip it
        for (int i = 0; i < static_cast<int>(m_render_tex_bloom.size() - 1); i++)
        {
            Pass_Downsample(m_render_tex_bloom[i], m_render_tex_bloom[i + 1], Shader_BloomDownsample_P);
        }

        auto upsample = [this, &shader_upsample](shared_ptr<RHI_Texture>& tex_in, shared_ptr<RHI_Texture>& tex_out)
        {
            m_cmd_list->Begin("Upsample");
            {
                // Update uber buffer
                m_buffer_uber_cpu.resolution = Vector2(static_cast<float>(tex_out->GetWidth()), static_cast<float>(tex_out->GetHeight()));
                UpdateUberBuffer();

                m_cmd_list->SetBlendState(m_blend_bloom); // blend with previous
                m_cmd_list->SetRenderTarget(tex_out);
                m_cmd_list->SetViewport(tex_out->GetViewport());
                m_cmd_list->SetShaderPixel(shader_upsample);
                m_cmd_list->SetTexture(0, tex_in);
                m_cmd_list->DrawIndexed(Rectangle::GetIndexCount(), 0, 0);
            }
            m_cmd_list->End();
            m_cmd_list->Submit(); // we have to submit because all upsample passes are using the same buffer
        };

		// Upsample + blend
        m_cmd_list->ClearTextures(); // avoids d3d11 warning where the render target is already bound as an input texture (from some previous pass)
        for (int i = static_cast<int>(m_render_tex_bloom.size() - 1); i > 0; i--)
        {
            upsample(m_render_tex_bloom[i], m_render_tex_bloom[i - 1]);
        }
		
		m_cmd_list->Begin("Additive_Blending");
		{
			// Pack textures
			void* textures[] = { tex_in->GetResource_Texture(), m_render_tex_bloom.front()->GetResource_Texture() };

            // Update uber buffer
            m_buffer_uber_cpu.resolution = Vector2(static_cast<float>(tex_out->GetWidth()), static_cast<float>(tex_out->GetHeight()));
            UpdateUberBuffer();

            m_cmd_list->SetBlendState(m_blend_disabled);
			m_cmd_list->SetRenderTarget(tex_out);
			m_cmd_list->SetViewport(tex_out->GetViewport());
			m_cmd_list->SetShaderPixel(shader_bloomBlend);
			m_cmd_list->SetTextures(0, textures, 2);
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

        // Update uber buffer
        m_buffer_uber_cpu.resolution = Vector2(static_cast<float>(tex_out->GetWidth()), static_cast<float>(tex_out->GetHeight()));
        UpdateUberBuffer();

		m_cmd_list->ClearTextures(); // avoids d3d11 warning where the render target is already bound as an input texture (from previous pass)
        m_cmd_list->SetDepthStencilState(m_depth_stencil_disabled);
		m_cmd_list->SetRenderTarget(tex_out);
		m_cmd_list->SetViewport(tex_out->GetViewport());
		m_cmd_list->SetShaderPixel(shader_toneMapping);
		m_cmd_list->SetTexture(0, tex_in);
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

        // Update uber buffer
        m_buffer_uber_cpu.resolution = Vector2(static_cast<float>(tex_out->GetWidth()), static_cast<float>(tex_out->GetHeight()));
        UpdateUberBuffer();

		m_cmd_list->ClearTextures(); // avoids d3d11 warning where the render target is already bound as an input texture (from previous pass)
        m_cmd_list->SetDepthStencilState(m_depth_stencil_disabled);
		m_cmd_list->SetRenderTarget(tex_out);
		m_cmd_list->SetViewport(tex_out->GetViewport());
		m_cmd_list->SetShaderPixel(shader_gammaCorrection);
		m_cmd_list->SetTexture(0, tex_in);
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

        // Update uber buffer
        m_buffer_uber_cpu.resolution = Vector2(static_cast<float>(tex_out->GetWidth()), static_cast<float>(tex_out->GetHeight()));
        UpdateUberBuffer();

		m_cmd_list->ClearTextures(); // avoids d3d11 warning where the render target is already bound as an input texture (from previous pass)
        m_cmd_list->SetDepthStencilState(m_depth_stencil_disabled);
		m_cmd_list->SetRenderTarget(tex_out);
		m_cmd_list->SetViewport(tex_out->GetViewport());

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

        // Update uber buffer
        m_buffer_uber_cpu.resolution = Vector2(static_cast<float>(tex_out->GetWidth()), static_cast<float>(tex_out->GetHeight()));
        UpdateUberBuffer();

		m_cmd_list->ClearTextures(); // avoids d3d11 warning where the render target is already bound as an input texture (from previous pass)
        m_cmd_list->SetDepthStencilState(m_depth_stencil_disabled);
		m_cmd_list->SetRenderTarget(tex_out);
		m_cmd_list->SetViewport(tex_out->GetViewport());
		m_cmd_list->SetShaderPixel(shader_chromaticAberration);
		m_cmd_list->SetTexture(0, tex_in);
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
		void* textures[] =
        {
            tex_in->GetResource_Texture(),
            m_render_targets[RenderTarget_Gbuffer_Velocity]->GetResource_Texture(),
            m_render_targets[RenderTarget_Gbuffer_Depth]->GetResource_Texture()
        };

        // Update uber buffer
        m_buffer_uber_cpu.resolution = Vector2(static_cast<float>(tex_out->GetWidth()), static_cast<float>(tex_out->GetHeight()));
        UpdateUberBuffer();

		m_cmd_list->ClearTextures(); // avoids d3d11 warning where the render target is already bound as an input texture (from previous pass)
        m_cmd_list->SetDepthStencilState(m_depth_stencil_disabled);
		m_cmd_list->SetRenderTarget(tex_out);
		m_cmd_list->SetViewport(tex_out->GetViewport());
		m_cmd_list->SetShaderPixel(shader_motionBlur);
		m_cmd_list->SetTextures(0, textures, 2);
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

        // Update uber buffer
        m_buffer_uber_cpu.resolution = Vector2(static_cast<float>(tex_out->GetWidth()), static_cast<float>(tex_out->GetHeight()));
        UpdateUberBuffer();

		m_cmd_list->ClearTextures(); // avoids d3d11 warning where the render target is already bound as an input texture (from previous pass)
        m_cmd_list->SetDepthStencilState(m_depth_stencil_disabled);
		m_cmd_list->SetRenderTarget(tex_out);
		m_cmd_list->SetViewport(tex_out->GetViewport());
		m_cmd_list->SetShaderPixel(shader_dithering);
		m_cmd_list->SetTexture(0, tex_in);
		m_cmd_list->DrawIndexed(Rectangle::GetIndexCount(), 0, 0);
		m_cmd_list->End();
		m_cmd_list->Submit();
	}

    void Renderer::Pass_TaaSharpen(std::shared_ptr<RHI_Texture>& tex_in, std::shared_ptr<RHI_Texture>& tex_out)
    {
        // Acquire shader
        const auto& shader = m_shaders[Shader_Sharpen_Taa_P];
        if (!shader->IsCompiled())
            return;

        m_cmd_list->Begin("Pass_TaaSharpen");

        // Update uber buffer
        m_buffer_uber_cpu.resolution = Vector2(static_cast<float>(tex_out->GetWidth()), static_cast<float>(tex_out->GetHeight()));
        UpdateUberBuffer();

        m_cmd_list->ClearTextures(); // avoids d3d11 warning where the render target is already bound as an input texture (from previous pass)
        m_cmd_list->SetDepthStencilState(m_depth_stencil_disabled);
        m_cmd_list->SetRenderTarget(tex_out);
        m_cmd_list->SetViewport(tex_out->GetViewport());
        m_cmd_list->SetShaderPixel(shader);
        m_cmd_list->SetTexture(0, tex_in);
        m_cmd_list->DrawIndexed(Rectangle::GetIndexCount(), 0, 0);
        m_cmd_list->End();
        m_cmd_list->Submit();
    }

	void Renderer::Pass_LumaSharpen(shared_ptr<RHI_Texture>& tex_in, shared_ptr<RHI_Texture>& tex_out)
	{
		// Acquire shader
		const auto& shader = m_shaders[Shader_Sharpen_Luma_P];
		if (!shader->IsCompiled())
			return;

		m_cmd_list->Begin("Pass_LumaSharpen");

        // Update uber buffer
        m_buffer_uber_cpu.resolution = Vector2(static_cast<float>(tex_out->GetWidth()), static_cast<float>(tex_out->GetHeight()));
        UpdateUberBuffer();
	
		m_cmd_list->ClearTextures(); // avoids d3d11 warning where the render target is already bound as an input texture (from previous pass)
        m_cmd_list->SetDepthStencilState(m_depth_stencil_disabled);
		m_cmd_list->SetRenderTarget(tex_out);
		m_cmd_list->SetViewport(tex_out->GetViewport());		
		m_cmd_list->SetShaderPixel(shader);
		m_cmd_list->SetTexture(0, tex_in);
		m_cmd_list->DrawIndexed(Rectangle::GetIndexCount(), 0, 0);
		m_cmd_list->End();
		m_cmd_list->Submit();
	}

	void Renderer::Pass_Lines(shared_ptr<RHI_Texture>& tex_out)
	{
		const bool draw_picking_ray = m_options & Render_Debug_PickingRay;
		const bool draw_aabb		= m_options & Render_Debug_AABB;
		const bool draw_grid		= m_options & Render_Debug_Grid;
        const bool draw_lights      = m_options & Render_Debug_Lights;
		const auto draw_lines		= !m_lines_list_depth_enabled.empty() || !m_lines_list_depth_disabled.empty(); // Any kind of lines, physics, user debug, etc.
		const auto draw				= draw_picking_ray || draw_aabb || draw_grid || draw_lines || draw_lights;
		if (!draw)
			return;

        // Acquire color shaders
        const auto& shader_color_v = m_shaders[Shader_Color_V];
        const auto& shader_color_p = m_shaders[Shader_Color_P];
        if (!shader_color_v->IsCompiled() || !shader_color_p->IsCompiled())
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

            // Lights
            if (draw_lights)
            {
                auto& lights = m_entities[Renderer_Object_Light];
                for (const auto& entity : lights)
                {
                    shared_ptr<Light>& light = entity->GetComponent<Light>();

                    if (light->GetLightType() == LightType_Spot)
                    {
                        Vector3 start   = light->GetTransform()->GetPosition();
                        Vector3 end     = light->GetTransform()->GetForward() * light->GetRange();
                        DrawLine(start, start + end, Vector4(0, 1, 0, 1));
                    }
                }
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

		// Begin command list
		m_cmd_list->SetViewport(tex_out->GetViewport());
		m_cmd_list->SetRasterizerState(m_rasterizer_cull_back_wireframe);
		m_cmd_list->SetBlendState(m_blend_disabled);
		m_cmd_list->SetPrimitiveTopology(PrimitiveTopology_LineList);
		m_cmd_list->SetShaderVertex(shader_color_v);
		m_cmd_list->SetShaderPixel(shader_color_p);
		m_cmd_list->SetInputLayout(shader_color_v->GetInputLayout());

		// Draw lines that require depth
		m_cmd_list->SetDepthStencilState(m_depth_stencil_enabled_no_write);
		m_cmd_list->SetRenderTarget(tex_out, m_render_targets[RenderTarget_Gbuffer_Depth]->GetResource_DepthStencil());
		{
			// Grid
			if (draw_grid)
			{
                // Update uber buffer
                m_buffer_uber_cpu.resolution    = m_resolution;
                m_buffer_uber_cpu.transform     = m_gizmo_grid->ComputeWorldMatrix(m_camera->GetTransform()) * m_buffer_frame_cpu.view_projection_unjittered;
                UpdateUberBuffer();

				m_cmd_list->SetBufferIndex(m_gizmo_grid->GetIndexBuffer());
				m_cmd_list->SetBufferVertex(m_gizmo_grid->GetVertexBuffer());
				m_cmd_list->SetBlendState(m_blend_enabled);
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

				m_cmd_list->SetBufferVertex(m_vertex_buffer_lines);
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

				m_cmd_list->SetBufferVertex(m_vertex_buffer_lines);
				m_cmd_list->Draw(line_vertex_buffer_size);

				m_lines_list_depth_disabled.clear();
			}
		}

		m_cmd_list->End();
		m_cmd_list->Submit();
	}

	void Renderer::Pass_Gizmos(shared_ptr<RHI_Texture>& tex_out)
	{
		bool render_lights		= m_options & Render_Debug_Lights;
		bool render_transform	= m_options & Render_Debug_Transform;
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
                shared_ptr<Light>& light = entity->GetComponent<Light>();
                // Light can be null if it just got removed and our buffer doesn't update till the next frame
                if (!light)
                    break;

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
				auto scale					= m_gizmo_size_max / distance;
				scale						= Clamp(scale, m_gizmo_size_min, m_gizmo_size_max);

				// Choose texture based on light type
				shared_ptr<RHI_Texture> light_tex = nullptr;
				auto type = light->GetLightType();
				if (type == LightType_Directional)	light_tex = m_gizmo_tex_light_directional;
				else if (type == LightType_Point)	light_tex = m_gizmo_tex_light_point;
				else if (type == LightType_Spot)	light_tex = m_gizmo_tex_light_spot;

				// Construct appropriate rectangle
				auto tex_width  = light_tex->GetWidth() * scale;
				auto tex_height = light_tex->GetHeight() * scale;
				auto rectangle  = Math::Rectangle(position_light_screen.x - tex_width * 0.5f, position_light_screen.y - tex_height * 0.5f, tex_width, tex_height);
				if (rectangle != m_gizmo_light_rect)
				{
					m_gizmo_light_rect = rectangle;
					m_gizmo_light_rect.CreateBuffers(this);
				}

                // Update uber buffer
                m_buffer_uber_cpu.resolution    = Vector2(static_cast<float>(tex_width), static_cast<float>(tex_width));
                m_buffer_uber_cpu.transform     = m_buffer_frame_cpu.view_projection_ortho;
                UpdateUberBuffer();

				m_cmd_list->SetShaderVertex(shader_quad);
				m_cmd_list->SetInputLayout(shader_quad->GetInputLayout());
				m_cmd_list->SetShaderPixel(m_shaders[Shader_Texture_P]);
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
			auto const& shader_gizmo_transform_v = m_shaders[Shader_GizmoTransform_V];
            auto const& shader_gizmo_transform_p = m_shaders[Shader_GizmoTransform_P];
            if (shader_gizmo_transform_v->IsCompiled() && shader_gizmo_transform_p->IsCompiled())
            { 
			    m_cmd_list->SetShaderVertex(shader_gizmo_transform_v);
			    m_cmd_list->SetShaderPixel(shader_gizmo_transform_p);
			    m_cmd_list->SetInputLayout(shader_gizmo_transform_v->GetInputLayout());
			    m_cmd_list->SetBufferIndex(m_gizmo_transform->GetIndexBuffer());
			    m_cmd_list->SetBufferVertex(m_gizmo_transform->GetVertexBuffer());

			    // Axis - X
                m_buffer_uber_cpu.transform         = m_gizmo_transform->GetHandle().GetTransform(Vector3::Right);
                m_buffer_uber_cpu.transform_axis    = m_gizmo_transform->GetHandle().GetColor(Vector3::Right);
                UpdateUberBuffer();
			    m_cmd_list->DrawIndexed(m_gizmo_transform->GetIndexCount(), 0, 0);
                m_cmd_list->Submit();

			    // Axis - Y
                m_buffer_uber_cpu.transform         = m_gizmo_transform->GetHandle().GetTransform(Vector3::Up);
                m_buffer_uber_cpu.transform_axis    = m_gizmo_transform->GetHandle().GetColor(Vector3::Up);
                UpdateUberBuffer();
			    m_cmd_list->DrawIndexed(m_gizmo_transform->GetIndexCount(), 0, 0);
                m_cmd_list->Submit();

			    // Axis - Z
                m_buffer_uber_cpu.transform         = m_gizmo_transform->GetHandle().GetTransform(Vector3::Forward);
                m_buffer_uber_cpu.transform_axis    = m_gizmo_transform->GetHandle().GetColor(Vector3::Forward);
                UpdateUberBuffer();
			    m_cmd_list->DrawIndexed(m_gizmo_transform->GetIndexCount(), 0, 0);
                m_cmd_list->Submit();

			    // Axes - XYZ
			    if (m_gizmo_transform->DrawXYZ())
			    {
                    m_buffer_uber_cpu.transform         = m_gizmo_transform->GetHandle().GetTransform(Vector3::One);
                    m_buffer_uber_cpu.transform_axis    = m_gizmo_transform->GetHandle().GetColor(Vector3::One);
                    UpdateUberBuffer();
			    	m_cmd_list->DrawIndexed(m_gizmo_transform->GetIndexCount(), 0, 0);
			    }
            }
		}

		m_cmd_list->End();
		m_cmd_list->Submit();
	}

	void Renderer::Pass_PerformanceMetrics(shared_ptr<RHI_Texture>& tex_out)
	{
        // Early exit cases
        const bool draw             = m_options & Render_Debug_PerformanceMetrics;
        const bool empty            = m_profiler->GetMetrics().empty();
        const auto& shader_font_v   = m_shaders[Shader_Font_V];
        const auto& shader_font_p   = m_shaders[Shader_Font_P];
        if (!draw || empty || !shader_font_v->IsCompiled() || !shader_font_p->IsCompiled())
            return;

		m_cmd_list->Begin("Pass_PerformanceMetrics");

		// Update text
		const auto text_pos = Vector2(-static_cast<int>(m_viewport.width) * 0.5f + 1.0f, static_cast<int>(m_viewport.height) * 0.5f);
		m_font->SetText(m_profiler->GetMetrics(), text_pos);

        // Update uber buffer
        m_buffer_uber_cpu.resolution    = Vector2(static_cast<float>(tex_out->GetWidth()), static_cast<float>(tex_out->GetHeight()));
        m_buffer_uber_cpu.color         = m_font->GetColor();
        UpdateUberBuffer();

		m_cmd_list->SetDepthStencilState(m_depth_stencil_disabled);
		m_cmd_list->SetRasterizerState(m_rasterizer_cull_back_solid);
		m_cmd_list->SetPrimitiveTopology(PrimitiveTopology_TriangleList);
		m_cmd_list->SetRenderTarget(tex_out);	
		m_cmd_list->SetViewport(tex_out->GetViewport());
		m_cmd_list->SetBlendState(m_blend_enabled);	
		m_cmd_list->SetTexture(0, m_font->GetAtlas());
		m_cmd_list->SetShaderVertex(shader_font_v);
		m_cmd_list->SetShaderPixel(shader_font_p);
		m_cmd_list->SetInputLayout(shader_font_v->GetInputLayout());
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
        Renderer_Shader_Type shader_type;
		if (m_debug_buffer == Renderer_Buffer_Albedo)
		{
			texture     = m_render_targets[RenderTarget_Gbuffer_Albedo];
			shader_type = Shader_Texture_P;
		}

		if (m_debug_buffer == Renderer_Buffer_Normal)
		{
			texture     = m_render_targets[RenderTarget_Gbuffer_Normal];
			shader_type = Shader_DebugNormal_P;
		}

		if (m_debug_buffer == Renderer_Buffer_Material)
		{
			texture     = m_render_targets[RenderTarget_Gbuffer_Material];
			shader_type = Shader_Texture_P;
		}

        if (m_debug_buffer == Renderer_Buffer_Diffuse)
        {
            texture     = m_render_targets[RenderTarget_Light_Diffuse];
            shader_type = Shader_DebugChannelRgbGammaCorrect_P;
        }

        if (m_debug_buffer == Renderer_Buffer_Specular)
        {
            texture     = m_render_targets[RenderTarget_Light_Specular];
            shader_type = Shader_DebugChannelRgbGammaCorrect_P;
        }

		if (m_debug_buffer == Renderer_Buffer_Velocity)
		{
			texture     = m_render_targets[RenderTarget_Gbuffer_Velocity];
			shader_type = Shader_DebugVelocity_P;
		}

		if (m_debug_buffer == Renderer_Buffer_Depth)
		{
			texture     = m_render_targets[RenderTarget_Gbuffer_Depth];
			shader_type = Shader_DebugChannelR_P;
		}

		if (m_debug_buffer == Renderer_Buffer_SSAO)
		{
			texture     = m_options & Render_SSAO ? m_render_targets[RenderTarget_Ssao] : m_tex_white;
			shader_type = Shader_DebugChannelR_P;
		}

        if (m_debug_buffer == Renderer_Buffer_SSR)
        {
            texture     = m_render_targets[RenderTarget_Ssr_Blurred];
            shader_type = Shader_DebugChannelRgbGammaCorrect_P;
        }

        if (m_debug_buffer == Renderer_Buffer_Bloom)
        {
            texture     = m_render_tex_bloom.front();
            shader_type = Shader_DebugChannelRgbGammaCorrect_P;
        }

        if (m_debug_buffer == Renderer_Buffer_VolumetricLighting)
        {
            texture     = m_render_targets[RenderTarget_Light_Volumetric_Blurred];
            shader_type = Shader_DebugChannelRgbGammaCorrect_P;
        }

        if (m_debug_buffer == Renderer_Buffer_Shadows)
        {
            texture     = m_render_targets[RenderTarget_Light_Diffuse];
            shader_type = Shader_DebugChannelA_P;
        }

        // Acquire shaders
        const auto& shader_quad     = m_shaders[Shader_Quad_V];
        const auto& shader_pixel    = m_shaders[shader_type];
        if (!shader_quad->IsCompiled() || !shader_pixel->IsCompiled())
            return false;

        // Draw
        m_cmd_list->Begin("Pass_DebugBuffer");

        // Update uber buffer
        m_buffer_uber_cpu.resolution    = Vector2(static_cast<float>(tex_out->GetWidth()), static_cast<float>(tex_out->GetHeight()));
        m_buffer_uber_cpu.transform     = m_buffer_frame_cpu.view_projection_ortho;
        UpdateUberBuffer();

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

        // Acquire render target
        const auto& texture = m_render_targets[RenderTarget_Brdf_Specular_Lut];

        m_cmd_list->Begin("Pass_BrdfSpecularLut");

        // Update uber buffer
        m_buffer_uber_cpu.resolution = Vector2(static_cast<float>(texture->GetWidth()), static_cast<float>(texture->GetHeight()));
        UpdateUberBuffer();

        m_cmd_list->SetDepthStencilState(m_depth_stencil_disabled);
        m_cmd_list->SetRasterizerState(m_rasterizer_cull_back_solid);
        m_cmd_list->SetBlendState(m_blend_disabled);
        m_cmd_list->SetPrimitiveTopology(PrimitiveTopology_TriangleList);
        m_cmd_list->SetBufferVertex(m_quad.GetVertexBuffer());
        m_cmd_list->SetBufferIndex(m_quad.GetIndexBuffer());
        m_cmd_list->SetRenderTarget(texture);
        m_cmd_list->SetViewport(texture->GetViewport());
        m_cmd_list->SetShaderVertex(shader_quad);
        m_cmd_list->SetInputLayout(shader_quad->GetInputLayout());
        m_cmd_list->SetShaderPixel(shader_brdf_specular_lut);
        m_cmd_list->DrawIndexed(Rectangle::GetIndexCount(), 0, 0);
        m_cmd_list->End();
        m_cmd_list->Submit();

        m_brdf_specular_lut_rendered = true;
    }

    void Renderer::Pass_Copy(shared_ptr<RHI_Texture>& tex_in, shared_ptr<RHI_Texture>& tex_out)
    {
        // Acquire shaders
        const auto& shader_quad     = m_shaders[Shader_Quad_V];
        const auto& shader_pixel    = m_shaders[Shader_Texture_P];
        if (!shader_quad->IsCompiled() || !shader_pixel->IsCompiled())
            return;

        // Draw
        m_cmd_list->Begin("Pass_Copy");

        // Update uber buffer
        m_buffer_uber_cpu.resolution    = Vector2(static_cast<float>(tex_out->GetWidth()), static_cast<float>(tex_out->GetHeight()));
        m_buffer_uber_cpu.transform     = m_buffer_frame_cpu.view_projection_ortho;
        UpdateUberBuffer();

        m_cmd_list->SetDepthStencilState(m_depth_stencil_disabled);
        m_cmd_list->SetRasterizerState(m_rasterizer_cull_back_solid);
        m_cmd_list->SetBlendState(m_blend_disabled);
        m_cmd_list->SetPrimitiveTopology(PrimitiveTopology_TriangleList);
        m_cmd_list->SetRenderTarget(tex_out);
        m_cmd_list->SetViewport(tex_out->GetViewport());
        m_cmd_list->SetShaderVertex(shader_quad);
        m_cmd_list->SetInputLayout(shader_quad->GetInputLayout());
        m_cmd_list->SetShaderPixel(shader_pixel);
        m_cmd_list->SetTexture(0, tex_in);
        m_cmd_list->SetBufferVertex(m_quad.GetVertexBuffer());
        m_cmd_list->SetBufferIndex(m_quad.GetIndexBuffer());
        m_cmd_list->DrawIndexed(Rectangle::GetIndexCount(), 0, 0);
        m_cmd_list->End();
        m_cmd_list->Submit();
    }
}
