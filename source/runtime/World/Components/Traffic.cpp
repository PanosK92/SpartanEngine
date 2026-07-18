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

#include "pch.h"
#include "Traffic.h"
#include "Physics.h"
#include "../../Car/Car.h"
#include "../../Core/Engine.h"
#include "../../Core/Timer.h"
#include "../../FileSystem/FileSystem.h"
#include "../../Physics/PhysicsWorld.h"
#include "../Entity.h"
#include "../World.h"
#include "../../IO/pugixml.hpp"

using namespace std;
using namespace spartan::math;

namespace spartan
{
    namespace
    {
        constexpr float sensor_radius = 0.65f;
        constexpr float sensor_height = 1.0f;
        constexpr float trajectory_length = 18.0f;
        constexpr float visit_cell_size = 18.0f;
        constexpr float spawn_separation = 12.0f;
        constexpr float decision_interval = 0.1f;
        constexpr float steering_samples[] = { -1.0f, -0.7f, -0.4f, 0.0f, 0.4f, 0.7f, 1.0f };

        Physics* find_physics(Entity* entity)
        {
            for (Entity* current = entity; current; current = current->GetParent())
            {
                if (Physics* physics = current->GetComponent<Physics>())
                {
                    return physics;
                }
            }
            return nullptr;
        }
    }

    Traffic::Traffic(Entity* entity) : Component(entity)
    {
        SP_REGISTER_ATTRIBUTE_VALUE_VALUE(m_bounds_min, Vector3);
        SP_REGISTER_ATTRIBUTE_VALUE_VALUE(m_bounds_max, Vector3);
        SP_REGISTER_ATTRIBUTE_VALUE_VALUE(m_car_file, string);
        SP_REGISTER_ATTRIBUTE_VALUE_VALUE(m_car_count, uint32_t);
        SP_REGISTER_ATTRIBUTE_VALUE_VALUE(m_simulation_frequency, float);
        SP_REGISTER_ATTRIBUTE_VALUE_VALUE(m_physics_radius, float);
        SP_REGISTER_ATTRIBUTE_VALUE_VALUE(m_physics_exit_radius, float);
    }

    Traffic::~Traffic()
    {
        Stop();
    }

    void Traffic::Start()
    {
        Stop();
        Spawn();
    }

    void Traffic::Stop()
    {
        const vector<Car*> cars = Car::GetAll();
        for (Driver& driver : m_drivers)
        {
            if (driver.car && find(cars.begin(), cars.end(), driver.car) != cars.end())
            {
                driver.car->SetThrottle(0.0f);
                driver.car->SetBrake(1.0f);
                driver.car->SetSteering(0.0f);
                driver.car->Destroy();
            }
            driver.car = nullptr;
            driver.entity = nullptr;
            driver.physics = nullptr;
        }
        m_drivers.clear();
    }

    void Traffic::Tick()
    {
        if (m_drivers.empty() || !Engine::IsFlagSet(EngineMode::Playing) || Engine::IsFlagSet(EngineMode::Paused))
        {
            return;
        }

        const float delta_time = std::clamp(static_cast<float>(Timer::GetDeltaTimeSec()), 0.0f, 0.1f);
        const vector<Car*> cars = Car::GetAll();
        Vector3 player_position;
        const bool has_player = GetPlayerPosition(player_position);
        for (Driver& driver : m_drivers)
        {
            if (!driver.car || find(cars.begin(), cars.end(), driver.car) == cars.end())
            {
                driver.car = nullptr;
                driver.entity = nullptr;
                driver.physics = nullptr;
                continue;
            }

            if (!driver.entity || driver.entity->GetComponent<Physics>() != driver.physics)
            {
                driver.car = nullptr;
                driver.entity = nullptr;
                driver.physics = nullptr;
                continue;
            }

            if (has_player)
            {
                Vector3 offset = driver.entity->GetPosition() - player_position;
                offset.y = 0.0f;
                const float radius = driver.physics_active ? m_physics_exit_radius : m_physics_radius;
                SetPhysicsActive(driver, offset.LengthSquared() <= radius * radius);
            }
            else
            {
                SetPhysicsActive(driver, false);
            }

            if (!driver.physics_active)
            {
                UpdateSplineDriver(driver, delta_time);
                continue;
            }

            driver.decision_time += delta_time;
            if (driver.decision_time >= decision_interval)
            {
                const float decision_time = driver.decision_time;
                driver.decision_time = 0.0f;
                UpdateDriver(driver, decision_time);
            }
        }
    }

