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
#include "../Logging/Log.h"
//======================================

namespace car
{
    using namespace physx;

    namespace tuning
    {
        // engine/brakes
        constexpr float engine_force               = 18000.0f;
        constexpr float brake_force                = 12000.0f;
        constexpr float brake_bias_front           = 0.65f;
        constexpr float reverse_power_ratio        = 0.5f;
        
        // input response
        constexpr float throttle_smoothing         = 10.0f;
        constexpr float steering_smoothing         = 15.0f;
        
        // tires - pacejka (Unchanged)
        constexpr float lat_B                      = 12.0f;
        constexpr float lat_C                      = 1.4f;
        constexpr float lat_D                      = 1.0f;
        constexpr float lat_E                      = 0.6f;
        constexpr float long_B                     = 20.0f;
        constexpr float long_C                     = 1.5f;
        constexpr float long_D                     = 1.0f;
        constexpr float long_E                     = -0.5f;
        
        // tire grip
        constexpr float tire_friction              = 1.5f;
        constexpr float min_slip_speed             = 0.5f;
        constexpr float load_sensitivity           = 0.92f;
        constexpr float load_reference             = 4000.0f;
        constexpr float rear_grip_ratio            = 1.0f;
        
        // tire temperature (Unchanged)
        constexpr float tire_ambient_temp          = 50.0f;
        constexpr float tire_optimal_temp          = 90.0f;
        constexpr float tire_temp_range            = 50.0f;
        constexpr float tire_heat_from_slip        = 25.0f;
        constexpr float tire_heat_from_rolling     = 0.15f;
        constexpr float tire_cooling_rate          = 2.0f;
        constexpr float tire_cooling_airflow       = 0.05f;
        constexpr float tire_grip_temp_factor      = 0.15f;
        constexpr float tire_min_temp              = 10.0f;
        constexpr float tire_max_temp              = 150.0f;
        constexpr float tire_relaxation_length     = 0.3f;
        
        // suspension
        constexpr float front_spring_freq          = 1.6f;
        constexpr float rear_spring_freq           = 1.5f;
        constexpr float damping_ratio              = 0.75f;
        
        constexpr float front_arb_stiffness        = 3500.0f;
        constexpr float rear_arb_stiffness         = 1500.0f;
        
        constexpr float max_susp_force             = 35000.0f;
        constexpr float max_damper_velocity        = 5.0f;
        
        // aerodynamics
        constexpr float drag_coeff                 = 0.35f;
        constexpr float frontal_area               = 2.2f;
        constexpr float air_density                = 1.225f;
        constexpr float rolling_resistance         = 0.015f;
        constexpr float lift_coeff_front           = -0.3f;
        constexpr float lift_coeff_rear            = -0.4f;
        constexpr float downforce_center_height    = 0.3f;
        
        // steering
        constexpr float max_steer_angle            = 0.65f;
        constexpr float high_speed_steer_reduction = 0.4f;
        constexpr float pneumatic_trail            = 0.03f;
        constexpr float self_align_gain            = 0.5f;
        
        // wheel behavior
        constexpr float airborne_wheel_decay       = 0.99f;
        constexpr float bearing_friction           = 0.2f;
        constexpr float ground_match_rate          = 8.0f;
        constexpr float handbrake_sliding_factor   = 0.75f;
        
        // limited slip differential
        constexpr float lsd_preload                = 100.0f;
        constexpr float lsd_lock_ratio_accel       = 0.4f;
        constexpr float lsd_lock_ratio_decel       = 0.2f;
        
        // thresholds
        constexpr float input_deadzone             = 0.01f;
        constexpr float steering_deadzone          = 0.001f;
        constexpr float braking_speed_threshold    = 3.0f;
        
        // speed limits
        constexpr float max_forward_speed          = 320.0f;
        constexpr float max_reverse_speed          = 80.0f;
        constexpr float max_power_reduction        = 0.85f;
        
