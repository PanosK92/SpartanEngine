// Copyright(c) 2015-2026 Panos Karabelas
// Distributed under the repository's MIT license.
#pragma once
#include "RoadTraffic.h"
#include "World.h"
#include "Entity.h"
#include "components/Spline.h"
#include "components/Physics.h"
#include "components/Render.h"
#include <map>

namespace spartan::road_traffic
{
    inline Network BuildWorldNetwork(bool pedestrians = false)
    {
        Network network;
        Spline::RebuildRoadJunctions();
        std::vector<math::BoundingBox> obstacles;
        if (pedestrians)
        {
            for (Entity* entity : World::GetEntities())
            {
                Physics* physics = entity->GetComponent<Physics>();
                Render* render = entity->GetComponent<Render>();
                if (!entity->GetActive() || !physics || !physics->IsStatic() || !render || !render->GetMesh() || render->HasInstancing()) continue;
                if (physics->GetBodyType() == BodyType::Heightfield || physics->GetBodyType() == BodyType::Plane) continue;
                if (entity->GetComponent<Spline>() || (entity->GetParent() && entity->GetParent()->GetComponent<Spline>())) continue;
                const auto& bounds = render->GetBoundingBox();
                if (bounds.GetMin().IsFinite() && bounds.GetMax().IsFinite()) obstacles.push_back(bounds);
            }
        }
        auto blocked = [&](const Vector3& a, const Vector3& b)
        {
            for (const auto& bounds : obstacles)
                if (WalkingSegmentBlocked(a, b, bounds.GetMin(), bounds.GetMax())) return true;
            return false;
        };
        for (Entity* entity : World::GetEntities())
        {
            Spline* spline = entity->GetComponent<Spline>();
            if (!spline || !entity->GetActive() || !spline->GetMeshEnabled() ||
                spline->GetProfile() != SplineProfile::Road || spline->IsAttached()) continue;
            const auto& frames = spline->GetRoadFrames();
            if (frames.size() < 2) continue;
            const uint32_t count = spline->GetControlPointCount();
            if (count < 2) continue;
            if (pedestrians)
            {
                if (!spline->GetSidewalkEnabled()) continue;
                for (float side : {-1.0f, 1.0f})
                {
                    Path path;
                    auto flush = [&]() { network.AddSidewalk(entity->GetObjectId(), path); path = Path{}; };
                    for (size_t i = 0; i < frames.size(); ++i)
                    {
                        const auto& f = frames[i];
                        const float paving_width = spline->GetSidewalkWidthAt(f.t);
                        // Leave room for a body at tapered ends and stop before junction cutouts.
                        if (paving_width < 1.2f || (i > 0 && spline->IsJunctionSegment(i - 1)) || spline->IsJunctionSegment(i))
                        {
                            flush();
                            continue;
                        }
                        const float road_width = spline->GetRoadWidth() + (spline->GetRoadWidthEnd() - spline->GetRoadWidth()) * f.t;
                        const float height = spline->GetCurbHeight() * paving_width / std::max(spline->GetSidewalkWidth(), 0.001f);
                        const Vector3 position = entity->GetMatrix() * (f.position + f.right * (side * (road_width + paving_width) * 0.5f) + f.up * height);
                        if (blocked(position, position)) { flush(); continue; }
                        if (!path.points.empty() && blocked(path.points.back(), position)) flush();
                        path.Add(position);
                    }
                    flush();
                }
                continue;
            }
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
                network.AddRoad(entity->GetObjectId(), a->second, b->second, path, width);
            }
        }
        SP_LOG_INFO("road traffic: %u junction/end nodes, %u directed lanes", static_cast<uint32_t>(network.nodes.size()), static_cast<uint32_t>(network.edges.size()));
        return network;
    }
}
