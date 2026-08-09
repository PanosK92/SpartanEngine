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

//= INCLUDES ============================
#include "pch.h"
#include "TerrainSystem.h"
#include "../RHI/RHI_Texture.h"
#include "../Core/ThreadPool.h"
#include <random>
#include <algorithm>
#include <cmath>
//=======================================

//= NAMESPACES ===============
using namespace std;
using namespace spartan::math;
//============================

namespace spartan
{
    namespace
    {
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
                {
                    temp_heights[i] = positions[i].y;
                }
            };
            ThreadPool::ParallelLoop(copy_heights, static_cast<uint32_t>(positions.size()));

            auto apply_blur = [&](uint32_t start_idx, uint32_t end_idx)
            {
                for (uint32_t idx = start_idx; idx < end_idx; idx++)
                {
                    uint32_t x = idx % width;
                    uint32_t z = idx / width;

                    if (x < 1 || x >= width - 1 || z < 1 || z >= height - 1)
                    {
                        continue;
                    }

                    float new_height = 0.0f;
                    for (int kz = -1; kz <= 1; ++kz)
                    {
                        for (int kx = -1; kx <= 1; ++kx)
                        {
                            new_height += temp_heights[(z + kz) * width + (x + kx)] * kernel[(kz + 1) * 3 + (kx + 1)];
                        }
                    }

                    positions[idx].y += wind_strength * (new_height - positions[idx].y);
                }
            };

