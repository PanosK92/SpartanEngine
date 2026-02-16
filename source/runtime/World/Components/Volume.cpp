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

#include <tracy/Tracy.hpp>

#include "Renderable.h"
#include "../Entity.h"
#include "../../Core/Engine.h"
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
    Volume::Volume(Entity* entity) : Component(entity)
    {
        // if the entity has a renderable, match the volume to its mesh-space bounding box
        // (not the world-space one, since the volume transforms by the entity matrix itself)
        if (Renderable* renderable = entity->GetComponent<Renderable>())
        {
            m_bounding_box = renderable->GetBoundingBoxMesh();
        }
        else
        {
            m_bounding_box = BoundingBox::Unit;
        }

        // register attributes for copy/paste and cloning
        SP_REGISTER_ATTRIBUTE_GET_SET(GetReverbEnabled, SetReverbEnabled, bool);
    }

    void Volume::Tick()
    {
        ZoneScoped;

        // only draw in editor mode (not playing)
        if (Engine::IsFlagSet(EngineMode::Playing))
            return;

        // transform the bounding box by the entity's transform matrix
        const Matrix& entity_matrix       = GetEntity()->GetMatrix();
        const BoundingBox transformed_box = m_bounding_box * entity_matrix;

        // draw the volume using the renderer
        Renderer::DrawBox(transformed_box);
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
