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

//= INCLUDES ===================
#include "pch.h"
#include "CommandEntityDelete.h"
#include "../World/Entity.h"
#include "../World/World.h"
SP_WARNINGS_OFF
#include "../IO/pugixml.hpp"
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
            return;

        m_entity_id = entity->GetObjectId();

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
        entity->Load(entity_node);

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
