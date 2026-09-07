// Copyright(c) 2015-2026 Panos Karabelas
// Distributed under the repository's MIT license.
#pragma once
#include "RoadTraffic.h"
#include "World.h"
#include "Entity.h"
#include "components/Spline.h"
#include <map>

namespace spartan::road_traffic
{
    inline Network BuildWorldNetwork(bool pedestrians = false)
    {
        Network network;
        Spline::RebuildRoadJunctions();
        for (Entity* entity : World::GetEntities())
        {
            Spline* spline = entity->GetComponent<Spline>();
            if (!spline || !entity->GetActive() || !spline->GetMeshEnabled() ||
                spline->GetProfile() != SplineProfile::Road || spline->IsAttached()) continue;
            const auto& frames = spline->GetRoadFrames();
            if (frames.size() < 2) continue;
            const uint32_t count = spline->GetControlPointCount();
            if (count < 2) continue;
            const std::string prefix = "road_" + std::to_string(entity->GetObjectId()) + "_";
            std::map<size_t, std::string> cuts;
            cuts[0] = prefix + "start";
            cuts[frames.size() - 1] = prefix + "end";
            uint32_t point = 0;
            for (Entity* child : entity->GetChildren())
            {
                if (!child || child->GetObjectName().find("spline_point_") != 0) continue;
                const float t = static_cast<float>(point++) / static_cast<float>(spline->GetClosedLoop() ? count : count - 1);
                auto it = std::lower_bound(frames.begin(), frames.end(), t, [](const SplineFrame& f, float value) { return f.t < value; });
                size_t frame = std::min(static_cast<size_t>(it - frames.begin()), frames.size() - 1);
                if (frame > 0 && fabsf(frames[frame - 1].t - t) < fabsf(frames[frame].t - t)) --frame;
                for (const std::string& tag : child->GetTags())
                {
                    if (tag.find("road_node_") == 0) { cuts[frame] = tag; break; }
                }
            }
            if (spline->GetClosedLoop()) cuts[frames.size() - 1] = cuts[0];
            for (auto a = cuts.begin(), b = std::next(a); b != cuts.end(); ++a, ++b)
            {
                road_traffic::Path path;
                for (size_t i = a->first; i <= b->first; ++i) path.Add(entity->GetMatrix() * frames[i].position);
                // Junction carve samples widen to the polygon radius; lane width
                // comes from the road, not that temporary terrain-carving extent.
                const Vector3 scale = entity->GetScale();
                const float width = std::min(spline->GetRoadWidth(), spline->GetRoadWidthEnd()) * fabsf(scale.x);
                network.AddRoad(entity->GetObjectId(), a->second, b->second, path, width, pedestrians);
            }
        }
        SP_LOG_INFO("road traffic: %u junction/end nodes, %u directed lanes", static_cast<uint32_t>(network.nodes.size()), static_cast<uint32_t>(network.edges.size()));
        return network;
    }
}
