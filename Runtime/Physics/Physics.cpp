/*
Copyright(c) 2016-2019 Panos Karabelas

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
#include "Physics.h"
#include "PhysicsDebugDraw.h"
#include "BulletPhysicsHelper.h"
#include "../Core/Engine.h"
#include "../Core/Context.h"
#include "../Core/Settings.h"
#include "../Profiling/Profiler.h"
#include "../Rendering/Renderer.h"
#pragma warning(push, 0) // Hide warnings which belong to Bullet
#include <BulletCollision/BroadphaseCollision/btDbvtBroadphase.h>
#include <BulletCollision/CollisionDispatch/btDefaultCollisionConfiguration.h>
#include <BulletDynamics/ConstraintSolver/btSequentialImpulseConstraintSolver.h>
#include <BulletDynamics/Dynamics/btDiscreteDynamicsWorld.h>
#pragma warning(pop)
//==============================================================================

//= NAMESPACES ================
using namespace std;
using namespace Spartan::Math;
//=============================

namespace Spartan
{ 
	Physics::Physics(Context* context) : ISubsystem(context)
	{
		// Create physics objects
		m_broadphase				= new btDbvtBroadphase();
		m_collision_configuration	= new btDefaultCollisionConfiguration();
		m_dispatcher				= new btCollisionDispatcher(m_collision_configuration);
		m_constraint_solver			= new btSequentialImpulseConstraintSolver();
		m_world						= new btDiscreteDynamicsWorld(m_dispatcher, m_broadphase, m_constraint_solver, m_collision_configuration);

		// Setup world
		m_world->setGravity(ToBtVector3(m_gravity));
		m_world->getDispatchInfo().m_useContinuous	= true;
		m_world->getSolverInfo().m_splitImpulse		= false;
		m_world->getSolverInfo().m_numIterations	= m_max_solve_iterations;
	}

	Physics::~Physics()
	{
		safe_delete(m_world);
		safe_delete(m_constraint_solver);
		safe_delete(m_dispatcher);
		safe_delete(m_collision_configuration);
		safe_delete(m_broadphase);
		safe_delete(m_debug_draw);
	}

	bool Physics::Initialize()
	{
        // Get dependencies
		m_renderer = m_context->GetSubsystem<Renderer>().get();
		m_profiler = m_context->GetSubsystem<Profiler>().get();

        // Get version
        const auto major = to_string(btGetVersion() / 100);
        const auto minor = to_string(btGetVersion()).erase(0, 1);
        m_context->GetSubsystem<Settings>()->m_versionBullet = major + "." + minor;

		// Enabled debug drawing
		m_debug_draw = new PhysicsDebugDraw(m_renderer);
		m_world->setDebugDrawer(m_debug_draw);

		return true;
	}

	void Physics::Tick(float delta_time_sec)
	{
		if (!m_world)
			return;
		
		// Debug draw
		if (m_renderer->GetFlags() & Render_Debug_Physics)
		{
			m_world->debugDrawWorld();
		}

		// Don't simulate physics if they are turned off or the we are in editor mode
		if (!m_context->m_engine->EngineMode_IsSet(Engine_Physics) || !m_context->m_engine->EngineMode_IsSet(Engine_Game))
			return;

		TIME_BLOCK_START_CPU(m_profiler);

		// This equation must be met: timeStep < maxSubSteps * fixedTimeStep
		auto internal_time_step	= 1.0f / m_internal_fps;
		auto max_substeps		= static_cast<int>(delta_time_sec * m_internal_fps) + 1;
		if (m_max_sub_steps < 0)
		{
			internal_time_step	= delta_time_sec;
			max_substeps		= 1;
		}
		else if (m_max_sub_steps > 0)
		{
			max_substeps = Min(max_substeps, m_max_sub_steps);
		}

		// Step the physics world. 
		m_simulating = true;
		m_world->stepSimulation(delta_time_sec, max_substeps, internal_time_step);
		m_simulating = false;

		TIME_BLOCK_END(m_profiler);
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
