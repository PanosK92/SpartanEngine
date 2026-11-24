/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

//= INCLUDES ======================
#include "pch.h"
#include "TreeGenerator.h"
#include "Mesh.h"
#include "GeometryProcessing.h"
#include <algorithm>
#include <cmath>
//=================================

using namespace std;
using namespace spartan::math;

namespace spartan::trees
{
    namespace
    {
        constexpr float tau = 6.28318530718f;

        struct tree_random
        {
            uint64_t state = 0;

            explicit tree_random(const uint64_t seed) : state(seed ? seed : 0x853c49e6748fea9bull) {}

            uint32_t next_uint()
            {
                // xorshift64*, plenty for placement and cheap enough to draw per card
                state ^= state >> 12;
                state ^= state << 25;
                state ^= state >> 27;
                return static_cast<uint32_t>((state * 0x2545f4914f6cdd1dull) >> 32);
            }

            float unit()                        { return static_cast<float>(next_uint() >> 8) / 16777216.0f; }
            float signed_unit()                 { return unit() * 2.0f - 1.0f; }
            float range(const float a, const float b) { return a + (b - a) * unit(); }

            Vector3 direction()
            {
                const float z     = signed_unit();
                const float angle = unit() * tau;
                const float r     = sqrt(max(0.0f, 1.0f - z * z));
                return Vector3(r * cos(angle), z, r * sin(angle));
            }
        };

        uint64_t hash_text(const string& text)
        {
            uint64_t hash = 0xcbf29ce484222325ull;
            for (const char character : text)
            {
                hash ^= static_cast<uint8_t>(character);
                hash *= 0x100000001b3ull;
            }
            return hash;
        }

        // 0 maps to low, 0.5 to one and 1 to high, every control reads as the species at its centre
        float around(const float value, const float low, const float high)
        {
            const float v = clamp(value, 0.0f, 1.0f);
            return v < 0.5f ? low + (1.0f - low) * (v / 0.5f) : 1.0f + (high - 1.0f) * ((v - 0.5f) / 0.5f);
        }

        Vector3 rotate(const Vector3& v, const Vector3& axis, const float angle)
        {
            const float c = cos(angle);
            const float s = sin(angle);
            return v * c + Vector3::Cross(axis, v) * s + axis * (Vector3::Dot(axis, v) * (1.0f - c));
        }

        Vector3 any_perpendicular(const Vector3& v)
        {
            const Vector3 reference = abs(v.y) < 0.95f ? Vector3::Up : Vector3::Right;
            return Vector3::Cross(v, reference).Normalized();
        }

        // weber and penn's shape ratio, 1 at the crown base and 0 at the top of the tree
        float shape_ratio(const crown_shape shape, const float ratio)
        {
            const float r = clamp(ratio, 0.0f, 1.0f);
            switch (shape)
            {
                case crown_shape::conical:             return 0.2f + 0.8f * r;
                case crown_shape::spherical:           return 0.2f + 0.8f * sin(3.14159265f * r);
                case crown_shape::hemispherical:       return 0.2f + 0.8f * sin(0.5f * 3.14159265f * r);
                case crown_shape::cylindrical:         return 1.0f;
                case crown_shape::tapered_cylindrical: return 0.5f + 0.5f * r;
                case crown_shape::flame:               return r <= 0.7f ? r / 0.7f : (1.0f - r) / 0.3f;
                case crown_shape::inverse_conical:     return 1.0f - 0.8f * r;
                case crown_shape::tend_flame:          return r <= 0.7f ? 0.5f + 0.5f * r / 0.7f : 0.5f + 0.5f * (1.0f - r) / 0.3f;
                case crown_shape::umbrella:            return 0.3f + 0.7f * sqrt(1.0f - r);
                case crown_shape::vase:                return 0.45f + 0.55f * (1.0f - r);
            }
            return 1.0f;
        }

        struct grow_settings
        {
            float length       = 1.0f;
            float radius_base  = 0.1f;
            float radius_tip   = 0.01f;
            uint32_t segments  = 6;
            float curve        = 0.0f; // degrees over the length
            float curve_var    = 0.0f;
            float gravity      = 0.0f;
            float gnarl        = 0.0f;
            bool flare         = false;
            bool split_base    = false;
            bool fork_collar   = false;
            Vector3 start_dir  = Vector3::Zero; // optional initial tangent that blends smoothly into direction over the first two segments
        };

        stem grow(const Vector3& origin, Vector3 direction, const grow_settings& settings, tree_random& rng)
        {
            stem result;
            result.length      = settings.length;
            result.flare       = settings.flare;
            result.split_base  = settings.split_base;
            result.fork_collar = settings.fork_collar;
            result.points.push_back(origin);
            result.radii.push_back(settings.radius_base);

            const uint32_t segments = max(2u, settings.segments);
            const float curve_step  = settings.curve * deg_to_rad / static_cast<float>(segments);
            const float var_step    = settings.curve_var * deg_to_rad / sqrt(static_cast<float>(segments));
            const bool has_start    = settings.start_dir.LengthSquared() > 1e-6f;
            const Vector3 start_dir = has_start ? settings.start_dir.Normalized() : direction;

            // a random plane for the variation, the curve itself always bends toward the sky (or away from it)
            Vector3 position  = origin;
            float t_previous  = 0.0f;
            for (uint32_t i = 1; i <= segments; i++)
            {
                // a trunk packs its rings near the ground, that is where the flare changes the radius
                const float linear = static_cast<float>(i) / static_cast<float>(segments);
                const float t      = settings.flare ? pow(linear, 1.25f) : linear;
                const float step   = (t - t_previous) * settings.length;
                t_previous         = t;

                Vector3 bend_axis = Vector3::Cross(direction, Vector3::Up);
                if (bend_axis.LengthSquared() < 1e-6f)
                {
                    bend_axis = any_perpendicular(direction);
                }
                bend_axis = bend_axis.Normalized();
                direction = rotate(direction, bend_axis, curve_step + var_step * rng.signed_unit());

                if (settings.gnarl > 0.0f)
                {
                    const Vector3 axis = Vector3::Cross(direction, rng.direction());
                    if (axis.LengthSquared() > 1e-6f)
                    {
                        direction = rotate(direction, axis.Normalized(), settings.gnarl * 0.30f * rng.signed_unit());
                    }
                }

                if (settings.gravity > 0.0f)
                {
                    direction = (direction - Vector3::Up * (settings.gravity * 2.4f * step / max(settings.length, 0.5f) * linear)).Normalized();
                    if (direction.y < -0.985f)
                    {
                        direction = (direction + any_perpendicular(direction) * 0.15f).Normalized();
                    }
                }

                // smoothly steer forked stems out of the vertical crotch instead of snapping at a sharp V corner
                Vector3 step_dir = direction;
                if (has_start && linear < 0.38f)
                {
                    const float blend = clamp(linear / 0.38f, 0.0f, 1.0f);
                    const float smooth_blend = blend * blend * (3.0f - 2.0f * blend);
                    step_dir = Vector3::Lerp(start_dir, direction, smooth_blend).Normalized();
                }

                position += step_dir * step;
                result.points.push_back(position);
                const float r = settings.radius_base + (settings.radius_tip - settings.radius_base) * pow(t, 0.85f);
                result.radii.push_back(r);
            }
            return result;
        }

