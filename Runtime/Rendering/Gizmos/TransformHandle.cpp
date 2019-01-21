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
#include "TransformHandle.h"
#include "..\..\Logging\Log.h"
#include "..\..\Core\Context.h"
#include "..\..\Input\Input.h"
#include "..\..\World\Components\Camera.h"
#include "..\..\World\Components\Transform.h"
#include "..\..\Core\Settings.h"
//===========================================

//=============================
using namespace std;
using namespace Directus::Math;
//=============================

namespace Directus
{
	TransformHandle::TransformHandle(TransformHandle_Axis axis, const std::vector<RHI_Vertex_PosUvNorTan>& vertices, Context* context)
	{
		m_context		= context;
		m_mTransform	= Matrix::Identity;
		m_box			= vertices;
		m_axis_type		= axis;
		m_axis_previous = Vector3::Zero;
		m_axis_current	= Vector3::Zero;
		m_axis_delta	= Vector3::Zero;
	}

	bool TransformHandle::Update(Transform* transform, Camera* camera)
	{
		m_mTransform		= Matrix(m_position, m_rotation, m_scale);
		m_box_transforemd	= m_box.Transformed(m_mTransform);

		// Do hit test
		if (camera)
		{
			Input* input		= m_context->GetSubsystem<Input>();
			Vector2	mousePos	= input->GetMousePosition();
			mousePos			-= Settings::Get().Viewport_GetTopLeft(); // compute relative mouse position

			Vector3 ray_start	= camera->GetTransform()->GetPosition();
			Vector3 ray_end		= camera->ScreenToWorldPoint(mousePos);

			Ray ray		= Ray(ray_start, ray_end);
			m_isHovered = ray.HitDistance(m_box_transforemd) != INFINITY;

			// Track delta
			m_axis_previous = m_axis_current;
			m_axis_current	= ray_end;
			if (m_axis_type == TransformHandle_X)
			{
				m_axis_delta = (m_axis_current - m_axis_previous) * Vector3::Right;
			}
			else if (m_axis_type == TransformHandle_Y)
			{
				m_axis_delta = (m_axis_current - m_axis_previous) * Vector3::Up;
			}
			else if (m_axis_type == TransformHandle_Z)
			{
				m_axis_delta = (m_axis_current - m_axis_previous) * Vector3::Forward;
			}

			// First press
			if (m_isHovered && input->GetKeyDown(Click_Left))
			{
				m_isPressed = true;
			}

			// Editing can happen here
			if (m_isPressed && input->GetKey(Click_Left))
			{
				EditTransform(transform);
			}

			// Last press (on release)
			if (m_isPressed && input->GetKeyUp(Click_Left))
			{
				m_isPressed = false;
			}
		}

		return m_isPressed;
	}

	const Vector3& TransformHandle::GetColor() const
	{
		if (m_isHovered || m_isPressed)
			return m_color_pressed;

		if (m_axis_type == TransformHandle_X)
		{
			return m_color_released_x;
		}
		else if (m_axis_type == TransformHandle_Y)
		{
			return m_color_released_y;
		}
		else
		{
			return m_color_released_z;
		}
	}

	void TransformHandle::EditTransform(Transform* transform)
	{
		if (!transform)
		{
			LOG_ERROR_INVALID_PARAMETER();
			return;
		}

		Vector3 position = transform->GetPosition();
		float speed = 12.0f;

		if (m_axis_type == TransformHandle_X)
		{
			position.x += m_axis_delta.x * speed;
		}
		else if (m_axis_type == TransformHandle_Y)
		{
			position.y += m_axis_delta.y * speed;
		}
		else if (m_axis_type == TransformHandle_Z)
		{
			position.z += m_axis_delta.z * speed;
		}

		transform->SetPosition(position);
	}
}