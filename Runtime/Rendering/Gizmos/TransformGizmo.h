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

//= INCLUDES =====================
#include <memory>
#include <vector>
#include "TransformHandle.h"
#include "../../Core/EngineDefs.h"
//================================

namespace Directus
{
	class Camera;
	class Model;
	class Context;
	class Actor;
	class RHI_IndexBuffer;
	class RHI_VertexBuffer;

	namespace Math
	{
		class Matrix;
	}

	enum TransformHandle_Type
	{
		TransformHandle_Position,
		TransformHandle_Rotation,
		TransformHandle_Scale
	};

	enum TransformHandle_Space
	{
		TransformHandle_Local,
		TransformHandle_World
	};

	class ENGINE_CLASS TransformGizmo
	{
	public:
		TransformGizmo(Context* context);
		~TransformGizmo();

		void Update(const std::shared_ptr<Actor>& actor, Camera* camera);
		const TransformHandle& GetHandle_Pos_X() const { return m_handle_position_x; }
		const TransformHandle& GetHandle_Pos_Y() const { return m_handle_position_y; }
		const TransformHandle& GetHandle_Pos_Z() const { return m_handle_position_z; }
		unsigned int GetIndexCount();
		std::shared_ptr<RHI_VertexBuffer> GetVertexBuffer();
		std::shared_ptr<RHI_IndexBuffer> GetIndexBuffer();
		bool IsInspecting() { return m_isInspecting; }

	private:
		bool m_isInspecting;
		bool m_isEditing;
		bool m_isEditing_handle_x;
		bool m_isEditing_handle_y;
		bool m_isEditing_handle_z;

		TransformHandle m_handle_position_x;
		TransformHandle m_handle_position_y;
		TransformHandle m_handle_position_z;

		std::shared_ptr<Actor> m_selectedActor;

		TransformHandle_Type m_activeHandle;
		TransformHandle_Space m_space;

		std::unique_ptr<Model> m_handle_position_model;
		std::unique_ptr<Model> m_handle_scale_model;
		Context* m_context;
	};
}