/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

//= INCLUDES ==================================
#include "pch.h"
#include "../profiling/Profiler.h"
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
#include "../core/Engine.h"
#include "../core/Timer.h"
//=============================================

//= NAMESPACES ===============
using namespace std;
using namespace spartan::math;
//============================

namespace spartan
{
    namespace
    {
        TConsoleVar<bool> cvar_grass_specialized("r.grass_specialized", true,
            "use grass-only shading, compact vertex exports and mesh bounds; disable for comparison");
        TConsoleVar<float> cvar_grass_lod_pixels("r.grass_lod_pixels", 1.0f,
            "estimated blade simplification error in output pixels; zero keeps authored mesh detail");
        TConsoleVar<float> cvar_grass_impostor_distance("r.grass_impostor_distance", 8000.0f,
            "reach in metres of the outer grass ring, which draws tuft impostor cards instead of blades; 0 keeps blades there");
        TConsoleVar<float> cvar_grass_impostor_thin("r.grass_impostor_thin", 300.0f,
            "distance in metres past which impostor cards thin out with the square of distance and widen to keep the same coverage; 0 keeps every card");
        TConsoleVar<float> cvar_grass_impostor_start("r.grass_impostor_start", 90.0f,
            "distance in metres where tuft impostor cards take over from blades, the blade ring before them shrinks to meet it at the same density");
        // 12.5 cm cells retain tire detail across a much larger history field (32 MiB total).
        constexpr uint32_t grass_interaction_resolution = 1024;
        constexpr float grass_interaction_size = 128.0f;
        TConsoleVar<float> cvar_grass_track_radius("r.grass_track_radius", 48.0f,
            "distance from the car in metres within which pressed grass stays down (1 to 52)");
        TConsoleVar<float> cvar_grass_track_recovery("r.grass_track_recovery", 0.05f,
            "grass recovery rate after the car leaves, in inverse seconds");
        // 12.5 cm cells, enough for a single leaf to catch a bumper or a shin
        constexpr uint32_t vegetation_interaction_resolution = 512;
        constexpr float vegetation_interaction_size = 64.0f;
        TConsoleVar<float> cvar_vegetation_frequency("r.vegetation_interaction_frequency", 1.6f,
            "natural frequency in hz at which pushed foliage swings back");
        TConsoleVar<float> cvar_vegetation_damping("r.vegetation_interaction_damping", 0.2f,
            "damping ratio of pushed foliage, lower values oscillate longer before settling");
        // Matches VegetationInteractor in vegetation_interaction.hlsl.
        struct VegetationInteractorGpu
        {
            Vector4 center_radius;
            Vector4 right;
            Vector4 forward;
            Vector4 velocity;
        };
        TConsoleVar<bool> cvar_shadows_gpu_driven("r.shadows_gpu_driven", true,
            "cull shadow casters per atlas slice and instance on the gpu; disable for the cpu path");
        // upper bound of (slice, instance) survivors, 8 bytes each
        constexpr uint32_t shadow_visible_capacity_max = 8u * 1024u * 1024u;
        // Matches GrassWheelContact in grass_interaction.hlsl.
        struct GrassWheelContact
        {
            Vector4 start_width;
            Vector4 end_length;
            Vector4 direction_pressure;
            Vector4 normal;
        };
        static_assert(sizeof(GrassWheelContact) == 64);

        TConsoleVar<float> cvar_tree_wind_cache_entries("r.tree_wind_cache_entries", static_cast<float>(TREE_WIND_CACHE_CAPACITY),
            "visible-instance root wind cache entries; 0 uses the identical uncached path, never affects population");
        TConsoleVar<float> cvar_hiz_depth_bias("r.hiz_depth_bias", 2e-7f,
            "minimum reverse-Z mesh/meshlet occlusion margin; 0.01 reproduces the former permissive bias");

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
            // the meshlet shaders transform and cull with the left eye only, multiview would draw left eye geometry into the right eye
            if (!cvar_mesh_shaders.GetValueAs<bool>() || !RHI_Device::IsSupportedMeshShaders() || Renderer::IsStereoView())
            {
                return false;
            }

            RHI_Shader* mesh_shader        = Renderer::GetShader(Renderer_Shader::meshlet_mesh_m);
            RHI_Shader* mesh_alpha_shader  = Renderer::GetShader(Renderer_Shader::meshlet_mesh_alpha_m);
            RHI_Shader* depth_shader       = Renderer::GetShader(Renderer_Shader::meshlet_mesh_depth_m);
            RHI_Shader* depth_alpha_shader = Renderer::GetShader(Renderer_Shader::meshlet_mesh_depth_alpha_m);
            RHI_Shader* depth_late_shader  = Renderer::GetShader(Renderer_Shader::meshlet_mesh_depth_late_m);
            RHI_Shader* depth_alpha_late   = Renderer::GetShader(Renderer_Shader::meshlet_mesh_depth_alpha_late_m);
            return mesh_shader && mesh_shader->IsCompiled() &&
                   mesh_alpha_shader && mesh_alpha_shader->IsCompiled() &&
                   depth_shader && depth_shader->IsCompiled() &&
                   depth_alpha_shader && depth_alpha_shader->IsCompiled() &&
                   depth_late_shader && depth_late_shader->IsCompiled() &&
                   depth_alpha_late && depth_alpha_late->IsCompiled();
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

        // the outer ring of a grass slot draws tuft impostor cards instead of blades, one card per cell
        // carries a whole tuft, so the ring can reach much further for a fraction of the instances
        constexpr uint32_t gpu_scatter_card_lod = renderer_max_gpu_scatter_lods - 1;
        // card width in cells, neighbours overlap so the tufts of adjacent cards interleave
        constexpr float gpu_scatter_card_cells = 1.5f;

        bool gpu_scatter_cards(const Renderer::PassState::GpuScatterSlot& slot)
        {
            return cvar_grass_impostor_distance.GetValue() > 0.0f &&
                   cvar_grass_specialized.GetValue()              &&
                   !Xr::IsSessionRunning()                        &&
                   gpu_scatter_ready(slot)                        &&
                   slot.material                                  &&
                   slot.params.uv_patch == 0.0f                   &&
                   slot.mesh->GetObjectName() == "grass_blade"    &&
                   slot.mesh->GetLodCount(0) == 3                 &&
                   slot.material->GetProperty(MaterialProperty::IsGrassBlade) != 0.0f &&
                   slot.material->GetProperty(MaterialProperty::IsTerrain) == 0.0f    &&
                   slot.material->GetProperty(MaterialProperty::IsWater) == 0.0f      &&
                   slot.material->GetProperty(MaterialProperty::IsFlower) == 0.0f     &&
                   slot.material->GetProperty(MaterialProperty::IsSkidMark) == 0.0f;
        }

        bool gpu_scatter_card_ring(const Renderer::PassState::GpuScatterSlot& slot, const uint32_t lod)
        {
            return lod == gpu_scatter_card_lod && slot.params.ring_radii_m[lod] > 0.0f && gpu_scatter_cards(slot);
        }

        // past this distance a card stands in for the ones thinned out around it, so its width grows with
        // distance, has to match the scale populate and the vertex shader derive from the same number
        float gpu_scatter_card_thin()
        {
            return max(cvar_grass_impostor_thin.GetValue(), 0.0f);
        }

        float gpu_scatter_card_scale(const float distance)
        {
            const float thin = gpu_scatter_card_thin();
            return thin > 0.0f ? max(1.0f, distance / thin) : 1.0f;
        }

        // blades past a few tens of metres are thinner than a pixel, so the blade ring in front of the cards
        // is pulled in to where the cards start and the card ring reaches out as far as it is asked to
        float gpu_scatter_ring_radius(const Renderer::PassState::GpuScatterSlot& slot, const uint32_t lod)
        {
            const float radius = slot.params.ring_radii_m[lod];
            if (radius <= 0.0f || !gpu_scatter_card_ring(slot, gpu_scatter_card_lod))
            {
                return radius;
            }

            if (lod == gpu_scatter_card_lod)
            {
                return max(radius, cvar_grass_impostor_distance.GetValue());
            }

            if (lod == gpu_scatter_card_lod - 1)
            {
                const float inner = lod > 0 ? slot.params.ring_radii_m[lod - 1] : 0.0f;
                return clamp(cvar_grass_impostor_start.GetValue(), min(inner + 10.0f, radius), radius);
            }

            return radius;
        }

        // has to match GRASS_FILL_MARGIN in grass_populate.hlsl
        const float gpu_scatter_fill_margin = 0.85f;

        // has to match grass_patch_max_boost in grass_populate.hlsl
        const float gpu_scatter_patch_max_boost = 4.0f;

        // has to match grass_max_boost in grass_populate.hlsl, this is what bounds the dispatch
        const float gpu_scatter_max_boost = 12.0f;

        // mirrors grass_patch_density_boost in grass_populate.hlsl, the two decide the same thing from
        // opposite ends, this one sizes dispatch z and the shader one sizes the keep probability, so a
        // disagreement shows up as a ring that is either starved or clipped by its own atomic
        //
        // pulling a slot into pockets empties most of the ring, spreading the same budget over what is
        // left is what makes clustering buy density rather than cost it
        float gpu_scatter_patch_boost(const float patch_size, const float coverage)
        {
            if (patch_size <= 0.0f || coverage >= 1.0f)
            {
                return 1.0f;
            }

            // the threshold keeps coverage exactly and the fringe is symmetric about it
            const float expected = clamp(coverage, 0.0f, 1.0f);

            return min(1.0f / max(expected, 0.08f), gpu_scatter_patch_max_boost);
        }

        // the ring is a full circle but the camera only ever sees a wedge of it, and the populate pass
        // culls against the frustum before it allocates, so most of the per lod budget was being spent
        // on candidates behind the viewer and then thrown away. the counter was sitting near a tenth of
        // its cap, which is the real reason the cover read as thin no matter how the rings were tuned
        //
        // handing the visible wedge the budget the rest of the circle is never going to use is what
        // makes the cap mean something. it is deliberately an over estimate of the visible share, so the
        // boost lands under the truth, because overshooting saturates the atomic and grass then stops
        // dead partway across the view, which is far worse than leaving a little budget unspent
        float gpu_scatter_frustum_boost(const float fov_horizontal_rad)
        {
            const float wedge = clamp(fov_horizontal_rad, 0.35f, math::pi_2);

            // the wedge widens toward the whole circle as the camera pitches down, and near the viewer
            // the ring is inside the view whichever way it points, so half the circle is the floor
            const float visible = clamp((wedge * 1.6f) / math::pi_2, 0.18f, 1.0f);

            return 1.0f / visible;
        }

        // the populate dispatch stops writing at this many instances, so the args builder has to clamp
        // the atomic counter against the same number or the raster reads instances nobody wrote
        uint32_t gpu_scatter_lod_cap(const uint32_t slot, const uint32_t lod, const float density)
        {
            const float scaled = static_cast<float>(renderer_gpu_scatter_cap(slot, lod)) *
                                 clamp(density, 0.01f, 1.0f);

            return max(1u, static_cast<uint32_t>(floorf(scaled)));
        }

        // density is the cap spread over the ring, so a blade ring pulled in by the cards gives up the
        // share of its cap the lost ground held and stays exactly as thick as authored
        uint32_t gpu_scatter_slot_cap(const Renderer::PassState::GpuScatterSlot& state, const uint32_t slot, const uint32_t lod)
        {
            const uint32_t cap    = gpu_scatter_lod_cap(slot, lod, state.params.density);
            const float authored  = state.params.ring_radii_m[lod];
            const float radius    = gpu_scatter_ring_radius(state, lod);
            if (lod == 0 || authored <= 0.0f || radius >= authored)
            {
                return cap;
            }

            const float inner        = min(state.params.ring_radii_m[lod - 1], authored);
            const float area_ratio   = (radius * radius - inner * inner) / max(authored * authored - inner * inner, 1.0f);
            return max(1u, static_cast<uint32_t>(static_cast<float>(cap) * clamp(area_ratio, 0.0f, 1.0f)));
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

        // xz bounds of the view pyramid clipped to a box, the near plane is ignored so the result only
        // over covers, false means the pyramid misses the box. the populate pass dispatches over this
        // window instead of the whole ring square, most of which sits behind the camera
        bool gpu_scatter_frustum_xz_bounds(const Matrix& view_projection_inverted, const Vector3& eye, const Vector3& box_min, const Vector3& box_max, Vector2& out_min, Vector2& out_max)
        {
            // corner rays from two finite depths, so reverse z, infinite far and a regular far plane all work
            const float corners[4][2] = { { -1.0f, -1.0f }, { 1.0f, -1.0f }, { 1.0f, 1.0f }, { -1.0f, 1.0f } };
            array<Vector3, 4> edges;
            Vector3 forward = Vector3::Zero;
            for (uint32_t i = 0; i < 4; i++)
            {
                Vector3 a = Vector3(corners[i][0], corners[i][1], 0.25f) * view_projection_inverted;
                Vector3 b = Vector3(corners[i][0], corners[i][1], 0.75f) * view_projection_inverted;
                if (Vector3::DistanceSquared(a, eye) > Vector3::DistanceSquared(b, eye))
                {
                    swap(a, b);
                }
                edges[i] = Vector3::Normalize(b - a);
                forward += edges[i];
            }

            array<Vector3, 4> normals;
            for (uint32_t i = 0; i < 4; i++)
            {
                normals[i] = Vector3::Normalize(Vector3::Cross(edges[i], edges[(i + 1) % 4]));
                if (Vector3::Dot(normals[i], forward) < 0.0f)
                {
                    normals[i] = normals[i] * -1.0f;
                }
            }

            const auto inside_pyramid = [&](const Vector3& p)
            {
                const Vector3 to_p    = p - eye;
                const float tolerance = -1e-3f * (1.0f + to_p.Length());
                for (const Vector3& normal : normals)
                {
                    if (Vector3::Dot(normal, to_p) < tolerance)
                    {
                        return false;
                    }
                }
                return true;
            };

            const auto inside_box = [&](const Vector3& p)
            {
                return p.x >= box_min.x && p.x <= box_max.x && p.y >= box_min.y && p.y <= box_max.y && p.z >= box_min.z && p.z <= box_max.z;
            };

            bool any = false;
            const auto add = [&](const Vector3& p)
            {
                if (!any)
                {
                    out_min = Vector2(p.x, p.z);
                    out_max = Vector2(p.x, p.z);
                    any     = true;
                    return;
                }
                out_min.x = min(out_min.x, p.x);
                out_min.y = min(out_min.y, p.z);
                out_max.x = max(out_max.x, p.x);
                out_max.y = max(out_max.y, p.z);
            };

            // the clipped pyramid is convex, its vertices are the apex, where the corner rays cross the
            // box, where the box edges cross the side planes and the box corners inside the pyramid
            if (inside_box(eye))
            {
                add(eye);
            }

            const float eye_axes[3]     = { eye.x, eye.y, eye.z };
            const float box_min_axes[3] = { box_min.x, box_min.y, box_min.z };
            const float box_max_axes[3] = { box_max.x, box_max.y, box_max.z };
            for (const Vector3& direction : edges)
            {
                const float direction_axes[3] = { direction.x, direction.y, direction.z };
                float t_enter = 0.0f;
                float t_exit  = numeric_limits<float>::max();
                bool hit      = true;
                for (uint32_t axis = 0; axis < 3 && hit; axis++)
                {
                    const float origin = eye_axes[axis];
                    const float d      = direction_axes[axis];
                    if (fabsf(d) < 1e-8f)
                    {
                        hit = origin >= box_min_axes[axis] && origin <= box_max_axes[axis];
                        continue;
                    }
                    float t0 = (box_min_axes[axis] - origin) / d;
                    float t1 = (box_max_axes[axis] - origin) / d;
                    if (t0 > t1)
                    {
                        swap(t0, t1);
                    }
                    t_enter = max(t_enter, t0);
                    t_exit  = min(t_exit, t1);
                    hit     = t_enter <= t_exit;
                }
                if (hit)
                {
                    add(eye + direction * t_enter);
                    add(eye + direction * t_exit);
                }
            }

            array<Vector3, 8> box_corners;
            for (uint32_t i = 0; i < 8; i++)
            {
                box_corners[i] = Vector3((i & 1) ? box_max.x : box_min.x, (i & 2) ? box_max.y : box_min.y, (i & 4) ? box_max.z : box_min.z);
                if (inside_pyramid(box_corners[i]))
                {
                    add(box_corners[i]);
                }
            }

            for (uint32_t i = 0; i < 8; i++)
            {
                for (uint32_t bit = 1; bit < 8; bit <<= 1)
                {
                    if (i & bit)
                    {
                        continue;
                    }
                    const Vector3& p0 = box_corners[i];
                    const Vector3& p1 = box_corners[i | bit];
                    for (const Vector3& normal : normals)
                    {
                        const float d0 = Vector3::Dot(normal, p0 - eye);
                        const float d1 = Vector3::Dot(normal, p1 - eye);
                        if ((d0 < 0.0f) == (d1 < 0.0f))
                        {
                            continue;
                        }
                        const Vector3 p = p0 + (p1 - p0) * (d0 / (d0 - d1));
                        if (inside_pyramid(p))
                        {
                            add(p);
                        }
                    }
                }
            }

            return any;
        }

        void bind_impostor_texels()
        {
            RHI_Buffer* impostor_texels = GeometryBuffer::GetImpostorTexelBuffer();
            RHI_CommandList::SetBuffer(Renderer_BindingsUav::impostor_texels, impostor_texels ? impostor_texels : GeometryBuffer::GetMeshletVertexBuffer());
        }

        void bind_mesh_shader_geometry()
        {
            bind_impostor_texels();
            RHI_CommandList::SetBuffer(Renderer_BindingsUav::tree_wind_cache, Renderer::GetBuffer(Renderer_Buffer::TreeWindCache));
            RHI_CommandList::SetBuffer(Renderer_BindingsUav::indirect_draw_data, Renderer::GetBuffer(Renderer_Buffer::IndirectDrawData));
            RHI_CommandList::SetBuffer(Renderer_BindingsUav::meshlet_instances, Renderer::GetBuffer(Renderer_Buffer::MeshletInstances));
            RHI_CommandList::SetBuffer(Renderer_BindingsUav::meshlet_bounds, GeometryBuffer::GetMeshletBoundsBuffer());
            RHI_CommandList::SetBuffer(Renderer_BindingsUav::meshlet_vertices, GeometryBuffer::GetMeshletVertexBuffer());
            RHI_CommandList::SetBuffer(Renderer_BindingsUav::meshlet_micro_indices, GeometryBuffer::GetMeshletMicroIndexBuffer());
            RHI_CommandList::SetBuffer(Renderer_BindingsUav::cull_phase, Renderer::GetBuffer(Renderer_Buffer::CullPhase));
        }

        void bind_visible_triangle_geometry()
        {
            RHI_CommandList::SetBuffer(Renderer_BindingsUav::indirect_draw_data, Renderer::GetBuffer(Renderer_Buffer::IndirectDrawData));
            RHI_CommandList::SetBuffer(Renderer_BindingsUav::meshlet_instances, Renderer::GetBuffer(Renderer_Buffer::MeshletInstances));
            RHI_CommandList::SetBuffer(Renderer_BindingsUav::visible_triangles, Renderer::GetBuffer(Renderer_Buffer::VisibleTriangles));
            RHI_CommandList::SetBuffer(Renderer_BindingsUav::meshlet_bounds, GeometryBuffer::GetMeshletBoundsBuffer());
            RHI_CommandList::SetBuffer(Renderer_BindingsUav::cull_phase, Renderer::GetBuffer(Renderer_Buffer::CullPhase));
            bind_impostor_texels();
        }

        void push_mesh_draw_constants(Pcb_Pass& pcb)
        {
            // the opaque/alpha split is a shader variant now, the region base is unused by the mesh path
            // the push still runs because the alpha pixel shaders read the other pass fields
            pcb.set(pass_visible_triangles::region_base, 0u);
            pcb.set(pass_visible_triangles::late, false);
            RHI_CommandList::PushConstants(pcb);
        }
    }

