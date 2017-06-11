#pragma once

//= INCLUDES ===================================
#include "IComponent.h"
#include "../Graphics/Vertex.h"
#include "../Graphics/D3D11/D3D11VertexBuffer.h"
#include <memory>
#include <vector>
//=============================================

namespace Directus
{
	class DLL_API LineRenderer : public IComponent
	{
	public:
		LineRenderer();
		~LineRenderer();

		virtual void Reset();
		virtual void Start();
		virtual void OnDisable();
		virtual void Remove();
		virtual void Update();
		virtual void Serialize();
		virtual void Deserialize();

		//= INPUT ==================================================
		void AddLineList(const std::vector<VertexPosCol>& lineList);
		void AddVertex(const VertexPosCol& vertex);

		//= MISC ===================================================
		void SetBuffer();
		unsigned int GetVertexCount() { return m_maxVertices; };

	private:
		void CreateBuffer();

		//= VERTICES ===================	
		std::shared_ptr<D3D11VertexBuffer> m_vertexBuffer;
		VertexPosCol* m_vertices;
		int m_maxVertices;
		int m_vertexIndex;
		//==============================

		//= MISC ========================
		void UpdateVertexBuffer();
		void ClearVertices();
		//==============================
	};
}
