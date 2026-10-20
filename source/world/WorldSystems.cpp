/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

#include "pch.h"
#include "WorldSystems.h"
#include "World.h"
#include "TerrainSystem.h"
#include "CameraController.h"
#include "AmbientWildlife.h"
#include "AmbientAtmosphere.h"
#include "RoadDetails.h"
#include "../core/ProgressTracker.h"
#include "../profiling/Profiler.h"
#include "../car/Car.h"
#include "../car/CarPhysics.h"
#include "../car/CarRender.h"
#include "../car/CarWeather.h"
#include "../car/CarParticleWake.h"
#include "../physics/PhysicsCollisionPolicy.h"
#include <sol/sol.hpp>
#include "BreakableTrees.h"
#include "../car/CarPhysicsTypes.h"
#include "components/SplineRoadSurface.h"
#include "RoadSurfaceLayers.h"
#include "../io/pugixml.hpp"
namespace spartan::world_systems
{
    namespace
    {
        Config config;
        bool landscape_prepared = false;

        void register_for_scripting(sol::state_view lua)
        {
            sol::table table = lua.create_named_table("WorldSystems");
            table["SetAmbientWildlife"]   = [](bool enabled) { config.ambient_wildlife = enabled; };
            table["SetAmbientAtmosphere"] = [](bool enabled) { config.ambient_atmosphere = enabled; };
            table["SetRoadDetails"]       = [](bool enabled) { config.road_details = enabled; };
            table["SetLayeredRoads"]      = [](const std::string& parent_name, const std::string& material_dir, sol::optional<std::string> material_prefix)
            {
                config.layered_roads_parent          = parent_name;
                config.layered_roads_material_dir    = material_dir;
                config.layered_roads_material_prefix = material_prefix.value_or("road");
            };
        }

        bool references_script(const pugi::xml_node& node, const std::string& script_name)
        {
            if (std::string(node.name()) == "script" && FileSystem::GetFileNameFromFilePath(node.attribute("file_path").as_string()) == script_name)
                return true;
            for (pugi::xml_node child = node.first_child(); child; child = child.next_sibling())
            {
                if (references_script(child, script_name))
                    return true;
            }
            return false;
        }

        // a world's companion script (plan.lua next to plan.world) sets up its optional systems, it runs once on the main
        // thread with the deferred scripts, before preparation builds the roads, unless an entity's script component runs it
        void queue_companion_script(const pugi::xml_node& world_node)
        {
            const std::string& world_path = World::GetFilePath();
            if (world_path.empty())
                return;
            const std::string script_name = FileSystem::GetFileNameWithoutExtensionFromFilePath(world_path) + ".lua";
            const std::string script_path = FileSystem::GetDirectoryFromFilePath(world_path) + script_name;
            if (!FileSystem::Exists(script_path) || references_script(world_node, script_name))
                return;
            World::AddDeferredScriptInit(-1, [script_path]()
            {
                sol::protected_function_result result = World::GetLuaState().safe_script_file(script_path);
                if (!result.valid())
                {
                    sol::error error = result;
                    SP_LOG_ERROR("world script %s failed: %s", script_path.c_str(), error.what());
                    return;
                }
                SP_LOG_INFO("world script %s configured the world", script_path.c_str());
            });
        }
    }

    const Config& GetConfig() { return config; }

    void Initialize()
    {
        road_surface::enabled = layered_road_surface::Enabled;
        road_surface::material_for = layered_road_surface::MaterialFor;
        InitializePhysicsCollisionPolicy();
        CarPhysics::Initialize();
        CarPhysics::RegisterForScripting(World::GetLuaState());
        register_for_scripting(World::GetLuaState());
        CameraController::Initialize();
        SP_SUBSCRIBE_TO_EVENT(EventType::WorldTicked, [](const sp_variant&) {
            if (!World::IsLoadingFromFile()) TickCarWeather(static_cast<float>(Timer::GetDeltaTimeSec()));
            SubmitCarRendering();
            // reads the bodies the submission above just gathered
            breakable_trees::Tick(static_cast<float>(Timer::GetDeltaTimeSec()));
        });
        SP_SUBSCRIBE_TO_EVENT(EventType::WorldUnloading, [](const sp_variant&) {
            ClearCarWeather();
        });
        WorldCallbacks callbacks;
        callbacks.before_load = [] { config = {}; landscape_prepared = false; Car::RegisterPrefabs(); };
        callbacks.prepare = [](const std::vector<Entity*>& entities, const ProgressTask& progress) {
            if (!landscape_prepared)
            {
                if (!TerrainSystem::PrepareWorld(entities, progress)) return false;
                landscape_prepared = true;
            }
            progress.SetDetail("Roadside details and guardrails");
            return road_details::PrepareWorld();
        };
        callbacks.before_entities_destroyed = [] { Car::ShutdownAll(); };
        callbacks.after_entities_destroyed = [] {
            vehicle_particle_wake::Clear();
            ambient_wildlife::Clear(false);
            ambient_atmosphere::Clear(false);
            road_details::Clear();
            breakable_trees::Clear();
            vegetation_interaction::Clear();
            config = {};
            landscape_prepared = false;
        };
        callbacks.stop_play = [] {
            breakable_trees::RestoreAll();
            vegetation_interaction::Clear();
            vehicle_particle_wake::Clear();
            ambient_wildlife::Clear(true);
            ambient_atmosphere::Clear(true);
            for (Car* car : Car::GetAll())
            {
                if (car) car->PrepareForPlayStop();
            }
        };
        callbacks.before_tick = [](float dt) { CarPhysics::BeginTireDeformationBatch(); ambient_wildlife::Tick(dt); };
        callbacks.after_pretick = [] { CarPhysics::FlushTireDeformationBatch(); };
        // leaves and the car wake run in the editor too, so shots can be framed and tuned without entering play
        callbacks.after_tick = [](float dt)
        {
            SP_PROFILE_CPU_START("road_details_tick");
            road_details::Tick(dt);
            SP_PROFILE_CPU_END();
            if (!Engine::IsFlagSet(EngineMode::Paused))
            {
                SP_PROFILE_CPU_START("ambient_atmosphere_tick");
                ambient_atmosphere::Tick(dt);
                SP_PROFILE_CPU_END();
            }
            SP_PROFILE_CPU_START("vehicle_particle_wake_tick");
            vehicle_particle_wake::Tick(dt);
            SP_PROFILE_CPU_END();
        };
        callbacks.controls = [] { CameraController::Tick(); };
        callbacks.load = [](pugi::xml_node& node) { queue_companion_script(node); };
        World::SetCallbacks(callbacks);
    }
}
