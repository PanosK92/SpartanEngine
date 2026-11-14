// Copyright(c) 2015-2026 Panos Karabelas
// Licensed under the Spartan Engine License. See license.md in the repository root.
// https://github.com/PanosK92/SpartanEngine/blob/master/license.md
// Commercial use requires written permission and negotiated payment terms.
#pragma once

#include "Component.h"
#include "../../math/Vector3.h"
#include <memory>
#include <vector>

namespace spartan
{
    class NavigationWorld;

    // Scene-owned navigation provider. Consumers share this runtime; only this component ticks it.
    class Navigation : public Component
    {
    public:
        bool StartsEarly() const override { return true; }
        Navigation(Entity* entity);
        ~Navigation() override;
        void Start() override;
        void Stop() override;
        void Remove() override;
        void Tick() override;
        void Save(pugi::xml_node& node) override;
        void Load(pugi::xml_node& node) override;

        bool GetEnabled() const { return m_enabled; }
        void SetEnabled(bool enabled);
        bool GetDebugDraw() const { return m_debug_draw; }
        void SetDebugDraw(bool enabled) { m_debug_draw = enabled; }
        bool GetFollowCamera() const { return m_follow_camera; }
        void SetFollowCamera(bool enabled) { m_follow_camera = enabled; }
        void Rebuild();
        // rebuilds the tiles from the current static geometry without dropping agents, e.g. after a wall is toggled
        void Refresh();
        std::shared_ptr<NavigationWorld> GetWorld() const;

        // queries and crowd agents, they fail until the tiles under the given point are built
        bool ProjectPoint(math::Vector3& position) const;
        bool FindPath(const math::Vector3& from, const math::Vector3& to, std::vector<math::Vector3>& points) const;
        int AddAgent(const math::Vector3& position, float speed);
        void RemoveAgent(int agent);
        bool MoveAgent(int agent, const math::Vector3& target);
        bool WanderAgent(int agent, float radius);
        void SetAgentSpeed(int agent, float speed);
        bool GetAgent(int agent, math::Vector3& position, math::Vector3& velocity) const;

    private:
        bool m_debug_draw = false;
        bool m_follow_camera = true;
        std::shared_ptr<NavigationWorld> m_world;
    };
}
