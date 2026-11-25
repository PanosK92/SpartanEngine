/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

//= INCLUDES ===================
#include "pch.h"
#include "MeshFracture.h"
#include "../core/ThreadPool.h"
#include <algorithm>
#include <cmath>
#include <queue>
#include <unordered_map>
//==============================

//= NAMESPACES ===============
using namespace std;
using namespace spartan::math;
//============================

namespace spartan::mesh_fracture
{
    namespace
    {
        struct ClipVertex
        {
            Vector3 pos = Vector3::Zero;
            Vector2 uv  = Vector2::Zero;
            Vector3 nor = Vector3::Up;
            Vector3 tan = Vector3::Right;
        };

        struct ClipTriangle
        {
            ClipVertex v[3];
        };

        struct Segment3D
        {
            Vector3 a;
            Vector3 b;
        };

        struct Rng
        {
            uint64_t state = 0x9E3779B97F4A7C15ULL;

            explicit Rng(uint32_t seed)
            {
                state = static_cast<uint64_t>(seed) * 0x9E3779B97F4A7C15ULL + 0xBF58476D1CE4E5B9ULL;
                NextU32();
                NextU32();
            }

            uint32_t NextU32()
            {
                uint64_t z = (state += 0x9E3779B97F4A7C15ULL);
                z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
                z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
                return static_cast<uint32_t>(z ^ (z >> 31));
            }

            float NextFloat()
            {
                return static_cast<float>(NextU32() & 0x00FFFFFFu) / static_cast<float>(0x01000000u);
            }

            float Range(float lo, float hi)
            {
                return lo + (hi - lo) * NextFloat();
            }
        };

        ClipVertex lerp_vertex(const ClipVertex& a, const ClipVertex& b, float t)
        {
            ClipVertex out;
            out.pos = a.pos + (b.pos - a.pos) * t;
            out.uv  = a.uv + (b.uv - a.uv) * t;
            out.nor = a.nor + (b.nor - a.nor) * t;
            out.tan = a.tan + (b.tan - a.tan) * t;

            const float nor_len = out.nor.Length();
            out.nor = nor_len > 1.0e-6f ? (out.nor / nor_len) : Vector3::Up;

            const float tan_len = out.tan.Length();
            out.tan = tan_len > 1.0e-6f ? (out.tan / tan_len) : Vector3::Right;
            return out;
        }

        void build_plane_basis(const Vector3& normal, Vector3& out_u, Vector3& out_v)
        {
            const Vector3 up = fabsf(normal.y) < 0.9f ? Vector3::Up : Vector3::Right;
            out_u = Vector3::Cross(up, normal);
            const float u_len = out_u.Length();
            out_u = u_len > 1.0e-6f ? (out_u / u_len) : Vector3::Right;
            out_v = Vector3::Cross(normal, out_u);
            const float v_len = out_v.Length();
            out_v = v_len > 1.0e-6f ? (out_v / v_len) : Vector3::Forward;
        }

        BoundingBox compute_triangle_bounds(const vector<ClipTriangle>& triangles)
        {
            Vector3 mn(FLT_MAX, FLT_MAX, FLT_MAX);
            Vector3 mx(-FLT_MAX, -FLT_MAX, -FLT_MAX);
            for (const ClipTriangle& tri : triangles)
            {
                for (int i = 0; i < 3; ++i)
                {
                    mn.x = min(mn.x, tri.v[i].pos.x);
                    mn.y = min(mn.y, tri.v[i].pos.y);
                    mn.z = min(mn.z, tri.v[i].pos.z);
                    mx.x = max(mx.x, tri.v[i].pos.x);
                    mx.y = max(mx.y, tri.v[i].pos.y);
                    mx.z = max(mx.z, tri.v[i].pos.z);
                }
            }
            return BoundingBox(mn, mx);
        }

