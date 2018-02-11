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

//= INCLUDES ==============
#include "../Core/EngineDefs.h"
#include <vector>
#include <memory>
#include "../Math/Matrix.h"
//=========================

namespace Directus
{
	class Context;
	class D3D11VertexBuffer;
	class D3D11IndexBuffer;
	class Transform;
	struct VertexPosCol;

	class ENGINE_CLASS Grid
	{
	public:
		Grid(Context* context);
		~Grid();

		void BuildGrid();
		bool SetBuffer();
		const Math::Matrix& ComputeWorldMatrix(Transform* camera);
		unsigned int GetIndexCount() { return m_indexCount; }

	private:	
		bool CreateBuffers(std::vector<VertexPosCol>& vertices, std::vector<unsigned int>& indices);

		Context* m_context;
		unsigned int m_indexCount;
		int m_terrainHeight;
		int m_terrainWidth;
		std::shared_ptr<D3D11VertexBuffer> m_vertexBuffer;
		std::shared_ptr<D3D11IndexBuffer> m_indexBuffer;
		Math::Matrix m_world;
	};
}