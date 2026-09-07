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
#include "Volume.h"
#include "Render.h"
#include "../Entity.h"
#include "../../core/Engine.h"
#include "../../rendering/Renderer.h"
SP_WARNINGS_OFF
#include "../io/pugixml.hpp"
SP_WARNINGS_ON
//===================================

//= NAMESPACES ===============
using namespace spartan::math;
using namespace std;
//============================

namespace spartan
{
    Volume::Volume(Entity* entity) : Component(entity)
    {
        // if the entity has a render component, match the volume to its mesh-space bounding box
        // (not the world-space one, since the volume transforms by the entity matrix itself)
        if (Render* render = entity->GetComponent<Render>())
        {
            m_bounding_box = render->GetBoundingBoxMesh();
        }
        else
        {
            m_bounding_box = BoundingBox::Unit;
        }

        // register attributes for copy/paste and cloning
        SP_REGISTER_ATTRIBUTE_GET_SET(GetBoundingBox, SetBoundingBox, math::BoundingBox);
        SP_REGISTER_ATTRIBUTE_GET_SET(GetReverbEnabled, SetReverbEnabled, bool);
        SP_REGISTER_ATTRIBUTE_GET_SET(GetAudioPolygon, SetAudioPolygon, std::vector<audio_region::Point>);
        SP_REGISTER_ATTRIBUTE_GET_SET(GetAudioFadeDistance, SetAudioFadeDistance, float);
        SP_REGISTER_ATTRIBUTE_GET_SET(GetAudioBoundaryOnly, SetAudioBoundaryOnly, bool);
        SP_REGISTER_ATTRIBUTE_GET_SET(GetAudioGroup, SetAudioGroup, std::string);
    }

    void Volume::Tick()
    {
        // only draw in editor mode (not playing)
        if (Engine::IsFlagSet(EngineMode::Playing))
        {
            return;
        }

        // transform the bounding box by the entity's transform matrix
        const Matrix& entity_matrix       = GetEntity()->GetMatrix();
        const BoundingBox transformed_box = m_bounding_box * entity_matrix;

        // draw the volume using the renderer
        Renderer::DrawBox(transformed_box);
        for (size_t i = 0; i < m_audio_polygon.size(); ++i)
        {
            const auto& a = m_audio_polygon[i];
            const auto& b = m_audio_polygon[(i + 1) % m_audio_polygon.size()];
            Renderer::DrawLine(entity_matrix * Vector3(a.x, 0.0f, a.z), entity_matrix * Vector3(b.x, 0.0f, b.z));
        }
    }

    float Volume::GetAudioWeight(const Vector3& listener) const
    {
        const Vector3 p = GetEntity()->GetMatrix().Inverted() * listener;
        const Vector3& lo = m_bounding_box.GetMin();
        const Vector3& hi = m_bounding_box.GetMax();
        const float vertical = audio_region::smooth(1.0f - std::max({lo.y - p.y, p.y - hi.y, 0.0f}) / m_audio_fade_distance);
        if (vertical == 0.0f) return 0.0f;
        float distance;
        if (m_audio_polygon.size() >= 3)
        {
            // Reject distant listeners before walking a coastline with hundreds of segments.
            if (p.x < lo.x - m_audio_fade_distance || p.x > hi.x + m_audio_fade_distance ||
                p.z < lo.z - m_audio_fade_distance || p.z > hi.z + m_audio_fade_distance) return 0.0f;
            distance = audio_region::signed_distance(m_audio_polygon, {p.x, p.z});
        }
        else
        {
            const float dx = std::max({lo.x - p.x, p.x - hi.x, 0.0f});
            const float dz = std::max({lo.z - p.z, p.z - hi.z, 0.0f});
            distance = dx > 0.0f || dz > 0.0f ? -std::sqrt(dx * dx + dz * dz) : std::min({p.x - lo.x, hi.x - p.x, p.z - lo.z, hi.z - p.z});
        }
        return vertical * audio_region::weight(distance, m_audio_fade_distance, m_audio_boundary_only);
    }

    void Volume::Save(pugi::xml_node& node)
    {
        // bounding box
        const Vector3& bb_min = m_bounding_box.GetMin();
        const Vector3& bb_max = m_bounding_box.GetMax();
        node.append_attribute("bb_min_x") = bb_min.x;
        node.append_attribute("bb_min_y") = bb_min.y;
        node.append_attribute("bb_min_z") = bb_min.z;
        node.append_attribute("bb_max_x") = bb_max.x;
        node.append_attribute("bb_max_y") = bb_max.y;
        node.append_attribute("bb_max_z") = bb_max.z;

        // options
        pugi::xml_node options_node = node.append_child("Options");
        for (const auto& [name, value] : m_options)
        {
            pugi::xml_node option_node = options_node.append_child("Option");
            option_node.append_attribute("name")  = name.c_str();
            option_node.append_attribute("value") = value;
        }

        // audio reverb
        node.append_attribute("reverb_enabled") = m_reverb_enabled;
        node.append_attribute("audio_fade_distance") = m_audio_fade_distance;
        node.append_attribute("audio_boundary_only") = m_audio_boundary_only;
        node.append_attribute("audio_group") = m_audio_group.c_str();
        if (!m_audio_polygon.empty())
        {
            auto polygon = node.append_child("AudioPolygon");
            for (const auto& point : m_audio_polygon)
            {
                auto vertex = polygon.append_child("Point");
                vertex.append_attribute("x") = point.x;
                vertex.append_attribute("z") = point.z;
            }
        }
    }

    void Volume::Load(pugi::xml_node& node)
    {
        // bounding box
        Vector3 bb_min, bb_max;
        bb_min.x = node.attribute("bb_min_x").as_float(-0.5f);
        bb_min.y = node.attribute("bb_min_y").as_float(-0.5f);
        bb_min.z = node.attribute("bb_min_z").as_float(-0.5f);
        bb_max.x = node.attribute("bb_max_x").as_float(0.5f);
        bb_max.y = node.attribute("bb_max_y").as_float(0.5f);
        bb_max.z = node.attribute("bb_max_z").as_float(0.5f);
        m_bounding_box = BoundingBox(bb_min, bb_max);

        // options
        m_options.clear();
        pugi::xml_node options_node = node.child("Options");
        if (options_node)
        {
            for (pugi::xml_node option_node : options_node.children("Option"))
            {
                string name  = option_node.attribute("name").as_string();
                float value  = option_node.attribute("value").as_float(0.0f);
                if (!name.empty())
                {
                    m_options[name] = value;
                }
            }
        }

        // audio reverb
        m_reverb_enabled = node.attribute("reverb_enabled").as_bool(false);
        SetAudioFadeDistance(node.attribute("audio_fade_distance").as_float(50.0f));
        m_audio_boundary_only = node.attribute("audio_boundary_only").as_bool(false);
        m_audio_group = node.attribute("audio_group").as_string();
        m_audio_polygon.clear();
        for (auto point : node.child("AudioPolygon").children("Point"))
            m_audio_polygon.push_back({point.attribute("x").as_float(), point.attribute("z").as_float()});
    }

    void Volume::SetOption(const char* name, float value)
    {
        m_options[name] = value;
    }

    void Volume::RemoveOption(const char* name)
    {
        m_options.erase(name);
    }

    float Volume::GetOption(const char* name) const
    {
        // try to find the specific override
        auto it = m_options.find(name);
        if (it != m_options.end())
        {
            return it->second;
        }

        return 0.0f;
    }

}
