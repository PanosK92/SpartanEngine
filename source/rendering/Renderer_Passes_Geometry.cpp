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
#include "Renderer_Internal.h"
#include "../world/Entity.h"
#include "../world/components/Light.h"
#include "../world/components/Camera.h"
#include "../world/components/Terrain.h"
#include "../world/World.h"
#include "../resource/IResource.h"
#include "../rhi/RHI_CommandList.h"
#include "../rhi/RHI_Buffer.h"
#include "../rhi/RHI_AccelerationStructure.h"
#include "../rhi/RHI_RasterizerState.h"
#include "../rhi/RHI_DepthStencilState.h"
#include "../rhi/RHI_Device.h"
#include "../rhi/RHI_Shader.h"
#include "../rhi/RHI_Texture.h"
#include "../rendering/Material.h"
#include "../rendering/GeometryBuffer.h"
#include "../xr/Xr.h"
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

        bool use_mesh_shaders()
        {
            if (!cvar_mesh_shaders.GetValueAs<bool>() || !RHI_Device::IsSupportedMeshShaders())
            {
                return false;
            }

            RHI_Shader* mesh_shader        = Renderer::GetShader(Renderer_Shader::meshlet_mesh_m);
            RHI_Shader* mesh_alpha_shader  = Renderer::GetShader(Renderer_Shader::meshlet_mesh_alpha_m);
            RHI_Shader* depth_shader       = Renderer::GetShader(Renderer_Shader::meshlet_mesh_depth_m);
            RHI_Shader* depth_alpha_shader = Renderer::GetShader(Renderer_Shader::meshlet_mesh_depth_alpha_m);
            return mesh_shader && mesh_shader->IsCompiled() &&
                   mesh_alpha_shader && mesh_alpha_shader->IsCompiled() &&
                   depth_shader && depth_shader->IsCompiled() &&
                   depth_alpha_shader && depth_alpha_shader->IsCompiled();
        }

        void log_mesh_path_diagnostics()
        {
            static bool was_enabled = false;

            const bool enabled = use_mesh_shaders();
            if (enabled != was_enabled)
            {
                was_enabled = enabled;

                RHI_Shader* mesh_shader       = Renderer::GetShader(Renderer_Shader::meshlet_mesh_m);
                RHI_Shader* depth_shader      = Renderer::GetShader(Renderer_Shader::meshlet_mesh_depth_m);
                RHI_Shader* depth_alpha_shader = Renderer::GetShader(Renderer_Shader::meshlet_mesh_depth_alpha_m);

                SP_LOG_INFO(
                    "mesh shaders %s (device=%s, mesh_m=%s, depth_m=%s, depth_alpha_m=%s, meshlet_verts=%s, meshlet_micros=%s, meshlet_bounds=%s)",
                    enabled ? "ON" : "OFF",
                    RHI_Device::IsSupportedMeshShaders() ? "yes" : "no",
                    (mesh_shader && mesh_shader->IsCompiled()) ? "ok" : "fail",
                    (depth_shader && depth_shader->IsCompiled()) ? "ok" : "fail",
                    (depth_alpha_shader && depth_alpha_shader->IsCompiled()) ? "ok" : "fail",
                    GeometryBuffer::GetMeshletVertexBuffer() ? "ok" : "null",
                    GeometryBuffer::GetMeshletMicroIndexBuffer() ? "ok" : "null",
                    GeometryBuffer::GetMeshletBoundsBuffer() ? "ok" : "null"
                );
            }
        }

        // same formula as Terrain::SampleHeight, world_y = local_y + matrix translation
        // GetPosition is 0 until UpdateTransform, which is also when the tiles drop, so the
        // offset cannot fire on its own and put grass under the mesh
        void get_grass_terrain_frame_state(Vector3& offset_out, Vector4& mapping_out)
        {
            offset_out  = Vector3::Zero;
            mapping_out = Vector4::Zero;

            Terrain* terrain = Terrain::FindActive();
            if (!terrain)
            {
                return;
            }

            mapping_out = terrain->GetWorldMapping();
            Entity* entity = terrain->GetEntity();
            if (!entity)
            {
                return;
            }

            const Matrix& world = entity->GetMatrix();
            offset_out = world.GetTranslation();
            mapping_out.x += offset_out.x;
            mapping_out.y += offset_out.z;
        }

        // a slot only dispatches once its mesh is in the geometry buffer and the height map it samples
        // has reached the gpu, otherwise the populate compute reads a freed or half uploaded resource
        bool gpu_scatter_ready(const Renderer::PassState::GpuScatterSlot& slot)
        {
            return slot.enabled                &&
                   slot.mesh                   &&
                   slot.heightmap              &&
                   slot.heightmap->GetRhiResource() &&
                   slot.heightmap->GetResourceState() == ResourceState::PreparedForGpu;
        }

        // the populate dispatch stops writing at this many instances, so the args builder has to clamp
        // the atomic counter against the same number or the raster reads instances nobody wrote
        uint32_t gpu_scatter_lod_cap(const uint32_t slot, const uint32_t lod, const float density)
        {
            const float scaled = static_cast<float>(renderer_gpu_scatter_cap(slot, lod)) *
                                 clamp(density, 0.01f, 1.0f);

            return max(1u, static_cast<uint32_t>(floorf(scaled)));
        }

        // compose_instance_transform unpacks the instance scale logarithmically over 0.01 to 100, so a
        // world scale has to travel as its position inside that curve
        uint32_t pack_instance_scale(const float scale)
        {
            const float scale_min_log2 = -6.643856f;
            const float scale_max_log2 =  6.643856f;

            const float normalized = (log2f(clamp(scale, 0.01f, 100.0f)) - scale_min_log2) /
                                     (scale_max_log2 - scale_min_log2);

            return static_cast<uint32_t>(clamp(normalized, 0.0f, 1.0f) * 255.0f + 0.5f);
        }

        void bind_mesh_shader_geometry()
        {
            RHI_CommandList::SetBuffer(static_cast<uint32_t>(Renderer_BindingsUav::indirect_draw_data), Renderer::GetBuffer(Renderer_Buffer::IndirectDrawData));
            RHI_CommandList::SetBuffer(static_cast<uint32_t>(Renderer_BindingsUav::meshlet_instances), Renderer::GetBuffer(Renderer_Buffer::MeshletInstances));
            RHI_CommandList::SetBuffer(static_cast<uint32_t>(Renderer_BindingsUav::meshlet_bounds), GeometryBuffer::GetMeshletBoundsBuffer());
            RHI_CommandList::SetBuffer(static_cast<uint32_t>(Renderer_BindingsUav::meshlet_vertices), GeometryBuffer::GetMeshletVertexBuffer());
            RHI_CommandList::SetBuffer(static_cast<uint32_t>(Renderer_BindingsUav::meshlet_micro_indices), GeometryBuffer::GetMeshletMicroIndexBuffer());
        }

        void push_mesh_draw_constants(Pcb_Pass& pcb)
        {
            // the opaque/alpha split is a shader variant now, f4 is unused by the mesh path
            // the push still runs because the alpha pixel shaders read the other pass fields
            pcb.set_f4_value(0.0f, 0.0f, 0.0f, 0.0f);
            RHI_CommandList::PushConstants(pcb);
        }
    }

    void Renderer::Pass_ShadowMaps()
    {
        if (World::GetLightCount() == 0)
        {
            return;
        }

        // shadow atlas is unused when full ray traced shadows own visibility, a secondary view
        // never traces so it needs the atlas
        const bool tlas_available =
            RHI_Device::IsSupportedRayTracing() &&
            GetTopLevelAccelerationStructure() != nullptr &&
            !IsSecondaryViewActive();
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
                    Render* render                     = draw_call.render;
                    if (!render->HasFlag(RenderFlags::CastsShadows))
                    {
                        continue;
                    }

                    const float shadow_distance = render->GetMaxShadowDistance();
                    if (draw_call.distance_squared > shadow_distance * shadow_distance)
                    {
                        continue;
                    }

                    Material* material = render->GetMaterial();
                    if (!material || material->IsTransparent())
                    {
                        continue;
                    }

                    if (!light->IsInViewFrustum(render, array_index))
                    {
                        continue;
                    }

                    slice.visible_draws.push_back(&draw_call);
                    RHI_Buffer* vertex_buffer = render->GetVertexBuffer();
                    RHI_Buffer* index_buffer  = render->GetIndexBuffer();
                    const bool alpha_tested   = array_index == 0 && light->GetLightType() == LightType::Directional && material->IsAlphaTested();
                    has_alpha_draws          |= alpha_tested;
                    const bool can_batch      = draw_call.instance_count == 1 && render->GetGlobalInstanceOffset() == 0 && vertex_buffer && index_buffer;
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

                    const bool close_to_shadow      = render->GetDistanceSquared() < 100.0f * 100.0f;
                    const uint32_t lod_index_bias   = light->GetLightType() == LightType::Directional ? 1 : 0;
                    const uint32_t lod_index_shadow = clamp(render->GetLodIndex() + lod_index_bias, 0u, render->GetLodCount() - 1);
                    const uint32_t lod_index        = close_to_shadow ? draw_call.lod_index : lod_index_shadow;

                    Sb_IndirectDrawArgs& argument = batch.arguments.emplace_back();
                    argument.index_count          = render->GetIndexCount(lod_index);
                    argument.instance_count       = 1;
                    argument.first_index          = render->GetIndexOffset(lod_index);
                    argument.vertex_offset        = static_cast<int32_t>(render->GetVertexOffset(lod_index));
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
            RHI_CommandList::UpdateBuffer(argument_buffer, m_cpu_indirect_draw_arg_count * sizeof(Sb_IndirectDrawArgs), arguments.size() * sizeof(Sb_IndirectDrawArgs), arguments.data());
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

        RHI_CommandList::BeginTimeblock(pso.name);
        {
            RHI_CommandList::SetPipelineState(pso);

            for (ShadowSlice& slice : slices)
            {
                Light* light = slice.light;
                RHI_RasterizerState* rasterizer_state = light->GetLightType() == LightType::Directional ? GetRasterizerState(Renderer_RasterizerState::Light_directional) : GetRasterizerState(Renderer_RasterizerState::Light_point_spot);
                RHI_Viewport viewport;
                viewport.x      = slice.rect.x;
                viewport.y      = slice.rect.y;
                viewport.width  = slice.rect.width;
                viewport.height = slice.rect.height;
                RHI_CommandList::SetViewport(viewport);
                RHI_CommandList::SetScissorRectangle(slice.rect);

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
                            RHI_CommandList::SetPipelineState(pso);
                            RHI_CommandList::SetViewport(viewport);
                            RHI_CommandList::SetScissorRectangle(slice.rect);
                        }

                        m_pcb_pass_cpu.draw_index     = numeric_limits<uint32_t>::max();
                        m_pcb_pass_cpu.is_transparent = 0;
                        m_pcb_pass_cpu.set_f3_value2(static_cast<float>(light->GetIndex()), static_cast<float>(slice.array_index), 0.0f);
                        RHI_CommandList::PushConstants(m_pcb_pass_cpu);
                        RHI_CommandList::SetCullMode(batch.cull_mode);
                        RHI_CommandList::SetBufferVertex(batch.vertex_buffer);
                        RHI_CommandList::SetBufferIndex(batch.index_buffer);
                        RHI_CommandList::DrawIndexedIndirect(argument_buffer, batch.argument_offset, static_cast<uint32_t>(batch.arguments.size()));
                    }
                }

                auto draw_direct = [&](const Renderer_DrawCall& draw_call)
                {
                    Render* render = draw_call.render;
                    Material* material = render->GetMaterial();
                    const bool is_first_cascade = slice.array_index == 0 && light->GetLightType() == LightType::Directional;
                    RHI_Shader* vertex_shader   = use_batches ? multi_vertex_shader : GetShader(Renderer_Shader::depth_light_v);
                    RHI_Shader* pixel_shader    = is_first_cascade && material->IsAlphaTested() ? (use_batches ? multi_pixel_shader : GetShader(Renderer_Shader::depth_light_alpha_color_p)) : nullptr;
                    if (pso.shaders[RHI_Shader_Type::Vertex] != vertex_shader || pso.shaders[RHI_Shader_Type::Pixel] != pixel_shader || pso.rasterizer_state != rasterizer_state)
                    {
                        pso.shaders[RHI_Shader_Type::Vertex] = vertex_shader;
                        pso.shaders[RHI_Shader_Type::Pixel]  = pixel_shader;
                        pso.rasterizer_state                 = rasterizer_state;
                        RHI_CommandList::SetPipelineState(pso);
                        RHI_CommandList::SetViewport(viewport);
                        RHI_CommandList::SetScissorRectangle(slice.rect);
                    }

                    m_pcb_pass_cpu.draw_index     = draw_call.draw_data_index;
                    m_pcb_pass_cpu.is_transparent = 0;
                    m_pcb_pass_cpu.material_index = material->GetIndex();
                    m_pcb_pass_cpu.set_f3_value(material->HasTextureOfType(MaterialTextureType::Color) ? 1.0f : 0.0f);
                    m_pcb_pass_cpu.set_f3_value2(static_cast<float>(light->GetIndex()), static_cast<float>(slice.array_index), 0.0f);
                    RHI_CommandList::PushConstants(m_pcb_pass_cpu);
                    RHI_CommandList::SetCullMode(static_cast<RHI_CullMode>(material->GetProperty(MaterialProperty::CullMode)));
                    RHI_Buffer* instance_buffer = GeometryBuffer::GetInstanceBuffer() ? GeometryBuffer::GetInstanceBuffer() : GetBuffer(Renderer_Buffer::DummyInstance);
                    RHI_CommandList::SetBufferVertex(render->GetVertexBuffer(), instance_buffer);
                    RHI_CommandList::SetBufferIndex(render->GetIndexBuffer());

                    const bool close_to_shadow      = render->GetDistanceSquared() < 100.0f * 100.0f;
                    const uint32_t lod_index_bias   = light->GetLightType() == LightType::Directional ? 1 : 0;
                    const uint32_t lod_index_shadow = clamp(render->GetLodIndex() + lod_index_bias, 0u, render->GetLodCount() - 1);
                    const uint32_t lod_index        = close_to_shadow ? draw_call.lod_index : lod_index_shadow;
                    RHI_CommandList::DrawIndexed(render->GetIndexCount(lod_index), render->GetIndexOffset(lod_index), render->GetVertexOffset(lod_index), render->GetGlobalInstanceOffset() + draw_call.instance_index, draw_call.instance_count);
                };

                const vector<const Renderer_DrawCall*>& draws = use_batches ? slice.direct_draws : slice.visible_draws;
                for (const Renderer_DrawCall* draw_call : draws)
                {
                    draw_direct(*draw_call);
                }
            }
        }
        RHI_CommandList::EndTimeblock();
    }

    void Renderer::Pass_HiZ()
    {
        // renders major occluders and builds the hi-z chain, always cleared and rebuilt so the cull shader never reads stale depth

        RHI_CommandList::BeginTimeblock("hiz");

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
                Render* render            = draw_call.render;
                RHI_Buffer* vertex_buffer = render->GetVertexBuffer();
                RHI_Buffer* index_buffer  = render->GetIndexBuffer();
                const bool can_batch      = draw_call.instance_count == 1 && render->GetGlobalInstanceOffset() == 0 && vertex_buffer && index_buffer;
                if (!can_batch)
                {
                    direct_draws.push_back(&draw_call);
                    continue;
                }

                RHI_CullMode cull_mode = static_cast<RHI_CullMode>(render->GetMaterial()->GetProperty(MaterialProperty::CullMode));
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
                argument.index_count          = render->GetIndexCount(draw_call.lod_index);
                argument.instance_count       = 1;
                argument.first_index          = render->GetIndexOffset(draw_call.lod_index);
                argument.vertex_offset        = static_cast<int32_t>(render->GetVertexOffset(draw_call.lod_index));
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
            RHI_CommandList::UpdateBuffer(argument_buffer, m_cpu_indirect_draw_arg_count * sizeof(Sb_IndirectDrawArgs), arguments.size() * sizeof(Sb_IndirectDrawArgs), arguments.data());
            m_cpu_indirect_draw_arg_count += argument_count;
        }

        // always start the pass so the depth clears, stale depth would propagate into the hi-z chain
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

            RHI_CommandList::SetPipelineState(pso);

            if (render_occluders)
            {
                if (use_batches)
                {
                    m_pcb_pass_cpu.draw_index = numeric_limits<uint32_t>::max();
                    RHI_CommandList::PushConstants(m_pcb_pass_cpu);
                    for (HiZBatch& batch : batches)
                    {
                        RHI_CommandList::SetCullMode(batch.cull_mode);
                        RHI_CommandList::SetBufferVertex(batch.vertex_buffer);
                        RHI_CommandList::SetBufferIndex(batch.index_buffer);
                        RHI_CommandList::DrawIndexedIndirect(argument_buffer, batch.argument_offset, static_cast<uint32_t>(batch.arguments.size()));
                    }
                }

                auto draw_direct = [&](const Renderer_DrawCall& draw_call)
                {
                    Render* render = draw_call.render;
                    RHI_CullMode cull_mode = static_cast<RHI_CullMode>(render->GetMaterial()->GetProperty(MaterialProperty::CullMode));
                    cull_mode              = (pso.rasterizer_state->GetPolygonMode() == RHI_PolygonMode::Wireframe) ? RHI_CullMode::None : cull_mode;
                    RHI_CommandList::SetCullMode(cull_mode);

                    m_pcb_pass_cpu.draw_index = draw_call.draw_data_index;
                    RHI_CommandList::PushConstants(m_pcb_pass_cpu);

                    RHI_CommandList::SetBufferVertex(render->GetVertexBuffer());
                    RHI_CommandList::SetBufferIndex(render->GetIndexBuffer());

                    RHI_CommandList::DrawIndexed(render->GetIndexCount(draw_call.lod_index), render->GetIndexOffset(draw_call.lod_index), render->GetVertexOffset(draw_call.lod_index));
                };

                const vector<const Renderer_DrawCall*>& draws = use_batches ? direct_draws : visible_draws;
                for (const Renderer_DrawCall* draw_call : draws)
                {
                    draw_direct(*draw_call);
                }
            }
        }

        // hi-z mip chain (min depth downsample, reverse z)
        Pass_Blit(tex_occluders, tex_occluders_hiz);
        Pass_Downscale(tex_occluders_hiz, Renderer_DownsampleFilter::Min);

        RHI_CommandList::EndTimeblock();
    }

    void Renderer::Pass_IndirectCull()
    {
        if (m_indirect_draw_count == 0 || m_cull_task_count == 0)
        {
            return;
        }

        RHI_Texture* tex_occluders_hiz = GetRenderTarget(Renderer_RenderTarget::gbuffer_depth_occluders_hiz);
        const float  max_hiz_mip       = static_cast<float>(tex_occluders_hiz->GetMipCount() - 1);

        // phase a, per-instance distance + side-frustum + hi-z, survivors land in surviving_instances and bump instance_dispatch_args.group_count_x
        RHI_CommandList::BeginPass("instance_cull");
        {
            RHI_CommandList::SetShader(GetShader(Renderer_Shader::instance_cull_c));

            RHI_CommandList::SetTexture(static_cast<uint32_t>(Renderer_BindingsSrv::tex), tex_occluders_hiz);

            RHI_CommandList::SetBuffer(static_cast<uint32_t>(Renderer_BindingsUav::indirect_draw_data), GetBuffer(Renderer_Buffer::IndirectDrawData));
            RHI_CommandList::SetBuffer(static_cast<uint32_t>(Renderer_BindingsUav::cull_tasks), GetBuffer(Renderer_Buffer::CullTasks));
            RHI_CommandList::SetBuffer(static_cast<uint32_t>(Renderer_BindingsUav::surviving_instances), GetBuffer(Renderer_Buffer::SurvivingInstances));
            RHI_CommandList::SetBuffer(static_cast<uint32_t>(Renderer_BindingsUav::instance_dispatch_args), GetBuffer(Renderer_Buffer::InstanceDispatchArgs));

            // f4_value: x = instance task count, y = max hiz mip, z = surviving instances cap (drop survivors past this)
            m_pcb_pass_cpu.set_f4_value(
                static_cast<float>(m_cull_task_count),
                max_hiz_mip,
                static_cast<float>(GetBuffer(Renderer_Buffer::SurvivingInstances)->GetElementCount()),
                0.0f);

            uint32_t thread_group_count = (m_cull_task_count + 255) / 256;
            RHI_CommandList::Dispatch(thread_group_count, 1, 1);
        }
        RHI_CommandList::EndPass();

        // phase b, expand the meshlets of the surviving instances, per-meshlet cone + frustum + hi-z, one workgroup per survivor
        RHI_CommandList::BeginPass("indirect_cull_meshlet");
        {
            RHI_CommandList::SetShader(GetShader(Renderer_Shader::indirect_cull_c));

            RHI_CommandList::SetTexture(static_cast<uint32_t>(Renderer_BindingsSrv::tex), tex_occluders_hiz);

            RHI_CommandList::SetBuffer(static_cast<uint32_t>(Renderer_BindingsUav::indirect_draw_data), GetBuffer(Renderer_Buffer::IndirectDrawData));
            RHI_CommandList::SetBuffer(static_cast<uint32_t>(Renderer_BindingsUav::meshlet_bounds), GeometryBuffer::GetMeshletBoundsBuffer());
            RHI_CommandList::SetBuffer(static_cast<uint32_t>(Renderer_BindingsUav::surviving_instances), GetBuffer(Renderer_Buffer::SurvivingInstances));
            RHI_CommandList::SetBuffer(static_cast<uint32_t>(Renderer_BindingsUav::meshlet_instances), GetBuffer(Renderer_Buffer::MeshletInstances));
            RHI_CommandList::SetBuffer(static_cast<uint32_t>(Renderer_BindingsUav::triangle_dispatch_args), GetBuffer(Renderer_Buffer::TriangleDispatchArgs));

            // f4_value: x = max hiz mip, y = meshlet instances cap, z = opaque/alpha region split
            m_pcb_pass_cpu.set_f4_value(
                max_hiz_mip,
                static_cast<float>(GetBuffer(Renderer_Buffer::MeshletInstances)->GetElementCount()),
                0.0f,
                0.0f);

            RHI_CommandList::DispatchIndirect(GetBuffer(Renderer_Buffer::InstanceDispatchArgs), 0);
        }
        RHI_CommandList::EndPass();

        // pass 2, per-triangle frustum + backface + sub-pixel cull, dispatched indirect with one workgroup per surviving meshlet
        // mesh shaders replace this stage, so skip it when they are the active draw path
        if (!use_mesh_shaders())
        {
            RHI_CommandList::BeginPass("indirect_cull_triangle");
            {
                RHI_CommandList::SetShader(GetShader(Renderer_Shader::indirect_cull_triangle_c));

                RHI_CommandList::SetBuffer(static_cast<uint32_t>(Renderer_BindingsUav::indirect_draw_args), GetBuffer(Renderer_Buffer::IndirectDrawArgs));
                RHI_CommandList::SetBuffer(static_cast<uint32_t>(Renderer_BindingsUav::indirect_draw_data), GetBuffer(Renderer_Buffer::IndirectDrawData));
                RHI_CommandList::SetBuffer(static_cast<uint32_t>(Renderer_BindingsUav::meshlet_bounds), GeometryBuffer::GetMeshletBoundsBuffer());
                RHI_CommandList::SetBuffer(static_cast<uint32_t>(Renderer_BindingsUav::meshlet_instances), GetBuffer(Renderer_Buffer::MeshletInstances));
                RHI_CommandList::SetBuffer(static_cast<uint32_t>(Renderer_BindingsUav::visible_triangles), GetBuffer(Renderer_Buffer::VisibleTriangles));

                // f4_value: x = meshlet instances cap, y = per-half visible triangle cap (also the alpha region base, drop survivors past it)
                m_pcb_pass_cpu.set_f4_value(
                    static_cast<float>(GetBuffer(Renderer_Buffer::MeshletInstances)->GetElementCount()),
                    static_cast<float>(GetBuffer(Renderer_Buffer::VisibleTriangles)->GetElementCount() / 2),
                    0.0f, 0.0f);

                RHI_CommandList::DispatchIndirect(GetBuffer(Renderer_Buffer::TriangleDispatchArgs), 0);
            }
            RHI_CommandList::EndPass();
        }
    }

    void Renderer::Pass_Depth_Prepass()
    {
        log_mesh_path_diagnostics();

        RHI_Texture* tex_depth = GetRenderTarget(Renderer_RenderTarget::gbuffer_depth);

        bool is_wireframe                     = cvar_wireframe.GetValueAs<bool>();
        bool xr_multiview                     = Xr::IsSessionRunning() && Xr::GetStereoMode();
        RHI_RasterizerState* rasterizer_state = GetRasterizerState(Renderer_RasterizerState::Solid);
        rasterizer_state                      = is_wireframe ? GetRasterizerState(Renderer_RasterizerState::Wireframe) : rasterizer_state;

        RHI_CommandList::BeginTimeblock("depth_prepass");
        {
            // two draws over the split survivor list, opaque with no pixel shader for double-speed depth, alpha with the cutout ps
            // mesh path: one workgroup per opaque/alpha meshlet via DrawMeshTasksIndirect, vs path: DrawIndirect over visible triangles
            {
                const bool mesh_path      = use_mesh_shaders();
                const uint32_t arg_stride = static_cast<uint32_t>(sizeof(Sb_IndirectDrawArgs));

                RHI_PipelineState pso;
                pso.name                             = mesh_path ? "depth_prepass_mesh" : "depth_prepass_indirect";
                if (mesh_path)
                {
                    pso.shaders[RHI_Shader_Type::MeshShader] = GetShader(Renderer_Shader::meshlet_mesh_depth_m);
                }
                else
                {
                    pso.shaders[RHI_Shader_Type::Vertex] = GetShader(Renderer_Shader::depth_prepass_indirect_v);
                }
                pso.rasterizer_state                 = rasterizer_state;
                pso.primitive_topology               =
                    GetSecondaryViewMode() ==
                    Renderer_SecondaryViewMode::Vertices
                        ? RHI_PrimitiveTopology::PointList
                        : RHI_PrimitiveTopology::TriangleList;
                pso.blend_state                      = GetBlendState(Renderer_BlendState::Off);
                pso.depth_stencil_state              = GetDepthStencilState(Renderer_DepthStencilState::ReadWrite);
                pso.vrs_input_texture                =
                    cvar_variable_rate_shading.GetValueAs<bool>() &&
                    !IsSecondaryViewActive()
                        ? GetRenderTarget(
                            Renderer_RenderTarget::shading_rate
                        )
                        : nullptr;
                pso.render_target_depth_texture      = tex_depth;
                pso.resolution_scale                 = true;
                pso.is_multiview                     = xr_multiview;

                // opaque half, no pixel shader, this pass also clears the depth target for the whole prepass
                // the clear runs unconditionally so the transparent ocean still tests against a fresh depth buffer when no opaque geometry is visible
                pso.shaders[RHI_Shader_Type::Pixel]  = nullptr;
                pso.clear_depth                      = 0.0f;
                RHI_CommandList::SetPipelineState(pso);

                if (m_indirect_draw_count > 0)
                {
                    RHI_CommandList::SetCullMode(RHI_CullMode::None);
                    if (mesh_path)
                    {
                        bind_mesh_shader_geometry();
                        push_mesh_draw_constants(m_pcb_pass_cpu);
                        RHI_CommandList::DrawMeshTasksIndirect(GetBuffer(Renderer_Buffer::TriangleDispatchArgs), 0);
                    }
                    else
                    {
                        RHI_CommandList::SetBuffer(static_cast<uint32_t>(Renderer_BindingsUav::indirect_draw_data), GetBuffer(Renderer_Buffer::IndirectDrawData));
                        RHI_CommandList::SetBuffer(static_cast<uint32_t>(Renderer_BindingsUav::meshlet_instances), GetBuffer(Renderer_Buffer::MeshletInstances));
                        RHI_CommandList::SetBuffer(static_cast<uint32_t>(Renderer_BindingsUav::visible_triangles), GetBuffer(Renderer_Buffer::VisibleTriangles));
                        RHI_CommandList::SetBuffer(static_cast<uint32_t>(Renderer_BindingsUav::meshlet_bounds), GeometryBuffer::GetMeshletBoundsBuffer());
                        m_pcb_pass_cpu.set_f4_value(0.0f, 0.0f, 0.0f, 0.0f);
                        RHI_CommandList::PushConstants(m_pcb_pass_cpu);
                        RHI_CommandList::DrawIndirect(GetBuffer(Renderer_Buffer::IndirectDrawArgs), 0);
                    }

                    // alpha-tested half, alpha-test pixel shader discards cutout texels, depth already cleared so load it
                    if (mesh_path)
                    {
                        pso.shaders[RHI_Shader_Type::MeshShader] = GetShader(Renderer_Shader::meshlet_mesh_depth_alpha_m);
                        pso.shaders[RHI_Shader_Type::Pixel]      = GetShader(Renderer_Shader::depth_prepass_mesh_alpha_p);
                    }
                    else
                    {
                        pso.shaders[RHI_Shader_Type::Pixel] = GetShader(Renderer_Shader::depth_prepass_indirect_alpha_test_p);
                    }
                    pso.clear_depth = rhi_depth_load;
                    RHI_CommandList::SetPipelineState(pso);
                    RHI_CommandList::SetCullMode(RHI_CullMode::None);
                    if (mesh_path)
                    {
                        bind_mesh_shader_geometry();
                        push_mesh_draw_constants(m_pcb_pass_cpu);
                        // same survivor list as opaque, mesh shader filters alpha via push constants
                        RHI_CommandList::DrawMeshTasksIndirect(GetBuffer(Renderer_Buffer::TriangleDispatchArgs), 0);
                    }
                    else
                    {
                        RHI_CommandList::SetBuffer(static_cast<uint32_t>(Renderer_BindingsUav::indirect_draw_data), GetBuffer(Renderer_Buffer::IndirectDrawData));
                        RHI_CommandList::SetBuffer(static_cast<uint32_t>(Renderer_BindingsUav::meshlet_instances), GetBuffer(Renderer_Buffer::MeshletInstances));
                        RHI_CommandList::SetBuffer(static_cast<uint32_t>(Renderer_BindingsUav::visible_triangles), GetBuffer(Renderer_Buffer::VisibleTriangles));
                        RHI_CommandList::SetBuffer(static_cast<uint32_t>(Renderer_BindingsUav::meshlet_bounds), GeometryBuffer::GetMeshletBoundsBuffer());
                        m_pcb_pass_cpu.set_f4_value(static_cast<float>(GetBuffer(Renderer_Buffer::VisibleTriangles)->GetElementCount() / 2), 0.0f, 0.0f, 0.0f);
                        RHI_CommandList::PushConstants(m_pcb_pass_cpu);
                        RHI_CommandList::DrawIndirect(GetBuffer(Renderer_Buffer::IndirectDrawArgs), arg_stride);
                    }
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
                pso.vrs_input_texture                =
                    cvar_variable_rate_shading.GetValueAs<bool>() &&
                    !IsSecondaryViewActive()
                        ? GetRenderTarget(
                            Renderer_RenderTarget::shading_rate
                        )
                        : nullptr;
                pso.render_target_depth_texture      = tex_depth;
                pso.resolution_scale                 = true;
                pso.is_multiview                     = xr_multiview;
                pso.clear_depth                      = rhi_depth_load;

                bool pipeline_set = false;

                for (uint32_t i = 0; i < m_draw_calls_prepass_count; i++)
                {
                    const Renderer_DrawCall& draw_call = m_draw_calls_prepass[i];
                    Render* render                     = draw_call.render;
                    Material* material                 = render->GetMaterial();
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
                        RHI_CommandList::SetPipelineState(pso);
                        pipeline_set = true;
                    }

                    bool has_color_texture        = material->HasTextureOfType(MaterialTextureType::Color);
                    m_pcb_pass_cpu.draw_index     = draw_call.draw_data_index;
                    m_pcb_pass_cpu.is_transparent = 0;
                    m_pcb_pass_cpu.material_index = material->GetIndex();
                    m_pcb_pass_cpu.set_f3_value(0.0f, has_color_texture ? 1.0f : 0.0f, static_cast<float>(i));
                    RHI_CommandList::PushConstants(m_pcb_pass_cpu);

                    RHI_CullMode cull_mode = static_cast<RHI_CullMode>(material->GetProperty(MaterialProperty::CullMode));
                    cull_mode              = (pso.rasterizer_state->GetPolygonMode() == RHI_PolygonMode::Wireframe) ? RHI_CullMode::None : cull_mode;
                    RHI_CommandList::SetCullMode(cull_mode);
                    RHI_Buffer* instance_buffer = GeometryBuffer::GetInstanceBuffer() ? GeometryBuffer::GetInstanceBuffer() : GetBuffer(Renderer_Buffer::DummyInstance);
                    RHI_CommandList::SetBufferVertex(render->GetVertexBuffer(), instance_buffer);
                    RHI_CommandList::SetBufferIndex(render->GetIndexBuffer());

                    RHI_CommandList::DrawIndexed(
                        render->GetIndexCount(draw_call.lod_index),
                        render->GetIndexOffset(draw_call.lod_index),
                        render->GetVertexOffset(draw_call.lod_index),
                        render->GetGlobalInstanceOffset() + draw_call.instance_index,
                        draw_call.instance_count
                    );
                }
            }

        }
        RHI_CommandList::EndTimeblock();
    }

    void Renderer::Pass_GBuffer_Indirect()
    {
        const bool xr_multiview = Xr::IsSessionRunning() && Xr::GetStereoMode();
        const bool mesh_path    = use_mesh_shaders();

        RHI_PipelineState pso;
        pso.name                             = mesh_path ? "g_buffer_mesh" : "g_buffer_indirect";
        if (mesh_path)
        {
            pso.shaders[RHI_Shader_Type::MeshShader] = GetShader(Renderer_Shader::meshlet_mesh_m);
        }
        else
        {
            pso.shaders[RHI_Shader_Type::Vertex] = GetShader(Renderer_Shader::gbuffer_indirect_v);
        }
        pso.shaders[RHI_Shader_Type::Pixel]  = GetShader(Renderer_Shader::gbuffer_indirect_p);
        pso.blend_state                      = GetBlendState(Renderer_BlendState::Off);
        pso.rasterizer_state                 = cvar_wireframe.GetValueAs<bool>() ? GetRasterizerState(Renderer_RasterizerState::Wireframe) : GetRasterizerState(Renderer_RasterizerState::Solid);
        pso.primitive_topology               =
            GetSecondaryViewMode() ==
            Renderer_SecondaryViewMode::Vertices
                ? RHI_PrimitiveTopology::PointList
                : RHI_PrimitiveTopology::TriangleList;
        // opaque uses greater-equal, an equality test demands bit identical depth from two different mesh
        // shaders, the prepass runs meshlet_mesh_depth_m and this pass runs meshlet_mesh_m, so a one bit
        // disagreement leaves the pixel unshaded, which reads as a hole through the terrain that flickers
        // as taa rejitters the projection, the alpha half below restores equal, it needs it for cutouts
        pso.depth_stencil_state              = GetDepthStencilState(Renderer_DepthStencilState::ReadWrite);
        pso.vrs_input_texture                =
            cvar_variable_rate_shading.GetValueAs<bool>() &&
            !IsSecondaryViewActive()
                ? GetRenderTarget(
                    Renderer_RenderTarget::shading_rate
                )
                : nullptr;
        pso.resolution_scale                 = true;
        pso.SetColorTargets(
            GetRenderTarget(Renderer_RenderTarget::gbuffer_color),
            GetRenderTarget(Renderer_RenderTarget::gbuffer_normal),
            GetRenderTarget(Renderer_RenderTarget::gbuffer_material),
            GetRenderTarget(Renderer_RenderTarget::gbuffer_velocity)
        );
        pso.SetDepthTarget(GetRenderTarget(Renderer_RenderTarget::gbuffer_depth));
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
        RHI_CommandList::SetPipelineState(pso);

        if (m_indirect_draw_count > 0)
        {
            RHI_CommandList::SetCullMode(RHI_CullMode::None);
            if (mesh_path)
            {
                bind_mesh_shader_geometry();
                push_mesh_draw_constants(m_pcb_pass_cpu);
                RHI_CommandList::DrawMeshTasksIndirect(GetBuffer(Renderer_Buffer::TriangleDispatchArgs), 0);
            }
            else
            {
                RHI_CommandList::SetBuffer(static_cast<uint32_t>(Renderer_BindingsUav::indirect_draw_data), GetBuffer(Renderer_Buffer::IndirectDrawData));
                RHI_CommandList::SetBuffer(static_cast<uint32_t>(Renderer_BindingsUav::meshlet_instances), GetBuffer(Renderer_Buffer::MeshletInstances));
                RHI_CommandList::SetBuffer(static_cast<uint32_t>(Renderer_BindingsUav::visible_triangles), GetBuffer(Renderer_Buffer::VisibleTriangles));
                RHI_CommandList::SetBuffer(static_cast<uint32_t>(Renderer_BindingsUav::meshlet_bounds), GeometryBuffer::GetMeshletBoundsBuffer());
                m_pcb_pass_cpu.set_f4_value(0.0f, 0.0f, 0.0f, 0.0f);
                RHI_CommandList::PushConstants(m_pcb_pass_cpu);
                RHI_CommandList::DrawIndirect(GetBuffer(Renderer_Buffer::IndirectDrawArgs), 0);
            }

            // alpha-tested half, same equal-z pixel shader, loads the g-buffer so the opaque output survives
            pso.clear_color[0] = rhi_color_load;
            pso.clear_color[1] = rhi_color_load;
            pso.clear_color[2] = rhi_color_load;
            pso.clear_color[3] = rhi_color_load;
            // equal, not greater-equal, so only prepass survivors draw, otherwise cutouts render as solid quads
            pso.depth_stencil_state = GetDepthStencilState(Renderer_DepthStencilState::ReadEqual);
            if (mesh_path)
            {
                // the alpha variant compiles GBUFFER_ALPHA so it keeps the alpha survivors instead of the opaque ones
                pso.shaders[RHI_Shader_Type::MeshShader] = GetShader(Renderer_Shader::meshlet_mesh_alpha_m);
            }
            RHI_CommandList::SetPipelineState(pso);
            RHI_CommandList::SetCullMode(RHI_CullMode::None);
            if (mesh_path)
            {
                bind_mesh_shader_geometry();
                push_mesh_draw_constants(m_pcb_pass_cpu);
                RHI_CommandList::DrawMeshTasksIndirect(GetBuffer(Renderer_Buffer::TriangleDispatchArgs), 0);
            }
            else
            {
                RHI_CommandList::SetBuffer(static_cast<uint32_t>(Renderer_BindingsUav::indirect_draw_data), GetBuffer(Renderer_Buffer::IndirectDrawData));
                RHI_CommandList::SetBuffer(static_cast<uint32_t>(Renderer_BindingsUav::meshlet_instances), GetBuffer(Renderer_Buffer::MeshletInstances));
                RHI_CommandList::SetBuffer(static_cast<uint32_t>(Renderer_BindingsUav::visible_triangles), GetBuffer(Renderer_Buffer::VisibleTriangles));
                RHI_CommandList::SetBuffer(static_cast<uint32_t>(Renderer_BindingsUav::meshlet_bounds), GeometryBuffer::GetMeshletBoundsBuffer());
                m_pcb_pass_cpu.set_f4_value(static_cast<float>(GetBuffer(Renderer_Buffer::VisibleTriangles)->GetElementCount() / 2), 0.0f, 0.0f, 0.0f);
                RHI_CommandList::PushConstants(m_pcb_pass_cpu);
                RHI_CommandList::DrawIndirect(GetBuffer(Renderer_Buffer::IndirectDrawArgs), arg_stride);
            }
        }
    }

    void Renderer::Pass_GBuffer_TessellatedAndTransparent(const bool is_transparent_pass)
    {
        const bool xr_multiview = Xr::IsSessionRunning() && Xr::GetStereoMode();

        RHI_PipelineState pso;
        pso.name                             = is_transparent_pass ? "g_buffer_transparent" : "g_buffer_tessellated";
        pso.shaders[RHI_Shader_Type::Vertex] = GetShader(Renderer_Shader::gbuffer_v);
        pso.shaders[RHI_Shader_Type::Pixel]  = GetShader(Renderer_Shader::gbuffer_p);
        pso.blend_state                      = GetBlendState(Renderer_BlendState::Off);
        pso.rasterizer_state                 = cvar_wireframe.GetValueAs<bool>() ? GetRasterizerState(Renderer_RasterizerState::Wireframe) : GetRasterizerState(Renderer_RasterizerState::Solid);
        pso.primitive_topology               = GetSecondaryViewMode() == Renderer_SecondaryViewMode::Vertices ? RHI_PrimitiveTopology::PointList : RHI_PrimitiveTopology::TriangleList;
        // both halves of this pass own their depth
        //
        // the tessellated half cannot use equal-z. the indirect path can, because there the prepass and the
        // g buffer rasterize the same vertices through the same transform. here the vertices are generated,
        // the patch is subdivided and displaced by a chain that reads the layer rules and a height map, and
        // it is driven by two separately compiled vertex shaders, depth_prepass_v in the prepass and
        // gbuffer_v here. those agree only to the last bit, and equal-z rejects anything inexact, so a
        // disagreement costs the whole patch its shading
        pso.depth_stencil_state              = GetDepthStencilState(Renderer_DepthStencilState::ReadWrite);
        pso.vrs_input_texture                =
            cvar_variable_rate_shading.GetValueAs<bool>() &&
            !IsSecondaryViewActive()
                ? GetRenderTarget(
                    Renderer_RenderTarget::shading_rate
                )
                : nullptr;
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
            Render* render                     = draw_call.render;
            Material* material                 = render->GetMaterial();
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
                RHI_CommandList::SetPipelineState(pso);
                pipeline_set = true;
            }

            m_pcb_pass_cpu.draw_index     = draw_call.draw_data_index;
            m_pcb_pass_cpu.is_transparent = is_transparent_pass ? 1 : 0;
            m_pcb_pass_cpu.material_index = material->GetIndex();
            RHI_CommandList::PushConstants(m_pcb_pass_cpu);

            RHI_CommandList::SetCullMode(cvar_wireframe.GetValueAs<bool>() ? RHI_CullMode::None : static_cast<RHI_CullMode>(material->GetProperty(MaterialProperty::CullMode)));
            RHI_Buffer* instance_buffer = GeometryBuffer::GetInstanceBuffer() ? GeometryBuffer::GetInstanceBuffer() : GetBuffer(Renderer_Buffer::DummyInstance);
            RHI_CommandList::SetBufferVertex(render->GetVertexBuffer(), instance_buffer);
            RHI_CommandList::SetBufferIndex(render->GetIndexBuffer());

            RHI_CommandList::DrawIndexed(
                render->GetIndexCount(draw_call.lod_index),
                render->GetIndexOffset(draw_call.lod_index),
                render->GetVertexOffset(draw_call.lod_index),
                render->GetGlobalInstanceOffset() + draw_call.instance_index,
                draw_call.instance_count
            );

            pso.clear_depth = rhi_depth_load;
        }
    }

    void Renderer::Pass_GBuffer(const bool is_transparent_pass)
    {
        RHI_CommandList::BeginTimeblock(is_transparent_pass ? "g_buffer_transparent" : "g_buffer");
        {
            if (!is_transparent_pass)
            {
                Pass_GBuffer_Indirect();
                // procedural grass runs after the indirect path, the draw call binds its own pipeline that reads grass_instances directly
                Pass_Grass_Draw();
            }

            Pass_GBuffer_TessellatedAndTransparent(is_transparent_pass);

            if (!is_transparent_pass)
            {
                // opaque depth blit moved here from the prepass, all opaque geometry including grass has rasterized so the
                // opaque output carries grass occlusion, batch b consumers run after phase 1 so this write is visible to them
                RHI_Texture* tex_depth        = GetRenderTarget(Renderer_RenderTarget::gbuffer_depth);
                RHI_Texture* tex_depth_output = GetRenderTarget(Renderer_RenderTarget::gbuffer_depth_opaque_output);
                RHI_CommandList::Blit(tex_depth, tex_depth_output, false, Renderer::GetResolutionScale());
            }

        }
        RHI_CommandList::EndTimeblock();

        if (!is_transparent_pass && !IsSecondaryViewActive())
        {
            // every renderable, not just this frame's draw list, a rotating car hides
            // interior parts that later come back with a stale previous matrix and
            // taa plus reflections then sparkle
            for (Entity* entity : World::GetEntitiesWithRender())
            {
                if (entity)
                {
                    entity->SetMatrixPrevious(entity->GetMatrix());
                }
            }
        }
    }

    void Renderer::Pass_Grass_Populate()
    {
        // fills the per-slot per-lod sections of grass_instances around the camera, then bakes
        // instance_count into the indirect args, slot 0 is grass and the higher slots are micro detail

        bool any_ready = false;
        for (const PassState::GpuScatterSlot& slot : m_pass_state.gpu_scatter)
        {
            any_ready = any_ready || gpu_scatter_ready(slot);
        }
        if (!any_ready)
        {
            return;
        }

        // camera position used as the anchor for the ring grid, the populate shader snaps it to the cell grid
        Camera* camera = World::GetCamera();
        if (!camera || !camera->GetEntity())
        {
            return;
        }

        RHI_CommandList::BeginPass("gpu_scatter_populate");
        {
            RHI_Buffer* buf_instances = GetBuffer(Renderer_Buffer::GrassInstances);
            RHI_Buffer* buf_count     = GetBuffer(Renderer_Buffer::GrassCount);
            RHI_Buffer* buf_args      = GetBuffer(Renderer_Buffer::GrassIndirectArgs);

            // bake static portions of the per-lod indirect args once per slot (or whenever its mesh changes)
            // these never change frame to frame so a single Update covers the lifetime of the slot
            for (uint32_t slot = 0; slot < renderer_max_gpu_scatter_slots; slot++)
            {
                PassState::GpuScatterSlot& state = m_pass_state.gpu_scatter[slot];
                if (!gpu_scatter_ready(state) || state.args_baked)
                {
                    continue;
                }

                RHI_CommandList::UpdateBuffer(
                    buf_args,
                    renderer_gpu_scatter_arg_index(slot, 0) * static_cast<uint32_t>(sizeof(Sb_IndirectDrawArgs)),
                    static_cast<uint32_t>(sizeof(Sb_IndirectDrawArgs) * renderer_max_gpu_scatter_lods),
                    &state.indirect_args_static[0],
                    false
                );
                state.args_baked = true;
            }

            // clear every counter on the gpu timeline, a mapped cpu memcpy races the in flight previous frame and drops a frame of scatter
            uint32_t zero_counts[renderer_max_gpu_scatter_args] = {};
            RHI_CommandList::UpdateBuffer(buf_count, 0, sizeof(zero_counts), &zero_counts[0], false);

            // the terrain frame is shared, every slot scatters onto the same surface
            Vector3 terrain_offset;
            Vector4 terrain_mapping_live;
            get_grass_terrain_frame_state(terrain_offset, terrain_mapping_live);
            const float terrain_entity_y = terrain_offset.y;

            // populate dispatches, one per slot per lod ring, each fills its own range of grass_instances
            RHI_CommandList::SetShader(GetShader(Renderer_Shader::grass_populate_c));

            for (uint32_t slot = 0; slot < renderer_max_gpu_scatter_slots; slot++)
            {
                const PassState::GpuScatterSlot& state = m_pass_state.gpu_scatter[slot];
                if (!gpu_scatter_ready(state))
                {
                    continue;
                }

                RHI_CommandList::SetBuffer(static_cast<uint32_t>(Renderer_BindingsUav::grass_instances), buf_instances);
                RHI_CommandList::SetBuffer(static_cast<uint32_t>(Renderer_BindingsUav::grass_count), buf_count);
                // the populate shader samples the terrain heightmap through the tex slot
                RHI_CommandList::SetTexture(static_cast<uint32_t>(Renderer_BindingsSrv::tex), state.heightmap);
                // occluder hi-z on tex2 drives the per-instance frustum + occlusion cull, built by Pass_HiZ which runs earlier this frame
                RHI_CommandList::SetTexture(static_cast<uint32_t>(Renderer_BindingsSrv::tex2), GetRenderTarget(Renderer_RenderTarget::gbuffer_depth_occluders_hiz));
                // biome prop mask on tex3, the slot picks the channel, black means nothing is suitable
                RHI_CommandList::SetTexture(static_cast<uint32_t>(Renderer_BindingsSrv::tex3), state.prop_mask ?
                        state.prop_mask :
                        GetStandardTexture(Renderer_StandardTexture::Black));

                const float max_slope_cos = cosf(state.params.max_slope_deg * (math::pi / 180.0f));
                // a negative weight is the slot saying it wants no biome gate at all, which is what micro
                // detail asks for. a slot that does want one and has no mask fails closed instead of
                // filling the world
                const float biome_min = state.params.biome_min_weight < 0.0f ?
                    -1.0f :
                    (state.prop_mask ? state.params.biome_min_weight : 2.0f);

                Vector4 terrain_mapping = terrain_mapping_live;
                if (terrain_mapping.z == 0.0f && terrain_mapping.w == 0.0f)
                {
                    terrain_mapping = state.params.terrain_world_mapping;
                    terrain_mapping.x += terrain_offset.x;
                    terrain_mapping.y += terrain_offset.z;
                }

                // the populate shader has no push constant floats left, so the slot, the mask channel
                // and the two ends of the size range travel in the spare bits of draw_index
                const uint32_t mask_channel = std::min(state.params.mask_channel, 3u);
                const uint32_t scale_min    = pack_instance_scale(std::min(state.params.size_min, state.params.size_max));
                const uint32_t scale_max    = pack_instance_scale(std::max(state.params.size_min, state.params.size_max));

                for (uint32_t lod = 0; lod < renderer_max_gpu_scatter_lods; lod++)
                {
                    const float cell_size   = state.params.cell_size_m[lod];
                    const float ring_radius = state.params.ring_radii_m[lod];
                    if (cell_size <= 0.0f || ring_radius <= 0.0f)
                    {
                        continue;
                    }

                    const float inner_radius = lod == 0 ?
                        0.0f :
                        std::min(
                            state.params.ring_radii_m[lod - 1],
                            ring_radius
                        );
                    // grass_instances is partitioned by slot and lod, the base is the cumulative prefix
                    // sum of every cap before it so each ring writes into its own contiguous range
                    const uint32_t lod_base   = renderer_gpu_scatter_base(slot, lod);
                    const uint32_t lod_cap    = gpu_scatter_lod_cap(slot, lod, state.params.density);

                    // layout mirrors grass_populate.hlsl values[0..2]
                    // values[0] = (cell_size, ring_radius, lod_base, max_instances_per_lod)
                    // values[1] = (height_min, height_max, max_slope_cos, inner_radius)
                    // values[2] = (map_origin_x, map_origin_z, map_inv_x, map_inv_z)
                    // heightmap is r32 local y, material_index bitcast is the entity y offset
                    // is_transparent bitcast carries biome_min_weight, negative disables the mask gate
                    m_pcb_pass_cpu.is_transparent = *reinterpret_cast<const uint32_t*>(&biome_min);
                    m_pcb_pass_cpu.draw_index     = lod              |
                                                    (slot     << 4)  |
                                                    (mask_channel << 8) |
                                                    (scale_min << 12) |
                                                    (scale_max << 20);
                    m_pcb_pass_cpu.material_index = *reinterpret_cast<const uint32_t*>(&terrain_entity_y);
                    m_pcb_pass_cpu.v[0]  = cell_size;
                    m_pcb_pass_cpu.v[1]  = ring_radius;
                    m_pcb_pass_cpu.v[2]  = static_cast<float>(lod_base);
                    m_pcb_pass_cpu.v[3]  = static_cast<float>(lod_cap);
                    m_pcb_pass_cpu.v[4]  = state.params.height_min;
                    m_pcb_pass_cpu.v[5]  = state.params.height_max;
                    m_pcb_pass_cpu.v[6]  = max_slope_cos;
                    m_pcb_pass_cpu.v[7]  = inner_radius;
                    m_pcb_pass_cpu.v[8]  = terrain_mapping.x;
                    m_pcb_pass_cpu.v[9]  = terrain_mapping.y;
                    m_pcb_pass_cpu.v[10] = terrain_mapping.z;
                    m_pcb_pass_cpu.v[11] = terrain_mapping.w;
                    RHI_CommandList::PushConstants(m_pcb_pass_cpu);

                    // one cell per thread, dispatch z carries the instance index inside the cell, the
                    // shader recomputes instances_per_cell so both formulas must match
                    const uint32_t cells_per_axis =
                        2u *
                        static_cast<uint32_t>(
                            ceilf(ring_radius / cell_size)
                        ) +
                        2u;
                    const uint32_t groups = (cells_per_axis + 7u) / 8u;
                    const float ring_area = math::pi * (
                        (ring_radius * ring_radius) -
                        (inner_radius * inner_radius)
                    );
                    const float cells_in_ring =
                        ring_area /
                        (cell_size * cell_size);
                    const uint32_t blades_per_cell = std::max(
                        1u,
                        static_cast<uint32_t>(
                            std::floor(
                                static_cast<float>(lod_cap) /
                                std::max(cells_in_ring, 1.0f)
                            )
                        )
                    );
                    RHI_CommandList::Dispatch(groups, groups, blades_per_cell);
                }
            }

            // args build, reads grass_count and writes the instance_count of this slot's three entries
            {
                RHI_CommandList::SetShader(
                    GetShader(Renderer_Shader::grass_indirect_args_c),
                    "grass_indirect_args"
                );

                RHI_CommandList::SetBuffer(static_cast<uint32_t>(Renderer_BindingsUav::grass_count), buf_count);
                RHI_CommandList::SetBuffer(static_cast<uint32_t>(Renderer_BindingsUav::grass_indirect_args), buf_args);

                // values[0] = (cap_lod0, cap_lod1, cap_lod2, lod_count), the args shader clamps each
                // atomic counter against its own cap, draw_index is the first args entry of the slot
                static_assert(renderer_max_gpu_scatter_lods == 3, "grass_indirect_args push constant layout assumes 3 lods");

                for (uint32_t slot = 0; slot < renderer_max_gpu_scatter_slots; slot++)
                {
                    const PassState::GpuScatterSlot& state = m_pass_state.gpu_scatter[slot];
                    if (!gpu_scatter_ready(state))
                    {
                        continue;
                    }

                    m_pcb_pass_cpu.material_index = 0;
                    m_pcb_pass_cpu.is_transparent = 0;
                    m_pcb_pass_cpu.draw_index     = renderer_gpu_scatter_arg_index(slot, 0);
                    m_pcb_pass_cpu.v[0] = static_cast<float>(gpu_scatter_lod_cap(slot, 0, state.params.density));
                    m_pcb_pass_cpu.v[1] = static_cast<float>(gpu_scatter_lod_cap(slot, 1, state.params.density));
                    m_pcb_pass_cpu.v[2] = static_cast<float>(gpu_scatter_lod_cap(slot, 2, state.params.density));
                    m_pcb_pass_cpu.v[3] = static_cast<float>(renderer_max_gpu_scatter_lods);
                    RHI_CommandList::PushConstants(m_pcb_pass_cpu);

                    RHI_CommandList::Dispatch(1, 1, 1);
                }
            }
        }
        RHI_CommandList::EndPass();
    }

    void Renderer::Pass_Grass_Draw()
    {
        // gpu scatter raster, one DrawIndexedIndirect per slot per lod ring, runs once inside the
        // g-buffer pass, shares the geometry stage render pass and sets its own vertex shader that
        // reads grass_instances

        bool any_ready = false;
        for (const PassState::GpuScatterSlot& slot : m_pass_state.gpu_scatter)
        {
            any_ready = any_ready ||
                        (gpu_scatter_ready(slot) &&
                         slot.args_baked         &&
                         slot.material           &&
                         slot.mesh->GetVertexBuffer() &&
                         slot.mesh->GetIndexBuffer());
        }
        if (!any_ready)
        {
            return;
        }

        // wait for the geometry buffer and the bindless material index, a zero index reads another render's textures
        if (!GeometryBuffer::GetInstanceBuffer())
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

        RHI_CommandList::SetPipelineState(pso);

        // grass blades are double sided and a stone chip is closed, but both are cheap enough that one
        // raster state for every slot is not worth a second pipeline
        RHI_CommandList::SetCullMode(RHI_CullMode::None);

        // the scatter vs never reads the per-instance stream, it is bound to the global instance buffer only to keep the vertex layout uniform
        RHI_Buffer* buf_instances     = GetBuffer(Renderer_Buffer::GrassInstances);
        RHI_Buffer* buf_args          = GetBuffer(Renderer_Buffer::GrassIndirectArgs);
        RHI_Buffer* binding1_instance = GeometryBuffer::GetInstanceBuffer() ? GeometryBuffer::GetInstanceBuffer() : GetBuffer(Renderer_Buffer::DummyInstance);
        RHI_CommandList::SetBuffer(static_cast<uint32_t>(Renderer_BindingsUav::grass_instances), buf_instances);

        const uint32_t arg_stride = static_cast<uint32_t>(sizeof(Sb_IndirectDrawArgs));

        for (uint32_t slot = 0; slot < renderer_max_gpu_scatter_slots; slot++)
        {
            const PassState::GpuScatterSlot& state = m_pass_state.gpu_scatter[slot];
            if (!gpu_scatter_ready(state) || !state.args_baked || !state.material)
            {
                continue;
            }

            Mesh* mesh = state.mesh;
            if (!mesh->GetVertexBuffer() || !mesh->GetIndexBuffer())
            {
                continue;
            }

            RHI_CommandList::SetBufferVertex(mesh->GetVertexBuffer(), binding1_instance);
            RHI_CommandList::SetBufferIndex(mesh->GetIndexBuffer());

            for (uint32_t lod = 0; lod < renderer_max_gpu_scatter_lods; lod++)
            {
                const uint32_t lod_base = renderer_gpu_scatter_base(slot, lod);

                // values[0] = (0, 0, lod_base, lod_index), the scatter vs reads lod_base from values[0].z
                m_pcb_pass_cpu.draw_index     = 0;
                m_pcb_pass_cpu.is_transparent = 0;
                m_pcb_pass_cpu.material_index = state.material->GetIndex();
                m_pcb_pass_cpu.v[0] = 0.0f;
                m_pcb_pass_cpu.v[1] = 0.0f;
                m_pcb_pass_cpu.v[2] = static_cast<float>(lod_base);
                m_pcb_pass_cpu.v[3] = static_cast<float>(lod);
                RHI_CommandList::PushConstants(m_pcb_pass_cpu);

                // an empty ring bakes instance_count 0 into the args so the gpu skips it at near-zero cost
                RHI_CommandList::DrawIndexedIndirect(buf_args, renderer_gpu_scatter_arg_index(slot, lod) * arg_stride);
            }
        }
    }

    void Renderer::Pass_MeshletVisualize()
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

        RHI_CommandList::BeginTimeblock("meshlet_visualize");
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
            RHI_CommandList::SetPipelineState(pso);

            RHI_CommandList::SetBuffer(static_cast<uint32_t>(Renderer_BindingsUav::indirect_draw_data), GetBuffer(Renderer_Buffer::IndirectDrawData));
            RHI_CommandList::SetBuffer(static_cast<uint32_t>(Renderer_BindingsUav::meshlet_instances), GetBuffer(Renderer_Buffer::MeshletInstances));
            RHI_CommandList::SetBuffer(static_cast<uint32_t>(Renderer_BindingsUav::visible_triangles), GetBuffer(Renderer_Buffer::VisibleTriangles));
            RHI_CommandList::SetBuffer(static_cast<uint32_t>(Renderer_BindingsUav::meshlet_bounds), GeometryBuffer::GetMeshletBoundsBuffer());

            // wireframe shows both faces so rear edges of thin meshlets stay visible, solid mode leaves culling to the triangle cull pass
            RHI_CommandList::SetCullMode(RHI_CullMode::None);

            // f3.x: 0 = color by global meshlet index, 1 = color by post-cull draw id
            // f4.x carries the visible-triangle region base, draw the opaque half then the alpha half
            const uint32_t arg_stride = static_cast<uint32_t>(sizeof(Sb_IndirectDrawArgs));
            m_pcb_pass_cpu.set_f3_value(color_by_draw_id ? 1.0f : 0.0f, 0.0f, 0.0f);

            m_pcb_pass_cpu.set_f4_value(0.0f, 0.0f, 0.0f, 0.0f);
            RHI_CommandList::PushConstants(m_pcb_pass_cpu);
            RHI_CommandList::DrawIndirect(GetBuffer(Renderer_Buffer::IndirectDrawArgs), 0);

            m_pcb_pass_cpu.set_f4_value(static_cast<float>(GetBuffer(Renderer_Buffer::VisibleTriangles)->GetElementCount() / 2), 0.0f, 0.0f, 0.0f);
            RHI_CommandList::PushConstants(m_pcb_pass_cpu);
            RHI_CommandList::DrawIndirect(GetBuffer(Renderer_Buffer::IndirectDrawArgs), arg_stride);
        }
        RHI_CommandList::EndTimeblock();
    }
}
