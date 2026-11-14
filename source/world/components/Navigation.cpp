// Copyright(c) 2015-2026 Panos Karabelas
// Licensed under the Spartan Engine License. See license.md in the repository root.
// https://github.com/PanosK92/SpartanEngine/blob/master/license.md
// Commercial use requires written permission and negotiated payment terms.
#include "pch.h"
#include "Navigation.h"
#include "Camera.h"
#include "../Entity.h"
#include "../World.h"
#include "../../navigation/NavigationWorld.h"
#include "../../io/pugixml.hpp"

namespace spartan
{
    Navigation::Navigation(Entity* entity) : Component(entity)
    {
        SP_REGISTER_ATTRIBUTE_GET_SET(GetEnabled, SetEnabled, bool);
        SP_REGISTER_ATTRIBUTE_GET_SET(GetDebugDraw, SetDebugDraw, bool);
        SP_REGISTER_ATTRIBUTE_GET_SET(GetFollowCamera, SetFollowCamera, bool);
    }

    Navigation::~Navigation() = default;
    void Navigation::Start() { Rebuild(); }
    void Navigation::Stop() { m_world.reset(); }
    void Navigation::Remove() { Stop(); }

    void Navigation::SetEnabled(bool enabled)
    {
        m_enabled = enabled;
        if (!enabled) Stop();
    }

    std::shared_ptr<NavigationWorld> Navigation::GetWorld() const
    {
        if (!m_enabled) return nullptr;
        for (Entity* entity = GetEntity(); entity; entity = entity->GetParent())
            if (!entity->IsActive()) return nullptr;
        return m_world;
    }

    void Navigation::Rebuild()
    {
        Stop();
        if (m_enabled && (Engine::IsFlagSet(EngineMode::Playing) || m_debug_draw) && !World::IsLoadingFromFile() && !World::IsPreparing())
            m_world = std::make_shared<NavigationWorld>();
    }

    void Navigation::Refresh()
    {
        if (std::shared_ptr<NavigationWorld> world = GetWorld()) world->Refresh();
    }

    bool Navigation::ProjectPoint(math::Vector3& position) const
    {
        std::shared_ptr<NavigationWorld> world = GetWorld();
        return world && world->Project(position);
    }

    bool Navigation::FindPath(const math::Vector3& from, const math::Vector3& to, std::vector<math::Vector3>& points) const
    {
        points.clear();
        std::shared_ptr<NavigationWorld> world = GetWorld();
        std::vector<navigation::Point> corners;
        if (!world || !world->GetMesh().FindPath({from.x, from.y, from.z}, {to.x, to.y, to.z}, corners)) return false;
        for (const navigation::Point& corner : corners) points.emplace_back(corner[0], corner[1], corner[2]);
        return true;
    }

    int Navigation::AddAgent(const math::Vector3& position, float speed)
    {
        std::shared_ptr<NavigationWorld> world = GetWorld();
        return world ? world->GetMesh().AddAgent({position.x, position.y, position.z}, speed) : -1;
    }

    void Navigation::RemoveAgent(int agent)
    {
        if (std::shared_ptr<NavigationWorld> world = GetWorld()) world->GetMesh().RemoveAgent(agent);
    }

    bool Navigation::MoveAgent(int agent, const math::Vector3& target)
    {
        std::shared_ptr<NavigationWorld> world = GetWorld();
        return world && world->GetMesh().MoveTo(agent, {target.x, target.y, target.z});
    }

    bool Navigation::WanderAgent(int agent, float radius)
    {
        std::shared_ptr<NavigationWorld> world = GetWorld();
        return world && world->GetMesh().Wander(agent, radius);
    }

    void Navigation::SetAgentSpeed(int agent, float speed)
    {
        if (std::shared_ptr<NavigationWorld> world = GetWorld()) world->GetMesh().SetAgentSpeed(agent, speed);
    }

    bool Navigation::GetAgent(int agent, math::Vector3& position, math::Vector3& velocity) const
    {
        std::shared_ptr<NavigationWorld> world = GetWorld();
        navigation::Point p, v;
        if (!world || !world->GetMesh().GetAgent(agent, p, v)) return false;
        position = math::Vector3(p[0], p[1], p[2]);
        velocity = math::Vector3(v[0], v[1], v[2]);
        return true;
    }

    void Navigation::Tick()
    {
        if (!m_enabled || World::IsLoadingFromFile() || World::IsPreparing()) return;
        const bool playing = Engine::IsFlagSet(EngineMode::Playing);
        const bool simulate = playing && !Engine::IsFlagSet(EngineMode::Paused);
        // Preview navigation in edit mode, and keep it visible while simulation is paused.
        if (!playing && !m_debug_draw) { Stop(); return; }
        // Also supports adding/enabling the component during play, after Entity::Start has run.
        if (!m_world) Rebuild();
        if (!GetWorld()) return;
        math::Vector3 focus = GetEntity()->GetPosition();
        if (m_follow_camera)
            if (Camera* camera = World::GetCamera()) focus = camera->GetEntity()->GetPosition();
        m_world->Tick(focus, simulate ? static_cast<float>(Timer::GetDeltaTimeSec()) : 0.0f, m_debug_draw);
    }

    void Navigation::Save(pugi::xml_node& node)
    {
        node.append_attribute("enabled") = m_enabled;
        node.append_attribute("debug_draw") = m_debug_draw;
        node.append_attribute("follow_camera") = m_follow_camera;
    }

    void Navigation::Load(pugi::xml_node& node)
    {
        Stop();
        m_enabled = node.attribute("enabled").as_bool(true);
        m_debug_draw = node.attribute("debug_draw").as_bool(false);
        m_follow_camera = node.attribute("follow_camera").as_bool(true);
    }
}
