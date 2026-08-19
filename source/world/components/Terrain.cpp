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

//= INCLUDES =================================
#include "pch.h"
#include <algorithm>
#include <fstream>
#include <functional>
#include <unordered_set>
#include "Terrain.h"
#include "Render.h"
#include "Physics.h"
#include "Water.h"
#include "Spline.h"
#include "Light.h"
#include "Camera.h"
#include "../Entity.h"
#include "../World.h"
#include "../TerrainSystem.h"
#include "../../rhi/RHI_Texture.h"
#include "../../resource/ResourceCache.h"
#include "../../geometry/Mesh.h"
#include "../../rendering/Material.h"
#include "../../rendering/Color.h"
#include "../../rendering/Renderer.h"
#include "../../geometry/GeometryProcessing.h"
#include "../../core/ThreadPool.h"
#include "../../core/ProgressTracker.h"
#include "../../file_system/FileSystem.h"
#include "../../physics/PhysicsWorld.h"
#include "../../math/Ray.h"
#include "../../math/BoundingBox.h"
#include "../WorldHelpers.h"
SP_WARNINGS_OFF
#include "../io/pugixml.hpp"
SP_WARNINGS_ON
//============================================

//= NAMESPACES ===============
using namespace std;
using namespace spartan::math;
//============================

namespace spartan
{
    namespace
    {
        bool is_entity_or_descendant(Entity* candidate, Entity* root)
        {
            if (!candidate || !root)
            {
                return false;
            }

            return candidate == root || candidate->IsDescendantOf(root);
        }

        bool is_terrain_tile_or_water(Entity* entity)
        {
            if (!entity)
            {
                return false;
            }

            if (entity->GetComponent<Water>())
            {
                return true;
            }

            for (Entity* current = entity; current; current = current->GetParent())
            {
                if (current->GetComponent<Terrain>())
                {
                    return true;
                }
            }

            return false;
        }

        // control points and spawned props are created and placed by the spline itself, moving
        // them from the outside would only be undone on the next regeneration
        bool is_spline_owned(Entity* entity)
        {
            const string& name = entity->GetObjectName();
            return name.find("spline_point_") == 0 || name.find("spline_instance_") == 0;
        }

        bool entity_has_snappable_mesh(Entity* entity)
        {
            if (!entity)
            {
                return false;
            }

            if (entity->GetComponent<Terrain>() || entity->GetComponent<Water>())
            {
                return false;
            }

            // roads and other splines snap via control points, not as a rigid mesh, and whatever
            // the spline spawned is placed by the spline itself, but a hand placed wall parented
            // to a road is ordinary geometry and has to land like everything else
            if (entity->GetComponent<Spline>())
            {
                return false;
            }

            for (Entity* current = entity; current; current = current->GetParent())
            {
                if (is_spline_owned(current))
                {
                    return false;
                }
            }

            Render* render = entity->GetComponent<Render>();
            return render && render->GetMesh();
        }

        void collect_spline_entities(Entity* root, vector<Entity*>& out)
        {
            if (!root)
            {
                return;
            }

            if (root->GetComponent<Spline>())
            {
                out.push_back(root);
            }

            vector<Entity*> descendants;
            root->GetDescendants(&descendants);
            for (Entity* descendant : descendants)
            {
                if (descendant && descendant->GetComponent<Spline>())
                {
                    out.push_back(descendant);
                }
            }
        }

        void collect_snappable_entities(Entity* root, vector<Entity*>& out)
        {
            if (!root)
            {
                return;
            }

            if (entity_has_snappable_mesh(root))
            {
                out.push_back(root);
            }

            vector<Entity*> descendants;
            root->GetDescendants(&descendants);
            for (Entity* descendant : descendants)
            {
                if (entity_has_snappable_mesh(descendant))
                {
                    out.push_back(descendant);
                }
            }
        }

        // topmost entities whose whole subtree missed the snap, moving one of these cannot
        // disturb anything that has already been placed
        void collect_unsnapped_roots(
            Entity* root,
            const unordered_set<Entity*>& snapped,
            const unordered_set<Entity*>& has_snapped_below,
            vector<Entity*>& out
        )
        {
            if (!root || is_terrain_tile_or_water(root))
            {
                return;
            }

            // a spline rides its own path, but a camera or marker parented to it is just cargo
            // and still has to come down with everything else
            if (root->GetComponent<Spline>())
            {
                const uint32_t spline_child_count = root->GetChildrenCount();
                for (uint32_t i = 0; i < spline_child_count; i++)
                {
                    Entity* child = root->GetChildByIndex(i);
                    if (!child || is_spline_owned(child))
                    {
                        continue;
                    }

                    collect_unsnapped_roots(child, snapped, has_snapped_below, out);
                }

                return;
            }

            // everything under something that snapped already came along for the ride
            if (snapped.count(root) > 0)
            {
                return;
            }

            if (has_snapped_below.count(root) == 0)
            {
                out.push_back(root);
                return;
            }

            const uint32_t child_count = root->GetChildrenCount();
            for (uint32_t i = 0; i < child_count; i++)
            {
                collect_unsnapped_roots(root->GetChildByIndex(i), snapped, has_snapped_below, out);
            }
        }

        void apply_surface_alignment(Entity* entity, const Vector3& normal)
        {
            Vector3 forward = entity->GetForward();
            forward.y = 0.0f;
            if (forward.LengthSquared() < epsilon)
            {
                forward = Vector3::Forward;
            }
            else
            {
                forward.Normalize();
            }

            const Quaternion yaw = Quaternion::FromLookRotation(forward, Vector3::Up);
            const Quaternion align = Quaternion::FromRotation(Vector3::Up, normal.Normalized());
            entity->SetRotation(align * yaw);
        }

        // world aabb recomputed here, the cached one lags a tick behind transform changes
        bool get_world_aabb(Entity* entity, BoundingBox& aabb_out)
        {
            Render* render = entity ? entity->GetComponent<Render>() : nullptr;
            if (!render || !render->GetMesh())
            {
                return false;
            }

            aabb_out = render->HasInstancing()
                ? render->GetBoundingBox()
                : render->GetBoundingBoxMesh() * entity->GetMatrix();

            return true;
        }

        // a probe grid coarser than the heightfield steps straight over ridges, and the mesh that
        // lands between two probes ends up under the ground, so follow the grid rather than a
        // fixed spacing, the cap only bites on the kilometre wide ground slabs
        uint32_t footprint_samples(float span, float grid_step)
        {
            const float step     = max(grid_step, 0.5f);
            const uint32_t count = static_cast<uint32_t>(max(span, 0.0f) / step) + 2;
            return min(count, 128u);
        }

        // lowest point of the entity in world space, its origin when it carries no mesh
        float entity_bottom(Entity* entity)
        {
            BoundingBox aabb;
            if (entity && get_world_aabb(entity, aabb))
            {
                return aabb.GetMin().y;
            }

            return entity ? entity->GetPosition().y : 0.0f;
        }

        // drop a vertical ray from the top of the entity, the heightfield is always the floor
        bool find_snap_surface(
            Entity* entity,
            Terrain* terrain,
            const unordered_set<Entity*>& ignored,
            Vector3& position_out,
            Vector3& normal_out
        )
        {
            const Vector3 position = entity->GetPosition();

            float start_y = position.y + 1.0f;
            BoundingBox entity_aabb;
            const bool has_aabb = get_world_aabb(entity, entity_aabb);
            if (has_aabb)
            {
                start_y = max(start_y, entity_aabb.GetMax().y + 0.05f);
            }

            const Vector3 origin(position.x, start_y, position.z);
            const float max_distance = max(start_y - position.y, 1.0f) + 100000.0f;

            bool found = false;
            float best_y = -numeric_limits<float>::max();
            Vector3 best_normal = Vector3::Up;

            // keep the highest surface that is still at or below the entity
            auto consider = [&start_y, &found, &best_y, &best_normal](float hit_y, const Vector3& hit_normal)
            {
                if (hit_y > start_y || hit_y <= best_y)
                {
                    return;
                }

                found       = true;
                best_y      = hit_y;
                best_normal = hit_normal.LengthSquared() > epsilon
                    ? hit_normal.Normalized()
                    : Vector3::Up;
            };

            // static physics first, buildings and props with colliders
            {
                PhysicsRaycastHit physics_hit;
                if (PhysicsWorld::RaycastStatic(
                    origin,
                    Vector3::Down,
                    max_distance,
                    physics_hit,
                    entity
                ))
                {
                    // never rest on a member of the same snap batch, it may still be floating,
                    // and let the heightfield speak for the terrain, its collider can be a rebuild behind
                    const bool usable =
                        ignored.find(physics_hit.entity) == ignored.end() &&
                        !is_terrain_tile_or_water(physics_hit.entity);

                    if (usable)
                    {
                        consider(physics_hit.position.y, physics_hit.normal);
                    }
                }
            }

            // render meshes without physics, same idea as viewport picking
            {
                const Ray ray(origin, Vector3::Down);
                vector<uint32_t> indices;
                vector<RHI_Vertex_PosTexNorTan> vertices;

                for (Entity* candidate : World::GetEntities())
                {
                    if (!candidate || is_entity_or_descendant(candidate, entity))
                    {
                        continue;
                    }

                    // terrain uses the heightfield path below, skip huge tile meshes
                    if (is_terrain_tile_or_water(candidate))
                    {
                        continue;
                    }

                    if (ignored.find(candidate) != ignored.end())
                    {
                        continue;
                    }

                    Render* render = candidate->GetComponent<Render>();
                    if (!render)
                    {
                        continue;
                    }

                    BoundingBox candidate_aabb;
                    if (!get_world_aabb(candidate, candidate_aabb))
                    {
                        continue;
                    }

                    // cheap reject, the box must sit under the ray and reach above the current best
                    if (ray.HitDistance(candidate_aabb) == numeric_limits<float>::infinity() ||
                        candidate_aabb.GetMax().y <= best_y)
                    {
                        continue;
                    }

                    indices.clear();
                    vertices.clear();
                    render->GetGeometry(&indices, &vertices);
                    if (indices.size() < 3 || vertices.empty())
                    {
                        continue;
                    }

                    const Matrix& transform = candidate->GetMatrix();
                    for (size_t i = 0; i + 2 < indices.size(); i += 3)
                    {
                        Vector3 p1(vertices[indices[i]].pos);
                        Vector3 p2(vertices[indices[i + 1]].pos);
                        Vector3 p3(vertices[indices[i + 2]].pos);
                        p1 = p1 * transform;
                        p2 = p2 * transform;
                        p3 = p3 * transform;

                        Vector3 triangle_normal;
                        const float distance = ray.HitDistance(
                            p1, p2, p3, &triangle_normal
                        );
                        if (distance == numeric_limits<float>::infinity())
                        {
                            continue;
                        }

                        consider(start_y - distance, triangle_normal);
                    }
                }
            }

            // the heightfield is the floor, it also lifts entities that ended up buried, probed
            // across the whole footprint so a wide flat mesh never gets half swallowed by a rise
            if (terrain)
            {
                float span_min_x = position.x;
                float span_max_x = position.x;
                float span_min_z = position.z;
                float span_max_z = position.z;
                if (has_aabb)
                {
                    span_min_x = entity_aabb.GetMin().x;
                    span_max_x = entity_aabb.GetMax().x;
                    span_min_z = entity_aabb.GetMin().z;
                    span_max_z = entity_aabb.GetMax().z;
                }

                const TerrainGridMapping mapping = terrain->GetGridMapping();
                const uint32_t samples_x         = footprint_samples(span_max_x - span_min_x, mapping.scale_x);
                const uint32_t samples_z         = footprint_samples(span_max_z - span_min_z, mapping.scale_z);

                float terrain_height = -numeric_limits<float>::max();
                bool has_terrain     = false;

                for (uint32_t iz = 0; iz < samples_z; iz++)
                {
                    for (uint32_t ix = 0; ix < samples_x; ix++)
                    {
                        const float u = (samples_x > 1) ? static_cast<float>(ix) / static_cast<float>(samples_x - 1) : 0.5f;
                        const float v = (samples_z > 1) ? static_cast<float>(iz) / static_cast<float>(samples_z - 1) : 0.5f;
                        const float x = span_min_x + (span_max_x - span_min_x) * u;
                        const float z = span_min_z + (span_max_z - span_min_z) * v;

                        float sampled = 0.0f;
                        if (terrain->SampleHeight(x, z, sampled) && sampled > terrain_height)
                        {
                            terrain_height = sampled;
                            has_terrain    = true;
                        }
                    }
                }

                if (has_terrain && (!found || terrain_height > best_y))
                {
                    found  = true;
                    best_y = terrain_height;

                    // the normal still comes from under the pivot, the highest corner is a poor guide
                    Vector3 terrain_normal = Vector3::Up;
                    if (terrain->SampleNormal(position.x, position.z, terrain_normal))
                    {
                        best_normal = terrain_normal;
                    }
                }
            }

            if (!found)
            {
                return false;
            }

            position_out = Vector3(position.x, best_y, position.z);
            normal_out   = best_normal;
            return true;
        }

        bool snap_mesh_entity(
            Entity* entity,
            Terrain* terrain,
            const unordered_set<Entity*>& ignored,
            float offset
        )
        {
            if (!entity || !entity_has_snappable_mesh(entity))
            {
                return false;
            }

            Vector3 snap_position;
            Vector3 snap_normal = Vector3::Up;
            if (!find_snap_surface(entity, terrain, ignored, snap_position, snap_normal))
            {
                return false;
            }

            // a wide slab has to stay level, tilting a three kilometre plane by a single degree
            // swings its far corner twenty six metres, so only small props follow the slope
            float span = 0.0f;
            BoundingBox pre_alignment_aabb;
            if (get_world_aabb(entity, pre_alignment_aabb))
            {
                const float span_x = pre_alignment_aabb.GetMax().x - pre_alignment_aabb.GetMin().x;
                const float span_z = pre_alignment_aabb.GetMax().z - pre_alignment_aabb.GetMin().z;
                span               = max(span_x, span_z);
                if (span > 50.0f)
                {
                    snap_normal = Vector3::Up;
                }
            }

            // rotate first, tilting moves the lowest point of the mesh
            apply_surface_alignment(entity, snap_normal);

            // rest the base of the mesh on the surface, the pivot is rarely at the bottom
            float pivot_to_bottom = 0.0f;
            BoundingBox aabb;
            if (get_world_aabb(entity, aabb))
            {
                pivot_to_bottom = entity->GetPosition().y - aabb.GetMin().y;
            }

            // a surface resting exactly on the ground loses the depth fight and the terrain shows
            // through it, and the bigger the slab the further away it is seen from, so the gap has
            // to grow with it or the far end starts flickering through
            const float clearance = min(max(span * 0.001f, 0.05f), 0.5f);

            entity->SetPosition(Vector3(
                snap_position.x,
                snap_position.y + pivot_to_bottom + offset + clearance,
                snap_position.z
            ));

            return true;
        }

        bool snap_entity_position(
            Entity* entity,
            Terrain* terrain,
            const unordered_set<Entity*>& ignored,
            float offset
        )
        {
            if (!entity)
            {
                return false;
            }

            Vector3 snap_position;
            Vector3 snap_normal = Vector3::Up;
            if (!find_snap_surface(entity, terrain, ignored, snap_position, snap_normal))
            {
                return false;
            }

            snap_position.y += offset;
            entity->SetPosition(snap_position);
            return true;
        }

        bool snap_spline_to_terrain(
            Entity* entity,
            Terrain* terrain,
            const unordered_set<Entity*>& ignored,
            float offset
        )
        {
            if (!entity)
            {
                return false;
            }

            Spline* spline = entity->GetComponent<Spline>();
            if (!spline)
            {
                return false;
            }

            // dropping only the control points leaves the pivot where it was authored, and every
            // wall, light and camera parented to the road keeps riding it, so land the pivot too,
            // the control points are set in world space right after and do not care where it sits
            if (terrain)
            {
                const Vector3 pivot = entity->GetPosition();
                float pivot_ground  = 0.0f;
                if (terrain->SampleHeight(pivot.x, pivot.z, pivot_ground))
                {
                    entity->SetPosition(Vector3(pivot.x, pivot_ground + offset, pivot.z));
                }
            }

            const uint32_t child_count = entity->GetChildrenCount();
            for (uint32_t i = 0; i < child_count; i++)
            {
                Entity* child = entity->GetChildByIndex(i);
                if (!child)
                {
                    continue;
                }

                if (child->GetObjectName().find("spline_point_") != 0)
                {
                    continue;
                }

                snap_entity_position(child, terrain, ignored, offset);
            }

            // denser samples between points follow the heightfield
            spline->SetConformToTerrain(true);
            if (spline->GetMeshEnabled())
            {
                spline->GenerateRoadMesh();
            }

            // props and lights ride the spline frames, rebuild them onto the new path
            if (spline->HasSpawnedInstances())
            {
                spline->SpawnInstances();
            }

            return true;
        }

        string get_terrain_cache_directory()
        {
            const string& world_path = World::GetFilePath();
            string directory;

            if (!world_path.empty())
            {
                directory = World::GetResourceDirectory(world_path);
            }
            else
            {
                directory = string(ResourceCache::GetProjectDirectory());
            }

            replace(directory.begin(), directory.end(), '\\', '/');
            if (!directory.empty() && directory.back() != '/')
            {
                directory += '/';
            }

            FileSystem::CreateDirectory_(directory);
            return directory;
        }

        string get_terrain_cache_bin_path()
        {
            return get_terrain_cache_directory() + "terrain_cache.bin";
        }

        string get_terrain_mesh_cache_path()
        {
            return get_terrain_cache_directory() + "terrain_mesh_cache.mesh";
        }

        string get_terrain_maps_cache_path()
        {
            return get_terrain_cache_directory() + "terrain_maps_cache.bin";
        }

        // a single mip, rhi_texture::prepareforgpu box downsamples the rest because the analysis
        // maps qualify as material textures, building a chain here would append a second one on
        // top of that and push the mip count past rhi_max_mip_count
        vector<RHI_Texture_Slice> to_single_mip_slice(const vector<uint8_t>& pixels)
        {
            vector<RHI_Texture_Slice> slices(1);
            slices[0].mips.resize(1);
            slices[0].mips[0].bytes.resize(pixels.size());
            memcpy(slices[0].mips[0].bytes.data(), pixels.data(), pixels.size());

            return slices;
        }
    }

    namespace placement
    {
        struct ClusterData
        {
            Vector3 center_position;
            uint32_t center_tri_idx;
        };

