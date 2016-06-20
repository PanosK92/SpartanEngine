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
#include "PhysicsEngine.h"
#include <BulletCollision/BroadphaseCollision/btDbvtBroadphase.h>
#include <BulletCollision/CollisionDispatch/btDefaultCollisionConfiguration.h>
#include <BulletCollision/CollisionDispatch/btCollisionDispatcher.h>
#include <BulletDynamics/ConstraintSolver/btTypedConstraint.h>
#include <BulletDynamics/ConstraintSolver/btSequentialImpulseConstraintSolver.h>
#include <BulletDynamics/Dynamics/btDiscreteDynamicsWorld.h>
#include "PhysicsDebugDraw.h"
#include "../Core/Globals.h"
//==============================================================================

PhysicsEngine::PhysicsEngine()
{
	m_broadphase = nullptr;
	m_dispatcher = nullptr;
	m_constraintSolver = nullptr;
	m_collisionConfiguration = nullptr;
	m_dynamicsWorld = nullptr;
	m_physicsDebugDraw = nullptr;

	m_debugDrawEnabled = false;
	gravity = -9.81f;
}

PhysicsEngine::~PhysicsEngine()
{
	DirectusSafeDelete(m_dynamicsWorld);
	DirectusSafeDelete(m_constraintSolver);
	DirectusSafeDelete(m_broadphase);
	DirectusSafeDelete(m_dispatcher);
	DirectusSafeDelete(m_collisionConfiguration);
	DirectusSafeRelease(m_physicsDebugDraw);
}

void PhysicsEngine::Initialize()
{
	// build the broadphase
	m_broadphase = new btDbvtBroadphase();

	// set up the collision configuration and dispatcher
	m_collisionConfiguration = new btDefaultCollisionConfiguration();
	m_dispatcher = new btCollisionDispatcher(m_collisionConfiguration);

	// the actual physics solver
	m_constraintSolver = new btSequentialImpulseConstraintSolver;

	// the world
	m_dynamicsWorld = new btDiscreteDynamicsWorld(m_dispatcher, m_broadphase, m_constraintSolver, m_collisionConfiguration);
	m_dynamicsWorld->setGravity(btVector3(0, gravity, 0));

	// create an implementation of the btIDebugDraw interface
	m_physicsDebugDraw = new PhysicsDebugDraw();

	// set the debug draw mode
	int debugMode = btIDebugDraw::DBG_DrawWireframe | btIDebugDraw::DBG_DrawConstraintLimits | btIDebugDraw::DBG_DrawConstraints;
	m_physicsDebugDraw->setDebugMode(debugMode);

	// pass the btIDebugDraw interface implementation to Bullet
	m_dynamicsWorld->setDebugDrawer(m_physicsDebugDraw);
}

void PhysicsEngine::Update()
{
	if (!m_dynamicsWorld)
		return;

	// step physics world
	m_dynamicsWorld->stepSimulation(1 / 60.f);

	if (m_debugDrawEnabled)
		DebugDraw();
}

void PhysicsEngine::Reset()
{
	// delete constraints
	for (int i = m_dynamicsWorld->getNumConstraints() - 1; i >= 0; i--)
	{
		btTypedConstraint* constraint = m_dynamicsWorld->getConstraint(i);
		m_dynamicsWorld->removeConstraint(constraint);
		delete constraint;
	}

	// remove the rigidbodies from the dynamics world and delete them
	for (int i = m_dynamicsWorld->getNumCollisionObjects() - 1; i >= 0; i--)
	{
		btCollisionObject* obj = m_dynamicsWorld->getCollisionObjectArray()[i];
		btRigidBody* body = btRigidBody::upcast(obj);
		if (body && body->getMotionState())
		{
			delete body->getMotionState();
		}
		m_dynamicsWorld->removeCollisionObject(obj);
		delete obj;
	}
}

//= RIGIDBODY ==================================================================
void PhysicsEngine::AddRigidBody(btRigidBody* rigidBody)
{
	m_dynamicsWorld->addRigidBody(rigidBody);
}

void PhysicsEngine::RemoveRigidBody(btRigidBody* rigidBody)
{
	m_dynamicsWorld->removeRigidBody(rigidBody);
}

//= CONSTRAINT =================================================================
void PhysicsEngine::AddConstraint(btTypedConstraint* constraint)
{
	m_dynamicsWorld->addConstraint(constraint);
}

void PhysicsEngine::RemoveConstraint(btTypedConstraint* constraint)
{
	m_dynamicsWorld->removeConstraint(constraint);
}

//= DEBUG DRAW ==================================================================
void PhysicsEngine::SetDebugDraw(bool enable)
{
	m_debugDrawEnabled = enable;
}

bool PhysicsEngine::GetDebugDraw()
{
	return m_debugDrawEnabled;
}

void PhysicsEngine::DebugDraw()
{
	m_dynamicsWorld->debugDrawWorld();
}

PhysicsDebugDraw* PhysicsEngine::GetPhysicsDebugDraw()
{
	return m_physicsDebugDraw;
}
