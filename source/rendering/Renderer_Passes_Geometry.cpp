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
        struct ClusterDrawRange
        {
            uint32_t first_index, index_count, vertex_offset;
            BoundingBox bounds;
        };

        float shadow_texel_density(Light* light, uint32_t slice, const BoundingBox& bounds)
        {
            if (!light->IsBoundsInViewFrustum(bounds, slice)) return 0.0f;
            const Matrix vp = light->GetViewProjectionMatrix(slice);
            const auto center = bounds.GetCenter(), extent = bounds.GetExtents();
            const Vector3 w(vp.m03, vp.m13, vp.m23);
            const float nearest = max(1e-4f, center.Dot(w) + vp.m33 -
                (extent.x * abs(w.x) + extent.y * abs(w.y) + extent.z * abs(w.z)));
            const auto& rect = light->GetAtlasRectangle(slice);
            const float x = Vector3(vp.m00, vp.m10, vp.m20).Length() * rect.width;
            const float y = Vector3(vp.m01, vp.m11, vp.m21).Length() * rect.height;
            return 0.5f * max(x, y) / nearest;
        }

        template<typename ViewDensity>
        bool cluster_draw_ranges(Mesh& mesh, uint32_t sub_index, const Matrix& transform, ViewDensity density, vector<ClusterDrawRange>& ranges)
        {
            ranges.clear();
            const auto& sub = mesh.GetSubMesh(sub_index);
            const auto& clusters = sub.clusters;
            if (clusters.empty() || sub.HasResidentRootOnly()) return false;
            const Vector3 scale = transform.GetScale();
            const float max_scale = max({scale.x, scale.y, scale.z});
            uint32_t first = mesh.GetGlobalIndexOffset() + sub.root.index_offset;
            const uint32_t vertices = mesh.GetGlobalVertexOffset() + sub.root.vertex_offset;
            bool refined = false;
            for (const auto& cluster : clusters)
            {
                const auto world = cluster.bounds * transform;
                ResidentGeometryPage page = {};
                if (GeometryBuffer::SelectPage(cluster, max_scale * density(world), page))
                {
                    ranges.push_back({page.indices, page.index_count, page.vertices, world});
                    refined = true;
                }
                else if (!ranges.empty() && ranges.back().vertex_offset == vertices &&
                    ranges.back().first_index + ranges.back().index_count == first)
                {
                    ranges.back().index_count += cluster.index_count;
                    ranges.back().bounds.Merge(world);
                }
                else ranges.push_back({first, cluster.index_count, vertices, world});
                first += cluster.index_count;
            }
            return refined;
        }

        template<typename ViewDensity>
        bool cluster_draw_ranges(Render* render, const Matrix& transform, ViewDensity density, vector<ClusterDrawRange>& ranges)
        {
            return render->GetMesh() && !render->HasBoundingBoxOverride() &&
                cluster_draw_ranges(*render->GetMesh(), render->GetSubMeshIndex(), transform, density, ranges);
        }

        template<typename LightDensity>
        bool cluster_shadow_ranges(Render* render, LightDensity density, vector<ClusterDrawRange>& ranges)
        {
            return !render->HasInstancing() && cluster_draw_ranges(render, render->GetEntity()->GetMatrix(), density, ranges);
        }

        template<typename Emit>
        bool visit_view_clusters(Render* render, const Renderer_DrawCall& draw, Camera* camera, float height,
            vector<ClusterDrawRange>& ranges, Emit emit)
        {
            const auto& sub = render->GetMesh()->GetSubMesh(render->GetSubMeshIndex());
            if (!camera || sub.clusters.empty() || sub.HasResidentRootOnly() || render->HasBoundingBoxOverride()) return false;
            const float focal = abs(camera->GetProjectionMatrix().m11) * height * 0.5f;
            const auto density = [&](const BoundingBox& box)
            {
                if (!camera->IsInViewFrustum(box)) return 0.0f;
                if (camera->GetProjectionType() != Projection_Perspective) return focal;
                const float depth = Vector3::Dot(box.GetCenter() - camera->GetEntity()->GetPosition(),
                    camera->GetEntity()->GetForward()) - box.GetExtents().Length();
                return focal / max(camera->GetNearPlane(), depth);
            };
            for (uint32_t j = 0; j < draw.instance_count; ++j)
            {
                const uint32_t instance = draw.instance_index + j;
                Matrix transform = render->GetEntity()->GetMatrix();
                if (render->HasInstancing()) transform = render->GetSceneData().instances[instance].GetMatrix() * transform;
                const uint32_t first_instance = render->GetGlobalInstanceOffset() + instance;
                if (cluster_draw_ranges(render, transform, density, ranges))
                {
                    for (const auto& range : ranges) emit(range, first_instance);
                }
                else
                {
                    emit(ClusterDrawRange{render->GetIndexOffset(), render->GetIndexCount(),
                        render->GetVertexOffset(), render->GetGeometryBounds() * transform}, first_instance);
                }
            }
            return true;
        }

        bool draw_view_clusters(RenderPass& pass, Render* render, const Renderer_DrawCall& draw, Camera* camera, float height,
            vector<ClusterDrawRange>& ranges)
        {
            return visit_view_clusters(render, draw, camera, height, ranges,
                [&](const ClusterDrawRange& range, uint32_t first_instance)
                {
                    pass.DrawIndexed(range.index_count, range.first_index, range.vertex_offset, first_instance, 1);
                });
        }

        TConsoleVar<bool> cvar_grass_specialized("r.grass_specialized", true,
            "use grass-only shading, compact vertex exports and mesh bounds; disable for comparison");
        TConsoleVar<bool> cvar_gbuffer_scatter_first("r.gbuffer_scatter_first", true,
            "rasterize gpu scatter before the opaque g-buffer so the ground it hides is never shaded; disable for comparison");
        TConsoleVar<float> cvar_grass_thin_distance("r.grass_thin_distance", 18.0f,
            "distance in metres past which a grass blade ring thins out towards the density of the ring after it, every blade nearer than that is kept; 0 keeps every blade");
        TConsoleVar<float> cvar_scatter_blend_distance("r.scatter_blend_distance", 8.0f,
            "distance in metres past which scattered detail such as pebbles sinks to its dirt line instead of blending with the ground, handed over chip by chip up to twice as far; 0 blends every chip");
        // 6.25 cm cells (matching TerrainDeformation) resolve both distant tire tracks and discrete footprints (64 MiB total).
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
            if (!cvar_mesh_shaders.GetValueAs<bool>() || !RHI_Device::Caps().mesh_shaders || Renderer::IsStereoView())
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

        bool use_visibility_buffer()
        {
            if (!cvar_visibility_buffer.GetValue() || !use_mesh_shaders() || Renderer::IsViewWireframe() ||
                Renderer::GetSecondaryViewMode() == Renderer_SecondaryViewMode::Vertices ||
                !Renderer::GetRenderTarget(Renderer_RenderTarget::visibility_buffer))
            {
                return false;
            }

            const Renderer_Shader required[] =
            {
                Renderer_Shader::meshlet_mesh_visibility_m,
                Renderer_Shader::meshlet_mesh_visibility_alpha_m,
                Renderer_Shader::meshlet_mesh_visibility_late_m,
                Renderer_Shader::meshlet_mesh_visibility_alpha_late_m,
                Renderer_Shader::impostor_mesh_visibility_m,
                Renderer_Shader::impostor_mesh_visibility_late_m,
                Renderer_Shader::visibility_opaque_p,
                Renderer_Shader::visibility_alpha_p,
                Renderer_Shader::visibility_classify_c,
                Renderer_Shader::visibility_resolve_opaque_c,
                Renderer_Shader::visibility_resolve_alpha_c,
                Renderer_Shader::visibility_resolve_impostor_c
            };
            for (Renderer_Shader id : required)
            {
                RHI_Shader* s = Renderer::GetShader(id);
                if (!s || !s->IsCompiled())
                {
                    return false;
                }
            }
            return true;
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
                    RHI_Device::Caps().mesh_shaders ? "yes" : "no",
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

        float gpu_scatter_ring_radius(const Renderer::PassState::GpuScatterSlot& slot, const uint32_t ring)
        {
            return slot.params.ring_radii_m[ring];
        }

        // has to match GRASS_FILL_MARGIN in grass_populate.hlsl
        const float gpu_scatter_fill_margin = 0.85f;
        // has to match grass_blades_per_thread in grass_placement.hlsl
        const uint32_t gpu_scatter_blades_per_thread = 4;

        // has to match grass_patch_max_boost in grass_populate.hlsl
        const float gpu_scatter_patch_max_boost = 4.0f;

        // has to match grass_max_boost in grass_populate.hlsl, this is what bounds the dispatch
        const float gpu_scatter_max_boost = 10.0f;

        // distance thinning of a blade ring starts no nearer than this many times the height of the camera over the ground
        const float gpu_scatter_thin_clearance_reach = 2.5f;
        // steepest fall of the share of blades a thinned ring keeps, as a power of the distance
        const float gpu_scatter_thin_exponent_max = 2.5f;

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
            uint32_t cap = gpu_scatter_lod_cap(slot, lod, state.params.density);

            // Triangle budget guard for non-grass detail meshes (e.g. multi-frond glTF ferns):
            // keeps g_buffer_grass bounded even when a high-poly foliage asset is placed on GPU scatter.
            const uint32_t tri_count = state.indirect_args_static[lod].index_count / 3u;
            if (slot > 0 && tri_count > 64u)
            {
                constexpr uint32_t max_detail_tris_per_ring = 1500000u;
                const uint32_t tri_cap = max(128u, max_detail_tris_per_ring / tri_count);
                cap = min(cap, tri_cap);
            }

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

        // height of the camera over the lowest ground the blades of a ring can stand on, the ground right under it is not
        // enough, from the top of a cliff the meadow below is seen from above as well. reach is the outer radius of the
        // ring and height_floor the lowest height the layer grows at, the sea bed next to a beach is not a place grass can be
        float gpu_scatter_camera_clearance(const Vector3& eye, const float reach, const float height_floor)
        {
            const Terrain* terrain = Terrain::FindActive();
            float lowest           = 0.0f;
            if (!terrain || !terrain->SampleHeight(eye.x, eye.z, lowest))
            {
                return 0.0f;
            }

            const uint32_t directions = 8;
            const uint32_t steps      = 3;
            for (uint32_t direction = 0; direction < directions; direction++)
            {
                const float angle = math::pi_2 * static_cast<float>(direction) / static_cast<float>(directions);
                for (uint32_t step = 1; step <= steps; step++)
                {
                    const float radius = reach * static_cast<float>(step) / static_cast<float>(steps);
                    float height       = 0.0f;
                    if (terrain->SampleHeight(eye.x + cosf(angle) * radius, eye.z + sinf(angle) * radius, height))
                    {
                        lowest = min(lowest, height);
                    }
                }
            }

            return max(eye.y - max(lowest, height_floor), 0.0f);
        }

        // a blade ring is authored at one density from its inner edge to its outer one and the ring after it is far sparser,
        // while a blade only gets smaller with distance, so the far part of a ring stacks many times the blades behind a
        // pixel that its near part needs to look as full. past the thinning distance populate keeps a share of the blades
        // that falls off as a power of the distance, picked so the ring reaches its outer edge at the density of the ring
        // that takes over there. nothing changes up close and nothing changes past the ring, the cliff between the two
        // authored densities becomes a slope. x is the distance the slope starts at and y its exponent, zero leaves the
        // ring alone. only the grass slot places its blades with a progressive sequence, which is what lets a cell drop
        // the tail of its candidates and stay evenly covered, and a ring of cards thins itself.
        // blades hide the ground by standing in front of each other, which only works along the ground, seen from above
        // every one of them is on its own and a thinned meadow shows its soil. so the slope starts no nearer than a few
        // times the height of the camera over the ground, a camera that climbs gets its blades back and one high enough
        // to look down on the whole ring keeps all of them
        Vector2 gpu_scatter_thin(const Renderer::PassState::GpuScatterSlot& state, const uint32_t slot, const uint32_t lod, const Vector3& eye)
        {
            const uint32_t next = lod + 1;
            if (slot != 0 || next >= renderer_max_gpu_scatter_lods)
            {
                return Vector2(0.0f, 0.0f);
            }

            const float outer = gpu_scatter_ring_radius(state, lod);
            const float inner = lod == 0 ? 0.0f : min(gpu_scatter_ring_radius(state, lod - 1), outer);
            const float after = gpu_scatter_ring_radius(state, next);
            const float base  = max(cvar_grass_thin_distance.GetValue(), inner);
            if (cvar_grass_thin_distance.GetValue() <= 0.0f || base >= outer || after <= outer || state.params.cell_size_m[next] <= 0.0f)
            {
                return Vector2(0.0f, 0.0f);
            }

            // the cap of a ring spread over its area, the boosts and margins the dispatch applies are shared by every ring of the slot
            const float density       = static_cast<float>(gpu_scatter_slot_cap(state, slot, lod)) / max(outer * outer - inner * inner, 1.0f);
            const float density_after = static_cast<float>(gpu_scatter_slot_cap(state, slot, next)) / max(after * after - outer * outer, 1.0f);
            if (density <= density_after)
            {
                return Vector2(0.0f, 0.0f);
            }

            const float start = max(base, gpu_scatter_camera_clearance(eye, outer, state.params.height_min) * gpu_scatter_thin_clearance_reach);
            if (start >= outer)
            {
                return Vector2(0.0f, 0.0f);
            }

            // a slope that starts close to the outer edge would have to be a wall to get there, the cap leaves part of the
            // authored step in place instead and lets the thinning fade out as the start reaches the edge
            return Vector2(start, min(logf(density / density_after) / logf(outer / start), gpu_scatter_thin_exponent_max));
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


        void bind_mesh_shader_geometry(RenderPass& pass)
        {
            pass.Bind(Renderer_BindingsUav::tree_wind_cache, Renderer_Buffer::TreeWindCache);
            pass.Bind(Renderer_BindingsUav::indirect_draw_data, Renderer_Buffer::IndirectDrawData);
            pass.Bind(Renderer_BindingsUav::meshlet_instances, Renderer_Buffer::MeshletInstances);
            pass.Bind(Renderer_BindingsUav::meshlet_bounds, GeometryBuffer::GetMeshletBoundsBuffer());
            pass.Bind(Renderer_BindingsUav::meshlet_vertices, GeometryBuffer::GetMeshletVertexBuffer());
            pass.Bind(Renderer_BindingsUav::meshlet_micro_indices, GeometryBuffer::GetMeshletMicroIndexBuffer());
            pass.Bind(Renderer_BindingsUav::cull_phase, Renderer_Buffer::CullPhase);
        }

        void bind_visible_triangle_geometry(RenderPass& pass)
        {
            pass.Bind(Renderer_BindingsUav::indirect_draw_data, Renderer_Buffer::IndirectDrawData);
            pass.Bind(Renderer_BindingsUav::meshlet_instances, Renderer_Buffer::MeshletInstances);
            pass.Bind(Renderer_BindingsUav::visible_triangles, Renderer_Buffer::VisibleTriangles);
            pass.Bind(Renderer_BindingsUav::meshlet_bounds, GeometryBuffer::GetMeshletBoundsBuffer());
            pass.Bind(Renderer_BindingsUav::cull_phase, Renderer_Buffer::CullPhase);
        }

        void set_mesh_draw_constants(RenderPass& pass)
        {
            // the opaque/alpha split is a shader variant now, the region base is unused by the mesh path
            // the constants still flush because the alpha pixel shaders read the other pass fields
            pass.Set(pass_visible_triangles::region_base, 0u);
            pass.Set(pass_visible_triangles::late, false);
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
        vector<ClusterDrawRange> cluster_ranges;
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

                    if (cluster_shadow_ranges(render, [&](const BoundingBox& bounds) { return shadow_texel_density(light, array_index, bounds); }, cluster_ranges))
                    {
                        for (const auto& range : cluster_ranges)
                        {
                            auto& args = batch.arguments.emplace_back();
                            args.index_count = range.index_count;
                            args.instance_count = 1;
                            args.first_index = range.first_index;
                            args.vertex_offset = static_cast<int32_t>(range.vertex_offset);
                            args.first_instance = draw_call.draw_data_index;
                            ++argument_count;
                        }
                        continue;
                    }

                    Sb_IndirectDrawArgs& argument = batch.arguments.emplace_back();
                    argument.index_count          = render->GetIndexCount();
                    argument.instance_count       = 1;
                    argument.first_index          = render->GetIndexOffset();
                    argument.vertex_offset        = static_cast<int32_t>(render->GetVertexOffset());
                    argument.first_instance       = draw_call.draw_data_index;
                    argument_count++;
                }
            }
        }

        RHI_Buffer* argument_buffer = GetBuffer(Renderer_Buffer::CpuIndirectDrawArgs);
        RHI_Shader* multi_vertex_shader = GetShader(Renderer_Shader::depth_light_multi_draw_v);
        RHI_Shader* multi_pixel_shader  = GetShader(Renderer_Shader::depth_light_multi_draw_alpha_color_p);
        const bool shaders_supported    = multi_vertex_shader && multi_vertex_shader->IsCompiled() && (!has_alpha_draws || (multi_pixel_shader && multi_pixel_shader->IsCompiled()));
        const bool use_batches          = RHI_Device::Caps().shader_base_instance && shaders_supported && argument_buffer && argument_count != 0 && m_cpu_indirect_draw_arg_count + argument_count <= renderer_max_cpu_indirect_draws;
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

        RHI_Shader* current_vs             = use_batches ? multi_vertex_shader : GetShader(Renderer_Shader::depth_light_v);
        RHI_Shader* current_ps             = nullptr;
        RHI_RasterizerMode current_raster  = RHI_RasterizerMode::SolidNoDepthClip;

        RenderPass pass("shadow_maps", current_vs);
        pass.Blend(RHI_BlendMode::Off);
        pass.Depth(RHI_DepthMode::ReadWrite);
        pass.Rasterizer(current_raster);
        pass.DepthTarget(Renderer_RenderTarget::shadow_atlas, 0.0f);

        for (ShadowSlice& slice : slices)
        {
            Light* light = slice.light;
            const RHI_RasterizerMode raster_mode = light->GetLightType() == LightType::Directional ? RHI_RasterizerMode::SolidNoDepthClip : RHI_RasterizerMode::Solid;
            RHI_Viewport viewport;
            viewport.x      = slice.rect.x;
            viewport.y      = slice.rect.y;
            viewport.width  = slice.rect.width;
            viewport.height = slice.rect.height;
            pass.Viewport(viewport);
            pass.Scissor(slice.rect);

            if (use_batches)
            {
                for (ShadowBatch& batch : slice.batches)
                {
                    RHI_Shader* vertex_shader = multi_vertex_shader;
                    RHI_Shader* pixel_shader  = batch.alpha_tested ? multi_pixel_shader : nullptr;
                    if (current_vs != vertex_shader || current_ps != pixel_shader || current_raster != raster_mode)
                    {
                        current_vs     = vertex_shader;
                        current_ps     = pixel_shader;
                        current_raster = raster_mode;
                        pass.Shaders(current_vs, current_ps);
                        pass.Rasterizer(current_raster);
                        pass.Viewport(viewport);
                        pass.Scissor(slice.rect);
                    }

                    pass.DrawIndex(numeric_limits<uint32_t>::max());
                    pass.Transparent(false);
                    pass.Set(pass_depth_light::light_index, static_cast<uint32_t>(light->GetIndex()));
                    pass.Set(pass_depth_light::array_index, static_cast<uint32_t>(slice.array_index));
                    pass.CullMode(batch.cull_mode);
                    pass.VertexBuffer(batch.vertex_buffer);
                    pass.IndexBuffer(batch.index_buffer);
                    pass.DrawIndexedIndirect(argument_buffer, batch.argument_offset, static_cast<uint32_t>(batch.arguments.size()));
                }
            }

            auto draw_direct = [&](const Renderer_DrawCall& draw_call)
            {
                Render* render = draw_call.render;
                Material* material = render->GetMaterial();
                RHI_Shader* vertex_shader = use_batches ? multi_vertex_shader : GetShader(Renderer_Shader::depth_light_v);
                RHI_Shader* pixel_shader  = material->IsAlphaTested() ? (use_batches ? multi_pixel_shader : GetShader(Renderer_Shader::depth_light_alpha_color_p)) : nullptr;
                if (current_vs != vertex_shader || current_ps != pixel_shader || current_raster != raster_mode)
                {
                    current_vs     = vertex_shader;
                    current_ps     = pixel_shader;
                    current_raster = raster_mode;
                    pass.Shaders(current_vs, current_ps);
                    pass.Rasterizer(current_raster);
                    pass.Viewport(viewport);
                    pass.Scissor(slice.rect);
                }

                pass.DrawIndex(draw_call.draw_data_index);
                pass.Transparent(false);
                pass.MaterialIndex(material->GetIndex());
                pass.Set(pass_depth_light::has_albedo, material->HasTextureOfType(MaterialTextureType::Color));
                pass.Set(pass_depth_light::light_index, static_cast<uint32_t>(light->GetIndex()));
                pass.Set(pass_depth_light::array_index, static_cast<uint32_t>(slice.array_index));
                pass.CullMode(static_cast<RHI_CullMode>(material->GetProperty(MaterialProperty::CullMode)));
                RHI_Buffer* instance_buffer = GeometryBuffer::GetInstanceBuffer() ? GeometryBuffer::GetInstanceBuffer() : GetBuffer(Renderer_Buffer::DummyInstance);
                pass.VertexBuffer(render->GetVertexBuffer(), instance_buffer);
                pass.IndexBuffer(render->GetIndexBuffer());

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
                    const auto flush_run = [&]()
                    {
                        if (run_count == 0) return;
                        pass.DrawIndexed(render->GetIndexCount(), render->GetIndexOffset(),
                            render->GetVertexOffset(), render->GetGlobalInstanceOffset() + run_start, run_count);
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
                        const auto& sub = render->GetMesh()->GetSubMesh(render->GetSubMeshIndex());
                        if (!sub.clusters.empty() && !sub.HasResidentRootOnly() &&
                            cluster_draw_ranges(render, render->GetSceneData().instances[instance].GetMatrix() *
                                render->GetEntity()->GetMatrix(),
                                [&](const BoundingBox& box) { return shadow_texel_density(light, slice.array_index, box); }, cluster_ranges))
                        {
                            flush_run();
                            for (const auto& range : cluster_ranges)
                                pass.DrawIndexed(range.index_count, range.first_index, range.vertex_offset,
                                    render->GetGlobalInstanceOffset() + instance, 1);
                            continue;
                        }
                        if (run_count && instance != run_start + run_count) flush_run();
                        if (!run_count) run_start = instance;
                        ++run_count;
                    }
                    flush_run();
                }
                else
                {
                    if (cluster_shadow_ranges(render, [&](const BoundingBox& bounds) { return shadow_texel_density(light, slice.array_index, bounds); }, cluster_ranges))
                    {
                        for (const auto& range : cluster_ranges)
                            pass.DrawIndexed(range.index_count, range.first_index, range.vertex_offset);
                        return;
                    }
                    pass.DrawIndexed(render->GetIndexCount(), render->GetIndexOffset(), render->GetVertexOffset(), render->GetGlobalInstanceOffset() + draw_call.instance_index, draw_call.instance_count);
                }
            };

            const vector<const Renderer_DrawCall*>& draws = use_batches ? slice.direct_draws : slice.visible_draws;
            for (const Renderer_DrawCall* draw_call : draws)
            {
                draw_direct(*draw_call);
            }
        }
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
        if (!RHI_Device::Caps().shader_base_instance)
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

        vector<ClusterDrawRange> cluster_ranges;
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
                if (!vertex_buffer || !index_buffer || !render->HasGeometry())
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

                const bool wind = material->GetProperty(MaterialProperty::WindAnimation) > 0.0f;
                const bool per_instance = render->HasInstancing() && !render->HasBoundingBoxOverride();
                const BoundingBox& bounds = per_instance ? render->GetGeometryBounds() : render->GetBoundingBox();
                auto& group_casters = groups[it->second].casters;
                auto& root = group_casters.emplace_back();
                root.draw_data_index = draw_call.draw_data_index;
                root.instance_offset = render->HasInstancing() ? render->GetGlobalInstanceOffset() + draw_call.instance_index : 0u;
                root.instance_count = draw_call.instance_count;
                root.flags = (per_instance ? 0u : shadow_caster_flag_world_bounds) | (wind ? shadow_caster_flag_wind : 0u);
                root.index_count_directional = root.index_count_local = render->GetIndexCount();
                root.first_index_directional = root.first_index_local = render->GetIndexOffset();
                root.vertex_offset_directional = root.vertex_offset_local = static_cast<int32_t>(render->GetVertexOffset());
                root.bounds_min = bounds.GetMin();
                root.bounds_max = bounds.GetMax();
                root.max_shadow_distance = per_instance ? shadow_distance : 0.0f;
                // A caster can be outside the camera view and still cover shadow texels.
                // Choose the finest resolution requested by any light slice using this geometry.
                auto light_density = [&](const BoundingBox& box)
                {
                    float density = 0;
                    for (const auto& slice : slices)
                        density = max(density, shadow_texel_density(slice.light, slice.array_index, box));
                    return density;
                };
                bool refined = false;
                const auto& sub = render->GetMesh()->GetSubMesh(render->GetSubMeshIndex());
                if (per_instance && !sub.clusters.empty() && !sub.HasResidentRootOnly())
                {
                    // The shadow pass needs an upper bound. This cached scale
                    // also bounds a draw that covers only part of the instance list.
                    const float scale = render->GetInstanceScaleBound();
                    const Vector3 parent = render->GetEntity()->GetMatrix().GetScale();
                    const float density = scale * max({abs(parent.x), abs(parent.y), abs(parent.z)}) * light_density(render->GetBoundingBox());
                    refined = cluster_draw_ranges(render, Matrix::Identity, [&](const BoundingBox&) { return density; }, cluster_ranges);
                }
                else refined = cluster_shadow_ranges(render, light_density, cluster_ranges);
                if (refined && (instance_total + uint64_t(draw_call.instance_count) * cluster_ranges.size()) *
                    slices.size() <= shadow_visible_capacity_max)
                {
                    const auto proxy = group_casters.back();
                    group_casters.pop_back();
                    for (const auto& range : cluster_ranges)
                    {
                        auto& caster = group_casters.emplace_back(proxy);
                        caster.index_count_directional = caster.index_count_local = range.index_count;
                        caster.first_index_directional = caster.first_index_local = range.first_index;
                        caster.vertex_offset_directional = caster.vertex_offset_local = static_cast<int32_t>(range.vertex_offset);
                        caster.bounds_min = range.bounds.GetMin();
                        caster.bounds_max = range.bounds.GetMax();
                    }
                    instance_total += uint64_t(draw_call.instance_count) * (cluster_ranges.size() - 1);
                }
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

            {
                RenderPass pass("shadow_cull");
                RHI_CommandList::UpdateBuffer(buffer_casters, 0, caster_count * sizeof(Sb_ShadowCaster), casters.data(), false);
                RHI_CommandList::UpdateBuffer(buffer_tasks, 0, task_count * sizeof(uint32_t) * 2, tasks.data(), false);
                RHI_CommandList::UpdateBuffer(buffer_slices, 0, slice_count * sizeof(uint32_t) * 4, slice_data.data(), false);

                pass.Set(pass_shadow_cull::slice_count, slice_count);
                pass.Set(pass_shadow_cull::caster_count, caster_count);
                pass.Set(pass_shadow_cull::task_count, task_count);
                pass.Set(pass_shadow_cull::visible_capacity, buffer_visible->GetElementCount());

                auto bind = [&]()
                {
                    pass.Bind("shadow_casters", buffer_casters);
                    pass.Bind("shadow_tasks", buffer_tasks);
                    pass.Bind("shadow_slices", buffer_slices);
                    pass.Bind("shadow_counts", buffer_counts);
                    pass.Bind("shadow_args", buffer_args);
                    pass.Bind("shadow_visible", buffer_visible);
                };

                pass.Shader(shader_clear);
                bind();
                pass.Dispatch((pair_count + 1 + 63) / 64, 1, 1);

                const uint32_t groups_x = min(task_count, INSTANCE_CULL_DISPATCH_WIDTH);
                const uint32_t groups_y = (task_count + INSTANCE_CULL_DISPATCH_WIDTH - 1) / INSTANCE_CULL_DISPATCH_WIDTH;
                pass.Shader(shader_cull);
                bind();
                pass.Set(pass_shadow_cull::write, false);
                pass.Dispatch(groups_x, groups_y, slice_count);

                pass.Shader(shader_args);
                bind();
                pass.Dispatch((pair_count + 63) / 64, 1, 1);

                pass.Shader(shader_cull);
                bind();
                pass.Set(pass_shadow_cull::write, true);
                pass.Dispatch(groups_x, groups_y, slice_count);
            }
        }

        // the pass always runs so the atlas clears even when nothing casts
        RHI_Shader* current_ps            = nullptr;
        RHI_RasterizerMode current_raster = RHI_RasterizerMode::SolidNoDepthClip;

        {
            RenderPass pass("shadow_maps", shader_vertex);
            pass.Blend(RHI_BlendMode::Off);
            pass.Depth(RHI_DepthMode::ReadWrite);
            pass.Rasterizer(current_raster);
            pass.DepthTarget(Renderer_RenderTarget::shadow_atlas, 0.0f);

            const uint32_t arg_stride = static_cast<uint32_t>(sizeof(Sb_IndirectDrawArgs));
            for (uint32_t slice_index = 0; pair_count != 0 && slice_index < slice_count; slice_index++)
            {
                const ShadowSlice& slice = slices[slice_index];
                const RHI_RasterizerMode raster_mode = slice.light->GetLightType() == LightType::Directional ? RHI_RasterizerMode::SolidNoDepthClip : RHI_RasterizerMode::Solid;
                RHI_Viewport viewport;
                viewport.x      = slice.rect.x;
                viewport.y      = slice.rect.y;
                viewport.width  = slice.rect.width;
                viewport.height = slice.rect.height;

                bool state_dirty = true;
                for (const ShadowGroup& group : groups)
                {
                    RHI_Shader* pixel_shader = group.alpha_tested ? shader_alpha : nullptr;
                    if (current_ps != pixel_shader || current_raster != raster_mode)
                    {
                        current_ps     = pixel_shader;
                        current_raster = raster_mode;
                        pass.Shaders(shader_vertex, current_ps);
                        pass.Rasterizer(current_raster);
                        state_dirty = true;
                    }

                    if (state_dirty)
                    {
                        pass.Viewport(viewport);
                        pass.Scissor(slice.rect);
                        pass.Bind("shadow_visible", buffer_visible);
                        state_dirty = false;
                    }

                    pass.DrawIndex(numeric_limits<uint32_t>::max());
                    pass.Transparent(false);
                    pass.Set(pass_depth_light::light_index, static_cast<uint32_t>(slice.light->GetIndex()));
                    pass.Set(pass_depth_light::array_index, slice.array_index);
                    pass.CullMode(group.cull_mode);
                    pass.VertexBuffer(group.vertex_buffer);
                    pass.IndexBuffer(group.index_buffer);
                    pass.DrawIndexedIndirect(buffer_args, (slice_index * caster_count + group.first) * arg_stride, static_cast<uint32_t>(group.casters.size()));
                }
            }
        }

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

    void Renderer::Pass_Water_Visibility()
    {
        if (!m_water_present || !m_pass_state.ocean || IsSecondaryViewActive() || IsStereoView())
        {
            return;
        }

        RHI_Shader* shader = GetShader(Renderer_Shader::water_visibility_c);
        RHI_Texture* tex_hiz = GetRenderTarget(Renderer_RenderTarget::gbuffer_depth_hiz);
        RHI_Buffer* buf_vis = GetBuffer(Renderer_Buffer::WaterVisibility);
        RHI_Buffer* buf_boxes = GetBuffer(Renderer_Buffer::WaterVisibilityBoxes);
        const uint32_t slot = m_frame_resource_index % renderer_draw_data_buffer_count;
        RHI_Buffer* buf_readback = GetBuffer(static_cast<Renderer_Buffer>(static_cast<uint32_t>(Renderer_Buffer::WaterVisibilityReadback0) + slot));
        if (!shader || !shader->IsCompiled() || !tex_hiz || !buf_vis || !buf_boxes || !buf_readback || tex_hiz->GetMipCount() == 0)
        {
            return;
        }

        const uint32_t max_hiz_mip = tex_hiz->GetMipCount() - 1u;
        const uint32_t mip_level   = min(3u, max_hiz_mip);
        const uint32_t mip_width   = max(1u, tex_hiz->GetWidth() >> mip_level);
        const uint32_t mip_height  = max(1u, tex_hiz->GetHeight() >> mip_level);
        const uint32_t box_count   = static_cast<uint32_t>(m_water_visibility_boxes.size() / 2);

        const uint32_t zero = 0u;
        RHI_CommandList::UpdateBuffer(buf_vis, 0, sizeof(uint32_t), &zero, false);
        if (box_count > 0)
        {
            RHI_CommandList::UpdateBuffer(buf_boxes, 0, static_cast<uint64_t>(box_count) * 2ull * sizeof(Vector4), m_water_visibility_boxes.data(), false);
        }

        RenderPass pass("water_visibility");
        pass.Shader(shader, "water_visibility");
        pass.Bind(Renderer_BindingsSrv::tex, tex_hiz);
        pass.Bind("water_visibility", buf_vis);
        pass.Bind("water_visibility_boxes", buf_boxes);
        pass.Set(0, mip_level);
        pass.Set(1, mip_width);
        pass.Set(2, mip_height);
        pass.Set(3, Renderer::view().frame.ocean_sea_level);
        pass.Set(4, box_count);
        pass.Set(5, max_hiz_mip);
        pass.Dispatch((mip_width + 7u) / 8u, (mip_height + 7u) / 8u, 1u);

        RHI_CommandList::PrepareBufferForReadback(buf_vis);
        RHI_CommandList::CopyBuffer(buf_vis, buf_readback, sizeof(uint32_t));
    }

    void Renderer::Pass_TessellatedCull(bool late)
    {
        static vector<Sb_TessellatedDraw> draws;
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
        }

        vector<ClusterDrawRange> cluster_ranges;
        for (uint32_t i = 0; i < (late ? 0u : m_draw_call_count); i++)
        {
            const Renderer_DrawCall& draw_call = m_draw_calls[i];
            Render* render                     = draw_call.render;
            Material* material                 = render->GetMaterial();
            if (!material || material->IsTransparent() || !material->IsTessellated() || material->GetProperty(MaterialProperty::IsSkidMark) != 0.0f)
            {
                continue;
            }

            if (visit_view_clusters(render, draw_call, GetViewCamera(), GetResolutionRender().y, cluster_ranges,
                [&](const ClusterDrawRange& range, uint32_t first_instance)
                {
                    auto& draw = draws.emplace_back();
                    draw.bounds_min = range.bounds.GetMin();
                    draw.bounds_max = range.bounds.GetMax();
                    const float distance = render->GetMaxRenderDistance();
                    draw.max_distance_squared = distance < sqrtf(FLT_MAX) ? distance * distance : 0.0f;
                    draw.flags = render->GetEntity()->GetTimeSinceLastTransform() <= 0.1f ? tessellated_flag_skip_hiz : 0u;
                    draw.index_count = range.index_count;
                    draw.first_index = range.first_index;
                    draw.vertex_offset = range.vertex_offset;
                    draw.first_instance = first_instance;
                    draw.instance_count = 1;
                    m_tessellated_draws.push_back(i);
                })) continue;

            const BoundingBox& bounds = render->GetBoundingBox();
            Sb_TessellatedDraw& draw  = draws.emplace_back();
            draw.bounds_min           = bounds.GetMin();
            draw.bounds_max           = bounds.GetMax();
            const float max_distance  = render->GetMaxRenderDistance();
            draw.max_distance_squared = (max_distance > 0.0f && max_distance < numeric_limits<float>::max() * 0.5f) ? max_distance * max_distance : 0.0f;
            Entity* entity            = render->GetEntity();
            draw.flags                = (entity && entity->GetTimeSinceLastTransform() <= 0.1f) ? tessellated_flag_skip_hiz : 0u;
            draw.index_count          = render->GetIndexCount();
            draw.first_index          = render->GetIndexOffset();
            draw.vertex_offset        = render->GetVertexOffset();
            draw.first_instance       = render->GetGlobalInstanceOffset() + draw_call.instance_index;
            draw.instance_count       = draw_call.instance_count;

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
        RHI_Buffer* buffer_args   = EnsureBufferCapacity(Renderer_Buffer::TessellatedArgs, sizeof(Sb_IndirectDrawArgs), draw_count * 2, "tessellated_args");
        RHI_Texture* tex_hiz      = GetRenderTarget(late ? Renderer_RenderTarget::gbuffer_depth_hiz_early : Renderer_RenderTarget::gbuffer_depth_hiz);
        const bool history_hiz    = m_pass_state.hiz_history_valid && cvar_hiz_occlusion.GetValueAs<bool>() && !m_is_hiz_suppressed;

        {
            RenderPass pass(late ? "tessellated_cull_late" : "tessellated_cull", shader);
            if (!late)
            {
                RHI_CommandList::UpdateBuffer(buffer_draws, 0, draw_count * sizeof(Sb_TessellatedDraw), draws.data(), false);
            }

            pass.Bind(Renderer_BindingsSrv::tex, tex_hiz);
            pass.Bind("tessellated_draws", buffer_draws);
            pass.Bind("tessellated_args", buffer_args);
            pass.Set(pass_tessellated_cull::draw_count, draw_count);
            pass.Set(pass_tessellated_cull::max_hiz_mip, static_cast<float>(tex_hiz->GetMipCount() - 1));
            pass.Set(pass_tessellated_cull::hiz_depth_bias, clamp(cvar_hiz_depth_bias.GetValueAs<float>(), 0.0f, 1.0f));
            pass.Set(pass_tessellated_cull::history_hiz, history_hiz);
            pass.Set(pass_tessellated_cull::late, late);
            pass.Dispatch((draw_count + 63u) / 64u, 1, 1);
        }

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

        // the early survivors were consumed by the early meshlet cull, the late ones take their place
        if (late)
        {
            Sb_IndirectDispatchArgs instance_dispatch = {};
            RHI_CommandList::UpdateBuffer(GetBuffer(Renderer_Buffer::InstanceDispatchArgs), 0, sizeof(instance_dispatch), &instance_dispatch, false);
        }

        // phase a, per-instance distance + side-frustum + hi-z, survivors land in surviving_instances and bump instance_dispatch_args.group_count_x
        {
            RenderPass pass(late ? "instance_cull_late" : "instance_cull", Renderer_Shader::instance_cull_c);
            pass.Bind("instance_visibility", EnsureBufferCapacity(Renderer_Buffer::InstanceVisibility, sizeof(uint32_t), m_cull_task_count * (INSTANCE_CULL_BATCH_SIZE / 16u), "instance_visibility"));
            pass.Bind(Renderer_BindingsSrv::tex, tex_hiz);
            pass.Bind(Renderer_BindingsUav::indirect_draw_data, Renderer_Buffer::IndirectDrawData);
            pass.Bind(Renderer_BindingsUav::cull_tasks, Renderer_Buffer::CullTasks);
            pass.Bind(Renderer_BindingsUav::surviving_instances, Renderer_Buffer::SurvivingInstances);
            pass.Bind(Renderer_BindingsUav::instance_dispatch_args, Renderer_Buffer::InstanceDispatchArgs);
            pass.Bind(Renderer_BindingsUav::triangle_dispatch_args, Renderer_Buffer::TriangleDispatchArgs);
            pass.Bind("impostor_instances", Renderer_Buffer::ImpostorInstances);

            // survivors past max_instances are dropped
            pass.Set(pass_instance_cull::hiz_depth_bias, clamp(cvar_hiz_depth_bias.GetValueAs<float>(), 0.0f, 1.0f));
            pass.Set(pass_instance_cull::task_count, m_cull_task_count);
            pass.Set(pass_instance_cull::max_hiz_mip, max_hiz_mip);
            pass.Set(pass_instance_cull::max_instances, GetBuffer(Renderer_Buffer::SurvivingInstances)->GetElementCount());
            pass.Set(pass_instance_cull::late, late);
            pass.Set(pass_instance_cull::history_hiz, history_hiz);
            pass.Set(pass_instance_cull::max_impostors, use_mesh_shaders() ? GetBuffer(Renderer_Buffer::ImpostorInstances)->GetElementCount() : 0u);

            const uint32_t groups_x = min(m_cull_task_count, INSTANCE_CULL_DISPATCH_WIDTH);
            const uint32_t groups_y = (m_cull_task_count + INSTANCE_CULL_DISPATCH_WIDTH - 1u) / INSTANCE_CULL_DISPATCH_WIDTH;
            pass.Dispatch(groups_x, groups_y, 1);
        }

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
        {
            RenderPass pass(late ? "indirect_cull_meshlet_late" : "indirect_cull_meshlet", Renderer_Shader::indirect_cull_c);
            pass.Bind(Renderer_BindingsSrv::tex, tex_hiz);
            pass.Bind(Renderer_BindingsSrv::tex2, tex_history);
            pass.Bind(Renderer_BindingsUav::indirect_draw_data, Renderer_Buffer::IndirectDrawData);
            pass.Bind(Renderer_BindingsUav::meshlet_bounds, GeometryBuffer::GetMeshletBoundsBuffer());
            pass.Bind(Renderer_BindingsUav::surviving_instances, Renderer_Buffer::SurvivingInstances);
            pass.Bind(Renderer_BindingsUav::meshlet_instances, Renderer_Buffer::MeshletInstances);
            pass.Bind(Renderer_BindingsUav::triangle_dispatch_args, Renderer_Buffer::TriangleDispatchArgs);
            pass.Bind(Renderer_BindingsUav::tree_wind_cache, Renderer_Buffer::TreeWindCache);

            pass.Set(pass_indirect_cull::hiz_depth_bias, clamp(cvar_hiz_depth_bias.GetValueAs<float>(), 0.0f, 1.0f));
            pass.Set(pass_indirect_cull::max_hiz_mip, max_hiz_mip);
            pass.Set(pass_indirect_cull::max_meshlet_instances, GetBuffer(Renderer_Buffer::MeshletInstances)->GetElementCount() / (use_mesh_shaders() ? 1u : 2u));
            pass.Set(pass_indirect_cull::split_opaque_alpha, use_mesh_shaders());
            pass.Set(pass_indirect_cull::wind_cache_limit, static_cast<uint32_t>(clamp(cvar_tree_wind_cache_entries.GetValueAs<float>(), 0.0f, static_cast<float>(GetBuffer(Renderer_Buffer::TreeWindCache)->GetElementCount()))));
            pass.Set(pass_indirect_cull::late, late);
            pass.Set(pass_indirect_cull::history_hiz, history_hiz);

            pass.DispatchIndirect(Renderer_Buffer::InstanceDispatchArgs);
        }

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
            {
                RenderPass pass(late ? "indirect_cull_triangle_late" : "indirect_cull_triangle", Renderer_Shader::indirect_cull_triangle_c);
                pass.Bind(Renderer_BindingsUav::indirect_draw_args, Renderer_Buffer::IndirectDrawArgs);
                pass.Bind(Renderer_BindingsUav::indirect_draw_data, Renderer_Buffer::IndirectDrawData);
                pass.Bind(Renderer_BindingsUav::meshlet_bounds, GeometryBuffer::GetMeshletBoundsBuffer());
                pass.Bind(Renderer_BindingsUav::meshlet_instances, Renderer_Buffer::MeshletInstances);
                pass.Bind(Renderer_BindingsUav::visible_triangles, Renderer_Buffer::VisibleTriangles);
                pass.Bind(Renderer_BindingsUav::cull_phase, Renderer_Buffer::CullPhase);

                // survivors past the per half cap are dropped
                pass.Set(pass_indirect_cull_triangle::max_meshlet_instances, GetBuffer(Renderer_Buffer::MeshletInstances)->GetElementCount());
                pass.Set(pass_indirect_cull_triangle::region_cap, GetBuffer(Renderer_Buffer::VisibleTriangles)->GetElementCount() / 2);
                pass.Set(pass_indirect_cull_triangle::late, late);

                pass.DispatchIndirect(Renderer_Buffer::TriangleDispatchArgs, late ? 2u * static_cast<uint32_t>(sizeof(Sb_IndirectDispatchArgs)) : 0u);
            }

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
        RenderPass pass("cull_phase", Renderer_Shader::cull_phase_c);
        pass.Bind(Renderer_BindingsUav::cull_phase, Renderer_Buffer::CullPhase);
        pass.Bind(Renderer_BindingsUav::triangle_dispatch_args, Renderer_Buffer::TriangleDispatchArgs);
        pass.Bind(Renderer_BindingsUav::indirect_draw_args, Renderer_Buffer::IndirectDrawArgs);
        pass.Set(pass_cull_phase::mode, mode);
        pass.Dispatch(1, 1, 1);
    }

    void Renderer::Pass_Cull_Early()
    {
        // test against last frame's complete depth, draw the survivors, then build the hi-z the late cull tests against
        m_pass_state.cull_late_active = false;

        RenderPass pass("cull_early");
        Pass_IndirectCull(false);
        Pass_TessellatedCull(false);
        Pass_Visibility(false);
        if (cvar_hiz_occlusion.GetValueAs<bool>() && !m_is_hiz_suppressed)
        {
            Pass_HiZ_BuildFromDepth(GetRenderTarget(Renderer_RenderTarget::gbuffer_depth_hiz_early));
        }
    }

    void Renderer::Pass_Cull_Late()
    {
        // recover what last frame's depth wrongly rejected, then keep the complete depth as next frame's history
        if (!cvar_hiz_occlusion.GetValueAs<bool>() || m_is_hiz_suppressed)
        {
            m_pass_state.hiz_history_valid = false;
            return;
        }

        RenderPass pass("cull_late");
        m_pass_state.cull_late_active = true;
        Pass_IndirectCull(true);
        Pass_TessellatedCull(true);
        Pass_Visibility(true);
        Pass_HiZ_BuildFromDepth(GetRenderTarget(Renderer_RenderTarget::gbuffer_depth_hiz));
        m_pass_state.hiz_history_valid = true;
    }

    void Renderer::Pass_Visibility(bool late)
    {
        if (!late)
        {
            log_mesh_path_diagnostics();
        }

        RHI_Texture* tex_depth      = GetRenderTarget(Renderer_RenderTarget::gbuffer_depth);
        RHI_Texture* tex_visibility = GetRenderTarget(Renderer_RenderTarget::visibility_buffer);

        // new depth history must start at far so disocclusion fails closed, depth clears are graphics queue only
        if (!m_pass_state.depth_history_cleared)
        {
            if (RHI_Texture* depth_prev = GetRenderTarget(Renderer_RenderTarget::gbuffer_depth_previous))
            {
                RHI_CommandList::ClearTexture(depth_prev, Color::standard_white, 1.0f);
            }
            m_pass_state.depth_history_cleared = true;
        }

        const bool is_wireframe               = IsViewWireframe();
        const bool xr_multiview               = IsStereoView();
        const bool vis_path                   = use_visibility_buffer();
        const RHI_RasterizerMode raster_mode  = is_wireframe ? RHI_RasterizerMode::Wireframe : RHI_RasterizerMode::Solid;

        RenderPass outer_pass(vis_path ? (late ? "visibility_pass_late" : "visibility_pass") : (late ? "depth_prepass_late" : "depth_prepass"));

        // two draws over the split survivor list: opaque and alpha-tested (plus batched impostors on the mesh path)
        // visibility path writes both depth and the 32-bit visibility_buffer id once so material_resolve skips re-rasterization
        // legacy fallback writes depth only and lets Pass_GBuffer_Indirect shade geometry in a second raster pass
        {
            const bool mesh_path           = use_mesh_shaders();
            const uint32_t arg_stride      = static_cast<uint32_t>(sizeof(Sb_IndirectDrawArgs));
            const uint32_t dispatch_stride = static_cast<uint32_t>(sizeof(Sb_IndirectDispatchArgs));
            const uint32_t slot_opaque     = late ? 2u : 0u;
            const uint32_t slot_alpha      = late ? 3u : 1u;

            const RHI_PrimitiveTopology topology = GetSecondaryViewMode() == Renderer_SecondaryViewMode::Vertices ? RHI_PrimitiveTopology::PointList : RHI_PrimitiveTopology::TriangleList;
            RHI_Texture* vrs_texture             = (!vis_path && cvar_variable_rate_shading.GetValueAs<bool>() && !cvar_restir_pt.GetValueAs<bool>() && !IsSecondaryViewActive())
                                                   ? GetRenderTarget(Renderer_RenderTarget::shading_rate)
                                                   : nullptr;

            auto configure_pass = [&](RenderPass& pass, const char* pass_name, RHI_Shader* primary_shader, RHI_Shader* pixel_shader, const Color& clear_vis, const float clear_depth)
            {
                pass.Name(pass_name);
                pass.Shaders(primary_shader, pixel_shader);
                pass.Rasterizer(raster_mode);
                pass.Topology(topology);
                pass.Blend(RHI_BlendMode::Off);
                pass.Depth(RHI_DepthMode::ReadWrite);
                pass.Vrs(vrs_texture);
                if (vis_path)
                {
                    pass.ColorTarget(tex_visibility, clear_vis);
                }
                pass.DepthTarget(tex_depth, clear_depth);
                pass.ResolutionScale(true);
                pass.Multiview(xr_multiview);
            };

            // the early pass clears depth (and visibility_buffer on the visibility path) unconditionally so
            // downstream passes still see fresh targets when no opaque geometry is visible
            {
                RenderPass pass(vis_path ? "visibility_opaque" : "depth_prepass_opaque");
                RHI_Shader* opaque_primary = vis_path
                    ? GetShader(late ? Renderer_Shader::meshlet_mesh_visibility_late_m : Renderer_Shader::meshlet_mesh_visibility_m)
                    : (mesh_path
                        ? GetShader(late ? Renderer_Shader::meshlet_mesh_depth_late_m : Renderer_Shader::meshlet_mesh_depth_m)
                        : GetShader(Renderer_Shader::depth_prepass_indirect_v));
                RHI_Shader* opaque_pixel = vis_path ? GetShader(Renderer_Shader::visibility_opaque_p) : nullptr;
                configure_pass(
                    pass,
                    vis_path ? "visibility_mesh" : (mesh_path ? "depth_prepass_mesh" : "depth_prepass_indirect"),
                    opaque_primary,
                    opaque_pixel,
                    late ? rhi_color_load : Color::standard_transparent,
                    late ? rhi_depth_load : 0.0f
                );

                if (m_indirect_draw_count > 0)
                {
                    pass.CullMode(RHI_CullMode::None);
                    if (mesh_path)
                    {
                        bind_mesh_shader_geometry(pass);
                        set_mesh_draw_constants(pass);
                        pass.DrawMeshTasksIndirect(Renderer_Buffer::TriangleDispatchArgs, (MESH_GRID_ARGS_SLOT + slot_opaque) * dispatch_stride);
                    }
                    else
                    {
                        bind_visible_triangle_geometry(pass);
                        pass.Set(pass_visible_triangles::region_base, 0u);
                        pass.Set(pass_visible_triangles::late, late);
                        pass.DrawIndirect(Renderer_Buffer::IndirectDrawArgs, slot_opaque * arg_stride);
                    }
                }
            }

            // alpha-tested half, alpha-test pixel shader discards cutout texels, depth/visibility already cleared so load them
            if (m_indirect_draw_count > 0 && m_has_alpha_indirect)
            {
                RenderPass pass(vis_path ? "visibility_alpha" : "depth_prepass_alpha");
                RHI_Shader* alpha_primary = vis_path
                    ? GetShader(late ? Renderer_Shader::meshlet_mesh_visibility_alpha_late_m : Renderer_Shader::meshlet_mesh_visibility_alpha_m)
                    : (mesh_path
                        ? GetShader(late ? Renderer_Shader::meshlet_mesh_depth_alpha_late_m : Renderer_Shader::meshlet_mesh_depth_alpha_m)
                        : GetShader(Renderer_Shader::depth_prepass_indirect_v));
                RHI_Shader* alpha_pixel = vis_path
                    ? GetShader(Renderer_Shader::visibility_alpha_p)
                    : (mesh_path
                        ? GetShader(Renderer_Shader::depth_prepass_mesh_alpha_p)
                        : GetShader(Renderer_Shader::depth_prepass_indirect_alpha_test_p));
                configure_pass(
                    pass,
                    vis_path ? "visibility_mesh" : (mesh_path ? "depth_prepass_mesh" : "depth_prepass_indirect"),
                    alpha_primary,
                    alpha_pixel,
                    rhi_color_load,
                    rhi_depth_load
                );
                pass.CullMode(RHI_CullMode::None);
                if (mesh_path)
                {
                    bind_mesh_shader_geometry(pass);
                    set_mesh_draw_constants(pass);
                    // Alpha work is compacted separately, with the original capacity for each category.
                    pass.DrawMeshTasksIndirect(Renderer_Buffer::TriangleDispatchArgs, (MESH_GRID_ARGS_SLOT + slot_alpha) * dispatch_stride);
                }
                else
                {
                    bind_visible_triangle_geometry(pass);
                    pass.Set(pass_visible_triangles::region_base, GetBuffer(Renderer_Buffer::VisibleTriangles)->GetElementCount() / 2);
                    pass.Set(pass_visible_triangles::late, late);
                    pass.DrawIndirect(Renderer_Buffer::IndirectDrawArgs, slot_alpha * arg_stride);
                }
            }

            // Batched impostor quads: 16 quads per 64-thread workgroup, written directly by Phase A
            if (m_indirect_draw_count > 0 && mesh_path && m_has_impostor_indirect)
            {
                RenderPass pass(vis_path ? "visibility_impostor" : "depth_prepass_impostor");
                RHI_Shader* impostor_mesh = GetShader(vis_path
                    ? (late ? Renderer_Shader::impostor_mesh_visibility_late_m : Renderer_Shader::impostor_mesh_visibility_m)
                    : (late ? Renderer_Shader::impostor_mesh_depth_late_m : Renderer_Shader::impostor_mesh_depth_m));
                RHI_Shader* impostor_pixel = vis_path
                    ? GetShader(Renderer_Shader::visibility_alpha_p)
                    : GetShader(Renderer_Shader::depth_prepass_mesh_alpha_p);
                configure_pass(
                    pass,
                    vis_path ? "visibility_impostor_mesh" : "depth_prepass_impostor_mesh",
                    impostor_mesh,
                    impostor_pixel,
                    rhi_color_load,
                    rhi_depth_load
                );
                pass.CullMode(RHI_CullMode::None);
                bind_mesh_shader_geometry(pass);
                pass.Bind(98u, GetBuffer(Renderer_Buffer::ImpostorInstances));
                set_mesh_draw_constants(pass);
                pass.DrawMeshTasksIndirect(Renderer_Buffer::TriangleDispatchArgs, (IMPOSTOR_GRID_ARGS_SLOT + (late ? 1u : 0u)) * dispatch_stride);
            }
        }

        // cpu-driven tessellated path (only tessellated still uses cpu draws, indirect path covers everything else)
        // the cpu fallback has no late half, the early pass drew all of it
        if (!late || m_tessellated_gpu_culled)
        {
            RHI_Texture* vrs_texture = (cvar_variable_rate_shading.GetValueAs<bool>() && !cvar_restir_pt.GetValueAs<bool>() && !IsSecondaryViewActive())
                                       ? GetRenderTarget(Renderer_RenderTarget::shading_rate)
                                       : nullptr;
            bool pipeline_set = false;

            // the gpu culled list holds every tessellated draw, its args carry the visibility and the lod
            vector<ClusterDrawRange> cluster_ranges;
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
                    if (!material->IsTessellated())
                    {
                        continue;
                    }
                }

                if (!pipeline_set)
                {
                    outer_pass.Name(vis_path ? "visibility_tessellated" : "depth_prepass_tessellated");
                    outer_pass.Shaders(Renderer_Shader::depth_prepass_v, Renderer_Shader::tessellation_h, Renderer_Shader::tessellation_d);
                    outer_pass.Rasterizer(raster_mode);
                    outer_pass.Blend(RHI_BlendMode::Off);
                    outer_pass.Depth(RHI_DepthMode::ReadWrite);
                    outer_pass.Vrs(vrs_texture);
                    outer_pass.DepthTarget(tex_depth, rhi_depth_load);
                    outer_pass.ResolutionScale(true);
                    outer_pass.Multiview(xr_multiview);
                    pipeline_set = true;
                }

                outer_pass.DrawIndex(draw_call.draw_data_index);
                outer_pass.Transparent(false);
                outer_pass.MaterialIndex(material->GetIndex());
                outer_pass.Set(pass_depth_prepass::has_albedo, material->HasTextureOfType(MaterialTextureType::Color));

                RHI_CullMode cull_mode = is_wireframe ? RHI_CullMode::None : static_cast<RHI_CullMode>(material->GetProperty(MaterialProperty::CullMode));
                outer_pass.CullMode(cull_mode);
                RHI_Buffer* instance_buffer = GeometryBuffer::GetInstanceBuffer() ? GeometryBuffer::GetInstanceBuffer() : GetBuffer(Renderer_Buffer::DummyInstance);
                outer_pass.VertexBuffer(render->GetVertexBuffer(), instance_buffer);
                outer_pass.IndexBuffer(render->GetIndexBuffer());

                if (m_tessellated_gpu_culled)
                {
                    outer_pass.DrawIndexedIndirect(Renderer_Buffer::TessellatedArgs, (args_base + i) * sizeof(Sb_IndirectDrawArgs));
                }
                else if (!draw_view_clusters(outer_pass, render, draw_call, GetViewCamera(), GetResolutionRender().y, cluster_ranges))
                {
                    outer_pass.DrawIndexed(
                        render->GetIndexCount(),
                        render->GetIndexOffset(),
                        render->GetVertexOffset(),
                        render->GetGlobalInstanceOffset() + draw_call.instance_index,
                        draw_call.instance_count
                    );
                }
            }
        }
    }

    void Renderer::Pass_Material_Resolve(const bool clear_targets, const bool scatter_cleared)
    {
        if (!use_visibility_buffer())
        {
            Pass_GBuffer_Indirect(clear_targets);
            return;
        }

        RHI_Texture* tex_visibility = GetRenderTarget(Renderer_RenderTarget::visibility_buffer);
        RHI_Texture* tex_albedo     = GetRenderTarget(Renderer_RenderTarget::gbuffer_color);
        RHI_Texture* tex_normal     = GetRenderTarget(Renderer_RenderTarget::gbuffer_normal);
        RHI_Texture* tex_material   = GetRenderTarget(Renderer_RenderTarget::gbuffer_material);
        RHI_Texture* tex_velocity   = GetRenderTarget(Renderer_RenderTarget::gbuffer_velocity);
        RHI_Texture* tex_emissive   = GetRenderTarget(Renderer_RenderTarget::gbuffer_emissive);

        const Vector2 res           = GetResolutionRender();
        const float   scale         = GetResolutionScale();
        const uint32_t width_active = max(static_cast<uint32_t>(floorf(res.x * scale)), 1u);
        const uint32_t height_active= max(static_cast<uint32_t>(floorf(res.y * scale)), 1u);
        const uint32_t tiles_x      = (width_active + VIS_TILE_SIZE - 1u) / VIS_TILE_SIZE;
        const uint32_t tiles_y      = (height_active + VIS_TILE_SIZE - 1u) / VIS_TILE_SIZE;
        const uint32_t tile_stride  = tiles_x * tiles_y;

        RHI_Buffer* buf_tile_args   = GetBuffer(Renderer_Buffer::VisibilityTileArgs);
        RHI_Buffer* buf_tiles       = EnsureBufferCapacity(Renderer_Buffer::VisibilityTiles, sizeof(uint32_t), VIS_CATEGORY_COUNT * tile_stride, "visibility_tiles");

        const Sb_IndirectDispatchArgs zero_args[VIS_CATEGORY_COUNT] =
        {
            { 0u, 1u, 1u },
            { 0u, 1u, 1u },
            { 0u, 1u, 1u }
        };
        RHI_CommandList::UpdateBuffer(buf_tile_args, 0, sizeof(zero_args), zero_args, false);

        const uint32_t meshlet_half_cap = GetBuffer(Renderer_Buffer::MeshletInstances)->GetElementCount() >> 1u;
        RenderPass outer_pass("material_resolve");

        // 1) Classify 8x8 tiles into opaque / alpha / impostor compact lists, mask scatter_first pixels,
        //    and clear sky/empty G-buffer pixels when no earlier pass cleared them.
        {
            RenderPass pass("visibility_classify", Renderer_Shader::visibility_classify_c);
            pass.Write(80u, tex_albedo);
            pass.Write(81u, tex_normal);
            pass.Write(82u, tex_material);
            pass.Write(83u, tex_velocity);
            pass.Write(84u, tex_emissive);
            pass.Write(85u, tex_visibility);
            pass.Bind(86u, buf_tile_args);
            pass.Bind(87u, buf_tiles);
            pass.Set(pass_visibility::tile_stride,           tile_stride);
            pass.Set(pass_visibility::clear_gbuffer,         clear_targets ? 1u : 0u);
            pass.Set(pass_visibility::scatter_cleared,       scatter_cleared ? 1u : 0u);
            pass.Set(pass_visibility::meshlet_half_capacity, meshlet_half_cap);
            pass.Dispatch(tiles_x, tiles_y, 1u);
        }

        // 2) Specialized per-category material resolve dispatches over only the tiles that contain that category.
        const Renderer_Shader resolve_shaders[VIS_CATEGORY_COUNT] =
        {
            Renderer_Shader::visibility_resolve_opaque_c,
            Renderer_Shader::visibility_resolve_alpha_c,
            Renderer_Shader::visibility_resolve_impostor_c
        };
        const char* resolve_names[VIS_CATEGORY_COUNT] =
        {
            "material_resolve_opaque",
            "material_resolve_alpha",
            "material_resolve_impostor"
        };
        const bool category_active[VIS_CATEGORY_COUNT] =
        {
            m_indirect_draw_count > 0,
            m_has_alpha_indirect,
            m_has_impostor_indirect
        };

        for (uint32_t cat = 0u; cat < VIS_CATEGORY_COUNT; cat++)
        {
            if (!category_active[cat])
            {
                continue;
            }
            RenderPass pass(resolve_names[cat], resolve_shaders[cat]);
            pass.Transparent(false);
            pass.Read(97u, tex_visibility);
            pass.Bind(Renderer_BindingsSrv::gbuffer_depth, Renderer_RenderTarget::gbuffer_depth);
            pass.Write(80u, tex_albedo);
            pass.Write(81u, tex_normal);
            pass.Write(82u, tex_material);
            pass.Write(83u, tex_velocity);
            pass.Write(84u, tex_emissive);
            pass.Bind(96u, buf_tiles);
            bind_mesh_shader_geometry(pass);
            if (cat == VIS_CATEGORY_IMPOSTOR)
            {
                pass.Bind(98u, GetBuffer(Renderer_Buffer::ImpostorInstances));
            }
            pass.Set(pass_visibility::tile_stride,           tile_stride);
            pass.Set(pass_visibility::category,              cat);
            pass.Set(pass_visibility::meshlet_half_capacity, meshlet_half_cap);
            pass.DispatchIndirect(buf_tile_args, cat * sizeof(Sb_IndirectDispatchArgs));
        }
    }

    void Renderer::Pass_GBuffer_Indirect(const bool clear_targets)
    {
        RenderPass outer_pass("g_buffer_indirect");

        const bool xr_multiview              = IsStereoView();
        const bool mesh_path                 = use_mesh_shaders();
        const RHI_RasterizerMode raster_mode = IsViewWireframe() ? RHI_RasterizerMode::Wireframe : RHI_RasterizerMode::Solid;
        const RHI_PrimitiveTopology topology = GetSecondaryViewMode() == Renderer_SecondaryViewMode::Vertices ? RHI_PrimitiveTopology::PointList : RHI_PrimitiveTopology::TriangleList;
        RHI_Texture* vrs_texture             = (cvar_variable_rate_shading.GetValueAs<bool>() && !cvar_restir_pt.GetValueAs<bool>() && !IsSecondaryViewActive())
                                               ? GetRenderTarget(Renderer_RenderTarget::shading_rate)
                                               : nullptr;
        const Color clear_color              = clear_targets ? Color::standard_transparent : rhi_color_load;

        auto configure_gbuffer_pass = [&](RenderPass& pass, const char* pass_name, RHI_Shader* primary_shader, RHI_Shader* pixel_shader, const RHI_DepthMode depth_mode, const Color& color_clear)
        {
            pass.Name(pass_name);
            pass.Shaders(primary_shader, pixel_shader);
            pass.Blend(RHI_BlendMode::Off);
            pass.Rasterizer(raster_mode);
            pass.Topology(topology);
            pass.Depth(depth_mode);
            pass.Vrs(vrs_texture);
            pass.ResolutionScale(true);
            pass.ColorTargets(
                GetRenderTarget(Renderer_RenderTarget::gbuffer_color),
                GetRenderTarget(Renderer_RenderTarget::gbuffer_normal),
                GetRenderTarget(Renderer_RenderTarget::gbuffer_material),
                GetRenderTarget(Renderer_RenderTarget::gbuffer_velocity),
                GetRenderTarget(Renderer_RenderTarget::gbuffer_emissive)
            );
            pass.ClearColor(color_clear);
            pass.DepthTarget(Renderer_RenderTarget::gbuffer_depth, rhi_depth_load);
            pass.Multiview(xr_multiview);
            pass.Transparent(false);
        };

        const uint32_t arg_stride = static_cast<uint32_t>(sizeof(Sb_IndirectDrawArgs));

        // opaque uses greater-equal, an equality test demands bit identical depth from two different mesh
        // shaders, the prepass runs meshlet_mesh_depth_m and this pass runs meshlet_mesh_m, so a one bit
        // disagreement leaves the pixel unshaded, which reads as a hole through the terrain that flickers
        // as taa rejitters the projection, the alpha half below restores equal, it needs it for cutouts
        // the clear runs unconditionally so the transparent ocean composites over a fresh g-buffer when no opaque geometry is visible
        {
            RenderPass pass("g_buffer_indirect_opaque");
            configure_gbuffer_pass(
                pass,
                mesh_path ? "g_buffer_mesh" : "g_buffer_indirect",
                mesh_path ? GetShader(Renderer_Shader::meshlet_mesh_m) : GetShader(Renderer_Shader::gbuffer_indirect_v),
                GetShader(Renderer_Shader::gbuffer_indirect_p),
                RHI_DepthMode::ReadWrite,
                clear_color
            );

            if (m_indirect_draw_count > 0)
            {
                pass.CullMode(RHI_CullMode::None);
                if (mesh_path)
                {
                    bind_mesh_shader_geometry(pass);
                    set_mesh_draw_constants(pass);
                    pass.DrawMeshTasksIndirect(Renderer_Buffer::TriangleDispatchArgs, MESH_GRID_ARGS_SLOT * sizeof(Sb_IndirectDispatchArgs));
                }
                else
                {
                    // slots 0 and 1 span the early and the late survivors together
                    bind_visible_triangle_geometry(pass);
                    pass.Set(pass_visible_triangles::region_base, 0u);
                    pass.Set(pass_visible_triangles::late, false);
                    pass.DrawIndirect(Renderer_Buffer::IndirectDrawArgs, 0);
                }
            }
        }

        if (m_indirect_draw_count > 0 && m_has_alpha_indirect)
        {
            RenderPass pass("g_buffer_indirect_alpha");
            // equal, not greater-equal, so only prepass survivors draw, otherwise cutouts render as solid quads
            RHI_Shader* alpha_primary = mesh_path ? GetShader(Renderer_Shader::meshlet_mesh_alpha_m) : GetShader(Renderer_Shader::gbuffer_indirect_v);
            RHI_Shader* alpha_pixel   = GetShader(Renderer_Shader::gbuffer_indirect_p);
            if (mesh_path)
            {
                if (RHI_Shader* alpha_ps = GetShader(Renderer_Shader::gbuffer_alpha_p); alpha_ps && alpha_ps->IsCompiled())
                {
                    alpha_pixel = alpha_ps;
                }
            }
            configure_gbuffer_pass(
                pass,
                mesh_path ? "g_buffer_mesh" : "g_buffer_indirect",
                alpha_primary,
                alpha_pixel,
                RHI_DepthMode::ReadEqual,
                rhi_color_load
            );
            pass.CullMode(RHI_CullMode::None);
            if (mesh_path)
            {
                bind_mesh_shader_geometry(pass);
                set_mesh_draw_constants(pass);
                pass.DrawMeshTasksIndirect(Renderer_Buffer::TriangleDispatchArgs, (MESH_GRID_ARGS_SLOT + 1) * sizeof(Sb_IndirectDispatchArgs));
            }
            else
            {
                bind_visible_triangle_geometry(pass);
                pass.Set(pass_visible_triangles::region_base, GetBuffer(Renderer_Buffer::VisibleTriangles)->GetElementCount() / 2);
                pass.Set(pass_visible_triangles::late, false);
                pass.DrawIndirect(Renderer_Buffer::IndirectDrawArgs, arg_stride);
            }
        }

        if (m_indirect_draw_count > 0 && mesh_path && m_has_impostor_indirect)
        {
            RenderPass pass("g_buffer_indirect_impostor");
            // Batched impostor quads: 16 quads per 64-thread workgroup, ReadEqual matches depth_prepass_impostor_mesh
            RHI_Shader* impostor_pixel = GetShader(Renderer_Shader::gbuffer_indirect_p);
            if (RHI_Shader* impostor_ps = GetShader(Renderer_Shader::gbuffer_impostor_p); impostor_ps && impostor_ps->IsCompiled())
            {
                impostor_pixel = impostor_ps;
            }
            else if (RHI_Shader* alpha_ps = GetShader(Renderer_Shader::gbuffer_alpha_p); alpha_ps && alpha_ps->IsCompiled())
            {
                impostor_pixel = alpha_ps;
            }
            configure_gbuffer_pass(
                pass,
                "g_buffer_impostor_mesh",
                GetShader(Renderer_Shader::impostor_mesh_m),
                impostor_pixel,
                RHI_DepthMode::ReadEqual,
                rhi_color_load
            );
            pass.CullMode(RHI_CullMode::None);
            bind_mesh_shader_geometry(pass);
            pass.Bind(98u, GetBuffer(Renderer_Buffer::ImpostorInstances));
            set_mesh_draw_constants(pass);
            pass.DrawMeshTasksIndirect(Renderer_Buffer::TriangleDispatchArgs, IMPOSTOR_GRID_ARGS_SLOT * sizeof(Sb_IndirectDispatchArgs));
        }
    }

    void Renderer::Pass_GBuffer_TessellatedAndTransparent(const bool is_transparent_pass)
    {
        RenderPass pass(is_transparent_pass ? "g_buffer_transparent_draw" : "g_buffer_tessellated");

        const bool xr_multiview              = IsStereoView();
        RHI_Shader* gbuffer_v                = GetShader(Renderer_Shader::gbuffer_v);
        RHI_Shader* gbuffer_p                = GetShader(Renderer_Shader::gbuffer_p);
        const RHI_RasterizerMode raster_mode = IsViewWireframe() ? RHI_RasterizerMode::Wireframe : RHI_RasterizerMode::Solid;
        const RHI_PrimitiveTopology topology = GetSecondaryViewMode() == Renderer_SecondaryViewMode::Vertices ? RHI_PrimitiveTopology::PointList : RHI_PrimitiveTopology::TriangleList;
        RHI_Texture* vrs_texture             = (cvar_variable_rate_shading.GetValueAs<bool>() && !cvar_restir_pt.GetValueAs<bool>() && !IsSecondaryViewActive())
                                               ? GetRenderTarget(Renderer_RenderTarget::shading_rate)
                                               : nullptr;

        // both halves of this pass own their depth
        //
        // the tessellated half cannot use equal-z. the indirect path can, because there the prepass and the
        // g buffer rasterize the same vertices through the same transform. here the vertices are generated,
        // the patch is subdivided and displaced by a chain that reads the layer rules and a height map, and
        // it is driven by two separately compiled vertex shaders, depth_prepass_v in the prepass and
        // gbuffer_v here. those agree only to the last bit, and equal-z rejects anything inexact, so a
        // disagreement costs the whole patch its shading
        pass.Name(is_transparent_pass ? "g_buffer_transparent" : "g_buffer_tessellated");
        pass.Blend(RHI_BlendMode::Off);
        pass.Rasterizer(raster_mode);
        pass.Topology(topology);
        pass.Depth(RHI_DepthMode::ReadWrite);
        pass.Vrs(vrs_texture);
        pass.ResolutionScale(true);
        pass.ColorTargets(
            GetRenderTarget(Renderer_RenderTarget::gbuffer_color),
            GetRenderTarget(Renderer_RenderTarget::gbuffer_normal),
            GetRenderTarget(Renderer_RenderTarget::gbuffer_material),
            GetRenderTarget(Renderer_RenderTarget::gbuffer_velocity),
            GetRenderTarget(Renderer_RenderTarget::gbuffer_emissive)
        );
        pass.DepthTarget(Renderer_RenderTarget::gbuffer_depth, rhi_depth_load);
        pass.Multiview(xr_multiview);

        bool pipeline_set         = false;
        RHI_Shader* current_hull  = nullptr;
        RHI_Shader* current_dom   = nullptr;
        // gpu culled tessellated draws all stay in the list, their args carry the visibility and the lod
        vector<ClusterDrawRange> cluster_ranges;
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

            const bool is_tessellated = material->IsTessellated();
            if (is_transparent_pass)
            {
                if (!material->IsTransparent())
                {
                    continue;
                }
                // in the default pipeline, glass and alpha-blended meshes run in the forward transparent pass;
                // only water stays in the deferred transparent g-buffer pass
                if (!IsRestirActive() && material->GetProperty(MaterialProperty::IsWater) == 0.0f)
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
            if (!pipeline_set || current_hull != hull || current_dom != domain)
            {
                current_hull = hull;
                current_dom  = domain;
                pass.Shaders(gbuffer_v, gbuffer_p, hull, domain);
                pipeline_set = true;
            }

            pass.DrawIndex(draw_call.draw_data_index);
            pass.Transparent(is_transparent_pass);
            pass.MaterialIndex(material->GetIndex());
            pass.CullMode(IsViewWireframe() ? RHI_CullMode::None : static_cast<RHI_CullMode>(material->GetProperty(MaterialProperty::CullMode)));
            RHI_Buffer* instance_buffer = GeometryBuffer::GetInstanceBuffer() ? GeometryBuffer::GetInstanceBuffer() : GetBuffer(Renderer_Buffer::DummyInstance);
            pass.VertexBuffer(render->GetVertexBuffer(), instance_buffer);
            pass.IndexBuffer(render->GetIndexBuffer());

            if (!gpu_tessellated && draw_view_clusters(pass, render, draw_call, GetViewCamera(), GetResolutionRender().y, cluster_ranges))
            {
                continue;
            }
            if (gpu_tessellated)
            {
                // a draw lands in at most one of the early and late halves, the other holds zero instances
                pass.DrawIndexedIndirect(Renderer_Buffer::TessellatedArgs, i * sizeof(Sb_IndirectDrawArgs));
                if (m_pass_state.cull_late_active)
                {
                    pass.DrawIndexedIndirect(Renderer_Buffer::TessellatedArgs, (draw_count + i) * sizeof(Sb_IndirectDrawArgs));
                }
            }
            else
            {
                pass.DrawIndexed(
                    render->GetIndexCount(),
                    render->GetIndexOffset(),
                    render->GetVertexOffset(),
                    render->GetGlobalInstanceOffset() + draw_call.instance_index,
                    draw_call.instance_count
                );
            }
        }
    }

    void Renderer::Pass_Transparent(uint32_t eye, uint32_t eye_layer /*= rhi_all_mips*/)
    {
        if (!m_forward_transparents_present)
        {
            return;
        }

        RHI_Shader* shader_v = GetShader(Renderer_Shader::gbuffer_v);
        RHI_Shader* shader_p = GetShader(Renderer_Shader::transparent_p);
        if (!shader_v || !shader_p || !shader_v->IsCompiled() || !shader_p->IsCompiled())
        {
            return;
        }

        RHI_Texture* tex_frame = GetRenderTarget(Renderer_RenderTarget::frame_render);
        RHI_Texture* tex_depth = GetRenderTarget(Renderer_RenderTarget::gbuffer_depth);
        if (!tex_frame || !tex_depth)
        {
            return;
        }

        // Collect visible transparent draws once (m_draw_calls is sorted with transparents at the end).
        // Apply the distance and screen-size culls here so distant frosted windows never trigger a
        // full-screen refraction blit or open an empty forward pass.
        struct TransparentDrawEntry
        {
            uint32_t draw_index;
            float    dist_sq;
        };
        static vector<TransparentDrawEntry> visible_transparent_draws;
        visible_transparent_draws.clear();
        Camera* view_camera      = GetViewCamera();
        const float render_h     = GetResolutionRender().y;
        const bool is_persp      = view_camera && view_camera->GetProjectionType() == Projection_Perspective;
        const float focal        = view_camera ? abs(view_camera->GetProjectionMatrix().m11) * render_h * 0.5f : render_h;
        const float near_plane   = view_camera ? view_camera->GetNearPlane() : 0.1f;
        const Vector3 cam_pos    = view_camera ? view_camera->GetEntity()->GetPosition() : Vector3::Zero;
        const Vector3 cam_fwd    = view_camera ? view_camera->GetEntity()->GetForward() : Vector3::Forward;

        bool needs_refraction_source = Renderer::view().frame.rain_vehicle.x > 0.0f || Renderer::view().frame.rain_car_atlas.w > 0.0f;
        uint32_t lo = 0;
        uint32_t hi = m_draw_call_count;
        while (lo < hi)
        {
            const uint32_t mid = lo + (hi - lo) / 2;
            if (m_draw_calls[mid].is_transparent)
            {
                hi = mid;
            }
            else
            {
                lo = mid + 1;
            }
        }
        for (uint32_t i = lo; i < m_draw_call_count; i++)
        {
            const Renderer_DrawCall& draw_call = m_draw_calls[i];
            if (!draw_call.is_transparent || !draw_call.camera_visible)
            {
                continue;
            }
            Material* material = draw_call.render->GetMaterial();
            if (!material || material->GetProperty(MaterialProperty::IsWater) != 0.0f || material->GetProperty(MaterialProperty::IsSkidMark) != 0.0f)
            {
                continue;
            }
            const BoundingBox& box = draw_call.render->GetBoundingBox();
            const float dist_sq    = Vector3::DistanceSquared(box.GetCenter(), cam_pos);
            if (is_persp)
            {
                if (dist_sq > 120.0f * 120.0f)
                {
                    continue;
                }
                if (dist_sq > 25.0f * 25.0f)
                {
                    const float radius = box.GetExtents().Length();
                    if (radius * 2.0f * focal < 8.0f * sqrt(dist_sq))
                    {
                        continue;
                    }
                }
            }
            visible_transparent_draws.push_back({ i, dist_sq });
            if (!needs_refraction_source &&
                material->GetProperty(MaterialProperty::AlphaBlend) <= 0.0f &&
                (material->GetProperty(MaterialProperty::ThinWalled) == 0.0f || material->GetProperty(MaterialProperty::Roughness) > 0.06f))
            {
                needs_refraction_source = true;
            }
        }
        if (visible_transparent_draws.empty())
        {
            return;
        }

        RHI_Texture* tex_refraction = GetRenderTarget(Renderer_RenderTarget::frame_render_opaque);
        if (needs_refraction_source && tex_refraction)
        {
            RHI_CommandList::Blit(tex_frame, tex_refraction, false);
        }

        RenderPass pass("transparent_forward");
        pass.Blend(RHI_BlendMode::DualSourceTransmittance);
        pass.Rasterizer(IsViewWireframe() ? RHI_RasterizerMode::Wireframe : RHI_RasterizerMode::Solid);
        pass.Topology(GetSecondaryViewMode() == Renderer_SecondaryViewMode::Vertices ? RHI_PrimitiveTopology::PointList : RHI_PrimitiveTopology::TriangleList);
        pass.Depth(RHI_DepthMode::ReadGreaterEqual);
        pass.ResolutionScale(true);
        pass.ColorTarget(tex_frame, rhi_color_load);
        pass.DepthTarget(tex_depth, rhi_depth_load);
        pass.ArrayIndex(eye_layer == rhi_all_mips ? 0 : eye_layer);
        pass.Multiview(false);

        RHI_Texture* tex_skysphere = GetRenderTarget(Renderer_RenderTarget::skysphere);
        RHI_Texture* tex_sky_sh    = GetRenderTarget(Renderer_RenderTarget::sky_sh);
        RHI_Texture* tex_black     = GetStandardTexture(Renderer_StandardTexture::Black);

        pass.Bind(Renderer_BindingsSrv::tex,  (needs_refraction_source && tex_refraction) ? tex_refraction : tex_black);
        pass.Bind(Renderer_BindingsSrv::tex2, Renderer_RenderTarget::shadow_atlas);
        pass.Bind(Renderer_BindingsSrv::tex3, Renderer_RenderTarget::lut_brdf_specular);
        pass.Bind(Renderer_BindingsSrv::tex4, Renderer_RenderTarget::gbuffer_depth_opaque_output);
        pass.Bind(Renderer_BindingsSrv::tex5, tex_skysphere);
        pass.Bind(Renderer_BindingsSrv::tex6, Renderer_RenderTarget::cloud_shadow);
        pass.Bind("tex_transparent_sky_sh", tex_sky_sh ? tex_sky_sh : tex_black);
        pass.Bind("tex_fog_extinction", Renderer_RenderTarget::fog_extinction);
        pass.Bind("tex_fog_air_source", m_pass_state.fog_source);
        pass.Bind("tex_fog_water_source", m_pass_state.fog_water_source);
        pass.Bind("tex_fog_scattering", Renderer_RenderTarget::fog_integrated);
        pass.Bind("tex_fog_transmittance", Renderer_RenderTarget::fog_transmittance);
        pass.Bind("tex_car_rain_surface", Renderer_RenderTarget::car_rain_surface);
        pass.Bind("tex_car_rain_micro", Renderer_RenderTarget::car_rain_micro);
        pass.Bind("tex_car_rain_ids", Renderer_RenderTarget::car_rain_ids);
        pass.Bind("car_rain_drops", Renderer_Buffer::CarRainDrops);
        pass.Bind(Renderer_BindingsUav::cluster_light_grid, Renderer_Buffer::ClusterLightGrid);
        pass.Bind(Renderer_BindingsUav::cluster_light_indices, Renderer_Buffer::ClusterLightIndices);

        bool use_rt_reflections = false;
        if (RHI_Device::Caps().ray_tracing)
        {
            pass.Bind("radiance_cache", Renderer_Buffer::RadianceCache);
            if (RHI_AccelerationStructure* tlas = GetTopLevelAccelerationStructure())
            {
                if (tlas->GetRhiResource())
                {
                    pass.Bind(Renderer_BindingsSrv::tlas, tlas);
                    pass.Bind(Renderer_BindingsUav::geometry_info, Renderer_Buffer::GeometryInfo);
                    use_rt_reflections = cvar_ray_traced_reflections.GetValueAs<bool>() && !IsSecondaryViewActive() && !m_pass_state.skip_rt_trace;
                }
            }
        }

        pass.Eye(IsStereoView() ? eye : 0);
        pass.Transparent(true);
        pass.Set(pass_transparent::mip_count, tex_skysphere ? static_cast<float>(tex_skysphere->GetMipCount()) : 1.0f);
        pass.Set(pass_transparent::light_count, m_count_shading_lights);
        pass.Set(pass_transparent::ray_traced_reflections, use_rt_reflections);
        pass.Set(pass_transparent::has_refraction_source, needs_refraction_source && tex_refraction != nullptr);
        pass.Set(pass_transparent::apply_fog, !IsSecondaryViewActive());

        bool pipeline_set        = false;
        RHI_Shader* current_hull = nullptr;
        RHI_Shader* current_dom  = nullptr;
        static vector<ClusterDrawRange> cluster_ranges;
        RHI_Buffer* instance_buffer = GeometryBuffer::GetInstanceBuffer() ? GeometryBuffer::GetInstanceBuffer() : GetBuffer(Renderer_Buffer::DummyInstance);
        RHI_Buffer* last_vb = nullptr;
        RHI_Buffer* last_ib = nullptr;
        RHI_CullMode last_cull_mode = RHI_CullMode::Max;

        auto issue_draw = [&](Render* render, const Renderer_DrawCall& draw_call, bool has_clusters)
        {
            if (has_clusters)
            {
                const uint32_t instance_offset = render->GetGlobalInstanceOffset() + draw_call.instance_index;
                for (const ClusterDrawRange& range : cluster_ranges)
                {
                    pass.DrawIndexed(range.index_count, range.first_index, range.vertex_offset, instance_offset, draw_call.instance_count);
                }
                return;
            }
            pass.DrawIndexed(
                render->GetIndexCount(),
                render->GetIndexOffset(),
                render->GetVertexOffset(),
                render->GetGlobalInstanceOffset() + draw_call.instance_index,
                draw_call.instance_count
            );
        };

        for (const TransparentDrawEntry& entry : visible_transparent_draws)
        {
            const Renderer_DrawCall& draw_call = m_draw_calls[entry.draw_index];
            Render* render     = draw_call.render;
            Material* material = render->GetMaterial();
            const float dist_sq = entry.dist_sq;

            const bool is_tessellated = material->IsTessellated();
            RHI_Shader* hull          = is_tessellated ? GetShader(Renderer_Shader::tessellation_h) : nullptr;
            RHI_Shader* domain        = is_tessellated ? GetShader(Renderer_Shader::tessellation_d) : nullptr;
            if (!pipeline_set || current_hull != hull || current_dom != domain)
            {
                current_hull = hull;
                current_dom  = domain;
                pass.Shaders(shader_v, shader_p, hull, domain);
                pipeline_set   = true;
                last_vb        = nullptr;
                last_ib        = nullptr;
                last_cull_mode = RHI_CullMode::Max;
            }

            pass.DrawIndex(draw_call.draw_data_index);
            pass.MaterialIndex(material->GetIndex());

            RHI_Buffer* vb = render->GetVertexBuffer();
            RHI_Buffer* ib = render->GetIndexBuffer();
            if (vb != last_vb)
            {
                pass.VertexBuffer(vb, instance_buffer);
                last_vb = vb;
            }
            if (ib != last_ib)
            {
                pass.IndexBuffer(ib);
                last_ib = ib;
            }

            // Resolve virtual geometry cluster ranges once per draw (skip past 10 m where root LOD suffices for glass).
            bool has_clusters = false;
            if (dist_sq < 10.0f * 10.0f)
            {
                if (Mesh* mesh = render->GetMesh(); mesh && !mesh->GetSubMesh(render->GetSubMeshIndex()).HasResidentRootOnly() && !mesh->GetSubMesh(render->GetSubMeshIndex()).clusters.empty())
                {
                    has_clusters = cluster_draw_ranges(*mesh, render->GetSubMeshIndex(), render->GetEntity()->GetMatrix(),
                        [&](const BoundingBox& world)
                        {
                            const float depth = is_persp
                                ? max(near_plane, Vector3::Dot(world.GetCenter() - cam_pos, cam_fwd) - world.GetExtents().Length())
                                : 1.0f;
                            return focal / depth;
                        }, cluster_ranges);
                }
            }

            const RHI_CullMode cull_mode = IsViewWireframe() ? RHI_CullMode::None : static_cast<RHI_CullMode>(material->GetProperty(MaterialProperty::CullMode));
            if (cull_mode == RHI_CullMode::None && !IsViewWireframe() && dist_sq < 20.0f * 20.0f)
            {
                // draw backfaces before frontfaces so nearby curved/enclosed glass shells layer far walls behind near walls
                if (last_cull_mode != RHI_CullMode::Front)
                {
                    pass.CullMode(RHI_CullMode::Front);
                }
                issue_draw(render, draw_call, has_clusters);
                pass.CullMode(RHI_CullMode::Back);
                last_cull_mode = RHI_CullMode::Back;
                issue_draw(render, draw_call, has_clusters);
            }
            else
            {
                if (last_cull_mode != cull_mode)
                {
                    pass.CullMode(cull_mode);
                    last_cull_mode = cull_mode;
                }
                issue_draw(render, draw_call, has_clusters);
            }
        }
    }

    void Renderer::Pass_SkidMarks()
    {
        // Blend rubber into the receiver before lighting. Zero coverage is a true no-op,
        // and crossing ribbons never replace depth, normals, velocity or material IDs.
        RHI_PipelineState pso;
        pso.name = "skid_marks";
        pso.shaders[RHI_Shader_Type::Vertex] = GetShader(Renderer_Shader::gbuffer_v);
        pso.shaders[RHI_Shader_Type::Pixel] = GetShader(Renderer_Shader::skid_marks_p);
        pso.blend_mode = RHI_BlendMode::SurfaceAlpha;
        pso.rasterizer_mode = RHI_RasterizerMode::Solid;
        pso.depth_mode = RHI_DepthMode::ReadGreaterEqual;
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
            RHI_Device::GetOrCreatePipeline(pso, pipeline);
            warmed_hash = pso.GetHash();
        }

        std::optional<RenderPass> pass;
        for (uint32_t i = 0; i < m_draw_call_count; ++i)
        {
            const Renderer_DrawCall& draw = m_draw_calls[i];
            Render* render = draw.render;
            Material* material = render->GetMaterial();
            if (!material || !draw.camera_visible || material->GetProperty(MaterialProperty::IsSkidMark) == 0.0f)
                continue;

            if (!pass)
            {
                pass.emplace("skid_marks", vertex_shader, pixel_shader);
                pass->Blend(RHI_BlendMode::SurfaceAlpha);
                pass->Rasterizer(RHI_RasterizerMode::Solid);
                pass->Depth(RHI_DepthMode::ReadGreaterEqual);
                pass->Topology(RHI_PrimitiveTopology::TriangleList);
                pass->ColorTarget(Renderer_RenderTarget::gbuffer_color, rhi_color_load);
                pass->DepthTarget(Renderer_RenderTarget::gbuffer_depth, rhi_depth_load);
                pass->ResolutionScale(true);
                pass->Multiview(IsStereoView());
                pass->CullMode(RHI_CullMode::None);
            }
            pass->DrawIndex(draw.draw_data_index);
            pass->MaterialIndex(material->GetIndex());
            pass->Transparent(false);
            RHI_Buffer* instances = GeometryBuffer::GetInstanceBuffer() ? GeometryBuffer::GetInstanceBuffer() : GetBuffer(Renderer_Buffer::DummyInstance);
            pass->VertexBuffer(render->GetVertexBuffer(), instances);
            pass->IndexBuffer(render->GetIndexBuffer());
            pass->DrawIndexed(render->GetIndexCount(), render->GetIndexOffset(),
                render->GetVertexOffset(), render->GetGlobalInstanceOffset() + draw.instance_index, draw.instance_count);
        }
    }

    void Renderer::Pass_GBuffer(const bool is_transparent_pass, const bool submit_before_scatter)
    {
        {
            std::optional<RenderPass> pass;
            pass.emplace(is_transparent_pass ? "g_buffer_transparent" : "g_buffer");

            if (!is_transparent_pass)
            {
                // scatter that is already populated rasterizes first, it tests against the prepass depth and writes its own,
                // so the opaque pass after it fails the depth test under every blade and chip and never shades the ground
                // they hide, the terrain shader is the most expensive one in the g-buffer and a meadow covers most of it
                const bool scatter_first   = cvar_gbuffer_scatter_first.GetValue();
                const bool scatter_cleared = scatter_first && Pass_Grass_Draw(true);
                Pass_Material_Resolve(!scatter_cleared, scatter_cleared);
                if (submit_before_scatter && !scatter_cleared)
                {
                    // the meshlet g-buffer never reads procedural scatter, submitting it here lets it overlap the
                    // populate compute, only the grass draw and what follows waits for the scatter queue
                    pass.reset();
                    RHI_Device::Submit(RHI_Frame_List::Graphics, nullptr, false);
                    RHI_Device::Bind(RHI_Frame_List::Graphics);
                    pass.emplace("g_buffer_scatter");
                }
                if (!scatter_first)
                {
                    // the populate compute is still running on its own queue, so the draw that reads grass_instances goes last
                    Pass_Grass_Draw();
                }
            }

            Pass_GBuffer_TessellatedAndTransparent(is_transparent_pass);

            if (!is_transparent_pass)
                Pass_SkidMarks();

            if (!is_transparent_pass)
            {
                // opaque depth blit moved here from the prepass, all opaque geometry including grass has rasterized so the
                // opaque output carries grass occlusion, batch b consumers run after phase 1 so this write is visible to them
                RenderPass blit_pass("g_buffer_depth_blit");
                RHI_Texture* tex_depth        = GetRenderTarget(Renderer_RenderTarget::gbuffer_depth);
                RHI_Texture* tex_depth_output = GetRenderTarget(Renderer_RenderTarget::gbuffer_depth_opaque_output);
                RHI_CommandList::Blit(tex_depth, tex_depth_output, false, Renderer::GetResolutionScale());
            }
        }

        if (!is_transparent_pass && !IsSecondaryViewActive())
        {
            SP_PROFILE_CPU_START("gbuffer_transform_history");
            Entity::CommitTransformHistory();
            SP_PROFILE_CPU_END();
        }
    }

    bool Renderer::HasActiveGpuScatter()
    {
        if (IsSecondaryViewActive() || !Terrain::FindActive())
        {
            return false;
        }
        for (const PassState::GpuScatterSlot& slot : m_pass_state.gpu_scatter)
        {
            if (gpu_scatter_ready(slot))
            {
                return true;
            }
        }
        return false;
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

                const float thin_start = cvar_grass_thin_distance.GetValue() > 0.0f
                    ? min(cvar_grass_thin_distance.GetValue(), grass.params.ring_radii_m[0] * 0.6f)
                    : grass.params.ring_radii_m[0] * 0.5f;

                auto& far       = m_pass_state.grass_far;
                far.prop_mask   = grass.prop_mask;
                far.tint        = Vector4(grass.material->GetProperty(MaterialProperty::ColorR), grass.material->GetProperty(MaterialProperty::ColorG), grass.material->GetProperty(MaterialProperty::ColorB), 1.0f);
                far.fade        = Vector4(thin_start, outer_start, ring_end, grass.params.patch_invert ? -patch_size : patch_size);
                far.gate        = Vector4(biome_min, cosf(grass.params.max_slope_deg * (math::pi / 180.0f)), static_cast<float>(grass.params.ground_mask), static_cast<float>(min(grass.params.mask_channel, 3u)));
                far.patch       = Vector4(grass.params.patch_coverage, grass.params.patch_edge, grass.params.height_min, grass.params.height_max);
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

        static_assert(renderer_max_gpu_scatter_args == 12 && renderer_max_gpu_scatter_slots == 4);
        // per slot lod parameters (z = habitat id, w = sea level), the protected vehicle region, then the cell window of every ring
        std::array<Vector4, renderer_gpu_scatter_parameter_count> detail_parameters{};
        for (uint32_t slot = 0; slot < renderer_max_gpu_scatter_slots; slot++)
        {
            const PassState::GpuScatterSlot& state = m_pass_state.gpu_scatter[slot];
            if (gpu_scatter_ready(state))
            {
                detail_parameters[slot] = Vector4(0.0f, 0.0f, static_cast<float>(state.params.habitat), state.params.sea_level);
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
                    const auto& box = state.mesh->GetSubMesh(0).root.aabb;
                    root_radius = box.GetCenter().Length() + box.GetExtents().Length();
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

                    const float margin     = cull_radius / max(sin_half_min, 0.05f) + 2.0f * cell_size;
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

            // z and w are the distance past which the blades of that ring thin out and the exponent they do it with
            for (uint32_t arg = 0; arg < renderer_max_gpu_scatter_args; arg++)
            {
                const uint32_t slot                    = arg / renderer_max_gpu_scatter_lods;
                const uint32_t lod                     = arg % renderer_max_gpu_scatter_lods;
                const PassState::GpuScatterSlot& state = m_pass_state.gpu_scatter[slot];
                const Vector2 thin                     = gpu_scatter_ready(state) ? gpu_scatter_thin(state, slot, lod, eye) : Vector2(0.0f, 0.0f);
                detail_parameters[renderer_max_gpu_scatter_slots + 1 + arg] = Vector4(static_cast<float>(cell_windows[arg][0]), static_cast<float>(cell_windows[arg][1]), thin.x, thin.y);
            }

        }

        RHI_Buffer* detail_buffer = GetBuffer(Renderer_Buffer::GrassLodParameters);
        RHI_CommandList::UpdateBuffer(detail_buffer, 0, sizeof(detail_parameters), detail_parameters.data(), false);
        RenderPass pass("gpu_scatter_populate");
        RHI_Buffer* buf_instances = GetBuffer(Renderer_Buffer::GrassInstances);
        RHI_Buffer* buf_count     = GetBuffer(Renderer_Buffer::GrassCount);
        RHI_Buffer* buf_args      = GetBuffer(Renderer_Buffer::GrassIndirectArgs);

        const bool hiz_active        = cvar_hiz_occlusion.GetValueAs<bool>() && !m_is_hiz_suppressed;
        RHI_Texture* tex_scatter_hiz = !hiz_active
            ? GetStandardTexture(Renderer_StandardTexture::Black)
            : GetRenderTarget(m_pass_state.cull_late_active ? Renderer_RenderTarget::gbuffer_depth_hiz : Renderer_RenderTarget::gbuffer_depth_hiz_early);

        vector<Sb_IndirectDrawArgs> cluster_args;
        vector<ClusterDrawRange> cluster_ranges;
        for (uint32_t slot = 0; slot < renderer_max_gpu_scatter_slots; slot++)
        {
            PassState::GpuScatterSlot& state = m_pass_state.gpu_scatter[slot];
            if (!gpu_scatter_ready(state)) continue;
            const auto& sub = state.mesh->GetSubMesh(0);
            const auto& root = sub.root;
            const float focal = abs(camera->GetProjectionMatrix().m11) * GetResolutionRender().y * 0.5f;
            state.cluster_counts.fill(0);
            if (!sub.HasResidentRootOnly() && !sub.clusters.empty())
            {
                for (uint32_t ring = 0; ring < renderer_max_gpu_scatter_lods; ++ring)
                {
                    const float inner = ring ? gpu_scatter_ring_radius(state, ring - 1) : camera->GetNearPlane();
                    const float density = focal * max(state.params.size_min, state.params.size_max) /
                        (camera->GetProjectionType() == Projection_Perspective ? max(camera->GetNearPlane(), inner) : 1.0f);
                    if (sub.clusters.size() > 1)
                    {
                        if (cluster_draw_ranges(*state.mesh, 0, Matrix::Identity, [&](const BoundingBox&) { return density; }, cluster_ranges))
                        {
                            state.cluster_offsets[ring] = static_cast<uint32_t>(cluster_args.size());
                            state.cluster_counts[ring] = static_cast<uint32_t>(cluster_ranges.size());
                            for (const auto& range : cluster_ranges)
                                cluster_args.push_back({range.index_count, 0, range.first_index, static_cast<int32_t>(range.vertex_offset), 0});
                        }
                        continue;
                    }
                    ResidentGeometryPage page = {};
                    const bool ready = GeometryBuffer::SelectPage(sub.clusters.front(), density, page);
                    auto& args = state.indirect_args_static[ring];
                    const uint32_t first = ready ? page.indices : state.mesh->GetGlobalIndexOffset() + root.index_offset;
                    const uint32_t base = ready ? page.vertices : state.mesh->GetGlobalVertexOffset() + root.vertex_offset;
                    const uint32_t count = ready ? page.index_count : root.index_count;
                    const uint32_t vertices = ready ? page.vertex_count : root.vertex_count;
                    if (args.first_index != first || args.vertex_offset != static_cast<int32_t>(base) ||
                        args.index_count != count || state.vertex_counts[ring] != vertices)
                    {
                        args.first_index = first;
                        args.vertex_offset = static_cast<int32_t>(base);
                        args.index_count = count;
                        state.vertex_counts[ring] = vertices;
                        state.args_baked = false;
                    }
                }
            }
            if (state.args_baked) continue;

            RHI_CommandList::UpdateBuffer(
                buf_args,
                renderer_gpu_scatter_arg_index(slot, 0) * static_cast<uint32_t>(sizeof(Sb_IndirectDrawArgs)),
                static_cast<uint32_t>(sizeof(Sb_IndirectDrawArgs) * renderer_max_gpu_scatter_lods),
                &state.indirect_args_static[0],
                false
            );
            // the same draw lists for the mesh shader path, which also needs each blade lod's vertex count
            auto mesh_geometry          = [&](const Sb_IndirectDrawArgs& arguments, const uint32_t lod)
            {
                return std::array<uint32_t, 4>{ arguments.first_index, arguments.index_count, static_cast<uint32_t>(arguments.vertex_offset), state.vertex_counts[lod] };
            };
            std::array<std::array<uint32_t, 4>, renderer_max_gpu_scatter_lods> ring_geometry;
            for (uint32_t lod = 0; lod < renderer_max_gpu_scatter_lods; lod++)
            {
                ring_geometry[lod]   = mesh_geometry(state.indirect_args_static[lod], lod);
            }
            RHI_Buffer* buf_mesh_geometry = GetBuffer(Renderer_Buffer::GrassMeshGeometry);
            RHI_CommandList::UpdateBuffer(buf_mesh_geometry,
                renderer_gpu_scatter_arg_index(slot, 0) * sizeof(ring_geometry[0]),
                sizeof(ring_geometry), ring_geometry.data(), false);
            state.args_baked = true;
        }

        RHI_Buffer* cluster_buffer = nullptr;
        if (!cluster_args.empty())
        {
            cluster_buffer = EnsureBufferCapacity(Renderer_Buffer::GrassClusterArgs, sizeof(Sb_IndirectDrawArgs),
                static_cast<uint32_t>(cluster_args.size()), "grass_cluster_args");
            RHI_CommandList::UpdateBuffer(cluster_buffer, 0, cluster_args.size() * sizeof(Sb_IndirectDrawArgs), cluster_args.data(), false);
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
        pass.Shader(Renderer_Shader::grass_populate_c);
        pass.Bind("grass_lod_parameters", detail_buffer);
        pass.Bind("terrain_road_exclusions", road_exclusions);

        for (uint32_t slot = 0; slot < renderer_max_gpu_scatter_slots; slot++)
        {
            const PassState::GpuScatterSlot& state = m_pass_state.gpu_scatter[slot];
            if (!gpu_scatter_ready(state))
            {
                continue;
            }

            pass.Bind(Renderer_BindingsUav::grass_instances, buf_instances);
            pass.Bind(Renderer_BindingsUav::grass_count, buf_count);
            // the populate shader samples the terrain heightmap through the tex slot
            pass.Bind(Renderer_BindingsSrv::tex, state.heightmap);
            // hi-z on tex2 drives the per-instance frustum + occlusion cull, this frame's early depth when populate runs before
            // the late cull, the complete depth after it, black (far) culls nothing when occlusion culling is off
            pass.Bind(Renderer_BindingsSrv::tex2, tex_scatter_hiz);
            // biome prop mask on tex3, the slot picks the channel, black means nothing is suitable
            pass.Bind(Renderer_BindingsSrv::tex3, state.prop_mask ?
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
                pass.TransparentRaw(*reinterpret_cast<const uint32_t*>(&biome_min));
                pass.DrawIndex(lod                 |
                               (slot         << 4)  |
                               (mask_channel << 8)  |
                               (scale_min    << 12) |
                               (scale_max    << 20) |
                               (tilt         << 28));
                pass.MaterialIndex(*reinterpret_cast<const uint32_t*>(&seat_y));
                pass.Set(pass_grass_populate::cell_size, cell_size);
                pass.Set(pass_grass_populate::ring_radius, ring_radius);
                pass.Set(pass_grass_populate::lod_base, lod_base);
                pass.Set(pass_grass_populate::max_instances_per_lod, lod_cap);
                pass.Set(pass_grass_populate::height_min, state.params.height_min);
                pass.Set(pass_grass_populate::height_max, state.params.height_max);
                pass.Set(pass_grass_populate::max_slope_cos, max_slope_cos);
                pass.Set(pass_grass_populate::inner_radius, inner_radius);
                pass.Set(pass_grass_populate::terrain_mapping, terrain_mapping);
                pass.Set(pass_grass_populate::patch_size, patch_push);
                pass.Set(pass_grass_populate::patch_coverage, state.params.patch_coverage);
                pass.Set(pass_grass_populate::patch_edge, state.params.patch_edge);
                pass.Set(pass_grass_populate::ground_mask, state.params.ground_mask);
                pass.Set(pass_grass_populate::min_slope_cos, min_slope_cos);
                pass.Set(pass_grass_populate::slope_bias, state.params.slope_bias);
                pass.Set(pass_grass_populate::height_fade, state.params.height_fade);
                pass.Set(pass_grass_populate::root_radius, 0.0f);
                if (cvar_grass_specialized.GetValue() && state.material &&
                    state.material->GetProperty(MaterialProperty::IsGrassBlade) != 0.0f &&
                    state.material->GetProperty(MaterialProperty::IsWater) == 0.0f &&
                    state.material->GetProperty(MaterialProperty::IsSkidMark) == 0.0f)
                {
                    // Every grass deformation preserves distance from the root, including
                    // tire and chassis bending. Bound any rotation of the actual mesh;
                    // the shader applies the maximum packed instance scale separately.
                    const auto& bounds = state.mesh->GetSubMesh(0).root.aabb;
                    pass.Set(pass_grass_populate::root_radius, bounds.GetCenter().Length() + bounds.GetExtents().Length() + 0.001f);
                }

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
                const uint32_t blades_per_cell = std::max(
                    1u,
                    static_cast<uint32_t>(std::ceil(per_cell))
                );
                pass.Dispatch(groups_x, groups_z, (blades_per_cell + gpu_scatter_blades_per_thread - 1) / gpu_scatter_blades_per_thread);
            }
        }

        // args build, reads grass_count and writes the instance_count of this slot's three entries
        {
            pass.Shader(Renderer_Shader::grass_indirect_args_c, "grass_indirect_args");
            pass.Bind(Renderer_BindingsUav::grass_count, buf_count);
            pass.Bind(Renderer_BindingsUav::grass_indirect_args, buf_args);
            pass.Bind("grass_mesh_geometry", Renderer_Buffer::GrassMeshGeometry);
            pass.Bind("grass_mesh_args", Renderer_Buffer::GrassMeshArgs);

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

                pass.MaterialIndex(0);
                pass.Transparent(false);
                pass.DrawIndex(renderer_gpu_scatter_arg_index(slot, 0));
                for (uint32_t lod = 0; lod < renderer_max_gpu_scatter_lods; lod++)
                {
                    pass.Set(pass_grass_indirect_args::lod_caps + lod, gpu_scatter_slot_cap(state, slot, lod));
                }
                pass.Set(pass_grass_indirect_args::lod_count, renderer_max_gpu_scatter_lods);
                pass.Set(pass_grass_indirect_args::cluster_pass, 0u);
                pass.Bind(Renderer_BindingsUav::grass_indirect_args, buf_args);
                pass.Dispatch(1, 1, 1);
                if (cluster_buffer && (state.cluster_counts[0] || state.cluster_counts[1] || state.cluster_counts[2]))
                {
                    for (uint32_t ring = 0; ring < renderer_max_gpu_scatter_lods; ++ring)
                    {
                        pass.Set(pass_grass_indirect_args::cluster_offsets + ring, state.cluster_offsets[ring]);
                        pass.Set(pass_grass_indirect_args::cluster_counts + ring, state.cluster_counts[ring]);
                    }
                    pass.Set(pass_grass_indirect_args::cluster_pass, 1u);
                    pass.Bind(Renderer_BindingsUav::grass_indirect_args, cluster_buffer);
                    pass.Dispatch(1, 1, 1);
                }
            }
        }
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
            {
                history.fields[i] = make_shared<RHI_Texture>(RHI_Texture_Type::Type2D,
                    grass_interaction_resolution, grass_interaction_resolution, 1, 1,
                    RHI_Format::R16G16B16A16_Float, RHI_Texture_Srv | RHI_Texture_Uav | RHI_Texture_ClearBlit,
                    i == 0 ? "grass_tracks_a" : "grass_tracks_b");
            }
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
            Vector3 start = sample.is_foot ? position : history.wheel_positions[i];
            if (!sample.is_foot)
            {
                const float distance = Vector3::Distance(start, position);
                if (!history.valid || !history.wheel_valid[i] || frame_dt > 0.25f ||
                    distance > min(12.0f, max(1.0f, velocity.Length() * dt * 2.0f + 0.5f)) ||
                    abs(Vector3::Dot(position - start, normal)) > 0.5f)
                {
                    start = position;
                }
            }

            const float width = sample.width;
            if (!isfinite(width) || width <= 0.0f)
            {
                continue;
            }
            auto& contact = contacts[count++];
            contact.start_width        = Vector4(start.x, start.y, start.z, width * 0.5f);
            contact.end_length         = Vector4(position.x, position.y, position.z, 0.22f);
            contact.direction_pressure = Vector4(direction.x, direction.z, sample.pressure, sample.is_foot ? sample.side : 0.0f);
            contact.normal             = Vector4(normal.x, normal.y, normal.z, sample.is_foot ? sample.push : 0.0f);
            history.wheel_positions[i] = position;
            history.wheel_valid[i]     = !sample.is_foot;
        }

        const uint32_t previous = history.current;
        const uint32_t current = 1 - previous;
        history.previous_valid = history.valid;
        history.origins[current] = origin;
        {
            RenderPass pass("grass_interaction", shader);
            if (!history.valid)
                RHI_CommandList::ClearTexture(history.fields[previous].get(), Color(0, 0, 0, 0));
            if (count)
                RHI_CommandList::UpdateBuffer(history.contacts.get(), 0, count * sizeof(GrassWheelContact), contacts.data(), false);
            pass.Bind("grass_wheel_contacts", history.contacts.get());
            pass.Bind(Renderer_BindingsSrv::tex, history.fields[previous].get());
            pass.Bind(Renderer_BindingsUav::tex, history.fields[current].get());
            pass.Set(pass_grass_interaction::origin, origin);
            pass.Set(pass_grass_interaction::cell_size, cell);
            pass.Set(pass_grass_interaction::history_valid, history.valid);
            pass.Set(pass_grass_interaction::origin_previous, history.origins[previous]);
            pass.Set(pass_grass_interaction::contact_count, count);
            pass.Set(pass_grass_interaction::delta_time, dt);
            pass.Set(pass_grass_interaction::track_center, Vector2(center.x, center.z));
            pass.Set(pass_grass_interaction::track_radius, clamp(cvar_grass_track_radius.GetValue(), 1.0f, grass_interaction_size * 0.5f - 12.0f));
            pass.Set(pass_grass_interaction::track_recovery, max(cvar_grass_track_recovery.GetValue(), 0.001f));
            pass.Dispatch(history.fields[current].get());
        }
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
        const bool simulating = Engine::IsFlagSet(EngineMode::Playing);
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
        {
            RenderPass pass("vegetation_interaction", shader);
            if (!state.valid)
                RHI_CommandList::ClearTexture(state.fields[previous].get(), Color(0, 0, 0, 0));
            if (count)
                RHI_CommandList::UpdateBuffer(state.interactors.get(), 0, count * sizeof(VegetationInteractorGpu), interactors.data(), false);
            pass.Bind("vegetation_interactors", state.interactors.get());
            pass.Bind(Renderer_BindingsSrv::tex, state.fields[previous].get());
            pass.Bind(Renderer_BindingsUav::tex, state.fields[current].get());
            pass.Set(pass_vegetation_interaction::origin, state.origins[current]);
            pass.Set(pass_vegetation_interaction::cell_size, cell);
            pass.Set(pass_vegetation_interaction::history_valid, state.valid);
            pass.Set(pass_vegetation_interaction::origin_previous, state.origins[previous]);
            pass.Set(pass_vegetation_interaction::interactor_count, count);
            pass.Set(pass_vegetation_interaction::delta_time, dt);
            pass.Set(pass_vegetation_interaction::frequency, max(cvar_vegetation_frequency.GetValue(), 0.05f));
            pass.Set(pass_vegetation_interaction::damping, clamp(cvar_vegetation_damping.GetValue(), 0.0f, 4.0f));
            pass.Dispatch(state.fields[current].get());
        }
        state.writing = false;
        state.valid   = true;
    }

    bool Renderer::Pass_Grass_Draw(const bool clear_targets)
    {
        // gpu scatter raster, one DrawIndexedIndirect per slot per lod ring, runs once inside the
        // g-buffer pass, shares the geometry stage render pass and sets its own vertex shader that
        // reads grass_instances

        // the instances were populated around the primary camera and belong to no preview subtree
        if (IsSecondaryViewActive())
        {
            return false;
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
            return false;
        }

        // wait for the geometry buffer and the bindless material index, a zero index reads another render's textures
        if (!GeometryBuffer::GetInstanceBuffer())
        {
            return false;
        }

        // matches the rest of the geometry stage so the draw lands in the same render pass
        const bool xr_multiview = IsStereoView();

        RenderPass pass("g_buffer_grass");
        pass.Name("grass_gbuffer");
        pass.Blend(RHI_BlendMode::Off);
        pass.Rasterizer(IsViewWireframe() ? RHI_RasterizerMode::Wireframe : RHI_RasterizerMode::Solid);
        pass.Multiview(xr_multiview);
        pass.ResolutionScale(true);
        // grass is the only geometry rasterized a single time, it owns its depth here so it writes and tests against the opaque geometry
        pass.Depth(RHI_DepthMode::ReadWrite);
        pass.ColorTargets(
            GetRenderTarget(Renderer_RenderTarget::gbuffer_color),
            GetRenderTarget(Renderer_RenderTarget::gbuffer_normal),
            GetRenderTarget(Renderer_RenderTarget::gbuffer_material),
            GetRenderTarget(Renderer_RenderTarget::gbuffer_velocity),
            GetRenderTarget(Renderer_RenderTarget::gbuffer_emissive)
        );
        pass.DepthTarget(Renderer_RenderTarget::gbuffer_depth, rhi_depth_load);

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
        RHI_Shader* opaque_pixel = GetShader(Renderer_Shader::grass_opaque_p);

        // the mesh shader shades every blade vertex exactly once, the instanced draw shades a vertex again whenever it
        // falls out of the post transform cache, which with the heavy grass vertex work costs about a third more
        RHI_Shader* grass_mesh       = GetShader(Renderer_Shader::grass_mesh_m);
        const bool grass_mesh_ready  = cvar_mesh_shaders.GetValueAs<bool>()        &&
                                       RHI_Device::Caps().mesh_shaders        &&
                                       !xr_multiview                               &&
                                       grass_mesh && grass_mesh->IsCompiled();
        RHI_Buffer* buf_mesh_args    = GetBuffer(Renderer_Buffer::GrassMeshArgs);

        const uint32_t arg_stride      = static_cast<uint32_t>(sizeof(Sb_IndirectDrawArgs));
        const uint32_t mesh_arg_stride = static_cast<uint32_t>(sizeof(uint32_t) * 4);

        // when this draw opens the g-buffer, the first pipeline it binds carries the clear, every one after it loads
        bool clear_pending = clear_targets;
        bool cleared       = false;

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

            // Both fine and coarse indirect draws use this pipeline. Larger custom meshes
            // retain their indexed draws when a single instance exceeds the shader's export arrays.
            bool fits_mesh_group = true;
            for (uint32_t ring = 0; ring < renderer_max_gpu_scatter_lods; ++ring)
                fits_mesh_group &= state.vertex_counts[ring] <= GRASS_MESH_MAX_VERTICES &&
                    state.indirect_args_static[ring].index_count / 3 <= GRASS_MESH_MAX_TRIANGLES;

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
                if (grass_specialized && opaque_pixel && opaque_pixel->IsCompiled())
                    shader_pixel = opaque_pixel;
                if (!shader_vertex || !shader_vertex->IsCompiled() || !shader_pixel || !shader_pixel->IsCompiled())
                {
                    continue;
                }
                // card rings stay instanced, the card turns to face the camera in the vertex shader
                const bool mesh_path    = grass_specialized && grass_mesh_ready && fits_mesh_group && !state.cluster_counts[lod];
                const Color clear_color = clear_pending ? Color::standard_transparent : rhi_color_load;
                pass.Shaders(mesh_path ? grass_mesh : shader_vertex, shader_pixel);
                pass.ClearColor(clear_color);
                cleared       = cleared || clear_pending;
                clear_pending = false;
                pass.CullMode(RHI_CullMode::None);
                pass.Bind(Renderer_BindingsUav::grass_instances, buf_instances);
                pass.Bind("grass_bodies", tracks.bodies.get());
                pass.Bind(Renderer_BindingsSrv::tex,
                    tracks.valid ? tracks.fields[tracks.current].get() : fallback);
                pass.Bind(Renderer_BindingsSrv::tex2,
                    tracks.valid ? tracks.fields[1 - tracks.current].get() : fallback);
                if (mesh_path)
                {
                    pass.Bind("grass_mesh_geometry", Renderer_Buffer::GrassMeshGeometry);
                    pass.Bind("grass_mesh_args", buf_mesh_args);
                }
                else
                {
                    pass.VertexBuffer(mesh->GetVertexBuffer(), binding1_instance);
                    pass.IndexBuffer(mesh->GetIndexBuffer());
                }

                // the mesh shader finds its draw list through draw_index, the instanced vertex shader ignores it
                const uint32_t ring_list = renderer_gpu_scatter_arg_index(slot, lod);
                pass.DrawIndex(mesh_path ? ring_list : 0);
                pass.Transparent(false);
                pass.MaterialIndex(state.material->GetIndex());
                pass.Set(pass_grass_draw::uv_patch, state.params.uv_patch);
                pass.Set(pass_grass_draw::lod_base, lod_base);
                pass.Set(pass_grass_draw::blend_reach, max(cvar_scatter_blend_distance.GetValue(), 0.0f));
                for (uint32_t frame = 0; frame < 2; ++frame)
                {
                    const Vector2 origin = tracks.origins[frame == 0 ? tracks.current : 1 - tracks.current];
                    const bool valid     = tracks.valid && (frame == 0 || tracks.previous_valid);
                    const uint32_t slot_tracks = frame == 0 ? pass_grass_draw::tracks : pass_grass_draw::tracks_previous;
                    pass.Set(slot_tracks, Vector4(origin.x, origin.y, 1.0f / grass_interaction_size, valid ? 1.0f : 0.0f));
                }
                pass.Set(pass_grass_draw::body, Vector4(tracks.valid ? 1.0f : 0.0f, body_center.x, body_center.y, contact_radius));

                // an empty ring bakes instance_count 0 into the args so the gpu skips it at near-zero cost
                if (mesh_path)
                {
                    pass.DrawMeshTasksIndirect(buf_mesh_args, ring_list * mesh_arg_stride);
                }
                else
                {
                    if (state.cluster_counts[lod])
                    {
                        RHI_Buffer* ranges = GetBuffer(Renderer_Buffer::GrassClusterArgs);
                        for (uint32_t i = 0; i < state.cluster_counts[lod]; ++i)
                            pass.DrawIndexedIndirect(ranges, (state.cluster_offsets[lod] + i) * arg_stride);
                    }
                    else
                    {
                        pass.DrawIndexedIndirect(buf_args, ring_list * arg_stride);
                    }
                }
            }
        }

        return cleared;
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
            RHI_CommandList::ClearTexture(tex_debug, Color::standard_black);
            return;
        }

        const bool mesh_path = use_mesh_shaders();
        if (mesh_path && (!GetShader(Renderer_Shader::meshlet_visualize_m)->IsCompiled() ||
                          !GetShader(Renderer_Shader::meshlet_visualize_alpha_m)->IsCompiled()))
        {
            return;
        }

        bool xr_multiview = IsStereoView();

        // mode 1/2 color/wireframe by meshlet id, mode 3/4 color/wireframe by post-cull draw id
        bool wireframe                       = (mode == 2 || mode == 4);
        bool color_by_draw_id                = (mode == 3 || mode == 4);
        const RHI_RasterizerMode raster_mode = wireframe ? RHI_RasterizerMode::Wireframe : RHI_RasterizerMode::Solid;

        RenderPass pass("meshlet_visualize",
            mesh_path ? Renderer_Shader::meshlet_visualize_m : Renderer_Shader::meshlet_visualize_v,
            Renderer_Shader::meshlet_visualize_p);
        pass.Blend(RHI_BlendMode::Off);
        pass.Rasterizer(raster_mode);
        // greater_equal mirrors the gbuffer test instead of equal which is fragile under fp drift between two vertex paths
        pass.Depth(RHI_DepthMode::ReadGreaterEqual);
        pass.ResolutionScale(true);
        pass.ColorTarget(tex_debug, Color::standard_black);
        pass.DepthTarget(tex_depth, rhi_depth_load);
        pass.Multiview(xr_multiview);
        // wireframe shows both faces so rear edges of thin meshlets stay visible, solid mode leaves culling to the triangle cull pass
        pass.CullMode(RHI_CullMode::None);

        // draw the opaque half then the alpha half
        const uint32_t arg_stride = static_cast<uint32_t>(sizeof(Sb_IndirectDrawArgs));
        pass.Set(pass_meshlet_visualize::color_by_draw_id, color_by_draw_id);
        if (mesh_path)
        {
            bind_mesh_shader_geometry(pass);
            set_mesh_draw_constants(pass);
            pass.DrawMeshTasksIndirect(Renderer_Buffer::TriangleDispatchArgs, MESH_GRID_ARGS_SLOT * sizeof(Sb_IndirectDispatchArgs));
            pass.ClearColor(0, rhi_color_load);
            pass.Shaders(Renderer_Shader::meshlet_visualize_alpha_m, Renderer_Shader::meshlet_visualize_p);
            pass.CullMode(RHI_CullMode::None);
            pass.DrawMeshTasksIndirect(Renderer_Buffer::TriangleDispatchArgs, (MESH_GRID_ARGS_SLOT + 1) * sizeof(Sb_IndirectDispatchArgs));
        }
        else
        {
            bind_visible_triangle_geometry(pass);
            pass.Set(pass_visible_triangles::late, false);

            pass.Set(pass_visible_triangles::region_base, 0u);
            pass.DrawIndirect(Renderer_Buffer::IndirectDrawArgs, 0);

            pass.Set(pass_visible_triangles::region_base, GetBuffer(Renderer_Buffer::VisibleTriangles)->GetElementCount() / 2);
            pass.DrawIndirect(Renderer_Buffer::IndirectDrawArgs, arg_stride);
        }
    }
}
