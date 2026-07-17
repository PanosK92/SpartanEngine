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
// from slip, runs the 3-zone thermal model and applies tire wear
// wheel torques are accumulated here and integrated by the physical wheel actors

namespace car
{
    constexpr float handbrake_lock_force = 1e8f;

    inline void update_handbrake_wheel_locks()
    {
        const bool handbrake_locked = input.handbrake >= 0.999f;
        for (int i : { rear_left, rear_right })
        {
            suspension_corner& corner = multibody.corners[i];
            if (corner.wheel_joint)
            {
                corner.wheel_joint->setDriveVelocity(0.0f);
                corner.wheel_joint->setDriveForceLimit(handbrake_locked ? handbrake_lock_force : 0.0f);
                corner.wheel_joint->setRevoluteJointFlag(PxRevoluteJointFlag::eDRIVE_ENABLED, handbrake_locked);
            }
            if (handbrake_locked && corner.wheel_body)
            {
                const PxVec3 wheel_axis = corner.wheel_body->getGlobalPose().q.rotate(PxVec3(1.0f, 0.0f, 0.0f));
                const PxVec3 angular_velocity = corner.wheel_body->getAngularVelocity();
                corner.wheel_body->setAngularVelocity(angular_velocity - wheel_axis * angular_velocity.dot(wheel_axis));
                wheels[i].angular_velocity = 0.0f;
                wheels[i].net_torque = 0.0f;
            }
        }
    }

    inline void update_tire_slip_state(float dt)
    {
        PxTransform chassis_pose = body->getGlobalPose();
        for (int i = 0; i < wheel_count; i++)
        {
            wheel& w = wheels[i];
            PxRigidDynamic* wheel_actor = multibody.corners[i].wheel_body;
            if (!wheel_actor || !w.grounded || w.tire_load <= 0.0f)
            {
                w.slip_ratio = 0.0f;
                w.slip_angle = 0.0f;
                continue;
            }

            float wheel_radius = PxMax(cfg.wheel_radius_for(i), 0.05f);
            float tire_deflection = tuning::spec.tire_vertical_stiffness > 1000.0f ? PxClamp(w.tire_load / tuning::spec.tire_vertical_stiffness, 0.0f, 0.05f) : 0.0f;
            float effective_radius = PxMax(wheel_radius - tire_deflection * 0.55f, 0.05f);
            PxVec3 wheel_axis = wheel_actor->getGlobalPose().q.rotate(PxVec3(1.0f, 0.0f, 0.0f));
            PxVec3 wheel_forward = wheel_axis.cross(w.contact_normal);
            wheel_forward -= w.contact_normal * wheel_forward.dot(w.contact_normal);
            if (wheel_forward.normalize() < 1e-4f)
            {
                wheel_forward = chassis_pose.q.rotate(PxVec3(0.0f, 0.0f, 1.0f));
            }
            PxVec3 wheel_lateral = w.contact_normal.cross(wheel_forward).getNormalized();
            PxVec3 wheel_velocity = wheel_actor->getLinearVelocity() - ground_point_velocity(w);
            wheel_velocity -= w.contact_normal * wheel_velocity.dot(w.contact_normal);
            float longitudinal_speed = wheel_velocity.dot(wheel_forward);
            float lateral_speed = wheel_velocity.dot(wheel_lateral);
            float surface_speed = w.angular_velocity * effective_radius;
            float ground_speed = sqrtf(longitudinal_speed * longitudinal_speed + lateral_speed * lateral_speed);
            float absolute_longitudinal_speed = fabsf(longitudinal_speed);
            float absolute_surface_speed = fabsf(surface_speed);
            float rest_speed = PxMax(ground_speed, absolute_surface_speed);
            float slip_denominator = PxMax(PxMax(absolute_longitudinal_speed, absolute_surface_speed), 0.01f);
            float raw_slip_ratio = PxClamp((surface_speed - longitudinal_speed) / slip_denominator, -1.0f, 1.0f);
            float raw_slip_angle = atan2f(lateral_speed, PxMax(absolute_longitudinal_speed, 0.5f));
            float relaxation_length = PxMax(tuning::spec.tire_relaxation_length, 0.05f);
            float longitudinal_blend = 1.0f - expf(-PxMax(ground_speed, absolute_surface_speed) * dt / relaxation_length);
            float lateral_blend = 1.0f - expf(-ground_speed * dt / relaxation_length);
            w.slip_ratio = lerp(w.slip_ratio, raw_slip_ratio, longitudinal_blend);
            w.slip_angle = lerp(w.slip_angle, raw_slip_angle, lateral_blend);

            if (rest_speed < tuning::spec.min_slip_speed && input.throttle <= tuning::spec.input_deadzone)
            {
                float rest_factor = 1.0f - rest_speed / PxMax(tuning::spec.min_slip_speed, 0.01f);
                float rest_decay = exp_decay(24.0f * rest_factor, dt);
                w.slip_ratio = lerp(w.slip_ratio, 0.0f, rest_decay);
                w.slip_angle = lerp(w.slip_angle, 0.0f, rest_decay);
                if (rest_speed < 0.03f)
                {
                    w.slip_ratio = 0.0f;
                    w.slip_angle = 0.0f;
                }
            }
        }
    }

