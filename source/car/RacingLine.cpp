/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

#include "pch.h"
#include "RacingLine.h"
#include "../world/Entity.h"
#include "../world/World.h"
#include "../world/components/Physics.h"
#include "../world/components/Spline.h"

using namespace std;
using namespace spartan::math;

namespace spartan
{
    namespace
    {
        constexpr float point_spacing   = 2.0f; // meters between racing line points
        constexpr float curvature_reach = 8.0f; // meters on each side the curvature is measured over

        Vector3 planar(Vector3 value)
        {
            value.y = 0.0f;
            return value;
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
    }

    shared_ptr<RacingLine> RacingLine::Build(Entity* spline_entity, float edge_margin)
    {
        Spline* spline = spline_entity ? spline_entity->GetComponent<Spline>() : nullptr;
        if (!spline || spline->GetControlPointCount() < 3)
        {
            SP_LOG_ERROR("racing_line: '%s' needs a spline road with at least three control points", spline_entity ? spline_entity->GetObjectName().c_str() : "null");
            return nullptr;
        }
        if (!spline->GetClosedLoop())
        {
            SP_LOG_WARNING("racing_line: '%s' is not a closed loop, cars will race back to the start across the gap", spline_entity->GetObjectName().c_str());
        }

        // the generated deck is what the wheels touch, control points can sit off a road that conforms to terrain
        vector<Vector3> dense;
        vector<float> dense_t;
        const vector<SplineFrame>& frames = spline->GetRoadFrames();
        if (frames.size() >= 3)
        {
            const Matrix world_matrix = spline_entity->GetMatrix();
            for (const SplineFrame& frame : frames)
            {
                dense.push_back(world_matrix * frame.position);
                dense_t.push_back(frame.t);
            }
        }
        else
        {
            const float length   = spline->GetLength(20);
            const uint32_t count = clamp(static_cast<uint32_t>(length / 0.5f), 16u, 200000u);
            for (uint32_t i = 0; i <= count; i++)
            {
                const float t = static_cast<float>(i) / static_cast<float>(count);
                dense.push_back(spline->GetPoint(t));
                dense_t.push_back(t);
            }
        }

        // a closed loop repeats its first point at the end
        while (dense.size() > 3 && Vector3::Distance(dense.back(), dense.front()) < 0.25f)
        {
            dense.pop_back();
            dense_t.pop_back();
        }

        // resample the closed polyline to even spacing so indices are meters
        float total = 0.0f;
        vector<float> dense_distance(dense.size() + 1, 0.0f);
        for (size_t i = 0; i < dense.size(); i++)
        {
            total                 += Vector3::Distance(dense[i], dense[(i + 1) % dense.size()]);
            dense_distance[i + 1]  = total;
        }
        const size_t count = static_cast<size_t>(total / point_spacing);
        if (count < 32)
        {
            SP_LOG_ERROR("racing_line: '%s' is too short (%.0f m)", spline_entity->GetObjectName().c_str(), total);
            return nullptr;
        }
        const float spacing = total / static_cast<float>(count);

        shared_ptr<RacingLine> line = make_shared<RacingLine>();
        line->m_road_entity_ids.push_back(spline_entity->GetObjectId());
        line->m_points.resize(count);

        const float width_start = spline->GetRoadWidth();
        const float width_end   = spline->GetRoadWidthEnd();
        size_t segment          = 0;
        for (size_t i = 0; i < count; i++)
        {
            const float distance = spacing * static_cast<float>(i);
            while (segment + 1 < dense.size() && dense_distance[segment + 1] < distance)
            {
                segment++;
            }
            const float span     = max(dense_distance[segment + 1] - dense_distance[segment], 1e-4f);
            const float fraction = (distance - dense_distance[segment]) / span;
            const size_t next    = (segment + 1) % dense.size();
            const float t        = dense_t[segment] + (dense_t[next] - dense_t[segment]) * fraction;

            Point& point     = line->m_points[i];
            point.center     = Vector3::Lerp(dense[segment], dense[next], fraction);
            point.half_width = (width_start + (width_end - width_start) * clamp(t, 0.0f, 1.0f)) * 0.5f;
            point.room_left  = point.half_width;
            point.room_right = point.half_width;
        }

        line->Finish(edge_margin);
        SP_LOG_INFO("racing_line: '%s' %.0f m, %zu points", spline_entity->GetObjectName().c_str(), line->m_length, count);
        return line;
    }

