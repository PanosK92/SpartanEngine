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

//= INCLUDES ========================
#include "pch.h"
#include "TransformPosition.h"
#include "../Components/Camera.h"
#include "../Components/Transform.h"
#include "../../Rendering/Model.h"
#include "../../Rendering/Geometry.h"
//===================================

//= NAMESPACES ===============
using namespace std;
using namespace Spartan::Math;
//============================

namespace Spartan
{
    TransformPosition::TransformPosition(Context* context) : TransformOperator(context, TransformHandleType::Position)
    {
        // Create model
        vector<RHI_Vertex_PosTexNorTan> vertices;
        vector<uint32_t> indices;

        Geometry::CreateCone(&vertices, &indices);

        m_axis_model = make_unique<Model>(m_context);
        m_axis_model->AppendGeometry(indices, vertices);
        m_axis_model->UpdateGeometry();

        // Create an axis for each axis of control and fourth axis which control all of them
        m_handle_x = TransformOperatorAxis(m_type, Vector3::Right, m_context);
        m_handle_y = TransformOperatorAxis(m_type, Vector3::Up, m_context);
        m_handle_z = TransformOperatorAxis(m_type, Vector3::Forward, m_context);

        // Create bounding boxes for the handles, based on the vertices used
        m_handle_x.m_box = BoundingBox(vertices.data(), static_cast<uint32_t>(vertices.size()));
        m_handle_y.m_box = m_handle_x.m_box;
        m_handle_z.m_box = m_handle_x.m_box;

        m_offset_handle_axes_from_center = true;
    }

    void TransformPosition::InteresectionTest(const Math::Ray& mouse_ray)
    {
        // Test if the ray intersects any of the handles
        m_handle_x_intersected = mouse_ray.HitDistance(m_handle_x.m_box_transformed) != Math::Helper::INFINITY_;
        m_handle_y_intersected = mouse_ray.HitDistance(m_handle_y.m_box_transformed) != Math::Helper::INFINITY_;
        m_handle_z_intersected = mouse_ray.HitDistance(m_handle_z.m_box_transformed) != Math::Helper::INFINITY_;
    }

    static Vector3 GetIntersectionPointBetweenRayAndCameraPlane(const Camera* camera, const Ray& ray)
    {
        Vector3 normal             = camera->GetTransform()->GetForward();
        float distance_from_origin = 0.0f;
        Plane screen_plane         = Plane(normal, distance_from_origin);

        // Find the intersection point between the plane and the ray.
        Vector3 plane_interesection_point = Vector3::Zero;
        ray.HitDistance(screen_plane, &plane_interesection_point);

        return plane_interesection_point;
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

    void TransformPosition::ComputeDelta(const Math::Ray& mouse_ray, const Camera* camera)
    {
        Vector3 mouse_point_on_axis = Vector3::Zero;
        if (m_handle_x.m_is_editing)
        {
            // Find the closest point to the world space mouse cursor, on a given world axis.
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

        bool is_first_editing_run       = m_handle_x.m_is_first_editing_run || m_handle_y.m_is_first_editing_run || m_handle_z.m_is_first_editing_run;
        m_delta                         = !is_first_editing_run ? (mouse_point_on_axis - m_previous_mouse_point_on_axis) : Vector3::Zero;
        m_previous_mouse_point_on_axis  = mouse_point_on_axis;
    }

    void TransformPosition::MapToTransform(Transform* transform, const TransformHandleSpace space)
    {
        SP_ASSERT(transform != nullptr);

        if (space == TransformHandleSpace::World)
        {
            transform->SetPosition(transform->GetPosition() + m_delta);
        }
        else
        {
            transform->SetPositionLocal(transform->GetPositionLocal() + m_delta);
        }
    }
}
