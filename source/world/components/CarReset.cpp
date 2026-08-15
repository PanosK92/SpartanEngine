/*
Copyright(c) 2015-2026 Panos Karabelas

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files(the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and / or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions :

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/

#include "pch.h"
#include "CarReset.h"
#include "SpawnPoint.h"
#include "../Entity.h"
#include "../World.h"
SP_WARNINGS_OFF
#include "../../IO/pugixml.hpp"
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
