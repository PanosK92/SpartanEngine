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
#include "../Logging/Log.h"
//==========================================

//= NAMESPACES ===============
using namespace std;
using namespace spartan::math;
//============================

namespace spartan
{
    //= FORWARD DECLARATIONS (world functions) ==================
    namespace worlds
    {
        namespace showroom        { void create(); void tick(); }
        namespace car_playground  { void create(); }
        namespace forest          { void create(); void tick(); }
        namespace liminal_space   { void create(); void tick(); }
        namespace sponza          { void create(); }
        namespace subway          { void create(); }
        namespace minecraft       { void create(); }
        namespace basic           { void create(); }
    }
    //===========================================================

    namespace
    {
        //= STATE ====================================
        DefaultWorld loaded_world = DefaultWorld::Max;
        //============================================

        //= SHARED ENTITIES ========================
        Entity* default_floor             = nullptr;
        Entity* default_terrain           = nullptr;
        Entity* default_car               = nullptr;
        Entity* default_car_window        = nullptr;
        Entity* default_camera            = nullptr;
        Entity* default_environment       = nullptr;
        Entity* default_light_directional = nullptr;
        Entity* default_metal_cube        = nullptr;
        Entity* default_ocean             = nullptr;
        vector<shared_ptr<Mesh>> meshes;
        //==========================================

        //= WORLD DISPATCH TABLES =====================================================================================================
        using create_fn = void(*)();
        using tick_fn   = void(*)();

        // indexed by DefaultWorld enum - add new worlds here
        constexpr create_fn world_create[] =
        {
            worlds::showroom::create,
            worlds::car_playground::create,
            worlds::forest::create,
            worlds::liminal_space::create,
            worlds::sponza::create,
            worlds::subway::create,
            worlds::minecraft::create,
            worlds::basic::create,
        };

        constexpr tick_fn world_tick[] =
        {
            worlds::showroom::tick,
            nullptr,
            worlds::forest::tick,
            worlds::liminal_space::tick,
            nullptr,
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

            Entity* ocean(std::shared_ptr<Material> material, const Vector3& position, float tile_size, uint32_t density, uint32_t grid_size)
            {
                // entity
                Entity* water = World::CreateEntity();
                water->SetObjectName("ocean");
                water->SetPosition(position);
                water->SetScale({ 1.0f, 1.0f, 1.0f });

                // material
                {
                    material->SetObjectName("material_ocean");
                    material->SetResourceFilePath("ocean" + string(EXTENSION_MATERIAL));

                    material->LoadFromFile(material->GetResourceFilePath());
                    material->SetOceanTileCount(grid_size);

                    material->SetOceanTileSize(tile_size);
                    material->SetOceanVerticesCount(density);
                    material->MarkSpectrumAsComputed(false);
                    material->SetTexture(MaterialTextureType::Flowmap, "project\\materials\\water\\flowmap.png");

                    // if material fails to load from file
                    if (material->GetProperty(MaterialProperty::IsOcean) != 1.0f)
                    {
                        material->SetColor(Color(0.0f, 142.0f / 255.0f, 229.0f / 255.0f, 254.0f / 255.0f)); 
                        material->SetProperty(MaterialProperty::IsOcean, 1.0f);

                        material->SetOceanProperty(OceanParameters::Angle, 0.0f); //handled internally
                        material->SetOceanProperty(OceanParameters::Alpha, 0.0f); // handled internally
                        material->SetOceanProperty(OceanParameters::PeakOmega, 0.0f); // handled internally

                        material->SetOceanProperty(OceanParameters::Scale, 1.0f);
                        material->SetOceanProperty(OceanParameters::SpreadBlend, 0.9f);
                        material->SetOceanProperty(OceanParameters::Swell, 1.0f);
                        material->SetOceanProperty(OceanParameters::Fetch, 1280000.0f);
                        material->SetOceanProperty(OceanParameters::WindDirection, 135.0f);
                        material->SetOceanProperty(OceanParameters::WindSpeed, 2.8f);
                        material->SetOceanProperty(OceanParameters::Gamma, 3.3f);
                        material->SetOceanProperty(OceanParameters::ShortWavesFade, 0.0f);
                        material->SetOceanProperty(OceanParameters::RepeatTime, 200.0f);

                        material->SetOceanProperty(OceanParameters::Depth, 20.0f);
                        material->SetOceanProperty(OceanParameters::LowCutoff, 0.001f);
                        material->SetOceanProperty(OceanParameters::HighCutoff, 1000.0f);

                        material->SetOceanProperty(OceanParameters::FoamDecayRate, 3.0f);
                        material->SetOceanProperty(OceanParameters::FoamThreshold, 0.5f);
                        material->SetOceanProperty(OceanParameters::FoamBias, 1.2f);
                        material->SetOceanProperty(OceanParameters::FoamAdd, 1.0f);

                        material->SetOceanProperty(OceanParameters::DisplacementScale, 1.0f);
                        material->SetOceanProperty(OceanParameters::SlopeScale, 1.0f);
                        material->SetOceanProperty(OceanParameters::LengthScale, 128.0f);
                    }
                }

                // geometry
                {
                    // generate grid
                    const uint32_t grid_points_per_dimension = density;
                    vector<RHI_Vertex_PosTexNorTan> vertices;
                    vector<uint32_t> indices;
                    geometry_generation::generate_grid(&vertices, &indices, grid_points_per_dimension, tile_size);

                    string name = "ocean mesh";

                    // create mesh if it doesn't exist
                    shared_ptr<Mesh> mesh = meshes.emplace_back(make_shared<Mesh>());
                    mesh->SetObjectName(name);
                    mesh->SetRootEntity(water);
                    mesh->SetFlag(static_cast<uint32_t>(MeshFlags::PostProcessOptimize), false);
                    mesh->SetFlag(static_cast<uint32_t>(MeshFlags::PostProcessNormalizeScale), false);
                    mesh->AddGeometry(vertices, indices, false);
                    mesh->CreateGpuBuffers();

                    // create a child entity, add a renderable, and this mesh tile to it
                    for (uint32_t row = 0; row < grid_size; row++)
                    {
                        for (uint32_t col = 0; col < grid_size; col++)
                        {
                            int tile_index = col + row * grid_size;

                            string tile_name = "ocean tile_" + to_string(tile_index);

                            Entity* entity_tile = World::CreateEntity();
                            entity_tile->SetObjectName(tile_name);
                            entity_tile->SetParent(water);

                            Vector3 tile_position = { col * tile_size, 0.0f, row * tile_size };
                            entity_tile->SetPosition(tile_position);

                            if (Renderable* renderable = entity_tile->AddComponent<Renderable>())
                            {
                                renderable->SetMesh(mesh.get());
                                renderable->SetMaterial(material);
                                renderable->SetFlag(RenderableFlags::CastsShadows, false);
                            }

                            // enable buoyancy
                            //Physics* physics = entity_tile->AddComponent<Physics>();
                            //physics->SetBodyType(BodyType::Water);
                        }
                    }
                }

                return water;
            }
        }

        // reset renderer options to defaults
        void set_base_renderer_options()
        {
            ConsoleRegistry::Get().SetValueFromString("r.dithering",            "0");
            ConsoleRegistry::Get().SetValueFromString("r.chromatic_aberration", "0");
            ConsoleRegistry::Get().SetValueFromString("r.grid",                 "0");
            ConsoleRegistry::Get().SetValueFromString("r.vhs",                  "0");
        }
    }

    //= CAR ==================================================================================
    namespace car
    {
        // configuration for car creation
        struct Config
        {
            Vector3 position        = Vector3::Zero;
            bool    drivable        = false;  // creates vehicle physics with wheels
            bool    static_physics  = false;  // kinematic physics on the body (for display)
            bool    show_telemetry  = false;  // shows vehicle telemetry hud
            bool    camera_follows  = false;  // attach camera to follow the car
        };

        // state for drivable cars
        Entity* vehicle_entity = nullptr;
        bool    show_telemetry = false;

        // chase camera state - gran turismo 7 style
        namespace chase_camera
        {
            Vector3 position     = Vector3::Zero;  // smoothed camera world position
            Vector3 velocity     = Vector3::Zero;  // velocity for smooth damping
            float   yaw          = 0.0f;           // smoothed yaw angle (radians)
            float   yaw_bias     = 0.0f;           // manual horizontal camera rotation from right stick (radians)
            float   pitch_bias   = 0.0f;           // manual vertical camera rotation from right stick (radians)
            float   speed_factor = 0.0f;           // smoothed speed factor for dynamic adjustments
            bool    initialized  = false;          // first frame initialization flag
            
            // base tuning parameters
            constexpr float distance_base      = 5.0f;   // base distance behind the car
            constexpr float distance_min       = 4.0f;   // minimum distance at high speed (camera pulls in)
            constexpr float height_base        = 1.5f;   // base height above the car
            constexpr float height_min         = 1.2f;   // minimum height at high speed (camera drops)
            constexpr float position_smoothing = 0.15f;  // position smooth time (lower = faster, snappier)
            constexpr float rotation_smoothing = 4.0f;   // rotation catch-up speed (higher = faster)
            constexpr float speed_smoothing    = 2.0f;   // how fast speed factor changes
            constexpr float look_offset_up     = 0.6f;   // look slightly above car center
            constexpr float look_ahead_amount  = 2.5f;   // how far ahead to look based on velocity
            constexpr float speed_reference    = 50.0f;  // speed (m/s) at which effects are maxed (~180 km/h)
            
            // right stick orbit parameters  
            constexpr float orbit_bias_speed   = 1.5f;   // how fast the right stick rotates the camera (radians/sec)
            constexpr float orbit_bias_decay   = 4.0f;   // how fast the camera returns to center when stick released
            constexpr float yaw_bias_max       = math::pi; // maximum yaw angle (180 degrees, can look behind)
            constexpr float pitch_bias_max     = 1.2f;   // maximum pitch angle (~70 degrees)

            // smooth damp - critically damped spring for smooth following
            Vector3 smooth_damp(const Vector3& current, const Vector3& target, Vector3& velocity, float smooth_time, float dt)
            {
                float omega = 2.0f / std::max(smooth_time, 0.0001f);
                float x = omega * dt;
                float exp_factor = 1.0f / (1.0f + x + 0.48f * x * x + 0.235f * x * x * x);
                Vector3 delta = current - target;
                Vector3 temp = (velocity + omega * delta) * dt;
                velocity = (velocity - omega * temp) * exp_factor;
                return target + (delta + temp) * exp_factor;
            }

            float lerp_angle(float a, float b, float t)
            {
                // handle wrap-around for angles
                float diff = fmodf(b - a + math::pi * 3.0f, math::pi * 2.0f) - math::pi;
                return a + diff * t;
            }
        }

        // track whether player is currently operating the car (independent of camera parenting)
        bool is_in_vehicle = false;

        // spawn position for reset functionality
        Vector3 spawn_position = Vector3::Zero;

