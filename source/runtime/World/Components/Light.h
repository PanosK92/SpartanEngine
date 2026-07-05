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

#pragma once

//= INCLUDES =====================
#include <array>
#include "Component.h"
#include "Render.h"
#include "../../Math/Matrix.h"
#include "../../Math/Frustum.h"
#include "../../Math/Rectangle.h"
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
        Area,
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

    enum class LightIntensityUnit
    {
        Lux,
        Lumens
    };

    enum class LightPreset
    {
        dawn,        // sunrise - early morning with warm orange glow
        day,         // bright midday sun - direct sunlight at peak intensity
        dusk,        // sunset - evening with warm golden tones
        night,       // nighttime with soft moonlight
        david_lynch, // dreamy late afternoon - low sun, soft eerie light
        custom       // custom settings
    };

    enum LightFlags : uint32_t
    {
        Shadows            = 1U << 0,
        ShadowsScreenSpace = 1U << 1,
        Volumetric         = 1U << 2,
        DayNightCycle      = 1U << 3, // rotates the light according to the time of day (game time)
        RealTimeCycle      = 1U << 4  // derives the time of day from the real world time
    };

    class Light : public Component
    {
    public:
        Light(Entity* entity);
        ~Light();

        //= COMPONENT ================================
        void Tick() override;
        void Save(pugi::xml_node& node) override;
        void Load(pugi::xml_node& node) override;
        //============================================

        static void RegisterForScripting(sol::state_view State);
        sol::reference AsLua(sol::state_view state) override;

        // flags
        uint32_t GetFlags() const                    { return m_flags; }
        bool GetFlag(const LightFlags flag)          { return m_flags & flag; }
        void SetFlag(const LightFlags flag, const bool enable = true);

        // type
        const LightType GetLightType() const { return m_light_type; }
        void SetLightType(LightType type);

        // color
        void SetTemperature(const float temperature_kelvin);
        float GetTemperature() const { return m_temperature_kelvin; }
        void SetColor(const Color& rgb);
        const Color& GetColor() const { return m_color_rgb; }
        // shading color, atmospheric sun transmittance for directional lights, authored color otherwise
        Color GetColorEffective() const;

        // intensity
        void SetIntensity(const float photometric_intensity);
        void SetIntensity(const LightIntensity intensity);
        float GetIntensityPhotometric() const { return m_intensity_photometric; }
        float GetIntensityLumens() const      { return m_intensity_photometric; } // legacy alias
        LightIntensity GetIntensity() const   { return m_intensity; }
        LightIntensityUnit GetIntensityUnit() const;
        float GetIntensityRadiometric() const;
        float GetIntensityWatt() const { return GetIntensityRadiometric(); } // legacy alias

        // preset
        void SetPreset(const LightPreset preset);
        LightPreset GetPreset() const { return m_preset; }

        // range
        void SetRange(float range);
        auto GetRange() const { return m_range; }

        // angle
        void SetAngle(float angle_rad);
        auto GetAngle() const { return m_angle_rad; }

        // area light dimensions
        void SetAreaWidth(float width);
        float GetAreaWidth() const { return m_area_width; }
        void SetAreaHeight(float height);
        float GetAreaHeight() const { return m_area_height; }
        // sizes the area emitter rectangle to match the entity's own mesh extents along its right and up axes
        bool FitToMesh();

        // matrices
        const math::Matrix GetViewProjectionMatrix(uint32_t index) const { return m_matrix_view[index] * m_matrix_projection[index]; }

        // frustum
        bool IsInViewFrustum(Render* renderable, const uint32_t array_index) const;

        // index
        void SetIndex(const uint32_t index) { m_index = index; }
        uint32_t GetIndex() const           { return m_index; }

        // screen space shadows slice index
        void SetScreenSpaceShadowsSliceIndex(const uint32_t index) { m_screen_space_shadows_slice_index = index; }
        uint32_t GetScreenSpaceShadowsSliceIndex() const           { return m_screen_space_shadows_slice_index; }

        // draw distance, beyond this the light is fully culled
        void SetDrawDistance(const float distance) { m_draw_distance = distance; }
        float GetDrawDistance() const              { return m_draw_distance; }

        // shadow distance, beyond this shadow maps stop being rendered for this light
        void SetShadowDistance(const float distance) { m_distance_shadows = distance; }
        float GetShadowDistance() const              { return m_distance_shadows; }

        // volumetric distance, beyond this volumetric scattering stops being computed
        void SetVolumetricDistance(const float distance) { m_distance_volumetric = distance; }
        float GetVolumetricDistance() const              { return m_distance_volumetric; }

        // distance based effective state, directional lights are always effective
        bool IsActiveByDistance() const;
        bool IsShadowEffective() const;
        bool IsVolumetricEffective() const;

        // misc
        bool NeedsSkysphereUpdate() const;
        uint32_t GetSliceCount() const;

        // atlas
        math::Vector2 GetAtlasOffset(uint32_t slice) const { return m_atlas_offsets[slice]; }
        math::Vector2 GetAtlasScale(uint32_t slice) const  { return m_atlas_scales[slice]; }
        const math::Rectangle& GetAtlasRectangle(uint32_t slice) const { return m_atlas_rectangles[slice]; }
        void SetAtlasRectangle(uint32_t slice, const math::Rectangle& rectangle);
        void ClearAtlasRectangles();
        math::BoundingBox GetBoundingBox() const { return m_bounding_box; }

    private:
        void UpdateMatrices();
        void UpdateViewMatrix();
        void UpdateProjectionMatrix();
        void UpdateBoundingBox();

        // properties
        LightIntensity m_intensity       = LightIntensity::bulb_500_watt;
        float m_intensity_photometric    = 2600.0f;
        uint32_t m_flags                 = 0;
        LightType m_light_type           = LightType::Max;
        Color m_color_rgb                = Color::standard_black;
        float m_temperature_kelvin       = 0.0f;
        LightPreset m_preset             = LightPreset::custom;
        float m_range                    = 32.0f;
        float m_angle_rad                = math::deg_to_rad * 30.0f;
        float m_area_width               = 1.0f;  // area light width in meters
        float m_area_height              = 1.0f;  // area light height in meters
        uint32_t m_index                 = 0;
        uint32_t m_screen_space_shadows_slice_index = 0;
        math::BoundingBox m_bounding_box = math::BoundingBox::Zero;
        math::Vector3 m_far_cascade_min  = math::Vector3::Zero;
        math::Vector3 m_far_cascade_max  = math::Vector3::Zero;
        bool m_is_active_previous_frame  = false;
        float m_draw_distance            = 512.0f; // max distance at which light will affect objects (meters)
        float m_distance_shadows         = 64.0f;  // max distance at which shadow maps are rendered (meters)
        float m_distance_volumetric      = 32.0f;  // max distance at which volumetric scattering is computed (meters)

        // matrices/frustums per slice/face/cascade
        std::array<math::Frustum, 6> m_frustums;
        std::array<math::Matrix, 6> m_matrix_view;
        std::array<math::Matrix, 6> m_matrix_projection;

        // atlas entries per slice/face/cascade
        std::array<math::Rectangle, 6> m_atlas_rectangles;
        std::array<math::Vector2, 6> m_atlas_offsets;
        std::array<math::Vector2, 6> m_atlas_scales;
    };
}
