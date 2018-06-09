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

//= INCLUDES ==========================================
#include "IComponent.h"
#include <memory>
#include <vector>
#include "../../Rendering/RI/RI_Vertex.h"
#include "../../Rendering/RI/D3D11/D3D11_VertexBuffer.h"
//=====================================================

namespace Directus
{
	namespace Math
	{
		class BoundingBox;
		class Vector3;
	}

	class ENGINE_CLASS LineRenderer : public IComponent
	{
	public:
		LineRenderer(Context* context, GameObject* gameObject, Transform* transform);
		~LineRenderer();

		//= INPUT ===================================================================================
		void AddBoundigBox(const Math::BoundingBox& box, const Math::Vector4& color);
		void AddLine(const Math::Vector3& from, const Math::Vector3& to, const Math::Vector4& color);	
		void AddLines(const std::vector<VertexPosCol>& lineList);
		void AddVertex(const VertexPosCol& line);
		void ClearVertices();

		//= MISC ================================================================
		void CreateVertexBuffer();
		void SetBuffer();
		unsigned int GetVertexCount() { return (unsigned int)m_vertices.size(); }

	private:
		//= VERTICES =====================================
		std::shared_ptr<D3D11_VertexBuffer> m_vertexBuffer;
		std::vector<VertexPosCol> m_vertices;
		//================================================

		//= MISC =================
		void UpdateVertexBuffer();
		//========================
	};
}
