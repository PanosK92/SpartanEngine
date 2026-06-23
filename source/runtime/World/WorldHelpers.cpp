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
#include "WorldHelpers.h"
#include "World.h"
#include "Entity.h"
#include "Components/Render.h"
#include "Components/Physics.h"
#include "Components/AudioSource.h"
#include "Components/Terrain.h"
#include "Components/Water.h"
#include "../Core/ThreadPool.h"
#include "../Core/Stopwatch.h"
#include "../Rendering/Renderer.h"
#include "../Rendering/Material.h"
#include "../Rendering/GeometryBuffer.h"
#include "../Resource/ResourceCache.h"
#include "../Geometry/Mesh.h"
#include "../Geometry/GeometryGeneration.h"
#include "../Geometry/GeometryProcessing.h"
#include "../RHI/RHI_Texture.h"
SP_WARNINGS_OFF
#include <sol/sol.hpp>
SP_WARNINGS_ON
//==========================================

//= NAMESPACES ===============
using namespace std;
using namespace spartan::math;
//============================

namespace spartan
{
    namespace
    {
        // long lived meshes and materials owned by the builders, kept alive beyond any single entity
        // procedural grass references a mesh and material from renderer state rather than a render component
        vector<shared_ptr<Mesh>> builder_meshes;
        vector<shared_ptr<Material>> builder_materials;
    }

    // fft ocean surface, the water component owns the clipmap mesh and drives the gpu simulation
    static Entity* create_water(const Vector3& position)
    {
        Entity* water = World::CreateEntity();
        water->SetObjectName("water");
        water->SetPosition(position);
        water->AddComponent<Water>();

        return water;
    }

