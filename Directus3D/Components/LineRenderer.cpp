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
	delete[] m_vertices;
}

void LineRenderer::Initialize()
{
	// initialize vertex array
	m_vertices = new VertexPositionColor[m_maxVertices];
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
void LineRenderer::AddLine(Vector3 start, Vector3 end, Vector4 color)
{
	AddVertex(start, color);
	AddVertex(end, color);
}

void LineRenderer::AddPoint(Vector3 point, Vector4 color)
{
	AddVertex(point, color);
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

/*------------------------------------------------------------------------------
								[MISC]
------------------------------------------------------------------------------*/

void LineRenderer::AddVertex(Vector3 vertex, Vector4 color)
{
	m_vertices[m_vertexIndex].position = vertex;
	m_vertices[m_vertexIndex].color = color;
	m_vertexIndex++;
}

void LineRenderer::CreateDynamicVertexBuffer()
{
	// Set up dynamic vertex buffer
	D3D11_BUFFER_DESC vertexBufferDesc;
	vertexBufferDesc.ByteWidth = sizeof(VertexPositionColor) * m_maxVertices;
	vertexBufferDesc.Usage = D3D11_USAGE_DYNAMIC;
	vertexBufferDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
	vertexBufferDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
	vertexBufferDesc.MiscFlags = 0;
	vertexBufferDesc.StructureByteStride = 0;

	// Give the subresource structure a pointer to the vertex data.
	D3D11_SUBRESOURCE_DATA subResourceData;
	subResourceData.pSysMem = m_vertices;

	// Create the vertex buffer.
	HRESULT result = g_d3d11Device->GetDevice()->CreateBuffer(&vertexBufferDesc, &subResourceData, &m_vertexBuffer);
	if (FAILED(result))
	LOG("Failed to create line renderer dynamic vertex buffer.", Log::Error);
}

void LineRenderer::UpdateVertexBuffer()
{
	D3D11_MAPPED_SUBRESOURCE mappedResource;

	// disable GPU access to the vertex buffer data.	
	g_d3d11Device->GetDeviceContext()->Map(m_vertexBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedResource);

	// update the vertex buffer.
	memcpy(mappedResource.pData, &m_vertices[0], sizeof(VertexPositionColor) * m_maxVertices);

	// re-enable GPU access to the vertex buffer data.
	g_d3d11Device->GetDeviceContext()->Unmap(m_vertexBuffer, 0);
}

void LineRenderer::ClearVertices()
{
	for (int i = 0; i < m_maxVertices; i++)
	{
		m_vertices[i].position = Vector3(0, 0, 0);
		m_vertices[i].color = Vector4(0, 0, 0, 0);
	}

	m_vertexIndex = 0;
}
