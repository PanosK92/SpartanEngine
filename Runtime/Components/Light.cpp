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

//= INCLUDES ===================
#include "Light.h"
#include "Transform.h"
#include "Camera.h"
#include "../IO/StreamIO.h"
#include "../Core/Scene.h"
#include "../Core/Settings.h"
#include "../Core/Context.h"
#include "../Core/GameObject.h"
#include "../Logging/Log.h"
#include "../Math/BoundingBox.h"
#include "MeshFilter.h"
#include "../Math/Frustrum.h"
//==============================

//= NAMESPACES ================
using namespace Directus::Math;
using namespace std;
//=============================

namespace Directus
{
	Light::Light()
	{
		m_lightType = Point;
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

	void Light::Reset()
	{

	}

	void Light::Start()
	{
		if (!m_shadowMaps.empty())
			return;

		m_shadowMaps.clear();
		Camera* camera = g_context->GetSubsystem<Scene>()->GetMainCamera()._Get()->GetComponent<Camera>();
		for (int i = 0; i < m_cascades; i++)
		{
			auto cascade = make_shared<Cascade>(SHADOWMAP_RESOLUTION, camera, g_context);
			m_shadowMaps.push_back(cascade);
		}
	}

	void Light::OnDisable()
	{

	}

	void Light::Remove()
	{

	}

	void Light::Update()
	{
		if (m_lightType != Directional)
			return;

		// DIRTY CHECK
		if (m_lastKnownRotation != g_transform->GetRotation())
		{
			m_lastKnownRotation = g_transform->GetRotation();
			m_isDirty = true;
		}

		if (!m_isDirty)
			return;

		// Used to prevent directional light
		// from casting shadows form underneath
		// the scene which can look weird
		ClampRotation();

		Camera* mainCamera = g_context->GetSubsystem<Scene>()->GetMainCamera()._Get()->GetComponent<Camera>();
		m_frustrum->Construct(ComputeViewMatrix(), ComputeOrthographicProjectionMatrix(2), mainCamera->GetFarPlane());
	}

	void Light::Serialize()
	{
		StreamIO::WriteInt(int(m_lightType));
		StreamIO::WriteInt(int(m_shadowType));
		StreamIO::WriteVector4(m_color);
		StreamIO::WriteFloat(m_range);
		StreamIO::WriteFloat(m_intensity);
		StreamIO::WriteFloat(m_angle);
		StreamIO::WriteFloat(m_bias);
	}

	void Light::Deserialize()
	{
		m_lightType = LightType(StreamIO::ReadInt());
		m_shadowType = ShadowType(StreamIO::ReadInt());
		m_color = StreamIO::ReadVector4();
		m_range = StreamIO::ReadFloat();
		m_intensity = StreamIO::ReadFloat();
		m_angle = StreamIO::ReadFloat();
		m_bias = StreamIO::ReadFloat();
	}

	void Light::SetLightType(LightType type)
	{
		m_lightType = type;
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
	}

	void Light::SetAngle(float angle)
	{
		m_angle = Clamp(angle, 0.0f, 1.0f);
	}

	Vector3 Light::GetDirection()
	{
		return g_transform->GetForward();
	}

	void Light::ClampRotation()
	{
		Vector3 rotation = g_transform->GetRotation().ToEulerAngles();
		if (rotation.x <= 0.0f)
		{
			g_transform->SetRotation(Quaternion::FromEulerAngles(Vector3(179.0f, rotation.y, rotation.z)));
		}
		if (rotation.x >= 180.0f)
		{
			g_transform->SetRotation(Quaternion::FromEulerAngles(Vector3(1.0f, rotation.y, rotation.z)));
		}
	}

	Matrix Light::ComputeViewMatrix()
	{
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

	Matrix Light::ComputeOrthographicProjectionMatrix(int cascadeIndex)
	{
		if (cascadeIndex >= m_shadowMaps.size())
			return Matrix::Identity;

		sharedGameObj mainCamera = g_context->GetSubsystem<Scene>()->GetMainCamera().lock();
		Vector3 centerPos = mainCamera ? mainCamera->GetTransform()->GetPosition() : Vector3::Zero;
		return m_shadowMaps[cascadeIndex]->ComputeProjectionMatrix(cascadeIndex, centerPos, ComputeViewMatrix());
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
		Vector3 extents = box.GetHalfSize();

		return m_frustrum->CheckCube(center, extents) != Outside;
	}
}
