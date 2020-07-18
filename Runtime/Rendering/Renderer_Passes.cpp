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
#include "ShaderGBuffer.h"
#include "ShaderLight.h"
#include "Font/Font.h"
#include "Gizmos/Grid.h"
#include "Gizmos/Transform_Gizmo.h"
#include "../Profiling/Profiler.h"
#include "../RHI/RHI_CommandList.h"
#include "../RHI/RHI_Implementation.h"
#include "../RHI/RHI_VertexBuffer.h"
#include "../RHI/RHI_PipelineState.h"
#include "../RHI/RHI_Texture.h"
#include "../World/Entity.h"
#include "../World/Components/Light.h"
#include "../World/Components/Camera.h"
#include "../World/Components/Transform.h"
#include "../World/Components/Renderable.h"
//=========================================

//= NAMESPACES ===============
using namespace std;
using namespace Spartan::Math;
//============================

namespace Spartan
{
    void Renderer::SetGlobalSamplersAndConstantBuffers(RHI_CommandList* cmd_list) const
    {
        // Constant buffers
        cmd_list->SetConstantBuffer(0, RHI_Shader_Vertex | RHI_Shader_Pixel | RHI_Shader_Compute, m_buffer_frame_gpu);
        cmd_list->SetConstantBuffer(1, RHI_Shader_Pixel, m_buffer_material_gpu);
        cmd_list->SetConstantBuffer(2, RHI_Shader_Vertex | RHI_Shader_Pixel | RHI_Shader_Compute, m_buffer_uber_gpu);
        cmd_list->SetConstantBuffer(3, RHI_Shader_Vertex | RHI_Shader_Compute, m_buffer_object_gpu);
        cmd_list->SetConstantBuffer(4, RHI_Shader_Pixel, m_buffer_light_gpu);
        
        // Samplers
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
        
        // Runs only once
        Pass_BrdfSpecularLut(cmd_list);
        
        const bool draw_transparent_objects = !m_entities[Renderer_Object_Transparent].empty();
        
        // Depth
        {
            Pass_LightDepth(cmd_list, Renderer_Object_Opaque);
            if (draw_transparent_objects)
            {
                Pass_LightDepth(cmd_list, Renderer_Object_Transparent);
            }
        
            if (GetOption(Render_DepthPrepass))
            {
                Pass_DepthPrePass(cmd_list);
            }
        }
        
        // G-Buffer to Composition
        {
            // Lighting
            Pass_GBuffer(cmd_list, Renderer_Object_Opaque);
            Pass_Hbao(cmd_list, false);
            Pass_Ssr(cmd_list, false);
            Pass_Light(cmd_list, false);
            Pass_Composition(cmd_list, m_render_targets[RenderTarget_Hdr], false);
        
            // Lighting for transparent objects
            if (draw_transparent_objects)
            {
                Pass_GBuffer(cmd_list, Renderer_Object_Transparent);
                Pass_Hbao(cmd_list, true);
                Pass_Ssr(cmd_list, true);
                Pass_Light(cmd_list, true);
                Pass_Composition(cmd_list, m_render_targets[RenderTarget_Hdr_2], true);
        
                // Alpha blend the transparent composition on top of opaque one
                Pass_AlphaBlend(cmd_list, m_render_targets[RenderTarget_Hdr_2].get(), m_render_targets[RenderTarget_Hdr].get(), true);
            }
        }
        
        // Post-processing
        {
            Pass_PostProcess(cmd_list);
            Pass_Outline(cmd_list, m_render_targets[RenderTarget_Ldr]);
            Pass_Lines(cmd_list, m_render_targets[RenderTarget_Ldr]);
            Pass_TransformHandle(cmd_list, m_render_targets[RenderTarget_Ldr].get());
            Pass_Icons(cmd_list, m_render_targets[RenderTarget_Ldr].get());
            Pass_DebugBuffer(cmd_list, m_render_targets[RenderTarget_Ldr]);
            Pass_Text(cmd_list, m_render_targets[RenderTarget_Ldr].get());
        }
	}

