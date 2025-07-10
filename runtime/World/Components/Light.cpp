/*
Copyright(c) 2015-2025 Panos Karabelas

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
        // directional shadows parameters
        const float cascade_near_half_extent = 20.0f;
        const float cascade_far_half_extent  = 256.0f;
        const float cascade_depth            = 1'000.0f;

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
                return Color::light_sky_clear;
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
            // day night cycle
            if (GetFlag(LightFlags::DayNightCycle))
            {
                Quaternion rotation = Quaternion::FromAxisAngle(
                    Vector3::Right,                                             // x-axis rotation (left to right)
                    (World::GetTimeOfDay() * 360.0f - 90.0f) * math::deg_to_rad // angle in radians, -90° offset for horizon
                );

                GetEntity()->SetRotation(rotation);
                update_matrices = true;
            }

            // it follows the camera, so it also need to updated if it moves
            if (Camera* camera = World::GetCamera())
            {
                update_matrices = camera->GetEntity()->GetTimeSinceLastTransform() < 0.1f ? true : update_matrices;
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

    void Light::SetIntensity(const float lumens_lux)
    {
        m_intensity_lumens_lux = lumens_lux;
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

    bool Light::NeedsLutAtmosphericScatteringUpdate() const
    {
        if (m_light_type != LightType::Directional)
            return false;

        static Quaternion last_rotation           = Quaternion::Identity;
        static Color last_color_rgb               = Color::standard_black;
        static float last_intensity_lumens_lux    = std::numeric_limits<float>::max();
    
        Quaternion current_rotation = GetEntity() ? GetEntity()->GetRotation() : Quaternion::Identity;
    
        bool rotation_changed  = current_rotation != last_rotation;
        bool color_changed     = m_color_rgb != last_color_rgb;
        bool intensity_changed = std::abs(m_intensity_lumens_lux - last_intensity_lumens_lux) > 0.01f;
    
        if (rotation_changed || color_changed || intensity_changed)
        {
            last_rotation              = current_rotation;
            last_color_rgb             = m_color_rgb;
            last_intensity_lumens_lux = m_intensity_lumens_lux;
            return true;
        }
    
        return false;
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
        const uint32_t texture_width  = m_texture_depth ? m_texture_depth->GetWidth() : 0;
    
        if (m_light_type == LightType::Directional)
        {
            Camera* camera = World::GetCamera();
            if (!camera)
                return;
    
            // both near and far cascades follow the camera
            Vector3 camera_pos = camera->GetEntity()->GetPosition();
            Vector3 position   = camera_pos - GetEntity()->GetForward() * cascade_depth * 0.5f;
            m_matrix_view[0]   = Matrix::CreateLookAtLH(position, camera_pos, Vector3::Up);
            m_matrix_view[1]   = m_matrix_view[0];
    
            // compute shadow extents inline
            if (texture_width > 0)
            {
                float extents[2] = { cascade_near_half_extent, cascade_far_half_extent };
    
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
            m_matrix_view[0] = Matrix::CreateLookAtLH(position, position + Vector3::Forward, Vector3::Up); // Front
            m_matrix_view[1] = Matrix::CreateLookAtLH(position, position - Vector3::Forward, Vector3::Up); // Back
        }
    }
    
   void Light::ComputeProjectionMatrix()
    {
       if (m_light_type == LightType::Directional)
        {
            Camera* camera = World::GetCamera();
            if (!camera)
                return;

            m_matrix_projection[0] = Matrix::CreateOrthoOffCenterLH(
                -cascade_near_half_extent, cascade_near_half_extent, // left, right
                -cascade_near_half_extent, cascade_near_half_extent, // bottom, top
                cascade_depth, 0.0f                                  // reverse-z near plane, far plane 0
            );

            m_matrix_projection[1] = Matrix::CreateOrthoOffCenterLH(
                -cascade_far_half_extent, cascade_far_half_extent,
                -cascade_far_half_extent, cascade_far_half_extent,
                cascade_depth, 0.0f
            );

            m_frustums[0] = Frustum(m_matrix_view[0], m_matrix_projection[0], cascade_depth);
            m_frustums[1] = Frustum(m_matrix_view[1], m_matrix_projection[1], cascade_depth);
        }
        else if (m_light_type == LightType::Spot)
        {
            if (!m_texture_depth)
                return;
    
            const float near_plane   = 0.05f;
            const float aspect_ratio = static_cast<float>(m_texture_depth->GetWidth()) / static_cast<float>(m_texture_depth->GetHeight());
            const float fov          = m_angle_rad * 2.0f;
            Matrix projection        = Matrix::CreatePerspectiveFieldOfViewLH(fov, aspect_ratio, near_plane, m_range);
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
