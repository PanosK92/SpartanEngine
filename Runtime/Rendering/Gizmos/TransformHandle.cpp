/*
Copyright(c) 2016-2020 Panos Karabelas

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
#include "../Model.h"
#include "../Renderer.h"
#include "../../Utilities/Geometry.h"
#include "../../Input/Input.h"
#include "../../World/Entity.h"
#include "../../World/Components/Camera.h"
#include "../../World/Components/Transform.h"
#include "../../World/Components/Renderable.h"
#include "../../RHI/RHI_Vertex.h"
//============================================

//=============================
using namespace std;
using namespace Spartan::Math;
//=============================

namespace Spartan
{
    void TransformHandleAxis::UpdateInput(const TransformHandle_Type type, Transform* transform, Input* input)
    {
        // First press
        if (isHovered && input->GetKeyDown(KeyCode::Click_Left))
        {
            isEditing = true;
        }

        // Editing can happen here
        if (isEditing && input->GetKey(KeyCode::Click_Left))
        {
            if (type == TransformHandle_Position)
            {
                auto position = transform->GetPosition();
                position += isEditing ? delta * axis : 0.0f;
                transform->SetPosition(position);
            }
            else if (type == TransformHandle_Scale)
            {
                auto scale = transform->GetScale();
                scale += isEditing ? delta * axis : 0.0f;
                transform->SetScale(scale);
            }
            else if (type == TransformHandle_Rotation)
            {
                auto rotation = transform->GetRotation().ToEulerAngles();
                const auto speed_multiplier = 10.0f;
                rotation += isEditing ? delta * axis * speed_multiplier : 0.0f;
                transform->SetRotation(Quaternion::FromEulerAngles(rotation));
            }
        }

        // Last press (on release)
        if (isEditing && input->GetKeyUp(KeyCode::Click_Left))
        {
            isEditing = false;
        }
    }

    void TransformHandleAxis::DrawExtra(Renderer* renderer, const Vector3& transform_center) const
    {
        // Draw axis line (connect the handle with the origin of the transform)
        const Vector4 color = Vector4(GetColor(), 1.0f);
        const Vector3 from  = box_transformed.GetCenter();
        const Vector3& to   = transform_center;
        renderer->DrawDebugLine(from, to, color, color, 0.0f, false);
    }

    void TransformHandle::Initialize(const TransformHandle_Type type, Context* context)
    {
        m_type      = type;
        m_context   = context;
        m_renderer  = context->GetSubsystem<Renderer>();
        m_input     = context->GetSubsystem<Input>();

        m_handle_x      = TransformHandleAxis(Vector3::Right, m_context);
        m_handle_y      = TransformHandleAxis(Vector3::Up, m_context);
        m_handle_z      = TransformHandleAxis(Vector3::Forward, m_context);
        m_handle_xyz    = TransformHandleAxis(Vector3::One, m_context);

        m_ray_previous  = Vector3::Zero;
        m_ray_current   = Vector3::Zero;

        // Create position controller
        vector<RHI_Vertex_PosTexNorTan> vertices;
        vector<uint32_t> indices;
        if (m_type == TransformHandle_Position)
        {
            Utility::Geometry::CreateCone(&vertices, &indices);
        }
        else if (m_type == TransformHandle_Scale)
        {
            Utility::Geometry::CreateCube(&vertices, &indices);
        }
        else if (m_type == TransformHandle_Rotation)
        {
            // I am super lazy at this very moment, let's just use a cylinder for now...
            Utility::Geometry::CreateCylinder(&vertices, &indices);
        }
        m_model = make_unique<Model>(m_context);
        m_model->AppendGeometry(indices, vertices);
        m_model->UpdateGeometry();

        // Create bounding boxes for the handles, based on the vertices used
        m_handle_x.box      = BoundingBox(vertices.data(), static_cast<uint32_t>(vertices.size()));
        m_handle_y.box      = m_handle_x.box;
        m_handle_z.box      = m_handle_x.box;
        m_handle_xyz.box    = m_handle_x.box;
    }

    bool TransformHandle::Update(const TransformHandle_Space space, Entity* entity, Camera* camera, const float handle_size, const float handle_speed)
    {
        if (!entity || !camera)
        {
            LOG_ERROR_INVALID_PARAMETER();
            return false;
        }

        // Snap to entity position
        SnapToTransform(space, entity, camera, handle_size);

        // Do hit test
        if (camera)
        {
            // Create ray starting from camera position and pointing towards where the mouse is pointing
            const auto mouse_pos            = m_input->GetMousePosition();
            const auto& viewport            = m_context->GetSubsystem<Renderer>()->GetViewport();
            const auto& editor_offset       = m_context->GetSubsystem<Renderer>()->GetViewportOffset();
            const auto mouse_pos_relative   = mouse_pos - editor_offset;
            const auto ray_start            = camera->GetTransform()->GetPosition();
            auto ray_end                    = camera->Unproject(mouse_pos_relative);
            const auto ray                  = Ray(ray_start, ray_end);

            // Test if ray intersects any of the handles
            const auto hovered_x    = ray.HitDistance(m_handle_x.box_transformed)   != INFINITY;
            const auto hovered_y    = ray.HitDistance(m_handle_y.box_transformed)   != INFINITY;
            const auto hovered_z    = ray.HitDistance(m_handle_z.box_transformed)   != INFINITY;
            const auto hovered_xyz  = ray.HitDistance(m_handle_xyz.box_transformed) != INFINITY;

            // Mark a handle as hovered, only if it's the only hovered handle (during the previous frame
            m_handle_x.isHovered        = hovered_x && !(m_handle_y.isHovered || m_handle_z.isHovered);
            m_handle_y.isHovered        = hovered_y && !(m_handle_x.isHovered || m_handle_z.isHovered);
            m_handle_z.isHovered        = hovered_z && !(m_handle_x.isHovered || m_handle_y.isHovered);
            m_handle_xyz.isHovered      = hovered_xyz && !(m_handle_x.isHovered || m_handle_y.isHovered || m_handle_z.isHovered);

            // Disable handle if one of the other two is active (affects the color)
            m_handle_x.isDisabled       = !m_handle_x.isEditing     && (m_handle_y.isEditing || m_handle_z.isEditing || m_handle_xyz.isEditing);
            m_handle_y.isDisabled       = !m_handle_y.isEditing     && (m_handle_x.isEditing || m_handle_z.isEditing || m_handle_xyz.isEditing);
            m_handle_z.isDisabled       = !m_handle_z.isEditing     && (m_handle_x.isEditing || m_handle_y.isEditing || m_handle_xyz.isEditing);
            m_handle_xyz.isDisabled     = !m_handle_xyz.isEditing   && (m_handle_x.isEditing || m_handle_y.isEditing || m_handle_z.isEditing);

            // Track delta
            m_ray_previous          = m_ray_current != Vector3::Zero ? m_ray_current : ray_end; // avoid big delta in the first run
            m_ray_current           = ray_end;
            const auto delta        = m_ray_current - m_ray_previous;
            const auto delta_xyz    = delta.Length();

            // If the delta reached infinity, ignore the input as it will result in NaN position.
            // This can happen if the transformation is happening extremely close the actual viewport camera.
            if (isinf(delta_xyz))
                return true;

            // Updated handles with delta
            m_handle_x.delta    = delta_xyz * Helper::Sign(delta.x) * handle_speed;
            m_handle_y.delta    = delta_xyz * Helper::Sign(delta.y) * handle_speed;
            m_handle_z.delta    = delta_xyz * Helper::Sign(delta.z) * handle_speed;
            m_handle_xyz.delta  = m_handle_x.delta + m_handle_y.delta + m_handle_z.delta;

            // Update input
            m_handle_x.UpdateInput(m_type, entity->GetTransform(), m_input);
            m_handle_y.UpdateInput(m_type, entity->GetTransform(), m_input);
            m_handle_z.UpdateInput(m_type, entity->GetTransform(), m_input);
            m_handle_xyz.UpdateInput(m_type, entity->GetTransform(), m_input);
        }

        return m_handle_x.isEditing || m_handle_y.isEditing || m_handle_z.isEditing || m_handle_xyz.isEditing;
    }

    const Matrix& TransformHandle::GetTransform(const Vector3& axis) const
    {
        if (axis == Vector3::Right)
        {
            return m_handle_x.transform;
        }
        if (axis == Vector3::Up)
        {
            return m_handle_y.transform;
        }
        if (axis == Vector3::Forward)
        {
            return m_handle_z.transform;
        }

        return m_handle_xyz.transform;
    }

    const Vector3& TransformHandle::GetColor(const Vector3& axis) const
    {
        if (axis == Vector3::Right)
        {
            return m_handle_x.GetColor();
        }

        if (axis == Vector3::Up)
        {
            return m_handle_y.GetColor();
        }

        if (axis == Vector3::Forward)
        {
            return m_handle_z.GetColor();
        }

        return m_handle_xyz.GetColor();
    }

    const RHI_VertexBuffer* TransformHandle::GetVertexBuffer() const
    {
        return m_model->GetVertexBuffer();
    }

    const RHI_IndexBuffer* TransformHandle::GetIndexBuffer() const
    {
        return m_model->GetIndexBuffer();
    }

    void TransformHandle::SnapToTransform(const TransformHandle_Space space, Entity* entity, Camera* camera, const float handle_size)
    {
        // Get entity's components
        const auto entity_transform    = entity->GetTransform();        // Transform alone is not enough
        auto entity_renderable    = entity->GetComponent<Renderable>();    // Bounding box is also needed as some meshes are not defined around P(0,0,0)    

        // Acquire entity's transformation data (local or world space)
        const auto& aabb_center        = entity_renderable ? entity_renderable->GetAabb().GetCenter()          : entity_transform->GetPositionLocal();
        const auto& entity_rotation    = (space == TransformHandle_World) ? entity_transform->GetRotation()    : entity_transform->GetRotationLocal();
        const auto& right            = (space == TransformHandle_World) ? Vector3::Right                        : entity_rotation * Vector3::Right;
        const auto& up                = (space == TransformHandle_World) ? Vector3::Up                        : entity_rotation * Vector3::Up;
        const auto& forward            = (space == TransformHandle_World) ? Vector3::Forward                    : entity_rotation * Vector3::Forward;

        // Compute scale
        const auto distance_to_camera    = camera ? (camera->GetTransform()->GetPosition() - (aabb_center)).Length()    : 0.0f;
        const auto handle_scale            = distance_to_camera / (1.0f / handle_size);
        const auto handle_distance        = distance_to_camera / (1.0f / 0.1f);

        // Compute transform for the handles
        m_handle_x.position        = aabb_center + right    * handle_distance;
        m_handle_y.position        = aabb_center + up        * handle_distance;
        m_handle_z.position        = aabb_center + forward * handle_distance;
        m_handle_xyz.position    = aabb_center;
        m_handle_x.rotation        = Quaternion::FromEulerAngles(0.0f, 0.0f, -90.0f);
        m_handle_y.rotation        = Quaternion::FromEulerAngles(0.0f, 90.0f, 0.0f);
        m_handle_z.rotation        = Quaternion::FromEulerAngles(90.0f, 0.0f, 0.0f);    
        m_handle_x.scale        = handle_scale;
        m_handle_y.scale        = handle_scale;
        m_handle_z.scale        = handle_scale;
        m_handle_xyz.scale        = handle_scale;

        // Update transforms
        m_handle_x.UpdateTransform();
        m_handle_y.UpdateTransform();
        m_handle_z.UpdateTransform();
        m_handle_xyz.UpdateTransform();

        // Allow the handles to draw anything else they need
        m_handle_x.DrawExtra(m_renderer, aabb_center);
        m_handle_y.DrawExtra(m_renderer, aabb_center);
        m_handle_z.DrawExtra(m_renderer, aabb_center);
        m_handle_xyz.DrawExtra(m_renderer, aabb_center);
    }
}
