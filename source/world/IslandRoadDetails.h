// Copyright(c) 2015-2026 Panos Karabelas
// Licensed under the Spartan Engine License. See license.md in the repository root.
// https://github.com/PanosK92/SpartanEngine/blob/master/license.md
// Commercial use requires written permission and negotiated payment terms.
#pragma once
#include "World.h"
#include "Entity.h"
#include "components/Spline.h"
#include "components/Terrain.h"
#include "components/Render.h"
#include "../rendering/Material.h"
#include "../geometry/Mesh.h"
#include "../geometry/GeometryProcessing.h"
#include "../geometry/GeneratedCache.h"
#include "../core/Stopwatch.h"
#include "../core/ProgressTracker.h"
#include "../core/ThreadPool.h"
#include "../core/Engine.h"
#include "../rhi/RHI_Vertex.h"
#include <unordered_map>
#include <array>
#include <cstring>
#include "RoadGuardrail.h"

namespace spartan::island_road_details
{
    using namespace math;
    enum Finish { White, Red, Metal, Dark, Yellow, Galvanized, ReflectiveAmber, FinishCount };
    struct Road { uint64_t id; uint64_t detail_id = 0; uint64_t signature = 0; };
    inline std::vector<Road> roads;
    inline std::unordered_map<uint64_t, uint64_t> preparation_keys;
    inline std::unordered_map<std::string,uint32_t> junction_degree;
    inline std::array<std::shared_ptr<Material>, FinishCount> materials;
    inline std::shared_ptr<Mesh> triangle;
    inline uint64_t root_id = 0;
    inline size_t cursor = 0;
    inline float discover_time = 0;
    inline double cache_ms = 0, build_ms = 0, save_ms = 0;
    inline uint32_t cache_hits = 0, cache_misses = 0;

    inline void ClearBakes();
    inline void Clear()
    {
        ClearBakes();
        roads.clear();
        preparation_keys.clear();
        junction_degree.clear();
        materials.fill(nullptr);
        triangle.reset();
        road_guardrail::Clear();
        root_id = 0;
        cursor = 0;
        discover_time = 0;
        cache_ms = build_ms = save_ms = 0;
        cache_hits = cache_misses = 0;
    }

    inline Entity* Part(Entity* parent, const char* name, Vector3 position, Vector3 scale, Finish finish, bool triangular = false)
    {
        if (!materials[finish])
        {
            static const Color colors[] = {Color(.88f,.89f,.84f,1),Color(.65f,.015f,.012f,1),
                Color(.35f,.38f,.4f,1),Color(.025f,.03f,.035f,1),Color(1,.65f,.015f,1),Color(1,1,1,1),Color(1,1,1,1)};
            materials[finish] = std::make_shared<Material>();
            materials[finish]->SetObjectName("roadside_finish_"+std::to_string(finish));
            materials[finish]->SetColor(colors[finish]);
            materials[finish]->SetProperty(MaterialProperty::Roughness,finish == Metal ? .42f : .7f);
            materials[finish]->SetProperty(MaterialProperty::Metalness,finish == Metal ? .7f : 0);
            const std::string assets=World::GetResourceDirectory()+"guardrail/";
            if (finish==Galvanized)
            {
                materials[finish]->SetTexture(MaterialTextureType::Color,assets+"zinc_color.png");
                materials[finish]->SetTexture(MaterialTextureType::Roughness,assets+"zinc_roughness.png");
                materials[finish]->SetTexture(MaterialTextureType::Metalness,assets+"zinc_metalness.png");
                materials[finish]->SetTexture(MaterialTextureType::Normal,assets+"zinc_normal.png");
            }
            else if (finish==ReflectiveAmber)
            {
                materials[finish]->SetTexture(MaterialTextureType::Color,assets+"reflector_color.png");
                materials[finish]->SetProperty(MaterialProperty::Roughness,.22f);
            }
        }
        Entity* part = World::CreateEntity();
        part->SetObjectName(name);
        part->SetTransient(true);
        part->SetParent(parent);
        part->SetPositionLocal(position);
        part->SetScaleLocal(scale);
        Render* render = part->AddComponent<Render>();
        if (triangular) render->SetMesh(triangle.get()); else render->SetMesh(MeshType::Cube);
        render->SetMaterial(materials[finish]);
        render->SetMaxRenderDistance(finish==Galvanized || finish==ReflectiveAmber ? 650.0f : 250.0f);
        render->SetMaxShadowDistance(finish==Galvanized ? 120.0f : 45.0f);
        if (finish!=Galvanized && finish!=ReflectiveAmber) render->SetFlag(RenderFlags::ExcludeFromRayTracing);
        render->SetFlag(RenderFlags::ExcludeFromTerrainBlend);
        return part;
    }

