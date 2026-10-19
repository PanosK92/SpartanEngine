/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

#pragma once
#include "../rendering/SurfaceWater.h"

//= INCLUDES ==================
#include "../math/Vector3.h"
#include <vector>
//=============================

namespace spartan
{
    class Entity;

    // the dirt on the occupied car, accumulated grain by grain in the texels of its rain atlas (CarRain.h), a few millimetres each
    // a texel holds one deposit: its dry colour, how much of the paint it hides, how coarse its grains are and until when it is wet
    // tire spray lands as clumps that burst into a core and a tail of satellite specks around the impact, dust settles as single
    // grains over the whole body, most of it on the low panels, behind the wheels and on the faces the wake curls back onto
    // the gpu resolves every texel into grains finer than the texel itself, so the dirt builds up like overspray, not stamps
    class CarDirt
    {
    public:
        struct Deposit
        {
            math::Vector3 color = math::Vector3(0.2f, 0.14f, 0.085f); // linear albedo once dry
            float moisture      = 0.0f; // 0 dry dust to 1 slurry, wet deposits are dark and glossy until they dry
            float coarseness    = 0.5f; // 0 powder to 1 clods and leaf matter
        };

        // world space, the impact of one clump of spray, mass 1 is a typical clump off a tire
        static void Splat(Entity* root, const math::Vector3& position, const math::Vector3& normal, const math::Vector3& velocity, const Deposit& deposit, float mass);
        // world space, dust in the air around the car, grains is how many settle this frame, wheels are the contact points
        static void Dust(Entity* root, const math::Vector3* wheels, uint32_t wheel_count, const math::Vector3& velocity, const Deposit& deposit, float grains);

        static void Tick(float delta_time);
        static void Clear();   // washes the car, the atlas stays
        static void Release(); // forgets the car and its atlas
        static float GetCoverage(); // mean coverage over the paint, 0 clean to 1 caked
        static bool IsActive();
        static bool IsDirty();
        static uint32_t GetVersion();
        static float GetClock();
        static const std::vector<uint32_t>& GetColor();
        static const std::vector<uint32_t>& GetState();
        static const std::vector<SurfaceDirtTexel>& GetTexels();
    };
}
