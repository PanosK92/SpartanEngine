/*
Copyright(c) 2016-2017 Panos Karabelas

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
#include "BulletCollision/BroadphaseCollision/btDbvtBroadphase.h"
#include "BulletCollision/CollisionDispatch/btDefaultCollisionConfiguration.h"
#include "BulletCollision/CollisionDispatch/btCollisionDispatcher.h"
#include "BulletDynamics/ConstraintSolver/btTypedConstraint.h"
#include "BulletDynamics/ConstraintSolver/btSequentialImpulseConstraintSolver.h"
#include "BulletDynamics/Dynamics/btDiscreteDynamicsWorld.h"
#include "PhysicsDebugDraw.h"
#include "../Core/Helper.h"
#include "BulletPhysicsHelper.h"
#include "../EventSystem/EventSystem.h"
#include <algorithm>
#include "../Core/Context.h"
#include "../Core/Timer.h"
#include "../Core/Engine.h"
#include "../Logging/Log.h"
//==============================================================================

//= NAMESPACES =====
using namespace std;
//==================

namespace Directus
{ 
	Physics::Physics(Context* context) : Subsystem(context)
	{
		m_internalFPS = 25.0f;
		m_maxSubSteps = 1;
		m_gravity = Math::Vector3(0.0f, -9.81f, 0.0f);
		m_simulating = false;

		// Subscribe to update event
		SUBSCRIBE_TO_EVENT(EVENT_UPDATE, EVENT_HANDLER(Step));
		SUBSCRIBE_TO_EVENT(EVENT_CLEAR_SUBSYSTEMS, EVENT_HANDLER(Clear));
	}

	Physics::~Physics()
	{
		delete m_world;
		delete m_constraintSolver;
		delete m_broadphase;
		delete m_dispatcher;
		delete m_collisionConfiguration;
		SafeRelease(m_debugDraw);
	}

	bool Physics::Initialize()
	{
		m_broadphase = new btDbvtBroadphase();
		m_collisionConfiguration = new btDefaultCollisionConfiguration();
		m_dispatcher = new btCollisionDispatcher(m_collisionConfiguration);
		m_constraintSolver = new btSequentialImpulseConstraintSolver;
		m_world = new btDiscreteDynamicsWorld(m_dispatcher, m_broadphase, m_constraintSolver, m_collisionConfiguration);

		// create an implementation of the btIDebugDraw interface
		m_debugDraw = new PhysicsDebugDraw();
		int debugMode = btIDebugDraw::DBG_MAX_DEBUG_DRAW_MODE;
		m_debugDraw->setDebugMode(debugMode);

		m_world->setGravity(ToBtVector3(m_gravity));
		m_world->getDispatchInfo().m_useContinuous = true;
		m_world->getSolverInfo().m_splitImpulse = false;
		m_world->setDebugDrawer(m_debugDraw);

		// Log version
		string major = to_string(btGetVersion() / 100);
		string minor = to_string(btGetVersion()).erase(0, 1);
		LOG_INFO("Physics: Bullet " + major + "." + minor);

		return true;
	}

	void Physics::Step()
	{
		if (!m_world)
			return;

		// Don't simulate physics if we are in editor mode
		if (m_context->GetSubsystem<Engine>()->GetMode() == Editor)
			return;

		// timeStep < maxSubSteps * fixedTimeStep

		float timeStep = m_context->GetSubsystem<Timer>()->GetDeltaTimeSec();
		float fixedTimeStep = 1.0f / m_internalFPS;

		m_simulating = true;

		// Step the physics world. 
		m_world->stepSimulation(timeStep, m_maxSubSteps, fixedTimeStep);

		m_simulating = false;
	}

	void Physics::Clear()
	{
		if (!m_world)
			return;

		// delete constraints
		for (int i = m_world->getNumConstraints() - 1; i >= 0; i--)
		{
			btTypedConstraint* constraint = m_world->getConstraint(i);
			m_world->removeConstraint(constraint);
			delete constraint;
		}

		// remove the rigidbodies from the dynamics world and delete them
		for (int i = m_world->getNumCollisionObjects() - 1; i >= 0; i--)
		{
			btCollisionObject* obj = m_world->getCollisionObjectArray()[i];
			btRigidBody* body = btRigidBody::upcast(obj);
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

	btDiscreteDynamicsWorld* Physics::GetWorld()
	{
		return m_world;
	}

	PhysicsDebugDraw* Physics::GetPhysicsDebugDraw()
	{
		return m_debugDraw;
	}
}