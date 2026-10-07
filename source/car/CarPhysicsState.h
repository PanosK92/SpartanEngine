/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

#pragma once
#include "CarPhysicsTypes.h"
#include "../math/Vector3.h"
#include "../math/Quaternion.h"
#include <memory>
#include <vector>
#include <cstdint>
namespace car { class Simulation; }
namespace spartan
{
    class Entity;
    class Car;
    // Specialized state lives with the car implementation; no PhysX handles are cloned.
    struct VehiclePhysicsState
    {
        VehiclePhysicsState();
        ~VehiclePhysicsState();
        // vehicle wheel entities and state
        Entity* wheel_entities[static_cast<int>(WheelIndex::Count)] = { nullptr, nullptr, nullptr, nullptr };
        Entity* wheel_calipers[static_cast<int>(WheelIndex::Count)] = {}; // optional axle-centered brake_caliper child
        float wheel_radius   = 0.35f; // wheel radius for spin calculation (default)
        math::Vector3 wheel_mesh_center_offsets[static_cast<int>(WheelIndex::Count)] = {};
        bool wheel_offsets_synced = false;  // flag to ensure wheel offsets are synced from entities once
        const void* wheel_ground_actors[static_cast<int>(WheelIndex::Count)] = {};
        uint8_t wheel_ground_surfaces[static_cast<int>(WheelIndex::Count)] = {};
        bool vehicle_simulation_active = true;
        bool vehicle_brake_reverse_enabled = true;
        bool vehicle_full_steering_lock = false;
        bool vehicle_road_surface = false;
        math::Vector3 vehicle_road_position = math::Vector3::Zero;
        math::Vector3 vehicle_road_tangent = math::Vector3::Forward;
        math::Vector3 vehicle_road_normal = math::Vector3::Up;
        VehicleSimMode vehicle_sim_mode = VehicleSimMode::Full;
        math::Vector3 cheap_wheel_local_pos[static_cast<int>(WheelIndex::Count)] = {};
        math::Quaternion cheap_wheel_local_rot[static_cast<int>(WheelIndex::Count)];
        bool cheap_wheel_rest_captured[static_cast<int>(WheelIndex::Count)] = {};
        float cheap_wheel_roll = 0.0f;
        float cheap_wheel_angular_speed = 0.0f;
        float cheap_steer_angle = 0.0f;

        // vehicle chassis entity and suspension state
        Entity* chassis_entity          = nullptr;
        std::vector<Entity*> chassis_entities_to_exclude;
        math::Vector3 chassis_base_pos  = math::Vector3::Zero; // base local position of chassis
        float chassis_suspension_offset = 0.0f;                // current suspension offset (smoothed)

        math::Vector3 vehicle_physics_position = math::Vector3::Zero;
        math::Quaternion vehicle_physics_rotation;
        math::Vector3 vehicle_render_position = math::Vector3::Zero;
        math::Quaternion vehicle_render_rotation;

        // car owner (ticked automatically through entity system)
        class Car* car = nullptr;
        std::unique_ptr<car::Simulation> vehicle_simulation;
        float vehicle_simulation_interval = 0.0f;
        float vehicle_simulation_accumulator = 0.0f;

        std::unique_ptr<TireVisualState> tire_visuals[4];
    };
}
