/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

//= INCLUDES ============================
#include "pch.h"
#include "AssetThumbnails.h"
#include "Editor.h"
#include "widgets/AssetViewer.h"
#include "core/Engine.h"
#include "core/Event.h"
#include "core/ProgressTracker.h"
#include "core/ThreadPool.h"
#include "file_system/FileSystem.h"
#include "geometry/Mesh.h"
#include "io/pugixml.hpp"
#include "rendering/Material.h"
#include "rendering/Renderer.h"
#include "resource/ResourceCache.h"
#include "rhi/RHI_Device.h"
#include "rhi/RHI_Texture.h"
#include "world/Entity.h"
#include "world/Prefab.h"
#include "world/World.h"
#include "world/components/Camera.h"
#include "world/components/Light.h"
#include "world/components/Render.h"
#include <algorithm>
#include <cctype>
#include <chrono>
#include <filesystem>
#include <future>
#include <iomanip>
#include <sstream>
#include <unordered_map>
#include <unordered_set>
//=======================================

using namespace std;
using namespace spartan;
using namespace spartan::math;

namespace
{
    using clock_type = chrono::steady_clock;

    // square and small, the browser draws at most 200 px per card and the images are read back and decoded
    constexpr uint32_t thumbnail_size = 256;
    // part of every cache name, bumping it retires every image rendered with an older studio setup
    constexpr uint32_t cache_version = 2;
    // decoded thumbnails kept on the gpu, the least recently seen are dropped past this
    constexpr size_t max_resident_textures = 512;
    constexpr uint32_t max_image_loads = 4;
    constexpr uint32_t max_attempts = 3;
    // a terrain tile or a baked city block is not something anyone browses for, and loading it only
    // to draw a 256 px image would park its geometry in the global buffer for the rest of the session
    constexpr uintmax_t max_mesh_file_bytes = 96ull * 1024 * 1024;
    // how long a file on screen goes before its size and write time are read again
    constexpr chrono::milliseconds stamp_check_interval(2000);
    // an item counts as on screen when it asked within this many frames
    constexpr uint64_t visible_frames = 3;
    // every render takes the frame from the viewport, so the viewport always gets a few in between
    constexpr uint64_t frames_between_renders = 3;
    constexpr uint32_t minimum_settle_frames = 3;
    constexpr uint32_t stable_frames_required = 2;
    constexpr chrono::milliseconds settle_timeout(6000);
    constexpr chrono::milliseconds capture_timeout(3000);
    constexpr chrono::milliseconds write_timeout(4000);
    const char* cache_directory_name = "generated_cache/thumbnails/";

    enum class asset_kind
    {
        none,
        material,
        mesh,
        prefab
    };

    enum class entry_state
    {
        // size and write time not read yet, so which image belongs to it is not known
        unchecked,
        // the image exists on disk and waits for a decode slot
        cached,
        loading,
        // no image for the current stamp, waits for a render
        queued,
        rendering,
        ready,
        failed
    };

    struct thumbnail_entry
    {
        string path;
        string cache_path;
        string stamp;
        asset_kind kind = asset_kind::none;
        entry_state state = entry_state::unchecked;
        // can hold the previous image while a newer one renders, so an edited asset never blinks to a glyph
        shared_ptr<RHI_Texture> texture;
        uint64_t requested_frame = 0;
        clock_type::time_point checked_at;
        uint32_t attempts = 0;
    };

    enum class job_stage
    {
        idle,
        preparing,
        settling,
        capturing,
        writing
    };

    struct job_state
    {
        job_stage stage = job_stage::idle;
        string key;
        string path;
        string cache_path;
        asset_kind kind = asset_kind::none;
        future<void> preparation;
        // written by the preparation task, read only once its future is ready
        vector<shared_ptr<IResource>> loaded;
        shared_ptr<Material> material;
        shared_ptr<Mesh> mesh;
        bool prepared = false;
        // what the cache held before the job, anything the job adds on top of this is the job's to release
        unordered_set<const IResource*> resident_before;
        uint64_t root_id = 0;
        uint64_t camera_id = 0;
        uint64_t signature = 0;
        uint64_t generation = 0;
        uint32_t settle_frames = 0;
        uint32_t stable_frames = 0;
        clock_type::time_point stage_started;
    };

    struct decoded_image
    {
        string key;
        string cache_path;
        shared_ptr<RHI_Texture> texture;
    };

    unordered_map<string, thumbnail_entry> entries;
    job_state job;
    vector<future<void>> image_loads;
    mutex decoded_mutex;
    vector<decoded_image> decoded;
    uint64_t frame = 0;
    uint64_t last_render_frame = 0;
    uint64_t world_unloading_handle = 0;
    bool subscribed = false;

