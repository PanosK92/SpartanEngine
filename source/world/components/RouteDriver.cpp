/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

#include "pch.h"
#include "RouteDriver.h"
#include "Physics.h"
#include "../Entity.h"
#include "../World.h"
#include "../RoadTrafficWorld.h"
#include "../../car/Car.h"
#include "../../car/RacingLine.h"
#include "../../core/Engine.h"
#include "../../core/ProgressTracker.h"
#include "../../core/Timer.h"
#include "../../profiling/Profiler.h"
#include "../../rendering/Renderer.h"
#include "../../io/pugixml.hpp"
#include <queue>

using namespace std;
using namespace spartan::math;

#define SP_REGISTER_ROUTE_STAT(name, type) RegisterAttribute("m_" #name, #type, \
    [this]() { return m_stats.name; },                                          \
    [this](const std::any& value) { m_stats.name = std::any_cast<type>(value); })

namespace spartan
{
    namespace
    {
        using road_traffic::Edge;
        using road_traffic::Network;
        using road_traffic::invalid;

        constexpr float lead_in_half_width = 2.5f;  // room either side of the path out of a parking spot
        constexpr float pull_out_stop      = 10.0f; // meters short of the road where a parked car stops driving straight and turns in
        constexpr float junction_radius    = 10.0f; // meters, a turn at a junction is an arc a car takes without running out of steering lock
        constexpr float min_arc_turn       = 0.35f; // radians, gentler turns keep traffic's curve
        constexpr float max_arc_turn       = 2.6f;  // radians, u-turns at dead ends keep traffic's curve
        constexpr float preview_interval   = 0.5f;  // seconds between checks for moved markers in edit mode
        constexpr float preview_lift       = 0.5f;  // meters the drawn route floats above the road

        bool car_alive(Car* car)
        {
            if (!car)
            {
                return false;
            }
            const vector<Car*> cars = Car::GetAll();
            return find(cars.begin(), cars.end(), car) != cars.end();
        }

        Vector3 planar_direction(Vector3 value)
        {
            value.y = 0.0f;
            if (value.LengthSquared() > 1e-8f)
            {
                value.Normalize();
            }
            return value;
        }

        // on the ground plane, zero when a and b are parallel
        float cross_2d(const Vector3& a, const Vector3& b)
        {
            return a.x * b.z - a.z * b.x;
        }

        // a cubic that leaves one pose and arrives at the other along their tangents, the end points themselves are left to the caller
        // between parallel tangents it is a smooth lane change, between turned ones its handles are sized so it traces a circular arc
        void append_curve(road_traffic::Path& path, const road_traffic::Pose& from, const road_traffic::Pose& to)
        {
            const Vector3 tangent_from = planar_direction(from.tangent);
            const Vector3 tangent_to   = planar_direction(to.tangent);
            const float turn           = acosf(clamp(tangent_from.Dot(tangent_to), -1.0f, 1.0f));
            const float chord          = Vector3::Distance(from.position, to.position);
            const float handle         = turn > 0.01f ? chord * (2.0f / 3.0f) * tanf(turn * 0.25f) / sinf(turn * 0.5f) : chord / 3.0f;
            const Vector3 a            = from.position + tangent_from * handle;
            const Vector3 b            = to.position - tangent_to * handle;
            const uint32_t steps       = max(4u, static_cast<uint32_t>(chord));
            for (uint32_t i = 1; i < steps; i++)
            {
                const float t = static_cast<float>(i) / static_cast<float>(steps);
                const float u = 1.0f - t;
                path.Add(from.position * (u * u * u) + a * (3.0f * u * u * t) + b * (3.0f * u * t * t) + to.position * (t * t * t));
            }
        }

        // a turn at a junction, driven as lane in, lane change out to wide, arc, lane change back, lane out
        struct Turn
        {
            bool arc = false;
            road_traffic::Pose enter; // where the arc starts
            road_traffic::Pose leave; // where it ends
        };

        // the lane is centered on the traffic offset, it spans from there to the road's middle or edge, whichever is closer
        float lane_half_width(const Edge& edge)
        {
            const float offset = road_cross_section::TrafficOffset(edge.width);
            return clamp(edge.width * 0.5f - offset, 1.75f, max(offset, 1.75f));
        }

