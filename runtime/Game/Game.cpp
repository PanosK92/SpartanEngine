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

//= INCLUDES ===============================
#include "pch.h"
#include "Game.h"
#include "../Game/Car.h"
#include "../World/World.h"
#include "../World/Entity.h"
#include "../World/Components/Camera.h"
#include "../World/Components/Light.h"
#include "../World/Components/PhysicsBody.h"
#include "../World/Components/AudioSource.h"
#include "../World/Components/Terrain.h"
#include "../Core/ThreadPool.h"
#include "../Core/ProgressTracker.h"
#include "../Rendering/Mesh.h"
#include "../Rendering/Renderer.h"
#include "../Rendering/Material.h"
#include "../Resource/ResourceCache.h"
#include "../Input/Input.h"
#include "../Geometry/GeometryGeneration.h"
#include "../Geometry/GeometryProcessing.h"
//==========================================

//= NAMESPACES ===============
using namespace std;
using namespace spartan::math;
//============================

namespace spartan
{
    namespace
    {
        // resources
        shared_ptr<Entity> m_default_terrain             = nullptr;
        shared_ptr<Entity> m_default_car                 = nullptr;
        Entity* m_default_car_window                     = nullptr;
        shared_ptr<Entity> m_default_physics_body_camera = nullptr;
        shared_ptr<Entity> m_default_environment         = nullptr;
        shared_ptr<Entity> m_default_light_directional   = nullptr;
        vector<shared_ptr<Mesh>> meshes;

        void create_music(const char* soundtrack_file_path = "project\\music\\jake_chudnow_shona.wav")
        {
            if (!soundtrack_file_path)
                return;

            shared_ptr<Entity> entity = World::CreateEntity();
            entity->SetObjectName("audio_source");

            shared_ptr<AudioSource> audio_source = entity->AddComponent<AudioSource>();
            audio_source->SetAudioClip(soundtrack_file_path);
            audio_source->SetLoop(true);
        }

        void create_sun(const LightIntensity sun_intensity = LightIntensity::sky_sunlight_morning_evening, const bool shadows_enabled = true)
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

        void create_floor()
        {
            // the scale of the entity and the UV tiling is adjusted so that it each square represents 1 unit (cube size)

            shared_ptr<Entity> entity = World::CreateEntity();
            entity->SetObjectName("floor");
            entity->SetPosition(Vector3(0.0f, 0.1f, 0.0f)); // raise it a bit to avoid z-fighting with world grid
            entity->SetScale(Vector3(1000.0f, 1.0f, 1000.0f));
            
            // add a renderable component
            shared_ptr<Renderable> renderable = entity->AddComponent<Renderable>();
            renderable->SetMesh(MeshType::Quad);
            renderable->SetDefaultMaterial();
            renderable->GetMaterial()->SetProperty(MaterialProperty::TextureTilingX, entity->GetScale().x);
            renderable->GetMaterial()->SetProperty(MaterialProperty::TextureTilingY, entity->GetScale().z);
            
            // add physics components
            shared_ptr<PhysicsBody> physics_body = entity->AddComponent<PhysicsBody>();
            physics_body->SetShapeType(PhysicsShape::StaticPlane);
        }

        void create_camera(const Vector3& camera_position = Vector3(0.0f, 2.0f, -10.0f), const Vector3& camera_rotation = Vector3(0.0f, 0.0f, 0.0f))
        {
            // create the camera's root (which will be used for movement)
            m_default_physics_body_camera = World::CreateEntity();
            m_default_physics_body_camera->SetObjectName("physics_body_camera");
            m_default_physics_body_camera->SetPosition(camera_position);
            
            // add a physics body so that the camera can move through the environment in a physical manner
            PhysicsBody* physics_body = m_default_physics_body_camera->AddComponent<PhysicsBody>().get();
            physics_body->SetBoundingBox(Vector3(0.45f, 1.8f, 0.25f)); // average european male
            physics_body->SetMass(82.0f);
            physics_body->SetShapeType(PhysicsShape::Capsule);
            physics_body->SetRotationLock(true);
            
            // create the entity that will actual hold the camera component
            shared_ptr<Entity> camera = World::CreateEntity();
            camera->SetObjectName("component_camera");
            camera->AddComponent<Camera>()->SetPhysicsBodyToControl(physics_body);
            camera->SetParent(m_default_physics_body_camera);
            camera->SetPositionLocal(Vector3(0.0f, 1.8f, 0.0f)); // place it at the top of the capsule
            camera->SetRotation(Quaternion::FromEulerAngles(camera_rotation));
        }

