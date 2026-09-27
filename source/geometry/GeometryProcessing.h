/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
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
        const bool preserve_edges, // for terrain tiles where edges must match neighboring tiles
        const bool prune_components = false // distance LODs may remove small disconnected details
    );

    // distance lod for leaf cards, error driven simplification prunes every card at once because each
    // one is smaller than the error bound, this keeps a nested subset of whole cards instead and grows
    // them about their centre so the crown keeps its cover, large connected pieces pass through untouched
    void thin_foliage_cards(
        std::vector<uint32_t>& indices,
        std::vector<RHI_Vertex_PosTexNorTan>& vertices,
        size_t target_index_count
    );

    // direction of hemi-octahedral impostor frame (x, y) out of frames per axis, and the image plane basis it
    // was baked with, impostor.hlsl decodes the same way so a card lines up with the frame it samples
    math::Vector3 impostor_frame_direction(uint32_t x, uint32_t y, uint32_t frames);
    void impostor_frame_basis(const math::Vector3& direction, math::Vector3& right, math::Vector3& up);

    // orthographic views of a mesh from every frame direction, per texel and per layer, nearest first:
    // word 0 is the source uv as unorm16 inside [uv_min, uv_min + uv_scale], word 1 is the frame space
    // normal as oct unorm8, the depth toward the viewer as unorm15 over [-radius, radius] and a coverage bit
    // texel (px, py) of layer l in frame (fx, fy) starts at ((((fy * frames + fx) * res + py) * res + px) * layers + l) * 2
    void bake_impostor(
        const std::vector<RHI_Vertex_PosTexNorTan>& vertices,
        const std::vector<uint32_t>& indices,
        uint32_t frames,
        uint32_t resolution,
        uint32_t layers,
        const math::Vector3& center,
        float radius,
        const math::Vector2& uv_min,
        const math::Vector2& uv_scale,
        std::vector<uint32_t>& texels_out
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

    // splits a row major grid surface into tile_count x tile_count tiles, each tile is a rectangular
    // block of cells so no hashing or locking is needed, vertices come out relative to the tile offset
    void split_grid_into_tiles(
        const std::vector<RHI_Vertex_PosTexNorTan>& grid_vertices,
        const uint32_t grid_width,
        const uint32_t grid_height,
        const uint32_t tile_count,
        std::vector<std::vector<RHI_Vertex_PosTexNorTan>>& tiled_vertices,
        std::vector<std::vector<uint32_t>>& tiled_indices,
        std::vector<math::Vector3>& tile_offsets
    );
}
