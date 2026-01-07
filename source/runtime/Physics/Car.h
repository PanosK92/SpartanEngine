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
#include <physx/vehicle2/PxVehicleAPI.h>
#include "../Logging/Log.h"
//======================================

namespace car
{
    // vehicle2 api implementation for a basic 4-wheel vehicle
    using namespace physx;
    using namespace physx::vehicle2;

    //=======================================================================================
    // constants
    //=======================================================================================
    
    // driving
    inline constexpr float max_engine_force   = 15000.0f; // newtons (peak torque ~250nm at wheel with 0.35m radius)
    inline constexpr float max_brake_force    = 8000.0f;  // newtons per wheel
    inline constexpr float brake_bias_front   = 0.6f;     // 60% front, 40% rear (typical for fwd/rwd)
    inline constexpr float input_smoothing    = 10.0f;    // throttle/brake response speed
    inline constexpr float steering_smoothing = 20.0f;    // steering response speed

    // tire model
    inline constexpr float tire_friction_coeff = 1.0f;     // base friction coefficient (dry asphalt ~1.0)
    inline constexpr float min_speed_for_slip  = 0.5f;     // m/s, threshold for slip calculations
    
    // suspension - anti-roll bars control understeer/oversteer balance
    // softer front + stiffer rear = less understeer
    inline constexpr float front_anti_roll_stiffness = 8000.0f;  // n/m, soft front = more front grip
    inline constexpr float rear_anti_roll_stiffness  = 20000.0f; // n/m, stiff rear = less rear grip = car rotates
    inline constexpr float max_suspension_force      = 25000.0f; // n, prevents explosive forces on hard impacts
    inline constexpr float front_suspension_freq     = 1.5f;     // hz
    inline constexpr float rear_suspension_freq      = 1.8f;     // hz
    
    // aerodynamics
    inline constexpr float drag_coefficient     = 0.35f;   // cd, typical sedan 0.30-0.35, sports 0.28-0.32
    inline constexpr float frontal_area         = 2.2f;    // m², typical sedan ~2.0-2.4
    inline constexpr float air_density          = 1.225f;  // kg/m³ at sea level
    inline constexpr float rolling_resistance_coeff = 0.015f; // typical tire on asphalt 0.010-0.020

    //=======================================================================================
    // enums and types
    //=======================================================================================
    
    // wheel indices for a standard 4-wheel vehicle
    enum wheel_id
    {
        front_left  = 0,
        front_right = 1,
        rear_left   = 2,
        rear_right  = 3,
        wheel_count = 4
    };

    //=======================================================================================
    // data structures
    //=======================================================================================
    
    // default vehicle dimensions (in meters)
    struct vehicle_dimensions
    {
        float chassis_length    = 4.5f;    // front to back
        float chassis_width     = 2.0f;    // side to side
        float chassis_height    = 0.5f;    // vertical
        float chassis_mass      = 1500.0f;
        float wheel_radius      = 0.35f;
        float wheel_width       = 0.25f;
        float wheel_mass        = 20.0f;
        float suspension_travel = 0.3f;
        float suspension_height = 0.4f;    // distance from chassis bottom to wheel center at rest
    };
    
    // vehicle data container
    struct vehicle_data
    {
        PxVehicleAxleDescription             axle_desc;
        PxVehicleRigidBodyParams             rigid_body_params;
        PxVehicleWheelParams                 wheel_params[wheel_count];
        PxVehicleSuspensionParams            suspension_params[wheel_count];
        PxVehicleSuspensionForceParams       suspension_force_params[wheel_count];
        PxVehicleSuspensionComplianceParams  suspension_compliance_params[wheel_count];
        PxVehicleTireForceParams             tire_params[wheel_count];
        PxVehiclePhysXActor                  physx_actor;
        PxMaterial*                          material = nullptr;
    };
    
    // driving parameters
    struct drive_input
    {
        float throttle = 0.0f;  // 0 to 1
        float brake    = 0.0f;  // 0 to 1
        float steering = 0.0f;  // -1 to 1 (left to right)
    };
    
    // suspension state per wheel
    struct wheel_state
    {
        float suspension_compression = 0.0f; // 0 = fully extended, 1 = fully compressed
        float previous_compression   = 0.0f; // for damping calculation
        bool  is_grounded            = false;
        PxVec3 contact_point         = PxVec3(0, 0, 0);
        PxVec3 contact_normal        = PxVec3(0, 1, 0);
        
        // tire slip state (for pacejka model)
        float slip_angle         = 0.0f; // radians, lateral slip
        float slip_ratio         = 0.0f; // longitudinal slip (-1 to 1)
        float tire_load          = 0.0f; // vertical load on tire (newtons)
        float lateral_force      = 0.0f; // resulting lateral force
        float longitudinal_force = 0.0f; // resulting longitudinal force
    };

    //=======================================================================================
    // global state
    //=======================================================================================
    
    inline static vehicle_data*       active_vehicle = nullptr;
    inline static drive_input         current_input;
    inline static drive_input         target_input;
    inline static wheel_state         wheel_states[wheel_count];
    inline static vehicle_dimensions  active_dims;

    //=======================================================================================
    // helper functions
    //=======================================================================================
    
    // bounds check helper for wheel index
    inline bool is_valid_wheel_index(int wheel_index)
    {
        return wheel_index >= 0 && wheel_index < wheel_count;
    }

