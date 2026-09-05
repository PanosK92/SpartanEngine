#pragma once
#include <algorithm>
#include <cmath>

namespace spartan::skid
{
    inline float ramp(float x)
    {
        x = std::clamp(x, 0.0f, 1.0f);
        return x * x * (3.0f - 2.0f * x);
    }

    // Ratios alone are ill-conditioned near rest. Require real tread/ground motion
    // and supported tire load as well as slip before depositing rubber.
    inline float demand(float ratio, float angle, float sliding_speed, float load, float threshold, bool active)
    {
        if (!std::isfinite(ratio) || !std::isfinite(angle) || !std::isfinite(sliding_speed) || !std::isfinite(load))
            return 0.0f;
        threshold = std::max(threshold, 0.05f);
        float severity = std::hypot(ratio / threshold, angle / (threshold * 0.5f));
        float onset = active ? 0.55f : 1.0f;
        return ramp((severity - onset) / 1.2f) * ramp((sliding_speed - 0.25f) / 1.5f) * ramp((load - 80.0f) / 1200.0f);
    }

    inline float follow(float current, float target, float dt)
    {
        float response = target > current ? 0.075f : 0.14f;
        return current + (target - current) * (1.0f - std::exp(-std::max(dt, 0.0f) / response));
    }

    inline bool discontinuity(float distance, float normal_dot, float direction_dot, float dt, float speed)
    {
        return !std::isfinite(distance) || dt > 0.25f || normal_dot < 0.65f || direction_dot < 0.0f ||
            distance > std::max(1.5f, speed * dt * 2.0f + 0.5f);
    }

    inline float retirement(float age, float remaining_slots)
    {
        return ramp((60.0f - age) / 12.0f) * ramp((remaining_slots - 1.0f) / 24.0f);
    }
}