    string lowercase(string text)
    {
        transform(text.begin(), text.end(), text.begin(), [](const unsigned char c)
        {
            return static_cast<char>(tolower(c));
        });
        return text;
    }

    // the key the resource cache would use for the same file, so a browser path and a world reference meet
    string normalize(const string& path)
    {
        return lowercase(filesystem::path(FileSystem::GetRelativePath(path)).lexically_normal().generic_string());
    }

    string loadable_path(const string& path)
    {
        return filesystem::path(FileSystem::GetRelativePath(path)).lexically_normal().generic_string();
    }

    uint64_t fnv1a(const string& text)
    {
        uint64_t hash = 14695981039346656037ull;
        for (const unsigned char c : text)
        {
            hash ^= static_cast<uint64_t>(c);
            hash *= 1099511628211ull;
        }
        return hash;
    }

    string hex(const uint64_t value)
    {
        stringstream stream;
        stream << std::hex << setw(16) << setfill('0') << value;
        return stream.str();
    }

    asset_kind kind_from_path(const string& path)
    {
        if (FileSystem::IsEngineMaterialFile(path))
        {
            return asset_kind::material;
        }
        if (FileSystem::IsEngineMeshFile(path))
        {
            return asset_kind::mesh;
        }
        if (FileSystem::IsEnginePrefabFile(path))
        {
            return asset_kind::prefab;
        }
        return asset_kind::none;
    }

    // images live beside what they depict, in the generated cache of the top level project folder the asset
    // sits in, which is disposable and never packaged, an asset outside the project gets no thumbnail
    string cache_directory_for(const string& key)
    {
        string project = normalize(ResourceCache::GetProjectDirectory());
        while (!project.empty() && project.back() == '/')
        {
            project.pop_back();
        }
        if (project.empty() || key.size() <= project.size() + 1 || key.compare(0, project.size() + 1, project + "/") != 0)
        {
            return "";
        }

        const size_t folder_begin = project.size() + 1;
        const size_t folder_end = key.find('/', folder_begin);
        if (folder_end == string::npos)
        {
            return "";
        }

        // the path's own casing is kept for the folder, the key is lowercase
        const string original = loadable_path(key);
        return original.substr(0, folder_end) + "/" + cache_directory_name;
    }

    string read_stamp(const string& path)
    {
        error_code error;
        const uintmax_t size = filesystem::file_size(path, error);
        if (error)
        {
            return "";
        }
        const auto time = filesystem::last_write_time(path, error);
        if (error)
        {
            return "";
        }
        return to_string(size) + ":" + to_string(time.time_since_epoch().count()) + ":" + to_string(cache_version);
    }

    string cache_path_for(const string& key, const string& stamp)
    {
        const string directory = cache_directory_for(key);
        if (directory.empty())
        {
            return "";
        }
        return directory + hex(fnv1a(key)) + "_" + hex(fnv1a(stamp)).substr(0, 8) + ".png";
    }

    // reads the stamp and decides whether the image for it is on disk, an unchanged stamp leaves the entry alone
    void refresh_entry(const string& key, thumbnail_entry& entry)
    {
        entry.checked_at = clock_type::now();
        const string stamp = read_stamp(entry.path);
        if (stamp.empty())
        {
            entry.state = entry_state::failed;
            return;
        }
        if (stamp == entry.stamp && entry.state != entry_state::unchecked)
        {
            return;
        }

        entry.stamp = stamp;
        entry.cache_path = cache_path_for(key, stamp);
        entry.attempts = 0;
        if (entry.cache_path.empty())
        {
            entry.state = entry_state::failed;
            return;
        }
        // a job already rendering this entry is rendering the old stamp, let it land and pick the change up after
        if (entry.state == entry_state::rendering)
        {
            return;
        }
        entry.state = FileSystem::Exists(entry.cache_path) ? entry_state::cached : entry_state::queued;
    }

    // older images of the same asset are retired once a newer one is written
    void delete_stale_images(const string& cache_path)
    {
        const filesystem::path image(cache_path);
        const string file_name = image.filename().string();
        const size_t separator = file_name.find('_');
        if (separator == string::npos)
        {
            return;
        }
        const string prefix = file_name.substr(0, separator + 1);

        error_code error;
        for (filesystem::directory_iterator it(image.parent_path(), error), end; !error && it != end; it.increment(error))
        {
            const string name = it->path().filename().string();
            if (name != file_name && name.compare(0, prefix.size(), prefix) == 0)
            {
                error_code remove_error;
                filesystem::remove(it->path(), remove_error);
            }
        }
    }

