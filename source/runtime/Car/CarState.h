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
#include <vector>
#include <cstring>
#include <cstdlib>
#include <cmath>
#include "../Logging/Log.h"
#include "../Core/Engine.h"
#include "CarPresets.h"
//==========================================

// vehicle dynamics state: structs, globals, telemetry, debug helpers.
// this is the shared substrate every other car sim header pulls in; it owns no physics
// logic of its own - that lives in CarPacejka / CarAero / CarSuspension / CarDrivetrain /
// CarTires / CarSimulation.

namespace car
{
    using namespace physx;

    //= tuning namespace ===========================================================

    // swap active car spec at runtime
    // defined in CarSimulation.h because it also has to update the cached cfg geometry,
    // recompute wheel offsets and refresh the body's mass and inertia tensor
    void load_car(const car_preset& new_spec);
    void reset_drivetrain_transients();
    void reset_wheel_thermals();
    void clamp_upgrade_stage(int& stage, int max_stage);
    void reapply_upgrades();
    void reset_upgrades();

    namespace tuning
    {
        // active car specification, load_car applies a definition loaded from a .car file
        inline car_preset spec = car_preset();

        // simulation-level parameters (not part of car spec)
        constexpr float air_density                  = 1.225f;
        constexpr float road_bump_amplitude          = 0.002f;
        constexpr float road_bump_frequency          = 0.5f;
        // lateral grip peaks at a slightly negative camber, quadratic loss either side, per rad squared
        constexpr float camber_optimal               = -0.0436f;
        constexpr float camber_grip_loss             = 16.0f;
        constexpr float surface_friction_asphalt     = 1.0f;
        constexpr float surface_friction_concrete    = 0.95f;
        constexpr float surface_friction_wet_asphalt = 0.7f;
        constexpr float surface_friction_gravel      = 0.6f;
        constexpr float surface_friction_grass       = 0.4f;
        constexpr float surface_friction_ice         = 0.1f;

        // debug
        inline bool draw_raycasts   = true;
        inline bool draw_suspension = true;
        inline bool log_pacejka     = false;
        inline bool log_telemetry   = false;
        // writes a per-tick csv (car_telemetry.csv) to the working directory
        // path is logged on first open so it's easy to find
        inline bool log_to_file     = true;
    }

    // load_car body lives in CarSimulation.h so it can touch the physics body and geometry

    // guards a nan or inf vector before it reaches physx and trips an invalid parameter warning
    inline bool is_finite_vec(const PxVec3& v)
    {
        return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
    }

    inline void safe_add_force(PxRigidDynamic* body, const PxVec3& force, PxForceMode::Enum mode = PxForceMode::eFORCE)
    {
        if (!body)
        {
            return;
        }
        if (!is_finite_vec(force))
        {
            SP_LOG_WARNING("dropping non finite force (%.3f, %.3f, %.3f) before addForce", force.x, force.y, force.z);
            return;
        }
        body->addForce(force, mode);
    }

    inline void safe_add_torque(PxRigidDynamic* body, const PxVec3& torque, PxForceMode::Enum mode = PxForceMode::eFORCE)
    {
        if (!body)
        {
            return;
        }
        if (!is_finite_vec(torque))
        {
            SP_LOG_WARNING("dropping non finite torque (%.3f, %.3f, %.3f) before addTorque", torque.x, torque.y, torque.z);
            return;
        }
        body->addTorque(torque, mode);
    }

    inline void safe_add_force_at_pos(PxRigidDynamic* body, const PxVec3& force, const PxVec3& pos, PxForceMode::Enum mode = PxForceMode::eFORCE)
    {
        if (!body)
        {
            return;
        }
        if (!is_finite_vec(force))
        {
            SP_LOG_WARNING("dropping non finite force (%.3f, %.3f, %.3f) before addForceAtPos", force.x, force.y, force.z);
            return;
        }
        if (!is_finite_vec(pos))
        {
            SP_LOG_WARNING("dropping non finite position (%.3f, %.3f, %.3f) before addForceAtPos", pos.x, pos.y, pos.z);
            return;
        }
        PxRigidBodyExt::addForceAtPos(*body, force, pos, mode);
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
        float  ride_height      = 0.0f;
        float  yaw_angle        = 0.0f;
        float  ground_effect_factor = 1.0f;
        bool   valid            = false;
    };
    // inline (not inline static) at namespace scope: gives external linkage so all TUs
    // that include this header share ONE instance. inline static at namespace scope keeps
    // the per-TU semantics of `static`, which silently splits state across translation
    // units. that was the root cause of wheel_offsets[i].y being 0 in physics.cpp tu while
    // compute_constants wrote -0.35 in some other tu, so suspension never produced spring
    // force and the chassis sat on the ground
    inline aero_debug_data aero_debug;