        // seen from the lane center, the whole road is drivable when getting by another car, oncoming lanes included
        RacingLine::Sample lane_sample(const Edge& edge, const Vector3& center)
        {
            const float offset = road_cross_section::TrafficOffset(edge.width);
            return { center, lane_half_width(edge), edge.width * 0.5f + offset, max(edge.width * 0.5f - offset, 0.0f) };
        }

        struct LanePoint
        {
            size_t edge    = invalid;
            float distance = 0.0f; // along the lane
        };

        // the closest point on any car lane, height counts for less so a bridge above does not win over the road below
        LanePoint nearest_lane(const Network& network, const Vector3& position)
        {
            LanePoint best;
            float best_gap = numeric_limits<float>::max();
            for (size_t i = 0; i < network.edges.size(); i++)
            {
                const Edge& edge = network.edges[i];
                if (edge.sidewalk || edge.lane.points.size() < 2)
                {
                    continue;
                }
                const float distance = edge.lane.Project(position, 0.0f, edge.lane.Length());
                Vector3 offset       = edge.lane.Sample(distance).position - position;
                offset.y            *= 0.25f;
                const float gap      = offset.LengthSquared();
                if (gap < best_gap)
                {
                    best_gap = gap;
                    best     = { i, distance };
                }
            }
            return best;
        }

        // a point on a road can be driven either way, both lanes are candidates
        vector<LanePoint> both_directions(const Network& network, const LanePoint& point, const Vector3& position)
        {
            vector<LanePoint> points;
            if (point.edge == invalid)
            {
                return points;
            }
            points.push_back(point);
            const size_t reverse = network.edges[point.edge].reverse;
            if (reverse < network.edges.size())
            {
                const Edge& edge = network.edges[reverse];
                points.push_back({ reverse, edge.lane.Project(position, 0.0f, edge.lane.Length()) });
            }
            return points;
        }

        // shortest chain of lanes from a point on one lane to the closest of the finish points, junction curves included
        // u-turns only happen at dead ends, the same rule traffic follows
        float shortest_chain(const Network& network, const LanePoint& from, const vector<LanePoint>& finishes, vector<size_t>& chain, size_t& finish)
        {
            float best = numeric_limits<float>::max();
            for (size_t k = 0; k < finishes.size(); k++)
            {
                if (finishes[k].edge == from.edge && finishes[k].distance >= from.distance && finishes[k].distance - from.distance < best)
                {
                    best   = finishes[k].distance - from.distance;
                    chain  = { from.edge };
                    finish = k;
                }
            }

            const size_t count = network.edges.size();
            vector<float> driven(count, numeric_limits<float>::max()); // meters from the start to the end of each lane
            vector<size_t> previous(count, invalid);
            using Entry = pair<float, size_t>;
            priority_queue<Entry, vector<Entry>, greater<Entry>> open;
            driven[from.edge] = network.edges[from.edge].lane.Length() - from.distance;
            open.push({ driven[from.edge], from.edge });

            size_t best_last = invalid;
            size_t best_k    = 0;
            while (!open.empty())
            {
                const auto [meters, e] = open.top();
                open.pop();
                if (meters >= best)
                {
                    break;
                }
                if (meters > driven[e])
                {
                    continue;
                }

                const Edge& edge = network.edges[e];
                bool dead_end    = true;
                for (size_t next : network.nodes[edge.to].exits)
                {
                    dead_end = dead_end && next == edge.reverse;
                }
                for (size_t next : network.nodes[edge.to].exits)
                {
                    const Edge& lane = network.edges[next];
                    if ((next == edge.reverse && !dead_end) || lane.sidewalk || lane.lane.points.empty())
                    {
                        continue;
                    }
                    const float arrive = meters + Vector3::Distance(edge.lane.points.back(), lane.lane.points.front());
                    for (size_t k = 0; k < finishes.size(); k++)
                    {
                        if (finishes[k].edge == next && arrive + finishes[k].distance < best)
                        {
                            best      = arrive + finishes[k].distance;
                            best_last = e;
                            best_k    = k;
                        }
                    }
                    const float through = arrive + lane.lane.Length();
                    if (through < driven[next])
                    {
                        driven[next]   = through;
                        previous[next] = e;
                        open.push({ through, next });
                    }
                }
            }

            if (best_last != invalid)
            {
                chain.clear();
                for (size_t e = best_last; e != invalid && chain.size() <= count; e = previous[e])
                {
                    chain.push_back(e);
                    if (e == from.edge)
                    {
                        break;
                    }
                }
                reverse(chain.begin(), chain.end());
                chain.push_back(finishes[best_k].edge);
                finish = best_k;
            }
            return best;
        }
    }

