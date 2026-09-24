// Copyright(c) 2015-2026 Panos Karabelas
// Licensed under the Spartan Engine License. See license.md in the repository root.
// https://github.com/PanosK92/SpartanEngine/blob/master/license.md
// Commercial use requires written permission and negotiated payment terms.
#pragma once
#include <algorithm>
#include <cmath>

namespace spartan::island_wildlife
{
    struct Steering
    {
        float yaw = 0;
        float target_yaw = 0;
        float retry = 0;
        bool avoiding = false;
    };

    inline float AngleDelta(float target, float current)
    {
        return std::remainder(target - current, 6.283185307f);
    }

    // The query validates the entire ground segment and records its endpoint.
    // A successful return commits that endpoint; speculative route probes never move us.
    template<typename CanTravel>
    bool AdvanceGround(Steering& steering, float dt, float speed, CanTravel&& can_travel)
    {
        steering.retry = std::max(0.0f, steering.retry - dt);
        if (speed <= 0 || dt <= 0) return false;
        const float turn = std::clamp(AngleDelta(steering.target_yaw, steering.yaw), -2.5f*dt, 2.5f*dt);
        const float yaw = steering.yaw + turn;
        if (can_travel(yaw, speed*dt))
        {
            steering.yaw = yaw;
            if (fabsf(AngleDelta(steering.target_yaw, yaw)) < .01f) steering.avoiding = false;
            return true;
        }
        // Finish turning toward a route we already checked. Do not pick a new
        // direction every frame when the intermediate arc still faces the obstacle.
        if (steering.avoiding && fabsf(AngleDelta(steering.target_yaw, yaw)) >= .01f)
        {
            steering.yaw = yaw;
            return false;
        }
        steering.avoiding = false;
        if (steering.retry > 0) return false;
        steering.retry = .75f;
        for (float offset : {0.0f, .7853982f, -.7853982f, 1.5707963f, -1.5707963f, 2.3561945f, -2.3561945f, 3.1415927f})
        {
            const float candidate = steering.target_yaw + offset;
            if (can_travel(candidate, std::max(.6f, speed*.35f)))
            {
                steering.target_yaw = candidate;
                steering.avoiding = true;
                break;
            }
        }
        // No route: hold the current facing and retry later, even when startled.
        return false;
    }
}
