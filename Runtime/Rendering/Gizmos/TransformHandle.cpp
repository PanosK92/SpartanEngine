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

//= INCLUDES =================================
#include "Spartan.h"
#include "TransformHandle.h"
#include "../Renderer.h"
#include "../../Input/Input.h"
#include "../../World/Entity.h"
#include "../../World/Components/Camera.h"
#include "../../World/Components/Transform.h"
#include "../../World/Components/Renderable.h"
//============================================

//= NAMESPACES ===============
using namespace std;
using namespace Spartan::Math;
//============================

namespace Spartan
{
    TransformHandle::TransformHandle(Context* context, const TransformHandleType transform_handle_type)
    {
        m_context   = context;
        m_type      = transform_handle_type;
        m_renderer  = context->GetSubsystem<Renderer>();
        m_input     = context->GetSubsystem<Input>();
    }

    bool TransformHandle::Tick(const TransformHandleSpace space, Entity* entity, Camera* camera, const float handle_size, const float handle_speed)
    {
        if (!entity || !camera)
        {
            LOG_ERROR_INVALID_PARAMETER();
            return false;
        }

        ReflectEntityTransform(space, entity, camera, handle_size);

        // Allow editing only when the camera is not fps controlled
        if (!camera->IsFpsControlled())
        {
            // Create ray starting from the camera position and pointing towards where the mouse is pointing
            const Vector2 mouse_pos             = m_input->GetMousePosition();
            const RHI_Viewport& viewport        = m_renderer->GetViewport();
            const Vector2& editor_offset        = m_renderer->GetViewportOffset();
            const Vector2 mouse_pos_relative    = mouse_pos - editor_offset;
            const Vector3 ray_start             = camera->GetTransform()->GetPosition();
            Vector3 ray_end                     = camera->Unproject(mouse_pos_relative);
            Ray camera_to_mouse                 = Ray(ray_start, ray_end);

            // Check if the mouse ray hits and of the handle axes
            InteresectionTest(camera_to_mouse);

            UpdateHandleAxesState();
            UpdateHandleAxesMouseDelta(camera, ray_end, handle_speed);

            // Use detected mouse delta to manipulate the entity's transform
            m_handle_x.ApplyDeltaToTransform(entity->GetTransform());
            m_handle_y.ApplyDeltaToTransform(entity->GetTransform());
            m_handle_z.ApplyDeltaToTransform(entity->GetTransform());
            m_handle_xyz.ApplyDeltaToTransform(entity->GetTransform());
        }

        // Allow the handles to draw any primitives
        Vector3 center = m_handle_xyz.m_position;
        m_handle_x.DrawPrimitives(center);
        m_handle_y.DrawPrimitives(center);
        m_handle_z.DrawPrimitives(center);
        m_handle_xyz.DrawPrimitives(center);

        return m_handle_x.m_is_editing || m_handle_y.m_is_editing || m_handle_z.m_is_editing || m_handle_xyz.m_is_editing;
    }

    void TransformHandle::ReflectEntityTransform(const TransformHandleSpace space, Entity* entity, Camera* camera, const float handle_size)
    {
        // Get entity's components
        Transform* entity_transform     = entity->GetTransform();               // Transform alone is not enough
        Renderable* entity_renderable   = entity->GetComponent<Renderable>();   // Bounding box is also needed as some meshes are not defined around P(0,0,0)

        // Acquire entity's transformation data (local or world space)
        const Vector3& aabb_center          = entity_renderable                      ? entity_renderable->GetAabb().GetCenter()   : entity_transform->GetPositionLocal();
        const Quaternion& entity_rotation   = (space == TransformHandleSpace::World) ? entity_transform->GetRotation()            : entity_transform->GetRotationLocal();
        const Vector3& right                = (space == TransformHandleSpace::World) ? Vector3::Right                             : entity_rotation * Vector3::Right;
        const Vector3& up                   = (space == TransformHandleSpace::World) ? Vector3::Up                                : entity_rotation * Vector3::Up;
        const Vector3& forward              = (space == TransformHandleSpace::World) ? Vector3::Forward                           : entity_rotation * Vector3::Forward;

        // Compute scale
        const float distance_to_camera   = camera ? (camera->GetTransform()->GetPosition() - (aabb_center)).Length() : 0.0f;
        const float handle_scale         = distance_to_camera / (1.0f / handle_size);
        const float handle_distance      = distance_to_camera / (1.0f / 0.1f);

        // Set position for each axis handle
        m_handle_x.m_position   = aabb_center;
        m_handle_y.m_position   = aabb_center;
        m_handle_z.m_position   = aabb_center;
        m_handle_xyz.m_position = aabb_center;
        if (m_offset_handle_axes_from_center)
        {
            m_handle_x.m_position += right * handle_distance;
            m_handle_y.m_position += up * handle_distance;
            m_handle_z.m_position += forward * handle_distance;
        }

        // Set rotation for each axis handle
        m_handle_x.m_rotation     = Quaternion::FromEulerAngles(0.0f, 0.0f, -90.0f);
        m_handle_y.m_rotation     = Quaternion::FromEulerAngles(0.0f, 90.0f, 0.0f);
        m_handle_z.m_rotation     = Quaternion::FromEulerAngles(90.0f, 0.0f, 0.0f);

        // Set scale for each axis handle
        m_handle_x.m_scale      = handle_scale;
        m_handle_y.m_scale      = handle_scale;
        m_handle_z.m_scale      = handle_scale;
        m_handle_xyz.m_scale    = handle_scale;

        // Update transforms
        m_handle_x.UpdateTransform();
        m_handle_y.UpdateTransform();
        m_handle_z.UpdateTransform();
        m_handle_xyz.UpdateTransform();
    }