    void start_image_load(const string& key, thumbnail_entry& entry)
    {
        entry.state = entry_state::loading;
        const string cache_path = entry.cache_path;
        image_loads.emplace_back(ThreadPool::AddTask([key, cache_path]()
        {
            // private to the browser, the resource cache would hand these to anything asking for the path
            shared_ptr<RHI_Texture> texture = make_shared<RHI_Texture>();
            texture->SetFlag(RHI_Texture_DeferUpload);
            texture->LoadFromFile(cache_path);
            if (texture->GetWidth() == 0 || texture->GetHeight() == 0)
            {
                texture.reset();
            }
            else
            {
                texture->PrepareForGpu();
            }
            delete_stale_images(cache_path);

            lock_guard<mutex> lock(decoded_mutex);
            decoded.push_back({ key, cache_path, texture });
        }));
    }

    void collect_prefab_references(const string& path, vector<string>& meshes, vector<string>& materials)
    {
        pugi::xml_document document;
        if (!document.load_file(path.c_str()))
        {
            return;
        }

        vector<pugi::xml_node> pending = { document.document_element() };
        while (!pending.empty())
        {
            const pugi::xml_node node = pending.back();
            pending.pop_back();
            for (pugi::xml_node child = node.first_child(); child; child = child.next_sibling())
            {
                pending.push_back(child);
            }

            const string mesh_path = node.attribute("mesh_path").as_string();
            if (!mesh_path.empty() && find(meshes.begin(), meshes.end(), mesh_path) == meshes.end())
            {
                meshes.push_back(mesh_path);
            }
            const string material_path = node.attribute("material_path").as_string();
            if (!material_path.empty() && find(materials.begin(), materials.end(), material_path) == materials.end())
            {
                materials.push_back(material_path);
            }
        }
    }

    shared_ptr<Material> load_material(const string& path)
    {
        // a .xml file is a material only when it says so, sequencer and importer xml share the extension
        {
            pugi::xml_document document;
            if (!document.load_file(path.c_str()) || !document.child("Material"))
            {
                return nullptr;
            }
        }

        shared_ptr<Material> material = ResourceCache::Load<Material>(path);
        // packing and compressing is the expensive part, done here it never lands on the main thread
        if (material && material->GetResourceState() == ResourceState::Max)
        {
            material->PrepareForGpu();
        }
        return material;
    }

    shared_ptr<Mesh> load_mesh(const string& path)
    {
        error_code error;
        const uintmax_t bytes = filesystem::file_size(path, error);
        if (error || bytes > max_mesh_file_bytes)
        {
            return nullptr;
        }

        shared_ptr<Mesh> mesh = ResourceCache::Load<Mesh>(path);
        if (!mesh || mesh->GetVertexCount() == 0)
        {
            return nullptr;
        }
        return mesh;
    }

    // runs on a worker, everything slow about an asset is loading and preparing it, not drawing it
    void prepare(job_state* state)
    {
        switch (state->kind)
        {
            case asset_kind::material:
            {
                state->material = load_material(state->path);
                if (state->material)
                {
                    state->loaded.push_back(state->material);
                }
                state->prepared = state->material != nullptr;
                break;
            }
            case asset_kind::mesh:
            {
                state->mesh = load_mesh(state->path);
                if (state->mesh)
                {
                    state->loaded.push_back(state->mesh);
                }
                state->prepared = state->mesh != nullptr;
                break;
            }
            case asset_kind::prefab:
            {
                // warmed here so the prefab load on the main thread finds everything cached and prepared
                vector<string> meshes;
                vector<string> materials;
                collect_prefab_references(state->path, meshes, materials);
                for (const string& mesh_path : meshes)
                {
                    if (FileSystem::Exists(mesh_path))
                    {
                        if (shared_ptr<Mesh> mesh = load_mesh(mesh_path))
                        {
                            state->loaded.push_back(mesh);
                        }
                    }
                }
                for (const string& material_path : materials)
                {
                    if (FileSystem::Exists(material_path))
                    {
                        if (shared_ptr<Material> material = load_material(material_path))
                        {
                            state->loaded.push_back(material);
                        }
                    }
                }
                pugi::xml_document document;
                state->prepared = document.load_file(state->path.c_str()) && document.child("Prefab");
                break;
            }
            default:
                break;
        }
    }

    Entity* job_root()
    {
        return job.root_id != 0 ? World::GetEntityById(job.root_id) : nullptr;
    }

    void collect_entities(Entity* root, vector<Entity*>& entities)
    {
        entities.clear();
        if (!root)
        {
            return;
        }
        entities.push_back(root);
        root->GetDescendants(&entities);
    }

