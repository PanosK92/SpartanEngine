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

//= includes ==========================
#include "pch.h"
#include "PhysicsWorld.h"
#include "ProgressTracker.h"
#include "../Profiling/Profiler.h"
#include "../Rendering/Renderer.h"
#include "../Input/Input.h"
#include "../World/Entity.h"
#include "../World/Components/Camera.h"
#include "../World/Components/Physics.h"
#include "../World/World.h"
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
SP_WARNINGS_ON
//=====================================

//= namespaces ================
using namespace std;
using namespace spartan::math;
using namespace physx;
//=============================

namespace spartan
{
    namespace
    {
        recursive_mutex physx_mutex;
    }

    namespace settings
    {
        float gravity = -9.81f; // gravity value in m/s^2
        float hz      = 200.0f; // simulation frequency in hz
    }
    
    namespace interpolation
    {
        float alpha = 0.0f; // interpolation factor between physics steps (0 = previous, 1 = current)
    }

    namespace
    {
        std::function<void(float)> vehicle_step_callback; // see PhysicsWorld::SetVehicleStepCallback
    }

    namespace picking
    {
        static PxRigidDynamic* picked_body = nullptr;
        static PxRigidDynamic* dummy_actor = nullptr;
        static PxD6Joint* joint            = nullptr;
        static PxReal pick_distance        = 0.0f;

        void PickBody()
        {
            // get camera
            Camera* camera = World::GetCamera();
            if (!camera)
            {
                return;
            }

            // get picking ray
            Ray picking_ray = camera->ComputePickingRay();
            PxVec3 origin(picking_ray.GetStart().x, picking_ray.GetStart().y, picking_ray.GetStart().z);
            PxVec3 direction(picking_ray.GetDirection().x, picking_ray.GetDirection().y, picking_ray.GetDirection().z);

            // normalize direction
            direction.normalize();

            // raycast
            PxRaycastBuffer hit;
            PxQueryFilterData filter_data(PxQueryFlag::eDYNAMIC); // only pick dynamic bodies - static/kinematic can be moved as per usual from the editor
            PxScene* scene = static_cast<PxScene*>(PhysicsWorld::GetScene());
            lock_guard<recursive_mutex> lock(PhysicsWorld::GetMutex());
            if (scene->raycast(origin, direction, 1000.0f, hit, PxHitFlag::eDEFAULT, filter_data) && hit.hasBlock)
            {
                PxRigidActor* actor = hit.block.actor;
                if (PxRigidDynamic* dynamic = actor->is<PxRigidDynamic>())
                {
                    // store the picked body
                    picked_body = dynamic;

                    // compute hit point in world space
                    PxVec3 hit_pos = hit.block.position;

                    // create dummy kinematic actor at hit point
                    PxTransform dummy_transform(hit_pos);
                    PxPhysics* physics = static_cast<PxPhysics*>(PhysicsWorld::GetPhysics());
                    dummy_actor = physics->createRigidDynamic(dummy_transform);
                    dummy_actor->setRigidBodyFlag(PxRigidBodyFlag::eKINEMATIC, true);
                    scene->addActor(*dummy_actor);

                    // create d6 joint between dummy and picked body
                    PxTransform local_frame_body = PxTransform(picked_body->getGlobalPose().transformInv(hit_pos));
                    joint = PxD6JointCreate(*physics, dummy_actor, PxTransform(PxIdentity), picked_body, local_frame_body);

                    // configure joint as a spring-like constraint
                    joint->setMotion(PxD6Axis::eX, PxD6Motion::eFREE);
                    joint->setMotion(PxD6Axis::eY, PxD6Motion::eFREE);
                    joint->setMotion(PxD6Axis::eZ, PxD6Motion::eFREE);
                    joint->setMotion(PxD6Axis::eTWIST, PxD6Motion::eLOCKED);
                    joint->setMotion(PxD6Axis::eSWING1, PxD6Motion::eLOCKED);
                    joint->setMotion(PxD6Axis::eSWING2, PxD6Motion::eLOCKED);

                    // add drive for spring-like behavior
                    float stiffness = 1000.0f; // controls how strongly the body is pulled
                    float damping   = 100.0f;  // reduces oscillation
                    joint->setDrive(PxD6Drive::eX, PxD6JointDrive(stiffness, damping, PX_MAX_F32, true));
                    joint->setDrive(PxD6Drive::eY, PxD6JointDrive(stiffness, damping, PX_MAX_F32, true));
                    joint->setDrive(PxD6Drive::eZ, PxD6JointDrive(stiffness, damping, PX_MAX_F32, true));

                    // store initial distance along the ray
                    pick_distance = (hit_pos - origin).magnitude();
                }
            }
        }

