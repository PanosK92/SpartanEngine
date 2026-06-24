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

        // simulation parameters
        uint32_t GetCascadeCount() const          { return m_cascade_count; }
        void SetCascadeCount(uint32_t count)      { m_cascade_count = count; PushToRenderer(); }
        float GetAmplitude() const                { return m_amplitude; }
        void SetAmplitude(float amplitude)        { m_amplitude = amplitude; PushToRenderer(); }
        float GetChoppiness() const               { return m_choppiness; }
        void SetChoppiness(float choppiness)      { m_choppiness = choppiness; PushToRenderer(); }
        float GetDisplacementScale() const        { return m_displacement_scale; }
        void SetDisplacementScale(float scale)    { m_displacement_scale = scale; PushToRenderer(); }
        float GetNormalStrength() const           { return m_normal_strength; }
        void SetNormalStrength(float strength)    { m_normal_strength = strength; PushToRenderer(); }
        float GetFoamCoverage() const             { return m_foam_coverage; }
        void SetFoamCoverage(float coverage)      { m_foam_coverage = coverage; PushToRenderer(); }
        float GetSeaLevel() const                 { return m_sea_level; }
        void SetSeaLevel(float level)             { m_sea_level = level; PushToRenderer(); }

    private:
        void BuildSurface();
        void PushToRenderer();

        // simulation parameters
        // four cascades spanning swells down to microwaves, each band-limited to its own range in the spectrum shader
        uint32_t m_cascade_count    = 4;
        float m_cascade_length[4]   = { 1000.0f, 250.0f, 60.0f, 15.0f };
        float m_amplitude           = 50.0f;
        float m_choppiness          = 2.5f;
        float m_displacement_scale  = 4.0f;
        float m_normal_strength     = 7.25f;
        float m_foam_coverage       = 0.5f;
        float m_sea_level           = 0.0f;

        // clipmap geometry
        uint32_t m_clipmap_resolution = 128;
        uint32_t m_clipmap_levels     = 8;
        float m_clipmap_base_cell     = 1.0f;

        // owned resources, kept alive beyond any single frame
        std::shared_ptr<Mesh> m_mesh         = nullptr;
        std::shared_ptr<Material> m_material = nullptr;
    };
}
