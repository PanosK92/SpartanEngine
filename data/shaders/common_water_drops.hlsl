/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

// the physics of a water drop sitting on a surface, the gpu side of WaterDrops.h, the two must agree
// a drop is held by contact angle hysteresis and pushed by gravity, the surface's acceleration and the airflow over it,
// when the push wins it slides at the speed viscous dissipation allows, and it evaporates by diffusion, faster in moving air

#ifndef SPARTAN_COMMON_WATER_DROPS
#define SPARTAN_COMMON_WATER_DROPS

static const float drop_water_density      = 1000.0f;
static const float drop_air_density        = 1.2f;
static const float drop_air_viscosity      = 1.5e-5f;
static const float drop_surface_tension    = 0.072f;
static const float drop_vapour_diffusivity = 2.5e-5f;
static const float drop_vapour_saturation  = 0.0173f;
static const float drop_contact_friction   = 0.15f;
static const float drop_drag_coefficient   = 0.8f;
static const float drop_boundary_layer     = 0.6f;
static const float drop_rain_retained      = 0.7f;
static const float drop_pi                 = 3.14159265f;

// contact radius of a cap meeting the surface at about 90 degrees, a half sphere
float drop_radius(float mass)
{
    return pow(max(3.0f * mass / (2.0f * drop_pi * drop_water_density), 0.0f), 1.0f / 3.0f);
}

float drop_ventilation(float speed, float radius)
{
    return 1.0f + 0.3f * sqrt(2.0f * radius * speed / drop_air_viscosity);
}

// aerodynamic drag of the air skimming the surface on a cap of this contact radius
float2 drop_drag(float2 flow, float radius)
{
    float area = 0.5f * drop_pi * radius * radius;
    return flow * (0.5f * drop_air_density * drop_drag_coefficient * area * length(flow));
}

// pulled off the surface harder than this, the drop drips or is thrown off
float drop_hold_off(float radius)
{
    return drop_pi * radius * drop_surface_tension;
}

// pinning force per metre of contact radius, hysteresis is the cosine of the receding minus the advancing contact angle
float drop_pinning(float hysteresis)
{
    return 2.0f * drop_surface_tension * hysteresis;
}

// held until the push along the surface beats the pinning, then viscous dissipation in the contact region sets the pace
float2 drop_slide(float2 velocity, float mass, float2 push, float pin, float radius, float dt)
{
    float friction  = drop_contact_friction * radius;
    float2 momentum = velocity * mass + push * dt;
    float held      = pin * dt;
    float p         = length(momentum);
    return p <= held ? float2(0.0f, 0.0f) : momentum * ((p - held) / p / (mass + friction * dt));
}

// diffusion limited evaporation, a sessile drop loses mass in proportion to its radius, kg/s
float drop_evaporation(float radius, float dryness, float flow_speed)
{
    return 2.0f * drop_pi * radius * drop_vapour_diffusivity * drop_vapour_saturation * dryness * drop_ventilation(flow_speed, radius);
}

// one drop drawn from an exponential size spectrum of this mean diameter, u uniform in 0 to 1
float drop_mass_sample(float diameter, float diameter_max, float u)
{
    float d = min(diameter_max, -log(max(u, 1e-6f)) * diameter);
    return drop_water_density * drop_pi / 6.0f * d * d * d;
}

uint drop_hash(uint x)
{
    x ^= x >> 16;
    x *= 0x7feb352du;
    x ^= x >> 15;
    x *= 0x846ca68bu;
    x ^= x >> 16;
    return x;
}

float drop_random(inout uint state)
{
    state = drop_hash(state + 0x9e3779b9u);
    return float(state >> 8) / 16777216.0f;
}

#endif
