#pragma once
#include <algorithm>
#include <cmath>
#include <array>
#include <cstdint>
#include "../math/Vector3.h"

namespace car
{
    // Plane covector and correction direction may be in a scaled mesh frame.
    // Their dot product is one; separation and correction stay in world metres.
    inline void project_tire_vertex(spartan::math::Vector3& p, spartan::math::Vector3& tangent,
        spartan::math::Vector3& bitangent, const spartan::math::Vector3& normal, float distance,
        const spartan::math::Vector3& direction)
    {
        const float separation = normal.Dot(p) + distance;
        if (separation >= 0) return;
        p -= direction * separation;
        tangent -= direction * normal.Dot(tangent);
        bitangent -= direction * normal.Dot(bitangent);
    }

    // First-order pneumatic spring: rubber/belt stiffness in parallel with an
    // inflation-dependent contribution. K_ref is measured at p_ref (gauge bar).
    // The carcass fraction needs tire-specific load/deflection measurements;
    // 0.35 is an estimate, not a finite-element tire construction model.
    inline float tire_radial_stiffness(float reference_stiffness, float pressure,
                                      float reference_pressure, float carcass_fraction)
    {
        const float carcass = std::clamp(carcass_fraction, 0.05f, 1.0f);
        return std::max(reference_stiffness, 1.0f) *
            (carcass + (1.0f - carcass) * std::max(pressure, 0.0f) / std::max(reference_pressure, 0.1f));
    }

    inline float tire_compression_limit(float radius)
    {
        return std::min(radius * 0.25f, 0.05f);
    }

    // The cage and its skinned surface must use the same support plane.
    // In particular, a deep contact must not bypass the rim-supported travel
    // limit in a second projection after the cage has already been solved.
    inline bool prepare_tire_contact_plane(spartan::math::Vector3& normal, float& distance, float radius)
    {
        const float length_squared = normal.LengthSquared();
        if (!normal.IsFinite() || !std::isfinite(length_squared) || length_squared <= 1e-12f ||
            !std::isfinite(distance)) return false;
        const float inverse_length = 1.0f / std::sqrt(length_squared);
        normal *= inverse_length;
        distance = std::max(distance * inverse_length, radius - tire_compression_limit(radius));
        return true;
    }
    // A collapsed sidewall progressively transfers load toward the rigid rim.
    // This second spring prevents low inflation from exhausting the bounded
    // contact travel and allowing the wheel to fall through its support plane.
    inline float tire_spring_force(float deflection, float stiffness, float reference_stiffness, float radius)
    {
        const float onset = tire_compression_limit(radius) * 0.7f;
        return stiffness * deflection + 4.0f * reference_stiffness * std::max(deflection - onset, 0.0f);
    }
    inline float tire_spring_deflection(float load, float stiffness, float reference_stiffness, float radius)
    {
        const float onset = tire_compression_limit(radius) * 0.7f;
        const float bottom_out = 4.0f * reference_stiffness;
        const float deflection = load <= stiffness * onset ? load / stiffness :
            (load + bottom_out * onset) / (stiffness + bottom_out);
        return std::clamp(deflection, 0.0f, tire_compression_limit(radius));
    }

    // Visual lattice in the rotating wheel frame. The contact plane supplies
    // tread compression, and the shortened shoulder-to-bead span bows outward.
    struct tire_cage
    {
        using V = spartan::math::Vector3;
        static constexpr int sectors = 64, lanes = 3, bands = 5;
        static constexpr int node_count = sectors * lanes * bands;
        struct binding
        {
            uint16_t nodes[8];
            float weight[8], tangent_weight[8], bitangent_weight[8];
        };
        std::array<V, node_count> displacement{};
        float radius = 0, width = 0;
        static constexpr int contact_lanes = 9;
        struct contact_sample { V point; float radial = 0; };
        std::array<contact_sample, sectors * contact_lanes> contact_samples{};
        std::array<V, sectors> radial_axis{};
        std::array<bool, sectors> active_sectors{};
        static int index(int r, int x, int a) { return (r * lanes + x) * sectors + (a % sectors); }

        void initialize(float r, float w)
        {
            radius = r;
            width = w;
            contact_samples = {};
            for (int a = 0; a < sectors; ++a)
            {
                const float angle = a * (spartan::math::pi_2 / sectors);
                radial_axis[a] = V(0, std::cos(angle), std::sin(angle));
            }
        }

        // Measure the imported crown once. Rounded shoulders and camber can
        // put the first contacting piece of tread between two lattice lanes.
        void include_contact_vertex(V p)
        {
            const float radial = std::sqrt(p.y * p.y + p.z * p.z);
            if (radial <= radius * 0.73f) return;
            float angle = std::atan2(p.z, p.y);
            if (angle < 0) angle += spartan::math::pi_2;
            const int sector = int(angle * sectors / spartan::math::pi_2 + 0.5f) % sectors;
            const int lane = std::clamp(int((p.x / width + 0.5f) * (contact_lanes - 1) + 0.5f), 0, contact_lanes - 1);
            auto& sample = contact_samples[lane * sectors + sector];
            if (radial > sample.radial) sample = {p, radial};
        }

