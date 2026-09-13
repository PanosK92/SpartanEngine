// Copyright(c) 2015-2026 Panos Karabelas. Licensed under the MIT license.
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace spartan::terrain_acoustics
{
    // Integrate nearby canopy footprints, including vegetation just beyond a
    // clearing edge. Finite support guarantees silence far from real vegetation.
    inline float contribution(float distance, float width, float depth, float radius)
    {
        if (!std::isfinite(distance) || !std::isfinite(width) || !std::isfinite(depth) ||
            distance >= radius || radius <= 0.0f || width <= 0.0f || depth <= 0.0f)
            return 0.0f;
        float t = std::clamp(1.0f - distance / radius, 0.0f, 1.0f);
        t = t * t * (3.0f - 2.0f * t);
        return std::min(width * depth * 0.78539816f, 400.0f) * t * t;
    }

    inline float gain(uint32_t profile, float canopy, float scrub, float exposure)
    {
        canopy = std::clamp(canopy, 0.0f, 1.0f);
        scrub = std::clamp(scrub, 0.0f, 1.0f);
        if (profile == 1) return std::clamp(canopy + scrub * 0.12f, 0.0f, 1.0f); // cicadas
        if (profile == 2) return std::clamp(canopy * 0.85f + scrub * 0.3f, 0.0f, 1.0f); // birds
        if (profile == 3) return (0.35f + 0.65f * std::clamp(exposure, 0.0f, 1.0f)) * (1.0f - canopy * 0.65f);
        return 1.0f; // ordinary volumes, surf and settlement detail
    }
}
