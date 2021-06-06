/*
Copyright(c) 2016-2021 Panos Karabelas

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

//= INCLUDES ================================
#include "Spartan.h"
#include "TransformGizmo.h"
#include "../../RHI/RHI_IndexBuffer.h"
#include "../../Input/Input.h"
#include "../../World/World.h"
#include "../../World/Entity.h"
#include "../../World/Components/Camera.h"
#include "../../World/Components/Transform.h"
#include "TransformPosition.h"
#include "TransformScale.h"
#include "TransformRotation.h"
//===========================================

//= NAMESPACES ===============
using namespace std;
using namespace Spartan::Math;
//============================

namespace Spartan
{
    TransformGizmo::TransformGizmo(Context* context)
    {
        m_context       = context;
        m_input         = context->GetSubsystem<Input>();
        m_world         = context->GetSubsystem<World>();
        m_type          = TransformHandleType::Position;
        m_space         = TransformHandleSpace::World;
        m_is_editing    = false;

        m_handles[TransformHandleType::Position]    = make_shared<TransformPosition>(context);
        m_handles[TransformHandleType::Scale]       = make_shared<TransformScale>(context);
        m_handles[TransformHandleType::Rotation]    = make_shared<TransformRotation>(context);
    }

    bool TransformGizmo::Tick(Camera* camera, const float handle_size, const float handle_speed)
    {
        shared_ptr<Entity> selected_entity = m_entity_selected.lock();

        // If there isn't a camera or an entity, ignore input
        if (!camera || !selected_entity)
        {
            m_is_editing = false;
            return false;
        }

        // If the selected entity is the camera itself, ignore input
        if (selected_entity->GetObjectId() == camera->GetTransform()->GetEntity()->GetObjectId())
        {
            m_is_editing = false;
            return false;
        }

        // Switch between position, rotation and scale handles, with W, E and R respectively
        if (!camera->IsFpsControlled())
        {
            if (m_input->GetKeyDown(KeyCode::W))
            {
                m_type = TransformHandleType::Position;
            }
            else if (m_input->GetKeyDown(KeyCode::E))
            {
                m_type = TransformHandleType::Scale;
            }
            else if (m_input->GetKeyDown(KeyCode::R))
            {
                m_type = TransformHandleType::Rotation;
            }
        }

        m_handles[m_type]->Tick(m_space, selected_entity.get(), camera, handle_size, handle_speed);
        m_is_editing = m_handles[m_type]->IsEditing();

        // Finally, render the currently selected transform handle only if it hash mashes.
        // e.g. the rotation transform does it's own custom rendering.
        return m_handles[m_type]->HasModel();
    }

    weak_ptr<Spartan::Entity> TransformGizmo::SetSelectedEntity(const shared_ptr<Entity>& entity)
    {
        // Set a new entity only if another is not being edited
        if (!m_is_editing)
        {
            // If in front of the entity the handles from the previous entity
            // are actual being hovered, then a selection not selected the new entity.
            if (!m_handles[m_type]->IsHovered())
            {
                m_entity_selected = entity;
            }
        }

        return m_entity_selected;
    }

    uint32_t TransformGizmo::GetIndexCount()
    {
        return m_handles[m_type]->GetIndexBuffer()->GetIndexCount();
    }

    const RHI_VertexBuffer* TransformGizmo::GetVertexBuffer()
    {
        return m_handles[m_type]->GetVertexBuffer();
    }

    const RHI_IndexBuffer* TransformGizmo::GetIndexBuffer()
    {
        return m_handles[m_type]->GetIndexBuffer();
    }

     const TransformHandle* TransformGizmo::GetHandle()
     {
         return m_handles[m_type].get();
     }
}