        // Fast AABB-against-plane test:
        // returns -1 if completely inside (dot(n, p) <= d), +1 if completely outside, 0 if straddling.
        int classify_aabb_plane(const BoundingBox& box, const Vector3& normal, float d, float eps)
        {
            const Vector3& mn = box.GetMin();
            const Vector3& mx = box.GetMax();

            // furthest corner in the direction of normal (max signed distance)
            const Vector3 p_max(
                normal.x >= 0.0f ? mx.x : mn.x,
                normal.y >= 0.0f ? mx.y : mn.y,
                normal.z >= 0.0f ? mx.z : mn.z
            );
            if (Vector3::Dot(normal, p_max) <= d + eps)
            {
                return -1;
            }

            // nearest corner in the direction of normal (min signed distance)
            const Vector3 p_min(
                normal.x >= 0.0f ? mn.x : mx.x,
                normal.y >= 0.0f ? mn.y : mx.y,
                normal.z >= 0.0f ? mn.z : mx.z
            );
            if (Vector3::Dot(normal, p_min) > d - eps)
            {
                return 1;
            }

            return 0;
        }

        // Emits a cap triangle on plane (normal, d) with Spartan's outward winding: dot(Cross(b - a, c - a), normal) > 0.
        void emit_cap_triangle(
            vector<ClipTriangle>& out_triangles,
            const Vector3& a,
            const Vector3& b,
            const Vector3& c,
            const Vector3& normal,
            const Vector3& u_axis,
            const Vector3& v_axis,
            float uv_scale
        )
        {
            const Vector3 cross = Vector3::Cross(b - a, c - a);
            const float area2   = cross.Length();
            if (area2 <= 1.0e-8f)
            {
                return;
            }

            Vector3 p0 = a;
            Vector3 p1 = b;
            Vector3 p2 = c;
            if (Vector3::Dot(cross, normal) < 0.0f)
            {
                swap(p1, p2);
            }

            ClipTriangle tri;
            const Vector3 pts[3] = { p0, p1, p2 };
            for (int k = 0; k < 3; ++k)
            {
                tri.v[k].pos = pts[k];
                tri.v[k].uv  = Vector2(Vector3::Dot(pts[k], u_axis) * uv_scale, Vector3::Dot(pts[k], v_axis) * uv_scale);
                tri.v[k].nor = normal;
                tri.v[k].tan = u_axis;
            }
            out_triangles.push_back(tri);
        }

