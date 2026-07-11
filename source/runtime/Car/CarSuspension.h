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

// suspension: convex-sweep ground detection, spring/damper + bump stop integration,
// anti-roll bars, per-wheel tire load with longitudinal and lateral weight transfer.
// also hosts ackermann steering geometry since bump steer couples to compression.

namespace car
{
    inline void update_suspension(PxScene* scene, float dt)
    {
        PxTransform pose = body->getGlobalPose();
        PxVec3 local_down = pose.q.rotate(PxVec3(0, -1, 0));

        PxQueryFilterData filter;
        filter.flags = PxQueryFlag::eSTATIC | PxQueryFlag::eDYNAMIC | PxQueryFlag::ePREFILTER;
        self_filter.ignore = body;

        // floor geometry that participates in divisions and force scaling
        float susp_travel_safe = PxMax(cfg.suspension_travel, 0.01f);

        float max_wheel_r = PxMax(cfg.front_wheel_radius, cfg.rear_wheel_radius);
        float sweep_dist  = susp_travel_safe + max_wheel_r + 0.5f;

        for (int i = 0; i < wheel_count; i++)
        {
            wheel& w = wheels[i];
            w.prev_compression = w.compression;
            float wr_raw = cfg.wheel_radius_for(i);
            float wr     = (std::isfinite(wr_raw) && wr_raw > 0.0f) ? PxMax(wr_raw, 0.05f) : 0.34f;

            PxVec3 attach = wheel_offsets[i];
            attach.y += susp_travel_safe;
            PxVec3 world_attach = pose.transform(attach);

            PxTransform sweep_pose(world_attach, pose.q);
            PxConvexMeshGeometry cylinder_geom(wheel_sweep_mesh);
            PxSweepBuffer hit;

            // eMTD makes physx return a valid position and normal even when the cylinder starts
            // already overlapping the ground. without it the suspension multiplies forces[i] by
            // a zero contact_normal once the body sinks below the spring rest line, the chassis
            // never gets pushed back up and the wheels stay visually buried with no traction
            const PxHitFlags sweep_flags = PxHitFlag::ePOSITION | PxHitFlag::eNORMAL | PxHitFlag::eMTD;

            bool swept = wheel_sweep_mesh
                && scene->sweep(cylinder_geom, sweep_pose, local_down, sweep_dist, hit,
                    sweep_flags, filter, &self_filter)
                && hit.block.actor;

            debug_sweep[i].origin = world_attach;
            debug_sweep[i].hit    = swept;

            if (swept)
            {
                debug_sweep[i].hit_point = hit.block.position;

                // fall back to body up when the normal is degenerate
                PxVec3 sweep_normal = hit.block.normal;
                float normal_len_sq = sweep_normal.dot(sweep_normal);
                if (!is_finite_vec(sweep_normal) || normal_len_sq < 1e-6f)
                {
                    sweep_normal = -local_down;
                }
                else
                {
                    sweep_normal *= 1.0f / sqrtf(normal_len_sq);
                }

                // reject wall-like hits, a curb face or barrier is not a road surface and must
                // not drive spring compression or tire load
                const float min_ground_alignment = 0.5f;
                const bool is_ground = sweep_normal.dot(-local_down) >= min_ground_alignment;

                sweep_distance[i] = hit.block.distance;

                if (is_ground)
                {
                    // eMTD reports negative distance when the sweep starts overlapping, that is real
                    // penetration past full travel and must drive the bump stop, not clamp to target 1
                    float dist_from_rest = hit.block.distance;

                    w.grounded       = true;
                    w.contact_point  = hit.block.position;
                    w.contact_normal = sweep_normal;
                    w.contact_actor  = hit.block.actor;

                    float speed = body->getLinearVelocity().magnitude();
                    if (speed > 1.0f && tuning::road_bump_amplitude > 0.0f)
                    {
                        float phase = road_bump_phase;
                        float bump  = sinf(phase * 17.3f + i * 2.1f) * (0.5f + 0.5f * sinf(phase * 7.1f + i * 4.3f));
                        bump += sinf(phase * 31.7f + i * 1.3f) * 0.3f;
                        dist_from_rest += bump * tuning::road_bump_amplitude;
                    }

                    // compression > 1 means the bump stop is engaged
                    w.target_compression = PxClamp(1.0f - dist_from_rest / susp_travel_safe, 0.0f, 1.5f);
                }
                else
                {
                    w.grounded           = false;
                    w.target_compression = 0.0f;
                    w.contact_normal     = -local_down;
                    w.contact_actor      = nullptr;
                }
            }
            else
            {
                debug_sweep[i].hit_point = world_attach + local_down * sweep_dist;
                w.grounded               = false;
                w.target_compression     = 0.0f;
                w.contact_normal         = PxVec3(0, 1, 0);
                w.contact_actor          = nullptr;
                sweep_distance[i]        = sweep_dist;
            }

            debug_suspension_top[i] = world_attach;
            float visual_comp = PxClamp(w.compression, 0.0f, 1.0f);
            PxVec3 wheel_center_dbg = world_attach + local_down * (susp_travel_safe * (1.0f - visual_comp) + wr);
            debug_suspension_bottom[i] = wheel_center_dbg;

            // kinematic strut: compression is the sweep geometry, velocity is its rate of change.
            // an unsprung mass lag here turned hard roll into a resonant bounce (rc car hop)
            w.compression = w.target_compression;
            w.compression_velocity = (w.compression - w.prev_compression) / PxMax(dt, 0.0001f);
        }
    }

