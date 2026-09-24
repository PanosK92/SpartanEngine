/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

#pragma once
#include "../math/Vector3.h"
#include <algorithm>
#include <cmath>

namespace spartan::car_camera
{
    // Follow the rendered car in translation and smooth only the heading.
    // A world-space position spring trails a moving target by a speed-dependent
    // distance and cuts chords through the car when the player orbits it.
    struct ChaseState
    {
        math::Vector3 position = math::Vector3(0.0f);
        math::Vector3 look_at = math::Vector3(0.0f);
        math::Vector3 previous_car_position = math::Vector3(0.0f);
        float yaw = 0;
        float yaw_bias = 0;
        float pitch_bias = 0;
        float orbit_yaw = 0;
        float orbit_pitch = 0;
        float speed_factor = 0;
        double wind_time = 0;
        bool initialized = false;

        void Update(const math::Vector3& car_position, const math::Vector3& car_forward,
                    float speed, float dt, bool wind_enabled)
        {
            dt = std::isfinite(dt) ? std::max(dt, 0.0f) : 0.0f;
            speed = std::isfinite(speed) ? std::max(speed, 0.0f) : 0.0f;
            const float target_yaw = atan2f(car_forward.x, car_forward.z);
            const float target_speed = std::clamp((speed - 15.0f) / 50.0f, 0.0f, 1.0f);
            // Respawns/teleports must not carry the old heading into the new shot.
            const float teleport_distance = std::max(20.0f, speed * dt * 3.0f);
            if (!initialized || (car_position - previous_car_position).LengthSquared() > teleport_distance * teleport_distance)
            {
                yaw = target_yaw;
                speed_factor = target_speed;
                orbit_yaw = yaw_bias;
                orbit_pitch = pitch_bias;
                wind_time = 0;
                initialized = true;
            }
            previous_car_position = car_position;
            const float yaw_error = std::remainder(target_yaw - yaw, 2.0f * math::pi);
            yaw = std::remainder(yaw + yaw_error * (1.0f - expf(-5.0f * dt)), 2.0f * math::pi);
            speed_factor += (target_speed - speed_factor) * (1.0f - expf(-2.0f * dt));
            const float orbit_blend = 1.0f - expf(-12.0f * dt);
            orbit_yaw = std::remainder(orbit_yaw + std::remainder(yaw_bias - orbit_yaw, 2.0f * math::pi) * orbit_blend, 2.0f * math::pi);
            orbit_pitch += (pitch_bias - orbit_pitch) * orbit_blend;

            // Orbit about the same focal point at every azimuth and elevation.
            // Neither speed nor tire slip changes the radius or lens.
            constexpr float distance = 5.0f;
            constexpr float height = 1.5f;
            constexpr float focus_height = 0.6f;
            const float radius = sqrtf(distance * distance + (height - focus_height) * (height - focus_height));
            const float pitch = std::clamp(atan2f(height - focus_height, distance) + orbit_pitch, 0.04f, 1.35f);
            const float heading = yaw + orbit_yaw;
            const math::Vector3 backward(-sinf(heading), 0, -cosf(heading));
            const math::Vector3 right(cosf(heading), 0, -sinf(heading));
            const math::Vector3 up(0, 1, 0);
            look_at = car_position + up * focus_height;
            position = look_at + backward * (radius * cosf(pitch)) + up * (radius * sinf(pitch));

            // Independent, continuous angular buffeting. Integrating time avoids
            // phase jumps from multiplying absolute engine time by changing speed.
            // It never feeds back into follow state, distance, or field of view.
            wind_time += dt;
            if (wind_enabled)
            {
                const float pressure = speed_factor * speed_factor;
                const float horizontal = static_cast<float>(0.65 * sin(wind_time * 17.0) + 0.35 * sin(wind_time * 29.3));
                const float vertical = static_cast<float>(0.60 * sin(wind_time * 23.0) + 0.40 * sin(wind_time * 37.7));
                const math::Vector3 camera_up = up * cosf(pitch) - backward * sinf(pitch);
                look_at += right * (horizontal * pressure * 0.04f) + camera_up * (vertical * pressure * 0.025f);
            }
        }
    };
}
