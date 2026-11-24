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
        struct SegmentChunk
        {
            size_t edge;
            size_t begin_index;
            size_t end_index;
            float min_x, max_x;
            float min_y, max_y;
            float min_z, max_z;
        };
        std::vector<Site> sites;
        std::vector<SegmentChunk> chunks;
        const Edge* cached_edges_ptr = nullptr;
        size_t cached_edge_count     = 0;
        size_t cached_point_count    = 0;

        void EnsureChunks(const Network& network)
        {
            size_t total_points = 0;
            for (const Edge& edge : network.edges)
            {
                total_points += edge.lane.points.size();
            }
            if (cached_edges_ptr == network.edges.data() && cached_edge_count == network.edges.size() && cached_point_count == total_points)
            {
                return;
            }
            cached_edges_ptr   = network.edges.data();
            cached_edge_count  = network.edges.size();
            cached_point_count = total_points;
            chunks.clear();
            chunks.reserve(total_points / 16 + network.edges.size());
            constexpr size_t chunk_segments = 32;
            for (size_t e = 0; e < network.edges.size(); ++e)
            {
                const Path& path = network.edges[e].lane;
                if (path.points.size() < 2)
                {
                    continue;
                }
                for (size_t start = 1; start < path.points.size(); start += chunk_segments)
                {
                    const size_t stop = std::min(path.points.size(), start + chunk_segments);
                    const Vector3& p0 = path.points[start - 1];
                    SegmentChunk chunk{ e, start, stop, p0.x, p0.x, p0.y, p0.y, p0.z, p0.z };
                    for (size_t i = start; i < stop; ++i)
                    {
                        const Vector3& p = path.points[i];
                        chunk.min_x = std::min(chunk.min_x, p.x);
                        chunk.max_x = std::max(chunk.max_x, p.x);
                        chunk.min_y = std::min(chunk.min_y, p.y);
                        chunk.max_y = std::max(chunk.max_y, p.y);
                        chunk.min_z = std::min(chunk.min_z, p.z);
                        chunk.max_z = std::max(chunk.max_z, p.z);
                    }
                    chunks.push_back(chunk);
                }
            }
        }
    public:
        void Clear()
        {
            sites.clear();
            chunks.clear();
            cached_edges_ptr   = nullptr;
            cached_edge_count  = 0;
            cached_point_count = 0;
        }
        void Build(const Network& network, const Vector3& focus, const Vector3& forward, float radius)
        {
            sites.clear();
            EnsureChunks(network);
            const float radius_sq = radius * radius;
            const float min_x     = focus.x - radius;
            const float max_x     = focus.x + radius;
            const float min_y     = focus.y - 60.0f;
            const float max_y     = focus.y + 60.0f;
            const float min_z     = focus.z - radius;
            const float max_z     = focus.z + radius;
            float total = 0;
            for (const SegmentChunk& chunk : chunks)
            {
                if (chunk.max_x < min_x || chunk.min_x > max_x || chunk.max_z < min_z || chunk.min_z > max_z || chunk.max_y < min_y || chunk.min_y > max_y)
                {
                    continue;
                }
                const Path& path = network.edges[chunk.edge].lane;
                for (size_t i = chunk.begin_index; i < chunk.end_index; ++i)
                {
                    const Vector3 a=path.points[i-1], delta=path.points[i]-a, offset=a-focus;
                    const float aa=delta.x*delta.x+delta.z*delta.z;
                    if (aa<.001f) continue;
                    const float bb=offset.x*delta.x+offset.z*delta.z;
                    const float cc=offset.x*offset.x+offset.z*offset.z-radius_sq;
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
                            sites.push_back({chunk.edge,path.distances[i-1]+length*begin,path.distances[i-1]+length*end,total});
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
