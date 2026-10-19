/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

#pragma once
#include "../math/Vector3.h"
#include "../math/Vector4.h"
#include <array>
#include <cstdint>
namespace spartan
{
    // Frame inputs supplied by gameplay. No physics objects cross the rendering boundary.
    struct SurfaceBody
    {
        math::Vector4 center, right, up, forward;
        std::array<math::Vector4, 6> hulls{};
        std::array<math::Vector4, 6 * 48> planes{};
    };
    struct SurfaceContact
    {
        math::Vector3 position, normal, velocity, direction;
        float width = 0.0f;
        float pressure = 0.0f;
        bool valid = false;
    };
    // Any shape that pushes vegetation aside: a car body, a person, a ball. A rounded oriented box,
    // a capsule is a box with zero horizontal extent and a radius.
    struct SurfaceInteractor
    {
        math::Vector3 center;
        math::Vector3 right   = math::Vector3::Right;
        math::Vector3 up      = math::Vector3::Up;
        math::Vector3 forward = math::Vector3::Forward;
        math::Vector3 extents; // half extents along right, up and forward
        math::Vector3 velocity;
        float radius = 0.0f;
        float mass   = 0.0f;
        uint64_t id  = 0;
    };

    constexpr uint32_t surface_interactor_max = 16;
    constexpr uint32_t surface_contact_max    = 8; // four wheels and the feet of whoever walks nearby

    struct SurfaceInteraction
    {
        uint64_t id = 0;
        math::Vector3 center;
        SurfaceBody body;
        std::array<SurfaceContact, surface_contact_max> contacts{};
        std::array<SurfaceInteractor, surface_interactor_max> interactors{};
        uint32_t interactor_count = 0;
    };
}
