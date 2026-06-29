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
#include <string>
#include <vector>
//==========================================

// static per-car preset data: engine/gearbox/chassis/tires/suspension/aero tuning tables.
// this file is pure data; it has no dependency on the simulation state in CarSimulation.h.

namespace car
{
    static constexpr int max_gears = 11;

    struct car_preset
    {
        car_preset() { memset(this, 0, sizeof(car_preset)); }

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
        float rear_grip_ratio;
        float slip_angle_deadband;
        float min_lateral_grip;
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
        float front_roll_center_height;
        float rear_roll_center_height;

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
        float high_speed_steer_reduction;
        float steering_rate;
        float self_align_gain;
        float steering_linearity;

        // alignment (radians)
        float front_camber;
        float rear_camber;
        float front_toe;
        float rear_toe;
        float front_bump_steer;
        float rear_bump_steer;

        float front_camber_gain;
        float rear_camber_gain;
        float tire_vertical_stiffness;
        float lsd_viscous;
        float abs_load_sensitivity;
        float steer_compliance;

        // wheels
        float airborne_wheel_decay;
        float bearing_friction;
        float ground_match_rate;
        float handbrake_sliding_factor;
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
        float yaw_damping;

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
    };

    //= car presets ================================================================

    // ferrari laferrari (2013-2018) - 6.262l v12 + hy-kers, 963 ps combined,
    // 7-speed dct, mid-rear longitudinal v12, rwd
    struct laferrari_preset : car_preset
    {
        laferrari_preset()
        {
            name = "Ferrari LaFerrari";

            // chassis - carbon monocoque, 41/59 f/r curb weight distribution
            mass              = 1430.0f;  // curb weight with fluids (dry is 1255 kg)
            length            = 4.702f;
            width             = 1.992f;
            height            = 1.116f;
            wheelbase         = 2.650f;
            track_front       = 1.656f;
            track_rear        = 1.640f;
            suspension_height = 0.30f;
            suspension_travel = 0.10f;

            // engine - 6.262l f140fe v12, 800 ps @ 9000 rpm, plus hy-kers electric motor
            // peak torque is the v12 alone, kers adds electric torque on top (not modeled here)
            engine_idle_rpm         = 1000.0f;
            engine_redline_rpm      = 9250.0f;
            engine_max_rpm          = 9500.0f;
            engine_peak_torque      = 700.0f;
            engine_peak_torque_rpm  = 6750.0f;
            engine_inertia          = 0.25f;
            engine_friction         = 0.02f;
            engine_rpm_smoothing    = 6.0f;
            downshift_blip_amount   = 0.35f;
            downshift_blip_duration = 0.15f;

            // gearbox - 7-speed getrag dual-clutch (f1-dct), rear-mounted
            gear_ratios[0]          = -3.230f; // reverse
            gear_ratios[1]          =  0.0f;   // neutral
            gear_ratios[2]          =  3.083f; // 1st
            gear_ratios[3]          =  2.190f; // 2nd
            gear_ratios[4]          =  1.633f; // 3rd
            gear_ratios[5]          =  1.290f; // 4th
            gear_ratios[6]          =  1.029f; // 5th
            gear_ratios[7]          =  0.846f; // 6th
            gear_ratios[8]          =  0.691f; // 7th
            gear_count              = 9;
            final_drive             = 4.443f;
            shift_up_rpm            = 8800.0f;
            shift_down_rpm          = 3500.0f;
            shift_time              = 0.08f;
            clutch_engagement_rate  = 20.0f;
            drivetrain_efficiency   = 0.92f;
            manual_transmission     = false;

            // shift speed thresholds calibrated for final_drive 4.443 and laferrari gear set
            float up_base[]  = { 0, 0, 60, 85, 115, 150, 190, 230, 0, 0, 0 };
            float up_sport[] = { 0, 0, 80, 115, 155, 195, 245, 300, 0, 0, 0 };
            float down[]     = { 0, 0, 0, 30, 55, 80, 110, 150, 200, 0, 0 };
            for (int i = 0; i < max_gears; i++)
            {
                upshift_speed_base[i]  = up_base[i];
                upshift_speed_sport[i] = up_sport[i];
                downshift_speeds[i]    = down[i];
            }

            // brakes - brembo ccm3 carbon-ceramic, 398mm front / 380mm rear
            brake_force            = 12000.0f;
            brake_bias_front       = 0.62f;
            reverse_power_ratio    = 0.5f;
            brake_ambient_temp     = 30.0f;
            brake_optimal_temp     = 400.0f;
            brake_fade_temp        = 700.0f;
            brake_max_temp         = 900.0f;
            brake_heat_coefficient = 0.015f;
            brake_cooling_base     = 8.0f;
            brake_cooling_airflow  = 1.5f;
            brake_thermal_mass     = 5.0f;

            // input
            throttle_smoothing = 10.0f;
            brake_smoothing    = 10.0f;

            // pacejka - michelin pilot sport cup 2 compound
            lat_B  = 12.0f;
            lat_C  = 1.4f;
            lat_D  = 1.0f;
            lat_E  = -0.5f;
            long_B = 20.0f;
            long_C = 1.5f;
            long_D = 1.0f;
            long_E = -0.5f;

            load_B_scale_min    = 0.5f;
            pneumatic_trail_max  = 0.04f;
            pneumatic_trail_peak = 0.14f;

            // tire grip
            tire_friction       = 1.5f;
            min_slip_speed      = 0.5f;
            load_sensitivity    = 0.92f;
            load_reference      = 4000.0f;
            rear_grip_ratio     = 1.10f;
            slip_angle_deadband = 0.01f;
            min_lateral_grip    = 0.4f;
            camber_thrust_coeff = 0.015f;
            max_slip_angle      = 0.40f;
            tire_pressure         = 2.2f;
            tire_pressure_optimal = 2.2f;

            // tire thermals
            tire_ambient_temp      = 50.0f;
            tire_optimal_temp      = 90.0f;
            tire_temp_range        = 50.0f;
            tire_heat_from_slip    = 25.0f;
            tire_heat_from_rolling = 0.15f;
            tire_cooling_rate      = 2.0f;
            tire_cooling_airflow   = 0.05f;
            tire_grip_temp_factor  = 0.15f;
            tire_min_temp          = 10.0f;
            tire_max_temp          = 350.0f;
            tire_relaxation_length = 0.3f;
            tire_wear_rate         = 0.00001f;
            tire_wear_heat_mult    = 2.0f;
            tire_grip_wear_loss    = 0.3f;
            tire_core_transfer_rate = 0.5f;
            tire_surface_response   = 3.0f;

            // suspension - adaptive magnetorheological dampers
            front_spring_freq     = 2.2f;
            rear_spring_freq      = 2.0f;
            front_damping_ratio   = 0.68f;
            rear_damping_ratio    = 0.72f;
            damping_bump_ratio    = 0.7f;
            damping_rebound_ratio = 1.3f;
            front_arb_stiffness      = 18000.0f;
            rear_arb_stiffness       = 8000.0f;
            max_susp_force           = 35000.0f;
            max_damper_velocity      = 5.0f;
            bump_stop_stiffness      = 100000.0f;
            bump_stop_threshold      = 0.9f;
            front_roll_center_height = 0.04f;
            rear_roll_center_height  = 0.06f;

            // aerodynamics - active flaps front/rear, flat underbody with diffuser
            // claimed downforce ~360 kg at 200 km/h, max ~3:1 downforce/drag
            rolling_resistance       = 0.011f;
            drag_coeff               = 0.35f;
            frontal_area             = 1.98f;
            lift_coeff_front         = -0.30f;
            lift_coeff_rear          = -0.45f;
            drs_enabled              = false;
            drs_rear_cl_factor       = 0.3f;
            side_area                = 4.0f;
            ground_effect_enabled    = true;
            ground_effect_multiplier = 1.5f;
            ground_effect_height_ref = 0.15f;
            ground_effect_height_max = 0.30f;
            yaw_aero_enabled         = true;
            yaw_drag_multiplier      = 2.5f;
            yaw_side_force_coeff     = 1.2f;
            pitch_aero_enabled       = true;
            pitch_sensitivity        = 0.5f;
            aero_center_height       = 0.3f;
            aero_center_front_z      = 0.0f;
            aero_center_rear_z       = 0.0f;

            // center of mass - mid-rear v12, kers battery pack low in floor, very low cg
            // z is derived from 41/59 distribution over a 2.65 m wheelbase
            center_of_mass_x = 0.0f;
            center_of_mass_y = -0.15f;
            center_of_mass_z = -0.238f;

            // steering
            max_steer_angle            = 0.65f;
            high_speed_steer_reduction = 0.4f;
            steering_rate              = 1.5f;
            self_align_gain            = 0.5f;
            steering_linearity         = 1.3f;

            // alignment
            front_camber     = -1.5f * (3.14159265f / 180.0f);
            rear_camber      = -1.0f * (3.14159265f / 180.0f);
            front_toe        =  0.1f * (3.14159265f / 180.0f);
            rear_toe         =  0.2f * (3.14159265f / 180.0f);
            front_bump_steer = -0.02f;
            rear_bump_steer  =  0.01f;

            front_camber_gain = -0.7f;
            rear_camber_gain  = -0.4f;
            tire_vertical_stiffness = 180000.0f;
            lsd_viscous = 40.0f;
            abs_load_sensitivity = 0.15f;
            steer_compliance = 0.04f;

            // wheels
            airborne_wheel_decay     = 0.99f;
            bearing_friction         = 0.2f;
            ground_match_rate        = 8.0f;
            // electronic parking brake only, no traditional handbrake lever
            handbrake_sliding_factor = 0.75f;
            handbrake_torque         = 2500.0f;

            // drivetrain layout - rear wheel drive
            drivetrain_type      = 0;
            torque_split_front   = 0.0f;

            // differential - e-diff (electronically controlled lsd, tied to f1-trac)
            lsd_preload          = 150.0f;
            lsd_lock_ratio_accel = 0.5f;
            lsd_lock_ratio_decel = 0.3f;
            diff_type            = 2;
            driveshaft_stiffness = 8000.0f;

            // input behavior
            input_deadzone          = 0.01f;
            steering_deadzone       = 0.001f;
            braking_speed_threshold = 3.0f;

            // damping
            linear_damping  = 0.001f;
            angular_damping = 0.05f;
            yaw_damping     = 3500.0f;

            // abs
            abs_enabled         = false;
            abs_slip_threshold  = 0.15f;
            abs_release_rate    = 0.7f;
            abs_pulse_frequency = 15.0f;

            // traction control
            tc_enabled         = false;
            tc_slip_threshold  = 0.08f;
            tc_power_reduction = 0.8f;
            tc_response_rate   = 15.0f;

            // turbo - not applicable (naturally aspirated)
            turbo_enabled       = false;
            boost_max_pressure  = 0.0f;
            boost_spool_rate    = 0.0f;
            boost_wastegate_rpm = 0.0f;
            boost_torque_mult   = 0.0f;
            boost_min_rpm       = 0.0f;

            engine_stage_max     = 1;
            suspension_stage_max = 1;
            tires_stage_max      = 1;
            brakes_stage_max     = 1;
            aero_stage_max       = 0;
            weight_stage_max     = 1;

        }
    };

