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

//= INCLUDES ===============================
#include "Renderer.h"
#include "Model.h"
#include "Font/Font.h"
#include "../Profiling/Profiler.h"
#include "ShaderVariation.h"
#include "Gizmos/Grid.h"
#include "Gizmos/Transform_Gizmo.h"
#include "../RHI/RHI_CommandList.h"
#include "../RHI/RHI_VertexBuffer.h"
#include "../RHI/RHI_PipelineState.h"
#include "../RHI/RHI_ConstantBuffer.h"
#include "../World/Entity.h"
#include "../World/Components/Light.h"
#include "../World/Components/Camera.h"
#include "../World/Components/Transform.h"
#include "../World/Components/Renderable.h"
#include "../Resource/ResourceCache.h"
//==========================================

//= NAMESPACES ===============
using namespace std;
using namespace Spartan::Math;
//============================

namespace Spartan
{
    void Renderer::SetGlobalSamplersAndConstantBuffers(RHI_CommandList* cmd_list)
    {
        // Set the buffers we will be using thought the frame
        cmd_list->SetConstantBuffer(0, RHI_Buffer_VertexShader | RHI_Buffer_PixelShader, m_buffer_frame_gpu);
        cmd_list->SetConstantBuffer(1, RHI_Buffer_VertexShader | RHI_Buffer_PixelShader, m_buffer_uber_gpu);
        cmd_list->SetConstantBuffer(2, RHI_Buffer_VertexShader, m_buffer_object_gpu);
        cmd_list->SetConstantBuffer(3, RHI_Buffer_PixelShader, m_buffer_light_gpu);
        
        // Set the samplers we will be using thought the frame
        cmd_list->SetSampler(0, m_sampler_compare_depth);
        cmd_list->SetSampler(1, m_sampler_point_clamp);
        cmd_list->SetSampler(2, m_sampler_bilinear_clamp);
        cmd_list->SetSampler(3, m_sampler_bilinear_wrap);
        cmd_list->SetSampler(4, m_sampler_trilinear_clamp);
        cmd_list->SetSampler(5, m_sampler_anisotropic_wrap);
    }

    void Renderer::Pass_Main(RHI_CommandList* cmd_list)
	{
        // Validate RHI device as it's required almost everywhere
        if (!m_rhi_device)
            return;

        SCOPED_TIME_BLOCK(m_profiler);

        // Updates onces, used almost everywhere
        UpdateFrameBuffer();

        // All renderer passes (some contain sub-passes)
        Pass_BrdfSpecularLut(cmd_list);
        Pass_LightDepth(cmd_list);
        if (GetOptionValue(Render_DepthPrepass))
        {
            Pass_DepthPrePass(cmd_list);
        }
        Pass_GBuffer(cmd_list);
        Pass_Ssao(cmd_list);
        Pass_Ssr(cmd_list);
        Pass_Light(cmd_list);
        Pass_Composition(cmd_list);
        Pass_PostProcess(cmd_list);
        Pass_Lines(cmd_list, m_render_targets[RenderTarget_Composition_Ldr]);
        Pass_Gizmos(cmd_list, m_render_targets[RenderTarget_Composition_Ldr]);
        Pass_DebugBuffer(cmd_list, m_render_targets[RenderTarget_Composition_Ldr]);
        Pass_PerformanceMetrics(cmd_list, m_render_targets[RenderTarget_Composition_Ldr]);
	}

	void Renderer::Pass_LightDepth(RHI_CommandList* cmd_list)
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