    // compute default wheel positions based on dimensions
    inline void compute_wheel_positions(const vehicle_dimensions& dims, PxVec3 out_positions[wheel_count])
    {
        const float front_z = dims.chassis_length * 0.35f;  // front wheels slightly forward
        const float rear_z  = -dims.chassis_length * 0.35f; // rear wheels slightly back
        const float half_w  = dims.chassis_width * 0.5f - dims.wheel_width * 0.5f;
        const float y       = -dims.suspension_height;      // below chassis
        
        out_positions[front_left]  = PxVec3(-half_w, y, front_z);
        out_positions[front_right] = PxVec3( half_w, y, front_z);
        out_positions[rear_left]   = PxVec3(-half_w, y, rear_z);
        out_positions[rear_right]  = PxVec3( half_w, y, rear_z);
    }

    //=======================================================================================
    // setup functions
    //=======================================================================================
    
    // initialize axle description for 4 wheels (2 axles, 2 wheels each)
    inline void setup_axle_description(PxVehicleAxleDescription& axle_desc)
    {
        axle_desc.setToDefault();
        
        const PxU32 front_wheels[] = { front_left, front_right };
        axle_desc.addAxle(2, front_wheels);
        
        const PxU32 rear_wheels[] = { rear_left, rear_right };
        axle_desc.addAxle(2, rear_wheels);
    }
    
    // initialize rigid body parameters
    inline void setup_rigid_body_params(PxVehicleRigidBodyParams& params, const vehicle_dimensions& dims)
    {
        params.mass = dims.chassis_mass;
        
        // moment of inertia calculation
        // real cars have mass distributed differently than a uniform box:
        // - mass concentrated low (engine, drivetrain)
        // - mass spread along length (passengers, fuel tank)
        // we use empirical multipliers based on real vehicle data
        const float w = dims.chassis_width;
        const float h = dims.chassis_height;
        const float l = dims.chassis_length;
        const float m = dims.chassis_mass;
        
        // base box inertia
        float ixx = (m / 12.0f) * (h * h + l * l); // roll
        float iyy = (m / 12.0f) * (w * w + l * l); // yaw
        float izz = (m / 12.0f) * (w * w + h * h); // pitch
        
        // empirical adjustments - slightly reduced yaw for responsive steering
        params.moi = PxVec3(
            ixx * 1.0f,  // roll
            iyy * 1.0f,  // yaw - normal
            izz * 1.0f   // pitch
        );
    }
    
    // initialize wheel parameters
    inline void setup_wheel_params(PxVehicleWheelParams wheels[wheel_count], const vehicle_dimensions& dims)
    {
        for (int i = 0; i < wheel_count; i++)
        {
            wheels[i].radius    = dims.wheel_radius;
            wheels[i].halfWidth = dims.wheel_width * 0.5f;
            wheels[i].mass      = dims.wheel_mass;
            
            // moment of inertia for a solid cylinder: I = 0.5 * m * r^2
            // wheels are more like rings (tire has mass at outer edge), so use ~0.8 factor
            wheels[i].moi = 0.8f * dims.wheel_mass * dims.wheel_radius * dims.wheel_radius;
            
            // damping rate - how quickly wheel rotation decays without drive/brake
            // this simulates bearing friction and drag
            // front wheels (non-driven in rwd) have less damping
            // rear wheels (driven) have slight engine braking effect
            const bool is_front = (i == front_left || i == front_right);
            wheels[i].dampingRate = is_front ? 0.15f : 0.25f;
        }
    }
    
    // initialize suspension parameters
    inline void setup_suspension_params(PxVehicleSuspensionParams suspension[wheel_count], const vehicle_dimensions& dims)
    {
        PxVec3 wheel_positions[wheel_count];
        compute_wheel_positions(dims, wheel_positions);
        
        for (int i = 0; i < wheel_count; i++)
        {
            // suspension attachment is at the top of the suspension travel (max compression)
            PxVec3 attachment_pos = wheel_positions[i];
            attachment_pos.y += dims.suspension_travel;
            
            suspension[i].suspensionAttachment = PxTransform(attachment_pos, PxQuat(PxIdentity));
            suspension[i].suspensionTravelDir  = PxVec3(0.0f, -1.0f, 0.0f);
            suspension[i].suspensionTravelDist = dims.suspension_travel;
            suspension[i].wheelAttachment      = PxTransform(PxVec3(0, 0, 0), PxQuat(PxIdentity));
        }
    }
    
    // initialize suspension force parameters
    inline void setup_suspension_force_params(PxVehicleSuspensionForceParams force[wheel_count], const vehicle_dimensions& dims)
    {
        // sprung mass per wheel - weight distribution affects handling:
        // - front heavy = understeer, rear heavy = oversteer
        // using 50/50 for neutral balance (sports car like)
        const float front_weight_bias = 0.50f;
        const float front_sprung_mass = dims.chassis_mass * front_weight_bias * 0.5f;
        const float rear_sprung_mass  = dims.chassis_mass * (1.0f - front_weight_bias) * 0.5f;
        
        // spring stiffness: k = m * (2 * pi * f)^2
        // different frequencies front vs rear creates handling balance:
        // - softer front = more grip during braking, better turn-in
        // - stiffer rear = more stable, less oversteer on exit
        const float front_omega_sq = (2.0f * PxPi * front_suspension_freq) * (2.0f * PxPi * front_suspension_freq);
        const float rear_omega_sq  = (2.0f * PxPi * rear_suspension_freq) * (2.0f * PxPi * rear_suspension_freq);
        
        // damping ratio: 1.0 = critically damped (no oscillation)
        // real cars: 0.2-0.4 (comfort), 0.5-0.7 (sport), 0.8+ (race)
        // we use different ratios: higher compression, lower rebound for better control
        const float damping_ratio_compression = 0.4f; // bump absorption
        const float damping_ratio_rebound     = 0.5f; // return speed
        const float avg_damping_ratio = (damping_ratio_compression + damping_ratio_rebound) * 0.5f;
        
        for (int i = 0; i < wheel_count; i++)
        {
            const bool is_front = (i == front_left || i == front_right);
            const float sprung_mass = is_front ? front_sprung_mass : rear_sprung_mass;
            const float omega_sq = is_front ? front_omega_sq : rear_omega_sq;
            
            force[i].sprungMass = sprung_mass;
            force[i].stiffness  = sprung_mass * omega_sq;
            
            // critical damping: c = 2 * sqrt(k * m), then multiply by ratio
            force[i].damping = 2.0f * avg_damping_ratio * sqrtf(force[i].stiffness * sprung_mass);
        }
    }
    
