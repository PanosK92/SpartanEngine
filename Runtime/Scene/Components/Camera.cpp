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
#include "../Components/Skybox.h"
#include "../../IO/FileStream.h"
#include "../../Core/Settings.h"
#include "../../Math/Quaternion.h"
#include "../../Math/Vector2.h"
#include "../../Math/Vector3.h"
#include "../../Math/Vector4.h"
#include "../../Math/Frustum.h"
#include "../../Graphics/Renderer.h"
#include "../GameObject.h"
#include "Renderable.h"
//===================================

//= NAMESPACES ================
using namespace Directus::Math;
using namespace std;
//=============================

namespace Directus
{
	Camera::Camera(Context* context, GameObject* gameObject, Transform* transform) : IComponent(context, gameObject, transform)
	{
		m_nearPlane			= 0.3f;
		m_farPlane			= 1000.0f;
		m_frustrum			= Frustum();
		m_projection		= Projection_Perspective;
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

	void Camera::OnUpdate()
	{
		if (m_lastKnownResolution != GET_RESOLUTION)
		{
			m_lastKnownResolution = GET_RESOLUTION;
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
		BoundingBox box = renderable->GetBoundingBoxTransformed();
		Vector3 center = box.GetCenter();
		Vector3 extents = box.GetExtents();

		return m_frustrum.CheckCube(center, extents) != Outside;
	}

	bool Camera::IsInViewFrustrum(const Vector3& center, const Vector3& extents)
	{
		return m_frustrum.CheckCube(center, extents) != Outside;
	}

	vector<VertexPosCol> Camera::GetPickingRay()
	{
		vector<VertexPosCol> lines;

		lines.emplace_back(VertexPosCol(m_ray.GetOrigin(), Vector4(0, 1, 0, 1)));
		lines.emplace_back(VertexPosCol(m_ray.GetEnd(), Vector4(0, 1, 0, 1)));

		return lines;
	}

	//= RAYCASTING =======================================================================
	weak_ptr<GameObject> Camera::Pick(const Vector2& mousePos)
	{
		// Compute ray given the origin and end
		m_ray = Ray(GetTransform()->GetPosition(), ScreenToWorldPoint(mousePos));

		// Hits <Distance, GameObject>
		map<float, std::weak_ptr<GameObject>> hits;

		// Find all the GameObjects that the ray hits
		vector<std::weak_ptr<GameObject>> gameObjects = GetContext()->GetSubsystem<Scene>()->GetRenderables();
		for (const auto& gameObj : gameObjects)
		{
			// Make sure there GameObject has a mesh and exclude the SkyBox
			if (!gameObj.lock()->HasComponent<Renderable>() || gameObj.lock()->HasComponent<Skybox>())
				continue;

			// Get bounding box
			BoundingBox box = gameObj.lock()->GetComponent<Renderable>().lock()->GetBoundingBoxTransformed();

			// Compute hit distance
			float hitDistance = m_ray.HitDistance(box);

			// Don't store hit data if we are inside the bounding box (0.0f) or there was no hit (INFINITY)
			if (hitDistance == 0.0f || hitDistance == INFINITY)
				continue;

			hits[hitDistance] = gameObj;
		}

		// Return closest hit
		return !hits.empty() ? hits.begin()->second : weak_ptr<GameObject>();
	}

	Vector2 Camera::WorldToScreenPoint(const Vector3& worldPoint)
	{
		Vector2 viewport = GetContext()->GetSubsystem<Renderer>()->GetViewportInternal();

		Vector3 localSpace = worldPoint * m_mView * m_mProjection;

		float screenX = localSpace.x	/ localSpace.z	* (viewport.x * 0.5f)	+ viewport.x * 0.5f;
		float screenY = -(localSpace.y	/ localSpace.z	* (viewport.y * 0.5f))	+ viewport.y * 0.5f;

		return Vector2(screenX, screenY);
	}

	Vector3 Camera::ScreenToWorldPoint(const Vector2& point)
	{
		Vector2 viewport = GetContext()->GetSubsystem<Renderer>()->GetViewportInternal();

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
		if (m_projection == Projection_Perspective)
		{
			Vector2 viewport = GetContext()->GetSubsystem<Renderer>()->GetViewportInternal();
			float vfovRad = 2.0f * atan(tan(m_fovHorizontalRad / 2.0f) * (viewport.y / viewport.x)); 
			m_mProjection = Matrix::CreatePerspectiveFieldOfViewLH(vfovRad, ASPECT_RATIO, m_nearPlane, m_farPlane);
		}
		else if (m_projection == Projection_Orthographic)
		{
			m_mProjection = Matrix::CreateOrthographicLH((float)RESOLUTION_WIDTH, (float)RESOLUTION_HEIGHT, m_nearPlane, m_farPlane);
		}
	}
}