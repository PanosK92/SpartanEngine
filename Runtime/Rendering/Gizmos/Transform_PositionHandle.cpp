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

//= INCLUDES ================================
#include "Transform_PositionHandle.h"
#include "..\Model.h"
#include "..\Utilities\Geometry.h"
#include "..\..\Logging\Log.h"
#include "..\..\Input\Input.h"
#include "..\..\World\Actor.h"
#include "..\..\World\Components\Camera.h"
#include "..\..\World\Components\Transform.h"
#include "..\..\Core\Context.h"
#include "..\..\Core\Settings.h"
#include "..\..\World\Components\Renderable.h"
//===========================================

//=============================
using namespace std;
using namespace Directus::Math;
//=============================

namespace Directus
{
	void PositionHandle_Axis::UpdateInput(Transform* transform, Input* input)
	{
		// First press
		if (isHovered && input->GetKeyDown(Click_Left))
		{
			isEditing = true;
		}

		// Editing can happen here
		if (isEditing && input->GetKey(Click_Left))
		{
			Vector3 position = transform->GetPosition();
			position += isEditing ? delta * axis : 0;
			transform->SetPosition(position);
		}

		// Last press (on release)
		if (isEditing && input->GetKeyUp(Click_Left))
		{
			isEditing = false;
		}
	}

	void Transform_PositionHandle::Initialize(Context* context)
	{
		m_context			= context;
		m_position_previous = Vector3::Zero;
		m_position_current	= Vector3::Zero;
		m_position_delta	= Vector3::Zero;

		// Create position controller
		vector<RHI_Vertex_PosUvNorTan> vertices;
		vector<unsigned int> indices;
		Utility::Geometry::CreateCone(&vertices, &indices);
		m_model = make_unique<Model>(m_context);
		m_model->Geometry_Append(indices, vertices);
		m_model->Geometry_Update();

		// Create bounding boxes for the handles, based on the vertices used
		m_handle_x.box = vertices;
		m_handle_y.box = m_handle_x.box;
		m_handle_z.box = m_handle_x.box;
	}

	bool Transform_PositionHandle::Update(TransformHandle_Space space, const shared_ptr<Actor>& actor, Camera* camera)
	{
		if (!actor || !camera)
		{
			LOG_ERROR_INVALID_PARAMETER();
			return false;
		}

		// Snap to actor position
		SnapToTransform(space, actor, camera);

		// Do hit test
		if (camera)
		{
			// Create ray starting from camera position and pointing towards where the mouse is pointing
			Input* input				= m_context->GetSubsystem<Input>();
			Vector2	mouse_pos			= input->GetMousePosition();
			Vector2 viewport_offset		= Settings::Get().Viewport_GetTopLeft();
			Vector2 mouse_pos_relative	= mouse_pos - viewport_offset;
			Vector3 ray_start			= camera->GetTransform()->GetPosition();
			Vector3 ray_end				= camera->ScreenToWorldPoint(mouse_pos_relative);
			Ray ray						= Ray(ray_start, ray_end);

			// Test if ray intersects any of the handles
			bool hovered_x = ray.HitDistance(m_handle_x.box_transformed) != INFINITY;
			bool hovered_y = ray.HitDistance(m_handle_y.box_transformed) != INFINITY;
			bool hovered_z = ray.HitDistance(m_handle_z.box_transformed) != INFINITY;

			// Mark a handle as hovered, only if it's the only hovered handle (during the previous frame
			m_handle_x.isHovered = hovered_x && !m_handle_y.isHovered && !m_handle_z.isHovered;
			m_handle_y.isHovered = hovered_y && !m_handle_x.isHovered && !m_handle_z.isHovered;
			m_handle_z.isHovered = hovered_z && !m_handle_x.isHovered && !m_handle_y.isHovered;

			// Disable handle if one of the other two is active (affects the color)
			m_handle_x.isDisabled = !m_handle_x.isEditing && (m_handle_y.isEditing || m_handle_z.isEditing);
			m_handle_y.isDisabled = !m_handle_y.isEditing && (m_handle_x.isEditing || m_handle_z.isEditing);
			m_handle_z.isDisabled = !m_handle_z.isEditing && (m_handle_x.isEditing || m_handle_y.isEditing);

			// Track delta
			m_position_previous = m_position_current != Vector3::Zero ? m_position_current : ray_end; // avoid big delta in the first run
			m_position_current	= ray_end;
			m_position_delta	= (m_position_current - m_position_previous);
			
			// Updated handles with delta
			float speed = 12.0f;
			m_handle_x.delta = m_position_delta * speed;
			m_handle_y.delta = m_position_delta * speed;
			m_handle_z.delta = m_position_delta * speed;

			// Update input
			m_handle_x.UpdateInput(actor->GetTransform_PtrRaw(), input);
			m_handle_y.UpdateInput(actor->GetTransform_PtrRaw(), input);
			m_handle_z.UpdateInput(actor->GetTransform_PtrRaw(), input);
		}

		return m_handle_x.isEditing ||  m_handle_y.isEditing || m_handle_z.isEditing;
	}