    RouteDriver::RouteDriver(Entity* entity) : Component(entity)
    {
        SP_REGISTER_ATTRIBUTE_VALUE_VALUE(m_start_entity_id, uint64_t);
        SP_REGISTER_ATTRIBUTE_VALUE_VALUE(m_end_entity_id, uint64_t);
        SP_REGISTER_ATTRIBUTE_VALUE_VALUE(m_skill, float);
        SP_REGISTER_ATTRIBUTE_VALUE_VALUE(m_max_speed, float);
        SP_REGISTER_ATTRIBUTE_VALUE_VALUE(m_edge_margin, float);
        SP_REGISTER_ATTRIBUTE_VALUE_VALUE(m_start_delay, float);
        SP_REGISTER_ATTRIBUTE_VALUE_VALUE(m_verbose, bool);
        SP_REGISTER_ATTRIBUTE_VALUE_VALUE(m_route_length, float);
        SP_REGISTER_ATTRIBUTE_VALUE_VALUE(m_status, string);
        SP_REGISTER_ROUTE_STAT(finished, bool);
        SP_REGISTER_ROUTE_STAT(resets, uint32_t);
        SP_REGISTER_ROUTE_STAT(lap_time, float);
        SP_REGISTER_ROUTE_STAT(last_lap_time, float);
        SP_REGISTER_ROUTE_STAT(speed_kmh, float);
        SP_REGISTER_ROUTE_STAT(target_kmh, float);
        SP_REGISTER_ROUTE_STAT(distance, float);
        SP_REGISTER_ROUTE_STAT(travelled, float);
        SP_REGISTER_ROUTE_STAT(line_error, float);
        SP_REGISTER_ROUTE_STAT(ideal_lap, float);
        SP_REGISTER_ROUTE_STAT(following, bool);
        SP_REGISTER_ROUTE_STAT(passing, bool);
    }

    RouteDriver::~RouteDriver()
    {
        // entities are deleted under the world entity lock, so nothing here may call into World, Stop/Remove hand the car back
        m_car = nullptr;
        m_line.reset();
    }

    void RouteDriver::Remove()
    {
        Stop();
    }

    Car* RouteDriver::GetCar() const
    {
        // a car prefab builds its car under this entity
        for (Car* car : Car::GetAll())
        {
            Entity* root = car ? car->GetRootEntity() : nullptr;
            if (root && (root == GetEntity() || root->GetParent() == GetEntity()))
            {
                return car;
            }
        }
        return nullptr;
    }

    void RouteDriver::Start()
    {
        m_stats      = AiDriverStats();
        m_launched   = false;
        m_line.reset();
        m_car        = nullptr;
        m_retry_time = 0.2f; // the car's own play start (spawn reset, physics) goes first
        m_status     = "starting";
        EnsureNetwork(true);
    }

    void RouteDriver::Stop()
    {
        if (car_alive(m_car) && m_car->GetAiDriver() && m_car->GetAiDriver()->GetLine() == m_line)
        {
            m_car->SetAiDriver(nullptr);
        }
        if (m_line)
        {
            m_line->RestoreCollisionStreaming();
        }
        m_line.reset();
        m_car      = nullptr;
        m_launched = false;
        m_status   = "idle";
    }

