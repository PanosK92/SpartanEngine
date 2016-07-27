//= INCLUDES ================
#include "LineRenderer.h"
#include "../Core/Globals.h"
#include "../IO/Log.h"
//===========================

using namespace Directus::Math;

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

	// create buffer
	CreateDynamicVertexBuffer();
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

//= INPUT ===============================================================
void LineRenderer::AddLineList(std::vector<VertexPositionColor> lineList)
{
	ClearVertices();

	for (int i = 0; i < lineList.size(); i++)
		AddVertex(lineList[i]);
}

void LineRenderer::AddVertex(VertexPositionColor vertex)
{
	m_vertices[m_vertexIndex] = vertex;
	m_vertexIndex++;
}

//= MISC ================================================================
void LineRenderer::SetBuffer()
{
	UpdateVertexBuffer();

	// Set vertex buffer stride and offset.
	UINT stride = sizeof(VertexPositionColor);
	UINT offset = 0;

	// Set vertex buffer
	g_graphicsDevice->GetDeviceContext()->IASetVertexBuffers(0, 1, &m_vertexBuffer, &stride, &offset);

	// Set primitive topology
	g_graphicsDevice->GetDeviceContext()->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_LINELIST);

	ClearVertices();
}

//= MISC ================================================================
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
	HRESULT result = g_graphicsDevice->GetDevice()->CreateBuffer(&vertexBufferDesc, &subResourceData, &m_vertexBuffer);
	if (FAILED(result))
		LOG("Failed to create line renderer dynamic vertex buffer.");
}

void LineRenderer::UpdateVertexBuffer()
{
	D3D11_MAPPED_SUBRESOURCE mappedResource;

	// disable GPU access to the vertex buffer data.	
	g_graphicsDevice->GetDeviceContext()->Map(m_vertexBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedResource);

	// update the vertex buffer.
	memcpy(mappedResource.pData, &m_vertices[0], sizeof(VertexPositionColor) * m_maxVertices);

	// re-enable GPU access to the vertex buffer data.
	g_graphicsDevice->GetDeviceContext()->Unmap(m_vertexBuffer, 0);
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
