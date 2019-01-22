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

//= INCLUDES ======================
#include <vector>
#include "Transform_Enums.h"
#include "../../Core/EngineDefs.h"
#include "../../Math/Matrix.h"
#include "../../Math/BoundingBox.h"
#include "../../Math/Ray.h"
//=================================

namespace Directus
{
	class Renderer;
	class Context;
	class RHI_VertexBuffer;
	class RHI_IndexBuffer;
	class Actor;
	class Model;
	class Input;
	class Transform;
	class Camera;

	struct PositionHandle_Axis
	{
		PositionHandle_Axis(const Math::Vector3& axis)
		{
			this->axis		= axis;
			transform		= Math::Matrix::Identity;
			position		= Math::Vector3::Zero;
			rotation		= Math::Quaternion::Identity;
			scale			= Math::Vector3::One;
			box				= Math::BoundingBox::Zero;
			box_transformed	= Math::BoundingBox::Zero;
			delta			= Math::Vector3::Zero;
			isEditing		= false;
			isHovered		= false;
			isDisabled		= false;
		}

		void UpdateTransform()
		{
			transform		= Math::Matrix(position, rotation, scale);
			box_transformed	= box.Transformed(transform);
		}

		void UpdateInput(Transform* transform, Input* input);
		const Math::Vector3& GetColor() const
		{
			if (isDisabled)
				return m_color_disabled;

			if (isHovered || isEditing)
				return m_color_active;

			return axis;
		}

		Math::Vector3 axis;
		Math::Matrix transform;
		Math::Vector3 position;
		Math::Quaternion rotation;
		Math::Vector3 scale;
		Math::BoundingBox box;
		Math::BoundingBox box_transformed;
		Math::Vector3 delta;
		bool isEditing;
		bool isHovered;
		bool isDisabled;
		Math::Vector3 m_color_active	= Math::Vector3(1.0f, 1.0f, 0.0f);
		Math::Vector3 m_color_disabled	= Math::Vector3(0.5f, 0.5f, 0.5f);
	};

	class ENGINE_CLASS Transform_PositionHandle
	{
	public:
		Transform_PositionHandle() {}	
		~Transform_PositionHandle() {}

		void Initialize(Context* context);
		bool Update(TransformHandle_Space space, const std::shared_ptr<Actor>& actor, Camera* camera);
		const Math::Matrix& GetTransform(const Math::Vector3& axis) const;
		const Math::Vector3& GetColor(const Math::Vector3& axis) const;
		std::shared_ptr<RHI_VertexBuffer> GetVertexBuffer();
		std::shared_ptr<RHI_IndexBuffer> GetIndexBuffer();
	
	private:
		void SnapToTransform(TransformHandle_Space space, const std::shared_ptr<Actor>& actor, Camera* camera);

		PositionHandle_Axis m_handle_x = PositionHandle_Axis(Math::Vector3::Right);
		PositionHandle_Axis m_handle_y = PositionHandle_Axis(Math::Vector3::Up);
		PositionHandle_Axis m_handle_z = PositionHandle_Axis(Math::Vector3::Forward);

		Math::Vector3 m_position_delta;
		Math::Vector3 m_position_previous;
		Math::Vector3 m_position_current;
		std::unique_ptr<Model> m_model;
		Context* m_context;
		Renderer* m_renderer;
	};
}