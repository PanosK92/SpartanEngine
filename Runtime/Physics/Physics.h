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

#pragma once

//= INCLUDES =================
#include "../Core/Subsystem.h"
#include <memory>
//============================

class btCollisionShape;
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
	class Variant;
	class RigidBody;
	class Collider;
	class Constraint;
	class PhysicsDebugDraw;
	namespace Math
	{
		class Vector3;
	}	

	class Physics : public Subsystem
	{
	public:
		Physics(Context* context);
		~Physics();

		//= Subsystem =============
		bool Initialize() override;
		//=========================

		// Step the world
		void Step(Variant deltaTime);
		// Remove everything from the world
		void Clear();
		// Draw debug geometry
		void DebugDraw();
		// Return the world
		std::shared_ptr<btDiscreteDynamicsWorld> GetWorld() { return m_world; }
		// Return the world's gravity
		Math::Vector3 GetGravity();

		PhysicsDebugDraw* GetPhysicsDebugDraw() { return m_debugDraw.get(); }
		bool IsSimulating() { return m_simulating; }

	private:
		std::unique_ptr<btBroadphaseInterface> m_broadphase;
		std::unique_ptr<btCollisionDispatcher> m_dispatcher;
		std::unique_ptr<btConstraintSolver> m_constraintSolver;
		std::unique_ptr<btDefaultCollisionConfiguration> m_collisionConfiguration;
		std::shared_ptr<btDiscreteDynamicsWorld> m_world;
		std::shared_ptr<PhysicsDebugDraw> m_debugDraw;

		//= PROPERTIES ===
		int m_maxSubSteps;
		bool m_simulating;
		//================
	};
}