    // Per-instance bounds already provide spatial culling. Do not create three
    // separate render entities for every 64 metres of otherwise identical hardware.
    inline void PublishGuardrailInstances(Entity* group, std::array<std::vector<Matrix>, road_guardrail::ModuleCount>& instances)
    {
        for (const auto module : {road_guardrail::Beam, road_guardrail::Post, road_guardrail::Splice, road_guardrail::Reflector})
        {
            auto& transforms = instances[module];
            if (transforms.empty()) continue;
            const Vector3 origin = transforms.front().GetTranslation();
            const Matrix local(-origin, Quaternion::Identity, Vector3::One);
            for (auto& transform : transforms) transform = transform * local;
            Entity* batch = Part(group, road_guardrail::InstanceName(module), origin, Vector3::One,
                module == road_guardrail::Reflector ? ReflectiveAmber : Galvanized);
            Render* render = batch->GetComponent<Render>();
            render->SetOwnedMesh(road_guardrail::SharedMesh(module));
            render->SetInstances(transforms);
            if (module == road_guardrail::Beam) road_guardrail::AddCollision(batch);
        }
    }

    inline void MakeTriangle()
    {
        if (triangle) return;
        triangle = std::make_shared<Mesh>();
        triangle->SetObjectName("roadside_yield_triangle");
        std::vector<RHI_Vertex_PosTexNorTan> vertices(6);
        const Vector3 corners[] = {Vector3(-.5f,.3f,0),Vector3(.5f,.3f,0),Vector3(0,-.566f,0)};
        for (int i = 0; i < 6; ++i)
        {
            const Vector3 p = corners[i%3];
            vertices[i] = RHI_Vertex_PosTexNorTan(p,Vector2(p.x+.5f,p.y+.566f),
                Vector3(0,0,i<3 ? -1.0f : 1.0f),Vector3::Right);
        }
        std::vector<uint32_t> indices = {0,1,2,5,4,3};
        triangle->AddGeometry(vertices,indices,false);
        triangle->CreateGpuBuffers();
    }

    inline Entity* Anchor(Entity* parent, const char* name, const Vector3& position, const Vector3& facing)
    {
        Entity* anchor = World::CreateEntity();
        anchor->SetObjectName(name);
        anchor->SetTransient(true);
        anchor->SetParent(parent);
        anchor->SetPosition(position);
        anchor->SetRotation(Quaternion::FromLookRotation(facing));
        return anchor;
    }