    // porsche 911 gt3 (992, 2022+) - 4.0l naturally-aspirated flat-6,
    // 510 ps @ 8400 rpm, 470 nm @ 6100 rpm, 7-speed pdk, rear-engine rwd
    struct gt3_992_preset : car_preset
    {
        gt3_992_preset()
        {
            name = "Porsche 911 GT3 (992)";

            // chassis - 39/61 f/r curb weight distribution (pdk)
            mass              = 1435.0f;
            length            = 4.573f;
            width             = 1.852f;
            height            = 1.279f;
            wheelbase         = 2.457f;
            track_front       = 1.575f;
            track_rear        = 1.558f;
            suspension_height = 0.28f;
            suspension_travel = 0.10f;

            // engine - 4.0l flat-6 boxer naturally aspirated, peak power 510 ps at 8400 rpm
            engine_idle_rpm         = 800.0f;
            engine_redline_rpm      = 9000.0f;
            engine_max_rpm          = 9200.0f;
            engine_peak_torque      = 470.0f;
            engine_peak_torque_rpm  = 6100.0f;
            engine_inertia          = 0.20f;
            engine_friction         = 0.02f;
            engine_rpm_smoothing    = 6.0f;
            downshift_blip_amount   = 0.40f;
            downshift_blip_duration = 0.12f;

            // gearbox - 7-speed pdk dual-clutch
            gear_ratios[0]          = -3.550f; // reverse
            gear_ratios[1]          =  0.0f;   // neutral
            gear_ratios[2]          =  3.910f; // 1st
            gear_ratios[3]          =  2.290f; // 2nd
            gear_ratios[4]          =  1.580f; // 3rd
            gear_ratios[5]          =  1.190f; // 4th
            gear_ratios[6]          =  0.970f; // 5th
            gear_ratios[7]          =  0.830f; // 6th
            gear_ratios[8]          =  0.680f; // 7th
            gear_count              = 9;
            final_drive             = 3.970f;
            shift_up_rpm            = 8800.0f;
            shift_down_rpm          = 3500.0f;
            shift_time              = 0.08f;
            clutch_engagement_rate  = 20.0f;
            drivetrain_efficiency   = 0.92f;
            manual_transmission     = false;

            // shift speed thresholds recalibrated for final_drive 3.97 and gt3 ratios
            float up_base[]  = { 0, 0, 50, 80, 115, 155, 195, 240, 0, 0, 0 };
            float up_sport[] = { 0, 0, 70, 110, 150, 195, 245, 310, 0, 0, 0 };
            float down[]     = { 0, 0, 0, 25, 50, 80, 115, 155, 200, 0, 0 };
            for (int i = 0; i < max_gears; i++)
            {
                upshift_speed_base[i]  = up_base[i];
                upshift_speed_sport[i] = up_sport[i];
                downshift_speeds[i]    = down[i];
            }

            // brakes - 408mm front / 380mm rear cast iron (pccb optional, similar response)
            brake_force            = 10000.0f;
            brake_bias_front       = 0.62f;
            reverse_power_ratio    = 0.5f;
            brake_ambient_temp     = 30.0f;
            brake_optimal_temp     = 400.0f;
            brake_fade_temp        = 700.0f;
            brake_max_temp         = 900.0f;
            brake_heat_coefficient = 0.015f;
            brake_cooling_base     = 9.0f;
            brake_cooling_airflow  = 1.8f;
            brake_thermal_mass     = 4.5f;

            // input
            throttle_smoothing = 10.0f;
            brake_smoothing    = 10.0f;

            // pacejka - michelin pilot sport cup 2 r
            lat_B  = 12.0f;
            lat_C  = 1.4f;
            lat_D  = 1.0f;
            lat_E  = -0.5f;
            long_B = 20.0f;
            long_C = 1.5f;
            long_D = 1.0f;
            long_E = -0.5f;

            load_B_scale_min     = 0.5f;
            pneumatic_trail_max  = 0.04f;
            pneumatic_trail_peak = 0.14f;

            // tire grip - cup 2 r compound, slightly grippier
            tire_friction       = 1.6f;
            min_slip_speed      = 0.5f;
            load_sensitivity    = 0.92f;
            load_reference      = 4000.0f;
            rear_grip_ratio     = 1.08f;
            slip_angle_deadband = 0.01f;
            min_lateral_grip    = 0.4f;
            camber_thrust_coeff = 0.015f;
            max_slip_angle      = 0.40f;
            tire_pressure         = 2.3f;
            tire_pressure_optimal = 2.3f;

            // tire thermals
            tire_ambient_temp      = 50.0f;
            tire_optimal_temp      = 90.0f;
            tire_temp_range        = 50.0f;
            tire_heat_from_slip    = 25.0f;
            tire_heat_from_rolling = 0.15f;
            tire_cooling_rate      = 2.0f;
            tire_cooling_airflow   = 0.05f;
            tire_grip_temp_factor  = 0.15f;
            tire_min_temp          = 10.0f;
            tire_max_temp          = 350.0f;
            tire_relaxation_length = 0.3f;
            tire_wear_rate         = 0.00001f;
            tire_wear_heat_mult    = 2.0f;
            tire_grip_wear_loss    = 0.3f;
            tire_core_transfer_rate = 0.5f;
            tire_surface_response   = 3.0f;

            // suspension - double wishbone front, multi-link rear, stiffer track setup
            front_spring_freq     = 2.8f;
            rear_spring_freq      = 2.5f;
            front_damping_ratio   = 0.70f;
            rear_damping_ratio    = 0.74f;
            damping_bump_ratio    = 0.7f;
            damping_rebound_ratio = 1.3f;
            front_arb_stiffness      = 20000.0f;
            rear_arb_stiffness       = 10000.0f;
            max_susp_force           = 35000.0f;
            max_damper_velocity      = 5.0f;
            bump_stop_stiffness      = 100000.0f;
            bump_stop_threshold      = 0.9f;
            front_roll_center_height = 0.03f;
            rear_roll_center_height  = 0.08f;

            // aerodynamics - swan-neck gooseneck rear wing, front splitter and diffuser
            // claimed 385 kg downforce at 200 km/h in performance setting
            rolling_resistance       = 0.012f;
            drag_coeff               = 0.39f;
            frontal_area             = 2.00f;
            lift_coeff_front         = -0.35f;
            lift_coeff_rear          = -0.65f;
            drs_enabled              = false;
            drs_rear_cl_factor       = 0.3f;
            side_area                = 3.8f;
            ground_effect_enabled    = true;
            ground_effect_multiplier = 1.2f;
            ground_effect_height_ref = 0.12f;
            ground_effect_height_max = 0.28f;
            yaw_aero_enabled         = true;
            yaw_drag_multiplier      = 2.5f;
            yaw_side_force_coeff     = 1.2f;
            pitch_aero_enabled       = true;
            pitch_sensitivity        = 0.5f;
            aero_center_height       = 0.28f;
            aero_center_front_z      = 0.0f;
            aero_center_rear_z       = 0.0f;

            // center of mass - flat-6 hung behind the rear axle (classic 911 layout)
            // z derived from 39/61 distribution over a 2.457 m wheelbase
            center_of_mass_x = 0.0f;
            center_of_mass_y = -0.10f;
            center_of_mass_z = -0.270f;

            // steering
            max_steer_angle            = 0.65f;
            high_speed_steer_reduction = 0.4f;
            steering_rate              = 1.5f;
            self_align_gain            = 0.5f;
            steering_linearity         = 1.3f;

            // alignment
            front_camber     = -2.0f * (3.14159265f / 180.0f);
            rear_camber      = -1.5f * (3.14159265f / 180.0f);
            front_toe        =  0.0f * (3.14159265f / 180.0f);
            rear_toe         =  0.3f * (3.14159265f / 180.0f);
            front_bump_steer = -0.02f;
            rear_bump_steer  =  0.01f;

            front_camber_gain = -1.0f;
            rear_camber_gain  = -0.5f;
            tire_vertical_stiffness = 200000.0f;
            lsd_viscous = 60.0f;
            abs_load_sensitivity = 0.2f;
            steer_compliance = 0.03f;

            // wheels
            airborne_wheel_decay     = 0.99f;
            bearing_friction         = 0.2f;
            ground_match_rate        = 8.0f;
            // electronic parking brake, no traditional handbrake lever in road car
            handbrake_sliding_factor = 0.75f;
            handbrake_torque         = 2500.0f;

            // drivetrain layout - rear wheel drive
            drivetrain_type      = 0;
            torque_split_front   = 0.0f;

            // differential - psm-controlled mechanical lsd (variable lock 30/37%)
            lsd_preload          = 120.0f;
            lsd_lock_ratio_accel = 0.4f;
            lsd_lock_ratio_decel = 0.25f;
            diff_type            = 2;
            driveshaft_stiffness = 7000.0f;

            // input behavior
            input_deadzone          = 0.01f;
            steering_deadzone       = 0.001f;
            braking_speed_threshold = 3.0f;

            // damping
            linear_damping  = 0.001f;
            angular_damping = 0.05f;
            yaw_damping     = 4000.0f;

            // abs
            abs_enabled         = false;
            abs_slip_threshold  = 0.15f;
            abs_release_rate    = 0.7f;
            abs_pulse_frequency = 15.0f;

            // traction control
            tc_enabled         = false;
            tc_slip_threshold  = 0.08f;
            tc_power_reduction = 0.8f;
            tc_response_rate   = 15.0f;

            // turbo - not applicable (naturally aspirated)
            turbo_enabled       = false;
            boost_max_pressure  = 0.0f;
            boost_spool_rate    = 0.0f;
            boost_wastegate_rpm = 0.0f;
            boost_torque_mult   = 0.0f;
            boost_min_rpm       = 0.0f;

            engine_stage_max     = 3;
            suspension_stage_max = 2;
            tires_stage_max      = 2;
            brakes_stage_max     = 2;
            aero_stage_max       = 1;
            weight_stage_max     = 2;

        }
    };