    void Renderer::Pass_ShadowMaps()
    {
        SP_ASSERT_MSG(!IsRayTracedShadowsActive(), "Shadow techniques are mutually exclusive: raster shadow pass entered with RT shadows active");
        // Keep the guard even if assertions are disabled. Never silently add
        // raster cascades to RT for foliage animation or any other workaround.
        if (IsRayTracedShadowsActive() || GetViewLightEntities().empty())
        {
            return;
        }

        if (cvar_shadows_gpu_driven.GetValue() && Pass_ShadowMaps_GpuDriven())
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
        for (Entity* entity_light : GetViewLightEntities())
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

                    // Grass casts through screen-space depth only, including authored mesh blades.
                    if (material->GetProperty(MaterialProperty::IsGrassBlade) != 0.0f)
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
                    const bool alpha_tested   = material->IsAlphaTested();
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

                    const bool close_to_shadow      = draw_call.distance_squared < 100.0f * 100.0f;
                    const uint32_t lod_index_bias   = light->GetLightType() == LightType::Directional ? 1 : 0;
                    const uint32_t lod_index_shadow = clamp(render->GetLodIndex() + lod_index_bias, 0u, render->GetLodCount() - 1);
                    const uint32_t lod_index        = material->GetProperty(MaterialProperty::WindAnimation) > 0.0f
                        ? min(draw_call.lod_index + (light->GetLightType() == LightType::Directional ? 2u : 1u), render->GetLodCount() - 1)
                        : (close_to_shadow ? draw_call.lod_index : lod_index_shadow);

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
        const bool use_batches          = RHI_Device::IsSupportedShaderBaseInstance() && shaders_supported && argument_buffer && argument_count != 0 && m_cpu_indirect_draw_arg_count + argument_count <= renderer_max_cpu_indirect_draws;
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

        RHI_CommandList::BeginPass(pso.name);
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
                        m_pcb_pass_cpu.set(pass_depth_light::light_index, static_cast<uint32_t>(light->GetIndex()));
                        m_pcb_pass_cpu.set(pass_depth_light::array_index, static_cast<uint32_t>(slice.array_index));
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
                    RHI_Shader* vertex_shader   = use_batches ? multi_vertex_shader : GetShader(Renderer_Shader::depth_light_v);
                    RHI_Shader* pixel_shader    = material->IsAlphaTested() ? (use_batches ? multi_pixel_shader : GetShader(Renderer_Shader::depth_light_alpha_color_p)) : nullptr;
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
                    m_pcb_pass_cpu.set(pass_depth_light::has_albedo, material->HasTextureOfType(MaterialTextureType::Color));
                    m_pcb_pass_cpu.set(pass_depth_light::light_index, static_cast<uint32_t>(light->GetIndex()));
                    m_pcb_pass_cpu.set(pass_depth_light::array_index, static_cast<uint32_t>(slice.array_index));
                    RHI_CommandList::PushConstants(m_pcb_pass_cpu);
                    RHI_CommandList::SetCullMode(static_cast<RHI_CullMode>(material->GetProperty(MaterialProperty::CullMode)));
                    RHI_Buffer* instance_buffer = GeometryBuffer::GetInstanceBuffer() ? GeometryBuffer::GetInstanceBuffer() : GetBuffer(Renderer_Buffer::DummyInstance);
                    RHI_CommandList::SetBufferVertex(render->GetVertexBuffer(), instance_buffer);
                    RHI_CommandList::SetBufferIndex(render->GetIndexBuffer());

                    const bool close_to_shadow      = draw_call.distance_squared < 100.0f * 100.0f;
                    const uint32_t lod_index_bias   = light->GetLightType() == LightType::Directional ? 1 : 0;
                    const uint32_t lod_index_shadow = clamp(render->GetLodIndex() + lod_index_bias, 0u, render->GetLodCount() - 1);
                    const uint32_t lod_index        = material->GetProperty(MaterialProperty::WindAnimation) > 0.0f
                        ? min(draw_call.lod_index + (light->GetLightType() == LightType::Directional ? 2u : 1u), render->GetLodCount() - 1)
                        : (close_to_shadow ? draw_call.lod_index : lod_index_shadow);
                    if (render->HasInstancing() && !render->HasBoundingBoxOverride())
                    {
                        // A tile may contain thousands of trees outside this
                        // light slice. Cull instances before submitting vertices,
                        // retaining their original indices, alpha and mesh LOD.
                        const Vector3 camera = Renderer::view().frame.camera_position;
                        const float max_distance = render->GetMaxShadowDistance();
                        const bool wind = material->GetProperty(MaterialProperty::WindAnimation) > 0.0f;
                        const uint32_t end = draw_call.instance_index + draw_call.instance_count;
                        uint32_t run_start = draw_call.instance_index;
                        uint32_t run_count = 0;
                        uint32_t run_lod = lod_index;
                        Camera* view_camera = Renderer::GetViewCamera();
                        const float tan_half_fov = view_camera ? tan(view_camera->GetFovVerticalRad() * 0.5f) : 0.57735f;
                        const auto flush_run = [&]()
                        {
                            if (run_count == 0) return;
                            RHI_CommandList::DrawIndexed(render->GetIndexCount(run_lod), render->GetIndexOffset(run_lod),
                                render->GetVertexOffset(run_lod), render->GetGlobalInstanceOffset() + run_start, run_count);
                            run_count = 0;
                        };
                        vector<uint32_t> visible_instances;
                        for (const Render::InstanceBoundsGroup& group : render->GetInstanceBoundsGroups())
                        {
                            if (Vector3::DistanceSquared(camera, group.bounds.GetClosestPoint(camera)) > max_distance * max_distance ||
                                !light->IsBoundsInViewFrustum(group.bounds, slice.array_index))
                            {
                                continue;
                            }
                            for (uint32_t j = group.offset; j < group.offset + group.count; ++j)
                            {
                                const uint32_t instance = render->GetGroupedInstanceIndex(j);
                                if (instance < draw_call.instance_index || instance >= end) continue;
                                BoundingBox bounds = render->GetInstanceBounds(instance);
                                if (wind)
                                {
                                    const float padding = render->GetInstanceWindPadding(instance);
                                    const Vector3 extent(padding, padding, padding);
                                    bounds = BoundingBox(bounds.GetMin() - extent, bounds.GetMax() + extent);
                                }
                                if (Vector3::DistanceSquared(camera, bounds.GetClosestPoint(camera)) <= max_distance * max_distance &&
                                    light->IsBoundsInViewFrustum(bounds, slice.array_index))
                                {
                                    visible_instances.push_back(instance);
                                }
                            }
                        }
                        // Preserve the original submission order and contiguous instance runs.
                        sort(visible_instances.begin(), visible_instances.end());
                        for (const uint32_t instance : visible_instances)
                        {
                            // A terrain tile's LOD says nothing about an individual tree's coverage.
                            // Shadows need the silhouette, not the near-camera branch tessellation.
                            uint32_t instance_lod = lod_index;
                            if (wind && render->GetLodCount() > 1)
                            {
                                const BoundingBox bounds = render->GetInstanceBounds(instance);
                                const float radius = bounds.GetExtents().Length();
                                const float distance = max(Vector3::Distance(camera, bounds.GetCenter()), 0.1f);
                                const float coverage = radius / (distance * tan_half_fov);
                                instance_lod = coverage >= 0.4f ? 0u : coverage >= 0.2f ? 1u : coverage >= 0.1f ? 2u : coverage >= 0.05f ? 3u : 4u;
                                instance_lod = min(instance_lod + (light->GetLightType() == LightType::Directional ? 2u : 1u), render->GetLodCount() - 1);
                            }
                            if (run_count != 0 && (instance != run_start + run_count || instance_lod != run_lod)) flush_run();
                            if (run_count == 0)
                            {
                                run_start = instance;
                                run_lod = instance_lod;
                            }
                            ++run_count;
                        }
                        flush_run();
                    }
                    else
                    {
                        RHI_CommandList::DrawIndexed(render->GetIndexCount(lod_index), render->GetIndexOffset(lod_index), render->GetVertexOffset(lod_index), render->GetGlobalInstanceOffset() + draw_call.instance_index, draw_call.instance_count);
                    }
                };