    void RouteDriver::Tick()
    {
        SP_PROFILE_CPU();
        if (!Engine::IsFlagSet(EngineMode::Playing))
        {
            TickPreview();
            return;
        }
        if (Engine::IsFlagSet(EngineMode::Paused))
        {
            return;
        }

        if (!m_launched)
        {
            m_retry_time -= static_cast<float>(Timer::GetDeltaTimeSec());
            if (m_retry_time > 0.0f)
            {
                return;
            }
            Car* car         = GetCar();
            Entity* vehicle  = car ? car->GetRootEntity() : nullptr;
            Physics* physics = vehicle ? vehicle->GetComponent<Physics>() : nullptr;
            if (!physics || !physics->GetVehicleSimulation())
            {
                m_status     = car ? "waiting for the car's physics" : "no drivable car on this entity";
                m_retry_time = 1.0f;
                return;
            }
            Launch(car);
            return;
        }

        const AiDriver* driver = car_alive(m_car) ? m_car->GetAiDriver() : nullptr;
        if (driver && driver->GetLine() == m_line)
        {
            m_stats  = driver->GetStats();
            m_status = m_stats.finished ? "finished" : (driver->IsHolding() ? "waiting to start" : "driving");
        }
        else
        {
            m_status = "handed back";
        }
    }

    bool RouteDriver::EnsureNetwork(bool rebuild)
    {
        if (!m_network || m_network->edges.empty() || rebuild)
        {
            if (ProgressTracker::IsLoading())
            {
                return false;
            }
            m_network = make_shared<Network>(road_traffic::BuildWorldNetwork());
        }
        return !m_network->edges.empty();
    }

