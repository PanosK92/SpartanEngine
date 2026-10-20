/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

//= INCLUDES =========================
#include "pch.h"
#include "Terrain.h"
#include "TerrainCommon.h"
#include "Render.h"
#include "Physics.h"
#include "Camera.h"
#include "../Entity.h"
#include "../World.h"
#include "../TerrainSystem.h"
#include "../TerrainPlacement.h"
#include "../../geometry/Mesh.h"
#include "../../rendering/Material.h"
#include "../../profiling/Profiler.h"
//====================================

//= NAMESPACES ===============
using namespace std;
using namespace spartan::math;
//============================

// terrain detail pages
//
// the coarse grid is one vertex every scale / density metres, 12.5 m on the island, far too coarse
// to hold a level pad the size of a house or the edge of a road, so every coarse cell something
// reshapes carries a page, a (n + 1)^2 grid of heights about a metre apart
//
// a page starts as the coarse surface, triangulated exactly like the coarse mesh, then the pads are
// blended in and the roads grade it, so a cell nothing touches would come out identical to the
// coarse triangles it replaces. a page edge that borders a coarse cell is pinned to the coarse edge,
// which makes the two meshes meet without a step, the coarse vertices under the pages sink just far
// enough to stay hidden and the coarse collision gets a hole wherever a page is
//
// pages belong to cells and not to pads, two pads sharing a cell share one surface

namespace spartan
{
    using namespace terrain_common;

    namespace
    {
        // metres between page samples, rounded so a coarse cell holds whole subdivisions
        const float detail_sample_spacing = 1.0f;
        // coarse cells per side of one detail render chunk
        const uint32_t detail_chunk_cells = 8;
        // how far the coarse mesh stays under the pages
        const float detail_sink = 0.03f;
        // a page leans its normals into the coarse ones over this distance from a coarse border, the
        // coarse mesh carries normals smoothed over a whole cell and the seam would show in the lighting
        const float detail_normal_blend = 3.0f;
        // a skirt hangs off every coarse border, float rounding between the two meshes can not open a crack
        const float detail_skirt = 0.25f;
        // steepest ground a pad ramp makes, run per metre of rise
        const float detail_ramp_run = 3.0f;
        // a road cell keeps the coarse triangles unless the graded page misses them by more than this
        // within detail_road_band of the road edge, further out the slopes are allowed to stay coarse
        const float detail_road_tolerance = 0.3f;
        const float detail_road_band      = 2.0f;
        // chunks either side of the camera's chunk whose road cells may carry pages
        const int32_t detail_road_window = 3;
        // chunk blocks per atlas row, common.hlsl terrain_detail_blocks_per_row reads the same layout
        const uint32_t detail_atlas_blocks_per_row = 16;
        // the vulkan minimum for maxImageDimension2D on desktop hardware
        const uint32_t detail_atlas_max_dimension = 16384;

        string detail_chunk_name(uint32_t x, uint32_t z)
        {
            return "terrain_detail_" + to_string(x) + "_" + to_string(z);
        }

        uint64_t detail_chunk_key(uint32_t x, uint32_t z)
        {
            return (static_cast<uint64_t>(z) << 32) | x;
        }

        float smooth_step(float t)
        {
            t = clamp(t, 0.0f, 1.0f);
            return t * t * (3.0f - 2.0f * t);
        }

        struct PadLocal
        {
            float cx        = 0.0f;
            float cz        = 0.0f;
            float hx        = 0.0f;
            float hz        = 0.0f;
            float yaw       = 0.0f;
            float height    = 0.0f;
            float ramp      = 1.0f; // metres the ground takes to meet the pad
            float influence = 1.0f; // reach of the coarse paint, every cell within it carries a page
        };

        void write_tangent(RHI_Vertex_PosTexNorTan& vertex, const Vector3& n)
        {
            vertex.set_normal(n);

            float tx   = 1.0f - n.x * n.x;
            float ty   = -n.y * n.x;
            float tz   = -n.z * n.x;
            float t_sq = tx * tx + ty * ty + tz * tz;
            if (t_sq < 1e-8f)
            {
                tx   = n.y;
                ty   = -n.x;
                tz   = 0.0f;
                t_sq = tx * tx + ty * ty;
            }

            const float inv = 1.0f / sqrtf(max(t_sq, 1e-12f));
            vertex.set_tangent(Vector3(tx * inv, ty * inv, tz * inv));
        }
    }

    uint32_t Terrain::GetDetailSubdivisions() const
    {
        const TerrainGridMapping mapping = GetGridMapping();
        const float cell = max(mapping.scale_x, mapping.scale_z);
        return clamp(static_cast<uint32_t>(roundf(cell / detail_sample_spacing)), 1u, 32u);
    }

    bool Terrain::IsDetailCell(uint32_t cell_x, uint32_t cell_z) const
    {
        if (m_dense_width < 2 || cell_x + 1 >= m_dense_width || cell_z + 1 >= m_dense_height)
        {
            return false;
        }

        shared_lock lock(m_detail_mutex);
        return m_detail_pages.find(cell_z * (m_dense_width - 1) + cell_x) != m_detail_pages.end();
    }

    float Terrain::GetDetailDrop(uint32_t vertex_index) const
    {
        shared_lock lock(m_detail_mutex);
        const auto it = m_detail_drop.find(vertex_index);
        return it != m_detail_drop.end() ? it->second : 0.0f;
    }

