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
#include "Components/Camera.h"
#include "Components/Light.h"
//==================================

//= NAMESPACES ===============
using namespace std;
using namespace spartan::math;
//============================

namespace spartan
{
    namespace
    {
        vector<shared_ptr<Entity>> entities;
        string name;
        string file_path;
        mutex entity_access_mutex;
        bool resolve             = false;
        bool was_in_editor_mode  = false;
        BoundingBox bounding_box = BoundingBox::Undefined;
        Entity* camera           = nullptr;
        Entity* light            = nullptr;

        void compute_bounding_box()
        {
            for (shared_ptr<Entity>& entity : entities)
            {
                if (entity->IsActive())
                {
                    if (Renderable* renderable = entity->GetComponent<Renderable>())
                    {
                        bounding_box.Merge(renderable->GetBoundingBox());
                    }
                }
            }
        }
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

        if (!ProgressTracker::IsLoading())
        {
            // detect game toggling
            const bool started =  Engine::IsFlagSet(EngineMode::Playing) &&  was_in_editor_mode;
            const bool stopped = !Engine::IsFlagSet(EngineMode::Playing) && !was_in_editor_mode;
            was_in_editor_mode = !Engine::IsFlagSet(EngineMode::Playing);

            // start
            if (started)
            {
                for (shared_ptr<Entity>& entity : entities)
                {
                    entity->OnStart();
                }
            }

            // stop
            if (stopped)
            {
               for (shared_ptr<Entity>& entity : entities)
                {
                    entity->OnStop();
                }
            }

            // tick
            for (shared_ptr<Entity>& entity : entities)
            {
                if (entity->IsActive())
                { 
                    entity->Tick();
                }
            }

            // notify renderer
            if (resolve)
            {
                // find key entities
                {
                    camera = nullptr;
                    light  = nullptr;
                    for (shared_ptr<Entity>& entity : entities)
                    {
                        if (entity->IsActive())
                        {
                            if (!camera && entity->GetComponent<Camera>())
                            { 
                                camera = entity.get();
                            }

                            if (!light && entity->GetComponent<Light>())
                            {
                                if (entity->GetComponent<Light>()->GetLightType() == LightType::Directional)
                                {
                                    light = entity.get();
                                }
                            }
                        }
                    }
                }

                Renderer::SetEntities(entities);
                resolve = false;
                compute_bounding_box();
            }

            Game::Tick();
        }
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
        // add scene file extension to the filepath if it's missing
        auto file_path = file_path_in;
        if (FileSystem::GetExtensionFromFilePath(file_path) != EXTENSION_WORLD)
        {
            file_path += EXTENSION_WORLD;
        }

        name      = FileSystem::GetFileNameWithoutExtensionFromFilePath(file_path);
        file_path = file_path;

        // notify subsystems that need to save data
        SP_FIRE_EVENT(EventType::WorldSaveStart);

        // create a prefab file
        auto file = make_unique<FileStream>(file_path, FileStream_Write);
        if (!file->IsOpen())
        {
            SP_LOG_ERROR("Failed to open file.");
            return false;
        }

        // only save root entities as they will also save their descendants
        vector<shared_ptr<Entity>> root_actors = GetRootEntities();
        const uint32_t root_entity_count = static_cast<uint32_t>(root_actors.size());

        // start progress tracking and timing
        const Stopwatch timer;
        ProgressTracker::GetProgress(ProgressType::World).Start(root_entity_count, "Saving world...");

        // save root entity count
        file->Write(root_entity_count);

        // save root entity IDs
        for (shared_ptr<Entity>& root : root_actors)
        {
            file->Write(root->GetObjectId());
        }

        // save root entities
        for (shared_ptr<Entity>& root : root_actors)
        {
            root->Serialize(file.get());
            ProgressTracker::GetProgress(ProgressType::World).JobDone();
        }

        // report time
        SP_LOG_INFO("World \"%s\" has been saved. Duration %.2f ms", file_path.c_str(), timer.GetElapsedTimeMs());

        // notify subsystems waiting for us to finish
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
        entities.push_back(entity);

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

        // remove the entity and all of its children
        {
            // get the root entity and its descendants
            vector<Entity*> entities_to_remove;
            entities_to_remove.push_back(entity_to_remove);        // add the root entity
            entity_to_remove->GetDescendants(&entities_to_remove); // get descendants

            // create a set containing the object ids of entities to remove
            set<uint64_t> ids_to_remove;
            for (Entity* entity : entities_to_remove)
            {
                ids_to_remove.insert(entity->GetObjectId());
            }

            // remove entities using a single loop
            for (auto it = entities.begin(); it != entities.end(); )
            {
                if (ids_to_remove.count((*it)->GetObjectId()) > 0)
                {
                    it = entities.erase(it);
                }
                else
                {
                    ++it;
                }
            }

            // if there was a parent, update it
            if (shared_ptr<Entity> parent = entity_to_remove->GetParent())
            {
                parent->AcquireChildren();
            }
        }

        resolve      = true;
        bounding_box = BoundingBox::Undefined;
    }

    vector<shared_ptr<Entity>> World::GetRootEntities()
    {
        lock_guard<mutex> lock(entity_access_mutex);

        vector<shared_ptr<Entity>> root_entities;

        for (shared_ptr<Entity>& entity : entities)
        {
            if (!entity->HasParent())
            {
                root_entities.emplace_back(entity);
            }
        }

        return root_entities;
    }

    const shared_ptr<Entity>& World::GetEntityById(const uint64_t id)
    {
        lock_guard<mutex> lock(entity_access_mutex);
    
        for (const auto& entity : entities)
        {
            if (entity && entity->GetObjectId() == id)
                return entity;
        }
    
        static shared_ptr<Entity> empty;
        return empty;
    }
    
    const vector<shared_ptr<Entity>>& World::GetAllEntities()
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

    BoundingBox& World::GetBoundingBox()
    {
        if (bounding_box == BoundingBox::Undefined)
        {
            compute_bounding_box();
        }

        return bounding_box;
    }

    Camera* World::GetCamera()
    {
        return camera ? camera->GetComponent<Camera>() : nullptr;
    }

    Light* World::GetDirectionalLight()
    {
        return light ? light->GetComponent<Light>() : nullptr;
    }
}