        void compute_triangle_data(
            const vector<vector<RHI_Vertex_PosTexNorTan>>& vertices_terrain,
            const vector<vector<uint32_t>>& indices_terrain,
            uint32_t tile_index,
            unordered_map<uint64_t, vector<TriangleData>>& triangle_data_out
        )
        {
            const vector<RHI_Vertex_PosTexNorTan>& vertices_tile = vertices_terrain[tile_index];
            const vector<uint32_t>& indices_tile                 = indices_terrain[tile_index];

            uint32_t triangle_count  = static_cast<uint32_t>(indices_tile.size() / 3);
            auto& tile_triangle_data = triangle_data_out[tile_index];
            tile_triangle_data.resize(triangle_count);

            auto compute_triangle = [&vertices_tile, &indices_tile, &tile_triangle_data](uint32_t start_index, uint32_t end_index)
            {
                for (uint32_t i = start_index; i < end_index; i++)
                {
                    uint32_t idx0 = indices_tile[i * 3];
                    uint32_t idx1 = indices_tile[i * 3 + 1];
                    uint32_t idx2 = indices_tile[i * 3 + 2];

                    Vector3 v0(vertices_tile[idx0].pos[0], vertices_tile[idx0].pos[1], vertices_tile[idx0].pos[2]);
                    Vector3 v1(vertices_tile[idx1].pos[0], vertices_tile[idx1].pos[1], vertices_tile[idx1].pos[2]);
                    Vector3 v2(vertices_tile[idx2].pos[0], vertices_tile[idx2].pos[1], vertices_tile[idx2].pos[2]);

                    Vector3 v1_minus_v0           = v1 - v0;
                    Vector3 v2_minus_v0           = v2 - v0;
                    Vector3 normal                = Vector3::Cross(v1_minus_v0, v2_minus_v0).Normalized();
                    float slope_radians           = acos(Vector3::Dot(normal, Vector3::Up));
                    Quaternion rotation_to_normal = Quaternion::FromRotation(Vector3::Up, normal);
                    Vector3 centroid              = v0 + (v1_minus_v0 + v2_minus_v0) / 3.0f;

                    tile_triangle_data[i] = {
                        normal, v0, v1_minus_v0, v2_minus_v0, slope_radians,
                        min({ v0.y, v1.y, v2.y }), max({ v0.y, v1.y, v2.y }),
                        rotation_to_normal, centroid
                    };
                }
            };

            ThreadPool::ParallelLoop(compute_triangle, triangle_count);
        }

        // the parts of the terrain a scatter rule needs that do not live on the rule itself
        struct ScatterContext
        {
            float sea_local           = 0.0f;   // sea level in tile local y
            float triangle_area       = 312.5f; // square meters, turns density per hectare into a count
            math::Vector3 tile_offset = math::Vector3::Zero;
            function<bool(float, float, TerrainSurfaceSample&)> sample_surface;
        };

        void find_transforms(
            const TerrainScatterLayer& layer,
            const ScatterContext& ctx,
            uint32_t tile_index,
            vector<Matrix>& transforms_out,
            unordered_map<uint64_t, vector<TriangleData>>& triangle_data,
            float* coverage_out = nullptr
        )
        {
            auto it = triangle_data.find(tile_index);
            if (it == triangle_data.end())
            {
                SP_LOG_ERROR("no triangle data found for tile %d", tile_index);
                return;
            }
            vector<TriangleData>& tile_triangle_data = it->second;
            SP_ASSERT(!tile_triangle_data.empty());

            // compute tile bounds using parallel reduction
            uint32_t tri_count_bounds = static_cast<uint32_t>(tile_triangle_data.size());
            uint32_t num_chunks = min(tri_count_bounds, static_cast<uint32_t>(thread::hardware_concurrency()));
            if (num_chunks == 0)
            {
                num_chunks = 1;
            }
            
            struct Bounds { float min_x, max_x, min_z, max_z; };
            vector<Bounds> chunk_bounds(num_chunks, { 
                numeric_limits<float>::max(), numeric_limits<float>::lowest(),
                numeric_limits<float>::max(), numeric_limits<float>::lowest() 
            });
            
            uint32_t chunk_size = (tri_count_bounds + num_chunks - 1) / num_chunks;
            auto parallel_bounds = [&](uint32_t start, uint32_t end)
            {
                for (uint32_t c = start; c < end; c++)
                {
                    uint32_t chunk_start = c * chunk_size;
                    uint32_t chunk_end   = min(chunk_start + chunk_size, tri_count_bounds);
                    Bounds& b            = chunk_bounds[c];
                    
                    for (uint32_t i = chunk_start; i < chunk_end; i++)
                    {
                        const auto& tri = tile_triangle_data[i];
                        b.min_x = min(b.min_x, tri.centroid.x);
                        b.max_x = max(b.max_x, tri.centroid.x);
                        b.min_z = min(b.min_z, tri.centroid.z);
                        b.max_z = max(b.max_z, tri.centroid.z);
                    }
                }
            };
            ThreadPool::ParallelLoop(parallel_bounds, num_chunks);
            
            // merge chunk results
            float tile_min_x = numeric_limits<float>::max();
            float tile_max_x = numeric_limits<float>::lowest();
            float tile_min_z = numeric_limits<float>::max();
            float tile_max_z = numeric_limits<float>::lowest();
            for (const auto& b : chunk_bounds)
            {
                tile_min_x = min(tile_min_x, b.min_x);
                tile_max_x = max(tile_max_x, b.max_x);
                tile_min_z = min(tile_min_z, b.min_z);
                tile_max_z = max(tile_max_z, b.max_z);
            }

            // filter triangles that meet spawn criteria
            const float edge_epsilon = 0.01f;
            float edge_threshold_x   = tile_max_x - edge_epsilon;
            float edge_threshold_z   = tile_max_z - edge_epsilon;

            const float slope_min_rad = layer.slope_min * math::deg_to_rad;
            const float slope_max_rad = layer.slope_max * math::deg_to_rad;
            const float slope_range   = max(slope_max_rad - slope_min_rad, 1e-3f);
            const bool reads_surface  = layer.ground_mask != 0 ||
                                        layer.mask_channel >= 0 ||
                                        layer.curvature_influence  != 0.0f ||
                                        layer.flow_influence       != 0.0f ||
                                        layer.occlusion_influence  != 0.0f ||
                                        layer.insolation_influence != 0.0f ||
                                        layer.wear_influence       != 0.0f ||
                                        layer.deposition_influence != 0.0f ||
                                        layer.talus_influence      != 0.0f;

            auto mask_of = [&layer](const TerrainSurfaceSample& s) -> float
            {
                if (layer.mask_channel == 0)
                {
                    return s.mask_grass;
                }
                if (layer.mask_channel == 1)
                {
                    return s.mask_trees;
                }
                if (layer.mask_channel == 2)
                {
                    return s.mask_rocks;
                }

                return 1.0f;
            };

            // one weight per triangle, 0 rejects the ground and 1 is ground the rule fully accepts,
            // the count follows the sum so density stays an honest instances per hectare figure
            auto weigh = [&](const TriangleData& tri) -> float
            {
                const float height = tri.centroid.y - ctx.sea_local;
                if (tri.slope_radians < slope_min_rad ||
                    tri.slope_radians > slope_max_rad ||
                    height < layer.height_min ||
                    height > layer.height_max)
                {
                    return 0.0f;
                }

                float weight = 1.0f;

                if (layer.height_fade > 0.0f)
                {
                    weight *= saturate((height - layer.height_min) / layer.height_fade);
                }

                if (layer.slope_bias != 0.0f)
                {
                    const float t = saturate((tri.slope_radians - slope_min_rad) / slope_range);
                    weight *= layer.slope_bias > 0.0f ?
                        powf(t, layer.slope_bias) :
                        powf(1.0f - t, -layer.slope_bias);
                }

                if (reads_surface && ctx.sample_surface)
                {
                    TerrainSurfaceSample sample;
                    ctx.sample_surface(
                        tri.centroid.x + ctx.tile_offset.x,
                        tri.centroid.z + ctx.tile_offset.z,
                        sample
                    );

                    if (layer.ground_mask != 0 && (layer.ground_mask & (1u << sample.dominant_layer)) == 0)
                    {
                        return 0.0f;
                    }

                    if (layer.mask_channel >= 0)
                    {
                        const float mask = mask_of(sample);
                        if (mask < layer.mask_min)
                        {
                            return 0.0f;
                        }
                        weight *= mask;
                    }

                    // same push the surface rules use, so an influence reads the same on both sides
                    float push = 0.0f;
                    push += layer.curvature_influence  * (sample.curvature * 2.0f - 1.0f);
                    push += layer.flow_influence       * (sample.flow * 2.0f - 1.0f);
                    push += layer.occlusion_influence  * (1.0f - sample.occlusion * 2.0f);
                    push += layer.insolation_influence * (sample.insolation * 2.0f - 1.0f);
                    push += layer.wear_influence       * (sample.wear * 2.0f - 1.0f);
                    push += layer.deposition_influence * (sample.deposition * 2.0f - 1.0f);
                    push += layer.talus_influence      * (sample.talus * 2.0f - 1.0f);
                    weight *= exp2f(clamp(push, -1.0f, 1.0f) * 1.25f);
                }

                return saturate(weight);
            };

            vector<uint32_t> acceptable_triangles;
            vector<float> acceptable_weights;
            acceptable_triangles.reserve(tile_triangle_data.size());
            acceptable_weights.reserve(tile_triangle_data.size());
            float weight_sum = 0.0f;
            for (uint32_t i = 0; i < tile_triangle_data.size(); i++)
            {
                const TriangleData& tri = tile_triangle_data[i];

                // skip edge triangles to prevent double-spawning at tile boundaries
                if (tri.centroid.x >= edge_threshold_x || tri.centroid.z >= edge_threshold_z)
                {
                    continue;
                }

                // centroid only, a triangle that merely clips the snow line must still be able to
                // hold trees on the side that is actually below it
                const float weight = weigh(tri);
                if (weight <= 1e-6f)
                {
                    continue;
                }

                acceptable_triangles.push_back(i);
                acceptable_weights.push_back(weight);
                weight_sum += weight;
            }

            if (coverage_out)
            {
                *coverage_out = weight_sum / max(static_cast<float>(tile_triangle_data.size()), 1.0f);
            }

            if (acceptable_triangles.empty() || weight_sum <= 1e-6f)
            {
                return;
            }

            vector<float> weight_prefix(acceptable_weights.size());
            {
                float running = 0.0f;
                for (size_t i = 0; i < acceptable_weights.size(); i++)
                {
                    running += acceptable_weights[i];
                    weight_prefix[i] = running;
                }
            }

            auto pick_weighted_local = [&](mt19937& generator) -> uint32_t
            {
                uniform_real_distribution<float> pick_dist(0.0f, weight_sum);
                const float x = pick_dist(generator);
                auto it = lower_bound(weight_prefix.begin(), weight_prefix.end(), x);
                uint32_t local = static_cast<uint32_t>(distance(weight_prefix.begin(), it));
                if (local >= acceptable_triangles.size())
                {
                    local = static_cast<uint32_t>(acceptable_triangles.size() - 1);
                }
                return local;
            };

            // instance count follows the accepted weight, a tile that is 10 percent meadow gets 10
            // percent of the instances instead of a flat scatter across every triangle that passed
            // the area factor is what makes density independent of the mesh resolution
            const float per_triangle = layer.density * ctx.triangle_area * (1.0f / 10000.0f);
            const float expected     = per_triangle * weight_sum;
            uint32_t adjusted_count  = static_cast<uint32_t>(expected);
            {
                uint32_t h = tile_index * 2654435761u + layer.seed * 2246822519u + 1013904223u;
                h = (h ^ (h >> 16)) * 0x7feb352du;
                h = (h ^ (h >> 15)) * 0x846ca68bu;
                h = h ^ (h >> 16);
                const float roll = static_cast<float>(h & 0xffffu) * (1.0f / 65535.0f);
                if (roll < (expected - floorf(expected)))
                {
                    adjusted_count++;
                }
            }
            if (layer.max_per_tile > 0 && adjusted_count > layer.max_per_tile)
            {
                adjusted_count = layer.max_per_tile;
            }
            transforms_out.resize(adjusted_count);
            if (adjusted_count == 0)
            {
                return;
            }

            // setup cluster parameters
            const float clump_radius = max(layer.clump_radius, 0.0f);
            float safe_min_x   = tile_min_x + clump_radius;
            float safe_max_x   = tile_max_x - clump_radius;
            float safe_min_z   = tile_min_z + clump_radius;
            float safe_max_z   = tile_max_z - clump_radius;
            bool has_safe_zone = (safe_min_x < safe_max_x) && (safe_min_z < safe_max_z);

            uint32_t cluster_count              = adjusted_count;
            uint32_t base_instances_per_cluster = 1;
            uint32_t remainder_instances        = 0;
            if (layer.clump_count > 1)
            {
                cluster_count              = max(1u, adjusted_count / layer.clump_count);
                base_instances_per_cluster = adjusted_count / cluster_count;
                remainder_instances        = adjusted_count % cluster_count;
            }
            vector<ClusterData> clusters(cluster_count);

            // place cluster centers
            auto place_cluster = [&](uint32_t start_index, uint32_t end_index)
            {
                mt19937 generator(tile_index * 1000003u + start_index * 31u + layer.seed * 7919u + 12345u);
                uniform_real_distribution<float> dist(0.0f, 1.0f);
                const uint32_t max_attempts = 50;
                
                for (uint32_t i = start_index; i < end_index; i++)
                {
                    Vector3 position;
                    uint32_t tri_idx;
                    uint32_t attempts = 0;
                    
                    do
                    {
                        tri_idx           = acceptable_triangles[pick_weighted_local(generator)];
                        TriangleData& tri = tile_triangle_data[tri_idx];

                        float r1      = dist(generator);
                        float r2      = dist(generator);
                        float sqrt_r1 = sqrtf(r1);
                        float u       = 1.0f - sqrt_r1;
                        float v       = r2 * sqrt_r1;
                        position      = tri.v0 + u * tri.v1_minus_v0 + v * tri.v2_minus_v0 + Vector3(0.0f, layer.surface_offset, 0.0f);
                        attempts++;
                        
                        if (!has_safe_zone || clump_radius <= 0.0f)
                        {
                            break;
                        }
                            
                    } while (attempts < max_attempts &&
                             (position.x < safe_min_x || position.x > safe_max_x ||
                              position.z < safe_min_z || position.z > safe_max_z));
                    
                    clusters[i] = { position, tri_idx };
                }
            };
            ThreadPool::ParallelLoop(place_cluster, cluster_count);

            // build spatial grid for nearby triangle lookup
            vector<vector<uint32_t>> cluster_nearby_tris(cluster_count);
            const float max_effective_radius = clump_radius * 1.6f;
            const float cell_size            = max(max_effective_radius, 1.0f);
            
            int32_t grid_min_x  = static_cast<int32_t>(floorf(tile_min_x / cell_size));
            int32_t grid_min_z  = static_cast<int32_t>(floorf(tile_min_z / cell_size));
            int32_t grid_max_x  = static_cast<int32_t>(floorf(tile_max_x / cell_size));
            int32_t grid_width  = grid_max_x - grid_min_x + 1;
            
            unordered_map<int64_t, vector<uint32_t>> spatial_grid;
            if (clump_radius > 0.0f)
            {
                for (uint32_t t = 0; t < static_cast<uint32_t>(acceptable_triangles.size()); t++)
                {
                    uint32_t tri_idx  = acceptable_triangles[t];
                    TriangleData& tri = tile_triangle_data[tri_idx];
                    int32_t cell_x    = static_cast<int32_t>(floorf(tri.centroid.x / cell_size)) - grid_min_x;
                    int32_t cell_z    = static_cast<int32_t>(floorf(tri.centroid.z / cell_size)) - grid_min_z;
                    int64_t cell_key  = static_cast<int64_t>(cell_z) * grid_width + cell_x;
                    spatial_grid[cell_key].push_back(t);
                }
            }
            
            // find triangles within cluster radius using organic noise shape
            auto compute_nearby = [&](uint32_t start_index, uint32_t end_index)
            {
                for (uint32_t c = start_index; c < end_index; c++)
                {
                    auto& nearby    = cluster_nearby_tris[c];
                    ClusterData& cluster = clusters[c];
                    Vector2 cluster_xz(cluster.center_position.x, cluster.center_position.z);
                    
                    if (clump_radius <= 0.0f)
                    {
                        nearby.push_back(cluster.center_tri_idx);
                        continue;
                    }
                    
                    // generate noise parameters from cluster position
                    float seed1 = (cluster.center_position.x * 12.9898f + cluster.center_position.z * 78.233f) * 43758.5453f;
                    float seed2 = (cluster.center_position.x * 39.346f + cluster.center_position.z * 11.135f) * 23421.631f;
                    float seed3 = (cluster.center_position.z * 47.134f + cluster.center_position.x * 93.271f) * 67823.183f;
                    seed1 -= floorf(seed1);
                    seed2 -= floorf(seed2);
                    seed3 -= floorf(seed3);
                    
                    float freq1  = 2.3f + seed1 * 1.4f;
                    float freq2  = 3.7f + seed2 * 2.1f;
                    float freq3  = 5.1f + seed3 * 2.8f;
                    float freq4  = 1.7f + seed1 * 0.8f;
                    float freq5  = 7.3f + seed2 * 3.2f;
                    float phase1 = seed1 * pi_2;
                    float phase2 = seed2 * pi_2;
                    float phase3 = seed3 * pi_2;
                    float phase4 = (seed1 + seed2) * pi;
                    float phase5 = (seed2 + seed3) * pi;
                    
                    // query nearby grid cells
                    float max_radius   = clump_radius * 1.6f;
                    int32_t cell_x     = static_cast<int32_t>(floorf(cluster_xz.x / cell_size)) - grid_min_x;
                    int32_t cell_z     = static_cast<int32_t>(floorf(cluster_xz.y / cell_size)) - grid_min_z;
                    int32_t cell_range = static_cast<int32_t>(ceilf(max_radius / cell_size));
                    
                    for (int32_t dz = -cell_range; dz <= cell_range; dz++)
                    {
                        for (int32_t dx = -cell_range; dx <= cell_range; dx++)
                        {
                            int64_t cell_key = static_cast<int64_t>(cell_z + dz) * grid_width + (cell_x + dx);
                            auto grid_it     = spatial_grid.find(cell_key);
                            if (grid_it == spatial_grid.end())
                            {
                                continue;
                            }
                            
                            for (uint32_t t : grid_it->second)
                            {
                                uint32_t tri_idx  = acceptable_triangles[t];
                                TriangleData& tri = tile_triangle_data[tri_idx];
                                Vector2 tri_xz(tri.centroid.x, tri.centroid.z);
                                Vector2 offset  = tri_xz - cluster_xz;
                                float dist_sq   = offset.LengthSquared();
                                float dist      = sqrtf(dist_sq);
                                float angle     = atan2f(offset.y, offset.x);
                                float norm_dist = dist / clump_radius;
                                
                                // layered noise for organic blob shape
                                float noise1     = sinf(angle * freq1 + phase1) * 0.18f;
                                float noise2     = sinf(angle * freq2 + phase2) * 0.14f;
                                float noise3     = sinf(angle * freq3 + phase3) * 0.10f;
                                float noise4     = cosf(angle * freq4 + phase4) * 0.20f;
                                float noise5     = sinf(angle * freq5 + phase5) * 0.06f;
                                float dist_noise = sinf(norm_dist * 3.14159f + seed1 * 6.28f) * 0.12f * norm_dist;
                                float pos_noise  = sinf(offset.x * 0.3f + seed2 * 10.0f) * cosf(offset.y * 0.3f + seed3 * 10.0f) * 0.08f;
                                
                                // raggedness of 0 leaves a clean circle, 1 is the full organic blob
                                const float ragged     = saturate(layer.clump_raggedness);
                                float radius_variation = 1.0f + (noise1 + noise2 + noise3 + noise4 + noise5 + dist_noise + pos_noise) * ragged;
                                radius_variation       = fmaxf(0.4f, fminf(1.6f, radius_variation));
                                
                                float effective_radius = clump_radius * radius_variation;
                                if (dist_sq <= effective_radius * effective_radius)
                                {
                                    nearby.push_back(tri_idx);
                                }
                            }
                        }
                    }
                    
                    if (nearby.empty())
                    {
                        nearby.push_back(cluster.center_tri_idx);
                    }
                }
            };
            ThreadPool::ParallelLoop(compute_nearby, cluster_count);

            // place instances within clusters
            auto place_mesh = [&](uint32_t start_index, uint32_t end_index)
            {
                mt19937 generator(tile_index * 2000003u + start_index * 37u + layer.seed * 104729u + 67890u);
                uniform_real_distribution<float> dist(0.0f, 1.0f);
                uniform_real_distribution<float> angle_dist(0.0f, 360.0f);
                uint32_t larger_cluster_size = base_instances_per_cluster + 1;
                
                for (uint32_t i = start_index; i < end_index; i++)
                {
                    // map instance to cluster
                    uint32_t cluster_idx;
                    if (i < remainder_instances * larger_cluster_size)
                    {
                        cluster_idx = i / larger_cluster_size;
                    }
                    else
                    {
                        cluster_idx = remainder_instances + (i - remainder_instances * larger_cluster_size) / base_instances_per_cluster;
                    }
                    
                    auto& nearby = cluster_nearby_tris[cluster_idx];
                    if (nearby.empty())
                    {
                        continue;
                    }

                    uniform_int_distribution<int> nearby_dist(0, static_cast<int>(nearby.size()) - 1);
                    Vector3 position;
                    uint32_t tri_idx = nearby[nearby_dist(generator)];
                    const uint32_t max_point_attempts = 8;
                    for (uint32_t attempt = 0; attempt < max_point_attempts; attempt++)
                    {
                        tri_idx = nearby[nearby_dist(generator)];
                        TriangleData& candidate = tile_triangle_data[tri_idx];

                        float r1      = dist(generator);
                        float r2      = dist(generator);
                        float sqrt_r1 = sqrtf(r1);
                        float u       = 1.0f - sqrt_r1;
                        float v       = r2 * sqrt_r1;
                        position = candidate.v0 + u * candidate.v1_minus_v0 + v * candidate.v2_minus_v0
                            + Vector3(0.0f, layer.surface_offset, 0.0f);

                        if (layer.mask_channel < 0 || !ctx.sample_surface)
                        {
                            break;
                        }

                        TerrainSurfaceSample sample;
                        ctx.sample_surface(
                            position.x + ctx.tile_offset.x,
                            position.z + ctx.tile_offset.z,
                            sample
                        );
                        if (mask_of(sample) >= layer.mask_min)
                        {
                            break;
                        }
                    }
                    TriangleData& tri = tile_triangle_data[tri_idx];

                    // rotation, align is a continuous lean into the slope so a rule can sit a prop
                    // anywhere between upright and flat on the face
                    const Quaternion yaw = Quaternion::FromEulerAngles(0.0f, angle_dist(generator), 0.0f);
                    Quaternion rotation;
                    if (layer.flags & TerrainScatterFlags_Tumble)
                    {
                        rotation = Quaternion::FromEulerAngles(
                            angle_dist(generator),
                            angle_dist(generator),
                            angle_dist(generator)
                        );
                    }
                    else if (layer.align_to_normal <= 0.001f)
                    {
                        rotation = yaw;
                    }
                    else if (layer.align_to_normal >= 0.999f)
                    {
                        rotation = tri.rotation_to_normal * yaw;
                    }
                    else
                    {
                        const Quaternion lean = Quaternion::Lerp(
                            Quaternion::Identity,
                            tri.rotation_to_normal,
                            layer.align_to_normal
                        );
                        rotation = lean * yaw;
                    }

                    // size, either a straight pick from the range or driven by where the ground sits
                    // in the slope and altitude bands
                    const float gradient_weight = layer.size_from_slope + layer.size_from_altitude;
                    float size                  = 0.0f;
                    if (gradient_weight > 0.0f)
                    {
                        const float relief = max(tri.centroid.y - ctx.sea_local, 16.0f);
                        const float alt_t  = sqrtf(saturate(relief / max(layer.altitude_span, 16.0f)));
                        const float slope_t = saturate((tri.slope_radians - slope_min_rad) / slope_range);
                        const float t = saturate(
                            (alt_t * layer.size_from_altitude + slope_t * layer.size_from_slope) / gradient_weight
                        );
                        size = lerp(layer.size_min, layer.size_max, t);
                        size *= lerp(0.85f, 1.2f, dist(generator));
                    }
                    else if (layer.flags & TerrainScatterFlags_LogSize)
                    {
                        const float log_min = logf(max(layer.size_min, 1e-4f));
                        const float log_max = logf(max(layer.size_max, 1e-4f));
                        size = expf(lerp(log_min, log_max, dist(generator)));
                    }
                    else
                    {
                        size = lerp(layer.size_min, layer.size_max, dist(generator));
                    }

                    if (layer.giant_chance > 0.0f && dist(generator) < layer.giant_chance)
                    {
                        const float giant = layer.giant_size > 0.0f ? layer.giant_size : layer.size_max;
                        size = lerp(giant * 0.55f, giant, dist(generator));
                    }

                    const float scale = max(size, 0.0f) * layer.mesh_scale;
                    if (layer.sink > 0.0f)
                    {
                        position -= tri.normal * (scale * layer.sink);
                    }

                    transforms_out[i] = Matrix::CreateScale(scale) * Matrix::CreateRotation(rotation) * Matrix::CreateTranslation(position);
                }
            };
            ThreadPool::ParallelLoop(place_mesh, adjusted_count);
        }
    }