    void Traffic::Spawn()
    {
        m_random_state = 0x6d2b79f5;
        m_drivers.reserve(m_car_count);

        string car_path = m_car_file;
        if (!World::GetFilePath().empty())
        {
            const string relative_path = FileSystem::GetDirectoryFromFilePath(World::GetFilePath()) + m_car_file;
            if (FileSystem::Exists(relative_path))
            {
                car_path = relative_path;
            }
        }

        for (uint32_t i = 0; i < m_car_count; i++)
        {
            Vector3 position;
            Quaternion rotation;
            if (!FindSpawnPosition(i, position, rotation))
            {
                SP_LOG_WARNING("Traffic could not find a safe spawn for car %u", i);
                continue;
            }

            const float hue = static_cast<float>((i * 7) % 20) / 20.0f;
            Car::Config config;
            config.position = position;
            config.car_file = car_path;
            config.drivable = true;
            config.customize_materials = false;
            config.high_quality_physics = false;
            config.paint_preset = MaterialPaintPreset::Metallic;
            config.paint_color = Color(0.15f + hue * 0.65f, 0.2f + fmodf(hue + 0.37f, 1.0f) * 0.55f, 0.2f + fmodf(hue + 0.71f, 1.0f) * 0.55f, 1.0f);

            Car* car = Car::Create(config);
            if (!car)
            {
                continue;
            }

            Entity* entity = car->GetRootEntity();
            Physics* physics = entity ? entity->GetComponent<Physics>() : nullptr;
            if (!entity || !physics)
            {
                car->Destroy();
                continue;
            }

            entity->SetObjectName("traffic_car_" + to_string(i + 1));
            entity->SetTransient(true);
            physics->SetBodyTransform(position, rotation);
            physics->SetVehicleSimulationFrequency(m_simulation_frequency);
            car->SetMcpControlled(true);

            Driver driver;
            driver.car = car;
            driver.entity = entity;
            driver.physics = physics;
            driver.collision_group = physics->GetVehicleCollisionGroup();
            driver.cruise_speed = 8.0f + static_cast<float>((i * 17) % 9) * 0.75f;
            driver.caution = 0.85f + static_cast<float>((i * 13) % 7) * 0.05f;
            driver.persistence = 0.7f + static_cast<float>((i * 11) % 8) * 0.08f;
            driver.exploration = 0.8f + static_cast<float>((i * 19) % 9) * 0.08f;
            driver.decision_time = decision_interval * static_cast<float>(i % 20) / 20.0f;
            driver.last_position = position;
            driver.spline_speed = driver.cruise_speed;
            Vector3 initial_velocity = entity->GetForward();
            initial_velocity.y = 0.0f;
            initial_velocity.Normalize();
            physics->SetLinearVelocity(initial_velocity * driver.cruise_speed);
            m_drivers.push_back(std::move(driver));
        }
    }

    bool Traffic::GetPlayerPosition(Vector3& position) const
    {
        const vector<Car*> cars = Car::GetAll();
        for (Car* car : cars)
        {
            if (car && car->IsOccupied() && car->GetRootEntity())
            {
                position = car->GetRootEntity()->GetPosition();
                return true;
            }
        }

        for (Car* car : cars)
        {
            if (!car || !car->GetRootEntity())
            {
                continue;
            }
            const bool is_traffic = any_of(m_drivers.begin(), m_drivers.end(), [car](const Driver& driver) { return driver.car == car; });
            if (!is_traffic)
            {
                position = car->GetRootEntity()->GetPosition();
                return true;
            }
        }
        return false;
    }