        // Clips a closed mesh against half-space dot(normal, p) <= d and caps the cut plane.
        // Returns true if the plane actually cut the mesh (producing cap area).
        bool clip_mesh_by_plane(
            vector<ClipTriangle>& triangles,
            BoundingBox& bounds,
            const Vector3& normal,
            float d,
            float uv_scale,
            bool& out_empty
        )
        {
            constexpr float eps = 1.0e-5f;
            out_empty = false;

            const int side = classify_aabb_plane(bounds, normal, d, eps);
            if (side < 0)
            {
                return false;
            }
            if (side > 0)
            {
                triangles.clear();
                out_empty = true;
                return false;
            }

            vector<ClipTriangle> kept;
            kept.reserve(triangles.size() + 16);

            vector<Segment3D> cut_segments;
            cut_segments.reserve(32);

            for (const ClipTriangle& tri : triangles)
            {
                float dist[3];
                bool inside[3];
                int inside_count = 0;
                for (int i = 0; i < 3; ++i)
                {
                    dist[i]   = Vector3::Dot(normal, tri.v[i].pos) - d;
                    inside[i] = dist[i] <= eps;
                    if (inside[i])
                    {
                        ++inside_count;
                    }
                }

                if (inside_count == 3)
                {
                    kept.push_back(tri);
                    continue;
                }
                if (inside_count == 0)
                {
                    continue;
                }

                // Collect clipped polygon vertices (3 or 4) in original winding order, plus the cut edge points.
                ClipVertex poly[4];
                int poly_count = 0;
                Vector3 cut_pts[2];
                int cut_count = 0;

                for (int i = 0; i < 3; ++i)
                {
                    const int j = (i + 1) % 3;
                    const ClipVertex& va = tri.v[i];
                    const ClipVertex& vb = tri.v[j];
                    const bool in_a = inside[i];
                    const bool in_b = inside[j];

                    if (in_a)
                    {
                        poly[poly_count++] = va;
                    }

                    if (in_a != in_b)
                    {
                        const float denom = dist[i] - dist[j];
                        const float t     = fabsf(denom) > 1.0e-8f ? clamp(dist[i] / denom, 0.0f, 1.0f) : 0.5f;
                        ClipVertex clipped = lerp_vertex(va, vb, t);
                        poly[poly_count++] = clipped;
                        if (cut_count < 2)
                        {
                            cut_pts[cut_count++] = clipped.pos;
                        }
                    }
                }

                if (poly_count >= 3)
                {
                    kept.push_back({ { poly[0], poly[1], poly[2] } });
                }
                if (poly_count == 4)
                {
                    kept.push_back({ { poly[0], poly[2], poly[3] } });
                }
                if (cut_count == 2 && (cut_pts[1] - cut_pts[0]).LengthSquared() > 1.0e-10f)
                {
                    cut_segments.push_back({ cut_pts[0], cut_pts[1] });
                }
            }

            if (kept.empty())
            {
                triangles.clear();
                out_empty = true;
                return false;
            }

            // Cap the cut plane using the collected boundary points.
            // For convex cuts (all Voronoi intersections with convex solids), angularly sorting unique boundary
            // points around their 2D centroid on the cut plane gives a guaranteed clean convex cap polygon.
            bool capped = false;
            if (cut_segments.size() >= 3)
            {
                Vector3 u_axis, v_axis;
                build_plane_basis(normal, u_axis, v_axis);

                vector<Vector3> unique_pts;
                unique_pts.reserve(cut_segments.size() * 2);

                auto add_unique = [&](const Vector3& p)
                {
                    for (const Vector3& existing : unique_pts)
                    {
                        if ((existing - p).LengthSquared() < 1.0e-7f)
                        {
                            return;
                        }
                    }
                    unique_pts.push_back(p);
                };

                for (const Segment3D& seg : cut_segments)
                {
                    add_unique(seg.a);
                    add_unique(seg.b);
                }

                if (unique_pts.size() >= 3)
                {
                    Vector3 center = Vector3::Zero;
                    for (const Vector3& p : unique_pts)
                    {
                        center += p;
                    }
                    center /= static_cast<float>(unique_pts.size());

                    struct OrderedPoint
                    {
                        Vector3 p;
                        float angle;
                    };
                    vector<OrderedPoint> ordered;
                    ordered.reserve(unique_pts.size());
                    for (const Vector3& p : unique_pts)
                    {
                        const Vector3 rel = p - center;
                        const float x     = Vector3::Dot(rel, u_axis);
                        const float y     = Vector3::Dot(rel, v_axis);
                        ordered.push_back({ p, atan2f(y, x) });
                    }
                    sort(ordered.begin(), ordered.end(), [](const OrderedPoint& a, const OrderedPoint& b)
                    {
                        return a.angle < b.angle;
                    });

                    // Fan around center so triangles stay well-shaped even for long slivers
                    float cap_area = 0.0f;
                    const size_t n = ordered.size();
                    for (size_t i = 0; i < n; ++i)
                    {
                        const Vector3& a = ordered[i].p;
                        const Vector3& b = ordered[(i + 1) % n].p;
                        const float tri_area = 0.5f * Vector3::Cross(a - center, b - center).Length();
                        if (tri_area > 1.0e-7f)
                        {
                            emit_cap_triangle(kept, center, a, b, normal, u_axis, v_axis, uv_scale);
                            cap_area += tri_area;
                        }
                    }
                    capped = cap_area > 1.0e-5f;
                }
            }

            triangles = std::move(kept);
            bounds    = compute_triangle_bounds(triangles);
            return capped;
        }