    // mitsubishi lancer evolution ix gsr (2005-2007) - 4g63t 2.0l twin-scroll turbo i4,
    // 286 hp / 392 nm, 5-speed manual, front transverse engine, awd (acd + super-ayc)
    struct evo_ix_preset : car_preset
    {
        evo_ix_preset()
        {
            name = "Mitsubishi Lancer Evolution IX";

            // chassis - 57/43 f/r weight distribution typical of the gsr
            mass              = 1420.0f;
            length            = 4.490f;
            width             = 1.770f;
            height            = 1.450f;
            wheelbase         = 2.625f;
            track_front       = 1.515f;
            track_rear        = 1.515f;
            suspension_height = 0.36f;
            suspension_travel = 0.15f;

            // engine - 4g63t mivec 2.0l turbo i4, peak torque at the bottom of the rev range
            engine_idle_rpm         = 800.0f;
            engine_redline_rpm      = 7000.0f;
            engine_max_rpm          = 7200.0f;
            engine_peak_torque      = 392.0f;
            engine_peak_torque_rpm  = 3000.0f;
            engine_inertia          = 0.30f;
            engine_friction         = 0.025f;
            engine_rpm_smoothing    = 5.0f;
            downshift_blip_amount   = 0.35f;
            downshift_blip_duration = 0.15f;

            // gearbox - 5-speed h-pattern manual (gsr trim)
            gear_ratios[0]          = -3.416f; // reverse
            gear_ratios[1]          =  0.0f;   // neutral
            gear_ratios[2]          =  2.928f; // 1st
            gear_ratios[3]          =  1.950f; // 2nd
            gear_ratios[4]          =  1.407f; // 3rd
            gear_ratios[5]          =  1.031f; // 4th
            gear_ratios[6]          =  0.720f; // 5th
            gear_count              = 7;       // r + n + 5 forward
            final_drive             = 4.529f;
            shift_up_rpm            = 6500.0f;
            shift_down_rpm          = 3000.0f;
            shift_time              = 0.25f;
            clutch_engagement_rate  = 12.0f;
            drivetrain_efficiency   = 0.82f;
            // real evo ix is a true manual, the hud can still force auto if the user prefers
            manual_transmission     = true;

            // shift speed thresholds calibrated for the 5-speed gsr ratios + 4.529 final
            float up_base[]  = { 0, 0, 40, 70, 105, 145, 0, 0, 0, 0, 0 };
            float up_sport[] = { 0, 0, 55, 95, 140, 195, 0, 0, 0, 0, 0 };
            float down[]     = { 0, 0, 0, 20, 45, 75, 110, 0, 0, 0, 0 };
            for (int i = 0; i < max_gears; i++)
            {
                upshift_speed_base[i]  = up_base[i];
                upshift_speed_sport[i] = up_sport[i];
                downshift_speeds[i]    = down[i];
            }

            // brakes - brembo 4-pot front, 2-pot rear, ventilated rotors
            brake_force            = 7500.0f;
            brake_bias_front       = 0.60f;
            reverse_power_ratio    = 0.5f;
            brake_ambient_temp     = 30.0f;
            brake_optimal_temp     = 350.0f;
            brake_fade_temp        = 650.0f;
            brake_max_temp         = 850.0f;
            brake_heat_coefficient = 0.015f;
            brake_cooling_base     = 8.0f;
            brake_cooling_airflow  = 1.2f;
            brake_thermal_mass     = 5.5f;

            // input
            throttle_smoothing = 8.0f;
            brake_smoothing    = 8.0f;

            // pacejka - yokohama advan a048 (235/45r17 oem)
            lat_B  = 10.5f;
            lat_C  = 1.3f;
            lat_D  = 1.0f;
            lat_E  = -0.6f;
            long_B = 16.0f;
            long_C = 1.4f;
            long_D = 1.0f;
            long_E = -0.3f;

            load_B_scale_min     = 0.5f;
            pneumatic_trail_max  = 0.04f;
            pneumatic_trail_peak = 0.14f;

            // tire grip - 235/45r17 yokohama advan oem
            tire_friction       = 1.3f;
            min_slip_speed      = 0.5f;
            load_sensitivity    = 0.90f;
            load_reference      = 4500.0f;
            rear_grip_ratio     = 1.05f;
            slip_angle_deadband = 0.01f;
            min_lateral_grip    = 0.35f;
            camber_thrust_coeff = 0.012f;
            max_slip_angle      = 0.38f;
            tire_pressure         = 2.4f;
            tire_pressure_optimal = 2.4f;

            // tire thermals
            tire_ambient_temp      = 45.0f;
            tire_optimal_temp      = 85.0f;
            tire_temp_range        = 45.0f;
            tire_heat_from_slip    = 22.0f;
            tire_heat_from_rolling = 0.15f;
            tire_cooling_rate      = 2.0f;
            tire_cooling_airflow   = 0.04f;
            tire_grip_temp_factor  = 0.15f;
            tire_min_temp          = 10.0f;
            tire_max_temp          = 300.0f;
            tire_relaxation_length = 0.35f;
            tire_wear_rate         = 0.000012f;
            tire_wear_heat_mult    = 2.0f;
            tire_grip_wear_loss    = 0.3f;
            tire_core_transfer_rate = 0.5f;
            tire_surface_response   = 3.0f;

            // suspension - macpherson front, multi-link rear, rally-stiff springs
            front_spring_freq     = 2.4f;
            rear_spring_freq      = 2.2f;
            front_damping_ratio   = 0.63f;
            rear_damping_ratio    = 0.67f;
            damping_bump_ratio    = 0.7f;
            damping_rebound_ratio = 1.3f;
            front_arb_stiffness      = 15000.0f;
            rear_arb_stiffness       = 12500.0f;
            max_susp_force           = 30000.0f;
            max_damper_velocity      = 5.0f;
            bump_stop_stiffness      = 80000.0f;
            bump_stop_threshold      = 0.88f;
            front_roll_center_height = 0.05f;
            rear_roll_center_height  = 0.07f;

            // aerodynamics - stock body with factory rear spoiler
            rolling_resistance       = 0.014f;
            drag_coeff               = 0.34f;
            frontal_area             = 2.15f;
            lift_coeff_front         = -0.05f;
            lift_coeff_rear          = -0.12f;
            drs_enabled              = false;
            drs_rear_cl_factor       = 0.3f;
            side_area                = 3.5f;
            ground_effect_enabled    = false;
            ground_effect_multiplier = 1.0f;
            ground_effect_height_ref = 0.12f;
            ground_effect_height_max = 0.30f;
            yaw_aero_enabled         = true;
            yaw_drag_multiplier      = 2.0f;
            yaw_side_force_coeff     = 1.0f;
            pitch_aero_enabled       = true;
            pitch_sensitivity        = 0.4f;
            aero_center_height       = 0.30f;
            aero_center_front_z      = 0.0f;
            aero_center_rear_z       = 0.0f;

            // center of mass - front transverse 4g63t with awd transfer case
            // z derived from 57/43 distribution over a 2.625 m wheelbase
            center_of_mass_x = 0.0f;
            center_of_mass_y = -0.08f;
            center_of_mass_z = 0.184f;

            // steering
            max_steer_angle            = 0.62f;
            high_speed_steer_reduction = 0.45f;
            steering_rate              = 1.6f;
            self_align_gain            = 0.45f;
            steering_linearity         = 1.2f;

            // alignment
            front_camber     = -1.5f * (3.14159265f / 180.0f);
            rear_camber      = -1.0f * (3.14159265f / 180.0f);
            front_toe        =  0.0f * (3.14159265f / 180.0f);
            rear_toe         =  0.2f * (3.14159265f / 180.0f);
            front_bump_steer = -0.015f;
            rear_bump_steer  =  0.01f;

            front_camber_gain = -0.9f;
            rear_camber_gain  = -0.3f;
            tire_vertical_stiffness = 160000.0f;
            lsd_viscous = 120.0f;
            abs_load_sensitivity = 0.25f;
            steer_compliance = 0.05f;

            // wheels
            airborne_wheel_decay     = 0.99f;
            bearing_friction         = 0.2f;
            ground_match_rate        = 8.0f;
            // proper hydraulic handbrake, strongest of the three (rally heritage)
            handbrake_sliding_factor = 0.80f;
            handbrake_torque         = 6000.0f;

            // drivetrain layout - awd via acd (active center diff), nominally 50/50
            drivetrain_type      = 2;
            torque_split_front   = 0.50f;

            // differentials - helical lsd front, super-ayc planetary rear
            lsd_preload          = 100.0f;
            lsd_lock_ratio_accel = 0.45f;
            lsd_lock_ratio_decel = 0.20f;
            diff_type            = 2;
            driveshaft_stiffness = 5000.0f;

            // input behavior
            input_deadzone          = 0.01f;
            steering_deadzone       = 0.001f;
            braking_speed_threshold = 3.0f;

            // damping
            linear_damping  = 0.001f;
            angular_damping = 0.08f;
            yaw_damping     = 3000.0f;

            // abs
            abs_enabled         = false;
            abs_slip_threshold  = 0.15f;
            abs_release_rate    = 0.7f;
            abs_pulse_frequency = 12.0f;

            // traction control
            tc_enabled         = false;
            tc_slip_threshold  = 0.10f;
            tc_power_reduction = 0.7f;
            tc_response_rate   = 12.0f;

            // turbo - td05hra-16g6c twin-scroll, ~18-20 psi stock (~1.35 bar)
            turbo_enabled       = true;
            boost_max_pressure  = 1.35f;
            boost_spool_rate    = 3.5f;
            boost_wastegate_rpm = 6000.0f;
            boost_torque_mult   = 0.25f;
            boost_min_rpm       = 2800.0f;

            engine_stage_max     = 3;
            suspension_stage_max = 2;
            tires_stage_max      = 2;
            brakes_stage_max     = 2;
            aero_stage_max       = 1;
            weight_stage_max     = 2;

        }
    };

    //= preset registry ============================================================

    struct preset_entry
    {
        const char*       name;
        car_preset*       instance;
    };

    inline laferrari_preset _preset_laferrari;
    inline gt3_992_preset   _preset_gt3_992;
    inline evo_ix_preset    _preset_evo_ix;

    inline std::vector<car_preset> external_presets;
    inline std::vector<std::string> external_preset_names;

    inline std::vector<preset_entry> preset_registry =
    {
        { "Ferrari LaFerrari",            &_preset_laferrari },
        { "Porsche 911 GT3 (992)",        &_preset_gt3_992   },
        { "Mitsubishi Lancer Evolution IX", &_preset_evo_ix  },
    };

    inline int preset_count        = static_cast<int>(preset_registry.size());
    inline int active_preset_index = 0;

    bool load_presets_from_xml(const char* file_path);
}
