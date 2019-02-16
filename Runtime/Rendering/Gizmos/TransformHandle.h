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
#include "Transform_Enums.h"
#include "../../Core/EngineDefs.h"
#include "../../Math/Matrix.h"
#include "../../Math/BoundingBox.h"
//=================================

namespace Directus
{
	class Renderer;
	class Context;
	class RHI_VertexBuffer;
	class RHI_IndexBuffer;
	class Entity;
	class Model;
	class Input;
	class Transform;
	class Camera;

	struct TransformHandleAxis
	{
		TransformHandleAxis(const Math::Vector3& axis)
		{
			this->axis		= axis;
			transform		= Math::Matrix::Identity;
			position		= Math::Vector3::Zero;
			rotation		= Math::Quaternion::Identity;
			scale			= Math::Vector3::One;
			box				= Math::BoundingBox::Zero;
			box_transformed	= Math::BoundingBox::Zero;
			delta			= 0.0f;
			isEditing		= false;
			isHovered		= false;
			isDisabled		= false;
		}

		void UpdateTransform()
		{
			transform		= Math::Matrix(position, rotation, scale);
			box_transformed	= box.Transformed(transform);
		}

		void UpdateInput(TransformHandle_Type type, Transform* transform, Input* input);
		void DrawExtra(Renderer* renderer, const Math::Vector3& transformCenter);

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
		float delta;
		bool isEditing;
		bool isHovered;
		bool isDisabled;
		Math::Vector3 m_color_active	= Math::Vector3(1.0f, 1.0f, 0.0f);
		Math::Vector3 m_color_disabled	= Math::Vector3(0.5f, 0.5f, 0.5f);
	};

	class ENGINE_CLASS TransformHandle
	{
	public:
		TransformHandle() {}	
		~TransformHandle() {}

		void Initialize(TransformHandle_Type type, Context* context);
		bool Update(TransformHandle_Space space, const std::shared_ptr<Entity>& entity, Camera* camera, float handle_size, float handle_speed);
		const Math::Matrix& GetTransform(const Math::Vector3& axis) const;
		const Math::Vector3& GetColor(const Math::Vector3& axis) const;
		std::shared_ptr<RHI_VertexBuffer> GetVertexBuffer();
		std::shared_ptr<RHI_IndexBuffer> GetIndexBuffer();
	
	private:
		void SnapToTransform(TransformHandle_Space space, const std::shared_ptr<Entity>& entity, Camera* camera, float handle_size);

		TransformHandleAxis m_handle_x		= TransformHandleAxis(Math::Vector3::Right);
		TransformHandleAxis m_handle_y		= TransformHandleAxis(Math::Vector3::Up);
		TransformHandleAxis m_handle_z		= TransformHandleAxis(Math::Vector3::Forward);
		TransformHandleAxis m_handle_xyz	= TransformHandleAxis(Math::Vector3::One);

		Math::Vector3 m_ray_previous;
		Math::Vector3 m_ray_current;
		std::unique_ptr<Model> m_model;
		Context* m_context;
		Renderer* m_renderer;
		Input* m_input;
		TransformHandle_Type m_type;
	};
}