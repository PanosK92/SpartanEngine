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

#pragma once

//= INCLUDES ==================
#include "IComponent.h"
#include "../Math/Vector2.h"
#include "../Math/Vector3.h"
#include "../Math/Vector4.h"
#include "../Math/Matrix.h"
#include "../Math/Quaternion.h"
#include "../Math/Frustrum.h"
#include <memory>
//=============================

enum Projection
{
	Perspective,
	Orthographic,
};

class DllExport Camera : public IComponent
{
public:
	Camera();
	~Camera();

	/*------------------------------------------------------------------------------
									[INTERFACE]
	------------------------------------------------------------------------------*/
	virtual void Reset();
	virtual void Start();
	virtual void OnDisable();
	virtual void Remove();
	virtual void Update();
	virtual void Serialize();
	virtual void Deserialize();

	/*------------------------------------------------------------------------------
									[MATRICES]
	------------------------------------------------------------------------------*/
	Directus::Math::Matrix GetViewMatrix() { return m_mView; }
	Directus::Math::Matrix GetProjectionMatrix() { return m_mProjection; }
	Directus::Math::Matrix GetBaseViewMatrix() { return m_mBaseView; }

	/*------------------------------------------------------------------------------
									[CONVERSIONS]
	------------------------------------------------------------------------------*/
	Directus::Math::Vector2 WorldToScreenPoint(const Directus::Math::Vector3& worldPoint);

	//= PLANES/PROJECTION ====================
	float GetNearPlane() { return m_nearPlane; }
	void SetNearPlane(float nearPlane);
	float GetFarPlane() { return m_farPlane; }
	void SetFarPlane(float farPlane);
	Projection GetProjection() { CalculateProjection();  return m_projection; }
	void SetProjection(Projection projection);
	float GetFieldOfView() { return Directus::Math::RadiansToDegrees(m_FOV); }
	void SetFieldOfView(float fov);
	const std::shared_ptr<Frustrum>& GetFrustrum() { return m_frustrum; }

	//= MISC ==================================
	Directus::Math::Vector4 GetClearColor() { return m_clearColor; }
	void SetClearColor(const Directus::Math::Vector4& color) { m_clearColor = color; }

private:
	float m_FOV;
	float m_nearPlane;
	float m_farPlane;
	std::shared_ptr<Frustrum> m_frustrum;
	Projection m_projection;
	Directus::Math::Vector4 m_clearColor;

	Directus::Math::Matrix m_mView;
	Directus::Math::Matrix m_mProjection;
	Directus::Math::Matrix m_mBaseView;

	Directus::Math::Vector3 m_position;
	Directus::Math::Quaternion m_rotation;
	bool m_isDirty;

	Directus::Math::Vector2 m_lastKnownResolution;

	/*------------------------------------------------------------------------------
									[PRIVATE]
	------------------------------------------------------------------------------*/
	void CalculateViewMatrix();
	void CalculateBaseView();
	void CalculateProjection();
};
