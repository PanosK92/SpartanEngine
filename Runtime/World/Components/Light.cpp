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
#include "Renderable.h"
#include "../../IO/FileStream.h"
#include "../../Rendering/Renderer.h"
#include "../../RHI/RHI_Texture2D.h"
#include "../../RHI/RHI_TextureCube.h"
//====================================

//= NAMESPACES ===============
using namespace Spartan::Math;
using namespace std;
//============================

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
        m_cascades = vector<Cascade>(g_cascade_count);
	}

	void Light::OnInitialize()
	{

	}

	void Light::OnStart()
	{
		CreateShadowMap(false);
	}

	void Light::OnTick(float delta_time)
	{
        CreateShadowMap(false);

		// Position and rotation dirty check
		if (m_lastPosLight != GetTransform()->GetPosition() || m_lastRotLight != GetTransform()->GetRotation())
		{
			m_lastPosLight = GetTransform()->GetPosition();
			m_lastRotLight = GetTransform()->GetRotation();

			m_is_dirty = true;
		}

        // Used in many places, no point in continuing without it
        if (!m_renderer)
        {
            LOG_ERROR_INVALID_INTERNALS();
            return;
        }

		// Camera dirty check (need for directional light cascade computations
		if (m_light_type == LightType_Directional)
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

		// Update shadow map(s)
        if (m_shadow_map)
        {
            ComputeCascadeSplits();
            ComputeViewMatrix();

            for (uint32_t i = 0; i < m_shadow_map->GetArraySize(); i++)
            {
                ComputeProjectionMatrix(i);
            }
        }

        m_is_dirty = false;
	}

	void Light::Serialize(FileStream* stream)
	{
		stream->Write(static_cast<uint32_t>(m_light_type));
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
        if (m_light_type == type)
            return;

		m_light_type    = type;
		m_is_dirty      = true;

        if (m_cast_shadows)
        {
            CreateShadowMap(true);
        }
	}

	void Light::SetCastShadows(bool cast_shadows)
	{
		if (m_cast_shadows == cast_shadows)
			return;

		m_cast_shadows = cast_shadows;

        if (m_cast_shadows)
        {
            CreateShadowMap(true);
        }
	}

	void Light::SetRange(float range)
	{
		m_range = Clamp(range, 0.0f, INFINITY);
	}

	void Light::SetAngle(float angle)
	{
		m_angle_rad = Clamp(angle, 0.0f, 1.0f);
		m_is_dirty  = true;
	}

	Vector3 Light::GetDirection() const
    {
		return GetTransform()->GetForward();
	}

	void Light::ComputeViewMatrix()
	{
		if (m_light_type == LightType_Directional)
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
		else if (m_light_type == LightType_Spot)
		{
            const Vector3 position  = GetTransform()->GetPosition();
            const Vector3 forward	= GetTransform()->GetForward();
            const Vector3 up		= GetTransform()->GetUp();

			// Compute
			m_matrix_view[0] = Matrix::CreateLookAtLH(position, position + forward, up);
		}
		else if (m_light_type == LightType_Point)
		{
            const Vector3 position = GetTransform()->GetPosition();

			// Compute view for each side of the cube map
			m_matrix_view[0] = Matrix::CreateLookAtLH(position, position + Vector3::Right,		Vector3::Up);		// x+
			m_matrix_view[1] = Matrix::CreateLookAtLH(position, position + Vector3::Left,		Vector3::Up);		// x-
			m_matrix_view[2] = Matrix::CreateLookAtLH(position, position + Vector3::Up,		    Vector3::Backward);	// y+
			m_matrix_view[3] = Matrix::CreateLookAtLH(position, position + Vector3::Down,		Vector3::Forward);	// y-
			m_matrix_view[4] = Matrix::CreateLookAtLH(position, position + Vector3::Forward,	Vector3::Up);		// z+
			m_matrix_view[5] = Matrix::CreateLookAtLH(position, position + Vector3::Backward,	Vector3::Up);		// z-
		}
	}

	bool Light::ComputeProjectionMatrix(uint32_t index /*= 0*/)
	{
		if (index >= m_shadow_map->GetArraySize())
        {
            LOG_ERROR_INVALID_PARAMETER();
            return false;
        }

        const bool reverse_z = m_renderer ? m_renderer->GetReverseZ() : false;

		if (m_light_type == LightType_Directional)
		{
            Cascade& cascade            = m_cascades[index];
            const float cascade_depth   = (cascade.max.z - cascade.min.z) * 10.0f;
            const float min_z           = reverse_z ? cascade_depth : 0.0f;
            const float max_z           = reverse_z ? 0.0f : cascade_depth;
            m_matrix_projection[index]  = Matrix::CreateOrthoOffCenterLH(cascade.min.x, cascade.max.x, cascade.min.y, cascade.max.y, min_z, max_z);
            cascade.frustum             = Frustum(m_matrix_view[index], m_matrix_projection[index], max_z);
		}
		else
		{
			const auto width			= static_cast<float>(m_shadow_map->GetWidth());
			const auto height			= static_cast<float>(m_shadow_map->GetHeight());		
			const auto aspect_ratio		= width / height;
			const float fov				= (m_light_type == LightType_Spot) ? m_angle_rad : 1.57079633f; // 1.57079633 = 90 deg
			const float near_plane		= reverse_z ? m_range : 0.0f;
			const float far_plane		= reverse_z ? 0.0f : m_range;
			m_matrix_projection[index]	= Matrix::CreatePerspectiveFieldOfViewLH(fov, aspect_ratio, near_plane, far_plane);
		}

		return true;
	}

    const Matrix& Light::GetViewMatrix(uint32_t index /*= 0*/)
    {
        if (index >= static_cast<uint32_t>(m_matrix_view.size()))
        {
            LOG_ERROR_INVALID_PARAMETER();
            return Matrix::Identity;
        }

        return m_matrix_view[index];
    }

    const Matrix& Light::GetProjectionMatrix(uint32_t index /*= 0*/)
    {
        if (index >= static_cast<uint32_t>(m_matrix_projection.size()))
        {
            LOG_ERROR_INVALID_PARAMETER();
            return Matrix::Identity;
        }

        return m_matrix_projection[index];
    }

    void Light::ComputeCascadeSplits()
    {
        // Can happen during the first frame, don't log error
        if (!m_renderer->GetCamera())
            return;

        Camera* camera                        = m_renderer->GetCamera().get();
        const float clip_near                 = camera->GetNearPlane();
        const float clip_far                  = camera->GetFarPlane();
        const Matrix view_projection_inverted = Matrix::Invert(camera->GetViewMatrix() * camera->ComputeProjection(true));

        // Calculate split depths based on view camera frustum
        // Based on method presented in https://developer.nvidia.com/gpugems/GPUGems3/gpugems3_ch10.html
        const float split_lambda  = 0.99f;
        const float clip_range    = clip_far - clip_near;
        const float min_z         = clip_near;
        const float max_z         = clip_near + clip_range;
        const float range         = max_z - min_z;
        const float ratio         = max_z / min_z;    
        float splits[g_cascade_count];
        for (uint32_t i = 0; i < g_cascade_count; i++)
        {
            const float p           = (i + 1) / static_cast<float>(g_cascade_count);
            const float log         = min_z * Math::Pow(ratio, p);
            const float uniform     = min_z + range * p;
            const float d           = split_lambda * (log - uniform) + uniform;
            splits[i]               = (d - clip_near) / clip_range;
        }

        for (uint32_t i = 0; i < g_cascade_count; i++)
        {
            // Define camera frustum corners in clip space
            Vector3 frustum_corners[8] =
            {
                Vector3(-1.0f,  1.0f, -1.0f),
                Vector3( 1.0f,  1.0f, -1.0f),
                Vector3( 1.0f, -1.0f, -1.0f),
                Vector3(-1.0f, -1.0f, -1.0f),
                Vector3(-1.0f,  1.0f,  1.0f),
                Vector3( 1.0f,  1.0f,  1.0f),
                Vector3( 1.0f, -1.0f,  1.0f),
                Vector3(-1.0f, -1.0f,  1.0f)
            };

            // Project frustum corners into world space
            for (Vector3& frustum_corner : frustum_corners)
            {
                Vector4 inverted_corner = Vector4(frustum_corner, 1.0f) * view_projection_inverted;
                frustum_corner = inverted_corner / inverted_corner.w;
            }

            // Compute split distance
            {
                // Reset split distance every time we restart
                static float split_distance_previous;
                if (i == 0) split_distance_previous = 0.0f;

                const float split_distance = splits[i];
                for (uint32_t i = 0; i < 4; i++)
                {
                    Vector3 distance        = frustum_corners[i + 4] - frustum_corners[i];
                    frustum_corners[i + 4]  = frustum_corners[i] + (distance * split_distance);
                    frustum_corners[i]      = frustum_corners[i] + (distance * split_distance_previous);
                }
                split_distance_previous = splits[i];
            }

            // Compute frustum bounds
            {
                // Compute bounding sphere which encloses the frustum.
                // Since a sphere is rotational invariant it will keep the size of the orthographic
                // projection frustum same independent of eye view direction, hence eliminating shimmering.

                Cascade& cascade = m_cascades[i];

                // Compute center
                cascade.center = Vector3::Zero;
                for (const Vector3& frustum_corner : frustum_corners)
                {
                    cascade.center += Vector3(frustum_corner);
                }
                cascade.center /= 8.0f;

                // Compute radius
                float radius = 0.0f;
                for (const Vector3& frustum_corner : frustum_corners)
                {
                    const float distance = Vector3::Distance(frustum_corner, cascade.center);
                    radius = Max(radius, distance);
                }
                radius = Ceil(radius * 16.0f) / 16.0f;

                // Compute min and max
                cascade.max = radius;
                cascade.min = -radius;
            }
        }
    }

    void Light::CreateShadowMap(bool force)
	{		
		if (!m_cast_shadows)
			return;

        if (m_shadow_map && !force)
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

    bool Light::IsInViewFrustrum(Renderable* renderable, uint32_t index) const
    {
        const auto box          = renderable->GetAabb();
        const auto center       = box.GetCenter();
        const auto extents      = box.GetExtents();

        // ensure that potential shadow casters from behind the near plane are not rejected
        bool ignore_near_plane = true; 

        return m_cascades[index].frustum.IsVisible(center, extents, ignore_near_plane);
    }
}  
