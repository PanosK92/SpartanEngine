/*
Copyright(c) 2015-2026 Panos Karabelas

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

//= INCLUDES ==================================
#include "pch.h"
#include "Renderer.h"
#include "../World/Entity.h"
#include "../World/Components/Light.h"
#include "../RHI/RHI_CommandList.h"
#include "../RHI/RHI_Buffer.h"
#include "../RHI/RHI_AccelerationStructure.h"
#include "../RHI/RHI_RasterizerState.h"
#include "../RHI/RHI_Device.h"
#include "../Rendering/Material.h"
#include "../Rendering/GeometryBuffer.h"
#include "../XR/Xr.h"
//=============================================

//= NAMESPACES ===============
using namespace std;
using namespace spartan::math;
//============================

namespace spartan
{
    void Renderer::Pass_ShadowMaps(RHI_CommandList* cmd_list)
    {
        if (World::GetLightCount() == 0)
        {
            return;
        }

        // light.hlsl skips the analytical shading path entirely when rt shadows or restir
        // own every light type (restir now covers directional, point, spot and area through
        // its initial nee pool, traces inline rt shadow rays inside trace_shift_visibility)
        const bool tlas_available = RHI_Device::IsSupportedRayTracing() && GetTopLevelAccelerationStructure() != nullptr;
        if ((cvar_ray_traced_shadows.GetValueAs<bool>() && tlas_available) || cvar_restir_pt.GetValueAs<bool>())
        {
            return;
        }

        RHI_PipelineState pso;
        pso.name                             = "shadow_maps";
        pso.shaders[RHI_Shader_Type::Vertex] = GetShader(Renderer_Shader::depth_light_v);
        pso.blend_state                      = GetBlendState(Renderer_BlendState::Off);
        pso.depth_stencil_state              = GetDepthStencilState(Renderer_DepthStencilState::ReadWrite);
        pso.clear_depth                      = 0.0f;
        pso.render_target_depth_texture      = GetRenderTarget(Renderer_RenderTarget::shadow_atlas);
        pso.rasterizer_state = GetRasterizerState(Renderer_RasterizerState::Light_directional);

        cmd_list->BeginTimeblock(pso.name);
        {
            cmd_list->SetPipelineState(pso);

            for (Entity* entity_light : World::GetEntitiesLights())
            {
                Light* light = entity_light->GetComponent<Light>();
                if (!light->GetFlag(LightFlags::Shadows) || light->GetIntensityRadiometric() == 0.0f)
                {
                    continue;
                }
    
                RHI_RasterizerState* new_state = (light->GetLightType() == LightType::Directional) ? GetRasterizerState(Renderer_RasterizerState::Light_directional) : GetRasterizerState(Renderer_RasterizerState::Light_point_spot);
                if (pso.rasterizer_state != new_state)
                {
                    pso.rasterizer_state = new_state;
                    cmd_list->SetPipelineState(pso);
                }

                for (uint32_t array_index = 0; array_index < light->GetSliceCount(); array_index++)
                {
                    const math::Rectangle& rect = light->GetAtlasRectangle(array_index);
                    if (!rect.IsDefined())
                    {
                        continue;
                    }
                    RHI_Viewport viewport;
                    viewport.x      = rect.x;
                    viewport.y      = rect.y;
                    viewport.width  = rect.width;
                    viewport.height = rect.height;
                    cmd_list->SetViewport(viewport);
                    cmd_list->SetScissorRectangle(rect);

                    for (uint32_t i = 0; i < m_draw_call_count; i++)
                    {
                        const Renderer_DrawCall& draw_call = m_draw_calls[i];
                        Render* renderable             = draw_call.renderable;
                        Material* material                 = renderable->GetMaterial();
                        const float shadow_distance        = renderable->GetMaxShadowDistance();
                        if (!material || material->IsTransparent() || !renderable->HasFlag(RenderableFlags::CastsShadows) || draw_call.distance_squared > shadow_distance * shadow_distance)
                        {
                            continue;
                        }

                        // todo: this needs to be recalculate only when the light or the renderable moves, not every frame
                        if (!light->IsInViewFrustum(renderable, array_index))
                        {
                            continue;
                        }

                        {
                            bool is_first_cascade = array_index == 0 && light->GetLightType() == LightType::Directional;
                            bool is_alpha_tested  = material->IsAlphaTested();
                            RHI_Shader* ps        = (is_first_cascade && is_alpha_tested) ? GetShader(Renderer_Shader::depth_light_alpha_color_p) : nullptr;
                        
                            if (pso.shaders[RHI_Shader_Type::Pixel] != ps)
                            {
                                pso.shaders[RHI_Shader_Type::Pixel] = ps;
                                cmd_list->SetPipelineState(pso);

                                cmd_list->SetViewport(viewport);
                                cmd_list->SetScissorRectangle(rect);
                            }
                        }

                        m_pcb_pass_cpu.draw_index = draw_call.draw_data_index;
                        m_pcb_pass_cpu.is_transparent = 0;
                        m_pcb_pass_cpu.material_index = material->GetIndex();
                        m_pcb_pass_cpu.set_f3_value(material->HasTextureOfType(MaterialTextureType::Color) ? 1.0f : 0.0f);
                        m_pcb_pass_cpu.set_f3_value2(static_cast<float>(light->GetIndex()), static_cast<float>(array_index), 0.0f);
                        cmd_list->PushConstants(m_pcb_pass_cpu);

                        {
                            cmd_list->SetCullMode(static_cast<RHI_CullMode>(material->GetProperty(MaterialProperty::CullMode)));
                            RHI_Buffer* instance_buffer = GeometryBuffer::GetInstanceBuffer() ? GeometryBuffer::GetInstanceBuffer() : GetBuffer(Renderer_Buffer::DummyInstance);
                            cmd_list->SetBufferVertex(renderable->GetVertexBuffer(), instance_buffer);
                            cmd_list->SetBufferIndex(renderable->GetIndexBuffer());

                            // compute lod index
                            bool close_to_shadow      = renderable->GetDistanceSquared() < 100.0f * 100.0f;                                   // anything within 100 meters of the shadow caster
                            uint32_t lod_index_bias   = light->GetLightType() == LightType::Directional ? 1 : 0;                              // bias for directional lights
                            uint32_t lod_index_shadow = clamp(renderable->GetLodIndex() + lod_index_bias, 0u, renderable->GetLodCount() - 1); // lod index biased towards lower quality lod
                            uint32_t lod_index        = close_to_shadow ? draw_call.lod_index : lod_index_shadow;                             // use normal lod if close to shadow caster, otherwise use light specific lod

                            cmd_list->DrawIndexed(
                                renderable->GetIndexCount(lod_index),
                                renderable->GetIndexOffset(lod_index),
                                renderable->GetVertexOffset(lod_index),
                                renderable->GetGlobalInstanceOffset() + draw_call.instance_index,
                                draw_call.instance_count
                            );
                        }
                    }
                }
            }
        }
        cmd_list->EndTimeblock();
    }

    void Renderer::Pass_HiZ(RHI_CommandList* cmd_list)
    {
        // renders major occluders to a depth buffer and builds a hi-z mip chain.
        // the indirect cull compute shader samples this for gpu-driven occlusion culling.
        // the depth texture is ALWAYS cleared to 0.0 (far plane, reverse-z) and the mip
        // chain is always rebuilt, even when occlusion is disabled or suppressed. this
        // guarantees the cull shader never reads stale/uninitialized depth, which would
        // cause non-deterministic culling artifacts depending on gpu memory contents.

        cmd_list->BeginTimeblock("hiz");

        RHI_Texture* tex_occluders     = GetRenderTarget(Renderer_RenderTarget::gbuffer_depth_occluders);
        RHI_Texture* tex_occluders_hiz = GetRenderTarget(Renderer_RenderTarget::gbuffer_depth_occluders_hiz);

        bool render_occluders = cvar_hiz_occlusion.GetValueAs<bool>() && !m_is_hiz_suppressed;

        // always start the render pass so the depth texture is cleared to far plane (0.0).
        // without this, the blit and downscale would propagate stale depth into the hi-z
        // mip chain, causing the cull shader to incorrectly occlude objects.
        {
            RHI_PipelineState pso;
            pso.name                             = "occluders";
            pso.shaders[RHI_Shader_Type::Vertex] = GetShader(Renderer_Shader::depth_prepass_v);
            pso.rasterizer_state                 = GetRasterizerState(Renderer_RasterizerState::Solid);
            pso.blend_state                      = GetBlendState(Renderer_BlendState::Off);
            pso.depth_stencil_state              = GetDepthStencilState(Renderer_DepthStencilState::ReadWrite);
            pso.render_target_depth_texture      = tex_occluders;
            pso.resolution_scale                 = true;
            pso.clear_depth                      = 0.0f;

            cmd_list->SetPipelineState(pso);

            if (render_occluders)
            {
                for (uint32_t i = 0; i < m_draw_calls_prepass_count; i++)
                {
                    const Renderer_DrawCall& draw_call = m_draw_calls_prepass[i];

                    if (!draw_call.is_occluder)
                    {
                        continue;
                    }

                    Render* renderable = draw_call.renderable;
                    RHI_CullMode cull_mode = static_cast<RHI_CullMode>(renderable->GetMaterial()->GetProperty(MaterialProperty::CullMode));
                    cull_mode              = (pso.rasterizer_state->GetPolygonMode() == RHI_PolygonMode::Wireframe) ? RHI_CullMode::None : cull_mode;
                    cmd_list->SetCullMode(cull_mode);

                    m_pcb_pass_cpu.draw_index = draw_call.draw_data_index;
                    cmd_list->PushConstants(m_pcb_pass_cpu);

                    cmd_list->SetBufferVertex(renderable->GetVertexBuffer());
                    cmd_list->SetBufferIndex(renderable->GetIndexBuffer());

                    cmd_list->DrawIndexed(
                        renderable->GetIndexCount(draw_call.lod_index),
                        renderable->GetIndexOffset(draw_call.lod_index),
                        renderable->GetVertexOffset(draw_call.lod_index)
                    );
                }
            }
        }

        // hi-z mip chain (min depth downsample, reverse z)
        Pass_Blit(cmd_list, tex_occluders, tex_occluders_hiz);
        Pass_Downscale(cmd_list, tex_occluders_hiz, Renderer_DownsampleFilter::Min);

        cmd_list->EndTimeblock();
    }

    void Renderer::Pass_IndirectCull(RHI_CommandList* cmd_list)
    {
        if (m_indirect_draw_count == 0 || m_cull_task_count == 0)
        {
            return;
        }

        cmd_list->BeginTimeblock("indirect_cull");
        {
            RHI_Texture* tex_occluders_hiz = GetRenderTarget(Renderer_RenderTarget::gbuffer_depth_occluders_hiz);

            // pass 1, per-meshlet cone + per-renderable hi-z, survivors land in meshlet_instances and bump triangle_dispatch_args.group_count_x
            {
                RHI_PipelineState pso;
                pso.name             = "indirect_cull_meshlet";
                pso.shaders[Compute] = GetShader(Renderer_Shader::indirect_cull_c);
                cmd_list->SetPipelineState(pso);

                cmd_list->SetTexture(Renderer_BindingsSrv::tex, tex_occluders_hiz);

                cmd_list->SetBuffer(Renderer_BindingsUav::indirect_draw_args,     GetBuffer(Renderer_Buffer::IndirectDrawArgs));
                cmd_list->SetBuffer(Renderer_BindingsUav::indirect_draw_data,     GetBuffer(Renderer_Buffer::IndirectDrawData));
                cmd_list->SetBuffer(Renderer_BindingsUav::meshlet_bounds,         GeometryBuffer::GetMeshletBoundsBuffer());
                cmd_list->SetBuffer(Renderer_BindingsUav::cull_tasks,             GetBuffer(Renderer_Buffer::CullTasks));
                cmd_list->SetBuffer(Renderer_BindingsUav::meshlet_instances,      GetBuffer(Renderer_Buffer::MeshletInstances));
                cmd_list->SetBuffer(Renderer_BindingsUav::triangle_dispatch_args, GetBuffer(Renderer_Buffer::TriangleDispatchArgs));

                // f4_value: x = task count, y = max hiz mip, z = meshlet instances cap (drop survivors past this)
                m_pcb_pass_cpu.set_f4_value(
                    static_cast<float>(m_cull_task_count),
                    static_cast<float>(tex_occluders_hiz->GetMipCount() - 1),
                    static_cast<float>(renderer_max_meshlet_instances),
                    0.0f);
                cmd_list->PushConstants(m_pcb_pass_cpu);

                uint32_t thread_group_count = (m_cull_task_count + 255) / 256;
                cmd_list->Dispatch(thread_group_count, 1, 1);

                // meshlet_instances and triangle_dispatch_args feed the triangle cull pass
                cmd_list->InsertBarrier(GetBuffer(Renderer_Buffer::MeshletInstances));
                cmd_list->InsertBarrier(GetBuffer(Renderer_Buffer::TriangleDispatchArgs));
            }

            // pass 2, per-triangle frustum + backface + sub-pixel cull, dispatched indirect with one workgroup per surviving meshlet
            {
                RHI_PipelineState pso;
                pso.name             = "indirect_cull_triangle";
                pso.shaders[Compute] = GetShader(Renderer_Shader::indirect_cull_triangle_c);
                cmd_list->SetPipelineState(pso);

                cmd_list->SetBuffer(Renderer_BindingsUav::indirect_draw_args,     GetBuffer(Renderer_Buffer::IndirectDrawArgs));
                cmd_list->SetBuffer(Renderer_BindingsUav::indirect_draw_data,     GetBuffer(Renderer_Buffer::IndirectDrawData));
                cmd_list->SetBuffer(Renderer_BindingsUav::meshlet_bounds,         GeometryBuffer::GetMeshletBoundsBuffer());
                cmd_list->SetBuffer(Renderer_BindingsUav::meshlet_instances,      GetBuffer(Renderer_Buffer::MeshletInstances));
                cmd_list->SetBuffer(Renderer_BindingsUav::visible_triangles,      GetBuffer(Renderer_Buffer::VisibleTriangles));

                // f4_value: x = meshlet instances cap, y = visible triangles cap (drop survivors past either cap)
                m_pcb_pass_cpu.set_f4_value(
                    static_cast<float>(renderer_max_meshlet_instances),
                    static_cast<float>(renderer_max_visible_triangles),
                    0.0f, 0.0f);
                cmd_list->PushConstants(m_pcb_pass_cpu);

                cmd_list->DispatchIndirect(GetBuffer(Renderer_Buffer::TriangleDispatchArgs), 0);

                // indirect_draw_args + visible_triangles feed the final indirect draw, plus indirect args needed for the indirect-draw stage
                cmd_list->InsertBarrier(GetBuffer(Renderer_Buffer::IndirectDrawArgs));
                cmd_list->InsertBarrier(GetBuffer(Renderer_Buffer::VisibleTriangles));
            }
        }
        cmd_list->EndTimeblock();
    }

    void Renderer::Pass_Depth_Prepass(RHI_CommandList* cmd_list)
    {
        RHI_Texture* tex_depth        = GetRenderTarget(Renderer_RenderTarget::gbuffer_depth);
        RHI_Texture* tex_depth_output = GetRenderTarget(Renderer_RenderTarget::gbuffer_depth_opaque_output);

        bool is_wireframe                     = cvar_wireframe.GetValueAs<bool>();
        bool xr_multiview                     = Xr::IsSessionRunning() && Xr::GetStereoMode();
        RHI_RasterizerState* rasterizer_state = GetRasterizerState(Renderer_RasterizerState::Solid);
        rasterizer_state                      = is_wireframe ? GetRasterizerState(Renderer_RasterizerState::Wireframe) : rasterizer_state;

        cmd_list->BeginTimeblock("depth_prepass");
        {
            // indirect prepass (must match g-buffer indirect path)
            // alpha-test pixel shader runs for all indirect draws, opaque materials with full-alpha albedo
            // pass through without discard so single bucket is fine
            if (m_indirect_draw_count > 0)
            {
                RHI_PipelineState pso;
                pso.name                             = "depth_prepass_indirect";
                pso.shaders[RHI_Shader_Type::Vertex] = GetShader(Renderer_Shader::depth_prepass_indirect_v);
                pso.shaders[RHI_Shader_Type::Pixel]  = GetShader(Renderer_Shader::depth_prepass_indirect_alpha_test_p);
                pso.rasterizer_state                 = rasterizer_state;
                pso.blend_state                      = GetBlendState(Renderer_BlendState::Off);
                pso.depth_stencil_state              = GetDepthStencilState(Renderer_DepthStencilState::ReadWrite);
                pso.vrs_input_texture                = cvar_variable_rate_shading.GetValueAs<bool>() ? GetRenderTarget(Renderer_RenderTarget::shading_rate) : nullptr;
                pso.render_target_depth_texture      = tex_depth;
                pso.resolution_scale                 = true;
                pso.clear_depth                      = 0.0f;
                pso.is_multiview                     = xr_multiview;
                cmd_list->SetPipelineState(pso);

                cmd_list->SetBuffer(Renderer_BindingsUav::indirect_draw_data, GetBuffer(Renderer_Buffer::IndirectDrawData));
                cmd_list->SetBuffer(Renderer_BindingsUav::meshlet_instances,  GetBuffer(Renderer_Buffer::MeshletInstances));
                cmd_list->SetBuffer(Renderer_BindingsUav::visible_triangles,  GetBuffer(Renderer_Buffer::VisibleTriangles));
                cmd_list->SetBuffer(Renderer_BindingsUav::meshlet_bounds,     GeometryBuffer::GetMeshletBoundsBuffer());

                // triangle cull discarded backfaces already, raster keeps both sides as safety against flag mismatch
                cmd_list->SetCullMode(RHI_CullMode::None);

                cmd_list->DrawIndirect(GetBuffer(Renderer_Buffer::IndirectDrawArgs), 0);
            }

            // cpu-driven tessellated path (only tessellated still uses cpu draws, indirect path covers everything else)
            {
                RHI_PipelineState pso;
                pso.name                             = "depth_prepass_tessellated";
                pso.shaders[RHI_Shader_Type::Vertex] = GetShader(Renderer_Shader::depth_prepass_v);
                pso.shaders[RHI_Shader_Type::Hull]   = GetShader(Renderer_Shader::tessellation_h);
                pso.shaders[RHI_Shader_Type::Domain] = GetShader(Renderer_Shader::tessellation_d);
                pso.rasterizer_state                 = rasterizer_state;
                pso.blend_state                      = GetBlendState(Renderer_BlendState::Off);
                pso.depth_stencil_state              = GetDepthStencilState(Renderer_DepthStencilState::ReadWrite);
                pso.vrs_input_texture                = cvar_variable_rate_shading.GetValueAs<bool>() ? GetRenderTarget(Renderer_RenderTarget::shading_rate) : nullptr;
                pso.render_target_depth_texture      = tex_depth;
                pso.resolution_scale                 = true;
                pso.is_multiview                     = xr_multiview;
                pso.clear_depth                      = rhi_depth_load;

                bool pipeline_set = false;

                for (uint32_t i = 0; i < m_draw_calls_prepass_count; i++)
                {
                    const Renderer_DrawCall& draw_call = m_draw_calls_prepass[i];
                    Render* renderable                 = draw_call.renderable;
                    Material* material                 = renderable->GetMaterial();
                    if (!material || material->IsTransparent() || !draw_call.camera_visible)
                    {
                        continue;
                    }
                    if (material->GetProperty(MaterialProperty::Tessellation) <= 0.0f)
                    {
                        continue;
                    }

                    if (!pipeline_set)
                    {
                        cmd_list->SetPipelineState(pso);
                        pipeline_set = true;
                    }

                    bool has_color_texture        = material->HasTextureOfType(MaterialTextureType::Color);
                    m_pcb_pass_cpu.draw_index     = draw_call.draw_data_index;
                    m_pcb_pass_cpu.is_transparent = 0;
                    m_pcb_pass_cpu.material_index = material->GetIndex();
                    m_pcb_pass_cpu.set_f3_value(0.0f, has_color_texture ? 1.0f : 0.0f, static_cast<float>(i));
                    cmd_list->PushConstants(m_pcb_pass_cpu);

                    RHI_CullMode cull_mode = static_cast<RHI_CullMode>(material->GetProperty(MaterialProperty::CullMode));
                    cull_mode              = (pso.rasterizer_state->GetPolygonMode() == RHI_PolygonMode::Wireframe) ? RHI_CullMode::None : cull_mode;
                    cmd_list->SetCullMode(cull_mode);
                    RHI_Buffer* instance_buffer = GeometryBuffer::GetInstanceBuffer() ? GeometryBuffer::GetInstanceBuffer() : GetBuffer(Renderer_Buffer::DummyInstance);
                    cmd_list->SetBufferVertex(renderable->GetVertexBuffer(), instance_buffer);
                    cmd_list->SetBufferIndex(renderable->GetIndexBuffer());

                    cmd_list->DrawIndexed(
                        renderable->GetIndexCount(draw_call.lod_index),
                        renderable->GetIndexOffset(draw_call.lod_index),
                        renderable->GetVertexOffset(draw_call.lod_index),
                        renderable->GetGlobalInstanceOffset() + draw_call.instance_index,
                        draw_call.instance_count
                    );
                }
            }

            float resolution_scale = Renderer::GetResolutionScale();
            cmd_list->Blit(tex_depth, tex_depth_output, false, resolution_scale);

            // early transitions
            {
                tex_depth->SetLayout(RHI_Image_Layout::Attachment, cmd_list);
                tex_depth_output->SetLayout(RHI_Image_Layout::Shader_Read, cmd_list);
            }
        }
        cmd_list->EndTimeblock();
    }

    void Renderer::Pass_GBuffer_Indirect(RHI_CommandList* cmd_list)
    {
        if (m_indirect_draw_count == 0)
        {
            return;
        }

        const bool xr_multiview = Xr::IsSessionRunning() && Xr::GetStereoMode();

        RHI_PipelineState pso;
        pso.name                             = "g_buffer_indirect";
        pso.shaders[RHI_Shader_Type::Vertex] = GetShader(Renderer_Shader::gbuffer_indirect_v);
        pso.shaders[RHI_Shader_Type::Pixel]  = GetShader(Renderer_Shader::gbuffer_indirect_p);
        pso.blend_state                      = GetBlendState(Renderer_BlendState::Off);
        pso.rasterizer_state                 = cvar_wireframe.GetValueAs<bool>() ? GetRasterizerState(Renderer_RasterizerState::Wireframe) : GetRasterizerState(Renderer_RasterizerState::Solid);
        pso.depth_stencil_state              = GetDepthStencilState(Renderer_DepthStencilState::ReadGreaterEqual);
        pso.vrs_input_texture                = cvar_variable_rate_shading.GetValueAs<bool>() ? GetRenderTarget(Renderer_RenderTarget::shading_rate) : nullptr;
        pso.resolution_scale                 = true;
        pso.render_target_color_textures[0]  = GetRenderTarget(Renderer_RenderTarget::gbuffer_color);
        pso.render_target_color_textures[1]  = GetRenderTarget(Renderer_RenderTarget::gbuffer_normal);
        pso.render_target_color_textures[2]  = GetRenderTarget(Renderer_RenderTarget::gbuffer_material);
        pso.render_target_color_textures[3]  = GetRenderTarget(Renderer_RenderTarget::gbuffer_velocity);
        pso.render_target_depth_texture      = GetRenderTarget(Renderer_RenderTarget::gbuffer_depth);
        pso.is_multiview                     = xr_multiview;
        pso.clear_color[0]                   = Color::standard_transparent;
        pso.clear_color[1]                   = Color::standard_transparent;
        pso.clear_color[2]                   = Color::standard_transparent;
        pso.clear_color[3]                   = Color::standard_transparent;
        cmd_list->SetPipelineState(pso);

        cmd_list->SetBuffer(Renderer_BindingsUav::indirect_draw_data, GetBuffer(Renderer_Buffer::IndirectDrawData));
        cmd_list->SetBuffer(Renderer_BindingsUav::meshlet_instances,  GetBuffer(Renderer_Buffer::MeshletInstances));
        cmd_list->SetBuffer(Renderer_BindingsUav::visible_triangles,  GetBuffer(Renderer_Buffer::VisibleTriangles));
        cmd_list->SetBuffer(Renderer_BindingsUav::meshlet_bounds,     GeometryBuffer::GetMeshletBoundsBuffer());

        // triangle cull discarded backfaces already, raster keeps both sides as safety against flag mismatch
        cmd_list->SetCullMode(RHI_CullMode::None);

        cmd_list->DrawIndirect(GetBuffer(Renderer_Buffer::IndirectDrawArgs), 0);

        // update previous transforms for motion vectors
        for (uint32_t i = 0; i < m_draw_call_count; i++)
        {
            Renderer_DrawCall& draw_call = m_draw_calls[i];
            if (draw_call.renderable->GetMaterial() && !draw_call.renderable->GetMaterial()->IsTransparent())
            {
                Entity* entity = draw_call.renderable->GetEntity();
                entity->SetMatrixPrevious(entity->GetMatrix());
            }
        }
    }

    void Renderer::Pass_GBuffer_TessellatedAndTransparent(RHI_CommandList* cmd_list, const bool is_transparent_pass)
    {
        const bool xr_multiview = Xr::IsSessionRunning() && Xr::GetStereoMode();

        RHI_PipelineState pso;
        pso.name                             = is_transparent_pass ? "g_buffer_transparent" : "g_buffer_tessellated";
        pso.shaders[RHI_Shader_Type::Vertex] = GetShader(Renderer_Shader::gbuffer_v);
        pso.shaders[RHI_Shader_Type::Pixel]  = GetShader(Renderer_Shader::gbuffer_p);
        pso.blend_state                      = GetBlendState(Renderer_BlendState::Off);
        pso.rasterizer_state                 = cvar_wireframe.GetValueAs<bool>() ? GetRasterizerState(Renderer_RasterizerState::Wireframe) : GetRasterizerState(Renderer_RasterizerState::Solid);
        pso.depth_stencil_state              = is_transparent_pass ? GetDepthStencilState(Renderer_DepthStencilState::ReadWrite) : GetDepthStencilState(Renderer_DepthStencilState::ReadGreaterEqual);
        pso.vrs_input_texture                = cvar_variable_rate_shading.GetValueAs<bool>() ? GetRenderTarget(Renderer_RenderTarget::shading_rate) : nullptr;
        pso.resolution_scale                 = true;
        pso.render_target_color_textures[0]  = GetRenderTarget(Renderer_RenderTarget::gbuffer_color);
        pso.render_target_color_textures[1]  = GetRenderTarget(Renderer_RenderTarget::gbuffer_normal);
        pso.render_target_color_textures[2]  = GetRenderTarget(Renderer_RenderTarget::gbuffer_material);
        pso.render_target_color_textures[3]  = GetRenderTarget(Renderer_RenderTarget::gbuffer_velocity);
        pso.render_target_depth_texture      = GetRenderTarget(Renderer_RenderTarget::gbuffer_depth);
        pso.is_multiview                     = xr_multiview;
        pso.clear_color[0]                   = rhi_color_load;
        pso.clear_color[1]                   = rhi_color_load;
        pso.clear_color[2]                   = rhi_color_load;
        pso.clear_color[3]                   = rhi_color_load;

        bool pipeline_set = false;
        for (uint32_t i = 0; i < m_draw_call_count; i++)
        {
            const Renderer_DrawCall& draw_call = m_draw_calls[i];
            Render* renderable                 = draw_call.renderable;
            Material* material                 = renderable->GetMaterial();
            if (!material || !draw_call.camera_visible)
            {
                continue;
            }

            const bool is_tessellated = material->GetProperty(MaterialProperty::Tessellation) > 0.0f;
            if (is_transparent_pass)
            {
                if (!material->IsTransparent())
                {
                    continue;
                }
            }
            else
            {
                if (material->IsTransparent())
                {
                    continue;
                }
                if (!is_tessellated)
                {
                    continue;
                }
            }

            RHI_Shader* hull   = is_tessellated ? GetShader(Renderer_Shader::tessellation_h) : nullptr;
            RHI_Shader* domain = is_tessellated ? GetShader(Renderer_Shader::tessellation_d) : nullptr;
            if (!pipeline_set || pso.shaders[RHI_Shader_Type::Hull] != hull || pso.shaders[RHI_Shader_Type::Domain] != domain)
            {
                pso.shaders[RHI_Shader_Type::Hull]   = hull;
                pso.shaders[RHI_Shader_Type::Domain] = domain;
                cmd_list->SetPipelineState(pso);
                pipeline_set = true;
            }

            Entity* entity                = renderable->GetEntity();
            m_pcb_pass_cpu.draw_index     = draw_call.draw_data_index;
            m_pcb_pass_cpu.is_transparent = is_transparent_pass ? 1 : 0;
            m_pcb_pass_cpu.material_index = material->GetIndex();
            cmd_list->PushConstants(m_pcb_pass_cpu);
            entity->SetMatrixPrevious(entity->GetMatrix());

            cmd_list->SetCullMode(cvar_wireframe.GetValueAs<bool>() ? RHI_CullMode::None : static_cast<RHI_CullMode>(material->GetProperty(MaterialProperty::CullMode)));
            RHI_Buffer* instance_buffer = GeometryBuffer::GetInstanceBuffer() ? GeometryBuffer::GetInstanceBuffer() : GetBuffer(Renderer_Buffer::DummyInstance);
            cmd_list->SetBufferVertex(renderable->GetVertexBuffer(), instance_buffer);
            cmd_list->SetBufferIndex(renderable->GetIndexBuffer());

            cmd_list->DrawIndexed(
                renderable->GetIndexCount(draw_call.lod_index),
                renderable->GetIndexOffset(draw_call.lod_index),
                renderable->GetVertexOffset(draw_call.lod_index),
                renderable->GetGlobalInstanceOffset() + draw_call.instance_index,
                draw_call.instance_count
            );

            pso.clear_depth = rhi_depth_load;
        }
    }

    void Renderer::Pass_GBuffer(RHI_CommandList* cmd_list, const bool is_transparent_pass)
    {
        cmd_list->BeginTimeblock(is_transparent_pass ? "g_buffer_transparent" : "g_buffer");
        {
            if (!is_transparent_pass)
            {
                Pass_GBuffer_Indirect(cmd_list);
            }

            Pass_GBuffer_TessellatedAndTransparent(cmd_list, is_transparent_pass);

            // early transition gbuffer color, normal, material, velocity to general (uav read), depth to shader read
            GetRenderTarget(Renderer_RenderTarget::gbuffer_color)   ->SetLayout(RHI_Image_Layout::General,     cmd_list);
            GetRenderTarget(Renderer_RenderTarget::gbuffer_normal)  ->SetLayout(RHI_Image_Layout::General,     cmd_list);
            GetRenderTarget(Renderer_RenderTarget::gbuffer_material)->SetLayout(RHI_Image_Layout::General,     cmd_list);
            GetRenderTarget(Renderer_RenderTarget::gbuffer_velocity)->SetLayout(RHI_Image_Layout::General,     cmd_list);
            GetRenderTarget(Renderer_RenderTarget::gbuffer_depth)   ->SetLayout(RHI_Image_Layout::Shader_Read, cmd_list);
        }
        cmd_list->EndTimeblock();
    }

    void Renderer::Pass_MeshletVisualize(RHI_CommandList* cmd_list)
    {
        RHI_Texture* tex_debug = GetRenderTarget(Renderer_RenderTarget::debug_output);
        if (!tex_debug)
        {
            return;
        }

        uint32_t mode = cvar_meshlet_visualize.GetValueAs<uint32_t>();
        if (mode == 0)
        {
            return;
        }

        RHI_Texture* tex_depth = GetRenderTarget(Renderer_RenderTarget::gbuffer_depth);
        if (!tex_depth || m_indirect_draw_count == 0)
        {
            return;
        }

        bool xr_multiview = Xr::IsSessionRunning() && Xr::GetStereoMode();

        cmd_list->BeginTimeblock("meshlet_visualize");
        {
            // gbuffer left depth in Shader_Read; promote it back to an attachment so we can do a read-only depth test
            tex_depth->SetLayout(RHI_Image_Layout::Attachment, cmd_list);

            // mode 1/2 color/wireframe by meshlet id, mode 3/4 color/wireframe by post-cull draw id
            bool wireframe                  = (mode == 2 || mode == 4);
            bool color_by_draw_id           = (mode == 3 || mode == 4);
            RHI_RasterizerState* rasterizer = wireframe ? GetRasterizerState(Renderer_RasterizerState::Wireframe) : GetRasterizerState(Renderer_RasterizerState::Solid);

            RHI_PipelineState pso;
            pso.name                             = "meshlet_visualize";
            pso.shaders[RHI_Shader_Type::Vertex] = GetShader(Renderer_Shader::meshlet_visualize_v);
            pso.shaders[RHI_Shader_Type::Pixel]  = GetShader(Renderer_Shader::meshlet_visualize_p);
            pso.blend_state                      = GetBlendState(Renderer_BlendState::Off);
            pso.rasterizer_state                 = rasterizer;
            // greater_equal mirrors the gbuffer test instead of equal which is fragile under fp drift between two vertex paths
            pso.depth_stencil_state              = GetDepthStencilState(Renderer_DepthStencilState::ReadGreaterEqual);
            pso.resolution_scale                 = true;
            pso.render_target_color_textures[0]  = tex_debug;
            pso.render_target_depth_texture      = tex_depth;
            pso.is_multiview                     = xr_multiview;
            pso.clear_color[0]                   = Color::standard_black;
            cmd_list->SetPipelineState(pso);

            cmd_list->SetBuffer(Renderer_BindingsUav::indirect_draw_data, GetBuffer(Renderer_Buffer::IndirectDrawData));
            cmd_list->SetBuffer(Renderer_BindingsUav::meshlet_instances,  GetBuffer(Renderer_Buffer::MeshletInstances));
            cmd_list->SetBuffer(Renderer_BindingsUav::visible_triangles,  GetBuffer(Renderer_Buffer::VisibleTriangles));
            cmd_list->SetBuffer(Renderer_BindingsUav::meshlet_bounds,     GeometryBuffer::GetMeshletBoundsBuffer());

            // wireframe shows both faces so rear edges of thin meshlets stay visible, solid mode leaves culling to the triangle cull pass
            cmd_list->SetCullMode(RHI_CullMode::None);

            // f3.x: 0 = color by global meshlet index, 1 = color by post-cull draw id
            m_pcb_pass_cpu.set_f3_value(color_by_draw_id ? 1.0f : 0.0f, 0.0f, 0.0f);
            cmd_list->PushConstants(m_pcb_pass_cpu);

            cmd_list->DrawIndirect(GetBuffer(Renderer_Buffer::IndirectDrawArgs), 0);

            // restore the layout the rest of the frame expects
            tex_depth->SetLayout(RHI_Image_Layout::Shader_Read, cmd_list);
        }
        cmd_list->EndTimeblock();
    }
}
