/*
Copyright(c) 2016-2022 Panos Karabelas

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
#include "TransformHandle.h"
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
    TransformHandle::TransformHandle(Context* context)
    {
        m_context    = context;
        m_input      = context->GetSubsystem<Input>();
        m_world      = context->GetSubsystem<World>();
        m_type       = TransformHandleType::Position;
        m_space      = TransformHandleSpace::World;
        m_is_editing = false;

        m_transform_operator[TransformHandleType::Position] = make_shared<TransformPosition>(context);
        m_transform_operator[TransformHandleType::Rotation] = make_shared<TransformRotation>(context);
        m_transform_operator[TransformHandleType::Scale]    = make_shared<TransformScale>(context);
    }

    bool TransformHandle::Tick(Camera* camera, const float handle_size)
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
                m_type = TransformHandleType::Rotation;
            }
            else if (m_input->GetKeyDown(KeyCode::R))
            {
                m_type = TransformHandleType::Scale;
            }
        }

        // Update operators
        m_transform_operator[m_type]->Tick(m_space, selected_entity.get(), camera, handle_size);

        m_is_editing = m_transform_operator[m_type]->IsEditing();

        return true;
    }

    weak_ptr<Entity> TransformHandle::SetSelectedEntity(const shared_ptr<Entity>& entity)
    {
        // If this a camera entity don't select it
        if (entity->GetComponent<Camera>())
        {
            return m_entity_selected;
        }

        // Set a new entity only if another is not being edited
        if (!m_is_editing)
        {
            // If in front of the entity the handles from the previous entity
            // are actual being hovered, then a selection not selected the new entity.
            if (!m_transform_operator[m_type]->IsHovered())
            {
                m_entity_selected = entity;
            }
        }

        return m_entity_selected;
    }

    uint32_t TransformHandle::GetIndexCount()
    {
        return m_transform_operator[m_type]->GetIndexBuffer()->GetIndexCount();
    }

    const RHI_VertexBuffer* TransformHandle::GetVertexBuffer()
    {
        return m_transform_operator[m_type]->GetVertexBuffer();
    }

    const RHI_IndexBuffer* TransformHandle::GetIndexBuffer()
    {
        return m_transform_operator[m_type]->GetIndexBuffer();
    }

    const TransformOperator* TransformHandle::GetHandle()
    {
        return m_transform_operator[m_type].get();
    }
}
