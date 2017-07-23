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
#include "../IO/Serializer.h"
#include "../Core/Settings.h"
#include "../Components/MeshFilter.h"
#include "../Components/Skybox.h"
#include "../Graphics/Model.h"
#include "../Logging/Log.h"
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
		Register();
		SetFOV_Horizontal_Deg(75);
		m_nearPlane = 0.3f;
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
		Serializer::WriteVector4(m_clearColor);
		Serializer::WriteInt(int(m_projection));
		Serializer::WriteFloat(m_fovHorizontal);
		Serializer::WriteFloat(m_nearPlane);
		Serializer::WriteFloat(m_farPlane);
	}

	void Camera::Deserialize()
	{
		m_clearColor = Serializer::ReadVector4();
		m_projection = Projection(Serializer::ReadInt());
		m_fovHorizontal = Serializer::ReadFloat();
		m_nearPlane = Serializer::ReadFloat();
		m_farPlane = Serializer::ReadFloat();

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
		Vector3 center = meshFilter->GetCenter();
		float radius = meshFilter->GetBoundingSphereRadius();

		return m_frustrum->CheckSphere(center, radius) != Outside;
	}

	bool Camera::IsInViewFrustrum(weak_ptr<Model> model)
	{
		if (model.expired())
			return true;

		Vector3 center = model._Get()->GetCenter();
		float radius = model._Get()->GetBoundingSphereRadius();

		return m_frustrum->CheckSphere(center, radius) != Outside;
	}

	vector<VertexPosCol> Camera::GetPickingRay()
	{
		vector<VertexPosCol> lines;

		VertexPosCol rayStart;
		rayStart.color = Vector4(0, 1, 0, 1);
		rayStart.position = m_pickingRayStart;

		VertexPosCol rayEnd;
		rayEnd.color = Vector4(0, 1, 0, 1);
		rayEnd.position = m_pickingRayEnd;

		lines.push_back(rayStart);
		lines.push_back(rayEnd);

		return lines;
	}

	//= RAYCASTING =======================================================================
	weak_ptr<GameObject> Camera::Pick(const Vector2& mouse)
	{
		// Compute ray
		m_pickingRayStart = g_transform->GetPosition();
		m_pickingRayEnd = ScreenToWorldPoint(mouse);
		m_pickingRayDirection = (m_pickingRayEnd - m_pickingRayStart).Normalized();

		vector<weakGameObj> gameObjects = g_context->GetSubsystem<Scene>()->GetRenderables();
		for (const auto& gameObject : gameObjects)
		{
			if (gameObject._Get()->HasComponent<Camera>())
				continue;

			if (gameObject._Get()->HasComponent<Skybox>())
				continue;

			float radius = gameObject._Get()->GetComponent<MeshFilter>()->GetBoundingSphereRadius();
			if (SphereIntersects(m_pickingRayStart, m_pickingRayDirection, gameObject._Get()->GetTransform()->GetPosition(), radius))
			{
				return gameObject;
			}
		}

		return weak_ptr<GameObject>();
	}
	
	Vector2 Camera::WorldToScreenPoint(const Vector3& worldPoint)
	{
		Vector2 viewport = g_context->GetSubsystem<Renderer>()->GetViewport();

		Vector3 localSpace = worldPoint * m_mView;

		int screenX = ((localSpace.x / localSpace.z) * (viewport.x * 0.5f)) + (viewport.x * 0.5f);
		int screenY = -((localSpace.y / localSpace.z) * (viewport.y * 0.5f)) + (viewport.y * 0.5f);

		return Vector2(screenX, screenY);
	}

	Vector3 Camera::ScreenToWorldPoint(const Vector2& point)
	{
		Vector2 viewport = g_context->GetSubsystem<Renderer>()->GetViewport();

		// Convert screen pixel to view space
		float pointX = (2.0f * point.x / viewport.x - 1.0f);
		float pointY = (-2.0f * point.y / viewport.y + 1.0f);

		// Unproject point
		Matrix unprojectMatrix = (m_mView * m_mProjection).Inverted();
		Vector3 worldPoint = Vector3(pointX, pointY, 1.0f) * unprojectMatrix;

		return worldPoint;
	}

	//= PRIVATE =======================================================================
	bool Camera::SphereIntersects(const Vector3& rayOrigin, const Vector3& rayDirection, const Vector3& sphereCenter, float sphereRadius)
	{
		float sphereRadiusSquered = sphereRadius * sphereRadius;

		// Squared distance between ray origin and sphere center
		float squaredDist = Vector3::Dot(rayOrigin - sphereCenter, rayOrigin - sphereCenter);

		// If the distance is less than the squared radius of the sphere...
		if (squaredDist <= sphereRadiusSquered)
		{
			// Point is in sphere, consider as no intersection existing
			return false;
		}

		// Compute the coefficients of the quadratic equation
		float a = Vector3::Dot(rayDirection, rayDirection);
		float b = 2.0f * Vector3::Dot(rayDirection, rayOrigin - sphereCenter);
		float c = Vector3::Dot(rayOrigin - sphereCenter, rayOrigin - sphereCenter) - sphereRadiusSquered;

		// Calculate discriminant
		float disc = (b * b) - (4.0f * a * c);

		if (disc < 0) // No intersection with sphere
		{
			return false;
		}

		// One or two intersections exist
		float sqrt_disc = sqrt(disc);
		float t1 = (-b + sqrt_disc) / (2 * a);

		// If the second intersection has a negative value then the intersections
		// happen behind the ray origin which is not considered. Otherwise t0 is
		// the intersection to be considered
		return t1 >= 0;
	}

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
			m_mProjection = Matrix::CreateOrthographicLH(RESOLUTION_WIDTH, RESOLUTION_HEIGHT, m_nearPlane, m_farPlane);
		}
	}
}