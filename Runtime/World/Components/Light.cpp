/*
Copyright(c) 2016-2020 Panos Karabelas

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
#include "../World.h"
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
		REGISTER_ATTRIBUTE_VALUE_VALUE(m_shadows_enabled, bool);
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
        
	}

	void Light::OnStart()
	{
		
	}

	void Light::OnTick(float delta_time)
	{
        // Used in many places, no point in continuing without it
        if (!m_renderer)
        {
            LOG_ERROR_INVALID_INTERNALS();
            return;
        }

        // During engine startup, keep checking until the rhi device gets
        // created so we can create potentially required shadow maps
        if (!m_initialized)
        {
            CreateShadowMap();
            m_initialized = true;
        }

		// Position and rotation dirty check
		if (m_previous_pos != GetTransform()->GetPosition() || m_previous_rot != GetTransform()->GetRotation())
		{
			m_previous_pos = GetTransform()->GetPosition();
			m_previous_rot = GetTransform()->GetRotation();

			m_is_dirty = true;
		}

		// Camera dirty check (needed for directional light cascade computations)
		if (m_light_type == LightType_Directional)
		{
			if (auto& camera = m_renderer->GetCamera())
			{
				if (m_previous_camera_view != camera->GetViewMatrix())
				{
                    m_previous_camera_view = camera->GetViewMatrix();
					m_is_dirty = true;
				}
			}
		}

		if (!m_is_dirty)
			return;

		// Update shadow map(s)
        if (m_shadows_enabled)
        {
            if (m_light_type == LightType_Directional)
            {
                ComputeCascadeSplits();
            }

            ComputeViewMatrix();

            if (m_shadow_map.texture)
            {
                for (uint32_t i = 0; i < m_shadow_map.texture->GetArraySize(); i++)
                {
                    ComputeProjectionMatrix(i);
                }
            }
        }

        m_is_dirty = false;
	}

	void Light::Serialize(FileStream* stream)
	{
		stream->Write(static_cast<uint32_t>(m_light_type));
		stream->Write(m_shadows_enabled);
        stream->Write(m_shadows_screen_space_enabled);
        stream->Write(m_volumetric_enabled);
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
		stream->Read(&m_shadows_enabled);
        stream->Read(&m_shadows_screen_space_enabled);
        stream->Read(&m_volumetric_enabled);
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

        if (m_shadows_enabled)
        {
            CreateShadowMap();
        }

        
        m_context->GetSubsystem<World>()->MakeDirty();
	}

	void Light::SetShadowsEnabled(bool cast_shadows)
	{
		if (m_shadows_enabled == cast_shadows)
			return;

        m_shadows_enabled  = cast_shadows;
        m_is_dirty      = true;

        if (m_shadows_enabled)
        {
            CreateShadowMap();
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
            if (!m_shadow_map.slices.empty())
            { 
                for (uint32_t i = 0; i < m_cascade_count; i++)
                {
                    ShadowSlice& shadow_map = m_shadow_map.slices[i];
                    Vector3 position        = shadow_map.center - GetDirection() * shadow_map.max.z;
                    Vector3 target          = shadow_map.center;
                    Vector3 up              = Vector3::Up;
                    m_matrix_view[i]        = Matrix::CreateLookAtLH(position, target, up);
                }
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
		if (index >= m_shadow_map.texture->GetArraySize())
        {
            LOG_ERROR_INVALID_PARAMETER();
            return false;
        }

        ShadowSlice& shadow_slice   = m_shadow_map.slices[index];
        const bool reverse_z        = m_renderer ? m_renderer->GetOptionValue(Render_ReverseZ) : false;

		if (m_light_type == LightType_Directional)
		{
            const float cascade_depth   = (shadow_slice.max.z - shadow_slice.min.z) * 10.0f;
            const float min_z           = reverse_z ? cascade_depth : 0.0f;
            const float max_z           = reverse_z ? 0.0f : cascade_depth;
            m_matrix_projection[index]  = Matrix::CreateOrthoOffCenterLH(shadow_slice.min.x, shadow_slice.max.x, shadow_slice.min.y, shadow_slice.max.y, min_z, max_z);
            shadow_slice.frustum        = Frustum(m_matrix_view[index], m_matrix_projection[index], max_z);
		}
		else
		{
			const float width			= static_cast<float>(m_shadow_map.texture->GetWidth());
			const float height			= static_cast<float>(m_shadow_map.texture->GetHeight());
			const auto aspect_ratio		= width / height;
			const float fov				= (m_light_type == LightType_Spot) ? m_angle_rad : 1.57079633f; // 1.57079633 = 90 deg
			const float near_plane		= reverse_z ? m_range : 0.1f;
			const float far_plane		= reverse_z ? 0.1f : m_range;
			m_matrix_projection[index]	= Matrix::CreatePerspectiveFieldOfViewLH(fov, aspect_ratio, near_plane, far_plane);
            shadow_slice.frustum        = Frustum(m_matrix_view[index], m_matrix_projection[index], far_plane);
		}

		return true;
	}

    const Matrix& Light::GetViewMatrix(uint32_t index /*= 0*/) const
    {
        if (index >= static_cast<uint32_t>(m_matrix_view.size()))
        {
            LOG_ERROR_INVALID_PARAMETER();
            return Matrix::Identity;
        }

        return m_matrix_view[index];
    }

    const Matrix& Light::GetProjectionMatrix(uint32_t index /*= 0*/) const
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
        if (m_shadow_map.slices.empty())
            return;

        // Can happen during the first frame, don't log error
        if (!m_renderer->GetCamera())
            return;

        Camera* camera                        = m_renderer->GetCamera().get();
        const float clip_near                 = camera->GetNearPlane();
        const float clip_far                  = camera->GetFarPlane();
        const Matrix view_projection_inverted = Matrix::Invert(camera->GetViewMatrix() * camera->ComputeProjection(false));

        // Calculate split depths based on view camera frustum
        // Based on method presented in https://developer.nvidia.com/gpugems/GPUGems3/gpugems3_ch10.html
        const float split_lambda  = 0.99f;
        const float clip_range    = clip_far - clip_near;
        const float min_z         = clip_near;
        const float max_z         = clip_near + clip_range;
        const float range         = max_z - min_z;
        const float ratio         = max_z / min_z;    
        vector<float> splits(m_cascade_count);
        for (uint32_t i = 0; i < m_cascade_count; i++)
        {
            const float p           = (i + 1) / static_cast<float>(m_cascade_count);
            const float log         = min_z * Math::Pow(ratio, p);
            const float uniform     = min_z + range * p;
            const float d           = split_lambda * (log - uniform) + uniform;
            splits[i]               = (d - clip_near) / clip_range;
        }

        for (uint32_t i = 0; i < m_cascade_count; i++)
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

                ShadowSlice& shadow_slice = m_shadow_map.slices[i];

                // Compute center
                shadow_slice.center = Vector3::Zero;
                for (const Vector3& frustum_corner : frustum_corners)
                {
                    shadow_slice.center += Vector3(frustum_corner);
                }
                shadow_slice.center /= 8.0f;

                // Compute radius
                float radius = 0.0f;
                for (const Vector3& frustum_corner : frustum_corners)
                {
                    const float distance = Vector3::Distance(frustum_corner, shadow_slice.center);
                    radius = Max(radius, distance);
                }
                radius = Ceil(radius * 16.0f) / 16.0f;

                // Compute min and max
                shadow_slice.max = radius;
                shadow_slice.min = -radius;
            }
        }
    }

    uint32_t Light::GetShadowArraySize() const
    {
        return m_shadow_map.texture ? m_shadow_map.texture->GetArraySize() : 0;
    }

    void Light::CreateShadowMap()
	{
        if (!m_renderer || !m_renderer->IsInitialized())
            return;

        const uint32_t resolution       = m_renderer->GetOptionValue<uint32_t>(Option_Value_ShadowResolution);
        const bool resolution_changed   = m_shadow_map.texture ? (resolution != m_shadow_map.texture->GetWidth()) : false;

        // Early exit if there was no change or the light doesn't cast any shadows
		if ((!m_is_dirty && !resolution_changed) || !m_shadows_enabled)
			return;

		if (GetLightType() == LightType_Directional)
		{
            m_shadow_map.texture    = make_unique<RHI_Texture2D>(m_context, resolution, resolution, RHI_Format_D32_Float, m_cascade_count);
            m_shadow_map.slices     = vector<ShadowSlice>(m_cascade_count);
		}
		else if (GetLightType() == LightType_Point)
		{
            m_shadow_map.texture    = make_unique<RHI_TextureCube>(m_context, resolution, resolution, RHI_Format_D32_Float);
            m_shadow_map.slices     = vector<ShadowSlice>(6);
		}
		else if (GetLightType() == LightType_Spot)
		{
            m_shadow_map.texture    = make_unique<RHI_Texture2D>(m_context, resolution, resolution, RHI_Format_D32_Float, 1);
            m_shadow_map.slices     = vector<ShadowSlice>(1);
		}
	}

    bool Light::IsInViewFrustrum(Renderable* renderable, uint32_t index) const
    {
        const auto box          = renderable->GetAabb();
        const auto center       = box.GetCenter();
        const auto extents      = box.GetExtents();

        // ensure that potential shadow casters from behind the near plane are not rejected
        bool ignore_near_plane = (m_light_type == LightType_Directional) ? true : false; 

        return m_shadow_map.slices[index].frustum.IsVisible(center, extents, ignore_near_plane);
    }
}  
