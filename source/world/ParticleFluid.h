// Copyright(c) 2015-2026 Panos Karabelas
// Licensed under the Spartan Engine License. See license.md in the repository root.
#pragma once
#include "../math/Vector3.h"
#include "../math/Vector4.h"
#include <array>
#include <cstdint>

namespace spartan::particle_fluid
{
    // What the smoke fluid has to flow around, published by gameplay before rendering.
    // The renderer solves the air in a box around the focus, see particles_fluid.hlsl.
    inline constexpr uint32_t box_capacity   = 8;
    inline constexpr uint32_t wheel_capacity = 16;

    // an oriented box moving as a rigid body
    struct Box
    {
        math::Vector3 center;
        math::Vector3 right   = math::Vector3::Right;
        math::Vector3 up      = math::Vector3::Up;
        math::Vector3 forward = math::Vector3::Forward;
        math::Vector3 extents; // half extents along right, up and forward
        math::Vector3 velocity;
        math::Vector3 angular_velocity;
        math::Vector3 pivot;   // the point the angular velocity turns the box around
    };

    // a spinning cylinder, the tread moves at spin times radius on top of the hub velocity
    struct Wheel
    {
        math::Vector3 center;
        math::Vector3 axis = math::Vector3::Right; // a positive spin about it rolls the wheel forward
        math::Vector3 velocity;
        float radius     = 0.0f;
        float half_width = 0.0f;
        float spin       = 0.0f; // rad/s
    };

    struct Scene
    {
        bool valid = false;
        math::Vector3 focus;          // where the box of air sits, on the ground under whoever is being watched
        math::Vector3 focus_velocity;
        math::Vector4 ground_plane = math::Vector4(0.0f, 1.0f, 0.0f, 0.0f);
        std::array<Box, box_capacity> boxes{};
        std::array<Wheel, wheel_capacity> wheels{};
        uint32_t box_count   = 0;
        uint32_t wheel_count = 0;
    };

    inline Scene scene;
}
