#pragma once
// physx suspension assembly and spring damper force coupling
#include "CarState.h"
namespace car
{

    static constexpr int max_suspension_members = 8;
    static constexpr int max_suspension_joints  = 96;
    static constexpr float wheel_guard_radius_scale = 0.75f;

    struct suspension_member
    {
        PxRigidDynamic* actor = nullptr;
        PxVec3 local_start    = PxVec3(0.0f);
        PxVec3 local_end      = PxVec3(0.0f);
    };

    struct suspension_corner
    {
        PxRigidDynamic* upright = nullptr;
        PxRigidDynamic* wheel_body = nullptr;
        PxRevoluteJoint* wheel_joint = nullptr;
        PxDistanceJoint* travel_joint = nullptr;
        suspension_member members[max_suspension_members];
        int member_count = 0;
        PxVec3 chassis_shock_anchor = PxVec3(0.0f);
        PxVec3 upright_shock_anchor = PxVec3(0.0f);
        float shock_rest_length = 0.0f;
        float shock_length = 0.0f;
        float shock_velocity = 0.0f;
    };

    struct multibody_state
    {
        PxPhysics* physics = nullptr;
        PxScene* scene = nullptr;
        suspension_corner corners[wheel_count];
        PxRigidDynamic* rack = nullptr;
        PxD6Joint* rack_joint = nullptr;
        PxJoint* joints[max_suspension_joints] = {};
        PxRigidDynamic* actors[wheel_count * (max_suspension_members + 2) + 1] = {};
        int joint_count = 0;
        int actor_count = 0;
        float rack_travel = 0.14f;
        bool initialized = false;
    };

    struct actor_motion_state
    {
        PxVec3 linear_velocity = PxVec3(0.0f);
        PxVec3 angular_velocity = PxVec3(0.0f);
        bool valid = false;
    };

    struct corner_motion_state
    {
        actor_motion_state upright;
        actor_motion_state wheel;
        actor_motion_state members[max_suspension_members];
        int member_count = 0;
    };

    struct multibody_motion_state
    {
        corner_motion_state corners[wheel_count];
        actor_motion_state rack;
        bool valid = false;
    };

}
