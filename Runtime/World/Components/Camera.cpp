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

//= INCLUDES ========================
#include "Spartan.h"
#include "Camera.h"
#include "Transform.h"
#include "Renderable.h"
#include "../Entity.h"
#include "../World.h"
#include "../../Input/Input.h"
#include "../../IO/FileStream.h"
#include "../../Rendering/Renderer.h"
//===================================

//= NAMESPACES ===============
using namespace Spartan::Math;
using namespace std;
//============================

namespace Spartan
{
    Camera::Camera(Context* context, Entity* entity, uint32_t id /*= 0*/) : IComponent(context, entity, id)
    {   
        m_renderer  = m_context->GetSubsystem<Renderer>();
        m_input     = m_context->GetSubsystem<Input>();
    }

    void Camera::OnInitialize()
    {
        m_view              = ComputeViewMatrix();
        m_projection        = ComputeProjection(m_renderer->GetOption(Render_ReverseZ));
        m_view_projection   = m_view * m_projection;
    }

    void Camera::OnTick(float delta_time)
    {
        const auto& current_viewport = m_renderer->GetViewport();
        if (m_last_known_viewport != current_viewport)
        {
            m_last_known_viewport   = current_viewport;
            m_is_dirty                = true;
        }

        // DIRTY CHECK
        if (m_position != GetTransform()->GetPosition() || m_rotation != GetTransform()->GetRotation())
        {
            m_position = GetTransform()->GetPosition();
            m_rotation = GetTransform()->GetRotation();
            m_is_dirty = true;
        }

        if (m_fps_control)
        {
            FpsControl(delta_time);
        }

        if (!m_is_dirty)
            return;

        m_view              = ComputeViewMatrix();
        m_projection        = ComputeProjection(m_renderer->GetOption(Render_ReverseZ));
        m_view_projection   = m_view * m_projection;
        m_frustrum          = Frustum(GetViewMatrix(), GetProjectionMatrix(), m_renderer->GetOption(Render_ReverseZ) ? GetNearPlane() : GetFarPlane());

        m_is_dirty = false;
    }

    void Camera::Serialize(FileStream* stream)
    {
        stream->Write(m_aperture);
        stream->Write(m_shutter_speed);
        stream->Write(m_iso);
        stream->Write(m_clear_color);
        stream->Write(uint32_t(m_projection_type));
        stream->Write(m_fov_horizontal_rad);
        stream->Write(m_near_plane);
        stream->Write(m_far_plane);
    }

    void Camera::Deserialize(FileStream* stream)
    {
        stream->Read(&m_aperture);
        stream->Read(&m_shutter_speed);
        stream->Read(&m_iso);
        stream->Read(&m_clear_color);
        m_projection_type = ProjectionType(stream->ReadAs<uint32_t>());
        stream->Read(&m_fov_horizontal_rad);
        stream->Read(&m_near_plane);
        stream->Read(&m_far_plane);

        m_view              = ComputeViewMatrix();
        m_projection        = ComputeProjection(m_renderer->GetOption(Render_ReverseZ));
        m_view_projection   = m_view * m_projection;
    }

    void Camera::SetNearPlane(const float near_plane)
    {
        m_near_plane = Helper::Max(0.01f, near_plane);
        m_is_dirty = true;
    }

    void Camera::SetFarPlane(const float far_plane)
    {
        m_far_plane = far_plane;
        m_is_dirty = true;
    }

    void Camera::SetProjection(const ProjectionType projection)
    {
        m_projection_type = projection;
        m_is_dirty = true;
    }

    float Camera::GetFovHorizontalDeg() const
    {
        return Helper::RadiansToDegrees(m_fov_horizontal_rad);
    }

    float Camera::GetFovVerticalRad() const
    {
        return 2.0f * atan(tan(m_fov_horizontal_rad / 2.0f) * (GetViewport().height / GetViewport().width));
    }

    void Camera::SetFovHorizontalDeg(const float fov)
    {
        m_fov_horizontal_rad = Helper::DegreesToRadians(fov);
        m_is_dirty = true;
    }

    const RHI_Viewport& Camera::GetViewport() const
    {
        return m_renderer ? m_renderer->GetViewport() : RHI_Viewport::Undefined;
    }

    bool Camera::IsInViewFrustrum(Renderable* renderable) const
    {
        const BoundingBox& box  = renderable->GetAabb();
        const Vector3 center    = box.GetCenter();
        const Vector3 extents   = box.GetExtents();

        return m_frustrum.IsVisible(center, extents);
    }

    bool Camera::IsInViewFrustrum(const Vector3& center, const Vector3& extents) const
    {
        return m_frustrum.IsVisible(center, extents);
    }

