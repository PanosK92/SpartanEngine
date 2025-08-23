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
#include "../../Rendering/Renderer.h"
SP_WARNINGS_OFF
#include "../IO/pugixml.hpp"
SP_WARNINGS_ON
//===================================

//= NAMESPACES ===============
using namespace spartan::math;
using namespace std;
//============================

namespace spartan
{
    namespace
    {
        // directional matrix parameters
        const float cascade_near_half_extent = 20.0f;
        const float cascade_far_half_extent  = 256.0f;
        const float cascade_depth            = 1000.0f;

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
    }

    void Light::Save(pugi::xml_node& node)
    {
        node.append_attribute("flags")         = m_flags;
        node.append_attribute("light_type")    = static_cast<int>(m_light_type);
        node.append_attribute("color_r")       = m_color_rgb.r;
        node.append_attribute("color_g")       = m_color_rgb.g;
        node.append_attribute("color_b")       = m_color_rgb.b;
        node.append_attribute("temperature")   = m_temperature_kelvin;
        node.append_attribute("intensity")     = static_cast<int>(m_intensity);
        node.append_attribute("intensity_lum") = m_intensity_lumens_lux;
        node.append_attribute("range")         = m_range;
        node.append_attribute("angle")         = m_angle_rad;
        node.append_attribute("index")         = m_index;
    }
    
