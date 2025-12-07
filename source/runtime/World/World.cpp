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

//= INCLUDES =========================
#include "pch.h"
#include "World.h"
#include "Entity.h"
#include "../Game/Game.h"
#include "../Profiling/Profiler.h"
#include "../Core/ProgressTracker.h"
//#include "../Core/ThreadPool.h" // Take a look at later
#include "Components/Renderable.h"
#include "Components/Camera.h"
#include "Components/Light.h"
#include "Components/AudioSource.h"
#include "../Resource/ResourceCache.h"
#include "Components/Volume.h"
#include "Rendering/Renderer.h"
SP_WARNINGS_OFF
#include "../IO/pugixml.hpp"
SP_WARNINGS_ON
//====================================

//= NAMESPACES ===============
using namespace std;
using namespace spartan::math;
//============================

namespace spartan
{
    namespace
    {
        vector<Entity*> entities;
        vector<Entity*> entities_lights; // entities subset that contains only lights
        string file_path;
        mutex entity_access_mutex;
        vector<Entity*> pending_add;
        set<uint64_t> pending_remove;
        vector<Volume*> registered_volumes;
        vector<Volume*> overlapping_volumes;
        RenderOptionsPool mix_volume_options = RenderOptionsPool(RenderOptionsListType::Component); // volume data accumulation (when overlapped)
        RenderOptionsPool blended_options = RenderOptionsPool(RenderOptionsListType::Global); // volume data accumulation (on interpolation)
        bool is_transitioning = false;
        uint32_t audio_source_count          = 0;
        atomic<bool> resolve                 = false;
        bool was_in_editor_mode              = false;
        BoundingBox bounding_box             = BoundingBox::Unit;
        Entity* camera                       = nullptr;
        Entity* light                        = nullptr;

        // entity state tracking - things that change the nature of the entity for rendering
        enum class EntityChange : uint8_t
        {
            None       = 0,
            Active     = 1 << 0,
            Components = 1 << 1,
            CullMode   = 1 << 2,
            LightType  = 1 << 3
        };
        unordered_map<uint64_t, uint32_t> entity_states; // stores: low 8 bits for flags, next 8 for component count, next 8 for cull mode, next 8 for light type

        // material change tracking - things that change the nature of the material for rendering
        unordered_map<uint64_t, size_t> material_state_hashes;

        void mark_entity_changed(uint64_t id, EntityChange change)
        {
            entity_states[id] |= static_cast<uint32_t>(change);
            resolve            = true;
        }

        size_t compute_material_hash(Material* material)
        {
            size_t hash = 17; // FNV-1a seed
            for (const auto* texture : material->GetTextures())
            {
                hash = (hash * 31) ^ reinterpret_cast<size_t>(texture);
            }
            for (const float prop : material->GetProperties())
            {
                hash = (hash * 31) ^ std::hash<float>{}(prop);
            }
            return hash;
        }

        void compute_bounding_box()
        {
            bounding_box = BoundingBox::Unit;

            for (Entity* entity : entities)
            {
                if (entity->GetActive())
                {
                    if (Renderable* renderable = entity->GetComponent<Renderable>())
                    {
                        bounding_box.Merge(renderable->GetBoundingBox());
                    }
                }
            }
        }

        string world_file_path_to_resource_directory(const string& world_file_path)
        {
            const string world_name = FileSystem::GetFileNameWithoutExtensionFromFilePath(world_file_path);
            return FileSystem::GetDirectoryFromFilePath(world_file_path) + "\\" + world_name + "_resources\\";
        }
    }

    namespace world_time
    {
        // simulated time
        float time_of_day = 0.25f; // 6 AM
        float time_scale = 200.0f; // 200x real time

        // tick simulated time every frame
        void tick()
        {
            time_of_day += (static_cast<float>(Timer::GetDeltaTimeSec()) * time_scale) / 86400.0f;
            if (time_of_day >= 1.0f)
            {
                time_of_day -= 1.0f;
            }
            else if (time_of_day < 0.0f)
            {
                time_of_day = 0.0f;
            }
        }

        // get current time of day based on boolean
        float get_time_of_day(bool use_real_world_time)
        {
            if (use_real_world_time)
            {
                using namespace std::chrono;
                auto now = system_clock::now();
                time_t t = system_clock::to_time_t(now);
                tm local_time = {};
            #if defined(_WIN32)
                localtime_s(&local_time, &t);
            #else
                localtime_r(&t, &local_time);
            #endif
                float hours = static_cast<float>(local_time.tm_hour);
                float minutes = static_cast<float>(local_time.tm_min);
                float seconds = static_cast<float>(local_time.tm_sec);
                return (hours + minutes / 60.0f + seconds / 3600.0f) / 24.0f;
            }

            // return simulated time if not using real-world time
            return time_of_day;
        }
    }

