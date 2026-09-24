// Copyright(c) 2015-2026 Panos Karabelas
// Licensed under the Spartan Engine License. See license.md in the repository root.
// https://github.com/PanosK92/SpartanEngine/blob/master/license.md
// Commercial use requires written permission and negotiated payment terms.
#pragma once
#include "World.h"
#include "Entity.h"
#include "components/Spline.h"
#include "components/Terrain.h"
#include "components/Physics.h"
#include "components/Render.h"
#include "../geometry/GeneratedCache.h"
#include "../geometry/Mesh.h"
#include "../core/ThreadPool.h"
#include "../rhi/RHI_Vertex.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <fstream>
#include "RoadGuardrailPlacement.h"

namespace spartan::road_guardrail
{
    using namespace math;
    constexpr float module_length = 4.0f;
    constexpr float post_spacing = 2.0f;
    constexpr float sample_spacing = 2.0f;
    constexpr float approach_length = 20.0f;
    constexpr float minimum_run = 24.0f;
    constexpr float batch_length = 64.0f;
    enum Module { Beam, Post, Splice, Reflector, Terminal, TerminalFace, ModuleCount };
    struct Corner { Vector3 position; Vector2 uv; Vector3 normal, tangent; };
    inline std::array<std::vector<Corner>, ModuleCount> modules;
    inline std::array<std::vector<std::vector<Corner>>, ModuleCount> module_lods;
    inline std::array<std::shared_ptr<Mesh>, ModuleCount> shared_meshes;
    inline uint64_t asset_hash = 0;

    inline void Clear() { for (auto& m : modules) m.clear(); for (auto& m : module_lods) m.clear(); shared_meshes.fill(nullptr); asset_hash = 0; }

    inline const std::vector<Corner>& ModuleLod(Module module, uint32_t lod)
    {
        if (lod == 0) return modules[module]; // Preserve the exact authored corners.
        auto& levels = module_lods[module];
        if (levels.empty())
        {
            std::vector<RHI_Vertex_PosTexNorTan> vertices;
            std::vector<uint32_t> indices;
            for (const Corner& c : modules[module])
            {
                indices.push_back(static_cast<uint32_t>(vertices.size()));
                vertices.emplace_back(c.position, c.uv, c.normal, c.tangent);
            }
            Mesh mesh;
            mesh.SetFlag(static_cast<uint32_t>(MeshFlags::PostProcessPreserveLod0));
            mesh.AddGeometry(vertices, indices, true);
            for (uint32_t i = 0; i < mesh.GetLodCount(0); ++i)
            {
                mesh.GetGeometryLod(0, i, &indices, &vertices);
                auto& corners = levels.emplace_back();
                corners.reserve(indices.size());
                for (uint32_t index : indices)
                {
                    const auto& v = vertices[index];
                    corners.push_back({v.get_position(), v.get_uv(), v.get_normal(), v.get_tangent()});
                }
            }
        }
        return levels[std::min<size_t>(lod, levels.size() - 1)];
    }

    struct Geometry
    {
        std::vector<RHI_Vertex_PosTexNorTan> vertices;
        std::vector<uint32_t> indices;
    };
    using GeometryLods = std::array<Geometry, mesh_lod_count>;

    inline bool Load(const std::string& directory)
    {
        if (asset_hash) return true;
        static const char* names[] = {"beam", "post", "splice", "reflector", "terminal", "terminal_face"};
        generated_cache::Hash hash;
        std::array<std::vector<Corner>, ModuleCount> loaded;
        for (size_t m = 0; m < ModuleCount; ++m)
        {
            std::ifstream file(directory + "guardrail/" + names[m] + ".rail", std::ios::binary);
            char magic[4] = {}; uint32_t count = 0;
            file.read(magic, 4); file.read(reinterpret_cast<char*>(&count), 4);
            if (!file || std::memcmp(magic, "GRL1", 4) || count == 0 || count > 100000 || count % 3) return false;
            loaded[m].reserve(count);
            for (uint32_t i = 0; i < count; ++i)
            {
                float v[11];
                file.read(reinterpret_cast<char*>(v), sizeof(v));
                if (!file || std::any_of(std::begin(v), std::end(v), [](float x) { return !std::isfinite(x); })) return false;
                loaded[m].push_back({Vector3(v[0],v[1],v[2]),Vector2(v[3],v[4]),Vector3(v[5],v[6],v[7]),Vector3(v[8],v[9],v[10])});
                hash.Bytes(v, sizeof(v));
            }
        }
        modules = std::move(loaded); asset_hash = hash.value;
        return true;
    }