    inline void Build(Entity* road, Spline* spline, Entity* group, const ProgressTask& progress)
    {
        const auto& frames = spline->GetRoadFrames();
        if (frames.size() < 3) return;
        const Matrix matrix = road->GetMatrix();
        const float length = frames.back().distance;
        auto tangent_at = [&](size_t i)
        {
            Vector3 tangent = matrix*(frames[i].position+frames[i].tangent)-matrix*frames[i].position;
            tangent.y=0;
            return tangent.LengthSquared() > .001f ? tangent.Normalized() : Vector3::Forward;
        };
        auto roadside = [&](size_t i, const Vector3& direction, float side)
        {
            const float half = (spline->GetRoadWidth()+(spline->GetRoadWidthEnd()-spline->GetRoadWidth())*frames[i].t)*.5f;
            return matrix*frames[i].position+Vector3::Up.Cross(direction)*(half+spline->GetSidewalkWidthAt(frames[i].t)+1.0f)*side;
        };
        std::vector<size_t> junctions, yield_junctions;
        size_t control = 0;
        const size_t count = spline->GetControlPointCount();
        for (Entity* point : road->GetChildren())
        {
            if (point->GetObjectName().find("spline_point_") != 0) continue;
            const float t = static_cast<float>(control++)/std::max(size_t(1),count-1);
            if (std::none_of(point->GetTags().begin(),point->GetTags().end(),[](const std::string& tag){return tag.find("road_node_")==0;})) continue;
            const auto it = std::lower_bound(frames.begin(),frames.end(),t,[](const SplineFrame& f,float t){return f.t<t;});
            const size_t frame=std::min(static_cast<size_t>(it-frames.begin()),frames.size()-1);
            junctions.push_back(frame);
            for (const std::string& tag:point->GetTags())
                if (const auto found=junction_degree.find(tag);found!=junction_degree.end() && found->second>=3)
                    yield_junctions.push_back(frame);
        }
        auto near_junction = [&](size_t i, float clearance)
        {
            for (size_t j : junctions) if (fabsf(frames[i].distance-frames[j].distance)<clearance) return true;
            return false;
        };
        // End approaches of smaller roads yield. Major through routes keep priority.
        if (spline->GetRoadWidth() <= 8.0f && length > 65)
        for (bool end : {false,true})
        {
            const size_t node = end ? frames.size()-1 : 0;
            if (std::none_of(yield_junctions.begin(),yield_junctions.end(),[&](size_t j)
                {return fabsf(frames[j].distance-frames[node].distance)<2;})) continue;
            size_t i = node;
            while (fabsf(frames[i].distance-frames[node].distance)<24 && (end ? i>0 : i+1<frames.size()))
                i = end ? i-1 : i+1;
            const Vector3 incoming = tangent_at(i)*(end ? 1.0f : -1.0f);
            Entity* sign = Anchor(group,"roadside_yield",roadside(i,incoming,1),-incoming);
            Part(sign,"galvanized_post",Vector3(0,1.1f,0),Vector3(.065f,2.2f,.065f),Metal);
            Part(sign,"yield_border",Vector3(0,2.15f,0),Vector3(1.05f,1.05f,1),Red,true);
            Part(sign,"yield_face",Vector3(0,2.15f,.008f),Vector3(.79f,.79f,1),White,true);
            Part(sign,"yield_back",Vector3(0,2.15f,-.008f),Vector3(.99f,.99f,1),Metal,true);
        }
        float next_marker = 60;
        float next_bend = 0;
        for (size_t i = 2; i+2 < frames.size(); ++i)
        {
            if (near_junction(i,30)) continue;
            const Vector3 direction = tangent_at(i);
            if (frames[i].distance >= next_marker)
            {
                for (float side : {-1.0f,1.0f})
                {
                    Entity* marker = Anchor(group,"roadside_delineator",roadside(i,direction,side),direction*side);
                    Part(marker,"post",Vector3(0,.5f,0),Vector3(.12f,1,.09f),White);
                    Part(marker,"reflector_band",Vector3(0,.8f,.05f),Vector3(.105f,.22f,.018f),Dark);
                    Part(marker,"reflector",Vector3(0,.8f,.063f),Vector3(.05f,.1f,.01f),side>0 ? Red : White);
                }
                next_marker = frames[i].distance+std::max(160.0f,length/24);
            }
            size_t before=i,after=i;
            while (before>0 && frames[i].distance-frames[before].distance<18) --before;
            while (after+1<frames.size() && frames[after].distance-frames[i].distance<18) ++after;
            const Vector3 a=tangent_at(before),b=tangent_at(after);
            if (a.Dot(b)>.82f || frames[i].distance<next_bend) continue;
            next_bend=frames[i].distance+200;
            const float turn = a.Cross(b).y > 0 ? 1.0f : -1.0f;
            Entity* sign=Anchor(group,"roadside_bend_chevron",roadside(i,direction,-turn),-a);
            Part(sign,"post",Vector3(0,.9f,0),Vector3(.065f,1.8f,.065f),Metal);
            Part(sign,"board",Vector3(0,1.7f,0),Vector3(1.0f,.65f,.055f),Dark);
            for (float half : {-1.0f,1.0f})
                Part(sign,"chevron",Vector3(0,1.7f+half*.13f,.04f),Vector3(.42f,.1f,.02f),Yellow)
                    ->SetRotationLocal(Quaternion::FromEulerAngles(0,0,-turn*half*40));
        }
        // Repeated furniture shares merged meshes. Keep signs individually
        // selectable, without retaining thousands of post/rail entities.
        std::array<std::vector<Matrix>,FinishCount> batches;
        const std::vector<Entity*> children=group->GetChildren();
        for (Entity* anchor:children)
        {
            const std::string& name=anchor->GetObjectName();
            if (name!="roadside_delineator") continue;
            for (Entity* part:anchor->GetChildren())
            {
                Render* render=part->GetComponent<Render>();
                if (!render) continue;
                for (size_t finish=0;finish<FinishCount;++finish)
                    if (render->GetMaterial()==materials[finish].get()) batches[finish].push_back(part->GetMatrix());
            }
            World::RemoveEntity(anchor);
        }
        for (size_t finish=0;finish<FinishCount;++finish)
        {
            if (batches[finish].empty()) continue;
            const Vector3 origin=batches[finish].front().GetTranslation();
            Entity* batch=Part(group,"roadside_furniture",origin,Vector3::One,static_cast<Finish>(finish));
            Render* render=batch->GetComponent<Render>();
            std::vector<RHI_Vertex_PosTexNorTan> cube,vertices;
            std::vector<uint32_t> cube_indices,indices;
            render->GetGeometry(&cube_indices,&cube);
            for (const Matrix& transform:batches[finish])
            {
                const uint32_t offset=static_cast<uint32_t>(vertices.size());
                const Quaternion rotation=transform.GetRotation();
                for (const auto& vertex:cube)
                    vertices.emplace_back(transform*vertex.get_position()-origin,vertex.get_uv(),
                        rotation*vertex.get_normal(),rotation*vertex.get_tangent());
                for (uint32_t index:cube_indices) indices.push_back(offset+index);
            }
            auto mesh=std::make_shared<Mesh>();
            mesh->SetObjectName("roadside_furniture_"+std::to_string(batch->GetObjectId()));
            mesh->AddGeometry(vertices,indices,false);
            mesh->CreateGpuBuffers();
            render->SetOwnedMesh(mesh);
        }
        struct GuardrailBatch
        {
            Vector3 origin;
            size_t finish;
            road_guardrail::GeometryLods lods;
            std::shared_ptr<Mesh> mesh;
        };
        std::vector<GuardrailBatch> guardrails;
        std::array<std::vector<Matrix>, road_guardrail::ModuleCount> hardware;
        progress.SetStep("Placing guardrails");
        road_guardrail::Build(road,spline,junctions,[&](const Vector3& origin,size_t finish,road_guardrail::GeometryLods& lods)
        {
            guardrails.push_back({origin, finish, std::move(lods), {}});
        },[&](const Vector3& origin,road_guardrail::Module module,const std::vector<Matrix>& instances)
        {
            const Matrix transform(origin, Quaternion::Identity, Vector3::One);
            for (const Matrix& instance : instances) hardware[module].push_back(instance * transform);
        });
        PublishGuardrailInstances(group, hardware);
        if (!guardrails.empty())
        {
            progress.SetStep("Building guardrail meshes and LODs");
            std::atomic<uint32_t> next{0};
            ThreadPool::ParallelLoop([&](uint32_t, uint32_t)
            {
                for (uint32_t i; (i = next.fetch_add(1, std::memory_order_relaxed)) < guardrails.size();)
                {
                    auto& batch = guardrails[i];
                    batch.mesh = std::make_shared<Mesh>();
                    batch.mesh->SetFlag(static_cast<uint32_t>(MeshFlags::PostProcessPreserveLod0) |
                        static_cast<uint32_t>(MeshFlags::PostProcessSkipCache));
                    auto& base = batch.lods[0];
                    batch.mesh->AddGeometry(base.vertices, base.indices, false);
                    size_t previous_count = base.indices.size();
                    for (uint32_t lod = 1; lod < mesh_lod_count; ++lod)
                    {
                        auto& level = batch.lods[lod];
                        if (level.indices.size() >= previous_count * mesh_lod_min_reduction) break;
                        geometry_processing::weld_and_optimize(level.vertices, level.indices);
                        batch.mesh->AddLod(level.vertices, level.indices, 0);
                        previous_count = level.indices.size();
                    }
                    for (auto& level : batch.lods)
                    {
                        std::vector<RHI_Vertex_PosTexNorTan>().swap(level.vertices);
                        std::vector<uint32_t>().swap(level.indices);
                    }
                }
            }, static_cast<uint32_t>(guardrails.size()));
            progress.SetStep("Publishing guardrail geometry");
            for (auto& batch : guardrails)
            {
                Entity* entity = Part(group, batch.finish == 0 ? "roadside_guardrail_steel" : "roadside_guardrail_reflectors",
                    batch.origin, Vector3::One, batch.finish == 0 ? Galvanized : ReflectiveAmber);
                batch.mesh->SetObjectName(entity->GetObjectName() + "_" + std::to_string(entity->GetObjectId()));
                batch.mesh->CreateGpuBuffers();
                entity->GetComponent<Render>()->SetOwnedMesh(batch.mesh);
                if (batch.finish == 0) road_guardrail::AddCollision(entity);
            }
        }
    }

