/*
Copyright(c) 2016-2024 Panos Karabelas

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
#include "../Entity.h"
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
    namespace
    {
        float orthographic_depth       = 4096; // depth of all cascades
        float orthographic_extent_near = 12.0f;
        float orthographic_extent_far  = 64.0f;

        float get_sensible_range(const float range, const LightType type)
        {
            if (type == LightType::Directional)
            {
                return orthographic_depth;
            }
            else if (type == LightType::Point)
            {
                return 15.0f;
            }
            else if (type == LightType::Spot)
            {
                return 15.0f;
            }

            return 0.0f;
        }

        LightIntensity get_sensible_intensity(const LightType type)
        {
            if (type == LightType::Directional)
            {
                return LightIntensity::sky_sunlight_noon;
            }
            else if (type == LightType::Point)
            {
                return LightIntensity::bulb_150_watt;
            }
            else if (type == LightType::Spot)
            {
                return LightIntensity::bulb_flashlight;
            }

            return LightIntensity::black_hole;
        }

        Color get_sensible_color(const LightType type)
        {
            if (type == LightType::Directional)
            {
                return Color::light_sky_sunrise;
            }
            else if (type == LightType::Point)
            {
                return Color::light_light_bulb;
            }
            else if (type == LightType::Spot)
            {
                return Color::light_light_bulb;
            }

            return Color::light_direct_sunlight;
        }
    }

    Light::Light(weak_ptr<Entity> entity) : Component(entity)
    {
        SP_REGISTER_ATTRIBUTE_VALUE_VALUE(m_flags, uint32_t);
        SP_REGISTER_ATTRIBUTE_VALUE_VALUE(m_range, float);
        SP_REGISTER_ATTRIBUTE_VALUE_VALUE(m_intensity_lumens, float);
        SP_REGISTER_ATTRIBUTE_VALUE_VALUE(m_angle_rad, float);
        SP_REGISTER_ATTRIBUTE_VALUE_VALUE(m_color_rgb, Color);
        SP_REGISTER_ATTRIBUTE_GET_SET(GetLightType, SetLightType, LightType);

        m_matrix_view.fill(Matrix::Identity);
        m_matrix_projection.fill(Matrix::Identity);

        SetColor(get_sensible_color(m_light_type));
        SetIntensity(get_sensible_intensity(m_light_type));
        SetRange(get_sensible_range(m_range, m_light_type));
        SetFlag(LightFlags::Shadows);
        SetFlag(LightFlags::ShadowsTransparent);
        SetFlag(LightFlags::ShadowsScreenSpace);
        if (m_light_type != LightType::Point)
        {
            SetFlag(LightFlags::Volumetric);
        }

        m_entity_ptr->SetRotation(Quaternion::FromEulerAngles(35.0f, 0.0f, 0.0f));
    }

    void Light::OnTick()
    {
        // if the light or the camera moves...
        bool update = GetEntity()->HasTransformChanged();
        if (shared_ptr<Camera> camera = Renderer::GetCamera())
        {
            if (camera->GetEntity()->HasTransformChanged())
            {
                update = true;
            }
        }

        // ... update the matrices
        if (update)
        {
            UpdateMatrices();
        }
    }

    void Light::Serialize(FileStream* stream)
    {
        stream->Write(static_cast<uint32_t>(m_light_type));
        stream->Write(m_flags);
        stream->Write(m_color_rgb);
        stream->Write(m_range);
        stream->Write(m_intensity_lumens);
        stream->Write(m_angle_rad);
    }

    void Light::Deserialize(FileStream* stream)
    {
        SetLightType(static_cast<LightType>(stream->ReadAs<uint32_t>()));
        stream->Read(&m_flags);
        stream->Read(&m_color_rgb);
        stream->Read(&m_range);
        stream->Read(&m_intensity_lumens);
        stream->Read(&m_angle_rad);
    }

    void Light::SetFlag(const LightFlags flag, const bool enable)
    {
        bool enabled      = false;
        bool disabled     = false;
        bool flag_present = m_flags & flag;

        if (enable && !flag_present)
        {
            m_flags |= static_cast<uint32_t>(flag);
            enabled  = true;

        }
        else if (!enable && flag_present)
        {
            m_flags  &= ~static_cast<uint32_t>(flag);
            disabled  = true;
        }

        if (enabled || disabled)
        {
            if (disabled)
            {
                // if the shadows have been disabled, disable properties which rely on them
                if (flag & LightFlags::Shadows)
                {
                    m_flags &= ~static_cast<uint32_t>(LightFlags::ShadowsScreenSpace);
                    m_flags &= ~static_cast<uint32_t>(LightFlags::ShadowsTransparent);
                    m_flags &= ~static_cast<uint32_t>(LightFlags::Volumetric);
                }
            }

            if (flag & LightFlags::Shadows || flag & LightFlags::ShadowsTransparent)
            {
                RefreshShadowMap();
            }

            SP_FIRE_EVENT(EventType::LightOnChanged);
        }
    }

    void Light::SetLightType(LightType type)
    {
        if (m_light_type == type)
            return;

        m_light_type = type;

        SetColor(get_sensible_color(m_light_type));
        SetRange(get_sensible_range(m_range, m_light_type));
        SetIntensity(get_sensible_intensity(m_light_type));

        if (IsFlagSet(Shadows) || IsFlagSet(ShadowsTransparent))
        {
            RefreshShadowMap();
        }

        UpdateMatrices();
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

    float Light::GetIntensityWatt() const
    {
        // this magic values are chosen empirically based on how the lights
        // types in the LightIntensity enum should look in the engine
        const float magic_value_a = 150.0f;
        const float magic_value_b = 0.015f;

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

        // multiply by the camera's exposure to get the final intensity
        Camera* camera  = Renderer::GetCamera().get();
        power_watts    *= camera ? camera->GetExposure() : 1.0f;

        return power_watts;
    }

    void Light::SetRange(float range)
    {
        range = Helper::Clamp(range, 0.0f, numeric_limits<float>::max());
        if (range == m_range)
            return;

        m_range = range;
        UpdateMatrices();
    }

    void Light::SetAngle(float angle)
    {
        angle = Helper::Clamp(angle, 0.0f, Math::Helper::PI_2);
        if (angle == m_angle_rad)
            return;

        m_angle_rad = angle;
        UpdateMatrices();
    }

    void Light::UpdateMatrices()
    {
        ComputeViewMatrix();
        ComputeProjectionMatrix();
        SP_FIRE_EVENT(EventType::LightOnChanged);
    }
    
    void Light::ComputeViewMatrix()
    {
        const Vector3 position = GetEntity()->GetPosition();
        const Vector3 forward  = GetEntity()->GetForward();

        if (m_light_type == LightType::Directional)
        {
            Vector3 target = Vector3::Zero;
            if (shared_ptr<Camera> camera = Renderer::GetCamera())
            {
                target = camera->GetEntity()->GetPosition();
            }
            Vector3 position = target - forward * orthographic_depth * 0.8f;

            m_matrix_view[0] = Matrix::CreateLookAtLH(position, target, Vector3::Up); // near
            m_matrix_view[1] = m_matrix_view[0];                                      // far
        }
        else if (m_light_type == LightType::Spot)
        {
            m_matrix_view[0] = Matrix::CreateLookAtLH(position, position + forward, Vector3::Up);
        }
        else if (m_light_type == LightType::Point)
        {
            m_matrix_view[0] = Matrix::CreateLookAtLH(position, position + forward, Vector3::Up); // front paraboloid
            m_matrix_view[1] = Matrix::CreateLookAtLH(position, position - forward, Vector3::Up); // back paraboloid
        }
    }

    void Light::ComputeProjectionMatrix()
    {
        if (!m_texture_depth)
            return;

        float near_plane = 0.01f;

        if (m_light_type == LightType::Directional)
        {
            for (uint32_t i = 0; i < 2; i++)
            {
                // determine the orthographic extent based on the cascade index
                float extent = (i == 0) ? orthographic_extent_near : orthographic_extent_far;

                // orthographic bounds
                float left       = -extent;
                float right      = extent;
                float bottom     = -extent;
                float top        = extent;
                float far_plane  = orthographic_depth;

                m_matrix_projection[i] = Matrix::CreateOrthoOffCenterLH(left, right, bottom, top, far_plane, near_plane);
                m_frustums[i] = Frustum(m_matrix_view[i], m_matrix_projection[i], far_plane - near_plane);
            }
        }
        else if (m_light_type == LightType::Spot)
        {
            const float aspect_ratio = static_cast<float>(m_texture_depth->GetWidth()) / static_cast<float>(m_texture_depth->GetHeight());
            const float fov          = m_angle_rad * 2.0f;
            Matrix projection = Matrix::CreatePerspectiveFieldOfViewLH(fov, aspect_ratio, m_range, near_plane);

            m_matrix_projection[0] = projection;
            m_frustums[0]          = Frustum(m_matrix_view[0], projection, m_range);
        }
    }

    bool Light::IsInViewFrustum(const BoundingBox& bounding_box, const uint32_t index) const
    {
        SP_ASSERT(bounding_box != BoundingBox::Undefined);

        if (m_light_type != LightType::Point)
        { 
            const Vector3 center    = bounding_box.GetCenter();
            const Vector3 extents   = bounding_box.GetExtents();
            const bool ignore_depth = m_light_type == LightType::Directional; // orthographic
            
            return m_frustums[index].IsVisible(center, extents, ignore_depth);
        }

        Vector3 to_box = bounding_box.GetCenter() - m_entity_ptr->GetPosition();
        float sign     = (index == 0) ? 1.0f : -1.0f;
        float bias     = 0.1f; // small bias to expand the paraboloid
        return Vector3::Dot(to_box, sign * m_entity_ptr->GetForward()) > -bias;
    }

    bool Light::IsInViewFrustum(Renderable* renderable, uint32_t index) const
    {
        BoundingBoxType type   = renderable->HasInstancing() ? BoundingBoxType::TransformedInstances : BoundingBoxType::Transformed;
        const BoundingBox& box = renderable->GetBoundingBox(type);

        if (box == BoundingBox::Undefined)
        {
            SP_LOG_WARNING("Undefined bounding box, treating as outside of the view frustum");
            return false;
        }

        return IsInViewFrustum(box, index);
    }

    void Light::RefreshShadowMap()
    {
        uint32_t resolution     = Renderer::GetOption<uint32_t>(Renderer_Option::ShadowResolution);
        RHI_Format format_depth = RHI_Format::D32_Float;
        RHI_Format format_color = RHI_Format::R8G8B8A8_Unorm;
        uint32_t flags          = RHI_Texture_Rtv | RHI_Texture_Srv | RHI_Texture_ClearBlit;
        m_texture_depth         = nullptr;
        m_texture_color         = nullptr;
        uint32_t array_length   = (GetLightType() == LightType::Spot) ? 1 : 2;

        // spot light:        1 slice
        // directional light: 2 slices for cascades
        // point light:       2 slices for front and back paraboloid

        m_texture_depth = make_unique<RHI_Texture2DArray>(resolution, resolution, format_depth, 2, flags, "light_depth");
        if (IsFlagSet(LightFlags::ShadowsTransparent))
        {
            m_texture_color = make_unique<RHI_Texture2DArray>(resolution, resolution, format_color, 2, flags, "light_color");
        }
    }
}  
