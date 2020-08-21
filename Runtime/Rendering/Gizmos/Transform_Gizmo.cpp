/*
Copyright(c) 2016-2020 Panos Karabelas

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
#include "Transform_Gizmo.h"
#include "../Model.h"
#include "../../RHI/RHI_IndexBuffer.h"
#include "../../Input/Input.h"
#include "../../World/World.h"
#include "../../World/Entity.h"
#include "../../World/Components/Camera.h"
#include "../../World/Components/Transform.h"
//===========================================

//============================
using namespace std;
using namespace Spartan::Math;
//============================

namespace Spartan
{
    Transform_Gizmo::Transform_Gizmo(Context* context)
    {
        m_context        = context;
        m_input            = m_context->GetSubsystem<Input>();
        m_world            = m_context->GetSubsystem<World>();
        m_type            = TransformHandle_Position;
        m_space            = TransformHandle_World;
        m_is_editing    = false;

        // Handles
        m_handle_position.Initialize(TransformHandle_Position, context);
        m_handle_rotation.Initialize(TransformHandle_Rotation, context);
        m_handle_scale.Initialize(TransformHandle_Scale, context);
    }

    std::weak_ptr<Spartan::Entity> Transform_Gizmo::SetSelectedEntity(const shared_ptr<Entity>& entity)
    {
        // Update picked entity only when it's not being edited
        if (!m_is_editing && !m_just_finished_editing)
        {
            m_entity_selected = entity;
        }

        return m_entity_selected;
    }

    bool Transform_Gizmo::Update(Camera* camera, const float handle_size, const float handle_speed)
    {
        m_just_finished_editing = false;

        Entity* selected_entity = m_entity_selected.lock().get();

        // If there is no camera, don't even bother
        if (!camera || !selected_entity)
        {
            m_is_editing = false;
            return false;
        }

        // If the selected entity is the actual viewport camera, ignore the input
        if (selected_entity->GetId() == camera->GetTransform()->GetEntity()->GetId())
        {
            m_is_editing = false;
            return false;
        }

        // Switch between position, rotation and scale handles, with W, E and R respectively
        if (m_input->GetKeyDown(KeyCode::W))
        {
            m_type = TransformHandle_Position;
        }
        else if (m_input->GetKeyDown(KeyCode::E))
        {
            m_type = TransformHandle_Scale;
        }
        else if (m_input->GetKeyDown(KeyCode::R))
        {
            m_type = TransformHandle_Rotation;
        }

        const bool was_editing = m_is_editing;

        // Update appropriate handle
        if (m_type == TransformHandle_Position)
        {
            m_is_editing = m_handle_position.Update(m_space, selected_entity, camera, handle_size, handle_speed);
        }
        else if (m_type == TransformHandle_Scale)
        {
            m_is_editing = m_handle_scale.Update(m_space, selected_entity, camera, handle_size, handle_speed);
        }
        else if (m_type == TransformHandle_Rotation)
        {
            m_is_editing = m_handle_rotation.Update(m_space, selected_entity, camera, handle_size, handle_speed);
        }

        m_just_finished_editing = was_editing && !m_is_editing;

        return true;
    }

    uint32_t Transform_Gizmo::GetIndexCount() const
    {
        if (m_type == TransformHandle_Position)
        {
            return m_handle_position.GetIndexBuffer()->GetIndexCount();
        }
        else if (m_type == TransformHandle_Scale)
        {
            return m_handle_scale.GetIndexBuffer()->GetIndexCount();
        }

        return m_handle_rotation.GetIndexBuffer()->GetIndexCount();
    }

    const RHI_VertexBuffer* Transform_Gizmo::GetVertexBuffer() const
    {
        if (m_type == TransformHandle_Position)
        {
            return m_handle_position.GetVertexBuffer();
        }
        else if (m_type == TransformHandle_Scale)
        {
            return m_handle_scale.GetVertexBuffer();
        }

        return m_handle_rotation.GetVertexBuffer();
    }

    const RHI_IndexBuffer* Transform_Gizmo::GetIndexBuffer() const
    {
        if (m_type == TransformHandle_Position)
        {
            return m_handle_position.GetIndexBuffer();
        }
        else if (m_type == TransformHandle_Scale)
        {
            return m_handle_scale.GetIndexBuffer();
        }

        return m_handle_rotation.GetIndexBuffer();
    }

     const TransformHandle& Transform_Gizmo::GetHandle() const
     {
         if (m_type == TransformHandle_Position)
         {
             return m_handle_position;
         }
         else if (m_type == TransformHandle_Scale)
         {
             return m_handle_scale;
         }

         return m_handle_rotation;
     }
}
