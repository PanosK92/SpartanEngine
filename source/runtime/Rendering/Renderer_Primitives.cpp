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

//= INCLUDES ==========================
#include "pch.h"
#include "Renderer.h"
#include "../World/Components/Camera.h"
#include "../World/Components/Light.h"
#include "../World/Entity.h"
#include "../Core/Timer.h"
//=====================================

//= NAMESPACES ===============
using namespace std;
using namespace spartan::math;
//============================

namespace spartan
{
    void Renderer::DrawLine(const Vector3& from, const Vector3& to, const Color& color_from, const Color& color_to, float duration_sec /*= 0.0f*/)
    {
        if (duration_sec <= 0.0f)
        {
            // single frame line - add directly to render list
            m_lines_vertices.emplace_back(from, color_from);
            m_lines_vertices.emplace_back(to, color_to);
        }
        else
        {
            // persistent line - add to persistent list with expiration time
            PersistentLine line;
            line.from        = from;
            line.to          = to;
            line.color_from  = color_from;
            line.color_to    = color_to;
            line.expire_time = Timer::GetTimeSec() + static_cast<double>(duration_sec);
            m_persistent_lines.push_back(line);
        }
    }

    void Renderer::DrawTriangle(const Vector3& v0, const Vector3& v1, const Vector3& v2, const Color& color /*= DEBUG_COLOR*/, float duration_sec /*= 0.0f*/)
    {
        DrawLine(v0, v1, color, color, duration_sec);
        DrawLine(v1, v2, color, color, duration_sec);
        DrawLine(v2, v0, color, color, duration_sec);
    }

    void Renderer::DrawBox(const BoundingBox& box, const Color& color, float duration_sec /*= 0.0f*/)
    {
        const Vector3& min = box.GetMin();
        const Vector3& max = box.GetMax();
    
        DrawLine(Vector3(min.x, min.y, min.z), Vector3(max.x, min.y, min.z), color, color, duration_sec);
        DrawLine(Vector3(max.x, min.y, min.z), Vector3(max.x, max.y, min.z), color, color, duration_sec);
        DrawLine(Vector3(max.x, max.y, min.z), Vector3(min.x, max.y, min.z), color, color, duration_sec);
        DrawLine(Vector3(min.x, max.y, min.z), Vector3(min.x, min.y, min.z), color, color, duration_sec);
        DrawLine(Vector3(min.x, min.y, min.z), Vector3(min.x, min.y, max.z), color, color, duration_sec);
        DrawLine(Vector3(max.x, min.y, min.z), Vector3(max.x, min.y, max.z), color, color, duration_sec);
        DrawLine(Vector3(max.x, max.y, min.z), Vector3(max.x, max.y, max.z), color, color, duration_sec);
        DrawLine(Vector3(min.x, max.y, min.z), Vector3(min.x, max.y, max.z), color, color, duration_sec);
        DrawLine(Vector3(min.x, min.y, max.z), Vector3(max.x, min.y, max.z), color, color, duration_sec);
        DrawLine(Vector3(max.x, min.y, max.z), Vector3(max.x, max.y, max.z), color, color, duration_sec);
        DrawLine(Vector3(max.x, max.y, max.z), Vector3(min.x, max.y, max.z), color, color, duration_sec);
        DrawLine(Vector3(min.x, max.y, max.z), Vector3(min.x, min.y, max.z), color, color, duration_sec);
    }

    void Renderer::DrawCircle(const Vector3& center, const Vector3& axis, const float radius, uint32_t segment_count, const Color& color /*= DEBUG_COLOR*/, float duration_sec /*= 0.0f*/)
    {
        if (radius <= 0.0f)
            return;

        // need at least 4 segments
        segment_count = max<uint32_t>(segment_count, static_cast<uint32_t>(4));

        vector<Vector3> points;
        points.reserve(segment_count + 1);
        points.resize(segment_count + 1);

        // compute points on circle
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

        // draw
        for (uint32_t i = 0; i <= segment_count - 1; i++)
        {
            DrawLine(points[i], points[i + 1], color, color, duration_sec);
        }
    }