    void TransformHandle::UpdateHandleAxesState()
    {
        // Mark a handle as hovered, only if it's the only hovered handle (during the previous frame)
        m_handle_x.m_is_hovered     = m_handle_x_intersected    && !(m_handle_y.m_is_hovered || m_handle_z.m_is_hovered);
        m_handle_y.m_is_hovered     = m_handle_y_intersected    && !(m_handle_x.m_is_hovered || m_handle_z.m_is_hovered);
        m_handle_z.m_is_hovered     = m_handle_z_intersected    && !(m_handle_x.m_is_hovered || m_handle_y.m_is_hovered);
        m_handle_xyz.m_is_hovered   = m_handle_xyz_intersected  && !(m_handle_x.m_is_hovered || m_handle_y.m_is_hovered || m_handle_z.m_is_hovered);
        
        // Disable handle if one of the others is active (affects the color)
        m_handle_x.m_is_disabled    = !m_handle_x.m_is_editing     && (m_handle_y.m_is_editing || m_handle_z.m_is_editing || m_handle_xyz.m_is_editing);
        m_handle_y.m_is_disabled    = !m_handle_y.m_is_editing     && (m_handle_x.m_is_editing || m_handle_z.m_is_editing || m_handle_xyz.m_is_editing);
        m_handle_z.m_is_disabled    = !m_handle_z.m_is_editing     && (m_handle_x.m_is_editing || m_handle_y.m_is_editing || m_handle_xyz.m_is_editing);
        m_handle_xyz.m_is_disabled  = !m_handle_xyz.m_is_editing   && (m_handle_x.m_is_editing || m_handle_y.m_is_editing || m_handle_z.m_is_editing);
    }

    void TransformHandle::UpdateHandleAxesMouseDelta(Camera* camera, const Vector3& ray_end, const float handle_speed)
    {
        // Track delta
        m_ray_previous          = m_ray_current != Vector3::Zero ? m_ray_current : ray_end; // ignore big delta in the first run
        m_ray_current           = ray_end;
        const Vector3 delta     = m_ray_current - m_ray_previous;
        const float delta_xyz   = delta.Length();
        
        // If the delta reached infinity, ignore the input as it will result in NaN position.
        // This can happen if the transformation is happening extremely close the camera.
        if (isinf(delta_xyz))
            return;

        // Updated handles with delta
        m_handle_x.m_delta    = delta_xyz * Helper::Sign(delta.x) * handle_speed;
        m_handle_y.m_delta    = delta_xyz * Helper::Sign(delta.y) * handle_speed;
        m_handle_z.m_delta    = delta_xyz * Helper::Sign(delta.z) * handle_speed;
        m_handle_xyz.m_delta  = m_handle_x.m_delta + m_handle_y.m_delta + m_handle_z.m_delta;
    }

    const Matrix& TransformHandle::GetTransform(const Vector3& axis) const
    {
        if (axis == Vector3::Right)
            return m_handle_x.m_transform;

        if (axis == Vector3::Up)
            return m_handle_y.m_transform;

        if (axis == Vector3::Forward)
            return m_handle_z.m_transform;

        return m_handle_xyz.m_transform;
    }

    const Vector3& TransformHandle::GetColor(const Vector3& axis) const
    {
        if (axis == Vector3::Right)
            return m_handle_x.GetColor();

        if (axis == Vector3::Up)
            return m_handle_y.GetColor();

        if (axis == Vector3::Forward)
            return m_handle_z.GetColor();

        return m_handle_xyz.GetColor();
    }

    const RHI_VertexBuffer* TransformHandle::GetVertexBuffer()
    {
        return m_axis_model ? m_axis_model->GetVertexBuffer() : nullptr;
    }

    const RHI_IndexBuffer* TransformHandle::GetIndexBuffer()
    {
        return m_axis_model ? m_axis_model->GetIndexBuffer() : nullptr;
    }

    bool TransformHandle::IsEditing() const
    {
        if (m_handle_x.m_type != TransformHandleType::Unknown && m_handle_x.m_is_editing)
            return true;

        if (m_handle_y.m_type != TransformHandleType::Unknown && m_handle_y.m_is_editing)
            return true;

        if (m_handle_z.m_type != TransformHandleType::Unknown && m_handle_z.m_is_editing)
            return true;

        if (m_handle_xyz.m_type != TransformHandleType::Unknown && m_handle_xyz.m_is_editing)
            return true;

        return false;
    }
}
