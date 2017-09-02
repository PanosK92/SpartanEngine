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
	Cascade::Cascade(int cascade, int resolution, Camera* camera, Graphics* device)
	{
		m_cascade = cascade;
		m_depthMap = make_unique<D3D11RenderTexture>(device);
		m_depthMap->Create(resolution, resolution, true);
		m_camera = camera;
	}

	void Cascade::SetAsRenderTarget()
	{
		m_depthMap->Clear(0.0f, 0.0f, 0.0f, 1.0f);
		m_depthMap->SetAsRenderTarget();
	}

	Matrix Cascade::CalculateProjectionMatrix(const Vector3 centerPos, const Matrix& viewMatrix)
	{
		// Hardcoded radius
		float radius = 0;
		if (m_cascade == 0)
			radius = 20;

		if (m_cascade == 1)
			radius = 40;

		if (m_cascade == 2)
			radius = 80;

		Vector3 center = centerPos * viewMatrix;
		Vector3 min = center - Vector3(radius, radius, radius);
		Vector3 max = center + Vector3(radius, radius, radius);

		return Matrix::CreateOrthoOffCenterLH(min.x, max.x, min.y, max.y, min.z, max.z);
	}

	float Cascade::GetSplit()
	{
		if (!m_camera)
		{
			LOG_WARNING("Cascade split can't be computed, camera is not present.");
			return 0;
		}

		float splitDistance = 0;

		if (m_cascade == 0)
			splitDistance = 0.0f;

		if (m_cascade == 1)
			splitDistance = 0.6f;

		if (m_cascade == 2)
			splitDistance = 0.8f;

		Vector4 shaderSplit = Vector4::Transform(Vector3(0, 0, splitDistance), m_camera->GetProjectionMatrix());
		return shaderSplit.z / shaderSplit.w;
	}

	Light::Light()
	{
		m_lightType = Point;
		m_shadowType = Hard_Shadows;
		m_range = 1.0f;
		m_intensity = 2.0f;
		m_angle = 0.5f; // about 30 degrees
		m_color = Vector4(1.0f, 0.76f, 0.57f, 1.0f);
		m_bias = 0.04f;
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
		for (int i = 0; i < m_cascades; i++)
		{
			Camera* camera = g_context->GetSubsystem<Scene>()->GetMainCamera()._Get()->GetComponent<Camera>();
			Graphics* graphics = g_context->GetSubsystem<Graphics>();
			auto shadowMap = make_shared<Cascade>(i, SHADOWMAP_RESOLUTION, camera, graphics);
			m_shadowMaps.push_back(shadowMap);
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

	float Light::GetShadowTypeAsFloat() const
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

	Matrix Light::ComputeViewMatrix()
	{
		Vector3 lightDirection = GetDirection();
		Vector3 position = lightDirection;
		Vector3 lookAt = position + lightDirection;
		Vector3 up = Vector3::Up;

		// Create the view matrix
		m_viewMatrix = Matrix::CreateLookAtLH(position, lookAt, up);

		return m_viewMatrix;
	}

	Matrix Light::ComputeOrthographicProjectionMatrix(int cascade)
	{
		if (cascade >= m_shadowMaps.size())
			return Matrix::Identity;

		sharedGameObj mainCamera = g_context->GetSubsystem<Scene>()->GetMainCamera().lock();
		Vector3 centerPos = mainCamera ? mainCamera->GetTransform()->GetPosition() : Vector3::Zero;
		return m_shadowMaps[cascade]->CalculateProjectionMatrix(centerPos, ComputeViewMatrix());
	}

	void Light::SetShadowCascadeAsRenderTarget(int cascade)
	{
		if (cascade < m_shadowMaps.size())
			m_shadowMaps[cascade]->SetAsRenderTarget();
	}

	weak_ptr<Cascade> Light::GetShadowCascade(int cascade)
	{
		if (cascade < m_shadowMaps.size())
			return m_shadowMaps[cascade];

		return weak_ptr<Cascade>();
	}

	float Light::GetShadowCascadeSplit(int cascade)
	{
		if (cascade < m_shadowMaps.size())
			return m_shadowMaps[cascade]->GetSplit();

		return 0.0f;
	}

	bool Light::IsInViewFrustrum(MeshFilter* meshFilter)
	{
		BoundingBox box = meshFilter->GetBoundingBoxTransformed();
		Vector3 center = box.GetCenter();
		Vector3 extents = box.GetHalfSize();

		return m_frustrum->CheckCube(center, extents) != Outside;
	}
}