        // damping
        constexpr float linear_damping             = 0.05f;
        constexpr float angular_damping            = 0.5f;
    }

    enum wheel_id { front_left = 0, front_right = 1, rear_left = 2, rear_right = 3, wheel_count = 4 };
    
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
        float  compression         = 0.0f;         // actual wheel position (smoothed)
        float  target_compression  = 0.0f;         // where ground wants wheel to be
        float  prev_compression    = 0.0f;
        float  compression_velocity= 0.0f;         // wheel vertical velocity for smooth travel
        bool   grounded            = false;
        PxVec3 contact_point       = PxVec3(0);
        PxVec3 contact_normal      = PxVec3(0, 1, 0);
        float  angular_velocity    = 0.0f;
        float  rotation            = 0.0f;
        float  tire_load           = 0.0f;
        float  slip_angle          = 0.0f;
        float  slip_ratio          = 0.0f;
        float  lateral_force       = 0.0f;
        float  longitudinal_force  = 0.0f;
        float  temperature         = tuning::tire_ambient_temp;
    };
    
    struct input_state
    {
        float throttle  = 0.0f;
        float brake     = 0.0f;
        float steering  = 0.0f;
        float handbrake = 0.0f;
    };

    // vehicle state
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

    inline void compute_constants()
    {
        // wheel positions (40% front, 60% rear weight distribution)
        float front_z = cfg.length * 0.35f;
        float rear_z  = -cfg.length * 0.35f;
        float half_w  = cfg.width * 0.5f - cfg.wheel_width * 0.5f;
        float y       = -cfg.suspension_height;
        
        wheel_offsets[front_left]  = PxVec3(-half_w, y, front_z);
        wheel_offsets[front_right] = PxVec3( half_w, y, front_z);
        wheel_offsets[rear_left]   = PxVec3(-half_w, y, rear_z);
        wheel_offsets[rear_right]  = PxVec3( half_w, y, rear_z);
        
        float front_mass = cfg.mass * 0.40f * 0.5f;
        float rear_mass  = cfg.mass * 0.60f * 0.5f;
        
        for (int i = 0; i < wheel_count; i++)
        {
            wheel_moi[i] = 0.8f * cfg.wheel_mass * cfg.wheel_radius * cfg.wheel_radius;
            
            float freq  = is_front(i) ? tuning::front_spring_freq : tuning::rear_spring_freq;
            float mass  = is_front(i) ? front_mass : rear_mass;
            float omega = 2.0f * PxPi * freq;
            
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
            wheels[i] = wheel();
        input = input_state();
        input_target = input_state();
        prev_velocity = PxVec3(0);
        chassis_acceleration = PxVec3(0);
        
        material = physics->createMaterial(0.8f, 0.7f, 0.1f);
        if (!material)
            return false;
        
        // spawn height accounting for suspension sag
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
        
        // chassis collision
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

    // input
    inline void set_throttle(float v)  { input_target.throttle  = PxClamp(v, 0.0f, 1.0f); }
    inline void set_brake(float v)     { input_target.brake     = PxClamp(v, 0.0f, 1.0f); }
    inline void set_steering(float v)  { input_target.steering  = PxClamp(v, -1.0f, 1.0f); }
    inline void set_handbrake(float v) { input_target.handbrake = PxClamp(v, 0.0f, 1.0f); }

    inline void update_input(float dt)
    {
        // steering: smooth
        input.steering = lerp(input.steering, input_target.steering, exp_decay(tuning::steering_smoothing, dt));
        
        // throttle/brake: instant release, smooth press
        if (input_target.throttle < input.throttle)
            input.throttle = input_target.throttle;
        else
            input.throttle = lerp(input.throttle, input_target.throttle, exp_decay(tuning::throttle_smoothing, dt));
        
        if (input_target.brake < input.brake)
            input.brake = input_target.brake;
        else
            input.brake = lerp(input.brake, input_target.brake, exp_decay(tuning::throttle_smoothing, dt));
        
        // handbrake: instant
        input.handbrake = input_target.handbrake;
    }
    
    inline void update_suspension(PxScene* scene, float dt)
    {
        PxTransform pose = body->getGlobalPose();
        PxVec3 local_down  = pose.q.rotate(PxVec3(0, -1, 0));
        PxVec3 local_fwd   = pose.q.rotate(PxVec3(0, 0, 1));
        PxVec3 local_right = pose.q.rotate(PxVec3(1, 0, 0));
        
        // multi-ray contact patch with wheel curvature
        // rays at front/back of wheel start higher to match circular profile
        const int ray_count = 7;
        float half_width = cfg.wheel_width * 0.4f;
        
        // sample points along the wheel's circular profile
        // at distance x from center, wheel surface height = radius - sqrt(radius² - x²)
        auto get_curvature_height = [&](float x_offset) -> float
        {
            float r = cfg.wheel_radius;
            float x = PxMin(fabsf(x_offset), r * 0.95f); // clamp to avoid sqrt of negative
            return r - sqrtf(r * r - x * x);
        };
        
        // sample at 3 distances: center, mid-radius, and near edge
        float dist_near = cfg.wheel_radius * 0.4f;
        float dist_far  = cfg.wheel_radius * 0.75f;
        float height_near = get_curvature_height(dist_near);
        float height_far  = get_curvature_height(dist_far);
        
        // ray offsets: (forward, sideways, height due to curvature)
        PxVec3 ray_offsets[ray_count] = {
            PxVec3(0.0f,       0.0f,        0.0f),         // center bottom
            PxVec3(dist_near,  0.0f,        height_near),  // front near
            PxVec3(dist_far,   0.0f,        height_far),   // front far (higher up the curve)
            PxVec3(-dist_near, 0.0f,        height_near),  // back near
            PxVec3(-dist_far,  0.0f,        height_far),   // back far
            PxVec3(0.0f,       -half_width, 0.0f),         // left
            PxVec3(0.0f,        half_width, 0.0f)          // right
        };
        
        PxQueryFilterData filter;
        filter.flags = PxQueryFlag::eSTATIC | PxQueryFlag::eDYNAMIC;
        
        float max_curvature_height = height_far; // highest ray offset
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
                // apply horizontal offset and raise ray origin by curvature height
                PxVec3 offset = local_fwd * ray_offsets[r].x + local_right * ray_offsets[r].y - local_down * ray_offsets[r].z;
                PxVec3 ray_origin = world_attach + offset;
                
                PxRaycastBuffer hit;
                if (scene->raycast(ray_origin, local_down, ray_len, hit, PxHitFlag::eDEFAULT, filter) &&
                    hit.block.actor && hit.block.actor != body)
                {
                    // adjust hit distance by curvature offset for fair comparison
                    // rays starting higher have farther to travel to reach the same ground
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
            }
            
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
            
            // simulate wheel travel with spring-damper dynamics for smooth motion
            // this prevents the wheel from snapping to new heights instantly
            float compression_error = w.target_compression - w.compression;
            
            // spring force pushes wheel toward target, damper resists velocity
            float wheel_spring_force = spring_stiffness[i] * compression_error;
            float wheel_damper_force = -spring_damping[i] * w.compression_velocity * 0.5f;
            
            // acceleration = force / unsprung mass (wheel mass)
            float wheel_accel = (wheel_spring_force + wheel_damper_force) / cfg.wheel_mass;
            
            // integrate velocity and position
            w.compression_velocity += wheel_accel * dt;
            w.compression += w.compression_velocity * dt;
            
            // clamp to valid range and handle bottoming out
            if (w.compression > 1.0f)
            {
                w.compression = 1.0f;
                w.compression_velocity = PxMin(w.compression_velocity, 0.0f);
            }
            else if (w.compression < 0.0f)
            {
                w.compression = 0.0f;
                w.compression_velocity = PxMax(w.compression_velocity, 0.0f);
            }
        }
    }
    
    inline void apply_suspension_forces(float dt)
    {
        PxTransform pose = body->getGlobalPose();
        float forces[wheel_count];
        
        // spring + damper
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
            
            // use tracked compression velocity for damping (more accurate than finite difference)
            float susp_vel = w.compression_velocity * cfg.suspension_travel;
            susp_vel = PxClamp(susp_vel, -tuning::max_damper_velocity, tuning::max_damper_velocity);
            float damper_f = spring_damping[i] * susp_vel;
            
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
        
        // apply forces at hardpoints
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
        // track acceleration for telemetry (load transfer handled naturally by suspension)
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
            
            // airborne
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
            
            // grip capacity
            float peak_force = tuning::tire_friction * load_sensitive_grip(PxMax(w.tire_load, 0.0f)) * get_tire_temp_grip_factor(w.temperature);
            
            // tire forces: combined slip pacejka at speed, linear model at low speed
            float lat_f = 0.0f, long_f = 0.0f;
            
            if (max_v > tuning::min_slip_speed)
            {
                float raw_slip_ratio = PxClamp((wheel_speed - vx) / max_v, -1.0f, 1.0f);
                float raw_slip_angle = atan2f(vy, fabsf(vx));
                
                float blend = exp_decay(ground_speed / tuning::tire_relaxation_length, dt);
                w.slip_ratio = lerp(w.slip_ratio, raw_slip_ratio, blend);
                w.slip_angle = lerp(w.slip_angle, raw_slip_angle, blend);
                
                // normalize slips by stiffness (B) to combine them
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
            
            // temperature - normalize friction work by reference load to get sensible heating rates
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
            
            // handbrake: sliding friction on rear
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
        float lock_torque = tuning::lsd_preload + fabsf(delta_w) * lock_ratio * fabsf(total_torque);
        lock_torque = PxMin(lock_torque, fabsf(total_torque) * 0.5f);
        
        float bias = (delta_w > 0.0f) ? -1.0f : 1.0f;
        wheels[rear_left].angular_velocity  += (total_torque * 0.5f + bias * lock_torque * 0.5f) / wheel_moi[rear_left] * dt;
        wheels[rear_right].angular_velocity += (total_torque * 0.5f - bias * lock_torque * 0.5f) / wheel_moi[rear_right] * dt;
    }
    
    inline void apply_drivetrain(float forward_speed_kmh, float dt)
    {
        // throttle -> rear via lsd
        if (input.throttle > tuning::input_deadzone)
        {
            float speed_kmh = body->getLinearVelocity().magnitude() * 3.6f;
            float power = 1.0f - PxClamp(speed_kmh / tuning::max_forward_speed, 0.0f, tuning::max_power_reduction);
            apply_lsd_torque(tuning::engine_force * cfg.wheel_radius * input.throttle * power, dt);
        }
        
        // brake or reverse
        if (input.brake > tuning::input_deadzone)
        {
            if (forward_speed_kmh > tuning::braking_speed_threshold)
            {
                float total_torque = tuning::brake_force * cfg.wheel_radius * input.brake;
                float front_t = total_torque * tuning::brake_bias_front * 0.5f;
                float rear_t = total_torque * (1.0f - tuning::brake_bias_front) * 0.5f;
                
                for (int i = 0; i < wheel_count; i++)
                {
                    float t = is_front(i) ? front_t : rear_t;
                    float sign = wheels[i].angular_velocity >= 0.0f ? -1.0f : 1.0f;
                    float new_w = wheels[i].angular_velocity + sign * t / wheel_moi[i] * dt;
                    
                    // prevent sign reversal
                    if ((wheels[i].angular_velocity > 0 && new_w < 0) || (wheels[i].angular_velocity < 0 && new_w > 0))
                        wheels[i].angular_velocity = 0.0f;
                    else
                        wheels[i].angular_velocity = new_w;
                }
            }
            else
            {
                // reverse
                float rev_speed = PxMax(-forward_speed_kmh, 0.0f);
                float power = 1.0f - PxClamp(rev_speed / tuning::max_reverse_speed, 0.0f, tuning::max_power_reduction);
                apply_lsd_torque(-tuning::engine_force * tuning::reverse_power_ratio * cfg.wheel_radius * input.brake * power, dt);
            }
        }
        
        // handbrake locks rear
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
        // speed-dependent reduction
        float reduction = 1.0f;
        if (speed_kmh > 80.0f)
            reduction = 1.0f - tuning::high_speed_steer_reduction * PxClamp((speed_kmh - 80.0f) / 120.0f, 0.0f, 1.0f);
        
        float base = input.steering * tuning::max_steer_angle * reduction;
        out_angles[rear_left] = out_angles[rear_right] = 0.0f;
        
        if (fabsf(base) < tuning::steering_deadzone)
        {
            out_angles[front_left] = out_angles[front_right] = 0.0f;
            return;
        }
        
        // ackermann geometry (forward only)
        if (forward_speed >= 0.0f)
        {
            float wheelbase = cfg.length * 0.7f;
            float half_track = (cfg.width - cfg.wheel_width) * 0.5f;
            float turn_r = wheelbase / tanf(fabsf(base));
            
            float inner = atanf(wheelbase / PxMax(turn_r - half_track, 0.1f));
            float outer = atanf(wheelbase / PxMax(turn_r + half_track, 0.1f));
            
            if (base > 0.0f) { out_angles[front_right] = inner; out_angles[front_left] = outer; }
            else             { out_angles[front_left] = -inner; out_angles[front_right] = -outer; }
        }
        else
        {
            out_angles[front_left] = out_angles[front_right] = base;
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
    
    inline float get_wheel_compression(int i)       { return is_valid_wheel(i) ? wheels[i].compression : 0.0f; }
    inline bool  is_wheel_grounded(int i)           { return is_valid_wheel(i) && wheels[i].grounded; }
    inline float get_wheel_slip_angle(int i)        { return is_valid_wheel(i) ? wheels[i].slip_angle : 0.0f; }
    inline float get_wheel_slip_ratio(int i)        { return is_valid_wheel(i) ? wheels[i].slip_ratio : 0.0f; }
    inline float get_wheel_tire_load(int i)         { return is_valid_wheel(i) ? wheels[i].tire_load : 0.0f; }
    inline float get_wheel_lateral_force(int i)     { return is_valid_wheel(i) ? wheels[i].lateral_force : 0.0f; }
    inline float get_wheel_longitudinal_force(int i){ return is_valid_wheel(i) ? wheels[i].longitudinal_force : 0.0f; }
    inline float get_wheel_angular_velocity(int i)  { return is_valid_wheel(i) ? wheels[i].angular_velocity : 0.0f; }
    inline float get_wheel_rotation(int i)          { return is_valid_wheel(i) ? wheels[i].rotation : 0.0f; }
    inline float get_wheel_temperature(int i)       { return is_valid_wheel(i) ? wheels[i].temperature : 0.0f; }
    
    inline float get_wheel_suspension_force(int i)
    {
        if (!is_valid_wheel(i) || !wheels[i].grounded) return 0.0f;
        return spring_stiffness[i] * wheels[i].compression * cfg.suspension_travel;
    }
    
    inline float get_wheel_load_transfer(int i)     { return 0.0f; } // handled naturally by suspension
    inline float get_wheel_effective_load(int i)    { return is_valid_wheel(i) ? wheels[i].tire_load : 0.0f; }
    
    inline float get_wheel_temp_grip_factor(int i)
    {
        return is_valid_wheel(i) ? get_tire_temp_grip_factor(wheels[i].temperature) : 1.0f;
    }
    
    inline float get_chassis_visual_offset_y()
    {
        const float offset = 0.1f; // this is just get the mesh to look right on top of the wheels
        return -(cfg.height * 0.5f + cfg.suspension_height) + offset;
    }
}
