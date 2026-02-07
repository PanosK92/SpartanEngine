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
    static void simplify(
        std::vector<uint32_t>& indices,
        std::vector<RHI_Vertex_PosTexNorTan>& vertices,
        size_t target_index_count,
        const bool preserve_uvs,  // typically true, false for non-rendered geometry like physics meshes
        const bool preserve_edges // for terrain tiles where edges must match neighboring tiles
    )
    {
        size_t index_count  = indices.size();
        size_t vertex_count = vertices.size();

        // early exit conditions
        if (target_index_count >= index_count)
            return;

        if (vertex_count <= 16 || index_count < 12)
            return;

        target_index_count = std::max(target_index_count, static_cast<size_t>(12));

        // output buffer for simplified indices
        std::vector<uint32_t> indices_simplified(index_count);

        // build vertex lock array for edge preservation (terrain tiles)
        std::vector<unsigned char> vertex_locks;
        if (preserve_edges)
        {
            vertex_locks.resize(vertex_count, 0);

            // compute bounding box
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

            // lock vertices on bounding box perimeter
            const float edge_tolerance = 0.01f;
            for (size_t i = 0; i < vertex_count; ++i)
            {
                float x = vertices[i].pos[0];
                float z = vertices[i].pos[2];
                if (std::abs(x - min_x) < edge_tolerance || std::abs(x - max_x) < edge_tolerance ||
                    std::abs(z - min_z) < edge_tolerance || std::abs(z - max_z) < edge_tolerance)
                {
                    vertex_locks[i] = 1;
                }
            }
        }

        // prepare uv attribute buffer for attribute-aware simplification
        std::vector<float> attr_buffer;
        const float* vertex_attributes        = nullptr;
        size_t attr_stride                    = 0;
        size_t attr_count                     = 0;
        static constexpr float uv_weights[2]  = { 0.5f, 0.5f };
        const float* attr_weights             = nullptr;

        if (preserve_uvs)
        {
            attr_buffer.resize(vertex_count * 2);
            for (size_t i = 0; i < vertex_count; ++i)
            {
                attr_buffer[i * 2 + 0] = vertices[i].tex[0];
                attr_buffer[i * 2 + 1] = vertices[i].tex[1];
            }
            vertex_attributes = attr_buffer.data();
            attr_stride       = sizeof(float) * 2;
            attr_weights      = uv_weights;
            attr_count        = 2;
        }

        const unsigned char* locks = preserve_edges ? vertex_locks.data() : nullptr;

        // strategic error thresholds - fewer iterations with larger steps
        // meshoptimizer tries to reach target_index_count while staying within error bound
        static constexpr float error_thresholds[] = { 0.02f, 0.05f, 0.1f, 0.2f, 0.5f, 1.0f };
        static constexpr size_t threshold_count   = sizeof(error_thresholds) / sizeof(error_thresholds[0]);

        float lod_error        = 0.0f;
        size_t result_count    = index_count;
        size_t best_count      = index_count;
        bool target_reached    = false;

        for (size_t i = 0; i < threshold_count && !target_reached; ++i)
        {
            result_count = meshopt_simplifyWithAttributes(
                indices_simplified.data(),
                indices.data(),
                index_count,
                &vertices[0].pos[0],
                vertex_count,
                sizeof(RHI_Vertex_PosTexNorTan),
                vertex_attributes,
                attr_stride,
                attr_weights,
                attr_count,
                locks,
                target_index_count,
                error_thresholds[i],
                0,
                &lod_error
            );

            // check if we reached the target or made meaningful progress
            if (result_count <= target_index_count)
            {
                target_reached = true;
            }
            else if (result_count < best_count)
            {
                best_count = result_count;
            }
            else
            {
                // no progress at this error level, increasing error won't help
                break;
            }
        }

        // fallback: use sloppy simplification for aggressive reduction (ignores topology/attributes)
        if (result_count > target_index_count && !preserve_edges && target_index_count >= 12)
        {
            size_t sloppy_count = meshopt_simplifySloppy(
                indices_simplified.data(),
                indices.data(),
                index_count,
                &vertices[0].pos[0],
                vertex_count,
                sizeof(RHI_Vertex_PosTexNorTan),
                target_index_count,
                FLT_MAX,
                &lod_error
            );

            if (sloppy_count > 0 && sloppy_count < result_count)
            {
                result_count = sloppy_count;
            }
        }

        // safeguard: if simplification collapsed to zero indices, keep original mesh
        if (result_count == 0)
        {
            SP_LOG_WARNING("simplification collapsed mesh to zero indices, keeping original");
            return;
        }

        // commit simplified indices
        indices.assign(indices_simplified.begin(), indices_simplified.begin() + result_count);

        // optimize vertex buffer: removes unused vertices and reorders for cache efficiency
        std::vector<RHI_Vertex_PosTexNorTan> optimized_vertices(vertex_count);
        size_t optimized_vertex_count = meshopt_optimizeVertexFetch(
            optimized_vertices.data(),
            indices.data(),
            result_count,
            vertices.data(),
            vertex_count,
            sizeof(RHI_Vertex_PosTexNorTan)
        );

        vertices.assign(optimized_vertices.begin(), optimized_vertices.begin() + optimized_vertex_count);
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
        // find surface bounds
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
    
        // execute in parallel
        ThreadPool::ParallelLoop(process_triangles, triangle_count);
    }
}
