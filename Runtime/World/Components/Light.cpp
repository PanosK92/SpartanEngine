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

//= INCLUDES =========================
#include "Light.h"
#include "Transform.h"
#include "Camera.h"
#include "../../IO/FileStream.h"
#include "../../Rendering/Renderer.h"
#include "../../Core/Context.h"
#include "../../RHI/RHI_Texture2D.h"
#include "../../RHI/RHI_TextureCube.h"
//====================================

//= NAMESPACES ================
using namespace Spartan::Math;
using namespace std;
//=============================

namespace Spartan
{
	Light::Light(Context* context, Entity* entity, Transform* transform) : IComponent(context, entity, transform)
	{
		REGISTER_ATTRIBUTE_VALUE_VALUE(m_cast_shadows, bool);
		REGISTER_ATTRIBUTE_VALUE_VALUE(m_range, float);
		REGISTER_ATTRIBUTE_VALUE_VALUE(m_intensity, float);
		REGISTER_ATTRIBUTE_VALUE_VALUE(m_angle_rad, float);
		REGISTER_ATTRIBUTE_VALUE_VALUE(m_color, Vector4);
		REGISTER_ATTRIBUTE_VALUE_VALUE(m_bias, float);
		REGISTER_ATTRIBUTE_VALUE_VALUE(m_normal_bias, float);
		REGISTER_ATTRIBUTE_GET_SET(GetLightType, SetLightType, LightType);

		m_color		= Vector4(1.0f, 0.76f, 0.57f, 1.0f);
		m_renderer	= m_context->GetSubsystem<Renderer>().get();
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
		// Position and rotation dirty check
		if (m_lastPosLight != GetTransform()->GetPosition() || m_lastRotLight != GetTransform()->GetRotation())
		{
			m_lastPosLight = GetTransform()->GetPosition();
			m_lastRotLight = GetTransform()->GetRotation();

			m_is_dirty = true;
		}

		// Camera position dirty check
		if (m_lightType == LightType_Directional)
		{
			if (auto camera = m_renderer->GetCamera())
			{
				if (m_lastPosCamera != camera->GetTransform()->GetPosition())
				{
					m_lastPosCamera = camera->GetTransform()->GetPosition();
					m_is_dirty = true;
				}
			}
		}

		if (!m_is_dirty)
			return;

		// Prevent directional light from casting shadows 
		// from underneath the scene, which can look weird
		if (m_lightType == LightType_Directional)
		{
			ClampRotation();
		}

		// Update view matrix
		ComputeViewMatrix();

		// Update projection matrix
		for (uint32_t i = 0; i < m_shadow_map->GetArraySize(); i++)
		{
			ComputeProjectionMatrix(i);
		}
	}

	void Light::Serialize(FileStream* stream)
	{
		stream->Write(static_cast<uint32_t>(m_lightType));
		stream->Write(m_cast_shadows);
		stream->Write(m_color);
		stream->Write(m_range);
		stream->Write(m_intensity);
		stream->Write(m_angle_rad);
		stream->Write(m_bias);
		stream->Write(m_normal_bias);
	}

	void Light::Deserialize(FileStream* stream)
	{
		SetLightType(static_cast<LightType>(stream->ReadAs<uint32_t>()));
		stream->Read(&m_cast_shadows);
		stream->Read(&m_color);
		stream->Read(&m_range);
		stream->Read(&m_intensity);
		stream->Read(&m_angle_rad);
		stream->Read(&m_bias);
		stream->Read(&m_normal_bias);
	}

	void Light::SetLightType(LightType type)
	{
		m_lightType = type;
		m_is_dirty	= true;
		ShadowMap_Create(true);
	}

	void Light::SetCastShadows(bool castShadows)
	{
		if (m_cast_shadows = castShadows)
			return;

		m_cast_shadows = castShadows;
		ShadowMap_Create(true);
	}

	void Light::SetRange(float range)
	{
		m_range = Clamp(range, 0.0f, INFINITY);
	}

	void Light::SetAngle(float angle)
	{
		m_angle_rad = Clamp(angle, 0.0f, 1.0f);
		m_is_dirty = true;
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
		Vector3 position;
		Vector3 look_at;
		Vector3 up;

		if (m_lightType == LightType_Directional)
		{
			Vector3 direction	= GetDirection();
			position			= direction;
			look_at				= position + direction;
			up					= Vector3::Up;
			
			// Compute
			m_matrix_view[0] = Matrix::CreateLookAtLH(position, look_at, up);
			m_matrix_view[1] = m_matrix_view[0];
			m_matrix_view[2] = m_matrix_view[0];
		}
		else if (m_lightType == LightType_Spot)
		{
			position	= GetTransform()->GetPosition();
			look_at		= GetTransform()->GetForward();
			up			= GetTransform()->GetUp();

			// Offset look_at by current position
			look_at += position;

			// Compute
			m_matrix_view[0] = Matrix::CreateLookAtLH(position, look_at, up);
		}
		else if (m_lightType == LightType_Point)
		{
			position = GetTransform()->GetPosition();

			// Compute view for each side of the cube map
			m_matrix_view[0] = Matrix::CreateLookAtLH(position, position + Vector3::Right,		Vector3::Up);		// x+
			m_matrix_view[1] = Matrix::CreateLookAtLH(position, position + Vector3::Left,		Vector3::Up);		// x-
			m_matrix_view[2] = Matrix::CreateLookAtLH(position, position + Vector3::Up,			Vector3::Backward);	// y+
			m_matrix_view[3] = Matrix::CreateLookAtLH(position, position + Vector3::Down,		Vector3::Forward);	// y-
			m_matrix_view[4] = Matrix::CreateLookAtLH(position, position + Vector3::Forward,	Vector3::Up);		// z+
			m_matrix_view[5] = Matrix::CreateLookAtLH(position, position + Vector3::Backward,	Vector3::Up);		// z-
		}
	}

