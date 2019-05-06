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
#include "Transform.h"
#include "Renderable.h"
#include "../../Math/RayHit.h"
#include "../../Core/Context.h"
#include "../../IO/FileStream.h"
#include "../../Rendering/Renderer.h"
//===================================

//= NAMESPACES ================
using namespace Spartan::Math;
using namespace std;
//=============================

namespace Spartan
{
	Camera::Camera(Context* context, Entity* entity, Transform* transform) : IComponent(context, entity, transform)
	{
		m_near_plane			= 0.3f;
		m_far_plane				= 1000.0f;
		m_projection_type		= Projection_Perspective;
		m_clear_color			= Vector4(0.396f, 0.611f, 0.937f, 1.0f); // A nice cornflower blue 
		m_isDirty				= false;
		m_fov_horizontal_rad	= DegreesToRadians(90.0f);
	}

	//= ICOMPONENT ============
	void Camera::OnInitialize()
	{
		ComputeBaseView();
		ComputeViewMatrix();
		ComputeProjection();
	}

	void Camera::OnTick()
	{
		const auto& current_viewport = m_context->GetSubsystem<Renderer>()->GetViewport();
		if (m_last_known_viewport != current_viewport)
		{
			m_last_known_viewport = current_viewport;
			m_isDirty			= true;
		}

		// DIRTY CHECK
		if (m_position != GetTransform()->GetPosition() || m_rotation != GetTransform()->GetRotation())
		{
			m_position = GetTransform()->GetPosition();
			m_rotation = GetTransform()->GetRotation();
			m_isDirty = true;
		}

		if (!m_isDirty)
			return;

		ComputeBaseView();
		ComputeViewMatrix();
		ComputeProjection();

		m_frustrum.Construct(GetViewMatrix(), GetProjectionMatrix(), Settings::Get().GetReverseZ() ? GetNearPlane() : GetFarPlane());

		m_isDirty = false;
	}

	void Camera::Serialize(FileStream* stream)
	{
		stream->Write(m_clear_color);
		stream->Write(unsigned int(m_projection_type));
		stream->Write(m_fov_horizontal_rad);
		stream->Write(m_near_plane);
		stream->Write(m_far_plane);
	}

	void Camera::Deserialize(FileStream* stream)
	{
		stream->Read(&m_clear_color);
		m_projection_type = ProjectionType(stream->ReadAs<unsigned int>());
		stream->Read(&m_fov_horizontal_rad);
		stream->Read(&m_near_plane);
		stream->Read(&m_far_plane);

		ComputeBaseView();
		ComputeViewMatrix();
		ComputeProjection();
	}

	//= PLANES/PROJECTION =====================================================
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

	void Camera::SetFovHorizontalDeg(const float fov)
	{
		m_fov_horizontal_rad = DegreesToRadians(fov);
		m_isDirty = true;
	}

	bool Camera::IsInViewFrustrum(Renderable* renderable)
	{
		auto box			= renderable->GeometryAabb();
		const auto center	= box.GetCenter();
		const auto extents	= box.GetExtents();

		return m_frustrum.CheckCube(center, extents) != Outside;
	}

	bool Camera::IsInViewFrustrum(const Vector3& center, const Vector3& extents)
	{
		return m_frustrum.CheckCube(center, extents) != Outside;
	}

	//= RAYCASTING =======================================================================
	bool Camera::Pick(const Vector2& mouse_position, shared_ptr<Entity>& entity)
	{
		const auto& viewport				= m_context->GetSubsystem<Renderer>()->GetViewport();
		const auto& offset					= m_context->GetSubsystem<Renderer>()->viewport_editor_offset;
		const auto mouse_position_relative	= mouse_position - offset;

		// Ensure the ray is inside the viewport
		const auto x_outside = (mouse_position.x < offset.x) || (mouse_position.x > offset.x + viewport.GetWidth());
		const auto y_outside = (mouse_position.y < offset.y) || (mouse_position.y > offset.y + viewport.GetHeight());
		if (x_outside || y_outside)
			return false;

		// Trace ray
		m_ray		= Ray(GetTransform()->GetPosition(), ScreenToWorldPoint(mouse_position_relative));
		auto hits	= m_ray.Trace(m_context);

		// Get closest hit that doesn't start inside an entity
		for (const auto& hit : hits)
		{
			if (hit.m_inside)
				continue;

			entity = hit.m_entity;
			return true;
		}

		entity = nullptr;
		return true;
	}

