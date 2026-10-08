/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

//= INCLUDES =========================
#include "pch.h"
#include "../profiling/WorldWork.h"
#include "commands/CommandStack.h"
#include <unordered_set>
#include "World.h"
#include "TerrainSystem.h"
#include "PlaySession.h"
#include "WorldPreparation.h"
#include "WorldResources.h"
#include "Entity.h"
#include "Prefab.h"
#include "WorldHelpers.h"
#include "Weather.h"
#include "../geometry/GeneratedCache.h"
#include "../rendering/GeometryBuffer.h"
#include "../profiling/Profiler.h"
#include "../core/ProgressTracker.h"
#include "../core/ThreadPool.h"
#include "../core/Event.h"
#include "components/Render.h"
#include "components/Camera.h"
#include "components/Spline.h"
#include "components/Light.h"
#include "components/AudioSource.h"
#include "components/Volume.h"
#include "components/ParticleSystem.h"
#include "components/Terrain.h"
#include "components/Text3D.h"
#include "components/Animator.h"
#include "components/Ragdoll.h"
#include "../resource/ResourceCache.h"
#include "../rhi/RHI_Texture.h"
#include "../rendering/Material.h"
#include "../rendering/Renderer.h"
#include "components/Physics.h"
#include "components/Navigation.h"
#include "components/Script.h"
#include "../physics/PhysicsWorld.h"
#include "../input/Input.h"
#include "../core/Timer.h"
#include "../memory/Allocator.h"
#include "../memory/GpuMemory.h"
#include "../rhi/RHI_Device.h"
#include "../geometry/Mesh.h"
#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <sstream>
#include <thread>
SP_WARNINGS_OFF
#include <sol/sol.hpp>
#include "../io/pugixml.hpp"
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
        void log_cpu_memory(const char* label)
        {
            constexpr double mb = 1024.0 * 1024.0;
            uint64_t mesh_bytes    = 0;
            uint64_t texture_bytes = 0;
            uint32_t mesh_count    = 0;
            vector<pair<uint64_t, string>> largest_meshes;
            for (const shared_ptr<IResource>& resource : ResourceCache::GetResourcesSnapshot())
            {
                if (resource->GetResourceType() == ResourceType::Mesh)
                {
                    const Mesh* mesh     = static_cast<const Mesh*>(resource.get());
                    const uint64_t bytes = mesh->GetCpuBytes();
                    mesh_bytes += bytes;
                    mesh_count++;
                    largest_meshes.emplace_back(bytes, mesh->GetObjectName());
                }
                else if (resource->GetResourceType() == ResourceType::Texture)
                {
                    texture_bytes += static_cast<const RHI_Texture*>(resource.get())->GetCpuBytes();
                }
            }
            sort(largest_meshes.begin(), largest_meshes.end(), [](const auto& a, const auto& b) { return a.first > b.first; });

            SP_LOG_INFO("Cpu memory (%s): heap %.0f MB (peak %.0f MB), working set %.0f MB, meshes %.0f MB (%u), geometry mirror %.0f MB, texture data %.0f MB",
                label,
                Allocator::GetMemoryAllocatedMb(),
                Allocator::GetMemoryAllocatedPeakMb(),
                Allocator::GetMemoryProcessUsedMb(),
                mesh_bytes / mb,
                mesh_count,
                GeometryBuffer::GetCpuBytes() / mb,
                texture_bytes / mb);
            for (size_t i = 0; i < largest_meshes.size() && i < 8; i++)
            {
                SP_LOG_INFO("Cpu memory (%s):   mesh %s %.1f MB", label, largest_meshes[i].second.c_str(), largest_meshes[i].first / mb);
            }
            Allocator::LogLargestAllocationSites(label, 40);

            vector<GpuMemoryBlock> blocks;
            GpuMemory::GetBlocks(blocks);
            uint64_t kind_bytes[static_cast<size_t>(GpuMemoryKind::Count)] = {};
            unordered_map<string, uint64_t> name_bytes;
            unordered_map<uint64_t, uint64_t> heap_bytes;
            for (const GpuMemoryBlock& block : blocks)
            {
                kind_bytes[static_cast<size_t>(block.kind)] += block.size;
                name_bytes[block.name[0] ? block.name : "(unnamed)"] += block.size;
                heap_bytes[block.heap_id] = max(heap_bytes[block.heap_id], block.heap_size);
            }
            uint64_t committed = 0;
            for (const auto& [heap, bytes] : heap_bytes)
            {
                committed += bytes;
            }
            SP_LOG_INFO("Gpu memory (%s): device local %.0f MB, tracked %.0f MB in %zu allocations, %zu heaps committing %.0f MB", label, RHI_Device::MemoryGetAllocatedMb(), GpuMemory::GetAllocatedBytes() / mb, blocks.size(), heap_bytes.size(), committed / mb);
            {
                struct heap_use
                {
                    uint64_t used  = 0;
                    uint32_t count = 0;
                    string largest;
                    uint64_t largest_size = 0;
                };
                unordered_map<uint64_t, heap_use> heap_uses;
                for (const GpuMemoryBlock& block : blocks)
                {
                    heap_use& use = heap_uses[block.heap_id];
                    use.used += block.size;
                    use.count++;
                    if (block.size > use.largest_size)
                    {
                        use.largest_size = block.size;
                        use.largest      = block.name;
                    }
                }
                vector<pair<uint64_t, uint64_t>> slack;
                for (const auto& [heap, use] : heap_uses)
                {
                    slack.emplace_back(heap_bytes[heap] - min(heap_bytes[heap], use.used), heap);
                }
                sort(slack.begin(), slack.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
                for (size_t i = 0; i < slack.size() && i < 12 && slack[i].first > 0; i++)
                {
                    const heap_use& use = heap_uses[slack[i].second];
                    SP_LOG_INFO("Gpu memory (%s):   heap %.0f MB, used %.1f MB by %u allocations (largest %s %.1f MB)", label, heap_bytes[slack[i].second] / mb, use.used / mb, use.count, use.largest.c_str(), use.largest_size / mb);
                }
            }
            for (size_t kind = 0; kind < static_cast<size_t>(GpuMemoryKind::Count); kind++)
            {
                if (kind_bytes[kind] > 0)
                {
                    SP_LOG_INFO("Gpu memory (%s):   %s %.0f MB", label, GpuMemory::GetKindName(static_cast<GpuMemoryKind>(kind)), kind_bytes[kind] / mb);
                }
            }
            vector<pair<uint64_t, string>> largest_names;
            for (const auto& [name, bytes] : name_bytes)
            {
                largest_names.emplace_back(bytes, name);
            }
            sort(largest_names.begin(), largest_names.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
            for (size_t i = 0; i < largest_names.size() && i < 30; i++)
            {
                SP_LOG_INFO("Gpu memory (%s):   %8.1f MB %s", label, largest_names[i].first / mb, largest_names[i].second.c_str());
            }
            vector<const GpuMemoryBlock*> textures;
            for (const GpuMemoryBlock& block : blocks)
            {
                if (block.kind == GpuMemoryKind::Texture)
                {
                    textures.push_back(&block);
                }
            }
            sort(textures.begin(), textures.end(), [](const GpuMemoryBlock* a, const GpuMemoryBlock* b) { return a->size > b->size; });
            for (size_t i = 0; i < textures.size() && i < 25; i++)
            {
                const GpuMemoryBlock& block = *textures[i];
                SP_LOG_INFO("Gpu memory (%s):   texture %6.1f MB %ux%ux%u mips %u %s %s %s", label, block.size / mb, block.width, block.height, block.depth, block.mip_count, block.format, block.name, block.path);
            }
        }

        ProgressTask world_progress;
        WorldWorkCounters work_counters;
        vector<Entity*> entities;
        bool memory_steady_log_pending = false;

        void release_uploaded_cpu_geometry()
        {
            uint32_t released = 0;
            for (Entity* entity : entities)
            {
                Render* render = entity ? entity->GetComponent<Render>() : nullptr;
                Mesh* mesh     = render ? render->GetMesh() : nullptr;
                if (mesh && mesh->ReleaseCpuGeometry())
                {
                    released++;
                }
            }
            if (released > 0)
            {
                SP_LOG_INFO("Released cpu geometry of %u uploaded meshes, %u restores so far", released, Mesh::GetCpuGeometryRestoreCount());
            }
        }

        void log_gpu_geometry(const char* label)
        {
            struct group_totals
            {
                uint64_t vertices     = 0;
                uint64_t indices      = 0;
                uint64_t lod0_indices = 0;
                uint32_t meshes       = 0;
                uint32_t entities     = 0;
            };
            unordered_map<const Mesh*, string> mesh_groups;
            unordered_map<string, group_totals> groups;
            for (Entity* entity : entities)
            {
                Render* render = entity ? entity->GetComponent<Render>() : nullptr;
                Mesh* mesh     = render ? render->GetMesh() : nullptr;
                if (!mesh)
                {
                    continue;
                }
                auto found = mesh_groups.find(mesh);
                if (found == mesh_groups.end())
                {
                    string name = mesh->GetObjectName();
                    while (!name.empty() && (isdigit(static_cast<unsigned char>(name.back())) || name.back() == '_'))
                    {
                        name.pop_back();
                    }
                    found = mesh_groups.emplace(mesh, name).first;
                    group_totals& totals = groups[name];
                    totals.vertices += mesh->GetVertexCount();
                    totals.indices  += mesh->GetIndexCount();
                    totals.meshes++;
                    for (uint32_t i = 0; i < mesh->GetSubMeshCount(); i++)
                    {
                        const SubMesh& sub_mesh = mesh->GetSubMesh(i);
                        totals.lod0_indices += sub_mesh.lods.empty() ? 0 : sub_mesh.lods[0].index_count;
                    }
                }
                groups[found->second].entities++;
            }
            vector<pair<uint64_t, string>> largest;
            for (const auto& [name, totals] : groups)
            {
                largest.emplace_back(totals.vertices * sizeof(RHI_Vertex_PosTexNorTan) + totals.indices * sizeof(uint32_t), name);
            }
            sort(largest.begin(), largest.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
            for (size_t i = 0; i < largest.size() && i < 30; i++)
            {
                const group_totals& totals = groups[largest[i].second];
                SP_LOG_INFO("Gpu geometry (%s): %8.1f MB %s, %u meshes, %u entities, %llu vertices, %llu indices (lod0 %llu)", label, largest[i].first / (1024.0 * 1024.0), largest[i].second.c_str(), totals.meshes, totals.entities, totals.vertices, totals.indices, totals.lod0_indices);
            }
        }
        uint64_t work_counter_tick = 0;
        unordered_map<uint64_t, Entity*> entities_by_id; // published entities, guarded by entity_access_mutex
        // Cached views borrow entities owned by the world. Keep invalidation and
        // removal together so no view can retain a destroyed entity.
        struct EntityViews
        {
            vector<Entity*> lights, render, ragdolls, logic, icons, particles, volumes;
            vector<Entity::PreTickGate*> pretick;
            vector<const RenderSceneData*> render_data;
            atomic<bool> render_dirty{true};

            void Clear()
            {
                for (auto* list : {&lights, &render, &ragdolls, &logic, &icons, &particles, &volumes})
                    list->clear();
                pretick.clear();
                render_data.clear();
                render_dirty.store(true, memory_order_relaxed);
            }

            void Remove(Entity* entity)
            {
                for (auto* list : {&lights, &render, &ragdolls, &logic, &icons, &particles, &volumes})
                    erase(*list, entity);
                erase_if(pretick, [entity](const Entity::PreTickGate* gate) { return gate->entity == entity; });
                render_dirty.store(true, memory_order_relaxed);
            }
        } views;
        string file_path;
        string world_name; // cached to avoid per-frame allocation
        string world_description;
        WorldCallbacks callbacks;
        mutex resource_cleanup_mutex;
        vector<string> last_resource_cleanup;
        vector<string> last_resource_cleanup_failures;
        // mcp ai blockout output (project/mcp/blockout), empty means none registered
        string generated_resource_directory;
        // asset viewer curated library (project/mcp/library)
        string library_resource_directory;
        vector<string> world_console_variables; // cvar names overridden by this world (preserved across save/load)
        mutex entity_access_mutex;
        // entities created by workers but not yet drained into the live entities vector, the main thread drains this every tick
        // worker model imports enter this queue only when their EntityBatch is complete
        vector<Entity*> entities_pending;
        thread_local World::EntityBatch* entity_batch = nullptr;
        set<uint64_t> pending_remove;
        uint32_t audio_source_count = 0;
        atomic<bool> resolve = false;
        enum class WorldIoState : uint8_t { Idle, Saving, Loading, Preparing };
        struct WorldIoOperation
        {
            atomic<WorldIoState> state = WorldIoState::Idle;
            atomic<bool> defer_scripts = false;
            atomic<bool> ready_for_commit = false;
            mutex script_mutex;
            vector<pair<int, function<void()>>> scripts;
            shared_ptr<pugi::xml_document> document;
            bool notify_loaded = false;
            bool restore_edit_mode = false;
            string pending_path;
            WorldPreparation preparation;
        };
        WorldIoOperation io;
        BoundingBox bounding_box    = BoundingBox::Unit;
        Entity* camera              = nullptr;
        Entity* camera_override     = nullptr; // set by the sequencer or gameplay, takes precedence over the default camera
        Entity* light               = nullptr;

        struct SaveStateReset
        {
            ~SaveStateReset()
            {
                io.state.store(
                    WorldIoState::Idle,
                    memory_order_release
                );
            }
        };

        // prefer the player flycam (camera under a controller body) over cinematic sequence cameras
        Entity* pick_default_camera(Entity* current, Entity* candidate)
        {
            if (!candidate || !candidate->GetComponent<Camera>())
            {
                return current;
            }

            auto is_player_camera = [](Entity* entity) -> bool
            {
                Entity* parent = entity->GetParent();
                if (!parent)
                {
                    return false;
                }

                Physics* physics = parent->GetComponent<Physics>();
                return physics && physics->GetBodyType() == BodyType::Controller;
            };

            if (!current)
            {
                return candidate;
            }

            if (!is_player_camera(current) && is_player_camera(candidate))
            {
                return candidate;
            }

            return current;
        }

        void untrack_entity(Entity* entity)
        {
            if (!entity)
            {
                return;
            }

            entities_by_id.erase(entity->GetObjectId());

            if (entity == camera)
            {
                camera = nullptr;
            }
            if (entity == camera_override)
            {
                camera_override = nullptr;
            }
            if (entity == light)
            {
                light = nullptr;
            }

            views.Remove(entity);
            erase(entities_pending, entity);
        }

        PlaySession play;

        void compute_bounding_box()
        {
            bounding_box = BoundingBox::Unit;

            for (Entity* entity : entities)
            {
                if (entity->GetActive())
                {
                    if (Render* render = entity->GetComponent<Render>())
                    {
                        bounding_box.Merge(render->GetBoundingBox());
                    }
                }
            }
        }

        bool is_world_in_project_directory(const string& world_file_path)
        {
            // check if the world is in the project directory (has local assets alongside it)
            string normalized_path = world_file_path;
            replace(normalized_path.begin(), normalized_path.end(), '\\', '/');

            string project_dir = ResourceCache::GetProjectDirectory();
            replace(project_dir.begin(), project_dir.end(), '\\', '/');

            // world is in project if path starts with project directory or contains /project/
            return normalized_path.find(project_dir) != string::npos ||
                   normalized_path.find("/project/") != string::npos ||
                   normalized_path.rfind("project/", 0) == 0;
        }

        string world_file_path_to_resource_directory(const string& world_file_path, bool log_directory = true)
        {
            const string world_name = FileSystem::GetFileNameWithoutExtensionFromFilePath(world_file_path);
            string result;

            // if the world is in the project directory, resources are alongside the world file
            if (is_world_in_project_directory(world_file_path))
            {
                result = FileSystem::GetDirectoryFromFilePath(world_file_path) + "/" + world_name + "_resources/";
            }
            else
            {
                // otherwise (worlds/, repo root, etc.), resources go to ./project/
                result = "./" + string(ResourceCache::GetProjectDirectory()) + world_name + "_resources/";
            }

            // normalize to forward slashes
            replace(result.begin(), result.end(), '\\', '/');

            if (log_directory)
                SP_LOG_INFO("World resource directory: %s (from world: %s)", result.c_str(), world_file_path.c_str());
            return result;
        }

        bool path_is_within(
            const string& path,
            const string& directory
        )
        {
            // Generated resources may not have a file yet. They belong to no directory.
            if (path.empty() || directory.empty())
            {
                return false;
            }

            filesystem::path normalized_path =
                filesystem::absolute(path).lexically_normal();
            filesystem::path normalized_directory =
                filesystem::absolute(directory).lexically_normal();

            // Directory names commonly end in a separator, which produces an empty
            // final component and would otherwise reject every file inside them.
            if (normalized_directory.filename().empty() && normalized_directory.has_relative_path())
            {
                normalized_directory = normalized_directory.parent_path();
            }

            auto path_it = normalized_path.begin();
            auto directory_it = normalized_directory.begin();
            for (
                ;
                directory_it != normalized_directory.end();
                ++directory_it, ++path_it
            )
            {
                if (path_it == normalized_path.end())
                {
                    return false;
                }

                string path_part = path_it->string();
                string directory_part = directory_it->string();
                transform(
                    path_part.begin(),
                    path_part.end(),
                    path_part.begin(),
                    [](unsigned char character)
                    {
                        return static_cast<char>(
                            tolower(character)
                        );
                    }
                );
                transform(
                    directory_part.begin(),
                    directory_part.end(),
                    directory_part.begin(),
                    [](unsigned char character)
                    {
                        return static_cast<char>(
                            tolower(character)
                        );
                    }
                );
                if (path_part != directory_part)
                {
                    return false;
                }
            }

            return true;
        }



    }

    void World::ProcessPendingRemovals()
    {
        vector<Entity*> removed;
        unique_lock<mutex> lock(entity_access_mutex);

        if (pending_remove.empty())
        {
            return;
        }

        // Undo can remove an entity before the next frame publishes it. Move only
        // those doomed additions into the removal pass; otherwise clearing the
        // removal queue would let them appear later and survive their own undo.
        for (auto it = entities_pending.begin(); it != entities_pending.end(); )
        {
            Entity* entity = *it;
            if (entity && pending_remove.count(entity->GetObjectId()))
            {
                entities.push_back(entity);
                entities_by_id[entity->GetObjectId()] = entity;
                it = entities_pending.erase(it);
            }
            else
            {
                ++it;
            }
        }

        // Also covers removals queued in edit mode before the play queue existed.
        play.CancelStarts(pending_remove);

        // unlink doomed entities from survivors first, everything is still alive
        // here so no surviving entity is left holding a freed parent or child
        for (Entity* entity : entities)
        {
            if (!entity || pending_remove.count(entity->GetObjectId()) == 0)
            {
                continue;
            }

            if (Entity* parent = entity->GetParent())
            {
                if (pending_remove.count(parent->GetObjectId()) == 0)
                {
                    parent->RemoveChild(entity, false);
                }
            }

            const vector<Entity*> children = entity->GetChildren();
            for (Entity* child : children)
            {
                if (!child)
                {
                    continue;
                }

                if (pending_remove.count(child->GetObjectId()) == 0)
                {
                    child->ClearParent();
                }
            }
        }

        for (auto it = entities.begin(); it != entities.end(); )
        {
            uint64_t id = (*it)->GetObjectId();
            if (pending_remove.count(id) > 0)
            {
                // strip cache lists before delete, pretick still runs this frame before resolve
                untrack_entity(*it);

                Renderer::ResetSceneChanges();
                removed.push_back(*it);
                it = entities.erase(it);
            }
            else
            {
                ++it;
            }
        }

        pending_remove.clear();

        // the tracked lists still point at the entities that were just freed, RemoveEntity
        // only flagged a resolve for the frame that queued the removal, not for this one
        resolve = true;
        lock.unlock();
        for (Entity* entity : removed)
        {
            SP_FIRE_EVENT_DATA(EventType::EntityRemoving, static_cast<void*>(entity));
            delete entity;
        }
    }

    void World::ProcessPendingAdditions()
    {
        lock_guard<mutex> lock(entity_access_mutex);

        if (entities_pending.empty())
        {
            return;
        }

        // publish completed batches; workers must finish component setup before enqueueing
        for (Entity* entity : entities_pending)
            if (entity) entities_by_id[entity->GetObjectId()] = entity;
        entities.insert(entities.end(), entities_pending.begin(), entities_pending.end());
        entities_pending.clear();
        resolve = true;
    }

    void World::Initialize()
    {
        InitializeScripting();
        Environment::ResetSpatialWeather();
    }

    void World::ClearScene()
    {
        CommandStack::Clear();
        Engine::SetFlag(EngineMode::Playing, false); // stop simulation

        // Complete worker construction before visiting components. Stop producers while all
        // entities are still alive, without holding the entity list lock.
        ThreadPool::Flush();
        vector<Entity*> stopping_entities;
        {
            lock_guard lock(entity_access_mutex);
            stopping_entities = entities;
            stopping_entities.insert(stopping_entities.end(), entities_pending.begin(), entities_pending.end());
        }
        for (Entity* entity : stopping_entities) entity->Stop();
        ThreadPool::Flush();
        play.Reset();
        io.preparation.Reset();

        io.defer_scripts.store(false, memory_order_release);
        {
            lock_guard lock(io.script_mutex);
            io.scripts.clear();
        }
        io.document.reset();
        io.ready_for_commit.store(false, memory_order_release);
        io.restore_edit_mode = false;

        world_progress.Finish();

        Renderer::DisableGpuScatter();               // drop renderer references to builder owned scatter meshes/materials
        if (!Engine::IsStartupSmokeTest())
            Renderer::DestroyAccelerationStructures(); // destroy tlas/blas before clearing resources

        // cars hold entity pointers, drop them before entity delete
        if (callbacks.before_entities_destroyed) callbacks.before_entities_destroyed();

        // Detach ownership under the lock; destructors may call back into World.
        vector<Entity*> to_delete;
        vector<Entity*> pending_to_delete;
        {
            lock_guard<mutex> lock(entity_access_mutex);
            camera          = nullptr;
            camera_override = nullptr;
            light           = nullptr;

            // drop the live lists first, destructors that walk GetEntities must not see freed pointers
            to_delete.swap(entities);
            entities_by_id.clear();
            pending_to_delete.swap(entities_pending);
            views.Clear();
            pending_remove.clear();

        }
        for (Entity* entity : to_delete)
        {
            SP_FIRE_EVENT_DATA(EventType::EntityRemoving, static_cast<void*>(entity));
            delete entity;
        }
        for (Entity* entity : pending_to_delete)
        {
            SP_FIRE_EVENT_DATA(EventType::EntityRemoving, static_cast<void*>(entity));
            delete entity;
        }

        if (callbacks.after_entities_destroyed) callbacks.after_entities_destroyed();
        WorldHelpers::Clear();
        SP_FIRE_EVENT(EventType::WorldUnloading);    // editor drops thumbnail pointers before the cache frees them
        Weather::Reset();
        Renderer::ResetSceneChanges();
        resolve = true;
    }

    void World::Shutdown()
    {
        ClearScene();
        ResourceCache::Shutdown();                   // release all resources (textures, materials, meshes, etc)
        camera = nullptr;
        light  = nullptr;
        file_path.clear();
        world_name.clear();
        world_description.clear();
        Environment::SetSettings(EnvironmentSettings{});
        Weather::Reset();

        // every load passes through here, so the next world gets a fresh cloudscape
        Environment::ResetSpatialWeather();

        // clear change tracking

        Renderer::ResetSceneChanges();

        // mark for resolve
        resolve = true;
    }

    void World::RestorePlayState(const shared_ptr<PlayState>& state)
    {
        SP_ASSERT(state && state->document);
        // Imported meshes cache scene hierarchies used as cloning templates. Keep
        // those hidden templates alive with the resource cache, not with play state.
        ThreadPool::Flush();
        ProcessPendingAdditions();
        unordered_set<Entity*> live(entities.begin(), entities.end());
        unordered_set<Entity*> templates;
        vector<pair<shared_ptr<Mesh>, uint64_t>> model_roots;
        for (const auto& resource : ResourceCache::GetByType(ResourceType::Mesh))
        {
            auto mesh = static_pointer_cast<Mesh>(resource);
            Entity* root = mesh->GetRootEntity();
            if (!root || !live.contains(root)) { mesh->SetRootEntity(nullptr); continue; }
            model_roots.emplace_back(mesh, root->GetObjectId());
            if (root->IsTransient() && !root->GetParent())
            {
                vector<Entity*> hierarchy;
                root->GetDescendants(&hierarchy);
                templates.insert(root);
                templates.insert(hierarchy.begin(), hierarchy.end());
            }
            else
            {
                mesh->SetRootEntity(nullptr); // rebind authored roots after loading
            }
        }
        vector<Entity*> retained;
        {
            lock_guard lock(entity_access_mutex);
            erase_if(entities, [&](Entity* entity)
            {
                if (!templates.contains(entity)) return false;
                retained.push_back(entity);
                entities_by_id.erase(entity->GetObjectId());
                return true;
            });
        }
        ClearScene();
        {
            lock_guard lock(entity_access_mutex);
            entities_pending.insert(entities_pending.end(), retained.begin(), retained.end());
        }
        auto world = state->document->child("World");
        if (callbacks.before_load) callbacks.before_load();
        if (callbacks.load) callbacks.load(world);
        Environment::SetSettings(state->environment);
        world_progress = ProgressTracker::Begin(ProgressType::World, "Restoring edit scene", "Loading authored entities");
        io.restore_edit_mode = true;
        io.document = state->document;
        io.defer_scripts.store(true, memory_order_release);
        io.state.store(WorldIoState::Loading, memory_order_release);
        // Reuse the same load and deferred-script paths as file IO and undo.
        for (auto node : world.child("Entities").children("Entity"))
            CreateEntity()->Load(node, true, &state->sculpt);
        for (const auto& [mesh, id] : model_roots) mesh->SetRootEntity(GetEntityById(id));
        io.ready_for_commit.store(true, memory_order_release);
        // Restoration completes in Tick through normal preparation. It must remain in edit mode.
    }

    const WorldWorkCounters& World::GetWorkCounters() { return work_counters; }
    uint64_t World::GetWorkCounterTick() { return work_counter_tick; }

    void World::Tick()
    {
        work_counters = {};
        ++work_counter_tick;
        ScopedWorldWork work_scope(work_counters);
        CountWorldWork(WorldWork::entities_total, entities.size());
        {
            static float census_logged_mb = 0.0f;
            const float heap_mb           = Allocator::GetMemoryAllocatedMb();
            if (heap_mb > census_logged_mb + 1024.0f)
            {
                census_logged_mb = heap_mb;
                Allocator::LogLargestAllocationSites("rising", 12);
            }
        }
        // only world file loads park the tick, model importer progress from warm preloads must not freeze play
        if (io.state.load(memory_order_acquire) == WorldIoState::Loading)
        {
            SP_PROFILE_CPU();
            io.notify_loaded = true;

            // only the main thread may publish into entities, the load worker stages into entities_pending
            if (io.ready_for_commit.exchange(false, memory_order_acq_rel))
            {
                ProcessPendingAdditions();

                io.defer_scripts.store(false, memory_order_release);
                {
                    vector<pair<int, function<void()>>> inits;
                    {
                        lock_guard lock(io.script_mutex);
                        inits.swap(io.scripts);
                    }

                    // run lower order first so lights are configured before heavy world builders populate the scene
                    stable_sort(inits.begin(), inits.end(), [](const pair<int, function<void()>>& a, const pair<int, function<void()>>& b)
                    {
                        return a.first < b.first;
                    });

                    for (pair<int, function<void()>>& init : inits)
                    {
                        init.second();
                    }
                }

                // builder scripts may have spawned more entities
                ProcessPendingAdditions();
                io.document.reset();

                world_progress.SetStep("Preparing terrain, roads and population");
                io.preparation.Begin();
                for (Entity* entity : entities)
                    if (entity->GetActive() && entity->GetComponent<Camera>()) camera = pick_default_camera(camera, entity);
                io.state.store(WorldIoState::Preparing, memory_order_release);

                // fall through so resolve rebuilds views.render before Renderer::Tick
                // returning here left that list empty while the renderer still
                // recorded hashes, so bindless materials never uploaded until a later entity spawn
            }
            else
            {
                return;
            }
        }

        SP_PROFILE_CPU();

        if (IsPreparing())
        {
            if (!io.preparation.Tick(entities, world_progress, callbacks.prepare ? callbacks.prepare : TerrainSystem::PrepareWorld, [] { ProcessPendingRemovals(); })) return;
            world_progress.SetStep("Uploading world geometry");
            generated_cache::SaveChecksumIndex(GetResourceDirectory());
            GeometryBuffer::SaveWorldLoadCapacity(GetResourceDirectory());
            GeometryBuffer::BuildIfDirty();
            SP_LOG_INFO("World preparation complete: %.2f ms", io.preparation.ElapsedMs());
            release_uploaded_cpu_geometry();
            log_cpu_memory("world ready");
            log_gpu_geometry("world ready");
            memory_steady_log_pending = true;
            for (const string& line : generated_cache::GetStatistics()) SP_LOG_INFO("Bake cache %s", line.c_str());
            // Cache eviction only removes reproducible data and runs off the editor thread.
            ThreadPool::AddTask([resources = GetResourceDirectory()]()
            {
                const auto result = generated_cache::Maintain(resources);
                if (result.removed) SP_LOG_INFO("Bake cache reclaimed %.1f MB (%llu files)", result.bytes_removed / 1000000.0, static_cast<unsigned long long>(result.removed));
            });
            world_progress.Finish();
            io.state.store(WorldIoState::Idle, memory_order_release);
        }
        else if (work_counter_tick % 1800 == 0)
        {
            // meshes built after the world became ready, e.g. edited roads
            release_uploaded_cpu_geometry();
            if (memory_steady_log_pending)
            {
                memory_steady_log_pending = false;
                log_cpu_memory("steady");
            }
        }

        // notify listeners on the first tick after loading completes
        // any final pending entities are drained so subscribers see a fully populated scene
        if (io.notify_loaded)
        {
            io.notify_loaded = false;
            ProcessPendingAdditions();
            // force a resolve so the final entity state, deferred script setup like the sun, is rebuilt into the
            // renderer caches, a static world such as empty would otherwise stay on the last unlit loading frame
            resolve = true;
            // drop hashes recorded against an empty views.render during the commit frame gap
            Renderer::ResetSceneChanges();
            SP_FIRE_EVENT(EventType::WorldLoaded);
            if (io.restore_edit_mode)
            {
                Engine::SetFlag(EngineMode::Playing, false);
                io.restore_edit_mode = false;
            }
        }

        // PlaySession owns the transition state; there is no second editor/play flag to drift.
        if (Engine::IsFlagSet(EngineMode::Playing) && !play.IsActive())
        {
            ProcessPendingAdditions();
            play.Begin(entities, callbacks);
        }
        else if (!Engine::IsFlagSet(EngineMode::Playing) && play.IsActive())
        {
            // Finish and publish play-time worker batches before restoring/removing entities.
            ThreadPool::Flush();
            ProcessPendingAdditions();
            play.Stop(callbacks);
            ProcessPendingAdditions();
            if (IsLoadingFromFile()) return;
        }

        if (Engine::IsFlagSet(EngineMode::Playing) && !Engine::IsFlagSet(EngineMode::Paused) && !play.IsStarting())
        {
            if (Light* light = GetDirectionalLight(); light && light->GetFlag(LightFlags::DayNightCycle))
                Environment::Tick(Timer::GetDeltaTimeSec());
        }

        ProcessPendingRemovals();

        // drain capture first, then staged starts, until the scene is ready
        play.Tick();

        // during boot keep rendering, but skip sim ticks and the per entity change scan
        if (!play.IsStarting())
        {
            if (Engine::IsFlagSet(EngineMode::Playing) && !Engine::IsFlagSet(EngineMode::Paused))
                if (callbacks.before_tick) callbacks.before_tick(static_cast<float>(Timer::GetDeltaTimeSec()));

            SP_PROFILE_CPU_START("world_pretick");
            Camera* pretick_camera = GetCamera();
            Vector3 pretick_position = pretick_camera ? pretick_camera->GetEntity()->GetPosition() : Vector3::Zero;
            bool pretick_playing = Engine::IsFlagSet(EngineMode::Playing);
            for (Entity::PreTickGate* gate : views.pretick)
            {
                CountWorldWork(WorldWork::pretick_candidates);
                if (!gate->ShouldRun(pretick_playing, pretick_camera != nullptr, pretick_position))
                {
                    CountWorldWork(WorldWork::pretick_sleeping);
                    continue;
                }
                CountWorldWork(WorldWork::pretick_entities);
                gate->entity->PreTick();
                // A controller or script can move/switch the camera or stop play.
                // Refresh after each callback to preserve the original entity order.
                pretick_camera = GetCamera();
                pretick_position = pretick_camera ? pretick_camera->GetEntity()->GetPosition() : Vector3::Zero;
                pretick_playing = Engine::IsFlagSet(EngineMode::Playing);
            }

            SP_PROFILE_CPU_END();
            Animator::BeginSkinningBatch();
            SP_PROFILE_CPU_START("world_render_tick");
            // renderables cover most of the scene, cull/lod in parallel then finish other components
            const auto& render_entries = GetRenderSceneData();
            const uint32_t render_count = static_cast<uint32_t>(render_entries.size());
            if (render_count > 0)
            {
                if (render_count >= 64)
                {
                    // Physics::Tick only streams this entity's static actors and
                    // serializes scene writes through PhysicsWorld. It can finish
                    // beside its own render update; other components keep
                    // their original order on the main thread after the join.
                    static array<vector<Entity*>, 8> followups;
                    for (auto& list : followups) list.clear();
                    const uint32_t jobs = min(render_count, 8u);
                    std::array<WorldWorkCounters, 8> batch_work;
                    ThreadPool::ParallelLoop([&](uint32_t first_job, uint32_t last_job)
                    {
                        ScopedWorldWork batch_scope(batch_work[first_job]);
                        auto& pending = followups[first_job];
                        const uint32_t start = render_count * first_job / jobs;
                        const uint32_t end = render_count * last_job / jobs;
                        for (uint32_t i = start; i < end; i++)
                        {
                            const RenderSceneData& data = *render_entries[i];
                            Entity* entity = data.entity;
                            if (!entity->GetActive())
                            {
                                continue;
                            }

                            CountWorldWork(WorldWork::render_entities);
                            Render* render = data.render;
                            if (render)
                            {
                                render->Tick();
                            }
                            if (render && entity->CanTickWithParallelRender())
                            {
                                if (Physics* physics = entity->GetComponent<Physics>()) physics->Tick();
                            }
                            else
                                pending.push_back(entity);
                        }
                    }, jobs);

                    for (const auto& batch : batch_work) work_counters.Merge(batch);
                    SP_PROFILE_CPU_START("world_post_render_tick");
                    for (const auto& pending : followups)
                    {
                        for (Entity* entity : pending)
                        {
                            if (entity->GetActive())
                                entity->Tick(false);
                        }
                    }
                    SP_PROFILE_CPU_END();
                }
                else
                {
                    for (Entity* entity : views.render)
                    {
                        if (entity->GetActive())
                        {
                            CountWorldWork(WorldWork::render_entities);
                            entity->Tick();
                        }
                    }
                }
            }
            SP_PROFILE_CPU_END();
            if (callbacks.controls) callbacks.controls();
            SP_PROFILE_CPU_START("world_logic_tick");
            for (Entity* entity : views.logic)
            {
                if (entity->GetActive())
                {
                    CountWorldWork(WorldWork::logic_entities);
                    entity->Tick();
                }
            }

            Animator::FlushSkinningBatch();
            Spline::ProcessPendingRoadMeshes();
            Spline::RebuildRoadJunctions();
            SP_PROFILE_CPU_START("world_road_details");
            if (!ProgressTracker::IsLoading())
                if (callbacks.after_tick) callbacks.after_tick(static_cast<float>(Timer::GetDeltaTimeSec()));
            SP_PROFILE_CPU_END();

            // ragdoll hit capsules after scripts/pedestrians moved the bodies
            SP_PROFILE_CPU_START("world_ragdoll_sync");
            for (Entity* entity : views.ragdolls)
            {
                if (entity->GetActive())
                {
                    if (Ragdoll* ragdoll = entity->GetComponent<Ragdoll>())
                    {
                        ragdoll->LateTick();
                    }
                }
            }
            SP_PROFILE_CPU_END();

            SP_PROFILE_CPU_END();
        }

        // after the camera settled for the frame, its drops, grid and sound follow it
        Weather::Tick(static_cast<float>(Timer::GetDeltaTimeSec()));

        ProcessPendingAdditions();

        // resolve if needed
        if (resolve)
        {
            // track entities
            {
                // the pick below walks the entity list, and that list is reordered as entities come and
                // go, so a blind re-pick can hand the seat to a different camera on any frame where an
                // entity changed, which swaps the view, the frustum and the temporal history at once
                Entity* camera_incumbent   = camera;
                bool camera_incumbent_live = false;

                camera             = nullptr;
                light              = nullptr;
                audio_source_count = 0;
                views.Clear();
                for (Entity* entity : entities)
                {
                    // still in the live list until next removal flush, skip so draw does not
                    // read a mesh the owner already freed this tick
                    if (pending_remove.count(entity->GetObjectId()) > 0)
                    {
                        continue;
                    }

                    if (entity->GetComponent<Volume>())
                    {
                        views.volumes.push_back(entity);
                    }

                    if (entity->GetActive())
                    {
                        camera = pick_default_camera(camera, entity);
                        camera_incumbent_live |= entity == camera_incumbent;

                        if (Light* light_comp = entity->GetComponent<Light>())
                        {
                            if (!light && light_comp->GetLightType() == LightType::Directional)
                            {
                                light = entity;
                            }
                            views.lights.push_back(entity);
                        }

                        const bool has_render = entity->GetComponent<Render>() != nullptr;
                        if (has_render)
                        {
                            views.render.push_back(entity);
                        }
                        else if (entity->GetComponentCount() > 0)
                        {
                            // lights, scripts, audio, etc without a mesh still need Entity::Tick
                            views.logic.push_back(entity);
                        }

                        if (entity->GetComponent<Ragdoll>())
                        {
                            views.ragdolls.push_back(entity);
                        }

                        if (
                            entity->GetComponent<Physics>() ||
                            entity->GetComponent<Script>() ||
                            entity->GetComponent<Ragdoll>()
                        )
                        {
                            views.pretick.push_back(entity->GetPreTickGate());
                        }

                        if (entity->GetComponent<AudioSource>())
                        {
                            audio_source_count++;
                        }

                        if (entity->GetComponent<ParticleSystem>())
                        {
                            views.particles.push_back(entity);
                        }

                        // editor icons, skip empty and render-only props
                        const uint32_t component_count = entity->GetComponentCount();
                        if (component_count > 0 && !(component_count == 1 && has_render))
                        {
                            views.icons.push_back(entity);
                        }
                    }
                }

                // the incumbent keeps the seat unless it is gone, went inactive, or a player camera showed up
                if (camera_incumbent_live)
                {
                    camera = pick_default_camera(camera_incumbent, camera);
                }
            }

            compute_bounding_box();
            resolve = false;

        }

    }

    string World::GetResourceDirectory()
    {
        return file_path.empty() ? string() : world_file_path_to_resource_directory(file_path, false);
    }

    string World::GetResourceDirectory(
        const string& world_file_path
    )
    {
        return world_file_path_to_resource_directory(
            world_file_path
        );
    }

    void World::SetGeneratedResourceDirectory(
        const string& directory
    )
    {
        generated_resource_directory = directory;
        replace(
            generated_resource_directory.begin(),
            generated_resource_directory.end(),
            '\\',
            '/'
        );
        if (
            !generated_resource_directory.empty() &&
            generated_resource_directory.back() != '/'
        )
        {
            generated_resource_directory += '/';
        }
    }

    const string& World::GetGeneratedResourceDirectory()
    {
        if (generated_resource_directory.empty())
        {
            SetGeneratedResourceDirectory(
                string(ResourceCache::GetProjectDirectory()) +
                "mcp/blockout/"
            );
        }
        return generated_resource_directory;
    }

    void World::SetLibraryResourceDirectory(
        const string& directory
    )
    {
        library_resource_directory = directory;
        replace(
            library_resource_directory.begin(),
            library_resource_directory.end(),
            '\\',
            '/'
        );
        if (
            !library_resource_directory.empty() &&
            library_resource_directory.back() != '/'
        )
        {
            library_resource_directory += '/';
        }
    }

    const string& World::GetLibraryResourceDirectory()
    {
        if (library_resource_directory.empty())
        {
            SetLibraryResourceDirectory(
                string(ResourceCache::GetProjectDirectory()) +
                "mcp/library/"
            );
        }
        return library_resource_directory;
    }

    vector<string> World::GetLastResourceCleanup()
    {
        lock_guard<mutex> lock(resource_cleanup_mutex);
        return last_resource_cleanup;
    }

    vector<string>
        World::GetLastResourceCleanupFailures()
    {
        lock_guard<mutex> lock(resource_cleanup_mutex);
        return last_resource_cleanup_failures;
    }

    bool World::SaveToFile(string file_path)
    {
        WorldIoState expected = WorldIoState::Idle;
        if (
            !io.state.compare_exchange_strong(
                expected,
                WorldIoState::Saving
            )
        )
        {
            SP_LOG_WARNING("A world save is already in progress");
            return false;
        }

        SaveStateReset reset;
        try
        {
            return SaveToFileInternal(file_path, false);
        }
        catch (const exception& error)
        {
            SP_LOG_ERROR("Failed to save world '%s': %s", file_path.c_str(), error.what());
            return false;
        }
    }

    bool World::SaveToFileAsync(string file_path)
    {
        WorldIoState expected = WorldIoState::Idle;
        if (
            !io.state.compare_exchange_strong(
                expected,
                WorldIoState::Saving
            )
        )
        {
            SP_LOG_WARNING("A world save is already in progress");
            return false;
        }

        // Capture live state on the caller; resource writes, XML formatting and cleanup run on a worker.
        try
        {
            if (SaveToFileInternal(file_path, true))
            {
                return true; // the worker now owns resetting the save state
            }
        }
        catch (const exception& error)
        {
            SP_LOG_ERROR("Failed to save world '%s': %s", file_path.c_str(), error.what());
        }

        io.state.store(WorldIoState::Idle, memory_order_release);
        return false;
    }

    bool World::IsSaving()
    {
        return
            io.state.load(memory_order_acquire) ==
            WorldIoState::Saving;
    }

    bool World::SaveToFileInternal(string file_path, bool asynchronous)
    {
        if (file_path.empty())
        {
            SP_LOG_ERROR("Cannot save a world without a file path");
            return false;
        }

        if (FileSystem::GetExtensionFromFilePath(file_path) != EXTENSION_WORLD)
        {
            file_path += string(EXTENSION_WORLD);
        }

        auto progress = ProgressTracker::Begin(ProgressType::World, FileSystem::GetFileNameFromFilePath(file_path),
            "Saving world", ProgressMode::Background);
        const Stopwatch timer;

        const auto save_started = filesystem::file_time_type::clock::now();
        vector<function<void()>> writes;
        function<void()> cleanup;
        set<string> owned_files = world_resources::ReadOwnedFiles(file_path);

        // Resolve identities and capture live data on the owner thread. Disk work
        // below only consumes owned snapshots and can safely outlive this frame.
        {
            string directory = world_file_path_to_resource_directory(file_path);
            writes.push_back([directory] {
                filesystem::create_directories(directory);
            });

            // terrain sculpt layers are world data written by the component, not resources, they
            // go next to the world so a regenerate on load finds them
            {
                lock_guard<mutex> lock(entity_access_mutex);
                for (Entity* entity : entities)
                {
                    if (!entity || entity->IsTransient())
                    {
                        continue;
                    }

                    if (Terrain* terrain = entity->GetComponent<Terrain>())
                    {
                        writes.push_back(terrain->CreateSculptSaveTask(directory));
                    }
                }
            }
            // mcp raw blockout and curated library live outside the world, leave them alone
            const string generated_directory =
                World::GetGeneratedResourceDirectory();
            const string library_directory =
                World::GetLibraryResourceDirectory();
            auto is_mcp_owned = [generated_directory, library_directory](const string& path) -> bool
            {
                if (path.empty())
                {
                    return false;
                }
                if (
                    !generated_directory.empty() &&
                    path_is_within(path, generated_directory)
                )
                {
                    return true;
                }
                if (
                    !library_directory.empty() &&
                    path_is_within(path, library_directory)
                )
                {
                    return true;
                }
                return false;
            };

            vector<shared_ptr<IResource>> resources = ResourceCache::GetResourcesSnapshot();
            set<IResource*> referenced_resources;
            auto reference_material =
                [&referenced_resources](Material* material)
            {
                if (material == nullptr)
                {
                    return;
                }
                referenced_resources.insert(material);
                for (RHI_Texture* texture : material->GetTextures())
                {
                    if (texture != nullptr)
                    {
                        referenced_resources.insert(texture);
                    }
                }
            };
            {
                lock_guard<mutex> lock(entity_access_mutex);
                for (Entity* entity : entities)
                {
                    if (entity == nullptr)
                    {
                        continue;
                    }
                    if (Render* render = entity->GetComponent<Render>())
                    {
                        if (Mesh* mesh = render->GetMesh())
                        {
                            referenced_resources.insert(mesh);
                        }
                        if (!render->IsUsingDefaultMaterial())
                        {
                            reference_material(
                                render->GetMaterial()
                            );
                        }
                    }
                    if (
                        ParticleSystem* particles =
                            entity->GetComponent<ParticleSystem>()
                    )
                    {
                        if (RHI_Texture* texture = particles->GetTexture())
                        {
                            referenced_resources.insert(texture);
                        }
                    }
                    if (
                        Terrain* terrain =
                            entity->GetComponent<Terrain>()
                    )
                    {
                        if (
                            RHI_Texture* height_map =
                                terrain->GetHeightMapSeed()
                        )
                        {
                            referenced_resources.insert(height_map);
                        }
                        reference_material(
                            terrain->GetMaterial().get()
                        );
                    }
                    if (
                        Spline* spline =
                            entity->GetComponent<Spline>()
                    )
                    {
                        const string& mesh_path =
                            spline->GetInstanceMeshPath();
                        if (
                            !mesh_path.empty()
                        )
                        {
                            if (
                                shared_ptr<Mesh> mesh =
                                    ResourceCache::GetByPath<Mesh>(
                                        mesh_path
                                    )
                            )
                            {
                                referenced_resources.insert(
                                    mesh.get()
                                );
                            }
                        }
                    }
                }
            }

            // the windows file system is case insensitive so the uniqueness check has to be too
            auto to_file_key = [](const string& file_name)
            {
                string key = file_name;
                transform(key.begin(), key.end(), key.begin(), [](unsigned char c) { return static_cast<char>(tolower(c)); });
                return key;
            };

            // give every resource a unique file, duplicate object names used to overwrite each other and break Render::Load
            struct PendingResourceSave
            {
                IResource* resource;
                string target_path;
                bool path_changed;
            };

            vector<PendingResourceSave> pending_saves;
            set<string> used_file_names;
            set<string> reserved_file_names;
            for (const auto& resource : resources)
            {
                if (path_is_within(resource->GetResourceFilePath(), directory))
                {
                    const string key = to_file_key(FileSystem::GetFileNameFromFilePath(resource->GetResourceFilePath()));
                    reserved_file_names.insert(key);
                    // Deferred textures can have no CPU data to save, but their files
                    // are still referenced by materials and must survive pruning.
                    if (referenced_resources.count(resource.get()) != 0)
                    {
                        used_file_names.insert(key);
                        const string name = FileSystem::GetFileNameFromFilePath(resource->GetResourceFilePath());
                        if (resource->IsPersistent() && world_resources::IsOwnedFileName(name)) owned_files.insert(name);
                    }
                }
            }
            for (shared_ptr<IResource>& resource : resources)
            {
                if (
                    referenced_resources.find(resource.get()) ==
                    referenced_resources.end()
                )
                {
                    continue;
                }
                // runtime generated resources are rebuilt by code on load, serializing them only accumulates orphans
                if (!resource->IsPersistent())
                {
                    continue;
                }

                string ext;
                switch (resource->GetResourceType())
                {
                    case ResourceType::Texture:
                    {
                        // only save textures that can be saved (compressed with data)
                        // others will be re-imported from source path when material loads
                        RHI_Texture* texture = static_cast<RHI_Texture*>(resource.get());
                        if (!texture->CanSaveToFile())
                        {
                            continue;
                        }
                        ext = EXTENSION_TEXTURE;
                        break;
                    }
                    case ResourceType::Material: ext = EXTENSION_MATERIAL; break;
                    case ResourceType::Mesh:     ext = EXTENSION_MESH;     break;
                    default: continue;
                }

                const string current_path =
                    resource->GetResourceFilePath();
                if (is_mcp_owned(current_path))
                {
                    continue;
                }

                // strip an embedded extension so a name that already carries one does not save with it doubled
                string name = resource->GetObjectName();
                if (name.size() > ext.size() && name.compare(name.size() - ext.size(), ext.size(), ext) == 0)
                {
                    name = name.substr(0, name.size() - ext.size());
                }

                // Keep existing world-local identities stable regardless of load order. New
                // resources must not claim a filename already owned by a saved texture/mesh.
                string target_path = current_path;
                if (!path_is_within(current_path, directory))
                {
                    string unique_name = name;
                    uint32_t suffix = 2;
                    while (used_file_names.count(to_file_key(unique_name + ext)) != 0 ||
                           reserved_file_names.count(to_file_key(unique_name + ext)) != 0 ||
                           FileSystem::Exists(directory + unique_name + ext))
                    {
                        unique_name = name + "_" + to_string(suffix++);
                    }
                    target_path = directory + unique_name + ext;
                }
                used_file_names.insert(to_file_key(FileSystem::GetFileNameFromFilePath(target_path)));

                const bool path_changed  = resource->GetResourceFilePath() != FileSystem::GetRelativePath(target_path);
                resource->SetResourceFilePath(target_path);
                pending_saves.push_back({ resource.get(), target_path, path_changed });
                owned_files.insert(FileSystem::GetFileNameFromFilePath(target_path));
            }


            // Materials must persist the texture paths assigned above, even when their own path stayed the same.
            for (const PendingResourceSave& pending : pending_saves)
            {
                const bool immutable = pending.resource->GetResourceType() != ResourceType::Material;
                if (immutable && !pending.path_changed && FileSystem::Exists(pending.target_path))
                {
                    continue;
                }
                if (auto write = pending.resource->CreateSaveTask(pending.target_path))
                    writes.push_back(move(write));
            }

            cleanup = [save_started, directory, used_file_names = move(used_file_names), to_file_key, is_mcp_owned, owned_files]() mutable
            {
                // prefabs on disk reference meshes and materials that no live entity owns,
                // without protecting them a save turns every saved prefab into dangling references
                {
                    auto protect_prefab_references =
                        [&used_file_names, &to_file_key](
                            const string& prefab_path
                        )
                    {
                        pugi::xml_document prefab_document;
                        if (!prefab_document.load_file(prefab_path.c_str()))
                        {
                            throw runtime_error("Cannot inspect prefab: " + prefab_path);
                        }

                        vector<pugi::xml_node> pending =
                        {
                            prefab_document.document_element()
                        };
                        while (!pending.empty())
                        {
                            const pugi::xml_node node = pending.back();
                            pending.pop_back();
                            for (
                                pugi::xml_node child = node.first_child();
                                child;
                                child = child.next_sibling()
                            )
                            {
                                pending.push_back(child);
                            }

                            for (
                                const char* attribute :
                                {
                                    "mesh_path",
                                    "material_path"
                                }
                            )
                            {
                                const string reference =
                                    node.attribute(attribute).as_string();
                                if (!reference.empty())
                                {
                                    used_file_names.insert(
                                        to_file_key(
                                            FileSystem::GetFileNameFromFilePath(
                                                reference
                                            )
                                        )
                                    );
                                }
                            }
                        }
                    };

                    try
                    {
                        for (
                            const filesystem::directory_entry& entry :
                            filesystem::recursive_directory_iterator(directory)
                        )
                        {
                            if (
                                entry.is_regular_file() &&
                                FileSystem::IsEnginePrefabFile(
                                    entry.path().string()
                                )
                            )
                            {
                                protect_prefab_references(
                                    entry.path().string()
                                );
                            }
                        }
                    }
                    catch (const exception& e)
                    {
                        SP_LOG_WARNING(
                            "Failed to scan prefabs for referenced resources: %s",
                            e.what()
                        );
                        return; // Never prune when reference discovery was incomplete.
                    }
                }

                // Only prune explicitly owned resource files. Unlisted assets, sculpt data and caches are untouched.
                vector<string> removed, failures;
                for (const string& name : owned_files)
                {
                    const string existing_file = (filesystem::path(directory) / name).string();
                    // Files created or edited after the snapshot belong to a later edit.
                    error_code time_error;
                    const auto modified = filesystem::last_write_time(existing_file, time_error);
                    if (time_error || modified >= save_started) continue;
                    if (is_mcp_owned(existing_file))
                    {
                        continue;
                    }

                    if (used_file_names.find(to_file_key(FileSystem::GetFileNameFromFilePath(existing_file))) == used_file_names.end())
                    {
                        if (FileSystem::Delete(existing_file))
                        {
                            removed.push_back(
                                existing_file
                            );
                        }
                        else
                        {
                            failures.push_back(
                                existing_file
                            );
                        }
                    }
                }

                // pruning used to be silent, which made deleted resources look like
                // files that never existed
                for (
                    size_t index = 0;
                    index < removed.size();
                    index++
                )
                {
                    if (index == 10)
                    {
                        SP_LOG_INFO(
                            "Pruned %zu more unreferenced resource files",
                            removed.size() - index
                        );
                        break;
                    }

                    SP_LOG_INFO(
                        "Pruned unreferenced resource: %s",
                        removed[index].c_str()
                    );
                }
                {
                    lock_guard<mutex> lock(resource_cleanup_mutex);
                    last_resource_cleanup = move(removed);
                    last_resource_cleanup_failures = move(failures);
                }
            };
        }

        // create document
        auto document = make_shared<pugi::xml_document>();
        auto& doc = *document;
        pugi::xml_node world_node = doc.append_child("World");
        world_resources::WriteOwnedFiles(world_node, owned_files);
        world_node.append_attribute("name")        = FileSystem::GetFileNameWithoutExtensionFromFilePath(file_path).c_str();
        world_node.append_attribute("description") = world_description.c_str();
        if (callbacks.save) callbacks.save(world_node);
        auto environment_node = world_node.append_child("Environment");
        Environment::Save(environment_node);

        // console variables (only those explicitly overridden by this world are persisted)
        if (!world_console_variables.empty())
        {
            pugi::xml_node cvars_node = world_node.append_child("ConsoleVariables");
            for (const string& cvar_name : world_console_variables)
            {
                if (cvar_name == "r.fog.debug")
                    continue;
                optional<string> value = ConsoleRegistry::Get().GetValueAsString(cvar_name);
                if (!value.has_value())
                {
                    continue;
                }

                pugi::xml_node var_node = cvars_node.append_child("Variable");
                var_node.append_attribute("name")  = cvar_name.c_str();
                var_node.append_attribute("value") = value->c_str();
            }
        }

        // entities
        {
            // node
            pugi::xml_node entities_node = world_node.append_child("Entities");

            // get root entities, save them, and they will save their children recursively
            vector<Entity*> root_entities;
            World::GetRootEntities(root_entities);

            // progress tracking
            progress.SetStep("Serializing entities");

            // write entities to node while the world is still owned by this thread
            for (Entity* root : root_entities)
            {
                // transient entities are runtime only, such as skid mark trails, they must never be serialized
                if (root->IsTransient())
                {
                    continue;
                }

                pugi::xml_node entity_node = entities_node.append_child("Entity");
                root->Save(entity_node);
            }

        }

        const float snapshot_ms = timer.GetElapsedTimeMs();
        auto write_snapshot = [progress, file_path, resources = world_file_path_to_resource_directory(file_path, false), document, writes = move(writes), cleanup = move(cleanup), snapshot_ms]() mutable
        {
            progress.SetStep("Writing resources and world");
            const Stopwatch write_timer;
            for (auto& write : writes) write();

            // Commit only a fully written document. A failed write must leave the
            // previous world intact, and cleanup must wait until that commit.
            const string temporary_path = file_path + ".tmp";
            if (!document->save_file(temporary_path.c_str(), " ", pugi::format_indent))
                throw runtime_error("Failed to write world: " + temporary_path);
            filesystem::rename(temporary_path, file_path);
            cleanup();
            const auto cache_result = generated_cache::Maintain(resources);
            if (cache_result.removed) SP_LOG_INFO("Bake cache reclaimed %.1f MB (%llu files)", cache_result.bytes_removed / 1000000.0, static_cast<unsigned long long>(cache_result.removed));
            SP_LOG_INFO("World '%s' saved: snapshot %.2f ms, background work %.2f ms", file_path.c_str(), snapshot_ms, write_timer.GetElapsedTimeMs());
        };

        if (asynchronous)
        {
            ThreadPool::AddTask([write_snapshot = move(write_snapshot)]() mutable {
                SaveStateReset reset;
                try { write_snapshot(); }
                catch (const exception& error) { SP_LOG_ERROR("World save failed: %s", error.what()); }
            });
        }
        else
        {
            write_snapshot();
        }
        return true;
    }

    bool World::LoadFromFile(const string& file_path_)
    {
        if (file_path_.empty())
        {
            return false;
        }

        // imgui still holds texture ids from this frame, shutdown must wait until the next tick
        io.pending_path = file_path_;
        return true;
    }

    bool World::IsLoadingFromFile()
    {
        const auto state = io.state.load(memory_order_acquire);
        return state == WorldIoState::Loading || state == WorldIoState::Preparing;
    }

    bool World::IsPreparing()
    {
        return io.state.load(memory_order_acquire) == WorldIoState::Preparing;
    }

    void World::ProcessPendingLoad()
    {
        if (io.pending_path.empty())
        {
            return;
        }

        const string file_path_ = io.pending_path;
        io.pending_path.clear();

        // reject re-entrant loads, the second Shutdown below would tear down the world while the first load's workers are still building it
        WorldIoState expected = WorldIoState::Idle;
        if (
            !io.state.compare_exchange_strong(
                expected,
                WorldIoState::Loading
            )
        )
        {
            return;
        }

        // ensure prefabs are registered before loading
        if (callbacks.before_load) callbacks.before_load();

        // Include teardown/job waits in the same visible task and elapsed time.
        // Shutdown finishes the previous world's handle, so retain this one locally.
        auto load_progress = ProgressTracker::Begin(ProgressType::World,
            FileSystem::GetFileNameFromFilePath(file_path_), "Finishing previous world work");
        // shutdown synchronously before async loading
        Shutdown();
        Renderer::ResetWorldGeometry();
        generated_cache::ResetStatistics();

        // publish the loading state now so the progress ui shows this frame instead of only once the worker task starts
        world_progress = move(load_progress);
        world_progress.SetStep("Reading world");

        // copy path for the lambda capture
        string path_copy = file_path_;

        // load asynchronously
        ThreadPool::AddTask([path_copy]()
        {
            // clears the loading state and releases the guard, must run on every exit path
            auto finish = []()
            {
                world_progress.Finish();
                io.state.store(
                    WorldIoState::Idle,
                    memory_order_release
                );
            };

            try
            {
                file_path  = path_copy;
                world_name = FileSystem::GetFileNameFromFilePath(file_path);

                // start timing
                const Stopwatch timer;

                // load xml document, kept alive until main thread finishes deferred script init
                shared_ptr<pugi::xml_document> doc = make_shared<pugi::xml_document>();
                pugi::xml_parse_result result = doc->load_file(file_path.c_str());
                if (!result)
                {
                    SP_LOG_ERROR("Failed to load XML file: %s", result.description());
                    finish();
                    return;
                }
                io.document = doc;

                // get world node
                pugi::xml_node world_node = doc->child("World");
                if (!world_node)
                {
                    SP_LOG_ERROR("No 'World' node found.");
                    io.document.reset();
                    finish();
                    return;
                }

                generated_cache::LoadChecksumIndex(GetResourceDirectory());
                GeometryBuffer::ReserveForWorldLoad(GetResourceDirectory());

                // deserialize the resources before loading the world (XML), as it references them
                {
                    string directory = world_file_path_to_resource_directory(file_path);

                    {
                        // The directory is an index for legacy name-only references,
                        // not a manifest: old imports and removed assets can remain here.
                        const vector<string> directory_files = FileSystem::IsDirectory(directory)
                            ? FileSystem::GetFilesInDirectory(directory) : vector<string>{};
                        vector<string> files;
                        unordered_set<string> resource_paths;
                        unordered_multimap<string, string> resources_by_name;
                        auto is_native = [](const string& path)
                        {
                            return FileSystem::IsEngineMeshFile(path) || FileSystem::IsEngineTextureFile(path) ||
                                FileSystem::IsEngineMaterialFile(path);
                        };
                        for (const string& path : directory_files)
                            if (is_native(path)) resources_by_name.emplace(FileSystem::GetFileNameWithoutExtensionFromFilePath(path), path);
                        unordered_map<string, bool> dependency_exists;
                        auto add_dependency = [&](const string& path) -> bool
                        {
                            if (path.empty() || !is_native(path)) return false;
                            if (auto found = dependency_exists.find(path); found != dependency_exists.end()) return found->second;
                            string key = filesystem::absolute(path).lexically_normal().generic_string();
#ifdef _WIN32
                            transform(key.begin(), key.end(), key.begin(), [](unsigned char c) { return static_cast<char>(tolower(c)); });
#endif
                            const bool exists = resource_paths.contains(key) || FileSystem::IsFile(path);
                            dependency_exists[path] = exists;
                            if (exists && resource_paths.insert(key).second) files.push_back(path);
                            return exists;
                        };
                        function<void(pugi::xml_node)> collect_dependencies = [&](pugi::xml_node node)
                        {
                            // Ownership includes retired files awaiting cleanup; it is not a live reference.
                            if (strcmp(node.name(), "OwnedResources") == 0) return;
                            for (pugi::xml_attribute attribute : node.attributes())
                            {
                                const string value = attribute.as_string();
                                add_dependency(value);
                                // IResource derives its lookup name from the filename stem.
                                const string name = attribute.name();
                                if (name.ends_with("_name"))
                                {
                                    const string saved_path = node.attribute((name.substr(0, name.size() - 5) + "_path").c_str()).as_string();
                                    if (add_dependency(saved_path)) continue;
                                    const auto [begin, end] = resources_by_name.equal_range(value);
                                    for (auto it = begin; it != end; ++it) add_dependency(it->second);
                                }
                            }
                            for (pugi::xml_node child : node.children()) collect_dependencies(child);
                        };
                        collect_dependencies(world_node);
                        // Include native texture dependencies before the parallel material
                        // pass. Foreign images remain deferred so alpha merging precedes compression.
                        for (size_t i = 0; i < files.size(); ++i)
                        {
                            if (!FileSystem::IsEngineMaterialFile(files[i])) continue;
                            pugi::xml_document material;
                            if (material.load_file(files[i].c_str())) collect_dependencies(material);
                        }
                        SP_LOG_INFO("World dependencies: %zu native resources referenced, %zu files in resource directory",
                            files.size(), directory_files.size());

                        // bucket files by type so we can fan each bucket out across the thread pool
                        // sequential loads here used to dominate world load time on texture heavy scenes
                        vector<string> texture_paths;
                        vector<string> mesh_paths;
                        vector<string> material_paths;
                        texture_paths.reserve(files.size());
                        mesh_paths.reserve(files.size());
                        material_paths.reserve(files.size());

                        for (const string& path : files)
                        {
                            const string file_name =
                                FileSystem::GetFileNameFromFilePath(path);
                            if (
                                file_name.rfind("car_", 0) == 0 &&
                                file_name.find("_packed_slot") != string::npos
                            )
                            {
                                continue;
                            }

                            // the terrain reads its own caches while it generates, loading them here as
                            // ordinary resources would build the whole terrain mesh a second time
                            if (
                                file_name.rfind("terrain_cache", 0)      == 0 ||
                                file_name.rfind("terrain_mesh_cache", 0) == 0
                            )
                            {
                                continue;
                            }

                            if (FileSystem::IsEngineTextureFile(path))
                            {
                                texture_paths.push_back(path);
                            }
                            else if (FileSystem::IsEngineMeshFile(path))
                            {
                                mesh_paths.push_back(path);
                            }
                            else if (FileSystem::IsEngineMaterialFile(path))
                            {
                                material_paths.push_back(path);
                            }
                        }

                        // progress counts only what is actually loaded below, counting every file on disk
                        // left the unclassified ones permanently outstanding and pinned the bar under 100 percent
                        uint32_t resource_count = static_cast<uint32_t>(
                            texture_paths.size() + mesh_paths.size() + material_paths.size()
                        );
                        if (resource_count > 0)
                        {
                            world_progress.SetStep("Loading resources");
                        }

                        // pass 1, textures and meshes are independent, fan them out together
                        // ResourceCache::Load uses a per-path in-flight lock so concurrent loads of the same path are deduplicated,
                        // RHI_Texture::PrepareForGpu transitions state via compare_exchange_strong so it is safe across threads
                        {
                            struct ResourceJob
                            {
                                enum class Type : uint8_t { Texture, Mesh } type;
                                string path;
                                uint64_t size_bytes = 0;
                            };

                            vector<ResourceJob> jobs;
                            jobs.reserve(texture_paths.size() + mesh_paths.size());
                            auto add_job = [&jobs](const ResourceJob::Type type, const string& path)
                            {
                                error_code ignored;
                                const uint64_t size_bytes = filesystem::file_size(path, ignored);
                                jobs.push_back({ type, path, ignored ? 0ull : size_bytes });
                            };
                            for (const string& path : texture_paths)
                            {
                                add_job(ResourceJob::Type::Texture, path);
                            }
                            for (const string& path : mesh_paths)
                            {
                                add_job(ResourceJob::Type::Mesh, path);
                            }

                            if (!jobs.empty())
                            {
                                // largest first, a 30 mb mesh picked up last would otherwise run alone at the end
                                sort(jobs.begin(), jobs.end(), [](const ResourceJob& a, const ResourceJob& b)
                                {
                                    return a.size_bytes > b.size_bytes;
                                });

                                // the loop only decides how many workers take part, each one pulls the next job
                                // from a shared counter so a worker that lands on small files keeps going while
                                // another is still inside a big one, static ranges left most of the pool idle
                                atomic<uint32_t> next_job = 0;
                                const uint32_t job_count  = static_cast<uint32_t>(jobs.size());
                                ThreadPool::ParallelLoop([&jobs, &next_job, job_count, resource_count](uint32_t, uint32_t)
                                {
                                    for (uint32_t i = next_job.fetch_add(1, memory_order_relaxed); i < job_count; i = next_job.fetch_add(1, memory_order_relaxed))
                                    {
                                        if (jobs[i].type == ResourceJob::Type::Texture)
                                        {
                                            if (shared_ptr<RHI_Texture> texture = ResourceCache::Load<RHI_Texture>(jobs[i].path, RHI_Texture_Stream))
                                            {
                                                texture->PrepareForGpu();
                                            }
                                        }
                                        else
                                        {
                                            ResourceCache::Load<Mesh>(jobs[i].path);
                                        }

                                        if (resource_count > 0)
                                        {
                                            world_progress.SetDetail(jobs[i].path);
                                        }
                                    }
                                }, job_count);
                            }
                        }

                        // pass 2, materials reference textures by path so they must run after the texture pass completes
                        if (!material_paths.empty())
                        {
                            atomic<uint32_t> next_material = 0;
                            const uint32_t material_count  = static_cast<uint32_t>(material_paths.size());
                            ThreadPool::ParallelLoop([&material_paths, &next_material, material_count, resource_count](uint32_t, uint32_t)
                            {
                                for (uint32_t i = next_material.fetch_add(1, memory_order_relaxed); i < material_count; i = next_material.fetch_add(1, memory_order_relaxed))
                                {
                                    ResourceCache::Load<Material>(material_paths[i]);

                                    if (resource_count > 0)
                                    {
                                        world_progress.SetDetail(material_paths[i]);
                                    }
                                }
                            }, material_count);
                        }
                    }
                }

                SP_LOG_INFO("World load: resources %.2f ms", timer.GetElapsedTimeMs());

                // read metadata
                world_description = world_node.attribute("description").as_string();
                if (callbacks.load) callbacks.load(world_node);
                Environment::Load(world_node);

                // console variables: apply any cvars defined by the world
                // format:
                //   <ConsoleVariables>
                //     <Variable name="r.restir_pt" value="1" />
                //   </ConsoleVariables>
                world_console_variables.clear();
                // A transport inspection view must not leak into another world.
                cvar_fog_debug.SetValue(0.0f);
                // Atmosphere density belongs to the world, not the last scene or
                // the editor's saved graphics settings. Persist these controls even
                // when the source world predates them, so UI edits survive saving.
                for (const char* name : { "r.atmosphere.mist_density", "r.atmosphere.mist_height",
                                         "r.atmosphere.ground_mist", "r.atmosphere.mist_variation" })
                {
                    if (ConsoleVariable* cvar = ConsoleRegistry::Get().Find(name))
                    {
                        *cvar->m_value_ptr = cvar->m_default_value;
                        world_console_variables.emplace_back(name);
                    }
                }
                if (pugi::xml_node cvars_node = world_node.child("ConsoleVariables"))
                {
                    for (pugi::xml_node var_node = cvars_node.child("Variable"); var_node; var_node = var_node.next_sibling("Variable"))
                    {
                        const char* name  = var_node.attribute("name").as_string();
                        const char* value = var_node.attribute("value").as_string();

                        if (string_view(name) == "r.fog.debug")
                            continue;
                        if (name && name[0] != '\0')
                        {
                            ConsoleRegistry::Get().SetValueFromString(name, value);
                            // Canonicalize legacy fog names; old worlds keep their
                            // exact mist values and save with the human-facing names.
                            const ConsoleVariable* cvar = ConsoleRegistry::Get().Find(name);
                            const string canonical_name = cvar ? string(cvar->m_name) : string(name);
                            if (find(world_console_variables.begin(), world_console_variables.end(), canonical_name) == world_console_variables.end())
                                world_console_variables.emplace_back(canonical_name);
                        }
                    }
                }

                // entities
                {
                    // get node
                    pugi::xml_node entities_node = world_node.child("Entities");
                    if (!entities_node)
                    {
                        SP_LOG_ERROR("No 'Entities' node found.");
                        io.document.reset();
                        finish();
                        return;
                    }

                    // flatten the entity tree so every node can load in parallel
                    // parent_index is into this same vector, UINT32_MAX means root
                    struct FlatEntity
                    {
                        pugi::xml_node node;
                        uint32_t parent_index = UINT32_MAX;
                    };

                    vector<FlatEntity> flat_entities;
                    {
                        function<void(pugi::xml_node, uint32_t)> collect = [&](pugi::xml_node node, uint32_t parent_index)
                        {
                            const uint32_t index = static_cast<uint32_t>(flat_entities.size());
                            flat_entities.push_back({ node, parent_index });

                            for (pugi::xml_node child = node.child("Entity"); child; child = child.next_sibling("Entity"))
                            {
                                collect(child, index);
                            }
                        };

                        for (pugi::xml_node entity_node = entities_node.child("Entity"); entity_node; entity_node = entity_node.next_sibling("Entity"))
                        {
                            collect(entity_node, UINT32_MAX);
                        }
                    }

                    // progress tracking
                    uint32_t entity_count = static_cast<uint32_t>(flat_entities.size());
                    world_progress.SetStep("Loading entities");

                    // defer script lua execution, lua is single threaded and cannot run across the worker threads below
                    {
                        lock_guard lock(io.script_mutex);
                        io.scripts.clear();
                    }
                    io.defer_scripts.store(true, memory_order_release);

                    // create and load every entity without hierarchy, children are wired after
                    // keep this sequential on the load worker, component Initialize/Load is not safe
                    // across the pool (audio cache, water gpu buffers, renderer ocean state)
                    vector<Entity*> loaded_entities(entity_count, nullptr);
                    if (entity_count > 0)
                    {
                        for (uint32_t i = 0; i < entity_count; i++)
                        {
                            Entity* entity = World::CreateEntity();
                            const Stopwatch entity_timer;
                            entity->Load(flat_entities[i].node, false);
                            if (entity_timer.GetElapsedTimeMs() > 100.0)
                            {
                                SP_LOG_INFO("World load: entity '%s' %.2f ms", entity->GetObjectName().c_str(), entity_timer.GetElapsedTimeMs());
                            }
                            loaded_entities[i] = entity;
                            world_progress.SetFraction(static_cast<float>(i + 1) / static_cast<float>(entity_count));
                        }

                        // wire parents in document order so each parent exists before its children attach
                        for (uint32_t i = 0; i < entity_count; i++)
                        {
                            const uint32_t parent_index = flat_entities[i].parent_index;
                            if (parent_index == UINT32_MAX)
                            {
                                continue;
                            }

                            SP_ASSERT(parent_index < loaded_entities.size());
                            SP_ASSERT(loaded_entities[i] != nullptr);
                            SP_ASSERT(loaded_entities[parent_index] != nullptr);
                            loaded_entities[i]->SetParent(loaded_entities[parent_index]);
                        }
                    }

                    // leave entities in entities_pending, only the main thread may publish into the live vector
                }

                // report time
                SP_LOG_INFO("World \"%s\" has been loaded. Duration %.2f ms", file_path.c_str(), timer.GetElapsedTimeMs());

                // hand off publish + deferred script init to World::Tick, loading stays up until that finishes
                io.ready_for_commit.store(true, memory_order_release);
            }
            catch (const exception& error)
            {
                SP_LOG_ERROR("World load failed: %s", error.what());
                io.document.reset();
                finish();
            }
        });
    }


    World::EntityBatch::EntityBatch() : previous(entity_batch) { entity_batch = this; }
    World::EntityBatch::~EntityBatch()
    {
        entity_batch = previous;
        // Deletion happens outside entity_access_mutex, just like normal removal.
        for (Entity* entity : entities) delete entity;
    }

    void World::EntityBatch::Commit()
    {
        if (previous)
        {
            previous->entities.insert(previous->entities.end(), entities.begin(), entities.end());
        }
        else
        {
            lock_guard lock(entity_access_mutex);
            entities_pending.insert(entities_pending.end(), entities.begin(), entities.end());
            resolve = true;
        }
        entities.clear();
    }

    Entity* World::CreateEntity()
    {
        lock_guard lock(entity_access_mutex);

        Entity* entity = new Entity();
        // worker builders use EntityBatch; main-thread creation is published at the next drain
        if (entity_batch) entity_batch->entities.push_back(entity);
        else entities_pending.push_back(entity);
        resolve = true;


        return entity;
    }

    bool World::IsDeferringScriptInit()
    {
        return io.defer_scripts.load(memory_order_acquire);
    }

    void World::AddDeferredScriptInit(int order, function<void()>&& init)
    {
        lock_guard lock(io.script_mutex);
        io.scripts.emplace_back(order, std::move(init));
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
            play.CancelStarts(ids_to_remove);

            // detach from parent so it won't hold a dangling pointer after deferred deletion
            if (Entity* parent = entity_to_remove->GetParent())
            {
                parent->RemoveChild(entity_to_remove, false);
            }
        }

        resolve = true;
    }

    bool World::CancelPendingRemoval(Entity* entity)
    {
        if (!entity) return false;
        lock_guard<mutex> lock(entity_access_mutex);
        if (!pending_remove.count(entity->GetObjectId())) return false;
        vector<Entity*> descendants;
        entity->GetDescendants(&descendants);
        descendants.push_back(entity);
        for (auto descendant : descendants) pending_remove.erase(descendant->GetObjectId());
        resolve = true;
        return true;
    }

    void World::RemoveEntityImmediate(Entity* entity_to_remove)
    {
        // Main thread only; EntityRemoving lets owners release external references.
        SP_ASSERT_MSG(entity_to_remove != nullptr, "Entity is null");
        SP_ASSERT_MSG(!ProgressTracker::IsLoading(), "Immediate delete is unsafe during world loading");

        unique_lock<mutex> lock(entity_access_mutex);

        // get the entity and all of its descendants
        vector<Entity*> entities_to_remove;
        entities_to_remove.push_back(entity_to_remove);
        entity_to_remove->GetDescendants(&entities_to_remove);

        set<uint64_t> ids_to_remove;
        for (Entity* entity : entities_to_remove)
            ids_to_remove.insert(entity->GetObjectId());
        play.CancelStarts(ids_to_remove);

        // detach from the parent before deleting, re-acquiring here would keep the
        // doomed entity in the list because it is still part of the world
        if (Entity* parent = entity_to_remove->GetParent())
        {
            parent->RemoveChild(entity_to_remove, false);
        }

        // remove and delete immediately
        for (Entity* entity : entities_to_remove)
        {
            uint64_t id = entity->GetObjectId();

            // any child that outlives this entity must not keep a freed parent
            const vector<Entity*> children = entity->GetChildren();
            for (Entity* child : children)
            {
                const bool child_survives =
                    child &&
                    find(
                        entities_to_remove.begin(),
                        entities_to_remove.end(),
                        child
                    ) == entities_to_remove.end();
                if (child_survives)
                {
                    child->ClearParent();
                }
            }

            // remove from entities vector
            auto it = find(entities.begin(), entities.end(), entity);
            if (it != entities.end())
            {
                Renderer::ResetSceneChanges();
                entities.erase(it);
            }

            std::erase(entities_pending, entity);
            untrack_entity(entity);

            pending_remove.erase(id);

        }

        resolve = true;
        lock.unlock();
        for (Entity* entity : entities_to_remove)
        {
            SP_FIRE_EVENT_DATA(EventType::EntityRemoving, static_cast<void*>(entity));
            delete entity;
        }
    }

    void World::GetRootEntities(vector<Entity*>& entities_out)
    {
        lock_guard<mutex> lock(entity_access_mutex);

        entities_out.clear();
        entities_out.reserve(entities.size() + entities_pending.size());

        // include committed entities
        for (Entity* entity : entities)
        {
            if (!entity->GetParent())
            {
                entities_out.emplace_back(entity);
            }
        }

        // also include entities that are still pending, important during world loading when prefabs reference entities that haven't been drained yet
        for (Entity* entity : entities_pending)
        {
            if (!entity->GetParent())
            {
                entities_out.emplace_back(entity);
            }
        }
    }

    void World::MoveEntityToIndex(Entity* entity, uint32_t index)
    {
        if (!entity)
        {
            return;
        }

        lock_guard<mutex> lock(entity_access_mutex);

        // find the entity in the list
        auto it = find(entities.begin(), entities.end(), entity);
        if (it == entities.end())
        {
            return;
        } // entity not found

        // get current position before removing
        uint32_t current_index = static_cast<uint32_t>(distance(entities.begin(), it));

        // remove from current position
        entities.erase(it);

        // adjust target index if the entity was before the target position
        // (removing it shifts all subsequent indices down by 1)
        if (current_index < index && index > 0)
        {
            index--;
        }

        // clamp index to valid range
        if (index > entities.size())
        {
            index = static_cast<uint32_t>(entities.size());
        }

        // insert at new position
        entities.insert(entities.begin() + index, entity);
    }

    void World::MoveRootEntityNear(Entity* entity_to_move, Entity* target_entity, bool insert_after)
    {
        if (!entity_to_move || !target_entity)
        {
            return;
        }

        // both must be root entities (no parent)
        if (entity_to_move->GetParent() || target_entity->GetParent())
        {
            return;
        }

        lock_guard<mutex> lock(entity_access_mutex);

        // find and remove the entity to move
        auto move_it = find(entities.begin(), entities.end(), entity_to_move);
        if (move_it == entities.end())
        {
            return;
        }
        entities.erase(move_it);

        // find the target entity's position (after removal of entity_to_move)
        auto target_it = find(entities.begin(), entities.end(), target_entity);
        if (target_it == entities.end())
        {
            // target not found, put entity_to_move back at end
            entities.push_back(entity_to_move);
            return;
        }

        // insert before or after the target
        if (insert_after)
        {
            ++target_it;
        }

        entities.insert(target_it, entity_to_move);
    }

    Entity* World::GetEntityById(const uint64_t id)
    {
        // A builder may resolve its own unpublished hierarchy, other threads cannot.
        for (EntityBatch* batch = entity_batch; batch; batch = batch->previous)
            for (Entity* entity : batch->entities)
                if (entity->GetObjectId() == id) return entity;
        lock_guard<mutex> lock(entity_access_mutex);

        const auto found = entities_by_id.find(id);
        if (found != entities_by_id.end()) return found->second;

        // entities created this frame are not drained yet, a lookup that misses them
        // makes callers think their own freshly created entity died
        for (const auto& entity : entities_pending)
        {
            if (entity && entity->GetObjectId() == id)
            {
                return entity;
            }
        }

        return nullptr;
    }

    const vector<Entity*>& World::GetEntities()
    {
        return entities;
    }

    const vector<Entity*>& World::GetEntitiesLights()
    {
        return views.lights;
    }

    void World::InvalidateRenderSceneData()
    {
        views.render_dirty.store(true, memory_order_relaxed);
    }

    const vector<const RenderSceneData*>& World::GetRenderSceneData()
    {
        // Read on the owner thread before dispatch. Component construction/destruction
        // may invalidate this from a loader, but workers never resize the published view.
        if (views.render_dirty.exchange(false, memory_order_relaxed))
        {
            views.render_data.clear();
            views.render_data.reserve(views.render.size());
            for (Entity* entity : views.render)
                if (Render* render = entity->GetComponent<Render>())
                    views.render_data.push_back(&render->GetSceneData());
        }
        return views.render_data;
    }

    const vector<Entity*>& World::GetEntitiesWithRender()
    {
        return views.render;
    }

    const vector<Entity*>& World::GetEntitiesWithIcon()
    {
        return views.icons;
    }

    const vector<Entity*>& World::GetEntitiesWithParticles()
    {
        return views.particles;
    }

    const vector<Entity*>& World::GetEntitiesWithVolume()
    {
        return views.volumes;
    }

    bool World::IsPlayBooting()
    {
        return play.IsStarting() || (Engine::IsFlagSet(EngineMode::Playing) && !play.IsActive());
    }

    const string& World::GetName()
    {
        return world_name;
    }

    const string& World::GetFilePath()
    {
        return file_path;
    }

    void World::SetFilePath(const string& path)
    {
        file_path  = path;
        world_name = FileSystem::GetFileNameFromFilePath(file_path);
    }

    BoundingBox& World::GetBoundingBox()
    {
        return bounding_box;
    }

    Camera* World::GetCamera()
    {
        if (camera_override && camera_override->GetActive())
        {
            if (Camera* component = camera_override->GetComponent<Camera>())
            {
                return component;
            }
        }

        return camera ? camera->GetComponent<Camera>() : nullptr;
    }

    void World::SetActiveCamera(Entity* entity)
    {
        camera_override = entity;
    }

    Entity* World::GetActiveCameraOverride()
    {
        return camera_override;
    }

    Light* World::GetDirectionalLight()
    {
        return light ? light->GetComponent<Light>() : nullptr;
    }

    uint32_t World::GetLightCount()
    {
        return static_cast<uint32_t>(views.lights.size());
    }

    uint32_t World::GetAudioSourceCount()
    {
        return audio_source_count;
    }

    float World::GetTimeOfDay(bool use_real_world_time)
    {
        return Environment::GetTimeOfDay(use_real_world_time);
    }

    void World::SetTimeOfDay(float time_of_day)
    {
        if (time_of_day < 0.0f)
        {
            time_of_day = 0.0f;
        }
        else if (time_of_day > 1.0f)
        {
            time_of_day = 1.0f;
        }
        Environment::SetTimeOfDay(time_of_day);
    }

    EnvironmentState World::GetEnvironment()
    {
        Light* light = GetDirectionalLight();
        return Environment::Evaluate(light && light->GetFlag(LightFlags::DayNightCycle) && light->GetFlag(LightFlags::RealTimeCycle),
            Environment::GetCloudCoverageEffective(), GetWind().Length());
    }

    const Vector3& World::GetWind()
    {
        return Environment::GetWind();
    }

    Vector3 World::SampleWind(const Vector3& position, float time)
    {
        return Environment::SampleWind(position, time);
    }

    void World::SetWind(const Vector3& wind) { Environment::SetWind(wind); }

    float World::GetPuddliness()
    {
        return Environment::GetPuddliness();
    }

    void World::SetPuddliness(float value) { Environment::SetPuddliness(value); }

    const Vector2& World::GetCloudSeedOffset()
    {
        return Environment::GetCloudSeedOffset();
    }

    const string& World::GetDescription()
    {
        return world_description;
    }

    void World::SetDescription(const string& description)
    {
        world_description = description;
    }

    void World::SetCallbacks(const WorldCallbacks& value)
    {
        callbacks = value;
    }

    bool World::ReadMetadata(const string& world_file_path, WorldMetadata& metadata)
    {
        // load xml document
        pugi::xml_document doc;
        pugi::xml_parse_result result = doc.load_file(world_file_path.c_str());
        if (!result)
        {
            SP_LOG_ERROR("Failed to load world file for metadata: %s", result.description());
            return false;
        }

        // get world node
        pugi::xml_node world_node = doc.child("World");
        if (!world_node)
        {
            SP_LOG_ERROR("No 'World' node found in: %s", world_file_path.c_str());
            return false;
        }

        // read metadata
        metadata.file_path   = world_file_path;
        metadata.name        = world_node.attribute("name").as_string();
        metadata.description = world_node.attribute("description").as_string();

        return true;
    }
}
