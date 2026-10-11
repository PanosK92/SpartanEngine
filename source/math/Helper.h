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
    constexpr float pi_div_2   = 1.57079632679f;
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
}