        // The caller normalizes and travel-limits the plane once for both
        // lattice deformation and the final tread-vertex projection.
        void solve(V normal, float distance, bool grounded, float pressure_bar, float reference_stiffness)
        {
            displacement.fill(V(0.0f));
            active_sectors.fill(false);
            if (!grounded) return;
            const float span = radius * 0.27f;
            // Small-slope parabolic membrane under inflation at rest.
            const float unloaded_bow = span * std::cbrt(3.0f / 64.0f * std::max(pressure_bar, 0.0f) *
                100000.0f * width / std::max(reference_stiffness, 1.0f));
            for (int a = 0; a < sectors; ++a)
            {
                float depth[lanes] = {};
                bool sampled[lanes] = {};
                for (int j = 0; j < contact_lanes; ++j)
                {
                    const auto& sample = contact_samples[j * sectors + a];
                    if (sample.radial == 0) continue;
                    const int lane = (j + 2) / 4;
                    sampled[lane] = true;
                    depth[lane] = std::max(depth[lane], -normal.Dot(sample.point) - distance);
                }
                for (int lane = 0; lane < lanes; ++lane)
                {
                    if (!sampled[lane])
                    {
                        const V tread = radial_axis[a] * radius + V((lane - 1) * width * 0.5f, 0, 0);
                        depth[lane] = std::max(0.0f, -normal.Dot(tread) - distance);
                    }
                    if (depth[lane] <= 0) continue;
                    active_sectors[a] = true;
                    V lift = normal * depth[lane];
                    // Camber shears the free tread sideways relative to the pinned bead.
                    lift.x += normal.x * depth[lane] * 1.5f;
                    const V chord = radial_axis[a] * span + lift;
                    // Preserve the approximate sidewall arc length as its chord
                    // shortens. Subtract the bow already baked into the rubber.
                    const float slack = std::max(0.0f, span * span - chord.LengthSquared());
                    V bow(0.0f);
                    if (lane != 1)
                    {
                        const V axis(float(lane - 1), 0, 0);
                        const V outward = (axis - chord * (axis.Dot(chord) / std::max(chord.LengthSquared(), 1e-12f))).Normalized();
                        bow = outward * std::sqrt(unloaded_bow * unloaded_bow + 0.1875f * slack) - axis * unloaded_bow;
                    }
                    for (int band = 1; band < bands; ++band)
                    {
                        const float t = float(band) / (bands - 1);
                        displacement[index(band, lane, a)] = lift * t + bow * (4.0f * t * (1.0f - t));
                    }
                }
            }
        }

        // Bind once. Per-frame mesh work is only weighted skinning, never beam
        // integration or ground queries per render vertex. Derivative weights
        // deform the tangent frame too, preserving tread normal-map lighting.
        binding bind(V p, V tangent, V bitangent) const
        {
            const float radial = std::max(std::sqrt(p.y * p.y + p.z * p.z), 1e-6f);
            const float raw_x = (p.x / width + 0.5f) * (lanes - 1);
            const float raw_r = (radial / radius - 0.73f) / 0.27f * (bands - 1);
            const float gx = std::clamp(raw_x, 0.0f, float(lanes - 1));
            const float gr = std::clamp(raw_r, 0.0f, float(bands - 1));
            float ga = std::atan2(p.z, p.y) * (sectors / spartan::math::pi_2);
            if (ga < 0) ga += sectors;
            const int x = std::min(int(gx), lanes - 2), r = std::min(int(gr), bands - 2), a = int(ga) % sectors;
            const float u = gx - x, v = gr - r, t = ga - std::floor(ga);
            V dx(raw_x >= 0 && raw_x <= lanes - 1 ? (lanes - 1) / width : 0, 0, 0);
            V dr = V(0, p.y / radial, p.z / radial) *
                (raw_r >= 0 && raw_r <= bands - 1 ? (bands - 1) / (radius * 0.27f) : 0);
            V da = V(0, -p.z, p.y) * (sectors / (spartan::math::pi_2 * radial * radial));
            binding result{};
            int n = 0;
            for (int ir = 0; ir < 2; ++ir)
                for (int ix = 0; ix < 2; ++ix)
                    for (int ia = 0; ia < 2; ++ia, ++n)
                    {
                        float wr = ir ? v : 1-v, wx = ix ? u : 1-u, wa = ia ? t : 1-t;
                        result.nodes[n] = static_cast<uint16_t>(index(r + ir, x + ix, a + ia));
                        result.weight[n] = wr * wx * wa;
                        V gradient = dr * ((ir ? 1.0f : -1.0f) * wx * wa) +
                            dx * ((ix ? 1.0f : -1.0f) * wr * wa) + da * ((ia ? 1.0f : -1.0f) * wr * wx);
                        result.tangent_weight[n] = gradient.Dot(tangent);
                        result.bitangent_weight[n] = gradient.Dot(bitangent);
                    }
            return result;
        }

        static void skin(const std::array<V, node_count>& local_displacement, const binding& b,
                         V& p, V& tangent, V& bitangent)
        {
            __m128 position = p.Simd(), du = tangent.Simd(), dv = bitangent.Simd();
            for (int i = 0; i < 8; ++i)
            {
                const __m128 offset = local_displacement[b.nodes[i]].Simd();
                position = _mm_fmadd_ps(offset, _mm_set1_ps(b.weight[i]), position);
                du = _mm_fmadd_ps(offset, _mm_set1_ps(b.tangent_weight[i]), du);
                dv = _mm_fmadd_ps(offset, _mm_set1_ps(b.bitangent_weight[i]), dv);
            }
            p = V(position);
            tangent = V(du);
            bitangent = V(dv);
        }
    };
}
