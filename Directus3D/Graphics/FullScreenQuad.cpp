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

//= INCLUDES ===============
#include "FullScreenQuad.h"
#include "../Core/Helper.h"
#include "../Graphics/Vertex.h"
//==========================

//= NAMESPACES ================
using namespace Directus::Math;
//=============================

FullScreenQuad::FullScreenQuad()
{
	m_graphics = nullptr;
	m_vertexBuffer = nullptr;
	m_indexBuffer = nullptr;
}

FullScreenQuad::~FullScreenQuad()
{
	SafeRelease(m_vertexBuffer);
	SafeRelease(m_indexBuffer);
}

bool FullScreenQuad::Initialize(int windowWidth, int windowHeight, Graphics* graphicsDevice)
{
	m_graphics = graphicsDevice;

	// Initialize the vertex and index buffer that hold the geometry for the ortho window model.
	bool result = InitializeBuffers(windowWidth, windowHeight);
	if (!result)
		return false;

	return true;
}

void FullScreenQuad::SetBuffers()
{
	// Put the vertex and index buffers on the graphics pipeline to prepare them for drawing.
	unsigned int stride;
	unsigned int offset;

	// Set vertex buffer stride and offset.
	stride = sizeof(VertexPositionTexture);
	offset = 0;

	// Set the vertex buffer to active in the input assembler so it can be rendered.
	m_graphics->GetDeviceContext()->IASetVertexBuffers(0, 1, &m_vertexBuffer, &stride, &offset);

	// Set the index buffer to active in the input assembler so it can be rendered.
	m_graphics->GetDeviceContext()->IASetIndexBuffer(m_indexBuffer, DXGI_FORMAT_R32_UINT, 0);

	// Set the type of primitive that should be rendered from this vertex buffer, in this case triangles.
	m_graphics->GetDeviceContext()->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
}


int FullScreenQuad::GetIndexCount()
{
	return m_indexCount;
}

bool FullScreenQuad::InitializeBuffers(int windowWidth, int windowHeight)
{
	float left, right, top, bottom;
	VertexPositionTexture* vertices;
	unsigned long* indices;
	D3D11_BUFFER_DESC vertexBufferDesc, indexBufferDesc;
	D3D11_SUBRESOURCE_DATA vertexData, indexData;
	HRESULT result;
	int i;

	// Calculate the screen coordinates of the left side of the window.
	left = static_cast<float>((windowWidth / 2) * -1);

	// Calculate the screen coordinates of the right side of the window.
	right = left + static_cast<float>(windowWidth);

	// Calculate the screen coordinates of the top of the window.
	top = static_cast<float>(windowHeight / 2);

	// Calculate the screen coordinates of the bottom of the window.
	bottom = top - static_cast<float>(windowHeight);

	// Set the number of vertices in the vertex array.
	m_vertexCount = 6;

	// Set the number of indices in the index array.
	m_indexCount = m_vertexCount;

	// Create the vertex array.
	vertices = new VertexPositionTexture[m_vertexCount];
	if (!vertices)
		return false;

	// Create the index array.
	indices = new unsigned long[m_indexCount];
	if (!indices)
		return false;

	// Load the vertex array with data.
	// First triangle.
	vertices[0].position = Vector3(left, top, 0.0f); // Top left.
	vertices[0].uv = Vector2(0.0f, 0.0f);

	vertices[1].position = Vector3(right, bottom, 0.0f); // Bottom right.
	vertices[1].uv = Vector2(1.0f, 1.0f);

	vertices[2].position = Vector3(left, bottom, 0.0f); // Bottom left.
	vertices[2].uv = Vector2(0.0f, 1.0f);

	// Second triangle.
	vertices[3].position = Vector3(left, top, 0.0f); // Top left.
	vertices[3].uv = Vector2(0.0f, 0.0f);

	vertices[4].position = Vector3(right, top, 0.0f);// Top right.
	vertices[4].uv = Vector2(1.0f, 0.0f);

	vertices[5].position = Vector3(right, bottom, 0.0f); // Bottom right.
	vertices[5].uv = Vector2(1.0f, 1.0f);

	// Load the index array with data.
	for (i = 0; i < m_indexCount; i++)
		indices[i] = i;

	// Set up the description of the vertex buffer.
	vertexBufferDesc.Usage = D3D11_USAGE_DEFAULT;
	vertexBufferDesc.ByteWidth = sizeof(VertexPositionTexture) * m_vertexCount;
	vertexBufferDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
	vertexBufferDesc.CPUAccessFlags = 0;
	vertexBufferDesc.MiscFlags = 0;
	vertexBufferDesc.StructureByteStride = 0;

	// Give the subresource structure a pointer to the vertex data.
	vertexData.pSysMem = vertices;
	vertexData.SysMemPitch = 0;
	vertexData.SysMemSlicePitch = 0;

	// Now finally create the vertex buffer.
	result = m_graphics->GetDevice()->CreateBuffer(&vertexBufferDesc, &vertexData, &m_vertexBuffer);
	if (FAILED(result))
		return false;

	// Set up the description of the index buffer.
	indexBufferDesc.Usage = D3D11_USAGE_DEFAULT;
	indexBufferDesc.ByteWidth = sizeof(unsigned long) * m_indexCount;
	indexBufferDesc.BindFlags = D3D11_BIND_INDEX_BUFFER;
	indexBufferDesc.CPUAccessFlags = 0;
	indexBufferDesc.MiscFlags = 0;
	indexBufferDesc.StructureByteStride = 0;

	// Give the subresource structure a pointer to the index data.
	indexData.pSysMem = indices;
	indexData.SysMemPitch = 0;
	indexData.SysMemSlicePitch = 0;

	// Create the index buffer.
	result = m_graphics->GetDevice()->CreateBuffer(&indexBufferDesc, &indexData, &m_indexBuffer);
	if (FAILED(result))
		return false;

	// Release the arrays now that the vertex and index buffers have been created and loaded.
	delete[] vertices;
	vertices = nullptr;

	delete[] indices;
	indices = nullptr;

	return true;
}