    // initialize suspension compliance parameters (defaults - no compliance effects)
    inline void setup_suspension_compliance_params(PxVehicleSuspensionComplianceParams compliance[wheel_count])
    {
        for (int i = 0; i < wheel_count; i++)
        {
            compliance[i].wheelToeAngle.clear();
            compliance[i].wheelCamberAngle.clear();
            compliance[i].suspForceAppPoint.clear();
            compliance[i].tireForceAppPoint.clear();
        }
    }
    
    // initialize tire force parameters
    inline void setup_tire_params(PxVehicleTireForceParams tires[wheel_count], const vehicle_dimensions& dims)
    {
        // rest load per tire (assumes level ground, 50/50 weight distribution)
        const float gravity    = 9.81f;
        const float total_mass = dims.chassis_mass + dims.wheel_mass * static_cast<float>(wheel_count);
        const float front_load = (total_mass * gravity * 0.50f) / 2.0f; // 50% front
        const float rear_load  = (total_mass * gravity * 0.50f) / 2.0f; // 50% rear
        
        for (int i = 0; i < wheel_count; i++)
        {
            const bool is_front = (i == front_left || i == front_right);
            const float rest_load = is_front ? front_load : rear_load;
            
            // lateral stiffness: how quickly lateral force builds with slip angle
            // real tires: ~150-300 n per degree per 1000n load
            // latStiffX = normalized load at which stiffness saturates
            // latStiffY = max lateral stiffness (n/rad at rest load)
            tires[i].latStiffX   = 2.0f;
            tires[i].latStiffY   = 200.0f * (180.0f / PxPi) * (rest_load / 1000.0f); // ~200 n/deg cornering stiffness
            
            // longitudinal stiffness: resistance to wheel spin/lockup
            // typically 5-15x lateral stiffness
            tires[i].longStiff   = tires[i].latStiffY * 8.0f;
            
            // camber thrust (force from tire lean) - simplified to zero
            tires[i].camberStiff = 0.0f;
            tires[i].restLoad    = rest_load;
            
            // friction vs slip curve - models tire behavior from grip to slide
            // based on real tire data: peak grip at ~8-12% slip, then drops to sliding friction
            // [0] = at zero slip: friction starts at 1.0 (linear region)
            // [1] = at peak slip (~8 deg or ~0.14 rad): friction peaks at 1.05 (slight overshoot)
            // [2] = full slide: friction drops to 0.85-0.90 (sliding/kinetic friction)
            tires[i].frictionVsSlip[0][0] = 0.0f;    tires[i].frictionVsSlip[0][1] = 1.0f;
            tires[i].frictionVsSlip[1][0] = 0.14f;   tires[i].frictionVsSlip[1][1] = 1.05f;
            tires[i].frictionVsSlip[2][0] = 1.0f;    tires[i].frictionVsSlip[2][1] = 0.85f;
            
            // load filter: prevents unrealistic spikes in tire load
            // [0] = min load multiplier, [1] = max load multiplier
            tires[i].loadFilter[0][0] = 0.0f;  tires[i].loadFilter[0][1] = 0.2f;  // min 20% at zero load
            tires[i].loadFilter[1][0] = 3.0f;  tires[i].loadFilter[1][1] = 2.5f;  // max 250% at 3x load
        }
    }
    