    struct BakedPart
    {
        uint32_t parent = UINT32_MAX, finish = FinishCount, shape = 0;
        Vector3 position, scale;
        Quaternion rotation;
        char name[64] = {};
        uint32_t block = UINT32_MAX, offset = 0, size = 0;
    };

    struct BakedRoad
    {
        std::vector<BakedPart> parts;
        std::vector<std::shared_ptr<Mesh>> meshes;
        std::vector<std::vector<Matrix>> instances;
        std::future<void> ready;
        bool valid = false;
    };
    inline std::unordered_map<uint64_t, std::shared_ptr<BakedRoad>> pending_bakes;
    inline void ClearBakes()
    {
        for (auto& [key, bake] : pending_bakes) if (bake->ready.valid()) bake->ready.wait();
        pending_bakes.clear();
    }

    // restores arrive in bursts from one road (collision streaming), keeping the last blocks
    // avoids rereading a whole 32 mb block for every mesh inside it
    inline bool ReadBlockSlice(const std::string& resources, uint64_t key, uint32_t block, uint32_t offset, uint32_t size, std::vector<uint8_t>& out)
    {
        struct CachedBlock
        {
            uint64_t id = 0;
            std::shared_ptr<const std::vector<uint8_t>> bytes;
        };
        static std::mutex cache_mutex;
        static std::array<CachedBlock, 2> cache;
        static uint32_t cache_next = 0;

        generated_cache::Hash block_key; block_key.Add(key); block_key.Add(block);
        std::shared_ptr<const std::vector<uint8_t>> bytes;
        {
            std::lock_guard lock(cache_mutex);
            for (const CachedBlock& entry : cache)
            {
                if (entry.bytes && entry.id == block_key.value)
                {
                    bytes = entry.bytes;
                }
            }
        }
        if (!bytes)
        {
            auto loaded = std::make_shared<std::vector<uint8_t>>();
            if (!generated_cache::ReadPayload(generated_cache::Path(resources, "road_furniture_blocks", block_key.value), block_key.value, *loaded)) return false;
            bytes = loaded;
            std::lock_guard lock(cache_mutex);
            cache[cache_next++ % cache.size()] = {block_key.value, bytes};
        }
        if (bytes->size() < sizeof(uint64_t) + uint64_t(offset) + size) return false;
        const uint8_t* begin = bytes->data() + sizeof(uint64_t) + offset;
        out.assign(begin, begin + size);
        return true;
    }

