/*
Copyright(c) 2023 Fredrik Svantesson

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

