/*
Copyright(c) 2015-2026 Panos Karabelas

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

//= INCLUDES ===============
#include <mutex>
#include <vector>
#include <functional>
#include "../Math/Vector3.h"
//==========================

namespace physx
{
    class PxRigidActor;
}

namespace spartan
{
    class Entity;

    struct PhysicsRaycastHit
    {
        math::Vector3 position = math::Vector3::Zero;
        math::Vector3 normal = math::Vector3::Up;
        Entity* entity = nullptr;
        float distance = 0.0f;
    };

    class PhysicsWorld
    {
    public:
        static void Initialize();
        static void Shutdown();
        static void Tick();
        static void DrawDebugVisualization();

        static void AddActor(physx::PxRigidActor* actor);
        static void RemoveActor(physx::PxRigidActor* actor);

        static math::Vector3 GetGravity();
        static void* GetScene();
        static void* GetPhysics();
        
        // interpolation alpha for smooth rendering between fixed physics steps
        // 0 = at previous physics state, 1 = at current physics state
        static float GetInterpolationAlpha();
        static float GetFixedTimeStep();

        // vehicle force model hooks, invoked once per fixed simulation step before scene simulation
        static void RegisterVehicleStepCallback(const void* owner, const std::function<void(float)>& callback);
        static void UnregisterVehicleStepCallback(const void* owner);

        // cast a ray against static geometry and return the closest hit position
        static bool RaycastStatic(const math::Vector3& origin, const math::Vector3& direction, float max_distance, math::Vector3& hit_position);

        // cast a ray against static geometry and return the closest hit position + the entity that was hit
        static bool RaycastStatic(const math::Vector3& origin, const math::Vector3& direction, float max_distance, math::Vector3& hit_position, Entity*& hit_entity);
        static bool RaycastStatic(const math::Vector3& origin, const math::Vector3& direction, float max_distance, PhysicsRaycastHit& hit, Entity* ignored_entity = nullptr);

        static bool SphereCast(const math::Vector3& origin, const math::Vector3& direction, float radius, float max_distance, uint32_t ignored_collision_group, math::Vector3& hit_position, float& hit_distance, Entity*& hit_entity);

        // global physx scene lock, used to serialize all reads and writes that touch
        // PxScene/PxRigidActor/PxShape state, async scene loading runs Physics::Create on
        // worker threads which races with the main thread, recursive so the same thread
        // can re-enter under nested helpers like PhysicsWorld::AddActor
        static std::recursive_mutex& GetMutex();
    };
}
