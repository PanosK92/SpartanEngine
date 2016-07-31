/*
Copyright(c) 2016 Panos Karabelas

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
#include "PhysicsWorld.h"
#include <BulletCollision/BroadphaseCollision/btDbvtBroadphase.h>
#include <BulletCollision/CollisionDispatch/btDefaultCollisionConfiguration.h>
#include <BulletCollision/CollisionDispatch/btCollisionDispatcher.h>
#include <BulletDynamics/ConstraintSolver/btTypedConstraint.h>
#include <BulletDynamics/ConstraintSolver/btSequentialImpulseConstraintSolver.h>
#include <BulletDynamics/Dynamics/btDiscreteDynamicsWorld.h>
#include "PhysicsDebugDraw.h"
#include "../Core/Globals.h"
#include "BulletPhysicsHelper.h"
//==============================================================================

PhysicsWorld::PhysicsWorld()
{
	m_broadphase = nullptr;
	m_dispatcher = nullptr;
	m_constraintSolver = nullptr;
	m_collisionConfiguration = nullptr;
	m_world = nullptr;
	m_debugDraw = nullptr;
	m_debugDrawEnabled = false;
	m_gravity = Directus::Math::Vector3(0.0f, -9.81f, 0.0f);
	m_timer = nullptr;
}

PhysicsWorld::~PhysicsWorld()
{
	SafeDelete(m_world);
	SafeDelete(m_constraintSolver);
	SafeDelete(m_broadphase);
	SafeDelete(m_dispatcher);
	SafeDelete(m_collisionConfiguration);
	SafeRelease(m_debugDraw);
}

void PhysicsWorld::Initialize(Timer* timer)
{
	m_timer = timer;

	m_broadphase = new btDbvtBroadphase();
	m_collisionConfiguration = new btDefaultCollisionConfiguration();
	m_dispatcher = new btCollisionDispatcher(m_collisionConfiguration);
	m_constraintSolver = new btSequentialImpulseConstraintSolver;
	m_world = new btDiscreteDynamicsWorld(m_dispatcher, m_broadphase, m_constraintSolver, m_collisionConfiguration);
	
	// create an implementation of the btIDebugDraw interface
	m_debugDraw = new PhysicsDebugDraw();
	int debugMode = btIDebugDraw::DBG_DrawWireframe | btIDebugDraw::DBG_DrawConstraintLimits | btIDebugDraw::DBG_DrawConstraints;
	m_debugDraw->setDebugMode(debugMode);

	m_world->setGravity(ToBtVector3(m_gravity));
	m_world->getDispatchInfo().m_useContinuous = true;
	m_world->getSolverInfo().m_splitImpulse = false;
	m_world->setDebugDrawer(m_debugDraw);
}

void PhysicsWorld::Update()
{
	if (!m_world || !m_timer)
		return;

	// Step the physics world
	m_world->stepSimulation(m_timer->GetDeltaTime());

	if (m_debugDrawEnabled)
		DebugDraw();
}

void PhysicsWorld::Reset()
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

btDiscreteDynamicsWorld* PhysicsWorld::GetWorld()
{
	return m_world;
}

//= DEBUG DRAW ==================================================================
void PhysicsWorld::SetDebugDraw(bool enable)
{
	m_debugDrawEnabled = enable;
}

bool PhysicsWorld::GetDebugDraw()
{
	return m_debugDrawEnabled;
}

void PhysicsWorld::DebugDraw()
{
	if (!m_world)
		return;

	m_world->debugDrawWorld();
}

PhysicsDebugDraw* PhysicsWorld::GetPhysicsDebugDraw()
{
	return m_debugDraw;
}
