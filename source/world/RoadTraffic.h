// Copyright(c) 2015-2026 Panos Karabelas
// Distributed under the repository's MIT license.
#pragma once
#include "../math/Vector3.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <string>
#include <unordered_map>
#include <vector>

namespace spartan::road_traffic
{
    using math::Vector3;
    constexpr size_t invalid = std::numeric_limits<size_t>::max();

    struct Pose { Vector3 position; Vector3 tangent; };
    struct Path
    {
        std::vector<Vector3> points;
        std::vector<float> distances;
        float Length() const { return distances.empty() ? 0.0f : distances.back(); }
        void Add(const Vector3& p)
        {
            if (!p.IsFinite()) return;
            const float step = points.empty() ? 0.0f : (p - points.back()).Length();
            if (!points.empty() && step < 0.001f) return;
            distances.push_back(Length() + step);
            points.push_back(p);
        }
        Pose Sample(float distance) const
        {
            if (points.size() < 2) return {points.empty() ? Vector3(0.0f) : points.front(), Vector3(0, 0, 1)};
            distance = std::clamp(distance, 0.0f, Length());
            auto it = std::upper_bound(distances.begin(), distances.end(), distance);
            const size_t b = std::clamp(static_cast<size_t>(it - distances.begin()), size_t(1), points.size() - 1);
            const float t = (distance - distances[b - 1]) / (distances[b] - distances[b - 1]);
            return {points[b - 1] + (points[b] - points[b - 1]) * t, (points[b] - points[b - 1]).Normalized()};
        }
        float Project(const Vector3& p, float begin, float end) const
        {
            float best = std::clamp(begin, 0.0f, Length());
            float error = std::numeric_limits<float>::max();
            auto first = std::upper_bound(distances.begin(), distances.end(), begin);
            size_t i = std::max(size_t(1), static_cast<size_t>(first - distances.begin()));
            for (; i < points.size() && distances[i - 1] <= end; ++i)
            {
                const Vector3 delta = points[i] - points[i - 1];
                const float t = std::clamp(Vector3::Dot(p - points[i - 1], delta) / delta.LengthSquared(), 0.0f, 1.0f);
                const float d = std::clamp(distances[i - 1] + t * (distances[i] - distances[i - 1]), begin, std::min(end, Length()));
                const float e = (Sample(d).position - p).LengthSquared();
                if (e < error) { error = e; best = d; }
            }
            return best;
        }
        void DiscardBehind(float& progress)
        {
            auto it = std::lower_bound(distances.begin(), distances.end(), std::max(progress - 8.0f, 0.0f));
            const size_t count = static_cast<size_t>(it - distances.begin());
            if (count == 0 || count >= points.size() - 1) return;
            const float offset = distances[count];
            points.erase(points.begin(), points.begin() + count);
            distances.erase(distances.begin(), distances.begin() + count);
            for (float& d : distances) d -= offset;
            progress -= offset;
        }
    };

    struct Edge
    {
        size_t from = invalid, to = invalid, reverse = invalid;
        uint64_t road = 0;
        float width = 0.0f;
        Path lane;
    };
    struct Node { Vector3 position; std::vector<size_t> exits; };

    class Network
    {
    public:
        std::vector<Node> nodes;
        std::vector<Edge> edges;
        std::unordered_map<std::string, size_t> node_ids;

        size_t NodeId(const std::string& key, const Vector3& position)
        {
            auto it = node_ids.find(key);
            if (it != node_ids.end())
            {
                // Tags connect authored junctions, never unrelated crossings.
                if ((nodes[it->second].position - position).LengthSquared() <= 4.0f) return it->second;
                return NodeId(key + "_separate", position);
            }
            const size_t id = nodes.size();
            nodes.push_back({position, {}});
            node_ids[key] = id;
            return id;
        }

