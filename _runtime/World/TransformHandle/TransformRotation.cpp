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
#include "Spartan.h"
#include "TransformRotation.h"
#include "../../Input/Input.h"
#include "../Components/Transform.h"
//===================================

//= NAMESPACES ===============
using namespace std;
using namespace Spartan::Math;
//============================

namespace Spartan
{
    TransformRotation::TransformRotation(Context* context) : TransformOperator(context, TransformHandleType::Rotation)
    {
        // Create an axis for each axis of control and fourth axis which control all of them
        m_handle_x = TransformOperatorAxis(m_type, Vector3::Right, m_context);
        m_handle_y = TransformOperatorAxis(m_type, Vector3::Up, m_context);
        m_handle_z = TransformOperatorAxis(m_type, Vector3::Forward, m_context);

        m_offset_handle_axes_from_center = false;
    }

    void TransformRotation::InteresectionTest(const Math::Ray& mouse_ray)
    {
        const float circle_radius    = m_handle_x.m_scale.Length() * 5.0f;
        const float circle_thickness = 0.05f;

        // Construct 3 planes, 1 for each axis
        const Plane plane_x = Plane(m_handle_x.m_axis, 0.0f);
        const Plane plane_y = Plane(m_handle_y.m_axis, 0.0f);
        const Plane plane_z = Plane(m_handle_z.m_axis, 0.0f);

        // Find the intersection point on those planes
        Vector3 plane_x_intersection_point = Vector3::Infinity; mouse_ray.HitDistance(plane_x, &plane_x_intersection_point);
        Vector3 plane_y_intersection_point = Vector3::Infinity; mouse_ray.HitDistance(plane_y, &plane_y_intersection_point);
        Vector3 plane_z_intersection_point = Vector3::Infinity; mouse_ray.HitDistance(plane_z, &plane_z_intersection_point);

        // Compute the distance of the intersection point form the plane's center
        const float handle_x_distance = plane_x_intersection_point.Distance(m_handle_x.m_position);
        const float handle_y_distance = plane_y_intersection_point.Distance(m_handle_y.m_position);
        const float handle_z_distance = plane_z_intersection_point.Distance(m_handle_z.m_position);

        // Test if the ray intersects any of the handles
        m_handle_x_intersected = handle_x_distance >= (circle_radius - circle_thickness) && handle_x_distance < (circle_radius + circle_thickness);
        m_handle_y_intersected = handle_y_distance >= (circle_radius - circle_thickness) && handle_y_distance < (circle_radius + circle_thickness);
        m_handle_z_intersected = handle_z_distance >= (circle_radius - circle_thickness) && handle_z_distance < (circle_radius + circle_thickness);

        // Capture initial intersection point and axis (to be able to calculate future deltas)
        if (m_input->GetKeyDown(KeyCode::Click_Left))
        {
            if (m_handle_x_intersected)
            {
                m_initial_direction = (plane_x_intersection_point - m_position).Normalized();
                m_intersection_axis = m_handle_x.m_axis;
            }
            else if (m_handle_y_intersected)
            {
                m_initial_direction = (plane_y_intersection_point - m_position).Normalized();
                m_intersection_axis = m_handle_y.m_axis;
            }
            else if (m_handle_z_intersected)
            {
                m_initial_direction = (plane_z_intersection_point - m_position).Normalized();
                m_intersection_axis = m_handle_z.m_axis;
            }
        }
    }

    void TransformRotation::ComputeDelta(const Math::Ray& mouse_ray, const Camera* camera)
    {
        const Plane plane = Plane(m_intersection_axis, 0.0f);
        Vector3 plane_intersection_point = Vector3::Infinity;
        mouse_ray.HitDistance(plane, &plane_intersection_point);

        Vector3 dir = (plane_intersection_point - m_position).Normalized();
        float angle = Vector3::Dot(dir, m_initial_direction);

        bool is_first_editing_run = m_handle_x.m_is_first_editing_run || m_handle_y.m_is_first_editing_run || m_handle_z.m_is_first_editing_run;
        m_angle_delta             = !is_first_editing_run ? (angle - m_previous_angle) : 0.0f;
        m_previous_angle          = angle;
    }

    void TransformRotation::MapToTransform(Transform* transform, const TransformHandleSpace space)
    {
        SP_ASSERT(transform != nullptr);

        Quaternion rotation_delta = Quaternion::FromEulerAngles(Vector3(m_angle_delta * Math::Helper::RAD_TO_DEG) * m_intersection_axis);
        
        if (space == TransformHandleSpace::World)
        {
            Quaternion rotation_new = transform->GetRotation() * rotation_delta;
            transform->SetRotation(rotation_new);
        }
        else
        {
            Quaternion rotation_new = transform->GetRotationLocal() * rotation_delta;
            transform->SetRotationLocal(rotation_new);
        }
    }
}
