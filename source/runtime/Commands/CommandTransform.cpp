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

//= INCLUDES ================
#include "pch.h"
#include "CommandTransform.h"
#include "../World/World.h"
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

        // In the current implementation of GetObjectId, it may seem unnecessary to not just store a (shared) pointer to the entity
        // however if we ever move to a UUID based system, or hashed name system, and entities can be destroyed/created or unloaded/loaded with consistent ids
        // we want to actually store the ID and then resolve from that.
        // Right now this wont work as expected, since the object ids are just incremented on creation
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
        shared_ptr<Entity> entity = World::GetEntityById(m_entity_id);
        if (!entity)
            return;

        entity->SetPosition(m_new_position);
        entity->SetRotation(m_new_rotation);
        entity->SetScale(m_new_scale);
    }

    void CommandTransform::OnRevert()
    {
        shared_ptr<Entity> entity = World::GetEntityById(m_entity_id);
        if (!entity)
            return;

        entity->SetPosition(m_old_position);
        entity->SetRotation(m_old_rotation);
        entity->SetScale(m_old_scale);
    }
}
