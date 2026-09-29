/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

#include "pch.h"
#include "RaceDriver.h"
#include "Physics.h"
#include "../Entity.h"
#include "../World.h"
#include "../../car/Car.h"
#include "../../car/CarPresets.h"
#include "../../car/RacingLine.h"
#include "../../core/Engine.h"
#include "../../core/ThreadPool.h"
#include "../../core/Timer.h"
#include "../../file_system/FileSystem.h"
#include "../../geometry/Mesh.h"
#include "../../profiling/Profiler.h"
#include "../../resource/ResourceCache.h"
#include "../../io/pugixml.hpp"

using namespace std;
using namespace spartan::math;

#define SP_REGISTER_DRIVER_STAT(name, type) RegisterAttribute("m_" #name, #type, \
    [this]() { return m_stats.name; },                                           \
    [this](const std::any& value) { m_stats.name = std::any_cast<type>(value); })

namespace spartan
{
    namespace
    {
        constexpr float spawn_retry_delay   = 5.0f;
        constexpr float watch_window        = 30.0f;
        constexpr float watch_min_distance  = 150.0f; // meters, a slow car still covers several times this
        constexpr uint32_t watch_max_resets = 4;

        bool car_alive(Car* car)
        {
            if (!car)
            {
                return false;
            }
            const vector<Car*> cars = Car::GetAll();
            return find(cars.begin(), cars.end(), car) != cars.end();
        }
    }

    RaceDriver::RaceDriver(Entity* entity) : Component(entity)
    {
        SP_REGISTER_ATTRIBUTE_VALUE_VALUE(m_track_entity_id, uint64_t);
        SP_REGISTER_ATTRIBUTE_VALUE_VALUE(m_car_file, string);
        SP_REGISTER_ATTRIBUTE_VALUE_VALUE(m_skill, float);
        SP_REGISTER_ATTRIBUTE_VALUE_VALUE(m_max_speed, float);
        SP_REGISTER_ATTRIBUTE_VALUE_VALUE(m_edge_margin, float);
        SP_REGISTER_ATTRIBUTE_VALUE_VALUE(m_start_distance, float);
        SP_REGISTER_ATTRIBUTE_VALUE_VALUE(m_start_delay, float);
        SP_REGISTER_ATTRIBUTE_VALUE_VALUE(m_learning, bool);
        SP_REGISTER_ATTRIBUTE_VALUE_VALUE(m_verbose, bool);
        SP_REGISTER_ATTRIBUTE_VALUE_VALUE(m_track_length, float);
        SP_REGISTER_ATTRIBUTE_VALUE_VALUE(m_car_spawns, uint32_t);
        SP_REGISTER_DRIVER_STAT(lap, uint32_t);
        SP_REGISTER_DRIVER_STAT(resets, uint32_t);
        SP_REGISTER_DRIVER_STAT(lap_time, float);
        SP_REGISTER_DRIVER_STAT(last_lap_time, float);
        SP_REGISTER_DRIVER_STAT(best_lap_time, float);
        SP_REGISTER_DRIVER_STAT(speed_kmh, float);
        SP_REGISTER_DRIVER_STAT(target_kmh, float);
        SP_REGISTER_DRIVER_STAT(line_error, float);
        SP_REGISTER_DRIVER_STAT(travelled, float);
        SP_REGISTER_DRIVER_STAT(ideal_lap, float);
        SP_REGISTER_DRIVER_STAT(cornering_g, float);
        SP_REGISTER_DRIVER_STAT(braking_g, float);
        SP_REGISTER_DRIVER_STAT(front_use, float);
        SP_REGISTER_DRIVER_STAT(rear_use, float);
        SP_REGISTER_DRIVER_STAT(body_slip, float);
    }

    RaceDriver::~RaceDriver()
    {
        // entities are deleted under the world entity lock, so skip Stop's track lookup
        if (m_preload)
        {
            m_preload->cancelled.store(true, memory_order_release);
            m_preload.reset();
        }
        DestroyCar();
    }

    AiDriverSettings RaceDriver::GetDriverSettings() const
    {
        AiDriverSettings settings;
        settings.skill     = m_skill;
        settings.max_speed = m_max_speed;
        settings.learning  = m_learning;
        settings.verbose   = m_verbose;
        return settings;
    }

