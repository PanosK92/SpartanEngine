/* Copyright(c) 2015-2026 Panos Karabelas. Licensed under the MIT license. */
#pragma once

#include "../../rhi/RHI_Vertex.h"

namespace spartan::spline_geometry
{
    // The deck owns [0, 1]. Curbs and earthworks must never change its UV domain.
    inline std::vector<float> profile_u(const std::vector<math::Vector2>& profile, bool road, bool closed, float width)
    {
        std::vector<float> u(profile.size(), 0.0f);
        for (size_t i = 1; i < profile.size(); i++)
            u[i] = u[i - 1] + math::Vector2::Distance(profile[i - 1], profile[i]);

        float origin = 0.0f;
        float scale = u.back();
        if (closed)
            scale += math::Vector2::Distance(profile.back(), profile.front());
        if (road)
        {
            scale = std::max(std::abs(width), 0.001f);
            for (size_t i = 0; i < profile.size(); i++)
            {
                if (std::abs(profile[i].x + width * 0.5f) < 0.001f && std::abs(profile[i].y) < 0.001f)
                {
                    origin = u[i];
                    break;
                }
            }
        }
        for (float& value : u)
            value = (value - origin) / std::max(scale, 0.001f);
        if (road)
        {
            for (size_t i = 0; i < profile.size(); i++)
            {
                if (std::abs(profile[i].y) > 0.001f) continue;
                if (std::abs(profile[i].x + width * 0.5f) < 0.001f) u[i] = 0.0f;
                if (std::abs(profile[i].x - width * 0.5f) < 0.001f) u[i] = 1.0f;
            }
        }
        return u;
    }

    // An integer repeat is removed from BOTH ends of a quad, before half packing.
    // Wrapping each vertex independently would interpolate backwards at every repeat.
    inline float uv_origin(float v, float period)
    {
        return period > 0.0f ? std::floor(v / period) * period : 0.0f;
    }
}
