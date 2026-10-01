/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

//= INCLUDES =================================
#include "pch.h"
#include <filesystem>
#include <cctype>
#include "../../profiling/Profiler.h"
#include <algorithm>
#include <array>
#include <cctype>
#include <fstream>
#include <functional>
#include <unordered_set>
#include "Terrain.h"
#include "TerrainCommon.h"
#include "../../rhi/RHI_Buffer.h"
#include "../../rhi/RHI_CommandList.h"
#include "../TerrainHabitat.h"
#include "Render.h"
#include "Physics.h"
#include "Water.h"
#include "Spline.h"
#include "../../geometry/GeneratedCache.h"
#include "RoadEditRegions.h"
#include "Light.h"
#include "Camera.h"
#include "../Entity.h"
#include "../World.h"
#include "../TerrainSystem.h"
#include "../TerrainPlacement.h"
#include "../../rhi/RHI_Texture.h"
#include "../../resource/ResourceCache.h"
#include "../../geometry/Mesh.h"
#include "../../rendering/Material.h"
#include "../../rendering/Color.h"
#include "../../rendering/Renderer.h"
#include "../../geometry/GeometryProcessing.h"
#include "../../core/ThreadPool.h"
#include "../../core/ProgressTracker.h"
#include "../../core/Engine.h"
#include "../../core/Timer.h"
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
    using namespace terrain_common;

    namespace
    {
        // marks the calling thread as the one allowed to touch the heightfield while it is rebuilt
        struct worker_scope
        {
            atomic<uint32_t>& busy;
            worker_scope(atomic<uint32_t>& busy_flag, thread::id& worker_id) : busy(busy_flag)
            {
                worker_id = this_thread::get_id();
                busy.fetch_add(1, memory_order_acq_rel);
            }
            ~worker_scope()
            {
                busy.fetch_sub(1, memory_order_acq_rel);
            }
        };

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

        // world data, not a cache, World::SaveToFile writes it next to the other world resources
        constexpr const char* terrain_sculpt_file_name = "terrain_sculpt.bin";

        string get_terrain_sculpt_path()
        {
            return get_terrain_cache_directory() + terrain_sculpt_file_name;
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
                    float slope_radians           = acos(clamp(Vector3::Dot(normal, Vector3::Up), -1.0f, 1.0f));
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
            function<bool(float, float, terrain_placement::Surface&)> sample_ground;
            const BoundingBox* mesh_bounds = nullptr;
        };

        void find_transforms(
            const TerrainScatterLayer& layer,
            const ScatterContext& ctx,
            uint32_t tile_index,
            vector<Matrix>& transforms_out,
            const unordered_map<uint64_t, vector<TriangleData>>& triangle_data,
            float* coverage_out = nullptr
        )
        {
            transforms_out.clear();
            if (coverage_out)
                *coverage_out = 0.0f;
            auto it = triangle_data.find(tile_index);
            if (it == triangle_data.end())
            {
                SP_LOG_ERROR("no triangle data found for tile %d", tile_index);
                return;
            }
            const vector<TriangleData>& tile_triangle_data = it->second;
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
                        for (const Vector3& vertex : {tri.v0, tri.v0 + tri.v1_minus_v0, tri.v0 + tri.v2_minus_v0})
                        {
                            b.min_x = min(b.min_x, vertex.x);
                            b.max_x = max(b.max_x, vertex.x);
                            b.min_z = min(b.min_z, vertex.z);
                            b.max_z = max(b.max_z, vertex.z);
                        }
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

            const float slope_min_rad = layer.slope_min * math::deg_to_rad;
            const float slope_max_rad = layer.slope_max * math::deg_to_rad;
            const float slope_range   = max(slope_max_rad - slope_min_rad, 1e-3f);
            const bool reads_surface  = (layer.habitat >= 1 && layer.habitat <= 3) || layer.ground_mask != 0 ||
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
            auto weigh_point = [&](const Vector3& position, float slope, bool footprint = false) -> float
            {
                const float height = position.y - ctx.sea_local;
                if (!footprint && (slope < slope_min_rad ||
                    slope > slope_max_rad ||
                    height < layer.height_min ||
                    height > layer.height_max))
                {
                    return 0.0f;
                }

                float weight = 1.0f;

                if (!footprint && layer.habitat == 4)
                    weight *= terrain_habitat::weight(layer.habitat,
                        position.x + ctx.tile_offset.x, position.z + ctx.tile_offset.z);

                if (!footprint && layer.height_fade > 0.0f)
                {
                    weight *= saturate((height - layer.height_min) / layer.height_fade);
                }

                if (!footprint && layer.slope_bias != 0.0f)
                {
                    const float t = saturate((slope - slope_min_rad) / slope_range);
                    weight *= layer.slope_bias > 0.0f ?
                        powf(t, layer.slope_bias) :
                        powf(1.0f - t, -layer.slope_bias);
                }

                if (reads_surface && ctx.sample_surface)
                {
                    TerrainSurfaceSample sample;
                    if (!ctx.sample_surface(
                        position.x + ctx.tile_offset.x,
                        position.z + ctx.tile_offset.z,
                        sample
                    ))
                        return 0.0f;

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

                    // Flatter ground at the foot of a cliff is valid support. Keep actual
                    // exclusions, but slope and analysis preferences only choose the anchor.
                    if (footprint)
                        return weight;

                    // same push the surface rules use, so an influence reads the same on both sides
                    if (layer.habitat == 1) weight *= sample.woodland;
                    if (layer.habitat == 2) weight *= sample.grove;
                    if (layer.habitat == 3) weight *= sample.scrub;

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

            if (layer.mountain_rocks)
            {
                if (!ctx.mesh_bounds || !ctx.sample_ground)
                    return;
                auto sample = [&](float x, float z, terrain_placement::Surface& ground) -> bool
                {
                    if (!ctx.sample_ground(x, z, ground))
                        return false;
                    const Vector3 local = Vector3(x, ground.height, z) - ctx.tile_offset;
                    ground.weight = weigh_point(local, acosf(saturate(ground.normal.y)));
                    ground.allowed = weigh_point(local, 0.0f, true) > 0.0f;
                    return true;
                };
                terrain_placement::mountain_formations(layer, *ctx.mesh_bounds,
                    tile_min_x + ctx.tile_offset.x, tile_min_z + ctx.tile_offset.z,
                    tile_max_x + ctx.tile_offset.x, tile_max_z + ctx.tile_offset.z,
                    ctx.sea_local + ctx.tile_offset.y, ctx.tile_offset, sample, transforms_out);
                if (coverage_out)
                {
                    float accepted = 0.0f;
                    for (const TriangleData& tri : tile_triangle_data)
                        accepted += weigh_point(tri.centroid, tri.slope_radians);
                    *coverage_out = accepted / max(static_cast<float>(tile_triangle_data.size()), 1.0f);
                }
                return;
            }

            vector<uint32_t> acceptable_triangles;
            vector<float> acceptable_weights;
            acceptable_triangles.reserve(tile_triangle_data.size());
            acceptable_weights.reserve(tile_triangle_data.size());
            float weight_sum = 0.0f;
            for (uint32_t i = 0; i < tile_triangle_data.size(); i++)
            {
                const TriangleData& tri = tile_triangle_data[i];

                // centroid only, a triangle that merely clips the snow line must still be able to
                // hold trees on the side that is actually below it
                const float weight = weigh_point(tri.centroid, tri.slope_radians);
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
                uniform_real_distribution<float> dist(0.0f, 1.0f);
                const uint32_t max_attempts = 50;

                for (uint32_t i = start_index; i < end_index; i++)
                {
                    // seeded per cluster, not per work chunk, so the layout does not follow the thread count
                    mt19937 generator(tile_index * 1000003u + i * 31u + layer.seed * 7919u + 12345u);
                    Vector3 position;
                    uint32_t tri_idx;
                    uint32_t attempts = 0;

                    do
                    {
                        tri_idx           = acceptable_triangles[pick_weighted_local(generator)];
                        const TriangleData& tri = tile_triangle_data[tri_idx];

                        float r1      = dist(generator);
                        float r2      = dist(generator);
                        float sqrt_r1 = sqrtf(r1);
                        float u       = 1.0f - sqrt_r1;
                        float v       = r2 * sqrt_r1;
                        position      = tri.v0 + u * tri.v1_minus_v0 + v * tri.v2_minus_v0 + tri.normal * layer.surface_offset;
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
                    const TriangleData& tri = tile_triangle_data[tri_idx];
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
                                const TriangleData& tri = tile_triangle_data[tri_idx];
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

            // One byte per slot, avoiding vector<bool>'s shared-word writes from parallel jobs.
            vector<uint8_t> placed(adjusted_count, 0);
            // place instances within clusters
            auto place_mesh = [&](uint32_t start_index, uint32_t end_index)
            {
                uniform_real_distribution<float> dist(0.0f, 1.0f);
                uniform_real_distribution<float> angle_dist(0.0f, 360.0f);
                uint32_t larger_cluster_size = base_instances_per_cluster + 1;

                for (uint32_t i = start_index; i < end_index; i++)
                {
                    mt19937 generator(tile_index * 2000003u + i * 37u + layer.seed * 104729u + 67890u);

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
                    bool accepted = false;
                    uint32_t tri_idx = nearby[nearby_dist(generator)];
                    const uint32_t max_point_attempts = 8;
                    for (uint32_t attempt = 0; attempt < max_point_attempts; attempt++)
                    {
                        tri_idx = nearby[nearby_dist(generator)];
                        const TriangleData& candidate = tile_triangle_data[tri_idx];

                        float r1      = dist(generator);
                        float r2      = dist(generator);
                        float sqrt_r1 = sqrtf(r1);
                        float u       = 1.0f - sqrt_r1;
                        float v       = r2 * sqrt_r1;
                        // along the face normal, the same axis the sink uses, so a lift on a slope stays a lift
                        position = candidate.v0 + u * candidate.v1_minus_v0 + v * candidate.v2_minus_v0
                            + candidate.normal * layer.surface_offset;

                        // Test the actual unoffset point, including altitude and ground type.
                        const Vector3 ground = position - candidate.normal * layer.surface_offset;
                        const float weight = weigh_point(ground, candidate.slope_radians);
                        if (weight > 0.0f && dist(generator) < weight)
                        {
                            accepted = true;
                            break;
                        }
                    }
                    if (!accepted)
                        continue;
                    const TriangleData& tri = tile_triangle_data[tri_idx];

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
                    placed[i] = 1;
                }
            };
            ThreadPool::ParallelLoop(place_mesh, adjusted_count);
            size_t kept = 0;
            for (size_t i = 0; i < transforms_out.size(); ++i)
                if (placed[i])
                    transforms_out[kept++] = transforms_out[i];
            transforms_out.resize(kept);
        }
    }

    Terrain::Terrain(Entity* entity) : Component(entity)
    {
        SP_REGISTER_ATTRIBUTE_VALUE_SET(m_min_y, SetMinY, float);
        SP_REGISTER_ATTRIBUTE_VALUE_SET(m_max_y, SetMaxY, float);
        SP_REGISTER_ATTRIBUTE_VALUE_VALUE(m_level_sea, float);
        SP_REGISTER_ATTRIBUTE_VALUE_VALUE(m_level_snow, float);
        SP_REGISTER_ATTRIBUTE_VALUE_VALUE(m_smoothing, uint32_t);
        SP_REGISTER_ATTRIBUTE_VALUE_SET(m_density, SetDensity, uint32_t);
        SP_REGISTER_ATTRIBUTE_VALUE_SET(m_scale, SetScale, uint32_t);
        SP_REGISTER_ATTRIBUTE_VALUE_SET(m_create_border, SetCreateBorder, bool);
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
        // a regenerate on the thread pool holds a raw pointer to this, tearing the arrays down under
        // it is a crash, wait it out
        while (m_worker_busy.load(memory_order_acquire) > 0)
        {
            this_thread::sleep_for(chrono::milliseconds(1));
        }

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
        // Texture decoding and packing are independent between folders. Keep
        // duplicate folder rules together so shared texture bytes have one writer.
        vector<vector<uint32_t>> groups;
        unordered_map<string, size_t> group_by_folder;
        for (uint32_t i = 0; i < terrain_layer_max; ++i)
        {
            string folder = filesystem::path("project/materials/" + m_layer_rules[i].name).lexically_normal().generic_string();
            transform(folder.begin(), folder.end(), folder.begin(), [](unsigned char c) { return static_cast<char>(tolower(c)); });
            auto [it, inserted] = group_by_folder.emplace(folder, groups.size());
            if (inserted)
            {
                groups.emplace_back();
            }
            groups[it->second].push_back(i);
        }
        ThreadPool::ParallelLoop([this, &groups](uint32_t start, uint32_t end)
        {
            for (uint32_t group = start; group < end; ++group)
            {
                for (uint32_t i : groups[group])
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
                    layer->SetPersistent(false);
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
            }

        }, static_cast<uint32_t>(groups.size()));

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
        // the shader compares world y, so this is the world sea, the same one the cpu bakes with
        params.sea_level     = ResolveSeaLevelWorld();
        params.snow_level    = m_level_snow;
        params.snow_amount   = m_snow_amount;
        params.wetness       = m_wetness;
        params.blend_height  = m_blend_height;
        params.quality       = m_layer_quality;
        params.debug_view    = static_cast<uint32_t>(m_debug_view);

        for (uint32_t i = 0; i < terrain_layer_max; i++)
        {
            params.layer_materials[i] = m_layer_materials[i].get();
            params.layer_rules[i]     = m_layer_rules[i];
        }

        Renderer::SetTerrain(params);
    }

    void Terrain::SaveScatterLayer(pugi::xml_node& layer_node, const TerrainScatterLayer& layer)
    {
        layer_node.append_attribute("name")                = layer.name.c_str();
        layer_node.append_attribute("mesh_path")            = layer.mesh_path.c_str();
        layer_node.append_attribute("mesh_variants")        = layer.mesh_variants.c_str();
        layer_node.append_attribute("habitat")              = layer.habitat;
        layer_node.append_attribute("material_folder")      = layer.material_folder.c_str();
        layer_node.append_attribute("enabled")              = layer.enabled;
        layer_node.append_attribute("kind")                 = static_cast<uint32_t>(layer.kind);
        layer_node.append_attribute("mountain_rocks")       = layer.mountain_rocks;
        layer_node.append_attribute("formation_spacing")    = layer.formation_spacing;
        layer_node.append_attribute("formation_length")     = layer.formation_length;
        layer_node.append_attribute("formation_width")      = layer.formation_width;
        layer_node.append_attribute("formation_height")     = layer.formation_height;
        layer_node.append_attribute("formation_jitter")     = layer.formation_jitter;
        layer_node.append_attribute("embed_fraction")       = layer.embed_fraction;
        layer_node.append_attribute("coating")              = layer.coating;
        layer_node.append_attribute("coating_scale")        = layer.coating_scale;
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
        layer_node.append_attribute("clump_coverage")       = layer.clump_coverage;
        layer_node.append_attribute("clump_invert")         = layer.clump_invert;
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
        layer_node.append_attribute("blend_height")         = layer.blend_height;
        layer_node.append_attribute("blend_sharpness")      = layer.blend_sharpness;
        layer_node.append_attribute("render_distance")      = layer.render_distance;
        layer_node.append_attribute("shadow_distance")      = layer.shadow_distance;
        layer_node.append_attribute("foliage_tint_r")       = layer.foliage_tint[0];
        layer_node.append_attribute("foliage_tint_g")       = layer.foliage_tint[1];
        layer_node.append_attribute("foliage_tint_b")       = layer.foliage_tint[2];
        layer_node.append_attribute("foliage_scattering")   = layer.foliage_scattering;
        layer_node.append_attribute("grass_ring_0")         = layer.grass_ring_radius[0];
        layer_node.append_attribute("grass_ring_1")         = layer.grass_ring_radius[1];
        layer_node.append_attribute("grass_ring_2")         = layer.grass_ring_radius[2];
        layer_node.append_attribute("grass_cell_0")         = layer.grass_cell_size[0];
        layer_node.append_attribute("grass_cell_1")         = layer.grass_cell_size[1];
        layer_node.append_attribute("grass_cell_2")         = layer.grass_cell_size[2];
        layer_node.append_attribute("flags")                = layer.flags;
    }

    void Terrain::LoadScatterLayer(const pugi::xml_node& layer_node, TerrainScatterLayer& layer, uint32_t index)
    {
        layer.name                 = layer_node.attribute("name").as_string("");
        layer.mesh_path            = layer_node.attribute("mesh_path").as_string("");
        layer.mesh_variants        = layer_node.attribute("mesh_variants").as_string("");
        layer.habitat              = layer_node.attribute("habitat").as_uint(0);
        layer.material_folder      = layer_node.attribute("material_folder").as_string("");
        layer.enabled              = layer_node.attribute("enabled").as_bool(false);
        layer.mountain_rocks       = layer_node.attribute("mountain_rocks").as_bool(false);
        layer.formation_spacing    = layer_node.attribute("formation_spacing").as_float(300.0f);
        layer.formation_length     = layer_node.attribute("formation_length").as_float(300.0f);
        layer.formation_width      = layer_node.attribute("formation_width").as_float(130.0f);
        layer.formation_height     = layer_node.attribute("formation_height").as_float(90.0f);
        layer.formation_jitter     = layer_node.attribute("formation_jitter").as_float(15.0f);
        layer.embed_fraction       = layer_node.attribute("embed_fraction").as_float(0.45f);
        layer.coating              = layer_node.attribute("coating").as_float(layer.mountain_rocks ? 0.65f : 0.0f);
        layer.coating_scale        = layer_node.attribute("coating_scale").as_float(3.0f);
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

        // a world saved before ground cover grew in pockets has no coverage attribute, and it
        // wrote a zero radius because the gpu kinds ignored that field back then. reading it
        // back would load the new look switched off, so those slots take the whole patch set
        // from the engine default instead. only the patch fields, the ring tuning in the file
        // is authored and stays
        if (layer_node.attribute("clump_coverage"))
        {
            layer.clump_coverage = layer_node.attribute("clump_coverage").as_float(0.0f);
            layer.clump_invert   = layer_node.attribute("clump_invert").as_bool(false);
        }
        else if (layer.kind != TerrainScatterKind::Mesh)
        {
            const TerrainScatterLayer& defaults = TerrainScatterDefaults::Get()[index];
            layer.clump_radius     = defaults.clump_radius;
            layer.clump_raggedness = defaults.clump_raggedness;
            layer.clump_coverage   = defaults.clump_coverage;
            layer.clump_invert     = defaults.clump_invert;
        }

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
        layer.blend_height         = layer_node.attribute("blend_height").as_float(1.0f);
        layer.blend_sharpness      = layer_node.attribute("blend_sharpness").as_float(0.5f);
        layer.render_distance      = layer_node.attribute("render_distance").as_float(0.0f);
        layer.shadow_distance      = layer_node.attribute("shadow_distance").as_float(150.0f);
        layer.foliage_tint[0]       = layer_node.attribute("foliage_tint_r").as_float(-1.0f);
        layer.foliage_tint[1]       = layer_node.attribute("foliage_tint_g").as_float(-1.0f);
        layer.foliage_tint[2]       = layer_node.attribute("foliage_tint_b").as_float(-1.0f);
        layer.foliage_scattering    = clamp(layer_node.attribute("foliage_scattering").as_float(0.35f), 0.0f, 1.0f);
        layer.grass_ring_radius[0] = layer_node.attribute("grass_ring_0").as_float(55.0f);
        layer.grass_ring_radius[1] = layer_node.attribute("grass_ring_1").as_float(180.0f);
        layer.grass_ring_radius[2] = layer_node.attribute("grass_ring_2").as_float(500.0f);
        layer.grass_cell_size[0]   = layer_node.attribute("grass_cell_0").as_float(0.36f);
        layer.grass_cell_size[1]   = layer_node.attribute("grass_cell_1").as_float(0.82f);
        layer.grass_cell_size[2]   = layer_node.attribute("grass_cell_2").as_float(2.1f);
        layer.flags                = layer_node.attribute("flags").as_uint(TerrainScatterFlags_CastShadows);
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
        node.append_attribute("blend_height")  = m_blend_height;

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
            rule_node.append_attribute("surface_cover")  = rule.surface_cover;
        }

        // scatter layers, the whole prop rule set travels with the world
        pugi::xml_node scatter_node = node.append_child("scatter");
        for (const TerrainScatterLayer& layer : m_scatter_layers)
        {
            pugi::xml_node layer_node = scatter_node.append_child("layer");

            SaveScatterLayer(layer_node, layer);
        }

        // pads cut for snapped floors, regenerate rebuilds them from this list
        pugi::xml_node platforms_node = node.append_child("platforms");
        for (const TerrainPlatform& platform : m_platforms)
        {
            pugi::xml_node pad = platforms_node.append_child("platform");
            pad.append_attribute("entity_id") = platform.entity_id;
            pad.append_attribute("min_x")     = platform.min_x;
            pad.append_attribute("min_z")     = platform.min_z;
            pad.append_attribute("max_x")     = platform.max_x;
            pad.append_attribute("max_z")     = platform.max_z;
            pad.append_attribute("center_x")  = platform.center_x;
            pad.append_attribute("center_z")  = platform.center_z;
            pad.append_attribute("half_x")    = platform.half_x;
            pad.append_attribute("half_z")    = platform.half_z;
            pad.append_attribute("yaw")       = platform.yaw;
            pad.append_attribute("height")    = platform.height;
            pad.append_attribute("margin")    = platform.margin;
            if (platform.anchored)
            {
                pad.append_attribute("anchor_x")  = platform.anchor_position.x;
                pad.append_attribute("anchor_y")  = platform.anchor_position.y;
                pad.append_attribute("anchor_z")  = platform.anchor_position.z;
                pad.append_attribute("anchor_qx") = platform.anchor_rotation.x;
                pad.append_attribute("anchor_qy") = platform.anchor_rotation.y;
                pad.append_attribute("anchor_qz") = platform.anchor_rotation.z;
                pad.append_attribute("anchor_qw") = platform.anchor_rotation.w;
            }
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
        LoadState(node, true);
    }

    void Terrain::LoadEditorState(pugi::xml_node& node, const TerrainSculptLayer* sculpt)
    {
        if (sculpt) { m_sculpt = *sculpt; m_sculpt_snapshot.reset(); }
        LoadState(node, false);
    }

    void Terrain::LoadState(pugi::xml_node& node, bool load_sculpt)
    {
        m_height_map_seed = nullptr;
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
        m_blend_height  = node.attribute("blend_height").as_float(0.35f);

        // hand sculpting, applied on top of whatever generate produces
        if (load_sculpt) { m_sculpt.Clear(); m_sculpt_snapshot.reset(); }
        if (load_sculpt && m_sculpt.LoadFromFile(get_terrain_sculpt_path()))
        {
            SP_LOG_INFO("loaded sculpt layer: %zu tiles", m_sculpt.GetTileCount());
        }

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
                bool cover_default = false;
                for (const TerrainLayerRule& defaults : TerrainLayerDefaults::Get())
                    if (defaults.name == rule.name)
                        cover_default = defaults.surface_cover;
                rule.surface_cover = rule_node.attribute("surface_cover").as_bool(cover_default);
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

                // a slot that was empty when the world was saved carries no authoring, so the engine
                // default wins, that is how a new default layer reaches a world saved before it existed
                const bool saved_empty = string(layer_node.attribute("mesh_path").as_string("")).empty() &&
                                         !layer_node.attribute("enabled").as_bool(false);
                if (saved_empty)
                {
                    continue;
                }

                LoadScatterLayer(layer_node, layer, index);
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

        m_platforms.clear();
        if (pugi::xml_node platforms_node = node.child("platforms"))
        {
            for (pugi::xml_node pad = platforms_node.child("platform"); pad; pad = pad.next_sibling("platform"))
            {
                TerrainPlatform platform;
                platform.entity_id = pad.attribute("entity_id").as_ullong(0);
                platform.min_x     = pad.attribute("min_x").as_float(0.0f);
                platform.min_z     = pad.attribute("min_z").as_float(0.0f);
                platform.max_x     = pad.attribute("max_x").as_float(0.0f);
                platform.max_z     = pad.attribute("max_z").as_float(0.0f);
                platform.center_x  = pad.attribute("center_x").as_float(0.0f);
                platform.center_z  = pad.attribute("center_z").as_float(0.0f);
                platform.half_x    = pad.attribute("half_x").as_float(0.0f);
                platform.half_z    = pad.attribute("half_z").as_float(0.0f);
                platform.yaw       = pad.attribute("yaw").as_float(0.0f);
                platform.height    = pad.attribute("height").as_float(0.0f);
                platform.margin    = min(pad.attribute("margin").as_float(0.0f), 6.0f);
                if (pad.attribute("anchor_qw"))
                {
                    platform.anchored        = true;
                    platform.anchor_position = Vector3(pad.attribute("anchor_x").as_float(), pad.attribute("anchor_y").as_float(), pad.attribute("anchor_z").as_float());
                    platform.anchor_rotation = Quaternion(pad.attribute("anchor_qx").as_float(), pad.attribute("anchor_qy").as_float(), pad.attribute("anchor_qz").as_float(), pad.attribute("anchor_qw").as_float(1.0f));
                    platform.seen_position   = platform.anchor_position;
                    platform.seen_rotation   = platform.anchor_rotation;
                }
                if (platform.half_x <= 0.05f || platform.half_z <= 0.05f)
                {
                    platform.center_x = (platform.min_x + platform.max_x) * 0.5f;
                    platform.center_z = (platform.min_z + platform.max_z) * 0.5f;
                    platform.half_x   = (platform.max_x - platform.min_x) * 0.5f;
                    platform.half_z   = (platform.max_z - platform.min_z) * 0.5f;
                    platform.yaw      = 0.0f;
                }
                else if (platform.max_x <= platform.min_x || platform.max_z <= platform.min_z)
                {
                    obb_write_aabb(
                        platform.center_x,
                        platform.center_z,
                        platform.half_x,
                        platform.half_z,
                        platform.yaw,
                        platform.min_x,
                        platform.min_z,
                        platform.max_x,
                        platform.max_z
                    );
                }

                if (platform.half_x > 0.05f && platform.half_z > 0.05f)
                {
                    m_platforms.push_back(platform);
                }
            }
        }

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

        // the exact bits, a cast to integer folds every value in a whole unit onto the same hash
        // and negative levels wrap, so a sea moved by half a metre would still hit the old cache
        auto hash_float = [&hash_combine](float value)
        {
            uint32_t bits = 0;
            memcpy(&bits, &value, sizeof(bits));
            hash_combine(bits);
        };
        auto hash_string = [&hash_combine](const string& text)
        {
            for (char c : text)
            {
                hash_combine(static_cast<uint64_t>(static_cast<uint8_t>(c)));
            }
            hash_combine(0xffu);
        };

        // bump when cache format or the generation algorithms change so old caches get invalidated
        const uint64_t cache_format_version = 16;
        hash_combine(cache_format_version);

        hash_float(m_min_y);
        hash_float(m_max_y);
        hash_float(m_level_snow);
        hash_float(m_shore_width);
        hash_combine(m_smoothing);
        hash_combine(m_density);
        hash_combine(m_scale);
        hash_combine(m_tile_count);
        hash_combine(m_create_border ? 1 : 0);

        // the sea the pipeline actually ran with, the water component wins over the terrain level,
        // and the entity height since the positions are local and the sea is world
        hash_float(GetSeaLevelLocal());

        if (m_height_map_seed)
        {
            hash_combine(m_height_map_seed->GetWidth());
            hash_combine(m_height_map_seed->GetHeight());
            hash_combine(m_height_map_seed->GetBitsPerChannel());
            hash_combine(m_height_map_seed->GetChannelCount());

            // the path is stable across runs, the write time catches a repainted map at the same path
            const string& file_path = m_height_map_seed->GetResourceFilePath();
            hash_string(file_path);
            if (FileSystem::Exists(file_path))
            {
                hash_string(FileSystem::GetLastWriteTime(file_path));
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

    bool Terrain::IsHeightfieldUnsafe() const
    {
        return m_worker_busy.load(memory_order_acquire) > 0 && this_thread::get_id() != m_worker_thread;
    }

    float Terrain::GetEntityY() const
    {
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

        return terrain_y;
    }

    float Terrain::ResolveSeaLevelWorld() const
    {
        // the water component is the sea the player sees, the terrain level is the fallback when
        // there is none, both sides of the pipeline have to agree on which one is in charge
        for (Entity* entity : World::GetEntities())
        {
            if (!entity)
            {
                continue;
            }

            if (Water* water = entity->GetComponent<Water>())
            {
                return water->GetSeaLevel();
            }
        }

        return m_level_sea;
    }

    float Terrain::GetSeaLevelLocal() const
    {
        // triangle heights are entity local, the levels are world
        return ResolveSeaLevelWorld() - GetEntityY();
    }

    float Terrain::GetSnowLevelLocal() const
    {
        return m_level_snow - GetEntityY();
    }

    Vector4 Terrain::GetMappingWorld() const
    {
        // the mapping is authored over the local positions, callers holding world xz shift it by
        // the entity translation, the same thing the renderer does for the height and grass paths
        Vector4 mapping = m_world_mapping;
        if (Entity* entity = GetEntity())
        {
            const Vector3 translation = entity->GetMatrix().GetTranslation();
            mapping.x += translation.x;
            mapping.y += translation.z;
        }

        return mapping;
    }

    bool Terrain::SampleSurface(float world_x, float world_z, TerrainSurfaceSample& sample_out) const
    {
        if (IsHeightfieldUnsafe() || m_positions.empty() || !GetEntity() || m_map_a_pixels.empty() || m_map_b_pixels.empty() || m_map_width == 0 || m_map_height == 0)
        {
            return false;
        }

        const Vector4 mapping = GetMappingWorld();
        float u = (world_x - mapping.x) * mapping.z;
        float v = (world_z - mapping.y) * mapping.w;
        if (!std::isfinite(u) || !std::isfinite(v) || u < 0.0f || u > 1.0f || v < 0.0f || v > 1.0f)
            return false;
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

        const Vector3 local = GetEntity()->GetMatrix().Inverted() * Vector3(world_x, 0.0f, world_z);
        const TerrainGridMapping grid = GetGridMapping();
        const float height = TerrainSystem::SampleHeight(m_positions, m_dense_width, m_dense_height, local.x, local.z, grid);
        const Vector3 normal = TerrainSystem::SampleNormal(m_positions, m_dense_width, m_dense_height, local.x, local.z, grid);
        const float slope = acosf(clamp(normal.y, -1.0f, 1.0f)) * math::rad_to_deg;
        sample_out.woodland = terrain_habitat_shared::habitat_woodland(world_x, world_z,
            height - GetSeaLevelLocal(), slope, sample_out.insolation, sample_out.deposition);
        sample_out.grove = terrain_habitat_shared::habitat_grove(world_x, world_z,
            height - GetSeaLevelLocal(), slope, sample_out.woodland);
        sample_out.scrub = (0.25f + 0.75f * (1.0f - sample_out.woodland))
            * (1.0f - sample_out.grove * 0.65f)
            * terrain_habitat::smooth((terrain_habitat::noise(world_x / 70.0f, world_z / 70.0f, 521u) - 0.25f) / 0.4f);

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

    uint64_t Terrain::GetScatterCacheKey(uint32_t tile_index, const TerrainScatterLayer& layer, const BoundingBox* bounds) const
    {
        generated_cache::Hash hash;
        hash.Add(uint32_t(2)); // placement and habitat algorithms
        hash.Add(tile_index); hash.Add(m_tile_count); hash.Add(m_dense_width); hash.Add(m_dense_height);
        hash.Add(m_density); hash.Add(m_scale); hash.Add(GetSeaLevelLocal());
        hash.Add(GetEntity()->GetMatrix()); hash.Add(m_world_mapping);
        hash.Add(m_map_width); hash.Add(m_map_height);
        if (bounds) { hash.Add(bounds->GetMin()); hash.Add(bounds->GetMax()); }
        hash.Add(layer.name);
        hash.Add(layer.mesh_path);
        hash.Add(layer.mesh_variants);
        hash.Add(layer.habitat);
        hash.Add(layer.material_folder);
        hash.Add(layer.enabled);
        hash.Add(layer.kind);
        hash.Add(layer.mountain_rocks);
        hash.Add(layer.formation_spacing);
        hash.Add(layer.formation_length);
        hash.Add(layer.formation_width);
        hash.Add(layer.formation_height);
        hash.Add(layer.formation_jitter);
        hash.Add(layer.embed_fraction);
        hash.Add(layer.coating);
        hash.Add(layer.coating_scale);
        hash.Add(layer.density);
        hash.Add(layer.max_per_tile);
        hash.Add(layer.seed);
        hash.Add(layer.slope_min);
        hash.Add(layer.slope_max);
        hash.Add(layer.slope_bias);
        hash.Add(layer.height_min);
        hash.Add(layer.height_max);
        hash.Add(layer.height_fade);
        hash.Add(layer.curvature_influence);
        hash.Add(layer.flow_influence);
        hash.Add(layer.occlusion_influence);
        hash.Add(layer.insolation_influence);
        hash.Add(layer.wear_influence);
        hash.Add(layer.deposition_influence);
        hash.Add(layer.talus_influence);
        hash.Add(layer.ground_mask);
        hash.Add(layer.mask_channel);
        hash.Add(layer.mask_min);
        hash.Add(layer.clump_radius);
        hash.Add(layer.clump_count);
        hash.Add(layer.clump_raggedness);
        hash.Add(layer.clump_coverage);
        hash.Add(layer.clump_invert);
        hash.Add(layer.mesh_scale);
        hash.Add(layer.size_min);
        hash.Add(layer.size_max);
        hash.Add(layer.size_from_slope);
        hash.Add(layer.size_from_altitude);
        hash.Add(layer.altitude_span);
        hash.Add(layer.giant_chance);
        hash.Add(layer.giant_size);
        hash.Add(layer.align_to_normal);
        hash.Add(layer.surface_offset);
        hash.Add(layer.sink);
        hash.Add(layer.blend_height);
        hash.Add(layer.blend_sharpness);
        hash.Add(layer.render_distance);
        hash.Add(layer.shadow_distance);
        hash.Add(layer.grass_ring_radius);
        hash.Add(layer.grass_cell_size);
        hash.Add(layer.flags);
        const uint32_t axis = max(m_tile_count, 1u);
        const TerrainGridMapping mapping = GetGridMapping();
        // Formations can sample support beyond their owner tile. Include their full reach.
        const float reach = layer.mountain_rocks ? 8.0f * (clamp(layer.formation_spacing, 8.0f, 1000.0f) +
            clamp(layer.formation_length, 1.0f, 1500.0f) + clamp(layer.formation_width, 1.0f, 1000.0f)) : 0.0f;
        const uint32_t halo_x = 2 + static_cast<uint32_t>(ceilf(reach / max(mapping.scale_x, 0.001f)));
        const uint32_t halo_z = 2 + static_cast<uint32_t>(ceilf(reach / max(mapping.scale_z, 0.001f)));
        uint32_t x0 = (tile_index % axis) * (m_dense_width - 1) / axis;
        uint32_t x1 = (tile_index % axis + 1) * (m_dense_width - 1) / axis;
        uint32_t z0 = (tile_index / axis) * (m_dense_height - 1) / axis;
        uint32_t z1 = (tile_index / axis + 1) * (m_dense_height - 1) / axis;
        x0 = x0 > halo_x ? x0 - halo_x : 0; x1 = min(x1 + halo_x, m_dense_width - 1);
        z0 = z0 > halo_z ? z0 - halo_z : 0; z1 = min(z1 + halo_z, m_dense_height - 1);
        for (uint32_t z = z0; z <= z1; ++z)
            hash.Bytes(m_positions.data() + size_t(z) * m_dense_width + x0, size_t(x1 - x0 + 1) * sizeof(Vector3));
        if (m_map_width && m_map_height)
        {
            const uint32_t mx0 = static_cast<uint32_t>(uint64_t(x0) * (m_map_width - 1) / (m_dense_width - 1));
            const uint32_t mx1 = min(m_map_width - 1, 1 + static_cast<uint32_t>(uint64_t(x1) * (m_map_width - 1) / (m_dense_width - 1)));
            const uint32_t mz0 = static_cast<uint32_t>(uint64_t(z0) * (m_map_height - 1) / (m_dense_height - 1));
            const uint32_t mz1 = min(m_map_height - 1, 1 + static_cast<uint32_t>(uint64_t(z1) * (m_map_height - 1) / (m_dense_height - 1)));
            for (const auto* pixels : {&m_map_a_pixels, &m_map_b_pixels, &m_prop_mask_pixels, &m_layer_dominant})
            {
                hash.Add(uint64_t(pixels->size()));
                const size_t channels = pixels == &m_layer_dominant ? 1 : 4;
                if (pixels->size() < size_t(m_map_width) * m_map_height * channels) continue;
                for (uint32_t z = mz0; z <= mz1; ++z)
                    hash.Bytes(pixels->data() + (size_t(z) * m_map_width + mx0) * channels, size_t(mx1 - mx0 + 1) * channels);
            }
        }
        return hash.value;
    }

    void Terrain::FindTransforms(
        const uint32_t tile_index,
        const TerrainScatterLayer& layer,
        vector<Matrix>& transforms_out,
        float* coverage_out,
        const BoundingBox* mesh_bounds
    )
    {
        placement::ScatterContext context;
        context.sea_local     = GetSeaLevelLocal();
        context.triangle_area = GetTriangleArea();
        context.tile_offset   = (tile_index < m_tile_offsets.size()) ? m_tile_offsets[tile_index] : Vector3::Zero;
        context.mesh_bounds   = mesh_bounds;
        context.sample_ground = [this](float x, float z, terrain_placement::Surface& out) -> bool
        {
            if (m_positions.empty())
                return false;
            const TerrainGridMapping mapping = GetGridMapping();
            if (x < m_positions.front().x || z < m_positions.front().z ||
                x > m_positions.back().x || z > m_positions.back().z)
                return false;
            out.height = TerrainSystem::SampleHeight(m_positions, m_dense_width, m_dense_height, x, z, mapping);
            out.normal = TerrainSystem::SampleNormal(m_positions, m_dense_width, m_dense_height, x, z, mapping);
            return true;
        };
        // triangle centroids plus the tile offset are terrain local, the sampler wants world xz
        Vector3 translation = Vector3::Zero;
        if (Entity* entity = GetEntity())
        {
            translation = entity->GetMatrix().GetTranslation();
        }
        context.sample_surface = [this, translation](float x, float z, TerrainSurfaceSample& out) -> bool
        {
            return SampleSurface(x + translation.x, z + translation.z, out);
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

        // edits still waiting on a flush have not reached the flat height mirror yet
        if (!m_height_dirty.IsEmpty() && m_height_data.size() == m_positions.size())
        {
            TerrainSystem::SyncHeightDataFromPositions(m_height_data, m_positions);
        }

        // This file stores the eroded heightfield. Prepared tiles and placement have
        // separate content-addressed caches so authored edits invalidate only their dependents.
        uint32_t width            = GetWidth();
        uint32_t height           = GetHeight();
        uint32_t height_data_size = static_cast<uint32_t>(m_height_data.size());
        uint32_t position_count   = static_cast<uint32_t>(m_positions.size());
        uint64_t cache_hash       = ComputeCacheHash();

        // header
        file.write(reinterpret_cast<const char*>(&cache_hash), sizeof(uint64_t));
        file.write(reinterpret_cast<const char*>(&width), sizeof(uint32_t));
        file.write(reinterpret_cast<const char*>(&height), sizeof(uint32_t));
        file.write(reinterpret_cast<const char*>(&height_data_size), sizeof(uint32_t));
        file.write(reinterpret_cast<const char*>(&position_count), sizeof(uint32_t));
        file.write(reinterpret_cast<const char*>(&m_dense_width), sizeof(uint32_t));
        file.write(reinterpret_cast<const char*>(&m_dense_height), sizeof(uint32_t));
        file.write(reinterpret_cast<const char*>(&m_area_km2), sizeof(float));

        // main data
        file.write(reinterpret_cast<const char*>(m_height_data.data()), height_data_size * sizeof(float));
        file.write(reinterpret_cast<const char*>(m_positions.data()), position_count * sizeof(Vector3));

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

        uint32_t height_data_size = 0;
        uint32_t position_count   = 0;
        uint32_t dense_width      = 0;
        uint32_t dense_height     = 0;
        float area_km2            = 0.0f;

        file.read(reinterpret_cast<char*>(&m_width), sizeof(uint32_t));
        file.read(reinterpret_cast<char*>(&m_height), sizeof(uint32_t));
        file.read(reinterpret_cast<char*>(&height_data_size), sizeof(uint32_t));
        file.read(reinterpret_cast<char*>(&position_count), sizeof(uint32_t));
        file.read(reinterpret_cast<char*>(&dense_width), sizeof(uint32_t));
        file.read(reinterpret_cast<char*>(&dense_height), sizeof(uint32_t));
        file.read(reinterpret_cast<char*>(&area_km2), sizeof(float));

        // a truncated or hand edited cache must not leave half loaded arrays behind, every count has
        // to agree with the grid the header describes before a single byte of payload is trusted
        auto reject = [&](const char* reason)
        {
            SP_LOG_ERROR("terrain cache rejected, %s, regenerating", reason);
            file.close();
            m_height_data.clear();
            m_positions.clear();
        };

        if (!file.good())
        {
            reject("header truncated");
            return;
        }

        const uint64_t grid_count = static_cast<uint64_t>(dense_width) * dense_height;
        if (dense_width < 2 || dense_height < 2 || grid_count > (1ull << 28))
        {
            reject("dense grid out of range");
            return;
        }

        if (position_count != grid_count || height_data_size != grid_count)
        {
            reject("array sizes do not match the grid");
            return;
        }

        m_dense_width  = dense_width;
        m_dense_height = dense_height;
        m_area_km2     = area_km2;

        m_height_data.resize(height_data_size);
        m_positions.resize(position_count);

        file.read(reinterpret_cast<char*>(m_height_data.data()), height_data_size * sizeof(float));
        file.read(reinterpret_cast<char*>(m_positions.data()), position_count * sizeof(Vector3));
        if (!file.good())
        {
            reject("payload truncated");
            return;
        }

        file.close();
        SP_LOG_INFO("loaded terrain from cache: hash=%llu", stored_hash);
    }

    void Terrain::Generate()
    {
        const Stopwatch generation_timer;
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

        worker_scope worker(m_worker_busy, m_worker_thread);

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

        // the heightfield is about to be rebuilt from scratch, the old road grading no longer applies
        ClearRoadCarve();

        m_progress = ProgressTracker::Begin(ProgressType::Terrain, GetEntity()->GetObjectName(), "Generating terrain");
        try
        {

            // try loading from cache in the world resource directory
            const string cache_file = get_terrain_cache_bin_path();
            bool loaded_from_cache  = false;

            LoadFromFile(cache_file.c_str());
            if (!m_positions.empty())
            {
                loaded_from_cache = true;
                m_progress.SetStep("Loaded from cache");

                // old caches still have a flat coast, lock it without rerunning erosion
                const bool shoreline_moved = ApplyShorelineLock();
                if (shoreline_moved)
                {
                    m_progress.SetStep("Locking shoreline");
                    SaveToFile(cache_file.c_str());
                }

            }

            if (!loaded_from_cache)
            {
                SP_LOG_INFO("generating terrain from scratch...");

                // 1. process height map
                m_progress.SetStep("Processing height map");
                TerrainSystem::GetValuesFromHeightMap(m_height_data, m_height_map_seed, m_min_y, m_max_y, m_smoothing, m_create_border);
                m_width  = m_height_map_seed->GetWidth();
                m_height = m_height_map_seed->GetHeight();
                TerrainSystem::DensifyHeightMap(m_height_data, m_width, m_height, m_density);
                m_dense_width  = m_density * (m_width - 1) + 1;
                m_dense_height = m_density * (m_height - 1) + 1;

                // 2. generate positions
                m_progress.SetStep("Generating positions");
                m_positions.resize(m_dense_width * m_dense_height);
                TerrainSystem::GeneratePositions(m_positions, m_height_data, m_dense_width, m_dense_height, m_density, m_scale);

                // positions are entity local, so every step below gets the sea in the same frame the
                // shoreline lock and the channel carve use, one sea for the whole pipeline
                const float sea_local = GetSeaLevelLocal();

                // 3. apply perlin noise
                m_progress.SetStep("Applying perlin noise");
                TerrainSystem::ApplyPerlinNoise(m_positions, m_dense_width, m_dense_height, sea_local);

                // 4. apply erosion, keeping what it moved so the texturing can key off it
                m_progress.SetStep("Applying erosion");
                TerrainSystem::ApplyErosion(m_positions, m_dense_width, m_dense_height, sea_local, 1.0f, &m_erosion_maps);

                // lift the real coastline above the waves and cut a beach
                m_progress.SetStep("Locking shoreline");
                ApplyShorelineLock();

                m_progress.SetStep("Carving channels");
                ApplyFlowChannelCarve();

                // 5. generate vertices and indices
                m_progress.SetStep("Generating mesh");
                m_vertices.resize(m_dense_width * m_dense_height);
                m_indices.resize((m_dense_width - 1) * (m_dense_height - 1) * 6);
                TerrainSystem::GenerateVerticesAndIndices(m_vertices, m_indices, m_positions, m_dense_width, m_dense_height);

                // Normals, tiles and placement are derived after sculpt/platform edits.
                // Building them here would immediately discard and repeat the same work.

                // surface area is expensive, computed once here so the cache carries it and a hit skips it
                m_area_km2 = TerrainSystem::ComputeSurfaceAreaKm2(m_vertices, m_indices);

                SaveToFile(cache_file.c_str());
            }

            // Erosion analysis affects placement and materials, even though the heights are already
            // baked. Preserve it too, so the first load and subsequent loads use identical inputs.
            generated_cache::Hash erosion_key;
            erosion_key.Add(uint32_t(1)); erosion_key.Add(ComputeCacheHash()); erosion_key.Add(m_positions);
            const auto erosion_path = generated_cache::Path(World::GetResourceDirectory(), "erosion", erosion_key.value);
            if (loaded_from_cache)
            {
                m_erosion_maps = TerrainErosionMaps{};
                if (!generated_cache::Load(erosion_path, erosion_key.value, m_erosion_maps.wear, m_erosion_maps.deposition) ||
                    !m_erosion_maps.IsValid(m_positions.size())) m_erosion_maps = TerrainErosionMaps{};
            }
            else if (m_erosion_maps.IsValid(m_positions.size()))
            {
                generated_cache::Save(erosion_path, erosion_key.value, m_erosion_maps.wear, m_erosion_maps.deposition);
            }

            // the cache above is pure procedural ground, hand sculpting goes on top of it and the seed
            // the pads paint from has to include it
            m_progress.SetStep("Applying sculpt layer");
            ApplySculptLayer();

            SnapshotSeed();
            m_live_pad_active = false;
            m_live_pad_dirty  = false;
            m_live_pad_props_dirty = false;

            if (!ProgressTracker::IsLoading(ProgressType::World))
            {
                PruneOrphanPlatforms();
            }

            ApplyPlatformsToHeightfield();

            // Prepared tiles include the final sculpt/platform surface. A warm load never needs
            // the full-grid vertices, normals, placement triangles or individual LOD caches.
            m_triangle_data.clear();
            const float surface_ms = generation_timer.GetElapsedTimeMs();
            BakeTerrainMaps(true);
            BakeHeightMapPixels();
            ReapplyPropMaskHoles();

            // the dense erosion grid is only needed for the analysis bake above, it is tens of
            // megabytes and nothing reads it afterwards
            m_erosion_maps = TerrainErosionMaps();

            // compute stats
            m_height_samples = m_dense_width * m_dense_height;
            m_vertex_count   = m_dense_width * m_dense_height;
            m_index_count    = (m_dense_width - 1) * (m_dense_height - 1) * 6;
            m_triangle_count = m_index_count / 3;

            m_progress.SetStep("Building mesh");

            m_mesh_pending.reset();
            const float maps_ms = generation_timer.GetElapsedTimeMs();
            BuildCpuMesh();
            SP_LOG_INFO("Terrain load: surface %.2f ms, maps %.2f ms, mesh %.2f ms",
                surface_ms, maps_ms - surface_ms, generation_timer.GetElapsedTimeMs() - maps_ms);

            m_progress.SetStep("Waiting for scene preparation");
            m_gpu_commit_pending.store(true, memory_order_release);
        }
        catch (...)
        {
            FinishGenerate();
            throw;
        }
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
        m_props_population_step = {};
        m_mesh_pending.reset();
        m_progress.Finish();
        m_is_generating.store(false, memory_order_release);
    }

    void Terrain::BuildCpuMesh()
    {
        const Stopwatch timer;
        const uint32_t axis = max(m_tile_count, 1u);
        const uint32_t count = axis * axis;
        vector<shared_ptr<Mesh>> tiles(count);
        vector<uint64_t> keys(count);
        m_tile_offsets.resize(count);
        atomic<uint32_t> hits = 0;
        const string resources = World::GetResourceDirectory();
        ThreadPool::ParallelLoop([&](uint32_t begin, uint32_t end)
        {
            for (uint32_t i = begin; i < end; ++i)
            {
                const uint32_t x0 = (i % axis) * (m_dense_width - 1) / axis;
                const uint32_t x1 = (i % axis + 1) * (m_dense_width - 1) / axis;
                const uint32_t z0 = (i / axis) * (m_dense_height - 1) / axis;
                const uint32_t z1 = (i / axis + 1) * (m_dense_height - 1) / axis;
                const Vector3& lo = m_positions[size_t(z0) * m_dense_width + x0];
                const Vector3& hi = m_positions[size_t(z1) * m_dense_width + x1];
                m_tile_offsets[i] = Vector3((lo.x + hi.x) * 0.5f, 0, (lo.z + hi.z) * 0.5f);
                generated_cache::Hash hash;
                hash.Add(uint32_t(1)); // grid normals, UVs, tiling, LOD and meshlet policy/layout
                hash.Add(sizeof(RHI_Vertex_PosTexNorTan)); hash.Add(sizeof(Sb_MeshletBounds));
                hash.Add(sizeof(MeshLod)); hash.Add(mesh_lod_count);
                hash.Add(m_dense_width); hash.Add(m_dense_height); hash.Add(axis); hash.Add(i);
                // One cell of neighbours affects edge normals, even when its own tile is unchanged.
                const uint32_t left = x0 ? x0 - 1 : 0;
                const uint32_t right = min(x1 + 1, m_dense_width - 1);
                for (uint32_t z = z0 ? z0 - 1 : 0; z <= min(z1 + 1, m_dense_height - 1); ++z)
                    hash.Bytes(m_positions.data() + size_t(z) * m_dense_width + left,
                        size_t(right - left + 1) * sizeof(Vector3));
                keys[i] = hash.value;
                auto tile = make_shared<Mesh>();
                if (tile->LoadPrepared(generated_cache::Path(resources, "terrain_tiles", hash.value).string(), hash.value))
                {
                    tiles[i] = move(tile);
                    hits.fetch_add(1, memory_order_relaxed);
                }
            }
        }, count);
        if (hits.load() != count)
        {
            RebuildMeshData(false);
            ThreadPool::ParallelLoop([&](uint32_t begin, uint32_t end)
            {
                for (uint32_t i = begin; i < end; ++i)
                {
                    if (tiles[i]) continue;
                    auto tile = make_shared<Mesh>();
                    tile->SetFlag(static_cast<uint32_t>(MeshFlags::PostProcessOptimize), false);
                    tile->SetFlag(static_cast<uint32_t>(MeshFlags::PostProcessPreserveTerrainEdges), true);
                    tile->AddGeometry(m_tile_vertices[i], m_tile_indices[i], true);
                    tile->SavePrepared(generated_cache::Path(resources, "terrain_tiles", keys[i]).string(), keys[i]);
                    tiles[i] = move(tile);
                }
            }, count);
        }
        m_mesh_pending = make_shared<Mesh>();
        m_mesh_pending->SetObjectName("terrain_mesh");
        m_mesh_pending->SetFlag(static_cast<uint32_t>(MeshFlags::PostProcessOptimize), false);
        m_mesh_pending->SetFlag(static_cast<uint32_t>(MeshFlags::PostProcessPreserveTerrainEdges), true);
        m_mesh_pending->ReserveSubMeshes(count);
        // Stable append order keeps the prepared mesh identical across worker scheduling.
        for (uint32_t i = 0; i < count; ++i) m_mesh_pending->AppendPrepared(*tiles[i], i);
        SP_LOG_INFO("Terrain tile bake: %u hits, %u misses (missing/stale/invalid), %.2f ms",
            hits.load(), count - hits.load(), timer.GetElapsedTimeMs());
    }

    void Terrain::BuildTileMesh(Mesh& mesh, const bool generate_lods)
    {
        const uint32_t tile_count = static_cast<uint32_t>(m_tile_vertices.size());
        if (tile_count == 0)
        {
            return;
        }

        // lod simplification and meshlet building dominate terrain generation, every tile is
        // independent so they run across the pool, the slot is reserved so tile i stays sub mesh i
        mesh.ReserveSubMeshes(tile_count);
        ThreadPool::ParallelLoop([this, &mesh, generate_lods](const uint32_t start, const uint32_t end)
        {
            for (uint32_t tile_index = start; tile_index < end; tile_index++)
            {
                mesh.AddGeometry(m_tile_vertices[tile_index], m_tile_indices[tile_index], generate_lods, tile_index);
            }
        }, tile_count);
    }

    void Terrain::Tick()
    {
        SP_PROFILE_CPU();
        if (m_gpu_commit_pending.exchange(false, memory_order_acq_rel))
        {
            CommitGpu();
            return;
        }

        if (m_props_commit_pending.exchange(false, memory_order_acq_rel))
        {
            CommitProps();
        }

        const bool carves_can_run = !m_is_generating.load(memory_order_acquire) && (!ProgressTracker::IsLoading() || World::IsPreparing()) &&
            !Spline::HasPendingRoadWork();

        // grade the ground before the props are re-evaluated, they key off the new surface
        if (m_road_carve_dirty && carves_can_run)
        {
            RefreshSplineHeightCarves();
        }

        if (m_spline_carve_dirty && carves_can_run)
        {
            m_spline_carve_dirty = false;
            RefreshSplinePropCarves();
        }

        if (!Engine::IsFlagSet(EngineMode::Playing) && !m_is_generating.load(memory_order_acquire))
        {
            UpdateLivePads();
        }
        else if (Engine::IsFlagSet(EngineMode::Playing) && (m_live_pad_dirty || m_live_pad_active))
        {
            CommitLivePad(true);
            DestroyPadOverlays();
            m_live_pad_active   = false;
            m_live_track_entity = 0;
        }
    }

    void Terrain::CommitGpu()
    {
        const Stopwatch commit_timer;
        m_progress.SetStep("Uploading gpu mesh");

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

        // everything below is a from scratch upload, pending region repairs are moot
        m_height_dirty.Clear();
        m_physics_dirty.Clear();
        m_prop_mask_bake_dirty.Clear();
        m_props_dirty.Clear();

        CreateTileEntities();
        m_progress.SetStep("Preparing terrain collision");
        RefreshPhysics();
        RefreshLayers();
        PushToRenderer();
        SpawnFlowRivers();
        DestroyPadOverlays();
        DestroyAllPadRefines();
        RebuildCommittedRefines();
        const float terrain_ms = commit_timer.GetElapsedTimeMs();
        m_progress.SetStep("Preparing terrain-conforming roads");
        generated_cache::Hash road_surface_hash;
        road_surface_hash.Add(uint32_t(2));
        road_surface_hash.Add(m_dense_width);
        road_surface_hash.Add(m_dense_height);
        road_surface_hash.Add(m_density);
        road_surface_hash.Add(m_scale);
        road_surface_hash.Add(GetEntity()->GetMatrix());
        // Roads use the sculpted seed, independently of movable building pads.
        // The same base must identify their bakes or saving a house invalidates every road.
        const auto& road_base = m_positions_seed.size() == m_positions.size() ? m_positions_seed : m_positions;
        for (const Vector3& point : road_base) road_surface_hash.Add(point);
        for (Entity* entity : World::GetEntities())
        {
            if (!entity)
            {
                continue;
            }

            if (Spline* spline = entity->GetComponent<Spline>())
            {
                if (spline->GetConformToTerrain())
                {
                    spline->QueueRoadRegeneration(road_surface_hash.value);
                }
            }
        }

        SP_LOG_INFO("Terrain commit: terrain %.2f ms, queue roads %.2f ms", terrain_ms, commit_timer.GetElapsedTimeMs() - terrain_ms);

        m_vertices.clear();
        m_indices.clear();
        m_tile_vertices.clear();
        m_tile_indices.clear();

        if (m_spawn_biome_props)
        {
            m_progress.SetStep("Populating vegetation and props");
            m_props_commit_pending.store(true, memory_order_release);
            return;
        }

        m_progress.Finish();
        m_is_generating.store(false, memory_order_release);
    }

    void Terrain::CommitProps()
    {
        // Populate once against the completed road network, rather than invalidating props per road.
        if (Spline::HasPendingRoadWork())
        {
            m_props_commit_pending.store(true, memory_order_release);
            return;
        }
        if (!m_props_population_step)
        {
            // Placement must see the finished ground and road exclusions, including on the first load.
            if (m_road_carve_dirty) RefreshSplineHeightCarves();
            if (m_spline_carve_dirty)
            {
                m_spline_carve_dirty = false;
                RefreshSplinePropCarves();
            }
            m_props_population_step = WorldHelpers::BeginTerrainBiomeProps(this);
        }
        // During loading there is no live scene to incrementally reveal. Keep
        // publishing batches within a bounded slice instead of paying a whole
        // editor frame (and another scene scan) for each four-tile batch.
        const Stopwatch population_slice;
        while (!m_props_population_step())
        {
            if (!World::IsPreparing() || population_slice.GetElapsedTimeMs() >= 20.0f)
            {
                m_props_commit_pending.store(true, memory_order_release);
                return;
            }
        }
        m_props_population_step = {};
        m_progress.Finish();
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
            // the direction is a vector, running it through the full matrix would add the translation
            Matrix inv = entity->GetMatrix().Inverted();
            Vector3 origin_local = inv * ray.GetStart();
            Vector3 far_local    = (inv * (ray.GetStart() + ray.GetDirection())) - origin_local;
            local_ray.m_origin    = origin_local;
            local_ray.m_direction = far_local.Normalized();
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

        auto try_pad = [&](const TerrainPlatform& pad) -> bool
        {
            // Any rotated rectangle fits inside this conservative square.
            // Most ground probes are nowhere near a building; reject those
            // before evaluating trigonometry for every platform in the world.
            const float reach = (fabsf(pad.half_x) + fabsf(pad.half_z)) * 1.000001f + 0.01f;
            if (fabsf(world_x - pad.center_x) > reach || fabsf(world_z - pad.center_z) > reach) return false;
            if (obb_outside_distance(
                world_x,
                world_z,
                pad.center_x,
                pad.center_z,
                pad.half_x,
                pad.half_z,
                pad.yaw) <= 0.0f)
            {
                height_out = pad.height;
                return true;
            }

            return false;
        };

        if (m_live_pad_active && try_pad(m_live_pad))
        {
            return true;
        }

        for (const TerrainPlatform& pad : m_platforms)
        {
            if (try_pad(pad))
            {
                return true;
            }
        }

        float local_x = world_x;
        float local_z = world_z;
        if (Entity* entity = GetEntity())
        {
            // Cache only the inverse transform, never sampled terrain data.
            // Matrix comparison keeps this valid across terrain edits/reloads,
            // and each worker owns its cache.
            static thread_local Matrix previous = Matrix::Identity;
            static thread_local Matrix inverse = Matrix::Identity;
            const Matrix& matrix = entity->GetMatrix();
            if (matrix != previous)
            {
                previous = matrix;
                inverse = matrix.Inverted();
            }
            Vector3 local = inverse * Vector3(world_x, 0.0f, world_z);
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
                // not the guarded check, the renderer keeps pointing at the terrain while it regenerates
                if (!terrain->m_positions.empty() && terrain->m_dense_width > 1 && terrain->m_dense_height > 1)
                {
                    return terrain;
                }
            }
        }

        return nullptr;
    }

    void Terrain::ApplyBrush(const Vector3& world_center, const TerrainBrush& brush)
    {
        m_sculpt_snapshot.reset();
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

        const TerrainGridMapping mapping = GetGridMapping();
        const float step_x = max(mapping.scale_x, 0.001f);
        const float step_z = max(mapping.scale_z, 0.001f);
        const int32_t cx   = static_cast<int32_t>(floorf((local_center.x + mapping.offset_x) / step_x));
        const int32_t cz   = static_cast<int32_t>(floorf((local_center.z + mapping.offset_z) / step_z));
        const int32_t rx   = static_cast<int32_t>(ceilf(brush.radius / step_x)) + 1;
        const int32_t rz   = static_cast<int32_t>(ceilf(brush.radius / step_z)) + 1;
        const int32_t x0   = max(cx - rx, 0);
        const int32_t z0   = max(cz - rz, 0);
        const int32_t x1   = min(cx + rx, static_cast<int32_t>(m_dense_width) - 1);
        const int32_t z1   = min(cz + rz, static_cast<int32_t>(m_dense_height) - 1);
        if (x1 < x0 || z1 < z0)
        {
            return;
        }

        // the heights before the stroke, what the brush moved is what the sculpt layer records
        const size_t span_x = static_cast<size_t>(x1 - x0 + 1);
        vector<float> before(span_x * static_cast<size_t>(z1 - z0 + 1));
        for (int32_t z = z0; z <= z1; z++)
        {
            const size_t row = static_cast<size_t>(z) * m_dense_width;
            float* dst       = before.data() + static_cast<size_t>(z - z0) * span_x;
            for (int32_t x = x0; x <= x1; x++)
            {
                dst[x - x0] = m_positions[row + static_cast<size_t>(x)].y;
            }
        }

        TerrainSystem::ApplyBrush(
            m_positions,
            &m_height_data,
            m_dense_width,
            m_dense_height,
            mapping,
            local_center,
            brush
        );

        // the layer is what survives a regenerate, the seed is what the pads paint from, both
        // have to carry the stroke or a pad passing over it later would wipe it
        EnsureSculptGrid();
        const bool seed_ok = m_positions_seed.size() == m_positions.size();
        for (int32_t z = z0; z <= z1; z++)
        {
            const size_t row = static_cast<size_t>(z) * m_dense_width;
            const float* src = before.data() + static_cast<size_t>(z - z0) * span_x;
            for (int32_t x = x0; x <= x1; x++)
            {
                const size_t index = row + static_cast<size_t>(x);
                const float moved  = m_positions[index].y - src[x - x0];
                if (fabsf(moved) < 1e-6f)
                {
                    continue;
                }

                m_sculpt.AddCell(x, z, moved);
                if (seed_ok)
                {
                    m_positions_seed[index].y += moved;
                }
            }
        }

        // the stroke lands on the mesh through the region flush, the editor decides when to flush,
        // the props under it are re-placed by FlushPendingProps once the stroke is over
        MarkHeightsDirty(x0, z0, x1, z1);
        m_props_dirty.Merge(x0, z0, x1, z1);
    }

    bool Terrain::FlattenRegion(
        float center_x,
        float center_z,
        float half_x,
        float half_z,
        float yaw,
        float world_height,
        float blend_margin
    )
    {
        if (!HasHeightfield())
        {
            return false;
        }

        ApplyFlattenToPositions(
            center_x,
            center_z,
            half_x,
            half_z,
            yaw,
            world_height,
            blend_margin
        );

        // region repair instead of a full remesh, the biome analysis maps keep their last bake
        FlushHeightEdits(true);
        PunchPropMaskFootprint(center_x, center_z, half_x + blend_margin, half_z + blend_margin, yaw);
        ClearFootprintProps(center_x, center_z, half_x + blend_margin, half_z + blend_margin, yaw);
        if (UploadPropMask())
        {
            PushToRenderer();
            WorldHelpers::RefreshTerrainGpuScatter(this);
        }
        return true;
    }

    void Terrain::ApplyFlattenToPositions(
        float center_x,
        float center_z,
        float half_x,
        float half_z,
        float yaw,
        float world_height,
        float blend_margin
    )
    {
        if (!HasHeightfield())
        {
            return;
        }

        float local_cx     = center_x;
        float local_cz     = center_z;
        float local_hx     = half_x;
        float local_hz     = half_z;
        float local_yaw    = yaw;
        float local_height = world_height;

        if (Entity* entity = GetEntity())
        {
            const Matrix inverse = entity->GetMatrix().Inverted();
            const Vector3 local_center = inverse * Vector3(center_x, world_height, center_z);
            local_cx     = local_center.x;
            local_cz     = local_center.z;
            local_height = local_center.y;

            const Vector3 world_axis(center_x + cosf(yaw), world_height, center_z + sinf(yaw));
            const Vector3 local_axis = inverse * world_axis - local_center;
            local_yaw = atan2f(local_axis.z, local_axis.x);

            const Vector3 world_x(center_x + cosf(yaw) * half_x, world_height, center_z + sinf(yaw) * half_x);
            const Vector3 world_z(center_x - sinf(yaw) * half_z, world_height, center_z + cosf(yaw) * half_z);
            local_hx = (inverse * world_x - local_center).Length();
            local_hz = (inverse * world_z - local_center).Length();
        }

        const TerrainGridMapping mapping = GetGridMapping();
        const float step_x = max(mapping.scale_x, 0.001f);
        const float step_z = max(mapping.scale_z, 0.001f);
        const float step   = max(step_x, step_z);
        const float margin = max(max(blend_margin, 0.0f), step);
        const float inner  = min(step * 0.51f, margin);

        // only the cells under the obb plus its ramp, a pad is a speck on the grid
        float min_x = 0.0f;
        float min_z = 0.0f;
        float max_x = 0.0f;
        float max_z = 0.0f;
        obb_write_aabb(local_cx, local_cz, local_hx, local_hz, local_yaw, min_x, min_z, max_x, max_z);
        const int32_t x0 = max(static_cast<int32_t>(floorf((min_x - margin + mapping.offset_x) / step_x)), 0);
        const int32_t z0 = max(static_cast<int32_t>(floorf((min_z - margin + mapping.offset_z) / step_z)), 0);
        const int32_t x1 = min(static_cast<int32_t>(ceilf((max_x + margin + mapping.offset_x) / step_x)), static_cast<int32_t>(m_dense_width) - 1);
        const int32_t z1 = min(static_cast<int32_t>(ceilf((max_z + margin + mapping.offset_z) / step_z)), static_cast<int32_t>(m_dense_height) - 1);

        for (int32_t z = z0; z <= z1; z++)
        {
            const size_t row = static_cast<size_t>(z) * m_dense_width;
            for (int32_t x = x0; x <= x1; x++)
            {
                Vector3& position    = m_positions[row + x];
                const float distance = obb_outside_distance(
                    position.x,
                    position.z,
                    local_cx,
                    local_cz,
                    local_hx,
                    local_hz,
                    local_yaw
                );

                if (distance > margin)
                {
                    continue;
                }

                float weight = 1.0f;
                if (distance > inner && margin > inner)
                {
                    const float t = (distance - inner) / (margin - inner);
                    weight        = 1.0f - (t * t * (3.0f - 2.0f * t));
                }

                position.y += (local_height - position.y) * weight;
            }
        }

        MarkHeightsDirty(x0, z0, x1, z1);
    }

    void Terrain::MarkSplinePropCarvesDirty(uint64_t /*spline_id*/)
    {
        m_spline_carve_dirty = true;
    }

    void Terrain::MarkSplineHeightCarvesDirty(uint64_t spline_id)
    {
        m_road_carve_dirty = true;
        if (spline_id == 0)
        {
            m_road_carve_dirty_all = true;
            return;
        }

        m_road_carve_dirty_ids.insert(spline_id);
    }

    // drop the carve record without restoring, the caller is rebuilding the heightfield from scratch
    void Terrain::ClearRoadCarve()
    {
        m_road_carve_delta.clear();
        m_road_carve_bounds.clear();
        m_road_carve_jobs.clear();
        m_road_carve_dirty_ids.clear();
        m_road_carve_dirty     = false;
        m_road_carve_dirty_all = false;
    }

    bool Terrain::SampleHeightBase(float world_x, float world_z, float& height_out) const
    {
        if (!HasHeightfield()) return false;

        Entity* entity = GetEntity();
        const Matrix matrix = entity ? entity->GetMatrix() : Matrix::Identity;
        const Vector3 local = entity ? matrix.Inverted() * Vector3(world_x, 0.0f, world_z) : Vector3(world_x, 0.0f, world_z);
        // The seed includes sculpting, but excludes both building pads and road
        // carves. Moving a building must not move a road on the next world load.
        const auto& base = m_positions_seed.size() == m_positions.size() ? m_positions_seed : m_positions;
        const float height = TerrainSystem::SampleHeight(base, m_dense_width, m_dense_height, local.x, local.z, GetGridMapping());
        height_out = (matrix * Vector3(local.x, height, local.z)).y;
        return true;
    }

    void Terrain::RefreshSplineHeightCarves()
    {
        const Stopwatch bake_timer;
        uint32_t bake_hits = 0, bake_misses = 0;
        m_road_carve_dirty = false;

        if (!HasHeightfield())
        {
            m_road_carve_dirty_ids.clear();
            m_road_carve_dirty_all = false;
            return;
        }

        const size_t cell_count = m_positions.size();
        if (m_road_carve_delta.size() != cell_count)
        {
            m_road_carve_delta.assign(cell_count, 0.0f);
            m_road_carve_bounds.clear();
            m_road_carve_jobs.clear();
            m_road_carve_dirty_all = true;
        }

        const TerrainGridMapping mapping = GetGridMapping();

        // the flat bench the carve holds either side of the deck, it must span at least one grid cell
        // or bilinear interpolation between vertices can climb straight over the road surface
        const float plateau = 0.75f * max(mapping.scale_x, mapping.scale_z);

        Matrix world_to_local = Matrix::Identity;
        float world_to_local_scale = 1.0f;
        if (Entity* entity = GetEntity())
        {
            world_to_local = entity->GetMatrix().Inverted();
            const Vector3 origin = world_to_local * Vector3::Zero;
            world_to_local_scale = ((world_to_local * Vector3::Right) - origin).Length();
            if (world_to_local_scale < 1e-4f)
            {
                world_to_local_scale = 1.0f;
            }
        }

        const int32_t grid_max_x = static_cast<int32_t>(m_dense_width) - 1;
        const int32_t grid_max_z = static_cast<int32_t>(m_dense_height) - 1;

        auto to_grid_bounds = [&](float min_x, float min_z, float max_x, float max_z)
        {
            std::array<int32_t, 4> bounds;
            bounds[0] = clamp(static_cast<int32_t>(floorf((min_x + mapping.offset_x) / mapping.scale_x)), 0, grid_max_x);
            bounds[1] = clamp(static_cast<int32_t>(ceilf ((max_x + mapping.offset_x) / mapping.scale_x)), 0, grid_max_x);
            bounds[2] = clamp(static_cast<int32_t>(floorf((min_z + mapping.offset_z) / mapping.scale_z)), 0, grid_max_z);
            bounds[3] = clamp(static_cast<int32_t>(ceilf ((max_z + mapping.offset_z) / mapping.scale_z)), 0, grid_max_z);
            return bounds;
        };

        // collect every road that grades the ground and convert its deck into terrain local space
        std::vector<RoadCarveJob> jobs;
        for (Entity* entity : World::GetEntities())
        {
            if (!entity)
            {
                continue;
            }

            Spline* spline = entity->GetComponent<Spline>();
            if (!spline || !spline->CarvesTerrain())
            {
                continue;
            }

            const std::vector<SplineCarveSample>& samples = spline->GetCarveSamples();
            if (samples.size() < 2)
            {
                continue;
            }

            RoadCarveJob job;
            job.id         = entity->GetObjectId();
            job.bed_drop   = spline->GetCarveBedDrop();
            job.fill_slope = tanf(clamp(spline->GetCarveFillSlopeDegrees(), 5.0f, 85.0f) * deg_to_rad);
            job.cut_slope  = tanf(clamp(spline->GetCarveCutSlopeDegrees(),  5.0f, 85.0f) * deg_to_rad);
            job.shoulder   = max(spline->GetCarveMaxShoulder(), plateau);

            job.points.reserve(samples.size());
            job.half_widths.reserve(samples.size());

            float min_x = numeric_limits<float>::max();
            float min_z = numeric_limits<float>::max();
            float max_x = -numeric_limits<float>::max();
            float max_z = -numeric_limits<float>::max();

            for (const SplineCarveSample& sample : samples)
            {
                const Vector3 local = world_to_local * sample.position;
                const float half    = sample.half_width * world_to_local_scale;
                job.points.push_back(local);
                job.half_widths.push_back(half);

                const float reach = half + job.shoulder;
                min_x = min(min_x, local.x - reach);
                max_x = max(max_x, local.x + reach);
                min_z = min(min_z, local.z - reach);
                max_z = max(max_z, local.z + reach);
            }

            job.bounds = to_grid_bounds(min_x, min_z, max_x, max_z);
            jobs.push_back(move(job));
        }

        // the dirty region is wherever a road used to be plus wherever it is now, roads that vanished
        // only contribute their old footprint so the ground there springs back
        std::vector<std::array<int32_t, 4>> dirty_regions;

        auto grow_rect = [&](const std::array<int32_t, 4>& bounds)
        {
            if (bounds[1] < bounds[0] || bounds[3] < bounds[2])
            {
                return;
            }

            auto merged = bounds;
            // Separate edits must not invalidate the untouched island between them.
            for (size_t i = 0; i < dirty_regions.size();)
            {
                const auto& region = dirty_regions[i];
                if (merged[1] < region[0] || merged[0] > region[1] ||
                    merged[3] < region[2] || merged[2] > region[3])
                {
                    ++i;
                    continue;
                }
                merged = {min(merged[0], region[0]), max(merged[1], region[1]),
                    min(merged[2], region[2]), max(merged[3], region[3])};
                dirty_regions.erase(dirty_regions.begin() + i);
                i = 0;
            }
            dirty_regions.push_back(merged);
        };

        std::unordered_map<uint64_t, std::array<int32_t, 4>> new_bounds;
        auto grow_segment = [&](const RoadCarveJob& job, size_t i)
        {
            const Vector3& a = job.points[i];
            const Vector3& b = job.points[i + 1];
            const float reach = max(job.half_widths[i], job.half_widths[i + 1]) + job.shoulder;
            grow_rect(to_grid_bounds(min(a.x, b.x) - reach, min(a.z, b.z) - reach,
                max(a.x, b.x) + reach, max(a.z, b.z) + reach));
        };
        for (const RoadCarveJob& job : jobs)
        {
            new_bounds[job.id] = job.bounds;

            if (m_road_carve_dirty_all || m_road_carve_dirty_ids.count(job.id) != 0)
            {
                const auto previous = m_road_carve_jobs.find(job.id);
                if (m_road_carve_dirty_all || previous == m_road_carve_jobs.end())
                {
                    grow_rect(job.bounds);
                    const auto old_bounds = m_road_carve_bounds.find(job.id);
                    if (old_bounds != m_road_carve_bounds.end()) grow_rect(old_bounds->second);
                    continue;
                }
                const RoadCarveJob& old = previous->second;
                const bool settings_changed = old.bed_drop != job.bed_drop || old.fill_slope != job.fill_slope ||
                    old.cut_slope != job.cut_slope || old.shoulder != job.shoulder;
                road_edit::changed_segments(old.points, job.points,
                    [&](size_t a, size_t b) { return old.points[a] == job.points[b] && old.half_widths[a] == job.half_widths[b]; },
                    [&](size_t i) { grow_segment(old, i); }, [&](size_t i) { grow_segment(job, i); }, settings_changed);
            }
        }

        for (const auto& previous : m_road_carve_bounds)
        {
            const bool vanished = new_bounds.find(previous.first) == new_bounds.end();
            if (vanished || m_road_carve_dirty_all)
            {
                grow_rect(previous.second);
            }
        }

        m_road_carve_bounds    = move(new_bounds);
        m_road_carve_jobs.clear();
        for (const RoadCarveJob& job : jobs) m_road_carve_jobs.emplace(job.id, job);
        m_road_carve_dirty_ids.clear();
        m_road_carve_dirty_all = false;

        if (dirty_regions.empty())
        {
            return;
        }

        for (const auto& region : dirty_regions)
        {
            const int32_t rect_x0 = region[0];
            const int32_t rect_x1 = region[1];
            const int32_t rect_z0 = region[2];
            const int32_t rect_z1 = region[3];
            const uint32_t rect_width  = static_cast<uint32_t>(rect_x1 - rect_x0 + 1);
            const uint32_t rect_height = static_cast<uint32_t>(rect_z1 - rect_z0 + 1);

            // put the ground back the way it was before any road touched this region
            for (int32_t z = rect_z0; z <= rect_z1; z++)
            {
                const size_t row = static_cast<size_t>(z) * m_dense_width;
                for (int32_t x = rect_x0; x <= rect_x1; x++)
                {
                    const size_t index = row + static_cast<size_t>(x);
                    m_positions[index].y   -= m_road_carve_delta[index];
                    m_road_carve_delta[index] = 0.0f;
                }
            }

            generated_cache::Hash carve_hash;
            carve_hash.Add(uint32_t(2)); // envelope/carve policy and region layout
            carve_hash.Add(region); carve_hash.Add(m_dense_width); carve_hash.Add(m_dense_height);
            carve_hash.Add(plateau);
            for (int32_t z = rect_z0; z <= rect_z1; ++z)
                carve_hash.Bytes(m_positions.data() + size_t(z) * m_dense_width + rect_x0, size_t(rect_width) * sizeof(Vector3));
            for (const RoadCarveJob& job : jobs)
            {
                if (job.bounds[1] < rect_x0 || job.bounds[0] > rect_x1 || job.bounds[3] < rect_z0 || job.bounds[2] > rect_z1) continue;
                carve_hash.Add(job.points); carve_hash.Add(job.half_widths);
                carve_hash.Add(job.bed_drop); carve_hash.Add(job.fill_slope); carve_hash.Add(job.cut_slope); carve_hash.Add(job.shoulder);
            }
            const auto carve_path = generated_cache::Path(World::GetResourceDirectory(), "road_carves", carve_hash.value);
            vector<float> baked_heights;
            const bool hit = generated_cache::Load(carve_path, carve_hash.value, baked_heights) &&
                baked_heights.size() == size_t(rect_width) * rect_height &&
                all_of(baked_heights.begin(), baked_heights.end(), [](float v) { return std::isfinite(v); });
            if (hit)
            {
                ++bake_hits;
                for (int32_t z = rect_z0; z <= rect_z1; ++z)
                for (int32_t x = rect_x0; x <= rect_x1; ++x)
                {
                    const size_t index = size_t(z) * m_dense_width + x;
                    const float height = baked_heights[size_t(z - rect_z0) * rect_width + x - rect_x0];
                    m_road_carve_delta[index] = height - m_positions[index].y;
                    m_positions[index].y = height;
                }
            }
            else
            {
                ++bake_misses;
                ApplyRoadHeightConstraints(m_positions, m_dense_width, mapping,
                    rect_x0, rect_z0, rect_x1, rect_z1, plateau, &m_road_carve_delta);

                baked_heights.reserve(size_t(rect_width) * rect_height);
                for (int32_t z = rect_z0; z <= rect_z1; ++z)
                for (int32_t x = rect_x0; x <= rect_x1; ++x)
                    baked_heights.push_back(m_positions[size_t(z) * m_dense_width + x].y);
                generated_cache::Save(carve_path, carve_hash.value, baked_heights);
            }

            // repair only what moved, the flush adds the seam ring and patches the height texture in place
            MarkHeightsDirty(rect_x0, rect_z0, rect_x1, rect_z1);
            FlushHeightEdits(true);
        }
        // Existing fine patches must follow road edits and the first road bake too.
        // Otherwise their pre-carve triangles can cover an otherwise protected deck.
        for (const TerrainPlatform& pad : m_platforms)
        {
            float cx, cz, hx, hz, yaw, height;
            platform_to_local(GetEntity(), pad, cx, cz, hx, hz, yaw, height);
            float min_x, min_z, max_x, max_z;
            obb_write_aabb(cx, cz, hx, hz, yaw, min_x, min_z, max_x, max_z);
            const float margin = pad_deform_margin(pad, max(mapping.scale_x, mapping.scale_z));
            const auto bounds = to_grid_bounds(min_x - margin, min_z - margin, max_x + margin, max_z + margin);
            for (const auto& region : dirty_regions)
            {
                if (bounds[1] < region[0] || bounds[0] > region[1] ||
                    bounds[3] < region[2] || bounds[2] > region[3]) continue;
                SyncPadRefine(pad, true);
                break;
            }
        }
        SP_LOG_INFO("Road carve bake: %u hits, %u misses (missing/stale/invalid), %.2f ms",
            bake_hits, bake_misses, bake_timer.GetElapsedTimeMs());
    }

    void Terrain::ApplyRoadHeightConstraints(
        vector<Vector3>& positions, uint32_t stride, const TerrainGridMapping& mapping,
        int32_t rect_x0, int32_t rect_z0, int32_t rect_x1, int32_t rect_z1,
        float plateau, vector<float>* deltas) const
    {
        if (rect_x1 < rect_x0 || rect_z1 < rect_z0 || m_road_carve_jobs.empty()) return;
        const uint32_t rect_width = static_cast<uint32_t>(rect_x1 - rect_x0 + 1);
        const uint32_t rect_height = static_cast<uint32_t>(rect_z1 - rect_z0 + 1);
        // Cached job bounds use the dense grid, including when constraining a finer patch.
        const TerrainGridMapping dense = GetGridMapping();
        const float dense_x0 = (rect_x0 * mapping.scale_x - mapping.offset_x + dense.offset_x) / dense.scale_x;
        const float dense_x1 = (rect_x1 * mapping.scale_x - mapping.offset_x + dense.offset_x) / dense.scale_x;
        const float dense_z0 = (rect_z0 * mapping.scale_z - mapping.offset_z + dense.offset_z) / dense.scale_z;
        const float dense_z1 = (rect_z1 * mapping.scale_z - mapping.offset_z + dense.offset_z) / dense.scale_z;
        // two envelopes, the highest fill cone and the lowest cut cone
        // the cut cone holds a flat plateau one grid cell wide around every road point, which is what
        // guarantees the carved surface can never interpolate up through the deck between vertices
        const size_t scratch_count = static_cast<size_t>(rect_width) * rect_height;
        std::vector<float> raise_to(scratch_count, -numeric_limits<float>::max());
        std::vector<float> lower_to(scratch_count,  numeric_limits<float>::max());

        for (const auto& [id, job] : m_road_carve_jobs)
        {
            if (job.bounds[1] < dense_x0 || job.bounds[0] > dense_x1 ||
                job.bounds[3] < dense_z0 || job.bounds[2] > dense_z1)
            {
                continue;
            }

            for (size_t s = 0; s + 1 < job.points.size(); s++)
            {
                const Vector3& a = job.points[s];
                const Vector3& b = job.points[s + 1];
                const float half_a = job.half_widths[s];
                const float half_b = job.half_widths[s + 1];
                const float reach  = max(half_a, half_b) + job.shoulder;

                const int32_t sx0 = max(static_cast<int32_t>(floorf((min(a.x, b.x) - reach + mapping.offset_x) / mapping.scale_x)), rect_x0);
                const int32_t sx1 = min(static_cast<int32_t>(ceilf ((max(a.x, b.x) + reach + mapping.offset_x) / mapping.scale_x)), rect_x1);
                const int32_t sz0 = max(static_cast<int32_t>(floorf((min(a.z, b.z) - reach + mapping.offset_z) / mapping.scale_z)), rect_z0);
                const int32_t sz1 = min(static_cast<int32_t>(ceilf ((max(a.z, b.z) + reach + mapping.offset_z) / mapping.scale_z)), rect_z1);

                if (sx1 < sx0 || sz1 < sz0)
                {
                    continue;
                }

                const float dx = b.x - a.x;
                const float dz = b.z - a.z;
                const float segment_length_sq = dx * dx + dz * dz;

                for (int32_t z = sz0; z <= sz1; z++)
                {
                    const size_t row        = static_cast<size_t>(z) * stride;
                    const size_t scratch_row = static_cast<size_t>(z - rect_z0) * rect_width;

                    for (int32_t x = sx0; x <= sx1; x++)
                    {
                        const size_t index = row + static_cast<size_t>(x);
                        const Vector3& cell = positions[index];

                        float t = 0.0f;
                        if (segment_length_sq > 1e-8f)
                        {
                            t = ((cell.x - a.x) * dx + (cell.z - a.z) * dz) / segment_length_sq;
                            t = clamp(t, 0.0f, 1.0f);
                        }

                        const float px = a.x + dx * t;
                        const float pz = a.z + dz * t;
                        const float ox = cell.x - px;
                        const float oz = cell.z - pz;
                        const float distance = sqrtf(ox * ox + oz * oz);

                        const float half = half_a + (half_b - half_a) * t;
                        if (distance > half + job.shoulder)
                        {
                            continue;
                        }

                        const size_t scratch = scratch_row + static_cast<size_t>(x - rect_x0);
                        const float bed      = (a.y + (b.y - a.y) * t) - job.bed_drop;

                        // fill cone, highest one wins so an embankment survives a neighbouring dip
                        const float fill_over = max(0.0f, distance - half);
                        raise_to[scratch] = max(raise_to[scratch], bed - fill_over * job.fill_slope);

                        // cut cone, lowest one wins, the plateau keeps it at bed level for a whole grid
                        // cell around the road so bilinear interpolation can never climb over the deck
                        const float cut_over = max(0.0f, distance - half - plateau);
                        lower_to[scratch] = min(lower_to[scratch], bed + cut_over * job.cut_slope);
                    }
                }
            }
        }

        // resolve both envelopes against the untouched ground
        for (int32_t z = rect_z0; z <= rect_z1; z++)
        {
            const size_t row         = static_cast<size_t>(z) * stride;
            const size_t scratch_row = static_cast<size_t>(z - rect_z0) * rect_width;

            for (int32_t x = rect_x0; x <= rect_x1; x++)
            {
                const size_t scratch = scratch_row + static_cast<size_t>(x - rect_x0);
                if (lower_to[scratch] == numeric_limits<float>::max())
                {
                    continue;
                }

                const size_t index = row + static_cast<size_t>(x);
                const float base   = positions[index].y;

                float target = max(base, raise_to[scratch]);
                target       = min(target, lower_to[scratch]);

                if (deltas) (*deltas)[index] = target - base;
                positions[index].y      = target;
            }
        }

    }

    void Terrain::CollectTilesInRegion(
        float local_min_x,
        float local_min_z,
        float local_max_x,
        float local_max_z,
        unordered_set<uint32_t>& tiles_out
    ) const
    {
        const TerrainGridMapping mapping = GetGridMapping();
        const uint32_t n   = max(m_tile_count, 1u);
        const float tile_w = max(mapping.extent_x / static_cast<float>(n), 0.001f);
        const float tile_d = max(mapping.extent_z / static_cast<float>(n), 0.001f);

        // tiles split on whole cells so their edges can sit up to a cell away from the even split, pad by one cell
        const float pad_x = max(mapping.scale_x, 0.0f);
        const float pad_z = max(mapping.scale_z, 0.0f);
        const int tx0 = max(static_cast<int>(floorf((local_min_x - pad_x + mapping.offset_x) / tile_w)), 0);
        const int tz0 = max(static_cast<int>(floorf((local_min_z - pad_z + mapping.offset_z) / tile_d)), 0);
        const int tx1 = min(static_cast<int>(floorf((local_max_x + pad_x + mapping.offset_x) / tile_w)), static_cast<int>(n) - 1);
        const int tz1 = min(static_cast<int>(floorf((local_max_z + pad_z + mapping.offset_z) / tile_d)), static_cast<int>(n) - 1);

        for (int tz = tz0; tz <= tz1; tz++)
        {
            for (int tx = tx0; tx <= tx1; tx++)
            {
                tiles_out.insert(static_cast<uint32_t>(tz) * n + static_cast<uint32_t>(tx));
            }
        }
    }

    void Terrain::CollectTilesInWorldRect(
        float world_min_x,
        float world_min_z,
        float world_max_x,
        float world_max_z,
        unordered_set<uint32_t>& tiles_out
    ) const
    {
        float local_min_x = world_min_x;
        float local_min_z = world_min_z;
        float local_max_x = world_max_x;
        float local_max_z = world_max_z;
        if (Entity* entity = GetEntity())
        {
            const Matrix inverse = entity->GetMatrix().Inverted();
            const Vector3 a = inverse * Vector3(world_min_x, 0.0f, world_min_z);
            const Vector3 b = inverse * Vector3(world_max_x, 0.0f, world_min_z);
            const Vector3 c = inverse * Vector3(world_min_x, 0.0f, world_max_z);
            const Vector3 d = inverse * Vector3(world_max_x, 0.0f, world_max_z);
            local_min_x = min(min(a.x, b.x), min(c.x, d.x));
            local_max_x = max(max(a.x, b.x), max(c.x, d.x));
            local_min_z = min(min(a.z, b.z), min(c.z, d.z));
            local_max_z = max(max(a.z, b.z), max(c.z, d.z));
        }

        CollectTilesInRegion(local_min_x, local_min_z, local_max_x, local_max_z, tiles_out);
    }

    void Terrain::PatchTilesInRegion(float local_min_x, float local_min_z, float local_max_x, float local_max_z)
    {
        if (!m_mesh || m_tile_offsets.empty() || !HasHeightfield())
        {
            return;
        }

        const TerrainGridMapping mapping = GetGridMapping();
        unordered_set<uint32_t> tiles;
        CollectTilesInRegion(local_min_x, local_min_z, local_max_x, local_max_z, tiles);

        vector<RHI_Vertex_PosTexNorTan>& verts = m_mesh->GetVertices();
        unordered_set<uint32_t> touched;

        for (uint32_t tile_index : tiles)
        {
            if (tile_index >= m_mesh->GetSubMeshCount() || tile_index >= m_tile_offsets.size())
            {
                continue;
            }

            const Vector3& offset = m_tile_offsets[tile_index];
            const SubMesh& sub    = m_mesh->GetSubMesh(tile_index);
            bool tile_changed     = false;
            for (const MeshLod& lod : sub.lods)
            {
                if (lod.vertex_count == 0)
                {
                    continue;
                }

                // only the span that actually moved goes to the gpu, a house sized pad on a big tile is a sliver of it
                uint32_t first = lod.vertex_count;
                uint32_t last  = 0;
                for (uint32_t i = 0; i < lod.vertex_count; i++)
                {
                    RHI_Vertex_PosTexNorTan& vertex = verts[lod.vertex_offset + i];
                    const float lx = vertex.pos[0] + offset.x;
                    const float lz = vertex.pos[2] + offset.z;
                    if (lx < local_min_x || lx > local_max_x || lz < local_min_z || lz > local_max_z)
                    {
                        continue;
                    }

                    vertex.pos[1] = TerrainSystem::SampleHeight(m_positions, m_dense_width, m_dense_height, lx, lz, mapping);
                    vertex.set_normal(TerrainSystem::SampleNormal(m_positions, m_dense_width, m_dense_height, lx, lz, mapping));
                    first = min(first, i);
                    last  = max(last, i + 1);
                }

                if (last > first)
                {
                    m_mesh->UploadVertexRange(lod.vertex_offset + first, last - first);
                    tile_changed = true;
                }
            }

            if (tile_changed)
            {
                m_mesh->RefreshLodBounds(tile_index);
                touched.insert(tile_index);
            }
        }

        if (!m_entity_ptr)
        {
            return;
        }

        for (Entity* child : m_entity_ptr->GetChildren())
        {
            const int index = ParseTileIndex(child);
            if (index < 0 || touched.find(static_cast<uint32_t>(index)) == touched.end())
            {
                continue;
            }

            if (Render* render = child->GetComponent<Render>())
            {
                render->SetMesh(m_mesh.get(), static_cast<uint32_t>(index));
            }
        }
    }

    void Terrain::RebuildPhysicsInRegion(float local_min_x, float local_min_z, float local_max_x, float local_max_z)
    {
        if (!m_entity_ptr)
        {
            return;
        }

        unordered_set<uint32_t> touched;
        CollectTilesInRegion(local_min_x, local_min_z, local_max_x, local_max_z, touched);

        for (Entity* child : m_entity_ptr->GetChildren())
        {
            const int index = ParseTileIndex(child);
            if (index < 0 || touched.find(static_cast<uint32_t>(index)) == touched.end())
            {
                continue;
            }

            if (Physics* physics = child->GetComponent<Physics>())
            {
                physics->Rebuild();
            }
        }
    }

    void Terrain::MarkHeightsDirty(int32_t x0, int32_t z0, int32_t x1, int32_t z1)
    {
        if (!HasHeightfield())
        {
            return;
        }

        x0 = clamp(x0, 0, static_cast<int32_t>(m_dense_width) - 1);
        x1 = clamp(x1, 0, static_cast<int32_t>(m_dense_width) - 1);
        z0 = clamp(z0, 0, static_cast<int32_t>(m_dense_height) - 1);
        z1 = clamp(z1, 0, static_cast<int32_t>(m_dense_height) - 1);
        m_height_dirty.Merge(x0, z0, x1, z1);
    }

    void Terrain::MarkHeightsDirtyLocal(float local_min_x, float local_min_z, float local_max_x, float local_max_z)
    {
        if (!HasHeightfield())
        {
            return;
        }

        const TerrainGridMapping mapping = GetGridMapping();
        const float step_x = max(mapping.scale_x, 0.001f);
        const float step_z = max(mapping.scale_z, 0.001f);
        MarkHeightsDirty(
            static_cast<int32_t>(floorf((local_min_x + mapping.offset_x) / step_x)),
            static_cast<int32_t>(floorf((local_min_z + mapping.offset_z) / step_z)),
            static_cast<int32_t>(ceilf((local_max_x + mapping.offset_x) / step_x)),
            static_cast<int32_t>(ceilf((local_max_z + mapping.offset_z) / step_z))
        );
    }

    bool Terrain::FlushHeightEdits(bool commit)
    {
        SP_PROFILE_CPU();
        if (!HasHeightfield())
        {
            m_height_dirty.Clear();
            return false;
        }

        bool texture_recreated = false;
        if (!m_height_dirty.IsEmpty())
        {
            // one ring of cells around the edit so the normals on the seam are recomputed too
            TerrainDirtyRect rect = m_height_dirty;
            rect.x0 = max(rect.x0 - 2, 0);
            rect.z0 = max(rect.z0 - 2, 0);
            rect.x1 = min(rect.x1 + 2, static_cast<int32_t>(m_dense_width) - 1);
            rect.z1 = min(rect.z1 + 2, static_cast<int32_t>(m_dense_height) - 1);
            m_height_dirty.Clear();

            // keep the flat height mirror in step, rows only
            if (m_height_data.size() == m_positions.size())
            {
                for (int32_t z = rect.z0; z <= rect.z1; z++)
                {
                    const size_t row = static_cast<size_t>(z) * m_dense_width;
                    for (int32_t x = rect.x0; x <= rect.x1; x++)
                    {
                        m_height_data[row + x] = m_positions[row + x].y;
                    }
                }
            }

            const TerrainGridMapping mapping = GetGridMapping();
            const float min_x = static_cast<float>(rect.x0) * mapping.scale_x - mapping.offset_x;
            const float max_x = static_cast<float>(rect.x1) * mapping.scale_x - mapping.offset_x;
            const float min_z = static_cast<float>(rect.z0) * mapping.scale_z - mapping.offset_z;
            const float max_z = static_cast<float>(rect.z1) * mapping.scale_z - mapping.offset_z;

            PatchTilesInRegion(min_x, min_z, max_x, max_z);
            texture_recreated = UploadHeightRegion(rect);

            // collision and the biome mask wait for the commit flush
            m_physics_dirty.Merge(rect.x0, rect.z0, rect.x1, rect.z1);
            m_prop_mask_bake_dirty.Merge(rect.x0, rect.z0, rect.x1, rect.z1);
        }

        if (commit)
        {
            FlushPendingPhysics();
            if (FlushPendingPropMask())
            {
                texture_recreated = true;
            }
        }

        if (texture_recreated)
        {
            PushToRenderer();
            WorldHelpers::RefreshTerrainGpuScatter(this);
        }

        return texture_recreated;
    }

    void Terrain::FlushPendingPhysics()
    {
        if (m_physics_dirty.IsEmpty() || !HasHeightfield())
        {
            m_physics_dirty.Clear();
            return;
        }

        const TerrainGridMapping mapping = GetGridMapping();
        const float min_x = static_cast<float>(m_physics_dirty.x0) * mapping.scale_x - mapping.offset_x;
        const float max_x = static_cast<float>(m_physics_dirty.x1) * mapping.scale_x - mapping.offset_x;
        const float min_z = static_cast<float>(m_physics_dirty.z0) * mapping.scale_z - mapping.offset_z;
        const float max_z = static_cast<float>(m_physics_dirty.z1) * mapping.scale_z - mapping.offset_z;
        m_physics_dirty.Clear();

        RebuildPhysicsInRegion(min_x, min_z, max_x, max_z);
    }

    void Terrain::FlushPendingProps()
    {
        if (m_props_dirty.IsEmpty() || !HasHeightfield())
        {
            m_props_dirty.Clear();
            return;
        }

        const TerrainGridMapping mapping = GetGridMapping();
        const float min_x = static_cast<float>(m_props_dirty.x0) * mapping.scale_x - mapping.offset_x;
        const float max_x = static_cast<float>(m_props_dirty.x1) * mapping.scale_x - mapping.offset_x;
        const float min_z = static_cast<float>(m_props_dirty.z0) * mapping.scale_z - mapping.offset_z;
        const float max_z = static_cast<float>(m_props_dirty.z1) * mapping.scale_z - mapping.offset_z;
        m_props_dirty.Clear();

        if (m_is_generating.load())
        {
            return;
        }

        unordered_set<uint32_t> touched;
        CollectTilesInRegion(min_x, min_z, max_x, max_z, touched);
        if (touched.empty())
        {
            return;
        }

        vector<uint32_t> tiles(touched.begin(), touched.end());
        sort(tiles.begin(), tiles.end());

        // no props on this terrain yet, a full populate is the editor's call, not the brush's, the
        // placement triangles still follow the edit so that populate lands on the sculpted ground
        if (!m_spawn_biome_props || (m_prop_instance_seed.empty() && m_prop_entity_seed.empty()))
        {
            RefreshPlacementData(tiles);
            return;
        }

        WorldHelpers::RepopulateTerrainProps(this, tiles);
    }

    void Terrain::RefreshPlacementData(const vector<uint32_t>& tile_indices)
    {
        if (!m_mesh)
        {
            return;
        }

        // lod 0 of a tile sub mesh is the tile's own triangles, tile local like the generate time
        // data, and PatchTilesInRegion has already written the new heights into it
        const vector<RHI_Vertex_PosTexNorTan>& vertices = m_mesh->GetVertices();
        const vector<uint32_t>& indices                 = m_mesh->GetIndices();

        for (uint32_t tile_index : tile_indices)
        {
            if (tile_index >= m_mesh->GetSubMeshCount())
            {
                continue;
            }

            const SubMesh& sub = m_mesh->GetSubMesh(tile_index);
            if (sub.lods.empty())
            {
                continue;
            }

            const MeshLod& lod = sub.lods[0];
            if (lod.index_count < 3 || lod.vertex_count == 0)
            {
                continue;
            }

            vector<vector<RHI_Vertex_PosTexNorTan>> tile_vertices(1);
            vector<vector<uint32_t>> tile_indices(1);
            tile_vertices[0].assign(
                vertices.begin() + lod.vertex_offset,
                vertices.begin() + lod.vertex_offset + lod.vertex_count
            );
            tile_indices[0].assign(
                indices.begin() + lod.index_offset,
                indices.begin() + lod.index_offset + lod.index_count
            );

            unordered_map<uint64_t, vector<TriangleData>> refreshed;
            placement::compute_triangle_data(tile_vertices, tile_indices, 0, refreshed);
            m_triangle_data[tile_index] = move(refreshed[0]);
        }
    }

    void Terrain::EnsurePlacementData(const vector<uint32_t>& tile_indices)
    {
        // serial, the scatter jobs read the map from many threads right after this
        vector<uint32_t> missing;
        for (uint32_t tile_index : tile_indices)
        {
            if (m_triangle_data.find(tile_index) == m_triangle_data.end())
            {
                missing.push_back(tile_index);
            }
        }

        if (!missing.empty())
        {
            RefreshPlacementData(missing);
        }
    }

    void Terrain::EnsureSculptGrid()
    {
        if (!HasHeightfield())
        {
            return;
        }

        // lattice cell (x, z) is dense cell (x, z), see FlushHeightEdits for the position formula
        const TerrainGridMapping mapping = GetGridMapping();
        if (!m_sculpt.MatchesGrid(-mapping.offset_x, -mapping.offset_z, mapping.scale_x, mapping.scale_z)) m_sculpt_snapshot.reset();
        m_sculpt.SetGrid(-mapping.offset_x, -mapping.offset_z, mapping.scale_x, mapping.scale_z);
    }

    bool Terrain::ApplySculptLayer()
    {
        if (m_sculpt.IsEmpty() || !HasHeightfield())
        {
            return false;
        }

        EnsureSculptGrid();

        float min_x = 0.0f;
        float min_z = 0.0f;
        float max_x = 0.0f;
        float max_z = 0.0f;
        if (!m_sculpt.GetBounds(min_x, min_z, max_x, max_z))
        {
            return false;
        }

        const TerrainGridMapping mapping = GetGridMapping();
        const int32_t x0 = clamp(static_cast<int32_t>(floorf((min_x + mapping.offset_x) / mapping.scale_x)), 0, static_cast<int32_t>(m_dense_width) - 1);
        const int32_t z0 = clamp(static_cast<int32_t>(floorf((min_z + mapping.offset_z) / mapping.scale_z)), 0, static_cast<int32_t>(m_dense_height) - 1);
        const int32_t x1 = clamp(static_cast<int32_t>(ceilf((max_x + mapping.offset_x) / mapping.scale_x)), 0, static_cast<int32_t>(m_dense_width) - 1);
        const int32_t z1 = clamp(static_cast<int32_t>(ceilf((max_z + mapping.offset_z) / mapping.scale_z)), 0, static_cast<int32_t>(m_dense_height) - 1);

        const bool sync_heights = m_height_data.size() == m_positions.size();
        bool changed            = false;
        for (int32_t z = z0; z <= z1; z++)
        {
            const size_t row = static_cast<size_t>(z) * m_dense_width;
            for (int32_t x = x0; x <= x1; x++)
            {
                const float delta = m_sculpt.GetCell(x, z);
                if (delta == 0.0f)
                {
                    continue;
                }

                const size_t index = row + static_cast<size_t>(x);
                m_positions[index].y += delta;
                if (sync_heights)
                {
                    m_height_data[index] += delta;
                }
                changed = true;
            }
        }

        if (changed)
        {
            SP_LOG_INFO("applied sculpt layer, %zu tiles", m_sculpt.GetTileCount());
        }
        return changed;
    }

    void Terrain::ClearSculptInRect(float min_x, float min_z, float max_x, float max_z)
    {
        if (!HasHeightfield() || m_sculpt.IsEmpty())
        {
            return;
        }

        EnsureSculptGrid();

        const TerrainGridMapping mapping = GetGridMapping();
        const int32_t x0 = clamp(static_cast<int32_t>(floorf((min_x + mapping.offset_x) / mapping.scale_x)), 0, static_cast<int32_t>(m_dense_width) - 1);
        const int32_t z0 = clamp(static_cast<int32_t>(floorf((min_z + mapping.offset_z) / mapping.scale_z)), 0, static_cast<int32_t>(m_dense_height) - 1);
        const int32_t x1 = clamp(static_cast<int32_t>(ceilf((max_x + mapping.offset_x) / mapping.scale_x)), 0, static_cast<int32_t>(m_dense_width) - 1);
        const int32_t z1 = clamp(static_cast<int32_t>(ceilf((max_z + mapping.offset_z) / mapping.scale_z)), 0, static_cast<int32_t>(m_dense_height) - 1);
        if (x1 < x0 || z1 < z0)
        {
            return;
        }

        // the seed is ground plus sculpt, take the sculpt back out of both
        const bool seed_ok = m_positions_seed.size() == m_positions.size();
        bool changed       = false;
        for (int32_t z = z0; z <= z1; z++)
        {
            const size_t row = static_cast<size_t>(z) * m_dense_width;
            for (int32_t x = x0; x <= x1; x++)
            {
                const float delta = m_sculpt.GetCell(x, z);
                if (delta == 0.0f)
                {
                    continue;
                }

                const size_t index = row + static_cast<size_t>(x);
                m_positions[index].y -= delta;
                if (seed_ok)
                {
                    m_positions_seed[index].y -= delta;
                }
                changed = true;
            }
        }

        const float rect_min_x = static_cast<float>(x0) * mapping.scale_x - mapping.offset_x;
        const float rect_min_z = static_cast<float>(z0) * mapping.scale_z - mapping.offset_z;
        const float rect_max_x = static_cast<float>(x1) * mapping.scale_x - mapping.offset_x;
        const float rect_max_z = static_cast<float>(z1) * mapping.scale_z - mapping.offset_z;
        m_sculpt_snapshot.reset();
        m_sculpt.ClearRect(rect_min_x, rect_min_z, rect_max_x, rect_max_z);
        if (!changed)
        {
            return;
        }

        // pads that overlap the rect were flattened over the sculpt, repaint them from the fresh seed
        // the rect is local, the pads are world, compare in world space
        float world_min_x = rect_min_x;
        float world_min_z = rect_min_z;
        float world_max_x = rect_max_x;
        float world_max_z = rect_max_z;
        if (Entity* entity = GetEntity())
        {
            const Matrix& matrix = entity->GetMatrix();
            const Vector3 corners[4] = {
                matrix * Vector3(rect_min_x, 0.0f, rect_min_z),
                matrix * Vector3(rect_max_x, 0.0f, rect_min_z),
                matrix * Vector3(rect_min_x, 0.0f, rect_max_z),
                matrix * Vector3(rect_max_x, 0.0f, rect_max_z)
            };
            world_min_x = world_max_x = corners[0].x;
            world_min_z = world_max_z = corners[0].z;
            for (const Vector3& corner : corners)
            {
                world_min_x = min(world_min_x, corner.x);
                world_max_x = max(world_max_x, corner.x);
                world_min_z = min(world_min_z, corner.z);
                world_max_z = max(world_max_z, corner.z);
            }
        }

        auto repaint = [&](const TerrainPlatform& pad)
        {
            float pad_min_x = 0.0f;
            float pad_min_z = 0.0f;
            float pad_max_x = 0.0f;
            float pad_max_z = 0.0f;
            obb_write_aabb(pad.center_x, pad.center_z, pad.half_x + pad.margin, pad.half_z + pad.margin, pad.yaw, pad_min_x, pad_min_z, pad_max_x, pad_max_z);
            if (pad_max_x < world_min_x || pad_min_x > world_max_x || pad_max_z < world_min_z || pad_min_z > world_max_z)
            {
                return;
            }

            PaintPadFromSeed(pad.center_x, pad.center_z, pad.half_x, pad.half_z, pad.yaw, pad.height, pad.margin, false);
        };
        for (const TerrainPlatform& pad : m_platforms)
        {
            repaint(pad);
        }
        if (m_live_pad_active)
        {
            repaint(m_live_pad);
        }

        MarkHeightsDirty(x0, z0, x1, z1);
        FlushHeightEdits(true);
    }

    void Terrain::ClearSculptLayer()
    {
        m_sculpt_snapshot.reset();
        float min_x = 0.0f;
        float min_z = 0.0f;
        float max_x = 0.0f;
        float max_z = 0.0f;
        if (HasHeightfield() && m_sculpt.GetBounds(min_x, min_z, max_x, max_z))
        {
            ClearSculptInRect(min_x, min_z, max_x, max_z);
        }

        m_sculpt.Clear();
    }

    std::shared_ptr<const TerrainSculptLayer> Terrain::GetSculptSnapshot() const
    {
        if (!m_sculpt_snapshot) m_sculpt_snapshot = std::make_shared<TerrainSculptLayer>(m_sculpt);
        return m_sculpt_snapshot;
    }

    void Terrain::RestoreSculptLayer(const TerrainSculptLayer& layer)
    {
        m_sculpt_snapshot.reset();
        if (!HasHeightfield()) { m_sculpt = layer; return; }
        float min_x = 0, min_z = 0, max_x = 0, max_z = 0;
        float next_min_x = 0, next_min_z = 0, next_max_x = 0, next_max_z = 0;
        const bool previous = m_sculpt.GetBounds(min_x, min_z, max_x, max_z);
        const bool next = layer.GetBounds(next_min_x, next_min_z, next_max_x, next_max_z);
        if (!previous && !next) { m_sculpt = layer; return; }
        if (!previous) { min_x = next_min_x; min_z = next_min_z; max_x = next_max_x; max_z = next_max_z; }
        else if (next)
        {
            min_x = min(min_x, next_min_x); min_z = min(min_z, next_min_z);
            max_x = max(max_x, next_max_x); max_z = max(max_z, next_max_z);
        }
        const auto mapping = GetGridMapping();
        const int32_t x0 = clamp(static_cast<int32_t>(floorf((min_x + mapping.offset_x) / mapping.scale_x)), 0, static_cast<int32_t>(m_dense_width) - 1);
        const int32_t z0 = clamp(static_cast<int32_t>(floorf((min_z + mapping.offset_z) / mapping.scale_z)), 0, static_cast<int32_t>(m_dense_height) - 1);
        const int32_t x1 = clamp(static_cast<int32_t>(ceilf((max_x + mapping.offset_x) / mapping.scale_x)), 0, static_cast<int32_t>(m_dense_width) - 1);
        const int32_t z1 = clamp(static_cast<int32_t>(ceilf((max_z + mapping.offset_z) / mapping.scale_z)), 0, static_cast<int32_t>(m_dense_height) - 1);
        const bool seed_ok = m_positions_seed.size() == m_positions.size();
        for (int32_t z = z0; z <= z1; ++z)
            for (int32_t x = x0; x <= x1; ++x)
            {
                const float delta = layer.GetCell(x, z) - m_sculpt.GetCell(x, z);
                if (delta == 0.0f) continue;
                const size_t index = static_cast<size_t>(z) * m_dense_width + x;
                if (seed_ok) { m_positions_seed[index].y += delta; m_positions[index].y = m_positions_seed[index].y; }
                else m_positions[index].y += delta;
            }
        m_sculpt = layer;
        for (const auto& pad : m_platforms)
            PaintPadFromSeed(pad.center_x, pad.center_z, pad.half_x, pad.half_z, pad.yaw, pad.height, pad.margin, false);
        if (m_live_pad_active)
            PaintPadFromSeed(m_live_pad.center_x, m_live_pad.center_z, m_live_pad.half_x, m_live_pad.half_z, m_live_pad.yaw, m_live_pad.height, m_live_pad.margin, false);
        MarkHeightsDirty(x0, z0, x1, z1);
        MarkSplineHeightCarvesDirty();
        FlushHeightEdits(true);
        FlushPendingProps();
    }

    void Terrain::SaveSculptLayer(const string& directory) const
    {
        CreateSculptSaveTask(directory)();
    }

    function<void()> Terrain::CreateSculptSaveTask(const string& directory) const
    {
        string path = directory;
        replace(path.begin(), path.end(), '\\', '/');
        if (!path.empty() && path.back() != '/')
        {
            path += '/';
        }
        path += terrain_sculpt_file_name;

        return [path, m_sculpt = m_sculpt]
        {
            // an empty layer removes the file so a cleared sculpt does not come back on the next load
            if (m_sculpt.IsEmpty())
            {
                if (FileSystem::Exists(path))
                {
                    if (!FileSystem::Delete(path)) throw runtime_error("Failed to clear sculpt: " + path);
                }
                return;
            }

            if (!m_sculpt.SaveToFile(path))
            {
                throw runtime_error("Failed to save sculpt: " + path);
            }

            SP_LOG_INFO("saved sculpt layer: %zu tiles, %.1f kb", m_sculpt.GetTileCount(), static_cast<float>(m_sculpt.GetByteCount()) / 1024.0f);
        };
    }

    void Terrain::RefreshSplinePropCarves()
    {
        // The biome map is far too coarse to represent a narrow road. Build a
        // spatial index of the rendered footprint, shared by CPU and GPU scatter.
        constexpr uint32_t grid_size = 1024;
        const Vector4 mapping = GetMappingWorld();
        if (mapping.z <= 0.0f || mapping.w <= 0.0f) return;
        const Stopwatch timer;
        generated_cache::Hash hash;
        hash.Add(uint32_t(1)); hash.Add(grid_size); hash.Add(mapping);
        vector<Entity*> roads;
        for (Entity* entity : World::GetEntities())
        {
            Spline* spline = entity ? entity->GetComponent<Spline>() : nullptr;
            if (!spline || !entity->GetActive() || !spline->GetMeshEnabled() || spline->GetProfile() != SplineProfile::Road) continue;
            for (Entity* part : {entity, entity->GetChildByName("spline_sidewalk"), entity->GetChildByName("spline_road_shoulder")})
            {
                if (!part || !part->GetActive()) continue;
                Render* render = part->GetComponent<Render>();
                Mesh* mesh = render ? render->GetMesh() : nullptr;
                if (!mesh || render->GetSubMeshIndex() >= mesh->GetSubMeshCount()) continue;
                const SubMesh& sub = mesh->GetSubMesh(render->GetSubMeshIndex());
                if (sub.lods.empty()) continue;
                const MeshLod& lod = sub.lods[0];
                hash.Add(part->GetMatrix());
                hash.Add(lod.vertex_count); hash.Add(lod.index_count);
                hash.Bytes(mesh->GetVertices().data() + lod.vertex_offset, size_t(lod.vertex_count) * sizeof(RHI_Vertex_PosTexNorTan));
                hash.Bytes(mesh->GetIndices().data() + lod.index_offset, size_t(lod.index_count) * sizeof(uint32_t));
                roads.push_back(part);
            }
        }
        const auto cache_path = generated_cache::Path(World::GetResourceDirectory(), "road_exclusions", hash.value);
        vector<Vector4> cached;
        bool hit = generated_cache::Load(cache_path, hash.value, cached) && cached.size() >= 2 + grid_size * grid_size &&
            cached[0] == mapping && cached[1].x == grid_size && cached[1].y == grid_size;
        for (uint32_t i = 0; hit && i < grid_size * grid_size; ++i)
        {
            const Vector4& range = cached[2 + i];
            hit = std::isfinite(range.x) && std::isfinite(range.y) && range.x >= 2 + grid_size * grid_size &&
                range.y >= 0 && double(range.x) + double(range.y) * 2 <= double(cached.size());
        }
        if (hit)
        {
            m_road_exclusions = move(cached);
        }
        else
        {
            vector<vector<Vector4>> cells(grid_size * grid_size);
            auto cell_x = [&](float x) { return clamp(static_cast<int>((x - mapping.x) * mapping.z * grid_size), 0, static_cast<int>(grid_size) - 1); };
            auto cell_z = [&](float z) { return clamp(static_cast<int>((z - mapping.y) * mapping.w * grid_size), 0, static_cast<int>(grid_size) - 1); };
            auto add_render = [&](Entity* entity)
            {
                if (!entity || !entity->GetActive()) return;
                Render* render = entity->GetComponent<Render>();
                Mesh* mesh = render ? render->GetMesh() : nullptr;
                if (!mesh || render->GetSubMeshIndex() >= mesh->GetSubMeshCount()) return;
                const SubMesh& sub = mesh->GetSubMesh(render->GetSubMeshIndex());
                if (sub.lods.empty()) return;
                const MeshLod& lod = sub.lods[0];
                const auto& vertices = mesh->GetVertices();
                const auto& indices = mesh->GetIndices();
                const Matrix& world = entity->GetMatrix();
                for (uint32_t i = 0; i + 2 < lod.index_count; i += 3)
                {
                    const Vector3 a = world * vertices[lod.vertex_offset + indices[lod.index_offset + i]].get_position();
                    const Vector3 b = world * vertices[lod.vertex_offset + indices[lod.index_offset + i + 1]].get_position();
                    const Vector3 c = world * vertices[lod.vertex_offset + indices[lod.index_offset + i + 2]].get_position();
                    const float area = (b.x - a.x) * (c.z - a.z) - (b.z - a.z) * (c.x - a.x);
                    if (fabsf(area) < 1e-6f) continue; // vertical curb faces have no footprint
                    const int x0 = cell_x(min(a.x, min(b.x, c.x))), x1 = cell_x(max(a.x, max(b.x, c.x)));
                    const int z0 = cell_z(min(a.z, min(b.z, c.z))), z1 = cell_z(max(a.z, max(b.z, c.z)));
                    for (int z = z0; z <= z1; ++z)
                    for (int x = x0; x <= x1; ++x)
                    {
                        auto& cell = cells[z * grid_size + x];
                        cell.emplace_back(a.x, a.z, b.x, b.z);
                        cell.emplace_back(c.x, c.z, 0.0f, 0.0f);
                    }
                }
            };
            for (Entity* entity : roads) add_render(entity);
            m_road_exclusions.assign(2 + grid_size * grid_size, Vector4::Zero);
            m_road_exclusions[0] = mapping;
            m_road_exclusions[1] = Vector4(static_cast<float>(grid_size), static_cast<float>(grid_size), 0.0f, 0.0f);
            for (uint32_t i = 0; i < cells.size(); ++i)
            {
                m_road_exclusions[2 + i] = Vector4(static_cast<float>(m_road_exclusions.size()), static_cast<float>(cells[i].size() / 2), 0.0f, 0.0f);
                m_road_exclusions.insert(m_road_exclusions.end(), cells[i].begin(), cells[i].end());
            }
            generated_cache::Save(cache_path, hash.value, m_road_exclusions);
        }
        SP_LOG_INFO("Road exclusion bake: %s, %.2f ms", hit ? "hit" : "miss (missing/stale/invalid)", timer.GetElapsedTimeMs());
        m_road_exclusion_buffer_dirty = true;
        // Seeds retain the original instances, so removing/narrowing a road restores
        // nearby vegetation instead of making each rebuild progressively emptier.
        ApplySplineCarveToProps(mapping.x, mapping.y, mapping.x + 1.0f / mapping.z, mapping.y + 1.0f / mapping.w);
    }

    bool Terrain::IsOnRoad(float world_x, float world_z) const
    {
        if (m_road_exclusions.size() < 2) return false;
        const Vector4& mapping = m_road_exclusions[0];
        const float u = (world_x - mapping.x) * mapping.z, v = (world_z - mapping.y) * mapping.w;
        if (u < 0.0f || v < 0.0f || u > 1.0f || v > 1.0f) return false;
        const uint32_t width = static_cast<uint32_t>(m_road_exclusions[1].x);
        const uint32_t height = static_cast<uint32_t>(m_road_exclusions[1].y);
        const uint32_t x = min(static_cast<uint32_t>(u * width), width - 1);
        const uint32_t z = min(static_cast<uint32_t>(v * height), height - 1);
        const Vector4& range = m_road_exclusions[2 + z * width + x];
        for (uint32_t i = 0; i < static_cast<uint32_t>(range.y); ++i)
        {
            const Vector4& ab = m_road_exclusions[static_cast<uint32_t>(range.x) + i * 2];
            const Vector4& c = m_road_exclusions[static_cast<uint32_t>(range.x) + i * 2 + 1];
            const float e0 = (ab.z - ab.x) * (world_z - ab.y) - (ab.w - ab.y) * (world_x - ab.x);
            const float e1 = (c.x - ab.z) * (world_z - ab.w) - (c.y - ab.w) * (world_x - ab.z);
            const float e2 = (ab.x - c.x) * (world_z - c.y) - (ab.y - c.y) * (world_x - c.x);
            if ((e0 >= -1e-4f && e1 >= -1e-4f && e2 >= -1e-4f) || (e0 <= 1e-4f && e1 <= 1e-4f && e2 <= 1e-4f)) return true;
        }
        return false;
    }

    RHI_Buffer* Terrain::GetRoadExclusionBuffer()
    {
        if (m_road_exclusion_buffer_dirty || !m_road_exclusion_buffer)
        {
            const Vector4 empty[2] = {};
            m_road_exclusion_buffer = make_shared<RHI_Buffer>(RHI_Buffer_Type::Storage, sizeof(Vector4),
                static_cast<uint32_t>(max(m_road_exclusions.size(), size_t(2))),
                nullptr, false, "terrain_road_exclusions");
            RHI_CommandList::UpdateBuffer(m_road_exclusion_buffer.get(), 0,
                static_cast<uint32_t>(max(m_road_exclusions.size(), size_t(2)) * sizeof(Vector4)),
                m_road_exclusions.empty() ? empty : m_road_exclusions.data(), false);
            m_road_exclusion_buffer_dirty = false;
        }
        return m_road_exclusion_buffer.get();
    }

    void Terrain::ApplySplineCarveToProps(float min_x, float min_z, float max_x, float max_z)
    {
        if (!m_entity_ptr)
        {
            return;
        }

        if (m_prop_instance_seed.empty() && m_prop_entity_seed.empty())
        {
            SnapshotPropInstances();
        }

        if (m_map_width < 2 || m_map_height < 2)
        {
            return;
        }

        auto on_road = [&](float world_x, float world_z) { return IsOnRoad(world_x, world_z); };

        // Cache footprint rotations once, rather than evaluating sin/cos for
        // every instance against every pad in the world.
        struct Footprint { float x, z, half_x, half_z, c, s; };
        vector<Footprint> footprints;
        auto add_footprint = [&](const TerrainPlatform& pad)
        {
            footprints.push_back({pad.center_x, pad.center_z, pad.half_x, pad.half_z, cosf(pad.yaw), sinf(pad.yaw)});
        };
        if (m_live_pad_active) add_footprint(m_live_pad);
        for (const TerrainPlatform& pad : m_platforms) add_footprint(pad);
        auto on_pad = [&](float world_x, float world_z) -> bool
        {
            for (const Footprint& pad : footprints)
            {
                const float dx = world_x - pad.x, dz = world_z - pad.z;
                const float lx = dx * pad.c + dz * pad.s;
                const float lz = -dx * pad.s + dz * pad.c;
                const float ox = max(fabsf(lx) - pad.half_x, 0.0f);
                const float oz = max(fabsf(lz) - pad.half_z, 0.0f);
                if (sqrtf(ox * ox + oz * oz) <= 0.0f) return true;
            }
            return false;
        };

        float local_min_x = min_x;
        float local_min_z = min_z;
        float local_max_x = max_x;
        float local_max_z = max_z;
        if (Entity* entity = GetEntity())
        {
            const Matrix inverse = entity->GetMatrix().Inverted();
            const Vector3 corners[4] =
            {
                inverse * Vector3(min_x, 0.0f, min_z),
                inverse * Vector3(max_x, 0.0f, min_z),
                inverse * Vector3(min_x, 0.0f, max_z),
                inverse * Vector3(max_x, 0.0f, max_z)
            };

            local_min_x = local_max_x = corners[0].x;
            local_min_z = local_max_z = corners[0].z;
            for (int i = 1; i < 4; i++)
            {
                local_min_x = min(local_min_x, corners[i].x);
                local_max_x = max(local_max_x, corners[i].x);
                local_min_z = min(local_min_z, corners[i].z);
                local_max_z = max(local_max_z, corners[i].z);
            }
        }

        const TerrainGridMapping mapping = GetGridMapping();
        const uint32_t n = max(m_tile_count, 1u);
        const float tile_w = max(mapping.extent_x / static_cast<float>(n), 0.001f);
        const float tile_d = max(mapping.extent_z / static_cast<float>(n), 0.001f);
        const int tx0 = max(static_cast<int>(floorf((local_min_x + mapping.offset_x) / tile_w)) - 1, 0);
        const int tz0 = max(static_cast<int>(floorf((local_min_z + mapping.offset_z) / tile_d)) - 1, 0);
        const int tx1 = min(static_cast<int>(floorf((local_max_x + mapping.offset_x) / tile_w)) + 1, static_cast<int>(n) - 1);
        const int tz1 = min(static_cast<int>(floorf((local_max_z + mapping.offset_z) / tile_d)) + 1, static_cast<int>(n) - 1);
        if (tx0 > tx1 || tz0 > tz1)
        {
            return;
        }

        unordered_set<int> dirty_tiles;
        dirty_tiles.reserve(static_cast<size_t>(max(tx1 - tx0 + 1, 1) * max(tz1 - tz0 + 1, 1)));
        for (int tz = tz0; tz <= tz1; tz++)
        {
            for (int tx = tx0; tx <= tx1; tx++)
            {
                dirty_tiles.insert(tz * static_cast<int>(n) + tx);
            }
        }

        struct PropFilter
        {
            Render* render;
            Matrix world;
            const vector<Matrix>* seed;
            vector<Matrix> kept;
        };
        vector<PropFilter> filters;
        function<void(Entity*, bool)> visit = [&](Entity* entity, bool inside_prop)
        {
            if (!entity)
            {
                return;
            }

            const bool prop = inside_prop || entity->HasTag("terrain_prop");
            if (prop)
            {
                const uint64_t id = entity->GetObjectId();
                if (Render* render = entity->GetComponent<Render>())
                {
                    auto seed = m_prop_instance_seed.find(id);
                    if (seed != m_prop_instance_seed.end())
                    {
                        filters.push_back({render, entity->GetMatrix(), &seed->second, {}});
                    }
                    else if (entity->GetChildrenCount() == 0)
                    {
                        auto active = m_prop_entity_seed.find(id);
                        const bool blocked = on_road(entity->GetPosition().x, entity->GetPosition().z) ||
                                             on_pad(entity->GetPosition().x, entity->GetPosition().z);
                        if (blocked)
                        {
                            entity->SetActive(false);
                        }
                        else if (active != m_prop_entity_seed.end())
                        {
                            entity->SetActive(active->second);
                        }
                    }
                }
            }

            const uint32_t child_count = entity->GetChildrenCount();
            for (uint32_t i = 0; i < child_count; i++)
            {
                visit(entity->GetChildByIndex(i), prop);
            }
        };

        for (Entity* child : m_entity_ptr->GetChildren())
        {
            const int index = ParseTileIndex(child);
            if (index < 0 || dirty_tiles.find(index) == dirty_tiles.end())
            {
                continue;
            }

            visit(child, false);
        }

        // Scene lookup and publication stay on the main thread. Workers only
        // read immutable seeds, road footprints and captured transforms.
        const Stopwatch filter_timer;
        if (!filters.empty()) ThreadPool::ParallelLoop([&](uint32_t begin, uint32_t end)
        {
            for (uint32_t i = begin; i < end; ++i)
            {
                PropFilter& filter = filters[i];
                filter.kept.reserve(filter.seed->size());
                for (const Matrix& local : *filter.seed)
                {
                    const Vector3 position = (local * filter.world).GetTranslation();
                    if (!on_road(position.x, position.z) && !on_pad(position.x, position.z))
                        filter.kept.push_back(local);
                }
            }
        }, static_cast<uint32_t>(filters.size()));
        const float filter_ms = filter_timer.GetElapsedTimeMs();
        for (PropFilter& filter : filters) filter.render->SetInstances(filter.kept);
        SP_LOG_INFO("Vegetation road filtering: %.2f ms, publish %.2f ms", filter_ms, filter_timer.GetElapsedTimeMs() - filter_ms);

        // the seed holds the props at their original ground, a pad ring lowered or raised that ground
        // and snapped them to it, the rewrite above put the seed back so snap the rings again
        const float cell = max(max(mapping.scale_x, mapping.scale_z), 1.0f);
        for (const TerrainPlatform& pad : m_platforms)
        {
            float deform_hx = 0.0f;
            float deform_hz = 0.0f;
            pad_deform_extents(pad, cell, deform_hx, deform_hz);

            float pad_min_x = 0.0f;
            float pad_min_z = 0.0f;
            float pad_max_x = 0.0f;
            float pad_max_z = 0.0f;
            obb_write_aabb(pad.center_x, pad.center_z, deform_hx, deform_hz, pad.yaw, pad_min_x, pad_min_z, pad_max_x, pad_max_z);
            if (pad_max_x < min_x || pad_min_x > max_x || pad_max_z < min_z || pad_min_z > max_z)
            {
                continue;
            }

            SnapPropsToSurface(
                pad.center_x,
                pad.center_z,
                deform_hx,
                deform_hz,
                pad.half_x,
                pad.half_z,
                pad.yaw
            );
        }
    }

    void Terrain::RebuildMeshData(bool update_placement)
    {
        if (m_dense_width < 2 || m_dense_height < 2 || m_positions.empty())
        {
            return;
        }

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
        geometry_processing::split_grid_into_tiles(
            m_vertices,
            m_dense_width,
            m_dense_height,
            m_tile_count,
            m_tile_vertices,
            m_tile_indices,
            m_tile_offsets
        );

        if (update_placement)
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
        }
    }

    float Terrain::ResolveSeaLevelLocal() const
    {
        return GetSeaLevelLocal();
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

        // rivers traced on the previous ground, a rebuild that kept its tiles keeps these too
        for (Entity* tile : tiles)
        {
            if (!tile)
            {
                continue;
            }

            vector<Entity*> tile_children = tile->GetChildren();
            for (Entity* child : tile_children)
            {
                if (child && child->GetObjectName().rfind("river_", 0) == 0)
                {
                    World::RemoveEntity(child);
                }
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
        water_material->SetPersistent(false);
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

        // local height at the rim so world y lands at sea level
        const float edge_local = GetSeaLevelLocal();

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

    bool Terrain::UploadHeightRegion(const TerrainDirtyRect& rect)
    {
        if (!HasHeightfield() || rect.IsEmpty())
        {
            return false;
        }

        // no texture yet, or one from a different grid, the full bake is the only option
        const bool texture_fits =
            m_height_map_gpu &&
            m_height_map_gpu->GetWidth() == m_dense_width &&
            m_height_map_gpu->GetHeight() == m_dense_height &&
            m_height_map_gpu->GetRhiResource() != nullptr;
        if (!texture_fits)
        {
            BakeHeightMapTexture();
            return true;
        }

        const int32_t x0 = clamp(rect.x0, 0, static_cast<int32_t>(m_dense_width) - 1);
        const int32_t x1 = clamp(rect.x1, 0, static_cast<int32_t>(m_dense_width) - 1);
        const int32_t z0 = clamp(rect.z0, 0, static_cast<int32_t>(m_dense_height) - 1);
        const int32_t z1 = clamp(rect.z1, 0, static_cast<int32_t>(m_dense_height) - 1);
        if (x1 < x0 || z1 < z0)
        {
            return false;
        }
        const uint32_t width  = static_cast<uint32_t>(x1 - x0 + 1);
        const uint32_t height = static_cast<uint32_t>(z1 - z0 + 1);

        vector<float> heights(static_cast<size_t>(width) * height);
        for (int32_t z = z0; z <= z1; z++)
        {
            const size_t row = static_cast<size_t>(z) * m_dense_width;
            float* out       = heights.data() + static_cast<size_t>(z - z0) * width;
            for (int32_t x = x0; x <= x1; x++)
            {
                out[x - x0] = m_positions[row + x].y;
            }
        }

        if (!m_height_map_gpu->UpdateRegion(static_cast<uint32_t>(x0), static_cast<uint32_t>(z0), width, height, heights.data()))
        {
            BakeHeightMapTexture();
            return true;
        }

        // the imgui preview follows with the range of the last full bake, anything outside it saturates
        const bool preview_fits =
            m_height_map_final &&
            m_height_map_final->GetWidth() == m_dense_width &&
            m_height_map_final->GetHeight() == m_dense_height &&
            m_height_map_final->GetRhiResource() != nullptr;
        if (preview_fits)
        {
            const float range = max(m_height_bake_max - m_height_bake_min, epsilon);
            vector<uint8_t> pixels(static_cast<size_t>(width) * height * 4);
            for (size_t i = 0; i < heights.size(); i++)
            {
                const float t       = saturate((heights[i] - m_height_bake_min) / range);
                const uint8_t value = static_cast<uint8_t>(t * 255.0f + 0.5f);
                pixels[i * 4 + 0]   = value;
                pixels[i * 4 + 1]   = value;
                pixels[i * 4 + 2]   = value;
                pixels[i * 4 + 3]   = 255;
            }
            m_height_map_final->UpdateRegion(static_cast<uint32_t>(x0), static_cast<uint32_t>(z0), width, height, pixels.data());
        }

        return false;
    }

    bool Terrain::LoadTerrainMapsFromCache(uint64_t surface_hash)
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

        if (!file || stored_hash != surface_hash || width == 0 || height == 0 || width > 8192 || height > 8192)
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

    void Terrain::SaveTerrainMapsToCache(uint64_t surface_hash) const
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

        file.write(reinterpret_cast<const char*>(&surface_hash), sizeof(uint64_t));
        file.write(reinterpret_cast<const char*>(&m_map_width),  sizeof(uint32_t));
        file.write(reinterpret_cast<const char*>(&m_map_height), sizeof(uint32_t));
        file.write(reinterpret_cast<const char*>(m_map_a_pixels.data()), m_map_a_pixels.size());
        file.write(reinterpret_cast<const char*>(m_map_b_pixels.data()), m_map_b_pixels.size());
        file.write(reinterpret_cast<const char*>(m_prop_mask_pixels.data()), m_prop_mask_pixels.size());
    }

    void Terrain::BakeTerrainMaps(bool allow_cache)
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

        // Key the analysis by its actual inputs, including sculpted heights and
        // building pads. The procedural recipe alone cannot identify this surface.
        // Keep live brush updates uncached; their regional repairs own the maps.
        uint64_t surface_hash = ComputeCacheHash();
        if (allow_cache)
        {
            auto hash_bytes = [&surface_hash](const void* data, size_t size)
            {
                const uint8_t* bytes = static_cast<const uint8_t*>(data);
                for (size_t i = 0; i < size; ++i)
                {
                    surface_hash = (surface_hash ^ bytes[i]) * 1099511628211ull;
                }
            };
            const uint32_t analysis_cache_version = 1;
            const float sea_level = GetSeaLevelLocal();
            hash_bytes(&analysis_cache_version, sizeof(analysis_cache_version));
            hash_bytes(&m_dense_width, sizeof(m_dense_width));
            hash_bytes(&m_dense_height, sizeof(m_dense_height));
            hash_bytes(&sea_level, sizeof(sea_level));
            hash_bytes(m_positions.data(), m_positions.size() * sizeof(Vector3));
            const bool has_erosion = m_erosion_maps.IsValid(m_positions.size());
            hash_bytes(&has_erosion, sizeof(has_erosion));
            if (has_erosion)
            {
                hash_bytes(m_erosion_maps.wear.data(), m_erosion_maps.wear.size() * sizeof(float));
                hash_bytes(m_erosion_maps.deposition.data(), m_erosion_maps.deposition.size() * sizeof(float));
            }
        }
        if (!allow_cache || !LoadTerrainMapsFromCache(surface_hash))
        {
            TerrainAnalysisMaps analysis;
            TerrainSystem::ComputeAnalysisMaps(
                analysis,
                m_positions,
                m_dense_width,
                m_dense_height,
                GetSeaLevelLocal(),
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
            if (allow_cache)
            {
                SaveTerrainMapsToCache(surface_hash);
            }
        }
        else
        {
            // cached analysis, still rebake the mask so placement tracks the current layer rules
            BakePropMask();
            SP_LOG_INFO("Reused terrain surface analysis cache");
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
        m_prop_mask_dirty.Clear();

        m_map_a->PrepareForGpu();
        m_map_b->PrepareForGpu();
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

    void Terrain::RebuildSurface(bool update_placement, bool preview)
    {
        if (!HasHeightfield())
        {
            SP_LOG_WARNING("no heightfield to rebuild");
            return;
        }

        // a full rebuild supersedes any region still waiting on a flush
        if (!m_height_dirty.IsEmpty() && m_height_data.size() == m_positions.size())
        {
            TerrainSystem::SyncHeightDataFromPositions(m_height_data, m_positions);
        }
        m_height_dirty.Clear();
        if (!preview)
        {
            m_physics_dirty.Clear();
            m_prop_mask_bake_dirty.Clear();
        }
        if (update_placement)
        {
            m_props_dirty.Clear();
        }

        m_vertices.resize(m_dense_width * m_dense_height);
        m_indices.resize((m_dense_width - 1) * (m_dense_height - 1) * 6);
        TerrainSystem::GenerateVerticesAndIndices(m_vertices, m_indices, m_positions, m_dense_width, m_dense_height);
        TerrainSystem::GenerateNormals(m_vertices, m_dense_width, m_dense_height);

        uint32_t tile_count = max(m_tile_count, 1u);
        geometry_processing::split_grid_into_tiles(
            m_vertices,
            m_dense_width,
            m_dense_height,
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

        if (!preview)
        {
            BakeHeightMapTexture();
            // a surface rebuild means the heights are no longer the procedural ones the cache describes
            BakeTerrainMaps(false);
            ReapplyPropMaskHoles();
            UploadTerrainMaps();
        }

        m_height_samples = m_dense_width * m_dense_height;
        m_vertex_count   = static_cast<uint32_t>(m_vertices.size());
        m_index_count    = static_cast<uint32_t>(m_indices.size());
        m_triangle_count = m_index_count / 3;
        if (!preview)
        {
            m_area_km2 = TerrainSystem::ComputeSurfaceAreaKm2(m_vertices, m_indices);
        }

        vector<Entity*> existing_tiles(m_tile_offsets.size(), nullptr);
        uint32_t existing_count = 0;
        if (m_entity_ptr)
        {
            for (Entity* child : m_entity_ptr->GetChildren())
            {
                const int index = ParseTileIndex(child);
                if (index >= 0 && static_cast<uint32_t>(index) < existing_tiles.size() && !existing_tiles[index])
                {
                    existing_tiles[index] = child;
                    existing_count++;
                }
            }
        }
        const bool reuse_tiles = existing_count == existing_tiles.size() && !existing_tiles.empty();

        // drop tile draws before freeing the mesh they still reference
        DetachTileMeshes();

        ResourceCache::Remove(m_mesh);
        m_mesh = make_shared<Mesh>();
        m_mesh->SetObjectName("terrain_mesh");
        m_mesh->SetFlag(static_cast<uint32_t>(MeshFlags::PostProcessOptimize), false);
        m_mesh->SetFlag(static_cast<uint32_t>(MeshFlags::PostProcessPreserveTerrainEdges), true);

        // a committed rebuild gets the same lod chain generate builds, otherwise the terrain draws
        // at full density everywhere until the next regenerate, previews stay cheap
        BuildTileMesh(*m_mesh, !preview);
        m_mesh->CreateGpuBuffers();

        if (reuse_tiles)
        {
            for (uint32_t tile_index = 0; tile_index < existing_tiles.size(); tile_index++)
            {
                Entity* tile = existing_tiles[tile_index];
                if (!tile)
                {
                    continue;
                }

                tile->SetPositionLocal(m_tile_offsets[tile_index]);
                if (Render* render = tile->GetComponent<Render>())
                {
                    render->SetMesh(m_mesh.get(), tile_index);
                }
            }
        }
        else
        {
            ClearTileEntities();
            CreateTileEntities();
        }

        if (!preview)
        {
            RefreshPhysics();
            RefreshLayers();
        }
        PushToRenderer();
        if (!preview)
        {
            // the channels moved with the ground, retrace them whether or not the tiles were reused
            SpawnFlowRivers();
        }

        if (!preview)
        {
            DestroyPadOverlays();
            RebuildCommittedRefines();
        }

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

        worker_scope worker(m_worker_busy, m_worker_thread);

        m_height_map_seed = nullptr;
        Clear();
        ClearRoadCarve();

        TerrainSystem::CreateFlatHeightfield(
            m_height_data,
            m_positions,
            base_width,
            base_height,
            m_density,
            m_scale,
            GetSeaLevelLocal()
        );

        m_width        = base_width;
        m_height       = base_height;
        m_dense_width  = m_density * (base_width - 1) + 1;
        m_dense_height = m_density * (base_height - 1) + 1;

        ApplySculptLayer();
        SnapshotSeed();
        m_live_pad_active = false;
        m_live_pad_dirty  = false;
        m_live_pad_props_dirty = false;
        if (!ProgressTracker::IsLoading(ProgressType::World))
        {
            PruneOrphanPlatforms();
        }
        ApplyPlatformsToHeightfield();
        RebuildSurface(true);

        WorldHelpers::PopulateTerrainBiomeProps(this);

        m_is_generating = false;
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
        if (!HasHeightfield())
        {
            SP_LOG_WARNING("no heightfield, generate the terrain first");
            return false;
        }

        const uint32_t n = max(m_tile_count, 1u);
        if (tile_index >= n * n)
        {
            SP_LOG_WARNING("tile index %u out of range for %ux%u grid", tile_index, n, n);
            return false;
        }

        if (m_sculpt.IsEmpty())
        {
            return true;
        }

        const TerrainGridMapping mapping = GetGridMapping();
        const float tile_w = mapping.extent_x / static_cast<float>(n);
        const float tile_d = mapping.extent_z / static_cast<float>(n);
        const uint32_t tx  = tile_index % n;
        const uint32_t tz  = tile_index / n;
        const float x0     = -mapping.offset_x + static_cast<float>(tx) * tile_w;
        const float z0     = -mapping.offset_z + static_cast<float>(tz) * tile_d;

        // the sculpt comes out, pads repaint from the seed, roads regrade on the next tick
        ClearSculptInRect(x0, z0, x0 + tile_w, z0 + tile_d);
        if (m_road_carve_delta.size() == m_positions.size())
        {
            MarkSplineHeightCarvesDirty();
        }
        return true;
    }

    void Terrain::Clear()
    {
        m_props_population_step = {};
        if (m_props_commit_pending.exchange(false, memory_order_acq_rel))
        {
            m_is_generating.store(false, memory_order_release);
        }
        // detach and queue tile removal before freeing geometry they pointed at
        ClearTileEntities();

        m_vertices.clear();
        m_indices.clear();
        m_tile_vertices.clear();
        m_tile_indices.clear();
        m_tile_offsets.clear();
        m_triangle_data.clear();
        m_positions.clear();
        m_positions_seed.clear();
        m_height_data.clear();
        m_erosion_maps = TerrainErosionMaps();
        m_prop_mask_seed.clear();
        m_prop_instance_seed.clear();
        m_prop_entity_seed.clear();
        m_height_samples         = 0;
        m_vertex_count           = 0;
        m_index_count            = 0;
        m_triangle_count         = 0;
        m_area_km2               = 0.0f;
        m_live_pad_active    = false;
        m_live_pad_dirty     = false;
        m_live_pad_props_dirty = false;
        m_live_pad           = {};
        m_live_track_entity  = 0;
        m_height_dirty.Clear();
        m_physics_dirty.Clear();
        m_prop_mask_dirty.Clear();
        m_prop_mask_bake_dirty.Clear();
        m_props_dirty.Clear();
        DestroyPadOverlays();
        DestroyAllPadRefines();
        ResourceCache::Remove(m_mesh);
        m_mesh = nullptr;
    }
}