        void create_car(const Vector3& position)
        {
            const float car_scale   = 0.0180f;
            const float wheel_scale = 0.3f;

            // load full detail model (no vertex/index optimisations)
            uint32_t mesh_flags = Mesh::GetDefaultFlags();
            mesh_flags &= ~static_cast<uint32_t>(MeshFlags::PostProcessOptimize); 
            
            if (shared_ptr<Mesh> mesh_car = ResourceCache::Load<Mesh>("project\\models\\toyota_ae86_sprinter_trueno_zenki\\scene.gltf", mesh_flags))
            {
                shared_ptr<Entity> entity_car = mesh_car->GetRootEntity().lock();
                entity_car->SetObjectName("geometry");
                entity_car->SetRotation(Quaternion::FromEulerAngles(90.0f, 0.0f, -180.0f));
                entity_car->SetScale(Vector3(car_scale));
            
                // the car is defined with a weird rotation (probably a bug with sketchfab auto converting to gltf)
                // so we create a root which has no rotation and we parent the car to it, then attach the physics body to the root
                m_default_car = World::CreateEntity();
                m_default_car->SetObjectName("toyota_ae86_sprinter_trueno");
                entity_car->SetParent(m_default_car);
            
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
                        material->SetTexture(MaterialTextureType::Roughness, nullptr);
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
                        material->SetTexture(MaterialTextureType::Emission, material->GetTexture(MaterialTextureType::Color));
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
                    PhysicsBody* physics_body = m_default_car->AddComponent<PhysicsBody>().get();
                    physics_body->SetCenterOfMass(Vector3(0.0f, 1.2f, 0.0f));
                    physics_body->SetBoundingBox(Vector3(3.0f, 1.9f, 7.0f));
                    physics_body->SetMass(960.0f); // http://www.j-garage.com/toyota/ae86.html
                    physics_body->SetBodyType(PhysicsBodyType::Vehicle);
                    physics_body->SetShapeType(PhysicsShape::Box);

                    // disable car control (it's toggled via the gameplay code in Tick())
                    physics_body->GetCar()->SetControlEnabled(false);

                    // set the steering wheel to the physics body so that it can rotate it
                    if (Entity* entity_steering_wheel = entity_car->GetDescendantByName("SteeringWheel_SteeringWheel_0"))
                    {
                        physics_body->GetCar()->SetSteeringWheelTransform(entity_steering_wheel);
                    }

                    // load our own wheel
                    if (shared_ptr<Mesh> mesh = ResourceCache::Load<Mesh>("project\\models\\wheel\\model.blend"))
                    {
                        shared_ptr<Entity> entity_wheel_root = mesh->GetRootEntity().lock();
                        entity_wheel_root->SetScale(Vector3(wheel_scale));

                        if (Entity* entity_wheel = entity_wheel_root->GetDescendantByName("wheel Low"))
                        {
                            // create material
                            shared_ptr<Material> material = make_shared<Material>();
                            material->SetTexture(MaterialTextureType::Color,     "project\\models\\wheel\\albedo.jpeg");
                            material->SetTexture(MaterialTextureType::Normal,    "project\\models\\wheel\\normal.png");
                            material->SetTexture(MaterialTextureType::Roughness, "project\\models\\wheel\\roughness.png");
                            material->SetTexture(MaterialTextureType::Metalness, "project\\models\\wheel\\metalness.png");

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
                            wheel->SetParent(m_default_car);
                            physics_body->GetCar()->SetWheelTransform(wheel.get(), 0);

                            wheel = entity_wheel_root->Clone();
                            wheel->SetObjectName("wheel_fr");
                            wheel->GetChildByIndex(0)->SetRotation(Quaternion::FromEulerAngles(0.0f, 0.0f, 180.0f));
                            wheel->GetChildByIndex(0)->SetPosition(Vector3(0.15f, 0.0f, 0.0f));
                            wheel->SetParent(m_default_car);
                            physics_body->GetCar()->SetWheelTransform(wheel.get(), 1);

                            wheel = entity_wheel_root->Clone();
                            wheel->SetObjectName("wheel_rl");
                            wheel->SetParent(m_default_car);
                            physics_body->GetCar()->SetWheelTransform(wheel.get(), 2);

                            wheel = entity_wheel_root->Clone();
                            wheel->SetObjectName("wheel_rr");
                            wheel->GetChildByIndex(0)->SetRotation(Quaternion::FromEulerAngles(0.0f, 0.0f, 180.0f));
                            wheel->GetChildByIndex(0)->SetPosition(Vector3(0.15f, 0.0f, 0.0f));
                            wheel->SetParent(m_default_car);
                            physics_body->GetCar()->SetWheelTransform(wheel.get(), 3);
                        }
                    }
                }

                // disable entities
                {
                    // disable all the wheels since they have weird rotations, we will add our own
                    {
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

                    // super hacky way to disable refraction
                    m_default_car_window = entity_car->GetDescendantByName("CarBody_Windows_0");
                    m_default_car_window->GetComponent<Renderable>()->GetMaterial()->SetProperty(MaterialProperty::ColorA, 0.4f);
                }

                // set the position last so that transforms all the way down to the new wheels are updated
                m_default_car->SetPosition(position);
            }

            // sounds
            {
                // start
                {
                    shared_ptr<Entity> sound = World::CreateEntity();
                    sound->SetObjectName("sound_start");
                    sound->SetParent(m_default_car);

                    shared_ptr<AudioSource> audio_source = sound->AddComponent<AudioSource>();
                    audio_source->SetAudioClip("project\\music\\car_start.wav");
                    audio_source->SetLoop(false);
                    audio_source->SetPlayOnStart(false);
                }

                // idle
                {
                    shared_ptr<Entity> sound = World::CreateEntity();
                    sound->SetObjectName("sound_idle");
                    sound->SetParent(m_default_car);

                    shared_ptr<AudioSource> audio_source = sound->AddComponent<AudioSource>();
                    audio_source->SetAudioClip("project\\music\\car_idle.wav");
                    audio_source->SetLoop(true);
                    audio_source->SetPlayOnStart(false);
                }

                // door
                {
                    shared_ptr<Entity> sound = World::CreateEntity();
                    sound->SetObjectName("sound_door");
                    sound->SetParent(m_default_car);

                    shared_ptr<AudioSource> audio_source = sound->AddComponent<AudioSource>();
                    audio_source->SetAudioClip("project\\music\\car_door.wav");
                    audio_source->SetLoop(false);
                    audio_source->SetPlayOnStart(false);
                }
            }
        }

