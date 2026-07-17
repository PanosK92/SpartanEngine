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

// ground contact and tire carcass support for the physical wheel actors

namespace car
{
    inline void update_suspension(PxScene* scene, float dt)
    {
        PxTransform pose = body->getGlobalPose();
        PxVec3 local_down = pose.q.rotate(PxVec3(0, -1, 0));
        PxVec3 local_up = -local_down;

        PxQueryFilterData filter;
        filter.flags = PxQueryFlag::eSTATIC | PxQueryFlag::eDYNAMIC | PxQueryFlag::ePREFILTER;
        self_filter.ignore = body;

        for (int i = 0; i < wheel_count; i++)
        {
            wheel& w = wheels[i];
            float wr_raw = cfg.wheel_radius_for(i);
            float wheel_radius = (std::isfinite(wr_raw) && wr_raw > 0.0f) ? PxMax(wr_raw, 0.05f) : 0.34f;
            float wheel_width = PxMax(cfg.wheel_width_for(i), 0.05f);
            PxRigidDynamic* wheel_actor = multibody.corners[i].wheel_body;
            PxTransform wheel_pose = wheel_actor ? wheel_actor->getGlobalPose() : PxTransform(pose.transform(wheel_offsets[i]), pose.q);
            PxVec3 wheel_center = wheel_pose.p;
            float downward_speed = wheel_actor ? PxMax(wheel_actor->getLinearVelocity().dot(local_down), 0.0f) : PxMax(body->getLinearVelocity().dot(local_down), 0.0f);
            float predicted_travel = PxMax(downward_speed * dt, 0.005f);
            PxQuat query_rotation = multibody.corners[i].upright ? multibody.corners[i].upright->getGlobalPose().q : pose.q;
            float source_radius = PxMax(PxMax(cfg.front_wheel_radius, cfg.rear_wheel_radius), 0.05f);
            float source_width = PxMax(PxMax(cfg.front_wheel_width, cfg.rear_wheel_width), 0.05f);
            PxMeshScale wheel_scale(PxVec3(wheel_width / source_width, wheel_radius / source_radius, wheel_radius / source_radius), PxQuat(PxIdentity));
            PxConvexMeshGeometry wheel_geometry(wheel_sweep_mesh, wheel_scale);
            float query_lift = 0.08f;
            float query_distance = query_lift + PxMax(0.08f, predicted_travel);
            PxTransform sweep_pose(wheel_center + local_up * query_lift, query_rotation);
            PxSweepBuffer hit;
            PxHitFlags hit_flags = PxHitFlag::ePOSITION | PxHitFlag::eNORMAL | PxHitFlag::eMTD;
            bool swept = wheel_sweep_mesh && scene->sweep(wheel_geometry, sweep_pose, local_down, query_distance, hit, hit_flags, filter, &self_filter) && hit.block.actor;

            debug_sweep[i].origin = sweep_pose.p;
            debug_sweep[i].hit = swept;
            debug_sweep[i].hit_point = swept ? hit.block.position : sweep_pose.p + local_down * query_distance;
            debug_suspension_top[i] = body->getGlobalPose().transform(multibody.corners[i].chassis_shock_anchor);
            debug_suspension_bottom[i] = wheel_center;

            w.grounded = false;
            w.tire_load = 0.0f;
            w.contact_actor = nullptr;
            w.contact_normal = local_up;
            sweep_distance[i] = swept ? hit.block.distance - query_lift : query_distance - query_lift;

            if (!swept || hit.block.distance > query_lift + predicted_travel)
            {
                continue;
            }

            PxVec3 normal = hit.block.normal;
            if (!is_finite_vec(normal) || normal.magnitudeSquared() < 1e-6f)
            {
                normal = local_up;
            }
            else
            {
                normal.normalize();
            }
            if (normal.dot(local_up) < 0.5f)
            {
                continue;
            }

            float penetration = PxMax(query_lift + predicted_travel - hit.block.distance, 0.0f);

            PxVec3 contact_velocity = wheel_actor ? actor_point_velocity(wheel_actor, hit.block.position) : actor_point_velocity(body, hit.block.position);
            if (const PxRigidDynamic* ground_actor = hit.block.actor->is<PxRigidDynamic>())
            {
                contact_velocity -= ground_actor->getLinearVelocity() + ground_actor->getAngularVelocity().cross(hit.block.position - ground_actor->getGlobalPose().p);
            }
            float vertical_velocity = contact_velocity.dot(normal);
            float tire_damping = 2.0f * 0.7f * sqrtf(PxMax(tuning::spec.tire_vertical_stiffness, 1.0f) * PxMax(cfg.wheel_mass, 1.0f));
            float normal_force = PxClamp(penetration * tuning::spec.tire_vertical_stiffness - vertical_velocity * tire_damping, 0.0f, tuning::spec.max_susp_force);

            w.grounded = true;
            w.contact_point = hit.block.position;
            w.contact_normal = normal;
            w.contact_actor = hit.block.actor;
            w.tire_load = normal_force;
            if (wheel_actor && normal_force > 0.0f)
            {
                safe_add_force_at_pos(wheel_actor, normal * normal_force, hit.block.position);
                if (PxRigidDynamic* ground_actor = hit.block.actor->is<PxRigidDynamic>())
                {
                    safe_add_force_at_pos(ground_actor, -normal * normal_force, hit.block.position);
                }
            }
        }
    }

}