    bool Terrain::SampleDetailLocal(float local_x, float local_z, float& height_out) const
    {
        if (m_dense_width < 2 || m_dense_height < 2)
        {
            return false;
        }

        shared_lock lock(m_detail_mutex);
        if (m_detail_pages.empty())
        {
            return false;
        }

        const TerrainGridMapping mapping = GetGridMapping();
        const float gx = (local_x + mapping.offset_x) / max(mapping.scale_x, 1e-4f);
        const float gz = (local_z + mapping.offset_z) / max(mapping.scale_z, 1e-4f);
        if (gx < 0.0f || gz < 0.0f || gx > static_cast<float>(m_dense_width - 1) || gz > static_cast<float>(m_dense_height - 1))
        {
            return false;
        }

        const uint32_t cx = min(static_cast<uint32_t>(gx), m_dense_width - 2);
        const uint32_t cz = min(static_cast<uint32_t>(gz), m_dense_height - 2);
        const auto page = m_detail_pages.find(cz * (m_dense_width - 1) + cx);
        if (page == m_detail_pages.end())
        {
            return false;
        }

        const uint32_t n      = GetDetailSubdivisions();
        const uint32_t stride = n + 1;
        if (page->second.size() != static_cast<size_t>(stride) * stride)
        {
            return false;
        }

        const float fx = clamp((gx - static_cast<float>(cx)) * n, 0.0f, static_cast<float>(n));
        const float fz = clamp((gz - static_cast<float>(cz)) * n, 0.0f, static_cast<float>(n));
        const uint32_t ix = min(static_cast<uint32_t>(fx), n - 1);
        const uint32_t iz = min(static_cast<uint32_t>(fz), n - 1);
        const float* h = page->second.data();
        height_out = terrain_placement::triangle_height(
            h[iz * stride + ix],
            h[iz * stride + ix + 1],
            h[(iz + 1) * stride + ix],
            h[(iz + 1) * stride + ix + 1],
            fx - static_cast<float>(ix),
            fz - static_cast<float>(iz)
        );
        return true;
    }

    float Terrain::SampleSurfaceLocal(float local_x, float local_z) const
    {
        float height = 0.0f;
        if (SampleDetailLocal(local_x, local_z, height))
        {
            return height;
        }

        return TerrainSystem::SampleHeight(m_positions, m_dense_width, m_dense_height, local_x, local_z, GetGridMapping());
    }

    void Terrain::PadCellRect(const TerrainPlatform& pad, int32_t& x0, int32_t& z0, int32_t& x1, int32_t& z1) const
    {
        float cx = 0.0f, cz = 0.0f, hx = 0.0f, hz = 0.0f, yaw = 0.0f, height = 0.0f;
        platform_to_local(GetEntity(), pad, cx, cz, hx, hz, yaw, height);

        const TerrainGridMapping mapping = GetGridMapping();
        const float sx        = max(mapping.scale_x, 1e-4f);
        const float sz        = max(mapping.scale_z, 1e-4f);
        const float half_diag = 0.5f * sqrtf(sx * sx + sz * sz);
        const float reach     = max({ pad.margin, sx, sz, 1.0f }) + half_diag;

        float min_x = 0.0f, min_z = 0.0f, max_x = 0.0f, max_z = 0.0f;
        obb_write_aabb(cx, cz, hx, hz, yaw, min_x, min_z, max_x, max_z);
        x0 = static_cast<int32_t>(floorf((min_x - reach + mapping.offset_x) / sx));
        z0 = static_cast<int32_t>(floorf((min_z - reach + mapping.offset_z) / sz));
        x1 = static_cast<int32_t>(floorf((max_x + reach + mapping.offset_x) / sx));
        z1 = static_cast<int32_t>(floorf((max_z + reach + mapping.offset_z) / sz));
    }

    void Terrain::MarkDetailDirtyForPad(const TerrainPlatform& pad, bool cook_physics)
    {
        if (pad.half_x <= 0.05f || pad.half_z <= 0.05f || !HasHeightfield())
        {
            return;
        }

        int32_t x0 = 0, z0 = 0, x1 = 0, z1 = 0;
        PadCellRect(pad, x0, z0, x1, z1);
        MarkDetailDirtyCells(x0, z0, x1, z1, cook_physics);
    }

    void Terrain::MarkDetailDirtyCells(int32_t x0, int32_t z0, int32_t x1, int32_t z1, bool cook_physics)
    {
        if (m_dense_width < 2 || m_dense_height < 2)
        {
            return;
        }

        const int32_t max_x = static_cast<int32_t>(m_dense_width) - 2;
        const int32_t max_z = static_cast<int32_t>(m_dense_height) - 2;
        x0 = clamp(x0, 0, max_x);
        z0 = clamp(z0, 0, max_z);
        x1 = clamp(x1, 0, max_x);
        z1 = clamp(z1, 0, max_z);
        if (x1 < x0 || z1 < z0)
        {
            return;
        }

        m_detail_dirty.push_back({ x0, z0, x1, z1 });
        m_detail_dirty_cook = m_detail_dirty_cook || cook_physics;
    }

    void Terrain::FlushDetail()
    {
        if (m_detail_dirty.empty())
        {
            return;
        }

        if (!HasHeightfield() || IsCpuGenerationPending() || !m_mesh || m_positions_seed.size() != m_positions.size())
        {
            return;
        }

        SP_PROFILE_CPU();

        // overlapping or touching rects merge, each pad changed this frame is otherwise its own region
        vector<array<int32_t, 4>> rects = move(m_detail_dirty);
        m_detail_dirty.clear();
        const bool cook = m_detail_dirty_cook;
        m_detail_dirty_cook = false;

        bool merged = true;
        while (merged)
        {
            merged = false;
            for (size_t i = 0; i < rects.size() && !merged; i++)
            {
                for (size_t j = i + 1; j < rects.size(); j++)
                {
                    array<int32_t, 4>& a = rects[i];
                    const array<int32_t, 4>& b = rects[j];
                    if (a[2] + 2 < b[0] || b[2] + 2 < a[0] || a[3] + 2 < b[1] || b[3] + 2 < a[1])
                    {
                        continue;
                    }

                    a = { min(a[0], b[0]), min(a[1], b[1]), max(a[2], b[2]), max(a[3], b[3]) };
                    rects.erase(rects.begin() + j);
                    merged = true;
                    break;
                }
            }
        }

        for (const array<int32_t, 4>& rect : rects)
        {
            RebuildDetailRegion(rect[0], rect[1], rect[2], rect[3], cook);
        }

        UploadDetailTextures();
    }

