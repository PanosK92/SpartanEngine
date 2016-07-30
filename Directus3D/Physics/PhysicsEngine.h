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

#pragma once

// Forward declarations to avoid dependencies when used in editor
class PhysicsDebugDraw;
class btBroadphaseInterface;
class btRigidBody;
class btTypedConstraint;
class btCollisionDispatcher;
class btConstraintSolver;
class btDefaultCollisionConfiguration;
class btDiscreteDynamicsWorld;

class PhysicsEngine
{
public:
	PhysicsEngine();
	~PhysicsEngine();

	void Initialize();
	void Update();
	void Reset();

	btDiscreteDynamicsWorld* GetWorld();

	/*------------------------------------------------------------------------------
								[RIGIDBODY]
	------------------------------------------------------------------------------*/
	void AddRigidBody(btRigidBody* rigidBody);
	void RemoveRigidBody(btRigidBody* rigidBody);

	/*------------------------------------------------------------------------------
								[CONSTRAINT]
	------------------------------------------------------------------------------*/
	void AddConstraint(btTypedConstraint* constraint);
	void RemoveConstraint(btTypedConstraint* constraint);

	/*------------------------------------------------------------------------------
								[DEBUG DRAW]
	------------------------------------------------------------------------------*/
	void SetDebugDraw(bool enable);
	bool GetDebugDraw();
	void DebugDraw();
	PhysicsDebugDraw* GetPhysicsDebugDraw();

private:
	btBroadphaseInterface* m_broadphase;
	btCollisionDispatcher* m_dispatcher;
	btConstraintSolver* m_constraintSolver;
	btDefaultCollisionConfiguration* m_collisionConfiguration;
	btDiscreteDynamicsWorld* m_dynamicsWorld;

	bool m_debugDrawEnabled;
	PhysicsDebugDraw* m_physicsDebugDraw;

	float gravity;
};
