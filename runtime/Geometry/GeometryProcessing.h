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

#pragma once

//= INCLUDES ===========================
#include <vector>
#include "../RHI/RHI_Vertex.h"
#ifdef _WIN32
SP_WARNINGS_OFF
#include "meshoptimizer/meshoptimizer.h"
SP_WARNINGS_ON
#else
#include "meshoptimizer.h"
#endif
//======================================

namespace spartan::geometry_processing
{
    static void register_meshoptimizer()
    {
        static std::atomic<bool> registered = false;
        if (registered)
            return;

         // always give credit where credit is due
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
        const std::vector<RHI_Vertex_PosTexNorTan>& terrain_vertices, const std::vector<uint32_t>& terrain_indices,
        const uint32_t tile_count,
        std::vector<std::vector<RHI_Vertex_PosTexNorTan>>& tiled_vertices, std::vector<std::vector<uint32_t>>& tiled_indices
    )
    {
        // initialize min and max values for terrain bounds
        float min_x = std::numeric_limits<float>::max();
        float max_x = std::numeric_limits<float>::lowest();
        float min_z = std::numeric_limits<float>::max();
        float max_z = std::numeric_limits<float>::lowest();
    
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
        std::vector<std::unordered_map<uint32_t, uint32_t>> global_to_local_indices(tile_count * tile_count);
    
        // assign vertices to tiles and track their indices
        for (uint32_t global_index = 0; global_index < terrain_vertices.size(); ++global_index)
        {
            const RHI_Vertex_PosTexNorTan& vertex = terrain_vertices[global_index];
    
            uint32_t tile_x = static_cast<uint32_t>((vertex.pos[0] - min_x) / tile_width);
            uint32_t tile_z = static_cast<uint32_t>((vertex.pos[2] - min_z) / tile_depth);
            tile_x          = std::min(tile_x, tile_count - 1);
            tile_z          = std::min(tile_z, tile_count - 1);
    
            // convert the 2D tile coordinates into a single index for the 1D output array
            uint32_t tile_index = tile_z * tile_count + tile_x;
    
            // add vertex to the appropriate tile
            tiled_vertices[tile_index].push_back(vertex);
    
            // track the local index of this vertex in the tile
            uint32_t local_index = static_cast<uint32_t>(tiled_vertices[tile_index].size() - 1);
            global_to_local_indices[tile_index][global_index] = local_index;
        }
    
        auto add_shared_vertex = [tile_count](
            uint32_t tile_x, uint32_t tile_z, uint32_t global_index,
            const std::vector<RHI_Vertex_PosTexNorTan>&terrain_vertices, std::vector<std::vector<RHI_Vertex_PosTexNorTan>>&tiled_vertices,
            std::vector<std::unordered_map<uint32_t, uint32_t>>&global_to_local_indices, std::vector<std::vector<uint32_t>>&tiled_indices)
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
            tile_x                                = std::min(tile_x, tile_count - 1);
            tile_z                                = std::min(tile_z, tile_count - 1);
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
                bool is_on_horizontal_edge = fmod(current_vertex.pos[0] - min_x, tile_width) <= std::numeric_limits<float>::epsilon() && tile_x > 0;
                bool is_on_vertical_edge   = fmod(current_vertex.pos[2] - min_z, tile_depth) <= std::numeric_limits<float>::epsilon() && tile_z > 0;
    
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