        // position, tangent and radius at a fraction of the arc length
        void sample(const stem& s, const float t, Vector3& position, Vector3& tangent, float& radius)
        {
            const size_t count = s.points.size();
            float total = 0.0f;
            vector<float> lengths(count, 0.0f);
            for (size_t i = 1; i < count; i++)
            {
                total     += (s.points[i] - s.points[i - 1]).Length();
                lengths[i] = total;
            }

            const float target = clamp(t, 0.0f, 1.0f) * total;
            size_t segment = 1;
            while (segment < count - 1 && lengths[segment] < target)
            {
                segment++;
            }
            const float span  = max(lengths[segment] - lengths[segment - 1], 1e-6f);
            const float local = clamp((target - lengths[segment - 1]) / span, 0.0f, 1.0f);
            position = Vector3::Lerp(s.points[segment - 1], s.points[segment], local);
            tangent  = (s.points[segment] - s.points[segment - 1]).Normalized();
            radius   = s.radii[segment - 1] + (s.radii[segment] - s.radii[segment - 1]) * local;
        }

        Vector3 child_direction(const Vector3& tangent, const float rotation, const float down, const bool planar, const float planar_sign)
        {
            Vector3 perpendicular;
            if (planar)
            {
                // the horizontal side of the parent, sprays stay flat the way spruce and fir hold them
                perpendicular = Vector3::Cross(tangent, Vector3::Up);
                if (perpendicular.LengthSquared() < 1e-6f)
                {
                    perpendicular = any_perpendicular(tangent);
                }
                perpendicular = perpendicular.Normalized() * planar_sign;
            }
            else
            {
                Vector3 reference = Vector3::Cross(tangent, Vector3::Up);
                if (reference.LengthSquared() < 1e-6f)
                {
                    reference = any_perpendicular(tangent);
                }
                perpendicular = rotate(reference.Normalized(), tangent, rotation);
            }
            return (tangent * cos(down) + perpendicular * sin(down)).Normalized();
        }

        struct bark_detail
        {
            float sides_scale  = 1.0f;
            uint32_t max_sides = 12;
            float min_radius   = 0.008f;
            uint32_t max_level = 4;
            float angle        = 5.0f * deg_to_rad; // rings are only kept where the stem turns at least this much
            float trunk_only_radius_ratio = 0.0f;  // drops stems thinner than this share of the trunk
        };

        uint32_t sides_for(const float radius, const bark_detail& detail, const bool trunk)
        {
            const float base   = 2.5f + 14.0f * sqrt(max(radius, 0.0f));
            const uint32_t cap = trunk ? detail.max_sides : max(3u, detail.max_sides - 2);
            return clamp(static_cast<uint32_t>(round(base * detail.sides_scale)), 3u, cap);
        }

        void mesh_stem(const stem& s, const species& sp, const bark_detail& detail, geometry& output)
        {
            const size_t count = s.points.size();
            if (count < 2)
            {
                return;
            }

            // keep the rings where the stem turns, thins, flares or spans a visible trunk section
            vector<size_t> kept = { 0 };
            Vector3 last_direction = (s.points[1] - s.points[0]).Normalized();
            float last_radius      = s.radii[0];
            float span_since_kept  = 0.0f;
            for (size_t i = 1; i + 1 < count; i++)
            {
                span_since_kept        += (s.points[i] - s.points[i - 1]).Length();
                const Vector3 direction = (s.points[i + 1] - s.points[i]).Normalized();
                const float turn        = acos(clamp(Vector3::Dot(direction, last_direction), -1.0f, 1.0f));
                const float thinning    = abs(1.0f - s.radii[i] / max(last_radius, 1e-5f));
                const bool flare_ring   = (s.flare && i <= 6) || (s.split_base && i + 3 >= count) || (s.fork_collar && i <= 3);
                const bool trunk_step   = s.level == 0 && span_since_kept >= 0.45f;
                if (turn >= detail.angle || thinning > 0.12f || flare_ring || trunk_step)
                {
                    kept.push_back(i);
                    last_direction  = direction;
                    last_radius     = s.radii[i];
                    span_since_kept = 0.0f;
                }
            }
            kept.push_back(count - 1);

            const bool trunk       = s.level == 0;
            const uint32_t sides   = sides_for(s.radii[0], detail, trunk);
            const float girth      = tau * s.radii[0];
            const float u_repeat   = max(1.0f, round(girth / max(sp.bark_scale, 0.05f)));
            const float tile_width = girth / u_repeat;
            const float twist      = sp.trunk_twist;
            const float base_r     = s.radii[0];

            // parallel transport keeps the seam from corkscrewing along a curved branch
            Vector3 tangent = (s.points[kept[1]] - s.points[kept[0]]).Normalized();
            Vector3 normal  = any_perpendicular(tangent);
            const uint32_t first_vertex = static_cast<uint32_t>(output.vertices.size());
            float v_length = 0.0f;

            for (size_t k = 0; k < kept.size(); k++)
            {
                const size_t i = kept[k];
                if (k > 0)
                {
                    v_length += (s.points[i] - s.points[kept[k - 1]]).Length();
                }

                Vector3 ring_tangent;
                if (k == 0)
                {
                    ring_tangent = (s.points[kept[1]] - s.points[kept[0]]).Normalized();
                }
                else if (k + 1 == kept.size())
                {
                    ring_tangent = (s.points[kept[k]] - s.points[kept[k - 1]]).Normalized();
                }
                else
                {
                    ring_tangent = (s.points[kept[k + 1]] - s.points[kept[k - 1]]).Normalized();
                }
                normal  = (normal - ring_tangent * Vector3::Dot(normal, ring_tangent)).Normalized();
                tangent = ring_tangent;
                const Vector3 binormal = Vector3::Cross(tangent, normal);

                const float height = s.points[i].y;
                // subtle, natural root flare decaying smoothly over ~1.8 trunk radii (never a steep cone skirt)
                const float flare  = s.flare ? min(sp.trunk_flare, 0.45f) * exp(-max(height, 0.0f) / max(base_r * 1.85f, 0.02f)) : 0.0f;
                const float v      = v_length / max(tile_width, 0.01f);
                for (uint32_t j = 0; j <= sides; j++)
                {
                    const float angle      = tau * static_cast<float>(j) / static_cast<float>(sides);
                    const Vector3 radial   = normal * cos(angle) + binormal * sin(angle);
                    // root buttresses: five organic lobes that merge smoothly into the ground without a conical skirt
                    const float lobes      = flare > 0.0f ? 1.0f + flare * (0.42f + 0.58f * pow(max(0.0f, cos(angle * 5.0f + 0.7f)), 2.2f)) : 1.0f;
                    const float fluting    = (trunk && sp.trunk_gnarl > 0.35f) ? 1.0f + (0.032f + 0.038f * sp.trunk_twist) * sin(angle * 5.0f + v * (0.9f + sp.trunk_twist * 1.1f)) : 1.0f;
                    const float radius     = s.radii[i] * lobes * fluting;
                    const Vector3 position = s.points[i] + radial * radius;
                    const Vector3 surface_tangent = Vector3::Cross(tangent, radial);
                    const float u = static_cast<float>(j) / static_cast<float>(sides) * u_repeat + twist * v * 0.055f;
                    output.vertices.emplace_back(position, Vector2(u, -v), radial, surface_tangent);
                }
            }

            const uint32_t ring = sides + 1;
            for (uint32_t k = 0; k + 1 < kept.size(); k++)
            {
                for (uint32_t j = 0; j < sides; j++)
                {
                    const uint32_t a = first_vertex + k * ring + j;
                    const uint32_t b = a + 1;
                    const uint32_t c = a + ring;
                    const uint32_t d = c + 1;
                    output.indices.insert(output.indices.end(), { a, b, d, a, d, c });
                }
            }

            // close the end: split crotch bases get a low rounded dome tucked inside the fork instead of a protruding spike
            const size_t last          = kept.back();
            const Vector3 tip_tangent  = (s.points[last] - s.points[kept[kept.size() - 2]]).Normalized();
            const float cap_lift       = s.split_base ? 0.18f : 1.15f;
            const float tip_v          = (v_length + s.radii[last] * cap_lift) / max(tile_width, 0.01f);
            const uint32_t tip         = static_cast<uint32_t>(output.vertices.size());
            output.vertices.emplace_back(s.points[last] + tip_tangent * (s.radii[last] * cap_lift), Vector2(0.5f * u_repeat, -tip_v), tip_tangent, normal);
            const uint32_t last_ring = first_vertex + static_cast<uint32_t>(kept.size() - 1) * ring;
            for (uint32_t j = 0; j < sides; j++)
            {
                output.indices.insert(output.indices.end(), { last_ring + j, last_ring + j + 1, tip });
            }
        }

