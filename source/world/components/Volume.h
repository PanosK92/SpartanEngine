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

//= includes ===================================
#include "Component.h"
#include "../../math/BoundingBox.h"
#include "../AudioRegion.h"
#include <unordered_map>
#include <string>
//==============================================

namespace spartan
{
    class Entity;

    // a box that overrides cvars and enables reverb while the camera or an audio source is inside it
    class Volume : public Component
    {
    public:
        Volume(Entity* entity);
        ~Volume() = default;

        //= COMPONENT =====================
        void Tick() override;
        void Save(pugi::xml_node& node) override;
        void Load(pugi::xml_node& node) override;
        //=================================

        // box
        const math::BoundingBox& GetBoundingBox() const   { return m_bounding_box; }
        void SetBoundingBox(const math::BoundingBox& box) { m_bounding_box = box; }

        // options (use cvar names like "r.bloom", "r.fog" as keys)
        void SetOption(const char* name, float value);
        void RemoveOption(const char* name);
        float GetOption(const char* name) const;
        const std::unordered_map<std::string, float>& GetOptions() const { return m_options; }

        // audio reverb - applied to any audio source inside this volume
        bool GetReverbEnabled() const             { return m_reverb_enabled; }
        void SetReverbEnabled(const bool enabled) { m_reverb_enabled = enabled; }

        // Optional footprint for listener-driven ambience. Render/reverb bounds stay unchanged.
        float GetAudioWeight(const math::Vector3& listener) const;
        const std::vector<audio_region::Point>& GetAudioPolygon() const { return m_audio_polygon; }
        void SetAudioPolygon(const std::vector<audio_region::Point>& points) { m_audio_polygon = points; }
        float GetAudioFadeDistance() const { return m_audio_fade_distance; }
        void SetAudioFadeDistance(float value) { m_audio_fade_distance = std::isfinite(value) ? std::clamp(value, 0.01f, 10000.0f) : 50.0f; }
        bool GetAudioBoundaryOnly() const { return m_audio_boundary_only; }
        void SetAudioBoundaryOnly(bool value) { m_audio_boundary_only = value; }
        const std::string& GetAudioGroup() const { return m_audio_group; }
        void SetAudioGroup(const std::string& value) { m_audio_group = value; }

    private:
        // the shape of the volume
        math::BoundingBox m_bounding_box;

        // the user defined overrides
        std::unordered_map<std::string, float> m_options;

        // audio reverb
        bool m_reverb_enabled = false;
        std::vector<audio_region::Point> m_audio_polygon;
        float m_audio_fade_distance = 50.0f;
        bool m_audio_boundary_only = false;
        std::string m_audio_group;
    };
}