        void create_physics_playground()
        {
            create_camera();
            create_sun(LightIntensity::sky_sunlight_morning_evening);
            create_music();
            create_floor();

            // we have long screen space shadows so they don't look good with small objects here
            m_default_light_directional->GetComponent<Light>()->SetFlag(LightFlags::ShadowsScreenSpace, false);
            m_default_light_directional->GetComponent<Light>()->SetFlag(LightFlags::Volumetric, false);

            float y = 5.0f;

            // cube
            {
                // create entity
                shared_ptr<Entity> entity = World::CreateEntity();
                entity->SetObjectName("cube");
                entity->SetPosition(Vector3(-2.0f, y, 0.0f));

                // create material
                shared_ptr<Material> material = make_shared<Material>();
                material->SetTexture(MaterialTextureType::Color,     "project\\materials\\crate_space\\albedo.png");
                material->SetTexture(MaterialTextureType::Normal,    "project\\materials\\crate_space\\normal.png");
                material->SetTexture(MaterialTextureType::Occlusion, "project\\materials\\crate_space\\ao.png");
                material->SetTexture(MaterialTextureType::Roughness, "project\\materials\\crate_space\\roughness.png");
                material->SetTexture(MaterialTextureType::Metalness, "project\\materials\\crate_space\\metallic.png");
                material->SetTexture(MaterialTextureType::Height,    "project\\materials\\crate_space\\height.png");
                material->SetProperty(MaterialProperty::Tessellation, 1.0f);

                // create a file path for this material (required for the material to be able to be cached by the resource cache)
                const string file_path = "project\\materials\\crate_space" + string(EXTENSION_MATERIAL);
                material->SetResourceFilePath(file_path);

                // add a renderable component
                shared_ptr<Renderable> renderable = entity->AddComponent<Renderable>();
                renderable->SetMesh(MeshType::Cube);
                renderable->SetMaterial(material);

                // add physics components
                shared_ptr<PhysicsBody> physics_body = entity->AddComponent<PhysicsBody>();
                physics_body->SetMass(PhysicsBody::mass_auto);
                physics_body->SetShapeType(PhysicsShape::Box);
            }

            // flight helmet
            if (shared_ptr<Mesh> mesh = ResourceCache::Load<Mesh>("project\\models\\flight_helmet\\FlightHelmet.gltf"))
            {
                shared_ptr<Entity> entity = mesh->GetRootEntity().lock();
                entity->SetObjectName("flight_helmet");
                entity->SetPosition(Vector3(0.0f, 0.1f, 0.0f));
                entity->SetScale(Vector3(1.7f, 1.7f, 1.7f));

                PhysicsBody* physics_body = entity->AddComponent<PhysicsBody>().get();
                physics_body->SetMass(PhysicsBody::mass_auto);
                physics_body->SetShapeType(PhysicsShape::Mesh, true);
            }

            // damaged helmet
            if (shared_ptr<Mesh> mesh = ResourceCache::Load<Mesh>("project\\models\\damaged_helmet\\DamagedHelmet.gltf"))
            {
                shared_ptr<Entity> entity = mesh->GetRootEntity().lock();
                entity->SetObjectName("damaged_helmet");
                entity->SetPosition(Vector3(2.0f, y, 0.0f));
                entity->SetScale(Vector3(0.3f, 0.3f, 0.3f));

                PhysicsBody* physics_body = entity->AddComponent<PhysicsBody>().get();
                physics_body->SetMass(PhysicsBody::mass_auto);
                physics_body->SetShapeType(PhysicsShape::Mesh);
            }

            // material ball
            if (shared_ptr<Mesh> mesh = ResourceCache::Load<Mesh>("project\\models\\material_ball_in_3d-coat\\scene.gltf"))
            {
                shared_ptr<Entity> entity = mesh->GetRootEntity().lock();
                entity->SetObjectName("material_ball");
                entity->SetPosition(Vector3(4.0f, y, 0.0f));
                entity->SetRotation(Quaternion::Identity);

                if (auto mesh_entity = entity->GetDescendantByName("Object_2"))
                {
                    PhysicsBody* physics_body = mesh_entity->AddComponent<PhysicsBody>().get();
                    physics_body->SetMass(PhysicsBody::mass_auto);
                    physics_body->SetShapeType(PhysicsShape::Mesh);
                }
            }
        }

        void create_forest_car()
        {
            const float foliage_max_render_distance = 1000.0f;

            create_sun(LightIntensity::sky_overcast_day);
            create_camera(Vector3(-458.0084f, 8.0f, 371.9392f), Vector3(0.0f, 0.0f, 0.0f));
            create_car(Vector3(-449.0260f, 6.5f, 359.2632f));

            // mood adjustment
            m_default_light_directional->SetRotation(Quaternion::FromEulerAngles(20.0f, 5.0f, 0.0f));
            Renderer::SetOption(Renderer_Option::Grid, 0.0f);
            Renderer::SetOption(Renderer_Option::GlobalIllumination, 0.0f); // in an open-world it offers little yet it costs the same

            // create
            m_default_terrain = World::CreateEntity();
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
                    audio_source->SetAudioClip("project\\music\\footsteps_grass.wav");
                    audio_source->SetPlayOnStart(false);
                }

                // forest and river sounds
                {
                    shared_ptr<Entity> sound = World::CreateEntity();
                    sound->SetObjectName("forest_river");
                    sound->SetParent(entity);

                    shared_ptr<AudioSource> audio_source = sound->AddComponent<AudioSource>();
                    audio_source->SetAudioClip("project\\music\\forest_river.wav");
                    audio_source->SetLoop(true);
                }

                // wind
                {
                    shared_ptr<Entity> sound = World::CreateEntity();
                    sound->SetObjectName("wind");
                    sound->SetParent(entity);

                    shared_ptr<AudioSource> audio_source = sound->AddComponent<AudioSource>();
                    audio_source->SetAudioClip("project\\music\\wind.wav");
                    audio_source->SetLoop(true);
                }

                // underwater
                {
                    shared_ptr<Entity> sound = World::CreateEntity();
                    sound->SetObjectName("underwater");
                    sound->SetParent(entity);

                    shared_ptr<AudioSource> audio_source = sound->AddComponent<AudioSource>();
                    audio_source->SetAudioClip("project\\music\\underwater.wav");
                    audio_source->SetPlayOnStart(false);
                }
            }