	Vector2 Camera::WorldToScreenPoint(const Vector3& position_world) const
	{
		const auto& viewport = m_context->GetSubsystem<Renderer>()->GetViewport();

		// Convert world space position to clip space position
		const auto vfov_rad			= 2.0f * atan(tan(m_fov_horizontal_rad / 2.0f) * (viewport.GetHeight() / viewport.GetWidth()));
		const auto projection		= Matrix::CreatePerspectiveFieldOfViewLH(vfov_rad, viewport.GetAspectRatio(), m_near_plane, m_far_plane); // compute non reverse z projection
		const auto position_clip	= position_world * m_mView * projection;

		// Convert clip space position to screen space position
		Vector2 position_screen;
		position_screen.x = (position_clip.x / position_clip.z) * (0.5f * viewport.GetWidth()) + (0.5f * viewport.GetWidth());
		position_screen.y = (position_clip.y / position_clip.z) * -(0.5f * viewport.GetHeight()) + (0.5f * viewport.GetHeight());

		return position_screen;
	}

	Vector3 Camera::ScreenToWorldPoint(const Vector2& position_screen) const
	{
		const auto& viewport = m_context->GetSubsystem<Renderer>()->GetViewport();

		// Convert screen space position to clip space position
		Vector3 position_clip;
		position_clip.x = (position_screen.x / viewport.GetWidth()) * 2.0f - 1.0f;
		position_clip.y = (position_screen.y / viewport.GetHeight()) * -2.0f + 1.0f;
		position_clip.z = 1.0f;

		// Compute world space position
		const auto view_projection_inverted	= (m_mView * m_mProjection).Inverted();
		auto position_world					= position_clip * view_projection_inverted;

		return position_world;
	}

	//= PRIVATE =======================================================================
	void Camera::ComputeViewMatrix()
	{
		const auto position	= GetTransform()->GetPosition();
		auto look_at		= GetTransform()->GetRotation() * Vector3::Forward;
		const auto up		= GetTransform()->GetRotation() * Vector3::Up;

		// offset look_at by current position
		look_at += position;

		// calculate view matrix
		m_mView = Matrix::CreateLookAtLH(position, look_at, up);
	}

	void Camera::ComputeBaseView()
	{
		const auto camera_pos	= Vector3(0, 0, -0.3f);
		const auto look_at		= (Vector3::Forward * Matrix::Identity).Normalized();
		m_mBaseView				= Matrix::CreateLookAtLH(camera_pos, look_at, Vector3::Up);
	}

	void Camera::ComputeProjection()
	{
		const auto& viewport	= m_context->GetSubsystem<Renderer>()->GetViewport();
		const float near_plane	= !Settings::Get().GetReverseZ() ? m_near_plane : m_far_plane;
		const float far_plane	= !Settings::Get().GetReverseZ() ? m_far_plane : m_near_plane;

		if (m_projection_type == Projection_Perspective)
		{
			const auto vfov_rad = 2.0f * atan(tan(m_fov_horizontal_rad / 2.0f) * (viewport.GetHeight() / viewport.GetWidth()));
			m_mProjection = Matrix::CreatePerspectiveFieldOfViewLH(vfov_rad, viewport.GetAspectRatio(), near_plane, far_plane);
		}
		else if (m_projection_type == Projection_Orthographic)
		{
			m_mProjection = Matrix::CreateOrthographicLH(viewport.GetWidth(), viewport.GetHeight(), near_plane, far_plane);
		}
	}
}