    void Renderer::DrawSphere(const Vector3& center, float radius, uint32_t segment_count, const Color& color /*= DEBUG_COLOR*/, float duration_sec /*= 0.0f*/)
    {
        // need at least 4 segments
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

                DrawLine(Vertex1, Vertex2, color, color, duration_sec);
                DrawLine(Vertex1, Vertex3, color, color, duration_sec);

                Vertex1 = Vertex2;
                Vertex3 = Vertex4;
                Longitude += AngleInc;
            }
            SinY1 = SinY2;
            CosY1 = CosY2;
            Latitude += AngleInc;
        }
    }

    void Renderer::DrawDirectionalArrow(const Vector3& start, const Vector3& end, float arrow_size, const Color& color /*= DEBUG_COLOR*/, float duration_sec /*= 0.0f*/)
    {
        arrow_size = max(0.1f, arrow_size);

        DrawLine(start, end, color, color, duration_sec);

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
        DrawLine(end, end + TM * Vector3(-arrow_sqrt, arrow_sqrt, 0), color, color, duration_sec);
        DrawLine(end, end + TM * Vector3(-arrow_sqrt, -arrow_sqrt, 0), color, color, duration_sec);
    }

    void Renderer::DrawPlane(const math::Plane& plane, const Color& color /*= DEBUG_COLOR*/, float duration_sec /*= 0.0f*/)
    {
        // arrow indicating normal
        Vector3 plane_origin = plane.normal * plane.d;
        DrawDirectionalArrow(plane_origin, plane_origin + plane.normal * 2.0f, 0.2f, color, duration_sec);

        Vector3 U, V;
        plane.normal.FindBestAxisVectors(U, V);
        static const float scale = 10000.0f;
        DrawLine(plane_origin - U * scale, plane_origin + U * scale, color, color, duration_sec);
        DrawLine(plane_origin - V * scale, plane_origin + V * scale, color, color, duration_sec);
    }

    void Renderer::UpdatePersistentLines()
    {
        double current_time = Timer::GetTimeSec();

        // add non-expired persistent lines to the render list and remove expired ones
        for (auto it = m_persistent_lines.begin(); it != m_persistent_lines.end();)
        {
            if (current_time < it->expire_time)
            {
                // line is still valid, add to render list
                m_lines_vertices.emplace_back(it->from, it->color_from);
                m_lines_vertices.emplace_back(it->to, it->color_to);
                ++it;
            }
            else
            {
                // line has expired, remove it
                it = m_persistent_lines.erase(it);
            }
        }
    }

    void Renderer::AddLinesToBeRendered()
    {
        // only render debug lines when not in game mode
        if (Engine::IsFlagSet(EngineMode::Playing))
            return;

        if (cvar_picking_ray.GetValueAs<bool>())
        {
            Ray ray = World::GetCamera()->ComputePickingRay();
            DrawLine(ray.GetStart(), ray.GetStart() + ray.GetDirection() * World::GetCamera()->GetFarPlane(), Color(0, 1, 0, 1));
        }
        
        if (cvar_lights.GetValueAs<bool>())
        {
            if (Camera* camera = World::GetCamera())
            {
                // iterate through all selected entities
                for (Entity* entity : camera->GetSelectedEntities())
                {
                    if (!entity)
                        continue;
                        
                    if (Light* light = entity->GetComponent<Light>())
                    { 
                        if (light->GetLightType() == LightType::Directional)
                        {
                            Vector3 pos = light->GetEntity()->GetPosition() - light->GetEntity()->GetForward() * FLT_MAX;
                            DrawDirectionalArrow(pos, Vector3::Zero, 2.5f);
                        }
                        else if (light->GetLightType() == LightType::Point)
                        {
                            Vector3 center = light->GetEntity()->GetPosition();
                            float radius   = light->GetRange();
                            uint32_t segment_count = 64;

                            DrawCircle(center, Vector3::Up,      radius, segment_count);
                            DrawCircle(center, Vector3::Right,   radius, segment_count);
                            DrawCircle(center, Vector3::Forward, radius, segment_count);
                        }
                        else if (light->GetLightType() == LightType::Spot)
                        {
                            // tan(angle) = opposite/adjacent
                            // opposite = adjacent * tan(angle)
                            float opposite = light->GetRange() * tan(light->GetAngle());

                            Vector3 pos_end_center = light->GetEntity()->GetForward() * light->GetRange();
                            Vector3 pos_end_up     = pos_end_center + light->GetEntity()->GetUp()    * opposite;
                            Vector3 pos_end_right  = pos_end_center + light->GetEntity()->GetRight() * opposite;
                            Vector3 pos_end_down   = pos_end_center + light->GetEntity()->GetDown()  * opposite;
                            Vector3 pos_end_left   = pos_end_center + light->GetEntity()->GetLeft()  * opposite;

                            Vector3 pos_start = light->GetEntity()->GetPosition();
                            DrawLine(pos_start, pos_start + pos_end_center);
                            DrawLine(pos_start, pos_start + pos_end_up);
                            DrawLine(pos_start, pos_start + pos_end_right);
                            DrawLine(pos_start, pos_start + pos_end_down);
                            DrawLine(pos_start, pos_start + pos_end_left);
                        }
                        else if (light->GetLightType() == LightType::Area)
                        {
                            // area light is a rectangular emitter
                            Vector3 center     = light->GetEntity()->GetPosition();
                            Vector3 right      = light->GetEntity()->GetRight();
                            Vector3 up         = light->GetEntity()->GetUp();
                            Vector3 forward    = light->GetEntity()->GetForward();
                            float half_width   = light->GetAreaWidth() * 0.5f;
                            float half_height  = light->GetAreaHeight() * 0.5f;

                            // compute the four corners of the rectangle
                            Vector3 corner_tl = center - right * half_width + up * half_height; // top-left
                            Vector3 corner_tr = center + right * half_width + up * half_height; // top-right
                            Vector3 corner_br = center + right * half_width - up * half_height; // bottom-right
                            Vector3 corner_bl = center - right * half_width - up * half_height; // bottom-left

                            // draw the rectangle outline
                            DrawLine(corner_tl, corner_tr);
                            DrawLine(corner_tr, corner_br);
                            DrawLine(corner_br, corner_bl);
                            DrawLine(corner_bl, corner_tl);

                            // draw direction indicator (arrow from center pointing forward)
                            float arrow_length = min(half_width, half_height) * 0.5f;
                            DrawDirectionalArrow(center, center + forward * arrow_length, arrow_length * 0.3f);
                        }
                    }
                }
            }
        }
        
        if (cvar_aabb.GetValueAs<bool>())
        {
            auto get_color = [](Renderable* renderable)
            {
                const Color color_visible  = Color::standard_renderer_lines;
                const Color color_occluded = Color(1.0f, 0.0f, 0.0f, 1.0f);

                return renderable->IsVisible() ? color_visible : color_occluded;
            };

            for (Entity* entity : World::GetEntities())
            {
                if (!entity)
                    continue;

                // meshes
                if (Renderable* renderable = entity->GetComponent<Renderable>())
                {
                    if (Camera* camera = World::GetCamera())
                    {
                        const Vector3 camera_position   = camera->GetEntity()->GetPosition();
                        const BoundingBox& bounding_box = renderable->GetBoundingBox();
                        const float distance            = bounding_box.GetClosestPoint(camera_position).Distance(camera_position);

                        if (distance > renderable->GetMaxRenderDistance())
                            continue;

                        DrawBox(bounding_box, get_color(renderable));
                    }
                }

                // lights
                if (Light* light = entity->GetComponent<Light>())
                {
                    DrawBox(light->GetBoundingBox(), Color::standard_yellow);
                }
            }
        }
    }
}