    // everything that decides what the frame would draw, a change means the scene is still assembling
    uint64_t scene_signature(Entity* root, bool& materials_ready)
    {
        materials_ready = true;
        vector<Entity*> entities;
        collect_entities(root, entities);

        uint64_t signature = 1469598103934665603ull;
        const auto mix = [&signature](const uint64_t value)
        {
            signature = (signature ^ value) * 1099511628211ull;
        };

        mix(entities.size());
        for (Entity* entity : entities)
        {
            Render* render = entity ? entity->GetComponent<Render>() : nullptr;
            if (!render)
            {
                continue;
            }
            Mesh* mesh = render->GetMesh();
            Material* material = render->GetMaterial();
            mix(reinterpret_cast<uint64_t>(mesh));
            mix(reinterpret_cast<uint64_t>(material));
            if (mesh)
            {
                mix(mesh->GetIndexCount());
            }
            if (material)
            {
                const ResourceState state = material->GetResourceState();
                mix(static_cast<uint64_t>(state));
                materials_ready &= state == ResourceState::PreparedForGpu;
            }
        }
        return signature;
    }

    void create_studio(Entity* root, const bool is_material)
    {
        // the bounds are read from world matrices, which only an active hierarchy keeps current
        root->SetActive(true);

        vector<Entity*> entities;
        collect_entities(root, entities);
        vector<BoundingBox> render_bounds;
        for (Entity* entity : entities)
        {
            entity->SetTransient(true);
            Render* render = entity->GetComponent<Render>();
            if (!render || !render->GetMesh())
            {
                continue;
            }
            render_bounds.push_back(render->GetBoundingBoxMesh() * entity->GetMatrix());
        }

        // parts that a component places at runtime (car wheels, for example) can sit far from the rest
        // until it ticks, so pieces whose centre is far from the median centre are left out of the framing
        vector<bool> keep(render_bounds.size(), true);
        if (render_bounds.size() >= 4)
        {
            auto median = [](vector<float> values)
            {
                nth_element(values.begin(), values.begin() + values.size() / 2, values.end());
                return values[values.size() / 2];
            };
            vector<float> xs, ys, zs;
            for (const BoundingBox& box : render_bounds)
            {
                xs.push_back(box.GetCenter().x);
                ys.push_back(box.GetCenter().y);
                zs.push_back(box.GetCenter().z);
            }
            const Vector3 median_center(median(xs), median(ys), median(zs));
            vector<float> distances, extents;
            for (const BoundingBox& box : render_bounds)
            {
                distances.push_back(Vector3::Distance(box.GetCenter(), median_center));
                extents.push_back(box.GetExtents().Length());
            }
            const float threshold = max(median(distances) * 4.0f, median(extents) * 2.0f);
            for (size_t i = 0; i < render_bounds.size(); i++)
            {
                keep[i] = distances[i] <= threshold;
            }
        }

        bool found = false;
        BoundingBox bounds;
        for (size_t i = 0; i < render_bounds.size(); i++)
        {
            if (!keep[i])
            {
                continue;
            }
            if (!found)
            {
                bounds = render_bounds[i];
                found = true;
            }
            else
            {
                bounds.Merge(render_bounds[i]);
            }
        }
        if (!found)
        {
            bounds = BoundingBox(Vector3(-0.5f), Vector3(0.5f));
        }
        const Vector3 center = bounds.GetCenter();
        const float radius = max(0.001f, bounds.GetSize().Length() * 0.5f);

        Entity* rig = World::CreateEntity();
        rig->SetObjectName("asset_thumbnail_rig");
        rig->SetTransient(true);
        rig->SetParent(root);

        // three quarter view from slightly above, the same angle for every asset so a folder reads as a set
        const float yaw = is_material ? 0.55f : 0.65f;
        const float pitch = is_material ? 0.25f : 0.38f;
        // models face +z, so the camera sits on that side to show their front three quarters
        const float facing = is_material ? -1.0f : 1.0f;
        const Vector3 direction(cos(pitch) * sin(yaw), sin(pitch), facing * cos(pitch) * cos(yaw));

        Entity* camera_entity = World::CreateEntity();
        camera_entity->SetObjectName("asset_thumbnail_camera");
        camera_entity->SetTransient(true);
        camera_entity->SetParent(rig);
        Camera* camera = camera_entity->AddComponent<Camera>();
        const float fov = 35.0f;
        camera->SetFovHorizontalDeg(fov);
        // fixed exposure, the auto exposure would carry over from whatever the previous asset was
        camera->SetExposureMode(CameraExposureMode::manual);
        camera->SetAperture(11.0f);
        camera->SetShutterSpeed(1.0f / 250.0f);
        camera->SetIso(100.0f);
        // the enclosing sphere of the box would leave a sphere at half the frame and a long plank as a speck,
        // so the box corners are fitted to the square frustum from the angle the camera actually looks
        const float tan_half = tan(fov * deg_to_rad * 0.5f) * (is_material ? 0.9f : 0.86f);
        float distance = 0.0f;
        if (is_material)
        {
            const float sphere_radius = max(0.001f, bounds.GetExtents().x);
            distance = sphere_radius / sin(atan(tan_half));
        }
        else
        {
            const Vector3 forward = -direction;
            const Vector3 right = Vector3::Cross(Vector3::Up, forward).Normalized();
            const Vector3 up = Vector3::Cross(forward, right);
            const Vector3 minimum = bounds.GetMin();
            const Vector3 maximum = bounds.GetMax();
            for (uint32_t corner = 0; corner < 8; corner++)
            {
                const Vector3 point(
                    (corner & 1) ? maximum.x : minimum.x,
                    (corner & 2) ? maximum.y : minimum.y,
                    (corner & 4) ? maximum.z : minimum.z
                );
                const Vector3 offset = point - center;
                const float depth = Vector3::Dot(offset, forward);
                distance = max(distance, abs(Vector3::Dot(offset, right)) / tan_half - depth);
                distance = max(distance, abs(Vector3::Dot(offset, up)) / tan_half - depth);
            }
        }
        distance = max(distance, camera->GetNearPlane() + radius * 1.05f);
        const Vector3 position = center + direction * distance;
        camera_entity->SetPosition(position);
        camera_entity->SetRotation(Quaternion::FromLookRotation((center - position).Normalized(), Vector3::Up));
        job.camera_id = camera_entity->GetObjectId();

        Entity* key_entity = World::CreateEntity();
        key_entity->SetObjectName("asset_thumbnail_key_light");
        key_entity->SetTransient(true);
        key_entity->SetParent(rig);
        key_entity->SetRotation(Quaternion::FromLookRotation(Vector3(-0.45f, -0.8f, 0.65f), Vector3::Up));
        Light* key_light = key_entity->AddComponent<Light>();
        key_light->SetLightType(LightType::Directional);
        key_light->SetIntensity(85000.0f);
        key_light->SetColor(Color::standard_white);

        Entity* fill_entity = World::CreateEntity();
        fill_entity->SetObjectName("asset_thumbnail_fill_light");
        fill_entity->SetTransient(true);
        fill_entity->SetParent(rig);
        fill_entity->SetPosition(center + Vector3(radius * 2.0f, radius * 1.2f, -radius * 1.5f));
        Light* fill_light = fill_entity->AddComponent<Light>();
        fill_light->SetLightType(LightType::Point);
        fill_light->SetIntensity(5000.0f);
        fill_light->SetRange(radius * 8.0f);
        fill_light->SetColor(Color::standard_white);

        // the renderer walks the committed entity lists, without this the first frame misses the rig
        World::ProcessPendingAdditions();
        root->SetActive(false);
    }

