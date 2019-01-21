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

#pragma once

//= INCLUDES ==================================
#include "Transform_Gizmo.h"
#include "..\Renderer.h"
#include "..\..\RHI\RHI_Vertex.h"
#include "..\..\RHI\RHI_IndexBuffer.h"
#include "..\..\Rendering\Utilities\Geometry.h"
#include "..\..\Rendering\Model.h"
#include "..\..\World\Actor.h"
#include "..\..\World\Components\Transform.h"
//=============================================

//=============================
using namespace std;
using namespace Directus::Math;
//=============================

namespace Directus
{
	Transform_Gizmo::Transform_Gizmo(Context* context)
	{
		m_context		= context;
		m_activeHandle	= TransformHandle_Position;
		m_space			= TransformHandle_World;
		m_isEditing		= false;

		// Position handle
		m_handle_position.Initialize(context);

		// Create scale model
		vector<RHI_Vertex_PosUvNorTan> vertices;
		vector<unsigned int> indices;
		Utility::Geometry::CreateCube(&vertices, &indices);
		m_handle_scale_model = make_unique<Model>(m_context);
		m_handle_scale_model->Geometry_Append(indices, vertices);
		m_handle_scale_model->Geometry_Update();
	}

	Transform_Gizmo::~Transform_Gizmo()
	{

	}

	bool Transform_Gizmo::Update(const shared_ptr<Actor>& actor, Camera* camera)
	{
		// If there is no camera, don't even bother
		if (!camera)
			return false;

		// Don't erase picked actor if it's currently being edited
		if (!actor)
		{
			m_selectedActor = m_isEditing ? m_selectedActor : nullptr;
		}

		// Update picked actor only if it's not being edited
		if (!m_isEditing)
		{
			m_selectedActor = actor;
		}

		// If there is a valid actor, update the handle
		if (m_selectedActor)
		{
			if (m_activeHandle == TransformHandle_Position)
			{
				m_isEditing = m_handle_position.Update(m_space, m_selectedActor, camera);
			}
		}

		m_isInspecting = m_isEditing || actor;

		return m_isInspecting;
	}

	unsigned int Transform_Gizmo::GetIndexCount()
	{
		if (m_activeHandle == TransformHandle_Position)
		{
			return m_handle_position.GetIndexBuffer()->GetIndexCount();
		}
		else if (m_activeHandle == TransformHandle_Scale)
		{
			return m_handle_scale_model->GetIndexBuffer()->GetIndexCount();
		}

		return 0;
	}

	shared_ptr<RHI_VertexBuffer> Transform_Gizmo::GetVertexBuffer()
	{
		if (m_activeHandle == TransformHandle_Position)
		{
			return m_handle_position.GetVertexBuffer();
		}
		else if (m_activeHandle == TransformHandle_Scale)
		{
			return m_handle_scale_model->GetVertexBuffer();
		}

		return nullptr;
	}

	 shared_ptr<RHI_IndexBuffer> Transform_Gizmo::GetIndexBuffer()
	{
		if (m_activeHandle == TransformHandle_Position)
		{
			return m_handle_position.GetIndexBuffer();
		}
		else if (m_activeHandle == TransformHandle_Scale)
		{
			return m_handle_scale_model->GetIndexBuffer();
		}

		return nullptr;
	}
}