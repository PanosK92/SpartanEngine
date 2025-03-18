/*
Copyright(c) 2016-2025 Panos Karabelas

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

//= INCLUDES =======================
#include "pch.h"
#include "World.h"
#include "Entity.h"
#include "../Game/Game.h"
#include "../IO/FileStream.h"
#include "../Profiling/Profiler.h"
#include "../Rendering/Renderer.h"
#include "../Core/ProgressTracker.h"
#include "Components/Renderable.h"
//==================================

//= NAMESPACES ===============
using namespace std;
using namespace spartan::math;
//============================

namespace spartan
{
    namespace
    {
        unordered_map<uint64_t, shared_ptr<Entity>> entities;
        string name;
        string file_path;
        mutex entity_access_mutex;
        bool resolve             = false;
        bool was_in_editor_mode  = false;
        BoundingBox bounding_box = BoundingBox::Undefined;
    }

    void World::Initialize()
    {

    }

    void World::Shutdown()
    {
        Game::Shutdown();
        Clear();
    }

    void World::Tick()
    {
        SP_PROFILE_CPU();

        lock_guard<mutex> lock(entity_access_mutex);

        // tick entities
        {
            // detect game toggling
            const bool started =  Engine::IsFlagSet(EngineMode::Playing) &&  was_in_editor_mode;
            const bool stopped = !Engine::IsFlagSet(EngineMode::Playing) && !was_in_editor_mode;
            was_in_editor_mode = !Engine::IsFlagSet(EngineMode::Playing);

            // start
            if (started)
            {
                for (auto it : entities)
                {
                    it.second->OnStart();
                }
            }

            // stop
            if (stopped)
            {
                for (auto it : entities)
                {
                    it.second->OnStop();
                }
            }

            // tick
            for (auto it : entities)
            {
                it.second->Tick();
            }
        }

        // notify renderer
        if (resolve && !ProgressTracker::IsLoading())
        {
            Renderer::SetEntities(entities);
            resolve      = false;
            bounding_box = BoundingBox::Undefined;
        }

        Game::Tick();
    }

    void World::Clear()
    {
        // fire event
        SP_FIRE_EVENT(EventType::WorldClear);
        
        // clear
        entities.clear();
        name.clear();
        file_path.clear();
        
        // mark for resolve
        resolve = true;
    }

    bool World::SaveToFile(const string& file_path_in)
    {
        // Add scene file extension to the filepath if it's missing
        auto file_path = file_path_in;
        if (FileSystem::GetExtensionFromFilePath(file_path) != EXTENSION_WORLD)
        {
            file_path += EXTENSION_WORLD;
        }

        name      = FileSystem::GetFileNameWithoutExtensionFromFilePath(file_path);
        file_path = file_path;

        // Notify subsystems that need to save data
        SP_FIRE_EVENT(EventType::WorldSaveStart);

        // Create a prefab file
        auto file = make_unique<FileStream>(file_path, FileStream_Write);
        if (!file->IsOpen())
        {
            SP_LOG_ERROR("Failed to open file.");
            return false;
        }

        // Only save root entities as they will also save their descendants
        vector<shared_ptr<Entity>> root_actors = GetRootEntities();
        const uint32_t root_entity_count = static_cast<uint32_t>(root_actors.size());

        // Start progress tracking and timing
        const Stopwatch timer;
        ProgressTracker::GetProgress(ProgressType::World).Start(root_entity_count, "Saving world...");

        // Save root entity count
        file->Write(root_entity_count);

        // Save root entity IDs
        for (shared_ptr<Entity>& root : root_actors)
        {
            file->Write(root->GetObjectId());
        }

        // Save root entities
        for (shared_ptr<Entity>& root : root_actors)
        {
            root->Serialize(file.get());
            ProgressTracker::GetProgress(ProgressType::World).JobDone();
        }

        // Report time
        SP_LOG_INFO("World \"%s\" has been saved. Duration %.2f ms", file_path.c_str(), timer.GetElapsedTimeMs());

        // Notify subsystems waiting for us to finish
        SP_FIRE_EVENT(EventType::WorldSavedEnd);

        return true;
    }

    bool World::LoadFromFile(const string& file_path_)
    {
        file_path = file_path_;

        if (!FileSystem::Exists(file_path))
        {
            SP_LOG_ERROR("\"%s\" was not found.", file_path.c_str());
            return false;
        }

        // open file
        unique_ptr<FileStream> file = make_unique<FileStream>(file_path, FileStream_Read);
        if (!file->IsOpen())
        {
            SP_LOG_ERROR("Failed to open \"%s\"", file_path.c_str());
            return false;
        }

        Clear();

        name = FileSystem::GetFileNameWithoutExtensionFromFilePath(file_path);

        // notify subsystems that need to load data
        SP_FIRE_EVENT(EventType::WorldLoadStart);

        // load root entity count
        const uint32_t root_entity_count = file->ReadAs<uint32_t>();

        // start progress tracking and timing
        ProgressTracker::GetProgress(ProgressType::World).Start(root_entity_count, "Loading world...");
        const Stopwatch timer;

        // load root entity IDs
        for (uint32_t i = 0; i < root_entity_count; i++)
        {
            shared_ptr<Entity> entity = CreateEntity();
            entity->SetObjectId(file->ReadAs<uint64_t>());
        }

        // serialize root entities
        for (uint32_t i = 0; i < root_entity_count; i++)
        {
            entities[i]->Deserialize(file.get(), nullptr);
            ProgressTracker::GetProgress(ProgressType::World).JobDone();
        }

        // report time
        SP_LOG_INFO("World \"%s\" has been loaded. Duration %.2f ms", file_path.c_str(), timer.GetElapsedTimeMs());

        SP_FIRE_EVENT(EventType::WorldLoadEnd);

        return true;
    }

    void World::Resolve()
    {
        resolve = true;
    }

    shared_ptr<Entity> World::CreateEntity()
    {
        lock_guard lock(entity_access_mutex);

        shared_ptr<Entity> entity = make_shared<Entity>();
        entity->Initialize();
        entities[entity->GetObjectId()] = entity;

        return entity;
    }

    bool World::EntityExists(Entity* entity)
    {
        SP_ASSERT_MSG(entity != nullptr, "Entity is null");
        return GetEntityById(entity->GetObjectId()) != nullptr;
    }

    void World::RemoveEntity(Entity* entity_to_remove)
    {
        SP_ASSERT_MSG(entity_to_remove != nullptr, "Entity is null");

        lock_guard<mutex> lock(entity_access_mutex);

        // Remove the entity and all of its children
        {
            // Get the root entity and its descendants
            std::vector<Entity*> entities_to_remove;
            entities_to_remove.push_back(entity_to_remove);        // add the root entity
            entity_to_remove->GetDescendants(&entities_to_remove); // get descendants

            // Create a set containing the object IDs of entities to remove
            std::set<uint64_t> ids_to_remove;
            for (Entity* entity : entities_to_remove) {
                ids_to_remove.insert(entity->GetObjectId());
            }

            // Remove entities using a single loop
            for (auto it = entities.begin(); it != entities.end(); )
            {
                if (ids_to_remove.count(it->first) > 0)
                {
                    it = entities.erase(it);
                }
                else
                {
                    ++it;
                }
            }

            // If there was a parent, update it
            if (std::shared_ptr<Entity> parent = entity_to_remove->GetParent())
            {
                parent->AcquireChildren();
            }
        }

        resolve = true;
    }

    vector<shared_ptr<Entity>> World::GetRootEntities()
    {
        lock_guard<mutex> lock(entity_access_mutex);

        vector<shared_ptr<Entity>> root_entities;
        for (auto it : entities)
        {
            if (!it.second->HasParent())
            {
                root_entities.emplace_back(it.second);
            }
        }

        return root_entities;
    }

    const shared_ptr<Entity>& World::GetEntityById(const uint64_t id)
    {
        lock_guard<mutex> lock(entity_access_mutex);

        auto it = entities.find(id);
        if (it != entities.end())
            return it->second;

        static shared_ptr<Entity> empty;
        return empty;
    }

    const unordered_map<uint64_t, shared_ptr<Entity>>& World::GetAllEntities()
    {
        return entities;
    }

    const string World::GetName()
    {
        return name;
    }

    const string& World::GetFilePath()
    {
        return file_path;
    }

    BoundingBox& World::GetBoundinBox()
    {
        if (bounding_box == BoundingBox::Undefined)
        { 
            for (auto& entity : entities)
            {
                if (entity.second->IsActive())
                {
                    if (Renderable* renderable = entity.second->GetComponent<Renderable>())
                    {
                        if (renderable->IsVisible())
                        {
                            bounding_box.Merge(renderable->GetBoundingBox(BoundingBoxType::Transformed));
                        }
                    }
                }
            }
        }

        return bounding_box;
    }
}
