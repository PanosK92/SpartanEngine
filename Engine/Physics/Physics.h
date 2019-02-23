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

#pragma once

//= INCLUDES =================
#include "../Core/ISubsystem.h"
//============================

class btBroadphaseInterface;
class btCollisionDispatcher;
class btConstraintSolver;
class btDefaultCollisionConfiguration;
class btDiscreteDynamicsWorld;

namespace Directus
{
	class Renderer;
	class Variant;
	class PhysicsDebugDraw;
	class Profiler;

	namespace Math
	{
		class Vector3;
	}	

	class Physics : public ISubsystem
	{
	public:
		Physics(Context* context);
		~Physics();

		//= Subsystem =============
		bool Initialize() override;
		void Tick() override;
		//=========================
	
		Math::Vector3 GetGravity();
		btDiscreteDynamicsWorld* GetWorld()		{ return m_world; }
		PhysicsDebugDraw* GetPhysicsDebugDraw() { return m_debugDraw; }
		bool IsSimulating()						{ return m_simulating; }

	private:
		btBroadphaseInterface* m_broadphase;
		btCollisionDispatcher* m_dispatcher;
		btConstraintSolver* m_constraintSolver;
		btDefaultCollisionConfiguration* m_collisionConfiguration;
		btDiscreteDynamicsWorld* m_world;
		PhysicsDebugDraw* m_debugDraw;

		//= PROPERTIES ===
		int m_maxSubSteps;
		bool m_simulating;
		//================

		Renderer* m_renderer;
		Profiler* m_profiler;
	};
}