    shared_ptr<RacingLine> RacingLine::BuildOpen(const vector<Sample>& samples, const vector<uint64_t>& roads, float edge_margin)
    {
        if (samples.size() < 2)
        {
            SP_LOG_ERROR("racing_line: a route needs at least two points");
            return nullptr;
        }

        float total = 0.0f;
        vector<float> distances(samples.size(), 0.0f);
        for (size_t i = 1; i < samples.size(); i++)
        {
            total        += Vector3::Distance(samples[i - 1].center, samples[i].center);
            distances[i]  = total;
        }
        const size_t count = static_cast<size_t>(total / point_spacing) + 1;
        if (count < 8)
        {
            SP_LOG_ERROR("racing_line: the route is too short (%.0f m)", total);
            return nullptr;
        }
        const float spacing = total / static_cast<float>(count - 1);

        shared_ptr<RacingLine> line = make_shared<RacingLine>();
        line->m_closed              = false;
        line->m_road_entity_ids     = roads;
        line->m_points.resize(count);
        size_t segment = 0;
        for (size_t i = 0; i < count; i++)
        {
            const float distance = spacing * static_cast<float>(i);
            while (segment + 2 < samples.size() && distances[segment + 1] < distance)
            {
                segment++;
            }
            const float span     = max(distances[segment + 1] - distances[segment], 1e-4f);
            const float fraction = clamp((distance - distances[segment]) / span, 0.0f, 1.0f);
            const Sample& a      = samples[segment];
            const Sample& b      = samples[segment + 1];
            Point& point         = line->m_points[i];
            point.center         = Vector3::Lerp(a.center, b.center, fraction);
            point.half_width     = a.half_width + (b.half_width - a.half_width) * fraction;
            point.room_left      = max(a.room_left + (b.room_left - a.room_left) * fraction, point.half_width);
            point.room_right     = max(a.room_right + (b.room_right - a.room_right) * fraction, point.half_width);
        }

        line->Finish(edge_margin);
        SP_LOG_INFO("racing_line: route %.0f m, %zu points, %zu roads", line->m_length, count, roads.size());
        return line;
    }

    void RacingLine::Finish(float edge_margin)
    {
        for (size_t i = 0; i < m_points.size(); i++)
        {
            const Vector3 tangent = planar_direction(m_points[Wrap(static_cast<int64_t>(i) + 1)].center - m_points[Wrap(static_cast<int64_t>(i) - 1)].center);
            m_points[i].right     = Vector3::Up.Cross(tangent);
        }
        BuildLine(edge_margin);
    }

    void RacingLine::BuildLine(float edge_margin)
    {
        // elastic band: every point is pulled toward the chord between its neighbours and held inside the road
        // coarse chords shape the whole corner (outside, apex, outside), fine ones smooth it, the result approaches the minimum curvature line
        const size_t count = m_points.size();
        for (Point& point : m_points)
        {
            point.offset   = 0.0f;
            point.position = point.center;
        }

        const int64_t reaches[] = { 64, 32, 16, 8, 4, 2, 1 };
        for (int64_t reach : reaches)
        {
            if (reach * 4 > static_cast<int64_t>(count))
            {
                continue;
            }
            for (uint32_t iteration = 0; iteration < 100; iteration++)
            {
                // an open line keeps its start and finish where they were asked for
                const size_t first = m_closed ? 0 : 1;
                const size_t last  = m_closed ? count : count - 1;
                for (size_t i = first; i < last; i++)
                {
                    Point& point         = m_points[i];
                    const Vector3& back  = m_points[Wrap(static_cast<int64_t>(i) - reach)].position;
                    const Vector3& ahead = m_points[Wrap(static_cast<int64_t>(i) + reach)].position;
                    const Vector3 chord  = (back + ahead) * 0.5f;
                    const float limit    = max(point.half_width - edge_margin, 0.0f);
                    const float pull     = planar(chord - point.position).Dot(point.right);
                    point.offset         = clamp(point.offset + pull * 0.6f, -limit, limit);
                    point.position       = point.center + point.right * point.offset;
                }
            }
        }

        float distance = 0.0f;
        for (size_t i = 0; i < count; i++)
        {
            Point& point      = m_points[i];
            point.distance    = distance;
            point.step        = Vector3::Distance(point.position, m_points[Wrap(static_cast<int64_t>(i) + 1)].position);
            point.line_right  = Vector3::Up.Cross(planar_direction(m_points[Wrap(static_cast<int64_t>(i) + 1)].position - m_points[Wrap(static_cast<int64_t>(i) - 1)].position));
            distance         += point.step;
        }
        m_length = distance;

        // signed curvature through three points, positive when the line bends right
        const int64_t reach = max(static_cast<int64_t>(curvature_reach / point_spacing), int64_t(1));
        vector<float> curvature(count, 0.0f);
        for (size_t i = 0; i < count; i++)
        {
            const Vector3 a  = planar(m_points[Wrap(static_cast<int64_t>(i) - reach)].position);
            const Vector3 b  = planar(m_points[i].position);
            const Vector3 c  = planar(m_points[Wrap(static_cast<int64_t>(i) + reach)].position);
            const float ab   = Vector3::Distance(a, b);
            const float bc   = Vector3::Distance(b, c);
            const float ac   = Vector3::Distance(a, c);
            const float turn = (b - a).Cross(c - b).y;
            curvature[i]     = ab * bc * ac > 1e-6f ? 2.0f * turn / (ab * bc * ac) : 0.0f;
        }
        for (size_t i = 0; i < count; i++)
        {
            float sum = 0.0f;
            for (int64_t k = -2; k <= 2; k++)
            {
                sum += curvature[Wrap(static_cast<int64_t>(i) + k)];
            }
            m_points[i].curvature = sum / 5.0f;
        }
    }

