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
#include "CarPacejka.h"
//==========================================

// drivetrain: engine torque curve, turbo boost, automatic/manual gearbox with
// rev-match downshifts, clutch, driveshaft compliance, differentials, traction
// control, engine braking, service brakes, and the apply_drivetrain orchestrator
// that runs once per tick.

namespace car
{
    inline void update_boost(float throttle, float rpm, float dt)
    {
        if (!tuning::spec.turbo_enabled)
        {
            boost_pressure = lerp(boost_pressure, 0.0f, exp_decay(tuning::spec.boost_spool_rate * 3.0f, dt));
            return;
        }

        float target = 0.0f;
        if (throttle > 0.3f && rpm > tuning::spec.boost_min_rpm)
        {
            target = tuning::spec.boost_max_pressure * PxMin((rpm - tuning::spec.boost_min_rpm) / 4000.0f, 1.0f);

            if (rpm > tuning::spec.boost_wastegate_rpm)
                target *= PxMax(0.0f, 1.0f - (rpm - tuning::spec.boost_wastegate_rpm) / 2000.0f);
        }

        float rate = (target > boost_pressure) ? tuning::spec.boost_spool_rate : tuning::spec.boost_spool_rate * 2.0f;
        boost_pressure = lerp(boost_pressure, target, exp_decay(rate, dt));
    }

    inline float get_engine_torque(float rpm)
    {
        rpm = PxClamp(rpm, tuning::spec.engine_idle_rpm, tuning::spec.engine_max_rpm);

        // breakpoints are relative to the engine's actual operating range
        float idle    = tuning::spec.engine_idle_rpm;
        float peak    = tuning::spec.engine_peak_torque_rpm;
        float redline = tuning::spec.engine_redline_rpm;
        float max_rpm = tuning::spec.engine_max_rpm;

        // split idle-to-peak into three progressive ramp zones
        float ramp_range = peak - idle;
        float bp1 = idle + ramp_range * 0.30f; // low-end spool
        float bp2 = idle + ramp_range * 0.65f; // mid-range build

        float factor;
        if (rpm < bp1)
            factor = 0.55f + ((rpm - idle) / (bp1 - idle)) * 0.15f;
        else if (rpm < bp2)
            factor = 0.70f + ((rpm - bp1) / (bp2 - bp1)) * 0.15f;
        else if (rpm < peak)
            factor = 0.85f + ((rpm - bp2) / (peak - bp2)) * 0.15f;
        else if (rpm < redline)
        {
            float t = (rpm - peak) / (redline - peak);
            factor = 1.0f - t * t * 0.20f;
        }
        else
            factor = 0.80f * (1.0f - ((rpm - redline) / (max_rpm - redline)) * 0.8f);

        return tuning::spec.engine_peak_torque * factor;
    }

    inline float wheel_rpm_to_engine_rpm(float wheel_rpm, int gear)
    {
        if (gear < 0 || gear >= tuning::spec.gear_count || gear == 1)
            return tuning::spec.engine_idle_rpm;
        return fabsf(wheel_rpm * tuning::spec.gear_ratios[gear] * tuning::spec.final_drive);
    }

    inline float get_upshift_speed(int from_gear, float throttle)
    {
        if (from_gear < 2 || from_gear >= tuning::spec.gear_count - 1) return 999.0f;
        float t = PxClamp((throttle - 0.3f) / 0.5f, 0.0f, 1.0f);
        return tuning::spec.upshift_speed_base[from_gear] + t * (tuning::spec.upshift_speed_sport[from_gear] - tuning::spec.upshift_speed_base[from_gear]);
    }

    inline float get_downshift_speed(int gear)
    {
        return (gear >= 2 && gear < tuning::spec.gear_count) ? tuning::spec.downshift_speeds[gear] : 0.0f;
    }

