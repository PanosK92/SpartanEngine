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
#include "Components/PhysicsBody.h"
#include "Components/Terrain.h"
#include "../Resource/ResourceCache.h"
#include "../IO/FileStream.h"
#include "../Profiling/Profiler.h"
#include "../Physics/Car.h"
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
        static mutex m_entity_access_mutex;
        static bool m_resolve            = false;
        static bool m_was_in_editor_mode = false;

        // default worlds resources
        static shared_ptr<Entity> m_default_terrain             = nullptr;
        static shared_ptr<Entity> m_default_cube                = nullptr;
        static shared_ptr<Entity> m_default_physics_body_camera = nullptr;
        static shared_ptr<Entity> m_default_environment         = nullptr;
        static shared_ptr<Entity> m_default_model_floor         = nullptr;
        static shared_ptr<Mesh> m_default_model_sponza          = nullptr;
        static shared_ptr<Mesh> m_default_model_sponza_curtains = nullptr;
        static shared_ptr<Mesh> m_default_model_car             = nullptr;
        static shared_ptr<Mesh> m_default_model_wheel           = nullptr;
        static shared_ptr<Mesh> m_default_model_helmet_flight   = nullptr;
        static shared_ptr<Mesh> m_default_model_helmet_damaged  = nullptr;

        static void create_default_world_common(
            const Math::Vector3& camera_position = Vector3(0.0f, 1.0f, -10.0f),
            const Math::Vector3& camera_rotation = Vector3(0.0f, 0.0f, 0.0f),
            const LightIntensity sun_intensity   = LightIntensity::sky_sunlight_noon,
            const char* soundtrack_file_path     = "project\\music\\jake_chudnow_shona.mp3",
            const bool shadows_enabled           = true,
            const bool load_floor                = true
        )
        {
            // environment
            {
                m_default_environment = World::CreateEntity();
                m_default_environment->SetObjectName("environment");
                m_default_environment->AddComponent<Environment>();
            }

            // camera
            {
                // create the camera's root (which will be used for movement)
                m_default_physics_body_camera = World::CreateEntity();
                m_default_physics_body_camera->SetObjectName("physics_body_camera");
                m_default_physics_body_camera->GetTransform()->SetPosition(camera_position);

                // add a physics body so that the camera can move through the environment in a physical manner
                PhysicsBody* physics_body = m_default_physics_body_camera->AddComponent<PhysicsBody>().get();
                physics_body->SetShapeType(PhysicsShape::Capsule);
                physics_body->SetMass(82.0f);
                physics_body->SetRestitution(0.0f);
                physics_body->SetFriction(1.0f);
                physics_body->SetBoundingBox(Vector3(0.5f, 1.8f, 0.5f));
                physics_body->SetRotationLock(true);

                // create the entity that will actual hold the camera component
                shared_ptr<Entity> camera = World::CreateEntity();
                camera->SetObjectName("component_camera");
                camera->AddComponent<Camera>()->SetPhysicsBodyToControl(physics_body);
                camera->AddComponent<AudioListener>();
                camera->GetTransform()->SetParent(m_default_physics_body_camera->GetTransform());
                camera->GetTransform()->SetPositionLocal(Vector3(0.0f, 1.8f, 0.0f)); // place it at the top of the capsule
                camera->GetTransform()->SetRotation(Quaternion::FromEulerAngles(camera_rotation));
            }

            // light - directional
            {
                shared_ptr<Entity> entity = World::CreateEntity();
                entity->SetObjectName("light_directional");

                entity->GetTransform()->SetRotation(Quaternion::FromEulerAngles(35.0f, 90.0f, 0.0f));

                shared_ptr<Light> light = entity->AddComponent<Light>();
                light->SetLightType(LightType::Directional);
                light->SetTemperature(1400.0f);
                light->SetIntensity(sun_intensity);
                light->SetShadowsEnabled(shadows_enabled ? (light->GetIntensityLumens() > 0.0f) : false);
            }

            // music
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
                m_default_model_floor = World::CreateEntity();
                m_default_model_floor->SetObjectName("floor");
                m_default_model_floor->GetTransform()->SetPosition(Vector3(0.0f, 0.1f, 0.0f)); // raise it a bit to avoid z-fighting with world grid
                m_default_model_floor->GetTransform()->SetScale(Vector3(256.0f, 1.0f, 256.0f));

                // add a renderable component
                shared_ptr<Renderable> renderable = m_default_model_floor->AddComponent<Renderable>();
                renderable->SetGeometry(Renderer::GetStandardMesh(Renderer_MeshType::Quad).get());
                renderable->SetDefaultMaterial();
                renderable->GetMaterial()->SetProperty(MaterialProperty::UvTilingX, 100.0f);
                renderable->GetMaterial()->SetProperty(MaterialProperty::UvTilingY, 100.0f);

                // add physics components
                shared_ptr<PhysicsBody> rigid_body = m_default_model_floor->AddComponent<PhysicsBody>();
                rigid_body->SetMass(0.0f); // static
                rigid_body->SetFriction(0.9f);
                rigid_body->SetRestitution(0.1f);
                rigid_body->SetShapeType(PhysicsShape::StaticPlane);
            }
        }

        static void create_default_cube(
            const Math::Vector3& position = Vector3(0.0f, 4.0f, 0.0f),
            const Math::Vector3& scale    = Vector3(1.0f,1.0f, 1.0f))
        {
            // create entity
            m_default_cube = World::CreateEntity();
            m_default_cube->SetObjectName("cube");
            m_default_cube->GetTransform()->SetPosition(position);
            m_default_cube->GetTransform()->SetScale(scale);
            
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
            renderable->SetGeometry(Renderer::GetStandardMesh(Renderer_MeshType::Cube).get());
            renderable->SetMaterial(material);
            
            // add physics components
            shared_ptr<PhysicsBody> rigid_body = m_default_cube->AddComponent<PhysicsBody>();
            rigid_body->SetMass(15.0f);
            rigid_body->SetRestitution(0.3f);
            rigid_body->SetFriction(1.0f);
            rigid_body->SetShapeType(PhysicsShape::Box);
        }

        static void create_default_car(const Math::Vector3& position = Vector3(0.0f, 1.0f, 0.0f))
        {
            if (m_default_model_car = ResourceCache::Load<Mesh>("project\\models\\toyota_ae86_sprinter_trueno_zenki\\scene.gltf"))
            {
                Entity* entity_car = m_default_model_car->GetRootEntity();
                entity_car->SetObjectName("geometry");

                entity_car->GetTransform()->SetPosition(Vector3(0.0f, 0.0f, 0.0f));
                entity_car->GetTransform()->SetRotation(Quaternion::FromEulerAngles(90.0f, 0.0f, -180.0f));
                entity_car->GetTransform()->SetScale(Vector3(0.02f, 0.02f, 0.02f));

                // the car is defined with a weird rotation (probably a bug with sketchfab auto converting to gltf)
                // so we create a root which has no rotation and we parent the car to it, then attach the physics body to the root
                Entity* entity_root = World::CreateEntity().get();
                entity_root->SetObjectName("toyota_ae86_sprinter_trueno");
                entity_root->GetTransform()->SetPosition(position);
                entity_car->GetTransform()->SetParent(entity_root->GetTransform());

                // body
                {
                    if (Entity* body = entity_car->GetTransform()->GetDescendantPtrByName("CarBody_Primary_0"))
                    {
                        if (Material* material = body->GetComponent<Renderable>()->GetMaterial())
                        {
                            material->SetColor(Color::material_aluminum);
                            material->SetProperty(MaterialProperty::RoughnessMultiplier, 0.1f);
                            material->SetProperty(MaterialProperty::MetalnessMultiplier, 0.15f);
                            material->SetProperty(MaterialProperty::Clearcoat, 1.0f);
                            material->SetProperty(MaterialProperty::Clearcoat_Roughness, 0.25f);
                        }
                    }

                    // plastic
                    {
                        if (Entity* body = entity_car->GetTransform()->GetDescendantPtrByName("CarBody_Secondary_0"))
                        {
                            if (Material* material = body->GetComponent<Renderable>()->GetMaterial())
                            {
                                material->SetColor(Color::material_tire);
                                material->SetProperty(MaterialProperty::RoughnessMultiplier, 0.35f);
                            }
                        }

                        if (Entity* body = entity_car->GetTransform()->GetDescendantPtrByName("CarBody_Trim1_0"))
                        {
                            if (Material* material = body->GetComponent<Renderable>()->GetMaterial())
                            {
                                material->SetColor(Color::material_tire);
                                material->SetProperty(MaterialProperty::RoughnessMultiplier, 0.35f);
                            }
                        }
                    }
                }

                // interior
                {
                    if (Material* material = entity_car->GetTransform()->GetDescendantPtrByName("Interior_InteriorPlastic_0")->GetComponent<Renderable>()->GetMaterial())
                    {
                        material->SetColor(Color::material_tire);
                        material->SetTexture(MaterialTexture::Roughness, nullptr);
                        material->SetProperty(MaterialProperty::RoughnessMultiplier, 0.8f);
                        material->SetProperty(MaterialProperty::MetalnessMultiplier, 0.0f);
                    }

                    if (Material* material = entity_car->GetTransform()->GetDescendantPtrByName("Interior_InteriorPlastic2_0")->GetComponent<Renderable>()->GetMaterial())
                    {
                        material->SetColor(Color::material_tire);
                        material->SetProperty(MaterialProperty::RoughnessMultiplier, 0.8f);
                        material->SetProperty(MaterialProperty::MetalnessMultiplier, 0.0f);
                    }

                }

                // lights
                {
                    if (Material* material = entity_car->GetTransform()->GetDescendantPtrByName("CarBody_LampCovers_0")->GetComponent<Renderable>()->GetMaterial())
                    {
                        material->SetColor(Color::material_glass);
                        material->SetProperty(MaterialProperty::RoughnessMultiplier, 0.2f);
                        material->SetTexture(MaterialTexture::Emission, material->GetTexture_PtrShared(MaterialTexture::Color));
                    }

                    // plastic covers
                    if (Material* material = entity_car->GetTransform()->GetDescendantPtrByName("Headlights_Trim2_0")->GetComponent<Renderable>()->GetMaterial())
                    {
                        material->SetProperty(MaterialProperty::RoughnessMultiplier, 0.35f);
                        material->SetColor(Color::material_tire);
                    }
                }

                // add physics body
                {
                    PhysicsBody* physics_body = entity_root->AddComponent<PhysicsBody>().get();
                    physics_body->SetBodyType(PhysicsBodyType::Vehicle);
                    physics_body->SetCenterOfMass(Vector3(0.0f, 1.1f, -0.1f));
                    physics_body->SetBoundingBox(Vector3(3.0f, 1.4f, 8.0f));
                    physics_body->SetRestitution(0.1f);
                    physics_body->SetFriction(0.6f);
                    physics_body->SetFrictionRolling(0.01f);
                    physics_body->SetMass(1000.0f); // 900 – 1,045 kg -> https://en.wikipedia.org/wiki/Toyota_AE86

                    // set the steering wheel to the physics body so that it can rotate it
                    if (Entity* entity_steering_wheel = entity_car->GetTransform()->GetDescendantPtrByName("SteeringWheel_SteeringWheel_0"))
                    {
                        physics_body->GetCar()->SetSteeringWheelTransform(entity_steering_wheel->GetTransform().get());
                    }

                    // remove all the wheels since they have weird rotations, we will add our own
                    {
                        World::RemoveEntity(entity_car->GetTransform()->GetDescendantPtrWeakByName("FL_Wheel_RimMaterial_0").lock());
                        World::RemoveEntity(entity_car->GetTransform()->GetDescendantPtrWeakByName("FL_Wheel_Brake Disc_0").lock());
                        World::RemoveEntity(entity_car->GetTransform()->GetDescendantPtrWeakByName("FL_Wheel_TireMaterial_0").lock());
                        World::RemoveEntity(entity_car->GetTransform()->GetDescendantPtrWeakByName("FL_Caliper_BrakeCaliper_0").lock());

                        World::RemoveEntity(entity_car->GetTransform()->GetDescendantPtrWeakByName("FR_Wheel_RimMaterial_0").lock());
                        World::RemoveEntity(entity_car->GetTransform()->GetDescendantPtrWeakByName("FR_Wheel_Brake Disc_0").lock());
                        World::RemoveEntity(entity_car->GetTransform()->GetDescendantPtrWeakByName("FR_Wheel_TireMaterial_0").lock());
                        World::RemoveEntity(entity_car->GetTransform()->GetDescendantPtrWeakByName("FR_Caliper_BrakeCaliper_0").lock());

                        World::RemoveEntity(entity_car->GetTransform()->GetDescendantPtrWeakByName("RL_Wheel_RimMaterial_0").lock());
                        World::RemoveEntity(entity_car->GetTransform()->GetDescendantPtrWeakByName("RL_Wheel_Brake Disc_0").lock());
                        World::RemoveEntity(entity_car->GetTransform()->GetDescendantPtrWeakByName("RL_Wheel_TireMaterial_0").lock());
                        World::RemoveEntity(entity_car->GetTransform()->GetDescendantPtrWeakByName("RL_Caliper_BrakeCaliper_0").lock());

                        World::RemoveEntity(entity_car->GetTransform()->GetDescendantPtrWeakByName("RR_Wheel_RimMaterial_0").lock());
                        World::RemoveEntity(entity_car->GetTransform()->GetDescendantPtrWeakByName("RR_Wheel_Brake Disc_0").lock());
                        World::RemoveEntity(entity_car->GetTransform()->GetDescendantPtrWeakByName("RR_Wheel_TireMaterial_0").lock());
                        World::RemoveEntity(entity_car->GetTransform()->GetDescendantPtrWeakByName("RR_Caliper_BrakeCaliper_0").lock());
                    }

                    // load our own wheel
                    if (m_default_model_wheel = ResourceCache::Load<Mesh>("project\\models\\wheel\\model.blend"))
                    {
                        Entity* entity_wheel_root = m_default_model_wheel->GetRootEntity();
                        entity_wheel_root->GetTransform()->SetScale(Vector3(0.38f));

                        if (Entity* entity_wheel = entity_wheel_root->GetTransform()->GetDescendantPtrByName("wheel Low"))
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
                            Entity* wheel = entity_wheel_root;
                            wheel->SetObjectName("wheel_fl");
                            wheel->GetTransform()->SetParent(entity_root->GetTransform());
                            physics_body->GetCar()->SetWheelTransform(wheel->GetTransform().get(), 0);

                            wheel = entity_wheel_root->Clone();
                            wheel->SetObjectName("wheel_fr");
                            wheel->GetTransform()->GetChildByIndex(0)->SetRotation(Quaternion::FromEulerAngles(0.0f, 0.0f, 180.0f));
                            wheel->GetTransform()->GetChildByIndex(0)->SetPosition(Vector3(0.15f, 0.0f, 0.0f));
                            wheel->GetTransform()->SetParent(entity_root->GetTransform());
                            physics_body->GetCar()->SetWheelTransform(wheel->GetTransform().get(), 1);

                            wheel = entity_wheel_root->Clone();
                            wheel->SetObjectName("wheel_rl");
                            wheel->GetTransform()->SetParent(entity_root->GetTransform());
                            physics_body->GetCar()->SetWheelTransform(wheel->GetTransform().get(), 2);

                            wheel = entity_wheel_root->Clone();
                            wheel->SetObjectName("wheel_rr");
                            wheel->GetTransform()->GetChildByIndex(0)->SetRotation(Quaternion::FromEulerAngles(0.0f, 0.0f, 180.0f));
                            wheel->GetTransform()->GetChildByIndex(0)->SetPosition(Vector3(0.15f, 0.0f, 0.0f));
                            wheel->GetTransform()->SetParent(entity_root->GetTransform());
                            physics_body->GetCar()->SetWheelTransform(wheel->GetTransform().get(), 3);
                        }
                    }
                }

                // lights
                {
                    // headlights
                    {
                        shared_ptr<Entity> entity_light_left = World::CreateEntity();
                        entity_light_left->SetObjectName("light_left");
                        entity_light_left->GetTransform()->SetParent(entity_car->GetTransform());
                        entity_light_left->GetTransform()->SetPositionLocal(Vector3(-50.0f, -185.0f, -70.0f));
                        entity_light_left->GetTransform()->SetRotationLocal(Quaternion::FromEulerAngles(80.0f, 0.0f, 0.0));

                        shared_ptr<Light> light = entity_light_left->AddComponent<Light>();
                        light->SetLightType(LightType::Spot);
                        light->SetColor(Color::light_light_bulb);
                        light->SetIntensity(LightIntensity::bulb_500_watt);
                        light->SetShadowsEnabled(false);
                        light->SetRange(20.0f);
                        light->SetAngle(30.0f * Math::Helper::DEG_TO_RAD);

                        Entity* entity_light_right = entity_light_left->Clone();
                        entity_light_right->SetObjectName("light_right");
                        entity_light_right->GetTransform()->SetParent(entity_car->GetTransform());
                        entity_light_right->GetTransform()->SetPositionLocal(Vector3(50.0f, -185.0f, -70.0f));
                    }

                    // taillights
                    {
                        shared_ptr<Entity> entity_light_left = World::CreateEntity();
                        entity_light_left->SetObjectName("light_back");
                        entity_light_left->GetTransform()->SetParent(entity_car->GetTransform());
                        entity_light_left->GetTransform()->SetPositionLocal(Vector3(0.0f, 190.0f, -70.0f));
                        entity_light_left->GetTransform()->SetRotationLocal(Quaternion::FromEulerAngles(-70.0f, 0.0f, 0.0));

                        shared_ptr<Light> light = entity_light_left->AddComponent<Light>();
                        light->SetLightType(LightType::Spot);
                        light->SetColor(Color(1.0f, 0.0f, 0.0f, 1.0f));
                        light->SetIntensity(LightIntensity::bulb_500_watt);
                        light->SetShadowsEnabled(false);
                        light->SetRange(5.0f);
                        light->SetAngle(145.0f * Math::Helper::DEG_TO_RAD);
                    }
                }
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
        Clear();

        m_default_environment           = nullptr;
        m_default_model_floor           = nullptr;
        m_default_model_sponza          = nullptr;
        m_default_model_sponza_curtains = nullptr;
        m_default_model_car             = nullptr;
        m_default_model_wheel           = nullptr;
        m_default_model_helmet_flight   = nullptr;
        m_default_model_helmet_damaged  = nullptr;
        m_default_cube                  = nullptr;
        m_default_physics_body_camera   = nullptr;
        m_default_terrain               = nullptr;
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

    void World::CreateDefaultWorldCube()
    {
        create_default_world_common();
        create_default_cube();

        // start simulating (for the music to play)
        Engine::AddFlag(EngineMode::Game);
    }

    void World::CreateDefaultWorldHelmets()
    {
        Vector3 camera_position = Vector3(0.0f, 1.0f, -10.0f);
        Vector3 camera_rotation = Vector3(0.0f, 0.0f, 0.0f);
        create_default_world_common(camera_position, camera_rotation, LightIntensity::black_hole);

        // Point light
        {
            shared_ptr<Entity> entity = CreateEntity();
            entity->SetObjectName("light_point");
            entity->GetTransform()->SetPosition(Vector3(0.0f, 2.0f, -1.5f));

            shared_ptr<Light> light = entity->AddComponent<Light>();
            light->SetLightType(LightType::Point);
            light->SetColor(Color::light_light_bulb);
            light->SetIntensity(LightIntensity::bulb_500_watt);
            light->SetRange(14.78f);
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

            PhysicsBody* physics_body = entity->AddComponent<PhysicsBody>().get();
            physics_body->SetMass(8.0f);
            physics_body->SetFriction(0.95f);
        }

        // Start simulating (for the physics and the music to work)
        Engine::AddFlag(EngineMode::Game);
    }

    void World::CreateDefaultWorldCar()
    {
        Vector3 camera_position = Vector3(8.7844f, 1.5f, -4.1412f);
        Vector3 camera_rotation = Vector3(7.4f, -65.5f, 0.0f);
        create_default_world_common(camera_position, camera_rotation, LightIntensity::sky_sunlight_morning_evening, "project\\music\\riders_on_the_storm_fredwreck_remix.mp3");

        // environment
        {
            m_default_environment->GetComponent<Environment>()->SetFromTextureSphere("project\\environment\\kloppenheim_05_4k.hdr");
        }

        create_default_car();

        Engine::AddFlag(EngineMode::Game);
        SP_LOG_INFO("Use the arrow keys to steer the car and space for handbreak!");
    }

    void World::CreateDefaultWorldForest()
    {
        Vector3 camera_position = Vector3(6.9900f, 25.0f, 332.4628f);
        Vector3 camera_rotation = Vector3(0.0f, 180.0f, 0.0f);
        create_default_world_common(camera_position, camera_rotation, LightIntensity::sky_sunlight_noon, "project\\music\\forest_river.mp3", true, false);

        // terrain
        {
            // create
            m_default_terrain = CreateEntity();
            m_default_terrain->SetObjectName("terrain");

            // add renderable component with a material
            {
                m_default_terrain->AddComponent<Renderable>();

                shared_ptr<Material> material = make_shared<Material>();
                material->SetResourceFilePath(string("project\\terrain\\material_terrain") + string(EXTENSION_MATERIAL));
                material->SetTexture(MaterialTexture::Color,       "project\\terrain\\florest_floor\\albedo.png");
                material->SetTexture(MaterialTexture::Normal,      "project\\terrain\\florest_floor\\normal.png");
                material->SetTexture(MaterialTexture::Color2,      "project\\terrain\\slate_cliff_rock\\albedo.png");
                material->SetTexture(MaterialTexture::Normal2,     "project\\terrain\\slate_cliff_rock\\normal.png");
                material->SetProperty(MaterialProperty::IsTerrain, 1.0f);
                material->SetProperty(MaterialProperty::UvTilingX, 300.0f);
                material->SetProperty(MaterialProperty::UvTilingY, 300.0f);

                m_default_terrain->GetComponent<Renderable>()->SetMaterial(material);
            }
            
            // generate a height field
            shared_ptr<Terrain> terrain = m_default_terrain->AddComponent<Terrain>();
            terrain->SetHeightMap(ResourceCache::Load<RHI_Texture2D>("project\\terrain\\height.png", RHI_Texture_Srv));
            terrain->GenerateAsync([terrain, camera_position]()
            {
                // add physics so we can walk on it
                PhysicsBody* rigid_body = m_default_terrain->AddComponent<PhysicsBody>().get();
                rigid_body->SetMass(0.0f);
                rigid_body->SetRestitution(0.0f);
                rigid_body->SetFriction(1.0f);
                rigid_body->SetFrictionRolling(1.0f);

                // water
                {
                    shared_ptr<Entity> water = CreateEntity();
                    water->SetObjectName("water");
                    water->GetTransform()->SetPosition(Vector3(0.0f, terrain->GetWaterLevel(), 0.0f));
                    water->GetTransform()->SetScale(Vector3(2000.0f, 1.0f, 2000.0f));

                    Renderable* renderable = water->AddComponent<Renderable>().get();
                    renderable->SetGeometry(Renderer_MeshType::Quad);

                    // material
                    {
                        shared_ptr<Material> material = make_shared<Material>();
                        material->SetObjectName("material_water");
                        material->SetColor(Color(0.0f, 48.0f / 255.0f, 75.0f / 255.0f));
                        material->SetTexture(MaterialTexture::Normal,                "project\\terrain\\water_normal_2.jpeg");
                        material->SetProperty(MaterialProperty::IsWater,             1.0f);
                        material->SetProperty(MaterialProperty::ColorA,              70.0f / 255.0f);
                        material->SetProperty(MaterialProperty::RoughnessMultiplier, 0.2f); // just a bit of roughness to diffuse the sun a little
                        material->SetProperty(MaterialProperty::NormalMultiplier,    1.0f);
                        material->SetProperty(MaterialProperty::UvTilingX,           500.0f);
                        material->SetProperty(MaterialProperty::UvTilingY,           500.0f);

                        // create a file path for this material (required for the material to be able to be cached by the resource cache)
                        const string file_path = "project\\terrain\\water_material" + string(EXTENSION_MATERIAL);
                        material->SetResourceFilePath(file_path);

                        renderable->SetMaterial(material);
                    }
                }

                // tree
                if (shared_ptr<Mesh> tree = ResourceCache::Load<Mesh>("project\\models\\vegetation_tree_1\\tree.fbx"))
                {
                    Entity* entity = tree->GetRootEntity();
                    entity->SetObjectName("tree_1");
                    entity->GetTransform()->SetScale(Vector3(0.01f, 0.01f, 0.01f));

                    if (Entity* bark = entity->GetTransform()->GetDescendantPtrByName("Mobile_Tree_1_1"))
                    {
                        Renderable* renderable = bark->GetComponent<Renderable>().get();
                        renderable->GetMaterial()->SetTexture(MaterialTexture::Color, "project\\models\\vegetation_tree_1\\bark.png");
                        renderable->SetInstances(terrain->GetTransformsTree());
                    }

                    if (Entity* leafs = entity->GetTransform()->GetDescendantPtrByName("Mobile_Tree_1_2"))
                    {
                        Renderable* renderable = leafs->GetComponent<Renderable>().get();
                        renderable->GetMaterial()->SetTexture(MaterialTexture::Color, "project\\models\\vegetation_tree_1\\leaf.png");
                        renderable->SetInstances(terrain->GetTransformsTree());
                    }
                }

                // plant_1
                if (shared_ptr<Mesh> plant = ResourceCache::Load<Mesh>("project\\models\\vegetation_plant_1\\ormbunke.obj"))
                {
                    Entity* entity = plant->GetRootEntity();
                    entity->SetObjectName("plant_1");
                    entity->GetTransform()->SetScale(Vector3(1.0f, 1.0f, 1.0f));

                    if (Entity* child = entity->GetTransform()->GetDescendantPtrByName("Plane.010"))
                    {
                        Renderable* renderable = child->GetComponent<Renderable>().get();
                        renderable->GetMaterial()->SetTexture(MaterialTexture::Color,    "project\\models\\vegetation_plant_1\\ormbunke.png");
                        renderable->GetMaterial()->SetProperty(MaterialProperty::ColorR, 1.0f);
                        renderable->GetMaterial()->SetProperty(MaterialProperty::ColorG, 1.0f);
                        renderable->GetMaterial()->SetProperty(MaterialProperty::ColorB, 1.0f);
                        renderable->SetInstances(terrain->GetTransformsPlant1());
                    }
                }

                // because this is loading in a different thread, we need to resolve the world after we enable instancing
                World::Resolve();

                // start simulating (for the music to play)
                Engine::AddFlag(EngineMode::Game);
            });
        }
    }

    void World::CreateDefaultWorldSponza()
    {
        Vector3 camera_position = Vector3(-27.405f, 2.0f, -0.07f);
        Vector3 camera_rotation = Vector3(-8.5f, 90.0f, 0.0f);
        create_default_world_common(camera_position, camera_rotation, LightIntensity::black_hole, "project\\music\\jake_chudnow_olive.mp3");

        // Point light
        {
            shared_ptr<Entity> entity = CreateEntity();
            entity->SetObjectName("light_point");
            entity->GetTransform()->SetPosition(Vector3(0.0f, 7.5f, 0.0f));

            shared_ptr<Light> light = entity->AddComponent<Light>();
            light->SetLightType(LightType::Point);
            light->SetColor(Color::light_light_bulb);
            light->SetRange(39.66f);
            light->SetIntensity(LightIntensity::bulb_500_watt);
        }

        // 3D model - Sponza
        if (m_default_model_sponza = ResourceCache::Load<Mesh>("project\\models\\sponza\\main\\NewSponza_Main_Blender_glTF.gltf"))
        {
            Entity* entity = m_default_model_sponza->GetRootEntity();
            entity->SetObjectName("sponza");
            entity->GetTransform()->SetPosition(Vector3(0.0f, 0.15f, 0.0f));
            entity->GetTransform()->SetScale(Vector3(2.0f, 2.0f, 2.0f)); // I actually walked in Sponza, it's that big

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
                entity->GetTransform()->SetPosition(Vector3(0.0f, 0.15f, 0.0f));
                entity->GetTransform()->SetScale(Vector3(2.0f, 2.0f, 2.0f)); // I actually walked in Sponza, it's that big
            }
        }

        // Start simulating (for the physics and the music to work)
        Engine::AddFlag(EngineMode::Game);
    }

    void World::CreateDefaultWorldDoomE1M1()
    {
        Vector3 camera_position = Vector3(-134.9146f, 11.6170f, -31.7093f);
        Vector3 camera_rotation = Vector3(0.0f, 90.0f, 0.0f);
        create_default_world_common(camera_position, camera_rotation, LightIntensity::sky_sunlight_noon, "project\\music\\doom_e1m1.mp3", false);

        // doom level
        if (m_default_model_helmet_flight = ResourceCache::Load<Mesh>("project\\models\\doom_e1m1\\doom_E1M1.obj"))
        {
            Entity* entity = m_default_model_helmet_flight->GetRootEntity();
            entity->SetObjectName("doom_e1m1");
            entity->GetTransform()->SetPosition(Vector3(0.0f, 1.8f, -355.5300f));
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