    bool RouteDriver::BuildRoute(Car* car, Route& route)
    {
        route = Route();
        Entity* end_entity   = m_end_entity_id != 0 ? World::GetEntityById(m_end_entity_id) : nullptr;
        Entity* start_entity = m_start_entity_id != 0 ? World::GetEntityById(m_start_entity_id) : nullptr;
        Entity* vehicle      = car ? car->GetRootEntity() : nullptr;
        if (!end_entity)
        {
            m_status = "no finish, set end_entity_id to an entity on a road";
            return false;
        }
        if (!start_entity && !vehicle)
        {
            m_status = "no start and no car to start from";
            return false;
        }
        if (!EnsureNetwork(false))
        {
            m_status = "no road network";
            return false;
        }
        const Network& network = *m_network;

        const Vector3 end_position = end_entity->GetPosition();
        const vector<LanePoint> finishes = both_directions(network, nearest_lane(network, end_position), end_position);

        // a parked car drives straight out the way it faces until it is close to the road, then merges into the lane a little further along
        route.from_car               = start_entity == nullptr;
        const Vector3 start_position = route.from_car ? vehicle->GetPosition() : start_entity->GetPosition();
        const Vector3 start_forward  = route.from_car ? planar_direction(vehicle->GetForward()) : Vector3::Zero;
        const LanePoint nearest      = nearest_lane(network, start_position);
        vector<LanePoint> starts     = both_directions(network, nearest, start_position);
        if (starts.empty() || finishes.empty())
        {
            m_status = "no road near the start or the finish";
            return false;
        }
        Vector3 corner = start_position;
        if (route.from_car)
        {
            const float ahead = (network.edges[nearest.edge].lane.Sample(nearest.distance).position - start_position).Dot(start_forward);
            if (ahead > pull_out_stop + 2.0f)
            {
                corner = start_position + start_forward * (ahead - pull_out_stop);
            }
        }

        float best          = numeric_limits<float>::max();
        LanePoint best_start;
        vector<size_t> best_chain;
        size_t best_finish  = 0;
        for (LanePoint start : starts)
        {
            const Edge& edge = network.edges[start.edge];
            float lead_in    = 0.0f;
            if (route.from_car)
            {
                const float from = edge.lane.Project(corner, 0.0f, edge.lane.Length());
                const float gap  = Vector3::Distance(edge.lane.Sample(from).position, corner);
                start.distance   = min(from + clamp(gap * 1.2f, 8.0f, 40.0f), max(edge.lane.Length() - 1.0f, from));
                const road_traffic::Pose join = edge.lane.Sample(start.distance);
                // a join that points back at the car means a hairpin out of the driveway
                lead_in = Vector3::Distance(corner, start_position) + Vector3::Distance(join.position, corner) + (1.0f - start_forward.Dot(planar_direction(join.tangent))) * 25.0f;
            }
            vector<size_t> chain;
            size_t finish      = 0;
            const float length = shortest_chain(network, start, finishes, chain, finish);
            if (length < numeric_limits<float>::max() && length + lead_in < best)
            {
                best        = length + lead_in;
                best_start  = start;
                best_chain  = chain;
                best_finish = finish;
            }
        }
        if (best_chain.empty())
        {
            m_status = "no route along the roads from the start to the finish";
            return false;
        }

        road_traffic::Path path;
        auto add = [&](const RacingLine::Sample& sample)
        {
            const size_t before = path.points.size();
            path.Add(sample.center);
            if (path.points.size() > before)
            {
                route.samples.push_back(sample);
            }
        };
        auto add_lane = [&](size_t e, float from, float to)
        {
            const Edge& edge = network.edges[e];
            add(lane_sample(edge, edge.lane.Sample(from).position));
            for (size_t i = 0; i < edge.lane.points.size(); i++)
            {
                if (edge.lane.distances[i] > from && edge.lane.distances[i] < to)
                {
                    add(lane_sample(edge, edge.lane.points[i]));
                }
            }
            add(lane_sample(edge, edge.lane.Sample(to).position));
            if (find(route.roads.begin(), route.roads.end(), edge.road) == route.roads.end())
            {
                route.roads.push_back(edge.road);
            }
        };

        if (route.from_car)
        {
            const float straight = Vector3::Distance(start_position, corner);
            for (float along = 0.0f; along < straight; along += 2.0f)
            {
                add({ start_position + start_forward * along, lead_in_half_width, lead_in_half_width, lead_in_half_width });
            }
            const road_traffic::Pose join = network.edges[best_start.edge].lane.Sample(best_start.distance);
            const Vector3 tangent         = planar_direction(join.tangent);
            const float reach             = Vector3::Distance(corner, join.position);
            const Vector3 a               = corner + start_forward * reach * 0.45f;
            const Vector3 b               = join.position - tangent * reach * 0.45f;
            const uint32_t steps          = max(8u, static_cast<uint32_t>(reach));
            for (uint32_t i = 0; i < steps; i++)
            {
                const float t = static_cast<float>(i) / static_cast<float>(steps);
                const float u = 1.0f - t;
                add({ corner * (u * u * u) + a * (3.0f * u * u * t) + b * (3.0f * u * t * t) + join.position * (t * t * t), lead_in_half_width, lead_in_half_width, lead_in_half_width });
            }
        }

        // traffic joins lanes where they end, at a tight corner that is a turn no car makes, so a turn becomes an arc tangent to both lanes
        // a turn across traffic has the junction on its inside and cuts the corner, a turn toward the curb keeps its inside on the lane
        // corner and swings out wide to fit the arc, the outside of a road is where the other lanes are
        const LanePoint& finish = finishes[best_finish];
        const size_t links      = best_chain.size();
        vector<float> from(links, 0.0f);
        vector<float> to(links, 0.0f);
        vector<Turn> turns(links);
        for (size_t i = 0; i < links; i++)
        {
            from[i] = i == 0 ? best_start.distance : 0.0f;
            to[i]   = i + 1 == links ? finish.distance : network.edges[best_chain[i]].lane.Length();
        }
        for (size_t i = 1; i < links; i++)
        {
            const road_traffic::Pose end   = network.edges[best_chain[i - 1]].lane.Sample(to[i - 1]);
            const road_traffic::Pose begin = network.edges[best_chain[i]].lane.Sample(from[i]);
            const Vector3 tangent_in       = planar_direction(end.tangent);
            const Vector3 tangent_out      = planar_direction(begin.tangent);
            const float turn               = acosf(clamp(tangent_in.Dot(tangent_out), -1.0f, 1.0f));
            const float det                = cross_2d(tangent_in, tangent_out);
            if (turn < min_arc_turn || turn > max_arc_turn || fabsf(det) < 1e-3f)
            {
                continue;
            }

            // the lanes carried on straight cross this far past the lane in's end and this far before the lane out's start
            const Vector3 gap   = begin.position - end.position;
            const float past_in = cross_2d(gap, tangent_out) / det;
            const float to_out  = -cross_2d(gap, tangent_in) / det;
            if (past_in < -2.0f || to_out < -2.0f)
            {
                continue;
            }

            // traffic keeps right, so a right turn is the one toward the curb
            const bool toward_curb    = det < 0.0f;
            const float wide          = toward_curb ? junction_radius * (1.0f - cosf(turn * 0.5f)) : 0.0f;
            const float tangent       = (junction_radius - wide) * tanf(turn * 0.5f);
            const float change        = wide > 0.01f ? max(8.0f, wide * 5.0f) : 0.0f;
            const float available_in  = to[i - 1] - from[i - 1] - 1.0f;
            const float available_out = to[i] - from[i] - 1.0f;
            if (tangent - past_in > available_in || tangent - to_out > available_out)
            {
                continue;
            }
            const Vector3 outside_in  = planar_direction(tangent_in * tangent_in.Dot(tangent_out) - tangent_out);
            const Vector3 outside_out = planar_direction(tangent_in - tangent_out * tangent_in.Dot(tangent_out));
            turns[i].arc   = true;
            turns[i].enter = { end.position + tangent_in * (past_in - tangent) + outside_in * wide, tangent_in };
            turns[i].leave = { begin.position + tangent_out * (tangent - to_out) + outside_out * wide, tangent_out };
            to[i - 1]     -= clamp(tangent + change - past_in, 0.0f, max(available_in, 0.0f));
            from[i]       += clamp(tangent + change - to_out, 0.0f, max(available_out, 0.0f));
        }

        for (size_t i = 0; i < links; i++)
        {
            const size_t e = best_chain[i];
            if (i > 0)
            {
                road_traffic::Path curve;
                if (turns[i].arc)
                {
                    const road_traffic::Pose lane_in  = network.edges[best_chain[i - 1]].lane.Sample(to[i - 1]);
                    const road_traffic::Pose lane_out = network.edges[e].lane.Sample(from[i]);
                    append_curve(curve, { lane_in.position, planar_direction(lane_in.tangent) }, turns[i].enter);
                    curve.Add(turns[i].enter.position);
                    append_curve(curve, turns[i].enter, turns[i].leave);
                    curve.Add(turns[i].leave.position);
                    append_curve(curve, turns[i].leave, { lane_out.position, planar_direction(lane_out.tangent) });
                }
                else
                {
                    network.AppendJunction(curve, best_chain[i - 1], e);
                }
                const RacingLine::Sample in  = lane_sample(network.edges[best_chain[i - 1]], Vector3::Zero);
                const RacingLine::Sample out = lane_sample(network.edges[e], Vector3::Zero);
                for (const Vector3& point : curve.points)
                {
                    add({ point, min(in.half_width, out.half_width), min(in.room_left, out.room_left), min(in.room_right, out.room_right) });
                }
            }
            add_lane(e, from[i], to[i]);
        }

        if (route.samples.size() < 2)
        {
            m_status = "the start and the finish are the same place";
            return false;
        }
        return true;
    }