    inline void apply_tire_forces(float dt)
    {
        // --- setup ---
        PxTransform pose = body->getGlobalPose();
        PxVec3 chassis_right = pose.q.rotate(PxVec3(1, 0, 0));
        PxVec3 chassis_forward = pose.q.rotate(PxVec3(0, 0, 1));

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
            const bool handbrake_locked = is_rear(i) && input.handbrake >= 0.999f;
            if (handbrake_locked)
            {
                w.angular_velocity = 0.0f;
                w.net_torque = 0.0f;
            }

            // floor wheel geometry so slip and torque calculations remain finite
            float wr_raw  = cfg.wheel_radius_for(i);
            float wr      = (std::isfinite(wr_raw) && wr_raw > 0.0f) ? PxMax(wr_raw, 0.05f) : 0.34f;
            float wmoi    = (std::isfinite(wheel_moi[i]) && wheel_moi[i] > 0.0f) ? wheel_moi[i] : 1.0f;
            float defl = 0.0f;
            if (tuning::spec.tire_vertical_stiffness > 1000.0f)
            {
                defl = PxClamp(w.tire_load / tuning::spec.tire_vertical_stiffness, 0.0f, 0.05f);
            }
            float wr_eff = PxMax(wr - defl * 0.55f, 0.05f);

            float dyn_camb = is_front(i) ? tuning::spec.front_camber : tuning::spec.rear_camber;
            if (wheel_actor)
            {
                PxVec3 chassis_up = pose.q.rotate(PxVec3(0.0f, 1.0f, 0.0f));
                float measured_camber = asinf(PxClamp(wheel_axis.dot(chassis_up), -1.0f, 1.0f));
                dyn_camb = i == front_left || i == rear_left ? -measured_camber : measured_camber;
                PxVec3 alignment_forward = wheel_axis.cross(chassis_up);
                if (alignment_forward.normalize() > 1e-4f)
                {
                    w.dynamic_toe = atan2f(alignment_forward.dot(chassis_right), alignment_forward.dot(chassis_forward));
                    float side = i == front_left || i == rear_left ? -1.0f : 1.0f;
                    float static_toe = side * (is_front(i) ? tuning::spec.front_toe : tuning::spec.rear_toe);
                    w.bump_steer = w.dynamic_toe - static_toe;
                }
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
                if (!handbrake_locked && input.handbrake > tuning::spec.input_deadzone && is_rear(i))
                {
                    float hb_sign = (w.angular_velocity > 0.0f) ? -1.0f : (w.angular_velocity < 0.0f) ? 1.0f : 0.0f;
                    w.net_torque += hb_sign * tuning::spec.handbrake_torque * input.handbrake;
                }

                float spin_retain = powf(PxClamp(tuning::spec.airborne_wheel_decay, 0.0f, 1.0f), dt * 200.0f);
                if (dt > 1e-5f)
                {
                    w.net_torque += w.angular_velocity * (spin_retain - 1.0f) * wmoi / dt;
                }
                if (wheel_actor)
                {
                    PxVec3 hub_torque = wheel_axis * w.net_torque;
                    safe_add_torque(wheel_actor, hub_torque);
                    safe_add_torque(multibody.corners[i].upright, -hub_torque);
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
            PxVec3 wheel_vel = (wheel_actor ? wheel_actor->getLinearVelocity() : body->getLinearVelocity() + body->getAngularVelocity().cross(world_pos - pose.p)) - ground_point_velocity(w);
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
            float blend_lo = 0.5f;
            float blend_hi = PxMax(tuning::spec.min_slip_speed * 2.0f, 1.0f);
            float blend_t  = PxClamp((max_v - blend_lo) / PxMax(blend_hi - blend_lo, 0.01f), 0.0f, 1.0f);
            float pacejka_weight = blend_t * blend_t * (3.0f - 2.0f * blend_t);

            // static friction model (dominant at rest / very low speed)
            float corner_mass = PxMax(cfg.mass * 0.25f, 1.0f);
            float effective_longitudinal_mass = 1.0f / (1.0f / corner_mass + wr_eff * wr_eff / wmoi);
            float inverse_dt = 1.0f / PxMax(dt, 0.001f);
            float static_lat_f = PxClamp(-vy * corner_mass * inverse_dt, -peak_force_lat * 0.8f, peak_force_lat * 0.8f);
            float static_long_f = PxClamp((wheel_speed - vx) * effective_longitudinal_mass * inverse_dt, -peak_force_long * 0.8f, peak_force_long * 0.8f);
            float static_x = static_long_f / PxMax(peak_force_long * 0.8f, 1.0f);
            float static_y = static_lat_f / PxMax(peak_force_lat * 0.8f, 1.0f);
            float static_magnitude = sqrtf(static_x * static_x + static_y * static_y);
            if (static_magnitude > 1.0f)
            {
                static_long_f /= static_magnitude;
                static_lat_f /= static_magnitude;
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
            float camber_bias = -dyn_camb * 3.0f;
            float zone_heat[3] = {
                PxMax(base_heat * (1.0f + camber_bias), 0.0f),
                base_heat,
                PxMax(base_heat * (1.0f - camber_bias), 0.0f)
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

            if (!handbrake_locked && is_rear(i) && input.handbrake > tuning::spec.input_deadzone)
            {
                float hb_sign = (w.angular_velocity > 0.0f) ? -1.0f : (w.angular_velocity < 0.0f) ? 1.0f : 0.0f;
                w.net_torque += hb_sign * tuning::spec.handbrake_torque * input.handbrake;
            }

            if (wheel_actor)
            {
                PxVec3 hub_torque = wheel_axis * w.net_torque;
                safe_add_torque(wheel_actor, hub_torque);
                safe_add_torque(multibody.corners[i].upright, -hub_torque);
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
            if (const PxRigidDynamic* ground_actor = wheels[i].contact_actor ? wheels[i].contact_actor->is<PxRigidDynamic>() : nullptr)
            {
                safe_add_torque(const_cast<PxRigidDynamic*>(ground_actor), -torque);
            }
        }
    }
}