    inline void update_automatic_gearbox(float dt, float throttle, float forward_speed)
    {
        if (shift_cooldown > 0.0f)
            shift_cooldown -= dt;

        if (is_shifting)
        {
            shift_timer -= dt;
            if (shift_timer <= 0.0f)
            {
                is_shifting = false;
                shift_timer = 0.0f;
                shift_cooldown = shift_cooldown_time;
            }
            return;
        }

        if (tuning::spec.manual_transmission)
            return;

        float speed_kmh = forward_speed * 3.6f;

        // reverse
        if (forward_speed < -1.0f && input.brake > 0.1f && throttle < 0.1f && current_gear != 0)
        {
            current_gear = 0;
            is_shifting = true;
            shift_timer = tuning::spec.shift_time * 2.0f;
            last_shift_direction = -1;
            return;
        }

        // neutral to first: clutch engagement, no shift delay
        if (current_gear == 1 && throttle > 0.1f && forward_speed >= -0.5f)
        {
            current_gear = 2;
            last_shift_direction = 1;
            return;
        }

        // reverse to first
        if (current_gear == 0)
        {
            if (throttle > 0.1f || forward_speed > 0.5f)
            {
                current_gear = 2;
                is_shifting = true;
                shift_timer = tuning::spec.shift_time * 2.0f;
                last_shift_direction = 1;
                return;
            }
        }

        // forward gears
        if (current_gear >= 2)
        {
            bool can_shift = shift_cooldown <= 0.0f;

            float upshift_threshold = get_upshift_speed(current_gear, throttle);
            if (last_shift_direction == -1)
                upshift_threshold += 10.0f;

            bool speed_trigger = speed_kmh > upshift_threshold;
            bool rpm_trigger   = engine_rpm > tuning::spec.shift_up_rpm;

            // track how long the engine has been sitting at redline
            if (engine_rpm > tuning::spec.shift_up_rpm)
                redline_hold_timer += dt;
            else
                redline_hold_timer = 0.0f;

            // force upshift after 0.5s at redline despite wheelspin
            if (rpm_trigger && !speed_trigger)
            {
                // gear-scaled slip threshold
                float slip_threshold = (current_gear <= 3) ? 0.50f : 0.25f;

                float avg_slip = 0.0f;
                int grounded_count = 0;
                for (int i = 0; i < wheel_count; i++)
                {
                    if (is_driven(i) && wheels[i].grounded)
                    {
                        avg_slip += fabsf(wheels[i].slip_ratio);
                        grounded_count++;
                    }
                }
                if (grounded_count > 0)
                    avg_slip /= (float)grounded_count;

                // block upshift during wheelspin, but not past the redline timer
                if (avg_slip > slip_threshold && redline_hold_timer < 0.5f)
                    rpm_trigger = false;
            }

            if (can_shift && (speed_trigger || rpm_trigger) && current_gear < tuning::spec.gear_count - 1 && throttle > 0.1f)
            {
                current_gear++;
                is_shifting = true;
                shift_timer = tuning::spec.shift_time;
                last_shift_direction = 1;
                return;
            }

            float downshift_threshold = get_downshift_speed(current_gear);
            if (last_shift_direction == 1)
                downshift_threshold -= 10.0f;

            if (can_shift && speed_kmh < downshift_threshold && current_gear > 2)
            {
                current_gear--;
                is_shifting = true;
                shift_timer = tuning::spec.shift_time;
                last_shift_direction = -1;
                downshift_blip_timer = tuning::spec.downshift_blip_duration;
                return;
            }

            // kickdown: only from cruise (below peak torque, no wheelspin)
            if (can_shift && throttle > 0.9f && current_gear > 2 && engine_rpm < tuning::spec.engine_peak_torque_rpm)
            {
                float avg_slip = 0.0f;
                int grounded = 0;
                for (int i = 0; i < wheel_count; i++)
                {
                    if (is_driven(i) && wheels[i].grounded)
                    {
                        avg_slip += fabsf(wheels[i].slip_ratio);
                        grounded++;
                    }
                }
                if (grounded > 0)
                    avg_slip /= (float)grounded;

                if (avg_slip < 0.15f)
                {
                    int target = current_gear;
                    for (int g = current_gear - 1; g >= 2; g--)
                    {
                        float ratio = fabsf(tuning::spec.gear_ratios[g]) * tuning::spec.final_drive;
                        float driven_r = (tuning::spec.drivetrain_type == 1) ? cfg.front_wheel_radius : cfg.rear_wheel_radius;
                        float potential_rpm = (forward_speed / driven_r) * (60.0f / (2.0f * PxPi)) * ratio;
                        if (potential_rpm < tuning::spec.shift_up_rpm * 0.85f)
                            target = g;
                        else
                            break;
                    }

                    if (target < current_gear)
                    {
                        current_gear = target;
                        is_shifting = true;
                        shift_timer = tuning::spec.shift_time;
                        last_shift_direction = -1;
                        downshift_blip_timer = tuning::spec.downshift_blip_duration;
                    }
                }
            }
        }
    }