    void Light::Load(pugi::xml_node& node)
    {
        m_flags                = node.attribute("flags").as_uint(0);
        m_light_type           = static_cast<LightType>(node.attribute("light_type").as_int(static_cast<int>(LightType::Max)));
        m_color_rgb.r          = node.attribute("color_r").as_float(0.0f);
        m_color_rgb.g          = node.attribute("color_g").as_float(0.0f);
        m_color_rgb.b          = node.attribute("color_b").as_float(0.0f);
        m_temperature_kelvin   = node.attribute("temperature").as_float(0.0f);
        m_intensity            = static_cast<LightIntensity>(node.attribute("intensity").as_int(static_cast<int>(LightIntensity::bulb_500_watt)));
        m_intensity_lumens_lux = node.attribute("intensity_lum").as_float(2600.0f);
        m_range                = node.attribute("range").as_float(32.0f);
        m_angle_rad            = node.attribute("angle").as_float(math::deg_to_rad * 30.0f);
        m_index                = node.attribute("index").as_uint(0);
    
        UpdateMatrices(); // regenerate view/projection after loading
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

    bool Light::NeedsSkysphereUpdate() const
    {
        if (m_light_type != LightType::Directional)
            return false;

        static Quaternion last_rotation           = Quaternion::Identity;
        static Color last_color_rgb               = Color::standard_black;
        static float last_intensity_lumens_lux    = numeric_limits<float>::max();
    
        Quaternion current_rotation = GetEntity() ? GetEntity()->GetRotation() : Quaternion::Identity;
    
        bool rotation_changed  = current_rotation != last_rotation;
        bool color_changed     = m_color_rgb != last_color_rgb;
        bool intensity_changed = abs(m_intensity_lumens_lux - last_intensity_lumens_lux) > 0.01f;
    
        if (rotation_changed || color_changed || intensity_changed)
        {
            last_rotation             = current_rotation;
            last_color_rgb            = m_color_rgb;
            last_intensity_lumens_lux = m_intensity_lumens_lux;
            return true;
        }
    
        return false;
    }

    void Light::SetAtlasRectangle(uint32_t slice, const math::Rectangle& rectangle)
    {
        m_atlas_rectangles[slice] = rectangle;
        float atlas_w             = static_cast<float>(Renderer::GetRenderTarget(Renderer_RenderTarget::shadow_atlas)->GetWidth());
        float atlas_h             = static_cast<float>(Renderer::GetRenderTarget(Renderer_RenderTarget::shadow_atlas)->GetHeight());
        m_atlas_offsets[slice]    = Vector2(rectangle.x / atlas_w, rectangle.y / atlas_h);
        m_atlas_scales[slice]     = Vector2(rectangle.width / atlas_w, rectangle.height / atlas_h);
    }

    void Light::ClearAtlasRectangles()
    {
        m_atlas_rectangles.fill(math::Rectangle::Zero);
        m_atlas_offsets.fill(Vector2::Zero);
        m_atlas_scales.fill(Vector2::Zero);
    }

    uint32_t Light::GetSliceCount() const
    {
        if (m_light_type == LightType::Directional) return 2;
        if (m_light_type == LightType::Point)       return 6;
        return 1; // spot
    }

    void Light::UpdateMatrices()
    {
        ComputeViewMatrix();
        ComputeProjectionMatrix();
        SP_FIRE_EVENT(EventType::LightOnChanged);
    }

    void Light::ComputeViewMatrix()
    {
        const Vector3 position = GetEntity()->GetPosition(); // light’s base position (arbitrary for directional)

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
            float extents[2] = { cascade_near_half_extent, cascade_far_half_extent };
            for (int i = 0; i < 2; i++)
            {
                float rect_width       = m_atlas_rectangles[i].width;
                float texel_size_world = (2.0f * extents[i]) / rect_width; // world units per texel
                m_matrix_view[i].m30   = round(m_matrix_view[i].m30 / texel_size_world) * texel_size_world; // snap x-translation
                m_matrix_view[i].m31   = round(m_matrix_view[i].m31 / texel_size_world) * texel_size_world; // snap y-translation
                // z-translation (m32) remains unchanged for orthographic projection
            }
        }
        else if (m_light_type == LightType::Spot)
        {
            m_matrix_view[0] = Matrix::CreateLookAtLH(position, position + GetEntity()->GetForward(), Vector3::Up);
        }
        else if (m_light_type == LightType::Point)
        {
            // +X (right)
            m_matrix_view[0] = Matrix::CreateLookAtLH(position, position + Vector3::Right,    Vector3::Up);
            // -X (left)
            m_matrix_view[1] = Matrix::CreateLookAtLH(position, position + Vector3::Left,     Vector3::Up);
            // +Y (up)
            m_matrix_view[2] = Matrix::CreateLookAtLH(position, position + Vector3::Up,       Vector3::Backward);
            // -Y (down)
            m_matrix_view[3] = Matrix::CreateLookAtLH(position, position + Vector3::Down,     Vector3::Forward);
            // +Z (forward)
            m_matrix_view[4] = Matrix::CreateLookAtLH(position, position + Vector3::Forward,  Vector3::Up);
            // -Z (backward)
            m_matrix_view[5] = Matrix::CreateLookAtLH(position, position + Vector3::Backward, Vector3::Up);
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
        else // spot/point
        {
            const float aspect_ratio     = 1;
            const float fov_y_radians    = m_light_type == LightType::Spot ? m_angle_rad * 2.0f : math::pi_div_2 + 0.02f; // small epsilon to hide face seems

            for (uint32_t i = 0; i < GetSliceCount(); i++)
            {
                m_matrix_projection[i] = Matrix::CreatePerspectiveFieldOfViewLH(fov_y_radians, aspect_ratio, m_range, 0.05f);
                m_frustums[i]          = Frustum(m_matrix_view[i], m_matrix_projection[i], m_range);
            }
        }
    }

    bool Light::IsInViewFrustum(Renderable* renderable, const uint32_t array_index, const uint32_t instance_group_index) const
    {
        const BoundingBox& bounding_box = renderable->HasInstancing() ? renderable->GetBoundingBoxInstanceGroup(instance_group_index) : renderable->GetBoundingBox();
        const Vector3 center            = bounding_box.GetCenter();
        const Vector3 extents           = bounding_box.GetExtents();
        const bool ignore_depth         = m_light_type == LightType::Directional; // orthographic
        
        return m_frustums[array_index].IsVisible(center, extents, ignore_depth);
    }
}
