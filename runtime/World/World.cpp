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

//= INCLUDES =========================
#include "pch.h"
#include "World.h"
#include "Entity.h"
#include "Components/Transform.h"
#include "Components/Camera.h"
#include "Components/Light.h"
#include "Components/Environment.h"
#include "Components/AudioListener.h"
#include "Components/AudioSource.h"
#include "Components/RigidBody.h"
#include "Components/Collider.h"
#include "Components/Terrain.h"
#include "../Resource/ResourceCache.h"
#include "../IO/FileStream.h"
#include "../Profiling/Profiler.h"
#include "../RHI/RHI_Texture2D.h"
#include "../Rendering/Mesh.h"
#include "../Rendering/Renderer.h"
//====================================

//= NAMESPACES ================
using namespace std;
using namespace Spartan::Math;
//=============================

namespace Spartan
{
    namespace
    {
        static vector<shared_ptr<Entity>> m_entities;
        static string m_name;
        static string m_file_path;
        static bool m_resolve                                   = false;
        static bool m_was_in_editor_mode                        = false;
        static shared_ptr<Entity> m_default_environment         = nullptr;
        static shared_ptr<Entity> m_default_model_floor         = nullptr;
        static shared_ptr<Mesh> m_default_model_sponza          = nullptr;
        static shared_ptr<Mesh> m_default_model_sponza_curtains = nullptr;
        static shared_ptr<Mesh> m_default_model_car             = nullptr;
        static shared_ptr<Mesh> m_default_model_helmet_flight   = nullptr;
        static shared_ptr<Mesh> m_default_model_helmet_damaged  = nullptr;
        static mutex m_entity_access_mutex;

        static void update_default_scene()
        {
            if (m_default_model_car)
            {
                if (Entity* entity = m_default_model_car->GetRootEntity()) // can be true when the entity is deleted
                {
                    // Rotate the car
                    float rotation_delta = 10.0f * static_cast<float>(Timer::GetDeltaTimeSmoothedSec()) * Helper::DEG_TO_RAD;
                    entity->GetTransform()->Rotate(Quaternion::FromAngleAxis(rotation_delta, Vector3::Forward));
                }
            }
        }

        static void create_default_world_common(
            const bool create_floor,
            const Math::Vector3& camera_position = Math::Vector3(-2.956f, 1.1474f, -2.9395f),
            const Math::Vector3& camera_rotation = Math::Vector3(15.9976f, 43.5998f, 0.0f),
            const LightIntensity sun_intensity   = LightIntensity::sky_sunlight_noon,
            const char* soundtrack_file_path     = "project\\music\\jake_chudnow_shona.mp3",
            const bool shadows_enabled           = true
        )
        {
            // Environment
            {
                m_default_environment = World::CreateEntity();
                m_default_environment->SetObjectName("environment");
                m_default_environment->AddComponent<Environment>();
            }

            // Camera
            {
                shared_ptr<Entity> entity = World::CreateEntity();
                entity->SetObjectName("camera");

                entity->AddComponent<Camera>();
                entity->AddComponent<AudioListener>();
                entity->GetTransform()->SetPosition(camera_position);
                entity->GetTransform()->SetRotation(Quaternion::FromEulerAngles(camera_rotation));
            }

            // Light - Directional
            {
                shared_ptr<Entity> entity = World::CreateEntity();
                entity->SetObjectName("light_directional");

                entity->GetTransform()->SetRotation(Quaternion::FromEulerAngles(35.0f, 90.0f, 0.0f));

                shared_ptr<Light> light = entity->AddComponent<Light>();
                light->SetLightType(LightType::Directional);
                light->SetColor(Color::light_sky_sunrise);
                light->SetIntensity(sun_intensity);
                light->SetShadowsEnabled(shadows_enabled ? (light->GetIntensityLumens() > 0.0f) : false);
            }

            // Music
            {
                shared_ptr<Entity> entity = World::CreateEntity();
                entity->SetObjectName("audio_source");

                shared_ptr<AudioSource> audio_source = entity->AddComponent<AudioSource>();
                audio_source->SetAudioClip(soundtrack_file_path);
                audio_source->SetLoop(true);
            }

            // Floor
            if (create_floor)
            {
                m_default_model_floor = World::CreateEntity();
                m_default_model_floor->SetObjectName("floor");
                m_default_model_floor->GetTransform()->SetPosition(Vector3(0.0f, 0.1f, 0.0f)); // raise it a bit to avoid z-fighting with world grid
                m_default_model_floor->GetTransform()->SetScale(Vector3(30.0f, 1.0f, 30.0f));

                // Add a renderable component
                shared_ptr<Renderable> renderable = m_default_model_floor->AddComponent<Renderable>();
                renderable->SetGeometry(Renderer::GetStandardMesh(Renderer_MeshType::Quad).get());
                renderable->SetDefaultMaterial();

                // Add physics components
                shared_ptr<RigidBody> rigid_body = m_default_model_floor->AddComponent<RigidBody>();
                rigid_body->SetMass(0.0f); // make it static/immovable
                rigid_body->SetFriction(0.5f);
                rigid_body->SetRestitution(0.2f);
                shared_ptr<Collider> collider = m_default_model_floor->AddComponent<Collider>();
                collider->SetShapeType(ColliderShape::StaticPlane); // set shape
            }
        }
    }

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
        m_default_environment           = nullptr;
        m_default_model_floor           = nullptr;
        m_default_model_sponza          = nullptr;
        m_default_model_sponza_curtains = nullptr;
        m_default_model_car             = nullptr;
        m_default_model_helmet_flight   = nullptr;
        m_default_model_helmet_damaged  = nullptr;
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
        SP_PROFILE_FUNCTION();