    bool build_scene()
    {
        Entity* root = World::CreateEntity();
        if (!root)
        {
            return false;
        }
        root->SetObjectName("asset_thumbnail");
        root->SetTransient(true);
        job.root_id = root->GetObjectId();

        if (job.kind == asset_kind::material)
        {
            Render* render = root->AddComponent<Render>();
            render->SetMesh(MeshType::Sphere);
            render->SetMaterial(job.material);
        }
        else if (job.kind == asset_kind::mesh)
        {
            // one render per sub mesh, a single render only ever draws its first one
            Mesh* mesh = job.mesh.get();
            const uint32_t sub_mesh_count = max(1u, mesh->GetSubMeshCount());
            for (uint32_t i = 0; i < sub_mesh_count; i++)
            {
                Entity* part = root;
                if (sub_mesh_count > 1)
                {
                    part = World::CreateEntity();
                    part->SetObjectName("asset_thumbnail_part_" + to_string(i));
                    part->SetTransient(true);
                    part->SetParent(root);
                }
                Render* render = part->AddComponent<Render>();
                render->SetMesh(mesh, i);
                render->SetDefaultMaterial();
            }
        }
        else if (!Prefab::LoadFromFile(job.path, root))
        {
            return false;
        }

        root->SetPosition(Vector3::Zero);
        create_studio(root, job.kind == asset_kind::material);
        return true;
    }

    void destroy_scene()
    {
        if (Entity* root = job_root())
        {
            // immediate removal is refused while anything loads, the deferred one is safe then, the job's
            // resources just stay resident because the pending entities still reference them
            if (ProgressTracker::IsLoading())
            {
                World::RemoveEntity(root);
            }
            else
            {
                World::RemoveEntityImmediate(root);
            }
        }
        job.root_id = 0;
        job.camera_id = 0;
    }

