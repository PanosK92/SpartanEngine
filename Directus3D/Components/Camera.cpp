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

//=============================

Camera::Camera()
{
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

void Camera::Update()
{
	// calculate the view matrix only if the transform has changed
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

	m_isDirty = false;
}

void Camera::Save()
{
	Serializer::SaveInt(static_cast<int>(m_projection));
	Serializer::SaveFloat(m_FOV);
	Serializer::SaveFloat(m_nearPlane);
	Serializer::SaveFloat(m_farPlane);
}

void Camera::Load()
{
	m_projection = static_cast<Projection>(Serializer::LoadInt());
	m_FOV = Serializer::LoadFloat();
	m_nearPlane = Serializer::LoadFloat();
	m_farPlane = Serializer::LoadFloat();

	CalculateViewMatrix();
}

/*------------------------------------------------------------------------------
								[CONVERSIONS]
------------------------------------------------------------------------------*/
Vector2 Camera::WorldSpaceToScreenPoint(Vector3 point)
{
	float screenWidth = RESOLUTION_WIDTH;
	float screenHeight = RESOLUTION_HEIGHT;

	Vector3 localSpace = Vector3::Transform(point, GetViewMatrix());

	int screenX = ((localSpace.x / localSpace.z) * (screenWidth * 0.5f)) + (screenWidth * 0.5f);
	int screenY = -((localSpace.y / localSpace.z) * (screenHeight * 0.5f)) + (screenHeight * 0.5f);

	return Vector2(screenX, screenY);
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
	return MathHelper::GetInstance().RadiansToDegrees(m_FOV);
}

void Camera::SetFieldOfView(float fov)
{
	m_FOV = MathHelper::GetInstance().DegreesToRadians(fov);

	m_isDirty = true;
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
	Matrix rotationMatrix = Matrix::CreateFromYawPitchRoll(0, 0, 0);
	Vector3 lookAt = Vector3::Transform(Vector3::Forward, rotationMatrix);
	lookAt = Vector3::Normalize(lookAt);
	m_baseViewMatrix = Matrix::CreateLookAtLH(Vector3(0, 0, -0.3), lookAt, Vector3::Up);
}

void Camera::CalculateProjectionMatrix()
{
	m_perspectiveProjectionMatrix = Matrix::CreatePerspectiveFieldOfViewLH(m_FOV, Settings::GetInstance().GetScreenAspect(), m_nearPlane, m_farPlane);
	m_orthographicProjectionMatrix = Matrix::CreateOrthographicLH(RESOLUTION_WIDTH, RESOLUTION_HEIGHT, m_nearPlane, m_farPlane);
}
