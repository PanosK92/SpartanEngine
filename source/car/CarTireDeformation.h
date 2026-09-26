#pragma once
#include <algorithm>
#include <cmath>
#include <array>
#include <vector>
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

    // Sparse, quasi-static spring cage. The vehicle's load-bearing contact
    // spring supplies hub/road separation; this cage resolves the tire shape at
    // that separation without applying a second normal force to the chassis.
    // This is not a second vehicle solver or a puncture/tearing simulation.
    struct tire_cage
    {
        using V = spartan::math::Vector3;
        static constexpr int sectors = 64, lanes = 3, bands = 5;
        static constexpr int node_count = sectors * lanes * bands;
        struct beam { int a, b; V axis; float stiffness; bool pneumatic; };
        struct binding
        {
            uint16_t nodes[8];
            float weight[8], tangent_weight[8], bitangent_weight[8];
        };
        std::array<V, node_count> rest{}, position{}, displacement{};
        std::vector<beam> beams;
        struct face { int a, b, c; };
        std::vector<face> cavity_faces;
        float radius = 0, width = 0;
        static constexpr int contact_lanes = 9;
        struct contact_sample { V point; binding weights{}; float radial = 0; };
        std::array<contact_sample, sectors * contact_lanes> contact_samples{};
        bool has_contact_samples = false;
        static int index(int r, int x, int a) { return (r * lanes + x) * sectors + (a % sectors); }
        static bool pinned(int i) { return i < lanes * sectors; }

        void initialize(float r, float w)
        {
            radius = r; width = w; beams.clear(); cavity_faces.clear();
            contact_samples = {}; has_contact_samples = false;
            for (int band = 0; band < bands; ++band)
                for (int lane = 0; lane < lanes; ++lane)
                    for (int a = 0; a < sectors; ++a)
                    {
                        float angle = a * (spartan::math::pi_2 / sectors);
                        float radial = radius * (0.73f + 0.27f * band / (bands - 1));
                        rest[index(band, lane, a)] = V(width * (float(lane) / (lanes - 1) - 0.5f),
                            radial * std::cos(angle), radial * std::sin(angle));
                    }
            auto connect = [&](int a, int b, float stiffness, bool pneumatic)
            {
                beams.push_back({a, b, (rest[b] - rest[a]).Normalized(), stiffness, pneumatic});
            };
            for (int band = 1; band < bands; ++band)
                for (int lane = 0; lane < lanes; ++lane)
                    for (int a = 0; a < sectors; ++a)
                    {
                        const int i = index(band, lane, a);
                        connect(i, index(band - 1, lane, a), 1.0f, true);
                        connect(i, index(band, lane, a + 1), 2.0f, false);
                        // Diagonals resist shear; cross-tire beams couple the shoulders.
                        connect(i, index(band - 1, lane, a + 1), 0.35f, true);
                        connect(index(band - 1, lane, a), index(band, lane, a + 1), 0.35f, true);
                        if (lane + 1 < lanes) connect(i, index(band, lane + 1, a), 0.8f, false);
                    }
            // Closed pneumatic envelope, including the rigid inner rim surface.
            auto quad = [&](int a, int b, int c, int d, V outward)
            {
                if ((rest[b] - rest[a]).Cross(rest[c] - rest[a]).Dot(outward) < 0)
                    std::swap(b, d);
                cavity_faces.push_back({a, b, c});
                cavity_faces.push_back({a, c, d});
            };
            for (int a = 0; a < sectors; ++a)
            {
                for (int lane = 0; lane < lanes - 1; ++lane)
                    for (int band : {0, bands - 1})
                        quad(index(band, lane, a), index(band, lane + 1, a),
                            index(band, lane + 1, a + 1), index(band, lane, a + 1),
                            V(0, rest[index(band, lane, a)].y, rest[index(band, lane, a)].z) * (band ? 1.0f : -1.0f));
                for (int band = 0; band < bands - 1; ++band)
                    for (int lane : {0, lanes - 1})
                        quad(index(band, lane, a), index(band + 1, lane, a),
                            index(band + 1, lane, a + 1), index(band, lane, a + 1), V(lane ? 1.0f : -1.0f, 0, 0));
            }
            position = rest;
            displacement.fill(V(0.0f));
        }

        // The cage encloses the rubber; its outer corners are not necessarily
        // on the tire surface. Measure the crown at its axial sample locations
        // instead of letting those empty corners collide with the road.
        void include_contact_vertex(V p)
        {
            const float radial = std::sqrt(p.y * p.y + p.z * p.z);
            if (radial <= radius * 0.73f) return;
            float angle = std::atan2(p.z, p.y);
            if (angle < 0) angle += spartan::math::pi_2;
            const int sector = int(angle * sectors / spartan::math::pi_2 + 0.5f) % sectors;
            const int lane = std::clamp(int((p.x / width + 0.5f) * (contact_lanes - 1) + 0.5f), 0, contact_lanes - 1);
            auto& sample = contact_samples[lane * sectors + sector];
            if (radial > sample.radial)
            {
                sample.point = p; sample.radial = radial;
                sample.weights = bind(p, V(0.0f), V(0.0f));
                has_contact_samples = true;
            }
        }

        void project_contacts(V normal, float distance)
        {
            for (const auto& sample : contact_samples)
            {
                if (sample.radial == 0) continue;
                V p = sample.point;
                float inverse_mass = 0;
                for (int k = 0; k < 8; ++k)
                {
                    const int node = sample.weights.nodes[k];
                    const float weight = sample.weights.weight[k];
                    p += (position[node] - rest[node]) * weight;
                    if (!pinned(node)) inverse_mass += weight * weight;
                }
                const float penetration = -(normal.Dot(p) + distance);
                if (penetration <= 0 || inverse_mass < 1e-8f) continue;
                // Embedded surface contact: distribute its correction through
                // the binding Jacobian, preserving the rim's pinned nodes.
                for (int k = 0; k < 8; ++k)
                    if (!pinned(sample.weights.nodes[k]))
                        position[sample.weights.nodes[k]] += normal * (penetration * sample.weights.weight[k] / inverse_mass);
            }
        }

        // Reduced membrane strip, loaded uniformly by air pressure. A parabola
        // has T = q*h*h/(8*b), where T is cord tension, h its chord and b its
        // midpoint bow. Match that tension to EA*(arc_length/rest_length - 1).
        // Pressure selects the outward tensile equilibrium; no displacement
        // clamp or artist-authored bulge curve selects a buckling direction.
        static float membrane_bow(float chord, float rest_length, float axial_rigidity, float line_pressure)
        {
            if (line_pressure <= 0 || chord <= 1e-6f) return 0;
            auto residual = [&](float bow)
            {
                const float slope = 4.0f * bow / chord;
                const float arc = slope < 1e-3f ? chord * (1.0f + slope * slope / 6.0f) :
                    0.5f * chord * (std::sqrt(1.0f + slope * slope) + std::asinh(slope) / slope);
                const float tension = axial_rigidity * std::max(arc / rest_length - 1.0f, 0.0f);
                return 8.0f * tension * bow / (chord * chord) - line_pressure;
            };
            float lo = 0, hi = std::max(chord, rest_length);
            while (residual(hi) < 0) hi *= 2;
            for (int i = 0; i < 24; ++i)
            {
                const float mid = (lo + hi) * 0.5f;
                if (residual(mid) < 0) lo = mid; else hi = mid;
            }
            return (lo + hi) * 0.5f;
        }

        void support_sidewalls(V normal, float distance, float pressure_bar, float reference_stiffness, float ambient_bar, int iterations = 6)
        {
            const float span = radius * 0.27f;
            const float pitch = spartan::math::pi_2 * radius * 0.865f / sectors;
            // Effective strip compliance estimated from the calibrated radial
            // spring, distributed by strip width. Tire-specific cord data would
            // improve this reduced model; this is not a carcass FE simulation.
            const float ea = std::max(reference_stiffness, 1.0f) * pitch / width * span;
            const float base_pressure = std::max(pressure_bar, 0.0f) * 100000.0f;
            const float base_bow = membrane_bow(span, span, ea, base_pressure * pitch);
            auto volume = [&](const std::array<V, node_count>& points)
            {
                auto physical = [&](int i)
                {
                    V p = points[i];
                    const int band = i / (lanes * sectors), lane = (i / sectors) % lanes;
                    const float t = float(band) / (bands - 1);
                    if (lane != 1) p.x += (lane ? 1.0f : -1.0f) * 4.0f * base_bow * t * (1.0f - t);
                    return p;
                };
                double result = 0;
                for (const face& f : cavity_faces)
                    result += physical(f.a).Dot(physical(f.b).Cross(physical(f.c))) / 6.0;
                return static_cast<float>(result);
            };
            const float rest_volume = volume(rest);
            float pressure = base_pressure;
            for (int iteration = 0; iteration < iterations; ++iteration)
            {
                for (int lane : {0, lanes - 1})
                    for (int a = 0; a < sectors; ++a)
                    {
                        const V bead = position[index(0, lane, a)];
                        const V shoulder = position[index(bands - 1, lane, a)];
                        const V chord = shoulder - bead;
                        const float length = chord.Length();
                        const V axis(lane ? 1.0f : -1.0f, 0, 0);
                        const V outward = (axis - chord * (axis.Dot(chord) / std::max(chord.LengthSquared(), 1e-12f))).Normalized();
                        const float bow = membrane_bow(length, span, ea, pressure * pitch);
                        for (int band = 1; band < bands - 1; ++band)
                        {
                            const float t = float(band) / (bands - 1);
                            // The imported mesh is already inflated. Subtract
                            // its unloaded equilibrium, rather than inflating it twice.
                            V p = bead + chord * t + (outward * bow - axis * base_bow) * (4.0f * t * (1.0f - t));
                            if (!has_contact_samples) p += normal * std::max(-(normal.Dot(p) + distance), 0.0f);
                            position[index(band, lane, a)] = p;
                        }
                    }
                // Fixed-temperature gas law over this quasi-static shape solve.
                // pressure_bar already includes the simulated core temperature.
                const float current_volume = volume(position);
                if (current_volume <= 0) break;
                const float gas_pressure = std::max((base_pressure + ambient_bar * 100000.0f) *
                    rest_volume / current_volume - ambient_bar * 100000.0f, 0.0f);
                pressure = 0.5f * (pressure + gas_pressure);
            }
        }

        void solve(V normal, float distance, float stiffness_ratio, bool grounded,
                   float pressure_bar, float reference_stiffness, float ambient_bar)
        {
            position = rest;
            displacement.fill(V(0.0f));
            if (!grounded || !prepare_tire_contact_plane(normal, distance, radius)) return;
            const float pneumatic = std::clamp(stiffness_ratio, 0.05f, 8.0f);
            // Spring coefficients and the Jacobi diagonal do not change during
            // a solve. Compute them once, not for every relaxation iteration.
            std::array<float, node_count> step{};
            step.fill(0.08f * pneumatic);
            std::array<float, (bands - 1) * sectors * (lanes * 4 + lanes - 1)> coefficients;
            for (size_t j = 0; j < beams.size(); ++j)
            {
                const beam& b = beams[j];
                const float k = b.stiffness * (b.pneumatic ? pneumatic : 1.0f);
                coefficients[j] = k;
                step[b.a] += k;
                step[b.b] += k;
            }
            for (float& value : step) value = 0.7f / value;
            std::array<bool, node_count> moving{};
            for (int iteration = 0; iteration < 32; ++iteration)
            {
                std::array<V, node_count> force{};
                for (int i = 0; i < node_count; ++i)
                {
                    // Weak bending restraint keeps unloaded arcs circular.
                    force[i] = (rest[i] - position[i]) * (0.08f * pneumatic);
                }
                for (size_t j = 0; j < beams.size(); ++j)
                {
                    const beam& b = beams[j];
                    // Unloaded arcs remain exactly at rest; ignore sub-10 nm numerical motion.
                    if (!moving[b.a] && !moving[b.b]) continue;
                    // Springs act along their rest axis. Length springs buckle once the
                    // flattened patch compresses the circumferential beams, and the solve
                    // then jumps between buckled shapes as pressure or load changes.
                    const V stretch = (position[b.b] - rest[b.b]) - (position[b.a] - rest[b.a]);
                    V f = b.axis * (coefficients[j] * stretch.Dot(b.axis));
                    force[b.a] += f; force[b.b] -= f;
                }
                for (int i = lanes * sectors; i < node_count; ++i)
                {
                    position[i] += force[i] * step[i];
                    if (!has_contact_samples) position[i] += normal * std::max(-(normal.Dot(position[i]) + distance), 0.0f);
                    moving[i] = (position[i] - rest[i]).LengthSquared() > 1e-16f;
                }
                // Couple the supported membrane back to the tread cage during
                // relaxation, so unsupported sidewall buckling cannot steer the shoulders.
                if (iteration % 4 == 3)
                {
                    support_sidewalls(normal, distance, pressure_bar, reference_stiffness, ambient_bar, 1);
                    for (int i = lanes * sectors; i < node_count; ++i)
                        moving[i] = (position[i] - rest[i]).LengthSquared() > 1e-16f;
                }
                if (has_contact_samples)
                {
                    project_contacts(normal, distance);
                    for (int i = lanes * sectors; i < node_count; ++i)
                        moving[i] = (position[i] - rest[i]).LengthSquared() > 1e-16f;
                }
            }
            support_sidewalls(normal, distance, pressure_bar, reference_stiffness, ambient_bar);
            if (has_contact_samples) project_contacts(normal, distance);
            for (int i = 0; i < node_count; ++i) displacement[i] = position[i] - rest[i];
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

        void skin(const binding& b, V& p, V& tangent, V& bitangent) const
        {
            for (int i = 0; i < 8; ++i)
            {
                const V& d = displacement[b.nodes[i]];
                p += d * b.weight[i];
                tangent += d * b.tangent_weight[i];
                bitangent += d * b.bitangent_weight[i];
            }
        }
    };
}