    // hands back to the cache only what this job made resident and nothing else has started using since, a
    // browser pass over a folder of materials must not leave every texture in it loaded for the session
    void release_job_resources()
    {
        unordered_set<const IResource*> owned;
        for (const shared_ptr<IResource>& resource : job.loaded)
        {
            if (!resource || job.resident_before.count(resource.get()))
            {
                continue;
            }
            owned.insert(resource.get());
            if (resource->GetResourceType() == ResourceType::Material)
            {
                for (RHI_Texture* texture : static_cast<Material*>(resource.get())->GetTextures())
                {
                    if (texture && !job.resident_before.count(texture))
                    {
                        owned.insert(texture);
                    }
                }
            }
        }
        job.loaded.clear();
        job.material.reset();
        job.mesh.reset();
        if (owned.empty())
        {
            return;
        }

        // anything the world draws with keeps its resources, it may have picked one up while the job ran
        unordered_set<const IResource*> referenced;
        for (Entity* entity : World::GetEntities())
        {
            if (Render* render = entity ? entity->GetComponent<Render>() : nullptr)
            {
                referenced.insert(render->GetMesh());
                referenced.insert(render->GetMaterial());
            }
        }

        vector<shared_ptr<IResource>> resources = ResourceCache::GetResourcesSnapshot();
        for (const shared_ptr<IResource>& resource : resources)
        {
            if (resource && resource->GetResourceType() == ResourceType::Material && (!owned.count(resource.get()) || referenced.count(resource.get())))
            {
                for (RHI_Texture* texture : static_cast<Material*>(resource.get())->GetTextures())
                {
                    referenced.insert(texture);
                }
            }
        }

        for (shared_ptr<IResource>& resource : resources)
        {
            if (resource && owned.count(resource.get()) && !referenced.count(resource.get()))
            {
                ResourceCache::Remove(resource);
            }
        }
    }

    void finish_job(const bool succeeded)
    {
        destroy_scene();
        release_job_resources();

        auto it = entries.find(job.key);
        if (it != entries.end() && it->second.state == entry_state::rendering)
        {
            thumbnail_entry& entry = it->second;
            if (succeeded && entry.cache_path == job.cache_path)
            {
                start_image_load(job.key, entry);
            }
            else if (succeeded)
            {
                // the asset changed while it rendered, the image just written belongs to the old stamp and is
                // retired when the new one is written
                entry.state = entry_state::queued;
            }
            else
            {
                entry.attempts++;
                entry.state = entry.attempts >= max_attempts ? entry_state::failed : entry_state::queued;
            }
        }

        job.stage = job_stage::idle;
        job.key.clear();
        job.path.clear();
        job.cache_path.clear();
        job.resident_before.clear();
        job.prepared = false;
        last_render_frame = frame;
    }

    void abort_job()
    {
        if (job.stage == job_stage::idle)
        {
            return;
        }
        if (job.preparation.valid())
        {
            job.preparation.wait();
        }
        finish_job(false);
    }

    void on_world_unloading()
    {
        // the world is about to delete the entities the job built and clear the cache it borrowed from
        if (job.stage != job_stage::idle && job.preparation.valid())
        {
            job.preparation.wait();
        }
        job.root_id = 0;
        job.camera_id = 0;
        job.loaded.clear();
        job.material.reset();
        job.mesh.reset();
        if (job.stage != job_stage::idle)
        {
            auto it = entries.find(job.key);
            if (it != entries.end() && it->second.state == entry_state::rendering)
            {
                it->second.state = entry_state::queued;
            }
        }
        job.stage = job_stage::idle;
        job.resident_before.clear();
    }

    bool can_render(Editor* editor)
    {
        if (ProgressTracker::IsLoading() || World::IsLoadingFromFile() || Engine::IsFlagSet(EngineMode::Playing))
        {
            return false;
        }
        // the viewer shows the secondary view output directly, it owns the view while it is open
        if (editor)
        {
            if (AssetViewer* viewer = editor->GetWidget<AssetViewer>())
            {
                if (viewer->GetVisible())
                {
                    return false;
                }
            }
        }
        return !Renderer::IsSecondaryScreenshotPending();
    }

    void start_job(const string& key, thumbnail_entry& entry)
    {
        entry.state = entry_state::rendering;
        job.stage = job_stage::preparing;
        job.key = key;
        job.path = entry.path;
        job.cache_path = entry.cache_path;
        job.kind = entry.kind;
        job.loaded.clear();
        job.prepared = false;
        job.signature = 0;
        job.settle_frames = 0;
        job.stable_frames = 0;
        job.stage_started = clock_type::now();

        job.resident_before.clear();
        for (const shared_ptr<IResource>& resource : ResourceCache::GetResourcesSnapshot())
        {
            job.resident_before.insert(resource.get());
        }

        job_state* state = &job;
        job.preparation = ThreadPool::AddTask([state]()
        {
            prepare(state);
        });
    }

