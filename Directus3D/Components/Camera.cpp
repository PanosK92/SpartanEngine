/*
Copyright(c) 2016 Panos Karabelas

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

//= INCLUDES ================
#include "Camera.h"
#include "Transform.h"
#include "../IO/Serializer.h"
#include "../Core/Settings.h"
//===========================

//= NAMESPACES ================
using namespace Directus::Math;
using namespace std;
//=============================

Camera::Camera()
{
	m_FOV = 1.04719755f; // 60 degrees
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

/*------------------------------------------------------------------------------
								[INTERFACE]
------------------------------------------------------------------------------*/
void Camera::Initialize()
{
	CalculateProjectionMatrix();
}

void Camera::Start()
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
	CalculateProjectionMatrix();

	m_frustrum->Construct(GetViewMatrix(), GetProjectionMatrix(), GetFarPlane());

	m_isDirty = false;
}

void Camera::Serialize()
{
	Serializer::WriteVector4(m_clearColor);
	Serializer::WriteInt(int(m_projection));
	Serializer::WriteFloat(m_FOV);
	Serializer::WriteFloat(m_nearPlane);
	Serializer::WriteFloat(m_farPlane);
}

void Camera::Deserialize()
{
	m_clearColor = Serializer::ReadVector4();
	m_projection = Projection(Serializer::ReadInt());
	m_FOV = Serializer::ReadFloat();
	m_nearPlane = Serializer::ReadFloat();
	m_farPlane = Serializer::ReadFloat();

	CalculateViewMatrix();
}

/*------------------------------------------------------------------------------
							[PLANES/PROJECTION]
------------------------------------------------------------------------------*/
float Camera::GetNearPlane()
{
	return m_nearPlane;
}

void Camera::SetNearPlane(float nearPlane)
{
	m_nearPlane = nearPlane;
	m_isDirty = true;
}

float Camera::GetFarPlane()
{
	return m_farPlane;
}

void Camera::SetFarPlane(float farPlane)
{
	m_farPlane = farPlane;
	m_isDirty = true;
}

Projection Camera::GetProjection()
{
	return m_projection;
}

void Camera::SetProjection(Projection projection)
{
	m_projection = projection;
	m_isDirty = true;
}

float Camera::GetFieldOfView()
{
	return RadiansToDegrees(m_FOV);
}

void Camera::SetFieldOfView(float fov)
{
	m_FOV = DegreesToRadians(fov);
	m_isDirty = true;
}

const shared_ptr<Frustrum>& Camera::GetFrustrum()
{
	return m_frustrum;
}

Vector4 Camera::GetClearColor()
{
	return m_clearColor;
}

void Camera::SetClearColor(const Vector4& color)
{
	m_clearColor = color;
}

/*------------------------------------------------------------------------------
									[MATRICES]
------------------------------------------------------------------------------*/
Matrix Camera::GetViewMatrix()
{
	return m_viewMatrix;
}

Matrix Camera::GetProjectionMatrix()
{
	if (m_projection == Perspective)
		return m_perspectiveProjectionMatrix;

	return m_orthographicProjectionMatrix;
}

Matrix Camera::GetPerspectiveProjectionMatrix()
{
	return m_perspectiveProjectionMatrix;
}

Matrix Camera::GetOrthographicProjectionMatrix()
{
	return m_orthographicProjectionMatrix;
}

Matrix Camera::GetBaseViewMatrix()
{
	return m_baseViewMatrix;
}

/*------------------------------------------------------------------------------
[CONVERSIONS]
------------------------------------------------------------------------------*/
Vector2 Camera::WorldToScreenPoint(const Vector3& worldPoint)
{
	float screenWidth = RESOLUTION_WIDTH;
	float screenHeight = RESOLUTION_HEIGHT;

	Vector3 localSpace = Vector3::Transform(worldPoint, GetViewMatrix());

	int screenX = ((localSpace.x / localSpace.z) * (screenWidth * 0.5f)) + (screenWidth * 0.5f);
	int screenY = -((localSpace.y / localSpace.z) * (screenHeight * 0.5f)) + (screenHeight * 0.5f);

	return Vector2(screenX, screenY);
}

Vector3 Camera::ScreenToWorldPoint(const Vector2& screenPoint)
{
	/*
	float sceenWidth = RESOLUTION_WIDTH;
	float sceenHeight = RESOLUTION_HEIGHT;

	// Move the mouse cursor coordinates into the -1 to +1 range.
	float pointX = ((2.0f * (float)screenPoint.x) / (float)sceenWidth) - 1.0f;
	float pointY = (((2.0f * (float)screenPoint.y) / (float)sceenHeight) - 1.0f) * -1.0f;

	// Adjust the points using the projection matrix to account for the aspect ratio of the viewport.
	pointX = pointX / GetProjectionMatrix().m00;
	pointY = pointY / GetProjectionMatrix().m11;

	// Calculate the direction of the picking ray in view space.
	Matrix viewInverse = GetViewMatrix().Inverted();
	Vector3 direction;
	direction.x = (pointX * viewInverse.m00) + (pointY * viewInverse.m10) + viewInverse.m20;
	direction.y = (pointX * viewInverse.m01) + (pointY * viewInverse.m11) + viewInverse.m21;
	direction.z = (pointX * viewInverse.m02) + (pointY * viewInverse.m12) + viewInverse.m22;

	// Get the origin of the picking ray which is the position of the camera.
	Vector3 origin = g_transform->GetPosition();

	// Get the world matrix and translate to the location of the sphere.
	Matrix worldMatrix = Matrix::Identity;
	D3DXMatrixTranslation(&translateMatrix, -5.0f, 1.0f, 5.0f);
	D3DXMatrixMultiply(&worldMatrix, &worldMatrix, &translateMatrix);

	// Now get the inverse of the translated world matrix.
	Matrix worldMatrixInverse = worldMatrix.Inverted();

	// Now transform the ray origin and the ray direction from view space to world space.
	Vector3 rayOrigin;
	Vector3 rayDirection;
	D3DXVec3TransformCoord(&rayOrigin, &origin, &inverseWorldMatrix);
	D3DXVec3TransformNormal(&rayDirection, &direction, &inverseWorldMatrix);

	// Normalize the ray direction.
	rayDirection.Normalize();

	return rayDirection;
	*/
}

/*------------------------------------------------------------------------------
								[PRIVATE]
------------------------------------------------------------------------------*/
void Camera::CalculateViewMatrix()
{
	Vector3 position = g_transform->GetPosition();
	Vector3 lookAt = g_transform->GetRotation() * Vector3::Forward; // global forward
	Vector3 up = g_transform->GetRotation() * Vector3::Up; // global up

	// offset lookAt by current position
	lookAt = position + lookAt;

	// calculate view matrix
	m_viewMatrix = Matrix::CreateLookAtLH(position, lookAt, up);
}

void Camera::CalculateBaseView()
{
	Vector3 lookAt = Vector3::Transform(Vector3::Forward, Matrix::Identity).Normalized();
	m_baseViewMatrix = Matrix::CreateLookAtLH(Vector3(0, 0, -0.3f), lookAt, Vector3::Up);
}

void Camera::CalculateProjectionMatrix()
{
	m_perspectiveProjectionMatrix = Matrix::CreatePerspectiveFieldOfViewLH(m_FOV, ASPECT_RATIO, m_nearPlane, m_farPlane);
	m_orthographicProjectionMatrix = Matrix::CreateOrthographicLH(RESOLUTION_WIDTH, RESOLUTION_HEIGHT, m_nearPlane, m_farPlane);
}
