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

//= INCLUDES ==================
#include <memory>
#include "../Core/EngineDefs.h"
#include "../Math/Matrix.h"
//=============================

namespace Directus
{
	class Context;
	class Actor;

	enum TransformGizmo_Type
	{
		TransformGizmo_Position,
		TransformGizmo_Rotation,
		TransformGizmo_Scale
	};

	enum TransformGizmo_Space
	{
		TransformGizmo_Local,
		TransformGizmo_World
	};

	class ENGINE_CLASS TransformationGizmo
	{
	public:
		TransformationGizmo(Context* context);
		~TransformationGizmo();

		void Pick(std::weak_ptr<Actor> actor);
		void SetBuffers();
		unsigned int GetIndexCount();

		const Math::Matrix& GetTransformationX() { return m_transformationX; }
		const Math::Matrix& GetTransformationY() { return m_transformationY; }
		const Math::Matrix& GetTransformationZ() { return m_transformationZ; }

	private:
		//std::unique_ptr<Mesh> m_meshCone;
		//std::unique_ptr<Mesh> m_meshCube;

		Math::Matrix m_transformationX;
		Math::Matrix m_transformationY;
		Math::Matrix m_transformationZ;

		TransformGizmo_Type m_type;
		TransformGizmo_Space m_space;
		Math::Vector3 m_scale;

		Context* m_context;
	};
}