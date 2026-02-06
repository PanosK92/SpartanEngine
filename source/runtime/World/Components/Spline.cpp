/*
Copyright(c) 2015-2026 Panos Karabelas

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and / or sell
copies of the Software, and to permit persons to whom the Software is furnished
to do so, subject to the following conditions :

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.IN NO EVENT SHALL THE AUTHORS OR
COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/

//= INCLUDES ========================
#include "pch.h"
#include "Spline.h"
#include "../Entity.h"
#include "../World.h"
#include "../../Core/Engine.h"
#include "../../Rendering/Renderer.h"
SP_WARNINGS_OFF
#include "../../IO/pugixml.hpp"
SP_WARNINGS_ON
//===================================

//= NAMESPACES ===============
using namespace std;
using namespace spartan::math;
//============================

namespace spartan
{
    Spline::Spline(Entity* entity) : Component(entity)
    {

    }

    void Spline::Tick()
    {
        // only draw spline visualization in edit mode
        if (Engine::IsFlagSet(EngineMode::Playing))
            return;

        vector<Vector3> points = GetControlPoints();
        if (points.size() < 2)
            return;

        const Color color_curve = Color(0.3f, 0.85f, 0.75f, 1.0f);
        const Color color_point = Color(1.0f, 0.8f, 0.3f, 1.0f);

        // draw the interpolated curve
        uint32_t span_count = m_closed_loop ? static_cast<uint32_t>(points.size()) : static_cast<uint32_t>(points.size()) - 1;
        for (uint32_t span = 0; span < span_count; span++)
        {
            Vector3 prev_point;
            for (uint32_t seg = 0; seg <= m_resolution; seg++)
            {
                float local_t = static_cast<float>(seg) / static_cast<float>(m_resolution);

                // determine the four control points for this span
                int32_t point_count = static_cast<int32_t>(points.size());
                int32_t i1 = static_cast<int32_t>(span);
                int32_t i2 = m_closed_loop ? (i1 + 1) % point_count : min(i1 + 1, point_count - 1);
                int32_t i0 = m_closed_loop ? (i1 - 1 + point_count) % point_count : max(i1 - 1, 0);
                int32_t i3 = m_closed_loop ? (i2 + 1) % point_count : min(i2 + 1, point_count - 1);

                Vector3 current_point = CatmullRom(points[i0], points[i1], points[i2], points[i3], local_t);

                if (seg > 0)
                {
                    Renderer::DrawLine(prev_point, current_point, color_curve, color_curve);
                }

                prev_point = current_point;
            }
        }

        // draw markers at each control point
        float marker_size = 0.15f;
        for (const Vector3& point : points)
        {
            // draw a small cross at each control point
            Renderer::DrawLine(point - Vector3(marker_size, 0, 0), point + Vector3(marker_size, 0, 0), color_point, color_point);
            Renderer::DrawLine(point - Vector3(0, marker_size, 0), point + Vector3(0, marker_size, 0), color_point, color_point);
            Renderer::DrawLine(point - Vector3(0, 0, marker_size), point + Vector3(0, 0, marker_size), color_point, color_point);
        }
    }

    void Spline::Save(pugi::xml_node& node)
    {
        node.append_attribute("closed_loop") = m_closed_loop;
        node.append_attribute("resolution")  = m_resolution;
    }

    void Spline::Load(pugi::xml_node& node)
    {
        m_closed_loop = node.attribute("closed_loop").as_bool(false);
        m_resolution  = node.attribute("resolution").as_uint(20);
    }

    Vector3 Spline::GetPoint(float t) const
    {
        vector<Vector3> points = GetControlPoints();
        if (points.empty())
            return Vector3::Zero;
        if (points.size() == 1)
            return points[0];

        uint32_t span_index = 0;
        float local_t       = 0.0f;
        MapToSpan(t, points, span_index, local_t);

        int32_t point_count = static_cast<int32_t>(points.size());
        int32_t i1 = static_cast<int32_t>(span_index);
        int32_t i2 = m_closed_loop ? (i1 + 1) % point_count : min(i1 + 1, point_count - 1);
        int32_t i0 = m_closed_loop ? (i1 - 1 + point_count) % point_count : max(i1 - 1, 0);
        int32_t i3 = m_closed_loop ? (i2 + 1) % point_count : min(i2 + 1, point_count - 1);

        return CatmullRom(points[i0], points[i1], points[i2], points[i3], local_t);
    }

    Vector3 Spline::GetTangent(float t) const
    {
        vector<Vector3> points = GetControlPoints();
        if (points.size() < 2)
            return Vector3::Forward;

        uint32_t span_index = 0;
        float local_t       = 0.0f;
        MapToSpan(t, points, span_index, local_t);

        int32_t point_count = static_cast<int32_t>(points.size());
        int32_t i1 = static_cast<int32_t>(span_index);
        int32_t i2 = m_closed_loop ? (i1 + 1) % point_count : min(i1 + 1, point_count - 1);
        int32_t i0 = m_closed_loop ? (i1 - 1 + point_count) % point_count : max(i1 - 1, 0);
        int32_t i3 = m_closed_loop ? (i2 + 1) % point_count : min(i2 + 1, point_count - 1);

        Vector3 tangent = CatmullRomTangent(points[i0], points[i1], points[i2], points[i3], local_t);
        tangent.Normalize();
        return tangent;
    }

    float Spline::GetLength(uint32_t samples_per_span) const
    {
        vector<Vector3> points = GetControlPoints();
        if (points.size() < 2)
            return 0.0f;

        uint32_t span_count    = m_closed_loop ? static_cast<uint32_t>(points.size()) : static_cast<uint32_t>(points.size()) - 1;
        uint32_t total_samples = span_count * samples_per_span;
        float length           = 0.0f;
        Vector3 prev_point     = GetPoint(0.0f);

        for (uint32_t i = 1; i <= total_samples; i++)
        {
            float t            = static_cast<float>(i) / static_cast<float>(total_samples);
            Vector3 curr_point = GetPoint(t);
            length            += prev_point.Distance(curr_point);
            prev_point         = curr_point;
        }

        return length;
    }

    uint32_t Spline::GetControlPointCount() const
    {
        return m_entity_ptr ? static_cast<uint32_t>(m_entity_ptr->GetChildrenCount()) : 0;
    }

    void Spline::AddControlPoint(const Vector3& local_position)
    {
        if (!m_entity_ptr)
            return;

        Entity* point = World::CreateEntity();

        // name the point based on its index
        uint32_t index = m_entity_ptr->GetChildrenCount();
        point->SetObjectName("spline_point_" + to_string(index));
        point->SetParent(m_entity_ptr);
        point->SetPositionLocal(local_position);
    }

    void Spline::RemoveLastControlPoint()
    {
        if (!m_entity_ptr || m_entity_ptr->GetChildrenCount() == 0)
            return;

        Entity* last_child = m_entity_ptr->GetChildByIndex(m_entity_ptr->GetChildrenCount() - 1);
        if (last_child)
        {
            World::RemoveEntity(last_child);
        }
    }

    vector<Vector3> Spline::GetControlPoints() const
    {
        vector<Vector3> points;

        if (!m_entity_ptr)
            return points;

        uint32_t child_count = m_entity_ptr->GetChildrenCount();
        points.reserve(child_count);

        for (uint32_t i = 0; i < child_count; i++)
        {
            if (Entity* child = m_entity_ptr->GetChildByIndex(i))
            {
                points.push_back(child->GetPosition());
            }
        }

        return points;
    }

    Vector3 Spline::CatmullRom(const Vector3& p0, const Vector3& p1, const Vector3& p2, const Vector3& p3, float t)
    {
        float t2 = t * t;
        float t3 = t2 * t;

        // catmull-rom matrix form
        return 0.5f * (
            (2.0f * p1) +
            (-p0 + p2) * t +
            (2.0f * p0 - 5.0f * p1 + 4.0f * p2 - p3) * t2 +
            (-p0 + 3.0f * p1 - 3.0f * p2 + p3) * t3
        );
    }

    Vector3 Spline::CatmullRomTangent(const Vector3& p0, const Vector3& p1, const Vector3& p2, const Vector3& p3, float t)
    {
        float t2 = t * t;

        // first derivative of the catmull-rom formula
        return 0.5f * (
            (-p0 + p2) +
            (4.0f * p0 - 10.0f * p1 + 8.0f * p2 - 2.0f * p3) * t +
            (-3.0f * p0 + 9.0f * p1 - 9.0f * p2 + 3.0f * p3) * t2
        );
    }

    void Spline::MapToSpan(float t, const vector<Vector3>& points, uint32_t& span_index, float& local_t) const
    {
        uint32_t span_count = m_closed_loop ? static_cast<uint32_t>(points.size()) : static_cast<uint32_t>(points.size()) - 1;

        // clamp t to [0, 1]
        t = max(0.0f, min(1.0f, t));

        // scale t to span range
        float scaled_t = t * static_cast<float>(span_count);
        span_index     = static_cast<uint32_t>(scaled_t);
        local_t        = scaled_t - static_cast<float>(span_index);

        // handle the edge case where t = 1.0
        if (span_index >= span_count)
        {
            span_index = span_count - 1;
            local_t    = 1.0f;
        }
    }
}
