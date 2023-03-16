/*
Copyright(c) 2016-2023 Panos Karabelas

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
#include "Components/Terrain.h"
#include "../Resource/ResourceCache.h"
#include "../IO/FileStream.h"
#include "../Profiling/Profiler.h"
#include "../Input/Input.h"
#include "../Core/ProgressTracker.h"
#include "../Core/ThreadPool.h"
#include "../Rendering/Renderer.h"
#include "../RHI/RHI_Device.h"
#include "../RHI/RHI_Texture2D.h"
#include "../Rendering/Mesh.h"
//==========================================

//= NAMESPACES ================
using namespace std;
using namespace Spartan::Math;
//=============================

namespace Spartan
{
    static std::vector<std::shared_ptr<Entity>> m_entities_to_add;
    static std::vector<std::shared_ptr<Entity>> m_entities;
    static std::string m_name;
    static std::string m_file_path;
    static bool m_was_in_editor_mode                             = false;
    static bool m_resolve                                        = true;
    static std::shared_ptr<Mesh> m_default_model_sponza          = nullptr;
    static std::shared_ptr<Mesh> m_default_model_sponza_curtains = nullptr;
    static std::shared_ptr<Mesh> m_default_model_car             = nullptr;
    
    // Sync primitives
    static std::mutex m_entity_access_mutex;

    void World::Initialize()
    {
        SP_SUBSCRIBE_TO_EVENT(EventType::WorldResolve, SP_EVENT_HANDLER_EXPRESSION_STATIC
        (
            m_resolve = true;
        ));
    }

    void World::Shutdown()
    {
        m_entities.clear();
    }

    void World::PreTick()
    {
        for (shared_ptr<Entity>& entity : m_entities)
        {
            entity->OnPreTick();
        }
    }

    void World::Tick()
    {
        lock_guard lock(m_entity_access_mutex);

        SP_PROFILE_FUNCTION();

        // Tick entities
        {
            // Detect game toggling
            const bool started   =  Engine::IsFlagSet(EngineMode::Game) &&  m_was_in_editor_mode;
            const bool stopped   = !Engine::IsFlagSet(EngineMode::Game) && !m_was_in_editor_mode;
            m_was_in_editor_mode = !Engine::IsFlagSet(EngineMode::Game);

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
                entity->Tick();
            }
        }

        if (m_resolve || !m_entities_to_add.empty())
        {
            // Remove entities
            vector<shared_ptr<Entity>> entities_copy = m_entities;
            for (shared_ptr<Entity>& entity : entities_copy)
            {
                if (entity->IsPendingDestruction())
                {
                    _EntityRemove(entity.get());
                }
            }

            // Add entities
            auto it = m_entities_to_add.begin();
            while (it != m_entities_to_add.end())
            {
                if ((*it)->IsActive())
                {
                    m_entities.emplace_back(*it);
                    it = m_entities_to_add.erase(it);
                }
                else
                {
                    ++it;
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
        CreateDefaultWorldSponza();
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
        SP_LOG_INFO("World \"%s\" has been saved. Duration %.2f ms", m_file_path.c_str(), timer.GetElapsedTimeMs());

        // Notify subsystems waiting for us to finish
        SP_FIRE_EVENT(EventType::WorldSavedEnd);

        return true;
    }

    bool World::LoadFromFile(const string& file_path)
    {
        if (!FileSystem::Exists(file_path))
        {
            SP_LOG_ERROR("\"%s\" was not found.", file_path.c_str());
            return false;
        }

        // Open file
        unique_ptr<FileStream> file = make_unique<FileStream>(file_path, FileStream_Read);
        if (!file->IsOpen())
        {
            SP_LOG_ERROR("Failed to open \"%s\"", file_path.c_str());
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
            shared_ptr<Entity> entity = CreateEntity();
            entity->SetObjectId(file->ReadAs<uint64_t>());
        }

        // Serialize root entities
        for (uint32_t i = 0; i < root_entity_count; i++)
        {
            m_entities[i]->Deserialize(file.get(), nullptr);
            ProgressTracker::GetProgress(ProgressType::World).JobDone();
        }

        // Report time
        SP_LOG_INFO("World \"%s\" has been loaded. Duration %.2f ms", m_file_path.c_str(), timer.GetElapsedTimeMs());

        SP_FIRE_EVENT(EventType::WorldLoadEnd);

        return true;
    }

    void World::Resolve()
    {
        m_resolve = true;
    }
    
    shared_ptr<Entity> World::CreateEntity(bool is_active /*= true*/)
    {
        lock_guard lock(m_entity_access_mutex);

        shared_ptr<Entity> entity = m_entities_to_add.emplace_back(make_shared<Entity>());
        entity->SetActive(is_active);

        return entity;
    }

    bool World::EntityExists(Entity* entity)
    {
        SP_ASSERT_MSG(entity != nullptr, "Entity is null");
        return GetEntityById(entity->GetObjectId()) != nullptr;
    }

    void World::RemoveEntity(Entity* entity)
    {
        SP_ASSERT_MSG(entity != nullptr, "Entity is null");
        entity->MarkForDestruction(); // delayed destruction in case the Renderer is using it
        m_resolve = true;
    }

    vector<shared_ptr<Entity>> World::GetRootEntities()
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

    const shared_ptr<Entity>& World::GetEntityByName(const string& name)
    {
        for (shared_ptr<Entity>& entity : m_entities)
        {
            if (entity->GetName() == name)
                return entity;
        }

        static shared_ptr<Entity> empty;
        return empty;
    }

    const shared_ptr<Entity>& World::GetEntityById(const uint64_t id)
    {
        for (const auto& entity : m_entities)
        {
            if (entity->GetObjectId() == id)
                return entity;
        }

        static shared_ptr<Entity> empty;
        return empty;
    }

    const std::vector<std::shared_ptr<Entity>>& World::GetAllEntities()
    {
        return m_entities;
    }
    
    void World::ActivateNewEntities()
    {
        lock_guard lock(m_entity_access_mutex);

        for (shared_ptr<Entity>& entity : m_entities_to_add)
        {
            entity->SetActive(true);
        }
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
    void World::_EntityRemove(Entity* entity)
    {
        // Remove any descendants
        auto children = entity->GetTransform()->GetChildren();
        for (const auto& child : children)
        {
            RemoveEntity(child->GetEntity());
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

    void World::CreateDefaultWorldCommon(const Math::Vector3& camera_position, const Math::Vector3& camera_rotation)
    {
        // Environment
        {
            shared_ptr<Entity> environment = CreateEntity();
            environment->SetName("environment");
            environment->AddComponent<Environment>();
        }

        // Camera
        {
            shared_ptr<Entity> entity = CreateEntity();
            entity->SetName("camera");

            entity->AddComponent<Camera>();
            entity->AddComponent<AudioListener>();
            entity->GetTransform()->SetPosition(camera_position);
            entity->GetTransform()->SetRotation(Quaternion::FromEulerAngles(camera_rotation));
        }

        // Light - Directional
        {
            shared_ptr<Entity> entity = CreateEntity();
            entity->SetName("light_directional");

            entity->GetTransform()->SetPosition(Vector3(0.0f, 10.0f, 0.0f));
            entity->GetTransform()->SetRotation(Quaternion::FromEulerAngles(112.37f, -60.91f, 0.0f));

            Light* light = entity->AddComponent<Light>();
            light->SetLightType(LightType::Directional);
            light->SetColor(Color::light_sky_clear);
            light->SetIntensity(50000.0f);
        }

        // Music
        {
            shared_ptr<Entity> entity = CreateEntity();
            entity->SetName("audio_source");

            AudioSource* audio_source = entity->AddComponent<AudioSource>();
            audio_source->SetAudioClip("project\\music\\vangelis_cosmos_theme.mp3");
            audio_source->SetLoop(true);
        }
    }

    void World::CreateDefaultWorldCube()
    {
        CreateDefaultWorldCommon();

        // Quad
        {
            // Create entity
            shared_ptr<Entity> entity = CreateEntity();
            entity->SetName("quad");
            entity->GetTransform()->SetPosition(Vector3(0.0f, 0.1f, 0.0f)); // raise a bit to avoid z-fighting with world grid
            entity->GetTransform()->SetScale(Vector3(4.0f, 1.0f, 4.0f));

            // Add a renderable component
            Renderable* renderable = entity->AddComponent<Renderable>();
            renderable->SetGeometry(DefaultGeometry::Quad);
            renderable->SetDefaultMaterial();

            // Add physics components
            RigidBody* rigid_body = entity->AddComponent<RigidBody>();
            rigid_body->SetMass(0.0f); // make it static/immovable
            rigid_body->SetFriction(0.5f);
            rigid_body->SetRestitution(0.2f);
            Collider* collider = entity->AddComponent<Collider>();
            collider->SetShapeType(ColliderShape::StaticPlane); // set shape
        }

        // Cube
        {
            // Create entity
            shared_ptr<Entity> entity = CreateEntity();
            entity->SetName("cube");
            entity->GetTransform()->SetPosition(Vector3(0.0f, 4.0f, 0.0f));

            // Add a renderable component
            Renderable* renderable = entity->AddComponent<Renderable>();
            renderable->SetGeometry(DefaultGeometry::Cube);
            renderable->SetDefaultMaterial();

            // Add physics components
            RigidBody* rigid_body = entity->AddComponent<RigidBody>();
            rigid_body->SetMass(1.0f); // give it some mass
            rigid_body->SetRestitution(1.0f);
            rigid_body->SetFriction(0.2f);
            Collider* collider = entity->AddComponent<Collider>();
            collider->SetShapeType(ColliderShape::Box); // set shape
        }

        // Start simulating (for the physics and the music to work)
        Engine::SetFlag(EngineMode::Game);
    }

    void World::CreateDefaultWorldCar()
    {
        Vector3 camera_position = Vector3(-2.8436f, 1.6070f, -2.6946f);
        Vector3 camera_rotation = Vector3(18.7975f, 37.3995f, 0.0f);
        CreateDefaultWorldCommon(camera_position, camera_rotation);

        // Quad
        shared_ptr<Entity> entity = CreateEntity();
        entity->SetName("quad");
        entity->GetTransform()->SetPosition(Vector3(0.0f, 0.1f, 0.0f)); // raise a bit to avoid z-fighting with world grid
        entity->GetTransform()->SetScale(Vector3(8.0f, 1.0f, 8.0f));
        Renderable* renderable = entity->AddComponent<Renderable>();
        renderable->SetGeometry(DefaultGeometry::Quad);
        renderable->SetDefaultMaterial();

        if (m_default_model_car = ResourceCache::Load<Mesh>("project\\models\\toyota_ae86_sprinter_trueno_zenki\\scene.gltf"))
        {
            Entity* entity = m_default_model_car->GetRootEntity();
            entity->SetName("car");

            entity->GetTransform()->SetPosition(Vector3(0.0f, 0.05f, 0.0f));
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

        // Start simulating (for the physics and the music to work)
        Engine::SetFlag(EngineMode::Game);
    }

    void World::CreateDefaultWorldTerrain()
    {
        CreateDefaultWorldCommon();

        // Terrain
        {
            shared_ptr<Entity> entity = CreateEntity();
            entity->SetName("terrain");

            entity->GetTransform()->SetPosition(Vector3(0.0f, -6.5f, 0.0f));

            shared_ptr<RHI_Texture2D> height_map = make_shared<RHI_Texture2D>(RHI_Texture_Srv, "height_map");
            height_map->LoadFromFile("project\\height_maps\\a.png");

            Terrain* terrain = entity->AddComponent<Terrain>();
            terrain->SetMinY(0.0f);
            terrain->SetMaxY(12.0f);
            terrain->SetHeightMap(height_map);
            terrain->GenerateAsync();
        }

        // Start simulating (for the physics and the music to work)
        Engine::SetFlag(EngineMode::Game);
    }

    void World::CreateDefaultWorldSponza()
    {
        Vector3 camera_position = Vector3(-10.4144f, 7.3257f, -1.1735f);
        Vector3 camera_rotation = Vector3(-6.0022f, 88.3969f, 0.0f);
        CreateDefaultWorldCommon(camera_position, camera_rotation);

        // 3D model - Sponza
        if (m_default_model_sponza = ResourceCache::Load<Mesh>("project\\models\\sponza\\main\\NewSponza_Main_Blender_glTF.gltf"))
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

            // Delete dirt decals since they look bad.
            // They are hovering over the surfaces, to avoid z-fighting, and they also cast shadows underneath them.
            RemoveEntity(entity->GetTransform()->GetDescendantByName("decals_1st_floor"));
            RemoveEntity(entity->GetTransform()->GetDescendantByName("decals_2nd_floor"));
            RemoveEntity(entity->GetTransform()->GetDescendantByName("decals_3rd_floor"));

            // 3D model - Sponza curtains
            if (m_default_model_sponza_curtains = ResourceCache::Load<Mesh>("project\\models\\sponza\\curtains\\NewSponza_Curtains_glTF.gltf"))
            {
                Entity* entity = m_default_model_sponza_curtains->GetRootEntity();
                entity->SetName("sponza_curtains");
                entity->GetTransform()->SetPosition(Vector3(0.0f, 0.06f, 0.0f));
                entity->GetTransform()->SetScale(Vector3::One);
            }
        }

        // Start simulating (for the physics and the music to work)
        Engine::SetFlag(EngineMode::Game);
    }

    const string World::GetName()
    {
        return m_name;
    }

    const string& World::GetFilePath()
    {
        return m_file_path;
    }
}