    Terrain::Terrain(Entity* entity) : Component(entity)
    {
        SP_REGISTER_ATTRIBUTE_VALUE_VALUE(m_min_y, float);
        SP_REGISTER_ATTRIBUTE_VALUE_VALUE(m_max_y, float);
        SP_REGISTER_ATTRIBUTE_VALUE_VALUE(m_level_sea, float);
        SP_REGISTER_ATTRIBUTE_VALUE_VALUE(m_level_snow, float);
        SP_REGISTER_ATTRIBUTE_VALUE_VALUE(m_smoothing, uint32_t);
        SP_REGISTER_ATTRIBUTE_VALUE_VALUE(m_density, uint32_t);
        SP_REGISTER_ATTRIBUTE_VALUE_VALUE(m_scale, uint32_t);
        SP_REGISTER_ATTRIBUTE_VALUE_VALUE(m_create_border, bool);
        SP_REGISTER_ATTRIBUTE_VALUE_VALUE(m_width, uint32_t);
        SP_REGISTER_ATTRIBUTE_VALUE_VALUE(m_height, uint32_t);
        SP_REGISTER_ATTRIBUTE_VALUE_VALUE(m_area_km2, float);
        SP_REGISTER_ATTRIBUTE_VALUE_VALUE(m_height_samples, uint32_t);
        SP_REGISTER_ATTRIBUTE_VALUE_VALUE(m_vertex_count, uint32_t);
        SP_REGISTER_ATTRIBUTE_VALUE_VALUE(m_index_count, uint32_t);
        SP_REGISTER_ATTRIBUTE_VALUE_VALUE(m_triangle_count, uint32_t);

        m_layer_rules    = TerrainLayerDefaults::Get();
        m_scatter_layers = TerrainScatterDefaults::Get();

        m_material = make_shared<Material>();
        m_material->SetObjectName("terrain");
        ApplyDefaultMaterial();
    }

    Terrain::~Terrain()
    {
        FinishGenerate();

        Renderer::ClearTerrain(m_material.get());

        m_height_map_seed = nullptr;
        m_height_map_final_retired.reset();
        m_height_map_final.reset();
        m_height_map_gpu_retired.reset();
        m_height_map_gpu.reset();
        m_map_a_retired.reset();
        m_map_b_retired.reset();
        m_prop_mask_retired.reset();
        m_map_a.reset();
        m_map_b.reset();
        m_prop_mask.reset();
    }

    void Terrain::ApplyDefaultMaterial()
    {
        if (!m_material)
        {
            m_material = make_shared<Material>();
            m_material->SetObjectName("terrain");
        }

        // the surface material carries no layer textures, it only marks the draw as terrain and
        // holds the uv scale, everything visible comes from the layer materials
        m_material->SetResourceName(string("terrain") + EXTENSION_MATERIAL);
        m_material->SetProperty(MaterialProperty::IsTerrain, 1.0f);
        // texture repeats per meter, the shader maps planar world xz, a 4k albedo over 7 meters still
        // leaves close to 600 texels per meter, so the density costs nothing and the repeat halves
        m_material->SetProperty(MaterialProperty::TextureTilingX, 0.15f);
        m_material->SetProperty(MaterialProperty::TextureTilingY, 0.15f);
        m_material->SetProperty(MaterialProperty::Tessellation, 0.0f);

        if (!m_material->GetResourceFilePath().empty())
        {
            m_material = ResourceCache::Cache(m_material);
        }

        RefreshLayers();
    }

    Material* Terrain::GetLayerMaterial(uint32_t index) const
    {
        return index < terrain_layer_max ? m_layer_materials[index].get() : nullptr;
    }

    bool Terrain::IsLayerEnabled(uint32_t index) const
    {
        return index < terrain_layer_max &&
               m_layer_materials[index] != nullptr &&
               m_layer_rules[index].weight_bias > 0.0f;
    }

    void Terrain::SetLayerQuality(uint32_t quality)
    {
        m_layer_quality = clamp(quality, 1u, 4u);
        PushToRenderer();
    }

    void Terrain::SetDebugView(TerrainDebugView view)
    {
        m_debug_view = view;
        PushToRenderer();
    }

    void Terrain::RefreshLayers()
    {
        // a layer is only built when its folder holds an albedo, anything else it is missing just
        // falls back to a material property, so a partial folder still produces a usable layer
        for (uint32_t i = 0; i < terrain_layer_max; i++)
        {
            const TerrainLayerRule& rule = m_layer_rules[i];
            const string folder          = "project/materials/" + rule.name + "/";
            const string albedo          = folder + "albedo.png";

            if (rule.name.empty() || !FileSystem::Exists(albedo))
            {
                m_layer_materials[i] = nullptr;
                continue;
            }

            if (!m_layer_materials[i])
            {
                m_layer_materials[i] = make_shared<Material>();
            }

            shared_ptr<Material>& layer = m_layer_materials[i];
            layer->SetObjectName("terrain_layer_" + rule.name);
            layer->SetResourceName("terrain_layer_" + rule.name + EXTENSION_MATERIAL);
            layer->SetProperty(MaterialProperty::IsTerrain, 1.0f);
            // a fresh material defaults both of these to zero, which would flatten every layer
            // normal and disable the parallax march, the layer rule carries the artistic control
            layer->SetProperty(MaterialProperty::Normal, 1.0f);
            layer->SetProperty(MaterialProperty::Height, 1.0f);

            layer->SetTexture(MaterialTextureType::Color, albedo, 0);

            auto set_optional = [&layer, &folder](MaterialTextureType type, const char* file)
            {
                const string path = folder + file;
                if (FileSystem::Exists(path))
                {
                    layer->SetTexture(type, path, 0);
                }
            };

            set_optional(MaterialTextureType::Normal,    "normal.png");
            set_optional(MaterialTextureType::Roughness, "roughness.png");
            set_optional(MaterialTextureType::Occlusion, "occlusion.png");
            set_optional(MaterialTextureType::Height,    "height.png");

            // no render component owns a layer, so nothing else would ever pack its orm, compress
            // it or upload it, the call guards itself against a second pass
            layer->PrepareForGpu();
        }

        PushToRenderer();
    }

    void Terrain::PushToRenderer() const
    {
        if (!m_material)
        {
            return;
        }

        Renderer::TerrainParams params;
        params.surface       = m_material.get();
        params.map_a         = m_map_a.get();
        params.map_b         = m_map_b.get();
        params.height_map    = m_height_map_gpu.get();
        params.world_mapping = m_world_mapping;
        params.sea_level     = m_level_sea;
        params.snow_level    = m_level_snow;
        params.snow_amount   = m_snow_amount;
        params.wetness       = m_wetness;
        params.quality       = m_layer_quality;
        params.debug_view    = static_cast<uint32_t>(m_debug_view);

        for (uint32_t i = 0; i < terrain_layer_max; i++)
        {
            params.layer_materials[i] = m_layer_materials[i].get();
            params.layer_rules[i]     = m_layer_rules[i];
        }

        Renderer::SetTerrain(params);
    }

    void Terrain::Save(pugi::xml_node& node)
    {
        // height map seed texture path
        if (m_height_map_seed)
        {
            node.append_attribute("height_map_path") = m_height_map_seed->GetResourceFilePath().c_str();
        }

        // configurable parameters
        node.append_attribute("min_y")         = m_min_y;
        node.append_attribute("max_y")         = m_max_y;
        node.append_attribute("level_sea")     = m_level_sea;
        node.append_attribute("level_snow")    = m_level_snow;
        node.append_attribute("shore_width")   = m_shore_width;
        node.append_attribute("smoothing")     = m_smoothing;
        node.append_attribute("density")       = m_density;
        node.append_attribute("scale")         = m_scale;
        node.append_attribute("tile_count")    = m_tile_count;
        node.append_attribute("create_border") = m_create_border;
        node.append_attribute("spawn_biome_props")   = m_spawn_biome_props;
        node.append_attribute("layer_quality") = m_layer_quality;
        node.append_attribute("snow_amount")   = m_snow_amount;
        node.append_attribute("wetness")       = m_wetness;

        // surface layer rules, these are authored per world exactly like the scatter rules are
        pugi::xml_node layers_node = node.append_child("layers");
        for (const TerrainLayerRule& rule : m_layer_rules)
        {
            pugi::xml_node rule_node = layers_node.append_child("layer");

            rule_node.append_attribute("name")           = rule.name.c_str();
            rule_node.append_attribute("slope_min")      = rule.slope_min;
            rule_node.append_attribute("slope_max")      = rule.slope_max;
            rule_node.append_attribute("height_min")     = rule.height_min;
            rule_node.append_attribute("height_max")     = rule.height_max;
            rule_node.append_attribute("curvature")      = rule.curvature_influence;
            rule_node.append_attribute("flow")           = rule.flow_influence;
            rule_node.append_attribute("occlusion")      = rule.occlusion_influence;
            rule_node.append_attribute("insolation")     = rule.insolation_influence;
            rule_node.append_attribute("wear")           = rule.wear_influence;
            rule_node.append_attribute("deposition")     = rule.deposition_influence;
            rule_node.append_attribute("talus")          = rule.talus_influence;
            rule_node.append_attribute("tiling_scale")   = rule.tiling_scale;
            rule_node.append_attribute("blend_contrast") = rule.blend_contrast;
            rule_node.append_attribute("porosity")       = rule.porosity;
            rule_node.append_attribute("macro_strength") = rule.macro_strength;
            rule_node.append_attribute("weight_bias")    = rule.weight_bias;
            rule_node.append_attribute("flags")          = rule.flags;
        }

        // scatter layers, the whole prop rule set travels with the world
        pugi::xml_node scatter_node = node.append_child("scatter");
        for (const TerrainScatterLayer& layer : m_scatter_layers)
        {
            pugi::xml_node layer_node = scatter_node.append_child("layer");

            layer_node.append_attribute("name")                = layer.name.c_str();
            layer_node.append_attribute("mesh_path")            = layer.mesh_path.c_str();
            layer_node.append_attribute("material_folder")      = layer.material_folder.c_str();
            layer_node.append_attribute("enabled")              = layer.enabled;
            layer_node.append_attribute("kind")                 = static_cast<uint32_t>(layer.kind);
            layer_node.append_attribute("density")              = layer.density;
            layer_node.append_attribute("max_per_tile")         = layer.max_per_tile;
            layer_node.append_attribute("seed")                 = layer.seed;
            layer_node.append_attribute("slope_min")            = layer.slope_min;
            layer_node.append_attribute("slope_max")            = layer.slope_max;
            layer_node.append_attribute("slope_bias")           = layer.slope_bias;
            layer_node.append_attribute("height_min")           = layer.height_min;
            layer_node.append_attribute("height_max")           = layer.height_max;
            layer_node.append_attribute("height_fade")          = layer.height_fade;
            layer_node.append_attribute("curvature")            = layer.curvature_influence;
            layer_node.append_attribute("flow")                 = layer.flow_influence;
            layer_node.append_attribute("occlusion")            = layer.occlusion_influence;
            layer_node.append_attribute("insolation")           = layer.insolation_influence;
            layer_node.append_attribute("wear")                 = layer.wear_influence;
            layer_node.append_attribute("deposition")           = layer.deposition_influence;
            layer_node.append_attribute("talus")                = layer.talus_influence;
            layer_node.append_attribute("ground_mask")          = layer.ground_mask;
            layer_node.append_attribute("mask_channel")         = layer.mask_channel;
            layer_node.append_attribute("mask_min")             = layer.mask_min;
            layer_node.append_attribute("clump_radius")         = layer.clump_radius;
            layer_node.append_attribute("clump_count")          = layer.clump_count;
            layer_node.append_attribute("clump_raggedness")     = layer.clump_raggedness;
            layer_node.append_attribute("mesh_scale")           = layer.mesh_scale;
            layer_node.append_attribute("size_min")             = layer.size_min;
            layer_node.append_attribute("size_max")             = layer.size_max;
            layer_node.append_attribute("size_from_slope")      = layer.size_from_slope;
            layer_node.append_attribute("size_from_altitude")   = layer.size_from_altitude;
            layer_node.append_attribute("altitude_span")        = layer.altitude_span;
            layer_node.append_attribute("giant_chance")         = layer.giant_chance;
            layer_node.append_attribute("giant_size")           = layer.giant_size;
            layer_node.append_attribute("align_to_normal")      = layer.align_to_normal;
            layer_node.append_attribute("surface_offset")       = layer.surface_offset;
            layer_node.append_attribute("sink")                 = layer.sink;
            layer_node.append_attribute("render_distance")      = layer.render_distance;
            layer_node.append_attribute("shadow_distance")      = layer.shadow_distance;
            layer_node.append_attribute("grass_ring_0")         = layer.grass_ring_radius[0];
            layer_node.append_attribute("grass_ring_1")         = layer.grass_ring_radius[1];
            layer_node.append_attribute("grass_ring_2")         = layer.grass_ring_radius[2];
            layer_node.append_attribute("grass_cell_0")         = layer.grass_cell_size[0];
            layer_node.append_attribute("grass_cell_1")         = layer.grass_cell_size[1];
            layer_node.append_attribute("grass_cell_2")         = layer.grass_cell_size[2];
            layer_node.append_attribute("flags")                = layer.flags;
        }

        // flat terrain dims, used when there is no height map seed
        if (!m_height_map_seed && m_width > 1 && m_height > 1)
        {
            node.append_attribute("flat_width")  = m_width;
            node.append_attribute("flat_height") = m_height;
        }
    }

