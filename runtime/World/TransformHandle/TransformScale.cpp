/*
Copyright(c) 2016-2022 Panos Karabelas

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

//= INCLUDES =======================
#include "pch.h"
#include "TransformScale.h"
#include "../Components/Camera.h"
#include "../Components/Transform.h"
#include "../Rendering/Geometry.h"
#include "../Rendering/Mesh.h"
//==================================

//= NAMESPACES ===============
using namespace std;
using namespace Spartan::Math;
//============================

namespace Spartan
{
    TransformScale::TransformScale(Context* context) : TransformOperator(context, TransformHandleType::Scale)
    {
        // Create model
        vector<RHI_Vertex_PosTexNorTan> vertices;
        vector<uint32_t> indices;

        Geometry::CreateCube(&vertices, &indices);

        m_axis_mesh = make_unique<Mesh>(m_context);
        m_axis_mesh->AddIndices(indices);
        m_axis_mesh->AddVertices(vertices);
        m_axis_mesh->CreateGpuBuffers();
        m_axis_mesh->ComputeAabb();

        // Create an axis for each axis of control and fourth axis which control all of them
        m_handle_x   = TransformOperatorAxis(m_type, Vector3::Right, m_context);
        m_handle_y   = TransformOperatorAxis(m_type, Vector3::Up, m_context);
        m_handle_z   = TransformOperatorAxis(m_type, Vector3::Forward, m_context);
        m_handle_xyz = TransformOperatorAxis(m_type, Vector3::One, m_context);

        // Create bounding boxes for the handles, based on the vertices used
        m_handle_x.m_box   = BoundingBox(vertices.data(), static_cast<uint32_t>(vertices.size()));
        m_handle_y.m_box   = m_handle_x.m_box;
        m_handle_z.m_box   = m_handle_x.m_box;
        m_handle_xyz.m_box = m_handle_x.m_box;

        m_offset_handle_axes_from_center = true;
    }

    void TransformScale::InteresectionTest(const Math::Ray& mouse_ray)
    {
        // Test if the ray intersects any of the handles
        m_handle_x_intersected   = mouse_ray.HitDistance(m_handle_x.m_box_transformed)   != Math::Helper::INFINITY_;
        m_handle_y_intersected   = mouse_ray.HitDistance(m_handle_y.m_box_transformed)   != Math::Helper::INFINITY_;
        m_handle_z_intersected   = mouse_ray.HitDistance(m_handle_z.m_box_transformed)   != Math::Helper::INFINITY_;
        m_handle_xyz_intersected = mouse_ray.HitDistance(m_handle_xyz.m_box_transformed) != Math::Helper::INFINITY_;
    }

    static Vector3 GetMousePointOnAxis(const Camera* camera, const Ray& mouse_ray, const TransformOperatorAxis& axis_handle)
    {
        // Find the intersection point between the camera plane and the mouse ray.
        Vector3 normal             = camera->GetTransform()->GetForward();
        float distance_from_origin = 0.0f;
        Plane screen_plane         = Plane(normal, distance_from_origin);
        Vector3 plane_interesection_point = Vector3::Zero;
        mouse_ray.HitDistance(screen_plane, &plane_interesection_point);

        // Find the point on the z-axis which is the closest to the ray-plane intersection point.
        Vector3 closest_point = Vector3::Zero;
        Ray(Vector3::Zero, axis_handle.m_axis).Distance(plane_interesection_point, closest_point);

        return closest_point;
    }

    void TransformScale::ComputeDelta(const Math::Ray& mouse_ray, const Camera* camera)
    {
        Vector3 mouse_point_on_axis = Vector3::Zero;
        float sign = 1.0f;

        if (m_handle_x.m_is_editing)
        {
            mouse_point_on_axis = GetMousePointOnAxis(camera, mouse_ray, m_handle_x);
        }
        else if (m_handle_y.m_is_editing)
        {
            mouse_point_on_axis = GetMousePointOnAxis(camera, mouse_ray, m_handle_y);
        }
        else if (m_handle_z.m_is_editing)
        {
            mouse_point_on_axis = GetMousePointOnAxis(camera, mouse_ray, m_handle_z);
        }
        else if (m_handle_xyz.m_is_editing)
        {
            float x = GetMousePointOnAxis(camera, mouse_ray, m_handle_x).x;
            float y = GetMousePointOnAxis(camera, mouse_ray, m_handle_y).y;
            float z = GetMousePointOnAxis(camera, mouse_ray, m_handle_z).z;

            mouse_point_on_axis.x = Math::Helper::Max3(x, y, z);
            mouse_point_on_axis.y = mouse_point_on_axis.x;
            mouse_point_on_axis.z = mouse_point_on_axis.x;

            sign = static_cast<float>(Math::Helper::Sign(Vector3::Dot(mouse_ray.GetDirection(), Vector3::Right)));
            LOG_INFO("%f", sign);
        }

        bool is_first_editing_run      = m_handle_x.m_is_first_editing_run || m_handle_y.m_is_first_editing_run || m_handle_z.m_is_first_editing_run || m_handle_xyz.m_is_first_editing_run;
        m_delta                        = !is_first_editing_run ? (mouse_point_on_axis - m_previous_mouse_point_on_axis) : 0.0f;
        m_previous_mouse_point_on_axis = mouse_point_on_axis;
    }

    void TransformScale::MapToTransform(Transform* transform, const TransformHandleSpace space)
    {
        SP_ASSERT(transform != nullptr);

        if (space == TransformHandleSpace::World)
        {
            transform->SetScale(transform->GetScale() + m_delta);
        }
        else
        {
            transform->SetScaleLocal(transform->GetScaleLocal() + m_delta);
        }
    }
}
