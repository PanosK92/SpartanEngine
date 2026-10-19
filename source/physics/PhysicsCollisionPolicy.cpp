/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

#include "pch.h"
#include "GameWorld.h"
#include "../physics/PhysicsWorld.h"

namespace spartan::game
{
    void InitializeCollisionPolicy()
    {
        PhysicsWorld::SetCollisionFilter([](uint32_t a, uint32_t group_a, uint32_t b, uint32_t group_b)
        {
            PhysicsCollisionResponse result;
            const bool vehicle = a == physics_collision_vehicle || b == physics_collision_vehicle;
            result.suppress =
                (a == physics_collision_character && b == physics_collision_vehicle) ||
                (b == physics_collision_character && a == physics_collision_vehicle) ||
                (a == physics_collision_vehicle && b == physics_collision_vehicle && group_a != 0 && group_a == group_b) ||
                (a == physics_collision_pedestrian && b == physics_collision_pedestrian);
            result.report_contacts = vehicle || a == physics_collision_pedestrian || b == physics_collision_pedestrian ||
                a == physics_collision_ragdoll || b == physics_collision_ragdoll;
            result.report_velocity = vehicle;
            return result;
        });
    }
}