    void RouteDriver::Launch(Car* car)
    {
        Route route;
        m_line = BuildRoute(car, route) ? RacingLine::BuildOpen(route.samples, route.roads, m_edge_margin) : nullptr;
        if (!m_line)
        {
            SP_LOG_ERROR("route_driver: %s, trying again in 5 s", m_status.c_str());
            m_retry_time = 5.0f;
            return;
        }
        m_route_length = m_line->GetLength();

        // a start on the road is a grid slot, the car is put there facing the route
        if (!route.from_car)
        {
            car->PlaceAt(m_line->GetPoint(0).position, Quaternion::FromLookRotation(m_line->DirectionAt(0), Vector3::Up));
        }

        AiDriverSettings settings;
        settings.skill        = m_skill;
        settings.max_speed    = m_max_speed;
        settings.launch_delay = m_start_delay;
        settings.learning     = true;
        settings.verbose      = m_verbose;
        car->SetAiDriver(make_unique<AiDriver>(m_line, settings));
        m_car      = car;
        m_launched = true;
        m_status   = "waiting to start";
        SP_LOG_INFO("route_driver: %s drives %.0f m over %zu roads%s", car->GetDisplayName().c_str(), m_route_length, route.roads.size(), route.from_car ? ", pulling out from where it is parked" : "");
    }