        geometry mesh_bark(const result& tree, const species& sp, const bark_detail& detail)
        {
            geometry output;
            for (const stem& s : tree.stems)
            {
                if (s.pruned || s.level > detail.max_level || s.points.size() < 2)
                {
                    continue;
                }
                if (s.radii[0] < detail.min_radius && s.level > 0)
                {
                    continue;
                }
                if (s.level > 0 && s.radii[0] < tree.trunk_radius * detail.trunk_only_radius_ratio)
                {
                    continue;
                }
                mesh_stem(s, sp, detail, output);
            }
            return output;
        }

        void tile_rect(const uint32_t tile, float& u0, float& v0, float& u1, float& v1)
        {
            // a small inset keeps bilinear and mip filtering from bleeding the neighbour tile into the card edge
            const float size  = 1.0f / static_cast<float>(atlas_tiles);
            const float inset = size * 0.01f;
            const uint32_t tx = tile % atlas_tiles;
            const uint32_t ty = (tile / atlas_tiles) % atlas_tiles;
            u0 = tx * size + inset;
            u1 = (tx + 1) * size - inset;
            v0 = ty * size + inset;
            v1 = (ty + 1) * size - inset;
        }

        // Hierarchical 3-way botanical shading normal:
        // blends macro-crown ellipsoid normal + local bough-pillow outward normal + curved sprig normal.
        // Pure ellipsoid normals make a tree read as a single smooth egg; blending local bough-lobe
        // normals gives every individual bough cloud crisp sunlit crests and deep sculptural shadow pockets.
        Vector3 crown_lobe_normal(const Vector3& position, const Vector3& own, const Vector3& lobe_center, const float lobe_radius, const result& tree)
        {
            const Vector3 offset = position - tree.crown_center;
            const Vector3 radii  = Vector3::Max(tree.crown_radii, Vector3(0.3f, 0.3f, 0.3f));
            Vector3 radial       = Vector3(offset.x / (radii.x * radii.x), offset.y / (radii.y * radii.y), offset.z / (radii.z * radii.z));
            radial               = radial.LengthSquared() > 1e-8f ? radial.Normalized() : Vector3::Up;

            const Vector3 lobe_off = (position - lobe_center) / max(lobe_radius, 0.15f);
            Vector3 lobe_n         = lobe_off.LengthSquared() > 1e-6f ? lobe_off.Normalized() : radial;
            // Bias bough pillows gently skyward so sunlit upper crests read cleanly
            lobe_n                 = (lobe_n + Vector3::Up * 0.22f).Normalized();

            return (own * 0.28f + lobe_n * 0.34f + radial * 0.38f).Normalized();
        }

