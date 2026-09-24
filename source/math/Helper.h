/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

#pragma once

//= INCLUDES ====
#include <random>
#include <algorithm> 
//===============

namespace spartan::math
{
    // bounding box, frustum and ray intersection
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
    constexpr float epsilon    = 1e-5f;

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
        {
            return 2;
        }

        if (x % 2 == 0)
        {
            return x << 1;
        }

        --x;
        x |= x >> 1;
        x |= x >> 2;
        x |= x >> 4;
        x |= x >> 8;
        x |= x >> 16;
        return x++;
    }
}
