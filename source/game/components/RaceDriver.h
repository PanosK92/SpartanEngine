/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

#pragma once

#include "../../world/components/Component.h"
#include "../../car/AiDriver.h"
#include <atomic>
#include <memory>
#include <string>

namespace spartan
{
    class Car;
    class RacingLine;

    // a race on a closed spline road: when play starts the road becomes a racing line, a car is spawned on the grid and an ai driver takes it
    // the line is shared, so any other car can be handed a driver for the same road, see Car::SetAiDriver
    class RaceDriver : public Component
    {
    public:
        bool StartsEarly() const override { return true; }
        RaceDriver(Entity* entity);
        ~RaceDriver() override;

        void Start() override;
        void Stop() override;
        void Remove() override;
        void Tick() override;
        void Save(pugi::xml_node& node) override;
        void Load(pugi::xml_node& node) override;

        Car* GetCar() const { return m_car; }
        const std::shared_ptr<RacingLine>& GetRacingLine() const { return m_line; }
        AiDriverSettings GetDriverSettings() const;

    private:
        struct PreloadState
        {
            std::atomic<bool> cancelled = false;
            std::atomic<bool> completed = false;
            std::atomic<bool> succeeded = false;
        };

        void BeginPreload();
        bool SpawnCar();
        Car* SpawnCarFromPrefab(const math::Vector3& position);
        void DestroyCar();
        void Watch(float delta_time);

        // settings
        uint64_t m_track_entity_id = 0; // spline road to race on, 0 uses this entity
        std::string m_car_file     = "project/cars/ferrari_laferrari.car";
        std::string m_car_prefab;           // optional .prefab holding a car prefab, the race car is built from it (lights, body and wheel fit, effects) instead of the bare car file
        float m_skill              = 1.0f;  // 0 to 1, how much of the measured grip and braking the driver dares to use
        float m_max_speed          = 95.0f; // m/s
        float m_edge_margin        = 1.7f;  // meters the racing line keeps between the car's center and the road edge
        float m_start_distance     = 0.0f;  // grid slot along the track
        float m_start_delay        = 2.0f;  // seconds on the grid before the launch
        bool m_learning            = true;
        bool m_verbose             = false;

        // the spawned car's driver, readable through component_get
        AiDriverStats m_stats;
        float m_track_length  = 0.0f;
        uint32_t m_car_spawns = 0; // a car that dies or keeps crashing is replaced, so the race never stops

        // watchdog window
        float m_watch_time      = 0.0f;
        float m_watch_progress  = 0.0f; // meters the driver had travelled when the window opened
        uint32_t m_watch_resets = 0;
        float m_spawn_delay     = 0.0f;
        const AiDriver* m_watch_driver = nullptr; // a new driver restarts its stats, so the window restarts too

        std::shared_ptr<RacingLine> m_line;
        std::shared_ptr<PreloadState> m_preload;
        std::string m_car_path;
        std::string m_car_prefab_path;  // resolved m_car_prefab, empty when unset or unreadable
        Car* m_car = nullptr;
        uint64_t m_car_holder_id = 0;   // entity a prefab car is spawned under, removed with the car
    };
}