        void mesh_cards(const result& tree, geometry& output)
        {
            for (const leaf_card& card : tree.cards)
            {
                float u0, v0, u1, v1;
                tile_rect(card.tile, u0, v0, u1, v1);
                if (card.mirror_u)
                {
                    swap(u0, u1);
                }
                const float um = (u0 + u1) * 0.5f;

                // 3D cupped + longitudinally arched botanical sprig (6 verts / 4 tris, preserves virtual-geometry FoliageCardClusters)
                const float card_len   = card.direction.Length();
                const float half_w     = card.side.Length();
                const Vector3 side_dir = card.side / max(half_w, 1e-5f);
                const Vector3 fwd_dir  = card.direction / max(card_len, 1e-5f);
                const Vector3 lift     = card.normal * (half_w * card.fold);
                const Vector3 droop_v  = card.normal * (card_len * card.droop);
                const Vector3 base     = card.base;
                const Vector3 tip      = card.base + card.direction;

                // Edges curl gently down around the arched midrib so every sprig reads as a plump 3D cushion from all angles
                const Vector3 positions[6] =
                {
                    base - card.side - lift * 0.25f,
                    base + lift * 0.75f,
                    base + card.side - lift * 0.25f,
                    tip - card.side * 0.94f - droop_v * 1.15f,
                    tip + lift * 0.65f - droop_v * 0.35f,
                    tip + card.side * 0.94f - droop_v * 1.15f
                };
                const Vector2 uvs[6] =
                {
                    Vector2(u0, v1), Vector2(um, v1), Vector2(u1, v1),
                    Vector2(u0, v0), Vector2(um, v0), Vector2(u1, v0)
                };
                // Per-vertex curved dome normals so specular & diffuse wrap organically across each sprig
                const Vector3 own_normals[6] =
                {
                    (card.normal - side_dir * 0.42f - fwd_dir * 0.14f).Normalized(),
                    (card.normal - fwd_dir * 0.08f).Normalized(),
                    (card.normal + side_dir * 0.42f - fwd_dir * 0.14f).Normalized(),
                    (card.normal - side_dir * 0.46f + fwd_dir * 0.26f).Normalized(),
                    (card.normal + fwd_dir * 0.22f).Normalized(),
                    (card.normal + side_dir * 0.46f + fwd_dir * 0.26f).Normalized()
                };

                const Vector3 tangent = card.mirror_u ? (side_dir * -1.0f) : side_dir;
                const uint32_t first  = static_cast<uint32_t>(output.vertices.size());
                for (uint32_t i = 0; i < 6; i++)
                {
                    output.vertices.emplace_back(
                        positions[i],
                        uvs[i],
                        crown_lobe_normal(positions[i], own_normals[i], card.lobe_center, card.lobe_radius, tree),
                        tangent
                    );
                }
                output.indices.insert(output.indices.end(),
                {
                    first + 0, first + 3, first + 1,
                    first + 1, first + 3, first + 4,
                    first + 1, first + 4, first + 2,
                    first + 2, first + 4, first + 5
                });
            }

            for (const frond& f : tree.fronds)
            {
                float u0, v0, u1, v1;
                tile_rect(f.tile, u0, v0, u1, v1);
                const float um        = (u0 + u1) * 0.5f;
                const uint32_t rows   = static_cast<uint32_t>(f.points.size());
                const uint32_t first  = static_cast<uint32_t>(output.vertices.size());
                for (uint32_t i = 0; i < rows; i++)
                {
                    const float t          = static_cast<float>(i) / static_cast<float>(max(rows - 1, 1u));
                    const float v          = v1 + (v0 - v1) * t;
                    const Vector3 along    = i + 1 < rows ? (f.points[i + 1] - f.points[i]).Normalized() : (f.points[i] - f.points[i - 1]).Normalized();
                    const Vector3 up_local = Vector3::Cross(f.sides[i], along).Normalized();
                    // leaflets rise from the rachis in a shallow v, the way a date palm frond is keeled
                    const Vector3 lift     = up_local * (f.sides[i].Length() * 0.35f);
                    const Vector3 tangent  = f.sides[i].Normalized();
                    const Vector3 offset   = f.points[i] - tree.crown_center;
                    const Vector3 radii    = Vector3::Max(tree.crown_radii, Vector3(0.3f, 0.3f, 0.3f));
                    Vector3 radial         = Vector3(offset.x / (radii.x * radii.x), offset.y / (radii.y * radii.y), offset.z / (radii.z * radii.z));
                    radial                 = radial.LengthSquared() > 1e-8f ? radial.Normalized() : Vector3::Up;
                    const Vector3 normal   = (up_local * 0.7f + radial * 0.3f).Normalized();
                    output.vertices.emplace_back(f.points[i] - f.sides[i] + lift, Vector2(u0, v), normal, tangent);
                    output.vertices.emplace_back(f.points[i], Vector2(um, v), normal, tangent);
                    output.vertices.emplace_back(f.points[i] + f.sides[i] + lift, Vector2(u1, v), normal, tangent);
                }
                for (uint32_t i = 0; i + 1 < rows; i++)
                {
                    const uint32_t a = first + i * 3;
                    const uint32_t b = a + 3;
                    output.indices.insert(output.indices.end(),
                    {
                        a + 0, b + 0, a + 1,
                        a + 1, b + 0, b + 1,
                        a + 1, b + 1, a + 2,
                        a + 2, b + 1, b + 2
                    });
                }
            }
        }

        float quality_factor(const quality detail)
        {
            switch (detail)
            {
                case quality::mobile: return 0.35f;
                case quality::hero:   return 2.2f;
                default:              return 1.0f;
            }
        }

        bool is_needle_like(const species& s)
        {
            return s.leaf == leaf_shape::needle || s.leaf == leaf_shape::scale;
        }
    }

    uint32_t budget_for(const species& s, const quality detail)
    {
        return max(600u, static_cast<uint32_t>(static_cast<float>(s.budget) * quality_factor(detail)));
    }