    // create the physx actor for the vehicle
    inline bool create_physx_actor(vehicle_data& data, const vehicle_dimensions& dims, PxPhysics* physics, PxScene* scene)
    {
        if (!physics || !scene)
            return false;
        
        // create material
        data.material = physics->createMaterial(0.8f, 0.7f, 0.1f);
        if (!data.material)
            return false;
        
        // create rigid dynamic body
        // the suspension will compress under the car's weight (typically ~30-40% at rest)
        // we need to account for this "sag" so wheels sit on ground at equilibrium
        // expected_sag ≈ (weight per wheel) / stiffness ≈ compression_ratio * suspension_travel
        const float expected_compression = 0.35f; // typical rest compression
        const float expected_sag = expected_compression * dims.suspension_travel;
        
        // body height = wheel_radius (wheel touches ground) + suspension_height + sag compensation
        const float body_height = dims.wheel_radius + dims.suspension_height + expected_sag;
        PxTransform initial_pose(PxVec3(0, body_height, 0));
        PxRigidDynamic* rigid_body = physics->createRigidDynamic(initial_pose);
        if (!rigid_body)
        {
            data.material->release();
            data.material = nullptr;
            return false;
        }
        
        // chassis shape (box)
        PxBoxGeometry chassis_geom(dims.chassis_width * 0.5f, dims.chassis_height * 0.5f, dims.chassis_length * 0.5f);
        PxShape* chassis_shape = physics->createShape(chassis_geom, *data.material);
        if (chassis_shape)
        {
            // disable scene query on chassis so suspension raycasts don't hit it
            chassis_shape->setFlag(PxShapeFlag::eSCENE_QUERY_SHAPE, false);
            rigid_body->attachShape(*chassis_shape);
            chassis_shape->release();
        }
        
        // set mass properties
        PxRigidBodyExt::setMassAndUpdateInertia(*rigid_body, data.rigid_body_params.mass);
        
        // disable gravity (vehicle2 applies gravity manually)
        rigid_body->setActorFlag(PxActorFlag::eDISABLE_GRAVITY, true);
        
        // enable ccd for fast-moving objects
        rigid_body->setRigidBodyFlag(PxRigidBodyFlag::eENABLE_CCD, true);
        
        // add to scene
        scene->addActor(*rigid_body);
        
        // store in vehicle data
        data.physx_actor.setToDefault();
        data.physx_actor.rigidBody = rigid_body;
        
        // create wheel shapes (cylinders approximated as capsules for simplicity)
        PxVec3 wheel_positions[wheel_count];
        compute_wheel_positions(dims, wheel_positions);
        
        for (int i = 0; i < wheel_count; i++)
        {
            PxCapsuleGeometry wheel_geom(dims.wheel_radius, dims.wheel_width * 0.5f);
            PxShape* wheel_shape = physics->createShape(wheel_geom, *data.material);
            if (wheel_shape)
            {
                // rotate capsule to align with lateral axis (z -> x rotation)
                PxTransform local_pose(wheel_positions[i], PxQuat(PxHalfPi, PxVec3(0, 0, 1)));
                wheel_shape->setLocalPose(local_pose);
                
                // wheel shapes should not participate in simulation or scene queries
                // (they're visual only, and we don't want suspension raycasts to hit them)
                wheel_shape->setFlag(PxShapeFlag::eSIMULATION_SHAPE, false);
                wheel_shape->setFlag(PxShapeFlag::eSCENE_QUERY_SHAPE, false);
                
                rigid_body->attachShape(*wheel_shape);
                data.physx_actor.wheelShapes[i] = wheel_shape;
                wheel_shape->release();
            }
        }
        
        return true;
    }
    
    // main setup function: creates a basic 4-wheel vehicle
    inline vehicle_data* setup(PxPhysics* physics, PxScene* scene, const vehicle_dimensions* custom_dims = nullptr)
    {
        // use default dimensions if none provided
        vehicle_dimensions dims;
        if (custom_dims)
            dims = *custom_dims;
        
        // allocate vehicle data
        vehicle_data* data = new vehicle_data();
        
        // setup all components
        setup_axle_description(data->axle_desc);
        setup_rigid_body_params(data->rigid_body_params, dims);
        setup_wheel_params(data->wheel_params, dims);
        setup_suspension_params(data->suspension_params, dims);
        setup_suspension_force_params(data->suspension_force_params, dims);
        setup_suspension_compliance_params(data->suspension_compliance_params);
        setup_tire_params(data->tire_params, dims);
        
        // validate all parameters
        if (!data->axle_desc.isValid())
        {
            SP_LOG_ERROR("vehicle axle description is invalid");
            delete data;
            return nullptr;
        }
        
        if (!data->rigid_body_params.isValid())
        {
            SP_LOG_ERROR("vehicle rigid body params are invalid");
            delete data;
            return nullptr;
        }
        
        for (int i = 0; i < wheel_count; i++)
        {
            if (!data->wheel_params[i].isValid())
            {
                SP_LOG_ERROR("vehicle wheel params [%d] are invalid", i);
                delete data;
                return nullptr;
            }
            
            if (!data->suspension_params[i].isValid())
            {
                SP_LOG_ERROR("vehicle suspension params [%d] are invalid", i);
                delete data;
                return nullptr;
            }
            
            if (!data->suspension_force_params[i].isValid())
            {
                SP_LOG_ERROR("vehicle suspension force params [%d] are invalid", i);
                delete data;
                return nullptr;
            }
            
            if (!data->tire_params[i].isValid())
            {
                SP_LOG_ERROR("vehicle tire params [%d] are invalid", i);
                delete data;
                return nullptr;
            }
        }
        
        // create physx actor
        if (!create_physx_actor(*data, dims, physics, scene))
        {
            SP_LOG_ERROR("failed to create vehicle physx actor");
            delete data;
            return nullptr;
        }
        
        SP_LOG_INFO("vehicle setup complete: 4 wheels, mass=%.0f kg", dims.chassis_mass);
        return data;
    }

    // cleanup function
    inline void destroy(vehicle_data* data)
    {
        if (!data)
            return;
        
        if (data->physx_actor.rigidBody)
        {
            data->physx_actor.rigidBody->release();
            data->physx_actor.rigidBody = nullptr;
        }
        
        if (data->material)
        {
            data->material->release();
            data->material = nullptr;
        }
        
        delete data;
    }

    //=======================================================================================
    // active vehicle management
    //=======================================================================================
    
    inline void set_active_vehicle(vehicle_data* data, const vehicle_dimensions* dims = nullptr)
    {
        active_vehicle = data;
        current_input  = drive_input();
        target_input   = drive_input();
        
        for (int i = 0; i < wheel_count; i++)
            wheel_states[i] = wheel_state();
        
        active_dims = dims ? *dims : vehicle_dimensions();
    }
    
    inline vehicle_data* get_active_vehicle()
    {
        return active_vehicle;
    }

    //=======================================================================================
    // control input
    //=======================================================================================
    
    inline void set_throttle(float value)
    {
        target_input.throttle = PxClamp(value, 0.0f, 1.0f);
    }
    
    inline void set_brake(float value)
    {
        target_input.brake = PxClamp(value, 0.0f, 1.0f);
    }
    
    inline void set_steering(float value)
    {
        target_input.steering = PxClamp(value, -1.0f, 1.0f);
    }

    //=======================================================================================
    // suspension simulation
    //=======================================================================================
    
