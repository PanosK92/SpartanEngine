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
#include "../../Core/EngineDefs.h"
#include "../../Math/Matrix.h"
#include "../../Math/BoundingBox.h"
#include "../../Math/Ray.h"
//=================================

namespace Directus
{
	class Transform;
	class Camera;

	enum TransformHandle_Axis
	{
		TransformHandle_X,
		TransformHandle_Y,
		TransformHandle_Z
	};

	class ENGINE_CLASS TransformHandle
	{
	public:
		TransformHandle() {}
		TransformHandle(TransformHandle_Axis axis, const std::vector<RHI_Vertex_PosUvNorTan>& vertices, Context* context);
		~TransformHandle() {}

		bool Update(Transform* transform, Camera* camera);		
		bool IsPressed()							{ return m_isPressed; }
		bool IsHovered()							{ return m_isHovered; }
		const Math::Matrix& GetTransform() const	{ return m_mTransform; }
		const Math::Vector3& GetColor() const;

		Math::Vector3 m_position;
		Math::Quaternion m_rotation;
		Math::Vector3 m_scale;
		Math::BoundingBox m_box;
		Math::BoundingBox m_box_transforemd;	
		
	private:
		void EditTransform(Transform* transform);

		bool m_isPressed;
		bool m_isHovered;

		Math::Matrix m_mTransform;
		TransformHandle_Axis m_axis_type;
		Math::Vector3 m_axis_delta;
		Math::Vector3 m_axis_previous;
		Math::Vector3 m_axis_current;
		Math::Vector3 m_color_released_x	= Math::Vector3::Right;
		Math::Vector3 m_color_released_y	= Math::Vector3::Up;
		Math::Vector3 m_color_released_z	= Math::Vector3::Forward;
		Math::Vector3 m_color_pressed		= Math::Vector3(1.0f, 1.0f, 0.0f);
		Context* m_context;
	};
}