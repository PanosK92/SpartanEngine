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

#ifdef DEBUG
    #define _DEBUG 1
    #undef NDEBUG
#else
    #define NDEBUG 1
    #undef _DEBUG
#endif
#define PX_PHYSX_STATIC_LIB
#include <physx/PxPhysicsAPI.h>
#include <physx/extensions/PxGearJoint.h>
#include <vector>
#include <string>
#include <algorithm>
#include <cstring>
#include <cstdlib>
#include <cmath>
#include "../Logging/Log.h"
#include "../Core/Engine.h"
#include "CarPresets.h"
//==========================================

// shared vehicle state telemetry and physx safety helpers

namespace car
{

    using namespace physx;

    namespace tuning
    {
// global environment and surface constants
        constexpr float air_density                  = 1.225f;
        // lateral grip peaks at a small negative camber
        constexpr float camber_optimal               = -0.0436f;
        constexpr float camber_grip_loss             = 16.0f;
        constexpr float surface_friction_asphalt     = 1.0f;
        constexpr float surface_friction_concrete    = 0.95f;
        constexpr float surface_friction_wet_asphalt = 0.7f;
        constexpr float surface_friction_gravel      = 0.6f;
        constexpr float surface_friction_grass       = 0.4f;
        constexpr float surface_friction_ice         = 0.1f;
// file telemetry opens lazily in the working directory
}

    struct aero_debug_data
    {
        PxVec3 position         = PxVec3(0);
        PxVec3 velocity         = PxVec3(0);
        PxVec3 drag_force       = PxVec3(0);
        PxVec3 front_downforce  = PxVec3(0);
        PxVec3 rear_downforce   = PxVec3(0);
        PxVec3 side_force       = PxVec3(0);
        PxVec3 front_aero_pos   = PxVec3(0);
        PxVec3 rear_aero_pos    = PxVec3(0);
        PxVec3 side_aero_pos    = PxVec3(0);
        float  ride_height      = 0.0f;
        float  yaw_angle        = 0.0f;
        float  ground_effect_factor = 1.0f;
        bool   valid            = false;
    };
    // chassis silhouette data for visualization
    struct shape_2d
    {
        std::vector<std::pair<float, float>> side_profile;   // (z, y) points for side view
        std::vector<std::pair<float, float>> front_profile;  // (x, y) points for front view
        float min_x = 0, max_x = 0;
        float min_y = 0, max_y = 0;
        float min_z = 0, max_z = 0;
        bool valid = false;
    };

    enum wheel_id { front_left = 0, front_right = 1, rear_left = 2, rear_right = 3, wheel_count = 4 };
    inline constexpr const char* wheel_names[] = { "FL", "FR", "RL", "RR" };
    enum surface_type { surface_asphalt = 0, surface_concrete, surface_wet_asphalt, surface_gravel, surface_grass, surface_ice, surface_count };

    struct config
    {
        float length              = 4.5f;
        float width               = 2.0f;
        float height              = 0.5f;
        float mass                = 1500.0f;
        float front_wheel_radius  = 0.34f;
        float rear_wheel_radius   = 0.35f;
        float front_wheel_width   = 0.245f;
        float rear_wheel_width    = 0.305f;
        float wheel_mass          = 20.0f;
        float suspension_travel   = 0.20f;
        float suspension_height   = 0.35f;

        // safe defaults remain authoritative until preset geometry is applied
        float wheelbase           = 2.6f;
        float track_front         = 1.6f;
        float track_rear          = 1.6f;

        float wheel_radius_for(int i) const { return (i == front_left || i == front_right) ? front_wheel_radius : rear_wheel_radius; }
        float wheel_width_for(int i) const  { return (i == front_left || i == front_right) ? front_wheel_width  : rear_wheel_width;  }
    };

    // tire surface zones and core thermal state
    struct tire_thermal
    {
        float surface[3] = { 50.0f, 50.0f, 50.0f }; // inside, middle, outside
        float core        = 50.0f;

        float avg_surface() const { return (surface[0] + surface[1] + surface[2]) / 3.0f; }
    };

    struct wheel
    {
        float        compression          = 0.0f;
        float        compression_velocity = 0.0f;
        bool         grounded             = false;
        PxVec3       contact_point        = PxVec3(0);
        PxVec3       contact_normal       = PxVec3(0, 1, 0);
        // engine layer maps the ground actor to a surface type
        const PxRigidActor* contact_actor = nullptr;
        float        angular_velocity     = 0.0f;
        float        rotation             = 0.0f;
        float        tire_load            = 0.0f;
        float        slip_angle           = 0.0f;
        float        slip_ratio           = 0.0f;
        float        lateral_force        = 0.0f;
        float        longitudinal_force   = 0.0f;
        float        net_torque           = 0.0f;
        float        drive_torque         = 0.0f;
        float        brake_torque         = 0.0f;
        tire_thermal thermal;
        float        brake_temp           = 30.0f;
        float        wear                 = 0.0f;
        surface_type contact_surface      = surface_asphalt;
        float        effective_radius     = 0.0f;
        float        dynamic_camber       = 0.0f;
        float        dynamic_toe          = 0.0f;
        float        bump_steer           = 0.0f;
        float        motion_ratio         = 1.0f;
        float        condition_grip       = 1.0f;
        float        condition_stiffness  = 1.0f;
        float        condition_relaxation = 1.0f;
        float        temperature_grip     = 1.0f;
        float        wear_grip            = 1.0f;
        float        brake_efficiency     = 1.0f;
        float        shock_length         = 0.0f;
        float        shock_rest_length    = 0.0f;
        float        shock_velocity       = 0.0f;
        PxVec3       hub_position         = PxVec3(0.0f);
        PxVec3       hub_linear_velocity  = PxVec3(0.0f);
        PxVec3       hub_angular_velocity = PxVec3(0.0f);
    };

    struct input_state
    {
        float throttle  = 0.0f;
        float brake     = 0.0f;
        float steering  = 0.0f;
        float handbrake = 0.0f;
    };

    struct assist_command
    {
        float engine_torque_scale = 1.0f;
        float brake_torque_scale[wheel_count] = { 1.0f, 1.0f, 1.0f, 1.0f };
    };

    struct active_upgrades
    {
        int engine = 0;
        int suspension = 0;
        int tires = 0;
        int brakes = 0;
        int aero = 0;
        int weight = 0;
    };

    // cooldown prevents automatic shift hunting
    inline constexpr float shift_cooldown_time     = 0.5f;

    // telemetry writes fixed step body wheel and component state to csv
struct debug_sweep_data
    {
        PxVec3 origin;
        PxVec3 hit_point;
        bool   hit;
    };

    // suspension queries skip the chassis while mechanism shapes remain query disabled
    class SelfFilterCallback : public PxQueryFilterCallback
    {
    public:
        PxRigidActor* ignore = nullptr;

        PxQueryHitType::Enum preFilter(const PxFilterData&, const PxShape*, const PxRigidActor* actor, PxHitFlags&) override
        {
            return (actor == ignore) ? PxQueryHitType::eNONE : PxQueryHitType::eBLOCK;
        }

        PxQueryHitType::Enum postFilter(const PxFilterData&, const PxQueryHit&, const PxShape*, const PxRigidActor*) override
        {
            return PxQueryHitType::eBLOCK;
        }
    };

}
