// Copyright(c) 2015-2026 Panos Karabelas
// Licensed under the Spartan Engine License. See license.md in the repository root.
#pragma once
#include "../math/Vector4.h"
#include <array>
#include <cstdint>

namespace spartan::particle_wake
{
    // Transient world-space flow sources, published by gameplay before rendering.
    inline constexpr uint32_t capacity = 64;
    struct Source
    {
        math::Vector4 center_strength;
        math::Vector4 axis_length;
        math::Vector4 shape; // half width, vortex radius, age, reserved
    };
    inline std::array<Source, capacity> sources = {};
}
