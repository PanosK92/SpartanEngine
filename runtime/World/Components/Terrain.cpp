/*
Copyright(c) 2016-2025 Panos Karabelas

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
#include "../../IO/FileStream.h"
#include "../../Resource/ResourceCache.h"
#include "../../Rendering/Mesh.h"
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
        const float sea_level               = 0.0f;      // the height at which the sea level is 0.0f; // this is an axiom of the engine
        const uint32_t scale                = 3;         // the scale factor to upscale the height map by
        const uint32_t smoothing_iterations = 0;         // the number of height map neighboring pixel averaging - useful if you are reading the height map with a scale of 1 (no bilinear interpolation)
        const uint32_t tile_count           = 8 * scale; // the number of tiles in each dimension to split the terrain into
    }

    namespace
    {
        struct TriangleData
        {
            Vector3 normal;
            Vector3 v0;
            Vector3 v1_minus_v0;
            Vector3 v2_minus_v0;
            float slope_radians;
            float min_height;
            Quaternion rotation_to_normal;
        };
        static vector<TriangleData> triangle_data;

        void compute_triangle_data(const vector<RHI_Vertex_PosTexNorTan>& terrain_vertices, const vector<uint32_t>& terrain_indices)
        {
            uint32_t triangle_count = static_cast<uint32_t>(terrain_indices.size() / 3);
            triangle_data.resize(triangle_count);
        
            auto compute_triangle = [&terrain_vertices, &terrain_indices](uint32_t start_index, uint32_t end_index)
            {
                for (uint32_t i = start_index; i < end_index; i++)
                {
                    uint32_t idx0 = terrain_indices[i * 3];
                    uint32_t idx1 = terrain_indices[i * 3 + 1];
                    uint32_t idx2 = terrain_indices[i * 3 + 2];
        
                    Vector3 v0(terrain_vertices[idx0].pos[0], terrain_vertices[idx0].pos[1], terrain_vertices[idx0].pos[2]);
                    Vector3 v1(terrain_vertices[idx1].pos[0], terrain_vertices[idx1].pos[1], terrain_vertices[idx1].pos[2]);
                    Vector3 v2(terrain_vertices[idx2].pos[0], terrain_vertices[idx2].pos[1], terrain_vertices[idx2].pos[2]);
        
                    Vector3 normal                = Vector3::Cross(v1 - v0, v2 - v0).Normalized();
                    float slope_radians           = acos(Vector3::Dot(normal, Vector3::Up));
                    float min_height              = min({v0.y, v1.y, v2.y});
                    Vector3 v1_minus_v0           = v1 - v0;
                    Vector3 v2_minus_v0           = v2 - v0;
                    Quaternion rotation_to_normal = Quaternion::FromToRotation(Vector3::Up, normal);
        
                    triangle_data[i] = { normal, v0, v1_minus_v0, v2_minus_v0, slope_radians, min_height, rotation_to_normal };
                }
            };
        
            ThreadPool::ParallelLoop(compute_triangle, triangle_count);
        }

        vector<Matrix> find_transforms(uint32_t transform_count, float max_slope_radians, bool rotate_to_match_surface_normal, float terrain_offset, float min_height)
        {
            SP_ASSERT(!triangle_data.empty());
        
            // step 1: filter acceptable triangles using precomputed data
            vector<uint32_t> acceptable_triangles;
            {
                acceptable_triangles.reserve(triangle_data.size());
        
                for (uint32_t i = 0; i < triangle_data.size(); i++)
                {
                    if (triangle_data[i].slope_radians <= max_slope_radians && triangle_data[i].min_height >= min_height)
                    {
                        acceptable_triangles.push_back(i); // store triangle index
                    }
                }
        
                if (acceptable_triangles.empty())
                {
                    SP_LOG_WARNING("No acceptable triangles found for the given criteria.");
                    return {};
                }
            }
        
            // step 2: prepare output vector and mutex
            vector<Matrix> transforms;
            transforms.reserve(transform_count);
            mutex mtx;
        
            // step 3: parallel placement with local storage
            auto place_mesh = [&acceptable_triangles, &transforms, &mtx, rotate_to_match_surface_normal, terrain_offset](uint32_t start_index, uint32_t end_index)
            {
                thread_local mt19937 generator(random_device{}());
                uniform_int_distribution<> triangle_dist(0, static_cast<uint32_t>(acceptable_triangles.size() - 1));
                uniform_real_distribution<float> barycentric_dist(0.0f, 1.0f);
                uniform_real_distribution<float> angle_dist(0.0f, 360.0f);
        
                vector<Matrix> local_transforms;
                local_transforms.reserve(end_index - start_index);
        
                for (uint32_t i = start_index; i < end_index; i++)
                {
                    uint32_t tri_idx        = acceptable_triangles[triangle_dist(generator)];
                    const TriangleData& tri = triangle_data[tri_idx];
                
                    float u = barycentric_dist(generator);
                    float v = barycentric_dist(generator);
                    if (u + v > 1.0f)
                    {
                        u = 1.0f - u;
                        v = 1.0f - v;
                    }
                
                    Vector3 position             = tri.v0 + u * tri.v1_minus_v0 + v * tri.v2_minus_v0 + Vector3(0.0f, terrain_offset, 0.0f);
                    Quaternion rotate_to_normal  = rotate_to_match_surface_normal ? tri.rotation_to_normal : Quaternion::Identity;
                    Quaternion random_y_rotation = Quaternion::FromEulerAngles(0.0f, angle_dist(generator), 0.0f);
                    Quaternion rotation          = rotate_to_normal * random_y_rotation;
                    Matrix transform             = Matrix::CreateRotation(rotation) * Matrix::CreateTranslation(position); // simplified, since scale is 1.0
                    local_transforms.push_back(transform);
                }
        
                // merge local transforms into the shared vector
                lock_guard<mutex> lock(mtx);
                transforms.insert(transforms.end(), local_transforms.begin(), local_transforms.end());
            };
        
            // step 4: Execute parallel loop
            ThreadPool::ParallelLoop(place_mesh, transform_count);
        
            return transforms;
        }

        float compute_terrain_area_km2(const vector<RHI_Vertex_PosTexNorTan>& vertices)
        {
            if (vertices.empty())
                return 0.0f;
        
            // Initialize min and max values for x and z coordinates
            float min_x = numeric_limits<float>::max();
            float max_x = numeric_limits<float>::lowest();
            float min_z = numeric_limits<float>::max();
            float max_z = numeric_limits<float>::lowest();
        
            // iterate through all vertices to find the bounding box
            for (const auto& vertex : vertices)
            {
                float x = vertex.pos[0]; // x-coordinate
                float z = vertex.pos[2]; // z-coordinate
        
                // Update min and max values
                if (x < min_x) min_x = x;
                if (x > max_x) max_x = x;
                if (z < min_z) min_z = z;
                if (z > max_z) max_z = z;
            }
        
            // calculate width (x extent) and depth (z extent) in meters
            float width = max_x - min_x;
            float depth = max_z - min_z;
        
            // compute area in square meters
            float area_m2 = width * depth;
        
            // convert to square kilometers (1 km² = 1,000,000 m²)
            float area_km2 = area_m2 / 1000000.0f;
        
            return area_km2;
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
                        uint32_t byte_index = pixel * bytes_per_pixel;
                        float normalized_value = static_cast<float>(height_data[byte_index]) / 255.0f;
                        height_data_out[pixel] = min_y + normalized_value * (max_y - min_y);
                    }
                };
                ThreadPool::ParallelLoop(map_height, pixel_count);
            }
        
            // second pass: upscale the height map by bilinearly interpolating the height values (parallelized)
            if (parameters::scale > 1)
            {
                // get the dimensions of the original texture
                uint32_t width  = height_texture->GetWidth();
                uint32_t height = height_texture->GetHeight();
                uint32_t upscaled_width  = parameters::scale * width;
                uint32_t upscaled_height = parameters::scale * height;
        
                // create a new vector for the upscaled height map
                vector<float> upscaled_height_data(upscaled_width * upscaled_height);
        
                // helper function to safely access height values with clamping
                auto get_height = [&](uint32_t i, uint32_t j) {
                    i = min(i, width - 1);
                    j = min(j, height - 1);
                    return height_data_out[j * width + i];
                };
        
                // parallel upscaling of the height map
                auto upscale_pixel = [&upscaled_height_data, &get_height, width, height, upscaled_width, upscaled_height](uint32_t start_index, uint32_t end_index)
                {
                    for (uint32_t index = start_index; index < end_index; index++)
                    {
                        uint32_t x = index % upscaled_width;
                        uint32_t y = index / upscaled_width;
        
                        // compute texture coordinates (u, v) in the range [0, 1]
                        float u = (static_cast<float>(x) + 0.5f) / static_cast<float>(upscaled_width);
                        float v = (static_cast<float>(y) + 0.5f) / static_cast<float>(upscaled_height);
        
                        // map to original texture pixel coordinates
                        float i_float = u * static_cast<float>(width);
                        float j_float = v * static_cast<float>(height);
        
                        // determine the four surrounding pixel indices
                        uint32_t i0 = static_cast<uint32_t>(floor(i_float));
                        uint32_t i1 = min(i0 + 1, width - 1);
                        uint32_t j0 = static_cast<uint32_t>(floor(j_float));
                        uint32_t j1 = min(j0 + 1, height - 1);
        
                        // compute interpolation weights
                        float frac_i = i_float - static_cast<float>(i0);
                        float frac_j = j_float - static_cast<float>(j0);
        
                        // get the four height values
                        float val00 = get_height(i0, j0); // top-left
                        float val10 = get_height(i1, j0); // top-right
                        float val01 = get_height(i0, j1); // bottom-left
                        float val11 = get_height(i1, j1); // bottom-right
        
                        // perform bilinear interpolation
                        float interpolated = (1.0f - frac_i) * (1.0f - frac_j) * val00 +
                                             frac_i * (1.0f - frac_j) * val10 +
                                             (1.0f - frac_i) * frac_j * val01 +
                                             frac_i * frac_j * val11;
        
                        // store the interpolated value
                        upscaled_height_data[y * upscaled_width + x] = interpolated;
                    }
                };
                ThreadPool::ParallelLoop(upscale_pixel, upscaled_width * upscaled_height);
        
                // replace the original height data with the upscaled data
                height_data_out = move(upscaled_height_data);
            }
        
            // third pass: smooth out the height map values, this will reduce hard terrain edges
            {
                const uint32_t width  = height_texture->GetWidth()  * parameters::scale;
                const uint32_t height = height_texture->GetHeight() * parameters::scale;
        
                for (uint32_t iteration = 0; iteration < parameters::smoothing_iterations; iteration++)
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
        }

        void apply_hydraulic_erosion(vector<float>& height_data, uint32_t width, uint32_t height)
        { 
            const uint32_t droplet_count         = 50000;
            const float erosion_strength         = 1.0f; 
            const float deposition_strength      = 0.5f; 
            const float sediment_capacity_factor = 10.0f;
            const float min_slope                = 0.01f;
            const uint32_t max_lifetime          = 50;   
            const float inertia                  = 0.05f;
            const float gravity                  = 8.0f; 

            // Random number generation for droplet placement and movement
            thread_local mt19937 generator(random_device{}());
            uniform_real_distribution<float> random_float(0.0f, 1.0f);
            uniform_int_distribution<uint32_t> random_index(0, width * height - 1);

            // Temporary buffer to store height changes (to avoid race conditions)
            vector<float> height_changes(width * height, 0.0f);
            mutex height_changes_mutex;

            // Helper function to get height and gradient at a position
            auto get_height_and_gradient = [&](float x, float y, float& height, Vector2& gradient)
            {
                // Clamp coordinates to valid range
                uint32_t x0 = static_cast<uint32_t>(max(0.0f, min(x, static_cast<float>(width - 1))));
                uint32_t y0 = static_cast<uint32_t>(max(0.0f, min(y, static_cast<float>(height - 1))));
                uint32_t x1 = min<uint32_t>(x0 + 1, width - 1);
                uint32_t y1 = min<uint32_t>(y0 + 1, height - 1);

                // Bilinear interpolation for height
                float fx = x - static_cast<float>(x0);
                float fy = y - static_cast<float>(y0);
                float h00 = height_data[y0 * width + x0];
                float h10 = height_data[y0 * width + x1];
                float h01 = height_data[y1 * width + x0];
                float h11 = height_data[y1 * width + x1];
                height = (1.0f - fx) * (1.0f - fy) * h00 + fx * (1.0f - fy) * h10 +
                         (1.0f - fx) * fy * h01 + fx * fy * h11;

                // Compute gradients using finite differences
                gradient.x = (h10 - h00 + h11 - h01) * 0.5f;
                gradient.y = (h01 - h00 + h11 - h10) * 0.5f;
            };

            // Parallel droplet simulation
            auto simulate_droplet_batch = [&](uint32_t start, uint32_t end)
            {
                vector<float> local_height_changes(width * height, 0.0f);

                for (uint32_t i = start; i < end; i++)
                {
                    // Initialize droplet
                    Vector2 pos(random_float(generator) * (width - 1), random_float(generator) * (height - 1));
                    Vector2 dir(0.0f, 0.0f);
                    float speed = 0.0f;
                    float sediment = 0.0f;
                    uint32_t lifetime = 0;

                    while (lifetime < max_lifetime)
                    {
                        // Get current height and gradient
                        float height;
                        Vector2 gradient;
                        get_height_and_gradient(pos.x, pos.y, height, gradient);

                        // Update direction with inertia and gradient
                        dir = dir * inertia - gradient * (1.0f - inertia);
                        if (dir.LengthSquared() > 0.0f)
                            dir.Normalize();

                        // Update speed based on slope
                        float slope = gradient.Length();
                        speed = sqrt(max(0.0f, speed * speed + gravity * slope));

                        // Move droplet
                        Vector2 new_pos = pos + dir * speed;
                        if (new_pos.x < 0.0f || new_pos.x >= width - 1 || new_pos.y < 0.0f || new_pos.y >= height - 1)
                            break;

                        // Compute sediment capacity
                        float capacity = max(min_slope, slope) * speed * sediment_capacity_factor;

                        // Erode or deposit based on sediment capacity
                        float sediment_change = sediment - capacity;
                        float erode_amount = min(erosion_strength * slope, -sediment_change);
                        float deposit_amount = min(deposition_strength * sediment_change, sediment);

                        // Update sediment
                        sediment += erode_amount - deposit_amount;

                        // Apply height change at current position
                        uint32_t x0 = static_cast<uint32_t>(pos.x);
                        uint32_t y0 = static_cast<uint32_t>(pos.y);
                        if (x0 < width && y0 < height)
                        {
                            local_height_changes[y0 * width + x0] -= erode_amount;
                            local_height_changes[y0 * width + x0] += deposit_amount;
                        }

                        // Move to new position
                        pos = new_pos;
                        lifetime++;

                        // Stop if speed is too low
                        if (speed < 0.01f)
                            break;
                    }
                }

                // Merge local height changes into global buffer
                lock_guard<mutex> lock(height_changes_mutex);
                for (uint32_t i = 0; i < width * height; i++)
                {
                    height_changes[i] += local_height_changes[i];
                }
            };

            // Run droplet simulation in parallel
            ThreadPool::ParallelLoop(simulate_droplet_batch, droplet_count);

            // Apply height changes to height_data
            auto apply_height_changes = [&](uint32_t start, uint32_t end)
            {
                for (uint32_t i = start; i < end; i++)
                {
                    height_data[i] += height_changes[i];
                    // Clamp height to valid range
                    height_data[i] = clamp(height_data[i], -1000.0f, 1000.0f);
                }
            };
            ThreadPool::ParallelLoop(apply_height_changes, width * height);
        }

        void generate_positions(vector<Vector3>& positions, const vector<float>& height_map, const uint32_t width, const uint32_t height)
        {
            SP_ASSERT_MSG(!height_map.empty(), "Height map is empty");
        
            // pre-allocate positions vector
            positions.resize(width * height);
        
            // parallel generation of positions
            auto generate_position_range = [&positions, &height_map, width, height](uint32_t start_index, uint32_t end_index)
            {
                for (uint32_t index = start_index; index < end_index; index++)
                {
                    // convert flat index to x,y coordinates
                    uint32_t x = index % width;
                    uint32_t y = index / width;
        
                    // center on the x and z axis
                    float centered_x = static_cast<float>(x) - width * 0.5f;
                    float centered_z = static_cast<float>(y) - height * 0.5f;
        
                    // get height from height_map
                    float height_value = height_map[index];
        
                    // assign position (no mutex needed since each index is unique)
                    positions[index] = Vector3(centered_x, height_value, centered_z);
                }
            };
        
            // calculate total number of positions and run parallel loop
            uint32_t total_positions = width * height;
            ThreadPool::ParallelLoop(generate_position_range, total_positions);
        }

        void generate_vertices_and_indices(vector<RHI_Vertex_PosTexNorTan>& terrain_vertices, vector<uint32_t>& terrain_indices, const vector<Vector3>& positions, const uint32_t width, const uint32_t height)
        {
            SP_ASSERT_MSG(!positions.empty(), "Positions are empty");

            Vector3 offset = Vector3::Zero;
            {
                // calculate offsets to center the terrain
                float offset_x   = -static_cast<float>(width) * 0.5f;
                float offset_z   = -static_cast<float>(height) * 0.5f;
                float min_height = FLT_MAX;

                // find the minimum height to align the lower part of the terrain at y = 0
                for (const Vector3& pos : positions)
                {
                    if (pos.y < min_height)
                    {
                        min_height = pos.y;
                    }
                }

                offset = Vector3(offset_x, -min_height, offset_z);
            }

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

        void generate_normals(const vector<uint32_t>& /*terrain_indices*/, vector<RHI_Vertex_PosTexNorTan>& terrain_vertices, uint32_t width, uint32_t height)
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
    }

    Terrain::Terrain(Entity* entity) : Component(entity)
    {
        m_material = make_shared<Material>();
        m_material->SetObjectName("terrain");
    }

    Terrain::~Terrain()
    {
        m_height_texture = nullptr;
    }

    uint32_t Terrain::GetWidth() const
    {
        return m_height_texture->GetWidth() * parameters::scale;
    }

    uint32_t Terrain::GetHeight() const
    {
        return m_height_texture->GetHeight() * parameters::scale;
    }

    void Terrain::GenerateTransforms(vector<Matrix>* transforms, const uint32_t count, const TerrainProp terrain_prop, float offset_y)
    {
        bool rotate_match_surface_normal = false; // don't rotate to match the surface normal
        float max_slope                  = 0.0f;  // don't allow slope
        float terrain_offset             = 0.0f;  // place exactly on the terrain
        float min_height                 = 0.0f;  // spawn at sea level= 0.0f; // spawn at sea level
    
        if (terrain_prop == TerrainProp::Tree)
        {
            max_slope                   = 30.0f * math::deg_to_rad;
            terrain_offset              = offset_y; // push the tree slightly into the ground
            min_height                  = 6.0f;
        }
    
        if (terrain_prop == TerrainProp::Grass)
        {
            max_slope                   = 40.0f * math::deg_to_rad;
            rotate_match_surface_normal = true; // small plants tend to grow towards the sun but they can have some wonky angles
            min_height                  = 0.5f;
        }
    
        *transforms = find_transforms(count, max_slope, rotate_match_surface_normal, terrain_offset, min_height);
    }

    void Terrain::SaveToFile(const char* file_path)
    {
        // open file for writing
        ofstream file(file_path, ios::binary);
        if (!file.is_open())
        {
            SP_LOG_ERROR("failed to open file for writing: %s", file_path);
            return;
        }
    
        // write all sizes first
        uint32_t width            = GetWidth();
        uint32_t height           = GetHeight();
        uint32_t height_data_size = static_cast<uint32_t>(m_height_data.size());
        uint32_t vertex_count     = static_cast<uint32_t>(m_vertices.size());
        uint32_t index_count      = static_cast<uint32_t>(m_indices.size());
        uint32_t tile_count       = static_cast<uint32_t>(m_tile_vertices.size());
        uint32_t placement_count  = static_cast<uint32_t>(triangle_data.size());
    
        file.write(reinterpret_cast<const char*>(&width), sizeof(uint32_t));
        file.write(reinterpret_cast<const char*>(&height), sizeof(uint32_t));
        file.write(reinterpret_cast<const char*>(&height_data_size), sizeof(uint32_t));
        file.write(reinterpret_cast<const char*>(&vertex_count), sizeof(uint32_t));
        file.write(reinterpret_cast<const char*>(&index_count), sizeof(uint32_t));
        file.write(reinterpret_cast<const char*>(&tile_count), sizeof(uint32_t));
        file.write(reinterpret_cast<const char*>(&placement_count), sizeof(uint32_t));
    
        // write vector data
        file.write(reinterpret_cast<const char*>(m_height_data.data()), height_data_size * sizeof(float));
        file.write(reinterpret_cast<const char*>(m_vertices.data()), vertex_count * sizeof(RHI_Vertex_PosTexNorTan));
        file.write(reinterpret_cast<const char*>(m_indices.data()), index_count * sizeof(uint32_t));
        file.write(reinterpret_cast<const char*>(triangle_data.data()), placement_count * sizeof(TriangleData));
    
        // write tile data
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
    
        // log save operation
        SP_LOG_INFO("saved terrain to %s: width=%u, height=%u, height_data_size=%u, vertex_count=%u, index_count=%u, tile_count=%u",
                    file_path, width, height, height_data_size, vertex_count, index_count, tile_count);
    }
    
    void Terrain::LoadFromFile(const char* file_path)
    {
        // open file for reading
        ifstream file(file_path, ios::binary);
        if (!file.is_open())
            return;
    
        // read all sizes first
        uint32_t width            = 0;
        uint32_t height           = 0;
        uint32_t height_data_size = 0;
        uint32_t vertex_count     = 0;
        uint32_t index_count      = 0;
        uint32_t tile_count       = 0;
        uint32_t placement_count  = 0;
    
        file.read(reinterpret_cast<char*>(&width), sizeof(uint32_t));
        file.read(reinterpret_cast<char*>(&height), sizeof(uint32_t));
        file.read(reinterpret_cast<char*>(&height_data_size), sizeof(uint32_t));
        file.read(reinterpret_cast<char*>(&vertex_count), sizeof(uint32_t));
        file.read(reinterpret_cast<char*>(&index_count), sizeof(uint32_t));
        file.read(reinterpret_cast<char*>(&tile_count), sizeof(uint32_t));
        file.read(reinterpret_cast<char*>(&placement_count), sizeof(uint32_t));
    
        // sanity check
        if (tile_count > 10000) // adjust as needed
        {
            SP_LOG_ERROR("invalid tile_count (%u) read from file, aborting load", tile_count);
            file.close();
            return;
        }
    
        // resize vectors based on saved sizes
        m_height_data.resize(height_data_size);
        m_vertices.resize(vertex_count);
        m_indices.resize(index_count);
        m_tile_vertices.resize(tile_count);
        m_tile_indices.resize(tile_count);
        triangle_data.resize(placement_count);
    
        // read vector data
        file.read(reinterpret_cast<char*>(m_height_data.data()), height_data_size * sizeof(float));
        file.read(reinterpret_cast<char*>(m_vertices.data()), vertex_count * sizeof(RHI_Vertex_PosTexNorTan));
        file.read(reinterpret_cast<char*>(m_indices.data()), index_count * sizeof(uint32_t));
        file.read(reinterpret_cast<char*>(triangle_data.data()), placement_count * sizeof(TriangleData));
    
        // read tile data
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
    
        // log load operation
        SP_LOG_INFO("loaded terrain from %s: width=%u, height=%u, height_data_size=%u, vertex_count=%u, index_count=%u, tile_count=%u",
                    file_path, width, height, height_data_size, vertex_count, index_count, tile_count);
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
        if (!m_height_texture)
        {
            SP_LOG_WARNING("you need to assign a height map before trying to generate a terrain");
            return;
        }
    
        m_is_generating = true;
    
        // start progress tracking
        uint32_t job_count = 8;
        ProgressTracker::GetProgress(ProgressType::Terrain).Start(job_count, "generating terrain...");
    
        uint32_t width  = 0;
        uint32_t height = 0;
        vector<Vector3> positions;
    
        // define cache file path
        const string cache_file = "terrain_cache.bin";
        bool loaded_from_cache  = false;
    
        // try to load from cache
        {
            LoadFromFile(cache_file.c_str());
            if (!m_vertices.empty())
            {
                loaded_from_cache = true;
                width             = GetWidth();
                height            = GetHeight();
    
                // skip to step 9
                ProgressTracker::GetProgress(ProgressType::Terrain).SetText("loaded from cache, skipping to mesh creation...");
                for (uint32_t i = 0; i < job_count - 1; i++)
                {
                    ProgressTracker::GetProgress(ProgressType::Terrain).JobDone();
                }
            }
        }
    
        if (!loaded_from_cache)
        {
            SP_LOG_INFO("Terrain not found, generating from scratch...");

            // 1. process height map
            {
                ProgressTracker::GetProgress(ProgressType::Terrain).SetText("process height map...");
                get_values_from_height_map(m_height_data, m_height_texture, m_min_y, m_max_y);
                width = GetWidth();
                height = GetHeight();
                ProgressTracker::GetProgress(ProgressType::Terrain).JobDone();
            }

           // 2. Apply hydraulic erosion
            {
                ProgressTracker::GetProgress(ProgressType::Terrain).SetText("applying hydraulic erosion...");
                apply_hydraulic_erosion(m_height_data, width, height);
                ProgressTracker::GetProgress(ProgressType::Terrain).JobDone();
            }

            // 3. compute positions
            {
                ProgressTracker::GetProgress(ProgressType::Terrain).SetText("generating positions...");
                positions.resize(width * height);
                generate_positions(positions, m_height_data, width, height);
                ProgressTracker::GetProgress(ProgressType::Terrain).JobDone();
            }
    
            // 4. compute vertices and indices
            {
                ProgressTracker::GetProgress(ProgressType::Terrain).SetText("generating vertices and indices...");
                m_vertices.resize(width * height);
                m_indices.resize(width * height * 6);
                generate_vertices_and_indices(m_vertices, m_indices, positions, width, height);
                ProgressTracker::GetProgress(ProgressType::Terrain).JobDone();
            }
    
            // 5. compute normals and tangents
            {
                ProgressTracker::GetProgress(ProgressType::Terrain).SetText("generating normals...");
                generate_normals(m_indices, m_vertices, width, height);
                ProgressTracker::GetProgress(ProgressType::Terrain).JobDone();
            }

            // 6. compute triangle data for placement
            {
                ProgressTracker::GetProgress(ProgressType::Terrain).SetText("computing triangle data for placement...");
                compute_triangle_data(m_vertices, m_indices);
                ProgressTracker::GetProgress(ProgressType::Terrain).JobDone();
            }
    
            // 7. split into tiles
            {
                ProgressTracker::GetProgress(ProgressType::Terrain).SetText("splitting into tiles...");
                spartan::geometry_processing::split_surface_into_tiles(m_vertices, m_indices, parameters::tile_count, m_tile_vertices, m_tile_indices);
                ProgressTracker::GetProgress(ProgressType::Terrain).JobDone();
    
                // save to cache file
                SaveToFile(cache_file.c_str());
            }
        }
    
        // initialize members
        m_height_samples = width * height;
        m_vertex_count   = static_cast<uint32_t>(m_vertices.size());
        m_index_count    = static_cast<uint32_t>(m_indices.size());
        m_triangle_count = m_index_count / 3;
    
        // 9. create a mesh for each tile
        {
            ProgressTracker::GetProgress(ProgressType::Terrain).SetText("creating gpu mesh...");
    
            m_mesh = make_shared<Mesh>();
            m_mesh->SetObjectName("terrain_mesh");
            m_mesh->SetFlag(static_cast<uint32_t>(MeshFlags::PostProcessOptimize), false); // the geometry was optimized at step 6, don't do it again
    
            // accumulate geometry from all tiles
            for (uint32_t tile_index = 0; tile_index < static_cast<uint32_t>(m_tile_vertices.size()); tile_index++)
            {
                // update with geometry
                uint32_t sub_mesh_index = 0;
                m_mesh->AddGeometry(m_tile_vertices[tile_index], m_tile_indices[tile_index], true, &sub_mesh_index);
    
                // create a child entity, add a renderable, and this mesh tile to it
                {
                    shared_ptr<Entity> entity = World::CreateEntity();
                    entity->SetObjectName("tile_" + to_string(tile_index));
                    entity->SetParent(World::GetEntityById(m_entity_ptr->GetObjectId()));
    
                    if (Renderable* renderable = entity->AddComponent<Renderable>())
                    {
                        renderable->SetMesh(m_mesh.get(), sub_mesh_index);
                        renderable->SetMaterial(m_material);
                    }
                }
            }
    
            m_mesh->CreateGpuBuffers();
    
            ProgressTracker::GetProgress(ProgressType::Terrain).JobDone();
        }
    
        m_area_km2 = compute_terrain_area_km2(m_vertices);
        m_is_generating = false;
    
        // clear everything but the height and placement data (they are used for physics and for placing foliage)
        m_vertices.clear();
        m_indices.clear();
        m_tile_vertices.clear();
        m_tile_indices.clear();
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