        for (uint32_t light_index = 0; light_index < entities_light.size(); light_index++)
        {
            const Light* light = entities_light[light_index]->GetComponent<Light>().get();

            // Light can be null if it just got removed and our buffer doesn't update till the next frame
            if (!light)
                break;

            // Skip if it doesn't need to cast shadows
            if (!light->GetCastShadows())
                break;

            // Acquire light's shadow map
            const auto& shadow_map = light->GetShadowMap();
            if (!shadow_map)
                continue;

            // Set render state
            static RHI_PipelineState pipeline_state;
            pipeline_state.shader_vertex                = shader_depth.get();
            pipeline_state.shader_pixel                 = nullptr;
            pipeline_state.blend_state                  = m_blend_disabled.get();
            pipeline_state.depth_stencil_state          = m_depth_stencil_enabled_write.get();
            pipeline_state.render_target_depth_texture  = shadow_map.get();
            pipeline_state.viewport                     = shadow_map->GetViewport();
            pipeline_state.primitive_topology           = RHI_PrimitiveTopology_TriangleList;
            pipeline_state.pass_name                    = "Pass_LightDepth";

            for (uint32_t i = 0; i < shadow_map->GetArraySize(); i++)
            {
                // Clear every shadow map
                pipeline_state.render_target_depth_clear = GetClearDepth();

                const Matrix& view_projection = light->GetViewMatrix(i) * light->GetProjectionMatrix(i);

                // Set appropriate array index
                pipeline_state.render_target_depth_array_index = i;

                // Set appropriate rasterizer state
                if (light->GetLightType() == LightType_Directional)
                {
                    // "Pancaking" - https://www.gamedev.net/forums/topic/639036-shadow-mapping-and-high-up-objects/
                    // It's basically a way to capture the silhouettes of potential shadow casters behind the light's view point.
                    // Of course we also have to make sure that the light doesn't cull them in the first place (this is done automatically by the light)
                    pipeline_state.rasterizer_state = m_rasterizer_cull_back_solid_no_clip.get();
                }
                else
                {
                    pipeline_state.rasterizer_state = m_rasterizer_cull_back_solid.get();
                }

                if (cmd_list->Begin(pipeline_state))
                {
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
                        cmd_list->SetBufferIndex(model->GetIndexBuffer());
                        cmd_list->SetBufferVertex(model->GetVertexBuffer());

                        // Update uber buffer with cascade transform
                        m_buffer_object_cpu.object = entity->GetTransform_PtrRaw()->GetMatrix() * view_projection;
                        if (UpdateObjectBuffer())
                        {
                            cmd_list->SetConstantBuffer(2, RHI_Buffer_VertexShader, m_buffer_object_gpu);
                        }

                        cmd_list->DrawIndexed(renderable->GeometryIndexCount(), renderable->GeometryIndexOffset(), renderable->GeometryVertexOffset());

                    }
                    cmd_list->End(); // end of array
                    cmd_list->Submit();
                }
            }
        }
	}

    void Renderer::Pass_DepthPrePass(RHI_CommandList* cmd_list)
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

        // Set render state
        static RHI_PipelineState pipeline_state;
        pipeline_state.shader_vertex                = shader_depth.get();
        pipeline_state.shader_pixel                 = nullptr;
        pipeline_state.rasterizer_state             = m_rasterizer_cull_back_solid.get();
        pipeline_state.blend_state                  = m_blend_disabled.get();
        pipeline_state.depth_stencil_state          = m_depth_stencil_enabled_write.get();
        pipeline_state.render_target_depth_texture  = tex_depth.get();
        pipeline_state.render_target_depth_clear    = GetClearDepth();
        pipeline_state.viewport                     = tex_depth->GetViewport();
        pipeline_state.primitive_topology           = RHI_PrimitiveTopology_TriangleList;
        pipeline_state.pass_name                    = "Pass_DepthPrePass";

        // Submit commands
        if (cmd_list->Begin(pipeline_state))
        { 
            if (!entities.empty())
            {
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
                        cmd_list->SetBufferIndex(model->GetIndexBuffer());
                        cmd_list->SetBufferVertex(model->GetVertexBuffer());
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
                    cmd_list->DrawIndexed(renderable->GeometryIndexCount(), renderable->GeometryIndexOffset(), renderable->GeometryVertexOffset());
                }
            }
            cmd_list->End();
            cmd_list->Submit();
        }
    }

	void Renderer::Pass_GBuffer(RHI_CommandList* cmd_list)
	{
        // Acquire required resources/shaders
        const auto& tex_albedo      = m_render_targets[RenderTarget_Gbuffer_Albedo];
        const auto& tex_normal      = m_render_targets[RenderTarget_Gbuffer_Normal];
        const auto& tex_material    = m_render_targets[RenderTarget_Gbuffer_Material];
        const auto& tex_velocity    = m_render_targets[RenderTarget_Gbuffer_Velocity];
        const auto& tex_depth       = m_render_targets[RenderTarget_Gbuffer_Depth];
        const auto& shader_v        = m_shaders[Shader_Gbuffer_V];

        // Validate that the shader has compiled
        if (!shader_v->IsCompiled())
            return;
      
        // Set render state
        static RHI_PipelineState pipeline_state;
        pipeline_state.shader_vertex                    = shader_v.get();
        pipeline_state.vertex_buffer_stride             = static_cast<uint32_t>(sizeof(RHI_Vertex_PosTexNorTan)); // assume all vertex buffers have the same stride
        pipeline_state.blend_state                      = m_blend_disabled.get();
        pipeline_state.rasterizer_state                 = GetOptionValue(Render_Debug_Wireframe)    ? m_rasterizer_cull_back_wireframe.get() : m_rasterizer_cull_back_solid.get();
        pipeline_state.depth_stencil_state              = GetOptionValue(Render_DepthPrepass)       ? m_depth_stencil_enabled_no_write.get() : m_depth_stencil_enabled_write.get();
        pipeline_state.render_target_color_textures[0]  = tex_albedo.get();
        pipeline_state.render_target_color_clear[0]     = Vector4::Zero;
        pipeline_state.render_target_color_textures[1]  = tex_normal.get();
        pipeline_state.render_target_color_clear[1]     = Vector4::Zero;
        pipeline_state.render_target_color_textures[2]  = tex_material.get();
        pipeline_state.render_target_color_clear[2]     = Vector4::Zero; // zeroed material buffer causes sky sphere to render
        pipeline_state.render_target_color_textures[3]  = tex_velocity.get();
        pipeline_state.render_target_color_clear[3]     = Vector4::Zero;
        pipeline_state.render_target_depth_texture      = tex_depth.get();
        pipeline_state.render_target_depth_clear        = GetOptionValue(Render_DepthPrepass) ? state_dont_clear_depth : GetClearDepth();
        pipeline_state.viewport                         = tex_albedo->GetViewport();
        pipeline_state.primitive_topology               = RHI_PrimitiveTopology_TriangleList;

        // Only useful to minimize D3D11 state changes (Vulkan backend is smarter)
        uint32_t m_set_material_id = 0;
        
        // Iterate through all the G-Buffer shader variations
        for (const shared_ptr<ShaderVariation>& resource : ShaderVariation::GetVariations())
        {
            if (!resource->IsCompiled())
                continue;

            // Set pixel shader
            pipeline_state.shader_pixel = static_cast<RHI_Shader*>(resource.get());

            // Set pass name
            pipeline_state.pass_name = pipeline_state.shader_pixel->GetName().c_str();

            auto& entities = m_entities[Renderer_Object_Opaque];
            const uint32_t entity_count = static_cast<uint32_t>(entities.size());

            // Enlarge constant buffer (if needed)
            bool buffer_reallocated = false;
            if (entity_count > m_buffer_object_gpu->GetOffsetCount())
            {
                const uint32_t new_size = m_buffer_object_gpu->GetOffsetCount() * 2;
                if (!m_buffer_object_gpu->Create<BufferObject>(new_size))
                    return;

                buffer_reallocated = true;
            }

            // Submit command list
            if (cmd_list->Begin(pipeline_state))
            {
                for (uint32_t i = 0; i < static_cast<uint32_t>(entities.size()); i++)
                {
                    Entity* entity = entities[i];

                    // Get renderable
                    const auto& renderable = entity->GetRenderable_PtrRaw();
                    if (!renderable)
                        continue;

                    // Get material
                    const auto& material = renderable->GetMaterial();
                    if (!material)
                        continue;

                    // Get shader
                    const auto& shader = material->GetShader();
                    if (!shader || !shader->IsCompiled())
                        continue;

                    // Get geometry
                    const auto& model = renderable->GeometryModel();
                    if (!model || !model->GetVertexBuffer() || !model->GetIndexBuffer())
                        continue;

                    // Draw matching shader entities
                    if (pipeline_state.shader_pixel->GetId() == shader->GetId())
                    {
                        // Skip objects outside of the view frustum
                        if (!m_camera->IsInViewFrustrum(renderable))
                            continue;

                        // Set geometry (will only happen if not already set)
                        cmd_list->SetBufferIndex(model->GetIndexBuffer());
                        cmd_list->SetBufferVertex(model->GetVertexBuffer());

                        // Bind material
                        if (m_set_material_id != material->GetId())
                        {
                            // Bind material textures		
                            cmd_list->SetTexture(0, material->GetTexture(TextureType_Albedo).get());
                            cmd_list->SetTexture(1, material->GetTexture(TextureType_Roughness).get());
                            cmd_list->SetTexture(2, material->GetTexture(TextureType_Metallic).get());
                            cmd_list->SetTexture(3, material->GetTexture(TextureType_Normal).get());
                            cmd_list->SetTexture(4, material->GetTexture(TextureType_Height).get());
                            cmd_list->SetTexture(5, material->GetTexture(TextureType_Occlusion).get());
                            cmd_list->SetTexture(6, material->GetTexture(TextureType_Emission).get());
                            cmd_list->SetTexture(7, material->GetTexture(TextureType_Mask).get());
                        
                            // Update uber buffer with material properties
                            m_buffer_uber_cpu.mat_albedo        = material->GetColorAlbedo();
                            m_buffer_uber_cpu.mat_tiling_uv     = material->GetTiling();
                            m_buffer_uber_cpu.mat_offset_uv     = material->GetOffset();
                            m_buffer_uber_cpu.mat_roughness_mul = material->GetMultiplier(TextureType_Roughness);
                            m_buffer_uber_cpu.mat_metallic_mul  = material->GetMultiplier(TextureType_Metallic);
                            m_buffer_uber_cpu.mat_normal_mul    = material->GetMultiplier(TextureType_Normal);
                            m_buffer_uber_cpu.mat_height_mul    = material->GetMultiplier(TextureType_Height);
                            m_buffer_uber_cpu.mat_shading_mode  = static_cast<float>(material->GetShadingMode());

                            // Update constant buffer
                            UpdateUberBuffer();

                            m_set_material_id = material->GetId();
                        }
                        
                        // Update uber buffer with entity transform
                        if (Transform* transform = entity->GetTransform_PtrRaw())
                        {
                            m_buffer_object_cpu.object          = transform->GetMatrix();
                            m_buffer_object_cpu.wvp_current     = transform->GetMatrix() * m_buffer_frame_cpu.view_projection;
                            m_buffer_object_cpu.wvp_previous    = transform->GetWvpLastFrame();

                            // Save matrix for velocity computation
                            transform->SetWvpLastFrame(m_buffer_object_cpu.wvp_current);

                            // Update constant buffer
                            if (UpdateObjectBuffer(i) || buffer_reallocated)
                            {
                                cmd_list->SetConstantBuffer(2, RHI_Buffer_VertexShader, m_buffer_object_gpu);
                            }
                        }
                        
                        // Render	
                        cmd_list->DrawIndexed(renderable->GeometryIndexCount(), renderable->GeometryIndexOffset(), renderable->GeometryVertexOffset());
                        m_profiler->m_renderer_meshes_rendered++;
                    }
                }
                cmd_list->End();
                cmd_list->Submit();
            }
        }
	}

	void Renderer::Pass_Ssao(RHI_CommandList* cmd_list)
	{
        if ((m_options & Render_SSAO) == 0)
            return;

        // Acquire shaders
        const auto& shader_v = m_shaders[Shader_Quad_V];
        const auto& shader_p = m_shaders[Shader_Ssao_P];
        if (!shader_v->IsCompiled() || !shader_p->IsCompiled())
            return;
        
        // Acquire render targets
        auto& tex_ssao_raw      = m_render_targets[RenderTarget_Ssao_Raw];
        auto& tex_ssao_blurred  = m_render_targets[RenderTarget_Ssao_Blurred];
        auto& tex_ssao          = m_render_targets[RenderTarget_Ssao];

        // Set render state
        static RHI_PipelineState pipeline_state;
        pipeline_state.shader_vertex                    = shader_v.get();
        pipeline_state.shader_pixel                     = shader_p.get();
        pipeline_state.rasterizer_state                 = m_rasterizer_cull_back_solid.get();
        pipeline_state.blend_state                      = m_blend_disabled.get();
        pipeline_state.depth_stencil_state              = m_depth_stencil_disabled.get();
        pipeline_state.vertex_buffer_stride             = m_quad.GetVertexBuffer()->GetStride();
        pipeline_state.render_target_color_textures[0]  = tex_ssao_raw.get();
        pipeline_state.viewport                         = tex_ssao_raw->GetViewport();
        pipeline_state.primitive_topology               = RHI_PrimitiveTopology_TriangleList;
        pipeline_state.pass_name                        = "Pass_Ssao";

        // Submit commands
        if (cmd_list->Begin(pipeline_state))
        {
            // Update uber buffer
            m_buffer_uber_cpu.resolution = Vector2(tex_ssao_raw->GetWidth(), tex_ssao_raw->GetHeight());
            UpdateUberBuffer();
        
            cmd_list->SetBufferVertex(m_quad.GetVertexBuffer());
            cmd_list->SetBufferIndex(m_quad.GetIndexBuffer());
            cmd_list->SetTexture(0, m_render_targets[RenderTarget_Gbuffer_Depth]);
            cmd_list->SetTexture(1, m_render_targets[RenderTarget_Gbuffer_Normal]);
            cmd_list->SetTexture(2, m_tex_noise_normal);
            cmd_list->DrawIndexed(Rectangle::GetIndexCount());
            cmd_list->End();
            cmd_list->Submit();
        
            // Bilateral blur
            const auto sigma = 2.0f;
            const auto pixel_stride = 2.0f;
            Pass_BlurBilateralGaussian(cmd_list, tex_ssao_raw, tex_ssao_blurred, sigma, pixel_stride);
        
            // Upscale to full size
            float ssao_scale = m_option_values[Option_Value_Ssao_Scale];
            if (ssao_scale < 1.0f)
            {
                Pass_Upsample(cmd_list, tex_ssao_blurred, tex_ssao);
            }
            else if (ssao_scale > 1.0f)
            {
                Pass_Downsample(cmd_list, tex_ssao_blurred, tex_ssao, Shader_Downsample_P);
            }
            else
            {
                tex_ssao_blurred.swap(tex_ssao);
            }
        }
	}

    void Renderer::Pass_Ssr(RHI_CommandList* cmd_list)
    {
        if ((m_options & Render_SSR) == 0)
            return;

        // Acquire shaders
        const auto& shader_v = m_shaders[Shader_Quad_V];
        const auto& shader_p = m_shaders[Shader_Ssr_P];
        if (!shader_v->IsCompiled() || !shader_p->IsCompiled())
            return;
        
        // Acquire render targets
        auto& tex_ssr           = m_render_targets[RenderTarget_Ssr];
        auto& tex_ssr_blurred   = m_render_targets[RenderTarget_Ssr_Blurred];

        // Set render state
        static RHI_PipelineState pipeline_state;
        pipeline_state.shader_vertex                    = shader_v.get();
        pipeline_state.shader_pixel                     = shader_p.get();
        pipeline_state.rasterizer_state                 = m_rasterizer_cull_back_solid.get();
        pipeline_state.blend_state                      = m_blend_disabled.get();
        pipeline_state.depth_stencil_state              = m_depth_stencil_disabled.get();
        pipeline_state.vertex_buffer_stride             = m_quad.GetVertexBuffer()->GetStride();
        pipeline_state.render_target_color_textures[0]  = tex_ssr.get();
        pipeline_state.viewport                         = tex_ssr->GetViewport();
        pipeline_state.primitive_topology               = RHI_PrimitiveTopology_TriangleList;
        pipeline_state.pass_name                        = "Pass_Ssr";

        // Submit commands
        if (cmd_list->Begin(pipeline_state))
        {
            // Update uber buffer
            m_buffer_uber_cpu.resolution = Vector2(tex_ssr->GetWidth(), tex_ssr->GetHeight());
            UpdateUberBuffer();
        
            cmd_list->SetBufferVertex(m_quad.GetVertexBuffer());
            cmd_list->SetBufferIndex(m_quad.GetIndexBuffer());
            cmd_list->SetTexture(0, m_render_targets[RenderTarget_Gbuffer_Normal]);
            cmd_list->SetTexture(1, m_render_targets[RenderTarget_Gbuffer_Depth]);
            cmd_list->SetTexture(2, m_render_targets[RenderTarget_Gbuffer_Material]);
            cmd_list->SetTexture(3, m_render_targets[RenderTarget_Composition_Ldr_2]);
            cmd_list->DrawIndexed(Rectangle::GetIndexCount());
            cmd_list->End();
            cmd_list->Submit();
        
            // Bilateral blur
            const auto sigma = 1.0f;
            const auto pixel_stride = 1.0f;
            Pass_BlurGaussian(cmd_list, tex_ssr, tex_ssr_blurred, sigma, pixel_stride);
        }
    }

    void Renderer::Pass_Light(RHI_CommandList* cmd_list)
    {
        // Acquire shaders
        const auto& shader_v                = m_shaders[Shader_Quad_V];
        const auto& shader_p_directional    = m_shaders[Shader_LightDirectional_P];
        const auto& shader_p_point          = m_shaders[Shader_LightPoint_P];
        const auto& shader_p_spot           = m_shaders[Shader_LightSpot_P];
        if (!shader_v->IsCompiled() || !shader_p_directional->IsCompiled() || !shader_p_point->IsCompiled() || !shader_p_spot->IsCompiled())
            return;

        // Acquire render targets
        auto& tex_diffuse       = m_render_targets[RenderTarget_Light_Diffuse];
        auto& tex_specular      = m_render_targets[RenderTarget_Light_Specular];
        auto& tex_volumetric    = m_render_targets[RenderTarget_Light_Volumetric];

        // Update uber buffer
        m_buffer_uber_cpu.resolution = Vector2(static_cast<float>(tex_diffuse->GetWidth()), static_cast<float>(tex_diffuse->GetHeight()));
        UpdateUberBuffer();

         // Set render state
        static RHI_PipelineState pipeline_state;
        pipeline_state.shader_vertex                    = shader_v.get();
        pipeline_state.rasterizer_state                 = m_rasterizer_cull_back_solid.get();
        pipeline_state.blend_state                      = m_blend_color_add.get(); // light accumulation
        pipeline_state.depth_stencil_state              = m_depth_stencil_disabled.get();
        pipeline_state.vertex_buffer_stride             = m_quad.GetVertexBuffer()->GetStride();
        pipeline_state.render_target_color_textures[0]  = tex_diffuse.get();
        pipeline_state.render_target_color_clear[0]     = Vector4::Zero;
        pipeline_state.render_target_color_textures[1]  = tex_specular.get();
        pipeline_state.render_target_color_clear[1]     = Vector4::Zero;
        pipeline_state.render_target_color_textures[2]  = tex_volumetric.get();
        pipeline_state.render_target_color_clear[2]     = Vector4::Zero;
        pipeline_state.viewport                         = tex_diffuse->GetViewport();
        pipeline_state.primitive_topology               = RHI_PrimitiveTopology_TriangleList;
        pipeline_state.pass_name                        = "Pass_Light";

        auto draw_lights = [this, &cmd_list, &shader_v, &shader_p_directional, &shader_p_point, &shader_p_spot, &tex_diffuse, &tex_specular, &tex_volumetric](Renderer_Object_Type type)
        {
            const vector<Entity*>& entities = m_entities[type];
            if (entities.empty())
                return;

            // Choose correct shader
            RHI_Shader* shader_p = nullptr;
            if (type == Renderer_Object_LightDirectional)   shader_p = shader_p_directional.get();
            else if (type == Renderer_Object_LightPoint)    shader_p = shader_p_point.get();
            else if (type == Renderer_Object_LightSpot)     shader_p = shader_p_spot.get();

            // Set pixel shader
            pipeline_state.shader_pixel = shader_p;

            if (cmd_list->Begin(pipeline_state))
            {
                // Update light buffer   
                UpdateLightBuffer(entities);

                cmd_list->SetBufferVertex(m_quad.GetVertexBuffer());
                cmd_list->SetBufferIndex(m_quad.GetIndexBuffer());
                cmd_list->SetTexture(0, m_render_targets[RenderTarget_Gbuffer_Normal]);
                cmd_list->SetTexture(1, m_render_targets[RenderTarget_Gbuffer_Material]);
                cmd_list->SetTexture(2, m_render_targets[RenderTarget_Gbuffer_Depth]);
                cmd_list->SetTexture(3, (m_options & Render_SSAO) ? m_render_targets[RenderTarget_Ssao] : m_tex_white);

                // Draw
                for (const auto& entity : entities)
                {
                    if (Light* light = entity->GetComponent<Light>().get())
                    {
                        if (RHI_Texture* shadow_map = light->GetShadowMap().get())
                        {
                            cmd_list->SetTexture(4, light->GetCastShadows() ? (light->GetLightType() == LightType_Directional   ? shadow_map : nullptr) : nullptr);
                            cmd_list->SetTexture(5, light->GetCastShadows() ? (light->GetLightType() == LightType_Point         ? shadow_map : nullptr) : nullptr);
                            cmd_list->SetTexture(6, light->GetCastShadows() ? (light->GetLightType() == LightType_Spot          ? shadow_map : nullptr) : nullptr);
                            cmd_list->DrawIndexed(Rectangle::GetIndexCount());
                        }
                    }
                }

                cmd_list->End();
                cmd_list->Submit();
            }
        };

        // Draw lights
        draw_lights(Renderer_Object_LightDirectional);
        draw_lights(Renderer_Object_LightPoint);
        draw_lights(Renderer_Object_LightSpot);

        // If we are doing volumetric lighting, blur it
        if (m_options & Render_VolumetricLighting)
        {
            const auto sigma        = 2.0f;
            const auto pixel_stride = 2.0f;
            Pass_BlurGaussian(cmd_list, tex_volumetric, m_render_targets[RenderTarget_Light_Volumetric_Blurred], sigma, pixel_stride);
        }
    }

	void Renderer::Pass_Composition(RHI_CommandList* cmd_list)
	{
        // Acquire shaders
        const auto& shader_v = m_shaders[Shader_Quad_V];
		const auto& shader_p = m_shaders[Shader_Composition_P];
		if (!shader_v->IsCompiled() || !shader_p->IsCompiled())
			return;

        // Acquire render target
        auto& tex_out = m_render_targets[RenderTarget_Composition_Hdr];

        // Set render state
        static RHI_PipelineState pipeline_state;
        pipeline_state.shader_vertex                    = shader_v.get();
        pipeline_state.shader_pixel                     = shader_p.get();
        pipeline_state.rasterizer_state                 = m_rasterizer_cull_back_solid.get();
        pipeline_state.blend_state                      = m_blend_disabled.get();
        pipeline_state.depth_stencil_state              = m_depth_stencil_disabled.get();
        pipeline_state.vertex_buffer_stride             = m_quad.GetVertexBuffer()->GetStride();
        pipeline_state.render_target_color_textures[0]  = tex_out.get();
        pipeline_state.primitive_topology               = RHI_PrimitiveTopology_TriangleList;
        pipeline_state.viewport                         = tex_out->GetViewport();
        pipeline_state.pass_name                        = "Pass_Composition";

        // Begin commands
        if (cmd_list->Begin(pipeline_state))
        {
            // Update uber buffer
            m_buffer_uber_cpu.resolution = Vector2(static_cast<float>(tex_out->GetWidth()), static_cast<float>(tex_out->GetHeight()));
            UpdateUberBuffer();

            // Setup command list
            cmd_list->SetBufferVertex(m_quad.GetVertexBuffer());
            cmd_list->SetBufferIndex(m_quad.GetIndexBuffer());
            cmd_list->SetTexture(0, m_render_targets[RenderTarget_Gbuffer_Albedo]);
            cmd_list->SetTexture(1, m_render_targets[RenderTarget_Gbuffer_Normal]);
            cmd_list->SetTexture(2, m_render_targets[RenderTarget_Gbuffer_Depth]);
            cmd_list->SetTexture(3, m_render_targets[RenderTarget_Gbuffer_Material]);
            cmd_list->SetTexture(4, m_render_targets[RenderTarget_Light_Diffuse]);
            cmd_list->SetTexture(5, m_render_targets[RenderTarget_Light_Specular]);
            cmd_list->SetTexture(6, (m_options & Render_VolumetricLighting) ? m_render_targets[RenderTarget_Light_Volumetric_Blurred] : m_tex_black);
            cmd_list->SetTexture(7, (m_options & Render_SSR) ? m_render_targets[RenderTarget_Ssr_Blurred] : m_tex_black);
            cmd_list->SetTexture(8, GetEnvironmentTexture());
            cmd_list->SetTexture(9, m_render_targets[RenderTarget_Brdf_Specular_Lut]);
            cmd_list->SetTexture(10, (m_options & Render_SSAO)? m_render_targets[RenderTarget_Ssao] : m_tex_white);
            cmd_list->SetBufferIndex(m_quad.GetIndexBuffer());
            cmd_list->SetBufferVertex(m_quad.GetVertexBuffer());
            cmd_list->DrawIndexed(Rectangle::GetIndexCount());
            cmd_list->End();
            cmd_list->Submit();
        }
	}

	void Renderer::Pass_PostProcess(RHI_CommandList* cmd_list)
	{
        // IN:  RenderTarget_Composition_Hdr
        // OUT: RenderTarget_Composition_Ldr

        // Acquire render targets
        auto& tex_in_hdr    = m_render_targets[RenderTarget_Composition_Hdr];
        auto& tex_out_hdr   = m_render_targets[RenderTarget_Composition_Hdr_2];
        auto& tex_in_ldr    = m_render_targets[RenderTarget_Composition_Ldr];
        auto& tex_out_ldr   = m_render_targets[RenderTarget_Composition_Ldr_2];

        // TAA	
        if (GetOptionValue(Render_AntiAliasing_TAA))
        {
            Pass_TAA(cmd_list, tex_in_hdr, tex_out_hdr);
            tex_in_hdr.swap(tex_out_hdr);
        }

        // Motion Blur
        if (GetOptionValue(Render_MotionBlur))
        {
            Pass_MotionBlur(cmd_list, tex_in_hdr, tex_out_hdr);
            tex_in_hdr.swap(tex_out_hdr);
        }

        // Bloom
        if (GetOptionValue(Render_Bloom))
        {
            Pass_Bloom(cmd_list, tex_in_hdr, tex_out_hdr);
            tex_in_hdr.swap(tex_out_hdr);
        }

        // Tone-Mapping
        if (m_option_values[Option_Value_Tonemapping] != 0)
        {
            Pass_ToneMapping(cmd_list, tex_in_hdr, tex_in_ldr); // HDR -> LDR
        }
        else
        {
            Pass_Copy(cmd_list, tex_in_hdr, tex_in_ldr);
        }

        // Dithering
        if (GetOptionValue(Render_Dithering))
        {
            Pass_Dithering(cmd_list, tex_in_ldr, tex_out_ldr);
            tex_in_ldr.swap(tex_out_ldr);
        }

        // FXAA
        if (GetOptionValue(Render_AntiAliasing_FXAA))
        {
            Pass_FXAA(cmd_list, tex_in_ldr, tex_out_ldr);
            tex_in_ldr.swap(tex_out_ldr);
        }

        // Sharpening
        if (GetOptionValue(Render_Sharpening_LumaSharpen))
        {
            Pass_LumaSharpen(cmd_list, tex_in_ldr, tex_out_ldr);
            tex_in_ldr.swap(tex_out_ldr);
        }

        // Chromatic aberration
        if (GetOptionValue(Render_ChromaticAberration))
        {
            Pass_ChromaticAberration(cmd_list, tex_in_ldr, tex_out_ldr);
            tex_in_ldr.swap(tex_out_ldr);
        }

        // Gamma correction
        Pass_GammaCorrection(cmd_list, tex_in_ldr, tex_out_ldr);

        // Swap textures
        tex_in_ldr.swap(tex_out_ldr);
	}

    void Renderer::Pass_Upsample(RHI_CommandList* cmd_list, shared_ptr<RHI_Texture>& tex_in, shared_ptr<RHI_Texture>& tex_out)
    {
        // Acquire shaders
        const auto& shader_v = m_shaders[Shader_Quad_V];
        const auto& shader_p = m_shaders[Shader_Upsample_P];
        if (!shader_v->IsCompiled() || !shader_p->IsCompiled())
            return;

        // Set render state
        static RHI_PipelineState pipeline_state;
        pipeline_state.shader_vertex                    = shader_v.get();
        pipeline_state.shader_pixel                     = shader_p.get();
        pipeline_state.rasterizer_state                 = m_rasterizer_cull_back_solid.get();
        pipeline_state.blend_state                      = m_blend_disabled.get();
        pipeline_state.depth_stencil_state              = m_depth_stencil_disabled.get();
        pipeline_state.vertex_buffer_stride             = m_quad.GetVertexBuffer()->GetStride();
        pipeline_state.render_target_color_textures[0]  = tex_out.get();
        pipeline_state.viewport                         = tex_out->GetViewport();
        pipeline_state.primitive_topology               = RHI_PrimitiveTopology_TriangleList;
        pipeline_state.pass_name                        = "Pass_Upsample";

        // Submit commands
        if (cmd_list->Begin(pipeline_state))
        {
            m_buffer_uber_cpu.resolution = Vector2(static_cast<float>(tex_out->GetWidth()), static_cast<float>(tex_out->GetHeight()));
            UpdateUberBuffer();

            cmd_list->SetBufferVertex(m_quad.GetVertexBuffer());
            cmd_list->SetBufferIndex(m_quad.GetIndexBuffer());
            cmd_list->SetTexture(0, tex_in);
            cmd_list->DrawIndexed(m_quad.GetIndexCount());
            cmd_list->End();
            cmd_list->Submit();
        }
    }

    void Renderer::Pass_Downsample(RHI_CommandList* cmd_list, shared_ptr<RHI_Texture>& tex_in, shared_ptr<RHI_Texture>& tex_out, Renderer_Shader_Type pixel_shader)
    {
        // Acquire shaders
        const auto& shader_v = m_shaders[Shader_Quad_V];
        const auto& shader_p = m_shaders[pixel_shader];
        if (!shader_v->IsCompiled() || !shader_p->IsCompiled())
            return;

        // Set render state
        static RHI_PipelineState pipeline_state;
        pipeline_state.shader_vertex                    = shader_v.get();
        pipeline_state.shader_pixel                     = shader_p.get();
        pipeline_state.rasterizer_state                 = m_rasterizer_cull_back_solid.get();
        pipeline_state.blend_state                      = m_blend_disabled.get();
        pipeline_state.depth_stencil_state              = m_depth_stencil_disabled.get();
        pipeline_state.vertex_buffer_stride             = m_quad.GetVertexBuffer()->GetStride();
        pipeline_state.render_target_color_textures[0]  = tex_out.get();
        pipeline_state.viewport                         = tex_out->GetViewport();
        pipeline_state.primitive_topology               = RHI_PrimitiveTopology_TriangleList;
        pipeline_state.pass_name                        = "Pass_Downsample";

        // Submit commands
        if (cmd_list->Begin(pipeline_state))
        {
            // Update uber buffer
            m_buffer_uber_cpu.resolution = Vector2(static_cast<float>(tex_out->GetWidth()), static_cast<float>(tex_out->GetHeight()));
            UpdateUberBuffer();
        
            cmd_list->SetBufferVertex(m_quad.GetVertexBuffer());
            cmd_list->SetBufferIndex(m_quad.GetIndexBuffer());
            cmd_list->SetTexture(0, tex_in);
            cmd_list->DrawIndexed(m_quad.GetIndexCount());
            cmd_list->End();
            cmd_list->Submit();
        }
    }

	void Renderer::Pass_BlurBox(RHI_CommandList* cmd_list, shared_ptr<RHI_Texture>& tex_in, shared_ptr<RHI_Texture>& tex_out, const float sigma)
	{
        // Acquire shaders
        const auto& shader_v = m_shaders[Shader_Quad_V];
        const auto& shader_p = m_shaders[Shader_BlurBox_P];
        if (!shader_v->IsCompiled() || !shader_p->IsCompiled())
            return;

        // Set render state
        static RHI_PipelineState pipeline_state;
        pipeline_state.shader_vertex                    = shader_v.get();
        pipeline_state.shader_pixel                     = shader_p.get();
        pipeline_state.rasterizer_state                 = m_rasterizer_cull_back_solid.get();
        pipeline_state.blend_state                      = m_blend_disabled.get();
        pipeline_state.depth_stencil_state              = m_depth_stencil_disabled.get();
        pipeline_state.vertex_buffer_stride             = m_quad.GetVertexBuffer()->GetStride();
        pipeline_state.render_target_color_textures[0]  = tex_out.get();
        pipeline_state.viewport                         = tex_out->GetViewport();
        pipeline_state.primitive_topology               = RHI_PrimitiveTopology_TriangleList;
        pipeline_state.pass_name                        = "Pass_BlurBox";

        // Submit commands
        if (cmd_list->Begin(pipeline_state))
        {
            // Update uber buffer
            m_buffer_uber_cpu.resolution = Vector2(static_cast<float>(tex_out->GetWidth()), static_cast<float>(tex_out->GetHeight()));
            UpdateUberBuffer();

            cmd_list->SetBufferVertex(m_quad.GetVertexBuffer());
            cmd_list->SetBufferIndex(m_quad.GetIndexBuffer());
            cmd_list->SetTexture(0, tex_in); // Shadows are in the alpha channel
            cmd_list->DrawIndexed(m_quad.GetIndexCount());
            cmd_list->End();
            cmd_list->Submit();
        }
	}

	void Renderer::Pass_BlurGaussian(RHI_CommandList* cmd_list, shared_ptr<RHI_Texture>& tex_in, shared_ptr<RHI_Texture>& tex_out, const float sigma, const float pixel_stride)
	{
		if (tex_in->GetWidth() != tex_out->GetWidth() || tex_in->GetHeight() != tex_out->GetHeight() || tex_in->GetFormat() != tex_out->GetFormat())
		{
			LOG_ERROR("Invalid parameters, textures must match because they will get swapped");
			return;
		}

        // Acquire shaders
        const auto& shader_v = m_shaders[Shader_Quad_V];
        const auto& shader_p = m_shaders[Shader_BlurGaussian_P];
        if (!shader_v->IsCompiled() || !shader_p->IsCompiled())
            return;

        // Set render state for horizontal pass
        static RHI_PipelineState pipeline_state_horizontal;
        pipeline_state_horizontal.shader_vertex                     = shader_v.get();
        pipeline_state_horizontal.shader_pixel                      = shader_p.get();
        pipeline_state_horizontal.rasterizer_state                  = m_rasterizer_cull_back_solid.get();
        pipeline_state_horizontal.blend_state                       = m_blend_disabled.get();
        pipeline_state_horizontal.depth_stencil_state               = m_depth_stencil_disabled.get();
        pipeline_state_horizontal.vertex_buffer_stride              = m_quad.GetVertexBuffer()->GetStride();
        pipeline_state_horizontal.render_target_color_textures[0]   = tex_out.get();
        pipeline_state_horizontal.viewport                          = tex_out->GetViewport();
        pipeline_state_horizontal.primitive_topology                = RHI_PrimitiveTopology_TriangleList;
        pipeline_state_horizontal.pass_name                         = "Pass_BlurGaussian_Horizontal";

        // Submit commands for horizontal pass
        if (cmd_list->Begin(pipeline_state_horizontal))
        {
            // Update uber buffer
            m_buffer_uber_cpu.resolution        = Vector2(static_cast<float>(tex_in->GetWidth()), static_cast<float>(tex_in->GetHeight()));
            m_buffer_uber_cpu.blur_direction    = Vector2(pixel_stride, 0.0f);
            m_buffer_uber_cpu.blur_sigma        = sigma;
            UpdateUberBuffer();
        
            cmd_list->SetBufferVertex(m_quad.GetVertexBuffer());
            cmd_list->SetBufferIndex(m_quad.GetIndexBuffer());
        	cmd_list->SetTexture(0, tex_in);
        	cmd_list->DrawIndexed(Rectangle::GetIndexCount());
            cmd_list->End();
            cmd_list->Submit();
        }
        
        // Set render state for vertical pass
        static RHI_PipelineState pipeline_state_vertical;
        pipeline_state_vertical.shader_vertex                   = shader_v.get();
        pipeline_state_vertical.shader_pixel                    = shader_p.get();
        pipeline_state_vertical.rasterizer_state                = m_rasterizer_cull_back_solid.get();
        pipeline_state_vertical.blend_state                     = m_blend_disabled.get();
        pipeline_state_vertical.depth_stencil_state             = m_depth_stencil_disabled.get();
        pipeline_state_vertical.vertex_buffer_stride            = m_quad.GetVertexBuffer()->GetStride();
        pipeline_state_vertical.render_target_color_textures[0] = tex_in.get();
        pipeline_state_vertical.viewport                        = tex_in->GetViewport();
        pipeline_state_vertical.primitive_topology              = RHI_PrimitiveTopology_TriangleList;
        pipeline_state_vertical.pass_name                       = "Pass_BlurGaussian_Vertical";

        // Submit commands for vertical pass
        if (cmd_list->Begin(pipeline_state_vertical))
        {
            m_buffer_uber_cpu.blur_direction    = Vector2(0.0f, pixel_stride);
            m_buffer_uber_cpu.blur_sigma        = sigma;
            UpdateUberBuffer();
        
            cmd_list->SetBufferVertex(m_quad.GetVertexBuffer());
            cmd_list->SetBufferIndex(m_quad.GetIndexBuffer());
            cmd_list->SetTexture(0, tex_out);
            cmd_list->DrawIndexed(Rectangle::GetIndexCount());
            cmd_list->End();
            cmd_list->Submit();
        }

		// Swap textures
		tex_in.swap(tex_out);
	}

	void Renderer::Pass_BlurBilateralGaussian(RHI_CommandList* cmd_list, shared_ptr<RHI_Texture>& tex_in, shared_ptr<RHI_Texture>& tex_out, const float sigma, const float pixel_stride)
	{
		if (tex_in->GetWidth() != tex_out->GetWidth() || tex_in->GetHeight() != tex_out->GetHeight() || tex_in->GetFormat() != tex_out->GetFormat())
		{
			LOG_ERROR("Invalid parameters, textures must match because they will get swapped.");
			return;
		}

		// Acquire shaders
		const auto& shader_v = m_shaders[Shader_Quad_V];
        const auto& shader_p = m_shaders[Shader_BlurGaussianBilateral_P];
		if (!shader_v->IsCompiled() || !shader_p->IsCompiled())
			return;

        // Acquire render targets
        auto& tex_depth     = m_render_targets[RenderTarget_Gbuffer_Depth];
        auto& tex_normal    = m_render_targets[RenderTarget_Gbuffer_Normal];

        // Set render state for horizontal pass
        static RHI_PipelineState pipeline_state_horizontal;
        pipeline_state_horizontal.shader_vertex                     = shader_v.get();
        pipeline_state_horizontal.shader_pixel                      = shader_p.get();
        pipeline_state_horizontal.rasterizer_state                  = m_rasterizer_cull_back_solid.get();
        pipeline_state_horizontal.blend_state                       = m_blend_disabled.get();
        pipeline_state_horizontal.depth_stencil_state               = m_depth_stencil_disabled.get();
        pipeline_state_horizontal.vertex_buffer_stride              = m_quad.GetVertexBuffer()->GetStride();
        pipeline_state_horizontal.render_target_color_textures[0]   = tex_out.get();
        pipeline_state_horizontal.viewport                          = tex_out->GetViewport();
        pipeline_state_horizontal.primitive_topology                = RHI_PrimitiveTopology_TriangleList;
        pipeline_state_horizontal.pass_name                         = "Pass_BlurBilateralGaussian_Horizontal";

        // Submit commands for horizontal pass
        if (cmd_list->Begin(pipeline_state_horizontal))
        {
            // Update uber buffer
            m_buffer_uber_cpu.resolution        = Vector2(static_cast<float>(tex_in->GetWidth()), static_cast<float>(tex_in->GetHeight()));
            m_buffer_uber_cpu.blur_direction    = Vector2(pixel_stride, 0.0f);
            m_buffer_uber_cpu.blur_sigma        = sigma;
            UpdateUberBuffer();

            cmd_list->SetBufferVertex(m_quad.GetVertexBuffer());
            cmd_list->SetBufferIndex(m_quad.GetIndexBuffer());
            cmd_list->SetTexture(0, tex_in);
            cmd_list->SetTexture(1, tex_depth);
            cmd_list->SetTexture(2, tex_normal);
            cmd_list->DrawIndexed(m_quad.GetIndexCount());
            cmd_list->End();
            cmd_list->Submit();
        }

        // Set render state for vertical pass
        static RHI_PipelineState pipeline_state_vertical;
        pipeline_state_vertical.shader_vertex                   = shader_v.get();
        pipeline_state_vertical.shader_pixel                    = shader_p.get();
        pipeline_state_vertical.rasterizer_state                = m_rasterizer_cull_back_solid.get();
        pipeline_state_vertical.blend_state                     = m_blend_disabled.get();
        pipeline_state_vertical.depth_stencil_state             = m_depth_stencil_disabled.get();
        pipeline_state_vertical.vertex_buffer_stride            = m_quad.GetVertexBuffer()->GetStride();
        pipeline_state_vertical.render_target_color_textures[0] = tex_in.get();
        pipeline_state_vertical.viewport                        = tex_in->GetViewport();
        pipeline_state_vertical.primitive_topology              = RHI_PrimitiveTopology_TriangleList;
        pipeline_state_vertical.pass_name                       = "Pass_BlurBilateralGaussian_Vertical";

        // Submit commands for vertical pass
        if (cmd_list->Begin(pipeline_state_vertical))
        {
            // Update uber buffer
            m_buffer_uber_cpu.blur_direction    = Vector2(0.0f, pixel_stride);
            m_buffer_uber_cpu.blur_sigma        = sigma;
            UpdateUberBuffer();

            cmd_list->SetBufferVertex(m_quad.GetVertexBuffer());
            cmd_list->SetBufferIndex(m_quad.GetIndexBuffer());
            cmd_list->SetTexture(0, tex_out);
            cmd_list->SetTexture(1, tex_depth);
            cmd_list->SetTexture(2, tex_normal);
            cmd_list->DrawIndexed(m_quad.GetIndexCount());
            cmd_list->End();
            cmd_list->Submit();
        }

        // Swap textures
		tex_in.swap(tex_out);
	}

	void Renderer::Pass_TAA(RHI_CommandList* cmd_list, shared_ptr<RHI_Texture>& tex_in, shared_ptr<RHI_Texture>& tex_out)
	{
		// Acquire shaders
        const auto& shader_v = m_shaders[Shader_Quad_V];
		const auto& shader_p = m_shaders[Shader_Taa_P];
		if (!shader_v->IsCompiled() || !shader_p->IsCompiled())
			return;

        // Acquire render targets
        auto& tex_history   = m_render_targets[RenderTarget_Composition_Hdr_History];
        auto& tex_history_2 = m_render_targets[RenderTarget_Composition_Hdr_History_2];

        // Set render state
        static RHI_PipelineState pipeline_state;
        pipeline_state.shader_vertex                    = shader_v.get();
        pipeline_state.shader_pixel                     = shader_p.get();
        pipeline_state.rasterizer_state                 = m_rasterizer_cull_back_solid.get();
        pipeline_state.blend_state                      = m_blend_disabled.get();
        pipeline_state.depth_stencil_state              = m_depth_stencil_disabled.get();
        pipeline_state.vertex_buffer_stride             = m_quad.GetVertexBuffer()->GetStride();
        pipeline_state.render_target_color_textures[0]  = tex_history_2.get();
        pipeline_state.viewport                         = tex_history_2->GetViewport();
        pipeline_state.primitive_topology               = RHI_PrimitiveTopology_TriangleList;
        pipeline_state.pass_name                        = "Pass_TAA";

        // Submit command list
		if (cmd_list->Begin(pipeline_state))
		{
            // Update uber buffer
            m_buffer_uber_cpu.resolution = Vector2(static_cast<float>(tex_out->GetWidth()), static_cast<float>(tex_out->GetHeight()));
            UpdateUberBuffer();

            cmd_list->SetBufferVertex(m_quad.GetVertexBuffer());
            cmd_list->SetBufferIndex(m_quad.GetIndexBuffer());
            cmd_list->SetTexture(0, tex_history);
            cmd_list->SetTexture(1, tex_in);
            cmd_list->SetTexture(2, m_render_targets[RenderTarget_Gbuffer_Velocity]);
            cmd_list->SetTexture(3, m_render_targets[RenderTarget_Gbuffer_Depth]);
			cmd_list->DrawIndexed(Rectangle::GetIndexCount());
            cmd_list->End();
            cmd_list->Submit();
		}

		// Copy
        Pass_Copy(cmd_list, tex_history_2, tex_out);

		// Swap history texture so the above works again in the next frame
        tex_history.swap(tex_history_2);
	}

	void Renderer::Pass_Bloom(RHI_CommandList* cmd_list, shared_ptr<RHI_Texture>& tex_in, shared_ptr<RHI_Texture>& tex_out)
	{
		// Acquire shaders
        const auto& shader_v                    = m_shaders[Shader_Quad_V];
		const auto& shader_p_bloom_luminance	= m_shaders[Shader_BloomDownsampleLuminance_P];
		const auto& shader_p_bloom_blend	    = m_shaders[Shader_BloomBlend_P];
		const auto& shader_p_downsample	        = m_shaders[Shader_BloomDownsample_P];
		const auto& shader_p_upsample		    = m_shaders[Shader_Upsample_P];
		if (!shader_p_downsample->IsCompiled() || !shader_p_bloom_luminance->IsCompiled() || !shader_p_upsample->IsCompiled() || !shader_p_downsample->IsCompiled())
			return;

        // Luminance
        {
            // Set render state
            static RHI_PipelineState pipeline_state;
            pipeline_state.shader_vertex                    = shader_v.get();
            pipeline_state.shader_pixel                     = shader_p_bloom_luminance.get();
            pipeline_state.rasterizer_state                 = m_rasterizer_cull_back_solid.get();
            pipeline_state.blend_state                      = m_blend_disabled.get();
            pipeline_state.depth_stencil_state              = m_depth_stencil_disabled.get();
            pipeline_state.vertex_buffer_stride             = m_quad.GetVertexBuffer()->GetStride();
            pipeline_state.render_target_color_textures[0]  = m_render_tex_bloom[0].get();
            pipeline_state.viewport                         = m_render_tex_bloom[0]->GetViewport();
            pipeline_state.primitive_topology               = RHI_PrimitiveTopology_TriangleList;
            pipeline_state.pass_name                        = "Pass_Bloom_Luminance";

            // Submit command list
            if (cmd_list->Begin(pipeline_state))
            {
                // Update uber buffer
                m_buffer_uber_cpu.resolution = Vector2(static_cast<float>(m_render_tex_bloom[0]->GetWidth()), static_cast<float>(m_render_tex_bloom[0]->GetHeight()));
                UpdateUberBuffer();
        
                cmd_list->SetBufferVertex(m_quad.GetVertexBuffer());
                cmd_list->SetBufferIndex(m_quad.GetIndexBuffer());
                cmd_list->SetTexture(0, tex_in);
                cmd_list->DrawIndexed(Rectangle::GetIndexCount());
                cmd_list->End();
                cmd_list->Submit();
            }
        }
        
        // Downsample
                    // The last bloom texture is the same size as the previous one (it's used for the Gaussian pass below), so we skip it
        for (int i = 0; i < static_cast<int>(m_render_tex_bloom.size() - 1); i++)
        {
            Pass_Downsample(cmd_list, m_render_tex_bloom[i], m_render_tex_bloom[i + 1], Shader_BloomDownsample_P);
        }
        
        auto upsample = [this, &cmd_list, &shader_v, &shader_p_upsample](shared_ptr<RHI_Texture>& tex_in, shared_ptr<RHI_Texture>& tex_out)
        {
            // Set render state
            static RHI_PipelineState pipeline_state;
            pipeline_state.shader_vertex                    = shader_v.get();
            pipeline_state.shader_pixel                     = shader_p_upsample.get();
            pipeline_state.rasterizer_state                 = m_rasterizer_cull_back_solid.get();
            pipeline_state.blend_state                      = m_blend_bloom.get(); // blend with previous
            pipeline_state.depth_stencil_state              = m_depth_stencil_disabled.get();
            pipeline_state.vertex_buffer_stride             = m_quad.GetVertexBuffer()->GetStride();
            pipeline_state.render_target_color_textures[0]  = tex_out.get();  
            pipeline_state.viewport                         = tex_out->GetViewport();
            pipeline_state.primitive_topology               = RHI_PrimitiveTopology_TriangleList;
            pipeline_state.pass_name                        = "Pass_Bloom_Upsample";

            if (cmd_list->Begin(pipeline_state))
            {
                // Update uber buffer
                m_buffer_uber_cpu.resolution = Vector2(static_cast<float>(tex_out->GetWidth()), static_cast<float>(tex_out->GetHeight()));
                UpdateUberBuffer();
        
                cmd_list->SetBufferVertex(m_quad.GetVertexBuffer());
                cmd_list->SetBufferIndex(m_quad.GetIndexBuffer());
                cmd_list->SetTexture(0, tex_in);
                cmd_list->DrawIndexed(Rectangle::GetIndexCount());
                cmd_list->End();
                cmd_list->Submit(); // we have to submit because all upsample passes are using the uber buffer
            }
        };
        
        // Upsample + blend
        for (int i = static_cast<int>(m_render_tex_bloom.size() - 1); i > 0; i--)
        {
            upsample(m_render_tex_bloom[i], m_render_tex_bloom[i - 1]);
        }
        
        // Additive blending
        {
            // Set render state
            static RHI_PipelineState pipeline_state;
            pipeline_state.shader_vertex                    = shader_v.get();
            pipeline_state.shader_pixel                     = shader_p_bloom_blend.get();
            pipeline_state.rasterizer_state                 = m_rasterizer_cull_back_solid.get();
            pipeline_state.blend_state                      = m_blend_disabled.get();
            pipeline_state.depth_stencil_state              = m_depth_stencil_disabled.get();
            pipeline_state.vertex_buffer_stride             = m_quad.GetVertexBuffer()->GetStride();
            pipeline_state.render_target_color_textures[0]  = tex_out.get();
            pipeline_state.viewport                         = tex_out->GetViewport();
            pipeline_state.primitive_topology               = RHI_PrimitiveTopology_TriangleList;
            pipeline_state.pass_name                        = "Pass_Bloom_Additive_Blending";
        
            // Submit command list
            if (cmd_list->Begin(pipeline_state))
            {
                // Update uber buffer
                m_buffer_uber_cpu.resolution = Vector2(static_cast<float>(tex_out->GetWidth()), static_cast<float>(tex_out->GetHeight()));
                UpdateUberBuffer();
        
                cmd_list->SetBufferVertex(m_quad.GetVertexBuffer());
                cmd_list->SetBufferIndex(m_quad.GetIndexBuffer());
                cmd_list->SetTexture(0, tex_in);
                cmd_list->SetTexture(1, m_render_tex_bloom.front());
                cmd_list->DrawIndexed(Rectangle::GetIndexCount());
                cmd_list->End();
                cmd_list->Submit();
            }
        }
	}

	void Renderer::Pass_ToneMapping(RHI_CommandList* cmd_list, shared_ptr<RHI_Texture>& tex_in, shared_ptr<RHI_Texture>& tex_out)
	{
		// Acquire shaders
		const auto& shader_p = m_shaders[Shader_ToneMapping_P];
        const auto& shader_v = m_shaders[Shader_Quad_V];
        if (!shader_p->IsCompiled() || !shader_v->IsCompiled())
            return;

        // Set render state
        static RHI_PipelineState pipeline_state;
        pipeline_state.shader_vertex                    = shader_v.get();
        pipeline_state.shader_pixel                     = shader_p.get();
        pipeline_state.rasterizer_state                 = m_rasterizer_cull_back_solid.get();
        pipeline_state.blend_state                      = m_blend_disabled.get();
        pipeline_state.depth_stencil_state              = m_depth_stencil_disabled.get();
        pipeline_state.vertex_buffer_stride             = m_quad.GetVertexBuffer()->GetStride();
        pipeline_state.render_target_color_textures[0]  = tex_out.get();
        pipeline_state.primitive_topology               = RHI_PrimitiveTopology_TriangleList;
        pipeline_state.viewport                         = tex_out->GetViewport();
        pipeline_state.pass_name                        = "Pass_ToneMapping";

        // Submit command list
        if (cmd_list->Begin(pipeline_state))
        {
            // Update uber buffer
            m_buffer_uber_cpu.resolution = Vector2(static_cast<float>(tex_out->GetWidth()), static_cast<float>(tex_out->GetHeight()));
            UpdateUberBuffer();

            cmd_list->SetBufferVertex(m_quad.GetVertexBuffer());
            cmd_list->SetBufferIndex(m_quad.GetIndexBuffer());
            cmd_list->SetTexture(0, tex_in);
            cmd_list->DrawIndexed(Rectangle::GetIndexCount());
            cmd_list->End();
            cmd_list->Submit();
        }
	}

	void Renderer::Pass_GammaCorrection(RHI_CommandList* cmd_list, shared_ptr<RHI_Texture>& tex_in, shared_ptr<RHI_Texture>& tex_out)
	{
		// Acquire shaders
        const auto& shader_v = m_shaders[Shader_Quad_V];
		const auto& shader_p = m_shaders[Shader_GammaCorrection_P];
		if (!shader_p->IsCompiled() || !shader_v->IsCompiled())
			return;

        // Set render state
        static RHI_PipelineState pipeline_state;
        pipeline_state.shader_vertex                    = shader_v.get();
        pipeline_state.shader_pixel                     = shader_p.get();
        pipeline_state.rasterizer_state                 = m_rasterizer_cull_back_solid.get();
        pipeline_state.blend_state                      = m_blend_disabled.get();
        pipeline_state.depth_stencil_state              = m_depth_stencil_disabled.get();
        pipeline_state.vertex_buffer_stride             = m_quad.GetVertexBuffer()->GetStride();
        pipeline_state.render_target_color_textures[0]  = tex_out.get();
        pipeline_state.primitive_topology               = RHI_PrimitiveTopology_TriangleList;
        pipeline_state.viewport                         = tex_out->GetViewport();
        pipeline_state.pass_name                        = "Pass_GammaCorrection";

        // Submit command list
        if (cmd_list->Begin(pipeline_state))
        {
            // Update uber buffer
            m_buffer_uber_cpu.resolution = Vector2(static_cast<float>(tex_out->GetWidth()), static_cast<float>(tex_out->GetHeight()));
            UpdateUberBuffer();

            cmd_list->SetBufferVertex(m_quad.GetVertexBuffer());
            cmd_list->SetBufferIndex(m_quad.GetIndexBuffer());
            cmd_list->SetTexture(0, tex_in);
            cmd_list->DrawIndexed(Rectangle::GetIndexCount());
            cmd_list->End();
            cmd_list->Submit();
        }
	}

	void Renderer::Pass_FXAA(RHI_CommandList* cmd_list, shared_ptr<RHI_Texture>& tex_in, shared_ptr<RHI_Texture>& tex_out)
	{
		// Acquire shaders
        const auto& shader_v        = m_shaders[Shader_Quad_V];
		const auto& shader_p_luma   = m_shaders[Shader_Luma_P];
		const auto& shader_p_fxaa   = m_shaders[Shader_Fxaa_P];
		if (!shader_v->IsCompiled() || !shader_p_luma->IsCompiled() || !shader_p_fxaa->IsCompiled())
			return;

        // Update uber buffer
        m_buffer_uber_cpu.resolution = Vector2(static_cast<float>(tex_out->GetWidth()), static_cast<float>(tex_out->GetHeight()));
        UpdateUberBuffer();

        // Luminance
        {
            // Set render state
            static RHI_PipelineState pipeline_state;
            pipeline_state.shader_vertex                    = shader_v.get();
            pipeline_state.shader_pixel                     = shader_p_luma.get();
            pipeline_state.rasterizer_state                 = m_rasterizer_cull_back_solid.get();
            pipeline_state.blend_state                      = m_blend_disabled.get();
            pipeline_state.depth_stencil_state              = m_depth_stencil_disabled.get();
            pipeline_state.vertex_buffer_stride             = m_quad.GetVertexBuffer()->GetStride();
            pipeline_state.render_target_color_textures[0]  = tex_out.get();
            pipeline_state.viewport                         = tex_out->GetViewport();
            pipeline_state.primitive_topology               = RHI_PrimitiveTopology_TriangleList;
            pipeline_state.pass_name                        = "Pass_FXAA_Luminance";

            // Submit command list
            if (cmd_list->Begin(pipeline_state))
            {
                cmd_list->SetBufferVertex(m_quad.GetVertexBuffer());
                cmd_list->SetBufferIndex(m_quad.GetIndexBuffer());
                cmd_list->SetTexture(0, tex_in);
                cmd_list->DrawIndexed(Rectangle::GetIndexCount());
                cmd_list->End();
                cmd_list->Submit();
            }
        }

        // FXAA
        {
            // Set render state
            static RHI_PipelineState pipeline_state;
            pipeline_state.shader_vertex                    = shader_v.get();
            pipeline_state.shader_pixel                     = shader_p_fxaa.get();
            pipeline_state.rasterizer_state                 = m_rasterizer_cull_back_solid.get();
            pipeline_state.blend_state                      = m_blend_disabled.get();
            pipeline_state.depth_stencil_state              = m_depth_stencil_disabled.get();
            pipeline_state.vertex_buffer_stride             = m_quad.GetVertexBuffer()->GetStride();
            pipeline_state.render_target_color_textures[0]  = tex_in.get();
            pipeline_state.viewport                         = tex_in->GetViewport();
            pipeline_state.primitive_topology               = RHI_PrimitiveTopology_TriangleList;
            pipeline_state.pass_name                        = "Pass_FXAA_FXAA";

            if (cmd_list->Begin(pipeline_state))
            {
                cmd_list->SetBufferVertex(m_quad.GetVertexBuffer());
                cmd_list->SetBufferIndex(m_quad.GetIndexBuffer());
                cmd_list->SetTexture(0, tex_out);
                cmd_list->DrawIndexed(Rectangle::GetIndexCount());
                cmd_list->End();
                cmd_list->Submit();
            }
        }

        // Swap the textures
        tex_in.swap(tex_out);
	}

	void Renderer::Pass_ChromaticAberration(RHI_CommandList* cmd_list, shared_ptr<RHI_Texture>& tex_in, shared_ptr<RHI_Texture>& tex_out)
	{
		// Acquire shaders
        const auto& shader_v = m_shaders[Shader_Quad_V];
		const auto& shader_p = m_shaders[Shader_ChromaticAberration_P];
		if (!shader_p->IsCompiled())
			return;

        // Set render state
        static RHI_PipelineState pipeline_state;
        pipeline_state.shader_vertex                    = shader_v.get();
        pipeline_state.shader_pixel                     = shader_p.get();
        pipeline_state.rasterizer_state                 = m_rasterizer_cull_back_solid.get();
        pipeline_state.blend_state                      = m_blend_disabled.get();
        pipeline_state.depth_stencil_state              = m_depth_stencil_disabled.get();
        pipeline_state.vertex_buffer_stride             = m_quad.GetVertexBuffer()->GetStride();
        pipeline_state.render_target_color_textures[0]  = tex_out.get();
        pipeline_state.primitive_topology               = RHI_PrimitiveTopology_TriangleList;
        pipeline_state.viewport                         = tex_out->GetViewport();
        pipeline_state.pass_name                        = "Pass_ChromaticAberration";

        // Submit command list
        if (cmd_list->Begin(pipeline_state))
        {
            // Update uber buffer
            m_buffer_uber_cpu.resolution = Vector2(static_cast<float>(tex_out->GetWidth()), static_cast<float>(tex_out->GetHeight()));
            UpdateUberBuffer();

            cmd_list->SetBufferVertex(m_quad.GetVertexBuffer());
            cmd_list->SetBufferIndex(m_quad.GetIndexBuffer());
            cmd_list->SetTexture(0, tex_in);
            cmd_list->DrawIndexed(Rectangle::GetIndexCount());
            cmd_list->End();
            cmd_list->Submit();
        }
	}

	void Renderer::Pass_MotionBlur(RHI_CommandList* cmd_list, shared_ptr<RHI_Texture>& tex_in, shared_ptr<RHI_Texture>& tex_out)
	{
		// Acquire shaders
        const auto& shader_v = m_shaders[Shader_Quad_V];
		const auto& shader_p = m_shaders[Shader_MotionBlur_P];
		if (!shader_p->IsCompiled())
			return;

        // Set render state
        static RHI_PipelineState pipeline_state;
        pipeline_state.shader_vertex                    = shader_v.get();
        pipeline_state.shader_pixel                     = shader_p.get();
        pipeline_state.rasterizer_state                 = m_rasterizer_cull_back_solid.get();
        pipeline_state.blend_state                      = m_blend_disabled.get();
        pipeline_state.depth_stencil_state              = m_depth_stencil_disabled.get();
        pipeline_state.vertex_buffer_stride             = m_quad.GetVertexBuffer()->GetStride();
        pipeline_state.render_target_color_textures[0]  = tex_out.get();
        pipeline_state.primitive_topology               = RHI_PrimitiveTopology_TriangleList;
        pipeline_state.viewport                         = tex_out->GetViewport();
        pipeline_state.pass_name                        = "Pass_MotionBlur";

        // Submit command list
        if (cmd_list->Begin(pipeline_state))
        {
            // Update uber buffer
            m_buffer_uber_cpu.resolution = Vector2(static_cast<float>(tex_out->GetWidth()), static_cast<float>(tex_out->GetHeight()));
            UpdateUberBuffer();

            cmd_list->SetBufferVertex(m_quad.GetVertexBuffer());
            cmd_list->SetBufferIndex(m_quad.GetIndexBuffer());
            cmd_list->SetTexture(0, tex_in);
            cmd_list->SetTexture(1, m_render_targets[RenderTarget_Gbuffer_Velocity]);
            cmd_list->SetTexture(2, m_render_targets[RenderTarget_Gbuffer_Depth]);
            cmd_list->DrawIndexed(Rectangle::GetIndexCount());
            cmd_list->End();
            cmd_list->Submit();
        }
	}

	void Renderer::Pass_Dithering(RHI_CommandList* cmd_list, shared_ptr<RHI_Texture>& tex_in, shared_ptr<RHI_Texture>& tex_out)
	{
		// Acquire shaders
        const auto& shader_v = m_shaders[Shader_Quad_V];
		const auto& shader_p = m_shaders[Shader_Dithering_P];
        if (!shader_p->IsCompiled() || !shader_v->IsCompiled())
            return;

        // Set render state
        static RHI_PipelineState pipeline_state;
        pipeline_state.shader_vertex                    = shader_v.get();
        pipeline_state.shader_pixel                     = shader_p.get();
        pipeline_state.rasterizer_state                 = m_rasterizer_cull_back_solid.get();
        pipeline_state.blend_state                      = m_blend_disabled.get();
        pipeline_state.depth_stencil_state              = m_depth_stencil_disabled.get();
        pipeline_state.vertex_buffer_stride             = m_quad.GetVertexBuffer()->GetStride();
        pipeline_state.render_target_color_textures[0]  = tex_out.get();
        pipeline_state.primitive_topology               = RHI_PrimitiveTopology_TriangleList;
        pipeline_state.viewport                         = tex_out->GetViewport();
        pipeline_state.pass_name                        = "Pass_Dithering";

        // Submit command list
        if (cmd_list->Begin(pipeline_state))
        {
            // Update uber buffer
            m_buffer_uber_cpu.resolution = Vector2(static_cast<float>(tex_out->GetWidth()), static_cast<float>(tex_out->GetHeight()));
            UpdateUberBuffer();

            cmd_list->SetBufferVertex(m_quad.GetVertexBuffer());
            cmd_list->SetBufferIndex(m_quad.GetIndexBuffer());
            cmd_list->SetTexture(0, tex_in);
            cmd_list->DrawIndexed(Rectangle::GetIndexCount());
            cmd_list->End();
            cmd_list->Submit();
        }
	}

	void Renderer::Pass_LumaSharpen(RHI_CommandList* cmd_list, shared_ptr<RHI_Texture>& tex_in, shared_ptr<RHI_Texture>& tex_out)
	{
		// Acquire shaders
        const auto& shader_v = m_shaders[Shader_Quad_V];
		const auto& shader_p = m_shaders[Shader_Sharpen_Luma_P];
        if (!shader_p->IsCompiled() || !shader_v->IsCompiled())
            return;

        // Set render state
        static RHI_PipelineState pipeline_state;
        pipeline_state.shader_vertex                    = shader_v.get();
        pipeline_state.shader_pixel                     = shader_p.get();
        pipeline_state.rasterizer_state                 = m_rasterizer_cull_back_solid.get();
        pipeline_state.blend_state                      = m_blend_disabled.get();
        pipeline_state.depth_stencil_state              = m_depth_stencil_disabled.get();
        pipeline_state.vertex_buffer_stride             = m_quad.GetVertexBuffer()->GetStride();
        pipeline_state.render_target_color_textures[0]  = tex_out.get();    
        pipeline_state.viewport                         = tex_out->GetViewport();
        pipeline_state.primitive_topology               = RHI_PrimitiveTopology_TriangleList;
        pipeline_state.pass_name                        = "Pass_LumaSharpen";

        // Submit command list
        if (cmd_list->Begin(pipeline_state))
        {
            // Update uber buffer
            m_buffer_uber_cpu.resolution = Vector2(static_cast<float>(tex_out->GetWidth()), static_cast<float>(tex_out->GetHeight()));
            UpdateUberBuffer();

            cmd_list->SetBufferVertex(m_quad.GetVertexBuffer());
            cmd_list->SetBufferIndex(m_quad.GetIndexBuffer());
            cmd_list->SetTexture(0, tex_in);
            cmd_list->DrawIndexed(Rectangle::GetIndexCount());
            cmd_list->End();
            cmd_list->Submit();
        }
	}

	void Renderer::Pass_Lines(RHI_CommandList* cmd_list, shared_ptr<RHI_Texture>& tex_out)
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
                        Vector3 start = light->GetTransform()->GetPosition();
                        Vector3 end = light->GetTransform()->GetForward() * light->GetRange();
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

        // Draw lines with depth
        {
            // Grid
            if (draw_grid)
            {
                // Set render state
                static RHI_PipelineState pipeline_state;
                pipeline_state.shader_vertex                    = shader_color_v.get();
                pipeline_state.shader_pixel                     = shader_color_p.get();
                pipeline_state.rasterizer_state                 = m_rasterizer_cull_back_wireframe.get();
                pipeline_state.blend_state                      = m_blend_enabled.get();
                pipeline_state.depth_stencil_state              = m_depth_stencil_enabled_no_write.get();
                pipeline_state.vertex_buffer_stride             = m_gizmo_grid->GetVertexBuffer()->GetStride();
                pipeline_state.render_target_color_textures[0]  = tex_out.get();
                pipeline_state.render_target_depth_texture      = m_render_targets[RenderTarget_Gbuffer_Depth].get();
                pipeline_state.viewport                         = tex_out->GetViewport();
                pipeline_state.primitive_topology               = RHI_PrimitiveTopology_LineList;
                pipeline_state.pass_name                        = "Pass_Lines_Grid";

                // Create and submit command list
                if (cmd_list->Begin(pipeline_state))
                {
                    // Update uber buffer
                    m_buffer_uber_cpu.resolution    = m_resolution;
                    m_buffer_uber_cpu.transform     = m_gizmo_grid->ComputeWorldMatrix(m_camera->GetTransform()) * m_buffer_frame_cpu.view_projection_unjittered;
                    UpdateUberBuffer();

                    cmd_list->SetBufferIndex(m_gizmo_grid->GetIndexBuffer());
                    cmd_list->SetBufferVertex(m_gizmo_grid->GetVertexBuffer());
                    cmd_list->DrawIndexed(m_gizmo_grid->GetIndexCount());
                    cmd_list->End();
                    cmd_list->Submit();
                }
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
                m_lines_list_depth_enabled.clear();

                // Set render state
                static RHI_PipelineState pipeline_state;
                pipeline_state.shader_vertex                    = shader_color_v.get();
                pipeline_state.shader_pixel                     = shader_color_p.get();
                pipeline_state.rasterizer_state                 = m_rasterizer_cull_back_wireframe.get();
                pipeline_state.blend_state                      = m_blend_enabled.get();
                pipeline_state.depth_stencil_state              = m_depth_stencil_enabled_no_write.get();
                pipeline_state.vertex_buffer_stride             = m_vertex_buffer_lines->GetStride();
                pipeline_state.render_target_color_textures[0]  = tex_out.get();
                pipeline_state.render_target_depth_texture      = m_render_targets[RenderTarget_Gbuffer_Depth].get();
                pipeline_state.viewport                         = tex_out->GetViewport();
                pipeline_state.primitive_topology               = RHI_PrimitiveTopology_LineList;
                pipeline_state.pass_name                        = "Pass_Lines";

                // Create and submit command list
                if (cmd_list->Begin(pipeline_state))
                {
                    cmd_list->SetBufferVertex(m_vertex_buffer_lines);
                    cmd_list->Draw(line_vertex_buffer_size);
                    cmd_list->End();
                    cmd_list->Submit();
                }
            }
        }

        // Draw lines without depth
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
            m_lines_list_depth_disabled.clear();

            // Set render state
            static RHI_PipelineState pipeline_state;
            pipeline_state.shader_vertex                    = shader_color_v.get();
            pipeline_state.shader_pixel                     = shader_color_p.get();
            pipeline_state.rasterizer_state                 = m_rasterizer_cull_back_wireframe.get();
            pipeline_state.blend_state                      = m_blend_disabled.get();
            pipeline_state.depth_stencil_state              = m_depth_stencil_disabled.get();
            pipeline_state.vertex_buffer_stride             = m_vertex_buffer_lines->GetStride();
            pipeline_state.render_target_color_textures[0]  = tex_out.get();
            pipeline_state.viewport                         = tex_out->GetViewport();
            pipeline_state.primitive_topology               = RHI_PrimitiveTopology_LineList;
            pipeline_state.pass_name                        = "Pass_Lines_No_Depth";

            // Create and submit command list
            if (cmd_list->Begin(pipeline_state))
            {
                cmd_list->SetBufferVertex(m_vertex_buffer_lines);
                cmd_list->Draw(line_vertex_buffer_size);
                cmd_list->End();
                cmd_list->Submit();
            }
        }
	}

	void Renderer::Pass_Gizmos(RHI_CommandList* cmd_list, shared_ptr<RHI_Texture>& tex_out)
	{
        // Early exit cases
		bool render_lights		                = m_options & Render_Debug_Lights;
		bool render_transform	                = m_options & Render_Debug_Transform;
        bool render				                = render_lights || render_transform;
		const auto& shader_quad_v               = m_shaders[Shader_Quad_V];
        const auto& shader_texture_p            = m_shaders[Shader_Texture_P];
        auto const& shader_gizmo_transform_v    = m_shaders[Shader_GizmoTransform_V];
        auto const& shader_gizmo_transform_p    = m_shaders[Shader_GizmoTransform_P];
		if (!render || !shader_quad_v->IsCompiled() || !shader_texture_p->IsCompiled() || !shader_gizmo_transform_v->IsCompiled() || !shader_gizmo_transform_p->IsCompiled())
			return;

        auto& lights = m_entities[Renderer_Object_Light];
        if (render_lights && !lights.empty())
        {
            // Set render state
            static RHI_PipelineState pipeline_state;
            pipeline_state.shader_vertex                    = shader_quad_v.get();
            pipeline_state.shader_pixel                     = shader_texture_p.get();
            pipeline_state.rasterizer_state                 = m_rasterizer_cull_back_solid.get();
            pipeline_state.blend_state                      = m_blend_enabled.get();
            pipeline_state.depth_stencil_state              = m_depth_stencil_disabled.get();
            pipeline_state.vertex_buffer_stride             = m_quad.GetVertexBuffer()->GetStride(); // stride matches rect
            pipeline_state.render_target_color_textures[0]  = tex_out.get();
            pipeline_state.primitive_topology               = RHI_PrimitiveTopology_TriangleList;
            pipeline_state.viewport                         = tex_out->GetViewport();
            pipeline_state.pass_name                        = "Pass_Gizmos_Lights";

            // For each light
        	for (const auto& entity : lights)
        	{
                if (cmd_list->Begin(pipeline_state))
                {
                    // Light can be null if it just got removed and our buffer doesn't update till the next frame
                    if (Light* light = entity->GetComponent<Light>().get())
                    {
                        auto position_light_world       = entity->GetTransform_PtrRaw()->GetPosition();
                        auto position_camera_world      = m_camera->GetTransform()->GetPosition();
                        auto direction_camera_to_light  = (position_light_world - position_camera_world).Normalized();
                        auto v_dot_l                    = Vector3::Dot(m_camera->GetTransform()->GetForward(), direction_camera_to_light);
        
                        // Only draw if it's inside our view
                        if (v_dot_l > 0.5f)
                        {
                            // Compute light screen space position and scale (based on distance from the camera)
                            auto position_light_screen = m_camera->WorldToScreenPoint(position_light_world);
                            auto distance = (position_camera_world - position_light_world).Length() + M_EPSILON;
                            auto scale = m_gizmo_size_max / distance;
                            scale = Clamp(scale, m_gizmo_size_min, m_gizmo_size_max);
        
                            // Choose texture based on light type
                            shared_ptr<RHI_Texture> light_tex = nullptr;
                            auto type = light->GetLightType();
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
        
                            // Update uber buffer
                            m_buffer_uber_cpu.resolution = Vector2(static_cast<float>(tex_width), static_cast<float>(tex_width));
                            m_buffer_uber_cpu.transform = m_buffer_frame_cpu.view_projection_ortho;
                            UpdateUberBuffer();
        
                            cmd_list->SetTexture(0, light_tex);
                            cmd_list->SetBufferIndex(m_gizmo_light_rect.GetIndexBuffer());
                            cmd_list->SetBufferVertex(m_gizmo_light_rect.GetVertexBuffer());
                            cmd_list->DrawIndexed(Rectangle::GetIndexCount());
                        }
                    }
                    cmd_list->End();
                    cmd_list->Submit();
                }
        	}
        }
        
        // Transform
        if (render_transform && m_gizmo_transform->Update(m_camera.get(), m_gizmo_transform_size, m_gizmo_transform_speed))
        {
            // Set render state
            static RHI_PipelineState pipeline_state;
            pipeline_state.shader_vertex                    = shader_gizmo_transform_v.get();
            pipeline_state.shader_pixel                     = shader_gizmo_transform_p.get();
            pipeline_state.rasterizer_state                 = m_rasterizer_cull_back_solid.get();
            pipeline_state.blend_state                      = m_blend_enabled.get();
            pipeline_state.depth_stencil_state              = m_depth_stencil_disabled.get();
            pipeline_state.vertex_buffer_stride             = m_gizmo_transform->GetVertexBuffer()->GetStride();
            pipeline_state.render_target_color_textures[0]  = tex_out.get();
            pipeline_state.primitive_topology               = RHI_PrimitiveTopology_TriangleList;
            pipeline_state.viewport                         = tex_out->GetViewport();

            // Axis - X
            pipeline_state.pass_name = "Pass_Gizmos_Axis_X";
            if (cmd_list->Begin(pipeline_state))
            {
                m_buffer_uber_cpu.transform = m_gizmo_transform->GetHandle().GetTransform(Vector3::Right);
                m_buffer_uber_cpu.transform_axis = m_gizmo_transform->GetHandle().GetColor(Vector3::Right);
                UpdateUberBuffer();
            
                cmd_list->SetBufferIndex(m_gizmo_transform->GetIndexBuffer());
                cmd_list->SetBufferVertex(m_gizmo_transform->GetVertexBuffer());
                cmd_list->DrawIndexed(m_gizmo_transform->GetIndexCount());
                cmd_list->End();
                cmd_list->Submit();
            }
            
            // Axis - Y
            pipeline_state.pass_name = "Pass_Gizmos_Axis_Y";
            if (cmd_list->Begin(pipeline_state))
            {
                m_buffer_uber_cpu.transform         = m_gizmo_transform->GetHandle().GetTransform(Vector3::Up);
                m_buffer_uber_cpu.transform_axis    = m_gizmo_transform->GetHandle().GetColor(Vector3::Up);
                UpdateUberBuffer();

                cmd_list->SetBufferIndex(m_gizmo_transform->GetIndexBuffer());
                cmd_list->SetBufferVertex(m_gizmo_transform->GetVertexBuffer());
                cmd_list->DrawIndexed(m_gizmo_transform->GetIndexCount());
                cmd_list->End();
                cmd_list->Submit();
            }
            
            // Axis - Z
            pipeline_state.pass_name = "Pass_Gizmos_Axis_Z";
            if (cmd_list->Begin(pipeline_state))
            {
                m_buffer_uber_cpu.transform = m_gizmo_transform->GetHandle().GetTransform(Vector3::Forward);
                m_buffer_uber_cpu.transform_axis = m_gizmo_transform->GetHandle().GetColor(Vector3::Forward);
                UpdateUberBuffer();

                cmd_list->SetBufferIndex(m_gizmo_transform->GetIndexBuffer());
                cmd_list->SetBufferVertex(m_gizmo_transform->GetVertexBuffer());
                cmd_list->DrawIndexed(m_gizmo_transform->GetIndexCount());
                cmd_list->End();
                cmd_list->Submit();
            }
            
            // Axes - XYZ
            if (m_gizmo_transform->DrawXYZ())
            {
                pipeline_state.pass_name = "Pass_Gizmos_Axis_XYZ";
                if (cmd_list->Begin(pipeline_state))
                {
                    m_buffer_uber_cpu.transform = m_gizmo_transform->GetHandle().GetTransform(Vector3::One);
                    m_buffer_uber_cpu.transform_axis = m_gizmo_transform->GetHandle().GetColor(Vector3::One);
                    UpdateUberBuffer();

                    cmd_list->SetBufferIndex(m_gizmo_transform->GetIndexBuffer());
                    cmd_list->SetBufferVertex(m_gizmo_transform->GetVertexBuffer());
                    cmd_list->DrawIndexed(m_gizmo_transform->GetIndexCount());
                    cmd_list->End();
                    cmd_list->Submit();
                }
            }
        }
	}

	void Renderer::Pass_PerformanceMetrics(RHI_CommandList* cmd_list, shared_ptr<RHI_Texture>& tex_out)
	{
        // Early exit cases
        const bool draw         = m_options & Render_Debug_PerformanceMetrics;
        const bool empty        = m_profiler->GetMetrics().empty();
        const auto& shader_v    = m_shaders[Shader_Font_V];
        const auto& shader_p    = m_shaders[Shader_Font_P];
        if (!draw || empty || !shader_v->IsCompiled() || !shader_p->IsCompiled())
            return;

        // Set render state
        static RHI_PipelineState pipeline_state;
        pipeline_state.shader_vertex                    = shader_v.get();
        pipeline_state.shader_pixel                     = shader_p.get();
        pipeline_state.rasterizer_state                 = m_rasterizer_cull_back_solid.get();
        pipeline_state.blend_state                      = m_blend_enabled.get();
        pipeline_state.depth_stencil_state              = m_depth_stencil_disabled.get();
        pipeline_state.vertex_buffer_stride             = m_font->GetVertexBuffer()->GetStride();
        pipeline_state.render_target_color_textures[0]  = tex_out.get();
        pipeline_state.primitive_topology               = RHI_PrimitiveTopology_TriangleList;
        pipeline_state.viewport                         = tex_out->GetViewport();
        pipeline_state.pass_name                        = "Pass_PerformanceMetrics";

        // Submit command list
        if (cmd_list->Begin(pipeline_state))
        {
            // Update text
            const auto text_pos = Vector2(-static_cast<int>(m_viewport.width) * 0.5f + 1.0f, static_cast<int>(m_viewport.height) * 0.5f);
            m_font->SetText(m_profiler->GetMetrics(), text_pos);

            // Update uber buffer
            m_buffer_uber_cpu.resolution    = Vector2(static_cast<float>(tex_out->GetWidth()), static_cast<float>(tex_out->GetHeight()));
            m_buffer_uber_cpu.color         = m_font->GetColor();
            UpdateUberBuffer();

            cmd_list->SetTexture(0, m_font->GetAtlas());
            cmd_list->SetBufferIndex(m_font->GetIndexBuffer());
            cmd_list->SetBufferVertex(m_font->GetVertexBuffer());
            cmd_list->DrawIndexed(m_font->GetIndexCount());
            cmd_list->End();
            cmd_list->Submit();
        }
	}

	bool Renderer::Pass_DebugBuffer(RHI_CommandList* cmd_list, shared_ptr<RHI_Texture>& tex_out)
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
        const auto& shader_v = m_shaders[Shader_Quad_V];
        const auto& shader_p = m_shaders[shader_type];
        if (!shader_v->IsCompiled() || !shader_p->IsCompiled())
            return false;

        // Set render state
        static RHI_PipelineState pipeline_state;
        pipeline_state.shader_vertex                    = shader_v.get();
        pipeline_state.shader_pixel                     = shader_p.get();
        pipeline_state.rasterizer_state                 = m_rasterizer_cull_back_solid.get();
        pipeline_state.blend_state                      = m_blend_disabled.get();
        pipeline_state.depth_stencil_state              = m_depth_stencil_disabled.get();
        pipeline_state.vertex_buffer_stride             = m_quad.GetVertexBuffer()->GetStride();
        pipeline_state.render_target_color_textures[0]  = tex_out.get();
        pipeline_state.primitive_topology               = RHI_PrimitiveTopology_TriangleList;
        pipeline_state.viewport                         = tex_out->GetViewport();
        pipeline_state.pass_name                        = "Pass_DebugBuffer";

        // // Submit command list
        if (cmd_list->Begin(pipeline_state))
        {
            // Update uber buffer
            m_buffer_uber_cpu.resolution    = Vector2(static_cast<float>(tex_out->GetWidth()), static_cast<float>(tex_out->GetHeight()));
            m_buffer_uber_cpu.transform     = m_buffer_frame_cpu.view_projection_ortho;
            UpdateUberBuffer();

            cmd_list->SetTexture(0, texture);
            cmd_list->SetBufferVertex(m_quad.GetVertexBuffer());
            cmd_list->SetBufferIndex(m_quad.GetIndexBuffer());
            cmd_list->DrawIndexed(Rectangle::GetIndexCount());
            cmd_list->End();
            cmd_list->Submit();
        }

		return true;
	}

    void Renderer::Pass_BrdfSpecularLut(RHI_CommandList* cmd_list)
    {
        if (m_brdf_specular_lut_rendered)
            return;

        // Acquire shaders
        const auto& shader_v = m_shaders[Shader_Quad_V];
        const auto& shader_p = m_shaders[Shader_BrdfSpecularLut];
        if (!shader_v->IsCompiled() || !shader_p->IsCompiled())
            return;

        // Acquire render target
        const auto& render_target = m_render_targets[RenderTarget_Brdf_Specular_Lut];

        // Set render state
        static RHI_PipelineState pipeline_state;
        pipeline_state.shader_vertex                    = shader_v.get();
        pipeline_state.shader_pixel                     = shader_p.get();
        pipeline_state.rasterizer_state                 = m_rasterizer_cull_back_solid.get();
        pipeline_state.blend_state                      = m_blend_disabled.get();
        pipeline_state.depth_stencil_state              = m_depth_stencil_disabled.get();
        pipeline_state.vertex_buffer_stride             = m_quad.GetVertexBuffer()->GetStride();
        pipeline_state.render_target_color_textures[0]  = render_target.get();
        pipeline_state.viewport                         = render_target->GetViewport();
        pipeline_state.primitive_topology               = RHI_PrimitiveTopology_TriangleList;
        pipeline_state.pass_name                        = "Pass_BrdfSpecularLut";

        // Submit command list
        if (cmd_list->Begin(pipeline_state))
        {
            // Update uber buffer
            m_buffer_uber_cpu.resolution = Vector2(static_cast<float>(render_target->GetWidth()), static_cast<float>(render_target->GetHeight()));
            UpdateUberBuffer();

            cmd_list->SetBufferVertex(m_quad.GetVertexBuffer());
            cmd_list->SetBufferIndex(m_quad.GetIndexBuffer());
            cmd_list->DrawIndexed(Rectangle::GetIndexCount());
            cmd_list->End();
            cmd_list->Submit();
        }

        m_brdf_specular_lut_rendered = true;
    }

    void Renderer::Pass_Copy(RHI_CommandList* cmd_list, shared_ptr<RHI_Texture>& tex_in, shared_ptr<RHI_Texture>& tex_out)
    {
        // Acquire shaders
        const auto& shader_v = m_shaders[Shader_Quad_V];
        const auto& shader_p = m_shaders[Shader_Texture_P];
        if (!shader_v->IsCompiled() || !shader_p->IsCompiled())
            return;

        // Set render state
        static RHI_PipelineState pipeline_state;
        pipeline_state.shader_vertex                    = shader_v.get();
        pipeline_state.shader_pixel                     = shader_p.get();
        pipeline_state.rasterizer_state                 = m_rasterizer_cull_back_solid.get();
        pipeline_state.blend_state                      = m_blend_disabled.get();
        pipeline_state.depth_stencil_state              = m_depth_stencil_disabled.get();
        pipeline_state.vertex_buffer_stride             = m_quad.GetVertexBuffer()->GetStride();
        pipeline_state.render_target_color_textures[0]  = tex_out.get();
        pipeline_state.primitive_topology               = RHI_PrimitiveTopology_TriangleList;
        pipeline_state.viewport                         = tex_out->GetViewport();
        pipeline_state.pass_name                        = "Pass_Copy";

        // Draw
        if (cmd_list->Begin(pipeline_state))
        {
            // Update uber buffer
            m_buffer_uber_cpu.resolution    = Vector2(static_cast<float>(tex_out->GetWidth()), static_cast<float>(tex_out->GetHeight()));
            m_buffer_uber_cpu.transform     = m_buffer_frame_cpu.view_projection_ortho;
            UpdateUberBuffer();

            cmd_list->SetTexture(0, tex_in);
            cmd_list->SetBufferVertex(m_quad.GetVertexBuffer());
            cmd_list->SetBufferIndex(m_quad.GetIndexBuffer());
            cmd_list->DrawIndexed(Rectangle::GetIndexCount());
            cmd_list->End();
            cmd_list->Submit();
        }
    }
}
