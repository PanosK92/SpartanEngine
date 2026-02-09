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
#include "ParticleSystem.h"
#include "../Entity.h"
SP_WARNINGS_OFF
#include "../IO/pugixml.hpp"
SP_WARNINGS_ON
//===================================

//= NAMESPACES =====
using namespace std;
//==================

namespace spartan
{
    ParticleSystem::ParticleSystem(Entity* entity) : Component(entity)
    {
        SP_REGISTER_ATTRIBUTE_VALUE_VALUE(m_max_particles, uint32_t);
        SP_REGISTER_ATTRIBUTE_VALUE_VALUE(m_emission_rate, float);
        SP_REGISTER_ATTRIBUTE_VALUE_VALUE(m_lifetime, float);
        SP_REGISTER_ATTRIBUTE_VALUE_VALUE(m_start_speed, float);
        SP_REGISTER_ATTRIBUTE_VALUE_VALUE(m_start_size, float);
        SP_REGISTER_ATTRIBUTE_VALUE_VALUE(m_end_size, float);
        SP_REGISTER_ATTRIBUTE_VALUE_VALUE(m_start_color, Color);
        SP_REGISTER_ATTRIBUTE_VALUE_VALUE(m_end_color, Color);
        SP_REGISTER_ATTRIBUTE_VALUE_VALUE(m_gravity_modifier, float);
        SP_REGISTER_ATTRIBUTE_VALUE_VALUE(m_emission_radius, float);
    }

    void ParticleSystem::Tick()
    {
        // nothing to do on the cpu side; the gpu handles emission, simulation, and rendering
        // the renderer reads the component properties directly when building the emitter params buffer
    }

    void ParticleSystem::Save(pugi::xml_node& node)
    {
        node.append_attribute("max_particles")   = m_max_particles;
        node.append_attribute("emission_rate")   = m_emission_rate;
        node.append_attribute("lifetime")        = m_lifetime;
        node.append_attribute("start_speed")     = m_start_speed;
        node.append_attribute("start_size")      = m_start_size;
        node.append_attribute("end_size")        = m_end_size;
        node.append_attribute("start_color_r")   = m_start_color.r;
        node.append_attribute("start_color_g")   = m_start_color.g;
        node.append_attribute("start_color_b")   = m_start_color.b;
        node.append_attribute("start_color_a")   = m_start_color.a;
        node.append_attribute("end_color_r")     = m_end_color.r;
        node.append_attribute("end_color_g")     = m_end_color.g;
        node.append_attribute("end_color_b")     = m_end_color.b;
        node.append_attribute("end_color_a")     = m_end_color.a;
        node.append_attribute("gravity_modifier") = m_gravity_modifier;
        node.append_attribute("emission_radius") = m_emission_radius;
    }

    void ParticleSystem::Load(pugi::xml_node& node)
    {
        m_max_particles   = node.attribute("max_particles").as_uint(10000);
        m_emission_rate   = node.attribute("emission_rate").as_float(100.0f);
        m_lifetime        = node.attribute("lifetime").as_float(3.0f);
        m_start_speed     = node.attribute("start_speed").as_float(5.0f);
        m_start_size      = node.attribute("start_size").as_float(0.05f);
        m_end_size        = node.attribute("end_size").as_float(0.0f);
        m_start_color.r   = node.attribute("start_color_r").as_float(1.0f);
        m_start_color.g   = node.attribute("start_color_g").as_float(0.8f);
        m_start_color.b   = node.attribute("start_color_b").as_float(0.4f);
        m_start_color.a   = node.attribute("start_color_a").as_float(1.0f);
        m_end_color.r     = node.attribute("end_color_r").as_float(1.0f);
        m_end_color.g     = node.attribute("end_color_g").as_float(0.2f);
        m_end_color.b     = node.attribute("end_color_b").as_float(0.0f);
        m_end_color.a     = node.attribute("end_color_a").as_float(0.0f);
        m_gravity_modifier = node.attribute("gravity_modifier").as_float(-1.0f);
        m_emission_radius = node.attribute("emission_radius").as_float(0.5f);
    }
}
