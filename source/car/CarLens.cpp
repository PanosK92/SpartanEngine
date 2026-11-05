/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

//= INCLUDES ======================
#include "pch.h"
#include "CarLens.h"
#include "../profiling/Profiler.h"
//=================================

//= NAMESPACES ===============
using namespace std;
using namespace spartan::math;
using namespace spartan::water_drops;
//============================

namespace spartan
{
    namespace
    {
        constexpr float half_height         = 0.015f; // metres of glass the frame's half height spans, a small front element so a raindrop reads at the scale it does on a chase camera
        constexpr float glass_margin        = 1.15f;  // the glass reaches past the frame, lens_glass_margin in lens_drops.hlsl
        constexpr float circle_of_confusion = 0.02f;  // lens units, the camera is focused on the car metres away, the drops blur over this much
        constexpr float impacts_max         = 2048.0f; // LENS_DROPS_MAX in shared_buffers.h, more in one step would not fit

        Conditions latest;
        float aspect_ratio  = 16.0f / 9.0f;
        float humidity      = humidity_dry;
        float clock         = 0.0f;
        float elapsed       = 0.0f; // since the gpu last stepped
        float rain_impacts_due  = 0.0f;
        float spray_impacts_due = 0.0f;
        bool running        = false;
        bool wipe           = true;
        uint32_t rng_state  = 0x2545f491u;

        float random01()
        {
            rng_state ^= rng_state << 13;
            rng_state ^= rng_state >> 17;
            rng_state ^= rng_state << 5;
            return static_cast<float>(rng_state >> 8) / 16777216.0f;
        }

        float glass_area()
        {
            return 4.0f * half_height * half_height * aspect_ratio * glass_margin * glass_margin;
        }
    }

    void CarLens::Tick(const Conditions& conditions, float spray, float aspect, float amount, float delta_time)
    {
        SP_PROFILE_CPU();

        latest       = conditions;
        aspect_ratio = aspect > 0.1f ? aspect : aspect_ratio;
        running      = true;
        if (delta_time <= 0.0f)
            return;

        const bool raining = (conditions.rain > 0.0f && conditions.exposure > 0.0f) || spray > 0.0f;
        humidity           = humidity_step(humidity, raining, delta_time);
        clock             += delta_time * (max(0.0f, 1.0f - humidity) / (1.0f - humidity_rain));
        elapsed           += delta_time;

        // the rain hits the glass when it comes at it, driving forward the camera runs into it, the gusts reach it at a standstill too
        if (conditions.rain > 0.0f && conditions.exposure > 0.0f)
        {
            const float approach = rain_approach(-rain_relative(conditions).z);
            rain_impacts_due    += rain_impacts(conditions.rain, approach, glass_area()) * delta_time * conditions.exposure * amount;
        }

        // the mist the tyres throw into the wake
        if (spray > 0.0f)
        {
            spray_impacts_due += spray * glass_area() * delta_time * amount / spray_mass_mean();
        }
    }

    void CarLens::Clear()
    {
        running           = false;
        wipe              = true;
        humidity          = humidity_dry;
        elapsed           = 0.0f;
        rain_impacts_due  = 0.0f;
        spray_impacts_due = 0.0f;
    }

    LensWater CarLens::Consume()
    {
        LensWater water;
        if (!running)
            return water;

        // whole impacts land now, the fraction carries over so a light rain still gets its share
        const float rain  = min(floorf(rain_impacts_due + random01()), impacts_max);
        const float spray = min(floorf(spray_impacts_due + random01()), impacts_max);
        rain_impacts_due  = max(0.0f, rain_impacts_due - rain);
        spray_impacts_due = max(0.0f, spray_impacts_due - spray);

        const Vector3 air   = latest.wind - latest.velocity;
        const Vector3 force = latest.gravity - latest.acceleration;
        water.rain          = Vector4(wipe ? 2.0f : 1.0f, circle_of_confusion, half_height, aspect_ratio);
        water.air           = Vector4(air.x, air.y, air.z, min(elapsed, 0.1f));
        water.force         = Vector4(force.x, force.y, force.z, clock);
        water.spawn         = Vector4(rain, spray, rain_diameter(latest.rain), max(0.0f, 1.0f - humidity));
        wipe                = false;
        elapsed             = 0.0f;
        return water;
    }
}