    result generate(const species& s, const controls& c)
    {
        result tree;
        tree_random rng(hash_text(s.id) ^ (static_cast<uint64_t>(c.seed) * 0x9e3779b97f4a7c15ull));

        const float wild        = around(c.wildness, 0.35f, 2.2f);
        const float size        = around(c.age, 0.28f, 1.3f);
        const float thickness   = c.age < 0.5f ? pow(size, 1.3f) : 1.0f + (c.age - 0.5f) * 1.1f;
        const float branch_mult = around(c.branchiness, 0.5f, 1.65f);
        const float leaf_mult   = around(c.leafiness, 0.4f, 1.8f);
        const float gnarl       = s.trunk_gnarl * around(c.age, 0.6f, 1.7f) * wild;
        const bool bare         = c.time == season::bare && !s.evergreen;

        const float height = s.height * size * (1.0f + s.height_var * rng.signed_unit());
        const float base_r = s.height * s.trunk_radius * thickness;
        tree.height        = height;
        tree.trunk_radius  = base_r;
        tree.budget        = budget_for(s, c.detail);

        uint32_t levels_used = static_cast<uint32_t>(s.levels.size());
        if (c.age < 0.2f && levels_used > 1)
        {
            levels_used--;
        }

        // trunk, or the split into stems
        const float lean_azimuth = rng.unit() * tau;
        const float lean         = (s.trunk_lean + max(0.0f, wild - 1.0f) * 5.0f) * rng.range(0.4f, 1.0f) * deg_to_rad;
        const Vector3 lean_axis  = Vector3(cos(lean_azimuth), 0.0f, sin(lean_azimuth));
        const Vector3 trunk_dir  = rotate(Vector3::Up, lean_axis, lean);

        grow_settings trunk;
        trunk.radius_base = base_r;
        trunk.segments    = s.trunk_segments;
        trunk.curve_var   = 4.0f * wild;
        trunk.gnarl       = gnarl;
        trunk.flare       = true;

        const uint32_t stems = max(1u, s.stems);
        if (stems == 1)
        {
            trunk.length     = height;
            trunk.radius_tip = base_r * s.trunk_taper;
            tree.stems.push_back(grow(Vector3::Zero, trunk_dir, trunk, rng));
        }
        else
        {
            const bool clump = s.split_height <= 0.02f;
            if (clump)
            {
                const float azimuth_offset = rng.unit() * tau;
                for (uint32_t i = 0; i < stems; i++)
                {
                    const float azimuth = azimuth_offset + tau * (static_cast<float>(i) + rng.range(-0.18f, 0.18f)) / static_cast<float>(stems);
                    const Vector3 out   = Vector3(cos(azimuth), 0.0f, sin(azimuth));
                    Vector3 axis        = Vector3::Cross(trunk_dir, out);
                    axis                = axis.LengthSquared() > 1e-6f ? axis.Normalized() : any_perpendicular(trunk_dir);
                    const float angle   = s.split_angle * rng.range(0.35f, 1.12f) * deg_to_rad;
                    const Vector3 dir   = rotate(trunk_dir, axis, -angle);

                    grow_settings stem_settings = trunk;
                    stem_settings.split_base    = false;
                    stem_settings.length        = height * rng.range(0.7f, 1.06f) / max(0.65f, cos(angle));
                    stem_settings.radius_base   = base_r * rng.range(0.8f, 1.1f);
                    stem_settings.radius_tip    = stem_settings.radius_base * s.trunk_taper;
                    stem_settings.segments      = max(7u, s.trunk_segments);
                    stem_settings.flare         = true;
                    stem_settings.fork_collar   = false;
                    stem_settings.start_dir     = Vector3::Zero;
                    stem_settings.curve         = 0.0f;

                    const Vector3 origin = out * (s.stem_spread * sqrt(rng.unit()));
                    tree.stems.push_back(grow(origin, dir, stem_settings, rng));
                }
            }
            else
            {
                // Grow one continuous main leader trunk from ground to crown so there is never a tube break, cuff, notch, or hourglass at split_height.
                trunk.length     = height;
                trunk.radius_tip = base_r * s.trunk_taper;
                trunk.segments   = max(10u, s.trunk_segments);
                trunk.split_base = false;
                trunk.curve      = s.split_angle * 0.25f;
                tree.stems.push_back(grow(Vector3::Zero, trunk_dir, trunk, rng));

                // Spawn co-dominant primary boughs emerging organically from inside the continuous trunk around split_height.
                const float azimuth_offset = rng.unit() * tau;
                for (uint32_t i = 1; i < stems; i++)
                {
                    const float t_attach = clamp(s.split_height + static_cast<float>(i - 1) * 0.045f + rng.range(-0.02f, 0.02f), 0.16f, 0.55f);
                    Vector3 split_point, split_dir;
                    float split_radius = base_r;
                    sample(tree.stems[0], t_attach, split_point, split_dir, split_radius);

                    const float azimuth = azimuth_offset + tau * (static_cast<float>(i) + rng.range(-0.15f, 0.15f)) / static_cast<float>(stems);
                    const Vector3 out   = Vector3(cos(azimuth), 0.0f, sin(azimuth));
                    Vector3 axis        = Vector3::Cross(split_dir, out);
                    axis                = axis.LengthSquared() > 1e-6f ? axis.Normalized() : any_perpendicular(split_dir);
                    const float angle   = s.split_angle * rng.range(0.95f, 1.38f) * deg_to_rad;
                    const Vector3 dir   = rotate(split_dir, axis, -angle);

                    grow_settings stem_settings = trunk;
                    stem_settings.split_base    = false;
                    const float remaining       = height * (1.0f - t_attach);
                    stem_settings.length        = remaining * rng.range(0.86f, 1.04f) / max(0.65f, cos(angle));
                    stem_settings.radius_base   = split_radius * 0.80f;
                    stem_settings.radius_tip    = stem_settings.radius_base * s.trunk_taper;
                    stem_settings.segments      = max(7u, static_cast<uint32_t>(s.trunk_segments * (1.0f - t_attach)) + 3);
                    stem_settings.flare         = false;
                    stem_settings.fork_collar   = false;
                    stem_settings.start_dir     = split_dir;
                    stem_settings.curve         = -angle / deg_to_rad * 0.34f;

                    const Vector3 origin = split_point - split_dir * (split_radius * 0.28f);
                    stem grown           = grow(origin, dir, stem_settings, rng);
                    grown.parent         = 0;
                    tree.stems.push_back(move(grown));
                }
            }
        }

        // branch levels, breadth first so every level knows the longest of its parents
        const uint32_t trunk_count = static_cast<uint32_t>(tree.stems.size());
        vector<float> level_max_length(levels_used + 2, 0.0f);
        for (const stem& t : tree.stems)
        {
            level_max_length[0] = max(level_max_length[0], t.length);
        }

        size_t level_begin = 0;
        size_t level_end   = tree.stems.size();
        for (uint32_t level = 0; level < levels_used; level++)
        {
            const branch_level& spec = s.levels[level];
            const bool last_level    = level + 1 == levels_used;
            float rotation           = rng.unit() * tau;

            for (size_t parent_index = level_begin; parent_index < level_end; parent_index++)
            {
                // the lower part of a split trunk carries nothing, its stems do
                if (level == 0 && !tree.stems[parent_index].terminal && parent_index < trunk_count && trunk_count > 1)
                {
                    continue;
                }

                const stem parent       = tree.stems[parent_index];
                const float parent_frac = parent.length / max(level_max_length[level], 1e-3f);
                float t_lo              = spec.start;
                float t_hi              = spec.end;
                if (level == 0)
                {
                    // children of a trunk start at the crown base measured in tree height, not along the stem
                    const float base_height = s.crown_base * height;
                    const float start_y     = parent.points.front().y;
                    const float end_y       = parent.points.back().y;
                    const float span_y      = max(end_y - start_y, 0.01f);
                    t_lo = max(t_lo, (base_height - start_y) / span_y);
                }
                if (t_hi <= t_lo + 0.01f)
                {
                    continue;
                }

                float count = spec.count * branch_mult * (t_hi - t_lo) / max(spec.end - spec.start, 0.05f);
                if (level == 0)
                {
                    count /= sqrt(static_cast<float>(max(1u, trunk_count - (trunk_count > 1 && s.split_height > 0.02f ? 1u : 0u))));
                }
                else
                {
                    count *= 0.3f + 0.7f * parent_frac;
                }
                const uint32_t children = static_cast<uint32_t>(max(0.0f, round(count + rng.signed_unit() * 0.5f)));
                if (children == 0)
                {
                    continue;
                }

                const uint32_t whorl = max(1u, spec.whorl);
                const uint32_t nodes = max(1u, (children + whorl - 1) / whorl);
                float planar_sign    = rng.unit() < 0.5f ? 1.0f : -1.0f;
                for (uint32_t node = 0; node < nodes; node++)
                {
                    const float t = t_lo + (t_hi - t_lo) * (static_cast<float>(node) + 0.5f + rng.signed_unit() * 0.35f) / static_cast<float>(nodes);
                    Vector3 position, tangent;
                    float parent_radius;
                    sample(parent, t, position, tangent, parent_radius);
                    const float run = (t - t_lo) / max(t_hi - t_lo, 1e-3f);

                    for (uint32_t w = 0; w < whorl; w++)
                    {
                        const float node_rotation = rotation + tau * static_cast<float>(w) / static_cast<float>(whorl);
                        const float down          = (spec.down_angle + (spec.down_angle_tip - spec.down_angle) * run + spec.down_angle_var * wild * rng.signed_unit()) * deg_to_rad;
                        const Vector3 direction   = child_direction(tangent, node_rotation, down, spec.planar, planar_sign);
                        planar_sign = -planar_sign;

                        float length;
                        if (level == 0)
                        {
                            const float h_frac = clamp(position.y / max(height, 0.01f), 0.0f, 1.0f);
                            const float ratio  = (1.0f - h_frac) / max(1.0f - s.crown_base, 0.05f);
                            length = height * spec.length * shape_ratio(s.shape, ratio);
                            // the crown width is the reach the species is known for, a long branch at a flat angle overshoots it
                            const float reach = length * max(sin(down), 0.2f);
                            const float limit = s.crown_width * height * 1.25f;
                            if (reach > limit)
                            {
                                length *= limit / reach;
                            }
                        }
                        else
                        {
                            length = parent.length * spec.length * (1.0f - 0.55f * t);
                        }
                        length *= 1.0f + spec.length_var * min(wild, 1.6f) * rng.signed_unit();
                        if (length < 0.04f)
                        {
                            continue;
                        }

                        // da vinci's rule keeps the cross sections adding up across a fork
                        const float expected = max(parent.length * spec.length, 0.05f);
                        float radius         = parent_radius * spec.radius_ratio * sqrt(clamp(length / expected, 0.15f, 1.0f));
                        radius               = clamp(radius, 0.0025f, parent_radius * 0.85f);

                        grow_settings settings;
                        settings.length      = length;
                        settings.radius_base = radius;
                        settings.radius_tip  = last_level ? max(0.002f, radius * 0.18f) : radius * 0.32f;
                        settings.segments    = spec.segments;
                        settings.curve       = spec.curve * clamp(wild, 0.6f, 1.6f);
                        settings.curve_var   = spec.curve_var * wild;
                        settings.gravity     = spec.gravity;
                        settings.gnarl       = gnarl * 0.55f;

                        stem child   = grow(position - direction * parent_radius * 0.25f, direction, settings, rng);
                        child.level  = level + 1;
                        child.parent = static_cast<int32_t>(parent_index);
                        tree.stems[parent_index].terminal = false;
                        level_max_length[level + 1]       = max(level_max_length[level + 1], length);
                        tree.stems.push_back(move(child));
                    }
                    rotation += spec.rotate * deg_to_rad + rng.signed_unit() * 0.25f;
                }
            }

            level_begin = level_end;
            level_end   = tree.stems.size();
        }

        // crown estimate from the branch tips, used for the shading normals and the interior pruning
        BoundingBox crown_box;
        bool crown_found = false;
        for (const stem& st : tree.stems)
        {
            if (st.level == 0 && levels_used > 0)
            {
                continue;
            }
            for (size_t i = st.points.size() / 2; i < st.points.size(); i++)
            {
                if (!crown_found)
                {
                    crown_box   = BoundingBox(st.points[i], st.points[i]);
                    crown_found = true;
                }
                else
                {
                    crown_box.Merge(BoundingBox(st.points[i], st.points[i]));
                }
            }
        }
        if (!crown_found)
        {
            crown_box = BoundingBox(Vector3(-1.0f, height * 0.6f, -1.0f), Vector3(1.0f, height, 1.0f));
        }
        tree.crown_center = crown_box.GetCenter();
        tree.crown_radii  = Vector3::Max(crown_box.GetExtents(), Vector3(0.4f, 0.4f, 0.4f));

        // light pruning, twigs deep inside a crown get no sun and a real tree sheds them
        if (levels_used >= 2)
        {
            for (stem& st : tree.stems)
            {
                if (!st.terminal || st.level < 2)
                {
                    continue;
                }
                const Vector3 o = st.points.back() - tree.crown_center;
                const Vector3 n = Vector3(o.x / tree.crown_radii.x, o.y / tree.crown_radii.y, o.z / tree.crown_radii.z);
                if (n.Length() < 0.45f && rng.unit() < 0.6f)
                {
                    st.pruned = true;
                }
            }
        }

        // leaf cards along terminal twigs AND outer sections of penultimate boughs so crowns read as
        // lush, multi-tiered 3D bough-pillow clouds (never sparse bare sticks or giant flat paper sheets)
        const bool needles     = is_needle_like(s);
        const float card_scale = clamp(tree.height / 18.0f, 0.80f, 1.04f);
        const float card_size  = s.card_size * card_scale;
        const float spacing    = card_size * 0.28f / max(s.card_density * leaf_mult, 0.05f);
        const uint32_t tile_count = atlas_tiles * atlas_tiles;
        if (s.fronds == 0)
        {
            for (const stem& st : tree.stems)
            {
                if (st.pruned || (st.level == 0 && levels_used > 0))
                {
                    continue;
                }
                // Terminal twigs carry the main outer canopy; penultimate boughs (level >= 1) carry softer
                // interior shade-core foliage along their outer half so crowns have deep continuous volume.
                const bool interior_bough = !st.terminal;
                if (interior_bough && (st.level == 0 || levels_used < 2 || st.level + 1 < levels_used - 1))
                {
                    continue;
                }

                // Compute local bough-pillow center from this stem's parent bough (or stem tip cluster)
                // so neighboring sprigs share a sculptural bough-cloud normal rather than one flat egg normal.
                Vector3 lobe_center = st.points[st.points.size() / 2];
                float lobe_radius   = max(st.length * 0.65f, card_size * 1.4f);
                if (st.parent >= 0 && st.parent < static_cast<int32_t>(tree.stems.size()) && st.level >= 2)
                {
                    const stem& parent_bough = tree.stems[static_cast<size_t>(st.parent)];
                    if (parent_bough.level >= 1 && parent_bough.points.size() >= 2)
                    {
                        lobe_center = Vector3::Lerp(parent_bough.points[parent_bough.points.size() / 2], parent_bough.points.back(), 0.45f);
                        lobe_radius = max(parent_bough.length * 0.55f, card_size * 1.8f);
                    }
                }

                const float start_t  = interior_bough ? max(s.leaf_start, 0.42f) : s.leaf_start;
                const float run      = st.length * max(0.0f, 1.0f - start_t);
                const float sp_local = interior_bough ? spacing * 1.65f : spacing;
                const uint32_t count = max(1u, static_cast<uint32_t>(run / sp_local + rng.unit()));

                auto emit_card = [&](const Vector3& base_pos, const Vector3& dir_unit, const float length, const float width_ratio, const float roll_rad, const float fold_val, const float droop_val, const uint32_t tile_idx)
                {
                    Vector3 side = Vector3::Cross(dir_unit, Vector3::Up);
                    side         = side.LengthSquared() > 1e-6f ? side.Normalized() : any_perpendicular(dir_unit);
                    side         = rotate(side, dir_unit, roll_rad);
                    Vector3 norm = Vector3::Cross(side, dir_unit).Normalized();
                    if (norm.y < 0.0f)
                    {
                        side = side * -1.0f;
                        norm = norm * -1.0f;
                    }

                    leaf_card card;
                    card.base        = base_pos - dir_unit * (length * 0.08f);
                    card.direction   = dir_unit * length;
                    card.side        = side * (length * width_ratio * 0.5f);
                    card.normal      = norm;
                    card.lobe_center = lobe_center;
                    card.lobe_radius = lobe_radius;
                    card.tile        = tile_idx % tile_count;
                    card.fold        = fold_val;
                    card.droop       = droop_val;
                    card.mirror_u    = (rng.next_uint() & 1u) != 0u;
                    tree.cards.push_back(card);
                };

                const float width_ratio = s.leaf == leaf_shape::needle ? 0.82f : (s.leaf == leaf_shape::scale ? 0.78f : 0.88f);
                const float base_fold   = needles ? 0.32f : 0.24f;
                const float base_droop  = s.leaf == leaf_shape::needle ? 0.14f : (s.leaf == leaf_shape::scale ? 0.08f : 0.16f);

                for (uint32_t i = 0; i <= count; i++)
                {
                    const bool is_tip = (!interior_bough && i == count);
                    const float t     = is_tip ? 1.0f : start_t + (1.0f - start_t) * (static_cast<float>(i) + rng.unit() * 0.6f) / static_cast<float>(count);
                    Vector3 position, tangent;
                    float radius;
                    sample(st, t, position, tangent, radius);

                    Vector3 outward = Vector3(position.x - tree.crown_center.x, (position.y - tree.crown_center.y) * 0.35f, position.z - tree.crown_center.z);
                    outward         = outward.LengthSquared() > 1e-6f ? outward.Normalized() : rng.direction();
                    const float size_mult = interior_bough ? rng.range(0.74f, 0.96f) : rng.range(0.82f, 1.18f);
                    const float length    = card_size * size_mult;

                    if (is_tip)
                    {
                        // Sculptural 3-sprig domed terminal rosette pillow at each outer shoot tip
                        const uint32_t rosette_n = s.leaf == leaf_shape::scale ? 2u : 3u;
                        const float az_base      = rng.unit() * tau;
                        const Vector3 perp_a     = any_perpendicular(tangent);
                        const Vector3 perp_b     = Vector3::Cross(tangent, perp_a).Normalized();
                        for (uint32_t r = 0; r < rosette_n; r++)
                        {
                            const float az      = az_base + tau * static_cast<float>(r) / static_cast<float>(rosette_n) + rng.signed_unit() * 0.30f;
                            const Vector3 splay = perp_a * cos(az) + perp_b * sin(az);
                            const float tilt    = rng.range(0.32f, 0.68f);
                            const Vector3 dir   = (tangent * 0.72f + splay * tilt + outward * 0.28f + Vector3::Up * (0.14f + s.leaf_tilt * 0.25f)).Normalized();
                            const float r_len   = length * rng.range(0.86f, 1.08f);
                            const float roll    = rng.signed_unit() * (needles ? 0.68f : 0.85f);
                            emit_card(position, dir, r_len, width_ratio, roll, base_fold, base_droop * rng.range(0.8f, 1.25f), rng.next_uint());
                        }
                        continue;
                    }

                    Vector3 direction;
                    if (needles && s.leaf == leaf_shape::needle)
                    {
                        // Organic Mediterranean pine needle clouds splaying naturally around arching shoots
                        Vector3 side_h = Vector3::Cross(tangent, Vector3::Up);
                        side_h         = side_h.LengthSquared() > 1e-6f ? side_h.Normalized() : any_perpendicular(tangent);
                        const float splay = (i % 2 == 0 ? 1.0f : -1.0f) * rng.range(0.35f, 0.88f);
                        direction = (tangent * 0.82f + side_h * splay + Vector3::Up * rng.range(0.10f, 0.36f) + rng.direction() * 0.18f).Normalized();
                    }
                    else
                    {
                        direction = (tangent * 0.68f + rng.direction() * 0.52f + outward * 0.38f + Vector3::Up * (s.leaf_tilt * 0.32f)).Normalized();
                    }

                    const float roll_limit = needles && s.leaf == leaf_shape::needle ? 0.82f : (1.0f - s.leaf_tilt) * 1.25f + 0.28f;
                    const float main_roll  = roll_limit * rng.signed_unit();
                    const uint32_t tile0   = rng.next_uint();
                    emit_card(position, direction, length, width_ratio, main_roll, base_fold, base_droop, tile0);

                    // Angled 3D companion sprig on a slightly diverged pitch/yaw (breaks any X-card cross seam)
                    if (!interior_bough && (needles || rng.unit() < 0.55f))
                    {
                        const float cross_roll  = main_roll + (rng.unit() < 0.5f ? 1.0f : -1.0f) * rng.range(0.92f, 1.32f);
                        const Vector3 cross_dir = (direction + rng.direction() * 0.28f + Vector3::Up * 0.12f).Normalized();
                        emit_card(
                            position + direction * (length * 0.06f),
                            cross_dir,
                            length * rng.range(0.84f, 1.00f),
                            width_ratio * 0.94f,
                            cross_roll,
                            base_fold * 0.85f,
                            base_droop * 1.1f,
                            tile0 + 1u + (rng.next_uint() % max(1u, tile_count - 1u))
                        );
                    }
                }
            }
        }
        else if (!bare)
        {
            // palm crowns, young fronds stand up in the middle and old ones hang outside
            const uint32_t frond_count = max(4u, static_cast<uint32_t>(round(s.fronds * sqrt(leaf_mult))));
            for (const stem& st : tree.stems)
            {
                if (!st.terminal || st.level != 0)
                {
                    continue;
                }
                const Vector3 top = st.points.back();
                for (uint32_t i = 0; i < frond_count; i++)
                {
                    const float age_frac  = static_cast<float>(i) / static_cast<float>(frond_count - 1);
                    const float azimuth   = static_cast<float>(i) * 2.39996f + rng.signed_unit() * 0.2f;
                    const float elevation = (65.0f - 85.0f * age_frac + rng.signed_unit() * 8.0f) * deg_to_rad;
                    Vector3 direction     = Vector3(cos(azimuth) * cos(elevation), sin(elevation), sin(azimuth) * cos(elevation)).Normalized();
                    const float length    = s.frond_length * size * rng.range(0.85f, 1.1f) * (0.7f + 0.3f * min(1.0f, age_frac * 2.0f));
                    const float width     = length * 0.2f;

                    frond f;
                    f.tile = rng.next_uint() % (atlas_tiles * atlas_tiles);
                    const uint32_t rows = 7;
                    Vector3 position    = top + direction * (st.radii.back() * 0.5f);
                    for (uint32_t r = 0; r < rows; r++)
                    {
                        const float t = static_cast<float>(r) / static_cast<float>(rows - 1);
                        Vector3 side  = Vector3::Cross(direction, Vector3::Up);
                        side          = side.LengthSquared() > 1e-6f ? side.Normalized() : any_perpendicular(direction);
                        const float w = width * pow(sin(3.14159265f * min(1.0f, 0.12f + t * 0.95f)), 0.6f);
                        f.points.push_back(position);
                        f.sides.push_back(side * max(w, 0.02f));

                        position += direction * (length / static_cast<float>(rows - 1));
                        const Vector3 axis = Vector3::Cross(direction, Vector3::Up);
                        if (axis.LengthSquared() > 1e-6f)
                        {
                            direction = rotate(direction, axis.Normalized(), -s.frond_droop * (0.15f + 0.35f * t) * (0.6f + age_frac));
                        }
                    }
                    tree.fronds.push_back(move(f));
                }
            }
        }

        // the normals use the crown that the leaves actually fill
        if (!tree.cards.empty() || !tree.fronds.empty())
        {
            BoundingBox leaf_box;
            bool found = false;
            auto add = [&](const Vector3& p)
            {
                if (!found)
                {
                    leaf_box = BoundingBox(p, p);
                    found    = true;
                }
                else
                {
                    leaf_box.Merge(BoundingBox(p, p));
                }
            };
            for (const leaf_card& card : tree.cards)
            {
                add(card.base);
                add(card.base + card.direction);
            }
            for (const frond& f : tree.fronds)
            {
                for (const Vector3& p : f.points)
                {
                    add(p);
                }
            }
            tree.crown_center = leaf_box.GetCenter();
            tree.crown_radii  = Vector3::Max(leaf_box.GetExtents(), Vector3(0.3f, 0.3f, 0.3f));
        }

        // budget, the foliage gets the larger share since it carries the silhouette
        const float foliage_share   = s.fronds > 0 ? 0.55f : (needles ? 0.76f : 0.74f);
        const uint32_t foliage_cap  = static_cast<uint32_t>(tree.budget * foliage_share);
        const uint32_t bark_cap     = tree.budget - foliage_cap;
        const uint32_t card_cap     = max(16u, foliage_cap / 4);
        if (tree.cards.size() > card_cap)
        {
            // keep an even subset without bloating surviving cards into flat sheets
            const float keep = static_cast<float>(card_cap) / static_cast<float>(tree.cards.size());
            const float grow_factor = min(1.12f, 1.0f / sqrt(keep));
            vector<leaf_card> kept;
            kept.reserve(card_cap);
            float accumulator = rng.unit();
            for (const leaf_card& card : tree.cards)
            {
                accumulator += keep;
                if (accumulator >= 1.0f)
                {
                    accumulator -= 1.0f;
                    leaf_card scaled = card;
                    scaled.direction = card.direction * grow_factor;
                    scaled.side      = card.side * grow_factor;
                    kept.push_back(scaled);
                }
            }
            tree.cards = move(kept);
        }

        // bark detail settles where it fits the remaining budget
        bark_detail detail;
        detail.sides_scale = c.detail == quality::hero ? 1.50f : (c.detail == quality::mobile ? 0.7f : 1.25f);
        detail.max_sides   = c.detail == quality::hero ? 20u : (c.detail == quality::mobile ? 8u : 16u);
        detail.min_radius  = (c.detail == quality::hero ? 0.0045f : (c.detail == quality::mobile ? 0.018f : 0.0065f)) * (bare ? 0.6f : 1.0f);
        geometry bark      = mesh_bark(tree, s, detail);
        for (uint32_t attempt = 0; attempt < 10 && bark.triangles() > bark_cap; attempt++)
        {
            detail.min_radius  *= 1.35f;
            detail.sides_scale  = max(0.45f, detail.sides_scale * 0.9f);
            detail.angle       *= 1.15f;
            bark                = mesh_bark(tree, s, detail);
        }
        tree.bark = move(bark);

        mesh_cards(tree, tree.foliage);

        // the bounds and the collider
        tree.bounds = BoundingBox(Vector3::Zero, Vector3::Zero);
        for (const geometry* g : { &tree.bark, &tree.foliage })
        {
            if (!g->vertices.empty())
            {
                tree.bounds.Merge(BoundingBox(g->vertices.data(), static_cast<uint32_t>(g->vertices.size())));
            }
        }
        tree.trunk_clear_height = max(0.5f, min(height * max(s.crown_base, 0.15f), s.stems > 1 && s.split_height > 0.02f ? height * s.split_height : height));
        return tree;
    }

