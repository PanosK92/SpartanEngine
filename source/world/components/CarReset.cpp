/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

#include "pch.h"
#include "CarReset.h"
#include "SpawnPoint.h"
#include "../Entity.h"
#include "../World.h"
SP_WARNINGS_OFF
#include "../../io/pugixml.hpp"
SP_WARNINGS_ON

namespace spartan
{
    CarReset::CarReset(Entity* entity) : Component(entity)
    {
        SP_REGISTER_ATTRIBUTE_GET_SET(
            GetSpawnPointEntityId,
            SetSpawnPointEntityId,
            uint64_t
        );
    }

    void CarReset::Save(pugi::xml_node& node)
    {
        node.append_attribute("spawn_point_entity_id") =
            m_spawn_point_entity_id;
    }

    void CarReset::Load(pugi::xml_node& node)
    {
        m_spawn_point_entity_id =
            node.attribute("spawn_point_entity_id").as_ullong(0);
        m_spawn_point_entity = nullptr;
    }

    void CarReset::SetSpawnPointEntityId(uint64_t id)
    {
        m_spawn_point_entity_id = id;
        m_spawn_point_entity = nullptr;
    }

    Entity* CarReset::GetSpawnPointEntity()
    {
        if (
            !m_spawn_point_entity &&
            m_spawn_point_entity_id != 0
        )
        {
            m_spawn_point_entity =
                World::GetEntityById(m_spawn_point_entity_id);
        }

        return m_spawn_point_entity;
    }

    SpawnPoint* CarReset::GetSpawnPoint()
    {
        Entity* entity = GetSpawnPointEntity();
        return entity ? entity->GetComponent<SpawnPoint>() : nullptr;
    }
}
