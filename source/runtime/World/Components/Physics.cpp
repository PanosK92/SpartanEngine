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

//= INCLUDES =================================
#include "pch.h"
#include "Physics.h"
#include "Renderable.h"
#include "Camera.h"
#include "../Entity.h"
#include "../../RHI/RHI_Vertex.h"
#include "../../Physics/PhysicsWorld.h"
#include "../../Geometry/GeometryProcessing.h"
SP_WARNINGS_OFF
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
#include "../IO/pugixml.hpp"
SP_WARNINGS_ON
//============================================

//= NAMESPACES ===============
using namespace std;
using namespace spartan::math;
using namespace physx;
//============================

namespace spartan
{
    namespace vehicle
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
        void compute_wheel_positions(const vehicle_dimensions& dims, PxVec3 out_positions[wheel_count])
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
        void setup_axle_description(PxVehicleAxleDescription& axle_desc)
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
        void setup_rigid_body_params(PxVehicleRigidBodyParams& params, const vehicle_dimensions& dims)
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
        void setup_wheel_params(PxVehicleWheelParams wheels[wheel_count], const vehicle_dimensions& dims)
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
        void setup_suspension_params(PxVehicleSuspensionParams suspension[wheel_count], const vehicle_dimensions& dims)
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
        void setup_suspension_force_params(PxVehicleSuspensionForceParams force[wheel_count], const vehicle_dimensions& dims)
        {
            // sprung mass per wheel (total mass divided roughly by 4, slightly biased)
            const float front_sprung_mass = dims.chassis_mass * 0.55f * 0.5f; // 55% front
            const float rear_sprung_mass  = dims.chassis_mass * 0.45f * 0.5f; // 45% rear
            
            // spring stiffness (natural frequency ~2 Hz for comfortable ride)
            // stiffness = sprung_mass * (2 * pi * freq)^2
            const float freq = 2.0f;
            const float omega_squared = (2.0f * PxPi * freq) * (2.0f * PxPi * freq);
            
            // damping ratio ~0.3 for street car
            const float damping_ratio = 0.3f;
            
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
        void setup_suspension_compliance_params(PxVehicleSuspensionComplianceParams compliance[wheel_count])
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
        void setup_tire_params(PxVehicleTireForceParams tires[wheel_count], const vehicle_dimensions& dims)
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
        bool create_physx_actor(vehicle_data& data, const vehicle_dimensions& dims, PxPhysics* physics, PxScene* scene)
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
                    
                    // wheel shapes should not participate in simulation (just visual/query)
                    wheel_shape->setFlag(PxShapeFlag::eSIMULATION_SHAPE, false);
                    wheel_shape->setFlag(PxShapeFlag::eSCENE_QUERY_SHAPE, true);
                    
                    rigid_body->attachShape(*wheel_shape);
                    data.physx_actor.wheelShapes[i] = wheel_shape;
                    wheel_shape->release();
                }
            }
            
            return true;
        }
        
        // main setup function: creates a basic 4-wheel vehicle
        vehicle_data* setup(PxPhysics* physics, PxScene* scene, const vehicle_dimensions* custom_dims = nullptr)
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
        void destroy(vehicle_data* data)
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
        
        // active vehicle state
        static vehicle_data* active_vehicle = nullptr;
        static drive_input current_input;
        static drive_input target_input;
        
        // driving constants
        const float max_engine_force    = 15000.0f;  // newtons
        const float max_brake_force     = 8000.0f;   // newtons
        const float input_smoothing     = 10.0f;     // throttle/brake response speed
        const float steering_smoothing  = 20.0f;     // steering response speed (faster)
        
        void set_active_vehicle(vehicle_data* data)
        {
            active_vehicle = data;
            current_input = drive_input();
            target_input = drive_input();
        }
        
        vehicle_data* get_active_vehicle()
        {
            return active_vehicle;
        }
        
        // control functions - call these from game code
        void set_throttle(float value)
        {
            target_input.throttle = PxClamp(value, 0.0f, 1.0f);
        }
        
        void set_brake(float value)
        {
            target_input.brake = PxClamp(value, 0.0f, 1.0f);
        }
        
        void set_steering(float value)
        {
            target_input.steering = PxClamp(value, -1.0f, 1.0f);
        }
        
        // tick function to update vehicle physics (called from Physics::Tick)
        void tick(float delta_time)
        {
            if (!active_vehicle || !active_vehicle->physx_actor.rigidBody)
                return;
            
            PxRigidDynamic* body = active_vehicle->physx_actor.rigidBody->is<PxRigidDynamic>();
            if (!body)
                return;
            
            // lerp inputs for smoother control (steering is faster)
            float lerp_factor = 1.0f - expf(-input_smoothing * delta_time);
            float steer_lerp  = 1.0f - expf(-steering_smoothing * delta_time);
            current_input.throttle = current_input.throttle + (target_input.throttle - current_input.throttle) * lerp_factor;
            current_input.brake    = current_input.brake + (target_input.brake - current_input.brake) * lerp_factor;
            current_input.steering = current_input.steering + (target_input.steering - current_input.steering) * steer_lerp;
            
            // get vehicle's local directions
            PxTransform pose = body->getGlobalPose();
            PxVec3 forward = pose.q.rotate(PxVec3(0, 0, 1));  // z is forward
            PxVec3 up      = PxVec3(0, 1, 0);                 // world up for steering
            
            // get current velocity info
            PxVec3 velocity = body->getLinearVelocity();
            float forward_speed = velocity.dot(forward);
            float speed = velocity.magnitude();
            
            // apply driving force (forward)
            if (current_input.throttle > 0.01f)
            {
                PxVec3 drive_force = forward * max_engine_force * current_input.throttle;
                body->addForce(drive_force, PxForceMode::eFORCE);
            }
            
            // apply braking/reverse
            if (current_input.brake > 0.01f)
            {
                if (forward_speed > 1.0f)
                {
                    // moving forward significantly, apply brakes (opposing velocity)
                    PxVec3 brake_force = -forward * max_brake_force * current_input.brake;
                    body->addForce(brake_force, PxForceMode::eFORCE);
                }
                else if (forward_speed > 0.1f)
                {
                    // moving forward slowly, stronger braking to stop
                    PxVec3 brake_force = -forward * max_brake_force * 2.0f * current_input.brake;
                    body->addForce(brake_force, PxForceMode::eFORCE);
                }
                else
                {
                    // stopped or moving backward, apply reverse thrust
                    PxVec3 reverse_force = -forward * max_engine_force * 0.6f * current_input.brake;
                    body->addForce(reverse_force, PxForceMode::eFORCE);
                }
            }
            
            // apply steering torque
            if (fabsf(current_input.steering) > 0.01f)
            {
                // strong steering torque for responsive turning
                float steer_torque = current_input.steering * 100000.0f;
                
                // scale with speed: more effective at higher speeds, but still works when slow
                if (speed > 0.1f)
                {
                    float speed_factor = PxClamp(speed / 10.0f, 0.5f, 2.0f);
                    steer_torque *= speed_factor;
                    
                    // invert steering when reversing
                    if (forward_speed < -0.1f)
                        steer_torque = -steer_torque;
                }
                else
                {
                    // when nearly stopped, allow some steering but reduced
                    steer_torque *= 0.5f;
                }
                
                body->addTorque(up * steer_torque, PxForceMode::eFORCE);
            }
            
            // apply gravity manually (vehicle2 requires this)
            PxVec3 gravity(0.0f, -9.81f * active_vehicle->rigid_body_params.mass, 0.0f);
            body->addForce(gravity, PxForceMode::eFORCE);
            
            // damping for stability (low angular damping for responsive steering)
            body->setLinearDamping(0.5f);
            body->setAngularDamping(0.5f);
        }
        
        // get current speed for display
        float get_speed_kmh()
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
        float get_steering()
        {
            return current_input.steering;
        }
    }

    namespace
    {
        const float distance_deactivate = 80.0f;
        const float distance_activate   = 40.0f;
        const float standing_height     = 1.8f;
        const float crouch_height       = 0.7f;

        // derivatives
        const float distance_deactivate_squared = distance_deactivate * distance_deactivate;
        const float distance_activate_squared   = distance_activate * distance_activate;

        PxControllerManager* controller_manager = nullptr;

        // helper to build lock flags from position and rotation lock vectors
        PxRigidDynamicLockFlags build_lock_flags(const Vector3& position_lock, const Vector3& rotation_lock)
        {
            PxRigidDynamicLockFlags flags = PxRigidDynamicLockFlags(0);
            if (position_lock.x) flags |= PxRigidDynamicLockFlag::eLOCK_LINEAR_X;
            if (position_lock.y) flags |= PxRigidDynamicLockFlag::eLOCK_LINEAR_Y;
            if (position_lock.z) flags |= PxRigidDynamicLockFlag::eLOCK_LINEAR_Z;
            if (rotation_lock.x) flags |= PxRigidDynamicLockFlag::eLOCK_ANGULAR_X;
            if (rotation_lock.y) flags |= PxRigidDynamicLockFlag::eLOCK_ANGULAR_Y;
            if (rotation_lock.z) flags |= PxRigidDynamicLockFlag::eLOCK_ANGULAR_Z;
            return flags;
        }
    }

    Physics::Physics(Entity* entity) : Component(entity)
    {
        SP_REGISTER_ATTRIBUTE_VALUE_VALUE(m_is_static, bool);
        SP_REGISTER_ATTRIBUTE_VALUE_VALUE(m_is_kinematic, bool);
        SP_REGISTER_ATTRIBUTE_VALUE_VALUE(m_mass, float);
        SP_REGISTER_ATTRIBUTE_VALUE_VALUE(m_friction, float);
        SP_REGISTER_ATTRIBUTE_VALUE_VALUE(m_friction_rolling, float);
        SP_REGISTER_ATTRIBUTE_VALUE_VALUE(m_restitution, float);
        SP_REGISTER_ATTRIBUTE_VALUE_VALUE(m_position_lock, Vector3);
        SP_REGISTER_ATTRIBUTE_VALUE_VALUE(m_rotation_lock, Vector3);
        SP_REGISTER_ATTRIBUTE_VALUE_VALUE(m_center_of_mass, Vector3);
        SP_REGISTER_ATTRIBUTE_VALUE_VALUE(m_velocity, Vector3);
        SP_REGISTER_ATTRIBUTE_VALUE_VALUE(m_controller, void*);
        SP_REGISTER_ATTRIBUTE_VALUE_VALUE(m_material, void*);
        SP_REGISTER_ATTRIBUTE_VALUE_VALUE(m_mesh, void*);
        SP_REGISTER_ATTRIBUTE_VALUE_VALUE(m_actors, vector<void*>);
        SP_REGISTER_ATTRIBUTE_VALUE_SET(m_body_type, SetBodyType, BodyType);
    }

    Physics::~Physics()
    {
        Remove();
    }

    void Physics::Initialize()
    {
        Component::Initialize();
    }

    void Physics::Shutdown()
    {
        // release the controller manager (created lazily when first controller is made)
        if (controller_manager)
        {
            controller_manager->release();
            controller_manager = nullptr;
        }
    }

    void Physics::Remove()
    {
        if (m_controller)
        {
            static_cast<PxController*>(m_controller)->release();
            m_controller = nullptr;
            
            // release the material that was created for this controller
            if (m_material)
            {
                static_cast<PxMaterial*>(m_material)->release();
                m_material = nullptr;
            }
        }

        for (auto* body : m_actors)
        {
            if (body)
            {
                PxRigidActor* actor = static_cast<PxRigidActor*>(body);
                PhysicsWorld::RemoveActor(actor);
                actor->release();
            }
        }
        m_actors.clear();
        m_actors_active.clear();

        if (PxMaterial* material = static_cast<PxMaterial*>(m_material))
        {
            material->release();
            m_material = nullptr;
        }
    }

    void Physics::Tick()
    {
        // map transform from physx to engine and vice versa
        if (m_body_type == BodyType::Controller)
        {
            if (Engine::IsFlagSet(EngineMode::Playing))
            {
                // compute gravitational acceleration
                float delta_time  = static_cast<float>(Timer::GetDeltaTimeSec());
                m_velocity.y     += PhysicsWorld::GetGravity().y * delta_time;
                PxVec3 displacement(0.0f, m_velocity.y * delta_time, 0.0f);

                // if there is a collision below, zero out the vertical velocity
                PxControllerFilters filters;
                filters.mFilterFlags = PxQueryFlag::eSTATIC | PxQueryFlag::eDYNAMIC;
                PxControllerCollisionFlags collision_flags = static_cast<PxCapsuleController*>(m_controller)->move(displacement, 0.001f, delta_time, filters);
                if (collision_flags & PxControllerCollisionFlag::eCOLLISION_DOWN)
                {
                    m_velocity.y = 0.0f;
                }

                // set new position to entity
                PxExtendedVec3 pos_ext = static_cast<PxCapsuleController*>(m_controller)->getPosition();
                Vector3 pos_previous   = GetEntity()->GetPosition();
                Vector3 pos            = Vector3(static_cast<float>(pos_ext.x), static_cast<float>(pos_ext.y), static_cast<float>(pos_ext.z));
                GetEntity()->SetPosition(pos);

                // compute velocity for xz
                if (delta_time > 0.0f)
                {
                    m_velocity.x = (pos.x - pos_previous.x) / delta_time;
                    m_velocity.z = (pos.z - pos_previous.z) / delta_time;
                }
            }
            else
            {
                Vector3 entity_pos = GetEntity()->GetPosition();
                static_cast<PxCapsuleController*>(m_controller)->setPosition(PxExtendedVec3(entity_pos.x, entity_pos.y, entity_pos.z));
                m_velocity = Vector3::Zero;
            }
        }
        else if (m_body_type == BodyType::Vehicle)
        {
            if (Engine::IsFlagSet(EngineMode::Playing))
            {
                // update vehicle physics (input is set externally via vehicle::set_throttle/brake/steering)
                float delta_time = static_cast<float>(Timer::GetDeltaTimeSec());
                vehicle::tick(delta_time);
                
                // sync physx -> entity
                if (!m_actors.empty() && m_actors[0])
                {
                    PxRigidActor* actor = static_cast<PxRigidActor*>(m_actors[0]);
                    PxTransform pose = actor->getGlobalPose();
                    GetEntity()->SetPosition(Vector3(pose.p.x, pose.p.y, pose.p.z));
                    GetEntity()->SetRotation(Quaternion(pose.q.x, pose.q.y, pose.q.z, pose.q.w));
                }

                // update wheel entity transforms (spin and steering)
                UpdateWheelTransforms();
            }
            else
            {
                // editor mode: sync entity -> physx, reset velocities
                if (!m_actors.empty() && m_actors[0])
                {
                    Vector3 pos = GetEntity()->GetPosition();
                    Quaternion rot = GetEntity()->GetRotation();
                    PxTransform pose(
                        PxVec3(pos.x, pos.y, pos.z),
                        PxQuat(rot.x, rot.y, rot.z, rot.w)
                    );
                    
                    PxRigidActor* actor = static_cast<PxRigidActor*>(m_actors[0]);
                    actor->setGlobalPose(pose);
                    
                    if (PxRigidDynamic* dynamic = actor->is<PxRigidDynamic>())
                    {
                        dynamic->setLinearVelocity(PxVec3(0, 0, 0));
                        dynamic->setAngularVelocity(PxVec3(0, 0, 0));
                    }
                }
            }
        }
        else if (!m_is_static)
        {
            Renderable* renderable = GetEntity()->GetComponent<Renderable>();
            if (!renderable)
                return;

            for (uint32_t i = 0; i < m_actors.size(); i++)
            {
                if (!m_actors[i])
                    continue;
                    
                PxRigidActor* actor     = static_cast<PxRigidActor*>(m_actors[i]);
                PxRigidDynamic* dynamic = actor->is<PxRigidDynamic>();
                if (Engine::IsFlagSet(EngineMode::Playing))
                {
                    if (m_is_kinematic && dynamic)
                    {
                        // sync entity -> physX (kinematic target)
                        math::Matrix transform;
                        if (renderable->HasInstancing() && i < renderable->GetInstanceCount())
                        {
                            transform = renderable->GetInstance(i, true);
                        }
                        else if (i == 0)
                        {
                            transform = GetEntity()->GetMatrix();
                        }
                        else
                        {
                            continue;
                        }
                        PxTransform target(
                            PxVec3(transform.GetTranslation().x, transform.GetTranslation().y, transform.GetTranslation().z),
                            PxQuat(transform.GetRotation().x, transform.GetRotation().y, transform.GetRotation().z, transform.GetRotation().w)
                        );
                        dynamic->setKinematicTarget(target);
                    }
                    else
                    {
                        // sync physx -> entity (simulated dynamic)
                        PxTransform pose = actor->getGlobalPose();
                        math::Matrix transform = math::Matrix::CreateTranslation(Vector3(pose.p.x, pose.p.y, pose.p.z)) * math::Matrix::CreateRotation(Quaternion(pose.q.x, pose.q.y, pose.q.z, pose.q.w));
                        if (renderable->HasInstancing() && i < renderable->GetInstanceCount())
                        {
                            //renderable->SetInstance(static_cast<uint32_t>(i), transform); // implement if needed
                        }
                        else if (i == 0)
                        {
                            GetEntity()->SetPosition(Vector3(pose.p.x, pose.p.y, pose.p.z));
                            GetEntity()->SetRotation(Quaternion(pose.q.x, pose.q.y, pose.q.z, pose.q.w));
                        }
                    }
                }
                else
                {
                    // editor mode: sync entity -> physx, reset velocities only for non-kinematics
                    math::Matrix transform;
                    if (renderable->HasInstancing() && i < renderable->GetInstanceCount())
                    {
                        transform = renderable->GetInstance(i, true);
                    }
                    else if (i == 0)
                    {
                        transform = GetEntity()->GetMatrix();
                    }
                    else
                    {
                        continue;
                    }

                    PxTransform pose(
                        PxVec3(transform.GetTranslation().x, transform.GetTranslation().y, transform.GetTranslation().z),
                        PxQuat(transform.GetRotation().x, transform.GetRotation().y, transform.GetRotation().z, transform.GetRotation().w)
                    );
                    actor->setGlobalPose(pose);

                    if (dynamic && !m_is_kinematic)
                    {
                        dynamic->setLinearVelocity(PxVec3(0, 0, 0));
                        dynamic->setAngularVelocity(PxVec3(0, 0, 0));
                    }
                }
            }
        }

        // distance-based activation/deactivation for static actors
        // this optimization prevents the physics scene from being overwhelmed with distant static colliders
        if (m_body_type != BodyType::Controller && m_is_static)
        {
            Camera* camera = World::GetCamera();
            Renderable* renderable = GetEntity()->GetComponent<Renderable>();
            if (camera && renderable)
            {
                const Vector3 camera_pos = camera->GetEntity()->GetPosition();
                
                // ensure tracking vector matches actor count
                if (m_actors_active.size() != m_actors.size())
                {
                    m_actors_active.resize(m_actors.size(), true); // assume initially active
                }

                for (uint32_t i = 0; i < static_cast<uint32_t>(m_actors.size()); i++)
                {
                    PxRigidActor* actor = static_cast<PxRigidActor*>(m_actors[i]);
                    if (!actor)
                        continue;

                    // compute distance to actor
                    Vector3 closest_point = renderable->HasInstancing()
                        ? renderable->GetInstance(i, true).GetTranslation()
                        : renderable->GetBoundingBox().GetClosestPoint(camera_pos);
                    const float distance_squared = Vector3::DistanceSquared(camera_pos, closest_point);

                    // use hysteresis to prevent flickering at boundary
                    const bool is_active     = m_actors_active[i];
                    const bool should_remove = is_active && (distance_squared > distance_deactivate_squared);
                    const bool should_add    = !is_active && (distance_squared <= distance_activate_squared);

                    if (should_remove)
                    {
                        PhysicsWorld::RemoveActor(actor);
                        m_actors_active[i] = false;
                    }
                    else if (should_add)
                    {
                        PhysicsWorld::AddActor(actor);
                        m_actors_active[i] = true;
                    }
                }
            }
        }
    }

    void Physics::Save(pugi::xml_node& node)
    {
        node.append_attribute("mass")             = m_mass;
        node.append_attribute("friction")         = m_friction;
        node.append_attribute("friction_rolling") = m_friction_rolling;
        node.append_attribute("restitution")      = m_restitution;
        node.append_attribute("is_static")        = m_is_static;
        node.append_attribute("is_kinematic")     = m_is_kinematic;
        node.append_attribute("position_lock_x")  = m_position_lock.x;
        node.append_attribute("position_lock_y")  = m_position_lock.y;
        node.append_attribute("position_lock_z")  = m_position_lock.z;
        node.append_attribute("rotation_lock_x")  = m_rotation_lock.x;
        node.append_attribute("rotation_lock_y")  = m_rotation_lock.y;
        node.append_attribute("rotation_lock_z")  = m_rotation_lock.z;
        node.append_attribute("center_of_mass_x") = m_center_of_mass.x;
        node.append_attribute("center_of_mass_y") = m_center_of_mass.y;
        node.append_attribute("center_of_mass_z") = m_center_of_mass.z;
        node.append_attribute("body_type")        = static_cast<int>(m_body_type);
    }
    
    void Physics::Load(pugi::xml_node& node)
    {
        m_mass             = node.attribute("mass").as_float(0.001f);
        m_friction         = node.attribute("friction").as_float(1.0f);
        m_friction_rolling = node.attribute("friction_rolling").as_float(0.002f);
        m_restitution      = node.attribute("restitution").as_float(0.2f);
        m_is_static        = node.attribute("is_static").as_bool(true);
        m_is_kinematic     = node.attribute("is_kinematic").as_bool(false);
        m_position_lock.x  = node.attribute("position_lock_x").as_float(0.0f);
        m_position_lock.y  = node.attribute("position_lock_y").as_float(0.0f);
        m_position_lock.z  = node.attribute("position_lock_z").as_float(0.0f);
        m_rotation_lock.x  = node.attribute("rotation_lock_x").as_float(0.0f);
        m_rotation_lock.y  = node.attribute("rotation_lock_y").as_float(0.0f);
        m_rotation_lock.z  = node.attribute("rotation_lock_z").as_float(0.0f);
        m_center_of_mass.x = node.attribute("center_of_mass_x").as_float(0.0f);
        m_center_of_mass.y = node.attribute("center_of_mass_y").as_float(0.0f);
        m_center_of_mass.z = node.attribute("center_of_mass_z").as_float(0.0f);
        m_body_type        = static_cast<BodyType>(node.attribute("body_type").as_int(static_cast<int>(BodyType::Max)));
    
        Create();
    }

    void Physics::SetMass(float mass)
    {
        // approximate mass from volume
        if (mass == mass_from_volume)
        {
            constexpr float density = 1000.0f; // kg/m³ (default density, e.g., water)
            float volume            = 0.0f;
            Vector3 scale           = GetEntity()->GetScale();

            if (m_body_type == BodyType::Max)
            {
                SP_LOG_WARNING("This call will be ignored. You need to set the body type before setting mass from volume.");
                return;
            }

            switch (m_body_type)
            {
                case BodyType::Box:
                {
                    // volume = x * y * z
                    volume = scale.x * scale.y * scale.z;
                    break;
                }
                case BodyType::Sphere:
                {
                    // volume = (4/3) * π * r³, radius = max(x, y, z) / 2
                    float radius = max(max(scale.x, scale.y), scale.z) * 0.5f;
                    volume       = (4.0f / 3.0f) * math::pi * radius * radius * radius;
                    break;
                }
                case BodyType::Capsule:
                {
                    // volume             = cylinder (π * r² * h) + two hemispheres ((4/3) * π * r³)
                    float radius          = max(scale.x, scale.z) * 0.5f;
                    float cylinder_height = max(0.0f, scale.y - 2.0f * radius); // height of cylindrical part (clamp to avoid negative)
                    float cylinder_volume = math::pi * radius * radius * cylinder_height;
                    float sphere_volume   = (4.0f / 3.0f) * math::pi * radius * radius * radius;
                    volume                = cylinder_volume + sphere_volume;
                    break;
                }
                case BodyType::Mesh:
                {
                    // approximate using bounding box volume
                    Renderable* renderable = GetEntity()->GetComponent<Renderable>();
                    if (renderable)
                    {
                        BoundingBox bbox = renderable->GetBoundingBox();
                        Vector3 extents = bbox.GetExtents();
                        volume = extents.x * extents.y * extents.z * 8.0f; // extents are half-size
                    }
                    else
                    {
                        volume = 1.0f; // fallback volume (1 m³)
                    }
                    break;
                }
                case BodyType::Plane:
                {
                    // infinite plane, use default mass
                    mass   = 1.0f;
                    volume = 0.0f; // skip volume-based calculation
                    break;
                }
                case BodyType::Controller:
                {
                    // controller, use default mass (e.g., human-like)
                    mass   = 70.0f; // approximate human mass
                    volume = 0.0f;  // skip volume-based calculation
                    break;
                }
            }
    
            // calculate mass from volume if applicable
            if (volume > 0.0f)
            {
                mass = volume * density;
            }
        }
    
        // ensure safe physx mass range
        m_mass = min(max(mass, 0.001f), 10000.0f);
    
        // update mass for all dynamic bodies
        for (auto* body : m_actors)
        {
            if (body)
            { 
                if (PxRigidDynamic* dynamic = static_cast<PxRigidActor*>(body)->is<PxRigidDynamic>())
                {
                    dynamic->setMass(m_mass);
                    // update inertia if center of mass is set
                    if (m_center_of_mass != Vector3::Zero)
                    {
                        PxVec3 p(m_center_of_mass.x, m_center_of_mass.y, m_center_of_mass.z);
                        PxRigidBodyExt::setMassAndUpdateInertia(*dynamic, m_mass, &p);
                    }
                }
            }
        }
    }

    void Physics::SetFriction(float friction)
    {
        if (m_friction == friction)
            return;
    
        if (m_material)
        {
            m_friction = friction;
            static_cast<PxMaterial*>(m_material)->setStaticFriction(m_friction);
        }
    }

    void Physics::SetFrictionRolling(float friction_rolling)
    {
        if (m_friction_rolling == friction_rolling)
            return;

        if (m_material)
        {
            m_friction_rolling = friction_rolling;
            static_cast<PxMaterial*>(m_material)->setDynamicFriction(m_friction_rolling);
        }
    }

    void Physics::SetRestitution(float restitution)
    {
        if (m_restitution == restitution)
            return;

        if (m_material)
        {
            m_restitution = restitution;
            static_cast<PxMaterial*>(m_material)->setRestitution(m_restitution);
        }
    }

    void Physics::SetLinearVelocity(const Vector3& velocity) const
    {
        if (m_body_type == BodyType::Controller)
            return;

        for (auto* body : m_actors)
        {
            if (PxRigidDynamic* dynamic = static_cast<PxRigidActor*>(body)->is<PxRigidDynamic>())
            {
                dynamic->setLinearVelocity(PxVec3(velocity.x, velocity.y, velocity.z));
                dynamic->wakeUp();
            }
        }
    }

    Vector3 Physics::GetLinearVelocity() const
    {
        if (m_body_type == BodyType::Controller)
        {
            if (m_controller)
            {
                // for controllers, return the stored velocity used for movement
                return m_velocity;
            }
            return Vector3::Zero;
        }
        
        if (m_actors.empty() || !m_actors[0])
            return Vector3::Zero;
            
        if (PxRigidDynamic* dynamic = static_cast<PxRigidActor*>(m_actors[0])->is<PxRigidDynamic>())
        {
            PxVec3 velocity = dynamic->getLinearVelocity();
            return Vector3(velocity.x, velocity.y, velocity.z);
        }
        
        return Vector3::Zero;
    }

    void Physics::SetAngularVelocity(const Vector3& velocity) const
    {
        if (m_body_type == BodyType::Controller)
            return;

        for (auto* body : m_actors)
        {
            if (PxRigidDynamic* dynamic = static_cast<PxRigidActor*>(body)->is<PxRigidDynamic>())
            {
                dynamic->setAngularVelocity(PxVec3(velocity.x, velocity.y, velocity.z));
                dynamic->wakeUp();
            }
        }
    }

    void Physics::ApplyForce(const Vector3& force, PhysicsForce mode) const
    {
        if (m_body_type == BodyType::Controller)
        {
            SP_LOG_WARNING("Don't call ApplyForce on a controller, call Move() instead");
            return;
        }

        for (auto* body : m_actors)
        {
            if (PxRigidDynamic* dynamic = static_cast<PxRigidActor*>(body)->is<PxRigidDynamic>())
            {
                PxForceMode::Enum px_mode = (mode == PhysicsForce::Constant) ? PxForceMode::eFORCE : PxForceMode::eIMPULSE;
                dynamic->addForce(PxVec3(force.x, force.y, force.z), px_mode);
                dynamic->wakeUp();
            }
        }
    }

    void Physics::SetPositionLock(bool lock)
    {
        SetPositionLock(lock ? Vector3::One : Vector3::Zero);
    }

    void Physics::SetPositionLock(const Vector3& lock)
    {
        if (m_body_type == BodyType::Controller)
            return;
    
        m_position_lock = lock;
        for (auto* body : m_actors)
        {
            if (PxRigidDynamic* dynamic = static_cast<PxRigidActor*>(body)->is<PxRigidDynamic>())
            {
                dynamic->setRigidDynamicLockFlags(build_lock_flags(m_position_lock, m_rotation_lock));
            }
        }
    }

    void Physics::SetRotationLock(bool lock)
    {
        SetRotationLock(lock ? Vector3::One : Vector3::Zero);
    }

    void Physics::SetRotationLock(const Vector3& lock)
    {
        if (m_body_type == BodyType::Controller)
            return;
    
        m_rotation_lock = lock;
        for (auto* body : m_actors)
        {
            if (PxRigidDynamic* dynamic = static_cast<PxRigidActor*>(body)->is<PxRigidDynamic>())
            {
                dynamic->setRigidDynamicLockFlags(build_lock_flags(m_position_lock, m_rotation_lock));
            }
        }
    }

    void Physics::SetCenterOfMass(const Vector3& center_of_mass)
    {
        if (m_body_type == BodyType::Controller)
            return;
    
        m_center_of_mass = center_of_mass;
        for (auto* body : m_actors)
        {
            if (!body)
                continue;
                
            if (PxRigidDynamic* dynamic = static_cast<PxRigidActor*>(body)->is<PxRigidDynamic>())
            {
                if (m_center_of_mass != Vector3::Zero)
                {
                    PxVec3 p(m_center_of_mass.x, m_center_of_mass.y, m_center_of_mass.z);
                    PxRigidBodyExt::setMassAndUpdateInertia(*dynamic, m_mass, &p);
                }
                else
                {
                    // update inertia with default center of mass (0,0,0)
                    PxRigidBodyExt::setMassAndUpdateInertia(*dynamic, m_mass, nullptr);
                }
            }
        }
    }

    void Physics::SetBodyType(BodyType type)
    {
        if (m_body_type == type)
            return;

        m_body_type = type;
        Create();
    }

    bool Physics::IsGrounded() const
    {
        return GetGroundEntity() != nullptr; // eCOLLISION_DOWN is not very reliable (it can flicker), so we use raycasting as a fallback
    }

    Entity* Physics::GetGroundEntity() const
    {
        // check if body is a controller
        if (m_body_type != BodyType::Controller)
        {
            SP_LOG_WARNING("this method is only applicable for controller bodies.");
            return nullptr;
        }
    
        if (!m_controller)
            return nullptr;
    
        // get controller's current position
        PxController* controller = static_cast<PxController*>(m_controller);
        PxExtendedVec3 pos_ext   = controller->getPosition();
        PxVec3 pos               = PxVec3(static_cast<float>(pos_ext.x), static_cast<float>(pos_ext.y), static_cast<float>(pos_ext.z));
    
        // ray start just below the controller
        const float ray_length = standing_height;
        PxVec3 ray_start       = pos;
        PxVec3 ray_dir         = PxVec3(0.0f, -1.0f, 0.0f);
    
        const PxU32 max_hits = 10;
        PxRaycastHit hit_buffer[max_hits];
        PxRaycastBuffer hit(hit_buffer, max_hits);
    
        PxQueryFilterData filter_data;
        filter_data.flags = PxQueryFlag::eSTATIC | PxQueryFlag::eDYNAMIC;
    
        PxScene* scene = static_cast<PxScene*>(PhysicsWorld::GetScene());
        if (!scene)
            return nullptr;
    
        // get the actor used by the controller to avoid returning itself
        PxRigidActor* actor_to_ignore = controller->getActor();
    
        if (scene->raycast(ray_start, ray_dir, ray_length, hit, PxHitFlag::eDEFAULT, filter_data))
        { 
            for (PxU32 i = 0; i < hit.nbTouches; ++i)
            {
                const PxRaycastHit& current_hit = hit.getTouch(i);
    
                if (!current_hit.actor || current_hit.actor == actor_to_ignore)
                    continue;
    
                if (current_hit.actor->userData)
                    return static_cast<Entity*>(current_hit.actor->userData);
            }
        }
    
        return nullptr;
    }

    float Physics::GetCapsuleVolume()
    {
        // total volume is the sum of the cylinder and two hemispheres
        const float radius = GetCapsuleRadius();
        const Vector3 scale = GetEntity()->GetScale();

        // cylinder volume: π * r² * h (clamp to avoid negative height)
        const float cylinder_height = max(0.0f, scale.y - 2.0f * radius);
        const float cylinder_volume = math::pi * radius * radius * cylinder_height;

        // sphere volume (two hemispheres = one full sphere): (4/3) * π * r³
        const float sphere_volume = (4.0f / 3.0f) * math::pi * radius * radius * radius;

        return cylinder_volume + sphere_volume;
    }

    float Physics::GetCapsuleRadius()
    {
        Vector3 scale = GetEntity()->GetScale();
        return max(scale.x, scale.z) * 0.5f;
    }

    Vector3 Physics::GetControllerTopLocal() const
    {
        if (m_body_type != BodyType::Controller || !m_controller)
        {
            SP_LOG_WARNING("Only applicable for controller bodies.");
            return Vector3::Zero;
        }
        
        PxCapsuleController* controller = static_cast<PxCapsuleController*>(m_controller);
        float height                    = controller->getHeight();
        float radius                    = controller->getRadius();
        
        // relative local position to the top of the capsule (from the capsule's center)
        return Vector3(0.0f, (height * 0.5f) + radius, 0.0f);
    }

    void Physics::SetStatic(bool is_static)
    {
        // return if state hasn't changed
        if (m_is_static == is_static)
            return;
    
        // update static state
        m_is_static    = is_static;
        m_is_kinematic = false; // statics can't be kinematic
     
        // recreate bodies to apply static/dynamic state
        Create();
    }

    void Physics::SetKinematic(bool is_kinematic)
    {
        // return if state hasn't changed
        if (m_is_kinematic == is_kinematic)
            return;
    
        // update kinematic state
        m_is_kinematic = is_kinematic;
        m_is_static    = false; // kinematics require dynamic (non-static) bodies
    
        Create(); // recreate body to apply changes
    }

    void Physics::Move(const math::Vector3& offset)
    {
        if (m_body_type == BodyType::Controller && Engine::IsFlagSet(EngineMode::Playing))
        {
            if (!m_controller)
                return;

            PxCapsuleController* controller = static_cast<PxCapsuleController*>(m_controller);
            float delta_time = static_cast<float>(Timer::GetDeltaTimeSec());
            PxControllerFilters filters;
            filters.mFilterFlags = PxQueryFlag::eSTATIC | PxQueryFlag::eDYNAMIC;
            controller->move(PxVec3(offset.x, offset.y, offset.z), 0.001f, delta_time, filters);
        }
        else
        {
            GetEntity()->Translate(offset);
        }
    }

    void Physics::Crouch(const bool crouch)
    {
        if (m_body_type != BodyType::Controller || !m_controller || !Engine::IsFlagSet(EngineMode::Playing))
            return;

        // resize the capsule
        PxCapsuleController* controller = static_cast<PxCapsuleController*>(m_controller);
        const float current_height      = controller->getHeight();
        const float target_height       = crouch ? crouch_height : standing_height;
        const float delta_time          = static_cast<float>(Timer::GetDeltaTimeSec());
        const float speed               = 10.0f;
        const float lerped_height       = math::lerp(current_height, target_height, 1.0f - exp(-speed * delta_time));  
        controller->resize(lerped_height);

        // ensure bottom of the capsule is touching the ground
        PxExtendedVec3 pos = controller->getPosition();
        GetEntity()->SetPosition(Vector3(static_cast<float>(pos.x), static_cast<float>(pos.y), static_cast<float>(pos.z)));
    }

    void Physics::SetVehicleThrottle(float value)
    {
        if (m_body_type != BodyType::Vehicle)
            return;
        
        vehicle::set_throttle(value);
    }

    void Physics::SetVehicleBrake(float value)
    {
        if (m_body_type != BodyType::Vehicle)
            return;
        
        vehicle::set_brake(value);
    }

    void Physics::SetVehicleSteering(float value)
    {
        if (m_body_type != BodyType::Vehicle)
            return;
        
        vehicle::set_steering(value);
    }

    void Physics::SetWheelEntity(WheelIndex wheel, Entity* entity)
    {
        if (m_body_type != BodyType::Vehicle)
        {
            SP_LOG_WARNING("SetWheelEntity only works with Vehicle body type");
            return;
        }

        int index = static_cast<int>(wheel);
        if (index >= 0 && index < static_cast<int>(WheelIndex::Count))
        {
            m_wheel_entities[index] = entity;
        }
    }

    Entity* Physics::GetWheelEntity(WheelIndex wheel) const
    {
        int index = static_cast<int>(wheel);
        if (index >= 0 && index < static_cast<int>(WheelIndex::Count))
        {
            return m_wheel_entities[index];
        }
        return nullptr;
    }

    void Physics::UpdateWheelTransforms()
    {
        if (m_body_type != BodyType::Vehicle || !Engine::IsFlagSet(EngineMode::Playing))
            return;

        // get vehicle speed for wheel spin
        float speed = 0.0f;
        float forward_speed = 0.0f;
        if (!m_actors.empty() && m_actors[0])
        {
            PxRigidDynamic* body = static_cast<PxRigidActor*>(m_actors[0])->is<PxRigidDynamic>();
            if (body)
            {
                PxVec3 velocity = body->getLinearVelocity();
                PxTransform pose = body->getGlobalPose();
                PxVec3 forward = pose.q.rotate(PxVec3(0, 0, 1));
                forward_speed = velocity.dot(forward);
                speed = velocity.magnitude();
            }
        }

        // get steering angle from vehicle system
        float steering = vehicle::get_steering();
        const float max_steering_angle = 35.0f * math::deg_to_rad; // max steering angle in radians
        float steering_angle = steering * max_steering_angle;

        // update wheel spin rotation based on speed
        // wheel rotation = distance / radius, distance = speed * dt
        const float wheel_radius = 0.35f; // from vehicle_dimensions
        float delta_time = static_cast<float>(Timer::GetDeltaTimeSec());
        float rotation_delta = (forward_speed * delta_time) / wheel_radius;
        m_wheel_rotation += rotation_delta;

        // keep rotation in reasonable bounds to avoid float precision issues
        const float two_pi = 2.0f * math::pi;
        while (m_wheel_rotation > two_pi) m_wheel_rotation -= two_pi;
        while (m_wheel_rotation < -two_pi) m_wheel_rotation += two_pi;

        // update each wheel entity
        for (int i = 0; i < static_cast<int>(WheelIndex::Count); i++)
        {
            Entity* wheel_entity = m_wheel_entities[i];
            if (!wheel_entity)
                continue;

            bool is_front_wheel = (i == static_cast<int>(WheelIndex::FrontLeft) || i == static_cast<int>(WheelIndex::FrontRight));
            bool is_left_wheel = (i == static_cast<int>(WheelIndex::FrontLeft) || i == static_cast<int>(WheelIndex::RearLeft));

            // wheel spin rotation (around the axle - X axis in local space)
            Quaternion spin_rotation = Quaternion::FromAxisAngle(Vector3::Right, m_wheel_rotation);

            // steering rotation for front wheels only (around Y axis)
            Quaternion steer_rotation = Quaternion::Identity;
            if (is_front_wheel)
            {
                steer_rotation = Quaternion::FromAxisAngle(Vector3::Up, steering_angle);
            }

            // combine rotations: first spin, then steer
            // note: for left wheels, we might need to flip the spin direction depending on model orientation
            Quaternion final_rotation = steer_rotation * spin_rotation;

            wheel_entity->SetRotationLocal(final_rotation);
        }
    }

    void Physics::Create()
    {
        // clear previous state
        Remove();

        PxPhysics* physics = static_cast<PxPhysics*>(PhysicsWorld::GetPhysics());
        PxScene* scene     = static_cast<PxScene*>(PhysicsWorld::GetScene());

        // material - shared across all shapes (if multiple shapes are used)
        m_material = physics->createMaterial(m_friction, m_friction_rolling, m_restitution);

        // body/controller
        if (m_body_type == BodyType::Controller)
        {
            if (!controller_manager)
            {
                controller_manager = PxCreateControllerManager(*scene);
                if (!controller_manager)
                {
                    SP_LOG_ERROR("Failed to create controller manager");
                    return;
                }
            }

            PxCapsuleControllerDesc desc;
            desc.radius           = 0.5f; // stable size for ground contact
            desc.height           = standing_height;
            desc.climbingMode     = PxCapsuleClimbingMode::eEASY; // easier handling on steps/slopes
            desc.stepOffset       = 0.3f; // keep under half a meter for better stepping
            desc.slopeLimit       = cosf(60.0f * math::deg_to_rad); // 60° climbable slope
            desc.contactOffset    = 0.01f; // allows early contact without tunneling
            desc.upDirection      = PxVec3(0, 1, 0); // up is y
            desc.nonWalkableMode  = PxControllerNonWalkableMode::ePREVENT_CLIMBING_AND_FORCE_SLIDING;
            
            // optional but recommended: disable callbacks unless needed
            desc.reportCallback   = nullptr;
            desc.behaviorCallback = nullptr;
            
            // apply initial position
            const Vector3& pos = GetEntity()->GetPosition();
            desc.position      = PxExtendedVec3(pos.x, pos.y, pos.z);
            
            // assign material
            desc.material = static_cast<PxMaterial*>(m_material);
            
            // create controller
            m_controller = static_cast<PxControllerManager*>(controller_manager)->createController(desc);
            if (!m_controller)
            {
                SP_LOG_ERROR("failed to create capsule controller");
                static_cast<PxMaterial*>(m_material)->release();
                m_material = nullptr;
                return;
            }
            
            // note: the controller internally references the material, so don't release m_material here
            // it will be released in Remove() when the controller is destroyed
        }
        else if (m_body_type == BodyType::Vehicle)
        {
            // create vehicle using vehicle2 api
            vehicle::vehicle_data* vehicle_data = vehicle::setup(physics, scene);
            if (vehicle_data)
            {
                // store the rigid body actor
                if (vehicle_data->physx_actor.rigidBody)
                {
                    m_actors.resize(1, nullptr);
                    m_actors[0] = vehicle_data->physx_actor.rigidBody;
                    m_actors_active.resize(1, true);
                    
                    // set initial position
                    Vector3 pos = GetEntity()->GetPosition();
                    vehicle_data->physx_actor.rigidBody->setGlobalPose(PxTransform(PxVec3(pos.x, pos.y, pos.z)));
                    
                    // store user data for raycasts
                    vehicle_data->physx_actor.rigidBody->userData = reinterpret_cast<void*>(GetEntity());
                    
                    // set as active vehicle for driving
                    vehicle::set_active_vehicle(vehicle_data);
                    
                    SP_LOG_INFO("vehicle physics body created successfully");
                }
                
                // note: vehicle_data is leaked here - will need proper lifecycle management later
                // for now this is just to get it working
            }
            else
            {
                SP_LOG_ERROR("failed to create vehicle physics body");
            }
        }
        else
        {
            // mesh
            if (m_body_type == BodyType::Mesh)
            {
                Renderable* renderable = GetEntity()->GetComponent<Renderable>();
                if (!renderable)
                {
                    SP_LOG_ERROR("No Renderable component found for mesh shape");
                    return;
                }

                // get geometry
                vector<uint32_t> indices;
                vector<RHI_Vertex_PosTexNorTan> vertices;
                renderable->GetGeometry(&indices, &vertices);
                if (vertices.empty() || indices.empty())
                {
                    SP_LOG_ERROR("Empty vertex or index data for mesh shape");
                    return;
                }

                // simplify geometry
                const float volume        = renderable->GetBoundingBox().GetVolume();
                const float max_volume    = 100000.0f;
                // simplify geometry based on volume (larger objects get more detail)
                const float volume_factor       = clamp(volume / max_volume, 0.0f, 1.0f);
                const size_t min_index_count    = min<size_t>(indices.size(), 256);
                const size_t max_index_count    = 16'000;
                const size_t target_index_count = clamp<size_t>(static_cast<size_t>(indices.size() * volume_factor), min_index_count, max_index_count);
                geometry_processing::simplify(indices, vertices, target_index_count, false, false);
                
                // warn if we hit the complexity cap (original mesh was very detailed)
                if (indices.size() > max_index_count && target_index_count == max_index_count)
                {
                    SP_LOG_WARNING("Mesh '%s' was simplified to %zu indices. It's still complex and may impact physics performance.", renderable->GetEntity()->GetObjectName().c_str(), target_index_count);
                }

                // convert vertices to physx format
                vector<PxVec3> px_vertices;
                px_vertices.reserve(vertices.size());
                Vector3 scale = GetEntity()->GetScale();
                for (const auto& vertex : vertices)
                {
                    px_vertices.emplace_back(vertex.pos[0] * scale.x, vertex.pos[1] * scale.y, vertex.pos[2] * scale.z);
                }

                // cooking parameters
                PxTolerancesScale _scale;
                _scale.length                          = 1.0f;                         // 1 unit = 1 meter
                Vector3 gravity                        = PhysicsWorld::GetGravity();
                _scale.speed                           = sqrtf(gravity.x * gravity.x + gravity.y * gravity.y + gravity.z * gravity.z); // magnitude of gravity vector
                PxCookingParams params(_scale);         
                params.areaTestEpsilon                 = 0.06f * _scale.length * _scale.length;
                params.planeTolerance                  = 0.0007f;
                params.convexMeshCookingType           = PxConvexMeshCookingType::eQUICKHULL;
                params.suppressTriangleMeshRemapTable  = false;
                params.buildTriangleAdjacencies        = true;
                params.buildGPUData                    = false;
                params.meshPreprocessParams           |= PxMeshPreprocessingFlag::eWELD_VERTICES;
                params.meshWeldTolerance               = 0.01f;
                params.meshAreaMinLimit                = 0.0f;
                params.meshEdgeLengthMaxLimit          = 500.0f;
                params.gaussMapLimit                   = 32;
                params.maxWeightRatioInTet             = FLT_MAX;

                PxInsertionCallback* insertion_callback = PxGetStandaloneInsertionCallback();
                if (IsStatic() || IsKinematic()) // triangle mesh for exact collision (static or kinematic)
                {
                    PxTriangleMeshDesc mesh_desc;
                    mesh_desc.points.count     = static_cast<PxU32>(px_vertices.size());
                    mesh_desc.points.stride    = sizeof(PxVec3);
                    mesh_desc.points.data      = px_vertices.data();
                    mesh_desc.triangles.count  = static_cast<PxU32>(indices.size() / 3);
                    mesh_desc.triangles.stride = 3 * sizeof(PxU32);
                    mesh_desc.triangles.data   = indices.data();

                    // create
                    PxTriangleMeshCookingResult::Enum condition;
                    m_mesh = PxCreateTriangleMesh(params, mesh_desc, *insertion_callback, &condition);
                    if (condition != PxTriangleMeshCookingResult::eSUCCESS)
                    {
                        SP_LOG_ERROR("Failed to create triangle mesh: %d", condition);
                        if (m_mesh)
                        {
                            static_cast<PxTriangleMesh*>(m_mesh)->release();
                            m_mesh = nullptr;
                        }
                        return;
                    }
                }
                else // dynamic: convex mesh
                {
                    PxConvexMeshDesc mesh_desc;
                    mesh_desc.points.count  = static_cast<PxU32>(px_vertices.size());
                    mesh_desc.points.stride = sizeof(PxVec3);
                    mesh_desc.points.data   = px_vertices.data();
                    mesh_desc.flags         = PxConvexFlag::eCOMPUTE_CONVEX;

                    // create
                    PxConvexMeshCookingResult::Enum condition;
                    m_mesh = PxCreateConvexMesh(params, mesh_desc, *insertion_callback, &condition);
                    if (!m_mesh || condition != PxConvexMeshCookingResult::eSUCCESS)
                    {
                        SP_LOG_ERROR("Failed to create convex mesh: %d", condition);
                        if (m_mesh)
                        {
                            static_cast<PxConvexMesh*>(m_mesh)->release();
                            m_mesh = nullptr;
                        }
                        return;
                    }
                }
            }

            CreateBodies();
        }
    }

    void Physics::CreateBodies()
    {
        PxPhysics* physics      = static_cast<PxPhysics*>(PhysicsWorld::GetPhysics());
        Renderable* renderable  = GetEntity()->GetComponent<Renderable>();
        
        if (!renderable)
        {
            SP_LOG_ERROR("No Renderable component found for physics body creation");
            return;
        }

        // create bodies and shapes
        const uint32_t instance_count = renderable->GetInstanceCount();
        m_actors.resize(instance_count, nullptr);
        m_actors_active.resize(instance_count, true); // all actors start active
        for (uint32_t i = 0; i < instance_count; i++)
        {
            math::Matrix transform = renderable->HasInstancing() ? renderable->GetInstance(i, true) : GetEntity()->GetMatrix();
            PxTransform pose(
                PxVec3(transform.GetTranslation().x, transform.GetTranslation().y, transform.GetTranslation().z),
                PxQuat(transform.GetRotation().x, transform.GetRotation().y, transform.GetRotation().z, transform.GetRotation().w)
            );
            PxRigidActor* actor = nullptr;
            if (IsStatic())
            {
                actor = physics->createRigidStatic(pose);
            }
            else
            {
                actor = physics->createRigidDynamic(pose);
                PxRigidDynamic* dynamic = actor->is<PxRigidDynamic>();
                if (dynamic)
                {
                    dynamic->setMass(m_mass);
                    dynamic->setRigidBodyFlag(PxRigidBodyFlag::eENABLE_CCD, !m_is_kinematic); // kinematics don't support ccd
                    dynamic->setRigidBodyFlag(PxRigidBodyFlag::eKINEMATIC, m_is_kinematic);
                    if (m_center_of_mass != Vector3::Zero)
                    {
                        PxVec3 p(m_center_of_mass.x, m_center_of_mass.y, m_center_of_mass.z);
                        PxRigidBodyExt::setMassAndUpdateInertia(*dynamic, m_mass, &p);
                    }
                    dynamic->setRigidDynamicLockFlags(build_lock_flags(m_position_lock, m_rotation_lock));
                }
            }
        
            PxShape* shape       = nullptr;
            PxMaterial* material = static_cast<PxMaterial*>(m_material);
            switch (m_body_type)
            {
                case BodyType::Box:
                {
                    Vector3 scale = GetEntity()->GetScale();
                    PxBoxGeometry geometry(scale.x * 0.5f, scale.y * 0.5f, scale.z * 0.5f);
                    shape = physics->createShape(geometry, *material);
                    break;
                }
                case BodyType::Sphere:
                {
                    Vector3 scale = GetEntity()->GetScale();
                    float radius  = max(max(scale.x, scale.y), scale.z) * 0.5f;
                    PxSphereGeometry geometry(radius);
                    shape = physics->createShape(geometry, *material);
                    break;
                }
                case BodyType::Plane:
                {
                    PxPlaneGeometry geometry;
                    shape = physics->createShape(geometry, *material);
                    shape->setLocalPose(PxTransform(PxVec3(0, 0, 0), PxQuat(PxHalfPi, PxVec3(0, 0, 1))));
                    break;
                }
                case BodyType::Capsule:
                {
                    Vector3 scale     = GetEntity()->GetScale();
                    float radius      = max(scale.x, scale.z) * 0.5f;
                    float half_height = scale.y * 0.5f;
                    PxCapsuleGeometry geometry(radius, half_height);
                    shape = physics->createShape(geometry, *material);
                    shape->setLocalPose(PxTransform(PxVec3(0, 0, 0), PxQuat(PxHalfPi, PxVec3(0, 0, 1))));
                    break;
                }
                case BodyType::Mesh:
                {
                    if (m_mesh)
                    {
                        if (IsStatic() || IsKinematic())
                        {
                            Vector3 scale = renderable->HasInstancing() ? renderable->GetInstance(i, false).GetScale() : Vector3::One;
                            PxMeshScale mesh_scale(PxVec3(scale.x, scale.y, scale.z)); // this is a runtime transform, cheap for statics but it won't be reflected for the internal baked shape (raycasts etc)
                            PxTriangleMeshGeometry geometry(static_cast<PxTriangleMesh*>(m_mesh), mesh_scale);
                            shape = physics->createShape(geometry, *material);
                        }
                        else
                        {
                            PxConvexMeshGeometry geometry(static_cast<PxConvexMesh*>(m_mesh));
                            shape = physics->createShape(geometry, *material);
                        }
                    }
                    break;
                }
            }
            
            if (shape)
            {
                shape->setFlag(PxShapeFlag::eVISUALIZATION, true);
                actor->attachShape(*shape);
                shape->release(); // release shape reference (actor owns it now)
            }

            if (actor)
            {
                actor->userData = reinterpret_cast<void*>(GetEntity());
                PhysicsWorld::AddActor(actor);
            }

            m_actors[i] = actor;
        }
    }
}
