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

#pragma once

//= INCLUDES ===========================
#include <vector>
#include "../RHI/RHI_Vertex.h"
#include "../Core/ThreadPool.h"
SP_WARNINGS_OFF
#include "meshoptimizer/meshoptimizer.h"
SP_WARNINGS_ON
//======================================

namespace spartan::geometry_processing
{
    static void register_meshoptimizer()
    {
        static std::atomic<bool> registered = false;
        if (registered)
            return;

         // give credit where credit is due
        const int major = MESHOPTIMIZER_VERSION / 1000;
        const int minor = (MESHOPTIMIZER_VERSION % 1000) / 10;
        const int rev   = MESHOPTIMIZER_VERSION % 10;
        Settings::RegisterThirdPartyLib("meshoptimizer", std::to_string(major) + "." + std::to_string(minor) + "." + std::to_string(rev), "https://github.com/zeux/meshoptimizer");

        registered = true;
    }

    static void simplify(
        std::vector<uint32_t>& indices,
        std::vector<RHI_Vertex_PosTexNorTan>& vertices,
        size_t target_index_count,
        const bool preserve_uvs,  // typically true, and false for anything that doesn't need to be rendererd, say physics meshes
        const bool preserve_edges // ideal for terrain tiles, where you want the edges to remain intact, so they meet with neighboring tiles
    )
    {
        register_meshoptimizer();
    
        // starting parameters
        float  error                  = 0.01f;
        size_t index_count            = indices.size();
        size_t current_triangle_count = index_count / 3;
    
        // early exit if target is already met
        if (target_index_count >= index_count)
            return;
    
        // early exit if mesh is too small, few vertices can collapse to nothing
        if (vertices.size() <= 16)
            return;
    
        // temporary buffer for simplified indices
        std::vector<uint32_t> indices_simplified(index_count);
    
        // vertex lock array for preserving perimeter vertices
        std::vector<unsigned char> vertex_locks;
        size_t                     vertex_count = vertices.size();
        if (preserve_edges)
        {
            vertex_locks.resize(vertex_count, 0); // 0 = unlocked, 1 = locked
    
            // compute tile bounding box
            float min_x = std::numeric_limits<float>::max();
            float max_x = std::numeric_limits<float>::lowest();
            float min_z = std::numeric_limits<float>::max();
            float max_z = std::numeric_limits<float>::lowest();
            for (const auto& vertex : vertices)
            {
                min_x = std::min(min_x, vertex.pos[0]);
                max_x = std::max(max_x, vertex.pos[0]);
                min_z = std::min(min_z, vertex.pos[2]);
                max_z = std::max(max_z, vertex.pos[2]);
            }
    
            // lock vertices near bounding box edges
            const float edge_tolerance = 0.01f;
            size_t      locked_count   = 0;
            for (size_t i = 0; i < vertex_count; ++i)
            {
                float x = vertices[i].pos[0];
                float z = vertices[i].pos[2];
                if (std::abs(x - min_x) < edge_tolerance || std::abs(x - max_x) < edge_tolerance ||
                    std::abs(z - min_z) < edge_tolerance || std::abs(z - max_z) < edge_tolerance)
                {
                    vertex_locks[i] = 1; // lock vertex on tile boundary
                    locked_count++;
                }
            }
        }
    
        // prepare attribute buffer for uvs (packed as float2 per vertex) if preserve_uvs is true
        std::vector<float> attr_buffer;
        const float* vertex_attributes = nullptr;
        size_t attr_stride = 0;
        size_t attr_count = 0;
        static constexpr float uv_weights[2] = {0.5f, 0.5f}; // weights for uv components
        const float* attr_weights = nullptr;
    
        if (preserve_uvs)
        {
            attr_buffer.reserve(vertex_count * 2); // 2 components per uv
            for (const auto& v : vertices)
            {
                attr_buffer.push_back(v.tex[0]);
                attr_buffer.push_back(v.tex[1]);
            }
            vertex_attributes = attr_buffer.data();
            attr_stride = sizeof(float) * 2; // packed float2
            attr_weights = uv_weights;
            attr_count = 2; // uv components
        }
    
        // get locks or nullptr
        const unsigned char* locks = preserve_edges && !vertex_locks.empty() ? vertex_locks.data() : nullptr;
    
        // simplification loop up to error = 1.0
        float lod_error = 0.0f;
        while (current_triangle_count > (target_index_count / 3) && error <= 1.0f)
        {
            if (target_index_count < 3)
                break;
    
            size_t index_count_new = meshopt_simplifyWithAttributes(
                indices_simplified.data(),       // destination for simplified indices
                indices.data(),                  // source indices
                index_count,                     // current index count
                &vertices[0].pos[0],             // vertex position data
                vertex_count,                    // vertex count
                sizeof(RHI_Vertex_PosTexNorTan), // vertex stride
                vertex_attributes,               // attribute data start
                attr_stride,                     // attribute stride per vertex
                attr_weights,                    // weights array
                attr_count,                      // total components
                locks,                           // vertex lock array or nullptr
                target_index_count,              // desired index count
                error,                           // error tolerance
                0,                               // options (default)
                &lod_error                       // output error
            );
    
            // update indices and triangle count
            index_count = index_count_new;
            indices.assign(indices_simplified.begin(), indices_simplified.begin() + index_count);
            current_triangle_count = index_count / 3;
    
            // increase error linearly
            error += 0.1f;
        }
    
        // second attempt: use meshopt_simplifySloppy if needed, it doesn't respect topology or attributes, it just reduces indices aggressively
        if (current_triangle_count > (target_index_count / 3) && !preserve_edges)
        {
            if (target_index_count >= 3)
            {
                float  target_error    = FLT_MAX;
                size_t index_count_new = 0;
    
                // keep trying with reduced aggressiveness until we get valid indices
                do
                {
                    index_count_new = meshopt_simplifySloppy(
                        indices_simplified.data(),
                        indices.data(),
                        index_count,
                        &vertices[0].pos[0],
                        vertex_count,
                        sizeof(RHI_Vertex_PosTexNorTan),
                        target_index_count,
                        target_error,
                        &lod_error
                    );
    
                    // if we got zero indices, reduce the error and try again
                    if (index_count_new == 0)
                        target_error *= 0.5f; // reduce error
    
                    // stop if error becomes too small to be practical
                    if (target_error < 0.1f)
                        break;
    
                } while (index_count_new == 0);
    
                // only update if we got valid indices
                if (index_count_new > 0)
                {
                    index_count            = index_count_new;
                    indices.assign(indices_simplified.begin(), indices_simplified.begin() + index_count);
                    current_triangle_count = index_count / 3;
                }
            }
        }
    
        // we early exit for 16 or less indices, but aggressive simplification still has a small probability of collapsing to no indices - if that happens, assert and improve the function
        SP_ASSERT(!indices.empty());
    
        // optimize the vertex buffer
        std::vector<RHI_Vertex_PosTexNorTan> new_vertices(vertices.size());
        size_t new_vertex_count = meshopt_optimizeVertexFetch(
            new_vertices.data(),
            indices.data(),
            index_count,
            vertices.data(),
            vertices.size(),
            sizeof(RHI_Vertex_PosTexNorTan)
        );
    
        vertices.assign(new_vertices.begin(), new_vertices.begin() + new_vertex_count);
    }

