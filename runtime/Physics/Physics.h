/*
Copyright(c) 2016-2025 Panos Karabelas

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

//= INCLUDES ===========
#include "Definitions.h"
//======================

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
class btRaycastVehicle;
//========================================

namespace Spartan
{
    class Physics
    {
    public:
        static void Initialize();
        static void Shutdown();
        static void Tick();

        static std::vector<btRigidBody*> RayCast(const Math::Vector3& start, const Math::Vector3& end);
        static Math::Vector3 RayCastFirstHitPosition(const Math::Vector3& start, const Math::Vector3& end);

        // body
        static void AddBody(btRigidBody* body);
        static void RemoveBody(btRigidBody*& body);
        static void AddBody(btSoftBody* body);
        static void RemoveBody(btSoftBody*& body);
        static void AddBody(btRaycastVehicle* body);
        static void RemoveBody(btRaycastVehicle*& body);

        // constraint
        static void AddConstraint(btTypedConstraint* constraint, bool collision_with_linked_body = true);
        static void RemoveConstraint(btTypedConstraint*& constraint);

        // misc
        static Math::Vector3& GetGravity();
        static btSoftBodyWorldInfo& GetSoftWorldInfo();
        static void* GetPhysicsDebugDraw();
        static void* GetWorld();
        static float GetTimeStepInternalSec();

    private:
        // picking
        static void PickBody();
        static void UnpickBody();
        static void MovePickedBody();
    };
}
