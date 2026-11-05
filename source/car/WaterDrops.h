/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

#pragma once

//= INCLUDES ==================
#include "../math/Helper.h"
#include "../math/Vector3.h"
#include <algorithm>
#include <cmath>
//=============================

// the physics of a water drop sitting on a surface, shared by every wet surface that is simulated drop by drop,
// the paint of the car (CarRain.h) and the front element of the camera lens (CarLens.h)
// a drop is held by contact angle hysteresis (furmidge, the pinning force grows with the contact line) and pushed by
// gravity, the surface's acceleration and the airflow over it, when the push wins it slides at the speed viscous
// dissipation allows, and it evaporates by diffusion, faster in moving air
namespace spartan::water_drops
{
    // what the surface feels, all in the surface's own frame
    struct Conditions
    {
        math::Vector3 velocity;     // m/s
        math::Vector3 acceleration; // m/s^2
        math::Vector3 gravity;      // m/s^2
        math::Vector3 wind;         // m/s
        float rain     = 0.0f;      // 0 to 1
        float exposure = 0.0f;      // 0 under a roof, 1 in the open
    };

    // water and air at 20 c
    constexpr float water_density      = 1000.0f;
    constexpr float air_density        = 1.2f;
    constexpr float air_viscosity      = 1.5e-5f; // kinematic, m^2/s
    constexpr float surface_tension    = 0.072f;  // n/m
    constexpr float vapour_diffusivity = 2.5e-5f; // m^2/s
    constexpr float vapour_saturation  = 0.0173f; // kg/m^3

    constexpr float contact_friction = 0.15f; // n s/m^2 per metre of contact radius, about 3 pi ln(r/a) times the viscosity of water, a 2 mm drop runs down a vertical panel at about 8 cm/s
    constexpr float drag_coefficient = 0.8f;
    constexpr float boundary_layer   = 0.6f; // share of the free stream a millimetre tall drop feels inside the boundary layer

    // rain
    constexpr float rain_rate_max      = 25.0f;  // mm/h at full rain
    constexpr float rain_fall_speed    = 7.0f;   // m/s, terminal speed of a 2 mm drop
    constexpr float rain_drop_diameter = 0.001f; // metres, mean of the marshall palmer size spectrum at full rain
    constexpr float rain_retained      = 0.7f;   // of an impact's mass that stays on the surface, the rest splashes away
    constexpr float rain_gust          = 1.5f;   // m/s, spread of the turbulent velocity of the rain about its mean, it reaches surfaces the mean flow misses

    // tyre spray, the film a tyre throws off a wet road, most of it falls back but a little hangs in the wake as a fine mist
    constexpr float road_film_depth      = 5.0e-4f; // metres of water on a fully wet road
    constexpr float tyre_width           = 0.3f;
    constexpr float spray_share          = 0.001f;  // of the thrown water that stays airborne in the wake at full speed, most of it falls back within a metre of the tyre
    constexpr float spray_speed_full     = 20.0f;   // m/s where the tyres atomise as much as they ever will
    constexpr float spray_drop_diameter  = 3.0e-4f; // metres, mean of the mist's size spectrum

    // the air saturates in the rain and dries out over minutes after it
    constexpr float humidity_rain = 0.97f;
    constexpr float humidity_dry  = 0.72f;
    constexpr float humidity_time = 300.0f; // seconds for the air to dry out once the rain stops

    // contact radius of a cap meeting the surface at about 90 degrees, a half sphere
    inline float radius_of(float mass)
    {
        return cbrtf(3.0f * mass / (2.0f * math::pi * water_density));
    }

    inline float ventilation(float speed, float radius)
    {
        return 1.0f + 0.3f * sqrtf(2.0f * radius * speed / air_viscosity);
    }

    inline float humidity_step(float humidity, bool raining, float dt)
    {
        const float target = raining ? humidity_rain : humidity_dry;
        return humidity + (target - humidity) * std::min(1.0f, dt / (raining ? 30.0f : humidity_time));
    }

    // the rain as the surface sees it, falling at the terminal speed, carried by the wind, coming at a moving surface
    inline math::Vector3 rain_relative(const Conditions& c)
    {
        return c.gravity.Normalized() * rain_fall_speed + c.wind - c.velocity;
    }