    void tick_job(Editor* editor)
    {
        const auto elapsed = clock_type::now() - job.stage_started;
        switch (job.stage)
        {
            case job_stage::preparing:
            {
                if (job.preparation.valid())
                {
                    if (job.preparation.wait_for(chrono::seconds(0)) != future_status::ready)
                    {
                        return;
                    }
                    job.preparation.get();
                }
                if (!job.prepared)
                {
                    // not something that can be drawn, a non material xml or an empty mesh, no retries
                    auto it = entries.find(job.key);
                    if (it != entries.end())
                    {
                        it->second.attempts = max_attempts;
                    }
                    finish_job(false);
                    return;
                }
                // the viewer opened or a load started while preparing, the loaded assets wait with the job
                if (!can_render(editor))
                {
                    return;
                }
                if (!build_scene())
                {
                    finish_job(false);
                    return;
                }
                job.stage = job_stage::settling;
                job.stage_started = clock_type::now();
                return;
            }
            case job_stage::settling:
            {
                Entity* root = job_root();
                if (!root)
                {
                    finish_job(false);
                    return;
                }

                bool materials_ready = false;
                const uint64_t signature = scene_signature(root, materials_ready);
                job.stable_frames = signature == job.signature ? job.stable_frames + 1 : 0;
                job.signature = signature;
                job.settle_frames++;

                const bool settled =
                    job.settle_frames >= minimum_settle_frames &&
                    job.stable_frames >= stable_frames_required &&
                    materials_ready;
                if (!settled && elapsed < settle_timeout)
                {
                    return;
                }
                if (!can_render(editor) || frame < last_render_frame + frames_between_renders)
                {
                    return;
                }

                Entity* camera = World::GetEntityById(job.camera_id);
                if (
                    !camera ||
                    !Renderer::RequestSecondaryView(camera, root, thumbnail_size, thumbnail_size, Renderer_SecondaryViewMode::Solid, Renderer_SecondaryViewBackdrop::Slate)
                )
                {
                    finish_job(false);
                    return;
                }
                job.generation = Renderer::GetSecondaryViewRequestGeneration();
                if (!Renderer::ScreenshotSecondary(job.cache_path))
                {
                    Renderer::InvalidateSecondaryView();
                    finish_job(false);
                    return;
                }
                last_render_frame = frame;
                job.stage = job_stage::capturing;
                job.stage_started = clock_type::now();
                return;
            }
            case job_stage::capturing:
            {
                // rendered and read back, the image is being written on a worker, the scene is no longer needed
                if (Renderer::GetSecondaryViewGeneration() >= job.generation && !Renderer::IsSecondaryScreenshotPending())
                {
                    destroy_scene();
                    // the output now holds this thumbnail, nothing should mistake it for its own preview
                    Renderer::InvalidateSecondaryView();
                    job.stage = job_stage::writing;
                    job.stage_started = clock_type::now();
                    return;
                }
                // another capture took the view first, try again later
                if (elapsed > capture_timeout)
                {
                    finish_job(false);
                }
                return;
            }
            case job_stage::writing:
            {
                // the save goes through a temporary file and a rename, the name existing means it is complete
                if (FileSystem::Exists(job.cache_path))
                {
                    finish_job(true);
                }
                else if (elapsed > write_timeout)
                {
                    finish_job(false);
                }
                return;
            }
            default:
                return;
        }
    }

    void drain_decoded_images()
    {
        vector<decoded_image> images;
        {
            lock_guard<mutex> lock(decoded_mutex);
            images.swap(decoded);
        }

        for (decoded_image& image : images)
        {
            auto it = entries.find(image.key);
            if (it == entries.end())
            {
                continue;
            }
            thumbnail_entry& entry = it->second;
            // a stamp change while decoding means the entry now wants a different image
            if (entry.cache_path != image.cache_path || entry.state != entry_state::loading)
            {
                continue;
            }
            if (image.texture && image.texture->GetRhiResource())
            {
                entry.texture = image.texture;
                entry.state = entry_state::ready;
            }
            else
            {
                // a corrupt or half written image, render it again
                error_code error;
                filesystem::remove(entry.cache_path, error);
                entry.attempts++;
                entry.state = entry.attempts >= max_attempts ? entry_state::failed : entry_state::queued;
            }
        }

        image_loads.erase(
            remove_if(image_loads.begin(), image_loads.end(), [](const future<void>& load)
            {
                return load.wait_for(chrono::seconds(0)) == future_status::ready;
            }),
            image_loads.end()
        );
    }