    // perform suspension raycast for a single wheel
    inline void update_wheel_suspension(int wheel_index, PxRigidDynamic* body, PxScene* scene, float delta_time)
    {
        PxTransform pose = body->getGlobalPose();
        
        // compute wheel attachment point in world space
        PxVec3 wheel_positions[wheel_count];
        compute_wheel_positions(active_dims, wheel_positions);
        
        // transform wheel position to world space (at rest position)
        PxVec3 local_attach = wheel_positions[wheel_index];
        local_attach.y += active_dims.suspension_travel;
        PxVec3 world_attach = pose.transform(local_attach);
        
        // raycast downward from attachment point (use world down, not local)
        PxVec3 ray_dir(0, -1, 0);
        float ray_length = active_dims.suspension_travel * 3.0f + active_dims.wheel_radius * 2.0f + 5.0f;
        
        PxRaycastBuffer hit;
        PxQueryFilterData filter;
        filter.flags = PxQueryFlag::eSTATIC | PxQueryFlag::eDYNAMIC;
        
        wheel_state& ws = wheel_states[wheel_index];
        ws.previous_compression = ws.suspension_compression;
        
        if (scene->raycast(world_attach, ray_dir, ray_length, hit, PxHitFlag::eDEFAULT, filter))
        {
            // check if we hit something other than ourselves
            if (hit.block.actor && hit.block.actor != body)
            {
                // check if hit is within suspension range
                float max_suspension_distance = active_dims.suspension_travel + active_dims.wheel_radius;
                if (hit.block.distance <= max_suspension_distance)
                {
                    ws.is_grounded    = true;
                    ws.contact_point  = hit.block.position;
                    ws.contact_normal = hit.block.normal;
                    
                    // calculate suspension compression
                    float actual_distance = hit.block.distance - active_dims.wheel_radius;
                    actual_distance = PxMax(actual_distance, 0.0f);
                    
                    // compression: 0 = fully extended, 1 = fully compressed
                    ws.suspension_compression = PxClamp(1.0f - (actual_distance / active_dims.suspension_travel), 0.0f, 1.0f);
                }
                else
                {
                    // ground is too far for suspension, but we detected it
                    ws.is_grounded            = false;
                    ws.suspension_compression = 0.0f;
                }
            }
            else
            {
                ws.is_grounded            = false;
                ws.suspension_compression = 0.0f;
            }
        }
        else
        {
            ws.is_grounded            = false;
            ws.suspension_compression = 0.0f;
        }
    }
    
    // calculate suspension force for a single wheel (doesn't apply it)
    inline float calculate_wheel_force(int wheel_index, float delta_time)
    {
        wheel_state& ws = wheel_states[wheel_index];
        
        if (!ws.is_grounded)
            return 0.0f;
        
        const PxVehicleSuspensionForceParams& force_params = active_vehicle->suspension_force_params[wheel_index];
        
        // spring force: F = stiffness * displacement
        float displacement = ws.suspension_compression * active_dims.suspension_travel;
        float spring_force = force_params.stiffness * displacement;
        
        // damper force: F = damping * velocity
        float compression_velocity = (ws.suspension_compression - ws.previous_compression) / delta_time;
        compression_velocity = PxClamp(compression_velocity, -5.0f, 5.0f);
        float damper_force = force_params.damping * compression_velocity * active_dims.suspension_travel;
        
        // total suspension force (spring + damper)
        float total_force = spring_force + damper_force;
        return PxClamp(total_force, 0.0f, max_suspension_force);
    }
    
    // apply all suspension forces with anti-roll bars
    inline void apply_suspension_forces(PxRigidDynamic* body, float delta_time)
    {
        if (!active_vehicle)
            return;
        
        // calculate base suspension forces for all wheels
        float forces[wheel_count];
        for (int i = 0; i < wheel_count; i++)
            forces[i] = calculate_wheel_force(i, delta_time);
        
        // anti-roll bar: front axle (couples fl and fr)
        // softer front anti-roll = more front grip in corners = less understeer
        {
            float compression_diff = wheel_states[front_left].suspension_compression - wheel_states[front_right].suspension_compression;
            float anti_roll_force  = compression_diff * front_anti_roll_stiffness * active_dims.suspension_travel;
            
            if (wheel_states[front_left].is_grounded)
                forces[front_left] += anti_roll_force;
            if (wheel_states[front_right].is_grounded)
                forces[front_right] -= anti_roll_force;
        }
        
        // anti-roll bar: rear axle (couples rl and rr)
        // stiffer rear anti-roll = less rear grip in corners = promotes rotation
        {
            float compression_diff = wheel_states[rear_left].suspension_compression - wheel_states[rear_right].suspension_compression;
            float anti_roll_force  = compression_diff * rear_anti_roll_stiffness * active_dims.suspension_travel;
            
            if (wheel_states[rear_left].is_grounded)
                forces[rear_left] += anti_roll_force;
            if (wheel_states[rear_right].is_grounded)
                forces[rear_right] -= anti_roll_force;
        }
        
        // clamp final forces and apply
        PxTransform pose = body->getGlobalPose();
        PxVec3 wheel_positions[wheel_count];
        compute_wheel_positions(active_dims, wheel_positions);
        
        for (int i = 0; i < wheel_count; i++)
        {
            forces[i] = PxClamp(forces[i], 0.0f, max_suspension_force);
            
            if (forces[i] > 0.0f && wheel_states[i].is_grounded)
            {
                PxVec3 force_dir      = wheel_states[i].contact_normal;
                PxVec3 force_vec      = force_dir * forces[i];
                PxVec3 world_wheel_pos = pose.transform(wheel_positions[i]);
                
                PxRigidBodyExt::addForceAtPos(*body, force_vec, world_wheel_pos, PxForceMode::eFORCE);
            }
        }
    }