    bool Camera::Pick(const Vector2& mouse_position, shared_ptr<Entity>& picked)
    {
        const RHI_Viewport& viewport            = m_renderer->GetViewport();
        const Vector2& offset                   = m_renderer->GetViewportOffset();
        const Vector2 mouse_position_relative   = mouse_position - offset;

        // Ensure the ray is inside the viewport
        const auto x_outside = (mouse_position.x < offset.x) || (mouse_position.x > offset.x + viewport.width);
        const auto y_outside = (mouse_position.y < offset.y) || (mouse_position.y > offset.y + viewport.height);
        if (x_outside || y_outside)
            return false;

        // Create mouse ray
        Vector3 ray_start   = GetTransform()->GetPosition();
        Vector3 ray_end     = Unproject(mouse_position_relative);
        m_ray               = Ray(ray_start, ray_end);

        // Traces ray against all AABBs in the world
        vector<RayHit> hits;
        {
            const auto& entities = m_context->GetSubsystem<World>()->EntityGetAll();
            for (const auto& entity : entities)
            {
                // Make sure there entity has a renderable
                if (!entity->HasComponent<Renderable>())
                    continue;

                // Get object oriented bounding box
                const BoundingBox& aabb = entity->GetComponent<Renderable>()->GetAabb();

                // Compute hit distance
                float distance = m_ray.HitDistance(aabb);

                // Don't store hit data if there was no hit
                if (distance == Helper::INFINITY_)
                    continue;

                hits.emplace_back(
                    entity,                                             // Entity
                    m_ray.GetStart() + distance * m_ray.GetDirection(), // Position
                    distance,                                           // Distance
                    distance == 0.0f                                    // Inside
                );
            }

            // Sort by distance (ascending)
            std::sort(hits.begin(), hits.end(), [](const RayHit& a, const RayHit& b) { return a.m_distance < b.m_distance; });
        }

        // Check if there are any hits
        if (hits.empty())
            return false;

        // If there is a single hit, return that
        if (hits.size() == 1)
        {
            picked = hits.front().m_entity;
            return true;
        }

        // Draw picking ray
        //m_renderer->DrawDebugLine(ray_start, ray_end, Vector4(0, 1, 0, 1), Vector4(0, 1, 0, 1), 5.0f, true);

        // If there are more hits, perform triangle intersection
        float distance_min = numeric_limits<float>::max();
        for (RayHit& hit : hits)
        {
            // Get entity geometry
            Renderable* renderable = hit.m_entity->GetRenderable();
            vector<uint32_t> indicies;
            vector<RHI_Vertex_PosTexNorTan> vertices;
            renderable->GeometryGet(&indicies, &vertices);
            if (indicies.empty()|| vertices.empty())
            {
                LOG_ERROR("Failed to get geometry of entity %s, skipping intersection test.");
                continue;
            }

            // Compute matrix which can transform vertices to view space
            Matrix vertex_transform = hit.m_entity->GetTransform()->GetMatrix();

            // Go through each face
            for (uint32_t i = 0; i < indicies.size(); i += 3)
            {
                Vector3 p1_world = Vector3(vertices[indicies[i]].pos) * vertex_transform;
                Vector3 p2_world = Vector3(vertices[indicies[i + 1]].pos) * vertex_transform;
                Vector3 p3_world = Vector3(vertices[indicies[i + 2]].pos) * vertex_transform;

                float distance = m_ray.HitDistance(p1_world, p2_world, p3_world);
                
                if (distance < distance_min)
                {
                    // Draw min distance triangle
                    //m_renderer->DrawDebugTriangle(p1_world, p2_world, p3_world, Vector4(1.0f, 0.0f, 0.0f, 1.0f), 5.0f, false);

                    picked = hit.m_entity;
                    distance_min = distance;
                }
            }
        }

        return picked != nullptr;
    }

    Vector2 Camera::Project(const Vector3& position_world) const
    {
        const auto& viewport = GetViewport();

        // A non reverse-z projection matrix is need, if it we don't have it, we create it
        const auto projection = m_renderer->GetOption(Render_ReverseZ) ? Matrix::CreatePerspectiveFieldOfViewLH(GetFovVerticalRad(), viewport.AspectRatio(), m_near_plane, m_far_plane) : m_projection;

        // Convert world space position to clip space position
        const auto position_clip = position_world * m_view * projection;

        // Convert clip space position to screen space position
        Vector2 position_screen;
        position_screen.x = (position_clip.x / position_clip.z) * (0.5f * viewport.width) + (0.5f * viewport.width);
        position_screen.y = (position_clip.y / position_clip.z) * -(0.5f * viewport.height) + (0.5f * viewport.height);

        return position_screen;
    }

    Math::Rectangle Camera::Project(const BoundingBox& bounding_box) const
    {
        const Vector3& min = bounding_box.GetMin();
        const Vector3& max = bounding_box.GetMax();

        Vector3 corners[8];
        corners[0] = min;
        corners[1] = Vector3(max.x, min.y, min.z);
        corners[2] = Vector3(min.x, max.y, min.z);
        corners[3] = Vector3(max.x, max.y, min.z);
        corners[4] = Vector3(min.x, min.y, max.z);
        corners[5] = Vector3(max.x, min.y, max.z);
        corners[6] = Vector3(min.x, max.y, max.z);
        corners[7] = max;

        Math::Rectangle rectangle;
        for (Vector3& corner : corners)
        {
            rectangle.Merge(Project(corner));
        }

        return rectangle;
    }

