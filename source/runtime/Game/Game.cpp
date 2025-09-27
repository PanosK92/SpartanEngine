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

//= INCLUDES ===============================
#include "pch.h"
#include "Game.h"
#include "../World/World.h"
#include "../World/Entity.h"
#include "../World/Components/Camera.h"
#include "../World/Components/Light.h"
#include "../World/Components/Physics.h"
#include "../World/Components/AudioSource.h"
#include "../World/Components/Terrain.h"
#include "../Core/ThreadPool.h"
#include "../Core/ProgressTracker.h"
#include "../Geometry/Mesh.h"
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
        DefaultWorld loaded_world         = DefaultWorld::Max;
        Entity* default_floor             = nullptr;
        Entity* default_terrain           = nullptr;
        Entity* default_car               = nullptr;
        Entity* default_car_window        = nullptr;
        Entity* default_camera            = nullptr;
        Entity* default_environment       = nullptr;
        Entity* default_light_directional = nullptr;
        Entity* default_metal_cube        = nullptr;
        Entity* default_water             = nullptr;
        vector<shared_ptr<Mesh>> meshes;

        namespace entities
        { 
            void music(const char* soundtrack_file_path = "project\\music\\jake_chudnow_shona.wav")
            {
                SP_ASSERT(soundtrack_file_path);

                auto entity = World::CreateEntity();
                entity->SetObjectName("music");

                AudioSource* audio_source = entity->AddComponent<AudioSource>();
                audio_source->SetAudioClip(soundtrack_file_path);
                audio_source->SetLoop(true);
            }

            void sun(const bool enabled, const Vector3& direction = Vector3(-1.0f, -0.2f, 0.25f))
            {
                default_light_directional = World::CreateEntity();
                default_light_directional->SetObjectName("light_directional");
                Light* light = default_light_directional->AddComponent<Light>();
                light->SetLightType(LightType::Directional);
            
                // rotation from direction
                Vector3 forward = direction.Normalized();
                Quaternion rot  = Quaternion::FromLookRotation(forward);
                default_light_directional->SetRotation(rot);
            
                // intensity
                light->SetTemperature(4000.0f);
                light->SetIntensity(enabled ? 40'000.0f : 0.0f);
                light->SetFlag(LightFlags::Shadows, enabled);
                light->SetFlag(LightFlags::ShadowsScreenSpace, enabled);
                light->SetFlag(LightFlags::DayNightCycle, false);
            }

            void floor()
            {
                // the scale of the entity and the UV tiling is adjusted so that it each square represents 1 unit (cube size)

                default_floor = World::CreateEntity();
                default_floor->SetObjectName("floor");
                default_floor->SetPosition(Vector3(0.0f, 0.1f, 0.0f)); // raise it a bit to avoid z-fighting with world grid
                default_floor->SetScale(Vector3(1000.0f, 1.0f, 1000.0f));
                
                // add a renderable component
                Renderable* renderable = default_floor->AddComponent<Renderable>();
                renderable->SetMesh(MeshType::Quad);
                renderable->SetDefaultMaterial();

                // add physics components
                Physics* physics_body = default_floor->AddComponent<Physics>();
                physics_body->SetBodyType(BodyType::Plane);
            }

            void camera(const Vector3& camera_position = Vector3(0.0f, 2.0f, -10.0f), const Vector3& camera_rotation = Vector3(0.0f, 0.0f, 0.0f))
            {
                // create the camera's root (which will be used for movement)
                default_camera = World::CreateEntity();
                default_camera->SetObjectName("physics_body_camera");
                default_camera->SetPosition(camera_position);

                // add a physics controller so that the camera can move around
                Physics* physics_body = default_camera->AddComponent<Physics>();
                physics_body->SetFriction(1.0f);
                physics_body->SetFrictionRolling(0.8f);
                physics_body->SetRestitution(0.1f);
                physics_body->SetBodyType(BodyType::Controller);

                // add a camera component
                Entity* camera = World::CreateEntity();
                camera->SetObjectName("component_camera");
                camera->AddComponent<Camera>();
                camera->SetParent(default_camera); // if the parent has a physics body, the camera will automatically control it for physics based movement
                camera->SetPositionLocal(physics_body->GetControllerTopLocal());
                camera->SetRotation(Quaternion::FromEulerAngles(camera_rotation));
            }

            void metal_cube(const Vector3& position)
            {
                // create entity
                default_metal_cube = World::CreateEntity();
                default_metal_cube->SetObjectName("metal_cube");
                default_metal_cube->SetPosition(position);
                
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
                Renderable* renderable = default_metal_cube->AddComponent<Renderable>();
                renderable->SetMesh(MeshType::Cube);
                renderable->SetMaterial(material);
                
                // add physics components
                Physics* physics_body = default_metal_cube->AddComponent<Physics>();
                physics_body->SetMass(Physics::mass_from_volume);
                physics_body->SetBodyType(BodyType::Box);
            }

            void flight_helmet(const Vector3& position)
            {
                if (shared_ptr<Mesh> mesh = ResourceCache::Load<Mesh>("project\\models\\flight_helmet\\FlightHelmet.gltf"))
                {
                    Entity* entity = mesh->GetRootEntity();
                    entity->SetObjectName("flight_helmet");
                    entity->SetPosition(position);
                    entity->SetScale(Vector3(1.7f, 1.7f, 1.7f));

                    Physics* physics_body = entity->AddComponent<Physics>();
                    physics_body->SetBodyType(BodyType::Mesh);
                    physics_body->SetMass(Physics::mass_from_volume);
                }
            }

            void damaged_helmet(const Vector3& position)
            {
                if (shared_ptr<Mesh> mesh = ResourceCache::Load<Mesh>("project\\models\\damaged_helmet\\DamagedHelmet.gltf"))
                {
                    Entity* entity = mesh->GetRootEntity();
                    entity->SetObjectName("damaged_helmet");
                    entity->SetPosition(position);
                    entity->SetScale(Vector3(0.3f, 0.3f, 0.3f));

                    Physics* physics_body = entity->AddComponent<Physics>();
                    physics_body->SetBodyType(BodyType::Mesh);
                    physics_body->SetMass(Physics::mass_from_volume);
                }
            }

            void material_ball(const Vector3& position)
            {
                uint32_t flags = Mesh::GetDefaultFlags() | static_cast<uint32_t>(MeshFlags::ImportCombineMeshes);
                if (shared_ptr<Mesh> mesh = ResourceCache::Load<Mesh>("project\\models\\material_ball_in_3d-coat\\scene.gltf", flags))
                {
                    // name, position, rotate
                    Entity* entity = mesh->GetRootEntity();
                    entity->SetObjectName("material_ball");
                    entity->SetPosition(Vector3(0.0f, 2.0f, 0.0f));
                    entity->SetRotation(Quaternion::Identity);

                    // add physics
                    Physics* physics_body = entity->AddComponent<Physics>();
                    physics_body->SetStatic(false);
                    physics_body->SetBodyType(BodyType::Mesh);
                    physics_body->SetMass(100.0f);
                }
            }

            Entity* water(const Vector3& position, float dimension, uint32_t density, Color color, float tiling, float normal_strength)
            {
                // entity
                Entity* water = World::CreateEntity();
                water->SetObjectName("water");
                water->SetPosition(position);
                
                // material
                shared_ptr<Material> material = make_shared<Material>();
                {
                    material->SetObjectName("material_water");
                    material->SetResourceFilePath("water" + string(EXTENSION_MATERIAL));

                    material->SetColor(Color(0.0f, 150.0f / 255.0f, 130.0f / 255.0f, 150.0f / 255.0f)); // pool water color
                    material->SetTexture(MaterialTextureType::Normal,            "project\\materials\\water\\normal.jpeg");
                    material->SetProperty(MaterialProperty::Roughness,           0.0f);
                    material->SetProperty(MaterialProperty::Clearcoat,           0.0f);
                    material->SetProperty(MaterialProperty::Clearcoat_Roughness, 0.0f);
                    material->SetProperty(MaterialProperty::WorldSpaceUv,        1.0f); // mesh size independent tiling
                    material->SetProperty(MaterialProperty::TextureTilingX,      1.0f);
                    material->SetProperty(MaterialProperty::TextureTilingY,      1.0f);
                    material->SetProperty(MaterialProperty::IsWater,             1.0f);
                    material->SetProperty(MaterialProperty::Tessellation,        0.0f); // turned off till I fix tessellation for the forest (it works in the small liminal space world)
                    material->SetProperty(MaterialProperty::Normal,              normal_strength);
                    material->SetProperty(MaterialProperty::TextureTilingX,      tiling);
                    material->SetProperty(MaterialProperty::TextureTilingY,      tiling);
                }
                
                // geometry
                {
                    // generate grid
                    const uint32_t grid_points_per_dimension = density;
                    vector<RHI_Vertex_PosTexNorTan> vertices;
                    vector<uint32_t> indices;
                    geometry_generation::generate_grid(&vertices, &indices, grid_points_per_dimension, dimension);
                
                    // split into tiles
                    const uint32_t tile_count = std::max(1u, density / 6); // dynamic tile count based on density, minimum 1
                    vector<vector<RHI_Vertex_PosTexNorTan>> tiled_vertices;
                    vector<vector<uint32_t>> tiled_indices;
                    vector<Vector3> tile_offsets;
                    spartan::geometry_processing::split_surface_into_tiles(vertices, indices, tile_count, tiled_vertices, tiled_indices, tile_offsets);
                
                    for (uint32_t tile_index = 0; tile_index < static_cast<uint32_t>(tiled_vertices.size()); tile_index++)
                    {
                        string name = "tile_" + to_string(tile_index);
                
                        // create mesh if it doesn't exist
                        shared_ptr<Mesh> mesh = meshes.emplace_back(make_shared<Mesh>());
                        mesh->SetObjectName(name);
                        mesh->SetFlag(static_cast<uint32_t>(MeshFlags::PostProcessOptimize), false);
                        mesh->AddGeometry(tiled_vertices[tile_index], tiled_indices[tile_index], false);
                        mesh->CreateGpuBuffers();
                
                        // create a child entity, add a renderable, and this mesh tile to it
                        {
                            Entity* entity_tile = World::CreateEntity();
                            entity_tile->SetObjectName(name);
                            entity_tile->SetParent(water);
                            entity_tile->SetPosition(tile_offsets[tile_index]);
                
                            if (Renderable* renderable = entity_tile->AddComponent<Renderable>())
                            {
                                renderable->SetMesh(mesh.get());
                                renderable->SetMaterial(material);
                                renderable->SetFlag(RenderableFlags::CastsShadows, false);
                            }

                            // enable buoyancy
                           Physics* physics = entity_tile->AddComponent<Physics>();
                           physics->SetBodyType(BodyType::Water);
                        }
                    }
                }

                return water;
            }
        }

        void set_base_renderer_options()
         {
             // disable all effects which are specific to certain worlds, let the each world decide which effects it wants to enable
             Renderer::SetOption(Renderer_Option::Dithering,           0.0f);
             Renderer::SetOption(Renderer_Option::ChromaticAberration, 0.0f);
             Renderer::SetOption(Renderer_Option::Grid,                0.0f);
             Renderer::SetOption(Renderer_Option::Vhs,                 0.0f);
         }
    }

    namespace car
    {
        void create(const Vector3& position, const bool physics, shared_ptr<RHI_Texture> texture_paint_normal)
        {
            // load and render model at max geometry quality
            uint32_t mesh_flags  = Mesh::GetDefaultFlags();
            mesh_flags          &= ~static_cast<uint32_t>(MeshFlags::PostProcessOptimize);     // don't reduce vertex/index count
            mesh_flags          &= ~static_cast<uint32_t>(MeshFlags::PostProcessGenerateLods); // don't genereate and use LODs
            if (shared_ptr<Mesh> mesh_car = ResourceCache::Load<Mesh>("project\\models\\ferrari_laferrari\\scene.gltf", mesh_flags))
            {
                default_car = mesh_car->GetRootEntity();
                default_car->SetObjectName("ferrari_laferrari");
                default_car->SetScale(2.0f);
                default_car->SetPosition(Vector3(0.0f, 0.1f, 0.0f));

                // material adjustments
                {
                    // body main
                    if (Material* material = default_car->GetDescendantByName("Object_12")->GetComponent<Renderable>()->GetMaterial())
                    {
                        material->SetProperty(MaterialProperty::Roughness, 0.0f);
                        material->SetProperty(MaterialProperty::Clearcoat, 1.0f);
                        material->SetProperty(MaterialProperty::Clearcoat_Roughness, 0.1f);
                        material->SetColor(Color(100.0f / 255.0f, 0.0f, 0.0f, 1.0f));
                    
                        material->SetTexture(MaterialTextureType::Normal, texture_paint_normal);
                        material->SetProperty(MaterialProperty::Normal, 0.03f);
                        material->SetProperty(MaterialProperty::TextureTilingX, 100.0f);
                        material->SetProperty(MaterialProperty::TextureTilingY, 100.0f);
                    }
                    
                    // body metallic/carbon parts
                    if (Material* material = default_car->GetDescendantByName("Object_10")->GetComponent<Renderable>()->GetMaterial())
                    {
                        material->SetProperty(MaterialProperty::Roughness, 0.4f);
                        material->SetProperty(MaterialProperty::Metalness, 1.0f);
                    }
                    
                    // tires
                    {
                         if (Material* material = default_car->GetDescendantByName("Object_127")->GetComponent<Renderable>()->GetMaterial())
                         {
                             material->SetProperty(MaterialProperty::Roughness, 0.7f);
                         }
                         
                         if (Material* material = default_car->GetDescendantByName("Object_142")->GetComponent<Renderable>()->GetMaterial())
                         {
                             material->SetProperty(MaterialProperty::Roughness, 0.7f);
                         }
                    
                         if (Material* material = default_car->GetDescendantByName("Object_157")->GetComponent<Renderable>()->GetMaterial())
                         {
                             material->SetProperty(MaterialProperty::Roughness, 0.7f);
                         }
                    
                         if (Material* material = default_car->GetDescendantByName("Object_172")->GetComponent<Renderable>()->GetMaterial())
                         {
                             material->SetProperty(MaterialProperty::Roughness, 0.7f);
                         }
                    
                    }
                    
                    // rims back
                    if (Material* material = default_car->GetDescendantByName("Object_180")->GetComponent<Renderable>()->GetMaterial())
                    {
                        material->SetProperty(MaterialProperty::Metalness, 1.0f);
                        material->SetProperty(MaterialProperty::Roughness, 0.3f);
                    }
                    
                    // rims front
                    if (Material* material = default_car->GetDescendantByName("Object_150")->GetComponent<Renderable>()->GetMaterial())
                    {
                        material->SetProperty(MaterialProperty::Metalness, 1.0f);
                        material->SetProperty(MaterialProperty::Roughness, 0.3f);
                    }
                    
                    // headlight and taillight glass
                    if (Material* material = default_car->GetDescendantByName("Object_38")->GetComponent<Renderable>()->GetMaterial())
                    {
                        material->SetProperty(MaterialProperty::Roughness, 0.5f);
                        material->SetProperty(MaterialProperty::Metalness, 1.0f);
                    }
                    
                    // front (windshield) and back (engine) glass
                    if (Material* material = default_car->GetDescendantByName("Object_58")->GetComponent<Renderable>()->GetMaterial())
                    {
                        material->SetProperty(MaterialProperty::Roughness, 0.0f);
                        material->SetProperty(MaterialProperty::Metalness, 0.0f);
                    }
                    
                    // side mirror glass
                    if (Material* material = default_car->GetDescendantByName("Object_98")->GetComponent<Renderable>()->GetMaterial())
                    {
                        material->SetProperty(MaterialProperty::Roughness, 0.0f);
                        material->SetProperty(MaterialProperty::Metalness, 1.0f);
                    }
                    
                    // engine block metal
                    if (Material* material = default_car->GetDescendantByName("Object_14")->GetComponent<Renderable>()->GetMaterial())
                    {
                        material->SetProperty(MaterialProperty::Roughness, 0.4f);
                        material->SetProperty(MaterialProperty::Metalness, 1.0f);
                    }
                    
                    // brake disc metal
                    {
                        if (Material* material = default_car->GetDescendantByName("Object_129")->GetComponent<Renderable>()->GetMaterial())
                        {
                            material->SetProperty(MaterialProperty::Metalness, 1.0f);
                            material->SetProperty(MaterialProperty::Anisotropic, 1.0f);
                            material->SetProperty(MaterialProperty::AnisotropicRotation, 0.2f);
                        }

                        if (Material* material = default_car->GetDescendantByName("Object_144")->GetComponent<Renderable>()->GetMaterial())
                        {
                            material->SetProperty(MaterialProperty::Metalness, 1.0f);
                            material->SetProperty(MaterialProperty::Anisotropic, 1.0f);
                            material->SetProperty(MaterialProperty::AnisotropicRotation, 0.2f);
                        }

                        if (Material* material = default_car->GetDescendantByName("Object_174")->GetComponent<Renderable>()->GetMaterial())
                        {
                            material->SetProperty(MaterialProperty::Metalness, 1.0f);
                            material->SetProperty(MaterialProperty::Anisotropic, 1.0f);
                            material->SetProperty(MaterialProperty::AnisotropicRotation, 0.2f);
                        }

                        if (Material* material = default_car->GetDescendantByName("Object_159")->GetComponent<Renderable>()->GetMaterial())
                        {
                            material->SetProperty(MaterialProperty::Metalness, 1.0f);
                            material->SetProperty(MaterialProperty::Anisotropic, 1.0f);
                            material->SetProperty(MaterialProperty::AnisotropicRotation, 0.2f);
                        }
                    }

                     // interior leather/plastic
                    if (Material* material = default_car->GetDescendantByName("Object_90")->GetComponent<Renderable>()->GetMaterial())
                    {
                        material->SetProperty(MaterialProperty::Roughness, 0.75f);
                    }
                }

                // physics
                vector<Entity*> car_parts;
                default_car->GetDescendants(&car_parts);
                for (Entity* car_part : car_parts)
                {
                    if (car_part->GetComponent<Renderable>())
                    {
                        Physics* physics = car_part->AddComponent<Physics>();
                        physics->SetKinematic(true);
                        physics->SetBodyType(BodyType::Mesh);
                    }
                }
            }

            // sounds
            {
                // start
                {
                    Entity* sound = World::CreateEntity();
                    sound->SetObjectName("sound_start");
                    sound->SetParent(default_car);

                    AudioSource* audio_source = sound->AddComponent<AudioSource>();
                    audio_source->SetAudioClip("project\\music\\car_start.wav");
                    audio_source->SetLoop(false);
                    audio_source->SetPlayOnStart(false);
                }

                // idle
                {
                    Entity* sound = World::CreateEntity();
                    sound->SetObjectName("sound_idle");
                    sound->SetParent(default_car);

                    AudioSource* audio_source = sound->AddComponent<AudioSource>();
                    audio_source->SetAudioClip("project\\music\\car_idle.wav");
                    audio_source->SetLoop(true);
                    audio_source->SetPlayOnStart(false);
                }

                // door
                {
                    Entity* sound = World::CreateEntity();
                    sound->SetObjectName("sound_door");
                    sound->SetParent(default_car);

                    AudioSource* audio_source = sound->AddComponent<AudioSource>();
                    audio_source->SetAudioClip("project\\music\\car_door.wav");
                    audio_source->SetLoop(false);
                    audio_source->SetPlayOnStart(false);
                }
            }
        }

        void tick()
        {
            // car
            if (default_car)
            {
                // car views
                enum class CarView { Dashboard, Hood, Chase };
                static CarView current_view = CarView::Dashboard;
                
                // camera positions for different views
                static const Vector3 car_view_positions[] =
                {
                    Vector3(0.5f, 1.8f, -0.6f), // dashboard
                    Vector3(0.0f, 2.0f, 1.0f),  // hood
                    Vector3(0.0f, 3.0f, -10.0f) // chase
                };
            
                // get some commonly used things
                bool inside_the_car             = default_camera->GetChildrenCount() == 0;
                Entity* sound_door_entity       = default_car->GetChildByName("sound_door");
                Entity* sound_start_entity      = default_car->GetChildByName("sound_start");
                Entity* sound_idle_entity       = default_car->GetChildByName("sound_idle");
                AudioSource* audio_source_door  = sound_door_entity  ? sound_door_entity->GetComponent<AudioSource>()  : nullptr;
                AudioSource* audio_source_start = sound_start_entity ? sound_start_entity->GetComponent<AudioSource>() : nullptr;
                AudioSource* audio_source_idle  = sound_idle_entity  ? sound_idle_entity->GetComponent<AudioSource>()  : nullptr;
                if (!audio_source_door || !audio_source_start || !audio_source_idle)
                    return;

                // enter/exit
                if (Input::GetKeyDown(KeyCode::E))
                {
                    Entity* camera = nullptr;
                    if (!inside_the_car)
                    {
                        camera = default_camera->GetChildByName("component_camera");
                        camera->SetParent(default_car);
                        camera->SetPositionLocal(car_view_positions[static_cast<int>(current_view)]);
                        camera->SetRotationLocal(Quaternion::Identity);
            
                        audio_source_start->PlayClip();
            
                        inside_the_car = true;
                    }
                    else
                    {
                        camera = default_car->GetChildByName("component_camera");
                        camera->SetParent(default_camera);
                        camera->SetPositionLocal(Vector3(0.0f, 1.8f, 0.0f));
                        camera->SetRotationLocal(Quaternion::Identity);
            
                        // place the camera on the left of the driver's door
                        default_camera->SetPosition(default_car->GetPosition() + default_car->GetLeft() * 3.0f + Vector3::Up * 2.0f);
            
                        audio_source_idle->StopClip();
            
                        inside_the_car = false;
                    }
            
                    // enable/disable car/camera control
                    camera->GetComponent<Camera>()->SetFlag(CameraFlags::CanBeControlled, !inside_the_car);
                    //default_car->AddComponent<PhysicsBody>()->GetCar()->SetControlEnabled(inside_the_car);
            
                    // play exit/enter sound
                    audio_source_door->PlayClip();
            
                    // disable/enable windshield
                    if (default_car_window)
                    {
                        default_car_window->SetActive(!inside_the_car);
                    }
                }
            
                // change car view
                if (Input::GetKeyDown(KeyCode::V))
                {
                    if (inside_the_car)
                    {
                        if (Entity* camera = default_car->GetChildByName("component_camera"))
                        {
                            current_view = static_cast<CarView>((static_cast<int>(current_view) + 1) % 3);
                            camera->SetPositionLocal(car_view_positions[static_cast<int>(current_view)]);
                        }
                    }
                }

                // osd
                {
                    Renderer::DrawString("WASD: Move Camera/Car | 'E': Enter/Exit Car | 'V': Change Car View", Vector2(0.005f, 0.98f));
                }
            }
        }
    }

    namespace worlds
    {
        void create_sponza_4k()
        {
            // set the mood
            entities::camera(Vector3(19.2692f, 2.65f, 0.1677f), Vector3(-18.0f, -90.0f, 0.0f));
            entities::sun(true);
            default_light_directional->GetComponent<Light>()->SetIntensity(120000.0f); // lux
            entities::music("project\\music\\jake_chudnow_olive.wav");
            Renderer::SetWind(Vector3(0.0f, 0.2f, 1.0f) * 0.1f);

            const Vector3 position = Vector3(0.0f, 1.5f, 0.0f);
            const float scale      = 2.0f; // actually walked in sponza, it's that big

            // 3d model - sponza
            uint32_t mesh_flags  = Mesh::GetDefaultFlags();
            //mesh_flags          |= static_cast<uint32_t>(MeshFlags::ImportLights); // they don't look good for some reason, investigate
            if (shared_ptr<Mesh> mesh = ResourceCache::Load<Mesh>("project\\models\\sponza\\main\\NewSponza_Main_Blender_glTF.gltf", mesh_flags))
            {
                Entity* entity = mesh->GetRootEntity();
                entity->SetObjectName("sponza");
                entity->SetPosition(position);
                entity->SetScale(scale);

                // disable dirt decals since they look bad
                // they are hovering over the surfaces, they have z-fighting, and they also cast shadows underneath them
                entity->GetDescendantByName("decals_1st_floor")->SetActive(false);
                entity->GetDescendantByName("decals_2nd_floor")->SetActive(false);
                entity->GetDescendantByName("decals_3rd_floor")->SetActive(false);

                // enable physics for all meshes
                vector<Entity*> entities;
                entity->GetDescendants(&entities);
                for (Entity* entity_it : entities)
                {
                    if (entity_it->GetActive() && entity_it->GetComponent<Renderable>() != nullptr)
                    {
                        Physics* physics_body = entity_it->AddComponent<Physics>();
                        physics_body->SetBodyType(BodyType::Mesh);
                    }
                }
            }

            // 3d model - curtains
            if (shared_ptr<Mesh> mesh = ResourceCache::Load<Mesh>("project\\models\\sponza\\curtains\\NewSponza_Curtains_glTF.gltf"))
            {
                Entity* entity = mesh->GetRootEntity();
                entity->SetObjectName("sponza_curtains");
                entity->SetPosition(position);
                entity->SetScale(scale);

                // disable back face culling and enable wind
                {
                    // this is fabric
                    if (Material* material = entity->GetDescendantByName("curtain_03_2")->GetComponent<Renderable>()->GetMaterial())
                    {
                        material->SetProperty(MaterialProperty::CullMode,      static_cast<float>(RHI_CullMode::None));
                        material->SetProperty(MaterialProperty::WindAnimation, 1.0f);
                    }

                     // this is fabric
                    if (Material* material = entity->GetDescendantByName("curtain_03_3")->GetComponent<Renderable>()->GetMaterial())
                    {
                        material->SetProperty(MaterialProperty::CullMode,      static_cast<float>(RHI_CullMode::None));
                        material->SetProperty(MaterialProperty::WindAnimation, 1.0f);
                    }

                     // this is fabric
                    if (Material* material = entity->GetDescendantByName("curtain_hanging_06_3")->GetComponent<Renderable>()->GetMaterial())
                    {
                        material->SetProperty(MaterialProperty::CullMode,      static_cast<float>(RHI_CullMode::None));
                        material->SetProperty(MaterialProperty::WindAnimation, 1.0f);
                    }
                }
            }

            // 3d model - ivy
            if (shared_ptr<Mesh> mesh = ResourceCache::Load<Mesh>("project\\models\\sponza\\ivy\\NewSponza_IvyGrowth_glTF.gltf"))
            {
                Entity* entity = mesh->GetRootEntity();
                entity->SetObjectName("sponza_ivy");
                entity->SetPosition(position);
                entity->SetScale(scale);

                if (Material* material = entity->GetDescendantByName("IvySim_Leaves")->GetComponent<Renderable>()->GetMaterial())
                {
                    material->SetProperty(MaterialProperty::CullMode,      static_cast<float>(RHI_CullMode::None));
                    material->SetProperty(MaterialProperty::WindAnimation, 1.0f);
                }
            }
        }

        void create_minecraft()
        {
             entities::camera(Vector3(-51.7576f, 21.4551f, -85.3699f), Vector3(11.3991f, 30.6026f, 0.0f));
             entities::sun(true);
             entities::music();

            // the entire minecraft world is a single mesh so don't optimize or generate lods (it will deteriorate a lot)
            uint32_t mesh_flags  = Mesh::GetDefaultFlags();
            mesh_flags          &= ~static_cast<uint32_t>(MeshFlags::PostProcessOptimize);
            mesh_flags          &= ~static_cast<uint32_t>(MeshFlags::PostProcessGenerateLods);
            if (shared_ptr<Mesh> mesh = ResourceCache::Load<Mesh>("project\\models\\vokselia_spawn\\vokselia_spawn.obj", mesh_flags))
            {
                Entity* entity = mesh->GetRootEntity();
                entity->SetObjectName("minecraft");
                entity->SetScale(100.0f);

                // enable physics for all meshes
                vector<Entity*> entities;
                entity->GetDescendants(&entities);
                for (Entity* entity_it : entities)
                {
                    if (entity_it->GetComponent<Renderable>() != nullptr)
                    {
                        Physics* physics_body = entity_it->AddComponent<Physics>();
                        physics_body->SetBodyType(BodyType::Mesh);
                    }
                }
            }
        }

        void create_subway_gi_test()
        {
            entities::sun(false);

            entities::camera();
            //default_camera->GetChildByIndex(0)->GetComponent<Camera>()->SetFlag(CameraFlags::Flashlight, true); // if you do that, you get a GPU crash, fix

            if (shared_ptr<Mesh> mesh = ResourceCache::Load<Mesh>("project\\models\\free-subway-station-r46-subway\\Metro.fbx"))
            {
                Entity* entity = mesh->GetRootEntity();
                entity->SetObjectName("subway");
                entity->SetScale(Vector3(0.015f));
                
                // enable physics for all meshes
                vector<Entity*> entities;
                entity->GetDescendants(&entities);
                for (Entity* entity_it : entities)
                {
                    if (entity_it->GetComponent<Renderable>() != nullptr)
                    {
                        Physics* physics_body = entity_it->AddComponent<Physics>();
                        physics_body->SetBodyType(BodyType::Mesh);
                    }
                }
            }
        }

        namespace forest
        {
            void create()
            {
                // tweak without exceeding a vram usage of 8 GB (that is until streaming is implemented)
                const float render_distance_trees          = 1'500.0f;
                const float render_distance_grass          = 350.0f;
                const uint32_t per_tile_count_grass_blades = 250'000;
                const uint32_t per_tile_count_tree         = 16;
                const uint32_t per_tile_count_rock         = 32;
                const float shadow_distance                = 150.0f; // beyond that, screen space shadows are enough

                // sun/lighting/mood
                entities::sun(true);
                Light* sun = default_light_directional->GetComponent<Light>();
                sun->SetIntensity(20'000.0f);
                sun->SetTemperature(3'800.0f); // kelvin - warm light
                sun->SetFlag(LightFlags::Volumetric, false);
                sun->GetEntity()->SetRotation(Quaternion::FromEulerAngles(10.0f, -100.0f, -0.5f));

                entities::camera(Vector3(-1476.0f, 17.9f, 1490.0f), Vector3(-3.6f, 90.0f, 0.0f));
                Renderer::SetOption(Renderer_Option::Grid, 0.0f);

                // create
                default_terrain = World::CreateEntity();
                default_terrain->SetObjectName("terrain");

                // sound
                {
                    Entity* entity = World::CreateEntity();
                    entity->SetObjectName("audio");

                    // footsteps grass
                    {
                        Entity* sound = World::CreateEntity();
                        sound->SetObjectName("footsteps");
                        sound->SetParent(entity);

                        AudioSource* audio_source = sound->AddComponent<AudioSource>();
                        audio_source->SetAudioClip("project\\music\\footsteps_grass.wav");
                        audio_source->SetPlayOnStart(false);
                    }

                    // forest and river sounds
                    {
                        Entity* sound = World::CreateEntity();
                        sound->SetObjectName("forest_river");
                        sound->SetParent(entity);

                        AudioSource* audio_source = sound->AddComponent<AudioSource>();
                        audio_source->SetAudioClip("project\\music\\forest_river.wav");
                        audio_source->SetLoop(true);
                    }

                    // wind
                    {
                        Entity* sound = World::CreateEntity();
                        sound->SetObjectName("wind");
                        sound->SetParent(entity);

                        AudioSource* audio_source = sound->AddComponent<AudioSource>();
                        audio_source->SetAudioClip("project\\music\\wind.wav");
                        audio_source->SetLoop(true);
                    }

                    // underwater
                    {
                        Entity* sound = World::CreateEntity();
                        sound->SetObjectName("underwater");
                        sound->SetParent(entity);

                        AudioSource* audio_source = sound->AddComponent<AudioSource>();
                        audio_source->SetAudioClip("project\\music\\underwater.wav");
                        audio_source->SetPlayOnStart(false);
                    }
                }

                // terrain
                Terrain* terrain = default_terrain->AddComponent<Terrain>();
                {
                    // add renderable component with a material
                    {
                        shared_ptr<Material> material = terrain->GetMaterial();

                        // set properties
                        material->SetResourceFilePath(string("project\\materials\\material_terrain") + string(EXTENSION_MATERIAL));
                        material->SetProperty(MaterialProperty::IsTerrain, 1.0f);
                        material->SetProperty(MaterialProperty::TextureTilingX, 250.0f);
                        material->SetProperty(MaterialProperty::TextureTilingY, 250.0f);

                        // set textures
                        material->SetTexture(MaterialTextureType::Color,     "project\\materials\\whispy_grass_meadow\\albedo.png",    0);
                        material->SetTexture(MaterialTextureType::Normal,    "project\\materials\\whispy_grass_meadow\\normal.png",    0);
                        material->SetTexture(MaterialTextureType::Roughness, "project\\materials\\whispy_grass_meadow\\roughness.png", 0);
                        material->SetTexture(MaterialTextureType::Occlusion, "project\\materials\\whispy_grass_meadow\\occlusion.png", 0);
                        material->SetTexture(MaterialTextureType::Color,     "project\\materials\\rock\\albedo.png",                   1);
                        material->SetTexture(MaterialTextureType::Normal,    "project\\materials\\rock\\normal.png",                   1);
                        material->SetTexture(MaterialTextureType::Roughness, "project\\materials\\rock\\roughness.png",                1);
                        material->SetTexture(MaterialTextureType::Occlusion, "project\\materials\\rock\\occlusion.png",                1);
                        material->SetTexture(MaterialTextureType::Height,    "project\\materials\\rock\\height.png",                   1);
                        material->SetTexture(MaterialTextureType::Color,     "project\\materials\\sand\\albedo.png",                   2);
                        material->SetTexture(MaterialTextureType::Normal,    "project\\materials\\sand\\normal.png",                   2);
                        material->SetTexture(MaterialTextureType::Roughness, "project\\materials\\sand\\roughness.png",                2);
                        material->SetTexture(MaterialTextureType::Occlusion, "project\\materials\\sand\\occlusion.png",                2);
                        material->SetProperty(MaterialProperty::Tessellation, 0.0f);
                    }

                    // generate a terrain from a height map
                    shared_ptr<RHI_Texture> height_map = ResourceCache::Load<RHI_Texture>("project\\height_maps\\height_map.png");
                    terrain->SetHeightMap(height_map.get());
                    terrain->Generate();

                    // add physics so we can walk on it
                    for (Entity* terrain_tile : terrain->GetEntity()->GetChildren())
                    {
                        Physics* physics_body = terrain_tile->AddComponent<Physics>();
                        physics_body->SetBodyType(BodyType::Mesh);
                    }
                }

                // water
                const float dimension  = 8000; // meters
                const uint32_t density = 64;   // geometric
                const Color forest_water_color = Color(0.0f / 255.0f, 150.0f / 255.0f, 70.0f / 255.0f, 220.0f / 255.0f);
                entities::water(Vector3(0.0f, 0.0f, 0.0f), dimension, density, forest_water_color, 5.0f, 0.1f);

                // props: trees, rocks, grass
                {
                    // load meshes
                    uint32_t flags = Mesh::GetDefaultFlags() | static_cast<uint32_t>(MeshFlags::ImportCombineMeshes); // combine gazillion entities tree entites into one
                    shared_ptr<Mesh> mesh_tree = ResourceCache::Load<Mesh>("project\\models\\tree\\tree.fbx", flags);
                    shared_ptr<Mesh> mesh_rock = ResourceCache::Load<Mesh>("project\\models\\rock_2\\model.obj");

                    // create mesh for grass blade
                    shared_ptr<Mesh> mesh_grass_blade = meshes.emplace_back(make_shared<Mesh>());
                    {
                        mesh_grass_blade->SetFlag(static_cast<uint32_t>(MeshFlags::PostProcessOptimize), false); // geometry is made to spec, don't optimize
                        mesh_grass_blade->SetLodDropoff(MeshLodDropoff::Linear);                                 // linear dropoff - more aggressive

                        // create sub-mesh and add three lods for the grass blade
                        uint32_t sub_mesh_index = 0;

                        // lod 0: high quality grass blade (6 segments)
                        {
                            vector<RHI_Vertex_PosTexNorTan> vertices;
                            vector<uint32_t> indices;
                            geometry_generation::generate_grass_blade(&vertices, &indices, 6);        // high detail
                            mesh_grass_blade->AddGeometry(vertices, indices, false, &sub_mesh_index); // add lod 0, no auto-lod generation
                        }

                        // lod 1: medium quality grass blade (2 segments)
                        {
                            vector<RHI_Vertex_PosTexNorTan> vertices;
                            vector<uint32_t> indices;
                            geometry_generation::generate_grass_blade(&vertices, &indices, 1); // medium detail
                            mesh_grass_blade->AddLod(vertices, indices, sub_mesh_index);       // add lod 1
                        }

                        mesh_grass_blade->SetResourceFilePath(string(ResourceCache::GetProjectDirectory()) + "standard_grass" + EXTENSION_MESH); // silly, need to remove that
                        mesh_grass_blade->CreateGpuBuffers();                                                                                    // aabb, gpu buffers, etc.
                    }

                    // materials
                    shared_ptr<Material> material_leaf;
                    shared_ptr<Material> material_body;
                    shared_ptr<Material> material_rock;
                    shared_ptr<Material> material_grass;
                    {
                        material_leaf = make_shared<Material>();
                        material_leaf->SetTexture(MaterialTextureType::Color, "project\\models\\tree\\Twig_Base_Material_2.png");
                        material_leaf->SetTexture(MaterialTextureType::Normal, "project\\models\\tree\\Twig_Normal.png");
                        material_leaf->SetTexture(MaterialTextureType::AlphaMask, "project\\models\\tree\\Twig_Opacity_Map.jpg");
                        material_leaf->SetProperty(MaterialProperty::WindAnimation, 1.0f);
                        material_leaf->SetProperty(MaterialProperty::ColorVariationFromInstance, 1.0f);
                        material_leaf->SetProperty(MaterialProperty::SubsurfaceScattering, 0.0f);
                        material_leaf->SetResourceFilePath("project\\materials\\tree_leaf" + string(EXTENSION_MATERIAL));

                        material_body = make_shared<Material>();
                        material_body->SetTexture(MaterialTextureType::Color, "project\\models\\tree\\tree_bark_diffuse.png");
                        material_body->SetTexture(MaterialTextureType::Normal, "project\\models\\tree\\tree_bark_normal.png");
                        material_body->SetTexture(MaterialTextureType::Roughness, "project\\models\\tree\\tree_bark_roughness.png");
                        material_body->SetResourceFilePath("project\\materials\\tree_body" + string(EXTENSION_MATERIAL));

                        material_rock = make_shared<Material>();
                        material_rock->SetResourceFilePath("project\\materials\\material_rock" + string(EXTENSION_MATERIAL));
                        material_rock->SetTexture(MaterialTextureType::Color, "project\\models\\rock_2\\albedo.png");
                        material_rock->SetTexture(MaterialTextureType::Normal, "project\\models\\rock_2\\normal.png");
                        material_rock->SetTexture(MaterialTextureType::Roughness, "project\\models\\rock_2\\roughness.png");
                        material_rock->SetTexture(MaterialTextureType::Occlusion, "project\\models\\rock_2\\occlusion.png");

                        material_grass = make_shared<Material>();
                        material_grass->SetResourceFilePath("project\\materials\\material_grass_blade" + string(EXTENSION_MATERIAL));
                        material_grass->SetProperty(MaterialProperty::IsGrassBlade, 1.0f);
                        material_grass->SetProperty(MaterialProperty::Roughness, 1.0f);
                        material_grass->SetProperty(MaterialProperty::Clearcoat, 1.0f);
                        material_grass->SetProperty(MaterialProperty::Clearcoat_Roughness, 0.2f);
                        material_grass->SetProperty(MaterialProperty::SubsurfaceScattering, 0.0f);
                        material_grass->SetColor(Color::standard_white);
                    }

                    // place props on each terrain tile
                    vector<Entity*> children = terrain->GetEntity()->GetChildren();
                    auto place_props_on_tiles = [
                        &children,
                        &mesh_rock,
                        &mesh_tree,
                        &mesh_grass_blade,
                        &terrain,
                        render_distance_trees,
                        render_distance_grass,
                        shadow_distance,
                        material_leaf,
                        material_body,
                        material_rock,
                        material_grass
                    ](uint32_t start_index, uint32_t end_index)
                    {
                        for (uint32_t tile_index = start_index; tile_index < end_index; tile_index++)
                        {
                            Entity* terrain_tile = children[tile_index];

                            // tree
                            {
                                Entity* entity = mesh_tree->GetRootEntity()->Clone();
                                entity->SetObjectName("tree");
                                entity->SetParent(terrain_tile);

                                // generate instances
                                vector<Matrix> transforms;
                                terrain->FindTransforms(tile_index, per_tile_count_tree, TerrainProp::Tree, entity, 0.04f, transforms);

                                // set renderable component
                                if (Entity* trunk = entity->GetChildByIndex(0))
                                {
                                    Renderable* renderable = trunk->GetComponent<Renderable>();
                                    renderable->SetInstances(transforms);
                                    renderable->SetMaxRenderDistance(render_distance_trees);
                                    renderable->SetMaxShadowDistance(shadow_distance);
                                    renderable->SetMaterial(material_body);
                                }

                                // set renderable component
                                if (Entity* leafs = entity->GetChildByIndex(1))
                                {
                                    Renderable* renderable = leafs->GetComponent<Renderable>();
                                    renderable->SetInstances(transforms);
                                    renderable->SetMaxRenderDistance(render_distance_trees);
                                    renderable->SetMaxShadowDistance(shadow_distance);
                                    renderable->SetMaterial(material_leaf);
                                }
                            }

                            // rock
                            {
                                Entity* entity = mesh_rock->GetRootEntity()->Clone();
                                entity->SetObjectName("rock");
                                entity->SetParent(terrain_tile);

                                // generate instances
                                {
                                    vector<Matrix> transforms;
                                    terrain->FindTransforms(tile_index, per_tile_count_rock, TerrainProp::Rock, entity, 2.0f, transforms);

                                    if (Entity* rock_entity = entity->GetDescendantByName("untitled")) // where the model keeps the mesh
                                    {
                                        // set renderable component
                                        Renderable* renderable = rock_entity->GetComponent<Renderable>();
                                        renderable->SetInstances(transforms);
                                        renderable->SetMaxRenderDistance(render_distance_trees);
                                        renderable->SetMaxShadowDistance(shadow_distance);
                                        renderable->SetMaterial(material_rock);

                                        // add physics so we can collide with rocks
                                        //Physics* physics = rock_entity->AddComponent<Physics>();
                                        //physics->SetBodyType(BodyType::Mesh);
                                    }
                                }
                            }

                            // grass
                            {
                               // create entity
                               Entity* entity = World::CreateEntity();
                               entity->SetObjectName("grass");
                               entity->SetParent(terrain_tile);

                               // generate instances
                               vector<Matrix> transforms;
                               terrain->FindTransforms(tile_index, per_tile_count_grass_blades, TerrainProp::Grass, entity, 1.0f, transforms);

                               // set renderable component
                               Renderable* renderable = entity->AddComponent<Renderable>();
                               renderable->SetMesh(mesh_grass_blade.get());
                               renderable->SetFlag(RenderableFlags::CastsShadows, false); // screen space shadows are enough
                               renderable->SetInstances(transforms);
                               renderable->SetMaterial(material_grass);
                               renderable->SetMaxRenderDistance(render_distance_grass);
                            }
                        }
                    };

                    // execute in parallel
                    ThreadPool::ParallelLoop(place_props_on_tiles, static_cast<uint32_t>(children.size()));
                }
            }

            void tick()
            {
                Camera*  camera  = World::GetCamera();
                Terrain* terrain = default_terrain->GetComponent<Terrain>();
                if (!camera || !terrain)
                    return;

                // sound
                {
                    bool is_below_water_level = camera->GetEntity()->GetPosition().y < 0.0f;

                    // underwater
                    {
                        if (Entity* entity = default_terrain->GetDescendantByName("underwater"))
                        {
                            if (AudioSource* audio_source = entity->GetComponent<AudioSource>())
                            {
                                if (is_below_water_level && !audio_source->IsPlaying())
                                {
                                    audio_source->PlayClip();
                                }
                                else if (!is_below_water_level && audio_source->IsPlaying())
                                {
                                    audio_source->StopClip();
                                }
                            }
                        }
                    }

                    // footsteps
                    if (!is_below_water_level)
                    {
                        if (Entity* entity = default_terrain->GetDescendantByName("footsteps"))
                        {
                            if (AudioSource* audio_source = entity->GetComponent<AudioSource>())
                            {
                                if (camera->IsWalking() && !audio_source->IsPlaying())
                                {
                                    audio_source->PlayClip();
                                }
                                else if (!camera->IsWalking() && audio_source->IsPlaying())
                                {
                                    audio_source->StopClip();
                                }
                            }
                        }
                    }
                }
            }
        }

        namespace showroom
        {
            shared_ptr<RHI_Texture> texture_brand_logo;
            shared_ptr<RHI_Texture> texture_paint_normal;
            Entity* turn_table = nullptr;

            void create()
            {
                // gran turismo 7 brand central music
                entities::music("project\\music\\gran_turismo.wav");
                
                // textures
                texture_brand_logo   = make_shared<RHI_Texture>("project\\models\\ferrari_laferrari\\logo.png");
                texture_paint_normal = make_shared<RHI_Texture>("project\\models\\ferrari_laferrari\\paint_normal.png");

                car::create(Vector3(0.0f, 0.08f, 0.0f), false, texture_paint_normal);

                // camera
                {
                    Vector3 camera_position = Vector3(5.0f, 1.5f, -10.0f);
                    entities::camera(camera_position);
                    Vector3 direction = (default_car->GetPosition() - camera_position).Normalized();
                    default_camera->GetChildByIndex(0)->SetRotationLocal(Quaternion::FromLookRotation(direction, Vector3::Up));
                    default_camera->GetChildByIndex(0)->GetComponent<Camera>()->SetFlag(CameraFlags::Flashlight, true);
                }

                // emissive tube lights and floor
                {
                    // load and render model at max geometry quality
                    uint32_t mesh_flags  = Mesh::GetDefaultFlags();
                    mesh_flags          &= static_cast<uint32_t>(MeshFlags::ImportLights);
                    mesh_flags          &= static_cast<uint32_t>(MeshFlags::ImportCombineMeshes);
                    mesh_flags          &= ~static_cast<uint32_t>(MeshFlags::PostProcessOptimize);     // don't reduce vertex/index count
                    mesh_flags          &= ~static_cast<uint32_t>(MeshFlags::PostProcessGenerateLods); // don't genereate and use LODs
                    if (shared_ptr<Mesh> mesh = ResourceCache::Load<Mesh>("project\\models\\ferrari_laferrari\\SpartanLaFerrariV2\\LaFerrariV2.gltf", mesh_flags))
                    {
                        Entity* floor_tube_lights = mesh->GetRootEntity();
                        floor_tube_lights->SetObjectName("tube_lights_and_floor");
                        floor_tube_lights->SetScale(2.0f);

                        // configure the tube lights
                        auto setup_tube_light = [floor_tube_lights](const char* descendant_name, Color color)
                        {
                            if (Entity* entity_tube_light = floor_tube_lights->GetDescendantByName(descendant_name))
                            {
                                entity_tube_light->GetComponent<Renderable>()->SetFlag(RenderableFlags::CastsShadows, false);
                                if (Material* material = entity_tube_light->GetComponent<Renderable>()->GetMaterial())
                                {
                                    material->SetColor(color);
                                    material->SetProperty(MaterialProperty::EmissiveFromAlbedo, 1.0f);
                        
                                    // until we support actual area lights matching each tube, just use a point light
                                    {
                                        Entity* entity = World::CreateEntity();
                                        entity->SetObjectName("light_point");
                                        entity->SetParent(entity_tube_light);
                        
                                        Light* light = entity->AddComponent<Light>();
                                        light->SetLightType(LightType::Point);
                                        light->SetColor(color);
                                        light->SetRange(40.0f);
                                        light->SetIntensity(20000.0f);
                                        light->SetFlag(LightFlags::Shadows,            true);
                                        light->SetFlag(LightFlags::ShadowsScreenSpace, false);
                                        light->SetFlag(LightFlags::Volumetric,         false);
                                    }
                                }
                            }
                       };
                       setup_tube_light("SM_TubeLight.007_1", Color(1.0f, 0.4f, 0.4f, 1.0f)); // bright red-pink
                       setup_tube_light("SM_TubeLight.004_1", Color(0.4f, 0.8f, 1.0f, 1.0f)); // bright cyan-blue
                       setup_tube_light("SM_TubeLight.006_1", Color(1.0f, 1.0f, 0.9f, 1.0f)); // warm white

                       // add physics to all the tube lights so we can collide with them
                       vector<Entity*> descendants;
                       floor_tube_lights->GetDescendants(&descendants);
                       for (Entity* descendant : descendants)
                       {
                           if (descendant->GetComponent<Renderable>())
                           {
                               descendant->AddComponent<Physics>()->SetBodyType(BodyType::Mesh);
                           }
                       }

                       // configure the floor
                       if (Entity* entity_floor = floor_tube_lights->GetDescendantByName("Floor"))
                       {
                           // scale the floor to be larger
                           const float scale = 100.0f;
                           entity_floor->SetScale(scale);
                           if (Material* material = entity_floor->GetComponent<Renderable>()->GetMaterial())
                           {
                               material->SetProperty(MaterialProperty::TextureTilingX, scale);
                               material->SetProperty(MaterialProperty::TextureTilingY, scale);
                               material->SetProperty(MaterialProperty::Metalness, 0.0f);
                           }
                       
                           // add physics to the floor so we can walk on it
                           entity_floor->GetComponent<Physics>()->SetBodyType(BodyType::Plane);
                       }

                       // make the car, a child of the turn table so that it rotates with it
                       if (turn_table = floor_tube_lights->GetDescendantByName("TurnTable"))
                       {
                           default_car->SetParent(turn_table);
                           default_car->SetScaleLocal(1.0f);
                           turn_table->SetPositionLocal(0.0f); // lower it a bit because it seems to have some weird alpha testing going on below it
                           if (Material* material = turn_table->GetComponent<Renderable>()->GetMaterial())
                           {
                                material->SetColor(Color::standard_black);
                           }
                           turn_table->GetComponent<Physics>()->SetKinematic(true);
                       }
                    }
                }

                // adjust renderer options
                {
                    Renderer::SetOption(Renderer_Option::PerformanceMetrics, 0.0f);
                    Renderer::SetOption(Renderer_Option::Lights,             0.0f);
                    Renderer::SetOption(Renderer_Option::Dithering,          0.0f);
                }
            }

            void tick()
            {
                // slow rotation: rotate car around y-axis (vertical)
                float rotation_speed = 0.15f; // radians per second
                float delta_time     = static_cast<float>(Timer::GetDeltaTimeSec());
                float angle          = rotation_speed * delta_time;
                Quaternion rotation  = Quaternion::FromAxisAngle(Vector3::Up, angle);
                turn_table->Rotate(rotation);
            
                const float x       = 0.75f;
                const float y       = 0.05f;
                const float spacing = 0.02f;
            
                // buffer for formatted text
                static char text_buffer[128];
            
                // car specs
                Renderer::DrawString("Ferrari LaFerrari", Vector2(x, y));
            
                snprintf(text_buffer, sizeof(text_buffer), "Torque: %.1f Nm", 900.0f);
                Renderer::DrawString(text_buffer, Vector2(x, y + spacing * 1));
            
                snprintf(text_buffer, sizeof(text_buffer), "Weight: %.1f kg", 1585.0f);
                Renderer::DrawString(text_buffer, Vector2(x, y + spacing * 2));
            
                snprintf(text_buffer, sizeof(text_buffer), "Power: %.1f kW", 708.0f);
                Renderer::DrawString(text_buffer, Vector2(x, y + spacing * 3));
            
                snprintf(text_buffer, sizeof(text_buffer), "Top Speed: %.1f km/h", 350.0f);
                Renderer::DrawString(text_buffer, Vector2(x, y + spacing * 4));
            
                Renderer::DrawString("Engine: 6.3L V12 + HY-KERS", Vector2(x, y + spacing * 5));
                Renderer::DrawString("Drivetrain: RWD", Vector2(x, y + spacing * 6));
            
                snprintf(text_buffer, sizeof(text_buffer), "0-100 km/h: %.1f s", 2.6f);
                Renderer::DrawString(text_buffer, Vector2(x, y + spacing * 7));
            
                snprintf(text_buffer, sizeof(text_buffer), "Power/Weight: %.1f kW/ton", 446.7f);
                Renderer::DrawString(text_buffer, Vector2(x, y + spacing * 8));
            
                Renderer::DrawString("Production: 2013-2018", Vector2(x, y + spacing * 9));
                Renderer::DrawString("Flagship Hypercar: Ferrari's Hybrid Masterpiece", Vector2(x, y + spacing * 10));
            
                // description
                Renderer::DrawString("The LaFerrari is Ferrari's first hybrid hypercar, blending a 6.3L V12 with", Vector2(x, y + spacing * 12));
                Renderer::DrawString("an electric motor via its HY-KERS system. It delivers extreme performance", Vector2(x, y + spacing * 13));
                Renderer::DrawString("and razor-sharp dynamics, wrapped in a design that embodies pure", Vector2(x, y + spacing * 14));
                Renderer::DrawString("Ferrari DNA. A limited-production icon of modern automotive engineering.", Vector2(x, y + spacing * 15));
            
                // logo
                Renderer::DrawIcon(texture_brand_logo.get(), Vector2(400.0f, 300.0f));
            }
        }

        namespace liminal_space
        {
            void create()
            {
                // shared material for surfaces
                shared_ptr<Material> tile_material = make_shared<Material>();
                tile_material->SetResourceFilePath("project\\materials\\material_floor_tile" + string(EXTENSION_MATERIAL));
                tile_material->SetTexture(MaterialTextureType::Color, "project\\materials\\tile_white\\albedo.png");
                tile_material->SetTexture(MaterialTextureType::Normal, "project\\materials\\tile_white\\normal.png");
                tile_material->SetTexture(MaterialTextureType::Metalness, "project\\materials\\tile_white\\metallic.png");
                tile_material->SetTexture(MaterialTextureType::Roughness, "project\\materials\\tile_white\\roughness.png");
                tile_material->SetTexture(MaterialTextureType::Occlusion, "project\\materials\\tile_white\\ao.png");
                tile_material->SetProperty(MaterialProperty::WorldSpaceUv, 1.0f);
                tile_material->SetProperty(MaterialProperty::TextureTilingX, 5.0f);
                tile_material->SetProperty(MaterialProperty::TextureTilingY, 5.0f);

                // pool light mesh
                Entity* entity_pool_light = nullptr;
                uint32_t flags  = Mesh::GetDefaultFlags() | static_cast<uint32_t>(MeshFlags::ImportCombineMeshes);
                flags          &= ~static_cast<uint32_t>(MeshFlags::PostProcessGenerateLods); // already very simple
                if (shared_ptr<Mesh> mesh = ResourceCache::Load<Mesh>("project\\models\\pool_light\\pool_light.blend", flags))
                {
                    entity_pool_light = mesh->GetRootEntity();
                    entity_pool_light->SetObjectName("pool_light");
                    entity_pool_light->SetScale(0.5f);
                    entity_pool_light->SetPosition(Vector3(0.0f, 1000.0f, 0.0f)); // hide blueprint
                    entity_pool_light->GetChildByIndex(3)->SetActive(false);
            
                    // outer metallic ring
                    shared_ptr<Material> material_metal = make_shared<Material>();
                    material_metal->SetResourceFilePath("project\\materials\\material_metal" + string(EXTENSION_MATERIAL));
                    material_metal->SetProperty(MaterialProperty::Roughness, 0.5f);
                    material_metal->SetProperty(MaterialProperty::Metalness, 1.0f);
                    entity_pool_light->GetChildByName("Circle")->GetComponent<Renderable>()->SetMaterial(material_metal);
            
                    // inner light paraboloid
                    shared_ptr<Material> material_paraboloid = make_shared<Material>();
                    material_paraboloid->SetResourceFilePath("project\\materials\\material_paraboloid" + string(EXTENSION_MATERIAL));
                    material_paraboloid->SetTexture(MaterialTextureType::Emission, "project\\models\\pool_light\\emissive.png");
                    material_paraboloid->SetProperty(MaterialProperty::Roughness, 0.5f);
                    material_paraboloid->SetProperty(MaterialProperty::Metalness, 1.0f);
                    entity_pool_light->GetChildByName("Circle.001")->GetComponent<Renderable>()->SetMaterial(material_paraboloid);
            
                    // point light
                    //Entity* light_source = entity_pool_light->GetChildByIndex(2);
                    //light_source->SetPositionLocal(Vector3(0.0f, 0.0f, -0.5f));
                    //Light* light = light_source->AddComponent<Light>();
                    //light->SetLightType(LightType::Point);
                    //light->SetIntensity(2500.0f);
                    //light->SetTemperature(5500.0f);
                    //light->SetRange(15.0f);
                    //light->SetFlag(LightFlags::Shadows, false);
                    //light->SetFlag(LightFlags::ShadowsScreenSpace, false);
                }
            
                // renderer options
                Renderer::SetOption(Renderer_Option::ChromaticAberration, 1.0f);
                Renderer::SetOption(Renderer_Option::Vhs, 1.0f);
            
                // camera
                entities::camera(Vector3(5.4084f, 1.8f, 4.7593f));
                default_camera->GetChildByIndex(0)->GetComponent<Camera>()->SetFlag(CameraFlags::Flashlight, true);
            
                // audio hum
                Entity* entity_hum = World::CreateEntity();
                entity_hum->SetObjectName("audio_hum_electric");
                entity_hum->SetParent(default_camera);
                AudioSource* audio_source = entity_hum->AddComponent<AudioSource>();
                audio_source->SetAudioClip("project\\music\\hum_electric.wav");
                audio_source->SetLoop(true);
                audio_source->SetVolume(0.25f);
            
                // tile footsteps
                Entity* entity_tiles = World::CreateEntity();
                entity_tiles->SetObjectName("audio_footsteps_tiles");
                entity_tiles->SetParent(default_camera);
                AudioSource* audio_source_tiles = entity_tiles->AddComponent<AudioSource>();
                audio_source_tiles->SetAudioClip("project\\music\\footsteps_tiles.wav");
                audio_source_tiles->SetPlayOnStart(false);
            
                // water footsteps
                Entity* entity_water = World::CreateEntity();
                entity_water->SetObjectName("audio_footsteps_water");
                entity_water->SetParent(default_camera);
                AudioSource* audio_source_water = entity_water->AddComponent<AudioSource>();
                audio_source_water->SetAudioClip("project\\music\\footsteps_water.wav");
                audio_source_water->SetPlayOnStart(false);

                // constants
                const float ROOM_WIDTH  = 40.0f;
                const float ROOM_DEPTH  = 40.0f;
                const float ROOM_HEIGHT = 100.0f;
                const float DOOR_WIDTH  = 2.0f;
                const float DOOR_HEIGHT = 5.0f;
                const int   NUM_ROOMS   = 100;
            
                // direction enum
                enum class Direction { Front, Back, Left, Right, Max };
            
                // rng
                mt19937 rng(random_device{}());
                auto rand_int = [&](int max)
                {
                    uniform_int_distribution<int> dist(0, max - 1);
                    return dist(rng);
                };
            
                // create surface lambda
                auto create_surface = [&](const char* name, const Vector3& pos, const Vector3& scale, Entity* parent)
                {
                    auto entity = World::CreateEntity();
                    entity->SetObjectName(name);
                    entity->SetPosition(pos);
                    entity->SetScale(scale);
                    entity->SetParent(parent);
                    auto renderable = entity->AddComponent<Renderable>();
                    renderable->SetMesh(MeshType::Cube);
                    renderable->SetMaterial(tile_material);
                    auto physics_body = entity->AddComponent<Physics>();
                    physics_body->SetMass(0.0f);
                    physics_body->SetBodyType(BodyType::Box);
                };
            
                // create door lambda
                auto create_door = [&](Direction dir, const Vector3& offset, Entity* parent)
                {
                    string base_name = "wall_" + to_string(static_cast<int>(dir) + 1);
                    bool isFb = (dir == Direction::Front || dir == Direction::Back);
                    float wall_pos = (dir == Direction::Front || dir == Direction::Left) ? -0.5f : 0.5f;
                    wall_pos *= isFb ? ROOM_DEPTH : ROOM_WIDTH;
            
                    // top
                    create_surface((base_name + "_top").c_str(),
                        Vector3(isFb ? 0 : wall_pos, (ROOM_HEIGHT + DOOR_HEIGHT) / 2, isFb ? wall_pos : 0) + offset,
                        Vector3(isFb ? ROOM_WIDTH : 1, ROOM_HEIGHT - DOOR_HEIGHT, isFb ? 1 : ROOM_DEPTH),
                        parent);
            
                    // sides
                    float dim = isFb ? ROOM_WIDTH : ROOM_DEPTH;
                    float side_w = (dim - DOOR_WIDTH) / 2;
                    float l_pos = -dim / 2 + side_w / 2;
                    float r_pos = dim / 2 - side_w / 2;
            
                    create_surface((base_name + "_left").c_str(),
                        Vector3(isFb ? l_pos : wall_pos, DOOR_HEIGHT / 2, isFb ? wall_pos : l_pos) + offset,
                        Vector3(isFb ? side_w : 1, DOOR_HEIGHT, isFb ? 1 : side_w),
                        parent);
            
                    create_surface((base_name + "_right").c_str(),
                        Vector3(isFb ? r_pos : wall_pos, DOOR_HEIGHT / 2, isFb ? wall_pos : r_pos) + offset,
                        Vector3(isFb ? side_w : 1, DOOR_HEIGHT, isFb ? 1 : side_w),
                        parent);
                };
            
                // create room lambda
                auto create_room = [&](Direction door_dir, Direction skip_dir, const Vector3& offset, int room_index)
                {
                    auto room_entity = World::CreateEntity();
                    room_entity->SetObjectName("room_" + to_string(room_index));
                    room_entity->SetPosition(offset);
            
                    // pool chance
                    uniform_real_distribution<float> dist(0.0f, 1.0f);
                    bool is_pool  = dist(rng) < 0.5f;
                    float floor_y = is_pool ? -0.5f : 0.0f;
            
                    // floor and ceiling
                    create_surface("floor", Vector3(0, floor_y, 0), Vector3(ROOM_WIDTH, 1, ROOM_DEPTH), room_entity);
                    create_surface("ceiling", Vector3(0, ROOM_HEIGHT, 0), Vector3(ROOM_WIDTH, 1, ROOM_DEPTH), room_entity);
            
                    // water
                    if (is_pool)
                    {
                        float water_distance = 0.5f; // distance from floor
                        float water_y        = floor_y + 0.5f + water_distance;
                        Color pool_color     = Color(0.0f, 150.0f / 255.0f, 130.0f / 255.0f, 254.0f / 255.0f);
                        auto water           = entities::water(Vector3(0, water_y, 0), ROOM_WIDTH, 2, pool_color, 2.0f, 0.1f);
                        water->SetParent(room_entity);
                    }

                    // wall configs
                    const struct WallConfig
                    {
                        Vector3 pos;
                        Vector3 scale;
                    } walls[] = {
                        {Vector3(0, ROOM_HEIGHT / 2, -ROOM_DEPTH / 2), {ROOM_WIDTH, ROOM_HEIGHT, 1}}, // front
                        {Vector3(0, ROOM_HEIGHT / 2, ROOM_DEPTH / 2), {ROOM_WIDTH, ROOM_HEIGHT, 1}}, // back
                        {Vector3(-ROOM_WIDTH / 2, ROOM_HEIGHT / 2, 0), {1, ROOM_HEIGHT, ROOM_DEPTH}}, // left
                        {Vector3(ROOM_WIDTH / 2, ROOM_HEIGHT / 2, 0), {1, ROOM_HEIGHT, ROOM_DEPTH}} // right
                    };
            
                    for (int i = 0; i < 4; ++i)
                    {
                        Direction dir = static_cast<Direction>(i);
                        if (dir == skip_dir) continue;
            
                        if (dir == door_dir)
                        {
                            create_door(dir, Vector3(0, 0, 0), room_entity);
                        }
                        else
                        {
                            string name = "wall_" + to_string(i + 1);
                            create_surface(name.c_str(), walls[i].pos, walls[i].scale, room_entity);
                        }
            
                        // light on side walls
                        if (dir == Direction::Left || dir == Direction::Right)
                        {
                            const float height = 1.5f;
                            Entity* light_clone = entity_pool_light->Clone();
                            light_clone->SetObjectName("pool_light_" + to_string(i));
                            light_clone->SetParent(room_entity);
                            light_clone->SetScale(0.5f);
                            light_clone->SetPositionLocal(Vector3(walls[i].pos.x, height, walls[i].pos.z));
                            Vector3 direction = (Vector3(0, height, 0) - Vector3(walls[i].pos.x, height, walls[i].pos.z)).Normalized();
                            light_clone->SetRotation(Quaternion::FromLookRotation(direction, Vector3::Up));
                            light_clone->SetActive(false);
                        }
                    }
                };
            
                // procedural generation
                vector<pair<int, int>> path;
                set<pair<int, int>> occupied;
            
                auto generate_path = [&](auto&& self, pair<int, int> pos, int remaining) -> bool
                {
                    path.push_back(pos);
                    occupied.insert(pos);
                    if (remaining == 0) return true;
            
                    vector<Direction> dirs = { Direction::Front, Direction::Back, Direction::Left, Direction::Right };
                    shuffle(dirs.begin(), dirs.end(), rng);
            
                    for (Direction dir : dirs)
                    {
                        pair<int, int> next = pos;
                        switch (dir)
                        {
                            case Direction::Front: next.second -= 1; break;
                            case Direction::Back: next.second += 1; break;
                            case Direction::Left: next.first -= 1; break;
                            case Direction::Right: next.first += 1; break;
                        }
                        if (occupied.find(next) == occupied.end())
                        {
                            if (self(self, next, remaining - 1)) return true;
                        }
                    }
            
                    path.pop_back();
                    occupied.erase(pos);
                    return false;
                };
            
                generate_path(generate_path, {0, 0}, NUM_ROOMS - 1);
                int actual_rooms = static_cast<int>(path.size());
            
                // doors
                vector<Direction> doors(actual_rooms);
                for (int i = 1; i < actual_rooms; i++)
                {
                    auto prev = path[i - 1];
                    auto curr = path[i];
                    int dx = curr.first - prev.first;
                    int dz = curr.second - prev.second;
                    if (dx == 1) doors[i - 1] = Direction::Right;
                    else if (dx == -1) doors[i - 1] = Direction::Left;
                    else if (dz == 1) doors[i - 1] = Direction::Back;
                    else if (dz == -1) doors[i - 1] = Direction::Front;
                }
            
                doors[actual_rooms - 1] = static_cast<Direction>(rand_int(4));
            
                // create rooms
                for (int i = 0; i < actual_rooms; i++)
                {
                    Vector3 offset = Vector3(path[i].first * ROOM_WIDTH, 0, path[i].second * ROOM_DEPTH);
                    Direction skip_dir = (i == 0) ? Direction::Max : Direction::Max;
                    if (i > 0)
                    {
                        switch (doors[i - 1])
                        {
                            case Direction::Front: skip_dir = Direction::Back; break;
                            case Direction::Back: skip_dir = Direction::Front; break;
                            case Direction::Left: skip_dir = Direction::Right; break;
                            case Direction::Right: skip_dir = Direction::Left; break;
                        }
                    }
                    create_room(doors[i], skip_dir, offset, i);
                }
            }

            void tick()
            {
                // footsteps
                {
                    AudioSource* audio_source_tiles = default_camera->GetChildByName("audio_footsteps_tiles")->GetComponent<AudioSource>();
                    AudioSource* audio_source_water = default_camera->GetChildByName("audio_footsteps_water")->GetComponent<AudioSource>();
                    Camera* camera                  = default_camera->GetChildByIndex(0)->GetComponent<Camera>();
                    bool is_in_pool                 = default_camera->GetPosition().y < 1.6f;
                    AudioSource* active_source      = is_in_pool ? audio_source_water : audio_source_tiles;
                    AudioSource* inactive_source    = is_in_pool ? audio_source_tiles : audio_source_water;
  
                    if (camera->IsWalking() && !active_source->IsPlaying())
                    {
                        active_source->PlayClip();
                        inactive_source->StopClip();
                    }
                    else if (!camera->IsWalking())
                    {
                        audio_source_tiles->StopClip();
                        audio_source_water->StopClip();
                    }
                }
            }
        }

        namespace basic
        {
            void create()
            {
                entities::camera();
                entities::floor();
                entities::sun(true);
                entities::material_ball(Vector3::Zero);
            }
        }
    }

    void Game::Shutdown()
    {
        default_floor                          = nullptr;
        default_camera                         = nullptr;
        default_environment                    = nullptr;
        default_light_directional              = nullptr;
        default_terrain                        = nullptr;
        default_car                            = nullptr;
        default_metal_cube                     = nullptr;
        worlds::showroom::texture_brand_logo   = nullptr;
        worlds::showroom::texture_paint_normal = nullptr;
        meshes.clear();
    }

    void Game::Tick()
    {
        car::tick();

        if (loaded_world == DefaultWorld::LiminalSpace)
        {
           worlds::liminal_space::tick();
        }
        else if (loaded_world == DefaultWorld::Showroom)
        {
            worlds::showroom::tick();
        }
        else if (loaded_world == DefaultWorld::Forest)
        {
            worlds::forest::tick();
        }
    }

    void Game::EditorTick()
    {

    }

    void Game::Load(DefaultWorld default_world)
    {
        Game::Shutdown();  // stop game
        World::Shutdown(); // clear current world

        // load whatever needs to be loaded
        ThreadPool::AddTask([default_world]()
        {
            ProgressTracker::SetGlobalLoadingState(true);

            set_base_renderer_options();

            switch (default_world)
            {
                case DefaultWorld::Forest:       worlds::forest::create();        break;
                case DefaultWorld::Minecraft:    worlds::create_minecraft();      break;
                case DefaultWorld::Sponza:       worlds::create_sponza_4k();      break;
                case DefaultWorld::Subway:       worlds::create_subway_gi_test(); break;
                case DefaultWorld::Showroom:     worlds::showroom::create();      break;
                case DefaultWorld::LiminalSpace: worlds::liminal_space::create(); break;
                case DefaultWorld::Basic:        worlds::basic::create();         break;
                default: SP_ASSERT_MSG(false, "Unhandled default world");         break;
            }

            ProgressTracker::SetGlobalLoadingState(false);
        });

        loaded_world = default_world;
    }
}