    inline bool ReadDetails(BakedRoad& data, const std::string& resources, uint64_t key)
    {
        auto& parts = data.parts;
        if (!generated_cache::Load(generated_cache::Path(resources, "road_furniture", key), key, parts)) return false;
        if (parts.empty()) return true;
        auto& meshes = data.meshes;
        auto& instances = data.instances;
        meshes.resize(parts.size());
        instances.resize(parts.size());
        uint32_t block_count = 0;
        for (size_t i = 0; i < parts.size(); ++i)
        {
            const auto& p = parts[i];
            if ((p.parent != UINT32_MAX && p.parent >= i) || p.finish > FinishCount || p.shape >= 3+road_guardrail::ModuleCount ||
                !p.position.IsFinite() || !p.scale.IsFinite() || p.name[63] != 0) return false;
            if (p.shape >= 2)
            {
                if (p.block >= parts.size() || p.size == 0) return false;
                block_count = std::max(block_count, p.block + 1);
            }
            if (p.shape>=3)
            {
                const auto module=static_cast<road_guardrail::Module>(p.shape-3);
                if (road_guardrail::InstanceModule(p.name)!=module) return false;
            }
        }
        std::atomic<bool> valid{true};
        std::vector<std::vector<uint8_t>> blocks(block_count);
        if (block_count)
        {
            ThreadPool::ParallelLoop([&](uint32_t begin, uint32_t end)
            {
                for (uint32_t i = begin; i < end; ++i)
                {
                    generated_cache::Hash block_key; block_key.Add(key); block_key.Add(i);
                    const auto path = generated_cache::Path(resources, "road_furniture_blocks", block_key.value);
                    generated_cache::ReadMeasurement measurement{"road_furniture_blocks"};
                    // Keep the decoded byte-vector's eight-byte header in place.
                    // Copying it into another vector duplicated every road block
                    // before the prepared meshes even read their own slices.
                    uint64_t byte_count = 0;
                    bool loaded = generated_cache::ReadPayload(path, block_key.value, blocks[i]) && blocks[i].size() >= sizeof(byte_count);
                    if (loaded)
                    {
                        memcpy(&byte_count, blocks[i].data(), sizeof(byte_count));
                        loaded = byte_count == blocks[i].size() - sizeof(byte_count);
                    }
                    measurement.hit = loaded;
                    if (!loaded)
                    {
                        std::error_code error;
                        measurement.missing = !std::filesystem::exists(path, error);
                        valid.store(false, std::memory_order_relaxed);
                    }
                }
            }, block_count);
            if (!valid.load(std::memory_order_relaxed)) return false;
        }
        ThreadPool::ParallelLoop([&](uint32_t begin, uint32_t end)
        {
            for (size_t i = begin; i < end && valid.load(std::memory_order_relaxed); ++i)
            {
                const auto& part = parts[i];
                if (part.shape < 2) continue;
                const auto& encoded_block = blocks[part.block];
                const std::span<const uint8_t> block(encoded_block.data() + sizeof(uint64_t), encoded_block.size() - sizeof(uint64_t));
                if (uint64_t(part.offset) + part.size > block.size())
                {
                    valid.store(false, std::memory_order_relaxed);
                    continue;
                }
                const std::span<const uint8_t> bytes(block.data() + part.offset, part.size);
                bool loaded = true;
                if (parts[i].shape >= 3)
                {
                    loaded = generated_cache::Decode(bytes,instances[i]) && !instances[i].empty();
                    for (const Matrix& matrix : instances[i])
                        for (size_t element = 0; element < 16; ++element) loaded &= std::isfinite(matrix.Data()[element]);
                }
                else if (parts[i].shape == 2)
                {
                    meshes[i] = std::make_shared<Mesh>();
                    loaded = meshes[i]->LoadPrepared(bytes);
                    meshes[i]->SetCpuGeometrySource([resources, key, block = part.block, offset = part.offset, size = part.size](std::vector<uint8_t>& prepared)
                    {
                        return ReadBlockSlice(resources, key, block, offset, size, prepared);
                    });
                }
                if (!loaded) valid.store(false, std::memory_order_relaxed);
            }
        }, static_cast<uint32_t>(parts.size()));
        if (!valid.load(std::memory_order_relaxed)) return false;
        return true;
    }

    inline void PrefetchBake(uint64_t key)
    {
        if (pending_bakes.contains(key)) return;
        auto data = std::make_shared<BakedRoad>();
        const std::string resources = World::GetResourceDirectory();
        data->ready = ThreadPool::AddTask([data, resources, key]() { data->valid = ReadDetails(*data, resources, key); });
        pending_bakes.emplace(key, std::move(data));
    }

