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

//= INCLUDES =============================================
#include "Light.h"
#include "Transform.h"
#include "Camera.h"
#include "Renderable.h"
#include "../Scene.h"
#include "../../Core/Settings.h"
#include "../../Core/Context.h"
#include "../../Math/BoundingBox.h"
#include "../../Math/Frustum.h"
#include "../../Rendering/RI/Backend_Imp.h"
#include "../../Rendering/RI/D3D11//D3D11_RenderTexture.h"
#include "../../Rendering/ShadowCascades.h"
#include "../../Scene/GameObject.h"
#include "../../Logging/Log.h"
#include "../../IO/FileStream.h"
//========================================================

//= NAMESPACES ================
using namespace Directus::Math;
using namespace std;
//=============================

namespace Directus
{
	Light::Light(Context* context, GameObject* gameObject, Transform* transform) : IComponent(context, gameObject, transform)
	{
		m_lightType			= LightType_Point;
		m_castShadows		= true;
		m_range				= 1.0f;
		m_intensity			= 2.0f;
		m_angle				= 0.5f; // about 30 degrees
		m_color				= Vector4(1.0f, 0.76f, 0.57f, 1.0f);
		m_bias				= 0.001f;	
		m_frustum			= make_shared<Frustum>();
		m_shadowCascades	= make_shared<ShadowCascades>(context, 3, SHADOWMAP_RESOLUTION, this);
		m_isDirty			= true;
	}

	Light::~Light()
	{
		m_shadowCascades->SetEnabled(false);
	}

	void Light::OnInitialize()
	{
		m_shadowCascades->SetEnabled(true);
	}

	void Light::OnStart()
	{
		
	}

	void Light::OnUpdate()
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
				m_frustum->Construct(ComputeViewMatrix(), m_shadowCascades->ComputeProjectionMatrix(2), cameraComp->GetFarPlane());
			}
		}
	}

	void Light::Serialize(FileStream* stream)
	{
		stream->Write(int(m_lightType));
		stream->Write(m_castShadows);
		stream->Write(m_color);
		stream->Write(m_range);
		stream->Write(m_intensity);
		stream->Write(m_angle);
		stream->Write(m_bias);
	}

	void Light::Deserialize(FileStream* stream)
	{
		m_lightType = LightType(stream->ReadInt());
		stream->Read(&m_castShadows);
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

	void Light::SetCastShadows(bool castShadows)
	{
		m_castShadows = castShadows;
		m_shadowCascades->SetEnabled(m_castShadows);
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
			GetTransform()->SetRotation(Quaternion::FromEulerAngles(179.0f, rotation.y, rotation.z));
		}
		if (rotation.x >= 180.0f)
		{
			GetTransform()->SetRotation(Quaternion::FromEulerAngles(1.0f, rotation.y, rotation.z));
		}
	}

	Matrix Light::ComputeViewMatrix()
	{
		// Only re-compute if dirty
		if (!m_isDirty)
			return m_viewMatrix;

		// Used to prevent directional light
		// from casting shadows form underneath
		// the scene which can look weird
		ClampRotation();

		Vector3 lightDirection	= GetDirection();
		Vector3 position		= lightDirection;
		Vector3 lookAt			= position + lightDirection;
		Vector3 up				= Vector3::Up;

		// Create the view matrix
		m_viewMatrix = Matrix::CreateLookAtLH(position, lookAt, up);

		return m_viewMatrix;
	}

	bool Light::IsInViewFrustrum(Renderable* renderable)
	{
		BoundingBox box = renderable->Geometry_BB();
		Vector3 center	= box.GetCenter();
		Vector3 extents = box.GetExtents();

		return m_frustum->CheckCube(center, extents) != Outside;
	}
}