    void Traffic::SetPhysicsActive(Driver& driver, bool active)
    {
        if (driver.physics_active == active || !driver.car || !driver.entity || !driver.physics)
        {
            return;
        }

        if (!active)
        {
            Vector3 velocity = driver.physics->GetLinearVelocity();
            velocity.y = 0.0f;
            driver.spline_speed = std::max(velocity.Length(), driver.cruise_speed);
            driver.car->SetThrottle(0.0f);
            driver.car->SetBrake(0.0f);
            driver.car->SetSteering(0.0f);
            driver.car->SetHandbrake(0.0f);
            driver.physics->SetVehicleSimulationActive(false);
            driver.spline_duration = 0.0f;
        }
        else
        {
            driver.physics->SetVehicleSimulationActive(true);
            driver.physics->SetBodyTransform(driver.entity->GetPosition(), driver.entity->GetRotation());
            Vector3 forward = driver.entity->GetForward();
            forward.y = 0.0f;
            forward.Normalize();
            driver.physics->SetLinearVelocity(forward * driver.spline_speed);
            driver.last_position = driver.entity->GetPosition();
            driver.stalled_time = 0.0f;
            driver.blocked_time = 0.0f;
            driver.recovery = RecoveryState::None;
            driver.decision_time = decision_interval;
        }
        driver.physics_active = active;
    }

    void Traffic::CreateSpline(Driver& driver)
    {
        Trajectory best;
        for (float steering : steering_samples)
        {
            const Trajectory candidate = EvaluateTrajectory(driver, steering);
            if (candidate.score > best.score)
            {
                best = candidate;
            }
        }

        Vector3 forward = driver.entity->GetForward();
        Vector3 right = driver.entity->GetRight();
        forward.y = 0.0f;
        right.y = 0.0f;
        forward.Normalize();
        right.Normalize();

        float steering = best.score > -999999.0f ? best.steering : 1.0f;
        float length = best.traversable ? std::clamp(best.clearance - 2.0f, 10.0f, 24.0f) : 6.0f;
        Vector3 direction = (forward * cosf(steering * 0.9f) + right * sinf(steering * 0.9f)).Normalized();
        if (!IsInsideBounds(driver.entity->GetPosition() + direction * length, 4.0f))
        {
            Vector3 center = (m_bounds_min + m_bounds_max) * 0.5f;
            direction = center - driver.entity->GetPosition();
            direction.y = 0.0f;
            direction.Normalize();
            length = 12.0f;
        }

        driver.spline_start = driver.entity->GetPosition();
        driver.spline_end = driver.spline_start + direction * length;
        driver.spline_control_a = driver.spline_start + forward * (length * 0.35f);
        driver.spline_control_b = driver.spline_end - direction * (length * 0.35f);

        Vector3 ground;
        if (SampleGround(driver.spline_control_a, ground))
        {
            driver.spline_control_a.y = ground.y + 1.0f;
        }
        if (SampleGround(driver.spline_control_b, ground))
        {
            driver.spline_control_b.y = ground.y + 1.0f;
        }
        if (SampleGround(driver.spline_end, ground))
        {
            driver.spline_end.y = ground.y + 1.0f;
        }

        driver.spline_speed = std::clamp(driver.spline_speed, driver.cruise_speed * 0.8f, driver.cruise_speed * 1.2f);
        driver.spline_duration = length / std::max(driver.spline_speed, 1.0f);
        driver.spline_time = 0.0f;
        driver.steering = steering;
    }

    void Traffic::UpdateSplineDriver(Driver& driver, float delta_time)
    {
        if (driver.spline_duration <= 0.0f || driver.spline_time >= driver.spline_duration)
        {
            CreateSpline(driver);
        }

        driver.spline_time = std::min(driver.spline_time + delta_time, driver.spline_duration);
        const float t = driver.spline_duration > 0.0f ? driver.spline_time / driver.spline_duration : 1.0f;
        const float inverse_t = 1.0f - t;
        const Vector3 position = driver.spline_start * (inverse_t * inverse_t * inverse_t) + driver.spline_control_a * (3.0f * inverse_t * inverse_t * t) + driver.spline_control_b * (3.0f * inverse_t * t * t) + driver.spline_end * (t * t * t);
        for (const Driver& other : m_drivers)
        {
            if (&other != &driver && other.entity && (other.entity->GetPosition() - position).LengthSquared() < 12.25f)
            {
                driver.spline_duration = 0.0f;
                return;
            }
        }
        Vector3 direction = (driver.spline_control_a - driver.spline_start) * (3.0f * inverse_t * inverse_t) + (driver.spline_control_b - driver.spline_control_a) * (6.0f * inverse_t * t) + (driver.spline_end - driver.spline_control_b) * (3.0f * t * t);
        direction.y = 0.0f;
        if (direction.LengthSquared() > 0.0001f)
        {
            direction.Normalize();
            driver.entity->SetRotation(Quaternion::FromLookRotation(direction));
        }
        driver.entity->SetPosition(position);
    }