    Vector3 Camera::Unproject(const Vector2& position_screen) const
    {
        // Convert screen space position to clip space position
        Vector3 position_clip;
        const auto& viewport = m_renderer->GetViewport();
        position_clip.x = (position_screen.x / viewport.width) * 2.0f - 1.0f;
        position_clip.y = (position_screen.y / viewport.height) * -2.0f + 1.0f;
        position_clip.z = m_near_plane;

        // Compute world space position
        const auto view_projection_inverted    = m_view_projection.Inverted();
        auto position_world                    = position_clip * view_projection_inverted;

        return position_world;
    }

    void Camera::FpsControl(float delta_time)
    {
        if (m_input->GetKey(KeyCode::Click_Right))
        {
            // Mouse look
            {
                // Snap to initial camera rotation (if this is the first time running)
                if (m_mouse_rotation == Vector2::Zero)
                {
                    const Quaternion rotation   = m_transform->GetRotation();
                    m_mouse_rotation.x          = rotation.Yaw();
                    m_mouse_rotation.y          = rotation.Pitch();
                }

                // Get mouse delta
                const Vector2 mouse_delta = m_input->GetMouseDelta() * m_mouse_sensitivity;

                // Lerp to it
                m_mouse_smoothed = Helper::Lerp(m_mouse_smoothed, mouse_delta, Helper::Saturate(1.0f - m_mouse_smoothing));

                // Accumulate rotation
                m_mouse_rotation += m_mouse_smoothed;

                // Clamp rotation along the x-axis
                m_mouse_rotation.y = Helper::Clamp(m_mouse_rotation.y, -90.0f, 90.0f);

                // Compute rotation
                const Quaternion xQuaternion    = Quaternion::FromAngleAxis(m_mouse_rotation.x * Helper::DEG_TO_RAD, Vector3::Up);
                const Quaternion yQuaternion    = Quaternion::FromAngleAxis(m_mouse_rotation.y * Helper::DEG_TO_RAD, Vector3::Right);
                const Quaternion rotation       = xQuaternion * yQuaternion;

                // Rotate
                m_transform->SetRotationLocal(rotation);
            }

            // Keyboard movement
            {
                // Compute max speed
                m_movement_speed_max += m_input->GetMouseWheelDelta();
                m_movement_speed_max = Helper::Clamp(m_movement_speed_max, 0.0f, numeric_limits<float>::max());

                // Compute direction
                Vector3 direction = Vector3::Zero;
                if (m_input->GetKey(KeyCode::W)) direction += m_transform->GetForward();
                if (m_input->GetKey(KeyCode::S)) direction += m_transform->GetBackward();
                if (m_input->GetKey(KeyCode::D)) direction += m_transform->GetRight();
                if (m_input->GetKey(KeyCode::A)) direction += m_transform->GetLeft();
                direction.Normalize();

                // Compute speed
                m_movement_speed += m_movement_acceleration * direction * delta_time;
                m_movement_speed.ClampMagnitude(m_movement_speed_max * delta_time);
            }
        }

        // Apply movement drag
        m_movement_speed *= 1.0f - Helper::Saturate(m_movement_drag * delta_time);

        // Translate for as long as there is speed
        if (m_movement_speed != Vector3::Zero)
        {
            m_transform->Translate(m_movement_speed);
        }
    }

    Matrix Camera::ComputeViewMatrix() const
    {
        const auto position = GetTransform()->GetPosition();
        auto look_at        = GetTransform()->GetRotation() * Vector3::Forward;
        const auto up       = GetTransform()->GetRotation() * Vector3::Up;

        // offset look_at by current position
        look_at += position;

        // compute view matrix
        return Matrix::CreateLookAtLH(position, look_at, up);
    }

    Matrix Camera::ComputeProjection(const bool reverse_z, const float near_plane /*= 0.0f*/, const float far_plane /*= 0.0f*/)
    {
        float _near  = near_plane != 0 ? near_plane : m_near_plane;
        float _far   = far_plane != 0  ? far_plane : m_far_plane;

        if (reverse_z)
        {
            const float temp = _near;
            _near = _far;
            _far = temp;
        }

        if (m_projection_type == Projection_Perspective)
        {
            return Matrix::CreatePerspectiveFieldOfViewLH(GetFovVerticalRad(), GetViewport().AspectRatio(), _near, _far);
        }
        else if (m_projection_type == Projection_Orthographic)
        {
            return Matrix::CreateOrthographicLH(GetViewport().width, GetViewport().height, _near, _far);
        }

        return Matrix::Identity;
    }
}