    void RouteDriver::TickPreview()
    {
        if (!Engine::IsFlagSet(EngineMode::EditorVisible) || ProgressTracker::IsLoading())
        {
            return;
        }

        m_preview_time -= static_cast<float>(Timer::GetDeltaTimeSec());
        if (m_preview_time <= 0.0f)
        {
            m_preview_time        = preview_interval;
            Car* car              = GetCar();
            Entity* vehicle       = car ? car->GetRootEntity() : nullptr;
            Entity* start_entity  = m_start_entity_id != 0 ? World::GetEntityById(m_start_entity_id) : nullptr;
            Entity* end_entity    = m_end_entity_id != 0 ? World::GetEntityById(m_end_entity_id) : nullptr;
            const Vector3 key[3]  =
            {
                vehicle ? vehicle->GetPosition() : GetEntity()->GetPosition(),
                start_entity ? start_entity->GetPosition() : Vector3::Zero,
                end_entity ? end_entity->GetPosition() : Vector3::Zero
            };
            bool moved = m_preview.empty();
            for (uint32_t i = 0; i < 3; i++)
            {
                moved = moved || Vector3::Distance(key[i], m_preview_key[i]) > 0.1f;
            }
            if (moved && end_entity)
            {
                Route route;
                m_preview.clear();
                const shared_ptr<RacingLine> line = BuildRoute(car, route) ? RacingLine::BuildOpen(route.samples, route.roads, m_edge_margin) : nullptr;
                for (size_t i = 0; line && i < line->GetCount(); i++)
                {
                    m_preview.push_back(line->GetPoint(i).position);
                }
                for (uint32_t i = 0; i < 3; i++)
                {
                    m_preview_key[i] = key[i];
                }
                // an empty network usually means roads were still being generated
                if (m_preview.empty())
                {
                    m_preview_time = 2.0f;
                }
            }
        }

        const Color route_color = Color(1.0f, 0.75f, 0.1f, 1.0f);
        const Vector3 lift      = Vector3(0.0f, preview_lift, 0.0f);
        for (size_t i = 1; i < m_preview.size(); i++)
        {
            Renderer::DrawLine(m_preview[i - 1] + lift, m_preview[i] + lift, route_color, route_color);
        }
        if (!m_preview.empty())
        {
            const Color start_color = Color(0.2f, 0.9f, 0.3f, 1.0f);
            const Color end_color   = Color(0.95f, 0.2f, 0.2f, 1.0f);
            Renderer::DrawLine(m_preview.front(), m_preview.front() + Vector3(0.0f, 4.0f, 0.0f), start_color, start_color);
            Renderer::DrawLine(m_preview.back(), m_preview.back() + Vector3(0.0f, 4.0f, 0.0f), end_color, end_color);
            Renderer::DrawCircle(m_preview.back() + lift, Vector3::Up, 2.0f, 24, end_color);
        }
    }

    void RouteDriver::Save(pugi::xml_node& node)
    {
        node.append_attribute("start_entity_id") = m_start_entity_id;
        node.append_attribute("end_entity_id")   = m_end_entity_id;
        node.append_attribute("skill")           = m_skill;
        node.append_attribute("max_speed")       = m_max_speed;
        node.append_attribute("edge_margin")     = m_edge_margin;
        node.append_attribute("start_delay")     = m_start_delay;
        node.append_attribute("verbose")         = m_verbose;
    }

    void RouteDriver::Load(pugi::xml_node& node)
    {
        m_start_entity_id = node.attribute("start_entity_id").as_ullong(m_start_entity_id);
        m_end_entity_id   = node.attribute("end_entity_id").as_ullong(m_end_entity_id);
        m_skill           = clamp(node.attribute("skill").as_float(m_skill), 0.0f, 1.0f);
        m_max_speed       = clamp(node.attribute("max_speed").as_float(m_max_speed), 5.0f, 150.0f);
        m_edge_margin     = max(node.attribute("edge_margin").as_float(m_edge_margin), 0.0f);
        m_start_delay     = max(node.attribute("start_delay").as_float(m_start_delay), 0.0f);
        m_verbose         = node.attribute("verbose").as_bool(m_verbose);
    }
}
