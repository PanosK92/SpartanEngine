/*
Copyright(c) 2016-2019 Panos Karabelas

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

//= INCLUDES =============================
#include "Light.h"
#include "Transform.h"
#include "Camera.h"
#include "Renderable.h"
#include "../../World/Entity.h"
#include "../../IO/FileStream.h"
#include "../../Rendering/Renderer.h"
#include "../../RHI/RHI_RenderTexture.h"
//========================================

//= NAMESPACES ================
using namespace Directus::Math;
using namespace std;
//=============================

namespace Directus
{
	Light::Light(Context* context, Entity* entity, Transform* transform) : IComponent(context, entity, transform)
	{
		REGISTER_ATTRIBUTE_VALUE_VALUE(m_castShadows, bool);
		REGISTER_ATTRIBUTE_VALUE_VALUE(m_range, float);
		REGISTER_ATTRIBUTE_VALUE_VALUE(m_intensity, float);
		REGISTER_ATTRIBUTE_VALUE_VALUE(m_angle, float);
		REGISTER_ATTRIBUTE_VALUE_VALUE(m_color, Vector4);
		REGISTER_ATTRIBUTE_VALUE_VALUE(m_bias, float);
		REGISTER_ATTRIBUTE_VALUE_VALUE(m_normalBias, float);
		REGISTER_ATTRIBUTE_GET_SET(GetLightType, SetLightType, LightType);

		m_color = Vector4(1.0f, 0.76f, 0.57f, 1.0f);
		m_renderer = m_context->GetSubsystem<Renderer>().get();
	}

	Light::~Light()
	{
		
	}

	void Light::OnInitialize()
	{
		ShadowMap_Create(true);
	}

	void Light::OnStart()
	{
		ShadowMap_Create(false);
	}

	void Light::OnTick()
	{
		if (m_lightType != LightType_Directional)
			return;

		// DIRTY CHECK
		if (m_lastPosLight != GetTransform()->GetPosition() || m_lastRotLight != GetTransform()->GetRotation())
		{
			m_lastPosLight = GetTransform()->GetPosition();
			m_lastRotLight = GetTransform()->GetRotation();
			
			// Used to prevent directional light
			// from casting shadows form underneath
			// the scene which can look weird
			ClampRotation();
			ComputeViewMatrix();

			m_isDirty = true;
		}

		// Acquire camera
		if (auto camera = m_renderer->GetCamera())
		{
			if (m_lastPosCamera != camera->GetTransform()->GetPosition())
			{
				m_lastPosCamera = camera->GetTransform()->GetPosition();

				// Update shadow map projection matrices
				m_shadowMapsProjectionMatrix.clear();
				for (unsigned int i = 0; i < m_shadowMap->GetArraySize(); i++)
				{
					m_shadowMapsProjectionMatrix.emplace_back(Matrix());
					ShadowMap_ComputeProjectionMatrix(i);
				}

				m_isDirty = true;
			}
		}

		if (!m_isDirty)
			return;
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
		stream->Write(m_normalBias);
	}

	void Light::Deserialize(FileStream* stream)
	{
		SetLightType(LightType(stream->ReadInt()));
		stream->Read(&m_castShadows);
		stream->Read(&m_color);
		stream->Read(&m_range);
		stream->Read(&m_intensity);
		stream->Read(&m_angle);
		stream->Read(&m_bias);
		stream->Read(&m_normalBias);
	}

	void Light::SetLightType(LightType type)
	{
		m_lightType = type;
		m_isDirty = true;
		ShadowMap_Create(true);
	}

	void Light::SetCastShadows(bool castShadows)
	{
		if (m_castShadows = castShadows)
			return;

		m_castShadows = castShadows;
		ShadowMap_Create(true);
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

	void Light::ComputeViewMatrix()
	{
		Vector3 lightDirection	= GetDirection();
		Vector3 position		= lightDirection;
		Vector3 lookAt			= position + lightDirection;
		Vector3 up				= Vector3::Up;

		// Create the view matrix
		m_viewMatrix = Matrix::CreateLookAtLH(position, lookAt, up);
	}

	const Matrix& Light::ShadowMap_GetProjectionMatrix(unsigned int index /*= 0*/)
	{
		if (index >= (unsigned int)m_shadowMapsProjectionMatrix.size())
			return Matrix::Identity;

		return m_shadowMapsProjectionMatrix[index];
	}

	bool Light::ShadowMap_ComputeProjectionMatrix(unsigned int index /*= 0*/)
	{
		if (!m_renderer->GetCamera() || index >= m_shadowMap->GetArraySize())
			return false;

		float camera_far			= m_renderer->GetCamera()->GetFarPlane();
		const Matrix& camera_view	= m_renderer->GetCamera()->GetViewMatrix();

		vector<float> extents =
		{
			10,
			40,
			camera_far * 0.5f
		};
		float extent = extents[index];

		Vector3 box_center	= m_lastPosCamera * GetViewMatrix();						// Follow the camera
		Vector3 box_extent	= Vector3(extents[index]) * GetTransform()->GetRotation();	// Rotate towards light direction
		Vector3 box_min		= box_center - box_extent;
		Vector3 box_max		= box_center + box_extent;

		//= Prevent shadow shimmering  ===================================================
		// Shadow shimmering remedy based on
		// https://msdn.microsoft.com/en-us/library/windows/desktop/ee416324(v=vs.85).aspx
		float worldUnitsPerTexel = (extent * 2.0f) / m_shadowMap->GetWidth();
		box_min /= worldUnitsPerTexel;
		box_min.Floor();
		box_min *= worldUnitsPerTexel;
		box_max /= worldUnitsPerTexel;
		box_max.Floor();
		box_max *= worldUnitsPerTexel;
		//================================================================================

		#if REVERSE_Z == 1
		m_shadowMapsProjectionMatrix[index] = Matrix::CreateOrthoOffCenterLH(box_min.x, box_max.x, box_min.y, box_max.y, box_max.z, box_min.z);
		#else
		m_shadowMapsProjectionMatrix[index] = Matrix::CreateOrthoOffCenterLH(box_min.x, box_max.x, box_min.y, box_max.y, box_min.z, box_max.z);
		#endif

		return true;
	}

	void Light::ShadowMap_Create(bool force)
	{		
		if (!force && !m_shadowMap)
			return;

		m_shadowMap.reset();
	
		// Compute array size
		int arraySize = 0;
		if (GetLightType() == LightType_Directional)
		{
			arraySize = 3; // cascades
		}
		else if (GetLightType() == LightType_Point)
		{
			arraySize = 6; // points of view
		}
		else if (GetLightType() == LightType_Spot)
		{
			arraySize = 1;
		}

		// Create the shadow maps
		unsigned int resolution	= Settings::Get().Shadows_GetResolution();
		auto rhiDevice			= m_context->GetSubsystem<Renderer>()->GetRHIDevice();
		m_shadowMap				= make_unique<RHI_RenderTexture>(rhiDevice, resolution, resolution, Format_R32_FLOAT, true, Format_D32_FLOAT, arraySize); // could use the g-buffers depth which should be same res
	}
}