// Copyright(c) 2015-2026 Panos Karabelas. Distributed under the MIT license.
#pragma once
#include "../RoadCrossSection.h"

namespace spartan::island_road_surface
{
    // The island opts into a layered road finish; generic spline materials keep
    // their existing UV/profile contract, including editor test fixtures.
    inline bool Enabled(Entity* entity)
    {
        return World::GetName()=="plan.world" && entity && entity->GetParent()
            && entity->GetParent()->GetObjectName()=="roads";
    }

    inline std::shared_ptr<Material> MaterialFor(int layer)
    {
        const char* names[]={"island_asphalt","island_road_paint","island_road_shoulder"};
        auto material=ResourceCache::GetByName<Material>(names[layer]);
        if (material) return material;
        material=std::make_shared<Material>();
        material->SetObjectName(names[layer]);
        const std::string base="project/materials/island_roads/";
        const std::string asset=layer==2 ? "gravel_road" : "asphalt_track";
        const std::string size=layer==2 ? "2k" : "4k";
        auto texture=[&](MaterialTextureType type,const char* channel)
        {
            material->SetTexture(type,base+asset+"_"+channel+"_"+size+".jpg");
        };
        if (layer!=1) texture(MaterialTextureType::Color,"diff");
        texture(MaterialTextureType::Normal,"nor_gl");
        texture(MaterialTextureType::Roughness,"rough");
        texture(MaterialTextureType::Occlusion,"ao");
        material->SetColor(layer==1 ? Color(.72f,.71f,.65f,1) :
            layer==2 ? Color(.66f,.63f,.58f,1) : Color(1.6f,1.65f,1.7f,1));
        material->SetProperty(MaterialProperty::Metalness,0);
        material->SetProperty(MaterialProperty::Roughness,layer==1 ? .92f : 1.0f);
        material->SetProperty(MaterialProperty::Normal,layer==2 ? .55f : .32f);
        material->SetProperty(MaterialProperty::TerrainBlend,layer==2 ? .8f : 0);
        material->SetProperty(MaterialProperty::TerrainCoating,0);
        material->SetProperty(MaterialProperty::IsRoadSurface,layer==0 ? 1.0f : 0.0f);
        material->SetProperty(MaterialProperty::IsRoadPaint,layer==1 ? 1.0f : 0.0f);
        material->SetProperty(MaterialProperty::CullMode,static_cast<float>(RHI_CullMode::None));
        return material;
    }

    inline void SetLayer(Entity* parent,const char* name,
        const std::shared_ptr<Mesh>& mesh,int layer)
    {
        Entity* child=parent->GetChildByName(name);
        if (child) {child->RemoveComponent<Physics>();child->RemoveComponent<Render>();}
        if (!mesh) return;
        if (!child)
        {
            child=World::CreateEntity();child->SetObjectName(name);child->SetParent(parent);
            child->SetPositionLocal(Vector3::Zero);child->SetRotationLocal(Quaternion::Identity);
            child->SetScaleLocal(Vector3::One);child->SetTransient(true);
        }
        mesh->SetObjectName(std::string(name)+"_"+std::to_string(parent->GetObjectId()));
        mesh->CreateGpuBuffers();
        Render* render=child->AddComponent<Render>();
        render->SetOwnedMesh(mesh);render->SetMaterial(MaterialFor(layer));
        render->SetFlag(RenderFlags::ExcludeFromTerrainBlend,layer!=2);
        if (layer==1) render->SetFlag(RenderFlags::CastsShadows,false);
        if (layer==2)
        {
            render->SetFlag(RenderFlags::PreserveCollisionGeometry,true);
            child->AddComponent<Physics>()->SetBodyType(BodyType::Mesh);
        }
    }