    inline float rain_diameter(float rain)
    {
        return rain_drop_diameter * (0.7f + 0.3f * rain);
    }

    // mean retained mass of an exponential size spectrum, the mean of d^3 is 6 d0^3
    inline float rain_mass_mean(float rain)
    {
        const float d = rain_diameter(rain);
        return water_density * math::pi * d * d * d * rain_retained;
    }

    // one impact drawn from the spectrum, u uniform in 0 to 1
    inline float rain_mass_sample(float rain, float u)
    {
        const float d = std::min(0.005f, -logf(std::max(u, 1e-6f)) * rain_diameter(rain));
        return water_density * math::pi / 6.0f * d * d * d * rain_retained;
    }

    // mean speed the rain comes at a surface with, approach is the mean flow along the normal, the gusts add the tail
    // of a gaussian, a smooth form of a * cdf(a / s) + s * pdf(a / s)
    inline float rain_approach(float approach)
    {
        return 0.5f * (approach + sqrtf(approach * approach + rain_gust * rain_gust));
    }

    // one drop of mist from the wake, u uniform in 0 to 1, everything that reaches the surface stays
    inline float spray_mass_sample(float u)
    {
        const float d = std::min(0.002f, -logf(std::max(u, 1e-6f)) * spray_drop_diameter);
        return water_density * math::pi / 6.0f * d * d * d;
    }

    inline float spray_mass_mean()
    {
        return water_density * math::pi * spray_drop_diameter * spray_drop_diameter * spray_drop_diameter;
    }

    // kg of water per second and per m^2 the wake of a car carries through a plane behind it, the tyres throw the road's
    // film at the car's speed and the mist spreads over a cloud that grows with the distance behind the car
    inline float spray_flux(float wetness, float speed, float distance, uint32_t tyres)
    {
        const float thrown = wetness * road_film_depth * tyre_width * speed * static_cast<float>(tyres) * water_density;
        const float share  = spray_share * std::min(1.0f, speed / spray_speed_full);
        const float spread = 1.0f + distance / 5.0f;
        const float cloud  = 2.5f * spread * spread;
        return thrown * share / cloud;
    }

    // impacts per second on an area the rain approaches at this speed along its normal
    inline float rain_impacts(float rain, float approach, float area)
    {
        // kg of water per m^3 of air, what falls at the terminal speed to give the rain rate
        const float density = rain * rain_rate_max / 3.6e6f * water_density / rain_fall_speed;
        return density * approach * area / rain_mass_mean(rain);
    }

    // aerodynamic drag of the air skimming the surface on a cap of this contact radius
    inline math::Vector3 drag(const math::Vector3& flow, float radius)
    {
        const float area = 0.5f * math::pi * radius * radius;
        return flow * (0.5f * air_density * drag_coefficient * area * flow.Length());
    }

    // pulled off the surface harder than this, the drop drips or is thrown off
    inline float hold_off(float radius)
    {
        return math::pi * radius * surface_tension;
    }

    // pinning force per metre of contact radius, hysteresis is the cosine of the receding minus the advancing contact angle
    inline float pinning(float hysteresis)
    {
        return 2.0f * surface_tension * hysteresis;
    }

    // held until the push along the surface beats the pinning, then viscous dissipation in the contact region sets the pace
    inline math::Vector3 slide(const math::Vector3& velocity, float mass, const math::Vector3& push, float pin, float radius, float dt)
    {
        const float friction          = contact_friction * radius;
        const math::Vector3 momentum  = velocity * mass + push * dt;
        const float held              = pin * dt;
        const float p                 = momentum.Length();
        return p <= held ? math::Vector3::Zero : momentum * ((p - held) / p / (mass + friction * dt));
    }

    // diffusion limited evaporation, a sessile drop loses mass in proportion to its radius, kg/s
    inline float evaporation(float radius, float dryness, float flow_speed)
    {
        return 2.0f * math::pi * radius * vapour_diffusivity * vapour_saturation * dryness * ventilation(flow_speed, radius);
    }
}
