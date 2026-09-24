/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
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
#include "../logging/Log.h"
#include "../core/Engine.h"
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
        constexpr float surface_friction_dirt        = 0.65f; // compacted dry dirt, road tires
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
    enum surface_type { surface_asphalt = 0, surface_concrete, surface_wet_asphalt, surface_gravel, surface_grass, surface_ice, surface_dirt, surface_count };

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

    struct tire_probe_row
    {
        PxVec3        point       = PxVec3(0.0f);
        PxVec3        normal      = PxVec3(0.0f, 1.0f, 0.0f);
        PxRigidActor* actor       = nullptr;
        float         penetration = 0.0f;
        float         load        = 0.0f;
        bool          hit         = false;
        float         friction_scale = 1.0f;
        surface_type  surface = surface_asphalt;
        float         rolling_scale = 1.0f;
    };

    struct wheel_force_debug
    {
        PxVec3 tire_point = PxVec3(0);
        PxVec3 longitudinal = PxVec3(0);
        PxVec3 lateral = PxVec3(0);
        PxVec3 rolling_point = PxVec3(0);
        PxVec3 rolling = PxVec3(0);
        PxVec3 rolling_torque = PxVec3(0); // actual world-space resistance moment, Nm
        float brake_torque = 0; // signed torque actually applied, after the stopping clamp
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
        float        stiction_long        = 0.0f; // m, low speed tread deflection against the road
        float        stiction_lat         = 0.0f; // m
        float        lateral_force        = 0.0f;
        float        camber_force         = 0.0f; // camber thrust share of lateral_force
        float        longitudinal_force   = 0.0f;
        float        net_torque           = 0.0f;
        float        drive_torque         = 0.0f;
        float        brake_torque         = 0.0f;
        wheel_force_debug force_debug;
        tire_thermal thermal;
        float        brake_temp           = 30.0f;
        float        wear                 = 0.0f;
        surface_type contact_surface      = surface_asphalt;
        float        surface_grip         = 0.0f; // load-weighted multiplier relative to dry asphalt
        float        surface_rolling      = 0.0f; // load-weighted rolling resistance multiplier
        bool         mixed_surface        = false;
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
        // measured load per tread row, this is what decides where the tire cooks rather than a
        // guess made from camber alone
        float        row_load[max_tire_probe_rows] = {};
        int          row_count            = 0;
        float        contact_patch_length = 0.0f;
        // fraction of the available friction the patch is using, one means fully sliding
        float        tire_saturation      = 0.0f;
        // force over available grip for either tire model, and the friction work rate at the patch
        float        friction_use         = 0.0f;
        float        slip_power           = 0.0f; // W
        tire_probe_row contacts[max_tire_probe_rows];
        float        pressure_bar         = 2.2f;
        float        damage               = 0.0f;
        float        water_depth          = 0.0f; // metres; environment/bench can author standing water
        float        dissipated_energy_j  = 0.0f;
    };

    // one row is a slice of tread across the width, its columns straddle the contact arc so the
    // row rides an averaged ground plane instead of a single sample
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
        float stability_brake_torque[wheel_count] = {};
        float target_yaw_rate = 0.0f;
        bool stability_active = false;
    };

    struct active_upgrades
    {
        int engine = 0;
        int suspension = 0;
        int tires = 0;
        int brakes = 0;
        int aero = 0;
        int weight = 0;
        // audible parts, they also add a little torque
        int exhaust = 0;
        int intake = 0;
        int turbo = 0;
    };

    // cooldown prevents automatic shift hunting
    inline constexpr float shift_cooldown_time     = 0.5f;

    // what the contact model found this step, kept purely so the debug skeleton can draw it
    struct debug_sweep_data
    {
        PxVec3 origin = PxVec3(0);
        PxVec3 hit_point = PxVec3(0);
        bool   hit = false;
        // every tread row that found ground, so the drawn patch is the one the solver was given
        PxVec3 row_point[max_tire_probe_rows];
        PxVec3 row_normal[max_tire_probe_rows];
        float  row_load[max_tire_probe_rows];
        int    row_count = 0;
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
