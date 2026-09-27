/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

#pragma once

//= INCLUDES ==================
#include "../math/Vector3.h"
#include "../math/Vector4.h"
#include <vector>
//=============================

namespace spartan
{
    class Entity;

    // one drop as the gpu sees it
    struct CarRainDropGpu
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
    struct CarRainTexelGpu
    {
        uint32_t texel = 0;    // atlas index
        float mass     = 0.0f; // milligrams of micro droplets, negative when a drop swept it and left only its residue
        float stamp    = 0.0f; // evaporation clock at the change
        float padding  = 0.0f;
    };

    // the water on the occupied car, simulated drop by drop in the car's frame
    // the body is baked into six orthographic height maps, one per car axis and side, packed in one atlas, so every
    // point of the paint is a texel and a drop can walk from texel to texel and from map to map over the edges
    // every texel holds micro water, the sub millimetre droplets the rain leaves, too small to ever slide, when a texel
    // gathers enough of it they coalesce into a drop, and a drop running over the paint sweeps them up and leaves a thin
    // residue, so the paths the drops take stay visible as clean wet lines through the speckle and later drops follow them
    // a drop is held by contact angle hysteresis (furmidge, the pinning force grows with the contact line) and pushed by
    // gravity, the car's acceleration and the airflow in the paint's boundary layer, when the push wins it slides at the
    // speed viscous dissipation allows, drops that touch merge, drops that are pulled off the paint drip away
    class CarRain
    {
    public:
        static constexpr uint32_t drops_max  = 131000; // the gpu id key packs the index in 17 bits
        static constexpr uint32_t texels_max = 65536;  // micro water changes sent to the gpu per frame

        struct Conditions
        {
            math::Vector3 velocity;     // car local, m/s
            math::Vector3 acceleration; // car local, m/s^2
            math::Vector3 gravity;      // car local, m/s^2
            math::Vector3 wind;         // car local, m/s
            float rain     = 0.0f;      // 0 to 1
            float exposure = 0.0f;      // 0 under a roof, 1 in the open
        };

        static void Tick(Entity* root, const Conditions& conditions, float delta_time);
        static void Clear();
        static bool IsActive();

        // bumps whenever the atlas is rebaked or the micro water is reset, the gpu recreates its textures from the cpu copies
        static uint32_t GetVersion();
        static uint32_t GetAtlasWidth();
        static uint32_t GetAtlasHeight();
        static float GetTexelSize();
        static math::Vector3 GetBoxMin();
        static math::Vector4 GetFaceRect(uint32_t face); // xy origin, zw size, texels
        static const std::vector<float>& GetSurface();   // axis coordinate of the face each texel belongs to, huge where none
        static const std::vector<float>& GetMicro();     // mass and stamp per texel, interleaved, the gpu layout
        static float GetClock();
        static float GetMicroLife();
        static float GetResidueMass(); // milligrams
        static const std::vector<CarRainDropGpu>& GetDrops();
        static const std::vector<CarRainTexelGpu>& GetTexels();
    };
}
