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
#include <cstring>
#include <deque>
#include <string>
#include <vector>
//==========================================

// static per-car preset data: engine/gearbox/chassis/tires/suspension/aero tuning tables.
// this file is pure data; it has no dependency on the simulation state in CarSimulation.h.

namespace car
{
    static constexpr int max_gears = 11;

    enum class suspension_mechanism : int
    {
        double_wishbone,
        macpherson,
        multi_link
    };

    struct suspension_geometry
    {
        suspension_mechanism mechanism = suspension_mechanism::double_wishbone;
        float chassis_inset             = 0.48f;
        float upper_inner_y             = 0.18f;
        float lower_inner_y             = -0.16f;
        float upper_upright_y           = 0.18f;
        float lower_upright_y           = -0.18f;
        float arm_span                   = 0.20f;
        float strut_top_y                = 0.42f;
        float strut_top_inset            = 0.32f;
        float tie_rod_y                  = 0.02f;
        float tie_rod_z                  = -0.14f;
        float link_spread_y              = 0.16f;
        float link_spread_z              = 0.22f;
    };

    struct assist_settings
    {
        float steering_speed_reduction = 0.45f;
        float steering_speed_reference = 50.0f;
        float abs_level = 1.0f;
        float traction_control_level = 1.0f;
    };

    struct validation_targets
    {
        float settle_speed_max = 0.05f;
        float zero_to_100_min = 2.0f;
        float zero_to_100_max = 12.0f;
        float braking_distance_min = 25.0f;
        float braking_distance_max = 55.0f;
        float skidpad_g_min = 0.70f;
        float skidpad_g_max = 1.50f;
    };

    struct car_preset
    {
        car_preset()
        {
            memset(this, 0, sizeof(car_preset));
            front_geometry       = suspension_geometry();
            rear_geometry        = suspension_geometry();
            upright_mass         = 14.0f;
            suspension_link_mass = 2.5f;
            steering_rack_mass   = 4.0f;
            assists              = assist_settings();
            validation           = validation_targets();
        }

        const char* name;

        // chassis (physical dimensions and mass)
        // these drive the simulation geometry, weight transfer, inertia and the
        // body itself so swapping presets actually changes the car's physical footprint
        float mass;                  // kg, curb weight with fluids
        float length;                // m, overall vehicle length
        float width;                 // m, overall vehicle width (no mirrors)
        float height;                // m, overall vehicle height
        float wheelbase;             // m, axle to axle distance
        float track_front;           // m, distance between front wheel centers
        float track_rear;            // m, distance between rear wheel centers
        float suspension_height;     // m, ride height of chassis bottom above wheel center
        float suspension_travel;     // m, available compression travel per wheel
        float front_wheel_radius;
        float rear_wheel_radius;
        float front_wheel_width;
        float rear_wheel_width;
        float wheel_mass;
        suspension_geometry front_geometry;
        suspension_geometry rear_geometry;
        float upright_mass;
        float suspension_link_mass;
        float steering_rack_mass;
        assist_settings assists;
        validation_targets validation;

        // engine
        float engine_idle_rpm;
        float engine_redline_rpm;
        float engine_max_rpm;
        float engine_peak_torque;
        float engine_peak_torque_rpm;
        float engine_inertia;
        float engine_friction;
        float engine_rpm_smoothing;
        float downshift_blip_amount;
        float downshift_blip_duration;

        // gearbox (indices: 0=reverse, 1=neutral, 2=1st, 3=2nd, ...)
        float gear_ratios[max_gears];
        int   gear_count;
        float final_drive;
        float shift_up_rpm;
        float shift_down_rpm;
        float shift_time;
        float clutch_engagement_rate;
        float drivetrain_efficiency;
        bool  manual_transmission;

        // shift speed thresholds (indexed by gear, km/h)
        float upshift_speed_base[max_gears];
        float upshift_speed_sport[max_gears];
        float downshift_speeds[max_gears];

        // brakes
        float brake_force;
        float brake_bias_front;
        float reverse_power_ratio;
        float brake_ambient_temp;
        float brake_optimal_temp;
        float brake_fade_temp;
        float brake_max_temp;
        float brake_heat_coefficient;
        float brake_cooling_base;
        float brake_cooling_airflow;
        float brake_thermal_mass;

        // input: rise-rate (exp) for pedal inputs - release is always instantaneous
        // (presses smooth up, lift-offs snap down)
        float throttle_smoothing;
        float brake_smoothing;

        // pacejka magic formula coefficients
        float lat_B;
        float lat_C;
        float lat_D;
        float lat_E;
        float long_B;
        float long_C;
        float long_D;
        float long_E;

        // load-dependent stiffness
        float load_B_scale_min;

        // pneumatic trail model
        float pneumatic_trail_max;
        float pneumatic_trail_peak;

        // tire grip
        float tire_friction;
        float min_slip_speed;
        float load_sensitivity;
        float load_reference;
        float rear_grip_ratio;           // rear axle mu relative to front, 1.0 = matched compound
        float slip_angle_deadband;
        float camber_thrust_coeff;
        float max_slip_angle;
        float tire_pressure;             // bar, nominal hot pressure
        float tire_pressure_optimal;     // bar, ideal operating pressure