    void Terrain::RebuildDetailRegion(int32_t x0, int32_t z0, int32_t x1, int32_t z1, bool cook_physics)
    {
        SP_PROFILE_CPU();

        const int32_t cells_x = static_cast<int32_t>(m_dense_width) - 1;
        const int32_t cells_z = static_cast<int32_t>(m_dense_height) - 1;
        const uint32_t width  = m_dense_width;

        // one ring more, the cells around a change re-derive the borders and corners they share with it
        const int32_t rx0 = max(x0 - 1, 0);
        const int32_t rz0 = max(z0 - 1, 0);
        const int32_t rx1 = min(x1 + 1, cells_x - 1);
        const int32_t rz1 = min(z1 + 1, cells_z - 1);
        const int32_t region_w = rx1 - rx0 + 1;
        const int32_t region_h = rz1 - rz0 + 1;
        if (region_w <= 0 || region_h <= 0)
        {
            return;
        }

        const TerrainGridMapping mapping = GetGridMapping();
        const float sx        = max(mapping.scale_x, 1e-4f);
        const float sz        = max(mapping.scale_z, 1e-4f);
        const float half_diag = 0.5f * sqrtf(sx * sx + sz * sz);
        const uint32_t n      = GetDetailSubdivisions();
        const uint32_t stride = n + 1;
        const float sub_x     = sx / static_cast<float>(n);
        const float sub_z     = sz / static_cast<float>(n);

        // the pads that reach into the region, in terrain space
        vector<PadLocal> pads;
        for (const TerrainPlatform& pad : m_platforms)
        {
            if (pad.half_x <= 0.05f || pad.half_z <= 0.05f)
            {
                continue;
            }

            int32_t px0 = 0, pz0 = 0, px1 = 0, pz1 = 0;
            PadCellRect(pad, px0, pz0, px1, pz1);
            if (px1 < rx0 || px0 > rx1 || pz1 < rz0 || pz0 > rz1)
            {
                continue;
            }

            PadLocal local;
            platform_to_local(GetEntity(), pad, local.cx, local.cz, local.hx, local.hz, local.yaw, local.height);
            local.influence = max({ pad.margin, sx, sz, 1.0f });

            // the ramp grows with the height it has to make up so the ground never turns into a wall
            float rise = 0.0f;
            float ground_min = 0.0f;
            float ground_max = 0.0f;
            if (SampleSeedRange(pad.center_x, pad.center_z, pad.half_x, pad.half_z, pad.yaw, ground_min, ground_max))
            {
                rise = max(fabsf(pad.height - ground_min), fabsf(pad.height - ground_max));
            }
            local.ramp = clamp(max(pad.margin, rise * detail_ramp_run), 1.0f, local.influence);
            pads.push_back(local);
        }

        // which cells carry a page, 1 for a pad, 2 for a road cell that only keeps its page if the coarse triangles miss it
        vector<uint8_t> want(static_cast<size_t>(region_w) * region_h, 0);
        for (const PadLocal& pad : pads)
        {
            float min_x = 0.0f, min_z = 0.0f, max_x = 0.0f, max_z = 0.0f;
            obb_write_aabb(pad.cx, pad.cz, pad.hx, pad.hz, pad.yaw, min_x, min_z, max_x, max_z);
            const float reach = pad.influence + half_diag;
            const int32_t px0 = max(static_cast<int32_t>(floorf((min_x - reach + mapping.offset_x) / sx)), rx0);
            const int32_t pz0 = max(static_cast<int32_t>(floorf((min_z - reach + mapping.offset_z) / sz)), rz0);
            const int32_t px1 = min(static_cast<int32_t>(floorf((max_x + reach + mapping.offset_x) / sx)), rx1);
            const int32_t pz1 = min(static_cast<int32_t>(floorf((max_z + reach + mapping.offset_z) / sz)), rz1);
            for (int32_t z = pz0; z <= pz1; z++)
            {
                for (int32_t x = px0; x <= px1; x++)
                {
                    const float center_x = (static_cast<float>(x) + 0.5f) * sx - mapping.offset_x;
                    const float center_z = (static_cast<float>(z) + 0.5f) * sz - mapping.offset_z;
                    if (obb_outside_distance(center_x, center_z, pad.cx, pad.cz, pad.hx, pad.hz, pad.yaw) <= reach)
                    {
                        want[static_cast<size_t>(z - rz0) * region_w + (x - rx0)] = 1;
                    }
                }
            }
        }

        // the road segments that reach each candidate cell, for the edge test below
        unordered_map<uint32_t, vector<pair<const RoadCarveJob*, uint32_t>>> cell_segments;
        for (const auto& [id, job] : m_road_carve_jobs)
        {
            if (job.bounds[1] < rx0 || job.bounds[0] > rx1 + 1 || job.bounds[3] < rz0 || job.bounds[2] > rz1 + 1)
            {
                continue;
            }

            for (size_t s = 0; s + 1 < job.points.size(); s++)
            {
                const Vector3& a = job.points[s];
                const Vector3& b = job.points[s + 1];
                const float reach = max(job.half_widths[s], job.half_widths[s + 1]) + job.shoulder + half_diag;
                const int32_t sx0 = max(static_cast<int32_t>(floorf((min(a.x, b.x) - reach + mapping.offset_x) / sx)), rx0);
                const int32_t sz0 = max(static_cast<int32_t>(floorf((min(a.z, b.z) - reach + mapping.offset_z) / sz)), rz0);
                const int32_t sx1 = min(static_cast<int32_t>(floorf((max(a.x, b.x) + reach + mapping.offset_x) / sx)), rx1);
                const int32_t sz1 = min(static_cast<int32_t>(floorf((max(a.z, b.z) + reach + mapping.offset_z) / sz)), rz1);
                const float dx = b.x - a.x;
                const float dz = b.z - a.z;
                const float length_sq = dx * dx + dz * dz;
                for (int32_t z = sz0; z <= sz1; z++)
                {
                    for (int32_t x = sx0; x <= sx1; x++)
                    {
                        uint8_t& slot = want[static_cast<size_t>(z - rz0) * region_w + (x - rx0)];
                        if (slot == 1 || m_detail_road_chunks.find(detail_chunk_key(static_cast<uint32_t>(x) / detail_chunk_cells, static_cast<uint32_t>(z) / detail_chunk_cells)) == m_detail_road_chunks.end())
                        {
                            continue;
                        }

                        const float center_x = (static_cast<float>(x) + 0.5f) * sx - mapping.offset_x;
                        const float center_z = (static_cast<float>(z) + 0.5f) * sz - mapping.offset_z;
                        const float t  = length_sq > 1e-8f ? clamp(((center_x - a.x) * dx + (center_z - a.z) * dz) / length_sq, 0.0f, 1.0f) : 0.0f;
                        const float ox = center_x - (a.x + dx * t);
                        const float oz = center_z - (a.z + dz * t);
                        if (ox * ox + oz * oz <= reach * reach)
                        {
                            slot = 2;
                            cell_segments[static_cast<uint32_t>(z) * cells_x + x].emplace_back(&job, static_cast<uint32_t>(s));
                        }
                    }
                }
            }
        }

        auto coarse = [&](int32_t vx, int32_t vz) -> float
        {
            return m_positions[static_cast<size_t>(vz) * width + vx].y;
        };

        // page heights before any edge is pinned, the road candidates decide here whether they need one
        unordered_map<uint32_t, vector<float>> raw;
        vector<Vector3> grid(static_cast<size_t>(stride) * stride);
        const float plateau = max(sub_x, sub_z);
        uint32_t road_pages = 0;
        for (int32_t z = rz0; z <= rz1; z++)
        {
            for (int32_t x = rx0; x <= rx1; x++)
            {
                uint8_t& slot = want[static_cast<size_t>(z - rz0) * region_w + (x - rx0)];
                if (slot == 0)
                {
                    continue;
                }

                // a road cell needs a page where the coarse triangles leave the envelope the road grades to,
                // a floating deck edge or ground poking through it, near the edge where the deck shows it
                if (slot == 2)
                {
                    const auto segments = cell_segments.find(static_cast<uint32_t>(z) * cells_x + x);
                    const float h00 = coarse(x, z);
                    const float h10 = coarse(x + 1, z);
                    const float h01 = coarse(x, z + 1);
                    const float h11 = coarse(x + 1, z + 1);
                    bool needed = false;
                    for (uint32_t j = 0; j < stride && !needed && segments != cell_segments.end(); j++)
                    {
                        const float lz = static_cast<float>(static_cast<uint32_t>(z) * n + j) * sub_z - mapping.offset_z;
                        for (uint32_t i = 0; i < stride && !needed; i++)
                        {
                            const float lx = static_cast<float>(static_cast<uint32_t>(x) * n + i) * sub_x - mapping.offset_x;
                            float lowest  = numeric_limits<float>::max();
                            float highest = -numeric_limits<float>::max();
                            bool near_edge = false;
                            for (const auto& [job, s] : segments->second)
                            {
                                const Vector3& a = job->points[s];
                                const Vector3& b = job->points[s + 1];
                                const float dx = b.x - a.x;
                                const float dz = b.z - a.z;
                                const float length_sq = dx * dx + dz * dz;
                                const float t  = length_sq > 1e-8f ? clamp(((lx - a.x) * dx + (lz - a.z) * dz) / length_sq, 0.0f, 1.0f) : 0.0f;
                                const float ox = lx - (a.x + dx * t);
                                const float oz = lz - (a.z + dz * t);
                                const float half = job->half_widths[s] + (job->half_widths[s + 1] - job->half_widths[s]) * t;
                                const float over = sqrtf(ox * ox + oz * oz) - half;
                                if (over > detail_road_band)
                                {
                                    continue;
                                }

                                near_edge = true;
                                const float bed = a.y + (b.y - a.y) * t - job->bed_drop;
                                highest = max(highest, bed - max(over, 0.0f) * job->fill_slope);
                                lowest  = min(lowest, bed + max(over - plateau, 0.0f) * job->cut_slope);
                            }

                            if (!near_edge)
                            {
                                continue;
                            }

                            const float surface = terrain_placement::triangle_height(h00, h10, h01, h11, static_cast<float>(i) / n, static_cast<float>(j) / n);
                            const float target  = min(max(surface, highest), lowest);
                            needed = fabsf(surface - target) > detail_road_tolerance;
                        }
                    }

                    if (!needed)
                    {
                        slot = 0;
                        continue;
                    }
                    road_pages++;
                }

                // sample positions come from global sample indices, the edge two pages share is bit identical
                for (uint32_t j = 0; j < stride; j++)
                {
                    const float lz = static_cast<float>(static_cast<uint32_t>(z) * n + j) * sub_z - mapping.offset_z;
                    for (uint32_t i = 0; i < stride; i++)
                    {
                        const float lx = static_cast<float>(static_cast<uint32_t>(x) * n + i) * sub_x - mapping.offset_x;
                        float y = TerrainSystem::SampleHeight(m_positions_seed, m_dense_width, m_dense_height, lx, lz, mapping);
                        for (const PadLocal& pad : pads)
                        {
                            const float distance = obb_outside_distance(lx, lz, pad.cx, pad.cz, pad.hx, pad.hz, pad.yaw);
                            if (distance >= pad.ramp)
                            {
                                continue;
                            }

                            const float weight = distance <= 0.0f ? 1.0f : 1.0f - smooth_step(distance / pad.ramp);
                            y += (pad.height - y) * weight;
                        }
                        // a wide bank reaches over its neighbours, every footprint keeps its own height
                        for (const PadLocal& pad : pads)
                        {
                            if (obb_outside_distance(lx, lz, pad.cx, pad.cz, pad.hx, pad.hz, pad.yaw) <= 0.0f)
                            {
                                y = pad.height;
                            }
                        }

                        grid[static_cast<size_t>(j) * stride + i] = Vector3(lx, y, lz);
                    }
                }

                // roads grade the page after the pads, the same order the coarse grid gets them in, but on a
                // bench one sample wide instead of the coarse one that has to span a whole cell
                TerrainGridMapping page_mapping;
                page_mapping.scale_x  = sub_x;
                page_mapping.scale_z  = sub_z;
                page_mapping.offset_x = mapping.offset_x - static_cast<float>(static_cast<uint32_t>(x) * n) * sub_x;
                page_mapping.offset_z = mapping.offset_z - static_cast<float>(static_cast<uint32_t>(z) * n) * sub_z;
                ApplyRoadHeightConstraints(grid, stride, page_mapping, 0, 0, static_cast<int32_t>(n), static_cast<int32_t>(n), plateau, nullptr);

                vector<float> heights(static_cast<size_t>(stride) * stride);
                for (size_t k = 0; k < heights.size(); k++)
                {
                    heights[k] = grid[k].y;
                }
                raw.emplace(static_cast<uint32_t>(z) * cells_x + x, move(heights));
            }
        }

        // the main thread is the only writer, it reads the map without the lock
        auto is_page = [&](int32_t x, int32_t z) -> bool
        {
            if (x < 0 || z < 0 || x >= cells_x || z >= cells_z)
            {
                return false;
            }

            if (x >= rx0 && x <= rx1 && z >= rz0 && z <= rz1)
            {
                return want[static_cast<size_t>(z - rz0) * region_w + (x - rx0)] != 0;
            }

            return m_detail_pages.find(static_cast<uint32_t>(z) * cells_x + x) != m_detail_pages.end();
        };

        // a coarse vertex sinks only when every cell around it is a page, otherwise a coarse cell uses it
        auto corner_interior = [&](int32_t vx, int32_t vz) -> bool
        {
            return is_page(vx - 1, vz - 1) && is_page(vx, vz - 1) && is_page(vx - 1, vz) && is_page(vx, vz);
        };

        if (road_pages > 0)
        {
            SP_LOG_INFO("detail region %d, %d to %d, %d: %zu pages, %u of them for roads", rx0, rz0, rx1, rz1, raw.size(), road_pages);
        }

        unordered_map<uint32_t, vector<float>> fresh;
        for (const auto& [key, raw_heights] : raw)
        {
            const int32_t x = static_cast<int32_t>(key % cells_x);
            const int32_t z = static_cast<int32_t>(key / cells_x);

            // pin every edge that borders a coarse cell to the coarse edge, and every corner a coarse cell uses
            const float h00 = coarse(x, z);
            const float h10 = coarse(x + 1, z);
            const float h01 = coarse(x, z + 1);
            const float h11 = coarse(x + 1, z + 1);
            const bool left   = !is_page(x - 1, z);
            const bool right  = !is_page(x + 1, z);
            const bool bottom = !is_page(x, z - 1);
            const bool top    = !is_page(x, z + 1);
            const bool c00 = !corner_interior(x, z);
            const bool c10 = !corner_interior(x + 1, z);
            const bool c01 = !corner_interior(x, z + 1);
            const bool c11 = !corner_interior(x + 1, z + 1);

            vector<float> heights(static_cast<size_t>(stride) * stride);
            for (uint32_t j = 0; j < stride; j++)
            {
                const float tz = static_cast<float>(j) / static_cast<float>(n);
                for (uint32_t i = 0; i < stride; i++)
                {
                    const float tx = static_cast<float>(i) / static_cast<float>(n);
                    float y = raw_heights[static_cast<size_t>(j) * stride + i];
                    if (i == 0 && left)        y = h00 + (h01 - h00) * tz;
                    else if (i == n && right)  y = h10 + (h11 - h10) * tz;
                    else if (j == 0 && bottom) y = h00 + (h10 - h00) * tx;
                    else if (j == n && top)    y = h01 + (h11 - h01) * tx;

                    if (i == 0 && j == 0 && c00) y = h00;
                    if (i == n && j == 0 && c10) y = h10;
                    if (i == 0 && j == n && c01) y = h01;
                    if (i == n && j == n && c11) y = h11;

                    heights[static_cast<size_t>(j) * stride + i] = y;
                }
            }

            fresh.emplace(key, move(heights));
        }

        // how far each interior coarse vertex has to sink so no coarse triangle reaches above a page,
        // lowering a vertex only ever lowers the triangles around it, so the largest demand satisfies all
        unordered_map<uint32_t, float> drops;
        for (const auto& [key, heights] : fresh)
        {
            const int32_t x = static_cast<int32_t>(key % cells_x);
            const int32_t z = static_cast<int32_t>(key / cells_x);
            const bool i00 = corner_interior(x, z);
            const bool i10 = corner_interior(x + 1, z);
            const bool i01 = corner_interior(x, z + 1);
            const bool i11 = corner_interior(x + 1, z + 1);
            if (!i00 && !i10 && !i01 && !i11)
            {
                continue;
            }

            const float h00 = coarse(x, z);
            const float h10 = coarse(x + 1, z);
            const float h01 = coarse(x, z + 1);
            const float h11 = coarse(x + 1, z + 1);
            const uint32_t v00 = static_cast<uint32_t>(z) * width + x;
            const uint32_t v10 = v00 + 1;
            const uint32_t v01 = v00 + width;
            const uint32_t v11 = v01 + 1;

            for (uint32_t j = 0; j < stride; j++)
            {
                const float fz = static_cast<float>(j) / static_cast<float>(n);
                for (uint32_t i = 0; i < stride; i++)
                {
                    const float fx = static_cast<float>(i) / static_cast<float>(n);
                    const float page = heights[static_cast<size_t>(j) * stride + i];

                    // the same diagonal the coarse mesh uses, br to tl
                    float w[3];
                    uint32_t v[3];
                    bool in[3];
                    float surface = 0.0f;
                    if (fx + fz <= 1.0f)
                    {
                        w[0] = 1.0f - fx - fz; v[0] = v00; in[0] = i00;
                        w[1] = fx;             v[1] = v10; in[1] = i10;
                        w[2] = fz;             v[2] = v01; in[2] = i01;
                        surface = h00 * w[0] + h10 * w[1] + h01 * w[2];
                    }
                    else
                    {
                        w[0] = fx + fz - 1.0f; v[0] = v11; in[0] = i11;
                        w[1] = 1.0f - fx;      v[1] = v01; in[1] = i01;
                        w[2] = 1.0f - fz;      v[2] = v10; in[2] = i10;
                        surface = h11 * w[0] + h01 * w[1] + h10 * w[2];
                    }

                    const float excess = surface - (page - detail_sink);
                    if (excess <= 0.0f)
                    {
                        continue;
                    }

                    float movable = 0.0f;
                    for (uint32_t k = 0; k < 3; k++)
                    {
                        movable += in[k] ? w[k] : 0.0f;
                    }

                    if (movable < 0.05f)
                    {
                        continue;
                    }

                    const float drop = min(excess / movable, 500.0f);
                    for (uint32_t k = 0; k < 3; k++)
                    {
                        if (in[k])
                        {
                            float& slot = drops[v[k]];
                            slot = max(slot, drop);
                        }
                    }
                }
            }
        }

        // publish, the ring's outer corners are shared with cells outside, they only ever deepen
        {
            unique_lock lock(m_detail_mutex);
            for (int32_t z = rz0; z <= rz1; z++)
            {
                for (int32_t x = rx0; x <= rx1; x++)
                {
                    m_detail_pages.erase(static_cast<uint32_t>(z) * cells_x + x);
                }
            }

            for (auto& [key, heights] : fresh)
            {
                m_detail_pages[key] = move(heights);
            }

            for (int32_t vz = rz0; vz <= rz1 + 1; vz++)
            {
                for (int32_t vx = rx0; vx <= rx1 + 1; vx++)
                {
                    const uint32_t index  = static_cast<uint32_t>(vz) * width + vx;
                    const bool boundary   = vx == rx0 || vx == rx1 + 1 || vz == rz0 || vz == rz1 + 1;
                    const auto computed   = drops.find(index);
                    if (boundary)
                    {
                        if (computed != drops.end())
                        {
                            float& slot = m_detail_drop[index];
                            slot = max(slot, computed->second);
                        }
                    }
                    else if (computed != drops.end())
                    {
                        m_detail_drop[index] = computed->second;
                    }
                    else
                    {
                        m_detail_drop.erase(index);
                    }
                }
            }
        }

        // the coarse mesh picks up the new drops and the collision its new holes
        MarkHeightsDirty(rx0, rz0, rx1 + 1, rz1 + 1);
        FlushHeightEdits(cook_physics);

        const uint32_t chunk_x0 = static_cast<uint32_t>(rx0) / detail_chunk_cells;
        const uint32_t chunk_z0 = static_cast<uint32_t>(rz0) / detail_chunk_cells;
        const uint32_t chunk_x1 = static_cast<uint32_t>(rx1) / detail_chunk_cells;
        const uint32_t chunk_z1 = static_cast<uint32_t>(rz1) / detail_chunk_cells;
        for (uint32_t cz = chunk_z0; cz <= chunk_z1; cz++)
        {
            for (uint32_t cx = chunk_x0; cx <= chunk_x1; cx++)
            {
                RebuildDetailChunk(cx, cz, cook_physics);
            }
        }
    }

