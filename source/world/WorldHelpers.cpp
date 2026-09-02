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
#include "components/Render.h"
#include "components/Physics.h"
#include "components/AudioSource.h"
#include "components/Terrain.h"
#include "components/Water.h"
#include "components/Camera.h"
#include "../core/ThreadPool.h"
#include "../core/Stopwatch.h"
#include "../rendering/Renderer.h"
#include "../rendering/Material.h"
#include "../rendering/GeometryBuffer.h"
#include "../resource/ResourceCache.h"
#include "../geometry/Mesh.h"
#include "../geometry/GeometryGeneration.h"
#include "../geometry/GeometryProcessing.h"
#include "../rhi/RHI_Texture.h"
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

        // grass and micro detail have no entities to sweep, they are renderer passes fed by the height
        // map and the prop mask, so removing props has to turn them off or they keep drawing over bare terrain
        Renderer::DisableGpuScatter();

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
                audio_source->SetAudioClip("project/music/ambient/forest_river.wav");
                audio_source->SetLoop(true);
            }

            // wind
            {
                Entity* sound = World::CreateEntity();
                sound->SetObjectName("wind");
                sound->SetParent(entity);
                AudioSource* audio_source = sound->AddComponent<AudioSource>();
                audio_source->SetAudioClip("project/music/ambient/wind.wav");
                audio_source->SetLoop(true);
            }

            // underwater
            {
                Entity* sound = World::CreateEntity();
                sound->SetObjectName("underwater");
                sound->SetParent(entity);
                AudioSource* audio_source = sound->AddComponent<AudioSource>();
                audio_source->SetAudioClip("project/music/ambient/underwater.wav");
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

    namespace
    {
        // generated foliage meshes and folder materials, cached so a respawn does not rebuild them
        unordered_map<string, shared_ptr<Mesh>> builtin_meshes;
        unordered_map<string, shared_ptr<Material>> scatter_materials;

        shared_ptr<Mesh> build_grass_blade_mesh()
        {
            shared_ptr<Mesh> mesh = make_shared<Mesh>();
            mesh->SetObjectName("grass_blade");
            mesh->SetFlag(static_cast<uint32_t>(MeshFlags::PostProcessOptimize), false);

            const uint32_t segments[3] = { 6, 3, 1 };
            uint32_t sub_mesh_index    = 0;
            for (uint32_t lod = 0; lod < 3; lod++)
            {
                vector<RHI_Vertex_PosTexNorTan> vertices;
                vector<uint32_t> indices;
                geometry_generation::generate_foliage_grass_blade(&vertices, &indices, segments[lod]);

                if (lod == 0)
                {
                    mesh->AddGeometry(vertices, indices, false, &sub_mesh_index);
                }
                else
                {
                    mesh->AddLod(vertices, indices, sub_mesh_index);
                }
            }
            mesh->CreateGpuBuffers();

            return mesh;
        }

        // a stone chip, the smallest thing the gpu rings scatter, three lods because the near ring
        // carries thousands of them and the far ring only needs a silhouette
        shared_ptr<Mesh> build_pebble_mesh()
        {
            shared_ptr<Mesh> mesh = make_shared<Mesh>();
            mesh->SetObjectName("pebble");
            mesh->SetFlag(static_cast<uint32_t>(MeshFlags::PostProcessOptimize), false);

            const uint32_t subdivisions[3] = { 1, 0, 0 };
            uint32_t sub_mesh_index        = 0;
            for (uint32_t lod = 0; lod < 3; lod++)
            {
                vector<RHI_Vertex_PosTexNorTan> vertices;
                vector<uint32_t> indices;
                geometry_generation::generate_pebble(&vertices, &indices, subdivisions[lod], 7u);

                if (lod == 0)
                {
                    mesh->AddGeometry(vertices, indices, false, &sub_mesh_index);
                }
                else
                {
                    mesh->AddLod(vertices, indices, sub_mesh_index);
                }
            }
            mesh->CreateGpuBuffers();

            return mesh;
        }

        shared_ptr<Mesh> build_flower_mesh()
        {
            shared_ptr<Mesh> mesh   = make_shared<Mesh>();
            const string cache_path = string(ResourceCache::GetProjectDirectory()) + "models/flower/standard_flower" + EXTENSION_MESH;
            if (FileSystem::Exists(cache_path))
            {
                mesh->LoadFromFile(cache_path);
            }
            if (mesh->GetVertexCount() > 0)
            {
                return mesh;
            }

            mesh->SetObjectName("flower");
            mesh->SetFlag(static_cast<uint32_t>(MeshFlags::PostProcessOptimize), false);

            const uint32_t petals[3] = { 3, 2, 1 };
            const uint32_t rings[3]  = { 6, 4, 1 };
            const uint32_t stems[3]  = { 3, 2, 1 };
            uint32_t sub_mesh_index  = 0;
            for (uint32_t lod = 0; lod < 3; lod++)
            {
                vector<RHI_Vertex_PosTexNorTan> vertices;
                vector<uint32_t> indices;
                geometry_generation::generate_foliage_flower(&vertices, &indices, petals[lod], rings[lod], stems[lod]);

                if (lod == 0)
                {
                    mesh->AddGeometry(vertices, indices, false, &sub_mesh_index);
                }
                else
                {
                    mesh->AddLod(vertices, indices, sub_mesh_index);
                }
            }
            mesh->SetResourceFilePath(cache_path);
            FileSystem::CreateDirectory_(FileSystem::GetDirectoryFromFilePath(cache_path));
            mesh->SaveToFile(cache_path);
            mesh->CreateGpuBuffers();

            return mesh;
        }

        // builtin/ is one of the generated foliage meshes, anything else is an asset path
        Mesh* resolve_scatter_mesh(const string& path)
        {
            if (path.rfind("builtin/", 0) == 0)
            {
                auto it = builtin_meshes.find(path);
                if (it != builtin_meshes.end())
                {
                    return it->second.get();
                }

                shared_ptr<Mesh> mesh;
                if (path == "builtin/grass_blade")
                {
                    mesh = build_grass_blade_mesh();
                }
                else if (path == "builtin/flower")
                {
                    mesh = build_flower_mesh();
                }
                else if (path == "builtin/pebble")
                {
                    mesh = build_pebble_mesh();
                }

                if (!mesh)
                {
                    return nullptr;
                }

                builder_meshes.push_back(mesh);
                builtin_meshes[path] = mesh;

                return mesh.get();
            }

            const uint32_t flags  = Mesh::GetDefaultFlags() | static_cast<uint32_t>(MeshFlags::ImportCombineMeshes);
            shared_ptr<Mesh> mesh = ResourceCache::Load<Mesh>(path, flags);

            return mesh ? mesh.get() : nullptr;
        }

        // one material from a folder of conventionally named textures, the same convention the
        // surface layers use, this is how a scatter layer overrides whatever the asset imported
        shared_ptr<Material> resolve_folder_material(const string& folder)
        {
            auto it = scatter_materials.find(folder);
            if (it != scatter_materials.end())
            {
                return it->second;
            }

            const string base   = folder.back() == '/' ? folder : folder + "/";
            const string albedo = base + "albedo.png";
            if (!FileSystem::Exists(albedo))
            {
                SP_LOG_WARNING("terrain scatter: no albedo.png in %s", base.c_str());
                return nullptr;
            }

            shared_ptr<Material> material = make_shared<Material>();
            material->SetPersistent(false);
            material->SetTexture(MaterialTextureType::Color, albedo);

            auto set_optional = [&material, &base](MaterialTextureType type, const char* file)
            {
                const string path = base + file;
                if (FileSystem::Exists(path))
                {
                    material->SetTexture(type, path);
                }
            };
            set_optional(MaterialTextureType::Normal,    "normal.png");
            set_optional(MaterialTextureType::Roughness, "roughness.png");
            set_optional(MaterialTextureType::Occlusion, "occlusion.png");
            set_optional(MaterialTextureType::Height,    "height.png");
            set_optional(MaterialTextureType::AlphaMask, "alpha_mask.png");

            string name        = base.substr(0, base.size() - 1);
            const size_t slash = name.find_last_of('/');
            if (slash != string::npos)
            {
                name = name.substr(slash + 1);
            }
            material->SetObjectName(name);
            material->SetResourceName(name + string(EXTENSION_MATERIAL));

            scatter_materials[folder] = material;
            builder_materials.push_back(material);

            return material;
        }

        // the generated foliage meshes carry no imported material, they need the shader flags that
        // make a blade bend and a flower face the light
        shared_ptr<Material> resolve_builtin_material(const string& mesh_path)
        {
            auto it = scatter_materials.find(mesh_path);
            if (it != scatter_materials.end())
            {
                return it->second;
            }

            shared_ptr<Material> material = make_shared<Material>();
            material->SetPersistent(false);
            material->SetColor(Color::standard_white);
            material->SetProperty(MaterialProperty::CullMode, static_cast<float>(RHI_CullMode::None));

            if (mesh_path == "builtin/grass_blade")
            {
                material->SetProperty(MaterialProperty::IsGrassBlade, 1.0f);
                material->SetProperty(MaterialProperty::Roughness, 0.85f);
                material->SetProperty(MaterialProperty::Clearcoat, 0.0f);
                material->SetProperty(MaterialProperty::Clearcoat_Roughness, 0.5f);
                material->SetProperty(MaterialProperty::SubsurfaceScattering, 0.35f);
                material->SetObjectName("grass_blade");
                material->SetResourceName("grass_blade" + string(EXTENSION_MATERIAL));
                material = ResourceCache::Cache(material);
            }
            else if (mesh_path == "builtin/pebble")
            {
                // a chip is a solid, none of the foliage flags apply, and it is only ever this grey
                // when the layer has no material folder to take its maps from
                material->SetProperty(MaterialProperty::CullMode, static_cast<float>(RHI_CullMode::Back));
                material->SetProperty(MaterialProperty::Roughness, 0.82f);
                // stone is far darker than it looks, a light grey chip reads as painted plastic against
                // any ground, this is roughly the albedo of dry granite
                material->SetProperty(MaterialProperty::ColorR, 0.18f);
                material->SetProperty(MaterialProperty::ColorG, 0.17f);
                material->SetProperty(MaterialProperty::ColorB, 0.16f);
                material->SetObjectName("pebble");
                material->SetResourceName("pebble" + string(EXTENSION_MATERIAL));
            }
            else
            {
                material->SetProperty(MaterialProperty::IsFlower, 1.0f);
                material->SetProperty(MaterialProperty::Roughness, 1.0f);
                material->SetProperty(MaterialProperty::Clearcoat, 1.0f);
                material->SetProperty(MaterialProperty::Clearcoat_Roughness, 0.2f);
                material->SetProperty(MaterialProperty::SubsurfaceScattering, 0.0f);
                material->SetObjectName("flower");
                material->SetResourceName("flower" + string(EXTENSION_MATERIAL));
            }

            scatter_materials[mesh_path] = material;
            builder_materials.push_back(material);

            return material;
        }

        // a cutout is what the importer bound, wind and no collision follow that map
        bool is_foliage_material(Material* material)
        {
            return material->HasTextureOfType(MaterialTextureType::AlphaMask);
        }

        uint32_t count_mesh_renderables(Entity* root)
        {
            if (!root)
            {
                return 1;
            }

            vector<Entity*> parts;
            parts.push_back(root);
            root->GetDescendants(&parts);

            uint32_t count = 0;
            for (Entity* part : parts)
            {
                Render* render = part->GetComponent<Render>();
                if (render && render->GetMesh())
                {
                    count++;
                }
            }

            return max(count, 1u);
        }

        // clone the prototype onto every tile that has instances and hand that tile's transforms to
        // each renderable it carries, the hierarchy is kept because bark and leaves are two materials
        // a scatter is judged by how fast the ground you are looking at fills in, not by how fast the
        // far side of the map does, so the tiles are ranked from a probe a little ahead of the eye
        vector<uint32_t> build_tile_order(const vector<Entity*>& tiles)
        {
            const uint32_t tile_count = static_cast<uint32_t>(tiles.size());

            vector<uint32_t> order;
            order.reserve(tile_count);
            for (uint32_t tile_index = 0; tile_index < tile_count; tile_index++)
            {
                order.push_back(tile_index);
            }

            Camera* camera = World::GetCamera();
            if (!camera || !camera->GetEntity())
            {
                return order;
            }

            // pushing the probe forward breaks ties in favour of what is in front of the camera while
            // still leaving it a circle, so turning around does not re-order much
            const Vector3 eye   = camera->GetEntity()->GetPosition();
            const Vector3 probe = eye + camera->GetEntity()->GetForward() * 64.0f;

            vector<float> distance(tile_count, numeric_limits<float>::max());
            for (uint32_t tile_index = 0; tile_index < tile_count; tile_index++)
            {
                if (!tiles[tile_index])
                {
                    continue;
                }

                // the tile origin is a corner, the nearest point of its box is what the camera sees
                Vector3 point = tiles[tile_index]->GetPosition();
                if (Render* render = tiles[tile_index]->GetComponent<Render>())
                {
                    point = render->GetBoundingBox().GetClosestPoint(probe);
                }

                distance[tile_index] = (point - probe).LengthSquared();
            }

            sort(order.begin(), order.end(), [&distance](const uint32_t a, const uint32_t b)
            {
                return distance[a] < distance[b];
            });

            return order;
        }

        // how far the ground creeps over a prop, taken from the prop rather than authored per layer, so
        // a new scatter slot lands somewhere sensible without anyone tuning it and a layer that rolls a
        // nine to one size range still beds its small end
        // soil banks against the narrow side of whatever it meets, a trunk takes a hand's width where a
        // boulder takes half a metre, and nothing takes more than the terrain's own band
        float derive_terrain_blend(const float prop_extent_min, const float terrain_band)
        {
            if (prop_extent_min <= 0.0f || terrain_band <= 0.0f)
            {
                return 1.0f;
            }

            float band = clamp(prop_extent_min * 0.18f, 0.015f, terrain_band);

            // a chip that is mostly band is a coloured smudge rather than a stone, nothing gives up more
            // than this much of itself to the ground
            band = min(band, prop_extent_min * 0.4f);

            return clamp(band / terrain_band, 0.0f, 1.0f);
        }

        // the shortest side of the mesh is what the ground has to climb, a slab lying flat wants its own
        // thickness and not its footprint, and a trunk wants its own width and not the canopy above it
        float mesh_extent_min(const BoundingBox& aabb, const float instance_scale)
        {
            const Vector3 size = aabb.GetSize();
            return min(min(size.x, size.y), size.z) * instance_scale;
        }

        // two layers can point at one mesh at very different scales, boulders and rock debris both draw
        // rock_2 two hundred times apart, and a shared material means whichever attaches last decides the
        // ground band for both. the first layer keeps the imported material. cloning and pushing that
        // copy through SetMaterial was wiping the fbx maps, trees have no second layer so they just
        // lost their bark and leaves
        unordered_map<uint64_t, string> scatter_material_owners;
        unordered_map<string, shared_ptr<Material>> scatter_layer_materials;

        shared_ptr<Material> resolve_layer_material(Material* source, const string& layer_name)
        {
            if (!source)
            {
                return nullptr;
            }

            const uint64_t id = source->GetObjectId();
            auto owner        = scatter_material_owners.find(id);
            if (owner == scatter_material_owners.end())
            {
                scatter_material_owners[id] = layer_name;
                return nullptr;
            }
            if (owner->second == layer_name)
            {
                return nullptr;
            }

            const string key = layer_name + "|" + to_string(id);
            auto it          = scatter_layer_materials.find(key);
            if (it != scatter_layer_materials.end())
            {
                return it->second;
            }

            const string file_name = layer_name + "_" + source->GetObjectName() + EXTENSION_MATERIAL;
            shared_ptr<Material> owned = source->Clone(file_name);

            string directory = FileSystem::GetDirectoryFromFilePath(source->GetResourceFilePath());
            if (directory.empty())
            {
                directory = string(ResourceCache::GetProjectDirectory()) + "materials/";
            }
            owned->SetResourceFilePath(directory + file_name);
            owned->SetPersistent(false);
            // the foliage checks read the name, a copy that renames itself stops being a leaf
            owned->SetObjectName(source->GetObjectName());

            scatter_layer_materials[key] = owned;
            builder_materials.push_back(owned);

            return owned;
        }

        void attach_scatter_layer(
            Mesh* mesh,
            const TerrainScatterLayer& layer,
            const vector<Entity*>& tiles,
            const vector<vector<Matrix>>& transforms,
            const vector<uint32_t>& tile_order,
            const uint32_t order_begin,
            const uint32_t order_end,
            const float terrain_band
        )
        {
            Entity* prototype = mesh->GetRootEntity();
            if (prototype)
            {
                prototype->SetTransient(true);
            }

            shared_ptr<Material> material_override;
            if (!layer.material_folder.empty())
            {
                material_override = resolve_folder_material(layer.material_folder);
            }
            else if (!prototype)
            {
                material_override = resolve_builtin_material(layer.mesh_path);
            }

            // trees and rocks read as the silhouette of an island from across the map, a radial
            // cutoff pops whole hillsides of them in and out, so 0 means no limit at all and the gpu
            // instance cull drops each one on its own once it projects under two pixels
            const float render_distance = layer.render_distance > 0.0f ?
                layer.render_distance :
                numeric_limits<float>::max();
            const bool casts_shadows = (layer.flags & TerrainScatterFlags_CastShadows) != 0;

            // the band is cut for an average instance of this layer, the clamps inside the derivation
            // take care of the ends of the size roll
            float scale_sum      = 0.0f;
            uint32_t scale_count = 0;
            for (uint32_t order_index = order_begin; order_index < order_end; order_index++)
            {
                for (const Matrix& instance : transforms[tile_order[order_index]])
                {
                    scale_sum += instance.GetScale().y;
                    scale_count++;
                }
            }
            const float scale_typical = scale_count > 0 ? scale_sum / static_cast<float>(scale_count) : 1.0f;

            for (uint32_t order_index = order_begin; order_index < order_end; order_index++)
            {
                const uint32_t tile_index = tile_order[order_index];
                Entity* tile              = tiles[tile_index];
                if (!tile || transforms[tile_index].empty())
                {
                    continue;
                }

                vector<Entity*> parts;
                if (prototype)
                {
                    Entity* entity = prototype->Clone();
                    entity->SetObjectName(layer.name);
                    entity->SetTransient(true);
                    entity->AddTag(terrain_prop_tag);
                    entity->SetParent(tile);
                    entity->SetActive(true);

                    parts.push_back(entity);
                    entity->GetDescendants(&parts);

                    // instances are tile local, every node must sit at the tile origin so the gpu
                    // ends up doing instance times tile
                    for (Entity* part : parts)
                    {
                        part->SetTransient(true);
                        part->SetPositionLocal(Vector3::Zero);
                        part->SetRotationLocal(Quaternion::Identity);
                        part->SetScaleLocal(Vector3::One);
                    }
                }
                else
                {
                    Entity* entity = World::CreateEntity();
                    entity->SetObjectName(layer.name);
                    entity->SetTransient(true);
                    entity->AddTag(terrain_prop_tag);
                    entity->SetParent(tile);
                    entity->AddComponent<Render>()->SetMesh(mesh);
                    parts.push_back(entity);
                }

                for (Entity* part : parts)
                {
                    Render* render = part->GetComponent<Render>();
                    if (!render || !render->GetMesh())
                    {
                        continue;
                    }

                    if (material_override)
                    {
                        render->SetMaterial(material_override);
                    }

                    if (shared_ptr<Material> owned = resolve_layer_material(render->GetMaterial(), layer.name))
                    {
                        render->SetMaterial(owned);
                    }

                    bool foliage = false;
                    if (Material* material = render->GetMaterial())
                    {
                        foliage = is_foliage_material(material);

                        if (foliage && (layer.flags & TerrainScatterFlags_Wind))
                        {
                            material->SetProperty(MaterialProperty::WindAnimation, 1.0f);
                            material->SetProperty(MaterialProperty::SubsurfaceScattering, 1.0f);
                        }

                        if (layer.flags & TerrainScatterFlags_ColorVariation)
                        {
                            material->SetProperty(MaterialProperty::ColorVariationFromInstance, 1.0f);
                        }

                        // how far the ground creeps over this prop, sized off this part's own mesh so a
                        // trunk gets the trunk's band and not the canopy's, the layer value trims it
                        const float extent = mesh_extent_min(render->GetLodAabb(0), scale_typical);
                        material->SetProperty(
                            MaterialProperty::TerrainBlend,
                            layer.blend_height * derive_terrain_blend(extent, terrain_band)
                        );
                        material->SetProperty(MaterialProperty::TerrainBlendSharpness, layer.blend_sharpness);
                    }

                    render->SetInstances(transforms[tile_index]);
                    render->SetMaxRenderDistance(render_distance);
                    render->SetMaxShadowDistance(layer.shadow_distance);
                    render->SetFlag(RenderFlags::CastsShadows, casts_shadows);
                    // foliage instance counts blow the tlas, and the entity origin would ghost a
                    // full size mesh at the tile center
                    render->SetFlag(RenderFlags::ExcludeFromRayTracing, true);

                    if (render->GetMeshletCount(0) == 0)
                    {
                        SP_LOG_WARNING(
                            "terrain scatter '%s': '%s' has no meshlets, the opaque path will skip it",
                            layer.name.c_str(),
                            part->GetObjectName().c_str()
                        );
                    }

                    // only the solid parts are collidable, twigs and leaves would just snag the player
                    if ((layer.flags & TerrainScatterFlags_Collision) && !foliage)
                    {
                        Physics* physics = part->AddComponent<Physics>();
                        physics->SetUseConvexHull(true);
                        physics->SetBodyType(BodyType::Mesh);
                    }
                }
            }

            // the prototype stays in the world as a template, hide it so it does not draw at the origin
            if (prototype)
            {
                prototype->SetActive(false);
            }
        }

        // one gpu scatter slot, there are no entities for this, the populate pass reads the height map
        // and a channel of the biome mask every frame. grass takes slot 0, micro detail takes the rest
        bool enable_gpu_scatter(Terrain* terrain, const TerrainScatterLayer& layer, const uint32_t slot)
        {
            RHI_Texture* height_map = terrain->GetHeightMapGpu();
            if (!height_map)
            {
                SP_LOG_WARNING("terrain scatter '%s': no r32 height map, gpu scatter disabled", layer.name.c_str());
                return false;
            }

            // a layer that gates on a mask channel cannot run without the mask, a layer that ignores it
            // does not care, which is what lets micro detail cover ground no biome claimed
            RHI_Texture* prop_mask = terrain->GetPropMask();
            if (!prop_mask && (layer.mask_channel >= 0 || layer.ground_mask != 0))
            {
                SP_LOG_WARNING("terrain scatter '%s': no biome mask, gpu scatter disabled", layer.name.c_str());
                return false;
            }

            Mesh* mesh = resolve_scatter_mesh(layer.mesh_path);

            shared_ptr<Material> material;
            if (!layer.material_folder.empty())
            {
                material = resolve_folder_material(layer.material_folder);
            }
            if (!material && layer.kind == TerrainScatterKind::Detail)
            {
                // a chip only needs stone, so any of the ground materials will do, this is what keeps
                // micro detail textured on a project that never made a gravel folder
                for (const char* folder : { "project/materials/rock", "project/materials/dirt" })
                {
                    material = resolve_folder_material(folder);
                    if (material)
                    {
                        break;
                    }
                }
            }
            if (!material)
            {
                // a generated mesh carries no imported material, the flags that make a blade bend or a
                // chip read as stone live in the builtin set
                const bool builtin   = layer.mesh_path.rfind("builtin/", 0) == 0;
                const char* fallback = layer.kind == TerrainScatterKind::Detail ? "builtin/pebble" : "builtin/grass_blade";
                material             = resolve_builtin_material(builtin ? layer.mesh_path : string(fallback));
            }

            if (!mesh || !material)
            {
                SP_LOG_WARNING("terrain scatter '%s': no mesh or material, gpu scatter disabled", layer.name.c_str());
                return false;
            }

            Renderer::GpuScatterParams params;
            for (uint32_t ring = 0; ring < 3; ring++)
            {
                params.ring_radii_m[ring] = layer.grass_ring_radius[ring];
                params.cell_size_m[ring]  = layer.grass_cell_size[ring];
            }
            params.height_min = terrain->GetSeaLevel() + layer.height_min;
            params.height_max = terrain->GetSeaLevel() + layer.height_max;
            params.max_slope_deg = layer.slope_max;
            // a mask channel of -1 turns the gate off, which is what detail wants, a chip belongs anywhere
            params.biome_min_weight = layer.mask_channel >= 0 ? layer.mask_min : -1.0f;
            params.mask_channel = layer.mask_channel >= 0 ? static_cast<uint32_t>(layer.mask_channel) : 0u;
            // the ground type gate used to stop at the mesh layers, so ticking sand on grass or pebbles
            // changed a value the gpu never saw
            params.ground_mask = layer.ground_mask;
            params.density = clamp(layer.density, 0.01f, 1.0f);
            // the gpu rolls one size per instance the same way the cpu placer does
            params.size_min = layer.mesh_scale * min(layer.size_min, layer.size_max);
            params.size_max = layer.mesh_scale * max(layer.size_min, layer.size_max);
            // a generated chip has a planar uv over the whole texture, so without this every chip in the
            // field is stamped with the same image of a whole gravel bed and the field reads as plastic
            const bool textured = material->HasTextureOfType(MaterialTextureType::Color);
            params.uv_patch     = (layer.kind == TerrainScatterKind::Detail && textured) ? 0.22f : 0.0f;
            params.tilt_deg     = layer.kind == TerrainScatterKind::Detail ? 18.0f : 0.0f;
            params.surface_offset = layer.surface_offset;
            params.terrain_world_mapping = terrain->GetWorldMapping();

            // the gpu kinds reuse the clump fields as a procedural patch field. patch size alone says
            // whether there are pockets at all, gating it on coverage as well meant zero coverage and
            // full coverage both came out as an even spread, so the slider was dead at both ends and
            // only did anything in the middle, which is exactly what it felt like to author
            //
            // the floor keeps it away from that dead zone from below, at this end the pockets are tiny
            // and the compensation below is already at its ceiling
            params.patch_coverage = clamp(layer.clump_coverage, 0.05f, 1.0f);
            params.patch_size_m   = max(layer.clump_radius, 0.0f);
            params.patch_edge     = clamp(layer.clump_raggedness, 0.0f, 1.0f);
            // the same knob that frays the outline breaks up the interior, a clean edged pocket with a
            // pockmarked middle would read as two unrelated effects. this is no longer pushed, the
            // shader derives it from the edge with the same ratio, so grass_patch_scar_ratio in
            // grass_populate.hlsl has to track this number. it still feeds the cpu density boost
            params.patch_scar     = params.patch_edge * 0.3f;
            params.patch_invert   = layer.clump_invert;

            // how far the ground creeps over this prop, sized off the chip itself, the size range here
            // already carries mesh_scale so this is the world extent the ground has to climb
            float extent = 0.0f;
            if (mesh->GetSubMeshCount() > 0 && !mesh->GetSubMesh(0).lods.empty())
            {
                extent = mesh_extent_min(mesh->GetSubMesh(0).lods[0].aabb, (params.size_min + params.size_max) * 0.5f);
            }
            material->SetProperty(
                MaterialProperty::TerrainBlend,
                layer.blend_height * derive_terrain_blend(extent, terrain->GetBlendHeight())
            );
            material->SetProperty(MaterialProperty::TerrainBlendSharpness, layer.blend_sharpness);

            const float extent_x = static_cast<float>(terrain->GetWidth()  - 1) * static_cast<float>(terrain->GetScale());
            const float extent_z = static_cast<float>(terrain->GetHeight() - 1) * static_cast<float>(terrain->GetScale());
            params.terrain_extent_m = Vector2(extent_x, extent_z);

            Renderer::EnableGpuScatter(slot, mesh, material.get(), height_map, params, prop_mask);

            return true;
        }
    }

    void WorldHelpers::RefreshTerrainGpuScatter(Terrain* terrain)
    {
        if (!terrain || !terrain->GetSpawnBiomeProps())
        {
            Renderer::DisableGpuScatter();
            return;
        }

        bool gpu_slot_pushed[renderer_max_gpu_scatter_slots] = {};
        uint32_t detail_slot_next                            = 1;
        array<TerrainScatterLayer, terrain_scatter_max>& layers = terrain->GetScatterLayers();

        for (uint32_t layer_index = 0; layer_index < terrain_scatter_max; layer_index++)
        {
            TerrainScatterLayer& layer = layers[layer_index];
            if (!terrain->IsScatterActive(layer))
            {
                continue;
            }

            if (layer.kind != TerrainScatterKind::Grass && layer.kind != TerrainScatterKind::Detail)
            {
                continue;
            }

            uint32_t slot = 0;
            if (layer.kind == TerrainScatterKind::Detail)
            {
                if (detail_slot_next >= renderer_max_gpu_scatter_slots)
                {
                    continue;
                }
                slot = detail_slot_next++;
            }
            else if (gpu_slot_pushed[0])
            {
                if (detail_slot_next >= renderer_max_gpu_scatter_slots)
                {
                    continue;
                }
                slot = detail_slot_next++;
            }

            if (enable_gpu_scatter(terrain, layer, slot))
            {
                gpu_slot_pushed[slot] = true;
            }
        }

        for (uint32_t slot = 0; slot < renderer_max_gpu_scatter_slots; slot++)
        {
            if (!gpu_slot_pushed[slot])
            {
                Renderer::DisableGpuScatter(slot);
            }
        }
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
            Renderer::DisableGpuScatter();
            return;
        }

        terrain->RebuildPropMask();

        // tiles, every prop is parented to one so tile culling and tile teardown take it along
        const uint32_t tile_axis  = max(terrain->GetTileCountAxis(), 1u);
        const uint32_t tile_count = tile_axis * tile_axis;
        vector<Entity*> tiles(tile_count, nullptr);
        for (Entity* child : terrain_entity->GetChildren())
        {
            const int tile_index = Terrain::ParseTileIndex(child);
            if (tile_index >= 0 && static_cast<uint32_t>(tile_index) < tile_count)
            {
                tiles[static_cast<uint32_t>(tile_index)] = child;
            }
        }

        // resolved once, every layer walks the tiles in the same camera first order
        const vector<uint32_t> tile_order = build_tile_order(tiles);

        array<TerrainScatterLayer, terrain_scatter_max>& layers = terrain->GetScatterLayers();

        // grass owns slot 0 because its caps are the large ones, every detail layer claims the next
        // slot in order, a world with more gpu layers than slots gets a warning and the extras stay off
        bool gpu_slot_pushed[renderer_max_gpu_scatter_slots] = {};
        uint32_t detail_slot_next                            = 1;

        // one of these per layer that has a mesh and passed its gates, placement only touches per tile
        // buckets so it parallelises, the entities it feeds are built on this thread so the scene graph
        // is only ever mutated in one order, the transforms stay tile local and inherit the tile offset
        struct scatter_job
        {
            TerrainScatterLayer* layer  = nullptr;
            Mesh* mesh                  = nullptr;
            uint32_t slots_per_instance = 1;
            vector<vector<Matrix>> transforms;
            vector<float> coverage;
            size_t placed               = 0;
            size_t placed_batch         = 0;
            float coverage_sum          = 0.0f;
        };
        vector<scatter_job> jobs;
        jobs.reserve(terrain_scatter_max);

        for (uint32_t layer_index = 0; layer_index < terrain_scatter_max; layer_index++)
        {
            TerrainScatterLayer& layer = layers[layer_index];
            layer.instance_count       = 0;
            layer.coverage             = 0.0f;

            if (!terrain->IsScatterActive(layer))
            {
                continue;
            }

            if (layer.kind == TerrainScatterKind::Grass || layer.kind == TerrainScatterKind::Detail)
            {
                uint32_t slot = 0;
                if (layer.kind == TerrainScatterKind::Detail)
                {
                    if (detail_slot_next >= renderer_max_gpu_scatter_slots)
                    {
                        SP_LOG_WARNING(
                            "terrain scatter '%s': every gpu detail slot is taken, this layer stays off",
                            layer.name.c_str()
                        );
                        continue;
                    }
                    slot = detail_slot_next++;
                }
                else if (gpu_slot_pushed[0])
                {
                    // two grass layers would fight over slot 0, the second one borrows a detail slot
                    if (detail_slot_next >= renderer_max_gpu_scatter_slots)
                    {
                        SP_LOG_WARNING(
                            "terrain scatter '%s': every gpu scatter slot is taken, this layer stays off",
                            layer.name.c_str()
                        );
                        continue;
                    }
                    slot = detail_slot_next++;
                }

                if (enable_gpu_scatter(terrain, layer, slot))
                {
                    gpu_slot_pushed[slot] = true;
                }
                continue;
            }

            Mesh* mesh = resolve_scatter_mesh(layer.mesh_path);
            if (!mesh)
            {
                SP_LOG_WARNING(
                    "terrain scatter '%s': no mesh at %s",
                    layer.name.c_str(),
                    layer.mesh_path.c_str()
                );
                continue;
            }

            scatter_job& job       = jobs.emplace_back();
            job.layer              = &layer;
            job.mesh               = mesh;
            job.slots_per_instance = count_mesh_renderables(mesh->GetRootEntity());
            job.transforms.resize(tile_count);
            job.coverage.resize(tile_count, 0.0f);
        }

        // a batch of tiles is finished across every layer before the next batch starts and the first
        // batch is tiny, so the ground the camera is on gets its trees and its rocks in a few
        // milliseconds and the map grows outward from there, a pass per layer over every tile would
        // leave the terrain bare until the last tile of the last layer was done
        uint32_t order_done = 0;
        uint32_t batch      = 4;
        while (order_done < tile_count && !jobs.empty())
        {
            const uint32_t batch_size = min(batch, tile_count - order_done);
            const uint32_t order_end  = order_done + batch_size;

            for (scatter_job& job : jobs)
            {
                auto place = [&job, &tiles, &tile_order, terrain, order_done](uint32_t start_index, uint32_t end_index)
                {
                    for (uint32_t batch_index = start_index; batch_index < end_index; batch_index++)
                    {
                        const uint32_t tile_index = tile_order[order_done + batch_index];
                        if (!tiles[tile_index])
                        {
                            continue;
                        }

                        terrain->FindTransforms(
                            tile_index,
                            *job.layer,
                            job.transforms[tile_index],
                            &job.coverage[tile_index]
                        );
                    }
                };
                ThreadPool::ParallelLoop(place, batch_size);

                job.placed_batch = 0;
                for (uint32_t order_index = order_done; order_index < order_end; order_index++)
                {
                    const uint32_t tile_index = tile_order[order_index];
                    job.placed_batch         += job.transforms[tile_index].size();
                    job.coverage_sum         += job.coverage[tile_index];
                }
                job.placed += job.placed_batch;
            }

            // the tiles done so far are a fair sample of the map, guessing the total from them keeps the
            // instance buffer from growing once per batch, and the guess can never sit under what is
            // already placed because there are always at least as many tiles left as sampled
            uint32_t slots_projected = 1;
            for (const scatter_job& job : jobs)
            {
                const uint64_t projected = (static_cast<uint64_t>(job.placed) * tile_count) / order_end;
                slots_projected         += static_cast<uint32_t>(projected) * job.slots_per_instance;
            }
            GeometryBuffer::Reserve(0, 0, 0, 0, 0, slots_projected);

            for (scatter_job& job : jobs)
            {
                if (job.placed_batch == 0)
                {
                    continue;
                }

                attach_scatter_layer(
                    job.mesh,
                    *job.layer,
                    tiles,
                    job.transforms,
                    tile_order,
                    order_done,
                    order_end,
                    terrain->GetBlendHeight()
                );

                // the editor reads this while the scatter runs, keeping it live makes the props count
                // climb instead of jumping once at the end
                job.layer->instance_count = static_cast<uint32_t>(job.placed);
            }

            order_done += batch_size;
            batch       = min(batch * 2u, 64u);
        }

        for (const scatter_job& job : jobs)
        {
            job.layer->instance_count = static_cast<uint32_t>(job.placed);
            job.layer->coverage       = job.coverage_sum / static_cast<float>(tile_count);

            if (job.placed == 0)
            {
                SP_LOG_WARNING(
                    "terrain scatter '%s': the rules accepted no ground, loosen the slope, height or mask gates",
                    job.layer->name.c_str()
                );
                continue;
            }

            SP_LOG_INFO(
                "terrain scatter '%s': %zu instances, rules accepted %.1f%% of the surface",
                job.layer->name.c_str(),
                job.placed,
                job.layer->coverage * 100.0f
            );
        }

        // a slot nobody claimed this pass has to be switched off, otherwise it keeps drawing the mesh
        // and the rules it was given the last time round
        for (uint32_t slot = 0; slot < renderer_max_gpu_scatter_slots; slot++)
        {
            if (!gpu_slot_pushed[slot])
            {
                Renderer::DisableGpuScatter(slot);
            }
        }

        terrain->OnBiomePropsPopulated();
    }

    void WorldHelpers::RepopulateTerrainProps(Terrain* terrain, const vector<uint32_t>& tile_indices)
    {
        if (!terrain || !terrain->GetEntity() || tile_indices.empty() || !terrain->GetSpawnBiomeProps())
        {
            return;
        }

        Entity* terrain_entity    = terrain->GetEntity();
        const uint32_t tile_axis  = max(terrain->GetTileCountAxis(), 1u);
        const uint32_t tile_count = tile_axis * tile_axis;

        // only the tiles asked for, everything else keeps the props it has
        vector<Entity*> tiles(tile_count, nullptr);
        for (Entity* child : terrain_entity->GetChildren())
        {
            const int tile_index = Terrain::ParseTileIndex(child);
            if (tile_index < 0 || static_cast<uint32_t>(tile_index) >= tile_count)
            {
                continue;
            }

            if (find(tile_indices.begin(), tile_indices.end(), static_cast<uint32_t>(tile_index)) != tile_indices.end())
            {
                tiles[static_cast<uint32_t>(tile_index)] = child;
            }
        }

        // the old props on these tiles go first, and their seeds with them, so a pad passing over
        // later cannot bring back an instance that no longer exists
        array<TerrainScatterLayer, terrain_scatter_max>& layers = terrain->GetScatterLayers();
        size_t removed_roots = 0;
        for (uint32_t tile_index : tile_indices)
        {
            Entity* tile = tile_index < tile_count ? tiles[tile_index] : nullptr;
            if (!tile)
            {
                continue;
            }

            // the name test is safe here, a tile subtree holds no mesh prototypes
            vector<Entity*> doomed;
            for (Entity* child : tile->GetChildren())
            {
                if (child && (child->HasTag(terrain_prop_tag) || is_terrain_prop_name(child->GetObjectName())))
                {
                    doomed.push_back(child);
                }
            }

            for (Entity* prop : doomed)
            {
                // the layer count comes down by what this root held, one instanced part is enough
                // to read it, every part of a clone carries the same instance list
                for (TerrainScatterLayer& layer : layers)
                {
                    if (layer.name != prop->GetObjectName())
                    {
                        continue;
                    }

                    vector<Entity*> parts;
                    parts.push_back(prop);
                    prop->GetDescendants(&parts);
                    for (Entity* part : parts)
                    {
                        Render* render = part ? part->GetComponent<Render>() : nullptr;
                        if (render && render->HasInstancing())
                        {
                            const uint32_t count = render->GetInstanceCount();
                            layer.instance_count = layer.instance_count > count ? layer.instance_count - count : 0;
                            break;
                        }
                    }
                    break;
                }

                terrain->ForgetPropSeeds(prop);
                World::RemoveEntity(prop);
                removed_roots++;
            }
        }

        // the scatter rules read the placement triangles, and those still describe the ground
        // from before the edit
        terrain->RefreshPlacementData(tile_indices);

        struct scatter_job
        {
            TerrainScatterLayer* layer = nullptr;
            Mesh* mesh                 = nullptr;
            vector<vector<Matrix>> transforms;
            size_t placed              = 0;
        };
        vector<scatter_job> jobs;
        jobs.reserve(terrain_scatter_max);

        for (uint32_t layer_index = 0; layer_index < terrain_scatter_max; layer_index++)
        {
            TerrainScatterLayer& layer = layers[layer_index];
            if (!terrain->IsScatterActive(layer))
            {
                continue;
            }

            // grass and detail are gpu passes over the height map, they follow the edit on their own
            if (layer.kind == TerrainScatterKind::Grass || layer.kind == TerrainScatterKind::Detail)
            {
                continue;
            }

            Mesh* mesh = resolve_scatter_mesh(layer.mesh_path);
            if (!mesh)
            {
                continue;
            }

            scatter_job& job = jobs.emplace_back();
            job.layer        = &layer;
            job.mesh         = mesh;
            job.transforms.resize(tile_count);
        }

        if (jobs.empty())
        {
            terrain->OnBiomePropsRepopulated(tile_indices);
            return;
        }

        const uint32_t order_count = static_cast<uint32_t>(tile_indices.size());
        for (scatter_job& job : jobs)
        {
            auto place = [&job, &tiles, &tile_indices, terrain](uint32_t start_index, uint32_t end_index)
            {
                for (uint32_t order_index = start_index; order_index < end_index; order_index++)
                {
                    const uint32_t tile_index = tile_indices[order_index];
                    if (tile_index >= tiles.size() || !tiles[tile_index])
                    {
                        continue;
                    }

                    terrain->FindTransforms(tile_index, *job.layer, job.transforms[tile_index]);
                }
            };
            ThreadPool::ParallelLoop(place, order_count);

            for (uint32_t tile_index : tile_indices)
            {
                if (tile_index < tile_count)
                {
                    job.placed += job.transforms[tile_index].size();
                }
            }
        }

        for (scatter_job& job : jobs)
        {
            if (job.placed == 0)
            {
                continue;
            }

            attach_scatter_layer(
                job.mesh,
                *job.layer,
                tiles,
                job.transforms,
                tile_indices,
                0,
                order_count,
                terrain->GetBlendHeight()
            );

            job.layer->instance_count += static_cast<uint32_t>(job.placed);
        }

        SP_LOG_INFO(
            "terrain scatter: re-placed %u tiles, %zu prop roots replaced",
            order_count,
            removed_roots
        );

        terrain->OnBiomePropsRepopulated(tile_indices);
    }

    void WorldHelpers::Clear()
    {
        // procedural grass references a mesh and a material owned by these vectors,
        // the caller is expected to disable it first so the renderer drops its references
        builtin_meshes.clear();
        scatter_materials.clear();
        scatter_material_owners.clear();
        scatter_layer_materials.clear();
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
            "IsSkidMark",                 MaterialProperty::IsSkidMark,
            "TerrainBlend",               MaterialProperty::TerrainBlend,
            "TerrainBlendSharpness",      MaterialProperty::TerrainBlendSharpness,
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