                const vector<const Renderer_DrawCall*>& draws = use_batches ? slice.direct_draws : slice.visible_draws;
                for (const Renderer_DrawCall* draw_call : draws)
                {
                    draw_direct(*draw_call);
                }
            }
        }
        RHI_CommandList::EndPass();
    }

    bool Renderer::Pass_ShadowMaps_GpuDriven()
    {
        SP_ASSERT_MSG(!IsRayTracedShadowsActive(), "Shadow techniques are mutually exclusive: GPU raster shadow pass entered with RT shadows active");
        if (IsRayTracedShadowsActive())
        {
            return false;
        }

        RHI_Shader* shader_clear  = GetShader(Renderer_Shader::shadow_cull_clear_c);
        RHI_Shader* shader_cull   = GetShader(Renderer_Shader::shadow_cull_c);
        RHI_Shader* shader_args   = GetShader(Renderer_Shader::shadow_cull_args_c);
        RHI_Shader* shader_vertex = GetShader(Renderer_Shader::depth_light_gpu_v);
        RHI_Shader* shader_alpha  = GetShader(Renderer_Shader::depth_light_multi_draw_alpha_color_p);
        for (RHI_Shader* shader : { shader_clear, shader_cull, shader_args, shader_vertex, shader_alpha })
        {
            if (!shader || !shader->IsCompiled())
            {
                return false;
            }
        }

        // the raster finds its range of the visible list through firstInstance
        if (!RHI_Device::IsSupportedShaderBaseInstance())
        {
            return false;
        }

        // slices, the gpu culls every caster against each of them
        struct ShadowSlice
        {
            Light* light = nullptr;
            uint32_t array_index = 0;
            math::Rectangle rect;
        };
        vector<ShadowSlice> slices;
        vector<array<uint32_t, 4>> slice_data;
        for (Entity* entity_light : GetViewLightEntities())
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

                slices.push_back({ light, array_index, rect });
                const uint32_t is_directional = light->GetLightType() == LightType::Directional ? 1u : 0u;
                slice_data.push_back({ static_cast<uint32_t>(light->GetIndex()), array_index, is_directional, 0u });
            }
        }

        // casters, grouped by the state an indirect draw cannot change, each group is a contiguous range
        struct ShadowGroup
        {
            RHI_Buffer* vertex_buffer = nullptr;
            RHI_Buffer* index_buffer  = nullptr;
            RHI_CullMode cull_mode    = RHI_CullMode::Back;
            bool alpha_tested         = false;
            uint32_t first            = 0;
            vector<Sb_ShadowCaster> casters;
        };
        static vector<ShadowGroup> groups;
        static unordered_map<IndexedBatchKey, uint32_t, IndexedBatchKeyHash> group_lookup;
        groups.clear();
        group_lookup.clear();

        uint64_t instance_total = 0;
        if (!slices.empty())
        {
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

                // Grass casts through screen-space depth only, including authored mesh blades.
                if (material->GetProperty(MaterialProperty::IsGrassBlade) != 0.0f)
                {
                    continue;
                }

                RHI_Buffer* vertex_buffer = render->GetVertexBuffer();
                RHI_Buffer* index_buffer  = render->GetIndexBuffer();
                if (!vertex_buffer || !index_buffer || render->GetLodCount() == 0)
                {
                    continue;
                }

                const bool alpha_tested      = material->IsAlphaTested();
                const RHI_CullMode cull_mode = static_cast<RHI_CullMode>(material->GetProperty(MaterialProperty::CullMode));
                const IndexedBatchKey key    = { vertex_buffer, index_buffer, cull_mode, alpha_tested };
                auto [it, inserted]          = group_lookup.try_emplace(key, static_cast<uint32_t>(groups.size()));
                if (inserted)
                {
                    ShadowGroup& group  = groups.emplace_back();
                    group.vertex_buffer = vertex_buffer;
                    group.index_buffer  = index_buffer;
                    group.cull_mode     = cull_mode;
                    group.alpha_tested  = alpha_tested;
                }

                const uint32_t lod_count = render->GetLodCount();
                const bool wind = material->GetProperty(MaterialProperty::WindAnimation) > 0.0f;
                const bool per_instance = render->HasInstancing() && !render->HasBoundingBoxOverride();
                const bool per_instance_lod = wind && per_instance && lod_count > 1 && lod_count <= 7;
                const bool close_to_shadow = draw_call.distance_squared < 100.0f * 100.0f;
                const uint32_t lod_local = wind ? min(draw_call.lod_index + 1u, lod_count - 1) : draw_call.lod_index;
                const uint32_t lod_directional = wind ? min(draw_call.lod_index + 2u, lod_count - 1) :
                    (close_to_shadow ? lod_local : min(render->GetLodIndex() + 1, lod_count - 1));
                const BoundingBox& bounds = per_instance ? render->GetLodAabb(0) : render->GetBoundingBox();
                // Candidate LODs share bounds and transforms. The GPU accepts each instance
                // once per slice, choosing a coarser silhouette independently of the tile LOD.
                for (uint32_t lod = per_instance_lod ? 1u : 0u; lod < (per_instance_lod ? lod_count : 1u); ++lod)
                {
                    const uint32_t directional = per_instance_lod ? lod : lod_directional;
                    const uint32_t local = per_instance_lod ? lod : lod_local;
                    Sb_ShadowCaster& caster          = groups[it->second].casters.emplace_back();
                    caster.draw_data_index           = draw_call.draw_data_index;
                    caster.instance_offset           = render->HasInstancing() ? render->GetGlobalInstanceOffset() : 0u;
                    caster.instance_count            = draw_call.instance_count;
                    caster.flags                     = per_instance ? 0u : shadow_caster_flag_world_bounds;
                    caster.flags                    |= wind ? shadow_caster_flag_wind : 0u;
                    caster.flags                    |= per_instance_lod ? shadow_caster_flag_lod | (lod << shadow_caster_lod_shift) | (lod_count << shadow_caster_lod_count_shift) : 0u;
                    caster.index_count_directional   = render->GetIndexCount(directional);
                    caster.first_index_directional   = render->GetIndexOffset(directional);
                    caster.vertex_offset_directional = static_cast<int32_t>(render->GetVertexOffset(directional));
                    caster.index_count_local         = render->GetIndexCount(local);
                    caster.first_index_local         = render->GetIndexOffset(local);
                    caster.vertex_offset_local       = static_cast<int32_t>(render->GetVertexOffset(local));
                    caster.bounds_min                = bounds.GetMin();
                    caster.bounds_max                = bounds.GetMax();
                    caster.max_shadow_distance       = per_instance ? shadow_distance : 0.0f;
                }
                // LOD candidates are exclusive, so do not multiply visible-buffer capacity by the LOD count.
                instance_total += draw_call.instance_count;
            }
        }

        // flatten the groups and split every caster into 64 instance tasks
        static vector<Sb_ShadowCaster> casters;
        static vector<array<uint32_t, 2>> tasks;
        casters.clear();
        tasks.clear();
        for (ShadowGroup& group : groups)
        {
            group.first = static_cast<uint32_t>(casters.size());
            for (const Sb_ShadowCaster& caster : group.casters)
            {
                const uint32_t caster_index = static_cast<uint32_t>(casters.size());
                for (uint32_t instance = 0; instance < caster.instance_count; instance += shadow_cull_group_size)
                {
                    tasks.push_back({ caster_index, instance });
                }
                casters.push_back(caster);
            }
        }

        const uint32_t slice_count   = static_cast<uint32_t>(slices.size());
        const uint32_t caster_count  = static_cast<uint32_t>(casters.size());
        const uint32_t task_count    = static_cast<uint32_t>(tasks.size());
        const uint32_t pair_count    = slice_count * caster_count;
        const uint32_t visible_count = static_cast<uint32_t>(min<uint64_t>(instance_total * slice_count, shadow_visible_capacity_max));

        RHI_Buffer* buffer_args    = nullptr;
        RHI_Buffer* buffer_visible = nullptr;
        if (pair_count != 0)
        {
            RHI_Buffer* buffer_casters = EnsureBufferCapacity(Renderer_Buffer::ShadowCasters, sizeof(Sb_ShadowCaster), caster_count, "shadow_casters");
            RHI_Buffer* buffer_tasks   = EnsureBufferCapacity(Renderer_Buffer::ShadowTasks, sizeof(uint32_t) * 2, task_count, "shadow_tasks");
            RHI_Buffer* buffer_slices  = EnsureBufferCapacity(Renderer_Buffer::ShadowSlices, sizeof(uint32_t) * 4, slice_count, "shadow_slices");
            RHI_Buffer* buffer_counts  = EnsureBufferCapacity(Renderer_Buffer::ShadowCounts, sizeof(uint32_t), pair_count + 1, "shadow_counts");
            buffer_args                = EnsureBufferCapacity(Renderer_Buffer::ShadowArgs, sizeof(Sb_IndirectDrawArgs), pair_count, "shadow_args");
            buffer_visible             = EnsureBufferCapacity(Renderer_Buffer::ShadowVisible, sizeof(uint32_t) * 2, visible_count, "shadow_visible");

            RHI_CommandList::BeginPass("shadow_cull");
            {
                RHI_CommandList::UpdateBuffer(buffer_casters, 0, caster_count * sizeof(Sb_ShadowCaster), casters.data(), false);
                RHI_CommandList::UpdateBuffer(buffer_tasks, 0, task_count * sizeof(uint32_t) * 2, tasks.data(), false);
                RHI_CommandList::UpdateBuffer(buffer_slices, 0, slice_count * sizeof(uint32_t) * 4, slice_data.data(), false);

                m_pcb_pass_cpu = {};
                m_pcb_pass_cpu.set(pass_shadow_cull::slice_count, slice_count);
                m_pcb_pass_cpu.set(pass_shadow_cull::caster_count, caster_count);
                m_pcb_pass_cpu.set(pass_shadow_cull::task_count, task_count);
                m_pcb_pass_cpu.set(pass_shadow_cull::visible_capacity, buffer_visible->GetElementCount());

                auto bind = [&]()
                {
                    RHI_CommandList::SetBuffer("shadow_casters", buffer_casters);
                    RHI_CommandList::SetBuffer("shadow_tasks", buffer_tasks);
                    RHI_CommandList::SetBuffer("shadow_slices", buffer_slices);
                    RHI_CommandList::SetBuffer("shadow_counts", buffer_counts);
                    RHI_CommandList::SetBuffer("shadow_args", buffer_args);
                    RHI_CommandList::SetBuffer("shadow_visible", buffer_visible);
                };

                RHI_CommandList::SetShader(shader_clear);
                bind();
                RHI_CommandList::PushConstants(m_pcb_pass_cpu);
                RHI_CommandList::Dispatch((pair_count + 1 + 63) / 64, 1, 1);

                const uint32_t groups_x = min(task_count, INSTANCE_CULL_DISPATCH_WIDTH);
                const uint32_t groups_y = (task_count + INSTANCE_CULL_DISPATCH_WIDTH - 1) / INSTANCE_CULL_DISPATCH_WIDTH;
                RHI_CommandList::SetShader(shader_cull);
                bind();
                m_pcb_pass_cpu.set(pass_shadow_cull::write, false);
                RHI_CommandList::PushConstants(m_pcb_pass_cpu);
                RHI_CommandList::Dispatch(groups_x, groups_y, slice_count);

                RHI_CommandList::SetShader(shader_args);
                bind();
                RHI_CommandList::PushConstants(m_pcb_pass_cpu);
                RHI_CommandList::Dispatch((pair_count + 63) / 64, 1, 1);

                RHI_CommandList::SetShader(shader_cull);
                bind();
                m_pcb_pass_cpu.set(pass_shadow_cull::write, true);
                RHI_CommandList::PushConstants(m_pcb_pass_cpu);
                RHI_CommandList::Dispatch(groups_x, groups_y, slice_count);
            }
            RHI_CommandList::EndPass();
        }

        // the pass always runs so the atlas clears even when nothing casts
        RHI_PipelineState pso;
        pso.name                             = "shadow_maps";
        pso.shaders[RHI_Shader_Type::Vertex] = shader_vertex;
        pso.blend_state                      = GetBlendState(Renderer_BlendState::Off);
        pso.depth_stencil_state              = GetDepthStencilState(Renderer_DepthStencilState::ReadWrite);
        pso.clear_depth                      = 0.0f;
        pso.render_target_depth_texture      = GetRenderTarget(Renderer_RenderTarget::shadow_atlas);
        pso.rasterizer_state                 = GetRasterizerState(Renderer_RasterizerState::Light_directional);

        RHI_CommandList::BeginPass(pso.name);
        {
            RHI_CommandList::SetPipelineState(pso);

            const uint32_t arg_stride = static_cast<uint32_t>(sizeof(Sb_IndirectDrawArgs));
            for (uint32_t slice_index = 0; pair_count != 0 && slice_index < slice_count; slice_index++)
            {
                const ShadowSlice& slice = slices[slice_index];
                RHI_RasterizerState* rasterizer_state = slice.light->GetLightType() == LightType::Directional ? GetRasterizerState(Renderer_RasterizerState::Light_directional) : GetRasterizerState(Renderer_RasterizerState::Light_point_spot);
                RHI_Viewport viewport;
                viewport.x      = slice.rect.x;
                viewport.y      = slice.rect.y;
                viewport.width  = slice.rect.width;
                viewport.height = slice.rect.height;

                bool state_dirty = true;
                for (const ShadowGroup& group : groups)
                {
                    RHI_Shader* pixel_shader = group.alpha_tested ? shader_alpha : nullptr;
                    if (pso.shaders[RHI_Shader_Type::Pixel] != pixel_shader || pso.rasterizer_state != rasterizer_state)
                    {
                        pso.shaders[RHI_Shader_Type::Pixel] = pixel_shader;
                        pso.rasterizer_state                = rasterizer_state;
                        RHI_CommandList::SetPipelineState(pso);
                        state_dirty = true;
                    }

                    if (state_dirty)
                    {
                        RHI_CommandList::SetViewport(viewport);
                        RHI_CommandList::SetScissorRectangle(slice.rect);
                        RHI_CommandList::SetBuffer("shadow_visible", buffer_visible);
                        state_dirty = false;
                    }

                    m_pcb_pass_cpu.draw_index     = numeric_limits<uint32_t>::max();
                    m_pcb_pass_cpu.is_transparent = 0;
                    m_pcb_pass_cpu.set(pass_depth_light::light_index, static_cast<uint32_t>(slice.light->GetIndex()));
                    m_pcb_pass_cpu.set(pass_depth_light::array_index, slice.array_index);
                    RHI_CommandList::PushConstants(m_pcb_pass_cpu);
                    RHI_CommandList::SetCullMode(group.cull_mode);
                    RHI_CommandList::SetBufferVertex(group.vertex_buffer);
                    RHI_CommandList::SetBufferIndex(group.index_buffer);
                    RHI_CommandList::DrawIndexedIndirect(buffer_args, (slice_index * caster_count + group.first) * arg_stride, static_cast<uint32_t>(group.casters.size()));
                }
            }
        }
        RHI_CommandList::EndPass();

        return true;
    }

    void Renderer::Pass_HiZ_BuildFromDepth(RHI_Texture* tex_hiz)
    {
        // min depth downsample of the prepass depth, reverse z
        RHI_Texture* tex_depth = GetRenderTarget(Renderer_RenderTarget::gbuffer_depth);
        if (!tex_depth || !tex_hiz)
        {
            return;
        }

        Pass_Blit(tex_depth, tex_hiz, false);
        Pass_Downscale(tex_hiz, Renderer_DownsampleFilter::Min);
    }

    void Renderer::Pass_TessellatedCull(bool late)
    {
        static vector<Sb_TessellatedDraw> draws;
        static vector<uint32_t> lods; // index count, first index, vertex offset, unused
        RHI_Shader* shader = GetShader(Renderer_Shader::tessellated_cull_c);

        // the late cull reuses the draws the early cull uploaded
        if (late)
        {
            if (!m_tessellated_gpu_culled || m_tessellated_draws.empty())
            {
                return;
            }
        }
        else
        {
            m_tessellated_draws.clear();
            m_tessellated_gpu_culled = false;

            if (!cvar_tessellation_gpu_driven.GetValueAs<bool>() || !shader || !shader->IsCompiled())
            {
                return;
            }

            draws.clear();
            lods.clear();
        }

        for (uint32_t i = 0; i < (late ? 0u : m_draw_call_count); i++)
        {
            const Renderer_DrawCall& draw_call = m_draw_calls[i];
            Render* render                     = draw_call.render;
            Material* material                 = render->GetMaterial();
            if (!material || material->IsTransparent() || material->GetProperty(MaterialProperty::Tessellation) <= 0.0f || material->GetProperty(MaterialProperty::IsSkidMark) != 0.0f)
            {
                continue;
            }

            const BoundingBox& bounds = render->GetBoundingBox();
            Sb_TessellatedDraw& draw  = draws.emplace_back();
            draw.bounds_min           = bounds.GetMin();
            draw.bounds_max           = bounds.GetMax();
            const float max_distance  = render->GetMaxRenderDistance();
            draw.max_distance_squared = (max_distance > 0.0f && max_distance < numeric_limits<float>::max() * 0.5f) ? max_distance * max_distance : 0.0f;
            Entity* entity            = render->GetEntity();
            draw.flags                = (entity && entity->GetTimeSinceLastTransform() <= 0.1f) ? tessellated_flag_skip_hiz : 0u;
            draw.lod_offset           = static_cast<uint32_t>(lods.size() / 4);
            draw.lod_count            = max(min(render->GetLodCount(), 7u), 1u);
            draw.first_instance       = render->GetGlobalInstanceOffset() + draw_call.instance_index;
            draw.instance_count       = draw_call.instance_count;

            // Render::UpdateLodIndices measures one instance of an instanced tile, not the tile
            Vector3 extents = bounds.GetExtents();
            if (render->HasInstancing() && entity && !render->GetSceneData().instances.empty())
            {
                const Vector3 entity_scale = entity->GetScale();
                const Vector3 inst_scale   = render->GetInstance(0, false).GetScale();
                const Vector3 mesh_extents = render->GetLodAabb(0).GetExtents();
                extents = Vector3(
                    mesh_extents.x * abs(entity_scale.x * inst_scale.x),
                    mesh_extents.y * abs(entity_scale.y * inst_scale.y),
                    mesh_extents.z * abs(entity_scale.z * inst_scale.z)
                );
            }
            draw.lod_extents = extents;

            for (uint32_t lod = 0; lod < draw.lod_count; lod++)
            {
                const uint32_t source = render->GetIndexCount(lod) != 0 ? lod : draw_call.lod_index;
                lods.push_back(render->GetIndexCount(source));
                lods.push_back(render->GetIndexOffset(source));
                lods.push_back(render->GetVertexOffset(source));
                lods.push_back(0u);
            }

            m_tessellated_draws.push_back(i);
        }

        if (draws.empty())
        {
            m_tessellated_gpu_culled = true;
            return;
        }

        // the args hold the early half then the late half, each draw lands in at most one
        const uint32_t draw_count = static_cast<uint32_t>(draws.size());
        RHI_Buffer* buffer_draws  = EnsureBufferCapacity(Renderer_Buffer::TessellatedDraws, sizeof(Sb_TessellatedDraw), draw_count, "tessellated_draws");
        RHI_Buffer* buffer_lods   = EnsureBufferCapacity(Renderer_Buffer::TessellatedLods, sizeof(uint32_t) * 4, static_cast<uint32_t>(lods.size() / 4), "tessellated_lods");
        RHI_Buffer* buffer_args   = EnsureBufferCapacity(Renderer_Buffer::TessellatedArgs, sizeof(Sb_IndirectDrawArgs), draw_count * 2, "tessellated_args");
        RHI_Texture* tex_hiz      = GetRenderTarget(late ? Renderer_RenderTarget::gbuffer_depth_hiz_early : Renderer_RenderTarget::gbuffer_depth_hiz);
        const bool history_hiz    = m_pass_state.hiz_history_valid && cvar_hiz_occlusion.GetValueAs<bool>() && !m_is_hiz_suppressed;

        RHI_CommandList::BeginPass(late ? "tessellated_cull_late" : "tessellated_cull");
        {
            if (!late)
            {
                RHI_CommandList::UpdateBuffer(buffer_draws, 0, draw_count * sizeof(Sb_TessellatedDraw), draws.data(), false);
                RHI_CommandList::UpdateBuffer(buffer_lods, 0, lods.size() * sizeof(uint32_t), lods.data(), false);
            }

            RHI_CommandList::SetShader(shader);
            RHI_CommandList::SetTexture(Renderer_BindingsSrv::tex, tex_hiz);
            RHI_CommandList::SetBuffer("tessellated_draws", buffer_draws);
            RHI_CommandList::SetBuffer("tessellated_lods", buffer_lods);
            RHI_CommandList::SetBuffer("tessellated_args", buffer_args);

            m_pcb_pass_cpu = {};
            m_pcb_pass_cpu.set(pass_tessellated_cull::draw_count, draw_count);
            m_pcb_pass_cpu.set(pass_tessellated_cull::max_hiz_mip, static_cast<float>(tex_hiz->GetMipCount() - 1));
            m_pcb_pass_cpu.set(pass_tessellated_cull::hiz_depth_bias, clamp(cvar_hiz_depth_bias.GetValueAs<float>(), 0.0f, 1.0f));
            m_pcb_pass_cpu.set(pass_tessellated_cull::history_hiz, history_hiz);
            m_pcb_pass_cpu.set(pass_tessellated_cull::late, late);
            RHI_CommandList::PushConstants(m_pcb_pass_cpu);
            RHI_CommandList::Dispatch((draw_count + 63u) / 64u, 1, 1);
        }
        RHI_CommandList::EndPass();

        m_tessellated_gpu_culled = true;
    }

    void Renderer::Pass_IndirectCull(bool late)
    {
        if (m_indirect_draw_count == 0 || m_cull_task_count == 0)
        {
            return;
        }

        RHI_Texture* tex_hiz    = GetRenderTarget(late ? Renderer_RenderTarget::gbuffer_depth_hiz_early : Renderer_RenderTarget::gbuffer_depth_hiz);
        const float max_hiz_mip = static_cast<float>(tex_hiz->GetMipCount() - 1);
        const bool history_hiz  = m_pass_state.hiz_history_valid && cvar_hiz_occlusion.GetValueAs<bool>() && !m_is_hiz_suppressed;

        // the late cull runs this again, the lod table only needs to reach the gpu once a frame
        RHI_Buffer* buffer_draw_lods = EnsureBufferCapacity(Renderer_Buffer::DrawLods, sizeof(Sb_DrawLod), max(static_cast<uint32_t>(m_draw_lods.size()), 1u), "draw_lods");
        if (!m_draw_lods_uploaded && !m_draw_lods.empty())
        {
            RHI_CommandList::UpdateBuffer(buffer_draw_lods, 0, static_cast<uint32_t>(m_draw_lods.size() * sizeof(Sb_DrawLod)), m_draw_lods.data(), false);
        }
        m_draw_lods_uploaded = true;

        // the early survivors were consumed by the early meshlet cull, the late ones take their place
        if (late)
        {
            Sb_IndirectDispatchArgs instance_dispatch = {};
            RHI_CommandList::UpdateBuffer(GetBuffer(Renderer_Buffer::InstanceDispatchArgs), 0, sizeof(instance_dispatch), &instance_dispatch, false);
        }

        // phase a, per-instance distance + side-frustum + hi-z, survivors land in surviving_instances and bump instance_dispatch_args.group_count_x
        RHI_CommandList::BeginPass(late ? "instance_cull_late" : "instance_cull");
        {
            RHI_CommandList::SetShader(GetShader(Renderer_Shader::instance_cull_c));
            RHI_CommandList::SetBuffer("draw_lods", buffer_draw_lods);
            RHI_CommandList::SetBuffer("indirect_draw_data_rw", GetBuffer(Renderer_Buffer::IndirectDrawData));
            RHI_CommandList::SetBuffer("instance_visibility", EnsureBufferCapacity(Renderer_Buffer::InstanceVisibility, sizeof(uint32_t), m_cull_task_count * (INSTANCE_CULL_BATCH_SIZE / 32u), "instance_visibility"));

            RHI_CommandList::SetTexture(Renderer_BindingsSrv::tex, tex_hiz);

            RHI_CommandList::SetBuffer(Renderer_BindingsUav::indirect_draw_data, GetBuffer(Renderer_Buffer::IndirectDrawData));
            RHI_CommandList::SetBuffer(Renderer_BindingsUav::cull_tasks, GetBuffer(Renderer_Buffer::CullTasks));
            RHI_CommandList::SetBuffer(Renderer_BindingsUav::surviving_instances, GetBuffer(Renderer_Buffer::SurvivingInstances));
            RHI_CommandList::SetBuffer(Renderer_BindingsUav::instance_dispatch_args, GetBuffer(Renderer_Buffer::InstanceDispatchArgs));

            // survivors past max_instances are dropped
            m_pcb_pass_cpu.set(pass_instance_cull::hiz_depth_bias, clamp(cvar_hiz_depth_bias.GetValueAs<float>(), 0.0f, 1.0f));
            m_pcb_pass_cpu.set(pass_instance_cull::task_count, m_cull_task_count);
            m_pcb_pass_cpu.set(pass_instance_cull::max_hiz_mip, max_hiz_mip);
            m_pcb_pass_cpu.set(pass_instance_cull::max_instances, GetBuffer(Renderer_Buffer::SurvivingInstances)->GetElementCount());
            m_pcb_pass_cpu.set(pass_instance_cull::late, late);
            m_pcb_pass_cpu.set(pass_instance_cull::history_hiz, history_hiz);
            RHI_CommandList::PushConstants(m_pcb_pass_cpu);

            const uint32_t groups_x = min(m_cull_task_count, INSTANCE_CULL_DISPATCH_WIDTH);
            const uint32_t groups_y = (m_cull_task_count + INSTANCE_CULL_DISPATCH_WIDTH - 1u) / INSTANCE_CULL_DISPATCH_WIDTH;
            RHI_CommandList::Dispatch(groups_x, groups_y, 1);
        }
        RHI_CommandList::EndPass();

        Pass_IndirectCull_Meshlets(late);
    }

    void Renderer::Pass_IndirectCull_Meshlets(bool late)
    {
        if (!GeometryBuffer::GetMeshletBoundsBuffer() ||
            !GetBuffer(Renderer_Buffer::SurvivingInstances) ||
            !GetBuffer(Renderer_Buffer::MeshletInstances) ||
            !GetBuffer(Renderer_Buffer::TriangleDispatchArgs) ||
            !GetBuffer(Renderer_Buffer::IndirectDrawData))
        {
            return;
        }

        // the late cull replays the early history test on tex2 so it only appends what the early cull did not draw
        RHI_Texture* tex_history = GetRenderTarget(Renderer_RenderTarget::gbuffer_depth_hiz);
        RHI_Texture* tex_hiz     = late ? GetRenderTarget(Renderer_RenderTarget::gbuffer_depth_hiz_early) : tex_history;
        const float max_hiz_mip  = static_cast<float>(tex_hiz->GetMipCount() - 1);
        const bool history_hiz   = m_pass_state.hiz_history_valid && cvar_hiz_occlusion.GetValueAs<bool>() && !m_is_hiz_suppressed;

        // phase b, expand the meshlets of the surviving instances, per-meshlet cone + frustum + hi-z, one workgroup per survivor
        RHI_CommandList::BeginPass(late ? "indirect_cull_meshlet_late" : "indirect_cull_meshlet");
        {
            RHI_CommandList::SetShader(GetShader(Renderer_Shader::indirect_cull_c));

            RHI_CommandList::SetTexture(Renderer_BindingsSrv::tex, tex_hiz);
            RHI_CommandList::SetTexture(Renderer_BindingsSrv::tex2, tex_history);

            RHI_CommandList::SetBuffer(Renderer_BindingsUav::indirect_draw_data, GetBuffer(Renderer_Buffer::IndirectDrawData));
            RHI_CommandList::SetBuffer(Renderer_BindingsUav::meshlet_bounds, GeometryBuffer::GetMeshletBoundsBuffer());
            RHI_CommandList::SetBuffer(Renderer_BindingsUav::surviving_instances, GetBuffer(Renderer_Buffer::SurvivingInstances));
            RHI_CommandList::SetBuffer(Renderer_BindingsUav::meshlet_instances, GetBuffer(Renderer_Buffer::MeshletInstances));
            RHI_CommandList::SetBuffer(Renderer_BindingsUav::triangle_dispatch_args, GetBuffer(Renderer_Buffer::TriangleDispatchArgs));

            RHI_CommandList::SetBuffer(Renderer_BindingsUav::tree_wind_cache, GetBuffer(Renderer_Buffer::TreeWindCache));

            m_pcb_pass_cpu.set(pass_indirect_cull::hiz_depth_bias, clamp(cvar_hiz_depth_bias.GetValueAs<float>(), 0.0f, 1.0f));
            m_pcb_pass_cpu.set(pass_indirect_cull::max_hiz_mip, max_hiz_mip);
            m_pcb_pass_cpu.set(pass_indirect_cull::max_meshlet_instances, GetBuffer(Renderer_Buffer::MeshletInstances)->GetElementCount() / (use_mesh_shaders() ? 1u : 2u));
            m_pcb_pass_cpu.set(pass_indirect_cull::split_opaque_alpha, use_mesh_shaders());
            m_pcb_pass_cpu.set(pass_indirect_cull::wind_cache_limit, static_cast<uint32_t>(clamp(cvar_tree_wind_cache_entries.GetValueAs<float>(), 0.0f, static_cast<float>(GetBuffer(Renderer_Buffer::TreeWindCache)->GetElementCount()))));
            m_pcb_pass_cpu.set(pass_indirect_cull::late, late);
            m_pcb_pass_cpu.set(pass_indirect_cull::history_hiz, history_hiz);
            RHI_CommandList::PushConstants(m_pcb_pass_cpu);

            RHI_CommandList::DispatchIndirect(GetBuffer(Renderer_Buffer::InstanceDispatchArgs), 0);
        }
        RHI_CommandList::EndPass();

        // the late meshlets the late cull appended get their own dispatch args in slots 2 and 3
        if (late)
        {
            Pass_CullPhase(CULL_PHASE_MODE_LATE_MESHLETS);
        }

        // pass 2, per-triangle frustum + backface + sub-pixel cull, dispatched indirect with one workgroup per surviving meshlet
        // mesh shaders replace this stage, so skip it when they are the active draw path
        if (!use_mesh_shaders())
        {
            // The vertex-pulling fallback needs this storage; the mesh shader path never reads or writes it.
            FrameResource& frame = m_frame_resources[m_frame_resource_index];
            const bool reduced = cvar_debug_renderdoc.GetValue() || m_frame_resource_index >= renderer_draw_data_buffer_count;
            const uint32_t capacity = reduced ? 8 * 1024 * 1024 : renderer_max_visible_triangles;
            if (frame.visible_triangles->GetElementCount() < capacity)
            {
                frame.visible_triangles = make_shared<RHI_Buffer>(RHI_Buffer_Type::Storage, sizeof(uint32_t), capacity,
                    nullptr, false, (string("visible_triangles_") + to_string(m_frame_resource_index)).c_str());
                BindFrameResource(m_frame_resource_index);
            }
            RHI_CommandList::BeginPass(late ? "indirect_cull_triangle_late" : "indirect_cull_triangle");
            {
                RHI_CommandList::SetShader(GetShader(Renderer_Shader::indirect_cull_triangle_c));

                RHI_CommandList::SetBuffer(Renderer_BindingsUav::indirect_draw_args, GetBuffer(Renderer_Buffer::IndirectDrawArgs));
                RHI_CommandList::SetBuffer(Renderer_BindingsUav::indirect_draw_data, GetBuffer(Renderer_Buffer::IndirectDrawData));
                RHI_CommandList::SetBuffer(Renderer_BindingsUav::meshlet_bounds, GeometryBuffer::GetMeshletBoundsBuffer());
                RHI_CommandList::SetBuffer(Renderer_BindingsUav::meshlet_instances, GetBuffer(Renderer_Buffer::MeshletInstances));
                RHI_CommandList::SetBuffer(Renderer_BindingsUav::visible_triangles, GetBuffer(Renderer_Buffer::VisibleTriangles));
                RHI_CommandList::SetBuffer(Renderer_BindingsUav::cull_phase, GetBuffer(Renderer_Buffer::CullPhase));

                // survivors past the per half cap are dropped
                m_pcb_pass_cpu.set(pass_indirect_cull_triangle::max_meshlet_instances, GetBuffer(Renderer_Buffer::MeshletInstances)->GetElementCount());
                m_pcb_pass_cpu.set(pass_indirect_cull_triangle::region_cap, GetBuffer(Renderer_Buffer::VisibleTriangles)->GetElementCount() / 2);
                m_pcb_pass_cpu.set(pass_indirect_cull_triangle::late, late);
                RHI_CommandList::PushConstants(m_pcb_pass_cpu);

                RHI_CommandList::DispatchIndirect(GetBuffer(Renderer_Buffer::TriangleDispatchArgs), late ? 2u * static_cast<uint32_t>(sizeof(Sb_IndirectDispatchArgs)) : 0u);
            }
            RHI_CommandList::EndPass();

            if (late)
            {
                Pass_CullPhase(CULL_PHASE_MODE_LATE_TRIANGLES);
            }
        }

        // where the early survivors end, the late cull appends after it
        if (!late)
        {
            Pass_CullPhase(CULL_PHASE_MODE_SNAPSHOT);
        }
    }

    void Renderer::Pass_CullPhase(uint32_t mode)
    {
        RHI_CommandList::BeginPass("cull_phase");
        {
            RHI_CommandList::SetShader(GetShader(Renderer_Shader::cull_phase_c));
            RHI_CommandList::SetBuffer(Renderer_BindingsUav::cull_phase, GetBuffer(Renderer_Buffer::CullPhase));
            RHI_CommandList::SetBuffer(Renderer_BindingsUav::triangle_dispatch_args, GetBuffer(Renderer_Buffer::TriangleDispatchArgs));
            RHI_CommandList::SetBuffer(Renderer_BindingsUav::indirect_draw_args, GetBuffer(Renderer_Buffer::IndirectDrawArgs));
            m_pcb_pass_cpu.set(pass_cull_phase::mode, mode);
            RHI_CommandList::PushConstants(m_pcb_pass_cpu);
            RHI_CommandList::Dispatch(1, 1, 1);
        }
        RHI_CommandList::EndPass();
    }

    void Renderer::Pass_Cull_Early()
    {
        // test against last frame's complete depth, draw the survivors, then build the hi-z the late cull tests against
        m_pass_state.cull_late_active = false;

        RHI_CommandList::BeginPass("cull_early");
        Pass_IndirectCull(false);
        Pass_TessellatedCull(false);
        Pass_Depth_Prepass(false);
        if (cvar_hiz_occlusion.GetValueAs<bool>() && !m_is_hiz_suppressed)
        {
            Pass_HiZ_BuildFromDepth(GetRenderTarget(Renderer_RenderTarget::gbuffer_depth_hiz_early));
        }
        RHI_CommandList::EndPass();
    }

    void Renderer::Pass_Cull_Late()
    {
        // recover what last frame's depth wrongly rejected, then keep the complete depth as next frame's history
        if (!cvar_hiz_occlusion.GetValueAs<bool>() || m_is_hiz_suppressed)
        {
            m_pass_state.hiz_history_valid = false;
            return;
        }

        RHI_CommandList::BeginPass("cull_late");
        m_pass_state.cull_late_active = true;
        Pass_IndirectCull(true);
        Pass_TessellatedCull(true);
        Pass_Depth_Prepass(true);
        Pass_HiZ_BuildFromDepth(GetRenderTarget(Renderer_RenderTarget::gbuffer_depth_hiz));
        m_pass_state.hiz_history_valid = true;
        RHI_CommandList::EndPass();
    }

    void Renderer::Pass_Depth_Prepass(bool late)
    {
        if (!late)
        {
            log_mesh_path_diagnostics();
        }

        RHI_Texture* tex_depth = GetRenderTarget(Renderer_RenderTarget::gbuffer_depth);

        // new depth history must start at far so disocclusion fails closed, depth clears are graphics queue only
        if (!m_pass_state.depth_history_cleared)
        {
            if (RHI_Texture* depth_prev = GetRenderTarget(Renderer_RenderTarget::gbuffer_depth_previous))
            {
                RHI_CommandList::ClearTexture(depth_prev, Color::standard_white, 1.0f);
            }
            m_pass_state.depth_history_cleared = true;
        }

        bool is_wireframe                     = IsViewWireframe();
        bool xr_multiview                     = IsStereoView();
        RHI_RasterizerState* rasterizer_state = GetRasterizerState(Renderer_RasterizerState::Solid);
        rasterizer_state                      = is_wireframe ? GetRasterizerState(Renderer_RasterizerState::Wireframe) : rasterizer_state;

        RHI_CommandList::BeginPass(late ? "depth_prepass_late" : "depth_prepass");
        {
            // two draws over the split survivor list, opaque with no pixel shader for double-speed depth, alpha with the cutout ps
            // mesh path: one workgroup per opaque/alpha meshlet via DrawMeshTasksIndirect, vs path: DrawIndirect over visible triangles
            // the late pass draws only what the late cull appended, from args slots 2 and 3
            {
                const bool mesh_path           = use_mesh_shaders();
                const uint32_t arg_stride      = static_cast<uint32_t>(sizeof(Sb_IndirectDrawArgs));
                const uint32_t dispatch_stride = static_cast<uint32_t>(sizeof(Sb_IndirectDispatchArgs));
                const uint32_t slot_opaque     = late ? 2u : 0u;
                const uint32_t slot_alpha      = late ? 3u : 1u;

                RHI_PipelineState pso;
                pso.name                             = mesh_path ? "depth_prepass_mesh" : "depth_prepass_indirect";
                if (mesh_path)
                {
                    pso.shaders[RHI_Shader_Type::MeshShader] = GetShader(late ? Renderer_Shader::meshlet_mesh_depth_late_m : Renderer_Shader::meshlet_mesh_depth_m);
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

                // opaque half, no pixel shader, the early pass also clears the depth target for the whole prepass
                // the clear runs unconditionally so the transparent ocean still tests against a fresh depth buffer when no opaque geometry is visible
                pso.shaders[RHI_Shader_Type::Pixel]  = nullptr;
                pso.clear_depth                      = late ? rhi_depth_load : 0.0f;
                RHI_CommandList::BeginPass("depth_prepass_opaque");
                RHI_CommandList::SetPipelineState(pso);

                if (m_indirect_draw_count > 0)
                {
                    RHI_CommandList::SetCullMode(RHI_CullMode::None);
                    if (mesh_path)
                    {
                        bind_mesh_shader_geometry();
                        push_mesh_draw_constants(m_pcb_pass_cpu);
                        RHI_CommandList::DrawMeshTasksIndirect(GetBuffer(Renderer_Buffer::TriangleDispatchArgs), slot_opaque * dispatch_stride);
                    }
                    else
                    {
                        bind_visible_triangle_geometry();
                        m_pcb_pass_cpu.set(pass_visible_triangles::region_base, 0u);
                        m_pcb_pass_cpu.set(pass_visible_triangles::late, late);
                        RHI_CommandList::PushConstants(m_pcb_pass_cpu);
                        RHI_CommandList::DrawIndirect(GetBuffer(Renderer_Buffer::IndirectDrawArgs), slot_opaque * arg_stride);
                    }

                    // alpha-tested half, alpha-test pixel shader discards cutout texels, depth already cleared so load it
                    RHI_CommandList::EndPass();
                    RHI_CommandList::BeginPass("depth_prepass_alpha");
                    if (mesh_path)
                    {
                        pso.shaders[RHI_Shader_Type::MeshShader] = GetShader(late ? Renderer_Shader::meshlet_mesh_depth_alpha_late_m : Renderer_Shader::meshlet_mesh_depth_alpha_m);
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
                        // Alpha work is compacted separately, with the original capacity for each category.
                        RHI_CommandList::DrawMeshTasksIndirect(GetBuffer(Renderer_Buffer::TriangleDispatchArgs), slot_alpha * dispatch_stride);
                    }
                    else
                    {
                        bind_visible_triangle_geometry();
                        m_pcb_pass_cpu.set(pass_visible_triangles::region_base, GetBuffer(Renderer_Buffer::VisibleTriangles)->GetElementCount() / 2);
                        m_pcb_pass_cpu.set(pass_visible_triangles::late, late);
                        RHI_CommandList::PushConstants(m_pcb_pass_cpu);
                        RHI_CommandList::DrawIndirect(GetBuffer(Renderer_Buffer::IndirectDrawArgs), slot_alpha * arg_stride);
                    }
                }
                RHI_CommandList::EndPass();
            }

            // cpu-driven tessellated path (only tessellated still uses cpu draws, indirect path covers everything else)
            // the cpu fallback has no late half, the early pass drew all of it
            if (!late || m_tessellated_gpu_culled)
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

                // the gpu culled list holds every tessellated draw, its args carry the visibility and the lod
                const uint32_t tessellated_count = m_tessellated_gpu_culled ? static_cast<uint32_t>(m_tessellated_draws.size()) : m_draw_calls_prepass_count;
                const uint32_t args_base         = late ? tessellated_count : 0u;
                for (uint32_t i = 0; i < tessellated_count; i++)
                {
                    const Renderer_DrawCall& draw_call = m_tessellated_gpu_culled ? m_draw_calls[m_tessellated_draws[i]] : m_draw_calls_prepass[i];
                    Render* render                     = draw_call.render;
                    Material* material                 = render->GetMaterial();
                    if (!m_tessellated_gpu_culled)
                    {
                        if (!material || material->IsTransparent() || !draw_call.camera_visible)
                        {
                            continue;
                        }
                        if (material->GetProperty(MaterialProperty::Tessellation) <= 0.0f)
                        {
                            continue;
                        }
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
                    m_pcb_pass_cpu.set(pass_depth_prepass::has_albedo, has_color_texture);
                    RHI_CommandList::PushConstants(m_pcb_pass_cpu);

                    RHI_CullMode cull_mode = static_cast<RHI_CullMode>(material->GetProperty(MaterialProperty::CullMode));
                    cull_mode              = (pso.rasterizer_state->GetPolygonMode() == RHI_PolygonMode::Wireframe) ? RHI_CullMode::None : cull_mode;
                    RHI_CommandList::SetCullMode(cull_mode);
                    RHI_Buffer* instance_buffer = GeometryBuffer::GetInstanceBuffer() ? GeometryBuffer::GetInstanceBuffer() : GetBuffer(Renderer_Buffer::DummyInstance);
                    RHI_CommandList::SetBufferVertex(render->GetVertexBuffer(), instance_buffer);
                    RHI_CommandList::SetBufferIndex(render->GetIndexBuffer());

                    if (m_tessellated_gpu_culled)
                    {
                        RHI_CommandList::DrawIndexedIndirect(GetBuffer(Renderer_Buffer::TessellatedArgs), (args_base + i) * sizeof(Sb_IndirectDrawArgs));
                    }
                    else
                    {
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

        }
        RHI_CommandList::EndPass();
    }

    void Renderer::Pass_GBuffer_Indirect()
    {
        RHI_CommandList::BeginPass("g_buffer_indirect");

        const bool xr_multiview = IsStereoView();
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
        pso.rasterizer_state                 = IsViewWireframe() ? GetRasterizerState(Renderer_RasterizerState::Wireframe) : GetRasterizerState(Renderer_RasterizerState::Solid);
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
            GetRenderTarget(Renderer_RenderTarget::gbuffer_velocity),
            GetRenderTarget(Renderer_RenderTarget::gbuffer_emissive)
        );
        pso.SetDepthTarget(GetRenderTarget(Renderer_RenderTarget::gbuffer_depth));
        pso.is_multiview                     = xr_multiview;
        // the opaque draw clears the g-buffer, the alpha draw loads it so it does not wipe the opaque output
        pso.clear_color[0]                   = Color::standard_transparent;
        pso.clear_color[1]                   = Color::standard_transparent;
        pso.clear_color[2]                   = Color::standard_transparent;
        pso.clear_color[3]                   = Color::standard_transparent;
        pso.clear_color[4]                   = Color::standard_transparent;

        const uint32_t arg_stride = static_cast<uint32_t>(sizeof(Sb_IndirectDrawArgs));
        m_pcb_pass_cpu.is_transparent = 0;

        RHI_CommandList::BeginPass("g_buffer_indirect_opaque");
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
                // slots 0 and 1 span the early and the late survivors together
                bind_visible_triangle_geometry();
                m_pcb_pass_cpu.set(pass_visible_triangles::region_base, 0u);
                m_pcb_pass_cpu.set(pass_visible_triangles::late, false);
                RHI_CommandList::PushConstants(m_pcb_pass_cpu);
                RHI_CommandList::DrawIndirect(GetBuffer(Renderer_Buffer::IndirectDrawArgs), 0);
            }

            RHI_CommandList::EndPass();
            RHI_CommandList::BeginPass("g_buffer_indirect_alpha");
            // alpha-tested half, same equal-z pixel shader, loads the g-buffer so the opaque output survives
            pso.clear_color[0] = rhi_color_load;
            pso.clear_color[1] = rhi_color_load;
            pso.clear_color[2] = rhi_color_load;
            pso.clear_color[3] = rhi_color_load;
            pso.clear_color[4] = rhi_color_load;
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
                RHI_CommandList::DrawMeshTasksIndirect(GetBuffer(Renderer_Buffer::TriangleDispatchArgs), sizeof(Sb_IndirectDispatchArgs));
            }
            else
            {
                bind_visible_triangle_geometry();
                m_pcb_pass_cpu.set(pass_visible_triangles::region_base, GetBuffer(Renderer_Buffer::VisibleTriangles)->GetElementCount() / 2);
                m_pcb_pass_cpu.set(pass_visible_triangles::late, false);
                RHI_CommandList::PushConstants(m_pcb_pass_cpu);
                RHI_CommandList::DrawIndirect(GetBuffer(Renderer_Buffer::IndirectDrawArgs), arg_stride);
            }
        }

        RHI_CommandList::EndPass();
        RHI_CommandList::EndPass();
    }

    void Renderer::Pass_GBuffer_TessellatedAndTransparent(const bool is_transparent_pass)
    {
        RHI_CommandList::BeginPass(is_transparent_pass ? "g_buffer_transparent_draw" : "g_buffer_tessellated");

        const bool xr_multiview = IsStereoView();

        RHI_PipelineState pso;
        pso.name                             = is_transparent_pass ? "g_buffer_transparent" : "g_buffer_tessellated";
        pso.shaders[RHI_Shader_Type::Vertex] = GetShader(Renderer_Shader::gbuffer_v);
        pso.shaders[RHI_Shader_Type::Pixel]  = GetShader(Renderer_Shader::gbuffer_p);
        pso.blend_state                      = GetBlendState(Renderer_BlendState::Off);
        pso.rasterizer_state                 = IsViewWireframe() ? GetRasterizerState(Renderer_RasterizerState::Wireframe) : GetRasterizerState(Renderer_RasterizerState::Solid);
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
        pso.render_target_color_textures[4]  = GetRenderTarget(Renderer_RenderTarget::gbuffer_emissive);
        pso.render_target_depth_texture      = GetRenderTarget(Renderer_RenderTarget::gbuffer_depth);
        pso.is_multiview                     = xr_multiview;
        pso.clear_color[0]                   = rhi_color_load;
        pso.clear_color[1]                   = rhi_color_load;
        pso.clear_color[2]                   = rhi_color_load;
        pso.clear_color[3]                   = rhi_color_load;
        pso.clear_color[4]                   = rhi_color_load;

        bool pipeline_set = false;
        // gpu culled tessellated draws all stay in the list, their args carry the visibility and the lod
        const bool gpu_tessellated = !is_transparent_pass && m_tessellated_gpu_culled;
        const uint32_t draw_count  = gpu_tessellated ? static_cast<uint32_t>(m_tessellated_draws.size()) : m_draw_call_count;
        for (uint32_t i = 0; i < draw_count; i++)
        {
            const Renderer_DrawCall& draw_call = m_draw_calls[gpu_tessellated ? m_tessellated_draws[i] : i];
            Render* render                     = draw_call.render;
            Material* material                 = render->GetMaterial();
            if (!material || (!draw_call.camera_visible && !gpu_tessellated) || material->GetProperty(MaterialProperty::IsSkidMark) != 0.0f)
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

            RHI_CommandList::SetCullMode(IsViewWireframe() ? RHI_CullMode::None : static_cast<RHI_CullMode>(material->GetProperty(MaterialProperty::CullMode)));
            RHI_Buffer* instance_buffer = GeometryBuffer::GetInstanceBuffer() ? GeometryBuffer::GetInstanceBuffer() : GetBuffer(Renderer_Buffer::DummyInstance);
            RHI_CommandList::SetBufferVertex(render->GetVertexBuffer(), instance_buffer);
            RHI_CommandList::SetBufferIndex(render->GetIndexBuffer());

            if (gpu_tessellated)
            {
                // a draw lands in at most one of the early and late halves, the other holds zero instances
                RHI_CommandList::DrawIndexedIndirect(GetBuffer(Renderer_Buffer::TessellatedArgs), i * sizeof(Sb_IndirectDrawArgs));
                if (m_pass_state.cull_late_active)
                {
                    RHI_CommandList::DrawIndexedIndirect(GetBuffer(Renderer_Buffer::TessellatedArgs), (draw_count + i) * sizeof(Sb_IndirectDrawArgs));
                }
            }
            else
            {
                RHI_CommandList::DrawIndexed(
                    render->GetIndexCount(draw_call.lod_index),
                    render->GetIndexOffset(draw_call.lod_index),
                    render->GetVertexOffset(draw_call.lod_index),
                    render->GetGlobalInstanceOffset() + draw_call.instance_index,
                    draw_call.instance_count
                );
            }

            pso.clear_depth = rhi_depth_load;
        }

        RHI_CommandList::EndPass();
    }

    void Renderer::Pass_SkidMarks()
    {
        // Blend rubber into the receiver before lighting. Zero coverage is a true no-op,
        // and crossing ribbons never replace depth, normals, velocity or material IDs.
        RHI_PipelineState pso;
        pso.name = "skid_marks";
        pso.shaders[RHI_Shader_Type::Vertex] = GetShader(Renderer_Shader::gbuffer_v);
        pso.shaders[RHI_Shader_Type::Pixel] = GetShader(Renderer_Shader::skid_marks_p);
        pso.blend_state = GetBlendState(Renderer_BlendState::SurfaceAlpha);
        pso.rasterizer_state = GetRasterizerState(Renderer_RasterizerState::Solid);
        pso.depth_stencil_state = GetDepthStencilState(Renderer_DepthStencilState::ReadGreaterEqual);
        pso.primitive_topology = RHI_PrimitiveTopology::TriangleList;
        pso.render_target_color_textures[0] = GetRenderTarget(Renderer_RenderTarget::gbuffer_color);
        pso.render_target_depth_texture = GetRenderTarget(Renderer_RenderTarget::gbuffer_depth);
        pso.clear_color[0] = rhi_color_load;
        pso.clear_depth = rhi_depth_load;
        pso.resolution_scale = true;
        pso.is_multiview = IsStereoView();

        // This pass exists before any tire slips. Compile its pipeline during
        // those initial frames, rather than blocking the first braking draw.
        auto* vertex_shader = pso.shaders[RHI_Shader_Type::Vertex];
        auto* pixel_shader = pso.shaders[RHI_Shader_Type::Pixel];
        if (!vertex_shader || !vertex_shader->IsCompiled() || !pixel_shader || !pixel_shader->IsCompiled())
            return;
        pso.Prepare();
        static uint64_t warmed_hash = 0;
        if (warmed_hash != pso.GetHash())
        {
            RHI_Pipeline* pipeline = nullptr;
            RHI_DescriptorSetLayout* layout = nullptr;
            RHI_Device::GetOrCreatePipeline(pso, pipeline, layout);
            warmed_hash = pso.GetHash();
        }

        bool pipeline_set = false;
        for (uint32_t i = 0; i < m_draw_call_count; ++i)
        {
            const Renderer_DrawCall& draw = m_draw_calls[i];
            Render* render = draw.render;
            Material* material = render->GetMaterial();
            if (!material || !draw.camera_visible || material->GetProperty(MaterialProperty::IsSkidMark) == 0.0f)
                continue;

            if (!pipeline_set)
            {
                RHI_CommandList::BeginPass("skid_marks");
                RHI_CommandList::SetPipelineState(pso);
                RHI_CommandList::SetCullMode(RHI_CullMode::None);
                pipeline_set = true;
            }
            m_pcb_pass_cpu.draw_index = draw.draw_data_index;
            m_pcb_pass_cpu.material_index = material->GetIndex();
            m_pcb_pass_cpu.is_transparent = 0;
            RHI_CommandList::PushConstants(m_pcb_pass_cpu);
            RHI_Buffer* instances = GeometryBuffer::GetInstanceBuffer() ? GeometryBuffer::GetInstanceBuffer() : GetBuffer(Renderer_Buffer::DummyInstance);
            RHI_CommandList::SetBufferVertex(render->GetVertexBuffer(), instances);
            RHI_CommandList::SetBufferIndex(render->GetIndexBuffer());
            RHI_CommandList::DrawIndexed(render->GetIndexCount(draw.lod_index), render->GetIndexOffset(draw.lod_index),
                render->GetVertexOffset(draw.lod_index), render->GetGlobalInstanceOffset() + draw.instance_index, draw.instance_count);
        }
        if (pipeline_set)
            RHI_CommandList::EndPass();
    }

    void Renderer::Pass_GBuffer(const bool is_transparent_pass, const bool submit_before_scatter)
    {
        RHI_CommandList::BeginPass(is_transparent_pass ? "g_buffer_transparent" : "g_buffer");
        {
            if (!is_transparent_pass)
            {
                Pass_GBuffer_Indirect();
                if (submit_before_scatter)
                {
                    // the meshlet g-buffer never reads procedural scatter, submitting it here lets it overlap the
                    // populate compute, only the grass draw and what follows waits for the scatter queue
                    RHI_CommandList::EndPass();
                    RHI_Device::Submit(RHI_Frame_List::Graphics, nullptr, false);
                    RHI_Device::Bind(RHI_Frame_List::Graphics);
                    RHI_CommandList::BeginPass("g_buffer_scatter");
                }
                // procedural grass runs after the indirect path, the draw call binds its own pipeline that reads grass_instances directly
                Pass_Grass_Draw();
            }

            Pass_GBuffer_TessellatedAndTransparent(is_transparent_pass);

            if (!is_transparent_pass)
                Pass_SkidMarks();

            if (!is_transparent_pass)
            {
                // opaque depth blit moved here from the prepass, all opaque geometry including grass has rasterized so the
                // opaque output carries grass occlusion, batch b consumers run after phase 1 so this write is visible to them
                RHI_CommandList::BeginPass("g_buffer_depth_blit");
                RHI_Texture* tex_depth        = GetRenderTarget(Renderer_RenderTarget::gbuffer_depth);
                RHI_Texture* tex_depth_output = GetRenderTarget(Renderer_RenderTarget::gbuffer_depth_opaque_output);
                RHI_CommandList::Blit(tex_depth, tex_depth_output, false, Renderer::GetResolutionScale());
                RHI_CommandList::EndPass();
            }

        }
        RHI_CommandList::EndPass();

        if (!is_transparent_pass && !IsSecondaryViewActive())
        {
            SP_PROFILE_CPU_START("gbuffer_transform_history");
            Entity::CommitTransformHistory();
            SP_PROFILE_CPU_END();
        }
    }

    void Renderer::Pass_Grass_Populate()
    {
        // fills the per-slot per-lod sections of grass_instances around the camera, then bakes
        // instance_count into the indirect args, slot 0 is grass and the higher slots are micro detail

        // a preview does not draw grass, refilling around its camera would only clobber the primary rings
        if (IsSecondaryViewActive())
        {
            return;
        }

        // the far grass carpet follows slot 0, it ends where the last ring ends and grows under the
        // same gates, the terrain reads it through the frame buffer
        m_pass_state.grass_far = {};
        {
            const PassState::GpuScatterSlot& grass = m_pass_state.gpu_scatter[0];
            if (gpu_scatter_ready(grass) && grass.prop_mask && grass.material &&
                grass.material->GetProperty(MaterialProperty::IsGrassBlade) != 0.0f)
            {
                uint32_t last_lod = 0;
                for (uint32_t lod = 0; lod < renderer_max_gpu_scatter_lods; lod++)
                {
                    if (grass.params.cell_size_m[lod] > 0.0f && grass.params.ring_radii_m[lod] > 0.0f)
                    {
                        last_lod = lod;
                    }
                }

                // mirrors the outer fade in grass_populate.hlsl
                const float ring_end         = gpu_scatter_ring_radius(grass, last_lod);
                const float inner_radius     = last_lod > 0 ? min(gpu_scatter_ring_radius(grass, last_lod - 1), ring_end) : 0.0f;
                const float outer_transition = max(grass.params.cell_size_m[last_lod] * 10.0f, ring_end * 0.42f);
                const float outer_start      = max(inner_radius, ring_end - outer_transition);
                const float patch_size       = max(grass.params.patch_size_m, 0.0f);
                const float biome_min        = grass.params.biome_min_weight < 0.0f ? -1.0f : grass.params.biome_min_weight;

                auto& far       = m_pass_state.grass_far;
                far.prop_mask   = grass.prop_mask;
                far.tint        = Vector4(grass.material->GetProperty(MaterialProperty::ColorR), grass.material->GetProperty(MaterialProperty::ColorG), grass.material->GetProperty(MaterialProperty::ColorB), 1.0f);
                far.fade        = Vector4(grass.params.ring_radii_m[0], outer_start, ring_end, grass.params.patch_invert ? -patch_size : patch_size);
                far.gate        = Vector4(biome_min, cosf(grass.params.max_slope_deg * (math::pi / 180.0f)), static_cast<float>(grass.params.ground_mask), static_cast<float>(min(grass.params.mask_channel, 3u)));
                far.patch       = Vector4(grass.params.patch_coverage, grass.params.patch_edge, 0.0f, 0.0f);
            }
        }

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
        Camera* camera = Renderer::GetViewCamera();
        if (!camera || !camera->GetEntity())
        {
            return;
        }

        Terrain* road_terrain = Terrain::FindActive();
        if (!road_terrain) return;
        RHI_Buffer* road_exclusions = road_terrain->GetRoadExclusionBuffer();

        static_assert(renderer_max_gpu_scatter_args == 9 && renderer_max_gpu_scatter_slots == 3);
        // per slot lod parameters, the protected vehicle region, then the cell window of every ring
        std::array<Vector4, renderer_max_gpu_scatter_slots + 1 + renderer_max_gpu_scatter_args> detail_parameters{};
        for (uint32_t slot = 0; slot < renderer_max_gpu_scatter_slots; ++slot)
        {
            const auto& state = m_pass_state.gpu_scatter[slot];
            if (gpu_scatter_ready(state) && state.material && state.mesh->GetObjectName() == "grass_blade" &&
                state.mesh->GetLodCount(0) == 3 && !Xr::IsSessionRunning() &&
                state.material->GetProperty(MaterialProperty::IsGrassBlade) != 0.0f &&
                state.material->GetProperty(MaterialProperty::IsWater) == 0.0f &&
                state.material->GetProperty(MaterialProperty::IsSkidMark) == 0.0f)
            {
                const auto& bounds = state.mesh->GetSubMesh(0).lods[0].aabb;
                detail_parameters[slot] = Vector4(bounds.GetSize().y,
                    clamp(cvar_grass_lod_pixels.GetValue(), 0.0f, 2.0f), 0.0f, 0.0f);
                if (detail_parameters[slot].y == 0.0f)
                    detail_parameters[slot].x = 0.0f;
            }
            // z names the impostor ring plus one, zero when the slot draws blades everywhere
            if (gpu_scatter_card_ring(state, gpu_scatter_card_lod))
            {
                detail_parameters[slot].z = static_cast<float>(gpu_scatter_card_lod + 1);
                detail_parameters[slot].w = gpu_scatter_card_thin();
            }
        }
        const auto& detail = GetSurfaceInteraction();
        if (detail.id)
            detail_parameters[renderer_max_gpu_scatter_slots] = Vector4(detail.center, 32.0f);

        // only the cells the view pyramid can reach get threads, the shader adds the window origin to
        // its thread id so every (cell, blade) keeps the hash and the placement it had on the full grid
        std::array<std::array<uint32_t, 4>, renderer_max_gpu_scatter_args> cell_windows{};
        {
            const Vector3 eye              = Renderer::view().frame.camera_position;
            const bool perspective         = fabsf(Renderer::view().frame.projection.m33) < 0.5f;
            const bool use_windows         = perspective && !Xr::IsSessionRunning();
            const float tan_half_h         = 1.0f / max(fabsf(Renderer::view().frame.projection.m00), 1e-4f);
            const float tan_half_v         = 1.0f / max(fabsf(Renderer::view().frame.projection.m11), 1e-4f);
            const float tan_half_min       = min(tan_half_h, tan_half_v);
            const float sin_half_min       = tan_half_min / sqrtf(1.0f + tan_half_min * tan_half_min);
            for (uint32_t slot = 0; slot < renderer_max_gpu_scatter_slots; slot++)
            {
                const PassState::GpuScatterSlot& state = m_pass_state.gpu_scatter[slot];
                if (!gpu_scatter_ready(state))
                {
                    continue;
                }

                // the shader culls a sphere around each instance, the planes pushed out by its radius meet
                // behind the eye, so the margin grows as the field of view narrows
                float root_radius = 0.0f;
                if (state.mesh->GetSubMeshCount() > 0)
                {
                    for (const auto& mesh_lod : state.mesh->GetSubMesh(0).lods)
                    {
                        root_radius = max(root_radius, mesh_lod.aabb.GetCenter().Length() + mesh_lod.aabb.GetExtents().Length());
                    }
                }
                const float largest_scale = clamp(max(state.params.size_min, state.params.size_max), 0.01f, 100.0f);
                const float cull_radius   = max(1.5f, root_radius) * largest_scale * 1.05f;
                const float cull_height   = 0.5f * largest_scale;

                for (uint32_t lod = 0; lod < renderer_max_gpu_scatter_lods; lod++)
                {
                    const float cell_size   = state.params.cell_size_m[lod];
                    const float ring_radius = gpu_scatter_ring_radius(state, lod);
                    if (cell_size <= 0.0f || ring_radius <= 0.0f)
                    {
                        continue;
                    }

                    const uint32_t cells_per_axis = 2u * static_cast<uint32_t>(ceilf(ring_radius / cell_size)) + 2u;
                    std::array<uint32_t, 4>& window = cell_windows[renderer_gpu_scatter_arg_index(slot, lod)];
                    window = { 0u, 0u, cells_per_axis, cells_per_axis };
                    if (!use_windows)
                    {
                        continue;
                    }

                    const bool card_ring   = gpu_scatter_card_ring(state, lod);
                    const float card_scale = card_ring ? gpu_scatter_card_scale(ring_radius) : 1.0f;
                    const float card_reach = card_ring ? cell_size * gpu_scatter_card_cells * 0.5f * card_scale : 0.0f;
                    const float margin     = (cull_radius * card_scale + card_reach) / max(sin_half_min, 0.05f) + 2.0f * cell_size;
                    const float reach  = (static_cast<float>(cells_per_axis / 2u) + 1.0f) * cell_size;
                    const float y_min  = max(state.params.height_min, eye.y - 4000.0f) - cull_radius;
                    const float y_max  = min(state.params.height_max, eye.y + 4000.0f) + cull_height + cull_radius;
                    Vector2 bounds_min;
                    Vector2 bounds_max;
                    const bool visible = y_min <= y_max && gpu_scatter_frustum_xz_bounds(
                        Renderer::view().frame.view_projection_inverted,
                        eye,
                        Vector3(eye.x - reach - margin, y_min, eye.z - reach - margin),
                        Vector3(eye.x + reach + margin, y_max, eye.z + reach + margin),
                        bounds_min,
                        bounds_max
                    );
                    if (!visible)
                    {
                        window = { 0u, 0u, 0u, 0u };
                        continue;
                    }

                    // same cell indexing as the shader, one spare cell per side absorbs cpu/gpu rounding
                    const int half_cells  = static_cast<int>(cells_per_axis / 2u);
                    const int anchor_x    = static_cast<int>(floorf(eye.x / cell_size));
                    const int anchor_z    = static_cast<int>(floorf(eye.z / cell_size));
                    const int last        = static_cast<int>(cells_per_axis) - 1;
                    const int x0          = clamp(static_cast<int>(floorf((bounds_min.x - margin) / cell_size)) - anchor_x + half_cells - 1, 0, last);
                    const int x1          = clamp(static_cast<int>(floorf((bounds_max.x + margin) / cell_size)) - anchor_x + half_cells + 1, 0, last);
                    const int z0          = clamp(static_cast<int>(floorf((bounds_min.y - margin) / cell_size)) - anchor_z + half_cells - 1, 0, last);
                    const int z1          = clamp(static_cast<int>(floorf((bounds_max.y + margin) / cell_size)) - anchor_z + half_cells + 1, 0, last);
                    window = { static_cast<uint32_t>(x0), static_cast<uint32_t>(z0), static_cast<uint32_t>(x1 - x0 + 1), static_cast<uint32_t>(z1 - z0 + 1) };
                }
            }

            for (uint32_t arg = 0; arg < renderer_max_gpu_scatter_args; arg++)
            {
                detail_parameters[renderer_max_gpu_scatter_slots + 1 + arg] = Vector4(static_cast<float>(cell_windows[arg][0]), static_cast<float>(cell_windows[arg][1]), 0.0f, 0.0f);
            }
        }

        RHI_Buffer* detail_buffer = GetBuffer(Renderer_Buffer::GrassLodParameters);
        RHI_CommandList::UpdateBuffer(detail_buffer, 0, sizeof(detail_parameters), detail_parameters.data(), false);
        RHI_CommandList::BeginPass("gpu_scatter_populate");
        {
            RHI_Buffer* buf_instances = GetBuffer(Renderer_Buffer::GrassInstances);
            RHI_Buffer* buf_count     = GetBuffer(Renderer_Buffer::GrassCount);
            RHI_Buffer* buf_args      = GetBuffer(Renderer_Buffer::GrassIndirectArgs);

            const bool hiz_active        = cvar_hiz_occlusion.GetValueAs<bool>() && !m_is_hiz_suppressed;
            RHI_Texture* tex_scatter_hiz = !hiz_active
                ? GetStandardTexture(Renderer_StandardTexture::Black)
                : GetRenderTarget(m_pass_state.cull_late_active ? Renderer_RenderTarget::gbuffer_depth_hiz : Renderer_RenderTarget::gbuffer_depth_hiz_early);

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
                std::array<Sb_IndirectDrawArgs, renderer_max_gpu_scatter_lods> coarse_args = state.indirect_args_static;
                coarse_args[0] = state.indirect_args_static[1];
                RHI_CommandList::UpdateBuffer(buf_args,
                    (renderer_max_gpu_scatter_args + renderer_gpu_scatter_arg_index(slot, 0)) * sizeof(Sb_IndirectDrawArgs),
                    sizeof(coarse_args), coarse_args.data(), false);
                state.args_baked = true;
            }

            // clear every counter on the gpu timeline, a mapped cpu memcpy races the in flight previous frame and drops a frame of scatter
            uint32_t zero_counts[renderer_max_gpu_scatter_args * 3] = {};
            RHI_CommandList::UpdateBuffer(buf_count, 0, sizeof(zero_counts), &zero_counts[0], false);

            // the terrain frame is shared, every slot scatters onto the same surface
            Vector3 terrain_offset;
            Vector4 terrain_mapping_live;
            get_grass_terrain_frame_state(terrain_offset, terrain_mapping_live);
            const float terrain_entity_y = terrain_offset.y;

            // populate dispatches, one per slot per lod ring, each fills its own range of grass_instances
            RHI_CommandList::SetShader(GetShader(Renderer_Shader::grass_populate_c));
            RHI_CommandList::SetBuffer("grass_lod_parameters", detail_buffer);
            RHI_CommandList::SetBuffer("terrain_road_exclusions", road_exclusions);

            for (uint32_t slot = 0; slot < renderer_max_gpu_scatter_slots; slot++)
            {
                const PassState::GpuScatterSlot& state = m_pass_state.gpu_scatter[slot];
                if (!gpu_scatter_ready(state))
                {
                    continue;
                }

                RHI_CommandList::SetBuffer(Renderer_BindingsUav::grass_instances, buf_instances);
                RHI_CommandList::SetBuffer(Renderer_BindingsUav::grass_count, buf_count);
                // the populate shader samples the terrain heightmap through the tex slot
                RHI_CommandList::SetTexture(Renderer_BindingsSrv::tex, state.heightmap);
                // hi-z on tex2 drives the per-instance frustum + occlusion cull, this frame's early depth when populate runs before
                // the late cull, the complete depth after it, black (far) culls nothing when occlusion culling is off
                RHI_CommandList::SetTexture(Renderer_BindingsSrv::tex2, tex_scatter_hiz);
                // biome prop mask on tex3, the slot picks the channel, black means nothing is suitable
                RHI_CommandList::SetTexture(Renderer_BindingsSrv::tex3, state.prop_mask ?
                        state.prop_mask :
                        GetStandardTexture(Renderer_StandardTexture::Black));

                const float max_slope_cos = cosf(state.params.max_slope_deg * (math::pi / 180.0f));
                // the floor can never sit above the ceiling, the shader divides by the gap between them
                const float min_slope_cos = cosf(
                    std::min(state.params.min_slope_deg, state.params.max_slope_deg) * (math::pi / 180.0f)
                );
                // a negative weight is the slot saying it wants no biome gate at all, which is what micro
                // detail asks for. a slot that does want one and has no mask fails closed instead of
                // filling the world
                const float biome_min = state.params.biome_min_weight < 0.0f ?
                    -1.0f :
                    (state.prop_mask ? state.params.biome_min_weight : 2.0f);

                // the seating offset rides along with the terrain entity y, the populate shader adds
                // that single float to every sampled height so no push constant slot is needed
                const float seat_y = terrain_entity_y + state.params.surface_offset;

                Vector4 terrain_mapping = terrain_mapping_live;
                if (terrain_mapping.z == 0.0f && terrain_mapping.w == 0.0f)
                {
                    terrain_mapping = state.params.terrain_world_mapping;
                    terrain_mapping.x += terrain_offset.x;
                    terrain_mapping.y += terrain_offset.z;
                }

                // the populate shader has no push constant floats left, so the slot, the mask channel,
                // the two ends of the size range and the lean travel in the bits of draw_index
                const uint32_t mask_channel = std::min(state.params.mask_channel, 3u);
                const uint32_t scale_min    = pack_instance_scale(std::min(state.params.size_min, state.params.size_max));
                const uint32_t scale_max    = pack_instance_scale(std::max(state.params.size_min, state.params.size_max));
                // four bits left, enough for the lean, one degree of resolution is beyond what a chip
                // a few centimetres across can show
                const uint32_t tilt         = static_cast<uint32_t>(
                    clamp(state.params.tilt_deg, 0.0f, 45.0f) * (15.0f / 45.0f) + 0.5f
                );

                // the sign of the size carries the inversion, a slot that inverts takes the ground the
                // slot it mirrors left bare, so the two have to be authored at the same size to interlock
                const float patch_size  = max(state.params.patch_size_m, 0.0f);
                const float patch_push  = state.params.patch_invert ? -patch_size : patch_size;
                const float patch_boost = gpu_scatter_patch_boost(patch_size, state.params.patch_coverage);

                // the same argument applies to the part of the circle the camera cannot see, and the
                // two multiply, this has to track grass_max_boost in grass_populate.hlsl
                const float total_boost = std::min(
                    patch_boost * gpu_scatter_frustum_boost(camera->GetFovHorizontalRad()),
                    gpu_scatter_max_boost
                );

                for (uint32_t lod = 0; lod < renderer_max_gpu_scatter_lods; lod++)
                {
                    const float cell_size   = state.params.cell_size_m[lod];
                    const float ring_radius = gpu_scatter_ring_radius(state, lod);
                    const bool card_ring    = gpu_scatter_card_ring(state, lod);
                    if (cell_size <= 0.0f || ring_radius <= 0.0f)
                    {
                        continue;
                    }

                    const float inner_radius = lod == 0 ?
                        0.0f :
                        std::min(
                            gpu_scatter_ring_radius(state, lod - 1),
                            ring_radius
                        );
                    // grass_instances is partitioned by slot and lod, the base is the cumulative prefix
                    // sum of every cap before it so each ring writes into its own contiguous range
                    const uint32_t lod_base   = renderer_gpu_scatter_base(slot, lod);
                    const uint32_t lod_cap    = gpu_scatter_slot_cap(state, slot, lod);

                    // heightmap is r32 local y, material_index bitcast is the entity y plus the layer's
                    // seating offset
                    // is_transparent bitcast carries biome_min_weight, negative disables the mask gate
                    m_pcb_pass_cpu.is_transparent = *reinterpret_cast<const uint32_t*>(&biome_min);
                    m_pcb_pass_cpu.draw_index     = lod                 |
                                                    (slot         << 4)  |
                                                    (mask_channel << 8)  |
                                                    (scale_min    << 12) |
                                                    (scale_max    << 20) |
                                                    (tilt         << 28);
                    m_pcb_pass_cpu.material_index = *reinterpret_cast<const uint32_t*>(&seat_y);
                    m_pcb_pass_cpu.set(pass_grass_populate::cell_size, cell_size);
                    m_pcb_pass_cpu.set(pass_grass_populate::ring_radius, ring_radius);
                    m_pcb_pass_cpu.set(pass_grass_populate::lod_base, lod_base);
                    m_pcb_pass_cpu.set(pass_grass_populate::max_instances_per_lod, lod_cap);
                    m_pcb_pass_cpu.set(pass_grass_populate::height_min, state.params.height_min);
                    m_pcb_pass_cpu.set(pass_grass_populate::height_max, state.params.height_max);
                    m_pcb_pass_cpu.set(pass_grass_populate::max_slope_cos, max_slope_cos);
                    m_pcb_pass_cpu.set(pass_grass_populate::inner_radius, inner_radius);
                    m_pcb_pass_cpu.set(pass_grass_populate::terrain_mapping, terrain_mapping);
                    m_pcb_pass_cpu.set(pass_grass_populate::patch_size, patch_push);
                    m_pcb_pass_cpu.set(pass_grass_populate::patch_coverage, state.params.patch_coverage);
                    m_pcb_pass_cpu.set(pass_grass_populate::patch_edge, state.params.patch_edge);
                    m_pcb_pass_cpu.set(pass_grass_populate::ground_mask, state.params.ground_mask);
                    m_pcb_pass_cpu.set(pass_grass_populate::min_slope_cos, min_slope_cos);
                    m_pcb_pass_cpu.set(pass_grass_populate::slope_bias, state.params.slope_bias);
                    m_pcb_pass_cpu.set(pass_grass_populate::height_fade, state.params.height_fade);
                    m_pcb_pass_cpu.set(pass_grass_populate::root_radius, 0.0f);
                    if (cvar_grass_specialized.GetValue() && state.material &&
                        state.material->GetProperty(MaterialProperty::IsGrassBlade) != 0.0f &&
                        state.material->GetProperty(MaterialProperty::IsWater) == 0.0f &&
                        state.material->GetProperty(MaterialProperty::IsSkidMark) == 0.0f)
                    {
                        // Every grass deformation preserves distance from the root, including
                        // tire and chassis bending. Bound any rotation of the actual mesh;
                        // the shader applies the maximum packed instance scale separately.
                        const auto& lods = state.mesh->GetSubMesh(0).lods;
                        const auto& bounds = lods[lod < lods.size() ? lod : 0u].aabb;
                        m_pcb_pass_cpu.set(pass_grass_populate::root_radius, bounds.GetCenter().Length() + bounds.GetExtents().Length() + 0.001f);
                    }
                    // a card spans more than a cell, the cull sphere has to hold all of it
                    if (card_ring)
                    {
                        const auto& bounds = state.mesh->GetSubMesh(0).lods[0].aabb;
                        m_pcb_pass_cpu.set(pass_grass_populate::root_radius, cell_size * gpu_scatter_card_cells * 0.5f + bounds.GetSize().y);
                    }
                    RHI_CommandList::PushConstants(m_pcb_pass_cpu);

                    // one cell per thread, dispatch z carries the instance index inside the cell, the
                    // shader recomputes instances_per_cell so both formulas must match
                    const uint32_t cells_per_axis =
                        2u *
                        static_cast<uint32_t>(
                            ceilf(ring_radius / cell_size)
                        ) +
                        2u;
                    const std::array<uint32_t, 4>& window = cell_windows[renderer_gpu_scatter_arg_index(slot, lod)];
                    const uint32_t groups_x = (min(window[2], cells_per_axis) + 7u) / 8u;
                    const uint32_t groups_z = (min(window[3], cells_per_axis) + 7u) / 8u;
                    if (groups_x == 0 || groups_z == 0)
                    {
                        continue;
                    }
                    const float ring_area = math::pi * (
                        (ring_radius * ring_radius) -
                        (inner_radius * inner_radius)
                    );
                    const float cells_in_ring =
                        ring_area /
                        (cell_size * cell_size);
                    // mirrors grass_populate.hlsl, the fraction it drops here it carries as a keep
                    // probability instead, so this only decides how many threads a cell gets
                    const float per_cell = static_cast<float>(lod_cap) * gpu_scatter_fill_margin *
                                           total_boost /
                                           std::max(cells_in_ring, 1.0f);
                    const uint32_t blades_per_cell = card_ring ? 1u : std::max(
                        1u,
                        static_cast<uint32_t>(std::ceil(per_cell))
                    );
                    RHI_CommandList::Dispatch(groups_x, groups_z, blades_per_cell);
                }
            }

            // args build, reads grass_count and writes the instance_count of this slot's three entries
            {
                RHI_CommandList::SetShader(
                    GetShader(Renderer_Shader::grass_indirect_args_c),
                    "grass_indirect_args"
                );

                RHI_CommandList::SetBuffer(Renderer_BindingsUav::grass_count, buf_count);
                RHI_CommandList::SetBuffer(Renderer_BindingsUav::grass_indirect_args, buf_args);

                // the args shader clamps each atomic counter against its own lod cap, draw_index is
                // the first args entry of the slot
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
                    for (uint32_t lod = 0; lod < renderer_max_gpu_scatter_lods; lod++)
                    {
                        m_pcb_pass_cpu.set(pass_grass_indirect_args::lod_caps + lod, gpu_scatter_slot_cap(state, slot, lod));
                    }
                    m_pcb_pass_cpu.set(pass_grass_indirect_args::lod_count, renderer_max_gpu_scatter_lods);
                    RHI_CommandList::PushConstants(m_pcb_pass_cpu);

                    RHI_CommandList::Dispatch(1, 1, 1);
                }
            }
        }
        RHI_CommandList::EndPass();
    }

    void Renderer::Pass_Grass_Interaction()
    {
        if (IsSecondaryViewActive())
            return;

        auto& history = m_pass_state.grass_interaction;
        const bool paused = Engine::IsFlagSet(EngineMode::Paused);
        if (!IsGpuScatterEnabled() || (!Engine::IsFlagSet(EngineMode::Playing) && !paused))
        {
            history.valid = false;
            history.wheel_valid.fill(false);
            return;
        }

        const auto& interaction = GetSurfaceInteraction();
        RHI_Shader* shader = GetShader(Renderer_Shader::grass_interaction_c);
        if (!interaction.id || !shader || !shader->IsCompiled())
        {
            history.valid = false;
            history.wheel_valid.fill(false);
            return;
        }

        if (!history.fields[0])
        {
            for (uint32_t i = 0; i < 2; ++i)
                history.fields[i] = make_shared<RHI_Texture>(RHI_Texture_Type::Type2D,
                    grass_interaction_resolution, grass_interaction_resolution, 1, 1,
                    RHI_Format::R32G32B32A32_Float, RHI_Texture_Srv | RHI_Texture_Uav | RHI_Texture_ClearBlit,
                    i == 0 ? "grass_tracks_a" : "grass_tracks_b");
            history.contacts = make_shared<RHI_Buffer>(RHI_Buffer_Type::Storage,
                sizeof(GrassWheelContact), surface_contact_max, nullptr, false, "grass_wheel_contacts");
        }

        const Vector3 center = interaction.center;
        const float cell = grass_interaction_size / grass_interaction_resolution;
        const Vector2 origin(floorf(center.x / cell) * cell - grass_interaction_size * 0.5f,
                             floorf(center.z / cell) * cell - grass_interaction_size * 0.5f);
        if (history.vehicle_id != interaction.id)
        {
            history.valid = false;
            history.wheel_valid.fill(false);
            history.vehicle_id = interaction.id;
        }

        history.body_data[1] = history.body_data[0];
        if (!paused || !history.valid)
        {
            history.body_data[0] = interaction.body;
        }
        if (!history.valid)
            history.body_data[1] = history.body_data[0];

        const float frame_dt = static_cast<float>(Timer::GetDeltaTimeSec());
        const float dt = paused ? 0.0f : min(frame_dt, 0.1f);
        array<GrassWheelContact, surface_contact_max> contacts{};
        uint32_t count = 0;
        for (uint32_t i = 0; i < surface_contact_max; ++i)
        {
            const auto& sample = interaction.contacts[i];
            if (paused || !sample.valid) { history.wheel_valid[i] = false; continue; }
            const Vector3 position = sample.position;
            const Vector3 normal = sample.normal;
            const Vector3 velocity = sample.velocity;
            const Vector3 direction = sample.direction;
            Vector3 start = history.wheel_positions[i];
            const float distance = Vector3::Distance(start, position);
            if (!history.valid || !history.wheel_valid[i] || frame_dt > 0.25f ||
                distance > min(12.0f, max(1.0f, velocity.Length() * dt * 2.0f + 0.5f)) ||
                abs(Vector3::Dot(position - start, normal)) > 0.5f)
                start = position;

            const float width = sample.width;
            if (!isfinite(width) || width <= 0.0f)
                continue;
            auto& contact = contacts[count++];
            contact.start_width = Vector4(start.x, start.y, start.z, width * 0.5f);
            contact.end_length = Vector4(position.x, position.y, position.z, 0.22f);
            contact.direction_pressure = Vector4(direction.x, direction.z, sample.pressure, 0.0f);
            contact.normal = Vector4(normal.x, normal.y, normal.z, 0.0f);
            history.wheel_positions[i] = position;
            history.wheel_valid[i] = true;
        }

        const uint32_t previous = history.current;
        const uint32_t current = 1 - previous;
        history.previous_valid = history.valid;
        history.origins[current] = origin;
        RHI_CommandList::BeginPass("grass_interaction");
        {
            if (!history.valid)
                RHI_CommandList::ClearTexture(history.fields[previous].get(), Color(0, 0, 0, 0));
            if (count)
                RHI_CommandList::UpdateBuffer(history.contacts.get(), 0, count * sizeof(GrassWheelContact), contacts.data(), false);
            RHI_CommandList::SetShader(shader);
            RHI_CommandList::SetBuffer("grass_wheel_contacts", history.contacts.get());
            RHI_CommandList::SetTexture(Renderer_BindingsSrv::tex, history.fields[previous].get());
            RHI_CommandList::SetTexture(Renderer_BindingsUav::tex, history.fields[current].get());
            m_pcb_pass_cpu = {};
            m_pcb_pass_cpu.set(pass_grass_interaction::origin, origin);
            m_pcb_pass_cpu.set(pass_grass_interaction::cell_size, cell);
            m_pcb_pass_cpu.set(pass_grass_interaction::history_valid, history.valid);
            m_pcb_pass_cpu.set(pass_grass_interaction::origin_previous, history.origins[previous]);
            m_pcb_pass_cpu.set(pass_grass_interaction::contact_count, count);
            m_pcb_pass_cpu.set(pass_grass_interaction::delta_time, dt);
            m_pcb_pass_cpu.set(pass_grass_interaction::track_center, Vector2(center.x, center.z));
            m_pcb_pass_cpu.set(pass_grass_interaction::track_radius, clamp(cvar_grass_track_radius.GetValue(), 1.0f, grass_interaction_size * 0.5f - 12.0f));
            m_pcb_pass_cpu.set(pass_grass_interaction::track_recovery, max(cvar_grass_track_recovery.GetValue(), 0.001f));
            RHI_CommandList::PushConstants(m_pcb_pass_cpu);
            RHI_CommandList::Dispatch(history.fields[current].get());
        }
        RHI_CommandList::EndPass();
        history.current = current;
        history.valid = true;
    }

    void Renderer::UpdateFrameCb_VegetationInteraction()
    {
        // the field belongs to the primary view, a preview keeps sampling the last one
        auto& state = m_pass_state.vegetation_interaction;
        if (IsSecondaryViewActive())
        {
            Renderer::view().frame.vegetation_field          = state.mapping;
            Renderer::view().frame.vegetation_field_previous = state.mapping_previous;
            return;
        }

        state.dispatch = false;
        const auto& interaction = GetSurfaceInteraction();
        const bool simulating = Engine::IsFlagSet(EngineMode::Playing) || Engine::IsFlagSet(EngineMode::Paused);
        RHI_Shader* shader = GetShader(Renderer_Shader::vegetation_interaction_c);
        if (!simulating || !interaction.id || !shader || !shader->IsCompiled())
        {
            state.valid            = false;
            state.mapping          = Vector4::Zero;
            state.mapping_previous = Vector4::Zero;
            Renderer::view().frame.vegetation_field          = state.mapping;
            Renderer::view().frame.vegetation_field_previous = state.mapping_previous;
            return;
        }

        if (!state.fields[0])
        {
            for (uint32_t i = 0; i < 2; ++i)
            {
                state.fields[i] = make_shared<RHI_Texture>(RHI_Texture_Type::Type2D,
                    vegetation_interaction_resolution, vegetation_interaction_resolution, 1, 1,
                    RHI_Format::R16G16B16A16_Float, RHI_Texture_Srv | RHI_Texture_Uav | RHI_Texture_ClearBlit,
                    i == 0 ? "vegetation_interaction_a" : "vegetation_interaction_b");
            }
            state.interactors = make_shared<RHI_Buffer>(RHI_Buffer_Type::Storage,
                sizeof(VegetationInteractorGpu), surface_interactor_max, nullptr, false, "vegetation_interactors");
        }

        const float cell = vegetation_interaction_size / vegetation_interaction_resolution;
        const Vector3 center = interaction.center;
        const Vector2 origin(floorf(center.x / cell) * cell - vegetation_interaction_size * 0.5f,
                             floorf(center.z / cell) * cell - vegetation_interaction_size * 0.5f);

        const bool history_valid = state.valid;
        state.current            = 1 - state.current;
        state.origins[state.current] = origin;
        state.mapping_previous   = history_valid ? state.mapping : Vector4::Zero;
        state.mapping            = Vector4(origin.x, origin.y, 1.0f / vegetation_interaction_size, 1.0f);
        state.dispatch           = true;
        state.valid              = history_valid; // becomes true once this frame's field is written

        Renderer::view().frame.vegetation_field          = state.mapping;
        Renderer::view().frame.vegetation_field_previous = state.mapping_previous;
    }

    void Renderer::Pass_Vegetation_Interaction()
    {
        auto& state = m_pass_state.vegetation_interaction;
        if (!state.dispatch)
            return;
        state.dispatch = false;

        RHI_Shader* shader = GetShader(Renderer_Shader::vegetation_interaction_c);
        if (!shader || !shader->IsCompiled())
            return;

        const auto& interaction = GetSurfaceInteraction();
        array<VegetationInteractorGpu, surface_interactor_max> interactors{};
        const uint32_t count = min(interaction.interactor_count, surface_interactor_max);
        for (uint32_t i = 0; i < count; ++i)
        {
            const SurfaceInteractor& source = interaction.interactors[i];
            // the field is horizontal, a body's footprint is its box seen from above
            Vector3 right(source.right.x, 0.0f, source.right.z);
            if (right.LengthSquared() < 0.01f)
                right = Vector3::Right;
            right.Normalize();
            const Vector3 forward(-right.z, 0.0f, right.x);
            interactors[i].center_radius = Vector4(source.center.x, source.center.y, source.center.z, source.radius);
            interactors[i].right         = Vector4(right.x, right.y, right.z, source.extents.x);
            interactors[i].forward       = Vector4(forward.x, forward.y, forward.z, source.extents.z);
            interactors[i].velocity      = Vector4(source.velocity.x, source.velocity.y, source.velocity.z, 0.0f);
        }

        const bool paused   = Engine::IsFlagSet(EngineMode::Paused);
        const float dt      = paused ? 0.0f : min(static_cast<float>(Timer::GetDeltaTimeSec()), 0.1f);
        const uint32_t current  = state.current;
        const uint32_t previous = 1 - current;
        const float cell        = vegetation_interaction_size / vegetation_interaction_resolution;

        state.writing = true;
        RHI_CommandList::BeginPass("vegetation_interaction");
        {
            if (!state.valid)
                RHI_CommandList::ClearTexture(state.fields[previous].get(), Color(0, 0, 0, 0));
            if (count)
                RHI_CommandList::UpdateBuffer(state.interactors.get(), 0, count * sizeof(VegetationInteractorGpu), interactors.data(), false);
            RHI_CommandList::SetShader(shader);
            RHI_CommandList::SetBuffer("vegetation_interactors", state.interactors.get());
            RHI_CommandList::SetTexture(Renderer_BindingsSrv::tex, state.fields[previous].get());
            RHI_CommandList::SetTexture(Renderer_BindingsUav::tex, state.fields[current].get());
            m_pcb_pass_cpu = {};
            m_pcb_pass_cpu.set(pass_vegetation_interaction::origin, state.origins[current]);
            m_pcb_pass_cpu.set(pass_vegetation_interaction::cell_size, cell);
            m_pcb_pass_cpu.set(pass_vegetation_interaction::history_valid, state.valid);
            m_pcb_pass_cpu.set(pass_vegetation_interaction::origin_previous, state.origins[previous]);
            m_pcb_pass_cpu.set(pass_vegetation_interaction::interactor_count, count);
            m_pcb_pass_cpu.set(pass_vegetation_interaction::delta_time, dt);
            m_pcb_pass_cpu.set(pass_vegetation_interaction::frequency, max(cvar_vegetation_frequency.GetValue(), 0.05f));
            m_pcb_pass_cpu.set(pass_vegetation_interaction::damping, clamp(cvar_vegetation_damping.GetValue(), 0.0f, 4.0f));
            RHI_CommandList::PushConstants(m_pcb_pass_cpu);
            RHI_CommandList::Dispatch(state.fields[current].get());
        }
        RHI_CommandList::EndPass();
        state.writing = false;
        state.valid   = true;
    }

    void Renderer::Pass_Grass_Draw()
    {
        // gpu scatter raster, one DrawIndexedIndirect per slot per lod ring, runs once inside the
        // g-buffer pass, shares the geometry stage render pass and sets its own vertex shader that
        // reads grass_instances

        // the instances were populated around the primary camera and belong to no preview subtree
        if (IsSecondaryViewActive())
        {
            return;
        }

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
        const bool xr_multiview = IsStereoView();

        RHI_PipelineState pso;
        pso.name                             = "grass_gbuffer";
        pso.shaders[RHI_Shader_Type::Vertex] = GetShader(Renderer_Shader::grass_gbuffer_v);
        pso.shaders[RHI_Shader_Type::Pixel]  = GetShader(Renderer_Shader::gbuffer_p);
        pso.blend_state                      = GetBlendState(Renderer_BlendState::Off);
        pso.rasterizer_state                 = IsViewWireframe() ? GetRasterizerState(Renderer_RasterizerState::Wireframe) : GetRasterizerState(Renderer_RasterizerState::Solid);
        pso.is_multiview                     = xr_multiview;
        pso.resolution_scale                 = true;
        // grass is the only geometry rasterized a single time, it owns its depth here so it writes and tests against the opaque geometry
        pso.depth_stencil_state              = GetDepthStencilState(Renderer_DepthStencilState::ReadWrite);
        pso.render_target_color_textures[0]  = GetRenderTarget(Renderer_RenderTarget::gbuffer_color);
        pso.render_target_color_textures[1]  = GetRenderTarget(Renderer_RenderTarget::gbuffer_normal);
        pso.render_target_color_textures[2]  = GetRenderTarget(Renderer_RenderTarget::gbuffer_material);
        pso.render_target_color_textures[3]  = GetRenderTarget(Renderer_RenderTarget::gbuffer_velocity);
        pso.render_target_color_textures[4]  = GetRenderTarget(Renderer_RenderTarget::gbuffer_emissive);
        pso.render_target_depth_texture      = GetRenderTarget(Renderer_RenderTarget::gbuffer_depth);
        pso.clear_color[0]                   = rhi_color_load;
        pso.clear_color[1]                   = rhi_color_load;
        pso.clear_color[2]                   = rhi_color_load;
        pso.clear_color[3]                   = rhi_color_load;
        pso.clear_color[4]                   = rhi_color_load;
        pso.clear_depth                      = rhi_depth_load;

        RHI_CommandList::BeginPass("g_buffer_grass");

        // grass blades are double sided and a stone chip is closed, but both are cheap enough that one
        // raster state for every slot is not worth a second pipeline

        // the scatter vs never reads the per-instance stream, it is bound to the global instance buffer only to keep the vertex layout uniform
        RHI_Buffer* buf_instances     = GetBuffer(Renderer_Buffer::GrassInstances);
        RHI_Buffer* buf_args          = GetBuffer(Renderer_Buffer::GrassIndirectArgs);
        RHI_Buffer* binding1_instance = GeometryBuffer::GetInstanceBuffer() ? GeometryBuffer::GetInstanceBuffer() : GetBuffer(Renderer_Buffer::DummyInstance);

        auto& tracks = m_pass_state.grass_interaction;
        static_assert(sizeof(tracks.body_data[0]) == (4 + 6 + 6 * 48) * sizeof(Vector4));
        if (!tracks.bodies)
            tracks.bodies = make_shared<RHI_Buffer>(RHI_Buffer_Type::Storage,
                static_cast<uint32_t>(sizeof(Vector4)), static_cast<uint32_t>(sizeof(tracks.body_data) / sizeof(Vector4)), nullptr, false, "grass_bodies");
        RHI_CommandList::UpdateBuffer(tracks.bodies.get(), 0, sizeof(tracks.body_data), tracks.body_data.data(), false);
        RHI_Texture* fallback = GetStandardTexture(Renderer_StandardTexture::Black);

        // A horizontal circle enclosing both chassis poses rejects distant blades
        // before the vertex shader loads the hull frame. Each blade expands it by
        // its own reach, retaining the existing detailed contact test nearby.
        const auto& body_now = tracks.body_data[0];
        const auto& body_before = tracks.body_data[1];
        const Vector2 body_center((body_now.center.x + body_before.center.x) * 0.5f,
                                  (body_now.center.z + body_before.center.z) * 0.5f);
        const auto body_radius = [&](const auto& body)
        {
            const float dx = body.center.x - body_center.x;
            const float dz = body.center.z - body_center.y;
            return sqrtf(dx * dx + dz * dz) +
                Vector3(body.right.w, body.up.w, body.forward.w).Length();
        };
        const float contact_radius = max(body_radius(body_now), body_radius(body_before)) + 0.001f;

        RHI_Shader* grass_vertex = GetShader(Renderer_Shader::grass_blade_v);
        RHI_Shader* grass_pixel = GetShader(Renderer_Shader::grass_gbuffer_p);

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

            for (uint32_t lod = 0; lod < renderer_max_gpu_scatter_lods; lod++)
            {
                const uint32_t lod_base = renderer_gpu_scatter_base(slot, lod);
                const bool grass_specialized = cvar_grass_specialized.GetValue() && !xr_multiview &&
                    grass_vertex && grass_vertex->IsCompiled() && grass_pixel && grass_pixel->IsCompiled() &&
                    state.params.uv_patch == 0.0f &&
                    state.material->GetProperty(MaterialProperty::IsGrassBlade) != 0.0f &&
                    state.material->GetProperty(MaterialProperty::IsTerrain) == 0.0f &&
                    state.material->GetProperty(MaterialProperty::IsWater) == 0.0f &&
                    state.material->GetProperty(MaterialProperty::IsFlower) == 0.0f &&
                    state.material->GetProperty(MaterialProperty::IsSkidMark) == 0.0f;
                RHI_Shader* shader_vertex = grass_specialized ? grass_vertex : GetShader(Renderer_Shader::grass_gbuffer_v);
                RHI_Shader* shader_pixel  = grass_specialized ? grass_pixel : GetShader(Renderer_Shader::gbuffer_p);
                if (!shader_vertex || !shader_vertex->IsCompiled() || !shader_pixel || !shader_pixel->IsCompiled())
                {
                    continue;
                }
                pso.shaders[RHI_Shader_Type::Vertex] = shader_vertex;
                pso.shaders[RHI_Shader_Type::Pixel]  = shader_pixel;
                RHI_CommandList::SetPipelineState(pso);
                RHI_CommandList::SetCullMode(RHI_CullMode::None);
                RHI_CommandList::SetBuffer(Renderer_BindingsUav::grass_instances, buf_instances);
                RHI_CommandList::SetBuffer("grass_bodies", tracks.bodies.get());
                RHI_CommandList::SetTexture(Renderer_BindingsSrv::tex,
                    tracks.valid ? tracks.fields[tracks.current].get() : fallback);
                RHI_CommandList::SetTexture(Renderer_BindingsSrv::tex2,
                    tracks.valid ? tracks.fields[1 - tracks.current].get() : fallback);
                RHI_CommandList::SetBufferVertex(mesh->GetVertexBuffer(), binding1_instance);
                RHI_CommandList::SetBufferIndex(mesh->GetIndexBuffer());

                m_pcb_pass_cpu.draw_index     = 0;
                m_pcb_pass_cpu.is_transparent = 0;
                m_pcb_pass_cpu.material_index = state.material->GetIndex();
                m_pcb_pass_cpu.set(pass_grass_draw::uv_patch, state.params.uv_patch);
                m_pcb_pass_cpu.set(pass_grass_draw::reverse_count, 0u);
                m_pcb_pass_cpu.set(pass_grass_draw::lod_base, lod_base);
                const bool card_ring = grass_specialized && gpu_scatter_card_ring(state, lod);
                m_pcb_pass_cpu.set(pass_grass_draw::card_width, card_ring ? state.params.cell_size_m[lod] * gpu_scatter_card_cells : 0.0f);
                m_pcb_pass_cpu.set(pass_grass_draw::card_thin, card_ring ? gpu_scatter_card_thin() : 0.0f);
                for (uint32_t frame = 0; frame < 2; ++frame)
                {
                    const Vector2 origin = tracks.origins[frame == 0 ? tracks.current : 1 - tracks.current];
                    const bool valid     = tracks.valid && (frame == 0 || tracks.previous_valid);
                    const uint32_t slot_tracks = frame == 0 ? pass_grass_draw::tracks : pass_grass_draw::tracks_previous;
                    m_pcb_pass_cpu.set(slot_tracks, Vector4(origin.x, origin.y, 1.0f / grass_interaction_size, valid ? 1.0f : 0.0f));
                }
                m_pcb_pass_cpu.set(pass_grass_draw::body, Vector4(tracks.valid ? 1.0f : 0.0f, body_center.x, body_center.y, contact_radius));
                RHI_CommandList::PushConstants(m_pcb_pass_cpu);

                // an empty ring bakes instance_count 0 into the args so the gpu skips it at near-zero cost
                RHI_CommandList::DrawIndexedIndirect(buf_args, renderer_gpu_scatter_arg_index(slot, lod) * arg_stride);
                if (lod == 0)
                {
                    m_pcb_pass_cpu.set(pass_grass_draw::reverse_count, gpu_scatter_lod_cap(slot, lod, state.params.density));
                    RHI_CommandList::PushConstants(m_pcb_pass_cpu);
                    RHI_CommandList::DrawIndexedIndirect(buf_args,
                        (renderer_max_gpu_scatter_args + renderer_gpu_scatter_arg_index(slot, lod)) * arg_stride);
                }
            }
        }

        RHI_CommandList::EndPass();
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

        bool xr_multiview = IsStereoView();

        RHI_CommandList::BeginPass("meshlet_visualize");
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

            RHI_CommandList::SetBuffer(Renderer_BindingsUav::indirect_draw_data, GetBuffer(Renderer_Buffer::IndirectDrawData));
            RHI_CommandList::SetBuffer(Renderer_BindingsUav::meshlet_instances, GetBuffer(Renderer_Buffer::MeshletInstances));
            RHI_CommandList::SetBuffer(Renderer_BindingsUav::visible_triangles, GetBuffer(Renderer_Buffer::VisibleTriangles));
            RHI_CommandList::SetBuffer(Renderer_BindingsUav::meshlet_bounds, GeometryBuffer::GetMeshletBoundsBuffer());
            RHI_CommandList::SetBuffer(Renderer_BindingsUav::cull_phase, GetBuffer(Renderer_Buffer::CullPhase));

            // wireframe shows both faces so rear edges of thin meshlets stay visible, solid mode leaves culling to the triangle cull pass
            RHI_CommandList::SetCullMode(RHI_CullMode::None);

            // draw the opaque half then the alpha half
            const uint32_t arg_stride = static_cast<uint32_t>(sizeof(Sb_IndirectDrawArgs));
            m_pcb_pass_cpu.set(pass_meshlet_visualize::color_by_draw_id, color_by_draw_id);
            m_pcb_pass_cpu.set(pass_visible_triangles::late, false);

            m_pcb_pass_cpu.set(pass_visible_triangles::region_base, 0u);
            RHI_CommandList::PushConstants(m_pcb_pass_cpu);
            RHI_CommandList::DrawIndirect(GetBuffer(Renderer_Buffer::IndirectDrawArgs), 0);

            m_pcb_pass_cpu.set(pass_visible_triangles::region_base, GetBuffer(Renderer_Buffer::VisibleTriangles)->GetElementCount() / 2);
            RHI_CommandList::PushConstants(m_pcb_pass_cpu);
            RHI_CommandList::DrawIndirect(GetBuffer(Renderer_Buffer::IndirectDrawArgs), arg_stride);
        }
        RHI_CommandList::EndPass();
    }
}