    void Terrain::RebuildDetailChunk(uint32_t chunk_x, uint32_t chunk_z, bool cook_physics)
    {
        SP_PROFILE_CPU();
        if (!m_entity_ptr)
        {
            return;
        }

        const uint32_t cells_x = m_dense_width - 1;
        const uint32_t cells_z = m_dense_height - 1;
        const uint32_t x_begin = chunk_x * detail_chunk_cells;
        const uint32_t z_begin = chunk_z * detail_chunk_cells;
        const uint32_t x_end   = min(x_begin + detail_chunk_cells, cells_x);
        const uint32_t z_end   = min(z_begin + detail_chunk_cells, cells_z);

        const TerrainGridMapping mapping = GetGridMapping();
        const float sx        = max(mapping.scale_x, 1e-4f);
        const float sz        = max(mapping.scale_z, 1e-4f);
        const uint32_t n      = GetDetailSubdivisions();
        const uint32_t stride = n + 1;
        const float sub_x     = sx / static_cast<float>(n);
        const float sub_z     = sz / static_cast<float>(n);
        const float extent_x  = max(mapping.extent_x, 0.001f);
        const float extent_z  = max(mapping.extent_z, 0.001f);

        // the chunk entity sits at its own corner so the vertices stay small, worlds sit kilometres out
        const float origin_x = static_cast<float>(x_begin) * sx - mapping.offset_x;
        const float origin_z = static_cast<float>(z_begin) * sz - mapping.offset_z;

        auto has_page = [&](int64_t x, int64_t z) -> bool
        {
            if (x < 0 || z < 0 || x >= static_cast<int64_t>(cells_x) || z >= static_cast<int64_t>(cells_z))
            {
                return false;
            }

            return m_detail_pages.find(static_cast<uint32_t>(z) * cells_x + static_cast<uint32_t>(x)) != m_detail_pages.end();
        };

        vector<RHI_Vertex_PosTexNorTan> vertices;
        vector<uint32_t> indices;
        for (uint32_t z = z_begin; z < z_end; z++)
        {
            for (uint32_t x = x_begin; x < x_end; x++)
            {
                const auto page = m_detail_pages.find(z * cells_x + x);
                if (page == m_detail_pages.end() || page->second.size() != static_cast<size_t>(stride) * stride)
                {
                    continue;
                }

                const bool left   = !has_page(static_cast<int64_t>(x) - 1, z);
                const bool right  = !has_page(static_cast<int64_t>(x) + 1, z);
                const bool bottom = !has_page(x, static_cast<int64_t>(z) - 1);
                const bool top    = !has_page(x, static_cast<int64_t>(z) + 1);

                const uint32_t base = static_cast<uint32_t>(vertices.size());
                for (uint32_t j = 0; j < stride; j++)
                {
                    const float lz = static_cast<float>(z * n + j) * sub_z - mapping.offset_z;
                    for (uint32_t i = 0; i < stride; i++)
                    {
                        const float lx = static_cast<float>(x * n + i) * sub_x - mapping.offset_x;
                        const float y  = page->second[static_cast<size_t>(j) * stride + i];

                        const float h_left   = SampleSurfaceLocal(lx - sub_x, lz);
                        const float h_right  = SampleSurfaceLocal(lx + sub_x, lz);
                        const float h_bottom = SampleSurfaceLocal(lx, lz - sub_z);
                        const float h_top    = SampleSurfaceLocal(lx, lz + sub_z);
                        Vector3 normal = Vector3(-(h_right - h_left) / (2.0f * sub_x), 1.0f, -(h_top - h_bottom) / (2.0f * sub_z)).Normalized();

                        float border = numeric_limits<float>::max();
                        if (left)   border = min(border, static_cast<float>(i) * sub_x);
                        if (right)  border = min(border, static_cast<float>(n - i) * sub_x);
                        if (bottom) border = min(border, static_cast<float>(j) * sub_z);
                        if (top)    border = min(border, static_cast<float>(n - j) * sub_z);
                        if (border < detail_normal_blend)
                        {
                            const Vector3 coarse = TerrainSystem::SampleNormal(m_positions, m_dense_width, m_dense_height, lx, lz, mapping);
                            const float t = smooth_step(border / detail_normal_blend);
                            normal = (coarse * (1.0f - t) + normal * t).Normalized();
                        }

                        RHI_Vertex_PosTexNorTan vertex(Vector3(lx - origin_x, y, lz - origin_z), Vector2((lx + mapping.offset_x) / extent_x, (lz + mapping.offset_z) / extent_z));
                        write_tangent(vertex, normal);
                        vertices.push_back(vertex);
                    }
                }

                for (uint32_t j = 0; j < n; j++)
                {
                    for (uint32_t i = 0; i < n; i++)
                    {
                        const uint32_t bl = base + j * stride + i;
                        const uint32_t br = bl + 1;
                        const uint32_t tl = bl + stride;
                        const uint32_t tr = tl + 1;
                        indices.insert(indices.end(), { br, bl, tl, br, tl, tr });
                    }
                }

                // skirts along the coarse borders, both windings so either side covers a crack
                auto skirt = [&](uint32_t first, uint32_t step)
                {
                    for (uint32_t k = 0; k < n; k++)
                    {
                        const uint32_t a = base + first + k * step;
                        const uint32_t b = a + step;
                        RHI_Vertex_PosTexNorTan low_a = vertices[a];
                        RHI_Vertex_PosTexNorTan low_b = vertices[b];
                        low_a.pos[1] -= detail_skirt;
                        low_b.pos[1] -= detail_skirt;
                        const uint32_t la = static_cast<uint32_t>(vertices.size());
                        vertices.push_back(low_a);
                        vertices.push_back(low_b);
                        const uint32_t lb = la + 1;
                        indices.insert(indices.end(), { a, b, lb, a, lb, la, a, lb, b, a, la, lb });
                    }
                };
                if (bottom) skirt(0, 1);
                if (top)    skirt(n * stride, 1);
                if (left)   skirt(0, stride);
                if (right)  skirt(n, stride);
            }
        }

        const string name  = detail_chunk_name(chunk_x, chunk_z);
        const uint64_t key = detail_chunk_key(chunk_x, chunk_z);
        Entity* child      = m_entity_ptr->GetChildByName(name);
        if (vertices.empty())
        {
            if (child)
            {
                if (Render* render = child->GetComponent<Render>())
                {
                    render->ClearMesh();
                }
                World::RemoveEntity(child);
            }
            m_detail_meshes.erase(key);
            return;
        }

        if (!child)
        {
            child = World::CreateEntity();
            child->SetObjectName(name);
            child->SetTransient(true);
            child->SetParent(m_entity_ptr);
            World::ProcessPendingAdditions();
        }
        child->SetPositionLocal(Vector3(origin_x, 0.0f, origin_z));

        shared_ptr<Mesh> mesh = make_shared<Mesh>();
        mesh->SetObjectName(name);
        mesh->SetFlag(static_cast<uint32_t>(MeshFlags::PostProcessOptimize), false);
        mesh->SetFlag(static_cast<uint32_t>(MeshFlags::PostProcessNormalizeScale), false);
        mesh->SetFlag(static_cast<uint32_t>(MeshFlags::PostProcessGenerateLods), false);
        mesh->SetDynamic(true);
        mesh->SetPersistent(false);
        mesh->AddGeometry(vertices, indices, false);
        mesh->CreateGpuBuffers();

        Render* render = child->GetComponent<Render>();
        if (!render)
        {
            render = child->AddComponent<Render>();
        }
        render->SetMesh(mesh.get());
        // simplified collision pulls the chunk borders in and opens cracks between neighbouring chunks
        render->SetFlag(RenderFlags::PreserveCollisionGeometry);
        if (m_material && render->GetMaterial() != m_material.get())
        {
            render->SetMaterial(m_material);
        }
        m_detail_meshes[key] = mesh;

        if (cook_physics)
        {
            Physics* physics = child->GetComponent<Physics>();
            if (!physics)
            {
                physics = child->AddComponent<Physics>();
                physics->SetUseConvexHull(false);
                physics->SetBodyType(BodyType::Mesh);
            }
            else
            {
                physics->Rebuild();
            }
        }
    }

