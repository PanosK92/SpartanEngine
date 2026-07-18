#pragma once

#include <cstdio>
#include "../Core/Engine.h"
// validation scenarios may rebuild their simulation assembly
#include "CarState.h"
namespace car
{

    enum class validation_scenario
    {
        settle,
        acceleration,
        braking,
        coastdown,
        skidpad,
        step_steer,
        slalom,
        curb,
        single_wheel_bump,
        count
    };

    struct validation_state
    {
        bool initialized = false;
        bool completed = false;
        bool requested = false;
        validation_scenario scenario = validation_scenario::settle;
        float elapsed = 0.0f;
        float reached_speed_time = -1.0f;
        float max_lateral_g = 0.0f;
        float max_yaw_rate = 0.0f;
        float max_compression = 0.0f;
        float minimum_up = 1.0f;
        bool event_applied = false;
        PxTransform start_pose = PxTransform(PxIdentity);
        PxVec3 scenario_start = PxVec3(0.0f);
        FILE* report = nullptr;
    };

}
