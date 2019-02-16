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
#include "TransformHandle.h"
#include "../../Core/EngineDefs.h"
//================================

namespace Directus
{
	class World;
	class Input;
	class Camera;
	class Context;
	class Entity;
	class RHI_IndexBuffer;
	class RHI_VertexBuffer;

	class ENGINE_CLASS Transform_Gizmo
	{
	public:
		Transform_Gizmo(Context* context);
		~Transform_Gizmo();

		bool Update(const std::shared_ptr<Entity>& entity, Camera* camera, float handle_size, float handle_speed);
		unsigned int GetIndexCount();
		std::shared_ptr<RHI_VertexBuffer> GetVertexBuffer();
		std::shared_ptr<RHI_IndexBuffer> GetIndexBuffer();
		const TransformHandle& GetHandle() const;
		bool DrawXYZ() { return m_type == TransformHandle_Scale; }

	private:
		bool m_isEditing;

		std::shared_ptr<Entity> m_selectedentity;
		TransformHandle m_handle_position;
		TransformHandle m_handle_rotation;
		TransformHandle m_handle_scale;
		TransformHandle_Type m_type;
		TransformHandle_Space m_space;
		Context* m_context;
		Input* m_input;
		World* m_world;
	};
}