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
            m_lines_vertices.emplace_back(from, color_from);
            m_lines_vertices.emplace_back(to, color_to);
            return;
        }

        PersistentLine line;
        line.from        = from;
        line.to          = to;
        line.color_from  = color_from;
        line.color_to    = color_to;
        line.expire_time = Timer::GetTimeSec() + static_cast<double>(duration_sec);
        m_persistent_lines.push_back(line);
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

        segment_count = max<uint32_t>(segment_count, 4u);

        vector<Vector3> points(segment_count + 1);
        const float angle_step = math::pi_2 / static_cast<float>(segment_count);
        for (uint32_t i = 0; i <= segment_count; i++)
        {
            const float angle = static_cast<float>(i) * angle_step;
            if (axis.x != 0.0f)
                points[i] = Vector3(center.x, cos(angle) * radius + center.y, sin(angle) * radius + center.z);
            else if (axis.y != 0.0f)
                points[i] = Vector3(cos(angle) * radius + center.x, center.y, sin(angle) * radius + center.z);
            else
                points[i] = Vector3(cos(angle) * radius + center.x, sin(angle) * radius + center.y, center.z);
        }

        for (uint32_t i = 0; i < segment_count; i++)
            DrawLine(points[i], points[i + 1], color, color, duration_sec);
    }

    void Renderer::DrawSphere(const Vector3& center, float radius, uint32_t segment_count, const Color& color /*= DEBUG_COLOR*/, float duration_sec /*= 0.0f*/)
    {
        segment_count = max(segment_count, 4u);

        const float angle_inc = 2.f * pi / static_cast<float>(segment_count);
        uint32_t  segments_y  = segment_count;
        float     latitude    = angle_inc;
        float     sin_y1      = 0.0f;
        float     cos_y1      = 1.0f;

        while (segments_y--)
        {
            const float sin_y2 = sin(latitude);
            const float cos_y2 = cos(latitude);

            Vector3 v1 = Vector3(sin_y1, 0.0f, cos_y1) * radius + center;
            Vector3 v3 = Vector3(sin_y2, 0.0f, cos_y2) * radius + center;
            float longitude = angle_inc;

            uint32_t segments_x = segment_count;
            while (segments_x--)
            {
                const float sin_x = sin(longitude);
                const float cos_x = cos(longitude);

                Vector3 v2 = Vector3(cos_x * sin_y1, sin_x * sin_y1, cos_y1) * radius + center;
                Vector3 v4 = Vector3(cos_x * sin_y2, sin_x * sin_y2, cos_y2) * radius + center;

                DrawLine(v1, v2, color, color, duration_sec);
                DrawLine(v1, v3, color, color, duration_sec);

                v1 = v2;
                v3 = v4;
                longitude += angle_inc;
            }
            sin_y1 = sin_y2;
            cos_y1 = cos_y2;
            latitude += angle_inc;
        }
    }

    void Renderer::DrawDirectionalArrow(const Vector3& start, const Vector3& end, float arrow_size, const Color& color /*= DEBUG_COLOR*/, float duration_sec /*= 0.0f*/)
    {
        arrow_size = max(0.1f, arrow_size);

        DrawLine(start, end, color, color, duration_sec);

        Vector3 dir = (end - start);
        dir.Normalize();
        Vector3 up(0, 0, 1);
        Vector3 right = dir.Cross(up);
        if (!right.IsNormalized())
            dir.FindBestAxisVectors(up, right);

        Matrix tm;
        tm.m00 = dir.x;   tm.m01 = dir.y;   tm.m02 = dir.z;
        tm.m10 = right.x; tm.m11 = right.y; tm.m12 = right.z;
        tm.m20 = up.x;    tm.m21 = up.y;    tm.m22 = up.z;

        const float arrow_sqrt = sqrt(arrow_size);
        DrawLine(end, end + tm * Vector3(-arrow_sqrt,  arrow_sqrt, 0), color, color, duration_sec);
        DrawLine(end, end + tm * Vector3(-arrow_sqrt, -arrow_sqrt, 0), color, color, duration_sec);
    }

    void Renderer::DrawPlane(const math::Plane& plane, const Color& color /*= DEBUG_COLOR*/, float duration_sec /*= 0.0f*/)
    {
        Vector3 plane_origin = plane.normal * plane.d;
        DrawDirectionalArrow(plane_origin, plane_origin + plane.normal * 2.0f, 0.2f, color, duration_sec);

        Vector3 u, v;
        plane.normal.FindBestAxisVectors(u, v);
        const float scale = 10000.0f;
        DrawLine(plane_origin - u * scale, plane_origin + u * scale, color, color, duration_sec);
        DrawLine(plane_origin - v * scale, plane_origin + v * scale, color, color, duration_sec);
    }

    void Renderer::UpdatePersistentLines()
    {
        const double now = Timer::GetTimeSec();

        for (auto it = m_persistent_lines.begin(); it != m_persistent_lines.end();)
        {
            if (now < it->expire_time)
            {
                m_lines_vertices.emplace_back(it->from, it->color_from);
                m_lines_vertices.emplace_back(it->to,   it->color_to);
                ++it;
            }
            else
            {
                it = m_persistent_lines.erase(it);
            }
        }
    }

    void Renderer::AddLinesToBeRendered()
    {
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
                for (Entity* entity : camera->GetSelectedEntities())
                {
                    if (!entity)
                        continue;
                        
                    Light* light = entity->GetComponent<Light>();
                    if (!light)
                        continue;

                    if (light->GetLightType() == LightType::Directional)
                    {
                        Vector3 pos = light->GetEntity()->GetPosition() - light->GetEntity()->GetForward() * FLT_MAX;
                        DrawDirectionalArrow(pos, Vector3::Zero, 2.5f);
                    }
                    else if (light->GetLightType() == LightType::Point)
                    {
                        Vector3 center         = light->GetEntity()->GetPosition();
                        float radius           = light->GetRange();
                        uint32_t segment_count = 64;

                        DrawCircle(center, Vector3::Up,      radius, segment_count);
                        DrawCircle(center, Vector3::Right,   radius, segment_count);
                        DrawCircle(center, Vector3::Forward, radius, segment_count);
                    }
                    else if (light->GetLightType() == LightType::Spot)
                    {
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
                        Vector3 center    = light->GetEntity()->GetPosition();
                        Vector3 right     = light->GetEntity()->GetRight();
                        Vector3 up        = light->GetEntity()->GetUp();
                        Vector3 forward   = light->GetEntity()->GetForward();
                        float half_width  = light->GetAreaWidth()  * 0.5f;
                        float half_height = light->GetAreaHeight() * 0.5f;

                        Vector3 corner_tl = center - right * half_width + up * half_height;
                        Vector3 corner_tr = center + right * half_width + up * half_height;
                        Vector3 corner_br = center + right * half_width - up * half_height;
                        Vector3 corner_bl = center - right * half_width - up * half_height;

                        DrawLine(corner_tl, corner_tr);
                        DrawLine(corner_tr, corner_br);
                        DrawLine(corner_br, corner_bl);
                        DrawLine(corner_bl, corner_tl);

                        float arrow_length = min(half_width, half_height) * 0.5f;
                        DrawDirectionalArrow(center, center + forward * arrow_length, arrow_length * 0.3f);
                    }
                }
            }
        }
        
        if (cvar_aabb.GetValueAs<bool>())
        {
            auto get_color = [](Render* renderable)
            {
                return renderable->IsVisible() ? Color::standard_renderer_lines : Color(1.0f, 0.0f, 0.0f, 1.0f);
            };

            for (Entity* entity : World::GetEntities())
            {
                if (!entity)
                    continue;

                if (Render* renderable = entity->GetComponent<Render>())
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

                if (Light* light = entity->GetComponent<Light>())
                    DrawBox(light->GetBoundingBox(), Color::standard_yellow);
            }
        }
    }
}