    size_t RacingLine::Wrap(int64_t index) const
    {
        const int64_t count = static_cast<int64_t>(m_points.size());
        if (!m_closed)
        {
            return static_cast<size_t>(clamp(index, int64_t(0), count - 1));
        }
        return static_cast<size_t>(((index % count) + count) % count);
    }

    size_t RacingLine::IndexAt(float distance, float& fraction) const
    {
        if (m_closed)
        {
            distance = fmodf(distance, m_length);
            if (distance < 0.0f)
            {
                distance += m_length;
            }
        }
        else
        {
            distance = clamp(distance, 0.0f, m_length);
        }
        const auto it  = upper_bound(m_points.begin(), m_points.end(), distance, [](float value, const Point& point) { return value < point.distance; });
        const size_t i = it == m_points.begin() ? 0 : static_cast<size_t>(it - m_points.begin()) - 1;
        fraction       = clamp((distance - m_points[i].distance) / max(m_points[i].step, 1e-4f), 0.0f, 1.0f);
        return i;
    }

    Vector3 RacingLine::PositionAt(float distance) const
    {
        float fraction = 0.0f;
        const size_t i = IndexAt(distance, fraction);
        return Vector3::Lerp(m_points[i].position, m_points[Wrap(static_cast<int64_t>(i) + 1)].position, fraction);
    }

    float RacingLine::CurvatureAt(float distance) const
    {
        float fraction = 0.0f;
        const size_t i = IndexAt(distance, fraction);
        return m_points[i].curvature + (m_points[Wrap(static_cast<int64_t>(i) + 1)].curvature - m_points[i].curvature) * fraction;
    }

    Vector3 RacingLine::DirectionAt(size_t index) const
    {
        // the finish of an open line has nothing ahead, it keeps the direction it arrived with
        const int64_t ahead = static_cast<int64_t>(index) + 2;
        if (!m_closed && ahead >= static_cast<int64_t>(m_points.size()))
        {
            return planar_direction(m_points[index].position - m_points[Wrap(static_cast<int64_t>(index) - 2)].position);
        }
        return planar_direction(m_points[Wrap(ahead)].position - m_points[index].position);
    }

    RacingLine::Projection RacingLine::Project(const Vector3& position, size_t hint, bool full_search) const
    {
        const int64_t count = static_cast<int64_t>(m_points.size());
        int64_t from        = full_search ? 0 : static_cast<int64_t>(hint) - 8;
        int64_t to          = full_search ? count : static_cast<int64_t>(hint) + 60;
        if (!m_closed)
        {
            from = max(from, int64_t(0));
            to   = min(to, count - 1);
        }
        const Vector3 p     = planar(position);

        float best_distance = numeric_limits<float>::max();
        size_t best_index   = Wrap(static_cast<int64_t>(hint));
        float best_t        = 0.0f;
        for (int64_t j = from; j < to; j++)
        {
            const size_t i       = Wrap(j);
            const Vector3 a      = planar(m_points[i].position);
            const Vector3 ab     = planar(m_points[Wrap(j + 1)].position) - a;
            const float t        = clamp((p - a).Dot(ab) / max(ab.LengthSquared(), 1e-6f), 0.0f, 1.0f);
            const float distance = (a + ab * t - p).LengthSquared();
            if (distance < best_distance)
            {
                best_distance = distance;
                best_index    = i;
                best_t        = t;
            }
        }

        const Point& point = m_points[best_index];
        Projection projection;
        projection.index         = best_index;
        projection.distance      = point.distance + point.step * best_t;
        projection.line_error    = planar(position - point.position).Dot(point.line_right);
        projection.center_offset = planar(position - point.center).Dot(point.right);
        return projection;
    }

    void RacingLine::KeepCollisionLoaded() const
    {
        // the road mesh rebuilds its physics component on edits, so this is asked again every frame
        for (uint64_t id : m_road_entity_ids)
        {
            Entity* entity = World::GetEntityById(id);
            if (Physics* physics = entity ? entity->GetComponent<Physics>() : nullptr; physics && physics->GetDistanceStreaming())
            {
                physics->SetDistanceStreaming(false);
            }
        }
    }

    void RacingLine::RestoreCollisionStreaming() const
    {
        for (uint64_t id : m_road_entity_ids)
        {
            Entity* entity = World::GetEntityById(id);
            if (Physics* physics = entity ? entity->GetComponent<Physics>() : nullptr; physics && !physics->GetDistanceStreaming())
            {
                physics->SetDistanceStreaming(true);
            }
        }
    }
}
