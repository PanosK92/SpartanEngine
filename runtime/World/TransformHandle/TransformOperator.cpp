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

//= INCLUDES =================================
#include "pch.h"
#include "TransformOperator.h"
#include "../../Input/Input.h"
#include "../../World/Entity.h"
#include "../../World/Components/Camera.h"
#include "../../World/Components/Transform.h"
#include "../../World/Components/Renderable.h"
#include "../../Rendering/Mesh.h"
#include "../../Rendering/Renderer.h"
//============================================

//= NAMESPACES ===============
using namespace std;
using namespace Spartan::Math;
//============================

namespace Spartan
{
    TransformOperator::TransformOperator(Context* context, const TransformHandleType transform_handle_type)
    {
        m_context  = context;
        m_type     = transform_handle_type;
        m_renderer = context->GetSystem<Renderer>();
        m_input    = context->GetSystem<Input>();
    }

    void TransformOperator::Tick(const TransformHandleSpace space, Entity* entity, Camera* camera, const float handle_size)
    {
        SP_ASSERT(entity != nullptr);
        SP_ASSERT(camera != nullptr);

        // Reflect entity transform
        SnapToTransform(space, entity, camera, handle_size);

        // Allow editing only when the camera is not fps controlled
        if (!camera->IsFpsControlled())
        {
            // Create ray starting from the camera position and pointing towards where the mouse is pointing
            Vector3 ray_start     = camera->GetTransform()->GetPosition();
            Vector3 ray_direction = camera->ScreenToWorldCoordinates(m_input->GetMousePositionRelativeToEditorViewport(), 1.0f);
            Ray mouse_ray         = Ray(ray_start, ray_direction);

            // Check if the mouse ray hits and of the handle axes
            InteresectionTest(mouse_ray);

            // Update handle states
            {
                // Mark a handle as hovered, only if it's the only hovered handle (during the previous frame).
                m_handle_x.m_is_hovered   = m_handle_x_intersected   && !(m_handle_y.m_is_hovered || m_handle_z.m_is_hovered);
                m_handle_y.m_is_hovered   = m_handle_y_intersected   && !(m_handle_x.m_is_hovered || m_handle_z.m_is_hovered);
                m_handle_z.m_is_hovered   = m_handle_z_intersected   && !(m_handle_x.m_is_hovered || m_handle_y.m_is_hovered);
                m_handle_xyz.m_is_hovered = m_handle_xyz_intersected && !(m_handle_x.m_is_hovered || m_handle_y.m_is_hovered || m_handle_z.m_is_hovered);
                
                // Disable handle if one of the others is active (disabling turns the color of the handle to grey).
                m_handle_x.m_is_disabled   = !m_handle_x.m_is_editing   && (m_handle_y.m_is_editing || m_handle_z.m_is_editing || m_handle_xyz.m_is_editing);
                m_handle_y.m_is_disabled   = !m_handle_y.m_is_editing   && (m_handle_x.m_is_editing || m_handle_z.m_is_editing || m_handle_xyz.m_is_editing);
                m_handle_z.m_is_disabled   = !m_handle_z.m_is_editing   && (m_handle_x.m_is_editing || m_handle_y.m_is_editing || m_handle_xyz.m_is_editing);
                m_handle_xyz.m_is_disabled = !m_handle_xyz.m_is_editing && (m_handle_x.m_is_editing || m_handle_y.m_is_editing || m_handle_z.m_is_editing);

                // Keep old editing state
                m_handle_x.m_is_editing_previous   = m_handle_x.m_is_editing;
                m_handle_y.m_is_editing_previous   = m_handle_y.m_is_editing;
                m_handle_z.m_is_editing_previous   = m_handle_z.m_is_editing;
                m_handle_xyz.m_is_editing_previous = m_handle_xyz.m_is_editing;

                // Detect if any of the handles should enter an editing state (on right click pressed)
                bool mouse_down = m_input->GetKeyDown(KeyCode::Click_Left);
                m_handle_x.m_is_editing   = (m_handle_x.m_is_hovered && mouse_down)   ? true : m_handle_x.m_is_editing;
                m_handle_y.m_is_editing   = (m_handle_y.m_is_hovered && mouse_down)   ? true : m_handle_y.m_is_editing;
                m_handle_z.m_is_editing   = (m_handle_z.m_is_hovered && mouse_down)   ? true : m_handle_z.m_is_editing;
                m_handle_xyz.m_is_editing = (m_handle_xyz.m_is_hovered && mouse_down) ? true : m_handle_xyz.m_is_editing;

                // Detect if any of the handles should exit the editing state (on left click released).
                bool mouse_up = m_input->GetKeyUp(KeyCode::Click_Left);
                m_handle_x.m_is_editing   = (m_handle_x.m_is_editing && mouse_up)   ? false : m_handle_x.m_is_editing;
                m_handle_y.m_is_editing   = (m_handle_y.m_is_editing && mouse_up)   ? false : m_handle_y.m_is_editing;
                m_handle_z.m_is_editing   = (m_handle_z.m_is_editing && mouse_up)   ? false : m_handle_z.m_is_editing;
                m_handle_xyz.m_is_editing = (m_handle_xyz.m_is_editing && mouse_up) ? false : m_handle_xyz.m_is_editing;

                // Determine if this is the first editing run
                m_handle_x.m_is_first_editing_run   = !m_handle_x.m_is_editing_previous   && m_handle_x.m_is_editing;
                m_handle_y.m_is_first_editing_run   = !m_handle_y.m_is_editing_previous   && m_handle_y.m_is_editing;
                m_handle_z.m_is_first_editing_run   = !m_handle_z.m_is_editing_previous   && m_handle_z.m_is_editing;
                m_handle_xyz.m_is_first_editing_run = !m_handle_xyz.m_is_editing_previous && m_handle_xyz.m_is_editing;
            }

            // Compute mouse delta
            if (m_handle_x.m_is_editing || m_handle_y.m_is_editing || m_handle_z.m_is_editing || m_handle_xyz.m_is_editing)
            {
                ComputeDelta(mouse_ray, camera);

                // Map computed delta to the entity's transform
                MapToTransform(entity->GetTransform(), space);
            }
        }

        // Allow the handles to draw any primitives
        Vector3 center = m_handle_xyz.m_position;
        m_handle_x.DrawPrimitives(center);
        m_handle_y.DrawPrimitives(center);
        m_handle_z.DrawPrimitives(center);
        m_handle_xyz.DrawPrimitives(center);
    }