    shared_ptr<Mesh> build_mesh(result& tree, const string& name, const bool cache_mesh)
    {
        shared_ptr<Mesh> mesh = make_shared<Mesh>();
        mesh->SetObjectName(name);
        // no scale normalisation, a tree is authored in metres. a preview skips the cache, every slider move
        // is a new tree, an export can reuse the cooked mesh
        uint32_t flags =
            static_cast<uint32_t>(MeshFlags::PostProcessOptimize) |
            static_cast<uint32_t>(MeshFlags::PreserveSourceGeometry) |
            static_cast<uint32_t>(MeshFlags::FoliageCardClusters);
        if (!cache_mesh)
        {
            flags |= static_cast<uint32_t>(MeshFlags::PostProcessSkipCache);
        }
        mesh->SetFlags(flags);

        const bool has_foliage = !tree.foliage.indices.empty();
        mesh->ReserveSubMeshes(has_foliage ? 2 : 1);

        mesh->AddGeometry(tree.bark.vertices, tree.bark.indices, 0u);
        if (has_foliage && !mesh->AddVirtualGeometry(tree.foliage.vertices, tree.foliage.indices, 1u, false, true))
            mesh->AddGeometry(tree.foliage.vertices, tree.foliage.indices, 1u);

        mesh->CreateGpuBuffers();
        return mesh;
    }
}
