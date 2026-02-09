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
//===================================

namespace spartan
{
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

        // max particles
        uint32_t GetMaxParticles() const       { return m_max_particles; }
        void SetMaxParticles(uint32_t count)   { m_max_particles = count; }

        // emission rate (particles per second)
        float GetEmissionRate() const          { return m_emission_rate; }
        void SetEmissionRate(float rate)       { m_emission_rate = rate; }

        // lifetime in seconds
        float GetLifetime() const              { return m_lifetime; }
        void SetLifetime(float lifetime)       { m_lifetime = lifetime; }

        // initial speed
        float GetStartSpeed() const            { return m_start_speed; }
        void SetStartSpeed(float speed)        { m_start_speed = speed; }

        // size over lifetime
        float GetStartSize() const             { return m_start_size; }
        void SetStartSize(float size)          { m_start_size = size; }
        float GetEndSize() const               { return m_end_size; }
        void SetEndSize(float size)            { m_end_size = size; }

        // color over lifetime
        const Color& GetStartColor() const     { return m_start_color; }
        void SetStartColor(const Color& color) { m_start_color = color; }
        const Color& GetEndColor() const       { return m_end_color; }
        void SetEndColor(const Color& color)   { m_end_color = color; }

        // gravity
        float GetGravityModifier() const       { return m_gravity_modifier; }
        void SetGravityModifier(float gravity) { m_gravity_modifier = gravity; }

        // emission shape (sphere radius)
        float GetEmissionRadius() const        { return m_emission_radius; }
        void SetEmissionRadius(float radius)   { m_emission_radius = radius; }

    private:
        uint32_t m_max_particles   = 10000;
        float m_emission_rate      = 500.0f;   // particles per second
        float m_lifetime           = 3.0f;     // seconds
        float m_start_speed        = 2.0f;     // meters per second
        float m_start_size         = 0.3f;     // meters
        float m_end_size           = 0.0f;     // meters (fade out)
        Color m_start_color        = Color(1.0f, 0.8f, 0.4f, 1.0f); // warm orange
        Color m_end_color          = Color(1.0f, 0.2f, 0.0f, 0.0f); // red, transparent
        float m_gravity_modifier   = -1.0f;    // negative = downward
        float m_emission_radius    = 0.5f;     // sphere radius in meters
    };
}
