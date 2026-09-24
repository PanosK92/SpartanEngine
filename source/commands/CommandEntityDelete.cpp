/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

//= INCLUDES ===================
#include "pch.h"
#include <sstream>
#include "CommandEntityDelete.h"
#include "../world/Entity.h"
#include "../world/World.h"
#include "../world/components/Terrain.h"
SP_WARNINGS_OFF
#include "../io/pugixml.hpp"
SP_WARNINGS_ON
//==============================

//= NAMESPACES =====
using namespace std;
//==================

namespace spartan
{
    CommandEntityDelete::CommandEntityDelete(Entity* entity)
    {
        if (!entity)
        {
            return;
        }

        m_entity_id = entity->GetObjectId();
        std::vector<Entity*> descendants;
        entity->GetDescendants(&descendants);
        descendants.push_back(entity);
        for (auto child : descendants)
            if (auto terrain = child->GetComponent<Terrain>())
                m_sculpt_snapshots[child->GetObjectId()] = terrain->GetSculptSnapshot();

        // store parent id for restoring hierarchy
        if (Entity* parent = entity->GetParent())
        {
            m_parent_id = parent->GetObjectId();
        }

        // serialize entity to xml string
        pugi::xml_document doc;
        pugi::xml_node root = doc.append_child("Entity");
        entity->Save(root);

        // convert to string
        ostringstream oss;
        doc.save(oss);
        m_entity_xml = oss.str();
    }

    void CommandEntityDelete::OnApply()
    {
        // delete the entity
        if (Entity* entity = World::GetEntityById(m_entity_id))
        {
            World::RemoveEntity(entity);
        }
    }

    void CommandEntityDelete::OnRevert()
    {
        // A delete followed by undo in the same frame has not destroyed anything
        // yet. Cancel that removal instead of creating a second entity with the same id.
        if (Entity* existing = World::GetEntityById(m_entity_id); existing && World::CancelPendingRemoval(existing))
        {
            existing->SetParent(nullptr);
            existing->SetParent(World::GetEntityById(m_parent_id));
            return;
        }
        // parse xml
        pugi::xml_document doc;
        pugi::xml_parse_result result = doc.load_string(m_entity_xml.c_str());
        if (!result)
        {
            SP_LOG_ERROR("Failed to parse entity xml for undo: %s", result.description());
            return;
        }

        // create entity and load from xml
        Entity* entity = World::CreateEntity();
        pugi::xml_node entity_node = doc.child("Entity");
        entity->Load(entity_node, true, &m_sculpt_snapshots);

        // restore parent relationship
        if (m_parent_id != 0)
        {
            if (Entity* parent = World::GetEntityById(m_parent_id))
            {
                entity->SetParent(parent);
            }
        }
    }
}
