/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

#pragma once

#include "../math/BoundingBox.h"
#include "Environment.h"
#include <string>
#include <vector>
#include <cstdint>
#include <functional>
#include <sol/forward.hpp>

namespace spartan
{
    class Entity;
    class Camera;
    class Light;
    struct RenderSceneData;
    struct WorldWorkCounters;

    // metadata structure for reading world info without fully loading
    struct WorldMetadata
    {
        std::string file_path;
        std::string name;
        std::string description;
    };

    // owns every entity, its serialization and the cached per-component entity lists the renderer reads
    class World
    {
    public:
        // system
        static void Initialize();
        static void Shutdown();
        static void Tick();
        static const WorldWorkCounters& GetWorkCounters();
        static uint64_t GetWorkCounterTick();

        // io
        static bool SaveToFile(std::string file_path);
        static bool SaveToFileAsync(std::string file_path);
        static bool LoadFromFile(const std::string& file_path);
        static bool IsLoadingFromFile();
        static bool IsPreparing();
        static void ProcessPendingLoad();
        static bool IsSaving();
        // Empty until a world has a file path; generated caches must not spill into the project root.
        static std::string GetResourceDirectory();
        static std::string GetResourceDirectory(const std::string& world_file_path);
        // mcp ai blockout output, world save leaves these alone
        static void SetGeneratedResourceDirectory(const std::string& directory);
        static const std::string& GetGeneratedResourceDirectory();
        // asset viewer curated library, separate from raw mcp blockout output
        static void SetLibraryResourceDirectory(const std::string& directory);
        static const std::string& GetLibraryResourceDirectory();
        static std::vector<std::string> GetLastResourceCleanup();
        static std::vector<std::string> GetLastResourceCleanupFailures();

        // entities
        static sol::state_view GetLuaState();
        static Entity* CreateEntity();

        // drain freshly created entities into the live lists, a caller that must render what it just
        // built in the same frame has to do this because the renderer only reads the live lists
        static void ProcessPendingAdditions();

        // lua is single threaded, so script init is queued during a bulk load and run in order once every entity exists
        static bool IsDeferringScriptInit();
        static void AddDeferredScriptInit(int order, std::function<void()>&& init);

        static bool EntityExists(Entity* entity);
        static void RemoveEntity(Entity* entity);
        static bool CancelPendingRemoval(Entity* entity);
        static void RemoveEntityImmediate(Entity* entity);
        static void GetRootEntities(std::vector<Entity*>& entities);
        static void MoveEntityToIndex(Entity* entity, uint32_t index);
        static void MoveRootEntityNear(Entity* entity_to_move, Entity* target_entity, bool insert_after);
        static Entity* GetEntityById(uint64_t id);
        static const std::vector<Entity*>& GetEntities();
        static const std::vector<Entity*>& GetEntitiesLights();
        static const std::vector<Entity*>& GetEntitiesWithRender();
        static const std::vector<const RenderSceneData*>& GetRenderSceneData();
        static void InvalidateRenderSceneData();
        // editor gizmo icons, excludes render-only props
        static const std::vector<Entity*>& GetEntitiesWithIcon();
        static const std::vector<Entity*>& GetEntitiesWithParticles();
        static const std::vector<Entity*>& GetEntitiesWithVolume();

        // true while play mode is still spreading Entity::Start across frames
        static bool IsPlayBooting();

        // misc
        static const std::string& GetName();
        static const std::string& GetFilePath();
        static void SetFilePath(const std::string& path);
        static math::BoundingBox& GetBoundingBox();
        static Camera* GetCamera();
        static void SetActiveCamera(Entity* entity);
        static Light* GetDirectionalLight();
        static uint32_t GetLightCount();
        static uint32_t GetAudioSourceCount();
        static bool HaveMaterialsChangedThisFrame();
        static bool HaveLightsChanged();

        // world time: 0.0 = midnight, 0.5 = noon, 1.0 = next midnight
        static float GetTimeOfDay(bool use_real_world_time = false);
        static void SetTimeOfDay(float time_of_day);

        static EnvironmentState GetEnvironment();

        // wind
        static const math::Vector3& GetWind();
        static math::Vector3 SampleWind(const math::Vector3& position, float time);
        static void SetWind(const math::Vector3& wind);

        // puddliness: 0 = dry ground, 1 = standing water in every low spot of terrain and roads
        static float GetPuddliness();
        static void SetPuddliness(float puddliness);

        // clouds, horizontal noise offset in meters, rerolled on every world load
        static const math::Vector2& GetCloudSeedOffset();

        // world metadata
        static const std::string& GetDescription();
        static void SetDescription(const std::string& description);

        // read metadata from a world file without fully loading it
        static bool ReadMetadata(const std::string& world_file_path, WorldMetadata& metadata);

    private:
        // Async saves serialize on the caller and dispatch only the XML write.
        static bool SaveToFileInternal(std::string file_path, bool asynchronous);
        static void ProcessPendingRemovals();
    };
}
