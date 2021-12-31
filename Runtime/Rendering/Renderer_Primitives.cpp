/*
Copyright(c) 2016-2021 Panos Karabelas

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

//= INCLUDES =============================
#include "Spartan.h"
#include "Renderer.h"
#include "../World/Components/Camera.h"
#include "../World/Components/Transform.h"
#include "../World/Components/Light.h"
#include "../World/Entity.h"
#include "../World/World.h"
#include "../World/Components/Renderable.h"
#include "Gizmos/TransformHandle.h"
//========================================

//= NAMESPACES ===============
using namespace std;
using namespace Spartan::Math;
//============================

namespace Spartan
{
    void Renderer::DrawLine(const Vector3& from, const Vector3& to, const Vector4& color_from, const Vector4& color_to, const float duration /*= 0.0f*/, const bool depth /*= true*/)
    {
        // Get vertex index.
        uint32_t& index = depth ? m_lines_index_depth_on : m_lines_index_depth_off;

        // Grow vertex vector (if needed).
         uint32_t vertex_count = static_cast<uint32_t>(m_line_vertices.size());
        if (index + 2 >= vertex_count)
        {
            uint32_t new_vertex_count = vertex_count == 0 ? 32768 : vertex_count * 2;

            // If this is not the first allocation, inform the user.
            if (vertex_count != 0)
            {
                LOG_INFO("Line buffer can hold %d vertices but %d are needed, resizing the buffer to fit %d vertices.", vertex_count, index + 2, new_vertex_count);
            }

            m_lines_index_depth_off = numeric_limits<uint32_t>::max(); // max because it's incremented below.
            m_lines_index_depth_on  = (new_vertex_count / 2) - 1;      // -1 because it's incremented below.

            m_line_vertices.reserve(new_vertex_count);
            m_line_vertices.resize(new_vertex_count);

            m_lines_duration.reserve(new_vertex_count);
            m_lines_duration.resize(new_vertex_count);
        }

        // Write lines.
        {
            index++;
            m_line_vertices[index]  = RHI_Vertex_PosCol(from, color_from);
            m_lines_duration[index] = duration;

            index++;
            m_line_vertices[index]  = RHI_Vertex_PosCol(to, color_to);
            m_lines_duration[index] = duration;
        }
    }

    void Renderer::DrawTriangle(const Math::Vector3& v0, const Math::Vector3& v1, const Math::Vector3& v2, const Math::Vector4& color /*= DEBUG_COLOR*/, const float duration /*= 0.0f*/, bool depth /*= true*/)
    {
        DrawLine(v0, v1, color, color, duration, depth);
        DrawLine(v1, v2, color, color, duration, depth);
        DrawLine(v2, v0, color, color, duration, depth);
    }

    void Renderer::DrawRectangle(const Math::Rectangle& rectangle, const Math::Vector4& color /*= DebugColor*/, const float duration /*= 0.0f*/, bool depth /*= true*/)
    {
        const float cam_z = m_camera->GetTransform()->GetPosition().z + m_camera->GetNearPlane() + 5.0f;

        DrawLine(Vector3(rectangle.left,    rectangle.top,      cam_z), Vector3(rectangle.right, rectangle.top,    cam_z), color, color, duration, depth);
        DrawLine(Vector3(rectangle.right,   rectangle.top,      cam_z), Vector3(rectangle.right, rectangle.bottom, cam_z), color, color, duration, depth);
        DrawLine(Vector3(rectangle.right,   rectangle.bottom,   cam_z), Vector3(rectangle.left,  rectangle.bottom, cam_z), color, color, duration, depth);
        DrawLine(Vector3(rectangle.left,    rectangle.bottom,   cam_z), Vector3(rectangle.left,  rectangle.top,    cam_z), color, color, duration, depth);
    }

    void Renderer::DrawBox(const BoundingBox& box, const Vector4& color, const float duration /*= 0.0f*/, const bool depth /*= true*/)
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

    void Renderer::DrawCircle(const Math::Vector3& center, const Math::Vector3& axis, const float radius, const uint32_t segment_count, const Math::Vector4& color /*= DEBUG_COLOR*/, const float duration /*= 0.0f*/, const bool depth /*= true*/)
    {
        if (radius <= 0.0f || segment_count <= 0)
            return;

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

    void Renderer::Lines_PostMain(const double delta_time)
    {
        // TODO
        // Remove lines which have expired

        //uint32_t end = static_cast<uint32_t>(m_lines_depth_disabled_duration.size());
        //for (uint32_t i = 0; i < end; i++)
        //{
        //    m_lines_depth_disabled_duration[i] -= static_cast<float>(delta_time);
        //
        //    if (m_lines_depth_disabled_duration[i] <= 0.0f)
        //    {
        //        m_lines_depth_disabled_duration.erase(m_lines_depth_disabled_duration.begin() + i);
        //        m_lines_depth_disabled.erase(m_lines_depth_disabled.begin() + i); 
        //        i--;
        //        end--;
        //    }
        //}
        //
        //end = static_cast<uint32_t>(m_lines_depth_enabled_duration.size());
        //for (uint32_t i = 0; i < end; i++)
        //{
        //    m_lines_depth_enabled_duration[i] -= static_cast<float>(delta_time);
        //
        //    if (m_lines_depth_enabled_duration[i] <= 0.0f)
        //    {
        //        m_lines_depth_enabled_duration.erase(m_lines_depth_enabled_duration.begin() + i);
        //        m_lines_depth_enabled.erase(m_lines_depth_enabled.begin() + i);
        //        i--;
        //        end--;
        //    }
        //}
    }

    void Renderer::Lines_PreMain()
    {
        // Generate lines for debug primitives supported by the renderer
        m_lines_index_depth_off = numeric_limits<uint32_t>::max(); // max +1 will wrap it to 0.
        m_lines_index_depth_on  = (static_cast<uint32_t>(m_line_vertices.size()) / 2) - 1; // -1 because +1 will make it go to size / 2.

        // Picking ray
        if (m_options & Render_Debug_PickingRay)
        {
            const auto& ray = m_camera->GetPickingRay();
            DrawLine(ray.GetStart(), ray.GetStart() + ray.GetDirection() * m_camera->GetFarPlane(), Vector4(0, 1, 0, 1));
        }
        
        // Lights
        if (m_options & Render_Debug_Lights)
        {
            auto& lights = m_entities[Renderer_ObjectType::Light];
            for (const auto& entity : lights)
            {
                const Entity* entity_selected = m_context->GetSubsystem<World>()->GetTransformHandle()->GetSelectedEntity();
                if (entity_selected && entity_selected->GetObjectId() == entity->GetObjectId())
                { 
                    Light* light = entity->GetComponent<Light>();
        
                    if (light->GetLightType() == LightType::Directional)
                    {
                        Vector3 pos_start = light->GetTransform()->GetPosition();
                        Vector3 pos_end   = -pos_start;
                        DrawLine(pos_start, pos_end);
        
                    }
                    else if (light->GetLightType() == LightType::Point)
                    {
                        Vector3 center          = light->GetTransform()->GetPosition();
                        float radius            = light->GetRange();
                        uint32_t segment_count  = 64;
        
                        DrawCircle(center, Vector3::Up, radius, segment_count);
                        DrawCircle(center, Vector3::Right, radius, segment_count);
                        DrawCircle(center, Vector3::Forward, radius, segment_count);
                    }
                    else if (light->GetLightType() == LightType::Spot)
                    {
                        // tan(angle) = opposite/adjacent
                        // opposite = adjacent * tan(angle)
                        float opposite  = light->GetRange() * Math::Helper::Tan(light->GetAngle());
        
                        Vector3 pos_end_center = light->GetTransform()->GetForward() * light->GetRange();
                        Vector3 pos_end_up     = pos_end_center + light->GetTransform()->GetUp()    * opposite;
                        Vector3 pos_end_right  = pos_end_center + light->GetTransform()->GetRight() * opposite;
                        Vector3 pos_end_down   = pos_end_center + light->GetTransform()->GetDown()  * opposite;
                        Vector3 pos_end_left   = pos_end_center + light->GetTransform()->GetLeft()  * opposite;
        
                        Vector3 pos_start = light->GetTransform()->GetPosition();
                        DrawLine(pos_start, pos_start + pos_end_center);
                        DrawLine(pos_start, pos_start + pos_end_up);
                        DrawLine(pos_start, pos_start + pos_end_right);
                        DrawLine(pos_start, pos_start + pos_end_down);
                        DrawLine(pos_start, pos_start + pos_end_left);
                    }
                }
            }
        }
        
        // AABBs
        if (m_options & Render_Debug_Aabb)
        {
            for (const auto& entity : m_entities[Renderer_ObjectType::GeometryOpaque])
            {
                if (auto renderable = entity->GetRenderable())
                {
                    DrawBox(renderable->GetAabb(), Vector4(0.41f, 0.86f, 1.0f, 1.0f));
                }
            }
        
            for (const auto& entity : m_entities[Renderer_ObjectType::GeometryTransparent])
            {
                if (auto renderable = entity->GetRenderable())
                {
                    DrawBox(renderable->GetAabb(), Vector4(0.41f, 0.86f, 1.0f, 1.0f));
                }
            }
        }
    }
}