    static void optimize(std::vector<RHI_Vertex_PosTexNorTan>& vertices, std::vector<uint32_t>& indices)
    {
        size_t vertex_count = vertices.size();
        size_t index_count  = indices.size();
    
        // step 1: vertex remapping
        {
            std::vector<unsigned int> remap(vertex_count);
            size_t vertex_count_optimized = meshopt_generateVertexRemap(remap.data(), indices.data(), index_count, vertices.data(), vertex_count, sizeof(RHI_Vertex_PosTexNorTan));
    
            std::vector<uint32_t> indices_remapped(index_count);
            meshopt_remapIndexBuffer(indices_remapped.data(), indices.data(), index_count, remap.data());
            indices = std::move(indices_remapped);
    
            std::vector<RHI_Vertex_PosTexNorTan> vertices_remapped(vertex_count_optimized);
            meshopt_remapVertexBuffer(vertices_remapped.data(), vertices.data(), vertex_count, sizeof(RHI_Vertex_PosTexNorTan), remap.data());
            vertices     = std::move(vertices_remapped);
            vertex_count = vertex_count_optimized;
        }

        // step 2: simplify with density-based targeting
        if (index_count > 30000)
        {
            // compute bounding box for density calculation
            float min_x = std::numeric_limits<float>::max();
            float max_x = std::numeric_limits<float>::lowest();
            float min_y = std::numeric_limits<float>::max();
            float max_y = std::numeric_limits<float>::lowest();
            float min_z = std::numeric_limits<float>::max();
            float max_z = std::numeric_limits<float>::lowest();
            for (const auto& vertex : vertices)
            {
                min_x = std::min(min_x, vertex.pos[0]);
                max_x = std::max(max_x, vertex.pos[0]);
                min_y = std::min(min_y, vertex.pos[1]);
                max_y = std::max(max_y, vertex.pos[1]);
                min_z = std::min(min_z, vertex.pos[2]);
                max_z = std::max(max_z, vertex.pos[2]);
            }
            float extent_x = max_x - min_x;
            float extent_y = max_y - min_y;
            float extent_z = max_z - min_z;
            float volume   = extent_x * extent_y * extent_z;

            if (volume > 0.0f)
            {
                // compute triangle density (triangles per unit volume)
                size_t triangle_count = index_count / 3;
                float density         = static_cast<float>(triangle_count) / volume;

                // compute reduction ratio based on density (linearly interpolate: ratio = 0.8 at density = 500, 0.2 at density = 1000)
                float ratio                = 0.8f - (density - 500.0f) * (0.6f / 500.0f); // 0.6 = 0.8 - 0.2, 500 = 1000 - 500
                ratio                      = std::max(0.2f, std::min(0.8f, ratio));       // clamp within a reasonable range
                size_t target_index_count  = static_cast<size_t>(index_count * ratio);

                simplify(indices, vertices, target_index_count, true, false);

                index_count  = indices.size();
                vertex_count = vertices.size();
            }
        }

        // step 3: vertex cache optimization
        meshopt_optimizeVertexCache(indices.data(), indices.data(), index_count, vertex_count);
    
        // step 4: overdraw optimization
        meshopt_optimizeOverdraw(indices.data(), indices.data(), index_count, &vertices[0].pos[0], vertex_count, sizeof(RHI_Vertex_PosTexNorTan), 1.05f);
    
        // step 5: vertex fetch optimization
        meshopt_optimizeVertexFetch(vertices.data(), indices.data(), index_count, vertices.data(), vertex_count, sizeof(RHI_Vertex_PosTexNorTan));
    }