    void TransformOperator::SnapToTransform(const TransformHandleSpace space, Entity* entity, Camera* camera, const float handle_size)
    {
        // Get entity's components
        Transform* entity_transform   = entity->GetTransform();             // Transform alone is not enough
        Renderable* entity_renderable = entity->GetComponent<Renderable>(); // Bounding box is also needed as some meshes are not defined around P(0,0,0)

        // Acquire entity's transformation data (local or world space)
        const Vector3& center             = entity_renderable                      ? entity_renderable->GetAabb().GetCenter() : entity_transform->GetPositionLocal();
        const Quaternion& entity_rotation = (space == TransformHandleSpace::World) ? entity_transform->GetRotation()          : entity_transform->GetRotationLocal();
        const Vector3& right              = (space == TransformHandleSpace::World) ? Vector3::Right                           : entity_rotation * Vector3::Right;
        const Vector3& up                 = (space == TransformHandleSpace::World) ? Vector3::Up                              : entity_rotation * Vector3::Up;
        const Vector3& forward            = (space == TransformHandleSpace::World) ? Vector3::Forward                         : entity_rotation * Vector3::Forward;

        // Compute scale
        const float distance_to_camera = camera ? (camera->GetTransform()->GetPosition() - (center)).Length() : 0.0f;
        const float handle_scale       = distance_to_camera / (1.0f / handle_size);
        m_offset_handle_from_center    = distance_to_camera / (1.0f / 0.1f);

        // Set position for each axis handle
        m_handle_x.m_position   = center;
        m_handle_y.m_position   = center;
        m_handle_z.m_position   = center;
        m_handle_xyz.m_position = center;
        if (m_offset_handle_axes_from_center)
        {
            m_handle_x.m_position += right   * m_offset_handle_from_center;
            m_handle_y.m_position += up      * m_offset_handle_from_center;
            m_handle_z.m_position += forward * m_offset_handle_from_center;
        }

        // Set rotation for each axis handle
        m_handle_x.m_rotation = Quaternion::FromEulerAngles(0.0f, 0.0f, -90.0f);
        m_handle_y.m_rotation = Quaternion::FromEulerAngles(0.0f, 90.0f, 0.0f);
        m_handle_z.m_rotation = Quaternion::FromEulerAngles(90.0f, 0.0f, 0.0f);

        // Set scale for each axis handle
        m_handle_x.m_scale   = handle_scale;
        m_handle_y.m_scale   = handle_scale;
        m_handle_z.m_scale   = handle_scale;
        m_handle_xyz.m_scale = handle_scale;

        // Update transforms
        m_handle_x.UpdateTransform();
        m_handle_y.UpdateTransform();
        m_handle_z.UpdateTransform();
        m_handle_xyz.UpdateTransform();
    }

    const Matrix& TransformOperator::GetTransform(const Vector3& axis) const
    {
        if (axis == Vector3::Right)
            return m_handle_x.m_transform;

        if (axis == Vector3::Up)
            return m_handle_y.m_transform;

        if (axis == Vector3::Forward)
            return m_handle_z.m_transform;

        return m_handle_xyz.m_transform;
    }

    const Vector3& TransformOperator::GetColor(const Vector3& axis) const
    {
        if (axis == Vector3::Right)
            return m_handle_x.GetColor();

        if (axis == Vector3::Up)
            return m_handle_y.GetColor();

        if (axis == Vector3::Forward)
            return m_handle_z.GetColor();

        return m_handle_xyz.GetColor();
    }

    const RHI_VertexBuffer* TransformOperator::GetVertexBuffer()
    {
        return m_axis_mesh? m_axis_mesh->GetVertexBuffer() : nullptr;
    }

    const RHI_IndexBuffer* TransformOperator::GetIndexBuffer()
    {
        return m_axis_mesh ? m_axis_mesh->GetIndexBuffer() : nullptr;
    }

    bool TransformOperator::IsEditing() const
    {
        SP_ASSERT(m_handle_x.m_type != TransformHandleType::Unknown);

        return m_handle_x.m_is_editing || m_handle_y.m_is_editing || m_handle_y.m_is_editing || m_handle_xyz.m_is_editing;
    }

    bool TransformOperator::IsHovered() const
    {
        if (m_handle_x.m_type != TransformHandleType::Unknown && m_handle_x.m_is_hovered)
            return true;

        if (m_handle_y.m_type != TransformHandleType::Unknown && m_handle_y.m_is_hovered)
            return true;

        if (m_handle_z.m_type != TransformHandleType::Unknown && m_handle_z.m_is_hovered)
            return true;

        if (m_handle_xyz.m_type != TransformHandleType::Unknown && m_handle_xyz.m_is_hovered)
            return true;

        return false;
    }
    
}