        void UnpickBody()
        {
            if (picked_body && joint)
            {
                lock_guard<recursive_mutex> lock(PhysicsWorld::GetMutex());
                joint->release();
                joint = nullptr;

                PxScene* scene = static_cast<PxScene*>(PhysicsWorld::GetScene());
                scene->removeActor(*dummy_actor);
                dummy_actor->release();
                dummy_actor = nullptr;

                picked_body = nullptr;
            }
        }

        void MovePickedBody()
        {
            if (!picked_body || !dummy_actor || !joint)
            {
                return;
            }

            Camera* camera = World::GetCamera();
            if (!camera)
            {
                return;
            }

            Ray picking_ray = camera->ComputePickingRay();
            PxVec3 origin(picking_ray.GetStart().x, picking_ray.GetStart().y, picking_ray.GetStart().z);
            PxVec3 direction(picking_ray.GetDirection().x, picking_ray.GetDirection().y, picking_ray.GetDirection().z);

            // normalize direction
            direction.normalize();

            // compute target position along the ray
            PxVec3 target = origin + direction * pick_distance;

            // move dummy actor to target
            lock_guard<recursive_mutex> lock(PhysicsWorld::GetMutex());
            dummy_actor->setKinematicTarget(PxTransform(target));
        }
    }

    class PhysXLogging : public physx::PxErrorCallback
    {
    public:
        void reportError(physx::PxErrorCode::Enum code, const char* message, const char* file, int line) override
        {
            string error_message = string(message) + " (File: " + file + ", Line: " + to_string(line) + ")";
            switch (code)
            {
                case physx::PxErrorCode::eINVALID_PARAMETER: SP_LOG_ERROR("PhysX Invalid Parameter: %s", error_message.c_str()); break;
                case physx::PxErrorCode::eINVALID_OPERATION: SP_LOG_ERROR("PhysX Invalid Operation: %s", error_message.c_str()); break;
                case physx::PxErrorCode::eOUT_OF_MEMORY: SP_LOG_ERROR("PhysX Out of Memory: %s", error_message.c_str()); break;
                case physx::PxErrorCode::eDEBUG_INFO: SP_LOG_INFO("PhysX Debug Info: %s", error_message.c_str()); break;
                case physx::PxErrorCode::eDEBUG_WARNING: SP_LOG_WARNING("PhysX Debug Warning: %s", error_message.c_str()); break;
                case physx::PxErrorCode::eINTERNAL_ERROR: SP_LOG_ERROR("PhysX Internal Error: %s", error_message.c_str()); break;
                case physx::PxErrorCode::eABORT: SP_LOG_ERROR("PhysX Abort: %s", error_message.c_str()); break;
                case physx::PxErrorCode::ePERF_WARNING: SP_LOG_WARNING("PhysX Perf Warning: %s", error_message.c_str()); break;
                default: SP_LOG_ERROR("PhysX Unknown Error (%d): %s", code, error_message.c_str()); break;
            }
        }
    };

    namespace
    {
        static PxDefaultAllocator allocator;
        static PhysXLogging logger;
        static PxFoundation* foundation           = nullptr;
        static PxPhysics* physics                 = nullptr;
        static PxScene* scene                     = nullptr;
        static PxDefaultCpuDispatcher* dispatcher = nullptr;

        // word2 tags for collision filtering
        // 1 = character controller, 2 = vehicle
        // only suppress contacts between character and vehicle; everything else is default
        PxFilterFlags collision_filter_shader(
            PxFilterObjectAttributes attributes0, PxFilterData filter_data0,
            PxFilterObjectAttributes attributes1, PxFilterData filter_data1,
            PxPairFlags& pair_flags, const void* constant_block, PxU32 constant_block_size)
        {
            bool is_character_vs_vehicle =
                (filter_data0.word2 == 1 && filter_data1.word2 == 2) ||
                (filter_data0.word2 == 2 && filter_data1.word2 == 1);

            if (is_character_vs_vehicle)
            {
                return PxFilterFlag::eSUPPRESS;
            }

            return PxDefaultSimulationFilterShader(attributes0, filter_data0, attributes1, filter_data1,
                pair_flags, constant_block, constant_block_size);
        }
    }

