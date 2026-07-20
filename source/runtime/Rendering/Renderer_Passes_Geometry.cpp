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
#include "../World/Components/Camera.h"
#include "../World/World.h"
#include "../RHI/RHI_CommandList.h"
#include "../RHI/RHI_Buffer.h"
#include "../RHI/RHI_AccelerationStructure.h"
#include "../RHI/RHI_RasterizerState.h"
#include "../RHI/RHI_DepthStencilState.h"
#include "../RHI/RHI_Device.h"
#include "../RHI/RHI_Shader.h"
#include "../RHI/RHI_Texture.h"
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
    namespace
    {
        struct IndexedBatchKey
        {
            RHI_Buffer* vertex_buffer = nullptr;
            RHI_Buffer* index_buffer  = nullptr;
            RHI_CullMode cull_mode    = RHI_CullMode::Back;
            bool alpha_tested         = false;

            bool operator==(const IndexedBatchKey& other) const
            {
                return vertex_buffer == other.vertex_buffer && index_buffer == other.index_buffer && cull_mode == other.cull_mode && alpha_tested == other.alpha_tested;
            }
        };

        struct IndexedBatchKeyHash
        {
            size_t operator()(const IndexedBatchKey& key) const
            {
                size_t hash = std::hash<RHI_Buffer*>{}(key.vertex_buffer);
                hash ^= std::hash<RHI_Buffer*>{}(key.index_buffer) + 0x9e3779b9 + (hash << 6) + (hash >> 2);
                hash ^= std::hash<uint32_t>{}(static_cast<uint32_t>(key.cull_mode)) + 0x9e3779b9 + (hash << 6) + (hash >> 2);
                hash ^= std::hash<bool>{}(key.alpha_tested) + 0x9e3779b9 + (hash << 6) + (hash >> 2);
                return hash;
            }
        };
    }

    void Renderer::Pass_ShadowMaps(RHI_CommandList* cmd_list)
    {
        if (World::GetLightCount() == 0)
        {
            return;
        }

        // shadow atlas is unused when full ray traced shadows own visibility
        const bool tlas_available = RHI_Device::IsSupportedRayTracing() && GetTopLevelAccelerationStructure() != nullptr;
        if (cvar_ray_traced_shadows.GetValueAs<bool>() && tlas_available)
        {
            return;
        }

        struct ShadowBatch
        {
            RHI_Buffer* vertex_buffer = nullptr;
            RHI_Buffer* index_buffer  = nullptr;
            RHI_CullMode cull_mode    = RHI_CullMode::Back;
            bool alpha_tested         = false;
            uint32_t argument_offset  = 0;
            vector<Sb_IndirectDrawArgs> arguments;
        };

        struct ShadowSlice
        {
            Light* light = nullptr;
            uint32_t array_index = 0;
            math::Rectangle rect;
            vector<ShadowBatch> batches;
            unordered_map<IndexedBatchKey, uint32_t, IndexedBatchKeyHash> batch_lookup;
            vector<const Renderer_DrawCall*> visible_draws;
            vector<const Renderer_DrawCall*> direct_draws;
        };

        vector<ShadowSlice> slices;
        uint32_t argument_count = 0;
        bool has_alpha_draws = false;
        for (Entity* entity_light : World::GetEntitiesLights())
        {
            Light* light = entity_light->GetComponent<Light>();
            if (!light->GetFlag(LightFlags::Shadows) || light->GetIntensityRadiometric() == 0.0f)
            {
                continue;
            }

            for (uint32_t array_index = 0; array_index < light->GetSliceCount(); array_index++)
            {
                const math::Rectangle& rect = light->GetAtlasRectangle(array_index);
                if (!rect.IsDefined())
                {
                    continue;
                }

                ShadowSlice& slice = slices.emplace_back();
                slice.light       = light;
                slice.array_index = array_index;
                slice.rect        = rect;

                for (uint32_t i = 0; i < m_draw_call_count; i++)
                {
                    const Renderer_DrawCall& draw_call = m_draw_calls[i];
                    Render* renderable                 = draw_call.renderable;
                    Material* material                 = renderable->GetMaterial();
                    const float shadow_distance        = renderable->GetMaxShadowDistance();
                    if (!material || material->IsTransparent() || !renderable->HasFlag(RenderableFlags::CastsShadows) || draw_call.distance_squared > shadow_distance * shadow_distance)
                    {
                        continue;
                    }

                    if (!light->IsInViewFrustum(renderable, array_index))
                    {
                        continue;
                    }

                    slice.visible_draws.push_back(&draw_call);
                    RHI_Buffer* vertex_buffer = renderable->GetVertexBuffer();
                    RHI_Buffer* index_buffer  = renderable->GetIndexBuffer();
                    const bool alpha_tested   = array_index == 0 && light->GetLightType() == LightType::Directional && material->IsAlphaTested();
                    has_alpha_draws          |= alpha_tested;
                    const bool can_batch      = draw_call.instance_count == 1 && renderable->GetGlobalInstanceOffset() == 0 && vertex_buffer && index_buffer;
                    if (!can_batch)
                    {
                        slice.direct_draws.push_back(&draw_call);
                        continue;
                    }

                    const RHI_CullMode cull_mode = static_cast<RHI_CullMode>(material->GetProperty(MaterialProperty::CullMode));
                    const IndexedBatchKey key    = { vertex_buffer, index_buffer, cull_mode, alpha_tested };
                    auto [it, inserted]          = slice.batch_lookup.try_emplace(key, static_cast<uint32_t>(slice.batches.size()));
                    if (inserted)
                    {
                        ShadowBatch* batch   = &slice.batches.emplace_back();
                        batch->vertex_buffer = vertex_buffer;
                        batch->index_buffer  = index_buffer;
                        batch->cull_mode     = cull_mode;
                        batch->alpha_tested  = alpha_tested;
                    }
                    ShadowBatch& batch = slice.batches[it->second];

                    const bool close_to_shadow      = renderable->GetDistanceSquared() < 100.0f * 100.0f;
                    const uint32_t lod_index_bias   = light->GetLightType() == LightType::Directional ? 1 : 0;
                    const uint32_t lod_index_shadow = clamp(renderable->GetLodIndex() + lod_index_bias, 0u, renderable->GetLodCount() - 1);
                    const uint32_t lod_index        = close_to_shadow ? draw_call.lod_index : lod_index_shadow;

                    Sb_IndirectDrawArgs& argument = batch.arguments.emplace_back();
                    argument.index_count          = renderable->GetIndexCount(lod_index);
                    argument.instance_count       = 1;
                    argument.first_index          = renderable->GetIndexOffset(lod_index);
                    argument.vertex_offset        = static_cast<int32_t>(renderable->GetVertexOffset(lod_index));
                    argument.first_instance       = draw_call.draw_data_index;
                    argument_count++;
                }
            }
        }

        RHI_Buffer* argument_buffer = GetBuffer(Renderer_Buffer::CpuIndirectDrawArgs);
        RHI_Shader* multi_vertex_shader = GetShader(Renderer_Shader::depth_light_multi_draw_v);
        RHI_Shader* multi_pixel_shader  = GetShader(Renderer_Shader::depth_light_multi_draw_alpha_color_p);
        const bool shaders_supported    = multi_vertex_shader && multi_vertex_shader->IsCompiled() && (!has_alpha_draws || (multi_pixel_shader && multi_pixel_shader->IsCompiled()));
        const bool use_batches          = shaders_supported && argument_buffer && argument_count != 0 && m_cpu_indirect_draw_arg_count + argument_count <= renderer_max_cpu_indirect_draws;
        if (use_batches)
        {
            vector<Sb_IndirectDrawArgs> arguments;
            arguments.reserve(argument_count);
            for (ShadowSlice& slice : slices)
            {
                for (ShadowBatch& batch : slice.batches)
                {
                    batch.argument_offset = static_cast<uint32_t>((m_cpu_indirect_draw_arg_count + arguments.size()) * sizeof(Sb_IndirectDrawArgs));
                    arguments.insert(arguments.end(), batch.arguments.begin(), batch.arguments.end());
                }
            }
            cmd_list->UpdateBuffer(argument_buffer, m_cpu_indirect_draw_arg_count * sizeof(Sb_IndirectDrawArgs), arguments.size() * sizeof(Sb_IndirectDrawArgs), arguments.data());
            m_cpu_indirect_draw_arg_count += argument_count;
        }

        RHI_PipelineState pso;
        pso.name                             = "shadow_maps";
        pso.shaders[RHI_Shader_Type::Vertex] = use_batches ? multi_vertex_shader : GetShader(Renderer_Shader::depth_light_v);
        pso.blend_state                      = GetBlendState(Renderer_BlendState::Off);
        pso.depth_stencil_state              = GetDepthStencilState(Renderer_DepthStencilState::ReadWrite);
        pso.clear_depth                      = 0.0f;
        pso.render_target_depth_texture      = GetRenderTarget(Renderer_RenderTarget::shadow_atlas);
        pso.rasterizer_state = GetRasterizerState(Renderer_RasterizerState::Light_directional);

        cmd_list->BeginTimeblock(pso.name);
        {
            cmd_list->SetPipelineState(pso);

            for (ShadowSlice& slice : slices)
            {
                Light* light = slice.light;
                RHI_RasterizerState* rasterizer_state = light->GetLightType() == LightType::Directional ? GetRasterizerState(Renderer_RasterizerState::Light_directional) : GetRasterizerState(Renderer_RasterizerState::Light_point_spot);
                RHI_Viewport viewport;
                viewport.x      = slice.rect.x;
                viewport.y      = slice.rect.y;
                viewport.width  = slice.rect.width;
                viewport.height = slice.rect.height;
                cmd_list->SetViewport(viewport);
                cmd_list->SetScissorRectangle(slice.rect);

                if (use_batches)
                {
                    for (ShadowBatch& batch : slice.batches)
                    {
                        RHI_Shader* vertex_shader = multi_vertex_shader;
                        RHI_Shader* pixel_shader  = batch.alpha_tested ? multi_pixel_shader : nullptr;
                        if (pso.shaders[RHI_Shader_Type::Vertex] != vertex_shader || pso.shaders[RHI_Shader_Type::Pixel] != pixel_shader || pso.rasterizer_state != rasterizer_state)
                        {
                            pso.shaders[RHI_Shader_Type::Vertex] = vertex_shader;
                            pso.shaders[RHI_Shader_Type::Pixel]  = pixel_shader;
                            pso.rasterizer_state                 = rasterizer_state;
                            cmd_list->SetPipelineState(pso);
                            cmd_list->SetViewport(viewport);
                            cmd_list->SetScissorRectangle(slice.rect);
                        }

                        m_pcb_pass_cpu.draw_index     = numeric_limits<uint32_t>::max();
                        m_pcb_pass_cpu.is_transparent = 0;
                        m_pcb_pass_cpu.set_f3_value2(static_cast<float>(light->GetIndex()), static_cast<float>(slice.array_index), 0.0f);
                        cmd_list->PushConstants(m_pcb_pass_cpu);
                        cmd_list->SetCullMode(batch.cull_mode);
                        cmd_list->SetBufferVertex(batch.vertex_buffer);
                        cmd_list->SetBufferIndex(batch.index_buffer);
                        cmd_list->DrawIndexedIndirect(argument_buffer, batch.argument_offset, static_cast<uint32_t>(batch.arguments.size()));
                    }
                }

                auto draw_direct = [&](const Renderer_DrawCall& draw_call)
                {
                    Render* renderable = draw_call.renderable;
                    Material* material = renderable->GetMaterial();
                    const bool is_first_cascade = slice.array_index == 0 && light->GetLightType() == LightType::Directional;
                    RHI_Shader* vertex_shader   = use_batches ? multi_vertex_shader : GetShader(Renderer_Shader::depth_light_v);
                    RHI_Shader* pixel_shader    = is_first_cascade && material->IsAlphaTested() ? (use_batches ? multi_pixel_shader : GetShader(Renderer_Shader::depth_light_alpha_color_p)) : nullptr;
                    if (pso.shaders[RHI_Shader_Type::Vertex] != vertex_shader || pso.shaders[RHI_Shader_Type::Pixel] != pixel_shader || pso.rasterizer_state != rasterizer_state)
                    {
                        pso.shaders[RHI_Shader_Type::Vertex] = vertex_shader;
                        pso.shaders[RHI_Shader_Type::Pixel]  = pixel_shader;
                        pso.rasterizer_state                 = rasterizer_state;
                        cmd_list->SetPipelineState(pso);
                        cmd_list->SetViewport(viewport);
                        cmd_list->SetScissorRectangle(slice.rect);
                    }

                    m_pcb_pass_cpu.draw_index     = draw_call.draw_data_index;
                    m_pcb_pass_cpu.is_transparent = 0;
                    m_pcb_pass_cpu.material_index = material->GetIndex();
                    m_pcb_pass_cpu.set_f3_value(material->HasTextureOfType(MaterialTextureType::Color) ? 1.0f : 0.0f);
                    m_pcb_pass_cpu.set_f3_value2(static_cast<float>(light->GetIndex()), static_cast<float>(slice.array_index), 0.0f);
                    cmd_list->PushConstants(m_pcb_pass_cpu);
                    cmd_list->SetCullMode(static_cast<RHI_CullMode>(material->GetProperty(MaterialProperty::CullMode)));
                    RHI_Buffer* instance_buffer = GeometryBuffer::GetInstanceBuffer() ? GeometryBuffer::GetInstanceBuffer() : GetBuffer(Renderer_Buffer::DummyInstance);
                    cmd_list->SetBufferVertex(renderable->GetVertexBuffer(), instance_buffer);
                    cmd_list->SetBufferIndex(renderable->GetIndexBuffer());

                    const bool close_to_shadow      = renderable->GetDistanceSquared() < 100.0f * 100.0f;
                    const uint32_t lod_index_bias   = light->GetLightType() == LightType::Directional ? 1 : 0;
                    const uint32_t lod_index_shadow = clamp(renderable->GetLodIndex() + lod_index_bias, 0u, renderable->GetLodCount() - 1);
                    const uint32_t lod_index        = close_to_shadow ? draw_call.lod_index : lod_index_shadow;
                    cmd_list->DrawIndexed(renderable->GetIndexCount(lod_index), renderable->GetIndexOffset(lod_index), renderable->GetVertexOffset(lod_index), renderable->GetGlobalInstanceOffset() + draw_call.instance_index, draw_call.instance_count);
                };

                const vector<const Renderer_DrawCall*>& draws = use_batches ? slice.direct_draws : slice.visible_draws;
                for (const Renderer_DrawCall* draw_call : draws)
                {
                    draw_direct(*draw_call);
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

        struct HiZBatch
        {
            RHI_Buffer* vertex_buffer = nullptr;
            RHI_Buffer* index_buffer  = nullptr;
            RHI_CullMode cull_mode    = RHI_CullMode::Back;
            uint32_t argument_offset  = 0;
            vector<Sb_IndirectDrawArgs> arguments;
        };

        vector<HiZBatch> batches;
        unordered_map<IndexedBatchKey, uint32_t, IndexedBatchKeyHash> batch_lookup;
        vector<const Renderer_DrawCall*> visible_draws;
        vector<const Renderer_DrawCall*> direct_draws;
        uint32_t argument_count = 0;
        if (render_occluders)
        {
            for (uint32_t i = 0; i < m_draw_calls_prepass_count; i++)
            {
                const Renderer_DrawCall& draw_call = m_draw_calls_prepass[i];
                if (!draw_call.is_occluder)
                {
                    continue;
                }

                visible_draws.push_back(&draw_call);
                Render* renderable        = draw_call.renderable;
                RHI_Buffer* vertex_buffer = renderable->GetVertexBuffer();
                RHI_Buffer* index_buffer  = renderable->GetIndexBuffer();
                const bool can_batch      = draw_call.instance_count == 1 && renderable->GetGlobalInstanceOffset() == 0 && vertex_buffer && index_buffer;
                if (!can_batch)
                {
                    direct_draws.push_back(&draw_call);
                    continue;
                }

                RHI_CullMode cull_mode = static_cast<RHI_CullMode>(renderable->GetMaterial()->GetProperty(MaterialProperty::CullMode));
                cull_mode              = GetRasterizerState(Renderer_RasterizerState::Solid)->GetPolygonMode() == RHI_PolygonMode::Wireframe ? RHI_CullMode::None : cull_mode;
                const IndexedBatchKey key = { vertex_buffer, index_buffer, cull_mode, false };
                auto [it, inserted]       = batch_lookup.try_emplace(key, static_cast<uint32_t>(batches.size()));
                if (inserted)
                {
                    HiZBatch* batch      = &batches.emplace_back();
                    batch->vertex_buffer = vertex_buffer;
                    batch->index_buffer  = index_buffer;
                    batch->cull_mode     = cull_mode;
                }
                HiZBatch& batch = batches[it->second];

                Sb_IndirectDrawArgs& argument = batch.arguments.emplace_back();
                argument.index_count          = renderable->GetIndexCount(draw_call.lod_index);
                argument.instance_count       = 1;
                argument.first_index          = renderable->GetIndexOffset(draw_call.lod_index);
                argument.vertex_offset        = static_cast<int32_t>(renderable->GetVertexOffset(draw_call.lod_index));
                argument.first_instance       = draw_call.draw_data_index;
                argument_count++;
            }
        }

        RHI_Buffer* argument_buffer = GetBuffer(Renderer_Buffer::CpuIndirectDrawArgs);
        RHI_Shader* multi_vertex_shader = GetShader(Renderer_Shader::depth_prepass_multi_draw_v);
        const bool use_batches          = multi_vertex_shader && multi_vertex_shader->IsCompiled() && argument_buffer && argument_count != 0 && m_cpu_indirect_draw_arg_count + argument_count <= renderer_max_cpu_indirect_draws;
        if (use_batches)
        {
            vector<Sb_IndirectDrawArgs> arguments;
            arguments.reserve(argument_count);
            for (HiZBatch& batch : batches)
            {
                batch.argument_offset = static_cast<uint32_t>((m_cpu_indirect_draw_arg_count + arguments.size()) * sizeof(Sb_IndirectDrawArgs));
                arguments.insert(arguments.end(), batch.arguments.begin(), batch.arguments.end());
            }
            cmd_list->UpdateBuffer(argument_buffer, m_cpu_indirect_draw_arg_count * sizeof(Sb_IndirectDrawArgs), arguments.size() * sizeof(Sb_IndirectDrawArgs), arguments.data());
            m_cpu_indirect_draw_arg_count += argument_count;
        }

        // always start the render pass so the depth texture is cleared to far plane (0.0).
        // without this, the blit and downscale would propagate stale depth into the hi-z
        // mip chain, causing the cull shader to incorrectly occlude objects.
        {
            RHI_PipelineState pso;
            pso.name                             = "occluders";
            pso.shaders[RHI_Shader_Type::Vertex] = use_batches ? multi_vertex_shader : GetShader(Renderer_Shader::depth_prepass_v);
            pso.rasterizer_state                 = GetRasterizerState(Renderer_RasterizerState::Solid);
            pso.blend_state                      = GetBlendState(Renderer_BlendState::Off);
            pso.depth_stencil_state              = GetDepthStencilState(Renderer_DepthStencilState::ReadWrite);
            pso.render_target_depth_texture      = tex_occluders;
            pso.resolution_scale                 = true;
            pso.clear_depth                      = 0.0f;

            cmd_list->SetPipelineState(pso);

            if (render_occluders)
            {
                if (use_batches)
                {
                    m_pcb_pass_cpu.draw_index = numeric_limits<uint32_t>::max();
                    cmd_list->PushConstants(m_pcb_pass_cpu);
                    for (HiZBatch& batch : batches)
                    {
                        cmd_list->SetCullMode(batch.cull_mode);
                        cmd_list->SetBufferVertex(batch.vertex_buffer);
                        cmd_list->SetBufferIndex(batch.index_buffer);
                        cmd_list->DrawIndexedIndirect(argument_buffer, batch.argument_offset, static_cast<uint32_t>(batch.arguments.size()));
                    }
                }

                auto draw_direct = [&](const Renderer_DrawCall& draw_call)
                {
                    Render* renderable = draw_call.renderable;
                    RHI_CullMode cull_mode = static_cast<RHI_CullMode>(renderable->GetMaterial()->GetProperty(MaterialProperty::CullMode));
                    cull_mode              = (pso.rasterizer_state->GetPolygonMode() == RHI_PolygonMode::Wireframe) ? RHI_CullMode::None : cull_mode;
                    cmd_list->SetCullMode(cull_mode);

                    m_pcb_pass_cpu.draw_index = draw_call.draw_data_index;
                    cmd_list->PushConstants(m_pcb_pass_cpu);

                    cmd_list->SetBufferVertex(renderable->GetVertexBuffer());
                    cmd_list->SetBufferIndex(renderable->GetIndexBuffer());

                    cmd_list->DrawIndexed(renderable->GetIndexCount(draw_call.lod_index), renderable->GetIndexOffset(draw_call.lod_index), renderable->GetVertexOffset(draw_call.lod_index));
                };

                const vector<const Renderer_DrawCall*>& draws = use_batches ? direct_draws : visible_draws;
                for (const Renderer_DrawCall* draw_call : draws)
                {
                    draw_direct(*draw_call);
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

        RHI_Texture* tex_occluders_hiz = GetRenderTarget(Renderer_RenderTarget::gbuffer_depth_occluders_hiz);
        const float  max_hiz_mip       = static_cast<float>(tex_occluders_hiz->GetMipCount() - 1);

        // phase a, per-instance distance + side-frustum + hi-z, survivors land in surviving_instances and bump instance_dispatch_args.group_count_x
        cmd_list->BeginTimeblock("instance_cull");
        {
            RHI_PipelineState pso;
            pso.name             = "instance_cull";
            pso.shaders[Compute] = GetShader(Renderer_Shader::instance_cull_c);
            cmd_list->SetPipelineState(pso);

            cmd_list->SetTexture(Renderer_BindingsSrv::tex, tex_occluders_hiz);

            cmd_list->SetBuffer(Renderer_BindingsUav::indirect_draw_data,     GetBuffer(Renderer_Buffer::IndirectDrawData));
            cmd_list->SetBuffer(Renderer_BindingsUav::cull_tasks,             GetBuffer(Renderer_Buffer::CullTasks));
            cmd_list->SetBuffer(Renderer_BindingsUav::surviving_instances,    GetBuffer(Renderer_Buffer::SurvivingInstances));
            cmd_list->SetBuffer(Renderer_BindingsUav::instance_dispatch_args, GetBuffer(Renderer_Buffer::InstanceDispatchArgs));

            // f4_value: x = instance task count, y = max hiz mip, z = surviving instances cap (drop survivors past this)
            m_pcb_pass_cpu.set_f4_value(
                static_cast<float>(m_cull_task_count),
                max_hiz_mip,
                static_cast<float>(GetBuffer(Renderer_Buffer::SurvivingInstances)->GetElementCount()),
                0.0f);
            cmd_list->PushConstants(m_pcb_pass_cpu);

            uint32_t thread_group_count = (m_cull_task_count + 255) / 256;
            cmd_list->Dispatch(thread_group_count, 1, 1);
        }
        cmd_list->EndTimeblock();

        // phase b, expand the meshlets of the surviving instances, per-meshlet cone + frustum + hi-z, one workgroup per survivor
        cmd_list->BeginTimeblock("indirect_cull_meshlet");
        {
            RHI_PipelineState pso;
            pso.name             = "indirect_cull_meshlet";
            pso.shaders[Compute] = GetShader(Renderer_Shader::indirect_cull_c);
            cmd_list->SetPipelineState(pso);

            cmd_list->SetTexture(Renderer_BindingsSrv::tex, tex_occluders_hiz);

            cmd_list->SetBuffer(Renderer_BindingsUav::indirect_draw_data,     GetBuffer(Renderer_Buffer::IndirectDrawData));
            cmd_list->SetBuffer(Renderer_BindingsUav::meshlet_bounds,         GeometryBuffer::GetMeshletBoundsBuffer());
            cmd_list->SetBuffer(Renderer_BindingsUav::surviving_instances,    GetBuffer(Renderer_Buffer::SurvivingInstances));
            cmd_list->SetBuffer(Renderer_BindingsUav::meshlet_instances,      GetBuffer(Renderer_Buffer::MeshletInstances));
            cmd_list->SetBuffer(Renderer_BindingsUav::triangle_dispatch_args, GetBuffer(Renderer_Buffer::TriangleDispatchArgs));

            // f4_value: x = max hiz mip, y = meshlet instances cap (drop survivors past this)
            m_pcb_pass_cpu.set_f4_value(
                max_hiz_mip,
                static_cast<float>(GetBuffer(Renderer_Buffer::MeshletInstances)->GetElementCount()),
                0.0f, 0.0f);
            cmd_list->PushConstants(m_pcb_pass_cpu);

            cmd_list->DispatchIndirect(GetBuffer(Renderer_Buffer::InstanceDispatchArgs), 0);
        }
        cmd_list->EndTimeblock();

        // pass 2, per-triangle frustum + backface + sub-pixel cull, dispatched indirect with one workgroup per surviving meshlet
        cmd_list->BeginTimeblock("indirect_cull_triangle");
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

            // f4_value: x = meshlet instances cap, y = per-half visible triangle cap (also the alpha region base, drop survivors past it)
            m_pcb_pass_cpu.set_f4_value(
                static_cast<float>(GetBuffer(Renderer_Buffer::MeshletInstances)->GetElementCount()),
                static_cast<float>(GetBuffer(Renderer_Buffer::VisibleTriangles)->GetElementCount() / 2),
                0.0f, 0.0f);
            cmd_list->PushConstants(m_pcb_pass_cpu);

            cmd_list->DispatchIndirect(GetBuffer(Renderer_Buffer::TriangleDispatchArgs), 0);
        }
        cmd_list->EndTimeblock();
    }

    void Renderer::Pass_Depth_Prepass(RHI_CommandList* cmd_list)
    {
        RHI_Texture* tex_depth = GetRenderTarget(Renderer_RenderTarget::gbuffer_depth);

        bool is_wireframe                     = cvar_wireframe.GetValueAs<bool>();
        bool xr_multiview                     = Xr::IsSessionRunning() && Xr::GetStereoMode();
        RHI_RasterizerState* rasterizer_state = GetRasterizerState(Renderer_RasterizerState::Solid);
        rasterizer_state                      = is_wireframe ? GetRasterizerState(Renderer_RasterizerState::Wireframe) : rasterizer_state;

        cmd_list->BeginTimeblock("depth_prepass");
        {
            // indirect prepass, two draws over the split survivor list (must match the g-buffer indirect path)
            // opaque half runs with no pixel shader so the hardware uses double-speed depth, the alpha half
            // runs the alpha-test pixel shader so foliage cutouts still discard, both pull from one buffer
            // with a region base pushed in f4_value.x and first_vertex 0
            {
                const uint32_t arg_stride = static_cast<uint32_t>(sizeof(Sb_IndirectDrawArgs));

                RHI_PipelineState pso;
                pso.name                             = "depth_prepass_indirect";
                pso.shaders[RHI_Shader_Type::Vertex] = GetShader(Renderer_Shader::depth_prepass_indirect_v);
                pso.rasterizer_state                 = rasterizer_state;
                pso.blend_state                      = GetBlendState(Renderer_BlendState::Off);
                pso.depth_stencil_state              = GetDepthStencilState(Renderer_DepthStencilState::ReadWrite);
                pso.vrs_input_texture                = cvar_variable_rate_shading.GetValueAs<bool>() ? GetRenderTarget(Renderer_RenderTarget::shading_rate) : nullptr;
                pso.render_target_depth_texture      = tex_depth;
                pso.resolution_scale                 = true;
                pso.is_multiview                     = xr_multiview;

                // opaque half, no pixel shader, this pass also clears the depth target for the whole prepass
                // the clear runs unconditionally so the transparent ocean still tests against a fresh depth buffer when no opaque geometry is visible
                pso.shaders[RHI_Shader_Type::Pixel]  = nullptr;
                pso.clear_depth                      = 0.0f;
                cmd_list->SetPipelineState(pso);

                if (m_indirect_draw_count > 0)
                {
                    cmd_list->SetBuffer(Renderer_BindingsUav::indirect_draw_data, GetBuffer(Renderer_Buffer::IndirectDrawData));
                    cmd_list->SetBuffer(Renderer_BindingsUav::meshlet_instances,  GetBuffer(Renderer_Buffer::MeshletInstances));
                    cmd_list->SetBuffer(Renderer_BindingsUav::visible_triangles,  GetBuffer(Renderer_Buffer::VisibleTriangles));
                    cmd_list->SetBuffer(Renderer_BindingsUav::meshlet_bounds,     GeometryBuffer::GetMeshletBoundsBuffer());
                    cmd_list->SetCullMode(RHI_CullMode::None);
                    m_pcb_pass_cpu.set_f4_value(0.0f, 0.0f, 0.0f, 0.0f);
                    cmd_list->PushConstants(m_pcb_pass_cpu);
                    cmd_list->DrawIndirect(GetBuffer(Renderer_Buffer::IndirectDrawArgs), 0);

                    // alpha-tested half, alpha-test pixel shader discards cutout texels, depth already cleared so load it
                    pso.shaders[RHI_Shader_Type::Pixel]  = GetShader(Renderer_Shader::depth_prepass_indirect_alpha_test_p);
                    pso.clear_depth                      = rhi_depth_load;
                    cmd_list->SetPipelineState(pso);
                    cmd_list->SetBuffer(Renderer_BindingsUav::indirect_draw_data, GetBuffer(Renderer_Buffer::IndirectDrawData));
                    cmd_list->SetBuffer(Renderer_BindingsUav::meshlet_instances,  GetBuffer(Renderer_Buffer::MeshletInstances));
                    cmd_list->SetBuffer(Renderer_BindingsUav::visible_triangles,  GetBuffer(Renderer_Buffer::VisibleTriangles));
                    cmd_list->SetBuffer(Renderer_BindingsUav::meshlet_bounds,     GeometryBuffer::GetMeshletBoundsBuffer());
                    cmd_list->SetCullMode(RHI_CullMode::None);
                    m_pcb_pass_cpu.set_f4_value(static_cast<float>(GetBuffer(Renderer_Buffer::VisibleTriangles)->GetElementCount() / 2), 0.0f, 0.0f, 0.0f);
                    cmd_list->PushConstants(m_pcb_pass_cpu);
                    cmd_list->DrawIndirect(GetBuffer(Renderer_Buffer::IndirectDrawArgs), arg_stride);
                }
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

        }
        cmd_list->EndTimeblock();
    }

    void Renderer::Pass_GBuffer_Indirect(RHI_CommandList* cmd_list)
    {
        const bool xr_multiview = Xr::IsSessionRunning() && Xr::GetStereoMode();

        RHI_PipelineState pso;
        pso.name                             = "g_buffer_indirect";
        pso.shaders[RHI_Shader_Type::Vertex] = GetShader(Renderer_Shader::gbuffer_indirect_v);
        pso.shaders[RHI_Shader_Type::Pixel]  = GetShader(Renderer_Shader::gbuffer_indirect_p);
        pso.blend_state                      = GetBlendState(Renderer_BlendState::Off);
        pso.rasterizer_state                 = cvar_wireframe.GetValueAs<bool>() ? GetRasterizerState(Renderer_RasterizerState::Wireframe) : GetRasterizerState(Renderer_RasterizerState::Solid);
        // equal-z gates the gbuffer on the depth value the prepass actually wrote,
        // alpha-tested geometry (tree leaves, ivy, foliage cards) discards in the prepass which leaves the depth at whatever opaque
        // surface sits behind the transparent leaf pixel (terrain, sky clear value of zero), greater-equal would happily pass those
        // pixels because the leaf quad is in front of that stored depth and the gbuffer pixel shader has no alpha discard of its own,
        // the net effect was leaf alpha cutouts rendering as solid quads in close range where get_alpha_threshold is non-zero,
        // switching to equal keeps the pixel shader discard-free (early-z preserved) and lets only the prepass-survivors draw
        pso.depth_stencil_state              = GetDepthStencilState(Renderer_DepthStencilState::ReadEqual);
        pso.vrs_input_texture                = cvar_variable_rate_shading.GetValueAs<bool>() ? GetRenderTarget(Renderer_RenderTarget::shading_rate) : nullptr;
        pso.resolution_scale                 = true;
        pso.render_target_color_textures[0]  = GetRenderTarget(Renderer_RenderTarget::gbuffer_color);
        pso.render_target_color_textures[1]  = GetRenderTarget(Renderer_RenderTarget::gbuffer_normal);
        pso.render_target_color_textures[2]  = GetRenderTarget(Renderer_RenderTarget::gbuffer_material);
        pso.render_target_color_textures[3]  = GetRenderTarget(Renderer_RenderTarget::gbuffer_velocity);
        pso.render_target_depth_texture      = GetRenderTarget(Renderer_RenderTarget::gbuffer_depth);
        pso.is_multiview                     = xr_multiview;
        // the opaque draw clears the g-buffer, the alpha draw loads it so it does not wipe the opaque output
        pso.clear_color[0]                   = Color::standard_transparent;
        pso.clear_color[1]                   = Color::standard_transparent;
        pso.clear_color[2]                   = Color::standard_transparent;
        pso.clear_color[3]                   = Color::standard_transparent;

        const uint32_t arg_stride = static_cast<uint32_t>(sizeof(Sb_IndirectDrawArgs));
        m_pcb_pass_cpu.is_transparent = 0;

        // opaque half, reads the opaque depth the prepass wrote, clears the g-buffer targets
        // the clear runs unconditionally so the transparent ocean composites over a fresh g-buffer when no opaque geometry is visible
        cmd_list->SetPipelineState(pso);

        if (m_indirect_draw_count > 0)
        {
            cmd_list->SetBuffer(Renderer_BindingsUav::indirect_draw_data, GetBuffer(Renderer_Buffer::IndirectDrawData));
            cmd_list->SetBuffer(Renderer_BindingsUav::meshlet_instances,  GetBuffer(Renderer_Buffer::MeshletInstances));
            cmd_list->SetBuffer(Renderer_BindingsUav::visible_triangles,  GetBuffer(Renderer_Buffer::VisibleTriangles));
            cmd_list->SetBuffer(Renderer_BindingsUav::meshlet_bounds,     GeometryBuffer::GetMeshletBoundsBuffer());
            cmd_list->SetCullMode(RHI_CullMode::None);
            m_pcb_pass_cpu.set_f4_value(0.0f, 0.0f, 0.0f, 0.0f);
            cmd_list->PushConstants(m_pcb_pass_cpu);
            cmd_list->DrawIndirect(GetBuffer(Renderer_Buffer::IndirectDrawArgs), 0);

            // alpha-tested half, same equal-z pixel shader, loads the g-buffer so the opaque output survives
            pso.clear_color[0] = rhi_color_load;
            pso.clear_color[1] = rhi_color_load;
            pso.clear_color[2] = rhi_color_load;
            pso.clear_color[3] = rhi_color_load;
            cmd_list->SetPipelineState(pso);
            cmd_list->SetBuffer(Renderer_BindingsUav::indirect_draw_data, GetBuffer(Renderer_Buffer::IndirectDrawData));
            cmd_list->SetBuffer(Renderer_BindingsUav::meshlet_instances,  GetBuffer(Renderer_Buffer::MeshletInstances));
            cmd_list->SetBuffer(Renderer_BindingsUav::visible_triangles,  GetBuffer(Renderer_Buffer::VisibleTriangles));
            cmd_list->SetBuffer(Renderer_BindingsUav::meshlet_bounds,     GeometryBuffer::GetMeshletBoundsBuffer());
            cmd_list->SetCullMode(RHI_CullMode::None);
            m_pcb_pass_cpu.set_f4_value(static_cast<float>(GetBuffer(Renderer_Buffer::VisibleTriangles)->GetElementCount() / 2), 0.0f, 0.0f, 0.0f);
            cmd_list->PushConstants(m_pcb_pass_cpu);
            cmd_list->DrawIndirect(GetBuffer(Renderer_Buffer::IndirectDrawArgs), arg_stride);
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
        // transparent draws own their depth so they keep ReadWrite, opaque/tessellated leans on the depth prepass written through the
        // tessellation_h/d pair so equal-z matches whatever the prepass produced, same alpha-test correctness argument as the indirect path
        pso.depth_stencil_state              = is_transparent_pass ? GetDepthStencilState(Renderer_DepthStencilState::ReadWrite) : GetDepthStencilState(Renderer_DepthStencilState::ReadEqual);
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

            m_pcb_pass_cpu.draw_index     = draw_call.draw_data_index;
            m_pcb_pass_cpu.is_transparent = is_transparent_pass ? 1 : 0;
            m_pcb_pass_cpu.material_index = material->GetIndex();
            cmd_list->PushConstants(m_pcb_pass_cpu);

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
                // procedural grass runs after the indirect path, the draw call binds its own pipeline that reads grass_instances directly
                Pass_Grass_Draw(cmd_list);
            }

            Pass_GBuffer_TessellatedAndTransparent(cmd_list, is_transparent_pass);

            if (!is_transparent_pass)
            {
                // opaque depth blit moved here from the prepass, all opaque geometry including grass has rasterized so the
                // opaque output carries grass occlusion, batch b consumers run after phase 1 so this write is visible to them
                RHI_Texture* tex_depth        = GetRenderTarget(Renderer_RenderTarget::gbuffer_depth);
                RHI_Texture* tex_depth_output = GetRenderTarget(Renderer_RenderTarget::gbuffer_depth_opaque_output);
                cmd_list->Blit(tex_depth, tex_depth_output, false, Renderer::GetResolutionScale());

                // single source of truth for motion vector previous transforms, the indirect, tessellated and transparent
                // paths all read transform_previous from the draw data snapshot taken in UpdateDrawCalls so updating here once
                // per frame (not per pass or per eye) is correct and replaces the duplicated per-draw updates
                for (uint32_t i = 0; i < m_draw_call_count; i++)
                {
                    Entity* entity = m_draw_calls[i].renderable->GetEntity();
                    entity->SetMatrixPrevious(entity->GetMatrix());
                }
            }

        }
        cmd_list->EndTimeblock();
    }

    void Renderer::Pass_Grass_Populate(RHI_CommandList* cmd_list)
    {
        // gpu procedural grass placement (ghost of tsushima style)
        // runs every frame just before the geometry rasters, fills the per-lod sections of
        // grass_instances with newly placed blades around the camera, and then bakes the
        // dynamic instance_count into grass_indirect_args so the raster passes can draw indirectly.

        if (!m_pass_state.grass_enabled || !m_pass_state.grass_mesh || !m_pass_state.grass_heightmap)
            return;

        cmd_list->BeginTimeblock("grass_populate");
        {
            RHI_Buffer* buf_instances = GetBuffer(Renderer_Buffer::GrassInstances);
            RHI_Buffer* buf_count     = GetBuffer(Renderer_Buffer::GrassCount);
            RHI_Buffer* buf_args      = GetBuffer(Renderer_Buffer::GrassIndirectArgs);

            // bake static portions of the per-lod indirect args buffer once (or whenever the mesh layout changes)
            // these never change frame to frame so a single Update covers the lifetime of EnableProceduralGrass
            if (!m_pass_state.grass_args_baked)
            {
                cmd_list->UpdateBuffer(buf_args, 0, static_cast<uint32_t>(sizeof(Sb_IndirectDrawArgs) * renderer_max_grass_lod_count), &m_pass_state.grass_indirect_args_static[0], false);
                m_pass_state.grass_args_baked = true;
            }

            // clear the per lod counters on the gpu timeline, a mapped cpu memcpy races the in flight previous frame and drops its grass for a frame
            uint32_t zero_counts[renderer_max_grass_lod_count] = { 0u, 0u, 0u };
            cmd_list->UpdateBuffer(buf_count, 0, sizeof(zero_counts), &zero_counts[0], false);

            // camera position used as the anchor for the ring grid, the populate shader snaps it to the cell grid
            Camera* camera = World::GetCamera();
            if (!camera || !camera->GetEntity())
            {
                cmd_list->EndTimeblock();
                return;
            }
            const Vector3 camera_pos = camera->GetEntity()->GetPosition();

            // populate dispatches, one per lod ring, each fills its slot in grass_instances and grass_count
            {
                RHI_PipelineState pso;
                pso.name             = "grass_populate";
                pso.shaders[Compute] = GetShader(Renderer_Shader::grass_populate_c);
                cmd_list->SetPipelineState(pso);

                cmd_list->SetBuffer(Renderer_BindingsUav::grass_instances, buf_instances);
                cmd_list->SetBuffer(Renderer_BindingsUav::grass_count,     buf_count);
                // the populate shader samples the terrain heightmap (R32_Float) through the tex slot
                cmd_list->SetTexture(Renderer_BindingsSrv::tex, m_pass_state.grass_heightmap);
                // occluder hi-z on tex2 drives the per-blade frustum + occlusion cull, built by Pass_HiZ which runs earlier this frame
                cmd_list->SetTexture(Renderer_BindingsSrv::tex2, GetRenderTarget(Renderer_RenderTarget::gbuffer_depth_occluders_hiz));

                const float max_slope_cos = cosf(m_pass_state.grass_params.max_slope_deg * (math::pi / 180.0f));

                for (uint32_t lod = 0; lod < renderer_max_grass_lod_count; lod++)
                {
                    const float cell_size  = m_pass_state.grass_params.cell_size_m[lod];
                    const float ring_radius = m_pass_state.grass_params.ring_radii_m[lod];
                    if (cell_size <= 0.0f || ring_radius <= 0.0f)
                        continue;

                    // grass_instances is partitioned by lod, lod_base is the cumulative prefix sum
                    // of the per-lod caps so each ring writes into its own contiguous slot
                    const uint32_t lod_base   = renderer_grass_lod_base(lod);
                    const uint32_t lod_cap    = renderer_max_grass_per_lod[lod];

                    // pack push constant payload, layout mirrors grass_populate.hlsl values[0..2]
                    // values[0] = (cell_size, ring_radius, lod_base, max_instances_per_lod)
                    // values[1] = (height_min, height_max, max_slope_cos, lod_index)
                    // values[2] = (camera_xz.x, camera_xz.z, terrain_extent.x, terrain_extent.z)
                    m_pcb_pass_cpu.material_index = 0;
                    m_pcb_pass_cpu.is_transparent = 0;
                    m_pcb_pass_cpu.draw_index     = 0;
                    m_pcb_pass_cpu.v[0]  = cell_size;
                    m_pcb_pass_cpu.v[1]  = ring_radius;
                    m_pcb_pass_cpu.v[2]  = static_cast<float>(lod_base);
                    m_pcb_pass_cpu.v[3]  = static_cast<float>(lod_cap);
                    m_pcb_pass_cpu.v[4]  = m_pass_state.grass_params.height_min;
                    m_pcb_pass_cpu.v[5]  = m_pass_state.grass_params.height_max;
                    m_pcb_pass_cpu.v[6]  = max_slope_cos;
                    m_pcb_pass_cpu.v[7]  = static_cast<float>(lod);
                    m_pcb_pass_cpu.v[8]  = camera_pos.x;
                    m_pcb_pass_cpu.v[9]  = camera_pos.z;
                    m_pcb_pass_cpu.v[10] = m_pass_state.grass_params.terrain_extent_m.x;
                    m_pcb_pass_cpu.v[11] = m_pass_state.grass_params.terrain_extent_m.y;
                    cmd_list->PushConstants(m_pcb_pass_cpu);

                    // square grid of cells, 8x8 thread groups, one cell per thread, the shader culls out-of-ring cells
                    // dispatch z dimension carries the per-cell blade index, blades_per_cell is derived from the ring area
                    // and the per-lod cap so the total writes stay below the cap, the shader recomputes the exact same value
                    // for its in-bounds check on dispatch_thread_id.z, both formulas must stay in lockstep
                    const uint32_t cells_per_axis  = static_cast<uint32_t>(ceilf(2.0f * ring_radius / cell_size));
                    const uint32_t groups          = (cells_per_axis + 7u) / 8u;
                    const float    ring_area       = math::pi * ring_radius * ring_radius;
                    const float    cells_in_ring   = ring_area / (cell_size * cell_size);
                    const uint32_t blades_per_cell = std::max(1u, static_cast<uint32_t>(std::floor(static_cast<float>(lod_cap) / std::max(cells_in_ring, 1.0f))));
                    cmd_list->Dispatch(groups, groups, blades_per_cell);
                }
            }

            // args build, reads grass_count and writes grass_indirect_args[lod].instance_count
            {
                RHI_PipelineState pso;
                pso.name             = "grass_indirect_args";
                pso.shaders[Compute] = GetShader(Renderer_Shader::grass_indirect_args_c);
                cmd_list->SetPipelineState(pso);

                cmd_list->SetBuffer(Renderer_BindingsUav::grass_count,         buf_count);
                cmd_list->SetBuffer(Renderer_BindingsUav::grass_indirect_args, buf_args);

                // values[0] = (cap_lod0, cap_lod1, cap_lod2, lod_count), one float per lod cap
                // grass_indirect_args_c reads the matching slot for its own lod_index and clamps the
                // atomic counter against it before baking the instance_count into the indirect args
                static_assert(renderer_max_grass_lod_count == 3, "grass_indirect_args push constant layout assumes 3 lods");
                m_pcb_pass_cpu.material_index = 0;
                m_pcb_pass_cpu.v[0] = static_cast<float>(renderer_max_grass_per_lod[0]);
                m_pcb_pass_cpu.v[1] = static_cast<float>(renderer_max_grass_per_lod[1]);
                m_pcb_pass_cpu.v[2] = static_cast<float>(renderer_max_grass_per_lod[2]);
                m_pcb_pass_cpu.v[3] = static_cast<float>(renderer_max_grass_lod_count);
                cmd_list->PushConstants(m_pcb_pass_cpu);
                cmd_list->Dispatch(1, 1, 1);
            }
        }
        cmd_list->EndTimeblock();
    }

    void Renderer::Pass_Grass_Draw(RHI_CommandList* cmd_list)
    {
        // procedural grass raster, one DrawIndexedIndirect per lod ring, runs once inside the g-buffer pass
        // shares the geometry stage render pass, sets its own vertex shader that reads grass_instances

        if (!m_pass_state.grass_enabled || !m_pass_state.grass_mesh || !m_pass_state.grass_material)
            return;

        if (!m_pass_state.grass_args_baked)
            return;

        Mesh*     mesh     = m_pass_state.grass_mesh;
        Material* material = m_pass_state.grass_material;

        // wait for the global geometry buffer to be built, the indirect args reference it for vertex and index data
        // also wait for the material to be registered in the bindless table, until then the index is zero and the
        // pixel shader would read another renderable's textures producing garbage colour for the grass blade
        if (!mesh->GetVertexBuffer() || !mesh->GetIndexBuffer() || !GeometryBuffer::GetInstanceBuffer())
        {
            return;
        }

        // pso, matches the rest of the geometry stage so the draw lands in the same render pass
        const bool xr_multiview = Xr::IsSessionRunning() && Xr::GetStereoMode();

        RHI_PipelineState pso;
        pso.name                             = "grass_gbuffer";
        pso.shaders[RHI_Shader_Type::Vertex] = GetShader(Renderer_Shader::grass_gbuffer_v);
        pso.shaders[RHI_Shader_Type::Pixel]  = GetShader(Renderer_Shader::gbuffer_p);
        pso.blend_state                      = GetBlendState(Renderer_BlendState::Off);
        pso.rasterizer_state                 = cvar_wireframe.GetValueAs<bool>() ? GetRasterizerState(Renderer_RasterizerState::Wireframe) : GetRasterizerState(Renderer_RasterizerState::Solid);
        pso.is_multiview                     = xr_multiview;
        pso.resolution_scale                 = true;
        // grass is the only geometry rasterized a single time, it owns its depth here so it writes and tests against the opaque geometry
        pso.depth_stencil_state              = GetDepthStencilState(Renderer_DepthStencilState::ReadWrite);
        pso.render_target_color_textures[0]  = GetRenderTarget(Renderer_RenderTarget::gbuffer_color);
        pso.render_target_color_textures[1]  = GetRenderTarget(Renderer_RenderTarget::gbuffer_normal);
        pso.render_target_color_textures[2]  = GetRenderTarget(Renderer_RenderTarget::gbuffer_material);
        pso.render_target_color_textures[3]  = GetRenderTarget(Renderer_RenderTarget::gbuffer_velocity);
        pso.render_target_depth_texture      = GetRenderTarget(Renderer_RenderTarget::gbuffer_depth);
        pso.clear_color[0]                   = rhi_color_load;
        pso.clear_color[1]                   = rhi_color_load;
        pso.clear_color[2]                   = rhi_color_load;
        pso.clear_color[3]                   = rhi_color_load;
        pso.clear_depth                      = rhi_depth_load;

        cmd_list->SetPipelineState(pso);

        // grass blades are double sided, the material flags carry this but the raster needs an explicit setting
        cmd_list->SetCullMode(RHI_CullMode::None);

        // grass_instances is uav-bound here, the vertex shader reads GrassInstance via the same descriptor
        // the per-instance vertex stream is bound to the global geometry instance buffer just like every other
        // geometry pass, the grass vs never reads those attributes so any in-range buffer is fine, sharing the
        // same binding keeps the engine wide vertex layout consistent and avoids any cross talk with subsequent passes
        RHI_Buffer* buf_instances     = GetBuffer(Renderer_Buffer::GrassInstances);
        RHI_Buffer* buf_args          = GetBuffer(Renderer_Buffer::GrassIndirectArgs);
        RHI_Buffer* binding1_instance = GeometryBuffer::GetInstanceBuffer() ? GeometryBuffer::GetInstanceBuffer() : GetBuffer(Renderer_Buffer::DummyInstance);
        cmd_list->SetBuffer(Renderer_BindingsUav::grass_instances, buf_instances);
        cmd_list->SetBufferVertex(mesh->GetVertexBuffer(), binding1_instance);
        cmd_list->SetBufferIndex(mesh->GetIndexBuffer());

        const uint32_t arg_stride = static_cast<uint32_t>(sizeof(Sb_IndirectDrawArgs));

        for (uint32_t lod = 0; lod < renderer_max_grass_lod_count; lod++)
        {
            const uint32_t lod_base = renderer_grass_lod_base(lod);

            // values[0] = (0, 0, lod_base, lod_index), the grass vs reads lod_base from values[0].z
            m_pcb_pass_cpu.draw_index     = 0;
            m_pcb_pass_cpu.is_transparent = 0;
            m_pcb_pass_cpu.material_index = material->GetIndex();
            m_pcb_pass_cpu.v[0] = 0.0f;
            m_pcb_pass_cpu.v[1] = 0.0f;
            m_pcb_pass_cpu.v[2] = static_cast<float>(lod_base);
            m_pcb_pass_cpu.v[3] = static_cast<float>(lod);
            cmd_list->PushConstants(m_pcb_pass_cpu);

            // an empty ring bakes instance_count 0 into the args so the gpu skips it at near-zero cost
            cmd_list->DrawIndexedIndirect(buf_args, lod * arg_stride);
        }
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
            // f4.x carries the visible-triangle region base, draw the opaque half then the alpha half
            const uint32_t arg_stride = static_cast<uint32_t>(sizeof(Sb_IndirectDrawArgs));
            m_pcb_pass_cpu.set_f3_value(color_by_draw_id ? 1.0f : 0.0f, 0.0f, 0.0f);

            m_pcb_pass_cpu.set_f4_value(0.0f, 0.0f, 0.0f, 0.0f);
            cmd_list->PushConstants(m_pcb_pass_cpu);
            cmd_list->DrawIndirect(GetBuffer(Renderer_Buffer::IndirectDrawArgs), 0);

            m_pcb_pass_cpu.set_f4_value(static_cast<float>(GetBuffer(Renderer_Buffer::VisibleTriangles)->GetElementCount() / 2), 0.0f, 0.0f, 0.0f);
            cmd_list->PushConstants(m_pcb_pass_cpu);
            cmd_list->DrawIndirect(GetBuffer(Renderer_Buffer::IndirectDrawArgs), arg_stride);
        }
        cmd_list->EndTimeblock();
    }
}
