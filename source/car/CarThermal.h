#pragma once
#include "CarState.h"

namespace car
{
    // Three equal-capacity surface zones exchange energy with one bulk carcass.
    // Shares sum to one; powers are W. Evaluate every flux from the old state so
    // surface/core exchange conserves energy independently of zone order.
    inline void integrate_tire_thermal(tire_thermal& thermal, const car_preset& preset,
        const float shares[3], float slip_power, float rolling_power, float airspeed, float dt)
    {
        const float surface_capacity = PxMax(preset.tire_surface_heat_capacity / 3.0f, 1.0f);
        const float core_capacity = PxMax(preset.tire_core_heat_capacity, 1.0f);
        const float conductance = preset.tire_surface_core_conductance / 3.0f;
        const float cooling = preset.tire_heat_transfer_static + preset.tire_heat_transfer_airflow * fabsf(airspeed);
        float core_power = PxMax(rolling_power, 0.0f) - 0.1f * cooling * (thermal.core - preset.tire_ambient_temp);
        for (int zone = 0; zone < 3; ++zone)
        {
            float exchange = conductance * (thermal.surface[zone] - thermal.core);
            // A fixed heat partition is an approximation; the remainder heats the road.
            float surface_power = 0.9f * PxMax(slip_power, 0.0f) * shares[zone]
                - exchange - cooling / 3.0f * (thermal.surface[zone] - preset.tire_ambient_temp);
            core_power += exchange;
            thermal.surface[zone] = PxClamp(thermal.surface[zone] + surface_power * dt / surface_capacity,
                preset.tire_min_temp, preset.tire_max_temp);
        }
        thermal.core = PxClamp(thermal.core + core_power * dt / core_capacity, preset.tire_min_temp, preset.tire_max_temp);
    }
}
