/*
Copyright(c) 2016-2018 Panos Karabelas

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
#include "../../IO/FileStream.h"
#include "../../Core/Settings.h"
#include "../../Rendering/Renderer.h"
#include "../Actor.h"
#include "Renderable.h"
#include "../TransformationGizmo.h"
//===================================

//= NAMESPACES ================
using namespace Directus::Math;
using namespace std;
//=============================

namespace Directus
{
	Camera::Camera(Context* context, Actor* actor, Transform* transform) : IComponent(context, actor, transform)
	{
		m_nearPlane			= 0.3f;
		m_farPlane			= 1000.0f;
		m_projection		= Projection_Perspective;
		m_clearColor		= Vector4(0.396f, 0.611f, 0.937f, 1.0f); // A nice cornflower blue 
		m_isDirty			= false;
		m_fovHorizontalRad	= DegreesToRadians(90.0f);
		m_transformGizmo	= make_shared<TransformationGizmo>(context);
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
		if (m_lastKnownViewport != Settings::Get().Viewport_Get())
		{
			m_lastKnownViewport = Settings::Get().Viewport_Get();
			m_isDirty = true;
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

		m_frustrum.Construct(GetViewMatrix(), GetProjectionMatrix(), GetFarPlane());

		m_isDirty = false;
	}

	void Camera::Serialize(FileStream* stream)
	{
		stream->Write(m_clearColor);
		stream->Write(int(m_projection));
		stream->Write(m_fovHorizontalRad);
		stream->Write(m_nearPlane);
		stream->Write(m_farPlane);
	}

	void Camera::Deserialize(FileStream* stream)
	{
		stream->Read(&m_clearColor);
		m_projection = ProjectionType(stream->ReadInt());
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
		m_projection = projection;
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
		BoundingBox box = renderable->Geometry_BB();
		Vector3 center	= box.GetCenter();
		Vector3 extents = box.GetExtents();

		return m_frustrum.CheckCube(center, extents) != Outside;
	}

	bool Camera::IsInViewFrustrum(const Vector3& center, const Vector3& extents)
	{
		return m_frustrum.CheckCube(center, extents) != Outside;
	}

	//= RAYCASTING =======================================================================
	shared_ptr<Actor> Camera::Pick(const Vector2& mousePos)
	{
		// Compute ray given the origin and end
		m_ray = Ray(GetTransform()->GetPosition(), ScreenToWorldPoint(mousePos));

		// Hits <Distance, actor>
		map<float, shared_ptr<Actor>> hits;

		// Find all the actors that the ray hits
		const vector<shared_ptr<Actor>>& actors = GetContext()->GetSubsystem<World>()->Actors_GetAll();
		for (const auto& actor : actors)
		{
			// Make sure there actor has a mesh and exclude the SkyBox
			if (!actor->HasComponent<Renderable>() || actor->HasComponent<Skybox>())
				continue;

			// Get bounding box
			BoundingBox bb = actor->GetComponent<Renderable>()->Geometry_BB();

			// Compute hit distance
			float hitDistance = m_ray.HitDistance(bb);

			// Don't store hit data if we are inside the bounding box (0.0f) or there was no hit (INFINITY)
			if (hitDistance == 0.0f || hitDistance == INFINITY)
				continue;

			hits[hitDistance] = actor;
		}

		// Get closest hit
		shared_ptr<Actor> hit = !hits.empty() ? hits.begin()->second : nullptr;

		// Display transformation gizmo
		m_transformGizmo->Pick(hit);

		return hit;
	}

	Vector2 Camera::WorldToScreenPoint(const Vector3& worldPoint)
	{
		Vector2 viewport = Settings::Get().Viewport_Get();

		Vector3 localSpace = worldPoint * m_mView * m_mProjection;

		float screenX = localSpace.x	/ localSpace.z	* (viewport.x * 0.5f)	+ viewport.x * 0.5f;
		float screenY = -(localSpace.y	/ localSpace.z	* (viewport.y * 0.5f))	+ viewport.y * 0.5f;

		return Vector2(screenX, screenY);
	}

	Vector3 Camera::ScreenToWorldPoint(const Vector2& point)
	{
		Vector2 viewport = Settings::Get().Viewport_Get();

		// Convert screen pixel to view space
		float pointX = 2.0f		* point.x / viewport.x - 1.0f;
		float pointY = -2.0f	* point.y / viewport.y + 1.0f;

		// Unproject point
		Matrix unprojectMatrix = (m_mView * m_mProjection).Inverted();
		Vector3 worldPoint = Vector3(pointX, pointY, 1.0f) * unprojectMatrix;

		return worldPoint;
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
		Vector3 cameraPos = Vector3(0, 0, -0.3f);
		Vector3 lookAt = (Vector3::Forward * Matrix::Identity).Normalized();
		m_mBaseView = Matrix::CreateLookAtLH(cameraPos, lookAt, Vector3::Up);
	}

	void Camera::ComputeProjection()
	{
		Vector2 viewport	= Settings::Get().Viewport_Get();
		float width			= viewport.x;
		float height		= viewport.y;

		if (m_projection == Projection_Perspective)
		{
			float vfovRad = 2.0f * atan(tan(m_fovHorizontalRad / 2.0f) * (viewport.y / viewport.x)); 
			m_mProjection = Matrix::CreatePerspectiveFieldOfViewLH(vfovRad, Settings::Get().AspectRatio_Get(), m_nearPlane, m_farPlane);
		}
		else if (m_projection == Projection_Orthographic)
		{
			m_mProjection = Matrix::CreateOrthographicLH(viewport.x, viewport.y, m_nearPlane, m_farPlane);
		}

		// TAA
		if (Renderer::RenderFlags_IsSet(Render_TAA))
		{
			bool isOdd = (Renderer::GetFrame() % 2) == 1;

			// Apply sub-pixel jitter		
			Vector3 jitterA		= Vector3(-0.5f / width, -0.5f / height, 1.0f);
			Vector3 jitterB		= Vector3(0.5f / width, 0.5f / height, 1.0f);
			Matrix jitterMatrix	= Matrix::CreateTranslation(isOdd ? jitterA : jitterB);
			m_mProjection		*= jitterMatrix;
		}
	}
}