    void Traffic::UpdateDriver(Driver& driver, float delta_time)
    {
        if (!driver.car || !driver.entity || !driver.physics)
        {
            return;
        }

        const Vector3 position = driver.entity->GetPosition();
        Vector3 forward = driver.entity->GetForward();
        Vector3 right = driver.entity->GetRight();
        forward.y = 0.0f;
        right.y = 0.0f;
        forward.Normalize();
        right.Normalize();
        const Vector3 velocity = driver.physics->GetLinearVelocity();
        const float speed = velocity.Length();

        driver.memory_time += delta_time;
        if (driver.memory_time >= 1.0f)
        {
            driver.memory_time = 0.0f;
            uint16_t& visits = driver.visits[GetVisitKey(position)];
            visits = static_cast<uint16_t>(std::min<uint32_t>(visits + 1, 255));
            if (driver.visits.size() > 2048)
            {
                for (auto it = driver.visits.begin(); it != driver.visits.end();)
                {
                    if (it->second <= 1)
                    {
                        it = driver.visits.erase(it);
                    }
                    else
                    {
                        it->second--;
                        ++it;
                    }
                }
            }
        }

        const float progress = (position - driver.last_position).Length();
        driver.last_position = position;
        driver.stalled_time = speed < 0.8f && progress < 0.08f ? driver.stalled_time + delta_time : 0.0f;

        Trajectory best;
        for (float steering : steering_samples)
        {
            const Trajectory candidate = EvaluateTrajectory(driver, steering);
            if (candidate.score > best.score)
            {
                best = candidate;
            }
        }

        bool spacing_emergency = false;
        bool spacing_yielding = false;
        for (const Driver& other : m_drivers)
        {
            if (&other == &driver || !other.entity)
            {
                continue;
            }

            Vector3 relative = other.entity->GetPosition() - position;
            relative.y = 0.0f;
            const float distance = relative.Length();
            if (distance >= 12.0f || distance <= 0.001f)
            {
                continue;
            }

            const float forward_distance = Vector3::Dot(relative, forward);
            const float lateral_distance = Vector3::Dot(relative, right);
            if (distance < 3.5f)
            {
                spacing_emergency = true;
            }
            if (forward_distance > 0.0f && fabsf(lateral_distance) < 4.0f)
            {
                spacing_yielding = distance < 10.0f;
                spacing_emergency = spacing_emergency || distance < 5.0f;
                const float away = fabsf(lateral_distance) > 0.2f ? (lateral_distance > 0.0f ? -1.0f : 1.0f) : (driver.entity->GetObjectId() < other.entity->GetObjectId() ? -1.0f : 1.0f);
                const float avoidance_weight = std::clamp((10.0f - distance) / 7.0f, 0.0f, 1.0f);
                best.steering += (away - best.steering) * avoidance_weight;
                best.clearance = std::min(best.clearance, std::max(distance - 3.0f, 0.0f));
            }
        }

        driver.blocked_time = best.traversable ? 0.0f : driver.blocked_time + delta_time;
        if (driver.recovery == RecoveryState::None && (driver.blocked_time > 0.7f || driver.stalled_time > 4.0f))
        {
            driver.recovery = RecoveryState::Brake;
            driver.recovery_time = 0.0f;
        }

        if (driver.recovery != RecoveryState::None)
        {
            driver.recovery_time += delta_time;
            if (driver.recovery == RecoveryState::Brake)
            {
                driver.car->SetThrottle(0.0f);
                driver.car->SetBrake(1.0f);
                driver.car->SetSteering(best.steering);
                driver.car->SetHandbrake(0.0f);
                if (speed < 0.5f || driver.recovery_time > 1.2f)
                {
                    driver.recovery = RecoveryState::Reverse;
                    driver.recovery_time = 0.0f;
                }
                return;
            }

            Vector3 reverse_hit;
            float reverse_distance = 6.0f;
            Entity* reverse_entity = nullptr;
            const bool reverse_blocked = PhysicsWorld::SphereCast(position + Vector3::Up * sensor_height, -forward, sensor_radius, 6.0f, driver.collision_group, reverse_hit, reverse_distance, reverse_entity);
            driver.car->SetThrottle(0.0f);
            driver.car->SetBrake(reverse_blocked && reverse_distance < 2.0f ? 0.0f : 1.0f);
            driver.car->SetSteering(-best.steering);
            driver.car->SetHandbrake(reverse_blocked && reverse_distance < 2.0f ? 1.0f : 0.0f);
            if (driver.recovery_time > 2.4f || (reverse_blocked && reverse_distance < 2.0f))
            {
                driver.recovery = RecoveryState::None;
                driver.blocked_time = 0.0f;
                driver.stalled_time = 0.0f;
                driver.recovery_time = 0.0f;
            }
            return;
        }

        float obstacle_distance = trajectory_length;
        Vector3 obstacle_position;
        Entity* obstacle_entity = nullptr;
        const bool obstacle = PhysicsWorld::SphereCast(position + Vector3::Up * sensor_height, forward, sensor_radius, trajectory_length, driver.collision_group, obstacle_position, obstacle_distance, obstacle_entity);
        bool emergency = spacing_emergency || (obstacle && obstacle_distance < 2.5f * driver.caution);
        bool yielding = spacing_yielding;
        if (obstacle && obstacle_entity)
        {
            if (Physics* obstacle_physics = find_physics(obstacle_entity))
            {
                const Vector3 relative_velocity = velocity - obstacle_physics->GetLinearVelocity();
                const float closing_speed = std::max(Vector3::Dot(relative_velocity, forward), 0.0f);
                const float time_to_collision = closing_speed > 0.1f ? obstacle_distance / closing_speed : 1000.0f;
                emergency = emergency || time_to_collision < 1.2f * driver.caution;
                yielding = obstacle_physics->GetBodyType() == BodyType::Vehicle && obstacle_distance < 7.0f && driver.entity->GetObjectId() > obstacle_physics->GetEntity()->GetObjectId();
            }
        }

        const float turn_factor = 1.0f - std::min(fabsf(best.steering) * 0.55f, 0.65f);
        const float clearance_factor = std::clamp(best.clearance / 14.0f, 0.2f, 1.0f);
        const float target_speed = driver.cruise_speed * turn_factor * clearance_factor;
        const float speed_error = target_speed - speed;
        const float steering_blend = 1.0f - expf(-delta_time * (3.0f + driver.persistence));
        driver.steering += (best.steering - driver.steering) * steering_blend;

        driver.car->SetSteering(driver.steering);
        driver.car->SetHandbrake(0.0f);
        if (emergency)
        {
            driver.car->SetThrottle(0.0f);
            driver.car->SetBrake(1.0f);
        }
        else if (yielding || speed_error < -1.0f)
        {
            driver.car->SetThrottle(0.0f);
            driver.car->SetBrake(yielding ? 0.55f : std::clamp(-speed_error / 6.0f, 0.0f, 0.65f));
        }
        else
        {
            driver.car->SetThrottle(std::clamp(0.25f + speed_error / 8.0f, 0.15f, 0.8f));
            driver.car->SetBrake(0.0f);
        }
    }

