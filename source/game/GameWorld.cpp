/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

#include "pch.h"
#include "GameWorld.h"
#include "CarRender.h"
#include "CarWeather.h"
#include "CameraController.h"
#include "../world/World.h"
#include "../world/TerrainSystem.h"
#include "../core/ProgressTracker.h"
#include "IslandWildlife.h"
#include "IslandRoadDetails.h"
#include "../car/Car.h"
#include "../car/CarPhysics.h"
#include <sol/sol.hpp>
#include "../car/CarPhysicsTypes.h"
#include "../world/components/SplineRoadSurface.h"
#include "IslandRoadSurface.h"
#include "../io/pugixml.hpp"
namespace spartan::game
{
    namespace { bool island_features = false; bool landscape_prepared = false; }
    bool GetIslandFeatures() { return island_features; }
    void SetIslandFeatures(bool enabled) { island_features = enabled; }
    void Initialize()
    {
        road_surface::enabled = island_road_surface::Enabled;
        road_surface::material_for = island_road_surface::MaterialFor;
        InitializeCollisionPolicy();
        CarPhysics::Initialize();
        CarPhysics::RegisterForScripting(World::GetLuaState());
        CameraController::Initialize();
        SP_SUBSCRIBE_TO_EVENT(EventType::WorldTicked, [](const sp_variant&) {
            if (!World::IsLoadingFromFile()) TickCarWeather(static_cast<float>(Timer::GetDeltaTimeSec()));
            SubmitCarRendering();
        });
        SP_SUBSCRIBE_TO_EVENT(EventType::WorldUnloading, [](const sp_variant&) {
            ClearCarWeather();
        });
        WorldCallbacks callbacks;
        callbacks.before_load = [] { landscape_prepared = false; Car::RegisterPrefabs(); };
        callbacks.prepare = [](const std::vector<Entity*>& entities, const ProgressTask& progress) {
            if (!landscape_prepared)
            {
                if (!TerrainSystem::PrepareWorld(entities, progress)) return false;
                landscape_prepared = true;
            }
            progress.SetDetail("Roadside details and guardrails");
            return island_road_details::PrepareWorld();
        };
        callbacks.before_entities_destroyed = [] { Car::ShutdownAll(); };
        callbacks.after_entities_destroyed = [] {
            island_wildlife::Clear(false);
            island_road_details::Clear();
            island_features = false;
            landscape_prepared = false;
        };
        callbacks.stop_play = [] {
            island_wildlife::Clear(true);
        };
        callbacks.before_tick = [](float dt) { island_wildlife::Tick(dt); };
        callbacks.after_tick = [](float dt) { island_road_details::Tick(dt); };
        callbacks.controls = [] { CameraController::Tick(); };
        callbacks.save = [](pugi::xml_node& node) { if (island_features) node.append_attribute("island_features") = true; };
        callbacks.load = [](pugi::xml_node& node) { island_features = node.attribute("island_features").as_bool(false); };
        World::SetCallbacks(callbacks);
    }
}