            // terrain
            {
                shared_ptr<Terrain> terrain = m_default_terrain->AddComponent<Terrain>();

                // add renderable component with a material
                {
                    shared_ptr<Material> material = terrain->GetMaterial();

                    // set properties
                    material->SetResourceFilePath(string("project\\terrain\\material_terrain") + string(EXTENSION_MATERIAL));
                    material->SetProperty(MaterialProperty::TextureSlopeBased, 1.0f);
                    material->SetProperty(MaterialProperty::TextureTilingX,    500.0f);
                    material->SetProperty(MaterialProperty::TextureTilingY,    500.0f);

                    // set textures
                    material->SetTexture(MaterialTextureType::Color,     "project\\terrain\\grass\\albedo.png",    0);
                    material->SetTexture(MaterialTextureType::Normal,    "project\\terrain\\grass\\normal.png",    0);
                    material->SetTexture(MaterialTextureType::Roughness, "project\\terrain\\grass\\roughness.png", 0);
                    material->SetTexture(MaterialTextureType::Occlusion, "project\\terrain\\grass\\occlusion.png", 0);
                    material->SetTexture(MaterialTextureType::Color,     "project\\terrain\\rock\\albedo.png",     1);
                    material->SetTexture(MaterialTextureType::Normal,    "project\\terrain\\rock\\normal.png",     1);
                    material->SetTexture(MaterialTextureType::Roughness, "project\\terrain\\rock\\roughness.png",  1);
                    material->SetTexture(MaterialTextureType::Occlusion, "project\\terrain\\rock\\occlusion.png",  1);
                    material->SetTexture(MaterialTextureType::Height,    "project\\terrain\\rock\\height.png",     1);
                    material->SetTexture(MaterialTextureType::Color,     "project\\terrain\\sand\\albedo.png",     2);
                    material->SetTexture(MaterialTextureType::Normal,    "project\\terrain\\sand\\normal.png",     2);
                    material->SetTexture(MaterialTextureType::Roughness, "project\\terrain\\sand\\roughness.png",  2);
                    material->SetTexture(MaterialTextureType::Occlusion, "project\\terrain\\sand\\occlusion.png",  2);
                    // we are not using the height maps of the sand and grass because their high frequency detail can be matched with vertices
                    // however we'll enable tessellation for the material, if no height map is present, perlin noise will be used instead
                    material->SetProperty(MaterialProperty::Tessellation, 1.0f);
                }
                
                // generate a height field
                shared_ptr<RHI_Texture> height_map = ResourceCache::Load<RHI_Texture>("project\\terrain\\height_map.png", RHI_Texture_KeepData);
                terrain->SetHeightMap(height_map.get());
                terrain->Generate();

                // add physics so we can walk on it
                PhysicsBody* physics_body = m_default_terrain->AddComponent<PhysicsBody>().get();
                physics_body->SetShapeType(PhysicsShape::Terrain);

                // water
                {
                    // create root entity
                    shared_ptr<Entity> water = World::CreateEntity();
                    water->SetObjectName("water");
                    water->SetPosition(Vector3(0.0f, 0.0f, 0.0f));
                    water->SetScale(Vector3(1.0f, 1.0f, 1.0f));

                    // create material
                    shared_ptr<Material> material = make_shared<Material>();
                    {
                        material->SetObjectName("material_water");
                        material->SetColor(Color(0.0f, 150.0f / 255.0f, 100.0f / 255.0f, 200.0f / 255.0f));
                        material->SetProperty(MaterialProperty::Ior,                 Material::EnumToIor(MaterialIor::Water));
                        material->SetProperty(MaterialProperty::Clearcoat,           1.0f);
                        material->SetProperty(MaterialProperty::Clearcoat_Roughness, 0.1f);
                        material->SetProperty(MaterialProperty::TextureTilingX,      400.0f);
                        material->SetProperty(MaterialProperty::TextureTilingY,      400.0f);
                        material->SetProperty(MaterialProperty::IsWater,             1.0f);
                        material->SetProperty(MaterialProperty::Tessellation,        1.0f); // close up water needs tessellation so you can see fine ripples

                        // create a file path for this material (required for the material to be able to be cached by the resource cache)
                        const string file_path = "project\\terrain\\water_material" + string(EXTENSION_MATERIAL);
                        material->SetResourceFilePath(file_path);
                    }

                    // geometry
                    {
                        // generate grid
                        const float extend                       = 2000.0f;
                        const uint32_t grid_points_per_dimension = 64;
                        vector<RHI_Vertex_PosTexNorTan> vertices;
                        vector<uint32_t> indices;
                        geometry_generation::generate_grid(&vertices, &indices, grid_points_per_dimension, extend);

                        // split into tiles
                        const uint32_t tile_count = 10; // 10x10 tiles
                        vector<vector<RHI_Vertex_PosTexNorTan>> tiled_vertices;
                        vector<vector<uint32_t>> tiled_indices;
                        spartan::geometry_processing::split_surface_into_tiles(vertices, indices, tile_count, tiled_vertices, tiled_indices);

                        for (uint32_t tile_index = 0; tile_index < static_cast<uint32_t>(tiled_vertices.size()); tile_index++)
                        {
                            string name = "tile_" + to_string(tile_index);

                            // create mesh if it doesn't exist
                            shared_ptr<Mesh> mesh = meshes.emplace_back(make_shared<Mesh>());
                            mesh->SetObjectName(name);
                            mesh->SetFlag(static_cast<uint32_t>(MeshFlags::PostProcessOptimize), false);
                            mesh->AddGeometry(tiled_vertices[tile_index], tiled_indices[tile_index], true);
                            mesh->CreateGpuBuffers();

                            // create a child entity, add a renderable, and this mesh tile to it
                            {
                                shared_ptr<Entity> entity = World::CreateEntity();
                                entity->SetObjectName(name);
                                entity->SetParent(water);

                                if (shared_ptr<Renderable> renderable = entity->AddComponent<Renderable>())
                                {
                                    renderable->SetMesh(mesh.get());
                                    renderable->SetMaterial(material);
                                    renderable->SetFlag(RenderableFlags::CastsShadows, false);
                                }
                            }
                        }
                    }
                }

                // tree (it has a gazillion entities so bake everything together using MeshFlags::ImportCombineMeshes)
                uint32_t flags = Mesh::GetDefaultFlags() | static_cast<uint32_t>(MeshFlags::ImportCombineMeshes);
                if (shared_ptr<Mesh> mesh = ResourceCache::Load<Mesh>("project\\terrain\\tree_elm\\scene.gltf", flags))
                {
                    shared_ptr<Entity> entity = mesh->GetRootEntity().lock();
                    entity->SetObjectName("tree");
                    entity->SetScale(1.0f);


                    // generate instances
                    {
                        vector<Matrix> instances;
                        terrain->GenerateTransforms(&instances, 5000, TerrainProp::Tree);
                        
                        if (Entity* branches = entity->GetDescendantByName("tree_bark_0"))
                        {
                            branches->GetComponent<Renderable>()->SetInstances(instances);
                            branches->GetComponent<Renderable>()->SetMaxRenderDistance(foliage_max_render_distance);
                        }
                        
                        if (Entity* leaf = entity->GetDescendantByName("Plane.550_leaf_0"))
                        {
                            Renderable* renderable = leaf->GetComponent<Renderable>().get();
                
                            renderable->SetInstances(instances);
                            renderable->SetMaxRenderDistance(foliage_max_render_distance);
                            renderable->GetMaterial()->SetProperty(MaterialProperty::IsTree, 1.0f);
                        }
                        
                        if (Entity* leaf = entity->GetDescendantByName("tree_bark for small bottom branch (circle)_0"))
                        {
                            leaf->SetActive(false);
                        }
                    }
                }

                // grass
                {
                    // create entity
                    shared_ptr<Entity> entity = World::CreateEntity();
                    entity->SetObjectName("grass");
                
                    // create a mesh with a grass blade
                    shared_ptr<Mesh> mesh = meshes.emplace_back(make_shared<Mesh>());
                    {
                        mesh->SetFlag(static_cast<uint32_t>(MeshFlags::PostProcessOptimize), false); // geometry is made to spec, don't optimize
                        mesh->SetLodDropoff(MeshLodDropoff::Linear); // linear dropoff - more agressive

                        // create sub-mesh and add three lods for the grass blade
                        uint32_t sub_mesh_index = 0;
                    
                        // lod 0: high quality grass blade (6 segments)
                        {
                            vector<RHI_Vertex_PosTexNorTan> vertices;
                            vector<uint32_t> indices;
                            geometry_generation::generate_grass_blade(&vertices, &indices, 6); // high detail
                            mesh->AddGeometry(vertices, indices, false, &sub_mesh_index);      // add lod 0, no auto-lod generation
                        }
                    
                        // lod 1: medium quality grass blade (2 segments)
                        {
                            vector<RHI_Vertex_PosTexNorTan> vertices;
                            vector<uint32_t> indices;
                            geometry_generation::generate_grass_blade(&vertices, &indices, 1); // medium detail
                            mesh->AddLod(vertices, indices, sub_mesh_index);                   // add lod 1
                        }

                        mesh->SetResourceFilePath(ResourceCache::GetProjectDirectory() + "standard_grass" + EXTENSION_MODEL); // silly, need to remove that
                        mesh->CreateGpuBuffers();                                                                             // aabb, gpu buffers, etc.
                    }

                    // generate instances
                    vector<Matrix> instances;
                    terrain->GenerateTransforms(&instances, 20000000, TerrainProp::Grass);
                
                    // add renderable component
                    Renderable* renderable = entity->AddComponent<Renderable>().get();
                    renderable->SetMesh(mesh.get());
                    renderable->SetFlag(RenderableFlags::CastsShadows, false); // screen space shadows are enough
                    renderable->SetInstances(instances);
                
                    // create a material
                    shared_ptr<Material> material = make_shared<Material>();
                    material->SetResourceFilePath(ResourceCache::GetProjectDirectory() + "grass_blade_material" + string(EXTENSION_MATERIAL));
                    material->SetProperty(MaterialProperty::IsGrassBlasde,       1.0f);
                    material->SetProperty(MaterialProperty::Roughness,           0.5f);
                    material->SetProperty(MaterialProperty::Clearcoat,           1.0f);
                    material->SetProperty(MaterialProperty::Clearcoat_Roughness, 0.8f);
                    material->SetColor(Color::standard_white);
                    renderable->SetMaterial(material);
                
                    renderable->SetMaxRenderDistance(foliage_max_render_distance);
                }
            }
        }

        void create_sponza_4k()
        {
            // set the mood
            create_camera(Vector3(19.2692f, 2.65f, 0.1677f), Vector3(-18.0f, -90.0f, 0.0f));
            create_sun(LightIntensity::black_hole, false);
            create_music("project\\music\\jake_chudnow_olive.wav");
            Renderer::SetWind(Vector3(0.0f, 0.2f, 1.0f) * 0.1f);

            // point light
            {
                shared_ptr<Entity> entity = World::CreateEntity();
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

            const Vector3 position = Vector3(0.0f, 1.5f, 0.0f);
            const float scale      = 2.0f; // I actually walked in sponza, it's that big

            // 3d model - Sponza
            if (shared_ptr<Mesh> mesh = ResourceCache::Load<Mesh>("project\\models\\sponza\\main\\NewSponza_Main_Blender_glTF.gltf"))
            {
                shared_ptr<Entity> entity = mesh->GetRootEntity().lock();
                entity->SetObjectName("sponza");
                entity->SetPosition(position);
                entity->SetScale(scale);

                // make the lamp frame not cast shadows
                if (shared_ptr<Renderable> renderable = entity->GetDescendantByName("lamp_1stfloor_entrance_1")->GetComponent<Renderable>())
                {
                    renderable->SetFlag(RenderableFlags::CastsShadows, false);
                }

                // disable dirt decals since they look bad
                // they are hovering over the surfaces, they have z-fighting, and they also cast shadows underneath them
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
                    }
                }
            }

            // 3d model - curtains
            if (shared_ptr<Mesh> mesh = ResourceCache::Load<Mesh>("project\\models\\sponza\\curtains\\NewSponza_Curtains_glTF.gltf"))
            {
                shared_ptr<Entity> entity = mesh->GetRootEntity().lock();
                entity->SetObjectName("sponza_curtains");
                entity->SetPosition(position);
                entity->SetScale(scale);

                // disable back face culling and enable wind
                {
                    // these are the ropes and the metal rings that hold them
                    if (Material* material = entity->GetDescendantByName("curtain_03_1")->GetComponent<Renderable>()->GetMaterial())
                    {
                        material->SetProperty(MaterialProperty::IsTree, 1.0f);
                    }

                    // this is fabric
                    if (Material* material = entity->GetDescendantByName("curtain_03_2")->GetComponent<Renderable>()->GetMaterial())
                    {
                        material->SetProperty(MaterialProperty::CullMode, static_cast<float>(RHI_CullMode::None));
                        material->SetProperty(MaterialProperty::IsTree,   1.0f);
                    }

                     // this is fabric
                    if (Material* material = entity->GetDescendantByName("curtain_03_3")->GetComponent<Renderable>()->GetMaterial())
                    {
                        material->SetProperty(MaterialProperty::CullMode, static_cast<float>(RHI_CullMode::None));
                        material->SetProperty(MaterialProperty::IsTree,   1.0f);
                    }

                     // this is fabric
                    if (Material* material = entity->GetDescendantByName("curtain_hanging_06_3")->GetComponent<Renderable>()->GetMaterial())
                    {
                        material->SetProperty(MaterialProperty::CullMode, static_cast<float>(RHI_CullMode::None));
                        material->SetProperty(MaterialProperty::IsTree,  1.0f);
                    }
                }
            }

            // 3d model - ivy
            if (shared_ptr<Mesh> mesh = ResourceCache::Load<Mesh>("project\\models\\sponza\\ivy\\NewSponza_IvyGrowth_glTF.gltf"))
            {
                shared_ptr<Entity> entity = mesh->GetRootEntity().lock();
                entity->SetObjectName("sponza_ivy");
                entity->SetPosition(position);
                entity->SetScale(scale);

                if (Material* material = entity->GetDescendantByName("IvySim_Leaves")->GetComponent<Renderable>()->GetMaterial())
                {
                    material->SetProperty(MaterialProperty::CullMode, static_cast<float>(RHI_CullMode::None));
                    material->SetProperty(MaterialProperty::IsTree,   1.0f);
                }
            }
        }

        void create_doom_e1m1()
        {
            create_camera(Vector3(-100.0f, 15.0f, -32.0f), Vector3(0.0f, 90.0f, 0.0f));
            create_sun(LightIntensity::sky_sunlight_noon, false);
            create_music("project\\music\\doom_e1m1.wav");

            if (shared_ptr<Mesh> mesh = ResourceCache::Load<Mesh>("project\\models\\doom_e1m1\\doom_E1M1.obj"))
            {
                shared_ptr<Entity> entity = mesh->GetRootEntity().lock();
                entity->SetObjectName("doom_e1m1");
                entity->SetPosition(Vector3(0.0f, 14.0f, -355.5300f));
                entity->SetScale(Vector3(0.1f, 0.1f, 0.1f));

                PhysicsBody* physics_body = entity->AddComponent<PhysicsBody>().get();
                physics_body->SetShapeType(PhysicsShape::Mesh, true);
            }
        }

        void create_bistro()
        {
            create_camera(Vector3(5.2739f, 1.6343f, 8.2956f), Vector3(0.0f, -180.0f, 0.0f));
            create_sun(LightIntensity::bulb_150_watt, false);
            create_music();

            if (shared_ptr<Mesh> mesh = ResourceCache::Load<Mesh>("project\\models\\Bistro_v5_2\\BistroExterior.fbx"))
            {
                shared_ptr<Entity> entity = mesh->GetRootEntity().lock();
                entity->SetObjectName("bistro_exterior");
                entity->SetPosition(Vector3(0.0f, 0.0f, 0.0f));
                entity->SetScale(Vector3(1.0f, 1.0f, 1.0f));

                // disable door (so we can go through)
                entity->GetDescendantByName("dOORS_2")->SetActive(false);
                entity->GetDescendantByName("Bistro_Research_Exterior_Paris_Building_01_paris_building_01_bottom_4825")->SetActive(false);
                // disable the glass windows as the interior also has them
                entity->GetDescendantByName("Bistro_Research_Exterior_Paris_Building_01_paris_building_01_bottom_4873")->SetActive(false);

                // enable physics for all meshes
                vector<Entity*> entities;
                entity->GetDescendants(&entities);
                for (Entity* entity : entities)
                {
                    if (entity->IsActive() && entity->GetComponent<Renderable>())
                    {
                        PhysicsBody* physics_body = entity->AddComponent<PhysicsBody>().get();
                        physics_body->SetShapeType(PhysicsShape::Mesh);
                    }
                }
            }

            if (shared_ptr<Mesh> mesh = ResourceCache::Load<Mesh>("project\\models\\Bistro_v5_2\\BistroInterior.fbx"))
            {
                shared_ptr<Entity> light = World::CreateEntity();
                light->SetObjectName("light_point");
                light->SetPositionLocal(Vector3(2.2f, 4.0f, 3.2f));
                light->AddComponent<Light>()->SetFlag(LightFlags::ShadowsTransparent, false);
                light->GetComponent<Light>()->SetFlag(LightFlags::Volumetric, false);
                light->GetComponent<Light>()->SetLightType(LightType::Point);
                light->GetComponent<Light>()->SetRange(120.0f);
                light->GetComponent<Light>()->SetIntensity(LightIntensity::bulb_60_watt);
                light->GetComponent<Light>()->SetTemperature(4000.0f); // a bit white, what the emissive textures seem to try to emulate

                shared_ptr<Entity> entity = mesh->GetRootEntity().lock();
                entity->SetObjectName("bistro_interior");
                entity->SetPosition(Vector3(0.0f, 0.0f, 0.0f));
                entity->SetScale(Vector3(1.6f, 1.6f, 1.6f)); // interior has a different scale (for some reason)

                // disable door (so we can go through)
                entity->GetDescendantByName("Bistro_Research_Exterior_Paris_Building_01_paris_building_01_bottom_121")->SetActive(false);

                // remove color and normal textures from the tablecloth material as they are empty/corrupted
                Material* material = entity->GetDescendantByName("Bistro_Research_Interior_Cotton_Placemat_1276")->GetComponent<Renderable>()->GetMaterial();
                material->SetTexture(MaterialTextureType::Color, nullptr);
                material->SetTexture(MaterialTextureType::Normal, nullptr);

                // enable physics for all meshes
                vector<Entity*> entities;
                entity->GetDescendants(&entities);
                for (Entity* entity : entities)
                {
                    if (entity->IsActive() && entity->GetComponent<Renderable>() != nullptr)
                    {
                        PhysicsBody* physics_body = entity->AddComponent<PhysicsBody>().get();
                        physics_body->SetShapeType(PhysicsShape::Mesh);
                    }
                }
            }
        }

        void create_minecraft()
        {
            create_camera(Vector3(-51.7576f, 21.4551f, -85.3699f), Vector3(11.3991f, 30.6026f, 0.0f));
            create_sun();
            create_music();
            create_floor();

            if (shared_ptr<Mesh> mesh = ResourceCache::Load<Mesh>("project\\models\\vokselia_spawn\\vokselia_spawn.obj"))
            {
                shared_ptr<Entity> entity = mesh->GetRootEntity().lock();
                entity->SetObjectName("minecraft");
                entity->SetPosition(Vector3(0.0f, 0.0f, 0.0f));
                entity->SetScale(Vector3(100.0f, 100.0f, 100.0f));

                PhysicsBody* physics_body = entity->AddComponent<PhysicsBody>().get();
                physics_body->SetShapeType(PhysicsShape::Mesh, true);
            }
        }

        void create_living_room_gi_test()
        {
            create_camera(Vector3(3.6573f, 2.4959f, -15.6978f), Vector3(3.9999f, -12.1947f, 0.0f));
            create_sun();
            create_music();

            Renderer::SetOption(Renderer_Option::Grid, 0.0f);
            Renderer::SetOption(Renderer_Option::GlobalIllumination, 0.5f);

            if (shared_ptr<Mesh> mesh = ResourceCache::Load<Mesh>("project\\models\\living_room\\living_room.obj"))
            {
                shared_ptr<Entity> entity = mesh->GetRootEntity().lock();
                entity->SetObjectName("living_Room");
                entity->SetPosition(Vector3(0.0f, 0.03f, 0.0f));
                entity->SetScale(Vector3(2.5f, 2.5f, 2.5f));

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

                // make the windows double sided
                if (shared_ptr<Renderable> renderable = entity->GetDescendantByName("WhitePaint")->GetComponent<Renderable>())
                {
                    if (Material* material = renderable->GetMaterial())
                    {
                        material->SetProperty(MaterialProperty::CullMode, static_cast<float>(RHI_CullMode::None));
                    }
                }

                 // make the windows blinds double sided
                {
                    if (shared_ptr<Renderable> renderable = entity->GetDescendantByName("Mesh_45")->GetComponent<Renderable>())
                    {
                        if (Material* material = renderable->GetMaterial())
                        {
                            material->SetProperty(MaterialProperty::CullMode, static_cast<float>(RHI_CullMode::None));
                        }
                    }
                    if (shared_ptr<Renderable> renderable = entity->GetDescendantByName("Mesh_55")->GetComponent<Renderable>())
                    {
                        if (Material* material = renderable->GetMaterial())
                        {
                            material->SetProperty(MaterialProperty::CullMode, static_cast<float>(RHI_CullMode::None));
                        }
                    }

                    if (shared_ptr<Renderable> renderable = entity->GetDescendantByName("Mesh_95")->GetComponent<Renderable>())
                    {
                        if (Material* material = renderable->GetMaterial())
                        {
                            material->SetProperty(MaterialProperty::CullMode, static_cast<float>(RHI_CullMode::None));
                        }
                    }
                }

               // enable physics for all meshes
               vector<Entity*> entities;
               entity->GetDescendants(&entities);
               for (Entity* entity : entities)
               {
                   if (entity->GetComponent<Renderable>() != nullptr)
                   {
                       PhysicsBody* physics_body = entity->AddComponent<PhysicsBody>().get();
                       physics_body->SetShapeType(PhysicsShape::Mesh);
                   }
               }
            }
        }

        void create_subway_gi_test()
        {
            create_camera();
            
            Renderer::SetOption(Renderer_Option::Grid, 0.0f);
            Renderer::SetOption(Renderer_Option::GlobalIllumination, 0.5f);

            if (shared_ptr<Mesh> mesh = ResourceCache::Load<Mesh>("project\\models\\free-subway-station-r46-subway\\Metro.fbx"))
            {
                shared_ptr<Entity> entity = mesh->GetRootEntity().lock();
                entity->SetObjectName("subway");
                entity->SetScale(Vector3(0.015f));

                // enable physics for all meshes
                vector<Entity*> entities;
                entity->GetDescendants(&entities);
                for (Entity* entity : entities)
                {
                    if (entity->GetComponent<Renderable>() != nullptr)
                    {
                        PhysicsBody* physics_body = entity->AddComponent<PhysicsBody>().get();
                        physics_body->SetShapeType(PhysicsShape::Mesh);
                    }
                }
            }
        }
    }

    void Game::Shutdown()
    {
        m_default_physics_body_camera = nullptr;
        m_default_environment         = nullptr;
        m_default_light_directional   = nullptr;
        m_default_terrain             = nullptr;
        m_default_car                 = nullptr;
        meshes.clear();
    }

    void Game::Tick()
    {
        if (ProgressTracker::IsLoading())
            return;

        // car
        if (m_default_car)
        {
            // car views
            enum class CarView { Dashboard, Hood, Chase };
            static CarView current_view = CarView::Dashboard;
            
            // camera positions for different views
            static const Vector3 car_view_positions[] =
            {
                Vector3(0.5f, 1.8f, -0.6f),  // dashboard
                Vector3(0.0f, 2.0f, 1.0f),   // hood
                Vector3(0.0f, 3.0f, -10.0f)  // chase
            };
        
            // get some commonly used things
            bool inside_the_car             = m_default_physics_body_camera->GetChildrenCount() == 0;
            AudioSource* audio_source_door  = m_default_car->GetChildByName("sound_door")->GetComponent<AudioSource>().get();
            AudioSource* audio_source_start = m_default_car->GetChildByName("sound_start")->GetComponent<AudioSource>().get();
            AudioSource* audio_source_idle  = m_default_car->GetChildByName("sound_idle")->GetComponent<AudioSource>().get();
        
            // enter/exit
            if (Input::GetKeyDown(KeyCode::E))
            {
                Entity* camera = nullptr;
                if (!inside_the_car)
                {
                    camera = m_default_physics_body_camera->GetChildByName("component_camera");
                    camera->SetParent(m_default_car);
                    camera->SetPositionLocal(car_view_positions[static_cast<int>(current_view)]);
                    camera->SetRotationLocal(Quaternion::Identity);
        
                    audio_source_start->Play();
        
                    inside_the_car = true;
                }
                else
                {
                    camera = m_default_car->GetChildByName("component_camera");
                    camera->SetParent(m_default_physics_body_camera);
                    camera->SetPositionLocal(Vector3(0.0f, 1.8f, 0.0f));
                    camera->SetRotationLocal(Quaternion::Identity);
        
                    // place the camera on the left of the driver's door
                    m_default_physics_body_camera->GetComponent<PhysicsBody>()->SetPosition(m_default_car->GetPosition() + m_default_car->GetLeft() * 3.0f + Vector3::Up * 2.0f);
        
                    audio_source_idle->Stop();
        
                    inside_the_car = false;
                }
        
                // enable/disable car/camera control
                camera->GetComponent<Camera>()->SetFlag(CameraFlags::CanBeControlled, !inside_the_car);
                m_default_car->AddComponent<PhysicsBody>()->GetCar()->SetControlEnabled(inside_the_car);
        
                // play exit/enter sound
                audio_source_door->Play();
        
                // disable/enable windshield
                m_default_car_window->SetActive(!inside_the_car);
            }
        
            // change car view
            if (Input::GetKeyDown(KeyCode::V))
            {
                if (inside_the_car)
                {
                    if (Entity* camera = m_default_car->GetChildByName("component_camera"))
                    {
                        current_view = static_cast<CarView>((static_cast<int>(current_view) + 1) % 3);
                        camera->SetPositionLocal(car_view_positions[static_cast<int>(current_view)]);
                    }
                }
            }

            // osd
            {
                Renderer::DrawString("WASD: Move Camera/Car | 'E': Enter/Exit Car | 'V': Change Car View", Vector2(0.005f, -0.96f));
            }
        }
        
        // forest logic
        if (m_default_terrain)
        {
            Camera*  camera  = Renderer::GetCamera().get();
            Terrain* terrain = m_default_terrain->GetComponent<Terrain>().get();
            if (!camera || !terrain)
                return;

            // sound
            {
                bool is_below_water_level = camera->GetEntity()->GetPosition().y < 0.0f;

                // underwater
                {
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
    }

    void Game::Load(DefaultWorld default_world)
    {
        // shutdown current world/logic
        Game::Shutdown();

        // clear all entities and their resources (and memory)
        World::Clear();

        // load whatever needs to be loaded
        ThreadPool::AddTask([default_world]()
        {
            ProgressTracker::SetGlobalLoadingState(true);

            switch (default_world)
            {
                case DefaultWorld::PhysicsPlayground: create_physics_playground();  break;
                case DefaultWorld::ForestCar:         create_forest_car();          break;
                case DefaultWorld::DoomE1M1:          create_doom_e1m1();           break;
                case DefaultWorld::Bistro:            create_bistro();              break;
                case DefaultWorld::Minecraft:         create_minecraft();           break;
                case DefaultWorld::LivingRoomGiTest:  create_living_room_gi_test(); break;
                case DefaultWorld::Sponza4K:          create_sponza_4k();           break;
                case DefaultWorld::SubwayGiTest:      create_subway_gi_test();      break;
                default: SP_ASSERT_MSG(false, "Unhandled default world");           break;
            }

            ProgressTracker::SetGlobalLoadingState(false);
        });
    }
}