    Traffic::Trajectory Traffic::EvaluateTrajectory(const Driver& driver, float steering) const
    {
        Trajectory result;
        result.steering = steering;

        Vector3 forward = driver.entity->GetForward();
        Vector3 right = driver.entity->GetRight();
        forward.y = 0.0f;
        right.y = 0.0f;
        forward.Normalize();
        right.Normalize();
        const float angle = steering * 0.9f;
        const Vector3 direction = (forward * cosf(angle) + right * sinf(angle)).Normalized();
        const Vector3 origin = driver.entity->GetPosition() + Vector3::Up * sensor_height;

        Vector3 hit_position;
        Entity* hit_entity = nullptr;
        result.clearance = trajectory_length;
        PhysicsWorld::SphereCast(origin, direction, sensor_radius, trajectory_length, driver.collision_group, hit_position, result.clearance, hit_entity);

        float slope_penalty = 0.0f;
        Vector3 previous_ground;
        if (!SampleGround(driver.entity->GetPosition() + direction * 2.0f, previous_ground))
        {
            return result;
        }

        for (float distance : { 5.0f, 9.0f, 13.0f, 17.0f })
        {
            Vector3 sample_position = driver.entity->GetPosition() + direction * distance;
            if (!IsInsideBounds(sample_position, 5.0f))
            {
                return result;
            }

            Vector3 ground;
            if (!SampleGround(sample_position, ground))
            {
                return result;
            }
            const float rise = fabsf(ground.y - previous_ground.y);
            if (rise > 2.2f)
            {
                return result;
            }
            slope_penalty += rise;
            previous_ground = ground;
        }

        for (const Driver& other : m_drivers)
        {
            if (&other == &driver || !other.entity)
            {
                continue;
            }
            Vector3 relative = other.entity->GetPosition() - driver.entity->GetPosition();
            relative.y = 0.0f;
            const float distance_along = Vector3::Dot(relative, direction);
            const Vector3 lateral = relative - direction * distance_along;
            if (distance_along > 0.0f && distance_along < result.clearance && lateral.LengthSquared() < 16.0f)
            {
                result.clearance = std::max(distance_along - 3.0f, 0.0f);
            }
        }
        const Vector3 future_position = driver.entity->GetPosition() + direction * std::min(result.clearance, trajectory_length);
        const float novelty = GetNovelty(driver, future_position);
        const float clearance_score = std::min(result.clearance / trajectory_length, 1.0f) * 7.0f;
        const float smoothness = 1.0f - std::min(fabsf(steering - driver.steering), 1.0f);
        const float heading_persistence = 1.0f - fabsf(steering);
        result.score = clearance_score + novelty * driver.exploration * 3.0f + smoothness * driver.persistence + heading_persistence * 0.7f - slope_penalty * driver.caution;
        result.traversable = result.clearance > 3.0f * driver.caution;
        return result;
    }

