/*
Copyright(c) 2016-2019 Panos Karabelas

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
#include "Camera.h"
#include <algorithm>
#include "Transform.h"
#include "Renderable.h"
#include "../Entity.h"
#include "../../Math/RayHit.h"
#include "../../Core/Context.h"
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
        m_renderer  = m_context->GetSubsystem<Renderer>().get();
        m_input     = m_context->GetSubsystem<Input>().get();
	}

	void Camera::OnInitialize()
	{
        m_mBaseView     = ComputeBaseView();
        m_mView         = ComputeViewMatrix();
        m_mProjection   = ComputeProjection();
	}

	void Camera::OnTick(float delta_time)
	{
		const auto& current_viewport = m_renderer->GetViewport();
		if (m_last_known_viewport != current_viewport)
		{
			m_last_known_viewport   = current_viewport;
			m_isDirty			    = true;
		}

		// DIRTY CHECK
		if (m_position != GetTransform()->GetPosition() || m_rotation != GetTransform()->GetRotation())
		{
			m_position = GetTransform()->GetPosition();
			m_rotation = GetTransform()->GetRotation();
			m_isDirty = true;
		}

        FpsControl(delta_time);
       
		if (!m_isDirty)
			return;

        m_mBaseView     = ComputeBaseView();
        m_mView         = ComputeViewMatrix();
        m_mProjection   = ComputeProjection();
		m_frustrum      = Frustum(GetViewMatrix(), GetProjectionMatrix(), m_renderer->GetReverseZ() ? GetNearPlane() : GetFarPlane());

		m_isDirty = false;
	}

	void Camera::Serialize(FileStream* stream)
	{
		stream->Write(m_clear_color);
		stream->Write(uint32_t(m_projection_type));
		stream->Write(m_fov_horizontal_rad);
		stream->Write(m_near_plane);
		stream->Write(m_far_plane);
	}

	void Camera::Deserialize(FileStream* stream)
	{
		stream->Read(&m_clear_color);
		m_projection_type = ProjectionType(stream->ReadAs<uint32_t>());
		stream->Read(&m_fov_horizontal_rad);
		stream->Read(&m_near_plane);
		stream->Read(&m_far_plane);

        m_mBaseView     = ComputeBaseView();
		m_mView         = ComputeViewMatrix();
		m_mProjection   = ComputeProjection();
	}

	void Camera::SetNearPlane(const float near_plane)
	{
		m_near_plane = Max(0.01f, near_plane);
		m_isDirty = true;
	}

	void Camera::SetFarPlane(const float far_plane)
	{
		m_far_plane = far_plane;
		m_isDirty = true;
	}

	void Camera::SetProjection(const ProjectionType projection)
	{
		m_projection_type = projection;
		m_isDirty = true;
	}

    float Camera::GetFovHorizontalDeg() const
	{
		return RadiansToDegrees(m_fov_horizontal_rad);
	}

    float Camera::GetFovVerticalRad()
    {
        return 2.0f * atan(tan(m_fov_horizontal_rad / 2.0f) * (GetViewport().height / GetViewport().width));
    }

    void Camera::SetFovHorizontalDeg(const float fov)
	{
		m_fov_horizontal_rad = DegreesToRadians(fov);
		m_isDirty = true;
	}

    const Spartan::RHI_Viewport& Camera::GetViewport()
    {
        return m_renderer->GetViewport();
    }

    bool Camera::IsInViewFrustrum(Renderable* renderable)
	{
		const auto box		= renderable->GetAabb();
		const auto center	= box.GetCenter();
		const auto extents	= box.GetExtents();

		return m_frustrum.IsVisible(center, extents);
	}

	bool Camera::IsInViewFrustrum(const Vector3& center, const Vector3& extents)
	{
		return m_frustrum.IsVisible(center, extents);
	}

	bool Camera::Pick(const Vector2& mouse_position, shared_ptr<Entity>& picked)
	{
		const auto& viewport				= m_renderer->GetViewport();
		const auto& offset					= m_renderer->viewport_editor_offset;
		const auto mouse_position_relative	= mouse_position - offset;

		// Ensure the ray is inside the viewport
		const auto x_outside = (mouse_position.x < offset.x) || (mouse_position.x > offset.x + viewport.width);
		const auto y_outside = (mouse_position.y < offset.y) || (mouse_position.y > offset.y + viewport.height);
		if (x_outside || y_outside)
			return false;

		// Trace ray
		m_ray		= Ray(GetTransform()->GetPosition(), ScreenToWorldPoint(mouse_position_relative));
		auto hits	= m_ray.Trace(m_context);

        // Create a struct to hold hit related data
        struct scored_entity
        {
            scored_entity(const shared_ptr<Entity>& entity, float distance_ray, float distance_obb)
            {
                this->entity = entity;
                score = distance_ray * 0.1f + distance_obb * 0.9f;
            }

            shared_ptr<Entity> entity;
            float score;
        };
        vector<scored_entity> m_scored;

        // Go through all the hits and score them
        m_scored.reserve(hits.size());
		for (const auto& hit : hits)
		{
            // Filter hits that start inside OBBs
			if (hit.m_inside)
				continue;

            // Score this hit
            auto& obb = hit.m_entity->GetComponent<Renderable>()->GetAabb();
            float distance_obb = Vector3::DistanceSquared(hit.m_position, obb.GetCenter());
            m_scored.emplace_back
            (
                hit.m_entity,
                1.0f - hit.m_distance / m_ray.GetLength(),          // normalized ray distance score
                1.0f - (distance_obb / obb.GetExtents().Length())   // normalized obb center distance score
            );
		}
        m_scored.shrink_to_fit();

        // Return entity with highest score
        picked = nullptr;
        if (!m_scored.empty())
        {
            // ordering descendingly
            sort(m_scored.begin(), m_scored.end(), [](const scored_entity& a, const scored_entity& b) { return a.score > b.score; });

            picked = m_scored.front().entity;
            return true;
        }

        // If no hit was good enough but there are hits, compromise by picking the closest one
        if (!picked && !hits.empty())
            picked = hits.front().m_entity;

		return true;
	}

	Vector2 Camera::WorldToScreenPoint(const Vector3& position_world) const
	{
		const auto& viewport = m_renderer->GetViewport();

		// Convert world space position to clip space position
		const auto vfov_rad			= 2.0f * atan(tan(m_fov_horizontal_rad / 2.0f) * (viewport.height / viewport.width));
		const auto projection		= Matrix::CreatePerspectiveFieldOfViewLH(vfov_rad, viewport.AspectRatio(), m_near_plane, m_far_plane); // compute non reverse z projection
		const auto position_clip	= position_world * m_mView * projection;

		// Convert clip space position to screen space position
		Vector2 position_screen;
		position_screen.x = (position_clip.x / position_clip.z) * (0.5f * viewport.width) + (0.5f * viewport.width);
		position_screen.y = (position_clip.y / position_clip.z) * -(0.5f * viewport.height) + (0.5f * viewport.height);

		return position_screen;
	}

	Vector3 Camera::ScreenToWorldPoint(const Vector2& position_screen) const
	{
		const auto& viewport = m_renderer->GetViewport();

		// Convert screen space position to clip space position
		Vector3 position_clip;
		position_clip.x = (position_screen.x / viewport.width) * 2.0f - 1.0f;
		position_clip.y = (position_screen.y / viewport.height) * -2.0f + 1.0f;
		position_clip.z = 1.0f;

		// Compute world space position
		const auto view_projection_inverted	= (m_mView * m_mProjection).Inverted();
		auto position_world					= position_clip * view_projection_inverted;

		return position_world;
	}

    void Camera::FpsControl(float delta_time)
    {
        static const float mouse_sensitivity        = 20.0f;
        static const float mouse_smoothing          = 2.5f;
        static const float movement_speed_max       = 40.0f;
        static const float movement_acceleration    = 0.8f;
        static const float movement_drag            = 0.08f;

        if (m_input->GetKey(KeyCode::Click_Right))
        {
            // Mouse look
            {
                // Get mouse delta
                Vector2 mouse_delta = m_input->GetMouseDelta() * mouse_sensitivity;

                // Lerp to it
                mouse_smoothed = Math::Lerp(mouse_smoothed, mouse_delta, 1.0f / mouse_smoothing);

                // Accumulate rotation
                mouse_rotation += mouse_smoothed * delta_time;

                // Clamp rotation along the x-axis
                mouse_rotation.y = Clamp(mouse_rotation.y, -90.0f, 90.0f);

                // Compute rotation
                auto xQuaternion = Quaternion::FromAngleAxis(mouse_rotation.x * DEG_TO_RAD, Vector3::Up);
                auto yQuaternion = Quaternion::FromAngleAxis(mouse_rotation.y * DEG_TO_RAD, Vector3::Right);

                // Rotate
                m_transform->SetRotationLocal(xQuaternion * yQuaternion);
            }

            // Keyboard movement
            {
                // Compute direction
                Vector3 direction = Vector3::Zero;
                if (m_input->GetKey(KeyCode::W)) direction += m_transform->GetForward();
                if (m_input->GetKey(KeyCode::S)) direction += m_transform->GetBackward();
                if (m_input->GetKey(KeyCode::D)) direction += m_transform->GetRight();
                if (m_input->GetKey(KeyCode::A)) direction += m_transform->GetLeft();
                direction.Normalize();

                // Compute speed
                m_movement_speed += direction * movement_acceleration;
                m_movement_speed.x = Clamp(m_movement_speed.x, -movement_speed_max, movement_speed_max);
                m_movement_speed.y = Clamp(m_movement_speed.y, -movement_speed_max, movement_speed_max);
                m_movement_speed.z = Clamp(m_movement_speed.z, -movement_speed_max, movement_speed_max);
            }
        }

        // Apply movement drag
        m_movement_speed *= 1.0f - movement_drag;

        // Translate for as long as there is speed
        if (m_movement_speed != Vector3::Zero)
        {
            m_transform->Translate(m_movement_speed * delta_time);
        }
    }

    Matrix Camera::ComputeViewMatrix()
	{
		const auto position	= GetTransform()->GetPosition();
		auto look_at		= GetTransform()->GetRotation() * Vector3::Forward;
		const auto up		= GetTransform()->GetRotation() * Vector3::Up;

		// offset look_at by current position
		look_at += position;

		// compute view matrix
		return Matrix::CreateLookAtLH(position, look_at, up);
	}

    Matrix Camera::ComputeBaseView()
	{
		const auto camera_pos	= Vector3(0, 0, -m_near_plane);
		const auto look_at		= (Vector3::Forward * Matrix::Identity).Normalized();

        // compute view matrix
		return Matrix::CreateLookAtLH(camera_pos, look_at, Vector3::Up);
	}

	Matrix Camera::ComputeProjection(const bool force_non_reverse_z /*= false*/, const float ovveride_far_plane /*= 0.0f*/)
	{
        bool reverse_z      = (m_renderer ? m_renderer->GetReverseZ() : false) && !force_non_reverse_z;
		float near_plane	= m_near_plane;
		float far_plane	    = ovveride_far_plane == 0.0f ? m_far_plane : ovveride_far_plane;

        // Swap planes if reverse Z is enabled
        if (reverse_z)
        {
            near_plane  = m_far_plane;
            far_plane   = m_near_plane;
        }

		if (m_projection_type == Projection_Perspective)
		{
			return Matrix::CreatePerspectiveFieldOfViewLH(GetFovVerticalRad(), GetViewport().AspectRatio(), near_plane, far_plane);
		}
		else if (m_projection_type == Projection_Orthographic)
		{
			return Matrix::CreateOrthographicLH(GetViewport().width, GetViewport().height, near_plane, far_plane);
		}

        return Matrix::Identity;
	}
}
