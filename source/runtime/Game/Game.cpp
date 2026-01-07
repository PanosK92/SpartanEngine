/*
Copyright(c) 2015-2026 Panos Karabelas

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
#include "../Physics/Car.h"
//==========================================

//= NAMESPACES ===============
using namespace std;
using namespace spartan::math;
//============================

namespace spartan
{
    //= FORWARD DECLARATIONS (world functions) =============
    namespace worlds
    {
        namespace showroom      { void create(); void tick(); }
        namespace forest        { void create(); void tick(); }
        namespace liminal_space { void create(); void tick(); }
        namespace sponza        { void create(); }
        namespace subway        { void create(); }
        namespace minecraft     { void create(); }
        namespace basic         { void create(); }
        namespace drivable_car  { void create(); void tick(); }
    }
    //======================================================

    namespace
    {
        //= STATE ===============================
        DefaultWorld loaded_world = DefaultWorld::Max;
        //=======================================

        //= SHARED ENTITIES =====================
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
        //=======================================

        //= WORLD DISPATCH TABLES ===================================================================================================
        using create_fn = void(*)();
        using tick_fn   = void(*)();

        // indexed by DefaultWorld enum - add new worlds here
        constexpr create_fn world_create[] =
        {
            worlds::showroom::create,      // Showroom
            worlds::forest::create,        // Forest
            worlds::liminal_space::create, // LiminalSpace
            worlds::sponza::create,        // Sponza
            worlds::subway::create,        // Subway
            worlds::minecraft::create,     // Minecraft
            worlds::basic::create,         // Basic
            worlds::drivable_car::create,  // DrivableCar
        };

        constexpr tick_fn world_tick[] =
        {
            worlds::showroom::tick,        // Showroom
            worlds::forest::tick,          // Forest
            worlds::liminal_space::tick,   // LiminalSpace
            nullptr,                       // Sponza (no tick)
            nullptr,                       // Subway (no tick)
            nullptr,                       // Minecraft (no tick)
            nullptr,                       // Basic (no tick)
            worlds::drivable_car::tick,    // CarTestWip
        };

        static_assert(size(world_create) == static_cast<size_t>(DefaultWorld::Max), "world_create out of sync with DefaultWorld enum");
        static_assert(size(world_tick)   == static_cast<size_t>(DefaultWorld::Max), "world_tick out of sync with DefaultWorld enum");
        //===========================================================================================================================

        //= ENTITY BUILDING BLOCKS ===================================================================
        namespace entities
        {
            // background music
            void music(const char* soundtrack_file_path = "project\\music\\jake_chudnow_shona.wav")
            {
                SP_ASSERT(soundtrack_file_path);

                auto entity = World::CreateEntity();
                entity->SetObjectName("music");

                AudioSource* audio_source = entity->AddComponent<AudioSource>();
                audio_source->SetAudioClip(soundtrack_file_path);
                audio_source->SetLoop(true);
            }

            // directional light (sun)
            void sun(const LightPreset preset, const bool enabled)
            {
                default_light_directional = World::CreateEntity();
                default_light_directional->SetObjectName("light_directional");
                Light* light = default_light_directional->AddComponent<Light>();
                light->SetLightType(LightType::Directional);

                if (enabled)
                {
                    light->SetPreset(preset);
                }
                else
                {
                    light->SetIntensity(0.0f);
                }

                light->SetFlag(LightFlags::Shadows, enabled);
                light->SetFlag(LightFlags::ShadowsScreenSpace, enabled);
                light->SetFlag(LightFlags::DayNightCycle, false);
            }

            // player camera with physics controller
            void camera(const bool is_night, const Vector3& camera_position = Vector3(0.0f, 2.0f, -10.0f), const Vector3& camera_rotation = Vector3(0.0f, 0.0f, 0.0f))
            {
                // root entity with physics body
                default_camera = World::CreateEntity();
                default_camera->SetObjectName("physics_body_camera");
                default_camera->SetPosition(camera_position);

                // physics controller for movement
                Physics* physics_body = default_camera->AddComponent<Physics>();
                physics_body->SetFriction(1.0f);
                physics_body->SetFrictionRolling(0.8f);
                physics_body->SetRestitution(0.1f);
                physics_body->SetBodyType(BodyType::Controller);

                // camera component as child
                Entity* camera = World::CreateEntity();
                camera->SetObjectName("component_camera");
                Camera* camera_comp = camera->AddComponent<Camera>();
                camera->SetParent(default_camera);
                camera->SetPositionLocal(physics_body->GetControllerTopLocal());
                camera->SetRotation(Quaternion::FromEulerAngles(camera_rotation));

                // exposure settings based on lighting conditions
                if (is_night)
                {
                    camera_comp->SetAperture(2.0f);
                    camera_comp->SetShutterSpeed(1.0f / 30.0f);
                    camera_comp->SetIso(800.0f);
                }
                else
                {
                    camera_comp->SetAperture(11.0f);
                    camera_comp->SetShutterSpeed(1.0f / 125.0f);
                    camera_comp->SetIso(100.0f);
                }
            }

            // ground plane with physics
            void floor()
            {
                default_floor = World::CreateEntity();
                default_floor->SetObjectName("floor");
                default_floor->SetPosition(Vector3(0.0f, 0.1f, 0.0f));
                default_floor->SetScale(Vector3(1000.0f, 1.0f, 1000.0f));

                Renderable* renderable = default_floor->AddComponent<Renderable>();
                renderable->SetMesh(MeshType::Quad);
                renderable->SetDefaultMaterial();

                Physics* physics_body = default_floor->AddComponent<Physics>();
                physics_body->SetBodyType(BodyType::Plane);
            }

            // metal crate with pbr material
            void metal_cube(const Vector3& position)
            {
                default_metal_cube = World::CreateEntity();
                default_metal_cube->SetObjectName("metal_cube");
                default_metal_cube->SetPosition(position);

                // pbr material
                shared_ptr<Material> material = make_shared<Material>();
                material->SetTexture(MaterialTextureType::Color,     "project\\materials\\crate_space\\albedo.png");
                material->SetTexture(MaterialTextureType::Normal,    "project\\materials\\crate_space\\normal.png");
                material->SetTexture(MaterialTextureType::Occlusion, "project\\materials\\crate_space\\ao.png");
                material->SetTexture(MaterialTextureType::Roughness, "project\\materials\\crate_space\\roughness.png");
                material->SetTexture(MaterialTextureType::Metalness, "project\\materials\\crate_space\\metallic.png");
                material->SetTexture(MaterialTextureType::Height,    "project\\materials\\crate_space\\height.png");
                material->SetProperty(MaterialProperty::Tessellation, 1.0f);
                material->SetResourceName("crate_space" + string(EXTENSION_MATERIAL));

                Renderable* renderable = default_metal_cube->AddComponent<Renderable>();
                renderable->SetMesh(MeshType::Cube);
                renderable->SetMaterial(material);

                Physics* physics_body = default_metal_cube->AddComponent<Physics>();
                physics_body->SetMass(Physics::mass_from_volume);
                physics_body->SetBodyType(BodyType::Box);
            }

            // flight helmet model
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

            // damaged helmet model
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

            // material test sphere
            void material_ball(const Vector3& position)
            {
                uint32_t flags = Mesh::GetDefaultFlags() | static_cast<uint32_t>(MeshFlags::ImportCombineMeshes);
                if (shared_ptr<Mesh> mesh = ResourceCache::Load<Mesh>("project\\models\\material_ball_in_3d-coat\\scene.gltf", flags))
                {
                    Entity* entity = mesh->GetRootEntity();
                    entity->SetObjectName("material_ball");
                    entity->SetPosition(Vector3(0.0f, 2.0f, 0.0f));
                    entity->SetRotation(Quaternion::Identity);

                    Physics* physics_body = entity->AddComponent<Physics>();
                    physics_body->SetStatic(false);
                    physics_body->SetBodyType(BodyType::Mesh);
                    physics_body->SetMass(100.0f);
                }
            }

            // tiled water surface with custom geometry
            Entity* water(const Vector3& position, float dimension, uint32_t density, Color color)
            {
                Entity* water = World::CreateEntity();
                water->SetObjectName("water");
                water->SetPosition(position);

                // water material
                shared_ptr<Material> material = make_shared<Material>();
                {
                    material->SetResourceName("water" + string(EXTENSION_MATERIAL));
                    material->SetColor(color);
                    material->SetTexture(MaterialTextureType::Normal,            "project\\materials\\water\\normal.jpeg");
                    material->SetProperty(MaterialProperty::Roughness,           0.0f);
                    material->SetProperty(MaterialProperty::Clearcoat,           0.0f);
                    material->SetProperty(MaterialProperty::Clearcoat_Roughness, 0.0f);
                    material->SetProperty(MaterialProperty::WorldSpaceUv,        1.0f);
                    material->SetProperty(MaterialProperty::TextureTilingX,      1.0f);
                    material->SetProperty(MaterialProperty::TextureTilingY,      1.0f);
                    material->SetProperty(MaterialProperty::IsWater,             1.0f);
                    material->SetProperty(MaterialProperty::Normal,              0.1f);
                    material->SetProperty(MaterialProperty::TextureTilingX,      0.1f);
                    material->SetProperty(MaterialProperty::TextureTilingY,      0.1f);
                }

                // generate tiled geometry
                {
                    const uint32_t grid_points_per_dimension = density;
                    vector<RHI_Vertex_PosTexNorTan> vertices;
                    vector<uint32_t> indices;
                    geometry_generation::generate_grid(&vertices, &indices, grid_points_per_dimension, dimension);

                    const uint32_t tile_count = std::max(1u, density / 6);
                    vector<vector<RHI_Vertex_PosTexNorTan>> tiled_vertices;
                    vector<vector<uint32_t>> tiled_indices;
                    vector<Vector3> tile_offsets;
                    spartan::geometry_processing::split_surface_into_tiles(vertices, indices, tile_count, tiled_vertices, tiled_indices, tile_offsets);

                    // create mesh tile entities
                    for (uint32_t tile_index = 0; tile_index < static_cast<uint32_t>(tiled_vertices.size()); tile_index++)
                    {
                        string name = "tile_" + to_string(tile_index);

                        shared_ptr<Mesh> mesh = meshes.emplace_back(make_shared<Mesh>());
                        mesh->SetObjectName(name);
                        mesh->SetFlag(static_cast<uint32_t>(MeshFlags::PostProcessOptimize), false);
                        mesh->AddGeometry(tiled_vertices[tile_index], tiled_indices[tile_index], false);
                        mesh->CreateGpuBuffers();

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
                    }
                }

                return water;
            }
        }
        //========================================================================================

        // reset renderer options to defaults
        void set_base_renderer_options()
        {
            Renderer::SetOption(Renderer_Option::Dithering,           0.0f);
            Renderer::SetOption(Renderer_Option::ChromaticAberration, 0.0f);
            Renderer::SetOption(Renderer_Option::Grid,                0.0f);
            Renderer::SetOption(Renderer_Option::Vhs,                 0.0f);
        }
    }

    //= CAR ==================================================================================
    namespace car
    {
        void create(const Vector3& position, const bool physics, shared_ptr<RHI_Texture> texture_paint_normal)
        {
            // load model at max quality
            uint32_t mesh_flags  = Mesh::GetDefaultFlags();
            mesh_flags          &= ~static_cast<uint32_t>(MeshFlags::PostProcessOptimize);
            mesh_flags          &= ~static_cast<uint32_t>(MeshFlags::PostProcessGenerateLods);
            if (shared_ptr<Mesh> mesh_car = ResourceCache::Load<Mesh>("project\\models\\ferrari_laferrari\\scene.gltf", mesh_flags))
            {
                default_car = mesh_car->GetRootEntity();
                default_car->SetObjectName("ferrari_laferrari");
                default_car->SetScale(2.0f);
                default_car->SetPosition(Vector3(0.0f, 0.1f, 0.0f));

                // material tweaks
                {
                    // body main - red clearcoat paint
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

                    // tires - rubber
                    {
                        const char* tire_parts[] = {"Object_127", "Object_142", "Object_157", "Object_172"};
                        for (const char* part : tire_parts)
                        {
                            if (Material* material = default_car->GetDescendantByName(part)->GetComponent<Renderable>()->GetMaterial())
                            {
                                material->SetProperty(MaterialProperty::Roughness, 0.7f);
                            }
                        }
                    }

                    // rims - polished metal
                    if (Material* material = default_car->GetDescendantByName("Object_180")->GetComponent<Renderable>()->GetMaterial())
                    {
                        material->SetProperty(MaterialProperty::Metalness, 1.0f);
                        material->SetProperty(MaterialProperty::Roughness, 0.3f);
                    }
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

                    // windshield and engine glass
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

                    // engine block
                    if (Material* material = default_car->GetDescendantByName("Object_14")->GetComponent<Renderable>()->GetMaterial())
                    {
                        material->SetProperty(MaterialProperty::Roughness, 0.4f);
                        material->SetProperty(MaterialProperty::Metalness, 1.0f);
                    }

                    // brake discs - anisotropic metal
                    {
                        const char* brake_parts[] = {"Object_129", "Object_144", "Object_174", "Object_159"};
                        for (const char* part : brake_parts)
                        {
                            if (Material* material = default_car->GetDescendantByName(part)->GetComponent<Renderable>()->GetMaterial())
                            {
                                material->SetProperty(MaterialProperty::Metalness, 1.0f);
                                material->SetProperty(MaterialProperty::Anisotropic, 1.0f);
                                material->SetProperty(MaterialProperty::AnisotropicRotation, 0.2f);
                            }
                        }
                    }

                    // interior leather
                    if (Material* material = default_car->GetDescendantByName("Object_90")->GetComponent<Renderable>()->GetMaterial())
                    {
                        material->SetProperty(MaterialProperty::Roughness, 0.75f);
                    }
                }

                // physics for all car parts
                if (physics)
                {
                    vector<Entity*> car_parts;
                    default_car->GetDescendants(&car_parts);
                    for (Entity* car_part : car_parts)
                    {
                        if (car_part->GetComponent<Renderable>())
                        {
                            Physics* physics_body = car_part->AddComponent<Physics>();
                            physics_body->SetKinematic(true);
                            physics_body->SetBodyType(BodyType::Mesh);
                        }
                    }
                }
            }

            // audio sources
            {
                // engine start
                {
                    Entity* sound = World::CreateEntity();
                    sound->SetObjectName("sound_start");
                    sound->SetParent(default_car);

                    AudioSource* audio_source = sound->AddComponent<AudioSource>();
                    audio_source->SetAudioClip("project\\music\\car_start.wav");
                    audio_source->SetLoop(false);
                    audio_source->SetPlayOnStart(false);
                }

                // engine idle
                {
                    Entity* sound = World::CreateEntity();
                    sound->SetObjectName("sound_idle");
                    sound->SetParent(default_car);

                    AudioSource* audio_source = sound->AddComponent<AudioSource>();
                    audio_source->SetAudioClip("project\\music\\car_idle.wav");
                    audio_source->SetLoop(true);
                    audio_source->SetPlayOnStart(false);
                }

                // door open/close
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
            if (!default_car)
                return;

            // view presets
            enum class CarView { Dashboard, Hood, Chase };
            static CarView current_view = CarView::Dashboard;

            static const Vector3 car_view_positions[] =
            {
                Vector3(0.5f, 1.8f, -0.6f), // dashboard
                Vector3(0.0f, 2.0f, 1.0f),  // hood
                Vector3(0.0f, 3.0f, -10.0f) // chase
            };

            // cached references
            bool inside_the_car             = default_camera->GetChildrenCount() == 0;
            Entity* sound_door_entity       = default_car->GetChildByName("sound_door");
            Entity* sound_start_entity      = default_car->GetChildByName("sound_start");
            Entity* sound_idle_entity       = default_car->GetChildByName("sound_idle");
            AudioSource* audio_source_door  = sound_door_entity  ? sound_door_entity->GetComponent<AudioSource>()  : nullptr;
            AudioSource* audio_source_start = sound_start_entity ? sound_start_entity->GetComponent<AudioSource>() : nullptr;
            AudioSource* audio_source_idle  = sound_idle_entity  ? sound_idle_entity->GetComponent<AudioSource>()  : nullptr;
            if (!audio_source_door || !audio_source_start || !audio_source_idle)
                return;

            // enter/exit car
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
                    default_camera->SetPosition(default_car->GetPosition() + default_car->GetLeft() * 3.0f + Vector3::Up * 2.0f);
                    audio_source_idle->StopClip();
                    inside_the_car = false;
                }

                camera->GetComponent<Camera>()->SetFlag(CameraFlags::CanBeControlled, !inside_the_car);
                audio_source_door->PlayClip();

                if (default_car_window)
                {
                    default_car_window->SetActive(!inside_the_car);
                }
            }

            // cycle camera view
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
            Renderer::DrawString("WASD: Move Camera/Car | 'E': Enter/Exit Car | 'V': Change Car View", Vector2(0.005f, 0.98f));
        }
    }
    //========================================================================================

    //= WORLDS ===============================================================================
    namespace worlds
    {
        //= SPONZA ===========================================================================
        namespace sponza
        {
            void create()
            {
                // base setup
                entities::camera(false, Vector3(19.2692f, 2.65f, 0.1677f), Vector3(-18.0f, -90.0f, 0.0f));
                entities::sun(LightPreset::dusk, true);
                entities::music("project\\music\\jake_chudnow_olive.wav");
                entities::floor();
                Renderer::SetWind(Vector3(0.0f, 0.2f, 1.0f) * 0.1f);

                const Vector3 position = Vector3(0.0f, 1.5f, 0.0f);
                const float scale      = 2.0f;

                // main building
                uint32_t mesh_flags = Mesh::GetDefaultFlags();
                if (shared_ptr<Mesh> mesh = ResourceCache::Load<Mesh>("project\\models\\sponza\\main\\NewSponza_Main_Blender_glTF.gltf", mesh_flags))
                {
                    Entity* entity = mesh->GetRootEntity();
                    entity->SetObjectName("sponza");
                    entity->SetPosition(position);
                    entity->SetScale(scale);

                    // disable bad decals
                    entity->GetDescendantByName("decals_1st_floor")->SetActive(false);
                    entity->GetDescendantByName("decals_2nd_floor")->SetActive(false);
                    entity->GetDescendantByName("decals_3rd_floor")->SetActive(false);

                    // physics for all meshes
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

                // curtains
                if (shared_ptr<Mesh> mesh = ResourceCache::Load<Mesh>("project\\models\\sponza\\curtains\\NewSponza_Curtains_glTF.gltf"))
                {
                    Entity* entity = mesh->GetRootEntity();
                    entity->SetObjectName("sponza_curtains");
                    entity->SetPosition(position);
                    entity->SetScale(scale);

                    // fabric wind animation
                    const char* curtain_parts[] = {"curtain_03_2", "curtain_03_3", "curtain_hanging_06_3"};
                    for (const char* part : curtain_parts)
                    {
                        if (Material* material = entity->GetDescendantByName(part)->GetComponent<Renderable>()->GetMaterial())
                        {
                            material->SetProperty(MaterialProperty::CullMode,      static_cast<float>(RHI_CullMode::None));
                            material->SetProperty(MaterialProperty::WindAnimation, 1.0f);
                        }
                    }
                }

                // ivy
                if (shared_ptr<Mesh> mesh = ResourceCache::Load<Mesh>("project\\models\\sponza\\ivy\\NewSponza_IvyGrowth_glTF.gltf"))
                {
                    Entity* entity = mesh->GetRootEntity();
                    entity->SetObjectName("sponza_ivy");
                    entity->SetPosition(position);
                    entity->SetScale(scale);

                    // leaf material
                    if (Material* material = entity->GetDescendantByName("IvySim_Leaves")->GetComponent<Renderable>()->GetMaterial())
                    {
                        material->SetProperty(MaterialProperty::CullMode,                  static_cast<float>(RHI_CullMode::None));
                        material->SetProperty(MaterialProperty::WindAnimation,             1.0f);
                        material->SetProperty(MaterialProperty::SubsurfaceScattering,      1.0f);
                        material->SetProperty(MaterialProperty::ColorVariationFromInstance, 1.0f);
                    }

                    // stem material
                    if (Material* material = entity->GetDescendantByName("IvySim_Stems")->GetComponent<Renderable>()->GetMaterial())
                    {
                        material->SetProperty(MaterialProperty::SubsurfaceScattering, 1.0f);
                    }
                }
            }
        }
        //====================================================================================

        //= MINECRAFT ========================================================================
        namespace minecraft
        {
            void create()
            {
                entities::camera(false, Vector3(-51.7576f, 21.4551f, -85.3699f), Vector3(11.3991f, 30.6026f, 0.0f));
                entities::sun(LightPreset::dusk, true);
                entities::music();

                // single mesh - disable optimization to preserve voxel look
                uint32_t mesh_flags  = Mesh::GetDefaultFlags();
                mesh_flags          &= ~static_cast<uint32_t>(MeshFlags::PostProcessOptimize);
                mesh_flags          &= ~static_cast<uint32_t>(MeshFlags::PostProcessGenerateLods);
                if (shared_ptr<Mesh> mesh = ResourceCache::Load<Mesh>("project\\models\\vokselia_spawn\\vokselia_spawn.obj", mesh_flags))
                {
                    Entity* entity = mesh->GetRootEntity();
                    entity->SetObjectName("minecraft");
                    entity->SetScale(100.0f);

                    // physics for all meshes
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
        }
        //====================================================================================

        //= SUBWAY ===========================================================================
        namespace subway
        {
            void create()
            {
                entities::camera(true);
                entities::floor();

                if (shared_ptr<Mesh> mesh = ResourceCache::Load<Mesh>("project\\models\\free-subway-station-r46-subway\\Metro.fbx"))
                {
                    Entity* entity = mesh->GetRootEntity();
                    entity->SetObjectName("subway");
                    entity->SetScale(Vector3(0.015f));

                    // physics for all meshes
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
        }
        //====================================================================================

        //= FOREST ===========================================================================
        namespace forest
        {
            void create()
            {
                // config
                const float render_distance_trees            = 2'000.0f;
                const float render_distance_foliage          = 500.0f;
                const float shadow_distance                  = 150.0f;
                const float per_triangle_density_grass_blade = 15.0f;
                const float per_triangle_density_flower      = 0.2f;
                const float per_triangle_density_tree        = 0.004f;
                const float per_triangle_density_rock        = 0.001f;

                // lighting
                entities::sun(LightPreset::dusk, true);
                Light* sun = default_light_directional->GetComponent<Light>();
                sun->SetFlag(LightFlags::Volumetric, true);

                entities::camera(false, Vector3(-1476.0f, 17.9f, 1490.0f), Vector3(-3.6f, 90.0f, 0.0f));
                Renderer::SetOption(Renderer_Option::Grid, 0.0f);

                // terrain root
                default_terrain = World::CreateEntity();
                default_terrain->SetObjectName("terrain");

                // audio
                {
                    Entity* entity = World::CreateEntity();
                    entity->SetObjectName("audio");

                    // footsteps
                    {
                        Entity* sound = World::CreateEntity();
                        sound->SetObjectName("footsteps");
                        sound->SetParent(entity);
                        AudioSource* audio_source = sound->AddComponent<AudioSource>();
                        audio_source->SetAudioClip("project\\music\\footsteps_grass.wav");
                        audio_source->SetPlayOnStart(false);
                    }

                    // forest ambience
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

                // terrain component
                Terrain* terrain = default_terrain->AddComponent<Terrain>();
                {
                    // terrain material
                    {
                        shared_ptr<Material> material = terrain->GetMaterial();
                        material->SetResourceName("terrain" + string(EXTENSION_MATERIAL));
                        material->SetProperty(MaterialProperty::IsTerrain, 1.0f);
                        material->SetProperty(MaterialProperty::TextureTilingX, 1000.0f);
                        material->SetProperty(MaterialProperty::TextureTilingY, 1000.0f);

                        // grass layer
                        material->SetTexture(MaterialTextureType::Color,     "project\\materials\\whispy_grass_meadow\\albedo.png",    0);
                        material->SetTexture(MaterialTextureType::Normal,    "project\\materials\\whispy_grass_meadow\\normal.png",    0);
                        material->SetTexture(MaterialTextureType::Roughness, "project\\materials\\whispy_grass_meadow\\roughness.png", 0);
                        material->SetTexture(MaterialTextureType::Occlusion, "project\\materials\\whispy_grass_meadow\\occlusion.png", 0);

                        // rock layer
                        material->SetTexture(MaterialTextureType::Color,     "project\\materials\\rock\\albedo.png",    1);
                        material->SetTexture(MaterialTextureType::Normal,    "project\\materials\\rock\\normal.png",    1);
                        material->SetTexture(MaterialTextureType::Roughness, "project\\materials\\rock\\roughness.png", 1);
                        material->SetTexture(MaterialTextureType::Occlusion, "project\\materials\\rock\\occlusion.png", 1);
                        material->SetTexture(MaterialTextureType::Height,    "project\\materials\\rock\\height.png",    1);

                        // sand layer
                        material->SetTexture(MaterialTextureType::Color,     "project\\materials\\sand\\albedo.png",    2);
                        material->SetTexture(MaterialTextureType::Normal,    "project\\materials\\sand\\normal.png",    2);
                        material->SetTexture(MaterialTextureType::Roughness, "project\\materials\\sand\\roughness.png", 2);
                        material->SetTexture(MaterialTextureType::Occlusion, "project\\materials\\sand\\occlusion.png", 2);
                        material->SetProperty(MaterialProperty::Tessellation, 0.0f);
                    }

                    // height map generation
                    shared_ptr<RHI_Texture> height_map = ResourceCache::Load<RHI_Texture>("project\\height_maps\\height_map.png");
                    terrain->SetHeightMapSeed(height_map.get());
                    terrain->Generate();

                    // terrain physics
                    for (Entity* terrain_tile : terrain->GetEntity()->GetChildren())
                    {
                        Physics* physics_body = terrain_tile->AddComponent<Physics>();
                        physics_body->SetBodyType(BodyType::Mesh);
                    }
                }

                // water
                const float dimension          = 8000;
                const uint32_t density         = 64;
                const Color forest_water_color = Color(0.0f / 255.0f, 140.0f / 255.0f, 100.0f / 255.0f, 50.0f / 255.0f);
                entities::water(Vector3::Zero, dimension, density, forest_water_color);

                // props: trees, rocks, grass
                {
                    // load meshes
                    uint32_t flags             = Mesh::GetDefaultFlags() | static_cast<uint32_t>(MeshFlags::ImportCombineMeshes);
                    shared_ptr<Mesh> mesh_tree = ResourceCache::Load<Mesh>("project\\models\\tree\\tree.fbx", flags);
                    shared_ptr<Mesh> mesh_rock = ResourceCache::Load<Mesh>("project\\models\\rock_2\\model.obj");

                    // procedural grass mesh with lods
                    shared_ptr<Mesh> mesh_grass_blade = meshes.emplace_back(make_shared<Mesh>());
                    {
                        mesh_grass_blade->SetFlag(static_cast<uint32_t>(MeshFlags::PostProcessOptimize), false);
                        uint32_t sub_mesh_index = 0;

                        // lod 0: 3 segments
                        {
                            vector<RHI_Vertex_PosTexNorTan> vertices;
                            vector<uint32_t> indices;
                            geometry_generation::generate_foliage_grass_blade(&vertices, &indices, 3);
                            mesh_grass_blade->AddGeometry(vertices, indices, false, &sub_mesh_index);
                        }

                        // lod 1: 2 segments
                        {
                            vector<RHI_Vertex_PosTexNorTan> vertices;
                            vector<uint32_t> indices;
                            geometry_generation::generate_foliage_grass_blade(&vertices, &indices, 2);
                            mesh_grass_blade->AddLod(vertices, indices, sub_mesh_index);
                        }

                        // lod 2: 1 segment
                        {
                            vector<RHI_Vertex_PosTexNorTan> vertices;
                            vector<uint32_t> indices;
                            geometry_generation::generate_foliage_grass_blade(&vertices, &indices, 1);
                            mesh_grass_blade->AddLod(vertices, indices, sub_mesh_index);
                        }

                        mesh_grass_blade->SetResourceFilePath(string(ResourceCache::GetProjectDirectory()) + "standard_grass" + EXTENSION_MESH);
                        mesh_grass_blade->CreateGpuBuffers();
                    }

                    // procedural flower mesh with lods
                    shared_ptr<Mesh> mesh_flower = meshes.emplace_back(make_shared<Mesh>());
                    {
                        mesh_flower->SetFlag(static_cast<uint32_t>(MeshFlags::PostProcessOptimize), false);
                        uint32_t sub_mesh_index = 0;

                        // lod 0
                        {
                            vector<RHI_Vertex_PosTexNorTan> vertices;
                            vector<uint32_t> indices;
                            geometry_generation::generate_foliage_flower(&vertices, &indices, 3, 6, 3);
                            mesh_flower->AddGeometry(vertices, indices, false, &sub_mesh_index);
                        }

                        // lod 1
                        {
                            vector<RHI_Vertex_PosTexNorTan> vertices;
                            vector<uint32_t> indices;
                            geometry_generation::generate_foliage_flower(&vertices, &indices, 2, 4, 2);
                            mesh_flower->AddLod(vertices, indices, sub_mesh_index);
                        }

                        // lod 2
                        {
                            vector<RHI_Vertex_PosTexNorTan> vertices;
                            vector<uint32_t> indices;
                            geometry_generation::generate_foliage_flower(&vertices, &indices, 1, 1, 1);
                            mesh_flower->AddLod(vertices, indices, sub_mesh_index);
                        }

                        mesh_flower->SetResourceFilePath(string(ResourceCache::GetProjectDirectory()) + "standard_flower" + EXTENSION_MESH);
                        mesh_flower->CreateGpuBuffers();
                    }

                    // materials
                    shared_ptr<Material> material_leaf;
                    shared_ptr<Material> material_body;
                    shared_ptr<Material> material_rock;
                    shared_ptr<Material> material_grass_blade;
                    shared_ptr<Material> material_flower;
                    {
                        // tree leaves
                        material_leaf = make_shared<Material>();
                        material_leaf->SetTexture(MaterialTextureType::Color, "project\\models\\tree\\Twig_Base_Material_2.png");
                        material_leaf->SetTexture(MaterialTextureType::Normal, "project\\models\\tree\\Twig_Normal.png");
                        material_leaf->SetTexture(MaterialTextureType::AlphaMask, "project\\models\\tree\\Twig_Opacity_Map.jpg");
                        material_leaf->SetProperty(MaterialProperty::WindAnimation, 1.0f);
                        material_leaf->SetProperty(MaterialProperty::ColorVariationFromInstance, 1.0f);
                        material_leaf->SetProperty(MaterialProperty::SubsurfaceScattering, 1.0f);
                        material_leaf->SetResourceName("tree_leaf" + string(EXTENSION_MATERIAL));

                        // tree bark
                        material_body = make_shared<Material>();
                        material_body->SetTexture(MaterialTextureType::Color, "project\\models\\tree\\tree_bark_diffuse.png");
                        material_body->SetTexture(MaterialTextureType::Normal, "project\\models\\tree\\tree_bark_normal.png");
                        material_body->SetTexture(MaterialTextureType::Roughness, "project\\models\\tree\\tree_bark_roughness.png");
                        material_body->SetResourceName("tree_body" + string(EXTENSION_MATERIAL));

                        // rocks
                        material_rock = make_shared<Material>();
                        material_rock->SetTexture(MaterialTextureType::Color, "project\\models\\rock_2\\albedo.png");
                        material_rock->SetTexture(MaterialTextureType::Normal, "project\\models\\rock_2\\normal.png");
                        material_rock->SetTexture(MaterialTextureType::Roughness, "project\\models\\rock_2\\roughness.png");
                        material_rock->SetTexture(MaterialTextureType::Occlusion, "project\\models\\rock_2\\occlusion.png");
                        material_rock->SetResourceName("rock" + string(EXTENSION_MATERIAL));

                        // grass blades
                        material_grass_blade = make_shared<Material>();
                        material_grass_blade->SetProperty(MaterialProperty::IsGrassBlade, 1.0f);
                        material_grass_blade->SetProperty(MaterialProperty::Roughness, 1.0f);
                        material_grass_blade->SetProperty(MaterialProperty::Clearcoat, 1.0f);
                        material_grass_blade->SetProperty(MaterialProperty::Clearcoat_Roughness, 0.2f);
                        material_grass_blade->SetProperty(MaterialProperty::SubsurfaceScattering, 1.0f);
                        material_grass_blade->SetProperty(MaterialProperty::CullMode, static_cast<float>(RHI_CullMode::None));
                        material_grass_blade->SetColor(Color::standard_white);
                        material_grass_blade->SetResourceName("grass_blade" + string(EXTENSION_MATERIAL));

                        // flowers
                        material_flower = make_shared<Material>();
                        material_flower->SetProperty(MaterialProperty::IsFlower, 1.0f);
                        material_flower->SetProperty(MaterialProperty::Roughness, 1.0f);
                        material_flower->SetProperty(MaterialProperty::Clearcoat, 1.0f);
                        material_flower->SetProperty(MaterialProperty::Clearcoat_Roughness, 0.2f);
                        material_flower->SetProperty(MaterialProperty::SubsurfaceScattering, 0.0f);
                        material_flower->SetProperty(MaterialProperty::CullMode, static_cast<float>(RHI_CullMode::None));
                        material_flower->SetColor(Color::standard_white);
                        material_flower->SetResourceName("flower" + string(EXTENSION_MATERIAL));
                    }

                    // place props on terrain tiles
                    vector<Entity*> children = terrain->GetEntity()->GetChildren();
                    auto place_props_on_tiles = [
                        &children,
                        &mesh_rock,
                        &mesh_tree,
                        &mesh_grass_blade,
                        &mesh_flower,
                        &terrain,
                        render_distance_trees,
                        render_distance_foliage,
                        shadow_distance,
                        per_triangle_density_grass_blade,
                        per_triangle_density_flower,
                        per_triangle_density_tree,
                        per_triangle_density_rock,
                        material_leaf,
                        material_body,
                        material_rock,
                        material_grass_blade,
                        material_flower
                    ](uint32_t start_index, uint32_t end_index)
                    {
                        for (uint32_t tile_index = start_index; tile_index < end_index; tile_index++)
                        {
                            Entity* terrain_tile = children[tile_index];

                            // trees
                            {
                                Entity* entity = mesh_tree->GetRootEntity()->Clone();
                                entity->SetObjectName("tree");
                                entity->SetParent(terrain_tile);

                                vector<Matrix> transforms;
                                terrain->FindTransforms(tile_index, TerrainProp::Tree, entity, per_triangle_density_tree, 0.04f, transforms);

                                if (Entity* trunk = entity->GetChildByIndex(0))
                                {
                                    Renderable* renderable = trunk->GetComponent<Renderable>();
                                    renderable->SetInstances(transforms);
                                    renderable->SetMaxRenderDistance(render_distance_trees);
                                    renderable->SetMaxShadowDistance(shadow_distance);
                                    renderable->SetMaterial(material_body);

                                    Physics* physics = trunk->AddComponent<Physics>();
                                    physics->SetBodyType(BodyType::Mesh);
                                }

                                if (Entity* leafs = entity->GetChildByIndex(1))
                                {
                                    Renderable* renderable = leafs->GetComponent<Renderable>();
                                    renderable->SetInstances(transforms);
                                    renderable->SetMaxRenderDistance(render_distance_trees);
                                    renderable->SetMaxShadowDistance(shadow_distance);
                                    renderable->SetMaterial(material_leaf);
                                }
                            }

                            // rocks
                            {
                                Entity* entity = mesh_rock->GetRootEntity()->Clone();
                                entity->SetObjectName("rock");
                                entity->SetParent(terrain_tile);

                                vector<Matrix> transforms;
                                terrain->FindTransforms(tile_index, TerrainProp::Rock, entity, per_triangle_density_rock, 1.0f, transforms);

                                if (Entity* rock_entity = entity->GetDescendantByName("untitled"))
                                {
                                    Renderable* renderable = rock_entity->GetComponent<Renderable>();
                                    renderable->SetInstances(transforms);
                                    renderable->SetMaxRenderDistance(render_distance_trees);
                                    renderable->SetMaxShadowDistance(shadow_distance);
                                    renderable->SetMaterial(material_rock);

                                    Physics* physics = rock_entity->AddComponent<Physics>();
                                    physics->SetBodyType(BodyType::Mesh);
                                }
                            }

                            // grass - density layers for lod
                            {
                                vector<Matrix> all_transforms;
                                terrain->FindTransforms(tile_index, TerrainProp::Grass, nullptr, per_triangle_density_grass_blade, 0.7f, all_transforms);

                                if (!all_transforms.empty())
                                {
                                    size_t total_count = all_transforms.size();
                                    size_t split_1     = static_cast<size_t>(total_count * 0.15f);
                                    size_t split_2     = static_cast<size_t>(total_count * 0.45f);

                                    // far layer (15%)
                                    {
                                        Entity* entity = World::CreateEntity();
                                        entity->SetObjectName("grass_layer_density_low");
                                        entity->SetParent(terrain_tile);

                                        vector<Matrix> far_transforms(all_transforms.begin(), all_transforms.begin() + split_1);

                                        Renderable* renderable = entity->AddComponent<Renderable>();
                                        renderable->SetMesh(mesh_grass_blade.get());
                                        renderable->SetFlag(RenderableFlags::CastsShadows, false);
                                        renderable->SetInstances(far_transforms);
                                        renderable->SetMaterial(material_grass_blade);
                                        renderable->SetMaxRenderDistance(render_distance_foliage);
                                    }

                                    // mid layer (30%)
                                    {
                                        Entity* entity = World::CreateEntity();
                                        entity->SetObjectName("grass_layer_density_mid");
                                        entity->SetParent(terrain_tile);

                                        vector<Matrix> mid_transforms(all_transforms.begin() + split_1, all_transforms.begin() + split_2);

                                        Renderable* renderable = entity->AddComponent<Renderable>();
                                        renderable->SetMesh(mesh_grass_blade.get());
                                        renderable->SetFlag(RenderableFlags::CastsShadows, false);
                                        renderable->SetInstances(mid_transforms);
                                        renderable->SetMaterial(material_grass_blade);
                                        renderable->SetMaxRenderDistance(render_distance_foliage * 0.6f);
                                    }

                                    // near layer (55%)
                                    {
                                        Entity* entity = World::CreateEntity();
                                        entity->SetObjectName("grass_layer_density_high");
                                        entity->SetParent(terrain_tile);

                                        vector<Matrix> near_transforms(all_transforms.begin() + split_2, all_transforms.end());

                                        Renderable* renderable = entity->AddComponent<Renderable>();
                                        renderable->SetMesh(mesh_grass_blade.get());
                                        renderable->SetFlag(RenderableFlags::CastsShadows, false);
                                        renderable->SetInstances(near_transforms);
                                        renderable->SetMaterial(material_grass_blade);
                                        renderable->SetMaxRenderDistance(render_distance_foliage * 0.3f);
                                    }
                                }
                            }

                            // flowers
                            {
                                Entity* entity = World::CreateEntity();
                                entity->SetObjectName("flower");
                                entity->SetParent(terrain_tile);

                                vector<Matrix> transforms;
                                terrain->FindTransforms(tile_index, TerrainProp::Flower, entity, per_triangle_density_flower, 1.0f, transforms);

                                Renderable* renderable = entity->AddComponent<Renderable>();
                                renderable->SetMesh(mesh_flower.get());
                                renderable->SetFlag(RenderableFlags::CastsShadows, false);
                                renderable->SetInstances(transforms);
                                renderable->SetMaterial(material_flower);
                                renderable->SetMaxRenderDistance(render_distance_foliage);
                            }
                        }
                    };

                    ThreadPool::ParallelLoop(place_props_on_tiles, static_cast<uint32_t>(children.size()));
                }
            }

            void tick()
            {
                Camera*  camera  = World::GetCamera();
                Terrain* terrain = default_terrain->GetComponent<Terrain>();
                if (!camera || !terrain)
                    return;

                bool is_below_water_level = camera->GetEntity()->GetPosition().y < 0.0f;

                // underwater sound
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
        //====================================================================================

        //= SHOWROOM =========================================================================
        namespace showroom
        {
            shared_ptr<RHI_Texture> texture_brand_logo;
            shared_ptr<RHI_Texture> texture_paint_normal;
            Entity* turn_table = nullptr;

            void create()
            {
                entities::music("project\\music\\gran_turismo.wav");

                // textures
                texture_brand_logo   = make_shared<RHI_Texture>("project\\models\\ferrari_laferrari\\logo.png");
                texture_paint_normal = make_shared<RHI_Texture>("project\\models\\ferrari_laferrari\\paint_normal.png");

                car::create(Vector3(0.0f, 0.08f, 0.0f), false, texture_paint_normal);

                // camera looking at car
                {
                    Vector3 camera_position = Vector3(5.0f, 1.5f, -10.0f);
                    entities::camera(true, camera_position);
                    Vector3 direction = (default_car->GetPosition() - camera_position).Normalized();
                    default_camera->GetChildByIndex(0)->SetRotationLocal(Quaternion::FromLookRotation(direction, Vector3::Up));
                    default_camera->GetChildByIndex(0)->GetComponent<Camera>()->SetFlag(CameraFlags::Flashlight, true);
                }

                // environment: tube lights and floor
                {
                    uint32_t mesh_flags  = Mesh::GetDefaultFlags();
                    mesh_flags          &= static_cast<uint32_t>(MeshFlags::ImportLights);
                    mesh_flags          &= static_cast<uint32_t>(MeshFlags::ImportCombineMeshes);
                    mesh_flags          &= ~static_cast<uint32_t>(MeshFlags::PostProcessOptimize);
                    mesh_flags          &= ~static_cast<uint32_t>(MeshFlags::PostProcessGenerateLods);
                    if (shared_ptr<Mesh> mesh = ResourceCache::Load<Mesh>("project\\models\\ferrari_laferrari\\SpartanLaFerrariV2\\LaFerrariV2.gltf", mesh_flags))
                    {
                        Entity* floor_tube_lights = mesh->GetRootEntity();
                        floor_tube_lights->SetObjectName("tube_lights_and_floor");
                        floor_tube_lights->SetScale(2.0f);

                        // tube light setup helper
                        auto setup_tube_light = [floor_tube_lights](const char* descendant_name, Color color)
                        {
                            if (Entity* entity_tube_light = floor_tube_lights->GetDescendantByName(descendant_name))
                            {
                                entity_tube_light->GetComponent<Renderable>()->SetFlag(RenderableFlags::CastsShadows, false);
                                if (Material* material = entity_tube_light->GetComponent<Renderable>()->GetMaterial())
                                {
                                    material->SetColor(color);
                                    material->SetProperty(MaterialProperty::EmissiveFromAlbedo, 1.0f);

                                    // point light
                                    Entity* entity = World::CreateEntity();
                                    entity->SetObjectName("light_point");
                                    entity->SetParent(entity_tube_light);

                                    Light* light = entity->AddComponent<Light>();
                                    light->SetLightType(LightType::Point);
                                    light->SetColor(color);
                                    light->SetRange(40.0f);
                                    light->SetIntensity(7000.0f);
                                    light->SetFlag(LightFlags::Shadows,            true);
                                    light->SetFlag(LightFlags::ShadowsScreenSpace, false);
                                    light->SetFlag(LightFlags::Volumetric,         false);
                                }
                            }
                        };

                        setup_tube_light("SM_TubeLight.007_1", Color(1.0f, 0.4f, 0.4f, 1.0f)); // red
                        setup_tube_light("SM_TubeLight.004_1", Color(0.4f, 0.8f, 1.0f, 1.0f)); // cyan
                        setup_tube_light("SM_TubeLight.006_1", Color(1.0f, 1.0f, 0.9f, 1.0f)); // warm white

                        // physics for all
                        vector<Entity*> descendants;
                        floor_tube_lights->GetDescendants(&descendants);
                        for (Entity* descendant : descendants)
                        {
                            if (descendant->GetComponent<Renderable>())
                            {
                                descendant->AddComponent<Physics>()->SetBodyType(BodyType::Mesh);
                            }
                        }

                        // floor setup
                        if (Entity* entity_floor = floor_tube_lights->GetDescendantByName("Floor"))
                        {
                            const float scale = 100.0f;
                            entity_floor->SetScale(scale);
                            if (Material* material = entity_floor->GetComponent<Renderable>()->GetMaterial())
                            {
                                material->SetProperty(MaterialProperty::TextureTilingX, scale);
                                material->SetProperty(MaterialProperty::TextureTilingY, scale);
                                material->SetProperty(MaterialProperty::Metalness, 0.0f);
                            }
                            entity_floor->GetComponent<Physics>()->SetBodyType(BodyType::Plane);
                        }

                        // turntable
                        if (turn_table = floor_tube_lights->GetDescendantByName("TurnTable"))
                        {
                            default_car->SetParent(turn_table);
                            default_car->SetScaleLocal(1.0f);
                            turn_table->SetPositionLocal(0.0f);
                            if (Material* material = turn_table->GetComponent<Renderable>()->GetMaterial())
                            {
                                material->SetColor(Color::standard_black);
                            }
                            turn_table->GetComponent<Physics>()->SetKinematic(true);
                        }
                    }
                }

                // renderer options
                Renderer::SetOption(Renderer_Option::PerformanceMetrics, 0.0f);
                Renderer::SetOption(Renderer_Option::Lights,             0.0f);
                Renderer::SetOption(Renderer_Option::Dithering,          0.0f);
            }

            void tick()
            {
                // rotate turntable
                float rotation_speed = 0.15f;
                float delta_time     = static_cast<float>(Timer::GetDeltaTimeSec());
                float angle          = rotation_speed * delta_time;
                Quaternion rotation  = Quaternion::FromAxisAngle(Vector3::Up, angle);
                turn_table->Rotate(rotation);

                // osd car specs
                const float x       = 0.75f;
                const float y       = 0.05f;
                const float spacing = 0.02f;

                static char text_buffer[128];

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

                Renderer::DrawString("The LaFerrari is Ferrari's first hybrid hypercar, blending a 6.3L V12 with", Vector2(x, y + spacing * 12));
                Renderer::DrawString("an electric motor via its HY-KERS system. It delivers extreme performance", Vector2(x, y + spacing * 13));
                Renderer::DrawString("and razor-sharp dynamics, wrapped in a design that embodies pure", Vector2(x, y + spacing * 14));
                Renderer::DrawString("Ferrari DNA. A limited-production icon of modern automotive engineering.", Vector2(x, y + spacing * 15));

                Renderer::DrawIcon(texture_brand_logo.get(), Vector2(400.0f, 300.0f));
            }
        }
        //====================================================================================

        //= LIMINAL SPACE ====================================================================
        namespace liminal_space
        {
            void create()
            {
                // shared tile material
                shared_ptr<Material> tile_material = make_shared<Material>();
                tile_material->SetResourceName("floor_tile" + string(EXTENSION_MATERIAL));
                tile_material->SetTexture(MaterialTextureType::Color,        "project\\materials\\tile_white\\albedo.png");
                tile_material->SetTexture(MaterialTextureType::Normal,       "project\\materials\\tile_white\\normal.png");
                tile_material->SetTexture(MaterialTextureType::Metalness,    "project\\materials\\tile_white\\metallic.png");
                tile_material->SetTexture(MaterialTextureType::Roughness,    "project\\materials\\tile_white\\roughness.png");
                tile_material->SetTexture(MaterialTextureType::Occlusion,    "project\\materials\\tile_white\\ao.png");
                tile_material->SetProperty(MaterialProperty::WorldSpaceUv,   1.0f);
                tile_material->SetProperty(MaterialProperty::TextureTilingX, 0.25);
                tile_material->SetProperty(MaterialProperty::TextureTilingY, 0.25);

                // pool light mesh
                Entity* entity_pool_light = nullptr;
                uint32_t flags  = Mesh::GetDefaultFlags() | static_cast<uint32_t>(MeshFlags::ImportCombineMeshes);
                flags          &= ~static_cast<uint32_t>(MeshFlags::PostProcessGenerateLods);
                if (shared_ptr<Mesh> mesh = ResourceCache::Load<Mesh>("project\\models\\pool_light\\pool_light.blend", flags))
                {
                    entity_pool_light = mesh->GetRootEntity();
                    entity_pool_light->SetObjectName("pool_light");
                    entity_pool_light->SetScale(0.5f);
                    entity_pool_light->SetPosition(Vector3(0.0f, 1000.0f, 0.0f)); // hide blueprint

                    entity_pool_light->GetChildByIndex(3)->SetActive(false);

                    // outer metallic ring
                    shared_ptr<Material> material_metal = make_shared<Material>();
                    material_metal->SetResourceName("material_metal" + string(EXTENSION_MATERIAL));
                    material_metal->SetProperty(MaterialProperty::Roughness, 0.5f);
                    material_metal->SetProperty(MaterialProperty::Metalness, 1.0f);
                    entity_pool_light->GetChildByName("Circle")->GetComponent<Renderable>()->SetMaterial(material_metal);

                    // inner light paraboloid
                    shared_ptr<Material> material_paraboloid = make_shared<Material>();
                    material_paraboloid->SetResourceName("material_paraboloid" + string(EXTENSION_MATERIAL));
                    material_paraboloid->SetTexture(MaterialTextureType::Emission, "project\\models\\pool_light\\emissive.png");
                    material_paraboloid->SetProperty(MaterialProperty::Roughness, 0.5f);
                    material_paraboloid->SetProperty(MaterialProperty::Metalness, 1.0f);
                    entity_pool_light->GetChildByName("Circle.001")->GetComponent<Renderable>()->SetMaterial(material_paraboloid);
                }

                // renderer
                Renderer::SetOption(Renderer_Option::ChromaticAberration, 1.0f);
                Renderer::SetOption(Renderer_Option::Vhs, 1.0f);

                // camera with flashlight
                entities::camera(true, Vector3(5.4084f, 1.8f, 4.7593f));
                default_camera->GetChildByIndex(0)->GetComponent<Camera>()->SetFlag(CameraFlags::Flashlight, true);

                // audio sources
                {
                    // electric hum
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
                }

                // room generation constants
                const float ROOM_WIDTH  = 40.0f;
                const float ROOM_DEPTH  = 40.0f;
                const float ROOM_HEIGHT = 100.0f;
                const float DOOR_WIDTH  = 2.0f;
                const float DOOR_HEIGHT = 5.0f;
                const int   NUM_ROOMS   = 100;

                enum class Direction { Front, Back, Left, Right, Max };

                // rng
                mt19937 rng(random_device{}());
                auto rand_int = [&](int max)
                {
                    uniform_int_distribution<int> dist(0, max - 1);
                    return dist(rng);
                };

                // surface factory
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

                // door factory
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

                // room factory
                auto create_room = [&](Direction door_dir, Direction skip_dir, const Vector3& offset, int room_index)
                {
                    auto room_entity = World::CreateEntity();
                    room_entity->SetObjectName("room_" + to_string(room_index));
                    room_entity->SetPosition(offset);

                    // random pool chance
                    uniform_real_distribution<float> dist(0.0f, 1.0f);
                    bool is_pool  = dist(rng) < 0.5f;
                    float floor_y = is_pool ? -0.5f : 0.0f;

                    // floor and ceiling
                    create_surface("floor", Vector3(0, floor_y, 0), Vector3(ROOM_WIDTH, 1, ROOM_DEPTH), room_entity);
                    create_surface("ceiling", Vector3(0, ROOM_HEIGHT, 0), Vector3(ROOM_WIDTH, 1, ROOM_DEPTH), room_entity);

                    // water
                    if (is_pool)
                    {
                        float water_distance = 0.5f;
                        float water_y        = floor_y + 0.5f + water_distance;
                        Color pool_color     = Color(0.0f, 150.0f / 255.0f, 130.0f / 255.0f, 254.0f / 255.0f);
                        auto water           = entities::water(Vector3(0, water_y, 0), ROOM_WIDTH, 2, pool_color);
                        water->SetParent(room_entity);
                    }

                    // wall configs
                    const struct WallConfig
                    {
                        Vector3 pos;
                        Vector3 scale;
                    } walls[] = {
                        {Vector3(0, ROOM_HEIGHT / 2, -ROOM_DEPTH / 2), {ROOM_WIDTH, ROOM_HEIGHT, 1}},
                        {Vector3(0, ROOM_HEIGHT / 2, ROOM_DEPTH / 2), {ROOM_WIDTH, ROOM_HEIGHT, 1}},
                        {Vector3(-ROOM_WIDTH / 2, ROOM_HEIGHT / 2, 0), {1, ROOM_HEIGHT, ROOM_DEPTH}},
                        {Vector3(ROOM_WIDTH / 2, ROOM_HEIGHT / 2, 0), {1, ROOM_HEIGHT, ROOM_DEPTH}}
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

                        // side wall lights
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

                // procedural path generation
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
                            case Direction::Back:  next.second += 1; break;
                            case Direction::Left:  next.first  -= 1; break;
                            case Direction::Right: next.first  += 1; break;
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

                // compute door directions
                vector<Direction> doors(actual_rooms);
                for (int i = 1; i < actual_rooms; i++)
                {
                    auto prev = path[i - 1];
                    auto curr = path[i];
                    int dx = curr.first - prev.first;
                    int dz = curr.second - prev.second;
                    if (dx == 1)       doors[i - 1] = Direction::Right;
                    else if (dx == -1) doors[i - 1] = Direction::Left;
                    else if (dz == 1)  doors[i - 1] = Direction::Back;
                    else if (dz == -1) doors[i - 1] = Direction::Front;
                }
                doors[actual_rooms - 1] = static_cast<Direction>(rand_int(4));

                // create all rooms
                for (int i = 0; i < actual_rooms; i++)
                {
                    Vector3 offset = Vector3(path[i].first * ROOM_WIDTH, 0, path[i].second * ROOM_DEPTH);
                    Direction skip_dir = (i == 0) ? Direction::Max : Direction::Max;
                    if (i > 0)
                    {
                        switch (doors[i - 1])
                        {
                            case Direction::Front: skip_dir = Direction::Back;  break;
                            case Direction::Back:  skip_dir = Direction::Front; break;
                            case Direction::Left:  skip_dir = Direction::Right; break;
                            case Direction::Right: skip_dir = Direction::Left;  break;
                        }
                    }
                    create_room(doors[i], skip_dir, offset, i);
                }
            }

            void tick()
            {
                // footstep audio based on surface
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
        //====================================================================================

        //= BASIC ============================================================================
        namespace basic
        {
            void create()
            {
                entities::camera(false);
                entities::floor();
                entities::sun(LightPreset::dusk, true);
                entities::material_ball(Vector3::Zero);
            }
        }
        //====================================================================================

        //= DRIVABLE CAR =========================================================================
        namespace drivable_car
        {
            Entity* vehicle_entity = nullptr;

            void create()
            {
                entities::camera(false, Vector3(0.0f, 5.0f, -15.0f), Vector3(5.0f, 0.0f, 0.0f));
                entities::sun(LightPreset::dusk, true);
                entities::floor();

                // vehicle entity
                vehicle_entity = World::CreateEntity();
                vehicle_entity->SetObjectName("vehicle_test");
                vehicle_entity->SetPosition(Vector3(0.0f, 2.0f, 0.0f));

                // vehicle physics
                Physics* physics = vehicle_entity->AddComponent<Physics>();
                physics->SetStatic(false);
                physics->SetMass(1500.0f);
                physics->SetBodyType(BodyType::Vehicle);

                // use existing car function for the body (handles all materials)
                car::create(Vector3::Zero, false, nullptr);

                // parent the car body to the vehicle entity
                if (default_car)
                {
                    default_car->SetParent(vehicle_entity);
                    default_car->SetPositionLocal(Vector3(0.0f, -0.65f, 0.0f)); // adjust chassis to match the physics
                    default_car->SetRotationLocal(Quaternion::FromAxisAngle(Vector3::Right, math::pi * 0.5f)); // rotate 90 degrees around X to face forward
                    default_car->SetScaleLocal(1.0f);
                }

                // load wheel and create 4 instances for the vehicle
                if (shared_ptr<Mesh> mesh = ResourceCache::Load<Mesh>("project\\models\\wheel\\model.blend"))
                {
                    Entity* wheel_root = mesh->GetRootEntity();
                    Entity* wheel_base = wheel_root->GetChildByIndex(0);
                    if (wheel_base)
                    {
                        // remove and delete parent - makes all math simpler down the line
                        wheel_base->SetParent(nullptr);
                        World::RemoveEntity(wheel_root);
                        
                        // scale to fit the car
                        wheel_base->SetScale(0.2f);

                        // set material
                        if (Renderable* renderable = wheel_base->GetComponent<Renderable>())
                        {
                            Material* material = renderable->GetMaterial();
                            material->SetTexture(MaterialTextureType::Color,     "project\\models\\wheel\\albedo.jpeg");
                            material->SetTexture(MaterialTextureType::Metalness, "project\\models\\wheel\\metalness.png");
                            material->SetTexture(MaterialTextureType::Normal,    "project\\models\\wheel\\normal.png");
                            material->SetTexture(MaterialTextureType::Roughness, "project\\models\\wheel\\roughness.png");
                        }

                        // compute wheel radius from the now-standalone entity
                        physics->ComputeWheelRadiusFromEntity(wheel_base);
                        const float wheel_radius = physics->GetWheelRadius();

                        // wheel positions relative to vehicle body center (laferrari dimensions)
                        // physics wheel shapes are at Y = -suspension_height relative to body center
                        // the visual wheel mesh has its origin at the center of the rim, matching the physics shape center
                        const float suspension_height = physics->GetSuspensionHeight();
                        const float wheel_x = 0.95f;
                        const float wheel_y = -suspension_height;
                        const float front_z = 1.45f;
                        const float rear_z  = -1.35f;

                        // front left wheel (use the base)
                        Entity* wheel_fl = wheel_base;
                        wheel_fl->SetObjectName("wheel_front_left");
                        wheel_fl->SetParent(vehicle_entity);
                        wheel_fl->SetPositionLocal(Vector3(-wheel_x, wheel_y, front_z));

                        // front right wheel (clone and mirror)
                        Entity* wheel_fr = wheel_base->Clone();
                        wheel_fr->SetObjectName("wheel_front_right");
                        wheel_fr->SetParent(vehicle_entity);
                        wheel_fr->SetPositionLocal(Vector3(wheel_x, wheel_y, front_z));
                        wheel_fr->SetRotationLocal(Quaternion::FromAxisAngle(Vector3::Up, math::pi)); // rotate to face outward

                        // rear left wheel (clone)
                        Entity* wheel_rl = wheel_base->Clone();
                        wheel_rl->SetObjectName("wheel_rear_left");
                        wheel_rl->SetParent(vehicle_entity);
                        wheel_rl->SetPositionLocal(Vector3(-wheel_x, wheel_y, rear_z));

                        // rear right wheel (clone and mirror)
                        Entity* wheel_rr = wheel_base->Clone();
                        wheel_rr->SetObjectName("wheel_rear_right");
                        wheel_rr->SetParent(vehicle_entity);
                        wheel_rr->SetPositionLocal(Vector3(wheel_x, wheel_y, rear_z));
                        wheel_rr->SetRotationLocal(Quaternion::FromAxisAngle(Vector3::Up, math::pi)); // rotate to face outward

                        // hook up wheel entities to the physics component
                        physics->SetWheelEntity(WheelIndex::FrontLeft,  wheel_fl);
                        physics->SetWheelEntity(WheelIndex::FrontRight, wheel_fr);
                        physics->SetWheelEntity(WheelIndex::RearLeft,   wheel_rl);
                        physics->SetWheelEntity(WheelIndex::RearRight,  wheel_rr);
                    }
                }

                // hook up chassis entity (the ferrari body that bounces on the suspension)
                if (default_car)
                {
                    physics->SetChassisEntity(default_car);
                }
            }

            void tick()
            {
                if (!vehicle_entity)
                    return;

                Physics* physics = vehicle_entity->GetComponent<Physics>();
                if (!physics)
                    return;

                // input handling
                if (Engine::IsFlagSet(EngineMode::Playing))
                {
                    physics->SetVehicleThrottle(Input::GetKey(KeyCode::Arrow_Up) ? 1.0f : 0.0f);
                    physics->SetVehicleBrake(Input::GetKey(KeyCode::Arrow_Down) ? 1.0f : 0.0f);

                    float steering = 0.0f;
                    if (Input::GetKey(KeyCode::Arrow_Left))  steering = -1.0f;
                    if (Input::GetKey(KeyCode::Arrow_Right)) steering =  1.0f;
                    physics->SetVehicleSteering(steering);
                }

                // osd - vehicle telemetry
                static char text_buffer[256];
                Vector3 velocity = physics->GetLinearVelocity();
                float speed_kmh = velocity.Length() * 3.6f;
                
                float y_pos = 0.58f;
                const float line_spacing = 0.018f;
                
                // header
                Renderer::DrawString("Vehicle Telemetry", Vector2(0.005f, y_pos));
                y_pos += line_spacing * 1.2f;
                
                // speed and rpm (average of driven wheels)
                float avg_rpm = (physics->GetWheelRPM(WheelIndex::RearLeft) + physics->GetWheelRPM(WheelIndex::RearRight)) * 0.5f;
                snprintf(text_buffer, sizeof(text_buffer), "Speed: %.1f km/h   RPM: %.0f", speed_kmh, avg_rpm);
                Renderer::DrawString(text_buffer, Vector2(0.005f, y_pos));
                y_pos += line_spacing;
                
                // inputs
                snprintf(text_buffer, sizeof(text_buffer), "Throttle: %.0f%%   Brake/Rev: %.0f%%   Steer: %+.0f%%",
                    physics->GetVehicleThrottle() * 100.0f,
                    physics->GetVehicleBrake() * 100.0f,
                    physics->GetVehicleSteering() * 100.0f);
                Renderer::DrawString(text_buffer, Vector2(0.005f, y_pos));
                y_pos += line_spacing * 1.5f;
                
                // per-wheel metrics header
                Renderer::DrawString("Tire Physics:", Vector2(0.005f, y_pos));
                y_pos += line_spacing;
                Renderer::DrawString("       GND   Slip Angle   Slip Ratio   Lat Force   Long Force   Load", Vector2(0.005f, y_pos));
                y_pos += line_spacing;
                
                const char* wheel_names[] = { "FL", "FR", "RL", "RR" };
                for (int i = 0; i < static_cast<int>(WheelIndex::Count); i++)
                {
                    WheelIndex wheel = static_cast<WheelIndex>(i);
                    bool grounded       = physics->IsWheelGrounded(wheel);
                    float slip_angle    = physics->GetWheelSlipAngle(wheel) * 57.2958f;     // rad to deg
                    float slip_ratio    = physics->GetWheelSlipRatio(wheel) * 100.0f;       // to percentage
                    float lat_force_kn  = physics->GetWheelLateralForce(wheel) / 1000.0f;
                    float long_force_kn = physics->GetWheelLongitudinalForce(wheel) / 1000.0f;
                    float load_kn       = physics->GetWheelTireLoad(wheel) / 1000.0f;
                    
                    snprintf(text_buffer, sizeof(text_buffer), "  %s:  %s   %+6.1f deg   %+6.1f %%    %+5.1f kN    %+5.1f kN   %.1f kN",
                        wheel_names[i],
                        grounded ? "YES" : " - ",
                        slip_angle,
                        slip_ratio,
                        lat_force_kn,
                        long_force_kn,
                        load_kn);
                    Renderer::DrawString(text_buffer, Vector2(0.005f, y_pos));
                    y_pos += line_spacing;
                }
                
                // suspension compression visual
                y_pos += line_spacing * 0.5f;
                Renderer::DrawString("Suspension:", Vector2(0.005f, y_pos));
                y_pos += line_spacing;
                for (int i = 0; i < static_cast<int>(WheelIndex::Count); i++)
                {
                    WheelIndex wheel = static_cast<WheelIndex>(i);
                    float compression = physics->GetWheelCompression(wheel);
                    // invert: show fewer bars when compressed (spring is shorter)
                    int bar_len = static_cast<int>((1.0f - compression) * 20.0f);
                    bar_len = bar_len > 20 ? 20 : bar_len;
                    
                    char bar[32];
                    for (int j = 0; j < 20; j++)
                        bar[j] = (j < bar_len) ? '|' : '.';
                    bar[20] = '\0';
                    
                    snprintf(text_buffer, sizeof(text_buffer), "  %s: [%s] %.0f%%",
                        wheel_names[i], bar, compression * 100.0f);
                    Renderer::DrawString(text_buffer, Vector2(0.005f, y_pos));
                    y_pos += line_spacing;
                }
                
                y_pos += line_spacing * 0.5f;
                Renderer::DrawString("Controls: Arrow Keys (Up=Throttle, Down=Brake/Reverse, L/R=Steer)", Vector2(0.005f, y_pos));
            }
        }
        //====================================================================================
    }
    //========================================================================================

    //= PUBLIC API ===========================================================================
    void Game::Shutdown()
    {
        // reset shared entities
        default_floor                          = nullptr;
        default_camera                         = nullptr;
        default_environment                    = nullptr;
        default_light_directional              = nullptr;
        default_terrain                        = nullptr;
        default_car                            = nullptr;
        default_metal_cube                     = nullptr;
        worlds::showroom::texture_brand_logo   = nullptr;
        worlds::showroom::texture_paint_normal = nullptr;
        worlds::drivable_car::vehicle_entity   = nullptr;
        meshes.clear();
    }

    void Game::Tick()
    {
        // car tick (always)
        car::tick();

        // world-specific tick
        if (loaded_world != DefaultWorld::Max)
        {
            if (auto fn = world_tick[static_cast<size_t>(loaded_world)])
            {
                fn();
            }
        }
    }

    void Game::Load(DefaultWorld default_world)
    {
        Game::Shutdown();
        World::Shutdown();

        ThreadPool::AddTask([default_world]()
        {
            ProgressTracker::SetGlobalLoadingState(true);
            set_base_renderer_options();

            // dispatch to world create function
            world_create[static_cast<size_t>(default_world)]();

            ProgressTracker::SetGlobalLoadingState(false);
        });

        loaded_world = default_world;
    }
    //========================================================================================
}
