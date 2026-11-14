/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

#pragma once

//= INCLUDES ===================
#include "Component.h"
#include "../../math/Vector3.h"
#include "../../math/Quaternion.h"
#include <deque>
//==============================

namespace spartan
{
    // replicates its entity's world position and rotation in a network session,
    // the owner (0 is the server, clients from 1) sends it and everyone else interpolates it
    class NetworkSync : public Component
    {
    public:
        NetworkSync(Entity* entity);
        ~NetworkSync() override;
        void Save(pugi::xml_node& node) override;
        void Load(pugi::xml_node& node) override;

        // the id peers agree on, entities loaded from the same world share one derived from their object id
        uint32_t GetNetworkId() const;
        void SetNetworkId(uint32_t id);
        int GetOwner() const { return m_owner; }
        void SetOwner(int owner);
        bool GetInterpolate() const { return m_interpolate; }
        void SetInterpolate(bool interpolate) { m_interpolate = interpolate; }
        // true outside a session, so nothing is overwritten while offline
        bool IsLocallyOwned() const;
        bool IsActiveInHierarchy() const;

        // received states, stamped with the local receive time
        void PushState(double time, const math::Vector3& position, const math::Quaternion& rotation);
        bool SampleState(double time, math::Vector3& position, math::Quaternion& rotation) const;
        bool GetLatestState(math::Vector3& position, math::Quaternion& rotation) const;
        void ClearStates() { m_states.clear(); }

    private:
        struct State
        {
            double time = 0.0;
            math::Vector3 position;
            math::Quaternion rotation;
        };

        uint32_t m_network_id = 0;
        int m_owner           = 0;
        bool m_interpolate    = true;
        std::deque<State> m_states;
    };
}
