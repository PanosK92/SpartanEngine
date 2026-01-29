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
#include "../../Logging/Log.h"
#include "../../Core/Engine.h"
#include "../../../editor/ImGui/Source/imgui.h"

namespace car
{
    using namespace physx;

    namespace tuning
    {
        // engine
        constexpr float engine_idle_rpm         = 1000.0f;
        constexpr float engine_redline_rpm      = 9250.0f;
        constexpr float engine_max_rpm          = 9500.0f;
        constexpr float engine_peak_torque      = 900.0f;
        constexpr float engine_peak_torque_rpm  = 6750.0f;
        constexpr float engine_inertia          = 0.25f;
        constexpr float engine_friction         = 0.02f;
        constexpr float engine_rpm_smoothing    = 6.0f;
        constexpr float downshift_blip_amount   = 0.35f;
        constexpr float downshift_blip_duration = 0.15f;

        // gearbox (reverse, neutral, 1st-7th)
        constexpr float gear_ratios[]          = { -3.15f, 0.0f, 3.08f, 2.19f, 1.63f, 1.29f, 1.03f, 0.84f, 0.69f };
        constexpr int   gear_count             = 9;
        constexpr float final_drive            = 4.44f;
        constexpr float shift_up_rpm           = 8500.0f;
        constexpr float shift_down_rpm         = 3500.0f;
        constexpr float shift_time             = 0.05f;
        constexpr float clutch_engagement_rate = 8.0f;
        constexpr float drivetrain_efficiency  = 0.88f;
        inline bool     manual_transmission    = false;
        
        // brakes
        constexpr float brake_force            = 12000.0f;
        constexpr float brake_bias_front       = 0.65f;
        constexpr float reverse_power_ratio    = 0.5f;
        constexpr float brake_ambient_temp     = 200.0f;
        constexpr float brake_optimal_temp     = 400.0f;
        constexpr float brake_fade_temp        = 700.0f;
        constexpr float brake_max_temp         = 900.0f;
        constexpr float brake_heat_coefficient = 0.015f;
        constexpr float brake_cooling_base     = 8.0f;
        constexpr float brake_cooling_airflow  = 1.5f;
        constexpr float brake_thermal_mass     = 5.0f;
        
        // input
        constexpr float throttle_smoothing = 10.0f;

        // pacejka
        constexpr float lat_B  = 12.0f;
        constexpr float lat_C  = 1.4f;
        constexpr float lat_D  = 1.0f;
        constexpr float lat_E  = 0.6f;
        constexpr float long_B = 20.0f;
        constexpr float long_C = 1.5f;
        constexpr float long_D = 1.0f;
        constexpr float long_E = -0.5f;
        
        // tire grip parameters
        constexpr float tire_friction       = 1.8f;
        constexpr float min_slip_speed      = 0.5f;
        constexpr float load_sensitivity    = 0.92f;
        constexpr float load_reference      = 4000.0f;
        constexpr float rear_grip_ratio     = 1.10f;
        constexpr float slip_angle_deadband = 0.01f;
        constexpr float min_lateral_grip    = 0.4f;
        constexpr float camber_thrust_coeff = 0.015f;

        // tire thermals
        constexpr float tire_ambient_temp      = 50.0f;
        constexpr float tire_optimal_temp      = 90.0f;
        constexpr float tire_temp_range        = 50.0f;
        constexpr float tire_heat_from_slip    = 25.0f;
        constexpr float tire_heat_from_rolling = 0.15f;
        constexpr float tire_cooling_rate      = 2.0f;
        constexpr float tire_cooling_airflow   = 0.05f;
        constexpr float tire_grip_temp_factor  = 0.15f;
        constexpr float tire_min_temp          = 10.0f;
        constexpr float tire_max_temp          = 150.0f;
        constexpr float tire_relaxation_length = 0.3f;
        
        // suspension
        constexpr float front_spring_freq     = 1.5f;
        constexpr float rear_spring_freq      = 1.4f;
        constexpr float damping_ratio         = 0.85f;
        constexpr float damping_bump_ratio    = 0.7f;
        constexpr float damping_rebound_ratio = 1.3f;
        constexpr float front_arb_stiffness   = 3500.0f;
        constexpr float rear_arb_stiffness    = 1500.0f;
        constexpr float max_susp_force        = 35000.0f;
        constexpr float max_damper_velocity   = 5.0f;
        
        // aerodynamics
        constexpr float air_density        = 1.225f;
        constexpr float rolling_resistance = 0.015f;
        
        inline float drag_coeff       = 0.35f;
        inline float frontal_area     = 2.2f;
        inline float lift_coeff_front = -0.3f;
        inline float lift_coeff_rear  = -0.4f;
        inline float side_area        = 4.0f;

        // ground effect
        inline bool  ground_effect_enabled    = true;
        inline float ground_effect_multiplier = 1.5f;
        inline float ground_effect_height_ref = 0.15f;
        inline float ground_effect_height_max = 0.30f;
        
        // yaw aero
        inline bool  yaw_aero_enabled     = true;
        inline float yaw_drag_multiplier  = 2.5f;
        inline float yaw_side_force_coeff = 1.2f;
        
        // pitch aero
        inline bool  pitch_aero_enabled = true;
        inline float pitch_sensitivity  = 0.5f;

        // aero center (computed from mesh)
        inline float aero_center_height = 0.3f;
        inline float aero_center_front_z = 0.0f;
        inline float aero_center_rear_z = 0.0f;

        // center of mass
        inline float center_of_mass_x = 0.0f;
        inline float center_of_mass_y = -0.15f;
        inline float center_of_mass_z = -0.3f;
        
        // steering
        constexpr float max_steer_angle            = 0.65f;
        constexpr float high_speed_steer_reduction = 0.4f;
        constexpr float steering_rate              = 1.5f;
        constexpr float pneumatic_trail            = 0.03f;
        constexpr float self_align_gain            = 0.5f;
        
        // alignment
        constexpr float front_camber = -1.5f * (3.14159265f / 180.0f);
        constexpr float rear_camber  = -1.0f * (3.14159265f / 180.0f);
        constexpr float front_toe    =  0.1f * (3.14159265f / 180.0f);
        constexpr float rear_toe     =  0.2f * (3.14159265f / 180.0f);

        // bump steer
        constexpr float front_bump_steer = -0.02f;
        constexpr float rear_bump_steer  =  0.01f;

        constexpr float steering_linearity = 1.3f;

        // wheels
        constexpr float airborne_wheel_decay     = 0.99f;
        constexpr float bearing_friction         = 0.2f;
        constexpr float ground_match_rate        = 8.0f;
        constexpr float handbrake_sliding_factor = 0.75f;
        
        // differential
        constexpr float lsd_preload          = 150.0f;
        constexpr float lsd_lock_ratio_accel = 0.5f;
        constexpr float lsd_lock_ratio_decel = 0.3f;
        
        // input
        constexpr float input_deadzone          = 0.01f;
        constexpr float steering_deadzone       = 0.001f;
        constexpr float braking_speed_threshold = 3.0f;
        
        // speed limits
        constexpr float max_forward_speed   = 320.0f;
        constexpr float max_reverse_speed   = 80.0f;
        constexpr float max_power_reduction = 0.85f;
        
        // damping
        constexpr float linear_damping  = 0.001f;
        constexpr float angular_damping = 0.50f;
        
        // abs
        inline bool     abs_enabled         = false;
        constexpr float abs_slip_threshold  = 0.15f;
        constexpr float abs_release_rate    = 0.7f;
        constexpr float abs_pulse_frequency = 15.0f;
        
        // traction control
        inline bool     tc_enabled         = false;
        constexpr float tc_slip_threshold  = 0.08f;
        constexpr float tc_power_reduction = 0.8f;
        constexpr float tc_response_rate   = 15.0f;
        
        // turbo
        inline bool     turbo_enabled       = false;
        constexpr float boost_max_pressure  = 1.2f;
        constexpr float boost_spool_rate    = 3.0f;
        constexpr float boost_wastegate_rpm = 7500.0f;
        constexpr float boost_torque_mult   = 0.35f;
        constexpr float boost_min_rpm       = 2500.0f;
        
        // surfaces
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
    inline static aero_debug_data aero_debug;

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
    inline static shape_2d shape_data;

    enum wheel_id { front_left = 0, front_right = 1, rear_left = 2, rear_right = 3, wheel_count = 4 };
    enum surface_type { surface_asphalt = 0, surface_concrete, surface_wet_asphalt, surface_gravel, surface_grass, surface_ice, surface_count };
    
    struct config
    {
        float length            = 4.5f;
        float width             = 2.0f;
        float height            = 0.5f;
        float mass              = 1500.0f;
        float wheel_radius      = 0.35f;
        float wheel_width       = 0.25f;
        float wheel_mass        = 20.0f;
        float suspension_travel = 0.20f;
        float suspension_height = 0.35f;
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
        float        angular_velocity     = 0.0f;
        float        rotation             = 0.0f;
        float        tire_load            = 0.0f;
        float        slip_angle           = 0.0f;
        float        slip_ratio           = 0.0f;
        float        lateral_force        = 0.0f;
        float        longitudinal_force   = 0.0f;
        float        temperature          = tuning::tire_ambient_temp;
        float        brake_temp           = tuning::brake_ambient_temp;
        surface_type contact_surface      = surface_asphalt;
    };
    
    struct input_state
    {
        float throttle  = 0.0f;
        float brake     = 0.0f;
        float steering  = 0.0f;
        float handbrake = 0.0f;
    };

    inline static PxRigidDynamic* body     = nullptr;
    inline static PxMaterial*     material = nullptr;
    inline static config          cfg;
    inline static wheel           wheels[wheel_count];
    inline static input_state     input;
    inline static input_state     input_target;
    inline static PxVec3          wheel_offsets[wheel_count];
    inline static float           wheel_moi[wheel_count];
    inline static float           spring_stiffness[wheel_count];
    inline static float           spring_damping[wheel_count];
    inline static float           abs_phase = 0.0f;
    inline static bool            abs_active[wheel_count] = {};
    inline static float           tc_reduction = 0.0f;
    inline static bool            tc_active = false;
    inline static float           engine_rpm = tuning::engine_idle_rpm;
    inline static int             current_gear = 2;
    inline static float           shift_timer = 0.0f;
    inline static bool            is_shifting = false;
    inline static float           clutch = 1.0f;
    inline static float           shift_cooldown = 0.0f;
    inline static int             last_shift_direction = 0;
    inline static float           boost_pressure = 0.0f;
    inline static float           downshift_blip_timer = 0.0f;
    inline static PxVec3          prev_velocity = PxVec3(0);
    
    struct debug_ray
    {
        PxVec3 origin;
        PxVec3 hit_point;
        bool   hit;
    };
    constexpr int debug_rays_per_wheel = 7;
    inline static debug_ray debug_rays[wheel_count][debug_rays_per_wheel];
    inline static PxVec3    debug_suspension_top[wheel_count];
    inline static PxVec3    debug_suspension_bottom[wheel_count];

    inline bool  is_front(int i)                { return i == front_left || i == front_right; }
    inline bool  is_rear(int i)                 { return i == rear_left || i == rear_right; }
    inline float lerp(float a, float b, float t){ return a + (b - a) * t; }
    inline float exp_decay(float rate, float dt){ return 1.0f - expf(-rate * dt); }
    
