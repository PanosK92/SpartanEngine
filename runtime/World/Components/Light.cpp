/*
Copyright(c) 2016-2025 Panos Karabelas

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

//= INCLUDES ========================
#include "pch.h"
#include "Light.h"
#include "Camera.h"
#include "../World.h"
#include "../Entity.h"
#include "../../IO/FileStream.h"
#include "../../Rendering/Renderer.h"
//===================================

//= NAMESPACES ===============
using namespace spartan::math;
using namespace std;
//============================

namespace spartan
{
    namespace
    {
        float get_sensible_range(const LightType type)
        {
            if (type == LightType::Directional)
            {
                return numeric_limits<float>::max();
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

        const float near_plane   = 0.05f;
        const float depth_range  = 2000.0f; // beyond that, screen space shadows are enough
        float shadow_extent_near = 0.0f;
        float shadow_extent_far  = 0.0f;
        void update_shadow_extents()
        {
            const BoundingBox world_bounds = World::GetBoundingBox();
            const float max_extent         = world_bounds.GetExtents().Abs().Max();
        
            shadow_extent_near = 32.0f; // small and precise
            shadow_extent_far  = max(128.0f, max_extent * 0.5f); // large and blurry - going too large can cause artifacts, a clamp would work better here
        }
    }

    Light::Light(Entity* entity) : Component(entity)
    {
        SP_REGISTER_ATTRIBUTE_VALUE_VALUE(m_flags, uint32_t);
        SP_REGISTER_ATTRIBUTE_VALUE_VALUE(m_range, float);
        SP_REGISTER_ATTRIBUTE_VALUE_VALUE(m_intensity_lumens_lux, float);
        SP_REGISTER_ATTRIBUTE_VALUE_VALUE(m_angle_rad, float);
        SP_REGISTER_ATTRIBUTE_VALUE_VALUE(m_color_rgb, Color);
        SP_REGISTER_ATTRIBUTE_GET_SET(GetLightType, SetLightType, LightType);

        m_matrix_view.fill(Matrix::Identity);
        m_matrix_projection.fill(Matrix::Identity);

        SetColor(get_sensible_color(m_light_type));
        SetIntensity(LightIntensity::bulb_500_watt);
        SetRange(get_sensible_range(m_light_type));
        SetFlag(LightFlags::Shadows);
        SetFlag(LightFlags::ShadowsScreenSpace);
        SetFlag(LightFlags::DayNightCycle);
        SetFlag(LightFlags::ShadowDirty);

        m_entity_ptr->SetRotation(Quaternion::FromEulerAngles(35.0f, 0.0f, 0.0f));
    }

    void Light::OnTick()
    {
        // update matrices
        bool update_matrices = false;
        if (GetEntity()->GetTimeSinceLastTransform() <= 0.1f)
        {
            update_matrices = true;
        }

        if (m_light_type == LightType::Directional)
        {
            // the directional light follows the camera, and emulates day and night cycle, so we just always update
            update_matrices = true;

            // day night cycle
            if (GetFlag(LightFlags::DayNightCycle))
            {
                Quaternion rotation = Quaternion::FromAngleAxis(
                    (World::GetTimeOfDay() * 360.0f - 90.0f) * math::deg_to_rad, // angle in radians, -90° offset for horizon
                    Vector3::Right                                               // x-axis rotation (left to right)
                );

                GetEntity()->SetRotation(rotation);
            }
        }

        if (update_matrices)
        {
            UpdateMatrices();
        }

        // create shadow maps
        {
            uint32_t resolution     = Renderer::GetOption<uint32_t>(Renderer_Option::ShadowResolution);
            RHI_Format format_depth = RHI_Format::D32_Float;
            RHI_Format format_color = RHI_Format::R8G8B8A8_Unorm;
            uint32_t flags          = RHI_Texture_Rtv | RHI_Texture_Srv | RHI_Texture_ClearBlit;
            uint32_t array_length   = GetLightType() == LightType::Spot ? 1 : 2;
            bool resolution_dirty   = (m_texture_depth ? m_texture_depth->GetWidth() : resolution) != resolution;

            // spot light:        1 slice
            // directional light: 2 slices for cascades
            // point light:       2 slices for front and back paraboloid

            if ((GetFlag(LightFlags::Shadows) && !m_texture_depth) || resolution_dirty)
            {
                m_texture_depth = make_unique<RHI_Texture>(RHI_Texture_Type::Type2DArray, resolution, resolution, array_length, 1, format_depth, flags, "light_depth");
            }
            else if (!GetFlag(LightFlags::Shadows) && m_texture_depth)
            {
                m_texture_depth = nullptr;
            }

            if ((GetFlag(LightFlags::ShadowsTransparent) && !m_texture_depth) || resolution_dirty)
            {
                m_texture_color = make_unique<RHI_Texture>(RHI_Texture_Type::Type2DArray, resolution, resolution, array_length, 1, format_color, flags, "light_color");
            }
            else if (!GetFlag(LightFlags::ShadowsTransparent) && m_texture_color)
            {
                m_texture_color = nullptr;
            }
        }
    }

    void Light::Serialize(FileStream* stream)
    {
        stream->Write(static_cast<uint32_t>(m_light_type));
        stream->Write(m_flags);
        stream->Write(m_color_rgb);
        stream->Write(m_range);
        stream->Write(m_intensity_lumens_lux);
        stream->Write(m_angle_rad);
    }

    void Light::Deserialize(FileStream* stream)
    {
        SetLightType(static_cast<LightType>(stream->ReadAs<uint32_t>()));
        stream->Read(&m_flags);
        stream->Read(&m_color_rgb);
        stream->Read(&m_range);
        stream->Read(&m_intensity_lumens_lux);
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

            SP_FIRE_EVENT(EventType::LightOnChanged);
        }
    }

    void Light::SetLightType(LightType type)
    {
        if (m_light_type == type)
            return;

        m_light_type = type;

        SetColor(get_sensible_color(m_light_type));
        SetRange(get_sensible_range(m_light_type));

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

        if (intensity == LightIntensity::bulb_stadium)
        {
            m_intensity_lumens_lux = 200000.0f;
        }
        else if (intensity == LightIntensity::bulb_500_watt)
        {
            m_intensity_lumens_lux = 8500.0f;
        }
        else if (intensity == LightIntensity::bulb_150_watt)
        {
            m_intensity_lumens_lux = 2600.0f;
        }
        else if (intensity == LightIntensity::bulb_100_watt)
        {
            m_intensity_lumens_lux = 1600.0f;
        }
        else if (intensity == LightIntensity::bulb_60_watt)
        {
            m_intensity_lumens_lux = 800.0f;
        }
        else if (intensity == LightIntensity::bulb_25_watt)
        {
            m_intensity_lumens_lux = 200.0f;
        }
        else if (intensity == LightIntensity::bulb_flashlight)
        {
            m_intensity_lumens_lux = 100.0f;
        }
        else // black hole
        {
            m_intensity_lumens_lux = 0.0f;
        }

        SP_FIRE_EVENT(EventType::LightOnChanged);
    }

    void Light::SetIntensity(const float lumens)
    {
        m_intensity_lumens_lux = lumens;
        m_intensity            = LightIntensity::custom;
        SP_FIRE_EVENT(EventType::LightOnChanged);
    }

    float Light::GetIntensityWatt() const
    {
        // ideal luminous efficacy at 555nm in lm/w
        const float luminous_efficacy = 683.0f;
        float intensity               = m_intensity_lumens_lux;
        
        if (m_light_type == LightType::Directional)
        {
            // assume the intensity is in lux (lm/m^2)
            // converting lux to W/m^2 using a reference area of 1 m^2
            intensity = m_intensity_lumens_lux / luminous_efficacy;
        } else
        {
            intensity = m_intensity_lumens_lux / luminous_efficacy;
        }
        
        return intensity;
    }


    void Light::SetRange(float range)
    {
        range = clamp(range, 0.0f, numeric_limits<float>::max());
        if (range == m_range)
            return;

        m_range = range;
        UpdateMatrices();
    }

    void Light::SetAngle(float angle)
    {
        angle = clamp(angle, 0.0f, math::pi_2);
        if (angle == m_angle_rad)
            return;

        m_angle_rad = angle;
        UpdateMatrices();
    }

    void Light::UpdateMatrices()
    {
        ComputeViewMatrix();
        ComputeProjectionMatrix();
        SetFlag(LightFlags::ShadowDirty);

        SP_FIRE_EVENT(EventType::LightOnChanged);
    }

    void Light::ComputeViewMatrix()
    {
        const Vector3 position        = GetEntity()->GetPosition(); // light’s base position (arbitrary for directional)
        const Vector3 light_direction = GetEntity()->GetForward();  // light direction
        const uint32_t texture_width  = m_texture_depth ? m_texture_depth->GetWidth() : 0;
    
        if (m_light_type == LightType::Directional)
        {
            Camera* camera = World::GetCamera();
            if (!camera)
                return;
    
            Vector3 camera_pos = camera->GetEntity()->GetPosition();
            float depth_range  = 500.0f; // just an arbitrary value

            // near and cascades: center on camera position
            Vector3 near_eye_position = camera_pos - (light_direction * depth_range);
            Vector3 near_target       = camera_pos;
            m_matrix_view[0]          = Matrix::CreateLookAtLH(near_eye_position, near_target, Vector3::Up);
            m_matrix_view[1]          = m_matrix_view[0];
    
            // snapping to reduce shadow shimmering
            if (texture_width > 0)
            {
                update_shadow_extents();
                float extents[2] = { shadow_extent_near, shadow_extent_far };
    
                for (int i = 0; i < 2; i++)
                {
                    float texel_size     = (2.0f * extents[i]) / static_cast<float>(texture_width); // texel size in world space
                    m_matrix_view[i].m30 = round(m_matrix_view[i].m30 / texel_size) * texel_size;   // snap x-translation
                    m_matrix_view[i].m31 = round(m_matrix_view[i].m31 / texel_size) * texel_size;   // snap y-translation
                    // z-translation (m32) remains unchanged for orthographic projection
                }
            }
        }
        else if (m_light_type == LightType::Spot)
        {
            m_matrix_view[0] = Matrix::CreateLookAtLH(position, position + GetEntity()->GetForward(), Vector3::Up);
        }
        else if (m_light_type == LightType::Point)
        {
            m_matrix_view[0] = Matrix::CreateLookAtLH(position, position + Vector3::Forward, Vector3::Up); // front
            m_matrix_view[1] = Matrix::CreateLookAtLH(position, position - Vector3::Forward, Vector3::Up); // back
        }
    }
    
    void Light::ComputeProjectionMatrix()
    {
        if (m_light_type == LightType::Directional)
        {
            // create near cascade projection using world bounds extents
            m_matrix_projection[0] = Matrix::CreateOrthoOffCenterLH(
                -shadow_extent_near, shadow_extent_near,
                -shadow_extent_near, shadow_extent_near,
                depth_range, near_plane 
            );
    
            // create far cascade projection
            m_matrix_projection[1] = Matrix::CreateOrthoOffCenterLH(
                -shadow_extent_far, shadow_extent_far,
                -shadow_extent_far, shadow_extent_far,
                depth_range, near_plane
            );
    
            // update frustums for each cascade
            m_frustums[0] = Frustum(m_matrix_view[0], m_matrix_projection[0], 2 * depth_range);
            m_frustums[1] = Frustum(m_matrix_view[1], m_matrix_projection[1], 2 * depth_range);
        }
        else if (m_light_type == LightType::Spot)
        {
            if (!m_texture_depth)
                return;
    
            const float aspect_ratio = static_cast<float>(m_texture_depth->GetWidth()) / static_cast<float>(m_texture_depth->GetHeight());
            const float fov          = m_angle_rad * 2.0f;
            Matrix projection        = Matrix::CreatePerspectiveFieldOfViewLH(fov, aspect_ratio, m_range, near_plane);
            m_matrix_projection[0]   = projection;
            m_frustums[0]            = Frustum(m_matrix_view[0], projection, m_range - near_plane);
        }
    }

    bool Light::IsInViewFrustum(Renderable* renderable, const uint32_t array_index, const uint32_t instance_group_index) const
    {
        const BoundingBox& bounding_box = renderable->HasInstancing() ? renderable->GetBoundingBoxInstanceGroup(instance_group_index) : renderable->GetBoundingBox();

        if (m_light_type != LightType::Point)
        { 
            const Vector3 center    = bounding_box.GetCenter();
            const Vector3 extents   = bounding_box.GetExtents();
            const bool ignore_depth = m_light_type == LightType::Directional; // orthographic
            
            return m_frustums[array_index].IsVisible(center, extents, ignore_depth);
        }

        // paraboloid point light
        {
            float sign = (array_index == 0) ? 1.0f : -1.0f;
            array<Vector3, 8> corners;
            bounding_box.GetCorners(&corners);
            for (const Vector3& corner : corners)
            {
                Vector3 to_corner = corner - m_entity_ptr->GetPosition();
                if (Vector3::Dot(to_corner, sign * m_entity_ptr->GetForward()) >= 0.0f)
                    return true; // at least one corner is inside
            }

            return false; // no corners are inside
        }
    }
}  