    void Terrain::Load(pugi::xml_node& node)
    {
        // height map seed texture
        string height_map_path = node.attribute("height_map_path").as_string("");
        if (!height_map_path.empty())
        {
            if (shared_ptr<RHI_Texture> texture = ResourceCache::Load<RHI_Texture>(height_map_path))
            {
                m_height_map_seed = texture.get();
            }
        }

        // configurable parameters
        m_min_y         = node.attribute("min_y").as_float(0.0f);
        m_max_y         = node.attribute("max_y").as_float(755.0f);
        m_level_sea     = node.attribute("level_sea").as_float(0.0f);
        m_level_snow    = node.attribute("level_snow").as_float(400.0f);
        m_shore_width   = node.attribute("shore_width").as_float(2000.0f);
        m_smoothing     = node.attribute("smoothing").as_uint(0);
        m_density       = max(node.attribute("density").as_uint(1), 1u);
        m_scale         = max(node.attribute("scale").as_uint(25), 1u);
        m_tile_count    = max(node.attribute("tile_count").as_uint(16), 1u);
        m_create_border = node.attribute("create_border").as_bool(false);
        m_spawn_biome_props = node.attribute("spawn_biome_props").as_bool(true);
        m_layer_quality = clamp(node.attribute("layer_quality").as_uint(3), 1u, 4u);
        m_snow_amount   = node.attribute("snow_amount").as_float(1.0f);
        m_wetness       = node.attribute("wetness").as_float(0.0f);

        // surface layer rules, a world saved before they travelled with it keeps the defaults
        m_layer_rules = TerrainLayerDefaults::Get();
        if (pugi::xml_node layers_node = node.child("layers"))
        {
            uint32_t rule_index = 0;
            for (pugi::xml_node rule_node = layers_node.child("layer");
                 rule_node && rule_index < terrain_layer_max;
                 rule_node = rule_node.next_sibling("layer"), rule_index++)
            {
                TerrainLayerRule& rule = m_layer_rules[rule_index];

                rule.name                 = rule_node.attribute("name").as_string(rule.name.c_str());
                rule.slope_min            = rule_node.attribute("slope_min").as_float(rule.slope_min);
                rule.slope_max            = rule_node.attribute("slope_max").as_float(rule.slope_max);
                rule.height_min           = rule_node.attribute("height_min").as_float(rule.height_min);
                rule.height_max           = rule_node.attribute("height_max").as_float(rule.height_max);
                rule.curvature_influence  = rule_node.attribute("curvature").as_float(rule.curvature_influence);
                rule.flow_influence       = rule_node.attribute("flow").as_float(rule.flow_influence);
                rule.occlusion_influence  = rule_node.attribute("occlusion").as_float(rule.occlusion_influence);
                rule.insolation_influence = rule_node.attribute("insolation").as_float(rule.insolation_influence);
                rule.wear_influence       = rule_node.attribute("wear").as_float(rule.wear_influence);
                rule.deposition_influence = rule_node.attribute("deposition").as_float(rule.deposition_influence);
                rule.talus_influence      = rule_node.attribute("talus").as_float(rule.talus_influence);
                rule.tiling_scale         = rule_node.attribute("tiling_scale").as_float(rule.tiling_scale);
                rule.blend_contrast       = rule_node.attribute("blend_contrast").as_float(rule.blend_contrast);
                rule.porosity             = rule_node.attribute("porosity").as_float(rule.porosity);
                rule.macro_strength       = rule_node.attribute("macro_strength").as_float(rule.macro_strength);
                rule.weight_bias          = rule_node.attribute("weight_bias").as_float(rule.weight_bias);
                rule.flags                = rule_node.attribute("flags").as_uint(rule.flags);
            }
        }

        // scatter layers, a world saved before they existed carries the old per prop multipliers
        // instead, fold those into the matching default layer so it still looks the way it did
        m_scatter_layers = TerrainScatterDefaults::Get();
        if (pugi::xml_node scatter_node = node.child("scatter"))
        {
            uint32_t index = 0;
            for (pugi::xml_node layer_node = scatter_node.child("layer");
                 layer_node && index < terrain_scatter_max;
                 layer_node = layer_node.next_sibling("layer"), index++)
            {
                TerrainScatterLayer& layer = m_scatter_layers[index];

                layer.name                 = layer_node.attribute("name").as_string("");
                layer.mesh_path            = layer_node.attribute("mesh_path").as_string("");
                layer.material_folder      = layer_node.attribute("material_folder").as_string("");
                layer.enabled              = layer_node.attribute("enabled").as_bool(false);
                layer.kind                 = static_cast<TerrainScatterKind>(
                    min(layer_node.attribute("kind").as_uint(0), static_cast<uint32_t>(TerrainScatterKind::Max) - 1u));
                layer.density              = layer_node.attribute("density").as_float(8.0f);
                layer.max_per_tile         = layer_node.attribute("max_per_tile").as_uint(0);
                layer.seed                 = layer_node.attribute("seed").as_uint(0);
                layer.slope_min            = layer_node.attribute("slope_min").as_float(0.0f);
                layer.slope_max            = layer_node.attribute("slope_max").as_float(35.0f);
                layer.slope_bias           = layer_node.attribute("slope_bias").as_float(0.0f);
                layer.height_min           = layer_node.attribute("height_min").as_float(1.0f);
                layer.height_max           = layer_node.attribute("height_max").as_float(100000.0f);
                layer.height_fade          = layer_node.attribute("height_fade").as_float(0.0f);
                layer.curvature_influence  = layer_node.attribute("curvature").as_float(0.0f);
                layer.flow_influence       = layer_node.attribute("flow").as_float(0.0f);
                layer.occlusion_influence  = layer_node.attribute("occlusion").as_float(0.0f);
                layer.insolation_influence = layer_node.attribute("insolation").as_float(0.0f);
                layer.wear_influence       = layer_node.attribute("wear").as_float(0.0f);
                layer.deposition_influence = layer_node.attribute("deposition").as_float(0.0f);
                layer.talus_influence      = layer_node.attribute("talus").as_float(0.0f);
                layer.ground_mask          = layer_node.attribute("ground_mask").as_uint(0);
                layer.mask_channel         = layer_node.attribute("mask_channel").as_int(-1);
                layer.mask_min             = layer_node.attribute("mask_min").as_float(0.0f);
                layer.clump_radius         = layer_node.attribute("clump_radius").as_float(0.0f);
                layer.clump_count          = max(layer_node.attribute("clump_count").as_uint(1), 1u);
                layer.clump_raggedness     = layer_node.attribute("clump_raggedness").as_float(1.0f);
                layer.mesh_scale           = layer_node.attribute("mesh_scale").as_float(1.0f);
                layer.size_min             = layer_node.attribute("size_min").as_float(0.8f);
                layer.size_max             = layer_node.attribute("size_max").as_float(1.2f);
                layer.size_from_slope      = layer_node.attribute("size_from_slope").as_float(0.0f);
                layer.size_from_altitude   = layer_node.attribute("size_from_altitude").as_float(0.0f);
                layer.altitude_span        = layer_node.attribute("altitude_span").as_float(180.0f);
                layer.giant_chance         = layer_node.attribute("giant_chance").as_float(0.0f);
                layer.giant_size           = layer_node.attribute("giant_size").as_float(0.0f);
                layer.align_to_normal      = layer_node.attribute("align_to_normal").as_float(1.0f);
                layer.surface_offset       = layer_node.attribute("surface_offset").as_float(0.05f);
                layer.sink                 = layer_node.attribute("sink").as_float(0.0f);
                layer.render_distance      = layer_node.attribute("render_distance").as_float(0.0f);
                layer.shadow_distance      = layer_node.attribute("shadow_distance").as_float(150.0f);
                layer.grass_ring_radius[0] = layer_node.attribute("grass_ring_0").as_float(55.0f);
                layer.grass_ring_radius[1] = layer_node.attribute("grass_ring_1").as_float(180.0f);
                layer.grass_ring_radius[2] = layer_node.attribute("grass_ring_2").as_float(500.0f);
                layer.grass_cell_size[0]   = layer_node.attribute("grass_cell_0").as_float(0.36f);
                layer.grass_cell_size[1]   = layer_node.attribute("grass_cell_1").as_float(0.82f);
                layer.grass_cell_size[2]   = layer_node.attribute("grass_cell_2").as_float(2.1f);
                layer.flags                = layer_node.attribute("flags").as_uint(TerrainScatterFlags_CastShadows);
            }
        }
        else
        {
            const float legacy_tree   = node.attribute("prop_density_tree").as_float(1.0f);
            const float legacy_rock   = node.attribute("prop_density_rock").as_float(1.0f);
            const float legacy_flower = node.attribute("prop_density_flower").as_float(1.0f);
            const float legacy_grass  = node.attribute("prop_density_grass").as_float(1.0f);

            for (TerrainScatterLayer& layer : m_scatter_layers)
            {
                if (layer.name == "trees")
                {
                    layer.density *= legacy_tree;
                }
                else if (layer.name == "boulders" || layer.name == "rock_debris")
                {
                    layer.density *= legacy_rock;
                }
                else if (layer.name == "flowers")
                {
                    layer.density *= legacy_flower;
                }
                else if (layer.name == "grass")
                {
                    layer.density *= legacy_grass;
                }
            }
        }

        // forest grass/rock/sand slope material
        ApplyDefaultMaterial();

        // regenerate terrain if we have a height map
        if (m_height_map_seed)
        {
            Generate();
        }
        else
        {
            // flat terrain authored without a height map
            const uint32_t flat_width  = node.attribute("flat_width").as_uint(0);
            const uint32_t flat_height = node.attribute("flat_height").as_uint(0);
            if (flat_width > 1 && flat_height > 1)
            {
                CreateFlat(flat_width, flat_height);
            }
        }
    }

    uint64_t Terrain::ComputeCacheHash() const
    {
        // hash inputs to detect when cache is stale
        uint64_t hash = 14695981039346656037ull; // fnv-1a offset basis
        auto hash_combine = [&hash](uint64_t value) {
            hash ^= value;
            hash *= 1099511628211ull; // fnv-1a prime
        };

        // bump when cache format or the generation algorithms change so old caches get invalidated
        const uint64_t cache_format_version = 13;
        hash_combine(cache_format_version);

        hash_combine(static_cast<uint64_t>(m_min_y * 1000));
        hash_combine(static_cast<uint64_t>(m_max_y * 1000));
        hash_combine(static_cast<uint64_t>(m_level_sea * 1000));
        hash_combine(static_cast<uint64_t>(m_level_snow * 1000));
        hash_combine(m_smoothing);
        hash_combine(m_density);
        hash_combine(m_scale);
        hash_combine(m_tile_count);
        hash_combine(m_create_border ? 1 : 0);
        
        if (m_height_map_seed)
        {
            hash_combine(m_height_map_seed->GetWidth());
            hash_combine(m_height_map_seed->GetHeight());

            // hash the file path (stable across runs) instead of object id (random per run)
            const string& file_path = m_height_map_seed->GetResourceFilePath();
            for (char c : file_path)
            {
                hash_combine(static_cast<uint64_t>(c));
            }
        }

        return hash;
    }

    bool Terrain::IsScatterSoloed() const
    {
        for (const TerrainScatterLayer& layer : m_scatter_layers)
        {
            if (layer.solo)
            {
                return true;
            }
        }

        return false;
    }

    bool Terrain::IsScatterActive(const TerrainScatterLayer& layer) const
    {
        if (!layer.enabled || layer.mesh_path.empty())
        {
            return false;
        }

        // soloing one layer is the fastest way to see what a rule is actually doing
        return layer.solo || !IsScatterSoloed();
    }

    float Terrain::GetTriangleArea() const
    {
        const float spacing = static_cast<float>(m_scale) / static_cast<float>(max(m_density, 1u));
        return max(spacing * spacing * 0.5f, 1e-3f);
    }

    float Terrain::GetSeaLevelLocal() const
    {
        // triangle heights are entity local, the levels are world
        // during load the world matrix can still be identity, local y is already authored
        float terrain_y = 0.0f;
        if (Entity* entity = GetEntity())
        {
            terrain_y = entity->GetPositionLocal().y;
            const float world_y = entity->GetMatrix().GetTranslation().y;
            if (world_y != 0.0f)
            {
                terrain_y = world_y;
            }
        }

        return m_level_sea - terrain_y;
    }

    bool Terrain::SampleSurface(float world_x, float world_z, TerrainSurfaceSample& sample_out) const
    {
        if (m_map_a_pixels.empty() || m_map_b_pixels.empty() || m_map_width == 0 || m_map_height == 0)
        {
            return false;
        }

        float u = (world_x - m_world_mapping.x) * m_world_mapping.z;
        float v = (world_z - m_world_mapping.y) * m_world_mapping.w;
        u = clamp(u, 0.0f, 1.0f);
        v = clamp(v, 0.0f, 1.0f);

        // nearest is enough, every channel here is a macro signal over tens of meters
        const uint32_t x    = min(static_cast<uint32_t>(u * static_cast<float>(m_map_width - 1) + 0.5f), m_map_width - 1u);
        const uint32_t z    = min(static_cast<uint32_t>(v * static_cast<float>(m_map_height - 1) + 0.5f), m_map_height - 1u);
        const size_t offset = (static_cast<size_t>(z) * m_map_width + x) * 4;
        const float inv     = 1.0f / 255.0f;

        sample_out.curvature  = m_map_a_pixels[offset + 0] * inv;
        sample_out.flow       = m_map_a_pixels[offset + 1] * inv;
        sample_out.occlusion  = m_map_a_pixels[offset + 2] * inv;
        sample_out.deposition = m_map_a_pixels[offset + 3] * inv;
        sample_out.wear       = m_map_b_pixels[offset + 0] * inv;
        sample_out.insolation = m_map_b_pixels[offset + 1] * inv;
        sample_out.talus      = m_map_b_pixels[offset + 3] * inv;

        if (m_prop_mask_pixels.size() > offset + 3)
        {
            sample_out.mask_grass = m_prop_mask_pixels[offset + 0] * inv;
            sample_out.mask_trees = m_prop_mask_pixels[offset + 1] * inv;
            sample_out.mask_rocks = m_prop_mask_pixels[offset + 2] * inv;
        }

        const size_t cell = static_cast<size_t>(z) * m_map_width + x;
        if (cell < m_layer_dominant.size())
        {
            sample_out.dominant_layer = min(
                static_cast<uint32_t>(m_layer_dominant[cell]),
                terrain_layer_max - 1u
            );
        }

        return true;
    }

    void Terrain::FindTransforms(
        const uint32_t tile_index,
        const TerrainScatterLayer& layer,
        vector<Matrix>& transforms_out,
        float* coverage_out
    )
    {
        placement::ScatterContext context;
        context.sea_local     = GetSeaLevelLocal();
        context.triangle_area = GetTriangleArea();
        context.tile_offset   = (tile_index < m_tile_offsets.size()) ? m_tile_offsets[tile_index] : Vector3::Zero;
        // triangle centroids are tile local, the baked maps are authored in terrain xz
        context.sample_surface = [this](float x, float z, TerrainSurfaceSample& out) -> bool
        {
            return SampleSurface(x, z, out);
        };

        placement::find_transforms(layer, context, tile_index, transforms_out, m_triangle_data, coverage_out);
    }

    void Terrain::SaveToFile(const char* file_path)
    {
        ofstream file(file_path, ios::binary);
        if (!file.is_open())
        {
            SP_LOG_ERROR("failed to open file for writing: %s", file_path);
            return;
        }
    
        uint32_t width               = GetWidth();
        uint32_t height              = GetHeight();
        uint32_t height_data_size    = static_cast<uint32_t>(m_height_data.size());
        uint32_t vertex_count        = static_cast<uint32_t>(m_vertices.size());
        uint32_t index_count         = static_cast<uint32_t>(m_indices.size());
        uint32_t tile_count          = static_cast<uint32_t>(m_tile_vertices.size());
        uint32_t triangle_data_count = static_cast<uint32_t>(m_triangle_data.size());
        uint32_t offset_count        = static_cast<uint32_t>(m_tile_offsets.size());
        uint32_t position_count      = static_cast<uint32_t>(m_positions.size());
        uint64_t cache_hash          = ComputeCacheHash();
    
        // header
        file.write(reinterpret_cast<const char*>(&cache_hash), sizeof(uint64_t));
        file.write(reinterpret_cast<const char*>(&width), sizeof(uint32_t));
        file.write(reinterpret_cast<const char*>(&height), sizeof(uint32_t));
        file.write(reinterpret_cast<const char*>(&height_data_size), sizeof(uint32_t));
        file.write(reinterpret_cast<const char*>(&vertex_count), sizeof(uint32_t));
        file.write(reinterpret_cast<const char*>(&index_count), sizeof(uint32_t));
        file.write(reinterpret_cast<const char*>(&tile_count), sizeof(uint32_t));
        file.write(reinterpret_cast<const char*>(&triangle_data_count), sizeof(uint32_t));
        file.write(reinterpret_cast<const char*>(&offset_count), sizeof(uint32_t));
        file.write(reinterpret_cast<const char*>(&position_count), sizeof(uint32_t));
        file.write(reinterpret_cast<const char*>(&m_dense_width), sizeof(uint32_t));
        file.write(reinterpret_cast<const char*>(&m_dense_height), sizeof(uint32_t));
        file.write(reinterpret_cast<const char*>(&m_area_km2), sizeof(float));

        // main data
        file.write(reinterpret_cast<const char*>(m_height_data.data()), height_data_size * sizeof(float));
        file.write(reinterpret_cast<const char*>(m_vertices.data()), vertex_count * sizeof(RHI_Vertex_PosTexNorTan));
        file.write(reinterpret_cast<const char*>(m_indices.data()), index_count * sizeof(uint32_t));
        file.write(reinterpret_cast<const char*>(m_tile_offsets.data()), offset_count * sizeof(Vector3));
        file.write(reinterpret_cast<const char*>(m_positions.data()), position_count * sizeof(Vector3));
    
        // triangle data
        for (const auto& [tile_id, tile_triangles] : m_triangle_data)
        {
            file.write(reinterpret_cast<const char*>(&tile_id), sizeof(uint64_t));
            uint32_t triangle_count = static_cast<uint32_t>(tile_triangles.size());
            file.write(reinterpret_cast<const char*>(&triangle_count), sizeof(uint32_t));
            file.write(reinterpret_cast<const char*>(tile_triangles.data()), triangle_count * sizeof(TriangleData));
        }
    
        // tile data
        for (uint32_t i = 0; i < tile_count; i++)
        {
            uint32_t vertex_size = static_cast<uint32_t>(m_tile_vertices[i].size());
            uint32_t index_size  = static_cast<uint32_t>(m_tile_indices[i].size());
            file.write(reinterpret_cast<const char*>(&vertex_size), sizeof(uint32_t));
            file.write(reinterpret_cast<const char*>(&index_size), sizeof(uint32_t));
            file.write(reinterpret_cast<const char*>(m_tile_vertices[i].data()), vertex_size * sizeof(RHI_Vertex_PosTexNorTan));
            file.write(reinterpret_cast<const char*>(m_tile_indices[i].data()), index_size * sizeof(uint32_t));
        }
    
        file.close();
        SP_LOG_INFO("saved terrain cache: hash=%llu", cache_hash);
    }
    
