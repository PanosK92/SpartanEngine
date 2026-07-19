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
#include "Spline.h"
#include "../../Car/Car.h"
#include "../../Car/CarPresets.h"
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
        constexpr float gravity = 9.81f;
        constexpr float sensor_height = 1.0f;
        constexpr float visit_cell_size = 18.0f;
        constexpr float spawn_separation = 12.0f;
        constexpr float decision_interval = 0.1f;
        constexpr float steering_samples[] = { -1.0f, -0.72f, -0.48f, -0.3f, -0.18f, -0.08f, 0.0f, 0.08f, 0.18f, 0.3f, 0.48f, 0.72f, 1.0f };

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

        Vector3 planar(Vector3 value)
        {
            value.y = 0.0f;
            if (!std::isfinite(value.x) || !std::isfinite(value.z))
            {
                return Vector3::Zero;
            }
            return value;
        }

        Vector3 horizontal(Vector3 value)
        {
            value = planar(value);
            if (value.LengthSquared() > 0.0001f)
            {
                value.Normalize();
            }
            return value;
        }

        float signed_angle(const Vector3& from, const Vector3& to)
        {
            return atan2f(Vector3::Dot(Vector3::Cross(from, to), Vector3::Up), Vector3::Dot(from, to));
        }

        float positive_or(float value, float fallback)
        {
            return std::isfinite(value) && value > 0.0f ? value : fallback;
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
        CacheRoads();
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
        m_road_samples.clear();
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
        Vector3 player_velocity;
        const bool has_player = GetPlayerState(player_position, player_velocity);
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

            UpdateTelemetry(driver, delta_time);
            driver.decision_time += delta_time;
            if (driver.decision_time >= decision_interval)
            {
                const float elapsed = driver.decision_time;
                driver.decision_time = 0.0f;
                PlanDriver(driver, elapsed);
            }
            ControlDriver(driver, delta_time);
            if (driver.diagnostic_time > 0.0f)
            {
                driver.diagnostic_time = std::max(driver.diagnostic_time - delta_time, 0.0f);
                driver.diagnostic_interval += delta_time;
                if (driver.diagnostic_interval >= 0.25f)
                {
                    driver.diagnostic_interval = 0.0f;
                    SP_LOG_INFO("traffic_ai %s speed=%.2f spline=%.2f target=%.2f clearance=%.2f traversable=%d blocked=%d throttle=%.2f brake=%.2f plan_steer=%.2f command_steer=%.2f applied_steer=%.2f grip=%.2f load=%.2f grounded=%.2f recovery=%u gear=%d", driver.entity->GetObjectName().c_str(), driver.telemetry.speed, driver.spline_speed, driver.plan.target_speed, driver.plan.clearance, driver.plan.traversable ? 1 : 0, driver.plan.blocked ? 1 : 0, driver.throttle, driver.brake, driver.plan.steering, driver.steering, driver.physics->GetVehicleSteering(), driver.telemetry.grip, driver.telemetry.load_factor, driver.telemetry.grounded_ratio, static_cast<uint32_t>(driver.recovery), driver.physics->GetCurrentGear());
                }
            }
        }
    }

    void Traffic::CacheRoads()
    {
        m_road_samples.clear();
        for (Entity* entity : World::GetEntities())
        {
            Spline* spline = entity ? entity->GetComponent<Spline>() : nullptr;
            if (!spline || spline->GetProfile() != SplineProfile::Road || (spline->GetControlPointCount() < 2 && !spline->IsAttached()))
            {
                continue;
            }

            const float length = spline->GetLength();
            if (!std::isfinite(length) || length < 1.0f)
            {
                continue;
            }

            const uint32_t sample_count = std::clamp(static_cast<uint32_t>(ceilf(length / 6.0f)) + 1u, 8u, 256u);
            for (uint32_t i = 0; i < sample_count; i++)
            {
                const float t = static_cast<float>(i) / static_cast<float>(sample_count - 1);
                RoadSample sample;
                sample.position = spline->GetPoint(t);
                sample.tangent = horizontal(spline->GetTangent(t));
                sample.half_width = std::max(positive_or((spline->GetRoadWidth() + (spline->GetRoadWidthEnd() - spline->GetRoadWidth()) * t) * 0.5f, 4.0f), 1.5f);
                if (std::isfinite(sample.position.x) && std::isfinite(sample.position.y) && std::isfinite(sample.position.z) && sample.tangent.LengthSquared() > 0.5f)
                {
                    m_road_samples.push_back(sample);
                }
            }
        }
        SP_LOG_INFO("traffic_ai cached %zu road samples", m_road_samples.size());
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
            config.high_quality_physics = true;
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
            physics->SetManualTransmission(false);
            car->SetMcpControlled(true);

            Driver driver;
            driver.car = car;
            driver.entity = entity;
            driver.physics = physics;
            driver.collision_group = physics->GetVehicleCollisionGroup();
            driver.cruise_speed = 15.0f + static_cast<float>((i * 17) % 9) * 1.25f;
            driver.caution = 0.85f + static_cast<float>((i * 13) % 7) * 0.05f;
            driver.persistence = 0.7f + static_cast<float>((i * 11) % 8) * 0.08f;
            driver.exploration = 0.8f + static_cast<float>((i * 19) % 9) * 0.08f;
            driver.decision_time = decision_interval * static_cast<float>(i % 20) / 20.0f;
            driver.last_position = position;
            driver.spline_speed = driver.cruise_speed;
            InitializeLimits(driver);
            const Vector3 initial_velocity = horizontal(entity->GetForward()) * std::min(driver.cruise_speed, 10.0f);
            physics->SetLinearVelocity(initial_velocity);
            m_drivers.push_back(std::move(driver));
        }
    }

    void Traffic::InitializeLimits(Driver& driver)
    {
        const car::car_definition* definition = driver.car ? driver.car->GetDefinition() : nullptr;
        if (!definition)
        {
            return;
        }

        const car::car_preset& preset = definition->performance;
        VehicleLimits& limits = driver.limits;
        limits.mass = std::max(positive_or(preset.mass, limits.mass), 300.0f);
        limits.wheelbase = std::max(positive_or(preset.wheelbase, limits.wheelbase), 1.5f);
        limits.half_width = std::max(positive_or(preset.width, limits.half_width * 2.0f) * 0.5f, 0.7f);
        limits.wheel_radius = std::max(positive_or((preset.front_wheel_radius + preset.rear_wheel_radius) * 0.5f, limits.wheel_radius), 0.2f);
        limits.max_steer_angle = std::clamp(positive_or(preset.max_steer_angle, limits.max_steer_angle), 0.25f, 0.9f);
        limits.lateral_acceleration = std::clamp(positive_or(preset.tire_friction, 0.8f) * gravity, 4.0f, 16.0f);

        float highest_forward_ratio = 0.0f;
        for (int gear = 2; gear < std::clamp(preset.gear_count, 2, car::max_gears); gear++)
        {
            highest_forward_ratio = std::max(highest_forward_ratio, positive_or(fabsf(preset.gear_ratios[gear]), 0.0f));
        }
        const float final_drive = std::max(positive_or(preset.final_drive, 1.0f), 0.1f);
        const float drivetrain_efficiency = std::clamp(positive_or(preset.drivetrain_efficiency, 0.8f), 0.2f, 1.0f);
        const float engine_drive_torque = positive_or(preset.engine_peak_torque, 0.0f) * highest_forward_ratio * final_drive;
        const float electric_drive_torque = preset.electric_enabled ? positive_or(preset.electric_motor_torque, 0.0f) * final_drive : 0.0f;
        const float drive_torque = (engine_drive_torque + electric_drive_torque) * drivetrain_efficiency;
        limits.acceleration = std::clamp(drive_torque / (limits.wheel_radius * limits.mass), 1.0f, limits.lateral_acceleration);
        limits.braking = std::clamp(positive_or(preset.brake_force, limits.mass * limits.braking) / limits.mass, 3.0f, limits.lateral_acceleration * 1.15f);
        limits.drag_factor = std::max(0.5f * 1.225f * positive_or(preset.drag_coeff, 0.0f) * positive_or(preset.frontal_area, 0.0f) / limits.mass, 0.0f);
    }

    void Traffic::UpdateTelemetry(Driver& driver, float delta_time)
    {
        Telemetry& telemetry = driver.telemetry;
        telemetry.velocity = driver.physics->GetLinearVelocity();
        if (!std::isfinite(telemetry.velocity.x) || !std::isfinite(telemetry.velocity.y) || !std::isfinite(telemetry.velocity.z))
        {
            telemetry.velocity = Vector3::Zero;
        }
        const Vector3 horizontal_velocity = planar(telemetry.velocity);
        telemetry.speed = horizontal_velocity.Length();
        const Vector3 forward = horizontal(driver.entity->GetForward());
        const Vector3 right = horizontal(driver.entity->GetRight());
        telemetry.forward_speed = Vector3::Dot(telemetry.velocity, forward);
        telemetry.lateral_speed = Vector3::Dot(telemetry.velocity, right);
        telemetry.acceleration = delta_time > 0.0001f ? (telemetry.speed - driver.previous_speed) / delta_time : 0.0f;
        driver.previous_speed = telemetry.speed;
        telemetry.abs_active = driver.physics->IsAbsActiveAny();
        telemetry.tc_active = driver.physics->IsTcActive();
        telemetry.drive_acceleration = driver.limits.acceleration;
        const car::car_definition* definition = driver.car->GetDefinition();
        if (definition)
        {
            const car::car_preset& preset = definition->performance;
            const int gear = driver.physics->GetCurrentGear();
            if (gear >= 2 && gear < std::clamp(preset.gear_count, 2, car::max_gears))
            {
                const float final_drive = std::max(positive_or(preset.final_drive, 1.0f), 0.1f);
                const float engine_axle_torque = positive_or(driver.physics->GetEngineTorque(), 0.0f) * positive_or(fabsf(preset.gear_ratios[gear]), 0.0f) * final_drive;
                const float electric_axle_torque = preset.electric_enabled ? positive_or(preset.electric_motor_torque, 0.0f) * final_drive : 0.0f;
                const float wheel_force = (engine_axle_torque + electric_axle_torque) * std::clamp(positive_or(preset.drivetrain_efficiency, 0.8f), 0.2f, 1.0f) / driver.limits.wheel_radius;
                telemetry.drive_acceleration = std::clamp(wheel_force / driver.limits.mass, 0.5f, driver.limits.lateral_acceleration);
            }
        }

        float slip = 0.0f;
        float grip = 0.0f;
        float tire_load = 0.0f;
        uint32_t grounded = 0;
        for (uint32_t i = 0; i < static_cast<uint32_t>(WheelIndex::Count); i++)
        {
            const WheelIndex wheel = static_cast<WheelIndex>(i);
            if (driver.physics->IsWheelGrounded(wheel))
            {
                grounded++;
            }
            slip += positive_or(driver.physics->GetWheelSlipMagnitude(wheel), 0.0f);
            grip += positive_or(driver.physics->GetWheelTempGripFactor(wheel) * driver.physics->GetWheelWearGripFactor(wheel), 0.35f);
            tire_load += positive_or(driver.physics->GetWheelTireLoad(wheel), 0.0f);
        }
        const bool has_wheel_telemetry = grounded > 0 || tire_load > driver.limits.mass * gravity * 0.05f;
        telemetry.slip = has_wheel_telemetry ? slip * 0.25f : 0.0f;
        telemetry.grip = has_wheel_telemetry ? std::clamp(grip * 0.25f, 0.35f, 1.2f) : 1.0f;
        telemetry.load_factor = has_wheel_telemetry ? std::clamp(tire_load / (driver.limits.mass * gravity), 0.45f, 1.15f) : 1.0f;
        telemetry.grounded_ratio = has_wheel_telemetry ? static_cast<float>(grounded) * 0.25f : 1.0f;
        if (driver.throttle > 0.35f && telemetry.grounded_ratio > 0.5f)
        {
            const float measured_response = std::clamp(telemetry.acceleration / std::max(telemetry.drive_acceleration * driver.throttle, 0.1f), 0.35f, 1.25f);
            telemetry.response_factor += (measured_response - telemetry.response_factor) * std::clamp(delta_time * 0.8f, 0.0f, 1.0f);
        }
        telemetry.drive_acceleration *= telemetry.response_factor;
    }

    bool Traffic::GetPlayerState(Vector3& position, Vector3& velocity) const
    {
        const vector<Car*> cars = Car::GetAll();
        for (Car* car : cars)
        {
            if (car && car->IsOccupied() && car->GetRootEntity())
            {
                position = car->GetRootEntity()->GetPosition();
                Physics* physics = car->GetRootEntity()->GetComponent<Physics>();
                velocity = physics ? planar(physics->GetLinearVelocity()) : Vector3::Zero;
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
                Physics* physics = car->GetRootEntity()->GetComponent<Physics>();
                velocity = physics ? planar(physics->GetLinearVelocity()) : Vector3::Zero;
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
            driver.spline_speed = std::clamp(velocity.Length(), 2.0f, driver.cruise_speed);
            driver.throttle = 0.0f;
            driver.brake = 0.0f;
            driver.steering = 0.0f;
            driver.car->SetThrottle(0.0f);
            driver.car->SetBrake(0.0f);
            driver.car->SetSteering(0.0f);
            driver.car->SetHandbrake(0.0f);
            driver.physics->SetVehicleSimulationActive(false);
            driver.spline_duration = 0.0f;
            driver.transition_time = 0.0f;
        }
        else
        {
            driver.physics->SetVehicleSimulationActive(true);
            driver.physics->SetBodyTransform(driver.entity->GetPosition(), driver.entity->GetRotation());
            const Vector3 forward = horizontal(driver.entity->GetForward());
            driver.physics->SetLinearVelocity(forward * driver.spline_speed);
            driver.physics->SetAngularVelocity(Vector3::Zero);
            driver.last_position = driver.entity->GetPosition();
            driver.previous_speed = driver.spline_speed;
            driver.throttle = 0.0f;
            driver.brake = 0.0f;
            driver.transition_time = 0.5f;
            driver.diagnostic_time = 3.0f;
            driver.diagnostic_interval = 0.25f;
            driver.stalled_time = 0.0f;
            driver.blocked_time = 0.0f;
            driver.recovery = RecoveryState::None;
            driver.recovery_time = 0.0f;
            driver.decision_time = decision_interval;
            SP_LOG_INFO("traffic_ai %s physics_on speed=%.2f", driver.entity->GetObjectName().c_str(), driver.spline_speed);
        }
        driver.physics_active = active;
    }

    bool Traffic::CreateSpline(Driver& driver)
    {
        Trajectory best;
        for (float steering : steering_samples)
        {
            const Trajectory candidate = EvaluateTrajectory(driver, steering);
            if (candidate.traversable && candidate.score > best.score)
            {
                best = candidate;
            }
        }
        if (!best.traversable || best.point_count < 2)
        {
            driver.spline_duration = 0.0f;
            return false;
        }

        const Vector3 forward = horizontal(driver.entity->GetForward());
        const Vector3 direction = horizontal(best.points[best.point_count - 1] - best.points[best.point_count - 2]);
        const float length = (best.points[best.point_count - 1] - best.points[0]).Length();
        if (!std::isfinite(length) || length < 1.0f || forward.LengthSquared() < 0.5f || direction.LengthSquared() < 0.5f)
        {
            driver.spline_duration = 0.0f;
            return false;
        }
        driver.spline_start = driver.entity->GetPosition();
        driver.spline_end = best.points[best.point_count - 1];
        driver.spline_control_a = driver.spline_start + forward * (length * 0.35f);
        driver.spline_control_b = driver.spline_end - direction * (length * 0.35f);
        Vector3 previous = driver.spline_start;
        float spline_length = 0.0f;
        for (uint32_t i = 1; i <= 8; i++)
        {
            const float t = static_cast<float>(i) / 8.0f;
            const float inverse_t = 1.0f - t;
            Vector3 point = driver.spline_start * (inverse_t * inverse_t * inverse_t) + driver.spline_control_a * (3.0f * inverse_t * inverse_t * t) + driver.spline_control_b * (3.0f * inverse_t * t * t) + driver.spline_end * (t * t * t);
            Vector3 ground;
            if (!SampleGround(point, ground))
            {
                driver.spline_duration = 0.0f;
                return false;
            }
            point.y = ground.y + 1.0f;
            const Vector3 segment = point - previous;
            const float distance = segment.Length();
            Vector3 hit_position;
            float hit_distance = distance;
            Entity* hit_entity = nullptr;
            if (distance > 0.001f && PhysicsWorld::SphereCast(previous + Vector3::Up * sensor_height, segment, driver.limits.half_width * 0.75f, distance, driver.collision_group, hit_position, hit_distance, hit_entity))
            {
                driver.spline_duration = 0.0f;
                return false;
            }
            if (!IsTrafficCorridorClear(driver, previous, point, driver.limits.half_width + 0.8f))
            {
                driver.spline_duration = 0.0f;
                return false;
            }
            spline_length += distance;
            previous = point;
        }
        driver.spline_speed = std::clamp(best.target_speed, 2.0f, driver.cruise_speed);
        driver.spline_duration = spline_length / driver.spline_speed;
        driver.spline_time = 0.0f;
        driver.steering = best.steering;
        return true;
    }

    void Traffic::UpdateSplineDriver(Driver& driver, float delta_time)
    {
        if (driver.spline_duration <= 0.0f || driver.spline_time >= driver.spline_duration)
        {
            if (!CreateSpline(driver))
            {
                return;
            }
        }

        const float next_time = std::min(driver.spline_time + delta_time, driver.spline_duration);
        const float t = driver.spline_duration > 0.0f ? next_time / driver.spline_duration : 1.0f;
        const float inverse_t = 1.0f - t;
        Vector3 position = driver.spline_start * (inverse_t * inverse_t * inverse_t) + driver.spline_control_a * (3.0f * inverse_t * inverse_t * t) + driver.spline_control_b * (3.0f * inverse_t * t * t) + driver.spline_end * (t * t * t);
        Vector3 ground;
        if (!SampleGround(position, ground))
        {
            driver.spline_duration = 0.0f;
            return;
        }
        position.y = ground.y + 1.0f;

        const Vector3 start = driver.entity->GetPosition();
        Vector3 movement = position - start;
        const float distance = movement.Length();
        Vector3 hit_position;
        float hit_distance = distance;
        Entity* hit_entity = nullptr;
        if (distance > 0.001f && PhysicsWorld::SphereCast(start + Vector3::Up * sensor_height, movement, driver.limits.half_width * 0.75f, distance, driver.collision_group, hit_position, hit_distance, hit_entity))
        {
            driver.spline_duration = 0.0f;
            return;
        }
        if (!IsTrafficCorridorClear(driver, start, position, driver.limits.half_width + 0.8f))
        {
            driver.spline_duration = 0.0f;
            return;
        }

        Vector3 direction = (driver.spline_control_a - driver.spline_start) * (3.0f * inverse_t * inverse_t) + (driver.spline_control_b - driver.spline_control_a) * (6.0f * inverse_t * t) + (driver.spline_end - driver.spline_control_b) * (3.0f * t * t);
        direction = horizontal(direction);
        if (direction.LengthSquared() > 0.5f)
        {
            driver.entity->SetRotation(Quaternion::FromLookRotation(direction));
        }
        driver.entity->SetPosition(position);
        driver.spline_time = next_time;
    }

    void Traffic::PlanDriver(Driver& driver, float delta_time)
    {
        const Vector3 position = driver.entity->GetPosition();
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

        Vector3 progress_offset = position - driver.last_position;
        progress_offset.y = 0.0f;
        const float progress = progress_offset.Length();
        driver.last_position = position;
        driver.stalled_time = driver.telemetry.speed < 0.8f && progress < 0.1f ? driver.stalled_time + delta_time : 0.0f;

        Trajectory best;
        for (float steering : steering_samples)
        {
            const Trajectory candidate = EvaluateTrajectory(driver, steering);
            if (candidate.score > best.score)
            {
                best = candidate;
            }
        }
        driver.plan = best;
        driver.plan_initialized = true;
        driver.blocked_time = best.traversable ? 0.0f : driver.blocked_time + delta_time;
        if (driver.recovery == RecoveryState::None && (driver.blocked_time > 0.7f || driver.stalled_time > 3.5f))
        {
            driver.recovery = RecoveryState::Brake;
            driver.recovery_time = 0.0f;
            driver.recovery_origin = position;
        }
    }

    void Traffic::ControlDriver(Driver& driver, float delta_time)
    {
        if (driver.recovery != RecoveryState::None)
        {
            UpdateRecovery(driver, delta_time);
            return;
        }
        if (!driver.plan_initialized)
        {
            driver.steering = 0.0f;
            driver.throttle = 0.0f;
            driver.brake = 0.0f;
            driver.car->SetThrottle(0.0f);
            driver.car->SetBrake(0.0f);
            driver.car->SetSteering(0.0f);
            driver.car->SetHandbrake(0.0f);
            return;
        }

        const bool transitioning = driver.transition_time > 0.0f;
        driver.transition_time = std::max(driver.transition_time - delta_time, 0.0f);
        const float speed = driver.telemetry.speed;
        float target_speed = driver.plan.target_speed;
        const float crawl_clearance = driver.limits.half_width * 2.0f + 1.5f;
        if (!driver.plan.traversable && driver.plan.clearance > crawl_clearance)
        {
            target_speed = std::max(target_speed, 3.0f);
        }
        if (transitioning)
        {
            target_speed = std::max(target_speed, std::min(driver.spline_speed, driver.cruise_speed));
        }
        const float available_grip = driver.limits.lateral_acceleration * driver.telemetry.grip * driver.telemetry.load_factor * std::clamp(driver.telemetry.grounded_ratio, 0.3f, 1.0f);
        const float lateral_use = speed * speed * fabsf(driver.plan.curvature);
        const float longitudinal_budget = sqrtf(std::max(1.0f - std::min(lateral_use / std::max(available_grip, 0.1f), 1.0f), 0.0f));
        const float speed_error = target_speed - speed;
        const float drive_acceleration = std::max(driver.telemetry.drive_acceleration, 0.5f);
        const float available_braking = std::max(driver.limits.braking * driver.telemetry.grip * driver.telemetry.load_factor * std::clamp(driver.telemetry.grounded_ratio, 0.3f, 1.0f), 1.0f);
        const float desired_acceleration = std::clamp(speed_error * 0.8f, -available_braking, drive_acceleration);
        const float actuator_acceleration = desired_acceleration + driver.limits.drag_factor * speed * speed;
        float throttle_target = actuator_acceleration > 0.0f ? actuator_acceleration / drive_acceleration : 0.0f;
        float brake_target = actuator_acceleration < 0.0f ? -actuator_acceleration / available_braking : 0.0f;
        throttle_target *= longitudinal_budget;
        if (driver.telemetry.tc_active || driver.telemetry.slip > 0.35f)
        {
            throttle_target *= std::clamp(1.0f - driver.telemetry.slip, 0.15f, 0.7f);
        }
        if (driver.telemetry.abs_active)
        {
            brake_target *= 0.85f;
        }
        if (!transitioning && driver.plan.blocked && driver.plan.clearance > 0.1f)
        {
            const float obstacle_speed = driver.plan.point_count > 0 ? driver.plan.speed_profile[driver.plan.point_count - 1] : 0.0f;
            const float required_deceleration = std::max(speed * speed - obstacle_speed * obstacle_speed, 0.0f) / (2.0f * driver.plan.clearance);
            if (required_deceleration > available_braking)
            {
                throttle_target = 0.0f;
                brake_target = 1.0f;
            }
        }

        const Vector3 forward = horizontal(driver.entity->GetForward());
        for (const Driver& other : m_drivers)
        {
            if (&other == &driver || !other.entity || !other.physics)
            {
                continue;
            }
            Vector3 relative = other.entity->GetPosition() - driver.entity->GetPosition();
            relative.y = 0.0f;
            const float longitudinal = Vector3::Dot(relative, forward);
            const float lateral = fabsf(Vector3::Dot(relative, horizontal(driver.entity->GetRight())));
            const float rear_closing = Vector3::Dot(other.physics->GetLinearVelocity() - driver.telemetry.velocity, forward);
            if (longitudinal < -2.0f && longitudinal > -12.0f && lateral < driver.limits.half_width * 2.5f && rear_closing > 2.0f && driver.plan.traversable)
            {
                brake_target = 0.0f;
                throttle_target = std::max(throttle_target, std::clamp(rear_closing / 8.0f, 0.25f, 0.75f));
            }
        }

        Vector3 velocity_direction = horizontal(driver.telemetry.velocity);
        float stability_correction = 0.0f;
        if (speed > 2.0f && velocity_direction.LengthSquared() > 0.5f)
        {
            const float velocity_angle = signed_angle(forward, velocity_direction);
            stability_correction = std::clamp(-velocity_angle * 0.65f - driver.telemetry.lateral_speed / std::max(speed, 1.0f) * 0.35f, -0.35f, 0.35f);
        }
        const float steering_target = std::clamp(driver.plan.steering + stability_correction, -1.0f, 1.0f);
        const float steering_rate = std::clamp(std::max(4.5f - speed * 0.08f, 0.5f) * delta_time, 0.0f, 0.45f);
        driver.steering += std::clamp(steering_target - driver.steering, -steering_rate, steering_rate);
        const float pedal_blend = 1.0f - expf(-delta_time * 7.0f);
        driver.throttle += (std::clamp(throttle_target, 0.0f, 1.0f) - driver.throttle) * pedal_blend;
        driver.brake += (std::clamp(brake_target, 0.0f, 1.0f) - driver.brake) * pedal_blend;

        driver.car->SetSteering(driver.steering);
        driver.car->SetThrottle(driver.throttle);
        driver.car->SetBrake(driver.brake);
        driver.car->SetHandbrake(0.0f);
    }

    void Traffic::UpdateRecovery(Driver& driver, float delta_time)
    {
        driver.recovery_time += delta_time;
        const float speed = driver.telemetry.speed;
        if (driver.recovery == RecoveryState::Brake)
        {
            driver.throttle = 0.0f;
            driver.brake = 1.0f;
            driver.car->SetThrottle(driver.throttle);
            driver.car->SetBrake(driver.brake);
            driver.car->SetSteering(0.0f);
            driver.car->SetHandbrake(0.0f);
            if (speed < 0.5f || driver.recovery_time > 1.2f)
            {
                Trajectory best_reverse;
                for (float steering : steering_samples)
                {
                    const Trajectory candidate = EvaluateTrajectory(driver, steering, true);
                    if (candidate.score > best_reverse.score)
                    {
                        best_reverse = candidate;
                    }
                }
                driver.recovery_steering = best_reverse.traversable ? best_reverse.steering : (driver.entity->GetObjectId() % 2 == 0 ? -1.0f : 1.0f);
                driver.recovery_heading = -driver.recovery_steering;
                driver.recovery = RecoveryState::ReverseEscape;
                driver.recovery_time = 0.0f;
            }
            return;
        }

        if (driver.recovery == RecoveryState::ReverseEscape)
        {
            const Trajectory reverse = EvaluateTrajectory(driver, driver.recovery_steering, true);
            driver.throttle = 0.0f;
            driver.brake = reverse.traversable ? (driver.physics->GetCurrentGear() == 0 ? 0.75f : 1.0f) : 0.0f;
            driver.car->SetThrottle(driver.throttle);
            driver.car->SetBrake(driver.brake);
            driver.car->SetSteering(-driver.recovery_steering);
            driver.car->SetHandbrake(reverse.traversable ? 0.0f : 1.0f);
            Vector3 escaped_offset = driver.entity->GetPosition() - driver.recovery_origin;
            escaped_offset.y = 0.0f;
            const float escaped_distance = escaped_offset.Length();
            if (!reverse.traversable || escaped_distance > 5.0f || driver.recovery_time > 2.8f)
            {
                driver.recovery = RecoveryState::ForwardRealign;
                driver.recovery_time = 0.0f;
            }
            return;
        }

        if (driver.recovery == RecoveryState::ForwardRealign)
        {
            const Trajectory forward = EvaluateTrajectory(driver, driver.recovery_heading);
            driver.throttle = forward.traversable ? 0.35f : 0.0f;
            driver.brake = forward.traversable ? 0.0f : 0.8f;
            driver.car->SetThrottle(driver.throttle);
            driver.car->SetBrake(driver.brake);
            driver.car->SetSteering(driver.recovery_heading);
            driver.car->SetHandbrake(0.0f);
            if ((forward.traversable && speed > 2.0f) || driver.recovery_time > 2.0f)
            {
                driver.recovery = RecoveryState::Cooldown;
                driver.recovery_time = 0.0f;
                driver.blocked_time = 0.0f;
                driver.stalled_time = 0.0f;
                driver.decision_time = decision_interval;
            }
            return;
        }

        driver.car->SetHandbrake(0.0f);
        if (driver.recovery_time > 1.5f)
        {
            driver.recovery = RecoveryState::None;
            driver.recovery_time = 0.0f;
        }
        else
        {
            const float blend = driver.recovery_time / 1.5f;
            driver.throttle = 0.25f * blend;
            driver.brake = 0.0f;
            driver.car->SetThrottle(driver.throttle);
            driver.car->SetBrake(driver.brake);
            driver.car->SetSteering(driver.steering * blend);
        }
    }

    Traffic::Trajectory Traffic::EvaluateTrajectory(const Driver& driver, float steering, bool reverse) const
    {
        Trajectory result;
        result.steering = steering;
        const Vector3 origin = driver.entity->GetPosition();
        const Vector3 forward = horizontal(driver.entity->GetForward()) * (reverse ? -1.0f : 1.0f);
        const Vector3 right = horizontal(driver.entity->GetRight());
        const float speed = driver.physics_active ? driver.telemetry.speed : driver.spline_speed;
        const float preview_length = reverse ? 7.0f : std::clamp(12.0f + speed * 1.1f, 16.0f, 48.0f);
        const float segment_length = preview_length < 22.0f ? 2.0f : 3.0f;
        const uint32_t point_count = std::min(static_cast<uint32_t>(ceilf(preview_length / segment_length)) + 1u, static_cast<uint32_t>(result.points.size()));
        const float steer_angle = steering * driver.limits.max_steer_angle;
        const float curvature = tanf(steer_angle) / driver.limits.wheelbase * (reverse ? -1.0f : 1.0f);
        result.curvature = curvature;
        result.point_count = point_count;
        result.points[0] = origin;
        result.clearance = preview_length;
        uint32_t valid_count = 1;

        Vector3 previous_ground;
        if (!SampleGround(origin, previous_ground))
        {
            return result;
        }

        float heading = 0.0f;
        float slope_penalty = 0.0f;
        float traffic_speed_limit = driver.cruise_speed;
        bool blocked = false;
        bool traffic_blocked = false;
        Vector3 player_position;
        Vector3 player_velocity;
        const bool has_player = GetPlayerState(player_position, player_velocity);
        for (uint32_t i = 1; i < point_count; i++)
        {
            const float step = std::min(segment_length, preview_length - segment_length * static_cast<float>(i - 1));
            const float next_heading = heading + curvature * step;
            const float segment_heading = (heading + next_heading) * 0.5f;
            const Vector3 direction = (forward * cosf(segment_heading) + right * sinf(segment_heading)).Normalized();
            heading = next_heading;
            Vector3 sample = result.points[i - 1] + direction * step;
            if (!IsInsideBounds(sample, driver.limits.half_width + 2.0f))
            {
                blocked = true;
                result.clearance = segment_length * static_cast<float>(i - 1);
                break;
            }

            Vector3 ground;
            if (!SampleGround(sample, ground))
            {
                blocked = true;
                result.clearance = segment_length * static_cast<float>(i - 1);
                break;
            }
            const float rise = fabsf(ground.y - previous_ground.y);
            const Vector3 lateral_direction = Vector3::Cross(Vector3::Up, direction);
            const float edge_offset = driver.limits.half_width * 0.85f;
            Vector3 left_ground;
            Vector3 right_ground;
            if (!SampleGround(sample - lateral_direction * edge_offset, left_ground) || !SampleGround(sample + lateral_direction * edge_offset, right_ground))
            {
                blocked = true;
                result.clearance = segment_length * static_cast<float>(i - 1);
                break;
            }
            const float cross_rise = std::max(fabsf(left_ground.y - ground.y), fabsf(right_ground.y - ground.y));
            if (rise > step * 0.45f || cross_rise > std::max(0.65f, edge_offset * 0.6f))
            {
                blocked = true;
                result.clearance = segment_length * static_cast<float>(i - 1);
                break;
            }
            sample.y = ground.y + 1.0f;
            result.points[i] = sample;
            valid_count = i + 1;

            Vector3 hit_position;
            float hit_distance = step;
            Entity* hit_entity = nullptr;
            const Vector3 cast_start = result.points[i - 1] + Vector3::Up * sensor_height;
            if (PhysicsWorld::SphereCast(cast_start, sample - result.points[i - 1], driver.limits.half_width * 0.8f, step, driver.collision_group, hit_position, hit_distance, hit_entity))
            {
                blocked = true;
                result.clearance = segment_length * static_cast<float>(i - 1) + hit_distance;
                result.points[i] = result.points[i - 1] + (sample - result.points[i - 1]).Normalized() * hit_distance;
                break;
            }

            const float sample_time = segment_length * static_cast<float>(i) / std::max(speed, 3.0f);
            for (const Driver& other : m_drivers)
            {
                if (&other == &driver || !other.entity)
                {
                    continue;
                }
                Vector3 other_velocity = other.physics_active && other.physics ? planar(other.physics->GetLinearVelocity()) : horizontal(other.entity->GetForward()) * other.spline_speed;
                Vector3 predicted_other = other.entity->GetPosition() + other_velocity * sample_time;
                Vector3 separation = predicted_other - sample;
                separation.y = 0.0f;
                const float safe_radius = driver.limits.half_width + other.limits.half_width + 1.0f;
                if (separation.LengthSquared() < safe_radius * safe_radius)
                {
                    const bool deterministic_yield = driver.entity->GetObjectId() > other.entity->GetObjectId();
                    const Vector3 candidate_velocity = direction * speed;
                    const float relative_closing = Vector3::Dot(candidate_velocity - other_velocity, direction);
                    if (deterministic_yield || relative_closing > 0.0f || separation.LengthSquared() < safe_radius * safe_radius * 0.55f)
                    {
                        blocked = true;
                        traffic_blocked = true;
                        result.clearance = segment_length * static_cast<float>(i);
                        traffic_speed_limit = std::max(other_velocity.Length() - 1.0f, 0.0f);
                        break;
                    }
                }
            }
            if (blocked)
            {
                break;
            }

            if (has_player)
            {
                const Vector3 predicted_player = player_position + player_velocity * sample_time;
                Vector3 player_separation = predicted_player - sample;
                player_separation.y = 0.0f;
                const float player_safe_radius = driver.limits.half_width + 2.7f;
                if (player_separation.LengthSquared() < player_safe_radius * player_safe_radius)
                {
                    blocked = true;
                    traffic_blocked = true;
                    result.clearance = segment_length * static_cast<float>(i);
                    traffic_speed_limit = std::max(player_velocity.Length() - 2.0f, 0.0f);
                    break;
                }
            }

            slope_penalty += rise / std::max(step, 0.1f) + cross_rise / std::max(edge_offset, 0.1f) * 0.5f;
            previous_ground = ground;
        }

        result.point_count = valid_count;
        result.blocked = blocked;
        result.traversable = result.clearance > (reverse ? 2.5f : std::max(5.0f, speed * 0.35f)) * driver.caution;
        const float telemetry_grip = driver.telemetry.grip * driver.telemetry.load_factor * std::clamp(driver.telemetry.grounded_ratio, 0.3f, 1.0f);
        const float grip = driver.limits.lateral_acceleration * (driver.physics_active ? telemetry_grip : 1.0f);
        const float corner_speed = fabsf(curvature) > 0.0001f ? sqrtf(std::max(grip / fabsf(curvature), 0.0f)) : driver.cruise_speed;
        const float braking = std::max(driver.limits.braking * (driver.physics_active ? telemetry_grip : 1.0f), 1.0f);
        result.stopping_distance = speed * speed / (2.0f * braking) + speed * 0.35f;

        for (uint32_t i = 0; i < valid_count; i++)
        {
            result.speed_profile[i] = std::min({ driver.cruise_speed, corner_speed, traffic_speed_limit });
        }
        if (blocked)
        {
            result.speed_profile[valid_count - 1] = traffic_blocked ? traffic_speed_limit : 0.0f;
        }
        for (uint32_t i = valid_count - 1; i > 0; i--)
        {
            const float distance = (result.points[i] - result.points[i - 1]).Length();
            result.speed_profile[i - 1] = std::min(result.speed_profile[i - 1], sqrtf(result.speed_profile[i] * result.speed_profile[i] + 2.0f * braking * distance));
        }
        result.speed_profile[0] = std::min(result.speed_profile[0], speed);
        for (uint32_t i = 1; i < valid_count; i++)
        {
            const float distance = (result.points[i] - result.points[i - 1]).Length();
            const float acceleration = driver.physics_active ? driver.telemetry.drive_acceleration : driver.limits.acceleration;
            result.speed_profile[i] = std::min(result.speed_profile[i], sqrtf(result.speed_profile[i - 1] * result.speed_profile[i - 1] + 2.0f * acceleration * distance));
        }
        result.target_speed = valid_count > 1 ? result.speed_profile[1] : 0.0f;

        const uint32_t last_valid = valid_count - 1;
        const uint32_t middle_valid = last_valid / 2;
        Vector3 middle_direction = middle_valid > 0 ? horizontal(result.points[middle_valid] - result.points[middle_valid - 1]) : forward;
        Vector3 final_direction = last_valid > 0 ? horizontal(result.points[last_valid] - result.points[last_valid - 1]) : forward;
        float middle_road_distance = 1000.0f;
        float final_road_distance = 1000.0f;
        const float middle_road_score = GetRoadScore(result.points[middle_valid], middle_direction, middle_road_distance);
        const float final_road_score = GetRoadScore(result.points[last_valid], final_direction, final_road_distance);
        const float road_score = middle_road_score * 0.4f + final_road_score * 0.6f;
        const float road_distance = std::min(middle_road_distance, final_road_distance);
        const float novelty = GetNovelty(driver, result.points[last_valid]);
        const float clearance_score = std::min(result.clearance / preview_length, 1.0f) * 8.0f;
        const float smoothness = 1.0f - std::min(fabsf(steering - driver.steering), 1.0f);
        const float progress_score = Vector3::Dot(result.points[last_valid] - origin, forward) / std::max(preview_length, 1.0f);
        const float road_weight = road_distance < 8.0f ? 7.0f : (road_distance < 24.0f ? 3.0f : 0.4f);
        result.score = clearance_score + novelty * driver.exploration * 3.0f + smoothness * driver.persistence * 0.6f + progress_score * 1.5f + road_score * road_weight - slope_penalty * driver.caution;
        if (!result.traversable)
        {
            result.score -= 12.0f;
        }
        return result;
    }

    float Traffic::GetRoadScore(const Vector3& position, const Vector3& direction, float& road_distance) const
    {
        float score = 0.0f;
        road_distance = 1000.0f;
        for (const RoadSample& road : m_road_samples)
        {
            Vector3 offset = road.position - position;
            offset.y = 0.0f;
            const float distance = offset.Length();
            if (distance >= road_distance)
            {
                continue;
            }
            road_distance = distance;
            const float alignment = fabsf(Vector3::Dot(direction, road.tangent));
            const float proximity = 1.0f - std::clamp(distance / std::max(road.half_width * 2.0f, 1.0f), 0.0f, 1.0f);
            score = alignment * 0.65f + proximity * 0.35f;
        }
        return score;
    }

    bool Traffic::IsTrafficCorridorClear(const Driver& driver, const Vector3& start, const Vector3& end, float radius) const
    {
        const Vector3 segment = end - start;
        const float length_squared = segment.LengthSquared();
        Vector3 player_position;
        Vector3 player_velocity;
        if (GetPlayerState(player_position, player_velocity))
        {
            const float t = length_squared > 0.0001f ? std::clamp(Vector3::Dot(player_position - start, segment) / length_squared, 0.0f, 1.0f) : 0.0f;
            Vector3 separation = player_position - (start + segment * t);
            separation.y = 0.0f;
            const float player_radius = radius + 2.7f;
            if (separation.LengthSquared() < player_radius * player_radius)
            {
                return false;
            }
        }
        for (const Driver& other : m_drivers)
        {
            if (&other == &driver || !other.entity)
            {
                continue;
            }
            const Vector3 relative = other.entity->GetPosition() - start;
            const float t = length_squared > 0.0001f ? std::clamp(Vector3::Dot(relative, segment) / length_squared, 0.0f, 1.0f) : 0.0f;
            Vector3 separation = other.entity->GetPosition() - (start + segment * t);
            separation.y = 0.0f;
            const float combined_radius = radius + other.limits.half_width;
            if (separation.LengthSquared() < combined_radius * combined_radius)
            {
                return false;
            }
        }
        return true;
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
            Vector3 direction;
            Vector3 candidate;
            Vector3 ground;
            if (!m_road_samples.empty() && attempt < 384)
            {
                const RoadSample& road = m_road_samples[(index * 37u + attempt * 17u) % m_road_samples.size()];
                direction = attempt % 2 == 0 ? road.tangent : -road.tangent;
                const Vector3 road_right = Vector3::Cross(Vector3::Up, road.tangent);
                const float lane_offset = road.half_width > 3.0f ? std::min(road.half_width * 0.35f, 2.5f) : 0.0f;
                candidate = road.position + road_right * (attempt % 4 < 2 ? lane_offset : -lane_offset);
                if (!SampleGround(candidate, ground))
                {
                    continue;
                }
            }
            else
            {
                const float x = m_bounds_min.x + (m_bounds_max.x - m_bounds_min.x) * random_unit();
                const float z = m_bounds_min.z + (m_bounds_max.z - m_bounds_min.z) * random_unit();
                if (!SampleGround(Vector3(x, m_bounds_max.y, z), ground))
                {
                    continue;
                }
                const float yaw = random_unit() * pi * 2.0f;
                direction = Vector3(sinf(yaw), 0.0f, cosf(yaw));
                candidate = ground;
            }
            candidate.y = ground.y + 1.0f;
            if (!IsInsideBounds(candidate, 4.0f))
            {
                continue;
            }

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
            for (const Driver& driver : m_drivers)
            {
                if (driver.entity && (driver.entity->GetPosition() - candidate).LengthSquared() < spawn_separation * spawn_separation)
                {
                    separated = false;
                    break;
                }
            }
            if (!separated)
            {
                continue;
            }

            const Vector3 right(direction.z, 0.0f, -direction.x);
            bool clear = true;
            for (const Vector3& probe_direction : { direction, -direction, right, -right })
            {
                Vector3 probe_ground;
                if (!SampleGround(candidate + probe_direction * 2.5f, probe_ground) || fabsf(probe_ground.y - ground.y) > 1.0f)
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