	void Renderer::Pass_LightDepth(RHI_CommandList* cmd_list, const Renderer_Object_Type object_type)
	{
        // All opaque objects are rendered from the lights point of view.
        // Opaque objects write their depth information to a depth buffer, using just a vertex shader.
        // Transparent objects, read the opaque depth but don't write their own, instead, they write their color information using a pixel shader.

		// Acquire shader
		RHI_Shader* shader_v = m_shaders[Shader_Depth_V].get();
        RHI_Shader* shader_p = m_shaders[Shader_Depth_P].get();
		if (!shader_v->IsCompiled() || !shader_p->IsCompiled())
			return;

        // Get entities
        const auto& entities = m_entities[object_type];
        if (entities.empty())
            return;

        const bool transparent_pass = object_type == Renderer_Object_Transparent;

        // Go through all of the lights
		const auto& entities_light = m_entities[Renderer_Object_Light];
        for (uint32_t light_index = 0; light_index < entities_light.size(); light_index++)
        {
            const Light* light = entities_light[light_index]->GetComponent<Light>();

            // Skip some obvious cases
            if (!light || !light->GetShadowsEnabled())
                continue;

            // Skip lights that don't cast transparent shadows (if this is a transparent pass)
            if (transparent_pass && !light->GetShadowsTransparentEnabled())
                continue;

            // Acquire light's shadow maps
            RHI_Texture* tex_depth = light->GetDepthTexture();
            RHI_Texture* tex_color = light->GetColorTexture();
            if (!tex_depth)
                continue;

            // Set render state
            static RHI_PipelineState pipeline_state;
            pipeline_state.shader_vertex                    = shader_v;
            pipeline_state.vertex_buffer_stride             = static_cast<uint32_t>(sizeof(RHI_Vertex_PosTexNorTan)); // assume all vertex buffers have the same stride (which they do)
            pipeline_state.shader_pixel                     = transparent_pass ? shader_p : nullptr;
            pipeline_state.blend_state                      = transparent_pass ? m_blend_alpha.get() : m_blend_disabled.get();
            pipeline_state.depth_stencil_state              = transparent_pass ? m_depth_stencil_on_off_r.get() : m_depth_stencil_on_off_w.get();
            pipeline_state.render_target_color_textures[0]  = tex_color; // always bind so we can clear to white (in case there are now transparent objects)
            pipeline_state.render_target_depth_texture      = tex_depth;
            pipeline_state.clear_stencil                    = state_stencil_dont_care;
            pipeline_state.viewport                         = tex_depth->GetViewport();
            pipeline_state.primitive_topology               = RHI_PrimitiveTopology_TriangleList;
            pipeline_state.pass_name                        = transparent_pass ? "Pass_LightDepthTransparent" : "Pass_LightDepth";

            for (uint32_t array_index = 0; array_index < tex_depth->GetArraySize(); array_index++)
            {
                // Set render target texture array index
                pipeline_state.render_target_color_texture_array_index          = array_index;
                pipeline_state.render_target_depth_stencil_texture_array_index  = array_index;

                // Set clear values
                pipeline_state.clear_color[0] = Vector4::One;
                pipeline_state.clear_depth    = transparent_pass ? state_depth_load : GetClearDepth();

                const Matrix& view_projection = light->GetViewMatrix(array_index) * light->GetProjectionMatrix(array_index);

                // Set appropriate rasterizer state
                if (light->GetLightType() == LightType::Directional)
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

                // State tracking
                bool render_pass_active     = false;
                uint32_t m_set_material_id  = 0;

                for (uint32_t entity_index = 0; entity_index < static_cast<uint32_t>(entities.size()); entity_index++)
                {
                    Entity* entity = entities[entity_index];

                    // Acquire renderable component
                    const auto& renderable = entity->GetRenderable();
                    if (!renderable)
                        continue;

                    // Skip meshes that don't cast shadows
                    if (!renderable->GetCastShadows())
                        continue;

                    // Acquire geometry
                    const auto& model = renderable->GeometryModel();
                    if (!model || !model->GetVertexBuffer() || !model->GetIndexBuffer())
                        continue;

                    // Acquire material
                    const auto& material = renderable->GetMaterial();
                    if (!material)
                        continue;

                    // Skip objects outside of the view frustum
                    if (!light->IsInViewFrustrum(renderable, array_index))
                        continue;

                    if (!render_pass_active)
                    {
                        render_pass_active = cmd_list->BeginRenderPass(pipeline_state);
                    }

                    // Bind material
                    if (transparent_pass && m_set_material_id != material->GetId())
                    {
                        // Bind material textures
                        RHI_Texture* tex_albedo = material->GetTexture_Ptr(Material_Color);
                        cmd_list->SetTexture(28, tex_albedo ? tex_albedo : m_tex_white.get());

                        // Update uber buffer with material properties
                        m_buffer_uber_cpu.mat_albedo    = material->GetColorAlbedo();
                        m_buffer_uber_cpu.mat_tiling_uv = material->GetTiling();
                        m_buffer_uber_cpu.mat_offset_uv = material->GetOffset();

                        // Update constant buffer
                        UpdateUberBuffer(cmd_list);

                        m_set_material_id = material->GetId();
                    }

                    // Bind geometry
                    cmd_list->SetBufferIndex(model->GetIndexBuffer());
                    cmd_list->SetBufferVertex(model->GetVertexBuffer());

                    // Update uber buffer with cascade transform
                    m_buffer_object_cpu.object = entity->GetTransform()->GetMatrix() * view_projection;
                    if (!UpdateObjectBuffer(cmd_list))
                        continue;

                    cmd_list->DrawIndexed(renderable->GeometryIndexCount(), renderable->GeometryIndexOffset(), renderable->GeometryVertexOffset());

                }

                if (render_pass_active)
                {
                    cmd_list->EndRenderPass();
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
        pipeline_state.depth_stencil_state          = m_depth_stencil_on_off_w.get();
        pipeline_state.render_target_depth_texture  = tex_depth.get();
        pipeline_state.clear_depth                  = GetClearDepth();
        pipeline_state.viewport                     = tex_depth->GetViewport();
        pipeline_state.primitive_topology           = RHI_PrimitiveTopology_TriangleList;
        pipeline_state.pass_name                    = "Pass_DepthPrePass";

        // Record commands
        if (cmd_list->BeginRenderPass(pipeline_state))
        { 
            if (!entities.empty())
            {
                // Variables that help reduce state changes
                uint32_t currently_bound_geometry = 0;

                // Draw opaque
                for (const auto& entity : entities)
                {
                    // Get renderable
                    const auto& renderable = entity->GetRenderable();
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
                    if (Transform* transform = entity->GetTransform())
                    {
                        // Update uber buffer with cascade transform
                        m_buffer_uber_cpu.transform = transform->GetMatrix() * m_buffer_frame_cpu.view_projection;
                        UpdateUberBuffer(cmd_list);
                    }

                    // Draw	
                    cmd_list->DrawIndexed(renderable->GeometryIndexCount(), renderable->GeometryIndexOffset(), renderable->GeometryVertexOffset());
                }
            }
            cmd_list->EndRenderPass();
        }
    }

	void Renderer::Pass_GBuffer(RHI_CommandList* cmd_list, const Renderer_Object_Type object_type)
	{
        // Acquire required resources/shaders
        RHI_Texture* tex_albedo       = m_render_targets[RenderTarget_Gbuffer_Albedo].get();
        RHI_Texture* tex_normal       = m_render_targets[RenderTarget_Gbuffer_Normal].get();
        RHI_Texture* tex_material     = m_render_targets[RenderTarget_Gbuffer_Material].get();
        RHI_Texture* tex_velocity     = m_render_targets[RenderTarget_Gbuffer_Velocity].get();
        RHI_Texture* tex_depth        = m_render_targets[RenderTarget_Gbuffer_Depth].get();
        RHI_Shader* shader_v          = m_shaders[Shader_Gbuffer_V].get();
        ShaderGBuffer* shader_p       = static_cast<ShaderGBuffer*>(m_shaders[Shader_Gbuffer_P].get());

        // Validate that the shader has compiled
        if (!shader_v->IsCompiled())
            return;

        // Clear values that depend on the objects being opaque or transparent
        const bool is_transparent = object_type == Renderer_Object_Transparent;

        // Set render state
        RHI_PipelineState pso;
        pso.shader_vertex                   = shader_v;
        pso.vertex_buffer_stride            = static_cast<uint32_t>(sizeof(RHI_Vertex_PosTexNorTan)); // assume all vertex buffers have the same stride (which they do)
        pso.blend_state                     = m_blend_disabled.get();
        pso.rasterizer_state                = GetOption(Render_Debug_Wireframe) ? m_rasterizer_cull_back_wireframe.get() : m_rasterizer_cull_back_solid.get();
        pso.depth_stencil_state             = is_transparent ? m_depth_stencil_on_on_w.get() : m_depth_stencil_on_off_w.get(); // GetOptionValue(Render_DepthPrepass) is not accounted for anymore, have to fix
        pso.render_target_color_textures[0] = tex_albedo;
        pso.clear_color[0]                  = !is_transparent ? Vector4::Zero : state_color_load;
        pso.render_target_color_textures[1] = tex_normal;
        pso.clear_color[1]                  = !is_transparent ? Vector4::Zero : state_color_load;
        pso.render_target_color_textures[2] = tex_material;
        pso.clear_color[2]                  = !is_transparent ? Vector4::Zero : state_color_load;
        pso.render_target_color_textures[3] = tex_velocity;
        pso.clear_color[3]                  = !is_transparent ? Vector4::Zero : state_color_load;
        pso.render_target_depth_texture     = tex_depth;
        pso.clear_depth                     = is_transparent || GetOption(Render_DepthPrepass) ? state_depth_load : GetClearDepth();
        pso.clear_stencil                   = 0;
        pso.viewport                        = tex_albedo->GetViewport();
        pso.primitive_topology              = RHI_PrimitiveTopology_TriangleList;

        bool cleared = false;
        uint32_t material_index = 0;
        uint32_t material_bound_id = 0;
        m_material_instances.fill(nullptr);

        // Iterate through all the G-Buffer shader variations
        for (const auto& it : ShaderGBuffer::GetVariations())
        {
            // Skip the shader until it compiles or the users spots a compilation error
            if (!it.second->IsCompiled())
                continue;

            // Set pixel shader
            pso.shader_pixel = static_cast<RHI_Shader*>(it.second.get());

            // Set pass name
            pso.pass_name = pso.shader_pixel->GetName().c_str();

            bool render_pass_active = false;
            auto& entities = m_entities[object_type];

            // Record commands
            for (uint32_t i = 0; i < static_cast<uint32_t>(entities.size()); i++)
            {
                Entity* entity = entities[i];

                // Get renderable
                const auto& renderable = entity->GetRenderable();
                if (!renderable)
                    continue;

                // Get material
                Material* material = renderable->GetMaterial();
                if (!material)
                    continue;

                // Skip objects with different shader requirements
                if (!static_cast<ShaderGBuffer*>(pso.shader_pixel)->IsSuitable(material->GetFlags()))
                    continue;

                // Skip transparent objects that won't contribute
                if (material->GetColorAlbedo().w == 0 && is_transparent)
                    continue;

                // Get geometry
                const auto& model = renderable->GeometryModel();
                if (!model || !model->GetVertexBuffer() || !model->GetIndexBuffer())
                    continue;

                // Skip objects outside of the view frustum
                if (!m_camera->IsInViewFrustrum(renderable))
                    continue;

                if (!render_pass_active)
                {
                    render_pass_active = cmd_list->BeginRenderPass(pso);
                }

                // Set geometry (will only happen if not already set)
                cmd_list->SetBufferIndex(model->GetIndexBuffer());
                cmd_list->SetBufferVertex(model->GetVertexBuffer());

                // Bind material
                bool firs_run       = material_index == 0;
                bool new_material   = material_bound_id != material->GetId();
                if (firs_run || new_material)
                {
                    material_bound_id = material->GetId();

                    // Keep track of used material instances (they get mapped to shaders)
                    if (material_index + 1 < m_material_instances.size())
                    {
                        // Advance index (0 is reserved for the sky)
                        material_index++;

                        // Keep reference
                        m_material_instances[material_index] = material;
                    }
                    else
                    {
                        LOG_ERROR("Material instance array has reached it's maximum capacity of %d elements. Consider increasing the size.", m_max_material_instances);
                    }

                    // Bind material textures		
                    cmd_list->SetTexture(0, material->GetTexture_Ptr(Material_Color));
                    cmd_list->SetTexture(1, material->GetTexture_Ptr(Material_Roughness));
                    cmd_list->SetTexture(2, material->GetTexture_Ptr(Material_Metallic));
                    cmd_list->SetTexture(3, material->GetTexture_Ptr(Material_Normal));
                    cmd_list->SetTexture(4, material->GetTexture_Ptr(Material_Height));
                    cmd_list->SetTexture(5, material->GetTexture_Ptr(Material_Occlusion));
                    cmd_list->SetTexture(6, material->GetTexture_Ptr(Material_Emission));
                    cmd_list->SetTexture(7, material->GetTexture_Ptr(Material_Mask));
                
                    // Update uber buffer with material properties
                    m_buffer_uber_cpu.mat_id            = static_cast<float>(material_index);
                    m_buffer_uber_cpu.mat_albedo        = material->GetColorAlbedo();
                    m_buffer_uber_cpu.mat_tiling_uv     = material->GetTiling();
                    m_buffer_uber_cpu.mat_offset_uv     = material->GetOffset();
                    m_buffer_uber_cpu.mat_roughness_mul = material->GetProperty(Material_Roughness);
                    m_buffer_uber_cpu.mat_metallic_mul  = material->GetProperty(Material_Metallic);
                    m_buffer_uber_cpu.mat_normal_mul    = material->GetProperty(Material_Normal);
                    m_buffer_uber_cpu.mat_height_mul    = material->GetProperty(Material_Height);

                    // Update constant buffer
                    UpdateUberBuffer(cmd_list);
                }
                
                // Update uber buffer with entity transform
                if (Transform* transform = entity->GetTransform())
                {
                    m_buffer_object_cpu.object          = transform->GetMatrix();
                    m_buffer_object_cpu.wvp_current     = transform->GetMatrix() * m_buffer_frame_cpu.view_projection;
                    m_buffer_object_cpu.wvp_previous    = transform->GetWvpLastFrame();

                    // Save matrix for velocity computation
                    transform->SetWvpLastFrame(m_buffer_object_cpu.wvp_current);

                    // Update object buffer
                    if (!UpdateObjectBuffer(cmd_list))
                        continue;
                }
                
                // Render	
                cmd_list->DrawIndexed(renderable->GeometryIndexCount(), renderable->GeometryIndexOffset(), renderable->GeometryVertexOffset());
                m_profiler->m_renderer_meshes_rendered++;

                // Clear only on first pass
                if (!cleared)
                {
                    pso.ResetClearValues();
                    cleared = true;
                }
            }

            if (render_pass_active)
            {
                cmd_list->EndRenderPass();
            }
        }

        // Update constant buffer (light pass will access it using material IDs)
        UpdateMaterialBuffer();
	}

	void Renderer::Pass_Hbao(RHI_CommandList* cmd_list, const bool use_stencil)
	{
        if ((m_options & Render_Hbao) == 0)
            return;

        bool indirect_bounce = (m_options & Render_IndirectBounce) != 0;

        // Acquire shaders
        RHI_Shader* shader_v = m_shaders[Shader_Quad_V].get();
        RHI_Shader* shader_p = m_shaders[indirect_bounce ? Shader_Hbao_IndirectBounce_P : Shader_Hbao_P].get();
        if (!shader_v->IsCompiled() || !shader_p->IsCompiled())
            return;
        
        // Acquire textures
        shared_ptr<RHI_Texture>& tex_hbao_noisy     = m_render_targets[RenderTarget_Hbao_Noisy];
        shared_ptr<RHI_Texture>& tex_hbao_blurred   = m_render_targets[RenderTarget_Hbao];
        RHI_Texture* tex_depth                      = m_render_targets[RenderTarget_Gbuffer_Depth].get();
        RHI_Texture* tex_normal                     = m_render_targets[RenderTarget_Gbuffer_Normal].get();
        RHI_Texture* tex_light_diffuse              = m_render_targets[RenderTarget_Light_Diffuse].get();
        RHI_Texture* tex_light_specular             = m_render_targets[RenderTarget_Light_Specular].get();

        // Set render state
        static RHI_PipelineState pipeline_state;
        pipeline_state.shader_vertex                            = shader_v;
        pipeline_state.shader_pixel                             = shader_p;
        pipeline_state.rasterizer_state                         = m_rasterizer_cull_back_solid.get();
        pipeline_state.blend_state                              = m_blend_disabled.get();
        pipeline_state.depth_stencil_state                      = use_stencil ? m_depth_stencil_off_on_r.get() : m_depth_stencil_off_off.get();
        pipeline_state.vertex_buffer_stride                     = m_viewport_quad.GetVertexBuffer()->GetStride();
        pipeline_state.render_target_color_textures[0]          = use_stencil ? tex_hbao_blurred.get() : tex_hbao_noisy.get();
        pipeline_state.clear_color[0]                           = use_stencil ? state_color_load : state_color_dont_care;
        pipeline_state.render_target_depth_texture              = use_stencil ? tex_depth : nullptr;
        pipeline_state.clear_stencil                            = use_stencil ? state_stencil_load : state_stencil_dont_care;
        pipeline_state.render_target_depth_texture_read_only    = use_stencil;
        pipeline_state.viewport                                 = tex_hbao_noisy->GetViewport();
        pipeline_state.primitive_topology                       = RHI_PrimitiveTopology_TriangleList;
        pipeline_state.pass_name                                = "Pass_Hbao";

        // Record commands
        if (cmd_list->BeginRenderPass(pipeline_state))
        {
            // Update uber buffer
            m_buffer_uber_cpu.resolution = Vector2(tex_hbao_noisy->GetWidth(), tex_hbao_noisy->GetHeight());
            UpdateUberBuffer(cmd_list);

            cmd_list->SetBufferVertex(m_viewport_quad.GetVertexBuffer());
            cmd_list->SetBufferIndex(m_viewport_quad.GetIndexBuffer());
            cmd_list->SetTexture(9, tex_normal);
            cmd_list->SetTexture(12, tex_depth);
            cmd_list->SetTexture(21, m_tex_noise_normal);
            cmd_list->SetTexture(23, tex_light_diffuse);

            cmd_list->DrawIndexed(Rectangle::GetIndexCount());
            cmd_list->EndRenderPass();
        
            // Bilateral blur
            const auto sigma        = 2.0f;
            const auto pixel_stride = 2.0f;
            Pass_BlurBilateralGaussian(
                cmd_list,
                use_stencil ? tex_hbao_blurred : tex_hbao_noisy,
                use_stencil ? tex_hbao_noisy : tex_hbao_blurred,
                sigma,
                pixel_stride,
                use_stencil
            );
        }
	}

    void Renderer::Pass_Ssr(RHI_CommandList* cmd_list, const bool use_stencil)
    {
        if ((m_options & Render_ScreenSpaceReflections) == 0)
            return;

        // Acquire shaders
        const auto& shader_v = m_shaders[Shader_Quad_V];
        const auto& shader_p = m_shaders[Shader_Ssr_P];
        if (!shader_v->IsCompiled() || !shader_p->IsCompiled())
            return;
        
        // Acquire render targets
        auto& tex_ssr   = m_render_targets[RenderTarget_Ssr];
        auto& tex_depth = m_render_targets[RenderTarget_Gbuffer_Depth];

        // Set render state
        static RHI_PipelineState pipeline_state;
        pipeline_state.shader_vertex                            = shader_v.get();
        pipeline_state.shader_pixel                             = shader_p.get();
        pipeline_state.rasterizer_state                         = m_rasterizer_cull_back_solid.get();
        pipeline_state.blend_state                              = m_blend_disabled.get();
        pipeline_state.depth_stencil_state                      = use_stencil ? m_depth_stencil_off_on_r.get() : m_depth_stencil_off_off.get();
        pipeline_state.vertex_buffer_stride                     = m_viewport_quad.GetVertexBuffer()->GetStride();
        pipeline_state.render_target_color_textures[0]          = tex_ssr.get();
        pipeline_state.clear_color[0]                           = use_stencil ? state_color_load : state_color_dont_care;
        pipeline_state.render_target_depth_texture              = use_stencil ? tex_depth.get() : nullptr;
        pipeline_state.clear_stencil                            = use_stencil ? state_stencil_load : state_stencil_dont_care;
        pipeline_state.render_target_depth_texture_read_only    = use_stencil;
        pipeline_state.viewport                                 = tex_ssr->GetViewport();
        pipeline_state.primitive_topology                       = RHI_PrimitiveTopology_TriangleList;
        pipeline_state.pass_name                                = "Pass_Ssr";

        // Record commands
        if (cmd_list->BeginRenderPass(pipeline_state))
        {
            // Update uber buffer
            m_buffer_uber_cpu.resolution = Vector2(tex_ssr->GetWidth(), tex_ssr->GetHeight());
            UpdateUberBuffer(cmd_list);
        
            cmd_list->SetBufferVertex(m_viewport_quad.GetVertexBuffer());
            cmd_list->SetBufferIndex(m_viewport_quad.GetIndexBuffer());
            cmd_list->SetTexture(9, m_render_targets[RenderTarget_Gbuffer_Normal]);
            cmd_list->SetTexture(12, m_render_targets[RenderTarget_Gbuffer_Depth]);
            cmd_list->DrawIndexed(Rectangle::GetIndexCount());
            cmd_list->EndRenderPass();
        }
    }

    void Renderer::Pass_Light(RHI_CommandList* cmd_list, const bool use_stencil)
    {
        // Acquire lights
        const vector<Entity*>& entities = m_entities[Renderer_Object_Light];
        if (entities.empty())
            return;

        // Acquire shaders
        RHI_Shader* shader_v    = m_shaders[Shader_Quad_V].get();
        ShaderLight* shader_p   = static_cast<ShaderLight*>(m_shaders[Shader_Light_P].get());
        if (!shader_v->IsCompiled())
            return;

        // Acquire render targets
        RHI_Texture* tex_diffuse       = m_render_targets[RenderTarget_Light_Diffuse].get();
        RHI_Texture* tex_specular      = m_render_targets[RenderTarget_Light_Specular].get();
        RHI_Texture* tex_volumetric    = m_render_targets[RenderTarget_Light_Volumetric].get();
        RHI_Texture* tex_depth         = m_render_targets[RenderTarget_Gbuffer_Depth].get();

        // Update uber buffer
        m_buffer_uber_cpu.resolution = Vector2(static_cast<float>(tex_diffuse->GetWidth()), static_cast<float>(tex_diffuse->GetHeight()));
        UpdateUberBuffer(cmd_list);

        // Diffuse and specular need to not be loaded as they willbe used for ssgi. Otherwise having a single transparent object to render (use_stencil == true) will cause the light to be discarded.
        bool indirect_bounce = (m_options & Render_IndirectBounce) != 0;
        Math::Vector4 clear_color = use_stencil ? (indirect_bounce ? state_color_load : state_color_dont_care) : Vector4::Zero;

         // Set render state
        static RHI_PipelineState pipeline_state;
        pipeline_state.shader_vertex                            = shader_v;
        pipeline_state.rasterizer_state                         = m_rasterizer_cull_back_solid.get();
        pipeline_state.blend_state                              = m_blend_additive.get();
        pipeline_state.depth_stencil_state                      = use_stencil ? m_depth_stencil_off_on_r.get() : m_depth_stencil_off_off.get();
        pipeline_state.vertex_buffer_stride                     = m_viewport_quad.GetVertexBuffer()->GetStride();
        pipeline_state.render_target_color_textures[0]          = tex_diffuse;
        pipeline_state.clear_color[0]                           = clear_color;
        pipeline_state.render_target_color_textures[1]          = tex_specular;
        pipeline_state.clear_color[1]                           = clear_color;
        pipeline_state.render_target_color_textures[2]          = tex_volumetric;
        pipeline_state.clear_color[2]                           = use_stencil ? state_color_dont_care : Vector4::Zero;
        pipeline_state.render_target_depth_texture              = use_stencil ? tex_depth : nullptr;
        pipeline_state.clear_stencil                            = use_stencil ? state_stencil_load : state_stencil_dont_care;
        pipeline_state.render_target_depth_texture_read_only    = use_stencil;
        pipeline_state.viewport                                 = tex_diffuse->GetViewport();
        pipeline_state.primitive_topology                       = RHI_PrimitiveTopology_TriangleList;
        pipeline_state.pass_name                                = "Pass_Light";

        bool cleared = false;

        // Iterate through all the light entities
        for (const auto& entity : entities)
        {
            if (Light* light = entity->GetComponent<Light>())
            {
                if (light->GetIntensity() != 0)
                {
                    // Set pixel shader
                    pipeline_state.shader_pixel = static_cast<RHI_Shader*>(ShaderLight::GetVariation(m_context, light, m_options));

                    // Skip the shader until it compiles or the users spots a compilation error
                    if (!pipeline_state.shader_pixel->IsCompiled())
                        continue;

                    if (cmd_list->BeginRenderPass(pipeline_state))
                    {
                        cmd_list->SetBufferVertex(m_viewport_quad.GetVertexBuffer());
                        cmd_list->SetBufferIndex(m_viewport_quad.GetIndexBuffer());
                        cmd_list->SetTexture(8, m_render_targets[RenderTarget_Gbuffer_Albedo]);
                        cmd_list->SetTexture(9, m_render_targets[RenderTarget_Gbuffer_Normal]);
                        cmd_list->SetTexture(10, m_render_targets[RenderTarget_Gbuffer_Material]);
                        cmd_list->SetTexture(12, tex_depth);
                        cmd_list->SetTexture(22, (m_options & Render_Hbao) ? m_render_targets[RenderTarget_Hbao] : m_tex_black_opaque);
                        cmd_list->SetTexture(26, (m_options & Render_ScreenSpaceReflections) ? m_render_targets[RenderTarget_Ssr] : m_tex_black_transparent);
                        cmd_list->SetTexture(27, m_render_targets[RenderTarget_Hdr_2]); // previous frame before post-processing
                        cmd_list->SetTexture(31, m_tex_blue_noise);

                        // Update light buffer
                        UpdateLightBuffer(light);

                        // Set shadow map
                        if (light->GetShadowsEnabled())
                        {
                            RHI_Texture* tex_depth = light->GetDepthTexture();
                            RHI_Texture* tex_color = light->GetShadowsTransparentEnabled() ? light->GetColorTexture() : m_tex_white.get();

                            if (light->GetLightType() == LightType::Directional)
                            {
                                cmd_list->SetTexture(13, tex_depth);
                                cmd_list->SetTexture(14, tex_color);
                            }
                            else if (light->GetLightType() == LightType::Point)
                            {
                                cmd_list->SetTexture(15, tex_depth);
                                cmd_list->SetTexture(16, tex_color);
                            }
                            else if (light->GetLightType() == LightType::Spot)
                            {
                                cmd_list->SetTexture(17, tex_depth);
                                cmd_list->SetTexture(18, tex_color);
                            }
                        }

                        // Draw
                        cmd_list->DrawIndexed(Rectangle::GetIndexCount());
                        cmd_list->EndRenderPass();

                        // Clear only on first pass
                        if (!cleared && !use_stencil)
                        {
                            pipeline_state.ResetClearValues();
                            cleared = true;
                        }
                    }
                }
            }
        }
    }

	void Renderer::Pass_Composition(RHI_CommandList* cmd_list, shared_ptr<RHI_Texture>& tex_out, const bool use_stencil)
	{
        bool indirect_bounce = (m_options & Render_IndirectBounce) != 0;

        // Acquire shaders
        const auto& shader_v = m_shaders[Shader_Quad_V];
		const auto& shader_p = m_shaders[indirect_bounce ? Shader_Composition_IndirectBounce_P : Shader_Composition_P];
		if (!shader_v->IsCompiled() || !shader_p->IsCompiled())
			return;

        // Set render state
        static RHI_PipelineState pipeline_state;
        pipeline_state.shader_vertex                    = shader_v.get();
        pipeline_state.shader_pixel                     = shader_p.get();
        pipeline_state.rasterizer_state                 = m_rasterizer_cull_back_solid.get();
        pipeline_state.depth_stencil_state              = use_stencil ? m_depth_stencil_off_on_r.get() : m_depth_stencil_off_off.get();
        pipeline_state.blend_state                      = m_blend_disabled.get();
        pipeline_state.vertex_buffer_stride             = m_viewport_quad.GetVertexBuffer()->GetStride();
        pipeline_state.render_target_color_textures[0]  = tex_out.get();
        pipeline_state.clear_color[0]                   = state_color_dont_care;
        pipeline_state.render_target_depth_texture      = use_stencil ? m_render_targets[RenderTarget_Gbuffer_Depth].get() : nullptr;
        pipeline_state.clear_stencil                    = use_stencil ? state_stencil_load : state_stencil_dont_care;
        pipeline_state.viewport                         = tex_out->GetViewport();
        pipeline_state.primitive_topology               = RHI_PrimitiveTopology_TriangleList;
        pipeline_state.pass_name                        = "Pass_Composition";

        // Begin commands
        if (cmd_list->BeginRenderPass(pipeline_state))
        {
            // Update uber buffer
            m_buffer_uber_cpu.resolution = Vector2(static_cast<float>(tex_out->GetWidth()), static_cast<float>(tex_out->GetHeight()));
            UpdateUberBuffer(cmd_list);

            // Setup command list
            cmd_list->SetBufferVertex(m_viewport_quad.GetVertexBuffer());
            cmd_list->SetBufferIndex(m_viewport_quad.GetIndexBuffer());
            cmd_list->SetTexture(8, m_render_targets[RenderTarget_Gbuffer_Albedo]);
            cmd_list->SetTexture(9, m_render_targets[RenderTarget_Gbuffer_Normal]);
            cmd_list->SetTexture(10, m_render_targets[RenderTarget_Gbuffer_Material]);
            cmd_list->SetTexture(12, m_render_targets[RenderTarget_Gbuffer_Depth]);
            cmd_list->SetTexture(22, (m_options & Render_Hbao) ? m_render_targets[RenderTarget_Hbao] : m_tex_black_opaque);
            cmd_list->SetTexture(23, m_render_targets[RenderTarget_Light_Diffuse]);
            cmd_list->SetTexture(24, m_render_targets[RenderTarget_Light_Specular]);
            cmd_list->SetTexture(25, m_render_targets[RenderTarget_Light_Volumetric]);
            cmd_list->SetTexture(26, (m_options & Render_ScreenSpaceReflections)    ? m_render_targets[RenderTarget_Ssr] : m_tex_black_transparent);
            cmd_list->SetTexture(27, m_render_targets[RenderTarget_Hdr_2]); // previous frame before post-processing
            cmd_list->SetTexture(19, m_render_targets[RenderTarget_Brdf_Specular_Lut]);
            cmd_list->SetTexture(20, GetEnvironmentTexture());
            cmd_list->SetBufferIndex(m_viewport_quad.GetIndexBuffer());
            cmd_list->SetBufferVertex(m_viewport_quad.GetVertexBuffer());
            cmd_list->DrawIndexed(Rectangle::GetIndexCount());
            cmd_list->EndRenderPass();
        }
	}

    void Renderer::Pass_AlphaBlend(RHI_CommandList* cmd_list, RHI_Texture* tex_in, RHI_Texture* tex_out, const bool use_stencil)
    {
        // Acquire shaders
        const auto& shader_v    = m_shaders[Shader_Quad_V];
        const auto& shader_p    = m_shaders[Shader_Texture_P];
        if (!shader_v->IsCompiled() || !shader_p->IsCompiled())
            return;

        // Update uber buffer
        m_buffer_uber_cpu.resolution = Vector2(static_cast<float>(tex_out->GetWidth()), static_cast<float>(tex_out->GetHeight()));
        UpdateUberBuffer(cmd_list);

        // Set render state
        static RHI_PipelineState pipeline_state;
        pipeline_state.shader_vertex                    = shader_v.get();
        pipeline_state.shader_pixel                     = shader_p.get();
        pipeline_state.rasterizer_state                 = m_rasterizer_cull_back_solid.get();
        pipeline_state.blend_state                      = m_blend_alpha.get();
        pipeline_state.depth_stencil_state              = use_stencil ? m_depth_stencil_off_on_r.get() : m_depth_stencil_off_off.get();
        pipeline_state.vertex_buffer_stride             = m_viewport_quad.GetVertexBuffer()->GetStride();
        pipeline_state.render_target_color_textures[0]  = tex_out;
        pipeline_state.clear_color[0]                   = use_stencil ? state_color_load : state_color_dont_care;
        pipeline_state.render_target_depth_texture      = use_stencil ? m_render_targets[RenderTarget_Gbuffer_Depth].get() : nullptr;
        pipeline_state.clear_stencil                    = use_stencil ? state_stencil_load : state_stencil_dont_care;
        pipeline_state.viewport                         = tex_out->GetViewport();
        pipeline_state.primitive_topology               = RHI_PrimitiveTopology_TriangleList;
        pipeline_state.pass_name                        = "Pass_AlphaBlend";
        
        // Record commands
        if (cmd_list->BeginRenderPass(pipeline_state))
        {
            cmd_list->SetBufferVertex(m_viewport_quad.GetVertexBuffer());
            cmd_list->SetBufferIndex(m_viewport_quad.GetIndexBuffer());
            cmd_list->SetTexture(28, tex_in);
            cmd_list->DrawIndexed(Rectangle::GetIndexCount());
            cmd_list->EndRenderPass();
        }
    }

	void Renderer::Pass_PostProcess(RHI_CommandList* cmd_list)
	{
        // IN:  RenderTarget_Composition_Hdr
        // OUT: RenderTarget_Composition_Ldr

        // Acquire render targets
        auto& tex_in_hdr    = m_render_targets[RenderTarget_Hdr];
        auto& tex_out_hdr   = m_render_targets[RenderTarget_Hdr_2];
        auto& tex_in_ldr    = m_render_targets[RenderTarget_Ldr];
        auto& tex_out_ldr   = m_render_targets[RenderTarget_Ldr_2];

        // TAA	
        if (GetOption(Render_AntiAliasing_Taa))
        {
            Pass_TemporalAntialiasing(cmd_list, tex_in_hdr, tex_out_hdr);
            tex_in_hdr.swap(tex_out_hdr);
        }

        // Depth of Field
        if (GetOption(Render_DepthOfField))
        {
            Pass_DepthOfField(cmd_list, tex_in_hdr, tex_out_hdr);
            tex_in_hdr.swap(tex_out_hdr);
        }

        // Motion Blur
        if (GetOption(Render_MotionBlur))
        {
            Pass_MotionBlur(cmd_list, tex_in_hdr, tex_out_hdr);
            tex_in_hdr.swap(tex_out_hdr);
        }

        // Bloom
        if (GetOption(Render_Bloom))
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
            Pass_Copy(cmd_list, tex_in_hdr.get(), tex_in_ldr.get());
        }

        // Dithering
        if (GetOption(Render_Dithering))
        {
            Pass_Dithering(cmd_list, tex_in_ldr, tex_out_ldr);
            tex_in_ldr.swap(tex_out_ldr);
        }

        // FXAA
        if (GetOption(Render_AntiAliasing_Fxaa))
        {
            Pass_FXAA(cmd_list, tex_in_ldr, tex_out_ldr);
            tex_in_ldr.swap(tex_out_ldr);
        }

        // Sharpening
        if (GetOption(Render_Sharpening_LumaSharpen))
        {
            Pass_Sharpening(cmd_list, tex_in_ldr, tex_out_ldr);
            tex_in_ldr.swap(tex_out_ldr);
        }

        // Film grain
        if (GetOption(Render_FilmGrain))
        {
            Pass_FilmGrain(cmd_list, tex_in_ldr, tex_out_ldr);
            tex_in_ldr.swap(tex_out_ldr);
        }

        // Chromatic aberration
        if (GetOption(Render_ChromaticAberration))
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
        pipeline_state.depth_stencil_state              = m_depth_stencil_off_off.get();
        pipeline_state.vertex_buffer_stride             = m_viewport_quad.GetVertexBuffer()->GetStride();
        pipeline_state.render_target_color_textures[0]  = tex_out.get();
        pipeline_state.clear_color[0]                   = state_color_dont_care;
        pipeline_state.viewport                         = tex_out->GetViewport();
        pipeline_state.primitive_topology               = RHI_PrimitiveTopology_TriangleList;
        pipeline_state.pass_name                        = "Pass_Upsample";

        // Record commands
        if (cmd_list->BeginRenderPass(pipeline_state))
        {
            m_buffer_uber_cpu.resolution = Vector2(static_cast<float>(tex_out->GetWidth()), static_cast<float>(tex_out->GetHeight()));
            UpdateUberBuffer(cmd_list);

            cmd_list->SetBufferVertex(m_viewport_quad.GetVertexBuffer());
            cmd_list->SetBufferIndex(m_viewport_quad.GetIndexBuffer());
            cmd_list->SetTexture(28, tex_in);
            cmd_list->DrawIndexed(m_viewport_quad.GetIndexCount());
            cmd_list->EndRenderPass();
        }
    }

    void Renderer::Pass_Downsample(RHI_CommandList* cmd_list, RHI_Texture* tex_in, RHI_Texture* tex_out, const Renderer_Shader_Type shader_type)
    {
        // Set render state
        static RHI_PipelineState pipeline_state;
        pipeline_state.shader_compute = m_shaders[shader_type].get();
        pipeline_state.pass_name      = "Pass_Downsample";

        // Draw
        if (cmd_list->BeginRenderPass(pipeline_state))
        {
            // Update uber buffer
            m_buffer_uber_cpu.resolution = Vector2(static_cast<float>(tex_out->GetWidth()), static_cast<float>(tex_out->GetHeight()));
            UpdateUberBuffer(cmd_list);

            uint32_t thread_group_count_x   = static_cast<uint32_t>(Math::Helper::Ceil(static_cast<float>(tex_out->GetWidth()) / m_thread_group_count));
            uint32_t thread_group_count_y   = static_cast<uint32_t>(Math::Helper::Ceil(static_cast<float>(tex_out->GetHeight()) / m_thread_group_count));
            uint32_t thread_group_count_z   = 1;
            bool async                      = false;

            cmd_list->SetTexture(3, tex_out, true);
            cmd_list->SetTexture(28, tex_in);
            cmd_list->Dispatch(thread_group_count_x, thread_group_count_y, thread_group_count_z, async);
            cmd_list->EndRenderPass();
        }
    }

    void Renderer::Pass_BlurBox(RHI_CommandList* cmd_list, shared_ptr<RHI_Texture>& tex_in, shared_ptr<RHI_Texture>& tex_out, const float sigma, const float pixel_stride, const bool use_stencil)
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
        pipeline_state.depth_stencil_state              = use_stencil ? m_depth_stencil_off_on_r.get() : m_depth_stencil_off_off.get();
        pipeline_state.vertex_buffer_stride             = m_viewport_quad.GetVertexBuffer()->GetStride();
        pipeline_state.render_target_color_textures[0]  = tex_out.get();
        pipeline_state.clear_color[0]                   = state_color_dont_care;
        pipeline_state.render_target_depth_texture      = use_stencil ? m_render_targets[RenderTarget_Gbuffer_Depth].get() : nullptr;
        pipeline_state.viewport                         = tex_out->GetViewport();
        pipeline_state.primitive_topology               = RHI_PrimitiveTopology_TriangleList;
        pipeline_state.pass_name                        = "Pass_BlurBox";

        // Record commands
        if (cmd_list->BeginRenderPass(pipeline_state))
        {
            // Update uber buffer
            m_buffer_uber_cpu.resolution        = Vector2(static_cast<float>(tex_out->GetWidth()), static_cast<float>(tex_out->GetHeight()));
            m_buffer_uber_cpu.blur_direction    = Vector2(pixel_stride, 0.0f);
            m_buffer_uber_cpu.blur_sigma        = sigma;
            UpdateUberBuffer(cmd_list);

            cmd_list->SetBufferVertex(m_viewport_quad.GetVertexBuffer());
            cmd_list->SetBufferIndex(m_viewport_quad.GetIndexBuffer());
            cmd_list->SetTexture(28, tex_in);
            cmd_list->DrawIndexed(m_viewport_quad.GetIndexCount());
            cmd_list->EndRenderPass();
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
        pipeline_state_horizontal.depth_stencil_state               = m_depth_stencil_off_off.get();
        pipeline_state_horizontal.vertex_buffer_stride              = m_viewport_quad.GetVertexBuffer()->GetStride();
        pipeline_state_horizontal.render_target_color_textures[0]   = tex_out.get();
        pipeline_state_horizontal.clear_color[0]                    = state_color_dont_care;
        pipeline_state_horizontal.viewport                          = tex_out->GetViewport();
        pipeline_state_horizontal.primitive_topology                = RHI_PrimitiveTopology_TriangleList;
        pipeline_state_horizontal.pass_name                         = "Pass_BlurGaussian_Horizontal";

        // Record commands for horizontal pass
        if (cmd_list->BeginRenderPass(pipeline_state_horizontal))
        {
            // Update uber buffer
            m_buffer_uber_cpu.resolution        = Vector2(static_cast<float>(tex_in->GetWidth()), static_cast<float>(tex_in->GetHeight()));
            m_buffer_uber_cpu.blur_direction    = Vector2(pixel_stride, 0.0f);
            m_buffer_uber_cpu.blur_sigma        = sigma;
            UpdateUberBuffer(cmd_list);
        
            cmd_list->SetBufferVertex(m_viewport_quad.GetVertexBuffer());
            cmd_list->SetBufferIndex(m_viewport_quad.GetIndexBuffer());
        	cmd_list->SetTexture(28, tex_in);
        	cmd_list->DrawIndexed(Rectangle::GetIndexCount());
            cmd_list->EndRenderPass();
        }
        
        // Set render state for vertical pass
        static RHI_PipelineState pipeline_state_vertical;
        pipeline_state_vertical.shader_vertex                   = shader_v.get();
        pipeline_state_vertical.shader_pixel                    = shader_p.get();
        pipeline_state_vertical.rasterizer_state                = m_rasterizer_cull_back_solid.get();
        pipeline_state_vertical.blend_state                     = m_blend_disabled.get();
        pipeline_state_vertical.depth_stencil_state             = m_depth_stencil_off_off.get();
        pipeline_state_vertical.vertex_buffer_stride            = m_viewport_quad.GetVertexBuffer()->GetStride();
        pipeline_state_vertical.render_target_color_textures[0] = tex_in.get();
        pipeline_state_vertical.clear_color[0]                  = state_color_dont_care;
        pipeline_state_vertical.viewport                        = tex_in->GetViewport();
        pipeline_state_vertical.primitive_topology              = RHI_PrimitiveTopology_TriangleList;
        pipeline_state_vertical.pass_name                       = "Pass_BlurGaussian_Vertical";

        // Record commands for vertical pass
        if (cmd_list->BeginRenderPass(pipeline_state_vertical))
        {
            m_buffer_uber_cpu.blur_direction    = Vector2(0.0f, pixel_stride);
            m_buffer_uber_cpu.blur_sigma        = sigma;
            UpdateUberBuffer(cmd_list);
        
            cmd_list->SetBufferVertex(m_viewport_quad.GetVertexBuffer());
            cmd_list->SetBufferIndex(m_viewport_quad.GetIndexBuffer());
            cmd_list->SetTexture(28, tex_out);
            cmd_list->DrawIndexed(Rectangle::GetIndexCount());
            cmd_list->EndRenderPass();
        }

		// Swap textures
		tex_in.swap(tex_out);
	}

	void Renderer::Pass_BlurBilateralGaussian(RHI_CommandList* cmd_list, shared_ptr<RHI_Texture>& tex_in, shared_ptr<RHI_Texture>& tex_out, const float sigma, const float pixel_stride, const bool use_stencil)
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
        RHI_Texture* tex_depth     = m_render_targets[RenderTarget_Gbuffer_Depth].get();
        RHI_Texture* tex_normal    = m_render_targets[RenderTarget_Gbuffer_Normal].get();

        // Set render state for horizontal pass
        static RHI_PipelineState pipeline_state_horizontal;
        pipeline_state_horizontal.shader_vertex                     = shader_v.get();
        pipeline_state_horizontal.shader_pixel                      = shader_p.get();
        pipeline_state_horizontal.rasterizer_state                  = m_rasterizer_cull_back_solid.get();
        pipeline_state_horizontal.blend_state                       = m_blend_disabled.get();
        pipeline_state_horizontal.depth_stencil_state               = use_stencil ? m_depth_stencil_off_on_r.get() : m_depth_stencil_off_off.get();
        pipeline_state_horizontal.vertex_buffer_stride              = m_viewport_quad.GetVertexBuffer()->GetStride();
        pipeline_state_horizontal.render_target_color_textures[0]   = tex_out.get();
        pipeline_state_horizontal.clear_color[0]                    = state_color_dont_care;
        pipeline_state_horizontal.render_target_depth_texture       = use_stencil ? tex_depth : nullptr;
        pipeline_state_horizontal.clear_stencil                     = use_stencil ? state_stencil_load : state_stencil_dont_care;
        pipeline_state_horizontal.viewport                          = tex_out->GetViewport();
        pipeline_state_horizontal.primitive_topology                = RHI_PrimitiveTopology_TriangleList;
        pipeline_state_horizontal.pass_name                         = "Pass_BlurBilateralGaussian_Horizontal";

        // Record commands for horizontal pass
        if (cmd_list->BeginRenderPass(pipeline_state_horizontal))
        {
            // Update uber buffer
            m_buffer_uber_cpu.resolution        = Vector2(static_cast<float>(tex_in->GetWidth()), static_cast<float>(tex_in->GetHeight()));
            m_buffer_uber_cpu.blur_direction    = Vector2(pixel_stride, 0.0f);
            m_buffer_uber_cpu.blur_sigma        = sigma;
            UpdateUberBuffer(cmd_list);

            cmd_list->SetBufferVertex(m_viewport_quad.GetVertexBuffer());
            cmd_list->SetBufferIndex(m_viewport_quad.GetIndexBuffer());
            cmd_list->SetTexture(28, tex_in);
            cmd_list->SetTexture(12, tex_depth);
            cmd_list->SetTexture(9, tex_normal);
            cmd_list->DrawIndexed(m_viewport_quad.GetIndexCount());
            cmd_list->EndRenderPass();
        }

        // Set render state for vertical pass
        static RHI_PipelineState pipeline_state_vertical;
        pipeline_state_vertical.shader_vertex                   = shader_v.get();
        pipeline_state_vertical.shader_pixel                    = shader_p.get();
        pipeline_state_vertical.rasterizer_state                = m_rasterizer_cull_back_solid.get();
        pipeline_state_vertical.blend_state                     = m_blend_disabled.get();
        pipeline_state_vertical.depth_stencil_state             = use_stencil ? m_depth_stencil_off_on_r.get() : m_depth_stencil_off_off.get();
        pipeline_state_vertical.vertex_buffer_stride            = m_viewport_quad.GetVertexBuffer()->GetStride();
        pipeline_state_vertical.render_target_color_textures[0] = tex_in.get();
        pipeline_state_vertical.clear_color[0]                  = state_color_dont_care;
        pipeline_state_vertical.render_target_depth_texture     = use_stencil ? tex_depth : nullptr;
        pipeline_state_vertical.clear_stencil                   = use_stencil ? state_stencil_load : state_stencil_dont_care;
        pipeline_state_vertical.viewport                        = tex_in->GetViewport();
        pipeline_state_vertical.primitive_topology              = RHI_PrimitiveTopology_TriangleList;
        pipeline_state_vertical.pass_name                       = "Pass_BlurBilateralGaussian_Vertical";

        // Record commands for vertical pass
        if (cmd_list->BeginRenderPass(pipeline_state_vertical))
        {
            // Update uber buffer
            m_buffer_uber_cpu.blur_direction    = Vector2(0.0f, pixel_stride);
            m_buffer_uber_cpu.blur_sigma        = sigma;
            UpdateUberBuffer(cmd_list);

            cmd_list->SetBufferVertex(m_viewport_quad.GetVertexBuffer());
            cmd_list->SetBufferIndex(m_viewport_quad.GetIndexBuffer());
            cmd_list->SetTexture(28, tex_out);
            cmd_list->SetTexture(12, tex_depth);
            cmd_list->SetTexture(9, tex_normal);
            cmd_list->DrawIndexed(m_viewport_quad.GetIndexCount());
            cmd_list->EndRenderPass();
        }

        // Swap textures
		tex_in.swap(tex_out);
	}

	void Renderer::Pass_TemporalAntialiasing(RHI_CommandList* cmd_list, shared_ptr<RHI_Texture>& tex_in, shared_ptr<RHI_Texture>& tex_out)
	{
        // Acquire shaders
        RHI_Shader* shader_c = m_shaders[Shader_Taa_C].get();
        if (!shader_c->IsCompiled())
            return;

        // Acquire history render target
        auto& tex_history = m_render_targets[RenderTarget_TaaHistory];

        // Set render state
        static RHI_PipelineState pipeline_state;
        pipeline_state.shader_compute   = shader_c;
        pipeline_state.pass_name        = "Pass_TemporalAntialiasing";

        // Draw
        if (cmd_list->BeginRenderPass(pipeline_state))
        {
            // Update uber buffer
            m_buffer_uber_cpu.resolution = Vector2(static_cast<float>(tex_out->GetWidth()), static_cast<float>(tex_out->GetHeight()));
            UpdateUberBuffer(cmd_list);

            uint32_t thread_group_count_x = static_cast<uint32_t>(Math::Helper::Ceil(static_cast<float>(tex_out->GetWidth()) / m_thread_group_count));
            uint32_t thread_group_count_y = static_cast<uint32_t>(Math::Helper::Ceil(static_cast<float>(tex_out->GetHeight()) / m_thread_group_count));
            uint32_t thread_group_count_z = 1;
            bool async = false;

            cmd_list->SetTexture(3, tex_out, true);
            cmd_list->SetTexture(28, tex_history);
            cmd_list->SetTexture(29, tex_in);
            cmd_list->SetTexture(11, m_render_targets[RenderTarget_Gbuffer_Velocity]);
            cmd_list->SetTexture(12, m_render_targets[RenderTarget_Gbuffer_Depth]);
            cmd_list->Dispatch(thread_group_count_x, thread_group_count_y, thread_group_count_z, async);
            cmd_list->EndRenderPass();
        }

		// Copy result
        Pass_Copy(cmd_list, tex_out.get(), tex_history.get());
	}

	void Renderer::Pass_Bloom(RHI_CommandList* cmd_list, shared_ptr<RHI_Texture>& tex_in, shared_ptr<RHI_Texture>& tex_out)
	{
		// Acquire shaders
        RHI_Shader* shader_v                    = m_shaders[Shader_Quad_V].get();
		RHI_Shader* shader_p_bloom_luminance	= m_shaders[Shader_BloomDownsampleLuminance_C].get();
		RHI_Shader* shader_p_bloom_blend	    = m_shaders[Shader_BloomUpsampleBlend_P].get();
		RHI_Shader* shader_p_downsample	        = m_shaders[Shader_BloomDownsample_C].get();
		RHI_Shader* shader_p_upsample		    = m_shaders[Shader_Upsample_P].get();
		if (!shader_v->IsCompiled() || !shader_p_bloom_luminance->IsCompiled() || !shader_p_bloom_blend->IsCompiled() || !shader_p_downsample->IsCompiled() || !shader_p_upsample->IsCompiled())
			return;

        // Luminance
        {
            // Set render state
            static RHI_PipelineState pipeline_state;
            pipeline_state.shader_compute = shader_p_bloom_luminance;
            pipeline_state.pass_name      = "Pass_Bloom_Luminance";

            // Draw
            if (cmd_list->BeginRenderPass(pipeline_state))
            {
                // Update uber buffer
                m_buffer_uber_cpu.resolution = Vector2(static_cast<float>(m_render_tex_bloom[0].get()->GetWidth()), static_cast<float>(m_render_tex_bloom[0].get()->GetHeight()));
                UpdateUberBuffer(cmd_list);

                uint32_t thread_group_count_x   = static_cast<uint32_t>(Math::Helper::Ceil(static_cast<float>(m_render_tex_bloom[0].get()->GetWidth()) / m_thread_group_count));
                uint32_t thread_group_count_y   = static_cast<uint32_t>(Math::Helper::Ceil(static_cast<float>(m_render_tex_bloom[0].get()->GetHeight()) / m_thread_group_count));
                uint32_t thread_group_count_z   = 1;
                bool async                      = false;

                cmd_list->SetTexture(3, m_render_tex_bloom[0].get(), true);
                cmd_list->SetTexture(28, tex_in);
                cmd_list->Dispatch(thread_group_count_x, thread_group_count_y, thread_group_count_z, async);
                cmd_list->EndRenderPass();
        }
        }
        
        // Downsample
        // The last bloom texture is the same size as the previous one (it's used for the Gaussian pass below), so we skip it
        for (int i = 0; i < static_cast<int>(m_render_tex_bloom.size() - 1); i++)
        {
            Pass_Downsample(cmd_list, m_render_tex_bloom[i].get(), m_render_tex_bloom[i + 1].get(), Shader_BloomDownsample_C);
        }
        
        auto upsample_additive = [this, &cmd_list, &shader_v, &shader_p_upsample](shared_ptr<RHI_Texture>& tex_in, shared_ptr<RHI_Texture>& tex_out)
        {
            // Set render state
            static RHI_PipelineState pipeline_state;
            pipeline_state.shader_vertex                    = shader_v;
            pipeline_state.shader_pixel                     = shader_p_upsample;
            pipeline_state.rasterizer_state                 = m_rasterizer_cull_back_solid.get();
            pipeline_state.blend_state                      = m_blend_additive.get();
            pipeline_state.depth_stencil_state              = m_depth_stencil_off_off.get();
            pipeline_state.vertex_buffer_stride             = m_viewport_quad.GetVertexBuffer()->GetStride();
            pipeline_state.render_target_color_textures[0]  = tex_out.get();
            pipeline_state.clear_color[0]                   = state_color_load;
            pipeline_state.viewport                         = tex_out->GetViewport();
            pipeline_state.primitive_topology               = RHI_PrimitiveTopology_TriangleList;
            pipeline_state.pass_name                        = "Pass_Bloom_Upsample";

            if (cmd_list->BeginRenderPass(pipeline_state))
            {
                // Update uber buffer
                m_buffer_uber_cpu.resolution = Vector2(static_cast<float>(tex_out->GetWidth()), static_cast<float>(tex_out->GetHeight()));
                UpdateUberBuffer(cmd_list);
        
                cmd_list->SetBufferVertex(m_viewport_quad.GetVertexBuffer());
                cmd_list->SetBufferIndex(m_viewport_quad.GetIndexBuffer());
                cmd_list->SetTexture(28, tex_in);
                cmd_list->DrawIndexed(Rectangle::GetIndexCount());
                cmd_list->EndRenderPass();
            }
        };
        
        // Upsample and blend with render target
        for (int i = static_cast<int>(m_render_tex_bloom.size() - 1); i > 0; i--)
        {
            upsample_additive(m_render_tex_bloom[i], m_render_tex_bloom[i - 1]);
        }
        
        // Additive blend on top of frame
        {
            // Set render state
            static RHI_PipelineState pipeline_state;
            pipeline_state.shader_vertex                    = shader_v;
            pipeline_state.shader_pixel                     = shader_p_bloom_blend;
            pipeline_state.rasterizer_state                 = m_rasterizer_cull_back_solid.get();
            pipeline_state.blend_state                      = m_blend_disabled.get();
            pipeline_state.depth_stencil_state              = m_depth_stencil_off_off.get();
            pipeline_state.vertex_buffer_stride             = m_viewport_quad.GetVertexBuffer()->GetStride();
            pipeline_state.render_target_color_textures[0]  = tex_out.get();
            pipeline_state.clear_color[0]                   = state_color_load;
            pipeline_state.viewport                         = tex_out->GetViewport();
            pipeline_state.primitive_topology               = RHI_PrimitiveTopology_TriangleList;
            pipeline_state.pass_name                        = "Pass_Bloom_Upsample_Blend";
        
            // Record commands
            if (cmd_list->BeginRenderPass(pipeline_state))
            {
                // Update uber buffer
                m_buffer_uber_cpu.resolution = Vector2(static_cast<float>(tex_out->GetWidth()), static_cast<float>(tex_out->GetHeight()));
                UpdateUberBuffer(cmd_list);
        
                cmd_list->SetBufferVertex(m_viewport_quad.GetVertexBuffer());
                cmd_list->SetBufferIndex(m_viewport_quad.GetIndexBuffer());
                cmd_list->SetTexture(28, tex_in);
                cmd_list->SetTexture(29, m_render_tex_bloom.front());
                cmd_list->DrawIndexed(Rectangle::GetIndexCount());
                cmd_list->EndRenderPass();
            }
        }
	}

	void Renderer::Pass_ToneMapping(RHI_CommandList* cmd_list, shared_ptr<RHI_Texture>& tex_in, shared_ptr<RHI_Texture>& tex_out)
	{
        // Acquire shaders
        RHI_Shader* shader_c = m_shaders[Shader_ToneMapping_C].get();
        if (!shader_c->IsCompiled())
            return;

        // Set render state
        static RHI_PipelineState pipeline_state;
        pipeline_state.shader_compute = shader_c;
        pipeline_state.pass_name      = "Pass_ToneMapping";

        // Draw
        if (cmd_list->BeginRenderPass(pipeline_state))
        {
            uint32_t thread_group_count_x = static_cast<uint32_t>(Math::Helper::Ceil(static_cast<float>(tex_out->GetWidth()) / m_thread_group_count));
            uint32_t thread_group_count_y = static_cast<uint32_t>(Math::Helper::Ceil(static_cast<float>(tex_out->GetHeight()) / m_thread_group_count));
            uint32_t thread_group_count_z = 1;
            bool async = false;

            cmd_list->SetTexture(3, tex_out, true);
            cmd_list->SetTexture(28, tex_in);
            cmd_list->Dispatch(thread_group_count_x, thread_group_count_y, thread_group_count_z, async);
            cmd_list->EndRenderPass();
        }
	}

	void Renderer::Pass_GammaCorrection(RHI_CommandList* cmd_list, shared_ptr<RHI_Texture>& tex_in, shared_ptr<RHI_Texture>& tex_out)
	{
        // Acquire shaders
        RHI_Shader* shader_c = m_shaders[Shader_GammaCorrection_C].get();
        if (!shader_c->IsCompiled())
            return;

        // Set render state
        static RHI_PipelineState pipeline_state;
        pipeline_state.shader_compute   = shader_c;
        pipeline_state.pass_name        = "Pass_GammaCorrection";

        // Draw
        if (cmd_list->BeginRenderPass(pipeline_state))
        {
            uint32_t thread_group_count_x   = static_cast<uint32_t>(Math::Helper::Ceil(static_cast<float>(tex_out->GetWidth()) / m_thread_group_count));
            uint32_t thread_group_count_y   = static_cast<uint32_t>(Math::Helper::Ceil(static_cast<float>(tex_out->GetHeight()) / m_thread_group_count));
            uint32_t thread_group_count_z   = 1;
            bool async                      = false;

            cmd_list->SetTexture(3, tex_out, true);
            cmd_list->SetTexture(28, tex_in);
            cmd_list->Dispatch(thread_group_count_x, thread_group_count_y, thread_group_count_z, async);
            cmd_list->EndRenderPass();
        }
	}

	void Renderer::Pass_FXAA(RHI_CommandList* cmd_list, shared_ptr<RHI_Texture>& tex_in, shared_ptr<RHI_Texture>& tex_out)
	{
		// Acquire shaders
		RHI_Shader* shader_p_luma   = m_shaders[Shader_Fxaa_Luminance_C].get();
		RHI_Shader* shader_p_fxaa   = m_shaders[Shader_Fxaa_C].get();
		if (!shader_p_luma->IsCompiled() || !shader_p_fxaa->IsCompiled())
			return;

        // Update uber buffer
        m_buffer_uber_cpu.resolution = Vector2(static_cast<float>(tex_out->GetWidth()), static_cast<float>(tex_out->GetHeight()));
        UpdateUberBuffer(cmd_list);

        // Compute thread count
        uint32_t thread_group_count_x = static_cast<uint32_t>(Math::Helper::Ceil(static_cast<float>(tex_out->GetWidth()) / m_thread_group_count));
        uint32_t thread_group_count_y = static_cast<uint32_t>(Math::Helper::Ceil(static_cast<float>(tex_out->GetHeight()) / m_thread_group_count));
        uint32_t thread_group_count_z = 1;
        bool async = false;

        // Luminance
        {
            // Set render state
            static RHI_PipelineState pipeline_state;
            pipeline_state.shader_compute   = shader_p_luma;
            pipeline_state.pass_name        = "Pass_FXAA_Luminance";

            // Draw
            if (cmd_list->BeginRenderPass(pipeline_state))
            {
                cmd_list->SetTexture(3, tex_out, true);
                cmd_list->SetTexture(28, tex_in);
                cmd_list->Dispatch(thread_group_count_x, thread_group_count_y, thread_group_count_z, async);
                cmd_list->EndRenderPass();
            }
        }

        // FXAA
        {
            // Set render state
            static RHI_PipelineState pipeline_state;
            pipeline_state.shader_compute   = shader_p_fxaa;
            pipeline_state.pass_name        = "Pass_FXAA";

            // Draw
            if (cmd_list->BeginRenderPass(pipeline_state))
            {
                cmd_list->SetTexture(3, tex_in, true);
                cmd_list->SetTexture(28, tex_out);
                cmd_list->Dispatch(thread_group_count_x, thread_group_count_y, thread_group_count_z, async);
                cmd_list->EndRenderPass();
            }
        }

        // Swap the textures
        tex_in.swap(tex_out);
	}

	void Renderer::Pass_ChromaticAberration(RHI_CommandList* cmd_list, shared_ptr<RHI_Texture>& tex_in, shared_ptr<RHI_Texture>& tex_out)
	{
        // Acquire shaders
        RHI_Shader* shader_c = m_shaders[Shader_ChromaticAberration_C].get();
        if (!shader_c->IsCompiled())
            return;

        // Set render state
        static RHI_PipelineState pipeline_state;
        pipeline_state.shader_compute = shader_c;
        pipeline_state.pass_name      = "Pass_ChromaticAberration";

        // Draw
        if (cmd_list->BeginRenderPass(pipeline_state))
        {
            // Update uber buffer
            m_buffer_uber_cpu.resolution = Vector2(static_cast<float>(tex_out->GetWidth()), static_cast<float>(tex_out->GetHeight()));
            UpdateUberBuffer(cmd_list);

            uint32_t thread_group_count_x   = static_cast<uint32_t>(Math::Helper::Ceil(static_cast<float>(tex_out->GetWidth()) / m_thread_group_count));
            uint32_t thread_group_count_y   = static_cast<uint32_t>(Math::Helper::Ceil(static_cast<float>(tex_out->GetHeight()) / m_thread_group_count));
            uint32_t thread_group_count_z   = 1;
            bool async                      = false;

            cmd_list->SetTexture(3, tex_out, true);
            cmd_list->SetTexture(28, tex_in);
            cmd_list->Dispatch(thread_group_count_x, thread_group_count_y, thread_group_count_z, async);
            cmd_list->EndRenderPass();
        }
	}

	void Renderer::Pass_MotionBlur(RHI_CommandList* cmd_list, shared_ptr<RHI_Texture>& tex_in, shared_ptr<RHI_Texture>& tex_out)
	{
        // Acquire shaders
        RHI_Shader* shader_c = m_shaders[Shader_MotionBlur_C].get();
        if (!shader_c->IsCompiled())
            return;

        // Set render state
        static RHI_PipelineState pipeline_state;
        pipeline_state.shader_compute = shader_c;
        pipeline_state.pass_name      = "Pass_MotionBlur";

        // Draw
        if (cmd_list->BeginRenderPass(pipeline_state))
        {
            // Update uber buffer
            m_buffer_uber_cpu.resolution = Vector2(static_cast<float>(tex_out->GetWidth()), static_cast<float>(tex_out->GetHeight()));
            UpdateUberBuffer(cmd_list);

            uint32_t thread_group_count_x   = static_cast<uint32_t>(Math::Helper::Ceil(static_cast<float>(tex_out->GetWidth()) / m_thread_group_count));
            uint32_t thread_group_count_y   = static_cast<uint32_t>(Math::Helper::Ceil(static_cast<float>(tex_out->GetHeight()) / m_thread_group_count));
            uint32_t thread_group_count_z   = 1;
            bool async                      = false;

            cmd_list->SetTexture(3, tex_out, true);
            cmd_list->SetTexture(28, tex_in);
            cmd_list->SetTexture(11, m_render_targets[RenderTarget_Gbuffer_Velocity]);
            cmd_list->SetTexture(12, m_render_targets[RenderTarget_Gbuffer_Depth]);
            cmd_list->Dispatch(thread_group_count_x, thread_group_count_y, thread_group_count_z, async);
            cmd_list->EndRenderPass();
        }
	}

    void Renderer::Pass_DepthOfField(RHI_CommandList* cmd_list, shared_ptr<RHI_Texture>& tex_in, shared_ptr<RHI_Texture>& tex_out)
    {
        // Acquire shaders
        RHI_Shader* shader_downsampleCoc    = m_shaders[Shader_Dof_DownsampleCoc_C].get();
        RHI_Shader* shader_bokeh            = m_shaders[Shader_Dof_Bokeh_C].get();
        RHI_Shader* shader_tent             = m_shaders[Shader_Dof_Tent_C].get();
        RHI_Shader* shader_upsampleBlend    = m_shaders[Shader_Dof_UpscaleBlend_C].get();
        if (!shader_downsampleCoc->IsCompiled() || !shader_bokeh->IsCompiled() || !shader_tent->IsCompiled() || !shader_upsampleBlend->IsCompiled())
            return;

        // Acquire render targets
        RHI_Texture* tex_bokeh_half     = m_render_targets[RenderTarget_Dof_Half].get();
        RHI_Texture* tex_bokeh_half_2   = m_render_targets[RenderTarget_Dof_Half_2].get();

        // Downsample and compute circle of confusion
        {
            // Set render state
            static RHI_PipelineState pipeline_state;
            pipeline_state.shader_compute = shader_downsampleCoc;
            pipeline_state.pass_name      = "Pass_Dof_DownsampleCoc";

            // Draw
            if (cmd_list->BeginRenderPass(pipeline_state))
            {
                // Update uber buffer
                m_buffer_uber_cpu.resolution = Vector2(static_cast<float>(tex_bokeh_half->GetWidth()), static_cast<float>(tex_bokeh_half->GetHeight()));
                UpdateUberBuffer(cmd_list);

                uint32_t thread_group_count_x   = static_cast<uint32_t>(Math::Helper::Ceil(static_cast<float>(tex_bokeh_half->GetWidth()) / m_thread_group_count));
                uint32_t thread_group_count_y   = static_cast<uint32_t>(Math::Helper::Ceil(static_cast<float>(tex_bokeh_half->GetHeight()) / m_thread_group_count));
                uint32_t thread_group_count_z   = 1;
                bool async                      = false;

                cmd_list->SetTexture(3, tex_bokeh_half, true);
                cmd_list->SetTexture(12, m_render_targets[RenderTarget_Gbuffer_Depth]);
                cmd_list->SetTexture(28, tex_in);
                cmd_list->Dispatch(thread_group_count_x, thread_group_count_y, thread_group_count_z, async);
                cmd_list->EndRenderPass();
            }
        }

        // Bokeh
        {
            // Set render state
            static RHI_PipelineState pipeline_state;
            pipeline_state.shader_compute   = shader_bokeh;
            pipeline_state.pass_name        = "Pass_Dof_Bokeh";

            // Draw
            if (cmd_list->BeginRenderPass(pipeline_state))
            {
                // Update uber buffer
                m_buffer_uber_cpu.resolution = Vector2(static_cast<float>(tex_bokeh_half_2->GetWidth()), static_cast<float>(tex_bokeh_half_2->GetHeight()));
                UpdateUberBuffer(cmd_list);

                uint32_t thread_group_count_x = static_cast<uint32_t>(Math::Helper::Ceil(static_cast<float>(tex_bokeh_half_2->GetWidth()) / m_thread_group_count));
                uint32_t thread_group_count_y = static_cast<uint32_t>(Math::Helper::Ceil(static_cast<float>(tex_bokeh_half_2->GetHeight()) / m_thread_group_count));
                uint32_t thread_group_count_z = 1;
                bool async = false;

                cmd_list->SetTexture(3, tex_bokeh_half_2, true);
                cmd_list->SetTexture(28, tex_bokeh_half);
                cmd_list->Dispatch(thread_group_count_x, thread_group_count_y, thread_group_count_z, async);
                cmd_list->EndRenderPass();
            }
        }

        // Blur the bokeh using a tent filter
        {
            // Set render state
            static RHI_PipelineState pipeline_state;
            pipeline_state.shader_compute   = shader_tent;
            pipeline_state.pass_name        = "Pass_Dof_Tent";

            // Draw
            if (cmd_list->BeginRenderPass(pipeline_state))
            {
                // Update uber buffer
                m_buffer_uber_cpu.resolution = Vector2(static_cast<float>(tex_bokeh_half->GetWidth()), static_cast<float>(tex_bokeh_half->GetHeight()));
                UpdateUberBuffer(cmd_list);

                uint32_t thread_group_count_x = static_cast<uint32_t>(Math::Helper::Ceil(static_cast<float>(tex_bokeh_half->GetWidth()) / m_thread_group_count));
                uint32_t thread_group_count_y = static_cast<uint32_t>(Math::Helper::Ceil(static_cast<float>(tex_bokeh_half->GetHeight()) / m_thread_group_count));
                uint32_t thread_group_count_z = 1;
                bool async = false;

                cmd_list->SetTexture(3, tex_bokeh_half, true);
                cmd_list->SetTexture(28, tex_bokeh_half_2);
                cmd_list->Dispatch(thread_group_count_x, thread_group_count_y, thread_group_count_z, async);
                cmd_list->EndRenderPass();
            }
        }

        // Upscale & Blend
        {
            // Set render state
            static RHI_PipelineState pipeline_state;
            pipeline_state.shader_compute   = shader_upsampleBlend;
            pipeline_state.pass_name        = "Pass_Dof_UpscaleBlend";

            // Draw
            if (cmd_list->BeginRenderPass(pipeline_state))
            {
                // Update uber buffer
                m_buffer_uber_cpu.resolution = Vector2(static_cast<float>(tex_out->GetWidth()), static_cast<float>(tex_out->GetHeight()));
                UpdateUberBuffer(cmd_list);

                uint32_t thread_group_count_x = static_cast<uint32_t>(Math::Helper::Ceil(static_cast<float>(tex_out->GetWidth()) / m_thread_group_count));
                uint32_t thread_group_count_y = static_cast<uint32_t>(Math::Helper::Ceil(static_cast<float>(tex_out->GetHeight()) / m_thread_group_count));
                uint32_t thread_group_count_z = 1;
                bool async = false;

                cmd_list->SetTexture(3, tex_out, true);
                cmd_list->SetTexture(28, tex_in);
                cmd_list->SetTexture(29, tex_bokeh_half);
                cmd_list->Dispatch(thread_group_count_x, thread_group_count_y, thread_group_count_z, async);
                cmd_list->EndRenderPass();
            }
        }
    }

	void Renderer::Pass_Dithering(RHI_CommandList* cmd_list, shared_ptr<RHI_Texture>& tex_in, shared_ptr<RHI_Texture>& tex_out)
	{
		// Acquire shaders
        RHI_Shader* shader_v = m_shaders[Shader_Quad_V].get();
        RHI_Shader* shader_p = m_shaders[Shader_Dithering_P].get();
        if (!shader_p->IsCompiled() || !shader_v->IsCompiled())
            return;

        // Set render state
        static RHI_PipelineState pipeline_state;
        pipeline_state.shader_vertex                    = shader_v;
        pipeline_state.shader_pixel                     = shader_p;
        pipeline_state.rasterizer_state                 = m_rasterizer_cull_back_solid.get();
        pipeline_state.blend_state                      = m_blend_disabled.get();
        pipeline_state.depth_stencil_state              = m_depth_stencil_off_off.get();
        pipeline_state.vertex_buffer_stride             = m_viewport_quad.GetVertexBuffer()->GetStride();
        pipeline_state.render_target_color_textures[0]  = tex_out.get();
        pipeline_state.clear_color[0]                   = Vector4::Zero;
        pipeline_state.primitive_topology               = RHI_PrimitiveTopology_TriangleList;
        pipeline_state.viewport                         = tex_out->GetViewport();
        pipeline_state.pass_name                        = "Pass_Dithering";

        // Record commands
        if (cmd_list->BeginRenderPass(pipeline_state))
        {
            // Update uber buffer
            m_buffer_uber_cpu.resolution = Vector2(static_cast<float>(tex_out->GetWidth()), static_cast<float>(tex_out->GetHeight()));
            UpdateUberBuffer(cmd_list);

            cmd_list->SetBufferVertex(m_viewport_quad.GetVertexBuffer());
            cmd_list->SetBufferIndex(m_viewport_quad.GetIndexBuffer());
            cmd_list->SetTexture(28, tex_in);
            cmd_list->DrawIndexed(Rectangle::GetIndexCount());
            cmd_list->EndRenderPass();
        }
	}

    void Renderer::Pass_FilmGrain(RHI_CommandList* cmd_list, shared_ptr<RHI_Texture>& tex_in, shared_ptr<RHI_Texture>& tex_out)
    {
        // Acquire shaders
        RHI_Shader* shader_c = m_shaders[Shader_FilmGrain_C].get();
        if (!shader_c->IsCompiled())
            return;

        // Set render state
        static RHI_PipelineState pipeline_state;
        pipeline_state.shader_compute = shader_c;
        pipeline_state.pass_name      = "Pass_FilmGrain";

        // Draw
        if (cmd_list->BeginRenderPass(pipeline_state))
        {
            // Update uber buffer
            m_buffer_uber_cpu.resolution = Vector2(static_cast<float>(tex_out->GetWidth()), static_cast<float>(tex_out->GetHeight()));
            UpdateUberBuffer(cmd_list);

            uint32_t thread_group_count_x   = static_cast<uint32_t>(Math::Helper::Ceil(static_cast<float>(tex_out->GetWidth()) / m_thread_group_count));
            uint32_t thread_group_count_y   = static_cast<uint32_t>(Math::Helper::Ceil(static_cast<float>(tex_out->GetHeight()) / m_thread_group_count));
            uint32_t thread_group_count_z   = 1;
            bool async                      = false;

            cmd_list->SetTexture(3, tex_out, true);
            cmd_list->SetTexture(28, tex_in);
            cmd_list->Dispatch(thread_group_count_x, thread_group_count_y, thread_group_count_z, async);
            cmd_list->EndRenderPass();
        }
    }

	void Renderer::Pass_Sharpening(RHI_CommandList* cmd_list, shared_ptr<RHI_Texture>& tex_in, shared_ptr<RHI_Texture>& tex_out)
	{
        // Acquire shaders
        RHI_Shader* shader_c = m_shaders[Shader_Sharpening_C].get();
        if (!shader_c->IsCompiled())
            return;

        // Set render state
        static RHI_PipelineState pipeline_state;
        pipeline_state.shader_compute = shader_c;
        pipeline_state.pass_name      = "Pass_Sharpening";

        // Draw
        if (cmd_list->BeginRenderPass(pipeline_state))
        {
            // Update uber buffer
            m_buffer_uber_cpu.resolution = Vector2(static_cast<float>(tex_out->GetWidth()), static_cast<float>(tex_out->GetHeight()));
            UpdateUberBuffer(cmd_list);

            uint32_t thread_group_count_x   = static_cast<uint32_t>(Math::Helper::Ceil(static_cast<float>(tex_out->GetWidth()) / m_thread_group_count));
            uint32_t thread_group_count_y   = static_cast<uint32_t>(Math::Helper::Ceil(static_cast<float>(tex_out->GetHeight()) / m_thread_group_count));
            uint32_t thread_group_count_z   = 1;
            bool async                      = false;

            cmd_list->SetTexture(3, tex_out, true);
            cmd_list->SetTexture(28, tex_in);
            cmd_list->Dispatch(thread_group_count_x, thread_group_count_y, thread_group_count_z, async);
            cmd_list->EndRenderPass();
        }
	}

	void Renderer::Pass_Lines(RHI_CommandList* cmd_list, shared_ptr<RHI_Texture>& tex_out)
	{
		const bool draw_picking_ray = m_options & Render_Debug_PickingRay;
		const bool draw_aabb		= m_options & Render_Debug_Aabb;
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
                    Light* light = entity->GetComponent<Light>();

                    if (light->GetLightType() == LightType::Spot)
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
                    if (auto renderable = entity->GetRenderable())
                    {
                        DrawBox(renderable->GetAabb(), Vector4(0.41f, 0.86f, 1.0f, 1.0f));
                    }
                }

                for (const auto& entity : m_entities[Renderer_Object_Transparent])
                {
                    if (auto renderable = entity->GetRenderable())
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
                pipeline_state.blend_state                      = m_blend_alpha.get();
                pipeline_state.depth_stencil_state              = m_depth_stencil_on_off_r.get();
                pipeline_state.vertex_buffer_stride             = m_gizmo_grid->GetVertexBuffer()->GetStride();
                pipeline_state.render_target_color_textures[0]  = tex_out.get();
                pipeline_state.render_target_depth_texture      = m_render_targets[RenderTarget_Gbuffer_Depth].get();
                pipeline_state.viewport                         = tex_out->GetViewport();
                pipeline_state.primitive_topology               = RHI_PrimitiveTopology_LineList;
                pipeline_state.pass_name                        = "Pass_Lines_Grid";

                // Create and submit command list
                if (cmd_list->BeginRenderPass(pipeline_state))
                {
                    // Update uber buffer
                    m_buffer_uber_cpu.resolution    = m_resolution;
                    m_buffer_uber_cpu.transform     = m_gizmo_grid->ComputeWorldMatrix(m_camera->GetTransform()) * m_buffer_frame_cpu.view_projection_unjittered;
                    UpdateUberBuffer(cmd_list);

                    cmd_list->SetBufferIndex(m_gizmo_grid->GetIndexBuffer().get());
                    cmd_list->SetBufferVertex(m_gizmo_grid->GetVertexBuffer().get());
                    cmd_list->DrawIndexed(m_gizmo_grid->GetIndexCount());
                    cmd_list->EndRenderPass();
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
                pipeline_state.blend_state                      = m_blend_alpha.get();
                pipeline_state.depth_stencil_state              = m_depth_stencil_on_off_r.get();
                pipeline_state.vertex_buffer_stride             = m_vertex_buffer_lines->GetStride();
                pipeline_state.render_target_color_textures[0]  = tex_out.get();
                pipeline_state.render_target_depth_texture      = m_render_targets[RenderTarget_Gbuffer_Depth].get();
                pipeline_state.viewport                         = tex_out->GetViewport();
                pipeline_state.primitive_topology               = RHI_PrimitiveTopology_LineList;
                pipeline_state.pass_name                        = "Pass_Lines";

                // Create and submit command list
                if (cmd_list->BeginRenderPass(pipeline_state))
                {
                    cmd_list->SetBufferVertex(m_vertex_buffer_lines.get());
                    cmd_list->Draw(line_vertex_buffer_size);
                    cmd_list->EndRenderPass();
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
            pipeline_state.depth_stencil_state              = m_depth_stencil_off_off.get();
            pipeline_state.vertex_buffer_stride             = m_vertex_buffer_lines->GetStride();
            pipeline_state.render_target_color_textures[0]  = tex_out.get();
            pipeline_state.viewport                         = tex_out->GetViewport();
            pipeline_state.primitive_topology               = RHI_PrimitiveTopology_LineList;
            pipeline_state.pass_name                        = "Pass_Lines_No_Depth";

            // Create and submit command list
            if (cmd_list->BeginRenderPass(pipeline_state))
            {
                cmd_list->SetBufferVertex(m_vertex_buffer_lines.get());
                cmd_list->Draw(line_vertex_buffer_size);
                cmd_list->EndRenderPass();
            }
        }
	}

	void Renderer::Pass_Icons(RHI_CommandList* cmd_list, RHI_Texture* tex_out)
	{
        if (!(m_options & Render_Debug_Lights))
            return;

        // Acquire resources
        auto& lights                    = m_entities[Renderer_Object_Light];
		const auto& shader_quad_v       = m_shaders[Shader_Quad_V];
        const auto& shader_texture_p    = m_shaders[Shader_Texture_P];
		if (lights.empty() || !shader_quad_v->IsCompiled() || !shader_texture_p->IsCompiled())
			return;

        // Set render state
        static RHI_PipelineState pipeline_state;
        pipeline_state.shader_vertex                    = shader_quad_v.get();
        pipeline_state.shader_pixel                     = shader_texture_p.get();
        pipeline_state.rasterizer_state                 = m_rasterizer_cull_back_solid.get();
        pipeline_state.blend_state                      = m_blend_alpha.get();
        pipeline_state.depth_stencil_state              = m_depth_stencil_off_off.get();
        pipeline_state.vertex_buffer_stride             = m_viewport_quad.GetVertexBuffer()->GetStride(); // stride matches rect
        pipeline_state.render_target_color_textures[0]  = tex_out;
        pipeline_state.primitive_topology               = RHI_PrimitiveTopology_TriangleList;
        pipeline_state.viewport                         = tex_out->GetViewport();
        pipeline_state.pass_name                        = "Pass_Gizmos_Lights";

        // For each light
        for (const auto& entity : lights)
        {
            if (cmd_list->BeginRenderPass(pipeline_state))
            {
                // Light can be null if it just got removed and our buffer doesn't update till the next frame
                if (Light* light = entity->GetComponent<Light>())
                {
                    auto position_light_world       = entity->GetTransform()->GetPosition();
                    auto position_camera_world      = m_camera->GetTransform()->GetPosition();
                    auto direction_camera_to_light  = (position_light_world - position_camera_world).Normalized();
                    const auto v_dot_l                    = Vector3::Dot(m_camera->GetTransform()->GetForward(), direction_camera_to_light);
        
                    // Only draw if it's inside our view
                    if (v_dot_l > 0.5f)
                    {
                        // Compute light screen space position and scale (based on distance from the camera)
                        const auto position_light_screen    = m_camera->Project(position_light_world);
                        const auto distance                 = (position_camera_world - position_light_world).Length() + Helper::M_EPSILON;
                        auto scale                          = m_gizmo_size_max / distance;
                        scale                               = Helper::Clamp(scale, m_gizmo_size_min, m_gizmo_size_max);
        
                        // Choose texture based on light type
                        shared_ptr<RHI_Texture> light_tex = nullptr;
                        const auto type = light->GetLightType();
                        if (type == LightType::Directional)	light_tex = m_gizmo_tex_light_directional;
                        else if (type == LightType::Point)	light_tex = m_gizmo_tex_light_point;
                        else if (type == LightType::Spot)	light_tex = m_gizmo_tex_light_spot;
        
                        // Construct appropriate rectangle
                        const auto tex_width = light_tex->GetWidth() * scale;
                        const auto tex_height = light_tex->GetHeight() * scale;
                        auto rectangle = Math::Rectangle
                        (
                            position_light_screen.x - tex_width * 0.5f,
                            position_light_screen.y - tex_height * 0.5f,
                            position_light_screen.x + tex_width,
                            position_light_screen.y + tex_height
                        );
                        if (rectangle != m_gizmo_light_rect)
                        {
                            m_gizmo_light_rect = rectangle;
                            m_gizmo_light_rect.CreateBuffers(this);
                        }
        
                        // Update uber buffer
                        m_buffer_uber_cpu.resolution = Vector2(static_cast<float>(tex_width), static_cast<float>(tex_width));
                        m_buffer_uber_cpu.transform = m_buffer_frame_cpu.view_projection_ortho;
                        UpdateUberBuffer(cmd_list);
        
                        cmd_list->SetTexture(28, light_tex);
                        cmd_list->SetBufferIndex(m_gizmo_light_rect.GetIndexBuffer());
                        cmd_list->SetBufferVertex(m_gizmo_light_rect.GetVertexBuffer());
                        cmd_list->DrawIndexed(Rectangle::GetIndexCount());
                    }
                }
                cmd_list->EndRenderPass();
            }
        }  
	}

    void Renderer::Pass_TransformHandle(RHI_CommandList* cmd_list, RHI_Texture* tex_out)
	{
        if (!GetOption(Render_Debug_Transform))
            return;

        // Acquire resources
        auto const& shader_gizmo_transform_v    = m_shaders[Shader_Entity_V];
        auto const& shader_gizmo_transform_p    = m_shaders[Shader_Entity_Transform_P];
        if (!shader_gizmo_transform_v->IsCompiled() || !shader_gizmo_transform_p->IsCompiled())
            return;

        // Transform
        if (m_gizmo_transform->Update(m_camera.get(), m_gizmo_transform_size, m_gizmo_transform_speed))
        {
            // Set render state
            static RHI_PipelineState pipeline_state;
            pipeline_state.shader_vertex                    = shader_gizmo_transform_v.get();
            pipeline_state.shader_pixel                     = shader_gizmo_transform_p.get();
            pipeline_state.rasterizer_state                 = m_rasterizer_cull_back_solid.get();
            pipeline_state.blend_state                      = m_blend_alpha.get();
            pipeline_state.depth_stencil_state              = m_depth_stencil_off_off.get();
            pipeline_state.vertex_buffer_stride             = m_gizmo_transform->GetVertexBuffer()->GetStride();
            pipeline_state.render_target_color_textures[0]  = tex_out;
            pipeline_state.primitive_topology               = RHI_PrimitiveTopology_TriangleList;
            pipeline_state.viewport                         = tex_out->GetViewport();

            // Axis - X
            pipeline_state.pass_name = "Pass_Gizmos_Axis_X";
            if (cmd_list->BeginRenderPass(pipeline_state))
            {
                m_buffer_uber_cpu.transform         = m_gizmo_transform->GetHandle().GetTransform(Vector3::Right);
                m_buffer_uber_cpu.transform_axis    = m_gizmo_transform->GetHandle().GetColor(Vector3::Right);
                UpdateUberBuffer(cmd_list);
            
                cmd_list->SetBufferIndex(m_gizmo_transform->GetIndexBuffer());
                cmd_list->SetBufferVertex(m_gizmo_transform->GetVertexBuffer());
                cmd_list->DrawIndexed(m_gizmo_transform->GetIndexCount());
                cmd_list->EndRenderPass();
            }
            
            // Axis - Y
            pipeline_state.pass_name = "Pass_Gizmos_Axis_Y";
            if (cmd_list->BeginRenderPass(pipeline_state))
            {
                m_buffer_uber_cpu.transform         = m_gizmo_transform->GetHandle().GetTransform(Vector3::Up);
                m_buffer_uber_cpu.transform_axis    = m_gizmo_transform->GetHandle().GetColor(Vector3::Up);
                UpdateUberBuffer(cmd_list);

                cmd_list->SetBufferIndex(m_gizmo_transform->GetIndexBuffer());
                cmd_list->SetBufferVertex(m_gizmo_transform->GetVertexBuffer());
                cmd_list->DrawIndexed(m_gizmo_transform->GetIndexCount());
                cmd_list->EndRenderPass();
            }
            
            // Axis - Z
            pipeline_state.pass_name = "Pass_Gizmos_Axis_Z";
            if (cmd_list->BeginRenderPass(pipeline_state))
            {
                m_buffer_uber_cpu.transform         = m_gizmo_transform->GetHandle().GetTransform(Vector3::Forward);
                m_buffer_uber_cpu.transform_axis    = m_gizmo_transform->GetHandle().GetColor(Vector3::Forward);
                UpdateUberBuffer(cmd_list);

                cmd_list->SetBufferIndex(m_gizmo_transform->GetIndexBuffer());
                cmd_list->SetBufferVertex(m_gizmo_transform->GetVertexBuffer());
                cmd_list->DrawIndexed(m_gizmo_transform->GetIndexCount());
                cmd_list->EndRenderPass();
            }
            
            // Axes - XYZ
            if (m_gizmo_transform->DrawXYZ())
            {
                pipeline_state.pass_name = "Pass_Gizmos_Axis_XYZ";
                if (cmd_list->BeginRenderPass(pipeline_state))
                {
                    m_buffer_uber_cpu.transform         = m_gizmo_transform->GetHandle().GetTransform(Vector3::One);
                    m_buffer_uber_cpu.transform_axis    = m_gizmo_transform->GetHandle().GetColor(Vector3::One);
                    UpdateUberBuffer(cmd_list);

                    cmd_list->SetBufferIndex(m_gizmo_transform->GetIndexBuffer());
                    cmd_list->SetBufferVertex(m_gizmo_transform->GetVertexBuffer());
                    cmd_list->DrawIndexed(m_gizmo_transform->GetIndexCount());
                    cmd_list->EndRenderPass();
                }
            }
        }
	}

    void Renderer::Pass_Outline(RHI_CommandList* cmd_list, shared_ptr<RHI_Texture>& tex_out)
    {
        if (!GetOption(Render_Debug_SelectionOutline))
            return;

        if (const Entity* entity = m_gizmo_transform->GetSelectedEntity())
        {
            // Get renderable
            const Renderable* renderable = entity->GetRenderable();
            if (!renderable)
                return;

            // Get material
            const Material* material = renderable->GetMaterial();
            if (!material)
                return;

            // Get geometry
            const Model* model = renderable->GeometryModel();
            if (!model || !model->GetVertexBuffer() || !model->GetIndexBuffer())
                return;

            // Acquire shaders
            const auto& shader_v = m_shaders[Shader_Entity_V];
            const auto& shader_p = m_shaders[Shader_Entity_Outline_P];
            if (!shader_v->IsCompiled() || !shader_p->IsCompiled())
                return;

            RHI_Texture* tex_depth  = m_render_targets[RenderTarget_Gbuffer_Depth].get();
            RHI_Texture* tex_normal = m_render_targets[RenderTarget_Gbuffer_Normal].get();

            // Set render state
            static RHI_PipelineState pipeline_state;
            pipeline_state.shader_vertex                            = shader_v.get();
            pipeline_state.shader_pixel                             = shader_p.get();
            pipeline_state.rasterizer_state                         = m_rasterizer_cull_back_solid.get();
            pipeline_state.blend_state                              = m_blend_alpha.get();
            pipeline_state.depth_stencil_state                      = m_depth_stencil_on_off_r.get();
            pipeline_state.vertex_buffer_stride                     = model->GetVertexBuffer()->GetStride();
            pipeline_state.render_target_color_textures[0]          = tex_out.get();
            pipeline_state.render_target_depth_texture              = tex_depth;
            pipeline_state.render_target_depth_texture_read_only    = true;
            pipeline_state.primitive_topology                       = RHI_PrimitiveTopology_TriangleList;
            pipeline_state.viewport                                 = tex_out->GetViewport();
            pipeline_state.pass_name                                = "Pass_Outline";

            // Record commands
            if (cmd_list->BeginRenderPass(pipeline_state))
            {
                 // Update uber buffer with entity transform
                if (Transform* transform = entity->GetTransform())
                {
                    m_buffer_uber_cpu.transform     = transform->GetMatrix();
                    m_buffer_uber_cpu.resolution    = Vector2(tex_out->GetWidth(), tex_out->GetHeight());
                    UpdateUberBuffer(cmd_list);
                }

                cmd_list->SetTexture(12, tex_depth);
                cmd_list->SetTexture(9, tex_normal);
                cmd_list->SetBufferVertex(model->GetVertexBuffer());
                cmd_list->SetBufferIndex(model->GetIndexBuffer());
                cmd_list->DrawIndexed(renderable->GeometryIndexCount(), renderable->GeometryIndexOffset(), renderable->GeometryVertexOffset());
                cmd_list->EndRenderPass();
            }
        }
    }

	void Renderer::Pass_Text(RHI_CommandList* cmd_list, RHI_Texture* tex_out)
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
        pipeline_state.blend_state                      = m_blend_alpha.get();
        pipeline_state.depth_stencil_state              = m_depth_stencil_off_off.get();
        pipeline_state.vertex_buffer_stride             = m_font->GetVertexBuffer()->GetStride();
        pipeline_state.render_target_color_textures[0]  = tex_out;
        pipeline_state.primitive_topology               = RHI_PrimitiveTopology_TriangleList;
        pipeline_state.viewport                         = tex_out->GetViewport();
        pipeline_state.pass_name                        = "Pass_Text";

        // Update text
        const auto text_pos = Vector2(-m_viewport.width * 0.5f + 5.0f, m_viewport.height * 0.5f - m_font->GetSize() - 2.0f);
        m_font->SetText(m_profiler->GetMetrics(), text_pos);

        // Draw outline
        if (m_font->GetOutline() != Font_Outline_None && m_font->GetOutlineSize() != 0)
        { 
            if (cmd_list->BeginRenderPass(pipeline_state))
            {
                // Update uber buffer
                m_buffer_uber_cpu.resolution    = Vector2(static_cast<float>(tex_out->GetWidth()), static_cast<float>(tex_out->GetHeight()));
                m_buffer_uber_cpu.color         = m_font->GetColorOutline();
                UpdateUberBuffer(cmd_list);

                cmd_list->SetBufferIndex(m_font->GetIndexBuffer());
                cmd_list->SetBufferVertex(m_font->GetVertexBuffer());
                cmd_list->SetTexture(30, m_font->GetAtlasOutline());
                cmd_list->DrawIndexed(m_font->GetIndexCount());
                cmd_list->EndRenderPass();
            }
        }

        // Draw 
        if (cmd_list->BeginRenderPass(pipeline_state))
        {
            // Update uber buffer
            m_buffer_uber_cpu.resolution    = Vector2(static_cast<float>(tex_out->GetWidth()), static_cast<float>(tex_out->GetHeight()));
            m_buffer_uber_cpu.color         = m_font->GetColor();
            UpdateUberBuffer(cmd_list);

            cmd_list->SetBufferIndex(m_font->GetIndexBuffer());
            cmd_list->SetBufferVertex(m_font->GetVertexBuffer());
            cmd_list->SetTexture(30, m_font->GetAtlas());
            cmd_list->DrawIndexed(m_font->GetIndexCount());
            cmd_list->EndRenderPass();
        }
	}

	bool Renderer::Pass_DebugBuffer(RHI_CommandList* cmd_list, shared_ptr<RHI_Texture>& tex_out)
	{
        if (m_render_target_debug == 0)
            return true;

		// Bind correct texture & shader pass
        RHI_Texture* texture                = m_render_targets[static_cast<Renderer_RenderTarget_Type>(m_render_target_debug)].get();
        Renderer_Shader_Type shader_type    = Shader_Texture_P;

		if (m_render_target_debug == RenderTarget_Gbuffer_Albedo)
		{
			shader_type = Shader_DebugChannelRgbGammaCorrect_P;
		}

		if (m_render_target_debug == RenderTarget_Gbuffer_Normal)
		{
			shader_type = Shader_DebugNormal_P;
		}

		if (m_render_target_debug == RenderTarget_Gbuffer_Material)
		{
			shader_type = Shader_Texture_P;
		}

        if (m_render_target_debug == RenderTarget_Light_Diffuse)
        {
            shader_type = Shader_DebugChannelRgbGammaCorrect_P;
        }

        if (m_render_target_debug == RenderTarget_Light_Specular)
        {
            shader_type = Shader_DebugChannelRgbGammaCorrect_P;
        }

		if (m_render_target_debug == RenderTarget_Gbuffer_Velocity)
		{
			shader_type = Shader_DebugVelocity_P;
		}

		if (m_render_target_debug == RenderTarget_Gbuffer_Depth)
		{
			shader_type = Shader_DebugChannelR_P;
		}

		if (m_render_target_debug == RenderTarget_Hbao)
		{
			texture     = m_options & Render_Hbao ? m_render_targets[RenderTarget_Hbao].get() : m_tex_white.get();
			shader_type = Shader_Texture_P;
		}

        if (m_render_target_debug == RenderTarget_Hbao_Noisy)
        {
            texture = m_options & Render_Hbao ? m_render_targets[RenderTarget_Hbao_Noisy].get() : m_tex_white.get();
            shader_type = Shader_Texture_P;
        }

        if (m_render_target_debug == RenderTarget_Ssr)
        {
            shader_type = Shader_DebugChannelRgbGammaCorrect_P;
        }

        if (m_render_target_debug == RenderTarget_Bloom)
        {
            texture     = m_render_tex_bloom.front().get();
            shader_type = Shader_DebugChannelRgbGammaCorrect_P;
        }

        if (m_render_target_debug == RenderTarget_Dof_Half)
        {
            texture = m_render_targets[RenderTarget_Dof_Half].get();
            shader_type = Shader_DebugChannelRgbGammaCorrect_P;
        }

        if (m_render_target_debug == RenderTarget_Dof_Half_2)
        {
            texture = m_render_targets[RenderTarget_Dof_Half_2].get();
            shader_type = Shader_DebugChannelRgbGammaCorrect_P;
        }

        if (m_render_target_debug == RenderTarget_Light_Volumetric)
        {
            shader_type = Shader_DebugChannelRgbGammaCorrect_P;
        }

        if (m_render_target_debug == RenderTarget_Brdf_Specular_Lut)
        {
            shader_type = Shader_Texture_P;
        }

        // Acquire shaders
        RHI_Shader* shader_v = m_shaders[Shader_Quad_V].get();
        RHI_Shader* shader_p = m_shaders[shader_type].get();
        if (!shader_v->IsCompiled() || !shader_p->IsCompiled())
            return false;

        // Set render state
        static RHI_PipelineState pipeline_state;
        pipeline_state.shader_vertex                    = shader_v;
        pipeline_state.shader_pixel                     = shader_p;
        pipeline_state.rasterizer_state                 = m_rasterizer_cull_back_solid.get();
        pipeline_state.blend_state                      = m_blend_disabled.get();
        pipeline_state.depth_stencil_state              = m_depth_stencil_off_off.get();
        pipeline_state.vertex_buffer_stride             = m_viewport_quad.GetVertexBuffer()->GetStride();
        pipeline_state.render_target_color_textures[0]  = tex_out.get();
        pipeline_state.clear_color[0]                   = state_color_dont_care;
        pipeline_state.primitive_topology               = RHI_PrimitiveTopology_TriangleList;
        pipeline_state.viewport                         = tex_out->GetViewport();
        pipeline_state.pass_name                        = "Pass_DebugBuffer";

        // Record commands
        if (cmd_list->BeginRenderPass(pipeline_state))
        {
            // Update uber buffer
            m_buffer_uber_cpu.resolution    = Vector2(static_cast<float>(tex_out->GetWidth()), static_cast<float>(tex_out->GetHeight()));
            m_buffer_uber_cpu.transform     = m_buffer_frame_cpu.view_projection_ortho;
            UpdateUberBuffer(cmd_list);

            cmd_list->SetTexture(28, texture);
            cmd_list->SetBufferVertex(m_viewport_quad.GetVertexBuffer());
            cmd_list->SetBufferIndex(m_viewport_quad.GetIndexBuffer());
            cmd_list->DrawIndexed(Rectangle::GetIndexCount());
            cmd_list->EndRenderPass();
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
        RHI_Texture* render_target = m_render_targets[RenderTarget_Brdf_Specular_Lut].get();

        // Set render state
        static RHI_PipelineState pipeline_state;
        pipeline_state.shader_vertex                    = shader_v.get();
        pipeline_state.shader_pixel                     = shader_p.get();
        pipeline_state.rasterizer_state                 = m_rasterizer_cull_back_solid.get();
        pipeline_state.blend_state                      = m_blend_disabled.get();
        pipeline_state.depth_stencil_state              = m_depth_stencil_off_off.get();
        pipeline_state.vertex_buffer_stride             = m_viewport_quad.GetVertexBuffer()->GetStride();
        pipeline_state.render_target_color_textures[0]  = render_target;
        pipeline_state.clear_color[0]                   = state_color_dont_care;
        pipeline_state.viewport                         = render_target->GetViewport();
        pipeline_state.primitive_topology               = RHI_PrimitiveTopology_TriangleList;
        pipeline_state.pass_name                        = "Pass_BrdfSpecularLut";

        // Record commands
        if (cmd_list->BeginRenderPass(pipeline_state))
        {
            // Update uber buffer
            m_buffer_uber_cpu.resolution = Vector2(static_cast<float>(render_target->GetWidth()), static_cast<float>(render_target->GetHeight()));
            UpdateUberBuffer(cmd_list);

            cmd_list->SetBufferVertex(m_viewport_quad.GetVertexBuffer());
            cmd_list->SetBufferIndex(m_viewport_quad.GetIndexBuffer());
            cmd_list->DrawIndexed(Rectangle::GetIndexCount()); 
            cmd_list->EndRenderPass();

            m_brdf_specular_lut_rendered = true;
        }
    }

    void Renderer::Pass_Copy(RHI_CommandList* cmd_list, RHI_Texture* tex_in, RHI_Texture* tex_out)
    {
        // Acquire shaders
        RHI_Shader* shader_c = m_shaders[Shader_Copy_C].get();
        if (!shader_c->IsCompiled())
            return;

        // Set render state
        static RHI_PipelineState pipeline_state;
        pipeline_state.shader_compute = shader_c;
        pipeline_state.pass_name      = "Pass_Copy";

        // Draw
        if (cmd_list->BeginRenderPass(pipeline_state))
        {
            uint32_t thread_group_count_x   = static_cast<uint32_t>(Math::Helper::Ceil(static_cast<float>(tex_out->GetWidth()) / m_thread_group_count));
            uint32_t thread_group_count_y   = static_cast<uint32_t>(Math::Helper::Ceil(static_cast<float>(tex_out->GetHeight()) / m_thread_group_count));
            uint32_t thread_group_count_z   = 1;
            bool async                      = false;

            cmd_list->SetTexture(3, tex_out, true);
            cmd_list->SetTexture(28, tex_in);
            cmd_list->Dispatch(thread_group_count_x, thread_group_count_y, thread_group_count_z, async);
            cmd_list->EndRenderPass();
        }
    }
}
