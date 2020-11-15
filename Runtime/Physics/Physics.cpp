/*
Copyright(c) 2016-2020 Panos Karabelas

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

//= INCLUDES =====================
#include "Spartan.h"
#include "Physics.h"
#include "PhysicsDebugDraw.h"
#include "BulletPhysicsHelper.h"
#include "../Profiling/Profiler.h"
#include "../Rendering/Renderer.h"
//================================

//= NAMESPACES ================
using namespace std;
using namespace Spartan::Math;
//=============================

namespace Spartan
{
    static const bool m_soft_body_support = true;

    Physics::Physics(Context* context) : ISubsystem(context)
    {
        m_broadphase        = new btDbvtBroadphase();
        m_constraint_solver = new btSequentialImpulseConstraintSolver();

        if (m_soft_body_support)
        {
            // Create
            m_collision_configuration  = new btSoftBodyRigidBodyCollisionConfiguration();
            m_collision_dispatcher     = new btCollisionDispatcher(m_collision_configuration);
            m_world                    = new btSoftRigidDynamicsWorld(m_collision_dispatcher, m_broadphase, m_constraint_solver, m_collision_configuration);

            // Setup         
            m_world_info = new btSoftBodyWorldInfo();
            m_world_info->m_sparsesdf.Initialize();
            m_world->getDispatchInfo().m_enableSPU  = true;
            m_world_info->m_dispatcher              = m_collision_dispatcher;
            m_world_info->m_broadphase              = m_broadphase;
            m_world_info->air_density               = (btScalar)1.2;
            m_world_info->water_density             = 0;
            m_world_info->water_offset              = 0;
            m_world_info->water_normal              = btVector3(0, 0, 0);
            m_world_info->m_gravity                 = ToBtVector3(m_gravity);

        }
        else
        {
            // Create
            m_collision_configuration   = new btDefaultCollisionConfiguration();
            m_collision_dispatcher      = new btCollisionDispatcher(m_collision_configuration);
            m_world                     = new btDiscreteDynamicsWorld(m_collision_dispatcher, m_broadphase, m_constraint_solver, m_collision_configuration);
        }

        // Setup
        m_world->setGravity(ToBtVector3(m_gravity));
        m_world->getDispatchInfo().m_useContinuous  = true;
        m_world->getSolverInfo().m_splitImpulse     = false;
        m_world->getSolverInfo().m_numIterations    = m_max_solve_iterations;
    }

    Physics::~Physics()
    {
        sp_ptr_delete(m_world);
        sp_ptr_delete(m_constraint_solver);
        sp_ptr_delete(m_collision_dispatcher);
        sp_ptr_delete(m_collision_configuration);
        sp_ptr_delete(m_broadphase);
        sp_ptr_delete(m_world_info);
        sp_ptr_delete(m_debug_draw);
    }

    bool Physics::Initialize()
    {
        // Get dependencies
        m_renderer = m_context->GetSubsystem<Renderer>();
        m_profiler = m_context->GetSubsystem<Profiler>();

        // Get version
        const auto major = to_string(btGetVersion() / 100);
        const auto minor = to_string(btGetVersion()).erase(0, 1);
        m_context->GetSubsystem<Settings>()->RegisterThirdPartyLib("Bullet", major + "." + minor, "https://github.com/bulletphysics/bullet3");

        // Enabled debug drawing
        {
            m_debug_draw = new PhysicsDebugDraw(m_renderer);

            if (m_world)
            {
                m_world->setDebugDrawer(m_debug_draw);
            }
        }

        return true;
    }

    void Physics::Tick(float delta_time_sec)
    {
        if (!m_world)
            return;
        
        // Debug draw
        if (m_renderer->GetOptions() & Render_Debug_Physics)
        {
            m_world->debugDrawWorld();
        }

        // Don't simulate physics if they are turned off or the we are in editor mode
        if (!m_context->m_engine->EngineMode_IsSet(Engine_Physics) || !m_context->m_engine->EngineMode_IsSet(Engine_Game))
            return;

        SCOPED_TIME_BLOCK(m_profiler);

        // This equation must be met: timeStep < maxSubSteps * fixedTimeStep
        auto internal_time_step    = 1.0f / m_internal_fps;
        auto max_substeps        = static_cast<int>(delta_time_sec * m_internal_fps) + 1;
        if (m_max_sub_steps < 0)
        {
            internal_time_step    = delta_time_sec;
            max_substeps        = 1;
        }
        else if (m_max_sub_steps > 0)
        {
            max_substeps = Helper::Min(max_substeps, m_max_sub_steps);
        }

        // Step the physics world. 
        m_simulating = true;
        m_world->stepSimulation(delta_time_sec, max_substeps, internal_time_step);
        m_simulating = false;
    }

    void Physics::AddBody(btRigidBody* body) const
    {
        if (!m_world)
            return;

        m_world->addRigidBody(body);
    }

    void Physics::RemoveBody(btRigidBody*& body) const
    {
        if (!m_world)
            return;

        m_world->removeRigidBody(body);
        delete body->getMotionState();
        sp_ptr_delete(body);
    }

    void Physics::AddConstraint(btTypedConstraint* constraint, bool collision_with_linked_body /*= true*/) const
    {
        if (!m_world)
            return;

        m_world->addConstraint(constraint, !collision_with_linked_body);
    }

    void Physics::RemoveConstraint(btTypedConstraint*& constraint) const
    {
        if (!m_world)
            return;

        m_world->removeConstraint(constraint);
        sp_ptr_delete(constraint);
    }

    void Physics::AddBody(btSoftBody* body) const
    {
        if (!m_world)
            return;

        if (btSoftRigidDynamicsWorld* world = static_cast<btSoftRigidDynamicsWorld*>(m_world))
        {
            world->addSoftBody(body);
        }
    }

    void Physics::RemoveBody(btSoftBody*& body) const
    {
        if (btSoftRigidDynamicsWorld* world = static_cast<btSoftRigidDynamicsWorld*>(m_world))
        {
            world->removeSoftBody(body);
            sp_ptr_delete(body);
        }
    }

    Vector3 Physics::GetGravity() const
    {
        auto gravity = m_world->getGravity();
        if (!gravity)
        {
            LOG_ERROR("Unable to get gravity, ensure physics are properly initialized.");
            return Vector3::Zero;
        }
        return gravity ? ToVector3(gravity) : Vector3::Zero;
    }
}
