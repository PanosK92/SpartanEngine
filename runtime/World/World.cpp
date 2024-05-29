/*
Copyright(c) 2016-2024 Panos Karabelas

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
#include "ThreadPool.h"
#include "Components/Camera.h"
#include "Components/Light.h"
#include "Components/AudioListener.h"
#include "Components/AudioSource.h"
#include "Components/PhysicsBody.h"
#include "Components/Terrain.h"
#include "../Resource/ResourceCache.h"
#include "../IO/FileStream.h"
#include "../Profiling/Profiler.h"
#include "../Physics/Car.h"
#include "../RHI/RHI_Texture2D.h"
#include "../Rendering/Mesh.h"
#include "../Rendering/Renderer.h"
#include "../Physics/Physics.h"
//====================================

//= NAMESPACES ================
using namespace std;
using namespace Spartan::Math;
//=============================

namespace Spartan
{
    namespace
    {
        vector<shared_ptr<Entity>> m_entities;
        string m_name;
        string m_file_path;
        mutex m_entity_access_mutex;
        bool m_resolve            = false;
        bool m_was_in_editor_mode = false;

        // default worlds resources
        shared_ptr<Entity> m_default_terrain             = nullptr;
        shared_ptr<Entity> m_default_cube                = nullptr;
        shared_ptr<Entity> m_default_physics_body_camera = nullptr;
        shared_ptr<Entity> m_default_environment         = nullptr;
        shared_ptr<Entity> m_default_model_floor         = nullptr;
        shared_ptr<Entity> m_default_light_directional   = nullptr;
        shared_ptr<Mesh> m_default_model                 = nullptr;
        shared_ptr<Mesh> m_default_model_sponza_curtains = nullptr;
        shared_ptr<Mesh> m_default_model_car             = nullptr;
        shared_ptr<Mesh> m_default_model_wheel           = nullptr;
        shared_ptr<Mesh> m_default_model_helmet_flight   = nullptr;
        shared_ptr<Mesh> m_default_model_helmet_damaged  = nullptr;
        shared_ptr<Mesh> m_default_model_material_ball   = nullptr;

        void create_default_world_common(
            const Math::Vector3& camera_position = Vector3(0.0f, 2.0f, -10.0f),
            const Math::Vector3& camera_rotation = Vector3(0.0f, 0.0f, 0.0f),
            const LightIntensity sun_intensity   = LightIntensity::sky_sunlight_morning_evening,
            const char* soundtrack_file_path     = "project\\music\\jake_chudnow_shona.mp3",
            const bool shadows_enabled           = true,
            const bool load_floor                = true
        )
        {
            // camera
            {
                // create the camera's root (which will be used for movement)
                m_default_physics_body_camera = World::CreateEntity();
                m_default_physics_body_camera->SetObjectName("physics_body_camera");
                m_default_physics_body_camera->SetPosition(camera_position);

                // add a physics body so that the camera can move through the environment in a physical manner
                PhysicsBody* physics_body = m_default_physics_body_camera->AddComponent<PhysicsBody>().get();
                physics_body->SetShapeType(PhysicsShape::Capsule);
                physics_body->SetMass(82.0f);
                physics_body->SetBoundingBox(Vector3(0.5f, 1.8f, 0.5f));
                physics_body->SetRotationLock(true);

                // create the entity that will actual hold the camera component
                shared_ptr<Entity> camera = World::CreateEntity();
                camera->SetObjectName("component_camera");
                camera->AddComponent<Camera>()->SetPhysicsBodyToControl(physics_body);
                camera->AddComponent<AudioListener>();
                camera->SetParent(m_default_physics_body_camera);
                camera->SetPositionLocal(Vector3(0.0f, 1.8f, 0.0f)); // place it at the top of the capsule
                camera->SetRotation(Quaternion::FromEulerAngles(camera_rotation));
            }

            // light - directional
            {
                m_default_light_directional = World::CreateEntity();
                m_default_light_directional->SetObjectName("light_directional");

                m_default_light_directional->SetRotation(Quaternion::FromEulerAngles(35.0f, 90.0f, 0.0f));

                shared_ptr<Light> light = m_default_light_directional->AddComponent<Light>();
                light->SetLightType(LightType::Directional);
                light->SetTemperature(2300.0f);
                light->SetIntensity(sun_intensity);
                light->SetFlag(LightFlags::Shadows, shadows_enabled ? (light->GetIntensityLumens() > 0.0f) : false);
                light->SetFlag(LightFlags::ShadowsTransparent, false);
            }

            // music
            if (soundtrack_file_path)
            {
                shared_ptr<Entity> entity = World::CreateEntity();
                entity->SetObjectName("audio_source");

                shared_ptr<AudioSource> audio_source = entity->AddComponent<AudioSource>();
                audio_source->SetAudioClip(soundtrack_file_path);
                audio_source->SetLoop(true);
            }

            // floor
            if (load_floor)
            {
                // the scale of the entity and the UV tiling is adjusted so that it each square represents 1 unit
                // the default cube has 1 unit dimensions, this what can be used as a reference

                m_default_model_floor = World::CreateEntity();
                m_default_model_floor->SetObjectName("floor");
                m_default_model_floor->SetPosition(Vector3(0.0f, 0.1f, 0.0f)); // raise it a bit to avoid z-fighting with world grid
                m_default_model_floor->SetScale(Vector3(1000.0f, 1.0f, 1000.0f));

                // add a renderable component
                shared_ptr<Renderable> renderable = m_default_model_floor->AddComponent<Renderable>();
                renderable->SetGeometry(Renderer::GetStandardMesh(MeshType::Quad).get());
                renderable->SetDefaultMaterial();
                renderable->GetMaterial()->SetProperty(MaterialProperty::TextureTilingX, 170.0f);
                renderable->GetMaterial()->SetProperty(MaterialProperty::TextureTilingY, 170.0f);

                // add physics components
                shared_ptr<PhysicsBody> rigid_body = m_default_model_floor->AddComponent<PhysicsBody>();
                rigid_body->SetMass(0.0f); // static
                rigid_body->SetShapeType(PhysicsShape::StaticPlane);
            }
        }
    }

    void World::Initialize()
    {

    }

    void World::Shutdown()
    {
        Clear();

        m_default_environment           = nullptr;
        m_default_model_floor           = nullptr;
        m_default_model                 = nullptr;
        m_default_model_sponza_curtains = nullptr;
        m_default_model_car             = nullptr;
        m_default_model_wheel           = nullptr;
        m_default_model_helmet_flight   = nullptr;
        m_default_model_helmet_damaged  = nullptr;
        m_default_model_material_ball   = nullptr;
        m_default_cube                  = nullptr;
        m_default_physics_body_camera   = nullptr;
        m_default_terrain               = nullptr;
        m_default_light_directional     = nullptr;
    }

    void World::Tick()
    {
        SP_PROFILE_CPU();

        lock_guard<mutex> lock(m_entity_access_mutex);

        // tick entities
        {
            // detect game toggling
            const bool started   =  Engine::IsFlagSet(EngineMode::Game) &&  m_was_in_editor_mode;
            const bool stopped   = !Engine::IsFlagSet(EngineMode::Game) && !m_was_in_editor_mode;
            m_was_in_editor_mode = !Engine::IsFlagSet(EngineMode::Game);

            // start
            if (started)
            {
                for (shared_ptr<Entity>& entity : m_entities)
                {
                    entity->OnStart();
                }
            }

            // stop
            if (stopped)
            {
                for (shared_ptr<Entity>& entity : m_entities)
                {
                    entity->OnStop();
                }
            }

            // tick
            for (shared_ptr<Entity>& entity : m_entities)
            {
                entity->Tick();
            }
        }

        // notify renderer
        if (m_resolve && !ProgressTracker::IsLoading())
        {
            Renderer::SetEntities(m_entities);
            m_resolve = false;
        }

        TickDefaultWorlds();
    }

    void World::New()
    {
        Clear();
    }

    bool World::SaveToFile(const string& file_path_in)
    {
        // Add scene file extension to the filepath if it's missing
        auto file_path = file_path_in;
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

        // open file
        unique_ptr<FileStream> file = make_unique<FileStream>(file_path, FileStream_Read);
        if (!file->IsOpen())
        {
            SP_LOG_ERROR("Failed to open \"%s\"", file_path.c_str());
            return false;
        }

        // clear existing entities
        Clear();

        m_name      = FileSystem::GetFileNameWithoutExtensionFromFilePath(file_path);
        m_file_path = file_path;

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
            m_entities[i]->Deserialize(file.get(), nullptr);
            ProgressTracker::GetProgress(ProgressType::World).JobDone();
        }

        // report time
        SP_LOG_INFO("World \"%s\" has been loaded. Duration %.2f ms", m_file_path.c_str(), timer.GetElapsedTimeMs());

        SP_FIRE_EVENT(EventType::WorldLoadEnd);

        return true;
    }

    void World::LoadDefaultWorld(DefaultWorld default_world)
    {
        ThreadPool::AddTask([default_world]()
        {
            ProgressTracker::SetLoadingStateGlobal(true);

            switch (default_world)
            {
                case DefaultWorld::Objects:    CreateDefaultWorldObjects();    break;
                case DefaultWorld::Car:        CreateDefaultWorldCar();        break;
                case DefaultWorld::Forest:     CreateDefaultWorldForest();     break;
                case DefaultWorld::Sponza:     CreateDefaultWorldSponza();     break;
                case DefaultWorld::Doom:       CreateDefaultWorldDoom();       break;
                case DefaultWorld::Bistro:     CreateDefaultWorldBistro();     break;
                case DefaultWorld::Minecraft:  CreateDefaultWorldMinecraft();  break;
                case DefaultWorld::LivingRoom: CreateDefaultWorldLivingRoom(); break;
                default: SP_ASSERT_MSG(false, "Unhandled default world");      break;
            }

            ProgressTracker::SetLoadingStateGlobal(false);

            // simulate physics and play music
            Engine::SetFlag(EngineMode::Game, true);
        });
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

    void World::RemoveEntity(Entity* entity_to_remove)
    {
        SP_ASSERT_MSG(entity_to_remove != nullptr, "Entity is null");

        lock_guard<mutex> lock(m_entity_access_mutex);

        // remove the entity and all of its children
        {
            // get the root entity and its descendants
            vector<Entity*> entities_to_remove;
            entities_to_remove.push_back(entity_to_remove);        // add the root entity
            entity_to_remove->GetDescendants(&entities_to_remove); // get descendants 

            // create a set containing the object IDs of entities to remove
            set<uint64_t> ids_to_remove;
            for (Entity* entity : entities_to_remove)
            {
                ids_to_remove.insert(entity->GetObjectId());
            }

            // remove entities using a single loop
            m_entities.erase(remove_if(m_entities.begin(), m_entities.end(),
            [&](const shared_ptr<Entity>& entity)
            {
                return ids_to_remove.count(entity->GetObjectId()) > 0;
            }),
            m_entities.end());

            // if there was a parent, update it
            if (shared_ptr<Entity> parent = entity_to_remove->GetParent())
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
            if (!entity->HasParent())
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

    void World::CreateDefaultWorldObjects()
    {
        create_default_world_common();

        // we have long screen space shadows so they don't look good with small objects here
        m_default_light_directional->GetComponent<Light>()->SetFlag(LightFlags::ShadowsScreenSpace, false);
        m_default_light_directional->GetComponent<Light>()->SetFlag(LightFlags::Volumetric, false);

        float y = 5.0f;

        // cube
        {
            // create entity
            m_default_cube = World::CreateEntity();
            m_default_cube->SetObjectName("cube");
            m_default_cube->SetPosition(Vector3(-2.0f, y, 0.0f));

            // create material
            shared_ptr<Material> material = make_shared<Material>();
            material->SetTexture(MaterialTexture::Color,     "project\\materials\\crate_space\\albedo.png");
            material->SetTexture(MaterialTexture::Normal,    "project\\materials\\crate_space\\normal.png");
            material->SetTexture(MaterialTexture::Occlusion, "project\\materials\\crate_space\\ao.png");
            material->SetTexture(MaterialTexture::Roughness, "project\\materials\\crate_space\\roughness.png");
            material->SetTexture(MaterialTexture::Metalness, "project\\materials\\crate_space\\metallic.png");
            material->SetTexture(MaterialTexture::Height,    "project\\materials\\crate_space\\height.png");

            // create a file path for this material (required for the material to be able to be cached by the resource cache)
            const string file_path = "project\\materials\\crate_space" + string(EXTENSION_MATERIAL);
            material->SetResourceFilePath(file_path);

            // add a renderable component
            shared_ptr<Renderable> renderable = m_default_cube->AddComponent<Renderable>();
            renderable->SetGeometry(Renderer::GetStandardMesh(MeshType::Cube).get());
            renderable->SetMaterial(material);

            // add physics components
            shared_ptr<PhysicsBody> rigid_body = m_default_cube->AddComponent<PhysicsBody>();
            rigid_body->SetMass(15.0f);
            rigid_body->SetShapeType(PhysicsShape::Box);
        }

        // flight helmet
        if (m_default_model_helmet_flight = ResourceCache::Load<Mesh>("project\\models\\flight_helmet\\FlightHelmet.gltf"))
        {
            shared_ptr<Entity> entity = m_default_model_helmet_flight->GetRootEntity().lock();
            entity->SetObjectName("flight_helmet");
            entity->SetPosition(Vector3(0.0f, 0.1f, 0.0f));
            entity->SetScale(Vector3(1.7f, 1.7f, 1.7f));
        }

        // damaged helmet
        if (m_default_model_helmet_damaged = ResourceCache::Load<Mesh>("project\\models\\damaged_helmet\\DamagedHelmet.gltf"))
        {
            shared_ptr<Entity> entity = m_default_model_helmet_damaged->GetRootEntity().lock();
            entity->SetObjectName("damaged_helmet");
            entity->SetPosition(Vector3(2.0f, y, 0.0f));
            entity->SetScale(Vector3(0.3f, 0.3f, 0.3f));

            PhysicsBody* physics_body = entity->AddComponent<PhysicsBody>().get();
            physics_body->SetMass(8.0f);
        }

        // material ball
        if (m_default_model_material_ball = ResourceCache::Load<Mesh>("project\\models\\material_ball_in_3d-coat\\scene.gltf"))
        {
            shared_ptr<Entity> entity = m_default_model_material_ball->GetRootEntity().lock();
            entity->SetObjectName("material_ball");
            entity->SetPosition(Vector3(4.0f, y, 0.0f));
            entity->SetRotation(Quaternion::Identity);

            if (auto mesh_entity = entity->GetDescendantByName("Object_2"))
            {
                PhysicsBody* physics_body = mesh_entity->AddComponent<PhysicsBody>().get();
                physics_body->SetMass(8.0f);
            }
        }
    }

    void World::CreateDefaultWorldCar()
    {
        Vector3 camera_position = Vector3(8.7844f, 2.0f, -4.1412f);
        Vector3 camera_rotation = Vector3(3.5996f, -44.099f, 0.0f);
        create_default_world_common(camera_position, camera_rotation, LightIntensity::sky_sunlight_morning_evening, "project\\music\\take_control.mp3");
        m_default_light_directional->GetComponent<Light>()->SetFlag(LightFlags::Volumetric, false);

        // create car
        {
            const float car_scale   = 0.0180f;
            const float wheel_scale = 0.3f;

            if (m_default_model_car = ResourceCache::Load<Mesh>("project\\models\\toyota_ae86_sprinter_trueno_zenki\\scene.gltf"))
            {
                shared_ptr<Entity> entity_car = m_default_model_car->GetRootEntity().lock();
                entity_car->SetObjectName("geometry");

                entity_car->SetRotation(Quaternion::FromEulerAngles(90.0f, 0.0f, -180.0f));
                entity_car->SetScale(Vector3(car_scale));

                // the car is defined with a weird rotation (probably a bug with sketchfab auto converting to gltf)
                // so we create a root which has no rotation and we parent the car to it, then attach the physics body to the root
                shared_ptr<Entity> entity_root = World::CreateEntity();
                entity_root->SetObjectName("toyota_ae86_sprinter_trueno");
                entity_root->SetPosition(Vector3(0.0f, 0.2f, 0.0f));
                entity_car->SetParent(entity_root);

                // body
                {
                    if (Entity* body = entity_car->GetDescendantByName("CarBody_Windows_0"))
                    {
                        if (Material* material = body->GetComponent<Renderable>()->GetMaterial())
                        {
                            material->SetProperty(MaterialProperty::Ior, 1.45f);
                        }
                    }

                    if (Entity* body = entity_car->GetDescendantByName("CarBody_Primary_0"))
                    {
                        if (Material* material = body->GetComponent<Renderable>()->GetMaterial())
                        {
                            material->SetColor(Color::material_aluminum);
                            material->SetProperty(MaterialProperty::Roughness, 0.08f);
                            material->SetProperty(MaterialProperty::Metalness, 0.15f);
                            material->SetProperty(MaterialProperty::Clearcoat, 1.0f);
                            material->SetProperty(MaterialProperty::Clearcoat_Roughness, 0.25f);
                        }
                    }

                    if (Entity* body = entity_car->GetDescendantByName("CarBody_Mirror_0"))
                    {
                        if (Material* material = body->GetComponent<Renderable>()->GetMaterial())
                        {
                            material->SetColor(Color::standard_black);
                            material->SetProperty(MaterialProperty::Roughness, 0.0f);
                            material->SetProperty(MaterialProperty::Metalness, 1.0f);
                        }
                    }

                    // plastic
                    {
                        if (Entity* body = entity_car->GetDescendantByName("CarBody_Secondary_0"))
                        {
                            if (Material* material = body->GetComponent<Renderable>()->GetMaterial())
                            {
                                material->SetColor(Color::material_tire);
                                material->SetProperty(MaterialProperty::Roughness, 0.35f);
                            }
                        }

                        if (Entity* body = entity_car->GetDescendantByName("CarBody_Trim1_0"))
                        {
                            if (Material* material = body->GetComponent<Renderable>()->GetMaterial())
                            {
                                material->SetColor(Color::material_tire);
                                material->SetProperty(MaterialProperty::Roughness, 0.35f);
                            }
                        }
                    }
                }

                // interior
                {
                    if (Material* material = entity_car->GetDescendantByName("Interior_InteriorPlastic_0")->GetComponent<Renderable>()->GetMaterial())
                    {
                        material->SetColor(Color::material_tire);
                        material->SetTexture(MaterialTexture::Roughness, nullptr);
                        material->SetProperty(MaterialProperty::Roughness, 0.8f);
                        material->SetProperty(MaterialProperty::Metalness, 0.0f);
                    }

                    if (Material* material = entity_car->GetDescendantByName("Interior_InteriorPlastic2_0")->GetComponent<Renderable>()->GetMaterial())
                    {
                        material->SetColor(Color::material_tire);
                        material->SetProperty(MaterialProperty::Roughness, 0.8f);
                        material->SetProperty(MaterialProperty::Metalness, 0.0f);
                    }

                }

                // lights
                {
                    if (Material* material = entity_car->GetDescendantByName("CarBody_LampCovers_0")->GetComponent<Renderable>()->GetMaterial())
                    {
                        material->SetColor(Color::material_glass);
                        material->SetProperty(MaterialProperty::Roughness, 0.2f);
                        material->SetTexture(MaterialTexture::Emission, material->GetTexture_PtrShared(MaterialTexture::Color));
                    }

                    // plastic covers
                    if (Material* material = entity_car->GetDescendantByName("Headlights_Trim2_0")->GetComponent<Renderable>()->GetMaterial())
                    {
                        material->SetProperty(MaterialProperty::Roughness, 0.35f);
                        material->SetColor(Color::material_tire);
                    }
                }

                // add physics body
                {
                    PhysicsBody* physics_body = entity_root->AddComponent<PhysicsBody>().get();
                    physics_body->SetBodyType(PhysicsBodyType::Vehicle);
                    physics_body->SetCenterOfMass(Vector3(0.0f, 1.2f, 0.0f));
                    physics_body->SetBoundingBox(Vector3(3.0f, 1.9f, 7.0f));
                    physics_body->SetMass(960.0f); // http://www.j-garage.com/toyota/ae86.html

                    // set the steering wheel to the physics body so that it can rotate it
                    if (Entity* entity_steering_wheel = entity_car->GetDescendantByName("SteeringWheel_SteeringWheel_0"))
                    {
                        physics_body->GetCar()->SetSteeringWheelTransform(entity_steering_wheel);
                    }

                    // load our own wheel
                    if (m_default_model_wheel = ResourceCache::Load<Mesh>("project\\models\\wheel\\model.blend"))
                    {
                        shared_ptr<Entity> entity_wheel_root = m_default_model_wheel->GetRootEntity().lock();
                        entity_wheel_root->SetScale(Vector3(wheel_scale));
                    
                        if (Entity* entity_wheel = entity_wheel_root->GetDescendantByName("wheel Low"))
                        {
                            // create material
                            shared_ptr<Material> material = make_shared<Material>();
                            material->SetTexture(MaterialTexture::Color, "project\\models\\wheel\\albedo.jpeg");
                            material->SetTexture(MaterialTexture::Normal, "project\\models\\wheel\\normal.png");
                            material->SetTexture(MaterialTexture::Roughness, "project\\models\\wheel\\roughness.png");
                            material->SetTexture(MaterialTexture::Metalness, "project\\models\\wheel\\metalness.png");
                    
                            // create a file path for this material (required for the material to be able to be cached by the resource cache)
                            const string file_path = "project\\models\\wheel" + string(EXTENSION_MATERIAL);
                            material->SetResourceFilePath(file_path);
                    
                            // set material
                            entity_wheel->GetComponent<Renderable>()->SetMaterial(material);
                        }
                    
                        // add the wheels to the body
                        {
                            shared_ptr<Entity> wheel = entity_wheel_root;
                            wheel->SetObjectName("wheel_fl");
                            wheel->SetParent(entity_root);
                            physics_body->GetCar()->SetWheelTransform(wheel.get(), 0);
                    
                            wheel = entity_wheel_root->Clone();
                            wheel->SetObjectName("wheel_fr");
                            wheel->GetChildByIndex(0)->SetRotation(Quaternion::FromEulerAngles(0.0f, 0.0f, 180.0f));
                            wheel->GetChildByIndex(0)->SetPosition(Vector3(0.15f, 0.0f, 0.0f));
                            wheel->SetParent(entity_root);
                            physics_body->GetCar()->SetWheelTransform(wheel.get(), 1);
                    
                            wheel = entity_wheel_root->Clone();
                            wheel->SetObjectName("wheel_rl");
                            wheel->SetParent(entity_root);
                            physics_body->GetCar()->SetWheelTransform(wheel.get(), 2);
                    
                            wheel = entity_wheel_root->Clone();
                            wheel->SetObjectName("wheel_rr");
                            wheel->GetChildByIndex(0)->SetRotation(Quaternion::FromEulerAngles(0.0f, 0.0f, 180.0f));
                            wheel->GetChildByIndex(0)->SetPosition(Vector3(0.15f, 0.0f, 0.0f));
                            wheel->SetParent(entity_root);
                            physics_body->GetCar()->SetWheelTransform(wheel.get(), 3);
                        }
                    }
                }
            }
        }

        // add a ramp
        {
            shared_ptr<Entity> ramp = World::CreateEntity();
            ramp->SetObjectName("ramp");
            ramp->SetPosition(Vector3(0.0f, 0.6f, 25.0f));
            ramp->SetRotation(Quaternion::FromEulerAngles(-6.0f, 0.0f, 0.0f));
            ramp->SetScale(Vector3(10.0f, 1.0f, 20.0f));

            // add a renderable component
            shared_ptr<Renderable> renderable = ramp->AddComponent<Renderable>();
            renderable->SetGeometry(Renderer::GetStandardMesh(MeshType::Cube).get());
            renderable->SetDefaultMaterial();

            // add physics components
            shared_ptr<PhysicsBody> rigid_body = ramp->AddComponent<PhysicsBody>();
            rigid_body->SetMass(0.0f); // static
            rigid_body->SetShapeType(PhysicsShape::Mesh);
        }

        // disable all the wheels since they have weird rotations, we will add our own
        {
            auto entity_car = m_default_model_car->GetRootEntity().lock();

            entity_car->GetDescendantByName("FL_Wheel_RimMaterial_0")->SetActive(false);
            entity_car->GetDescendantByName("FL_Wheel_Brake Disc_0")->SetActive(false);
            entity_car->GetDescendantByName("FL_Wheel_TireMaterial_0")->SetActive(false);
            entity_car->GetDescendantByName("FL_Caliper_BrakeCaliper_0")->SetActive(false);

            entity_car->GetDescendantByName("FR_Wheel_RimMaterial_0")->SetActive(false);
            entity_car->GetDescendantByName("FR_Wheel_Brake Disc_0")->SetActive(false);
            entity_car->GetDescendantByName("FR_Wheel_TireMaterial_0")->SetActive(false);
            entity_car->GetDescendantByName("FR_Caliper_BrakeCaliper_0")->SetActive(false);

            entity_car->GetDescendantByName("RL_Wheel_RimMaterial_0")->SetActive(false);
            entity_car->GetDescendantByName("RL_Wheel_Brake Disc_0")->SetActive(false);
            entity_car->GetDescendantByName("RL_Wheel_TireMaterial_0")->SetActive(false);
            entity_car->GetDescendantByName("RL_Caliper_BrakeCaliper_0")->SetActive(false);

            entity_car->GetDescendantByName("RR_Wheel_RimMaterial_0")->SetActive(false);
            entity_car->GetDescendantByName("RR_Wheel_Brake Disc_0")->SetActive(false);
            entity_car->GetDescendantByName("RR_Wheel_TireMaterial_0")->SetActive(false);
            entity_car->GetDescendantByName("RR_Caliper_BrakeCaliper_0")->SetActive(false);
        }
    }

    void World::CreateDefaultWorldForest()
    {
        Vector3 camera_position = Vector3(-244.5046f, 68.0f, 407.8690f);
        Vector3 camera_rotation = Vector3(0.0f, 144.0f, 0.0f);
        create_default_world_common(camera_position, camera_rotation, LightIntensity::sky_sunlight_morning_evening, nullptr, true, false);

        // mood adjustment
        m_default_light_directional->GetComponent<Light>()->SetTemperature(2300.0f);
        Renderer::SetOption(Renderer_Option::Grid, 0.0f);

        // create
        m_default_terrain = CreateEntity();
        m_default_terrain->SetObjectName("terrain");

        // sound
        {
            shared_ptr<Entity> entity = World::CreateEntity();
            entity->SetObjectName("audio");
            entity->SetParent(m_default_terrain);

            // footsteps grass
            {
                shared_ptr<Entity> sound = World::CreateEntity();
                sound->SetObjectName("footsteps");
                sound->SetParent(entity);

                shared_ptr<AudioSource> audio_source = sound->AddComponent<AudioSource>();
                audio_source->SetAudioClip("project\\music\\footsteps_grass.mp3");
                audio_source->SetPlayOnStart(false);
            }

            // forest and river sounds
            {
                shared_ptr<Entity> sound = World::CreateEntity();
                sound->SetObjectName("forest_river");
                sound->SetParent(entity);

                shared_ptr<AudioSource> audio_source = sound->AddComponent<AudioSource>();
                audio_source->SetAudioClip("project\\music\\forest_river.mp3");
                audio_source->SetLoop(true);
            }

            // wind
            {
                shared_ptr<Entity> sound = World::CreateEntity();
                sound->SetObjectName("wind");
                sound->SetParent(entity);

                shared_ptr<AudioSource> audio_source = sound->AddComponent<AudioSource>();
                audio_source->SetAudioClip("project\\music\\wind.mp3");
                audio_source->SetLoop(true);
            }

            // underwater
            {
                shared_ptr<Entity> sound = World::CreateEntity();
                sound->SetObjectName("underwater");
                sound->SetParent(entity);

                shared_ptr<AudioSource> audio_source = sound->AddComponent<AudioSource>();
                audio_source->SetAudioClip("project\\music\\underwater.mp3");
                audio_source->SetPlayOnStart(false);
            }

            // wind
            {
                shared_ptr<Entity> sound = World::CreateEntity();
                sound->SetObjectName("skyrim");
                sound->SetParent(entity);

                shared_ptr<AudioSource> audio_source = sound->AddComponent<AudioSource>();
                audio_source->SetAudioClip("project\\music\\skyrim.mp3");
                audio_source->SetLoop(true);
            }
        }

        // terrain
        {
            shared_ptr<Terrain> terrain = m_default_terrain->AddComponent<Terrain>();

            // add renderable component with a material
            {
                shared_ptr<Material> material = terrain->GetMaterial();

                // create material
                material->SetResourceFilePath(string("project\\terrain\\material_terrain") + string(EXTENSION_MATERIAL));
                material->SetProperty(MaterialProperty::TextureSlopeBased, 1.0f);
                material->SetProperty(MaterialProperty::TextureTilingX,    500.0f);
                material->SetProperty(MaterialProperty::TextureTilingY,    500.0f);

                // texture flat
                material->SetTexture(MaterialTexture::Color,     "project\\terrain\\grass\\albedo.png");
                material->SetTexture(MaterialTexture::Normal,    "project\\terrain\\grass\\normal.png");
                material->SetTexture(MaterialTexture::Roughness, "project\\terrain\\grass\\roughness.png");
                material->SetTexture(MaterialTexture::Occlusion, "project\\terrain\\grass\\occlusion.png");
                //material->SetTexture(MaterialTexture::Height,    "project\\terrain\\grass\\height.png");

                // texture slope
                material->SetTexture(MaterialTexture::Color2,     "project\\terrain\\rock\\albedo.png");
                material->SetTexture(MaterialTexture::Normal2,    "project\\terrain\\rock\\normal.png");
                material->SetTexture(MaterialTexture::Roughness2, "project\\terrain\\rock\\roughness.png");
                material->SetTexture(MaterialTexture::Occlusion2, "project\\terrain\\rock\\occlusion.png");
                //material->SetTexture(MaterialTexture::Height2,    "project\\terrain\\rock\\height.png");

                // texture subterranean
                material->SetTexture(MaterialTexture::Color3,     "project\\terrain\\sand\\albedo.png");
                material->SetTexture(MaterialTexture::Normal3,    "project\\terrain\\sand\\normal.png");
                material->SetTexture(MaterialTexture::Roughness3, "project\\terrain\\sand\\roughness.png");
                material->SetTexture(MaterialTexture::Occlusion3, "project\\terrain\\sand\\occlusion.png");
                //material->SetTexture(MaterialTexture::Height3,    "project\\terrain\\sand\\height.png");

                // texture snow
                material->SetTexture(MaterialTexture::Color4,     "project\\terrain\\snow\\albedo.png");
                material->SetTexture(MaterialTexture::Normal4,    "project\\terrain\\snow\\normal.png");
                material->SetTexture(MaterialTexture::Roughness4, "project\\terrain\\snow\\roughness.png");
                material->SetTexture(MaterialTexture::Occlusion4, "project\\terrain\\snow\\occlusion.png");
                //material->SetTexture(MaterialTexture::Height4,    "project\\terrain\\snow\\height.png");
            }
            
            // generate a height field
            terrain->SetHeightMap("project\\terrain\\height_map.png");
            terrain->Generate();

            // add water and vegetation
            {
                // add physics so we can walk on it
                PhysicsBody* rigid_body = m_default_terrain->AddComponent<PhysicsBody>().get();
                rigid_body->SetMass(0.0f);
   
                // water
                {
                    shared_ptr<Entity> water = CreateEntity();
                    water->SetObjectName("water");
                    water->SetPosition(Vector3(0.0f, 0.0f, 0.0f));
                    water->SetScale(Vector3(1024.0f, 1.0f, 1024.0f));

                    Renderable* renderable = water->AddComponent<Renderable>().get();
                    renderable->SetGeometry(MeshType::Grid);

                    // material
                    {
                        // set material
                        shared_ptr<Material> material = make_shared<Material>();
                        material->SetObjectName("material_water");
                        material->SetColor(Color(0.0f, 60.0f / 255.0f, 120.0f / 255.0f, 250.0f / 255.0f));
                        material->SetTexture(MaterialTexture::Normal,               "project\\terrain\\water_normal.jpeg");
                        material->SetProperty(MaterialProperty::Ior,                Material::EnumToIor(MaterialIor::Water)); // water
                        material->SetProperty(MaterialProperty::Roughness,          0.0f);
                        material->SetProperty(MaterialProperty::Normal,             0.1f);
                        material->SetProperty(MaterialProperty::TextureTilingX,     200.0f);
                        material->SetProperty(MaterialProperty::TextureTilingY,     200.0f);
                        material->SetProperty(MaterialProperty::VertexAnimateWater, 1.0f);
                        material->SetProperty(MaterialProperty::CullMode,           static_cast<float>(RHI_CullMode::None));

                        // create a file path for this material (required for the material to be able to be cached by the resource cache)
                        const string file_path = "project\\terrain\\water_material" + string(EXTENSION_MATERIAL);
                        material->SetResourceFilePath(file_path);

                        renderable->SetMaterial(material);
                    }
                }

                // vegetation_tree_2
                if (shared_ptr<Mesh> mesh = ResourceCache::Load<Mesh>("project\\terrain\\vegetation_tree_2\\tree.fbx"))
                {
                    shared_ptr<Entity> entity = mesh->GetRootEntity().lock();
                    entity->SetObjectName("tree_2");
                    entity->SetScale(Vector3(0.3f, 0.3f, 0.3f));
                    entity->SetParent(m_default_terrain);

                    vector<Matrix> instances;

                    if (Entity* bark = entity->GetDescendantByName("Trunk"))
                    {
                        bark->SetScaleLocal(Vector3::One);

                        Renderable* renderable = bark->GetComponent<Renderable>().get();
                        renderable->GetMaterial()->SetColor(Color::standard_white);
                        renderable->GetMaterial()->SetTexture(MaterialTexture::Color,  "project\\terrain\\vegetation_tree_2\\trunk_color.png");
                        renderable->GetMaterial()->SetTexture(MaterialTexture::Normal, "project\\terrain\\vegetation_tree_2\\trunk_normal.png");

                        // generate instances
                        terrain->GenerateTransforms(&instances, 5000, TerrainProp::Tree);
                        renderable->SetInstances(instances);
                    }

                    if (Entity* branches = entity->GetDescendantByName("Branches"))
                    {
                        branches->SetScaleLocal(Vector3::One);

                        Renderable* renderable = branches->GetComponent<Renderable>().get();
                        renderable->SetInstances(instances);

                        // tweak material
                        Material* material = renderable->GetMaterial();
                        material->SetColor(Color::standard_white);
                        material->SetTexture(MaterialTexture::Color,                 "project\\terrain\\vegetation_tree_2\\branches_color.png");
                        material->SetTexture(MaterialTexture::Normal,                "project\\terrain\\vegetation_tree_2\\branches_normal.png");
                        material->SetTexture(MaterialTexture::Occlusion,             "project\\terrain\\vegetation_tree_2\\branches_ao.png");
                        material->SetProperty(MaterialProperty::VertexAnimateWind,    1.0f);
                        material->SetProperty(MaterialProperty::SubsurfaceScattering, 1.0f);
                        material->SetProperty(MaterialProperty::WorldSpaceHeight,     renderable->GetBoundingBox(BoundingBoxType::Transformed).GetSize().y);
                        material->SetProperty(MaterialProperty::CullMode,             static_cast<float>(RHI_CullMode::None));
                    }
                }

                // vegetation_plant_1
                if (shared_ptr<Mesh> mesh = ResourceCache::Load<Mesh>("project\\terrain\\vegetation_plant_1\\ormbunke.obj"))
                {
                    shared_ptr<Entity> entity = mesh->GetRootEntity().lock();
                    entity->SetObjectName("plant_1");
                    entity->SetScale(Vector3(1.0f, 1.0f, 1.0f));
                    entity->SetParent(m_default_terrain);

                    if (Entity* child = entity->GetDescendantByName("Plane.010"))
                    {
                        Renderable* renderable = child->GetComponent<Renderable>().get();
                        renderable->SetFlag(RenderableFlags::CastsShadows, false); // cheaper and screen space shadows are enough

                        // tweak material
                        Material* material = renderable->GetMaterial();
                        material->SetColor(Color::standard_white);
                        material->SetTexture(MaterialTexture::Color,                  "project\\terrain\\vegetation_plant_1\\ormbunke.png");
                        material->SetProperty(MaterialProperty::SubsurfaceScattering, 1.0f);
                        material->SetProperty(MaterialProperty::VertexAnimateWind,    1.0f);
                        material->SetProperty(MaterialProperty::WorldSpaceHeight,     renderable->GetBoundingBox(BoundingBoxType::Transformed).GetSize().y);
                        material->SetProperty(MaterialProperty::CullMode,             static_cast<float>(RHI_CullMode::None));

                        // generate instances
                        vector<Matrix> instances;
                        terrain->GenerateTransforms(&instances, 20000, TerrainProp::Plant);
                        renderable->SetInstances(instances);
                    }
                }
            }
        }
    }

    void World::CreateDefaultWorldSponza()
    {
        Vector3 camera_position = Vector3(-27.405f, 3.5f, -0.07f);
        Vector3 camera_rotation = Vector3(-8.5f, 90.0f, 0.0f);
        create_default_world_common(camera_position, camera_rotation, LightIntensity::black_hole, "project\\music\\jake_chudnow_olive.mp3", false, false);

        // point light
        {
            shared_ptr<Entity> entity = CreateEntity();
            entity->SetObjectName("light_point");
            entity->SetPosition(Vector3(0.0f, 7.5f, 0.0f));

            shared_ptr<Light> light = entity->AddComponent<Light>();
            light->SetLightType(LightType::Point);
            light->SetColor(Color::light_light_bulb);
            light->SetRange(39.66f);
            light->SetIntensity(LightIntensity::bulb_150_watt);
            light->SetFlag(LightFlags::ShadowsTransparent, false);
            light->SetFlag(LightFlags::Volumetric, false); // volumetric fog looks bad with point lights
        }

        float scale = 2.0f; // I actually walked in sponza, it's that big

        // 3d model - Sponza
        if (m_default_model = ResourceCache::Load<Mesh>("project\\models\\sponza\\main\\NewSponza_Main_Blender_glTF.gltf"))
        {
            shared_ptr<Entity> entity = m_default_model->GetRootEntity().lock();
            entity->SetObjectName("sponza");
            entity->SetPosition(Vector3(0.0f, 1.5f, 0.0f));
            entity->SetScale(scale);

            // make the lamp frame not cast shadows
            if (shared_ptr<Renderable> renderable = entity->GetDescendantByName("lamp_1stfloor_entrance_1")->GetComponent<Renderable>())
            {
                renderable->SetFlag(RenderableFlags::CastsShadows, false);
            }

            // disable dirt decals since they look bad
            // they are hovering over the surfaces, to avoid z-fighting, and they also cast shadows underneath them
            entity->GetDescendantByName("decals_1st_floor")->SetActive(false);
            entity->GetDescendantByName("decals_2nd_floor")->SetActive(false);
            entity->GetDescendantByName("decals_3rd_floor")->SetActive(false);

            // enable physics for all meshes
            vector<Entity*> entities;
            entity->GetDescendants(&entities);
            for (Entity* entity : entities)
            {
                if (entity->IsActive() && entity->GetComponent<Renderable>() != nullptr)
                {
                    PhysicsBody* physics_body = entity->AddComponent<PhysicsBody>().get();
                    physics_body->SetShapeType(PhysicsShape::Mesh);
                    physics_body->SetMass(0.0f); // static
                }
            }
        }

        // 3d model - sponza curtains
        if (m_default_model_sponza_curtains = ResourceCache::Load<Mesh>("project\\models\\sponza\\curtains\\NewSponza_Curtains_glTF.gltf"))
        {
            shared_ptr<Entity> entity = m_default_model_sponza_curtains->GetRootEntity().lock();
            entity->SetObjectName("sponza_curtains");
            entity->SetPosition(Vector3(0.0f, 0.15f, 0.0f));
            entity->SetScale(scale);

            // disable back face culling
            {
                if (Material* material = entity->GetDescendantByName("curtain_03_2")->GetComponent<Renderable>()->GetMaterial())
                {
                    material->SetProperty(MaterialProperty::CullMode, static_cast<float>(RHI_CullMode::None));
                    material->SetProperty(MaterialProperty::SubsurfaceScattering, 1.0f);
                }

                if (Material* material = entity->GetDescendantByName("curtain_03_3")->GetComponent<Renderable>()->GetMaterial())
                {
                    material->SetProperty(MaterialProperty::CullMode, static_cast<float>(RHI_CullMode::None));
                    material->SetProperty(MaterialProperty::SubsurfaceScattering, 1.0f);
                }

                if (Material* material = entity->GetDescendantByName("curtain_hanging_06_3")->GetComponent<Renderable>()->GetMaterial())
                {
                    material->SetProperty(MaterialProperty::CullMode, static_cast<float>(RHI_CullMode::None));
                    material->SetProperty(MaterialProperty::SubsurfaceScattering, 1.0f);
                }
            }
        }
    }

    void World::CreateDefaultWorldDoom()
    {
        Vector3 camera_position = Vector3(-120.0f, 23.0f, -30.0f);
        Vector3 camera_rotation = Vector3(0.0f, 90.0f, 0.0f);
        create_default_world_common(camera_position, camera_rotation, LightIntensity::sky_sunlight_noon, "project\\music\\doom_e1m1.mp3", false, false);

        if (m_default_model = ResourceCache::Load<Mesh>("project\\models\\doom_e1m1\\doom_E1M1.obj"))
        {
            shared_ptr<Entity> entity = m_default_model->GetRootEntity().lock();
            entity->SetObjectName("doom_e1m1");
            entity->SetPosition(Vector3(0.0f, 14.0f, -355.5300f));
            entity->SetScale(Vector3(0.1f, 0.1f, 0.1f));

            // enable physics for all meshes
            vector<Entity*> entities;
            entity->GetDescendants(&entities);
            for (Entity* entity : entities)
            {
                if (entity->GetComponent<Renderable>() != nullptr)
                {
                    PhysicsBody* physics_body = entity->AddComponent<PhysicsBody>().get();
                    physics_body->SetShapeType(PhysicsShape::Mesh);
                    physics_body->SetMass(0.0f); // static
                }
            }
        }
    }

    void World::CreateDefaultWorldBistro()
    {
        Vector3 camera_position = Vector3(5.2739f, 1.6343f, 8.2956f);
        Vector3 camera_rotation = Vector3(0.0f, -180.0f, 0.0f);
        create_default_world_common(camera_position, camera_rotation, LightIntensity::bulb_150_watt, "project\\music\\jake_chudnow_shona.mp3", false);

        if (m_default_model = ResourceCache::Load<Mesh>("project\\models\\Bistro_v5_2\\BistroExterior.fbx"))
        {
            shared_ptr<Entity> entity = m_default_model->GetRootEntity().lock();
            entity->SetObjectName("bistro_exterior");
            entity->SetPosition(Vector3(0.0f, 0.0f, 0.0f));
            entity->SetScale(Vector3(1.0f, 1.0f, 1.0f));

            // disable door (so we can go through)
            entity->GetDescendantByName("dOORS_2")->SetActive(false);
            entity->GetDescendantByName("Bistro_Research_Exterior_Paris_Building_01_paris_building_01_bottom_4825")->SetActive(false);

            // enable physics for all meshes
            vector<Entity*> entities;
            entity->GetDescendants(&entities);
            for (Entity* entity : entities)
            {
                if (entity->IsActive() && entity->GetComponent<Renderable>() != nullptr)
                {
                    PhysicsBody* physics_body = entity->AddComponent<PhysicsBody>().get();
                    physics_body->SetShapeType(PhysicsShape::Mesh);
                    physics_body->SetMass(0.0f); // static
                }
            }
        }

        if (m_default_model = ResourceCache::Load<Mesh>("project\\models\\Bistro_v5_2\\BistroInterior.fbx"))
        {
            shared_ptr<Entity> light = World::CreateEntity();
            light->SetObjectName("light_point");
            light->SetPositionLocal(Vector3(2.2039f, 3.9965f, 3.2015f));
            light->AddComponent<Light>()->SetFlag(LightFlags::ShadowsTransparent, false);
            light->AddComponent<Light>()->SetFlag(LightFlags::Volumetric, false);
            light->GetComponent<Light>()->SetLightType(LightType::Point);
            light->GetComponent<Light>()->SetRange(20.0f);

            shared_ptr<Entity> entity = m_default_model->GetRootEntity().lock();
            entity->SetObjectName("bistro_interior");
            entity->SetPosition(Vector3(0.0f, 0.0f, 0.0f));
            entity->SetScale(Vector3(1.6f, 1.6f, 1.6f)); // interior has a different scale (for some reason)

            // disable door (so we can go through)
            entity->GetDescendantByName("Bistro_Research_Exterior_Paris_Building_01_paris_building_01_bottom_121")->SetActive(false);

            // remove color and normal textures from the tablecloth material as they are empty/corrupted
            Material* material = entity->GetDescendantByName("Bistro_Research_Interior_Cotton_Placemat_1276")->GetComponent<Renderable>()->GetMaterial();
            material->SetTexture(MaterialTexture::Color, nullptr);
            material->SetTexture(MaterialTexture::Normal, nullptr);

            // enable physics for all meshes
            vector<Entity*> entities;
            entity->GetDescendants(&entities);
            for (Entity* entity : entities)
            {
                if (entity->IsActive() && entity->GetComponent<Renderable>() != nullptr)
                {
                    PhysicsBody* physics_body = entity->AddComponent<PhysicsBody>().get();
                    physics_body->SetShapeType(PhysicsShape::Mesh);
                    physics_body->SetMass(0.0f); // static
                }
            }
        }
    }

    void World::CreateDefaultWorldMinecraft()
    {
        Vector3 camera_position = Vector3(-51.7576f, 21.4551f, -85.3699f);
        Vector3 camera_rotation = Vector3(11.3991f, 30.6026f, 0.0f);
        create_default_world_common(camera_position, camera_rotation);

        if (m_default_model = ResourceCache::Load<Mesh>("project\\models\\vokselia_spawn\\vokselia_spawn.obj"))
        {
            shared_ptr<Entity> entity = m_default_model->GetRootEntity().lock();
            entity->SetObjectName("minecraft");
            entity->SetPosition(Vector3(0.0f, 0.0f, 0.0f));
            entity->SetScale(Vector3(100.0f, 100.0f, 100.0f));

            // enable physics for all meshes
            vector<Entity*> entities;
            entity->GetDescendants(&entities);
            for (Entity* entity : entities)
            {
                if (entity->GetComponent<Renderable>() != nullptr)
                {
                    PhysicsBody* physics_body = entity->AddComponent<PhysicsBody>().get();
                    physics_body->SetShapeType(PhysicsShape::Mesh);
                    physics_body->SetMass(0.0f); // static
                }
            }
        }
    }

    void World::CreateDefaultWorldLivingRoom()
    {
        Vector3 camera_position = Vector3(3.6573f, 2.4959f, -15.6978f);
        Vector3 camera_rotation = Vector3(3.9999f, -12.1947f, 0.0f);
        create_default_world_common(camera_position, camera_rotation);

        if (m_default_model = ResourceCache::Load<Mesh>("project\\models\\living_room\\living_room.obj"))
        {
            shared_ptr<Entity> entity = m_default_model->GetRootEntity().lock();
            entity->SetObjectName("living_Room");
            entity->SetPosition(Vector3(0.0f, 0.03f, 0.0f));
            entity->SetScale(Vector3(2.5f, 2.5f, 2.5f));

            // enable physics for all meshes
            vector<Entity*> entities;
            entity->GetDescendants(&entities);
            for (Entity* entity : entities)
            {
                if (entity->GetComponent<Renderable>() != nullptr)
                {
                    PhysicsBody* physics_body = entity->AddComponent<PhysicsBody>().get();
                    physics_body->SetShapeType(PhysicsShape::Mesh);
                    physics_body->SetMass(0.0f); // static
                }
            }

            // make the radiator metallic
            if (shared_ptr<Renderable> renderable = entity->GetDescendantByName("Mesh_93")->GetComponent<Renderable>())
            {
                if (Material* material = renderable->GetMaterial())
                {
                    material->SetProperty(MaterialProperty::Roughness, 0.3f);
                    material->SetProperty(MaterialProperty::Metalness, 1.0f);
                }
            }

            // make the vase/plate smoother
            if (shared_ptr<Renderable> renderable = entity->GetDescendantByName("Mesh_122")->GetComponent<Renderable>())
            {
                if (Material* material = renderable->GetMaterial())
                {
                    material->SetProperty(MaterialProperty::Roughness, 0.4f);
                }
            }

            // make the tv smoother
            if (shared_ptr<Renderable> renderable = entity->GetDescendantByName("Mesh_20")->GetComponent<Renderable>())
            {
                if (Material* material = renderable->GetMaterial())
                {
                    material->SetProperty(MaterialProperty::Roughness, 0.0f);
                }
            }

            // make the floor smoother
            if (shared_ptr<Renderable> renderable = entity->GetDescendantByName("Mesh_111")->GetComponent<Renderable>())
            {
                if (Material* material = renderable->GetMaterial())
                {
                    material->SetProperty(MaterialProperty::Roughness, 0.5f);
                }
            }

            // disable window blinds
           entity->GetDescendantByName("Default_1")->SetActive(false);
           entity->GetDescendantByName("Default_2")->SetActive(false);
           entity->GetDescendantByName("Default_3")->SetActive(false);

            // make the same come in through the window
            m_default_light_directional->SetRotation(Quaternion::FromEulerAngles(30.0f, 180.0f, 0.0f));
            m_default_light_directional->GetComponent<Light>()->SetIntensity(LightIntensity::sky_overcast_day);

            // make the walls double sided
            if (shared_ptr<Renderable> renderable = entity->GetDescendantByName("Mesh_114")->GetComponent<Renderable>())
            {
                if (Material* material = renderable->GetMaterial())
                {
                    material->SetProperty(MaterialProperty::CullMode, static_cast<float>(RHI_CullMode::None));
                }
            }

            // make the ceiling double sided
            if (shared_ptr<Renderable> renderable = entity->GetDescendantByName("Mesh_110")->GetComponent<Renderable>())
            {
                if (Material* material = renderable->GetMaterial())
                {
                    material->SetProperty(MaterialProperty::CullMode, static_cast<float>(RHI_CullMode::None));
                }
            }
        }
    }

    void World::TickDefaultWorlds()
    {
        // forest default world logic
        {
            if (!m_default_terrain)
                return;

            Camera* camera = Renderer::GetCamera().get();
            if (!camera)
                return;

            Terrain* terrain = m_default_terrain->GetComponent<Terrain>().get();
            if (!terrain)
                return;

            bool is_below_water_level = camera->GetEntity()->GetPosition().y < 0.0f;

            // underwater
            {
                // sound
                if (Entity* entity = m_default_terrain->GetDescendantByName("underwater"))
                {
                    if (AudioSource* audio_source = entity->GetComponent<AudioSource>().get())
                    {
                        if (is_below_water_level && !audio_source->IsPlaying())
                        {
                            audio_source->Play();
                        }
                        else if (!is_below_water_level && audio_source->IsPlaying())
                        {
                            audio_source->Stop();
                        }
                    }
                }
            }

            // footsteps
            if (!is_below_water_level)
            {
                if (Entity* entity = m_default_terrain->GetDescendantByName("footsteps"))
                {
                    if (AudioSource* audio_source = entity->GetComponent<AudioSource>().get())
                    {
                        if (camera->IsWalking() && !audio_source->IsPlaying())
                        {
                            audio_source->Play();
                        }
                        else if (!camera->IsWalking() && audio_source->IsPlaying())
                        {
                            audio_source->Stop();
                        }
                    }
                }
            }
        }
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
