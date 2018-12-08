/*
Copyright(c) 2016-2018 Panos Karabelas

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
#include "TransformationGizmo.h"
#include "Actor.h"
#include "Components\Transform.h"
#include "..\RHI\RHI_Vertex.h"
#include "..\Rendering\Utilities\Geometry.h"
//==========================================

//=============================
using namespace std;
using namespace Directus::Math;
//=============================

namespace Directus
{
	TransformationGizmo::TransformationGizmo(Context* context)
	{
		m_context			= context;
		m_transformationX	= Matrix::Identity;
		m_transformationY	= Matrix::Identity;
		m_transformationZ	= Matrix::Identity;
		m_type				= TransformGizmo_Position;
		m_space				= TransformGizmo_World;
		m_scale				= Vector3(0.2f);
	
		// Create cone
		vector<RHI_Vertex_PosUVTBN> vertices;
		vector<unsigned int> indices;
		Utility::Geometry::CreateCone(&vertices, &indices);
		/*m_meshCone = make_unique<Mesh>(m_context);
		m_meshCone->Vertices_Set(vertices);
		m_meshCone->Indices_Set(indices);
		m_meshCone->Geometry_Update();*/

		// Create cone
		vertices.clear(); vertices.shrink_to_fit();
		indices.clear(); indices.shrink_to_fit();
		Utility::Geometry::CreateCube(&vertices, &indices);
		/*m_meshCube = make_unique<Mesh>(m_context);
		m_meshCube->Vertices_Set(vertices);
		m_meshCube->Indices_Set(indices);
		m_meshCube->Geometry_Update();*/
	}

	TransformationGizmo::~TransformationGizmo()
	{

	}

	void TransformationGizmo::Pick(weak_ptr<Actor> actor)
	{
		if (actor.expired())
			return;

		Transform* transformComponent = actor.lock()->GetComponent<Transform>().get();
		Matrix transform = (m_space == TransformGizmo_Local) ? transformComponent->GetMatrix() : transformComponent->GetMatrix();

		Matrix mTranslation		= Matrix::CreateTranslation(transform.GetTranslation());
		Quaternion qRotation	= transform.GetRotation();
		Matrix mRotation		= Matrix::CreateRotation(transform.GetRotation());
		Vector3 mRotationEuler	= transform.GetRotation().ToEulerAngles();
		Matrix mScaleGizmo		= Matrix::CreateScale(m_scale);

		// Default transformation
		m_transformationX = mTranslation * mRotation;
		m_transformationY = m_transformationX;
		m_transformationZ = m_transformationX;

		// Add position offset
		m_transformationY = Matrix::CreateTranslation(Vector3(0, 1, 0)) * m_transformationY;
		m_transformationX = Matrix::CreateTranslation(Vector3(1, 0, 0)) * m_transformationX;
		m_transformationZ = Matrix::CreateTranslation(Vector3(0, 0, 1)) * m_transformationZ;

		// Add rotation offset
		m_transformationY = m_transformationY;
		m_transformationX = Matrix::CreateRotation(qRotation * Quaternion::FromEulerAngles(Vector3(mRotationEuler.x + 90.0f, mRotationEuler.y, mRotationEuler.z))) * m_transformationX;
		m_transformationZ = Matrix::CreateRotation(qRotation * Quaternion::FromEulerAngles(Vector3(mRotationEuler.x, mRotationEuler.y, mRotationEuler.z + 90.0f))) * m_transformationZ;	

		// Add scale offset
		m_transformationY = mScaleGizmo * m_transformationY;
		m_transformationX = mScaleGizmo * m_transformationX;
		m_transformationZ = mScaleGizmo * m_transformationZ;
	}

	void TransformationGizmo::SetBuffers()
	{
		if (m_type == TransformGizmo_Position)
		{
			//m_meshCone->Geometry_Bind();
		}
		else if (m_type == TransformGizmo_Scale)
		{
			//m_meshCube->Geometry_Bind();
		}
	}

	unsigned int TransformationGizmo::GetIndexCount()
	{
		if (m_type == TransformGizmo_Position)
		{
			//return m_meshCone->Indices_Count();
		}
		else if (m_type == TransformGizmo_Scale)
		{
			//return m_meshCube->Indices_Count();
		}

		return 0;
	}
}