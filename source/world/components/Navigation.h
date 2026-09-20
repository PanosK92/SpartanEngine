// Copyright(c) 2015-2026 Panos Karabelas
#pragma once

#include "Component.h"
#include <memory>

namespace spartan
{
    class NavigationWorld;

    // Scene-owned navigation provider. Consumers share this runtime; only this component ticks it.
    class Navigation : public Component
    {
    public:
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
        std::shared_ptr<NavigationWorld> GetWorld() const;

    private:
        bool m_debug_draw = false;
        bool m_follow_camera = true;
        std::shared_ptr<NavigationWorld> m_world;
    };
}