    void World::ProcessPendingRemovals()
    {
        lock_guard<mutex> lock(entity_access_mutex);

        if (pending_remove.empty())
            return;

        for (auto it = entities.begin(); it != entities.end(); )
        {
            uint64_t id = (*it)->GetObjectId();
            if (pending_remove.count(id) > 0)
            {
                // clean up change tracking
                entity_states.erase(id);
                if (Material* mat = (*it)->GetComponent<Renderable>() ? (*it)->GetComponent<Renderable>()->GetMaterial() : nullptr)
                {
                    material_state_hashes.erase(mat->GetObjectId());
                }
                delete *it;
                it = entities.erase(it);
            }
            else
            {
                ++it;
            }
        }

        pending_remove.clear();
    }

    void World::ProcessPendingAdditions()
    {
        lock_guard<mutex> lock(entity_access_mutex);

        if (pending_add.empty())
            return;

        entities.insert(entities.end(), pending_add.begin(), pending_add.end());
        pending_add.clear();
    }

    void World::Initialize()
    {

    }

    void World::Shutdown()
    {
        Engine::SetFlag(EngineMode::Playing, false); // stop simulation
        ResourceCache::Shutdown();                   // release all resources (textures, materials, meshes, etc)

        // clear entities
        camera = nullptr;
        light  = nullptr;
        for (Entity* entity : entities)
        {
            delete entity;
        }
        entities.clear();
        entities_lights.clear();
        overlapping_volumes.clear();
        registered_volumes.clear();
        pending_add.clear();
        camera = nullptr;
        light  = nullptr;
        file_path.clear();

        // clear change tracking
        entity_states.clear();
        material_state_hashes.clear();

        // mark for resolve
        resolve = true;
    }

    void World::Tick()
    {
        // loading can happen in the background
        if (ProgressTracker::IsLoading())
            return;

        SP_PROFILE_CPU();

        // detect game toggling
        const bool started = Engine::IsFlagSet(EngineMode::Playing) && was_in_editor_mode;
        const bool stopped = !Engine::IsFlagSet(EngineMode::Playing) && !was_in_editor_mode;
        was_in_editor_mode = !Engine::IsFlagSet(EngineMode::Playing);

        // start
        if (started)
        {
            for (Entity* entity : entities)
            {
                entity->Start();
            }
        }

        // stop
        if (stopped)
        {
            for (Entity* entity : entities)
            {
                entity->Stop();
            }
        }

        ProcessPendingRemovals();


        for (Entity* entity : entities)
        {
            if (entity->GetActive())
            {
                entity->PreTick();
            }
        }

        // tick
        for (Entity* entity : entities)
        {
            if (entity->GetActive())
            {
                entity->Tick();
            }
        }

        // check for entity changes
        for (Entity* entity : entities)
        {
            if (entity->GetActive())
            {
                uint64_t id = entity->GetObjectId();
                auto it = entity_states.find(id);
                if (it != entity_states.end())
                {
                    uint32_t& state = it->second;
                    uint32_t new_state = state;

                    // active state
                    bool was_active = (state & static_cast<uint32_t>(EntityChange::Active)) != 0;
                    if (entity->GetActive() != was_active)
                    {
                        new_state |= static_cast<uint32_t>(EntityChange::Active);
                        resolve = true;
                    }

                    // component count
                    uint8_t prev_component_count = (state >> 8) & 0xFF;
                    uint8_t curr_component_count = static_cast<uint8_t>(min(entity->GetComponentCount(), 255u));
                    if (curr_component_count != prev_component_count)
                    {
                        new_state = (new_state & ~0xFF00) | (curr_component_count << 8);
                        new_state |= static_cast<uint32_t>(EntityChange::Components);
                        resolve = true;
                    }

                    // cull mode
                    uint8_t prev_cull = (state >> 16) & 0xFF;
                    uint8_t curr_cull = static_cast<uint8_t>(RHI_CullMode::None);
                    if (Renderable* renderable = entity->GetComponent<Renderable>())
                    {
                        if (Material* material = renderable->GetMaterial())
                        {
                            curr_cull = static_cast<uint8_t>(material->GetProperty(MaterialProperty::CullMode));
                        }
                    }
                    if (curr_cull != prev_cull)
                    {
                        new_state = (new_state & ~0xFF0000) | (curr_cull << 16);
                        new_state |= static_cast<uint32_t>(EntityChange::CullMode);
                        resolve = true;
                    }

                    // light type
                    uint8_t prev_light_type = (state >> 24) & 0xFF;
                    uint8_t curr_light_type = static_cast<uint8_t>(LightType::Max);
                    if (Light* light_comp = entity->GetComponent<Light>())
                    {
                        curr_light_type = static_cast<uint8_t>(light_comp->GetLightType());
                    }
                    if (curr_light_type != prev_light_type)
                    {
                        new_state = (new_state & ~0xFF000000) | (curr_light_type << 24);
                        new_state |= static_cast<uint32_t>(EntityChange::LightType);
                        resolve = true;
                    }

                    state = new_state;
                }
            }
        }

        ProcessPendingAdditions();

        // resolve if needed
        if (resolve)
        {
            // track entities
            {
                camera             = nullptr;
                light              = nullptr;
                audio_source_count = 0;
                entities_lights.clear();
                for (Entity* entity : entities)
                {
                    if (entity->GetActive())
                    {
                        if (!camera && entity->GetComponent<Camera>())
                        {
                            camera = entity;
                        }

                        if (Light* light_comp = entity->GetComponent<Light>())
                        {
                            if (!light && light_comp->GetLightType() == LightType::Directional)
                            {
                                light = entity;
                            }
                            entities_lights.push_back(entity);
                        }

                        if (entity->GetComponent<AudioSource>())
                        {
                            audio_source_count++;
                        }
                    }
                }
            }

            compute_bounding_box();
            resolve = false;
            entity_states.clear();
        }

        // Volumes interpolation
        UpdateActiveVolumes();
        InterpolateOverlappingVolumes();

        if (Engine::IsFlagSet(EngineMode::Playing))
        {
            world_time::tick();
            Game::Tick();
        }
        else
        {
            Game::EditorTick();
        }
    }

