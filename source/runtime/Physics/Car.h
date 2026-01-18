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

//= INCLUDES ===========================
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
#include "../Logging/Log.h"
//======================================

namespace car
{
    using namespace physx;

    namespace tuning
    {
        // engine
        constexpr float engine_idle_rpm        = 1000.0f;
        constexpr float engine_redline_rpm     = 9250.0f;
        constexpr float engine_max_rpm         = 9500.0f;
        constexpr float engine_peak_torque     = 700.0f;
        constexpr float engine_peak_torque_rpm = 6750.0f;
        constexpr float engine_inertia         = 0.25f;
        constexpr float engine_friction        = 0.02f;
        
        // gearbox
        constexpr float gear_ratios[]          = { -3.15f, 0.0f, 3.08f, 2.19f, 1.63f, 1.29f, 1.03f, 0.84f, 0.69f };
        constexpr int   gear_count             = 9;
        constexpr float final_drive            = 4.44f;
        constexpr float shift_up_rpm           = 8500.0f;
        constexpr float shift_down_rpm         = 3500.0f;
        constexpr float shift_time             = 0.05f;
        constexpr float clutch_engagement_rate = 8.0f;
        constexpr float drivetrain_efficiency  = 0.88f;  // ~12% loss through gearbox/diff
        inline bool     manual_transmission    = false;
        
        // brakes
        constexpr float brake_force            = 12000.0f;
        constexpr float brake_bias_front       = 0.65f;
        constexpr float reverse_power_ratio    = 0.5f;
        constexpr float brake_ambient_temp     = 200.0f;   // celsius
        constexpr float brake_optimal_temp     = 400.0f;
        constexpr float brake_fade_temp        = 700.0f;
        constexpr float brake_max_temp         = 900.0f;
        constexpr float brake_heat_coefficient = 0.015f;
        constexpr float brake_cooling_rate     = 50.0f;
        constexpr float brake_cooling_airflow  = 0.8f;
        
        // input
        constexpr float throttle_smoothing     = 10.0f;
        constexpr float steering_smoothing     = 15.0f;
        
        // pacejka tire model
        constexpr float lat_B  = 12.0f;
        constexpr float lat_C  = 1.4f;
        constexpr float lat_D  = 1.0f;
        constexpr float lat_E  = 0.6f;
        constexpr float long_B = 20.0f;
        constexpr float long_C = 1.5f;
        constexpr float long_D = 1.0f;
        constexpr float long_E = -0.5f;
        
        // tire grip
        constexpr float tire_friction        = 1.8f;
        constexpr float min_slip_speed       = 0.5f;
        constexpr float load_sensitivity     = 0.92f;
        constexpr float load_reference       = 4000.0f;
        constexpr float rear_grip_ratio      = 1.0f;
        
        // tire temperature
        constexpr float tire_ambient_temp    = 50.0f;
        constexpr float tire_optimal_temp    = 90.0f;
        constexpr float tire_temp_range      = 50.0f;
        constexpr float tire_heat_from_slip  = 25.0f;
        constexpr float tire_heat_from_rolling = 0.15f;
        constexpr float tire_cooling_rate    = 2.0f;
        constexpr float tire_cooling_airflow = 0.05f;
        constexpr float tire_grip_temp_factor = 0.15f;
        constexpr float tire_min_temp        = 10.0f;
        constexpr float tire_max_temp        = 150.0f;
        constexpr float tire_relaxation_length = 0.3f;
        
        // suspension
        constexpr float front_spring_freq    = 1.5f;
        constexpr float rear_spring_freq     = 1.4f;
        constexpr float damping_ratio        = 0.85f;
        constexpr float damping_bump_ratio   = 0.7f;   // compression (softer)
        constexpr float damping_rebound_ratio = 1.3f;  // extension (stiffer)
        constexpr float front_arb_stiffness  = 3500.0f;
        constexpr float rear_arb_stiffness   = 1500.0f;
        constexpr float max_susp_force       = 35000.0f;
        constexpr float max_damper_velocity  = 5.0f;
        
        // aerodynamics
        constexpr float drag_coeff           = 0.35f;
        constexpr float frontal_area         = 2.2f;
        constexpr float air_density          = 1.225f;
        constexpr float rolling_resistance   = 0.015f;
        constexpr float lift_coeff_front     = -0.3f;
        constexpr float lift_coeff_rear      = -0.4f;
        constexpr float downforce_center_height = 0.3f;
        
        // steering
        constexpr float max_steer_angle            = 0.65f;
        constexpr float high_speed_steer_reduction = 0.4f;
        constexpr float steering_rate              = 1.5f;
        constexpr float pneumatic_trail            = 0.03f;
        constexpr float self_align_gain            = 0.5f;
        
