//= INCLUDES ================
#include "LineRenderer.h"
#include "../IO/Log.h"
//===========================

using namespace Directus::Math;

LineRenderer::LineRenderer()
{
	m_vertexBuffer = nullptr;
	m_vertices = nullptr;
	m_vertexIndex = 0;
}

LineRenderer::~LineRenderer()
{
	delete m_vertexBuffer;
	delete[] m_vertices;
}

void LineRenderer::Initialize()
{
	// create vertex array
	m_vertices = new VertexPositionColor[m_maxVertices];

	// create vertex buffer
	m_vertexBuffer = new D3D11Buffer();
	m_vertexBuffer->Initialize(g_graphicsDevice);
	m_vertexBuffer->Create(
		sizeof(VertexPositionColor),
		m_maxVertices,
		nullptr,
		D3D11_USAGE_DYNAMIC,
		D3D11_BIND_VERTEX_BUFFER,
		D3D11_CPU_ACCESS_WRITE
	);
}

void LineRenderer::Remove()
{

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
void LineRenderer::AddLineList(const std::vector<VertexPositionColor>& lineList)
{
	ClearVertices();

	for (int i = 0; i < lineList.size(); i++)
		AddVertex(lineList[i]);
}

void LineRenderer::AddVertex(const VertexPositionColor& vertex)
{
	m_vertices[m_vertexIndex] = vertex;
	m_vertexIndex++;
}

//= MISC ================================================================
void LineRenderer::SetBuffer()
{
	UpdateVertexBuffer();

	// Set vertex buffer
	m_vertexBuffer->SetIA();

	// Set primitive topology
	g_graphicsDevice->GetDeviceContext()->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_LINELIST);

	ClearVertices();
}

//= MISC ================================================================
void LineRenderer::UpdateVertexBuffer()
{
	// disable GPU access to the vertex buffer data.	
	void* data = m_vertexBuffer->Map();

	// update the vertex buffer.
	memcpy(data, &m_vertices[0], sizeof(VertexPositionColor) * m_maxVertices);

	// re-enable GPU access to the vertex buffer data.
	m_vertexBuffer->Unmap();
}

void LineRenderer::ClearVertices()
{
	m_vertexIndex = 0;
}