    inline float pacejka(float slip, float B, float C, float D, float E)
    {
        float Bx = B * slip;
        return D * sinf(C * atanf(Bx - E * (Bx - atanf(Bx))));
    }
    
    inline float load_sensitive_grip(float load)
    {
        if (load <= 0.0f) return 0.0f;
        return load * powf(load / tuning::load_reference, tuning::load_sensitivity - 1.0f);
    }
    
    inline float get_tire_temp_grip_factor(float temperature)
    {
        float penalty = PxClamp(fabsf(temperature - tuning::tire_optimal_temp) / tuning::tire_temp_range, 0.0f, 1.0f);
        return 1.0f - penalty * tuning::tire_grip_temp_factor;
    }
    
    inline float get_camber_grip_factor(int wheel_index, float slip_angle)
    {
        float camber = is_front(wheel_index) ? tuning::front_camber : tuning::rear_camber;
        float effective_camber = camber - slip_angle * 0.3f;
        return 1.0f - fabsf(effective_camber) * 0.1f;
    }
    
    inline float get_surface_friction(surface_type surface)
    {
        static constexpr float friction[] = {
            tuning::surface_friction_asphalt,
            tuning::surface_friction_concrete,
            tuning::surface_friction_wet_asphalt,
            tuning::surface_friction_gravel,
            tuning::surface_friction_grass,
            tuning::surface_friction_ice
        };
        return (surface >= 0 && surface < surface_count) ? friction[surface] : 1.0f;
    }
    
    inline float get_brake_efficiency(float temp)
    {
        if (temp >= tuning::brake_fade_temp)
            return 0.6f;
        
        if (temp < tuning::brake_optimal_temp)
        {
            float t = PxClamp((temp - tuning::brake_ambient_temp) / (tuning::brake_optimal_temp - tuning::brake_ambient_temp), 0.0f, 1.0f);
            return 0.85f + 0.15f * t;
        }
        
        float t = (temp - tuning::brake_optimal_temp) / (tuning::brake_fade_temp - tuning::brake_optimal_temp);
        return 1.0f - 0.4f * t;
    }
    
    inline void update_boost(float throttle, float rpm, float dt)
    {
        if (!tuning::turbo_enabled)
        {
            boost_pressure = lerp(boost_pressure, 0.0f, exp_decay(tuning::boost_spool_rate * 3.0f, dt));
            return;
        }
        
        float target = 0.0f;
        if (throttle > 0.3f && rpm > tuning::boost_min_rpm)
        {
            target = tuning::boost_max_pressure * PxMin((rpm - tuning::boost_min_rpm) / 4000.0f, 1.0f);
            
            if (rpm > tuning::boost_wastegate_rpm)
                target *= PxMax(0.0f, 1.0f - (rpm - tuning::boost_wastegate_rpm) / 2000.0f);
        }
        
        float rate = (target > boost_pressure) ? tuning::boost_spool_rate : tuning::boost_spool_rate * 2.0f;
        boost_pressure = lerp(boost_pressure, target, exp_decay(rate, dt));
    }
    
    inline float get_engine_torque(float rpm)
    {
        rpm = PxClamp(rpm, tuning::engine_idle_rpm, tuning::engine_max_rpm);
        
        float factor;
        if (rpm < 2500.0f)
            factor = 0.55f + ((rpm - tuning::engine_idle_rpm) / 1500.0f) * 0.15f;
        else if (rpm < 4500.0f)
            factor = 0.70f + ((rpm - 2500.0f) / 2000.0f) * 0.15f;
        else if (rpm < tuning::engine_peak_torque_rpm)
            factor = 0.85f + ((rpm - 4500.0f) / (tuning::engine_peak_torque_rpm - 4500.0f)) * 0.15f;
        else if (rpm < 8000.0f)
            factor = 1.0f - ((rpm - tuning::engine_peak_torque_rpm) / (8000.0f - tuning::engine_peak_torque_rpm)) * 0.08f;
        else if (rpm < tuning::engine_redline_rpm)
            factor = 0.92f - ((rpm - 8000.0f) / (tuning::engine_redline_rpm - 8000.0f)) * 0.10f;
        else
            factor = 0.82f * (1.0f - ((rpm - tuning::engine_redline_rpm) / (tuning::engine_max_rpm - tuning::engine_redline_rpm)) * 0.8f);
        
        return tuning::engine_peak_torque * factor;
    }
    
    inline float wheel_rpm_to_engine_rpm(float wheel_rpm, int gear)
    {
        if (gear < 0 || gear >= tuning::gear_count || gear == 1)
            return tuning::engine_idle_rpm;
        return fabsf(wheel_rpm * tuning::gear_ratios[gear] * tuning::final_drive);
    }
    
    inline float get_upshift_speed(int from_gear, float throttle)
    {
        constexpr float base[]  = { 0.0f, 0.0f, 40.0f, 65.0f, 90.0f, 120.0f, 155.0f, 200.0f };
        constexpr float sport[] = { 0.0f, 0.0f, 60.0f, 95.0f, 130.0f, 175.0f, 225.0f, 290.0f };
        
        if (from_gear < 2 || from_gear > 7) return 999.0f;
        float t = PxClamp((throttle - 0.3f) / 0.5f, 0.0f, 1.0f);
        return base[from_gear] + t * (sport[from_gear] - base[from_gear]);
    }
    
    inline float get_downshift_speed(int gear)
    {
        constexpr float speeds[] = { 0.0f, 0.0f, 0.0f, 20.0f, 35.0f, 50.0f, 70.0f, 95.0f, 125.0f };
        return (gear >= 2 && gear <= 8) ? speeds[gear] : 0.0f;
    }
    
    inline void update_automatic_gearbox(float dt, float throttle, float forward_speed)
    {
        if (shift_cooldown > 0.0f)
            shift_cooldown -= dt;
        
        if (is_shifting)
        {
            shift_timer -= dt;
            if (shift_timer <= 0.0f)
            {
                is_shifting = false;
                shift_timer = 0.0f;
                shift_cooldown = 0.5f;
            }
            return;
        }
        
        if (tuning::manual_transmission)
            return;
        
        float speed_kmh = forward_speed * 3.6f;
        
        // reverse
        if (forward_speed < -1.0f && input.brake > 0.1f && throttle < 0.1f && current_gear != 0)
        {
            current_gear = 0;
            is_shifting = true;
            shift_timer = tuning::shift_time * 2.0f;
            last_shift_direction = -1;
            return;
        }

        // neutral to first
        if (current_gear == 1 && throttle > 0.1f && forward_speed >= -0.5f)
        {
            current_gear = 2;
            is_shifting = true;
            shift_timer = tuning::shift_time;
            last_shift_direction = 1;
            return;
        }

        // reverse to first
        if (current_gear == 0)
        {
            if ((throttle > 0.1f && forward_speed > -2.0f) || forward_speed > 0.5f)
            {
                current_gear = 2;
                is_shifting = true;
                shift_timer = tuning::shift_time * 2.0f;
                last_shift_direction = 1;
                return;
            }
        }

        // forward gears
        if (current_gear >= 2)
        {
            bool can_shift = shift_cooldown <= 0.0f;
            
            float upshift_threshold = get_upshift_speed(current_gear, throttle);
            if (last_shift_direction == -1)
                upshift_threshold += 10.0f;
            
            bool speed_trigger = speed_kmh > upshift_threshold;
            bool rpm_trigger   = engine_rpm > tuning::shift_up_rpm;
            
            if (can_shift && (speed_trigger || rpm_trigger) && current_gear < 8 && throttle > 0.1f)
            {
                current_gear++;
                is_shifting = true;
                shift_timer = tuning::shift_time;
                last_shift_direction = 1;
                return;
            }
            
            float downshift_threshold = get_downshift_speed(current_gear);
            if (last_shift_direction == 1)
                downshift_threshold -= 10.0f;

            if (can_shift && speed_kmh < downshift_threshold && current_gear > 2)
            {
                current_gear--;
                is_shifting = true;
                shift_timer = tuning::shift_time;
                last_shift_direction = -1;
                downshift_blip_timer = tuning::downshift_blip_duration;
                return;
            }

            // kickdown
            if (throttle > 0.9f && current_gear > 2)
            {
                int target = current_gear;
                for (int g = current_gear - 1; g >= 2; g--)
                {
                    float ratio = fabsf(tuning::gear_ratios[g]) * tuning::final_drive;
                    float potential_rpm = (forward_speed / cfg.wheel_radius) * (60.0f / (2.0f * PxPi)) * ratio;
                    if (potential_rpm < tuning::engine_redline_rpm * 0.85f)
                        target = g;
                    else
                        break;
                }

                if (target < current_gear)
                {
                    current_gear = target;
                    is_shifting = true;
                    shift_timer = tuning::shift_time;
                    last_shift_direction = -1;
                    downshift_blip_timer = tuning::downshift_blip_duration;
                }
            }
        }
    }
    
    inline const char* get_gear_string()
    {
        static const char* names[] = { "R", "N", "1", "2", "3", "4", "5", "6", "7" };
        return (current_gear >= 0 && current_gear < tuning::gear_count) ? names[current_gear] : "?";
    }

    inline void compute_constants()
    {
        float front_z = cfg.length * 0.35f;
        float rear_z  = -cfg.length * 0.35f;
        float half_w  = cfg.width * 0.5f - cfg.wheel_width * 0.5f;
        float y       = -cfg.suspension_height;
        
        wheel_offsets[front_left]  = PxVec3(-half_w, y, front_z);
        wheel_offsets[front_right] = PxVec3( half_w, y, front_z);
        wheel_offsets[rear_left]   = PxVec3(-half_w, y, rear_z);
        wheel_offsets[rear_right]  = PxVec3( half_w, y, rear_z);
        
        float axle_mass[2] = { cfg.mass * 0.40f * 0.5f, cfg.mass * 0.60f * 0.5f };
        float freq[2]      = { tuning::front_spring_freq, tuning::rear_spring_freq };
        
        for (int i = 0; i < wheel_count; i++)
        {
            int axle   = is_front(i) ? 0 : 1;
            float mass = axle_mass[axle];
            float omega = 2.0f * PxPi * freq[axle];
            
            wheel_moi[i]        = 0.7f * cfg.wheel_mass * cfg.wheel_radius * cfg.wheel_radius;
            spring_stiffness[i] = mass * omega * omega;
            spring_damping[i]   = 2.0f * tuning::damping_ratio * sqrtf(spring_stiffness[i] * mass);
        }
    }
    
    inline void destroy()
    {
        if (body)    { body->release();     body = nullptr; }
        if (material){ material->release(); material = nullptr; }
    }

