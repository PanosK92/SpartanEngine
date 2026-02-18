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

//= INCLUDES =====
#include "Component.h"
//================

namespace spartan
{
    // behavior when the follower reaches the end of the spline
    enum class SplineFollowMode : uint8_t
    {
        Clamp,    // stop at the end
        Loop,     // jump back to the start and continue
        PingPong, // reverse direction at each end
        Max
    };

    class SplineFollower : public Component
    {
    public:
        SplineFollower(Entity* entity);
        ~SplineFollower() = default;

        // lifecycle
        void Start() override;
        void Stop() override;
        void Tick() override;

        // serialization
        void Save(pugi::xml_node& node) override;
        void Load(pugi::xml_node& node) override;

        // spline entity reference
        uint64_t GetSplineEntityId() const            { return m_spline_entity_id; }
        void SetSplineEntityId(uint64_t id);
        Entity* GetSplineEntity() const               { return m_spline_entity; }

        // movement properties
        float GetSpeed() const                        { return m_speed; }
        void SetSpeed(float speed)                    { m_speed = speed; }
        SplineFollowMode GetFollowMode() const        { return m_follow_mode; }
        void SetFollowMode(SplineFollowMode mode)     { m_follow_mode = mode; }
        bool GetAlignToSpline() const                 { return m_align_to_spline; }
        void SetAlignToSpline(bool align)             { m_align_to_spline = align; }

        // read-only runtime state
        float GetProgress() const                     { return m_progress; }

    private:
        // try to resolve the runtime entity pointer from the stored id
        void ResolveSplineEntity();

        // id of the entity that has the spline component (persisted)
        uint64_t m_spline_entity_id = 0;

        // runtime pointer to the spline entity (not persisted)
        Entity* m_spline_entity = nullptr;

        // movement speed in world units per second
        float m_speed = 5.0f;

        // what happens when the follower reaches the end
        SplineFollowMode m_follow_mode = SplineFollowMode::Loop;

        // orient the entity along the spline tangent
        bool m_align_to_spline = true;

        // current normalized position along the spline [0, 1]
        float m_progress = 0.0f;

        // travel direction: +1 forward, -1 backward (used by ping-pong)
        float m_direction = 1.0f;
    };
}