    void PhysicsWorld::Initialize()
    {
        // foundation
        foundation = PxCreateFoundation(PX_PHYSICS_VERSION, allocator, logger);
        SP_ASSERT(foundation);

        // physics
        physics = PxCreatePhysics(PX_PHYSICS_VERSION, *foundation, PxTolerancesScale(), false, nullptr);
        SP_ASSERT(physics);

        // scene
        PxSceneDesc scene_desc(physics->getTolerancesScale());
        scene_desc.gravity        = PxVec3(0.0f, settings::gravity, 0.0f);
        scene_desc.cpuDispatcher  = PxDefaultCpuDispatcherCreate(2);
        scene_desc.filterShader   = collision_filter_shader;
        scene_desc.flags         |= PxSceneFlag::eENABLE_CCD; // enable continuous collision detection to reduce tunneling
        scene                     = physics->createScene(scene_desc);
        SP_ASSERT(scene);

        // store dispatcher
        dispatcher = static_cast<PxDefaultCpuDispatcher*>(scene_desc.cpuDispatcher);

        // enable all debug visualization parameters
        scene->setVisualizationParameter(PxVisualizationParameter::eSCALE, 1.0f);
        scene->setVisualizationParameter(PxVisualizationParameter::eWORLD_AXES, 1.0f);
        scene->setVisualizationParameter(PxVisualizationParameter::eACTOR_AXES, 1.0f);
        scene->setVisualizationParameter(PxVisualizationParameter::eCOLLISION_SHAPES, 1.0f);
        scene->setVisualizationParameter(PxVisualizationParameter::eCOLLISION_AXES, 1.0f);
        scene->setVisualizationParameter(PxVisualizationParameter::eCOLLISION_COMPOUNDS, 1.0f);
        scene->setVisualizationParameter(PxVisualizationParameter::eCOLLISION_EDGES, 1.0f);
        scene->setVisualizationParameter(PxVisualizationParameter::eCONTACT_POINT, 1.0f);
        scene->setVisualizationParameter(PxVisualizationParameter::eCONTACT_NORMAL, 1.0f);
        scene->setVisualizationParameter(PxVisualizationParameter::eCONTACT_ERROR, 1.0f);
        scene->setVisualizationParameter(PxVisualizationParameter::eCONTACT_FORCE, 1.0f);
        scene->setVisualizationParameter(PxVisualizationParameter::eJOINT_LOCAL_FRAMES, 1.0f);
        scene->setVisualizationParameter(PxVisualizationParameter::eJOINT_LIMITS, 1.0f);
    }

    void PhysicsWorld::Shutdown()
    {
        // cleanup picking
        picking::UnpickBody();

        // release controller manager (owned by physics component system)
        Physics::Shutdown();

        // release physx resources
        PX_RELEASE(scene);
        PX_RELEASE(dispatcher);
        PX_RELEASE(physics);
        PX_RELEASE(foundation);
    }