    // Paint has a physical width and dash length, independent of road width or
    // asphalt repeat. Clip each dash to the final trimmed road segment so the
    // junction remains clear. The surface grain comes from the asphalt normal.
    inline void Markings(const SplineFrame& a,const SplineFrame& b,float width_a,float width_b,
        std::vector<RHI_Vertex_PosTexNorTan>& vertices,std::vector<uint32_t>& indices)
    {
        const float span=b.distance-a.distance;
        if (span<.0001f) return;
        auto ribbon=[&](float start,float end,float lane,float paint_width,float fixed_offset=0.0f)
        {
            start=std::max(start,a.distance);end=std::min(end,b.distance);
            if (end-start<.01f) return;
            std::vector<Vector2> strip;
            for (float distance:{start,end})
            {
                const float t=(distance-a.distance)/span;
                const float width=width_a+(width_b-width_a)*t;
                const float offset=lane*road_cross_section::EdgeOffset(width)+fixed_offset;
                // A very small irregular edge avoids perfectly machined paint.
                const float wear=.006f*sinf(distance*7.13f+lane*2.1f);
                for (float side:{-1.0f,1.0f})
                    strip.emplace_back(.5f+(offset+side*(paint_width*.5f+wear))/width,t);
            }
            std::swap(strip[2],strip[3]);
            // Follow the actual asphalt triangles, including their diagonal.
            // Interpolating a frame instead makes paint sink into banked quads.
            const Vector3 al=a.position-a.right*(width_a*.5f), ar=a.position+a.right*(width_a*.5f);
            const Vector3 bl=b.position-b.right*(width_b*.5f), br=b.position+b.right*(width_b*.5f);
            for (int half:{-1,1})
            {
                std::vector<Vector2> polygon;
                for (size_t i=0;i<strip.size();++i)
                {
                    const Vector2 p=strip[i],q=strip[(i+1)%strip.size()];
                    const float dp=(p.x+p.y-1)*half,dq=(q.x+q.y-1)*half;
                    if (dp>=0) polygon.push_back(p);
                    if ((dp<0)!=(dq<0)) polygon.push_back(p+(q-p)*(dp/(dp-dq)));
                }
                if (polygon.size()<3) continue;
                const uint32_t base=static_cast<uint32_t>(vertices.size());
                for (const Vector2& p:polygon)
                {
                    const Vector3 position=half<0 ? al*(1-p.x-p.y)+ar*p.x+bl*p.y
                        : ar*(1-p.y)+bl*(1-p.x)+br*(p.x+p.y-1);
                    const Vector3 up=(a.up+(b.up-a.up)*p.y).Normalized();
                    const Vector3 right=(a.right+(b.right-a.right)*p.y).Normalized();
                    vertices.emplace_back(position+up*.008f,
                        Vector2(p.x,(a.distance+span*p.y-floorf(start/3)*3)/3),up,right);
                }
                for (uint32_t i=1;i+1<static_cast<uint32_t>(polygon.size());++i)
                    indices.insert(indices.end(),{base,base+i,base+i+1});
            }
        };
        const bool four_lanes=road_cross_section::FourLanes(std::min(width_a,width_b));
        if (four_lanes)
        {
            // Opposing directions share the same asphalt, separated by paint.
            // Same-direction dividers leave each of the four lanes 3.5 m wide.
            ribbon(a.distance,b.distance,0,.12f,-.12f);
            ribbon(a.distance,b.distance,0,.12f, .12f);
            for (int dash=static_cast<int>(floorf(a.distance/9));dash*9<b.distance;++dash)
                for (float lane:{-.5f,.5f}) ribbon(dash*9.0f,dash*9.0f+3.0f,lane,.12f);
        }
        else if (std::min(width_a,width_b)>=6.0f)
        {
            for (int dash=static_cast<int>(floorf(a.distance/9));dash*9<b.distance;++dash)
                ribbon(dash*9.0f,dash*9.0f+3.0f,0,.13f);
        }
        for (float side:{-1.0f,1.0f}) ribbon(a.distance,b.distance,side,.12f);
    }
}