    inline void compute_aero_from_shape(const std::vector<PxVec3>& vertices)
    {
        if (vertices.size() < 4)
            return;

        PxVec3 min_pt(FLT_MAX, FLT_MAX, FLT_MAX);
        PxVec3 max_pt(-FLT_MAX, -FLT_MAX, -FLT_MAX);

        for (const PxVec3& v : vertices)
        {
            min_pt.x = PxMin(min_pt.x, v.x);
            min_pt.y = PxMin(min_pt.y, v.y);
            min_pt.z = PxMin(min_pt.z, v.z);
            max_pt.x = PxMax(max_pt.x, v.x);
            max_pt.y = PxMax(max_pt.y, v.y);
            max_pt.z = PxMax(max_pt.z, v.z);
        }

        float width  = max_pt.x - min_pt.x;
        float height = max_pt.y - min_pt.y;
        float length = max_pt.z - min_pt.z;

        float frontal_fill_factor = 0.82f;
        float computed_frontal_area = width * height * frontal_fill_factor;

        float side_fill_factor = 0.75f;
        float computed_side_area = length * height * side_fill_factor;

        float length_height_ratio = length / PxMax(height, 0.1f);
        float base_cd = 0.32f;
        float ratio_factor = PxClamp(2.5f / length_height_ratio, 0.8f, 1.3f);
        float computed_drag_coeff = base_cd * ratio_factor;

        if (computed_frontal_area > 0.5f && computed_frontal_area < 10.0f)
        {
            tuning::frontal_area = computed_frontal_area;
            SP_LOG_INFO("aero: frontal area = %.2f m²", computed_frontal_area);
        }

        if (computed_side_area > 1.0f && computed_side_area < 20.0f)
        {
            tuning::side_area = computed_side_area;
            SP_LOG_INFO("aero: side area = %.2f m²", computed_side_area);
        }

        if (computed_drag_coeff > 0.2f && computed_drag_coeff < 0.6f)
        {
            tuning::drag_coeff = computed_drag_coeff;
            SP_LOG_INFO("aero: drag coefficient = %.3f", computed_drag_coeff);
        }

        float centroid_y = 0.0f;
        float centroid_z = 0.0f;
        float front_area = 0.0f;
        float rear_area = 0.0f;
        float mid_z = (min_pt.z + max_pt.z) * 0.5f;

        for (const PxVec3& v : vertices)
        {
            float h = v.y - min_pt.y;
            float weight = h * h;
            centroid_y += v.y * weight;
            centroid_z += v.z * weight;

            if (v.z > mid_z)
                front_area += weight;
            else
                rear_area += weight;
        }

        float total_weight = 0.0f;
        for (const PxVec3& v : vertices)
        {
            float h = v.y - min_pt.y;
            total_weight += h * h;
        }

        if (total_weight > 0.0f)
        {
            centroid_y /= total_weight;
            centroid_z /= total_weight;
        }

        tuning::aero_center_height = centroid_y;

        float total_area = front_area + rear_area;
        float front_bias = (total_area > 0.0f) ? front_area / total_area : 0.5f;

        tuning::aero_center_front_z = max_pt.z * 0.8f;
        tuning::aero_center_rear_z = min_pt.z * 0.8f;

        float base_lift = (tuning::lift_coeff_front + tuning::lift_coeff_rear) * 0.5f;
        tuning::lift_coeff_front = base_lift * (0.5f + (front_bias - 0.5f) * 0.5f);
        tuning::lift_coeff_rear = base_lift * (0.5f + (0.5f - front_bias) * 0.5f);

        SP_LOG_INFO("aero: dimensions %.2f x %.2f x %.2f m (L x W x H)", length, width, height);
        SP_LOG_INFO("aero: center height=%.2f, front_z=%.2f, rear_z=%.2f",
            tuning::aero_center_height, tuning::aero_center_front_z, tuning::aero_center_rear_z);
        SP_LOG_INFO("aero: front/rear bias=%.0f%%/%.0f%%, lift F/R=%.2f/%.2f",
            front_bias * 100.0f, (1.0f - front_bias) * 100.0f, tuning::lift_coeff_front, tuning::lift_coeff_rear);

        // compute 2D hull profiles for visualization
        shape_data.min_x = min_pt.x; shape_data.max_x = max_pt.x;
        shape_data.min_y = min_pt.y; shape_data.max_y = max_pt.y;
        shape_data.min_z = min_pt.z; shape_data.max_z = max_pt.z;
        
        // helper: compute 2D convex hull from projected points using gift wrapping
        auto compute_hull_2d = [](std::vector<std::pair<float, float>>& points) -> std::vector<std::pair<float, float>>
        {
            if (points.size() < 3)
                return points;
            
            // find leftmost point
            size_t start = 0;
            for (size_t i = 1; i < points.size(); i++)
                if (points[i].first < points[start].first)
                    start = i;
            
            std::vector<std::pair<float, float>> hull;
            size_t current = start;
            
            do {
                hull.push_back(points[current]);
                size_t next = 0;
                
                for (size_t i = 0; i < points.size(); i++)
                {
                    if (i == current) continue;
                    
                    // cross product to determine left turn
                    float ax = points[next].first - points[current].first;
                    float ay = points[next].second - points[current].second;
                    float bx = points[i].first - points[current].first;
                    float by = points[i].second - points[current].second;
                    float cross = ax * by - ay * bx;
                    
                    if (next == current || cross < 0 || (cross == 0 && bx*bx + by*by > ax*ax + ay*ay))
                        next = i;
                }
                
                current = next;
            } while (current != start && hull.size() < points.size());
            
            return hull;
        };
        
        // project vertices to side view (z, y) and compute hull
        std::vector<std::pair<float, float>> side_points;
        for (const PxVec3& v : vertices)
            side_points.push_back({v.z, v.y});
        shape_data.side_profile = compute_hull_2d(side_points);
        
        // project vertices to front view (x, y) and compute hull
        std::vector<std::pair<float, float>> front_points;
        for (const PxVec3& v : vertices)
            front_points.push_back({v.x, v.y});
        shape_data.front_profile = compute_hull_2d(front_points);
        
        shape_data.valid = !shape_data.side_profile.empty() && !shape_data.front_profile.empty();
        SP_LOG_INFO("aero: shape profiles computed (side: %zu pts, front: %zu pts)", 
            shape_data.side_profile.size(), shape_data.front_profile.size());
    }

    struct setup_params
    {
        PxPhysics*              physics      = nullptr;
        PxScene*                scene        = nullptr;
        PxConvexMesh*           chassis_mesh = nullptr;  // convex hull for collision
        std::vector<PxVec3>     vertices;                // original mesh verts for aero calculation
        config                  car_config;
    };

    inline bool setup(const setup_params& params)
    {
        if (!params.physics || !params.scene)
            return false;

        cfg = params.car_config;
        compute_constants();

        for (int i = 0; i < wheel_count; i++)
        {
            wheels[i] = wheel();
            abs_active[i] = false;
        }
        input = input_state();
        input_target = input_state();
        abs_phase = 0.0f;
        tc_reduction = 0.0f;
        tc_active = false;
        engine_rpm = tuning::engine_idle_rpm;
        current_gear = 2;
        shift_timer = 0.0f;
        is_shifting = false;
        clutch = 1.0f;
        shift_cooldown = 0.0f;
        last_shift_direction = 0;
        boost_pressure = 0.0f;
        downshift_blip_timer = 0.0f;
        prev_velocity = PxVec3(0);

        material = params.physics->createMaterial(0.8f, 0.7f, 0.1f);
        if (!material)
            return false;

        float front_mass_per_wheel = cfg.mass * 0.40f * 0.5f;
        float front_omega = 2.0f * PxPi * tuning::front_spring_freq;
        float front_stiffness = front_mass_per_wheel * front_omega * front_omega;
        float expected_sag = PxClamp((front_mass_per_wheel * 9.81f) / front_stiffness, 0.0f, cfg.suspension_travel * 0.8f);
        float spawn_y = cfg.wheel_radius + cfg.suspension_height + expected_sag;

        body = params.physics->createRigidDynamic(PxTransform(PxVec3(0, spawn_y, 0)));
        if (!body)
        {
            material->release();
            material = nullptr;
            return false;
        }

        // attach chassis shape
        if (params.chassis_mesh)
        {
            PxConvexMeshGeometry geometry(params.chassis_mesh);
            PxShape* shape = params.physics->createShape(geometry, *material);
            if (shape)
            {
                shape->setFlag(PxShapeFlag::eSCENE_QUERY_SHAPE, false);
                shape->setFlag(PxShapeFlag::eVISUALIZATION, true);
                body->attachShape(*shape);
                shape->release();
            }
        }
        else
        {
            PxShape* chassis = params.physics->createShape(
                PxBoxGeometry(cfg.width * 0.5f, cfg.height * 0.5f, cfg.length * 0.5f),
                *material
            );
            if (chassis)
            {
                chassis->setFlag(PxShapeFlag::eSCENE_QUERY_SHAPE, false);
                body->attachShape(*chassis);
                chassis->release();
            }
        }

        PxVec3 com(tuning::center_of_mass_x, tuning::center_of_mass_y, tuning::center_of_mass_z);
        PxRigidBodyExt::setMassAndUpdateInertia(*body, cfg.mass, &com);
        body->setActorFlag(PxActorFlag::eDISABLE_GRAVITY, true);
        body->setRigidBodyFlag(PxRigidBodyFlag::eENABLE_CCD, true);
        body->setLinearDamping(tuning::linear_damping);
        body->setAngularDamping(tuning::angular_damping);

        params.scene->addActor(*body);

        if (!params.vertices.empty())
            compute_aero_from_shape(params.vertices);

        SP_LOG_INFO("car setup complete: mass=%.0f kg", cfg.mass);
        return true;
    }

    inline bool set_chassis(PxConvexMesh* mesh, const std::vector<PxVec3>& vertices, PxPhysics* physics)
    {
        if (!body || !physics)
            return false;

        PxU32 shape_count = body->getNbShapes();
        if (shape_count > 0)
        {
            std::vector<PxShape*> shapes(shape_count);
            body->getShapes(shapes.data(), shape_count);
            for (PxShape* shape : shapes)
                body->detachShape(*shape);
        }

        if (mesh && material)
        {
            PxConvexMeshGeometry geometry(mesh);
            PxShape* shape = physics->createShape(geometry, *material);
            if (shape)
            {
                shape->setFlag(PxShapeFlag::eSCENE_QUERY_SHAPE, false);
                shape->setFlag(PxShapeFlag::eVISUALIZATION, true);
                body->attachShape(*shape);
                shape->release();
            }
        }

        PxVec3 com(tuning::center_of_mass_x, tuning::center_of_mass_y, tuning::center_of_mass_z);
        PxRigidBodyExt::setMassAndUpdateInertia(*body, cfg.mass, &com);

        if (!vertices.empty())
            compute_aero_from_shape(vertices);

        return true;
    }

    inline void update_mass_properties()
    {
        if (!body)
            return;
        
        PxVec3 com(tuning::center_of_mass_x, tuning::center_of_mass_y, tuning::center_of_mass_z);
        PxRigidBodyExt::setMassAndUpdateInertia(*body, cfg.mass, &com);
        
        SP_LOG_INFO("car center of mass set to (%.2f, %.2f, %.2f)", com.x, com.y, com.z);
    }
    
    inline void set_center_of_mass(float x, float y, float z)
    {
        tuning::center_of_mass_x = x;
        tuning::center_of_mass_y = y;
        tuning::center_of_mass_z = z;
        update_mass_properties();
    }
    
    inline void set_center_of_mass_x(float x) { tuning::center_of_mass_x = x; update_mass_properties(); }
    inline void set_center_of_mass_y(float y) { tuning::center_of_mass_y = y; update_mass_properties(); }
    inline void set_center_of_mass_z(float z) { tuning::center_of_mass_z = z; update_mass_properties(); }
    
