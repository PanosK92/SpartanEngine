/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

#include "pch.h"
#include "SpawnPoint.h"
#include "Physics.h"
#include "../Entity.h"

namespace spartan
{
    SpawnPoint::SpawnPoint(Entity* entity) : Component(entity)
    {

    }

    void SpawnPoint::Place(Entity* entity) const
    {
        if (!entity || !m_entity_ptr)
        {
            return;
        }

        const math::Vector3 position = m_entity_ptr->GetPosition();
        const math::Quaternion rotation = m_entity_ptr->GetRotation();

        if (Physics* physics = entity->GetComponent<Physics>())
        {
            physics->SetBodyTransform(position, rotation);
            return;
        }

        entity->SetPosition(position);
        entity->SetRotation(rotation);
    }
}
