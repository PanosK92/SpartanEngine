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
#include "../rhi/RHI_Vertex.h"
#include "../rendering/Renderer_Buffers.h"
#include "../math/BoundingBox.h"
//======================================

namespace spartan::geometry_processing
{
    void simplify(
        std::vector<uint32_t>& indices,
        std::vector<RHI_Vertex_PosTexNorTan>& vertices,
        size_t target_index_count,
        const bool preserve_uvs,  // typically true, false for non-rendered geometry like physics meshes
        const bool preserve_edges // for terrain tiles where edges must match neighboring tiles
    );

    void optimize(
        std::vector<RHI_Vertex_PosTexNorTan>& vertices,
        std::vector<uint32_t>& indices
    );

    // welds identical vertices and reorders for the caches, what comes out draws pixel for pixel the
    // same as what went in. optimize() also simplifies once a mesh passes a size threshold, which is
    // not wanted when the caller is only trying to make geometry cheaper to draw
    void weld_and_optimize(
        std::vector<RHI_Vertex_PosTexNorTan>& vertices,
        std::vector<uint32_t>& indices
    );

    // builds per-lod meshlets and repacks the indices in meshlet order
    // also emits unique vertex remaps + micro-indices for the mesh shader path
    // the returned bounds match that order and reference the packed unique/micro outputs
    void build_meshlets(
        const std::vector<RHI_Vertex_PosTexNorTan>& vertices,
        std::vector<uint32_t>& indices,
        std::vector<Sb_MeshletBounds>& meshlets_out,
        std::vector<uint32_t>& unique_vertices_out,
        std::vector<uint32_t>& micro_indices_out,
        math::BoundingBox& lod_aabb_out
    );

    void split_surface_into_tiles(
        const std::vector<RHI_Vertex_PosTexNorTan>& terrain_vertices,
        const std::vector<uint32_t>& terrain_indices,
        const uint32_t tile_count,
        std::vector<std::vector<RHI_Vertex_PosTexNorTan>>& tiled_vertices,
        std::vector<std::vector<uint32_t>>& tiled_indices,
        std::vector<math::Vector3>& tile_offsets
    );
}