    void Terrain::DestroyDetail()
    {
        if (m_entity_ptr)
        {
            vector<Entity*> children = m_entity_ptr->GetChildren();
            for (Entity* child : children)
            {
                if (child && child->GetObjectName().rfind("terrain_detail_", 0) == 0)
                {
                    if (Render* render = child->GetComponent<Render>())
                    {
                        render->ClearMesh();
                    }
                    World::RemoveEntity(child);
                }
            }
        }

        {
            unique_lock lock(m_detail_mutex);
            m_detail_pages.clear();
            m_detail_drop.clear();
            m_detail_meshes.clear();
            m_detail_dirty.clear();
            m_detail_dirty_cook = false;
        }
        m_detail_road_chunks.clear();
        m_detail_window_x = INT32_MIN;
        m_detail_window_z = INT32_MIN;

        if (m_detail_table_gpu)
        {
            UploadDetailTextures();
        }
    }

    void Terrain::UpdateDetailWindow()
    {
        if (!HasHeightfield() || !m_entity_ptr || m_dense_width < 2 || m_dense_height < 2)
        {
            return;
        }

        Camera* camera = World::GetCamera();
        if (!camera || !camera->GetEntity())
        {
            return;
        }

        const TerrainGridMapping mapping = GetGridMapping();
        const Vector3 local    = m_entity_ptr->GetMatrix().Inverted() * camera->GetEntity()->GetPosition();
        const int32_t window_x = static_cast<int32_t>(floorf((local.x + mapping.offset_x) / max(mapping.scale_x, 1e-4f) / detail_chunk_cells));
        const int32_t window_z = static_cast<int32_t>(floorf((local.z + mapping.offset_z) / max(mapping.scale_z, 1e-4f) / detail_chunk_cells));
        if (window_x == m_detail_window_x && window_z == m_detail_window_z)
        {
            return;
        }

        const int32_t chunks_x = static_cast<int32_t>((m_dense_width - 1 + detail_chunk_cells - 1) / detail_chunk_cells);
        const int32_t chunks_z = static_cast<int32_t>((m_dense_height - 1 + detail_chunk_cells - 1) / detail_chunk_cells);
        unordered_set<uint64_t> next;
        for (int32_t z = window_z - detail_road_window; z <= window_z + detail_road_window; z++)
        {
            for (int32_t x = window_x - detail_road_window; x <= window_x + detail_road_window; x++)
            {
                if (x >= 0 && z >= 0 && x < chunks_x && z < chunks_z)
                {
                    next.insert(detail_chunk_key(static_cast<uint32_t>(x), static_cast<uint32_t>(z)));
                }
            }
        }

        auto mark_chunk = [this](uint64_t key)
        {
            const int32_t x = static_cast<int32_t>(key & 0xffffffffu) * static_cast<int32_t>(detail_chunk_cells);
            const int32_t z = static_cast<int32_t>(key >> 32) * static_cast<int32_t>(detail_chunk_cells);
            MarkDetailDirtyCells(x, z, x + static_cast<int32_t>(detail_chunk_cells) - 1, z + static_cast<int32_t>(detail_chunk_cells) - 1, true);
        };

        if (!m_road_carve_jobs.empty())
        {
            for (uint64_t key : next)
            {
                if (m_detail_road_chunks.find(key) == m_detail_road_chunks.end())
                {
                    mark_chunk(key);
                }
            }
            for (uint64_t key : m_detail_road_chunks)
            {
                if (next.find(key) == next.end())
                {
                    mark_chunk(key);
                }
            }
        }

        m_detail_road_chunks = move(next);
        m_detail_window_x    = window_x;
        m_detail_window_z    = window_z;
    }

