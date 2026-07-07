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
#include "Render.h"
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
        SP_REGISTER_ATTRIBUTE_GET_SET(GetSplineEntityId, SetSplineEntityId, uint64_t);
        SP_REGISTER_ATTRIBUTE_GET_SET(GetSpeed,          SetSpeed,          float);
        SP_REGISTER_ATTRIBUTE_GET_SET(GetFollowMode,     SetFollowMode,     SplineFollowMode);
        SP_REGISTER_ATTRIBUTE_GET_SET(GetAlignToSpline,  SetAlignToSpline,  bool);
        SP_REGISTER_ATTRIBUTE_GET_SET(GetFlipForward,    SetFlipForward,    bool);
        SP_REGISTER_ATTRIBUTE_GET_SET(GetProgress,       SetProgress,       float);
        SP_REGISTER_ATTRIBUTE_GET_SET(GetAnimateWheels,  SetAnimateWheels,  bool);
        SP_REGISTER_ATTRIBUTE_GET_SET(GetWheelRadius,    SetWheelRadius,    float);
        SP_REGISTER_ATTRIBUTE_GET_SET(GetMaxSteerAngle,  SetMaxSteerAngle,  float);
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
        m_wheels_resolved = false;
        m_wheels.clear();
        ResolveSplineEntity();
    }

    void SplineFollower::Stop()
    {
        m_spline_entity   = nullptr;
        m_wheels_resolved = false;
        m_wheels.clear();
    }

    void SplineFollower::Tick()
    {
        // only move during play mode
        if (!Engine::IsFlagSet(EngineMode::Playing))
        {
            return;
        }

        Spline* spline = GetValidSpline();
        if (!spline)
        {
            return;
        }

        float delta_time = static_cast<float>(Timer::GetDeltaTimeSec());

        // advance progress
        m_progress += (m_speed * delta_time * m_direction) / spline->GetLength();

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

        ApplyProgress(spline);
    }

    void SplineFollower::SetTime(float seconds)
    {
        Spline* spline = GetValidSpline();
        if (!spline)
        {
            return;
        }

        // deterministic mapping so scrubbing backwards works too
        m_progress = max(0.0f, min((m_speed * seconds) / spline->GetLength(), 1.0f));
        ApplyProgress(spline);
    }

    void SplineFollower::ApplyProgress(Spline* spline)
    {
        // progress is a distance fraction, convert to parametric t so world speed stays constant on uneven control points
        float t = spline->GetTAtDistance(m_progress * spline->GetLength(), 32);

        // set position along the spline
        Vector3 position = spline->GetPoint(t);
        GetEntity()->SetPosition(position);

        // optionally orient the entity along the tangent
        if (m_align_to_spline)
        {
            Vector3 tangent = spline->GetTangent(t);
            if (tangent.LengthSquared() > 0.0f)
            {
                tangent.Normalize();
                if (m_flip_forward)
                {
                    tangent = -tangent;
                }
                GetEntity()->SetRotation(Quaternion::FromLookRotation(tangent, Vector3::Up));
            }
        }

        // spin the wheels with the travelled distance and steer the front ones into the turn
        UpdateWheels(spline, t);
    }

    void SplineFollower::ResolveWheels()
    {
        m_wheels.clear();
        m_wheels_resolved = true;

        if (!m_animate_wheels)
        {
            return;
        }

        Entity* car = GetEntity();
        std::vector<Entity*> descendants;
        car->GetDescendants(&descendants);

        // gather every part whose name marks it as a wheel or tire
        // the imported car nests the renderable on a child, so match by name and read bounds from any renderable under it
        const Quaternion car_rot_inverse = car->GetRotation().Inverse();
        const Vector3 car_position       = car->GetPosition();
        std::vector<float> local_forward;
        float radius_estimate = 0.0f;
        for (Entity* entity : descendants)
        {
            std::string name = entity->GetObjectName();
            transform(name.begin(), name.end(), name.begin(), [](unsigned char c) { return static_cast<char>(tolower(c)); });
            if (name.find("tire") == std::string::npos && name.find("wheel") == std::string::npos)
            {
                continue;
            }

            WheelState wheel;
            wheel.entity      = entity;
            wheel.rest_offset = car_rot_inverse * entity->GetRotation();
            m_wheels.push_back(wheel);

            // forward position in the car local frame, used to split front from rear
            local_forward.push_back((car_rot_inverse * (entity->GetPosition() - car_position)).z);

            // estimate the wheel radius from the largest half extent of the wheel bounds
            Render* render = entity->GetComponent<Render>();
            if (!render)
            {
                std::vector<Entity*> wheel_parts;
                entity->GetDescendants(&wheel_parts);
                for (Entity* wheel_part : wheel_parts)
                {
                    if ((render = wheel_part->GetComponent<Render>()))
                    {
                        break;
                    }
                }
            }
            if (render)
            {
                const Vector3 extents = render->GetBoundingBox().GetExtents();
                radius_estimate       = max(radius_estimate, max(extents.x, max(extents.y, extents.z)));
            }
        }

        if (m_wheels.empty())
        {
            return;
        }

        // wheels ahead of the midpoint are the steering front wheels
        float forward_min = local_forward[0];
        float forward_max = local_forward[0];
        for (float value : local_forward)
        {
            forward_min = min(forward_min, value);
            forward_max = max(forward_max, value);
        }
        const float forward_mid = (forward_min + forward_max) * 0.5f;
        const float travel_sign = m_flip_forward ? -1.0f : 1.0f;
        for (size_t i = 0; i < m_wheels.size(); i++)
        {
            m_wheels[i].is_front = (local_forward[i] * travel_sign) > (forward_mid * travel_sign);
        }

        m_wheel_radius_active = m_wheel_radius > 0.0f ? m_wheel_radius : (radius_estimate > 0.0f ? radius_estimate : 0.35f);
    }

    float SplineFollower::ComputeSteerAngle(Spline* spline, float t) const
    {
        // compare the heading now with the heading a short distance ahead
        const float length    = spline->GetLength();
        const float lookahead = 3.0f;
        const float distance  = min(m_progress * length + lookahead, length);
        const float t_ahead   = spline->GetTAtDistance(distance, 32);

        Vector3 tangent_now   = spline->GetTangent(t);
        Vector3 tangent_ahead = spline->GetTangent(t_ahead);
        if (tangent_now.LengthSquared() <= 0.0f || tangent_ahead.LengthSquared() <= 0.0f)
        {
            return 0.0f;
        }
        tangent_now.Normalize();
        tangent_ahead.Normalize();
        if (m_flip_forward)
        {
            tangent_now   = -tangent_now;
            tangent_ahead = -tangent_ahead;
        }

        // signed yaw difference on the horizontal plane
        const float heading_now   = atan2f(tangent_now.x, tangent_now.z);
        const float heading_ahead = atan2f(tangent_ahead.x, tangent_ahead.z);
        float delta               = heading_ahead - heading_now;
        while (delta > math::pi)  { delta -= math::pi * 2.0f; }
        while (delta < -math::pi) { delta += math::pi * 2.0f; }

        const float max_steer = m_max_steer_angle * math::deg_to_rad;
        const float steer     = delta * 2.5f; // exaggerate so gentle curves read on the wheels
        return max(-max_steer, min(steer, max_steer));
    }

    void SplineFollower::UpdateWheels(Spline* spline, float t)
    {
        if (!m_animate_wheels)
        {
            return;
        }

        if (!m_wheels_resolved)
        {
            ResolveWheels();
        }
        if (m_wheels.empty())
        {
            return;
        }

        const Quaternion car_rot = GetEntity()->GetRotation();
        const Vector3 car_right  = car_rot * Vector3::Right;
        const Vector3 car_up     = car_rot * Vector3::Up;

        // roll is deterministic from the travelled distance so scrubbing matches playback
        const float length   = spline->GetLength();
        const float radius   = max(m_wheel_radius > 0.0f ? m_wheel_radius : m_wheel_radius_active, 0.01f);
        const float roll      = (m_progress * length / radius) * (m_flip_forward ? -1.0f : 1.0f);
        const Quaternion spin = Quaternion::FromAxisAngle(car_right, roll);

        const float steer         = ComputeSteerAngle(spline, t);
        const Quaternion steer_q  = Quaternion::FromAxisAngle(car_up, steer);

        for (const WheelState& wheel : m_wheels)
        {
            if (!wheel.entity)
            {
                continue;
            }
            const Quaternion base  = car_rot * wheel.rest_offset;
            const Quaternion final = (wheel.is_front ? steer_q : Quaternion::Identity) * spin * base;
            wheel.entity->SetRotation(final);
        }
    }

    Spline* SplineFollower::GetValidSpline()
    {
        // resolve the spline entity pointer if needed
        if (!m_spline_entity)
        {
            ResolveSplineEntity();
            if (!m_spline_entity)
            {
                return nullptr;
            }
        }

        // grab the spline component from the referenced entity
        Spline* spline = m_spline_entity->GetComponent<Spline>();
        if (!spline || spline->GetControlPointCount() < 2)
        {
            return nullptr;
        }

        // arc length is needed to convert speed to normalized progress
        if (spline->GetLength() <= 0.0f)
        {
            return nullptr;
        }

        return spline;
    }

    void SplineFollower::Save(pugi::xml_node& node)
    {
        node.append_attribute("spline_entity_id") = m_spline_entity_id;
        node.append_attribute("speed")            = m_speed;
        node.append_attribute("follow_mode")      = static_cast<uint32_t>(m_follow_mode);
        node.append_attribute("align_to_spline")  = m_align_to_spline;
        node.append_attribute("flip_forward")     = m_flip_forward;
        node.append_attribute("animate_wheels")   = m_animate_wheels;
        node.append_attribute("wheel_radius")     = m_wheel_radius;
        node.append_attribute("max_steer_angle")  = m_max_steer_angle;
    }

    void SplineFollower::Load(pugi::xml_node& node)
    {
        m_spline_entity_id = node.attribute("spline_entity_id").as_ullong(0);
        m_speed            = node.attribute("speed").as_float(5.0f);
        m_follow_mode      = static_cast<SplineFollowMode>(node.attribute("follow_mode").as_uint(static_cast<uint32_t>(SplineFollowMode::Loop)));
        m_align_to_spline  = node.attribute("align_to_spline").as_bool(true);
        m_flip_forward     = node.attribute("flip_forward").as_bool(false);
        m_animate_wheels   = node.attribute("animate_wheels").as_bool(true);
        m_wheel_radius     = node.attribute("wheel_radius").as_float(0.0f);
        m_max_steer_angle  = node.attribute("max_steer_angle").as_float(35.0f);

        // the entity pointer will be resolved on the first tick or when play starts
        m_spline_entity   = nullptr;
        m_wheels_resolved = false;
    }

    void SplineFollower::ResolveSplineEntity()
    {
        if (m_spline_entity_id != 0)
        {
            m_spline_entity = World::GetEntityById(m_spline_entity_id);
        }
    }
}
