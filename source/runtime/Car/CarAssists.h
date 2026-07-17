#pragma once
#include "CarState.h"

// traction control scales engine torque and abs scales each wheel brake torque
namespace car
{
    inline void clear_abs_state()
    {
        for (int i = 0; i < wheel_count; i++)
        {
            abs_active[i] = false;
        }
    }

    inline float get_assisted_steering_target(float raw_input)
    {
        float deadzone = PxClamp(tuning::spec.steering_deadzone, 0.0f, 0.95f);
        float magnitude = fabsf(raw_input);
        float filtered_input = magnitude <= deadzone ? 0.0f : copysignf((magnitude - deadzone) / (1.0f - deadzone), raw_input);
        float speed_kmh = body ? body->getLinearVelocity().magnitude() * 3.6f : 0.0f;
        float speed_factor = PxClamp(speed_kmh / PxMax(tuning::spec.assists.steering_speed_reference, 1.0f), 0.0f, 1.0f);
        float steering_limit = 1.0f - tuning::spec.assists.steering_speed_reduction * speed_factor;
        return PxClamp(filtered_input, -steering_limit, steering_limit);
    }

    inline void update_assist_controller(bool traction_requested, bool braking_requested, float dt)
    {
        assisted_actuators = assist_command();
        tc_active = false;
        if (traction_requested && tuning::spec.tc_enabled && tuning::spec.assists.traction_control_level > 0.0f)
        {
            float max_slip = 0.0f;
            for (int i = 0; i < wheel_count; i++)
            {
                if (is_driven(i) && wheels[i].grounded)
                {
                    max_slip = PxMax(max_slip, wheels[i].slip_ratio);
                }
            }
            float target_reduction = 0.0f;
            if (max_slip > tuning::spec.tc_slip_threshold)
            {
                tc_active = true;
                float reduction_limit = tuning::spec.tc_power_reduction * tuning::spec.assists.traction_control_level;
                target_reduction = PxClamp((max_slip - tuning::spec.tc_slip_threshold) * 5.0f, 0.0f, reduction_limit);
            }
            tc_reduction = lerp(tc_reduction, target_reduction, exp_decay(tuning::spec.tc_response_rate, dt));
        }
        else
        {
            tc_reduction = lerp(tc_reduction, 0.0f, exp_decay(tuning::spec.tc_response_rate * 2.0f, dt));
        }
        assisted_actuators.engine_torque_scale = 1.0f - tc_reduction;

        if (!braking_requested)
        {
            clear_abs_state();
            return;
        }

        abs_phase += tuning::spec.abs_pulse_frequency * dt;
        abs_phase -= floorf(abs_phase);
        for (int i = 0; i < wheel_count; i++)
        {
            abs_active[i] = false;
            if (!tuning::spec.abs_enabled || tuning::spec.assists.abs_level <= 0.0f || !wheels[i].grounded)
            {
                continue;
            }
            float load_factor = PxClamp(wheels[i].tire_load / PxMax(tuning::spec.load_reference, 1000.0f), 0.6f, 1.6f);
            float threshold = tuning::spec.abs_slip_threshold * (1.0f - tuning::spec.abs_load_sensitivity * (load_factor - 1.0f));
            if (-wheels[i].slip_ratio > threshold)
            {
                abs_active[i] = true;
                float release_factor = lerp(1.0f, tuning::spec.abs_release_rate, tuning::spec.assists.abs_level);
                assisted_actuators.brake_torque_scale[i] = abs_phase < 0.5f ? release_factor : 1.0f;
            }
        }
    }
}