    inline void apply_suspension_forces(float dt)
    {
        (void)dt;
        PxTransform pose = body->getGlobalPose();
        // spring and damper act along the strut, never along a curb face normal
        PxVec3 strut_dir = pose.q.rotate(PxVec3(0, 1, 0));
        float forces[wheel_count];

        for (int i = 0; i < wheel_count; i++)
        {
            wheel& w = wheels[i];
            if (!w.grounded)
            {
                forces[i] = 0.0f;
                w.tire_load = 0.0f;
                continue;
            }

            // soft spring saturates at full travel, bump stop handles anything past that
            float travel    = cfg.suspension_travel;
            float soft_comp = PxMin(w.compression, 1.0f);
            float spring_f  = spring_stiffness[i] * soft_comp * travel;
            float susp_vel  = PxClamp(w.compression_velocity * travel, -tuning::spec.max_damper_velocity, tuning::spec.max_damper_velocity);
            float damper_ratio = (susp_vel > 0.0f) ? tuning::spec.damping_bump_ratio : tuning::spec.damping_rebound_ratio;
            float damper_f  = spring_damping[i] * susp_vel * damper_ratio;

            forces[i] = spring_f + damper_f;

            // progressive bump stop once past the soft travel limit
            if (w.compression > tuning::spec.bump_stop_threshold)
            {
                float penetration = (w.compression - tuning::spec.bump_stop_threshold) / PxMax(1.0f - tuning::spec.bump_stop_threshold, 0.01f);
                forces[i] += tuning::spec.bump_stop_stiffness * penetration * penetration * travel;
            }

            forces[i] = PxClamp(forces[i], 0.0f, tuning::spec.max_susp_force);
        }

        auto apply_arb = [&](int left, int right, float stiffness)
        {
            float diff = (wheels[left].compression - wheels[right].compression) * cfg.suspension_travel;
            float arb_force = diff * stiffness;
            if (wheels[left].grounded)
            {
                forces[left]  += arb_force;
            }
            if (wheels[right].grounded)
            {
                forces[right] -= arb_force;
            }
        };
        apply_arb(front_left, front_right, tuning::spec.front_arb_stiffness);
        apply_arb(rear_left, rear_right, tuning::spec.rear_arb_stiffness);

        float wheel_mass_safe = (std::isfinite(cfg.wheel_mass) && cfg.wheel_mass > 0.0f) ? cfg.wheel_mass : 20.0f;
        for (int i = 0; i < wheel_count; i++)
        {
            forces[i] = PxClamp(forces[i], 0.0f, tuning::spec.max_susp_force);
            spring_force[i]     = forces[i];
            wheels[i].tire_load = forces[i] + wheel_mass_safe * 9.81f;

            if (forces[i] > 0.0f && wheels[i].grounded)
            {
                safe_add_force_at_pos(body, strut_dir * forces[i], wheels[i].contact_point);
            }
        }

        // geometric weight transfer only, elastic transfer already comes from the springs
        float track_width        = (cfg.track_front + cfg.track_rear) * 0.5f;
        float total_lat_force    = cfg.mass * lateral_accel;
        float wdf                = get_weight_distribution_front();
        float front_lat_transfer = total_lat_force * wdf          * tuning::spec.front_roll_center_height / PxMax(track_width, 0.1f);
        float rear_lat_transfer  = total_lat_force * (1.0f - wdf) * tuning::spec.rear_roll_center_height  / PxMax(track_width, 0.1f);
        float long_transfer      = cfg.mass * longitudinal_accel * tuning::spec.pitch_center_height / PxMax(cfg.wheelbase, 0.1f);

        for (int i = 0; i < wheel_count; i++)
        {
            if (wheels[i].grounded)
            {
                bool is_left = (i == front_left || i == rear_left);
                float axle_transfer = is_front(i) ? front_lat_transfer : rear_lat_transfer;
                if (is_left)
                {
                    wheels[i].tire_load += axle_transfer;
                }
                else
                {
                    wheels[i].tire_load -= axle_transfer;
                }
                wheels[i].tire_load += is_front(i) ? -long_transfer * 0.5f : long_transfer * 0.5f;
                wheels[i].tire_load = PxMax(wheels[i].tire_load, 0.0f);
            }
        }
    }