        void generate_voronoi_sites(
            const BoundingBox& bounds,
            const FractureParams& params,
            vector<Vector3>& out_sites
        )
        {
            out_sites.clear();
            const uint32_t count = clamp(params.chunk_count, 2u, 96u);
            out_sites.reserve(count);

            const Vector3 mn     = bounds.GetMin();
            const Vector3 mx     = bounds.GetMax();
            const Vector3 size   = mx - mn;
            const Vector3 center = (mn + mx) * 0.5f;
            const float diag     = max(size.Length(), 0.05f);

            const Vector3 impact = params.has_impact_point
                ? Vector3(clamp(params.impact_local.x, mn.x, mx.x), clamp(params.impact_local.y, mn.y, mx.y), clamp(params.impact_local.z, mn.z, mx.z))
                : center;

            Rng rng(params.seed);

            if (params.pattern == FracturePattern::RadialGlass)
            {
                // Identify the two largest axes (the pane plane) and keep sites centered on the thin axis
                // so glass shards cut cleanly straight through the pane thickness.
                int thin_axis = 2;
                if (size.x <= size.y && size.x <= size.z)
                {
                    thin_axis = 0;
                }
                else if (size.y <= size.x && size.y <= size.z)
                {
                    thin_axis = 1;
                }

                const int axis_a = (thin_axis + 1) % 3;
                const int axis_b = (thin_axis + 2) % 3;
                auto comp = [](const Vector3& v, int idx) -> float
                {
                    return idx == 0 ? v.x : (idx == 1 ? v.y : v.z);
                };
                auto set_comp = [](Vector3& v, int idx, float val)
                {
                    if (idx == 0) v.x = val;
                    else if (idx == 1) v.y = val;
                    else v.z = val;
                };

                const float extent_a = max(comp(size, axis_a), 0.05f);
                const float extent_b = max(comp(size, axis_b), 0.05f);
                const float max_r    = sqrtf(extent_a * extent_a + extent_b * extent_b) * 0.65f;

                // Concentric spiderweb rings around impact point
                const uint32_t ring_count = clamp(count / 6u, 2u, 5u);
                const uint32_t per_ring   = max(4u, count / ring_count);

                // Place one central impact site
                Vector3 core = impact;
                set_comp(core, thin_axis, comp(center, thin_axis));
                out_sites.push_back(core);

                for (uint32_t r = 0; r < ring_count && out_sites.size() < count; ++r)
                {
                    const float frac   = static_cast<float>(r + 1) / static_cast<float>(ring_count);
                    const float radius = max_r * (frac * frac * 0.85f + 0.08f);
                    const float phase  = rng.Range(0.0f, 6.2831853f);

                    for (uint32_t k = 0; k < per_ring && out_sites.size() < count; ++k)
                    {
                        const float angle = phase + (6.2831853f * static_cast<float>(k) / static_cast<float>(per_ring)) + rng.Range(-0.22f, 0.22f);
                        const float rr    = radius * rng.Range(0.82f, 1.18f);

                        Vector3 s = impact;
                        set_comp(s, axis_a, clamp(comp(impact, axis_a) + cosf(angle) * rr, comp(mn, axis_a), comp(mx, axis_a)));
                        set_comp(s, axis_b, clamp(comp(impact, axis_b) + sinf(angle) * rr, comp(mn, axis_b), comp(mx, axis_b)));
                        set_comp(s, thin_axis, comp(center, thin_axis) + rng.Range(-0.02f, 0.02f) * comp(size, thin_axis));
                        out_sites.push_back(s);
                    }
                }
            }
            else if (params.pattern == FracturePattern::LayeredStone)
            {
                // Horizontal masonry strata with staggered vertical seams
                const uint32_t rows = clamp(static_cast<uint32_t>(roundf(sqrtf(static_cast<float>(count)) * max(0.6f, size.y / max(max(size.x, size.z), 0.1f)))), 2u, 8u);
                const uint32_t cols = max(2u, (count + rows - 1u) / rows);

                for (uint32_t r = 0; r < rows && out_sites.size() < count; ++r)
                {
                    const float y = mn.y + (static_cast<float>(r) + rng.Range(0.32f, 0.68f)) / static_cast<float>(rows) * size.y;
                    const float stagger = (r & 1u) ? 0.5f : 0.0f;
                    for (uint32_t c = 0; c < cols && out_sites.size() < count; ++c)
                    {
                        const float u = clamp((static_cast<float>(c) + stagger + rng.Range(0.18f, 0.82f)) / static_cast<float>(cols), 0.04f, 0.96f);
                        const float x = size.x >= size.z ? (mn.x + u * size.x) : rng.Range(mn.x + 0.1f * size.x, mx.x - 0.1f * size.x);
                        const float z = size.z >  size.x ? (mn.z + u * size.z) : rng.Range(mn.z + 0.1f * size.z, mx.z - 0.1f * size.z);
                        out_sites.emplace_back(x, y, z);
                    }
                }
            }
            else
            {
                // Uniform or SplinterWood: impact-biased radial cluster + stratified volume fill
                const uint32_t cluster_count = params.has_impact_point ? static_cast<uint32_t>(roundf(static_cast<float>(count) * clamp(params.impact_bias, 0.0f, 0.8f))) : 0u;
                for (uint32_t i = 0; i < cluster_count; ++i)
                {
                    const float theta = rng.Range(0.0f, 6.2831853f);
                    const float cos_p = rng.Range(-1.0f, 1.0f);
                    const float sin_p = sqrtf(max(0.0f, 1.0f - cos_p * cos_p));
                    const float r     = diag * 0.32f * powf(rng.NextFloat(), 0.7f);

                    Vector3 dir(sin_p * cosf(theta), cos_p, sin_p * sinf(theta));
                    Vector3 s = impact + dir * r;
                    s.x = clamp(s.x, mn.x + 0.02f * size.x, mx.x - 0.02f * size.x);
                    s.y = clamp(s.y, mn.y + 0.02f * size.y, mx.y - 0.02f * size.y);
                    s.z = clamp(s.z, mn.z + 0.02f * size.z, mx.z - 0.02f * size.z);
                    out_sites.push_back(s);
                }

                while (out_sites.size() < count)
                {
                    out_sites.emplace_back(
                        rng.Range(mn.x + 0.04f * size.x, mx.x - 0.04f * size.x),
                        rng.Range(mn.y + 0.04f * size.y, mx.y - 0.04f * size.y),
                        rng.Range(mn.z + 0.04f * size.z, mx.z - 0.04f * size.z)
                    );
                }
            }

            // Guarantee no two sites coincide
            for (size_t i = 0; i < out_sites.size(); ++i)
            {
                for (size_t j = i + 1; j < out_sites.size(); ++j)
                {
                    if ((out_sites[j] - out_sites[i]).LengthSquared() < 1.0e-5f)
                    {
                        out_sites[j] += Vector3(rng.Range(-0.02f, 0.02f), rng.Range(-0.02f, 0.02f), rng.Range(-0.02f, 0.02f));
                    }
                }
            }
        }