    void WorldHelpers::BuildForest(Entity* builder_entity)
    {
        // config
        const float render_distance_trees            = 2'000.0f;
        const float render_distance_foliage          = 500.0f;
        const float shadow_distance                  = 150.0f;
        const float per_triangle_density_flower      = 0.2f;
        const float per_triangle_density_tree        = 0.004f;
        const float per_triangle_density_rock        = 0.001f;

        // pre-size the global geometry buffer high enough for the whole forest so worker threads streaming
        // mesh data in cannot trip a mid-load rebuild from the renderer's per-frame BuildIfDirty
        GeometryBuffer::Reserve(
            12u * 1024u * 1024u, // ~12M vertices
            32u * 1024u * 1024u, // ~32M indices
            128u * 1024u,        // ~128K meshlet bounds
            256u * 1024u         // ~256K instances
        );

        // heavy mesh work, spread across the pool through ParallelLoop
        // ParallelLoop is nesting safe, it runs sequentially when the pool is already saturated,
        // BuildForest itself runs on a worker thread during world load so a raw AddTask wait could deadlock
        shared_ptr<Mesh> mesh_tree;
        shared_ptr<Mesh> mesh_rock;
        shared_ptr<Mesh> mesh_grass_blade = builder_meshes.emplace_back(make_shared<Mesh>());
        shared_ptr<Mesh> mesh_flower      = builder_meshes.emplace_back(make_shared<Mesh>());

        Stopwatch sw_parallel_meshes;

        const uint32_t tree_flags      = Mesh::GetDefaultFlags() | static_cast<uint32_t>(MeshFlags::ImportCombineMeshes);
        const string flower_cache_path = string(ResourceCache::GetProjectDirectory()) + "standard_flower" + EXTENSION_MESH;

        auto build_mesh = [&mesh_tree, &mesh_rock, mesh_grass_blade, mesh_flower, tree_flags, flower_cache_path](uint32_t start, uint32_t end)
        {
            for (uint32_t i = start; i < end; i++)
            {
                if (i == 0)
                {
                    mesh_tree = ResourceCache::Load<Mesh>("project/models/tree/tree.fbx", tree_flags);
                }
                else if (i == 1)
                {
                    mesh_rock = ResourceCache::Load<Mesh>("project/models/rock_2/model.obj");
                }
                else if (i == 2)
                {
                    mesh_grass_blade->SetObjectName("grass_blade");
                    mesh_grass_blade->SetFlag(static_cast<uint32_t>(MeshFlags::PostProcessOptimize), false);
                    uint32_t sub_mesh_index = 0;

                    {
                        vector<RHI_Vertex_PosTexNorTan> vertices;
                        vector<uint32_t> indices;
                        geometry_generation::generate_foliage_grass_blade(&vertices, &indices, 6);
                        mesh_grass_blade->AddGeometry(vertices, indices, false, &sub_mesh_index);
                    }

                    {
                        vector<RHI_Vertex_PosTexNorTan> vertices;
                        vector<uint32_t> indices;
                        geometry_generation::generate_foliage_grass_blade(&vertices, &indices, 3);
                        mesh_grass_blade->AddLod(vertices, indices, sub_mesh_index);
                    }

                    {
                        vector<RHI_Vertex_PosTexNorTan> vertices;
                        vector<uint32_t> indices;
                        geometry_generation::generate_foliage_grass_blade(&vertices, &indices, 1);
                        mesh_grass_blade->AddLod(vertices, indices, sub_mesh_index);
                    }

                    mesh_grass_blade->CreateGpuBuffers();
                }
                else if (i == 3)
                {
                    if (FileSystem::Exists(flower_cache_path))
                    {
                        mesh_flower->LoadFromFile(flower_cache_path);
                        if (mesh_flower->GetVertexCount() > 0)
                        {
                            continue;
                        }
                    }

                    mesh_flower->SetObjectName("flower");
                    mesh_flower->SetFlag(static_cast<uint32_t>(MeshFlags::PostProcessOptimize), false);
                    uint32_t sub_mesh_index = 0;

                    {
                        vector<RHI_Vertex_PosTexNorTan> vertices;
                        vector<uint32_t> indices;
                        geometry_generation::generate_foliage_flower(&vertices, &indices, 3, 6, 3);
                        mesh_flower->AddGeometry(vertices, indices, false, &sub_mesh_index);
                    }

                    {
                        vector<RHI_Vertex_PosTexNorTan> vertices;
                        vector<uint32_t> indices;
                        geometry_generation::generate_foliage_flower(&vertices, &indices, 2, 4, 2);
                        mesh_flower->AddLod(vertices, indices, sub_mesh_index);
                    }

                    {
                        vector<RHI_Vertex_PosTexNorTan> vertices;
                        vector<uint32_t> indices;
                        geometry_generation::generate_foliage_flower(&vertices, &indices, 1, 1, 1);
                        mesh_flower->AddLod(vertices, indices, sub_mesh_index);
                    }

                    mesh_flower->SetResourceFilePath(flower_cache_path);
                    mesh_flower->SaveToFile(flower_cache_path);
                    mesh_flower->CreateGpuBuffers();
                }
            }
        };

        ThreadPool::ParallelLoop(build_mesh, 4);
        SP_LOG_INFO("forest parallel mesh build took %d ms", static_cast<int>(sw_parallel_meshes.GetElapsedTimeMs()));

        // terrain root
        Entity* terrain_entity = World::CreateEntity();
        terrain_entity->SetObjectName("terrain");
        terrain_entity->SetParent(builder_entity);

        // audio
        {
            Entity* entity = World::CreateEntity();
            entity->SetObjectName("audio");
            entity->SetParent(builder_entity);

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
        Terrain* terrain = terrain_entity->AddComponent<Terrain>();
        {
            // terrain material
            {
                shared_ptr<Material> material = terrain->GetMaterial();
                material->SetResourceName("terrain" + string(EXTENSION_MATERIAL));
                material->SetProperty(MaterialProperty::IsTerrain, 1.0f);
                material->SetProperty(MaterialProperty::TextureTilingX, 2000.0f);
                material->SetProperty(MaterialProperty::TextureTilingY, 2000.0f);

                material->SetTexture(MaterialTextureType::Color,     "project/materials/whispy_grass_meadow/albedo.png",    0);
                material->SetTexture(MaterialTextureType::Normal,    "project/materials/whispy_grass_meadow/normal.png",    0);
                material->SetTexture(MaterialTextureType::Roughness, "project/materials/whispy_grass_meadow/roughness.png", 0);
                material->SetTexture(MaterialTextureType::Occlusion, "project/materials/whispy_grass_meadow/occlusion.png", 0);

                material->SetTexture(MaterialTextureType::Color,     "project/materials/rock/albedo.png",    1);
                material->SetTexture(MaterialTextureType::Normal,    "project/materials/rock/normal.png",    1);
                material->SetTexture(MaterialTextureType::Roughness, "project/materials/rock/roughness.png", 1);
                material->SetTexture(MaterialTextureType::Occlusion, "project/materials/rock/occlusion.png", 1);
                material->SetTexture(MaterialTextureType::Height,    "project/materials/rock/height.png",    1);

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

            // terrain physics
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
        create_water(Vector3::Zero);

        // props: trees, rocks, grass
        {
            // materials
            shared_ptr<Material> material_leaf;
            shared_ptr<Material> material_body;
            shared_ptr<Material> material_rock;
            shared_ptr<Material> material_grass_blade;
            shared_ptr<Material> material_flower;
            {
                material_leaf = make_shared<Material>();
                material_leaf->SetTexture(MaterialTextureType::Color, "project/models/tree/Twig_Base_Material_2.png");
                material_leaf->SetTexture(MaterialTextureType::Normal, "project/models/tree/Twig_Normal.png");
                material_leaf->SetTexture(MaterialTextureType::AlphaMask, "project/models/tree/Twig_Opacity_Map.jpg");
                material_leaf->SetProperty(MaterialProperty::WindAnimation, 1.0f);
                material_leaf->SetProperty(MaterialProperty::ColorVariationFromInstance, 1.0f);
                material_leaf->SetProperty(MaterialProperty::SubsurfaceScattering, 1.0f);
                material_leaf->SetResourceName("tree_leaf" + string(EXTENSION_MATERIAL));

                material_body = make_shared<Material>();
                material_body->SetTexture(MaterialTextureType::Color, "project/models/tree/tree_bark_diffuse.png");
                material_body->SetTexture(MaterialTextureType::Normal, "project/models/tree/tree_bark_normal.png");
                material_body->SetTexture(MaterialTextureType::Roughness, "project/models/tree/tree_bark_roughness.png");
                material_body->SetResourceName("tree_body" + string(EXTENSION_MATERIAL));

                material_rock = make_shared<Material>();
                material_rock->SetTexture(MaterialTextureType::Color, "project/models/rock_2/albedo.png");
                material_rock->SetTexture(MaterialTextureType::Normal, "project/models/rock_2/normal.png");
                material_rock->SetTexture(MaterialTextureType::Roughness, "project/models/rock_2/roughness.png");
                material_rock->SetTexture(MaterialTextureType::Occlusion, "project/models/rock_2/occlusion.png");
                material_rock->SetResourceName("rock" + string(EXTENSION_MATERIAL));

                material_grass_blade = make_shared<Material>();
                material_grass_blade->SetProperty(MaterialProperty::IsGrassBlade, 1.0f);
                material_grass_blade->SetProperty(MaterialProperty::Roughness, 1.0f);
                material_grass_blade->SetProperty(MaterialProperty::Clearcoat, 1.0f);
                material_grass_blade->SetProperty(MaterialProperty::Clearcoat_Roughness, 0.2f);
                material_grass_blade->SetProperty(MaterialProperty::SubsurfaceScattering, 1.0f);
                material_grass_blade->SetProperty(MaterialProperty::CullMode, static_cast<float>(RHI_CullMode::None));
                material_grass_blade->SetColor(Color::standard_white);
                material_grass_blade->SetResourceName("grass_blade" + string(EXTENSION_MATERIAL));

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

            // hand the grass material to the long-lived store so it outlives any single entity
            builder_materials.push_back(material_grass_blade);

            // place props on terrain tiles
            vector<Entity*> children = terrain->GetEntity()->GetChildren();

            vector<vector<Matrix>> tree_transforms_per_tile(children.size());
            vector<vector<Matrix>> rock_transforms_per_tile(children.size());

            auto place_props_on_tiles = [
                &children,
                &mesh_flower,
                &tree_transforms_per_tile,
                &rock_transforms_per_tile,
                &terrain,
                render_distance_foliage,
                per_triangle_density_flower,
                per_triangle_density_tree,
                per_triangle_density_rock,
                material_flower
            ](uint32_t start_index, uint32_t end_index)
            {
                for (uint32_t tile_index = start_index; tile_index < end_index; tile_index++)
                {
                    Entity* terrain_tile = children[tile_index];

                    const math::Matrix tile_world_matrix = terrain_tile->GetMatrix();

                    terrain->FindTransforms(tile_index, TerrainProp::Tree, nullptr, per_triangle_density_tree, 0.026f, tree_transforms_per_tile[tile_index]);
                    for (math::Matrix& t : tree_transforms_per_tile[tile_index])
                        t *= tile_world_matrix;

                    terrain->FindTransforms(tile_index, TerrainProp::Rock, nullptr, per_triangle_density_rock, 0.64f, rock_transforms_per_tile[tile_index]);
                    for (math::Matrix& t : rock_transforms_per_tile[tile_index])
                        t *= tile_world_matrix;

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
                        renderable->SetFlag(RenderableFlags::ExcludeFromRayTracing, true);
                        renderable->SetInstances(transforms);
                        renderable->SetMaterial(material_flower);
                        renderable->SetMaxRenderDistance(render_distance_foliage);
                    }
                }
            };

            ThreadPool::ParallelLoop(place_props_on_tiles, static_cast<uint32_t>(children.size()));

            // single tree entity for the whole world
            {
                size_t tree_total = 0;
                for (const auto& v : tree_transforms_per_tile) tree_total += v.size();
                vector<Matrix> all_tree_transforms;
                all_tree_transforms.reserve(tree_total);
                for (auto& v : tree_transforms_per_tile)
                    all_tree_transforms.insert(all_tree_transforms.end(), v.begin(), v.end());

                if (!all_tree_transforms.empty() && mesh_tree)
                {
                    Entity* entity = mesh_tree->GetRootEntity()->Clone();
                    entity->SetObjectName("tree");
                    entity->SetParent(terrain_entity);
                    entity->SetScale(math::Vector3::One);

                    vector<Entity*> tree_candidates;
                    tree_candidates.push_back(entity);
                    entity->GetDescendants(&tree_candidates);
                    uint32_t tree_renderable_count = 0;
                    for (Entity* candidate : tree_candidates)
                    {
                        Render* renderable = candidate->GetComponent<Render>();
                        if (!renderable || !renderable->GetMesh())
                        {
                            continue;
                        }

                        renderable->SetInstances(all_tree_transforms);
                        renderable->SetMaxRenderDistance(render_distance_trees);
                        renderable->SetMaxShadowDistance(shadow_distance);

                        Material* imported_material = renderable->GetMaterial();
                        const string imported_name  = imported_material ? imported_material->GetObjectName() : string();
                        const bool   is_bark        = imported_name.find("Bark") != string::npos || imported_name.find("bark") != string::npos;
                        renderable->SetMaterial(is_bark ? material_body : material_leaf);

                        if (is_bark)
                        {
                            Physics* physics = candidate->AddComponent<Physics>();
                            physics->SetBodyType(BodyType::Mesh);
                        }

                        tree_renderable_count++;
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

                if (!all_rock_transforms.empty() && mesh_rock)
                {
                    Entity* entity = mesh_rock->GetRootEntity()->Clone();
                    entity->SetObjectName("rock");
                    entity->SetParent(terrain_entity);
                    entity->SetScale(math::Vector3::One);

                    vector<Entity*> rock_candidates;
                    rock_candidates.push_back(entity);
                    entity->GetDescendants(&rock_candidates);
                    uint32_t rock_renderable_count = 0;
                    for (Entity* candidate : rock_candidates)
                    {
                        Render* renderable = candidate->GetComponent<Render>();
                        if (!renderable || !renderable->GetMesh())
                        {
                            continue;
                        }

                        renderable->SetInstances(all_rock_transforms);
                        renderable->SetMaxRenderDistance(render_distance_trees);
                        renderable->SetMaxShadowDistance(shadow_distance);
                        renderable->SetMaterial(material_rock);

                        if (rock_renderable_count == 0)
                        {
                            Physics* physics = candidate->AddComponent<Physics>();
                            physics->SetBodyType(BodyType::Mesh);
                        }

                        rock_renderable_count++;
                    }
                }
            }

            // procedural grass
            if (Terrain* terrain_component = terrain_entity->GetComponent<Terrain>())
            {
                if (RHI_Texture* heightmap = terrain_component->GetHeightMapFinal())
                {
                    Renderer::ProceduralGrassParams grass_params;
                    grass_params.ring_radii_m[0]  = 30.0f;
                    grass_params.ring_radii_m[1]  = 120.0f;
                    grass_params.ring_radii_m[2]  = render_distance_foliage;
                    grass_params.cell_size_m[0]   = 1.0f;
                    grass_params.cell_size_m[1]   = 3.0f;
                    grass_params.cell_size_m[2]   = 8.0f;
                    grass_params.height_min       = terrain_component->GetSeaLevel() + 1.0f;
                    grass_params.height_max       = terrain_component->GetSnowLevel();
                    grass_params.max_slope_deg    = 45.0f;
                    const float extent_x          = static_cast<float>(terrain_component->GetWidth()  - 1) * static_cast<float>(terrain_component->GetScale());
                    const float extent_z          = static_cast<float>(terrain_component->GetHeight() - 1) * static_cast<float>(terrain_component->GetScale());
                    grass_params.terrain_extent_m = Vector2(extent_x, extent_z);
                    Renderer::EnableProceduralGrass(mesh_grass_blade.get(), material_grass_blade.get(), heightmap, grass_params);
                }
            }
        }
    }

    void WorldHelpers::Clear()
    {
        // procedural grass references a mesh and a material owned by these vectors,
        // the caller is expected to disable it first so the renderer drops its references
        builder_meshes.clear();
        builder_materials.clear();
    }

    void WorldHelpers::RegisterForScripting(sol::state_view state)
    {
        // material texture types
        state.new_enum("MaterialTextureType",
            "Color",     MaterialTextureType::Color,
            "Roughness", MaterialTextureType::Roughness,
            "Metalness", MaterialTextureType::Metalness,
            "Normal",    MaterialTextureType::Normal,
            "Occlusion", MaterialTextureType::Occlusion,
            "Emission",  MaterialTextureType::Emission,
            "Height",    MaterialTextureType::Height,
            "AlphaMask", MaterialTextureType::AlphaMask,
            "Packed",    MaterialTextureType::Packed
        );

        // material properties (subset commonly needed by builders)
        state.new_enum("MaterialProperty",
            "WorldSpaceUv",               MaterialProperty::WorldSpaceUv,
            "Tessellation",               MaterialProperty::Tessellation,
            "Roughness",                  MaterialProperty::Roughness,
            "Metalness",                  MaterialProperty::Metalness,
            "Normal",                     MaterialProperty::Normal,
            "Height",                     MaterialProperty::Height,
            "Clearcoat",                  MaterialProperty::Clearcoat,
            "Clearcoat_Roughness",        MaterialProperty::Clearcoat_Roughness,
            "SubsurfaceScattering",       MaterialProperty::SubsurfaceScattering,
            "EmissiveFromAlbedo",         MaterialProperty::EmissiveFromAlbedo,
            "TextureTilingX",             MaterialProperty::TextureTilingX,
            "TextureTilingY",             MaterialProperty::TextureTilingY,
            "IsTerrain",                  MaterialProperty::IsTerrain,
            "IsGrassBlade",               MaterialProperty::IsGrassBlade,
            "IsFlower",                   MaterialProperty::IsFlower,
            "WindAnimation",              MaterialProperty::WindAnimation,
            "ColorVariationFromInstance", MaterialProperty::ColorVariationFromInstance,
            "IsWater",                    MaterialProperty::IsWater,
            "CullMode",                   MaterialProperty::CullMode
        );

        state.new_usertype<Material>("Material",
            "New",             sol::factories([]() { return make_shared<Material>(); }),
            "SetProperty",     &Material::SetProperty,
            "GetProperty",     &Material::GetProperty,
            "SetColor",        [](Material& self, float r, float g, float b, float a) { self.SetColor(Color(r, g, b, a)); },
            "SetTexture",      [](Material& self, MaterialTextureType type, const string& path, sol::optional<int> slot)
            {
                self.SetTexture(type, path, static_cast<uint8_t>(slot.value_or(0)));
            },
            "SetResourceName", &Material::SetResourceName
        );

        // resource cache helpers
        sol::table resource_cache = state.create_named_table("ResourceCache");
        resource_cache["LoadMesh"] = sol::overload(
            [](const string& path) -> Mesh*
            {
                shared_ptr<Mesh> mesh = ResourceCache::Load<Mesh>(path);
                return mesh ? mesh.get() : nullptr;
            },
            [](const string& path, uint32_t flags) -> Mesh*
            {
                shared_ptr<Mesh> mesh = ResourceCache::Load<Mesh>(path, flags);
                return mesh ? mesh.get() : nullptr;
            }
        );

        // forest builder
        sol::table forest = state.create_named_table("Forest");
        forest["Build"] = [](Entity* builder_entity) { WorldHelpers::BuildForest(builder_entity); };
    }
}
