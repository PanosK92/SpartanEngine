/*
Copyright(c) 2016-2021 Panos Karabelas

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

//= INCLUDES ==========================
#include "Spartan.h"
#include "World.h"
#include "Entity.h"
#include "Components/Transform.h"
#include "Components/Camera.h"
#include "Components/Light.h"
#include "Components/Environment.h"
#include "Components/AudioListener.h"
#include "../Resource/ResourceCache.h"
#include "../Resource/ProgressTracker.h"
#include "../IO/FileStream.h"
#include "../Profiling/Profiler.h"
#include "../Rendering/Renderer.h"
#include "../Input/Input.h"
#include "../RHI/RHI_Device.h"
//=====================================

//= NAMESPACES ================
using namespace std;
using namespace Spartan::Math;
//=============================

namespace Spartan
{
    World::World(Context* context) : ISubsystem(context)
    {
        // Subscribe to events
        SP_SUBSCRIBE_TO_EVENT(EventType::WorldResolve, [this](Variant) { m_resolve = true; });
    }

    World::~World()
    {
        m_input     = nullptr;
        m_profiler  = nullptr;
    }

    bool World::OnInitialise()
    {
        m_input     = m_context->GetSubsystem<Input>();
        m_profiler  = m_context->GetSubsystem<Profiler>();

        CreateCamera();
        CreateEnvironment();
        CreateDirectionalLight();

        return true;
    }

    void World::OnTick(float delta_time)
    {
        // If something is being loaded, don't tick as entities are probably being added
        if (IsLoading())
            return;

        SCOPED_TIME_BLOCK(m_profiler);

        // Tick entities
        {
            // Detect game toggling
            const bool started      = m_context->m_engine->EngineMode_IsSet(Engine_Game) && m_was_in_editor_mode;
            const bool stopped      = !m_context->m_engine->EngineMode_IsSet(Engine_Game) && !m_was_in_editor_mode;
            m_was_in_editor_mode    = !m_context->m_engine->EngineMode_IsSet(Engine_Game);

            // Start
            if (started)
            {
                for (shared_ptr<Entity>& entity : m_entities)
                {
                    entity->Start();
                }
            }

            // Stop
            if (stopped)
            {
                for (shared_ptr<Entity>& entity : m_entities)
                {
                    entity->Stop();
                }
            }

            // Tick
            for (shared_ptr<Entity>& entity : m_entities)
            {
                entity->Tick(delta_time);
            }
        }

        if (m_resolve)
        {
            // Update dirty entities
            {
                // Make a copy so we can iterate while removing entities
                auto entities_copy = m_entities;

                for (shared_ptr<Entity>& entity : entities_copy)
                {
                    if (entity->IsPendingDestruction())
                    {
                        _EntityRemove(entity);
                    }
                }
            }

            // Notify Renderer
            SP_FIRE_EVENT_DATA(EventType::WorldResolved, m_entities);
            m_resolve = false;
        }
    }

    void World::New()
    {
        Clear();
    }

    bool World::SaveToFile(const string& filePathIn)
    {
        // Start progress report and timer
        ProgressTracker::Get().Reset(ProgressType::World);
        ProgressTracker::Get().SetIsLoading(ProgressType::World, true);
        ProgressTracker::Get().SetStatus(ProgressType::World, "Saving world...");
        const Stopwatch timer;
    
        // Add scene file extension to the filepath if it's missing
        auto file_path = filePathIn;
        if (FileSystem::GetExtensionFromFilePath(file_path) != EXTENSION_WORLD)
        {
            file_path += EXTENSION_WORLD;
        }
        m_name = FileSystem::GetFileNameNoExtensionFromFilePath(file_path);

        // Notify subsystems that need to save data
        SP_FIRE_EVENT(EventType::WorldSave);

        // Create a prefab file
        auto file = make_unique<FileStream>(file_path, FileStream_Write);
        if (!file->IsOpen())
        {
            LOG_ERROR_GENERIC_FAILURE();
            return false;
        }

        // Only save root entities as they will also save their descendants
        auto root_actors = EntityGetRoots();
        const auto root_entity_count = static_cast<uint32_t>(root_actors.size());

        ProgressTracker::Get().SetJobCount(ProgressType::World, root_entity_count);

        // Save root entity count
        file->Write(root_entity_count);

        // Save root entity IDs
        for (const auto& root : root_actors)
        {
            file->Write(root->GetId());
        }

        // Save root entities
        for (const auto& root : root_actors)
        {
            root->Serialize(file.get());
            ProgressTracker::Get().IncrementJobsDone(ProgressType::World);
        }

        // Finish with progress report and timer
        ProgressTracker::Get().SetIsLoading(ProgressType::World, false);
        LOG_INFO("Saving took %.2f ms", timer.GetElapsedTimeMs());

        // Notify subsystems waiting for us to finish
        SP_FIRE_EVENT(EventType::WorldSaved);

        return true;
    }

    bool World::LoadFromFile(const string& file_path)
    {
        if (!FileSystem::Exists(file_path))
        {
            LOG_ERROR("%s was not found.", file_path.c_str());
            return false;
        }

        // Open file
        auto file = make_unique<FileStream>(file_path, FileStream_Read);
        if (!file->IsOpen())
            return false;

        // Start progress report and timing
        ProgressTracker::Get().Reset(ProgressType::World);
        ProgressTracker::Get().SetIsLoading(ProgressType::World, true);
        ProgressTracker::Get().SetStatus(ProgressType::World, "Loading world...");
        const Stopwatch timer;

        // The Renderer will detect the world loading progress and stop (to avoid entity race conditions).
        // Because this loading function can be called by a different thread, we wait for it to stop first.
        Renderer* renderer = m_context->GetSubsystem<Renderer>();
        renderer->Stop();
        
        // Clear current entities
        Clear();

        renderer->Start();

        m_name = FileSystem::GetFileNameNoExtensionFromFilePath(file_path);

        // Notify subsystems that need to load data
        SP_FIRE_EVENT(EventType::WorldLoad);

        // Load root entity count
        const uint32_t root_entity_count = file->ReadAs<uint32_t>();

        ProgressTracker::Get().SetJobCount(ProgressType::World, root_entity_count);

        // Load root entity IDs
        for (uint32_t i = 0; i < root_entity_count; i++)
        {
            shared_ptr<Entity> entity = EntityCreate();
            entity->SetId(file->ReadAs<uint32_t>());
        }

        // Serialize root entities
        for (uint32_t i = 0; i < root_entity_count; i++)
        {
            m_entities[i]->Deserialize(file.get(), nullptr);
            ProgressTracker::Get().IncrementJobsDone(ProgressType::World);
        }

        ProgressTracker::Get().SetIsLoading(ProgressType::World, false);
        LOG_INFO("Loading took %.2f ms", timer.GetElapsedTimeMs());

        SP_FIRE_EVENT(EventType::WorldLoaded);

        return true;
    }

    bool World::IsLoading()
    {
        auto& progress_report = ProgressTracker::Get();

        const bool is_loading_model = progress_report.GetIsLoading(ProgressType::ModelImporter);
        const bool is_loading_scene = progress_report.GetIsLoading(ProgressType::World);

        return is_loading_model || is_loading_scene;
    }

    shared_ptr<Entity> World::EntityCreate(bool is_active /*= true*/)
    {
        shared_ptr<Entity> entity = m_entities.emplace_back(make_shared<Entity>(m_context));
        entity->SetActive(is_active);
        return entity;
    }

    bool World::EntityExists(const shared_ptr<Entity>& entity)
    {
        if (!entity)
            return false;

        return EntityGetById(entity->GetId()) != nullptr;
    }

    void World::EntityRemove(const shared_ptr<Entity>& entity)
    {
        if (!entity)
            return;

        // Mark for destruction but don't delete now
        // as the Renderer might still be using it.
        entity->MarkForDestruction();
        m_resolve = true;
    }

    vector<shared_ptr<Entity>> World::EntityGetRoots()
    {
        vector<shared_ptr<Entity>> root_entities;
        for (const auto& entity : m_entities)
        {
            if (entity->GetTransform()->IsRoot())
            {
                root_entities.emplace_back(entity);
            }
        }

        return root_entities;
    }

    const shared_ptr<Entity>& World::EntityGetByName(const string& name)
    {
        for (const auto& entity : m_entities)
        {
            if (entity->GetObjectName() == name)
                return entity;
        }

        static shared_ptr<Entity> empty;
        return empty;
    }

    const shared_ptr<Entity>& World::EntityGetById(const uint32_t id)
    {
        for (const auto& entity : m_entities)
        {
            if (entity->GetId() == id)
                return entity;
        }

        static shared_ptr<Entity> empty;
        return empty;
    }

    void World::Clear()
    {
        // Notify any systems that the entities are about to be cleared
        SP_FIRE_EVENT(EventType::WorldClear);
        m_context->GetSubsystem<Renderer>()->Clear();
        m_context->GetSubsystem<ResourceCache>()->Clear();

        // Clear the entities
        m_entities.clear();

        m_resolve = true;
    }

    // Removes an entity and all of it's children
    void World::_EntityRemove(const std::shared_ptr<Entity>& entity)
    {
        // Remove any descendants
        auto children = entity->GetTransform()->GetChildren();
        for (const auto& child : children)
        {
            EntityRemove(child->GetEntity()->GetPtrShared());
        }

        // Keep a reference to it's parent (in case it has one)
        auto parent = entity->GetTransform()->GetParent();

        // Remove this entity
        for (auto it = m_entities.begin(); it < m_entities.end();)
        {
            const auto temp = *it;
            if (temp->GetId() == entity->GetId())
            {
                it = m_entities.erase(it);
                break;
            }
            ++it;
        }

        // If there was a parent, update it
        if (parent)
        {
            parent->AcquireChildren();
        }
    }

    shared_ptr<Entity> World::CreateEnvironment()
    {
        shared_ptr<Entity> environment = EntityCreate();
        environment->SetName("Environment");
        environment->AddComponent<Environment>()->LoadDefault();

        return environment;
    }

    shared_ptr<Entity> World::CreateCamera()
    {
        ResourceCache* resource_cache   = m_context->GetSubsystem<ResourceCache>();
        const string dir_scripts        = resource_cache->GetResourceDirectory(ResourceDirectory::Scripts) + "/";

        shared_ptr<Entity> entity = EntityCreate();
        entity->SetName("Camera");
        entity->AddComponent<Camera>();
        entity->AddComponent<AudioListener>();
        entity->GetTransform()->SetPositionLocal(Vector3(0.0f, 1.0f, -5.0f));

        return entity;
    }

    shared_ptr<Entity> World::CreateDirectionalLight()
    {
        shared_ptr<Entity> light = EntityCreate();
        light->SetName("DirectionalLight");
        light->GetTransform()->SetRotationLocal(Quaternion::FromEulerAngles(30.0f, 30.0, 0.0f));
        light->GetTransform()->SetPosition(Vector3(0.0f, 10.0f, 0.0f));

        auto light_comp = light->AddComponent<Light>();
        light_comp->SetLightType(LightType::Directional);

        return light;
    }
}
