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
#include "../profiling/Profiler.h"
#include "../rendering/Renderer.h"
#include "../input/Input.h"
#include "../world/Entity.h"
#include "../world/components/Camera.h"
#include "../world/components/Physics.h"
#include "../car/CarSimulation.h"
#include "../world/components/Ragdoll.h"
#include "../world/World.h"
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
#include "PhysicsSceneConfig.h"
#include "PhysicsSceneOrigin.h"
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
        PhysicsSceneOrigin scene_origin;
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
        struct VehicleStepCallback
        {
            const void* owner;
            std::function<void(float)> callback;
        };

        vector<VehicleStepCallback> vehicle_step_callbacks;
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
            const Vector3 local_start = PhysicsWorld::ToPhysicsPosition(picking_ray.GetStart());
            PxVec3 origin(local_start.x, local_start.y, local_start.z);
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
                    // frozen ragdoll limbs are kinematic, wake them so the pick joint can drag
                    if (Entity* entity = static_cast<Entity*>(dynamic->userData))
                    {
                        if (Ragdoll* ragdoll = entity->GetComponent<Ragdoll>())
                        {
                            if (ragdoll->IsFrozen())
                            {
                                const Vector3 wake_pos(
                                    hit.block.position.x,
                                    hit.block.position.y,
                                    hit.block.position.z
                                );
                                if (!ragdoll->Wake(PhysicsWorld::ToWorldPosition(wake_pos), Vector3::Zero))
                                {
                                    return;
                                }
                            }

                            // drop kick velocity so grab does not fling him upward
                            if (ragdoll->IsDead())
                            {
                                ragdoll->PrepareForPick();
                            }
                        }
                    }

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

                    // kill motion on the grabbed limb itself before the spring attaches
                    picked_body->setLinearVelocity(PxVec3(0.0f, 0.0f, 0.0f));
                    picked_body->setAngularVelocity(PxVec3(0.0f, 0.0f, 0.0f));
                    picked_body->clearForce();
                    picked_body->clearTorque();

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

                    // stiff + heavily damped so the grab snaps to the mouse without bouncing
                    const float stiffness = 2500.0f;
                    const float damping   = 500.0f;
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
            const Vector3 local_start = PhysicsWorld::ToPhysicsPosition(picking_ray.GetStart());
            PxVec3 origin(local_start.x, local_start.y, local_start.z);
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
        static mutex contact_mutex;
        static vector<PhysicsContact> pending_contacts;

        void queue_contact(Entity* entity_a, Entity* entity_b, const Vector3& position, const Vector3& normal, const Vector3& impulse)
        {
            if (!entity_a || !entity_b)
            {
                return;
            }

            PhysicsContact contact;
            contact.entity_a = entity_a;
            contact.entity_b = entity_b;
            contact.position = position;
            contact.normal = normal;
            contact.impulse = impulse;

            lock_guard<mutex> lock(contact_mutex);
            pending_contacts.push_back(contact);
        }

        class ContactReportCallback : public PxSimulationEventCallback
        {
        public:
            void onConstraintBreak(PxConstraintInfo*, PxU32) override {}
            void onWake(PxActor**, PxU32) override {}
            void onSleep(PxActor**, PxU32) override {}
            void onAdvance(const PxRigidBody* const*, const PxTransform*, const PxU32) override {}

            void onContact(const PxContactPairHeader& pair_header, const PxContactPair* pairs, PxU32 pair_count) override
            {
                if (pair_header.flags & (PxContactPairHeaderFlag::eREMOVED_ACTOR_0 | PxContactPairHeaderFlag::eREMOVED_ACTOR_1))
                {
                    return;
                }

                Entity* entity_a = pair_header.actors[0]
                    ? static_cast<Entity*>(pair_header.actors[0]->userData)
                    : nullptr;
                Entity* entity_b = pair_header.actors[1]
                    ? static_cast<Entity*>(pair_header.actors[1]->userData)
                    : nullptr;
                // Record the complete contact impulse for each vehicle even if
                // the other actor has no entity (terrain/bench/static geometry).
                PxVec3 vehicle_impulse(0);
                for (PxU32 i = 0; i < pair_count; ++i)
                {
                    vector<PxContactPairPoint> contacts(pairs[i].contactCount);
                    PxU32 count = contacts.empty() ? 0 : pairs[i].extractContacts(contacts.data(), static_cast<PxU32>(contacts.size()));
                    for (PxU32 j = 0; j < count; ++j) vehicle_impulse += contacts[j].impulse;
                }
                auto record = [&](Entity* entity, const PxVec3& impulse) {
                    if (entity) if (auto* component = entity->GetComponent<Physics>())
                        if (auto* simulation = component->GetVehicleSimulation()) simulation->record_contact_impulse(impulse);
                };
                record(entity_a, vehicle_impulse); record(entity_b, -vehicle_impulse);
                if (!entity_a || !entity_b) return;

                for (PxU32 i = 0; i < pair_count; ++i)
                {
                    const PxContactPair& pair = pairs[i];
                    // found + persists: a fast car can stay overlapping after a weak first touch
                    const PxU32 touch_events =
                        PxPairFlag::eNOTIFY_TOUCH_FOUND | PxPairFlag::eNOTIFY_TOUCH_PERSISTS;
                    if (!(pair.events & touch_events))
                    {
                        continue;
                    }

                    Vector3 position = Vector3::Zero;
                    Vector3 normal = Vector3::Up;
                    Vector3 impulse = Vector3::Zero;
                    PxContactPairPoint points[4];
                    const PxU32 point_count = pair.extractContacts(points, 4);
                    if (point_count > 0)
                    {
                        // fetchResults can dispatch this callback on a worker
                        // while the caller holds physx_mutex. The origin is
                        // immutable for the whole simulate/fetch interval.
                        const PxVec3 world = points[0].position + scene_origin.offset;
                        position = Vector3(world.x, world.y, world.z);
                        normal = Vector3(points[0].normal.x, points[0].normal.y, points[0].normal.z);
                        impulse = Vector3(points[0].impulse.x, points[0].impulse.y, points[0].impulse.z);
                    }

                    queue_contact(entity_a, entity_b, position, normal, impulse);
                }
            }

            void onTrigger(PxTriggerPair* pairs, PxU32 count) override
            {
                for (PxU32 i = 0; i < count; ++i)
                {
                    const PxTriggerPair& pair = pairs[i];
                    if (pair.status != PxPairFlag::eNOTIFY_TOUCH_FOUND)
                    {
                        continue;
                    }
                    if (pair.flags & (PxTriggerPairFlag::eREMOVED_SHAPE_TRIGGER | PxTriggerPairFlag::eREMOVED_SHAPE_OTHER))
                    {
                        continue;
                    }

                    Entity* trigger_entity = pair.triggerActor
                        ? static_cast<Entity*>(pair.triggerActor->userData)
                        : nullptr;
                    Entity* other_entity = pair.otherActor
                        ? static_cast<Entity*>(pair.otherActor->userData)
                        : nullptr;
                    queue_contact(trigger_entity, other_entity, Vector3::Zero, Vector3::Up, Vector3::Zero);
                }
            }
        };

        static ContactReportCallback contact_callback;

        // word two tags characters, vehicles and pedestrians while word three groups one vehicle
        // character vehicle pairs and same vehicle pairs never collide
        PxFilterFlags collision_filter_shader(
            PxFilterObjectAttributes attributes0, PxFilterData filter_data0,
            PxFilterObjectAttributes attributes1, PxFilterData filter_data1,
            PxPairFlags& pair_flags, const void* constant_block, PxU32 constant_block_size)
        {
            bool is_character_vs_vehicle =
                (filter_data0.word2 == physics_collision_character && filter_data1.word2 == physics_collision_vehicle) ||
                (filter_data0.word2 == physics_collision_vehicle && filter_data1.word2 == physics_collision_character);
            bool is_same_vehicle =
                filter_data0.word2 == physics_collision_vehicle &&
                filter_data1.word2 == physics_collision_vehicle &&
                filter_data0.word3 != 0 &&
                filter_data0.word3 == filter_data1.word3;
            bool is_pedestrian_vs_pedestrian =
                filter_data0.word2 == physics_collision_pedestrian &&
                filter_data1.word2 == physics_collision_pedestrian;

            // vehicle vs ragdoll stays on, physx pushes the body; soft depenetration on spawn
            if (is_character_vs_vehicle || is_same_vehicle || is_pedestrian_vs_pedestrian)
            {
                return PxFilterFlag::eSUPPRESS;
            }

            PxFilterFlags filter_flags = PxDefaultSimulationFilterShader(
                attributes0,
                filter_data0,
                attributes1,
                filter_data1,
                pair_flags,
                constant_block,
                constant_block_size
            );

            const bool involves_pedestrian =
                filter_data0.word2 == physics_collision_pedestrian ||
                filter_data1.word2 == physics_collision_pedestrian;
            const bool involves_ragdoll =
                filter_data0.word2 == physics_collision_ragdoll ||
                filter_data1.word2 == physics_collision_ragdoll;

            const bool involves_vehicle = filter_data0.word2 == physics_collision_vehicle || filter_data1.word2 == physics_collision_vehicle;
            if ((involves_pedestrian || involves_ragdoll || involves_vehicle) &&
                !PxFilterObjectIsTrigger(attributes0) &&
                !PxFilterObjectIsTrigger(attributes1))
            {
                pair_flags |= PxPairFlag::eNOTIFY_TOUCH_FOUND;
                pair_flags |= PxPairFlag::eNOTIFY_TOUCH_PERSISTS;
                pair_flags |= PxPairFlag::eNOTIFY_CONTACT_POINTS;
                pair_flags |= PxPairFlag::eDETECT_CCD_CONTACT;
            }

            if (!PxFilterObjectIsTrigger(attributes0) && !PxFilterObjectIsTrigger(attributes1))
            {
                pair_flags |= PxPairFlag::eDETECT_CCD_CONTACT;
            }
            return filter_flags;
        }
    }

    namespace
    {
        // physx builds its debug render buffer inside fetchResults for every visualised shape, so these
        // stay off until something actually draws them, a terrain grid is millions of triangles and
        // generating lines for it stalls every simulation step even when nobody reads the buffer
        void set_visualization_enabled(bool enabled)
        {
            if (!scene)
            {
                return;
            }

            const float value = enabled ? 1.0f : 0.0f;
            scene->setVisualizationParameter(PxVisualizationParameter::eSCALE, value);
            scene->setVisualizationParameter(PxVisualizationParameter::eCULL_BOX, value);
            scene->setVisualizationParameter(PxVisualizationParameter::eWORLD_AXES, value);
            scene->setVisualizationParameter(PxVisualizationParameter::eACTOR_AXES, value);
            scene->setVisualizationParameter(PxVisualizationParameter::eCOLLISION_SHAPES, value);
            scene->setVisualizationParameter(PxVisualizationParameter::eCOLLISION_AXES, value);
            scene->setVisualizationParameter(PxVisualizationParameter::eCOLLISION_COMPOUNDS, value);
            scene->setVisualizationParameter(PxVisualizationParameter::eCOLLISION_EDGES, value);
            scene->setVisualizationParameter(PxVisualizationParameter::eCONTACT_POINT, value);
            scene->setVisualizationParameter(PxVisualizationParameter::eCONTACT_NORMAL, value);
            scene->setVisualizationParameter(PxVisualizationParameter::eCONTACT_ERROR, value);
            scene->setVisualizationParameter(PxVisualizationParameter::eCONTACT_FORCE, value);
            scene->setVisualizationParameter(PxVisualizationParameter::eJOINT_LOCAL_FRAMES, value);
            scene->setVisualizationParameter(PxVisualizationParameter::eJOINT_LIMITS, value);
        }
    }

    void PhysicsWorld::Initialize()
    {
        scene_origin = PhysicsSceneOrigin();
        // foundation
        foundation = PxCreateFoundation(PX_PHYSICS_VERSION, allocator, logger);
        SP_ASSERT(foundation);

        // physics
        physics = PxCreatePhysics(PX_PHYSICS_VERSION, *foundation, PxTolerancesScale(), false, nullptr);
        SP_ASSERT(physics);

        // scene
        PxSceneDesc scene_desc(physics->getTolerancesScale());
        scene_desc.gravity                 = PxVec3(0.0f, settings::gravity, 0.0f);
        scene_desc.cpuDispatcher           = PxDefaultCpuDispatcherCreate(2);
        scene_desc.filterShader            = collision_filter_shader;
        scene_desc.simulationEventCallback = &contact_callback;
        ConfigurePhysicsScene(scene_desc);
        scene                              = physics->createScene(scene_desc);
        SP_ASSERT(scene);

        // store dispatcher
        dispatcher = static_cast<PxDefaultCpuDispatcher*>(scene_desc.cpuDispatcher);

        // debug visualization parameters are enabled on demand, see DrawDebugVisualization
        set_visualization_enabled(false);
    }

    void PhysicsWorld::Shutdown()
    {
        // cleanup picking
        picking::UnpickBody();

        // release controller manager (owned by physics component system)
        Physics::Shutdown();
        vehicle_step_callbacks.clear();

        {
            lock_guard<mutex> lock(contact_mutex);
            pending_contacts.clear();
        }

        // release physx resources
        PX_RELEASE(scene);
        PX_RELEASE(dispatcher);
        PX_RELEASE(physics);
        PX_RELEASE(foundation);
    }

    const vector<PhysicsContact>& PhysicsWorld::GetFrameContacts()
    {
        return pending_contacts;
    }

    vector<PhysicsContact> PhysicsWorld::ConsumeContacts()
    {
        lock_guard<mutex> lock(contact_mutex);
        vector<PhysicsContact> contacts;
        contacts.swap(pending_contacts);
        return contacts;
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

                // fresh contact list for this frame's simulation steps
                {
                    lock_guard<mutex> lock(contact_mutex);
                    pending_contacts.clear();
                }

                // perform simulation steps
                while (accumulated_time >= fixed_time_step)
                {
                    // simulate one fixed time step
                    lock_guard<recursive_mutex> lock(physx_mutex);

                    // snapshot entries so callback registration can change during an update
                    if (Camera* camera = World::GetCamera()) RebaseOrigin(camera->GetEntity()->GetPosition());
                    vector<VehicleStepCallback> callbacks;
                    callbacks.reserve(vehicle_step_callbacks.size());
                    for (const VehicleStepCallback& entry : vehicle_step_callbacks)
                    {
                        callbacks.push_back(entry);
                    }
                    for (const VehicleStepCallback& entry : callbacks)
                    {
                        const bool registered = any_of(
                            vehicle_step_callbacks.begin(),
                            vehicle_step_callbacks.end(),
                            [&entry](const VehicleStepCallback& current)
                            {
                                return current.owner == entry.owner;
                            }
                        );
                        if (registered)
                        {
                            entry.callback(fixed_time_step);
                        }
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
            // object picking, skip when right is held so cube shoot does not also grab
            {
                if (Input::GetKeyDown(KeyCode::Click_Left) &&
                    Input::GetMouseIsInViewport() &&
                    !Input::GetKey(KeyCode::Click_Right))
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

    }

    void PhysicsWorld::DrawDebugVisualization()
    {
        const bool wants_visualization =
            cvar_physics.GetValueAs<bool>() &&
            !Engine::IsFlagSet(EngineMode::Playing) &&
            !ProgressTracker::IsLoading();

        // the parameters have to be live before the simulate call that fills the buffer, and off again
        // the moment they are not needed, they are not free
        static bool visualization_enabled = false;
        if (visualization_enabled != wants_visualization)
        {
            lock_guard<recursive_mutex> lock(physx_mutex);
            set_visualization_enabled(wants_visualization);
            visualization_enabled = wants_visualization;
        }

        if (!wants_visualization)
        {
            return;
        }

        lock_guard<recursive_mutex> lock(physx_mutex);

        // physx emits a line per triangle of every visualised shape, so a terrain grid has to be clipped
        // to what is around the camera, the horizontal reach matches the distance at which static bodies
        // deactivate so the box shows everything that is actually simulating near you
        {
            Vector3 centre = Vector3::Zero;
            if (Camera* camera = World::GetCamera())
            {
                centre = ToPhysicsPosition(camera->GetEntity()->GetPosition());
            }

            const float reach_horizontal = 80.0f;
            const float reach_vertical   = 300.0f; // the editor camera often sits well above the ground
            scene->setVisualizationCullingBox(PxBounds3(
                PxVec3(centre.x - reach_horizontal, centre.y - reach_vertical, centre.z - reach_horizontal),
                PxVec3(centre.x + reach_horizontal, centre.y + reach_vertical, centre.z + reach_horizontal)
            ));
        }

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
            Renderer::DrawLine(ToWorldPosition(start), ToWorldPosition(end), color, color);
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

    Vector3 PhysicsWorld::GetOrigin()
    {
        lock_guard<recursive_mutex> lock(physx_mutex);
        return Vector3(scene_origin.offset.x, scene_origin.offset.y, scene_origin.offset.z);
    }

    Vector3 PhysicsWorld::ToPhysicsPosition(const Vector3& position) { return position - GetOrigin(); }
    Vector3 PhysicsWorld::ToWorldPosition(const Vector3& position) { return position + GetOrigin(); }

    void PhysicsWorld::RebaseOrigin(const Vector3& focus)
    {
        lock_guard<recursive_mutex> lock(physx_mutex);
        if (!scene) return;
        const PxVec3 shift = scene_origin.Update(*scene, PxVec3(focus.x, focus.y, focus.z));
        if (!shift.isZero()) Physics::ShiftOrigin(Vector3(shift.x, shift.y, shift.z));
    }

    void PhysicsWorld::RemoveActor(PxRigidActor* actor)
    {
        if (actor && scene && actor->getScene() == scene)
        {
            lock_guard<recursive_mutex> lock(physx_mutex);
            scene->removeActor(*actor);
            scene->flushQueryUpdates();
        }
    }

    Vector3 PhysicsWorld::GetGravity()
    {
        // read the cpu side settings, physx treats a concurrent NpScene access as undefined and corrupts its pruner
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

    float PhysicsWorld::GetFixedTimeStep()
    {
        return 1.0f / settings::hz;
    }

    void PhysicsWorld::RegisterVehicleStepCallback(const void* owner, const function<void(float)>& callback)
    {
        lock_guard<recursive_mutex> lock(physx_mutex);
        if (!owner || !callback)
        {
            return;
        }

        for (VehicleStepCallback& entry : vehicle_step_callbacks)
        {
            if (entry.owner == owner)
            {
                entry.callback = callback;
                return;
            }
        }

        vehicle_step_callbacks.push_back({ owner, callback });
    }

    void PhysicsWorld::UnregisterVehicleStepCallback(const void* owner)
    {
        lock_guard<recursive_mutex> lock(physx_mutex);
        vehicle_step_callbacks.erase(remove_if(vehicle_step_callbacks.begin(), vehicle_step_callbacks.end(), [owner](const VehicleStepCallback& entry) { return entry.owner == owner; }), vehicle_step_callbacks.end());
    }

    bool PhysicsWorld::RaycastStatic(const Vector3& origin, const Vector3& direction, float max_distance, Vector3& hit_position)
    {
        Entity* unused = nullptr;
        return RaycastStatic(origin, direction, max_distance, hit_position, unused);
    }

    bool PhysicsWorld::RaycastStatic(const Vector3& origin, const Vector3& direction, float max_distance, Vector3& hit_position, Entity*& hit_entity)
    {
        PhysicsRaycastHit hit;
        const bool did_hit = RaycastStatic(
            origin,
            direction,
            max_distance,
            hit
        );
        hit_position = hit.position;
        hit_entity = hit.entity;
        return did_hit;
    }

    bool PhysicsWorld::RaycastStatic(
        const Vector3& origin,
        const Vector3& direction,
        float max_distance,
        PhysicsRaycastHit& hit_result,
        Entity* ignored_entity
    )
    {
        hit_result = PhysicsRaycastHit();
        if (!scene)
        {
            return false;
        }

        lock_guard<recursive_mutex> scene_lock(physx_mutex);
        const Vector3 local_origin = ToPhysicsPosition(origin);
        PxVec3 px_origin(local_origin.x, local_origin.y, local_origin.z);
        PxVec3 px_direction(direction.x, direction.y, direction.z);
        px_direction.normalize();

        PxRaycastBuffer hit;
        class QueryFilter : public PxQueryFilterCallback
        {
        public:
            explicit QueryFilter(Entity* ignored_entity)
                : m_ignored_entity(ignored_entity)
            {
            }

            PxQueryHitType::Enum preFilter(
                const PxFilterData&,
                const PxShape*,
                const PxRigidActor* actor,
                PxHitFlags&
            ) override
            {
                Entity* hit_entity =
                    actor && actor->userData
                    ? static_cast<Entity*>(actor->userData)
                    : nullptr;
                for (
                    Entity* current = hit_entity;
                    current != nullptr;
                    current = current->GetParent()
                )
                {
                    if (current == m_ignored_entity)
                    {
                        return PxQueryHitType::eNONE;
                    }
                }
                return PxQueryHitType::eBLOCK;
            }

            PxQueryHitType::Enum postFilter(
                const PxFilterData&,
                const PxQueryHit&,
                const PxShape*,
                const PxRigidActor*
            ) override
            {
                return PxQueryHitType::eBLOCK;
            }

        private:
            Entity* m_ignored_entity = nullptr;
        };

        PxQueryFilterData filter_data(
            PxQueryFlag::eSTATIC |
            PxQueryFlag::ePREFILTER
        );
        QueryFilter filter(ignored_entity);

        lock_guard<recursive_mutex> lock(physx_mutex);
        if (
            scene->raycast(
                px_origin,
                px_direction,
                max_distance,
                hit,
                PxHitFlag::eDEFAULT,
                filter_data,
                &filter
            ) &&
            hit.hasBlock
        )
        {
            hit_result.position = ToWorldPosition(Vector3(
                hit.block.position.x,
                hit.block.position.y,
                hit.block.position.z
            ));
            hit_result.normal = Vector3(
                hit.block.normal.x,
                hit.block.normal.y,
                hit.block.normal.z
            ).Normalized();
            hit_result.distance = hit.block.distance;

            if (hit.block.actor && hit.block.actor->userData)
            {
                hit_result.entity =
                    static_cast<Entity*>(hit.block.actor->userData);
            }

            return true;
        }

        return false;
    }

    bool PhysicsWorld::SphereCast(const Vector3& origin, const Vector3& direction, float radius, float max_distance, uint32_t ignored_collision_group, Vector3& hit_position, float& hit_distance, Entity*& hit_entity)
    {
        hit_entity   = nullptr;
        hit_distance = max_distance;

        if (!scene || radius <= 0.0f || max_distance <= 0.0f || direction.LengthSquared() <= 0.0f)
        {
            return false;
        }

        const Vector3 normalized_direction =
            direction.Normalized();
        Vector3 right = Vector3::Cross(
            normalized_direction,
            Vector3::Up
        );
        if (right.LengthSquared() <= 0.0001f)
        {
            right = Vector3::Right;
        }
        else
        {
            right.Normalize();
        }

        const array<Vector3, 5> ray_origins =
        {
            origin,
            origin + right * radius,
            origin - right * radius,
            origin + Vector3::Up * radius,
            origin - Vector3::Up * radius
        };

        bool did_hit = false;
        for (const Vector3& ray_origin : ray_origins)
        {
            PhysicsRaycastHit ray_hit;
            if (
                RaycastStatic(
                    ray_origin,
                    normalized_direction,
                    max_distance,
                    ray_hit
                ) &&
                ray_hit.distance < hit_distance
            )
            {
                did_hit = true;
                hit_position = ray_hit.position;
                hit_distance = ray_hit.distance;
                hit_entity = ray_hit.entity;
            }
        }

        (void)ignored_collision_group;
        return did_hit;
    }
}