    bool World::SaveToFile(string file_path)
    {
        if (FileSystem::GetExtensionFromFilePath(file_path) != EXTENSION_WORLD)
        {
            file_path += string(EXTENSION_WORLD);
        }

        // start timing
        const Stopwatch timer;

        // serialize the resources before saving the world (XML), as it references them
        {
            string directory = world_file_path_to_resource_directory(file_path);
            FileSystem::CreateDirectory_(directory);

            vector<shared_ptr<IResource>> resources = ResourceCache::GetResources();

            // Combined loop for resource saving, filtered by type
            for (shared_ptr<IResource>& resource : resources)
            {
                string ext;
                switch (resource->GetResourceType())
                {
                    case ResourceType::Texture:  ext = EXTENSION_TEXTURE;  break;
                    case ResourceType::Material: ext = EXTENSION_MATERIAL; break;
                    case ResourceType::Mesh:     ext = EXTENSION_MESH;     break;
                default: continue;
                }
                resource->SaveToFile(directory + resource->GetObjectName() + ext);
            }
        }

        // create document
        pugi::xml_document doc;
        pugi::xml_node world_node = doc.append_child("World");
        world_node.append_attribute("name") = FileSystem::GetFileNameWithoutExtensionFromFilePath(file_path).c_str();

        // entities
        {
            // node
            pugi::xml_node entities_node = world_node.append_child("Entities");

            // get root entities, save them, and they will save their children recursively
            static vector<Entity*> root_entities;
            World::GetRootEntities(root_entities);
            const uint32_t root_entity_count = static_cast<uint32_t>(root_entities.size());

            // progress tracking
            ProgressTracker::GetProgress(ProgressType::World).Start(root_entity_count, "Saving world...");

            // write entities to node
            for (Entity* root : root_entities)
            {
                pugi::xml_node entity_node = entities_node.append_child("Entity");
                root->Save(entity_node);
                ProgressTracker::GetProgress(ProgressType::World).JobDone();
            }
        }

        // save to file
        bool saved = doc.save_file(file_path.c_str(), " ", pugi::format_indent);
        if (!saved)
        {
            SP_LOG_ERROR("Failed to save XML file.");
            return false;
        }

        // log
        SP_LOG_INFO("World \"%s\" has been saved. Duration %.2f ms", file_path.c_str(), timer.GetElapsedTimeMs());

        return true;
    }

