/*
Copyright(c) 2015-2025 Panos Karabelas

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
using namespace spartan::math;
//============================

namespace spartan
{
    void Renderer::DrawLine(const Vector3& from, const Vector3& to, const Color& color_from, const Color& color_to)
    {
        m_lines_vertices.emplace_back(from, color_from);
        m_lines_vertices.emplace_back(to, color_to);
    }

    void Renderer::DrawTriangle(const Vector3& v0, const Vector3& v1, const Vector3& v2, const Color& color /*= DEBUG_COLOR*/)
    {
        DrawLine(v0, v1, color, color);
        DrawLine(v1, v2, color, color);
        DrawLine(v2, v0, color, color);
    }

    void Renderer::DrawBox(const BoundingBox& box, const Color& color)
    {
        const Vector3& min = box.GetMin();
        const Vector3& max = box.GetMax();
    
        DrawLine(Vector3(min.x, min.y, min.z), Vector3(max.x, min.y, min.z), color, color);
        DrawLine(Vector3(max.x, min.y, min.z), Vector3(max.x, max.y, min.z), color, color);
        DrawLine(Vector3(max.x, max.y, min.z), Vector3(min.x, max.y, min.z), color, color);
        DrawLine(Vector3(min.x, max.y, min.z), Vector3(min.x, min.y, min.z), color, color);
        DrawLine(Vector3(min.x, min.y, min.z), Vector3(min.x, min.y, max.z), color, color);
        DrawLine(Vector3(max.x, min.y, min.z), Vector3(max.x, min.y, max.z), color, color);
        DrawLine(Vector3(max.x, max.y, min.z), Vector3(max.x, max.y, max.z), color, color);
        DrawLine(Vector3(min.x, max.y, min.z), Vector3(min.x, max.y, max.z), color, color);
        DrawLine(Vector3(min.x, min.y, max.z), Vector3(max.x, min.y, max.z), color, color);
        DrawLine(Vector3(max.x, min.y, max.z), Vector3(max.x, max.y, max.z), color, color);
        DrawLine(Vector3(max.x, max.y, max.z), Vector3(min.x, max.y, max.z), color, color);
        DrawLine(Vector3(min.x, max.y, max.z), Vector3(min.x, min.y, max.z), color, color);
    }

    void Renderer::DrawCircle(const Vector3& center, const Vector3& axis, const float radius, uint32_t segment_count, const Color& color /*= DEBUG_COLOR*/)
    {
        if (radius <= 0.0f)
            return;

        // Need at least 4 segments
        segment_count = max<uint32_t>(segment_count, static_cast<uint32_t>(4));

        vector<Vector3> points;
        points.reserve(segment_count + 1);
        points.resize(segment_count + 1);

        // Compute points on circle
        float angle_step = math::pi_2 / (float)segment_count;
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
            DrawLine(points[i], points[i + 1], color, color);
        }
    }

    void Renderer::DrawSphere(const Vector3& center, float radius, uint32_t segment_count, const Color& color /*= DEBUG_COLOR*/)
    {
        // Need at least 4 segments
        segment_count = max(segment_count, static_cast<uint32_t>(4));

        Vector3 Vertex1, Vertex2, Vertex3, Vertex4;
        const float AngleInc = 2.f * pi / float(segment_count);
        uint32_t NumSegmentsY = segment_count;
        float Latitude = AngleInc;
        uint32_t NumSegmentsX;
        float Longitude;
        float SinY1 = 0.0f, CosY1 = 1.0f, SinY2, CosY2;
        float SinX, CosX;

        while (NumSegmentsY--)
        {
            SinY2 = sin(Latitude);
            CosY2 = cos(Latitude);

            Vertex1 = Vector3(SinY1, 0.0f, CosY1) * radius + center;
            Vertex3 = Vector3(SinY2, 0.0f, CosY2) * radius + center;
            Longitude = AngleInc;

            NumSegmentsX = segment_count;
            while (NumSegmentsX--)
            {
                SinX = sin(Longitude);
                CosX = cos(Longitude);

                Vertex2 = Vector3((CosX * SinY1), (SinX * SinY1), CosY1) * radius + center;
                Vertex4 = Vector3((CosX * SinY2), (SinX * SinY2), CosY2) * radius + center;

                DrawLine(Vertex1, Vertex2, color, color);
                DrawLine(Vertex1, Vertex3, color, color);

                Vertex1 = Vertex2;
                Vertex3 = Vertex4;
                Longitude += AngleInc;
            }
            SinY1 = SinY2;
            CosY1 = CosY2;
            Latitude += AngleInc;
        }
    }

    void Renderer::DrawDirectionalArrow(const Vector3& start, const Vector3& end, float arrow_size, const Color& color /*= DEBUG_COLOR*/)
    {
        arrow_size = max(0.1f, arrow_size);

        DrawLine(start, end, color, color);

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
        float arrow_sqrt = sqrt(arrow_size);
        Vector3 arrow_pos;
        DrawLine(end, end + TM * Vector3(-arrow_sqrt, arrow_sqrt, 0), color, color);
        DrawLine(end, end + TM * Vector3(-arrow_sqrt, -arrow_sqrt, 0), color, color);
    }

    void Renderer::DrawPlane(const math::Plane& plane, const Color& color /*= DEBUG_COLOR*/)
    {
        // Arrow indicating normal
        Vector3 plane_origin = plane.normal * plane.d;
        DrawDirectionalArrow(plane_origin, plane_origin + plane.normal * 2.0f, 0.2f, color);

        Vector3 U, V;
        plane.normal.FindBestAxisVectors(U, V);
        static const float scale = 10000.0f;
        DrawLine(plane_origin - U * scale, plane_origin + U * scale, color, color);
        DrawLine(plane_origin - V * scale, plane_origin + V * scale, color, color);
    }

    void Renderer::AddLinesToBeRendered()
    {
        // only render debug lines when not in game mode
        if (Engine::IsFlagSet(EngineMode::Playing))
            return;

        if (GetOption<bool>(Renderer_Option::PickingRay))
        {
            Ray ray = World::GetCamera()->ComputePickingRay();
            DrawLine(ray.GetStart(), ray.GetStart() + ray.GetDirection() * World::GetCamera()->GetFarPlane(), Color(0, 1, 0, 1));
        }
        
        if (GetOption<bool>(Renderer_Option::Lights))
        {
            for (const shared_ptr<Entity>& entity : World::GetEntities())
            {
                if (Camera* camera = World::GetCamera())
                {
                    shared_ptr<Entity> entity_selected = camera->GetSelectedEntity();
                    if (entity_selected && entity_selected->GetObjectId() == entity->GetObjectId())
                    {
                        if (Light* light = entity->GetComponent<Light>())
                        { 
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
                                float opposite = light->GetRange() * tan(light->GetAngle());

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
        }
        
        if (GetOption<bool>(Renderer_Option::Aabb))
        {
            auto get_color = [](Renderable* renderable)
            {
                const Color color_visible  = Color::standard_renderer_lines;
                const Color color_occluded = Color(1.0f, 0.0f, 0.0f, 1.0f);

                return renderable->IsVisible() ? color_visible : color_occluded;
            };

            for (const shared_ptr<Entity>& entity : World::GetEntities())
            {
                if (entity)
                { 
                    if (Renderable* renderable = entity->GetComponent<Renderable>())
                    {
                        if (!renderable->HasInstancing())
                        { 
                            DrawBox(renderable->GetBoundingBox(), get_color(renderable));
                        }
                        else
                        {
                            uint32_t group_count = static_cast<uint32_t>(renderable->GetBoundingBoxGroupEndIndices().size());
                            for (uint32_t group_index = 0; group_index < group_count; group_index++)
                            {
                                const BoundingBox& bounding_box_group = renderable->GetBoundingBoxInstanceGroup(group_index);
                                DrawBox(bounding_box_group, get_color(renderable));
                            }
                        }
                    }
                }
            }
        }
    }
}
