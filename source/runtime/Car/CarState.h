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

    // preset loading rebuilds cached geometry and mass properties
    void load_car(const car_preset& new_spec);
    void reset_drivetrain_transients();
    void reset_wheel_thermals();
    void clamp_upgrade_stage(int& stage, int max_stage);
    void reapply_upgrades();
    void reset_upgrades();

    namespace tuning
    {
        inline car_preset spec = car_preset();

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

        inline bool log_pacejka     = false;
        inline bool log_telemetry   = false;
        // file telemetry opens lazily in the working directory
        inline bool log_to_file     = true;
    }

    // rejects invalid vectors before physx api calls
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
        PxVec3 side_aero_pos    = PxVec3(0);
        float  ride_height      = 0.0f;
        float  yaw_angle        = 0.0f;
        float  ground_effect_factor = 1.0f;
        bool   valid            = false;
    };
    // namespace state must use inline external linkage to remain shared across translation units
    inline aero_debug_data aero_debug;

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

    // function local storage avoids duplicate definitions
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

    // shared state for the single active vehicle
    inline PxRigidDynamic* body             = nullptr;
    inline PxMaterial*     material         = nullptr;
    inline PxMaterial*     wheel_guard_material = nullptr;
    inline PxConvexMesh*   wheel_sweep_mesh = nullptr;
    inline config          cfg;
    inline car_preset      base_spec;
    inline active_upgrades upgrades;
    inline wheel           wheels[wheel_count];
    inline input_state     input;
    inline input_state     input_target;
    inline assist_command  assisted_actuators;
    inline PxVec3          wheel_offsets[wheel_count];
    inline float           wheel_moi[wheel_count];
    inline float           spring_stiffness[wheel_count];
    inline float           spring_damping[wheel_count];
    // final suspension force applied at each corner
    inline float           spring_force[wheel_count]   = {};
    // negative sweep distance means the wheel geometry overlaps the surface
    inline float           sweep_distance[wheel_count] = {};
    inline float           abs_phase               = 0.0f;
    inline bool            abs_active[wheel_count] = {};
    inline float           tc_reduction            = 0.0f;
    inline bool            tc_active               = false;
    inline float           engine_rpm              = 800.0f;
    inline float           engine_rotation         = 0.0f;
    // gear indices are zero reverse one neutral and two onward forward
    inline int             current_gear            = 1;

    inline bool is_in_reverse()      { return current_gear == 0; }
    inline bool is_in_neutral()      { return current_gear == 1; }
    inline bool is_in_forward_gear() { return current_gear >= 2; }
    inline float           shift_timer             = 0.0f;
    inline bool            is_shifting             = false;
    inline float           clutch                  = 1.0f;
    inline float           shift_cooldown          = 0.0f;
    // cooldown prevents automatic shift hunting
    inline constexpr float shift_cooldown_time     = 0.5f;
    inline int             last_shift_direction    = 0;
    inline float           previous_automatic_throttle = 0.0f;
    inline float           boost_pressure          = 0.0f;
    inline float           motor_torque            = 0.0f;
    inline bool            rev_limiter_active      = false;
    inline float           downshift_blip_timer    = 0.0f;
    inline float           driveshaft_twist        = 0.0f;
    // reflected engine inertia is added to each driven wheel inertia
    inline float           reflected_engine_inertia = 0.0f;
    inline bool            drs_active              = false;
    inline float           longitudinal_accel      = 0.0f;
    inline float           lateral_accel           = 0.0f;
    inline PxVec3          prev_velocity           = PxVec3(0);
    inline float           vehicle_sleep_timer     = 0.0f;
    inline bool            vehicle_sleeping        = false;
    // total engine braking torque routed to the driven axle
    inline float           engine_brake_torque     = 0.0f;
    inline float           engine_output_torque    = 0.0f;
    inline float           axle_drive_torque       = 0.0f;

    // telemetry writes fixed step body wheel and component state to csv
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

        void flush()
        {
            if (file)
            {
                fflush(file);
            }
        }

        // absolute path of the csv when known, empty if not open yet
        std::string absolute_path() const
        {
            char abs_path[1024] = {};
            if (_fullpath(abs_path, "car_telemetry.csv", sizeof(abs_path)))
            {
                return abs_path;
            }
            return "car_telemetry.csv";
        }

        // reopen for append without truncating an existing csv
        bool reopen_append()
        {
            if (file)
            {
                return true;
            }
            fopen_s(&file, "car_telemetry.csv", "a");
            return file != nullptr;
        }

        // flush, temporarily close, read the last max_rows lines, reopen for append
        bool snapshot_tail(int max_rows, std::string& out_text, std::string& out_path, int& out_total_lines)
        {
            out_text.clear();
            out_path = absolute_path();
            out_total_lines = 0;
            if (!tuning::log_to_file)
            {
                return false;
            }
            flush();
            if (file)
            {
                fclose(file);
                file = nullptr;
            }

            FILE* read_file = nullptr;
            fopen_s(&read_file, "car_telemetry.csv", "r");
            if (!read_file)
            {
                reopen_append();
                return false;
            }

            std::vector<std::string> lines;
            char buffer[8192];
            while (fgets(buffer, sizeof(buffer), read_file))
            {
                lines.emplace_back(buffer);
            }
            fclose(read_file);
            out_total_lines = static_cast<int>(lines.size());

            if (lines.empty())
            {
                reopen_append();
                return true;
            }

            // always keep header, then the last max_rows data rows
            out_text = lines[0];
            if (!out_text.empty() && out_text.back() != '\n')
            {
                out_text.push_back('\n');
            }
            const int data_count = std::max(0, out_total_lines - 1);
            const int start = 1 + std::max(0, data_count - std::max(max_rows, 0));
            for (int i = start; i < out_total_lines; i++)
            {
                out_text += lines[static_cast<size_t>(i)];
                if (!out_text.empty() && out_text.back() != '\n')
                {
                    out_text.push_back('\n');
                }
            }

            return reopen_append();
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

            // header and writer order form one external schema and must remain aligned
            fprintf(file,
                // time + body state
                "frame,time,dt,car_name,"
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
                // suspension diagnostics from the physical assembly and contact sweep
                // contact_ny is the y component of the contact normal so we can spot tilted or
                // degenerate ground hits, key to debugging chassis on ground vs spring on ground
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
                // effective wheel geometry used by tire forces
                "fl_eff_r,fr_eff_r,rl_eff_r,rr_eff_r,"
                "fl_dyn_camb,fr_dyn_camb,rl_dyn_camb,rr_dyn_camb,"
                "fl_abs,fr_abs,rl_abs,rr_abs,"
                "mass,tire_friction,brake_force,engine_peak_tq,"
                "rot_x,rot_y,rot_z,rot_w,vel_x,vel_y,vel_z,ang_vel_x,ang_vel_y,ang_vel_z,"
                "target_throttle,target_brake,target_steering,target_handbrake,"
                "active_gear_ratio,shift_timer,shift_cooldown,last_shift_direction,engine_rotation,boost_pressure,motor_torque,engine_output_torque,axle_drive_torque,driveshaft_twist,driveshaft_torque,reflected_engine_inertia,rev_limiter,downshift_blip_timer,abs_phase,vehicle_sleeping,vehicle_sleep_timer,drs_active,"
                "assist_engine_scale,fl_assist_brake_scale,fr_assist_brake_scale,rl_assist_brake_scale,rr_assist_brake_scale,"
                "aero_valid,aero_ride_height,aero_yaw_angle,aero_ground_effect,aero_drag_x,aero_drag_y,aero_drag_z,aero_front_downforce_x,aero_front_downforce_y,aero_front_downforce_z,aero_rear_downforce_x,aero_rear_downforce_y,aero_rear_downforce_z,aero_side_force_x,aero_side_force_y,aero_side_force_z,"
                "fl_rotation,fl_drive_torque,fl_brake_torque,fl_comp_velocity,fl_surface,fl_contact_x,fl_contact_y,fl_contact_z,fl_contact_nx,fl_contact_nz,fl_dynamic_toe,fl_bump_steer,fl_motion_ratio,fl_shock_length,fl_shock_rest_length,fl_shock_velocity,fl_temp_inside,fl_temp_middle,fl_temp_outside,fl_condition_grip,fl_condition_stiffness,fl_condition_relaxation,fl_wheel_moi,fl_spring_stiffness,fl_spring_damping,"
                "fr_rotation,fr_drive_torque,fr_brake_torque,fr_comp_velocity,fr_surface,fr_contact_x,fr_contact_y,fr_contact_z,fr_contact_nx,fr_contact_nz,fr_dynamic_toe,fr_bump_steer,fr_motion_ratio,fr_shock_length,fr_shock_rest_length,fr_shock_velocity,fr_temp_inside,fr_temp_middle,fr_temp_outside,fr_condition_grip,fr_condition_stiffness,fr_condition_relaxation,fr_wheel_moi,fr_spring_stiffness,fr_spring_damping,"
                "rl_rotation,rl_drive_torque,rl_brake_torque,rl_comp_velocity,rl_surface,rl_contact_x,rl_contact_y,rl_contact_z,rl_contact_nx,rl_contact_nz,rl_dynamic_toe,rl_bump_steer,rl_motion_ratio,rl_shock_length,rl_shock_rest_length,rl_shock_velocity,rl_temp_inside,rl_temp_middle,rl_temp_outside,rl_condition_grip,rl_condition_stiffness,rl_condition_relaxation,rl_wheel_moi,rl_spring_stiffness,rl_spring_damping,"
                "rr_rotation,rr_drive_torque,rr_brake_torque,rr_comp_velocity,rr_surface,rr_contact_x,rr_contact_y,rr_contact_z,rr_contact_nx,rr_contact_nz,rr_dynamic_toe,rr_bump_steer,rr_motion_ratio,rr_shock_length,rr_shock_rest_length,rr_shock_velocity,rr_temp_inside,rr_temp_middle,rr_temp_outside,rr_condition_grip,rr_condition_stiffness,rr_condition_relaxation,rr_wheel_moi,rr_spring_stiffness,rr_spring_damping,"
                "fl_hub_x,fl_hub_y,fl_hub_z,fl_hub_vx,fl_hub_vy,fl_hub_vz,fl_hub_wx,fl_hub_wy,fl_hub_wz,fr_hub_x,fr_hub_y,fr_hub_z,fr_hub_vx,fr_hub_vy,fr_hub_vz,fr_hub_wx,fr_hub_wy,fr_hub_wz,rl_hub_x,rl_hub_y,rl_hub_z,rl_hub_vx,rl_hub_vy,rl_hub_vz,rl_hub_wx,rl_hub_wy,rl_hub_wz,rr_hub_x,rr_hub_y,rr_hub_z,rr_hub_vx,rr_hub_vy,rr_hub_vz,rr_hub_wx,rr_hub_wy,rr_hub_wz\n");
            frame_counter = 0;
            elapsed_time  = 0.0f;
            return true;
        }

        void write_wheel_state(int i)
        {
            const wheel& w = wheels[i];
            fprintf(file, "%.6g,%.6g,%.6g,%.6g,%d,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g", w.rotation, w.drive_torque, w.brake_torque, w.compression_velocity, static_cast<int>(w.contact_surface), w.contact_point.x, w.contact_point.y, w.contact_point.z, w.contact_normal.x, w.contact_normal.z, w.dynamic_toe, w.bump_steer, w.motion_ratio, w.shock_length, w.shock_rest_length, w.shock_velocity, w.thermal.surface[0], w.thermal.surface[1], w.thermal.surface[2], w.condition_grip, w.condition_stiffness, w.condition_relaxation, wheel_moi[i], spring_stiffness[i], spring_damping[i]);
        }

        void tick(float dt, float speed_kmh)
        {
            if (!tuning::log_to_file)
            {
                close();
                return;
            }
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

            fprintf(file,
                "%d,%.3f,%.4f,\"%s\","
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
                "%.1f,%.3f,%.1f,%.1f,"
                "%.6g,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g,"
                "%.6g,%.6g,%.6g,%.6g,"
                "%.6g,%.6g,%.6g,%d,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g,%d,%.6g,%.6g,%d,%.6g,%d,"
                "%.6g,%.6g,%.6g,%.6g,%.6g,"
                "%d,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g,",
                frame_counter, elapsed_time, dt, tuning::spec.name ? tuning::spec.name : "",
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
                wheels[front_left].brake_efficiency, wheels[front_right].brake_efficiency, wheels[rear_left].brake_efficiency, wheels[rear_right].brake_efficiency,
                wheels[front_left].temperature_grip, wheels[front_right].temperature_grip, wheels[rear_left].temperature_grip, wheels[rear_right].temperature_grip,
                wheels[front_left].wear_grip, wheels[front_right].wear_grip, wheels[rear_left].wear_grip, wheels[rear_right].wear_grip,
                // computed geometry used in this ticks slip and force
                wheels[front_left].effective_radius, wheels[front_right].effective_radius,
                wheels[rear_left].effective_radius,  wheels[rear_right].effective_radius,
                wheels[front_left].dynamic_camber,   wheels[front_right].dynamic_camber,
                wheels[rear_left].dynamic_camber,    wheels[rear_right].dynamic_camber,
                abs_active[front_left] ? 1 : 0, abs_active[front_right] ? 1 : 0,
                abs_active[rear_left] ? 1 : 0,  abs_active[rear_right] ? 1 : 0,
                cfg.mass, tuning::spec.tire_friction, tuning::spec.brake_force, tuning::spec.engine_peak_torque,
                pose.q.x, pose.q.y, pose.q.z, pose.q.w, vel.x, vel.y, vel.z, ang_vel.x, ang_vel.y, ang_vel.z,
                input_target.throttle, input_target.brake, input_target.steering, input_target.handbrake,
                tuning::spec.gear_ratios[current_gear], shift_timer, shift_cooldown, last_shift_direction, engine_rotation, boost_pressure, motor_torque, engine_output_torque, axle_drive_torque, driveshaft_twist, driveshaft_twist * tuning::spec.driveshaft_stiffness, reflected_engine_inertia, rev_limiter_active ? 1 : 0, downshift_blip_timer, abs_phase, vehicle_sleeping ? 1 : 0, vehicle_sleep_timer, drs_active ? 1 : 0,
                assisted_actuators.engine_torque_scale, assisted_actuators.brake_torque_scale[front_left], assisted_actuators.brake_torque_scale[front_right], assisted_actuators.brake_torque_scale[rear_left], assisted_actuators.brake_torque_scale[rear_right],
                aero_debug.valid ? 1 : 0, aero_debug.ride_height, aero_debug.yaw_angle, aero_debug.ground_effect_factor, aero_debug.drag_force.x, aero_debug.drag_force.y, aero_debug.drag_force.z, aero_debug.front_downforce.x, aero_debug.front_downforce.y, aero_debug.front_downforce.z, aero_debug.rear_downforce.x, aero_debug.rear_downforce.y, aero_debug.rear_downforce.z, aero_debug.side_force.x, aero_debug.side_force.y, aero_debug.side_force.z);
            for (int i = 0; i < wheel_count; i++)
            {
                if (i > 0)
                {
                    fputc(',', file);
                }
                write_wheel_state(i);
            }
            for (int i = 0; i < wheel_count; i++)
            {
                fprintf(file, ",%.6g,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g", wheels[i].hub_position.x, wheels[i].hub_position.y, wheels[i].hub_position.z, wheels[i].hub_linear_velocity.x, wheels[i].hub_linear_velocity.y, wheels[i].hub_linear_velocity.z, wheels[i].hub_angular_velocity.x, wheels[i].hub_angular_velocity.y, wheels[i].hub_angular_velocity.z);
            }
            fputc('\n', file);

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
    inline SelfFilterCallback self_filter;

    // sanitization prevents one invalid wheel value from poisoning physx
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
        fixed |= sanitize_float(w.dynamic_toe);
        fixed |= sanitize_float(w.bump_steer);
        fixed |= sanitize_float(w.motion_ratio);
        fixed |= sanitize_float(w.drive_torque);
        return fixed;
    }

    inline bool  is_front(int i)                { return i == front_left || i == front_right; }
    inline bool  is_rear(int i)                 { return i == rear_left || i == rear_right; }
    inline bool  is_driven(int i)
    {
        if (tuning::spec.drivetrain_type == 0)
        {
            return is_rear(i);
        }
        if (tuning::spec.drivetrain_type == 1)
        {
            return is_front(i);
        }
        return true;
    }
    inline float lerp(float a, float b, float t){ return a + (b - a) * t; }
    inline float exp_decay(float rate, float dt){ return 1.0f - expf(-rate * dt); }

    inline bool        is_valid_wheel(int i) { return i >= 0 && i < wheel_count; }
    inline const char* get_wheel_name(int i) { return is_valid_wheel(i) ? wheel_names[i] : "??"; }
}
