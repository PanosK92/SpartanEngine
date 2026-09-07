// Copyright(c) 2015-2026 Panos Karabelas. MIT license.
#include <cassert>
#include <cstdio>
#include "../../source/world/RoadTraffic.h"
using namespace spartan::road_traffic;

Path line(Vector3 a, Vector3 b)
{
    Path path; path.Add(a); path.Add(b); return path;
}
int main()
{
    Network network;
    network.AddRoad(1, "west", "junction", line({-100, 0, 0}, {0, 10, 0}), 10);
    network.AddRoad(2, "junction", "east", line({0, 10, 0}, {100, 20, 0}), 10);
    network.AddRoad(3, "junction", "north", line({0, 10, 0}, {0, 10, 100}), 10);
    assert(network.edges.size() == 6);
    // Opposite directions occupy opposite sides and follow the road grade.
    auto forward = network.edges[0].lane.Sample(30);
    auto reverse = network.edges[1].lane.Sample(30);
    assert(forward.position.z < -2.4f && reverse.position.z > 2.4f);
    assert(fabsf(forward.position.y - (forward.position.x + 100) * 0.1f) < 0.001f);
    assert(forward.tangent.x > 0 && reverse.tangent.x < 0);
    uint32_t random = 17;
    bool straight = false, turn = false;
    for (int i = 0; i < 100; i++)
    {
        size_t next = network.ChooseExit(0, random);
        assert(next == 2 || next == 4); // never U-turn at a connected junction
        straight |= next == 2; turn |= next == 4;
    }
    assert(straight && turn);
    assert(network.ChooseExit(2, random) == 3); // dead end returns on its other lane

    Path route = network.edges[0].lane;
    const float join = route.Length();
    network.AppendExit(route, 0, 4);
    for (size_t i = 1; i < route.points.size(); i++)
        assert((route.points[i] - route.points[i - 1]).Length() < 2.1f);
    auto entering = route.Sample(join + 0.01f);
    assert(Vector3::Dot(entering.tangent, forward.tangent) > 0.99f);
    float progress = join + 10;
    const auto before = route.Sample(progress);
    route.DiscardBehind(progress);
    assert((route.Sample(progress).position - before.position).Length() < 0.001f);
    assert(fabsf(route.Project(before.position, progress - 2, progress + 2) - progress) < 0.001f);

    // Geometric crossings and malformed shared tags do not connect overpasses.
    network.AddRoad(4, "bridge_a", "bridge_b", line({0, 50, -100}, {0, 50, 100}), 10);
    assert(network.nodes[network.edges[0].to].exits.size() == 3);
    network.AddRoad(5, "junction", "upper", line({0, 50, 0}, {100, 50, 100}), 10);
    assert(network.nodes[network.edges[0].to].exits.size() == 3);

    // Determinism, closed circuits, long runs and bounded route storage.
    Network loop;
    loop.AddRoad(1, "a", "b", line({0, 0, 0}, {0, 5, 100}), 10);
    loop.AddRoad(2, "b", "c", line({0, 5, 100}, {100, 10, 100}), 10);
    loop.AddRoad(3, "c", "a", line({100, 10, 100}, {0, 0, 0}), 10);
    size_t edge = 0; route = loop.edges[0].lane; progress = 0;
    for (int frame = 0; frame < 100000; frame++)
    {
        if (route.Length() - progress < 40)
        {
            const size_t next = loop.ChooseExit(edge, random);
            route.DiscardBehind(progress);
            loop.AppendExit(route, edge, next);
            edge = next;
        }
        const auto a = route.Sample(progress);
        progress += 0.2f;
        const auto b = route.Sample(progress);
        assert(b.position.IsFinite());
        assert((b.position - a.position).Length() < 0.201f);
        assert(route.points.size() < 200);
    }
    Network elbow;
    elbow.AddRoad(1, "a", "b", line({0,0,0}, {60,0,0}), 12);
    elbow.AddRoad(2, "b", "c", line({60,0,0}, {60,0,60}), 12);
    Path bend = elbow.edges[0].lane;
    elbow.AppendExit(bend, 0, 2);
    for (const auto& p : bend.points)
        assert((p.x <= 60.001f && fabsf(p.z) <= 6.001f) || (p.z >= -0.001f && fabsf(p.x - 60) <= 6.001f));
    Path opposite = elbow.edges[3].lane;
    elbow.AppendExit(opposite, 3, 1);
    for (const auto& a : bend.points)
        for (const auto& b : opposite.points)
            assert((a - b).Length() > 5.0f);
    Network pedestrians;
    pedestrians.AddRoad(1, "a", "b", line({0,0,0}, {0,10,100}), 12, true);
    const auto walking = pedestrians.edges[0].lane.Sample(20);
    assert(fabsf(walking.position.x - 5.2f) < 0.01f);
    assert(walking.position.y > 1.0f && walking.position.y < 4.0f);
    assert(pedestrians.edges[1].lane.Sample(20).position.x < -5.1f);
    Path empty;
    assert(empty.Sample(10).position.IsFinite());
    Network invalid;
    invalid.AddRoad(1, "a", "b", empty, 10);
    invalid.AddRoad(2, "a", "b", line({0,0,0}, {20,0,0}), 2);
    assert(invalid.edges.empty());
    puts("PASS lanes, grades, junction choices, dead ends, overpasses, turns, and 100000 traversal steps");
}
