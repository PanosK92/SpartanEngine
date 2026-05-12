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
#include "../Input/Input.h"
#include "../Car/Car.h"
#include "../Car/CarSimulation.h"
#include "../World/Entity.h"
#include "../World/Prefab.h"
#include "../World/Components/Camera.h"
#include "../World/Components/Light.h"
#include "../World/Components/Physics.h"
#include "../World/Components/AudioSource.h"
#include "../World/Components/Terrain.h"
#include "../Core/ProgressTracker.h"
#include "../Core/ThreadPool.h"
#include "../Core/Stopwatch.h"
#include "../Rendering/Renderer.h"
#include "../Rendering/GeometryBuffer.h"
#include "../Resource/ResourceCache.h"
#include "../Geometry/GeometryGeneration.h"
#include "../Geometry/GeometryProcessing.h"
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
        namespace forest     { void create(); void tick(); }
        namespace sponza     { void create(); }
        namespace light_test { void create(); }
        namespace empty      { void create(); }
    }
    //======================================================

    // entities shared with other files (external linkage required)
    Entity* default_car        = nullptr;
    Entity* default_car_window = nullptr;
    Entity* default_camera     = nullptr;

    namespace
    {
        //= STATE ====================================
        DefaultWorld loaded_world = DefaultWorld::Max;
        //============================================

        //= SHARED ENTITIES ========================
        Entity* default_floor             = nullptr;
        Entity* default_terrain           = nullptr;
        Entity* default_environment       = nullptr;
        Entity* default_light_directional = nullptr;
        Entity* default_metal_cube        = nullptr;
        Entity* default_water             = nullptr;
        std::vector<std::shared_ptr<Mesh>> meshes;
        //==========================================

        //= WORLD DISPATCH TABLES =====================================================================================================
        using create_fn = void(*)();
        using tick_fn   = void(*)();

        // indexed by DefaultWorld enum - add new worlds here
        constexpr create_fn world_create[] =
        {
            worlds::forest::create,
            worlds::sponza::create,
            worlds::light_test::create,
            worlds::empty::create,
        };

        constexpr tick_fn world_tick[] =
        {
            worlds::forest::tick,
            nullptr,
            nullptr,
            nullptr,
        };

        static_assert(size(world_create) == static_cast<size_t>(DefaultWorld::Max), "world_create out of sync with DefaultWorld enum");
        static_assert(size(world_tick)   == static_cast<size_t>(DefaultWorld::Max), "world_tick out of sync with DefaultWorld enum");
        //=============================================================================================================================

        //= ENTITY BUILDING BLOCKS ===================================================================
        namespace entities
        {
            // background music
            void music(const char* soundtrack_file_path = "project/music/jake_chudnow_shona.wav")
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
                    camera_comp->SetAperture(5.0f);
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

                Render* renderable = default_floor->AddComponent<Render>();
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
                material->SetTexture(MaterialTextureType::Color,     "project/materials/crate_space/albedo.png");
                material->SetTexture(MaterialTextureType::Normal,    "project/materials/crate_space/normal.png");
                material->SetTexture(MaterialTextureType::Occlusion, "project/materials/crate_space/ao.png");
                material->SetTexture(MaterialTextureType::Roughness, "project/materials/crate_space/roughness.png");
                material->SetTexture(MaterialTextureType::Metalness, "project/materials/crate_space/metallic.png");
                material->SetTexture(MaterialTextureType::Height,    "project/materials/crate_space/height.png");
                material->SetProperty(MaterialProperty::Tessellation, 1.0f);
                material->SetResourceName("crate_space" + string(EXTENSION_MATERIAL));

                Render* renderable = default_metal_cube->AddComponent<Render>();
                renderable->SetMesh(MeshType::Cube);
                renderable->SetMaterial(material);

                Physics* physics_body = default_metal_cube->AddComponent<Physics>();
                physics_body->SetMass(Physics::mass_from_volume);
                physics_body->SetBodyType(BodyType::Box);
            }

            // flight helmet model
            void flight_helmet(const Vector3& position)
            {
                if (shared_ptr<Mesh> mesh = ResourceCache::Load<Mesh>("project/models/flight_helmet/FlightHelmet.gltf"))
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
                if (shared_ptr<Mesh> mesh = ResourceCache::Load<Mesh>("project/models/damaged_helmet/DamagedHelmet.gltf"))
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
                if (shared_ptr<Mesh> mesh = ResourceCache::Load<Mesh>("project/models/material_ball_in_3d-coat/scene.gltf", flags))
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
                    material->SetTexture(MaterialTextureType::Normal,            "project/materials/water/normal.jpeg");
                    material->SetProperty(MaterialProperty::Roughness,           0.0f);
                    material->SetProperty(MaterialProperty::Clearcoat,           0.0f);
                    material->SetProperty(MaterialProperty::Clearcoat_Roughness, 0.0f);
                    material->SetProperty(MaterialProperty::WorldSpaceUv,        1.0f);
                    material->SetProperty(MaterialProperty::TextureTilingX,      1.0f);
                    material->SetProperty(MaterialProperty::TextureTilingY,      1.0f);
                    material->SetProperty(MaterialProperty::IsWater,             1.0f);
                    material->SetProperty(MaterialProperty::Normal,              0.01f);
                    material->SetProperty(MaterialProperty::TextureTilingX,      0.1f);
                    material->SetProperty(MaterialProperty::TextureTilingY,      0.1f);
                }

                // generate tiled geometry
                {
                    const uint32_t grid_points_per_dimension = density;
                    vector<RHI_Vertex_PosTexNorTan> vertices;
                    vector<uint32_t> indices;
                    geometry_generation::generate_grid(&vertices, &indices, grid_points_per_dimension, dimension);

                    const uint32_t tile_count = max(1u, density / 6);
                    vector<vector<RHI_Vertex_PosTexNorTan>> tiled_vertices;
                    vector<vector<uint32_t>> tiled_indices;
                    vector<Vector3> tile_offsets;
                    spartan::geometry_processing::split_surface_into_tiles(vertices, indices, tile_count, tiled_vertices, tiled_indices, tile_offsets);

                    const uint32_t actual_tile_count = static_cast<uint32_t>(tiled_vertices.size());

                    // pre-allocate per-tile meshes sequentially so meshes.emplace_back stays single-threaded
                    vector<shared_ptr<Mesh>> tile_meshes(actual_tile_count);
                    for (uint32_t tile_index = 0; tile_index < actual_tile_count; tile_index++)
                    {
                        string name = "tile_" + to_string(tile_index);
                        tile_meshes[tile_index] = meshes.emplace_back(make_shared<Mesh>());
                        tile_meshes[tile_index]->SetObjectName(name);
                        tile_meshes[tile_index]->SetFlag(static_cast<uint32_t>(MeshFlags::PostProcessOptimize), false);
                    }

                    // build mesh geometry in parallel, each tile is independent
                    ThreadPool::ParallelLoop([&tile_meshes, &tiled_vertices, &tiled_indices](uint32_t start, uint32_t end)
                    {
                        for (uint32_t i = start; i < end; i++)
                        {
                            tile_meshes[i]->AddGeometry(tiled_vertices[i], tiled_indices[i], false);
                            tile_meshes[i]->CreateGpuBuffers();
                        }
                    }, actual_tile_count);

                    // create mesh tile entities sequentially
                    for (uint32_t tile_index = 0; tile_index < actual_tile_count; tile_index++)
                    {
                        Entity* entity_tile = World::CreateEntity();
                        entity_tile->SetObjectName(tile_meshes[tile_index]->GetObjectName());
                        entity_tile->SetParent(water);
                        entity_tile->SetPosition(tile_offsets[tile_index]);

                        if (Render* renderable = entity_tile->AddComponent<Render>())
                        {
                            renderable->SetMesh(tile_meshes[tile_index].get());
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
            ConsoleRegistry::Get().SetValueFromString("r.dithering",            "0");
            ConsoleRegistry::Get().SetValueFromString("r.chromatic_aberration", "0");
            ConsoleRegistry::Get().SetValueFromString("r.grid",                 "0");
            ConsoleRegistry::Get().SetValueFromString("r.vhs",                  "0");
        }
    }
    //========================================================================================

    // register prefabs (called once before any world file is loaded)
    namespace
    {
        bool prefabs_registered = false;
    }

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
                default_light_directional->SetRotation(Quaternion::FromEulerAngles(75.0f, 180.0f, 180.0f));
                entities::music("project/music/jake_chudnow_olive.wav");
                entities::floor();
                World::SetWind(Vector3(0.0f, 0.2f, 1.0f) * 0.1f);

                const Vector3 position = Vector3(0.0f, 1.5f, 0.0f);
                const float scale      = 1.5f;

                // load the three glTFs in parallel
                shared_ptr<Mesh> mesh_main;
                shared_ptr<Mesh> mesh_curtains;
                shared_ptr<Mesh> mesh_ivy;
                {
                    Stopwatch sw_total;
                    uint32_t mesh_flags     = Mesh::GetDefaultFlags();
                    future<void> f_main     = ThreadPool::AddTask([&mesh_main, mesh_flags]()     { mesh_main     = ResourceCache::Load<Mesh>("project/models/sponza/main/NewSponza_Main_Blender_glTF.gltf", mesh_flags); });
                    future<void> f_curtains = ThreadPool::AddTask([&mesh_curtains]()             { mesh_curtains = ResourceCache::Load<Mesh>("project/models/sponza/curtains/NewSponza_Curtains_glTF.gltf"); });
                    future<void> f_ivy      = ThreadPool::AddTask([&mesh_ivy]()                  { mesh_ivy      = ResourceCache::Load<Mesh>("project/models/sponza/ivy/NewSponza_IvyGrowth_glTF.gltf"); });
                    f_main.wait();
                    f_curtains.wait();
                    f_ivy.wait();
                    SP_LOG_INFO("sponza parallel mesh load took %d ms", static_cast<int>(sw_total.GetElapsedTimeMs()));
                }

                // main building
                if (mesh_main)
                {
                    Entity* entity = mesh_main->GetRootEntity();
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
                        if (entity_it->GetActive() && entity_it->GetComponent<Render>() != nullptr)
                        {
                            Physics* physics_body = entity_it->AddComponent<Physics>();
                            physics_body->SetBodyType(BodyType::Mesh);
                        }
                    }
                }

                // curtains
                if (mesh_curtains)
                {
                    Entity* entity = mesh_curtains->GetRootEntity();
                    entity->SetObjectName("sponza_curtains");
                    entity->SetPosition(position);
                    entity->SetScale(scale);

                    // fabric wind animation
                    const char* curtain_parts[] = {"curtain_03_2", "curtain_03_3", "curtain_hanging_06_3"};
                    for (const char* part : curtain_parts)
                    {
                        if (Material* material = entity->GetDescendantByName(part)->GetComponent<Render>()->GetMaterial())
                        {
                            material->SetProperty(MaterialProperty::CullMode, static_cast<float>(RHI_CullMode::None));
                        }
                    }
                }

                // ivy
                if (mesh_ivy)
                {
                    Entity* entity = mesh_ivy->GetRootEntity();
                    entity->SetObjectName("sponza_ivy");
                    entity->SetPosition(position);
                    entity->SetScale(scale);

                    // leaf material
                    if (Entity* leaves = entity->GetDescendantByName("IvySim_Leaves"))
                    { 
                        if (Material* material = leaves->GetComponent<Render>()->GetMaterial())
                        {
                            material->SetProperty(MaterialProperty::CullMode,                   static_cast<float>(RHI_CullMode::None));
                            material->SetProperty(MaterialProperty::SubsurfaceScattering,       1.0f);
                            material->SetProperty(MaterialProperty::ColorVariationFromInstance, 1.0f);
                        }
                    }

                    // stem material
                    if (Entity* stems = entity->GetDescendantByName("IvySim_Stems"))
                    { 
                        if (Material* material = stems->GetComponent<Render>()->GetMaterial())
                        {
                            material->SetProperty(MaterialProperty::SubsurfaceScattering, 1.0f);
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

                // pre-size the global geometry buffer high enough for the whole forest so worker threads streaming
                // mesh data in cannot trip a mid-load rebuild from the renderer's per-frame BuildIfDirty
                GeometryBuffer::Reserve(
                    12u * 1024u * 1024u, // ~12M vertices  (terrain lods + trees + rocks + water + foliage)
                    32u * 1024u * 1024u, // ~32M indices
                    128u * 1024u,        // ~128K meshlet bounds
                    256u * 1024u         // ~256K instances (grass dominates this)
                );

                // kick off heavy mesh work in parallel so it overlaps terrain generation, audio, camera, lighting
                shared_ptr<Mesh> mesh_tree;
                shared_ptr<Mesh> mesh_rock;
                shared_ptr<Mesh> mesh_grass_blade = meshes.emplace_back(make_shared<Mesh>());
                shared_ptr<Mesh> mesh_flower      = meshes.emplace_back(make_shared<Mesh>());

                Stopwatch sw_parallel_meshes;

                const uint32_t tree_flags      = Mesh::GetDefaultFlags() | static_cast<uint32_t>(MeshFlags::ImportCombineMeshes);
                const string grass_cache_path  = string(ResourceCache::GetProjectDirectory()) + "standard_grass" + EXTENSION_MESH;
                const string flower_cache_path = string(ResourceCache::GetProjectDirectory()) + "standard_flower" + EXTENSION_MESH;

                future<void> f_tree = ThreadPool::AddTask([&mesh_tree, tree_flags]()
                {
                    mesh_tree = ResourceCache::Load<Mesh>("project/models/tree/tree.fbx", tree_flags);
                });

                future<void> f_rock = ThreadPool::AddTask([&mesh_rock]()
                {
                    mesh_rock = ResourceCache::Load<Mesh>("project/models/rock_2/model.obj");
                });

                future<void> f_grass = ThreadPool::AddTask([mesh_grass_blade, grass_cache_path]()
                {
                    // try the engine-mesh cache first to skip simplify and build_meshlets entirely
                    if (FileSystem::Exists(grass_cache_path))
                    {
                        mesh_grass_blade->LoadFromFile(grass_cache_path);
                        if (mesh_grass_blade->GetVertexCount() > 0)
                            return;
                    }

                    mesh_grass_blade->SetObjectName("grass_blade");
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

                    mesh_grass_blade->SetResourceFilePath(grass_cache_path);
                    mesh_grass_blade->SaveToFile(grass_cache_path);
                    mesh_grass_blade->CreateGpuBuffers();
                });

                future<void> f_flower = ThreadPool::AddTask([mesh_flower, flower_cache_path]()
                {
                    if (FileSystem::Exists(flower_cache_path))
                    {
                        mesh_flower->LoadFromFile(flower_cache_path);
                        if (mesh_flower->GetVertexCount() > 0)
                            return;
                    }

                    mesh_flower->SetObjectName("flower");
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

                    mesh_flower->SetResourceFilePath(flower_cache_path);
                    mesh_flower->SaveToFile(flower_cache_path);
                    mesh_flower->CreateGpuBuffers();
                });

                // lighting
                entities::sun(LightPreset::david_lynch, true);
                default_light_directional->SetRotation(Quaternion::FromEulerAngles(9.07f, -122.84f, 180.0f));
                Light* sun = default_light_directional->GetComponent<Light>();
                sun->SetFlag(LightFlags::Volumetric, true);

                entities::camera(false, Vector3(-1476.0f, 17.9f, 1490.0f), Vector3(-3.6f, 90.0f, 0.0f));
                ConsoleRegistry::Get().SetValueFromString("r.grid", "0");

                // drivable car near the player
                {
                    //Car::Config car_config;
                    //car_config.position       = Vector3(-1470.0f, 20.0f, 1490.0f); // slightly in front of camera
                    //car_config.drivable       = true;
                    //car_config.show_telemetry = true;
                    //Car::Create(car_config);
                }

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
                        audio_source->SetAudioClip("project/music/footsteps_grass.wav");
                        audio_source->SetPlayOnStart(false);
                    }

                    // forest ambience
                    {
                        Entity* sound = World::CreateEntity();
                        sound->SetObjectName("forest_river");
                        sound->SetParent(entity);
                        AudioSource* audio_source = sound->AddComponent<AudioSource>();
                        audio_source->SetAudioClip("project/music/forest_river.wav");
                        audio_source->SetLoop(true);
                    }

                    // wind
                    {
                        Entity* sound = World::CreateEntity();
                        sound->SetObjectName("wind");
                        sound->SetParent(entity);
                        AudioSource* audio_source = sound->AddComponent<AudioSource>();
                        audio_source->SetAudioClip("project/music/wind.wav");
                        audio_source->SetLoop(true);
                    }

                    // underwater
                    {
                        Entity* sound = World::CreateEntity();
                        sound->SetObjectName("underwater");
                        sound->SetParent(entity);
                        AudioSource* audio_source = sound->AddComponent<AudioSource>();
                        audio_source->SetAudioClip("project/music/underwater.wav");
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
                        material->SetProperty(MaterialProperty::TextureTilingX, 2000.0f);
                        material->SetProperty(MaterialProperty::TextureTilingY, 2000.0f);

                        // grass layer
                        material->SetTexture(MaterialTextureType::Color,     "project/materials/whispy_grass_meadow/albedo.png",    0);
                        material->SetTexture(MaterialTextureType::Normal,    "project/materials/whispy_grass_meadow/normal.png",    0);
                        material->SetTexture(MaterialTextureType::Roughness, "project/materials/whispy_grass_meadow/roughness.png", 0);
                        material->SetTexture(MaterialTextureType::Occlusion, "project/materials/whispy_grass_meadow/occlusion.png", 0);

                        // rock layer
                        material->SetTexture(MaterialTextureType::Color,     "project/materials/rock/albedo.png",    1);
                        material->SetTexture(MaterialTextureType::Normal,    "project/materials/rock/normal.png",    1);
                        material->SetTexture(MaterialTextureType::Roughness, "project/materials/rock/roughness.png", 1);
                        material->SetTexture(MaterialTextureType::Occlusion, "project/materials/rock/occlusion.png", 1);
                        material->SetTexture(MaterialTextureType::Height,    "project/materials/rock/height.png",    1);

                        // sand layer
                        material->SetTexture(MaterialTextureType::Color,     "project/materials/sand/albedo.png",    2);
                        material->SetTexture(MaterialTextureType::Normal,    "project/materials/sand/normal.png",    2);
                        material->SetTexture(MaterialTextureType::Roughness, "project/materials/sand/roughness.png", 2);
                        material->SetTexture(MaterialTextureType::Occlusion, "project/materials/sand/occlusion.png", 2);
                        material->SetProperty(MaterialProperty::Tessellation, 0.0f);
                    }

                    // height map generation
                    shared_ptr<RHI_Texture> height_map = ResourceCache::Load<RHI_Texture>("project/height_maps/height_map.png");
                    if (height_map)
                    {
                        height_map->PrepareForGpu();
                    }
                    terrain->SetHeightMapSeed(height_map.get());
                    terrain->Generate();

                    // terrain physics, parallelized because each tile's BodyType::Mesh path simplifies the mesh and runs physx cooking, which dominates this loop
                    vector<Entity*> terrain_tiles = terrain->GetEntity()->GetChildren();
                    ThreadPool::ParallelLoop([&terrain_tiles](uint32_t start, uint32_t end)
                    {
                        for (uint32_t i = start; i < end; i++)
                        {
                            Physics* physics_body = terrain_tiles[i]->AddComponent<Physics>();
                            physics_body->SetBodyType(BodyType::Mesh);
                        }
                    }, static_cast<uint32_t>(terrain_tiles.size()));
                }

                // water
                const float dimension          = 8000;
                const uint32_t density         = 64;
                const Color forest_water_color = Color(0.0f / 255.0f, 140.0f / 255.0f, 100.0f / 255.0f, 50.0f / 255.0f);
                entities::water(Vector3::Zero, dimension, density, forest_water_color);

                // props: trees, rocks, grass
                {
                    // wait for the parallel mesh tasks kicked off at the top of forest::create
                    f_tree.wait();
                    f_rock.wait();
                    f_grass.wait();
                    f_flower.wait();
                    SP_LOG_INFO("forest parallel mesh build took %d ms", static_cast<int>(sw_parallel_meshes.GetElapsedTimeMs()));

                    // materials
                    shared_ptr<Material> material_leaf;
                    shared_ptr<Material> material_body;
                    shared_ptr<Material> material_rock;
                    shared_ptr<Material> material_grass_blade;
                    shared_ptr<Material> material_flower;
                    {
                        // tree leaves
                        material_leaf = make_shared<Material>();
                        material_leaf->SetTexture(MaterialTextureType::Color, "project/models/tree/Twig_Base_Material_2.png");
                        material_leaf->SetTexture(MaterialTextureType::Normal, "project/models/tree/Twig_Normal.png");
                        material_leaf->SetTexture(MaterialTextureType::AlphaMask, "project/models/tree/Twig_Opacity_Map.jpg");
                        material_leaf->SetProperty(MaterialProperty::WindAnimation, 1.0f);
                        material_leaf->SetProperty(MaterialProperty::ColorVariationFromInstance, 1.0f);
                        material_leaf->SetProperty(MaterialProperty::SubsurfaceScattering, 1.0f);
                        material_leaf->SetResourceName("tree_leaf" + string(EXTENSION_MATERIAL));

                        // tree bark
                        material_body = make_shared<Material>();
                        material_body->SetTexture(MaterialTextureType::Color, "project/models/tree/tree_bark_diffuse.png");
                        material_body->SetTexture(MaterialTextureType::Normal, "project/models/tree/tree_bark_normal.png");
                        material_body->SetTexture(MaterialTextureType::Roughness, "project/models/tree/tree_bark_roughness.png");
                        material_body->SetResourceName("tree_body" + string(EXTENSION_MATERIAL));

                        // rocks
                        material_rock = make_shared<Material>();
                        material_rock->SetTexture(MaterialTextureType::Color, "project/models/rock_2/albedo.png");
                        material_rock->SetTexture(MaterialTextureType::Normal, "project/models/rock_2/normal.png");
                        material_rock->SetTexture(MaterialTextureType::Roughness, "project/models/rock_2/roughness.png");
                        material_rock->SetTexture(MaterialTextureType::Occlusion, "project/models/rock_2/occlusion.png");
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

                    // collected per tile so the parallel pass can write without contention
                    // trees and rocks are merged into single entities afterwards so physx cooks each shape only once
                    vector<vector<Matrix>> tree_transforms_per_tile(children.size());
                    vector<vector<Matrix>> rock_transforms_per_tile(children.size());

                    auto place_props_on_tiles = [
                        &children,
                        &mesh_grass_blade,
                        &mesh_flower,
                        &mesh_tree,
                        &mesh_rock,
                        &tree_transforms_per_tile,
                        &rock_transforms_per_tile,
                        &terrain,
                        render_distance_foliage,
                        per_triangle_density_grass_blade,
                        per_triangle_density_flower,
                        per_triangle_density_tree,
                        per_triangle_density_rock,
                        material_grass_blade,
                        material_flower
                    ](uint32_t start_index, uint32_t end_index)
                    {
                        for (uint32_t tile_index = start_index; tile_index < end_index; tile_index++)
                        {
                            Entity* terrain_tile = children[tile_index];

                            // tile vertices are tile-local, so FindTransforms returns instance matrices in tile-local space
                            // when the consolidated tree/rock entity is parented to default_terrain (at origin), we need world-space instances
                            // multiply by the tile's world matrix to lift each per-tile instance into the consolidated entity's space
                            const math::Matrix tile_world_matrix = terrain_tile->GetMatrix();

                            // tree transforms (deferred to a single entity below)
                            terrain->FindTransforms(tile_index, TerrainProp::Tree, mesh_tree->GetRootEntity(), per_triangle_density_tree, 0.026f, tree_transforms_per_tile[tile_index]);
                            for (math::Matrix& t : tree_transforms_per_tile[tile_index])
                                t *= tile_world_matrix;

                            // rock transforms (deferred to a single entity below)
                            terrain->FindTransforms(tile_index, TerrainProp::Rock, mesh_rock->GetRootEntity(), per_triangle_density_rock, 0.64f, rock_transforms_per_tile[tile_index]);
                            for (math::Matrix& t : rock_transforms_per_tile[tile_index])
                                t *= tile_world_matrix;

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

                                        Render* renderable = entity->AddComponent<Render>();
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

                                        Render* renderable = entity->AddComponent<Render>();
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

                                        Render* renderable = entity->AddComponent<Render>();
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
                                terrain->FindTransforms(tile_index, TerrainProp::Flower, entity, per_triangle_density_flower, 0.64f, transforms);

                                Render* renderable = entity->AddComponent<Render>();
                                renderable->SetMesh(mesh_flower.get());
                                renderable->SetFlag(RenderableFlags::CastsShadows, false);
                                renderable->SetInstances(transforms);
                                renderable->SetMaterial(material_flower);
                                renderable->SetMaxRenderDistance(render_distance_foliage);
                            }
                        }
                    };

                    ThreadPool::ParallelLoop(place_props_on_tiles, static_cast<uint32_t>(children.size()));

                    // single tree entity for the whole world - one cook, one render submission per submesh
                    {
                        size_t tree_total = 0;
                        for (const auto& v : tree_transforms_per_tile) tree_total += v.size();
                        vector<Matrix> all_tree_transforms;
                        all_tree_transforms.reserve(tree_total);
                        for (auto& v : tree_transforms_per_tile)
                            all_tree_transforms.insert(all_tree_transforms.end(), v.begin(), v.end());

                        if (!all_tree_transforms.empty())
                        {
                            Entity* entity = mesh_tree->GetRootEntity()->Clone();
                            entity->SetObjectName("tree");
                            entity->SetParent(default_terrain);

                            if (Entity* trunk = entity->GetChildByIndex(0))
                            {
                                Render* renderable = trunk->GetComponent<Render>();
                                renderable->SetInstances(all_tree_transforms);
                                renderable->SetMaxRenderDistance(render_distance_trees);
                                renderable->SetMaxShadowDistance(shadow_distance);
                                renderable->SetMaterial(material_body);

                                Physics* physics = trunk->AddComponent<Physics>();
                                physics->SetBodyType(BodyType::Mesh);
                            }

                            if (Entity* leafs = entity->GetChildByIndex(1))
                            {
                                Render* renderable = leafs->GetComponent<Render>();
                                renderable->SetInstances(all_tree_transforms);
                                renderable->SetMaxRenderDistance(render_distance_trees);
                                renderable->SetMaxShadowDistance(shadow_distance);
                                renderable->SetMaterial(material_leaf);
                            }
                        }
                    }

                    // single rock entity for the whole world
                    {
                        size_t rock_total = 0;
                        for (const auto& v : rock_transforms_per_tile) rock_total += v.size();
                        vector<Matrix> all_rock_transforms;
                        all_rock_transforms.reserve(rock_total);
                        for (auto& v : rock_transforms_per_tile)
                            all_rock_transforms.insert(all_rock_transforms.end(), v.begin(), v.end());

                        if (!all_rock_transforms.empty())
                        {
                            Entity* entity = mesh_rock->GetRootEntity()->Clone();
                            entity->SetObjectName("rock");
                            entity->SetParent(default_terrain);

                            if (Entity* rock_entity = entity->GetDescendantByName("untitled"))
                            {
                                Render* renderable = rock_entity->GetComponent<Render>();
                                renderable->SetInstances(all_rock_transforms);
                                renderable->SetMaxRenderDistance(render_distance_trees);
                                renderable->SetMaxShadowDistance(shadow_distance);
                                renderable->SetMaterial(material_rock);

                                Physics* physics = rock_entity->AddComponent<Physics>();
                                physics->SetBodyType(BodyType::Mesh);
                            }
                        }
                    }
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

        //= LIGHT TEST =======================================================================
        namespace light_test
        {
            void create()
            {
                entities::camera(false, Vector3(0.0f, 1.2f, -8.0f), Vector3(0.0f, 0.0f, 0.0f));
                entities::floor();
                entities::sun(LightPreset::dusk, true);

                // material test sphere
                entities::material_ball(Vector3(-3.0f, 0.0f, 0.0f));

                // cornell box
                {
                    const float room_scale = 2.0f;

                    // bring the sun below the horizon so the scene is night-lit by the emissive panel
                    default_light_directional->SetRotation(Quaternion::FromEulerAngles(-30.0f, 0.0f, 0.0f));

                    uint32_t mesh_flags  = Mesh::GetDefaultFlags();
                    mesh_flags          &= ~static_cast<uint32_t>(MeshFlags::ImportGenerateSmoothNormals);

                    // preserve the cornell box hard edges so the cubes don't get smoothed shading.
                    if (shared_ptr<Mesh> mesh = ResourceCache::Load<Mesh>("project/models/CornellBox/CornellBox-Original.obj", mesh_flags))
                    {
                        Entity* entity = mesh->GetRootEntity();
                        entity->SetObjectName("cornell_box");
                        entity->SetPosition(Vector3(3.0f, 0.2f, 0.0f));
                        entity->SetScale(room_scale);

                        // make the ceiling panel emissive so it lights the scene via path tracing
                        if (Entity* light_entity = entity->GetDescendantByName("light"))
                        {
                            if (Render* renderable = light_entity->GetComponent<Render>())
                            {
                                if (Material* material = renderable->GetMaterial())
                                {
                                    material->SetProperty(MaterialProperty::EmissiveFromAlbedo, 1.0f);
                                }
                            }
                        }

                        // physics for all meshes
                        vector<Entity*> entities;
                        entity->GetDescendants(&entities);
                        for (Entity* entity_it : entities)
                        {
                            if (entity_it->GetComponent<Render>() != nullptr)
                            {
                                Physics* physics_body = entity_it->AddComponent<Physics>();
                                physics_body->SetBodyType(BodyType::Mesh);
                            }
                        }
                    }
                }
            }
        }
        //====================================================================================

        //= EMPTY ============================================================================
        namespace empty
        {
            void create()
            {
                entities::camera(false);
                entities::floor();
                entities::sun(LightPreset::dusk, true);
            }
        }
        //====================================================================================

    }
    //========================================================================================

    //= PUBLIC API ===========================================================================
    void Game::Shutdown()
    {
        // reset shared entities
        default_floor             = nullptr;
        default_camera            = nullptr;
        default_environment       = nullptr;
        default_light_directional = nullptr;
        default_terrain           = nullptr;
        default_car               = nullptr;
        default_metal_cube        = nullptr;

        // reset world-specific state
        Car::ShutdownAll();
        meshes.clear();
    }

    void Game::Tick()
    {
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

            // entities created by this function are auto-drained by World::Tick each frame, the renderer skips entities whose components are still being set up
            world_create[static_cast<size_t>(default_world)]();

            ProgressTracker::SetGlobalLoadingState(false);
        });

        loaded_world = default_world;
    }

    DefaultWorld Game::GetLoadedWorld()
    {
        return loaded_world;
    }

    void Game::RegisterPrefabs()
    {
        if (prefabs_registered)
            return;

        Prefab::Register("car", Car::CreatePrefab);
        prefabs_registered = true;
    }
    //========================================================================================
}