    inline void calculate_steering(float forward_speed, float out_angles[wheel_count])
    {
        float curved_input = copysignf(powf(fabsf(input.steering), tuning::spec.steering_linearity), input.steering);
        float base = curved_input * tuning::spec.max_steer_angle;

        // load dependent steer compliance: lateral force at the front bends the rack slightly
        if (tuning::spec.steer_compliance > 0.0f)
        {
            float front_lat = wheels[front_left].lateral_force + wheels[front_right].lateral_force;
            float compliance_angle = front_lat * tuning::spec.steer_compliance * 1.0e-5f;
            base -= compliance_angle;
            base = PxClamp(base, -tuning::spec.max_steer_angle, tuning::spec.max_steer_angle);
        }

        // bump steer
        float front_left_bump  = wheels[front_left].compression * cfg.suspension_travel * tuning::spec.front_bump_steer;
        float front_right_bump = wheels[front_right].compression * cfg.suspension_travel * tuning::spec.front_bump_steer;
        float rear_left_bump   = wheels[rear_left].compression * cfg.suspension_travel * tuning::spec.rear_bump_steer;
        float rear_right_bump  = wheels[rear_right].compression * cfg.suspension_travel * tuning::spec.rear_bump_steer;

        out_angles[rear_left]  = tuning::spec.rear_toe + rear_left_bump;
        out_angles[rear_right] = -tuning::spec.rear_toe - rear_right_bump;

        if (fabsf(base) < tuning::spec.steering_deadzone)
        {
            out_angles[front_left]  = tuning::spec.front_toe + front_left_bump;
            out_angles[front_right] = -tuning::spec.front_toe - front_right_bump;
            return;
        }

        // ackermann geometry
        if (forward_speed >= 0.0f)
        {
            float half_track = cfg.track_front * 0.5f;
            float turn_r     = cfg.wheelbase / tanf(fabsf(base));

            float inner = atanf(cfg.wheelbase / PxMax(turn_r - half_track, 0.1f));
            float outer = atanf(cfg.wheelbase / PxMax(turn_r + half_track, 0.1f));

            // bump steer mirrors like toe: positive on the left wheel, negative on the right
            if (base > 0.0f)
            {
                out_angles[front_right] =  inner - tuning::spec.front_toe - front_right_bump;
                out_angles[front_left]  =  outer + tuning::spec.front_toe + front_left_bump;
            }
            else
            {
                out_angles[front_left]  = -inner + tuning::spec.front_toe + front_left_bump;
                out_angles[front_right] = -outer - tuning::spec.front_toe - front_right_bump;
            }
        }
        else
        {
            out_angles[front_left]  = base + tuning::spec.front_toe + front_left_bump;
            out_angles[front_right] = base - tuning::spec.front_toe - front_right_bump;
        }
    }
}