	const Matrix& Light::GetViewMatrix(uint32_t index /*= 0*/)
	{
		if (index >= static_cast<uint32_t>(m_matrix_view.size()))
			return Matrix::Identity;

		return m_matrix_view[index];
	}

	const Matrix& Light::GetProjectionMatrix(uint32_t index /*= 0*/)
	{
		if (index >= static_cast<uint32_t>(m_matrix_projection.size()))
			return Matrix::Identity;

		return m_matrix_projection[index];
	}

	bool Light::ComputeProjectionMatrix(uint32_t index /*= 0*/)
	{
		if (!m_renderer->GetCamera() || index >= m_shadow_map->GetArraySize())
			return false;

		const auto& camera = m_renderer->GetCamera();
		const auto& camera_transform = camera->GetTransform();

		if (m_lightType == LightType_Directional)
		{		
			float splits[3] =
			{
				camera->GetFarPlane() * 0.01f,
				camera->GetFarPlane() * 0.05f,
				camera->GetFarPlane()
			};

			float split		= splits[index];
			float extent	= split * tan(camera->GetFovHorizontalRad() * 0.5f);

			Vector3 box_center	= (camera_transform->GetPosition() + camera_transform->GetForward() * split * 0.5f) * GetViewMatrix(); // Transform to light space
			Vector3 box_extent	= Vector3(extent) * GetTransform()->GetRotation();	// Rotate towards light direction
			Vector3 box_min		= box_center - box_extent;
			Vector3 box_max		= box_center + box_extent;

			//= Prevent shadow shimmering  ========================================================
			// Shadow shimmering remedy based on
			// https://msdn.microsoft.com/en-us/library/windows/desktop/ee416324(v=vs.85).aspx
			float units_per_texel = (extent * 2.0f) / static_cast<float>(m_shadow_map->GetWidth());
			box_min /= units_per_texel;
			box_min.Floor();
			box_min *= units_per_texel;
			box_max /= units_per_texel;
			box_max.Floor();
			box_max *= units_per_texel;
			//=====================================================================================

			if (Settings::Get().GetReverseZ())
				m_matrix_projection[index] = Matrix::CreateOrthoOffCenterLH(box_min.x, box_max.x, box_min.y, box_max.y, box_max.z, box_min.z);
			else
				m_matrix_projection[index] = Matrix::CreateOrthoOffCenterLH(box_min.x, box_max.x, box_min.y, box_max.y, box_min.z, box_max.z);
		}
		else
		{
			const auto width			= static_cast<float>(m_shadow_map->GetWidth());
			const auto height			= static_cast<float>(m_shadow_map->GetHeight());		
			const auto aspect_ratio		= width / height;
			const float fov				= (m_lightType == LightType_Spot) ? m_angle_rad : 1.57079633f; // 1.57079633 = 90 deg
			const float near_plane		= !Settings::Get().GetReverseZ() ? 0.1f : m_range;
			const float far_plane		= !Settings::Get().GetReverseZ() ? m_range : 0.1f;
			m_matrix_projection[index]	= Matrix::CreatePerspectiveFieldOfViewLH(fov, aspect_ratio, near_plane, far_plane);
		}

		return true;
	}

	void Light::ShadowMap_Create(bool force)
	{		
		if (!force && !m_shadow_map)
			return;

		uint32_t resolution = Settings::Get().GetShadowResolution();
		auto rhi_device			= m_context->GetSubsystem<Renderer>()->GetRhiDevice();

		if (GetLightType() == LightType_Directional)
		{
			m_shadow_map = make_unique<RHI_Texture2D>(m_context, resolution, resolution, Format_D32_FLOAT, 3);
		}
		else if (GetLightType() == LightType_Point)
		{
			m_shadow_map = make_unique<RHI_TextureCube>(m_context, resolution, resolution, Format_D32_FLOAT);			
		}
		else if (GetLightType() == LightType_Spot)
		{
			m_shadow_map = make_unique<RHI_Texture2D>(m_context, resolution, resolution, Format_D32_FLOAT, 1);
		}
	}
}  