    inline bool IsBatch(const std::string& name) { return name.find("roadside_guardrail_") == 0; }
    inline const char* InstanceName(Module module)
    {
        return module==Post ? "roadside_guardrail_posts" : module==Splice ? "roadside_guardrail_splices" : "roadside_guardrail_markers";
    }
    inline Module InstanceModule(const std::string& name)
    {
        for (Module module:{Post,Splice,Reflector}) if (name==InstanceName(module)) return module;
        return ModuleCount;
    }
    inline std::shared_ptr<Mesh> SharedMesh(Module module)
    {
        if (!shared_meshes[module])
        {
            std::vector<RHI_Vertex_PosTexNorTan> vertices;
            std::vector<uint32_t> indices;
            for (const Corner& c:modules[module])
            {
                indices.push_back(static_cast<uint32_t>(vertices.size()));
                vertices.emplace_back(c.position,c.uv,c.normal,c.tangent);
            }
            auto mesh=std::make_shared<Mesh>(); mesh->SetObjectName(InstanceName(module));
            mesh->SetFlag(static_cast<uint32_t>(MeshFlags::PostProcessPreserveLod0));
            mesh->AddGeometry(vertices,indices,true); mesh->CreateGpuBuffers();
            shared_meshes[module]=mesh;
        }
        return shared_meshes[module];
    }
    inline void AddCollision(Entity* entity)
    {
        // Static triangle meshes preserve the curve; a single convex hull would
        // bridge across hairpins and block the road enclosed by the barrier.
        // Generic prop simplification otherwise erases this thin steel surface.
        entity->GetComponent<Render>()->SetFlag(RenderFlags::PreserveCollisionGeometry);
        Physics* physics = entity->AddComponent<Physics>();
        physics->SetStatic(true); physics->SetBodyType(BodyType::Mesh);
    }

    struct Station
    {
        Vector3 center, right, up, forward;
        float distance, t, half_width, sidewalk;
    };

