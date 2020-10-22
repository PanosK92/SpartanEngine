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
#include "../World/Components/Camera.h"
#include "../World/Components/Light.h"
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
        cmd_list->SetConstantBuffer(1, RHI_Shader_Compute, m_buffer_material_gpu);
        cmd_list->SetConstantBuffer(2, RHI_Shader_Vertex | RHI_Shader_Pixel | RHI_Shader_Compute, m_buffer_uber_gpu);
        cmd_list->SetConstantBuffer(3, RHI_Shader_Vertex | RHI_Shader_Compute, m_buffer_object_gpu);
        cmd_list->SetConstantBuffer(4, RHI_Shader_Compute, m_buffer_light_gpu);
        
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

        // Validate command list state
        if (!cmd_list->IsRecording())
            return;

        SCOPED_TIME_BLOCK(m_profiler);

        Pass_UpdateFrameBuffer(cmd_list);
        
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
            Pass_GBuffer(cmd_list);
            Pass_Ssr(cmd_list);
            Pass_Hbao(cmd_list);
            Pass_Ssgi(cmd_list);
            Pass_Light(cmd_list);
            Pass_Composition(cmd_list, m_render_targets[RendererRt::Frame_Hdr]);
        
            // Lighting for transparent objects (skip ssr, hbao and ssgi as they will not be that noticeable anyway)
            if (draw_transparent_objects)
            {
                // save a copy of the opaque composition, so that the transparent one can use it
                Pass_Copy(cmd_list, m_render_targets[RendererRt::Frame_Hdr].get(), m_render_targets[RendererRt::Frame_Hdr_2].get());

                Pass_GBuffer(cmd_list, true);
                Pass_Light(cmd_list, true);
                Pass_Composition(cmd_list, m_render_targets[RendererRt::Frame_Hdr], true);
            }
        }
        
        // Post-processing
        {
            Pass_PostProcess(cmd_list);
            Pass_Outline(cmd_list, m_render_targets[RendererRt::Frame_Ldr]);
            Pass_TransformHandle(cmd_list, m_render_targets[RendererRt::Frame_Ldr].get());
            Pass_Lines(cmd_list, m_render_targets[RendererRt::Frame_Ldr]);
            Pass_Icons(cmd_list, m_render_targets[RendererRt::Frame_Ldr].get());
            Pass_DebugBuffer(cmd_list, m_render_targets[RendererRt::Frame_Ldr]);
            Pass_Text(cmd_list, m_render_targets[RendererRt::Frame_Ldr].get());
        }
    }

    void Renderer::Pass_UpdateFrameBuffer(RHI_CommandList* cmd_list)
    {
        // TODO: An empty pipeline should create an empty/basic render pass so buffers can be updated.
        // For the time being, I just provided a dummy compute shader.

        // Acquire shaders
        RHI_Shader* shader_c = m_shaders[RendererShader::Copy_C].get();
        if (!shader_c->IsCompiled())
            return;

        // Set render state
        static RHI_PipelineState pipeline_state;
        pipeline_state.shader_compute   = shader_c;
        pipeline_state.pass_name        = "Pass_UpdateFrameBuffer";

        // Draw
        if (cmd_list->BeginRenderPass(pipeline_state))
        {
            UpdateFrameBuffer(cmd_list);
            cmd_list->EndRenderPass();
        }

    }

    void Renderer::Pass_LightDepth(RHI_CommandList* cmd_list, const Renderer_Object_Type object_type)
    {
        // All opaque objects are rendered from the lights point of view.
        // Opaque objects write their depth information to a depth buffer, using just a vertex shader.
        // Transparent objects, read the opaque depth but don't write their own, instead, they write their color information using a pixel shader.

        // Acquire shader
        RHI_Shader* shader_v = m_shaders[RendererShader::Depth_V].get();
        RHI_Shader* shader_p = m_shaders[RendererShader::Depth_P].get();
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
            pipeline_state.clear_stencil                    = rhi_stencil_dont_care;
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
                pipeline_state.clear_depth    = transparent_pass ? rhi_depth_load : GetClearDepth();

                const Matrix& view_projection = light->GetViewMatrix(array_index) * light->GetProjectionMatrix(array_index);

                // Set appropriate rasterizer state
                if (light->GetLightType() == LightType::Directional)
                {
                    // "Pancaking" - https://www.gamedev.net/forums/topic/639036-shadow-mapping-and-high-up-objects/
                    // It's basically a way to capture the silhouettes of potential shadow casters behind the light's view point.
                    // Of course we also have to make sure that the light doesn't cull them in the first place (this is done automatically by the light)
                    pipeline_state.rasterizer_state = m_rasterizer_light_directional.get();
                }
                else
                {
                    pipeline_state.rasterizer_state = m_rasterizer_light_point_spot.get();
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
                        cmd_list->SetTexture(RendererBindingsSrv::tex, tex_albedo ? tex_albedo : m_default_tex_white.get());

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
        const auto& shader_depth    = m_shaders[RendererShader::Depth_V];
        const auto& tex_depth       = m_render_targets[RendererRt::Gbuffer_Depth];
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

    void Renderer::Pass_GBuffer(RHI_CommandList* cmd_list, const bool is_transparent_pass /*= false*/)
    {
        // Acquire required resources/shaders
        RHI_Texture* tex_albedo       = m_render_targets[RendererRt::Gbuffer_Albedo].get();
        RHI_Texture* tex_normal       = m_render_targets[RendererRt::Gbuffer_Normal].get();
        RHI_Texture* tex_material     = m_render_targets[RendererRt::Gbuffer_Material].get();
        RHI_Texture* tex_velocity     = m_render_targets[RendererRt::Gbuffer_Velocity].get();
        RHI_Texture* tex_depth        = m_render_targets[RendererRt::Gbuffer_Depth].get();
        RHI_Shader* shader_v          = m_shaders[RendererShader::Gbuffer_V].get();
        ShaderGBuffer* shader_p       = static_cast<ShaderGBuffer*>(m_shaders[RendererShader::Gbuffer_P].get());

        // Validate that the shader has compiled
        if (!shader_v->IsCompiled())
            return;

        // Set render state
        RHI_PipelineState pso;
        pso.shader_vertex                   = shader_v;
        pso.vertex_buffer_stride            = static_cast<uint32_t>(sizeof(RHI_Vertex_PosTexNorTan)); // assume all vertex buffers have the same stride (which they do)
        pso.blend_state                     = m_blend_disabled.get();
        pso.rasterizer_state                = GetOption(Render_Debug_Wireframe) ? m_rasterizer_cull_back_wireframe.get() : m_rasterizer_cull_back_solid.get();
        pso.depth_stencil_state             = is_transparent_pass ? m_depth_stencil_on_on_w.get() : m_depth_stencil_on_off_w.get(); // GetOptionValue(Render_DepthPrepass) is not accounted for anymore, have to fix
        pso.render_target_color_textures[0] = tex_albedo;
        pso.clear_color[0]                  = !is_transparent_pass ? Vector4::Zero : rhi_color_load;
        pso.render_target_color_textures[1] = tex_normal;
        pso.clear_color[1]                  = !is_transparent_pass ? Vector4::Zero : rhi_color_load;
        pso.render_target_color_textures[2] = tex_material;
        pso.clear_color[2]                  = !is_transparent_pass ? Vector4::Zero : rhi_color_load;
        pso.render_target_color_textures[3] = tex_velocity;
        pso.clear_color[3]                  = !is_transparent_pass ? Vector4::Zero : rhi_color_load;
        pso.render_target_depth_texture     = tex_depth;
        pso.clear_depth                     = is_transparent_pass || GetOption(Render_DepthPrepass) ? rhi_depth_load : GetClearDepth();
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
            auto& entities = m_entities[is_transparent_pass ? Renderer_Object_Transparent : Renderer_Object_Opaque];

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
                if (material->GetColorAlbedo().w == 0 && is_transparent_pass)
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
                const bool firs_run       = material_index == 0;
                const bool new_material   = material_bound_id != material->GetId();
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
                    cmd_list->SetTexture(RendererBindingsSrv::material_albedo, material->GetTexture_Ptr(Material_Color));
                    cmd_list->SetTexture(RendererBindingsSrv::material_roughness, material->GetTexture_Ptr(Material_Roughness));
                    cmd_list->SetTexture(RendererBindingsSrv::material_metallic, material->GetTexture_Ptr(Material_Metallic));
                    cmd_list->SetTexture(RendererBindingsSrv::material_normal, material->GetTexture_Ptr(Material_Normal));
                    cmd_list->SetTexture(RendererBindingsSrv::material_height, material->GetTexture_Ptr(Material_Height));
                    cmd_list->SetTexture(RendererBindingsSrv::material_occlusion, material->GetTexture_Ptr(Material_Occlusion));
                    cmd_list->SetTexture(RendererBindingsSrv::material_emission, material->GetTexture_Ptr(Material_Emission));
                    cmd_list->SetTexture(RendererBindingsSrv::material_mask, material->GetTexture_Ptr(Material_Mask));
                
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
    }

    void Renderer::Pass_Ssgi(RHI_CommandList* cmd_list)
    {
        if ((m_options & Render_Ssgi) == 0)
            return;

        // Acquire shaders
        RHI_Shader* shader_c = m_shaders[RendererShader::Ssgi_C].get();
        if (!shader_c->IsCompiled())
            return;

        // Get render target
        RHI_Texture* tex_out            = m_render_targets[RendererRt::Ssgi].get();
        RHI_Texture* tex_accumulation   = m_render_targets[RendererRt::Accumulation_Ssgi].get();

        // Set render state
        static RHI_PipelineState pipeline_state;
        pipeline_state.shader_compute   = shader_c;
        pipeline_state.pass_name        = "Pass_Ssgi";

        // Draw
        if (cmd_list->BeginRenderPass(pipeline_state))
        {
            // Update uber buffer
            m_buffer_uber_cpu.resolution = Vector2(tex_out->GetWidth(), tex_out->GetHeight());
            UpdateUberBuffer(cmd_list);

            const uint32_t thread_group_count_x   = static_cast<uint32_t>(Math::Helper::Ceil(static_cast<float>(tex_out->GetWidth()) / m_thread_group_count));
            const uint32_t thread_group_count_y   = static_cast<uint32_t>(Math::Helper::Ceil(static_cast<float>(tex_out->GetHeight()) / m_thread_group_count));
            const uint32_t thread_group_count_z   = 1;
            const bool async                      = false;

            cmd_list->SetTexture(RendererBindingsUav::rgb, tex_out);
            cmd_list->SetTexture(RendererBindingsSrv::gbuffer_normal, m_render_targets[RendererRt::Gbuffer_Normal]);
            cmd_list->SetTexture(RendererBindingsSrv::gbuffer_material, m_render_targets[RendererRt::Gbuffer_Material]);
            cmd_list->SetTexture(RendererBindingsSrv::gbuffer_velocity, m_render_targets[RendererRt::Gbuffer_Velocity]);
            cmd_list->SetTexture(RendererBindingsSrv::gbuffer_depth, m_render_targets[RendererRt::Gbuffer_Depth]);
            cmd_list->SetTexture(RendererBindingsSrv::light_diffuse, m_render_targets[RendererRt::Light_Diffuse]);
            cmd_list->SetTexture(RendererBindingsSrv::light_specular, m_render_targets[RendererRt::Light_Specular]);
            cmd_list->SetTexture(RendererBindingsSrv::ssr, (m_options & Render_ScreenSpaceReflections) ? m_render_targets[RendererRt::Ssr] : m_default_tex_transparent);
            cmd_list->SetTexture(RendererBindingsSrv::tex, tex_accumulation);
            cmd_list->Dispatch(thread_group_count_x, thread_group_count_y, thread_group_count_z, async);
            cmd_list->EndRenderPass();
        }

        // Accumulate
        Pass_Copy(cmd_list, tex_out, tex_accumulation);
    }

    void Renderer::Pass_Hbao(RHI_CommandList* cmd_list)
    {
        if ((m_options & Render_Hbao) == 0)
            return;

        // Acquire shaders
        RHI_Shader* shader_c = m_shaders[RendererShader::Hbao_C].get();
        if (!shader_c->IsCompiled())
            return;

        // Acquire textures
        shared_ptr<RHI_Texture>& tex_hbao_noisy     = m_render_targets[RendererRt::Hbao];
        shared_ptr<RHI_Texture>& tex_hbao_blurred   = m_render_targets[RendererRt::Hbao_Blurred];
        RHI_Texture* tex_depth                      = m_render_targets[RendererRt::Gbuffer_Depth].get();
        RHI_Texture* tex_normal                     = m_render_targets[RendererRt::Gbuffer_Normal].get();

        // Set render state
        static RHI_PipelineState pipeline_state;
        pipeline_state.shader_compute   = shader_c;
        pipeline_state.pass_name        = "Pass_Hbao";

        // Draw
        if (cmd_list->BeginRenderPass(pipeline_state))
        {
            // Update uber buffer
            m_buffer_uber_cpu.resolution = Vector2(static_cast<float>(tex_hbao_noisy->GetWidth()), static_cast<float>(tex_hbao_noisy->GetHeight()));
            UpdateUberBuffer(cmd_list);

            const uint32_t thread_group_count_x = static_cast<uint32_t>(Math::Helper::Ceil(static_cast<float>(tex_hbao_noisy->GetWidth()) / m_thread_group_count));
            const uint32_t thread_group_count_y = static_cast<uint32_t>(Math::Helper::Ceil(static_cast<float>(tex_hbao_noisy->GetHeight()) / m_thread_group_count));
            const uint32_t thread_group_count_z = 1;
            const bool async = false;

            cmd_list->SetTexture(RendererBindingsUav::r, tex_hbao_noisy);
            cmd_list->SetTexture(RendererBindingsSrv::gbuffer_normal, tex_normal);
            cmd_list->SetTexture(RendererBindingsSrv::gbuffer_depth, tex_depth);
            cmd_list->Dispatch(thread_group_count_x, thread_group_count_y, thread_group_count_z, async);
            cmd_list->EndRenderPass();

            // Bilateral blur
            const auto sigma = 2.0f;
            const auto pixel_stride = 2.0f;
            Pass_BlurBilateralGaussian(
                cmd_list,
                tex_hbao_noisy,
                tex_hbao_blurred,
                sigma,
                pixel_stride,
                false
            );
        }
    }

    void Renderer::Pass_Ssr(RHI_CommandList* cmd_list)
    {
        if ((m_options & Render_ScreenSpaceReflections) == 0)
            return;

        // Acquire shaders
        RHI_Shader* shader_c = m_shaders[RendererShader::Ssr_C].get();
        if (!shader_c->IsCompiled())
            return;

        // Acquire render targets
        RHI_Texture* tex_out = m_render_targets[RendererRt::Ssr].get();

        // Set render state
        static RHI_PipelineState pipeline_state;
        pipeline_state.shader_compute   = shader_c;
        pipeline_state.pass_name        = "Pass_Ssr";

        // Draw
        if (cmd_list->BeginRenderPass(pipeline_state))
        {
            // Update uber buffer
            m_buffer_uber_cpu.resolution = Vector2(static_cast<float>(tex_out->GetWidth()), static_cast<float>(tex_out->GetHeight()));
            UpdateUberBuffer(cmd_list);

            const uint32_t thread_group_count_x = static_cast<uint32_t>(Math::Helper::Ceil(static_cast<float>(tex_out->GetWidth()) / m_thread_group_count));
            const uint32_t thread_group_count_y = static_cast<uint32_t>(Math::Helper::Ceil(static_cast<float>(tex_out->GetHeight()) / m_thread_group_count));
            const uint32_t thread_group_count_z = 1;
            const bool async = false;

            cmd_list->SetTexture(RendererBindingsUav::rg, tex_out);
            cmd_list->SetTexture(RendererBindingsSrv::gbuffer_normal, m_render_targets[RendererRt::Gbuffer_Normal]);
            cmd_list->SetTexture(RendererBindingsSrv::gbuffer_depth, m_render_targets[RendererRt::Gbuffer_Depth]);
            cmd_list->Dispatch(thread_group_count_x, thread_group_count_y, thread_group_count_z, async);
            cmd_list->EndRenderPass();
        }
    }

    void Renderer::Pass_Light(RHI_CommandList* cmd_list, const bool is_transparent_pass /*= false*/)
    {
        // Acquire lights
        const vector<Entity*>& entities = m_entities[Renderer_Object_Light];
        if (entities.empty())
            return;

        // Acquire render targets
        RHI_Texture* tex_diffuse    = is_transparent_pass ? m_render_targets[RendererRt::Light_Diffuse_Transparent].get()   : m_render_targets[RendererRt::Light_Diffuse].get();
        RHI_Texture* tex_specular   = is_transparent_pass ? m_render_targets[RendererRt::Light_Specular_Transparent].get()  : m_render_targets[RendererRt::Light_Specular].get();
        RHI_Texture* tex_volumetric = m_render_targets[RendererRt::Light_Volumetric].get();

        // Clear render targets
        cmd_list->ClearRenderTarget(tex_diffuse,    0, 0, true, Vector4::Zero);
        cmd_list->ClearRenderTarget(tex_specular,   0, 0, true, Vector4::Zero);
        cmd_list->ClearRenderTarget(tex_volumetric, 0, 0, true, Vector4::Zero);

        // Set render state
        static RHI_PipelineState pipeline_state;
        pipeline_state.pass_name = "Pass_Light";

        // Iterate through all the light entities
        for (const auto& entity : entities)
        {
            if (Light* light = entity->GetComponent<Light>())
            {
                if (light->GetIntensity() != 0)
                {
                    // Set pixel shader
                    pipeline_state.shader_compute = static_cast<RHI_Shader*>(ShaderLight::GetVariation(m_context, light, m_options, is_transparent_pass));

                    // Skip the shader until it compiles or the users spots a compilation error
                    if (!pipeline_state.shader_compute->IsCompiled())
                        continue;

                    // Draw
                    if (cmd_list->BeginRenderPass(pipeline_state))
                    {
                        // Update constant buffer (light pass will access it using material IDs)
                        UpdateMaterialBuffer(cmd_list);

                        cmd_list->SetTexture(RendererBindingsUav::rgb,              tex_diffuse);
                        cmd_list->SetTexture(RendererBindingsUav::rgb2,             tex_specular);
                        cmd_list->SetTexture(RendererBindingsUav::rgb3,             tex_volumetric);
                        cmd_list->SetTexture(RendererBindingsSrv::gbuffer_albedo,   m_render_targets[RendererRt::Gbuffer_Albedo]);
                        cmd_list->SetTexture(RendererBindingsSrv::gbuffer_normal,   m_render_targets[RendererRt::Gbuffer_Normal]);
                        cmd_list->SetTexture(RendererBindingsSrv::gbuffer_material, m_render_targets[RendererRt::Gbuffer_Material]);
                        cmd_list->SetTexture(RendererBindingsSrv::gbuffer_depth,    m_render_targets[RendererRt::Gbuffer_Depth]);
                        cmd_list->SetTexture(RendererBindingsSrv::hbao,             (m_options & Render_Hbao) ? m_render_targets[RendererRt::Hbao_Blurred] : m_default_tex_white);
                        cmd_list->SetTexture(RendererBindingsSrv::ssr,              (m_options & Render_ScreenSpaceReflections) ? m_render_targets[RendererRt::Ssr] : m_default_tex_transparent);
                        cmd_list->SetTexture(RendererBindingsSrv::frame,            m_render_targets[RendererRt::Frame_Hdr_2]); // previous frame before post-processing

                        // Set shadow map
                        if (light->GetShadowsEnabled())
                        {
                            RHI_Texture* tex_depth = light->GetDepthTexture();
                            RHI_Texture* tex_color = light->GetShadowsTransparentEnabled() ? light->GetColorTexture() : m_default_tex_white.get();

                            if (light->GetLightType() == LightType::Directional)
                            {
                                cmd_list->SetTexture(RendererBindingsSrv::light_directional_depth, tex_depth);
                                cmd_list->SetTexture(RendererBindingsSrv::light_directional_color, tex_color);
                            }
                            else if (light->GetLightType() == LightType::Point)
                            {
                                cmd_list->SetTexture(RendererBindingsSrv::light_point_depth, tex_depth);
                                cmd_list->SetTexture(RendererBindingsSrv::light_point_color, tex_color);
                            }
                            else if (light->GetLightType() == LightType::Spot)
                            {
                                cmd_list->SetTexture(RendererBindingsSrv::light_spot_depth, tex_depth);
                                cmd_list->SetTexture(RendererBindingsSrv::light_spot_color, tex_color);
                            }
                        }

                        // Update light buffer
                        UpdateLightBuffer(cmd_list, light);

                        // Update uber buffer
                        m_buffer_uber_cpu.resolution = Vector2(static_cast<float>(tex_diffuse->GetWidth()), static_cast<float>(tex_diffuse->GetHeight()));
                        UpdateUberBuffer(cmd_list);

                        const uint32_t thread_group_count_x = static_cast<uint32_t>(Math::Helper::Ceil(static_cast<float>(tex_diffuse->GetWidth()) / m_thread_group_count));
                        const uint32_t thread_group_count_y = static_cast<uint32_t>(Math::Helper::Ceil(static_cast<float>(tex_diffuse->GetHeight()) / m_thread_group_count));
                        const uint32_t thread_group_count_z = 1;
                        const bool async = false;

                        cmd_list->Dispatch(thread_group_count_x, thread_group_count_y, thread_group_count_z, async);
                        cmd_list->EndRenderPass();
                    }
                }
            }
        }
    }

    void Renderer::Pass_Composition(RHI_CommandList* cmd_list, shared_ptr<RHI_Texture>& tex_out, const bool is_transparent_pass /*= false*/)
    {
        // Acquire shaders
        RHI_Shader* shader_v = m_shaders[RendererShader::Quad_V].get();
        RHI_Shader* shader_p = is_transparent_pass ? m_shaders[RendererShader::Composition_Transparent_P].get() : m_shaders[RendererShader::Composition_P].get();
        if (!shader_v->IsCompiled() || !shader_p->IsCompiled())
            return;

        RHI_Texture* tex_depth = m_render_targets[RendererRt::Gbuffer_Depth].get();

        // Set render state
        static RHI_PipelineState pipeline_state;
        pipeline_state.shader_vertex                            = shader_v;
        pipeline_state.shader_pixel                             = shader_p;
        pipeline_state.rasterizer_state                         = m_rasterizer_cull_back_solid.get();
        pipeline_state.depth_stencil_state                      = is_transparent_pass ? m_depth_stencil_off_on_r.get() : m_depth_stencil_off_off.get();
        pipeline_state.blend_state                              = m_blend_disabled.get();
        pipeline_state.vertex_buffer_stride                     = m_viewport_quad.GetVertexBuffer()->GetStride();
        pipeline_state.render_target_color_textures[0]          = tex_out.get();
        pipeline_state.clear_color[0]                           = is_transparent_pass ? rhi_color_load : rhi_color_dont_care;
        pipeline_state.render_target_depth_texture              = is_transparent_pass ? tex_depth : nullptr;
        pipeline_state.render_target_depth_texture_read_only    = is_transparent_pass;
        pipeline_state.clear_stencil                            = is_transparent_pass ? rhi_stencil_load : rhi_stencil_dont_care;
        pipeline_state.viewport                                 = tex_out->GetViewport();
        pipeline_state.primitive_topology                       = RHI_PrimitiveTopology_TriangleList;
        pipeline_state.pass_name                                = "Pass_Composition";

        // Begin commands
        if (cmd_list->BeginRenderPass(pipeline_state))
        {
            // Update uber buffer
            m_buffer_uber_cpu.resolution = Vector2(static_cast<float>(tex_out->GetWidth()), static_cast<float>(tex_out->GetHeight()));
            UpdateUberBuffer(cmd_list);

            // Setup command list
            cmd_list->SetBufferVertex(m_viewport_quad.GetVertexBuffer());
            cmd_list->SetBufferIndex(m_viewport_quad.GetIndexBuffer());
            cmd_list->SetTexture(RendererBindingsSrv::gbuffer_albedo, m_render_targets[RendererRt::Gbuffer_Albedo]);
            cmd_list->SetTexture(RendererBindingsSrv::gbuffer_normal, m_render_targets[RendererRt::Gbuffer_Normal]);
            cmd_list->SetTexture(RendererBindingsSrv::gbuffer_material, m_render_targets[RendererRt::Gbuffer_Material]);
            cmd_list->SetTexture(RendererBindingsSrv::gbuffer_depth, tex_depth);
            cmd_list->SetTexture(RendererBindingsSrv::hbao, (m_options & Render_Hbao)       ? m_render_targets[RendererRt::Hbao_Blurred]                : m_default_tex_white);
            cmd_list->SetTexture(RendererBindingsSrv::light_diffuse, is_transparent_pass    ? m_render_targets[RendererRt::Light_Diffuse_Transparent]   : m_render_targets[RendererRt::Light_Diffuse]);
            cmd_list->SetTexture(RendererBindingsSrv::light_specular, is_transparent_pass   ? m_render_targets[RendererRt::Light_Specular_Transparent]  : m_render_targets[RendererRt::Light_Specular]);
            cmd_list->SetTexture(RendererBindingsSrv::light_volumetric, m_render_targets[RendererRt::Light_Volumetric]);
            cmd_list->SetTexture(RendererBindingsSrv::ssr, (m_options & Render_ScreenSpaceReflections) ? m_render_targets[RendererRt::Ssr] : m_default_tex_transparent);
            cmd_list->SetTexture(RendererBindingsSrv::frame, m_render_targets[RendererRt::Frame_Hdr_2]);
            cmd_list->SetTexture(RendererBindingsSrv::lutIbl, m_render_targets[RendererRt::Brdf_Specular_Lut]);
            cmd_list->SetTexture(RendererBindingsSrv::environment, GetEnvironmentTexture());
            cmd_list->SetTexture(RendererBindingsSrv::ssgi, (m_options & Render_Ssgi) ? m_render_targets[RendererRt::Ssgi] : m_default_tex_black);
            cmd_list->SetBufferIndex(m_viewport_quad.GetIndexBuffer());
            cmd_list->SetBufferVertex(m_viewport_quad.GetVertexBuffer());
            cmd_list->DrawIndexed(Rectangle::GetIndexCount());
            cmd_list->EndRenderPass();
        }
    }
    
    void Renderer::Pass_PostProcess(RHI_CommandList* cmd_list)
    {
        // IN:  RenderTarget_Composition_Hdr
        // OUT: RenderTarget_Composition_Ldr

        // Acquire render targets
        auto& tex_in_hdr    = m_render_targets[RendererRt::Frame_Hdr];
        auto& tex_out_hdr   = m_render_targets[RendererRt::Frame_Hdr_2];
        auto& tex_in_ldr    = m_render_targets[RendererRt::Frame_Ldr];
        auto& tex_out_ldr   = m_render_targets[RendererRt::Frame_Ldr_2];

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
            Pass_Copy(cmd_list, tex_in_hdr.get(), tex_in_ldr.get()); // clipping
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
    
    void Renderer::Pass_BlurBox(RHI_CommandList* cmd_list, shared_ptr<RHI_Texture>& tex_in, shared_ptr<RHI_Texture>& tex_out, const float sigma, const float pixel_stride, const bool use_stencil)
    {
        // Acquire shaders
        const auto& shader_v = m_shaders[RendererShader::Quad_V];
        const auto& shader_p = m_shaders[RendererShader::BlurBox_P];
        if (!shader_v->IsCompiled() || !shader_p->IsCompiled())
            return;

        // Set render state
        static RHI_PipelineState pipeline_state         = {};
        pipeline_state.shader_vertex                    = shader_v.get();
        pipeline_state.shader_pixel                     = shader_p.get();
        pipeline_state.rasterizer_state                 = m_rasterizer_cull_back_solid.get();
        pipeline_state.blend_state                      = m_blend_disabled.get();
        pipeline_state.depth_stencil_state              = use_stencil ? m_depth_stencil_off_on_r.get() : m_depth_stencil_off_off.get();
        pipeline_state.vertex_buffer_stride             = m_viewport_quad.GetVertexBuffer()->GetStride();
        pipeline_state.render_target_color_textures[0]  = tex_out.get();
        pipeline_state.clear_color[0]                   = rhi_color_dont_care;
        pipeline_state.render_target_depth_texture      = use_stencil ? m_render_targets[RendererRt::Gbuffer_Depth].get() : nullptr;
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
            cmd_list->SetTexture(RendererBindingsSrv::tex, tex_in);
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
        const auto& shader_v = m_shaders[RendererShader::Quad_V];
        const auto& shader_p = m_shaders[RendererShader::BlurGaussian_P];
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
        pipeline_state_horizontal.clear_color[0]                    = rhi_color_dont_care;
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
            cmd_list->SetTexture(RendererBindingsSrv::tex, tex_in);
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
        pipeline_state_vertical.clear_color[0]                  = rhi_color_dont_care;
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
            cmd_list->SetTexture(RendererBindingsSrv::tex, tex_out);
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
        const auto& shader_v = m_shaders[RendererShader::Quad_V];
        const auto& shader_p = m_shaders[RendererShader::BlurGaussianBilateral_P];
        if (!shader_v->IsCompiled() || !shader_p->IsCompiled())
            return;

        // Acquire render targets
        RHI_Texture* tex_depth     = m_render_targets[RendererRt::Gbuffer_Depth].get();
        RHI_Texture* tex_normal    = m_render_targets[RendererRt::Gbuffer_Normal].get();

        // Set render state for horizontal pass
        static RHI_PipelineState pipeline_state_horizontal;
        pipeline_state_horizontal.shader_vertex                     = shader_v.get();
        pipeline_state_horizontal.shader_pixel                      = shader_p.get();
        pipeline_state_horizontal.rasterizer_state                  = m_rasterizer_cull_back_solid.get();
        pipeline_state_horizontal.blend_state                       = m_blend_disabled.get();
        pipeline_state_horizontal.depth_stencil_state               = use_stencil ? m_depth_stencil_off_on_r.get() : m_depth_stencil_off_off.get();
        pipeline_state_horizontal.vertex_buffer_stride              = m_viewport_quad.GetVertexBuffer()->GetStride();
        pipeline_state_horizontal.render_target_color_textures[0]   = tex_out.get();
        pipeline_state_horizontal.clear_color[0]                    = rhi_color_dont_care;
        pipeline_state_horizontal.render_target_depth_texture       = use_stencil ? tex_depth : nullptr;
        pipeline_state_horizontal.clear_stencil                     = use_stencil ? rhi_stencil_load : rhi_stencil_dont_care;
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
            cmd_list->SetTexture(RendererBindingsSrv::tex, tex_in);
            cmd_list->SetTexture(RendererBindingsSrv::gbuffer_depth, tex_depth);
            cmd_list->SetTexture(RendererBindingsSrv::gbuffer_normal, tex_normal);
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
        pipeline_state_vertical.clear_color[0]                  = rhi_color_dont_care;
        pipeline_state_vertical.render_target_depth_texture     = use_stencil ? tex_depth : nullptr;
        pipeline_state_vertical.clear_stencil                   = use_stencil ? rhi_stencil_load : rhi_stencil_dont_care;
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
            cmd_list->SetTexture(RendererBindingsSrv::tex, tex_out);
            cmd_list->SetTexture(RendererBindingsSrv::gbuffer_depth, tex_depth);
            cmd_list->SetTexture(RendererBindingsSrv::gbuffer_normal, tex_normal);
            cmd_list->DrawIndexed(m_viewport_quad.GetIndexCount());
            cmd_list->EndRenderPass();
        }

        // Swap textures
        tex_in.swap(tex_out);
    }
    
    void Renderer::Pass_TemporalAntialiasing(RHI_CommandList* cmd_list, shared_ptr<RHI_Texture>& tex_in, shared_ptr<RHI_Texture>& tex_out)
    {
        // Acquire shaders
        RHI_Shader* shader_c = m_shaders[RendererShader::Taa_C].get();
        if (!shader_c->IsCompiled())
            return;

        // Acquire accumulation render target
        auto& tex_accumulation = m_render_targets[RendererRt::Accumulation_Taa];

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

            const uint32_t thread_group_count_x = static_cast<uint32_t>(Math::Helper::Ceil(static_cast<float>(tex_out->GetWidth()) / m_thread_group_count));
            const uint32_t thread_group_count_y = static_cast<uint32_t>(Math::Helper::Ceil(static_cast<float>(tex_out->GetHeight()) / m_thread_group_count));
            const uint32_t thread_group_count_z = 1;
            const bool async = false;

            cmd_list->SetTexture(RendererBindingsUav::rgba, tex_out);
            cmd_list->SetTexture(RendererBindingsSrv::tex, tex_accumulation);
            cmd_list->SetTexture(RendererBindingsSrv::tex2, tex_in);
            cmd_list->SetTexture(RendererBindingsSrv::gbuffer_velocity, m_render_targets[RendererRt::Gbuffer_Velocity]);
            cmd_list->SetTexture(RendererBindingsSrv::gbuffer_depth, m_render_targets[RendererRt::Gbuffer_Depth]);
            cmd_list->Dispatch(thread_group_count_x, thread_group_count_y, thread_group_count_z, async);
            cmd_list->EndRenderPass();
        }

        // Accumulate
        Pass_Copy(cmd_list, tex_out.get(), tex_accumulation.get());
    }
    
    void Renderer::Pass_Bloom(RHI_CommandList* cmd_list, shared_ptr<RHI_Texture>& tex_in, shared_ptr<RHI_Texture>& tex_out)
    {
        // Acquire shaders
        RHI_Shader* shader_downsampleLuminance  = m_shaders[RendererShader::BloomDownsampleLuminance_C].get();
        RHI_Shader* shader_downsample           = m_shaders[RendererShader::BloomDownsample_C].get();
        RHI_Shader* shader_upsampleBlendMip     = m_shaders[RendererShader::BloomUpsampleBlendMip_C].get();
        RHI_Shader* shader_upsampleBlendFrame   = m_shaders[RendererShader::BloomUpsampleBlendFrame_C].get();
        if (!shader_downsampleLuminance->IsCompiled() || !shader_upsampleBlendMip->IsCompiled() || !shader_downsample->IsCompiled() || !shader_upsampleBlendFrame->IsCompiled())
            return;

        // Luminance
        {
            // Set render state
            static RHI_PipelineState pipeline_state;
            pipeline_state.shader_compute = shader_downsampleLuminance;
            pipeline_state.pass_name      = "Pass_BloomDownsampleLuminance";

            // Draw
            if (cmd_list->BeginRenderPass(pipeline_state))
            {
                // Update uber buffer
                m_buffer_uber_cpu.resolution = Vector2(static_cast<float>(m_render_tex_bloom[0].get()->GetWidth()), static_cast<float>(m_render_tex_bloom[0].get()->GetHeight()));
                UpdateUberBuffer(cmd_list);

                const uint32_t thread_group_count_x   = static_cast<uint32_t>(Math::Helper::Ceil(static_cast<float>(m_render_tex_bloom[0].get()->GetWidth()) / m_thread_group_count));
                const uint32_t thread_group_count_y   = static_cast<uint32_t>(Math::Helper::Ceil(static_cast<float>(m_render_tex_bloom[0].get()->GetHeight()) / m_thread_group_count));
                const uint32_t thread_group_count_z   = 1;
                const bool async                      = false;

                cmd_list->SetTexture(RendererBindingsUav::rgba, m_render_tex_bloom[0].get());
                cmd_list->SetTexture(RendererBindingsSrv::tex, tex_in);
                cmd_list->Dispatch(thread_group_count_x, thread_group_count_y, thread_group_count_z, async);
                cmd_list->EndRenderPass();
            }
        }
        
        // Downsample
        // The last bloom texture is the same size as the previous one (it's used for the Gaussian pass below), so we skip it
        for (int i = 0; i < static_cast<int>(m_render_tex_bloom.size() - 1); i++)
        {
            RHI_Texture* mip_small = m_render_tex_bloom[i + 1].get();
            RHI_Texture* mip_large = m_render_tex_bloom[i].get();

            // Set render state
            static RHI_PipelineState pipeline_state;
            pipeline_state.shader_compute   = shader_downsample;
            pipeline_state.pass_name        = "Pass_BloomDownsample";

            // Draw
            if (cmd_list->BeginRenderPass(pipeline_state))
            {
                // Update uber buffer
                m_buffer_uber_cpu.resolution = Vector2(static_cast<float>(mip_small->GetWidth()), static_cast<float>(mip_small->GetHeight()));
                UpdateUberBuffer(cmd_list);

                const uint32_t thread_group_count_x = static_cast<uint32_t>(Math::Helper::Ceil(static_cast<float>(mip_small->GetWidth()) / m_thread_group_count));
                const uint32_t thread_group_count_y = static_cast<uint32_t>(Math::Helper::Ceil(static_cast<float>(mip_small->GetHeight()) / m_thread_group_count));
                const uint32_t thread_group_count_z = 1;
                const bool async = false;

                cmd_list->SetTexture(RendererBindingsUav::rgba, mip_small);
                cmd_list->SetTexture(RendererBindingsSrv::tex, mip_large);
                cmd_list->Dispatch(thread_group_count_x, thread_group_count_y, thread_group_count_z, async);
                cmd_list->EndRenderPass();
            }
        }
        
        // Starting from the smallest mip, upsample and blend with the higher one
        for (int i = static_cast<int>(m_render_tex_bloom.size() - 1); i > 0; i--)
        {
            RHI_Texture* mip_small = m_render_tex_bloom[i].get();
            RHI_Texture* mip_large = m_render_tex_bloom[i - 1].get();

            // Set render state
            static RHI_PipelineState pipeline_state;
            pipeline_state.shader_compute   = shader_upsampleBlendMip;
            pipeline_state.pass_name        = "Pass_BloomUpsampleBlendMip";

            // Draw
            if (cmd_list->BeginRenderPass(pipeline_state))
            {
                // Update uber buffer
                m_buffer_uber_cpu.resolution = Vector2(static_cast<float>(mip_large->GetWidth()), static_cast<float>(mip_large->GetHeight()));
                UpdateUberBuffer(cmd_list);

                const uint32_t thread_group_count_x = static_cast<uint32_t>(Math::Helper::Ceil(static_cast<float>(mip_large->GetWidth()) / m_thread_group_count));
                const uint32_t thread_group_count_y = static_cast<uint32_t>(Math::Helper::Ceil(static_cast<float>(mip_large->GetHeight()) / m_thread_group_count));
                const uint32_t thread_group_count_z = 1;
                const bool async = false;

                cmd_list->SetTexture(RendererBindingsUav::rgba, mip_large);
                cmd_list->SetTexture(RendererBindingsSrv::tex, mip_small);
                cmd_list->Dispatch(thread_group_count_x, thread_group_count_y, thread_group_count_z, async);
                cmd_list->EndRenderPass();
            }
        }
        
        // Upsample mip 2 and blend with mip 1, then blend with the frame
        {
            // Set render state
            static RHI_PipelineState pipeline_state;
            pipeline_state.shader_compute   = shader_upsampleBlendFrame;
            pipeline_state.pass_name        = "Pass_BloomUpsampleBlendFrame";

            // Draw
            if (cmd_list->BeginRenderPass(pipeline_state))
            {
                // Update uber buffer
                m_buffer_uber_cpu.resolution = Vector2(static_cast<float>(tex_out->GetWidth()), static_cast<float>(tex_out->GetHeight()));
                UpdateUberBuffer(cmd_list);

                const uint32_t thread_group_count_x = static_cast<uint32_t>(Math::Helper::Ceil(static_cast<float>(tex_out->GetWidth()) / m_thread_group_count));
                const uint32_t thread_group_count_y = static_cast<uint32_t>(Math::Helper::Ceil(static_cast<float>(tex_out->GetHeight()) / m_thread_group_count));
                const uint32_t thread_group_count_z = 1;
                const bool async = false;

                cmd_list->SetTexture(RendererBindingsUav::rgba, tex_out.get());
                cmd_list->SetTexture(RendererBindingsSrv::tex, tex_in);
                cmd_list->SetTexture(RendererBindingsSrv::tex2, m_render_tex_bloom.front());
                cmd_list->Dispatch(thread_group_count_x, thread_group_count_y, thread_group_count_z, async);
                cmd_list->EndRenderPass();
            }
        }
    }
    
    void Renderer::Pass_ToneMapping(RHI_CommandList* cmd_list, shared_ptr<RHI_Texture>& tex_in, shared_ptr<RHI_Texture>& tex_out)
    {
        // Acquire shaders
        RHI_Shader* shader_c = m_shaders[RendererShader::ToneMapping_C].get();
        if (!shader_c->IsCompiled())
            return;

        // Set render state
        static RHI_PipelineState pipeline_state;
        pipeline_state.shader_compute = shader_c;
        pipeline_state.pass_name      = "Pass_ToneMapping";

        // Draw
        if (cmd_list->BeginRenderPass(pipeline_state))
        {
            // Update uber buffer
            m_buffer_uber_cpu.resolution = Vector2(static_cast<float>(tex_out->GetWidth()), static_cast<float>(tex_out->GetHeight()));
            UpdateUberBuffer(cmd_list);

            const uint32_t thread_group_count_x = static_cast<uint32_t>(Math::Helper::Ceil(static_cast<float>(tex_out->GetWidth()) / m_thread_group_count));
            const uint32_t thread_group_count_y = static_cast<uint32_t>(Math::Helper::Ceil(static_cast<float>(tex_out->GetHeight()) / m_thread_group_count));
            const uint32_t thread_group_count_z = 1;
            const bool async = false;

            cmd_list->SetTexture(RendererBindingsUav::rgba, tex_out);
            cmd_list->SetTexture(RendererBindingsSrv::tex, tex_in);
            cmd_list->Dispatch(thread_group_count_x, thread_group_count_y, thread_group_count_z, async);
            cmd_list->EndRenderPass();
        }
    }
    
    void Renderer::Pass_GammaCorrection(RHI_CommandList* cmd_list, shared_ptr<RHI_Texture>& tex_in, shared_ptr<RHI_Texture>& tex_out)
    {
        // Acquire shaders
        RHI_Shader* shader_c = m_shaders[RendererShader::GammaCorrection_C].get();
        if (!shader_c->IsCompiled())
            return;

        // Set render state
        static RHI_PipelineState pipeline_state;
        pipeline_state.shader_compute   = shader_c;
        pipeline_state.pass_name        = "Pass_GammaCorrection";

        // Draw
        if (cmd_list->BeginRenderPass(pipeline_state))
        {
            // Update uber buffer
            m_buffer_uber_cpu.resolution = Vector2(static_cast<float>(tex_out->GetWidth()), static_cast<float>(tex_out->GetHeight()));
            UpdateUberBuffer(cmd_list);

            const uint32_t thread_group_count_x   = static_cast<uint32_t>(Math::Helper::Ceil(static_cast<float>(tex_out->GetWidth()) / m_thread_group_count));
            const uint32_t thread_group_count_y   = static_cast<uint32_t>(Math::Helper::Ceil(static_cast<float>(tex_out->GetHeight()) / m_thread_group_count));
            const uint32_t thread_group_count_z   = 1;
            const bool async                      = false;

            cmd_list->SetTexture(RendererBindingsUav::rgba, tex_out);
            cmd_list->SetTexture(RendererBindingsSrv::tex, tex_in);
            cmd_list->Dispatch(thread_group_count_x, thread_group_count_y, thread_group_count_z, async);
            cmd_list->EndRenderPass();
        }
    }
    
    void Renderer::Pass_FXAA(RHI_CommandList* cmd_list, shared_ptr<RHI_Texture>& tex_in, shared_ptr<RHI_Texture>& tex_out)
    {
        // Acquire shaders
        RHI_Shader* shader_p_luma   = m_shaders[RendererShader::Fxaa_Luminance_C].get();
        RHI_Shader* shader_p_fxaa   = m_shaders[RendererShader::Fxaa_C].get();
        if (!shader_p_luma->IsCompiled() || !shader_p_fxaa->IsCompiled())
            return;

        // Update uber buffer
        m_buffer_uber_cpu.resolution = Vector2(static_cast<float>(tex_out->GetWidth()), static_cast<float>(tex_out->GetHeight()));
        UpdateUberBuffer(cmd_list);

        // Compute thread count
        const uint32_t thread_group_count_x = static_cast<uint32_t>(Math::Helper::Ceil(static_cast<float>(tex_out->GetWidth()) / m_thread_group_count));
        const uint32_t thread_group_count_y = static_cast<uint32_t>(Math::Helper::Ceil(static_cast<float>(tex_out->GetHeight()) / m_thread_group_count));
        const uint32_t thread_group_count_z = 1;
        const bool async = false;

        // Luminance
        {
            // Set render state
            static RHI_PipelineState pipeline_state;
            pipeline_state.shader_compute   = shader_p_luma;
            pipeline_state.pass_name        = "Pass_FXAA_Luminance";

            // Draw
            if (cmd_list->BeginRenderPass(pipeline_state))
            {
                cmd_list->SetTexture(RendererBindingsUav::rgba, tex_out);
                cmd_list->SetTexture(RendererBindingsSrv::tex, tex_in);
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
                cmd_list->SetTexture(RendererBindingsUav::rgba, tex_in);
                cmd_list->SetTexture(RendererBindingsSrv::tex, tex_out);
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
        RHI_Shader* shader_c = m_shaders[RendererShader::ChromaticAberration_C].get();
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

            const uint32_t thread_group_count_x   = static_cast<uint32_t>(Math::Helper::Ceil(static_cast<float>(tex_out->GetWidth()) / m_thread_group_count));
            const uint32_t thread_group_count_y   = static_cast<uint32_t>(Math::Helper::Ceil(static_cast<float>(tex_out->GetHeight()) / m_thread_group_count));
            const uint32_t thread_group_count_z   = 1;
            const bool async                      = false;

            cmd_list->SetTexture(RendererBindingsUav::rgba, tex_out);
            cmd_list->SetTexture(RendererBindingsSrv::tex, tex_in);
            cmd_list->Dispatch(thread_group_count_x, thread_group_count_y, thread_group_count_z, async);
            cmd_list->EndRenderPass();
        }
    }
    
    void Renderer::Pass_MotionBlur(RHI_CommandList* cmd_list, shared_ptr<RHI_Texture>& tex_in, shared_ptr<RHI_Texture>& tex_out)
    {
        // Acquire shaders
        RHI_Shader* shader_c = m_shaders[RendererShader::MotionBlur_C].get();
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

            const uint32_t thread_group_count_x   = static_cast<uint32_t>(Math::Helper::Ceil(static_cast<float>(tex_out->GetWidth()) / m_thread_group_count));
            const uint32_t thread_group_count_y   = static_cast<uint32_t>(Math::Helper::Ceil(static_cast<float>(tex_out->GetHeight()) / m_thread_group_count));
            const uint32_t thread_group_count_z   = 1;
            const bool async                      = false;

            cmd_list->SetTexture(RendererBindingsUav::rgba, tex_out);
            cmd_list->SetTexture(RendererBindingsSrv::tex, tex_in);
            cmd_list->SetTexture(RendererBindingsSrv::gbuffer_velocity, m_render_targets[RendererRt::Gbuffer_Velocity]);
            cmd_list->SetTexture(RendererBindingsSrv::gbuffer_depth, m_render_targets[RendererRt::Gbuffer_Depth]);
            cmd_list->Dispatch(thread_group_count_x, thread_group_count_y, thread_group_count_z, async);
            cmd_list->EndRenderPass();
        }
    }
    
    void Renderer::Pass_DepthOfField(RHI_CommandList* cmd_list, shared_ptr<RHI_Texture>& tex_in, shared_ptr<RHI_Texture>& tex_out)
    {
        // Acquire shaders
        RHI_Shader* shader_downsampleCoc    = m_shaders[RendererShader::Dof_DownsampleCoc_C].get();
        RHI_Shader* shader_bokeh            = m_shaders[RendererShader::Dof_Bokeh_C].get();
        RHI_Shader* shader_tent             = m_shaders[RendererShader::Dof_Tent_C].get();
        RHI_Shader* shader_upsampleBlend    = m_shaders[RendererShader::Dof_UpscaleBlend_C].get();
        if (!shader_downsampleCoc->IsCompiled() || !shader_bokeh->IsCompiled() || !shader_tent->IsCompiled() || !shader_upsampleBlend->IsCompiled())
            return;

        // Acquire render targets
        RHI_Texture* tex_bokeh_half     = m_render_targets[RendererRt::Dof_Half].get();
        RHI_Texture* tex_bokeh_half_2   = m_render_targets[RendererRt::Dof_Half_2].get();
        RHI_Texture* tex_depth          = m_render_targets[RendererRt::Gbuffer_Depth].get();

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

                const uint32_t thread_group_count_x   = static_cast<uint32_t>(Math::Helper::Ceil(static_cast<float>(tex_bokeh_half->GetWidth()) / m_thread_group_count));
                const uint32_t thread_group_count_y   = static_cast<uint32_t>(Math::Helper::Ceil(static_cast<float>(tex_bokeh_half->GetHeight()) / m_thread_group_count));
                const uint32_t thread_group_count_z   = 1;
                const bool async                      = false;

                cmd_list->SetTexture(RendererBindingsUav::rgba, tex_bokeh_half);
                cmd_list->SetTexture(RendererBindingsSrv::gbuffer_depth, tex_depth);
                cmd_list->SetTexture(RendererBindingsSrv::tex, tex_in);
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

                const uint32_t thread_group_count_x = static_cast<uint32_t>(Math::Helper::Ceil(static_cast<float>(tex_bokeh_half_2->GetWidth()) / m_thread_group_count));
                const uint32_t thread_group_count_y = static_cast<uint32_t>(Math::Helper::Ceil(static_cast<float>(tex_bokeh_half_2->GetHeight()) / m_thread_group_count));
                const uint32_t thread_group_count_z = 1;
                const bool async = false;

                cmd_list->SetTexture(RendererBindingsUav::rgba, tex_bokeh_half_2);
                cmd_list->SetTexture(RendererBindingsSrv::tex, tex_bokeh_half);
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

                const uint32_t thread_group_count_x = static_cast<uint32_t>(Math::Helper::Ceil(static_cast<float>(tex_bokeh_half->GetWidth()) / m_thread_group_count));
                const uint32_t thread_group_count_y = static_cast<uint32_t>(Math::Helper::Ceil(static_cast<float>(tex_bokeh_half->GetHeight()) / m_thread_group_count));
                const uint32_t thread_group_count_z = 1;
                const bool async = false;

                cmd_list->SetTexture(RendererBindingsUav::rgba, tex_bokeh_half);
                cmd_list->SetTexture(RendererBindingsSrv::tex, tex_bokeh_half_2);
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

                const uint32_t thread_group_count_x = static_cast<uint32_t>(Math::Helper::Ceil(static_cast<float>(tex_out->GetWidth()) / m_thread_group_count));
                const uint32_t thread_group_count_y = static_cast<uint32_t>(Math::Helper::Ceil(static_cast<float>(tex_out->GetHeight()) / m_thread_group_count));
                const uint32_t thread_group_count_z = 1;
                const bool async = false;

                cmd_list->SetTexture(RendererBindingsUav::rgba, tex_out);
                cmd_list->SetTexture(RendererBindingsSrv::gbuffer_depth, tex_depth);
                cmd_list->SetTexture(RendererBindingsSrv::tex, tex_in);
                cmd_list->SetTexture(RendererBindingsSrv::tex2, tex_bokeh_half);
                cmd_list->Dispatch(thread_group_count_x, thread_group_count_y, thread_group_count_z, async);
                cmd_list->EndRenderPass();
            }
        }
    }
    
    void Renderer::Pass_Dithering(RHI_CommandList* cmd_list, shared_ptr<RHI_Texture>& tex_in, shared_ptr<RHI_Texture>& tex_out)
    {
        // Acquire shaders
        RHI_Shader* shader = m_shaders[RendererShader::Dithering_C].get();
        if (!shader->IsCompiled())
            return;

        // Set render state
        static RHI_PipelineState pipeline_state;
        pipeline_state.shader_compute   = shader;
        pipeline_state.pass_name        = "Pass_Dithering";

        // Draw
        if (cmd_list->BeginRenderPass(pipeline_state))
        {
            // Update uber buffer
            m_buffer_uber_cpu.resolution = Vector2(static_cast<float>(tex_out->GetWidth()), static_cast<float>(tex_out->GetHeight()));
            UpdateUberBuffer(cmd_list);

            const uint32_t thread_group_count_x = static_cast<uint32_t>(Math::Helper::Ceil(static_cast<float>(tex_out->GetWidth()) / m_thread_group_count));
            const uint32_t thread_group_count_y = static_cast<uint32_t>(Math::Helper::Ceil(static_cast<float>(tex_out->GetHeight()) / m_thread_group_count));
            const uint32_t thread_group_count_z = 1;
            const bool async = false;

            cmd_list->SetTexture(RendererBindingsUav::rgba, tex_out);
            cmd_list->SetTexture(RendererBindingsSrv::tex, tex_in);
            cmd_list->Dispatch(thread_group_count_x, thread_group_count_y, thread_group_count_z, async);
            cmd_list->EndRenderPass();
        }
    }
    
    void Renderer::Pass_FilmGrain(RHI_CommandList* cmd_list, shared_ptr<RHI_Texture>& tex_in, shared_ptr<RHI_Texture>& tex_out)
    {
        // Acquire shaders
        RHI_Shader* shader_c = m_shaders[RendererShader::FilmGrain_C].get();
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

            const uint32_t thread_group_count_x   = static_cast<uint32_t>(Math::Helper::Ceil(static_cast<float>(tex_out->GetWidth()) / m_thread_group_count));
            const uint32_t thread_group_count_y   = static_cast<uint32_t>(Math::Helper::Ceil(static_cast<float>(tex_out->GetHeight()) / m_thread_group_count));
            const uint32_t thread_group_count_z   = 1;
            const bool async                      = false;

            cmd_list->SetTexture(RendererBindingsUav::rgba, tex_out);
            cmd_list->SetTexture(RendererBindingsSrv::tex, tex_in);
            cmd_list->Dispatch(thread_group_count_x, thread_group_count_y, thread_group_count_z, async);
            cmd_list->EndRenderPass();
        }
    }
    
    void Renderer::Pass_Sharpening(RHI_CommandList* cmd_list, shared_ptr<RHI_Texture>& tex_in, shared_ptr<RHI_Texture>& tex_out)
    {
        // Acquire shaders
        RHI_Shader* shader_c = m_shaders[RendererShader::Sharpening_C].get();
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

            const uint32_t thread_group_count_x   = static_cast<uint32_t>(Math::Helper::Ceil(static_cast<float>(tex_out->GetWidth()) / m_thread_group_count));
            const uint32_t thread_group_count_y   = static_cast<uint32_t>(Math::Helper::Ceil(static_cast<float>(tex_out->GetHeight()) / m_thread_group_count));
            const uint32_t thread_group_count_z   = 1;
            const bool async                      = false;

            cmd_list->SetTexture(RendererBindingsUav::rgba, tex_out);
            cmd_list->SetTexture(RendererBindingsSrv::tex, tex_in);
            cmd_list->Dispatch(thread_group_count_x, thread_group_count_y, thread_group_count_z, async);
            cmd_list->EndRenderPass();
        }
    }
    
    void Renderer::Pass_Lines(RHI_CommandList* cmd_list, shared_ptr<RHI_Texture>& tex_out)
    {
        const bool draw_picking_ray = m_options & Render_Debug_PickingRay;
        const bool draw_aabb        = m_options & Render_Debug_Aabb;
        const bool draw_grid        = m_options & Render_Debug_Grid;
        const bool draw_lights      = m_options & Render_Debug_Lights;
        const auto draw_lines       = !m_lines_depth_disabled.empty() || !m_lines_depth_enabled.empty(); // Any kind of lines, physics, user debug, etc.
        const auto draw             = draw_picking_ray || draw_aabb || draw_grid || draw_lines || draw_lights;
        if (!draw)
            return;

        // Acquire color shaders
        RHI_Shader* shader_color_v = m_shaders[RendererShader::Color_V].get();
        RHI_Shader* shader_color_p = m_shaders[RendererShader::Color_P].get();
        if (!shader_color_v->IsCompiled() || !shader_color_p->IsCompiled())
            return;

        // Grid
        if (draw_grid)
        {
            // Set render state
            static RHI_PipelineState pipeline_state;
            pipeline_state.shader_vertex                    = shader_color_v;
            pipeline_state.shader_pixel                     = shader_color_p;
            pipeline_state.rasterizer_state                 = m_rasterizer_cull_back_wireframe.get();
            pipeline_state.blend_state                      = m_blend_alpha.get();
            pipeline_state.depth_stencil_state              = m_depth_stencil_on_off_r.get();
            pipeline_state.vertex_buffer_stride             = m_gizmo_grid->GetVertexBuffer()->GetStride();
            pipeline_state.render_target_color_textures[0]  = tex_out.get();
            pipeline_state.render_target_depth_texture      = m_render_targets[RendererRt::Gbuffer_Depth].get();
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

        // Generate lines for debug primitives supported by the renderer
        {
            // Picking ray
            if (draw_picking_ray)
            {
                const auto& ray = m_camera->GetPickingRay();
                DrawDebugLine(ray.GetStart(), ray.GetStart() + ray.GetDirection() * m_camera->GetFarPlane(), Vector4(0, 1, 0, 1));
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
                        DrawDebugLine(start, start + end, Vector4(0, 1, 0, 1));
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
                        DrawDebugBox(renderable->GetAabb(), Vector4(0.41f, 0.86f, 1.0f, 1.0f));
                    }
                }

                for (const auto& entity : m_entities[Renderer_Object_Transparent])
                {
                    if (auto renderable = entity->GetRenderable())
                    {
                        DrawDebugBox(renderable->GetAabb(), Vector4(0.41f, 0.86f, 1.0f, 1.0f));
                    }
                }
            }
        }

        // Draw lines
        {
            // Width depth
            uint32_t line_vertex_buffer_size = static_cast<uint32_t>(m_lines_depth_enabled.size());
            if (line_vertex_buffer_size != 0)
            {
                // Grow vertex buffer (if needed)
                if (line_vertex_buffer_size > m_vertex_buffer_lines->GetVertexCount())
                {
                    m_vertex_buffer_lines->CreateDynamic<RHI_Vertex_PosCol>(line_vertex_buffer_size);
                }

                // Update vertex buffer
                RHI_Vertex_PosCol* buffer = static_cast<RHI_Vertex_PosCol*>(m_vertex_buffer_lines->Map());
                std::copy(m_lines_depth_enabled.begin(), m_lines_depth_enabled.end(), buffer);
                m_vertex_buffer_lines->Unmap();

                // Set render state
                static RHI_PipelineState pipeline_state;
                pipeline_state.shader_vertex                    = shader_color_v;
                pipeline_state.shader_pixel                     = shader_color_p;
                pipeline_state.rasterizer_state                 = m_rasterizer_cull_back_wireframe.get();
                pipeline_state.blend_state                      = m_blend_alpha.get();
                pipeline_state.depth_stencil_state              = m_depth_stencil_on_off_r.get();
                pipeline_state.vertex_buffer_stride             = m_vertex_buffer_lines->GetStride();
                pipeline_state.render_target_color_textures[0]  = tex_out.get();
                pipeline_state.render_target_depth_texture      = m_render_targets[RendererRt::Gbuffer_Depth].get();
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

            // Without depth
            line_vertex_buffer_size = static_cast<uint32_t>(m_lines_depth_disabled.size());
            if (line_vertex_buffer_size != 0)
            {
                // Grow vertex buffer (if needed)
                if (line_vertex_buffer_size > m_vertex_buffer_lines->GetVertexCount())
                {
                    m_vertex_buffer_lines->CreateDynamic<RHI_Vertex_PosCol>(line_vertex_buffer_size);
                }

                // Update vertex buffer
                RHI_Vertex_PosCol* buffer = static_cast<RHI_Vertex_PosCol*>(m_vertex_buffer_lines->Map());
                std::copy(m_lines_depth_disabled.begin(), m_lines_depth_disabled.end(), buffer);
                m_vertex_buffer_lines->Unmap();

                // Set render state
                static RHI_PipelineState pipeline_state;
                pipeline_state.shader_vertex                    = shader_color_v;
                pipeline_state.shader_pixel                     = shader_color_p;
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
    }
    
    void Renderer::Pass_Icons(RHI_CommandList* cmd_list, RHI_Texture* tex_out)
    {
        if (!(m_options & Render_Debug_Lights))
            return;

        // Acquire resources
        auto& lights                    = m_entities[Renderer_Object_Light];
        const auto& shader_quad_v       = m_shaders[RendererShader::Quad_V];
        const auto& shader_texture_p    = m_shaders[RendererShader::Texture_P];
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
        pipeline_state.pass_name                        = "Pass_Icons";

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
                    const float v_dot_l             = Vector3::Dot(m_camera->GetTransform()->GetForward(), direction_camera_to_light);
        
                    // Only draw if it's inside our view
                    if (v_dot_l > 0.5f)
                    {
                        // Compute light screen space position and scale (based on distance from the camera)
                        const auto position_light_screen    = m_camera->Project(position_light_world);
                        const auto distance                 = (position_camera_world - position_light_world).Length() + Helper::EPSILON;
                        auto scale                          = m_gizmo_size_max / distance;
                        scale                               = Helper::Clamp(scale, m_gizmo_size_min, m_gizmo_size_max);
        
                        // Choose texture based on light type
                        shared_ptr<RHI_Texture> light_tex = nullptr;
                        const auto type = light->GetLightType();
                        if (type == LightType::Directional) light_tex = m_gizmo_tex_light_directional;
                        else if (type == LightType::Point)  light_tex = m_gizmo_tex_light_point;
                        else if (type == LightType::Spot)   light_tex = m_gizmo_tex_light_spot;
        
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
        
                        cmd_list->SetTexture(RendererBindingsSrv::tex, light_tex);
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
        auto const& shader_gizmo_transform_v    = m_shaders[RendererShader::Entity_V];
        auto const& shader_gizmo_transform_p    = m_shaders[RendererShader::Entity_Transform_P];
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
            const auto& shader_v = m_shaders[RendererShader::Entity_V];
            const auto& shader_p = m_shaders[RendererShader::Entity_Outline_P];
            if (!shader_v->IsCompiled() || !shader_p->IsCompiled())
                return;

            RHI_Texture* tex_depth  = m_render_targets[RendererRt::Gbuffer_Depth].get();
            RHI_Texture* tex_normal = m_render_targets[RendererRt::Gbuffer_Normal].get();

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

                cmd_list->SetTexture(RendererBindingsSrv::gbuffer_depth, tex_depth);
                cmd_list->SetTexture(RendererBindingsSrv::gbuffer_normal, tex_normal);
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
        const auto& shader_v    = m_shaders[RendererShader::Font_V];
        const auto& shader_p    = m_shaders[RendererShader::Font_P];
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
                cmd_list->SetTexture(RendererBindingsSrv::font_atlas, m_font->GetAtlasOutline());
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
            cmd_list->SetTexture(RendererBindingsSrv::font_atlas, m_font->GetAtlas());
            cmd_list->DrawIndexed(m_font->GetIndexCount());
            cmd_list->EndRenderPass();
        }
    }
    
    bool Renderer::Pass_DebugBuffer(RHI_CommandList* cmd_list, shared_ptr<RHI_Texture>& tex_out)
    {
        if (m_render_target_debug == 0)
            return true;

        // Bind correct texture & shader pass
        RHI_Texture* texture                = m_render_targets[static_cast<RendererRt>(m_render_target_debug)].get();
        RendererShader shader_type    = RendererShader::Copy_C;

        if (m_render_target_debug == static_cast<uint64_t>(RendererRt::Gbuffer_Albedo))
        {
            shader_type = RendererShader::DebugChannelRgbGammaCorrect_C;
        }

        if (m_render_target_debug == static_cast<uint64_t>(RendererRt::Gbuffer_Normal))
        {
            shader_type = RendererShader::DebugNormal_C;
        }

        if (m_render_target_debug == static_cast<uint64_t>(RendererRt::Gbuffer_Material))
        {
            shader_type = RendererShader::Copy_C;
        }

        if (m_render_target_debug == static_cast<uint64_t>(RendererRt::Light_Diffuse) || m_render_target_debug == static_cast<uint64_t>(RendererRt::Light_Diffuse_Transparent))
        {
            shader_type = RendererShader::DebugChannelRgbGammaCorrect_C;
        }

        if (m_render_target_debug == static_cast<uint64_t>(RendererRt::Light_Specular) || m_render_target_debug == static_cast<uint64_t>(RendererRt::Light_Specular_Transparent))
        {
            shader_type = RendererShader::DebugChannelRgbGammaCorrect_C;
        }

        if (m_render_target_debug == static_cast<uint64_t>(RendererRt::Gbuffer_Velocity))
        {
            shader_type = RendererShader::DebugVelocity_C;
        }

        if (m_render_target_debug == static_cast<uint64_t>(RendererRt::Gbuffer_Depth))
        {
            shader_type = RendererShader::DebugChannelR_C;
        }

        if (m_render_target_debug == static_cast<uint64_t>(RendererRt::Hbao_Blurred))
        {
            texture     = m_options & Render_Hbao ? m_render_targets[RendererRt::Hbao_Blurred].get() : m_default_tex_white.get();
            shader_type = RendererShader::DebugChannelR_C;
        }

        if (m_render_target_debug == static_cast<uint64_t>(RendererRt::Hbao))
        {
            texture = m_options & Render_Hbao ? m_render_targets[RendererRt::Hbao].get() : m_default_tex_white.get();
            shader_type = RendererShader::DebugChannelR_C;
        }

        if (m_render_target_debug == static_cast<uint64_t>(RendererRt::Ssgi))
        {
            texture = m_options & Render_Ssgi ? m_render_targets[RendererRt::Ssgi].get() : m_default_tex_black.get();
            shader_type = RendererShader::DebugChannelRgbGammaCorrect_C;
        }

        if (m_render_target_debug == static_cast<uint64_t>(RendererRt::Ssr))
        {
            shader_type = RendererShader::DebugChannelRgbGammaCorrect_C;
        }

        if (m_render_target_debug == static_cast<uint64_t>(RendererRt::Bloom))
        {
            texture     = m_render_tex_bloom.front().get();
            shader_type = RendererShader::DebugChannelRgbGammaCorrect_C;
        }

        if (m_render_target_debug == static_cast<uint64_t>(RendererRt::Dof_Half))
        {
            texture = m_render_targets[RendererRt::Dof_Half].get();
            shader_type = RendererShader::DebugChannelRgbGammaCorrect_C;
        }

        if (m_render_target_debug == static_cast<uint64_t>(RendererRt::Dof_Half_2))
        {
            texture = m_render_targets[RendererRt::Dof_Half_2].get();
            shader_type = RendererShader::DebugChannelRgbGammaCorrect_C;
        }

        if (m_render_target_debug == static_cast<uint64_t>(RendererRt::Light_Volumetric))
        {
            shader_type = RendererShader::DebugChannelRgbGammaCorrect_C;
        }

        if (m_render_target_debug == static_cast<uint64_t>(RendererRt::Brdf_Specular_Lut))
        {
            shader_type = RendererShader::Copy_C;
        }

        // Acquire shaders
        RHI_Shader* shader = m_shaders[shader_type].get();
        if (!shader->IsCompiled())
            return false;

        // Set render state
        static RHI_PipelineState pipeline_state;
        pipeline_state.shader_compute   = shader;
        pipeline_state.pass_name        = "Pass_DebugBuffer";

        // Draw
        if (cmd_list->BeginRenderPass(pipeline_state))
        {
            // Update uber buffer
            m_buffer_uber_cpu.resolution    = Vector2(static_cast<float>(tex_out->GetWidth()), static_cast<float>(tex_out->GetHeight()));
            m_buffer_uber_cpu.transform     = m_buffer_frame_cpu.view_projection_ortho;
            UpdateUberBuffer(cmd_list);

            const uint32_t thread_group_count_x = static_cast<uint32_t>(Math::Helper::Ceil(static_cast<float>(tex_out->GetWidth()) / m_thread_group_count));
            const uint32_t thread_group_count_y = static_cast<uint32_t>(Math::Helper::Ceil(static_cast<float>(tex_out->GetHeight()) / m_thread_group_count));
            const uint32_t thread_group_count_z = 1;
            const bool async = false;

            cmd_list->SetTexture(RendererBindingsUav::rgba, tex_out);
            cmd_list->SetTexture(RendererBindingsSrv::tex, texture);
            cmd_list->Dispatch(thread_group_count_x, thread_group_count_y, thread_group_count_z, async);
            cmd_list->EndRenderPass();
        }

        return true;
    }
    
    void Renderer::Pass_BrdfSpecularLut(RHI_CommandList* cmd_list)
    {
        if (m_brdf_specular_lut_rendered)
            return;

        // Acquire shaders
        RHI_Shader* shader = m_shaders[RendererShader::BrdfSpecularLut_C].get();
        if (!shader->IsCompiled())
            return;

        // Acquire render target
        RHI_Texture* render_target = m_render_targets[RendererRt::Brdf_Specular_Lut].get();

        // Set render state
        static RHI_PipelineState pipeline_state;
        pipeline_state.shader_compute   = shader;
        pipeline_state.pass_name        = "Pass_BrdfSpecularLut";

        // Draw
        if (cmd_list->BeginRenderPass(pipeline_state))
        {
            // Update uber buffer
            m_buffer_uber_cpu.resolution = Vector2(static_cast<float>(render_target->GetWidth()), static_cast<float>(render_target->GetHeight()));
            UpdateUberBuffer(cmd_list);

            const uint32_t thread_group_count_x = static_cast<uint32_t>(Math::Helper::Ceil(static_cast<float>(render_target->GetWidth()) / m_thread_group_count));
            const uint32_t thread_group_count_y = static_cast<uint32_t>(Math::Helper::Ceil(static_cast<float>(render_target->GetHeight()) / m_thread_group_count));
            const uint32_t thread_group_count_z = 1;
            const bool async = false;

            cmd_list->SetTexture(RendererBindingsUav::rg, render_target);
            cmd_list->Dispatch(thread_group_count_x, thread_group_count_y, thread_group_count_z, async);
            cmd_list->EndRenderPass();

            m_brdf_specular_lut_rendered = true;
        }
    }
    
    void Renderer::Pass_Copy(RHI_CommandList* cmd_list, RHI_Texture* tex_in, RHI_Texture* tex_out)
    {
        // Acquire shaders
        RHI_Shader* shader_c = m_shaders[RendererShader::Copy_C].get();
        if (!shader_c->IsCompiled())
            return;

        // Set render state
        static RHI_PipelineState pipeline_state;
        pipeline_state.shader_compute = shader_c;
        pipeline_state.pass_name      = "Pass_Copy";

        // Draw
        if (cmd_list->BeginRenderPass(pipeline_state))
        {
            const uint32_t thread_group_count_x   = static_cast<uint32_t>(Math::Helper::Ceil(static_cast<float>(tex_out->GetWidth()) / m_thread_group_count));
            const uint32_t thread_group_count_y   = static_cast<uint32_t>(Math::Helper::Ceil(static_cast<float>(tex_out->GetHeight()) / m_thread_group_count));
            const uint32_t thread_group_count_z   = 1;
            const bool async                      = false;

            cmd_list->SetTexture(RendererBindingsUav::rgba, tex_out);
            cmd_list->SetTexture(RendererBindingsSrv::tex, tex_in);
            cmd_list->Dispatch(thread_group_count_x, thread_group_count_y, thread_group_count_z, async);
            cmd_list->EndRenderPass();
        }
    }

    void Renderer::Pass_CopyToBackbuffer(RHI_CommandList* cmd_list)
    {
        // Acquire shaders
        RHI_Shader* shader_v = m_shaders[RendererShader::Quad_V].get();
        RHI_Shader* shader_p = m_shaders[RendererShader::Texture_P].get();
        if (!shader_v->IsCompiled() || !shader_p->IsCompiled())
            return;

        // Set render state
        static RHI_PipelineState pipeline_state = {};
        pipeline_state.shader_vertex            = shader_v;
        pipeline_state.shader_pixel             = shader_p;
        pipeline_state.rasterizer_state         = m_rasterizer_cull_back_solid.get();
        pipeline_state.blend_state              = m_blend_disabled.get();
        pipeline_state.depth_stencil_state      = m_depth_stencil_off_off.get();
        pipeline_state.vertex_buffer_stride     = m_viewport_quad.GetVertexBuffer()->GetStride();
        pipeline_state.render_target_swapchain  = m_swap_chain.get();
        pipeline_state.clear_color[0]           = rhi_color_dont_care;
        pipeline_state.primitive_topology       = RHI_PrimitiveTopology_TriangleList;
        pipeline_state.viewport                 = m_viewport;
        pipeline_state.pass_name                = "Pass_CopyToBackbuffer";

        // Record commands
        if (cmd_list->BeginRenderPass(pipeline_state))
        {
            cmd_list->SetBufferVertex(m_viewport_quad.GetVertexBuffer());
            cmd_list->SetBufferIndex(m_viewport_quad.GetIndexBuffer());
            cmd_list->SetTexture(RendererBindingsSrv::tex, m_render_targets[RendererRt::Frame_Ldr].get());
            cmd_list->DrawIndexed(m_viewport_quad.GetIndexCount());
            cmd_list->EndRenderPass();
        }
    }
}
