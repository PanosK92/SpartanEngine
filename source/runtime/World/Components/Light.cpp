/*
Copyright(c) 2015-2026 Panos Karabelas

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
#include "Render.h"
#include "../World.h"
#include "../Entity.h"
#include "../../Rendering/Renderer.h"
SP_WARNINGS_OFF
#include <sol/sol.hpp>
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
        const float cascade_near_extent    = 20.0f;
        const float cascade_far_extent     = 300.0f;
        const float cascade_depth          = 1000.0f;
        const float cascade_far_max_extent = FLT_MAX;

        float get_sensible_range(const LightType type, const float photometric_intensity = 0.0f, const float angle_rad = math::deg_to_rad * 30.0f)
        {
            const float illuminance_cutoff_lux = 0.25f;
            const float min_range              = 0.5f;

            if (type == LightType::Directional)
            {
                return numeric_limits<float>::max();
            }
            else if (type == LightType::Point)
            {
                const float denominator = 4.0f * pi * illuminance_cutoff_lux;
                return max(min_range, sqrt(max(photometric_intensity, 0.0f) / denominator));
            }
            else if (type == LightType::Spot)
            {
                const float solid_angle = 2.0f * pi * (1.0f - cos(max(angle_rad, 0.001f)));
                const float candela     = max(photometric_intensity, 0.0f) / max(solid_angle, 0.001f);
                return max(min_range, sqrt(candela / illuminance_cutoff_lux));
            }
            else if (type == LightType::Area)
            {
                const float denominator = 2.0f * pi * illuminance_cutoff_lux;
                return max(min_range, sqrt(max(photometric_intensity, 0.0f) / denominator));
            }

            return 0.0f;
        }

        bool is_sensible_range(const float range, const LightType type, const float photometric_intensity, const float angle_rad)
        {
            if (type == LightType::Directional)
            {
                return false;
            }

            const float sensible_range = get_sensible_range(type, photometric_intensity, angle_rad);
            const float tolerance      = max(0.01f, sensible_range * 0.001f);
            return abs(range - sensible_range) <= tolerance;
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
            else if (type == LightType::Area)
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
        SP_REGISTER_ATTRIBUTE_VALUE_VALUE(m_intensity_photometric, float);
        SP_REGISTER_ATTRIBUTE_VALUE_VALUE(m_angle_rad, float);
        SP_REGISTER_ATTRIBUTE_VALUE_VALUE(m_color_rgb, Color);
        SP_REGISTER_ATTRIBUTE_VALUE_VALUE(m_temperature_kelvin, float);
        SP_REGISTER_ATTRIBUTE_VALUE_VALUE(m_draw_distance, float);
        SP_REGISTER_ATTRIBUTE_VALUE_VALUE(m_distance_shadows, float);
        SP_REGISTER_ATTRIBUTE_VALUE_VALUE(m_distance_volumetric, float);
        SP_REGISTER_ATTRIBUTE_VALUE_VALUE(m_bounding_box, math::BoundingBox);
        SP_REGISTER_ATTRIBUTE_VALUE_VALUE(m_far_cascade_min, math::Vector3);
        SP_REGISTER_ATTRIBUTE_VALUE_VALUE(m_far_cascade_max, math::Vector3);
        SP_REGISTER_ATTRIBUTE_VALUE_VALUE(m_is_active_previous_frame, bool);
        SP_REGISTER_ATTRIBUTE_VALUE_VALUE(m_index, uint32_t);
        SP_REGISTER_ATTRIBUTE_VALUE_VALUE(m_area_width, float);
        SP_REGISTER_ATTRIBUTE_VALUE_VALUE(m_area_height, float);
        SP_REGISTER_ATTRIBUTE_GET_SET(GetLightType, SetLightType, LightType);

        m_matrix_view.fill(Matrix::Identity);
        m_matrix_projection.fill(Matrix::Identity);

        SetColor(get_sensible_color(m_light_type));
        SetIntensity(LightIntensity::bulb_500_watt);
        SetRange(get_sensible_range(m_light_type, m_intensity_photometric, m_angle_rad));
        SetFlag(LightFlags::Shadows);
    }

    Light::~Light()
    {

    }

    void Light::Tick()
    {
        // detect transform change
        bool update_matrices = false;
        if (GetEntity()->GetTimeSinceLastTransform() <= 0.1f)
        {
            update_matrices = true;
        }

        // detect day night cycle change
        if (m_light_type == LightType::Directional)
        {
            // day night cycle
            if (GetFlag(LightFlags::DayNightCycle))
            {
                Quaternion rotation = Quaternion::FromAxisAngle(
                    Vector3::Right,                                                                               // x-axis rotation (left to right)
                    (World::GetTimeOfDay(GetFlag(LightFlags::RealTimeCycle)) * 360.0f - 90.0f) * math::deg_to_rad // angle in radians, -90� offset for horizon
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

        // detect active state change
        if (m_is_active_previous_frame != GetEntity()->GetActive())
        {
            m_is_active_previous_frame = GetEntity()->GetActive();
            update_matrices = true;
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
        node.append_attribute("intensity_photometric") = m_intensity_photometric;
        node.append_attribute("range")         = m_range;
        node.append_attribute("angle")         = m_angle_rad;
        node.append_attribute("index")         = m_index;
        node.append_attribute("preset")        = static_cast<int>(m_preset);
        node.append_attribute("area_width")    = m_area_width;
        node.append_attribute("area_height")   = m_area_height;
        node.append_attribute("draw_distance")       = m_draw_distance;
        node.append_attribute("distance_shadows")    = m_distance_shadows;
        node.append_attribute("distance_volumetric") = m_distance_volumetric;
    }

    void Light::Load(pugi::xml_node& node)
    {
        m_flags                = node.attribute("flags").as_uint(m_flags);
        int light_type         = node.attribute("light_type").as_int(static_cast<int>(m_light_type));
        if (light_type < static_cast<int>(LightType::Directional) || light_type >= static_cast<int>(LightType::Max))
        {
            light_type = static_cast<int>(LightType::Point);
        }
        m_light_type           = static_cast<LightType>(light_type);
        SetColor(get_sensible_color(m_light_type));
        m_color_rgb.r          = node.attribute("color_r").as_float(m_color_rgb.r);
        m_color_rgb.g          = node.attribute("color_g").as_float(m_color_rgb.g);
        m_color_rgb.b          = node.attribute("color_b").as_float(m_color_rgb.b);
        m_temperature_kelvin   = node.attribute("temperature").as_float(m_temperature_kelvin);
        m_intensity = static_cast<LightIntensity>(node.attribute("intensity").as_int(static_cast<int>(m_intensity)));

        pugi::xml_attribute intensity_attribute = node.attribute("intensity_photometric");
        if (!intensity_attribute)
        {
            intensity_attribute = node.attribute("intensity_lum");
        }
        m_intensity_photometric = intensity_attribute.as_float(m_intensity_photometric);
        m_angle_rad            = node.attribute("angle").as_float(m_angle_rad);
        m_range                = node.attribute("range").as_float(get_sensible_range(m_light_type, m_intensity_photometric, m_angle_rad));
        m_index                = node.attribute("index").as_uint(m_index);
        m_preset               = static_cast<LightPreset>(node.attribute("preset").as_int(static_cast<int>(m_preset)));
        m_area_width           = node.attribute("area_width").as_float(m_area_width);
        m_area_height          = node.attribute("area_height").as_float(m_area_height);
        m_draw_distance        = node.attribute("draw_distance").as_float(m_draw_distance);
        m_distance_shadows     = node.attribute("distance_shadows").as_float(m_distance_shadows);
        m_distance_volumetric  = node.attribute("distance_volumetric").as_float(m_distance_volumetric);
        m_screen_space_shadows_slice_index = 0;

        if (m_light_type != LightType::Directional || !(m_flags & LightFlags::Shadows))
        {
            m_flags &= ~static_cast<uint32_t>(LightFlags::ShadowsScreenSpace);
        }

        UpdateMatrices(); // regenerate view/projection after loading
    }

    void Light::RegisterForScripting(sol::state_view State)
    {
        State.new_enum("LightType",
            "Directional",              LightType::Directional,
            "Point",                    LightType::Point,
            "Spot",                     LightType::Spot,
            "Area",                     LightType::Area,
            "Max",                      LightType::Max
        );

        State.new_enum("LightIntensity",
            "bulb_stadium",             LightIntensity::bulb_stadium,
            "bulb_500_watt",            LightIntensity::bulb_500_watt,
            "bulb_150_watt",            LightIntensity::bulb_150_watt,
            "bulb_100_watt",            LightIntensity::bulb_100_watt,
            "bulb_60_watt",             LightIntensity::bulb_60_watt,
            "bulb_25_watt",             LightIntensity::bulb_25_watt,
            "bulb_flashlight",          LightIntensity::bulb_flashlight,
            "black_hole",               LightIntensity::black_hole,
            "custom",                   LightIntensity::custom
        );

        State.new_enum("LightIntensityUnit",
            "Lux",                      LightIntensityUnit::Lux,
            "Lumens",                   LightIntensityUnit::Lumens
        );

        State.new_enum("LightFlags",
            "Shadows",                  LightFlags::Shadows,
            "ShadowsScreenSpace",       LightFlags::ShadowsScreenSpace,
            "Volumetric",               LightFlags::Volumetric,
            "DayNightCycle",            LightFlags::DayNightCycle,
            "RealTimeCycle",            LightFlags::RealTimeCycle
        );

        State.new_enum("LightPreset",
            "dawn",                     LightPreset::dawn,
            "day",                      LightPreset::day,
            "dusk",                     LightPreset::dusk,
            "night",                    LightPreset::night,
            "david_lynch",              LightPreset::david_lynch,
            "custom",                   LightPreset::custom
        );


        State.new_usertype<Light>("Light",
            sol::base_classes,              sol::bases<Component>(),

            "SetTemperature",               &Light::SetTemperature,
            "GetTemperature",               &Light::GetTemperature,


            "SetColor",                     &Light::SetColor,
            "GetColor",                     &Light::GetColor,
            "GetIntensityPhotometric",      &Light::GetIntensityPhotometric,
            "GetIntensityLumens",           &Light::GetIntensityLumens,
            "GetIntensityUnit",             &Light::GetIntensityUnit,
            "GetIntensityRadiometric",      &Light::GetIntensityRadiometric,
            "GetIntensityWatt",             &Light::GetIntensityWatt,

            "SetIntensity",                 sol::overload(
                [](Light& Self, float Lumens) { Self.SetIntensity(Lumens); },
                [](Light& Self, LightIntensity Intensity) { Self.SetIntensity(Intensity); }),

            "SetAngle",                     &Light::SetAngle,
            "GetAngle",                     &Light::GetAngle,

            "SetRange",                     &Light::SetRange,
            "GetRange",                     &Light::GetRange,

            "SetAreaWidth",                 &Light::SetAreaWidth,
            "GetAreaWidth",                 &Light::GetAreaWidth,
            "SetAreaHeight",                &Light::SetAreaHeight,
            "GetAreaHeight",                &Light::GetAreaHeight,
            "FitToMesh",                    &Light::FitToMesh,

            "IsInViewFrustrum",             &Light::IsInViewFrustum,

            "SetDrawDistance",              &Light::SetDrawDistance,
            "GetDrawDistance",              &Light::GetDrawDistance,

            "SetShadowDistance",            &Light::SetShadowDistance,
            "GetShadowDistance",            &Light::GetShadowDistance,
            "SetVolumetricDistance",        &Light::SetVolumetricDistance,
            "GetVolumetricDistance",        &Light::GetVolumetricDistance,

            "GetLightType",                 &Light::GetLightType,
            "SetLightType",                 &Light::SetLightType,

            "SetFlag",                      [](Light& Self, LightFlags flag, bool enable) { Self.SetFlag(flag, enable); },
            "GetFlag",                      &Light::GetFlag,
            "SetPreset",                    &Light::SetPreset,
            "GetPreset",                    &Light::GetPreset,

            "NeedsSkysphereUpdate",         &Light::NeedsSkysphereUpdate,
            "GetSliceCount",                &Light::GetSliceCount,

            "GetAtlasOffset",               &Light::GetAtlasOffset,
            "GetAtlasScale",                &Light::GetAtlasScale,
            "GetBoundingBox",               &Light::GetBoundingBox

        );



    }

    sol::reference Light::AsLua(sol::state_view state)
    {
        return sol::make_reference(state, this);
    }

    void Light::SetFlag(const LightFlags flag, const bool enable)
    {
        if (flag == LightFlags::ShadowsScreenSpace && enable && m_light_type != LightType::Directional)
        {
            return;
        }

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
        }
    }

    void Light::SetLightType(LightType type)
    {
        if (m_light_type == type)
        {
            return;
        }

        m_light_type = type;

        SetColor(get_sensible_color(m_light_type));
        SetRange(get_sensible_range(m_light_type, m_intensity_photometric, m_angle_rad));

        if (m_light_type != LightType::Directional)
        {
            m_flags &= ~static_cast<uint32_t>(LightFlags::ShadowsScreenSpace);
            m_screen_space_shadows_slice_index = 0;
        }

        UpdateMatrices();
    }

    void Light::SetTemperature(const float temperature_kelvin)
    {
        m_temperature_kelvin = temperature_kelvin;
        m_color_rgb          = Color(temperature_kelvin);
    }

    void Light::SetColor(const Color& rgb)
    {
        m_color_rgb = rgb;

        if (rgb == Color::light_sky_clear)
        {
            m_temperature_kelvin = 15000.0f;
        }
        else if (rgb == Color::light_sky_daylight_overcast)
        {
            m_temperature_kelvin = 6500.0f;
        }
        else if (rgb == Color::light_sky_moonlight)
        {
            m_temperature_kelvin = 4000.0f;
        }
        else if (rgb == Color::light_sky_sunrise)
        {
            m_temperature_kelvin = 2000.0f;
        }
        else if (rgb == Color::light_candle_flame)
        {
            m_temperature_kelvin = 1850.0f;
        }
        else if (rgb == Color::light_direct_sunlight)
        {
            m_temperature_kelvin = 5778.0f;
        }
        else if (rgb == Color::light_digital_display)
        {
            m_temperature_kelvin = 6500.0f;
        }
        else if (rgb == Color::light_fluorescent_tube_light)
        {
            m_temperature_kelvin = 5000.0f;
        }
        else if (rgb == Color::light_kerosene_lamp)
        {
            m_temperature_kelvin = 1850.0f;
        }
        else if (rgb == Color::light_light_bulb)
        {
            m_temperature_kelvin = 2700.0f;
        }
        else if (rgb == Color::light_photo_flash)
        {
            m_temperature_kelvin = 5500.0f;
        }
    }

    void Light::SetIntensity(const LightIntensity intensity)
    {
        const bool update_range = is_sensible_range(m_range, m_light_type, m_intensity_photometric, m_angle_rad);
        m_intensity = intensity;

        if (intensity == LightIntensity::bulb_stadium)
        {
            m_intensity_photometric = 200000.0f;
        }
        else if (intensity == LightIntensity::bulb_500_watt)
        {
            m_intensity_photometric = 8500.0f;
        }
        else if (intensity == LightIntensity::bulb_150_watt)
        {
            m_intensity_photometric = 2600.0f;
        }
        else if (intensity == LightIntensity::bulb_100_watt)
        {
            m_intensity_photometric = 1600.0f;
        }
        else if (intensity == LightIntensity::bulb_60_watt)
        {
            m_intensity_photometric = 800.0f;
        }
        else if (intensity == LightIntensity::bulb_25_watt)
        {
            m_intensity_photometric = 200.0f;
        }
        else if (intensity == LightIntensity::bulb_flashlight)
        {
            m_intensity_photometric = 100.0f;
        }
        else // black hole
        {
            m_intensity_photometric = 0.0f;
        }

        if (update_range)
        {
            SetRange(get_sensible_range(m_light_type, m_intensity_photometric, m_angle_rad));
        }
    }

    void Light::SetIntensity(const float photometric_intensity)
    {
        const bool update_range = is_sensible_range(m_range, m_light_type, m_intensity_photometric, m_angle_rad);
        m_intensity_photometric = photometric_intensity;
        m_intensity             = LightIntensity::custom;

        if (update_range)
        {
            SetRange(get_sensible_range(m_light_type, m_intensity_photometric, m_angle_rad));
        }
    }

    LightIntensityUnit Light::GetIntensityUnit() const
    {
        return m_light_type == LightType::Directional ? LightIntensityUnit::Lux : LightIntensityUnit::Lumens;
    }

    void Light::SetPreset(const LightPreset preset)
    {
        m_preset = preset;

        float time_of_day = 0.0f;
        float temperature = 0.0f;
        float intensity   = 0.0f;
        float yaw_degrees = 0.0f; // horizontal rotation around Y axis

        // preset calibration philosophy, every value below is grounded in measured real world
        // photometry (color temperature kelvin, illuminance lux at the receiving surface) and
        // tuned slightly toward the cinematic end of the realistic range so the directional
        // light's chromaticity reads clearly through hdr tonemapping instead of compressing
        // toward neutral white, the temperatures are blackbody equivalents of the perceived
        // sun color at ground level after atmospheric extinction, not the sun's photospheric
        // 5778 K which appears neutral white once the blue has been scattered out
        //
        // illuminance references for direct sun on a horizontal surface
        //   civil dawn / dusk, sun just above horizon  ~  500 to  2000 lx
        //   golden hour, sun low                       ~ 5000 to 25000 lx
        //   overcast midday                            ~10000 to 25000 lx
        //   clear sunny noon                           ~50000 to 130000 lx
        //   full moon                                  ~  0.1 to    0.3 lx
        switch (preset)
        {
        case LightPreset::dawn:
            // sun just above the horizon, atmospheric extinction warms it heavily
            // 2200 K reads as deep orange red, ~800 lx is civil dawn intensity
            time_of_day = 0.25f; // 6:00 AM
            temperature = 2200.0f;
            intensity   = 800.0f;
            break;

        case LightPreset::day:
            // peak direct midday sun, ~80000 lx is a clear summer noon at temperate latitudes
            // (100000 lx is the peak summer equator value and tends to crush the tonemap into
            // featureless white on most displays), 5000 K is the d50 illuminant aka graphic
            // arts daylight, this is the perceived sun color at ground level once the blue
            // and uv have been scattered out into the sky and reads as warm white rather than
            // the neutral white of 5500 to 5800 K which loses its chromaticity through the
            // hdr clamp and the tonemap highlight compression
            time_of_day = 0.5f; // 12:00 PM
            temperature = 5000.0f;
            intensity   = 80000.0f;
            break;

        case LightPreset::dusk:
            // golden hour, sun low in the sky with strong warm tones
            // 3000 K is the warm end of golden hour, ~15000 lx matches a clear sky 30 min before sunset
            time_of_day = 0.69f; // 4:30 PM
            temperature = 3000.0f;
            intensity   = 15000.0f;
            break;

        case LightPreset::night:
            // moonlight is sunlight reflected off the lunar surface so its spectrum is close
            // to the sun, the perceived color shifts cooler/bluer due to the purkinje effect
            // (rod vision peaks at shorter wavelengths in low light), 4200 K captures that
            // shift, ~0.3 lx is full moon at the zenith on a clear night
            time_of_day = 0.875f; // 9:00 PM
            temperature = 4200.0f;
            intensity   = 0.3f;
            break;

        case LightPreset::david_lynch:
            // stylized dreamy sunset, deep orange pink tones, intentionally low key but bright
            // enough that scene ambient (sky ibl) is visible (the previous 5000 lx produced an
            // almost black scene because the indirect lighting scales linearly with sun intensity)
            time_of_day = 0.74f; // sun near horizon for sunset colors
            temperature = 2400.0f;
            intensity   = 12000.0f;
            yaw_degrees = 125.0f; // rotate to avoid mountain
            break;

        case LightPreset::custom:
            // do nothing, keep current settings
            return;
        }

        // set time of day
        World::SetTimeOfDay(time_of_day);

        // set light properties
        SetTemperature(temperature);
        SetIntensity(intensity);

        // set rotation based on time of day (only for directional lights)
        if (m_light_type == LightType::Directional)
        {
            // elevation from time of day
            float elevation_rad = (time_of_day * 360.0f - 90.0f) * math::deg_to_rad;
            Quaternion elevation = Quaternion::FromAxisAngle(Vector3::Right, elevation_rad);

            // horizontal rotation (yaw)
            Quaternion yaw = Quaternion::FromAxisAngle(Vector3::Up, yaw_degrees * math::deg_to_rad);

            // combine: yaw first, then elevation
            GetEntity()->SetRotation(yaw * elevation);
            UpdateMatrices();
        }
    }

    float Light::GetIntensityRadiometric() const
    {
        // use the same 683 lm/w white-light approximation that the display path uses.
        // this keeps the engine's photometric authoring and shader radiometric math in sync.
        const float luminous_efficacy = 683.0f;
        const float photometric_intensity = max(m_intensity_photometric, 0.0f);

        if (m_light_type == LightType::Directional)
        {
            // directional lights store illuminance in lux.
            return photometric_intensity / luminous_efficacy;
        }

        // convert luminous flux to radiant flux.
        float radiant_flux = photometric_intensity / luminous_efficacy;

        if (m_light_type == LightType::Point)
        {
            // point lights emit isotropically across the full sphere.
            return radiant_flux / (4.0f * pi);
        }

        if (m_light_type == LightType::Spot)
        {
            // spot lights store total beam flux in lumens.
            // distribute that beam over the actual cone solid angle so narrowing the cone raises candela.
            float solid_angle = 2.0f * pi * (1.0f - cos(max(m_angle_rad, 0.001f)));
            return radiant_flux / max(solid_angle, 0.001f);
        }

        if (m_light_type == LightType::Area)
        {
            const float emitter_area = max(m_area_width * m_area_height, 0.0001f);
            return radiant_flux / (pi * emitter_area);
        }

        return radiant_flux / (4.0f * pi);
    }

    void Light::SetRange(float range)
    {
        range = clamp(range, 0.0f, numeric_limits<float>::max());
        if (range == m_range)
        {
            return;
        }

        m_range = range;
        UpdateMatrices();
    }

    void Light::SetAngle(float angle)
    {
        angle = clamp(angle, 0.0f, math::pi_2);
        if (angle == m_angle_rad)
        {
            return;
        }

        const bool update_range = is_sensible_range(m_range, m_light_type, m_intensity_photometric, m_angle_rad);
        m_angle_rad = angle;
        if (update_range)
        {
            m_range = get_sensible_range(m_light_type, m_intensity_photometric, m_angle_rad);
        }

        UpdateMatrices();
    }

    void Light::SetAreaWidth(float width)
    {
        width = clamp(width, 0.01f, 100.0f);
        if (width == m_area_width)
        {
            return;
        }

        m_area_width = width;
        UpdateMatrices();
    }

    void Light::SetAreaHeight(float height)
    {
        height = clamp(height, 0.01f, 100.0f);
        if (height == m_area_height)
        {
            return;
        }

        m_area_height = height;
        UpdateMatrices();
    }

    bool Light::FitToMesh()
    {
        // only meaningful for area lights (rectangular emitter)
        if (m_light_type != LightType::Area)
        {
            return false;
        }

        Render* renderable = GetEntity()->GetComponent<Render>();
        if (!renderable || !renderable->GetMesh())
        {
            return false;
        }

        const BoundingBox& bbox_local = renderable->GetBoundingBoxMesh();
        const Vector3 bbox_extent     = bbox_local.GetMax() - bbox_local.GetMin();
        if (bbox_extent == Vector3::Zero)
        {
            return false;
        }

        // bbox is in mesh local space, scale by the entity's world scale to get the world footprint
        const Vector3 world_scale     = GetEntity()->GetScale();
        const Vector3 scaled_extent(bbox_extent.x * world_scale.x, bbox_extent.y * world_scale.y, bbox_extent.z * world_scale.z);

        // pick the two largest extents so the rectangle matches the tube regardless of which local axis it was authored along
        float e[3] = { scaled_extent.x, scaled_extent.y, scaled_extent.z };
        if (e[0] < e[1])
        {
            swap(e[0], e[1]);
        }
        if (e[1] < e[2])
        {
            swap(e[1], e[2]);
        }
        if (e[0] < e[1])
        {
            swap(e[0], e[1]);
        }

        SetAreaWidth(e[0]);
        SetAreaHeight(e[1]);
        return true;
    }

    bool Light::NeedsSkysphereUpdate() const
    {
        if (m_light_type != LightType::Directional)
        {
            return false;
        }

        static Quaternion last_rotation           = Quaternion::Identity;
        static Color last_color_rgb               = Color::standard_black;
        static float last_intensity_photometric   = numeric_limits<float>::max();

        Quaternion current_rotation = GetEntity() ? GetEntity()->GetRotation() : Quaternion::Identity;

        bool rotation_changed  = current_rotation != last_rotation;
        bool color_changed     = m_color_rgb != last_color_rgb;
        bool intensity_changed = abs(m_intensity_photometric - last_intensity_photometric) > 0.01f;

        if (rotation_changed || color_changed || intensity_changed)
        {
            last_rotation             = current_rotation;
            last_color_rgb            = m_color_rgb;
            last_intensity_photometric = m_intensity_photometric;
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
        if (m_light_type == LightType::Directional)
        {
            return 2;
        }
        if (m_light_type == LightType::Point)
        {
            return 6;
        }
        return 1; // spot and area lights use a single slice
    }

    void Light::UpdateMatrices()
    {
        UpdateViewMatrix();
        UpdateProjectionMatrix();
        UpdateBoundingBox();
    }

    void Light::UpdateViewMatrix()
    {
        const Vector3 position = GetEntity()->GetPosition(); // light�s base position (arbitrary for directional)

        if (m_light_type == LightType::Directional)
        {
            Camera* camera = World::GetCamera();
            if (!camera)
            {
                return;
            }

            // both cascades follow the camera
            Vector3 camera_pos = camera->GetEntity()->GetPosition();
            Vector3 position   = camera_pos - GetEntity()->GetForward() * cascade_depth * 0.5f;
            m_matrix_view[0]   = Matrix::CreateLookAtLH(position, camera_pos, Vector3::Up);
            m_matrix_view[1]   = m_matrix_view[0];

            // move the light in words units per texel to avoid shimmering
            {
                // compute shadow extents (both fixed sizes)
                float extents[2];
                extents[0] = cascade_near_extent; // near cascade: fixed
                extents[1] = cascade_far_extent;  // far cascade: fixed, bigger than near

                float atlas_width = static_cast<float>(Renderer::GetRenderTarget(Renderer_RenderTarget::shadow_atlas)->GetWidth());
                for (uint32_t i  = 0; i < 2; i++)
                {
                    float rect_width           = m_atlas_rectangles[i].width;                // cascade rectangle width in atlas
                    float atlas_scale          = rect_width / atlas_width;                   // proportion of atlas used by cascade
                    float effective_resolution = atlas_width * atlas_scale;                  // effective resolution for cascade
                    float texel_size_world     = (2.0f * extents[i]) / effective_resolution; // world units per texel
                    m_matrix_view[i].m30       = round(m_matrix_view[i].m30 / texel_size_world) * texel_size_world; // snap x
                    m_matrix_view[i].m31       = round(m_matrix_view[i].m31 / texel_size_world) * texel_size_world; // snap y
                    // z-translation (m32) remains unchanged for orthographic projection
                }
            }
        }
        else if (m_light_type == LightType::Spot)
        {
            // use the entity's own up vector so a forward of world up or down does not collapse the cross product
            m_matrix_view[0] = Matrix::CreateLookAtLH(position, position + GetEntity()->GetForward(), GetEntity()->GetUp());
        }
        else if (m_light_type == LightType::Area)
        {
            // use the entity's own up vector so a forward of world up or down does not collapse the cross product
            m_matrix_view[0] = Matrix::CreateLookAtLH(position, position + GetEntity()->GetForward(), GetEntity()->GetUp());
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

    void Light::UpdateProjectionMatrix()
    {
        if (m_light_type == LightType::Directional)
        {
            // near cascade (tight, camera following)
            m_matrix_projection[0] = Matrix::CreateOrthoOffCenterLH(
                -cascade_near_extent, cascade_near_extent,
                -cascade_near_extent, cascade_near_extent,
                cascade_depth, 0.0f
            );

            // far cascade (camera following, fixed size, bigger than near cascade)
            m_matrix_projection[1] = Matrix::CreateOrthoOffCenterLH(
                -cascade_far_extent, cascade_far_extent,
                -cascade_far_extent, cascade_far_extent,
                cascade_depth, 0.0f
            );

            m_frustums[0] = Frustum(m_matrix_view[0], m_matrix_projection[0]);
            m_frustums[1] = Frustum(m_matrix_view[1], m_matrix_projection[1]);
        }
        else if (m_light_type == LightType::Area)
        {
            // wide perspective from the rectangle center so the shadow map captures occluders
            // across the front hemisphere, an orthographic at the rectangle size only covers
            // a narrow column directly under the emitter and misses anything outside it
            const float fov_y_radians = math::deg_to_rad * 120.0f;
            const float aspect_ratio  = 1.0f;
            m_matrix_projection[0]    = Matrix::CreatePerspectiveFieldOfViewLH(fov_y_radians, aspect_ratio, m_range, 0.05f);
            m_frustums[0]             = Frustum(m_matrix_view[0], m_matrix_projection[0]);
        }
        else // spot/point
        {
            const float aspect_ratio  = 1;
            const float fov_y_radians = m_light_type == LightType::Spot ? m_angle_rad * 2.0f : math::pi_div_2 + 0.02f; // small epsilon to hide face seems

            for (uint32_t i = 0; i < GetSliceCount(); i++)
            {
                m_matrix_projection[i] = Matrix::CreatePerspectiveFieldOfViewLH(fov_y_radians, aspect_ratio, m_range, 0.05f);
                m_frustums[i]          = Frustum(m_matrix_view[i], m_matrix_projection[i]);
            }
        }
    }

    void Light::UpdateBoundingBox()
    {
        const Vector3 position = GetEntity()->GetPosition();

        if (m_light_type == LightType::Point)
        {
            const float radius = m_range;
            m_bounding_box = math::BoundingBox(
                position - Vector3(radius, radius, radius),
                position + Vector3(radius, radius, radius)
            );
        }
        else if (m_light_type == LightType::Spot)
        {
            const float opposite = m_range * tan(m_angle_rad);

            const Vector3 pos_tip    = position;
            const Vector3 pos_center = pos_tip    + GetEntity()->GetForward() * m_range;
            const Vector3 pos_up     = pos_center + GetEntity()->GetUp()      * opposite;
            const Vector3 pos_down   = pos_center + GetEntity()->GetDown()    * opposite;
            const Vector3 pos_right  = pos_center + GetEntity()->GetRight()   * opposite;
            const Vector3 pos_left   = pos_center + GetEntity()->GetLeft()    * opposite;

            Vector3 min = pos_tip;
            Vector3 max = pos_tip;

            auto expand = [&](const Vector3& p)
            {
                min = Vector3::Min(min, p);
                max = Vector3::Max(max, p);
            };

            expand(pos_center);
            expand(pos_up);
            expand(pos_down);
            expand(pos_right);
            expand(pos_left);

            m_bounding_box = math::BoundingBox(min, max);
        }
        else if (m_light_type == LightType::Area)
        {
            // area lights emit in a hemisphere so the influence volume fans out with distance,
            // use the range as the lateral spread at the far end to avoid premature frustum culling
            const float half_width  = m_area_width * 0.5f;
            const float half_height = m_area_height * 0.5f;

            const Vector3 right   = GetEntity()->GetRight();
            const Vector3 up      = GetEntity()->GetUp();
            const Vector3 forward = GetEntity()->GetForward();

            Vector3 min = position;
            Vector3 max = position;

            auto expand = [&](const Vector3& p)
            {
                min = Vector3::Min(min, p);
                max = Vector3::Max(max, p);
            };

            // near end: the area light rectangle itself
            expand(position + right * half_width + up * half_height);
            expand(position - right * half_width + up * half_height);
            expand(position + right * half_width - up * half_height);
            expand(position - right * half_width - up * half_height);

            // far end: expand laterally by range to cover the hemispheric spread
            const Vector3 far_center = position + forward * m_range;
            expand(far_center + right * m_range + up * m_range);
            expand(far_center - right * m_range + up * m_range);
            expand(far_center + right * m_range - up * m_range);
            expand(far_center - right * m_range - up * m_range);

            m_bounding_box = math::BoundingBox(min, max);
        }
        else // directional
        {
            m_bounding_box = math::BoundingBox::Infinite;
        }
    }

    namespace
    {
        // shared helper, returns true for directional or when the light is within the given distance from the camera
        bool is_within_distance(const Light* light, float distance_meters)
        {
            if (light->GetLightType() == LightType::Directional)
            {
                return true;
            }

            Camera* camera = World::GetCamera();
            if (!camera)
            {
                return false;
            }

            const Vector3 light_pos  = light->GetEntity()->GetPosition();
            const Vector3 camera_pos = camera->GetEntity()->GetPosition();
            const float distance_squared = Vector3::DistanceSquared(light_pos, camera_pos);
            return distance_squared <= distance_meters * distance_meters;
        }
    }

    bool Light::IsActiveByDistance() const
    {
        return is_within_distance(this, m_draw_distance);
    }

    bool Light::IsShadowEffective() const
    {
        if (!(m_flags & static_cast<uint32_t>(LightFlags::Shadows)))
        {
            return false;
        }

        return is_within_distance(this, m_distance_shadows);
    }

    bool Light::IsVolumetricEffective() const
    {
        if (!(m_flags & static_cast<uint32_t>(LightFlags::Volumetric)))
        {
            return false;
        }

        return is_within_distance(this, m_distance_volumetric);
    }

    bool Light::IsInViewFrustum(Render* renderable, const uint32_t array_index) const
    {
        const BoundingBox& bounding_box = renderable->GetBoundingBox();
        const Vector3 center            = bounding_box.GetCenter();
        const Vector3 extents           = bounding_box.GetExtents();
        const bool ignore_depth         = m_light_type == LightType::Directional; // orthographic

        return m_frustums[array_index].IsVisible(center, extents, ignore_depth);
    }
}
