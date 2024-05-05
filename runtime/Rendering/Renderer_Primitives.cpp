/*
Copyright(c) 2016-2024 Panos Karabelas

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

//= INCLUDES ==========================
#include "pch.h"
#include "Renderer.h"
#include "../World/Components/Camera.h"
#include "../World/Components/Light.h"
#include "../World/Entity.h"
//=====================================

//= NAMESPACES ===============
using namespace std;
using namespace Spartan::Math;
//============================

namespace Spartan
{
    namespace
    {
        vector<RHI_Vertex_PosCol> m_line_vertices;
        vector<float> m_lines_duration;
        uint32_t m_lines_index_depth_off = 0;
        uint32_t m_lines_index_depth_on  = 0;
    }

    void Renderer::DrawLine(const Vector3& from, const Vector3& to, const Color& color_from, const Color& color_to, const float duration /*= 0.0f*/, const bool depth /*= true*/)
    {
        // get vertex index
        uint32_t& index = depth ? m_lines_index_depth_on : m_lines_index_depth_off;

        // grow vertex vector (if needed)
        uint32_t vertex_count = static_cast<uint32_t>(m_line_vertices.size());
        if (index + 2 >= vertex_count)
        {
            uint32_t new_vertex_count = vertex_count == 0 ? 32768 : vertex_count * 2;

            // if this is not the first allocation, inform the user
            if (vertex_count != 0)
            {
                SP_LOG_INFO("Line buffer can hold %d vertices but %d are needed, resizing the buffer to fit %d vertices.", vertex_count, index + 2, new_vertex_count);
            }

            m_lines_index_depth_off = numeric_limits<uint32_t>::max(); // max because it's incremented below
            m_lines_index_depth_on  = (new_vertex_count / 2) - 1;      // -1 because it's incremented below

            m_line_vertices.reserve(new_vertex_count);
            m_line_vertices.resize(new_vertex_count);

            m_lines_duration.reserve(new_vertex_count);
            m_lines_duration.resize(new_vertex_count);
        }

        // write lines
        {
            index++;
            RHI_Vertex_PosCol& line_start = m_line_vertices[index];
            line_start.pos[0]             = from.x;
            line_start.pos[1]             = from.y;
            line_start.pos[2]             = from.z;
            line_start.col[0]             = color_from.r;
            line_start.col[1]             = color_from.g;
            line_start.col[2]             = color_from.b;
            line_start.col[3]             = 1.0f;
            m_lines_duration[index]       = duration;

            index++;
            RHI_Vertex_PosCol& line_end = m_line_vertices[index];
            line_end.pos[0]             = to.x;
            line_end.pos[1]             = to.y;
            line_end.pos[2]             = to.z;
            line_end.col[0]             = color_to.r;
            line_end.col[1]             = color_to.g;
            line_end.col[2]             = color_to.b;
            line_end.col[3]             = 1.0f;
            m_lines_duration[index]     = duration;
        }
    }

    void Renderer::DrawTriangle(const Vector3& v0, const Vector3& v1, const Vector3& v2, const Color& color /*= DEBUG_COLOR*/, const float duration /*= 0.0f*/, bool depth /*= true*/)
    {
        DrawLine(v0, v1, color, color, duration, depth);
        DrawLine(v1, v2, color, color, duration, depth);
        DrawLine(v2, v0, color, color, duration, depth);
    }

    void Renderer::DrawBox(const BoundingBox& box, const Color& color, const float duration /*= 0.0f*/, const bool depth /*= true*/)
    {
        const Vector3& min = box.GetMin();
        const Vector3& max = box.GetMax();
    
        DrawLine(Vector3(min.x, min.y, min.z), Vector3(max.x, min.y, min.z), color, color, duration, depth);
        DrawLine(Vector3(max.x, min.y, min.z), Vector3(max.x, max.y, min.z), color, color, duration, depth);
        DrawLine(Vector3(max.x, max.y, min.z), Vector3(min.x, max.y, min.z), color, color, duration, depth);
        DrawLine(Vector3(min.x, max.y, min.z), Vector3(min.x, min.y, min.z), color, color, duration, depth);
        DrawLine(Vector3(min.x, min.y, min.z), Vector3(min.x, min.y, max.z), color, color, duration, depth);
        DrawLine(Vector3(max.x, min.y, min.z), Vector3(max.x, min.y, max.z), color, color, duration, depth);
        DrawLine(Vector3(max.x, max.y, min.z), Vector3(max.x, max.y, max.z), color, color, duration, depth);
        DrawLine(Vector3(min.x, max.y, min.z), Vector3(min.x, max.y, max.z), color, color, duration, depth);
        DrawLine(Vector3(min.x, min.y, max.z), Vector3(max.x, min.y, max.z), color, color, duration, depth);
        DrawLine(Vector3(max.x, min.y, max.z), Vector3(max.x, max.y, max.z), color, color, duration, depth);
        DrawLine(Vector3(max.x, max.y, max.z), Vector3(min.x, max.y, max.z), color, color, duration, depth);
        DrawLine(Vector3(min.x, max.y, max.z), Vector3(min.x, min.y, max.z), color, color, duration, depth);
    }

    void Renderer::DrawCircle(const Vector3& center, const Vector3& axis, const float radius, uint32_t segment_count, const Color& color /*= DEBUG_COLOR*/, const float duration /*= 0.0f*/, const bool depth /*= true*/)
    {
        if (radius <= 0.0f)
            return;

        // Need at least 4 segments
        segment_count = Helper::Max<uint32_t>(segment_count, 4);

        vector<Vector3> points;
        points.reserve(segment_count + 1);
        points.resize(segment_count + 1);

        // Compute points on circle
        float angle_step = Math::Helper::PI_2 / (float)segment_count;
        for (uint32_t i = 0; i <= segment_count; i++)
        {
            float angle = (float)i * angle_step;
            if (axis.x != 0.0f)
            {
                points[i] = Vector3(center.x, cos(angle) * radius + center.y, sin(angle) * radius + center.z);
            }
            else if (axis.y != 0.0f)
            {
                points[i] = Vector3(cos(angle) * radius + center.x, center.y, sin(angle) * radius + center.z);
            }
            else
            {
                points[i] = Vector3(cos(angle) * radius + center.x, sin(angle) * radius + center.y, center.z);
            }
        }

        // Draw
        for (uint32_t i = 0; i <= segment_count - 1; i++)
        {
            DrawLine(points[i], points[i + 1], color, color, duration, depth);
        }
    }

    void Renderer::DrawSphere(const Vector3& center, float radius, uint32_t segment_count, const Color& color /*= DEBUG_COLOR*/, const float duration /*= 0.0f*/, const bool depth /*= true*/)
    {
        // Need at least 4 segments
        segment_count = Helper::Max<uint32_t>(segment_count, 4);

        Vector3 Vertex1, Vertex2, Vertex3, Vertex4;
        const float AngleInc = 2.f * Helper::PI / float(segment_count);
        uint32_t NumSegmentsY = segment_count;
        float Latitude = AngleInc;
        uint32_t NumSegmentsX;
        float Longitude;
        float SinY1 = 0.0f, CosY1 = 1.0f, SinY2, CosY2;
        float SinX, CosX;

        while (NumSegmentsY--)
        {
            SinY2 = Helper::Sin(Latitude);
            CosY2 = Helper::Cos(Latitude);

            Vertex1 = Vector3(SinY1, 0.0f, CosY1) * radius + center;
            Vertex3 = Vector3(SinY2, 0.0f, CosY2) * radius + center;
            Longitude = AngleInc;

            NumSegmentsX = segment_count;
            while (NumSegmentsX--)
            {
                SinX = Helper::Sin(Longitude);
                CosX = Helper::Cos(Longitude);

                Vertex2 = Vector3((CosX * SinY1), (SinX * SinY1), CosY1) * radius + center;
                Vertex4 = Vector3((CosX * SinY2), (SinX * SinY2), CosY2) * radius + center;

                DrawLine(Vertex1, Vertex2, color, color, duration, depth);
                DrawLine(Vertex1, Vertex3, color, color, duration, depth);

                Vertex1 = Vertex2;
                Vertex3 = Vertex4;
                Longitude += AngleInc;
            }
            SinY1 = SinY2;
            CosY1 = CosY2;
            Latitude += AngleInc;
        }
    }

    void Renderer::DrawDirectionalArrow(const Vector3& start, const Vector3& end, float arrow_size, const Color& color /*= DEBUG_COLOR*/, const float duration /*= 0.0f*/, const bool depth /*= true*/)
    {
        arrow_size = Helper::Max<float>(0.1f, arrow_size);

        DrawLine(start, end, color, color, duration, depth);

        Vector3 Dir = (end - start);
        Dir.Normalize();
        Vector3 Up(0, 0, 1);
        Vector3 Right = Dir.Cross(Up);
        if (!Right.IsNormalized())
        {
            Dir.FindBestAxisVectors(Up, Right);
        }

        Matrix TM;
        TM.m00 = Dir.x;   TM.m01 = Dir.y;   TM.m02 = Dir.z;
        TM.m10 = Right.x; TM.m11 = Right.y; TM.m12 = Right.z;
        TM.m20 = Up.x;    TM.m21 = Up.y;    TM.m22 = Up.z;

        // since dir is x direction, my arrow will be pointing +y, -x and -y, -x
        float arrow_sqrt = Helper::Sqrt(arrow_size);
        Vector3 arrow_pos;
        DrawLine(end, end + TM * Vector3(-arrow_sqrt, arrow_sqrt, 0), color, color, duration, depth);
        DrawLine(end, end + TM * Vector3(-arrow_sqrt, -arrow_sqrt, 0), color, color, duration, depth);
    }

    void Renderer::DrawPlane(const Math::Plane& plane, const Color& color /*= DEBUG_COLOR*/, const float duration /*= 0.0f*/, const bool depth /*= true*/)
    {
        // Arrow indicating normal
        Vector3 plane_origin = plane.normal * plane.d;
        DrawDirectionalArrow(plane_origin, plane_origin + plane.normal * 2.0f, 0.2f, color, duration, depth);

        Vector3 U, V;
        plane.normal.FindBestAxisVectors(U, V);
        static const float scale = 10000.0f;
        DrawLine(plane_origin - U * scale, plane_origin + U * scale, color, color, duration, depth);
        DrawLine(plane_origin - V * scale, plane_origin + V * scale, color, color, duration, depth);
    }

    void Renderer::AddLinesToBeRendered()
    {
        // only render debug lines when not in game mode
        if (Engine::IsFlagSet(EngineMode::Game))
            return;

        if (GetOption<bool>(Renderer_Option::PickingRay))
        {
            const auto& ray = GetCamera()->GetPickingRay();
            DrawLine(ray.GetStart(), ray.GetStart() + ray.GetDirection() * GetCamera()->GetFarPlane(), Color(0, 1, 0, 1));
        }
        
        if (GetOption<bool>(Renderer_Option::Lights))
        {
            auto& lights = GetEntities()[Renderer_Entity::Light];
            for (const auto& entity : lights)
            {
                if (shared_ptr<Camera> camera = GetCamera())
                {
                    shared_ptr<Entity> entity_selected = camera->GetSelectedEntity();
                    if (entity_selected && entity_selected->GetObjectId() == entity->GetObjectId())
                    {
                        shared_ptr<Light> light = entity->GetComponent<Light>();

                        if (light->GetLightType() == LightType::Directional)
                        {
                            Vector3 pos = light->GetEntity()->GetPosition() - light->GetEntity()->GetForward() * 1000.0f;
                            DrawDirectionalArrow(pos, Vector3::Zero, 2.5f);
                        }
                        else if (light->GetLightType() == LightType::Point)
                        {
                            Vector3 center = light->GetEntity()->GetPosition();
                            float radius   = light->GetRange();
                            uint32_t segment_count = 64;

                            DrawCircle(center, Vector3::Up, radius, segment_count);
                            DrawCircle(center, Vector3::Right, radius, segment_count);
                            DrawCircle(center, Vector3::Forward, radius, segment_count);
                        }
                        else if (light->GetLightType() == LightType::Spot)
                        {
                            // tan(angle) = opposite/adjacent
                            // opposite = adjacent * tan(angle)
                            float opposite = light->GetRange() * Math::Helper::Tan(light->GetAngle());

                            Vector3 pos_end_center = light->GetEntity()->GetForward() * light->GetRange();
                            Vector3 pos_end_up     = pos_end_center + light->GetEntity()->GetUp() * opposite;
                            Vector3 pos_end_right  = pos_end_center + light->GetEntity()->GetRight() * opposite;
                            Vector3 pos_end_down   = pos_end_center + light->GetEntity()->GetDown() * opposite;
                            Vector3 pos_end_left   = pos_end_center + light->GetEntity()->GetLeft() * opposite;

                            Vector3 pos_start = light->GetEntity()->GetPosition();
                            DrawLine(pos_start, pos_start + pos_end_center);
                            DrawLine(pos_start, pos_start + pos_end_up);
                            DrawLine(pos_start, pos_start + pos_end_right);
                            DrawLine(pos_start, pos_start + pos_end_down);
                            DrawLine(pos_start, pos_start + pos_end_left);
                        }
                    }
                }
            }
        }
        
        if (GetOption<bool>(Renderer_Option::Aabb))
        {
            auto get_color = [](shared_ptr<Renderable>& renderable)
            {
                static const Color color_visible        = Color::standard_renderer_lines;
                static const Color color_occluded       = Color(1.0f, 0.0f, 0.0f, 1.0f);
                static const Color color_ignore_culling = Color(1.0f, 1.0f, 0.0f, 1.0f);

                Color color = color_visible;
                color       = !renderable->IsVisible() ? color_occluded : color;

                return color;
            };

            auto draw_bounding_boxes = [&get_color](const Renderer_Entity entity_type)
            {
                for (const auto& entity : GetEntities()[entity_type])
                {
                    if (auto renderable = entity->GetComponent<Renderable>())
                    {
                        BoundingBoxType bounding_box_type = renderable->HasInstancing() ? BoundingBoxType::TransformedInstances : BoundingBoxType::Transformed;
                        DrawBox(renderable->GetBoundingBox(bounding_box_type), get_color(renderable));
                    }
                }
            };
            
            auto draw_instance_group_bounding_boxes = [&get_color](const Renderer_Entity entity_type)
            {
                for (const auto& entity : GetEntities()[entity_type])
                {
                    if (shared_ptr<Renderable> renderable = entity->GetComponent<Renderable>())
                    {
                        uint32_t group_count = static_cast<uint32_t>(renderable->GetBoundingBoxGroupEndIndices().size());
                        for (uint32_t group_index = 0; group_index < group_count; group_index++)
                        {
                            const BoundingBox& bounding_box_group = renderable->GetBoundingBox(BoundingBoxType::TransformedInstanceGroup, group_index);
                            DrawBox(bounding_box_group, get_color(renderable));
                        }
                    }
                }
            };

            draw_bounding_boxes(Renderer_Entity::Mesh);
            draw_instance_group_bounding_boxes(Renderer_Entity::Mesh);
        }
    }
}
