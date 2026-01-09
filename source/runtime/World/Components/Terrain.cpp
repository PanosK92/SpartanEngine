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
#include "Terrain.h"
#include "Renderable.h"
#include "../Entity.h"
#include "../World.h"
#include "../../RHI/RHI_Texture.h"
#include "../../Resource/ResourceCache.h"
#include "../../Geometry/Mesh.h"
#include "../../Rendering/Material.h"
#include "../../Geometry/GeometryProcessing.h"
#include "../../Core/ThreadPool.h"
#include "../../Core/ProgressTracker.h"
//============================================

//= NAMESPACES ===============
using namespace std;
using namespace spartan::math;
//============================

namespace spartan
{
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

        void find_transforms(
            TerrainPropDescription prop_desc,
            const float density_fraction,
            uint32_t tile_index,
            vector<Matrix>& transforms_out,
            unordered_map<uint64_t, vector<TriangleData>>& triangle_data
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
            if (num_chunks == 0) num_chunks = 1;
            
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

            vector<uint32_t> acceptable_triangles;
            acceptable_triangles.reserve(tile_triangle_data.size());
            for (uint32_t i = 0; i < tile_triangle_data.size(); i++)
            {
                const TriangleData& tri = tile_triangle_data[i];

                // skip edge triangles to prevent double-spawning at tile boundaries
                if (tri.centroid.x >= edge_threshold_x || tri.centroid.z >= edge_threshold_z)
                    continue;

                if (tri.slope_radians <= prop_desc.max_slope_angle_rad &&
                    tri.height_min >= prop_desc.min_spawn_height &&
                    tri.height_max <= prop_desc.max_spawn_height)
                {
                    acceptable_triangles.push_back(i);
                }
            }
            if (acceptable_triangles.empty())
                return;

            // compute instance count based on density
            uint32_t adjusted_count = static_cast<uint32_t>(density_fraction * static_cast<float>(acceptable_triangles.size()) + 0.5f);
            transforms_out.resize(adjusted_count);
            if (adjusted_count == 0)
                return;

            // setup cluster parameters
            float safe_min_x   = tile_min_x + prop_desc.cluster_radius;
            float safe_max_x   = tile_max_x - prop_desc.cluster_radius;
            float safe_min_z   = tile_min_z + prop_desc.cluster_radius;
            float safe_max_z   = tile_max_z - prop_desc.cluster_radius;
            bool has_safe_zone = (safe_min_x < safe_max_x) && (safe_min_z < safe_max_z);

            uint32_t cluster_count              = adjusted_count;
            uint32_t base_instances_per_cluster = 1;
            uint32_t remainder_instances        = 0;
            if (prop_desc.instances_per_cluster > 1)
            {
                cluster_count              = max(1u, adjusted_count / prop_desc.instances_per_cluster);
                base_instances_per_cluster = adjusted_count / cluster_count;
                remainder_instances        = adjusted_count % cluster_count;
            }
            vector<ClusterData> clusters(cluster_count);

            // place cluster centers
            auto place_cluster = [&](uint32_t start_index, uint32_t end_index)
            {
                mt19937 generator(tile_index * 1000003u + start_index * 31u + 12345u);
                const uint32_t tri_count = static_cast<uint32_t>(acceptable_triangles.size());
                uniform_int_distribution<> triangle_dist(0, tri_count - 1);
                uniform_real_distribution<float> dist(0.0f, 1.0f);
                const uint32_t max_attempts = 50;
                
                for (uint32_t i = start_index; i < end_index; i++)
                {
                    Vector3 position;
                    uint32_t tri_idx;
                    uint32_t attempts = 0;
                    
                    do
                    {
                        tri_idx           = acceptable_triangles[triangle_dist(generator)];
                        TriangleData& tri = tile_triangle_data[tri_idx];

                        float r1      = dist(generator);
                        float r2      = dist(generator);
                        float sqrt_r1 = sqrtf(r1);
                        float u       = 1.0f - sqrt_r1;
                        float v       = r2 * sqrt_r1;
                        position      = tri.v0 + u * tri.v1_minus_v0 + v * tri.v2_minus_v0 + Vector3(0.0f, prop_desc.surface_offset, 0.0f);
                        attempts++;
                        
                        if (!has_safe_zone || prop_desc.cluster_radius <= 0.0f)
                            break;
                            
                    } while (attempts < max_attempts &&
                             (position.x < safe_min_x || position.x > safe_max_x ||
                              position.z < safe_min_z || position.z > safe_max_z));
                    
                    clusters[i] = { position, tri_idx };
                }
            };
            ThreadPool::ParallelLoop(place_cluster, cluster_count);

            // build spatial grid for nearby triangle lookup
            vector<vector<uint32_t>> cluster_nearby_tris(cluster_count);
            const float max_effective_radius = prop_desc.cluster_radius * 1.6f;
            const float cell_size            = max(max_effective_radius, 1.0f);
            
            int32_t grid_min_x  = static_cast<int32_t>(floorf(tile_min_x / cell_size));
            int32_t grid_min_z  = static_cast<int32_t>(floorf(tile_min_z / cell_size));
            int32_t grid_max_x  = static_cast<int32_t>(floorf(tile_max_x / cell_size));
            int32_t grid_width  = grid_max_x - grid_min_x + 1;
            
            unordered_map<int64_t, vector<uint32_t>> spatial_grid;
            if (prop_desc.cluster_radius > 0.0f)
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
                    ClusterData& cl = clusters[c];
                    Vector2 cl_xz(cl.center_position.x, cl.center_position.z);
                    
                    if (prop_desc.cluster_radius <= 0.0f)
                    {
                        nearby.push_back(cl.center_tri_idx);
                        continue;
                    }
                    
                    // generate noise parameters from cluster position
                    float seed1 = (cl.center_position.x * 12.9898f + cl.center_position.z * 78.233f) * 43758.5453f;
                    float seed2 = (cl.center_position.x * 39.346f + cl.center_position.z * 11.135f) * 23421.631f;
                    float seed3 = (cl.center_position.z * 47.134f + cl.center_position.x * 93.271f) * 67823.183f;
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
                    float max_radius   = prop_desc.cluster_radius * 1.6f;
                    int32_t cell_x     = static_cast<int32_t>(floorf(cl_xz.x / cell_size)) - grid_min_x;
                    int32_t cell_z     = static_cast<int32_t>(floorf(cl_xz.y / cell_size)) - grid_min_z;
                    int32_t cell_range = static_cast<int32_t>(ceilf(max_radius / cell_size));
                    
                    for (int32_t dz = -cell_range; dz <= cell_range; dz++)
                    {
                        for (int32_t dx = -cell_range; dx <= cell_range; dx++)
                        {
                            int64_t cell_key = static_cast<int64_t>(cell_z + dz) * grid_width + (cell_x + dx);
                            auto grid_it     = spatial_grid.find(cell_key);
                            if (grid_it == spatial_grid.end())
                                continue;
                            
                            for (uint32_t t : grid_it->second)
                            {
                                uint32_t tri_idx  = acceptable_triangles[t];
                                TriangleData& tri = tile_triangle_data[tri_idx];
                                Vector2 tri_xz(tri.centroid.x, tri.centroid.z);
                                Vector2 offset  = tri_xz - cl_xz;
                                float dist_sq   = offset.LengthSquared();
                                float dist      = sqrtf(dist_sq);
                                float angle     = atan2f(offset.y, offset.x);
                                float norm_dist = dist / prop_desc.cluster_radius;
                                
                                // layered noise for organic blob shape
                                float noise1     = sinf(angle * freq1 + phase1) * 0.18f;
                                float noise2     = sinf(angle * freq2 + phase2) * 0.14f;
                                float noise3     = sinf(angle * freq3 + phase3) * 0.10f;
                                float noise4     = cosf(angle * freq4 + phase4) * 0.20f;
                                float noise5     = sinf(angle * freq5 + phase5) * 0.06f;
                                float dist_noise = sinf(norm_dist * 3.14159f + seed1 * 6.28f) * 0.12f * norm_dist;
                                float pos_noise  = sinf(offset.x * 0.3f + seed2 * 10.0f) * cosf(offset.y * 0.3f + seed3 * 10.0f) * 0.08f;
                                
                                float radius_variation = 1.0f + noise1 + noise2 + noise3 + noise4 + noise5 + dist_noise + pos_noise;
                                radius_variation       = fmaxf(0.4f, fminf(1.6f, radius_variation));
                                
                                float effective_radius = prop_desc.cluster_radius * radius_variation;
                                if (dist_sq <= effective_radius * effective_radius)
                                    nearby.push_back(tri_idx);
                            }
                        }
                    }
                    
                    if (nearby.empty())
                        nearby.push_back(cl.center_tri_idx);
                }
            };
            ThreadPool::ParallelLoop(compute_nearby, cluster_count);

            // place instances within clusters
            auto place_mesh = [&](uint32_t start_index, uint32_t end_index)
            {
                mt19937 generator(tile_index * 2000003u + start_index * 37u + 67890u);
                uniform_real_distribution<float> dist(0.0f, 1.0f);
                uniform_real_distribution<float> angle_dist(0.0f, 360.0f);
                uniform_real_distribution<float> scale_dist(prop_desc.min_scale, prop_desc.max_scale);
                uint32_t larger_cluster_size = base_instances_per_cluster + 1;
                
                for (uint32_t i = start_index; i < end_index; i++)
                {
                    // map instance to cluster
                    uint32_t cluster_idx;
                    if (i < remainder_instances * larger_cluster_size)
                        cluster_idx = i / larger_cluster_size;
                    else
                        cluster_idx = remainder_instances + (i - remainder_instances * larger_cluster_size) / base_instances_per_cluster;
                    
                    auto& nearby = cluster_nearby_tris[cluster_idx];
                    if (nearby.empty())
                        continue;

                    uniform_int_distribution<int> nearby_dist(0, static_cast<int>(nearby.size()) - 1);
                    uint32_t tri_idx  = nearby[nearby_dist(generator)];
                    TriangleData& tri = tile_triangle_data[tri_idx];

                    // random position within triangle
                    float r1      = dist(generator);
                    float r2      = dist(generator);
                    float sqrt_r1 = sqrtf(r1);
                    float u       = 1.0f - sqrt_r1;
                    float v       = r2 * sqrt_r1;
                    Vector3 position = tri.v0 + u * tri.v1_minus_v0 + v * tri.v2_minus_v0 + Vector3(0.0f, prop_desc.surface_offset, 0.0f);

                    // rotation
                    Quaternion rotation;
                    if (prop_desc.align_to_surface_normal)
                    {
                        Quaternion random_y_rotation = Quaternion::FromEulerAngles(0.0f, angle_dist(generator), 0.0f);
                        rotation = tri.rotation_to_normal * random_y_rotation;
                    }
                    else
                    {
                        rotation = Quaternion::FromEulerAngles(0.0f, angle_dist(generator), 0.0f);
                    }

                    // scale with optional slope adjustment
                    float scale = scale_dist(generator);
                    if (prop_desc.scale_adjust_by_slope)
                    {
                        float slope_normalized = clamp(tri.slope_radians / prop_desc.max_slope_angle_rad, 0.0f, 1.0f);
                        scale *= lerp(1.0f, prop_desc.max_scale / prop_desc.min_scale, slope_normalized);
                    }
                    
                    transforms_out[i] = Matrix::CreateScale(scale) * Matrix::CreateRotation(rotation) * Matrix::CreateTranslation(position);
                }
            };
            ThreadPool::ParallelLoop(place_mesh, adjusted_count);
        }
    }

    namespace
    {
        float compute_surface_area_km2(const vector<RHI_Vertex_PosTexNorTan>& vertices, const vector<uint32_t>& indices)
        {
            uint32_t triangle_count = static_cast<uint32_t>(indices.size() / 3);
            vector<float> partial_areas(triangle_count);
            
            auto compute_areas = [&](uint32_t start_tri, uint32_t end_tri)
            {
                for (uint32_t t = start_tri; t < end_tri; t++)
                {
                    size_t i        = static_cast<size_t>(t) * 3;
                    const auto& v0  = vertices[indices[i + 0]].pos;
                    const auto& v1  = vertices[indices[i + 1]].pos;
                    const auto& v2  = vertices[indices[i + 2]].pos;
                    
                    float e1x = v1[0] - v0[0], e1y = v1[1] - v0[1], e1z = v1[2] - v0[2];
                    float e2x = v2[0] - v0[0], e2y = v2[1] - v0[1], e2z = v2[2] - v0[2];
                    float cx  = e1y * e2z - e1z * e2y;
                    float cy  = e1z * e2x - e1x * e2z;
                    float cz  = e1x * e2y - e1y * e2x;
                    
                    partial_areas[t] = 0.5f * sqrtf(cx * cx + cy * cy + cz * cz);
                }
            };
            ThreadPool::ParallelLoop(compute_areas, triangle_count);
            
            float area_m2 = 0.0f;
            for (float a : partial_areas)
                area_m2 += a;
            
            return area_m2 / 1'000'000.0f;
        }
        
        void get_values_from_height_map(
            vector<float>& height_data_out,
            RHI_Texture* height_texture,
            float min_y,
            float max_y,
            uint32_t smoothing,
            bool create_border
        )
        {
            vector<byte> height_data = height_texture->GetMip(0, 0)->bytes;
            SP_ASSERT(height_data.size() > 0);
        
            // map texture bytes to height values
            uint32_t bytes_per_pixel = (height_texture->GetChannelCount() * height_texture->GetBitsPerChannel()) / 8;
            uint32_t pixel_count     = static_cast<uint32_t>(height_data.size() / bytes_per_pixel);
            height_data_out.resize(pixel_count);

            auto map_height = [&](uint32_t start_pixel, uint32_t end_pixel)
            {
                for (uint32_t pixel = start_pixel; pixel < end_pixel; pixel++)
                {
                    uint32_t byte_index    = pixel * bytes_per_pixel;
                    float normalized_value = static_cast<float>(height_data[byte_index]) / 255.0f;
                    height_data_out[pixel] = min_y + normalized_value * (max_y - min_y);
                }
            };
            ThreadPool::ParallelLoop(map_height, pixel_count);
        
            // smooth height map to reduce hard edges
            const uint32_t width  = height_texture->GetWidth();
            const uint32_t height = height_texture->GetHeight();

            for (uint32_t iteration = 0; iteration < smoothing; iteration++)
            {
                vector<float> smoothed_height_data(height_data_out.size());
                
                auto smooth_pixel = [&](uint32_t start_idx, uint32_t end_idx)
                {
                    for (uint32_t idx = start_idx; idx < end_idx; idx++)
                    {
                        uint32_t x     = idx % width;
                        uint32_t y     = idx / width;
                        float sum      = height_data_out[idx];
                        uint32_t count = 1;
                        
                        for (int ny = -1; ny <= 1; ++ny)
                        {
                            for (int nx = -1; nx <= 1; ++nx)
                            {
                                if (nx == 0 && ny == 0)
                                    continue;
                                
                                int neighbor_x = static_cast<int>(x) + nx;
                                int neighbor_y = static_cast<int>(y) + ny;
                                
                                if (neighbor_x >= 0 && neighbor_x < static_cast<int>(width) && 
                                    neighbor_y >= 0 && neighbor_y < static_cast<int>(height))
                                {
                                    sum += height_data_out[neighbor_y * width + neighbor_x];
                                    count++;
                                }
                            }
                        }
                        
                        smoothed_height_data[idx] = sum / static_cast<float>(count);
                    }
                };
                
                ThreadPool::ParallelLoop(smooth_pixel, width * height);
                height_data_out = move(smoothed_height_data);
            }
        
            // create border mountains to prevent player from leaving the terrain
            if (create_border)
            {
                const uint32_t border_backface_width = 2;
                const uint32_t border_plateau_width  = 25;
                const uint32_t border_blend_width    = 20;
                const float border_height_max        = 150.0f;

                auto apply_border = [&](uint32_t start_index, uint32_t end_index)
                {
                    for (uint32_t index = start_index; index < end_index; index++)
                    {
                        uint32_t x        = index % width;
                        uint32_t y        = index / width;
                        uint32_t min_dist = min({x, width - 1 - x, y, height - 1 - y});
                        
                        if (min_dist < border_backface_width)
                        {
                            height_data_out[index] = min_y;
                        }
                        else if (min_dist < border_backface_width + border_plateau_width)
                        {
                            height_data_out[index] += border_height_max;
                        }
                        else if (min_dist < border_backface_width + border_plateau_width + border_blend_width)
                        {
                            float blend = 1.0f - static_cast<float>(min_dist - (border_backface_width + border_plateau_width)) / static_cast<float>(border_blend_width);
                            height_data_out[index] += blend * border_height_max;
                        }
                    }
                };
                ThreadPool::ParallelLoop(apply_border, width * height);
            }
        }

        void densify_height_map(vector<float>& height_data, uint32_t width, uint32_t height, uint32_t density)
        {
            if (density <= 1)
                return;
        
            uint32_t dense_width  = density * (width - 1) + 1;
            uint32_t dense_height = density * (height - 1) + 1;
            vector<float> dense_height_data(dense_width * dense_height);
        
            auto get_height = [&height_data, width, height](uint32_t x, uint32_t y) -> float
            {
                return height_data[min(y, height - 1) * width + min(x, width - 1)];
            };
        
            // bilinear interpolation to increase resolution
            auto compute_dense_pixel = [&](uint32_t start_index, uint32_t end_index)
            {
                for (uint32_t index = start_index; index < end_index; index++)
                {
                    uint32_t x = index % dense_width;
                    uint32_t y = index / dense_width;

                    float u      = static_cast<float>(x) / static_cast<float>(density);
                    float v      = static_cast<float>(y) / static_cast<float>(density);
                    uint32_t x0  = static_cast<uint32_t>(floor(u));
                    uint32_t x1  = min(x0 + 1, width - 1);
                    uint32_t y0  = static_cast<uint32_t>(floor(v));
                    uint32_t y1  = min(y0 + 1, height - 1);
                    float dx     = u - static_cast<float>(x0);
                    float dy     = v - static_cast<float>(y0);

                    float h00 = get_height(x0, y0);
                    float h10 = get_height(x1, y0);
                    float h01 = get_height(x0, y1);
                    float h11 = get_height(x1, y1);

                    dense_height_data[y * dense_width + x] = 
                        (1.0f - dx) * (1.0f - dy) * h00 +
                        dx * (1.0f - dy) * h10 +
                        (1.0f - dx) * dy * h01 +
                        dx * dy * h11;
                }
            };
        
            ThreadPool::ParallelLoop(compute_dense_pixel, dense_width * dense_height);
            height_data = move(dense_height_data);
        }

        void generate_positions(vector<Vector3>& positions, const vector<float>& height_map, uint32_t width, uint32_t height, uint32_t density, uint32_t scale)
        {
            SP_ASSERT_MSG(!height_map.empty(), "height map is empty");
        
            positions.resize(width * height);
        
            uint32_t base_width  = (width - 1) / density + 1;
            uint32_t base_height = (height - 1) / density + 1;
            float extent_x       = static_cast<float>(base_width - 1) * scale;
            float extent_z       = static_cast<float>(base_height - 1) * scale;
            float scale_x        = extent_x / static_cast<float>(width - 1);
            float scale_z        = extent_z / static_cast<float>(height - 1);
            float offset_x       = extent_x / 2.0f;
            float offset_z       = extent_z / 2.0f;
        
            auto generate_position = [&](uint32_t start_index, uint32_t end_index)
            {
                for (uint32_t index = start_index; index < end_index; index++)
                {
                    uint32_t x       = index % width;
                    uint32_t y       = index / width;
                    float centered_x = static_cast<float>(x) * scale_x - offset_x;
                    float centered_z = static_cast<float>(y) * scale_z - offset_z;
                    positions[index] = Vector3(centered_x, height_map[index], centered_z);
                }
            };
        
            ThreadPool::ParallelLoop(generate_position, width * height);
        }

        void apply_wind_erosion(vector<Vector3>& positions, uint32_t width, uint32_t height, float wind_strength = 0.3f)
        {
            static const float kernel[9] = {
                0.0625f, 0.125f, 0.0625f,
                0.125f,  0.25f,  0.125f,
                0.0625f, 0.125f, 0.0625f
            };
            
            vector<float> temp_heights(positions.size());
            auto copy_heights = [&](uint32_t start, uint32_t end)
            {
                for (uint32_t i = start; i < end; i++)
                    temp_heights[i] = positions[i].y;
            };
            ThreadPool::ParallelLoop(copy_heights, static_cast<uint32_t>(positions.size()));
            
            // gaussian blur for wind smoothing effect
            auto apply_blur = [&](uint32_t start_idx, uint32_t end_idx)
            {
                for (uint32_t idx = start_idx; idx < end_idx; idx++)
                {
                    uint32_t x = idx % width;
                    uint32_t z = idx / width;
                    
                    if (x < 1 || x >= width - 1 || z < 1 || z >= height - 1)
                        continue;
                    
                    float new_height = 0.0f;
                    for (int kz = -1; kz <= 1; ++kz)
                        for (int kx = -1; kx <= 1; ++kx)
                            new_height += temp_heights[(z + kz) * width + (x + kx)] * kernel[(kz + 1) * 3 + (kx + 1)];
                    
                    positions[idx].y += wind_strength * (new_height - positions[idx].y);
                }
            };
            
            ThreadPool::ParallelLoop(apply_blur, width * height);
        }
        
        void apply_erosion(vector<Vector3>& positions, uint32_t width, uint32_t height, float level_sea, uint32_t iterations = 1'000'000, uint32_t wind_interval = 50'000)
        {
            const float inertia          = 0.05f;
            const float capacity_factor  = 1.0f;
            const float min_slope        = 0.01f;
            const float deposition_rate  = 0.05f;
            const float erosion_rate     = 0.2f;
            const float evaporation_rate = 0.05f;
            const float gravity          = 4.0f;
            const uint32_t max_steps     = 30;
            const float wind_strength    = 0.3f;
            const float max_step         = 0.5f;

            // height interpolation
            auto get_height = [&positions, width, height](float x, float z) -> float
            {
                int ix   = clamp(static_cast<int>(floor(x)), 0, static_cast<int>(width) - 2);
                int iz   = clamp(static_cast<int>(floor(z)), 0, static_cast<int>(height) - 2);
                float fx = x - static_cast<float>(ix);
                float fz = z - static_cast<float>(iz);
        
                float h00 = positions[static_cast<size_t>(iz) * width + ix].y;
                float h10 = positions[static_cast<size_t>(iz) * width + ix + 1].y;
                float h01 = positions[static_cast<size_t>(iz + 1) * width + ix].y;
                float h11 = positions[static_cast<size_t>(iz + 1) * width + ix + 1].y;
        
                return (h00 + fx * (h10 - h00)) + fz * ((h01 + fx * (h11 - h01)) - (h00 + fx * (h10 - h00)));
            };
        
            // bilinear height modification
            auto add_height = [&positions, width, height, level_sea, max_step](float x, float z, float amount)
            {
                int ix   = clamp(static_cast<int>(floor(x)), 0, static_cast<int>(width) - 2);
                int iz   = clamp(static_cast<int>(floor(z)), 0, static_cast<int>(height) - 2);
                float fx = x - static_cast<float>(ix);
                float fz = z - static_cast<float>(iz);
            
                float w00 = (1.0f - fx) * (1.0f - fz);
                float w10 = fx * (1.0f - fz);
                float w01 = (1.0f - fx) * fz;
                float w11 = fx * fz;

                auto apply = [&](size_t idx, float delta)
                {
                    float& h = positions[idx].y;
                    h       += clamp(delta, -max_step, max_step);
                    h        = max(h, level_sea - 10.0f);
                };
            
                apply(static_cast<size_t>(iz) * width + ix,         amount * w00);
                apply(static_cast<size_t>(iz) * width + ix + 1,     amount * w10);
                apply(static_cast<size_t>(iz + 1) * width + ix,     amount * w01);
                apply(static_cast<size_t>(iz + 1) * width + ix + 1, amount * w11);
            };

            auto get_gradient = [get_height](float x, float z) -> Vector2
            {
                return Vector2(
                    (get_height(x + 1.0f, z) - get_height(x - 1.0f, z)) / 2.0f,
                    (get_height(x, z + 1.0f) - get_height(x, z - 1.0f)) / 2.0f
                );
            };
        
            mt19937 gen(width * 3000017u + height * 41u + 11111u);
            uniform_real_distribution<float> dist_x(1.0f, static_cast<float>(width) - 2.0f);
            uniform_real_distribution<float> dist_z(1.0f, static_cast<float>(height) - 2.0f);
        
            // hydraulic erosion simulation
            for (uint32_t i = 0; i < iterations; ++i)
            {
                if (i % wind_interval == 0 && i != 0)
                    apply_wind_erosion(positions, width, height, wind_strength);
        
                float pos_x    = dist_x(gen);
                float pos_z    = dist_z(gen);
                Vector2 dir    = Vector2::Zero;
                float speed    = 1.0f;
                float water    = 1.0f;
                float sediment = 0.0f;
        
                for (uint32_t step = 0; step < max_steps; ++step)
                {
                    if (water < 0.01f)
                        break;
        
                    float h          = get_height(pos_x, pos_z);
                    Vector2 gradient = get_gradient(pos_x, pos_z);
                    float slope      = max(gradient.Length(), min_slope);

                    Vector2 new_dir = -gradient.Normalized();
                    dir             = (dir * inertia + new_dir * (1.0f - inertia)).Normalized();
        
                    float new_x   = pos_x + dir.x;
                    float new_z   = pos_z + dir.y;
                    float delta_h = get_height(new_x, new_z) - h;
        
                    // stop if moving uphill
                    if (delta_h >= 0.0f || dir.LengthSquared() < 0.0001f)
                    {
                        add_height(pos_x, pos_z, sediment * deposition_rate);
                        break;
                    }
        
                    pos_x = new_x;
                    pos_z = new_z;
                    speed = sqrt(speed * speed - gravity * delta_h);

                    float capacity = capacity_factor * water * speed * slope;
        
                    if (sediment > capacity)
                    {
                        float deposit = (sediment - capacity) * deposition_rate;
                        add_height(pos_x, pos_z, deposit);
                        sediment -= deposit;
                    }
                    else
                    {
                        float erode = min((capacity - sediment) * erosion_rate, -delta_h);
                        add_height(pos_x, pos_z, -erode);
                        sediment += erode;
                    }
        
                    water *= (1.0f - evaporation_rate);
                }
            }
        
            apply_wind_erosion(positions, width, height, wind_strength);
        }

        void generate_vertices_and_indices(vector<RHI_Vertex_PosTexNorTan>& vertices, vector<uint32_t>& indices, const vector<Vector3>& positions, uint32_t width, uint32_t height)
        {
            SP_ASSERT_MSG(!positions.empty(), "positions are empty");

            const float inv_width  = 1.0f / static_cast<float>(width - 1);
            const float inv_height = 1.0f / static_cast<float>(height - 1);
            
            auto gen_vertices = [&](uint32_t start_idx, uint32_t end_idx)
            {
                for (uint32_t idx = start_idx; idx < end_idx; idx++)
                {
                    uint32_t x   = idx % width;
                    uint32_t y   = idx / width;
                    vertices[idx] = RHI_Vertex_PosTexNorTan(positions[idx], Vector2(x * inv_width, y * inv_height));
                }
            };
            ThreadPool::ParallelLoop(gen_vertices, width * height);
            
            uint32_t quad_count = (width - 1) * (height - 1);
            auto gen_indices = [&](uint32_t start_quad, uint32_t end_quad)
            {
                for (uint32_t quad = start_quad; quad < end_quad; quad++)
                {
                    uint32_t x  = quad % (width - 1);
                    uint32_t y  = quad / (width - 1);
                    uint32_t k  = quad * 6;
                    uint32_t bl = y * width + x;
                    uint32_t br = bl + 1;
                    uint32_t tl = bl + width;
                    uint32_t tr = tl + 1;
                    
                    indices[k]     = br;
                    indices[k + 1] = bl;
                    indices[k + 2] = tl;
                    indices[k + 3] = br;
                    indices[k + 4] = tl;
                    indices[k + 5] = tr;
                }
            };
            ThreadPool::ParallelLoop(gen_indices, quad_count);
        }

        void generate_normals(vector<RHI_Vertex_PosTexNorTan>& vertices, uint32_t width, uint32_t height)
        {
            SP_ASSERT_MSG(!vertices.empty(), "vertices are empty");
        
            // interior vertices - no boundary checks needed
            uint32_t interior_count = (width - 2) * (height - 2);
            if (interior_count > 0)
            {
                auto compute_interior = [&](uint32_t start, uint32_t end)
                {
                    for (uint32_t index = start; index < end; index++)
                    {
                        uint32_t interior_width = width - 2;
                        uint32_t i              = (index % interior_width) + 1;
                        uint32_t j              = (index / interior_width) + 1;
                        uint32_t vertex_idx     = j * width + i;
                        
                        float h_left   = vertices[vertex_idx - 1].pos[1];
                        float h_right  = vertices[vertex_idx + 1].pos[1];
                        float h_bottom = vertices[vertex_idx - width].pos[1];
                        float h_top    = vertices[vertex_idx + width].pos[1];
                        
                        float dh_dx = (h_right - h_left) * 0.5f;
                        float dh_dz = (h_top - h_bottom) * 0.5f;
                        
                        float nx      = -dh_dx, ny = 1.0f, nz = -dh_dz;
                        float inv_len = 1.0f / sqrtf(nx * nx + ny * ny + nz * nz);
                        nx *= inv_len; ny *= inv_len; nz *= inv_len;
                        vertices[vertex_idx].nor[0] = nx;
                        vertices[vertex_idx].nor[1] = ny;
                        vertices[vertex_idx].nor[2] = nz;
                        
                        float proj      = nx;
                        float tx        = 1.0f - nx * proj, ty = -ny * proj, tz = -nz * proj;
                        float t_inv_len = 1.0f / sqrtf(tx * tx + ty * ty + tz * tz);
                        vertices[vertex_idx].tan[0] = tx * t_inv_len;
                        vertices[vertex_idx].tan[1] = ty * t_inv_len;
                        vertices[vertex_idx].tan[2] = tz * t_inv_len;
                    }
                };
                ThreadPool::ParallelLoop(compute_interior, interior_count);
            }
            
            // edge vertices with boundary handling
            uint32_t edge_count = 2 * width + 2 * (height - 2);
            auto compute_edges = [&](uint32_t start, uint32_t end)
            {
                for (uint32_t edge_idx = start; edge_idx < end; edge_idx++)
                {
                    uint32_t i, j;
                    uint32_t perimeter = 2 * width + 2 * (height - 2);
                    
                    if (edge_idx < width)
                        { i = edge_idx; j = 0; }
                    else if (edge_idx < width + height - 1)
                        { i = width - 1; j = edge_idx - width + 1; }
                    else if (edge_idx < 2 * width + height - 2)
                        { i = 2 * width + height - 3 - edge_idx; j = height - 1; }
                    else
                        { i = 0; j = perimeter - edge_idx; }
                    
                    uint32_t index   = j * width + i;
                    uint32_t i_left  = (i > 0) ? i - 1 : i;
                    uint32_t i_right = (i < width - 1) ? i + 1 : i;
                    uint32_t j_bot   = (j > 0) ? j - 1 : j;
                    uint32_t j_top   = (j < height - 1) ? j + 1 : j;
                    
                    float h_left  = vertices[j * width + i_left].pos[1];
                    float h_right = vertices[j * width + i_right].pos[1];
                    float h_bot   = vertices[j_bot * width + i].pos[1];
                    float h_top   = vertices[j_top * width + i].pos[1];
                    
                    float dh_dx = (h_right - h_left) / ((i_right != i_left) ? static_cast<float>(i_right - i_left) : 1.0f);
                    float dh_dz = (h_top - h_bot) / ((j_top != j_bot) ? static_cast<float>(j_top - j_bot) : 1.0f);
                    
                    float nx      = -dh_dx, ny = 1.0f, nz = -dh_dz;
                    float inv_len = 1.0f / sqrtf(nx * nx + ny * ny + nz * nz);
                    nx *= inv_len; ny *= inv_len; nz *= inv_len;
                    vertices[index].nor[0] = nx;
                    vertices[index].nor[1] = ny;
                    vertices[index].nor[2] = nz;
                    
                    float proj      = nx;
                    float tx        = 1.0f - nx * proj, ty = -ny * proj, tz = -nz * proj;
                    float t_inv_len = 1.0f / sqrtf(tx * tx + ty * ty + tz * tz);
                    vertices[index].tan[0] = tx * t_inv_len;
                    vertices[index].tan[1] = ty * t_inv_len;
                    vertices[index].tan[2] = tz * t_inv_len;
                }
            };
            ThreadPool::ParallelLoop(compute_edges, edge_count);
        }

        void apply_perlin_noise(vector<Vector3>& positions, uint32_t width, uint32_t height, float amplitude = 5.0f, float frequency = 0.01f, uint32_t octaves = 4, float persistence = 1.0f)
        {
            auto fade = [](float t) -> float { return t * t * t * (t * (t * 6 - 15) + 10); };
            auto lerp = [](float a, float b, float t) -> float { return a + t * (b - a); };
        
            // initialize permutation table and gradients
            vector<uint8_t> permutation(512);
            vector<Vector2> gradients(256);
            
            mt19937 gen(width * 4000037u + height * 53u + 22222u);
            uniform_real_distribution<float> dist(-1.0f, 1.0f);

            for (uint32_t i = 0; i < 256; ++i)
            {
                permutation[i] = static_cast<uint8_t>(i);
                Vector2 grad(dist(gen), dist(gen));
                gradients[i] = grad.Normalized();
            }

            for (uint32_t i = 255; i > 0; --i)
            {
                uniform_int_distribution<uint32_t> swap_dist(0, i);
                swap(permutation[i], permutation[swap_dist(gen)]);
            }
            
            for (uint32_t i = 0; i < 256; ++i)
                permutation[256 + i] = permutation[i];
        
            auto perlin_noise = [&](float x, float z) -> float
            {
                int X = static_cast<int>(floor(x)) & 255;
                int Z = static_cast<int>(floor(z)) & 255;
                x -= floor(x);
                z -= floor(z);
        
                float u = fade(x);
                float v = fade(z);
        
                int aa = permutation[permutation[X] + Z];
                int ab = permutation[permutation[X] + Z + 1];
                int ba = permutation[permutation[X + 1] + Z];
                int bb = permutation[permutation[X + 1] + Z + 1];
        
                float grad00 = gradients[aa & 255].x * x + gradients[aa & 255].y * z;
                float grad10 = gradients[ba & 255].x * (x - 1) + gradients[ba & 255].y * z;
                float grad01 = gradients[ab & 255].x * x + gradients[ab & 255].y * (z - 1);
                float grad11 = gradients[bb & 255].x * (x - 1) + gradients[bb & 255].y * (z - 1);
        
                return lerp(lerp(grad00, grad10, u), lerp(grad01, grad11, u), v);
            };
        
            auto apply_noise = [&](uint32_t start_index, uint32_t end_index)
            {
                for (uint32_t index = start_index; index < end_index; ++index)
                {
                    uint32_t x = index % width;
                    uint32_t z = index / width;

                    float scaled_x          = static_cast<float>(x) * frequency;
                    float scaled_z          = static_cast<float>(z) * frequency;
                    float noise_value       = 0.0f;
                    float current_amplitude = amplitude;
                    float current_frequency = 1.0f;
                    float max_amplitude     = 0.0f;
        
                    for (uint32_t octave = 0; octave < octaves; ++octave)
                    {
                        noise_value       += perlin_noise(scaled_x * current_frequency, scaled_z * current_frequency) * current_amplitude;
                        max_amplitude     += current_amplitude;
                        current_amplitude *= persistence;
                        current_frequency *= 2.0f;
                    }
        
                    positions[index].y += (noise_value / max_amplitude) * amplitude;
                }
            };
        
            ThreadPool::ParallelLoop(apply_noise, width * height);
        }
    }

    Terrain::Terrain(Entity* entity) : Component(entity)
    {
        m_material = make_shared<Material>();
        m_material->SetObjectName("terrain");
    }

    Terrain::~Terrain()
    {
        m_height_map_seed = nullptr;
    }

    uint64_t Terrain::ComputeCacheHash() const
    {
        // hash inputs to detect when cache is stale
        uint64_t hash = 14695981039346656037ull; // fnv-1a offset basis
        auto hash_combine = [&hash](uint64_t value) {
            hash ^= value;
            hash *= 1099511628211ull; // fnv-1a prime
        };

        hash_combine(static_cast<uint64_t>(m_min_y * 1000));
        hash_combine(static_cast<uint64_t>(m_max_y * 1000));
        hash_combine(static_cast<uint64_t>(m_level_sea * 1000));
        hash_combine(static_cast<uint64_t>(m_level_snow * 1000));
        hash_combine(m_smoothing);
        hash_combine(m_density);
        hash_combine(m_scale);
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

    void Terrain::FindTransforms(const uint32_t tile_index, const TerrainProp terrain_prop, Entity* entity, const float density_fraction, const float scale, vector<Matrix>& transforms_out)
    {
        TerrainPropDescription description;

        if (terrain_prop == TerrainProp::Tree)
        {
            description.max_slope_angle_rad  = 45.0f * math::deg_to_rad;
            description.min_spawn_height     = m_level_sea + 5.0f;
            description.max_spawn_height     = m_level_snow + 20;
            description.min_scale            = scale * 0.4f;
            description.max_scale            = scale * 1.0f;
        }
        else if (terrain_prop == TerrainProp::Grass)
        {
            description.max_slope_angle_rad     = 45.0f * math::deg_to_rad;
            description.align_to_surface_normal = true;
            description.min_spawn_height        = m_level_sea + 5.0f;
            description.max_spawn_height        = m_level_snow;
            description.min_scale               = scale * 1.0f;
            description.max_scale               = scale * 1.5f;
        }
        else if (terrain_prop == TerrainProp::Flower)
        {
            description.max_slope_angle_rad     = 45.0f * math::deg_to_rad;
            description.align_to_surface_normal = true;
            description.min_spawn_height        = m_level_sea + 5.0f;
            description.max_spawn_height        = m_level_snow;
            description.min_scale               = scale * 0.2f;
            description.max_scale               = scale * 1.2f;
            description.instances_per_cluster   = 1000;
            description.cluster_radius          = 30.0f;
        }
        else if (terrain_prop == TerrainProp::Rock)
        {
            description.max_slope_angle_rad     = 45.0f * math::deg_to_rad;
            description.align_to_surface_normal = true;
            description.min_spawn_height        = m_level_sea - 10.0f;
            description.max_spawn_height        = numeric_limits<float>::max();
            description.min_scale               = scale * 0.1f;
            description.max_scale               = scale * 1.0f;
            description.scale_adjust_by_slope   = true;
        }
        else
        {
            SP_ASSERT_MSG(false, "unknown terrain prop type");
        }

        placement::find_transforms(description, density_fraction, tile_index, transforms_out, m_triangle_data);

        // compensate for entity scale
        if (entity && entity->GetScale() != Vector3::One && entity->GetScale() != Vector3::Zero)
        {
            Matrix root_scale_matrix = Matrix::CreateScale(Vector3::One / entity->GetScale());
            for (Matrix& t : transforms_out)
                t *= root_scale_matrix;
        }
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
            return;
    
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
        if (m_is_generating)
        {
            SP_LOG_WARNING("terrain generation already in progress");
            return;
        }
    
        if (!m_height_map_seed)
        {
            SP_LOG_WARNING("assign a height map before generating terrain");
            return;
        }
    
        m_is_generating = true;
    
        uint32_t job_count = 9;
        ProgressTracker::GetProgress(ProgressType::Terrain).Start(job_count, "generating terrain...");
    
        // try loading from cache
        const string cache_file = "terrain_cache.bin";
        bool loaded_from_cache  = false;

        LoadFromFile(cache_file.c_str());
        if (!m_vertices.empty())
        {
            loaded_from_cache = true;
            ProgressTracker::GetProgress(ProgressType::Terrain).SetText("loaded from cache");
            for (uint32_t i = 0; i < job_count - 1; i++)
                ProgressTracker::GetProgress(ProgressType::Terrain).JobDone();
        }

        if (!loaded_from_cache)
        {
            SP_LOG_INFO("generating terrain from scratch...");
    
            // 1. process height map
            ProgressTracker::GetProgress(ProgressType::Terrain).SetText("processing height map...");
            get_values_from_height_map(m_height_data, m_height_map_seed, m_min_y, m_max_y, m_smoothing, m_create_border);
            m_width  = m_height_map_seed->GetWidth();
            m_height = m_height_map_seed->GetHeight();
            densify_height_map(m_height_data, m_width, m_height, m_density);
            m_dense_width  = m_density * (m_width - 1) + 1;
            m_dense_height = m_density * (m_height - 1) + 1;
            ProgressTracker::GetProgress(ProgressType::Terrain).JobDone();
    
            // 2. generate positions
            ProgressTracker::GetProgress(ProgressType::Terrain).SetText("generating positions...");
            m_positions.resize(m_dense_width * m_dense_height);
            generate_positions(m_positions, m_height_data, m_dense_width, m_dense_height, m_density, m_scale);
            ProgressTracker::GetProgress(ProgressType::Terrain).JobDone();

            // 3. apply perlin noise
            ProgressTracker::GetProgress(ProgressType::Terrain).SetText("applying perlin noise...");
            apply_perlin_noise(m_positions, m_dense_width, m_dense_height);
            ProgressTracker::GetProgress(ProgressType::Terrain).JobDone();

            // 4. apply erosion
            ProgressTracker::GetProgress(ProgressType::Terrain).SetText("applying erosion...");
            apply_erosion(m_positions, m_dense_width, m_dense_height, m_level_sea);
            ProgressTracker::GetProgress(ProgressType::Terrain).JobDone();

            // 5. generate vertices and indices
            ProgressTracker::GetProgress(ProgressType::Terrain).SetText("generating mesh...");
            m_vertices.resize(m_dense_width * m_dense_height);
            m_indices.resize((m_dense_width - 1) * (m_dense_height - 1) * 6);
            generate_vertices_and_indices(m_vertices, m_indices, m_positions, m_dense_width, m_dense_height);
            ProgressTracker::GetProgress(ProgressType::Terrain).JobDone();
    
            // 6. generate normals
            ProgressTracker::GetProgress(ProgressType::Terrain).SetText("generating normals...");
            generate_normals(m_vertices, m_dense_width, m_dense_height);
            ProgressTracker::GetProgress(ProgressType::Terrain).JobDone();
    
            // 7. split into tiles
            ProgressTracker::GetProgress(ProgressType::Terrain).SetText("splitting into tiles...");
            uint32_t tile_count = 16;
            geometry_processing::split_surface_into_tiles(m_vertices, m_indices, tile_count, m_tile_vertices, m_tile_indices, m_tile_offsets);
            ProgressTracker::GetProgress(ProgressType::Terrain).JobDone();

            // 8. compute triangle data for placement
            ProgressTracker::GetProgress(ProgressType::Terrain).SetText("computing placement data...");
            for (uint32_t tile_index = 0; tile_index < m_tile_vertices.size(); tile_index++)
                placement::compute_triangle_data(m_tile_vertices, m_tile_indices, tile_index, m_triangle_data);
            ProgressTracker::GetProgress(ProgressType::Terrain).JobDone();

            SaveToFile(cache_file.c_str());
        }

        // bake height map texture
        {
            vector<RHI_Texture_Slice> data(1);
            data[0].mips.resize(1);
            data[0].mips[0].bytes.resize(m_dense_width * m_dense_height * sizeof(float));
        
            float* height_ptr = reinterpret_cast<float*>(data[0].mips[0].bytes.data());
            auto copy_heights = [this, height_ptr](uint32_t start, uint32_t end)
            {
                for (uint32_t i = start; i < end; i++)
                    height_ptr[i] = m_positions[i].y;
            };
            ThreadPool::ParallelLoop(copy_heights, m_dense_width * m_dense_height);
        
            m_height_map_final = make_shared<RHI_Texture>(
                RHI_Texture_Type::Type2D,
                m_dense_width, m_dense_height, 1, 1,
                RHI_Format::R32_Float, RHI_Texture_Srv,
                "terrain_baked", data
            );
        }
    
        // compute stats
        m_height_samples = m_dense_width * m_dense_height;
        m_vertex_count   = static_cast<uint32_t>(m_vertices.size());
        m_index_count    = static_cast<uint32_t>(m_indices.size());
        m_triangle_count = m_index_count / 3;
        m_area_km2       = compute_surface_area_km2(m_vertices, m_indices);

        // 9. create tile entities and gpu buffers
        ProgressTracker::GetProgress(ProgressType::Terrain).SetText("creating gpu mesh...");
        m_mesh = make_shared<Mesh>();
        m_mesh->SetObjectName("terrain_mesh");
        m_mesh->SetFlag(static_cast<uint32_t>(MeshFlags::PostProcessOptimize), false);
        m_mesh->SetFlag(static_cast<uint32_t>(MeshFlags::PostProcessPreserveTerrainEdges), true);

        for (uint32_t tile_index = 0; tile_index < static_cast<uint32_t>(m_tile_vertices.size()); tile_index++)
        {
            uint32_t sub_mesh_index = 0;
            m_mesh->AddGeometry(m_tile_vertices[tile_index], m_tile_indices[tile_index], true, &sub_mesh_index);
            
            Entity* entity = World::CreateEntity();
            entity->SetObjectName("tile_" + to_string(tile_index + 1));
            entity->SetParent(GetEntity());
            entity->SetPosition(m_tile_offsets[tile_index]);

            if (Renderable* renderable = entity->AddComponent<Renderable>())
            {
                renderable->SetMesh(m_mesh.get(), sub_mesh_index);
                renderable->SetMaterial(m_material);
            }
        }
    
        m_mesh->CreateGpuBuffers();
        ProgressTracker::GetProgress(ProgressType::Terrain).JobDone();
    
        // free temporary data
        m_vertices.clear();
        m_indices.clear();
        m_tile_vertices.clear();
        m_tile_indices.clear();

        m_is_generating = false;
    }

    void Terrain::Clear()
    {
        m_vertices.clear();
        m_indices.clear();
        m_tile_vertices.clear();
        m_tile_indices.clear();
        m_triangle_data.clear();
        ResourceCache::Remove(m_mesh);
        m_mesh = nullptr;

        for (Entity* child : m_entity_ptr->GetChildren())
        {
            if (Renderable* renderable = child->AddComponent<Renderable>())
                renderable->SetMesh(nullptr);
        }
    }
}
