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
    
    // wheel indices for a standard 4-wheel vehicle
    enum wheel_id
    {
        front_left  = 0,
        front_right = 1,
        rear_left   = 2,
        rear_right  = 3,
        wheel_count = 4
    };
    
    // default vehicle dimensions (in meters)
    struct vehicle_dimensions
    {
        float chassis_length     = 4.5f;   // front to back
        float chassis_width      = 2.0f;   // side to side
        float chassis_height     = 0.5f;   // vertical
        float chassis_mass       = 1500.0f;
        float wheel_radius       = 0.35f;
        float wheel_width        = 0.25f;
        float wheel_mass         = 20.0f;
        float suspension_travel  = 0.3f;
        float suspension_height  = 0.4f;   // distance from chassis bottom to wheel center at rest
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
    
    // compute default wheel positions based on dimensions
    inline void compute_wheel_positions(const vehicle_dimensions& dims, PxVec3 out_positions[wheel_count])
    {
        const float front_z = dims.chassis_length * 0.35f;   // front wheels slightly forward
        const float rear_z  = -dims.chassis_length * 0.35f;  // rear wheels slightly back
        const float half_w  = dims.chassis_width * 0.5f - dims.wheel_width * 0.5f;
        const float y       = -dims.suspension_height;       // below chassis
        
        out_positions[front_left]  = PxVec3(-half_w, y, front_z);
        out_positions[front_right] = PxVec3( half_w, y, front_z);
        out_positions[rear_left]   = PxVec3(-half_w, y, rear_z);
        out_positions[rear_right]  = PxVec3( half_w, y, rear_z);
    }
    
    // initialize axle description for 4 wheels (2 axles, 2 wheels each)
    inline void setup_axle_description(PxVehicleAxleDescription& axle_desc)
    {
        axle_desc.setToDefault();
        
        // front axle: wheels 0 and 1
        const PxU32 front_wheels[] = { front_left, front_right };
        axle_desc.addAxle(2, front_wheels);
        
        // rear axle: wheels 2 and 3
        const PxU32 rear_wheels[] = { rear_left, rear_right };
        axle_desc.addAxle(2, rear_wheels);
    }
    
    // initialize rigid body parameters
    inline void setup_rigid_body_params(PxVehicleRigidBodyParams& params, const vehicle_dimensions& dims)
    {
        params.mass = dims.chassis_mass;
        
        // approximate moment of inertia for a box
        const float w = dims.chassis_width;
        const float h = dims.chassis_height;
        const float l = dims.chassis_length;
        const float m = dims.chassis_mass;
        params.moi = PxVec3(
            (m / 12.0f) * (h * h + l * l),  // around x axis
            (m / 12.0f) * (w * w + l * l),  // around y axis (yaw)
            (m / 12.0f) * (w * w + h * h)   // around z axis
        );
    }
    
    // initialize wheel parameters
    inline void setup_wheel_params(PxVehicleWheelParams wheels[wheel_count], const vehicle_dimensions& dims)
    {
        for (int i = 0; i < wheel_count; i++)
        {
            wheels[i].radius      = dims.wheel_radius;
            wheels[i].halfWidth   = dims.wheel_width * 0.5f;
            wheels[i].mass        = dims.wheel_mass;
            wheels[i].moi         = 0.5f * dims.wheel_mass * dims.wheel_radius * dims.wheel_radius;
            wheels[i].dampingRate = 0.25f;
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
            attachment_pos.y += dims.suspension_travel; // move up by travel distance
            
            suspension[i].suspensionAttachment = PxTransform(attachment_pos, PxQuat(PxIdentity));
            suspension[i].suspensionTravelDir  = PxVec3(0.0f, -1.0f, 0.0f); // downward
            suspension[i].suspensionTravelDist = dims.suspension_travel;
            suspension[i].wheelAttachment      = PxTransform(PxVec3(0, 0, 0), PxQuat(PxIdentity));
        }
    }
    
    // initialize suspension force parameters
    inline void setup_suspension_force_params(PxVehicleSuspensionForceParams force[wheel_count], const vehicle_dimensions& dims)
    {
        // sprung mass per wheel (total mass divided roughly by 4, slightly biased)
        const float front_sprung_mass = dims.chassis_mass * 0.55f * 0.5f; // 55% front
        const float rear_sprung_mass  = dims.chassis_mass * 0.45f * 0.5f; // 45% rear
        
        // spring stiffness (natural frequency ~1.5 Hz for stable ride)
        // stiffness = sprung_mass * (2 * pi * freq)^2
        const float freq = 1.5f;
        const float omega_squared = (2.0f * PxPi * freq) * (2.0f * PxPi * freq);
        
        // damping ratio ~0.6 for critically damped (stable, minimal oscillation)
        // real cars are 0.2-0.4 but games need higher for stability
        const float damping_ratio = 0.6f;
        
        for (int i = 0; i < wheel_count; i++)
        {
            const bool is_front = (i == front_left || i == front_right);
            const float sprung_mass = is_front ? front_sprung_mass : rear_sprung_mass;
            
            force[i].sprungMass = sprung_mass;
            force[i].stiffness  = sprung_mass * omega_squared;
            force[i].damping    = 2.0f * damping_ratio * sqrtf(force[i].stiffness * sprung_mass);
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
        // rest load per tire (assumes level ground, even weight distribution for simplicity)
        const float gravity = 9.81f;
        const float total_mass = dims.chassis_mass + dims.wheel_mass * static_cast<float>(wheel_count);
        const float rest_load = (total_mass * gravity) / static_cast<float>(wheel_count);
        
        for (int i = 0; i < wheel_count; i++)
        {
            tires[i].latStiffX    = 2.0f;                  // normalized load for max lat stiffness
            tires[i].latStiffY    = rest_load * 2.0f;      // lateral stiffness at peak
            tires[i].longStiff    = rest_load * 10.0f;     // longitudinal stiffness
            tires[i].camberStiff  = 0.0f;                  // no camber effect for simplicity
            tires[i].restLoad     = rest_load;
            
            // friction vs slip curve (typical road tire)
            tires[i].frictionVsSlip[0][0] = 0.0f;   tires[i].frictionVsSlip[0][1] = 1.0f;   // at zero slip
            tires[i].frictionVsSlip[1][0] = 0.1f;   tires[i].frictionVsSlip[1][1] = 1.1f;   // peak friction at ~10% slip
            tires[i].frictionVsSlip[2][0] = 1.0f;   tires[i].frictionVsSlip[2][1] = 0.9f;   // sliding friction
            
            // load filter (prevents excessive load variations)
            tires[i].loadFilter[0][0] = 0.0f;  tires[i].loadFilter[0][1] = 0.0f;
            tires[i].loadFilter[1][0] = 3.0f;  tires[i].loadFilter[1][1] = 3.0f;
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
        PxTransform initial_pose(PxVec3(0, dims.suspension_height + dims.chassis_height * 0.5f + dims.wheel_radius, 0));
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
        
        // release physx actor
        if (data->physx_actor.rigidBody)
        {
            data->physx_actor.rigidBody->release();
            data->physx_actor.rigidBody = nullptr;
        }
        
        // release material
        if (data->material)
        {
            data->material->release();
            data->material = nullptr;
        }
        
        delete data;
    }
    
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
    };
    
    // active vehicle state
    inline static vehicle_data* active_vehicle = nullptr;
    inline static drive_input current_input;
    inline static drive_input target_input;
    inline static wheel_state wheel_states[wheel_count];
    inline static vehicle_dimensions active_dims;
    
    // driving constants
    const float max_engine_force    = 15000.0f;  // newtons
    const float max_brake_force     = 8000.0f;   // newtons
    const float input_smoothing     = 10.0f;     // throttle/brake response speed
    const float steering_smoothing  = 20.0f;     // steering response speed (faster)
    
    inline void set_active_vehicle(vehicle_data* data, const vehicle_dimensions* dims = nullptr)
    {
        active_vehicle = data;
        current_input = drive_input();
        target_input = drive_input();
        
        // reset wheel states
        for (int i = 0; i < wheel_count; i++)
        {
            wheel_states[i] = wheel_state();
        }
        
        // store dimensions
        if (dims)
        {
            active_dims = *dims;
        }
        else
        {
            active_dims = vehicle_dimensions();
        }
    }
    
    inline vehicle_data* get_active_vehicle()
    {
        return active_vehicle;
    }
    
    // control functions - call these from game code
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
        
    // perform suspension raycast for a single wheel
    inline void update_wheel_suspension(int wheel_index, PxRigidDynamic* body, PxScene* scene, float delta_time)
    {
        PxTransform pose = body->getGlobalPose();
        
        // compute wheel attachment point in world space
        PxVec3 wheel_positions[wheel_count];
        compute_wheel_positions(active_dims, wheel_positions);
        
        // transform wheel position to world space (at rest position)
        PxVec3 local_attach = wheel_positions[wheel_index];
        local_attach.y += active_dims.suspension_travel; // attachment point is at top of suspension travel
        PxVec3 world_attach = pose.transform(local_attach);
        
        // raycast downward from attachment point (use world down, not local)
        PxVec3 ray_dir(0, -1, 0);
        // use a longer ray to ensure we hit ground even when vehicle is high
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
                    ws.is_grounded = true;
                    ws.contact_point = hit.block.position;
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
                    ws.is_grounded = false;
                    ws.suspension_compression = 0.0f;
                }
            }
            else
            {
                ws.is_grounded = false;
                ws.suspension_compression = 0.0f;
            }
        }
        else
        {
            ws.is_grounded = false;
            ws.suspension_compression = 0.0f;
        }
    }
    
    // anti-roll bar stiffness (prevents excessive body roll)
    inline constexpr float anti_roll_stiffness = 15000.0f; // N/m
    
    // maximum suspension force per wheel (prevents explosive forces on hard impacts)
    inline constexpr float max_suspension_force = 25000.0f; // N
    
    // calculate suspension force for a single wheel (doesn't apply it)
    inline float calculate_wheel_force(int wheel_index, float delta_time)
    {
        wheel_state& ws = wheel_states[wheel_index];
        
        if (!ws.is_grounded)
            return 0.0f;
        
        // get suspension parameters for this wheel
        const PxVehicleSuspensionForceParams& force_params = active_vehicle->suspension_force_params[wheel_index];
        
        // spring force: F = stiffness * displacement
        float displacement = ws.suspension_compression * active_dims.suspension_travel;
        float spring_force = force_params.stiffness * displacement;
        
        // damper force: F = damping * velocity
        // when compressing (velocity > 0), damper resists by adding upward force
        // when extending (velocity < 0), damper resists by reducing upward force
        float compression_velocity = (ws.suspension_compression - ws.previous_compression) / delta_time;
        compression_velocity = PxClamp(compression_velocity, -5.0f, 5.0f); // limit velocity to prevent spikes
        float damper_force = force_params.damping * compression_velocity * active_dims.suspension_travel;
        
        // total suspension force (spring + damper, both resist motion)
        float total_force = spring_force + damper_force;
        total_force = PxClamp(total_force, 0.0f, max_suspension_force); // suspension can only push, with limit
        
        return total_force;
    }
    
    // apply all suspension forces with anti-roll bars
    inline void apply_suspension_forces(PxRigidDynamic* body, float delta_time)
    {
        if (!active_vehicle)
            return;
        
        // calculate base suspension forces for all wheels
        float forces[wheel_count];
        for (int i = 0; i < wheel_count; i++)
        {
            forces[i] = calculate_wheel_force(i, delta_time);
        }
        
        // anti-roll bar: front axle (couples FL and FR)
        {
            float compression_diff = wheel_states[front_left].suspension_compression - wheel_states[front_right].suspension_compression;
            float anti_roll_force = compression_diff * anti_roll_stiffness * active_dims.suspension_travel;
            
            if (wheel_states[front_left].is_grounded)
                forces[front_left] -= anti_roll_force;
            if (wheel_states[front_right].is_grounded)
                forces[front_right] += anti_roll_force;
        }
        
        // anti-roll bar: rear axle (couples RL and RR)
        {
            float compression_diff = wheel_states[rear_left].suspension_compression - wheel_states[rear_right].suspension_compression;
            float anti_roll_force = compression_diff * anti_roll_stiffness * active_dims.suspension_travel;
            
            if (wheel_states[rear_left].is_grounded)
                forces[rear_left] -= anti_roll_force;
            if (wheel_states[rear_right].is_grounded)
                forces[rear_right] += anti_roll_force;
        }
        
        // clamp final forces
        for (int i = 0; i < wheel_count; i++)
        {
            forces[i] = PxClamp(forces[i], 0.0f, max_suspension_force);
        }
        
        // apply forces at wheel positions
        PxTransform pose = body->getGlobalPose();
        PxVec3 wheel_positions[wheel_count];
        compute_wheel_positions(active_dims, wheel_positions);
        
        for (int i = 0; i < wheel_count; i++)
        {
            if (forces[i] > 0.0f && wheel_states[i].is_grounded)
            {
                PxVec3 force_dir = wheel_states[i].contact_normal;
                PxVec3 force_vec = force_dir * forces[i];
                PxVec3 world_wheel_pos = pose.transform(wheel_positions[i]);
                
                PxRigidBodyExt::addForceAtPos(*body, force_vec, world_wheel_pos, PxForceMode::eFORCE);
            }
        }
    }
    
    // legacy single-wheel function (kept for compatibility, but use apply_suspension_forces instead)
    inline void apply_wheel_forces(int wheel_index, PxRigidDynamic* body, float delta_time)
    {
        // now handled by apply_suspension_forces for proper anti-roll bar support
    }
    
    // tick function to update vehicle physics (called from Physics::Tick)
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
        PxVec3 forward = pose.q.rotate(PxVec3(0, 0, 1));  // z is forward
        PxVec3 right   = pose.q.rotate(PxVec3(1, 0, 0));  // x is right
        PxVec3 up      = PxVec3(0, 1, 0);                 // world up
        
        // get current velocity info
        PxVec3 velocity = body->getLinearVelocity();
        float forward_speed = velocity.dot(forward);
        float lateral_speed = velocity.dot(right);
        float speed = velocity.magnitude();
        
        // convert to km/h for intuitive tuning
        float speed_kmh = speed * 3.6f;
        float forward_speed_kmh = forward_speed * 3.6f;
        
        // apply driving force (forward) - with speed-dependent reduction for realistic top speed
        if (current_input.throttle > 0.01f)
        {
            // reduce power at high speeds (simulates air resistance and engine power curve)
            float power_factor = 1.0f - PxClamp(speed_kmh / 250.0f, 0.0f, 0.85f);
            PxVec3 drive_force = forward * max_engine_force * current_input.throttle * power_factor;
            body->addForce(drive_force, PxForceMode::eFORCE);
        }
        
        // apply braking/reverse
        if (current_input.brake > 0.01f)
        {
            if (forward_speed_kmh > 5.0f)
            {
                // moving forward, apply brakes
                PxVec3 brake_force = -forward * max_brake_force * current_input.brake;
                body->addForce(brake_force, PxForceMode::eFORCE);
            }
            else if (forward_speed_kmh > 1.0f)
            {
                // moving forward slowly, stronger brakes to stop completely
                PxVec3 brake_force = -forward * max_brake_force * 1.5f * current_input.brake;
                body->addForce(brake_force, PxForceMode::eFORCE);
            }
            else
            {
                // stopped or reversing - apply reverse thrust (max ~60 km/h reverse)
                float reverse_speed_kmh = -forward_speed_kmh;
                float reverse_power = 1.0f - PxClamp(reverse_speed_kmh / 60.0f, 0.0f, 0.9f);
                PxVec3 reverse_force = -forward * max_engine_force * 0.8f * current_input.brake * reverse_power;
                body->addForce(reverse_force, PxForceMode::eFORCE);
            }
        }
        
        // steering - requires some forward motion to turn (prevents spinning in place)
        if (fabsf(current_input.steering) > 0.01f)
        {
            float abs_speed_kmh = fabsf(forward_speed_kmh);
            
            // need at least 2 km/h to steer at all
            if (abs_speed_kmh > 2.0f)
            {
                float base_steer_torque = 40000.0f;
                
                // steering ramps up from 2 km/h to 10 km/h, then reduces at high speeds
                float speed_factor = 1.0f;
                if (abs_speed_kmh < 10.0f)
                {
                    // ramp up: 0.3 at 2 km/h, 1.0 at 10 km/h
                    speed_factor = 0.3f + 0.7f * (abs_speed_kmh - 2.0f) / 8.0f;
                }
                else if (abs_speed_kmh > 80.0f)
                {
                    // reduce at high speeds to prevent sudden spins
                    speed_factor = 1.0f - 0.5f * PxClamp((abs_speed_kmh - 80.0f) / 100.0f, 0.0f, 1.0f);
                }
                
                float steer_torque = current_input.steering * base_steer_torque * speed_factor;
                
                // invert steering when reversing
                if (forward_speed < 0.0f)
                    steer_torque = -steer_torque;
                
                body->addTorque(up * steer_torque, PxForceMode::eFORCE);
            }
        }
        
        // lateral friction - prevents sliding sideways (tire grip)
        float lateral_friction_coeff = 6000.0f;
        PxVec3 lateral_friction = -right * lateral_speed * lateral_friction_coeff;
        body->addForce(lateral_friction, PxForceMode::eFORCE);
        
        // rolling resistance (small constant drag)
        if (speed > 0.5f)
        {
            PxVec3 rolling_resistance = -velocity.getNormalized() * 300.0f;
            body->addForce(rolling_resistance, PxForceMode::eFORCE);
        }
        
        // apply gravity manually (vehicle2 requires this since we disabled it on the actor)
        PxVec3 gravity(0.0f, -9.81f * active_vehicle->rigid_body_params.mass, 0.0f);
        body->addForce(gravity, PxForceMode::eFORCE);
        
        // low linear damping for higher top speed, higher angular damping for stability
        body->setLinearDamping(0.1f);
        body->setAngularDamping(3.0f);
        
        // suspension simulation - update wheel states then apply all forces together
        PxScene* scene = body->getScene();
        if (scene)
        {
            // first pass: update all wheel states (raycasts)
            for (int i = 0; i < wheel_count; i++)
            {
                update_wheel_suspension(i, body, scene, delta_time);
            }
            
            // second pass: apply forces with anti-roll bars
            apply_suspension_forces(body, delta_time);
        }
    }
    
    // get current speed for display
    inline float get_speed_kmh()
    {
        if (!active_vehicle || !active_vehicle->physx_actor.rigidBody)
            return 0.0f;
        
        PxRigidDynamic* body = active_vehicle->physx_actor.rigidBody->is<PxRigidDynamic>();
        if (!body)
            return 0.0f;
        
        PxVec3 velocity = body->getLinearVelocity();
        float speed_ms = velocity.magnitude();
        return speed_ms * 3.6f; // m/s to km/h
    }
    
    // get current steering for display
    inline float get_steering()
    {
        return current_input.steering;
    }
    
    // get suspension compression for a wheel (0 = extended, 1 = compressed)
    inline float get_wheel_compression(int wheel_index)
    {
        if (wheel_index >= 0 && wheel_index < wheel_count)
        {
            return wheel_states[wheel_index].suspension_compression;
        }
        return 0.0f;
    }
    
    // check if wheel is grounded
    inline bool is_wheel_grounded(int wheel_index)
    {
        if (wheel_index >= 0 && wheel_index < wheel_count)
        {
            return wheel_states[wheel_index].is_grounded;
        }
        return false;
    }
    
    // get suspension travel distance
    inline float get_suspension_travel()
    {
        return active_dims.suspension_travel;
    }
    
    // get current throttle input (0-1)
    inline float get_throttle()
    {
        return current_input.throttle;
    }
    
    // get current brake input (0-1)
    inline float get_brake()
    {
        return current_input.brake;
    }
    
    // get suspension force for a wheel (newtons)
    inline float get_wheel_suspension_force(int wheel_index)
    {
        if (wheel_index < 0 || wheel_index >= wheel_count || !active_vehicle)
            return 0.0f;
        
        const wheel_state& ws = wheel_states[wheel_index];
        if (!ws.is_grounded)
            return 0.0f;
        
        const PxVehicleSuspensionForceParams& force_params = active_vehicle->suspension_force_params[wheel_index];
        float spring_force = force_params.stiffness * ws.suspension_compression * active_dims.suspension_travel;
        return spring_force;
    }
    
    // get wheel name for display
    inline const char* get_wheel_name(int wheel_index)
    {
        static const char* names[] = { "FL", "FR", "RL", "RR" };
        if (wheel_index >= 0 && wheel_index < wheel_count)
            return names[wheel_index];
        return "??";
    }
}