    //=======================================================================================
    // tire simulation
    //=======================================================================================
    
    // simplified pacejka-like tire model
    // returns normalized force (0-1) for a given slip value
    inline float tire_force_curve(float slip, float peak_slip, float peak_force, float slide_force)
    {
        // piecewise linear approximation of pacejka magic formula
        // - linear rise to peak at peak_slip
        // - gradual decline to sliding friction beyond peak
        float abs_slip = fabsf(slip);
        float sign = slip >= 0.0f ? 1.0f : -1.0f;
        
        if (abs_slip < peak_slip)
        {
            // linear region: force rises with slip
            return sign * (abs_slip / peak_slip) * peak_force;
        }
        else
        {
            // saturation region: force declines toward sliding friction
            float excess = abs_slip - peak_slip;
            float decay = expf(-excess * 0.5f); // smooth exponential decay
            float force = slide_force + (peak_force - slide_force) * decay;
            return sign * force;
        }
    }
    
    // calculate tire slip and forces for a single wheel using slip-angle model
    inline void calculate_tire_forces(int wheel_index, PxRigidDynamic* body, float steering_angle)
    {
        wheel_state& ws = wheel_states[wheel_index];
        
        if (!ws.is_grounded || ws.tire_load <= 0.0f)
        {
            ws.slip_angle         = 0.0f;
            ws.slip_ratio         = 0.0f;
            ws.lateral_force      = 0.0f;
            ws.longitudinal_force = 0.0f;
            return;
        }
        
        PxTransform pose = body->getGlobalPose();
        
        // get velocity at wheel contact point (includes body rotation)
        PxVec3 wheel_positions[wheel_count];
        compute_wheel_positions(active_dims, wheel_positions);
        PxVec3 world_wheel_pos = pose.transform(wheel_positions[wheel_index]);
        
        PxVec3 wheel_velocity = body->getLinearVelocity();
        PxVec3 angular_vel    = body->getAngularVelocity();
        PxVec3 r = world_wheel_pos - pose.p;
        wheel_velocity += angular_vel.cross(r);
        
        // get wheel orientation (steering for front wheels)
        float wheel_angle = 0.0f;
        bool is_front = (wheel_index == front_left || wheel_index == front_right);
        if (is_front)
            wheel_angle = steering_angle;
        
        // wheel direction vectors in world space
        PxVec3 chassis_forward = pose.q.rotate(PxVec3(0, 0, 1));
        PxVec3 chassis_right   = pose.q.rotate(PxVec3(1, 0, 0));
        float cos_steer        = cosf(wheel_angle);
        float sin_steer        = sinf(wheel_angle);
        PxVec3 wheel_forward   = chassis_forward * cos_steer + chassis_right * sin_steer;
        PxVec3 wheel_lateral   = chassis_right * cos_steer - chassis_forward * sin_steer;
        
        // project velocity onto ground plane (contact patch plane)
        PxVec3 ground_velocity = wheel_velocity - ws.contact_normal * wheel_velocity.dot(ws.contact_normal);
        float ground_speed = ground_velocity.magnitude();
        
        // get lateral and longitudinal velocity components
        float vy = ground_velocity.dot(wheel_lateral);   // lateral velocity (side slip)
        float vx = ground_velocity.dot(wheel_forward);   // longitudinal velocity
        
        // calculate slip angle (angle between wheel heading and travel direction)
        // slip angle is what generates lateral force in real tires
        if (ground_speed > min_speed_for_slip)
        {
            // proper slip angle: arctan(vy/vx)
            // positive slip angle = wheel pointing more inward than travel direction
            ws.slip_angle = atan2f(vy, fabsf(vx) + 0.1f);
        }
        else
        {
            // at very low speeds, use velocity-proportional model to avoid instability
            ws.slip_angle = vy * 0.5f; // small angle approximation
        }
        
        // simple viscous tire model: force opposes lateral velocity
        // this is stable and predictable - the force directly fights sideways motion
        float max_lateral_force = tire_friction_coeff * ws.tire_load;
        
        // lateral stiffness scales with tire load for realistic feel
        float lateral_stiffness = 12000.0f * (ws.tire_load / 4000.0f);
        lateral_stiffness = PxClamp(lateral_stiffness, 4000.0f, 20000.0f);
        
        // force = -stiffness * lateral_velocity, clamped to grip limit
        float raw_lateral_force = -vy * lateral_stiffness;
        ws.lateral_force = PxClamp(raw_lateral_force, -max_lateral_force, max_lateral_force);
        
        // slip ratio for longitudinal forces (acceleration/braking)
        // slip ratio = (wheel_speed - ground_speed) / max(wheel_speed, ground_speed)
        // for now we don't track wheel angular velocity, so estimate from throttle/brake
        ws.slip_ratio = 0.0f; // will be set by drive force application
        ws.longitudinal_force = 0.0f; // drive forces handled separately in tick()
    }
    
    // apply tire forces for all wheels
    inline void apply_tire_forces(PxRigidDynamic* body, float steering_angle)
    {
        PxTransform pose = body->getGlobalPose();
        PxVec3 wheel_positions[wheel_count];
        compute_wheel_positions(active_dims, wheel_positions);
        
        PxVec3 chassis_forward = pose.q.rotate(PxVec3(0, 0, 1));
        PxVec3 chassis_right   = pose.q.rotate(PxVec3(1, 0, 0));
        
        for (int i = 0; i < wheel_count; i++)
        {
            wheel_state& ws = wheel_states[i];
            
            if (!ws.is_grounded || ws.tire_load <= 0.0f)
                continue;
            
            // get wheel orientation
            float wheel_angle = 0.0f;
            bool is_front     = (i == front_left || i == front_right);
            if (is_front)
                wheel_angle = steering_angle;
            
            // wheel lateral direction
            float cos_steer      = cosf(wheel_angle);
            float sin_steer      = sinf(wheel_angle);
            PxVec3 wheel_lateral = chassis_right * cos_steer - chassis_forward * sin_steer;
            
            // apply lateral force
            PxVec3 tire_force      = wheel_lateral * ws.lateral_force;
            PxVec3 world_wheel_pos = pose.transform(wheel_positions[i]);
            PxRigidBodyExt::addForceAtPos(*body, tire_force, world_wheel_pos, PxForceMode::eFORCE);
        }
    }