    bool Traffic::FindSpawnPosition(uint32_t index, Vector3& position, Quaternion& rotation)
    {
        const vector<Car*> cars = Car::GetAll();
        auto random_unit = [this]()
        {
            m_random_state = m_random_state * 1664525u + 1013904223u;
            return static_cast<float>(m_random_state & 0x00ffffffu) / static_cast<float>(0x01000000u);
        };

        for (uint32_t attempt = 0; attempt < 768; attempt++)
        {
            const float x = m_bounds_min.x + (m_bounds_max.x - m_bounds_min.x) * random_unit();
            const float z = m_bounds_min.z + (m_bounds_max.z - m_bounds_min.z) * random_unit();
            Vector3 ground;
            if (!SampleGround(Vector3(x, m_bounds_max.y, z), ground))
            {
                continue;
            }

            const Vector3 candidate(ground.x, ground.y + 1.0f, ground.z);
            bool separated = true;
            for (Car* car : cars)
            {
                Entity* car_entity = car ? car->GetRootEntity() : nullptr;
                if (car_entity && (car_entity->GetPosition() - candidate).LengthSquared() < spawn_separation * spawn_separation)
                {
                    separated = false;
                    break;
                }
            }
            if (!separated)
            {
                continue;
            }

            const float yaw = random_unit() * pi * 2.0f;
            const Vector3 direction(sinf(yaw), 0.0f, cosf(yaw));
            const Vector3 right(direction.z, 0.0f, -direction.x);
            bool clear = true;
            for (const Vector3& probe_direction : { direction, -direction, right, -right })
            {
                Vector3 probe_ground;
                if (!SampleGround(candidate + probe_direction * 2.5f, probe_ground))
                {
                    clear = false;
                    break;
                }
                if (fabsf(probe_ground.y - ground.y) > 1.0f)
                {
                    clear = false;
                    break;
                }

                Vector3 hit_position;
                float hit_distance = 3.0f;
                Entity* hit_entity = nullptr;
                if (PhysicsWorld::SphereCast(candidate + Vector3::Up * sensor_height, probe_direction, 1.0f, 3.0f, 0, hit_position, hit_distance, hit_entity) && hit_distance < 2.5f)
                {
                    clear = false;
                    break;
                }
            }
            if (!clear)
            {
                continue;
            }

            position = candidate;
            rotation = Quaternion::FromLookRotation(direction);
            return true;
        }

        SP_LOG_WARNING("Traffic spawn search exhausted for car %u", index);
        return false;
    }