    // Road-agnostic placement: feed the finished road frames and junction indices.
    // World-distance sampling makes density independent of spline tessellation.
    template<typename CreateBatch, typename CreateInstances>
    inline void Build(Entity* road, Spline* spline, const std::vector<size_t>& junctions, CreateBatch create_batch, CreateInstances create_instances)
    {
        if (!asset_hash) return;
        const auto& frames = spline->GetRoadFrames();
        if (frames.size() < 2) return;
        const Matrix matrix = road->GetMatrix();
        std::vector<Station> path;
        float distance = 0;
        for (const auto& f : frames)
        {
            const Vector3 center = matrix * f.position;
            if (!path.empty()) distance += (center - path.back().center).Length();
            const Vector3 forward = (matrix * (f.position + f.tangent) - center).Normalized();
            // Spline profiles store tangent.Cross(up) as "right" (the left
            // side in this asset's basis). Derive a right-handed frame from
            // the authored up vector so posts rise above the road, on both sides.
            const Vector3 frame_up = matrix * (f.position + f.up) - center;
            const float lateral_scale = (matrix * (f.position + f.right) - center).Length();
            const Vector3 raw_right = frame_up.Cross(forward).Normalized()*lateral_scale;
            const Vector3 right = (raw_right - forward * raw_right.Dot(forward)).Normalized();
            const float half = (spline->GetRoadWidth() + (spline->GetRoadWidthEnd()-spline->GetRoadWidth())*f.t)*.5f;
            path.push_back({center,right,forward.Cross(right).Normalized(),forward,distance,f.t,
                half*raw_right.Length(),spline->GetSidewalkWidthAt(f.t)*raw_right.Length()});
        }
        const float length = path.back().distance;
        if (length < minimum_run + 12) return;
        auto sample = [&](float d)
        {
            d = std::clamp(d, 0.0f, length);
            auto it = std::lower_bound(path.begin(),path.end(),d,[](const Station& s,float value){return s.distance<value;});
            if (it == path.begin()) return path.front();
            if (it == path.end()) return path.back();
            const Station& b=*it; const Station& a=*(it-1);
            const float t=(d-a.distance)/std::max(.001f,b.distance-a.distance);
            const Vector3 forward=(a.forward+(b.forward-a.forward)*t).Normalized();
            Vector3 right=(a.right+(b.right-a.right)*t).Normalized();
            right=(right-forward*right.Dot(forward)).Normalized();
            return Station{a.center+(b.center-a.center)*t,right,forward.Cross(right).Normalized(),forward,d,
                a.t+(b.t-a.t)*t,a.half_width+(b.half_width-a.half_width)*t,a.sidewalk+(b.sidewalk-a.sidewalk)*t};
        };
        const size_t count=static_cast<size_t>(length/sample_spacing)+1;
        std::vector<Station> samples;
        std::vector<bool> allowed(count,true);
        for (size_t i=0;i<count;++i)
        {
            const Station s=sample(static_cast<float>(i)*sample_spacing);
            samples.push_back(s);
            // The terminal and its posts must fit entirely outside intersections
            // and pedestrian areas, including after run extension and gap filling.
            if (s.distance<6 || length-s.distance<6 || s.sidewalk>.25f) allowed[i]=false;
            for (size_t j:junctions)
                if (j<path.size() && std::abs(s.distance-path[j].distance)<std::max(22.0f,s.half_width*3.0f)) allowed[i]=false;
        }
        struct Chunk
        {
            float side, start, end, distance;
            Vector3 origin;
            std::array<GeometryLods, 2> geometry;
            std::array<std::vector<Matrix>, ModuleCount> instances;
        };
        std::vector<Chunk> chunks;
        Terrain* terrain=Terrain::FindActive();
        for (float side:{-1.0f,1.0f})
        {
            std::vector<bool> hazard(count,false);
            for (size_t i=0;i<count;++i)
            {
                if (!allowed[i]) continue;
                const Station& s=samples[i];
                const Station a=sample(s.distance-12), b=sample(s.distance+12);
                const float turn=a.forward.Cross(b.forward).y;
                const float curvature=std::acos(std::clamp(a.forward.Dot(b.forward),-1.0f,1.0f))/24.0f;
                const bool outside_bend=turn*side<0 && curvature>1.0f/140.0f && s.half_width>=2.5f;
                const Vector3 edge=s.center+s.right*(s.half_width+.8f)*side;
                bool drop=false;
                if (terrain)
                {
                    for (float offset:{3.0f,7.0f})
                    {
                        const Vector3 probe=edge+s.right*offset*side;
                        float ground=0;
                        if (terrain->SampleHeight(probe.x,probe.z,ground) && edge.y-ground>std::max(1.5f,offset*.35f)) drop=true;
                    }
                }
                hazard[i]=outside_bend || drop;
            }
            for (const auto& [first,last]:SelectRuns(hazard,allowed,sample_spacing,approach_length,12.0f,minimum_run))
            {
                const float start=samples[first].distance, end=samples[last].distance;
                for (float chunk=start;chunk<end-.01f;chunk+=batch_length)
                {
                    chunks.push_back({side, start, end, chunk});
                }
            }
        }
        if (chunks.empty()) return;
        // Resolve lazy module LODs before sharing their immutable data with workers.
        for (Module module : {Beam, Terminal, TerminalFace}) ModuleLod(module, 1);
        ThreadPool::ParallelLoop([&](uint32_t begin, uint32_t finish)
        {
            for (uint32_t i = begin; i < finish; ++i)
            {
                auto& output = chunks[i];
                const float side = output.side, start = output.start, end = output.end, chunk = output.distance;
                const float chunk_end=std::min(end,chunk+batch_length);
                auto& geometry = output.geometry;
                auto& instances = output.instances;
                const Vector3 origin = output.origin = sample(chunk).center;
                auto instance=[&](Module module,float at)
                {
                    const Station s=sample(at);
                    const Vector3 x=s.right*side, z=s.forward*side;
                    const Vector3 p=s.center+x*(s.half_width+.8f+s.sidewalk)-origin;
                    // Hardware is symmetric along its length. Rotating the left
                    // side preserves winding and normal-map handedness.
                    instances[module].emplace_back(x.x,x.y,x.z,0.0f,s.up.x,s.up.y,s.up.z,0.0f,
                        z.x,z.y,z.z,0.0f,p.x,p.y,p.z,1.0f);
                };
                auto append=[&](Module module,float at,float stretch=1.0f,float facing=1.0f)
                {
                    const size_t finish=module==Reflector || module==TerminalFace ? 1 : 0;
                    for (uint32_t lod = 0; lod < mesh_lod_count; ++lod)
                    {
                        const auto& corners = ModuleLod(module, lod);
                        auto& v=geometry[finish][lod].vertices; auto& ix=geometry[finish][lod].indices;
                        const uint32_t base=static_cast<uint32_t>(v.size());
                        for (const Corner& c:corners)
                        {
                            const Station s=sample(at+c.position.z*stretch*facing);
                            const Vector3 x=s.right*side, z=s.forward*facing;
                            const Vector3 center=s.center+x*(s.half_width+.8f+s.sidewalk);
                            const Vector3 n=(x*c.normal.x+s.up*c.normal.y+z*(c.normal.z/std::max(.001f,stretch))).Normalized();
                            Vector3 tangent=(x*c.tangent.x+s.up*c.tangent.y+z*(c.tangent.z*stretch)).Normalized();
                            tangent=(tangent-n*tangent.Dot(n)).Normalized();
                            v.emplace_back(center+x*c.position.x+s.up*c.position.y-origin,c.uv,n,tangent);
                        }
                        for (uint32_t j=0;j<static_cast<uint32_t>(corners.size());j+=3)
                        {
                            ix.push_back(base+j); ix.push_back(base+j+(side*facing>0 ? 1:2)); ix.push_back(base+j+(side*facing>0 ? 2:1));
                        }
                    }
                };
                for (float d=chunk;d<chunk_end-.01f;d+=module_length)
                {
                    append(Beam,d,std::min(module_length,chunk_end-d)/module_length);
                    if (d>start+.01f) instance(Splice,d);
                }
                for (float d=chunk;d<chunk_end-.01f;d+=post_spacing)
                {
                    instance(Post,d);
                    if (static_cast<int>(std::round((d-start)/post_spacing))%8==0) instance(Reflector,d);
                }
                if (chunk==start) {append(Terminal,start+.15f);append(TerminalFace,start+.15f);}
                if (chunk_end==end)
                {
                    instance(Post,end-.2f); append(Terminal,end-.15f,1,-1);append(TerminalFace,end-.15f,1,-1);
                }
            }
        }, static_cast<uint32_t>(chunks.size()));
        // Scene mutation and GPU publication remain on the owning thread.
        for (auto& output : chunks)
        {
            const auto& origin = output.origin;
            auto& geometry = output.geometry;
            auto& instances = output.instances;
            for (size_t finish=0;finish<2;++finish)
                if (!geometry[finish][0].vertices.empty()) create_batch(origin,finish,geometry[finish]);
            for (Module module:{Post,Splice,Reflector})
                if (!instances[module].empty()) create_instances(origin,module,instances[module]);
        }
    }
}
