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

//= INCLUDES ======================
#include "Light.h"
#include "Transform.h"
#include "Camera.h"
#include "MeshFilter.h"
#include "../Scene.h"
#include "../../IO/FileStream.h"
#include "../../Core/Settings.h"
#include "../../Core/Context.h"
#include "../../Scene/GameObject.h"
#include "../../Logging/Log.h"
#include "../../Math/BoundingBox.h"
#include "../../Math/Frustrum.h"
//=================================

//= NAMESPACES ================
using namespace Directus::Math;
using namespace std;
//=============================

namespace Directus
{
	Light::Light()
	{
		m_lightType = LightType_Point;
		m_shadowType = Hard_Shadows;
		m_range = 1.0f;
		m_intensity = 2.0f;
		m_angle = 0.5f; // about 30 degrees
		m_color = Vector4(1.0f, 0.76f, 0.57f, 1.0f);
		m_bias = 0.001f;
		m_cascades = 3;
		m_frustrum = make_shared<Frustrum>();
		m_isDirty = true;
	}

	Light::~Light()
	{
		m_shadowMaps.clear();
	}

	void Light::Start()
	{
		if (!m_shadowMaps.empty())
			return;

		m_shadowMaps.clear();
		Camera* camera = GetContext()->GetSubsystem<Scene>()->GetMainCamera().lock()->GetComponent<Camera>().lock().get();
		for (int i = 0; i < m_cascades; i++)
		{
			auto cascade = make_shared<Cascade>(SHADOWMAP_RESOLUTION, camera, GetContext());
			m_shadowMaps.push_back(cascade);
		}
	}

	void Light::Update()
	{
		if (m_lightType != LightType_Directional)
			return;

		// DIRTY CHECK
		if (m_lastPos != GetTransform()->GetPosition() || m_lastRot != GetTransform()->GetRotation())
		{
			m_lastPos = GetTransform()->GetPosition();
			m_lastRot = GetTransform()->GetRotation();
			m_isDirty = true;
		}

		if (!m_isDirty)
			return;

		// Used to prevent directional light
		// from casting shadows from underneath
		// the scene which can look weird
		ClampRotation();

		if (auto mainCamera = GetContext()->GetSubsystem<Scene>()->GetMainCamera().lock().get())
		{
			if (auto cameraComp = mainCamera->GetComponent<Camera>().lock().get())
			{
				m_frustrum->Construct(GetViewMatrix(), GetOrthographicProjectionMatrix(2), cameraComp->GetFarPlane());
			}
		}
	}

	void Light::Serialize(FileStream* stream)
	{
		stream->Write(int(m_lightType));
		stream->Write(int(m_shadowType));
		stream->Write(m_color);
		stream->Write(m_range);
		stream->Write(m_intensity);
		stream->Write(m_angle);
		stream->Write(m_bias);
	}

	void Light::Deserialize(FileStream* stream)
	{
		m_lightType = LightType(stream->ReadInt());
		m_shadowType = ShadowType(stream->ReadInt());
		stream->Read(&m_color);
		stream->Read(&m_range);
		stream->Read(&m_intensity);
		stream->Read(&m_angle);
		stream->Read(&m_bias);
	}

	void Light::SetLightType(LightType type)
	{
		m_lightType = type;
		m_isDirty = true;
	}

	float Light::GetShadowTypeAsFloat()
	{
		if (m_shadowType == Hard_Shadows)
			return 0.5f;

		if (m_shadowType == Soft_Shadows)
			return 1.0f;

		return 0.0f;
	}

	void Light::SetRange(float range)
	{
		m_range = Clamp(range, 0.0f, INFINITY);
		m_isDirty = true;
	}

	void Light::SetAngle(float angle)
	{
		m_angle = Clamp(angle, 0.0f, 1.0f);
		m_isDirty = true;
	}

	Vector3 Light::GetDirection()
	{
		return GetTransform()->GetForward();
	}

	void Light::ClampRotation()
	{
		Vector3 rotation = GetTransform()->GetRotation().ToEulerAngles();
		if (rotation.x <= 0.0f)
		{
			GetTransform()->SetRotation(Quaternion::FromEulerAngles(Vector3(179.0f, rotation.y, rotation.z)));
		}
		if (rotation.x >= 180.0f)
		{
			GetTransform()->SetRotation(Quaternion::FromEulerAngles(Vector3(1.0f, rotation.y, rotation.z)));
		}
	}

	Matrix Light::GetViewMatrix()
	{
		// Only re-compute if dirty
		if (!m_isDirty)
			return m_viewMatrix;

		// Used to prevent directional light
		// from casting shadows form underneath
		// the scene which can look weird
		ClampRotation();

		Vector3 lightDirection = GetDirection();
		Vector3 position = lightDirection;
		Vector3 lookAt = position + lightDirection;
		Vector3 up = Vector3::Up;

		// Create the view matrix
		m_viewMatrix = Matrix::CreateLookAtLH(position, lookAt, up);

		return m_viewMatrix;
	}

	Matrix Light::GetOrthographicProjectionMatrix(int cascadeIndex)
	{
		if (!GetContext() || cascadeIndex >= m_shadowMaps.size())
			return Matrix::Identity;

		// Only re-compute if dirty
		if (!m_isDirty)
			return m_projectionMatrix;

		auto mainCamera = GetContext()->GetSubsystem<Scene>()->GetMainCamera().lock();
		Vector3 centerPos = mainCamera ? mainCamera->GetTransform()->GetPosition() : Vector3::Zero;
		m_projectionMatrix = m_shadowMaps[cascadeIndex]->ComputeProjectionMatrix(cascadeIndex, centerPos, GetViewMatrix());
		return m_projectionMatrix;
	}

	void Light::SetShadowCascadeAsRenderTarget(int cascade)
	{
		if (cascade < m_shadowMaps.size())
			m_shadowMaps[cascade]->SetAsRenderTarget();
	}

	weak_ptr<Cascade> Light::GetShadowCascade(int cascadeIndex)
	{
		if (cascadeIndex >= m_shadowMaps.size())
			return weak_ptr<Cascade>();

		return m_shadowMaps[cascadeIndex];
	}

	float Light::GetShadowCascadeSplit(int cascadeIndex)
	{
		if (cascadeIndex >= m_shadowMaps.size())
			return 0.0f;

		return m_shadowMaps[cascadeIndex]->GetSplit(cascadeIndex);
	}

	bool Light::IsInViewFrustrum(MeshFilter* meshFilter)
	{
		BoundingBox box = meshFilter->GetBoundingBoxTransformed();
		Vector3 center = box.GetCenter();
		Vector3 extents = box.GetExtents();

		return m_frustrum->CheckCube(center, extents) != Outside;
	}
}
