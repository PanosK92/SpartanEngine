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

#pragma once

//= INCLUDES =================
#include "../Math/Vector3.h"
#include "../Core/Subsystem.h"
//============================

class btVector3;
class btBroadphaseInterface;
class btRigidBody;
class btTypedConstraint;
class btCollisionDispatcher;
class btConstraintSolver;
class btDefaultCollisionConfiguration;
class btDiscreteDynamicsWorld;

namespace Directus
{
	class PhysicsDebugDraw;

	class Physics : public Subsystem
	{
	public:
		Physics(Context* context);
		~Physics();

		//= Subsystem ============
		virtual bool Initialize();
		//========================

		void Step();
		void Clear();
		void DebugDraw();

		btDiscreteDynamicsWorld* GetWorld();
		PhysicsDebugDraw* GetPhysicsDebugDraw();
		bool IsSimulating() { return m_simulating; }

	private:
		btBroadphaseInterface* m_broadphase;
		btCollisionDispatcher* m_dispatcher;
		btConstraintSolver* m_constraintSolver;
		btDefaultCollisionConfiguration* m_collisionConfiguration;
		btDiscreteDynamicsWorld* m_world;
		PhysicsDebugDraw* m_debugDraw;

		//= PROPERTIES ====================
		float m_internalFPS;
		int m_maxSubSteps;
		Math::Vector3 m_gravity;
		bool m_simulating;
		//=================================
	};
}