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

#include "Component.h"
#include "../../Math/Vector3.h"
#include "../../Math/Quaternion.h"
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace spartan
{
    class Car;
    class Entity;
    class Physics;

    class Traffic : public Component
    {
    public:
        Traffic(Entity* entity);
        ~Traffic() override;

        void Start() override;
        void Stop() override;
        void Tick() override;
        void Save(pugi::xml_node& node) override;
        void Load(pugi::xml_node& node) override;

    private:
        enum class RecoveryState
        {
            None,
            Brake,
            Reverse
        };

        struct Driver
        {
            Car* car = nullptr;
            Entity* entity = nullptr;
            Physics* physics = nullptr;
            uint32_t collision_group = 0;
            float cruise_speed = 10.0f;
            float caution = 1.0f;
            float persistence = 1.0f;
            float exploration = 1.0f;
            float steering = 0.0f;
            float decision_time = 0.0f;
            float blocked_time = 0.0f;
            float stalled_time = 0.0f;
            float recovery_time = 0.0f;
            float memory_time = 0.0f;
            float spline_time = 0.0f;
            float spline_duration = 0.0f;
            float spline_speed = 0.0f;
            math::Vector3 last_position = math::Vector3::Zero;
            math::Vector3 spline_start = math::Vector3::Zero;
            math::Vector3 spline_control_a = math::Vector3::Zero;
            math::Vector3 spline_control_b = math::Vector3::Zero;
            math::Vector3 spline_end = math::Vector3::Zero;
            RecoveryState recovery = RecoveryState::None;
            bool physics_active = true;
            std::unordered_map<uint64_t, uint16_t> visits;
        };

        struct Trajectory
        {
            float steering = 0.0f;
            float clearance = 0.0f;
            float score = -1000000.0f;
            bool traversable = false;
        };

        void Spawn();
        void UpdateDriver(Driver& driver, float delta_time);
        void UpdateSplineDriver(Driver& driver, float delta_time);
        void CreateSpline(Driver& driver);
        void SetPhysicsActive(Driver& driver, bool active);
        bool GetPlayerPosition(math::Vector3& position) const;
        Trajectory EvaluateTrajectory(const Driver& driver, float steering) const;
        bool FindSpawnPosition(uint32_t index, math::Vector3& position, math::Quaternion& rotation);
        bool SampleGround(const math::Vector3& position, math::Vector3& ground_position) const;
        bool IsInsideBounds(const math::Vector3& position, float margin) const;
        uint64_t GetVisitKey(const math::Vector3& position) const;
        float GetNovelty(const Driver& driver, const math::Vector3& position) const;

        std::vector<Driver> m_drivers;
        math::Vector3 m_bounds_min = math::Vector3(-220.0f, -10.0f, -380.0f);
        math::Vector3 m_bounds_max = math::Vector3(380.0f, 80.0f, 260.0f);
        std::string m_car_file = "cars/ferrari_laferrari.car";
        uint32_t m_car_count = 20;
        float m_simulation_frequency = 50.0f;
        float m_physics_radius = 80.0f;
        float m_physics_exit_radius = 100.0f;
        uint32_t m_random_state = 0x6d2b79f5;
    };
}