    void evict_textures()
    {
        vector<thumbnail_entry*> resident;
        for (auto& [key, entry] : entries)
        {
            if (entry.texture)
            {
                resident.push_back(&entry);
            }
        }
        if (resident.size() <= max_resident_textures)
        {
            return;
        }

        sort(resident.begin(), resident.end(), [](const thumbnail_entry* a, const thumbnail_entry* b)
        {
            return a->requested_frame < b->requested_frame;
        });
        size_t excess = resident.size() - max_resident_textures;
        for (thumbnail_entry* entry : resident)
        {
            // one drawn recently can still be referenced by a frame in flight
            if (excess == 0 || entry->requested_frame + 120 > frame)
            {
                break;
            }
            entry->texture.reset();
            if (entry->state == entry_state::ready)
            {
                entry->state = entry_state::unchecked;
            }
            excess--;
        }
    }
}

void AssetThumbnails::Tick(Editor* editor)
{
    frame++;
    if (!subscribed)
    {
        subscribed = true;
        world_unloading_handle = SP_SUBSCRIBE_TO_EVENT(EventType::WorldUnloading, SP_EVENT_HANDLER_STATIC(on_world_unloading));
    }

    drain_decoded_images();

    // decodes are cheap and parallel, the most recently seen go first
    uint32_t loads_in_flight = static_cast<uint32_t>(image_loads.size());
    if (loads_in_flight < max_image_loads)
    {
        vector<pair<uint64_t, string>> candidates;
        for (auto& [key, entry] : entries)
        {
            if (entry.state == entry_state::cached && entry.requested_frame + visible_frames >= frame)
            {
                candidates.emplace_back(entry.requested_frame, key);
            }
        }
        sort(candidates.begin(), candidates.end(), greater<>());
        for (const auto& [requested, key] : candidates)
        {
            if (loads_in_flight >= max_image_loads)
            {
                break;
            }
            start_image_load(key, entries[key]);
            loads_in_flight++;
        }
    }

    if (job.stage != job_stage::idle)
    {
        tick_job(editor);
    }
    else if (can_render(editor) && frame >= last_render_frame + frames_between_renders)
    {
        // renders go to what is on screen right now, anything scrolled away waits until it is back
        thumbnail_entry* next = nullptr;
        const string* next_key = nullptr;
        for (auto& [key, entry] : entries)
        {
            if (entry.state != entry_state::queued || entry.requested_frame + visible_frames < frame)
            {
                continue;
            }
            if (!next || entry.requested_frame > next->requested_frame || (entry.requested_frame == next->requested_frame && key < *next_key))
            {
                next = &entry;
                next_key = &key;
            }
        }
        if (next)
        {
            start_job(*next_key, *next);
        }
    }

    evict_textures();
}

void AssetThumbnails::Shutdown()
{
    abort_job();
    for (future<void>& load : image_loads)
    {
        if (load.valid())
        {
            load.wait();
        }
    }
    image_loads.clear();
    {
        lock_guard<mutex> lock(decoded_mutex);
        decoded.clear();
    }
    if (subscribed)
    {
        SP_UNSUBSCRIBE_FROM_EVENT(EventType::WorldUnloading, world_unloading_handle);
        subscribed = false;
    }

    RHI_Device::QueueWaitAll();
    for (auto& [key, entry] : entries)
    {
        if (entry.texture)
        {
            entry.texture->ReleaseGpuResources();
        }
    }
    entries.clear();
}

bool AssetThumbnails::IsSupported(const string& path)
{
    return kind_from_path(path) != asset_kind::none;
}

RHI_Texture* AssetThumbnails::Get(const string& path)
{
    const asset_kind kind = kind_from_path(path);
    if (kind == asset_kind::none)
    {
        return nullptr;
    }

    const string key = normalize(path);
    auto [it, inserted] = entries.try_emplace(key);
    thumbnail_entry& entry = it->second;
    if (inserted)
    {
        entry.path = loadable_path(path);
        entry.kind = kind;
    }
    entry.requested_frame = frame;

    if (entry.state == entry_state::unchecked || clock_type::now() - entry.checked_at > stamp_check_interval)
    {
        refresh_entry(key, entry);
    }

    RHI_Texture* texture = entry.texture.get();
    if (texture && texture->GetResourceState() == ResourceState::PreparedForGpu && texture->GetRhiResource())
    {
        return texture;
    }
    return nullptr;
}

void AssetThumbnails::Regenerate(const string& path)
{
    const string key = normalize(path);
    auto it = entries.find(key);
    if (it == entries.end())
    {
        return;
    }

    thumbnail_entry& entry = it->second;
    if (entry.state == entry_state::rendering || entry.state == entry_state::loading)
    {
        return;
    }
    refresh_entry(key, entry);
    if (entry.cache_path.empty())
    {
        return;
    }
    error_code error;
    filesystem::remove(entry.cache_path, error);
    entry.attempts = 0;
    entry.state = entry_state::queued;
}
