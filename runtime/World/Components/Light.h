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

#pragma once

//= INCLUDES =========================
#include <array>
#include <memory>
#include "Component.h"
#include "Renderable.h"
#include "../../Math/Vector4.h"
#include "../../Math/Vector3.h"
#include "../../Math/Matrix.h"
#include "../../RHI/RHI_Definitions.h"
#include "../../Math/Frustum.h"
#include "../../Rendering/Color.h"
//====================================

namespace Spartan
{
    //= FWD DECLARATIONS =
    class Camera;
    //====================

    enum class LightType
    {
        Directional,
        Point,
        Spot
    };

    enum class LightIntensity
    {
        sky_sunlight_noon,            // Direct sunlight at noon, the brightest light
        sky_sunlight_morning_evening, // Direct sunlight at morning or evening, less intense than noon light
        sky_overcast_day,             // Light on an overcast day, considerably less intense than direct sunlight
        sky_twilight,                 // Light just after sunset, a soft and less intense light
        bulb_stadium,                 // Intense light used in stadiums for sports events, comparable to sunlight
        bulb_500_watt,                // A very bright domestic bulb or small industrial light
        bulb_150_watt,                // A bright domestic bulb, equivalent to an old-school incandescent bulb
        bulb_100_watt,                // A typical bright domestic bulb
        bulb_60_watt,                 // A medium intensity domestic bulb
        bulb_25_watt,                 // A low intensity domestic bulb, used for mood lighting or as a night light
        bulb_flashlight,              // Light emitted by an average flashlight, portable and less intense
        black_hole,                   // No light emitted
        custom                        // Custom intensity
    };

    class SP_CLASS Light : public Component
    {
    public:
        Light(std::weak_ptr<Entity> entity);
        ~Light() = default;

        //= COMPONENT ================================
        void OnInitialize() override;
        void OnTick() override;
        void Serialize(FileStream* stream) override;
        void Deserialize(FileStream* stream) override;
        //============================================

        // type
        const LightType GetLightType() const { return m_light_type; }
        void SetLightType(LightType type);

        // color
        void SetTemperature(const float temperature_kelvin);
        float GetTemperature() const { return m_temperature_kelvin; }
        void SetColor(const Color& rgb);
        const Color& GetColor() const { return m_color_rgb; }

        // intensity
        void SetIntensityLumens(const float lumens);
        void SetIntensity(const LightIntensity lumens);
        float GetIntensityLumens() const    { return m_intensity_lumens; }
        LightIntensity GetIntensity() const { return m_intensity; }
        float GetIntensityWatt(Camera* camera) const;

        // shadows
        bool GetShadowsEnabled() const { return m_shadows_enabled; }
        void SetShadowsEnabled(bool cast_shadows);
        bool GetShadowsTransparentEnabled() const { return m_shadows_transparent_enabled; }
        void SetShadowsTransparentEnabled(bool cast_transparent_shadows);

        // bias
        static float GetBias()            { return -0.004f; }
        static float GetBiasSlopeScaled() { return -2.0f; }
        void SetNormalBias(float value)   { m_bias_normal = value; }
        auto GetNormalBias() const        { return m_bias_normal; }

        // volumetric
        bool GetVolumetricEnabled() const             { return m_volumetric_enabled; }
        void SetVolumetricEnabled(bool is_volumetric) { m_volumetric_enabled = is_volumetric; }

        // range
        void SetRange(float range);
        auto GetRange() const { return m_range; }

        // angle
        void SetAngle(float angle_rad);
        auto GetAngle() const { return m_angle_rad; }

        // matrices
        const Math::Matrix& GetViewMatrix(uint32_t index) const;
        const Math::Matrix& GetProjectionMatrix(uint32_t index) const;

        // textures
        RHI_Texture* GetDepthTexture() const { return m_texture_depth.get(); }
        RHI_Texture* GetColorTexture() const { return m_texture_color.get(); }
        void CreateShadowMap();

        bool IsInViewFrustum(const Math::BoundingBox& bounding_box, const uint32_t index) const;
        bool IsInViewFrustum(Renderable* renderable, const uint32_t index) const;

    private:
        void ComputeViewMatrix();
        void ComputeProjectionMatrix();

        // intensity
        LightIntensity m_intensity = LightIntensity::bulb_500_watt;
        float m_intensity_lumens   = 2600.0f;

        // shadows
        bool m_shadows_enabled             = true;
        bool m_shadows_transparent_enabled = true;
        float m_bias_normal                = 0.0f;
        std::shared_ptr<RHI_Texture> m_texture_color;
        std::shared_ptr<RHI_Texture> m_texture_depth;
        std::array<Math::Frustum, 6> m_frustums;
        std::array<Math::Matrix, 6> m_matrix_view;
        std::array<Math::Matrix, 6> m_matrix_projection;

        // misc
        LightType m_light_type     = LightType::Directional;
        Color m_color_rgb          = Color::standard_black;;
        float m_temperature_kelvin = 0.0f;
        bool m_volumetric_enabled  = true;
        float m_range              = 0.0f;
        float m_angle_rad          = Math::Helper::DEG_TO_RAD * 30.0f;
        bool m_initialized         = false;

        // dirty checks
        bool m_is_dirty                     = true;
        Math::Matrix m_previous_camera_view = Math::Matrix::Identity;
    };
}
