/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

#pragma once

//= INCLUDES ===============
#include <mutex>
#include <vector>
#include <functional>
#include "../math/Vector3.h"
//==========================

namespace physx
{
    class PxRigidActor;
    struct PxContactPairHeader;
    struct PxContactPair;
}

namespace spartan
{
    class Entity;

    // word2 tags used by the simulation filter shader
    constexpr uint32_t physics_collision_character  = 1;
    constexpr uint32_t physics_collision_vehicle    = 2;
    constexpr uint32_t physics_collision_pedestrian = 3;
    constexpr uint32_t physics_collision_ragdoll    = 4;

    // Application policy; filter tags/groups are opaque to the physics backend.
    struct PhysicsCollisionResponse
    {
        bool suppress = false;
        bool report_contacts = false;
        bool report_velocity = false;
    };

    struct PhysicsRaycastHit
    {
        math::Vector3 position = math::Vector3::Zero;
        math::Vector3 normal = math::Vector3::Up;
        Entity* entity = nullptr;
        float distance = 0.0f;
    };

    struct PhysicsContact
    {
        Entity* entity_a = nullptr;
        Entity* entity_b = nullptr;
        math::Vector3 position = math::Vector3::Zero;
        math::Vector3 normal = math::Vector3::Up;
        math::Vector3 impulse = math::Vector3::Zero;
        math::Vector3 relative_velocity = math::Vector3::Zero; // actor A minus B at the contact, before solving
        uint32_t tracked_actor_mask = 0; // bit 0: A, bit 1: B; selected by the contact observer
        math::Vector3 actor_local_position[2] = {}; // contact-time actor space, before later substeps move the car
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

        // Invoked on the physics worker during contact reporting. Install before simulation;
        // return bits 0/1 to retain actor A/B contacts even when the other actor has no entity.
        using ContactObserver = uint32_t (*)(const physx::PxContactPairHeader&, const physx::PxContactPair*, uint32_t);
        static void SetContactObserver(ContactObserver observer);
        using CollisionFilter = PhysicsCollisionResponse (*)(uint32_t tag_a, uint32_t group_a, uint32_t tag_b, uint32_t group_b);
        // Install before creating actors; unchanged for the lifetime of a simulation.
        static void SetCollisionFilter(CollisionFilter filter);


        // force model hooks, invoked once per fixed simulation step before scene simulation
        static void RegisterStepCallback(const void* owner, const std::function<void(float)>& callback);
        static void UnregisterStepCallback(const void* owner);

        // contacts from the last physics ticks, valid until the next physics tick
        static const std::vector<PhysicsContact>& GetFrameContacts();
        static std::vector<PhysicsContact> ConsumeContacts();

        // cast a ray against static geometry and return the closest hit position
        static bool RaycastStatic(const math::Vector3& origin, const math::Vector3& direction, float max_distance, math::Vector3& hit_position);

        // cast a ray against static geometry and return the closest hit position + the entity that was hit
        static bool RaycastStatic(const math::Vector3& origin, const math::Vector3& direction, float max_distance, math::Vector3& hit_position, Entity*& hit_entity);
        static bool RaycastStatic(const math::Vector3& origin, const math::Vector3& direction, float max_distance, PhysicsRaycastHit& hit, Entity* ignored_entity = nullptr);

        static bool SphereCast(const math::Vector3& origin, const math::Vector3& direction, float radius, float max_distance, uint32_t ignored_collision_group, math::Vector3& hit_position, float& hit_distance, Entity*& hit_entity);

        // serializes every PxScene, PxRigidActor and PxShape access, recursive so nested helpers can re-enter
        static std::recursive_mutex& GetMutex();
        static math::Vector3 GetOrigin();
        static math::Vector3 ToPhysicsPosition(const math::Vector3& world_position);
        static math::Vector3 ToWorldPosition(const math::Vector3& physics_position);
        static void RebaseOrigin(const math::Vector3& world_focus);
    };
}
