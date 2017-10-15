/*
Copyright(c) 2016-2017 Panos Karabelas

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
#include "../IO/StreamIO.h"
#include "../Core/Settings.h"
#include "../Components/MeshFilter.h"
#include "../Components/Skybox.h"
#include "../Math/Quaternion.h"
#include "../Math/Vector2.h"
#include "../Math/Vector3.h"
#include "../Math/Vector4.h"
#include "../Math/Frustrum.h"
#include "../Graphics/Renderer.h"
//===================================

//= NAMESPACES ================
using namespace Directus::Math;
using namespace std;
//=============================

namespace Directus
{
	Camera::Camera()
	{
		SetFOV_Horizontal_Deg(75);
		m_nearPlane = 0.1f;
		m_farPlane = 1000.0f;
		m_frustrum = make_shared<Frustrum>();
		m_projection = Perspective;
		m_clearColor = Vector4(0.396f, 0.611f, 0.937f, 1.0f); // A nice cornflower blue 
		m_isDirty = false;
	}

	Camera::~Camera()
	{

	}

	//= ICOMPONENT ============
	void Camera::Reset()
	{
		CalculateBaseView();
		CalculateViewMatrix();
		CalculateProjection();
	}

	void Camera::Start()
	{

	}

	void Camera::OnDisable()
	{

	}

	void Camera::Remove()
	{

	}

	void Camera::Update()
	{
		if (m_lastKnownResolution != GET_RESOLUTION)
		{
			m_lastKnownResolution = GET_RESOLUTION;
			m_isDirty = true;
		}

		// DIRTY CHECK
		if (m_position != g_transform->GetPosition() || m_rotation != g_transform->GetRotation())
		{
			m_position = g_transform->GetPosition();
			m_rotation = g_transform->GetRotation();
			m_isDirty = true;
		}

		if (!m_isDirty)
			return;

		CalculateBaseView();
		CalculateViewMatrix();
		CalculateProjection();

		m_frustrum->Construct(GetViewMatrix(), GetProjectionMatrix(), GetFarPlane());

		m_isDirty = false;
	}

	void Camera::Serialize()
	{
		StreamIO::WriteVector4(m_clearColor);
		StreamIO::WriteInt(int(m_projection));
		StreamIO::WriteFloat(m_fovHorizontal);
		StreamIO::WriteFloat(m_nearPlane);
		StreamIO::WriteFloat(m_farPlane);
	}

	void Camera::Deserialize()
	{
		m_clearColor = StreamIO::ReadVector4();
		m_projection = Projection(StreamIO::ReadInt());
		m_fovHorizontal = StreamIO::ReadFloat();
		m_nearPlane = StreamIO::ReadFloat();
		m_farPlane = StreamIO::ReadFloat();

		CalculateBaseView();
		CalculateViewMatrix();
		CalculateProjection();
	}

	//= PLANES/PROJECTION =====================================================
	void Camera::SetNearPlane(float nearPlane)
	{
		m_nearPlane = nearPlane;
		m_isDirty = true;
	}

	void Camera::SetFarPlane(float farPlane)
	{
		m_farPlane = farPlane;
		m_isDirty = true;
	}

	void Camera::SetProjection(Projection projection)
	{
		m_projection = projection;
		m_isDirty = true;
	}

	float Camera::GetFOV_Horizontal_Deg()
	{
		return RadiansToDegrees(m_fovHorizontal);
	}

	void Camera::SetFOV_Horizontal_Deg(float fov)
	{
		m_fovHorizontal = DegreesToRadians(fov);
		m_isDirty = true;
	}

	bool Camera::IsInViewFrustrum(MeshFilter* meshFilter)
	{
		BoundingBox box = meshFilter->GetBoundingBoxTransformed();
		Vector3 center = box.GetCenter();
		Vector3 extents = box.GetHalfSize();

		return m_frustrum->CheckCube(center, extents) != Outside;
	}

	bool Camera::IsInViewFrustrum(const Vector3& center, const Vector3& extents)
	{
		return m_frustrum->CheckCube(center, extents) != Outside;
	}

	vector<VertexPosCol> Camera::GetPickingRay()
	{
		vector<VertexPosCol> lines;

		VertexPosCol rayStart;
		rayStart.color = Vector4(0, 1, 0, 1);
		rayStart.position = m_ray.GetOrigin();

		VertexPosCol rayEnd;
		rayEnd.color = Vector4(0, 1, 0, 1);
		rayEnd.position = m_ray.GetEnd();

		lines.push_back(rayStart);
		lines.push_back(rayEnd);

		return lines;
	}

	//= RAYCASTING =======================================================================
	weak_ptr<GameObject> Camera::Pick(const Vector2& mouse)
	{
		// Compute ray given the origin and end
		m_ray = Ray(g_transform->GetPosition(), ScreenToWorldPoint(mouse));

		// We use the boundibg box of each mesh to determine find and return the 
		// one that's nearest to the camera. However, there are scenarios where
		// hollow meshes (let's say a building) will have a large bounding box
		// which will contain other bounding boxes and potentially even the camera.
		// In a scenario like this, the hit distance of the large bounding box
		// will be zero, and it will be picked every time. Because this is unlikely
		// what the user would want to do (e.g. in a building, trying to pick contained objects),
		// we reject any bounding boxes that contain the camera and return them only
		// if nothing else was hit. Hence bounding boxes inside a larger one can 
		// be picked. It feels more intuitive.

		// Nearest mesh
		float hitDistanceMin = INFINITY;
		weakGameObj nearestGameObj;

		// A mesh we are potentialy inside of
		vector<weakGameObj> containerGameObj;

		// Find the GameObject nearest to the camera
		vector<weakGameObj> gameObjects = g_context->GetSubsystem<Scene>()->GetRenderables();
		for (const auto& gameObj : gameObjects)
		{
			if (gameObj._Get()->HasComponent<Skybox>())
				continue;

			if (!gameObj._Get()->HasComponent<MeshFilter>())
				continue;

			BoundingBox box = gameObj._Get()->GetComponent<MeshFilter>()->GetBoundingBoxTransformed();

			// Ignore collision if we are inside the bounding box
			// but keep those container bounding boxes
			if (box.IsInside(g_transform->GetPosition()))
			{
				containerGameObj.push_back(gameObj);
				continue;
			}

			float hitDistance = m_ray.HitDistance(box);
			if (hitDistance < hitDistanceMin)
			{
				hitDistanceMin = hitDistance;
				nearestGameObj = gameObj;
			}
		}

		weakGameObj pickedGameObj = nearestGameObj;

		// In case there is no nearest GameObject, go through the 
		// containg GameObjects and return the one whose center 
		// is nearest to the camera's position.
		if (nearestGameObj.expired())
		{
			float distanceMin = INFINITY;

			for (const auto& gameObj : containerGameObj)
			{
				BoundingBox box = gameObj._Get()->GetComponent<MeshFilter>()->GetBoundingBoxTransformed();
				float distance = Vector3::LengthSquared(g_transform->GetPosition(), box.GetCenter());

				if (distance < distanceMin)
				{
					distanceMin = distance;
					pickedGameObj = gameObj;
				}
			}
		}

		return pickedGameObj;
	}
	
	Vector2 Camera::WorldToScreenPoint(const Vector3& worldPoint)
	{
		Vector2 viewport = g_context->GetSubsystem<Renderer>()->GetViewport();

		Vector3 localSpace = worldPoint * m_mView * m_mProjection;

		float screenX = localSpace.x / localSpace.z * (viewport.x * 0.5f) + viewport.x * 0.5f;
		float screenY = -(localSpace.y / localSpace.z * (viewport.y * 0.5f)) + viewport.y * 0.5f;

		return Vector2(screenX, screenY);
	}

	Vector3 Camera::ScreenToWorldPoint(const Vector2& point)
	{
		Vector2 viewport = g_context->GetSubsystem<Renderer>()->GetViewport();

		// Convert screen pixel to view space
		float pointX = 2.0f * point.x / viewport.x - 1.0f;
		float pointY = -2.0f * point.y / viewport.y + 1.0f;

		// Unproject point
		Matrix unprojectMatrix = (m_mView * m_mProjection).Inverted();
		Vector3 worldPoint = Vector3(pointX, pointY, 1.0f) * unprojectMatrix;

		return worldPoint;
	}

	//= PRIVATE =======================================================================
	void Camera::CalculateViewMatrix()
	{
		Vector3 position = g_transform->GetPosition();
		Vector3 lookAt = g_transform->GetRotation() * Vector3::Forward;
		Vector3 up = g_transform->GetRotation() * Vector3::Up;

		// offset lookAt by current position
		lookAt = position + lookAt;

		// calculate view matrix
		m_mView = Matrix::CreateLookAtLH(position, lookAt, up);
	}

	void Camera::CalculateBaseView()
	{
		Vector3 cameraPos = Vector3(0, 0, -0.3f);
		Vector3 lookAt = (Vector3::Forward * Matrix::Identity).Normalized();
		m_mBaseView = Matrix::CreateLookAtLH(cameraPos, lookAt, Vector3::Up);
	}

	void Camera::CalculateProjection()
	{
		if (m_projection == Perspective)
		{
			m_mProjection = Matrix::CreatePerspectiveFieldOfViewLH(m_fovHorizontal, ASPECT_RATIO, m_nearPlane, m_farPlane);
		}
		else if (m_projection == Orthographic)
		{
			m_mProjection = Matrix::CreateOrthographicLH((float)RESOLUTION_WIDTH, (float)RESOLUTION_HEIGHT, m_nearPlane, m_farPlane);
		}
	}
}