/*
Copyright(c) 2016-2018 Panos Karabelas

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
#include "../Core/Context.h"
#include "../Core/Engine.h"
#include "../Core/EngineDefs.h"
#include "../Logging/Log.h"
#include "../EventSystem/EventSystem.h"
#include "PhysicsDebugDraw.h"
#include "BulletPhysicsHelper.h"
#include "BulletCollision/BroadphaseCollision/btDbvtBroadphase.h"
#include "BulletCollision/CollisionDispatch/btDefaultCollisionConfiguration.h"
#include "BulletCollision/CollisionDispatch/btCollisionDispatcher.h"
#include "BulletDynamics/ConstraintSolver/btTypedConstraint.h"
#include "BulletDynamics/ConstraintSolver/btSequentialImpulseConstraintSolver.h"
#include "BulletDynamics/Dynamics/btDiscreteDynamicsWorld.h"
#include "../Core/Settings.h"
//==============================================================================

//= NAMESPACES ================
using namespace std;
using namespace Directus::Math;
//=============================

static const int MAX_SOLVER_ITERATIONS = 256;
static const float INTERNAL_FPS = 60.0f;
static const Vector3 GRAVITY = Vector3(0.0f, -9.81f, 0.0f);

namespace Directus
{ 
	Physics::Physics(Context* context) : Subsystem(context)
	{
		m_maxSubSteps = 1;
		m_simulating = false;

		// Subscribe to events
		SUBSCRIBE_TO_EVENT(EVENT_UPDATE,			EVENT_HANDLER_VARIANT(Step));
		SUBSCRIBE_TO_EVENT(EVENT_SCENE_CLEARED,		EVENT_HANDLER(Clear));
	}

	Physics::~Physics()
	{
		SafeRelease(m_debugDraw);
	}

	bool Physics::Initialize()
	{
		m_broadphase				= make_unique<btDbvtBroadphase>();
		m_collisionConfiguration	= make_unique<btDefaultCollisionConfiguration>();
		m_dispatcher				= make_unique<btCollisionDispatcher>(m_collisionConfiguration.get());
		m_constraintSolver			= make_unique<btSequentialImpulseConstraintSolver>();
		m_world						= make_shared<btDiscreteDynamicsWorld>(
									m_dispatcher.get(), 
									m_broadphase.get(), 
									m_constraintSolver.get(), 
									m_collisionConfiguration.get()
									);

		// create an implementation of the btIDebugDraw interface
		m_debugDraw = make_shared<PhysicsDebugDraw>();
		int debugMode = btIDebugDraw::DBG_MAX_DEBUG_DRAW_MODE;
		m_debugDraw->setDebugMode(debugMode);

		// Setup world
		m_world->setGravity(ToBtVector3(GRAVITY));
		m_world->getDispatchInfo().m_useContinuous = true;
		m_world->getSolverInfo().m_splitImpulse = false;
		m_world->getSolverInfo().m_numIterations = MAX_SOLVER_ITERATIONS;
		m_world->setDebugDrawer(m_debugDraw.get());

		// Log version
		string major = to_string(btGetVersion() / 100);
		string minor = to_string(btGetVersion()).erase(0, 1);
		Settings::Get().g_versionBullet = major + "." + minor;
		LOG_INFO("Physics: Bullet " + Settings::Get().g_versionBullet);

		return true;
	}

	void Physics::Step(const Variant& deltaTime)
	{
		if (!m_world)
			return;
		
		// Don't simulate physics if they are turned off
		if (!Engine::EngineMode_IsSet(Engine_Physics))
			return;

		// Don't simulate physics if they engine is not in game mode
		if (!Engine::EngineMode_IsSet(Engine_Game))
			return;

		float timeStep = deltaTime.GetFloat();

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
	}

	void Physics::Clear()
	{
		if (!m_world)
			return;

		// delete constraints
		for (int i = m_world->getNumConstraints() - 1; i >= 0; i--)
		{
			auto constraint = m_world->getConstraint(i);
			m_world->removeConstraint(constraint);
			delete constraint;
		}

		// remove the rigidbodies from the dynamics world and delete them
		for (int i = m_world->getNumCollisionObjects() - 1; i >= 0; i--)
		{
			auto obj = m_world->getCollisionObjectArray()[i];
			auto body = btRigidBody::upcast(obj);
			if (body && body->getMotionState())
			{
				delete body->getMotionState();
			}
			m_world->removeCollisionObject(obj);
			delete obj;
		}
	}

	void Physics::DebugDraw()
	{
		m_debugDraw->ClearLines();
		m_world->debugDrawWorld();
	}

	Vector3 Physics::GetGravity()
	{
		return ToVector3(m_world->getGravity());
	}
}