    inline bool LoadDetails(Entity* group, uint64_t key, const ProgressTask& progress)
    {
        progress.SetStep("Reading cached guardrail geometry");
        std::shared_ptr<BakedRoad> data;
        if (auto found = pending_bakes.find(key); found != pending_bakes.end())
        {
            data = std::move(found->second);
            pending_bakes.erase(found);
            data->ready.get();
        }
        else
        {
            data = std::make_shared<BakedRoad>();
            data->valid = ReadDetails(*data, World::GetResourceDirectory(), key);
        }
        if (!data->valid) return false;
        auto& parts = data->parts;
        auto& meshes = data->meshes;
        auto& instances = data->instances;
        for (size_t i = 0; i < parts.size(); ++i)
            if (parts[i].shape >= 3) meshes[i] = road_guardrail::SharedMesh(static_cast<road_guardrail::Module>(parts[i].shape - 3));
        progress.SetStep("Restoring roadside entities");
        std::vector<Entity*> entities;
        entities.reserve(parts.size());
        std::array<std::vector<Matrix>, road_guardrail::ModuleCount> hardware;
        std::vector<bool> has_children(parts.size(), false);
        for (const auto& p : parts) if (p.parent != UINT32_MAX) has_children[p.parent] = true;
        for (size_t i = 0; i < parts.size(); ++i)
        {
            const auto& p = parts[i];
            // Existing bakes remain usable: consolidate their leaf hardware on load.
            if (p.shape >= 3 && p.parent == UINT32_MAX && !has_children[i])
            {
                const Matrix transform(p.position, p.rotation, p.scale);
                for (const Matrix& instance : instances[i]) hardware[p.shape - 3].push_back(instance * transform);
                entities.push_back(nullptr);
                continue;
            }
            Entity* parent = p.parent == UINT32_MAX ? group : entities[p.parent];
            Entity* entity;
            if (p.finish == FinishCount)
            {
                entity = World::CreateEntity(); entity->SetObjectName(p.name);
                entity->SetTransient(true); entity->SetParent(parent);
            }
            else
            {
                entity = Part(parent, p.name, p.position, p.scale, static_cast<Finish>(p.finish), p.shape == 1);
                if (meshes[i])
                {
                    if (p.shape==2)
                    {
                        meshes[i]->SetObjectName("roadside_furniture");
                        meshes[i]->CreateGpuBuffers();
                    }
                    entity->GetComponent<Render>()->SetOwnedMesh(meshes[i]);
                    if (!instances[i].empty()) entity->GetComponent<Render>()->SetInstances(instances[i]);
                }
            }
            entity->SetPositionLocal(p.position); entity->SetScaleLocal(p.scale); entity->SetRotationLocal(p.rotation);
            if (entity->GetObjectName()=="roadside_guardrail_steel") road_guardrail::AddCollision(entity);
            entities.push_back(entity);
        }
        PublishGuardrailInstances(group, hardware);
        // the gpu has them now, collision cooking restores the few it needs from the bake
        for (size_t i = 0; i < parts.size(); ++i)
        {
            if (parts[i].shape == 2 && meshes[i])
            {
                meshes[i]->ReleaseCpuGeometry();
            }
        }
        return true;
    }

    inline void SaveDetails(Entity* group, uint64_t key)
    {
        std::vector<BakedPart> parts;
        std::vector<std::vector<uint8_t>> blocks;
        bool valid = true;
        auto pack = [&](BakedPart& part, const std::vector<uint8_t>& bytes)
        {
            if (bytes.empty() || bytes.size() > generated_cache::maximum_payload_size - sizeof(uint64_t))
            {
                valid = false;
                return;
            }
            constexpr size_t block_size = 32 * 1024 * 1024;
            if (blocks.empty() || blocks.back().size() + bytes.size() > block_size) blocks.emplace_back();
            part.block = static_cast<uint32_t>(blocks.size() - 1);
            part.offset = static_cast<uint32_t>(blocks.back().size());
            part.size = static_cast<uint32_t>(bytes.size());
            blocks.back().insert(blocks.back().end(), bytes.begin(), bytes.end());
        };
        const std::string resources = World::GetResourceDirectory();
        std::function<void(Entity*, uint32_t)> visit = [&](Entity* entity, uint32_t parent)
        {
            // These temporary anchors were already merged and are pending removal.
            if (entity->GetObjectName() == "roadside_delineator") return;
            BakedPart p;
            p.parent = parent; p.position = entity->GetPositionLocal(); p.scale = entity->GetScaleLocal();
            p.rotation = entity->GetRotationLocal();
            const std::string& name = entity->GetObjectName();
            std::memcpy(p.name, name.data(), std::min(name.size(), sizeof(p.name) - 1));
            const uint32_t index = static_cast<uint32_t>(parts.size());
            if (Render* render = entity->GetComponent<Render>())
            {
                for (uint32_t f = 0; f < FinishCount; ++f) if (render->GetMaterial() == materials[f].get()) p.finish = f;
                p.shape = render->GetMesh() == triangle.get() ? 1 : name == "roadside_furniture" || road_guardrail::IsBatch(name) ? 2 : 0;
                if (const auto module=road_guardrail::InstanceModule(name);module!=road_guardrail::ModuleCount)
                {
                    p.shape=3+module;
                    std::vector<Matrix> instances;
                    for (uint32_t i=0;i<render->GetInstanceCount();++i) instances.push_back(render->GetInstance(i,false));
                    pack(p, generated_cache::Encode(instances));
                }
                if (p.shape == 2)
                {
                    pack(p, render->GetMesh()->SerializePrepared());
                }
            }
            parts.push_back(p);
            for (Entity* child : entity->GetChildren()) visit(child, index);
        };
        for (Entity* child : group->GetChildren()) visit(child, UINT32_MAX);
        if (!valid) return;
        if (!blocks.empty())
        {
            // A few bounded archives replace thousands of tiny files per road.
            // Publish the manifest only after every block has been written.
            ThreadPool::ParallelLoop([&](uint32_t begin, uint32_t end)
            {
                for (uint32_t i = begin; i < end; ++i)
                {
                    generated_cache::Hash block_key; block_key.Add(key); block_key.Add(i);
                    generated_cache::Save(generated_cache::Path(resources,"road_furniture_blocks",block_key.value),block_key.value,blocks[i]);
                }
            }, static_cast<uint32_t>(blocks.size()));
        }
        generated_cache::Save(generated_cache::Path(resources, "road_furniture", key), key, parts);
    }