    bool World::LoadFromFile(const string& file_path_)
    {
        Shutdown(); // clear existing world
        file_path = file_path_;

        // start timing
        const Stopwatch timer;

        // deserialize the resources before loading the world (XML), as it references them
        {
            string directory = world_file_path_to_resource_directory(file_path);
            vector<string> files = FileSystem::GetFilesInDirectory(directory);

            // Combined loop for loading, filtered by extension
            for (string& path : files)
            {
                if (FileSystem::IsEngineTextureFile(path))
                {
                    ResourceCache::Load<RHI_Texture>(path);
                }
                else if (FileSystem::IsEngineMaterialFile(path))
                {
                    ResourceCache::Load<Material>(path);
                }
                else if (FileSystem::IsEngineMeshFile(path))
                {
                    ResourceCache::Load<Mesh>(path);
                }
            }
        }

        // load xml document
        pugi::xml_document doc;
        pugi::xml_parse_result result = doc.load_file(file_path.c_str());
        if (!result)
        {
            SP_LOG_ERROR("Failed to load XML file: %s", result.description());
            return false;
        }

        // get world node
        pugi::xml_node world_node = doc.child("World");
        if (!world_node)
        {
            SP_LOG_ERROR("No 'World' node found.");
            return false;
        }

        // entities
        {
            // get node
            pugi::xml_node entities_node = world_node.child("Entities");
            if (!entities_node)
            {
                SP_LOG_ERROR("No 'Entities' node found.");
                return false;
            }

            // count root entities for progress tracking
            uint32_t root_entity_count = 0;
            for (pugi::xml_node entity_node = entities_node.child("Entity"); entity_node; entity_node = entity_node.next_sibling("Entity"))
            {
                ++root_entity_count;
            }

            // progress tracking
            ProgressTracker::GetProgress(ProgressType::World).Start(root_entity_count, "Loading world...");

            // load root entities (they will load their descendants recursively)
            for (pugi::xml_node entity_node = entities_node.child("Entity"); entity_node; entity_node = entity_node.next_sibling("Entity"))
            {
                Entity* entity = World::CreateEntity();
                entity->Load(entity_node);
                ProgressTracker::GetProgress(ProgressType::World).JobDone();
            }
        }

        // report time
        SP_LOG_INFO("World \"%s\" has been loaded. Duration %.2f ms", file_path.c_str(), timer.GetElapsedTimeMs());

        return true;
    }

    Entity* World::CreateEntity()
    {
        lock_guard lock(entity_access_mutex);

        Entity* entity = new Entity();
        pending_add.push_back(entity);
        mark_entity_changed(entity->GetObjectId(), EntityChange::Components); // new entity requires resolve

        return entity;
    }

    bool World::EntityExists(Entity* entity)
    {
        SP_ASSERT_MSG(entity != nullptr, "Entity is null");

        return GetEntityById(entity->GetObjectId()) != nullptr;
    }

    void World::RemoveEntity(Entity* entity_to_remove)
    {
        SP_ASSERT_MSG(entity_to_remove != nullptr, "Entity is null");

        lock_guard<mutex> lock(entity_access_mutex);

        // keep track of the local camera pointer so we don't have a dangling pointer
        if (Camera* camera_ = entity_to_remove->GetComponent<Camera>())
        {
            camera = nullptr;
        }
        if (Volume* volume = entity_to_remove->GetComponent<Volume>())
        {
            UnregisterVolume(volume);
        }

        // remove the entity and all of its children
        {
            // get the root entity and its descendants
            vector<Entity*> entities_to_remove;
            entities_to_remove.push_back(entity_to_remove); // add the root entity
            entity_to_remove->GetDescendants(&entities_to_remove); // get descendants

            // create a set containing the object ids of entities to remove
            set<uint64_t> ids_to_remove;
            for (Entity* entity : entities_to_remove)
            {
                ids_to_remove.insert(entity->GetObjectId());
            }

            // defer removal
            pending_remove.insert(ids_to_remove.begin(), ids_to_remove.end());

            // if there was a parent, update it
            if (Entity* parent = entity_to_remove->GetParent())
            {
                parent->AcquireChildren();
            }
        }

        resolve = true;
    }

    void World::GetRootEntities(vector<Entity*>& entities_out)
    {
        lock_guard<mutex> lock(entity_access_mutex);

        entities_out.clear();
        entities_out.reserve(entities.size());
        for (Entity* entity : entities)
        {
            if (!entity->GetParent())
            {
                entities_out.emplace_back(entity);
            }
        }
    }

