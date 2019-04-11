/*
Copyright(c) 2016-2019 Panos Karabelas

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

//= INCLUDES =========================
#include "Transform_Gizmo.h"
#include "../Model.h"
#include "../../RHI/RHI_IndexBuffer.h"
#include "../../World/World.h"
#include "../../World/Entity.h"
#include "../../Input/Input.h"
//====================================

//=============================
using namespace std;
using namespace Spartan::Math;
//=============================

namespace Spartan
{
	Transform_Gizmo::Transform_Gizmo(Context* context)
	{
		m_context		= context;
		m_input			= m_context->GetSubsystem<Input>().get();
		m_world			= m_context->GetSubsystem<World>().get();
		m_type			= TransformHandle_Position;
		m_space			= TransformHandle_World;
		m_is_editing	= false;

		// Handles
		m_handle_position.Initialize(TransformHandle_Position, context);
		m_handle_rotation.Initialize(TransformHandle_Rotation, context);
		m_handle_scale.Initialize(TransformHandle_Scale, context);
	}

	shared_ptr<Entity>& Transform_Gizmo::SetSelectedEntity(const shared_ptr<Entity>& entity)
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

		// If there is no camera, don't even bother
		if (!camera || !m_entity_selected)
		{
			m_is_editing = false;
			return false;
		}

		// Switch between position, rotation and scale handles, with W, E and R respectively
		if (m_input->GetKeyDown(W))
		{
			m_type = TransformHandle_Position;
		}
		else if (m_input->GetKeyDown(E))
		{
			m_type = TransformHandle_Scale;
		}
		else if (m_input->GetKeyDown(R))
		{
			m_type = TransformHandle_Rotation;
		}

		bool was_editing = m_is_editing;

		// Update appropriate handle
		if (m_type == TransformHandle_Position)
		{
			m_is_editing = m_handle_position.Update(m_space, m_entity_selected, camera, handle_size, handle_speed);
		}
		else if (m_type == TransformHandle_Scale)
		{
			m_is_editing = m_handle_scale.Update(m_space, m_entity_selected, camera, handle_size, handle_speed);
		}
		else if (m_type == TransformHandle_Rotation)
		{
			m_is_editing = m_handle_rotation.Update(m_space, m_entity_selected, camera, handle_size, handle_speed);
		}

		m_just_finished_editing = was_editing && !m_is_editing;

		return true;
	}

	unsigned int Transform_Gizmo::GetIndexCount()
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

	shared_ptr<RHI_VertexBuffer> Transform_Gizmo::GetVertexBuffer()
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

	shared_ptr<RHI_IndexBuffer> Transform_Gizmo::GetIndexBuffer()
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