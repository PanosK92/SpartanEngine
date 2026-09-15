/*
Copyright(c) 2015-2026 Panos Karabelas

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and / or sell
copies of the Software, and to permit persons to whom the Software is furnished
to do so, subject to the following conditions :

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.IN NO EVENT SHALL THE AUTHORS OR
COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/

#pragma once
#include "CarState.h"

namespace car
{
    // Three equal-capacity surface zones exchange energy with one bulk carcass.
    // Shares sum to one; powers are W. Evaluate every flux from the old state so
    // surface/core exchange conserves energy independently of zone order.
    inline void integrate_tire_thermal(tire_thermal& thermal, const car_preset& preset,
        const float shares[3], float slip_power, float rolling_power, float airspeed, float dt, float ambient = NAN, float road = NAN, float contact = 0)
    {
        if (!std::isfinite(ambient)) ambient = preset.tire_ambient_temp;
        if (!std::isfinite(road)) road = ambient;
        const float minimum = PxMin(preset.tire_min_temp, PxMin(ambient, road));
        const float surface_capacity = PxMax(preset.tire_surface_heat_capacity / 3.0f, 1.0f);
        const float core_capacity = PxMax(preset.tire_core_heat_capacity, 1.0f);
        const float conductance = preset.tire_surface_core_conductance / 3.0f;
        const float cooling = preset.tire_heat_transfer_static + preset.tire_heat_transfer_airflow * fabsf(airspeed);
        float core_power = PxMax(rolling_power, 0.0f) - 0.1f * cooling * (thermal.core - ambient);
        for (int zone = 0; zone < 3; ++zone)
        {
            float exchange = conductance * (thermal.surface[zone] - thermal.core);
            // A fixed heat partition is an approximation; the remainder heats the road.
            float surface_power = 0.9f * PxMax(slip_power, 0.0f) * shares[zone]
                - exchange - cooling / 3.0f * (thermal.surface[zone] - ambient)
                + 40.0f * PxClamp(contact, 0.0f, 1.0f) * shares[zone] * (road - thermal.surface[zone]);
            core_power += exchange;
            thermal.surface[zone] = PxClamp(thermal.surface[zone] + surface_power * dt / surface_capacity,
                minimum, preset.tire_max_temp);
        }
        thermal.core = PxClamp(thermal.core + core_power * dt / core_capacity, minimum, preset.tire_max_temp);
    }
}
