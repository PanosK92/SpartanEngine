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
        Water* water_component = water->AddComponent<Water>();
        water_component->SetAmplitude(0.18f);
        water_component->SetChoppiness(0.30f);
        water_component->SetDisplacementScale(0.35f);
        water_component->SetNormalStrength(0.65f);
        water_component->SetTurbidity(1.20f);
        water_component->SetCausticsIntensity(0.40f);

        return water;
    }

    void WorldHelpers::BuildForest(Entity* builder_entity)
    {
        // pre-size the global geometry buffer high enough for the whole forest so worker threads streaming
        // mesh data in cannot trip a mid-load rebuild from the renderer's per-frame BuildIfDirty
        GeometryBuffer::Reserve(
            12u * 1024u * 1024u, // ~12M vertices
            32u * 1024u * 1024u, // ~32M indices
            128u * 1024u,        // ~128K meshlet bounds
            10u * 1024u * 1024u, // ~10M unique verts (~index/3)
            32u * 1024u * 1024u, // ~32M micro indices (~index count)
            256u * 1024u         // ~256K instances
        );

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
            // terrain material, the layer set is built by the component from project/materials
            terrain->ApplyDefaultMaterial();

            // height map generation
            shared_ptr<RHI_Texture> height_map = ResourceCache::Load<RHI_Texture>("project/height_maps/height_map.png");
            if (height_map)
            {
                height_map->PrepareForGpu();
            }
            terrain->SetHeightMapSeed(height_map.get());
            // generate also stands up the static heightfield collision for the whole surface
            terrain->Generate();
        }

        // water
        create_water(Vector3::Zero);

        // generate already calls PopulateTerrainBiomeProps when spawn_biome_props is on
    }

    void WorldHelpers::PopulateTerrainBiomeProps(Terrain* terrain)
    {
        if (!terrain || !terrain->GetEntity())
        {
            return;
        }

        Entity* terrain_entity = terrain->GetEntity();

        // drop previous biome prop roots so reloads do not stack instances
        {
            vector<Entity*> children = terrain_entity->GetChildren();
            for (Entity* child : children)
            {
                if (!child)
                {
                    continue;
                }
                const string& name = child->GetObjectName();
                if (name == "tree" || name == "rock" || name == "flower" || name.find("flower") == 0)
                {
                    World::RemoveEntity(child);
                }
            }
            // tile-parented flowers from older builds
            for (Entity* tile : terrain_entity->GetChildren())
            {
                if (!tile)
                {
                    continue;
                }
                vector<Entity*> tile_children = tile->GetChildren();
                for (Entity* child : tile_children)
                {
                    if (child && child->GetObjectName() == "flower")
                    {
                        World::RemoveEntity(child);
                    }
                }
            }
        }

        if (!terrain->GetSpawnBiomeProps())
        {
            Renderer::DisableProceduralGrass();
            return;
        }

        const float render_distance_trees   = 2'000.0f;
        const float render_distance_foliage = 500.0f;
        const float shadow_distance         = 150.0f;
        const float per_triangle_density_flower = 0.28f;
        const float per_triangle_density_tree   = 0.008f;
        const float per_triangle_density_rock   = 0.0012f;

        const uint32_t tree_flags = Mesh::GetDefaultFlags() | static_cast<uint32_t>(MeshFlags::ImportCombineMeshes);
        shared_ptr<Mesh> mesh_tree = ResourceCache::Load<Mesh>("project/models/tree/tree.fbx", tree_flags);
        shared_ptr<Mesh> mesh_rock = ResourceCache::Load<Mesh>("project/models/rock_2/model.obj");
        if (!mesh_rock)
        {
            SP_LOG_WARNING("biome props: rock mesh missing at project/models/rock_2/model.obj");
        }

        shared_ptr<Mesh> mesh_grass_blade = builder_meshes.emplace_back(make_shared<Mesh>());
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

        shared_ptr<Mesh> mesh_flower = builder_meshes.emplace_back(make_shared<Mesh>());
        {
            const string flower_cache_path = string(ResourceCache::GetProjectDirectory()) + "standard_flower" + EXTENSION_MESH;
            if (FileSystem::Exists(flower_cache_path))
            {
                mesh_flower->LoadFromFile(flower_cache_path);
            }
            if (mesh_flower->GetVertexCount() == 0)
            {
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

        shared_ptr<Material> material_leaf = make_shared<Material>();
        material_leaf->SetTexture(MaterialTextureType::Color, "project/models/tree/Twig_Base_Material_2.png");
        material_leaf->SetTexture(MaterialTextureType::Normal, "project/models/tree/Twig_Normal.png");
        material_leaf->SetTexture(MaterialTextureType::AlphaMask, "project/models/tree/Twig_Opacity_Map.jpg");
        material_leaf->SetProperty(MaterialProperty::WindAnimation, 1.0f);
        material_leaf->SetProperty(MaterialProperty::ColorVariationFromInstance, 1.0f);
        material_leaf->SetProperty(MaterialProperty::SubsurfaceScattering, 1.0f);
        material_leaf->SetResourceName("tree_leaf" + string(EXTENSION_MATERIAL));

        shared_ptr<Material> material_body = make_shared<Material>();
        material_body->SetTexture(MaterialTextureType::Color, "project/models/tree/tree_bark_diffuse.png");
        material_body->SetTexture(MaterialTextureType::Normal, "project/models/tree/tree_bark_normal.png");
        material_body->SetTexture(MaterialTextureType::Roughness, "project/models/tree/tree_bark_roughness.png");
        material_body->SetResourceName("tree_body" + string(EXTENSION_MATERIAL));

        shared_ptr<Material> material_rock = make_shared<Material>();
        material_rock->SetTexture(MaterialTextureType::Color, "project/models/rock_2/albedo.png");
        material_rock->SetTexture(MaterialTextureType::Normal, "project/models/rock_2/normal.png");
        material_rock->SetTexture(MaterialTextureType::Roughness, "project/models/rock_2/roughness.png");
        material_rock->SetTexture(MaterialTextureType::Occlusion, "project/models/rock_2/occlusion.png");
        material_rock->SetResourceName("rock" + string(EXTENSION_MATERIAL));

        shared_ptr<Material> material_grass_blade = make_shared<Material>();
        material_grass_blade->SetProperty(MaterialProperty::IsGrassBlade, 1.0f);
        material_grass_blade->SetProperty(MaterialProperty::Roughness, 0.85f);
        material_grass_blade->SetProperty(MaterialProperty::Clearcoat, 0.0f);
        material_grass_blade->SetProperty(MaterialProperty::Clearcoat_Roughness, 0.5f);
        material_grass_blade->SetProperty(MaterialProperty::SubsurfaceScattering, 0.35f);
        material_grass_blade->SetProperty(MaterialProperty::CullMode, static_cast<float>(RHI_CullMode::None));
        material_grass_blade->SetColor(Color::standard_white);
        material_grass_blade->SetResourceName("grass_blade" + string(EXTENSION_MATERIAL));
        material_grass_blade = ResourceCache::Cache(material_grass_blade);
        builder_materials.push_back(material_grass_blade);

        shared_ptr<Material> material_flower = make_shared<Material>();
        material_flower->SetProperty(MaterialProperty::IsFlower, 1.0f);
        material_flower->SetProperty(MaterialProperty::Roughness, 1.0f);
        material_flower->SetProperty(MaterialProperty::Clearcoat, 1.0f);
        material_flower->SetProperty(MaterialProperty::Clearcoat_Roughness, 0.2f);
        material_flower->SetProperty(MaterialProperty::SubsurfaceScattering, 0.0f);
        material_flower->SetProperty(MaterialProperty::CullMode, static_cast<float>(RHI_CullMode::None));
        material_flower->SetColor(Color::standard_white);
        material_flower->SetResourceName("flower" + string(EXTENSION_MATERIAL));

        vector<Entity*> children = terrain_entity->GetChildren();
        const uint32_t tile_axis  = max(terrain->GetTileCountAxis(), 1u);
        const uint32_t tile_count = tile_axis * tile_axis;
        vector<Entity*> tiles(tile_count, nullptr);
        for (Entity* child : children)
        {
            const int tile_index = Terrain::ParseTileIndex(child);
            if (tile_index >= 0 && static_cast<uint32_t>(tile_index) < tile_count)
            {
                tiles[static_cast<uint32_t>(tile_index)] = child;
            }
        }

        vector<vector<Matrix>> tree_transforms_per_tile(tile_count);
        vector<vector<Matrix>> rock_transforms_per_tile(tile_count);

        auto place_props_on_tiles = [
            &tiles,
            &mesh_flower,
            &tree_transforms_per_tile,
            &rock_transforms_per_tile,
            terrain,
            render_distance_foliage,
            per_triangle_density_flower,
            per_triangle_density_tree,
            per_triangle_density_rock,
            material_flower
        ](uint32_t start_index, uint32_t end_index)
        {
            for (uint32_t tile_index = start_index; tile_index < end_index; tile_index++)
            {
                Entity* terrain_tile = tiles[tile_index];
                if (!terrain_tile)
                {
                    continue;
                }
                const math::Matrix tile_world_matrix = terrain_tile->GetMatrix();

                terrain->FindTransforms(tile_index, TerrainProp::Tree, nullptr, per_triangle_density_tree, 0.026f, tree_transforms_per_tile[tile_index]);
                for (math::Matrix& t : tree_transforms_per_tile[tile_index])
                {
                    t *= tile_world_matrix;
                }

                terrain->FindTransforms(tile_index, TerrainProp::Rock, nullptr, per_triangle_density_rock, 0.64f, rock_transforms_per_tile[tile_index]);
                for (math::Matrix& t : rock_transforms_per_tile[tile_index])
                {
                    t *= tile_world_matrix;
                }

                Entity* entity = World::CreateEntity();
                entity->SetObjectName("flower");
                entity->SetParent(terrain_tile);

                vector<Matrix> transforms;
                terrain->FindTransforms(tile_index, TerrainProp::Flower, entity, per_triangle_density_flower, 0.64f, transforms);

                Render* render = entity->AddComponent<Render>();
                render->SetMesh(mesh_flower.get());
                render->SetFlag(RenderFlags::CastsShadows, false);
                render->SetFlag(RenderFlags::ExcludeFromRayTracing, true);
                render->SetInstances(transforms);
                render->SetMaterial(material_flower);
                render->SetMaxRenderDistance(render_distance_foliage);
            }
        };

        ThreadPool::ParallelLoop(place_props_on_tiles, tile_count);

        // single tree entity for the whole world
        {
            size_t tree_total = 0;
            for (const auto& v : tree_transforms_per_tile)
            {
                tree_total += v.size();
            }
            vector<Matrix> all_tree_transforms;
            all_tree_transforms.reserve(tree_total);
            for (auto& v : tree_transforms_per_tile)
            {
                all_tree_transforms.insert(all_tree_transforms.end(), v.begin(), v.end());
            }

            if (!all_tree_transforms.empty() && mesh_tree && mesh_tree->GetRootEntity())
            {
                SP_LOG_INFO("biome props: placing %zu trees", all_tree_transforms.size());
                mesh_tree->GetRootEntity()->SetTransient(true);
                Entity* entity = mesh_tree->GetRootEntity()->Clone();
                entity->SetObjectName("tree");
                entity->SetParent(terrain_entity);
                entity->SetScale(math::Vector3::One);

                vector<Entity*> tree_candidates;
                tree_candidates.push_back(entity);
                entity->GetDescendants(&tree_candidates);
                for (Entity* candidate : tree_candidates)
                {
                    Render* render = candidate->GetComponent<Render>();
                    if (!render || !render->GetMesh())
                    {
                        continue;
                    }

                    render->SetInstances(all_tree_transforms);
                    render->SetMaxRenderDistance(render_distance_trees);
                    render->SetMaxShadowDistance(shadow_distance);

                    Material* imported_material = render->GetMaterial();
                    const string imported_name  = imported_material ? imported_material->GetObjectName() : string();
                    const bool is_bark = imported_name.find("Bark") != string::npos || imported_name.find("bark") != string::npos;
                    render->SetMaterial(is_bark ? material_body : material_leaf);

                    if (is_bark)
                    {
                        Physics* physics = candidate->AddComponent<Physics>();
                        physics->SetBodyType(BodyType::Mesh);
                    }
                }
            }
        }

        // single rock entity for the whole world
        {
            size_t rock_total = 0;
            for (const auto& v : rock_transforms_per_tile)
            {
                rock_total += v.size();
            }
            vector<Matrix> all_rock_transforms;
            all_rock_transforms.reserve(rock_total);
            for (auto& v : rock_transforms_per_tile)
            {
                all_rock_transforms.insert(all_rock_transforms.end(), v.begin(), v.end());
            }

            if (!all_rock_transforms.empty() && mesh_rock && mesh_rock->GetRootEntity())
            {
                SP_LOG_INFO("biome props: placing %zu rocks", all_rock_transforms.size());
                mesh_rock->GetRootEntity()->SetTransient(true);
                Entity* entity = mesh_rock->GetRootEntity()->Clone();
                entity->SetObjectName("rock");
                entity->SetParent(terrain_entity);
                entity->SetScale(math::Vector3::One);

                vector<Entity*> rock_candidates;
                rock_candidates.push_back(entity);
                entity->GetDescendants(&rock_candidates);
                uint32_t rock_render_count = 0;
                for (Entity* candidate : rock_candidates)
                {
                    Render* render = candidate->GetComponent<Render>();
                    if (!render || !render->GetMesh())
                    {
                        continue;
                    }

                    render->SetInstances(all_rock_transforms);
                    render->SetMaxRenderDistance(render_distance_trees);
                    render->SetMaxShadowDistance(shadow_distance);
                    render->SetMaterial(material_rock);

                    if (rock_render_count == 0)
                    {
                        Physics* physics = candidate->AddComponent<Physics>();
                        physics->SetBodyType(BodyType::Mesh);
                    }
                    rock_render_count++;
                }

                if (rock_render_count == 0)
                {
                    SP_LOG_WARNING("biome props: rock mesh has no renderables, spawned %zu transforms unused", all_rock_transforms.size());
                }
            }
            else if (!all_rock_transforms.empty())
            {
                SP_LOG_WARNING("biome props: %zu rock transforms but mesh/root missing", all_rock_transforms.size());
            }
            else
            {
                SP_LOG_INFO("biome props: 0 rocks placed");
            }
        }

        // gpu procedural grass, gated by the grass channel of the biome prop mask
        // r32 world heights, r8 preview only has ~3m steps over a tall island
        if (RHI_Texture* heightmap = terrain->GetHeightMapGpu())
        {
            Renderer::ProceduralGrassParams grass_params;
            grass_params.ring_radii_m[0]  = 30.0f;
            grass_params.ring_radii_m[1]  = 120.0f;
            grass_params.ring_radii_m[2]  = render_distance_foliage;
            grass_params.cell_size_m[0]   = 0.55f;
            grass_params.cell_size_m[1]   = 1.6f;
            grass_params.cell_size_m[2]   = 4.5f;
            grass_params.height_min       = terrain->GetSeaLevel() + 1.0f;
            grass_params.height_max       = terrain->GetSnowLevel();
            grass_params.max_slope_deg    = 48.0f;
            grass_params.biome_min_weight = 0.35f;
            grass_params.terrain_world_mapping = terrain->GetWorldMapping();
            const float extent_x = static_cast<float>(terrain->GetWidth()  - 1) * static_cast<float>(terrain->GetScale());
            const float extent_z = static_cast<float>(terrain->GetHeight() - 1) * static_cast<float>(terrain->GetScale());
            grass_params.terrain_extent_m = Vector2(extent_x, extent_z);
            Renderer::EnableProceduralGrass(
                mesh_grass_blade.get(),
                material_grass_blade.get(),
                heightmap,
                grass_params,
                terrain->GetPropMask()
            );
        }
        else
        {
            SP_LOG_WARNING("biome props: missing terrain r32 height map, gpu grass disabled");
            Renderer::DisableProceduralGrass();
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
            "MotionBlurRadial",           MaterialProperty::MotionBlurRadial,
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
