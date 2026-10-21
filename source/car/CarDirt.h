/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

#pragma once
#include "../rendering/SurfaceWater.h"
#include "CarRain.h"

//= INCLUDES ==================
#include "../math/Vector3.h"
#include <vector>
//=============================

namespace spartan
{
    class Entity;

    // the dirt on the occupied car, accumulated grain by grain in the texels of its rain atlas (CarRain.h), a few millimetres each
    // the atlas knows the shape, how much the body closes over each texel and how sharp an edge it sits on, an open panel only
    // holds a film while a crevice fills up, and the air and the rain take back from the exposed paint what the hollows keep
    // a texel holds one deposit: its dry colour, how much of the paint it hides, how coarse its grains are and until when it is wet
    // tire spray lands as clumps that burst into a core and a tail of satellite specks around the impact, dust settles as single
    // grains over the whole body, most of it on the low panels, behind the wheels and on the faces the wake curls back onto
    // the gpu resolves every texel into grains finer than the texel itself, so the dirt builds up like overspray, not stamps
    // the paint damage lives in the same texels: how deep a scrape cut through clearcoat, basecoat and primer towards the steel,
    // the grain of its grooves and the rust on bare steel, a scrape is carved along the body itself, not projected
    // the layers work on each other: grit under a scrape cuts deeper and is ploughed aside, grooves hold dirt and catch the silt
    // the running drops carry, dirt and cut paint snag the drops (CarRain.h), and steel rusts while the rain or wet mud sits on it
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
        // world space, something hard dragged over the paint from position along slide for length metres, severity 0 to 1
        static void Scrape(Entity* root, const math::Vector3& position, const math::Vector3& normal, const math::Vector3& slide, float severity, float length);

        // the airflow lifts loose dry dust off the open panels and the rain rinses the faces it strikes, the hollows keep theirs
        static void Tick(float delta_time, const CarRain::Conditions& conditions);
        static void Clear();   // washes the car, the atlas stays
        static void Wash();    // lifts the dirt, the scratches stay in the paint
        static void Release(); // forgets the car and its atlas
        static float GetCoverage(); // mean coverage over the paint, 0 clean to 1 caked
        static float GetDamage(float depth); // share of the paint cut at least this deep, 0.85 is to the steel
        static bool IsActive();
        static bool IsDirty();
        static uint32_t GetVersion();
        static float GetClock();
        static const std::vector<uint32_t>& GetColor();
        static const std::vector<uint32_t>& GetState();
        static const std::vector<uint32_t>& GetWear();
        static const std::vector<SurfaceDirtTexel>& GetTexels();
    };
}
