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

//= INCLUDES ================================
#include "pch.h"
#include "Spline.h"
#include "Physics.h"
#include "Renderable.h"
#include "../Entity.h"
#include "../../Rendering/Renderer.h"
#include "../../Rendering/Material.h"
#include "../../Resource/ResourceCache.h"
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

    Spline::~Spline()
    {
        ClearRoadMesh();
    }

    void Spline::Tick()
    {
        // if the spline had a road mesh when saved, regenerate it now that child entities are loaded
        if (m_needs_road_regeneration)
        {
            m_needs_road_regeneration = false;
            GenerateRoadMesh();
        }

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
        node.append_attribute("closed_loop")   = m_closed_loop;
        node.append_attribute("resolution")   = m_resolution;
        node.append_attribute("road_width")   = m_road_width;
        node.append_attribute("has_road_mesh") = HasRoadMesh();
    }

    void Spline::Load(pugi::xml_node& node)
    {
        m_closed_loop             = node.attribute("closed_loop").as_bool(false);
        m_resolution              = node.attribute("resolution").as_uint(20);
        m_road_width              = node.attribute("road_width").as_float(8.0f);
        m_needs_road_regeneration = node.attribute("has_road_mesh").as_bool(false);

        // if a road mesh was saved, remove the renderable and physics as they will be recreated by GenerateRoadMesh()
        // save the material name first so it can be restored after regeneration
        if (m_needs_road_regeneration && m_entity_ptr)
        {
            if (Renderable* renderable = m_entity_ptr->GetComponent<Renderable>())
            {
                if (Material* material = renderable->GetMaterial())
                {
                    m_saved_material_name = material->GetObjectName();
                }
            }

            m_entity_ptr->RemoveComponent<Renderable>();
            m_entity_ptr->RemoveComponent<Physics>();
        }
    }

    Vector3 Spline::GetPoint(float t) const
    {
        return EvaluatePoint(GetControlPoints(), t);
    }

    Vector3 Spline::GetTangent(float t) const
    {
        return EvaluateTangent(GetControlPoints(), t);
    }

    float Spline::GetLength(uint32_t samples_per_span) const
    {
        vector<Vector3> points = GetControlPoints();
        if (points.size() < 2)
            return 0.0f;

        uint32_t span_count    = m_closed_loop ? static_cast<uint32_t>(points.size()) : static_cast<uint32_t>(points.size()) - 1;
        uint32_t total_samples = span_count * samples_per_span;
        float length           = 0.0f;
        Vector3 prev_point     = EvaluatePoint(points, 0.0f);

        for (uint32_t i = 1; i <= total_samples; i++)
        {
            float t            = static_cast<float>(i) / static_cast<float>(total_samples);
            Vector3 curr_point = EvaluatePoint(points, t);
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

    void Spline::GenerateRoadMesh()
    {
        // need at least 2 control points to generate a road
        vector<Vector3> points = GetControlPointsLocal();
        if (points.size() < 2)
        {
            SP_LOG_WARNING("need at least 2 control points to generate a road mesh");
            return;
        }

        // clean up any previous mesh
        ClearRoadMesh();

        float half_width       = m_road_width * 0.5f;
        uint32_t span_count    = m_closed_loop ? static_cast<uint32_t>(points.size()) : static_cast<uint32_t>(points.size()) - 1;
        uint32_t total_samples = span_count * m_resolution;

        // generate cross-section vertices along the spline
        vector<RHI_Vertex_PosTexNorTan> vertices;
        vector<uint32_t> indices;

        // two vertices per sample (left and right edge), plus one extra for the end
        uint32_t sample_count = total_samples + 1;
        vertices.reserve(sample_count * 2);

        // accumulate distance along the spline for v coordinate
        float accumulated_distance = 0.0f;
        Vector3 prev_position;

        for (uint32_t i = 0; i <= total_samples; i++)
        {
            float t = static_cast<float>(i) / static_cast<float>(total_samples);

            // evaluate position and tangent on the spline
            Vector3 position = EvaluatePoint(points, t);
            Vector3 tangent  = EvaluateTangent(points, t);
            tangent.Normalize();

            // build a coordinate frame: forward (tangent), right, up
            // start with world up and derive right from cross product
            Vector3 up = Vector3::Up;

            // handle near-vertical tangents: fall back to world forward
            if (abs(tangent.Dot(Vector3::Up)) > 0.99f)
            {
                up = Vector3::Forward;
            }

            Vector3 right = tangent.Cross(up);
            right.Normalize();

            // recompute up to be perpendicular to both
            up = right.Cross(tangent);
            up.Normalize();

            // accumulate distance for uv v-coordinate
            if (i > 0)
            {
                accumulated_distance += position.Distance(prev_position);
            }
            prev_position = position;

            // uv: u goes across the road [0, 1], v tiles along the road length
            float v = accumulated_distance / m_road_width; // tile proportionally to road width

            // left vertex
            Vector3 left_pos = position - right * half_width;
            vertices.emplace_back(left_pos, Vector2(0.0f, v), up, tangent);

            // right vertex
            Vector3 right_pos = position + right * half_width;
            vertices.emplace_back(right_pos, Vector2(1.0f, v), up, tangent);
        }

        // generate triangle indices connecting adjacent cross-sections
        indices.reserve(total_samples * 6);
        for (uint32_t i = 0; i < total_samples; i++)
        {
            uint32_t bl = i * 2;       // bottom-left
            uint32_t br = i * 2 + 1;   // bottom-right
            uint32_t tl = (i + 1) * 2; // top-left
            uint32_t tr = (i + 1) * 2 + 1; // top-right

            // first triangle (clockwise winding for front-face)
            indices.push_back(bl);
            indices.push_back(br);
            indices.push_back(tl);

            // second triangle (clockwise winding for front-face)
            indices.push_back(br);
            indices.push_back(tr);
            indices.push_back(tl);
        }

        // create the mesh
        m_mesh = make_shared<Mesh>();
        m_mesh->SetObjectName("spline_road_mesh");
        m_mesh->SetFlag(static_cast<uint32_t>(MeshFlags::PostProcessOptimize), false);
        m_mesh->SetFlag(static_cast<uint32_t>(MeshFlags::PostProcessNormalizeScale), false);
        m_mesh->AddGeometry(vertices, indices, false);
        m_mesh->CreateGpuBuffers();

        // attach to a renderable component on this entity
        Renderable* renderable = m_entity_ptr->GetComponent<Renderable>();
        if (!renderable)
        {
            renderable = m_entity_ptr->AddComponent<Renderable>();
        }
        renderable->SetMesh(m_mesh.get(), 0);

        // restore saved material if one was preserved from a previous load, otherwise use default
        if (!m_saved_material_name.empty())
        {
            shared_ptr<Material> material = ResourceCache::GetByName<Material>(m_saved_material_name);
            if (material)
            {
                renderable->SetMaterial(material);
            }
            else
            {
                renderable->SetDefaultMaterial();
            }
            m_saved_material_name.clear();
        }
        else if (!renderable->GetMaterial())
        {
            renderable->SetDefaultMaterial();
        }

        // attach a physics component so the road mesh is collidable
        // remove any existing one first to force recreation with the new mesh data
        if (m_entity_ptr->GetComponent<Physics>())
        {
            m_entity_ptr->RemoveComponent<Physics>();
        }
        Physics* physics = m_entity_ptr->AddComponent<Physics>();
        physics->SetBodyType(BodyType::Mesh);

        SP_LOG_INFO("generated road mesh: %u vertices, %u indices, %.1f m long, %.1f m wide",
            static_cast<uint32_t>(vertices.size()), static_cast<uint32_t>(indices.size()),
            accumulated_distance, m_road_width);
    }

    void Spline::ClearRoadMesh()
    {
        if (m_mesh)
        {
            // remove the renderable and physics components to avoid dangling mesh pointers
            if (m_entity_ptr)
            {
                m_entity_ptr->RemoveComponent<Physics>();
                m_entity_ptr->RemoveComponent<Renderable>();
            }

            m_mesh.reset();
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

    vector<Vector3> Spline::GetControlPointsLocal() const
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
                points.push_back(child->GetPositionLocal());
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

    Vector3 Spline::EvaluatePoint(const vector<Vector3>& points, float t) const
    {
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

    Vector3 Spline::EvaluateTangent(const vector<Vector3>& points, float t) const
    {
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
