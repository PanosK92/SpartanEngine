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
#include <span>
namespace spartan
{
    // one drop as the gpu sees it
    struct SurfaceWaterDrop
    {
        float u      = 0.0f; // atlas texel coordinates of the centre
        float v      = 0.0f;
        float radius = 0.0f; // contact radius in metres, 0 for a free slot
        float seed   = 0.0f;
        float lean_x = 0.0f; // car local, the force along the paint over the pinning force, what deforms the cap
        float lean_y = 0.0f;
        float lean_z = 0.0f;
        float speed  = 0.0f; // m/s
    };

    // a texel whose micro water changed this frame
    struct SurfaceWaterTexel
    {
        uint32_t texel = 0;    // atlas index
        float mass     = 0.0f; // milligrams of micro droplets, negative when a drop swept it and left only its residue
        float stamp    = 0.0f; // evaporation clock at the change
        float padding  = 0.0f;
    };

    // a texel whose dirt changed this frame, the packed values the gpu stores (CarDirt.h)
    struct SurfaceDirtTexel
    {
        uint32_t texel   = 0;
        uint32_t color   = 0; // rgb sqrt encoded albedo and coverage, 8 bits each
        uint32_t state   = 0; // wet until in 1/16 s of the dirt clock << 8 | coarseness
        uint32_t padding = 0;
    };

    // Borrowed CPU payloads remain alive and immutable until rendering finishes this frame.
    struct SurfaceWater
    {
        static constexpr uint32_t drops_max = 131000;
        static constexpr uint32_t texels_max = 65536;
        static constexpr uint32_t dirt_texels_max = 65536;
        uint64_t entity_id = 0;
        bool active = false;
        float wetness = 0.0f;
        std::array<math::Vector3, 3> axes{};
        uint32_t version = 0, width = 0, height = 0;
        float texel_size = 0, clock = 0, micro_life = 0, residue_mass = 0;
        math::Vector3 origin, box_min;
        std::array<math::Vector4, 6> faces{};
        std::span<const float> surface, micro;
        std::span<const SurfaceWaterDrop> drops;
        std::span<const SurfaceWaterTexel> texels;

        // the dirt on the same car, in the same atlas
        bool dirt_active = false;
        uint32_t dirt_version = 0;
        float dirt_clock = 0;
        std::span<const uint32_t> dirt_color, dirt_state;
        std::span<const SurfaceDirtTexel> dirt_texels;
    };
}
