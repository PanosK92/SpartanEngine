// Copyright(c) 2015-2026 Panos Karabelas. Distributed under the MIT license.
#pragma once
#include "World.h"
#include "Entity.h"
#include "components/Spline.h"
#include "components/Terrain.h"
#include "components/Render.h"
#include "../rendering/Material.h"
#include "../geometry/Mesh.h"
#include "../rhi/RHI_Vertex.h"
#include <unordered_map>
#include <array>
#include <cstring>

namespace spartan::island_road_details
{
    using namespace math;
    enum Finish { White, Red, Metal, Dark, Yellow, FinishCount };
    struct Road { uint64_t id; uint64_t detail_id = 0; uint64_t signature = 0; };
    inline std::vector<Road> roads;
    inline std::unordered_map<std::string,uint32_t> junction_degree;
    inline std::array<std::shared_ptr<Material>, FinishCount> materials;
    inline std::shared_ptr<Mesh> triangle;
    inline uint64_t root_id = 0;
    inline size_t cursor = 0;
    inline float discover_time = 0;

    inline void Clear()
    {
        roads.clear();
        junction_degree.clear();
        materials.fill(nullptr);
        triangle.reset();
        root_id = 0;
        cursor = 0;
        discover_time = 0;
    }

    inline Entity* Part(Entity* parent, const char* name, Vector3 position, Vector3 scale, Finish finish, bool triangular = false)
    {
        if (!materials[finish])
        {
            static const Color colors[] = {Color(.88f,.89f,.84f,1),Color(.65f,.015f,.012f,1),
                Color(.35f,.38f,.4f,1),Color(.025f,.03f,.035f,1),Color(1,.65f,.015f,1)};
            materials[finish] = std::make_shared<Material>();
            materials[finish]->SetObjectName("roadside_finish_"+std::to_string(finish));
            materials[finish]->SetColor(colors[finish]);
            materials[finish]->SetProperty(MaterialProperty::Roughness,finish == Metal ? .42f : .7f);
            materials[finish]->SetProperty(MaterialProperty::Metalness,finish == Metal ? .7f : 0);
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
        render->SetMaxRenderDistance(250);
        render->SetMaxShadowDistance(45);
        render->SetFlag(RenderFlags::ExcludeFromRayTracing);
        render->SetFlag(RenderFlags::ExcludeFromTerrainBlend);
        return part;
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

    inline void Build(Entity* road, Spline* spline, Entity* group)
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
            // A short barrier on the outside of a sharp bend, never across a junction.
            for (int step=0;step<7;++step)
            {
                const size_t j=before+(after-before)*step/6;
                if (near_junction(j,30)) continue;
                Entity* rail=Anchor(group,"roadside_guardrail",roadside(j,tangent_at(j),-turn),tangent_at(j));
                Part(rail,"rail_post",Vector3(0,.45f,0),Vector3(.10f,.9f,.10f),Metal);
                Part(rail,"beam",Vector3(0,.75f,0),Vector3(.12f,.22f,7),Metal);
            }
        }
        // Repeated furniture shares merged meshes. Keep signs individually
        // selectable, without retaining thousands of post/rail entities.
        std::array<std::vector<Matrix>,FinishCount> batches;
        const std::vector<Entity*> children=group->GetChildren();
        for (Entity* anchor:children)
        {
            const std::string& name=anchor->GetObjectName();
            if (name!="roadside_delineator" && name!="roadside_guardrail") continue;
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
    }

    inline void Tick(float dt)
    {
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
        cursor%=roads.size();
        Road& record=roads[cursor++];
        Entity* entity=World::GetEntityById(record.id);
        Spline* spline=entity ? entity->GetComponent<Spline>() : nullptr;
        if (!spline || spline->GetRoadFrames().empty()) return;
        uint64_t hash=1469598103934665603ull;
        auto add=[&](float value){uint32_t bits;std::memcpy(&bits,&value,sizeof(bits));hash=(hash^bits)*1099511628211ull;};
        add(spline->GetRoadWidth()); add(spline->GetRoadWidthEnd());
        add(spline->GetSidewalkWidth()); add(spline->GetSidewalkEnabled() ? 1.0f : 0.0f);
        for (const Vector3 v : {entity->GetPosition(),entity->GetRotation().ToEulerAngles(),entity->GetScale()})
            {add(v.x);add(v.y);add(v.z);}
        for (const auto& f:spline->GetRoadFrames()) {add(f.position.x);add(f.position.y);add(f.position.z);}
        for (Entity* point:entity->GetChildren())
            for (const std::string& tag:point->GetTags())
                if (const auto found=junction_degree.find(tag);found!=junction_degree.end()) add(static_cast<float>(found->second));
        if (record.signature==hash) return;
        record.signature=hash;
        if (Entity* old=World::GetEntityById(record.detail_id)) World::RemoveEntity(old);
        Entity* group=Anchor(root,("details_"+entity->GetObjectName()).c_str(),Vector3::Zero,Vector3::Forward);
        record.detail_id=group->GetObjectId();
        Build(entity,spline,group);
    }
}
