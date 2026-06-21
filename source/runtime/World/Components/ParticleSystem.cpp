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
#include "../../FileSystem/FileSystem.h"
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
        SP_REGISTER_ATTRIBUTE_VALUE_SET(m_preset, ApplyPreset, ParticlePreset);
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
        SP_REGISTER_ATTRIBUTE_VALUE_SET(m_blend_mode, SetBlendMode, ParticleBlendMode);
        SP_REGISTER_ATTRIBUTE_VALUE_SET(m_lighting_mode, SetLightingMode, ParticleLightingMode);
        SP_REGISTER_ATTRIBUTE_VALUE_SET(m_render_mode, SetRenderMode, ParticleRenderMode);
        SP_REGISTER_ATTRIBUTE_VALUE_VALUE(m_emissive_strength, float);
        SP_REGISTER_ATTRIBUTE_VALUE_VALUE(m_soft_depth_scale, float);
        SP_REGISTER_ATTRIBUTE_VALUE_VALUE(m_volume_density, float);
        SP_REGISTER_ATTRIBUTE_VALUE_VALUE(m_volume_anisotropy, float);
        SP_REGISTER_ATTRIBUTE_VALUE_VALUE(m_volume_shadowing, float);
        SP_REGISTER_ATTRIBUTE_VALUE_VALUE(m_drag, float);
        SP_REGISTER_ATTRIBUTE_VALUE_VALUE(m_turbulence_strength, float);
        SP_REGISTER_ATTRIBUTE_VALUE_VALUE(m_wind_influence, float);
        SP_REGISTER_ATTRIBUTE_VALUE_VALUE(m_velocity_inheritance, float);
        SP_REGISTER_ATTRIBUTE_VALUE_VALUE(m_velocity_stretch, float);
        SP_REGISTER_ATTRIBUTE_VALUE_VALUE(m_spawn_burst, float);
        SP_REGISTER_ATTRIBUTE_VALUE_VALUE(m_flipbook_rows, uint32_t);
        SP_REGISTER_ATTRIBUTE_VALUE_VALUE(m_flipbook_columns, uint32_t);
        SP_REGISTER_ATTRIBUTE_VALUE_VALUE(m_flipbook_fps, float);
        SP_REGISTER_ATTRIBUTE_VALUE_SET(m_effect_path, SetEffectPath, std::string);

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
        m_effect_path.clear();
        m_blend_mode           = ParticleBlendMode::Additive;
        m_lighting_mode        = ParticleLightingMode::Lit;
        m_render_mode          = ParticleRenderMode::Billboard;
        m_emissive_strength    = 0.0f;
        m_soft_depth_scale      = 20.0f;
        m_volume_density        = 1.0f;
        m_volume_anisotropy     = 0.35f;
        m_volume_shadowing      = 0.5f;
        m_drag                  = 1.2f;
        m_turbulence_strength   = 0.3f;
        m_wind_influence        = 0.0f;
        m_velocity_inheritance  = 0.0f;
        m_velocity_stretch      = 0.0f;
        m_spawn_burst           = 0.0f;
        m_flipbook_rows         = 1;
        m_flipbook_columns      = 1;
        m_flipbook_fps          = 0.0f;

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
                m_lighting_mode = ParticleLightingMode::Emissive;
                m_emissive_strength = 6.0f;
                m_turbulence_strength = 0.45f;
                m_velocity_stretch = 0.2f;
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
                m_blend_mode = ParticleBlendMode::Premultiplied;
                m_drag = 1.6f;
                m_turbulence_strength = 0.55f;
                m_wind_influence = 0.28f;
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
                m_blend_mode = ParticleBlendMode::Premultiplied;
                m_lighting_mode = ParticleLightingMode::Unlit;
                m_drag = 1.8f;
                m_turbulence_strength = 0.35f;
                m_wind_influence = 0.45f;
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
                m_lighting_mode = ParticleLightingMode::Emissive;
                m_emissive_strength = 10.0f;
                m_drag = 0.45f;
                m_velocity_stretch = 1.0f;
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
                m_blend_mode = ParticleBlendMode::Premultiplied;
                m_drag = 1.7f;
                m_turbulence_strength = 0.45f;
                m_wind_influence = 0.55f;
                m_velocity_inheritance = 0.4f;
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
                m_blend_mode = ParticleBlendMode::Alpha;
                m_lighting_mode = ParticleLightingMode::Unlit;
                m_drag = 0.2f;
                m_wind_influence = 0.35f;
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
                m_blend_mode = ParticleBlendMode::Alpha;
                m_lighting_mode = ParticleLightingMode::Unlit;
                m_drag = 0.05f;
                m_velocity_stretch = 1.0f;
                m_wind_influence = 0.12f;
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
                m_lighting_mode = ParticleLightingMode::Emissive;
                m_emissive_strength = 2.0f;
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
                m_lighting_mode = ParticleLightingMode::Emissive;
                m_emissive_strength = 5.0f;
                m_turbulence_strength = 0.6f;
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
                m_lighting_mode = ParticleLightingMode::Emissive;
                m_emissive_strength = 12.0f;
                m_drag = 0.8f;
                m_turbulence_strength = 0.8f;
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
                m_blend_mode = ParticleBlendMode::Alpha;
                m_lighting_mode = ParticleLightingMode::Unlit;
                m_drag = 0.4f;
                m_velocity_stretch = 0.5f;
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
                m_lighting_mode = ParticleLightingMode::Emissive;
                m_emissive_strength = 4.0f;
                m_drag = 0.6f;
                m_turbulence_strength = 0.55f;
                m_wind_influence = 0.2f;
                m_velocity_stretch = 0.4f;
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
                m_blend_mode = ParticleBlendMode::Premultiplied;
                m_drag = 1.85f;
                m_turbulence_strength = 0.65f;
                m_wind_influence = 0.45f;
                m_velocity_inheritance = 0.55f;
                m_velocity_stretch = 0.25f;
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
                m_blend_mode = ParticleBlendMode::Premultiplied;
                m_drag = 1.4f;
                m_turbulence_strength = 0.28f;
                m_wind_influence = 0.3f;
                m_velocity_inheritance = 0.75f;
                m_velocity_stretch = 0.35f;
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

    ParticleBlendMode ParticleSystem::GetBlendMode() const
    {
        return m_blend_mode;
    }

    void ParticleSystem::SetBlendMode(ParticleBlendMode mode)
    {
        if (static_cast<uint32_t>(mode) >= static_cast<uint32_t>(ParticleBlendMode::Count))
        {
            return;
        }

        m_blend_mode = mode;
        MarkCustom();
    }

    ParticleLightingMode ParticleSystem::GetLightingMode() const
    {
        return m_lighting_mode;
    }

    void ParticleSystem::SetLightingMode(ParticleLightingMode mode)
    {
        if (static_cast<uint32_t>(mode) >= static_cast<uint32_t>(ParticleLightingMode::Count))
        {
            return;
        }

        m_lighting_mode = mode;
        MarkCustom();
    }

    ParticleRenderMode ParticleSystem::GetRenderMode() const
    {
        return m_render_mode;
    }

    void ParticleSystem::SetRenderMode(ParticleRenderMode mode)
    {
        if (static_cast<uint32_t>(mode) >= static_cast<uint32_t>(ParticleRenderMode::Count))
        {
            return;
        }

        m_render_mode = mode;
        MarkCustom();
    }

    float ParticleSystem::GetEmissiveStrength() const
    {
        return m_emissive_strength;
    }

    void ParticleSystem::SetEmissiveStrength(float strength)
    {
        m_emissive_strength = clamp(strength, 0.0f, 100.0f);
        MarkCustom();
    }

    float ParticleSystem::GetSoftDepthScale() const
    {
        return m_soft_depth_scale;
    }

    void ParticleSystem::SetSoftDepthScale(float scale)
    {
        m_soft_depth_scale = clamp(scale, 0.0f, 100.0f);
        MarkCustom();
    }

    float ParticleSystem::GetVolumeDensity() const
    {
        return m_volume_density;
    }

    void ParticleSystem::SetVolumeDensity(float density)
    {
        m_volume_density = clamp(density, 0.0f, 25.0f);
        MarkCustom();
    }

    float ParticleSystem::GetVolumeAnisotropy() const
    {
        return m_volume_anisotropy;
    }

    void ParticleSystem::SetVolumeAnisotropy(float anisotropy)
    {
        m_volume_anisotropy = clamp(anisotropy, -0.9f, 0.9f);
        MarkCustom();
    }

    float ParticleSystem::GetVolumeShadowing() const
    {
        return m_volume_shadowing;
    }

    void ParticleSystem::SetVolumeShadowing(float shadowing)
    {
        m_volume_shadowing = clamp(shadowing, 0.0f, 1.0f);
        MarkCustom();
    }

    float ParticleSystem::GetDrag() const
    {
        return m_drag;
    }

    void ParticleSystem::SetDrag(float drag)
    {
        m_drag = clamp(drag, 0.0f, 10.0f);
        MarkCustom();
    }

    float ParticleSystem::GetTurbulenceStrength() const
    {
        return m_turbulence_strength;
    }

    void ParticleSystem::SetTurbulenceStrength(float strength)
    {
        m_turbulence_strength = clamp(strength, 0.0f, 10.0f);
        MarkCustom();
    }

    float ParticleSystem::GetWindInfluence() const
    {
        return m_wind_influence;
    }

    void ParticleSystem::SetWindInfluence(float influence)
    {
        m_wind_influence = clamp(influence, 0.0f, 5.0f);
        MarkCustom();
    }

    float ParticleSystem::GetVelocityInheritance() const
    {
        return m_velocity_inheritance;
    }

    void ParticleSystem::SetVelocityInheritance(float inheritance)
    {
        m_velocity_inheritance = clamp(inheritance, 0.0f, 2.0f);
        MarkCustom();
    }

    float ParticleSystem::GetVelocityStretch() const
    {
        return m_velocity_stretch;
    }

    void ParticleSystem::SetVelocityStretch(float stretch)
    {
        m_velocity_stretch = clamp(stretch, 0.0f, 5.0f);
        MarkCustom();
    }

    float ParticleSystem::GetSpawnBurst() const
    {
        return m_spawn_burst;
    }

    void ParticleSystem::SetSpawnBurst(float count)
    {
        m_spawn_burst = clamp(count, 0.0f, 100000.0f);
        MarkCustom();
    }

    void ParticleSystem::TriggerBurst(float count)
    {
        m_pending_burst += clamp(count, 0.0f, 100000.0f);
    }

    uint32_t ParticleSystem::GetFlipbookRows() const
    {
        return m_flipbook_rows;
    }

    void ParticleSystem::SetFlipbookRows(uint32_t rows)
    {
        m_flipbook_rows = clamp(rows, 1u, 32u);
        MarkCustom();
    }

    uint32_t ParticleSystem::GetFlipbookColumns() const
    {
        return m_flipbook_columns;
    }

    void ParticleSystem::SetFlipbookColumns(uint32_t columns)
    {
        m_flipbook_columns = clamp(columns, 1u, 32u);
        MarkCustom();
    }

    float ParticleSystem::GetFlipbookFps() const
    {
        return m_flipbook_fps;
    }

    void ParticleSystem::SetFlipbookFps(float fps)
    {
        m_flipbook_fps = clamp(fps, 0.0f, 120.0f);
        MarkCustom();
    }

    const string& ParticleSystem::GetEffectPath() const
    {
        return m_effect_path;
    }

    void ParticleSystem::SetEffectPath(const string& file_path)
    {
        m_effect_path = file_path;
    }

    uint32_t ParticleSystem::ConsumeEmissionCount(float delta_time)
    {
        float emit = max(m_emission_rate, 0.0f) * max(delta_time, 0.0f) + m_emission_remainder + m_pending_burst;
        uint32_t count = static_cast<uint32_t>(floorf(emit));
        m_emission_remainder = emit - static_cast<float>(count);
        m_pending_burst = 0.0f;

        return min(count, m_max_particles);
    }

    void ParticleSystem::UpdateRuntime(const Vector3& position, float delta_time)
    {
        Vector3 velocity = Vector3::Zero;
        if (m_has_last_position && delta_time > 0.000001f)
        {
            velocity = (position - m_last_position) / delta_time;
            const float max_velocity = 80.0f;
            velocity.ClampMagnitude(max_velocity);
        }

        if (m_has_last_position && delta_time > 0.000001f)
        {
            const float response = 1.0f - expf(-delta_time * 7.0f);
            m_emitter_velocity += (velocity - m_emitter_velocity) * response;
        }
        else
        {
            m_emitter_velocity = velocity;
        }

        m_last_position     = position;
        m_has_last_position = true;
    }

    const Vector3& ParticleSystem::GetEmitterVelocity() const
    {
        return m_emitter_velocity;
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
        SaveProperties(node);
    }

    void ParticleSystem::Load(pugi::xml_node& node)
    {
        string effect_path = node.attribute("effect_path").as_string("");
        if (!effect_path.empty())
        {
            LoadEffect(effect_path);
        }

        LoadProperties(node);
    }

    void ParticleSystem::SaveProperties(pugi::xml_node& node) const
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
        node.append_attribute("blend_mode")           = static_cast<uint32_t>(m_blend_mode);
        node.append_attribute("lighting_mode")        = static_cast<uint32_t>(m_lighting_mode);
        node.append_attribute("render_mode")          = static_cast<uint32_t>(m_render_mode);
        node.append_attribute("emissive_strength")    = m_emissive_strength;
        node.append_attribute("soft_depth_scale")     = m_soft_depth_scale;
        node.append_attribute("volume_density")       = m_volume_density;
        node.append_attribute("volume_anisotropy")    = m_volume_anisotropy;
        node.append_attribute("volume_shadowing")     = m_volume_shadowing;
        node.append_attribute("drag")                 = m_drag;
        node.append_attribute("turbulence_strength")  = m_turbulence_strength;
        node.append_attribute("wind_influence")       = m_wind_influence;
        node.append_attribute("velocity_inheritance") = m_velocity_inheritance;
        node.append_attribute("velocity_stretch")     = m_velocity_stretch;
        node.append_attribute("spawn_burst")          = m_spawn_burst;
        node.append_attribute("flipbook_rows")        = m_flipbook_rows;
        node.append_attribute("flipbook_columns")     = m_flipbook_columns;
        node.append_attribute("flipbook_fps")         = m_flipbook_fps;
        node.append_attribute("effect_path")          = m_effect_path.c_str();
        node.append_attribute("texture_path")         = m_texture ? m_texture->GetResourceFilePath().c_str() : "";
    }

    void ParticleSystem::LoadProperties(pugi::xml_node& node)
    {
        m_preset        = static_cast<ParticlePreset>(node.attribute("preset").as_uint(static_cast<uint32_t>(m_preset)));
        m_max_particles = node.attribute("max_particles").as_uint(m_max_particles);
        m_emission_rate = node.attribute("emission_rate").as_float(m_emission_rate);
        m_lifetime      = node.attribute("lifetime").as_float(m_lifetime);
        m_start_speed   = node.attribute("start_speed").as_float(m_start_speed);
        m_start_size    = node.attribute("start_size").as_float(m_start_size);
        m_end_size      = node.attribute("end_size").as_float(m_end_size);
        m_start_color.r = node.attribute("start_color_r").as_float(m_start_color.r);
        m_start_color.g = node.attribute("start_color_g").as_float(m_start_color.g);
        m_start_color.b = node.attribute("start_color_b").as_float(m_start_color.b);
        m_start_color.a = node.attribute("start_color_a").as_float(m_start_color.a);
        m_end_color.r   = node.attribute("end_color_r").as_float(m_end_color.r);
        m_end_color.g   = node.attribute("end_color_g").as_float(m_end_color.g);
        m_end_color.b   = node.attribute("end_color_b").as_float(m_end_color.b);
        m_end_color.a   = node.attribute("end_color_a").as_float(m_end_color.a);
        m_gravity_modifier = node.attribute("gravity_modifier").as_float(m_gravity_modifier);
        m_emission_radius  = node.attribute("emission_radius").as_float(m_emission_radius);

        m_emission_direction = Vector3(
            node.attribute("emission_direction_x").as_float(m_emission_direction.x),
            node.attribute("emission_direction_y").as_float(m_emission_direction.y),
            node.attribute("emission_direction_z").as_float(m_emission_direction.z)
        );
        if (!m_emission_direction.IsFinite() || m_emission_direction.LengthSquared() <= 0.0001f)
        {
            m_emission_direction = Vector3::Up;
        }
        else
        {
            m_emission_direction.Normalize();
        }

        m_emission_cone_angle = clamp(node.attribute("emission_cone_angle").as_float(m_emission_cone_angle), 0.0f, 3.14159265f);
        m_directional_blend   = clamp(node.attribute("directional_blend").as_float(m_directional_blend), 0.0f, 1.0f);
        m_blend_mode          = static_cast<ParticleBlendMode>(node.attribute("blend_mode").as_uint(static_cast<uint32_t>(m_blend_mode)));
        m_lighting_mode       = static_cast<ParticleLightingMode>(node.attribute("lighting_mode").as_uint(static_cast<uint32_t>(m_lighting_mode)));
        m_render_mode         = static_cast<ParticleRenderMode>(node.attribute("render_mode").as_uint(static_cast<uint32_t>(m_render_mode)));
        if (static_cast<uint32_t>(m_blend_mode) >= static_cast<uint32_t>(ParticleBlendMode::Count))
        {
            m_blend_mode = ParticleBlendMode::Alpha;
        }
        if (static_cast<uint32_t>(m_lighting_mode) >= static_cast<uint32_t>(ParticleLightingMode::Count))
        {
            m_lighting_mode = ParticleLightingMode::Lit;
        }
        if (static_cast<uint32_t>(m_render_mode) >= static_cast<uint32_t>(ParticleRenderMode::Count))
        {
            m_render_mode = ParticleRenderMode::Billboard;
        }

        m_emissive_strength    = clamp(node.attribute("emissive_strength").as_float(m_emissive_strength), 0.0f, 100.0f);
        m_soft_depth_scale     = clamp(node.attribute("soft_depth_scale").as_float(m_soft_depth_scale), 0.0f, 100.0f);
        m_volume_density       = clamp(node.attribute("volume_density").as_float(m_volume_density), 0.0f, 25.0f);
        m_volume_anisotropy    = clamp(node.attribute("volume_anisotropy").as_float(m_volume_anisotropy), -0.9f, 0.9f);
        m_volume_shadowing     = clamp(node.attribute("volume_shadowing").as_float(m_volume_shadowing), 0.0f, 1.0f);
        m_drag                 = clamp(node.attribute("drag").as_float(m_drag), 0.0f, 10.0f);
        m_turbulence_strength  = clamp(node.attribute("turbulence_strength").as_float(m_turbulence_strength), 0.0f, 10.0f);
        m_wind_influence       = clamp(node.attribute("wind_influence").as_float(m_wind_influence), 0.0f, 5.0f);
        m_velocity_inheritance = clamp(node.attribute("velocity_inheritance").as_float(m_velocity_inheritance), 0.0f, 2.0f);
        m_velocity_stretch     = clamp(node.attribute("velocity_stretch").as_float(m_velocity_stretch), 0.0f, 5.0f);
        m_spawn_burst          = clamp(node.attribute("spawn_burst").as_float(m_spawn_burst), 0.0f, 100000.0f);
        m_flipbook_rows        = clamp(node.attribute("flipbook_rows").as_uint(m_flipbook_rows), 1u, 32u);
        m_flipbook_columns     = clamp(node.attribute("flipbook_columns").as_uint(m_flipbook_columns), 1u, 32u);
        m_flipbook_fps         = clamp(node.attribute("flipbook_fps").as_float(m_flipbook_fps), 0.0f, 120.0f);
        m_effect_path          = node.attribute("effect_path").as_string(m_effect_path.c_str());

        string texture_path = node.attribute("texture_path").as_string(m_texture ? m_texture->GetResourceFilePath().c_str() : "");
        SetTexture(texture_path);
        if (m_spawn_burst > 0.0f)
        {
            TriggerBurst(m_spawn_burst);
        }
    }

    bool ParticleSystem::LoadEffect(const string& file_path)
    {
        if (file_path.empty() || !FileSystem::Exists(file_path))
        {
            return false;
        }

        pugi::xml_document doc;
        pugi::xml_parse_result result = doc.load_file(file_path.c_str());
        if (!result)
        {
            return false;
        }

        pugi::xml_node root = doc.child("ParticleEffect");
        if (!root)
        {
            root = doc.child("particle_system");
        }
        if (!root)
        {
            root = doc.first_child();
        }
        if (!root)
        {
            return false;
        }

        LoadProperties(root);
        m_effect_path = file_path;
        return true;
    }

    bool ParticleSystem::SaveEffect(const string& file_path)
    {
        if (file_path.empty())
        {
            return false;
        }

        string directory = FileSystem::GetDirectoryFromFilePath(file_path);
        if (!directory.empty())
        {
            FileSystem::CreateDirectory_(directory);
        }

        pugi::xml_document doc;
        pugi::xml_node declaration = doc.append_child(pugi::node_declaration);
        declaration.append_attribute("version")  = "1.0";
        declaration.append_attribute("encoding") = "utf-8";

        pugi::xml_node root = doc.append_child("ParticleEffect");
        root.append_attribute("version") = 1;
        SaveProperties(root);
        root.attribute("effect_path").set_value("");

        return doc.save_file(file_path.c_str(), " ", pugi::format_indent | pugi::format_indent_attributes);
    }

    void ParticleSystem::MarkCustom()
    {
        m_preset = ParticlePreset::Custom;
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
            "GetBlendMode",           [](ParticleSystem& self) { return static_cast<uint32_t>(self.GetBlendMode()); },
            "SetBlendMode",           [](ParticleSystem& self, uint32_t mode) { self.SetBlendMode(static_cast<ParticleBlendMode>(mode)); },
            "GetLightingMode",        [](ParticleSystem& self) { return static_cast<uint32_t>(self.GetLightingMode()); },
            "SetLightingMode",        [](ParticleSystem& self, uint32_t mode) { self.SetLightingMode(static_cast<ParticleLightingMode>(mode)); },
            "GetRenderMode",          [](ParticleSystem& self) { return static_cast<uint32_t>(self.GetRenderMode()); },
            "SetRenderMode",          [](ParticleSystem& self, uint32_t mode) { self.SetRenderMode(static_cast<ParticleRenderMode>(mode)); },
            "GetEmissiveStrength",    &ParticleSystem::GetEmissiveStrength,
            "SetEmissiveStrength",    &ParticleSystem::SetEmissiveStrength,
            "GetSoftDepthScale",      &ParticleSystem::GetSoftDepthScale,
            "SetSoftDepthScale",      &ParticleSystem::SetSoftDepthScale,
            "GetVolumeDensity",       &ParticleSystem::GetVolumeDensity,
            "SetVolumeDensity",       &ParticleSystem::SetVolumeDensity,
            "GetVolumeAnisotropy",    &ParticleSystem::GetVolumeAnisotropy,
            "SetVolumeAnisotropy",    &ParticleSystem::SetVolumeAnisotropy,
            "GetVolumeShadowing",     &ParticleSystem::GetVolumeShadowing,
            "SetVolumeShadowing",     &ParticleSystem::SetVolumeShadowing,
            "GetDrag",                &ParticleSystem::GetDrag,
            "SetDrag",                &ParticleSystem::SetDrag,
            "GetTurbulenceStrength",  &ParticleSystem::GetTurbulenceStrength,
            "SetTurbulenceStrength",  &ParticleSystem::SetTurbulenceStrength,
            "GetWindInfluence",       &ParticleSystem::GetWindInfluence,
            "SetWindInfluence",       &ParticleSystem::SetWindInfluence,
            "GetVelocityInheritance", &ParticleSystem::GetVelocityInheritance,
            "SetVelocityInheritance", &ParticleSystem::SetVelocityInheritance,
            "GetVelocityStretch",     &ParticleSystem::GetVelocityStretch,
            "SetVelocityStretch",     &ParticleSystem::SetVelocityStretch,
            "GetSpawnBurst",          &ParticleSystem::GetSpawnBurst,
            "SetSpawnBurst",          &ParticleSystem::SetSpawnBurst,
            "TriggerBurst",           &ParticleSystem::TriggerBurst,
            "GetFlipbookRows",        &ParticleSystem::GetFlipbookRows,
            "SetFlipbookRows",        &ParticleSystem::SetFlipbookRows,
            "GetFlipbookColumns",     &ParticleSystem::GetFlipbookColumns,
            "SetFlipbookColumns",     &ParticleSystem::SetFlipbookColumns,
            "GetFlipbookFps",         &ParticleSystem::GetFlipbookFps,
            "SetFlipbookFps",         &ParticleSystem::SetFlipbookFps,
            "GetEffectPath",          &ParticleSystem::GetEffectPath,
            "LoadEffect",             &ParticleSystem::LoadEffect,
            "SaveEffect",             &ParticleSystem::SaveEffect,
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
