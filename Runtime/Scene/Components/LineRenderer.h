#pragma once

//= INCLUDES ======================================
#include "Component.h"
#include <memory>
#include <vector>
#include "../../Graphics/Vertex.h"
#include "../../Graphics/D3D11/D3D11VertexBuffer.h"
//=================================================

namespace Directus
{
	namespace Math
	{
		class BoundingBox;
		class Vector3;
	}

	class ENGINE_CLASS LineRenderer : public Component
	{
	public:
		LineRenderer();
		~LineRenderer();

		//= INPUT ===================================================================================
		void AddBoundigBox(const Math::BoundingBox& box, const Math::Vector4& color);
		void AddLine(const Math::Vector3& from, const Math::Vector3& to, const Math::Vector4& color);	
		void AddLines(const std::vector<VertexPosCol>& lineList);
		void AddVertex(const VertexPosCol& line);
		void ClearVertices();

		//= MISC ================================================================
		void CreateVertexBuffer();
		void SetBuffer();
		unsigned int GetVertexCount() { return (unsigned int)m_vertices.size(); }

	private:
		//= VERTICES =====================================
		std::shared_ptr<D3D11VertexBuffer> m_vertexBuffer;
		std::vector<VertexPosCol> m_vertices;
		//================================================

		//= MISC =================
		void UpdateVertexBuffer();
		//========================
	};
}
