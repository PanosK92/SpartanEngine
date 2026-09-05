#pragma once
#include "CarState.h"
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
    inline float hot_tire_pressure(const car_preset& s, float core, float damage)
    {
        // Gauge bar -> absolute bar -> Kelvin gas law -> gauge bar.
        float pressure = (s.tire_pressure + 1.01325f) * (core + 273.15f) / (s.tire_pressure_reference_temp + 273.15f) - 1.01325f;
        return PxMax(pressure * (1.0f - PxClamp(damage, 0.0f, 1.0f) * 0.9f), 0.05f);
    }
    inline float water_grip(const car_preset& s, float speed, float pressure, float water_depth)
    {
        // Estimated smooth hydrodynamic unloading model. Calibration speed is
        // at optimal pressure and 3 mm water; dry contact is exactly unchanged.
        float depth = PxClamp(water_depth / 0.003f, 0.0f, 1.0f);
        float critical = s.tire_hydroplaning_speed * sqrtf(PxMax(pressure, 0.05f) / PxMax(s.tire_pressure_optimal, 0.1f));
        float ratio = fabsf(speed) / PxMax(critical, 1.0f);
        return 1.0f - depth * 0.95f * PxClamp((ratio - 0.7f) / 0.6f, 0.0f, 1.0f);
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