        // alignment (camber/toe in radians)
        constexpr float front_camber = -1.5f * (3.14159265f / 180.0f);  // negative camber
        constexpr float rear_camber  = -1.0f * (3.14159265f / 180.0f);
        constexpr float front_toe    =  0.1f * (3.14159265f / 180.0f);  // slight toe-in
        constexpr float rear_toe     =  0.2f * (3.14159265f / 180.0f);
        
        // wheel
        constexpr float airborne_wheel_decay     = 0.99f;
        constexpr float bearing_friction         = 0.2f;
        constexpr float ground_match_rate        = 8.0f;
        constexpr float handbrake_sliding_factor = 0.75f;
        
        // lsd
        constexpr float lsd_preload         = 100.0f;
        constexpr float lsd_lock_ratio_accel = 0.4f;
        constexpr float lsd_lock_ratio_decel = 0.2f;
        
        // thresholds
        constexpr float input_deadzone          = 0.01f;
        constexpr float steering_deadzone       = 0.001f;
        constexpr float braking_speed_threshold = 3.0f;
        
        // speed limits
        constexpr float max_forward_speed   = 320.0f;
        constexpr float max_reverse_speed   = 80.0f;
        constexpr float max_power_reduction = 0.85f;
        
        // damping
        constexpr float linear_damping  = 0.05f;
        constexpr float angular_damping = 0.5f;
        
        // abs
        inline bool     abs_enabled        = false;
        constexpr float abs_slip_threshold = 0.15f;
        constexpr float abs_release_rate   = 0.7f;
        constexpr float abs_pulse_frequency = 15.0f;
        
        // traction control
        inline bool     tc_enabled        = false;
        constexpr float tc_slip_threshold = 0.08f;
        constexpr float tc_power_reduction = 0.8f;
        constexpr float tc_response_rate  = 15.0f;
        
        // turbo/boost
        inline bool     turbo_enabled         = false;
        constexpr float boost_max_pressure    = 1.2f; // bar
        constexpr float boost_spool_rate      = 3.0f;
        constexpr float boost_wastegate_rpm   = 7500.0f;
        constexpr float boost_torque_mult     = 0.35f;
        constexpr float boost_min_rpm         = 2500.0f;
        
        // surface friction multipliers
        constexpr float surface_friction_asphalt     = 1.0f;
        constexpr float surface_friction_concrete    = 0.95f;
        constexpr float surface_friction_wet_asphalt = 0.7f;
        constexpr float surface_friction_gravel      = 0.6f;
        constexpr float surface_friction_grass       = 0.4f;
        constexpr float surface_friction_ice         = 0.1f;
        
        // debug visualization
        inline bool draw_raycasts   = true; // draw wheel raycast lines
        inline bool draw_suspension = true; // draw suspension travel
    }

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

    // state
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
    inline static float           sprung_mass[wheel_count];
    inline static PxVec3          prev_velocity = PxVec3(0);
    inline static PxVec3          chassis_acceleration = PxVec3(0);
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
    
    // debug visualization data
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

    // helpers
    inline bool  is_front(int i)                { return i == front_left || i == front_right; }
    inline bool  is_rear(int i)                 { return i == rear_left || i == rear_right; }
    inline float lerp(float a, float b, float t){ return a + (b - a) * t; }
    inline float exp_decay(float rate, float dt){ return 1.0f - expf(-rate * dt); }
    
    inline float pacejka(float slip, float B, float C, float D, float E)
    {
        float Bx = B * slip;
        return D * sinf(C * atanf(Bx - E * (Bx - atanf(Bx))));
    }
    
    inline float tire_force_lateral(float slip_angle)
    {
        return pacejka(slip_angle, tuning::lat_B, tuning::lat_C, tuning::lat_D, tuning::lat_E);
    }
    
    inline float tire_force_longitudinal(float slip_ratio)
    {
        return pacejka(slip_ratio, tuning::long_B, tuning::long_C, tuning::long_D, tuning::long_E);
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
        // optimal camber matches slip angle direction for better contact patch
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
        // cold brakes: 85% -> 100%, optimal: 100% -> 60% fade, severe fade: 60%
        if (temp >= tuning::brake_fade_temp) return 0.6f;
        
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
            
            // wastegate limits boost at high rpm
            if (rpm > tuning::boost_wastegate_rpm)
                target *= PxMax(0.0f, 1.0f - (rpm - tuning::boost_wastegate_rpm) / 2000.0f);
        }
        
