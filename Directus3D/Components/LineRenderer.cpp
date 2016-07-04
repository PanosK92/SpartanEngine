/*
Copyright(c) 2016 Panos Karabelas

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

//= INCLUDES =============================
#include "LineRenderer.h"
#include "../IO/Log.h"
#include "../Graphics/D3D11/D3D11Buffer.h"
//========================================

//= NAMESPACES ================
using namespace Directus::Math;
using namespace std;
//=============================

LineRenderer::LineRenderer()
{
	m_vertexBuffer = nullptr;
}

LineRenderer::~LineRenderer()
{
	m_vertices.clear();
}

//= INTERFACE ===================================
void LineRenderer::Initialize()
{
	m_vertexBuffer = make_shared<D3D11Buffer>();
	m_vertexBuffer->Initialize(g_graphicsDevice);

	bool result = m_vertexBuffer->Create(
		sizeof(VertexPositionColor),
		m_maximumVertices,
		nullptr,
		D3D11_USAGE_DYNAMIC,
		D3D11_BIND_VERTEX_BUFFER,
		D3D11_CPU_ACCESS_WRITE
	);

	if (!result)
		LOG("Failed to create a vertex buffer for the LineRenderer component.", Log::Error);
}

void LineRenderer::Update()
{

}

void LineRenderer::Serialize()
{

}

void LineRenderer::Deserialize()
{

}

//= INPUT ===================================================================
void LineRenderer::AddLineList(vector<VertexPositionColor> vertices)
{
	ClearVertices();
	m_vertices = vertices;
}

void LineRenderer::AddLine(Vector3 start, Vector3 end, Vector4 color)
{
	AddVertex(start, color);
	AddVertex(end, color);
}

void LineRenderer::AddVertex(Vector3 position, Vector4 color)
{
	VertexPositionColor vertex;
	vertex.position = position;
	vertex.color = color;
	m_vertices.push_back(vertex);
}

//= PROPERTIES ========================================================================
void LineRenderer::SetBuffer()
{
	UpdateVertexBuffer();

	m_vertexBuffer->SetIA();

	// Set primitive topology
	g_graphicsDevice->GetDeviceContext()->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_LINELIST);

	ClearVertices();
}

unsigned int LineRenderer::GetVertexCount()
{
	return unsigned int(m_vertices.size());
}

//= MISC =============================================================================
void LineRenderer::UpdateVertexBuffer()
{
	if (!m_vertexBuffer)
		return;

	void* pData = m_vertexBuffer->Map();

	if (!pData)
		return;

	// update the vertex buffer.
	memcpy(pData, &m_vertices[0], sizeof(VertexPositionColor) * m_vertices.size());

	m_vertexBuffer->Unmap();
}

void LineRenderer::ClearVertices()
{
	m_vertices.clear();
	m_vertices.shrink_to_fit();
}
