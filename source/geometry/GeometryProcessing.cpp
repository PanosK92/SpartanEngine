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

//= INCLUDES ===========================
#include "pch.h"
#include "GeometryProcessing.h"
#include "../core/ThreadPool.h"
#include <unordered_map>
#include <cmath>
SP_WARNINGS_OFF
#include "meshoptimizer/meshoptimizer.h"
SP_WARNINGS_ON
//======================================

namespace spartan::geometry_processing
{
    namespace
    {
        // meshlet generation tunables, matches meshoptimizer recommendations
        constexpr size_t meshlet_max_vertices  = 64;
        constexpr size_t meshlet_max_triangles = 124;
    }

    void simplify(
        std::vector<uint32_t>& indices,
        std::vector<RHI_Vertex_PosTexNorTan>& vertices,
        size_t target_index_count,
        const bool preserve_uvs,
        const bool preserve_edges
    )
    {
        size_t index_count  = indices.size();
        size_t vertex_count = vertices.size();

        // early exit conditions
        if (target_index_count >= index_count)
        {
            return;
        }

        if (vertex_count <= 16 || index_count < 12)
        {
            return;
        }

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
                math::Vector2 uv      = vertices[i].get_uv();
                attr_buffer[i * 2 + 0] = uv.x;
                attr_buffer[i * 2 + 1] = uv.y;
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

    void optimize(std::vector<RHI_Vertex_PosTexNorTan>& vertices, std::vector<uint32_t>& indices)
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

    void weld_and_optimize(std::vector<RHI_Vertex_PosTexNorTan>& vertices, std::vector<uint32_t>& indices)
    {
        if (vertices.empty() || indices.empty())
        {
            return;
        }

        size_t index_count = indices.size();

        // weld, geometry that was assembled from separate pieces carries a duplicate for every vertex
        // the pieces had in common
        {
            std::vector<unsigned int> remap(vertices.size());
            const size_t welded_count = meshopt_generateVertexRemap(
                remap.data(),
                indices.data(),
                index_count,
                vertices.data(),
                vertices.size(),
                sizeof(RHI_Vertex_PosTexNorTan)
            );

            std::vector<uint32_t> indices_remapped(index_count);
            meshopt_remapIndexBuffer(indices_remapped.data(), indices.data(), index_count, remap.data());
            indices = std::move(indices_remapped);

            std::vector<RHI_Vertex_PosTexNorTan> vertices_remapped(welded_count);
            meshopt_remapVertexBuffer(vertices_remapped.data(), vertices.data(), vertices.size(), sizeof(RHI_Vertex_PosTexNorTan), remap.data());
            vertices = std::move(vertices_remapped);
        }

        const size_t vertex_count = vertices.size();
        meshopt_optimizeVertexCache(indices.data(), indices.data(), index_count, vertex_count);
        meshopt_optimizeOverdraw(indices.data(), indices.data(), index_count, &vertices[0].pos[0], vertex_count, sizeof(RHI_Vertex_PosTexNorTan), 1.05f);
        meshopt_optimizeVertexFetch(vertices.data(), indices.data(), index_count, vertices.data(), vertex_count, sizeof(RHI_Vertex_PosTexNorTan));
    }

    void build_meshlets(
        const std::vector<RHI_Vertex_PosTexNorTan>& vertices,
        std::vector<uint32_t>& indices,
        std::vector<Sb_MeshletBounds>& meshlets_out,
        std::vector<uint32_t>& unique_vertices_out,
        std::vector<uint32_t>& micro_indices_out,
        math::BoundingBox& lod_aabb_out
    )
    {
        meshlets_out.clear();
        unique_vertices_out.clear();
        micro_indices_out.clear();
        lod_aabb_out = math::BoundingBox::Zero;

        const size_t index_count  = indices.size();
        const size_t vertex_count = vertices.size();
        if (index_count == 0 || vertex_count == 0)
        {
            return;
        }

        // compute the lod-local aabb up-front, the compressed meshlet bounds quantize center/radius against it and the cull shader receives the same aabb via DrawData.lod_aabb_*
        lod_aabb_out                       = math::BoundingBox(vertices.data(), static_cast<uint32_t>(vertices.size()));
        const math::Vector3 aabb_min       = lod_aabb_out.GetMin();
        const math::Vector3 aabb_extent    = lod_aabb_out.GetMax() - aabb_min;
        // guard against zero-extent axes (single-plane or degenerate meshes) so the dequant doesn't divide by zero on the cpu or shader
        const math::Vector3 aabb_extent_safe(
            aabb_extent.x > 1e-8f ? aabb_extent.x : 1e-8f,
            aabb_extent.y > 1e-8f ? aabb_extent.y : 1e-8f,
            aabb_extent.z > 1e-8f ? aabb_extent.z : 1e-8f
        );
        const float aabb_diag              = aabb_extent_safe.Length();
        const float aabb_diag_safe         = aabb_diag > 1e-8f ? aabb_diag : 1e-8f;
        // worst-case center quantization error magnitude, half a step per axis combined in 3d
        const math::Vector3 center_step(
            aabb_extent_safe.x / 65535.0f,
            aabb_extent_safe.y / 65535.0f,
            aabb_extent_safe.z / 65535.0f
        );
        const float center_quant_err_3d    = center_step.Length() * 0.5f;

        // worst case meshlet count, used for buffer allocation
        const size_t max_meshlets = meshopt_buildMeshletsBound(index_count, meshlet_max_vertices, meshlet_max_triangles);

        std::vector<meshopt_Meshlet> meshlets(max_meshlets);
        std::vector<unsigned int> meshlet_vertices(max_meshlets * meshlet_max_vertices);
        std::vector<unsigned char> meshlet_triangles(max_meshlets * meshlet_max_triangles * 3);

        // cone_weight 0.5 biases meshlet builds toward triangles with similar normals so cone backface culling is effective
        const size_t meshlet_count = meshopt_buildMeshlets(
            meshlets.data(),
            meshlet_vertices.data(),
            meshlet_triangles.data(),
            indices.data(),
            index_count,
            &vertices[0].pos[0],
            vertex_count,
            sizeof(RHI_Vertex_PosTexNorTan),
            meshlet_max_vertices,
            meshlet_max_triangles,
            0.5f
        );

        if (meshlet_count == 0)
        {
            return;
        }

        // trim the worst-case allocation and tightly-pack the last meshlet (per meshopt docs)
        const meshopt_Meshlet& last = meshlets[meshlet_count - 1];
        meshlet_vertices.resize(last.vertex_offset + last.vertex_count);
        meshlet_triangles.resize(last.triangle_offset + ((last.triangle_count * 3 + 3) & ~3));
        meshlets.resize(meshlet_count);

        // repack the lod's index buffer so each meshlet occupies a contiguous range (vs fallback path)
        // also keep unique verts + micro indices for the mesh shader path
        std::vector<uint32_t> repacked;
        repacked.reserve(index_count);
        unique_vertices_out.reserve(meshlet_vertices.size());
        micro_indices_out.reserve(index_count);
        meshlets_out.reserve(meshlet_count);

        for (size_t m = 0; m < meshlet_count; ++m)
        {
            const meshopt_Meshlet& meshlet = meshlets[m];

            Sb_MeshletBounds bounds       = {};
            const uint32_t first_index    = static_cast<uint32_t>(repacked.size());
            const uint32_t first_vertex   = static_cast<uint32_t>(unique_vertices_out.size());
            const uint32_t first_micro    = static_cast<uint32_t>(micro_indices_out.size());
            const uint32_t triangle_count = meshlet.triangle_count;
            const uint32_t vertex_count_m = meshlet.vertex_count;

            // unique vertex remap for mesh shaders
            for (uint32_t v = 0; v < vertex_count_m; ++v)
            {
                unique_vertices_out.push_back(meshlet_vertices[meshlet.vertex_offset + v]);
            }

            // emit absolute indices for the vs fallback and micro indices for the mesh path
            for (uint32_t t = 0; t < triangle_count; ++t)
            {
                const uint8_t i0 = meshlet_triangles[meshlet.triangle_offset + t * 3 + 0];
                const uint8_t i1 = meshlet_triangles[meshlet.triangle_offset + t * 3 + 1];
                const uint8_t i2 = meshlet_triangles[meshlet.triangle_offset + t * 3 + 2];

                repacked.push_back(meshlet_vertices[meshlet.vertex_offset + i0]);
                repacked.push_back(meshlet_vertices[meshlet.vertex_offset + i1]);
                repacked.push_back(meshlet_vertices[meshlet.vertex_offset + i2]);

                micro_indices_out.push_back(i0);
                micro_indices_out.push_back(i1);
                micro_indices_out.push_back(i2);
            }

            // bounding sphere for hi-z meshlet culling
            meshopt_Bounds meshlet_bounds = meshopt_computeMeshletBounds(
                &meshlet_vertices[meshlet.vertex_offset],
                &meshlet_triangles[meshlet.triangle_offset],
                triangle_count,
                &vertices[0].pos[0],
                vertex_count,
                sizeof(RHI_Vertex_PosTexNorTan)
            );

            // quantize center against the lod aabb, clamp first so any tiny float drift past the aabb edges doesn't underflow the unorm round
            const float tx    = std::clamp((meshlet_bounds.center[0] - aabb_min.x) / aabb_extent_safe.x, 0.0f, 1.0f);
            const float ty    = std::clamp((meshlet_bounds.center[1] - aabb_min.y) / aabb_extent_safe.y, 0.0f, 1.0f);
            const float tz    = std::clamp((meshlet_bounds.center[2] - aabb_min.z) / aabb_extent_safe.z, 0.0f, 1.0f);
            const uint32_t cx = static_cast<uint32_t>(tx * 65535.0f + 0.5f);
            const uint32_t cy = static_cast<uint32_t>(ty * 65535.0f + 0.5f);
            const uint32_t cz = static_cast<uint32_t>(tz * 65535.0f + 0.5f);

            // expand radius by the center quantization error and the unorm step that radius itself will round to, then round up so the encoded sphere always covers the true sphere
            const float radius_step    = aabb_diag_safe / 65535.0f;
            const float radius_padded  = meshlet_bounds.radius + center_quant_err_3d + radius_step;
            const float radius_norm    = std::clamp(radius_padded / aabb_diag_safe, 0.0f, 1.0f);
            const uint32_t r_quant     = static_cast<uint32_t>(std::ceil(radius_norm * 65535.0f));
            const uint32_t r           = r_quant > 0xFFFFu ? 0xFFFFu : r_quant;

            bounds.center_xy        = cx | (cy << 16);
            bounds.center_z_radius  = cz | (r << 16);

            // pack the snorm cone axis xyz and cone cutoff into one uint, byte 0..2 axis, byte 3 cutoff
            const uint32_t ax = static_cast<uint32_t>(static_cast<uint8_t>(meshlet_bounds.cone_axis_s8[0]));
            const uint32_t ay = static_cast<uint32_t>(static_cast<uint8_t>(meshlet_bounds.cone_axis_s8[1]));
            const uint32_t az = static_cast<uint32_t>(static_cast<uint8_t>(meshlet_bounds.cone_axis_s8[2]));
            const uint32_t cc = static_cast<uint32_t>(static_cast<uint8_t>(meshlet_bounds.cone_cutoff_s8));
            bounds.cone_axis_cutoff = ax | (ay << 8) | (az << 16) | (cc << 24);

            // pack first_index (25 bits) and triangle_count (7 bits)
            SP_ASSERT_MSG(first_index <= MESHLET_FIRST_INDEX_MAX,    "Meshlet first_index exceeds the 25-bit pack budget");
            SP_ASSERT_MSG(triangle_count <= MESHLET_TRI_COUNT_MASK,  "Meshlet triangle_count exceeds the 7-bit pack budget");
            bounds.first_index_tri_count = (first_index & MESHLET_FIRST_INDEX_MASK) | ((triangle_count & MESHLET_TRI_COUNT_MASK) << MESHLET_TRI_COUNT_SHIFT);

            // pack first_vertex (25 bits) and vertex_count (7 bits)
            SP_ASSERT_MSG(first_vertex <= MESHLET_FIRST_VERTEX_MAX,  "Meshlet first_vertex exceeds the 25-bit pack budget");
            SP_ASSERT_MSG(vertex_count_m <= MESHLET_VERT_COUNT_MASK, "Meshlet vertex_count exceeds the 7-bit pack budget");
            bounds.first_vertex_vert_count = (first_vertex & MESHLET_FIRST_VERTEX_MASK) | ((vertex_count_m & MESHLET_VERT_COUNT_MASK) << MESHLET_VERT_COUNT_SHIFT);
            bounds.first_micro             = first_micro;

            meshlets_out.push_back(bounds);
        }

        // commit repacked index buffer
        indices = std::move(repacked);
    }

    void split_grid_into_tiles(
        const std::vector<RHI_Vertex_PosTexNorTan>& grid_vertices,
        const uint32_t grid_width,
        const uint32_t grid_height,
        const uint32_t tile_count,
        std::vector<std::vector<RHI_Vertex_PosTexNorTan>>& tiled_vertices,
        std::vector<std::vector<uint32_t>>& tiled_indices,
        std::vector<math::Vector3>& tile_offsets
    )
    {
        SP_ASSERT_MSG(grid_width >= 2 && grid_height >= 2, "grid is too small to tile");
        SP_ASSERT_MSG(grid_vertices.size() >= static_cast<size_t>(grid_width) * grid_height, "grid vertex count does not match its dimensions");

        const uint32_t tiles_per_axis = std::max(1u, tile_count);
        const uint32_t total_tiles    = tiles_per_axis * tiles_per_axis;

        tiled_vertices.clear();
        tiled_indices.clear();
        tiled_vertices.resize(total_tiles);
        tiled_indices.resize(total_tiles);
        tile_offsets.assign(total_tiles, math::Vector3::Zero);

        // same cell split the physics heightfield uses, neighbouring tiles share their boundary row and column
        auto cell_range = [tiles_per_axis](const uint32_t tile, const uint32_t samples, uint32_t& start, uint32_t& count)
        {
            start = (tile * (samples - 1)) / tiles_per_axis;
            count = ((tile + 1) * (samples - 1)) / tiles_per_axis - start + 1;
        };

        auto build_tile = [&](const uint32_t tile_begin, const uint32_t tile_end)
        {
            for (uint32_t tile_index = tile_begin; tile_index < tile_end; tile_index++)
            {
                const uint32_t tx = tile_index % tiles_per_axis;
                const uint32_t tz = tile_index / tiles_per_axis;

                uint32_t x_start = 0;
                uint32_t z_start = 0;
                uint32_t x_count = 0;
                uint32_t z_count = 0;
                cell_range(tx, grid_width, x_start, x_count);
                cell_range(tz, grid_height, z_start, z_count);

                // the offset is the centre of the tile footprint, vertices are stored relative to it
                const RHI_Vertex_PosTexNorTan& corner_min = grid_vertices[static_cast<size_t>(z_start) * grid_width + x_start];
                const RHI_Vertex_PosTexNorTan& corner_max = grid_vertices[static_cast<size_t>(z_start + z_count - 1) * grid_width + (x_start + x_count - 1)];
                const float center_x                      = (corner_min.pos[0] + corner_max.pos[0]) * 0.5f;
                const float center_z                      = (corner_min.pos[2] + corner_max.pos[2]) * 0.5f;
                tile_offsets[tile_index]                  = math::Vector3(center_x, 0.0f, center_z);

                std::vector<RHI_Vertex_PosTexNorTan>& vertices = tiled_vertices[tile_index];
                std::vector<uint32_t>& indices                 = tiled_indices[tile_index];
                vertices.resize(static_cast<size_t>(x_count) * z_count);
                indices.resize(static_cast<size_t>(x_count - 1) * (z_count - 1) * 6);

                for (uint32_t z = 0; z < z_count; z++)
                {
                    const size_t source_row = static_cast<size_t>(z_start + z) * grid_width + x_start;
                    const size_t local_row  = static_cast<size_t>(z) * x_count;
                    for (uint32_t x = 0; x < x_count; x++)
                    {
                        RHI_Vertex_PosTexNorTan& vertex = vertices[local_row + x];
                        vertex                          = grid_vertices[source_row + x];
                        vertex.pos[0]                  -= center_x;
                        vertex.pos[2]                  -= center_z;
                    }
                }

                // same winding as the full grid so normals and culling agree with the ungrouped surface
                size_t k = 0;
                for (uint32_t z = 0; z + 1 < z_count; z++)
                {
                    for (uint32_t x = 0; x + 1 < x_count; x++)
                    {
                        const uint32_t bl = z * x_count + x;
                        const uint32_t br = bl + 1;
                        const uint32_t tl = bl + x_count;
                        const uint32_t tr = tl + 1;

                        indices[k++] = br;
                        indices[k++] = bl;
                        indices[k++] = tl;
                        indices[k++] = br;
                        indices[k++] = tl;
                        indices[k++] = tr;
                    }
                }
            }
        };

        ThreadPool::ParallelLoop(build_tile, total_tiles);
    }
}
