#pragma once
// physx suspension assembly and spring damper force coupling
#include "CarState.h"
namespace car
{

    static constexpr int max_suspension_members = 8;
    static constexpr int max_suspension_joints  = 128;
    static constexpr int max_multibody_actors   = wheel_count * (max_suspension_members + 2) + 32;
    static constexpr float wheel_inertia_shape_radius_scale = 0.75f;

    struct anti_roll_bar
    {
        PxRigidDynamic* left_half  = nullptr;
        PxRigidDynamic* right_half = nullptr;
        PxRigidDynamic* left_drop  = nullptr;
        PxRigidDynamic* right_drop = nullptr;
        PxD6Joint* torsion_joint   = nullptr;
        float arm_length           = 0.16f;
    };

    struct driveline_assembly
    {
        PxRigidDynamic* gearbox_output = nullptr;
        PxRigidDynamic* axle_input     = nullptr;
        PxRigidDynamic* propshaft[2]   = {};
        PxRigidDynamic* differential[2] = {};
        PxRigidDynamic* halfshaft[wheel_count] = {};
        PxD6Joint* torsion_joint       = nullptr;
        int propshaft_count            = 0;
        int differential_count         = 0;
        bool initialized               = false;
    };

    struct suspension_member
    {
        PxRigidDynamic* actor = nullptr;
        PxVec3 local_start    = PxVec3(0.0f);
        PxVec3 local_end      = PxVec3(0.0f);
        // the joint at local_start, kept so the debug view can show a rubber bush deflecting rather
        // than drawing every pivot as the same rigid ball joint
        PxJoint* pivot_joint  = nullptr;
        bool pivot_is_bushing = false;
    };

    struct coilover
    {
        PxRigidDynamic* tube = nullptr;
        PxRigidDynamic* rod = nullptr;
        PxD6Joint* spring_joint = nullptr;
        float rest_length = 0.0f;
    };

    struct suspension_corner
    {
        PxRigidDynamic* upright = nullptr;
        PxRigidDynamic* wheel_body = nullptr;
        PxRevoluteJoint* wheel_joint = nullptr;
        PxDistanceJoint* travel_joint = nullptr;
        coilover coilover_unit;
        // upright twist about the chassis vertical, its limit is the steering lock, zero on a rear corner
        PxD6Joint* steering_stop = nullptr;
        float steering_limit = 0.0f;
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
        anti_roll_bar front_arb;
        anti_roll_bar rear_arb;
        driveline_assembly driveline;
        PxRigidDynamic* rack = nullptr;
        PxD6Joint* rack_joint = nullptr;
        PxJoint* joints[max_suspension_joints] = {};
        PxRigidDynamic* actors[max_multibody_actors] = {};
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
