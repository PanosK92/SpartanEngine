/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

#pragma once
#include "CarState.h"
#include "CarTireDeformation.h"
namespace car
{
    inline float sample_curve(const float* x, const float* y, int count, float value, float fallback)
    {
        if (count < 2 || count > 16 || !std::isfinite(value)) return fallback;
        if (value <= x[0]) return y[0];
        for (int i = 1; i < count; ++i)
            if (value < x[i]) return y[i-1] + (y[i] - y[i-1]) * (value - x[i-1]) / (x[i] - x[i-1]);
        return y[count-1];
    }
    inline float hot_tire_pressure(const car_preset& s, float core, float damage, float ambient_pressure_bar = 1.01325f)
    {
        // Gauge bar -> absolute bar -> Kelvin gas law -> gauge bar.
        float pressure = (s.tire_pressure + 1.01325f) * (core + 273.15f) / (s.tire_pressure_reference_temp + 273.15f) - ambient_pressure_bar;
        return PxMax(pressure * (1.0f - PxClamp(damage, 0.0f, 1.0f) * 0.9f), 0.05f);
    }
    inline float loaded_tire_stiffness(const car_preset& s, float pressure)
    {
        return tire_radial_stiffness(s.tire_vertical_stiffness, pressure,
            s.tire_pressure_optimal, s.tire_carcass_stiffness_fraction);
    }
    // metres of tread left, 8 mm new down to the 1.6 mm legal limit when fully worn
    inline float tread_depth(float wear)
    {
        return 0.008f - 0.0064f * PxClamp(wear, 0.0f, 1.0f);
    }

    // share of the patch the water wedge lifts off the road, horne's dynamic hydroplaning
    // the critical speed grows with the root of inflation pressure, tread that can still pump the water out of
    // the patch holds it off longer and the lift itself rises with dynamic pressure, so with speed squared
    // calibration speed is at optimal pressure in water deeper than the tread, where the grooves no longer help
    inline float hydroplaning_lift(const car_preset& s, float speed, float pressure, float water_depth, float wear)
    {
        float flooded = PxClamp((water_depth - 0.0005f) / 0.0025f, 0.0f, 1.0f);
        if (flooded <= 0.0f)
            return 0.0f;

        float drainage = PxClamp(tread_depth(wear) / 0.008f, 0.0f, 1.0f);
        float channels = 1.0f + 0.5f * drainage * PxClamp(1.0f - water_depth / 0.008f, 0.0f, 1.0f);
        float critical = s.tire_hydroplaning_speed * channels * sqrtf(PxMax(pressure, 0.05f) / PxMax(s.tire_pressure_optimal, 0.1f));
        float ratio    = fabsf(speed) / PxMax(critical, 1.0f);
        return flooded * PxClamp((ratio * ratio - 0.35f) / 0.65f, 0.0f, 1.0f);
    }

    // wet grip over dry grip at the same load, dry contact is exactly unchanged
    // a film lubricates rubber and road texture, most of it within the first quarter millimetre,
    // viscous squeeze film pressure then erodes the rest with speed (the piarc friction speed gradient),
    // sooner on worn tread and in deeper water, and past the hydroplaning onset only viscous drag is left
    inline float water_grip(const car_preset& s, float speed, float pressure, float water_depth, float wear = 0.0f)
    {
        if (!(water_depth > 0.0f))
            return 1.0f;

        float film           = 1.0f - expf(-water_depth / 0.00025f);
        float lubricated     = 1.0f - 0.2f * film;
        float drainage       = PxClamp(tread_depth(wear) / 0.008f, 0.0f, 1.0f);
        float gradient_speed = (40.0f + 60.0f * drainage) / (1.0f + water_depth / 0.003f);
        float viscous        = 1.0f - 0.85f * film * (1.0f - expf(-fabsf(speed) / gradient_speed));
        float lift           = hydroplaning_lift(s, speed, pressure, water_depth, wear);
        return lubricated * viscous * (1.0f - lift) + 0.05f * lift;
    }
    struct hybrid_state
    {
        float energy_j = 0, temperature = 20, electrical_power_w = 0, loss_power_w = 0;
    };
    // Mechanical power is positive when motoring. Energy is a usable-energy
    // state, not a voltage/chemistry model; its SOC is energy based.
    inline float integrate_hybrid(const car_preset& s, hybrid_state& b, float requested_power, float dt, float ambient, float motor_efficiency = 0)
    {
        const float capacity = s.battery_capacity_kwh * 3600000.0f;
        const float eta = (motor_efficiency > 0 ? motor_efficiency : s.motor_efficiency) * s.battery_efficiency;
        float power = requested_power >= 0 ? PxMin(requested_power, b.energy_j * eta / dt)
            : PxMax(requested_power, -(capacity - b.energy_j) / (eta * dt));
        b.electrical_power_w = power >= 0 ? power / eta : power * eta;
        b.loss_power_w = fabsf(b.electrical_power_w - power);
        b.energy_j = PxClamp(b.energy_j - b.electrical_power_w * dt, 0.0f, capacity);
        b.temperature = (b.temperature + dt * (b.loss_power_w + s.battery_cooling * ambient) / s.battery_heat_capacity)
            / (1.0f + dt * s.battery_cooling / s.battery_heat_capacity);
        return power;
    }
}
