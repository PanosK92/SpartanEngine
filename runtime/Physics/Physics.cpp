/*
Copyright(c) 2016-2024 Panos Karabelas

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

//= INCLUDES ===================================================================
#include "pch.h"
#include "Physics.h"
#include "PhysicsDebugDraw.h"
#include "BulletPhysicsHelper.h"
#include "ProgressTracker.h"
#include "../Profiling/Profiler.h"
#include "../Rendering/Renderer.h"
#include "../Input/Input.h"
#include "../World/Components/Camera.h"
SP_WARNINGS_OFF
#include <btBulletDynamicsCommon.h>
#include <BulletDynamics/ConstraintSolver/btPoint2PointConstraint.h>
#include <BulletDynamics/ConstraintSolver/btSequentialImpulseConstraintSolver.h>
#include <BulletCollision/BroadphaseCollision/btDbvtBroadphase.h>
#include <BulletCollision/NarrowPhaseCollision/btRaycastCallback.h>
#include <BulletSoftBody/btSoftBodyRigidBodyCollisionConfiguration.h>
#include <BulletSoftBody/btSoftRigidDynamicsWorld.h>
SP_WARNINGS_ON
//==============================================================================

//= NAMESPACES ================
using namespace std;
using namespace Spartan::Math;
//=============================

namespace Spartan
{
    namespace
    { 
        btBroadphaseInterface* broadphase                        = nullptr;
        btCollisionDispatcher* collision_dispatcher              = nullptr;
        btSequentialImpulseConstraintSolver* constraint_solver   = nullptr;
        btDefaultCollisionConfiguration* collision_configuration = nullptr;
        btDiscreteDynamicsWorld* world                           = nullptr;
        btSoftBodyWorldInfo* world_info                          = nullptr;
        PhysicsDebugDraw* debug_draw                             = nullptr;

        // world properties
        int max_solve_iterations       = 256;
        const float internal_time_step = 1.0f / 200.0f; // 200 Hz physics resolution (needed for car simulation)
        const float real_time_step     = 1.0f / 60.0f;  // 60 Hz update rate
        float accumulator              = 0.0f;
        Math::Vector3 gravity          = Math::Vector3(0.0f, -9.81f, 0.0f);

        // picking
        btRigidBody* picked_body                = nullptr;
        btTypedConstraint* picked_constraint    = nullptr;
        int activation_state                    = 0;
        Math::Vector3 hit_position              = Math::Vector3::Zero;
        Math::Vector3 picking_position_previous = Math::Vector3::Zero;
        float picking_distance_previous         = 0.0f;

        const bool soft_body_support = true;
    }

    void Physics::Initialize()
    {
        broadphase        = new btDbvtBroadphase();
        constraint_solver = new btSequentialImpulseConstraintSolver();

        if (soft_body_support)
        {
            // create
            collision_configuration = new btSoftBodyRigidBodyCollisionConfiguration();
            collision_dispatcher    = new btCollisionDispatcher(collision_configuration);
            world                   = new btSoftRigidDynamicsWorld(collision_dispatcher, broadphase, constraint_solver, collision_configuration);

            // setup         
            world_info = new btSoftBodyWorldInfo();
            world_info->m_sparsesdf.Initialize();
            world->getDispatchInfo().m_enableSPU = true;
            world_info->m_dispatcher             = collision_dispatcher;
            world_info->m_broadphase             = broadphase;
            world_info->air_density              = (btScalar)1.2;
            world_info->water_density            = 0;
            world_info->water_offset             = 0;
            world_info->water_normal             = btVector3(0, 0, 0);
            world_info->m_gravity                = ToBtVector3(gravity);
        }
        else
        {
            // create
            collision_configuration = new btDefaultCollisionConfiguration();
            collision_dispatcher    = new btCollisionDispatcher(collision_configuration);
            world                   = new btDiscreteDynamicsWorld(collision_dispatcher, broadphase, constraint_solver, collision_configuration);
        }

        // setup
        world->setGravity(ToBtVector3(gravity));
        world->getDispatchInfo().m_useContinuous = true;
        world->getSolverInfo().m_splitImpulse    = false;
        world->getSolverInfo().m_numIterations   = max_solve_iterations;

        // get version
        const string major = to_string(btGetVersion() / 100);
        const string minor = to_string(btGetVersion()).erase(0, 1);
        Settings::RegisterThirdPartyLib("Bullet", major + "." + minor, "https://github.com/bulletphysics/bullet3");

        // enabled debug drawing
        {
            debug_draw = new PhysicsDebugDraw();

            if (world)
            {
                world->setDebugDrawer(debug_draw);
            }
        }
    }

    void Physics::Shutdown()
    {
        delete world;
        delete constraint_solver;
        delete collision_dispatcher;
        delete collision_configuration;
        delete broadphase;
        delete world_info;
        delete debug_draw;
    }

    void Physics::Tick()
    {
        SP_PROFILE_CPU();

        bool is_in_editor_mode = !Engine::IsFlagSet(EngineMode::Game);
        bool physics_enabled   = Engine::IsFlagSet(EngineMode::Physics);
        bool debug_draw        = Renderer::GetOption<bool>(Renderer_Option::Physics);
        bool simulate_physics  = physics_enabled && !is_in_editor_mode;

        // don't simulate or debug draw when loading a world (a different thread could be creating physics objects)
        if (ProgressTracker::IsLoading())
            return;

        if (simulate_physics)
        {
            // Picking
            {
                if (Input::GetKeyDown(KeyCode::Click_Left) && Input::GetMouseIsInViewport())
                {
                    PickBody();
                }
                else if (Input::GetKeyUp(KeyCode::Click_Left))
                {
                    UnpickBody();
                }

                MovePickedBody();
            }

            // accumulate elapsed time
            float freme_time  = static_cast<float>(Timer::GetDeltaTimeSec());
            accumulator      += freme_time;
            // update physics as many times as needed to consume the accumulator at 200 Hz rate
            while (accumulator >= internal_time_step)
            {
                world->stepSimulation(internal_time_step, 1, internal_time_step);
                accumulator -= internal_time_step;
            }
        }

        if (debug_draw)
        {
            world->debugDrawWorld();
        }
    }

    vector<btRigidBody*> Physics::RayCast(const Vector3& start, const Vector3& end)
    {
        btVector3 bt_start = ToBtVector3(start);
        btVector3 bt_end   = ToBtVector3(end);

        btCollisionWorld::AllHitsRayResultCallback ray_callback(bt_start, bt_end);
        world->rayTest(bt_start, bt_end, ray_callback);

        vector<btRigidBody*> hit_bodies;
        if (ray_callback.hasHit())
        {
            for (int i = 0; i < ray_callback.m_collisionObjects.size(); ++i)
            {
                if (const btRigidBody* body = btRigidBody::upcast(ray_callback.m_collisionObjects[i]))
                {
                    hit_bodies.push_back(const_cast<btRigidBody*>(body));
                }
            }
        }

        return hit_bodies;
    }

    Vector3 Physics::RayCastFirstHitPosition(const Math::Vector3& start, const Math::Vector3& end)
    {
        btVector3 bt_start = ToBtVector3(start);
        btVector3 bt_end   = ToBtVector3(end);

        btCollisionWorld::ClosestRayResultCallback ray_callback(bt_start, bt_end);
        world->rayTest(bt_start, bt_end, ray_callback);

        if (ray_callback.hasHit())
        {
            return ToVector3(ray_callback.m_hitPointWorld);
        }

        return Vector3::Infinity;
    }

    void Physics::AddBody(btRigidBody* body)
    {
        world->addRigidBody(body);
    }

    void Physics::RemoveBody(btRigidBody*& body)
    {
        world->removeRigidBody(body);
    }

    void Physics::AddBody(btRaycastVehicle* body)
    {
        world->addVehicle(body);
    }

    void Physics::RemoveBody(btRaycastVehicle*& body)
    {
        world->removeVehicle(body);
    }

    void Physics::AddConstraint(btTypedConstraint* constraint, bool collision_with_linked_body /*= true*/)
    {
        world->addConstraint(constraint, !collision_with_linked_body);
    }

    void Physics::RemoveConstraint(btTypedConstraint*& constraint)
    {
        world->removeConstraint(constraint);
        delete constraint;
    }

    void Physics::AddBody(btSoftBody* body)
    {
        if (btSoftRigidDynamicsWorld* _world = static_cast<btSoftRigidDynamicsWorld*>(world))
        {
            _world->addSoftBody(body);
        }
    }

    void Physics::RemoveBody(btSoftBody*& body)
    {
        if (btSoftRigidDynamicsWorld* _world = static_cast<btSoftRigidDynamicsWorld*>(world))
        {
            _world->removeSoftBody(body);
            delete body;
        }
    }

    Vector3& Physics::GetGravity()
    {
        return gravity;
    }

    btSoftBodyWorldInfo& Physics::GetSoftWorldInfo()
    {
        return *world_info;
    }

    void* Physics::GetPhysicsDebugDraw()
    {
        return static_cast<void*>(debug_draw);
    }

    void* Physics::GetWorld()
    {
        return static_cast<void*>(world);
    }

    float Physics::GetTimeStepInternalSec()
    {
        return 1.0f / internal_time_step;
    }

    void Physics::PickBody()
    {
        if (shared_ptr<Camera> camera = Renderer::GetCamera())
        {
            const Ray& picking_ray = camera->GetPickingRay();

            if (picking_ray.IsDefined())
            { 
                // get camera picking ray
                Vector3 ray_start     = picking_ray.GetStart();
                Vector3 ray_direction = picking_ray.GetDirection();
                Vector3 ray_end       = ray_start + ray_direction * camera->GetFarPlane();

                btVector3 bt_ray_start = ToBtVector3(ray_start);
                btVector3 bt_ray_end   = ToBtVector3(ray_end);
                btCollisionWorld::ClosestRayResultCallback rayCallback(bt_ray_start, bt_ray_end);

                rayCallback.m_flags |= btTriangleRaycastCallback::kF_UseGjkConvexCastRaytest;
                world->rayTest(bt_ray_start, bt_ray_end, rayCallback);

                if (rayCallback.hasHit())
                {
                    btVector3 pick_position = rayCallback.m_hitPointWorld;

                    if (btRigidBody* body = (btRigidBody*)btRigidBody::upcast(rayCallback.m_collisionObject))
                    {
                        if (!(body->isStaticObject() || body->isKinematicObject()))
                        {
                            picked_body                 = body;
                            activation_state            = picked_body->getActivationState();
                            picked_body->setActivationState(DISABLE_DEACTIVATION);
                            btVector3 localPivot          = body->getCenterOfMassTransform().inverse() * pick_position;
                            btPoint2PointConstraint* p2p  = new btPoint2PointConstraint(*body, localPivot);
                            world->addConstraint(p2p, true);
                            picked_constraint           = p2p;
                            btScalar mouse_pick_clamping  = 30.0f;
                            p2p->m_setting.m_impulseClamp = mouse_pick_clamping;
                            p2p->m_setting.m_tau          = 0.3f;
                            p2p->m_setting.m_damping      = 0.3f;
                        }
                    }

                    hit_position              = ToVector3(pick_position);
                    picking_distance_previous = (hit_position - ray_start).Length();
                }
            }
        }
    }

    void Physics::UnpickBody()
    {
        if (picked_constraint)
        {
            picked_body->forceActivationState(activation_state);
            picked_body->activate();
            world->removeConstraint(picked_constraint);
            delete picked_constraint;
            picked_constraint = nullptr;
            picked_body       = nullptr;
        }
    }

    void Physics::MovePickedBody()
    {
        if (shared_ptr<Camera> camera = Renderer::GetCamera())
        {
            Ray picking_ray       = camera->ComputePickingRay();
            Vector3 ray_start     = picking_ray.GetStart();
            Vector3 ray_direction = picking_ray.GetDirection();

            if (picked_body && picked_constraint)
            {
                if (btPoint2PointConstraint* pick_constraint = static_cast<btPoint2PointConstraint*>(picked_constraint))
                {
                    // keep it at the same picking distance
                    ray_direction *= picking_distance_previous;
                    Vector3 new_pivot_b = ray_start + ray_direction;
                    pick_constraint->setPivotB(ToBtVector3(new_pivot_b));
                }
            }
        }
    }
}
