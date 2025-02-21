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

         // always give credit where credit is due
        const int major = MESHOPTIMIZER_VERSION / 1000;
        const int minor = (MESHOPTIMIZER_VERSION % 1000) / 10;
        const int rev   = MESHOPTIMIZER_VERSION % 10;
        Settings::RegisterThirdPartyLib("meshoptimizer", std::to_string(major) + "." + std::to_string(minor) + "." + std::to_string(rev), "https://github.com/zeux/meshoptimizer");

        registered = true;
    }

    static void simplify(std::vector<uint32_t>& indices, const std::vector<RHI_Vertex_PosTexNorTan>& vertices, size_t triangle_target)
    {
        register_meshoptimizer();

        float reduction               = 0.1f;
        float error                   = 0.1f;
        size_t index_count            = indices.size();
        size_t current_triangle_count = indices.size() / 3;

        if (triangle_target >= current_triangle_count)
            return;

        // loop until the current triangle count is less than or equal to the target triangle count
        std::vector<uint32_t> indices_simplified(index_count);
        while (current_triangle_count > triangle_target)
        {
            float threshold           = 1.0f - reduction;
            size_t target_index_count = static_cast<size_t>(index_count * threshold);

            if (target_index_count < 3)
                break;

            size_t index_count_new = meshopt_simplify(
                indices_simplified.data(),
                indices.data(),
                index_count,
                &vertices[0].pos[0],
                static_cast<uint32_t>(vertices.size()),
                sizeof(RHI_Vertex_PosTexNorTan),
                target_index_count,
                error
            );

            // break if meshopt_simplify can't simplify further
            if (index_count_new == index_count)
                break;

            index_count = index_count_new;
            indices.assign(indices_simplified.begin(), indices_simplified.begin() + index_count);
            current_triangle_count = index_count / 3;
            reduction              = fmodf(reduction + 0.1f, 1.0f);
            error                  = fmodf(error + 0.1f, 1.0f);
        }
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
            auto get_triangle_target = [](size_t triangle_count)
            {
                std::tuple<float, size_t> agressivness_table[] =
                {
                    { 0.2f, 20000 },  // ultra aggressive
                    { 0.4f, 10000  }, // aggressive
                    { 0.6f, 5000  },  // balanced
                    { 0.8f, 2500  }   // gentle
                };
            
                for (const auto& [reduction_percentage, triangle_threshold] : agressivness_table)
                {
                    if (triangle_count > triangle_threshold)
                    {
                        return static_cast<size_t>(triangle_count * reduction_percentage);
                    }
                }
                return triangle_count; // native
            };

            simplify(indices, vertices, get_triangle_target(indices.size() / 3));
        }

        // optimization #5: removed unused vertices
        {
            // create a remap table for compacting vertices
            std::vector<uint32_t> vertex_remap(vertex_count, static_cast<uint32_t>(-1));
            uint32_t new_vertex_count = 0;

            // assign new indices and mark used vertices
            for (uint32_t& index : indices)
            {
                if (vertex_remap[index] == static_cast<uint32_t>(-1))
                {
                    vertex_remap[index] = new_vertex_count++;
                }
                index = vertex_remap[index];
            }

            // create the compacted vertex buffer
            std::vector<RHI_Vertex_PosTexNorTan> compacted_vertices(new_vertex_count);
            for (size_t old_index = 0; old_index < vertex_count; ++old_index)
            {
                if (vertex_remap[old_index] != static_cast<uint32_t>(-1))
                {
                    compacted_vertices[vertex_remap[old_index]] = vertices[old_index];
                }
            }

            // replace the original vertex buffer with the compacted one
            vertices = std::move(compacted_vertices);
        }
    }
}
