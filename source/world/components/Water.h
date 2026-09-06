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

//= INCLUDES ========
#include "Component.h"
#include <memory>
#include <algorithm>
#include <cmath>
//===================

namespace spartan
{
    class Mesh;
    class Material;

    // fft ocean surface, simulates the spectrum on the gpu and renders a camera-centered clipmap
    class Water : public Component
    {
    public:
        Water(Entity* entity);
        ~Water();

        // component
        void Initialize() override;
        void Tick() override;
        void Remove() override;
        void Save(pugi::xml_node& node) override;
        void Load(pugi::xml_node& node) override;

        // Everyday controls. Wind supplies the wave spectrum; size changes its height.
        float GetWaveSize() const { return m_amplitude * m_displacement_scale; }
        void SetWaveSize(float size)
        {
            m_amplitude = Sanitize(size, 0.0f, 3.0f, 1.0f);
            m_displacement_scale = 1.0f;
            m_normal_strength = 1.0f;
            m_choppiness = 1.2f;
            m_cascade_count = cascade_max;
            PushToRenderer(true);
        }

        float GetClarity() const { return 1.0f - m_turbidity / 4.0f; }
        void SetClarity(float clarity)
        {
            m_turbidity = (1.0f - Sanitize(clarity, 0.0f, 1.0f, 0.75f)) * 4.0f;
            m_caustics_intensity = 1.0f;
        }

        // simulation parameters
        uint32_t GetCascadeCount() const
        {
            return m_cascade_count;
        }

        void SetCascadeCount(uint32_t count)
        {
            m_cascade_count =
                count < 1 ?
                1 :
                (
                    count > cascade_max ?
                    cascade_max :
                    count
                );
            PushToRenderer(true);
        }

        const float* GetCascadeLengths() const
        {
            return m_cascade_length;
        }

        float GetAmplitude() const
        {
            return m_amplitude;
        }

        void SetAmplitude(float amplitude)
        {
            m_amplitude = Sanitize(amplitude, 0.0f, 10.0f, 1.0f);
            PushToRenderer(true);
        }

        float GetChoppiness() const
        {
            return m_choppiness;
        }

        void SetChoppiness(float choppiness)
        {
            m_choppiness = Sanitize(choppiness, 0.0f, 4.0f, 1.2f);
            PushToRenderer(true);
        }

        float GetDisplacementScale() const
        {
            return m_displacement_scale;
        }

        void SetDisplacementScale(float scale)
        {
            m_displacement_scale = Sanitize(scale, 0.0f, 4.0f, 1.0f);
            PushToRenderer(true);
        }

        float GetNormalStrength() const
        {
            return m_normal_strength;
        }

        void SetNormalStrength(float strength)
        {
            m_normal_strength = Sanitize(strength, 0.0f, 4.0f, 1.0f);
            PushToRenderer(false);
        }

        float GetSeaLevel() const
        {
            return m_sea_level;
        }

        void SetSeaLevel(float level)
        {
            m_sea_level = Sanitize(level, -1000.0f, 1000.0f, 0.0f);
            PushToRenderer(false);
        }

        float GetTurbidity() const
        {
            return m_turbidity;
        }

        void SetTurbidity(float turbidity)
        {
            m_turbidity = Sanitize(turbidity, 0.0f, 4.0f, 1.0f);
        }

        float GetCausticsIntensity() const
        {
            return m_caustics_intensity;
        }

        void SetCausticsIntensity(float intensity)
        {
            m_caustics_intensity = Sanitize(intensity, 0.0f, 4.0f, 1.0f);
        }

    private:
        static constexpr uint32_t cascade_max = 4;

        static float Sanitize(float value, float low, float high, float fallback)
        {
            return std::isfinite(value) ? std::clamp(value, low, high) : fallback;
        }

        void BuildSurface();
        void PushToRenderer(bool spectrum_dirty);

        // four cascades from swells to microwaves, the spectrum is normalized so wind speed alone dictates the sea state
        uint32_t m_cascade_count              = cascade_max;
        float m_cascade_length[cascade_max]   = { 1000.0f, 250.0f, 60.0f, 15.0f };
        float m_amplitude           = 1.0f;
        float m_choppiness          = 1.2f; // automatic crest sharpening
        float m_displacement_scale  = 1.0f;
        float m_normal_strength     = 1.0f;
        float m_sea_level           = 0.0f;

        // water body optics, read by the renderer each frame, they do not touch the spectrum
        float m_turbidity           = 1.0f; // suspended particles, affects transmission and underwater lighting
        float m_caustics_intensity  = 1.0f; // brightness of the sun caustics on submerged geometry

        // clipmap geometry
        // near levels stay fine for waves, a flat skirt past the outer ring hides the rim from altitude
        uint32_t m_clipmap_resolution = 128;
        uint32_t m_clipmap_levels     = 8;
        float m_clipmap_base_cell     = 0.5f; // meters at the finest ring around the camera
        float m_horizon_extent        = 150000.0f; // flat skirt half-extent in meters

        // owned resources, kept alive beyond any single frame
        std::shared_ptr<Mesh> m_mesh         = nullptr;
        std::shared_ptr<Material> m_material = nullptr;
    };
}
