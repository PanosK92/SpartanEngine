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

//= INCLUDES ===============================
#pragma once
#include "CarState.h"
//==========================================

// tire math: pacejka magic formula curves, load-sensitive grip, temperature factors,
// per-surface friction scalars, brake efficiency. all pure functions, no state mutation.

namespace car
{
    inline float pacejka(float slip, float B, float C, float D, float E)
    {
        float Bx = B * slip;
        return D * sinf(C * atanf(Bx - E * (Bx - atanf(Bx))));
    }

    // slip at which the magic formula peaks, closed form with E ignored
    inline float pacejka_peak_slip(float B, float C)
    {
        return tanf(PxPi / (2.0f * PxMax(C, 1.01f))) / PxMax(B, 0.1f);
    }

    // lateral grip peaks at a slightly negative camber and falls off quadratically
    inline float get_camber_grip_factor(float camber)
    {
        float dev = camber - tuning::camber_optimal;
        return PxClamp(1.0f - tuning::camber_grip_loss * dev * dev, 0.5f, 1.0f);
    }

    // derived from com z-offset and wheelbase, no need to store separately
    inline float get_weight_distribution_front()
    {
        if (cfg.wheelbase < 0.01f)
        {
            return 0.5f;
        }
        return PxClamp(0.5f + tuning::spec.center_of_mass_z / cfg.wheelbase, 0.0f, 1.0f);
    }

    inline float load_sensitive_grip(float load)
    {
        if (load <= 0.0f)
        {
            return 0.0f;
        }
        return load * powf(load / tuning::spec.load_reference, tuning::spec.load_sensitivity - 1.0f);
    }

    inline float get_tire_temp_grip_factor(float temperature)
    {
        float opt = tuning::spec.tire_optimal_temp;
        float range = PxMax(tuning::spec.tire_temp_range, 1.0f);
        float dev = fabsf(temperature - opt);
        float norm = PxClamp(dev / range, 0.0f, 1.0f);
        float penalty = norm * norm * tuning::spec.tire_grip_temp_factor;
        return 1.0f - penalty;
    }

    struct tire_condition_modifiers
    {
        float peak_grip = 1.0f;
        float stiffness = 1.0f;
        float relaxation = 1.0f;
    };

    inline tire_condition_modifiers get_tire_condition_modifiers(float surface_temperature, float core_temperature, float wear, float load)
    {
        tire_condition_modifiers modifiers;
        float temperature_range = PxMax(tuning::spec.tire_temp_range, 1.0f);
        float core_deviation = PxClamp(fabsf(core_temperature - tuning::spec.tire_optimal_temp) / temperature_range, 0.0f, 1.5f);
        float pressure_ratio = PxClamp(tuning::spec.tire_pressure / PxMax(tuning::spec.tire_pressure_optimal, 0.1f), 0.6f, 1.4f);
        float pressure_error = pressure_ratio - 1.0f;
        float pressure_grip = PxClamp(1.0f - pressure_error * pressure_error * 0.45f, 0.75f, 1.0f);
        float pressure_stiffness = powf(pressure_ratio, 0.55f);
        float temperature_stiffness = PxClamp(1.0f - core_deviation * core_deviation * 0.22f, 0.55f, 1.0f);
        float wear_clamped = PxClamp(wear, 0.0f, 1.0f);
        float wear_grip = PxClamp(1.0f - wear_clamped * tuning::spec.tire_grip_wear_loss, 0.20f, 1.0f);
        float wear_stiffness = 1.0f - wear_clamped * 0.18f;
        float load_ratio = PxClamp(load / PxMax(tuning::spec.load_reference, 1.0f), 0.25f, 3.0f);
        modifiers.peak_grip = PxClamp(get_tire_temp_grip_factor(surface_temperature) * pressure_grip * wear_grip, 0.15f, 1.0f);
        modifiers.stiffness = PxClamp(temperature_stiffness * pressure_stiffness * wear_stiffness, 0.55f, 1.30f);
        modifiers.relaxation = PxClamp(powf(load_ratio, 0.12f) * (1.0f + wear_clamped * 0.20f) / modifiers.stiffness, 0.65f, 1.80f);
        return modifiers;
    }

    inline float get_surface_friction(surface_type surface)
    {
        static constexpr float friction[] = {
            tuning::surface_friction_asphalt,
            tuning::surface_friction_concrete,
            tuning::surface_friction_wet_asphalt,
            tuning::surface_friction_gravel,
            tuning::surface_friction_grass,
            tuning::surface_friction_ice
        };
        return (surface >= 0 && surface < surface_count) ? friction[surface] : 1.0f;
    }

    inline float get_brake_efficiency(float temp)
    {
        float amb = tuning::spec.brake_ambient_temp;
        float opt = PxMax(tuning::spec.brake_optimal_temp, amb + 10.0f);
        float fade = PxMax(tuning::spec.brake_fade_temp, opt + 10.0f);
        if (temp >= fade)
        {
            return 0.6f;
        }
        if (temp < opt)
        {
            float t = PxClamp((temp - amb) / (opt - amb), 0.0f, 1.0f);
            return 0.80f + 0.20f * t;
        }
        float t = (temp - opt) / (fade - opt);
        return PxClamp(1.0f - 0.4f * t, 0.5f, 1.0f);
    }
}
