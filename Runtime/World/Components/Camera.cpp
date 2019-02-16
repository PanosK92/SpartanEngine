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
#include "../Entity.h"
#include "../../IO/FileStream.h"
#include "../../Core/Settings.h"
#include "../../Rendering/Renderer.h"
#include "../../Math/RayHit.h"
//===================================

//= NAMESPACES ================
using namespace Directus::Math;
using namespace std;
//=============================

namespace Directus
{
	Camera::Camera(Context* context, Entity* entity, Transform* transform) : IComponent(context, entity, transform)
	{
		m_nearPlane			= 0.3f;
		m_farPlane			= 1000.0f;
		m_projectionType	= Projection_Perspective;
		m_clearColor		= Vector4(0.396f, 0.611f, 0.937f, 1.0f); // A nice cornflower blue 
		m_isDirty			= false;
		m_fovHorizontalRad	= DegreesToRadians(90.0f);
	}

	Camera::~Camera()
	{

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
		const RHI_Viewport& currentViewport = m_context->GetSubsystem<Renderer>()->GetViewport();
		if (m_lastKnownViewport != currentViewport)
		{
			m_lastKnownViewport = currentViewport;
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

		#if REVERSE_Z == 1
		m_frustrum.Construct(GetViewMatrix(), GetProjectionMatrix(), GetNearPlane());
		#else
		m_frustrum.Construct(GetViewMatrix(), GetProjectionMatrix(), GetFarPlane());
		#endif

		m_isDirty = false;
	}

	void Camera::Serialize(FileStream* stream)
	{
		stream->Write(m_clearColor);
		stream->Write(int(m_projectionType));
		stream->Write(m_fovHorizontalRad);
		stream->Write(m_nearPlane);
		stream->Write(m_farPlane);
	}

	void Camera::Deserialize(FileStream* stream)
	{
		stream->Read(&m_clearColor);
		m_projectionType = ProjectionType(stream->ReadInt());
		stream->Read(&m_fovHorizontalRad);
		stream->Read(&m_nearPlane);
		stream->Read(&m_farPlane);

		ComputeBaseView();
		ComputeViewMatrix();
		ComputeProjection();
	}

	//= PLANES/PROJECTION =====================================================
	void Camera::SetNearPlane(float nearPlane)
	{
		m_nearPlane = Max(0.01f, nearPlane);
		m_isDirty = true;
	}

	void Camera::SetFarPlane(float farPlane)
	{
		m_farPlane = farPlane;
		m_isDirty = true;
	}

	void Camera::SetProjection(ProjectionType projection)
	{
		m_projectionType = projection;
		m_isDirty = true;
	}

	float Camera::GetFOV_Horizontal_Deg()
	{
		return RadiansToDegrees(m_fovHorizontalRad);
	}

	void Camera::SetFOV_Horizontal_Deg(float fov)
	{
		m_fovHorizontalRad = DegreesToRadians(fov);
		m_isDirty = true;
	}

	bool Camera::IsInViewFrustrum(Renderable* renderable)
	{
		BoundingBox box = renderable->Geometry_AABB();
		Vector3 center	= box.GetCenter();
		Vector3 extents = box.GetExtents();

		return m_frustrum.CheckCube(center, extents) != Outside;
	}

	bool Camera::IsInViewFrustrum(const Vector3& center, const Vector3& extents)
	{
		return m_frustrum.CheckCube(center, extents) != Outside;
	}

	//= RAYCASTING =======================================================================
	bool Camera::Pick(const Vector2& mouse_position, shared_ptr<Entity>& entity)
	{
		const RHI_Viewport& viewport	= m_context->GetSubsystem<Renderer>()->GetViewport();
		const Vector2& offset			= m_context->GetSubsystem<Renderer>()->viewport_editorOffset;
		Vector2 mouse_position_relative = mouse_position - offset;

		// Ensure the ray is inside the viewport
		bool x_outside = (mouse_position.x < offset.x) || (mouse_position.x > offset.x + viewport.GetWidth());
		bool y_outside = (mouse_position.y < offset.y) || (mouse_position.y > offset.y + viewport.GetHeight());
		if (x_outside || y_outside)
			return false;

		// Trace ray
		m_ray = Ray(GetTransform()->GetPosition(), ScreenToWorldPoint(mouse_position_relative));
		std::vector<RayHit> hits = m_ray.Trace(m_context);

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

	Vector2 Camera::WorldToScreenPoint(const Vector3& position_world)
	{
		const RHI_Viewport& viewport = m_context->GetSubsystem<Renderer>()->GetViewport();

		// Convert world space position to clip space position
		float vfovRad			= 2.0f * atan(tan(m_fovHorizontalRad / 2.0f) * (viewport.GetHeight() / viewport.GetWidth()));
		Matrix projection		= Matrix::CreatePerspectiveFieldOfViewLH(vfovRad, viewport.GetAspectRatio(), m_nearPlane, m_farPlane); // compute non reverse z projection
		Vector3 position_clip	= position_world * m_mView * projection;

		// Convert clip space position to screen space position
		Vector2 position_screen;
		position_screen.x = (position_clip.x / position_clip.z) * (0.5f * viewport.GetWidth()) + (0.5f * viewport.GetWidth());
		position_screen.y = (position_clip.y / position_clip.z) * -(0.5f * viewport.GetHeight()) + (0.5f * viewport.GetHeight());

		return position_screen;
	}

	Vector3 Camera::ScreenToWorldPoint(const Vector2& position_screen)
	{
		const RHI_Viewport& viewport = m_context->GetSubsystem<Renderer>()->GetViewport();

		// Convert screen space position to clip space position
		Vector3 position_clip;
		position_clip.x = (position_screen.x / viewport.GetWidth()) * 2.0f - 1.0f;
		position_clip.y = (position_screen.y / viewport.GetHeight()) * -2.0f + 1.0f;
		position_clip.z = 1.0f;

		// Compute world space position
		Matrix viewProjectionInverted	= (m_mView * m_mProjection).Inverted();
		Vector3 position_world			= position_clip * viewProjectionInverted;

		return position_world;
	}

	//= PRIVATE =======================================================================
	void Camera::ComputeViewMatrix()
	{
		Vector3 position	= GetTransform()->GetPosition();
		Vector3 lookAt		= GetTransform()->GetRotation() * Vector3::Forward;
		Vector3 up			= GetTransform()->GetRotation() * Vector3::Up;

		// offset lookAt by current position
		lookAt = position + lookAt;

		// calculate view matrix
		m_mView = Matrix::CreateLookAtLH(position, lookAt, up);
	}

	void Camera::ComputeBaseView()
	{
		Vector3 cameraPos	= Vector3(0, 0, -0.3f);
		Vector3 lookAt		= (Vector3::Forward * Matrix::Identity).Normalized();
		m_mBaseView			= Matrix::CreateLookAtLH(cameraPos, lookAt, Vector3::Up);
	}

	void Camera::ComputeProjection()
	{
		const RHI_Viewport& viewport = m_context->GetSubsystem<Renderer>()->GetViewport();

		if (m_projectionType == Projection_Perspective)
		{
			float vfovRad = 2.0f * atan(tan(m_fovHorizontalRad / 2.0f) * (viewport.GetHeight() / viewport.GetWidth()));
			m_mProjection = Matrix::CreatePerspectiveFieldOfViewLH(vfovRad, viewport.GetAspectRatio(), m_farPlane, m_nearPlane);
		}
		else if (m_projectionType == Projection_Orthographic)
		{
			m_mProjection = Matrix::CreateOrthographicLH(viewport.GetWidth(), viewport.GetHeight(), m_farPlane, m_nearPlane);
		}
	}
}