        void AddRoad(uint64_t road, const std::string& from, const std::string& to, const Path& center, float width, bool pedestrians = false)
        {
            if (center.Length() < 4.0f || width < 4.5f) return;
            const size_t a = NodeId(from, center.points.front());
            const size_t b = NodeId(to, center.points.back());
            const float trim = std::min(width * 0.8f, center.Length() * 0.2f);
            const float offset = pedestrians ? width * 0.5f - 0.8f : std::min(width * 0.25f, width * 0.5f - 1.15f);
            const size_t first = edges.size();
            for (int direction : {1, -1})
            {
                Edge edge;
                edge.road = road;
                edge.width = width;
                edge.from = direction == 1 ? a : b;
                edge.to = direction == 1 ? b : a;
                edge.reverse = direction == 1 ? first + 1 : first;
                const float length = center.Length() - 2.0f * trim;
                const uint32_t steps = std::max(1u, static_cast<uint32_t>(std::ceil(length / 2.0f)));
                for (uint32_t i = 0; i <= steps; i++)
                {
                    float d = trim + length * static_cast<float>(i) / static_cast<float>(steps);
                    if (direction < 0) d = center.Length() - d;
                    Pose pose = center.Sample(d);
                    // Average neighbouring tangents to avoid a lateral jump at
                    // each tessellated road vertex, including uneven grades.
                    Vector3 tangent = (center.Sample(d + 1.0f).position - center.Sample(d - 1.0f).position).Normalized();
                    tangent *= static_cast<float>(direction);
                    Vector3 right(tangent.z, 0.0f, -tangent.x);
                    right.Normalize();
                    edge.lane.Add(pose.position + right * offset);
                }
                nodes[edge.from].exits.push_back(edges.size());
                edges.push_back(std::move(edge));
            }
        }

        size_t ChooseExit(size_t incoming, uint32_t& random) const
        {
            if (incoming >= edges.size()) return invalid;
            const Edge& edge = edges[incoming];
            std::vector<size_t> choices;
            for (size_t next : nodes[edge.to].exits)
                if (next != edge.reverse) choices.push_back(next);
            // A dead end turns back onto its other lane, never wraps to a
            // distant road or crosses the terrain to find a new destination.
            if (choices.empty()) return edge.reverse;
            random = random * 1664525u + 1013904223u;
            return choices[(random >> 8) % choices.size()];
        }

        void AppendExit(Path& path, size_t incoming, size_t outgoing) const
        {
            const Pose a = edges[incoming].lane.Sample(edges[incoming].lane.Length());
            const Pose b = edges[outgoing].lane.Sample(0.0f);
            auto curve = [&](const Pose& start, const Pose& end, float handle)
            {
                const Vector3 p1 = start.position + start.tangent * handle;
                const Vector3 p2 = end.position - end.tangent * handle;
                for (uint32_t i = 1; i <= 24; i++)
                {
                    const float t = static_cast<float>(i) / 24.0f, u = 1.0f - t;
                    path.Add(start.position * (u*u*u) + p1 * (3*u*u*t) + p2 * (3*u*t*t) + end.position * (t*t*t));
                }
            };
            const Node& junction = nodes[edges[incoming].to];
            if (junction.exits.size() == 2 && outgoing != edges[incoming].reverse && Vector3::Dot(a.tangent, b.tangent) < 0.95f)
            {
                // Circular-arc handles keep opposing lanes separate through an
                // elbow and avoid the oversized cubic's empty outer corner.
                const float angle = std::acos(std::clamp(Vector3::Dot(a.tangent, b.tangent), -0.99f, 0.99f));
                const float handle = (b.position - a.position).Length() * (2.0f / 3.0f) * std::tan(angle * 0.25f) / std::sin(angle * 0.5f);
                curve(a, b, handle);
            }
            else
            {
                const float handle = outgoing == edges[incoming].reverse
                    ? edges[incoming].width * 0.65f : (b.position - a.position).Length() * 0.5f;
                curve(a, b, handle);
            }
            for (const Vector3& p : edges[outgoing].lane.points) path.Add(p);
        }
    };
}
