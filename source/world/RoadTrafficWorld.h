// Copyright(c) 2015-2026 Panos Karabelas
// Licensed under the Spartan Engine License. See license.md in the repository root.
// https://github.com/PanosK92/SpartanEngine/blob/master/license.md
// Commercial use requires written permission and negotiated payment terms.
#pragma once
#include "RoadTraffic.h"
#include "World.h"
#include "Entity.h"
#include "components/Spline.h"
#include "components/Physics.h"
#include "components/Render.h"
#include <map>
#include "../geometry/GeneratedCache.h"
#include "../core/Stopwatch.h"

namespace spartan::road_traffic
{
    // Flatten only immutable navigation data, never pointers, actor state or population choices.
    struct BakedEdge
    {
        uint64_t from, to, reverse, road, first, count;
        float width;
        uint32_t sidewalk;
    };

    inline bool LoadNetwork(const std::filesystem::path& path, uint64_t key, const std::vector<uint64_t>& road_ids, Network& network)
    {
        std::vector<Vector3> nodes, points;
        std::vector<float> distances;
        std::vector<BakedEdge> edges;
        if (!generated_cache::Load(path, key, nodes, edges, points, distances) || points.size() != distances.size()) return false;
        Network loaded;
        for (const Vector3& p : nodes)
        {
            if (!p.IsFinite()) return false;
            loaded.nodes.push_back({p, {}});
        }
        for (const auto& e : edges)
        {
            if (e.from >= nodes.size() || e.to >= nodes.size() ||
                (e.reverse != invalid && e.reverse >= edges.size()) || e.first > points.size() ||
                e.count > points.size() - e.first || e.road >= road_ids.size() || e.count < 2 || !std::isfinite(e.width)) return false;
            Edge edge;
            edge.from = e.from; edge.to = e.to; edge.reverse = e.reverse; edge.road = road_ids[e.road];
            edge.width = e.width; edge.sidewalk = e.sidewalk != 0;
            for (size_t i = e.first; i < e.first + e.count; ++i)
                if (!points[i].IsFinite() || !std::isfinite(distances[i]) ||
                    (i == e.first ? distances[i] != 0 : distances[i] <= distances[i - 1])) return false;
            edge.lane.points.assign(points.begin() + e.first, points.begin() + e.first + e.count);
            edge.lane.distances.assign(distances.begin() + e.first, distances.begin() + e.first + e.count);
            loaded.nodes[e.from].exits.push_back(loaded.edges.size());
            loaded.edges.push_back(std::move(edge));
        }
        // node_ids is only used during construction; consumers use the baked node/edge indices.
        network = std::move(loaded);
        return true;
    }

    inline void SaveNetwork(const std::filesystem::path& path, uint64_t key, const std::vector<uint64_t>& road_ids, const Network& network)
    {
        std::vector<Vector3> nodes, points;
        std::vector<float> distances;
        std::vector<BakedEdge> edges;
        for (const auto& node : network.nodes) nodes.push_back(node.position);
        for (const auto& e : network.edges)
        {
            const uint64_t road = std::find(road_ids.begin(), road_ids.end(), e.road) - road_ids.begin();
            edges.push_back({e.from, e.to, e.reverse, road, points.size(), e.lane.points.size(), e.width, e.sidewalk ? 1u : 0u});
            points.insert(points.end(), e.lane.points.begin(), e.lane.points.end());
            distances.insert(distances.end(), e.lane.distances.begin(), e.lane.distances.end());
        }
        generated_cache::Save(path, key, nodes, edges, points, distances);
    }