    static void split_surface_into_tiles(
        const std::vector<RHI_Vertex_PosTexNorTan>& terrain_vertices,
        const std::vector<uint32_t>& terrain_indices,
        const uint32_t tile_count,
        std::vector<std::vector<RHI_Vertex_PosTexNorTan>>& tiled_vertices,
        std::vector<std::vector<uint32_t>>& tiled_indices,
        std::vector<math::Vector3>& tile_offsets
    )
    {
        // find terrain bounds
        float min_x = std::numeric_limits<float>::max();
        float max_x = std::numeric_limits<float>::lowest();
        float min_z = std::numeric_limits<float>::max();
        float max_z = std::numeric_limits<float>::lowest();
        for (const auto& vertex : terrain_vertices)
        {
            min_x = std::min(min_x, vertex.pos[0]);
            max_x = std::max(max_x, vertex.pos[0]);
            min_z = std::min(min_z, vertex.pos[2]);
            max_z = std::max(max_z, vertex.pos[2]);
        }
    
        // calculate tile dimensions
        float terrain_width = max_x - min_x;
        float terrain_depth = max_z - min_z;
        float tile_width    = terrain_width / static_cast<float>(tile_count);
        float tile_depth    = terrain_depth / static_cast<float>(tile_count);
                            
        // onitialize output containers and mutexes
        const uint32_t total_tiles = tile_count * tile_count;
        tiled_vertices.resize(total_tiles);
        tiled_indices.resize(total_tiles);
        tile_offsets.resize(total_tiles, math::Vector3::Zero);
        std::vector<std::unordered_map<uint32_t, uint32_t>> global_to_local_indices(total_tiles);
        std::vector<std::mutex> tile_mutexes(total_tiles);
    
        // precompute tile offsets
        for (uint32_t tz = 0; tz < tile_count; ++tz)
        {
            for (uint32_t tx = 0; tx < tile_count; ++tx)
            {
                uint32_t tile_index = tz * tile_count + tx;
                float tile_center_x = min_x + (tx + 0.5f) * tile_width;
                float tile_center_z = min_z + (tz + 0.5f) * tile_depth;
                tile_offsets[tile_index] = math::Vector3(tile_center_x, 0.0f, tile_center_z);
            }
        }
    
        // calculate number of triangles
        uint32_t triangle_count = static_cast<uint32_t>(terrain_indices.size()) / 3;
    
        // parallel processing of triangles
        auto process_triangles = [&terrain_vertices, &terrain_indices, tile_count, min_x, min_z, tile_width, tile_depth, &tiled_vertices, &tiled_indices, &global_to_local_indices, &tile_mutexes, &tile_offsets](uint32_t start_tri, uint32_t end_tri)
        {
            const float epsilon = 1e-6f;
            for (uint32_t tri = start_tri; tri < end_tri; ++tri)
            {
                // get starting index of the triangle
                uint32_t i = tri * 3;
    
                // get vertices
                const auto& v0 = terrain_vertices[terrain_indices[i]];
                const auto& v1 = terrain_vertices[terrain_indices[i + 1]];
                const auto& v2 = terrain_vertices[terrain_indices[i + 2]];
    
                // compute triangle bounds
                float tri_min_x = std::min({v0.pos[0], v1.pos[0], v2.pos[0]});
                float tri_max_x = std::max({v0.pos[0], v1.pos[0], v2.pos[0]});
                float tri_min_z = std::min({v0.pos[2], v1.pos[2], v2.pos[2]});
                float tri_max_z = std::max({v0.pos[2], v1.pos[2], v2.pos[2]});
    
                // compute overlapping tile range
                uint32_t tile_min_x = static_cast<uint32_t>(std::floor((tri_min_x - min_x) / tile_width));
                uint32_t tile_max_x = std::min(tile_count - 1, static_cast<uint32_t>(std::floor((tri_max_x - min_x - epsilon) / tile_width)));
                uint32_t tile_min_z = static_cast<uint32_t>(std::floor((tri_min_z - min_z) / tile_depth));
                uint32_t tile_max_z = std::min(tile_count - 1, static_cast<uint32_t>(std::floor((tri_max_z - min_z - epsilon) / tile_depth)));
    
                // add triangle to each overlapping tile
                for (uint32_t tz = tile_min_z; tz <= tile_max_z; ++tz)
                {
                    for (uint32_t tx = tile_min_x; tx <= tile_max_x; ++tx)
                    {
                        uint32_t tile_index = tz * tile_count + tx;
                        float tile_center_x = tile_offsets[tile_index].x;
                        float tile_center_z = tile_offsets[tile_index].z;
    
                        // lock the tile
                        std::lock_guard<std::mutex> lock(tile_mutexes[tile_index]);
    
                        // add vertices and indices
                        auto& map = global_to_local_indices[tile_index];
                        for (uint32_t j = 0; j < 3; ++j)
                        {
                            uint32_t global_idx = terrain_indices[i + j];
                            uint32_t local_idx;
                            auto it = map.find(global_idx);
                            if (it != map.end())
                            {
                                local_idx = it->second;
                            }
                            else
                            {
                                RHI_Vertex_PosTexNorTan vertex_local = terrain_vertices[global_idx];
                                vertex_local.pos[0] -= tile_center_x;
                                vertex_local.pos[2] -= tile_center_z;
                                tiled_vertices[tile_index].push_back(vertex_local);
                                local_idx = static_cast<uint32_t>(tiled_vertices[tile_index].size() - 1);
                                map[global_idx] = local_idx;
                            }
                            tiled_indices[tile_index].push_back(local_idx);
                        }
                    }
                }
            }
        };
    
        // execute parallel loop over triangles
        ThreadPool::ParallelLoop(process_triangles, triangle_count);
    
        // clean up empty tiles
        for (uint32_t i = 0; i < total_tiles; ++i)
        {
            if (tiled_vertices[i].empty())
            {
                tiled_vertices[i].clear();
                tiled_indices[i].clear();
                global_to_local_indices[i].clear();
                tile_offsets[i] = math::Vector3::Zero;
            }
        }
    }
}