        // spool up slower than spool down
        float rate = (target > boost_pressure) ? tuning::boost_spool_rate : tuning::boost_spool_rate * 2.0f;
        boost_pressure = lerp(boost_pressure, target, exp_decay(rate, dt));
    }
    
    inline float get_engine_torque(float rpm)
    {
        rpm = PxClamp(rpm, tuning::engine_idle_rpm, tuning::engine_max_rpm);
        
        // piecewise torque curve
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
    
    inline float engine_rpm_to_wheel_rpm(float eng_rpm, int gear)
    {
        if (gear < 0 || gear >= tuning::gear_count || gear == 1)
            return 0.0f;
        float ratio = tuning::gear_ratios[gear] * tuning::final_drive;
        return (fabsf(ratio) < 0.001f) ? 0.0f : eng_rpm / fabsf(ratio);
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
        
        // skip automatic logic if manual transmission
        if (tuning::manual_transmission)
            return;
        
        float speed_kmh = forward_speed * 3.6f;
        
        // reverse engagement
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
            
            // upshift
            float upshift_threshold = get_upshift_speed(current_gear, throttle);
            if (last_shift_direction == -1) upshift_threshold += 10.0f;
            
            if (can_shift && speed_kmh > upshift_threshold && current_gear < 8 && throttle > 0.1f)
            {
                current_gear++;
                is_shifting = true;
                shift_timer = tuning::shift_time;
                last_shift_direction = 1;
                return;
            }
            
            // downshift
            float downshift_threshold = get_downshift_speed(current_gear);
            if (last_shift_direction == 1) downshift_threshold -= 10.0f;
            
            if (can_shift && speed_kmh < downshift_threshold && current_gear > 2)
            {
                current_gear--;
                is_shifting = true;
                shift_timer = tuning::shift_time;
                last_shift_direction = -1;
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
                }
            }
        }
    }
    
    inline const char* get_gear_string()
    {
        static const char* names[] = { "R", "N", "1", "2", "3", "4", "5", "6", "7" };
        return (current_gear >= 0 && current_gear < tuning::gear_count) ? names[current_gear] : "?";
    }
    
    // these are kept for backwards compatibility, prefer the get_current_* versions below
    inline int get_gear()       { return current_gear; }
    inline float get_engine_rpm() { return engine_rpm; }

    inline void compute_constants()
    {
        // wheel positions (40/60 front/rear weight distribution)
        float front_z = cfg.length * 0.35f;
        float rear_z  = -cfg.length * 0.35f;
        float half_w  = cfg.width * 0.5f - cfg.wheel_width * 0.5f;
        float y       = -cfg.suspension_height;
        
        wheel_offsets[front_left]  = PxVec3(-half_w, y, front_z);
        wheel_offsets[front_right] = PxVec3( half_w, y, front_z);
        wheel_offsets[rear_left]   = PxVec3(-half_w, y, rear_z);
        wheel_offsets[rear_right]  = PxVec3( half_w, y, rear_z);
        
        // per-axle sprung mass: 40% front, 60% rear, split per wheel
        float axle_mass[2] = { cfg.mass * 0.40f * 0.5f, cfg.mass * 0.60f * 0.5f };
        float freq[2]      = { tuning::front_spring_freq, tuning::rear_spring_freq };
        
        for (int i = 0; i < wheel_count; i++)
        {
            int axle      = is_front(i) ? 0 : 1;
            float mass    = axle_mass[axle];
            float omega   = 2.0f * PxPi * freq[axle];
            wheel_moi[i]  = cfg.wheel_mass * cfg.wheel_radius * cfg.wheel_radius;
            sprung_mass[i]      = mass;
            spring_stiffness[i] = mass * omega * omega;
            spring_damping[i]   = 2.0f * tuning::damping_ratio * sqrtf(spring_stiffness[i] * mass);
        }
    }
    
    inline bool create(PxPhysics* physics, PxScene* scene, const config* custom_cfg = nullptr)
    {
        if (!physics || !scene)
            return false;
        
        cfg = custom_cfg ? *custom_cfg : config();
        compute_constants();
        
        // reset state
        for (int i = 0; i < wheel_count; i++)
        {
            wheels[i] = wheel();
            abs_active[i] = false;
        }
        input = input_state();
        input_target = input_state();
        prev_velocity = PxVec3(0);
        chassis_acceleration = PxVec3(0);
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
        
        material = physics->createMaterial(0.8f, 0.7f, 0.1f);
        if (!material)
            return false;
        
        // spawn height with suspension sag
        float front_mass_per_wheel = cfg.mass * 0.40f * 0.5f;
        float front_omega = 2.0f * PxPi * tuning::front_spring_freq;
        float front_stiffness = front_mass_per_wheel * front_omega * front_omega;
        float expected_sag = PxClamp((front_mass_per_wheel * 9.81f) / front_stiffness, 0.0f, cfg.suspension_travel * 0.8f);
        float spawn_y = cfg.wheel_radius + cfg.suspension_height + expected_sag;
        
        body = physics->createRigidDynamic(PxTransform(PxVec3(0, spawn_y, 0)));
        if (!body)
        {
            material->release();
            material = nullptr;
            return false;
        }
        
        PxShape* chassis = physics->createShape(
            PxBoxGeometry(cfg.width * 0.5f, cfg.height * 0.5f, cfg.length * 0.5f),
            *material
        );
        if (!chassis)
        {
            body->release();
            body = nullptr;
            material->release();
            material = nullptr;
            return false;
        }
        
        chassis->setFlag(PxShapeFlag::eSCENE_QUERY_SHAPE, false);
        body->attachShape(*chassis);
        chassis->release();
        
        PxRigidBodyExt::setMassAndUpdateInertia(*body, cfg.mass);
        body->setActorFlag(PxActorFlag::eDISABLE_GRAVITY, true);
        body->setRigidBodyFlag(PxRigidBodyFlag::eENABLE_CCD, true);
        body->setLinearDamping(tuning::linear_damping);
        body->setAngularDamping(tuning::angular_damping);
        
        scene->addActor(*body);
        
        SP_LOG_INFO("car created: mass=%.0f kg", cfg.mass);
        return true;
    }
    
    inline void destroy()
    {
        if (body)    { body->release();     body = nullptr; }
        if (material){ material->release(); material = nullptr; }
    }
    
    // removes all existing shapes from the chassis body
    // call this before adding custom convex shapes
    inline void clear_chassis_shapes()
    {
        if (!body)
            return;
            
        PxU32 shape_count = body->getNbShapes();
        if (shape_count == 0)
            return;
            
        std::vector<PxShape*> shapes(shape_count);
        body->getShapes(shapes.data(), shape_count);
        
        for (PxShape* shape : shapes)
        {
            body->detachShape(*shape);
        }
        
        SP_LOG_INFO("cleared %u chassis shapes", shape_count);
    }
    
    // attaches a convex shape to the chassis body with the given local transform
    // returns true if successful
    inline bool attach_chassis_convex_shape(PxConvexMesh* convex_mesh, const PxTransform& local_pose, PxPhysics* physics)
    {
        if (!body || !convex_mesh || !material || !physics)
            return false;
            
        PxConvexMeshGeometry geometry(convex_mesh);
        PxShape* shape = physics->createShape(geometry, *material);
        if (!shape)
            return false;
            
        shape->setLocalPose(local_pose);
        shape->setFlag(PxShapeFlag::eSCENE_QUERY_SHAPE, false);
        shape->setFlag(PxShapeFlag::eVISUALIZATION, true);
        body->attachShape(*shape);
        shape->release(); // body owns the shape now
        
        return true;
    }
    
    // updates mass and inertia after changing chassis shapes
    inline void update_mass_properties()
    {
        if (!body)
            return;
            
        PxRigidBodyExt::setMassAndUpdateInertia(*body, cfg.mass);
    }

    inline void set_throttle(float v)  { input_target.throttle  = PxClamp(v, 0.0f, 1.0f); }
    inline void set_brake(float v)     { input_target.brake     = PxClamp(v, 0.0f, 1.0f); }
    inline void set_steering(float v)  { input_target.steering  = PxClamp(v, -1.0f, 1.0f); }
    inline void set_handbrake(float v) { input_target.handbrake = PxClamp(v, 0.0f, 1.0f); }

    inline void update_input(float dt)
    {
        // steering: rate-limited
        float diff = input_target.steering - input.steering;
        float max_change = tuning::steering_rate * dt;
        input.steering = (fabsf(diff) <= max_change) ? input_target.steering : input.steering + ((diff > 0) ? max_change : -max_change);
        
        // throttle/brake: instant release, smooth press
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
        
        // multi-ray contact patch with wheel curvature
        const int ray_count = 7;
        float half_width = cfg.wheel_width * 0.4f;
        
        auto get_curvature_height = [&](float x_offset) -> float
        {
            float r = cfg.wheel_radius;
            float x = PxMin(fabsf(x_offset), r * 0.95f);
            return r - sqrtf(r * r - x * x);
        };
        
        float dist_near = cfg.wheel_radius * 0.4f;
        float dist_far  = cfg.wheel_radius * 0.75f;
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
                
                // store debug ray origin
                debug_rays[i][r].origin = ray_origin;
                debug_rays[i][r].hit = false;
                
                PxRaycastBuffer hit;
                if (scene->raycast(ray_origin, local_down, ray_len, hit, PxHitFlag::eDEFAULT, filter) &&
                    hit.block.actor && hit.block.actor != body)
                {
                    // store debug ray hit point
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
                    // no hit - show ray extending to max length
                    debug_rays[i][r].hit_point = ray_origin + local_down * ray_len;
                }
            }
            
            // store debug suspension positions
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
            
            // spring-damper wheel travel
            float compression_error = w.target_compression - w.compression;
            float wheel_spring_force = spring_stiffness[i] * compression_error;
            float wheel_damper_force = -spring_damping[i] * w.compression_velocity * 0.5f;
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
            // bump/rebound split - different damping for compression vs extension
            float damper_ratio = (susp_vel > 0.0f) ? tuning::damping_bump_ratio : tuning::damping_rebound_ratio;
            float damper_f = spring_damping[i] * susp_vel * damper_ratio;
            
            forces[i] = PxClamp(spring_f + damper_f, 0.0f, tuning::max_susp_force);
        }
        
        // anti-roll bars
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
    
    inline void update_chassis_acceleration(float dt)
    {
        PxVec3 vel = body->getLinearVelocity();
        if (dt > 0.0f)
            chassis_acceleration = (vel - prev_velocity) / dt;
        prev_velocity = vel;
    }
    
    inline void apply_tire_forces(float wheel_angles[wheel_count], float dt)
    {
        PxTransform pose = body->getGlobalPose();
        PxVec3 chassis_fwd   = pose.q.rotate(PxVec3(0, 0, 1));
        PxVec3 chassis_right = pose.q.rotate(PxVec3(1, 0, 0));
        
        for (int i = 0; i < wheel_count; i++)
        {
            wheel& w = wheels[i];
            
            // airborne wheel
            if (!w.grounded || w.tire_load <= 0.0f)
            {
                w.slip_angle = w.slip_ratio = w.lateral_force = w.longitudinal_force = 0.0f;
                w.angular_velocity = (is_rear(i) && input.handbrake > tuning::input_deadzone)
                    ? 0.0f : w.angular_velocity * tuning::airborne_wheel_decay;
                
                float temp_above = w.temperature - tuning::tire_ambient_temp;
                if (temp_above > 0.0f)
                    w.temperature -= tuning::tire_cooling_rate * 3.0f * (temp_above / 60.0f) * dt;
                w.temperature = PxMax(w.temperature, tuning::tire_ambient_temp);
                w.rotation += w.angular_velocity * dt;
                continue;
            }
            
            // velocities
            PxVec3 world_pos = pose.transform(wheel_offsets[i]);
            PxVec3 vel = body->getLinearVelocity() + body->getAngularVelocity().cross(world_pos - pose.p);
            vel -= w.contact_normal * vel.dot(w.contact_normal);
            
            float cs = cosf(wheel_angles[i]), sn = sinf(wheel_angles[i]);
            PxVec3 wheel_fwd = chassis_fwd * cs + chassis_right * sn;
            PxVec3 wheel_lat = chassis_right * cs - chassis_fwd * sn;
            
            float vx = vel.dot(wheel_fwd);
            float vy = vel.dot(wheel_lat);
            float wheel_speed  = w.angular_velocity * cfg.wheel_radius;
            float ground_speed = sqrtf(vx * vx + vy * vy);
            float max_v = PxMax(fabsf(wheel_speed), fabsf(vx));
            
            // combine all grip factors: base friction, load sensitivity, temperature, camber, surface
            float base_grip = tuning::tire_friction * load_sensitive_grip(PxMax(w.tire_load, 0.0f));
            float temp_factor = get_tire_temp_grip_factor(w.temperature);
            float camber_factor = get_camber_grip_factor(i, w.slip_angle);
            float surface_factor = get_surface_friction(w.contact_surface);
            float peak_force = base_grip * temp_factor * camber_factor * surface_factor;
            
            // tire forces
            float lat_f = 0.0f, long_f = 0.0f;
            
            if (max_v > tuning::min_slip_speed)
            {
                float raw_slip_ratio = PxClamp((wheel_speed - vx) / max_v, -1.0f, 1.0f);
                float raw_slip_angle = atan2f(vy, fabsf(vx));
                
                float blend = exp_decay(ground_speed / tuning::tire_relaxation_length, dt);
                w.slip_ratio = lerp(w.slip_ratio, raw_slip_ratio, blend);
                w.slip_angle = lerp(w.slip_angle, raw_slip_angle, blend);
                
                // combined slip via normalized stiffness
                float norm_lat  = w.slip_angle * tuning::lat_B;
                float norm_long = w.slip_ratio * tuning::long_B;
                float combined  = sqrtf(norm_lat * norm_lat + norm_long * norm_long);
                
                if (combined > 0.001f)
                {
                    float force_mag = pacejka(combined / tuning::lat_B, tuning::lat_B, tuning::lat_C, tuning::lat_D, tuning::lat_E) * peak_force;
                    lat_f  = -force_mag * (norm_lat / combined);
                    long_f =  force_mag * (norm_long / combined);
                    if (is_rear(i)) lat_f *= tuning::rear_grip_ratio;
                }
            }
            else
            {
                // low speed linear model
                w.slip_ratio = w.slip_angle = 0.0f;
                long_f = PxClamp((wheel_speed - vx) / tuning::min_slip_speed, -1.0f, 1.0f) * peak_force;
                lat_f  = PxClamp(-vy / tuning::min_slip_speed, -1.0f, 1.0f) * peak_force;
            }
            
            // tire temperature
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
            
            // handbrake sliding friction
            if (is_rear(i) && input.handbrake > tuning::input_deadzone)
            {
                float sliding_f = tuning::handbrake_sliding_factor * peak_force;
                long_f = (fabsf(vx) > 0.01f) ? ((vx > 0.0f ? -1.0f : 1.0f) * sliding_f * input.handbrake) : 0.0f;
                lat_f *= (1.0f - 0.5f * input.handbrake);
            }
            
            w.lateral_force = lat_f;
            w.longitudinal_force = long_f;
            
            PxRigidBodyExt::addForceAtPos(*body, wheel_lat * lat_f + wheel_fwd * long_f, world_pos, PxForceMode::eFORCE);
            
            // wheel rotation
            if (is_rear(i) && input.handbrake > tuning::input_deadzone)
            {
                w.angular_velocity = 0.0f;
            }
            else
            {
                float target_w = vx / cfg.wheel_radius;
                w.angular_velocity += (-long_f * cfg.wheel_radius / wheel_moi[i]) * dt;
                
                float match_rate = (ground_speed < tuning::min_slip_speed) ? tuning::ground_match_rate * 2.0f : tuning::ground_match_rate;
                bool coasting = input.throttle < 0.01f && input.brake < 0.01f;
                if (coasting || is_front(i) || ground_speed < tuning::min_slip_speed)
                    w.angular_velocity = lerp(w.angular_velocity, target_w, exp_decay(match_rate, dt));
                
                w.angular_velocity *= (1.0f - tuning::bearing_friction * dt);
            }
            w.rotation += w.angular_velocity * dt;
        }
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
        float delta_w = w_right - w_left;
        
        float lock_ratio = (total_torque >= 0.0f) ? tuning::lsd_lock_ratio_accel : tuning::lsd_lock_ratio_decel;
        float lock_torque = PxMin(tuning::lsd_preload + fabsf(delta_w) * lock_ratio * fabsf(total_torque), fabsf(total_torque) * 0.5f);
        
        float bias = (delta_w > 0.0f) ? -1.0f : 1.0f;
        wheels[rear_left].angular_velocity  += (total_torque * 0.5f + bias * lock_torque * 0.5f) / wheel_moi[rear_left] * dt;
        wheels[rear_right].angular_velocity += (total_torque * 0.5f - bias * lock_torque * 0.5f) / wheel_moi[rear_right] * dt;
    }
    
    inline void apply_drivetrain(float forward_speed_kmh, float dt)
    {
        float forward_speed_ms = forward_speed_kmh / 3.6f;
        
        update_automatic_gearbox(dt, input.throttle, forward_speed_ms);
        
        // engine rpm from wheel speed
        float avg_wheel_rpm = (wheels[rear_left].angular_velocity + wheels[rear_right].angular_velocity) * 0.5f * 60.0f / (2.0f * PxPi);
        float wheel_driven_rpm = wheel_rpm_to_engine_rpm(fabsf(avg_wheel_rpm), current_gear);
        
        // clutch logic
        if (is_shifting)                                          clutch = 0.2f;
        else if (current_gear == 1)                               clutch = 0.0f;
        else if (fabsf(forward_speed_ms) < 2.0f && input.throttle > 0.1f) clutch = lerp(clutch, 1.0f, exp_decay(tuning::clutch_engagement_rate, dt));
        else                                                      clutch = 1.0f;
        
        // engine rpm (free_rev_rpm is throttle-controlled rpm when clutch is disengaged)
        float free_rev_rpm = tuning::engine_idle_rpm + input.throttle * (tuning::engine_redline_rpm - tuning::engine_idle_rpm) * 0.7f;
        if (current_gear == 1)
            engine_rpm = lerp(engine_rpm, free_rev_rpm, exp_decay(8.0f, dt));
        else if (clutch < 0.9f)
            engine_rpm = lerp(engine_rpm, lerp(free_rev_rpm, PxMax(wheel_driven_rpm, tuning::engine_idle_rpm), clutch), exp_decay(10.0f, dt));
        else
            engine_rpm = PxMax(wheel_driven_rpm, tuning::engine_idle_rpm);
        engine_rpm = PxClamp(engine_rpm, tuning::engine_idle_rpm, tuning::engine_max_rpm);
        
        // engine braking - apply resistance to rear wheels when coasting in gear
        if (input.throttle < tuning::input_deadzone && clutch > 0.5f && current_gear >= 2)
        {
            float wheel_brake_torque = tuning::engine_friction * engine_rpm * 0.1f * fabsf(tuning::gear_ratios[current_gear]) * tuning::final_drive * 0.5f;
            for (int i = rear_left; i <= rear_right; i++)
                if (wheels[i].angular_velocity > 0.0f)
                    wheels[i].angular_velocity -= wheel_brake_torque / wheel_moi[i] * dt;
        }
        
        // update turbo boost
        update_boost(input.throttle, engine_rpm, dt);
        
        // throttle to rear wheels
        if (input.throttle > tuning::input_deadzone && current_gear >= 2)
        {
            // base engine torque with boost
            float base_torque = get_engine_torque(engine_rpm);
            float boosted_torque = base_torque * (1.0f + boost_pressure * tuning::boost_torque_mult);
            float engine_torque = boosted_torque * input.throttle;
            
            // traction control
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
            // reverse gear throttle acts as brake
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
        
        // braking
        if (input.brake > tuning::input_deadzone)
        {
            if (forward_speed_kmh > tuning::braking_speed_threshold)
            {
                float total_torque = tuning::brake_force * cfg.wheel_radius * input.brake;
                float front_t = total_torque * tuning::brake_bias_front * 0.5f;
                float rear_t = total_torque * (1.0f - tuning::brake_bias_front) * 0.5f;
                
                abs_phase += tuning::abs_pulse_frequency * dt;
                if (abs_phase > 1.0f) abs_phase -= 1.0f;
                
                for (int i = 0; i < wheel_count; i++)
                {
                    float t = is_front(i) ? front_t : rear_t;
                    
                    // brake temperature and fade
                    float brake_efficiency = get_brake_efficiency(wheels[i].brake_temp);
                    t *= brake_efficiency;
                    
                    // heat brakes based on energy dissipation
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
    }
    
    inline void apply_aero_and_resistance()
    {
        PxTransform pose = body->getGlobalPose();
        PxVec3 vel = body->getLinearVelocity();
        float speed = vel.magnitude();
        
        // drag
        if (speed > 1.0f)
            body->addForce(-vel.getNormalized() * 0.5f * tuning::air_density * tuning::drag_coeff * tuning::frontal_area * speed * speed, PxForceMode::eFORCE);
        
        // downforce
        if (speed > 10.0f)
        {
            float dyn_pressure = 0.5f * tuning::air_density * speed * speed * tuning::frontal_area;
            PxVec3 local_up = pose.q.rotate(PxVec3(0, 1, 0));
            PxVec3 front_pos = pose.p + pose.q.rotate(PxVec3(0, tuning::downforce_center_height, cfg.length * 0.35f));
            PxVec3 rear_pos  = pose.p + pose.q.rotate(PxVec3(0, tuning::downforce_center_height, -cfg.length * 0.35f));
            
            PxRigidBodyExt::addForceAtPos(*body, local_up * tuning::lift_coeff_front * dyn_pressure, front_pos, PxForceMode::eFORCE);
            PxRigidBodyExt::addForceAtPos(*body, local_up * tuning::lift_coeff_rear * dyn_pressure, rear_pos, PxForceMode::eFORCE);
        }
        
        // rolling resistance
        float tire_load = 0.0f;
        for (int i = 0; i < wheel_count; i++)
            if (wheels[i].grounded) tire_load += wheels[i].tire_load;
        
        if (speed > 0.1f && tire_load > 0.0f)
            body->addForce(-vel.getNormalized() * tuning::rolling_resistance * tire_load, PxForceMode::eFORCE);
    }
    
    inline void calculate_steering(float forward_speed, float speed_kmh, float out_angles[wheel_count])
    {
        float reduction = (speed_kmh > 80.0f)
            ? 1.0f - tuning::high_speed_steer_reduction * PxClamp((speed_kmh - 80.0f) / 120.0f, 0.0f, 1.0f)
            : 1.0f;
        
        float base = input.steering * tuning::max_steer_angle * reduction;
        
        // rear toe (positive = toe-in, adds stability)
        out_angles[rear_left]  = tuning::rear_toe;
        out_angles[rear_right] = -tuning::rear_toe;
        
        if (fabsf(base) < tuning::steering_deadzone)
        {
            // front toe when not steering
            out_angles[front_left]  = tuning::front_toe;
            out_angles[front_right] = -tuning::front_toe;
            return;
        }
        
        // ackermann geometry
        if (forward_speed >= 0.0f)
        {
            float wheelbase = cfg.length * 0.7f;
            float half_track = (cfg.width - cfg.wheel_width) * 0.5f;
            float turn_r = wheelbase / tanf(fabsf(base));
            
            float inner = atanf(wheelbase / PxMax(turn_r - half_track, 0.1f));
            float outer = atanf(wheelbase / PxMax(turn_r + half_track, 0.1f));
            
            // add toe to steering angles
            if (base > 0.0f)
            {
                out_angles[front_right] = inner + tuning::front_toe;
                out_angles[front_left]  = outer - tuning::front_toe;
            }
            else
            {
                out_angles[front_left]  = -inner + tuning::front_toe;
                out_angles[front_right] = -outer - tuning::front_toe;
            }
        }
        else
        {
            out_angles[front_left]  = base + tuning::front_toe;
            out_angles[front_right] = base - tuning::front_toe;
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
        
        // cool brakes (airflow + ambient)
        for (int i = 0; i < wheel_count; i++)
        {
            float cooling = tuning::brake_cooling_rate + vel.magnitude() * tuning::brake_cooling_airflow;
            float temp_above_ambient = wheels[i].brake_temp - tuning::brake_ambient_temp;
            if (temp_above_ambient > 0.0f)
            {
                wheels[i].brake_temp -= cooling * (temp_above_ambient / 200.0f) * dt;
                wheels[i].brake_temp = PxMax(wheels[i].brake_temp, tuning::brake_ambient_temp);
            }
        }
        
        update_chassis_acceleration(dt);
        
        float wheel_angles[wheel_count];
        calculate_steering(forward_speed, speed_kmh, wheel_angles);
        
        apply_drivetrain(forward_speed * 3.6f, dt);
        update_suspension(scene, dt);
        apply_suspension_forces(dt);
        apply_tire_forces(wheel_angles, dt);
        apply_self_aligning_torque();
        apply_aero_and_resistance();
        
        body->addForce(PxVec3(0, -9.81f * cfg.mass, 0), PxForceMode::eFORCE);
    }

    // accessors
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
    
    // wheel property accessors - return 0/false for invalid indices
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
    
    // note: load transfer is implicitly handled via suspension compression affecting tire_load
    
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
    
    // manual transmission
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
    
    // aliases for get_gear() and get_engine_rpm()
    inline int         get_current_gear()          { return current_gear; }
    inline const char* get_current_gear_string()   { return get_gear_string(); }
    inline float       get_current_engine_rpm()    { return engine_rpm; }
    inline bool        get_is_shifting()           { return is_shifting; }
    inline float       get_clutch()                { return clutch; }
    inline float       get_engine_torque_current() { return get_engine_torque(engine_rpm) * (1.0f + boost_pressure * tuning::boost_torque_mult); }
    inline float       get_redline_rpm()           { return tuning::engine_redline_rpm; }
    inline float       get_max_rpm()               { return tuning::engine_max_rpm; }
    inline float       get_idle_rpm()              { return tuning::engine_idle_rpm; }
    
    // turbo/boost
    inline void  set_turbo_enabled(bool enabled) { tuning::turbo_enabled = enabled; }
    inline bool  get_turbo_enabled()             { return tuning::turbo_enabled; }
    inline float get_boost_pressure()            { return boost_pressure; }
    inline float get_boost_max_pressure()        { return tuning::boost_max_pressure; }
    
    // brake temperature
    inline float get_wheel_brake_temp(int i)       { return is_valid_wheel(i) ? wheels[i].brake_temp : 0.0f; }
    inline float get_wheel_brake_efficiency(int i) { return is_valid_wheel(i) ? get_brake_efficiency(wheels[i].brake_temp) : 1.0f; }
    
    // surface type
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
    
    // alignment getters
    inline float get_front_camber() { return tuning::front_camber; }
    inline float get_rear_camber()  { return tuning::rear_camber; }
    inline float get_front_toe()    { return tuning::front_toe; }
    inline float get_rear_toe()     { return tuning::rear_toe; }
    
    // wheel offset setters (for syncing with actual wheel mesh positions)
    inline void set_wheel_offset(int wheel, float x, float z)
    {
        if (wheel >= 0 && wheel < wheel_count)
        {
            wheel_offsets[wheel].x = x;
            wheel_offsets[wheel].z = z;
            // y stays as -suspension_height (set in compute_constants)
        }
    }
    
    inline PxVec3 get_wheel_offset(int wheel)
    {
        if (wheel >= 0 && wheel < wheel_count)
            return wheel_offsets[wheel];
        return PxVec3(0);
    }
    
    // debug visualization
    inline void set_draw_raycasts(bool enabled)   { tuning::draw_raycasts = enabled; }
    inline bool get_draw_raycasts()               { return tuning::draw_raycasts; }
    inline void set_draw_suspension(bool enabled) { tuning::draw_suspension = enabled; }
    inline bool get_draw_suspension()             { return tuning::draw_suspension; }
    
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
}
