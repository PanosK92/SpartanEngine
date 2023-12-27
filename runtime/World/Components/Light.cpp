/*
Copyright(c) 2016-2023 Panos Karabelas

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
#include "pch.h"
#include "Light.h"
#include "Camera.h"
#include "../World.h"
#include "../../IO/FileStream.h"
#include "../../Rendering/Renderer.h"
#include "../../RHI/RHI_Texture2D.h"
#include "../../RHI/RHI_TextureCube.h"
#include "../../RHI/RHI_Texture2DArray.h"
#include "../Entity.h"
//=======================================

//= NAMESPACES ===============
using namespace Spartan::Math;
using namespace std;
//============================

namespace Spartan
{
    namespace
    {
        float orthographic_depth  = 1024.0; // depth of all cascades
        float orthographic_extent = 20.0f;  // size of the near cascade
        float far_cascade_scale   = 5.0f;   // size of the far cascade compared to the near one
    }

    Light::Light(weak_ptr<Entity> entity) : Component(entity)
    {
        SP_REGISTER_ATTRIBUTE_VALUE_VALUE(m_shadows_enabled, bool);
        SP_REGISTER_ATTRIBUTE_VALUE_VALUE(m_shadows_transparent_enabled, bool);
        SP_REGISTER_ATTRIBUTE_VALUE_VALUE(m_range, float);
        SP_REGISTER_ATTRIBUTE_VALUE_VALUE(m_intensity_lumens, float);
        SP_REGISTER_ATTRIBUTE_VALUE_VALUE(m_angle_rad, float);
        SP_REGISTER_ATTRIBUTE_VALUE_VALUE(m_color_rgb, Color);
        SP_REGISTER_ATTRIBUTE_VALUE_VALUE(m_bias_normal, float);
        SP_REGISTER_ATTRIBUTE_GET_SET(GetLightType, SetLightType, LightType);

        if (m_light_type == LightType::Directional)
        {
            SetIntensity(LightIntensity::sky_sunlight_noon);
            m_range = orthographic_depth;
        }
        else if (m_light_type == LightType::Point)
        {
            SetIntensity(LightIntensity::bulb_150_watt);
            m_range = 10.0f;
        }
        else if (m_light_type == LightType::Spot)
        {
            SetIntensity(LightIntensity::bulb_flashlight);
            m_range = 10.0f;
        }
    }

    void Light::OnInitialize()
    {
        Component::OnInitialize();
    }

    void Light::OnTick()
    {
        // during engine startup, keep checking until the rhi device gets
        // created so we can create potentially required shadow maps
        if (!m_initialized)
        {
            CreateShadowMap();
            m_initialized = true;
        }

        // dirty checks
        {
            // Position, rotation
            if (m_entity_ptr->HasPositionChangedThisFrame() || m_entity_ptr->HasRotationChangedThisFrame())
            {
                m_is_dirty = true;
            }

            // camera (needed for directional light cascade computations)
            if (m_light_type == LightType::Directional)
            {
                if (shared_ptr<Camera> camera = Renderer::GetCamera())
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

        // update matrices
        if (m_shadows_enabled)
        {
            ComputeViewMatrix();
            ComputeProjectionMatrix();
        }

        m_is_dirty = false;
        SP_FIRE_EVENT(EventType::LightOnChanged);
    }

    void Light::Serialize(FileStream* stream)
    {
        stream->Write(static_cast<uint32_t>(m_light_type));
        stream->Write(m_shadows_enabled);
        stream->Write(m_shadows_transparent_enabled);
        stream->Write(m_volumetric_enabled);
        stream->Write(m_color_rgb);
        stream->Write(m_range);
        stream->Write(m_intensity_lumens);
        stream->Write(m_angle_rad);
        stream->Write(m_bias_normal);
    }

    void Light::Deserialize(FileStream* stream)
    {
        SetLightType(static_cast<LightType>(stream->ReadAs<uint32_t>()));
        stream->Read(&m_shadows_enabled);
        stream->Read(&m_shadows_transparent_enabled);
        stream->Read(&m_volumetric_enabled);
        stream->Read(&m_color_rgb);
        stream->Read(&m_range);
        stream->Read(&m_intensity_lumens);
        stream->Read(&m_angle_rad);
        stream->Read(&m_bias_normal);
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

        World::Resolve();
    }

    void Light::SetTemperature(const float temperature_kelvin)
    {
        m_temperature_kelvin = temperature_kelvin;
        m_color_rgb          = Color(temperature_kelvin);

        SP_FIRE_EVENT(EventType::LightOnChanged);
    }

    void Light::SetColor(const Color& rgb)
    {
        m_color_rgb = rgb;

        if (rgb == Color::light_sky_clear)
            m_temperature_kelvin = 15000.0f;
        else if (rgb == Color::light_sky_daylight_overcast)
            m_temperature_kelvin = 6500.0f;
        else if (rgb == Color::light_sky_moonlight)
            m_temperature_kelvin = 4000.0f;
        else if (rgb == Color::light_sky_sunrise)
            m_temperature_kelvin = 2000.0f;
        else if (rgb == Color::light_candle_flame)
            m_temperature_kelvin = 1850.0f;
        else if (rgb == Color::light_direct_sunlight)
            m_temperature_kelvin = 5778.0f;
        else if (rgb == Color::light_digital_display)
            m_temperature_kelvin = 6500.0f;
        else if (rgb == Color::light_fluorescent_tube_light)
            m_temperature_kelvin = 5000.0f;
        else if (rgb == Color::light_kerosene_lamp)
            m_temperature_kelvin = 1850.0f;
        else if (rgb == Color::light_light_bulb)
            m_temperature_kelvin = 2700.0f;
        else if (rgb == Color::light_photo_flash)
            m_temperature_kelvin = 5500.0f;

        SP_FIRE_EVENT(EventType::LightOnChanged);
    }

    void Light::SetIntensity(const LightIntensity intensity)
    {
        m_intensity = intensity;

        if (intensity == LightIntensity::sky_sunlight_noon)
        {
            m_intensity_lumens = 120000.0f;
        }
        else if (intensity == LightIntensity::sky_sunlight_morning_evening)
        {
            m_intensity_lumens = 60000.0f;
        }
        else if (intensity == LightIntensity::sky_overcast_day)
        {
            m_intensity_lumens = 20000.0f;
        }
        else if (intensity == LightIntensity::sky_twilight)
        {
            m_intensity_lumens = 10000.0f;
        }
        else if (intensity == LightIntensity::bulb_stadium)
        {
            m_intensity_lumens = 200000.0f;
        }
        else if (intensity == LightIntensity::bulb_500_watt)
        {
            m_intensity_lumens = 8500.0f;
        }
        else if (intensity == LightIntensity::bulb_150_watt)
        {
            m_intensity_lumens = 2600.0f;
        }
        else if (intensity == LightIntensity::bulb_100_watt)
        {
            m_intensity_lumens = 1600.0f;
        }
        else if (intensity == LightIntensity::bulb_60_watt)
        {
            m_intensity_lumens = 800.0f;
        }
        else if (intensity == LightIntensity::bulb_25_watt)
        {
            m_intensity_lumens = 200.0f;
        }
        else if (intensity == LightIntensity::bulb_flashlight)
        {
            m_intensity_lumens = 100.0f;
        }
        else // black hole
        {
            m_intensity_lumens = 0.0f;
        }

        SP_FIRE_EVENT(EventType::LightOnChanged);
    }

    void Light::SetIntensityLumens(const float lumens)
    {
        m_intensity_lumens = lumens;
        m_intensity        = LightIntensity::custom;

        SP_FIRE_EVENT(EventType::LightOnChanged);
    }

    float Light::GetIntensityWatt(Camera* camera) const
    {
        SP_ASSERT(camera != nullptr);

        // This magic values are chosen empirically based on how the lights
        // types in the LightIntensity enum should look in the engine
        const float magic_value_a = 150.0f;
        const float magic_value_b = 0.025f;

        // convert lumens to power (in watts) assuming all light is at 555nm
        float power_watts = (m_intensity_lumens / 683.0f) * magic_value_a;

        // for point lights and spot lights, intensity should fall off with the square of the distance,
        // so we don't need to modify the power. For directional lights, intensity should be constant,
        // so we need to multiply the power by the area over which the light is spread
        if (m_light_type == LightType::Directional)
        {
            float area  = magic_value_b;
            power_watts *= area;
        }

        // watts can be multiplied by the camera's exposure to get the final intensity
        return power_watts * camera->GetExposure();
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
        m_range    = Helper::Clamp(range, 0.0f, std::numeric_limits<float>::max());
        m_is_dirty = true;
    }

    void Light::SetAngle(float angle)
    {
        m_angle_rad = Helper::Clamp(angle, 0.0f, Math::Helper::PI_2);
        m_is_dirty  = true;
    }

    void Light::ComputeViewMatrix()
    {
        const Vector3 position = GetEntity()->GetPosition();
        const Vector3 forward  = GetEntity()->GetForward();

        if (m_light_type == LightType::Directional)
        {
            if (Camera* camera = Renderer::GetCamera().get())
            {
                Vector3 target = camera->GetEntity()->GetPosition();

                // near cascade
                Vector3 position = target - forward * m_range * 0.5f; // center on camera
                m_matrix_view[0] = Matrix::CreateLookAtLH(position, target, Vector3::Up);

                // far cascade
                position         = target - forward * (m_range * far_cascade_scale) * 0.5f;
                m_matrix_view[1] = Matrix::CreateLookAtLH(position, target, Vector3::Up);
            }
        }
        else if (m_light_type == LightType::Spot)
        {
            m_matrix_view[0] = Matrix::CreateLookAtLH(position, position + forward, Vector3::Up);
        }
        else if (m_light_type == LightType::Point)
        {
            m_matrix_view[0] = Matrix::CreateLookAtLH(position, position + Vector3::Right,    Vector3::Up);
            m_matrix_view[1] = Matrix::CreateLookAtLH(position, position + Vector3::Left,     Vector3::Up);
            m_matrix_view[2] = Matrix::CreateLookAtLH(position, position + Vector3::Up,       Vector3::Backward);
            m_matrix_view[3] = Matrix::CreateLookAtLH(position, position + Vector3::Down,     Vector3::Forward);
            m_matrix_view[4] = Matrix::CreateLookAtLH(position, position + Vector3::Forward,  Vector3::Up);
            m_matrix_view[5] = Matrix::CreateLookAtLH(position, position + Vector3::Backward, Vector3::Up);
        }
    }

    void Light::ComputeProjectionMatrix()
    {
        if (!m_texture_depth)
            return;

        if (m_light_type == LightType::Directional)
        {
            for (uint32_t i = 0; i < 2; i++)
            { 
                // determine the orthographic extent based on the cascade index
                float cascade_extent_multiplier = (i == 0) ? 1.0f : far_cascade_scale;
                float extent                    = orthographic_extent * cascade_extent_multiplier;

                // orthographic bounds
                float left       = -extent;
                float right      = extent;
                float bottom     = -extent;
                float top        = extent;
                float near_plane = 0.0f;
                float far_plane  = m_range * cascade_extent_multiplier;

                // snap the orthographic bounds to the nearest texel (to avoid shimmering)
                //float world_units_per_texel = (2.0f * orthographic_extent) / static_cast<float>(m_texture_depth->GetWidth());
                //left                        = floor(left / world_units_per_texel) * world_units_per_texel;
                //right                       = floor(right / world_units_per_texel) * world_units_per_texel;
                //bottom                      = floor(bottom / world_units_per_texel) * world_units_per_texel;
                //top                         = floor(top / world_units_per_texel) * world_units_per_texel;

                m_matrix_projection[i] = Matrix::CreateOrthoOffCenterLH(left, right, bottom, top, far_plane, near_plane);
                m_frustums[i]          = Frustum(m_matrix_view[i], m_matrix_projection[i], far_plane - near_plane);
            }
        }
        else
        {
            const float aspect_ratio = static_cast<float>(m_texture_depth->GetWidth()) / static_cast<float>(m_texture_depth->GetHeight());
            const float fov          = m_light_type == LightType::Spot ? m_angle_rad * 2.0f : Math::Helper::PI_DIV_2;
            Matrix projection        = Matrix::CreatePerspectiveFieldOfViewLH(fov, aspect_ratio, m_range, 0.3f);

            for (uint32_t i = 0; i < m_texture_depth->GetArrayLength(); i++)
            {
                m_matrix_projection[i] = projection;
                m_frustums[i]          = Frustum(m_matrix_view[i], projection, m_range);
            }
        }
    }

    const Matrix& Light::GetViewMatrix(uint32_t index) const
    {
        return m_matrix_view[index];
    }

    const Spartan::Math::Matrix& Light::GetProjectionMatrix(uint32_t index) const
    {
        return m_matrix_projection[index];
    }

    bool Light::IsInViewFrustum(const BoundingBox& bounding_box, const uint32_t index) const
    {
        SP_ASSERT(bounding_box != BoundingBox::Undefined);

        const Vector3 center  = bounding_box.GetCenter();
        const Vector3 extents = bounding_box.GetExtents();

        // ensure that potential shadow casters from behind the near plane are not rejected
        const bool ignore_near_plane = (m_light_type == LightType::Directional) ? true : false;

        return m_frustums[index].IsVisible(center, extents, ignore_near_plane);
    }

    bool Light::IsInViewFrustum(Renderable* renderable, uint32_t index) const
    {
        return IsInViewFrustum(renderable->GetBoundingBox(BoundingBoxType::Transformed), index);
    }

    void Light::CreateShadowMap()
    {
        // early exit if there is no change in shadow map resolution
        const uint32_t resolution     = Renderer::GetOption<uint32_t>(Renderer_Option::ShadowResolution);
        const bool resolution_changed = m_texture_depth ? (resolution != m_texture_depth->GetWidth()) : false;
        if ((!m_is_dirty && !resolution_changed))
            return;

        // early exit if this light casts no shadows
        if (!m_shadows_enabled)
        {
            m_texture_depth.reset();
            return;
        }

        if (!m_shadows_transparent_enabled)
        {
            m_texture_color.reset();
        }

        RHI_Format format_depth = RHI_Format::D32_Float;
        RHI_Format format_color = RHI_Format::R8G8B8A8_Unorm;
        uint32_t flags          = RHI_Texture_Rtv | RHI_Texture_Srv;

        if (GetLightType() == LightType::Directional)
        {
            m_texture_depth = make_unique<RHI_Texture2DArray>(resolution, resolution, format_depth, 2, flags, "light_directional_depth");

            if (m_shadows_transparent_enabled)
            {
                m_texture_color = make_unique<RHI_Texture2DArray>(resolution, resolution, format_color, 2, flags, "light_directional_color");
            }          
        }
        else if (GetLightType() == LightType::Spot)
        {
            m_texture_depth = make_unique<RHI_Texture2D>(resolution, resolution, 1, format_depth, flags, "light_spot_depth");

            if (m_shadows_transparent_enabled)
            {
                m_texture_color = make_unique<RHI_Texture2D>(resolution, resolution, 1, format_color, flags, "light_spot_color");
            }
        }
        else if (GetLightType() == LightType::Point)
        {
            m_texture_depth = make_unique<RHI_TextureCube>(resolution, resolution, format_depth, flags, "light_point_depth");

            if (m_shadows_transparent_enabled)
            {
                m_texture_color = make_unique<RHI_TextureCube>(resolution, resolution, format_color, flags, "light_point_color");
            }
        }
    }
}  
