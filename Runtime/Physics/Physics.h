/*
Copyright(c) 2016-2020 Panos Karabelas

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

//= INCLUDES ==================
#include "../Core/ISubsystem.h"
#include "../Math/Vector3.h"
//=============================

//= FORWARD DECLARATIONS =================
class btBroadphaseInterface;
class btCollisionDispatcher;
class btSequentialImpulseConstraintSolver;
class btDefaultCollisionConfiguration;
class btCollisionObject;
class btDiscreteDynamicsWorld;
class btRigidBody;
class btSoftBody;
class btTypedConstraint;
struct btSoftBodyWorldInfo;
//========================================

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
        //===================================

        // Rigid body
        void AddBody(btRigidBody* body) const;
        void RemoveBody(btRigidBody*& body) const;

        // Soft body
        void AddBody(btSoftBody* body) const;
        void RemoveBody(btSoftBody*& body) const;

        // Constraint
        void AddConstraint(btTypedConstraint* constraint, bool collision_with_linked_body = true) const;
        void RemoveConstraint(btTypedConstraint*& constraint) const;

        // Properties
        Math::Vector3 GetGravity()  const;
        auto& GetSoftWorldInfo()    const { return *m_world_info; }
        auto GetPhysicsDebugDraw()  const { return m_debug_draw; }
        bool IsSimulating()         const { return m_simulating; }

    private:
        btBroadphaseInterface* m_broadphase                         = nullptr;
        btCollisionDispatcher* m_collision_dispatcher               = nullptr;
        btSequentialImpulseConstraintSolver* m_constraint_solver    = nullptr;
        btDefaultCollisionConfiguration* m_collision_configuration  = nullptr;
        btDiscreteDynamicsWorld* m_world                            = nullptr;
        btSoftBodyWorldInfo* m_world_info                           = nullptr;
        PhysicsDebugDraw* m_debug_draw                              = nullptr;

        // Misc
        Renderer* m_renderer = nullptr;
        Profiler* m_profiler = nullptr;

        //= PROPERTIES =================================================
        int m_max_sub_steps         = 1;
        int m_max_solve_iterations  = 256;
        float m_internal_fps        = 60.0f;
        Math::Vector3 m_gravity     = Math::Vector3(0.0f, -9.81f, 0.0f);
        bool m_simulating           = false;
        //==============================================================
    };
}