            ThreadPool::ParallelLoop(apply_blur, width * height);
        }

        float brush_falloff_weight(float distance, float radius, float falloff)
        {
            if (radius <= 0.0f || distance >= radius)
            {
                return 0.0f;
            }

            float t = distance / radius;
            float soft = 1.0f - t;
            soft = soft * soft * (3.0f - 2.0f * soft);
            float hard = 1.0f;
            float f = clamp(falloff, 0.0f, 1.0f);
            return lerp(hard, soft, f);
        }
    }

        float TerrainSystem::ComputeSurfaceAreaKm2(const vector<RHI_Vertex_PosTexNorTan>& vertices, const vector<uint32_t>& indices)
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
        
        void TerrainSystem::GetValuesFromHeightMap(
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
                                {
                                    continue;
                                }
                                
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

        void TerrainSystem::DensifyHeightMap(vector<float>& height_data, uint32_t width, uint32_t height, uint32_t density)
        {
            if (density <= 1)
            {
                return;
            }
        
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

        void TerrainSystem::GeneratePositions(vector<Vector3>& positions, const vector<float>& height_map, uint32_t width, uint32_t height, uint32_t density, uint32_t scale)
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

        void TerrainSystem::ApplyErosion(vector<Vector3>& positions, uint32_t width, uint32_t height, float level_sea, uint32_t iterations, uint32_t wind_interval)
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
                {
                    apply_wind_erosion(positions, width, height, wind_strength);
                }
        
                float pos_x    = dist_x(gen);
                float pos_z    = dist_z(gen);
                Vector2 dir    = Vector2::Zero;
                float speed    = 1.0f;
                float water    = 1.0f;
                float sediment = 0.0f;
        
                for (uint32_t step = 0; step < max_steps; ++step)
                {
                    if (water < 0.01f)
                    {
                        break;
                    }
        
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

        void TerrainSystem::GenerateVerticesAndIndices(vector<RHI_Vertex_PosTexNorTan>& vertices, vector<uint32_t>& indices, const vector<Vector3>& positions, uint32_t width, uint32_t height)
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

        void TerrainSystem::GenerateNormals(vector<RHI_Vertex_PosTexNorTan>& vertices, uint32_t width, uint32_t height)
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
                        vertices[vertex_idx].set_normal(Vector3(nx, ny, nz));

                        float proj      = nx;
                        float tx        = 1.0f - nx * proj, ty = -ny * proj, tz = -nz * proj;
                        float t_inv_len = 1.0f / sqrtf(tx * tx + ty * ty + tz * tz);
                        vertices[vertex_idx].set_tangent(Vector3(tx * t_inv_len, ty * t_inv_len, tz * t_inv_len));
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
                    vertices[index].set_normal(Vector3(nx, ny, nz));

                    float proj      = nx;
                    float tx        = 1.0f - nx * proj, ty = -ny * proj, tz = -nz * proj;
                    float t_inv_len = 1.0f / sqrtf(tx * tx + ty * ty + tz * tz);
                    vertices[index].set_tangent(Vector3(tx * t_inv_len, ty * t_inv_len, tz * t_inv_len));
                }
            };
            ThreadPool::ParallelLoop(compute_edges, edge_count);
        }

        void TerrainSystem::ApplyPerlinNoise(vector<Vector3>& positions, uint32_t width, uint32_t height, float amplitude, float frequency, uint32_t octaves, float persistence)
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
    

    TerrainGridMapping TerrainSystem::ComputeGridMapping(
        uint32_t dense_width,
        uint32_t dense_height,
        uint32_t density,
        uint32_t scale
    )
    {
        TerrainGridMapping mapping;
        uint32_t dens = max(density, 1u);
        uint32_t base_width  = (dense_width - 1) / dens + 1;
        uint32_t base_height = (dense_height - 1) / dens + 1;
        mapping.extent_x = static_cast<float>(base_width - 1) * static_cast<float>(scale);
        mapping.extent_z = static_cast<float>(base_height - 1) * static_cast<float>(scale);
        mapping.scale_x  = mapping.extent_x / static_cast<float>(dense_width - 1);
        mapping.scale_z  = mapping.extent_z / static_cast<float>(dense_height - 1);
        mapping.offset_x = mapping.extent_x / 2.0f;
        mapping.offset_z = mapping.extent_z / 2.0f;
        return mapping;
    }

    void TerrainSystem::CreateFlatHeightfield(
        vector<float>& height_data_out,
        vector<Vector3>& positions_out,
        uint32_t base_width,
        uint32_t base_height,
        uint32_t density,
        uint32_t scale,
        float sea_level
    )
    {
        density = max(density, 1u);
        base_width  = max(base_width, 2u);
        base_height = max(base_height, 2u);

        height_data_out.assign(base_width * base_height, sea_level);
        DensifyHeightMap(height_data_out, base_width, base_height, density);

        uint32_t dense_width  = density * (base_width - 1) + 1;
        uint32_t dense_height = density * (base_height - 1) + 1;
        GeneratePositions(positions_out, height_data_out, dense_width, dense_height, density, scale);
    }

    void TerrainSystem::SyncHeightDataFromPositions(
        vector<float>& height_data,
        const vector<Vector3>& positions
    )
    {
        height_data.resize(positions.size());
        auto copy = [&](uint32_t start, uint32_t end)
        {
            for (uint32_t i = start; i < end; i++)
            {
                height_data[i] = positions[i].y;
            }
        };
        ThreadPool::ParallelLoop(copy, static_cast<uint32_t>(positions.size()));
    }

    float TerrainSystem::SampleHeight(
        const vector<Vector3>& positions,
        uint32_t width,
        uint32_t height,
        float world_x,
        float world_z,
        const TerrainGridMapping& mapping
    )
    {
        if (positions.empty() || width < 2 || height < 2)
        {
            return 0.0f;
        }

        float gx = (world_x + mapping.offset_x) / mapping.scale_x;
        float gz = (world_z + mapping.offset_z) / mapping.scale_z;
        int ix   = clamp(static_cast<int>(floor(gx)), 0, static_cast<int>(width) - 2);
        int iz   = clamp(static_cast<int>(floor(gz)), 0, static_cast<int>(height) - 2);
        float fx = gx - static_cast<float>(ix);
        float fz = gz - static_cast<float>(iz);

        float h00 = positions[static_cast<size_t>(iz) * width + ix].y;
        float h10 = positions[static_cast<size_t>(iz) * width + ix + 1].y;
        float h01 = positions[static_cast<size_t>(iz + 1) * width + ix].y;
        float h11 = positions[static_cast<size_t>(iz + 1) * width + ix + 1].y;

        return (h00 + fx * (h10 - h00)) + fz * ((h01 + fx * (h11 - h01)) - (h00 + fx * (h10 - h00)));
    }

    Vector3 TerrainSystem::SampleNormal(
        const vector<Vector3>& positions,
        uint32_t width,
        uint32_t height,
        float world_x,
        float world_z,
        const TerrainGridMapping& mapping
    )
    {
        if (positions.empty() || width < 2 || height < 2)
        {
            return Vector3::Up;
        }

        const float step_x = max(mapping.scale_x, epsilon);
        const float step_z = max(mapping.scale_z, epsilon);

        const float h_left = SampleHeight(
            positions, width, height, world_x - step_x, world_z, mapping
        );
        const float h_right = SampleHeight(
            positions, width, height, world_x + step_x, world_z, mapping
        );
        const float h_bottom = SampleHeight(
            positions, width, height, world_x, world_z - step_z, mapping
        );
        const float h_top = SampleHeight(
            positions, width, height, world_x, world_z + step_z, mapping
        );

        const float dh_dx = (h_right - h_left) / (2.0f * step_x);
        const float dh_dz = (h_top - h_bottom) / (2.0f * step_z);

        Vector3 normal(-dh_dx, 1.0f, -dh_dz);
        if (normal.LengthSquared() < epsilon)
        {
            return Vector3::Up;
        }

        normal.Normalize();
        return normal;
    }

    bool TerrainSystem::RaycastHeightfield(
        const Ray& ray,
        const vector<Vector3>& positions,
        uint32_t width,
        uint32_t height,
        const TerrainGridMapping& mapping,
        Vector3& hit_out,
        float max_distance
    )
    {
        if (positions.empty() || width < 2 || height < 2)
        {
            return false;
        }

        // camera picking stores a far-plane world point in direction
        Vector3 direction = ray.GetDirection() - ray.GetStart();
        if (direction.LengthSquared() < epsilon)
        {
            direction = ray.GetDirection();
        }
        if (direction.LengthSquared() < epsilon)
        {
            return false;
        }
        direction.Normalize();

        const float step = max(mapping.scale_x, mapping.scale_z) * 0.5f;
        float t = 0.0f;
        bool was_above = true;

        while (t <= max_distance)
        {
            Vector3 p = ray.GetStart() + direction * t;
            float h = SampleHeight(positions, width, height, p.x, p.z, mapping);
            bool above = p.y >= h;

            if (was_above && !above)
            {
                float t0 = max(0.0f, t - step);
                float t1 = t;
                for (int i = 0; i < 8; i++)
                {
                    float tm = (t0 + t1) * 0.5f;
                    Vector3 pm = ray.GetStart() + direction * tm;
                    float hm = SampleHeight(positions, width, height, pm.x, pm.z, mapping);
                    if (pm.y >= hm)
                    {
                        t0 = tm;
                    }
                    else
                    {
                        t1 = tm;
                    }
                }
                hit_out = ray.GetStart() + direction * t1;
                hit_out.y = SampleHeight(positions, width, height, hit_out.x, hit_out.z, mapping);
                return true;
            }

            was_above = above;
            t += step;
        }

        return false;
    }

    void TerrainSystem::ApplyBrush(
        vector<Vector3>& positions,
        vector<float>* height_data,
        uint32_t width,
        uint32_t height,
        const TerrainGridMapping& mapping,
        const Vector3& world_center,
        const TerrainBrush& brush
    )
    {
        if (positions.empty() || width < 2 || height < 2 || brush.radius <= 0.0f)
        {
            return;
        }

        float gx = (world_center.x + mapping.offset_x) / mapping.scale_x;
        float gz = (world_center.z + mapping.offset_z) / mapping.scale_z;
        int radius_cells_x = static_cast<int>(ceil(brush.radius / max(mapping.scale_x, 0.001f))) + 1;
        int radius_cells_z = static_cast<int>(ceil(brush.radius / max(mapping.scale_z, 0.001f))) + 1;
        int cx = static_cast<int>(floor(gx));
        int cz = static_cast<int>(floor(gz));
        int x0 = max(0, cx - radius_cells_x);
        int z0 = max(0, cz - radius_cells_z);
        int x1 = min(static_cast<int>(width) - 1, cx + radius_cells_x);
        int z1 = min(static_cast<int>(height) - 1, cz + radius_cells_z);

        vector<float> smooth_src;
        if (brush.mode == TerrainBrushMode::Smooth)
        {
            smooth_src.resize(positions.size());
            for (size_t i = 0; i < positions.size(); i++)
            {
                smooth_src[i] = positions[i].y;
            }
        }

        for (int z = z0; z <= z1; z++)
        {
            for (int x = x0; x <= x1; x++)
            {
                size_t idx = static_cast<size_t>(z) * width + x;
                Vector3& pos = positions[idx];
                float dx = pos.x - world_center.x;
                float dz = pos.z - world_center.z;
                float dist = sqrtf(dx * dx + dz * dz);
                float weight = brush_falloff_weight(dist, brush.radius, brush.falloff);
                if (weight <= 0.0f)
                {
                    continue;
                }

                float delta = brush.strength * weight;

                if (brush.mode == TerrainBrushMode::Raise)
                {
                    pos.y += delta;
                }
                else if (brush.mode == TerrainBrushMode::Lower)
                {
                    pos.y -= delta;
                }
                else if (brush.mode == TerrainBrushMode::Flatten)
                {
                    pos.y = lerp(pos.y, brush.target_height, clamp(weight * (brush.strength / 10.0f), 0.0f, 1.0f));
                }
                else if (brush.mode == TerrainBrushMode::Smooth)
                {
                    float sum = 0.0f;
                    uint32_t count = 0;
                    for (int nz = -1; nz <= 1; nz++)
                    {
                        for (int nx = -1; nx <= 1; nx++)
                        {
                            int sx = x + nx;
                            int sz = z + nz;
                            if (sx >= 0 && sx < static_cast<int>(width) && sz >= 0 && sz < static_cast<int>(height))
                            {
                                sum += smooth_src[static_cast<size_t>(sz) * width + sx];
                                count++;
                            }
                        }
                    }
                    if (count > 0)
                    {
                        float avg = sum / static_cast<float>(count);
                        pos.y = lerp(pos.y, avg, clamp(weight * (brush.strength / 10.0f), 0.0f, 1.0f));
                    }
                }

                if (height_data && idx < height_data->size())
                {
                    (*height_data)[idx] = pos.y;
                }
            }
        }
    }

    void TerrainSystem::ApplyIslandShore(
        vector<Vector3>& positions,
        vector<float>* height_data,
        uint32_t width,
        uint32_t height,
        const TerrainGridMapping& mapping,
        float shore_width,
        float edge_height_local
    )
    {
        if (positions.empty() || width < 2 || height < 2 || shore_width <= 0.0f)
        {
            return;
        }

        const float cell_x = max(mapping.scale_x, 0.001f);
        const float cell_z = max(mapping.scale_z, 0.001f);

        auto bend = [&](uint32_t start, uint32_t end)
        {
            for (uint32_t i = start; i < end; i++)
            {
                const uint32_t x = i % width;
                const uint32_t z = i / width;

                // meters to nearest map border
                const float dist_x = static_cast<float>(min(x, width - 1 - x)) * cell_x;
                const float dist_z = static_cast<float>(min(z, height - 1 - z)) * cell_z;
                const float dist_edge = min(dist_x, dist_z);

                if (dist_edge >= shore_width)
                {
                    continue;
                }

                // 0 at border, 1 inland past shore_width
                float t = dist_edge / shore_width;
                t = t * t * (3.0f - 2.0f * t);

                positions[i].y = lerp(edge_height_local, positions[i].y, t);

                if (height_data && i < height_data->size())
                {
                    (*height_data)[i] = positions[i].y;
                }
            }
        };
        ThreadPool::ParallelLoop(bend, static_cast<uint32_t>(positions.size()));
    }
}