    inline const char* get_gear_string()
    {
        static const char* names[] = { "R", "N", "1", "2", "3", "4", "5", "6", "7" };
        return (current_gear >= 0 && current_gear < tuning::spec.gear_count) ? names[current_gear] : "?";
    }

    // apply differential torque to a single axle (left/right wheel pair)
    inline void apply_axle_diff(int left, int right, float axle_torque, float dt)
    {
        if (tuning::spec.diff_type == 0)
        {
            wheels[left].net_torque  += axle_torque * 0.5f;
            wheels[right].net_torque += axle_torque * 0.5f;
        }
        else if (tuning::spec.diff_type == 1)
        {
            float avg_w = (wheels[left].angular_velocity + wheels[right].angular_velocity) * 0.5f;
            wheels[left].angular_velocity  = avg_w;
            wheels[right].angular_velocity = avg_w;
            wheels[left].net_torque  += axle_torque * 0.5f;
            wheels[right].net_torque += axle_torque * 0.5f;
        }
        else
        {
            float w_left  = wheels[left].angular_velocity;
            float w_right = wheels[right].angular_velocity;
            float delta_w = w_left - w_right;

            // smoothstep ramp (instead of a hard 0.5 rad/s gate) avoids on/off chatter
            // when the wheels oscillate around the threshold
            float ramp = PxClamp(fabsf(delta_w) / 0.5f, 0.0f, 1.0f);
            float smooth_ramp = ramp * ramp * (3.0f - 2.0f * ramp);
            float effective_delta = delta_w * smooth_ramp;

            // torque-dependent lock, capped so it never exceeds the applied torque itself
            float lock_ratio = (axle_torque >= 0.0f) ? tuning::spec.lsd_lock_ratio_accel : tuning::spec.lsd_lock_ratio_decel;
            float torque_lock = fabsf(effective_delta) * lock_ratio * fabsf(axle_torque);
            torque_lock = PxMin(torque_lock, fabsf(axle_torque) * 0.9f);

            // preload is the static friction of the plate pack - it resists differential motion
            // regardless of input torque, so it must never be scaled by axle_torque
            float lock_torque = tuning::spec.lsd_preload * smooth_ramp + torque_lock;
            float bias_sign = (delta_w > 0.0f) ? -1.0f : 1.0f;

            wheels[left].net_torque  += axle_torque * 0.5f + bias_sign * lock_torque * 0.5f;
            wheels[right].net_torque += axle_torque * 0.5f - bias_sign * lock_torque * 0.5f;
        }
    }

    // route torque to driven axle(s) based on drivetrain layout
    inline void apply_drive_torque(float total_torque, float dt)
    {
        if (tuning::spec.drivetrain_type == 2)
        {
            // awd - center diff torque split
            float front_torque = total_torque * tuning::spec.torque_split_front;
            float rear_torque  = total_torque * (1.0f - tuning::spec.torque_split_front);
            apply_axle_diff(front_left, front_right, front_torque, dt);
            apply_axle_diff(rear_left,  rear_right,  rear_torque,  dt);
        }
        else if (tuning::spec.drivetrain_type == 1)
        {
            // fwd
            apply_axle_diff(front_left, front_right, total_torque, dt);
        }
        else
        {
            // rwd
            apply_axle_diff(rear_left, rear_right, total_torque, dt);
        }
    }

