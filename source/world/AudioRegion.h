// Copyright(c) 2015-2026 Panos Karabelas. Licensed under the MIT license.
#pragma once

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

namespace spartan::audio_region
{
    struct Point { float x = 0.0f; float z = 0.0f; };

    inline float smooth(float value)
    {
        value = std::clamp(value, 0.0f, 1.0f);
        return value * value * (3.0f - 2.0f * value);
    }

    // Positive inside, negative outside. Works with concave outlines and either winding.
    inline float signed_distance(const std::vector<Point>& polygon, Point point)
    {
        if (polygon.size() < 3) return -std::numeric_limits<float>::max();
        bool inside = false;
        float distance_squared = std::numeric_limits<float>::max();
        for (size_t i = 0, j = polygon.size() - 1; i < polygon.size(); j = i++)
        {
            const Point a = polygon[j], b = polygon[i];
            const float dx = b.x - a.x, dz = b.z - a.z;
            const float length_squared = dx * dx + dz * dz;
            const float t = length_squared > 0.0f ? std::clamp(((point.x - a.x) * dx + (point.z - a.z) * dz) / length_squared, 0.0f, 1.0f) : 0.0f;
            const float ex = point.x - a.x - t * dx, ez = point.z - a.z - t * dz;
            distance_squared = std::min(distance_squared, ex * ex + ez * ez);
            if ((a.z > point.z) != (b.z > point.z) && point.x < a.x + dx * (point.z - a.z) / dz) inside = !inside;
        }
        return std::sqrt(distance_squared) * (inside ? 1.0f : -1.0f);
    }

    inline float weight(float distance, float fade, bool boundary_only)
    {
        fade = std::max(fade, 0.01f);
        return boundary_only ? smooth(1.0f - std::abs(distance) / fade) : smooth(0.5f + distance / (2.0f * fade));
    }

    inline float slew(float current, float target, float sample_rate, float seconds)
    {
        return current + (target - current) * (1.0f - std::exp(-1.0f / (std::max(sample_rate, 1.0f) * std::max(seconds, 0.01f))));
    }
}