    inline uint64_t CacheKey(Entity* entity, Spline* spline, bool hazard_signature = false)
    {
        // Terrain grading and junction solving are finished before this stage.
        // Prefetch and publication can share one fingerprint per road; live
        // editing still evaluates the full recipe every time.
        if (World::IsPreparing() && !hazard_signature)
            if (auto found = preparation_keys.find(entity->GetObjectId()); found != preparation_keys.end()) return found->second;
        generated_cache::Hash recipe;
        recipe.Add(uint32_t(7)); recipe.Add(sizeof(BakedPart)); recipe.Add(sizeof(MeshLod));
        recipe.Add(road_guardrail::asset_hash);
        recipe.Add(spline->GetRoadFrames()); recipe.Add(spline->GetControlPointCount());
        for (const auto& f : spline->GetRoadFrames()) recipe.Add(spline->GetSidewalkWidthAt(f.t));
        uint64_t& hash = recipe.value;
        auto add=[&](float value){uint32_t bits;std::memcpy(&bits,&value,sizeof(bits));hash=(hash^bits)*1099511628211ull;};
        add(spline->GetRoadWidth()); add(spline->GetRoadWidthEnd());
        add(spline->GetSidewalkWidth()); add(spline->GetSidewalkEnabled() ? 1.0f : 0.0f);
        for (const Vector3 v : {entity->GetPosition(),entity->GetRotation().ToEulerAngles(),entity->GetScale()})
            {add(v.x);add(v.y);add(v.z);}
        for (const auto& f:spline->GetRoadFrames()) {add(f.position.x);add(f.position.y);add(f.position.z);}
        // Terrain-only edits can change whether an otherwise identical road needs
        // protection. Include the exposed roadside heights in its cache identity.
        if (Terrain* terrain=Terrain::FindActive())
        {
            const Matrix matrix=entity->GetMatrix();
            for (const auto& f:spline->GetRoadFrames())
            {
                const Vector3 center=matrix*f.position;
                const Vector3 right=matrix*(f.position+f.right)-center;
                const float half=(spline->GetRoadWidth()+(spline->GetRoadWidthEnd()-spline->GetRoadWidth())*f.t)*.5f;
                for (float side:{-1.0f,1.0f}) for (float offset:{3.0f,7.0f})
                {
                    const Vector3 probe=center+right*half+right.Normalized()*(.8f+offset);
                    const Vector3 sided=center+(probe-center)*side;
                    float height=0;
                    const bool valid=terrain->SampleHeight(sided.x,sided.z,height);
                    if (hazard_signature)
                    {
                        // Furniture depends on whether protection is needed, not every
                        // centimetre a nearby building moves the soil. Keep the detailed
                        // cache key for actual rebuilds and existing on-disk bakes.
                        const Vector3 edge = center + (right * half + right.Normalized() * .8f) * side;
                        recipe.Add(valid && edge.y - height > std::max(1.5f, offset * .35f));
                    }
                    else
                    {
                        recipe.Add(valid); if (valid) add(height);
                    }
                }
            }
        }
        for (Entity* point:entity->GetChildren())
        {
            if (point->GetObjectName().find("spline_point_") != 0) continue;
            recipe.Add(point->GetObjectName());
            for (const std::string& tag:point->GetTags())
            {
                if (tag.find("road_node_") != 0) continue;
                recipe.Add(tag);
                if (const auto found=junction_degree.find(tag);found!=junction_degree.end()) add(static_cast<float>(found->second));
            }
            recipe.Add(uint8_t(0));
        }
        if (World::IsPreparing() && !hazard_signature) preparation_keys[entity->GetObjectId()] = hash;
        return hash;
    }

