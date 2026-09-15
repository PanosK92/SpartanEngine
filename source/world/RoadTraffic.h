// Copyright(c) 2015-2026 Panos Karabelas
// Distributed under the repository's MIT license.
#pragma once
#include "../math/Vector3.h"
#include "RoadCrossSection.h"
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

    // Sweep an upright pedestrian through a static obstacle's bounds. Include the
    // entire segment so thin walls between spline samples cannot be skipped.
    inline bool WalkingSegmentBlocked(const Vector3& a, const Vector3& b, Vector3 lo, Vector3 hi)
    {
        lo -= Vector3(0.45f, 1.8f, 0.45f);
        hi += Vector3(0.45f, -0.25f, 0.45f);
        float enter = 0.0f, leave = 1.0f;
        const Vector3 d = b - a;
        for (int axis = 0; axis < 3; ++axis)
        {
            const float origin = axis == 0 ? a.x : axis == 1 ? a.y : a.z;
            const float delta = axis == 0 ? d.x : axis == 1 ? d.y : d.z;
            const float minimum = axis == 0 ? lo.x : axis == 1 ? lo.y : lo.z;
            const float maximum = axis == 0 ? hi.x : axis == 1 ? hi.y : hi.z;
            if (fabsf(delta) < 0.000001f)
            {
                if (origin < minimum || origin > maximum) return false;
                continue;
            }
            const float t0 = (minimum - origin) / delta, t1 = (maximum - origin) / delta;
            enter = std::max(enter, std::min(t0, t1));
            leave = std::min(leave, std::max(t0, t1));
            if (enter > leave) return false;
        }
        return true;
    }

    // Static obstacle hierarchy for sidewalk construction. The broad phase only
    // rejects disjoint bounds; leaves retain the exact swept-body clearance test.
    class WalkingObstacles
    {
    public:
        struct Bounds { Vector3 minimum, maximum; };
        void Add(const Vector3& lo, const Vector3& hi) { bounds.push_back({lo, hi}); }
        void Build()
        {
            nodes.clear();
            nodes.reserve(bounds.size() * 2);
            if (!bounds.empty()) BuildNode(0, bounds.size());
        }
        bool Blocked(const Vector3& a, const Vector3& b) const
        {
            if (nodes.empty()) return false;
            const Vector3 lo(std::min(a.x,b.x)-0.45f, std::min(a.y,b.y), std::min(a.z,b.z)-0.45f);
            const Vector3 hi(std::max(a.x,b.x)+0.45f, std::max(a.y,b.y)+1.8f, std::max(a.z,b.z)+0.45f);
            return Query(0, lo, hi, a, b);
        }
    private:
        struct Node { Bounds box; size_t begin, end, left = invalid, right = invalid; };
        std::vector<Bounds> bounds;
        std::vector<Node> nodes;
        size_t BuildNode(size_t begin, size_t end)
        {
            Bounds box = bounds[begin];
            for (size_t i = begin + 1; i < end; ++i)
            {
                const auto& b = bounds[i];
                box.minimum = Vector3(std::min(box.minimum.x,b.minimum.x),std::min(box.minimum.y,b.minimum.y),std::min(box.minimum.z,b.minimum.z));
                box.maximum = Vector3(std::max(box.maximum.x,b.maximum.x),std::max(box.maximum.y,b.maximum.y),std::max(box.maximum.z,b.maximum.z));
            }
            const size_t index = nodes.size();
            nodes.push_back({box, begin, end});
            if (end - begin <= 8) return index;
            const Vector3 size = box.maximum - box.minimum;
            const bool split_x = size.x >= size.z;
            const size_t middle = begin + (end - begin) / 2;
            std::nth_element(bounds.begin()+begin, bounds.begin()+middle, bounds.begin()+end,
                [split_x](const Bounds& a, const Bounds& b)
                {
                    return split_x ? a.minimum.x+a.maximum.x < b.minimum.x+b.maximum.x
                                   : a.minimum.z+a.maximum.z < b.minimum.z+b.maximum.z;
                });
            const size_t left = BuildNode(begin, middle);
            const size_t right = BuildNode(middle, end);
            nodes[index].left = left;
            nodes[index].right = right;
            return index;
        }
        bool Query(size_t index, const Vector3& lo, const Vector3& hi, const Vector3& a, const Vector3& b) const
        {
            const Node& node = nodes[index];
            if (hi.x < node.box.minimum.x || lo.x > node.box.maximum.x ||
                hi.y < node.box.minimum.y || lo.y > node.box.maximum.y ||
                hi.z < node.box.minimum.z || lo.z > node.box.maximum.z) return false;
            if (node.left != invalid)
                return Query(node.left,lo,hi,a,b) || Query(node.right,lo,hi,a,b);
            for (size_t i = node.begin; i < node.end; ++i)
                if (WalkingSegmentBlocked(a,b,bounds[i].minimum,bounds[i].maximum)) return true;
            return false;
        }
    };

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
                // This segment is already known; Sample would binary-search the
                // whole path and normalize a tangent that projection never uses.
                const float fraction = (d - distances[i - 1]) / (distances[i] - distances[i - 1]);
                const float e = (points[i - 1] + delta * fraction - p).LengthSquared();
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
        bool sidewalk = false;
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

        void AddRoad(uint64_t road, const std::string& from, const std::string& to, const Path& center, float width)
        {
            if (center.Length() < 4.0f || width < 4.5f) return;
            const size_t a = NodeId(from, center.points.front());
            const size_t b = NodeId(to, center.points.back());
            const float trim = std::min(width * 0.8f, center.Length() * 0.2f);
            const float offset = road_cross_section::TrafficOffset(width);
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

        // Each continuous sidewalk has two directions on the SAME surface.
        // No inferred road-crossing curves at its ends or at unmarked junctions.
        void AddSidewalk(uint64_t road, const Path& path)
        {
            if (path.Length() < 4.0f) return;
            const size_t first = edges.size();
            const std::string key = "sidewalk_" + std::to_string(first);
            const size_t a = NodeId(key + "_a", path.points.front());
            const size_t b = NodeId(key + "_b", path.points.back());
            for (int direction : {1, -1})
            {
                Edge edge;
                edge.road = road;
                edge.sidewalk = true;
                edge.from = direction == 1 ? a : b;
                edge.to = direction == 1 ? b : a;
                edge.reverse = direction == 1 ? first + 1 : first;
                if (direction == 1) edge.lane = path;
                else for (auto it = path.points.rbegin(); it != path.points.rend(); ++it) edge.lane.Add(*it);
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
            if (edges[incoming].sidewalk)
            {
                for (const Vector3& p : edges[outgoing].lane.points) path.Add(p);
                return;
            }
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