    inline float get_center_of_mass_x() { return tuning::center_of_mass_x; }
    inline float get_center_of_mass_y() { return tuning::center_of_mass_y; }
    inline float get_center_of_mass_z() { return tuning::center_of_mass_z; }

    inline float get_frontal_area()     { return tuning::frontal_area; }
    inline float get_side_area()        { return tuning::side_area; }
    inline float get_drag_coeff()       { return tuning::drag_coeff; }
    inline float get_lift_coeff_front() { return tuning::lift_coeff_front; }
    inline float get_lift_coeff_rear()  { return tuning::lift_coeff_rear; }
    
    inline void set_frontal_area(float area)   { tuning::frontal_area = area; }
    inline void set_side_area(float area)      { tuning::side_area = area; }
    inline void set_drag_coeff(float cd)       { tuning::drag_coeff = cd; }
    inline void set_lift_coeff_front(float cl) { tuning::lift_coeff_front = cl; }
    inline void set_lift_coeff_rear(float cl)  { tuning::lift_coeff_rear = cl; }
    
    inline void  set_ground_effect_enabled(bool enabled)  { tuning::ground_effect_enabled = enabled; }
    inline bool  get_ground_effect_enabled()              { return tuning::ground_effect_enabled; }
    inline void  set_ground_effect_multiplier(float mult) { tuning::ground_effect_multiplier = mult; }
    inline float get_ground_effect_multiplier()           { return tuning::ground_effect_multiplier; }

    inline void set_throttle(float v)  { input_target.throttle  = PxClamp(v, 0.0f, 1.0f); }
    inline void set_brake(float v)     { input_target.brake     = PxClamp(v, 0.0f, 1.0f); }
    inline void set_steering(float v)  { input_target.steering  = PxClamp(v, -1.0f, 1.0f); }
    inline void set_handbrake(float v) { input_target.handbrake = PxClamp(v, 0.0f, 1.0f); }

    inline void update_input(float dt)
    {
        float diff = input_target.steering - input.steering;
        float max_change = tuning::steering_rate * dt;
        input.steering = (fabsf(diff) <= max_change) ? input_target.steering : input.steering + ((diff > 0) ? max_change : -max_change);

        input.throttle = (input_target.throttle < input.throttle) ? input_target.throttle
            : lerp(input.throttle, input_target.throttle, exp_decay(tuning::throttle_smoothing, dt));
        input.brake = (input_target.brake < input.brake) ? input_target.brake
            : lerp(input.brake, input_target.brake, exp_decay(tuning::throttle_smoothing, dt));

        input.handbrake = input_target.handbrake;
    }
    
    inline void update_suspension(PxScene* scene, float dt)
    {
        PxTransform pose = body->getGlobalPose();
        PxVec3 local_down  = pose.q.rotate(PxVec3(0, -1, 0));
        PxVec3 local_fwd   = pose.q.rotate(PxVec3(0, 0, 1));
        PxVec3 local_right = pose.q.rotate(PxVec3(1, 0, 0));
        
        const int ray_count = 7;
        float half_width = cfg.wheel_width * 0.4f;
        
        auto get_curvature_height = [&](float x_offset) -> float
        {
            float r = cfg.wheel_radius;
            float x = PxMin(fabsf(x_offset), r * 0.95f);
            return r - sqrtf(r * r - x * x);
        };
        
        float dist_near   = cfg.wheel_radius * 0.4f;
        float dist_far    = cfg.wheel_radius * 0.75f;
        float height_near = get_curvature_height(dist_near);
        float height_far  = get_curvature_height(dist_far);
        
        PxVec3 ray_offsets[ray_count] = {
            PxVec3(0.0f,       0.0f,        0.0f),
            PxVec3(dist_near,  0.0f,        height_near),
            PxVec3(dist_far,   0.0f,        height_far),
            PxVec3(-dist_near, 0.0f,        height_near),
            PxVec3(-dist_far,  0.0f,        height_far),
            PxVec3(0.0f,       -half_width, 0.0f),
            PxVec3(0.0f,        half_width, 0.0f)
        };
        
        PxQueryFilterData filter;
        filter.flags = PxQueryFlag::eSTATIC | PxQueryFlag::eDYNAMIC;
        
        float max_curvature_height = height_far;
        float ray_len = cfg.suspension_travel + cfg.wheel_radius + max_curvature_height + 0.5f;
        float max_dist = cfg.suspension_travel + cfg.wheel_radius;
        
        for (int i = 0; i < wheel_count; i++)
        {
            wheel& w = wheels[i];
            w.prev_compression = w.compression;
            
            PxVec3 attach = wheel_offsets[i];
            attach.y += cfg.suspension_travel;
            PxVec3 world_attach = pose.transform(attach);
            
            float min_ground_dist = FLT_MAX;
            PxVec3 best_contact_point = PxVec3(0);
            PxVec3 accumulated_normal = PxVec3(0);
            int hit_count = 0;
            
            for (int r = 0; r < ray_count; r++)
            {
                PxVec3 offset = local_fwd * ray_offsets[r].x + local_right * ray_offsets[r].y - local_down * ray_offsets[r].z;
                PxVec3 ray_origin = world_attach + offset;
                
                    debug_rays[i][r].origin = ray_origin;
                debug_rays[i][r].hit = false;
                
                PxRaycastBuffer hit;
                if (scene->raycast(ray_origin, local_down, ray_len, hit, PxHitFlag::eDEFAULT, filter) &&
                    hit.block.actor && hit.block.actor != body)
                {
                    debug_rays[i][r].hit_point = hit.block.position;
                    debug_rays[i][r].hit = true;
                    
                    float adjusted_dist = hit.block.distance - ray_offsets[r].z;
                    if (adjusted_dist <= max_dist)
                    {
                        hit_count++;
                        accumulated_normal += hit.block.normal;
                        if (adjusted_dist < min_ground_dist)
                        {
                            min_ground_dist = adjusted_dist;
                            best_contact_point = hit.block.position;
                        }
                    }
                }
                else
                {
                    debug_rays[i][r].hit_point = ray_origin + local_down * ray_len;
                }
            }
            
            debug_suspension_top[i] = world_attach;
            PxVec3 wheel_center = world_attach + local_down * (cfg.suspension_travel * (1.0f - w.compression) + cfg.wheel_radius);
            debug_suspension_bottom[i] = wheel_center;
            
            if (hit_count > 0)
            {
                w.grounded = true;
                w.contact_point = best_contact_point;
                w.contact_normal = accumulated_normal.getNormalized();
                float dist_from_rest = min_ground_dist - cfg.wheel_radius;
                w.target_compression = PxClamp(1.0f - dist_from_rest / cfg.suspension_travel, 0.0f, 1.0f);
            }
            else
            {
                w.grounded = false;
                w.target_compression = 0.0f;
                w.contact_normal = PxVec3(0, 1, 0);
            }
            
            // wheel ground tracking
            float compression_error = w.target_compression - w.compression;
            float wheel_spring_force = spring_stiffness[i] * compression_error;
            float wheel_damper_force = -spring_damping[i] * w.compression_velocity * 0.15f;
            float wheel_accel = (wheel_spring_force + wheel_damper_force) / cfg.wheel_mass;
            
            w.compression_velocity += wheel_accel * dt;
            w.compression += w.compression_velocity * dt;
            
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
            float susp_vel = PxClamp(w.compression_velocity * cfg.suspension_travel, -tuning::max_damper_velocity, tuning::max_damper_velocity);
            float damper_ratio = (susp_vel > 0.0f) ? tuning::damping_bump_ratio : tuning::damping_rebound_ratio;
            float damper_f = spring_damping[i] * susp_vel * damper_ratio;
            
            forces[i] = PxClamp(spring_f + damper_f, 0.0f, tuning::max_susp_force);
        }
        
        auto apply_arb = [&](int left, int right, float stiffness)
        {
            float diff = wheels[left].compression - wheels[right].compression;
            float arb_force = diff * stiffness * cfg.suspension_travel;
            if (wheels[left].grounded)  forces[left]  += arb_force;
            if (wheels[right].grounded) forces[right] -= arb_force;
        };
        apply_arb(front_left, front_right, tuning::front_arb_stiffness);
        apply_arb(rear_left, rear_right, tuning::rear_arb_stiffness);
        
        for (int i = 0; i < wheel_count; i++)
        {
            forces[i] = PxClamp(forces[i], 0.0f, tuning::max_susp_force);
            wheels[i].tire_load = forces[i] + cfg.wheel_mass * 9.81f;

            if (forces[i] > 0.0f && wheels[i].grounded)
            {
                PxVec3 force = wheels[i].contact_normal * forces[i];
                PxVec3 pos = pose.transform(wheel_offsets[i]);
                PxRigidBodyExt::addForceAtPos(*body, force, pos, PxForceMode::eFORCE);
            }
        }
    }
    
