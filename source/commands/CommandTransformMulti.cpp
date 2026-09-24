/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

//= INCLUDES ===================
#include "pch.h"
#include "CommandTransformMulti.h"
#include "../world/World.h"
//==============================

//= NAMESPACES ===============
using namespace std;
using namespace spartan::math;
//============================

namespace spartan
{
    CommandTransformMulti::CommandTransformMulti(
        const vector<Entity*>& entities,
        const vector<Vector3>& old_positions,
        const vector<Quaternion>& old_rotations,
        const vector<Vector3>& old_scales
    )
    {
        SP_ASSERT(entities.size() == old_positions.size());
        SP_ASSERT(entities.size() == old_rotations.size());
        SP_ASSERT(entities.size() == old_scales.size());

        m_transforms.reserve(entities.size());

        for (size_t i = 0; i < entities.size(); ++i)
        {
            if (!entities[i])
            {
                continue;
            }

            EntityTransformData transform;
            transform.entity_id    = entities[i]->GetObjectId();
            transform.old_position = old_positions[i];
            transform.old_rotation = old_rotations[i];
            transform.old_scale    = old_scales[i];
            transform.new_position = entities[i]->GetPosition();
            transform.new_rotation = entities[i]->GetRotation();
            transform.new_scale    = entities[i]->GetScale();

            m_transforms.push_back(transform);
        }
    }

    void CommandTransformMulti::OnApply()
    {
        for (const auto& transform : m_transforms)
        {
            Entity* entity = World::GetEntityById(transform.entity_id);
            if (!entity)
            {
                continue;
            }

            entity->SetPosition(transform.new_position);
            entity->SetRotation(transform.new_rotation);
            entity->SetScale(transform.new_scale);
        }
    }

    void CommandTransformMulti::OnRevert()
    {
        for (const auto& transform : m_transforms)
        {
            Entity* entity = World::GetEntityById(transform.entity_id);
            if (!entity)
            {
                continue;
            }

            entity->SetPosition(transform.old_position);
            entity->SetRotation(transform.old_rotation);
            entity->SetScale(transform.old_scale);
        }
    }
}

