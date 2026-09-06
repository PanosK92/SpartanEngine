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

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>
#include "TerrainLayer.h"
#include "../math/BoundingBox.h"

namespace spartan::terrain_placement
{
    // The same diagonal as GenerateVerticesAndIndices and terrain_blend_ground_height.
    inline float triangle_height(float h00, float h10, float h01, float h11, float x, float z)
    {
        return x + z <= 1.0f
            ? h00 + x * (h10 - h00) + z * (h01 - h00)
            : h11 + (1.0f - x) * (h01 - h11) + (1.0f - z) * (h10 - h11);
    }

    inline uint32_t hash(uint32_t x)
    {
        x = (x ^ (x >> 16)) * 0x7feb352du;
        x = (x ^ (x >> 15)) * 0x846ca68bu;
        return x ^ (x >> 16);
    }

    inline float random(uint32_t seed, uint32_t channel)
    {
        return static_cast<float>(hash(seed + channel * 0x9e3779b9u) >> 8) * (1.0f / 16777216.0f);
    }

    struct Surface
    {
        float height = 0.0f;
        math::Vector3 normal = math::Vector3::Up;
        float weight = 0.0f;
        bool allowed = true; // hard exclusions; a low slope preference is still valid support
    };

    // Bounds and samples are terrain local. A global lattice and half-open ownership mean that
    // adjacent tiles evaluate identical formations, including when only one tile is rescattered.
    // Each sample callback returns false outside the heightfield; no clamping rocks onto its edge.
    template<typename Sample>
    void mountain_formations(
        const TerrainScatterLayer& layer, const math::BoundingBox& mesh_bounds,
        float min_x, float min_z, float max_x, float max_z, float sea,
        const math::Vector3& tile_offset, Sample&& sample, std::vector<math::Matrix>& output)
    {
        using namespace math;
        output.clear();
        if (layer.density <= 0.0f || layer.mesh_scale <= 0.0f || layer.size_max <= 0.0f)
            return;

        const Vector3 extent = mesh_bounds.GetExtents();
        if (!std::isfinite(extent.x + extent.y + extent.z) || std::min({extent.x, extent.y, extent.z}) <= 0.0f)
            return;

        const float spacing = std::clamp(layer.formation_spacing, 8.0f, 1000.0f);
        const float length = std::clamp(layer.formation_length, 1.0f, spacing * 1.5f);
        const float width = std::clamp(layer.formation_width, 1.0f, spacing);
        const float thickness = std::clamp(layer.formation_height, 1.0f, 500.0f);
        const uint32_t members = std::clamp(layer.clump_count, 1u, 32u);
        // Includes the largest formation's shoulders and fragments, even across tile boundaries.
        const float reach = length + width;
        const int32_t x0 = static_cast<int32_t>(std::floor((min_x - reach) / spacing));
        const int32_t z0 = static_cast<int32_t>(std::floor((min_z - reach) / spacing));
        const int32_t x1 = static_cast<int32_t>(std::floor((max_x + reach) / spacing));
        const int32_t z1 = static_cast<int32_t>(std::floor((max_z + reach) / spacing));
        const float fill = saturate(layer.density * spacing * spacing / (10000.0f * members));
        struct Formation
        {
            uint32_t priority;
            std::vector<Matrix> rocks;
        };
        std::vector<Formation> formations;
        auto relief = [&](const Surface& ground)
        {
            const float slope = std::acos(saturate(ground.normal.y)) * rad_to_deg;
            const float slope_t = saturate((slope - layer.slope_min) / std::max(layer.slope_max - layer.slope_min, 0.001f));
            const float altitude_t = saturate((ground.height - sea) / std::max(layer.altitude_span, 1.0f));
            const float influence = std::max(layer.size_from_slope + layer.size_from_altitude, 0.0f);
            return influence > 0.0f ? saturate((slope_t * layer.size_from_slope +
                altitude_t * layer.size_from_altitude) / influence) : 0.5f;
        };

        for (int32_t z = z0; z <= z1; ++z)
        for (int32_t x = x0; x <= x1; ++x)
        {
            const uint32_t seed = hash(static_cast<uint32_t>(x) * 73856093u ^
                static_cast<uint32_t>(z) * 19349663u ^ hash(layer.seed));
            const float cx = (static_cast<float>(x) + 0.2f + random(seed, 0) * 0.6f) * spacing;
            const float cz = (static_cast<float>(z) + 0.2f + random(seed, 1) * 0.6f) * spacing;
            Surface anchor;
            if (!sample(cx, cz, anchor) || !anchor.allowed || anchor.height <= sea ||
                anchor.weight <= 0.0f || random(seed, 2) >= fill * anchor.weight)
                continue;

            Vector3 along(anchor.normal.z, 0.0f, -anchor.normal.x);
            if (along.LengthSquared() < 1e-6f)
                along = Vector3(1.0f, 0.0f, 0.0f);
            along.Normalize();
            along = Quaternion::FromEulerAngles(0.0f, (random(seed, 3) - 0.5f) * 24.0f, 0.0f) * along;
            // Positive across points downhill, so the broken fragments collect at the cliff toe.
            Vector3 across(-along.z, 0.0f, along.x);
            if (across.Dot(anchor.normal) < 0.0f)
                across = -across;
            const float mass = lerp(0.65f, 1.25f, random(seed, 7)) * lerp(0.8f, 1.15f, relief(anchor));
            const float span = length * mass, depth = width * mass, height = thickness * mass;
            Formation formation{hash(seed ^ 0xb5297a4du), {}};
            uint32_t seated_cores = 0;

            for (uint32_t member = 0; member < members; ++member)
            {
                const uint32_t rock_seed = hash(seed ^ ((member + 1u) * 83492791u));
                const bool core = member < 3;
                const bool shoulder = member >= 3 && member < 6;
                float strike_offset, dip_offset;
                Vector3 dimensions;
                if (core)
                {
                    // Overlap by more than half a slab: this creates a continuous bedrock body,
                    // rather than spaced copies of a boulder along a line. Core zero is largest.
                    const float side = member == 0 ? 0.0f : (member == 1 ? -1.0f : 1.0f);
                    strike_offset = side * span * 0.23f;
                    dip_offset = (random(rock_seed, 1) - 0.5f) * depth * 0.16f;
                    const float taper = member == 0 ? 1.0f : lerp(0.72f, 0.90f, random(rock_seed, 2));
                    dimensions = Vector3(span * 0.72f, height, depth * 0.95f) * taper;
                }
                else if (shoulder)
                {
                    const float side = member == 3 ? -1.0f : (member == 4 ? 1.0f : 0.0f);
                    strike_offset = side * span * 0.47f;
                    dip_offset = depth * (member == 5 ? 0.42f : 0.18f);
                    dimensions = Vector3(span * 0.30f, height * 0.48f, depth * 0.52f) *
                        lerp(0.8f, 1.1f, random(rock_seed, 2));
                }
                else
                {
                    strike_offset = (random(rock_seed, 0) * 2.0f - 1.0f) * span * 0.60f;
                    dip_offset = depth * lerp(0.3f, 0.65f, random(rock_seed, 1));
                    // Broad, independent fragment sizes; relief no longer makes every rock equal.
                    float size = lerp(std::min(layer.size_min, layer.size_max), std::max(layer.size_min, layer.size_max),
                        random(rock_seed, 2) * random(rock_seed, 2));
                    size *= lerp(0.35f, 0.8f, random(rock_seed, 3)) * lerp(0.8f, 1.2f, relief(anchor));
                    if (layer.giant_chance > 0.0f && random(rock_seed, 4) < layer.giant_chance)
                        size = std::max(size, layer.giant_size > 0.0f ? layer.giant_size : layer.size_max);
                    dimensions = extent * (2.0f * size * layer.mesh_scale) * Vector3(1.25f, 0.85f, 1.0f);
                    // Even a tiny authored formation keeps a hierarchy of slabs and fragments.
                    dimensions *= std::min(1.0f, std::min(span, depth) * 0.25f /
                        std::max({dimensions.x, dimensions.y, dimensions.z, 0.001f}));
                }
                const float px = cx + along.x * strike_offset + across.x * dip_offset;
                const float pz = cz + along.z * strike_offset + across.z * dip_offset;
                Surface ground;
                if (!sample(px, pz, ground) || !ground.allowed || ground.height <= sea)
                    continue;

                // Slabs are specified in world metres, independent of asset bounds and units.
                const Vector3 scale_xyz = dimensions / (extent * 2.0f);
                const Vector3 up = lerp(Vector3::Up, ground.normal, saturate(layer.align_to_normal)).Normalized();
                const Vector3 strike = (along - up * along.Dot(up)).Normalized();
                const Quaternion jitter = Quaternion::FromEulerAngles(0.0f,
                    (random(rock_seed, 5) * 2.0f - 1.0f) * std::clamp(layer.formation_jitter, 0.0f, 90.0f) *
                    (core ? 0.35f : 1.0f), 0.0f);
                const Quaternion rotation = Quaternion::FromLookRotation(strike.Cross(up), up) * jitter;
                const Vector3 half = dimensions * 0.5f;
                const Vector3 center = mesh_bounds.GetCenter() * scale_xyz;
                const Vector3 support_axes(ground.normal.Dot(rotation * Vector3(half.x, 0, 0)),
                    ground.normal.Dot(rotation * Vector3(0, half.y, 0)), ground.normal.Dot(rotation * Vector3(0, 0, half.z)));
                const float support = support_axes.Length();
                const float burial = std::clamp(layer.embed_fraction + (random(rock_seed, 6) - 0.5f) * 0.1f, 0.1f, 0.8f);
                Vector3 position = Vector3(px, ground.height, pz) - rotation * center +
                    ground.normal * (support * (1.0f - 2.0f * burial) + layer.surface_offset);

                float lower = 0.0f;
                bool supported = true;
                // Probe concentric footprints. The outer ring checks exclusions while the inner
                // rings seat the rounded underside. A flatter toe must not delete a whole cliff.
                for (uint32_t probe = 0; probe < 37; ++probe)
                {
                    const float angle = static_cast<float>(probe % 12) * pi_2 / 12.0f;
                    const float radius = probe == 36 ? 0.0f : (probe < 12 ? 0.4f : (probe < 24 ? 0.75f : 0.98f));
                    const Vector3 foot(half.x * std::cos(angle) * radius,
                        -half.y * std::sqrt(1.0f - radius * radius), half.z * std::sin(angle) * radius);
                    const Vector3 point = position + rotation * (center + foot);
                    Surface contact;
                    if (!sample(point.x, point.z, contact) || contact.height <= sea || !contact.allowed)
                    {
                        supported = false;
                        break;
                    }
                    if (radius < 0.9f)
                        lower = std::max(lower, point.y - contact.height);
                }
                if (!supported || lower > support * 0.75f)
                    continue;
                position.y -= lower;
                if (core)
                    ++seated_cores;
                if (px >= min_x && px < max_x && pz >= min_z && pz < max_z)
                    formation.rocks.push_back(Matrix::CreateScale(scale_xyz) * Matrix::CreateRotation(rotation) *
                        Matrix::CreateTranslation(position - tile_offset));
            }
            // Failed bedrock anchors must not leave disconnected nuggets masquerading as cliffs.
            if (seated_cores > 0 && !formation.rocks.empty())
                formations.push_back(std::move(formation));
        }

        // Budget whole formations first, in deterministic spatially shuffled order. An evenly
        // spaced sample of individual rocks would punch holes through every bedrock body.
        std::stable_sort(formations.begin(), formations.end(), [](const Formation& a, const Formation& b)
        {
            return a.priority < b.priority;
        });
        for (const Formation& formation : formations)
        {
            const size_t remaining = layer.max_per_tile > 0 ? layer.max_per_tile - output.size() : formation.rocks.size();
            const size_t count = std::min(remaining, formation.rocks.size());
            output.insert(output.end(), formation.rocks.begin(), formation.rocks.begin() + count);
            if (layer.max_per_tile > 0 && output.size() == layer.max_per_tile)
                break;
        }
    }
}