    inline void apply_tire_forces(float wheel_angles[wheel_count], float dt)
    {
        PxTransform pose = body->getGlobalPose();
        PxVec3 chassis_fwd   = pose.q.rotate(PxVec3(0, 0, 1));
        PxVec3 chassis_right = pose.q.rotate(PxVec3(1, 0, 0));
        
        static const char* wheel_names[] = { "FL", "FR", "RL", "RR" };
        
        if (tuning::log_pacejka)
            SP_LOG_INFO("=== tire forces: speed=%.1f m/s ===", body->getLinearVelocity().magnitude());
        
        for (int i = 0; i < wheel_count; i++)
        {
            wheel& w = wheels[i];
            const char* wheel_name = wheel_names[i];
            
            if (!w.grounded || w.tire_load <= 0.0f)
            {
                if (tuning::log_pacejka)
                    SP_LOG_INFO("[%s] airborne: grounded=%d, tire_load=%.1f", wheel_name, w.grounded, w.tire_load);
                w.slip_angle = w.slip_ratio = w.lateral_force = w.longitudinal_force = 0.0f;
                
                PxVec3 vel = body->getLinearVelocity();
                float car_fwd_speed = vel.dot(chassis_fwd);
                float target_w = car_fwd_speed / cfg.wheel_radius;
                
                if (input.handbrake > tuning::input_deadzone && is_rear(i))
                    w.angular_velocity = 0.0f;
                else
                    w.angular_velocity = lerp(w.angular_velocity, target_w, exp_decay(5.0f, dt));
                
                float temp_above = w.temperature - tuning::tire_ambient_temp;
                if (temp_above > 0.0f)
                    w.temperature -= tuning::tire_cooling_rate * 3.0f * (temp_above / 60.0f) * dt;
                w.temperature = PxMax(w.temperature, tuning::tire_ambient_temp);
                w.rotation += w.angular_velocity * dt;
                continue;
            }
            
            PxVec3 world_pos = pose.transform(wheel_offsets[i]);
            PxVec3 wheel_vel = body->getLinearVelocity() + body->getAngularVelocity().cross(world_pos - pose.p);
            wheel_vel -= w.contact_normal * wheel_vel.dot(w.contact_normal);
            
            float cs = cosf(wheel_angles[i]), sn = sinf(wheel_angles[i]);
            PxVec3 wheel_fwd = chassis_fwd * cs + chassis_right * sn;
            PxVec3 wheel_lat = chassis_right * cs - chassis_fwd * sn;
            
            float vx = wheel_vel.dot(wheel_fwd);
            float vy = wheel_vel.dot(wheel_lat);
            float wheel_speed  = w.angular_velocity * cfg.wheel_radius;
            float ground_speed = sqrtf(vx * vx + vy * vy);
            float max_v = PxMax(fabsf(wheel_speed), fabsf(vx));
            
            if (tuning::log_pacejka)
                SP_LOG_INFO("[%s] vx=%.3f, vy=%.3f, ws=%.3f", wheel_name, vx, vy, wheel_speed);
            
            float base_grip     = tuning::tire_friction * load_sensitive_grip(PxMax(w.tire_load, 0.0f));
            float temp_factor   = get_tire_temp_grip_factor(w.temperature);
            float camber_factor = get_camber_grip_factor(i, w.slip_angle);
            float surface_factor = get_surface_friction(w.contact_surface);
            float peak_force    = base_grip * temp_factor * camber_factor * surface_factor;
            
            if (tuning::log_pacejka)
                SP_LOG_INFO("[%s] load=%.0f, peak_force=%.0f", wheel_name, w.tire_load, peak_force);
            
            float lat_f = 0.0f, long_f = 0.0f;
            
            bool at_rest = ground_speed < 0.1f && fabsf(wheel_speed) < 0.2f;
            if (at_rest)
            {
                w.slip_ratio = w.slip_angle = 0.0f;
                w.angular_velocity = lerp(w.angular_velocity, 0.0f, exp_decay(20.0f, dt));
                w.rotation += w.angular_velocity * dt;
                
                float friction_force = peak_force * 0.8f;
                float friction_gain = cfg.mass * 10.0f;
                lat_f  = PxClamp(-vy * friction_gain, -friction_force, friction_force);
                long_f = PxClamp(-vx * friction_gain, -friction_force, friction_force);
                w.lateral_force = lat_f;
                w.longitudinal_force = long_f;
                PxRigidBodyExt::addForceAtPos(*body, wheel_lat * lat_f + wheel_fwd * long_f, world_pos, PxForceMode::eFORCE);
                
                float temp_above_ambient = w.temperature - tuning::tire_ambient_temp;
                if (temp_above_ambient > 0.0f)
                {
                    float cooling_rate = tuning::tire_cooling_rate * (temp_above_ambient / 50.0f);
                    w.temperature -= cooling_rate * dt;
                    w.temperature = PxMax(w.temperature, tuning::tire_ambient_temp);
                }
                
                if (tuning::log_pacejka)
                    SP_LOG_INFO("[%s] at rest: vx=%.3f, vy=%.3f, friction long_f=%.1f, lat_f=%.1f", wheel_name, vx, vy, long_f, lat_f);
                continue;
            }
            
            if (max_v > tuning::min_slip_speed)
            {
                float raw_slip_ratio = PxClamp((wheel_speed - vx) / max_v, -1.0f, 1.0f);
                float raw_slip_angle = atan2f(vy, fabsf(vx));
                float distance_traveled = ground_speed * dt;
                float blend = 1.0f - expf(-distance_traveled / tuning::tire_relaxation_length);
                w.slip_ratio = lerp(w.slip_ratio, raw_slip_ratio, blend);
                w.slip_angle = lerp(w.slip_angle, raw_slip_angle, blend);

                if (tuning::log_pacejka)
                    SP_LOG_INFO("[%s] slip: sr=%.4f, sa=%.4f", wheel_name, w.slip_ratio, w.slip_angle);

                float effective_slip_angle = w.slip_angle;
                if (fabsf(effective_slip_angle) < tuning::slip_angle_deadband)
                {
                    float factor = fabsf(effective_slip_angle) / tuning::slip_angle_deadband;
                    effective_slip_angle *= factor * factor;
                }

                float lat_mu  = pacejka(effective_slip_angle, tuning::lat_B, tuning::lat_C, tuning::lat_D, tuning::lat_E);
                float long_mu = pacejka(w.slip_ratio, tuning::long_B, tuning::long_C, tuning::long_D, tuning::long_E);

                float combined_mu = sqrtf(lat_mu * lat_mu + long_mu * long_mu);
                if (combined_mu > 1.0f)
                {
                    lat_mu  /= combined_mu;
                    long_mu /= combined_mu;
                }

                float lat_abs  = fabsf(lat_mu);
                float long_abs = fabsf(long_mu);
                if (lat_abs < tuning::min_lateral_grip * long_abs && fabsf(effective_slip_angle) > 0.001f)
                {
                    float scale = tuning::min_lateral_grip * long_abs / PxMax(lat_abs, 0.001f);
                    lat_mu *= PxMin(scale, 2.0f);
                }

                lat_f  = -lat_mu * peak_force;
                long_f =  long_mu * peak_force;
                if (is_rear(i))
                    lat_f *= tuning::rear_grip_ratio;

                float camber = is_front(i) ? tuning::front_camber : tuning::rear_camber;
                bool is_left_wheel = (i == front_left || i == rear_left);
                float camber_thrust = camber * w.tire_load * tuning::camber_thrust_coeff;
                lat_f += is_left_wheel ? -camber_thrust : camber_thrust;

                if (tuning::log_pacejka)
                    SP_LOG_INFO("[%s] pacejka: lat_mu=%.3f, long_mu=%.3f, lat_f=%.1f, long_f=%.1f", wheel_name, lat_mu, long_mu, lat_f, long_f);
            }
            else
            {
                w.slip_ratio = w.slip_angle = 0.0f;
                float speed_factor = PxClamp(max_v / tuning::min_slip_speed, 0.0f, 1.0f);
                float low_speed_force = peak_force * speed_factor * 0.3f;
                long_f = PxClamp((wheel_speed - vx) / tuning::min_slip_speed, -1.0f, 1.0f) * low_speed_force;
                lat_f  = PxClamp(-vy / tuning::min_slip_speed, -1.0f, 1.0f) * low_speed_force;
                
                if (tuning::log_pacejka)
                    SP_LOG_INFO("[%s] low-speed: max_v=%.3f, speed_factor=%.2f, long_f=%.1f, lat_f=%.1f", wheel_name, max_v, speed_factor, long_f, lat_f);
            }
            
            float rolling_heat = fabsf(wheel_speed) * tuning::tire_heat_from_rolling;
            float cooling = tuning::tire_cooling_rate + ground_speed * tuning::tire_cooling_airflow;
            float temp_delta = w.temperature - tuning::tire_ambient_temp;
            float force_magnitude = sqrtf(long_f * long_f + lat_f * lat_f);
            float normalized_force = force_magnitude / tuning::load_reference;
            float friction_work = (max_v > tuning::min_slip_speed)
                ? normalized_force * (fabsf(w.slip_angle) + fabsf(w.slip_ratio))
                : normalized_force * 0.01f;
            
            float heating = friction_work * tuning::tire_heat_from_slip + rolling_heat;
            float cooling_factor = (temp_delta > 0.0f) ? PxMin(temp_delta / 30.0f, 1.0f) : 0.0f;
            w.temperature += (heating - cooling * cooling_factor) * dt;
            w.temperature = PxClamp(w.temperature, tuning::tire_min_temp, tuning::tire_max_temp);
            
            if (is_rear(i) && input.handbrake > tuning::input_deadzone)
            {
                float sliding_f = tuning::handbrake_sliding_factor * peak_force;
                long_f = (fabsf(vx) > 0.01f) ? ((vx > 0.0f ? -1.0f : 1.0f) * sliding_f * input.handbrake) : 0.0f;
                lat_f *= (1.0f - 0.5f * input.handbrake);
            }
            
            w.lateral_force = lat_f;
            w.longitudinal_force = long_f;
            
            PxRigidBodyExt::addForceAtPos(*body, wheel_lat * lat_f + wheel_fwd * long_f, world_pos, PxForceMode::eFORCE);
            
            if (is_rear(i) && input.handbrake > tuning::input_deadzone)
            {
                w.angular_velocity = 0.0f;
            }
            else
            {
                w.angular_velocity += (-long_f * cfg.wheel_radius / wheel_moi[i]) * dt;
                
                bool coasting = input.throttle < 0.01f && input.brake < 0.01f;
                if (coasting || is_front(i) || ground_speed < tuning::min_slip_speed)
                {
                    float target_w = vx / cfg.wheel_radius;
                    float match_rate = coasting ? 50.0f : ((ground_speed < tuning::min_slip_speed) ? tuning::ground_match_rate * 2.0f : tuning::ground_match_rate);
                    w.angular_velocity = lerp(w.angular_velocity, target_w, exp_decay(match_rate, dt));
                }
                
                w.angular_velocity *= (1.0f - tuning::bearing_friction * dt);
            }
            w.rotation += w.angular_velocity * dt;
            
            if (tuning::log_pacejka)
                SP_LOG_INFO("[%s] ang_vel=%.4f, lat_f=%.1f, long_f=%.1f", wheel_name, w.angular_velocity, lat_f, long_f);
        }
        if (tuning::log_pacejka)
            SP_LOG_INFO("=== pacejka tick end ===\n");
    }
    
    inline void apply_self_aligning_torque()
    {
        float sat = 0.0f;
        for (int i = 0; i < 2; i++)
            if (wheels[i].grounded)
                sat += wheels[i].lateral_force * tuning::pneumatic_trail;
        
        PxVec3 up = body->getGlobalPose().q.rotate(PxVec3(0, 1, 0));
        body->addTorque(up * sat * tuning::self_align_gain, PxForceMode::eFORCE);
    }
    
    inline void apply_lsd_torque(float total_torque, float dt)
    {
        float w_left  = wheels[rear_left].angular_velocity;
        float w_right = wheels[rear_right].angular_velocity;
        float delta_w = w_left - w_right;

        float lock_ratio = (total_torque >= 0.0f) ? tuning::lsd_lock_ratio_accel : tuning::lsd_lock_ratio_decel;

        float lock_torque = tuning::lsd_preload + fabsf(delta_w) * lock_ratio * fabsf(total_torque);
        lock_torque = PxMin(lock_torque, fabsf(total_torque) * 0.9f);

        float left_bias  = (delta_w > 0.0f) ? -lock_torque : lock_torque;
        float right_bias = (delta_w > 0.0f) ? lock_torque : -lock_torque;

        wheels[rear_left].angular_velocity  += (total_torque * 0.5f + left_bias * 0.5f) / wheel_moi[rear_left] * dt;
        wheels[rear_right].angular_velocity += (total_torque * 0.5f + right_bias * 0.5f) / wheel_moi[rear_right] * dt;
    }
    