    // target engine rpm during the current tick given driven-wheel speed & gear/clutch state
    inline float compute_target_engine_rpm(float wheel_driven_rpm)
    {
        float blip = (downshift_blip_timer > 0.0f) ? tuning::spec.downshift_blip_amount * (downshift_blip_timer / tuning::spec.downshift_blip_duration) : 0.0f;
        float effective_throttle_for_rpm = PxMax(input.throttle, blip);
        float free_rev_rpm = tuning::spec.engine_idle_rpm + effective_throttle_for_rpm * (tuning::spec.engine_redline_rpm - tuning::spec.engine_idle_rpm) * 0.7f;

        if (is_in_neutral())
            return free_rev_rpm;

        // throttle floor decays with clutch to avoid decoupling engine from wheels
        float throttle_floor = tuning::spec.engine_idle_rpm + effective_throttle_for_rpm * 500.0f * (1.0f - clutch * 0.8f);
        return PxMax(wheel_driven_rpm, throttle_floor);
    }

    inline void update_engine_rpm(float target_rpm, float dt)
    {
        // first-order model of the crankshaft as a rotating inertia with internal friction:
        //   inertia * d(rpm)/dt = drive_torque_to_target - friction_torque
        // collapsed into a first-order lag whose time constant is shaped by
        // engine_inertia (rev hang on lift) and engine_friction (faster decay on lift)
        float rpm_diff = target_rpm - engine_rpm;
        float smoothing_rate;
        if (rpm_diff >= 0.0f)
        {
            smoothing_rate = tuning::spec.engine_rpm_smoothing;
        }
        else
        {
            constexpr float friction_rpm_decay_gain = 20.0f;
            float friction_term = 1.0f + tuning::spec.engine_friction * friction_rpm_decay_gain;
            smoothing_rate      = tuning::spec.engine_rpm_smoothing * friction_term / (1.0f + tuning::spec.engine_inertia);
        }
        engine_rpm = lerp(engine_rpm, target_rpm, exp_decay(smoothing_rate, dt));
        engine_rpm = PxClamp(engine_rpm, tuning::spec.engine_idle_rpm, tuning::spec.engine_max_rpm);
    }

    // engine braking: torque pushed into net_torque and integrated once in apply_tire_forces
    inline void apply_engine_braking(int driven_count)
    {
        engine_brake_torque = 0.0f;
        if (driven_count <= 0) return;
        if (input.throttle > tuning::spec.input_deadzone) return;
        if (clutch <= 0.5f || !is_in_forward_gear()) return;

        float eb_total = tuning::spec.engine_friction * engine_rpm * 0.1f * fabsf(tuning::spec.gear_ratios[current_gear]) * tuning::spec.final_drive;
        engine_brake_torque = eb_total;
        float share = eb_total / (float)driven_count;
        for (int i = 0; i < wheel_count; i++)
        {
            if (!is_driven(i)) continue;
            float s = (wheels[i].angular_velocity > 0.0f) ? -1.0f : (wheels[i].angular_velocity < 0.0f) ? 1.0f : 0.0f;
            wheels[i].net_torque += s * share;
        }
    }

    // traction control: reduces engine torque when any driven wheel is exceeding slip threshold.
    // returns the (possibly reduced) engine torque
    inline float apply_traction_control(float engine_torque, float forward_speed_ms, float dt)
    {
        tc_active = false;
        if (!tuning::spec.tc_enabled)
        {
            tc_reduction = 0.0f;
            return engine_torque;
        }

        float ground_v = PxMax(fabsf(forward_speed_ms), 0.1f);
        float max_slip = 0.0f;
        for (int i = 0; i < wheel_count; i++)
        {
            if (!is_driven(i) || !wheels[i].grounded) continue;
            float wheel_v = fabsf(wheels[i].angular_velocity * cfg.wheel_radius_for(i));
            float raw_slip = (wheel_v - ground_v) / PxMax(wheel_v, ground_v);
            if (raw_slip > 0.0f)
                max_slip = PxMax(max_slip, raw_slip);
        }

        float target_reduction = 0.0f;
        if (max_slip > tuning::spec.tc_slip_threshold)
        {
            tc_active = true;
            target_reduction = PxClamp((max_slip - tuning::spec.tc_slip_threshold) * 5.0f, 0.0f, tuning::spec.tc_power_reduction);
        }

        tc_reduction = lerp(tc_reduction, target_reduction, exp_decay(tuning::spec.tc_response_rate, dt));
        return engine_torque * (1.0f - tc_reduction);
    }