    void Terrain::LoadFromFile(const char* file_path)
    {
        ifstream file(file_path, ios::binary);
        if (!file.is_open())
        {
            return;
        }
    
        // verify cache hash matches current parameters
        uint64_t stored_hash = 0;
        file.read(reinterpret_cast<char*>(&stored_hash), sizeof(uint64_t));
        
        uint64_t current_hash = ComputeCacheHash();
        if (stored_hash != current_hash)
        {
            SP_LOG_INFO("terrain cache invalidated (hash mismatch: %llu vs %llu)", stored_hash, current_hash);
            file.close();
            return;
        }

        uint32_t height_data_size    = 0;
        uint32_t vertex_count        = 0;
        uint32_t index_count         = 0;
        uint32_t tile_count          = 0;
        uint32_t triangle_data_count = 0;
        uint32_t offset_count        = 0;
        uint32_t position_count      = 0;
    
        file.read(reinterpret_cast<char*>(&m_width), sizeof(uint32_t));
        file.read(reinterpret_cast<char*>(&m_height), sizeof(uint32_t));
        file.read(reinterpret_cast<char*>(&height_data_size), sizeof(uint32_t));
        file.read(reinterpret_cast<char*>(&vertex_count), sizeof(uint32_t));
        file.read(reinterpret_cast<char*>(&index_count), sizeof(uint32_t));
        file.read(reinterpret_cast<char*>(&tile_count), sizeof(uint32_t));
        file.read(reinterpret_cast<char*>(&triangle_data_count), sizeof(uint32_t));
        file.read(reinterpret_cast<char*>(&offset_count), sizeof(uint32_t));
        file.read(reinterpret_cast<char*>(&position_count), sizeof(uint32_t));
        file.read(reinterpret_cast<char*>(&m_dense_width), sizeof(uint32_t));
        file.read(reinterpret_cast<char*>(&m_dense_height), sizeof(uint32_t));
        file.read(reinterpret_cast<char*>(&m_area_km2), sizeof(float));

        if (tile_count > 10000 || offset_count > 10000)
        {
            SP_LOG_ERROR("invalid tile_count (%u) or offset_count (%u), aborting load", tile_count, offset_count);
            file.close();
            return;
        }
    
        m_height_data.resize(height_data_size);
        m_vertices.resize(vertex_count);
        m_indices.resize(index_count);
        m_tile_vertices.resize(tile_count);
        m_tile_indices.resize(tile_count);
        m_tile_offsets.resize(offset_count);
        m_positions.resize(position_count);
        m_triangle_data.clear();
    
        file.read(reinterpret_cast<char*>(m_height_data.data()), height_data_size * sizeof(float));
        file.read(reinterpret_cast<char*>(m_vertices.data()), vertex_count * sizeof(RHI_Vertex_PosTexNorTan));
        file.read(reinterpret_cast<char*>(m_indices.data()), index_count * sizeof(uint32_t));
        file.read(reinterpret_cast<char*>(m_tile_offsets.data()), offset_count * sizeof(Vector3));
        file.read(reinterpret_cast<char*>(m_positions.data()), position_count * sizeof(Vector3));
    
        for (uint32_t i = 0; i < triangle_data_count; i++)
        {
            uint64_t tile_id;
            uint32_t triangle_count;
            file.read(reinterpret_cast<char*>(&tile_id), sizeof(uint64_t));
            file.read(reinterpret_cast<char*>(&triangle_count), sizeof(uint32_t));
            vector<TriangleData>& tile_triangles = m_triangle_data[tile_id];
            tile_triangles.resize(triangle_count);
            file.read(reinterpret_cast<char*>(tile_triangles.data()), triangle_count * sizeof(TriangleData));
        }
    
        for (uint32_t i = 0; i < tile_count; i++)
        {
            uint32_t vertex_size, index_size;
            file.read(reinterpret_cast<char*>(&vertex_size), sizeof(uint32_t));
            file.read(reinterpret_cast<char*>(&index_size), sizeof(uint32_t));
            m_tile_vertices[i].resize(vertex_size);
            m_tile_indices[i].resize(index_size);
            file.read(reinterpret_cast<char*>(m_tile_vertices[i].data()), vertex_size * sizeof(RHI_Vertex_PosTexNorTan));
            file.read(reinterpret_cast<char*>(m_tile_indices[i].data()), index_size * sizeof(uint32_t));
        }
    
        file.close();
        SP_LOG_INFO("loaded terrain from cache: hash=%llu", stored_hash);
    }

    void Terrain::Generate()
    {
        bool expected = false;
        if (!m_is_generating.compare_exchange_strong(expected, true))
        {
            SP_LOG_WARNING("terrain generation already in progress");
            return;
        }

        if (!m_height_map_seed)
        {
            SP_LOG_WARNING("assign a height map before generating terrain");
            m_is_generating.store(false);
            return;
        }

        // min == max collapses every pixel to one height, looks like a flat plane
        if (abs(m_max_y - m_min_y) < epsilon)
        {
            SP_LOG_WARNING(
                "terrain min height (%.1f) equals max height, using 0 to 755 for zakynthos-scale relief",
                m_min_y
            );
            m_min_y = 0.0f;
            m_max_y = 755.0f;
        }
    
        // 8 cpu jobs, 1 gpu upload on the main thread
        uint32_t job_count = 9;
        ProgressTracker::GetProgress(ProgressType::Terrain).Start(job_count, "generating terrain...");
    
        // try loading from cache in the world resource directory
        const string cache_file = get_terrain_cache_bin_path();
        bool loaded_from_cache  = false;

        LoadFromFile(cache_file.c_str());
        if (!m_vertices.empty())
        {
            loaded_from_cache = true;
            ProgressTracker::GetProgress(ProgressType::Terrain).SetText("loaded from cache");

            // old caches still have a flat coast, lock it without rerunning erosion
            const bool shoreline_moved = ApplyShorelineLock();
            if (shoreline_moved)
            {
                ProgressTracker::GetProgress(ProgressType::Terrain).SetText("locking shoreline...");
            }

            // cached meshes used unit-grid normals, rebuild so slope matches the heightfield
            ProgressTracker::GetProgress(ProgressType::Terrain).SetText("rebuilding normals...");
            m_vertices.resize(m_dense_width * m_dense_height);
            m_indices.resize((m_dense_width - 1) * (m_dense_height - 1) * 6);
            TerrainSystem::GenerateVerticesAndIndices(
                m_vertices,
                m_indices,
                m_positions,
                m_dense_width,
                m_dense_height
            );
            TerrainSystem::GenerateNormals(m_vertices, m_dense_width, m_dense_height);
            geometry_processing::split_surface_into_tiles(
                m_vertices,
                m_indices,
                m_tile_count,
                m_tile_vertices,
                m_tile_indices,
                m_tile_offsets
            );
            if (shoreline_moved)
            {
                m_triangle_data.clear();
                for (uint32_t tile_index = 0; tile_index < m_tile_vertices.size(); tile_index++)
                {
                    placement::compute_triangle_data(
                        m_tile_vertices,
                        m_tile_indices,
                        tile_index,
                        m_triangle_data
                    );
                }
                SaveToFile(cache_file.c_str());
            }

            for (uint32_t i = 0; i < 8; i++)
            {
                ProgressTracker::GetProgress(ProgressType::Terrain).JobDone();
            }
        }

        if (!loaded_from_cache)
        {
            SP_LOG_INFO("generating terrain from scratch...");
    
            // 1. process height map
            ProgressTracker::GetProgress(ProgressType::Terrain).SetText("processing height map...");
            TerrainSystem::GetValuesFromHeightMap(m_height_data, m_height_map_seed, m_min_y, m_max_y, m_smoothing, m_create_border);
            m_width  = m_height_map_seed->GetWidth();
            m_height = m_height_map_seed->GetHeight();
            TerrainSystem::DensifyHeightMap(m_height_data, m_width, m_height, m_density);
            m_dense_width  = m_density * (m_width - 1) + 1;
            m_dense_height = m_density * (m_height - 1) + 1;
            ProgressTracker::GetProgress(ProgressType::Terrain).JobDone();
    
            // 2. generate positions
            ProgressTracker::GetProgress(ProgressType::Terrain).SetText("generating positions...");
            m_positions.resize(m_dense_width * m_dense_height);
            TerrainSystem::GeneratePositions(m_positions, m_height_data, m_dense_width, m_dense_height, m_density, m_scale);
            ProgressTracker::GetProgress(ProgressType::Terrain).JobDone();

            // 3. apply perlin noise
            ProgressTracker::GetProgress(ProgressType::Terrain).SetText("applying perlin noise...");
            TerrainSystem::ApplyPerlinNoise(m_positions, m_dense_width, m_dense_height, m_level_sea);
            ProgressTracker::GetProgress(ProgressType::Terrain).JobDone();

            // 4. apply erosion, keeping what it moved so the texturing can key off it
            ProgressTracker::GetProgress(ProgressType::Terrain).SetText("applying erosion...");
            TerrainSystem::ApplyErosion(m_positions, m_dense_width, m_dense_height, m_level_sea, 1.0f, &m_erosion_maps);
            ProgressTracker::GetProgress(ProgressType::Terrain).JobDone();

            // lift the real coastline above the waves and cut a beach
            ProgressTracker::GetProgress(ProgressType::Terrain).SetText("locking shoreline...");
            ApplyShorelineLock();

            ProgressTracker::GetProgress(ProgressType::Terrain).SetText("carving channels...");
            ApplyFlowChannelCarve();

            // 5. generate vertices and indices
            ProgressTracker::GetProgress(ProgressType::Terrain).SetText("generating mesh...");
            m_vertices.resize(m_dense_width * m_dense_height);
            m_indices.resize((m_dense_width - 1) * (m_dense_height - 1) * 6);
            TerrainSystem::GenerateVerticesAndIndices(m_vertices, m_indices, m_positions, m_dense_width, m_dense_height);
            ProgressTracker::GetProgress(ProgressType::Terrain).JobDone();
    
            // 6. generate normals
            ProgressTracker::GetProgress(ProgressType::Terrain).SetText("generating normals...");
            TerrainSystem::GenerateNormals(m_vertices, m_dense_width, m_dense_height);
            ProgressTracker::GetProgress(ProgressType::Terrain).JobDone();
    
            // 7. split into tiles
            ProgressTracker::GetProgress(ProgressType::Terrain).SetText("splitting into tiles...");
            geometry_processing::split_surface_into_tiles(m_vertices, m_indices, m_tile_count, m_tile_vertices, m_tile_indices, m_tile_offsets);
            ProgressTracker::GetProgress(ProgressType::Terrain).JobDone();

            // 8. compute triangle data for placement
            ProgressTracker::GetProgress(ProgressType::Terrain).SetText("computing placement data...");
            for (uint32_t tile_index = 0; tile_index < m_tile_vertices.size(); tile_index++)
            {
                placement::compute_triangle_data(m_tile_vertices, m_tile_indices, tile_index, m_triangle_data);
            }
            ProgressTracker::GetProgress(ProgressType::Terrain).JobDone();

            SaveToFile(cache_file.c_str());
        }

        BakeTerrainMaps();
        BakeHeightMapPixels();

        // the dense erosion grid is only needed for the analysis bake above, it is tens of
        // megabytes and nothing reads it afterwards
        m_erosion_maps = TerrainErosionMaps();

        // compute stats
        m_height_samples = m_dense_width * m_dense_height;
        m_vertex_count   = static_cast<uint32_t>(m_vertices.size());
        m_index_count    = static_cast<uint32_t>(m_indices.size());
        m_triangle_count = m_index_count / 3;

        // surface area is expensive to compute, only recompute on cache miss (cached value was read from disk)
        if (!loaded_from_cache)
        {
            m_area_km2 = TerrainSystem::ComputeSurfaceAreaKm2(m_vertices, m_indices);
        }

        ProgressTracker::GetProgress(ProgressType::Terrain).SetText("building mesh...");

        m_mesh_pending.reset();
        BuildCpuMesh();

        m_gpu_commit_pending.store(true, memory_order_release);
    }

    void Terrain::FinishGenerate()
    {
        if (!m_is_generating.load(memory_order_acquire) &&
            !m_gpu_commit_pending.load(memory_order_acquire) &&
            !m_props_commit_pending.load(memory_order_acquire))
        {
            return;
        }

        m_gpu_commit_pending.store(false, memory_order_release);
        m_props_commit_pending.store(false, memory_order_release);
        m_mesh_pending.reset();
        ProgressTracker::GetProgress(ProgressType::Terrain).Complete();
        m_is_generating.store(false, memory_order_release);
    }

    void Terrain::BuildCpuMesh()
    {
        m_mesh_pending = make_shared<Mesh>();
        m_mesh_pending->SetObjectName("terrain_mesh");
        m_mesh_pending->SetFlag(static_cast<uint32_t>(MeshFlags::PostProcessOptimize), false);
        m_mesh_pending->SetFlag(static_cast<uint32_t>(MeshFlags::PostProcessPreserveTerrainEdges), true);

        for (uint32_t tile_index = 0; tile_index < static_cast<uint32_t>(m_tile_vertices.size()); tile_index++)
        {
            uint32_t sub_mesh_index = 0;
            m_mesh_pending->AddGeometry(m_tile_vertices[tile_index], m_tile_indices[tile_index], true, &sub_mesh_index);
        }
    }

    void Terrain::Tick()
    {
        if (m_gpu_commit_pending.exchange(false, memory_order_acq_rel))
        {
            CommitGpu();
            return;
        }

        if (m_props_commit_pending.exchange(false, memory_order_acq_rel))
        {
            CommitProps();
        }
    }

    void Terrain::CommitGpu()
    {
        ProgressTracker::GetProgress(ProgressType::Terrain).SetText("uploading gpu mesh...");

        DetachTileMeshes();
        ClearTileEntities();

        if (m_mesh_pending)
        {
            ResourceCache::Remove(m_mesh);
            m_mesh = m_mesh_pending;
            m_mesh_pending.reset();
            m_mesh->CreateGpuBuffers();
        }

        if ((!m_mesh || m_mesh->GetVertexCount() == 0) && !m_tile_vertices.empty())
        {
            BuildCpuMesh();
            if (m_mesh_pending)
            {
                ResourceCache::Remove(m_mesh);
                m_mesh = m_mesh_pending;
                m_mesh_pending.reset();
                m_mesh->CreateGpuBuffers();
            }
        }

        UploadHeightMapTextures();
        UploadTerrainMaps();

        CreateTileEntities();
        RefreshPhysics();
        RefreshLayers();
        PushToRenderer();
        SpawnFlowRivers();

        ProgressTracker::GetProgress(ProgressType::Terrain).JobDone();
        ProgressTracker::GetProgress(ProgressType::Terrain).Complete();

        m_vertices.clear();
        m_indices.clear();
        m_tile_vertices.clear();
        m_tile_indices.clear();

        SnapshotBaseline();

        if (m_spawn_biome_props)
        {
            m_props_commit_pending.store(true, memory_order_release);
            return;
        }

        m_is_generating.store(false, memory_order_release);
    }

    void Terrain::CommitProps()
    {
        WorldHelpers::PopulateTerrainBiomeProps(this);
        m_is_generating.store(false, memory_order_release);
    }


    TerrainGridMapping Terrain::GetGridMapping() const
    {
        return TerrainSystem::ComputeGridMapping(m_dense_width, m_dense_height, m_density, m_scale);
    }

    bool Terrain::Raycast(const Ray& ray, Vector3& hit_out) const
    {
        if (!HasHeightfield())
        {
            return false;
        }

        Ray local_ray = ray;
        if (Entity* entity = GetEntity())
        {
            Matrix inv = entity->GetMatrix().Inverted();
            Vector3 origin_local = inv * ray.GetStart();
            Vector3 far_local    = inv * ray.GetDirection();
            local_ray.m_origin    = origin_local;
            local_ray.m_direction = far_local;
        }

        Vector3 local_hit;
        if (!TerrainSystem::RaycastHeightfield(
            local_ray,
            m_positions,
            m_dense_width,
            m_dense_height,
            GetGridMapping(),
            local_hit
        ))
        {
            return false;
        }

        if (Entity* entity = GetEntity())
        {
            hit_out = entity->GetMatrix() * local_hit;
        }
        else
        {
            hit_out = local_hit;
        }

        return true;
    }

    bool Terrain::SampleHeight(float world_x, float world_z, float& height_out) const
    {
        if (!HasHeightfield())
        {
            return false;
        }

        float local_x = world_x;
        float local_z = world_z;
        if (Entity* entity = GetEntity())
        {
            Vector3 local = entity->GetMatrix().Inverted() * Vector3(world_x, 0.0f, world_z);
            local_x = local.x;
            local_z = local.z;
        }

        const float local_height = TerrainSystem::SampleHeight(
            m_positions,
            m_dense_width,
            m_dense_height,
            local_x,
            local_z,
            GetGridMapping()
        );

        if (Entity* entity = GetEntity())
        {
            height_out = (entity->GetMatrix() * Vector3(local_x, local_height, local_z)).y;
        }
        else
        {
            height_out = local_height;
        }

        return true;
    }

    bool Terrain::SampleNormal(float world_x, float world_z, Vector3& normal_out) const
    {
        if (!HasHeightfield())
        {
            return false;
        }

        float local_x = world_x;
        float local_z = world_z;
        Quaternion terrain_rotation = Quaternion::Identity;
        if (Entity* entity = GetEntity())
        {
            Vector3 local = entity->GetMatrix().Inverted() * Vector3(world_x, 0.0f, world_z);
            local_x = local.x;
            local_z = local.z;
            terrain_rotation = entity->GetRotation();
        }

        Vector3 local_normal = TerrainSystem::SampleNormal(
            m_positions,
            m_dense_width,
            m_dense_height,
            local_x,
            local_z,
            GetGridMapping()
        );

        normal_out = (terrain_rotation * local_normal).Normalized();
        if (normal_out.LengthSquared() < math::epsilon)
        {
            normal_out = Vector3::Up;
        }

        return true;
    }

    Terrain* Terrain::FindActive()
    {
        for (Entity* entity : World::GetEntities())
        {
            if (!entity)
            {
                continue;
            }

            if (Terrain* terrain = entity->GetComponent<Terrain>())
            {
                if (terrain->HasHeightfield())
                {
                    return terrain;
                }
            }
        }

        return nullptr;
    }

    bool Terrain::SnapEntityToTerrain(Entity* entity, float offset)
    {
        return SnapEntitiesToTerrain({ entity }, offset) > 0;
    }

    bool Terrain::SnapEntityToFlatTerrain(Entity* entity, float offset)
    {
        return SnapEntitiesToFlatTerrain({ entity }, offset) > 0;
    }

