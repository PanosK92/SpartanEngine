/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

//= INCLUDES ===========================
#include "pch.h"
#include "GeometryProcessing.h"
#include "../core/ThreadPool.h"
#include <unordered_map>
#include <algorithm>
#include <cfloat>
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
        const bool preserve_edges,
        const bool prune_components,
        const float max_error
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
        bool target_reached    = false;

        for (size_t i = 0; i < threshold_count && !target_reached; ++i)
        {
            const float error_bound = std::min(error_thresholds[i], max_error);
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
                error_bound,
                prune_components && !preserve_edges ? meshopt_SimplifyPrune : 0,
                &lod_error
            );

            // Pruning may cross several disconnected components at once. Refine
            // the error bound instead of accepting a nearly empty distant canopy.
            if (prune_components && result_count < target_index_count * 4 / 5)
            {
                auto best_indices = indices_simplified;
                size_t closest_count = result_count;
                size_t closest_delta = target_index_count - result_count;
                float lower_error = i > 0 ? std::min(error_thresholds[i - 1], error_bound) : 0.0f;
                float upper_error = error_bound;
                for (uint32_t refinement = 0; refinement < 10; refinement++)
                {
                    const float error = (lower_error + upper_error) * 0.5f;
                    const size_t count = meshopt_simplifyWithAttributes(
                        indices_simplified.data(), indices.data(), index_count,
                        &vertices[0].pos[0], vertex_count, sizeof(RHI_Vertex_PosTexNorTan),
                        vertex_attributes, attr_stride, attr_weights, attr_count, locks,
                        target_index_count, error, preserve_edges ? 0 : meshopt_SimplifyPrune, &lod_error);
                    const size_t delta = count > target_index_count ? count - target_index_count : target_index_count - count;
                    if (count > 0 && delta < closest_delta)
                    {
                        closest_delta = delta;
                        closest_count = count;
                        best_indices = indices_simplified;
                    }
                    if (count < target_index_count) upper_error = error;
                    else lower_error = error;
                }
                indices_simplified = std::move(best_indices);
                result_count = closest_count;
                // Close above-target results are preferable to stripping extra detail.
                target_reached = result_count <= target_index_count * 6 / 5;
            }

            // check if we reached the target or made meaningful progress
            if (result_count <= target_index_count)
            {
                target_reached = true;
            }
            // A plateau at one error bound does not prove a topology limit.
            // Later bounds can permit collapses that the earlier bound rejected.

            if (error_bound >= max_error)
            {
                break;
            }
        }

        // Sloppy reduction ignores UV seams and can join separate leaf cards into
        // long triangles. Never use it when the caller requested UV preservation.
        // It has no error bound either, so a capped caller never gets it.
        if (result_count > target_index_count && !preserve_uvs && !preserve_edges && target_index_count >= 12 && max_error >= 1.0f)
        {
            std::vector<uint32_t> sloppy_indices(index_count);
            size_t sloppy_count = meshopt_simplifySloppy(
                sloppy_indices.data(),
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
                indices_simplified.swap(sloppy_indices);
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

    static void pack_culling_bounds(Sb_MeshletBounds& bounds, const meshopt_Bounds& meshlet_bounds,
        const math::Vector3& aabb_min, const math::Vector3& aabb_extent_safe, float aabb_diag_safe)
    {
        const float center_quant_err_3d = 0.5f * aabb_diag_safe / 65535.0f;
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
    }

    static math::Vector3 quantization_extent(const math::BoundingBox& bounds)
    {
        const math::Vector3 extent = bounds.GetMax() - bounds.GetMin();
        return math::Vector3(std::max(extent.x, 1e-8f), std::max(extent.y, 1e-8f), std::max(extent.z, 1e-8f));
    }

    void refit_meshlet_bounds(const std::vector<RHI_Vertex_PosTexNorTan>& vertices, const std::vector<uint32_t>& indices,
        std::vector<Sb_MeshletBounds>& meshlets, const math::BoundingBox& bounds)
    {
        const math::Vector3 extent = quantization_extent(bounds);
        const float diagonal = extent.Length();
        for (auto& meshlet : meshlets)
        {
            const uint32_t first = meshlet.first_index_tri_count & MESHLET_FIRST_INDEX_MASK;
            const uint32_t count = 3 * (meshlet.first_index_tri_count >> MESHLET_TRI_COUNT_SHIFT);
            const auto culling = meshopt_computeClusterBounds(indices.data() + first, count, vertices.front().pos,
                vertices.size(), sizeof(vertices.front()));
            pack_culling_bounds(meshlet, culling, bounds.GetMin(), extent, diagonal);
        }
    }

    void build_meshlets(
        const std::vector<RHI_Vertex_PosTexNorTan>& vertices,
        std::vector<uint32_t>& indices,
        std::vector<Sb_MeshletBounds>& meshlets_out,
        std::vector<uint32_t>& unique_vertices_out,
        std::vector<uint32_t>& micro_indices_out,
        math::BoundingBox& geometry_bounds_out,
        bool scan, const math::BoundingBox* quantization_bounds
    )
    {
        meshlets_out.clear();
        unique_vertices_out.clear();
        micro_indices_out.clear();
        geometry_bounds_out = math::BoundingBox::Zero;

        const size_t index_count  = indices.size();
        const size_t vertex_count = vertices.size();
        if (index_count == 0 || vertex_count == 0)
        {
            return;
        }

        // compute the lod-local aabb up-front, the compressed meshlet bounds quantize center/radius against it and the cull shader receives the same aabb via DrawData.geometry_bounds_*
        geometry_bounds_out                       = quantization_bounds ? *quantization_bounds : math::BoundingBox(vertices.data(), static_cast<uint32_t>(vertices.size()));
        const math::Vector3 extent = quantization_extent(geometry_bounds_out);
        const float diagonal = extent.Length();

        // worst case meshlet count, used for buffer allocation
        const size_t max_meshlets = meshopt_buildMeshletsBound(index_count, meshlet_max_vertices, meshlet_max_triangles);

        std::vector<meshopt_Meshlet> meshlets(max_meshlets);
        std::vector<unsigned int> meshlet_vertices(max_meshlets * meshlet_max_vertices);
        std::vector<unsigned char> meshlet_triangles(max_meshlets * meshlet_max_triangles * 3);

        // cone_weight 0.5 biases meshlet builds toward triangles with similar normals so cone backface culling is effective
        const size_t meshlet_count = scan ? meshopt_buildMeshletsScan(
            meshlets.data(), meshlet_vertices.data(), meshlet_triangles.data(),
            indices.data(), index_count, vertex_count, meshlet_max_vertices, meshlet_max_triangles
        ) : meshopt_buildMeshlets(
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

            pack_culling_bounds(bounds, meshlet_bounds, geometry_bounds_out.GetMin(), extent, diagonal);

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

    math::Vector3 impostor_frame_direction(uint32_t x, uint32_t y, uint32_t frames)
    {
        // the frame grid is the square rotated 45 degrees onto the upper octahedron, every centre lands
        // above the horizon and the corners of the grid meet it
        const float ox = (static_cast<float>(x) + 0.5f) / static_cast<float>(frames) * 2.0f - 1.0f;
        const float oy = (static_cast<float>(y) + 0.5f) / static_cast<float>(frames) * 2.0f - 1.0f;
        const float px = (ox + oy) * 0.5f;
        const float pz = (ox - oy) * 0.5f;
        return math::Vector3(px, 1.0f - fabsf(px) - fabsf(pz), pz).Normalized();
    }

    void impostor_frame_basis(const math::Vector3& direction, math::Vector3& right, math::Vector3& up)
    {
        const math::Vector3 reference = direction.y > 0.999f ? math::Vector3(0.0f, 0.0f, 1.0f) : math::Vector3(0.0f, 1.0f, 0.0f);
        right = math::Vector3::Cross(reference, direction).Normalized();
        up    = math::Vector3::Cross(direction, right);
    }

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
    )
    {
        const size_t frame_texels = static_cast<size_t>(resolution) * resolution;
        const size_t frame_words  = frame_texels * layers * 2;
        texels_out.assign(frame_words * frames * frames, 0u);
        if (vertices.empty() || indices.size() < 3 || radius <= 0.0f)
        {
            return;
        }

        std::vector<math::Vector3> positions(vertices.size());
        std::vector<math::Vector3> normals(vertices.size());
        std::vector<math::Vector2> uvs(vertices.size());
        for (size_t i = 0; i < vertices.size(); i++)
        {
            positions[i] = vertices[i].get_position() - center;
            normals[i]   = vertices[i].get_normal();
            uvs[i]       = vertices[i].get_uv();
        }

        const float inv_radius  = 1.0f / radius;
        const float resolution_f = static_cast<float>(resolution);
        const math::Vector2 inv_uv_scale(1.0f / std::max(uv_scale.x, 1e-6f), 1.0f / std::max(uv_scale.y, 1e-6f));

        struct Projected
        {
            float x;
            float y;
            float z;
        };
        std::vector<Projected> projected(vertices.size());
        std::vector<float> depth(frame_texels * layers);

        for (uint32_t fy = 0; fy < frames; fy++)
        {
            for (uint32_t fx = 0; fx < frames; fx++)
            {
                const math::Vector3 direction = impostor_frame_direction(fx, fy, frames);
                math::Vector3 right;
                math::Vector3 up;
                impostor_frame_basis(direction, right, up);

                // +z points at the viewer, so the nearest surface is the largest z
                for (size_t i = 0; i < positions.size(); i++)
                {
                    const math::Vector3& p = positions[i];
                    projected[i].x = (math::Vector3::Dot(p, right) * inv_radius * 0.5f + 0.5f) * resolution_f;
                    projected[i].y = (math::Vector3::Dot(p, up) * inv_radius * 0.5f + 0.5f) * resolution_f;
                    projected[i].z = math::Vector3::Dot(p, direction);
                }
                std::fill(depth.begin(), depth.end(), -FLT_MAX);
                uint32_t* frame = texels_out.data() + (static_cast<size_t>(fy) * frames + fx) * frame_words;

                auto write = [&](uint32_t px, uint32_t py, float z, const math::Vector2& uv, const math::Vector3& normal)
                {
                    const size_t texel = static_cast<size_t>(py) * resolution + px;
                    float* texel_depth = depth.data() + texel * layers;
                    uint32_t slot      = layers;
                    for (uint32_t l = 0; l < layers; l++)
                    {
                        if (z > texel_depth[l])
                        {
                            slot = l;
                            break;
                        }
                    }
                    if (slot == layers)
                    {
                        return;
                    }

                    uint32_t* words = frame + texel * layers * 2;
                    for (uint32_t l = layers - 1; l > slot; l--)
                    {
                        texel_depth[l]      = texel_depth[l - 1];
                        words[l * 2]        = words[(l - 1) * 2];
                        words[l * 2 + 1]    = words[(l - 1) * 2 + 1];
                    }

                    // a two sided card seen from behind lights with its front normal flipped toward the viewer
                    float nx = math::Vector3::Dot(normal, right);
                    float ny = math::Vector3::Dot(normal, up);
                    float nz = math::Vector3::Dot(normal, direction);
                    if (nz < 0.0f)
                    {
                        nx = -nx;
                        ny = -ny;
                        nz = -nz;
                    }
                    const float n_sum = std::max(fabsf(nx) + fabsf(ny) + nz, 1e-6f);
                    const uint32_t ox = static_cast<uint32_t>(std::clamp((nx / n_sum) * 0.5f + 0.5f, 0.0f, 1.0f) * 255.0f + 0.5f);
                    const uint32_t oy = static_cast<uint32_t>(std::clamp((ny / n_sum) * 0.5f + 0.5f, 0.0f, 1.0f) * 255.0f + 0.5f);
                    const uint32_t dz = static_cast<uint32_t>(std::clamp(z * inv_radius * 0.5f + 0.5f, 0.0f, 1.0f) * 32767.0f + 0.5f);
                    const uint32_t u  = static_cast<uint32_t>(std::clamp((uv.x - uv_min.x) * inv_uv_scale.x, 0.0f, 1.0f) * 65535.0f + 0.5f);
                    const uint32_t v  = static_cast<uint32_t>(std::clamp((uv.y - uv_min.y) * inv_uv_scale.y, 0.0f, 1.0f) * 65535.0f + 0.5f);

                    texel_depth[slot]  = z;
                    words[slot * 2]     = u | (v << 16);
                    words[slot * 2 + 1] = ox | (oy << 8) | (dz << 16) | (1u << 31);
                };

                for (size_t t = 0; t + 2 < indices.size(); t += 3)
                {
                    const uint32_t i0 = indices[t];
                    const uint32_t i1 = indices[t + 1];
                    const uint32_t i2 = indices[t + 2];
                    const Projected& a = projected[i0];
                    const Projected& b = projected[i1];
                    const Projected& c = projected[i2];

                    const float area = (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
                    const int32_t x0 = std::max(static_cast<int32_t>(floorf(std::min({a.x, b.x, c.x}))), 0);
                    const int32_t y0 = std::max(static_cast<int32_t>(floorf(std::min({a.y, b.y, c.y}))), 0);
                    const int32_t x1 = std::min(static_cast<int32_t>(floorf(std::max({a.x, b.x, c.x}))), static_cast<int32_t>(resolution) - 1);
                    const int32_t y1 = std::min(static_cast<int32_t>(floorf(std::max({a.y, b.y, c.y}))), static_cast<int32_t>(resolution) - 1);
                    if (x0 > x1 || y0 > y1)
                    {
                        continue;
                    }

                    bool covered = false;
                    if (fabsf(area) > 1e-12f)
                    {
                        const float inv_area = 1.0f / area;
                        for (int32_t py = y0; py <= y1; py++)
                        {
                            for (int32_t px = x0; px <= x1; px++)
                            {
                                const float sx = static_cast<float>(px) + 0.5f;
                                const float sy = static_cast<float>(py) + 0.5f;
                                const float w0 = ((b.x - sx) * (c.y - sy) - (b.y - sy) * (c.x - sx)) * inv_area;
                                const float w1 = ((c.x - sx) * (a.y - sy) - (c.y - sy) * (a.x - sx)) * inv_area;
                                const float w2 = 1.0f - w0 - w1;
                                if (w0 < 0.0f || w1 < 0.0f || w2 < 0.0f)
                                {
                                    continue;
                                }

                                covered = true;
                                write(
                                    static_cast<uint32_t>(px),
                                    static_cast<uint32_t>(py),
                                    a.z * w0 + b.z * w1 + c.z * w2,
                                    uvs[i0] * w0 + uvs[i1] * w1 + uvs[i2] * w2,
                                    (normals[i0] * w0 + normals[i1] * w1 + normals[i2] * w2).Normalized()
                                );
                            }
                        }
                    }

                    // a needle thinner than a texel misses every texel centre, it still has to leave its mark
                    // or the distant crown bakes out as bare branches
                    if (!covered)
                    {
                        const float third = 1.0f / 3.0f;
                        const uint32_t px = static_cast<uint32_t>(std::clamp((a.x + b.x + c.x) * third, 0.0f, resolution_f - 1.0f));
                        const uint32_t py = static_cast<uint32_t>(std::clamp((a.y + b.y + c.y) * third, 0.0f, resolution_f - 1.0f));
                        write(
                            px,
                            py,
                            (a.z + b.z + c.z) * third,
                            (uvs[i0] + uvs[i1] + uvs[i2]) * third,
                            (normals[i0] + normals[i1] + normals[i2]).Normalized()
                        );
                    }
                }
            }
        }
    }

    void bake_tree_impostor_rgba(
        const std::vector<RHI_Vertex_PosTexNorTan>& bark_vertices,
        const std::vector<uint32_t>& bark_indices,
        const ImpostorTextureInput& bark_texture,
        const std::vector<RHI_Vertex_PosTexNorTan>& canopy_vertices,
        const std::vector<uint32_t>& canopy_indices,
        const ImpostorTextureInput& canopy_texture,
        uint32_t frames,
        uint32_t resolution,
        const math::Vector3& center,
        float radius,
        std::vector<uint32_t>& texels_out
    )
    {
        const size_t frame_texels = static_cast<size_t>(resolution) * resolution;
        const size_t frame_words  = frame_texels * 2;
        texels_out.assign(frame_words * frames * frames, 0u);
        if (canopy_vertices.empty() || canopy_indices.size() < 3 || radius <= 0.0f)
        {
            return;
        }

        // build a coarse mip matching the impostor texel footprint with alpha-weighted rgb so thin leaves
        // retain their silhouette without dark transparent fringes bleeding in
        struct MipImage
        {
            std::vector<uint8_t> rgba;
            uint32_t width  = 0;
            uint32_t height = 0;

            void build_raw(const uint8_t* src, uint32_t src_w, uint32_t src_h, uint32_t target_res, bool alpha_weighted)
            {
                if (!src || src_w == 0 || src_h == 0)
                {
                    return;
                }

                width  = src_w;
                height = src_h;
                rgba.assign(src, src + static_cast<size_t>(width) * height * 4);

                const uint32_t target = std::max(target_res * 2u, 64u);
                while (std::max(width, height) > target && width > 1 && height > 1)
                {
                    const uint32_t nw = std::max(width / 2u, 1u);
                    const uint32_t nh = std::max(height / 2u, 1u);
                    std::vector<uint8_t> next(static_cast<size_t>(nw) * nh * 4);
                    for (uint32_t y = 0; y < nh; y++)
                    {
                        for (uint32_t x = 0; x < nw; x++)
                        {
                            float r = 0.0f, g = 0.0f, b = 0.0f, a = 0.0f, w_sum = 0.0f;
                            for (uint32_t dy = 0; dy < 2; dy++)
                            {
                                const uint32_t sy = std::min(y * 2 + dy, height - 1);
                                for (uint32_t dx = 0; dx < 2; dx++)
                                {
                                    const uint32_t sx = std::min(x * 2 + dx, width - 1);
                                    const size_t idx  = (static_cast<size_t>(sy) * width + sx) * 4;
                                    const float sa    = rgba[idx + 3] * (1.0f / 255.0f);
                                    const float w     = alpha_weighted ? sa : 1.0f;
                                    r     += rgba[idx + 0] * w;
                                    g     += rgba[idx + 1] * w;
                                    b     += rgba[idx + 2] * w;
                                    a     += sa;
                                    w_sum += w;
                                }
                            }
                            const size_t dst = (static_cast<size_t>(y) * nw + x) * 4;
                            const float inv_w = w_sum > 1e-4f ? (1.0f / w_sum) : 0.0f;
                            next[dst + 0] = static_cast<uint8_t>(std::clamp(r * inv_w + 0.5f, 0.0f, 255.0f));
                            next[dst + 1] = static_cast<uint8_t>(std::clamp(g * inv_w + 0.5f, 0.0f, 255.0f));
                            next[dst + 2] = static_cast<uint8_t>(std::clamp(b * inv_w + 0.5f, 0.0f, 255.0f));
                            next[dst + 3] = static_cast<uint8_t>(std::clamp((a * 0.25f) * 255.0f + 0.5f, 0.0f, 255.0f));
                        }
                    }
                    rgba   = std::move(next);
                    width  = nw;
                    height = nh;
                }
            }

            void sample(const math::Vector2& uv, const math::Vector3& tint, uint8_t& r_out, uint8_t& g_out, uint8_t& b_out, float& a_out) const
            {
                if (rgba.empty() || width == 0 || height == 0)
                {
                    r_out = static_cast<uint8_t>(std::clamp(tint.x * 255.0f, 0.0f, 255.0f));
                    g_out = static_cast<uint8_t>(std::clamp(tint.y * 255.0f, 0.0f, 255.0f));
                    b_out = static_cast<uint8_t>(std::clamp(tint.z * 255.0f, 0.0f, 255.0f));
                    a_out = 1.0f;
                    return;
                }

                float u = uv.x - floorf(uv.x);
                float v = uv.y - floorf(uv.y);
                const float fx = u * static_cast<float>(width) - 0.5f;
                const float fy = v * static_cast<float>(height) - 0.5f;
                const int32_t x0 = static_cast<int32_t>(floorf(fx));
                const int32_t y0 = static_cast<int32_t>(floorf(fy));
                const float tx = fx - static_cast<float>(x0);
                const float ty = fy - static_cast<float>(y0);
                const uint32_t ux0 = static_cast<uint32_t>((x0 % static_cast<int32_t>(width) + static_cast<int32_t>(width)) % static_cast<int32_t>(width));
                const uint32_t uy0 = static_cast<uint32_t>((y0 % static_cast<int32_t>(height) + static_cast<int32_t>(height)) % static_cast<int32_t>(height));
                const uint32_t ux1 = (ux0 + 1u) % width;
                const uint32_t uy1 = (uy0 + 1u) % height;

                const uint8_t* p00 = &rgba[(static_cast<size_t>(uy0) * width + ux0) * 4];
                const uint8_t* p10 = &rgba[(static_cast<size_t>(uy0) * width + ux1) * 4];
                const uint8_t* p01 = &rgba[(static_cast<size_t>(uy1) * width + ux0) * 4];
                const uint8_t* p11 = &rgba[(static_cast<size_t>(uy1) * width + ux1) * 4];

                const float w00 = (1.0f - tx) * (1.0f - ty);
                const float w10 = tx * (1.0f - ty);
                const float w01 = (1.0f - tx) * ty;
                const float w11 = tx * ty;

                const float r = (p00[0] * w00 + p10[0] * w10 + p01[0] * w01 + p11[0] * w11) * tint.x;
                const float g = (p00[1] * w00 + p10[1] * w10 + p01[1] * w01 + p11[1] * w11) * tint.y;
                const float b = (p00[2] * w00 + p10[2] * w10 + p01[2] * w01 + p11[2] * w11) * tint.z;
                a_out = (p00[3] * w00 + p10[3] * w10 + p01[3] * w01 + p11[3] * w11) * (1.0f / 255.0f);

                r_out = static_cast<uint8_t>(std::clamp(r + 0.5f, 0.0f, 255.0f));
                g_out = static_cast<uint8_t>(std::clamp(g + 0.5f, 0.0f, 255.0f));
                b_out = static_cast<uint8_t>(std::clamp(b + 0.5f, 0.0f, 255.0f));
            }

            math::Vector3 sample_normal_ts(const math::Vector2& uv) const
            {
                if (rgba.empty() || width == 0 || height == 0)
                {
                    return math::Vector3(0.0f, 0.0f, 1.0f);
                }

                uint8_t r = 128, g = 128, b = 255;
                float a = 1.0f;
                sample(uv, math::Vector3(1.0f, 1.0f, 1.0f), r, g, b, a);
                const float nx = (static_cast<float>(r) * (1.0f / 255.0f)) * 2.0f - 1.0f;
                const float ny = (static_cast<float>(g) * (1.0f / 255.0f)) * 2.0f - 1.0f;
                const float nz = std::max(0.05f, (static_cast<float>(b) * (1.0f / 255.0f)) * 2.0f - 1.0f);
                return math::Vector3(nx, ny, nz).Normalized();
            }

            float sample_scalar(const math::Vector2& uv, float fallback) const
            {
                if (rgba.empty() || width == 0 || height == 0)
                {
                    return fallback;
                }

                uint8_t r = 255, g = 255, b = 255;
                float a = 1.0f;
                sample(uv, math::Vector3(1.0f, 1.0f, 1.0f), r, g, b, a);
                return static_cast<float>(r) * (1.0f / 255.0f);
            }
        };

        MipImage bark_mip;
        bark_mip.build_raw(bark_texture.rgba, bark_texture.width, bark_texture.height, resolution, false);
        MipImage bark_occ_mip;
        bark_occ_mip.build_raw(bark_texture.occlusion_rgba, bark_texture.occlusion_width, bark_texture.occlusion_height, resolution, false);

        MipImage canopy_mip;
        canopy_mip.build_raw(canopy_texture.rgba, canopy_texture.width, canopy_texture.height, resolution, true);
        MipImage canopy_normal_mip;
        canopy_normal_mip.build_raw(canopy_texture.normal_rgba, canopy_texture.normal_width, canopy_texture.normal_height, resolution, false);
        MipImage canopy_occ_mip;
        canopy_occ_mip.build_raw(canopy_texture.occlusion_rgba, canopy_texture.occlusion_width, canopy_texture.occlusion_height, resolution, false);

        struct MeshCache
        {
            std::vector<math::Vector3> positions;
            std::vector<math::Vector3> normals;
            std::vector<math::Vector3> tangents;
            std::vector<math::Vector2> uvs;

            void fill(const std::vector<RHI_Vertex_PosTexNorTan>& verts, const math::Vector3& center)
            {
                positions.resize(verts.size());
                normals.resize(verts.size());
                tangents.resize(verts.size());
                uvs.resize(verts.size());
                for (size_t i = 0; i < verts.size(); i++)
                {
                    positions[i] = verts[i].get_position() - center;
                    normals[i]   = verts[i].get_normal();
                    tangents[i]  = verts[i].get_tangent();
                    uvs[i]       = verts[i].get_uv();
                }
            }
        };

        MeshCache bark_cache;
        bark_cache.fill(bark_vertices, center);
        MeshCache canopy_cache;
        canopy_cache.fill(canopy_vertices, center);

        // canopy ellipsoid bounds in center-relative local space for volumetric radial & vertical cavity occlusion
        math::Vector3 crown_min(FLT_MAX, FLT_MAX, FLT_MAX);
        math::Vector3 crown_max(-FLT_MAX, -FLT_MAX, -FLT_MAX);
        for (const math::Vector3& p : canopy_cache.positions)
        {
            crown_min = math::Vector3::Min(crown_min, p);
            crown_max = math::Vector3::Max(crown_max, p);
        }
        const math::Vector3 crown_center = (crown_min + crown_max) * 0.5f;
        const math::Vector3 crown_radii  = math::Vector3::Max((crown_max - crown_min) * 0.5f, math::Vector3(0.35f, 0.35f, 0.35f));

        const float inv_radius       = 1.0f / radius;
        const float resolution_f     = static_cast<float>(resolution);
        const float texel_world_size = (2.0f * radius) / resolution_f;
        const float leaf_cutoff      = std::clamp(canopy_texture.alpha_cutoff * 0.72f, 0.1f, 0.9f);

        struct Projected
        {
            float x;
            float y;
            float z;
        };

        static const float horizon_dirs[8][2] =
        {
            {  1.0f,  0.0f }, { -1.0f,  0.0f }, {  0.0f,  1.0f }, {  0.0f, -1.0f },
            {  0.70710678f,  0.70710678f }, { -0.70710678f,  0.70710678f },
            {  0.70710678f, -0.70710678f }, { -0.70710678f, -0.70710678f }
        };
        static const float horizon_radii[4] = { 1.25f, 2.75f, 5.0f, 8.5f };

        auto smoothstep_f = [](float edge0, float edge1, float x) -> float
        {
            const float t = std::clamp((x - edge0) / std::max(edge1 - edge0, 1e-6f), 0.0f, 1.0f);
            return t * t * (3.0f - 2.0f * t);
        };

        auto bake_frame = [&](uint32_t frame_start, uint32_t frame_end)
        {
            std::vector<Projected> projected;
            std::vector<float> depth(frame_texels);
            std::vector<uint8_t> rgb(frame_texels * 3);
            std::vector<float> normal_frame(frame_texels * 3);
            std::vector<float> tex_ao(frame_texels);
            std::vector<uint8_t> flags(frame_texels); // bit 0: valid, bit 1: is_bark

            for (uint32_t f = frame_start; f < frame_end; f++)
            {
                const uint32_t fx = f % frames;
                const uint32_t fy = f / frames;

                const math::Vector3 direction = impostor_frame_direction(fx, fy, frames);
                math::Vector3 right;
                math::Vector3 up;
                impostor_frame_basis(direction, right, up);

                std::fill(depth.begin(), depth.end(), -FLT_MAX);
                std::fill(flags.begin(), flags.end(), 0u);
                std::fill(tex_ao.begin(), tex_ao.end(), 1.0f);
                uint32_t* frame = texels_out.data() + static_cast<size_t>(f) * frame_words;

                auto rasterize_submesh = [&](const MeshCache& cache, const std::vector<uint32_t>& indices, const MipImage& albedo_mip, const MipImage& normal_mip, const MipImage& occ_mip, const math::Vector3& tint, bool is_bark, float alpha_threshold)
                {
                    if (cache.positions.empty() || indices.size() < 3)
                    {
                        return;
                    }

                    projected.resize(cache.positions.size());
                    for (size_t i = 0; i < cache.positions.size(); i++)
                    {
                        const math::Vector3& p = cache.positions[i];
                        projected[i].x = (math::Vector3::Dot(p, right) * inv_radius * 0.5f + 0.5f) * resolution_f;
                        projected[i].y = (math::Vector3::Dot(p, up) * inv_radius * 0.5f + 0.5f) * resolution_f;
                        projected[i].z = math::Vector3::Dot(p, direction);
                    }

                    const bool has_normal_map = !normal_mip.rgba.empty();

                    auto write = [&](uint32_t px, uint32_t py, float z, const math::Vector2& uv, const math::Vector3& interp_normal, const math::Vector3& interp_tangent)
                    {
                        const size_t texel = static_cast<size_t>(py) * resolution + px;
                        if (z <= depth[texel])
                        {
                            return;
                        }

                        uint8_t r = 0, g = 0, b = 0;
                        float a = 1.0f;
                        albedo_mip.sample(uv, tint, r, g, b, a);
                        if (!is_bark && a <= alpha_threshold)
                        {
                            return;
                        }

                        math::Vector3 shaded_n = interp_normal;
                        if (!is_bark && has_normal_map)
                        {
                            const math::Vector3 n_ts = normal_mip.sample_normal_ts(uv);
                            math::Vector3 t_ortho = interp_tangent - shaded_n * math::Vector3::Dot(interp_tangent, shaded_n);
                            if (t_ortho.LengthSquared() > 1e-6f)
                            {
                                t_ortho.Normalize();
                                const math::Vector3 b_ortho = math::Vector3::Cross(shaded_n, t_ortho);
                                // perturb vertex normal by high-frequency leaf sprite normal
                                const float strength = 0.85f;
                                shaded_n = (shaded_n * n_ts.z + t_ortho * (n_ts.x * strength) + b_ortho * (n_ts.y * strength)).Normalized();
                            }
                        }

                        float nx = math::Vector3::Dot(shaded_n, right);
                        float ny = math::Vector3::Dot(shaded_n, up);
                        float nz = math::Vector3::Dot(shaded_n, direction);
                        if (nz < 0.0f)
                        {
                            nx = -nx;
                            ny = -ny;
                            nz = -nz;
                        }

                        depth[texel]                = z;
                        rgb[texel * 3 + 0]          = r;
                        rgb[texel * 3 + 1]          = g;
                        rgb[texel * 3 + 2]          = b;
                        normal_frame[texel * 3 + 0] = nx;
                        normal_frame[texel * 3 + 1] = ny;
                        normal_frame[texel * 3 + 2] = nz;
                        tex_ao[texel]               = occ_mip.sample_scalar(uv, 1.0f);
                        flags[texel]                = static_cast<uint8_t>(1u | (is_bark ? 2u : 0u));
                    };

                    for (size_t t = 0; t + 2 < indices.size(); t += 3)
                    {
                        const uint32_t i0 = indices[t];
                        const uint32_t i1 = indices[t + 1];
                        const uint32_t i2 = indices[t + 2];
                        const Projected& a = projected[i0];
                        const Projected& b = projected[i1];
                        const Projected& c = projected[i2];

                        const float area = (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
                        const int32_t x0 = std::max(static_cast<int32_t>(floorf(std::min({a.x, b.x, c.x}))), 0);
                        const int32_t y0 = std::max(static_cast<int32_t>(floorf(std::min({a.y, b.y, c.y}))), 0);
                        const int32_t x1 = std::min(static_cast<int32_t>(floorf(std::max({a.x, b.x, c.x}))), static_cast<int32_t>(resolution) - 1);
                        const int32_t y1 = std::min(static_cast<int32_t>(floorf(std::max({a.y, b.y, c.y}))), static_cast<int32_t>(resolution) - 1);
                        if (x0 > x1 || y0 > y1)
                        {
                            continue;
                        }

                        bool covered = false;
                        if (fabsf(area) > 1e-12f)
                        {
                            const float inv_area = 1.0f / area;
                            for (int32_t py = y0; py <= y1; py++)
                            {
                                for (int32_t px = x0; px <= x1; px++)
                                {
                                    const float sx = static_cast<float>(px) + 0.5f;
                                    const float sy = static_cast<float>(py) + 0.5f;
                                    const float w0 = ((b.x - sx) * (c.y - sy) - (b.y - sy) * (c.x - sx)) * inv_area;
                                    const float w1 = ((c.x - sx) * (a.y - sy) - (c.y - sy) * (a.x - sx)) * inv_area;
                                    const float w2 = 1.0f - w0 - w1;
                                    if (w0 < 0.0f || w1 < 0.0f || w2 < 0.0f)
                                    {
                                        continue;
                                    }

                                    covered = true;
                                    write(
                                        static_cast<uint32_t>(px),
                                        static_cast<uint32_t>(py),
                                        a.z * w0 + b.z * w1 + c.z * w2,
                                        cache.uvs[i0] * w0 + cache.uvs[i1] * w1 + cache.uvs[i2] * w2,
                                        (cache.normals[i0] * w0 + cache.normals[i1] * w1 + cache.normals[i2] * w2).Normalized(),
                                        (cache.tangents[i0] * w0 + cache.tangents[i1] * w1 + cache.tangents[i2] * w2).Normalized()
                                    );
                                }
                            }
                        }

                        if (!covered)
                        {
                            const float third = 1.0f / 3.0f;
                            const uint32_t px = static_cast<uint32_t>(std::clamp((a.x + b.x + c.x) * third, 0.0f, resolution_f - 1.0f));
                            const uint32_t py = static_cast<uint32_t>(std::clamp((a.y + b.y + c.y) * third, 0.0f, resolution_f - 1.0f));
                            write(
                                px,
                                py,
                                (a.z + b.z + c.z) * third,
                                (cache.uvs[i0] + cache.uvs[i1] + cache.uvs[i2]) * third,
                                (cache.normals[i0] + cache.normals[i1] + cache.normals[i2]).Normalized(),
                                (cache.tangents[i0] + cache.tangents[i1] + cache.tangents[i2]).Normalized()
                            );
                        }
                    }
                };

                MipImage empty_mip;
                rasterize_submesh(bark_cache, bark_indices, bark_mip, empty_mip, bark_occ_mip, bark_texture.tint, true, 0.0f);
                rasterize_submesh(canopy_cache, canopy_indices, canopy_mip, canopy_normal_mip, canopy_occ_mip, canopy_texture.tint, false, leaf_cutoff);

                // post-process: bake volumetric crown cavity AO (radial + vertical + 8-dir depth-horizon) into word 0 alpha,
                // and blend high-frequency depth-gradient cluster relief into canopy frame-space normals
                const float max_horizon_dz = radius * 0.55f;
                for (uint32_t py = 0; py < resolution; py++)
                {
                    const float fy_norm = ((static_cast<float>(py) + 0.5f) / resolution_f) * 2.0f - 1.0f;
                    for (uint32_t px = 0; px < resolution; px++)
                    {
                        const size_t texel = static_cast<size_t>(py) * resolution + px;
                        if ((flags[texel] & 1u) == 0u)
                        {
                            continue;
                        }

                        const bool is_bark  = (flags[texel] & 2u) != 0u;
                        const float z0      = depth[texel];
                        const float fx_norm = ((static_cast<float>(px) + 0.5f) / resolution_f) * 2.0f - 1.0f;
                        const math::Vector3 local_p = right * (fx_norm * radius) + up * (fy_norm * radius) + direction * z0;

                        // radial & vertical volumetric crown occlusion
                        const math::Vector3 rel = local_p - crown_center;
                        const float radial_dist = sqrtf(
                            (rel.x * rel.x) / (crown_radii.x * crown_radii.x) +
                            (rel.y * rel.y) / (crown_radii.y * crown_radii.y) +
                            (rel.z * rel.z) / (crown_radii.z * crown_radii.z)
                        );
                        const float radial_ao   = 0.24f + 0.76f * smoothstep_f(0.28f, 0.94f, radial_dist);
                        const float vert_t      = std::clamp((local_p.y - crown_min.y) / std::max(crown_max.y - crown_min.y, 0.5f), 0.0f, 1.0f);
                        const float vertical_ao = 0.42f + 0.58f * powf(vert_t, 0.65f);

                        // 8-direction local depth-horizon occlusion captures crisp crevices between leaf clusters
                        float horizon_occ_sum = 0.0f;
                        for (uint32_t d = 0; d < 8u; d++)
                        {
                            float max_sin = 0.0f;
                            for (uint32_t r_idx = 0; r_idx < 4u; r_idx++)
                            {
                                const float step_px = horizon_radii[r_idx];
                                const int32_t sx    = static_cast<int32_t>(lroundf(static_cast<float>(px) + horizon_dirs[d][0] * step_px));
                                const int32_t sy    = static_cast<int32_t>(lroundf(static_cast<float>(py) + horizon_dirs[d][1] * step_px));
                                if (sx < 0 || sy < 0 || sx >= static_cast<int32_t>(resolution) || sy >= static_cast<int32_t>(resolution))
                                {
                                    continue;
                                }
                                const size_t s_texel = static_cast<size_t>(sy) * resolution + static_cast<size_t>(sx);
                                if ((flags[s_texel] & 1u) == 0u)
                                {
                                    continue;
                                }
                                const float dz = depth[s_texel] - z0;
                                if (dz > 0.0f && dz < max_horizon_dz)
                                {
                                    const float dist_w = step_px * texel_world_size;
                                    const float s_ang  = dz / sqrtf(dz * dz + dist_w * dist_w);
                                    const float falloff = 1.0f - (dz / max_horizon_dz) * 0.45f;
                                    max_sin = std::max(max_sin, s_ang * falloff);
                                }
                            }
                            horizon_occ_sum += max_sin;
                        }
                        const float horizon_ao = 1.0f - (horizon_occ_sum * (1.0f / 8.0f)) * 0.82f;

                        float cavity_ao = is_bark
                            ? tex_ao[texel] * vertical_ao * (0.35f + 0.65f * horizon_ao)
                            : tex_ao[texel] * radial_ao * vertical_ao * (0.28f + 0.72f * horizon_ao);
                        cavity_ao = std::clamp(cavity_ao, 0.08f, 1.0f);

                        // blend local depth-buffer cluster slope into canopy normal for crisp sunlit/shaded bough relief
                        float nx = normal_frame[texel * 3 + 0];
                        float ny = normal_frame[texel * 3 + 1];
                        float nz = normal_frame[texel * 3 + 2];
                        if (!is_bark)
                        {
                            auto sample_z_clamped = [&](int32_t ox, int32_t oy) -> float
                            {
                                const int32_t cx = std::clamp(static_cast<int32_t>(px) + ox, 0, static_cast<int32_t>(resolution) - 1);
                                const int32_t cy = std::clamp(static_cast<int32_t>(py) + oy, 0, static_cast<int32_t>(resolution) - 1);
                                const size_t st  = static_cast<size_t>(cy) * resolution + static_cast<size_t>(cx);
                                if ((flags[st] & 1u) == 0u)
                                {
                                    return z0 - texel_world_size * 0.85f;
                                }
                                const float dz = depth[st] - z0;
                                return z0 + std::clamp(dz, -texel_world_size * 1.8f, texel_world_size * 1.8f);
                            };

                            const float dz_dx = (sample_z_clamped(1, 0) - sample_z_clamped(-1, 0)) / (2.0f * texel_world_size);
                            const float dz_dy = (sample_z_clamped(0, 1) - sample_z_clamped(0, -1)) / (2.0f * texel_world_size);
                            const math::Vector3 depth_n = math::Vector3(-dz_dx * 0.65f, -dz_dy * 0.65f, 1.0f).Normalized();
                            const math::Vector3 combined = (math::Vector3(nx, ny, nz) * 0.62f + depth_n * 0.38f).Normalized();
                            nx = combined.x;
                            ny = combined.y;
                            nz = std::max(combined.z, 0.05f);
                        }

                        const float n_sum    = std::max(fabsf(nx) + fabsf(ny) + nz, 1e-6f);
                        const uint32_t ox    = static_cast<uint32_t>(std::clamp((nx / n_sum) * 0.5f + 0.5f, 0.0f, 1.0f) * 255.0f + 0.5f);
                        const uint32_t oy    = static_cast<uint32_t>(std::clamp((ny / n_sum) * 0.5f + 0.5f, 0.0f, 1.0f) * 255.0f + 0.5f);
                        const uint32_t dz14  = static_cast<uint32_t>(std::clamp(z0 * inv_radius * 0.5f + 0.5f, 0.0f, 1.0f) * 16383.0f + 0.5f);
                        const uint32_t ao8   = static_cast<uint32_t>(std::clamp(cavity_ao * 255.0f + 0.5f, 0.0f, 255.0f));

                        frame[texel * 2]     = static_cast<uint32_t>(rgb[texel * 3 + 0]) | (static_cast<uint32_t>(rgb[texel * 3 + 1]) << 8) | (static_cast<uint32_t>(rgb[texel * 3 + 2]) << 16) | (ao8 << 24);
                        frame[texel * 2 + 1] = ox | (oy << 8) | (dz14 << 16) | (is_bark ? (1u << 30) : 0u) | (1u << 31);
                    }
                }
            }
        };

        ThreadPool::ParallelLoop(bake_frame, frames * frames);
    }
}