    Entity* World::GetEntityById(const uint64_t id)
    {
        lock_guard<mutex> lock(entity_access_mutex);

        for (const auto& entity : entities)
        {
            if (entity && entity->GetObjectId() == id)
                return entity;
        }

        return nullptr;
    }

    const vector<Entity*>& World::GetEntities()
    {
        return entities;
    }

    const vector<Entity*>& World::GetEntitiesLights()
    {
        return entities_lights;
    }

    void World::UpdateActiveVolumes()
    {
        for (Volume* volume : registered_volumes)
        {
            const float alpha = volume->ComputeAlpha(camera->GetMatrix().GetTranslation());

            auto volume_it = ranges::find(overlapping_volumes, volume);
            if (alpha > 0.0f && volume_it == overlapping_volumes.end())
            {
                // Camera is within volume's range. Mark it as overlapping
                overlapping_volumes.push_back(volume);
                UpdateMixRenderOptions();
                is_transitioning = true;
            }
            else if (alpha <= 0.0f && volume_it != overlapping_volumes.end())
            {
                // Camera just got out of volume's range. Cancel overlapping behaviour.
                overlapping_volumes.erase(volume_it);
                UpdateMixRenderOptions();
            }
        }

        // If we are moving out from any volumes, update back to global values once
        if (overlapping_volumes.empty())
        {
            is_transitioning = false;
            blended_options = Renderer::GetRenderOptionsPoolRef(true);
            UpdateRenderOptions();
        }
    }

    void World::UpdateMixRenderOptions()
    {
        // Clear previous accumulation for proper reset
        mix_volume_options = RenderOptionsPool(RenderOptionsListType::Component);

        // Update the render options results for overlapping volumes.
        // Works for single-volume overlapping cases as well.
        // - for floats:          take average result (ex: Fog_v1=50, Fog_v2=100, Fog_Mix=75)
        // - for booleans:        take combination result
        // - for enums:           "newest wins" semantic
        // - for ints:            not implemented or applicable (no options have this type)

        // Frequency table for boolean render options to determine the combination result
        map<Renderer_Option, int> bool_options_frequencies;

        // Simple accumulation containers for floats (so we don't rely on GetOption before it's set)
        map<Renderer_Option, float> float_options_accumulations;
        map<Renderer_Option, int> float_options_counts;

        for (Volume* volume : overlapping_volumes)
        {
            for (auto& [option_key, option_value] : volume->GetOptionsCollection().GetOptions())
            {
                if (holds_alternative<bool>(option_value))
                {
                    const int bool_option_count = bool_options_frequencies[option_key]++;

                    const bool existing_bool_value = mix_volume_options.GetOption<bool>(option_key);
                    bool new_bool_value = get<bool>(option_value);
                    mix_volume_options.SetOption(option_key, bool_option_count <= 1 ? new_bool_value : new_bool_value && existing_bool_value);
                }
                else if (holds_alternative<uint32_t>(option_value))
                {
                    uint32_t new_enum_value = std::get<uint32_t>(option_value);
                    mix_volume_options.SetOption(option_key, new_enum_value);
                }
                else if (holds_alternative<float>(option_value))
                {
                    const float new_float_value = std::get<float>(option_value);
                    float_options_accumulations[option_key] += new_float_value;
                    float_options_counts[option_key] += 1;
                }
            }
        }

        // Finalize float averages into mix_volume_options
        for (auto& [option_key, float_sum_value] : float_options_accumulations)
        {
            if (const int count = float_options_counts[option_key]; count > 0)
            {
                float float_average_value = float_sum_value / static_cast<float>(count);
                mix_volume_options.SetOption(option_key, float_average_value);
            }
        }
    }

