//= INCLUDES ===============
#include "LineRenderer.h"
#include "../Core/Context.h"
#include "../Math/MathHelper.h"
#include "../Math/Matrix.h"
//==========================

//= NAMESPACES ================
using namespace Directus::Math;
using namespace std;
//=============================

namespace Directus
{
	LineRenderer::LineRenderer()
	{
		Register();
	}

	LineRenderer::~LineRenderer()
	{
		m_vertices.clear();
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
	void LineRenderer::AddSphere(const Vector3& center, float radius, const Vector4& color)
	{
		int lats = 5;
		int longs = 5;
		Matrix translation = Matrix::CreateTranslation(center);

		for (int i = 0; i <= lats; i++)
		{
			float lat0 = PI * (-0.5f + (i - 1) / lats);
			float z0 = radius * sin(lat0);
			float zr0 = radius * cos(lat0);

			float lat1 = PI * (-0.5f + i / lats);
			float z1 = radius * sin(lat1);
			float zr1 = radius * cos(lat1);

			for (int j = 0; j <= longs; j++)
			{
				float lng = 2 * PI * (j - 1) / longs;
				float x = cos(lng);
				float y = sin(lng);

				Vector3 from = Vector3(x * zr0, y * zr0, z0);
				Vector3 to = Vector3(x * zr1, y * zr1, z1);

				from = from * translation;
				to = to * translation;

				AddLine(from, to, color);
			}
		}
	}

	void LineRenderer::AddLine(const Vector3& from, const Vector3& to, const Vector4& color)
	{
		AddVertex(VertexPosCol{ from, color });
		AddVertex(VertexPosCol{ to, color });
	}

	void LineRenderer::AddLines(const vector<VertexPosCol>& lineList)
	{
		for (const auto& line : lineList)
		{
			AddVertex(line);
		}
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
		g_context->GetSubsystem<Graphics>()->SetPrimitiveTopology(LineList);
	}

	void LineRenderer::CreateVertexBuffer()
	{
		m_vertexBuffer = make_shared<D3D11VertexBuffer>(g_context->GetSubsystem<Graphics>());
		m_vertexBuffer->CreateDynamic(sizeof(VertexPosCol), (UINT)m_vertices.size());
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