        // helper: loads car body mesh with material tweaks
        // out_excluded_entities: if remove_wheels is true, returns entities that were disabled (for collision exclusion)
        Entity* create_body(bool remove_wheels, vector<Entity*>* out_excluded_entities = nullptr)
        {
            uint32_t mesh_flags  = Mesh::GetDefaultFlags();
            mesh_flags          &= ~static_cast<uint32_t>(MeshFlags::PostProcessOptimize);
            mesh_flags          &= ~static_cast<uint32_t>(MeshFlags::PostProcessGenerateLods);

            shared_ptr<Mesh> mesh_car = ResourceCache::Load<Mesh>("project\\models\\ferrari_laferrari\\scene.gltf", mesh_flags);
            if (!mesh_car)
                return nullptr;

            Entity* car_entity = mesh_car->GetRootEntity();
            car_entity->SetObjectName("ferrari_laferrari");
            car_entity->SetScale(2.0f);

            if (remove_wheels)
            {
                auto to_lower = [](string s)
                {
                    transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return tolower(c); });
                    return s;
                };

                vector<Entity*> descendants;
                car_entity->GetDescendants(&descendants);

                for (Entity* descendant : descendants)
                {
                     string entity_name = to_lower(descendant->GetObjectName());
            
                     if (entity_name.find("tire 1")    != string::npos ||
                         entity_name.find("tire 2")    != string::npos ||
                         entity_name.find("tire 3")    != string::npos ||
                         entity_name.find("tire 4")    != string::npos ||
                         entity_name.find("brakerear") != string::npos) // all four have this prefix
                     {
                         descendant->SetActive(false);
                         
                         // collect excluded entities for collision shape building
                         if (out_excluded_entities)
                         {
                             out_excluded_entities->push_back(descendant);
                         }
                     }
                }
            }
            
            // material tweaks
            {
                // body main - red clearcoat paint
                if (Material* material = car_entity->GetDescendantByName("Object_12")->GetComponent<Renderable>()->GetMaterial())
                {
                    material->SetResourceName("car_paint" + string(EXTENSION_MATERIAL));
                    material->SetProperty(MaterialProperty::Roughness, 0.0f);
                    material->SetProperty(MaterialProperty::Clearcoat, 1.0f);
                    material->SetProperty(MaterialProperty::Clearcoat_Roughness, 0.1f);
                    material->SetColor(Color(100.0f / 255.0f, 0.0f, 0.0f, 1.0f));
                    material->SetProperty(MaterialProperty::Normal, 0.03f);
                    material->SetProperty(MaterialProperty::TextureTilingX, 100.0f);
                    material->SetProperty(MaterialProperty::TextureTilingY, 100.0f);
                    //material->SetTexture(MaterialTextureType::Normal, "project\\models\\ferrari_laferrari\\paint_normal.png"); fix: it doesn't tile wile
                }

                // body metallic/carbon parts
                if (Material* material = car_entity->GetDescendantByName("Object_10")->GetComponent<Renderable>()->GetMaterial())
                {
                    material->SetProperty(MaterialProperty::Roughness, 0.4f);
                    material->SetProperty(MaterialProperty::Metalness, 1.0f);
                }

                // tires - rubber
                {
                    const char* tire_parts[] = {"Object_127", "Object_142", "Object_157", "Object_172"};
                    for (const char* part : tire_parts)
                    {
                        if (Material* material = car_entity->GetDescendantByName(part)->GetComponent<Renderable>()->GetMaterial())
                        {
                            material->SetProperty(MaterialProperty::Roughness, 0.7f);
                        }
                    }
                }

                // rims - polished metal
                if (Material* material = car_entity->GetDescendantByName("Object_180")->GetComponent<Renderable>()->GetMaterial())
                {
                    material->SetProperty(MaterialProperty::Metalness, 1.0f);
                    material->SetProperty(MaterialProperty::Roughness, 0.3f);
                }
                if (Material* material = car_entity->GetDescendantByName("Object_150")->GetComponent<Renderable>()->GetMaterial())
                {
                    material->SetProperty(MaterialProperty::Metalness, 1.0f);
                    material->SetProperty(MaterialProperty::Roughness, 0.3f);
                }

                // headlight and taillight glass
                if (Material* material = car_entity->GetDescendantByName("Object_38")->GetComponent<Renderable>()->GetMaterial())
                {
                    material->SetProperty(MaterialProperty::Roughness, 0.5f);
                    material->SetProperty(MaterialProperty::Metalness, 1.0f);
                }

                // windshield and engine glass
                if (Material* material = car_entity->GetDescendantByName("Object_58")->GetComponent<Renderable>()->GetMaterial())
                {
                    material->SetProperty(MaterialProperty::Roughness, 0.0f);
                    material->SetProperty(MaterialProperty::Metalness, 0.0f);
                }

                // side mirror glass
                if (Material* material = car_entity->GetDescendantByName("Object_98")->GetComponent<Renderable>()->GetMaterial())
                {
                    material->SetProperty(MaterialProperty::Roughness, 0.0f);
                    material->SetProperty(MaterialProperty::Metalness, 1.0f);
                }

                // engine block
                if (Material* material = car_entity->GetDescendantByName("Object_14")->GetComponent<Renderable>()->GetMaterial())
                {
                    material->SetProperty(MaterialProperty::Roughness, 0.4f);
                    material->SetProperty(MaterialProperty::Metalness, 1.0f);
                }

                // brake discs - anisotropic metal
                {
                    const char* brake_parts[] = {"Object_129", "Object_144", "Object_174", "Object_159"};
                    for (const char* part : brake_parts)
                    {
                        if (Material* material = car_entity->GetDescendantByName(part)->GetComponent<Renderable>()->GetMaterial())
                        {
                            material->SetProperty(MaterialProperty::Metalness, 1.0f);
                            material->SetProperty(MaterialProperty::Anisotropic, 1.0f);
                            material->SetProperty(MaterialProperty::AnisotropicRotation, 0.2f);
                        }
                    }
                }

                // interior leather
                if (Material* material = car_entity->GetDescendantByName("Object_90")->GetComponent<Renderable>()->GetMaterial())
                {
                    material->SetProperty(MaterialProperty::Roughness, 0.75f);
                }
            }