    void RaceDriver::Start()
    {
        DestroyCar();
        m_stats       = AiDriverStats();
        m_car_spawns  = 0;
        m_watch_time  = 0.0f;
        m_spawn_delay = 0.0f;

        Entity* track  = m_track_entity_id != 0 ? World::GetEntityById(m_track_entity_id) : GetEntity();
        m_line         = RacingLine::Build(track, m_edge_margin);
        m_track_length = m_line ? m_line->GetLength() : 0.0f;
        if (m_line)
        {
            m_line->KeepCollisionLoaded();
            BeginPreload();
        }
    }

    void RaceDriver::Stop()
    {
        if (m_preload)
        {
            // never wait on the main thread, the shared state keeps the worker safe until it finishes
            m_preload->cancelled.store(true, memory_order_release);
            m_preload.reset();
        }
        DestroyCar();
        if (m_line)
        {
            m_line->RestoreCollisionStreaming();
        }
    }

    void RaceDriver::Tick()
    {
        SP_PROFILE_CPU();
        if (!Engine::IsFlagSet(EngineMode::Playing) || !m_line)
        {
            return;
        }

        if (m_car && !car_alive(m_car))
        {
            SP_LOG_WARNING("race_driver: the race car is gone, spawning a new one");
            m_car = nullptr;
        }

        if (!m_car && m_preload && m_preload->completed.load(memory_order_acquire))
        {
            const bool succeeded = m_preload->succeeded.load(memory_order_acquire);
            m_preload.reset();
            if (!succeeded || !SpawnCar())
            {
                SP_LOG_ERROR("race_driver: failed to spawn car %s, retrying in %.0f s", m_car_path.c_str(), spawn_retry_delay);
                m_spawn_delay = spawn_retry_delay;
            }
        }
        if (!m_car && !m_preload && !Engine::IsFlagSet(EngineMode::Paused))
        {
            m_spawn_delay -= static_cast<float>(Timer::GetDeltaTimeSec());
            if (m_spawn_delay <= 0.0f)
            {
                BeginPreload();
            }
        }

        if (m_car && !Engine::IsFlagSet(EngineMode::Paused))
        {
            Watch(static_cast<float>(Timer::GetDeltaTimeSec()));
        }
    }

    void RaceDriver::Watch(float delta_time)
    {
        // someone took the driver away, the race car always has one
        if (!m_car->GetAiDriver())
        {
            AiDriverSettings settings = GetDriverSettings();
            m_car->SetAiDriver(make_unique<AiDriver>(m_line, settings));
            m_watch_time = 0.0f;
            SP_LOG_WARNING("race_driver: the race car lost its driver, a new one took over");
        }
        if (m_car->GetAiDriver() != m_watch_driver)
        {
            m_watch_driver = m_car->GetAiDriver();
            m_watch_time   = 0.0f;
        }
        m_stats = m_car->GetAiDriver()->GetStats();

        // the driver puts a crashed car back on the line itself, a car that keeps needing it or does not move is broken (suspension, physics blow up)
        Entity* vehicle      = m_car->GetRootEntity();
        const Vector3 where  = vehicle ? vehicle->GetPosition() : Vector3::Zero;
        const bool finite    = isfinite(where.x) && isfinite(where.y) && isfinite(where.z);
        const float progress = m_stats.travelled;
        if (m_watch_time == 0.0f)
        {
            m_watch_progress = progress;
            m_watch_resets   = m_stats.resets;
        }
        m_watch_time += delta_time;

        bool broken = !vehicle || !finite;
        if (broken)
        {
            SP_LOG_WARNING("race_driver: replacing the race car, its physics state is not finite");
        }
        else if (m_watch_time >= watch_window)
        {
            const float driven    = progress - m_watch_progress;
            const uint32_t resets = m_stats.resets - m_watch_resets;
            broken                = broken || driven < watch_min_distance || resets >= watch_max_resets;
            if (broken)
            {
                SP_LOG_WARNING("race_driver: replacing the race car, %.0f m and %u resets in the last %.0f s", driven, resets, watch_window);
            }
            m_watch_time = 0.0f;
        }
        if (broken)
        {
            DestroyCar();
            m_watch_time = 0.0f;
        }
    }