    inline Network BuildWorldNetwork(bool pedestrians = false)
    {
        const Stopwatch timer;
        Network network;
        Spline::RebuildRoadJunctions();
        generated_cache::Hash hash;
        hash.Add(uint32_t(3)); // lane sampling, sidewalk clearance and binary format
        hash.Add(sizeof(BakedEdge)); hash.Add(pedestrians);
        const auto& entities = World::GetEntities();
        std::vector<uint64_t> obstacle_keys;
        std::vector<std::pair<uint64_t, Entity*>> roads;
        WalkingObstacles obstacles;
        if (pedestrians)
        {
            for (Entity* entity : entities)
            {
                Physics* physics = entity->GetComponent<Physics>();
                Render* render = entity->GetComponent<Render>();
                if (!entity->GetActive() || !physics || !physics->IsStatic() || !render || !render->GetMesh() || render->HasInstancing()) continue;
                if (physics->GetBodyType() == BodyType::Heightfield || physics->GetBodyType() == BodyType::Plane) continue;
                if (entity->GetComponent<Spline>() || (entity->GetParent() && entity->GetParent()->GetComponent<Spline>())) continue;
                const auto& bounds = render->GetBoundingBox();
                if (bounds.GetMin().IsFinite() && bounds.GetMax().IsFinite())
                {
                    generated_cache::Hash obstacle;
                    obstacle.Add(bounds.GetMin()); obstacle.Add(bounds.GetMax());
                    obstacle_keys.push_back(obstacle.value);
                    obstacles.Add(bounds.GetMin(), bounds.GetMax());
                }
            }
        }
        for (Entity* entity : entities)
        {
            Spline* spline = entity->GetComponent<Spline>();
            if (!spline || !entity->GetActive() || !spline->GetMeshEnabled() ||
                spline->GetProfile() != SplineProfile::Road || spline->IsAttached()) continue;
            if (spline->GetRoadFrames().size() < 2 || spline->GetControlPointCount() < 2 ||
                (pedestrians && !spline->GetSidewalkEnabled())) continue;
            generated_cache::Hash road_hash;
            road_hash.Add(entity->GetMatrix()); road_hash.Add(entity->GetScale());
            road_hash.Add(spline->GetRoadFrames()); road_hash.Add(spline->GetClosedLoop());
            road_hash.Add(spline->GetControlPointCount()); road_hash.Add(spline->GetRoadWidth()); road_hash.Add(spline->GetRoadWidthEnd());
            road_hash.Add(spline->GetSidewalkEnabled()); road_hash.Add(spline->GetSidewalkWidth()); road_hash.Add(spline->GetCurbHeight());
            const auto& frames = spline->GetRoadFrames();
            for (size_t i = 0; i < frames.size(); ++i)
            {
                road_hash.Add(spline->GetSidewalkWidthAt(frames[i].t)); road_hash.Add(spline->IsJunctionSegment(i));
            }
            for (Entity* child : entity->GetChildren())
            {
                if (!child || child->GetObjectName().find("spline_point_") != 0) continue;
                road_hash.Add(child->GetObjectName());
                for (const auto& tag : child->GetTags()) road_hash.Add(tag);
                road_hash.Add(uint8_t(0));
            }
            roads.emplace_back(road_hash.value, entity);
        }
        // Procedural splines receive new entity IDs each load. Cache canonical geometry order,
        // and remap baked road ordinals to this load's live entity IDs.
        std::sort(obstacle_keys.begin(), obstacle_keys.end());
        hash.Add(obstacle_keys);
        std::stable_sort(roads.begin(), roads.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
        std::vector<uint64_t> road_ids;
        hash.Add(uint64_t(roads.size()));
        for (const auto& [key, entity] : roads) { hash.Add(key); road_ids.push_back(entity->GetObjectId()); }
        const auto cache_path = generated_cache::Path(World::GetResourceDirectory(), pedestrians ? "walking_network" : "traffic_network", hash.value);
        // Never persist an incomplete graph while roads are still being prepared.
        const bool cacheable = !Spline::HasPendingRoadWork();
        if (cacheable && LoadNetwork(cache_path, hash.value, road_ids, network))
        {
            SP_LOG_INFO("Navigation bake (%s): hit, %.2f ms", pedestrians ? "walking" : "traffic", timer.GetElapsedTimeMs());
            return network;
        }
        obstacles.Build();
        auto blocked = [&](const Vector3& a, const Vector3& b)
        {
            return obstacles.Blocked(a, b);
        };
        for (const auto& [road_key, entity] : roads)
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
        if (cacheable) SaveNetwork(cache_path, hash.value, road_ids, network);
        SP_LOG_INFO("Navigation bake (%s): %s, %.2f ms", pedestrians ? "walking" : "traffic",
            cacheable ? "miss (missing/stale/invalid)" : "waiting for roads", timer.GetElapsedTimeMs());
        SP_LOG_INFO("road traffic: %u junction/end nodes, %u directed lanes", static_cast<uint32_t>(network.nodes.size()), static_cast<uint32_t>(network.edges.size()));
        return network;
    }
}
