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

//= INCLUDES ===============================
#include "TransformGizmo.h"
#include "..\RHI\RHI_Vertex.h"
#include "..\RHI\RHI_IndexBuffer.h"
#include "..\Rendering\Utilities\Geometry.h"
#include "..\Rendering\Model.h"
#include "..\World\Components\Transform.h"
#include "..\World\Actor.h"
//==========================================

//=============================
using namespace std;
using namespace Directus::Math;
//=============================

namespace Directus
{
	TransformGizmo::TransformGizmo(Context* context)
	{
		m_context		= context;
		m_transformX	= Matrix::Identity;
		m_transformY	= Matrix::Identity;
		m_transformZ	= Matrix::Identity;
		m_type			= TransformGizmo_Position;
		m_space			= TransformGizmo_World;
		m_scale			= Matrix::CreateScale(Vector3(0.2f));
	
		// Create position model
		vector<RHI_Vertex_PosUvNorTan> vertices;
		vector<unsigned int> indices;
		Utility::Geometry::CreateCone(&vertices, &indices);
		m_positionModel = make_unique<Model>(m_context);
		m_positionModel->Geometry_Append(indices, vertices);
		m_positionModel->Geometry_Update();

		// Create scale model
		vertices.clear(); vertices.shrink_to_fit();
		indices.clear(); indices.shrink_to_fit();
		Utility::Geometry::CreateCube(&vertices, &indices);
		m_scaleModel = make_unique<Model>(m_context);
		m_scaleModel->Geometry_Append(indices, vertices);
		m_scaleModel->Geometry_Update();
	}

	TransformGizmo::~TransformGizmo()
	{

	}

	void TransformGizmo::Pick(shared_ptr<Actor> actor)
	{
		if (!actor)
			return;

		Transform* transformComponent = actor->GetTransform_PtrRaw();
		Matrix transform = (m_space == TransformGizmo_Local) ? transformComponent->GetLocalMatrix() : transformComponent->GetMatrix();

		Matrix mTranslation		= Matrix::CreateTranslation(transform.GetTranslation());
		Quaternion qRotation	= transform.GetRotation();
		Matrix mRotation		= Matrix::CreateRotation(transform.GetRotation());
		Vector3 mRotationEuler	= transform.GetRotation().ToEulerAngles();
		
		// Default transform
		m_transformX = mTranslation * mRotation;
		m_transformY = m_transformX;
		m_transformZ = m_transformX;

		// Position offset
		m_transformX = Matrix::CreateTranslation(Vector3::Right) * m_transformX;
		m_transformY = Matrix::CreateTranslation(Vector3::Up) * m_transformY;
		m_transformZ = Matrix::CreateTranslation(Vector3::Forward) * m_transformZ;

		// Rotation offset
		m_transformY = m_transformY;
		m_transformX = Matrix::CreateRotation(qRotation * Quaternion::FromEulerAngles(Vector3(mRotationEuler.x + 90.0f, mRotationEuler.y, mRotationEuler.z))) * m_transformX;
		m_transformZ = Matrix::CreateRotation(qRotation * Quaternion::FromEulerAngles(Vector3(mRotationEuler.x, mRotationEuler.y, mRotationEuler.z + 90.0f))) * m_transformZ;	

		// Scale offset
		m_transformX = m_scale * m_transformX;
		m_transformY = m_scale * m_transformY;
		m_transformZ = m_scale * m_transformZ;
	}

	unsigned int TransformGizmo::GetIndexCount()
	{
		if (m_type == TransformGizmo_Position)
		{
			return m_positionModel->GetIndexBuffer()->GetIndexCount();
		}
		else if (m_type == TransformGizmo_Scale)
		{
			return m_scaleModel->GetIndexBuffer()->GetIndexCount();
		}

		return 0;
	}

	shared_ptr<RHI_VertexBuffer> TransformGizmo::GetVertexBuffer()
	{
		if (m_type == TransformGizmo_Position)
		{
			return m_positionModel->GetVertexBuffer();
		}
		else if (m_type == TransformGizmo_Scale)
		{
			return m_scaleModel->GetVertexBuffer();
		}

		return nullptr;
	}

	shared_ptr<RHI_IndexBuffer> TransformGizmo::GetIndexBuffer()
	{
		if (m_type == TransformGizmo_Position)
		{
			return m_positionModel->GetIndexBuffer();
		}
		else if (m_type == TransformGizmo_Scale)
		{
			return m_scaleModel->GetIndexBuffer();
		}

		return nullptr;
	}
}