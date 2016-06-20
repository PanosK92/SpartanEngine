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
#include "../Misc/Globals.h"
#include "../IO/Log.h"
#include "../Graphics/D3D11/D3D11Buffer.h"
//========================================

//= NAMESPACES ================
using namespace Directus::Math;
//=============================

LineRenderer::LineRenderer()
{
	m_vertexBuffer = nullptr;
}

LineRenderer::~LineRenderer()
{
	DirectusSafeRelease(m_vertexBuffer);
	m_vertices.clear();
}

void LineRenderer::Initialize()
{
	// initialize vertex array
	ClearVertices();

	// craete buffer
	CreateDynamicVertexBuffer();
}

void LineRenderer::Update()
{

}

void LineRenderer::Save()
{

}

void LineRenderer::Load()
{

}

/*------------------------------------------------------------------------------
								[INPUT]
------------------------------------------------------------------------------*/
void LineRenderer::AddLineList(std::vector<VertexPositionColor> vertices)
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

/*------------------------------------------------------------------------------
								[MISC]
------------------------------------------------------------------------------*/
void LineRenderer::SetBuffer()
{
	UpdateVertexBuffer();

	// Set vertex buffer stride and offset.
	unsigned int stride = sizeof(VertexPositionColor);
	unsigned int offset = 0;

	// Set vertex buffer
	g_d3d11Device->GetDeviceContext()->IASetVertexBuffers(0, 1, &m_vertexBuffer, &stride, &offset);

	// Set primitive topology
	g_d3d11Device->GetDeviceContext()->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_LINELIST);

	ClearVertices();
}

unsigned int LineRenderer::GetVertexCount()
{
	return unsigned int(m_vertices.size());
}

/*------------------------------------------------------------------------------
								[MISC]
------------------------------------------------------------------------------*/
void LineRenderer::CreateDynamicVertexBuffer()
{
	if (m_vertices.empty())
		return;

	DirectusSafeRelease(m_vertexBuffer);

	// Set up dynamic vertex buffer
	D3D11_BUFFER_DESC vertexBufferDesc;
	vertexBufferDesc.ByteWidth = sizeof(VertexPositionColor) * 1000000;
	vertexBufferDesc.Usage = D3D11_USAGE_DYNAMIC;
	vertexBufferDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
	vertexBufferDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
	vertexBufferDesc.MiscFlags = 0;
	vertexBufferDesc.StructureByteStride = 0;

	// Give the subresource structure a pointer to the vertex data.
	D3D11_SUBRESOURCE_DATA subResourceData;
	subResourceData.pSysMem = &m_vertices[0];

	// Create the vertex buffer.
	HRESULT result = g_d3d11Device->GetDevice()->CreateBuffer(&vertexBufferDesc, &subResourceData, &m_vertexBuffer);
	if (FAILED(result))
		LOG("Failed to create line renderer dynamic vertex buffer.", Log::Error);
}

void LineRenderer::UpdateVertexBuffer()
{
	if (m_vertices.empty())
		return;

	if (!m_vertexBuffer)
	{
		CreateDynamicVertexBuffer();
		return;
	}

	D3D11_MAPPED_SUBRESOURCE mappedResource;

	// disable GPU access to the vertex buffer data.	
	g_d3d11Device->GetDeviceContext()->Map(m_vertexBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedResource);

	// update the vertex buffer.
	memcpy(mappedResource.pData, &m_vertices[0], sizeof(VertexPositionColor) * m_vertices.size());

	// re-enable GPU access to the vertex buffer data.
	g_d3d11Device->GetDeviceContext()->Unmap(m_vertexBuffer, 0);
}

void LineRenderer::ClearVertices()
{
	m_vertices.clear();
}
