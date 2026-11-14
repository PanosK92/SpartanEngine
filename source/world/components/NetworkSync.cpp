/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

//= INCLUDES ========================
#include "pch.h"
#include "NetworkSync.h"
#include "../Entity.h"
#include "../../network/Network.h"
#include "../../io/pugixml.hpp"
//===================================

//= NAMESPACES ===============
using namespace std;
using namespace spartan::math;
//============================

namespace spartan
{
    namespace
    {
        // a second of history at the default 20 Hz send rate
        constexpr size_t max_states = 20;
    }

    NetworkSync::NetworkSync(Entity* entity) : Component(entity)
    {
        SP_REGISTER_ATTRIBUTE_VALUE_SET(m_network_id, SetNetworkId, uint32_t);
        SP_REGISTER_ATTRIBUTE_GET_SET(GetOwner, SetOwner, int);
        SP_REGISTER_ATTRIBUTE_GET_SET(GetInterpolate, SetInterpolate, bool);
        Network::Register(this);
    }

    NetworkSync::~NetworkSync()
    {
        Network::Unregister(this);
    }

    uint32_t NetworkSync::GetNetworkId() const
    {
        if (m_network_id != 0)
        {
            return m_network_id;
        }

        // the top bit keeps derived ids clear of the small ones scripts assign
        const uint64_t id = GetEntity()->GetObjectId();
        return static_cast<uint32_t>(id ^ (id >> 32)) | 0x80000000u;
    }

    void NetworkSync::SetNetworkId(uint32_t id)
    {
        m_network_id = id;
        m_states.clear();
    }

    void NetworkSync::SetOwner(int owner)
    {
        m_owner = owner < 0 ? 0 : owner;
        m_states.clear();
    }

    bool NetworkSync::IsLocallyOwned() const
    {
        const int local_id = Network::GetLocalId();
        return local_id < 0 || local_id == m_owner;
    }

    bool NetworkSync::IsActiveInHierarchy() const
    {
        for (Entity* entity = GetEntity(); entity; entity = entity->GetParent())
        {
            if (!entity->IsActive())
            {
                return false;
            }
        }

        return true;
    }

    void NetworkSync::PushState(double time, const Vector3& position, const Quaternion& rotation)
    {
        m_states.push_back({ time, position, rotation });
        while (m_states.size() > max_states)
        {
            m_states.pop_front();
        }
    }

    bool NetworkSync::SampleState(double time, Vector3& position, Quaternion& rotation) const
    {
        if (m_states.empty())
        {
            return false;
        }

        const State& latest = m_states.back();
        if (!m_interpolate || time >= latest.time)
        {
            position = latest.position;
            rotation = latest.rotation;
            return true;
        }

        if (time <= m_states.front().time)
        {
            position = m_states.front().position;
            rotation = m_states.front().rotation;
            return true;
        }

        for (size_t i = m_states.size() - 1; i > 0; i--)
        {
            const State& a = m_states[i - 1];
            const State& b = m_states[i];
            if (time >= a.time)
            {
                const double span = b.time - a.time;
                const float t     = span > 0.0 ? static_cast<float>((time - a.time) / span) : 1.0f;
                position          = Vector3::Lerp(a.position, b.position, t);
                rotation          = Quaternion::Lerp(a.rotation, b.rotation, t);
                return true;
            }
        }

        return false;
    }

    bool NetworkSync::GetLatestState(Vector3& position, Quaternion& rotation) const
    {
        if (m_states.empty())
        {
            return false;
        }

        position = m_states.back().position;
        rotation = m_states.back().rotation;
        return true;
    }

    void NetworkSync::Save(pugi::xml_node& node)
    {
        node.append_attribute("network_id")  = m_network_id;
        node.append_attribute("owner")       = m_owner;
        node.append_attribute("interpolate") = m_interpolate;
    }

    void NetworkSync::Load(pugi::xml_node& node)
    {
        m_network_id  = node.attribute("network_id").as_uint(0);
        m_owner       = node.attribute("owner").as_int(0);
        m_interpolate = node.attribute("interpolate").as_bool(true);
        m_states.clear();
    }
}
