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
    struct SurfaceInteraction
    {
        uint64_t id = 0;
        math::Vector3 center;
        SurfaceBody body;
        std::array<SurfaceContact, 4> contacts{};
    };
}
