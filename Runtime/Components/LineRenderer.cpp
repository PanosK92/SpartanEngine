//= INCLUDES ===============
#include "LineRenderer.h"
#include "../Core/Context.h"
//==========================

//= NAMESPACES ================
using namespace Directus::Math;
using namespace std;
//=============================

namespace Directus
{
	LineRenderer::LineRenderer()
	{
		m_vertexBuffer = nullptr;
		m_vertices = nullptr;
		m_vertexIndex = 0;
		m_maxVertices = 0;
	}

	LineRenderer::~LineRenderer()
	{
		delete[] m_vertices;
	}

	void LineRenderer::Reset()
	{

	}

	void LineRenderer::Start()
	{

	}

	void LineRenderer::OnDisable()
	{

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
	void LineRenderer::AddLineList(const vector<VertexPositionColor>& lineList)
	{
		ClearVertices();

		// Grow if needed, otherwise we'll crash
		if (lineList.size() >= m_maxVertices)
		{
			m_maxVertices = (int)lineList.size();
			CreateBuffer();
		}

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
		g_context->GetSubsystem<D3D11GraphicsDevice>()->SetPrimitiveTopology(LineList);

		ClearVertices();
	}

	void LineRenderer::CreateBuffer()
	{
		if (m_vertices)
		{
			delete[] m_vertices;
			m_vertices = nullptr;
		}

		// create vertex array
		m_vertices = new VertexPositionColor[m_maxVertices];

		// create vertex buffer
		m_vertexBuffer = make_shared<D3D11VertexBuffer>(g_context->GetSubsystem<D3D11GraphicsDevice>());
		m_vertexBuffer->CreateDynamic(sizeof(VertexPositionColor), m_maxVertices);
	}

	//= MISC ================================================================
	void LineRenderer::UpdateVertexBuffer()
	{
		if (!m_vertexBuffer)
			return;

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
}