    uint32_t Terrain::SnapEntitiesToFlatTerrain(const vector<Entity*>& entities, float offset)
    {
        Terrain* terrain = FindActive();
        if (!terrain)
        {
            SP_LOG_WARNING("no terrain with a heightfield to flatten");
            return 0;
        }

        // horizontal footprint of everything the snap is about to move
        float min_x          = numeric_limits<float>::max();
        float max_x          = -numeric_limits<float>::max();
        float min_z          = numeric_limits<float>::max();
        float max_z          = -numeric_limits<float>::max();
        Entity* driver_min_x = nullptr;
        Entity* driver_max_x = nullptr;
        Entity* driver_min_z = nullptr;
        Entity* driver_max_z = nullptr;
        bool has_footprint   = false;
        uint32_t mesh_count  = 0;

        auto grow = [&](const Vector3& point, Entity* part)
        {
            if (point.x < min_x)
            {
                min_x        = point.x;
                driver_min_x = part;
            }
            if (point.x > max_x)
            {
                max_x        = point.x;
                driver_max_x = part;
            }
            if (point.z < min_z)
            {
                min_z        = point.z;
                driver_min_z = part;
            }
            if (point.z > max_z)
            {
                max_z        = point.z;
                driver_max_z = part;
            }

            has_footprint = true;
        };

        // only real geometry defines the footprint, a marker or a control point out on the
        // horizon would otherwise stretch the rectangle across the whole map
        auto grow_by_entity = [&grow, &mesh_count](Entity* part)
        {
            BoundingBox aabb;
            if (!part || !get_world_aabb(part, aabb))
            {
                return;
            }

            const Vector3 box_min = aabb.GetMin();
            const Vector3 box_max = aabb.GetMax();

            // an empty or non finite box would drag the rectangle out to infinity
            const bool usable =
                isfinite(box_min.x) && isfinite(box_min.z) &&
                isfinite(box_max.x) && isfinite(box_max.z) &&
                box_min.x <= box_max.x && box_min.z <= box_max.z;

            if (!usable)
            {
                return;
            }

            mesh_count++;
            grow(box_min, part);
            grow(box_max, part);
        };

        for (Entity* entity : entities)
        {
            vector<Entity*> meshes;
            collect_snappable_entities(entity, meshes);
            for (Entity* part : meshes)
            {
                grow_by_entity(part);
            }

            // a spline only counts once it has an actual road mesh
            vector<Entity*> splines;
            collect_spline_entities(entity, splines);
            for (Entity* spline_entity : splines)
            {
                grow_by_entity(spline_entity);
            }
        }

        if (!has_footprint)
        {
            SP_LOG_WARNING("nothing snappable in the selection, no footprint to flatten");
            return 0;
        }

        // level the pad at the average ground height under the footprint, least cut and fill
        const uint32_t axis_samples = 16;
        float height_sum            = 0.0f;
        uint32_t height_count       = 0;

        for (uint32_t iz = 0; iz < axis_samples; iz++)
        {
            for (uint32_t ix = 0; ix < axis_samples; ix++)
            {
                const float u = static_cast<float>(ix) / static_cast<float>(axis_samples - 1);
                const float v = static_cast<float>(iz) / static_cast<float>(axis_samples - 1);
                const float x = min_x + (max_x - min_x) * u;
                const float z = min_z + (max_z - min_z) * v;

                float sampled_height = 0.0f;
                if (terrain->SampleHeight(x, z, sampled_height))
                {
                    height_sum += sampled_height;
                    height_count++;
                }
            }
        }

        if (height_count == 0)
        {
            SP_LOG_WARNING("the selection footprint does not overlap the terrain");
            return 0;
        }

        const float pad_height = height_sum / static_cast<float>(height_count);

        // the ramp begins where the pad ends, so a rectangle drawn tight around the geometry puts
        // rising ground right under the outer edge of every ground plane and buries it, the pad
        // has to reach past the footprint before it starts climbing back
        const float overhang = min(max(max(max_x - min_x, max_z - min_z) * 0.02f, 10.0f), 100.0f);
        min_x -= overhang;
        max_x += overhang;
        min_z -= overhang;
        max_z += overhang;

        // the pad itself stays a hard rectangle, the ramp only lives outside it
        const float footprint = max(max_x - min_x, max_z - min_z);
        const float margin    = min(max(footprint * 0.15f, 25.0f), 500.0f);

        auto driver_name = [](Entity* part)
        {
            return part ? part->GetObjectName().c_str() : "none";
        };

        SP_LOG_INFO(
            "snap flat: %u meshes, rect %.0f x %.0f, pad at %.1f, ramp %.0f",
            mesh_count, max_x - min_x, max_z - min_z, pad_height, margin
        );
        SP_LOG_INFO(
            "snap flat: extents from -x '%s', +x '%s', -z '%s', +z '%s'",
            driver_name(driver_min_x),
            driver_name(driver_max_x),
            driver_name(driver_min_z),
            driver_name(driver_max_z)
        );

        terrain->FlattenRegion(min_x, min_z, max_x, max_z, pad_height, margin);

        return SnapEntitiesToTerrain(entities, offset);
    }

    uint32_t Terrain::SnapEntitiesToTerrain(const vector<Entity*>& entities, float offset)
    {
        Terrain* terrain = FindActive();
        unordered_set<Entity*> unique_splines;
        unordered_set<Entity*> unique_targets;
        vector<Entity*> splines;
        vector<Entity*> targets;

        for (Entity* entity : entities)
        {
            vector<Entity*> collected_splines;
            collect_spline_entities(entity, collected_splines);
            for (Entity* spline_entity : collected_splines)
            {
                if (unique_splines.insert(spline_entity).second)
                {
                    splines.push_back(spline_entity);
                }
            }

            vector<Entity*> collected;
            collect_snappable_entities(entity, collected);
            for (Entity* target : collected)
            {
                if (unique_targets.insert(target).second)
                {
                    targets.push_back(target);
                }
            }
        }

        // an entity that has not been snapped yet may still be floating, so it cannot serve as
        // ground, once it lands it becomes a valid surface for whatever sits on top of it
        unordered_set<Entity*> pending = unique_targets;
        for (Entity* spline_entity : splines)
        {
            pending.insert(spline_entity);

            vector<Entity*> spline_descendants;
            spline_entity->GetDescendants(&spline_descendants);
            for (Entity* descendant : spline_descendants)
            {
                pending.insert(descendant);
            }
        }

        uint32_t count = 0;
        for (Entity* spline_entity : splines)
        {
            if (snap_spline_to_terrain(spline_entity, terrain, pending, offset))
            {
                count++;
            }
        }

        // work from the ground up, the lowest entity settles first and then counts as a surface,
        // so a platform lands on the terrain and everything standing on it stacks onto the platform
        struct snap_order
        {
            Entity* entity;
            float bottom;
            uint32_t depth;
        };

        vector<snap_order> ordered;
        ordered.reserve(targets.size());

        for (Entity* target : targets)
        {
            uint32_t depth = 0;
            for (Entity* parent = target->GetParent(); parent; parent = parent->GetParent())
            {
                depth++;
            }

            // a parent sorts with its lowest batch descendant, it must never move after they settle
            float bottom = entity_bottom(target);
            vector<Entity*> descendants;
            target->GetDescendants(&descendants);
            for (Entity* descendant : descendants)
            {
                if (unique_targets.count(descendant) > 0)
                {
                    bottom = min(bottom, entity_bottom(descendant));
                }
            }

            ordered.push_back({ target, bottom, depth });
        }

        sort(ordered.begin(), ordered.end(), [](const snap_order& a, const snap_order& b)
        {
            if (a.bottom != b.bottom)
            {
                return a.bottom < b.bottom;
            }

            return a.depth < b.depth;
        });

        struct snap_shift
        {
            Vector3 position;
            float delta_y;
        };

        vector<snap_shift> shifts;
        unordered_set<Entity*> snapped;

        for (const snap_order& item : ordered)
        {
            // it has had its turn, from now on it is ground for whatever sits above it
            pending.erase(item.entity);

            const float y_before = item.entity->GetPosition().y;
            if (snap_mesh_entity(item.entity, terrain, pending, offset))
            {
                const Vector3 landed = item.entity->GetPosition();
                shifts.push_back({ landed, landed.y - y_before });
                snapped.insert(item.entity);
                count++;
            }
        }

        // lights, markers and other mesh free entities cannot be raycast against anything, so they
        // take the vertical shift of the nearest thing that did move and keep their local layout
        if (!shifts.empty())
        {
            unordered_set<Entity*> has_snapped_below;
            for (Entity* entity : snapped)
            {
                for (Entity* ancestor = entity; ancestor; ancestor = ancestor->GetParent())
                {
                    if (!has_snapped_below.insert(ancestor).second)
                    {
                        break;
                    }
                }
            }

            vector<Entity*> orphans;
            for (Entity* entity : entities)
            {
                collect_unsnapped_roots(entity, snapped, has_snapped_below, orphans);
            }

            for (Entity* orphan : orphans)
            {
                const Vector3 position = orphan->GetPosition();

                float best_distance = numeric_limits<float>::max();
                float best_delta    = 0.0f;
                for (const snap_shift& shift : shifts)
                {
                    const float delta_x  = shift.position.x - position.x;
                    const float delta_z  = shift.position.z - position.z;
                    const float distance = delta_x * delta_x + delta_z * delta_z;

                    if (distance < best_distance)
                    {
                        best_distance = distance;
                        best_delta    = shift.delta_y;
                    }
                }

                orphan->SetPosition(Vector3(position.x, position.y + best_delta, position.z));
                count++;
            }

            SP_LOG_INFO(
                "snap: %zu entities snapped, %zu mesh free entities carried by their neighbours",
                shifts.size(),
                orphans.size()
            );
        }

        // anything visible left hanging in the air, or swallowed by the ground, is a bug, name the
        // worst offenders rather than leaving them to be spotted by eye
        {
            struct offender
            {
                Entity* entity;
                float amount;
            };

            vector<offender> floating;
            vector<offender> buried;

            for (Entity* entity : entities)
            {
                vector<Entity*> parts;
                parts.push_back(entity);
                entity->GetDescendants(&parts);

                for (Entity* part : parts)
                {
                    if (!part || is_terrain_tile_or_water(part))
                    {
                        continue;
                    }

                    // a bare group node has no business on the ground, only things you can see
                    Render* render    = part->GetComponent<Render>();
                    const bool meshed = render && render->GetMesh();
                    const bool visible =
                        meshed ||
                        part->GetComponent<Light>() ||
                        part->GetComponent<Camera>();

                    if (!visible)
                    {
                        continue;
                    }

                    const Vector3 position = part->GetPosition();
                    float ground           = 0.0f;
                    if (!terrain->SampleHeight(position.x, position.z, ground))
                    {
                        continue;
                    }

                    // a spline pivot sits far from its own road mesh, so geometry is judged by its
                    // box and only a light or a camera has to fall back on the pivot
                    BoundingBox aabb;
                    const bool has_box = meshed && get_world_aabb(part, aabb);
                    const float bottom = has_box ? aabb.GetMin().y : position.y;

                    if (bottom - ground > 50.0f)
                    {
                        floating.push_back({ part, bottom - ground });
                    }

                    if (!has_box)
                    {
                        continue;
                    }

                    const Vector3 box_min            = aabb.GetMin();
                    const Vector3 box_max            = aabb.GetMax();
                    const TerrainGridMapping mapping = terrain->GetGridMapping();
                    const uint32_t samples_x         = footprint_samples(box_max.x - box_min.x, mapping.scale_x);
                    const uint32_t samples_z         = footprint_samples(box_max.z - box_min.z, mapping.scale_z);

                    float highest = ground;
                    for (uint32_t iz = 0; iz < samples_z; iz++)
                    {
                        for (uint32_t ix = 0; ix < samples_x; ix++)
                        {
                            const float u = (samples_x > 1) ? static_cast<float>(ix) / static_cast<float>(samples_x - 1) : 0.5f;
                            const float v = (samples_z > 1) ? static_cast<float>(iz) / static_cast<float>(samples_z - 1) : 0.5f;

                            float sampled = 0.0f;
                            if (terrain->SampleHeight(
                                box_min.x + (box_max.x - box_min.x) * u,
                                box_min.z + (box_max.z - box_min.z) * v,
                                sampled
                            ))
                            {
                                highest = max(highest, sampled);
                            }
                        }
                    }

                    if (box_max.y < highest - 0.5f)
                    {
                        buried.push_back({ part, highest - box_max.y });
                    }
                }
            }

            auto worst_first = [](vector<offender>& list)
            {
                sort(list.begin(), list.end(), [](const offender& a, const offender& b)
                {
                    return a.amount > b.amount;
                });

                return min<size_t>(list.size(), 12);
            };

            if (!floating.empty())
            {
                const size_t reported = worst_first(floating);
                SP_LOG_WARNING("snap: %zu visible entities are still over 50 m above the ground", floating.size());

                for (size_t i = 0; i < reported; i++)
                {
                    Entity* parent = floating[i].entity->GetParent();
                    SP_LOG_WARNING(
                        "snap: '%s' floats %.0f m, parent '%s'",
                        floating[i].entity->GetObjectName().c_str(),
                        floating[i].amount,
                        parent ? parent->GetObjectName().c_str() : "none"
                    );
                }
            }

            if (!buried.empty())
            {
                const size_t reported = worst_first(buried);
                SP_LOG_WARNING("snap: %zu meshes are completely under the terrain", buried.size());

                for (size_t i = 0; i < reported; i++)
                {
                    Entity* parent = buried[i].entity->GetParent();
                    SP_LOG_WARNING(
                        "snap: '%s' is buried %.1f m, parent '%s'",
                        buried[i].entity->GetObjectName().c_str(),
                        buried[i].amount,
                        parent ? parent->GetObjectName().c_str() : "none"
                    );
                }
            }
        }

        return count;
    }

    void Terrain::ApplyBrush(const Vector3& world_center, const TerrainBrush& brush)
    {
        if (!HasHeightfield())
        {
            return;
        }

        // transform brush center into terrain local space
        Vector3 local_center = world_center;
        if (Entity* entity = GetEntity())
        {
            local_center = entity->GetMatrix().Inverted() * world_center;
        }

        TerrainSystem::ApplyBrush(
            m_positions,
            &m_height_data,
            m_dense_width,
            m_dense_height,
            GetGridMapping(),
            local_center,
            brush
        );
    }

    bool Terrain::FlattenRegion(
        float world_min_x,
        float world_min_z,
        float world_max_x,
        float world_max_z,
        float world_height,
        float blend_margin
    )
    {
        if (!HasHeightfield())
        {
            return false;
        }

        // the grid lives in terrain local space
        Vector3 local_min(world_min_x, world_height, world_min_z);
        Vector3 local_max(world_max_x, world_height, world_max_z);
        if (Entity* entity = GetEntity())
        {
            const Matrix inverse = entity->GetMatrix().Inverted();
            local_min = inverse * local_min;
            local_max = inverse * local_max;
        }

        const float min_x    = min(local_min.x, local_max.x);
        const float max_x    = max(local_min.x, local_max.x);
        const float min_z    = min(local_min.z, local_max.z);
        const float max_z    = max(local_min.z, local_max.z);
        const float target_y = (local_min.y + local_max.y) * 0.5f;
        const float margin   = max(blend_margin, 0.0f);

        for (Vector3& position : m_positions)
        {
            // how far outside the rectangle the sample sits, zero anywhere inside it
            const float distance_x = max(max(min_x - position.x, position.x - max_x), 0.0f);
            const float distance_z = max(max(min_z - position.z, position.z - max_z), 0.0f);
            const float distance   = sqrtf(distance_x * distance_x + distance_z * distance_z);

            if (distance > margin)
            {
                continue;
            }

            // dead flat inside the rectangle, smooth ramp back to the original ground outside it
            float weight = 1.0f;
            if (distance > 0.0f && margin > 0.0f)
            {
                const float t = distance / margin;
                weight        = 1.0f - (t * t * (3.0f - 2.0f * t));
            }

            position.y += (target_y - position.y) * weight;
        }

        if (!m_height_data.empty() && m_height_data.size() == m_positions.size())
        {
            TerrainSystem::SyncHeightDataFromPositions(m_height_data, m_positions);
        }

        RebuildSurface(true);
        return true;
    }

    float Terrain::ResolveSeaLevelLocal() const
    {
        float sea_world = m_level_sea;
        for (Entity* entity : World::GetEntities())
        {
            if (!entity)
            {
                continue;
            }

            if (Water* water = entity->GetComponent<Water>())
            {
                sea_world = water->GetSeaLevel();
                break;
            }
        }

        float entity_y = 0.0f;
        if (Entity* entity = GetEntity())
        {
            entity_y = entity->GetPosition().y;
        }

        return sea_world - entity_y;
    }

    bool Terrain::ApplyShorelineLock()
    {
        if (!HasHeightfield())
        {
            return false;
        }

        return TerrainSystem::ApplyCoastalProfile(
            m_positions,
            m_height_data.empty() ? nullptr : &m_height_data,
            m_dense_width,
            m_dense_height,
            GetGridMapping(),
            ResolveSeaLevelLocal()
        );
    }

    void Terrain::LockShoreline()
    {
        if (!ApplyShorelineLock())
        {
            SP_LOG_INFO("shoreline already locked");
            return;
        }

        RebuildSurface(true);
        SnapshotBaseline();
    }

    bool Terrain::ApplyFlowChannelCarve()
    {
        if (!HasHeightfield())
        {
            return false;
        }

        TerrainChannelCarve params;
        params.sea_level = ResolveSeaLevelLocal();

        return TerrainSystem::CarveFlowChannels(
            m_positions,
            m_height_data.empty() ? nullptr : &m_height_data,
            m_dense_width,
            m_dense_height,
            params
        );
    }

    void Terrain::CarveFlowChannels()
    {
        if (!ApplyFlowChannelCarve())
        {
            SP_LOG_INFO("no flow channels to carve");
            return;
        }

        RebuildSurface(true);
        SnapshotBaseline();
    }

    void Terrain::SpawnFlowRivers()
    {
        Entity* root = GetEntity();
        if (!root || !HasHeightfield() || m_tile_offsets.empty())
        {
            return;
        }

        // leftover from when rivers sat on the terrain root
        vector<Entity*> root_children = root->GetChildren();
        for (Entity* child : root_children)
        {
            if (child && child->GetObjectName().rfind("river_", 0) == 0)
            {
                World::RemoveEntity(child);
            }
        }

        vector<Entity*> tiles(m_tile_offsets.size(), nullptr);
        for (Entity* child : root->GetChildren())
        {
            const int index = ParseTileIndex(child);
            if (index >= 0 && static_cast<uint32_t>(index) < tiles.size())
            {
                tiles[static_cast<uint32_t>(index)] = child;
            }
        }

        vector<TerrainFlowPath> paths;
        TerrainSystem::TraceFlowPaths(
            paths,
            m_positions,
            m_dense_width,
            m_dense_height,
            ResolveSeaLevelLocal()
        );

        if (paths.empty())
        {
            return;
        }

        shared_ptr<Material> water_material = make_shared<Material>();
        water_material->SetResourceName("river_water" + string(EXTENSION_MATERIAL));
        water_material->SetColor(Color(0.0f, 0.09f, 0.13f, 0.9f));
        water_material->SetProperty(MaterialProperty::Roughness,            0.05f);
        water_material->SetProperty(MaterialProperty::SubsurfaceScattering, 0.3f);
        water_material->SetProperty(MaterialProperty::IsWater,              1.0f);
        water_material->SetProperty(MaterialProperty::Ior,                  Material::EnumToIor(MaterialIor::Water));
        water_material->SetProperty(MaterialProperty::CullMode,             static_cast<float>(RHI_CullMode::None));

        for (uint32_t i = 0; i < paths.size(); i++)
        {
            const TerrainFlowPath& path = paths[i];
            if (path.points.size() < 3)
            {
                continue;
            }

            const Vector3 mid = path.points[path.points.size() / 2];
            uint32_t tile_index = 0;
            float best_dist = numeric_limits<float>::max();
            for (uint32_t t = 0; t < m_tile_offsets.size(); t++)
            {
                const float dx = mid.x - m_tile_offsets[t].x;
                const float dz = mid.z - m_tile_offsets[t].z;
                const float dist = dx * dx + dz * dz;
                if (dist < best_dist)
                {
                    best_dist  = dist;
                    tile_index = t;
                }
            }

            Entity* tile = tiles[tile_index];
            if (!tile)
            {
                continue;
            }

            Entity* river = World::CreateEntity();
            river->SetObjectName("river_" + to_string(i));
            river->SetTransient(true);
            river->SetParent(tile);
            river->SetPositionLocal(Vector3::Zero);

            Spline* spline = river->AddComponent<Spline>();
            spline->SetMeshEnabled(true);
            spline->SetProfile(SplineProfile::Road);
            spline->SetClosedLoop(false);
            spline->SetRoadWidth(path.width_start);
            spline->SetRoadWidthEnd(path.width_end);
            spline->SetConformToTerrain(true);
            spline->SetTerrainOffset(0.28f);
            spline->SetResolution(6);

            const Vector3 tile_offset = m_tile_offsets[tile_index];
            for (const Vector3& point : path.points)
            {
                spline->AddControlPoint(point - tile_offset);
            }

            for (Entity* point : river->GetChildren())
            {
                if (point)
                {
                    point->SetTransient(true);
                }
            }

            spline->GenerateRoadMesh();

            if (Render* render = river->GetComponent<Render>())
            {
                render->SetMaterial(water_material);
                render->SetFlag(RenderFlags::CastsShadows, false);
            }

            river->RemoveComponent<Physics>();
        }
    }