    inline void apply_drivetrain(float forward_speed_kmh, float dt)
    {
        float forward_speed_ms = forward_speed_kmh / 3.6f;

        update_automatic_gearbox(dt, input.throttle, forward_speed_ms);

        if (downshift_blip_timer > 0.0f)
            downshift_blip_timer -= dt;

        float avg_wheel_rpm = (wheels[rear_left].angular_velocity + wheels[rear_right].angular_velocity) * 0.5f * 60.0f / (2.0f * PxPi);
        float wheel_driven_rpm = wheel_rpm_to_engine_rpm(fabsf(avg_wheel_rpm), current_gear);

        bool coasting = input.throttle < tuning::input_deadzone && input.brake < tuning::input_deadzone;
        if (coasting && current_gear >= 2)
        {
            float ground_wheel_rpm = fabsf(forward_speed_ms) / cfg.wheel_radius * 60.0f / (2.0f * PxPi);
            float ground_driven_rpm = wheel_rpm_to_engine_rpm(ground_wheel_rpm, current_gear);
            wheel_driven_rpm = PxMax(wheel_driven_rpm, ground_driven_rpm);
        }

        if (is_shifting)                                                  clutch = 0.2f;
        else if (current_gear == 1)                                       clutch = 0.0f;
        else if (fabsf(forward_speed_ms) < 2.0f && input.throttle > 0.1f) clutch = lerp(clutch, 1.0f, exp_decay(tuning::clutch_engagement_rate, dt));
        else                                                              clutch = 1.0f;

        float blip = (downshift_blip_timer > 0.0f) ? tuning::downshift_blip_amount * (downshift_blip_timer / tuning::downshift_blip_duration) : 0.0f;
        float effective_throttle_for_rpm = PxMax(input.throttle, blip);
        float free_rev_rpm = tuning::engine_idle_rpm + effective_throttle_for_rpm * (tuning::engine_redline_rpm - tuning::engine_idle_rpm) * 0.7f;
        
        // target rpm from clutch state
        float target_rpm;
        if (current_gear == 1)
            target_rpm = free_rev_rpm;
        else if (clutch < 0.9f)
            target_rpm = lerp(free_rev_rpm, PxMax(wheel_driven_rpm, tuning::engine_idle_rpm), clutch);
        else
            target_rpm = PxMax(wheel_driven_rpm, tuning::engine_idle_rpm);

        engine_rpm = lerp(engine_rpm, target_rpm, exp_decay(tuning::engine_rpm_smoothing, dt));
        engine_rpm = PxClamp(engine_rpm, tuning::engine_idle_rpm, tuning::engine_max_rpm);

        // engine braking
        if (input.throttle < tuning::input_deadzone && clutch > 0.5f && current_gear >= 2)
        {
            float wheel_brake_torque = tuning::engine_friction * engine_rpm * 0.1f * fabsf(tuning::gear_ratios[current_gear]) * tuning::final_drive * 0.5f;
            for (int i = rear_left; i <= rear_right; i++)
                if (wheels[i].angular_velocity > 0.0f)
                    wheels[i].angular_velocity -= wheel_brake_torque / wheel_moi[i] * dt;
        }
        
        update_boost(input.throttle, engine_rpm, dt);
        
        if (input.throttle > tuning::input_deadzone && current_gear >= 2)
        {
            float base_torque = get_engine_torque(engine_rpm);
            float boosted_torque = base_torque * (1.0f + boost_pressure * tuning::boost_torque_mult);
            float engine_torque = boosted_torque * input.throttle;
            
            tc_active = false;
            if (tuning::tc_enabled)
            {
                float max_slip = 0.0f;
                for (int i = rear_left; i <= rear_right; i++)
                    if (wheels[i].grounded && wheels[i].slip_ratio > 0.0f)
                        max_slip = PxMax(max_slip, wheels[i].slip_ratio);
                
                float target_reduction = 0.0f;
                if (max_slip > tuning::tc_slip_threshold)
                {
                    tc_active = true;
                    target_reduction = PxClamp((max_slip - tuning::tc_slip_threshold) * 5.0f, 0.0f, tuning::tc_power_reduction);
                }
                
                tc_reduction = lerp(tc_reduction, target_reduction, exp_decay(tuning::tc_response_rate, dt));
                engine_torque *= (1.0f - tc_reduction);
            }
            else
            {
                tc_reduction = 0.0f;
            }
            
            float gear_ratio = tuning::gear_ratios[current_gear] * tuning::final_drive;
            float wheel_torque = engine_torque * gear_ratio * clutch * tuning::drivetrain_efficiency;
            if (is_shifting) wheel_torque *= 0.3f;

            apply_lsd_torque(wheel_torque, dt);
        }
        else if (input.throttle > tuning::input_deadzone && current_gear == 0)
        {
            float brake_torque = tuning::brake_force * cfg.wheel_radius * input.throttle * 0.5f;
            for (int i = 0; i < wheel_count; i++)
            {
                if (wheels[i].angular_velocity < 0.0f)
                {
                    wheels[i].angular_velocity += brake_torque / wheel_moi[i] * dt;
                    if (wheels[i].angular_velocity > 0.0f) wheels[i].angular_velocity = 0.0f;
                }
            }
        }
        else
        {
            tc_reduction = lerp(tc_reduction, 0.0f, exp_decay(tuning::tc_response_rate * 2.0f, dt));
            tc_active = false;
        }
        
        if (input.brake > tuning::input_deadzone)
        {
            if (forward_speed_kmh > tuning::braking_speed_threshold)
            {
                float total_torque = tuning::brake_force * cfg.wheel_radius * input.brake;
                float front_t = total_torque * tuning::brake_bias_front * 0.5f;
                float rear_t  = total_torque * (1.0f - tuning::brake_bias_front) * 0.5f;
                
                abs_phase += tuning::abs_pulse_frequency * dt;
                if (abs_phase > 1.0f)
                    abs_phase -= 1.0f;
                
                for (int i = 0; i < wheel_count; i++)
                {
                    float t = is_front(i) ? front_t : rear_t;
                    
                    float brake_efficiency = get_brake_efficiency(wheels[i].brake_temp);
                    t *= brake_efficiency;
                    
                    float heat = fabsf(wheels[i].angular_velocity) * t * tuning::brake_heat_coefficient * dt;
                    wheels[i].brake_temp += heat;
                    wheels[i].brake_temp = PxMin(wheels[i].brake_temp, tuning::brake_max_temp);
                    
                    abs_active[i] = false;
                    if (tuning::abs_enabled && wheels[i].grounded && -wheels[i].slip_ratio > tuning::abs_slip_threshold)
                    {
                        abs_active[i] = true;
                        t *= (abs_phase < 0.5f) ? tuning::abs_release_rate : 1.0f;
                    }
                    
                    float sign = wheels[i].angular_velocity >= 0.0f ? -1.0f : 1.0f;
                    float new_w = wheels[i].angular_velocity + sign * t / wheel_moi[i] * dt;
                    
                    wheels[i].angular_velocity = ((wheels[i].angular_velocity > 0 && new_w < 0) || (wheels[i].angular_velocity < 0 && new_w > 0))
                        ? 0.0f : new_w;
                }
            }
            else
            {
                for (int i = 0; i < wheel_count; i++)
                    abs_active[i] = false;
                
                if (current_gear == 0)
                {
                    float engine_torque = get_engine_torque(engine_rpm) * input.brake * tuning::reverse_power_ratio;
                    float gear_ratio = tuning::gear_ratios[0] * tuning::final_drive;
                    apply_lsd_torque(engine_torque * gear_ratio * clutch, dt);
                }
                else if (forward_speed_ms > -0.5f && current_gear != 0 && !is_shifting)
                {
                    current_gear = 0;
                    is_shifting = true;
                    shift_timer = tuning::shift_time * 2.0f;
                }
            }
        }
        else
        {
            for (int i = 0; i < wheel_count; i++)
                abs_active[i] = false;
        }
        
        // handbrake
        if (input.handbrake > tuning::input_deadzone)
        {
            wheels[rear_left].angular_velocity = 0.0f;
            wheels[rear_right].angular_velocity = 0.0f;
        }

        // coasting wheel sync
        if (input.throttle < tuning::input_deadzone && input.brake < tuning::input_deadzone && input.handbrake < tuning::input_deadzone)
        {
            float ground_angular_v = fabsf(forward_speed_ms) / cfg.wheel_radius;
            for (int i = rear_left; i <= rear_right; i++)
            {
                float wheel_v = fabsf(wheels[i].angular_velocity);
                if (ground_angular_v > 1.0f && (wheel_v < ground_angular_v * 0.5f || wheel_v > ground_angular_v * 1.5f))
                {
                    float sign = (forward_speed_ms >= 0.0f) ? 1.0f : -1.0f;
                    wheels[i].angular_velocity = sign * ground_angular_v;
                }
            }
        }
    }
    
