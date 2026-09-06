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
