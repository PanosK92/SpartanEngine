/*
Copyright(c) 2015-2025 Panos Karabelas

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

//= INCLUDES ====
#include <random>
#include <algorithm> 
//===============

namespace spartan::math
{
    // boudning box, frustum and ray intersection
    enum class Intersection
    {
        Outside,
        Inside,
        Intersects
    };

    constexpr float pi         = 3.14159265359f;
    constexpr float pi_2       = 6.28318530718f;
    constexpr float pi_4       = 12.5663706144f;
    constexpr float pi_div_2   = 1.57079632679f;
    constexpr float pi_div_4   = 0.78539816339f;
    constexpr float pi_inv     = 0.31830988618f;
    constexpr float deg_to_rad = pi / 180.0f;
    constexpr float rad_to_deg = 180.0f / pi;

    template <typename T>
    constexpr T saturate(T x) { return std::clamp<T>(x, static_cast<T>(0), static_cast<T>(1)); }

    template <class T, class U>
    constexpr T lerp(T lhs, T rhs, U t) { return lhs * (static_cast<U>(1) - t) + rhs * t; }

    template <class T>
    constexpr bool approximate_equals(T lhs, T rhs, T error = std::numeric_limits<T>::epsilon()) { return lhs + error >= rhs && lhs - error <= rhs; }

    template <typename T>
    constexpr int sign(T x) { return (static_cast<T>(0) < x) - (x < static_cast<T>(0)); }

    template <class T>
    inline T random(T from = static_cast<T>(0), T to = static_cast<T>(1))
    {
        std::random_device rd;                              // obtain a random number from hardware
        std::mt19937 eng(rd());                             // seed the generator
        std::uniform_real_distribution<T> distr(from, to);  // define the range
        return distr(eng);
    }

    constexpr uint32_t power_of_two_previous(uint32_t x)
    {
        x = x | (x >> 1);
        x = x | (x >> 2);
        x = x | (x >> 4);
        x = x | (x >> 8);
        x = x | (x >> 16);
        return x - (x >> 1);
    }

    constexpr uint32_t power_of_two_next(uint32_t x)
    {
        if (x < 2)
            return 2;

        if (x % 2 == 0)
            return x << 1;

        --x;
        x |= x >> 1;
        x |= x >> 2;
        x |= x >> 4;
        x |= x >> 8;
        x |= x >> 16;
        return x++;
    }
}
