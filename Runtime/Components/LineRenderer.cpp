//= INCLUDES ====================
#include "LineRenderer.h"
#include "../Core/Context.h"
#include "../Math/Matrix.h"
#include "../Math/BoundingBox.h"
//==============================

//= NAMESPACES ================
using namespace Directus::Math;
using namespace std;
//=============================

namespace Directus
{
	LineRenderer::LineRenderer()
	{

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

	void LineRenderer::Serialize(StreamIO* stream)
	{

	}

	void LineRenderer::Deserialize(StreamIO* stream)
	{

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