    //=======================================================================================
    // main update
    //=======================================================================================
    
    // tick function to update vehicle physics (called from physics::tick)
    inline void tick(float delta_time)
    {
        if (!active_vehicle || !active_vehicle->physx_actor.rigidBody)
            return;
        
        PxRigidDynamic* body = active_vehicle->physx_actor.rigidBody->is<PxRigidDynamic>();
        if (!body)
            return;
        
        // lerp inputs for smoother control
        float lerp_factor = 1.0f - expf(-input_smoothing * delta_time);
        float steer_lerp  = 1.0f - expf(-steering_smoothing * delta_time);
        current_input.throttle = current_input.throttle + (target_input.throttle - current_input.throttle) * lerp_factor;
        current_input.brake    = current_input.brake + (target_input.brake - current_input.brake) * lerp_factor;
        current_input.steering = current_input.steering + (target_input.steering - current_input.steering) * steer_lerp;
        
        // get vehicle's local directions
        PxTransform pose = body->getGlobalPose();
        PxVec3 forward   = pose.q.rotate(PxVec3(0, 0, 1));
        PxVec3 right     = pose.q.rotate(PxVec3(1, 0, 0));
        
        // get current velocity info
        PxVec3 velocity         = body->getLinearVelocity();
        float forward_speed     = velocity.dot(forward);
        float speed             = velocity.magnitude();
        float speed_kmh         = speed * 3.6f;
        float forward_speed_kmh = forward_speed * 3.6f;
        
        // wheel positions for force application
        PxVec3 wheel_positions[wheel_count];
        compute_wheel_positions(active_dims, wheel_positions);
        
        // apply driving force at rear wheels (rwd) for proper weight transfer
        // weight transfers to rear under acceleration, giving rear wheels more grip
        if (current_input.throttle > 0.01f)
        {
            // engine power curve: peak torque at mid rpm, falls off at high rpm
            // approximated as power reduction at high speeds
            float power_factor = 1.0f - PxClamp(speed_kmh / 250.0f, 0.0f, 0.85f);
            float drive_force_magnitude = max_engine_force * current_input.throttle * power_factor;
            
            // distribute to rear wheels (for rwd - adjust for fwd/awd as needed)
            // only apply force if wheel is grounded
            for (int i = rear_left; i <= rear_right; i++)
            {
                if (wheel_states[i].is_grounded && wheel_states[i].tire_load > 0.0f)
                {
                    PxVec3 force_per_wheel = forward * (drive_force_magnitude * 0.5f);
                    PxVec3 world_pos = pose.transform(wheel_positions[i]);
                    PxRigidBodyExt::addForceAtPos(*body, force_per_wheel, world_pos, PxForceMode::eFORCE);
                }
            }
        }
        
        // apply braking at all wheels with front bias for stability
        // weight transfers forward under braking, front wheels get more grip
        if (current_input.brake > 0.01f)
        {
            if (forward_speed_kmh > 3.0f)
            {
                // moving forward: apply brakes with front bias
                float total_brake = max_brake_force * current_input.brake;
                float front_brake = total_brake * brake_bias_front * 0.5f;
                float rear_brake  = total_brake * (1.0f - brake_bias_front) * 0.5f;
                
                // front wheels
                for (int i = front_left; i <= front_right; i++)
                {
                    if (wheel_states[i].is_grounded)
                    {
                        PxVec3 brake_force = -forward * front_brake;
                        PxVec3 world_pos = pose.transform(wheel_positions[i]);
                        PxRigidBodyExt::addForceAtPos(*body, brake_force, world_pos, PxForceMode::eFORCE);
                    }
                }
                // rear wheels
                for (int i = rear_left; i <= rear_right; i++)
                {
                    if (wheel_states[i].is_grounded)
                    {
                        PxVec3 brake_force = -forward * rear_brake;
                        PxVec3 world_pos = pose.transform(wheel_positions[i]);
                        PxRigidBodyExt::addForceAtPos(*body, brake_force, world_pos, PxForceMode::eFORCE);
                    }
                }
            }
            else
            {
                // stopped or already reversing: apply reverse thrust (max ~50 km/h reverse)
                float reverse_speed_kmh = PxMax(-forward_speed_kmh, 0.0f);
                float reverse_power = 1.0f - PxClamp(reverse_speed_kmh / 50.0f, 0.0f, 0.9f);
                float reverse_force = max_engine_force * 0.5f * current_input.brake * reverse_power;
                
                // apply at rear wheels
                for (int i = rear_left; i <= rear_right; i++)
                {
                    if (wheel_states[i].is_grounded)
                    {
                        PxVec3 force = -forward * (reverse_force * 0.5f);
                        PxVec3 world_pos = pose.transform(wheel_positions[i]);
                        PxRigidBodyExt::addForceAtPos(*body, force, world_pos, PxForceMode::eFORCE);
                    }
                }
            }
        }
        
        // steering angle - minimal speed reduction for responsive handling
        const float max_steering_angle = 40.0f * (PxPi / 180.0f);
        float steering_reduction = 1.0f;
        if (speed_kmh > 80.0f)
        {
            // gentle reduction: 100% at 80km/h down to 70% at 200km/h
            steering_reduction = 1.0f - 0.3f * PxClamp((speed_kmh - 80.0f) / 120.0f, 0.0f, 1.0f);
        }
        float steering_angle = current_input.steering * max_steering_angle * steering_reduction;
        
        // aerodynamic drag: f = 0.5 * rho * cd * a * v^2
        // this is the correct physics formula for air resistance
        if (speed > 1.0f)
        {
            float drag_force = 0.5f * air_density * drag_coefficient * frontal_area * speed * speed;
            PxVec3 drag = -velocity.getNormalized() * drag_force;
            body->addForce(drag, PxForceMode::eFORCE);
        }
        
        // rolling resistance: proportional to normal load (weight on tires)
        // f = crr * n, where n is normal force
        float total_tire_load = 0.0f;
        for (int i = 0; i < wheel_count; i++)
        {
            if (wheel_states[i].is_grounded)
                total_tire_load += wheel_states[i].tire_load;
        }
        
        if (speed > 0.1f && total_tire_load > 0.0f)
        {
            float rolling_force = rolling_resistance_coeff * total_tire_load;
            PxVec3 rolling_resistance = -velocity.getNormalized() * rolling_force;
            body->addForce(rolling_resistance, PxForceMode::eFORCE);
        }
        
        // apply gravity manually (vehicle2 requires this since we disabled it on the actor)
        PxVec3 gravity(0.0f, -9.81f * active_vehicle->rigid_body_params.mass, 0.0f);
        body->addForce(gravity, PxForceMode::eFORCE);
        
        // minimal damping - tire forces handle vehicle dynamics
        body->setLinearDamping(0.05f);
        body->setAngularDamping(0.3f);
        
        // suspension and tire simulation
        PxScene* scene = body->getScene();
        if (!scene)
            return;
        
        // pass 1: update all wheel states (raycasts)
        for (int i = 0; i < wheel_count; i++)
            update_wheel_suspension(i, body, scene, delta_time);
        
        // pass 2: apply suspension forces with anti-roll bars
        apply_suspension_forces(body, delta_time);
        
        // pass 3: calculate tire loads from suspension compression
        // this creates proper weight transfer during accel/braking/cornering
        for (int i = 0; i < wheel_count; i++)
        {
            if (wheel_states[i].is_grounded)
            {
                const PxVehicleSuspensionForceParams& force_params = active_vehicle->suspension_force_params[i];
                float displacement = wheel_states[i].suspension_compression * active_dims.suspension_travel;
                wheel_states[i].tire_load = PxMax(force_params.stiffness * displacement, 0.0f);
            }
            else
            {
                wheel_states[i].tire_load = 0.0f;
            }
        }
        
        // pass 4: calculate and apply tire lateral forces (cornering)
        for (int i = 0; i < wheel_count; i++)
            calculate_tire_forces(i, body, steering_angle);
        
        apply_tire_forces(body, steering_angle);
    }

