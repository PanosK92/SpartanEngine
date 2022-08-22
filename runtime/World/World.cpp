/*
Copyright(c) 2016-2022 Panos Karabelas

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

//= INCLUDES ===============================
#include "pch.h"
#include "World.h"
#include "Entity.h"
#include "Components/Transform.h"
#include "Components/Camera.h"
#include "Components/Light.h"
#include "Components/Environment.h"
#include "Components/AudioListener.h"
#include "Components/Renderable.h"
#include "Components/AudioSource.h"
#include "Components/RigidBody.h"
#include "Components/Collider.h"
#include "TransformHandle/TransformHandle.h"
#include "../Resource/ResourceCache.h"
#include "../Resource/ProgressTracker.h"
#include "../IO/FileStream.h"
#include "../Profiling/Profiler.h"
#include "../Input/Input.h"
#include "../RHI/RHI_Device.h"
#include "../Rendering/Renderer.h"
#include "../Threading/Threading.h"
//==========================================

//= NAMESPACES ================
using namespace std;
using namespace Spartan::Math;
//=============================

namespace Spartan
{
    World::World(Context* context) : Subsystem(context)
    {
        SP_SUBSCRIBE_TO_EVENT(EventType::WorldResolve, [this](Variant) { m_resolve = true; });
    }

    World::~World()
    {
        m_input    = nullptr;
        m_profiler = nullptr;
    }

    void World::OnInitialise()
    {
        m_input    = m_context->GetSubsystem<Input>();
        m_profiler = m_context->GetSubsystem<Profiler>();
    }

    void World::OnPostInitialise()
    {
        m_transform_handle = make_shared<TransformHandle>(m_context);

        CreateDefaultWorld();
    }

    void World::OnPreTick()
    {
        for (shared_ptr<Entity>& entity : m_entities)
        {
            entity->OnPreTick();
        }
    }

    void World::OnTick(double delta_time)
    {
        // If something is being loaded, don't tick as entities are probably being added
        if (IsLoading())
            return;

        SCOPED_TIME_BLOCK(m_profiler);

        if (Renderer* renderer = m_context->GetSubsystem<Renderer>())
        {
            if (renderer->GetOption<bool>(RendererOption::Debug_TransformHandle))
            {
                m_transform_handle->Tick(renderer->GetCamera().get(), m_gizmo_transform_size);
            }
        }

        // Tick entities
        {
            // Detect game toggling
            const bool started   = m_context->m_engine->IsFlagSet(EngineMode::Game)  && m_was_in_editor_mode;
            const bool stopped   = !m_context->m_engine->IsFlagSet(EngineMode::Game) && !m_was_in_editor_mode;
            m_was_in_editor_mode = !m_context->m_engine->IsFlagSet(EngineMode::Game);

            // Start
            if (started)
            {
                for (shared_ptr<Entity>& entity : m_entities)
                {
                    entity->OnStart();
                }
            }

            // Stop
            if (stopped)
            {
                for (shared_ptr<Entity>& entity : m_entities)
                {
                    entity->OnStop();
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
                vector<shared_ptr<Entity>> entities_copy = m_entities;

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

        UpdateDefaultWorld(delta_time);
    }

    void World::New()
    {
        Clear();
        CreateDefaultWorld();
    }

    bool World::SaveToFile(const string& filePathIn)
    {
        // Add scene file extension to the filepath if it's missing
        auto file_path = filePathIn;
        if (FileSystem::GetExtensionFromFilePath(file_path) != EXTENSION_WORLD)
        {
            file_path += EXTENSION_WORLD;
        }

        m_name      = FileSystem::GetFileNameWithoutExtensionFromFilePath(file_path);
        m_file_path = file_path;

        // Notify subsystems that need to save data
        SP_FIRE_EVENT(EventType::WorldSaveStart);

        // Create a prefab file
        auto file = make_unique<FileStream>(file_path, FileStream_Write);
        if (!file->IsOpen())
        {
            LOG_ERROR("Failed to open file.");
            return false;
        }

        // Only save root entities as they will also save their descendants
        vector<shared_ptr<Entity>> root_actors = EntityGetRoots();
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
        LOG_INFO("World \"%s\" has been saved. Duration %.2f ms", m_file_path.c_str(), timer.GetElapsedTimeMs());

        // Notify subsystems waiting for us to finish
        SP_FIRE_EVENT(EventType::WorldSavedEnd);

        return true;
    }

    bool World::LoadFromFile(const string& file_path)
    {
        if (!FileSystem::Exists(file_path))
        {
            LOG_ERROR("\"%s\" was not found.", file_path.c_str());
            return false;
        }

        // Open file
        unique_ptr<FileStream> file = make_unique<FileStream>(file_path, FileStream_Read);
        if (!file->IsOpen())
        {
            LOG_ERROR("Failed to open \"%s\"", file_path.c_str());
            return false;
        }

        // Clear current entities
        Clear();

        m_name      = FileSystem::GetFileNameWithoutExtensionFromFilePath(file_path);
        m_file_path = file_path;

        // Notify subsystems that need to load data
        SP_FIRE_EVENT(EventType::WorldLoadStart);

        // Load root entity count
        const uint32_t root_entity_count = file->ReadAs<uint32_t>();

        // Start progress tracking and timing
        ProgressTracker::GetProgress(ProgressType::World).Start(root_entity_count, "Loading world...");
        const Stopwatch timer;

        // Load root entity IDs
        for (uint32_t i = 0; i < root_entity_count; i++)
        {
            shared_ptr<Entity> entity = EntityCreate();
            entity->SetObjectId(file->ReadAs<uint64_t>());
        }

        // Serialize root entities
        for (uint32_t i = 0; i < root_entity_count; i++)
        {
            m_entities[i]->Deserialize(file.get(), nullptr);
            ProgressTracker::GetProgress(ProgressType::World).JobDone();
        }

        // Report time
        LOG_INFO("World \"%s\" has been loaded. Duration %.2f ms", m_file_path.c_str(), timer.GetElapsedTimeMs());

        SP_FIRE_EVENT(EventType::WorldLoadEnd);

        return true;
    }

    bool World::IsLoading()
    {
        const bool is_loading_model = ProgressTracker::GetProgress(ProgressType::ModelImporter).IsLoading();
        const bool is_loading_scene = ProgressTracker::GetProgress(ProgressType::World).IsLoading();
        return is_loading_model || is_loading_scene;
    }

    shared_ptr<Entity> World::EntityCreate(bool is_active /*= true*/)
    {
        lock_guard lock(m_mutex_create_entity);

        shared_ptr<Entity> entity = m_entities.emplace_back(make_shared<Entity>(m_context));
        entity->SetActive(is_active);
        return entity;
    }

    bool World::EntityExists(const shared_ptr<Entity>& entity)
    {
        if (!entity)
            return false;

        return EntityGetById(entity->GetObjectId()) != nullptr;
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
        for (const shared_ptr<Entity>& entity : m_entities)
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
        for (shared_ptr<Entity>& entity : m_entities)
        {
            if (entity->GetName() == name)
                return entity;
        }

        static shared_ptr<Entity> empty;
        return empty;
    }

    const shared_ptr<Entity>& World::EntityGetById(const uint64_t id)
    {
        for (const auto& entity : m_entities)
        {
            if (entity->GetObjectId() == id)
                return entity;
        }

        static shared_ptr<Entity> empty;
        return empty;
    }

    void World::Clear()
    {
        // Fire event
        SP_FIRE_EVENT(EventType::WorldClear);

        // Clear
        m_entities.clear();
        m_name.clear();
        m_file_path.clear();

        // Mark for resolve
        m_resolve = true;
    }

    // Removes an entity and all of it's children
    void World::_EntityRemove(const shared_ptr<Entity>& entity)
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
            if (temp->GetObjectId() == entity->GetObjectId())
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

    void World::CreateDefaultWorld()
    {
        // Environment
        {
            shared_ptr<Entity> environment = EntityCreate();
            environment->SetName("environment");
            environment->AddComponent<Environment>();
        }

        // Camera
        {
            shared_ptr<Entity> entity = EntityCreate();
            entity->SetName("camera");

            entity->AddComponent<Camera>();
            entity->AddComponent<AudioListener>();
            entity->GetTransform()->SetPosition(Vector3(-2.6660f, 1.9089f, 1.9960f));
            entity->GetTransform()->SetRotation(Quaternion::FromEulerAngles(Vector3(-0.0015f, 105.8024f, 0.0f)));
        }

        // Light - Directional
        {
            shared_ptr<Entity> entity = EntityCreate();
            entity->SetName("light_directional");

            entity->GetTransform()->SetPosition(Vector3(0.0f, 10.0f, 0.0f));
            entity->GetTransform()->SetRotation(Quaternion::FromEulerAngles(112.3700f, -60.9100f, 0.0f));

            Light* light = entity->AddComponent<Light>();
            light->SetLightType(LightType::Directional);
            light->SetColor(Color::light_sky_sunrise);
            light->SetIntensity(120000.0f);
        }

        // Light - Point
        {
            shared_ptr<Entity> entity = EntityCreate();
            entity->SetName("light_point");

            entity->GetTransform()->SetPosition(Vector3(15.7592f, 3.1752f, 0.9272f));

            Light* light = entity->AddComponent<Light>();
            light->SetLightType(LightType::Point);
            light->SetColor(Color::material_skin_1); // weird, I know
            light->SetIntensity(8500.0f);
            light->SetRange(25.0f);
            light->SetShadowsTransparentEnabled(false);
        }

        // Music
        {
            shared_ptr<Entity> entity = EntityCreate();
            entity->SetName("audio_source");

            AudioSource* audio_source = entity->AddComponent<AudioSource>();
            audio_source->SetAudioClip("project\\music\\vangelis_alpha.mp3");
            audio_source->SetLoop(true);
        }

        // Asset directories
        ResourceCache* resource_cache = m_context->GetSubsystem<ResourceCache>();

        // 3D model - Car
        if (m_default_model_car = resource_cache->Load<Model>("project\\models\\toyota_ae86_sprinter_trueno_zenki\\scene.gltf"))
        {
            Entity* entity = m_default_model_car->GetRootEntity();
            entity->SetName("car");
        
            entity->GetTransform()->SetPosition(Vector3(15.5200f, -0.6100f, 0.0300f));
            entity->GetTransform()->SetRotation(Quaternion::FromEulerAngles(90.0f, -4.8800f, -95.0582f));
            entity->GetTransform()->SetScale(Vector3(0.0125f, 0.0125f, 0.0125f));
        
            // Break calipers have a wrong rotation, probably a bug with sketchfab auto converting to gltf
            entity->GetTransform()->GetDescendantByName("FR_Caliper_BrakeCaliper_0")->GetTransform()->SetRotationLocal(Quaternion::FromEulerAngles(0.0f, 75.0f, 0.0f));
            entity->GetTransform()->GetDescendantByName("RR_Caliper_BrakeCaliper_0")->GetTransform()->SetRotationLocal(Quaternion::FromEulerAngles(0.0f, 75.0f, 0.0f));
        
            // body
            {
                // metal - make it aluminum
                if (Material* material = entity->GetTransform()->GetDescendantByName("CarBody_Primary_0")->GetRenderable()->GetMaterial())
                {
                    material->SetColor(Color::material_aluminum);
                    material->SetProperty(MaterialProperty::RoughnessMultiplier, 0.1f);
                    material->SetProperty(MaterialProperty::MetallnessMultiplier, 0.15f);
                }
        
                // plastic
                {
                    if (Material* material = entity->GetTransform()->GetDescendantByName("CarBody_Secondary_0")->GetRenderable()->GetMaterial())
                    {
                        material->SetColor(Color::material_tire);
                        material->SetProperty(MaterialProperty::RoughnessMultiplier, 0.35f);
                    }
        
                    if (Material* material = entity->GetTransform()->GetDescendantByName("CarBody_Trim1_0")->GetRenderable()->GetMaterial())
                    {
                        material->SetColor(Color::material_tire);
                        material->SetProperty(MaterialProperty::RoughnessMultiplier, 0.35f);
                    }
                }
            }
        
            // interior
            {
                if (Material* material = entity->GetTransform()->GetDescendantByName("Interior_InteriorPlastic_0")->GetRenderable()->GetMaterial())
                {
                    material->SetColor(Color::material_tire);
                    material->SetProperty(MaterialProperty::RoughnessMultiplier, 0.7f);
                    material->SetProperty(MaterialProperty::MetallnessMultiplier, 0.0f);
                }
        
                if (Material* material = entity->GetTransform()->GetDescendantByName("Interior_InteriorPlastic2_0")->GetRenderable()->GetMaterial())
                {
                    material->SetColor(Color::material_tire);
                    material->SetProperty(MaterialProperty::RoughnessMultiplier, 0.7f);
                    material->SetProperty(MaterialProperty::MetallnessMultiplier, 0.0f);
                }
       
            }
        
            // lights
            {
                if (Material* material = entity->GetTransform()->GetDescendantByName("CarBody_LampCovers_0")->GetRenderable()->GetMaterial())
                {
                    material->SetColor(Color::material_glass);
                    material->SetProperty(MaterialProperty::RoughnessMultiplier, 0.2f);
                    material->SetTexture(MaterialTexture::Emission, material->GetTexture_PtrShared(MaterialTexture::Color));
                }
        
                // plastic covers
                if (Material* material = entity->GetTransform()->GetDescendantByName("Headlights_Trim2_0")->GetRenderable()->GetMaterial())
                {
                    material->SetProperty(MaterialProperty::RoughnessMultiplier, 0.35f);
                    material->SetColor(Color::material_tire);
                }
            }
        
            // wheels
            {
                // brake caliper
                if (Material* material = entity->GetTransform()->GetDescendantByName("FR_Caliper_BrakeCaliper_0")->GetRenderable()->GetMaterial())
                {
                    material->SetColor(Color::material_aluminum);
                    material->SetProperty(MaterialProperty::RoughnessMultiplier, 0.5f);
                    material->SetProperty(MaterialProperty::MetallnessMultiplier, 1.0f);
                    material->SetProperty(MaterialProperty::Anisotropic, 1.0f);
                    material->SetProperty(MaterialProperty::AnisotropicRotation, 0.5f);
                }
        
                // tires
                if (Material* material = entity->GetTransform()->GetDescendantByName("FL_Wheel_TireMaterial_0")->GetRenderable()->GetMaterial())
                {
                    material->SetColor(Color::material_tire);
                    material->SetProperty(MaterialProperty::RoughnessMultiplier, 0.5f);
                    material->SetProperty(MaterialProperty::MetallnessMultiplier, 0.0f);
                }
        
                // rims
                if (Material* material = entity->GetTransform()->GetDescendantByName("FR_Wheel_RimMaterial_0")->GetRenderable()->GetMaterial())
                {
                    material->SetColor(Color::material_aluminum);
                    material->SetProperty(MaterialProperty::RoughnessMultiplier, 0.5f);
                    material->SetProperty(MaterialProperty::RoughnessMultiplier, 0.5f);
                    material->SetProperty(MaterialProperty::MetallnessMultiplier, 1.0f);
                }
            }
        }

        // 3D model - Sponza
        if (m_default_model_sponza = resource_cache->Load<Model>("project\\models\\sponza\\main\\NewSponza_Main_Blender_glTF.gltf"))
        {
            Entity* entity = m_default_model_sponza->GetRootEntity();
            entity->SetName("sponza");
            entity->GetTransform()->SetPosition(Vector3(0.0f, 0.06f, 0.0f));
            entity->GetTransform()->SetScale(Vector3::One);

            // Make the lamp frame not cast shadows, so we can place a light within it
            if (Renderable* renderable = entity->GetTransform()->GetDescendantByName("lamp_1stfloor_entrance_1")->GetRenderable())
            {
                renderable->SetCastShadows(false);
            }

            // Make the dirt decal fully rough
            if (Material* material = entity->GetTransform()->GetDescendantByName("decals_1st_floor")->GetRenderable()->GetMaterial())
            {
                material->SetProperty(MaterialProperty::RoughnessMultiplier, 1.0f);
            }

            // 3D model - Sponza curtains
            if (m_default_model_sponza_curtains = resource_cache->Load<Model>("project\\models\\sponza\\curtains\\NewSponza_Curtains_glTF.gltf"))
            {
                Entity* entity = m_default_model_sponza_curtains->GetRootEntity();
                entity->SetName("sponza_curtains");
                entity->GetTransform()->SetPosition(Vector3(0.0f, 0.06f, 0.0f));
                entity->GetTransform()->SetScale(Vector3::One);
            }
        }
    }

    void World::UpdateDefaultWorld(double delta_time)
    {
        // Play!
        if (!m_default_world_started)
        {
            m_context->m_engine->ToggleFlag(EngineMode::Game);
            m_default_world_started = true;
        }
    }
}
