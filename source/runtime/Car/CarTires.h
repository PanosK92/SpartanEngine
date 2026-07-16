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

// tire forces and self-aligning torque. this is the main coupling point between
// the chassis and the ground: it produces per-wheel lateral / longitudinal forces
// from slip, runs the 3-zone thermal model, applies tire wear, and performs the
// single semi-implicit Euler integration of wheel angular velocity that every
// other torque source (engine, diff, brakes, handbrake) writes into.

namespace car
{
    inline void apply_tire_forces(float dt)
    {
        // --- setup ---
        PxTransform pose = body->getGlobalPose();
        PxVec3 chassis_right = pose.q.rotate(PxVec3(1, 0, 0));

        if (tuning::log_pacejka)
        {
            SP_LOG_INFO("=== tire forces: speed=%.1f m/s ===", body->getLinearVelocity().magnitude());
        }

        for (int i = 0; i < wheel_count; i++)
        {
            wheel& w = wheels[i];
            const char* wheel_name = wheel_names[i];
            PxRigidDynamic* wheel_actor = multibody.corners[i].wheel_body;
            PxVec3 wheel_axis = wheel_actor ? wheel_actor->getGlobalPose().q.rotate(PxVec3(1.0f, 0.0f, 0.0f)) : chassis_right;
            if (wheel_actor)
            {
                w.angular_velocity = wheel_actor->getAngularVelocity().dot(wheel_axis);
            }

            // floor wr and the wheel moi so the divisions in the airborne branch, the slip
            // calculation, the semi implicit euler step and the ground match step can never
            // be 0 / 0 or x / 0. without this a single bad wheel radius poisons the entire sim
            float wr_raw  = cfg.wheel_radius_for(i);
            float wr      = (std::isfinite(wr_raw) && wr_raw > 0.0f) ? PxMax(wr_raw, 0.05f) : 0.34f;
            float wmoi    = (std::isfinite(wheel_moi[i]) && wheel_moi[i] > 0.0f) ? wheel_moi[i] : 1.0f;
            float defl = 0.0f;
            if (tuning::spec.tire_vertical_stiffness > 1000.0f)
            {
                defl = PxClamp(w.tire_load / tuning::spec.tire_vertical_stiffness, 0.0f, 0.05f);
            }
            float wr_eff = PxMax(wr - defl * 0.55f, 0.05f);

            float camb = is_front(i) ? tuning::spec.front_camber : tuning::spec.rear_camber;
            float g = is_front(i) ? tuning::spec.front_camber_gain : tuning::spec.rear_camber_gain;
            float dyn_camb = camb + g * w.compression * cfg.suspension_travel;
            if (wheel_actor)
            {
                PxVec3 chassis_up = pose.q.rotate(PxVec3(0.0f, 1.0f, 0.0f));
                float measured_camber = asinf(PxClamp(wheel_axis.dot(chassis_up), -1.0f, 1.0f));
                dyn_camb = i == front_left || i == rear_left ? -measured_camber : measured_camber;
            }

            w.effective_radius = wr_eff;
            w.dynamic_camber   = dyn_camb;

            // airborne branch: no tire force, but drivetrain and brake torque already sitting in
            // net_torque still integrate so mid air throttle spins the wheels and brakes stop them
            if (!w.grounded || w.tire_load <= 0.0f)
            {
                if (tuning::log_pacejka)
                {
                    SP_LOG_INFO("[%s] airborne: grounded=%d, tire_load=%.1f", wheel_name, w.grounded, w.tire_load);
                }
                w.slip_angle = w.slip_ratio = w.lateral_force = w.longitudinal_force = 0.0f;

                w.net_torque -= w.angular_velocity * tuning::spec.bearing_friction * wmoi;
                if (input.handbrake > tuning::spec.input_deadzone && is_rear(i))
                {
                    float hb_sign = (w.angular_velocity > 0.0f) ? -1.0f : (w.angular_velocity < 0.0f) ? 1.0f : 0.0f;
                    w.net_torque += hb_sign * tuning::spec.handbrake_torque * input.handbrake;
                }

                if (wheel_actor)
                {
                    safe_add_torque(wheel_actor, wheel_axis * w.net_torque);
                }

                float spin_retain  = powf(PxClamp(tuning::spec.airborne_wheel_decay, 0.0f, 1.0f), dt * 200.0f);
                if (wheel_actor)
                {
                    PxVec3 angular_velocity = wheel_actor->getAngularVelocity();
                    PxVec3 axle_velocity = wheel_axis * angular_velocity.dot(wheel_axis) * spin_retain;
                    wheel_actor->setAngularVelocity(axle_velocity);
                    w.angular_velocity = axle_velocity.dot(wheel_axis);
                }

                // airborne cooling: all zones cool at 3x rate
                for (int z = 0; z < 3; z++)
                {
                    float s_above = w.thermal.surface[z] - tuning::spec.tire_ambient_temp;
                    if (s_above > 0.0f)
                    {
                        w.thermal.surface[z] -= tuning::spec.tire_cooling_rate * 3.0f * s_above / 30.0f * dt;
                    }
                    w.thermal.surface[z] = PxMax(w.thermal.surface[z], tuning::spec.tire_ambient_temp);
                }
                float c_above = w.thermal.core - tuning::spec.tire_ambient_temp;
                if (c_above > 0.0f)
                {
                    w.thermal.core -= tuning::spec.tire_cooling_rate * 1.0f * c_above / 30.0f * dt;
                }
                w.thermal.core = PxMax(w.thermal.core, tuning::spec.tire_ambient_temp);
                w.rotation += w.angular_velocity * dt;
                continue;
            }

            PxVec3 world_pos = wheel_actor ? wheel_actor->getGlobalPose().p : pose.transform(wheel_offsets[i]);
            PxVec3 wheel_vel = wheel_actor ? wheel_actor->getLinearVelocity() : body->getLinearVelocity() + body->getAngularVelocity().cross(world_pos - pose.p);
            wheel_vel -= w.contact_normal * wheel_vel.dot(w.contact_normal);

            PxVec3 wheel_lat = wheel_axis;
            PxVec3 wheel_fwd = wheel_lat.cross(w.contact_normal);

            // keep tire forces in the contact plane so body roll cannot turn lateral grip into lift
            wheel_fwd -= w.contact_normal * wheel_fwd.dot(w.contact_normal);
            wheel_lat -= w.contact_normal * wheel_lat.dot(w.contact_normal);
            if (wheel_fwd.magnitudeSquared() > 1e-6f)
            {
                wheel_fwd.normalize();
            }
            if (wheel_lat.magnitudeSquared() > 1e-6f)
            {
                wheel_lat.normalize();
            }
            // re-orthogonalise after projection
            wheel_lat = w.contact_normal.cross(wheel_fwd);
            if (wheel_lat.magnitudeSquared() > 1e-6f)
            {
                wheel_lat.normalize();
            }
            wheel_fwd = wheel_lat.cross(w.contact_normal);
            if (wheel_fwd.magnitudeSquared() > 1e-6f)
            {
                wheel_fwd.normalize();
            }

            float vx = wheel_vel.dot(wheel_fwd);
            float vy = wheel_vel.dot(wheel_lat);
            float wheel_speed  = w.angular_velocity * wr_eff;
            float ground_speed = sqrtf(vx * vx + vy * vy);
            float slip_v_long = fabsf(wheel_speed - vx);
            float slip_v_lat  = fabsf(vy);
            float slip_v      = sqrtf(slip_v_long * slip_v_long + slip_v_lat * slip_v_lat);
            // include lateral motion, otherwise a sideways slide reads as at rest and
            // falls back to the static friction model instead of the pacejka curves
            float max_v = PxMax(fabsf(wheel_speed), ground_speed);

            if (tuning::log_pacejka)
            {
                SP_LOG_INFO("[%s] vx=%.3f, vy=%.3f, ws=%.3f", wheel_name, vx, vy, wheel_speed);
            }

            float wear_factor    = 1.0f - w.wear * tuning::spec.tire_grip_wear_loss;
            float base_grip      = tuning::spec.tire_friction * load_sensitive_grip(PxMax(w.tire_load, 0.0f));
            float temp_factor    = get_tire_temp_grip_factor(w.thermal.avg_surface());
            float camber_factor  = get_camber_grip_factor(dyn_camb);
            float surface_factor = get_surface_friction(w.contact_surface);
            // rear_grip_ratio is a per axle mu scale for tire compound differences, 1.0 means matched
            float axle_grip_scale = is_rear(i) ? tuning::spec.rear_grip_ratio : 1.0f;
            // camber mainly costs lateral grip, temp and wear apply to both directions without a floor
            float shared_grip     = base_grip * surface_factor * axle_grip_scale;
            float long_grip_scale = temp_factor * wear_factor;
            float lat_grip_scale  = temp_factor * wear_factor * camber_factor;
            float peak_force_long = shared_grip * long_grip_scale;
            float peak_force_lat  = shared_grip * lat_grip_scale;

            if (tuning::log_pacejka)
            {
                SP_LOG_INFO("[%s] load=%.0f, peak_long=%.0f, peak_lat=%.0f", wheel_name, w.tire_load, peak_force_long, peak_force_lat);
            }

            float lat_f = 0.0f, long_f = 0.0f;

            // smoothstep blend: 0 at rest, 1 at full speed, smooth transition in between
            float blend_lo = 0.1f;
            float blend_hi = tuning::spec.min_slip_speed;
            float blend_t  = PxClamp((max_v - blend_lo) / PxMax(blend_hi - blend_lo, 0.01f), 0.0f, 1.0f);
            float pacejka_weight = blend_t * blend_t * (3.0f - 2.0f * blend_t);

            // static friction model (dominant at rest / very low speed)
            float friction_gain = cfg.mass * static_friction_gain_per_kg;
            float static_lat_f  = PxClamp(-vy * friction_gain, -peak_force_lat * 0.8f, peak_force_lat * 0.8f);
            float static_long_f = PxClamp(-vx * friction_gain, -peak_force_long * 0.8f, peak_force_long * 0.8f);

            // slip calculation (always runs, values approach zero smoothly at low speed)
            float abs_vx = fabsf(vx);
            float abs_ws = fabsf(wheel_speed);
            float rest_speed = PxMax(ground_speed, abs_ws);
            float slip_denom = PxMax(PxMax(abs_vx, abs_ws), 0.01f);
            float raw_slip_ratio = PxClamp((wheel_speed - vx) / slip_denom, -1.0f, 1.0f);
            float raw_slip_angle = atan2f(vy, PxMax(abs_vx, 0.5f));

            float effective_relaxation = PxMax(tuning::spec.tire_relaxation_length, 0.05f);
            float long_distance = PxMax(ground_speed, abs_ws) * dt;
            float lat_distance  = ground_speed * dt;
            float long_blend = 1.0f - expf(-long_distance / effective_relaxation);
            float lat_blend  = 1.0f - expf(-lat_distance / effective_relaxation);
            w.slip_ratio = lerp(w.slip_ratio, raw_slip_ratio, long_blend);
            w.slip_angle = lerp(w.slip_angle, raw_slip_angle, lat_blend);

            float rest_slip_speed = tuning::spec.min_slip_speed;
            if (rest_speed < rest_slip_speed && input.throttle <= tuning::spec.input_deadzone)
            {
                float rest_t = 1.0f - rest_speed / PxMax(rest_slip_speed, 0.01f);
                float rest_decay = exp_decay(24.0f * rest_t, dt);
                w.slip_ratio = lerp(w.slip_ratio, 0.0f, rest_decay);
                w.slip_angle = lerp(w.slip_angle, 0.0f, rest_decay);

                if (rest_speed < 0.03f)
                {
                    w.slip_ratio = 0.0f;
                    w.slip_angle = 0.0f;
                }
            }

            float effective_slip_angle = w.slip_angle;
            if (fabsf(effective_slip_angle) < tuning::spec.slip_angle_deadband)
            {
                float factor = fabsf(effective_slip_angle) / tuning::spec.slip_angle_deadband;
                effective_slip_angle *= factor * factor;
            }

            float pacejka_slip_angle = PxClamp(effective_slip_angle, -tuning::spec.max_slip_angle, tuning::spec.max_slip_angle);

            // load-dependent B coefficient scaling + pressure effect on cornering stiffness
            float load_norm = w.tire_load / tuning::spec.load_reference;
            float B_load_scale = powf(1.0f / PxMax(load_norm, tuning::spec.load_B_scale_min), 0.4f);
            float pressure_ratio = tuning::spec.tire_pressure / PxMax(tuning::spec.tire_pressure_optimal, 0.1f);
            float pressure_B_scale = 1.0f + (pressure_ratio - 1.0f) * 0.5f;
            float lat_B_eff  = tuning::spec.lat_B * B_load_scale * pressure_B_scale;
            float long_B_eff = tuning::spec.long_B * B_load_scale;

            float long_input = w.slip_ratio;
            float lat_input  = tanf(pacejka_slip_angle);

            // combined slip: normalize each slip by its curve peak, evaluate both curves at the
            // combined magnitude and project back, preserves the falloff shape past the limit
            float s_peak    = pacejka_peak_slip(long_B_eff, tuning::spec.long_C);
            float a_peak    = pacejka_peak_slip(lat_B_eff, tuning::spec.lat_C);
            float norm_long = long_input / PxMax(s_peak, 0.001f);
            float norm_lat  = lat_input / PxMax(a_peak, 0.001f);
            float rho       = sqrtf(norm_long * norm_long + norm_lat * norm_lat);
            float Fx0 = 0.0f;
            float Fy0 = 0.0f;
            if (rho > 1e-4f)
            {
                Fx0 = (norm_long / rho) * pacejka(rho * s_peak, long_B_eff, tuning::spec.long_C, tuning::spec.long_D, tuning::spec.long_E);
                Fy0 = (norm_lat / rho)  * pacejka(rho * a_peak, lat_B_eff,  tuning::spec.lat_C,  tuning::spec.lat_D,  tuning::spec.lat_E);
            }

            float dynamic_lat_f  = -Fy0 * peak_force_lat;
            float dynamic_long_f =  Fx0 * peak_force_long;

            bool is_left_wheel = (i == front_left || i == rear_left);
            float camber_thrust = dyn_camb * w.tire_load * tuning::spec.camber_thrust_coeff;
            dynamic_lat_f += is_left_wheel ? -camber_thrust : camber_thrust;

            // ellipse safety clamp, only needed because camber thrust is added on top
            float ex = dynamic_long_f / PxMax(peak_force_long, 1.0f);
            float ey = dynamic_lat_f / PxMax(peak_force_lat, 1.0f);
            float e  = sqrtf(ex * ex + ey * ey);
            if (e > 1.0f)
            {
                dynamic_long_f /= e;
                dynamic_lat_f  /= e;
            }

            // blend between static friction and pacejka
            lat_f  = static_lat_f  * (1.0f - pacejka_weight) + dynamic_lat_f  * pacejka_weight;
            long_f = static_long_f * (1.0f - pacejka_weight) + dynamic_long_f * pacejka_weight;

            // at very low speed, decay wheel spin toward zero
            if (max_v < blend_hi)
            {
                float rest_blend = 1.0f - pacejka_weight;
                w.angular_velocity = lerp(w.angular_velocity, vx / wr_eff, exp_decay(20.0f * rest_blend, dt));
            }

            if (tuning::log_pacejka)
            {
                SP_LOG_INFO("[%s] blend=%.2f, lat_f=%.1f, long_f=%.1f", wheel_name, pacejka_weight, lat_f, long_f);
            }

            // --- 3-zone surface + core thermal model ---
            // carcass flex peaks when pressure is below optimal; over-inflation should never make
            // the friction-work term negative, so clamp to a small positive floor
            float pressure_heat_mult = PxMax(1.0f + (1.0f - pressure_ratio) * 1.5f, 0.2f);
            float rolling_heat = fabsf(wheel_speed) * tuning::spec.tire_heat_from_rolling * pressure_heat_mult;
            float cooling_air = tuning::spec.tire_cooling_rate + ground_speed * tuning::spec.tire_cooling_airflow;
            float force_magnitude = sqrtf(long_f * long_f + lat_f * lat_f);
            float normalized_force = force_magnitude / tuning::spec.load_reference;
            float slip_ratio_eff = PxClamp(slip_v / PxMax(ground_speed, 0.5f), 0.0f, 2.0f);
            float speed_heat_scale = PxClamp(ground_speed / 2.0f, 0.0f, 1.0f);
            float friction_work = normalized_force * slip_ratio_eff * pacejka_weight * speed_heat_scale;
            float base_heat = friction_work * tuning::spec.tire_heat_from_slip * pressure_heat_mult + rolling_heat;

            // camber determines heat distribution across zones:
            // negative camber loads the inside zone more under cornering
            float camber_bias = dyn_camb * 3.0f;
            float zone_heat[3] = {
                base_heat * (1.0f + camber_bias),  // inside: more heat with negative camber
                base_heat,                          // middle: uniform
                base_heat * (1.0f - camber_bias)   // outside: less heat with negative camber
            };

            float surface_resp = tuning::spec.tire_surface_response;
            float core_rate = tuning::spec.tire_core_transfer_rate;
            for (int z = 0; z < 3; z++)
            {
                float s = w.thermal.surface[z];
                float s_delta = s - tuning::spec.tire_ambient_temp;
                float s_cooling = (s_delta > 0.0f) ? cooling_air * s_delta / 30.0f : 0.0f;
                float core_exchange = core_rate * (w.thermal.core - s);
                w.thermal.surface[z] += (zone_heat[z] * surface_resp - s_cooling + core_exchange) * dt;
                w.thermal.surface[z] = PxClamp(w.thermal.surface[z], tuning::spec.tire_min_temp, tuning::spec.tire_max_temp);
            }

            // core absorbs heat from surface average, cools slowly
            float avg_surf = w.thermal.avg_surface();
            float core_delta = w.thermal.core - tuning::spec.tire_ambient_temp;
            float core_cooling = (core_delta > 0.0f) ? tuning::spec.tire_cooling_rate * 0.3f * core_delta / 30.0f : 0.0f;
            w.thermal.core += (core_rate * (avg_surf - w.thermal.core) - core_cooling) * dt;
            w.thermal.core = PxClamp(w.thermal.core, tuning::spec.tire_min_temp, tuning::spec.tire_max_temp);

            // --- tire wear (per-zone based on local temperature) ---
            float total_wear = 0.0f;
            for (int z = 0; z < 3; z++)
            {
                float zone_excess = PxMax(w.thermal.surface[z] - tuning::spec.tire_optimal_temp, 0.0f) / tuning::spec.tire_temp_range;
                float zone_wear = tuning::spec.tire_wear_rate * (1.0f + zone_excess * tuning::spec.tire_wear_heat_mult);
                total_wear += zone_wear;
            }
            float wear_rate = total_wear / 3.0f;
            float wear_amount = wear_rate * slip_v * dt;
            w.wear = PxMin(w.wear + PxMax(wear_amount, 0.0f), 1.0f);

            if (is_rear(i) && input.handbrake > tuning::spec.input_deadzone)
            {
                float sliding_f = tuning::spec.handbrake_sliding_factor * peak_force_long;
                long_f = (fabsf(vx) > 0.01f) ? ((vx > 0.0f ? -1.0f : 1.0f) * sliding_f * input.handbrake) : 0.0f;
                lat_f *= (1.0f - 0.5f * input.handbrake);
            }

            w.lateral_force = lat_f;
            w.longitudinal_force = long_f;

            PxVec3 fpos = w.grounded ? w.contact_point : world_pos;
            PxVec3 tire_force = wheel_lat * lat_f + wheel_fwd * long_f;
            safe_add_force_at_pos(wheel_actor, tire_force, fpos);
            if (w.contact_actor)
            {
                if (const PxRigidDynamic* ground_actor = w.contact_actor->is<PxRigidDynamic>())
                {
                    safe_add_force_at_pos(const_cast<PxRigidDynamic*>(ground_actor), -tire_force, fpos);
                }
            }

            w.net_torque -= w.angular_velocity * tuning::spec.bearing_friction * wmoi; // bearing drag

            if (is_rear(i) && input.handbrake > tuning::spec.input_deadzone)
            {
                float hb_sign = (w.angular_velocity > 0.0f) ? -1.0f : (w.angular_velocity < 0.0f) ? 1.0f : 0.0f;
                w.net_torque += hb_sign * tuning::spec.handbrake_torque * input.handbrake;
            }

            if (wheel_actor)
            {
                safe_add_torque(wheel_actor, wheel_axis * w.net_torque);
            }

            w.rotation += w.angular_velocity * dt;

            if (tuning::log_pacejka)
            {
                SP_LOG_INFO("[%s] ang_vel=%.4f, lat_f=%.1f, long_f=%.1f", wheel_name, w.angular_velocity, lat_f, long_f);
            }
        }
        if (tuning::log_pacejka)
        {
            SP_LOG_INFO("=== pacejka tick end ===\n");
        }
    }

    inline void apply_self_aligning_torque()
    {
        for (int i = 0; i < wheel_count; i++)
        {
            if (!wheels[i].grounded)
            {
                continue;
            }

            float abs_sa = fabsf(wheels[i].slip_angle);
            float sa_norm = abs_sa / PxMax(tuning::spec.pneumatic_trail_peak, 0.01f);
            float trail   = PxMax(tuning::spec.pneumatic_trail_max * (1.0f - sa_norm), 0.0f);
            PxVec3 torque = wheels[i].contact_normal * (-wheels[i].lateral_force * trail * tuning::spec.self_align_gain);
            safe_add_torque(multibody.corners[i].upright, torque);
        }
    }
}