    void Terrain::MakeIslandShore()
    {
        if (!HasHeightfield())
        {
            SP_LOG_WARNING("no heightfield to turn into an island");
            return;
        }

        float sea_world = m_level_sea;
        for (Entity* entity : World::GetEntities())
        {
            if (!entity)
            {
                continue;
            }

            if (Water* water = entity->GetComponent<Water>())
            {
                sea_world = water->GetSeaLevel();
                break;
            }
        }

        // local height at the rim so world y lands at sea level
        float entity_y = 0.0f;
        if (Entity* entity = GetEntity())
        {
            entity_y = entity->GetPosition().y;
        }
        const float edge_local = sea_world - entity_y;

        // shore must cover at least a couple of grid cells or the slope is invisible
        const TerrainGridMapping mapping = GetGridMapping();
        const float min_shore = max(mapping.scale_x, mapping.scale_z) * 2.0f;
        const float shore = max(m_shore_width, min_shore);

        TerrainSystem::ApplyIslandShore(
            m_positions,
            m_height_data.empty() ? nullptr : &m_height_data,
            m_dense_width,
            m_dense_height,
            mapping,
            shore,
            edge_local
        );

        RebuildSurface(true);
        SnapshotBaseline();
    }

    void Terrain::BakeHeightMapPixels()
    {
        if (m_positions.empty() || m_dense_width == 0 || m_dense_height == 0)
        {
            m_height_gpu_bytes.clear();
            m_height_preview_bytes.clear();
            return;
        }

        const uint32_t sample_count = m_dense_width * m_dense_height;

        m_height_gpu_bytes.resize(static_cast<size_t>(sample_count) * sizeof(float));
        float* heights = reinterpret_cast<float*>(m_height_gpu_bytes.data());
        auto copy_world_y = [this, heights](uint32_t start, uint32_t end)
        {
            for (uint32_t i = start; i < end; i++)
            {
                heights[i] = m_positions[i].y;
            }
        };
        ThreadPool::ParallelLoop(copy_world_y, sample_count);

        m_height_bake_min = m_positions[0].y;
        m_height_bake_max = m_positions[0].y;
        float min_x = m_positions[0].x;
        float max_x = m_positions[0].x;
        float min_z = m_positions[0].z;
        float max_z = m_positions[0].z;
        for (const Vector3& position : m_positions)
        {
            m_height_bake_min = min(m_height_bake_min, position.y);
            m_height_bake_max = max(m_height_bake_max, position.y);
            min_x = min(min_x, position.x);
            max_x = max(max_x, position.x);
            min_z = min(min_z, position.z);
            max_z = max(max_z, position.z);
        }

        m_world_mapping = Vector4(
            min_x,
            min_z,
            1.0f / max(max_x - min_x, epsilon),
            1.0f / max(max_z - min_z, epsilon)
        );

        const float height_range = max(m_height_bake_max - m_height_bake_min, epsilon);
        m_height_preview_bytes.resize(static_cast<size_t>(sample_count) * 4);
        uint8_t* pixels = m_height_preview_bytes.data();
        auto copy_heights = [this, pixels, height_range](uint32_t start, uint32_t end)
        {
            for (uint32_t i = start; i < end; i++)
            {
                const float t = saturate((m_positions[i].y - m_height_bake_min) / height_range);
                const uint8_t value = static_cast<uint8_t>(t * 255.0f + 0.5f);
                const uint32_t offset = i * 4;
                pixels[offset + 0] = value;
                pixels[offset + 1] = value;
                pixels[offset + 2] = value;
                pixels[offset + 3] = 255;
            }
        };
        ThreadPool::ParallelLoop(copy_heights, sample_count);
    }

    void Terrain::UploadHeightMapTextures()
    {
        if (m_height_gpu_bytes.empty() || m_dense_width == 0 || m_dense_height == 0)
        {
            return;
        }

        {
            vector<RHI_Texture_Slice> slices = to_single_mip_slice(m_height_gpu_bytes);
            m_height_map_gpu_retired = m_height_map_gpu;
            m_height_map_gpu = make_shared<RHI_Texture>(
                RHI_Texture_Type::Type2D,
                m_dense_width, m_dense_height, 1, 1,
                RHI_Format::R32_Float, RHI_Texture_Srv,
                "terrain_height_gpu", move(slices)
            );
        }

        if (!m_height_preview_bytes.empty())
        {
            vector<RHI_Texture_Slice> slices = to_single_mip_slice(m_height_preview_bytes);
            m_height_map_final_retired = m_height_map_final;
            m_height_map_final = make_shared<RHI_Texture>(
                RHI_Texture_Type::Type2D,
                m_dense_width, m_dense_height, 1, 1,
                RHI_Format::R8G8B8A8_Unorm, RHI_Texture_Srv,
                "terrain_baked", move(slices)
            );
        }

        m_height_gpu_bytes.clear();
        m_height_preview_bytes.clear();
    }

    void Terrain::BakeHeightMapTexture()
    {
        BakeHeightMapPixels();
        UploadHeightMapTextures();
    }

    bool Terrain::LoadTerrainMapsFromCache()
    {
        ifstream file(get_terrain_maps_cache_path(), ios::binary);
        if (!file.is_open())
        {
            return false;
        }

        uint64_t stored_hash = 0;
        uint32_t width       = 0;
        uint32_t height      = 0;
        file.read(reinterpret_cast<char*>(&stored_hash), sizeof(uint64_t));
        file.read(reinterpret_cast<char*>(&width),  sizeof(uint32_t));
        file.read(reinterpret_cast<char*>(&height), sizeof(uint32_t));

        if (!file || stored_hash != ComputeCacheHash() || width == 0 || height == 0 || width > 8192 || height > 8192)
        {
            return false;
        }

        const size_t byte_count = static_cast<size_t>(width) * height * 4;
        m_map_a_pixels.resize(byte_count);
        m_map_b_pixels.resize(byte_count);
        m_prop_mask_pixels.resize(byte_count);
        file.read(reinterpret_cast<char*>(m_map_a_pixels.data()), byte_count);
        file.read(reinterpret_cast<char*>(m_map_b_pixels.data()), byte_count);
        file.read(reinterpret_cast<char*>(m_prop_mask_pixels.data()), byte_count);

        if (!file)
        {
            m_map_a_pixels.clear();
            m_map_b_pixels.clear();
            m_prop_mask_pixels.clear();
            return false;
        }

        m_map_width  = width;
        m_map_height = height;
        return true;
    }

    void Terrain::SaveTerrainMapsToCache() const
    {
        if (m_map_a_pixels.empty() || m_map_b_pixels.empty() || m_prop_mask_pixels.empty())
        {
            return;
        }

        ofstream file(get_terrain_maps_cache_path(), ios::binary);
        if (!file.is_open())
        {
            return;
        }

        const uint64_t hash = ComputeCacheHash();
        file.write(reinterpret_cast<const char*>(&hash),         sizeof(uint64_t));
        file.write(reinterpret_cast<const char*>(&m_map_width),  sizeof(uint32_t));
        file.write(reinterpret_cast<const char*>(&m_map_height), sizeof(uint32_t));
        file.write(reinterpret_cast<const char*>(m_map_a_pixels.data()), m_map_a_pixels.size());
        file.write(reinterpret_cast<const char*>(m_map_b_pixels.data()), m_map_b_pixels.size());
        file.write(reinterpret_cast<const char*>(m_prop_mask_pixels.data()), m_prop_mask_pixels.size());
    }

    void Terrain::BakeTerrainMaps()
    {
        if (m_positions.empty() || m_dense_width < 16 || m_dense_height < 16)
        {
            return;
        }

        // world mapping first, the shader needs it even if the analysis itself comes from cache
        {
            float min_x = m_positions[0].x;
            float max_x = m_positions[0].x;
            float min_z = m_positions[0].z;
            float max_z = m_positions[0].z;
            for (const Vector3& position : m_positions)
            {
                min_x = min(min_x, position.x);
                max_x = max(max_x, position.x);
                min_z = min(min_z, position.z);
                max_z = max(max_z, position.z);
            }

            m_world_mapping = Vector4(
                min_x,
                min_z,
                1.0f / max(max_x - min_x, epsilon),
                1.0f / max(max_z - min_z, epsilon)
            );
        }

        if (!LoadTerrainMapsFromCache())
        {
            TerrainAnalysisMaps analysis;
            TerrainSystem::ComputeAnalysisMaps(
                analysis,
                m_positions,
                m_dense_width,
                m_dense_height,
                m_level_sea,
                m_erosion_maps.IsValid(m_positions.size()) ? &m_erosion_maps : nullptr
            );

            if (!analysis.IsValid())
            {
                return;
            }

            m_map_width  = analysis.width;
            m_map_height = analysis.height;

            const size_t cell_count = static_cast<size_t>(m_map_width) * m_map_height;
            m_map_a_pixels.resize(cell_count * 4);
            m_map_b_pixels.resize(cell_count * 4);

            auto to_byte = [](float value) { return static_cast<uint8_t>(saturate(value) * 255.0f + 0.5f); };

            auto encode = [&](uint32_t start, uint32_t end)
            {
                for (uint32_t i = start; i < end; i++)
                {
                    const size_t offset = static_cast<size_t>(i) * 4;

                    m_map_a_pixels[offset + 0] = to_byte(analysis.curvature[i]);
                    m_map_a_pixels[offset + 1] = to_byte(analysis.flow[i]);
                    m_map_a_pixels[offset + 2] = to_byte(analysis.occlusion[i]);
                    m_map_a_pixels[offset + 3] = to_byte(analysis.deposition[i]);

                    m_map_b_pixels[offset + 0] = to_byte(analysis.wear[i]);
                    m_map_b_pixels[offset + 1] = to_byte(analysis.insolation[i]);
                    m_map_b_pixels[offset + 2] = to_byte(analysis.height_norm[i]);
                    m_map_b_pixels[offset + 3] = to_byte(analysis.talus[i]);
                }
            };
            ThreadPool::ParallelLoop(encode, static_cast<uint32_t>(cell_count));

            BakePropMask();
            SaveTerrainMapsToCache();
        }
        else
        {
            // cached analysis, still rebake the mask so placement tracks the current layer rules
            BakePropMask();
            SaveTerrainMapsToCache();
        }

        if (m_map_a_pixels.empty() || m_map_b_pixels.empty())
        {
            return;
        }
    }

    void Terrain::UploadTerrainMaps()
    {
        if (m_map_a_pixels.empty() || m_map_b_pixels.empty() || m_map_width == 0 || m_map_height == 0)
        {
            return;
        }

        m_map_a_retired = m_map_a;
        m_map_b_retired = m_map_b;
        m_prop_mask_retired = m_prop_mask;

        m_map_a = make_shared<RHI_Texture>(
            RHI_Texture_Type::Type2D,
            m_map_width, m_map_height, 1, 1,
            RHI_Format::R8G8B8A8_Unorm, RHI_Texture_Srv,
            "terrain_analysis_a", to_single_mip_slice(m_map_a_pixels)
        );

        m_map_b = make_shared<RHI_Texture>(
            RHI_Texture_Type::Type2D,
            m_map_width, m_map_height, 1, 1,
            RHI_Format::R8G8B8A8_Unorm, RHI_Texture_Srv,
            "terrain_analysis_b", to_single_mip_slice(m_map_b_pixels)
        );

        if (!m_prop_mask_pixels.empty())
        {
            m_prop_mask = make_shared<RHI_Texture>(
                RHI_Texture_Type::Type2D,
                m_map_width, m_map_height, 1, 1,
                RHI_Format::R8G8B8A8_Unorm, RHI_Texture_Srv,
                "terrain_prop_mask", to_single_mip_slice(m_prop_mask_pixels)
            );
            m_prop_mask->PrepareForGpu();
        }

        m_map_a->PrepareForGpu();
        m_map_b->PrepareForGpu();
    }

    void Terrain::BakePropMask()
    {
        if (m_map_a_pixels.empty() || m_map_b_pixels.empty() || m_positions.empty() ||
            m_map_width == 0 || m_map_height == 0 || m_dense_width < 2 || m_dense_height < 2)
        {
            m_prop_mask_pixels.clear();
            m_layer_dominant.clear();
            return;
        }

        // cpu port of terrain_layer_weight, scores each biome then packs grass/tree/rock channels
        // the constants below mirror common_terrain.hlsl, they must move together or the props end up
        // scattered over ground the surface shader painted as something else
        const float slope_domain_min   = 0.0f;
        const float slope_domain_max   = 1.5698f;
        const float height_domain_min  = -99999.0f;
        const float height_domain_max  = 99999.0f;
        const float slope_feather_max  = 0.35f;
        const float height_feather_max = 6.0f;
        const float blend_width_world  = 14.0f;
        const float gradient_max       = 8.0f;

        auto band = [](float value, float low, float high, float feather_max,
            float domain_min, float domain_max) -> float
        {
            const bool open_low  = low  <= domain_min;
            const bool open_high = high >= domain_max;

            const float span    = (open_low || open_high) ? (feather_max * 2.0f) : (high - low);
            const float feather = max(min(span, feather_max), 1e-4f);

            auto smooth = [](float t) -> float
            {
                return t * t * (3.0f - 2.0f * t);
            };

            const float rise = open_low ?
                1.0f : smooth(saturate((value - (low - feather)) / max(2.0f * feather, 1e-4f)));
            const float fall = open_high ?
                1.0f : smooth(1.0f - saturate((value - (high - feather)) / max(2.0f * feather, 1e-4f)));

            return rise * fall;
        };

        auto layer_weight = [&](const TerrainLayerRule& rule, float height, float slope_rad,
            float curvature, float flow, float occlusion, float deposition,
            float wear, float insolation, float talus) -> float
        {
            if (rule.weight_bias <= 0.0f)
            {
                return 0.0f;
            }

            // snow is driven by accumulation on the gpu, a height and slope stand-in is enough
            // to keep grass and trees off the snow line in the prop mask
            if (rule.flags & TerrainLayerFlags_Snow)
            {
                if (m_snow_amount <= 0.0f)
                {
                    return 0.0f;
                }

                const float altitude = height - m_level_snow;
                float snow = saturate(altitude / 130.0f);
                snow *= powf(saturate(cosf(slope_rad)), 2.5f);
                snow *= lerp(0.55f, 1.15f, curvature);
                snow *= lerp(0.65f, 1.1f, 1.0f - occlusion);
                return snow * m_snow_amount * rule.weight_bias;
            }

            const float height_for_band = (rule.flags & TerrainLayerFlags_BelowSea) ?
                (height - m_level_sea) : height;

            // altitude gained per metre travelled horizontally, a feather written in altitude covers
            // less and less ground as the slope steepens, which is what turns a boundary into a knife
            // edge, scaling by the gradient holds the width along the surface instead
            const float gradient       = min(tanf(min(slope_rad, 1.5f)), gradient_max);
            const float height_feather = max(blend_width_world * gradient, height_feather_max * 0.25f);

            float weight = band(
                slope_rad,
                rule.slope_min * math::deg_to_rad,
                rule.slope_max * math::deg_to_rad,
                slope_feather_max,
                slope_domain_min,
                slope_domain_max
            );
            weight *= band(
                height_for_band,
                rule.height_min,
                rule.height_max,
                height_feather,
                height_domain_min,
                height_domain_max
            );
            if (weight <= 0.0f)
            {
                return 0.0f;
            }

            float push = 0.0f;
            push += rule.curvature_influence  * (curvature * 2.0f - 1.0f);
            push += rule.flow_influence       * (flow * 2.0f - 1.0f);
            push += rule.occlusion_influence  * (1.0f - occlusion * 2.0f);
            push += rule.insolation_influence * (insolation * 2.0f - 1.0f);
            push += rule.wear_influence       * (wear * 2.0f - 1.0f);
            push += rule.deposition_influence * (deposition * 2.0f - 1.0f);
            push += rule.talus_influence      * (talus * 2.0f - 1.0f);

            return weight * exp2f(clamp(push, -1.0f, 1.0f) * 1.25f) * rule.weight_bias;
        };

        // find layer indices by name so a reordered rule table still maps correctly
        int grass_i  = -1;
        int rock_i   = -1;
        int gravel_i = -1;
        int forest_i = -1;
        int sand_i   = -1;
        int dirt_i   = -1;
        int snow_i   = -1;
        int moss_i   = -1;
        for (uint32_t i = 0; i < terrain_layer_max; i++)
        {
            if (m_layer_rules[i].name == "whispy_grass_meadow") grass_i = static_cast<int>(i);
            if (m_layer_rules[i].name == "rock") rock_i = static_cast<int>(i);
            if (m_layer_rules[i].name == "gravel") gravel_i = static_cast<int>(i);
            if (m_layer_rules[i].name == "forest_floor") forest_i = static_cast<int>(i);
            if (m_layer_rules[i].name == "sand") sand_i = static_cast<int>(i);
            if (m_layer_rules[i].name == "dirt") dirt_i = static_cast<int>(i);
            if (m_layer_rules[i].name == "snow") snow_i = static_cast<int>(i);
            if (m_layer_rules[i].name == "moss") moss_i = static_cast<int>(i);
        }

        const size_t cell_count = static_cast<size_t>(m_map_width) * m_map_height;
        m_prop_mask_pixels.resize(cell_count * 4);
        m_layer_dominant.resize(cell_count);

        const uint32_t dense_w = m_dense_width;
        const uint32_t dense_h = m_dense_height;
        float cell_x = 1.0f;
        float cell_z = 1.0f;
        if (m_positions.size() > 1)
        {
            cell_x = max(fabsf(m_positions[1].x - m_positions[0].x), 1e-3f);
        }
        if (m_positions.size() > dense_w)
        {
            cell_z = max(fabsf(m_positions[dense_w].z - m_positions[0].z), 1e-3f);
        }

        auto bake = [&](uint32_t start, uint32_t end)
        {
            for (uint32_t i = start; i < end; i++)
            {
                const uint32_t ax = i % m_map_width;
                const uint32_t az = i / m_map_width;

                const uint32_t dx = min(
                    static_cast<uint32_t>((static_cast<float>(ax) + 0.5f) / static_cast<float>(m_map_width) * dense_w),
                    dense_w - 1u
                );
                const uint32_t dz = min(
                    static_cast<uint32_t>((static_cast<float>(az) + 0.5f) / static_cast<float>(m_map_height) * dense_h),
                    dense_h - 1u
                );

                const Vector3& pos = m_positions[static_cast<size_t>(dz) * dense_w + dx];
                const uint32_t dx1 = min(dx + 1u, dense_w - 1u);
                const uint32_t dz1 = min(dz + 1u, dense_h - 1u);
                const float y_c = pos.y;
                const float y_r = m_positions[static_cast<size_t>(dz) * dense_w + dx1].y;
                const float y_u = m_positions[static_cast<size_t>(dz1) * dense_w + dx].y;
                Vector3 normal = Vector3(-(y_r - y_c) / cell_x, 1.0f, -(y_u - y_c) / cell_z).Normalized();
                const float slope = acosf(clamp(normal.y, -1.0f, 1.0f));

                const size_t offset = static_cast<size_t>(i) * 4;
                const float curvature   = m_map_a_pixels[offset + 0] * (1.0f / 255.0f);
                const float flow        = m_map_a_pixels[offset + 1] * (1.0f / 255.0f);
                const float occlusion   = m_map_a_pixels[offset + 2] * (1.0f / 255.0f);
                const float deposition  = m_map_a_pixels[offset + 3] * (1.0f / 255.0f);
                const float wear        = m_map_b_pixels[offset + 0] * (1.0f / 255.0f);
                const float insolation  = m_map_b_pixels[offset + 1] * (1.0f / 255.0f);
                const float talus       = m_map_b_pixels[offset + 3] * (1.0f / 255.0f);

                auto score = [&](int layer_index) -> float
                {
                    if (layer_index < 0)
                    {
                        return 0.0f;
                    }
                    if (!IsLayerEnabled(static_cast<uint32_t>(layer_index)))
                    {
                        return 0.0f;
                    }
                    return layer_weight(
                        m_layer_rules[static_cast<size_t>(layer_index)],
                        y_c, slope, curvature, flow, occlusion, deposition, wear, insolation, talus
                    );
                };

                // same pick as the surface shader, sand dirt and snow keep their share so grass
                // cannot inherit ground it does not own
                float scores[terrain_layer_max];
                for (uint32_t layer = 0; layer < terrain_layer_max; layer++)
                {
                    scores[layer] = score(static_cast<int>(layer));
                }

                const uint32_t keep = clamp(m_layer_quality, 1u, 4u);
                float picked[terrain_layer_max] = {};
                uint32_t dominant_layer = 0;
                for (uint32_t slot = 0; slot < keep; slot++)
                {
                    float best_score = 0.0f;
                    int best_layer   = -1;
                    for (uint32_t layer = 0; layer < terrain_layer_max; layer++)
                    {
                        if (scores[layer] > best_score)
                        {
                            best_score = scores[layer];
                            best_layer = static_cast<int>(layer);
                        }
                    }
                    if (best_layer < 0)
                    {
                        break;
                    }
                    if (slot == 0)
                    {
                        dominant_layer = static_cast<uint32_t>(best_layer);
                    }
                    scores[best_layer] = 0.0f;
                    picked[best_layer] = best_score;
                }

                // the rank cut is a step, subtracting the highest rejected score pins a layer to zero
                // as it enters and leaves the set, same as terrain_pick_layers, without it the prop
                // mask steps where the surface fades and grass stops on a hard line
                float rejected = 0.0f;
                for (uint32_t layer = 0; layer < terrain_layer_max; layer++)
                {
                    rejected = max(rejected, scores[layer]); // the winners were zeroed as they were consumed
                }

                float total = 0.0f;
                for (uint32_t layer = 0; layer < terrain_layer_max; layer++)
                {
                    picked[layer] = max(picked[layer] - rejected, 0.0f);
                    total += picked[layer];
                }

                float grass = 0.0f;
                float trees = 0.0f;
                float rock  = 0.0f;
                if (total > 1e-6f)
                {
                    const float inv = 1.0f / total;
                    auto share = [&](int layer_index) -> float
                    {
                        return (layer_index >= 0) ? picked[layer_index] * inv : 0.0f;
                    };

                    const float slope_deg  = slope * math::rad_to_deg;
                    const float above_sea  = y_c - m_level_sea;
                    const float below_snow = m_level_snow - y_c;
                    const float grass_w    = share(grass_i);
                    const float forest_w   = share(forest_i);
                    const float moss_w     = share(moss_i);
                    const float rock_w     = share(rock_i);
                    const float gravel_w   = share(gravel_i);
                    const float dirt_w     = share(dirt_i);
                    const float sand_w     = share(sand_i);
                    const float snow_w     = share(snow_i);

                    // the surface pick is the biome, props follow the same shares the shader painted
                    const float living  = grass_w + forest_w * 0.9f + moss_w * 0.45f;
                    const float mineral = rock_w + gravel_w * 0.9f + dirt_w * 0.55f;
                    const float barren  = sand_w + snow_w;

                    const float shade = 1.0f - insolation;
                    const float altitude = saturate(
                        (y_c - m_level_sea) / max(m_level_snow - m_level_sea, 1.0f)
                    );
                    const float alpine    = saturate((altitude - 0.55f) / 0.45f);
                    const float tree_line = saturate(1.0f - alpine * 1.35f);
                    const float slope_soft = saturate(1.0f - slope_deg / 32.0f);

                    // grass only where living ground beats rock, dirt, sand and snow
                    float grass_raw = living - mineral * 1.2f - barren * 1.5f;
                    grass_raw *= slope_soft;
                    if (above_sea < 1.0f || below_snow < 2.0f)
                    {
                        grass_raw = 0.0f;
                    }
                    grass_raw = saturate(grass_raw);
                    // lift the floor so mixed seams stay empty and meadow cores stay full
                    grass = saturate((grass_raw - 0.18f) / 0.82f);
                    grass = grass * grass * (3.0f - 2.0f * grass);

                    // trees fill living ground, forest is dense, meadows get groves
                    float tree_raw = forest_w
                        + grass_w * lerp(0.5f, 0.95f, saturate(shade * 1.15f + deposition * 0.35f))
                        + moss_w * 0.25f;
                    tree_raw *= slope_soft;
                    tree_raw *= tree_line;
                    tree_raw *= saturate(1.0f - mineral * 1.35f);
                    tree_raw *= saturate(1.0f - barren * 2.0f);
                    if (above_sea < 2.0f || below_snow < 6.0f)
                    {
                        tree_raw = 0.0f;
                    }
                    trees = saturate(tree_raw);

                    // rocks on almost all dry ground, denser on mineral and slope
                    float rock_raw = 0.55f + mineral * 0.45f;
                    rock_raw += saturate((slope_deg - 3.0f) / 28.0f) * 0.4f;
                    rock_raw *= saturate(1.0f - living * 0.12f);
                    rock_raw *= saturate(1.0f - sand_w * 0.25f);
                    rock_raw *= saturate(1.0f - snow_w * 0.55f);
                    if (above_sea < -0.5f)
                    {
                        rock_raw = 0.0f;
                    }
                    rock = saturate(rock_raw);
                }

                m_prop_mask_pixels[offset + 0] = static_cast<uint8_t>(grass * 255.0f + 0.5f);
                m_prop_mask_pixels[offset + 1] = static_cast<uint8_t>(trees * 255.0f + 0.5f);
                m_prop_mask_pixels[offset + 2] = static_cast<uint8_t>(rock * 255.0f + 0.5f);
                m_prop_mask_pixels[offset + 3] = 255;
                // which surface layer won this point, it stays off the texture so the preview and the
                // grass populate pass keep reading an opaque rgb mask
                m_layer_dominant[i] = static_cast<uint8_t>(dominant_layer);
            }
        };
        ThreadPool::ParallelLoop(bake, static_cast<uint32_t>(cell_count));

        uint32_t grass_hits = 0;
        uint32_t tree_hits  = 0;
        uint32_t rock_hits  = 0;
        for (size_t i = 0; i < cell_count; i++)
        {
            const size_t offset = i * 4;
            if (m_prop_mask_pixels[offset + 0] > 40)
            {
                grass_hits++;
            }
            if (m_prop_mask_pixels[offset + 1] > 20)
            {
                tree_hits++;
            }
            if (m_prop_mask_pixels[offset + 2] > 20)
            {
                rock_hits++;
            }
        }
        const float inv_cells = 100.0f / max(static_cast<float>(cell_count), 1.0f);
        SP_LOG_INFO(
            "prop mask: grass %.1f%%, trees %.1f%%, rocks %.1f%%",
            static_cast<float>(grass_hits) * inv_cells,
            static_cast<float>(tree_hits) * inv_cells,
            static_cast<float>(rock_hits) * inv_cells
        );
    }

