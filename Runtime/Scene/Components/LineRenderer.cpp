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

//= INCLUDES ========================================
#include "LineRenderer.h"
#include "../../Core/Context.h"
#include "../../Math/Matrix.h"
#include "../../Math/BoundingBox.h"
#include "../../Core/Backends_Def.h"
#include "../../Graphics/D3D11/D3D11Graphics.h"
//===================================================

//= NAMESPACES ================
using namespace Directus::Math;
using namespace std;
//=============================

namespace Directus
{
	LineRenderer::LineRenderer(Context* context, GameObject* gameObject, Transform* transform) : IComponent(context, gameObject, transform)
	{

	}

	LineRenderer::~LineRenderer()
	{
		m_vertices.clear();
	}

	//= INPUT ===============================================================
	void LineRenderer::AddBoundigBox(const BoundingBox& box, const Vector4& color)
	{
		// Compute points from min and max
		Vector3 boundPoint1 = box.min;
		Vector3 boundPoint2 = box.max;
		Vector3 boundPoint3 = Vector3(boundPoint1.x, boundPoint1.y, boundPoint2.z);
		Vector3 boundPoint4 = Vector3(boundPoint1.x, boundPoint2.y, boundPoint1.z);
		Vector3 boundPoint5 = Vector3(boundPoint2.x, boundPoint1.y, boundPoint1.z);
		Vector3 boundPoint6 = Vector3(boundPoint1.x, boundPoint2.y, boundPoint2.z);
		Vector3 boundPoint7 = Vector3(boundPoint2.x, boundPoint1.y, boundPoint2.z);
		Vector3 boundPoint8 = Vector3(boundPoint2.x, boundPoint2.y, boundPoint1.z);

		// top of rectangular cuboid (6-2-8-4)
		AddLine(boundPoint6, boundPoint2, color);
		AddLine(boundPoint2, boundPoint8, color);
		AddLine(boundPoint8, boundPoint4, color);
		AddLine(boundPoint4, boundPoint6, color);

		// bottom of rectangular cuboid (3-7-5-1)
		AddLine(boundPoint3, boundPoint7, color);
		AddLine(boundPoint7, boundPoint5, color);
		AddLine(boundPoint5, boundPoint1, color);
		AddLine(boundPoint1, boundPoint3, color);

		// legs (6-3, 2-7, 8-5, 4-1)
		AddLine(boundPoint6, boundPoint3, color);
		AddLine(boundPoint2, boundPoint7, color);
		AddLine(boundPoint8, boundPoint5, color);
		AddLine(boundPoint4, boundPoint1, color);
	}

	void LineRenderer::AddLine(const Vector3& from, const Vector3& to, const Vector4& color)
	{
		AddVertex(VertexPosCol{ from, color });
		AddVertex(VertexPosCol{ to, color });
	}

	void LineRenderer::AddLines(const vector<VertexPosCol>& lineList)
	{
		m_vertices.insert(m_vertices.end(), lineList.begin(), lineList.end());
	}

	// All add functions resolve to this one
	void LineRenderer::AddVertex(const VertexPosCol& line)
	{
		m_vertices.push_back(line);
	}

	void LineRenderer::ClearVertices()
	{
		m_vertices.clear();
		m_vertices.shrink_to_fit();
	}

	//= MISC ================================================================
	void LineRenderer::SetBuffer()
	{
		CreateVertexBuffer();
		UpdateVertexBuffer();

		// Set vertex buffer
		m_vertexBuffer->SetIA();

		// Set primitive topology
		GetContext()->GetSubsystem<Graphics>()->SetPrimitiveTopology(LineList);
	}

	void LineRenderer::CreateVertexBuffer()
	{
		m_vertexBuffer = make_shared<D3D11VertexBuffer>(GetContext()->GetSubsystem<Graphics>());
		m_vertexBuffer->CreateDynamic(sizeof(VertexPosCol), (unsigned int)m_vertices.size());
	}

	//= MISC ================================================================
	void LineRenderer::UpdateVertexBuffer()
	{
		if (!m_vertexBuffer)
			return;

		// disable GPU access to the vertex buffer data.	
		void* data = m_vertexBuffer->Map();

		// update the vertex buffer.
		memcpy(data, &m_vertices[0], sizeof(VertexPosCol) * (int)m_vertices.size());

		// re-enable GPU access to the vertex buffer data.
		m_vertexBuffer->Unmap();
	}
}