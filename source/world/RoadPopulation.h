// Copyright(c) 2015-2026 Panos Karabelas
// Licensed under the Spartan Engine License. See license.md in the repository root.
// https://github.com/PanosK92/SpartanEngine/blob/master/license.md
// Commercial use requires written permission and negotiated payment terms.
#pragma once
#include "RoadTraffic.h"

namespace spartan::road_traffic
{
    // A bounded, player-local sampling table. Clip segments, rather than
    // selecting whole edges: a kilometre-long edge can pass beside the player.
    class LocalPopulation
    {
        struct Site { size_t edge; float begin, end, cumulative; };
        std::vector<Site> sites;
    public:
        void Clear() { sites.clear(); }
        void Build(const Network& network, const Vector3& focus, const Vector3& forward, float radius)
        {
            sites.clear();
            float total=0;
            for (size_t e=0;e<network.edges.size();++e)
            {
                const Path& path=network.edges[e].lane;
                for (size_t i=1;i<path.points.size();++i)
                {
                    const Vector3 a=path.points[i-1], delta=path.points[i]-a, offset=a-focus;
                    const float aa=delta.x*delta.x+delta.z*delta.z;
                    if (aa<.001f) continue;
                    const float bb=offset.x*delta.x+offset.z*delta.z;
                    const float cc=offset.x*offset.x+offset.z*offset.z-radius*radius;
                    const float discriminant=bb*bb-aa*cc;
                    if (discriminant<=0) continue;
                    const float enter=std::max(0.0f,(-bb-sqrtf(discriminant))/aa);
                    const float leave=std::min(1.0f,(-bb+sqrtf(discriminant))/aa);
                    if (leave<=enter) continue;
                    const float length=path.distances[i]-path.distances[i-1];
                    for (float begin=enter;begin<leave;)
                    {
                        const float end=std::min(leave,begin+30.0f/length);
                        const Vector3 middle=a+delta*((begin+end)*.5f)-focus;
                        if (fabsf(middle.y)<=60)
                        {
                            const float distance=sqrtf(middle.x*middle.x+middle.z*middle.z);
                            const float ahead=Vector3::Dot(middle,forward)>0 ? 1.8f : 1.0f;
                            total+=(end-begin)*length*ahead/(1.0f+3.0f*distance/radius);
                            sites.push_back({e,path.distances[i-1]+length*begin,path.distances[i-1]+length*end,total});
                        }
                        begin=end;
                    }
                }
            }
        }
        bool Sample(uint32_t& random, size_t& edge, float& progress) const
        {
            if (sites.empty()) return false;
            auto next=[&]() { random^=random<<13;random^=random>>17;random^=random<<5;
                return static_cast<float>(random&0xffffff)/16777216.0f; };
            if (!random) random=1;
            const float target=next()*sites.back().cumulative;
            auto it=std::upper_bound(sites.begin(),sites.end(),target,
                [](float value,const Site& site){return value<site.cumulative;});
            if (it==sites.end()) it=sites.end()-1;
            edge=it->edge;progress=it->begin+(it->end-it->begin)*next();
            return true;
        }
    };
}