    // forward-drive torque path: evaluate engine torque -> traction control -> driveshaft
    // compliance -> differential. returns the torque ultimately sent to the axles.
    inline void apply_forward_drive_torque(float forward_speed_ms, float dt)
    {
        float base_torque    = get_engine_torque(engine_rpm);
        float boosted_torque = base_torque * (1.0f + boost_pressure * tuning::spec.boost_torque_mult);
        float engine_torque  = rev_limiter_active ? 0.0f : boosted_torque * input.throttle;

        engine_torque = apply_traction_control(engine_torque, forward_speed_ms, dt);

        float gear_ratio   = tuning::spec.gear_ratios[current_gear] * tuning::spec.final_drive;
        float rigid_torque = engine_torque * gear_ratio * clutch * tuning::spec.drivetrain_efficiency;

        // driveshaft torsional compliance: 1st torsional mode ~30-50 hz, fixed rate
        float stiffness = tuning::spec.driveshaft_stiffness;
        if (stiffness > 0.0f)
        {
            float target_twist = rigid_torque / stiffness;
            constexpr float twist_rate = 50.0f;
            driveshaft_twist = lerp(driveshaft_twist, target_twist, exp_decay(twist_rate, dt));
            float wheel_torque = driveshaft_twist * stiffness;
            apply_drive_torque(wheel_torque, dt);
        }
        else
        {
            apply_drive_torque(rigid_torque, dt);
        }
    }

    inline void apply_reverse_drive_torque(float dt)
    {
        float base_torque    = get_engine_torque(engine_rpm);
        float boosted_torque = base_torque * (1.0f + boost_pressure * tuning::spec.boost_torque_mult);
        float engine_torque  = boosted_torque * input.brake * tuning::spec.reverse_power_ratio;
        float gear_ratio     = tuning::spec.gear_ratios[0] * tuning::spec.final_drive;
        float wheel_torque   = engine_torque * gear_ratio * clutch * tuning::spec.drivetrain_efficiency;
        apply_drive_torque(wheel_torque, dt);
    }

    inline void relax_drivetrain(float dt)
    {
        driveshaft_twist = lerp(driveshaft_twist, 0.0f, exp_decay(10.0f, dt));
        tc_reduction       = lerp(tc_reduction, 0.0f, exp_decay(tuning::spec.tc_response_rate * 2.0f, dt));
        tc_active          = false;
    }

    // service brakes: writes brake torque into net_torque (integrated once in apply_tire_forces),
    // while also accumulating brake heat and toggling abs per wheel. returns true if any brake
    // torque was applied this tick.
    inline void apply_service_brakes(float forward_speed_kmh, float forward_speed_ms, float dt)
    {
        if (input.brake <= tuning::spec.input_deadzone)
        {
            for (int i = 0; i < wheel_count; i++) abs_active[i] = false;
            return;
        }

        if (forward_speed_kmh > tuning::spec.braking_speed_threshold)
        {
            float avg_r = (cfg.front_wheel_radius + cfg.rear_wheel_radius) * 0.5f;
            float total_torque = tuning::spec.brake_force * avg_r * input.brake;
            float front_t = total_torque * tuning::spec.brake_bias_front * 0.5f;
            float rear_t  = total_torque * (1.0f - tuning::spec.brake_bias_front) * 0.5f;

            abs_phase += tuning::spec.abs_pulse_frequency * dt;
            if (abs_phase > 1.0f) abs_phase -= 1.0f;

            for (int i = 0; i < wheel_count; i++)
            {
                float t = is_front(i) ? front_t : rear_t;

                float brake_efficiency = get_brake_efficiency(wheels[i].brake_temp);
                t *= brake_efficiency;

                float heat = fabsf(wheels[i].angular_velocity) * t * tuning::spec.brake_heat_coefficient * dt;
                wheels[i].brake_temp = PxMin(wheels[i].brake_temp + heat, tuning::spec.brake_max_temp);

                abs_active[i] = false;
                if (tuning::spec.abs_enabled && wheels[i].grounded && -wheels[i].slip_ratio > tuning::spec.abs_slip_threshold)
                {
                    abs_active[i] = true;
                    t *= (abs_phase < 0.5f) ? tuning::spec.abs_release_rate : 1.0f;
                }

                // push brake torque into net_torque: the single semi-implicit integration in
                // apply_tire_forces handles the actual spin-down and sign-reversal lock
                float sign = (wheels[i].angular_velocity > 0.0f) ? -1.0f : (wheels[i].angular_velocity < 0.0f) ? 1.0f : 0.0f;
                wheels[i].net_torque += sign * t;
            }
        }
        else
        {
            for (int i = 0; i < wheel_count; i++) abs_active[i] = false;

            if (is_in_reverse())
            {
                float engine_torque = get_engine_torque(engine_rpm) * input.brake * tuning::spec.reverse_power_ratio;
                float gear_ratio    = tuning::spec.gear_ratios[0] * tuning::spec.final_drive;
                apply_drive_torque(engine_torque * gear_ratio * clutch, dt);
            }
            // reverse request: full stop + brake hold while in forward gear
            else if (fabsf(forward_speed_ms) < 0.5f && input.brake > 0.8f && input.throttle < tuning::spec.input_deadzone && is_in_forward_gear() && !is_shifting)
            {
                current_gear = 0;
                is_shifting  = true;
                shift_timer  = tuning::spec.shift_time * 2.0f;
            }
        }
    }