    inline void apply_aero_and_resistance()
    {
        PxTransform pose = body->getGlobalPose();
        PxVec3 vel = body->getLinearVelocity();
        float speed = vel.magnitude();

        // aero application points from mesh-computed center
        float aero_height = tuning::aero_center_height;
        PxVec3 front_pos = pose.p + pose.q.rotate(PxVec3(0, aero_height, tuning::aero_center_front_z));
        PxVec3 rear_pos  = pose.p + pose.q.rotate(PxVec3(0, aero_height, tuning::aero_center_rear_z));
        
        aero_debug.valid = false;
        aero_debug.position = pose.p;
        aero_debug.velocity = vel;
        aero_debug.front_aero_pos = front_pos;
        aero_debug.rear_aero_pos = rear_pos;
        aero_debug.ride_height = cfg.suspension_height + cfg.wheel_radius; // default ride height
        aero_debug.ground_effect_factor = 1.0f;
        aero_debug.yaw_angle = 0.0f;
        aero_debug.drag_force = PxVec3(0);
        aero_debug.front_downforce = PxVec3(0);
        aero_debug.rear_downforce = PxVec3(0);
        aero_debug.side_force = PxVec3(0);
        
        if (speed < 0.5f)
        {
            float tire_load = 0.0f;
            for (int i = 0; i < wheel_count; i++)
                if (wheels[i].grounded)
                    tire_load += wheels[i].tire_load;
            if (speed > 0.1f && tire_load > 0.0f)
                body->addForce(-vel.getNormalized() * tuning::rolling_resistance * tire_load, PxForceMode::eFORCE);
            aero_debug.valid = true;
            return;
        }
        
        PxVec3 local_fwd   = pose.q.rotate(PxVec3(0, 0, 1));
        PxVec3 local_up    = pose.q.rotate(PxVec3(0, 1, 0));
        PxVec3 local_right = pose.q.rotate(PxVec3(1, 0, 0));
        
        float forward_speed = vel.dot(local_fwd);
        float lateral_speed = vel.dot(local_right);
        
        float yaw_angle = 0.0f;
        if (speed > 1.0f)
        {
            PxVec3 vel_norm = vel.getNormalized();
            float cos_yaw = PxClamp(vel_norm.dot(local_fwd), -1.0f, 1.0f);
            yaw_angle = acosf(fabsf(cos_yaw));
        }
        
        float front_compression = (wheels[front_left].compression + wheels[front_right].compression) * 0.5f;
        float rear_compression  = (wheels[rear_left].compression + wheels[rear_right].compression) * 0.5f;
        float pitch_angle = (rear_compression - front_compression) * cfg.suspension_travel / (cfg.length * 0.7f);
        
        float avg_compression = (front_compression + rear_compression) * 0.5f;
        float ride_height = cfg.suspension_height - avg_compression * cfg.suspension_travel + cfg.wheel_radius;
        
        // drag
        float base_drag = 0.5f * tuning::air_density * tuning::drag_coeff * tuning::frontal_area * speed * speed;
        
        float yaw_drag_factor = 1.0f;
        if (tuning::yaw_aero_enabled && yaw_angle > 0.01f)
        {
            float yaw_factor = sinf(yaw_angle);
            yaw_drag_factor = 1.0f + yaw_factor * (tuning::yaw_drag_multiplier - 1.0f);
        }
        
        PxVec3 drag_force_vec = -vel.getNormalized() * base_drag * yaw_drag_factor;
        body->addForce(drag_force_vec, PxForceMode::eFORCE);

        // side force
        PxVec3 side_force_vec(0);
        if (tuning::yaw_aero_enabled && fabsf(lateral_speed) > 1.0f)
        {
            float side_force = 0.5f * tuning::air_density * tuning::yaw_side_force_coeff * tuning::side_area * lateral_speed * fabsf(lateral_speed);
            side_force_vec = -local_right * side_force;
            body->addForce(side_force_vec, PxForceMode::eFORCE);
        }
        
        // downforce
        PxVec3 front_downforce_vec(0);
        PxVec3 rear_downforce_vec(0);
        float ground_effect_factor = 1.0f;
        
        if (speed > 10.0f)
        {
            float dyn_pressure = 0.5f * tuning::air_density * speed * speed;
            
            float front_cl = tuning::lift_coeff_front;
            float rear_cl  = tuning::lift_coeff_rear;
            
            if (tuning::ground_effect_enabled)
            {
                if (ride_height < tuning::ground_effect_height_max)
                {
                    float height_ratio = PxClamp((tuning::ground_effect_height_max - ride_height) / 
                                                 (tuning::ground_effect_height_max - tuning::ground_effect_height_ref), 0.0f, 1.0f);
                    ground_effect_factor = 1.0f + height_ratio * (tuning::ground_effect_multiplier - 1.0f);
                }
            }
            
            if (tuning::pitch_aero_enabled)
            {
                float pitch_shift = pitch_angle * tuning::pitch_sensitivity;
                front_cl *= (1.0f - pitch_shift);
                rear_cl  *= (1.0f + pitch_shift);
            }
            
            float yaw_downforce_factor = 1.0f;
            if (tuning::yaw_aero_enabled && yaw_angle > 0.1f)
                yaw_downforce_factor = PxMax(0.3f, 1.0f - sinf(yaw_angle) * 0.7f);
            
            float front_downforce = front_cl * dyn_pressure * tuning::frontal_area * ground_effect_factor * yaw_downforce_factor;
            float rear_downforce  = rear_cl  * dyn_pressure * tuning::frontal_area * ground_effect_factor * yaw_downforce_factor;
            
            front_downforce_vec = local_up * front_downforce;
            rear_downforce_vec  = local_up * rear_downforce;
            
            PxRigidBodyExt::addForceAtPos(*body, front_downforce_vec, front_pos, PxForceMode::eFORCE);
            PxRigidBodyExt::addForceAtPos(*body, rear_downforce_vec, rear_pos, PxForceMode::eFORCE);
        }
        
        // rolling resistance
        float tire_load = 0.0f;
        for (int i = 0; i < wheel_count; i++)
            if (wheels[i].grounded)
                tire_load += wheels[i].tire_load;
        
        if (tire_load > 0.0f)
            body->addForce(-vel.getNormalized() * tuning::rolling_resistance * tire_load, PxForceMode::eFORCE);
        
        aero_debug.drag_force = drag_force_vec;
        aero_debug.front_downforce = front_downforce_vec;
        aero_debug.rear_downforce = rear_downforce_vec;
        aero_debug.side_force = side_force_vec;
        aero_debug.front_aero_pos = front_pos;
        aero_debug.rear_aero_pos = rear_pos;
        aero_debug.ride_height = ride_height;
        aero_debug.yaw_angle = yaw_angle;
        aero_debug.ground_effect_factor = ground_effect_factor;
        aero_debug.valid = true;
    }
    
    inline void calculate_steering(float forward_speed, float speed_kmh, float out_angles[wheel_count])
    {
        float reduction = (speed_kmh > 80.0f)
            ? 1.0f - tuning::high_speed_steer_reduction * PxClamp((speed_kmh - 80.0f) / 120.0f, 0.0f, 1.0f)
            : 1.0f;

        float curved_input = copysignf(powf(fabsf(input.steering), tuning::steering_linearity), input.steering);
        float base = curved_input * tuning::max_steer_angle * reduction;

        // bump steer
        float front_left_bump  = wheels[front_left].compression * cfg.suspension_travel * tuning::front_bump_steer;
        float front_right_bump = wheels[front_right].compression * cfg.suspension_travel * tuning::front_bump_steer;
        float rear_left_bump   = wheels[rear_left].compression * cfg.suspension_travel * tuning::rear_bump_steer;
        float rear_right_bump  = wheels[rear_right].compression * cfg.suspension_travel * tuning::rear_bump_steer;

        out_angles[rear_left]  = tuning::rear_toe + rear_left_bump;
        out_angles[rear_right] = -tuning::rear_toe - rear_right_bump;

        if (fabsf(base) < tuning::steering_deadzone)
        {
            out_angles[front_left]  = tuning::front_toe + front_left_bump;
            out_angles[front_right] = -tuning::front_toe - front_right_bump;
            return;
        }

        // ackermann geometry
        if (forward_speed >= 0.0f)
        {
            float wheelbase  = cfg.length * 0.7f;
            float half_track = (cfg.width - cfg.wheel_width) * 0.5f;
            float turn_r     = wheelbase / tanf(fabsf(base));

            float inner = atanf(wheelbase / PxMax(turn_r - half_track, 0.1f));
            float outer = atanf(wheelbase / PxMax(turn_r + half_track, 0.1f));

            if (base > 0.0f)
            {
                out_angles[front_right] =  inner - tuning::front_toe + front_right_bump;
                out_angles[front_left]  =  outer + tuning::front_toe + front_left_bump;
            }
            else
            {
                out_angles[front_left]  = -inner + tuning::front_toe + front_left_bump;
                out_angles[front_right] = -outer - tuning::front_toe + front_right_bump;
            }
        }
        else
        {
            out_angles[front_left]  = base + tuning::front_toe + front_left_bump;
            out_angles[front_right] = base - tuning::front_toe - front_right_bump;
        }
    }

    inline void tick(float dt)
    {
        if (!body) return;

        PxScene* scene = body->getScene();
        if (!scene) return;

        update_input(dt);

        PxTransform pose = body->getGlobalPose();
        PxVec3 fwd = pose.q.rotate(PxVec3(0, 0, 1));
        PxVec3 vel = body->getLinearVelocity();
        float forward_speed = vel.dot(fwd);
        float speed_kmh = vel.magnitude() * 3.6f;

        prev_velocity = vel;
        
        // brake cooling
        float airspeed = vel.magnitude();
        for (int i = 0; i < wheel_count; i++)
        {
            float temp_above_ambient = wheels[i].brake_temp - tuning::brake_ambient_temp;
            if (temp_above_ambient > 0.0f)
            {
                float h = tuning::brake_cooling_base + airspeed * tuning::brake_cooling_airflow;
                float cooling_power = h * temp_above_ambient;
                float temp_drop = (cooling_power / tuning::brake_thermal_mass) * dt;
                wheels[i].brake_temp -= temp_drop;
                wheels[i].brake_temp = PxMax(wheels[i].brake_temp, tuning::brake_ambient_temp);
            }
        }
        
        float wheel_angles[wheel_count];
        calculate_steering(forward_speed, speed_kmh, wheel_angles);
        
        apply_drivetrain(forward_speed * 3.6f, dt);
        update_suspension(scene, dt);
        apply_suspension_forces(dt);
        apply_tire_forces(wheel_angles, dt);
        apply_self_aligning_torque();
        apply_aero_and_resistance();
        
        body->addForce(PxVec3(0, -9.81f * cfg.mass, 0), PxForceMode::eFORCE);
        
        // wheel speed correction
        float ground_angular_v = fabsf(forward_speed) / cfg.wheel_radius;
        if (ground_angular_v > 5.0f && input.handbrake < tuning::input_deadzone)
        {
            float sign = (forward_speed >= 0.0f) ? 1.0f : -1.0f;
            for (int i = rear_left; i <= rear_right; i++)
            {
                float wheel_v = fabsf(wheels[i].angular_velocity);
                if (wheel_v < ground_angular_v * 0.3f || wheel_v > ground_angular_v * 1.5f)
                    wheels[i].angular_velocity = sign * ground_angular_v;
            }
        }
        
        if (tuning::log_telemetry)
        {
            float avg_wheel_w = (wheels[rear_left].angular_velocity + wheels[rear_right].angular_velocity) * 0.5f;
            float wheel_surface_speed = avg_wheel_w * cfg.wheel_radius * 3.6f;
            SP_LOG_INFO("rpm=%.0f, speed=%.0f km/h, gear=%s%s, wheel_speed=%.0f km/h, throttle=%.0f%%",
                engine_rpm, speed_kmh, get_gear_string(), is_shifting ? "(shifting)" : "",
                wheel_surface_speed, input.throttle * 100.0f);
        }
    }

    inline float get_speed_kmh()        { return body ? body->getLinearVelocity().magnitude() * 3.6f : 0.0f; }
    inline float get_throttle()         { return input.throttle; }
    inline float get_brake()            { return input.brake; }
    inline float get_steering()         { return input.steering; }
    inline float get_handbrake()        { return input.handbrake; }
    inline float get_suspension_travel(){ return cfg.suspension_travel; }
    
    inline bool is_valid_wheel(int i) { return i >= 0 && i < wheel_count; }
    inline const char* get_wheel_name(int i)
    {
        static const char* names[] = { "FL", "FR", "RL", "RR" };
        return is_valid_wheel(i) ? names[i] : "??";
    }
    
    #define WHEEL_GETTER(name, field) inline float get_wheel_##name(int i) { return is_valid_wheel(i) ? wheels[i].field : 0.0f; }
    WHEEL_GETTER(compression, compression)
    WHEEL_GETTER(slip_angle, slip_angle)
    WHEEL_GETTER(slip_ratio, slip_ratio)
    WHEEL_GETTER(tire_load, tire_load)
    WHEEL_GETTER(lateral_force, lateral_force)
    WHEEL_GETTER(longitudinal_force, longitudinal_force)
    WHEEL_GETTER(angular_velocity, angular_velocity)
    WHEEL_GETTER(rotation, rotation)
    WHEEL_GETTER(temperature, temperature)
    #undef WHEEL_GETTER
    
    inline bool is_wheel_grounded(int i) { return is_valid_wheel(i) && wheels[i].grounded; }
    
    inline float get_wheel_suspension_force(int i)
    {
        if (!is_valid_wheel(i) || !wheels[i].grounded) return 0.0f;
        return spring_stiffness[i] * wheels[i].compression * cfg.suspension_travel;
    }
    
    inline float get_wheel_temp_grip_factor(int i)
    {
        return is_valid_wheel(i) ? get_tire_temp_grip_factor(wheels[i].temperature) : 1.0f;
    }
    
    inline float get_chassis_visual_offset_y()
    {
        const float offset = 0.1f;
        return -(cfg.height * 0.5f + cfg.suspension_height) + offset;
    }
    
    inline void set_abs_enabled(bool enabled) { tuning::abs_enabled = enabled; }
    inline bool get_abs_enabled()             { return tuning::abs_enabled; }
    inline bool is_abs_active(int i)          { return is_valid_wheel(i) && abs_active[i]; }
    inline bool is_abs_active_any()           { for (int i = 0; i < wheel_count; i++) if (abs_active[i]) return true; return false; }
    
