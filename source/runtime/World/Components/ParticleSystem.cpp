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
#include "../../RHI/RHI_Texture.h"
#include "../../Resource/ResourceCache.h"
SP_WARNINGS_OFF
#include <sol/sol.hpp>
#include "../IO/pugixml.hpp"
SP_WARNINGS_ON
//===================================

//= NAMESPACES =====
using namespace std;
using namespace spartan::math;
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
        SP_REGISTER_ATTRIBUTE_VALUE_VALUE(m_emission_direction, Vector3);
        SP_REGISTER_ATTRIBUTE_VALUE_VALUE(m_emission_cone_angle, float);
        SP_REGISTER_ATTRIBUTE_VALUE_VALUE(m_directional_blend, float);

        ApplyPreset(ParticlePreset::Fire);
    }

    void ParticleSystem::Tick()
    {
        // nothing to do on the cpu side; the gpu handles emission, simulation, and rendering
        // the renderer reads the component properties directly when building the emitter params buffer
    }

    void ParticleSystem::ApplyPreset(ParticlePreset preset)
    {
        m_preset = preset;

        switch (preset)
        {
            case ParticlePreset::Fire:
                m_max_particles   = 10000;
                m_emission_rate   = 500.0f;
                m_lifetime        = 2.0f;
                m_start_speed     = 2.0f;
                m_start_size      = 0.3f;
                m_end_size        = 0.0f;
                m_start_color     = Color(1.0f, 0.8f, 0.4f, 1.0f);
                m_end_color       = Color(1.0f, 0.2f, 0.0f, 0.0f);
                m_gravity_modifier = -0.5f;
                m_emission_radius = 0.3f;
                m_emission_direction = Vector3::Up;
                m_emission_cone_angle = 0.9f;
                m_directional_blend = 0.2f;
                break;

            case ParticlePreset::Smoke:
                m_max_particles   = 5000;
                m_emission_rate   = 150.0f;
                m_lifetime        = 6.0f;
                m_start_speed     = 1.5f;
                m_start_size      = 0.2f;
                m_end_size        = 1.5f;
                m_start_color     = Color(0.3f, 0.3f, 0.3f, 0.6f);
                m_end_color       = Color(0.1f, 0.1f, 0.1f, 0.0f);
                m_gravity_modifier = -0.2f;
                m_emission_radius = 0.4f;
                m_emission_direction = Vector3::Up;
                m_emission_cone_angle = 1.3f;
                m_directional_blend = 0.15f;
                break;

            case ParticlePreset::Steam:
                m_max_particles   = 3000;
                m_emission_rate   = 200.0f;
                m_lifetime        = 3.0f;
                m_start_speed     = 1.0f;
                m_start_size      = 0.15f;
                m_end_size        = 0.8f;
                m_start_color     = Color(0.9f, 0.9f, 0.95f, 0.4f);
                m_end_color       = Color(1.0f, 1.0f, 1.0f, 0.0f);
                m_gravity_modifier = -0.3f;
                m_emission_radius = 0.2f;
                m_emission_direction = Vector3::Up;
                m_emission_cone_angle = 1.1f;
                m_directional_blend = 0.2f;
                break;

            case ParticlePreset::Sparks:
                m_max_particles   = 5000;
                m_emission_rate   = 800.0f;
                m_lifetime        = 1.0f;
                m_start_speed     = 8.0f;
                m_start_size      = 0.04f;
                m_end_size        = 0.01f;
                m_start_color     = Color(1.0f, 0.9f, 0.5f, 1.0f);
                m_end_color       = Color(1.0f, 0.4f, 0.1f, 0.0f);
                m_gravity_modifier = -2.0f;
                m_emission_radius = 0.1f;
                m_emission_direction = Vector3::Up;
                m_emission_cone_angle = 0.45f;
                m_directional_blend = 0.8f;
                break;

            case ParticlePreset::Dust:
                m_max_particles   = 3000;
                m_emission_rate   = 100.0f;
                m_lifetime        = 5.0f;
                m_start_speed     = 0.5f;
                m_start_size      = 0.1f;
                m_end_size        = 0.4f;
                m_start_color     = Color(0.6f, 0.55f, 0.45f, 0.3f);
                m_end_color       = Color(0.5f, 0.45f, 0.35f, 0.0f);
                m_gravity_modifier = -0.05f;
                m_emission_radius = 1.0f;
                m_emission_direction = Vector3::Up;
                m_emission_cone_angle = 1.4f;
                m_directional_blend = 0.1f;
                break;

            case ParticlePreset::Snow:
                m_max_particles   = 20000;
                m_emission_rate   = 2000.0f;
                m_lifetime        = 8.0f;
                m_start_speed     = 0.3f;
                m_start_size      = 0.03f;
                m_end_size        = 0.03f;
                m_start_color     = Color(0.95f, 0.95f, 1.0f, 0.8f);
                m_end_color       = Color(0.9f, 0.9f, 1.0f, 0.0f);
                m_gravity_modifier = -0.3f;
                m_emission_radius = 10.0f;
                m_emission_direction = Vector3::Down;
                m_emission_cone_angle = 0.5f;
                m_directional_blend = 0.9f;
                break;

            case ParticlePreset::Rain:
                m_max_particles   = 30000;
                m_emission_rate   = 5000.0f;
                m_lifetime        = 2.0f;
                m_start_speed     = 15.0f;
                m_start_size      = 0.02f;
                m_end_size        = 0.02f;
                m_start_color     = Color(0.7f, 0.75f, 0.85f, 0.4f);
                m_end_color       = Color(0.6f, 0.65f, 0.8f, 0.0f);
                m_gravity_modifier = -3.0f;
                m_emission_radius = 15.0f;
                m_emission_direction = Vector3::Down;
                m_emission_cone_angle = 0.2f;
                m_directional_blend = 1.0f;
                break;

            case ParticlePreset::Confetti:
                m_max_particles   = 10000;
                m_emission_rate   = 1000.0f;
                m_lifetime        = 5.0f;
                m_start_speed     = 6.0f;
                m_start_size      = 0.05f;
                m_end_size        = 0.05f;
                m_start_color     = Color(1.0f, 0.3f, 0.5f, 1.0f);
                m_end_color       = Color(0.3f, 0.5f, 1.0f, 0.0f);
                m_gravity_modifier = -1.5f;
                m_emission_radius = 0.5f;
                m_emission_direction = Vector3::Up;
                m_emission_cone_angle = 1.0f;
                m_directional_blend = 0.4f;
                break;

            case ParticlePreset::Fireflies:
                m_max_particles   = 2000;
                m_emission_rate   = 50.0f;
                m_lifetime        = 8.0f;
                m_start_speed     = 0.3f;
                m_start_size      = 0.06f;
                m_end_size        = 0.02f;
                m_start_color     = Color(0.6f, 1.0f, 0.3f, 0.8f);
                m_end_color       = Color(0.2f, 0.8f, 0.1f, 0.0f);
                m_gravity_modifier = 0.1f;
                m_emission_radius = 5.0f;
                m_emission_direction = Vector3::Up;
                m_emission_cone_angle = 1.57f;
                m_directional_blend = 0.0f;
                break;

            case ParticlePreset::Blood:
                m_max_particles   = 3000;
                m_emission_rate   = 2000.0f;
                m_lifetime        = 1.0f;
                m_start_speed     = 5.0f;
                m_start_size      = 0.06f;
                m_end_size        = 0.02f;
                m_start_color     = Color(0.6f, 0.0f, 0.0f, 1.0f);
                m_end_color       = Color(0.3f, 0.0f, 0.0f, 0.0f);
                m_gravity_modifier = -3.0f;
                m_emission_radius = 0.1f;
                m_emission_direction = Vector3::Up;
                m_emission_cone_angle = 1.2f;
                m_directional_blend = 0.0f;
                break;

            case ParticlePreset::Magic:
                m_max_particles   = 8000;
                m_emission_rate   = 400.0f;
                m_lifetime        = 3.0f;
                m_start_speed     = 1.5f;
                m_start_size      = 0.08f;
                m_end_size        = 0.0f;
                m_start_color     = Color(0.4f, 0.2f, 1.0f, 1.0f);
                m_end_color       = Color(0.8f, 0.4f, 1.0f, 0.0f);
                m_gravity_modifier = 0.2f;
                m_emission_radius = 0.8f;
                m_emission_direction = Vector3::Up;
                m_emission_cone_angle = 1.57f;
                m_directional_blend = 0.0f;
                break;

            case ParticlePreset::Explosion:
                m_max_particles   = 15000;
                m_emission_rate   = 10000.0f;
                m_lifetime        = 1.5f;
                m_start_speed     = 12.0f;
                m_start_size      = 0.2f;
                m_end_size        = 0.0f;
                m_start_color     = Color(1.0f, 0.9f, 0.5f, 1.0f);
                m_end_color       = Color(0.4f, 0.1f, 0.0f, 0.0f);
                m_gravity_modifier = -1.0f;
                m_emission_radius = 0.2f;
                m_emission_direction = Vector3::Up;
                m_emission_cone_angle = 1.57f;
                m_directional_blend = 0.0f;
                break;

            case ParticlePreset::Waterfall:
                m_max_particles   = 15000;
                m_emission_rate   = 3000.0f;
                m_lifetime        = 3.0f;
                m_start_speed     = 0.5f;
                m_start_size      = 0.08f;
                m_end_size        = 0.15f;
                m_start_color     = Color(0.7f, 0.85f, 1.0f, 0.5f);
                m_end_color       = Color(0.8f, 0.9f, 1.0f, 0.0f);
                m_gravity_modifier = -4.0f;
                m_emission_radius = 1.0f;
                m_emission_direction = Vector3::Down;
                m_emission_cone_angle = 0.4f;
                m_directional_blend = 0.9f;
                break;

            case ParticlePreset::Embers:
                m_max_particles   = 5000;
                m_emission_rate   = 200.0f;
                m_lifetime        = 5.0f;
                m_start_speed     = 0.8f;
                m_start_size      = 0.04f;
                m_end_size        = 0.01f;
                m_start_color     = Color(1.0f, 0.6f, 0.1f, 1.0f);
                m_end_color       = Color(1.0f, 0.3f, 0.0f, 0.0f);
                m_gravity_modifier = -0.15f;
                m_emission_radius = 0.5f;
                m_emission_direction = Vector3::Up;
                m_emission_cone_angle = 0.9f;
                m_directional_blend = 0.3f;
                break;

            case ParticlePreset::TireSmoke:
                m_max_particles   = 8000;
                m_emission_rate   = 600.0f;
                m_lifetime        = 3.0f;
                m_start_speed     = 1.5f;
                m_start_size      = 0.15f;
                m_end_size        = 1.2f;
                m_start_color     = Color(0.85f, 0.85f, 0.85f, 0.5f);
                m_end_color       = Color(0.7f, 0.7f, 0.7f, 0.0f);
                m_gravity_modifier = -0.05f;
                m_emission_radius = 0.15f;
                m_emission_direction = Vector3::Up;
                m_emission_cone_angle = 1.2f;
                m_directional_blend = 0.2f;
                break;

            case ParticlePreset::Exhaust:
                m_max_particles   = 3000;
                m_emission_rate   = 300.0f;
                m_lifetime        = 1.2f;
                m_start_speed     = 0.6f;
                m_start_size      = 0.03f;
                m_end_size        = 0.15f;
                m_start_color     = Color(0.4f, 0.4f, 0.4f, 0.8f);
                m_end_color       = Color(0.25f, 0.25f, 0.25f, 0.0f);
                m_gravity_modifier = -0.08f;
                m_emission_radius = 0.03f;
                m_emission_direction = Vector3::Backward;
                m_emission_cone_angle = 0.45f;
                m_directional_blend = 0.85f;
                break;

            case ParticlePreset::Custom:
            default:
                break;
        }
    }

    void ParticleSystem::SetEmissionDirection(const Vector3& direction)
    {
        if (!direction.IsFinite() || direction.LengthSquared() <= 0.0001f)
        {
            return;
        }

        m_emission_direction = direction.Normalized();
        m_preset = ParticlePreset::Custom;
    }

    void ParticleSystem::SetEmissionConeAngle(float angle)
    {
        m_emission_cone_angle = clamp(angle, 0.0f, 3.14159265f);
        m_preset = ParticlePreset::Custom;
    }

    void ParticleSystem::SetDirectionalBlend(float blend)
    {
        m_directional_blend = clamp(blend, 0.0f, 1.0f);
        m_preset = ParticlePreset::Custom;
    }

    void ParticleSystem::SetTexture(const string& file_path)
    {
        if (file_path.empty())
        {
            m_texture = nullptr;
            return;
        }

        // the cache owns the texture, we keep a raw pointer
        m_texture = ResourceCache::Load<RHI_Texture>(file_path, RHI_Texture_Srv).get();
    }

    void ParticleSystem::Save(pugi::xml_node& node)
    {
        node.append_attribute("preset")          = static_cast<uint32_t>(m_preset);
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
        node.append_attribute("emission_direction_x") = m_emission_direction.x;
        node.append_attribute("emission_direction_y") = m_emission_direction.y;
        node.append_attribute("emission_direction_z") = m_emission_direction.z;
        node.append_attribute("emission_cone_angle")  = m_emission_cone_angle;
        node.append_attribute("directional_blend")    = m_directional_blend;
        node.append_attribute("texture_path")    = m_texture ? m_texture->GetResourceFilePath().c_str() : "";
    }

    void ParticleSystem::Load(pugi::xml_node& node)
    {
        m_preset          = static_cast<ParticlePreset>(node.attribute("preset").as_uint(static_cast<uint32_t>(ParticlePreset::Fire)));
        m_max_particles   = node.attribute("max_particles").as_uint(10000);
        m_emission_rate   = node.attribute("emission_rate").as_float(500.0f);
        m_lifetime        = node.attribute("lifetime").as_float(3.0f);
        m_start_speed     = node.attribute("start_speed").as_float(2.0f);
        m_start_size      = node.attribute("start_size").as_float(0.3f);
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
        m_emission_direction = Vector3(
            node.attribute("emission_direction_x").as_float(0.0f),
            node.attribute("emission_direction_y").as_float(1.0f),
            node.attribute("emission_direction_z").as_float(0.0f)
        );
        SetEmissionDirection(m_emission_direction);
        m_preset              = static_cast<ParticlePreset>(node.attribute("preset").as_uint(static_cast<uint32_t>(ParticlePreset::Fire)));
        m_emission_cone_angle = node.attribute("emission_cone_angle").as_float(1.57f);
        m_directional_blend   = node.attribute("directional_blend").as_float(0.0f);

        string texture_path = node.attribute("texture_path").as_string("");
        if (!texture_path.empty())
        {
            SetTexture(texture_path);
        }
    }

    void ParticleSystem::RegisterForScripting(sol::state_view state)
    {
        state.new_usertype<ParticleSystem>("ParticleSystem",
            sol::base_classes,        sol::bases<Component>(),

            "GetEmissionRate",        &ParticleSystem::GetEmissionRate,
            "SetEmissionRate",        &ParticleSystem::SetEmissionRate,
            "GetMaxParticles",        &ParticleSystem::GetMaxParticles,
            "SetMaxParticles",        &ParticleSystem::SetMaxParticles,
            "GetLifetime",            &ParticleSystem::GetLifetime,
            "SetLifetime",            &ParticleSystem::SetLifetime,
            "GetStartSpeed",          &ParticleSystem::GetStartSpeed,
            "SetStartSpeed",          &ParticleSystem::SetStartSpeed,
            "GetStartSize",           &ParticleSystem::GetStartSize,
            "SetStartSize",           &ParticleSystem::SetStartSize,
            "GetEndSize",             &ParticleSystem::GetEndSize,
            "SetEndSize",             &ParticleSystem::SetEndSize,
            "GetGravityModifier",     &ParticleSystem::GetGravityModifier,
            "SetGravityModifier",     &ParticleSystem::SetGravityModifier,
            "GetEmissionRadius",      &ParticleSystem::GetEmissionRadius,
            "SetEmissionRadius",      &ParticleSystem::SetEmissionRadius,
            "SetEmissionDirection",   [](ParticleSystem& self, float x, float y, float z) { self.SetEmissionDirection(Vector3(x, y, z)); },
            "GetEmissionConeAngle",   &ParticleSystem::GetEmissionConeAngle,
            "SetEmissionConeAngle",   &ParticleSystem::SetEmissionConeAngle,
            "GetDirectionalBlend",    &ParticleSystem::GetDirectionalBlend,
            "SetDirectionalBlend",    &ParticleSystem::SetDirectionalBlend,
            "SetStartColor",          [](ParticleSystem& self, float r, float g, float b, float a) { self.SetStartColor(Color(r, g, b, a)); },
            "SetEndColor",            [](ParticleSystem& self, float r, float g, float b, float a) { self.SetEndColor(Color(r, g, b, a)); },
            "SetTexture",             sol::overload(
                [](ParticleSystem& self, const string& file_path) { self.SetTexture(file_path); },
                [](ParticleSystem& self) { self.SetTexture(""); })
            );
    }

    sol::reference ParticleSystem::AsLua(sol::state_view state)
    {
        return sol::make_reference(state, this);
    }
}
