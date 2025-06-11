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

#pragma once

//= INCLUDES =====================
#include <array>
#include <memory>
#include "Component.h"
#include "Renderable.h"
#include "../../Math/Matrix.h"
#include "../../Math/Frustum.h"
#include "../../Rendering/Color.h"
//================================

namespace spartan
{
    class Camera;
    class RHI_Texture;

    enum class LightType
    {
        Directional,
        Point,
        Spot,
        Max
    };

    enum class LightIntensity
    {
        bulb_stadium,    // intense light used in stadiums for sports events, comparable to sunlight
        bulb_500_watt,   // a very bright domestic bulb or small industrial light
        bulb_150_watt,   // a bright domestic bulb, equivalent to an old-school incandescent bulb
        bulb_100_watt,   // a typical bright domestic bulb
        bulb_60_watt,    // a medium intensity domestic bulb
        bulb_25_watt,    // a low intensity domestic bulb, used for mood lighting or as a night light
        bulb_flashlight, // light emitted by an average flashlight, portable and less intense
        black_hole,      // no light emitted
        custom           // custom intensity
    };

    enum LightFlags : uint32_t
    {
        Shadows            = 1U << 0,
        ShadowsScreenSpace = 1U << 1,
        Volumetric         = 1U << 2,
        DayNightCycle      = 1U << 3, // only affects directional lights
        ShadowDirty        = 1U << 4,
    };

    class Light : public Component
    {
    public:
        Light(Entity* entity);
        ~Light() = default;

        //= COMPONENT ================================
        void OnTick() override;
        void Serialize(FileStream* stream) override;
        void Deserialize(FileStream* stream) override;
        //============================================

        // flags
        bool GetFlag(const LightFlags flag) { return m_flags & flag; }
        void SetFlag(const LightFlags flag, const bool enable = true);

        // type
        const LightType GetLightType() const { return m_light_type; }
        void SetLightType(LightType type);

        // color
        void SetTemperature(const float temperature_kelvin);
        float GetTemperature() const { return m_temperature_kelvin; }
        void SetColor(const Color& rgb);
        const Color& GetColor() const { return m_color_rgb; }

        // intensity
        void SetIntensity(const float lumens_lux);
        void SetIntensity(const LightIntensity intensity);
        float GetIntensityLumens() const    { return m_intensity_lumens_lux; }
        LightIntensity GetIntensity() const { return m_intensity; }
        float GetIntensityWatt() const;

        // bias
        static float GetBias()            { return -0.003f; }
        static float GetBiasSlopeScaled() { return -2.0f; }

        // range
        void SetRange(float range);
        auto GetRange() const { return m_range; }

        // angle
        void SetAngle(float angle_rad);
        auto GetAngle() const { return m_angle_rad; }

        // matrices
        const math::Matrix& GetViewMatrix(uint32_t index) const       { return m_matrix_view[index]; }
        const math::Matrix& GetProjectionMatrix(uint32_t index) const { return m_matrix_projection[index]; }

        // textures
        RHI_Texture* GetDepthTexture() const { return m_texture_depth.get(); }

        // frustum
        bool IsInViewFrustum(Renderable* renderable, const uint32_t array_index, const uint32_t instance_group_index = 0) const;

        // index
        void SetIndex(const uint32_t index) { m_index = index; }
        uint32_t GetIndex() const           { return m_index; }

        // screen space shadows slice index
        void SetScreenSpaceShadowsSliceIndex(const uint32_t index) { m_index = index; }
        uint32_t GetScreenSpaceShadowsSliceIndex() const           { return m_index; }

    private:
        void UpdateMatrices();
        void ComputeViewMatrix();
        void ComputeProjectionMatrix();

        // intensity
        LightIntensity m_intensity   = LightIntensity::bulb_500_watt;
        float m_intensity_lumens_lux = 2600.0f;

        // shadows
        std::shared_ptr<RHI_Texture> m_texture_depth;
        std::array<math::Frustum, 2> m_frustums;
        std::array<math::Matrix, 2> m_matrix_view;
        std::array<math::Matrix, 2> m_matrix_projection;

        // misc
        uint32_t m_flags           = 0;
        LightType m_light_type     = LightType::Max;
        Color m_color_rgb          = Color::standard_black;
        float m_temperature_kelvin = 0.0f;
        float m_range              = 32.0f;
        float m_angle_rad          = math::deg_to_rad * 30.0f;
        uint32_t m_index           = 0;
        float m_shadow_extent_near = 0.0f;
        float m_shadow_extent_far  = 0.0f;
    };
}