    // stored shape data for visualization (2D projections of convex hull)
    struct shape_2d
    {
        std::vector<std::pair<float, float>> side_profile;   // (z, y) points for side view
        std::vector<std::pair<float, float>> front_profile;  // (x, y) points for front view
        float min_x = 0, max_x = 0;
        float min_y = 0, max_y = 0;
        float min_z = 0, max_z = 0;
        bool valid = false;
    };

    // function-local static for odr safety
    inline shape_2d& shape_data_ref()
    {
        static shape_2d instance;
        return instance;
    }

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

        // cached geometry filled by compute_constants, source of truth for all consumers
        // non zero defaults so wheel placement and force math stay sane if a preset overlay is skipped
        float wheelbase           = 2.6f;
        float track_front         = 1.6f;
        float track_rear          = 1.6f;

        float wheel_radius_for(int i) const { return (i == front_left || i == front_right) ? front_wheel_radius : rear_wheel_radius; }
        float wheel_width_for(int i) const  { return (i == front_left || i == front_right) ? front_wheel_width  : rear_wheel_width;  }
    };

    // 3-zone surface temperature + core temperature
    struct tire_thermal
    {
        float surface[3] = { 50.0f, 50.0f, 50.0f }; // inside, middle, outside
        float core        = 50.0f;

        float avg_surface() const { return (surface[0] + surface[1] + surface[2]) / 3.0f; }
    };

    struct wheel
    {
        float        compression          = 0.0f;
        float        target_compression   = 0.0f;
        float        prev_compression     = 0.0f;
        float        compression_velocity = 0.0f;
        bool         grounded             = false;
        PxVec3       contact_point        = PxVec3(0);
        PxVec3       contact_normal       = PxVec3(0, 1, 0);
        // ground actor under this wheel, the engine layer maps it to a surface_type per tick
        const PxRigidActor* contact_actor = nullptr;
        float        angular_velocity     = 0.0f;
        float        rotation             = 0.0f;
        float        tire_load            = 0.0f;
        float        slip_angle           = 0.0f;
        float        slip_ratio           = 0.0f;
        float        lateral_force        = 0.0f;
        float        longitudinal_force   = 0.0f;
        float        net_torque           = 0.0f;
        tire_thermal thermal;
        float        brake_temp           = 30.0f;
        float        wear                 = 0.0f;
        surface_type contact_surface      = surface_asphalt;
        float        effective_radius     = 0.0f;
        float        dynamic_camber       = 0.0f;
    };

    struct input_state
    {
        float throttle  = 0.0f;
        float brake     = 0.0f;
        float steering  = 0.0f;
        float handbrake = 0.0f;
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

    // all of the following are `inline` (not `inline static`) at namespace scope so they
    // get external linkage. see the comment on aero_debug above for why this matters
    inline PxRigidDynamic* body             = nullptr;
    inline PxMaterial*     material         = nullptr;
    inline PxConvexMesh*   wheel_sweep_mesh = nullptr;
    inline config          cfg;
    inline car_preset      base_spec;
    inline active_upgrades upgrades;
    inline wheel           wheels[wheel_count];
    inline input_state     input;
    inline input_state     input_target;
    inline PxVec3          wheel_offsets[wheel_count];
    inline float           wheel_moi[wheel_count];
    inline float           spring_stiffness[wheel_count];
    inline float           spring_damping[wheel_count];
    // per wheel spring force after arb, capping and scaling, exposed for telemetry so
    // we can correlate compression with the force actually pushed into the body
    inline float           spring_force[wheel_count]   = {};
    // raw sweep distance from the suspension attach point to ground, before clamping.
    // negative means the sweep started already overlapping the ground via emtd
    inline float           sweep_distance[wheel_count] = {};
    inline float           abs_phase               = 0.0f;
    inline bool            abs_active[wheel_count] = {};
    inline float           tc_reduction            = 0.0f;
    inline bool            tc_active               = false;
    inline float           engine_rpm              = 800.0f;
    // gear encoding: 0 = reverse, 1 = neutral (gear_ratios[1] == 0 acts as the sentinel),
    // 2..8 = forward gears. use the helpers below rather than literal-comparing the index.
    inline int             current_gear            = 1;

    inline bool is_in_reverse()      { return current_gear == 0; }
    inline bool is_in_neutral()      { return current_gear == 1; }
    inline bool is_in_forward_gear() { return current_gear >= 2; }
    inline float           shift_timer             = 0.0f;
    inline bool            is_shifting             = false;
    inline float           clutch                  = 1.0f;
    inline float           shift_cooldown          = 0.0f;
    // cooldown after a shift completes before the next auto-shift can occur
    inline constexpr float shift_cooldown_time     = 0.5f;
    // per-wheel chassis reaction force cap from the suspension, expressed in g's
    inline constexpr float chassis_force_cap_g     = 6.0f;
    // "large" static friction gain applied under the low-slip static-friction model,
    // units are (N per m/s) per kg of chassis mass
    inline constexpr float static_friction_gain_per_kg = 10.0f;
    inline int             last_shift_direction    = 0;
    inline float           redline_hold_timer      = 0.0f;
    inline float           boost_pressure          = 0.0f;
    inline float           motor_torque            = 0.0f;
    inline bool            rev_limiter_active      = false;
    inline float           downshift_blip_timer    = 0.0f;
    inline float           driveshaft_twist        = 0.0f;
    // engine flywheel inertia reflected to each driven wheel through the current gearing,
    // added to wheel_moi during spin integration so low gears resist wheel speed changes
    inline float           reflected_engine_inertia = 0.0f;
    inline bool            drs_active              = false;
    inline float           longitudinal_accel      = 0.0f;
    inline float           lateral_accel           = 0.0f;
    inline float           road_bump_phase         = 0.0f;
    inline PxVec3          prev_velocity           = PxVec3(0);
    // total engine braking torque routed to the driven axle this tick before any per wheel split
    inline float           engine_brake_torque     = 0.0f;

    // telemetry: writes a per-tick csv of body + per-wheel state to car_telemetry.csv
    // in the working directory. opens lazily, closes when tuning::log_to_file is off,
    // and flushes periodically so the file survives a crash. columns tuned for
    // diagnosing handling: spin out, self align, weight transfer, thermals, grip, geometry
    struct telemetry_dump
    {
        FILE* file          = nullptr;
        int   frame_counter = 0;
        float elapsed_time  = 0.0f;

        ~telemetry_dump()      { close(); }
        void close()
        {
            if (file)
            {
                fclose(file);
                file = nullptr;
            }
            frame_counter = 0;
            elapsed_time  = 0.0f;
        }

        bool open_if_needed()
        {
            if (file)
            {
                return true;
            }
            fopen_s(&file, "car_telemetry.csv", "w");
            if (!file)
            {
                return false;
            }

            // log absolute path so the user can find the file
            char abs_path[1024] = {};
            if (_fullpath(abs_path, "car_telemetry.csv", sizeof(abs_path)))
            {
                SP_LOG_INFO("car telemetry: writing to %s", abs_path);
            }

            fprintf(file,
                // time + body state
                "frame,time,dt,"
                "pos_x,pos_y,pos_z,"
                "speed_kmh,forward_speed_ms,lateral_speed_ms,"
                "yaw_rate,body_slip_deg,"
                "long_accel,lat_accel,"
                // body vertical velocity and angular velocity magnitude, top level signal for
                // whether the chassis is settling, oscillating or being pumped by the springs
                "vy,ang_vel_mag,"
                // inputs
                "throttle,brake,steering,handbrake,"
                // drivetrain
                "gear,engine_rpm,is_shifting,clutch,tc_active,tc_reduction,"
                // per-wheel grounded
                "fl_grounded,fr_grounded,rl_grounded,rr_grounded,"
                // per-wheel slip
                "fl_slip_angle,fr_slip_angle,rl_slip_angle,rr_slip_angle,"
                "fl_slip_ratio,fr_slip_ratio,rl_slip_ratio,rr_slip_ratio,"
                // per-wheel forces and loads
                "fl_lat_force,fr_lat_force,rl_lat_force,rr_lat_force,"
                "fl_long_force,fr_long_force,rl_long_force,rr_long_force,"
                "fl_tire_load,fr_tire_load,rl_tire_load,rr_tire_load,"
                // per-wheel angular velocity
                "fl_ang_vel,fr_ang_vel,rl_ang_vel,rr_ang_vel,"
                // per wheel net torque accumulated this tick from engine, brakes, tire reaction and bearing
                "fl_net_torque,fr_net_torque,rl_net_torque,rr_net_torque,"
                // total engine braking torque applied to driven axle, key signal for liftoff oversteer
                "engine_brake_torque,"
                // suspension diag: target compression from sweep, actual compression after spring
                // dynamics, raw sweep distance and the post arb post cap spring force in newtons.
                // contact_ny is the y component of the contact normal so we can spot tilted or
                // degenerate ground hits, key to debugging chassis on ground vs spring on ground
                "fl_target_comp,fr_target_comp,rl_target_comp,rr_target_comp,"
                "fl_comp,fr_comp,rl_comp,rr_comp,"
                "fl_sweep_dist,fr_sweep_dist,rl_sweep_dist,rr_sweep_dist,"
                "fl_spring_force,fr_spring_force,rl_spring_force,rr_spring_force,"
                "fl_contact_ny,fr_contact_ny,rl_contact_ny,rr_contact_ny,"
                // upgrades levels and key handling state that changes at runtime
                "eng_up,susp_up,tire_up,brake_up,aero_up,weight_up,"
                // per wheel brake and wear directly affect braking and grip
                "fl_brake_temp,fr_brake_temp,rl_brake_temp,rr_brake_temp,"
                "fl_wear,fr_wear,rl_wear,rr_wear,"
                // tire thermals surface avg core drive grip via temp factor
                "fl_surf_temp,fr_surf_temp,rl_surf_temp,rr_surf_temp,"
                "fl_core_temp,fr_core_temp,rl_core_temp,rr_core_temp,"
                // instantaneous efficiency and grip multipliers used this tick
                "fl_brake_eff,fr_brake_eff,rl_brake_eff,rr_brake_eff,"
                "fl_grip_temp_f,fr_grip_temp_f,rl_grip_temp_f,rr_grip_temp_f,"
                "fl_grip_wear_f,fr_grip_wear_f,rl_grip_wear_f,rr_grip_wear_f,"
                // new geometry effective loaded radius and dynamic camber used in slip grip calcs
                "fl_eff_r,fr_eff_r,rl_eff_r,rr_eff_r,"
                "fl_dyn_camb,fr_dyn_camb,rl_dyn_camb,rr_dyn_camb,"
                "fl_abs,fr_abs,rl_abs,rr_abs,"
                "mass,tire_friction,brake_force,engine_peak_tq\n");
            frame_counter = 0;
            elapsed_time  = 0.0f;
            return true;
        }

        void tick(float dt, float speed_kmh)
        {
            if (!tuning::log_to_file) { close(); return; }
            if (!open_if_needed())
            {
                return;
            }
            if (!body)
            {
                return;
            }

            elapsed_time += dt;

            PxTransform pose    = body->getGlobalPose();
            PxVec3      vel     = body->getLinearVelocity();
            PxVec3      ang_vel = body->getAngularVelocity();
            PxVec3      fwd     = pose.q.rotate(PxVec3(0, 0, 1));
            PxVec3      right   = pose.q.rotate(PxVec3(1, 0, 0));
            PxVec3      up      = pose.q.rotate(PxVec3(0, 1, 0));

            float forward_speed = vel.dot(fwd);
            float lateral_speed = vel.dot(right);
            float yaw_rate      = ang_vel.dot(up);

            // body slip: angle between velocity vector and car forward, in degrees
            // a stable car holds this near zero, a spinning car has it growing toward 90+
            float body_slip_deg = 0.0f;
            if (vel.magnitude() > 0.5f)
            {
                body_slip_deg = atan2f(lateral_speed, forward_speed) * 180.0f / PxPi;
            }

            // derive handling multipliers for this tick, inlined to avoid include cycle
            auto brake_eff = [](float t){ float amb=tuning::spec.brake_ambient_temp; float opt=PxMax(tuning::spec.brake_optimal_temp,amb+10.f); float fad=PxMax(tuning::spec.brake_fade_temp,opt+10.f); if(t>=fad)return 0.6f; if(t<opt){float u=PxClamp((t-amb)/PxMax(opt-amb,1.f),0.f,1.f);return 0.8f+0.2f*u;} float u=(t-opt)/PxMax(fad-opt,1.f); return PxClamp(1.f-0.4f*u,0.5f,1.f); };
            auto temp_gf  = [](float s){ float dev=fabsf(s-tuning::spec.tire_optimal_temp); float n=PxClamp(dev/PxMax(tuning::spec.tire_temp_range,1.f),0.f,1.f); return 1.f - n*n * tuning::spec.tire_grip_temp_factor; };
            float be_fl = brake_eff(wheels[front_left].brake_temp),  be_fr=brake_eff(wheels[front_right].brake_temp),  be_rl=brake_eff(wheels[rear_left].brake_temp),  be_rr=brake_eff(wheels[rear_right].brake_temp);
            float tg_fl = temp_gf(wheels[front_left].thermal.avg_surface()), tg_fr=temp_gf(wheels[front_right].thermal.avg_surface()), tg_rl=temp_gf(wheels[rear_left].thermal.avg_surface()), tg_rr=temp_gf(wheels[rear_right].thermal.avg_surface());
            float wg_fl = 1.f - wheels[front_left].wear * tuning::spec.tire_grip_wear_loss, wg_fr=1.f - wheels[front_right].wear * tuning::spec.tire_grip_wear_loss, wg_rl=1.f - wheels[rear_left].wear * tuning::spec.tire_grip_wear_loss, wg_rr=1.f - wheels[rear_right].wear * tuning::spec.tire_grip_wear_loss;

            fprintf(file,
                "%d,%.3f,%.4f,"
                "%.2f,%.2f,%.2f,"
                "%.2f,%.3f,%.3f,"
                "%.4f,%.2f,"
                "%.3f,%.3f,"
                "%.3f,%.3f,"
                "%.3f,%.3f,%.3f,%.3f,"
                "%d,%.0f,%d,%.3f,%d,%.3f,"
                "%d,%d,%d,%d,"
                "%.4f,%.4f,%.4f,%.4f,"
                "%.4f,%.4f,%.4f,%.4f,"
                "%.1f,%.1f,%.1f,%.1f,"
                "%.1f,%.1f,%.1f,%.1f,"
                "%.1f,%.1f,%.1f,%.1f,"
                "%.3f,%.3f,%.3f,%.3f,"
                "%.1f,%.1f,%.1f,%.1f,"
                "%.1f,"
                "%.3f,%.3f,%.3f,%.3f,"
                "%.3f,%.3f,%.3f,%.3f,"
                "%.4f,%.4f,%.4f,%.4f,"
                "%.1f,%.1f,%.1f,%.1f,"
                "%.3f,%.3f,%.3f,%.3f,"
                // upgrades
                "%d,%d,%d,%d,%d,%d,"
                // brake wear
                "%.1f,%.1f,%.1f,%.1f,%.3f,%.3f,%.3f,%.3f,"
                // thermals
                "%.1f,%.1f,%.1f,%.1f,%.1f,%.1f,%.1f,%.1f,"
                // eff grip factors
                "%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,"
                // eff r dyn camb
                "%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,"
                "%d,%d,%d,%d,"
                "%.1f,%.3f,%.1f,%.1f\n",
                frame_counter, elapsed_time, dt,
                pose.p.x, pose.p.y, pose.p.z,
                speed_kmh, forward_speed, lateral_speed,
                yaw_rate, body_slip_deg,
                longitudinal_accel, lateral_accel,
                vel.y, ang_vel.magnitude(),
                input.throttle, input.brake, input.steering, input.handbrake,
                current_gear, engine_rpm, is_shifting ? 1 : 0, clutch, tc_active ? 1 : 0, tc_reduction,
                wheels[front_left].grounded  ? 1 : 0,
                wheels[front_right].grounded ? 1 : 0,
                wheels[rear_left].grounded   ? 1 : 0,
                wheels[rear_right].grounded  ? 1 : 0,
                wheels[front_left].slip_angle,  wheels[front_right].slip_angle,
                wheels[rear_left].slip_angle,   wheels[rear_right].slip_angle,
                wheels[front_left].slip_ratio,  wheels[front_right].slip_ratio,
                wheels[rear_left].slip_ratio,   wheels[rear_right].slip_ratio,
                wheels[front_left].lateral_force,  wheels[front_right].lateral_force,
                wheels[rear_left].lateral_force,   wheels[rear_right].lateral_force,
                wheels[front_left].longitudinal_force,  wheels[front_right].longitudinal_force,
                wheels[rear_left].longitudinal_force,   wheels[rear_right].longitudinal_force,
                wheels[front_left].tire_load,  wheels[front_right].tire_load,
                wheels[rear_left].tire_load,   wheels[rear_right].tire_load,
                wheels[front_left].angular_velocity,  wheels[front_right].angular_velocity,
                wheels[rear_left].angular_velocity,   wheels[rear_right].angular_velocity,
                wheels[front_left].net_torque,  wheels[front_right].net_torque,
                wheels[rear_left].net_torque,   wheels[rear_right].net_torque,
                engine_brake_torque,
                wheels[front_left].target_compression,  wheels[front_right].target_compression,
                wheels[rear_left].target_compression,   wheels[rear_right].target_compression,
                wheels[front_left].compression,         wheels[front_right].compression,
                wheels[rear_left].compression,          wheels[rear_right].compression,
                sweep_distance[front_left],             sweep_distance[front_right],
                sweep_distance[rear_left],              sweep_distance[rear_right],
                spring_force[front_left],               spring_force[front_right],
                spring_force[rear_left],                spring_force[rear_right],
                wheels[front_left].contact_normal.y,    wheels[front_right].contact_normal.y,
                wheels[rear_left].contact_normal.y,     wheels[rear_right].contact_normal.y,
                // upgrades
                car::upgrades.engine, car::upgrades.suspension, car::upgrades.tires, car::upgrades.brakes, car::upgrades.aero, car::upgrades.weight,
                // brake temp wear
                wheels[front_left].brake_temp, wheels[front_right].brake_temp, wheels[rear_left].brake_temp, wheels[rear_right].brake_temp,
                wheels[front_left].wear,       wheels[front_right].wear,       wheels[rear_left].wear,       wheels[rear_right].wear,
                // thermals
                wheels[front_left].thermal.avg_surface(), wheels[front_right].thermal.avg_surface(),
                wheels[rear_left].thermal.avg_surface(),  wheels[rear_right].thermal.avg_surface(),
                wheels[front_left].thermal.core, wheels[front_right].thermal.core,
                wheels[rear_left].thermal.core,  wheels[rear_right].thermal.core,
                // effs and factors what actually multiplies grip brake this tick
                be_fl, be_fr, be_rl, be_rr,
                tg_fl, tg_fr, tg_rl, tg_rr,
                wg_fl, wg_fr, wg_rl, wg_rr,
                // computed geometry used in this ticks slip and force
                wheels[front_left].effective_radius, wheels[front_right].effective_radius,
                wheels[rear_left].effective_radius,  wheels[rear_right].effective_radius,
                wheels[front_left].dynamic_camber,   wheels[front_right].dynamic_camber,
                wheels[rear_left].dynamic_camber,    wheels[rear_right].dynamic_camber,
                abs_active[front_left] ? 1 : 0, abs_active[front_right] ? 1 : 0,
                abs_active[rear_left] ? 1 : 0,  abs_active[rear_right] ? 1 : 0,
                cfg.mass, tuning::spec.tire_friction, tuning::spec.brake_force, tuning::spec.engine_peak_torque);

            if (frame_counter % 200 == 0)
            {
                fflush(file);
            }
            frame_counter++;
        }
    };
    inline telemetry_dump  telemetry;

    struct debug_sweep_data
    {
        PxVec3 origin;
        PxVec3 hit_point;
        bool   hit;
    };
    inline debug_sweep_data debug_sweep[wheel_count];
    inline PxVec3           debug_suspension_top[wheel_count];
    inline PxVec3           debug_suspension_bottom[wheel_count];

    // pre-filter that skips the car's own body during suspension sweeps/raycasts
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
    inline SelfFilterCallback self_filter;

    // wheel state heals: scrub nan or inf out of the per wheel fields so a single division by zero
    // anywhere in the sim cannot poison angular velocity or rotation for the rest of the run
    inline bool sanitize_float(float& v, float fallback = 0.0f)
    {
        if (!std::isfinite(v))
        {
            v = fallback;
            return true;
        }
        return false;
    }

    inline bool sanitize_vec(PxVec3& v, const PxVec3& fallback = PxVec3(0.0f))
    {
        bool fixed = false;
        if (!std::isfinite(v.x))
        {
            v.x = fallback.x;
            fixed = true;
        }
        if (!std::isfinite(v.y))
        {
            v.y = fallback.y;
            fixed = true;
        }
        if (!std::isfinite(v.z))
        {
            v.z = fallback.z;
            fixed = true;
        }
        return fixed;
    }

    inline bool sanitize_wheel_state(int i)
    {
        if (i < 0 || i >= wheel_count)
        {
            return false;
        }
        wheel& w = wheels[i];
        bool fixed = false;
        fixed |= sanitize_float(w.compression);
        fixed |= sanitize_float(w.target_compression);
        fixed |= sanitize_float(w.prev_compression);
        fixed |= sanitize_float(w.compression_velocity);
        fixed |= sanitize_float(w.angular_velocity);
        fixed |= sanitize_float(w.rotation);
        fixed |= sanitize_float(w.tire_load);
        fixed |= sanitize_float(w.slip_angle);
        fixed |= sanitize_float(w.slip_ratio);
        fixed |= sanitize_float(w.lateral_force);
        fixed |= sanitize_float(w.longitudinal_force);
        fixed |= sanitize_float(w.net_torque);
        fixed |= sanitize_vec(w.contact_point);
        fixed |= sanitize_vec(w.contact_normal, PxVec3(0, 1, 0));
        fixed |= sanitize_float(w.wear);
        fixed |= sanitize_float(w.brake_temp);
        fixed |= sanitize_float(w.thermal.surface[0]);
        fixed |= sanitize_float(w.thermal.surface[1]);
        fixed |= sanitize_float(w.thermal.surface[2]);
        fixed |= sanitize_float(w.thermal.core);
        fixed |= sanitize_float(w.effective_radius);
        fixed |= sanitize_float(w.dynamic_camber);
        return fixed;
    }

    inline bool  is_front(int i)                { return i == front_left || i == front_right; }
    inline bool  is_rear(int i)                 { return i == rear_left || i == rear_right; }
    inline bool  is_driven(int i)
    {
        if (tuning::spec.drivetrain_type == 0)
        {
            return is_rear(i);
        }   // rwd
        if (tuning::spec.drivetrain_type == 1)
        {
            return is_front(i);
        }  // fwd
        return true;                                                // awd
    }
    inline float lerp(float a, float b, float t){ return a + (b - a) * t; }
    inline float exp_decay(float rate, float dt){ return 1.0f - expf(-rate * dt); }

    inline bool        is_valid_wheel(int i) { return i >= 0 && i < wheel_count; }
    inline const char* get_wheel_name(int i) { return is_valid_wheel(i) ? wheel_names[i] : "??"; }
}