    void RaceDriver::BeginPreload()
    {
        m_car_path = m_car_file;
        if (!World::GetFilePath().empty())
        {
            const string relative_path = FileSystem::GetDirectoryFromFilePath(World::GetFilePath()) + m_car_file;
            if (FileSystem::Exists(relative_path))
            {
                m_car_path = relative_path;
            }
        }

        // load the car meshes on a worker so the launch does not hitch the first play frame
        m_preload = make_shared<PreloadState>();
        const shared_ptr<PreloadState> state = m_preload;
        const string car_path = m_car_path;
        ThreadPool::AddTask([state, car_path]()
        {
            bool succeeded = false;
            if (!state->cancelled.load(memory_order_acquire))
            {
                if (const car::car_definition* definition = car::load_car_file(car_path))
                {
                    car::load_car_directory(FileSystem::GetDirectoryFromFilePath(car_path));
                    const uint32_t mesh_flags = Mesh::GetDefaultFlags() & ~static_cast<uint32_t>(MeshFlags::PostProcessOptimize);
                    const bool body_loaded    = definition->body_model.empty() || ResourceCache::Load<Mesh>(definition->body_model, mesh_flags) != nullptr;
                    const bool wheel_loaded   = definition->wheel_model.empty() || ResourceCache::Load<Mesh>(definition->wheel_model, mesh_flags) != nullptr;
                    succeeded                 = body_loaded && wheel_loaded;
                }
            }
            state->succeeded.store(succeeded, memory_order_release);
            state->completed.store(true, memory_order_release);
        });
    }

    bool RaceDriver::SpawnCar()
    {
        float fraction                 = 0.0f;
        const size_t grid              = m_line->IndexAt(m_start_distance, fraction);
        const RacingLine::Point& point = m_line->GetPoint(grid);

        Car::Config config;
        config.position            = point.position + Vector3(0.0f, 1.0f, 0.0f);
        config.car_file            = m_car_path;
        config.drivable            = true;
        config.vehicle_sim_mode    = VehicleSimMode::Full;
        config.customize_materials = true;
        config.paint_preset        = MaterialPaintPreset::GlossSolid;
        config.paint_color         = Color(0.92f, 0.62f, 0.02f, 1.0f);

        Car* car = Car::Create(config);
        if (!car)
        {
            return false;
        }
        Entity* vehicle = car->GetRootEntity();
        if (!vehicle || !vehicle->GetComponent<Physics>())
        {
            car->Destroy();
            return false;
        }
        vehicle->SetObjectName("race_driver_car");
        vehicle->SetTransient(true);
        car->PlaceAt(point.position, Quaternion::FromLookRotation(m_line->DirectionAt(grid), Vector3::Up));

        AiDriverSettings settings = GetDriverSettings();
        settings.launch_delay     = m_start_delay;
        car->SetAiDriver(make_unique<AiDriver>(m_line, settings));
        m_car        = car;
        m_watch_time = 0.0f;
        m_car_spawns++;
        return true;
    }

    void RaceDriver::DestroyCar()
    {
        if (car_alive(m_car))
        {
            m_car->Destroy();
        }
        m_car = nullptr;
    }

    void RaceDriver::Save(pugi::xml_node& node)
    {
        node.append_attribute("track_entity_id") = m_track_entity_id;
        node.append_attribute("car_file")        = m_car_file.c_str();
        node.append_attribute("skill")           = m_skill;
        node.append_attribute("max_speed")       = m_max_speed;
        node.append_attribute("edge_margin")     = m_edge_margin;
        node.append_attribute("start_distance")  = m_start_distance;
        node.append_attribute("start_delay")     = m_start_delay;
        node.append_attribute("learning")        = m_learning;
        node.append_attribute("verbose")         = m_verbose;
    }

    void RaceDriver::Load(pugi::xml_node& node)
    {
        m_track_entity_id = node.attribute("track_entity_id").as_ullong(m_track_entity_id);
        m_car_file        = node.attribute("car_file").as_string(m_car_file.c_str());
        m_skill           = clamp(node.attribute("skill").as_float(m_skill), 0.0f, 1.0f);
        m_max_speed       = clamp(node.attribute("max_speed").as_float(m_max_speed), 5.0f, 150.0f);
        m_edge_margin     = max(node.attribute("edge_margin").as_float(m_edge_margin), 0.0f);
        m_start_distance  = node.attribute("start_distance").as_float(m_start_distance);
        m_start_delay     = max(node.attribute("start_delay").as_float(m_start_delay), 0.0f);
        m_learning        = node.attribute("learning").as_bool(m_learning);
        m_verbose         = node.attribute("verbose").as_bool(m_verbose);
    }
}
