// Copyright(c) 2015-2026 Panos Karabelas
// Licensed under the Spartan Engine License. See license.md in the repository root.
// https://github.com/PanosK92/SpartanEngine/blob/master/license.md
// Commercial use requires written permission and negotiated payment terms.
#pragma once
#include "../RoadCrossSection.h"

namespace spartan::road_surface
{
    using math::Vector2;
    using math::Vector3;
    using math::Quaternion;
    // Metres per asphalt texture repeat, puts the aggregate at its real 5-12 mm stone size.
    // Must match road_asphalt_repeat in common_road.hlsl.
    constexpr float asphalt_repeat=1.5f;

    // The application supplies authored road styling; the spline only builds geometry.
    inline bool (*enabled)(Entity*) = nullptr;
    inline std::shared_ptr<Material> (*material_for)(int) = nullptr;
    inline bool Enabled(Entity* entity) { return enabled && material_for && enabled(entity); }
    inline std::shared_ptr<Material> MaterialFor(int layer) { return material_for ? material_for(layer) : nullptr; }

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
                    // 3 mm is real thermoplastic thickness. U matches the asphalt
                    // (lateral metres / repeat) so the paint carries the grain beneath it.
                    const float width=width_a+(width_b-width_a)*p.y;
                    const float r=asphalt_repeat;
                    vertices.emplace_back(position+up*.003f,
                        Vector2((p.x-.5f)*width/r,(a.distance+span*p.y-floorf(start/r)*r)/r),up,right);
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
