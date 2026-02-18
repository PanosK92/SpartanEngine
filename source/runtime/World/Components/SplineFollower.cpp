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

//= INCLUDES ======================
#include "pch.h"
#include "SplineFollower.h"
#include "Spline.h"
#include "../Entity.h"
#include "../World.h"
#include "../../Math/Quaternion.h"
SP_WARNINGS_OFF
#include "../../IO/pugixml.hpp"
SP_WARNINGS_ON
//=================================

//= NAMESPACES ===============
using namespace std;
using namespace spartan::math;
//============================

namespace spartan
{
    SplineFollower::SplineFollower(Entity* entity) : Component(entity)
    {
        SP_REGISTER_ATTRIBUTE_GET_SET(GetSpeed,          SetSpeed,          float);
        SP_REGISTER_ATTRIBUTE_GET_SET(GetAlignToSpline,  SetAlignToSpline,  bool);
    }

    void SplineFollower::SetSplineEntityId(uint64_t id)
    {
        m_spline_entity_id = id;
        m_spline_entity    = nullptr; // invalidate so it gets resolved on next tick
    }

    void SplineFollower::Start()
    {
        m_progress  = 0.0f;
        m_direction = 1.0f;
        ResolveSplineEntity();
    }

    void SplineFollower::Stop()
    {
        m_spline_entity = nullptr;
    }

    void SplineFollower::Tick()
    {
        // only move during play mode
        if (!Engine::IsFlagSet(EngineMode::Playing))
            return;

        // resolve the spline entity pointer if needed
        if (!m_spline_entity)
        {
            ResolveSplineEntity();
            if (!m_spline_entity)
                return;
        }

        // grab the spline component from the referenced entity
        Spline* spline = m_spline_entity->GetComponent<Spline>();
        if (!spline || spline->GetControlPointCount() < 2)
            return;

        // compute arc length so speed is in world units per second
        float spline_length = spline->GetLength();
        if (spline_length <= 0.0f)
            return;

        float delta_time = static_cast<float>(Timer::GetDeltaTimeSec());

        // advance progress
        m_progress += (m_speed * delta_time * m_direction) / spline_length;

        // apply follow mode
        switch (m_follow_mode)
        {
        case SplineFollowMode::Clamp:
        {
            m_progress = max(0.0f, min(m_progress, 1.0f));
        } break;

        case SplineFollowMode::Loop:
        {
            m_progress = m_progress - floorf(m_progress); // wrap to [0, 1)
        } break;

        case SplineFollowMode::PingPong:
        {
            if (m_progress >= 1.0f)
            {
                m_progress  = 1.0f;
                m_direction = -1.0f;
            }
            else if (m_progress <= 0.0f)
            {
                m_progress  = 0.0f;
                m_direction = 1.0f;
            }
        } break;

        default:
            break;
        }

        // set position along the spline
        Vector3 position = spline->GetPoint(m_progress);
        GetEntity()->SetPosition(position);

        // optionally orient the entity along the tangent
        if (m_align_to_spline)
        {
            Vector3 tangent = spline->GetTangent(m_progress);
            if (tangent.LengthSquared() > 0.0f)
            {
                tangent.Normalize();
                GetEntity()->SetRotation(Quaternion::FromLookRotation(tangent, Vector3::Up));
            }
        }
    }

    void SplineFollower::Save(pugi::xml_node& node)
    {
        node.append_attribute("spline_entity_id") = m_spline_entity_id;
        node.append_attribute("speed")            = m_speed;
        node.append_attribute("follow_mode")      = static_cast<uint32_t>(m_follow_mode);
        node.append_attribute("align_to_spline")  = m_align_to_spline;
    }

    void SplineFollower::Load(pugi::xml_node& node)
    {
        m_spline_entity_id = node.attribute("spline_entity_id").as_ullong(0);
        m_speed            = node.attribute("speed").as_float(5.0f);
        m_follow_mode      = static_cast<SplineFollowMode>(node.attribute("follow_mode").as_uint(static_cast<uint32_t>(SplineFollowMode::Loop)));
        m_align_to_spline  = node.attribute("align_to_spline").as_bool(true);

        // the entity pointer will be resolved on the first tick or when play starts
        m_spline_entity = nullptr;
    }

    void SplineFollower::ResolveSplineEntity()
    {
        if (m_spline_entity_id != 0)
        {
            m_spline_entity = World::GetEntityById(m_spline_entity_id);
        }
    }
}
