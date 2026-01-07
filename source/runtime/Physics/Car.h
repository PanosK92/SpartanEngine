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

    //===================================================================================
    // tuning constants
    //===================================================================================
    
    namespace tuning
    {
        // engine/brakes
        constexpr float engine_force             = 55000.0f;  // newtons (~950hp supercar)
        constexpr float brake_force              = 12000.0f;  // newtons per wheel
        constexpr float brake_bias_front         = 0.6f;      // 60% front, 40% rear
        constexpr float reverse_power_ratio      = 0.8f;      // reverse gets 80% of forward power
        
        // input response
        constexpr float throttle_smoothing       = 10.0f;
        constexpr float steering_smoothing       = 20.0f;
        
        // tires
        constexpr float tire_friction            = 1.4f;      // high-performance slicks (street tires ~1.0, race slicks ~1.5)
        constexpr float peak_slip_angle          = 0.14f;     // ~8 degrees
        constexpr float peak_slip_ratio          = 0.14f;     // ~14%, more forgiving before breaking loose
        constexpr float sliding_friction         = 0.92f;     // less grip loss when sliding (was 0.85)
        constexpr float min_slip_speed           = 0.5f;      // m/s threshold for slip calcs
        constexpr float slip_decay_rate          = 0.3f;      // slower grip drop-off past peak (was 0.5)
        
        // suspension
        constexpr float front_spring_freq        = 1.6f;      // hz (soft but not bottoming out)
        constexpr float rear_spring_freq         = 1.8f;      // hz (soft but not bottoming out)
        constexpr float damping_ratio            = 0.50f;     // moderate bounce
        constexpr float front_arb_stiffness      = 3000.0f;   // n/m, soft = more grip
        constexpr float rear_arb_stiffness       = 8000.0f;   // n/m, reduced for balance
        constexpr float max_susp_force           = 35000.0f;  // prevents explosive forces
        constexpr float max_damper_velocity      = 5.0f;      // m/s clamp for damper calc
        
        // aerodynamics
        constexpr float drag_coeff               = 0.35f;
        constexpr float frontal_area             = 2.2f;      // m²
        constexpr float air_density              = 1.225f;    // kg/m³
        constexpr float rolling_resistance       = 0.015f;
        
        // steering
        constexpr float max_steer_angle          = 0.698f;    // ~40 degrees in radians
        constexpr float high_speed_steer_reduction = 0.3f;
        
        // wheel behavior
        constexpr float airborne_wheel_decay     = 0.99f;     // angular velocity decay when airborne
        constexpr float bearing_friction         = 0.2f;      // per-second friction factor
        constexpr float ground_match_rate        = 8.0f;      // rate for matching ground speed
        constexpr float handbrake_sliding_factor = 0.75f;     // sliding friction multiplier for handbrake
        
        // thresholds
        constexpr float input_deadzone           = 0.01f;     // minimum input to register
        constexpr float steering_deadzone        = 0.001f;    // minimum steering angle
        constexpr float braking_speed_threshold  = 3.0f;      // km/h, below this we reverse
        
        // speed limits
        constexpr float max_forward_speed        = 250.0f;    // km/h
        constexpr float max_reverse_speed        = 80.0f;     // km/h
        constexpr float max_power_reduction      = 0.85f;     // max power reduction at top speed
        
        // damping
        constexpr float linear_damping           = 0.05f;
        constexpr float angular_damping          = 0.05f;
        
        // lateral force reduction at extreme slip
        constexpr float extreme_slip_threshold   = 0.25f;     // fraction of pi
        constexpr float extreme_slip_reduction   = 0.5f;      // max reduction factor
    }

    //===================================================================================
    // data types
    //===================================================================================
    
    enum wheel_id { front_left = 0, front_right = 1, rear_left = 2, rear_right = 3, wheel_count = 4 };
    
    struct config
    {
        float length           = 4.5f;
        float width            = 2.0f;
        float height           = 0.5f;
        float mass             = 1500.0f;
        float wheel_radius     = 0.35f;
        float wheel_width      = 0.25f;
        float wheel_mass       = 20.0f;
        float suspension_travel= 0.20f;   // increased for visible movement
        float suspension_height= 0.35f;
    };
    
    struct wheel
    {
        // suspension
        float compression      = 0.0f;
        float prev_compression = 0.0f;
        bool  grounded         = false;
PxVec3 contact_point   = PxVec3(0);
PxVec3 contact_normal  = PxVec3(0, 1, 0);
        
        // dynamics
        float angular_velocity = 0.0f;
        float rotation         = 0.0f;
        float tire_load        = 0.0f;
        
        // slip
        float slip_angle       = 0.0f;
        float slip_ratio       = 0.0f;
        float lateral_force    = 0.0f;
        float longitudinal_force = 0.0f;
    };
    
    struct input_state
    {
        float throttle  = 0.0f;
        float brake     = 0.0f;
        float steering  = 0.0f;
        float handbrake = 0.0f;
    };

    //===================================================================================
    // vehicle state (single active vehicle)
    //===================================================================================
    
    inline static PxRigidDynamic* body        = nullptr;
    inline static PxMaterial*     material    = nullptr;
    inline static config                 cfg;
    inline static wheel                  wheels[wheel_count];
    inline static input_state            input;
    inline static input_state            input_target;
    inline static PxVec3          wheel_offsets[wheel_count];
    
    // precomputed per-wheel constants
    inline static float wheel_moi[wheel_count];
    inline static float spring_stiffness[wheel_count];
    inline static float spring_damping[wheel_count];
    inline static float sprung_mass[wheel_count];

    //===================================================================================
    // helpers
    //===================================================================================
    
    inline bool is_front(int i) { return i == front_left || i == front_right; }
    inline bool is_rear(int i)  { return i == rear_left || i == rear_right; }
    
    inline float lerp(float a, float b, float t) { return a + (b - a) * t; }
    inline float exp_decay(float rate, float dt) { return 1.0f - expf(-rate * dt); }
    
    inline float tire_force_curve(float slip, float peak_slip)
    {
        // simplified pacejka: linear rise to peak, then exponential decay to sliding friction
        float abs_slip = fabsf(slip);
        float sign = slip >= 0.0f ? 1.0f : -1.0f;
        
        if (abs_slip < peak_slip)
            return sign * (abs_slip / peak_slip);
        
        float excess = abs_slip - peak_slip;
        float decay = expf(-excess * tuning::slip_decay_rate);
        return sign * (tuning::sliding_friction + (1.0f - tuning::sliding_friction) * decay);
    }

    //===================================================================================
    // setup / teardown
    //===================================================================================
    
    inline void compute_constants()
    {
        // wheel positions relative to chassis center
        float front_z = cfg.length * 0.35f;
        float rear_z  = -cfg.length * 0.35f;
        float half_w  = cfg.width * 0.5f - cfg.wheel_width * 0.5f;
        float y       = -cfg.suspension_height;
        
        wheel_offsets[front_left]  = PxVec3(-half_w, y, front_z);
        wheel_offsets[front_right] = PxVec3( half_w, y, front_z);
        wheel_offsets[rear_left]   = PxVec3(-half_w, y, rear_z);
        wheel_offsets[rear_right]  = PxVec3( half_w, y, rear_z);
        
        // weight distribution: 40% front, 60% rear (mid-engine supercar)
        float front_mass = cfg.mass * 0.40f * 0.5f;
        float rear_mass  = cfg.mass * 0.60f * 0.5f;
        
        for (int i = 0; i < wheel_count; i++)
        {
            // wheel moment of inertia (ring-like distribution)
            wheel_moi[i] = 0.8f * cfg.wheel_mass * cfg.wheel_radius * cfg.wheel_radius;
            
            // spring stiffness from natural frequency: k = m * (2*pi*f)^2
            float freq = is_front(i) ? tuning::front_spring_freq : tuning::rear_spring_freq;
            float mass = is_front(i) ? front_mass : rear_mass;
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
        
        // reset wheel states
        for (int i = 0; i < wheel_count; i++)
            wheels[i] = wheel();
        input = input_state();
        input_target = input_state();
        
        // create physics material
        material = physics->createMaterial(0.8f, 0.7f, 0.1f);
        if (!material)
            return false;
        
        // spawn position accounting for suspension sag
        // estimate sag from spring stiffness: sag = (weight per wheel) / stiffness
        // use front wheel since it typically has lower stiffness (more sag)
        float front_mass_per_wheel = cfg.mass * 0.40f * 0.5f;
        float front_omega = 2.0f * PxPi * tuning::front_spring_freq;
        float front_stiffness = front_mass_per_wheel * front_omega * front_omega;
        float front_load = front_mass_per_wheel * 9.81f;
        float expected_sag = PxClamp(front_load / front_stiffness, 0.0f, cfg.suspension_travel * 0.8f);
        float spawn_y = cfg.wheel_radius + cfg.suspension_height + expected_sag;
        
        body = physics->createRigidDynamic(PxTransform(PxVec3(0, spawn_y, 0)));
        if (!body)
        {
            material->release();
            material = nullptr;
            return false;
        }
        
        // chassis collision box
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
        
        chassis->setFlag(PxShapeFlag::eSCENE_QUERY_SHAPE, false); // don't hit self with raycasts
        body->attachShape(*chassis);
        chassis->release();
        
        // mass and inertia
PxRigidBodyExt::setMassAndUpdateInertia(*body, cfg.mass);
        
        // we handle gravity manually
        body->setActorFlag(PxActorFlag::eDISABLE_GRAVITY, true);
        body->setRigidBodyFlag(PxRigidBodyFlag::eENABLE_CCD, true);
        
        // set damping once here, not every frame
        body->setLinearDamping(tuning::linear_damping);
        body->setAngularDamping(tuning::angular_damping);
        
        scene->addActor(*body);
        
        SP_LOG_INFO("car created: mass=%.0f kg", cfg.mass);
        return true;
    }
    
    inline void destroy()
    {
        if (body)
        {
            body->release();
            body = nullptr;
        }
        if (material)
        {
            material->release();
            material = nullptr;
        }
    }

    //===================================================================================
    // input
    //===================================================================================
    
    inline void set_throttle(float v)  { input_target.throttle  = PxClamp(v, 0.0f, 1.0f); }
    inline void set_brake(float v)     { input_target.brake     = PxClamp(v, 0.0f, 1.0f); }
    inline void set_steering(float v)  { input_target.steering  = PxClamp(v, -1.0f, 1.0f); }
    inline void set_handbrake(float v) { input_target.handbrake = PxClamp(v, 0.0f, 1.0f); }

    //===================================================================================
    // physics simulation
    //===================================================================================
    
    inline void update_input(float dt)
    {
        // steering: always smooth
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
        
        // use chassis local down direction for proper slope handling
PxVec3 local_down = pose.q.rotate(PxVec3(0, -1, 0));
        
        for (int i = 0; i < wheel_count; i++)
        {
            wheel& w = wheels[i];
            w.prev_compression = w.compression;
            
            // raycast from suspension attachment point
PxVec3 attach = wheel_offsets[i];
            attach.y += cfg.suspension_travel;
PxVec3 world_attach = pose.transform(attach);
            
            float ray_len = cfg.suspension_travel + cfg.wheel_radius + 0.5f;
PxRaycastBuffer hit;
PxQueryFilterData filter;
            filter.flags = PxQueryFlag::eSTATIC | PxQueryFlag::eDYNAMIC;
            
            if (scene->raycast(world_attach, local_down, ray_len, hit, PxHitFlag::eDEFAULT, filter) &&
                hit.block.actor && hit.block.actor != body)
            {
                float max_dist = cfg.suspension_travel + cfg.wheel_radius;
                if (hit.block.distance <= max_dist)
                {
                    w.grounded = true;
                    w.contact_point = hit.block.position;
                    w.contact_normal = hit.block.normal;
                    
                    float dist = PxMax(hit.block.distance - cfg.wheel_radius, 0.0f);
                    w.compression = PxClamp(1.0f - dist / cfg.suspension_travel, 0.0f, 1.0f);
                }
                else
                {
                    w.grounded = false;
                    w.compression = 0.0f;
                }
            }
            else
            {
                w.grounded = false;
                w.compression = 0.0f;
            }
        }
    }
    
    inline void apply_suspension_forces(float dt)
    {
PxTransform pose = body->getGlobalPose();
        float forces[wheel_count];
        
        // base spring + damper forces
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
            
            float velocity = (w.compression - w.prev_compression) * cfg.suspension_travel / dt;
            velocity = PxClamp(velocity, -tuning::max_damper_velocity, tuning::max_damper_velocity);
            float damper_f = spring_damping[i] * velocity;
            
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
        
        // apply forces
        for (int i = 0; i < wheel_count; i++)
        {
            forces[i] = PxClamp(forces[i], 0.0f, tuning::max_susp_force);
            wheels[i].tire_load = forces[i];
            
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
PxVec3 chassis_fwd = pose.q.rotate(PxVec3(0, 0, 1));
PxVec3 chassis_right = pose.q.rotate(PxVec3(1, 0, 0));
        
        for (int i = 0; i < wheel_count; i++)
        {
            wheel& w = wheels[i];
            
            if (!w.grounded || w.tire_load <= 0.0f)
            {
                w.slip_angle = w.slip_ratio = w.lateral_force = w.longitudinal_force = 0.0f;
                
                // airborne wheel behavior
                if (is_rear(i) && input.handbrake > tuning::input_deadzone)
                    w.angular_velocity = 0.0f;
                else
                    w.angular_velocity *= tuning::airborne_wheel_decay;
                
                w.rotation += w.angular_velocity * dt;
                continue;
            }
            
            // wheel velocity at contact point
PxVec3 world_pos = pose.transform(wheel_offsets[i]);
PxVec3 vel = body->getLinearVelocity() + body->getAngularVelocity().cross(world_pos - pose.p);
            
            // project to ground plane
            vel -= w.contact_normal * vel.dot(w.contact_normal);
            
            // wheel-aligned directions
            float cs = cosf(wheel_angles[i]), sn = sinf(wheel_angles[i]);
PxVec3 wheel_fwd = chassis_fwd * cs + chassis_right * sn;
PxVec3 wheel_lat = chassis_right * cs - chassis_fwd * sn;
            
            float vx = vel.dot(wheel_fwd);  // forward velocity
            float vy = vel.dot(wheel_lat);  // lateral velocity
            float wheel_speed = w.angular_velocity * cfg.wheel_radius;
            
            // slip ratio (longitudinal)
            float max_v = PxMax(fabsf(wheel_speed), fabsf(vx));
            w.slip_ratio = (max_v > tuning::min_slip_speed)
                ? PxClamp((wheel_speed - vx) / max_v, -1.0f, 1.0f)
                : 0.0f;
            
            // slip angle (lateral)
            w.slip_angle = (fabsf(vx) > tuning::min_slip_speed)
                ? atan2f(vy, fabsf(vx))
                : 0.0f;
            
            // tire forces from slip
            float max_force = tuning::tire_friction * w.tire_load;
            float lat_norm = tire_force_curve(w.slip_angle, tuning::peak_slip_angle);
            float long_norm = tire_force_curve(w.slip_ratio, tuning::peak_slip_ratio);
            
            float lat_f = -lat_norm * max_force;
            float long_f = long_norm * max_force;
            
            // reduce lateral force at extreme slip angles (stability)
            float extreme_threshold = PxPi * tuning::extreme_slip_threshold;
            if (fabsf(w.slip_angle) > extreme_threshold)
            {
                float reduction = 1.0f - tuning::extreme_slip_reduction * 
PxClamp((fabsf(w.slip_angle) - extreme_threshold) / extreme_threshold, 0.0f, 1.0f);
                lat_f *= reduction;
            }
            
            // handbrake: lock rear wheels with sliding friction
            if (is_rear(i) && input.handbrake > tuning::input_deadzone)
            {
                float sliding_f = tuning::handbrake_sliding_factor * max_force;
                if (fabsf(vx) > tuning::min_slip_speed)
                    long_f = (vx > 0.0f ? -1.0f : 1.0f) * sliding_f * input.handbrake;
            }
            
            // friction circle constraint
            float combined = sqrtf(lat_f * lat_f + long_f * long_f);
            if (combined > max_force)
            {
                float scale = max_force / combined;
                lat_f *= scale;
                long_f *= scale;
            }
            
            w.lateral_force = lat_f;
            w.longitudinal_force = long_f;
            
            // apply tire force
PxVec3 tire_force = wheel_lat * lat_f + wheel_fwd * long_f;
PxRigidBodyExt::addForceAtPos(*body, tire_force, world_pos, PxForceMode::eFORCE);
            
            // update wheel angular velocity
            if (is_rear(i) && input.handbrake > tuning::input_deadzone)
            {
                w.angular_velocity = 0.0f;
            }
            else
            {
                float target_w = vx / cfg.wheel_radius;
                float tire_torque = -long_f * cfg.wheel_radius;
                w.angular_velocity += (tire_torque / wheel_moi[i]) * dt;
                
                // ground matching when coasting
                bool coasting = input.throttle < tuning::input_deadzone && input.brake < tuning::input_deadzone;
                if (coasting || is_front(i))
                {
                    float blend = exp_decay(tuning::ground_match_rate, dt);
                    w.angular_velocity = lerp(w.angular_velocity, target_w, blend);
                }
                
                w.angular_velocity *= (1.0f - tuning::bearing_friction * dt); // bearing friction
            }
            
            w.rotation += w.angular_velocity * dt;
        }
    }
    
    inline void apply_drivetrain(float forward_speed_kmh, float dt)
    {
        // throttle -> rear wheels
        if (input.throttle > tuning::input_deadzone)
        {
            float speed_kmh = body->getLinearVelocity().magnitude() * 3.6f;
            float power = 1.0f - PxClamp(speed_kmh / tuning::max_forward_speed, 0.0f, tuning::max_power_reduction);
            float torque = tuning::engine_force * cfg.wheel_radius * input.throttle * power;
            
            // explicit indexing instead of relying on enum order
            wheels[rear_left].angular_velocity  += (torque * 0.5f / wheel_moi[rear_left]) * dt;
            wheels[rear_right].angular_velocity += (torque * 0.5f / wheel_moi[rear_right]) * dt;
        }
        
        // brake / reverse
        if (input.brake > tuning::input_deadzone)
        {
            if (forward_speed_kmh > tuning::braking_speed_threshold)
            {
                // braking
                float total_torque = tuning::brake_force * cfg.wheel_radius * input.brake;
                float front_t = total_torque * tuning::brake_bias_front * 0.5f;
                float rear_t = total_torque * (1.0f - tuning::brake_bias_front) * 0.5f;
                
                for (int i = 0; i < wheel_count; i++)
                {
                    float t = is_front(i) ? front_t : rear_t;
                    float sign = wheels[i].angular_velocity >= 0.0f ? -1.0f : 1.0f;
                    float new_w = wheels[i].angular_velocity + sign * t / wheel_moi[i] * dt;
                    
                    // don't reverse wheel direction
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
                float torque = tuning::engine_force * tuning::reverse_power_ratio * cfg.wheel_radius * input.brake * power;
                
                // explicit indexing instead of relying on enum order
                wheels[rear_left].angular_velocity  -= (torque * 0.5f / wheel_moi[rear_left]) * dt;
                wheels[rear_right].angular_velocity -= (torque * 0.5f / wheel_moi[rear_right]) * dt;
            }
        }
        
        // handbrake locks rear wheels
        if (input.handbrake > tuning::input_deadzone)
        {
            wheels[rear_left].angular_velocity = 0.0f;
            wheels[rear_right].angular_velocity = 0.0f;
        }
    }
    
    inline void apply_aero_and_resistance()
    {
PxVec3 vel = body->getLinearVelocity();
        float speed = vel.magnitude();
        
        // aerodynamic drag: f = 0.5 * rho * cd * a * v²
        if (speed > 1.0f)
        {
            float drag = 0.5f * tuning::air_density * tuning::drag_coeff * tuning::frontal_area * speed * speed;
            body->addForce(-vel.getNormalized() * drag, PxForceMode::eFORCE);
        }
        
        // rolling resistance
        float tire_load = 0.0f;
        for (int i = 0; i < wheel_count; i++)
            if (wheels[i].grounded) tire_load += wheels[i].tire_load;
        
        if (speed > 0.1f && tire_load > 0.0f)
        {
            float rr = tuning::rolling_resistance * tire_load;
            body->addForce(-vel.getNormalized() * rr, PxForceMode::eFORCE);
        }
    }
    
    inline void calculate_steering(float forward_speed, float speed_kmh, float out_angles[wheel_count])
    {
        // speed-dependent steering reduction
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
        
        // ackermann geometry (only when going forward)
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

    //===================================================================================
    // main tick
    //===================================================================================
    
    inline void tick(float dt)
    {
        if (!body) return;
        
PxScene* scene = body->getScene();
        if (!scene) return;
        
        // smooth inputs
        update_input(dt);
        
        // get velocity info
PxTransform pose = body->getGlobalPose();
PxVec3 fwd = pose.q.rotate(PxVec3(0, 0, 1));
PxVec3 vel = body->getLinearVelocity();
        float forward_speed = vel.dot(fwd);
        float speed_kmh = vel.magnitude() * 3.6f;
        
        // steering angles
        float wheel_angles[wheel_count];
        calculate_steering(forward_speed, speed_kmh, wheel_angles);
        
        // drivetrain (modifies wheel angular velocities)
        apply_drivetrain(forward_speed * 3.6f, dt);
        
        // suspension raycasts
        update_suspension(scene, dt);
        
        // suspension forces (spring + damper + arb)
        apply_suspension_forces(dt);
        
        // tire forces (slip-based pacejka model)
        apply_tire_forces(wheel_angles, dt);
        
        // aero and rolling resistance
        apply_aero_and_resistance();
        
        // manual gravity
        body->addForce(PxVec3(0, -9.81f * cfg.mass, 0), PxForceMode::eFORCE);
    }

    //===================================================================================
    // accessors
    //===================================================================================
    
    inline float get_speed_kmh()
    {
        return body ? body->getLinearVelocity().magnitude() * 3.6f : 0.0f;
    }
    
    inline float get_throttle()          { return input.throttle; }
    inline float get_brake()             { return input.brake; }
    inline float get_steering()          { return input.steering; }
    inline float get_handbrake()         { return input.handbrake; }
    inline float get_suspension_travel() { return cfg.suspension_travel; }
    
    inline bool  is_valid_wheel(int i)   { return i >= 0 && i < wheel_count; }
    inline const char* get_wheel_name(int i)
    {
        static const char* names[] = { "FL", "FR", "RL", "RR" };
        return is_valid_wheel(i) ? names[i] : "??";
    }
    
    inline float get_wheel_compression(int i)       { return is_valid_wheel(i) ? wheels[i].compression : 0.0f; }
    inline bool  is_wheel_grounded(int i)           { return is_valid_wheel(i) ? wheels[i].grounded : false; }
    inline float get_wheel_slip_angle(int i)        { return is_valid_wheel(i) ? wheels[i].slip_angle : 0.0f; }
    inline float get_wheel_slip_ratio(int i)        { return is_valid_wheel(i) ? wheels[i].slip_ratio : 0.0f; }
    inline float get_wheel_tire_load(int i)         { return is_valid_wheel(i) ? wheels[i].tire_load : 0.0f; }
    inline float get_wheel_lateral_force(int i)     { return is_valid_wheel(i) ? wheels[i].lateral_force : 0.0f; }
    inline float get_wheel_longitudinal_force(int i){ return is_valid_wheel(i) ? wheels[i].longitudinal_force : 0.0f; }
    inline float get_wheel_angular_velocity(int i)  { return is_valid_wheel(i) ? wheels[i].angular_velocity : 0.0f; }
    inline float get_wheel_rotation(int i)          { return is_valid_wheel(i) ? wheels[i].rotation : 0.0f; }
    
    inline float get_wheel_suspension_force(int i)
    {
        if (!is_valid_wheel(i) || !wheels[i].grounded) return 0.0f;
        return spring_stiffness[i] * wheels[i].compression * cfg.suspension_travel;
    }
    
    // returns the local y offset for visual chassis to align with physics body
    // the physics body center is above ground by (wheel_radius + suspension_height)
    // the visual mesh origin is typically at ground level, so we offset down by this amount
    inline float get_chassis_visual_offset_y()
    {
        return -(cfg.height * 0.5f + cfg.suspension_height);
    }
}
