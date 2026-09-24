/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

//= INCLUDES ================
#include "pch.h"
#include "CommandTransform.h"
#include "../world/World.h"
//===========================

//= NAMESPACES ===============
using namespace std;
using namespace spartan::math;
//============================

namespace spartan
{
    CommandTransform::CommandTransform(Entity* entity, Vector3 old_position, Quaternion old_rotation, Vector3 old_scale)
    {
        SP_ASSERT(entity);

        // @todo store the id, not a pointer, so this survives a move to uuids where entities can be unloaded and reloaded
        m_entity_id = entity->GetObjectId();

        m_old_position = old_position;
        m_old_rotation = old_rotation;
        m_old_scale    = old_scale;

        m_new_position = entity->GetPosition();
        m_new_rotation = entity->GetRotation();
        m_new_scale    = entity->GetScale();
    }

    void CommandTransform::OnApply()
    {
        Entity* entity = World::GetEntityById(m_entity_id);
        if (!entity)
            return;

        entity->SetPosition(m_new_position);
        entity->SetRotation(m_new_rotation);
        entity->SetScale(m_new_scale);
    }

    void CommandTransform::OnRevert()
    {
        Entity* entity = World::GetEntityById(m_entity_id);
        if (!entity)
            return;

        entity->SetPosition(m_old_position);
        entity->SetRotation(m_old_rotation);
        entity->SetScale(m_old_scale);
    }
}