    void Terrain::RebuildPropMask()
    {
        BakePropMask();
        if (m_prop_mask_pixels.empty() || m_map_width == 0 || m_map_height == 0)
        {
            m_prop_mask_retired = m_prop_mask;
            m_prop_mask.reset();
            return;
        }

        m_prop_mask_retired = m_prop_mask;
        m_prop_mask = make_shared<RHI_Texture>(
            RHI_Texture_Type::Type2D,
            m_map_width, m_map_height, 1, 1,
            RHI_Format::R8G8B8A8_Unorm, RHI_Texture_Srv,
            "terrain_prop_mask", to_single_mip_slice(m_prop_mask_pixels)
        );
        m_prop_mask->PrepareForGpu();
    }

    Vector3 Terrain::SamplePropMask(float world_x, float world_z) const
    {
        if (m_prop_mask_pixels.empty() || m_map_width == 0 || m_map_height == 0)
        {
            return Vector3::Zero;
        }

        float u = (world_x - m_world_mapping.x) * m_world_mapping.z;
        float v = (world_z - m_world_mapping.y) * m_world_mapping.w;
        u = clamp(u, 0.0f, 1.0f);
        v = clamp(v, 0.0f, 1.0f);

        const float fx = u * static_cast<float>(m_map_width - 1);
        const float fz = v * static_cast<float>(m_map_height - 1);
        const uint32_t x0 = static_cast<uint32_t>(fx);
        const uint32_t z0 = static_cast<uint32_t>(fz);
        const uint32_t x1 = min(x0 + 1u, m_map_width - 1u);
        const uint32_t z1 = min(z0 + 1u, m_map_height - 1u);
        const float tx = fx - static_cast<float>(x0);
        const float tz = fz - static_cast<float>(z0);

        auto fetch = [&](uint32_t x, uint32_t z) -> Vector3
        {
            const size_t offset = (static_cast<size_t>(z) * m_map_width + x) * 4;
            return Vector3(
                m_prop_mask_pixels[offset + 0] * (1.0f / 255.0f),
                m_prop_mask_pixels[offset + 1] * (1.0f / 255.0f),
                m_prop_mask_pixels[offset + 2] * (1.0f / 255.0f)
            );
        };

        const Vector3 c00 = fetch(x0, z0);
        const Vector3 c10 = fetch(x1, z0);
        const Vector3 c01 = fetch(x0, z1);
        const Vector3 c11 = fetch(x1, z1);
        const Vector3 c0 = c00 * (1.0f - tx) + c10 * tx;
        const Vector3 c1 = c01 * (1.0f - tx) + c11 * tx;
        return c0 * (1.0f - tz) + c1 * tz;
    }

    float Terrain::SamplePropMaskChannel(float world_x, float world_z, int channel) const
    {
        const Vector3 mask = SamplePropMask(world_x, world_z);
        if (channel == 0) return mask.x;
        if (channel == 1) return mask.y;
        if (channel == 2) return mask.z;
        return 1.0f;
    }

    void Terrain::DetachTileMeshes()
    {
        if (!m_entity_ptr)
        {
            return;
        }

        // removeentity is deferred to world tick, clear draws now so the renderer
        // cannot touch a mesh that generate is about to free
        for (Entity* child : m_entity_ptr->GetChildren())
        {
            if (!child)
            {
                continue;
            }

            if (child->GetObjectName().rfind("tile_", 0) != 0)
            {
                continue;
            }

            if (Render* render = child->GetComponent<Render>())
            {
                render->ClearMesh();
            }
        }
    }

    void Terrain::ClearTileEntities()
    {
        if (!m_entity_ptr)
        {
            return;
        }

        DetachTileMeshes();

        vector<Entity*> children = m_entity_ptr->GetChildren();
        for (Entity* child : children)
        {
            if (!child)
            {
                continue;
            }

            string name = child->GetObjectName();
            if (name.rfind("tile_", 0) == 0)
            {
                World::RemoveEntity(child);
            }
        }
    }

    void Terrain::RefreshPhysics()
    {
        if (!m_entity_ptr)
        {
            return;
        }

        // the surface itself never carries a body, the tiles do
        m_entity_ptr->RemoveComponent<Physics>();

        if (!HasHeightfield())
        {
            return;
        }

        // one static grid per tile, physx samples the heights directly so there is nothing to cook and
        // the collision matches the rendered mesh exactly, cooked per tile meshes did neither, keeping
        // it per tile is what lets the distance activation drop everything but the tiles around you
        for (Entity* child : m_entity_ptr->GetChildren())
        {
            if (ParseTileIndex(child) < 0)
            {
                continue;
            }

            Physics* physics = child->GetComponent<Physics>();
            if (!physics)
            {
                physics = child->AddComponent<Physics>();
            }

            physics->SetStatic(true);
            if (physics->GetBodyType() == BodyType::Heightfield)
            {
                // the type setter is a no op when the type already matches, so the stale grid needs forcing out
                physics->Rebuild();
            }
            else
            {
                physics->SetBodyType(BodyType::Heightfield);
            }
        }
    }

    void Terrain::CreateTileEntities()
    {
        ClearTileEntities();

        if (!m_mesh)
        {
            return;
        }

        const uint32_t tile_count_entities = static_cast<uint32_t>(m_tile_offsets.size());
        for (uint32_t tile_index = 0; tile_index < tile_count_entities; tile_index++)
        {
            Entity* entity = World::CreateEntity();
            entity->SetObjectName("tile_" + to_string(tile_index + 1));
            entity->SetTransient(true);
            entity->SetParent(GetEntity());
            // offsets are terrain local, SetPosition would fight the parent world y
            entity->SetPositionLocal(m_tile_offsets[tile_index]);

            if (Render* render = entity->AddComponent<Render>())
            {
                render->SetMesh(m_mesh.get(), tile_index);
                render->SetMaterial(m_material);
            }
        }
    }

    void Terrain::RebuildSurface(bool update_placement)
    {
        if (!HasHeightfield())
        {
            SP_LOG_WARNING("no heightfield to rebuild");
            return;
        }

        m_vertices.resize(m_dense_width * m_dense_height);
        m_indices.resize((m_dense_width - 1) * (m_dense_height - 1) * 6);
        TerrainSystem::GenerateVerticesAndIndices(m_vertices, m_indices, m_positions, m_dense_width, m_dense_height);
        TerrainSystem::GenerateNormals(m_vertices, m_dense_width, m_dense_height);

        uint32_t tile_count = max(m_tile_count, 1u);
        geometry_processing::split_surface_into_tiles(
            m_vertices,
            m_indices,
            tile_count,
            m_tile_vertices,
            m_tile_indices,
            m_tile_offsets
        );

        if (update_placement)
        {
            m_triangle_data.clear();
            for (uint32_t tile_index = 0; tile_index < m_tile_vertices.size(); tile_index++)
            {
                placement::compute_triangle_data(m_tile_vertices, m_tile_indices, tile_index, m_triangle_data);
            }
        }

        BakeHeightMapTexture();
        BakeTerrainMaps();
        UploadTerrainMaps();

        m_height_samples = m_dense_width * m_dense_height;
        m_vertex_count   = static_cast<uint32_t>(m_vertices.size());
        m_index_count    = static_cast<uint32_t>(m_indices.size());
        m_triangle_count = m_index_count / 3;
        m_area_km2       = TerrainSystem::ComputeSurfaceAreaKm2(m_vertices, m_indices);

        // drop tile draws before freeing the mesh they still reference
        DetachTileMeshes();
        ClearTileEntities();

        ResourceCache::Remove(m_mesh);
        m_mesh = make_shared<Mesh>();
        m_mesh->SetObjectName("terrain_mesh");
        m_mesh->SetFlag(static_cast<uint32_t>(MeshFlags::PostProcessOptimize), false);
        m_mesh->SetFlag(static_cast<uint32_t>(MeshFlags::PostProcessPreserveTerrainEdges), true);

        for (uint32_t tile_index = 0; tile_index < static_cast<uint32_t>(m_tile_vertices.size()); tile_index++)
        {
            uint32_t sub_mesh_index = 0;
            m_mesh->AddGeometry(m_tile_vertices[tile_index], m_tile_indices[tile_index], false, &sub_mesh_index);
        }
        m_mesh->CreateGpuBuffers();

        CreateTileEntities();
        RefreshPhysics();
        RefreshLayers();
        PushToRenderer();
        SpawnFlowRivers();

        m_vertices.clear();
        m_indices.clear();
        m_tile_vertices.clear();
        m_tile_indices.clear();
    }

    void Terrain::CreateFlat(uint32_t base_width, uint32_t base_height)
    {
        bool expected = false;
        if (!m_is_generating.compare_exchange_strong(expected, true))
        {
            SP_LOG_WARNING("terrain generation already in progress");
            return;
        }

        Clear();

        TerrainSystem::CreateFlatHeightfield(
            m_height_data,
            m_positions,
            base_width,
            base_height,
            m_density,
            m_scale,
            m_level_sea
        );

        m_width        = base_width;
        m_height       = base_height;
        m_dense_width  = m_density * (base_width - 1) + 1;
        m_dense_height = m_density * (base_height - 1) + 1;

        RebuildSurface(true);
        SnapshotBaseline();

        WorldHelpers::PopulateTerrainBiomeProps(this);

        m_is_generating = false;
    }

    void Terrain::SnapshotBaseline()
    {
        m_positions_baseline = m_positions;
    }

    void Terrain::SetTileCountAxis(uint32_t count)
    {
        m_tile_count = max(count, 1u);
    }

    int Terrain::ParseTileIndex(Entity* entity)
    {
        if (!entity)
        {
            return -1;
        }

        const string& name = entity->GetObjectName();
        if (name.rfind("tile_", 0) != 0)
        {
            return -1;
        }

        try
        {
            const int one_based = stoi(name.substr(5));
            return one_based > 0 ? one_based - 1 : -1;
        }
        catch (...)
        {
            return -1;
        }
    }

    void Terrain::Regenerate()
    {
        if (m_height_map_seed)
        {
            FileSystem::Delete(get_terrain_cache_bin_path());
            FileSystem::Delete(get_terrain_mesh_cache_path());
            FileSystem::Delete(get_terrain_maps_cache_path());
            Generate();
            return;
        }

        if (m_width > 1 && m_height > 1)
        {
            CreateFlat(m_width, m_height);
            return;
        }

        SP_LOG_WARNING("nothing to regenerate, assign a height map or create a flat terrain first");
    }

    bool Terrain::RegenerateTile(uint32_t tile_index)
    {
        if (!HasHeightfield() || m_positions_baseline.empty())
        {
            SP_LOG_WARNING("no baseline heights, regenerate the full terrain once first");
            return false;
        }

        if (m_positions_baseline.size() != m_positions.size())
        {
            SP_LOG_WARNING("baseline size mismatch, regenerate the full terrain");
            return false;
        }

        const uint32_t n = max(m_tile_count, 1u);
        if (tile_index >= n * n)
        {
            SP_LOG_WARNING("tile index %u out of range for %ux%u grid", tile_index, n, n);
            return false;
        }

        const TerrainGridMapping mapping = GetGridMapping();
        const float tile_w = mapping.extent_x / static_cast<float>(n);
        const float tile_d = mapping.extent_z / static_cast<float>(n);
        const uint32_t tx  = tile_index % n;
        const uint32_t tz  = tile_index / n;
        const float x0     = -mapping.offset_x + static_cast<float>(tx) * tile_w;
        const float x1     = x0 + tile_w;
        const float z0     = -mapping.offset_z + static_cast<float>(tz) * tile_d;
        const float z1     = z0 + tile_d;
        const float eps    = 0.001f;

        auto restore = [this, x0, x1, z0, z1, eps](uint32_t start, uint32_t end)
        {
            for (uint32_t i = start; i < end; i++)
            {
                const Vector3& p = m_positions[i];
                if (p.x < x0 - eps || p.x > x1 + eps || p.z < z0 - eps || p.z > z1 + eps)
                {
                    continue;
                }

                m_positions[i].y = m_positions_baseline[i].y;
            }
        };
        ThreadPool::ParallelLoop(restore, static_cast<uint32_t>(m_positions.size()));

        if (!m_height_data.empty() && m_height_data.size() == m_positions.size())
        {
            TerrainSystem::SyncHeightDataFromPositions(m_height_data, m_positions);
        }

        RebuildSurface(true);
        return true;
    }

    void Terrain::Clear()
    {
        // detach and queue tile removal before freeing geometry they pointed at
        ClearTileEntities();

        m_vertices.clear();
        m_indices.clear();
        m_tile_vertices.clear();
        m_tile_indices.clear();
        m_triangle_data.clear();
        m_positions_baseline.clear();
        ResourceCache::Remove(m_mesh);
        m_mesh = nullptr;
    }
}
