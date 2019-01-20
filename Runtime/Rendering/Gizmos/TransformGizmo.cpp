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
#include "TransformGizmo.h"
#include "..\Renderer.h"
#include "..\..\RHI\RHI_Vertex.h"
#include "..\..\RHI\RHI_IndexBuffer.h"
#include "..\..\Rendering\Utilities\Geometry.h"
#include "..\..\Rendering\Model.h"
#include "..\..\World\Actor.h"
#include "..\..\World\Components\Transform.h"
#include "..\..\World\Components\Camera.h"
#include "..\..\World\Components\Renderable.h"
//=============================================

//=============================
using namespace std;
using namespace Directus::Math;
//=============================

namespace Directus
{
	TransformGizmo::TransformGizmo(Context* context)
	{
		m_context				= context;
		m_activeHandle			= TransformHandle_Position;
		m_space					= TransformHandle_World;
		m_isEditing				= false;
		m_isEditing_handle_x	= false;
		m_isEditing_handle_y	= false;
		m_isEditing_handle_z	= false;

		// Create position controllers
		vector<RHI_Vertex_PosUvNorTan> vertices;
		vector<unsigned int> indices;
		Utility::Geometry::CreateCone(&vertices, &indices);
		m_handle_position_model = make_unique<Model>(context);
		m_handle_position_model->Geometry_Append(indices, vertices);
		m_handle_position_model->Geometry_Update();
		m_handle_position_x = TransformHandle(TransformHandle_X, vertices, context);
		m_handle_position_y = TransformHandle(TransformHandle_Y, vertices, context);
		m_handle_position_z = TransformHandle(TransformHandle_Z, vertices, context);

		// Create scale model
		vertices.clear(); vertices.shrink_to_fit();
		indices.clear(); indices.shrink_to_fit();
		Utility::Geometry::CreateCube(&vertices, &indices);
		m_handle_scale_model = make_unique<Model>(m_context);
		m_handle_scale_model->Geometry_Append(indices, vertices);
		m_handle_scale_model->Geometry_Update();
	}

	TransformGizmo::~TransformGizmo()
	{

	}

	void TransformGizmo::Update(const shared_ptr<Actor>& actor, Camera* camera)
	{
		if (actor && camera)
		{	
			m_pickedActor = actor;

			// Get actor's components
			Transform* actor_transform				= actor->GetTransform_PtrRaw();			// Transform alone is not enough
			shared_ptr<Renderable> actor_renderable = actor->GetComponent<Renderable>();	// Bounding box is also needed as some meshes are not defined around P(0,0,0)	

			// Acquire actor's transformation data (local or world space)
			Vector3 aabb_center			= actor_renderable ? actor_renderable->Geometry_AABB().GetCenter() : Vector3::Zero;
			Vector3 actor_position		= (m_space == TransformHandle_World) ? actor_transform->GetPosition() : actor_transform->GetPositionLocal();
			Quaternion actor_rotation	= (m_space == TransformHandle_World) ? actor_transform->GetRotation() : actor_transform->GetRotationLocal();
			Vector3 actor_scale			= (m_space == TransformHandle_World) ? actor_transform->GetScale() : actor_transform->GetScaleLocal();
			Vector3 right				= (m_space == TransformHandle_World) ? Vector3::Right : actor_rotation * Vector3::Right;
			Vector3 up					= (m_space == TransformHandle_World) ? Vector3::Up : actor_rotation * Vector3::Up;
			Vector3 forward				= (m_space == TransformHandle_World) ? Vector3::Forward : actor_rotation * Vector3::Forward;

			// Compute scale
			float distance_to_camera	= camera ? (camera->GetTransform()->GetPosition() - (aabb_center)).Length() : 0.0f;
			float distance_to_camera_x	= camera ? (camera->GetTransform()->GetPosition() - (aabb_center - right)).Length() : 0.0f;
			float distance_to_camera_y	= camera ? (camera->GetTransform()->GetPosition() - (aabb_center - up)).Length() : 0.0f;
			float distance_to_camera_z	= camera ? (camera->GetTransform()->GetPosition() - (aabb_center - forward)).Length() : 0.0f;
			float handle_size			= 0.025f;
			float handle_distance		= distance_to_camera / (1.0f / 0.1f);

			// Compute transform for the handles
			m_handle_position_x.m_position	= aabb_center + right * handle_distance;
			m_handle_position_y.m_position	= aabb_center + up * handle_distance;
			m_handle_position_z.m_position	= aabb_center + forward * handle_distance;
			m_handle_position_x.m_rotation	= Quaternion::FromEulerAngles(0.0f, 0.0f, -90.0f);
			m_handle_position_y.m_rotation	= Quaternion::FromLookRotation(up, up);
			m_handle_position_z.m_rotation	= Quaternion::FromEulerAngles(90.0f, 0.0f, 0.0f);
			m_handle_position_x.m_scale		= distance_to_camera_x / (1.0f / handle_size);
			m_handle_position_y.m_scale		= distance_to_camera_y / (1.0f / handle_size);
			m_handle_position_z.m_scale		= distance_to_camera_z / (1.0f / handle_size);
		}

		// Update all the handles
		if (m_pickedActor && camera)
		{
			m_isEditing_handle_x	= m_handle_position_x.Update(m_pickedActor->GetTransform_PtrRaw(), camera);
			m_isEditing_handle_y	= m_handle_position_y.Update(m_pickedActor->GetTransform_PtrRaw(), camera);
			m_isEditing_handle_z	= m_handle_position_z.Update(m_pickedActor->GetTransform_PtrRaw(), camera);
			m_isEditing				= (m_isEditing_handle_x || m_isEditing_handle_y || m_isEditing_handle_z) && actor == nullptr;
		}
	}

	unsigned int TransformGizmo::GetIndexCount()
	{
		if (m_activeHandle == TransformHandle_Position)
		{
			return m_handle_position_model->GetIndexBuffer()->GetIndexCount();
		}
		else if (m_activeHandle == TransformHandle_Scale)
		{
			return m_handle_scale_model->GetIndexBuffer()->GetIndexCount();
		}

		return 0;
	}

	shared_ptr<RHI_VertexBuffer> TransformGizmo::GetVertexBuffer()
	{
		if (m_activeHandle == TransformHandle_Position)
		{
			return m_handle_position_model->GetVertexBuffer();
		}
		else if (m_activeHandle == TransformHandle_Scale)
		{
			return m_handle_scale_model->GetVertexBuffer();
		}

		return nullptr;
	}

	shared_ptr<RHI_IndexBuffer> TransformGizmo::GetIndexBuffer()
	{
		if (m_activeHandle == TransformHandle_Position)
		{
			return m_handle_position_model->GetIndexBuffer();
		}
		else if (m_activeHandle == TransformHandle_Scale)
		{
			return m_handle_scale_model->GetIndexBuffer();
		}

		return nullptr;
	}
}