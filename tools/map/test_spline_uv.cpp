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

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>
#include <type_traits>
#include <iostream>
#include <cassert>
#include "../../source/world/components/SplineMeshGeometry.h"

int main()
{
    using namespace spartan;
    using math::Vector2;
    for (float depth : {0.25f, 1.0f, 8.0f})
    {
        std::vector<Vector2> section = {{-4-depth,-depth}, {-4,0}, {4,0}, {4+depth,-depth}};
        auto uv = spline_geometry::profile_u(section, true, false, 8);
        assert(std::abs(uv[1]) < 1e-6f && std::abs(uv[2] - 1) < 1e-6f);
    }
    std::vector<Vector2> sidewalk = {{-6.0f,0.15f}, {-4.0f,0.15f}, {-4,0}, {4,0}, {4.0f,0.15f}, {6.0f,0.15f}};
    auto uv = spline_geometry::profile_u(sidewalk, true, false, 8);
    assert(uv[2] == 0 && uv[3] == 1);
    // A 30 km road still preserves a 10 cm longitudinal step after half packing.
    for (float tiling : {0.75f, 1.0f, 2.0f})
    {
        float a = 30000.0f / 8.0f, b = 30000.1f / 8.0f;
        float origin = spline_geometry::uv_origin(a, 1.0f / tiling);
        float packed_a = vertex_pack::half_to_float(vertex_pack::float_to_half(a - origin));
        float packed_b = vertex_pack::half_to_float(vertex_pack::float_to_half(b - origin));
        assert(packed_b > packed_a);
        assert(std::abs((packed_b - packed_a) - (b - a)) < 0.001f);
        assert(std::abs(origin * tiling - std::round(origin * tiling)) < 0.001f);
    }
    std::cout << "Spline UV regressions passed\n";
}