    inline void Tick(float dt)
    {
        // Road furniture is baked during world preparation. Runtime traffic only
        // follows those roads; rediscovery and terrain cache-key sampling belong
        // to authoring, and resume when returning to the editor.
        if (Engine::IsFlagSet(EngineMode::Playing) && !World::IsPreparing()) return;
        if (World::GetName()!="plan.world" || !Terrain::FindActive()) return;
        // Junction solving can replace every road frame. Wait for its final meshes
        // before building furniture, otherwise startup repeatedly discards these batches.
        if (Spline::HasPendingRoadWork()) return;
        Entity* root=root_id ? World::GetEntityById(root_id) : nullptr;
        if (!root)
        {
            Clear();
            root=World::CreateEntity(); root->SetObjectName("island_road_details"); root->SetTransient(true);
            root_id=root->GetObjectId(); MakeTriangle();
        }
        discover_time-=dt;
        if (discover_time<=0)
        {
            discover_time=3;
            std::unordered_map<uint64_t,Road> previous;
            for (const Road& r:roads) previous.emplace(r.id,r);
            roads.clear();
            junction_degree.clear();
            for (Entity* e:World::GetEntities())
            {
                Spline* s=e->GetComponent<Spline>();
                if (!s || !e->GetActive() || !s->GetMeshEnabled() || s->IsAttached() || !e->GetParent() || e->GetParent()->GetObjectName()!="roads") continue;
                const auto found=previous.find(e->GetObjectId());
                roads.push_back(found!=previous.end() ? found->second : Road{e->GetObjectId()});
                previous.erase(e->GetObjectId());
                size_t point_index=0;
                const size_t point_count=s->GetControlPointCount();
                for (Entity* point:e->GetChildren())
                {
                    if (point->GetObjectName().find("spline_point_")!=0) continue;
                    const uint32_t arms=point_index==0 || point_index+1==point_count ? 1 : 2;
                    ++point_index;
                    for (const std::string& tag:point->GetTags())
                        if (tag.find("road_node_")==0) junction_degree[tag]+=arms;
                }
            }
            for (const auto& [id,r]:previous) if (Entity* e=World::GetEntityById(r.detail_id)) World::RemoveEntity(e);
        }
        if (roads.empty()) return;
        // Missing assets must not stall world loading or produce an empty cache.
        // A later successful load changes the recipe and rebuilds these details.
        const bool guardrails_ready=road_guardrail::Load(World::GetResourceDirectory());
        cursor%=roads.size();
        Road& record=roads[cursor++];
        Entity* entity=World::GetEntityById(record.id);
        Spline* spline=entity ? entity->GetComponent<Spline>() : nullptr;
        if (!spline || spline->GetRoadFrames().empty()) return;
        const uint64_t signature = CacheKey(entity, spline, true);
        if (record.signature == signature) return;
        const uint64_t hash = CacheKey(entity, spline);
        // Keep a bounded pipeline: read/decode the next five roads while the
        // owning thread publishes this one. No scene access occurs in workers.
        if (World::IsPreparing() && guardrails_ready)
        {
            PrefetchBake(hash);
            for (size_t ahead = 0; ahead < 5 && cursor + ahead < roads.size(); ++ahead)
            {
                const Road& next = roads[cursor + ahead];
                if (next.signature != 0) continue;
                Entity* next_entity = World::GetEntityById(next.id);
                Spline* next_spline = next_entity ? next_entity->GetComponent<Spline>() : nullptr;
                if (next_spline && !next_spline->GetRoadFrames().empty()) PrefetchBake(CacheKey(next_entity, next_spline));
            }
        }
        auto progress = ProgressTracker::Begin(ProgressType::Terrain, entity->GetObjectName(), "Loading roadside details");
        const Stopwatch preparation_time;
        record.signature=signature;
        if (Entity* old=World::GetEntityById(record.detail_id)) World::RemoveEntity(old);
        Entity* group=Anchor(root,("details_"+entity->GetObjectName()).c_str(),Vector3::Zero,Vector3::Forward);
        record.detail_id=group->GetObjectId();
        Stopwatch phase;
        const bool cached = guardrails_ready && LoadDetails(group, hash, progress);
        cache_ms += phase.GetElapsedTimeMs();
        if (cached) ++cache_hits;
        else
        {
            ++cache_misses;
            progress.SetStep("Building roadside details and guardrails");
            phase.Start();
            Build(entity,spline,group,progress);
            build_ms += phase.GetElapsedTimeMs();
            progress.SetStep("Caching roadside details");
            phase.Start();
            if (guardrails_ready) SaveDetails(group, hash);
            save_ms += phase.GetElapsedTimeMs();
        }
        if (preparation_time.GetElapsedTimeMs() > 1000.0f)
            SP_LOG_INFO("Roadside preparation '%s': %.2f ms", entity->GetObjectName().c_str(), preparation_time.GetElapsedTimeMs());
    }
    inline bool PrepareWorld()
    {
        if (World::GetName() != "plan.world" || !Terrain::FindActive()) return true;
        const Stopwatch slice;
        do
        {
            Tick(0.0f);
            World::ProcessPendingAdditions();
            const bool pending = std::any_of(roads.begin(), roads.end(), [](const Road& road)
            {
                Entity* entity = World::GetEntityById(road.id);
                Spline* spline = entity ? entity->GetComponent<Spline>() : nullptr;
                return road.signature == 0 && spline && !spline->GetRoadFrames().empty();
            });
            if (!pending)
            {
                SP_LOG_INFO("Roadside bake: %u hits, %u misses, cache %.2f ms, generation %.2f ms, save %.2f ms",
                    cache_hits, cache_misses, cache_ms, build_ms, save_ms);
                return true;
            }
        } while (slice.GetElapsedTimeMs() < 20.0f);
        return false;
    }

}
