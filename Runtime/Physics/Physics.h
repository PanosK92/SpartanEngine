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

namespace Spartan
{
	class Renderer;
	class PhysicsDebugDraw;
	class Profiler;
	namespace Math { class Vector3; }	

	class Physics : public ISubsystem
	{
	public:
		Physics(Context* context);
		~Physics();

		//= Subsystem =======================
		bool Initialize() override;
		void Tick(float delta_time) override;
		//====================================
	
		Math::Vector3 GetGravity() const;
		btDiscreteDynamicsWorld* GetWorld() const		{ return m_world; }
		PhysicsDebugDraw* GetPhysicsDebugDraw() const	{ return m_debug_draw; }
		bool IsSimulating() const						{ return m_simulating; }

	private:
		btBroadphaseInterface* m_broadphase;
		btCollisionDispatcher* m_dispatcher;
		btConstraintSolver* m_constraint_solver;
		btDefaultCollisionConfiguration* m_collision_configuration;
		btDiscreteDynamicsWorld* m_world;
		PhysicsDebugDraw* m_debug_draw;

		//= PROPERTIES =====
		int m_max_sub_steps;
		bool m_simulating;
		//==================

		Renderer* m_renderer;
		Profiler* m_profiler;
	};
}