        // tire thermals
        float tire_ambient_temp;
        float tire_optimal_temp;
        float tire_temp_range;
        float tire_heat_from_slip;
        float tire_heat_from_rolling;
        float tire_cooling_rate;
        float tire_cooling_airflow;
        float tire_grip_temp_factor;
        float tire_min_temp;
        float tire_max_temp;
        float tire_relaxation_length;
        float tire_wear_rate;
        float tire_wear_heat_mult;
        float tire_grip_wear_loss;
        float tire_core_transfer_rate;   // heat transfer rate between surface and core
        float tire_surface_response;     // surface temperature response multiplier (flash heat)

        // suspension
        float front_spring_freq;
        float rear_spring_freq;
        float front_damping_ratio;
        float rear_damping_ratio;
        float damping_bump_ratio;
        float damping_rebound_ratio;
        float front_arb_stiffness;
        float rear_arb_stiffness;
        float max_susp_force;
        float max_damper_velocity;
        float bump_stop_stiffness;
        float bump_stop_threshold;

        // aerodynamics
        float rolling_resistance;
        float drag_coeff;
        float frontal_area;
        float lift_coeff_front;
        float lift_coeff_rear;
        bool  drs_enabled;
        float drs_rear_cl_factor;
        float side_area;
        bool  ground_effect_enabled;
        float ground_effect_multiplier;
        float ground_effect_height_ref;
        float ground_effect_height_max;
        bool  yaw_aero_enabled;
        float yaw_drag_multiplier;
        float yaw_side_force_coeff;
        bool  pitch_aero_enabled;
        float pitch_sensitivity;
        float aero_center_height;
        float aero_center_front_z;
        float aero_center_rear_z;

        // center of mass
        float center_of_mass_x;
        float center_of_mass_y;
        float center_of_mass_z;

        // steering
        float max_steer_angle;
        float steering_rate;
        float self_align_gain;            // scales pneumatic trail sat, 1.0 = physical
        float steering_linearity;

        // alignment (radians)
        float front_camber;
        float rear_camber;
        float front_toe;
        float rear_toe;
        float tire_vertical_stiffness;
        float lsd_viscous;
        float abs_load_sensitivity;

        // wheels
        float airborne_wheel_decay;
        float bearing_friction;
        float handbrake_torque;

        // drivetrain layout (0 = rwd, 1 = fwd, 2 = awd)
        int   drivetrain_type;
        float torque_split_front;   // awd only: 0.0 = full rear, 1.0 = full front

        // differential (0 = open, 1 = locked, 2 = lsd)
        float lsd_preload;
        float lsd_lock_ratio_accel;
        float lsd_lock_ratio_decel;
        int   diff_type;

        int engine_stage_max;
        int suspension_stage_max;
        int tires_stage_max;
        int brakes_stage_max;
        int aero_stage_max;
        int weight_stage_max;

        // driveshaft
        float driveshaft_stiffness;  // Nm/rad torsional compliance

        // input behavior
        float input_deadzone;
        float steering_deadzone;
        float braking_speed_threshold;

        // damping
        float linear_damping;
        float angular_damping;

        // abs
        bool  abs_enabled;
        float abs_slip_threshold;
        float abs_release_rate;
        float abs_pulse_frequency;

        // traction control
        bool  tc_enabled;
        float tc_slip_threshold;
        float tc_power_reduction;
        float tc_response_rate;

        // turbo
        bool  turbo_enabled;
        float boost_max_pressure;
        float boost_spool_rate;
        float boost_wastegate_rpm;
        float boost_torque_mult;
        float boost_min_rpm;

        // electric motor (HY-KERS for laferrari etc)
        bool  electric_enabled;
        float electric_motor_torque;    // Nm peak
        float electric_motor_power_kw;
        float electric_motor_max_rpm;
        float electric_torque_response; // response rate
    };

    //= car definition =============================================================

    // a complete car described by a .car xml file: visuals, geometry and performance
    struct car_definition
    {
        std::string name;
        std::string file_path;

        // body visual, optional, a missing model uses a generic body
        std::string body_model;
        float       body_scale     = 1.0f;
        float       body_forward_z = 1.0f;         // sign of the local z axis the nose points at
        std::vector<std::string> body_hide_parts;  // baked in parts to deactivate, the spawned wheels replace them

        // wheel visual
        std::string wheel_model;
        std::string wheel_albedo;
        std::string wheel_metalness;
        std::string wheel_normal;
        std::string wheel_roughness;

        // simulation tuning
        car_preset performance;
    };

    //= car registry ===============================================================

    struct preset_entry
    {
        const char* name;
        car_preset* instance;
        const car_definition* definition;
    };

    // filled from .car files, a deque never relocates so pointers into it stay valid
    inline std::deque<car_definition> definitions;
    inline std::vector<preset_entry> preset_registry;
    inline int preset_count        = 0;
    inline int active_preset_index = 0;

    // load one .car file, cached by path, also registers it for the hud preset selector
    const car_definition* load_car_file(const std::string& file_path);

    // load every .car file in a directory
    void load_car_directory(const std::string& directory);
}