        lock_guard<mutex> lock(m_entity_access_mutex);

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

        // Notify Renderer
        if (m_resolve)
        {
            SP_FIRE_EVENT_DATA(EventType::WorldResolved, m_entities);
            m_resolve = false;
        }

        if (Engine::IsFlagSet(EngineMode::Game))
        {
            update_default_scene();
        }
    }

    void World::New()
    {
        Clear();
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
            static shared_ptr<Transform> empty;
            m_entities[i]->Deserialize(file.get(), empty);
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

    shared_ptr<Entity> World::CreateEntity()
    {
        lock_guard lock(m_entity_access_mutex);

        shared_ptr<Entity> entity = make_shared<Entity>();
        entity->Initialize();
        m_entities.emplace_back(entity);

        return entity;
    }

    bool World::EntityExists(Entity* entity)
    {
        SP_ASSERT_MSG(entity != nullptr, "Entity is null");
        return GetEntityById(entity->GetObjectId()) != nullptr;
    }

    void World::RemoveEntity(shared_ptr<Entity> entity_to_remove)
    {
        SP_ASSERT_MSG(entity_to_remove != nullptr, "Entity is null");

        lock_guard<mutex> lock(m_entity_access_mutex);

        // Remove the entity and all of its children
        {
            // Get the root entity and its descendants
            vector<Transform*> entities_to_remove;
            entities_to_remove.push_back(entity_to_remove->GetTransform().get());  // Add the root entity
            entity_to_remove->GetTransform()->GetDescendants(&entities_to_remove); // Get descendants 

            // Create a set containing the object IDs of entities to remove
            set<uint64_t> ids_to_remove;
            for (Transform* transform : entities_to_remove)
            {
                ids_to_remove.insert(transform->GetEntityPtr()->GetObjectId());
            }

            // Remove entities using a single loop
            m_entities.erase(remove_if(m_entities.begin(), m_entities.end(),
                [&](const shared_ptr<Entity>& entity)
                {
                    return ids_to_remove.count(entity->GetObjectId()) > 0;
                }),
                m_entities.end());

            // If there was a parent, update it
            if (Transform* parent = entity_to_remove->GetTransform()->GetParent())
            {
                parent->AcquireChildren();
            }
        }

        m_resolve = true;
    }

    vector<shared_ptr<Entity>> World::GetRootEntities()
    {
        lock_guard<mutex> lock(m_entity_access_mutex);

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
        lock_guard<mutex> lock(m_entity_access_mutex);

        for (shared_ptr<Entity>& entity : m_entities)
        {
            if (entity->GetObjectName() == name)
                return entity;
        }

        static shared_ptr<Entity> empty;
        return empty;
    }

    const shared_ptr<Entity>& World::GetEntityById(const uint64_t id)
    {
        lock_guard<mutex> lock(m_entity_access_mutex);

        for (const auto& entity : m_entities)
        {
            if (entity->GetObjectId() == id)
                return entity;
        }

        static shared_ptr<Entity> empty;
        return empty;
    }

    const vector<shared_ptr<Entity>>& World::GetAllEntities()
    {
        return m_entities;
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

    void World::CreateDefaultWorldPickablePhysicsCube()
    {
        create_default_world_common(true);

        // Cube
        {
            // Create entity
            shared_ptr<Entity> entity = CreateEntity();
            entity->SetObjectName("cube");
            entity->GetTransform()->SetPosition(Vector3(0.0f, 4.0f, 0.0f));

            // Load textures
            shared_ptr<RHI_Texture2D> tex_albedo = make_shared<RHI_Texture2D>();
            tex_albedo->LoadFromFile("project\\materials\\crate_space\\albedo.png");

            shared_ptr<RHI_Texture2D> tex_normal = make_shared<RHI_Texture2D>();
            tex_normal->LoadFromFile("project\\materials\\crate_space\\normal.png");

            shared_ptr<RHI_Texture2D> tex_occlusion = make_shared<RHI_Texture2D>();
            tex_occlusion->LoadFromFile("project\\materials\\crate_space\\ao.png");

            shared_ptr<RHI_Texture2D> tex_roughness = make_shared<RHI_Texture2D>();
            tex_roughness->LoadFromFile("project\\materials\\crate_space\\roughness.png");

            shared_ptr<RHI_Texture2D> tex_metalness = make_shared<RHI_Texture2D>();
            tex_metalness->LoadFromFile("project\\materials\\crate_space\\metallic.png");

            shared_ptr<RHI_Texture2D> tex_height = make_shared<RHI_Texture2D>();
            tex_height->LoadFromFile("project\\materials\\crate_space\\height.png");

            // Create material
            shared_ptr<Material> material = make_shared<Material>();
            material->SetTexture(MaterialTexture::Color, tex_albedo);
            material->SetTexture(MaterialTexture::Normal, tex_normal);
            material->SetTexture(MaterialTexture::Occlusion, tex_occlusion);
            material->SetTexture(MaterialTexture::Roughness, tex_roughness);
            material->SetTexture(MaterialTexture::Metalness, tex_metalness);
            material->SetTexture(MaterialTexture::Height, tex_height);

            // Create a file path for this material (required for the material to be able to be cached by the resource cache)
            const string file_path = "project\\materials\\crate_space" + string(EXTENSION_MATERIAL);
            material->SetResourceFilePath(file_path);

            // Add a renderable component
            shared_ptr<Renderable> renderable = entity->AddComponent<Renderable>();
            renderable->SetGeometry(Renderer::GetStandardMesh(Renderer_MeshType::Cube).get());
            renderable->SetMaterial(material);

            // Add physics components
            shared_ptr<RigidBody> rigid_body = entity->AddComponent<RigidBody>();
            rigid_body->SetMass(1.0f); // give it some mass
            rigid_body->SetRestitution(1.0f);
            rigid_body->SetFriction(0.2f);
            shared_ptr<Collider> collider = entity->AddComponent<Collider>();
            collider->SetShapeType(ColliderShape::Box); // set shape
        }

        // Start simulating (for the physics and the music to work)
        Engine::AddFlag(EngineMode::Game);
    }

    void World::CreateDefaultWorldHelmets()
    {
        Vector3 camera_position = Vector3(-1.1131f, 1.3112f, -1.8209f);
        Vector3 camera_rotation = Vector3(14.1965f, 43.3965f, 0.0f);
        create_default_world_common(true, camera_position, camera_rotation, LightIntensity::black_hole);

        // Point light
        {
            shared_ptr<Entity> entity = CreateEntity();
            entity->SetObjectName("light_point");
            entity->GetTransform()->SetPosition(Vector3(0.0f, 2.0f, -1.5f));

            shared_ptr<Light> light = entity->AddComponent<Light>();
            light->SetLightType(LightType::Point);
            light->SetColor(Color::light_light_bulb);
            light->SetIntensity(LightIntensity::bulb_500_watt);
        }

        // Flight helmet
        if (m_default_model_helmet_flight = ResourceCache::Load<Mesh>("project\\models\\flight_helmet\\FlightHelmet.gltf"))
        {
            Entity* entity = m_default_model_helmet_flight->GetRootEntity();
            entity->SetObjectName("flight_helmet");
            entity->GetTransform()->SetPosition(Vector3(0.0f, 0.1f, 0.0f));
            entity->GetTransform()->SetScale(Vector3(2.0f, 2.0f, 2.0f));
        }

        // Damaged helmet
        if (m_default_model_helmet_damaged = ResourceCache::Load<Mesh>("project\\models\\damaged_helmet\\DamagedHelmet.gltf"))
        {
            Entity* entity = m_default_model_helmet_damaged->GetRootEntity();
            entity->SetObjectName("damaged_helmet");
            entity->GetTransform()->SetPosition(Vector3(1.1713f, 0.4747f, -0.1711f));
            entity->GetTransform()->SetScale(Vector3(0.4343f, 0.4343f, 0.4343f));
        }

        // Start simulating (for the physics and the music to work)
        Engine::AddFlag(EngineMode::Game);
    }

    void World::CreateDefaultWorldCar()
    {
        Vector3 camera_position = Vector3(6.2f, 1.0f, -0.2f);
        Vector3 camera_rotation = Vector3(0.0f, -90.0f, 0.0f);
        create_default_world_common(true, camera_position, camera_rotation, LightIntensity::sky_twilight, "project\\music\\isola_any_day.mp3");

        // Point light - side of car
        {
            shared_ptr<Entity> entity = CreateEntity();
            entity->SetObjectName("light_point_side");
            entity->GetTransform()->SetPosition(Vector3(4.0f, 2.5, 3.0f));

            shared_ptr<Light> light = entity->AddComponent<Light>();
            light->SetLightType(LightType::Point);
            light->SetColor(Color::light_photo_flash);
            light->SetIntensity(LightIntensity::bulb_500_watt);
        }

        // Environment
        {
            m_default_environment->GetComponent<Environment>()->SetFromTextureSphere("project\\environment\\kloppenheim_05_4k.hdr");
        }

        // Load floor material
        {
            // Load textures
            shared_ptr<RHI_Texture2D> tex_albedo = make_shared<RHI_Texture2D>();
            tex_albedo->LoadFromFile("project\\materials\\tile_black\\albedo.png");

            shared_ptr<RHI_Texture2D> tex_normal = make_shared<RHI_Texture2D>();
            tex_normal->LoadFromFile("project\\materials\\tile_black\\normal.png");

            shared_ptr<RHI_Texture2D> tex_occlusion = make_shared<RHI_Texture2D>();
            tex_occlusion->LoadFromFile("project\\materials\\tile_black\\ao.png");

            shared_ptr<RHI_Texture2D> tex_roughness = make_shared<RHI_Texture2D>();
            tex_roughness->LoadFromFile("project\\materials\\tile_black\\roughness.png");

            shared_ptr<RHI_Texture2D> tex_metalness = make_shared<RHI_Texture2D>();
            tex_metalness->LoadFromFile("project\\materials\\tile_black\\metallic.png");

            shared_ptr<RHI_Texture2D> tex_height = make_shared<RHI_Texture2D>();
            tex_height->LoadFromFile("project\\materials\\tile_black\\height.png");

            // Create material
            shared_ptr<Material> material = make_shared<Material>();
            material->SetTexture(MaterialTexture::Color, tex_albedo);
            material->SetTexture(MaterialTexture::Normal, tex_normal);
            material->SetTexture(MaterialTexture::Occlusion, tex_occlusion);
            material->SetTexture(MaterialTexture::Roughness, tex_roughness);
            material->SetTexture(MaterialTexture::Metalness, tex_metalness);
            material->SetTexture(MaterialTexture::Height, tex_height);
            material->SetProperty(MaterialProperty::UvTilingX, 10.0f);
            material->SetProperty(MaterialProperty::UvTilingY, 10.0f);

            // Create a file path for this material (required for the material to be able to be cached by the resource cache)
            const string file_path = "project\\materials\\tile_black" + string(EXTENSION_MATERIAL);
            material->SetResourceFilePath(file_path);

            // Set material
            m_default_model_floor->GetComponent<Renderable>()->SetMaterial(material);
        }

        if (m_default_model_car = ResourceCache::Load<Mesh>("project\\models\\toyota_ae86_sprinter_trueno_zenki\\scene.gltf"))
        {
            Entity* entity = m_default_model_car->GetRootEntity();
            entity->SetObjectName("car");

            entity->GetTransform()->SetPosition(Vector3(0.0f, 0.07f, 0.0f));
            entity->GetTransform()->SetRotation(Quaternion::FromEulerAngles(90.0f, -4.8800f, -95.0582f));
            entity->GetTransform()->SetScale(Vector3(0.0125f, 0.0125f, 0.0125f));

            // Break calipers have a wrong rotation, probably a bug with sketchfab auto converting to gltf
            entity->GetTransform()->GetDescendantPtrByName("FR_Caliper_BrakeCaliper_0")->GetTransform()->SetRotationLocal(Quaternion::FromEulerAngles(0.0f, 75.0f, 0.0f));
            entity->GetTransform()->GetDescendantPtrByName("RR_Caliper_BrakeCaliper_0")->GetTransform()->SetRotationLocal(Quaternion::FromEulerAngles(0.0f, 75.0f, 0.0f));

            // body
            {
                // metal - make it aluminum
                if (Material* material = entity->GetTransform()->GetDescendantPtrByName("CarBody_Primary_0")->GetComponent<Renderable>()->GetMaterial())
                {
                    material->SetColor(Color::material_aluminum);
                    material->SetProperty(MaterialProperty::RoughnessMultiplier, 0.1f);
                    material->SetProperty(MaterialProperty::MetalnessMultiplier, 0.15f);
                    material->SetProperty(MaterialProperty::Clearcoat,           1.0f);
                    material->SetProperty(MaterialProperty::Clearcoat_Roughness, 0.25f);
                }

                // plastic
                {
                    if (Material* material = entity->GetTransform()->GetDescendantPtrByName("CarBody_Secondary_0")->GetComponent<Renderable>()->GetMaterial())
                    {
                        material->SetColor(Color::material_tire);
                        material->SetProperty(MaterialProperty::RoughnessMultiplier, 0.35f);
                    }

                    if (Material* material = entity->GetTransform()->GetDescendantPtrByName("CarBody_Trim1_0")->GetComponent<Renderable>()->GetMaterial())
                    {
                        material->SetColor(Color::material_tire);
                        material->SetProperty(MaterialProperty::RoughnessMultiplier, 0.35f);
                    }
                }
            }

            // interior
            {
                if (Material* material = entity->GetTransform()->GetDescendantPtrByName("Interior_InteriorPlastic_0")->GetComponent<Renderable>()->GetMaterial())
                {
                    material->SetColor(Color::material_tire);
                    material->SetTexture(MaterialTexture::Roughness, nullptr);
                    material->SetProperty(MaterialProperty::RoughnessMultiplier, 0.8f);
                    material->SetProperty(MaterialProperty::MetalnessMultiplier, 0.0f);
                }

                if (Material* material = entity->GetTransform()->GetDescendantPtrByName("Interior_InteriorPlastic2_0")->GetComponent<Renderable>()->GetMaterial())
                {
                    material->SetColor(Color::material_tire);
                    material->SetProperty(MaterialProperty::RoughnessMultiplier, 0.8f);
                    material->SetProperty(MaterialProperty::MetalnessMultiplier, 0.0f);
                }

            }

            // lights
            {
                if (Material* material = entity->GetTransform()->GetDescendantPtrByName("CarBody_LampCovers_0")->GetComponent<Renderable>()->GetMaterial())
                {
                    material->SetColor(Color::material_glass);
                    material->SetProperty(MaterialProperty::RoughnessMultiplier, 0.2f);
                    material->SetTexture(MaterialTexture::Emission, material->GetTexture_PtrShared(MaterialTexture::Color));
                }

                // plastic covers
                if (Material* material = entity->GetTransform()->GetDescendantPtrByName("Headlights_Trim2_0")->GetComponent<Renderable>()->GetMaterial())
                {
                    material->SetProperty(MaterialProperty::RoughnessMultiplier, 0.35f);
                    material->SetColor(Color::material_tire);
                }
            }

            // wheels
            {
                // brake caliper
                if (Material* material = entity->GetTransform()->GetDescendantPtrByName("FR_Caliper_BrakeCaliper_0")->GetComponent<Renderable>()->GetMaterial())
                {
                    material->SetTexture(MaterialTexture::Roughness, nullptr);
                    material->SetTexture(MaterialTexture::Metalness, nullptr);
                    material->SetColor(Color::material_aluminum);
                    material->SetProperty(MaterialProperty::RoughnessMultiplier, 0.5f);
                    material->SetProperty(MaterialProperty::MetalnessMultiplier, 1.0f);
                    material->SetProperty(MaterialProperty::Anisotropic, 1.0f);
                    material->SetProperty(MaterialProperty::AnisotropicRotation, 0.5f);
                }

                // brake disc
                if (Material* material = entity->GetTransform()->GetDescendantPtrByName("FL_Wheel_Brake Disc_0")->GetComponent<Renderable>()->GetMaterial())
                {
                    material->SetTexture(MaterialTexture::Roughness, nullptr);
                    material->SetTexture(MaterialTexture::Metalness, nullptr);
                    material->SetColor(Color::material_aluminum);
                    material->SetProperty(MaterialProperty::RoughnessMultiplier, 0.5f);
                    material->SetProperty(MaterialProperty::MetalnessMultiplier, 1.0f);
                    material->SetProperty(MaterialProperty::Anisotropic, 1.0f);
                    material->SetProperty(MaterialProperty::AnisotropicRotation, 0.5f);
                }

                // tires
                if (Material* material = entity->GetTransform()->GetDescendantPtrByName("FL_Wheel_TireMaterial_0")->GetComponent<Renderable>()->GetMaterial())
                {
                    material->SetColor(Color::material_tire);
                    material->SetTexture(MaterialTexture::Roughness, nullptr);
                    material->SetProperty(MaterialProperty::RoughnessMultiplier, 0.5f);
                    material->SetProperty(MaterialProperty::MetalnessMultiplier, 0.0f);
                }

                // rims
                if (Material* material = entity->GetTransform()->GetDescendantPtrByName("FR_Wheel_RimMaterial_0")->GetComponent<Renderable>()->GetMaterial())
                {
                    material->SetTexture(MaterialTexture::Roughness, nullptr);
                    material->SetTexture(MaterialTexture::Metalness, nullptr);
                    material->SetColor(Color::material_aluminum);
                    material->SetProperty(MaterialProperty::RoughnessMultiplier, 0.2f);
                    material->SetProperty(MaterialProperty::MetalnessMultiplier, 1.0f);
                }
            }
        }

        // Start simulating (for the physics and the music to work)
        Engine::AddFlag(EngineMode::Game);
    }

    void World::CreateDefaultWorldTerrain()
    {
        Vector3 camera_position = Vector3(320.9897f, 41.3801f, 666.2982f);
        Vector3 camera_rotation = Vector3(4.7933f, -123.5102f, 0.0f);
        create_default_world_common(false, camera_position, camera_rotation);

        // terrain
        {
            shared_ptr<Entity> entity = CreateEntity();
            entity->SetObjectName("terrain");

            entity->GetTransform()->SetPosition(Vector3(0.0f, 0.0f, 0.0f));

            shared_ptr<RHI_Texture2D> texture_height = make_shared<RHI_Texture2D>(RHI_Texture_Srv, "height");
            texture_height->LoadFromFile("project\\terrain\\height.png");

            shared_ptr<Terrain> terrain = entity->AddComponent<Terrain>();
            terrain->SetMinY(0.0f);
            terrain->SetMaxY(100.0f);
            terrain->SetHeightMap(texture_height);
            terrain->GenerateAsync();
        }

        // start simulating (for the physics and the music to work)
        Engine::AddFlag(EngineMode::Game);
    }

    void World::CreateDefaultWorldSponza()
    {
        Vector3 camera_position = Vector3(-10.0f, 2.0f, 0.1385f);
        Vector3 camera_rotation = Vector3(0.0f, 90.0f, 0.0f);
        create_default_world_common(false, camera_position, camera_rotation, LightIntensity::black_hole, "project\\music\\jake_chudnow_olive.mp3");

        // Point light
        {
            shared_ptr<Entity> entity = CreateEntity();
            entity->SetObjectName("light_point");
            entity->GetTransform()->SetPosition(Vector3(0.0f, 7.5f, 0.0f));

            shared_ptr<Light> light = entity->AddComponent<Light>();
            light->SetLightType(LightType::Point);
            light->SetColor(Color::light_light_bulb);
            light->SetRange(14.960f);
            light->SetIntensity(LightIntensity::bulb_500_watt);
        }

        // 3D model - Sponza
        if (m_default_model_sponza = ResourceCache::Load<Mesh>("project\\models\\sponza\\main\\NewSponza_Main_Blender_glTF.gltf"))
        {
            Entity* entity = m_default_model_sponza->GetRootEntity();
            entity->SetObjectName("sponza");
            entity->GetTransform()->SetPosition(Vector3(0.0f, 0.06f, 0.0f));
            entity->GetTransform()->SetScale(Vector3::One);

            // Make the lamp frame not cast shadows, so we can place a light within it
            if (shared_ptr<Renderable> renderable = entity->GetTransform()->GetDescendantPtrByName("lamp_1stfloor_entrance_1")->GetComponent<Renderable>())
            {
                renderable->SetCastShadows(false);
            }

            // Delete dirt decals since they look bad.
            // They are hovering over the surfaces, to avoid z-fighting, and they also cast shadows underneath them.
            RemoveEntity(entity->GetTransform()->GetDescendantPtrWeakByName("decals_1st_floor").lock());
            RemoveEntity(entity->GetTransform()->GetDescendantPtrWeakByName("decals_2nd_floor").lock());
            RemoveEntity(entity->GetTransform()->GetDescendantPtrWeakByName("decals_3rd_floor").lock());

            // 3D model - Sponza curtains
            if (m_default_model_sponza_curtains = ResourceCache::Load<Mesh>("project\\models\\sponza\\curtains\\NewSponza_Curtains_glTF.gltf"))
            {
                Entity* entity = m_default_model_sponza_curtains->GetRootEntity();
                entity->SetObjectName("sponza_curtains");
                entity->GetTransform()->SetPosition(Vector3(0.0f, 0.06f, 0.0f));
                entity->GetTransform()->SetScale(Vector3::One);
            }
        }

        // Start simulating (for the physics and the music to work)
        Engine::AddFlag(EngineMode::Game);
    }

    void World::CreateDefaultWorldDoomE1M1()
    {
        Vector3 camera_position = Vector3(-134.9146f, 11.6170f, -31.7093f);
        Vector3 camera_rotation = Vector3(0.0f, 90.0f, 0.0f);
        create_default_world_common(true, camera_position, camera_rotation, LightIntensity::sky_sunlight_noon, "project\\music\\doom_e1m1.mp3", false);

        // doom level
        if (m_default_model_helmet_flight = ResourceCache::Load<Mesh>("project\\models\\doom_e1m1\\doom_E1M1.obj"))
        {
            Entity* entity = m_default_model_helmet_flight->GetRootEntity();
            entity->SetObjectName("doom_e1m1");
            entity->GetTransform()->SetPosition(Vector3(0.0f, 9.0f, -355.5300f));
            entity->GetTransform()->SetScale(Vector3(0.1f, 0.1f, 0.1f));
        }

        // Start simulating (for the physics and the music to work)
        Engine::AddFlag(EngineMode::Game);
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
