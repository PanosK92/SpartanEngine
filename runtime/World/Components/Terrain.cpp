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
    namespace perlin
    {
        // Permutation table (256 values, typically used in Perlin noise for randomness)
        static unsigned char p[512] = {
            151,160,137,91,90,15,131,13,201,95,96,53,194,233,7,225,140,36,103,30,69,142,
            8,99,37,240,21,10,23,190,6,148,247,120,234,75,0,26,197,62,94,252,219,203,117,
            35,11,32,57,177,33,88,237,149,56,87,174,20,125,136,171,168,68,175,74,165,71,
            134,139,48,27,166,77,146,158,231,83,111,229,122,60,211,133,230,220,105,92,41,
            55,46,245,40,244,102,143,54,65,25,63,161,1,216,80,73,209,76,132,187,208,89,
            18,169,200,196,135,130,116,188,159,86,164,100,109,198,173,186,3,64,52,217,
            226,250,124,123,5,202,38,147,118,126,255,82,85,212,207,206,59,227,47,16,58,
            17,182,189,28,42,223,183,170,213,119,248,152,2,44,154,163,70,221,153,101,
            155,167,43,172,9,129,22,39,253,19,98,108,110,79,113,224,232,178,185,112,104,
            218,246,97,228,251,34,242,193,238,210,144,12,191,179,162,241,81,51,145,235,
            249,14,239,107,49,192,214,31,181,199,106,157,184,84,204,176,115,121,50,45,
            127,4,150,254,138,236,205,93,222,114,67,29,24,72,243,141,128,195,78,66,215,
            61,156,180,
            // Duplicate the array for wrapping (common practice in Perlin noise)
            151,160,137,91,90,15,131,13,201,95,96,53,194,233,7,225,140,36,103,30,69,142,
            8,99,37,240,21,10,23,190,6,148,247,120,234,75,0,26,197,62,94,252,219,203,117,
            35,11,32,57,177,33,88,237,149,56,87,174,20,125,136,171,168,68,175,74,165,71,
            134,139,48,27,166,77,146,158,231,83,111,229,122,60,211,133,230,220,105,92,41,
            55,46,245,40,244,102,143,54,65,25,63,161,1,216,80,73,209,76,132,187,208,89,
            18,169,200,196,135,130,116,188,159,86,164,100,109,198,173,186,3,64,52,217,
            226,250,124,123,5,202,38,147,118,126,255,82,85,212,207,206,59,227,47,16,58,
            17,182,189,28,42,223,183,170,213,119,248,152,2,44,154,163,70,221,153,101,
            155,167,43,172,9,129,22,39,253,19,98,108,110,79,113,224,232,178,185,112,104,
            218,246,97,228,251,34,242,193,238,210,144,12,191,179,162,241,81,51,145,235,
            249,14,239,107,49,192,214,31,181,199,106,157,184,84,204,176,115,121,50,45,
            127,4,150,254,138,236,205,93,222,114,67,29,24,72,243,141,128,195,78,66,215,
            61,156,180
        };
    
        // Fade function for smooth interpolation (6t^5 - 15t^4 + 10t^3)
        inline float fade(float t)
        {
            return t * t * t * (t * (t * 6 - 15) + 10);
        }
    
        // Linear interpolation
        inline float lerp(float a, float b, float t)
        {
            return a + t * (b - a);
        }
    
        // Gradient function: computes dot product between gradient vector and distance vector
        inline float grad(int hash, float x, float y)
        {
            int h = hash & 15;           // Take lower 4 bits of hash
            float u = h < 8 ? x : y;     // If h < 8, use x, else use y
            float v = h < 4 ? y : (h == 12 || h == 14 ? x : 0); // Select v based on hash
            return ((h & 1) == 0 ? u : -u) + ((h & 2) == 0 ? v : -v); // Dot product
        }
    
        // 2D Perlin noise function
        float noise(float x, float y)
        {
            // Find unit grid cell containing point
            int X = static_cast<int>(floor(x)) & 255;
            int Y = static_cast<int>(floor(y)) & 255;
    
            // Get relative coordinates within the cell
            x -= floor(x);
            y -= floor(y);
    
            // Compute fade curves for smooth interpolation
            float u = fade(x);
            float v = fade(y);
    
            // Hash coordinates of the 4 corners of the grid cell
            int aa = p[p[X] + Y];       // Bottom-left
            int ab = p[p[X] + Y + 1];   // Top-left
            int ba = p[p[X + 1] + Y];   // Bottom-right
            int bb = p[p[X + 1] + Y + 1]; // Top-right
    
            // Compute gradients and interpolate
            float g1 = grad(aa, x, y);           // Bottom-left gradient
            float g2 = grad(ba, x - 1, y);       // Bottom-right gradient
            float x1 = lerp(g1, g2, u);          // Interpolate along x (bottom edge)
            
            float g3 = grad(ab, x, y - 1);       // Top-left gradient
            float g4 = grad(bb, x - 1, y - 1);   // Top-right gradient
            float x2 = lerp(g3, g4, u);          // Interpolate along x (top edge)
    
            // Interpolate along y and return noise value in range [-1, 1]
            return lerp(x1, x2, v);
        }
    }

    namespace
    {
        const float sea_level               = 0.0f; // the height at which the sea level is= 0.0f; // this is an axiom of the engine
        const uint32_t smoothing_iterations = 1;    // the number of height map neighboring pixel averaging
        const uint32_t tile_count           = 8;    // the number of tiles in each dimension to split the terrain into

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

        bool get_values_from_height_map(vector<float>& height_data_out, RHI_Texture* height_texture, float min_y, float max_y)
        {
            vector<byte> height_data = height_texture->GetMip(0, 0).bytes;
            SP_ASSERT(height_data.size() > 0);

            // read from the red channel and save a normalized height value
            {
                // bytes per pixel
                uint32_t bytes_per_pixel = (height_texture->GetChannelCount() * height_texture->GetBitsPerChannel()) / 8;

                // normalize and scale height data
                height_data_out.resize(height_data.size() / bytes_per_pixel);
                for (uint32_t i = 0; i < height_data.size(); i += bytes_per_pixel)
                {
                    // assuming the height is stored in the red channel (first channel)
                    height_data_out[i / bytes_per_pixel] = min_y + (static_cast<float>(height_data[i]) / 255.0f) * (max_y - min_y);
                }
            }

            // smooth out the height map values, this will reduce hard terrain edges
            {
                const uint32_t width  = height_texture->GetWidth();
                const uint32_t height = height_texture->GetHeight();

                for (uint32_t iteration = 0; iteration < smoothing_iterations; iteration++)
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

            return true;
        }

        void add_perlin_noise(vector<float>& height_data, uint32_t width, uint32_t height, float frequency, float amplitude)
        {
            for (uint32_t j = 0; j < height; ++j)
            {
                for (uint32_t i = 0; i < width; ++i)
                {
                    float x                     = static_cast<float>(i) - width * 0.5f;
                    float z                     = static_cast<float>(j) - height * 0.5f;
                    float noise_value           = perlin::noise(x * frequency, z * frequency);
                    height_data[j * width + i] += noise_value * amplitude;
                }
            }
        }

        void generate_positions(vector<Vector3>& positions, const vector<float>& height_map, const uint32_t width, const uint32_t height)
        {
            SP_ASSERT_MSG(!height_map.empty(), "Height map is empty");
        
            mutex mtx;
            positions.resize(width * height);
            auto generate_position_range = [&mtx, &positions, &height_map, width, height](uint32_t start_index, uint32_t end_index)
            {
                for (uint32_t index = start_index; index < end_index; index++)
                {
                    // convert flat index to x,y coordinates
                    uint32_t x = index % width;
                    uint32_t y = index / width;
        
                    // center on the X and Z axis
                    float centered_x = static_cast<float>(x) - width * 0.5f;
                    float centered_z = static_cast<float>(y) - height * 0.5f;
        
                    // get height from height_map
                    float height_value = height_map[index];
        
                    // assign position
                    lock_guard<mutex> lock(mtx);
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

        void generate_normals(const vector<uint32_t>& terrain_indices, vector<RHI_Vertex_PosTexNorTan>& terrain_vertices)
        {
            SP_ASSERT_MSG(!terrain_indices.empty(), "Indices are empty");
            SP_ASSERT_MSG(!terrain_vertices.empty(), "Vertices are empty");

            uint32_t triangle_count = static_cast<uint32_t>(terrain_indices.size()) / 3;
            vector<Vector3> face_normals(triangle_count);
            vector<Vector3> face_tangents(triangle_count);
            vector<vector<uint32_t>> vertex_to_triangle_map(terrain_vertices.size());
            Vector3 edge_a, edge_b;

            for (uint32_t i = 0; i < triangle_count; ++i)
            {
                uint32_t index_a = terrain_indices[i * 3];
                uint32_t index_b = terrain_indices[i * 3 + 1];
                uint32_t index_c = terrain_indices[i * 3 + 2];

                vertex_to_triangle_map[index_a].push_back(i);
                vertex_to_triangle_map[index_b].push_back(i);
                vertex_to_triangle_map[index_c].push_back(i);

                edge_a.x = terrain_vertices[index_a].pos[0] - terrain_vertices[index_b].pos[0];
                edge_a.y = terrain_vertices[index_a].pos[1] - terrain_vertices[index_b].pos[1];
                edge_a.z = terrain_vertices[index_a].pos[2] - terrain_vertices[index_b].pos[2];

                edge_b.x = terrain_vertices[index_b].pos[0] - terrain_vertices[index_c].pos[0];
                edge_b.y = terrain_vertices[index_b].pos[1] - terrain_vertices[index_c].pos[1];
                edge_b.z = terrain_vertices[index_b].pos[2] - terrain_vertices[index_c].pos[2];

                face_normals[i] = Vector3::Cross(edge_a, edge_b);

                const float tc_u1 = terrain_vertices[index_a].tex[0] - terrain_vertices[index_b].tex[0];
                const float tc_v1 = terrain_vertices[index_a].tex[1] - terrain_vertices[index_b].tex[1];
                const float tc_u2 = terrain_vertices[index_b].tex[0] - terrain_vertices[index_c].tex[0];
                const float tc_v2 = terrain_vertices[index_b].tex[1] - terrain_vertices[index_c].tex[1];

                float coef = 1.0f / (tc_u1 * tc_v2 - tc_u2 * tc_v1);
                face_tangents[i] = coef * (tc_v2 * edge_a - tc_v1 * edge_b);
            }

            auto compute_vertex_normals_tangents = [&terrain_vertices, &vertex_to_triangle_map, &face_normals, &face_tangents](uint32_t start_index, uint32_t end_index)
            {
                for (uint32_t i = start_index; i < end_index; i++)
                {
                    Vector3 normal_average  = Vector3::Zero;
                    Vector3 tangent_average = Vector3::Zero;
                    float face_usage_count  = 0;

                    for (uint32_t j : vertex_to_triangle_map[i])
                    {
                        normal_average  += face_normals[j];
                        tangent_average += face_tangents[j];
                        face_usage_count++;
                    }

                    if (face_usage_count > 0)
                    {
                        normal_average  /= face_usage_count;
                        tangent_average /= face_usage_count;

                        normal_average.Normalize();
                        tangent_average.Normalize();

                        terrain_vertices[i].nor[0] = normal_average.x;
                        terrain_vertices[i].nor[1] = normal_average.y;
                        terrain_vertices[i].nor[2] = normal_average.z;

                        terrain_vertices[i].tan[0] = tangent_average.x;
                        terrain_vertices[i].tan[1] = tangent_average.y;
                        terrain_vertices[i].tan[2] = tangent_average.z;
                    }
                }
            };

            uint32_t vertex_count = static_cast<uint32_t>(terrain_vertices.size());
            ThreadPool::ParallelLoop(compute_vertex_normals_tangents, vertex_count);
        }

        float get_random_float(mt19937& gen, float x, float y)
        {
            uniform_real_distribution<float> distr(x, y); // define the distribution
            return distr(gen);
        }

        vector<Matrix> generate_transforms(
            const vector<RHI_Vertex_PosTexNorTan>& terrain_vertices,
            const vector<uint32_t>& terrain_indices,
            uint32_t transform_count,
            float max_slope_radians,
            bool rotate_to_match_surface_normal,
            float terrain_offset
        )
        {
            vector<Matrix> transforms;
            random_device seed;
            mt19937 generator(seed());
            uniform_int_distribution<> distribution(0, static_cast<int>(terrain_indices.size() / 3 - 1));
            mutex mtx;

            auto place_mesh = [
                &mtx,
                &transforms,
                &terrain_vertices,
                &terrain_indices,
                &distribution,
                &max_slope_radians,
                &generator,
                &terrain_offset,
                &rotate_to_match_surface_normal
            ](uint32_t start_index, uint32_t end_index)
            {
                // each thread gets its own generator
                random_device rd; 
                mt19937 generator(rd());
                uniform_int_distribution<> distribution(0, static_cast<int>(terrain_indices.size() / 3 - 1));
                uniform_real_distribution<float> barycentric_dist(0.0f, 1.0f);

                for (uint32_t i = start_index; i < end_index; i++)
                {
                    // randomly select a triangle from the mesh
                    uint32_t triangle_index = distribution(generator) * 3;

                    // get the vertices of the the terrain triangle
                    Vector3 v0 = Vector3(terrain_vertices[terrain_indices[triangle_index]].pos[0],     terrain_vertices[terrain_indices[triangle_index]].pos[1],     terrain_vertices[terrain_indices[triangle_index]].pos[2]);
                    Vector3 v1 = Vector3(terrain_vertices[terrain_indices[triangle_index + 1]].pos[0], terrain_vertices[terrain_indices[triangle_index + 1]].pos[1], terrain_vertices[terrain_indices[triangle_index + 1]].pos[2]);
                    Vector3 v2 = Vector3(terrain_vertices[terrain_indices[triangle_index + 2]].pos[0], terrain_vertices[terrain_indices[triangle_index + 2]].pos[1], terrain_vertices[terrain_indices[triangle_index + 2]].pos[2]);

                    // ensure the the triangle is within acceptable slope and height
                    Vector3 normal            = Vector3::Cross(v1 - v0, v2 - v0).Normalized();
                    float slope_radians       = acos(Vector3::Dot(normal, Vector3::Up));
                    bool is_acceptable_slope  = slope_radians <= max_slope_radians;
                    bool is_acceptable_height = v0.y >= sea_level && v1.y >= sea_level && v2.y >= sea_level;

                    if (is_acceptable_slope && is_acceptable_height)
                    {
                        // generate barycentric coordinates
                        float u = barycentric_dist(generator);
                        float v = barycentric_dist(generator);
                        if (u + v > 1.0f)
                        {
                            u = 1.0f - u;
                            v = 1.0f - v;
                        }

                        // position is the barycentric coordinates multiplied by the vertices of the triangle, plus a user defined terrain offset
                        Vector3 position = v0 + (u * (v1 - v0) + terrain_offset) + v * (v2 - v0);

                        // rotation is a random rotation around the Y axis, and then rotated to match the normal of the triangle
                        Quaternion rotate_to_normal = rotate_to_match_surface_normal ? Quaternion::FromToRotation(Vector3::Up, normal) : Quaternion::Identity;
                        Quaternion rotation         = rotate_to_normal * Quaternion::FromEulerAngles(0.0f, get_random_float(generator, 0.0f, 360.0f), 0.0f);

                        lock_guard<mutex> lock(mtx);
                        transforms.emplace_back(position, rotation, 1.0f);
                    }
                    else
                    {
                        // if the slope is too steep or the object is underwater, try again
                        --i;
                    }
                }
            };

            ThreadPool::ParallelLoop(place_mesh, transform_count);

            return transforms;
        }

        void split_terrain_into_tiles(
            const vector<RHI_Vertex_PosTexNorTan>& terrain_vertices, const vector<uint32_t>& terrain_indices,
            vector<vector<RHI_Vertex_PosTexNorTan>>& tiled_vertices, vector<vector<uint32_t>>& tiled_indices)
        {
            // initialize min and max values for terrain bounds
            float min_x = numeric_limits<float>::max();
            float max_x = numeric_limits<float>::lowest();
            float min_z = numeric_limits<float>::max();
            float max_z = numeric_limits<float>::lowest();

            // iterate over all vertices to find the minimum and maximum x and z values
            for (const RHI_Vertex_PosTexNorTan& vertex : terrain_vertices)
            {
                // compare and store the minimum and maximum x coordinates
                if (vertex.pos[0] < min_x) min_x = vertex.pos[0];
                if (vertex.pos[0] > max_x) max_x = vertex.pos[0];

                // compare and store the minimum and maximum z coordinates
                if (vertex.pos[2] < min_z) min_z = vertex.pos[2];
                if (vertex.pos[2] > max_z) max_z = vertex.pos[2];
            }

            // calculate dimensions
            float terrain_width = max_x - min_x;
            float terrain_depth = max_z - min_z;
            float tile_width    = terrain_width / static_cast<float>(tile_count);
            float tile_depth    = terrain_depth / static_cast<float>(tile_count);

            // initialize tiled vertices and indices
            tiled_vertices.resize(tile_count * tile_count);
            tiled_indices.resize(tile_count * tile_count);

            // create a mapping for each tile to track vertex global indices to their new local indices
            vector<unordered_map<uint32_t, uint32_t>> global_to_local_indices(tile_count * tile_count);

            // assign vertices to tiles and track their indices
            for (uint32_t global_index = 0; global_index < terrain_vertices.size(); ++global_index)
            {
                const RHI_Vertex_PosTexNorTan& vertex = terrain_vertices[global_index];

                uint32_t tile_x = static_cast<uint32_t>((vertex.pos[0] - min_x) / tile_width);
                uint32_t tile_z = static_cast<uint32_t>((vertex.pos[2] - min_z) / tile_depth);
                tile_x          = min(tile_x, tile_count - 1);
                tile_z          = min(tile_z, tile_count - 1);

                // convert the 2D tile coordinates into a single index for the 1D output array
                uint32_t tile_index = tile_z * tile_count + tile_x;

                // add vertex to the appropriate tile
                tiled_vertices[tile_index].push_back(vertex);

                // track the local index of this vertex in the tile
                uint32_t local_index = static_cast<uint32_t>(tiled_vertices[tile_index].size() - 1);
                global_to_local_indices[tile_index][global_index] = local_index;
            }

            auto add_shared_vertex = [](
                uint32_t tile_x, uint32_t tile_z, uint32_t global_index,
                const vector<RHI_Vertex_PosTexNorTan>&terrain_vertices, vector<vector<RHI_Vertex_PosTexNorTan>>&tiled_vertices,
                vector<unordered_map<uint32_t, uint32_t>>&global_to_local_indices, vector<vector<uint32_t>>&tiled_indices)
            {
                // check if tile_x and tile_z are within the valid range
                if (tile_x >= tile_count || tile_z >= tile_count)
                    return; // out of valid tile range, do nothing

                uint32_t tile_count = static_cast<uint32_t>(sqrt(tiled_vertices.size())); // assuming square number of tiles
                uint32_t tile_index = tile_z * tile_count + tile_x;
                const RHI_Vertex_PosTexNorTan& vertex = terrain_vertices[global_index];

                // add the vertex if it doesn't exist in the tile
                if (global_to_local_indices[tile_index].find(global_index) == global_to_local_indices[tile_index].end())
                {
                    tiled_vertices[tile_index].push_back(vertex);
                    uint32_t local_index = static_cast<uint32_t>(tiled_vertices[tile_index].size() - 1);
                    global_to_local_indices[tile_index][global_index] = local_index;
                }
            };

            // adjust and assign indices to tiles
            for (uint32_t global_index = 0; global_index < terrain_indices.size(); global_index += 3)
            {
                // find the tile for the first vertex of the triangle
                const RHI_Vertex_PosTexNorTan& vertex = terrain_vertices[terrain_indices[global_index]];
                uint32_t tile_x                       = static_cast<uint32_t>((vertex.pos[0] - min_x) / tile_width);
                uint32_t tile_z                       = static_cast<uint32_t>((vertex.pos[2] - min_z) / tile_depth);
                tile_x                                = min(tile_x, tile_count - 1);
                tile_z                                = min(tile_z, tile_count - 1);
                uint32_t tile_index                   = tile_z * tile_count + tile_x;

                // add all vertices of the triangle to the current tile
                for (uint32_t j = 0; j < 3; ++j)
                {
                    uint32_t current_global_index = terrain_indices[global_index + j];
                    const RHI_Vertex_PosTexNorTan& current_vertex = terrain_vertices[current_global_index];
                    uint32_t local_index;

                    // check if the vertex index already exists in the local index map for the current tile
                    auto it = global_to_local_indices[tile_index].find(current_global_index);
                    if (it != global_to_local_indices[tile_index].end())
                    {
                        local_index = it->second;
                    }
                    else
                    {
                        // If the vertex is not already in the tile, add it and update the index map
                        tiled_vertices[tile_index].push_back(current_vertex);
                        local_index = static_cast<uint32_t>(tiled_vertices[tile_index].size() - 1);
                        global_to_local_indices[tile_index][current_global_index] = local_index;
                    }
                    tiled_indices[tile_index].push_back(local_index);
                }

                // check for shared edges and corners
                for (uint32_t j = 0; j < 3; ++j)
                {
                    // for each vertex of the triangle, check if it's on a shared edge
                    uint32_t current_global_index = terrain_indices[global_index + j];
                    const RHI_Vertex_PosTexNorTan& current_vertex = terrain_vertices[current_global_index];

                    // calculate the local tile coordinates again
                    tile_x = static_cast<uint32_t>((current_vertex.pos[0] - min_x) / tile_width);
                    tile_z = static_cast<uint32_t>((current_vertex.pos[2] - min_z) / tile_depth);

                    // determine if the vertex is on an edge or corner
                    bool is_on_horizontal_edge = fmod(current_vertex.pos[0] - min_x, tile_width) <= numeric_limits<float>::epsilon() && tile_x > 0;
                    bool is_on_vertical_edge   = fmod(current_vertex.pos[2] - min_z, tile_depth) <= numeric_limits<float>::epsilon() && tile_z > 0;

                    // add the vertex to the shared edges/corners tiles if needed
                    if (is_on_horizontal_edge)
                    {
                        // add to tile on the left
                        add_shared_vertex(tile_x - 1, tile_z, current_global_index, terrain_vertices, tiled_vertices, global_to_local_indices, tiled_indices);
                    }

                    if (is_on_vertical_edge)
                    {
                        // add to tile below
                        add_shared_vertex(tile_x, tile_z - 1, current_global_index, terrain_vertices, tiled_vertices, global_to_local_indices, tiled_indices);
                    }

                    if (is_on_horizontal_edge && is_on_vertical_edge)
                    {
                        // add to the diagonal tile (bottom left)
                        add_shared_vertex(tile_x - 1, tile_z - 1, current_global_index, terrain_vertices, tiled_vertices, global_to_local_indices, tiled_indices);
                    }
                }
            }
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

    void Terrain::Serialize(FileStream* stream)
    {
        SP_LOG_WARNING("Not implemented");
    }

    void Terrain::Deserialize(FileStream* stream)
    {
        SP_LOG_WARNING("Not implemented");
    }

    uint32_t Terrain::GetWidth() const
    {
        return m_height_texture->GetWidth();
    }

    uint32_t Terrain::GetHeight() const
    {
        return m_height_texture->GetHeight();
    }

    void Terrain::GenerateTransforms(vector<Matrix>* transforms, const uint32_t count, const TerrainProp terrain_prop)
    {
        bool rotate_match_surface_normal = false;
        float max_slope                  = 0.0f; // allow no slope
        float terrain_offset             = 0.0f; // spawn at sea level
    
        if (terrain_prop == TerrainProp::Tree)
        {
            max_slope                   = 30.0f * math::deg_to_rad;
            rotate_match_surface_normal = false; // trees tend to grow upwards, towards the sun
            terrain_offset              = -2.0f;
        }
    
        if (terrain_prop == TerrainProp::Grass)
        {
            max_slope                   = 40.0f * math::deg_to_rad;
            rotate_match_surface_normal = true; // small plants tend to grow towards the sun but they can have some wonky angles due to low mass
            terrain_offset              = 0.0f;
        }
    
        *transforms = generate_transforms(m_vertices, m_indices, count, max_slope, rotate_match_surface_normal, terrain_offset);
    }

    void Terrain::Generate()
    {
        // thread safety
        if (m_is_generating)
        {
            SP_LOG_WARNING("Terrain is already being generated, please wait...");
            return;
        }
    
        if (!m_height_texture)
        {
            SP_LOG_WARNING("You need to assign a height map before trying to generate a terrain");
            Clear();
            return;
        }
    
        m_is_generating = true;
    
        // start progress tracking
        uint32_t job_count = 8;
        ProgressTracker::GetProgress(ProgressType::Terrain).Start(job_count, "Generating terrain...");
    
        uint32_t width  = 0;
        uint32_t height = 0;
        vector<Vector3> positions;
    
        // 1. process height map
        {
            ProgressTracker::GetProgress(ProgressType::Terrain).SetText("Process height map...");

            if (!get_values_from_height_map(m_height_data, m_height_texture, m_min_y, m_max_y))
            {
                m_is_generating = false;
                return;
            }

            // deduce some stuff
            width            = m_height_texture->GetWidth();
            height           = m_height_texture->GetHeight();
            m_height_samples = width * height;
            m_vertex_count   = m_height_samples;
            m_index_count    = m_vertex_count * 6;
            m_triangle_count = m_index_count / 3;

            // allocate memory for the calculations that follow
            positions  = vector<Vector3>(m_height_samples);
            m_vertices = vector<RHI_Vertex_PosTexNorTan>(m_vertex_count);
            m_indices  = vector<uint32_t>(m_index_count);

            ProgressTracker::GetProgress(ProgressType::Terrain).JobDone();
        }
    
        // 2. add perlin noise - todo: needs to operate at a vertex level
        {
            ProgressTracker::GetProgress(ProgressType::Terrain).SetText("Adding Perlin noise...");
            const float frequency = 0.1f;
            const float amplitude = 1.0f;
            //add_perlin_noise(m_height_data, width, height, frequency, amplitude);
            ProgressTracker::GetProgress(ProgressType::Terrain).JobDone();
        }
    
        // 3. compute positions 
        {
            ProgressTracker::GetProgress(ProgressType::Terrain).SetText("Generating positions...");
            generate_positions(positions, m_height_data, width, height);
            ProgressTracker::GetProgress(ProgressType::Terrain).JobDone();
        }
    
        // 4. compute vertices and indices
        {
            ProgressTracker::GetProgress(ProgressType::Terrain).SetText("Generating vertices and indices...");
            generate_vertices_and_indices(m_vertices, m_indices, positions, width, height);
            ProgressTracker::GetProgress(ProgressType::Terrain).JobDone();
        }
    
        // 5. compute normals and tangents
        {
            ProgressTracker::GetProgress(ProgressType::Terrain).SetText("Generating normals...");
            generate_normals(m_indices, m_vertices);
            ProgressTracker::GetProgress(ProgressType::Terrain).JobDone();
        }
    
        // 6. Optimize geometry (optional)
        {
            ProgressTracker::GetProgress(ProgressType::Terrain).SetText("Optimizing geometry...");
            spartan::geometry_processing::optimize(m_vertices, m_indices);
            ProgressTracker::GetProgress(ProgressType::Terrain).JobDone();
        }
    
        // 7. Split into tiles
        {
            ProgressTracker::GetProgress(ProgressType::Terrain).SetText("Splitting into tiles...");
            split_terrain_into_tiles(m_vertices, m_indices, m_tile_vertices, m_tile_indices);
            ProgressTracker::GetProgress(ProgressType::Terrain).JobDone();
        }
    
        // 8. Create a mesh for each tile
        {
            ProgressTracker::GetProgress(ProgressType::Terrain).SetText("Creating tile meshes");
            for (uint32_t tile_index = 0; tile_index < static_cast<uint32_t>(m_tile_vertices.size()); tile_index++)
            {
                UpdateMesh(tile_index);
            }
            ProgressTracker::GetProgress(ProgressType::Terrain).JobDone();
        }

        m_area_km2      = compute_terrain_area_km2(m_vertices);
        m_is_generating = false;
    }
    
    void Terrain::UpdateMesh(const uint32_t tile_index)
    {
        string name = "tile_" + to_string(tile_index);

        // create mesh if it doesn't exist
        if (m_tile_meshes.size() <= tile_index)
        {
            shared_ptr<Mesh>& mesh = m_tile_meshes.emplace_back(make_shared<Mesh>());
            mesh->SetObjectName(name);
            // don't optimize the terrain as tile seams will be visible
            mesh->SetFlag(static_cast<uint32_t>(MeshFlags::PostProcessOptimize), false);
        }

        // update with geometry
        shared_ptr<Mesh>& mesh = m_tile_meshes[tile_index];
        mesh->Clear();
        mesh->AddGeometry(m_tile_vertices[tile_index], m_tile_indices[tile_index]);
        mesh->PostProcess();

        // create a child entity, add a renderable, and this mesh tile to it
        {
            shared_ptr<Entity> entity = World::CreateEntity();
            entity->SetObjectName(name);
            entity->SetParent(World::GetEntityById(m_entity_ptr->GetObjectId()));

            if (shared_ptr<Renderable> renderable = entity->AddComponent<Renderable>())
            {
                renderable->SetGeometry(
                    mesh.get(),
                    mesh->GetAabb(),
                    0,                     // index offset
                    mesh->GetIndexCount(), // index count
                    0,                     // vertex offset
                    mesh->GetVertexCount() // vertex count
                );

                renderable->SetMaterial(m_material);
            }
        }
    }

    void Terrain::Clear()
    {
        m_vertices.clear();
        m_indices.clear();
        m_tile_meshes.clear();
        m_tile_vertices.clear();
        m_tile_indices.clear();

        for (auto& mesh : m_tile_meshes)
        {
            ResourceCache::Remove(mesh);
        }

        for (Entity* child : m_entity_ptr->GetChildren())
        {
            if (shared_ptr<Renderable> renderable = child->AddComponent<Renderable>())
            {
                renderable->SetGeometry(nullptr);
            }
        }
    }
}