        // Computes signed volume and center of mass of a closed triangle mesh using tetrahedral decomposition.
        void compute_mass_properties(
            const vector<ClipTriangle>& triangles,
            float& out_volume,
            Vector3& out_centroid
        )
        {
            double vol_sum = 0.0;
            double cx = 0.0;
            double cy = 0.0;
            double cz = 0.0;

            for (const ClipTriangle& tri : triangles)
            {
                const Vector3& p0 = tri.v[0].pos;
                const Vector3& p1 = tri.v[1].pos;
                const Vector3& p2 = tri.v[2].pos;

                // Spartan outward winding has Cross(p1 - p0, p2 - p0) outward, so signed tet volume is +Dot(p0, Cross(p1, p2)) / 6
                const Vector3 c = Vector3::Cross(p1, p2);
                const double v6 = static_cast<double>(Vector3::Dot(p0, c));
                vol_sum += v6;
                cx += v6 * static_cast<double>(p0.x + p1.x + p2.x);
                cy += v6 * static_cast<double>(p0.y + p1.y + p2.y);
                cz += v6 * static_cast<double>(p0.z + p1.z + p2.z);
            }

            out_volume = static_cast<float>(fabs(vol_sum) / 6.0);
            if (fabs(vol_sum) > 1.0e-10)
            {
                const double inv = 1.0 / (4.0 * vol_sum);
                out_centroid = Vector3(static_cast<float>(cx * inv), static_cast<float>(cy * inv), static_cast<float>(cz * inv));
            }
            else
            {
                const BoundingBox box = compute_triangle_bounds(triangles);
                out_centroid = box.GetCenter();
            }
        }
    }