    inline void  set_tc_enabled(bool enabled) { tuning::tc_enabled = enabled; }
    inline bool  get_tc_enabled()             { return tuning::tc_enabled; }
    inline bool  is_tc_active()               { return tc_active; }
    inline float get_tc_reduction()           { return tc_reduction; }
    
    inline void set_manual_transmission(bool enabled) { tuning::manual_transmission = enabled; }
    inline bool get_manual_transmission()             { return tuning::manual_transmission; }
    
    inline void begin_shift(int direction)
    {
        is_shifting = true;
        shift_timer = tuning::shift_time;
        last_shift_direction = direction;
    }
    
    inline void shift_up()
    {
        if (!tuning::manual_transmission || is_shifting || current_gear >= tuning::gear_count - 1) return;
        current_gear = (current_gear == 0) ? 1 : current_gear + 1; // from reverse, go to neutral first
        begin_shift(1);
    }
    
    inline void shift_down()
    {
        if (!tuning::manual_transmission || is_shifting || current_gear <= 0) return;
        current_gear = (current_gear == 1) ? 0 : current_gear - 1; // from neutral, go to reverse
        begin_shift(-1);
    }
    
    inline void shift_to_neutral()
    {
        if (!tuning::manual_transmission || is_shifting) return;
        current_gear = 1;
        begin_shift(0);
    }
    
    inline int         get_gear()                  { return current_gear; }
    inline int         get_current_gear()          { return current_gear; }
    inline const char* get_current_gear_string()   { return get_gear_string(); }
    inline float       get_engine_rpm()            { return engine_rpm; }
    inline float       get_current_engine_rpm()    { return engine_rpm; }
    inline bool        get_is_shifting()           { return is_shifting; }
    inline float       get_clutch()                { return clutch; }
    inline float       get_engine_torque_current() { return get_engine_torque(engine_rpm) * (1.0f + boost_pressure * tuning::boost_torque_mult); }
    inline float       get_redline_rpm()           { return tuning::engine_redline_rpm; }
    inline float       get_max_rpm()               { return tuning::engine_max_rpm; }
    inline float       get_idle_rpm()              { return tuning::engine_idle_rpm; }
    
    inline void  set_turbo_enabled(bool enabled) { tuning::turbo_enabled = enabled; }
    inline bool  get_turbo_enabled()             { return tuning::turbo_enabled; }
    inline float get_boost_pressure()            { return boost_pressure; }
    inline float get_boost_max_pressure()        { return tuning::boost_max_pressure; }
    
    inline float get_wheel_brake_temp(int i)       { return is_valid_wheel(i) ? wheels[i].brake_temp : 0.0f; }
    inline float get_wheel_brake_efficiency(int i) { return is_valid_wheel(i) ? get_brake_efficiency(wheels[i].brake_temp) : 1.0f; }
    
    inline void set_wheel_surface(int i, surface_type surface)
    {
        if (is_valid_wheel(i))
            wheels[i].contact_surface = surface;
    }
    inline surface_type get_wheel_surface(int i) { return is_valid_wheel(i) ? wheels[i].contact_surface : surface_asphalt; }
    inline const char* get_surface_name(surface_type surface)
    {
        static const char* names[] = { "Asphalt", "Concrete", "Wet", "Gravel", "Grass", "Ice" };
        return (surface >= 0 && surface < surface_count) ? names[surface] : "Unknown";
    }
    
    inline float get_front_camber() { return tuning::front_camber; }
    inline float get_rear_camber()  { return tuning::rear_camber; }
    inline float get_front_toe()    { return tuning::front_toe; }
    inline float get_rear_toe()     { return tuning::rear_toe; }
    
    inline void set_wheel_offset(int wheel, float x, float z)
    {
        if (wheel >= 0 && wheel < wheel_count)
        {
            wheel_offsets[wheel].x = x;
            wheel_offsets[wheel].z = z;
        }
    }
    
    inline PxVec3 get_wheel_offset(int wheel)
    {
        if (wheel >= 0 && wheel < wheel_count)
            return wheel_offsets[wheel];
        return PxVec3(0);
    }
    
    inline void set_draw_raycasts(bool enabled)   { tuning::draw_raycasts = enabled; }
    inline bool get_draw_raycasts()               { return tuning::draw_raycasts; }
    inline void set_draw_suspension(bool enabled) { tuning::draw_suspension = enabled; }
    inline bool get_draw_suspension()             { return tuning::draw_suspension; }
    inline void set_log_pacejka(bool enabled)     { tuning::log_pacejka = enabled; }
    inline bool get_log_pacejka()                 { return tuning::log_pacejka; }
    
    inline const aero_debug_data& get_aero_debug() { return aero_debug; }
    inline const shape_2d& get_shape_data() { return shape_data; }
    
    inline void get_debug_ray(int wheel, int ray, PxVec3& origin, PxVec3& hit_point, bool& hit)
    {
        if (wheel >= 0 && wheel < wheel_count && ray >= 0 && ray < debug_rays_per_wheel)
        {
            origin    = debug_rays[wheel][ray].origin;
            hit_point = debug_rays[wheel][ray].hit_point;
            hit       = debug_rays[wheel][ray].hit;
        }
    }
    
    inline void get_debug_suspension(int wheel, PxVec3& top, PxVec3& bottom)
    {
        if (wheel >= 0 && wheel < wheel_count)
        {
            top    = debug_suspension_top[wheel];
            bottom = debug_suspension_bottom[wheel];
        }
    }
    
    inline int get_debug_rays_per_wheel() { return debug_rays_per_wheel; }

    // debug window - call this during tick to display car telemetry
    inline void debug_window(bool* visible = nullptr)
    {
        if (!spartan::Engine::IsFlagSet(spartan::EngineMode::EditorVisible))
            return;
        if (visible && !*visible)
            return;
        if (!body)
            return;
        if (!ImGui::Begin("Car Telemetry", visible, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoResize))
        {
            ImGui::End();
            return;
        }

        float speed = get_speed_kmh();
        ImGui::Text("Speed: %.1f km/h", speed);
        ImGui::Text("Gear: %s %s", get_gear_string(), is_shifting ? "(shifting)" : "");
        ImGui::Text("RPM: %.0f / %.0f", engine_rpm, tuning::engine_redline_rpm);

        float rpm_fraction = engine_rpm / tuning::engine_max_rpm;
        ImVec4 rpm_color = (engine_rpm > tuning::engine_redline_rpm) ? ImVec4(1, 0, 0, 1) : ImVec4(0.2f, 0.8f, 0.2f, 1);
        ImGui::PushStyleColor(ImGuiCol_PlotHistogram, rpm_color);
        ImGui::ProgressBar(rpm_fraction, ImVec2(-1, 0), "");
        ImGui::PopStyleColor();

        ImGui::Text("Throttle: %.0f%%  Brake: %.0f%%  Clutch: %.0f%%", input.throttle * 100, input.brake * 100, clutch * 100);

        ImGui::Separator();
        ImGui::Text("Driver Aids:");
        ImGui::Text("  ABS: %s %s", tuning::abs_enabled ? "ON" : "OFF", is_abs_active_any() ? "(active)" : "");
        ImGui::Text("  TC:  %s %s", tuning::tc_enabled ? "ON" : "OFF", tc_active ? "(active)" : "");
        if (tuning::turbo_enabled)
            ImGui::Text("  Boost: %.2f bar", boost_pressure);

        ImGui::Separator();
        if (ImGui::BeginTable("wheels", 7, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg))
        {
            ImGui::TableSetupColumn("Wheel");
            ImGui::TableSetupColumn("Grounded");
            ImGui::TableSetupColumn("Load (N)");
            ImGui::TableSetupColumn("Slip Ratio");
            ImGui::TableSetupColumn("Slip Angle");
            ImGui::TableSetupColumn("Tire Temp");
            ImGui::TableSetupColumn("Brake Temp");
            ImGui::TableHeadersRow();

            const char* names[] = { "FL", "FR", "RL", "RR" };
            for (int i = 0; i < wheel_count; i++)
            {
                ImGui::TableNextRow();
                ImGui::TableNextColumn(); ImGui::Text("%s", names[i]);
                ImGui::TableNextColumn(); ImGui::Text("%s", wheels[i].grounded ? "yes" : "no");
                ImGui::TableNextColumn(); ImGui::Text("%.0f", wheels[i].tire_load);
                ImGui::TableNextColumn(); ImGui::Text("%.3f", wheels[i].slip_ratio);
                ImGui::TableNextColumn(); ImGui::Text("%.2f", wheels[i].slip_angle * 57.2958f); // to degrees
                ImGui::TableNextColumn();
                {
                    float temp = wheels[i].temperature;
                    ImVec4 color = (temp > tuning::tire_optimal_temp + 20) ? ImVec4(1, 0.5f, 0, 1) :
                                   (temp < tuning::tire_optimal_temp - 20) ? ImVec4(0.5f, 0.5f, 1, 1) :
                                   ImVec4(0.2f, 1, 0.2f, 1);
                    ImGui::TextColored(color, "%.0f C", temp);
                }
                ImGui::TableNextColumn();
                {
                    float temp = wheels[i].brake_temp;
                    ImVec4 color = (temp > tuning::brake_fade_temp) ? ImVec4(1, 0, 0, 1) :
                                   (temp > tuning::brake_optimal_temp) ? ImVec4(1, 0.5f, 0, 1) :
                                   ImVec4(0.8f, 0.8f, 0.8f, 1);
                    ImGui::TextColored(color, "%.0f C", temp);
                }
            }
            ImGui::EndTable();
        }

        ImGui::Separator();
        if (ImGui::BeginTable("forces", 4, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg))
        {
            ImGui::TableSetupColumn("Wheel");
            ImGui::TableSetupColumn("Lateral (N)");
            ImGui::TableSetupColumn("Longitudinal (N)");
            ImGui::TableSetupColumn("Suspension (N)");
            ImGui::TableHeadersRow();

            const char* names[] = { "FL", "FR", "RL", "RR" };
            for (int i = 0; i < wheel_count; i++)
            {
                ImGui::TableNextRow();
                ImGui::TableNextColumn(); ImGui::Text("%s", names[i]);
                ImGui::TableNextColumn(); ImGui::Text("%.0f", wheels[i].lateral_force);
                ImGui::TableNextColumn(); ImGui::Text("%.0f", wheels[i].longitudinal_force);
                ImGui::TableNextColumn(); ImGui::Text("%.0f", get_wheel_suspension_force(i));
            }
            ImGui::EndTable();
        }

        if (aero_debug.valid)
        {
            ImGui::Separator();
            ImGui::Text("Aerodynamics:");
            ImGui::Text("  Ride Height: %.3f m", aero_debug.ride_height);
            ImGui::Text("  Yaw Angle: %.1f deg", aero_debug.yaw_angle * 57.2958f);
            ImGui::Text("  Ground Effect: %.2fx", aero_debug.ground_effect_factor);
            ImGui::Text("  Drag: %.0f N", aero_debug.drag_force.magnitude());
            ImGui::Text("  Downforce F/R: %.0f / %.0f N", aero_debug.front_downforce.magnitude(), aero_debug.rear_downforce.magnitude());
        }

        ImGui::End();
    }
}
