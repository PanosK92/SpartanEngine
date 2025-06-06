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
#include <array>
#include <functional>
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

    static void simplify(std::vector<uint32_t>& indices, std::vector<RHI_Vertex_PosTexNorTan>& vertices, size_t target_index_count)
    {
        register_meshoptimizer();
    
        // starting parameters
        float error                   = 0.01f; // initial error tolerance
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
    
        // simplification loop up to error = 1.0
        float lod_error = 0.0f;
        while (current_triangle_count > (target_index_count / 3) && error <= 1.0f)
        {
            if (target_index_count < 3)
                break;
    
            size_t index_count_new = meshopt_simplify(
                indices_simplified.data(),              // destination for simplified indices
                indices.data(),                         // source indices
                index_count,                            // current index count
                &vertices[0].pos[0],                    // vertex position data
                static_cast<uint32_t>(vertices.size()), // vertex count
                sizeof(RHI_Vertex_PosTexNorTan),        // vertex stride
                target_index_count,                     // desired index count
                error,                                  // error tolerance
                0,                                      // options (default)
                &lod_error                              // output error
            );
    
            // update indices and triangle count
            index_count = index_count_new;
            indices.assign(indices_simplified.begin(), indices_simplified.begin() + index_count);
            current_triangle_count = index_count / 3;
    
            // increase error linearly
            error += 0.1f;
        }
    
        // second attempt: use meshopt_simplifySloppy with fallback if indices become zero
        if (current_triangle_count > (target_index_count / 3))
        {
            if (target_index_count >= 3)
            {
                float target_error     = FLT_MAX;
                size_t index_count_new = 0;
                
                // Keep trying with reduced aggressiveness until we get valid indices
                do
                {
                    index_count_new = meshopt_simplifySloppy(
                        indices_simplified.data(),
                        indices.data(),
                        index_count,
                        &vertices[0].pos[0],
                        static_cast<uint32_t>(vertices.size()),
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
                    index_count = index_count_new;
                    indices.assign(indices_simplified.begin(), indices_simplified.begin() + index_count);
                    current_triangle_count = index_count / 3;
                }
            }
        }
    
        // aggressive simplification can produce nothing - we never want that
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
    
        // create a remap table
        std::vector<unsigned int> remap(index_count);
        size_t vertex_count_optimized = meshopt_generateVertexRemap(remap.data(), 
                                                            indices.data(),
                                                            index_count,
                                                            vertices.data(),
                                                            vertex_count,
                                                            sizeof(RHI_Vertex_PosTexNorTan));
    
        // note: when we import with Assimp, JoinIdenticalVertices is used, so we don't need to remove duplicates here
    
        // optimization #1: improve the locality of the vertices
        meshopt_optimizeVertexCache(indices.data(), indices.data(), index_count, vertex_count);
    
        // optimization #2: reduce pixel overdraw
        meshopt_optimizeOverdraw(indices.data(), indices.data(), index_count, &(vertices[0].pos[0]), vertex_count, sizeof(RHI_Vertex_PosTexNorTan), 1.05f);
    
        // optimization #3: optimize access to the vertex buffer
        meshopt_optimizeVertexFetch(vertices.data(), indices.data(), index_count, vertices.data(), vertex_count, sizeof(RHI_Vertex_PosTexNorTan));
    
        // optimization #4: create a simplified version of the mesh while trying to maintain the topology
        {
            auto get_target_index_count = [](size_t index_count)
            {
                std::tuple<float, size_t> aggressiveness_table[] =
                {
                    { 0.2f, 60000 },  // ultra aggressive (20000 triangles * 3)
                    { 0.4f, 30000 },  // aggressive (10000 triangles * 3)
                    { 0.6f, 15000 },  // balanced (5000 triangles * 3)
                    { 0.8f, 7500  }   // gentle (2500 triangles * 3)
                };
            
                for (const auto& [reduction_percentage, index_threshold] : aggressiveness_table)
                {
                    if (index_count > index_threshold)
                        return static_cast<size_t>(index_count * reduction_percentage);
                }
        
                return index_count; // native
            };
        
            simplify(indices, vertices, get_target_index_count(indices.size()));
        }
    }

    static void split_surface_into_tiles(
        const std::vector<RHI_Vertex_PosTexNorTan>& terrain_vertices,
        const std::vector<uint32_t>& terrain_indices,
        const uint32_t tile_count,
        std::vector<std::vector<RHI_Vertex_PosTexNorTan>>& tiled_vertices,
        std::vector<std::vector<uint32_t>>& tiled_indices
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

        // initialize output containers and mutexes
        const uint32_t total_tiles = tile_count * tile_count;
        tiled_vertices.resize(total_tiles);
        tiled_indices.resize(total_tiles);
        std::vector<std::unordered_map<uint32_t, uint32_t>> global_to_local_indices(total_tiles);
        std::vector<std::mutex> tile_mutexes(total_tiles);
    
        // calculate number of triangles
        uint32_t triangle_count = static_cast<uint32_t>(terrain_indices.size()) / 3;
    
        // parallel processing of triangles
        auto process_triangles = [&terrain_vertices, &terrain_indices, tile_count, min_x, min_z, tile_width, tile_depth, &tiled_vertices, &tiled_indices, &global_to_local_indices, &tile_mutexes](uint32_t start_tri, uint32_t end_tri)
        {
            for (uint32_t tri = start_tri; tri < end_tri; ++tri)
            {
                // get starting index of the triangle
                uint32_t i = tri * 3;
    
                // assign triangle to tile based on first vertex
                const auto& vertex  = terrain_vertices[terrain_indices[i]];
                uint32_t tile_x     = std::min(static_cast<uint32_t>((vertex.pos[0] - min_x) / tile_width), tile_count - 1);
                uint32_t tile_z     = std::min(static_cast<uint32_t>((vertex.pos[2] - min_z) / tile_depth), tile_count - 1);
                uint32_t tile_index = tile_z * tile_count + tile_x;
    
                // lock the tile to prevent concurrent access
                std::lock_guard<std::mutex> lock(tile_mutexes[tile_index]);
    
                // add all three vertices to the tile
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
                        tiled_vertices[tile_index].push_back(terrain_vertices[global_idx]);
                        local_idx = static_cast<uint32_t>(tiled_vertices[tile_index].size() - 1);
                        map[global_idx] = local_idx;
                    }
                    tiled_indices[tile_index].push_back(local_idx);
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
            }
        }
    }

    static void remove_duplicate_vertices(std::vector<RHI_Vertex_PosTexNorTan>& vertices, std::vector<uint32_t>& indices)
    {
        register_meshoptimizer();
    
        if (vertices.empty() || indices.empty())
        {
            SP_LOG_WARNING("Cannot remove duplicate vertices: vertex or index data is empty");
            return;
        }
    
        // ensure indices are a multiple of 3 (triangle mesh requirement)
        if (indices.size() % 3 != 0)
        {
            SP_LOG_ERROR("Index count must be a multiple of 3 for a valid triangle mesh");
            indices.clear();
            vertices.clear();
            return;
        }
    
        // hash function for vertex positions with 5cm tolerance (0.05 units)
        struct Vec3Hash
        {
            size_t operator()(const math::Vector3& v) const
            {
                // Quantize positions to 5cm grid to match PhysX welding tolerance
                const float tolerance = 0.05f;
                int x = static_cast<int>(std::round(v.x / tolerance));
                int y = static_cast<int>(std::round(v.y / tolerance));
                int z = static_cast<int>(std::round(v.z / tolerance));
                return std::hash<int>()(x) ^ (std::hash<int>()(y) << 1) ^ (std::hash<int>()(z) << 2);
            }
        };
    
        // equality comparison for vertex positions with 5cm tolerance
        struct Vec3Equal
        {
            bool operator()(const math::Vector3& a, const math::Vector3& b) const
            {
                const float tolerance = 0.05f;
                return std::abs(a.x - b.x) < tolerance &&
                       std::abs(a.y - b.y) < tolerance &&
                       std::abs(a.z - b.z) < tolerance;
            }
        };
    
        // map from quantized position to new vertex index
        std::unordered_map<math::Vector3, uint32_t, Vec3Hash, Vec3Equal> pos_to_index;
        std::vector<RHI_Vertex_PosTexNorTan> new_vertices;
        std::vector<uint32_t> remap(vertices.size());
    
        // build new vertex list and remap table
        for (size_t i = 0; i < vertices.size(); ++i)
        {
            math::Vector3 pos(vertices[i].pos[0], vertices[i].pos[1], vertices[i].pos[2]);
            auto it = pos_to_index.find(pos);
            if (it == pos_to_index.end())
            {
                // new unique position
                uint32_t new_index = static_cast<uint32_t>(new_vertices.size());
                pos_to_index[pos] = new_index;
                new_vertices.push_back(vertices[i]);
                remap[i] = new_index;
            }
            else
            {
                // duplicate position, map to existing vertex
                remap[i] = it->second;
            }
        }
    
        // remap indices
        for (auto& index : indices)
        {
            index = remap[index];
        }
    
        // rRemove degenerate triangles
        std::vector<uint32_t> new_indices;
        new_indices.reserve(indices.size());
        for (size_t i = 0; i < indices.size(); i += 3)
        {
            uint32_t i0 = indices[i];
            uint32_t i1 = indices[i + 1];
            uint32_t i2 = indices[i + 2];
            if (i0 != i1 && i0 != i2 && i1 != i2)
            {
                // non-degenerate triangle
                new_indices.push_back(i0);
                new_indices.push_back(i1);
                new_indices.push_back(i2);
            }
        }
    
        // update vertices and indices
        vertices = std::move(new_vertices);
        indices  = std::move(new_indices);
    
        // validate the result
        if (vertices.empty() || indices.empty() || indices.size() % 3 != 0)
        {
            SP_LOG_ERROR("Mesh is invalid after removing duplicate vertices");
            vertices.clear();
            indices.clear();
            return;
        }
    }
}