    void World::InterpolateOverlappingVolumes()
    {
        // Reset blended options to a clean pool seeded with global values
        blended_options = RenderOptionsPool(RenderOptionsListType::Global);
        RenderOptionsPool& global_render_options = Renderer::GetRenderOptionsPoolRef(true);

        // ensure blended_options has keys for options we will override, or use Get/Set semantics below

        float total_alpha = 0.0f;
        bool any_volume_transitioning = false;

        map<Renderer_Option, float> accumulator_floats;
        map<Renderer_Option, float> accumulator_weights; // sum of weights per option

        // Accumulate weighted sums for each option across volumes
        for (Volume* volume : overlapping_volumes)
        {
            // Alpha computations
            const float alpha = volume->ComputeAlpha(camera->GetMatrix().GetTranslation());
            any_volume_transitioning = alpha > 0.0f && alpha < 1.0f;
            total_alpha += alpha;

            for (auto& [option_key, option_value] : volume->GetOptionsCollection().GetOptions())
            {
                // Only float render options can be interpolated
                if (std::holds_alternative<float>(option_value))
                {
                    float option_float_value = std::get<float>(option_value);
                    accumulator_floats[option_key] += option_float_value * alpha;
                    accumulator_weights[option_key] += alpha;
                }
            }
        }

        // Already snapped to global options. See UpdateActiveVolumes()
        if (overlapping_volumes.empty())
        {
            return;
        }

        // No volumes are transitioning at the moment. Snap to mixed options.
        if (!any_volume_transitioning)
        {
            blended_options = mix_volume_options;
            UpdateRenderOptions();
            is_transitioning = false;
            return;
        }

        // Volume in transition detected: compute per-option blended float values and lerp with global
        for (auto& [option_key, sumVal] : accumulator_floats)
        {
            const float weight_sum = accumulator_weights[option_key];
            if (weight_sum <= 0.0f)
                continue;

            const float weighted_average = sumVal / weight_sum; // this is the average of option values weighted by alpha
            const float global_float_value = global_render_options.GetOption<float>(option_key);
            const float t = clamp(total_alpha, 0.0f, 1.0f);

            float final = lerp(global_float_value, weighted_average, t);

            blended_options.SetOption(option_key, final);
        }

        // Update every frame, since there is currently a transition being performed
        UpdateRenderOptions();

        // Keep is_transitioning state true so we don't snap prematurely
        is_transitioning = true;
    }

    void World::UpdateRenderOptions()
    {
        for (auto& [option_key, option_value] : blended_options.GetOptions())
        {
            if (RenderOptionType existing_value = Renderer::GetOption(option_key); !RenderOptionsPool::AreVariantsEqual(existing_value, option_value))
            {
                Renderer::SetOption(option_key, option_value);
            }
        }
    }

    void World::RegisterVolume(Volume* volume)
    {
        if (!volume)
            return;

        if (const auto it = ranges::find(registered_volumes, volume); it == registered_volumes.end())
        {
            registered_volumes.push_back(volume);
        }
    }

    void World::UnregisterVolume(Volume* volume)
    {
        if (!volume)
            return;

        if (const auto it = ranges::find(registered_volumes, volume); it != registered_volumes.end())
        {
            registered_volumes.erase(ranges::remove(registered_volumes, volume).begin(), registered_volumes.end());
        }
    }

    string World::GetName()
    {
        return FileSystem::GetFileNameFromFilePath(file_path);
    }

    const string& World::GetFilePath()
    {
        return file_path;
    }

    BoundingBox& World::GetBoundingBox()
    {
        return bounding_box;
    }

    Camera* World::GetCamera()
    {
        return camera ? camera->GetComponent<Camera>() : nullptr;
    }

    Light* World::GetDirectionalLight()
    {
        return light ? light->GetComponent<Light>() : nullptr;
    }

    uint32_t World::GetLightCount()
    {
        return static_cast<uint32_t>(entities_lights.size());
    }

    uint32_t World::GetAudioSourceCount()
    {
        return audio_source_count;
    }

    bool World::HaveMaterialsChangedThisFrame()
    {
        lock_guard<mutex> lock(entity_access_mutex);

        bool changed = false;
        for (Entity* entity : entities)
        {
            if (Renderable* renderable = entity->GetComponent<Renderable>())
            {
                if (Material* material = renderable->GetMaterial())
                {
                    const uint64_t id   = material->GetObjectId();
                    size_t current_hash = compute_material_hash(material);
                    auto it = material_state_hashes.find(id);
                    if (it == material_state_hashes.end())
                    {
                        // new material
                        material_state_hashes[id] = current_hash;
                        changed = true;
                    }
                    else if (it->second != current_hash)
                    {
                        // material changed
                        it->second = current_hash;
                        changed = true;
                    }
                }
            }
        }

        return changed;
    }

    bool World::HaveLightsChangedThisFrame()
    {
        lock_guard<mutex> lock(entity_access_mutex);

        for (Entity* entity : entities_lights)
        {
            if (Light* light = entity->GetComponent<Light>())
            {
                if (light->HasChangedThisFrame())
                    return true;
            }
        }

        return false;
    }

    float World::GetTimeOfDay(bool use_real_world_time)
    {
        return world_time::get_time_of_day(use_real_world_time);
    }
}
