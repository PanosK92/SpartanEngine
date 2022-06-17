/*
Copyright(c) 2016-2022 Panos Karabelas

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
#include "Runtime/Core/Spartan.h"
#include "Light.h"
#include "Transform.h"
#include "Camera.h"
#include "Renderable.h"
#include "../World.h"
#include "../../IO/FileStream.h"
#include "../../Rendering/Renderer.h"
#include "../../RHI/RHI_Texture2D.h"
#include "../../RHI/RHI_TextureCube.h"
#include "../../RHI/RHI_Texture2DArray.h"
//=======================================

//= NAMESPACES ===============
using namespace Spartan::Math;
using namespace std;
//============================

namespace Spartan
{
    Light::Light(Context* context, Entity* entity, uint64_t id /*= 0*/) : IComponent(context, entity, id)
    {
        SP_REGISTER_ATTRIBUTE_VALUE_VALUE(m_shadows_enabled, bool);
        SP_REGISTER_ATTRIBUTE_VALUE_VALUE(m_shadows_screen_space_enabled, bool);
        SP_REGISTER_ATTRIBUTE_VALUE_VALUE(m_shadows_transparent_enabled, bool);
        SP_REGISTER_ATTRIBUTE_VALUE_VALUE(m_range, float);
        SP_REGISTER_ATTRIBUTE_VALUE_VALUE(m_intensity, float);
        SP_REGISTER_ATTRIBUTE_VALUE_VALUE(m_angle_rad, float);
        SP_REGISTER_ATTRIBUTE_VALUE_VALUE(m_color_rgb, Vector4);
        SP_REGISTER_ATTRIBUTE_VALUE_VALUE(m_bias, float);
        SP_REGISTER_ATTRIBUTE_VALUE_VALUE(m_normal_bias, float);
        SP_REGISTER_ATTRIBUTE_GET_SET(GetLightType, SetLightType, LightType);

        m_renderer = m_context->GetSubsystem<Renderer>();
    }

    void Light::OnInitialize()
    {

    }

    void Light::OnStart()
    {

    }

    void Light::OnTick(double delta_time)
    {
        SP_ASSERT(m_renderer != nullptr);

        // During engine startup, keep checking until the rhi device gets
        // created so we can create potentially required shadow maps
        if (!m_initialized)
        {
            CreateShadowMap();
            m_initialized = true;
        }

        // Dirty checks
        {
            // Position, rotation and reverse-z
            const bool reverse_z = m_renderer ? m_renderer->GetOption(Renderer::Option::ReverseZ) : false;
            if (m_transform->HasPositionChangedThisFrame() || m_transform->HasRotationChangedThisFrame() || m_previous_reverse_z != reverse_z)
            {
                m_previous_reverse_z = reverse_z;
                m_is_dirty           = true;
            }

            // Camera (needed for directional light cascade computations)
            if (m_light_type == LightType::Directional)
            {
                if (shared_ptr<Camera> camera = m_renderer->GetCamera())
                {
                    if (m_previous_camera_view != camera->GetViewMatrix())
                    {
                        m_previous_camera_view = camera->GetViewMatrix();
                        m_is_dirty = true;
                    }
                }
            }
        }

        if (!m_is_dirty)
            return;

        // Update position based on direction (for directional light)
        if (m_light_type == LightType::Directional)
        {
            float distance = m_renderer->GetCamera() ? m_renderer->GetCamera()->GetFarPlane() : 1000.0f;
            m_transform->SetPosition(-m_transform->GetForward() * distance);
        }

        // Update shadow map(s)
        if (m_shadows_enabled)
        {
            if (m_light_type == LightType::Directional)
            {
                ComputeCascadeSplits();
            }

            ComputeViewMatrix();

            // Compute projection matrix
            if (m_shadow_map.texture_depth)
            {
                for (uint32_t i = 0; i < m_shadow_map.texture_depth->GetArrayLength(); i++)
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
        stream->Write(m_shadows_transparent_enabled);
        stream->Write(m_volumetric_enabled);
        stream->Write(m_color_rgb);
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
        stream->Read(&m_shadows_transparent_enabled);
        stream->Read(&m_volumetric_enabled);
        stream->Read(&m_color_rgb);
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

        m_light_type = type;
        m_is_dirty   = true;

        if (m_shadows_enabled)
        {
            CreateShadowMap();
        }

        m_context->GetSubsystem<World>()->Resolve();
    }

    void Light::SetColor(const float temperature)
    {

    }

    void Light::SetShadowsEnabled(bool cast_shadows)
    {
        if (m_shadows_enabled == cast_shadows)
            return;

        m_shadows_enabled = cast_shadows;
        m_is_dirty        = true;

        CreateShadowMap();
    }

    void Light::SetShadowsTransparentEnabled(bool cast_transparent_shadows)
    {
        if (m_shadows_transparent_enabled == cast_transparent_shadows)
            return;

        m_shadows_transparent_enabled = cast_transparent_shadows;
        m_is_dirty                    = true;

        CreateShadowMap();
    }

    void Light::SetRange(float range)
    {
        m_range = Helper::Clamp(range, 0.0f, std::numeric_limits<float>::max());
        m_is_dirty = true;
    }

    void Light::SetAngle(float angle)
    {
        m_angle_rad = Helper::Clamp(angle, 0.0f, Math::Helper::PI_2);
        m_is_dirty  = true;
    }

    void Light::SetTimeOfDay(float time_of_day)
    {
        m_time_of_day = Helper::Clamp(time_of_day, 0.0f, 24.0f);
    }

    void Light::ComputeViewMatrix()
    {
        if (m_light_type == LightType::Directional)
        {
            if (!m_shadow_map.slices.empty())
            { 
                for (uint32_t i = 0; i < m_cascade_count; i++)
                {
                    ShadowSlice& shadow_map = m_shadow_map.slices[i];
                    Vector3 position        = shadow_map.center - m_transform->GetForward() * shadow_map.max.z;
                    Vector3 target          = shadow_map.center;
                    Vector3 up              = Vector3::Up;
                    m_matrix_view[i]        = Matrix::CreateLookAtLH(position, target, up);
                }
            }
        }
        else if (m_light_type == LightType::Spot)
        {   
            const Vector3 position = m_transform->GetPosition();
            const Vector3 forward  = m_transform->GetForward();
            const Vector3 up       = m_transform->GetUp();

            // Compute
            m_matrix_view[0] = Matrix::CreateLookAtLH(position, position + forward, up);
        }
        else if (m_light_type == LightType::Point)
        {
            const Vector3 position = m_transform->GetPosition();

            // Compute view for each side of the cube map
            m_matrix_view[0] = Matrix::CreateLookAtLH(position, position + Vector3::Right,      Vector3::Up);       // x+
            m_matrix_view[1] = Matrix::CreateLookAtLH(position, position + Vector3::Left,       Vector3::Up);       // x-
            m_matrix_view[2] = Matrix::CreateLookAtLH(position, position + Vector3::Up,         Vector3::Backward); // y+
            m_matrix_view[3] = Matrix::CreateLookAtLH(position, position + Vector3::Down,       Vector3::Forward);  // y-
            m_matrix_view[4] = Matrix::CreateLookAtLH(position, position + Vector3::Forward,    Vector3::Up);       // z+
            m_matrix_view[5] = Matrix::CreateLookAtLH(position, position + Vector3::Backward,   Vector3::Up);       // z-
        }
    }

    bool Light::ComputeProjectionMatrix(uint32_t index /*= 0*/)
    {
        SP_ASSERT(index < m_shadow_map.texture_depth->GetArrayLength());

        ShadowSlice& shadow_slice = m_shadow_map.slices[index];
        const bool reverse_z      = m_renderer ? m_renderer->GetOption(Renderer::Option::ReverseZ) : false;

        if (m_light_type == LightType::Directional)
        {
            const float cascade_depth   = (shadow_slice.max.z - shadow_slice.min.z) * 10.0f;
            const float min_z           = reverse_z ? cascade_depth : 0.0f;
            const float max_z           = reverse_z ? 0.0f : cascade_depth;
            m_matrix_projection[index]  = Matrix::CreateOrthoOffCenterLH(shadow_slice.min.x, shadow_slice.max.x, shadow_slice.min.y, shadow_slice.max.y, min_z, max_z);
            shadow_slice.frustum        = Frustum(m_matrix_view[index], m_matrix_projection[index], max_z);
        }
        else
        {
            const uint32_t width       = m_shadow_map.texture_depth->GetWidth();
            const uint32_t height      = m_shadow_map.texture_depth->GetHeight();
            const float aspect_ratio   = static_cast<float>(width) / static_cast<float>(height);
            const float fov            = m_light_type == LightType::Spot ? m_angle_rad * 2.0f : Math::Helper::PI_DIV_2;
            const float near_plane     = reverse_z ? m_range : 0.3f;
            const float far_plane      = reverse_z ? 0.3f : m_range;
            m_matrix_projection[index] = Matrix::CreatePerspectiveFieldOfViewLH(fov, aspect_ratio, near_plane, far_plane);
            shadow_slice.frustum       = Frustum(m_matrix_view[index], m_matrix_projection[0], far_plane);
        }

        return true;
    }

    const Matrix& Light::GetViewMatrix(uint32_t index /*= 0*/) const
    {
        SP_ASSERT(index < static_cast<uint32_t>(m_matrix_view.size()));

        return m_matrix_view[index];
    }

    const Matrix& Light::GetProjectionMatrix(uint32_t index /*= 0*/) const
    {
        SP_ASSERT(index < static_cast<uint32_t>(m_matrix_projection.size()));

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
        const Matrix view_projection_inverted = Matrix::Invert(camera->GetViewMatrix() * camera->ComputeProjection(false, clip_near, clip_far));

        // Calculate split depths based on view camera frustum
        // Based on method presented in https://developer.nvidia.com/gpugems/GPUGems3/gpugems3_ch10.html
        const float split_lambda  = 0.98f;
        const float clip_range    = clip_far - clip_near;
        const float min_z         = clip_near;
        const float max_z         = clip_near + clip_range;
        const float range         = max_z - min_z;
        const float ratio         = max_z / min_z;
        vector<float> splits(m_cascade_count);
        for (uint32_t i = 0; i < m_cascade_count; i++)
        {
            const float p           = (i + 1) / static_cast<float>(m_cascade_count);
            const float log         = min_z * Math::Helper::Pow(ratio, p);
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
                    radius = Helper::Max(radius, distance);
                }
                radius = Helper::Ceil(radius * 16.0f) / 16.0f;

                // Compute min and max
                shadow_slice.max = radius;
                shadow_slice.min = -radius;
            }
        }
    }

    uint32_t Light::GetShadowArraySize() const
    {
        return m_shadow_map.texture_depth ? m_shadow_map.texture_depth->GetArrayLength() : 0;
    }

    void Light::CreateShadowMap()
    {
        // Early exit if there is no change in shadow map resolution
        const uint32_t resolution     = m_renderer->GetOptionValue<uint32_t>(Renderer::OptionValue::ShadowResolution);
        const bool resolution_changed = m_shadow_map.texture_depth ? (resolution != m_shadow_map.texture_depth->GetWidth()) : false;
        if ((!m_is_dirty && !resolution_changed))
            return;

        // Early exit if this light casts no shadows
        if (!m_shadows_enabled)
        {
            m_shadow_map.texture_depth.reset();
            return;
        }

        if (!m_shadows_transparent_enabled)
        {
            m_shadow_map.texture_color.reset();
        }

        if (GetLightType() == LightType::Directional)
        {
            m_shadow_map.texture_depth = make_unique<RHI_Texture2DArray>(m_context, resolution, resolution, RHI_Format_D32_Float, m_cascade_count, RHI_Texture_Rt_DepthStencil | RHI_Texture_Srv, "shadow_map_directional");

            if (m_shadows_transparent_enabled)
            {
                m_shadow_map.texture_color = make_unique<RHI_Texture2DArray>(m_context, resolution, resolution, RHI_Format_R8G8B8A8_Unorm, m_cascade_count, RHI_Texture_Rt_Color | RHI_Texture_Srv, "shadow_map_directional_color");
            }

            m_shadow_map.slices = vector<ShadowSlice>(m_cascade_count);
        }
        else if (GetLightType() == LightType::Point)
        {
            m_shadow_map.texture_depth = make_unique<RHI_TextureCube>(m_context, resolution, resolution, RHI_Format_D32_Float, RHI_Texture_Rt_DepthStencil | RHI_Texture_Srv, "shadow_map_point_color");

            if (m_shadows_transparent_enabled)
            {
                m_shadow_map.texture_color = make_unique<RHI_TextureCube>(m_context, resolution, resolution, RHI_Format_R8G8B8A8_Unorm, RHI_Texture_Rt_Color | RHI_Texture_Srv, "shadow_map_point_color");
            }

            m_shadow_map.slices = vector<ShadowSlice>(6);
        }
        else if (GetLightType() == LightType::Spot)
        {
            m_shadow_map.texture_depth  = make_unique<RHI_Texture2D>(m_context, resolution, resolution, 1, RHI_Format_D32_Float, RHI_Texture_Rt_DepthStencil | RHI_Texture_Srv, "shadow_map_spot_color");

            if (m_shadows_transparent_enabled)
            {
                m_shadow_map.texture_color = make_unique<RHI_Texture2D>(m_context, resolution, resolution, 1, RHI_Format_R8G8B8A8_Unorm, RHI_Texture_Rt_Color | RHI_Texture_Srv, "shadow_map_spot_color");
            }

            m_shadow_map.slices = vector<ShadowSlice>(1);
        }
    }

    bool Light::IsInViewFrustum(Renderable* renderable, uint32_t index) const
    {
        const auto box     = renderable->GetAabb();
        const auto center  = box.GetCenter();
        const auto extents = box.GetExtents();

        // ensure that potential shadow casters from behind the near plane are not rejected
        const bool ignore_near_plane = (m_light_type == LightType::Directional) ? true : false;

        return m_shadow_map.slices[index].frustum.IsVisible(center, extents, ignore_near_plane);
    }
}  