    //=======================================================================================
    // accessors (state queries)
    //=======================================================================================
    
    inline float get_speed_kmh()
    {
        if (!active_vehicle || !active_vehicle->physx_actor.rigidBody)
            return 0.0f;
        
        PxRigidDynamic* body = active_vehicle->physx_actor.rigidBody->is<PxRigidDynamic>();
        if (!body)
            return 0.0f;
        
        return body->getLinearVelocity().magnitude() * 3.6f;
    }
    
    inline float get_steering()           { return current_input.steering; }
    inline float get_throttle()           { return current_input.throttle; }
    inline float get_brake()              { return current_input.brake;    }
    inline float get_suspension_travel()  { return active_dims.suspension_travel; }
    
    inline float get_wheel_compression(int wheel_index)
    {
        return is_valid_wheel_index(wheel_index) ? wheel_states[wheel_index].suspension_compression : 0.0f;
    }
    
    inline bool is_wheel_grounded(int wheel_index)
    {
        return is_valid_wheel_index(wheel_index) ? wheel_states[wheel_index].is_grounded : false;
    }
    
    inline float get_wheel_suspension_force(int wheel_index)
    {
        if (!is_valid_wheel_index(wheel_index) || !active_vehicle)
            return 0.0f;
        
        const wheel_state& ws = wheel_states[wheel_index];
        if (!ws.is_grounded)
            return 0.0f;
        
        const PxVehicleSuspensionForceParams& force_params = active_vehicle->suspension_force_params[wheel_index];
        return force_params.stiffness * ws.suspension_compression * active_dims.suspension_travel;
    }
    
    inline const char* get_wheel_name(int wheel_index)
    {
        static const char* names[] = { "FL", "FR", "RL", "RR" };
        return is_valid_wheel_index(wheel_index) ? names[wheel_index] : "??";
    }
    
    inline float get_wheel_slip_angle(int wheel_index)
    {
        return is_valid_wheel_index(wheel_index) ? wheel_states[wheel_index].slip_angle : 0.0f;
    }
    
    inline float get_wheel_slip_ratio(int wheel_index)
    {
        return is_valid_wheel_index(wheel_index) ? wheel_states[wheel_index].slip_ratio : 0.0f;
    }
    
    inline float get_wheel_tire_load(int wheel_index)
    {
        return is_valid_wheel_index(wheel_index) ? wheel_states[wheel_index].tire_load : 0.0f;
    }
    
    inline float get_wheel_lateral_force(int wheel_index)
    {
        return is_valid_wheel_index(wheel_index) ? wheel_states[wheel_index].lateral_force : 0.0f;
    }
}