    void Terrain::MarkRoadDetailDirty(int32_t x0, int32_t z0, int32_t x1, int32_t z1)
    {
        if (m_detail_window_x == INT32_MIN)
        {
            return;
        }

        const int32_t span = static_cast<int32_t>(detail_chunk_cells);
        x0 = max(x0, (m_detail_window_x - detail_road_window) * span);
        z0 = max(z0, (m_detail_window_z - detail_road_window) * span);
        x1 = min(x1, (m_detail_window_x + detail_road_window + 1) * span - 1);
        z1 = min(z1, (m_detail_window_z + detail_road_window + 1) * span - 1);
        if (x1 >= x0 && z1 >= z0)
        {
            MarkDetailDirtyCells(x0, z0, x1, z1, true);
        }
    }

    void Terrain::UploadDetailTextures()
    {
        SP_PROFILE_CPU();

        auto clear = [this]()
        {
            m_detail_table_gpu_retired = move(m_detail_table_gpu);
            m_detail_atlas_gpu_retired = move(m_detail_atlas_gpu);
            m_detail_gpu_subdivisions  = 0;
            PushToRenderer();
        };

        if (m_dense_width < 2 || m_dense_height < 2)
        {
            clear();
            return;
        }

        const uint32_t cells_x  = m_dense_width - 1;
        const uint32_t cells_z  = m_dense_height - 1;
        const uint32_t chunks_x = (cells_x + detail_chunk_cells - 1) / detail_chunk_cells;
        const uint32_t chunks_z = (cells_z + detail_chunk_cells - 1) / detail_chunk_cells;
        const uint32_t n        = GetDetailSubdivisions();
        const uint32_t stride   = n + 1;
        const uint32_t block    = detail_chunk_cells * stride;

        shared_lock lock(m_detail_mutex);

        vector<uint32_t> chunks;
        chunks.reserve(m_detail_pages.size());
        for (const auto& [key, heights] : m_detail_pages)
        {
            chunks.push_back(((key / cells_x) / detail_chunk_cells) * chunks_x + (key % cells_x) / detail_chunk_cells);
        }
        sort(chunks.begin(), chunks.end());
        chunks.erase(unique(chunks.begin(), chunks.end()), chunks.end());

        const uint32_t rows = static_cast<uint32_t>((chunks.size() + detail_atlas_blocks_per_row - 1) / detail_atlas_blocks_per_row);
        if (chunks.empty() || rows * block > detail_atlas_max_dimension)
        {
            if (!chunks.empty())
            {
                SP_LOG_WARNING("%zu detail chunks do not fit one atlas, grass and blending fall back to the coarse terrain", chunks.size());
            }
            lock.unlock();
            clear();
            return;
        }

        vector<uint8_t> table_bytes(static_cast<size_t>(chunks_x) * chunks_z * sizeof(float), 0);
        float* table = reinterpret_cast<float*>(table_bytes.data());
        for (size_t slot = 0; slot < chunks.size(); slot++)
        {
            table[chunks[slot]] = static_cast<float>(slot + 1);
        }

        const uint32_t atlas_w = min(static_cast<uint32_t>(chunks.size()), detail_atlas_blocks_per_row) * block;
        const uint32_t atlas_h = rows * block;
        vector<uint8_t> atlas_bytes(static_cast<size_t>(atlas_w) * atlas_h * sizeof(float));
        float* atlas = reinterpret_cast<float*>(atlas_bytes.data());
        fill(atlas, atlas + static_cast<size_t>(atlas_w) * atlas_h, -numeric_limits<float>::max());

        for (const auto& [key, heights] : m_detail_pages)
        {
            if (heights.size() != static_cast<size_t>(stride) * stride)
            {
                continue;
            }

            const uint32_t cx    = key % cells_x;
            const uint32_t cz    = key / cells_x;
            const uint32_t chunk = (cz / detail_chunk_cells) * chunks_x + cx / detail_chunk_cells;
            const uint32_t slot  = static_cast<uint32_t>(lower_bound(chunks.begin(), chunks.end(), chunk) - chunks.begin());
            const uint32_t base_x = (slot % detail_atlas_blocks_per_row) * block + (cx % detail_chunk_cells) * stride;
            const uint32_t base_z = (slot / detail_atlas_blocks_per_row) * block + (cz % detail_chunk_cells) * stride;
            for (uint32_t z = 0; z < stride; z++)
            {
                memcpy(&atlas[static_cast<size_t>(base_z + z) * atlas_w + base_x], &heights[static_cast<size_t>(z) * stride], stride * sizeof(float));
            }
        }
        lock.unlock();

        m_detail_table_gpu_retired = move(m_detail_table_gpu);
        m_detail_atlas_gpu_retired = move(m_detail_atlas_gpu);
        m_detail_table_gpu = make_shared<RHI_Texture>(
            RHI_Texture_Type::Type2D,
            chunks_x, chunks_z, 1, 1,
            RHI_Format::R32_Float, RHI_Texture_Srv,
            "terrain_detail_table", to_single_mip_slice(table_bytes)
        );
        m_detail_atlas_gpu = make_shared<RHI_Texture>(
            RHI_Texture_Type::Type2D,
            atlas_w, atlas_h, 1, 1,
            RHI_Format::R32_Float, RHI_Texture_Srv,
            "terrain_detail_atlas", to_single_mip_slice(atlas_bytes)
        );
        m_detail_gpu_subdivisions = n;
        PushToRenderer();
    }
}
