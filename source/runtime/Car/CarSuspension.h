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
        PxVec3 local_down  = pose.q.rotate(PxVec3(0, -1, 0));
        PxVec3 local_right = pose.q.rotate(PxVec3(1, 0, 0));

        PxQueryFilterData filter;
        filter.flags = PxQueryFlag::eSTATIC | PxQueryFlag::eDYNAMIC | PxQueryFlag::ePREFILTER;
        self_filter.ignore = body;

        float max_wheel_r = PxMax(cfg.front_wheel_radius, cfg.rear_wheel_radius);
        float sweep_dist = cfg.suspension_travel + max_wheel_r + 0.5f;

        for (int i = 0; i < wheel_count; i++)
        {
            wheel& w = wheels[i];
            w.prev_compression = w.compression;
            float wr = cfg.wheel_radius_for(i);
            float ww = cfg.wheel_width_for(i);

            PxVec3 attach = wheel_offsets[i];
            attach.y += cfg.suspension_travel;
            PxVec3 world_attach = pose.transform(attach);

            PxTransform sweep_pose(world_attach, pose.q);
            PxConvexMeshGeometry cylinder_geom(wheel_sweep_mesh);
            PxSweepBuffer hit;

            bool swept = wheel_sweep_mesh
                && scene->sweep(cylinder_geom, sweep_pose, local_down, sweep_dist, hit,
                    PxHitFlag::eDEFAULT, filter, &self_filter)
                && hit.block.actor;

            debug_sweep[i].origin = world_attach;
            debug_sweep[i].hit    = swept;

            if (swept)
            {
                debug_sweep[i].hit_point = hit.block.position;

                w.grounded       = true;
                w.contact_point  = hit.block.position;
                w.contact_normal = hit.block.normal;
                float dist_from_rest = hit.block.distance;

                float speed = body->getLinearVelocity().magnitude();
                if (speed > 1.0f && tuning::road_bump_amplitude > 0.0f)
                {
                    float phase = road_bump_phase;
                    float bump  = sinf(phase * 17.3f + i * 2.1f) * (0.5f + 0.5f * sinf(phase * 7.1f + i * 4.3f));
                    bump += sinf(phase * 31.7f + i * 1.3f) * 0.3f;
                    dist_from_rest += bump * tuning::road_bump_amplitude;
                }

                w.target_compression = PxClamp(1.0f - dist_from_rest / cfg.suspension_travel, 0.0f, 1.0f);

                PxVec3 wheel_center = world_attach + local_down * (cfg.suspension_travel * (1.0f - w.compression) + wr);
                float probe_len     = wr + 0.3f;
                float half_width    = ww * 0.4f;
                PxVec3 probe_origins[3] = {
                    wheel_center,
                    wheel_center - local_right * half_width,
                    wheel_center + local_right * half_width
                };

                for (int p = 0; p < 3; p++)
                {
                    PxRaycastBuffer probe;
                    if (scene->raycast(probe_origins[p], local_down, probe_len, probe, PxHitFlag::eDEFAULT, filter, &self_filter) &&
                        probe.block.actor)
                    {
                        // TODO: map probe.block.shape material to surface_type for split-mu detection
                    }
                }
            }
            else
            {
                debug_sweep[i].hit_point = world_attach + local_down * sweep_dist;
                w.grounded               = false;
                w.target_compression     = 0.0f;
                w.contact_normal         = PxVec3(0, 1, 0);
            }

            debug_suspension_top[i] = world_attach;
            PxVec3 wheel_center_dbg = world_attach + local_down * (cfg.suspension_travel * (1.0f - w.compression) + wr);
            debug_suspension_bottom[i] = wheel_center_dbg;

            // wheel tracking
            float compression_error  = w.target_compression - w.compression;
            float wheel_spring_force = spring_stiffness[i] * compression_error;
            float wheel_damper_force = -spring_damping[i] * w.compression_velocity * 0.5f;
            float wheel_accel        = (wheel_spring_force + wheel_damper_force) / cfg.wheel_mass;

            w.compression_velocity += wheel_accel * dt;
            w.compression          += w.compression_velocity * dt;

            if (w.compression > 1.0f)      { w.compression = 1.0f; w.compression_velocity = PxMin(w.compression_velocity, 0.0f); }
            else if (w.compression < 0.0f) { w.compression = 0.0f; w.compression_velocity = PxMax(w.compression_velocity, 0.0f); }
        }
    }

    inline void apply_suspension_forces(float dt)
    {
        PxTransform pose = body->getGlobalPose();
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

            float displacement = w.compression * cfg.suspension_travel;
            float spring_f = spring_stiffness[i] * displacement;
            float susp_vel = PxClamp(w.compression_velocity * cfg.suspension_travel, -tuning::spec.max_damper_velocity, tuning::spec.max_damper_velocity);
            float damper_ratio = (susp_vel > 0.0f) ? tuning::spec.damping_bump_ratio : tuning::spec.damping_rebound_ratio;
            float damper_f = spring_damping[i] * susp_vel * damper_ratio;

            forces[i] = PxClamp(spring_f + damper_f, 0.0f, tuning::spec.max_susp_force);

            // bump stop - progressive stiffness increase near full compression
            if (w.compression > tuning::spec.bump_stop_threshold)
            {
                float penetration = (w.compression - tuning::spec.bump_stop_threshold) / (1.0f - tuning::spec.bump_stop_threshold);
                forces[i] += tuning::spec.bump_stop_stiffness * penetration * penetration * cfg.suspension_travel;
            }
        }

        auto apply_arb = [&](int left, int right, float stiffness)
        {
            float diff = (wheels[left].compression - wheels[right].compression) * cfg.suspension_travel;
            float arb_force = diff * stiffness;
            if (wheels[left].grounded)  forces[left]  += arb_force;
            if (wheels[right].grounded) forces[right] -= arb_force;
        };
        apply_arb(front_left, front_right, tuning::spec.front_arb_stiffness);
        apply_arb(rear_left, rear_right, tuning::spec.rear_arb_stiffness);

        // cap per-wheel forces, then scale all wheels down if the total would
        // produce more than ~3g of net upward acceleration (prevents launch from
        // external impulses like the player landing on the car)
        float total_force = 0.0f;
        for (int i = 0; i < wheel_count; i++)
        {
            forces[i] = PxClamp(forces[i], 0.0f, tuning::spec.max_susp_force);
            total_force += forces[i];
        }

        float max_total_force = cfg.mass * 9.81f * chassis_force_cap_g;
        if (total_force > max_total_force)
        {
            float scale = max_total_force / total_force;
            for (int i = 0; i < wheel_count; i++)
                forces[i] *= scale;
        }

        for (int i = 0; i < wheel_count; i++)
        {
            wheels[i].tire_load = forces[i] + cfg.wheel_mass * 9.81f;

            if (forces[i] > 0.0f && wheels[i].grounded)
            {
                PxVec3 force = wheels[i].contact_normal * forces[i];
                PxVec3 pos = pose.transform(wheel_offsets[i]);
                PxRigidBodyExt::addForceAtPos(*body, force, pos, PxForceMode::eFORCE);
            }
        }

        // longitudinal weight transfer from body acceleration
        float avg_wr = (cfg.front_wheel_radius + cfg.rear_wheel_radius) * 0.5f;
        float com_height = fabsf(tuning::spec.center_of_mass_y) + avg_wr;
        float weight_transfer = cfg.mass * longitudinal_accel * com_height / PxMax(cfg.wheelbase, 0.1f);
        float max_transfer = cfg.mass * 9.81f * 0.25f;
        weight_transfer = PxClamp(weight_transfer, -max_transfer, max_transfer);
        float transfer_per_wheel = weight_transfer * 0.5f;
        for (int i = 0; i < wheel_count; i++)
        {
            if (wheels[i].grounded)
            {
                if (is_front(i))
                    wheels[i].tire_load -= transfer_per_wheel;
                else
                    wheels[i].tire_load += transfer_per_wheel;
                wheels[i].tire_load = PxMax(wheels[i].tire_load, 0.0f);
            }
        }

        // lateral weight transfer: only the geometric component is added explicitly
        // the elastic component is already produced by the springs as the body rolls under lateral force
        // adding it here would double count it and starve the inside wheels of load at high lateral g
        float track_width      = (cfg.track_front + cfg.track_rear) * 0.5f;
        float total_lat_force  = cfg.mass * lateral_accel;
        float max_lat_transfer = cfg.mass * 9.81f * 0.25f;
        float wdf              = get_weight_distribution_front();
        float front_geo        = total_lat_force * wdf          * tuning::spec.front_roll_center_height / PxMax(track_width, 0.1f);
        float rear_geo         = total_lat_force * (1.0f - wdf) * tuning::spec.rear_roll_center_height  / PxMax(track_width, 0.1f);
        float front_lat_transfer = PxClamp(front_geo, -max_lat_transfer, max_lat_transfer);
        float rear_lat_transfer  = PxClamp(rear_geo,  -max_lat_transfer, max_lat_transfer);
        for (int i = 0; i < wheel_count; i++)
        {
            if (wheels[i].grounded)
            {
                bool is_left = (i == front_left || i == rear_left);
                float axle_transfer = is_front(i) ? front_lat_transfer : rear_lat_transfer;
                if (is_left)
                    wheels[i].tire_load += axle_transfer;
                else
                    wheels[i].tire_load -= axle_transfer;
                wheels[i].tire_load = PxMax(wheels[i].tire_load, 0.0f);
            }
        }
    }

    inline void calculate_steering(float forward_speed, float speed_kmh, float out_angles[wheel_count])
    {
        float reduction = (speed_kmh > 80.0f)
            ? 1.0f - tuning::spec.high_speed_steer_reduction * PxClamp((speed_kmh - 80.0f) / 120.0f, 0.0f, 1.0f)
            : 1.0f;

        float curved_input = copysignf(powf(fabsf(input.steering), tuning::spec.steering_linearity), input.steering);
        float base = curved_input * tuning::spec.max_steer_angle * reduction;

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

            if (base > 0.0f)
            {
                out_angles[front_right] =  inner - tuning::spec.front_toe + front_right_bump;
                out_angles[front_left]  =  outer + tuning::spec.front_toe + front_left_bump;
            }
            else
            {
                out_angles[front_left]  = -inner + tuning::spec.front_toe + front_left_bump;
                out_angles[front_right] = -outer - tuning::spec.front_toe + front_right_bump;
            }
        }
        else
        {
            out_angles[front_left]  = base + tuning::spec.front_toe + front_left_bump;
            out_angles[front_right] = base - tuning::spec.front_toe - front_right_bump;
        }
    }
}