    void PhysicsWorld::Tick()
    {
        SP_PROFILE_CPU();

        // skip if loading
        if (ProgressTracker::IsLoading())
        {
            return;
        }

        if (Engine::IsFlagSet(EngineMode::Playing))
        {
            // simulation (frozen while paused)
            {
                const float fixed_time_step   = 1.0f / settings::hz;
                static float accumulated_time = 0.0f;

                if (Engine::IsFlagSet(EngineMode::Paused))
                {
                    accumulated_time = 0.0f;
                }

                // accumulate delta time
                if (!Engine::IsFlagSet(EngineMode::Paused))
                {
                    accumulated_time += static_cast<float>(Timer::GetDeltaTimeSec());
                }

                // perform simulation steps
                while (accumulated_time >= fixed_time_step)
                {
                    // simulate one fixed time step
                    lock_guard<recursive_mutex> lock(physx_mutex);

                    // run the vehicle force model in lockstep with the integration so its forces
                    // are consumed by exactly this step and it reads the pose from the previous one
                    if (vehicle_step_callback)
                    {
                        vehicle_step_callback(fixed_time_step);
                    }

                    // buoyancy from the fft water, applied per step so the force integrates consistently
                    Physics::TickBuoyancy();

                    scene->simulate(fixed_time_step);
                    scene->fetchResults(true); // block
                    accumulated_time -= fixed_time_step;
                }
                
                // compute interpolation alpha for smooth rendering
                // alpha = how far into the next physics step we are (0 to 1)
                interpolation::alpha = accumulated_time / fixed_time_step;
            }
            // object picking
            {
                if (Input::GetKeyDown(KeyCode::Click_Left) && Input::GetMouseIsInViewport())
                {
                    picking::PickBody();
                }
                else if (Input::GetKeyUp(KeyCode::Click_Left))
                {
                    picking::UnpickBody();
                }
                picking::MovePickedBody();
            }
        }

        // debug visualization (editor only, skip during play)
        if (cvar_physics.GetValueAs<bool>() && !Engine::IsFlagSet(EngineMode::Playing))
        {
            lock_guard<recursive_mutex> lock(physx_mutex);

            // run a near-zero step so physx populates the render buffer (physx requires dt > 0)
            scene->simulate(numeric_limits<float>::min());
            scene->fetchResults(true);

            const PxRenderBuffer& rb = scene->getRenderBuffer();
            for (PxU32 i = 0; i < rb.getNbLines(); i++)
            {
                const PxDebugLine& line = rb.getLines()[i];
                Vector3 start(line.pos0.x, line.pos0.y, line.pos0.z);
                Vector3 end(line.pos1.x, line.pos1.y, line.pos1.z);
                Color color(
                    ((line.color0 >> 16) & 0xFF) / 255.0f,
                    ((line.color0 >> 8) & 0xFF) / 255.0f,
                    (line.color0 & 0xFF) / 255.0f
                );
                Renderer::DrawLine(start, end, color, color);
            }
        }
    }

    void PhysicsWorld::AddActor(PxRigidActor* actor)
    {
        if (actor && scene && !actor->getScene())
        {
            lock_guard<recursive_mutex> lock(physx_mutex);
            scene->addActor(*actor);
        }
    }

    void PhysicsWorld::RemoveActor(PxRigidActor* actor)
    {
        if (actor && scene && actor->getScene() == scene)
        {
            lock_guard<recursive_mutex> lock(physx_mutex);
            scene->removeActor(*actor);
        }
    }

    Vector3 PhysicsWorld::GetGravity()
    {
        // read from the cpu side settings rather than scene->getGravity() so worker threads
        // cooking physics during world load don't race against the main thread's pxscene access,
        // physx flags any concurrent read/write into NpScene as undefined and corrupts the pruner
        return Vector3(0.0f, settings::gravity, 0.0f);
    }

    void* PhysicsWorld::GetScene()
    {
        return static_cast<void*>(scene);
    }

    void* PhysicsWorld::GetPhysics()
    {
        return static_cast<void*>(physics);
    }

    recursive_mutex& PhysicsWorld::GetMutex()
    {
        return physx_mutex;
    }
    
    float PhysicsWorld::GetInterpolationAlpha()
    {
        return interpolation::alpha;
    }

    void PhysicsWorld::SetVehicleStepCallback(const function<void(float)>& callback)
    {
        lock_guard<recursive_mutex> lock(physx_mutex);
        vehicle_step_callback = callback;
    }

    bool PhysicsWorld::RaycastStatic(const Vector3& origin, const Vector3& direction, float max_distance, Vector3& hit_position)
    {
        Entity* unused = nullptr;
        return RaycastStatic(origin, direction, max_distance, hit_position, unused);
    }

    bool PhysicsWorld::RaycastStatic(const Vector3& origin, const Vector3& direction, float max_distance, Vector3& hit_position, Entity*& hit_entity)
    {
        hit_entity = nullptr;

        if (!scene)
        {
            return false;
        }

        PxVec3 px_origin(origin.x, origin.y, origin.z);
        PxVec3 px_direction(direction.x, direction.y, direction.z);
        px_direction.normalize();

        PxRaycastBuffer hit;
        PxQueryFilterData filter_data(PxQueryFlag::eSTATIC);

        lock_guard<recursive_mutex> lock(physx_mutex);
        if (scene->raycast(px_origin, px_direction, max_distance, hit, PxHitFlag::eDEFAULT, filter_data) && hit.hasBlock)
        {
            hit_position = Vector3(hit.block.position.x, hit.block.position.y, hit.block.position.z);

            if (hit.block.actor && hit.block.actor->userData)
            {
                hit_entity = static_cast<Entity*>(hit.block.actor->userData);
            }

            return true;
        }

        return false;
    }
}
