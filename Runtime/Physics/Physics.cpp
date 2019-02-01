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
#include "../Core/Engine.h"
#include "../Core/EventSystem.h"
#include "../Core/Settings.h"
#include "../Profiling/Profiler.h"
#include "PhysicsDebugDraw.h"
#include "BulletPhysicsHelper.h"
#include "../Rendering/Renderer.h"
#pragma warning(push, 0) // Hide warnings which belong to Bullet
#include <BulletCollision/BroadphaseCollision/btDbvtBroadphase.h>
#include <BulletCollision/CollisionDispatch/btDefaultCollisionConfiguration.h>
#include <BulletDynamics/ConstraintSolver/btSequentialImpulseConstraintSolver.h>
#include <BulletDynamics/Dynamics/btDiscreteDynamicsWorld.h>
#include <BulletDynamics/ConstraintSolver/btConstraintSolver.h>
#pragma warning(pop)
//==============================================================================

//= NAMESPACES ================
using namespace std;
using namespace Directus::Math;
//=============================

static const int MAX_SOLVER_ITERATIONS	= 256;
static const float INTERNAL_FPS			= 60.0f;
static const Vector3 GRAVITY			= Vector3(0.0f, -9.81f, 0.0f);

namespace Directus
{ 
	Physics::Physics(Context* context) : Subsystem(context)
	{
		m_maxSubSteps	= 1;
		m_simulating	= false;
		
		// Create physics objects
		m_broadphase				= new btDbvtBroadphase();
		m_collisionConfiguration	= new btDefaultCollisionConfiguration();
		m_dispatcher				= new btCollisionDispatcher(m_collisionConfiguration);
		m_constraintSolver			= new btSequentialImpulseConstraintSolver();
		m_debugDraw					= new PhysicsDebugDraw(m_context->GetSubsystem<Renderer>());
		m_world						= new btDiscreteDynamicsWorld(m_dispatcher, m_broadphase, m_constraintSolver, m_collisionConfiguration);

		// Setup world
		m_world->setGravity(ToBtVector3(GRAVITY));
		m_world->getDispatchInfo().m_useContinuous = true;
		m_world->getSolverInfo().m_splitImpulse = false;
		m_world->getSolverInfo().m_numIterations = MAX_SOLVER_ITERATIONS;
		m_world->setDebugDrawer(m_debugDraw);

		// Get version
		string major = to_string(btGetVersion() / 100);
		string minor = to_string(btGetVersion()).erase(0, 1);
		Settings::Get().m_versionBullet = major + "." + minor;

		// Subscribe to events
		SUBSCRIBE_TO_EVENT(Event_Tick, EVENT_HANDLER_VARIANT(Step));
	}

	Physics::~Physics()
	{
		SafeDelete(m_world);
		SafeDelete(m_constraintSolver);
		SafeDelete(m_dispatcher);
		SafeDelete(m_collisionConfiguration);
		SafeDelete(m_broadphase);
		SafeDelete(m_debugDraw);
	}

	bool Physics::Initialize()
	{
		m_renderer = m_context->GetSubsystem<Renderer>();
		return true;
	}

	void Physics::Step(const Variant& deltaTime)
	{
		if (!m_world)
			return;
		
		// Debug draw
		if (m_renderer->Flags_IsSet(Render_Gizmo_Physics))
		{
			m_world->debugDrawWorld();
		}

		// Don't simulate physics if they are turned off or the we are in editor mode
		if (!Engine::EngineMode_IsSet(Engine_Physics) || !Engine::EngineMode_IsSet(Engine_Game))
			return;

		TIME_BLOCK_START_CPU();

		float timeStep = deltaTime.Get<float>();

		// This equation must be met: timeStep < maxSubSteps * fixedTimeStep
		float internalTimeStep = 1.0f / INTERNAL_FPS;
		int maxSubsteps = (int)(timeStep * INTERNAL_FPS) + 1;
		if (m_maxSubSteps < 0)
		{
			internalTimeStep = timeStep;
			maxSubsteps = 1;
		}
		else if (m_maxSubSteps > 0)
		{
			maxSubsteps = Min(maxSubsteps, m_maxSubSteps);
		}

		m_simulating = true;

		// Step the physics world. 
		m_world->stepSimulation(timeStep, maxSubsteps, internalTimeStep);

		m_simulating = false;

		TIME_BLOCK_END_CPU();
	}

	Vector3 Physics::GetGravity()
	{
		return ToVector3(m_world->getGravity());
	}
}
