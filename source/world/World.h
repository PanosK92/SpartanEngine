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

//= INCLUDES ===================
#include "../math/BoundingBox.h"
#include <string>
#include <functional>
#include <sol/forward.hpp>
//==============================

namespace spartan
{
    class Camera;
    class Light;

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

        // io
        static bool SaveToFile(std::string file_path);
        static bool SaveToFileAsync(std::string file_path);
        static bool LoadFromFile(const std::string& file_path);
        static bool IsSaving();
        static std::string GetResourceDirectory(
            const std::string& world_file_path
        );
        // mcp ai blockout output, world save leaves these alone
        static void SetGeneratedResourceDirectory(
            const std::string& directory
        );
        static const std::string&
            GetGeneratedResourceDirectory();
        // asset viewer curated library, separate from raw mcp blockout output
        static void SetLibraryResourceDirectory(
            const std::string& directory
        );
        static const std::string&
            GetLibraryResourceDirectory();
        static const std::vector<std::string>&
            GetLastResourceCleanup();
        static const std::vector<std::string>&
            GetLastResourceCleanupFailures();

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
        static void RemoveEntityImmediate(Entity* entity);
        static void GetRootEntities(std::vector<Entity*>& entities);
        static void MoveEntityToIndex(Entity* entity, uint32_t index);
        static void MoveRootEntityNear(Entity* entity_to_move, Entity* target_entity, bool insert_after);
        static Entity* GetEntityById(uint64_t id);
        static const std::vector<Entity*>& GetEntities();
        static const std::vector<Entity*>& GetEntitiesLights();
        static const std::vector<Entity*>& GetEntitiesWithRender();
        // editor gizmo icons, excludes render-only props
        static const std::vector<Entity*>& GetEntitiesWithIcon();
        static const std::vector<Entity*>& GetEntitiesWithParticles();

        // true while play mode is still spreading Entity::Start across frames
        static bool IsPlayBooting();

        // misc
        static const std::string& GetName();
        static const std::string& GetFilePath();
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

        // wind
        static const math::Vector3& GetWind();
        static math::Vector3 SampleWind(const math::Vector3& position, float time);
        static void SetWind(const math::Vector3& wind);

        // world metadata
        static const std::string& GetDescription();
        static void SetDescription(const std::string& description);

        // read metadata from a world file without fully loading it
        static bool ReadMetadata(const std::string& world_file_path, WorldMetadata& metadata);

    private:
        // when defer_xml_write is true, resource and entity serialization runs on the caller
        // and only the xml file write is posted to the thread pool
        static bool SaveToFileInternal(std::string file_path, bool defer_xml_write);
        static void ProcessPendingRemovals();
    };
}
