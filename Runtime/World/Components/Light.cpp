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

//= INCLUDES ============================
#include "Light.h"
#include "Transform.h"
#include "Camera.h"
#include "../../IO/FileStream.h"
#include "../../Rendering/Renderer.h"
#include "../../Core/Context.h"
#include "../../RHI/RHI_Texture2D.h"
#include "../../RHI/RHI_TextureCube.h"
#include "../../RHI/RHI_ConstantBuffer.h"
//=======================================

//= NAMESPACES ================
using namespace Spartan::Math;
using namespace std;
//=============================

namespace Spartan
{
	Light::Light(Context* context, Entity* entity, uint32_t id /*= 0*/) : IComponent(context, entity, id)
	{
		REGISTER_ATTRIBUTE_VALUE_VALUE(m_cast_shadows, bool);
		REGISTER_ATTRIBUTE_VALUE_VALUE(m_range, float);
		REGISTER_ATTRIBUTE_VALUE_VALUE(m_intensity, float);
		REGISTER_ATTRIBUTE_VALUE_VALUE(m_angle_rad, float);
		REGISTER_ATTRIBUTE_VALUE_VALUE(m_color, Vector4);
		REGISTER_ATTRIBUTE_VALUE_VALUE(m_bias, float);
		REGISTER_ATTRIBUTE_VALUE_VALUE(m_normal_bias, float);
		REGISTER_ATTRIBUTE_GET_SET(GetLightType, SetLightType, LightType);

		m_renderer = m_context->GetSubsystem<Renderer>().get();
	}

	void Light::OnInitialize()
	{
		CreateShadowMap(true);
	}

	void Light::OnStart()
	{
		CreateShadowMap(false);
	}

	void Light::OnTick(float delta_time)
	{
		// Position and rotation dirty check
		if (m_lastPosLight != GetTransform()->GetPosition() || m_lastRotLight != GetTransform()->GetRotation())
		{
			m_lastPosLight = GetTransform()->GetPosition();
			m_lastRotLight = GetTransform()->GetRotation();

			m_is_dirty = true;
		}

		// Camera dirty check (need for directional light cascade computations
		if (m_lightType == LightType_Directional)
		{
			if (auto& camera = m_renderer->GetCamera())
			{
				if (m_camera_last_view != camera->GetViewMatrix())
				{
                    m_camera_last_view = camera->GetViewMatrix();
					m_is_dirty = true;
				}
			}
		}

		if (!m_is_dirty)
			return;

        if (!ComputeCascadeSplits())
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

        m_is_dirty = false;
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

        // Temp tweak because only directional light shadows work without issues so far
        m_cast_shadows = GetLightType() == LightType_Directional;

		CreateShadowMap(true);
	}

	void Light::SetCastShadows(bool castShadows)
	{
		if (m_cast_shadows = castShadows)
			return;

		m_cast_shadows = castShadows;
		CreateShadowMap(true);
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
		if (m_lightType == LightType_Directional)
		{
            for (uint32_t i = 0; i < g_cascade_count; i++)
            {
                Cascade& cascade    = m_cascades[i];
                Vector3 position    = cascade.center - GetDirection() * cascade.max.z;
                Vector3 target      = cascade.center;
                Vector3 up          = Vector3::Up;
                m_matrix_view[i]    = Matrix::CreateLookAtLH(position, target, up);
            }
		}
		else if (m_lightType == LightType_Spot)
		{
            Vector3 position    = GetTransform()->GetPosition();
            Vector3 look_at		= GetTransform()->GetForward();
            Vector3 up			= GetTransform()->GetUp();

			// Offset look_at by current position
			look_at += position;

			// Compute
			m_matrix_view[0] = Matrix::CreateLookAtLH(position, look_at, up);
		}
		else if (m_lightType == LightType_Point)
		{
            Vector3 position = GetTransform()->GetPosition();

			// Compute view for each side of the cube map
			m_matrix_view[0] = Matrix::CreateLookAtLH(position, position + Vector3::Right,		Vector3::Up);		// x+
			m_matrix_view[1] = Matrix::CreateLookAtLH(position, position + Vector3::Left,		Vector3::Up);		// x-
			m_matrix_view[2] = Matrix::CreateLookAtLH(position, position + Vector3::Up,			Vector3::Backward);	// y+
			m_matrix_view[3] = Matrix::CreateLookAtLH(position, position + Vector3::Down,		Vector3::Forward);	// y-
			m_matrix_view[4] = Matrix::CreateLookAtLH(position, position + Vector3::Forward,	Vector3::Up);		// z+
			m_matrix_view[5] = Matrix::CreateLookAtLH(position, position + Vector3::Backward,	Vector3::Up);		// z-
		}
	}

