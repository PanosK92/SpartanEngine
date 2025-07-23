/*
Copyright(c) 2015-2025 Panos Karabelas

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
#include "../Core/ProgressTracker.h"
#include "Components/Renderable.h"
#include "Components/Camera.h"
#include "Components/Light.h"
#include "Components/AudioSource.h"
SP_WARNINGS_OFF
#include "../IO/pugixml.hpp"
SP_WARNINGS_ON
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
        vector<shared_ptr<Entity>> entities_lights; // entities subset that contains only lights
        string name;
        string file_path;
        mutex entity_access_mutex;
        bool resolve                = false;
        bool was_in_editor_mode     = false;
        BoundingBox bounding_box    = BoundingBox::Unit;
        shared_ptr<Entity> camera   = nullptr;
        shared_ptr<Entity> light    = nullptr;
        uint32_t audio_source_count = 0;

        void compute_bounding_box()
        {
            for (shared_ptr<Entity>& entity : entities)
            {
                if (entity->GetActive())
                {
                    if (Renderable* renderable = entity->GetComponent<Renderable>())
                    {
                        bounding_box.Merge(renderable->GetBoundingBox());
                    }
                }
            }
        }
    }

    namespace day_night_cycle
    {
        float current_time = 0.25f;  // start at 6 am
        float time_scale   = 200.0f; // 200x real time

        void tick()
        {
            current_time += (static_cast<float>(Timer::GetDeltaTimeSec()) * time_scale) / 86400.0f;
            if (current_time >= 1.0f)
            {
               current_time -= 1.0f;
            }
            if (current_time < 0.0f)
            {
                current_time = 0.0f;
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
        // loading can happen in the background
        if (ProgressTracker::IsLoading())
            return;

        SP_PROFILE_CPU();
        lock_guard<mutex> lock(entity_access_mutex);

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
            if (entity->GetActive())
            { 
                entity->Tick();
            }
        }
        
        if (resolve)
        {
            // track entities
            {
                camera             = nullptr;
                light              = nullptr;
                audio_source_count = 0;
                entities_lights.clear();
                for (shared_ptr<Entity>& entity : entities)
                {
                    if (entity->GetActive())
                    {
                        if (!camera && entity->GetComponent<Camera>())
                        { 
                            camera = entity;
                        }
        
                        if (entity->GetComponent<Light>())
                        {
                            if (!light && entity->GetComponent<Light>()->GetLightType() == LightType::Directional)
                            {
                                light = entity;
                            }

                            entities_lights.push_back(entity);
                        }
        
                        if (entity->GetComponent<AudioSource>())
                        {
                            audio_source_count++;
                        }
                    }
                }
            }

            compute_bounding_box();
            resolve = false;
        }

        if (Engine::IsFlagSet(EngineMode::Playing))
        { 
            day_night_cycle::tick();
        }
        Game::Tick();
    }

    void World::Clear()
    {
        // fire event
        SP_FIRE_EVENT(EventType::WorldClear);
        
        // clear
        entities.clear();
        entities_lights.clear();
        camera = nullptr;
        light  = nullptr;
        name.clear();
        file_path.clear();
        
        // mark for resolve
        resolve = true;
    }

    bool World::SaveToFile(const string& file_path)
    {
        // create xml document
        pugi::xml_document doc;
        pugi::xml_node world_node = doc.append_child("World");
        world_node.append_attribute("name") = name.c_str();

        // get root entities, save them, and they will save their children recursively
        vector<shared_ptr<Entity>> root_actors = GetRootEntities();
        const uint32_t root_entity_count       = static_cast<uint32_t>(root_actors.size());

        // start progress tracking and timing
        const Stopwatch timer;
        ProgressTracker::GetProgress(ProgressType::World).Start(root_entity_count, "Saving world...");

        // write to xml node
        for (shared_ptr<Entity>& root : root_actors)
        {
            pugi::xml_node entity_node = world_node.append_child("Entity");
            root->Serialize(entity_node);
            ProgressTracker::GetProgress(ProgressType::World).JobDone();
        }

        // save to xml node
        bool saved = doc.save_file(file_path.c_str(), "  ", pugi::format_indent);
        if (!saved)
        {
            SP_LOG_ERROR("Failed to save XML file.");
            return false;
        }

        // log
        SP_LOG_INFO("World \"%s\" has been saved. Duration %.2f ms", file_path.c_str(), timer.GetElapsedTimeMs());

        return true;
    }

    bool World::LoadFromFile(const string& file_path_)
    {
        Clear();

        file_path = file_path_;

        // load xml document
        pugi::xml_document doc;
        pugi::xml_parse_result result = doc.load_file(file_path.c_str());
        if (!result)
        {
            SP_LOG_ERROR("Failed to load XML file: %s", result.description());
            return false;
        }

        // get world node
        pugi::xml_node world_node = doc.child("World");
        if (!world_node)
        {
            SP_LOG_ERROR("No 'World' node found.");
            return false;
        }

        name = world_node.attribute("name").as_string();

        // count root entities for progress tracking
        uint32_t root_entity_count = 0;
        for (pugi::xml_node entity_node = world_node.child("Entity"); entity_node; entity_node = entity_node.next_sibling("Entity"))
        {
            ++root_entity_count;
        }

        // start progress tracking and timing
        ProgressTracker::GetProgress(ProgressType::World).Start(root_entity_count, "Loading world...");
        const Stopwatch timer;

        // load root entities (they will load their descendants recursively)
        for (pugi::xml_node entity_node = world_node.child("Entity"); entity_node; entity_node = entity_node.next_sibling("Entity"))
        {
            shared_ptr<Entity> entity = CreateEntity();
            entity->Deserialize(entity_node);
            ProgressTracker::GetProgress(ProgressType::World).JobDone();
        }

        // report time
        SP_LOG_INFO("World \"%s\" has been loaded. Duration %.2f ms", file_path.c_str(), timer.GetElapsedTimeMs());

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
        bounding_box = BoundingBox::Unit;
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
    
    const vector<shared_ptr<Entity>>& World::GetEntities()
    {
        return entities;
    }

    const vector<shared_ptr<Entity>>& World::GetEntitiesLights()
    {
        return entities_lights;
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

    uint32_t World::GetLightCount()
    {
        return static_cast<uint32_t>(entities_lights.size());
    }

    uint32_t World::GetAudioSourceCount()
    {
        return audio_source_count;
    }

    float World::GetTimeOfDay()
    {
        return day_night_cycle::current_time;
    }
}
