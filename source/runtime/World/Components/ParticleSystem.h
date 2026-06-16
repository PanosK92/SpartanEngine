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

//= INCLUDES ========================
#include "Component.h"
#include "../../Rendering/Color.h"
#include "../../Math/Vector3.h"
#include <sol/forward.hpp>
//===================================

namespace spartan
{
    class RHI_Texture;

    enum class ParticlePreset : uint32_t
    {
        Custom,     // user-defined values
        Fire,       // campfire-style flames
        Smoke,      // rising dark smoke
        Steam,      // soft white vapor
        Sparks,     // bright metallic sparks
        Dust,       // kicked-up ground dust
        Snow,       // falling snowflakes
        Rain,       // downpour streaks
        Confetti,   // celebratory burst
        Fireflies,  // slow-drifting glowing dots
        Blood,      // impact splatter
        Magic,      // arcane energy swirl
        Explosion,  // short-lived blast
        Waterfall,  // cascading water mist
        Embers,     // floating hot embers rising from fire
        TireSmoke,  // white burnout/drift smoke from wheels
        Exhaust,    // thin exhaust fumes from tailpipe
        Count
    };

    class ParticleSystem : public Component
    {
    public:
        ParticleSystem(Entity* entity);
        ~ParticleSystem() = default;

        //= COMPONENT ================================
        void Tick() override;
        void Save(pugi::xml_node& node) override;
        void Load(pugi::xml_node& node) override;
        //============================================

        static void RegisterForScripting(sol::state_view state);
        sol::reference AsLua(sol::state_view state) override;

        // preset
        ParticlePreset GetPreset() const              { return m_preset; }
        void ApplyPreset(ParticlePreset preset);

        // max particles
        uint32_t GetMaxParticles() const       { return m_max_particles; }
        void SetMaxParticles(uint32_t count)   { m_max_particles = count; m_preset = ParticlePreset::Custom; }

        // emission rate (particles per second)
        float GetEmissionRate() const          { return m_emission_rate; }
        void SetEmissionRate(float rate)       { m_emission_rate = rate; m_preset = ParticlePreset::Custom; }

        // lifetime in seconds
        float GetLifetime() const              { return m_lifetime; }
        void SetLifetime(float lifetime)       { m_lifetime = lifetime; m_preset = ParticlePreset::Custom; }

        // initial speed
        float GetStartSpeed() const            { return m_start_speed; }
        void SetStartSpeed(float speed)        { m_start_speed = speed; m_preset = ParticlePreset::Custom; }

        // size over lifetime
        float GetStartSize() const             { return m_start_size; }
        void SetStartSize(float size)          { m_start_size = size; m_preset = ParticlePreset::Custom; }
        float GetEndSize() const               { return m_end_size; }
        void SetEndSize(float size)            { m_end_size = size; m_preset = ParticlePreset::Custom; }

        // color over lifetime
        const Color& GetStartColor() const     { return m_start_color; }
        void SetStartColor(const Color& color) { m_start_color = color; m_preset = ParticlePreset::Custom; }
        const Color& GetEndColor() const       { return m_end_color; }
        void SetEndColor(const Color& color)   { m_end_color = color; m_preset = ParticlePreset::Custom; }

        // gravity
        float GetGravityModifier() const       { return m_gravity_modifier; }
        void SetGravityModifier(float gravity) { m_gravity_modifier = gravity; m_preset = ParticlePreset::Custom; }

        // emission shape (sphere radius)
        float GetEmissionRadius() const        { return m_emission_radius; }
        void SetEmissionRadius(float radius)   { m_emission_radius = radius; m_preset = ParticlePreset::Custom; }

        // emission direction
        const math::Vector3& GetEmissionDirection() const { return m_emission_direction; }
        void SetEmissionDirection(const math::Vector3& direction);
        float GetEmissionConeAngle() const                { return m_emission_cone_angle; }
        void SetEmissionConeAngle(float angle);
        float GetDirectionalBlend() const                 { return m_directional_blend; }
        void SetDirectionalBlend(float blend);

        // particle texture, sampled as a billboard mask when set, otherwise a procedural circle is used
        RHI_Texture* GetTexture() const        { return m_texture; }
        void SetTexture(RHI_Texture* texture)  { m_texture = texture; }
        void SetTexture(const std::string& file_path);

    private:
        ParticlePreset m_preset    = ParticlePreset::Fire;
        uint32_t m_max_particles   = 10000;
        float m_emission_rate      = 500.0f;
        float m_lifetime           = 3.0f;
        float m_start_speed        = 2.0f;
        float m_start_size         = 0.3f;
        float m_end_size           = 0.0f;
        Color m_start_color        = Color(1.0f, 0.8f, 0.4f, 1.0f);
        Color m_end_color          = Color(1.0f, 0.2f, 0.0f, 0.0f);
        float m_gravity_modifier   = -1.0f;
        float m_emission_radius    = 0.5f;
        math::Vector3 m_emission_direction = math::Vector3::Up;
        float m_emission_cone_angle        = 1.57f;
        float m_directional_blend          = 0.0f;
        RHI_Texture* m_texture     = nullptr; // owned by the resource cache
    };
}