            return car_entity;
        }

        // helper: adds audio sources to car
        void add_audio_sources(Entity* car_entity)
        {
            // engine start
            {
                Entity* sound = World::CreateEntity();
                sound->SetObjectName("sound_start");
                sound->SetParent(car_entity);

                AudioSource* audio_source = sound->AddComponent<AudioSource>();
                audio_source->SetAudioClip("project\\music\\car_start.wav");
                audio_source->SetLoop(false);
                audio_source->SetPlayOnStart(false);
            }

            // engine idle
            {
                Entity* sound = World::CreateEntity();
                sound->SetObjectName("sound_idle");
                sound->SetParent(car_entity);

                AudioSource* audio_source = sound->AddComponent<AudioSource>();
                audio_source->SetAudioClip("project\\music\\car_idle.wav");
                audio_source->SetLoop(true);
                audio_source->SetPlayOnStart(false);
            }

            // door open/close
            {
                Entity* sound = World::CreateEntity();
                sound->SetObjectName("sound_door");
                sound->SetParent(car_entity);

                AudioSource* audio_source = sound->AddComponent<AudioSource>();
                audio_source->SetAudioClip("project\\music\\car_door.wav");
                audio_source->SetLoop(false);
                audio_source->SetPlayOnStart(false);
            }
        }

        // helper: creates wheels and attaches to vehicle
        void create_wheels(Entity* vehicle_ent, Physics* physics)
        {
            uint32_t mesh_flags  = Mesh::GetDefaultFlags();
            mesh_flags          &= ~static_cast<uint32_t>(MeshFlags::PostProcessOptimize);
            mesh_flags          &= ~static_cast<uint32_t>(MeshFlags::PostProcessGenerateLods);

            shared_ptr<Mesh> mesh = ResourceCache::Load<Mesh>("project\\models\\wheel\\model.blend", mesh_flags);
            if (!mesh)
                return;

            Entity* wheel_root = mesh->GetRootEntity();
            Entity* wheel_base = wheel_root->GetChildByIndex(0);
            if (!wheel_base)
                return;

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
            // physics wheel shapes are at y = -suspension_height relative to body center
            // the visual wheel mesh has its origin at the center of the rim, matching the physics shape center
            const float suspension_height = physics->GetSuspensionHeight();
            const float wheel_x           = 0.95f;
            const float wheel_y           = -suspension_height;
            const float front_z           = 1.45f;
            const float rear_z            = -1.35f;

            // front left wheel (use the base)
            Entity* wheel_fl = wheel_base;
            wheel_fl->SetObjectName("wheel_front_left");
            wheel_fl->SetParent(vehicle_ent);
            wheel_fl->SetPositionLocal(Vector3(-wheel_x, wheel_y, front_z));

            // front right wheel (clone and mirror)
            Entity* wheel_fr = wheel_base->Clone();
            wheel_fr->SetObjectName("wheel_front_right");
            wheel_fr->SetParent(vehicle_ent);
            wheel_fr->SetPositionLocal(Vector3(wheel_x, wheel_y, front_z));
            wheel_fr->SetRotationLocal(Quaternion::FromAxisAngle(Vector3::Up, math::pi));

            // rear left wheel (clone)
            Entity* wheel_rl = wheel_base->Clone();
            wheel_rl->SetObjectName("wheel_rear_left");
            wheel_rl->SetParent(vehicle_ent);
            wheel_rl->SetPositionLocal(Vector3(-wheel_x, wheel_y, rear_z));

            // rear right wheel (clone and mirror)
            Entity* wheel_rr = wheel_base->Clone();
            wheel_rr->SetObjectName("wheel_rear_right");
            wheel_rr->SetParent(vehicle_ent);
            wheel_rr->SetPositionLocal(Vector3(wheel_x, wheel_y, rear_z));
            wheel_rr->SetRotationLocal(Quaternion::FromAxisAngle(Vector3::Up, math::pi));

            // hook up wheel entities to the physics component
            physics->SetWheelEntity(WheelIndex::FrontLeft,  wheel_fl);
            physics->SetWheelEntity(WheelIndex::FrontRight, wheel_fr);
            physics->SetWheelEntity(WheelIndex::RearLeft,   wheel_rl);
            physics->SetWheelEntity(WheelIndex::RearRight,  wheel_rr);
        }

        // main car creation function - returns the root entity (vehicle_entity if drivable, car body otherwise)
        Entity* create(const Config& config)
        {
            show_telemetry = config.show_telemetry;
            spawn_position = config.position;

            if (config.drivable)
            {
                // create vehicle entity with physics
                vehicle_entity = World::CreateEntity();
                vehicle_entity->SetObjectName("vehicle");
                vehicle_entity->SetPosition(config.position);

                Physics* physics = vehicle_entity->AddComponent<Physics>();
                physics->SetStatic(false);
                physics->SetMass(1500.0f);
                physics->SetBodyType(BodyType::Vehicle);

                // create car body (without its original wheels)
                // collect excluded wheel entities for collision shape building
                vector<Entity*> excluded_wheel_entities;
                default_car = create_body(true, &excluded_wheel_entities);
                if (default_car)
                {
                    // the wheel distances are based on laferrari dimensions
                    // if you scale the body by 1.1, it seems to match them
                    // same goes for the 0.07f z offset
                    default_car->SetParent(vehicle_entity);
                    default_car->SetPositionLocal(Vector3(0.0f, ::car::get_chassis_visual_offset_y(), 0.07f));
                    default_car->SetRotationLocal(Quaternion::FromAxisAngle(Vector3::Right, math::pi * 0.5f));
                    default_car->SetScaleLocal(1.1f);

                    // hook up chassis entity (the ferrari body that bounces on the suspension)
                    // pass excluded wheel entities so they're not included in the collision shape
                    physics->SetChassisEntity(default_car, excluded_wheel_entities);
                }

                add_audio_sources(vehicle_entity);
                create_wheels(vehicle_entity, physics);

                // setup camera to follow if requested
                if (config.camera_follows && default_camera)
                {
                    if (Camera* camera = default_camera->GetChildByIndex(0)->GetComponent<Camera>())
                    {
                        camera->SetFlag(CameraFlags::CanBeControlled, false);
                    }

                    // start already inside the car (default chase view)
                    is_in_vehicle             = true;
                    chase_camera::initialized = false;
                }

                return vehicle_entity;
            }
            else
            {
                // non-drivable display car
                default_car = create_body(false);
                if (default_car)
                {
                    default_car->SetPosition(config.position);

                    // add kinematic physics if requested
                    if (config.static_physics)
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

                add_audio_sources(default_car);
                return default_car;
            }
        }

        // helper: draws vehicle telemetry hud
        void draw_telemetry()
        {
            if (!vehicle_entity)
                return;

            Physics* physics = vehicle_entity->GetComponent<Physics>();
            if (!physics)
                return;

            static char text_buffer[256];
            Vector3 velocity = physics->GetLinearVelocity();
            float speed_kmh  = velocity.Length() * 3.6f;
            
            const float line_spacing = 0.018f;
            const float left_x       = 0.005f;
            const float right_x      = 0.75f;
            const char* wheel_names[] = { "FL", "FR", "RL", "RR" };
            
            // draw debug visualization
            physics->DrawDebugVisualization();
            
            // ============================================
            // right side - traditional dashboard
            // ============================================
            float y_right = 0.70f;
            
            // speed (large, prominent)
            snprintf(text_buffer, sizeof(text_buffer), "%.0f km/h", speed_kmh);
            Renderer::DrawString(text_buffer, Vector2(right_x, y_right));
            y_right += line_spacing * 1.5f;
            
            // gear and rpm
            float engine_rpm = physics->GetEngineRPM();
            float redline    = physics->GetRedlineRPM();
            const char* gear_str = physics->GetCurrentGearString();
            bool is_shifting = physics->IsShifting();
            snprintf(text_buffer, sizeof(text_buffer), "Gear: %s%s  RPM: %.0f/%.0f", 
                gear_str, is_shifting ? "*" : "", engine_rpm, redline);
            Renderer::DrawString(text_buffer, Vector2(right_x, y_right));
            y_right += line_spacing;
            
            // throttle/brake bars
            int throttle_bar = static_cast<int>(physics->GetVehicleThrottle() * 10.0f);
            int brake_bar    = static_cast<int>(physics->GetVehicleBrake() * 10.0f);
            char thr_bar[16], brk_bar[16];
            for (int j = 0; j < 10; j++) { thr_bar[j] = (j < throttle_bar) ? '=' : '.'; }
            for (int j = 0; j < 10; j++) { brk_bar[j] = (j < brake_bar) ? '=' : '.'; }
            thr_bar[10] = brk_bar[10] = '\0';
            snprintf(text_buffer, sizeof(text_buffer), "THR [%s]  BRK [%s]", thr_bar, brk_bar);
            Renderer::DrawString(text_buffer, Vector2(right_x, y_right));
            y_right += line_spacing;
            
            // steering indicator
            float steer = physics->GetVehicleSteering();
            char steer_bar[21];
            for (int j = 0; j < 20; j++) steer_bar[j] = '.';
            steer_bar[10] = '|'; // center
            int steer_pos = 10 + static_cast<int>(steer * 9.0f);
            steer_pos = steer_pos < 0 ? 0 : (steer_pos > 19 ? 19 : steer_pos);
            steer_bar[steer_pos] = 'O';
            steer_bar[20] = '\0';
            snprintf(text_buffer, sizeof(text_buffer), "STR [%s]", steer_bar);
            Renderer::DrawString(text_buffer, Vector2(right_x, y_right));
            y_right += line_spacing * 1.2f;
            
            // assists status (compact)
            bool abs_active = physics->IsAbsActiveAny();
            bool tc_active  = physics->IsTcActive();
            snprintf(text_buffer, sizeof(text_buffer), "ABS:%s%s TC:%s%s %s",
                physics->GetAbsEnabled() ? "ON" : "--",
                abs_active ? "!" : "",
                physics->GetTcEnabled() ? "ON" : "--",
                tc_active ? "!" : "",
                physics->GetManualTransmission() ? "MT" : "AT");
            Renderer::DrawString(text_buffer, Vector2(right_x, y_right));
            y_right += line_spacing;
            
            // handbrake
            if (physics->GetVehicleHandbrake() > 0.1f)
            {
                Renderer::DrawString("[ HANDBRAKE ]", Vector2(right_x, y_right));
            }
            
            // ============================================
            // left side - technical telemetry
            // ============================================
            float y_left = 0.58f;
            
            Renderer::DrawString("Tire Physics", Vector2(left_x, y_left));
            y_left += line_spacing;
            
            // compact per-wheel data
            for (int i = 0; i < static_cast<int>(WheelIndex::Count); i++)
            {
                WheelIndex wheel = static_cast<WheelIndex>(i);
                bool grounded       = physics->IsWheelGrounded(wheel);
                float slip_angle    = physics->GetWheelSlipAngle(wheel) * 57.2958f;
                float slip_ratio    = physics->GetWheelSlipRatio(wheel) * 100.0f;
                float lat_force_kn  = physics->GetWheelLateralForce(wheel) / 1000.0f;
                float long_force_kn = physics->GetWheelLongitudinalForce(wheel) / 1000.0f;
                
                snprintf(text_buffer, sizeof(text_buffer), "%s %s SA:%+5.1f SR:%+5.1f Lat:%+4.1f Lon:%+4.1f",
                    wheel_names[i],
                    grounded ? "G" : "-",
                    slip_angle, slip_ratio, lat_force_kn, long_force_kn);
                Renderer::DrawString(text_buffer, Vector2(left_x, y_left));
                y_left += line_spacing;
            }
            
            // temperature section
            y_left += line_spacing * 0.3f;
            Renderer::DrawString("Temperature", Vector2(left_x, y_left));
            y_left += line_spacing;
            
            for (int i = 0; i < static_cast<int>(WheelIndex::Count); i++)
            {
                WheelIndex wheel = static_cast<WheelIndex>(i);
                float temp             = physics->GetWheelTemperature(wheel);
                float grip_factor      = physics->GetWheelTempGripFactor(wheel);
                float brake_temp       = physics->GetWheelBrakeTemp(wheel);
                float brake_efficiency = physics->GetWheelBrakeEfficiency(wheel);
                
                // compact tire temp bar (10 chars)
                int tire_bar_len = static_cast<int>((temp / 150.0f) * 10.0f);
                tire_bar_len = tire_bar_len > 10 ? 10 : (tire_bar_len < 0 ? 0 : tire_bar_len);
                char tire_bar[16];
                for (int j = 0; j < 10; j++)
                    tire_bar[j] = (j < tire_bar_len) ? ((j < 4) ? '-' : ((j < 8) ? '=' : '+')) : '.';
                tire_bar[10] = '\0';
                
                // compact brake temp bar (6 chars)
                int brk_bar_len = static_cast<int>((brake_temp / 900.0f) * 6.0f);
                brk_bar_len = brk_bar_len > 6 ? 6 : (brk_bar_len < 0 ? 0 : brk_bar_len);
                char brk_bar[8];
                for (int j = 0; j < 6; j++)
                    brk_bar[j] = (j < brk_bar_len) ? ((j < 3) ? '-' : ((j < 5) ? '=' : '!')) : '.';
                brk_bar[6] = '\0';
                
                snprintf(text_buffer, sizeof(text_buffer), "%s T[%s]%.0f%% B[%s]%.0f%%",
                    wheel_names[i], tire_bar, grip_factor * 100.0f, brk_bar, brake_efficiency * 100.0f);
                Renderer::DrawString(text_buffer, Vector2(left_x, y_left));
                y_left += line_spacing;
            }
            
            // suspension section
            y_left += line_spacing * 0.3f;
            Renderer::DrawString("Suspension", Vector2(left_x, y_left));
            y_left += line_spacing;
            
            // show front pair and rear pair on same lines
            for (int pair = 0; pair < 2; pair++)
            {
                int left_wheel  = pair * 2;
                int right_wheel = pair * 2 + 1;
                float comp_l = physics->GetWheelCompression(static_cast<WheelIndex>(left_wheel));
                float comp_r = physics->GetWheelCompression(static_cast<WheelIndex>(right_wheel));
                
                // bars (8 chars each)
                char bar_l[12], bar_r[12];
                int len_l = static_cast<int>((1.0f - comp_l) * 8.0f);
                int len_r = static_cast<int>((1.0f - comp_r) * 8.0f);
                for (int j = 0; j < 8; j++) { bar_l[j] = (j < len_l) ? '|' : '.'; bar_r[j] = (j < len_r) ? '|' : '.'; }
                bar_l[8] = bar_r[8] = '\0';
                
                snprintf(text_buffer, sizeof(text_buffer), "%s[%s]%2.0f%%  %s[%s]%2.0f%%",
                    wheel_names[left_wheel], bar_l, comp_l * 100.0f,
                    wheel_names[right_wheel], bar_r, comp_r * 100.0f);
                Renderer::DrawString(text_buffer, Vector2(left_x, y_left));
                y_left += line_spacing;
            }
            
            // debug toggles (compact)
            y_left += line_spacing * 0.3f;
            snprintf(text_buffer, sizeof(text_buffer), "Debug: Rays[%s] Susp[%s]",
                physics->GetDrawRaycasts() ? "X" : "-",
                physics->GetDrawSuspension() ? "X" : "-");
            Renderer::DrawString(text_buffer, Vector2(left_x, y_left));
        }

        void tick()
        {
            if (!default_car)
                return;

            // handle drivable car input
            if (vehicle_entity)
            {
                Physics* physics = vehicle_entity->GetComponent<Physics>();
                if (physics && Engine::IsFlagSet(EngineMode::Playing))
                {
                    // input mapping - keyboard and gamepad combined into analog values
                    bool is_gamepad_connected = Input::IsGamepadConnected();

                    // throttle: right trigger (analog) or arrow up (binary)
                    float throttle = 0.0f;
                    if (is_gamepad_connected)
                    {
                        throttle = Input::GetGamepadTriggerRight();
                    }
                    if (Input::GetKey(KeyCode::Arrow_Up))
                    {
                        throttle = 1.0f;
                    }

                    // brake: left trigger (analog) or arrow down (binary)
                    float brake = 0.0f;
                    if (is_gamepad_connected)
                    {
                        brake = Input::GetGamepadTriggerLeft();
                    }
                    if (Input::GetKey(KeyCode::Arrow_Down))
                    {
                        brake = 1.0f;
                    }

                    // steering: left stick x-axis (analog) or arrow keys (binary)
                    float steering = 0.0f;
                    if (is_gamepad_connected)
                    {
                        steering = Input::GetGamepadThumbStickLeft().x;
                    }
                    if (Input::GetKey(KeyCode::Arrow_Left))
                    {
                        steering = -1.0f;
                    }
                    if (Input::GetKey(KeyCode::Arrow_Right))
                    {
                        steering = 1.0f;
                    }

                    // handbrake: space or button south (A on Xbox, X on PlayStation)
                    float handbrake = (Input::GetKey(KeyCode::Space) || Input::GetKey(KeyCode::Button_South)) ? 1.0f : 0.0f;

                    // apply vehicle controls
                    physics->SetVehicleThrottle(throttle);
                    physics->SetVehicleBrake(brake);
                    physics->SetVehicleSteering(steering);
                    physics->SetVehicleHandbrake(handbrake);

                    // camera orbit: right stick rotates camera around the car (horizontal and vertical)
                    float dt = static_cast<float>(Timer::GetDeltaTimeSec());
                    if (is_gamepad_connected)
                    {
                        Vector2 right_stick = Input::GetGamepadThumbStickRight();

                        // horizontal (yaw) - three zones:
                        // - active (> 0.3): orbit the camera
                        // - hold (0.1 - 0.3): camera stays in place (small stick offset to lock view)
                        // - release (< 0.1): camera reverts back behind the car
                        float stick_x = fabsf(right_stick.x);
                        if (stick_x > 0.3f)
                        {
                            chase_camera::yaw_bias += right_stick.x * chase_camera::orbit_bias_speed * dt;
                            chase_camera::yaw_bias  = std::clamp(chase_camera::yaw_bias, -chase_camera::yaw_bias_max, chase_camera::yaw_bias_max);
                        }
                        else if (stick_x < 0.1f && fabsf(chase_camera::yaw_bias) > 0.01f)
                        {
                            chase_camera::yaw_bias *= expf(-chase_camera::orbit_bias_decay * dt);
                        }
                        // hold zone (0.1 - 0.3): do nothing, camera stays where it is

                        // vertical (pitch) - same three zones as horizontal
                        float stick_y = fabsf(right_stick.y);
                        if (stick_y > 0.3f)
                        {
                            chase_camera::pitch_bias += right_stick.y * chase_camera::orbit_bias_speed * dt;
                            chase_camera::pitch_bias  = std::clamp(chase_camera::pitch_bias, -chase_camera::pitch_bias_max, chase_camera::pitch_bias_max);
                        }
                        else if (stick_y < 0.1f && fabsf(chase_camera::pitch_bias) > 0.01f)
                        {
                            chase_camera::pitch_bias *= expf(-chase_camera::orbit_bias_decay * dt);
                        }
                        // hold zone (0.1 - 0.3): do nothing, camera stays where it is
                    }

                    // reset car to spawn position: R key or button east (B on Xbox, O on PlayStation)
                    if (Input::GetKeyDown(KeyCode::R) || Input::GetKeyDown(KeyCode::Button_East))
                    {
                        physics->SetBodyTransform(spawn_position, Quaternion::Identity);
                        chase_camera::initialized = false; // reset camera to avoid jump
                    }

                    // haptic feedback - focused on meaningful events
                    if (is_gamepad_connected)
                    {
                        float left_motor  = 0.0f;  // low-frequency rumble (heavy, tire slip)
                        float right_motor = 0.0f;  // high-frequency rumble (light, abs/braking)

                        // collect wheel slip data
                        float max_slip_ratio = 0.0f;
                        float max_slip_angle = 0.0f;
                        for (int i = 0; i < 4; i++)
                        {
                            WheelIndex wheel = static_cast<WheelIndex>(i);
                            max_slip_ratio   = std::max(max_slip_ratio, fabsf(physics->GetWheelSlipRatio(wheel)));
                            max_slip_angle   = std::max(max_slip_angle, fabsf(physics->GetWheelSlipAngle(wheel)));
                        }

                        // wheelspin (acceleration) or lockup (braking) - strong feedback
                        if (max_slip_ratio > 0.15f)
                        {
                            float slip_intensity = std::clamp((max_slip_ratio - 0.15f) * 1.5f, 0.0f, 1.0f);
                            left_motor += slip_intensity * 0.5f;
                        }

                        // drifting/sliding - moderate feedback
                        if (max_slip_angle > 0.15f)
                        {
                            float drift_intensity = std::clamp((max_slip_angle - 0.15f) * 2.0f, 0.0f, 1.0f);
                            left_motor  += drift_intensity * 0.3f;
                            right_motor += drift_intensity * 0.2f;
                        }

                        // abs activation - distinctive pulsing feedback
                        if (physics->IsAbsActiveAny())
                        {
                            static float abs_pulse = 0.0f;
                            abs_pulse += dt * 25.0f;  // 25hz pulse
                            float pulse_value = (sinf(abs_pulse * math::pi * 2.0f) + 1.0f) * 0.5f;
                            right_motor += pulse_value * 0.6f;
                            left_motor  += pulse_value * 0.3f;
                        }

                        // heavy braking feedback (without abs)
                        if (brake > 0.8f && !physics->IsAbsActiveAny())
                        {
                            right_motor += (brake - 0.8f) * 0.4f;
                        }

                        // clamp and apply
                        left_motor  = std::clamp(left_motor, 0.0f, 1.0f);
                        right_motor = std::clamp(right_motor, 0.0f, 1.0f);
                        Input::GamepadVibrate(left_motor, right_motor);
                    }
                }

                // draw telemetry if enabled
                if (show_telemetry)
                {
                    draw_telemetry();
                }
            }

            // view presets (chase is default like GT7)
            enum class CarView { Chase, Hood, Dashboard };
            static CarView current_view = CarView::Chase;

            // compute car aabb from all renderables in the hierarchy
            auto get_car_aabb = []() -> BoundingBox
            {
                if (!default_car)
                    return BoundingBox::Unit;

                BoundingBox combined(Vector3::Infinity, Vector3::InfinityNeg);
                vector<Entity*> descendants;
                default_car->GetDescendants(&descendants);
                descendants.push_back(default_car);

                for (Entity* entity : descendants)
                {
                    if (Renderable* renderable = entity->GetComponent<Renderable>())
                    {
                        combined.Merge(renderable->GetBoundingBox());
                    }
                }

                return combined;
            };

            // compute view positions and rotations based on car aabb
            struct CarViewData
            {
                Vector3    position;
                Quaternion rotation;
            };

            auto get_car_view_data = [&]() -> array<CarViewData, 3>
            {
                // the car body is rotated 90 degrees around X for physics alignment
                // we need to counter-rotate the camera to look forward
                Quaternion car_local_rot     = default_car->GetRotationLocal();
                Quaternion camera_correction = car_local_rot.Inverse();

                // use fixed positions that work well for typical car models
                // note: car's 90-degree X rotation swaps Y and Z axes
                // x = right/left, y = forward/back, z = down/up (negative = up)
                // order matches enum: Chase, Hood, Dashboard
                return
                {
                    CarViewData
                    {
                        // chase: behind and above the car (handled dynamically, this is just fallback)
                        Vector3(0.0f, -5.0, -1.5f),
                        camera_correction
                    },
                    CarViewData
                    {
                        // hood: above the hood, looking forward
                        Vector3(0.0f, 0.8f, -1.0f),
                        camera_correction
                    },
                    CarViewData
                    {
                        // dashboard: driver seat position
                        Vector3(-0.3f, 0.05f, -0.85f),
                        camera_correction
                    }
                };
            };

            // need camera for inside/outside detection
            if (!default_camera)
                return;

            // cached references
            bool inside_the_car             = is_in_vehicle;
            Entity* sound_door_entity       = vehicle_entity ? vehicle_entity->GetChildByName("sound_door")  : nullptr;
            Entity* sound_start_entity      = vehicle_entity ? vehicle_entity->GetChildByName("sound_start") : nullptr;
            Entity* sound_idle_entity       = vehicle_entity ? vehicle_entity->GetChildByName("sound_idle")  : nullptr;
            AudioSource* audio_source_door  = sound_door_entity  ? sound_door_entity->GetComponent<AudioSource>()  : nullptr;
            AudioSource* audio_source_start = sound_start_entity ? sound_start_entity->GetComponent<AudioSource>() : nullptr;
            AudioSource* audio_source_idle  = sound_idle_entity  ? sound_idle_entity->GetComponent<AudioSource>()  : nullptr;
            if (!vehicle_entity || !audio_source_door || !audio_source_start || !audio_source_idle)
                return;

            // engine sound: pitch and volume based on rpm
            if (vehicle_entity && inside_the_car)
            {
                Physics* physics = vehicle_entity->GetComponent<Physics>();
                if (physics)
                {
                    if (!audio_source_idle->IsPlaying())
                    {
                        audio_source_idle->PlayClip();
                    }
                    
                    float engine_rpm   = physics->GetEngineRPM();
                    float idle_rpm     = physics->GetIdleRPM();
                    float redline_rpm  = physics->GetRedlineRPM();
                    
                    float rpm_normalized = (engine_rpm - idle_rpm) / (redline_rpm - idle_rpm);
                    rpm_normalized = std::max(0.0f, std::min(1.0f, rpm_normalized));
                    
                    // pitch curve: slight quadratic gives more response at higher rpm
                    float pitch_curve = rpm_normalized * rpm_normalized * 0.3f + rpm_normalized * 0.7f;
                    float pitch = 0.8f + pitch_curve * 1.5f;  // 0.8 at idle, up to 2.3 at redline
                    audio_source_idle->SetPitch(pitch);
                    
                    // volume increases with rpm
                    float volume = 0.6f + rpm_normalized * 0.4f;
                    audio_source_idle->SetVolume(volume);
                }
            }
            else if (!inside_the_car && audio_source_idle->IsPlaying())
            {
                audio_source_idle->StopClip();
            }

            // gt7-style chase camera
            if (inside_the_car && current_view == CarView::Chase && vehicle_entity)
            {
                // chase camera must be parented to default_camera, not the car
                Entity* camera = default_camera->GetChildByName("component_camera");
                if (!camera)
                {
                    camera = vehicle_entity->GetChildByName("component_camera");
                    if (!camera)
                        camera = default_car->GetChildByName("component_camera");
                    if (camera)
                    {
                        camera->SetParent(default_camera);
                        chase_camera::initialized = false;
                    }
                }
                
                if (camera)
                {
                    Physics* car_physics = vehicle_entity->GetComponent<Physics>();
                    float dt = static_cast<float>(Timer::GetDeltaTimeSec());
                    
                    // get car state (position is already smoothly interpolated by physics component)
                    Vector3 car_position = vehicle_entity->GetPosition();
                    Vector3 car_forward  = vehicle_entity->GetForward();
                    Vector3 car_velocity = car_physics ? car_physics->GetLinearVelocity() : Vector3::Zero;
                    float car_speed      = car_velocity.Length();
                    
                    // extract yaw from forward vector
                    float target_yaw = atan2f(car_forward.x, car_forward.z);
                    
                    // gt7-style: smooth speed factor for gradual transitions
                    float target_speed_factor = std::clamp(car_speed / chase_camera::speed_reference, 0.0f, 1.0f);
                    chase_camera::speed_factor += (target_speed_factor - chase_camera::speed_factor) * 
                        std::min(1.0f, chase_camera::speed_smoothing * dt);
                    
                    // gt7-style: dynamic distance and height based on speed
                    float dynamic_distance = chase_camera::distance_base - 
                        (chase_camera::distance_base - chase_camera::distance_min) * chase_camera::speed_factor;
                    float dynamic_height = chase_camera::height_base - 
                        (chase_camera::height_base - chase_camera::height_min) * chase_camera::speed_factor;
                    
                    // initialize chase camera state on first use
                    if (!chase_camera::initialized)
                    {
                        chase_camera::yaw          = target_yaw;
                        chase_camera::yaw_bias     = 0.0f;
                        chase_camera::pitch_bias   = 0.0f;
                        chase_camera::speed_factor = target_speed_factor;
                        chase_camera::position     = car_position - Vector3(sinf(target_yaw), 0.0f, cosf(target_yaw)) * dynamic_distance
                                                   + Vector3::Up * dynamic_height;
                        chase_camera::velocity     = Vector3::Zero;
                        chase_camera::initialized  = true;
                    }
                    
                    // gt7-style: rotation follows car with slight lag (more lag = more dramatic swinging)
                    float rotation_speed = chase_camera::rotation_smoothing * (1.0f + chase_camera::speed_factor * 0.5f);
                    chase_camera::yaw = chase_camera::lerp_angle(chase_camera::yaw, target_yaw, 
                        1.0f - expf(-rotation_speed * dt));
                    
                    // compute target camera position based on smoothed yaw/pitch + manual bias from right stick
                    float effective_yaw   = chase_camera::yaw + chase_camera::yaw_bias;
                    float effective_pitch = chase_camera::pitch_bias;

                    // pitch affects the orbit: positive pitch = higher camera, negative = lower
                    float horizontal_scale = cosf(effective_pitch);
                    float vertical_offset  = sinf(effective_pitch) * dynamic_distance;

                    Vector3 offset_direction = Vector3(sinf(effective_yaw), 0.0f, cosf(effective_yaw));
                    Vector3 target_position  = car_position 
                                             - offset_direction * dynamic_distance * horizontal_scale
                                             + Vector3::Up * (dynamic_height + vertical_offset);
                    
                    // gt7-style: position smoothing gets snappier at higher speeds
                    float position_smooth = chase_camera::position_smoothing * (1.0f - chase_camera::speed_factor * 0.3f);
                    Vector3 prev_position = chase_camera::position;
                    chase_camera::position = chase_camera::smooth_damp(
                        chase_camera::position, target_position, chase_camera::velocity, 
                        position_smooth, dt);
                    
                    // gt7-style: look-ahead based on velocity (camera looks where the car is going)
                    Vector3 velocity_xz = Vector3(car_velocity.x, 0.0f, car_velocity.z);
                    float velocity_xz_len = velocity_xz.Length();
                    Vector3 look_ahead = Vector3::Zero;
                    if (velocity_xz_len > 2.0f)
                    {
                        look_ahead = (velocity_xz / velocity_xz_len) * chase_camera::look_ahead_amount * chase_camera::speed_factor;
                    }
                    Vector3 look_at = car_position + Vector3::Up * chase_camera::look_offset_up + look_ahead;
                    
                    // update camera transform
                    camera->SetPosition(chase_camera::position);
                    Vector3 look_direction = (look_at - chase_camera::position).Normalized();
                    camera->SetRotation(Quaternion::FromLookRotation(look_direction, Vector3::Up));
                }
            }

            // enter/exit car
            if (Input::GetKeyDown(KeyCode::E))
            {
                Entity* camera = nullptr;
                if (!inside_the_car)
                {
                    // entering the car
                    camera = default_camera->GetChildByName("component_camera");
                    
                    if (current_view == CarView::Chase)
                    {
                        // chase: stays under default_camera, world-space following
                        chase_camera::initialized = false;
                    }
                    else
                    {
                        // hood: parent to car body
                        camera->SetParent(default_car);
                        array<CarViewData, 3> view_data = get_car_view_data();
                        camera->SetPositionLocal(view_data[static_cast<int>(current_view)].position);
                        camera->SetRotationLocal(view_data[static_cast<int>(current_view)].rotation);
                    }
                    
                    audio_source_start->PlayClip();
                    is_in_vehicle = true;
                }
                else
                {
                    // exiting the car
                    camera = default_car->GetChildByName("component_camera");
                    if (!camera)
                        camera = default_camera->GetChildByName("component_camera");
                    
                    camera->SetParent(default_camera);
                    camera->SetPositionLocal(default_camera->GetComponent<Physics>()->GetControllerTopLocal());
                    camera->SetRotationLocal(Quaternion::Identity);
                    
                    BoundingBox car_aabb = get_car_aabb();
                    Vector3 exit_offset  = default_car->GetLeft() * car_aabb.GetSize().x + Vector3::Up * car_aabb.GetSize().y * 0.5f;
                    default_camera->SetPosition(default_car->GetPosition() + exit_offset);
                    
                    audio_source_idle->StopClip();
                    chase_camera::initialized = false;
                    is_in_vehicle = false;

                    // stop vibration when exiting car
                    Input::GamepadVibrate(0.0f, 0.0f);
                }

                camera->GetComponent<Camera>()->SetFlag(CameraFlags::CanBeControlled, !is_in_vehicle);
                audio_source_door->PlayClip();

                if (default_car_window)
                {
                    default_car_window->SetActive(!is_in_vehicle);
                }
            }

            // cycle camera view: V key or Right Shoulder button (like GT7)
            if (Input::GetKeyDown(KeyCode::V) || Input::GetKeyDown(KeyCode::Right_Shoulder))
            {
                if (inside_the_car)
                {
                    // find camera
                    Entity* camera = default_car->GetChildByName("component_camera");
                    if (!camera)
                        camera = default_camera->GetChildByName("component_camera");

                    if (camera)
                    {
                        CarView previous_view = current_view;
                        current_view = static_cast<CarView>((static_cast<int>(current_view) + 1) % 2); // chase and hood only
                        
                        if (current_view == CarView::Chase)
                        {
                            // switching to chase: unparent for world-space following
                            camera->SetParent(default_camera);
                            chase_camera::initialized = false;
                        }
                        else
                        {
                            // switching to hood: parent to car body
                            camera->SetParent(default_car);
                            array<CarViewData, 3> view_data = get_car_view_data();
                            camera->SetPositionLocal(view_data[static_cast<int>(current_view)].position);
                            camera->SetRotationLocal(view_data[static_cast<int>(current_view)].rotation);
                        }
                    }
                }
            }

            // osd
            Renderer::DrawString("WASD/Gamepad: Move | E: Enter/Exit | V/RB: Change View | R/B: Reset | RS: Look Around", Vector2(0.005f, 0.98f));
        }

        // reset state on shutdown
        void shutdown()
        {
            vehicle_entity                 = nullptr;
            show_telemetry                 = false;
            is_in_vehicle                  = false;
            chase_camera::initialized  = false;
            chase_camera::position     = Vector3::Zero;
            chase_camera::velocity     = Vector3::Zero;
            chase_camera::yaw          = 0.0f;
            chase_camera::yaw_bias     = 0.0f;
            chase_camera::pitch_bias   = 0.0f;
            chase_camera::speed_factor = 0.0f;

            // stop any vibration
            Input::GamepadVibrate(0.0f, 0.0f);
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
                const float scale      = 1.5f;

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
                            material->SetProperty(MaterialProperty::CullMode, static_cast<float>(RHI_CullMode::None));
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
                    if (Entity* leaves = entity->GetDescendantByName("IvySim_Leaves"))
                    { 
                        if (Material* material = leaves->GetComponent<Renderable>()->GetMaterial())
                        {
                            material->SetProperty(MaterialProperty::CullMode,                   static_cast<float>(RHI_CullMode::None));
                            material->SetProperty(MaterialProperty::SubsurfaceScattering,       1.0f);
                            material->SetProperty(MaterialProperty::ColorVariationFromInstance, 1.0f);
                        }
                    }

                    // stem material
                    if (Entity* stems = entity->GetDescendantByName("IvySim_Stems"))
                    { 
                        if (Material* material = stems->GetComponent<Renderable>()->GetMaterial())
                        {
                            material->SetProperty(MaterialProperty::SubsurfaceScattering, 1.0f);
                        }
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
            uint32_t ocean_tile_count = 1;
            float tile_size = 128.0f;
            uint32_t vertices_count = 512;
            shared_ptr<Material> ocean_material = make_shared<Material>();
            shared_ptr<RHI_Texture> flow_map;

            inline int idx(int x, int y, int w) { return y * w + x; }

            void GenerateLakeOutwardFlow(
                const float* height_data,
                uint32_t tex_width,
                uint32_t tex_height,
                float waterLevel,                    // e.g. 0.0f
                std::vector<Vector2>& out_flow_data, // output, size must be tex_width*tex_height
                int blurRadius = 3,                  // optional smoothing radius (3..8)
                float center_strength = 1.0f         // scale of outward strength
            )
            {
                const int W = (int)tex_width;
                const int H = (int)tex_height;
                const int N = W * H;
                out_flow_data.assign(N, Vector2(0.5f, 0.5f));

                // 1) lake mask and shore detection
                std::vector<uint8_t> isLake(N, 0);
                std::vector<uint8_t> isShore(N, 0);

                for (int y = 0; y < H; ++y)
                {
                    for (int x = 0; x < W; ++x)
                    {
                        int i = idx(x, y, W);
                        if (height_data[i] <= waterLevel) isLake[i] = 1;
                    }
                }

                // find shore pixels: lake pixel adjacent to any non-lake (4-neighbour)
                auto inBounds = [&](int x, int y) { return x >= 0 && x < W && y >= 0 && y < H; };
                for (int y = 0; y < H; ++y)
                {
                    for (int x = 0; x < W; ++x)
                    {
                        int i = idx(x, y, W);
                        if (!isLake[i]) continue;
                        bool shore = false;
                        const int nx[4] = { 1,-1,0,0 };
                        const int ny[4] = { 0,0,1,-1 };
                        for (int k = 0; k < 4; ++k)
                        {
                            int sx = x + nx[k], sy = y + ny[k];
                            if (!inBounds(sx, sy) || !isLake[idx(sx, sy, W)]) { shore = true; break; }
                        }
                        if (shore) isShore[i] = 1;
                    }
                }

                // 2) multi-source BFS from shore pixels
                // store nearest shore coords and distance (in pixels)
                const int INF = 1 << 30;
                std::vector<int> dist(N, INF);
                std::vector<int> nearestX(N, -1), nearestY(N, -1);
                std::deque<int> q;

                // push all shore pixels as index seeds
                for (int y = 0; y < H; ++y)
                {
                    for (int x = 0; x < W; ++x)
                    {
                        int i = idx(x, y, W);
                        if (isShore[i])
                        {
                            dist[i] = 0;
                            nearestX[i] = x;
                            nearestY[i] = y;
                            q.push_back(i);
                        }
                    }
                }

                // if no shore pixels (rare), bail out
                if (q.empty()) {
                    // fallback: set small wind or zero flow
                    for (int i = 0; i < N; i++) out_flow_data[i] = Vector2(0.5f, 0.5f);
                    return;
                }

                const int nbrX[4] = { 1,-1,0,0 };
                const int nbrY[4] = { 0,0,1,-1 };

                while (!q.empty())
                {
                    int cur = q.front(); q.pop_front();
                    int cx = cur % W;
                    int cy = cur / W;
                    int cd = dist[cur];

                    for (int k = 0; k < 4; ++k)
                    {
                        int nxp = cx + nbrX[k];
                        int nyp = cy + nbrY[k];
                        if (!inBounds(nxp, nyp)) continue;
                        int ni = idx(nxp, nyp, W);
                        if (!isLake[ni]) continue; // only propagate inside lakes

                        if (dist[ni] > cd + 1)
                        {
                            dist[ni] = cd + 1;
                            nearestX[ni] = nearestX[cur];
                            nearestY[ni] = nearestY[cur];
                            q.push_back(ni);
                        }
                    }
                }

                // 3) build outward flow: nearest shore vector -> direction to shore
                // also compute max distance for normalization
                int maxDist = 0;
                for (int i = 0; i < N; i++) if (isLake[i] && dist[i] < INF) maxDist = std::max(maxDist, dist[i]);

                if (maxDist == 0) maxDist = 1;

                for (int y = 0; y < H; ++y)
                {
                    for (int x = 0; x < W; ++x)
                    {
                        int i = idx(x, y, W);
                        if (!isLake[i])
                        {
                            // encode 0 flow on land (or whatever you prefer)
                            out_flow_data[i] = Vector2(0.5f, 0.5f);
                            continue;
                        }

                        int sx = nearestX[i];
                        int sy = nearestY[i];
                        if (sx < 0)
                        {
                            // no nearest shore found (shouldn't happen) => tiny noise/wind
                            out_flow_data[i] = Vector2(0.5f, 0.5f);
                            continue;
                        }

                        // vector from pixel -> shore
                        float vx = (float)sx - (float)x;
                        float vy = (float)sy - (float)y;
                        float d = std::sqrt(vx * vx + vy * vy);
                        if (d < 1e-6f) {
                            // on the shore pixel: zero magnitude
                            out_flow_data[i] = Vector2(0.5f, 0.5f);
                        }
                        else {
                            // normalized direction towards shore (points outward)
                            float nxv = vx / d;
                            float nyv = vy / d;

                            // optional magnitude: stronger near center (far from shore)
                            float mag = (float)dist[i] / (float)maxDist; // 0..1 (0 at shore, 1 at farthest)
                            mag = pow(mag, 0.8f) * center_strength;    // tweak exponent for shape

                            // combine direction and magnitude (we'll store unit direction only; magnitude can be separate channel)
                            float ux = nxv * mag;
                            float uy = nyv * mag;

                            // store as signed normalized vector in [-1,1] then encode to [0,1]
                            float ex = ux * 0.5f + 0.5f;
                            float ey = uy * 0.5f + 0.5f;
                            out_flow_data[i] = Vector2(ex, ey);
                        }
                    }
                }

                // 4) optional: blur/smooth the flow vectors (box blur or Gaussian)
                if (blurRadius > 0)
                {
                    std::vector<Vector2> temp = out_flow_data;
                    for (int y = 0; y < H; ++y)
                    {
                        for (int x = 0; x < W; ++x)
                        {
                            int i = idx(x, y, W);
                            if (!isLake[i]) continue;
                            float sx = 0.0f, sy = 0.0f; int cnt = 0;
                            for (int oy = -blurRadius; oy <= blurRadius; ++oy)
                            {
                                for (int ox = -blurRadius; ox <= blurRadius; ++ox)
                                {
                                    int nxp = std::clamp(x + ox, 0, W - 1);
                                    int nyp = std::clamp(y + oy, 0, H - 1);
                                    int ni = idx(nxp, nyp, W);
                                    if (!isLake[ni]) continue;
                                    sx += (temp[ni].x - 0.5f) * 2.0f; // decode back -1..1
                                    sy += (temp[ni].y - 0.5f) * 2.0f;
                                    cnt++;
                                }
                            }
                            if (cnt > 0) {
                                sx /= (float)cnt;
                                sy /= (float)cnt;
                                float l = std::sqrt(sx * sx + sy * sy);
                                if (l > 1e-6f) { sx /= l; sy /= l; }
                                // reapply magnitude based on dist (optional)
                                float mag = (float)dist[i] / (float)maxDist;
                                float ux = sx * mag;
                                float uy = sy * mag;
                                out_flow_data[i] = Vector2(ux * 0.5f + 0.5f, uy * 0.5f + 0.5f);
                            }
                        }
                    }
                }
            }

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
                entities::sun(LightPreset::david_lynch, true);
                Light* sun = default_light_directional->GetComponent<Light>();
                sun->SetFlag(LightFlags::Volumetric, true);

                entities::camera(false, Vector3(-1476.0f, 17.9f, 1490.0f), Vector3(-3.6f, 90.0f, 0.0f));
                ConsoleRegistry::Get().SetValueFromString("r.grid", "0");

                // drivable car near the player
                {
                    //car::Config car_config;
                    //car_config.position       = Vector3(-1470.0f, 20.0f, 1490.0f); // slightly in front of camera
                    //car_config.drivable       = true;
                    //car_config.show_telemetry = true;
                    //car::create(car_config);
                }

                // terrain root
                default_terrain = World::CreateEntity();
                default_terrain->SetObjectName("terrain");
                default_ocean = entities::ocean(ocean_material, { 0.0f, 0.0f, 0.0f }, tile_size, vertices_count, ocean_tile_count);

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
                        material->SetProperty(MaterialProperty::TextureTilingX, 2000.0f);
                        material->SetProperty(MaterialProperty::TextureTilingY, 2000.0f);

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
                    if (height_map)
                    {
                        height_map->PrepareForGpu();
                    }
                    terrain->SetHeightMapSeed(height_map.get());
                    terrain->Generate();

                    // terrain physics
                    for (Entity* terrain_tile : terrain->GetEntity()->GetChildren())
                    {
                        Physics* physics_body = terrain_tile->AddComponent<Physics>();
                        physics_body->SetBodyType(BodyType::Mesh);
                    }
                }

                // TEMP - ocean
                RHI_Texture* height_map = terrain->GetHeightMapFinal();
                //default_ocean = entities::ocean(ocean_material, { 0.0f, 0.0f, 0.0f }, tile_size, vertices_count, ocean_tile_count);
                ocean_material->SetTexture(MaterialTextureType::Height, height_map);
                // generate flowmap
                {
                    const float* height_data = reinterpret_cast<const float*>(height_map->GetMip(0, 0).bytes.data());

                    const uint32_t tex_width = height_map->GetWidth();
                    const uint32_t tex_height = height_map->GetHeight();

                    SP_ASSERT(height_map->GetMip(0, 0).bytes.size() == tex_width * tex_height * sizeof(float));

                    std::vector<Vector2> flow_data(tex_width * tex_height);
                    std::vector<Vector2> lake_flow(tex_width * tex_height);

                    const float waterLevel = 0.0f; // threshold for "lake" height
                    const int kernelRadius = 8;    // used for slope-based flow smoothing

                    // --- 1) Generate lake outward flow field ---
                    GenerateLakeOutwardFlow(height_data, tex_width, tex_height, waterLevel, lake_flow, 4, 1.0f);

                    // --- 2) Generate slope-based (river) flow field ---
                    for (uint32_t y = 0; y < tex_height; y++)
                    {
                        for (uint32_t x = 0; x < tex_width; x++)
                        {
                            float gx = 0.0f;
                            float gy = 0.0f;
                            int samples = 0;

                            for (int ky = -kernelRadius; ky <= kernelRadius; ky++)
                            {
                                for (int kx = -kernelRadius; kx <= kernelRadius; kx++)
                                {
                                    uint32_t ix = std::clamp<int>(x + kx, 0, tex_width - 1);
                                    uint32_t iy = std::clamp<int>(y + ky, 0, tex_height - 1);
                                    uint32_t ixL = std::clamp<int>(ix - 1, 0, tex_width - 1);
                                    uint32_t ixR = std::clamp<int>(ix + 1, 0, tex_width - 1);
                                    uint32_t iyU = std::clamp<int>(iy + 1, 0, tex_height - 1);
                                    uint32_t iyD = std::clamp<int>(iy - 1, 0, tex_height - 1);

                                    float hL = height_data[iy * tex_width + ixL];
                                    float hR = height_data[iy * tex_width + ixR];
                                    float hU = height_data[iyU * tex_width + ix];
                                    float hD = height_data[iyD * tex_width + ix];

                                    gx += (hR - hL);
                                    gy += (hD - hU);
                                    samples++;
                                }
                            }

                            gx /= (samples * 2.0f);
                            gy /= (samples * 2.0f);

                            Vector2 flow = { -gx, -gy };

                            // --- 3) Replace flow with lake pattern where below waterLevel ---
                            if (height_data[y * tex_width + x] <= waterLevel)
                            {
                                flow_data[y * tex_width + x] = lake_flow[y * tex_width + x];
                                continue;
                            }

                            float len = std::sqrt(flow.x * flow.x + flow.y * flow.y);
                            if (len > 0.0001f)
                                flow /= len;

                            // Encode slope flow into [0,1]
                            flow_data[y * tex_width + x] = Vector2(flow.x * 0.5f + 0.5f, flow.y * 0.5f + 0.5f);
                        }
                    }

                    // --- 4) Encode into R8G8_UNORM texture ---
                    vector<RHI_Texture_Slice> data(1);
                    auto& slice = data[0];
                    slice.mips.resize(1);
                    auto& mip_bytes = slice.mips[0].bytes;
                    mip_bytes.resize(tex_width * tex_height * 2); // 2 bytes per pixel for R8G8_Unorm

                    auto copy_data = [&flow_data, &mip_bytes](uint32_t start, uint32_t end)
                        {
                            for (uint32_t i = start; i < end; i++)
                            {
                                const Vector2& f = flow_data[i];
                                float fx = std::clamp(f.x, 0.0f, 1.0f);
                                float fy = std::clamp(f.y, 0.0f, 1.0f);
                                uint8_t r = static_cast<uint8_t>(std::round(fx * 255.0f));
                                uint8_t g = static_cast<uint8_t>(std::round(fy * 255.0f));
                                mip_bytes[i * 2 + 0] = static_cast<byte>(r);
                                mip_bytes[i * 2 + 1] = static_cast<byte>(g);
                            }
                        };

                    ThreadPool::ParallelLoop(copy_data, tex_width * tex_height);

                    // --- 5) Upload to GPU ---
                    flow_map = std::make_shared<RHI_Texture>(
                        RHI_Texture_Type::Type2D,
                        tex_width, tex_height, 1, 1,
                        RHI_Format::R8G8_Unorm,
                        RHI_Texture_Srv,
                        "terrain_flowmap",
                        data
                    );

                    ocean_material->SetTexture(MaterialTextureType::Flowmap, flow_map);
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
                                terrain->FindTransforms(tile_index, TerrainProp::Tree, entity, per_triangle_density_tree, 0.026f, transforms);

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
                                terrain->FindTransforms(tile_index, TerrainProp::Rock, entity, per_triangle_density_rock, 0.64f, transforms);

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
                                terrain->FindTransforms(tile_index, TerrainProp::Flower, entity, per_triangle_density_flower, 0.64f, transforms);

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
            Entity* turn_table = nullptr;

            void create()
            {
                entities::music("project\\music\\gran_turismo.wav");

                // textures
                texture_brand_logo = make_shared<RHI_Texture>("project\\models\\ferrari_laferrari\\logo.png");

                // create display car (non-drivable)
                car::Config car_config;
                car_config.position       = Vector3(0.0f, 0.08f, 0.0f);
                car_config.drivable       = false;
                car_config.static_physics = false;
                car::create(car_config);

                // camera looking at car
                {
                    Vector3 camera_position = Vector3(0.2745f, 0.91f, 4.9059f);
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
                        floor_tube_lights->SetScale(1.0f);

                        // tube light setup helper
                        auto setup_tube_light = [floor_tube_lights](const char* descendant_name, Color color)
                        {
                            if (Entity* entity_tube_light = floor_tube_lights->GetDescendantByName(descendant_name))
                            {
                                Renderable* renderable = entity_tube_light->GetComponent<Renderable>();
                                renderable->SetFlag(RenderableFlags::CastsShadows, false);
                                if (Material* material = renderable->GetMaterial())
                                {
                                    material->SetColor(color);
                                    material->SetProperty(MaterialProperty::EmissiveFromAlbedo, 1.0f);

                                    // get tube mesh dimensions from bounding box
                                    const math::BoundingBox& bbox = renderable->GetBoundingBox();
                                    Vector3 size = bbox.GetSize();

                                    // area light matching the tube mesh
                                    Entity* entity = World::CreateEntity();
                                    entity->SetObjectName("light_area");
                                    entity->SetParent(entity_tube_light);

                                    // orient the area light to face downward (tubes are ceiling lights)
                                    entity->SetRotationLocal(Quaternion::FromEulerAngles(90.0f, 0.0f, 0.0f));

                                    Light* light = entity->AddComponent<Light>();
                                    light->SetLightType(LightType::Area);
                                    light->SetColor(color);
                                    light->SetRange(80.0f);
                                    light->SetIntensity(4000.0f);
                                    light->SetFlag(LightFlags::Shadows,            true);
                                    light->SetFlag(LightFlags::ShadowsScreenSpace, false);
                                    light->SetFlag(LightFlags::Volumetric,         false);

                                    // set area light dimensions from the tube's bounding box
                                    // tube is oriented horizontally, so use x/z for width and y for height
                                    float area_width  = max(size.x, size.z); // length of the tube
                                    float area_height = min(size.x, size.z); // diameter of the tube
                                    light->SetAreaWidth(area_width);
                                    light->SetAreaHeight(area_height);
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
                            turn_table->SetRotation(Quaternion::FromEulerAngles(0.0f, 142.9024f, 0.0f));
                            if (Material* material = turn_table->GetComponent<Renderable>()->GetMaterial())
                            {
                                material->SetColor(Color::standard_black);
                            }
                            turn_table->GetComponent<Physics>()->SetKinematic(true);
                        }
                    }
                }

                // renderer options
                ConsoleRegistry::Get().SetValueFromString("r.performance_metrics", "0");
                ConsoleRegistry::Get().SetValueFromString("r.lights",              "0");
                ConsoleRegistry::Get().SetValueFromString("r.dithering",           "0");
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
                ConsoleRegistry::Get().SetValueFromString("r.chromatic_aberration", "1");
                ConsoleRegistry::Get().SetValueFromString("r.vhs", "1");

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
    }
    //====================================================================================

    //== CAR PLAYGROUND ==================================================================
    namespace car_playground
    {
        // helper to create a cube obstacle with physics
        // mass = 0 means static, mass > 0 means dynamic with that mass in kg
        void create_cube(const string& name, const Vector3& position, const Vector3& euler_angles, const Vector3& scale, float mass = 0.0f)
        {
            Entity* entity = World::CreateEntity();
            entity->SetObjectName(name);
            entity->SetPosition(position);
            entity->SetRotation(Quaternion::FromEulerAngles(euler_angles.x, euler_angles.y, euler_angles.z));
            entity->SetScale(scale);

            Renderable* renderable = entity->AddComponent<Renderable>();
            renderable->SetMesh(MeshType::Cube);
            renderable->SetDefaultMaterial();

            Physics* physics_body = entity->AddComponent<Physics>();
            physics_body->SetBodyType(BodyType::Box);
            physics_body->SetStatic(mass == 0.0f);
            physics_body->SetMass(mass);
        }

        void create()
        {
            entities::camera(false, Vector3(0.0f, 8.0f, -25.0f), Vector3(15.0f, 0.0f, 0.0f));
            entities::sun(LightPreset::dusk, true);
            entities::floor();

            // create drivable car with telemetry
            car::Config car_config;
            car_config.position = Vector3(0.0f, 0.5f, 0.0f);
            car_config.drivable = true;
            car_config.show_telemetry = true;
            car_config.camera_follows = true;
            car::create(car_config);

            //==================================================================================
            // zone 1: main jump ramp area (in front of spawn)
            //==================================================================================

            // gentle starter ramp
            create_cube("ramp_starter", Vector3(12.0f, 0.3f, 0.0f), Vector3(0.0f, 0.0f, 8.0f), Vector3(8.0f, 0.6f, 6.0f));

            // main jump ramp - steep for big air
            create_cube("ramp_jump_main", Vector3(28.0f, 1.2f, 0.0f), Vector3(0.0f, 0.0f, 18.0f), Vector3(10.0f, 0.8f, 7.0f));

            // landing ramp - downward slope for smooth landings
            create_cube("ramp_landing", Vector3(50.0f, 0.5f, 0.0f), Vector3(0.0f, 0.0f, -12.0f), Vector3(12.0f, 0.6f, 7.0f));

            //==================================================================================
            // zone 2: suspension test track (to the right of spawn)
            //==================================================================================

            // speed bumps - series of small bumps to test suspension
            for (int i = 0; i < 8; i++)
            {
                float x_offset = i * 4.0f;
                create_cube("speed_bump_" + to_string(i), Vector3(15.0f + x_offset, 0.15f, 20.0f), Vector3::Zero, Vector3(1.5f, 0.3f, 5.0f));
            }

            // rumble strips - alternating small ridges
            for (int i = 0; i < 12; i++)
            {
                float x_offset = i * 2.5f;
                float height = (i % 2 == 0) ? 0.1f : 0.18f;
                create_cube("rumble_" + to_string(i), Vector3(15.0f + x_offset, height * 0.5f, 30.0f), Vector3::Zero, Vector3(1.0f, height, 4.0f));
            }

            // pothole simulation - dips created by raised edges
            create_cube("pothole_edge_1", Vector3(60.0f, 0.08f, 20.0f), Vector3::Zero, Vector3(0.8f, 0.16f, 6.0f));
            create_cube("pothole_edge_2", Vector3(66.0f, 0.08f, 20.0f), Vector3::Zero, Vector3(0.8f, 0.16f, 6.0f));

            //==================================================================================
            // zone 3: stunt ramps and half-pipe (to the left of spawn)
            //==================================================================================

            // half-pipe left wall
            create_cube("halfpipe_left", Vector3(-25.0f, 2.0f, 0.0f), Vector3(0.0f, 0.0f, 35.0f), Vector3(8.0f, 0.5f, 20.0f));

            // half-pipe right wall
            create_cube("halfpipe_right", Vector3(-25.0f, 2.0f, 15.0f), Vector3(0.0f, 0.0f, -35.0f), Vector3(8.0f, 0.5f, 20.0f));

            // half-pipe back wall (for u-turns)
            create_cube("halfpipe_back", Vector3(-38.0f, 1.5f, 7.5f), Vector3(25.0f, 0.0f, 0.0f), Vector3(6.0f, 0.5f, 18.0f));

            // kicker ramp - small but steep for tricks
            create_cube("kicker_ramp", Vector3(-10.0f, 0.6f, -15.0f), Vector3(0.0f, 0.0f, 25.0f), Vector3(4.0f, 0.5f, 4.0f));

            // side ramp for barrel rolls
            create_cube("barrel_roll_ramp", Vector3(-15.0f, 0.8f, -25.0f), Vector3(30.0f, 45.0f, 15.0f), Vector3(5.0f, 0.4f, 3.0f));

            //==================================================================================
            // zone 4: slalom course (behind spawn)
            //==================================================================================

            // slalom pylons - alternating obstacles (25 kg like plastic barriers)
            for (int i = 0; i < 6; i++)
            {
                float z_offset = -20.0f - (i * 12.0f);
                float x_offset = (i % 2 == 0) ? 5.0f : -5.0f;
                create_cube("slalom_pylon_" + to_string(i), Vector3(x_offset, 1.0f, z_offset), Vector3::Zero, Vector3(1.5f, 2.0f, 1.5f), 25.0f);
            }

            // slalom finish gate pillars - dynamic so they can be knocked over
            create_cube("gate_left", Vector3(-6.0f, 2.0f, -95.0f), Vector3::Zero, Vector3(1.0f, 4.0f, 1.0f), 30.0f);
            create_cube("gate_right", Vector3(6.0f, 2.0f, -95.0f), Vector3::Zero, Vector3(1.0f, 4.0f, 1.0f), 30.0f);

            //==================================================================================
            // zone 5: banked turn circuit (far right area)
            //==================================================================================

            // banked turn - outside wall
            create_cube("bank_outer", Vector3(80.0f, 1.5f, 0.0f), Vector3(0.0f, 30.0f, -25.0f), Vector3(20.0f, 0.6f, 8.0f));

            // banked turn - inside wall
            create_cube("bank_inner", Vector3(75.0f, 0.8f, 8.0f), Vector3(0.0f, 30.0f, -15.0f), Vector3(15.0f, 0.4f, 6.0f));

            // exit ramp from banked turn
            create_cube("bank_exit_ramp", Vector3(95.0f, 0.4f, -10.0f), Vector3(0.0f, 60.0f, 10.0f), Vector3(8.0f, 0.5f, 5.0f));

            //==================================================================================
            // zone 6: obstacle course (scattered dynamic objects)
            //==================================================================================

            // stack of crates to crash through (20 kg wooden crates)
            // add small gaps (1.55 spacing for 1.5 size) to prevent interpenetration explosions
            for (int row = 0; row < 3; row++)
            {
                for (int col = 0; col < 3; col++)
                {
                    float y_pos = 0.76f + (row * 1.55f);
                    float x_pos = 35.0f + (col * 1.65f);
                    create_cube("crate_stack_" + to_string(row) + "_" + to_string(col), Vector3(x_pos, y_pos, -30.0f), Vector3::Zero, Vector3(1.5f, 1.5f, 1.5f), 20.0f);
                }
            }

            // barrel wall (15 kg empty barrels)
            // add gaps to prevent interpenetration
            for (int i = 0; i < 5; i++)
            {
                float x_pos = 50.0f + (i * 2.2f);
                create_cube("barrel_" + to_string(i), Vector3(x_pos, 0.85f, -45.0f), Vector3(90.0f, 0.0f, 0.0f), Vector3(1.2f, 1.6f, 1.2f), 15.0f);
            }

            // pyramid of boxes (15 kg cardboard boxes)
            // add small gaps to prevent interpenetration explosions
            int pyramid_base = 4;
            for (int level = 0; level < pyramid_base; level++)
            {
                int boxes_in_level = pyramid_base - level;
                float y_pos = 0.62f + (level * 1.25f);
                float start_x = 70.0f - (boxes_in_level * 0.65f);
                for (int b = 0; b < boxes_in_level; b++)
                {
                    create_cube("pyramid_" + to_string(level) + "_" + to_string(b), Vector3(start_x + (b * 1.35f), y_pos, -60.0f), Vector3::Zero, Vector3(1.2f, 1.2f, 1.2f), 15.0f);
                }
            }

            //==================================================================================
            // zone 7: wavy terrain (far left)
            //==================================================================================

            // series of sine-wave like bumps
            for (int i = 0; i < 10; i++)
            {
                float z_pos = -40.0f + (i * 6.0f);
                float height = 0.3f + 0.3f * sin(i * 0.8f);
                float angle = 8.0f * sin(i * 0.5f);
                create_cube("wave_" + to_string(i), Vector3(-50.0f, height, z_pos), Vector3(angle, 0.0f, 0.0f), Vector3(8.0f, 0.4f, 4.0f));
            }

            //==================================================================================
            // zone 8: stunt park center piece - mega ramp
            //==================================================================================

            // approach ramp
            create_cube("mega_approach", Vector3(-70.0f, 1.0f, -30.0f), Vector3(0.0f, 0.0f, 12.0f), Vector3(15.0f, 0.6f, 10.0f));

            // main mega ramp
            create_cube("mega_ramp", Vector3(-90.0f, 4.0f, -30.0f), Vector3(0.0f, 0.0f, 30.0f), Vector3(12.0f, 0.8f, 10.0f));

            // mega ramp platform top
            create_cube("mega_platform", Vector3(-105.0f, 7.5f, -30.0f), Vector3::Zero, Vector3(8.0f, 0.5f, 10.0f));

            // drop ramp on other side
            create_cube("mega_drop", Vector3(-118.0f, 4.0f, -30.0f), Vector3(0.0f, 0.0f, -35.0f), Vector3(10.0f, 0.8f, 10.0f));

            //==================================================================================
            // zone 9: figure-8 crossover
            //==================================================================================

            // elevated crossing ramp 1
            create_cube("cross_ramp_up_1", Vector3(0.0f, 1.0f, 50.0f), Vector3(0.0f, 45.0f, 15.0f), Vector3(12.0f, 0.5f, 6.0f));

            // elevated bridge section
            create_cube("cross_bridge", Vector3(8.0f, 2.5f, 58.0f), Vector3(0.0f, 45.0f, 0.0f), Vector3(10.0f, 0.4f, 6.0f));

            // elevated crossing ramp 2
            create_cube("cross_ramp_down_1", Vector3(16.0f, 1.0f, 66.0f), Vector3(0.0f, 45.0f, -15.0f), Vector3(12.0f, 0.5f, 6.0f));

            // lower path goes underneath
            create_cube("under_path_guide_left", Vector3(-2.0f, 0.4f, 62.0f), Vector3(0.0f, -45.0f, 0.0f), Vector3(0.5f, 0.8f, 15.0f));
            create_cube("under_path_guide_right", Vector3(10.0f, 0.4f, 50.0f), Vector3(0.0f, -45.0f, 0.0f), Vector3(0.5f, 0.8f, 15.0f));

            //==================================================================================
            // zone 10: parking challenge (precision driving)
            //==================================================================================

            // tight parking spots with pillars
            for (int i = 0; i < 4; i++)
            {
                float z_pos = 80.0f + (i * 8.0f);
                create_cube("parking_left_" + to_string(i), Vector3(-8.0f, 0.5f, z_pos), Vector3::Zero, Vector3(0.3f, 1.0f, 0.3f), 5.0f);
                create_cube("parking_right_" + to_string(i), Vector3(8.0f, 0.5f, z_pos), Vector3::Zero, Vector3(0.3f, 1.0f, 0.3f), 5.0f);
            }

            // parking lot boundary walls
            create_cube("parking_wall_back", Vector3(0.0f, 0.5f, 115.0f), Vector3::Zero, Vector3(20.0f, 1.0f, 0.5f));
            create_cube("parking_wall_left", Vector3(-10.0f, 0.5f, 97.0f), Vector3::Zero, Vector3(0.5f, 1.0f, 38.0f));
            create_cube("parking_wall_right", Vector3(10.0f, 0.5f, 97.0f), Vector3::Zero, Vector3(0.5f, 1.0f, 38.0f));

            //==================================================================================
            // decorative boundary markers
            //==================================================================================

            // corner markers for the playground area
            create_cube("marker_ne", Vector3(120.0f, 1.5f, 120.0f), Vector3::Zero, Vector3(2.0f, 3.0f, 2.0f));
            create_cube("marker_nw", Vector3(-130.0f, 1.5f, 120.0f), Vector3::Zero, Vector3(2.0f, 3.0f, 2.0f));
            create_cube("marker_se", Vector3(120.0f, 1.5f, -100.0f), Vector3::Zero, Vector3(2.0f, 3.0f, 2.0f));
            create_cube("marker_sw", Vector3(-130.0f, 1.5f, -100.0f), Vector3::Zero, Vector3(2.0f, 3.0f, 2.0f));

            // make room for the telemetry display
            ConsoleRegistry::Get().SetValueFromString("r.performance_metrics", "0");
        }
    }
    //====================================================================================
    }
    //========================================================================================


    namespace ocean
    {
        uint32_t ocean_tile_count = 6;
        float tile_size = 128.0f;
        uint32_t vertices_count = 512;
        shared_ptr<Material> material = make_shared<Material>();

        void create()
        {
            entities::camera();
            entities::sun(true);

            auto entity = World::CreateEntity();

            default_ocean = entities::ocean(material, { 0.0f, 0.0f, 0.0f }, tile_size, vertices_count, ocean_tile_count);

            default_ocean->SetParent(entity);

            /*auto light_entity = World::CreateEntity();
            light_entity->SetPosition({ 196.0f, 280.0f, 196.0f });

            Light* point = light_entity->AddComponent<Light>();
            point->SetLightType(LightType::Point);
            point->SetRange(800.0f);
            point->SetTemperature(10000.0f);
            point->SetIntensity(8500.0f);
            point->SetObjectName("Point Light");
            */

            default_light_directional->GetComponent<Light>()->SetFlag(LightFlags::ShadowsScreenSpace, false);
        }

        void tick()
        {
            if (!material)
                return;

            uint32_t current_tile_count = material->GetOceanTileCount();
            if (current_tile_count != ocean_tile_count || tile_size != material->GetOceanTileSize() || vertices_count != material->GetOceanVerticesCount())
            {
                ocean_tile_count = current_tile_count;
                auto& children = default_ocean->GetChildren();

                for (uint32_t i = 0; i < children.size(); i++)
                {
                    World::RemoveEntity(children[i]);
                }
                children.clear();

                std::shared_ptr<Mesh> ocean_mesh;

                for (size_t i = 0; i < meshes.size(); i++)
                {
                    if (meshes[i]->GetObjectName() == "ocean mesh")
                        ocean_mesh = meshes[i];
                }

                if (ocean_mesh.get() == nullptr)
                    return;

                // regenerate mesh
                if (tile_size != material->GetOceanTileSize() || vertices_count != material->GetOceanVerticesCount())
                {
                    tile_size = material->GetOceanTileSize();
                    vertices_count = material->GetOceanVerticesCount();

                    // generate grid
                    const uint32_t grid_points_per_dimension = vertices_count;
                    vector<RHI_Vertex_PosTexNorTan> vertices;
                    vector<uint32_t> indices;
                    geometry_generation::generate_grid(&vertices, &indices, grid_points_per_dimension, tile_size);

                    //string name = "ocean mesh";

                    // create mesh if it doesn't exist
                    ocean_mesh->Clear();

                    //for (std::vector<std::shared_ptr<Mesh>>::iterator it = meshes.begin(); it != meshes.end();)
                    //{
                    //    std::shared_ptr<Mesh> m = *it;
                    //    if (m->GetObjectName() == "ocean mesh")
                    //        it = meshes.erase(it);
                    //    else;
                    //        ++it;
                    //}

                    /*ocean_mesh = meshes.emplace_back(make_shared<Mesh>());
                    ocean_mesh->SetObjectName(name);
                    ocean_mesh->SetRootEntity(default_ocean);
                    ocean_mesh->SetFlag(static_cast<uint32_t>(MeshFlags::PostProcessOptimize), false);
                    ocean_mesh->SetFlag(static_cast<uint32_t>(MeshFlags::PostProcessNormalizeScale), false);*/
                    ocean_mesh->AddGeometry(vertices, indices, false);
                    ocean_mesh->CreateGpuBuffers();
                }

                for (uint32_t row = 0; row < current_tile_count; row++)
                {
                    for (uint32_t col = 0; col < current_tile_count; col++)
                    {
                        int tile_index = col + row * current_tile_count;

                        string tile_name = "ocean tile_" + to_string(tile_index);

                        Entity* entity_tile = World::CreateEntity();
                        entity_tile->SetObjectName(tile_name);
                        entity_tile->SetParent(default_ocean);

                        Vector3 tile_position = { col * tile_size, 0.0f, row * tile_size };
                        entity_tile->SetPosition(tile_position);

                        if (Renderable* renderable = entity_tile->AddComponent<Renderable>())
                        {
                            renderable->SetMesh(ocean_mesh.get());
                            renderable->SetMaterial(material);
                            renderable->SetFlag(RenderableFlags::CastsShadows, false);
                        }

                        // enable buoyancy
                        //Physics* physics = entity_tile->AddComponent<Physics>();
                        //physics->SetBodyType(BodyType::Water);
                    }
                }
            }

            Vector3 camera_pos = default_camera->GetPosition();

            //Vector3 ocean_pos = default_ocean->GetPosition();
            //Vector3 new_ocean_pos = ocean_pos;
            //new_ocean_pos.x = camera_pos.x;
            //new_ocean_pos.z = camera_pos.z;
            //default_ocean->SetPosition(new_ocean_pos);
        }

        void on_shutdown()
        {
            if (!default_ocean)
                return;

            if (!material)
                SP_ASSERT_MSG(false, "Failed to get ocean material");

            material->SaveToFile(material->GetResourceFilePath());

            default_ocean = nullptr;
        }
    }
    }

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

        // reset world-specific state
        worlds::showroom::texture_brand_logo = nullptr;
        worlds::ocean::on_shutdown();
        car::shutdown();
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

    void Game::Tick()
    {
        // car tick (always)
        car::tick();

        // ocean-specific tick
        if (loaded_world == DefaultWorld::Ocean)
        {
            worlds::ocean::tick();
        }

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