    inline void apply_drivetrain(float forward_speed_kmh, float dt)
    {
        float forward_speed_ms = forward_speed_kmh / 3.6f;

        update_automatic_gearbox(dt, input.throttle, forward_speed_ms);

        if (downshift_blip_timer > 0.0f)
            downshift_blip_timer -= dt;

        // average angular velocity of driven wheels (used for rpm tracking)
        float driven_w_sum = 0.0f;
        int driven_count = 0;
        for (int i = 0; i < wheel_count; i++)
        {
            if (is_driven(i)) { driven_w_sum += wheels[i].angular_velocity; driven_count++; }
        }
        float avg_wheel_rpm    = (driven_count > 0 ? driven_w_sum / driven_count : 0.0f) * 60.0f / (2.0f * PxPi);
        float wheel_driven_rpm = wheel_rpm_to_engine_rpm(fabsf(avg_wheel_rpm), current_gear);

        bool coasting = input.throttle < tuning::spec.input_deadzone && input.brake < tuning::spec.input_deadzone;
        if (coasting && is_in_forward_gear())
        {
            float driven_r          = (tuning::spec.drivetrain_type == 1) ? cfg.front_wheel_radius : cfg.rear_wheel_radius;
            float ground_wheel_rpm  = fabsf(forward_speed_ms) / driven_r * 60.0f / (2.0f * PxPi);
            float ground_driven_rpm = wheel_rpm_to_engine_rpm(ground_wheel_rpm, current_gear);
            wheel_driven_rpm        = PxMax(wheel_driven_rpm, ground_driven_rpm);
        }

        // clutch schedule
        if (is_shifting)                                                  clutch = 0.8f;
        else if (is_in_neutral())                                         clutch = 0.0f;
        else if (fabsf(forward_speed_ms) < 2.0f && input.throttle > 0.1f) clutch = lerp(clutch, 1.0f, exp_decay(tuning::spec.clutch_engagement_rate, dt));
        else                                                              clutch = 1.0f;

        update_engine_rpm(compute_target_engine_rpm(wheel_driven_rpm), dt);

        apply_engine_braking(driven_count);
        update_boost(input.throttle, engine_rpm, dt);

        // rev limiter hysteresis
        if (engine_rpm >= tuning::spec.engine_redline_rpm)
            rev_limiter_active = true;
        else if (engine_rpm < tuning::spec.engine_redline_rpm - 200.0f)
            rev_limiter_active = false;

        // drive-torque path selection
        if (input.throttle > tuning::spec.input_deadzone && is_in_forward_gear())
            apply_forward_drive_torque(forward_speed_ms, dt);
        else if (input.brake > tuning::spec.input_deadzone && is_in_reverse())
            apply_reverse_drive_torque(dt);
        else
            relax_drivetrain(dt);

        apply_service_brakes(forward_speed_kmh, forward_speed_ms, dt);

        // handbrake is handled exclusively inside apply_tire_forces (force + torque),
        // coasting wheel sync lives there too
    }
}