	bool Light::ComputeProjectionMatrix(uint32_t index /*= 0*/)
	{
		if (!m_renderer->GetCamera() || index >= m_shadow_map->GetArraySize())
			return false;

		if (m_lightType == LightType_Directional)
		{
            Cascade& cascade        = m_cascades[index];
            float cascade_extents   = (cascade.max.z - cascade.min.z);
            m_matrix_projection[index] = Matrix::CreateOrthoOffCenterLH
            (
                cascade.min.x, cascade.max.x,                                                                           // x
                cascade.min.y, cascade.max.y,                                                                           // y
                m_renderer->GetReverseZ() ? cascade_extents : 0.0f, m_renderer->GetReverseZ() ? 0.0f : cascade_extents  // z
            );
		}
		else
		{
			const auto width			= static_cast<float>(m_shadow_map->GetWidth());
			const auto height			= static_cast<float>(m_shadow_map->GetHeight());		
			const auto aspect_ratio		= width / height;
			const float fov				= (m_lightType == LightType_Spot) ? m_angle_rad : 1.57079633f; // 1.57079633 = 90 deg
			const float near_plane		= m_renderer->GetReverseZ() ? m_range : 0.1f;
			const float far_plane		= m_renderer->GetReverseZ() ? 0.1f : m_range;
			m_matrix_projection[index]	= Matrix::CreatePerspectiveFieldOfViewLH(fov, aspect_ratio, near_plane, far_plane);
		}

		return true;
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

    bool Light::ComputeCascadeSplits()
    {
        if (!m_renderer || !m_renderer->GetCamera())
        {
            LOG_ERROR_INVALID_INTERNALS();
            return false;
        }

        if (m_cascades.empty())
        {
            m_cascades = vector<Cascade>(g_cascade_count);
        }

        Camera* camera = m_renderer->GetCamera().get();

        // Compute camera required information
        float clip_near         = camera->GetNearPlane();
        float clip_far          = camera->GetFarPlane();
        Vector3 camera_forward  = camera->GetTransform()->GetForward();
        Vector3 camera_up       = camera->GetTransform()->GetUp();
        Vector3 camera_right    = camera->GetTransform()->GetRight();
        Vector3 pos_near        = camera->GetTransform()->GetPosition() + clip_near * camera_forward;
        Vector3 pos_far         = camera->GetTransform()->GetPosition() + clip_far * camera_forward;
        float tanHalfHFOV       = Math::Tan(camera->GetFovHorizontalRad() / 2.0f);
        float tanHalfVFOV       = Math::Tan(camera->GetFovVerticalRad() / 2.0f);
        float near_height       = clip_near * tanHalfVFOV;
        float near_width        = clip_near * tanHalfHFOV;
        float far_height        = clip_far * tanHalfVFOV;
        float far_width         = clip_far * tanHalfHFOV;

        // Calculate split depths based on view camera frustum
        // Based on method presented in https://developer.nvidia.com/gpugems/GPUGems3/gpugems3_ch10.html
        float split_lambda  = 0.99f;
        float clip_range    = clip_far - clip_near;
        float min_z         = clip_near;
        float max_z         = clip_near + clip_range;
        float range         = max_z - min_z;
        float ratio         = max_z / min_z;    
        float splits[g_cascade_count];
        for (uint32_t i = 0; i < g_cascade_count; i++)
        {
            float p         = (i + 1) / static_cast<float>(g_cascade_count);
            float log       = min_z * Math::Pow(ratio, p);
            float uniform   = min_z + range * p;
            float d         = split_lambda * (log - uniform) + uniform;
            splits[i]       = (d - clip_near) / clip_range;
        }

        for (uint32_t cascade_index = 0; cascade_index < g_cascade_count; cascade_index++)
        {
            // Compute frustum corners in world space
            Vector3 frustum_corners[8] =
            {
                pos_near    - camera_up * near_height   - camera_right * near_width,    // near-bottom-left
                pos_near    + camera_up * near_height   - camera_right * near_width,    // near-top-left
                pos_near    + camera_up * near_height   + camera_right * near_width,    // near-top-right
                pos_near    - camera_up * near_height   + camera_right * near_width,    // near-bottom-right
                pos_far     - camera_up * far_height    - camera_right * far_width,     // far-bottom-left
                pos_far     + camera_up * far_height    - camera_right * far_width,     // far-top-left
                pos_far     + camera_up * far_height    + camera_right * far_width,     // far-top-right
                pos_far     - camera_up * far_height    + camera_right * far_width      // far-bottom-right
            };

            // Compute split distance
            {
                // Reset split distance every time we restart
                static float last_split_distance;
                if (cascade_index == 0) last_split_distance = 0.0f;

                float split_distance = splits[cascade_index];
                for (uint32_t i = 0; i < 4; i++)
                {
                    Vector3 distance        = frustum_corners[i + 4] - frustum_corners[i];
                    frustum_corners[i + 4]  = frustum_corners[i] + (distance * split_distance);
                    frustum_corners[i]      = frustum_corners[i] + (distance * last_split_distance);
                }
                last_split_distance = splits[cascade_index];
            }

            // Compute frustum bounds
            {
                // Compute bounding sphere which encloses the frustum.
                // Since a sphere is rotational invariant it will keep the size of the orthographic
                // projection frustum same independent of eye view direction, hence eliminating shimmering.

                Cascade& cascade = m_cascades[cascade_index];

                // Compute center
                for (uint32_t i = 0; i < 8; i++)
                {
                    cascade.center += Vector3(frustum_corners[i]);
                }
                cascade.center /= 8.0f;

                // Compute radius
                float radius = 0.0f;
                for (uint32_t i = 0; i < 8; i++)
                {
                    float distance = (Vector3(frustum_corners[i]) - cascade.center).Length();
                    radius = Max(radius, distance);
                }
                radius = Ceil(radius * 16.0f) / 16.0f;

                // Compute min and max
                cascade.max = radius;
                cascade.min = -radius;
            }

            // Debug
            {
                //Vector3 offset  = camera_forward * 1.0f;
                //Vector4 color   = Vector4(cascade_index == 0 ? 1.0f : 0.0f, cascade_index == 1 ? 1.0f : 0.0f, cascade_index == 2 ? 1.0f : 0.0f, 1.0f);

                //// Draw only near as the next cascade's near will the far for this one
                //m_renderer->DrawLine(frustum_corners[0] + offset, frustum_corners[1] + offset, color, color);
                //m_renderer->DrawLine(frustum_corners[1] + offset, frustum_corners[2] + offset, color, color);
                //m_renderer->DrawLine(frustum_corners[2] + offset, frustum_corners[3] + offset, color, color);
                //m_renderer->DrawLine(frustum_corners[3] + offset, frustum_corners[0] + offset, color, color);
                //// Connect with far
                //m_renderer->DrawLine(frustum_corners[0] + offset, frustum_corners[4] + offset, color, color);
                //m_renderer->DrawLine(frustum_corners[1] + offset, frustum_corners[5] + offset, color, color);
                //m_renderer->DrawLine(frustum_corners[2] + offset, frustum_corners[6] + offset, color, color);
                //m_renderer->DrawLine(frustum_corners[3] + offset, frustum_corners[7] + offset, color, color);
            }
        }

        return true;
    }

    void Light::CreateShadowMap(bool force)
	{		
		if (!force && !m_shadow_map)
			return;

 		uint32_t resolution = m_renderer->GetShadowResolution();
		auto rhi_device		= m_renderer->GetRhiDevice();

		if (GetLightType() == LightType_Directional)
		{
			m_shadow_map = make_unique<RHI_Texture2D>(m_context, resolution, resolution, Format_D32_FLOAT, g_cascade_count);
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

    void Light::UpdateConstantBuffer(bool volumetric_lighting, bool screen_space_contact_shadows)
    {
        // Has to match GBuffer.hlsl
        if (!m_cb_light_gpu)
        {
            m_cb_light_gpu = make_shared<RHI_ConstantBuffer>(m_context->GetSubsystem<Renderer>()->GetRhiDevice());
            m_cb_light_gpu->Create<CB_Light>();
        }

        // Update buffer
        auto buffer = static_cast<CB_Light*>(m_cb_light_gpu->Map());

        for (int i = 0; i < g_cascade_count; i++)
        {
            buffer->view_projection[i] = GetViewMatrix(i) * GetProjectionMatrix(i);
        }
        buffer->color                           = m_color;
        buffer->intensity                       = GetIntensity();
        buffer->position                        = GetTransform()->GetPosition();
        buffer->range                           = GetRange();
        buffer->direction                       = GetDirection();
        buffer->angle                           = GetAngle();
        buffer->bias                            = m_renderer->GetReverseZ() ? GetBias() : -GetBias();
        buffer->normal_bias                     = GetNormalBias();
        buffer->shadow_enabled                  = GetCastShadows();
        buffer->volumetric_lighting             = volumetric_lighting;
        buffer->screen_space_contact_shadows    = screen_space_contact_shadows;

        m_cb_light_gpu->Unmap();
    }
}  