    bool Traffic::SampleGround(const Vector3& position, Vector3& ground_position) const
    {
        const float height = std::max(position.y, m_bounds_max.y) + 10.0f;
        const Vector3 origin(position.x, height, position.z);
        const float distance = height - m_bounds_min.y + 10.0f;
        if (!PhysicsWorld::RaycastStatic(origin, Vector3::Down, distance, ground_position))
        {
            return false;
        }
        return ground_position.y >= m_bounds_min.y && ground_position.y <= m_bounds_max.y;
    }

    bool Traffic::IsInsideBounds(const Vector3& position, float margin) const
    {
        return position.x >= m_bounds_min.x + margin && position.x <= m_bounds_max.x - margin && position.z >= m_bounds_min.z + margin && position.z <= m_bounds_max.z - margin;
    }

    uint64_t Traffic::GetVisitKey(const Vector3& position) const
    {
        const int32_t x = static_cast<int32_t>(floorf((position.x - m_bounds_min.x) / visit_cell_size));
        const int32_t z = static_cast<int32_t>(floorf((position.z - m_bounds_min.z) / visit_cell_size));
        return (static_cast<uint64_t>(static_cast<uint32_t>(x)) << 32) | static_cast<uint32_t>(z);
    }

    float Traffic::GetNovelty(const Driver& driver, const Vector3& position) const
    {
        const auto it = driver.visits.find(GetVisitKey(position));
        return it == driver.visits.end() ? 1.0f : 1.0f / (1.0f + static_cast<float>(it->second));
    }

    void Traffic::Save(pugi::xml_node& node)
    {
        node.append_attribute("car_count") = m_car_count;
        node.append_attribute("car_file") = m_car_file.c_str();
        node.append_attribute("bounds_min_x") = m_bounds_min.x;
        node.append_attribute("bounds_min_y") = m_bounds_min.y;
        node.append_attribute("bounds_min_z") = m_bounds_min.z;
        node.append_attribute("bounds_max_x") = m_bounds_max.x;
        node.append_attribute("bounds_max_y") = m_bounds_max.y;
        node.append_attribute("bounds_max_z") = m_bounds_max.z;
        node.append_attribute("simulation_frequency") = m_simulation_frequency;
        node.append_attribute("physics_radius") = m_physics_radius;
        node.append_attribute("physics_exit_radius") = m_physics_exit_radius;
    }

    void Traffic::Load(pugi::xml_node& node)
    {
        m_car_count = node.attribute("car_count").as_uint(m_car_count);
        m_car_file = node.attribute("car_file").as_string(m_car_file.c_str());
        m_bounds_min.x = node.attribute("bounds_min_x").as_float(m_bounds_min.x);
        m_bounds_min.y = node.attribute("bounds_min_y").as_float(m_bounds_min.y);
        m_bounds_min.z = node.attribute("bounds_min_z").as_float(m_bounds_min.z);
        m_bounds_max.x = node.attribute("bounds_max_x").as_float(m_bounds_max.x);
        m_bounds_max.y = node.attribute("bounds_max_y").as_float(m_bounds_max.y);
        m_bounds_max.z = node.attribute("bounds_max_z").as_float(m_bounds_max.z);
        m_simulation_frequency = std::clamp(node.attribute("simulation_frequency").as_float(m_simulation_frequency), 20.0f, 100.0f);
        m_physics_radius = std::clamp(node.attribute("physics_radius").as_float(m_physics_radius), 20.0f, 300.0f);
        m_physics_exit_radius = std::clamp(node.attribute("physics_exit_radius").as_float(m_physics_exit_radius), m_physics_radius + 5.0f, 350.0f);
        m_car_count = std::min(m_car_count, 64u);
    }
}
