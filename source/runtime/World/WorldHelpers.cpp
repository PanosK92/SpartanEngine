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

        // spawned props carry this, the mesh prototypes they are cloned from never do
        const char* terrain_prop_tag = "terrain_prop";

        bool is_terrain_prop_name(const string& name)
        {
            return name == "tree" || name == "rock" || name == "flower";
        }

        // stops at the first match, removal already takes the descendants with it
        //
        // the name test is only trusted inside a terrain subtree, the tree and rock mesh prototypes sit
        // at world root under the same names and deleting one of those would take the source with it
        void collect_terrain_prop_roots(Entity* entity, bool inside_terrain, vector<Entity*>& out)
        {
            if (!entity)
            {
                return;
            }

            if (entity->HasTag(terrain_prop_tag) ||
                (inside_terrain && is_terrain_prop_name(entity->GetObjectName())))
            {
                out.push_back(entity);
                return;
            }

            inside_terrain = inside_terrain || entity->GetComponent<Terrain>() != nullptr;

            for (Entity* child : entity->GetChildren())
            {
                collect_terrain_prop_roots(child, inside_terrain, out);
            }
        }
    }

    // props are tile children now, so tile teardown takes them with it, but a world saved before that
    // holds prop roots parked elsewhere in the hierarchy, this sweeps the whole world so a regenerate
    // can never inherit a stale set
    //
    // the walk goes through the scene graph rather than World::GetEntities, that list does not include
    // entities created this frame and props spawned by a previous generate can still be sitting in it
    void WorldHelpers::RemoveTerrainProps()
    {
        vector<Entity*> roots;
        World::GetRootEntities(roots);

        vector<Entity*> doomed;
        doomed.reserve(64);
        for (Entity* root : roots)
        {
            collect_terrain_prop_roots(root, false, doomed);
        }

        for (Entity* entity : doomed)
        {
            World::RemoveEntity(entity);
        }

        // grass has no entities to sweep, it is a renderer pass fed by the height map and the prop
        // mask, so removing props has to turn it off explicitly or it keeps drawing over bare terrain
        Renderer::DisableProceduralGrass();

        if (!doomed.empty())
        {
            SP_LOG_INFO("biome props: removed %zu stale prop roots", doomed.size());
        }
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
        RemoveTerrainProps();

        if (!terrain->GetSpawnBiomeProps())
        {
            Renderer::DisableProceduralGrass();
            return;
        }

        const float render_distance_trees   = 2'000.0f;
        const float render_distance_foliage = 500.0f;
        const float shadow_distance         = 150.0f;
        // authored base densities, the terrain multipliers scale these so a world can be retuned
        // without a rebuild, see Terrain::SetPropDensityTree and friends
        const float per_triangle_density_flower = 0.28f   * terrain->GetPropDensityFlower();
        const float per_triangle_density_tree   = 0.008f  * terrain->GetPropDensityTree();
        const float per_triangle_density_rock   = 0.0012f * terrain->GetPropDensityRock();

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
        vector<vector<Matrix>> flower_transforms_per_tile(tile_count);

        // placement is the expensive part and it only touches per tile buckets, the entities it feeds
        // are built afterwards on this thread so the scene graph is only ever mutated in one order
        // the transforms stay tile local, every prop is parented to its tile and inherits its offset
        auto compute_transforms = [
            &tiles,
            &tree_transforms_per_tile,
            &rock_transforms_per_tile,
            &flower_transforms_per_tile,
            terrain,
            per_triangle_density_flower,
            per_triangle_density_tree,
            per_triangle_density_rock
        ](uint32_t start_index, uint32_t end_index)
        {
            for (uint32_t tile_index = start_index; tile_index < end_index; tile_index++)
            {
                if (!tiles[tile_index])
                {
                    continue;
                }

                terrain->FindTransforms(tile_index, TerrainProp::Tree,   nullptr, per_triangle_density_tree,   0.026f, tree_transforms_per_tile[tile_index]);
                terrain->FindTransforms(tile_index, TerrainProp::Rock,   nullptr, per_triangle_density_rock,   0.64f,  rock_transforms_per_tile[tile_index]);
                terrain->FindTransforms(tile_index, TerrainProp::Flower, nullptr, per_triangle_density_flower, 0.64f,  flower_transforms_per_tile[tile_index]);
            }
        };

        ThreadPool::ParallelLoop(compute_transforms, tile_count);

        // clones a prop prototype onto one tile and hands that tile's instances to every renderable it
        // carries, the prototype hierarchy is kept because bark and leaves need different materials
        auto attach_prop_to_tile = [](
            Entity* tile,
            Entity* prototype,
            const char* name,
            const vector<Matrix>& transforms,
            float render_distance,
            float shadow_distance_in,
            const function<void(Entity*, Render*, bool)>& configure
        ) -> uint32_t
        {
            if (!tile || !prototype || transforms.empty())
            {
                return 0;
            }

            Entity* entity = prototype->Clone();
            entity->SetObjectName(name);
            entity->SetTransient(true);
            entity->AddTag(terrain_prop_tag);
            entity->SetParent(tile);
            entity->SetPositionLocal(Vector3::Zero);
            entity->SetRotationLocal(Quaternion::Identity);
            entity->SetScaleLocal(Vector3::One);

            vector<Entity*> candidates;
            candidates.push_back(entity);
            entity->GetDescendants(&candidates);

            uint32_t render_count = 0;
            for (Entity* candidate : candidates)
            {
                candidate->SetTransient(true);

                Render* render = candidate->GetComponent<Render>();
                if (!render || !render->GetMesh())
                {
                    continue;
                }

                render->SetInstances(transforms);
                render->SetMaxRenderDistance(render_distance);
                render->SetMaxShadowDistance(shadow_distance_in);
                configure(candidate, render, render_count == 0);
                render_count++;
            }

            return render_count;
        };

        size_t tree_total   = 0;
        size_t rock_total   = 0;
        uint32_t rock_tiles = 0;

        for (uint32_t tile_index = 0; tile_index < tile_count; tile_index++)
        {
            Entity* terrain_tile = tiles[tile_index];
            if (!terrain_tile)
            {
                continue;
            }

            // trees
            if (mesh_tree && mesh_tree->GetRootEntity())
            {
                mesh_tree->GetRootEntity()->SetTransient(true);
                const uint32_t placed = attach_prop_to_tile(
                    terrain_tile,
                    mesh_tree->GetRootEntity(),
                    "tree",
                    tree_transforms_per_tile[tile_index],
                    render_distance_trees,
                    shadow_distance,
                    [&material_body, &material_leaf](Entity* candidate, Render* render, bool)
                    {
                        Material* imported     = render->GetMaterial();
                        const string imported_name = imported ? imported->GetObjectName() : string();
                        const bool is_bark     = imported_name.find("Bark") != string::npos ||
                                                 imported_name.find("bark") != string::npos;
                        render->SetMaterial(is_bark ? material_body : material_leaf);

                        // only the trunk is collidable, twigs and leaves would just snag the player
                        if (is_bark)
                        {
                            Physics* physics = candidate->AddComponent<Physics>();
                            physics->SetUseConvexHull(true);
                            physics->SetBodyType(BodyType::Mesh);
                        }
                    }
                );

                if (placed > 0)
                {
                    tree_total += tree_transforms_per_tile[tile_index].size();
                }
            }

            // rocks
            if (mesh_rock && mesh_rock->GetRootEntity())
            {
                mesh_rock->GetRootEntity()->SetTransient(true);
                const uint32_t placed = attach_prop_to_tile(
                    terrain_tile,
                    mesh_rock->GetRootEntity(),
                    "rock",
                    rock_transforms_per_tile[tile_index],
                    render_distance_trees,
                    shadow_distance,
                    [&material_rock](Entity* candidate, Render* render, bool is_first)
                    {
                        render->SetMaterial(material_rock);

                        // a boulder is close enough to convex that a hull matches the silhouette
                        if (is_first)
                        {
                            Physics* physics = candidate->AddComponent<Physics>();
                            physics->SetUseConvexHull(true);
                            physics->SetBodyType(BodyType::Mesh);
                        }
                    }
                );

                if (placed > 0)
                {
                    rock_total += rock_transforms_per_tile[tile_index].size();
                    rock_tiles++;
                }
            }

            // flowers, a generated mesh with one material so it needs no prototype hierarchy
            const vector<Matrix>& flowers = flower_transforms_per_tile[tile_index];
            if (!flowers.empty() && mesh_flower)
            {
                Entity* entity = World::CreateEntity();
                entity->SetObjectName("flower");
                entity->SetTransient(true);
                entity->AddTag(terrain_prop_tag);
                entity->SetParent(terrain_tile);

                Render* render = entity->AddComponent<Render>();
                render->SetMesh(mesh_flower.get());
                render->SetFlag(RenderFlags::CastsShadows, false);
                render->SetFlag(RenderFlags::ExcludeFromRayTracing, true);
                render->SetInstances(flowers);
                render->SetMaterial(material_flower);
                render->SetMaxRenderDistance(render_distance_foliage);
            }
        }

        SP_LOG_INFO("biome props: placed %zu trees and %zu rocks across %u tiles", tree_total, rock_total, tile_count);

        // rocks have three independent ways to end up invisible, separate them so one run says which
        {
            size_t rock_transforms = 0;
            for (const vector<Matrix>& v : rock_transforms_per_tile)
            {
                rock_transforms += v.size();
            }

            const bool has_mesh = mesh_rock && mesh_rock->GetRootEntity();
            uint32_t mesh_renderables = 0;
            if (has_mesh)
            {
                vector<Entity*> parts;
                parts.push_back(mesh_rock->GetRootEntity());
                mesh_rock->GetRootEntity()->GetDescendants(&parts);
                for (Entity* part : parts)
                {
                    Render* render = part->GetComponent<Render>();
                    if (render && render->GetMesh())
                    {
                        mesh_renderables++;
                    }
                }
            }

            if (!has_mesh)
            {
                SP_LOG_WARNING("biome props: rock mesh failed to load from project/models/rock_2/model.obj");
            }
            else if (mesh_renderables == 0)
            {
                SP_LOG_WARNING("biome props: rock mesh loaded but carries no renderable, %zu transforms unused", rock_transforms);
            }
            else if (rock_transforms == 0)
            {
                SP_LOG_WARNING("biome props: rock mesh is fine but the prop mask rock channel placed nothing, raise the rock layer weight or lower prop_mask_min");
            }
            else
            {
                SP_LOG_INFO("biome props: rock mesh has %u renderables, %u tiles carry rocks", mesh_renderables, rock_tiles);
            }
        }

        // gpu procedural grass, gated by the grass channel of the biome prop mask
        // r32 local heights, the populate pass adds the terrain entity y each frame
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