	const Matrix& Transform_PositionHandle::GetTransform(const Vector3& axis) const
	{
		if (axis == Vector3::Right)
		{
			return m_handle_x.transform;
		}
		else if (axis == Vector3::Up)
		{
			return m_handle_y.transform;
		}

		return m_handle_z.transform;
	}

	const Vector3& Transform_PositionHandle::GetColor(const Vector3& axis) const
	{
		if (axis == Vector3::Right)
		{
			return m_handle_x.GetColor();
		}
		else if (axis == Vector3::Up)
		{
			return m_handle_y.GetColor();
		}

		return m_handle_z.GetColor();
	}

	shared_ptr<RHI_VertexBuffer> Transform_PositionHandle::GetVertexBuffer()
	{
		return m_model->GetVertexBuffer();
	}

	shared_ptr<RHI_IndexBuffer> Transform_PositionHandle::GetIndexBuffer()
	{
		return m_model->GetIndexBuffer();
	}

	void Transform_PositionHandle::SnapToTransform(TransformHandle_Space space, const shared_ptr<Actor>& actor, Camera* camera)
	{
		// Get actor's components
		Transform* actor_transform				= actor->GetTransform_PtrRaw();			// Transform alone is not enough
		shared_ptr<Renderable> actor_renderable = actor->GetComponent<Renderable>();	// Bounding box is also needed as some meshes are not defined around P(0,0,0)	

		// Acquire actor's transformation data (local or world space)
		Vector3 aabb_center			= actor_renderable ? actor_renderable->Geometry_AABB().GetCenter()	: Vector3::Zero;
		Vector3 actor_position		= (space == TransformHandle_World) ? actor_transform->GetPosition() : actor_transform->GetPositionLocal();
		Quaternion actor_rotation	= (space == TransformHandle_World) ? actor_transform->GetRotation() : actor_transform->GetRotationLocal();
		Vector3 actor_scale			= (space == TransformHandle_World) ? actor_transform->GetScale()	: actor_transform->GetScaleLocal();
		Vector3 right				= (space == TransformHandle_World) ? Vector3::Right					: actor_rotation * Vector3::Right;
		Vector3 up					= (space == TransformHandle_World) ? Vector3::Up					: actor_rotation * Vector3::Up;
		Vector3 forward				= (space == TransformHandle_World) ? Vector3::Forward				: actor_rotation * Vector3::Forward;

		// Compute scale
		float distance_to_camera	= camera ? (camera->GetTransform()->GetPosition() - (aabb_center)).Length()				: 0.0f;
		float distance_to_camera_x	= camera ? (camera->GetTransform()->GetPosition() - (aabb_center - right)).Length()		: 0.0f;
		float distance_to_camera_y	= camera ? (camera->GetTransform()->GetPosition() - (aabb_center - up)).Length()		: 0.0f;
		float distance_to_camera_z	= camera ? (camera->GetTransform()->GetPosition() - (aabb_center - forward)).Length()	: 0.0f;
		float handle_size			= 0.025f;
		float handle_distance		= distance_to_camera / (1.0f / 0.1f);

		// Compute transform for the handles
		m_handle_x.position = aabb_center + right	* handle_distance;
		m_handle_y.position = aabb_center + up		* handle_distance;
		m_handle_z.position = aabb_center + forward * handle_distance;
		m_handle_x.rotation = Quaternion::FromEulerAngles(0.0f, 0.0f, -90.0f);
		m_handle_y.rotation = Quaternion::FromLookRotation(up, up);
		m_handle_z.rotation = Quaternion::FromEulerAngles(90.0f, 0.0f, 0.0f);
		m_handle_x.scale	= distance_to_camera_x / (1.0f / handle_size);
		m_handle_y.scale	= distance_to_camera_y / (1.0f / handle_size);
		m_handle_z.scale	= distance_to_camera_z / (1.0f / handle_size);

		// Update transforms
		m_handle_x.UpdateTransform();
		m_handle_y.UpdateTransform();
		m_handle_z.UpdateTransform();
	}
}