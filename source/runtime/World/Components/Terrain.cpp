/*
Copyright(c) 2015-2025 Panos Karabelas

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
    namespace parameters
    {
        const float level_sea    = 0.0f;   // world height where sea level is set
        const float level_snow   = 400.0f; // world height where snow starts
        const uint32_t smoothing = 0;      // smoothing passes for height map spikes
        const uint32_t density   = 3;      // positions per height map sample, affects mesh resolution
        const uint32_t scale     = 6;      // physical size of the terrain mesh
        const bool create_border = true;   // adds a natural border to block player exit
    }

    namespace placement
    {
        struct TriangleData
        {
            Vector3 normal;
            Vector3 v0;
            Vector3 v1_minus_v0;
            Vector3 v2_minus_v0;
            float slope_radians;
            float height_min;
            float height_max;
            Quaternion rotation_to_normal;
            Vector3 centroid;
        };
        static unordered_map<uint64_t, vector<TriangleData>> triangle_data;

        struct ClusterData
        {
            Vector3 center_position;
            uint32_t center_tri_idx;
        };

        void compute_triangle_data(
            const vector<vector<RHI_Vertex_PosTexNorTan>>& vertices_terrain,
            const vector<vector<uint32_t>>& indices_terrain,
            uint32_t tile_index
        )
        {
            const vector<RHI_Vertex_PosTexNorTan>& vertices_tile = vertices_terrain[tile_index];
            const vector<uint32_t>& indices_tile                 = indices_terrain[tile_index];

            uint32_t triangle_count = static_cast<uint32_t>(indices_tile.size() / 3);
            auto& tile_triangle_data = triangle_data[tile_index];
            tile_triangle_data.resize(triangle_count);

            auto compute_triangle = [&vertices_tile, &indices_tile, &tile_triangle_data](uint32_t start_index, uint32_t end_index)
            {
                for (uint32_t i = start_index; i < end_index; i++)
                {
                    uint32_t idx0 = indices_tile[i * 3];
                    uint32_t idx1 = indices_tile[i * 3 + 1];
                    uint32_t idx2 = indices_tile[i * 3 + 2];

                    // apply tile offset so vertices are in world space
                    Vector3 v0(vertices_tile[idx0].pos[0], vertices_tile[idx0].pos[1], vertices_tile[idx0].pos[2]);
                    Vector3 v1(vertices_tile[idx1].pos[0], vertices_tile[idx1].pos[1], vertices_tile[idx1].pos[2]);
                    Vector3 v2(vertices_tile[idx2].pos[0], vertices_tile[idx2].pos[1], vertices_tile[idx2].pos[2]);

                    Vector3 normal                = Vector3::Cross(v1 - v0, v2 - v0).Normalized();
                    float slope_radians           = acos(Vector3::Dot(normal, Vector3::Up));
                    float height_min              = min({ v0.y, v1.y, v2.y });
                    float height_max              = max({ v0.y, v1.y, v2.y });
                    Vector3 v1_minus_v0           = v1 - v0;
                    Vector3 v2_minus_v0           = v2 - v0;
                    Quaternion rotation_to_normal = Quaternion::FromToRotation(Vector3::Up, normal);

                    Vector3 centroid = v0 + (v1_minus_v0 + v2_minus_v0) / 3.0f;

                    tile_triangle_data[i] = { normal, v0, v1_minus_v0, v2_minus_v0, slope_radians, height_min, height_max, rotation_to_normal, centroid };
                }
            };

            ThreadPool::ParallelLoop(compute_triangle, triangle_count);
        }

        void find_transforms(
            TerrainPropDescription prop_desc,
            const float density_fraction,
            uint32_t tile_index,
            vector<Matrix>& transforms_out
        )
        {
            auto it = triangle_data.find(tile_index);
            if (it == triangle_data.end())
            {
                SP_LOG_ERROR("No triangle data found for tile %d", tile_index);
                return;
            }
            vector<TriangleData>& tile_triangle_data = it->second;
            SP_ASSERT(!tile_triangle_data.empty());

            // step 1: filter acceptable triangles using precomputed data
            vector<uint32_t> acceptable_triangles;
            acceptable_triangles.reserve(tile_triangle_data.size());
            {
                for (uint32_t i = 0; i < tile_triangle_data.size(); i++)
                {
                    if (tile_triangle_data[i].slope_radians <= prop_desc.max_slope_angle_rad &&
                        tile_triangle_data[i].height_min >= prop_desc.min_spawn_height &&
                        tile_triangle_data[i].height_max <= prop_desc.max_spawn_height)
                    {
                        acceptable_triangles.push_back(i);
                    }
                }
                if (acceptable_triangles.empty())
                    return;
            }

            // step 2: compute adjusted count based on density fraction of acceptable triangles (maintains uniform visual density)
            uint32_t adjusted_count = static_cast<uint32_t>(density_fraction * static_cast<float>(acceptable_triangles.size()) + 0.5f);

            // step 3: pre-allocate output vector
            transforms_out.resize(adjusted_count);
            if (adjusted_count == 0)
                return;

            // setup clusters
            uint32_t cluster_count              = adjusted_count;
            uint32_t base_instances_per_cluster = 1;
            uint32_t remainder_instances        = 0;
            if (prop_desc.instances_per_cluster > 1)
            {
                cluster_count = max(1u, adjusted_count / prop_desc.instances_per_cluster);
                base_instances_per_cluster = adjusted_count / cluster_count;
                remainder_instances = adjusted_count % cluster_count;
            }
            vector<ClusterData> clusters(cluster_count);

            auto place_cluster = [
                &clusters,
                &tile_triangle_data,
                &acceptable_triangles,
                &prop_desc
            ]
            (uint32_t start_index, uint32_t end_index)
            {
                mt19937 generator(random_device{}());
                const uint32_t tri_count = static_cast<uint32_t>(acceptable_triangles.size());
                uniform_int_distribution<> triangle_dist(0, tri_count - 1);
                uniform_real_distribution<float> dist(0.0f, 1.0f);
                for (uint32_t i = start_index; i < end_index; i++)
                {
                    uint32_t tri_idx  = acceptable_triangles[triangle_dist(generator)];
                    TriangleData& tri = tile_triangle_data[tri_idx];

                    // position (xz used as cluster center)
                    float r1         = dist(generator);
                    float r2         = dist(generator);
                    float sqrt_r1    = sqrtf(r1);
                    float u          = 1.0f - sqrt_r1;
                    float v          = r2 * sqrt_r1;
                    Vector3 position = tri.v0 + u * tri.v1_minus_v0 + v * tri.v2_minus_v0 + Vector3(0.0f, prop_desc.surface_offset, 0.0f);
                    clusters[i]      = { position, tri_idx };
                }
            };
            ThreadPool::ParallelLoop(place_cluster, cluster_count);

            // compute nearby acceptable triangles per cluster (for snapping to surface)
            vector<vector<uint32_t>> cluster_nearby_tris(cluster_count);
            auto compute_nearby = [
                &cluster_nearby_tris,
                &clusters,
                &tile_triangle_data,
                &acceptable_triangles,
                &prop_desc
            ]
            (uint32_t start_index, uint32_t end_index)
            {
                mt19937 generator(random_device{}());
                const uint32_t tri_count = static_cast<uint32_t>(acceptable_triangles.size());
                uniform_int_distribution<> triangle_dist(0, tri_count - 1);
                for (uint32_t c = start_index; c < end_index; c++)
                {
                    auto& nearby = cluster_nearby_tris[c];
                    ClusterData& cl = clusters[c];
                    Vector2 cl_xz(cl.center_position.x, cl.center_position.z);
                    if (prop_desc.cluster_radius <= 0.0f)
                    {
                        nearby.push_back(cl.center_tri_idx);
                    }
                    else
                    {
                        for (uint32_t t = 0; t < tri_count; t++)
                        {
                            uint32_t tri_idx = acceptable_triangles[t];
                            TriangleData& tri = tile_triangle_data[tri_idx];
                            Vector2 tri_xz(tri.centroid.x, tri.centroid.z);
                            float dist_sq = (tri_xz - cl_xz).LengthSquared();
                            if (dist_sq <= prop_desc.cluster_radius * prop_desc.cluster_radius)
                            {
                                nearby.push_back(tri_idx);
                            }
                        }
                        if (nearby.empty())
                        {
                            nearby.push_back(cl.center_tri_idx);
                        }
                    }
                }
            };
            ThreadPool::ParallelLoop(compute_nearby, cluster_count);

            // step 4: parallel placement without mutex by direct assignment
            auto place_mesh = [
                &transforms_out,
                &tile_triangle_data,
                &prop_desc,
                &cluster_nearby_tris,
                base_instances_per_cluster,
                remainder_instances
            ]
            (uint32_t start_index, uint32_t end_index)
            {
                mt19937 generator(random_device{}());
                uniform_real_distribution<float> dist(0.0f, 1.0f);
                uniform_real_distribution<float> angle_dist(0.0f, 360.0f);
                uniform_real_distribution<float> scale_dist(prop_desc.min_scale, prop_desc.max_scale);
                uint32_t larger_cluster_size = base_instances_per_cluster + 1;
                for (uint32_t i = start_index; i < end_index; i++)
                {
                    // compute cluster idx from instance idx (distributes remainder to first clusters)
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
                        continue;

                    uniform_int_distribution<int> nearby_dist(0, static_cast<int>(nearby.size()) - 1);
                    uint32_t tri_idx = nearby[nearby_dist(generator)];
                    TriangleData& tri = tile_triangle_data[tri_idx];
                    // position
                    Vector3 position = Vector3::Zero;
                    {
                        float r1      = dist(generator);
                        float r2      = dist(generator);
                        float sqrt_r1 = sqrtf(r1);
                        float u       = 1.0f - sqrt_r1;
                        float v       = r2 * sqrt_r1;
                        position      = tri.v0 + u * tri.v1_minus_v0 + v * tri.v2_minus_v0 + Vector3(0.0f, prop_desc.surface_offset, 0.0f);
                    }

                    // rotation
                    Quaternion rotation;
                    {
                        if (prop_desc.align_to_surface_normal)
                        {
                            Quaternion random_y_rotation = Quaternion::FromEulerAngles(0.0f, angle_dist(generator), 0.0f);
                            rotation                     = tri.rotation_to_normal * random_y_rotation;
                        }
                        else
                        {
                            rotation = Quaternion::FromEulerAngles(0.0f, angle_dist(generator), 0.0f);
                        }
                    }

                    // scale
                    float scale = scale_dist(generator);
                    if (prop_desc.scale_adjust_by_slope)
                    {
                        float slope_normalized = tri.slope_radians / prop_desc.max_slope_angle_rad;
                        slope_normalized       = clamp(slope_normalized, 0.0f, 1.0f);
                        float slope_scale      = lerp(1.0f, prop_desc.max_scale / prop_desc.min_scale, slope_normalized);
                        scale                  *= slope_scale;
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
            float area_m2 = 0.0f;
        
            for (size_t i = 0; i + 2 < indices.size(); i += 3)
            {
                const auto& v0 = vertices[indices[i + 0]].pos;
                const auto& v1 = vertices[indices[i + 1]].pos;
                const auto& v2 = vertices[indices[i + 2]].pos;
        
                // compute edges
                Vector3 edge1 = { v1[0] - v0[0], v1[1] - v0[1], v1[2] - v0[2] };
                Vector3 edge2 = { v2[0] - v0[0], v2[1] - v0[1], v2[2] - v0[2] };
        
                // cross product
                Vector3 cross = {
                    edge1.y * edge2.z - edge1.z * edge2.y,
                    edge1.z * edge2.x - edge1.x * edge2.z,
                    edge1.x * edge2.y - edge1.y * edge2.x
                };
        
                // triangle area = 0.5 * length of cross product
                float triangle_area = 0.5f * sqrtf(cross.x * cross.x + cross.y * cross.y + cross.z * cross.z);
                area_m2 += triangle_area;
            }
        
            return area_m2 / 1'000'000.0f; // in km²
        }
        
        void get_values_from_height_map(vector<float>& height_data_out, RHI_Texture* height_texture, const float min_y, const float max_y)
        {
            vector<byte> height_data = height_texture->GetMip(0, 0).bytes;
            SP_ASSERT(height_data.size() > 0);
        
            // first pass: map the red channel values to heights in the range [min_y, max_y] (parallelized)
            {
                uint32_t bytes_per_pixel = (height_texture->GetChannelCount() * height_texture->GetBitsPerChannel()) / 8;
                uint32_t pixel_count = static_cast<uint32_t>(height_data.size() / bytes_per_pixel);
        
                // pre-allocate output vector
                height_data_out.resize(pixel_count);
        
                // parallel mapping of heights
                auto map_height = [&height_data_out, &height_data, bytes_per_pixel, min_y, max_y](uint32_t start_pixel, uint32_t end_pixel)
                {
                    for (uint32_t pixel = start_pixel; pixel < end_pixel; pixel++)
                    {
                        uint32_t byte_index    = pixel * bytes_per_pixel;
                        float normalized_value = static_cast<float>(height_data[byte_index]) / 255.0f;
                        height_data_out[pixel] = min_y + normalized_value * (max_y - min_y);
                    }
                };
                ThreadPool::ParallelLoop(map_height, pixel_count);
            }
        
            // second pass: smooth out the height map values, this will reduce hard terrain edges
            {
                const uint32_t width  = height_texture->GetWidth();
                const uint32_t height = height_texture->GetHeight();
        
                for (uint32_t iteration = 0; iteration < parameters::smoothing; iteration++)
                {
                    vector<float> smoothed_height_data = height_data_out; // create a copy to store the smoothed data
        
                    for (uint32_t y = 0; y < height; y++)
                    {
                        for (uint32_t x = 0; x < width; x++)
                        {
                            float sum      = height_data_out[y * width + x];
                            uint32_t count = 1;
        
                            // iterate over neighboring pixels
                            for (int ny = -1; ny <= 1; ++ny)
                            {
                                for (int nx = -1; nx <= 1; ++nx)
                                {
                                    // skip self/center pixel
                                    if (nx == 0 && ny == 0)
                                        continue;
        
                                    uint32_t neighbor_x = x + nx;
                                    uint32_t neighbor_y = y + ny;
        
                                    // check boundaries
                                    if (neighbor_x >= 0 && neighbor_x < width && neighbor_y >= 0 && neighbor_y < height)
                                    {
                                        sum += height_data_out[neighbor_y * width + neighbor_x];
                                        count++;
                                    }
                                }
                            }
        
                            // average the sum
                            smoothed_height_data[y * width + x] = sum / static_cast<float>(count);
                        }
                    }
        
                    height_data_out = smoothed_height_data;
                }
            }
        
            // optional third pass: create natural borders by raising edges to form mountains/walls with depth
            if (parameters::create_border)
            {
                const uint32_t width  = height_texture->GetWidth();
                const uint32_t height = height_texture->GetHeight();

                // border parameters (tweak these as needed)
                const uint32_t border_backface_width = 2;      // width of the thin outer edge to push down for backface creation (prevents see-through from outside)
                const uint32_t border_plateau_width  = 25;     // width of the flat plateau at max height inward from the backface
                const uint32_t border_blend_width    = 20;     // width over which to blend down from max height further inward (slope of the inner wall)
                const float border_height_max        = 150.0f; // maximum height to raise borders

                // create the borders
                auto apply_border = [&height_data_out, width, height, border_backface_width, border_plateau_width, border_blend_width, border_height_max, min_y](uint32_t start_index, uint32_t end_index)
                {
                    for (uint32_t index = start_index; index < end_index; index++)
                    {
                        uint32_t x = index % width;
                        uint32_t y = index / width;

                        // compute minimum distance to any edge
                        uint32_t dist_left    = x;
                        uint32_t dist_right   = width - 1 - x;
                        uint32_t dist_top     = y;
                        uint32_t dist_bottom  = height - 1 - y;
                        uint32_t min_dist     = min({dist_left, dist_right, dist_top, dist_bottom});
                        float height_increase = 0.0f;
                        if (min_dist < border_backface_width)
                        {
                            // push down the thin outer edge to create a backface (set to min_y for a sharp drop, preventing see-through)
                            height_data_out[index] = min_y;
                            continue; // skip further processing for these pixels
                        }
                        else if (min_dist < border_backface_width + border_plateau_width)
                        {
                            // flat plateau at max height inward from the backface
                            height_increase = border_height_max;
                        }
                        else if (min_dist < border_backface_width + border_plateau_width + border_blend_width)
                        {
                            // blend down inward from plateau to normal terrain
                            float blend     = 1.0f - static_cast<float>(min_dist - (border_backface_width + border_plateau_width)) / static_cast<float>(border_blend_width);
                            height_increase = blend * border_height_max;
                        }
                        // add to existing height for natural integration
                        height_data_out[index] += height_increase;
                    }
                };
                ThreadPool::ParallelLoop(apply_border, width * height);
            }
        }

        void densify_height_map(vector<float>& height_data, uint32_t width, uint32_t height, uint32_t density)
        {
            if (density <= 1)
                return; // no density increase needed
        
            // compute dense grid dimensions
            uint32_t dense_width  = density * (width - 1) + 1;
            uint32_t dense_height = density * (height - 1) + 1;
        
            // create new height map with denser grid
            vector<float> dense_height_data(dense_width * dense_height);
        
            // helper function to get height at integer coordinates
            auto get_height = [&height_data, width, height](uint32_t x, uint32_t y) -> float
            {
                x = min(x, width - 1);
                y = min(y, height - 1);
                return height_data[y * width + x];
            };
        
            // parallel computation of dense grid
            auto compute_dense_pixel = [&dense_height_data, &get_height, width, height, dense_width, dense_height, density](uint32_t start_index, uint32_t end_index)
            {
                for (uint32_t index = start_index; index < end_index; index++)
                {
                    uint32_t x = index % dense_width;
                    uint32_t y = index / dense_width;
        
                    // map to original height map coordinates (0 to width-1, 0 to height-1)
                    float u = static_cast<float>(x) / static_cast<float>(density);
                    float v = static_cast<float>(y) / static_cast<float>(density);
        
                    // integer and fractional parts for interpolation
                    uint32_t x0 = static_cast<uint32_t>(floor(u));
                    uint32_t x1 = min(x0 + 1, width - 1);
                    uint32_t y0 = static_cast<uint32_t>(floor(v));
                    uint32_t y1 = min(y0 + 1, height - 1);
                    float dx    = u - static_cast<float>(x0);
                    float dy    = v - static_cast<float>(y0);
        
                    // get heights at the four corners
                    float h00 = get_height(x0, y0);
                    float h10 = get_height(x1, y0);
                    float h01 = get_height(x0, y1);
                    float h11 = get_height(x1, y1);
        
                    // perform bilinear interpolation
                    float height = (1.0f - dx) * (1.0f - dy) * h00 +
                                   dx * (1.0f - dy) * h10 +
                                   (1.0f - dx) * dy * h01 +
                                   dx * dy * h11;
        
                    dense_height_data[y * dense_width + x] = height;
                }
            };
        
            ThreadPool::ParallelLoop(compute_dense_pixel, dense_width * dense_height);
        
            // replace original height data with denser grid
            height_data = move(dense_height_data);
        }

        void generate_positions(vector<Vector3>& positions, const vector<float>& height_map, const uint32_t width, const uint32_t height)
        {
            SP_ASSERT_MSG(!height_map.empty(), "Height map is empty");
        
            // pre-allocate positions vector
            positions.resize(width * height);
        
            // compute base dimensions (before density)
            uint32_t base_width  = (width - 1) / parameters::density + 1;
            uint32_t base_height = (height - 1) / parameters::density + 1;
        
            // apply scale to physical dimensions
            float extent_x = static_cast<float>(base_width - 1) * parameters::scale;
            float extent_z = static_cast<float>(base_height - 1) * parameters::scale;
        
            // scale coordinates to match physical dimensions
            float scale_x  = extent_x / static_cast<float>(width - 1);
            float scale_z  = extent_z / static_cast<float>(height - 1);
            float offset_x = extent_x / 2.0f;
            float offset_z = extent_z / 2.0f;
        
            // parallel generation of positions
            auto generate_position_range = [&positions, &height_map, width, height, scale_x, scale_z, offset_x, offset_z](uint32_t start_index, uint32_t end_index)
            {
                for (uint32_t index = start_index; index < end_index; index++)
                {
                    uint32_t x = index % width;
                    uint32_t y = index / width;
        
                    // scale coordinates
                    float scaled_x = static_cast<float>(x) * scale_x;
                    float scaled_z = static_cast<float>(y) * scale_z;
        
                    // center on x and z axes
                    float centered_x = scaled_x - offset_x;
                    float centered_z = scaled_z - offset_z;
        
                    // get height from height_map
                    float height_value = height_map[index];
        
                    positions[index] = Vector3(centered_x, height_value, centered_z);
                }
            };
        
            uint32_t total_positions = width * height;
            ThreadPool::ParallelLoop(generate_position_range, total_positions);
        }

        void apply_wind_erosion(vector<Vector3>& positions, uint32_t width, uint32_t height, float wind_strength = 0.3f)
        {
            // 3x3 gaussian kernel
            const float kernel[3][3] =
            {
                {0.0625f, 0.125f, 0.0625f},
                {0.125f,  0.25f,  0.125f},
                {0.0625f, 0.125f, 0.0625f}
            };
            const int kernel_size = 3;
            const int kernel_half = kernel_size / 2;
        
            // store original positions for reference
           vector<Vector3> temp_positions = positions;
           mutex positions_mutex;
        
            // sequential wind erosion
            for (uint32_t z = 0; z < height; ++z)
            {
                for (uint32_t x = 0; x < width; ++x)
                {
                    // skip borders to avoid out-of-bounds access
                    if (x < kernel_half || x >= width - kernel_half || z < kernel_half || z >= height - kernel_half)
                        continue;
        
                    // apply gaussian convolution
                    float new_height = 0.0f;
                    for (int kz = -kernel_half; kz <= kernel_half; ++kz)
                    {
                        for (int kx = -kernel_half; kx <= kernel_half; ++kx)
                        {
                            uint32_t idx = (x + kx) + (z + kz) * width;
                            new_height += temp_positions[idx].y * kernel[kz + kernel_half][kx + kernel_half];
                        }
                    }
        
                    // update height with wind strength (interpolate between original and convolved height)
                    uint32_t idx = x + z * width;
                    float original_height = positions[idx].y;
                    float smoothed_height = new_height;
                    float final_height = original_height + wind_strength * (smoothed_height - original_height);
        
                    // update
                    {
                        lock_guard<mutex> lock(positions_mutex);
                        positions[idx].y = final_height;
                    }
                }
            }
        }
        
        void apply_erosion(vector<Vector3>& positions, uint32_t width, uint32_t height, uint32_t iterations = 1'000'000, uint32_t wind_interval = 50'000)
        {
            // erosion parameters
            const float inertia          = 0.05f;
            const float capacity_factor  = 1.0f;
            const float min_slope        = 0.01f;
            const float deposition_rate  = 0.05f;
            const float erosion_rate     = 0.2f;
            const float evaporation_rate = 0.05f;
            const float gravity          = 4.0f;
            const uint32_t max_steps     = 30;
            const float wind_strength    = 0.3f;

            auto get_height = [&positions, width, height](float x, float z) -> float
            {
                int ix   = static_cast<int>(floor(x));
                int iz   = static_cast<int>(floor(z));
                float fx = x - static_cast<float>(ix);
                float fz = z - static_cast<float>(iz);
                ix       = clamp(ix, 0, static_cast<int>(width) - 2);
                iz       = clamp(iz, 0, static_cast<int>(height) - 2);
        
                float h00 = positions[static_cast<size_t>(iz) * width + ix].y;
                float h10 = positions[static_cast<size_t>(iz) * width + ix + 1].y;
                float h01 = positions[static_cast<size_t>(iz + 1) * width + ix].y;
                float h11 = positions[static_cast<size_t>(iz + 1) * width + ix + 1].y;
        
                float h0 = h00 + fx * (h10 - h00);
                float h1 = h01 + fx * (h11 - h01);

                return h0 + fz * (h1 - h0);
            };
        
            auto add_height = [&positions, width, height](float x, float z, float amount)
            {
                int ix   = static_cast<int>(floor(x));
                int iz   = static_cast<int>(floor(z));
                float fx = x - static_cast<float>(ix);
                float fz = z - static_cast<float>(iz);
                ix       = clamp(ix, 0, static_cast<int>(width) - 2);
                iz       = clamp(iz, 0, static_cast<int>(height) - 2);
            
                float w00 = (1.0f - fx) * (1.0f - fz);
                float w10 = fx * (1.0f - fz);
                float w01 = (1.0f - fx) * fz;
                float w11 = fx * fz;

                // limit how deep a height adjustment can be in a single step to avoid extreme spikes
                // ideally, you also want to adjust erosion paramters so that clamping is only needed for edge cases
                float max_step = 0.5f;
                auto apply     = [&](size_t idx, float delta)
                {
                    float& h  = positions[idx].y;
                    h        += clamp(delta, -max_step, max_step);
                    h         = max(h, parameters::level_sea - 10.0f);
                };
            
                apply(static_cast<size_t>(iz) * width + ix,         amount * w00);
                apply(static_cast<size_t>(iz) * width + ix + 1,     amount * w10);
                apply(static_cast<size_t>(iz + 1) * width + ix,     amount * w01);
                apply(static_cast<size_t>(iz + 1) * width + ix + 1, amount * w11);
            };

            auto get_gradient = [get_height](float x, float z) -> Vector2
            {
                float hx = (get_height(x + 1.0f, z) - get_height(x - 1.0f, z)) / 2.0f;
                float hz = (get_height(x, z + 1.0f) - get_height(x, z - 1.0f)) / 2.0f;

                return Vector2(hx, hz);
            };
        
            // random number generation
            mt19937 gen(random_device{}());
            uniform_real_distribution<float> dist_x(1.0f, static_cast<float>(width) - 2.0f);
            uniform_real_distribution<float> dist_z(1.0f, static_cast<float>(height) - 2.0f);
        
            for (uint32_t i = 0; i < iterations; ++i)
            {
                // apply wind erosion periodically
                if (i % wind_interval == 0 && i != 0)
                {
                    apply_wind_erosion(positions, width, height, wind_strength);
                }
        
                // hydraulic erosion: simulate a single droplet
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
        
                    float height     = get_height(pos_x, pos_z);
                    Vector2 gradient = get_gradient(pos_x, pos_z);
                    float slope      = gradient.Length();
                    if (slope < min_slope)
                    {
                        slope = min_slope;
                    }

                    // update direction
                    Vector2 new_dir = -gradient.Normalized();
                    dir             = (dir * inertia + new_dir * (1.0f - inertia)).Normalized();
        
                    // proposed new position
                    float new_x      = pos_x + dir.x;
                    float new_z      = pos_z + dir.y;
                    float new_height = get_height(new_x, new_z);
                    float delta_h    = new_height - height;
        
                    // if moving uphill or stuck, deposit a fraction and stop (reduces spikes)
                    if (delta_h >= 0.0f || dir.LengthSquared() < 0.0001f)
                    {
                        float deposit_amount = sediment * deposition_rate; // only deposit a fraction, discard rest (reduce spikes)
                        add_height(pos_x, pos_z, deposit_amount);
                        break;
                    }
        
                    // move droplet
                    pos_x = new_x;
                    pos_z = new_z;
        
                    // update speed
                    speed = sqrt(speed * speed - gravity * delta_h);  // delta_h is negative downhill
        
                    // compute capacity
                    float capacity = capacity_factor * water * speed * slope;
        
                    // erode or deposit
                    if (sediment > capacity)
                    {
                        float deposit_amount = (sediment - capacity) * deposition_rate;

                        add_height(pos_x, pos_z, deposit_amount);
                        sediment -= deposit_amount;
                    }
                    else
                    {
                        float erode_amount = (capacity - sediment) * erosion_rate;
                        erode_amount       = min(erode_amount, -delta_h); // can't erode more than height diff

                        add_height(pos_x, pos_z, -erode_amount);
                        sediment += erode_amount;
                    }
        
                    // evaporate water
                    water *= (1.0f - evaporation_rate);
                }
            }
        
            // final wind erosion pass
            apply_wind_erosion(positions, width, height, wind_strength);
        }

        void generate_vertices_and_indices(vector<RHI_Vertex_PosTexNorTan>& terrain_vertices, vector<uint32_t>& terrain_indices, const vector<Vector3>& positions, const uint32_t width, const uint32_t height)
        {
            SP_ASSERT_MSG(!positions.empty(), "Positions are empty");

            // offset that centers the mesh
            Vector3 offset = Vector3( -static_cast<float>(width) * 0.5f, 0.0f, -static_cast<float>(height) * 0.5f);

            uint32_t index = 0;
            uint32_t k     = 0;
            for (uint32_t y = 0; y < height - 1; y++)
            {
                for (uint32_t x = 0; x < width - 1; x++)
                {
                    Vector3 position = positions[index] + offset;

                    float u = static_cast<float>(x) / static_cast<float>(width - 1);
                    float v = static_cast<float>(y) / static_cast<float>(height - 1);

                    const uint32_t index_bottom_left  = y * width + x;
                    const uint32_t index_bottom_right = y * width + x + 1;
                    const uint32_t index_top_left     = (y + 1) * width + x;
                    const uint32_t index_top_right    = (y + 1) * width + x + 1;

                    // bottom right of quad
                    index           = index_bottom_right;
                    terrain_indices[k]      = index;
                    terrain_vertices[index] = RHI_Vertex_PosTexNorTan(positions[index], Vector2(u + 1.0f / (width - 1), v + 1.0f / (height - 1)));

                    // bottom left of quad
                    index           = index_bottom_left;
                    terrain_indices[k + 1]  = index;
                    terrain_vertices[index] = RHI_Vertex_PosTexNorTan(positions[index], Vector2(u, v + 1.0f / (height - 1)));

                    // top left of quad
                    index           = index_top_left;
                    terrain_indices[k + 2]  = index;
                    terrain_vertices[index] = RHI_Vertex_PosTexNorTan(positions[index], Vector2(u, v));

                    // bottom right of quad
                    index           = index_bottom_right;
                    terrain_indices[k + 3]  = index;
                    terrain_vertices[index] = RHI_Vertex_PosTexNorTan(positions[index], Vector2(u + 1.0f / (width - 1), v + 1.0f / (height - 1)));

                    // top left of quad
                    index           = index_top_left;
                    terrain_indices[k + 4]  = index;
                    terrain_vertices[index] = RHI_Vertex_PosTexNorTan(positions[index], Vector2(u, v));

                    // top right of quad
                    index           = index_top_right;
                    terrain_indices[k + 5]  = index;
                    terrain_vertices[index] = RHI_Vertex_PosTexNorTan(positions[index], Vector2(u + 1.0f / (width - 1), v));

                    k += 6; // next quad
                }
            }
        }

        void generate_normals(vector<RHI_Vertex_PosTexNorTan>& terrain_vertices, uint32_t width, uint32_t height)
        {
            SP_ASSERT_MSG(!terrain_vertices.empty(), "Vertices are empty");
        
            auto compute_vertex_data = [&](uint32_t start, uint32_t end)
            {
                for (uint32_t index = start; index < end; index++)
                {
                    uint32_t i = index % width;
                    uint32_t j = index / width;
        
                    // compute normal using gradients
                    float h_left, h_right, h_bottom, h_top;
        
                    // x-direction gradient
                    if (i == 0)
                    {
                        h_left  = terrain_vertices[j * width + i].pos[1];
                        h_right = terrain_vertices[j * width + i + 1].pos[1];
                    }
                    else if (i == width - 1)
                    {
                        h_left  = terrain_vertices[j * width + i - 1].pos[1];
                        h_right = terrain_vertices[j * width + i].pos[1];
                    } else
                    {
                        h_left  = terrain_vertices[j * width + i - 1].pos[1];
                        h_right = terrain_vertices[j * width + i + 1].pos[1];
                    }
                    float dh_dx = (h_right - h_left) / (i == 0 || i == width - 1 ? 1.0f : 2.0f);
        
                    // z-direction gradient
                    if (j == 0)
                    {
                        h_bottom = terrain_vertices[j * width + i].pos[1];
                        h_top    = terrain_vertices[(j + 1) * width + i].pos[1];
                    }
                    else if (j == height - 1)
                    {
                        h_bottom = terrain_vertices[(j - 1) * width + i].pos[1];
                        h_top    = terrain_vertices[j * width + i].pos[1];
                    }
                    else
                    {
                        h_bottom = terrain_vertices[(j - 1) * width + i].pos[1];
                        h_top    = terrain_vertices[(j + 1) * width + i].pos[1];
                    }
                    float dh_dz = (h_top - h_bottom) / (j == 0 || j == height - 1 ? 1.0f : 2.0f);

                    // normal
                    Vector3 normal(-dh_dx, 1.0f, -dh_dz);
                    normal.Normalize();
                    terrain_vertices[index].nor[0] = normal.x;
                    terrain_vertices[index].nor[1] = normal.y;
                    terrain_vertices[index].nor[2] = normal.z;
        
                    // tangent
                    Vector3 tangent(1.0f, 0.0f, 0.0f);
                    float proj  = Vector3::Dot(normal, tangent);
                    tangent     -= normal * proj; // Orthogonalize to normal
                    tangent.Normalize();
                    terrain_vertices[index].tan[0] = tangent.x;
                    terrain_vertices[index].tan[1] = tangent.y;
                    terrain_vertices[index].tan[2] = tangent.z;
                }
            };
        
            ThreadPool::ParallelLoop(compute_vertex_data, static_cast<uint32_t>(terrain_vertices.size()));
        }

        void apply_perlin_noise(vector<Vector3>& positions, uint32_t width, uint32_t height, float amplitude = 5.0f, float frequency = 0.01f, uint32_t octaves = 4, float persistence = 1.0f)
        {
            auto fade = [](float t) -> float
            {
                return t * t * t * (t * (t * 6 - 15) + 10); // 6t^5 - 15t^4 + 10t^3
            };
        
            auto lerp = [](float a, float b, float t) -> float
            {
                return a + t * (b - a);
            };
        
            // initialize permutation table and gradients
            vector<uint8_t> permutation(512);
            vector<Vector2> gradients(256);
            {
                mt19937 gen(random_device{}());
                uniform_real_distribution<float> dist(-1.0f, 1.0f);
        
                for (uint32_t i = 0; i < 256; ++i)
                {
                    permutation[i] = static_cast<uint8_t>(i);
        
                    // generate normalized gradient vectors
                    Vector2 grad(dist(gen), dist(gen));
                    gradients[i] = grad.Normalized();
                }
        
                for (uint32_t i = 0; i < 256; ++i)
                {
                    permutation[256 + i] = permutation[i];
                }
        
                // shuffle
                for (uint32_t i = 255; i > 0; --i)
                {
                    uniform_int_distribution<uint32_t> dist(0, i);
                    uint32_t j = dist(gen);
                    swap(permutation[i], permutation[j]);
                    permutation[256 + i] = permutation[i];
                }
            }
        
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
        
                float lerp_x0 = lerp(grad00, grad10, u);
                float lerp_x1 = lerp(grad01, grad11, u);
                return lerp(lerp_x0, lerp_x1, v); // returns in range ~[-1,1]
            };
        
            auto apply_noise = [&](uint32_t start_index, uint32_t end_index)
            {
                for (uint32_t index = start_index; index < end_index; ++index)
                {
                    uint32_t x = index % width;
                    uint32_t z = index / width;
        
                    float scaled_x = static_cast<float>(x) * frequency;
                    float scaled_z = static_cast<float>(z) * frequency;
        
                    float noise_value       = 0.0f;
                    float current_amplitude = amplitude;
                    float current_frequency = 1.0f;
                    float max_amplitude     = 0.0f;
        
                    for (uint32_t octave = 0; octave < octaves; ++octave)
                    {
                        float n = perlin_noise(scaled_x * current_frequency, scaled_z * current_frequency);
                        noise_value += n * current_amplitude;
        
                        max_amplitude += current_amplitude;
                        current_amplitude *= persistence;
                        current_frequency *= 2.0f;
                    }
        
                    noise_value /= max_amplitude; // normalize to [-1,1] range
                    positions[index].y += noise_value * amplitude; // apply final amplitude
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

    void Terrain::FindTransforms(const uint32_t tile_index, const TerrainProp terrain_prop, Entity* entity, const float density_fraction, const float scale, vector<Matrix>& transforms_out)
    {
        TerrainPropDescription description;

        if (terrain_prop == TerrainProp::Tree)
        {
            description.max_slope_angle_rad  = 45.0f * math::deg_to_rad;     // moderate slope
            description.min_spawn_height     = parameters::level_sea + 5.0f; // a bit above sea level
            description.max_spawn_height     = parameters::level_snow + 20;  // stop a bit above the snow
            description.min_scale            = scale * 0.4f;
            description.max_scale            = scale * 1.0f;
        }
        else if (terrain_prop == TerrainProp::Grass)
        {
            description.max_slope_angle_rad     = 45.0f * math::deg_to_rad;     // moderate slope for grass in snowy, high-altitude conditions
            description.align_to_surface_normal = true;                         // small plants align with terrain normal
            description.min_spawn_height        = parameters::level_sea + 5.0f; // a bit above sea level
            description.max_spawn_height        = parameters::level_snow;       // stop when snow shows up
            description.min_scale               = scale * 0.2f;
            description.max_scale               = scale * 1.2f;
        }
        else if (terrain_prop == TerrainProp::Flower)
        {
            description.max_slope_angle_rad     = 45.0f * math::deg_to_rad;     // moderate slope for grass in snowy, high-altitude conditions
            description.align_to_surface_normal = true;                         // small plants align with terrain normal
            description.min_spawn_height        = parameters::level_sea + 5.0f; // a bit above sea level
            description.max_spawn_height        = parameters::level_snow;       // stop when snow shows up
            description.min_scale               = scale * 0.2f;
            description.max_scale               = scale * 1.2f;
            description.instances_per_cluster   = 1000;                         // avg flowers per cluster
            description.cluster_radius          = 30.0f;                        // max spread radius in world units
        }
        else if (terrain_prop == TerrainProp::Rock)
        {
            description.max_slope_angle_rad     = 45.0f * math::deg_to_rad;      // steeper slope for rocks
            description.align_to_surface_normal = true;                          // rocks align with terrain normal
            description.min_spawn_height        = parameters::level_sea - 10.0f; // can spawn underwater
            description.max_spawn_height        = numeric_limits<float>::max();  // can spawn at any height
            description.min_scale               = scale * 0.1f;
            description.max_scale               = scale * 1.0f;
            description.scale_adjust_by_slope   = true;
        }
        else
        {
            SP_ASSERT_MSG(false, "Unknown terrain prop type for FindTransforms");
        }

        placement::find_transforms(
            description,
            density_fraction,
            tile_index,
            transforms_out
        );

        // counter-act any scaling since it's baked into the instance transforms and can be controled via scale_min and scale_max
        if (entity->GetScale() != Vector3::One && entity->GetScale() != Vector3::Zero)
        {
            Matrix root_scale_matrix = Matrix::CreateScale(Vector3::One / entity->GetScale());
            for (Matrix& t : transforms_out)
            {
                t *= root_scale_matrix;
            }
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

        uint32_t width = GetWidth();
        uint32_t height = GetHeight();
        uint32_t height_data_size = static_cast<uint32_t>(m_height_data.size());
        uint32_t vertex_count = static_cast<uint32_t>(m_vertices.size());
        uint32_t index_count = static_cast<uint32_t>(m_indices.size());
        uint32_t tile_count = static_cast<uint32_t>(m_tile_vertices.size());
        uint32_t triangle_data_count = static_cast<uint32_t>(placement::triangle_data.size());
        uint32_t offset_count = static_cast<uint32_t>(m_tile_offsets.size());
        file.write(reinterpret_cast<const char*>(&width), sizeof(uint32_t));
        file.write(reinterpret_cast<const char*>(&height), sizeof(uint32_t));
        file.write(reinterpret_cast<const char*>(&height_data_size), sizeof(uint32_t));
        file.write(reinterpret_cast<const char*>(&vertex_count), sizeof(uint32_t));
        file.write(reinterpret_cast<const char*>(&index_count), sizeof(uint32_t));
        file.write(reinterpret_cast<const char*>(&tile_count), sizeof(uint32_t));
        file.write(reinterpret_cast<const char*>(&triangle_data_count), sizeof(uint32_t));
        file.write(reinterpret_cast<const char*>(&offset_count), sizeof(uint32_t));
        file.write(reinterpret_cast<const char*>(m_height_data.data()), height_data_size * sizeof(float));
        file.write(reinterpret_cast<const char*>(m_vertices.data()), vertex_count * sizeof(RHI_Vertex_PosTexNorTan));
        file.write(reinterpret_cast<const char*>(m_indices.data()), index_count * sizeof(uint32_t));
        file.write(reinterpret_cast<const char*>(m_tile_offsets.data()), offset_count * sizeof(Vector3));

        for (const auto& [tile_id, tile_triangles] : placement::triangle_data)
        {
            file.write(reinterpret_cast<const char*>(&tile_id), sizeof(uint64_t));
            uint32_t triangle_count = static_cast<uint32_t>(tile_triangles.size());
            file.write(reinterpret_cast<const char*>(&triangle_count), sizeof(uint32_t));
            file.write(reinterpret_cast<const char*>(tile_triangles.data()), triangle_count * sizeof(placement::TriangleData));
        }

        for (uint32_t i = 0; i < tile_count; i++)
        {
            uint32_t vertex_size = static_cast<uint32_t>(m_tile_vertices[i].size());
            uint32_t index_size = static_cast<uint32_t>(m_tile_indices[i].size());
            file.write(reinterpret_cast<const char*>(&vertex_size), sizeof(uint32_t));
            file.write(reinterpret_cast<const char*>(&index_size), sizeof(uint32_t));
            file.write(reinterpret_cast<const char*>(m_tile_vertices[i].data()), vertex_size * sizeof(RHI_Vertex_PosTexNorTan));
            file.write(reinterpret_cast<const char*>(m_tile_indices[i].data()), index_size * sizeof(uint32_t));
        }

        file.close();
        SP_LOG_INFO("saved terrain to %s: width=%u, height=%u, height_data_size=%u, vertex_count=%u, index_count=%u, tile_count=%u, triangle_data_count=%u, offset_count=%u",
            file_path, width, height, height_data_size, vertex_count, index_count, tile_count, triangle_data_count, offset_count);
    }

    void Terrain::LoadFromFile(const char* file_path)
    {
        ifstream file(file_path, ios::binary);
        if (!file.is_open())
            return;

        uint32_t height_data_size = 0;
        uint32_t vertex_count = 0;
        uint32_t index_count = 0;
        uint32_t tile_count = 0;
        uint32_t triangle_data_count = 0;
        uint32_t offset_count = 0;
        file.read(reinterpret_cast<char*>(&m_width), sizeof(uint32_t));
        file.read(reinterpret_cast<char*>(&m_height), sizeof(uint32_t));
        file.read(reinterpret_cast<char*>(&height_data_size), sizeof(uint32_t));
        file.read(reinterpret_cast<char*>(&vertex_count), sizeof(uint32_t));
        file.read(reinterpret_cast<char*>(&index_count), sizeof(uint32_t));
        file.read(reinterpret_cast<char*>(&tile_count), sizeof(uint32_t));
        file.read(reinterpret_cast<char*>(&triangle_data_count), sizeof(uint32_t));
        file.read(reinterpret_cast<char*>(&offset_count), sizeof(uint32_t));
        if (tile_count > 10000 || offset_count > 10000)
        {
            SP_LOG_ERROR("invalid tile_count (%u) or offset_count (%u) read from file, aborting load", tile_count, offset_count);
            file.close();
            return;
        }

        m_height_data.resize(height_data_size);
        m_vertices.resize(vertex_count);
        m_indices.resize(index_count);
        m_tile_vertices.resize(tile_count);
        m_tile_indices.resize(tile_count);
        m_tile_offsets.resize(offset_count);
        placement::triangle_data.clear();
        file.read(reinterpret_cast<char*>(m_height_data.data()), height_data_size * sizeof(float));
        file.read(reinterpret_cast<char*>(m_vertices.data()), vertex_count * sizeof(RHI_Vertex_PosTexNorTan));
        file.read(reinterpret_cast<char*>(m_indices.data()), index_count * sizeof(uint32_t));
        file.read(reinterpret_cast<char*>(m_tile_offsets.data()), offset_count * sizeof(Vector3));
        for (uint32_t i = 0; i < triangle_data_count; i++)
        {
            uint64_t tile_id;
            uint32_t triangle_count;
            file.read(reinterpret_cast<char*>(&tile_id), sizeof(uint64_t));
            file.read(reinterpret_cast<char*>(&triangle_count), sizeof(uint32_t));
            vector<placement::TriangleData>& tile_triangles = placement::triangle_data[tile_id];
            tile_triangles.resize(triangle_count);
            file.read(reinterpret_cast<char*>(tile_triangles.data()), triangle_count * sizeof(placement::TriangleData));
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
        SP_LOG_INFO("loaded terrain from %s: width=%u, height=%u, height_data_size=%u, vertex_count=%u, index_count=%u, tile_count=%u, triangle_data_count=%u, offset_count=%u",
            file_path, m_width, m_height, height_data_size, vertex_count, index_count, tile_count, triangle_data_count, offset_count);
    }

    uint32_t Terrain::GetDensity() const
    {
        return parameters::density;
    }

    uint32_t Terrain::GetScale() const
    {
        return parameters::scale;
    }

    void Terrain::Generate()
    {
        // check if already generating
        if (m_is_generating)
        {
            SP_LOG_WARNING("terrain is already being generated, please wait...");
            return;
        }
    
        // check if height texture is assigned
        if (!m_height_map_seed)
        {
            SP_LOG_WARNING("you need to assign a height map before trying to generate a terrain");
            return;
        }
    
        m_is_generating = true;
    
        // start progress tracking
        uint32_t job_count = 10;
        ProgressTracker::GetProgress(ProgressType::Terrain).Start(job_count, "generating terrain...");
    
        // define cache file path
        const string cache_file = "terrain_cache.bin";
        bool loaded_from_cache  = false;
    
        // try to load from cache
        {
            LoadFromFile(cache_file.c_str());
            if (!m_vertices.empty())
            {
                loaded_from_cache = true;
                ProgressTracker::GetProgress(ProgressType::Terrain).SetText("loaded from cache, skipping to mesh creation...");
                for (uint32_t i = 0; i < job_count - 1; i++)
                {
                    ProgressTracker::GetProgress(ProgressType::Terrain).JobDone();
                }
            }
        }

        vector<Vector3> positions;
        uint32_t dense_width  = 0;
        uint32_t dense_height = 0;
        if (!loaded_from_cache)
        {
            SP_LOG_INFO("Terrain not found, generating from scratch...");
    
            // 1. process height map
            {
                ProgressTracker::GetProgress(ProgressType::Terrain).SetText("process height map...");
                get_values_from_height_map(m_height_data, m_height_map_seed, m_min_y, m_max_y);
                m_width  = m_height_map_seed->GetWidth();
                m_height = m_height_map_seed->GetHeight();
    
                // increase grid density
                densify_height_map(m_height_data, m_width, m_height, parameters::density);
                dense_width  = parameters::density * (m_width - 1) + 1;
                dense_height = parameters::density * (m_height - 1) + 1;
                ProgressTracker::GetProgress(ProgressType::Terrain).JobDone();
            }
    
            // 2. compute positions
            {
                ProgressTracker::GetProgress(ProgressType::Terrain).SetText("generating positions...");
                positions.resize(dense_width * dense_height);
                generate_positions(positions, m_height_data, dense_width, dense_height);
                ProgressTracker::GetProgress(ProgressType::Terrain).JobDone();
            }

            // 3. apply perlin noise
            {
                ProgressTracker::GetProgress(ProgressType::Terrain).SetText("applying Perlin noise...");
                apply_perlin_noise(positions, dense_width, dense_height);
                ProgressTracker::GetProgress(ProgressType::Terrain).JobDone();
            }

            // 4. apply hydraulic and wind erosion
            {
                ProgressTracker::GetProgress(ProgressType::Terrain).SetText("applying hydraulic and wind erosion...");
                apply_erosion(positions, dense_width, dense_height);
                ProgressTracker::GetProgress(ProgressType::Terrain).JobDone();
            }

            // 5. bake
            {
                ProgressTracker::GetProgress(ProgressType::Terrain).SetText("baking terrain into texture...");

                vector<RHI_Texture_Slice> data(1);
                auto& slice = data[0];
                slice.mips.resize(1);
                auto& mip_bytes = slice.mips[0].bytes;
                mip_bytes.resize(dense_width * dense_height * sizeof(float));

                auto copy_heights = [&positions, &mip_bytes](uint32_t start, uint32_t end)
                {
                    for (uint32_t i = start; i < end; i++)
                    {
                        memcpy(mip_bytes.data() + i * sizeof(float), &positions[i].y, sizeof(float));
                    }
                };
                ThreadPool::ParallelLoop(copy_heights, dense_width * dense_height);

                m_height_map_final = make_shared<RHI_Texture>(
                    RHI_Texture_Type::Type2D,
                    dense_width,
                    dense_height,
                    1,
                    1,
                    RHI_Format::R32_Float,
                    RHI_Texture_Srv,
                    "terrain_baked",
                    data
                );

                ProgressTracker::GetProgress(ProgressType::Terrain).JobDone();
            }
    
            // 6. compute vertices and indices
            {
                ProgressTracker::GetProgress(ProgressType::Terrain).SetText("generating vertices and indices...");
                m_vertices.resize(dense_width * dense_height);
                m_indices.resize((dense_width - 1) * (dense_height - 1) * 6);
                generate_vertices_and_indices(m_vertices, m_indices, positions, dense_width, dense_height);
                ProgressTracker::GetProgress(ProgressType::Terrain).JobDone();
            }
    
            // 7. compute normals and tangents
            {
                ProgressTracker::GetProgress(ProgressType::Terrain).SetText("generating normals...");
                generate_normals(m_vertices, dense_width, dense_height);
                ProgressTracker::GetProgress(ProgressType::Terrain).JobDone();
            }
    
            // 8. split into tiles
            {
                ProgressTracker::GetProgress(ProgressType::Terrain).SetText("splitting into tiles...");
                uint32_t tile_count = 16;
                geometry_processing::split_surface_into_tiles(m_vertices, m_indices, tile_count, m_tile_vertices, m_tile_indices, m_tile_offsets);
                ProgressTracker::GetProgress(ProgressType::Terrain).JobDone();
            }

            // 9 compute triangle data for placement
            {
                ProgressTracker::GetProgress(ProgressType::Terrain).SetText("computing triangle data for placement...");
                for (uint32_t tile_index = 0; tile_index < m_tile_vertices.size(); tile_index++)
                {
                    placement::compute_triangle_data(m_tile_vertices, m_tile_indices, tile_index);
                }
                ProgressTracker::GetProgress(ProgressType::Terrain).JobDone();
            }

            SaveToFile(cache_file.c_str());
        }
    
        // compute certain properties
        m_height_samples = dense_width * dense_height;
        m_vertex_count   = static_cast<uint32_t>(m_vertices.size());
        m_index_count    = static_cast<uint32_t>(m_indices.size());
        m_triangle_count = m_index_count / 3;
        m_area_km2       = compute_surface_area_km2(m_vertices, m_indices);

        // 9. create an entity for each tile and create a single gpu-buffer (for now)
        {
            ProgressTracker::GetProgress(ProgressType::Terrain).SetText("creating gpu mesh...");
            m_mesh = make_shared<Mesh>();
            m_mesh->SetObjectName("terrain_mesh");
            m_mesh->SetFlag(static_cast<uint32_t>(MeshFlags::PostProcessOptimize), false);            // meshes are build to spec, so don't mesh with them
            m_mesh->SetFlag(static_cast<uint32_t>(MeshFlags::PostProcessPreserveTerrainEdges), true); // preserve edges when generating lods, to ensure the tiles meet each other

            for (uint32_t tile_index = 0; tile_index < static_cast<uint32_t>(m_tile_vertices.size()); tile_index++)
            {
                uint32_t sub_mesh_index = 0;
                m_mesh->AddGeometry(m_tile_vertices[tile_index], m_tile_indices[tile_index], true, &sub_mesh_index);
                Entity* entity = World::CreateEntity();
                entity->SetObjectName("tile_" + to_string(tile_index + 1)); // +1 so it makes more sense in the editor
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
        }
    
        // clear everything but height and placement data
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
        ResourceCache::Remove(m_mesh);
        m_mesh = nullptr;

        for (Entity* child : m_entity_ptr->GetChildren())
        {
            if (Renderable* renderable = child->AddComponent<Renderable>())
            {
                renderable->SetMesh(nullptr);
            }
        }
    }
}
