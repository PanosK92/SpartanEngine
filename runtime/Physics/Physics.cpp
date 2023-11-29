/*
Copyright(c) 2016-2023 Panos Karabelas

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
        static btBroadphaseInterface* m_broadphase                        = nullptr;
        static btCollisionDispatcher* m_collision_dispatcher              = nullptr;
        static btSequentialImpulseConstraintSolver* m_constraint_solver   = nullptr;
        static btDefaultCollisionConfiguration* m_collision_configuration = nullptr;
        static btDiscreteDynamicsWorld* m_world                           = nullptr;
        static btSoftBodyWorldInfo* m_world_info                          = nullptr;
        static PhysicsDebugDraw* m_debug_draw                             = nullptr;

        // world properties
        static int m_max_sub_steps        = 1;
        static int m_max_solve_iterations = 256;
        static float m_internal_hz        = 200.0f; // almost mandatory for advanced car physics
        static Math::Vector3 m_gravity    = Math::Vector3(0.0f, -9.81f, 0.0f);

        // picking
        static btRigidBody* m_picked_body                = nullptr;
        static btTypedConstraint* m_picked_constraint    = nullptr;
        static int m_activation_state                    = 0;
        static Math::Vector3 m_hit_position              = Math::Vector3::Zero;
        static Math::Vector3 m_picking_position_previous = Math::Vector3::Zero;
        static float m_picking_distance_previous         = 0.0f;

        static const bool m_soft_body_support = true;
    }

    void Physics::Initialize()
    {
        m_broadphase        = new btDbvtBroadphase();
        m_constraint_solver = new btSequentialImpulseConstraintSolver();

        if (m_soft_body_support)
        {
            // create
            m_collision_configuration = new btSoftBodyRigidBodyCollisionConfiguration();
            m_collision_dispatcher    = new btCollisionDispatcher(m_collision_configuration);
            m_world                   = new btSoftRigidDynamicsWorld(m_collision_dispatcher, m_broadphase, m_constraint_solver, m_collision_configuration);

            // setup         
            m_world_info = new btSoftBodyWorldInfo();
            m_world_info->m_sparsesdf.Initialize();
            m_world->getDispatchInfo().m_enableSPU = true;
            m_world_info->m_dispatcher             = m_collision_dispatcher;
            m_world_info->m_broadphase             = m_broadphase;
            m_world_info->air_density              = (btScalar)1.2;
            m_world_info->water_density            = 0;
            m_world_info->water_offset             = 0;
            m_world_info->water_normal             = btVector3(0, 0, 0);
            m_world_info->m_gravity                = ToBtVector3(m_gravity);

        }
        else
        {
            // create
            m_collision_configuration = new btDefaultCollisionConfiguration();
            m_collision_dispatcher    = new btCollisionDispatcher(m_collision_configuration);
            m_world                   = new btDiscreteDynamicsWorld(m_collision_dispatcher, m_broadphase, m_constraint_solver, m_collision_configuration);
        }

        // setup
        m_world->setGravity(ToBtVector3(m_gravity));
        m_world->getDispatchInfo().m_useContinuous = true;
        m_world->getSolverInfo().m_splitImpulse    = false;
        m_world->getSolverInfo().m_numIterations   = m_max_solve_iterations;

        // get version
        const string major = to_string(btGetVersion() / 100);
        const string minor = to_string(btGetVersion()).erase(0, 1);
        Settings::RegisterThirdPartyLib("Bullet", major + "." + minor, "https://github.com/bulletphysics/bullet3");

        // enabled debug drawing
        {
            m_debug_draw = new PhysicsDebugDraw();

            if (m_world)
            {
                m_world->setDebugDrawer(m_debug_draw);
            }
        }
    }

    void Physics::Shutdown()
    {
        delete m_world;
        delete m_constraint_solver;
        delete m_collision_dispatcher;
        delete m_collision_configuration;
        delete m_broadphase;
        delete m_world_info;
        delete m_debug_draw;
    }

    void Physics::Tick()
    {
        SP_PROFILE_FUNCTION();

        bool is_in_editor_mode = !Engine::IsFlagSet(EngineMode::Game);
        bool physics_enabled   = Engine::IsFlagSet(EngineMode::Physics);
        bool debug_draw        = Renderer::GetOption<bool>(Renderer_Option::Debug_Physics);
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

            // determine the internal time step and max sub-steps based on the internal frequency
            float real_world_elapsed_time = static_cast<float>(Timer::GetDeltaTimeSec());
            float internal_time_step      = 1.0f / m_internal_hz;
            uint32_t max_substeps         = static_cast<uint32_t>(real_world_elapsed_time / internal_time_step);

            // if max_substeps is zero, it means the internal frequency is too high for the elapsed real-world time.
            // in this case, set max_substeps to 1 to ensure the simulation advances.
            max_substeps = max_substeps > 0 ? max_substeps : 1;

            // step the physics world
            m_world->stepSimulation(real_world_elapsed_time, max_substeps, internal_time_step);
        }

        if (debug_draw)
        {
            m_world->debugDrawWorld();
        }
    }

    vector<btRigidBody*> Physics::RayCast(const Vector3& start, const Vector3& end)
    {
        btVector3 bt_start = ToBtVector3(start);
        btVector3 bt_end   = ToBtVector3(end);

        btCollisionWorld::AllHitsRayResultCallback ray_callback(bt_start, bt_end);
        m_world->rayTest(bt_start, bt_end, ray_callback);

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
        m_world->rayTest(bt_start, bt_end, ray_callback);

        if (ray_callback.hasHit())
        {
            return ToVector3(ray_callback.m_hitPointWorld);
        }

        return Vector3::Infinity;
    }

    void Physics::AddBody(btRigidBody* body)
    {
        m_world->addRigidBody(body);
    }

    void Physics::RemoveBody(btRigidBody*& body)
    {
        m_world->removeRigidBody(body);
    }

    void Physics::AddBody(btRaycastVehicle* body)
    {
        m_world->addVehicle(body);
    }

    void Physics::RemoveBody(btRaycastVehicle*& body)
    {
        m_world->removeVehicle(body);
    }

    void Physics::AddConstraint(btTypedConstraint* constraint, bool collision_with_linked_body /*= true*/)
    {
        m_world->addConstraint(constraint, !collision_with_linked_body);
    }

    void Physics::RemoveConstraint(btTypedConstraint*& constraint)
    {
        m_world->removeConstraint(constraint);
        delete constraint;
    }

    void Physics::AddBody(btSoftBody* body)
    {
        if (btSoftRigidDynamicsWorld* world = static_cast<btSoftRigidDynamicsWorld*>(m_world))
        {
            world->addSoftBody(body);
        }
    }

    void Physics::RemoveBody(btSoftBody*& body)
    {
        if (btSoftRigidDynamicsWorld* world = static_cast<btSoftRigidDynamicsWorld*>(m_world))
        {
            world->removeSoftBody(body);
            delete body;
        }
    }

    Vector3 Physics::GetGravity()
    {
        return ToVector3(m_world->getGravity());
    }

    btSoftBodyWorldInfo& Physics::GetSoftWorldInfo()
    {
        return *m_world_info;
    }

    void* Physics::GetPhysicsDebugDraw()
    {
        return static_cast<void*>(m_debug_draw);
    }

    void* Physics::GetWorld()
    {
        return static_cast<void*>(m_world);
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
                m_world->rayTest(bt_ray_start, bt_ray_end, rayCallback);

                if (rayCallback.hasHit())
                {
                    btVector3 pick_position = rayCallback.m_hitPointWorld;

                    if (btRigidBody* body = (btRigidBody*)btRigidBody::upcast(rayCallback.m_collisionObject))
                    {
                        if (!(body->isStaticObject() || body->isKinematicObject()))
                        {
                            m_picked_body                 = body;
                            m_activation_state            = m_picked_body->getActivationState();
                            m_picked_body->setActivationState(DISABLE_DEACTIVATION);
                            btVector3 localPivot          = body->getCenterOfMassTransform().inverse() * pick_position;
                            btPoint2PointConstraint* p2p  = new btPoint2PointConstraint(*body, localPivot);
                            m_world->addConstraint(p2p, true);
                            m_picked_constraint           = p2p;
                            btScalar mouse_pick_clamping  = 30.0f;
                            p2p->m_setting.m_impulseClamp = mouse_pick_clamping;
                            p2p->m_setting.m_tau          = 0.001f; // very weak constraint for picking
                        }
                    }

                    m_hit_position              = ToVector3(pick_position);
                    m_picking_distance_previous = (m_hit_position - ray_start).Length();
                }
            }
        }
    }

    void Physics::UnpickBody()
    {
        if (m_picked_constraint)
        {
            m_picked_body->forceActivationState(m_activation_state);
            m_picked_body->activate();
            m_world->removeConstraint(m_picked_constraint);
            delete m_picked_constraint;
            m_picked_constraint = nullptr;
            m_picked_body       = nullptr;
        }
    }

    void Physics::MovePickedBody()
    {
        if (shared_ptr<Camera> camera = Renderer::GetCamera())
        {
            Ray picking_ray       = camera->ComputePickingRay();
            Vector3 ray_start     = picking_ray.GetStart();
            Vector3 ray_direction = picking_ray.GetDirection();

            if (m_picked_body && m_picked_constraint)
            {
                if (btPoint2PointConstraint* pick_constraint = static_cast<btPoint2PointConstraint*>(m_picked_constraint))
                {
                    // keep it at the same picking distance
                    ray_direction *= m_picking_distance_previous;
                    Vector3 new_pivot_b = ray_start + ray_direction;
                    pick_constraint->setPivotB(ToBtVector3(new_pivot_b));
                }
            }
        }
    }
}