    bool Fracture(
        const vector<RHI_Vertex_PosTexNorTan>& source_vertices,
        const vector<uint32_t>& source_indices,
        const Vector3& entity_scale,
        const FractureParams& params,
        vector<FractureCell>& out_cells
    )
    {
        out_cells.clear();
        if (source_vertices.empty() || source_indices.size() < 3)
        {
            return false;
        }

        const Vector3 safe_scale(
            max(fabsf(entity_scale.x), 1.0e-3f),
            max(fabsf(entity_scale.y), 1.0e-3f),
            max(fabsf(entity_scale.z), 1.0e-3f)
        );
        const Vector3 inv_scale(1.0f / safe_scale.x, 1.0f / safe_scale.y, 1.0f / safe_scale.z);

        // Build metric-space source triangles
        vector<ClipTriangle> base_triangles;
        base_triangles.reserve(source_indices.size() / 3);
        for (size_t i = 0; i + 2 < source_indices.size(); i += 3)
        {
            const uint32_t i0 = source_indices[i];
            const uint32_t i1 = source_indices[i + 1];
            const uint32_t i2 = source_indices[i + 2];
            if (i0 >= source_vertices.size() || i1 >= source_vertices.size() || i2 >= source_vertices.size())
            {
                continue;
            }

            ClipTriangle tri;
            const uint32_t idx[3] = { i0, i1, i2 };
            for (int k = 0; k < 3; ++k)
            {
                const RHI_Vertex_PosTexNorTan& sv = source_vertices[idx[k]];
                const Vector3 p = sv.get_position();
                const Vector3 n = sv.get_normal();
                const Vector3 t = sv.get_tangent();

                tri.v[k].pos = Vector3(p.x * safe_scale.x, p.y * safe_scale.y, p.z * safe_scale.z);
                tri.v[k].uv  = sv.get_uv();
                Vector3 sn(n.x * inv_scale.x, n.y * inv_scale.y, n.z * inv_scale.z);
                tri.v[k].nor = sn.LengthSquared() > 1.0e-8f ? sn.Normalized() : Vector3::Up;
                Vector3 st(t.x * safe_scale.x, t.y * safe_scale.y, t.z * safe_scale.z);
                tri.v[k].tan = st.LengthSquared() > 1.0e-8f ? st.Normalized() : Vector3::Right;
            }
            base_triangles.push_back(tri);
        }

        if (base_triangles.empty())
        {
            return false;
        }

        const BoundingBox source_bounds = compute_triangle_bounds(base_triangles);
        const Vector3 source_size       = source_bounds.GetMax() - source_bounds.GetMin();

        // For SplinterWood, stretch bisector metric along the longest axis so cells become long splinters
        Vector3 metric_stretch = Vector3::One;
        if (params.pattern == FracturePattern::SplinterWood)
        {
            if (source_size.x >= source_size.y && source_size.x >= source_size.z)
            {
                metric_stretch = Vector3(0.28f, 1.0f, 1.0f);
            }
            else if (source_size.y >= source_size.x && source_size.y >= source_size.z)
            {
                metric_stretch = Vector3(1.0f, 0.28f, 1.0f);
            }
            else
            {
                metric_stretch = Vector3(1.0f, 1.0f, 0.28f);
            }
        }

        vector<Vector3> sites;
        generate_voronoi_sites(source_bounds, params, sites);
        const size_t site_count = sites.size();

        // Keep track of original site -> compacted output cell index so neighbor graph stays valid
        vector<int32_t> site_to_cell(site_count, -1);
        vector<vector<uint32_t>> raw_neighbors(site_count);
        vector<FractureCell> candidate_cells(site_count);
        vector<uint8_t> candidate_valid(site_count, 0);

        const float bottom_y   = source_bounds.GetMin().y;
        const float ground_tol = max(0.04f, source_size.y * 0.04f);

        auto build_cell_range = [&](uint32_t start, uint32_t end)
        {
            vector<ClipTriangle> cell_tris;
            vector<pair<float, size_t>> order;
            order.reserve(site_count);

            for (uint32_t i = start; i < end; ++i)
            {
                cell_tris = base_triangles;
                BoundingBox cell_bounds = source_bounds;

                // Sort neighbor sites by stretched squared distance to site i so nearest planes prune fastest
                order.clear();
                for (size_t j = 0; j < site_count; ++j)
                {
                    if (j == i)
                    {
                        continue;
                    }
                    const Vector3 diff = (sites[j] - sites[i]) * metric_stretch;
                    order.emplace_back(diff.LengthSquared(), j);
                }
                sort(order.begin(), order.end(), [](const auto& a, const auto& b)
                {
                    return a.first < b.first;
                });

                auto compute_max_stretched_r_sq = [&](const BoundingBox& b) -> float
                {
                    const Vector3 mn = b.GetMin();
                    const Vector3 mx = b.GetMax();
                    const float dx = max(fabsf(mn.x - sites[i].x), fabsf(mx.x - sites[i].x)) * metric_stretch.x;
                    const float dy = max(fabsf(mn.y - sites[i].y), fabsf(mx.y - sites[i].y)) * metric_stretch.y;
                    const float dz = max(fabsf(mn.z - sites[i].z), fabsf(mx.z - sites[i].z)) * metric_stretch.z;
                    return dx * dx + dy * dy + dz * dz;
                };

                float max_r_sq = compute_max_stretched_r_sq(cell_bounds);
                bool empty = false;
                for (const auto& [dist_sq, j] : order)
                {
                    if (dist_sq < 1.0e-10f)
                    {
                        continue;
                    }

                    // Exact half-space early-out: if the bisector plane is farther than the farthest corner of
                    // cell_bounds in stretched metric space, neither this site nor any farther site can cut the cell.
                    if (0.25f * dist_sq > max_r_sq + 1.0e-4f)
                    {
                        break;
                    }

                    // Weighted bisector plane between site i and site j:
                    // ((p - mid) * metric_stretch) . ((site_j - site_i) * metric_stretch) <= 0
                    const Vector3 mid  = (sites[i] + sites[j]) * 0.5f;
                    const Vector3 diff = sites[j] - sites[i];
                    Vector3 plane_n(
                        diff.x * metric_stretch.x * metric_stretch.x,
                        diff.y * metric_stretch.y * metric_stretch.y,
                        diff.z * metric_stretch.z * metric_stretch.z
                    );
                    const float n_len = plane_n.Length();
                    if (n_len < 1.0e-6f)
                    {
                        continue;
                    }
                    plane_n /= n_len;
                    const float plane_d = Vector3::Dot(plane_n, mid);

                    const bool cut = clip_mesh_by_plane(cell_tris, cell_bounds, plane_n, plane_d, params.interior_uv_scale, empty);
                    if (empty)
                    {
                        break;
                    }
                    if (cut)
                    {
                        raw_neighbors[i].push_back(static_cast<uint32_t>(j));
                        max_r_sq = compute_max_stretched_r_sq(cell_bounds);
                    }
                }

                if (empty || cell_tris.size() < 4)
                {
                    continue;
                }

                float volume = 0.0f;
                Vector3 centroid = Vector3::Zero;
                compute_mass_properties(cell_tris, volume, centroid);
                if (volume < params.min_volume)
                {
                    continue;
                }

                FractureCell& cell = candidate_cells[i];
                cell.centroid = centroid;
                cell.volume   = volume;
                cell.grounded = cell_bounds.GetMin().y <= bottom_y + ground_tol;
                cell.bounds   = BoundingBox(cell_bounds.GetMin() - centroid, cell_bounds.GetMax() - centroid);
                cell.vertices.reserve(cell_tris.size() * 3);
                cell.indices.reserve(cell_tris.size() * 3);

                uint32_t v_cursor = 0;
                for (const ClipTriangle& tri : cell_tris)
                {
                    for (int k = 0; k < 3; ++k)
                    {
                        cell.vertices.emplace_back(tri.v[k].pos - centroid, tri.v[k].uv, tri.v[k].nor, tri.v[k].tan);
                        cell.indices.push_back(v_cursor++);
                    }
                }

                candidate_valid[i] = 1;
            }
        };

        if (site_count >= 6 && ThreadPool::GetThreadCount() > 1)
        {
            ThreadPool::ParallelLoop(build_cell_range, static_cast<uint32_t>(site_count));
        }
        else
        {
            build_cell_range(0u, static_cast<uint32_t>(site_count));
        }

        out_cells.reserve(site_count);
        for (size_t i = 0; i < site_count; ++i)
        {
            if (candidate_valid[i])
            {
                site_to_cell[i] = static_cast<int32_t>(out_cells.size());
                out_cells.push_back(std::move(candidate_cells[i]));
            }
        }

        // Remap and symmetrize neighbor indices
        for (size_t i = 0; i < site_count; ++i)
        {
            const int32_t ci = site_to_cell[i];
            if (ci < 0)
            {
                continue;
            }
            for (uint32_t j : raw_neighbors[i])
            {
                const int32_t cj = site_to_cell[j];
                if (cj < 0 || cj == ci)
                {
                    continue;
                }
                auto& ni = out_cells[ci].neighbors;
                if (find(ni.begin(), ni.end(), static_cast<uint32_t>(cj)) == ni.end())
                {
                    ni.push_back(static_cast<uint32_t>(cj));
                }
                auto& nj = out_cells[cj].neighbors;
                if (find(nj.begin(), nj.end(), static_cast<uint32_t>(ci)) == nj.end())
                {
                    nj.push_back(static_cast<uint32_t>(ci));
                }
            }
        }

        return !out_cells.empty();
    }

    void FindUnsupportedCells(
        const vector<FractureCell>& cells,
        const vector<uint8_t>& detached,
        vector<uint32_t>& out_unsupported
    )
    {
        out_unsupported.clear();
        const size_t n = cells.size();
        if (n == 0 || detached.size() != n)
        {
            return;
        }

        vector<uint8_t> supported(n, 0);
        queue<uint32_t> frontier;

        for (size_t i = 0; i < n; ++i)
        {
            if (!detached[i] && cells[i].grounded)
            {
                supported[i] = 1;
                frontier.push(static_cast<uint32_t>(i));
            }
        }

        while (!frontier.empty())
        {
            const uint32_t u = frontier.front();
            frontier.pop();

            for (uint32_t v : cells[u].neighbors)
            {
                if (v < n && !detached[v] && !supported[v])
                {
                    supported[v] = 1;
                    frontier.push(v);
                }
            }
        }

        for (size_t i = 0; i < n; ++i)
        {
            if (!detached[i] && !supported[i])
            {
                out_unsupported.push_back(static_cast<uint32_t>(i));
            }
        }
    }
}
