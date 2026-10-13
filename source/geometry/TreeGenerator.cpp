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
        };

        stem grow(const Vector3& origin, Vector3 direction, const grow_settings& settings, tree_random& rng)
        {
            stem result;
            result.length = settings.length;
            result.flare  = settings.flare;
            result.points.push_back(origin);
            result.radii.push_back(settings.radius_base);

            const uint32_t segments = max(2u, settings.segments);
            const float curve_step  = settings.curve * deg_to_rad / static_cast<float>(segments);
            const float var_step    = settings.curve_var * deg_to_rad / sqrt(static_cast<float>(segments));

            // a random plane for the variation, the curve itself always bends toward the sky (or away from it)
            Vector3 position  = origin;
            float t_previous  = 0.0f;
            for (uint32_t i = 1; i <= segments; i++)
            {
                // a trunk packs its rings near the ground, that is where the flare changes the radius
                const float linear = static_cast<float>(i) / static_cast<float>(segments);
                const float t      = settings.flare ? pow(linear, 1.35f) : linear;
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
                        direction = rotate(direction, axis.Normalized(), settings.gnarl * 0.35f * rng.signed_unit());
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

                position += direction * step;
                result.points.push_back(position);
                result.radii.push_back(settings.radius_base + (settings.radius_tip - settings.radius_base) * pow(t, 0.85f));
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

            // keep the rings where the stem turns or thins, a straight run needs only its ends
            vector<size_t> kept = { 0 };
            Vector3 last_direction = (s.points[1] - s.points[0]).Normalized();
            float last_radius      = s.radii[0];
            for (size_t i = 1; i + 1 < count; i++)
            {
                const Vector3 direction = (s.points[i + 1] - s.points[i]).Normalized();
                const float turn        = acos(clamp(Vector3::Dot(direction, last_direction), -1.0f, 1.0f));
                const float thinning    = 1.0f - s.radii[i] / max(last_radius, 1e-5f);
                const bool flare_ring   = s.flare && i <= 2;
                if (turn >= detail.angle || thinning > 0.3f || flare_ring)
                {
                    kept.push_back(i);
                    last_direction = direction;
                    last_radius    = s.radii[i];
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
                const float flare  = s.flare ? sp.trunk_flare * exp(-max(height, 0.0f) / max(base_r * 2.2f, 0.02f)) : 0.0f;
                const float v      = v_length / max(tile_width, 0.01f);
                for (uint32_t j = 0; j <= sides; j++)
                {
                    const float angle      = tau * static_cast<float>(j) / static_cast<float>(sides);
                    const Vector3 radial   = normal * cos(angle) + binormal * sin(angle);
                    // root buttresses, five soft lobes that fade a couple of radii up the trunk
                    const float lobes      = flare > 0.0f ? 1.0f + flare * (0.55f + 0.45f * pow(max(0.0f, cos(angle * 5.0f + 0.7f)), 2.0f)) : 1.0f;
                    const float radius     = s.radii[i] * lobes;
                    const Vector3 position = s.points[i] + radial * radius;
                    const Vector3 surface_tangent = Vector3::Cross(tangent, radial);
                    const float u = static_cast<float>(j) / static_cast<float>(sides) * u_repeat + twist * v * 0.15f;
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

            // a cone closes the end, an open tube shows its hollow from below
            const size_t last          = kept.back();
            const Vector3 tip_tangent  = (s.points[last] - s.points[kept[kept.size() - 2]]).Normalized();
            const float tip_v          = (v_length + s.radii[last]) / max(tile_width, 0.01f);
            const uint32_t tip         = static_cast<uint32_t>(output.vertices.size());
            output.vertices.emplace_back(s.points[last] + tip_tangent * s.radii[last] * 1.5f, Vector2(0.5f * u_repeat, -tip_v), tip_tangent, normal);
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

        // pushes the normal toward the outside of the crown, a crown of flat cards lit by their own normals reads as
        // a pile of paper, an ellipsoid normal makes it shade like one mass of leaves while the texture keeps the detail
        Vector3 crown_normal(const Vector3& position, const Vector3& own, const result& tree, const float blend)
        {
            const Vector3 offset = position - tree.crown_center;
            const Vector3 radii  = Vector3::Max(tree.crown_radii, Vector3(0.3f, 0.3f, 0.3f));
            Vector3 radial       = Vector3(offset.x / (radii.x * radii.x), offset.y / (radii.y * radii.y), offset.z / (radii.z * radii.z));
            radial               = radial.LengthSquared() > 1e-8f ? radial.Normalized() : Vector3::Up;
            return (own * (1.0f - blend) + radial * blend).Normalized();
        }

        void mesh_cards(const result& tree, geometry& output)
        {
            for (const leaf_card& card : tree.cards)
            {
                float u0, v0, u1, v1;
                tile_rect(card.tile, u0, v0, u1, v1);
                const float um = (u0 + u1) * 0.5f;

                // folded along the midrib, a v section reads as volume from the side where a flat card vanishes
                const Vector3 lift = card.normal * (card.side.Length() * card.fold);
                const Vector3 base = card.base;
                const Vector3 tip  = card.base + card.direction;
                const Vector3 positions[6] =
                {
                    base - card.side, base + lift, base + card.side,
                    tip - card.side,  tip + lift,  tip + card.side
                };
                const Vector2 uvs[6] =
                {
                    Vector2(u0, v1), Vector2(um, v1), Vector2(u1, v1),
                    Vector2(u0, v0), Vector2(um, v0), Vector2(u1, v0)
                };

                const Vector3 tangent = card.side.Normalized();
                const uint32_t first  = static_cast<uint32_t>(output.vertices.size());
                for (uint32_t i = 0; i < 6; i++)
                {
                    output.vertices.emplace_back(positions[i], uvs[i], crown_normal(positions[i], card.normal, tree, 0.55f), tangent);
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
                    const Vector3 normal   = crown_normal(f.points[i], up_local, tree, 0.3f);
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
            const bool clump      = s.split_height <= 0.02f;
            Vector3 split_point   = Vector3::Zero;
            Vector3 split_dir     = trunk_dir;
            float split_radius    = base_r;
            int32_t lower_index   = -1;
            if (!clump)
            {
                trunk.length     = height * s.split_height;
                trunk.radius_tip = base_r * (1.0f - (1.0f - s.trunk_taper) * s.split_height) * 0.92f;
                trunk.segments   = max(3u, static_cast<uint32_t>(s.trunk_segments * s.split_height) + 2);
                tree.stems.push_back(grow(Vector3::Zero, trunk_dir, trunk, rng));
                tree.stems.back().terminal = false;
                lower_index  = 0;
                split_point  = tree.stems.back().points.back();
                split_dir    = (tree.stems.back().points.back() - tree.stems.back().points[tree.stems.back().points.size() - 2]).Normalized();
                split_radius = trunk.radius_tip;
            }

            const float azimuth_offset = rng.unit() * tau;
            for (uint32_t i = 0; i < stems; i++)
            {
                const float azimuth = azimuth_offset + tau * (static_cast<float>(i) + rng.range(-0.2f, 0.2f)) / static_cast<float>(stems);
                const Vector3 out   = Vector3(cos(azimuth), 0.0f, sin(azimuth));
                Vector3 axis        = Vector3::Cross(split_dir, out);
                axis                = axis.LengthSquared() > 1e-6f ? axis.Normalized() : any_perpendicular(split_dir);
                const float angle   = s.split_angle * rng.range(clump ? 0.35f : 0.75f, 1.2f) * deg_to_rad;
                const Vector3 dir   = rotate(split_dir, axis, -angle);

                grow_settings stem_settings = trunk;
                const float remaining       = height * (1.0f - (clump ? 0.0f : s.split_height));
                stem_settings.length        = remaining * rng.range(clump ? 0.7f : 0.85f, 1.05f) / max(0.6f, cos(angle));
                stem_settings.radius_base   = clump ? base_r * rng.range(0.8f, 1.1f) : split_radius * 1.15f / sqrt(static_cast<float>(stems));
                stem_settings.radius_tip    = stem_settings.radius_base * s.trunk_taper;
                stem_settings.segments      = max(4u, static_cast<uint32_t>(s.trunk_segments * (1.0f - s.split_height)) + 1);
                stem_settings.flare         = clump;
                stem_settings.curve         = clump ? 0.0f : angle / deg_to_rad * 0.6f;

                const Vector3 origin = clump ? out * (s.stem_spread * sqrt(rng.unit())) : split_point - dir * split_radius * 0.3f;
                stem grown           = grow(origin, dir, stem_settings, rng);
                grown.parent         = lower_index;
                tree.stems.push_back(move(grown));
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

        // leaf cards along the terminal branches
        // a tall crown seen from tens of meters needs bigger sprays, fixed size cards spread too thin over it
        const bool needles     = is_needle_like(s);
        const float card_scale = clamp(tree.height / 12.0f, 1.0f, 1.6f);
        const float card_size  = s.card_size * card_scale;
        const float spacing    = card_size * 0.38f / max(s.card_density * leaf_mult, 0.05f);
        if (s.fronds == 0)
        {
            for (const stem& st : tree.stems)
            {
                if (st.pruned || !st.terminal || (st.level == 0 && levels_used > 0))
                {
                    continue;
                }

                // rounded stochastically, most terminal twigs are shorter than the spacing and truncation would
                // give every one of them the same card count whatever the density and leafiness ask for
                const float run      = st.length * (1.0f - s.leaf_start);
                const uint32_t count = max(1u, static_cast<uint32_t>(run / spacing + rng.unit()));
                for (uint32_t i = 0; i <= count; i++)
                {
                    const float t = i == count ? 1.0f : s.leaf_start + (1.0f - s.leaf_start) * (static_cast<float>(i) + rng.unit() * 0.6f) / static_cast<float>(count);
                    Vector3 position, tangent;
                    float radius;
                    sample(st, t, position, tangent, radius);

                    Vector3 outward = Vector3(position.x - tree.crown_center.x, 0.0f, position.z - tree.crown_center.z);
                    outward         = outward.LengthSquared() > 1e-6f ? outward.Normalized() : rng.direction();

                    leaf_card card;
                    const float length = card_size * rng.range(0.8f, 1.2f);
                    Vector3 direction;
                    if (needles && s.leaf == leaf_shape::needle)
                    {
                        // sprays follow the branch and splay sideways in its plane, seen from above they read as layers
                        Vector3 side_h = Vector3::Cross(tangent, Vector3::Up);
                        side_h         = side_h.LengthSquared() > 1e-6f ? side_h.Normalized() : any_perpendicular(tangent);
                        const float splay = (i % 2 == 0 ? 1.0f : -1.0f) * rng.range(0.25f, 0.8f);
                        direction = (tangent + side_h * splay + Vector3::Up * 0.15f).Normalized();
                        if (i == count)
                        {
                            direction = tangent;
                        }
                    }
                    else
                    {
                        direction = (tangent * 0.75f + rng.direction() * 0.55f + outward * 0.35f + Vector3::Up * (s.leaf_tilt * 0.35f)).Normalized();
                    }

                    Vector3 side = Vector3::Cross(direction, Vector3::Up);
                    side         = side.LengthSquared() > 1e-6f ? side.Normalized() : any_perpendicular(direction);
                    const float roll_limit = needles && s.leaf == leaf_shape::needle ? 0.75f : (1.0f - s.leaf_tilt) * 1.4f + 0.2f;
                    side = rotate(side, direction, roll_limit * rng.signed_unit());
                    Vector3 normal = Vector3::Cross(side, direction).Normalized();
                    if (normal.y < 0.0f)
                    {
                        side   = side * -1.0f;
                        normal = normal * -1.0f;
                    }

                    const float width_ratio = s.leaf == leaf_shape::needle ? 0.55f : (s.leaf == leaf_shape::scale ? 0.7f : 0.85f);
                    card.base      = position - direction * length * 0.06f;
                    card.direction = direction * length;
                    card.side      = side * (length * width_ratio * 0.5f);
                    card.normal    = normal;
                    card.tile      = rng.next_uint() % (atlas_tiles * atlas_tiles);
                    card.fold      = needles ? 0.55f : 0.3f;
                    tree.cards.push_back(card);
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
        const float foliage_share   = s.fronds > 0 ? 0.55f : (needles ? 0.72f : 0.7f);
        const uint32_t foliage_cap  = static_cast<uint32_t>(tree.budget * foliage_share);
        const uint32_t bark_cap     = tree.budget - foliage_cap;
        const uint32_t card_cap     = max(16u, foliage_cap / 4);
        if (tree.cards.size() > card_cap)
        {
            // keep an even subset and grow the survivors so the crown keeps its cover
            const float keep = static_cast<float>(card_cap) / static_cast<float>(tree.cards.size());
            const float grow_factor = min(1.6f, 1.0f / sqrt(keep));
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
        detail.sides_scale = c.detail == quality::hero ? 1.35f : (c.detail == quality::mobile ? 0.7f : 1.0f);
        detail.max_sides   = c.detail == quality::hero ? 16u : (c.detail == quality::mobile ? 8u : 12u);
        detail.min_radius  = (c.detail == quality::hero ? 0.005f : (c.detail == quality::mobile ? 0.018f : 0.009f)) * (bare ? 0.6f : 1.0f);
        geometry bark      = mesh_bark(tree, s, detail);
        for (uint32_t attempt = 0; attempt < 10 && bark.triangles() > bark_cap; attempt++)
        {
            detail.min_radius  *= 1.35f;
            detail.sides_scale  = max(0.45f, detail.sides_scale * 0.9f);
            detail.angle       *= 1.15f;
            bark                = mesh_bark(tree, s, detail);
        }
        tree.bark_lods.push_back(move(bark));

        // distance levels drop whole branch orders before meshoptimizer touches what is left, simplification
        // alone collapses a thin branch into a spike while dropping it keeps the remaining silhouette clean
        const uint32_t deepest = levels_used;
        struct lod_step
        {
            float sides;
            float radius;
            int32_t level_drop;
            float trunk_share;
            float angle;
        };
        const lod_step steps[] =
        {
            { 0.65f, 2.2f,  0, 0.00f, 1.8f },
            { 0.45f, 5.0f,  1, 0.05f, 2.6f },
            { 0.30f, 10.0f, 2, 0.12f, 3.5f },
            { 0.20f, 30.0f, 3, 0.30f, 5.0f }
        };
        for (const lod_step& step : steps)
        {
            bark_detail lod = detail;
            lod.sides_scale             = max(0.15f, detail.sides_scale * step.sides);
            lod.max_sides               = max(4u, static_cast<uint32_t>(detail.max_sides * max(step.sides, 0.35f)));
            lod.min_radius              = detail.min_radius * step.radius;
            lod.max_level               = static_cast<uint32_t>(max(0, static_cast<int32_t>(deepest) - step.level_drop));
            lod.trunk_only_radius_ratio = step.trunk_share;
            lod.angle                   = detail.angle * step.angle;
            geometry level              = mesh_bark(tree, s, lod);
            if (level.indices.size() > 192)
            {
                const size_t target = max<size_t>(96, level.indices.size() * 4 / 5);
                geometry_processing::simplify(level.indices, level.vertices, target, true, false, false);
            }
            const geometry& previous = tree.bark_lods.back();
            if (level.indices.empty() || level.indices.size() >= static_cast<size_t>(previous.indices.size() * mesh_lod_min_reduction))
            {
                continue;
            }
            tree.bark_lods.push_back(move(level));
        }

        mesh_cards(tree, tree.foliage);

        // the bounds and the collider
        tree.bounds = BoundingBox(Vector3::Zero, Vector3::Zero);
        for (const geometry* g : { &tree.bark_lods.front(), &tree.foliage })
        {
            if (!g->vertices.empty())
            {
                tree.bounds.Merge(BoundingBox(g->vertices.data(), static_cast<uint32_t>(g->vertices.size())));
            }
        }
        tree.trunk_clear_height = max(0.5f, min(height * max(s.crown_base, 0.15f), s.stems > 1 && s.split_height > 0.02f ? height * s.split_height : height));
        return tree;
    }

    shared_ptr<Mesh> build_mesh(result& tree, const string& name, const bool impostor)
    {
        shared_ptr<Mesh> mesh = make_shared<Mesh>();
        mesh->SetObjectName(name);
        // no scale normalisation, a tree is authored in metres. a preview skips the cache, every slider move
        // is a new tree, an export keeps it so the impostor baked here is the one its file finds on load
        uint32_t flags =
            static_cast<uint32_t>(MeshFlags::PostProcessOptimize) |
            static_cast<uint32_t>(MeshFlags::PostProcessPreserveLod0) |
            static_cast<uint32_t>(MeshFlags::PostProcessGenerateLods);
        if (!impostor)
        {
            flags |= static_cast<uint32_t>(MeshFlags::PostProcessSkipCache);
        }
        mesh->SetFlags(flags);

        const bool has_foliage = !tree.foliage.indices.empty();
        mesh->ReserveSubMeshes(has_foliage ? 2 : 1);

        // bark, lod 0 through the regular path and the branch order levels after it
        {
            geometry lod0 = tree.bark_lods.front();
            mesh->AddGeometry(lod0.vertices, lod0.indices, false, 0, true);
            for (size_t i = 1; i < tree.bark_lods.size(); i++)
            {
                geometry level = tree.bark_lods[i];
                geometry_processing::weld_and_optimize(level.vertices, level.indices);
                mesh->AddLod(level.vertices, level.indices, 0);
            }
        }

        // foliage, the card thinning lods and the impostor come from the mesh itself
        if (has_foliage)
        {
            geometry foliage = tree.foliage;
            mesh->AddGeometry(foliage.vertices, foliage.indices, true, 1, true, true, impostor);
        }

        mesh->CreateGpuBuffers();
        return mesh;
    }
}
