/*
Copyright(c) 2016-2017 Panos Karabelas

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

#pragma once

//= INCLUDES ==================
#include <vector>
#include "Vertex.h"
#include "../Math/BoundingBox.h"
#include "../Resource/Resource.h"
//==============================

namespace Directus
{
	class Context;
	class FileStream;
	class D3D11VertexBuffer;
	class D3D11IndexBuffer;

	class Mesh : public Resource
	{
	public:
		Mesh(Context* context);
		~Mesh();

		void ClearVerticesAndIndices();
		void Clear();

		//= RESOURCE INTERFACE =============================================
		bool LoadFromFile(const std::string& filePath) override;
		bool SaveToFile(const std::string& filePath) override;
		unsigned int GetMemoryUsageKB() override { return m_memoryUsageKB; }
		//==================================================================

		unsigned int GetGameObjectID() { return m_gameObjID; }
		void SetGameObjectID(unsigned int gameObjID) { m_gameObjID = gameObjID; }

		unsigned int GetModelID() { return m_modelID; }
		void SetModelID(unsigned int modelID) { m_modelID = modelID; }

		void GetGeometry(std::vector<VertexPosTexTBN>* vertices, std::vector<unsigned int>* indices);
		void SetVertices(const std::vector<VertexPosTexTBN>& vertices) { m_vertices = vertices; }
		void SetIndices(const std::vector<unsigned int>& indices) { m_indices = indices; }

		// Adds a single vertex
		void AddVertex(VertexPosTexTBN vertex) { m_vertices.push_back(vertex); }
		// Adds a single index
		void AddIndex(unsigned int index) { m_indices.push_back(index); }

		unsigned int GetVertexCount() const { return m_vertexCount; }
		unsigned int GetIndexCount() const { return m_indexCount; }
		unsigned int GetTriangleCount() const { return m_triangleCount; }
		unsigned int GetIndexStart() { return !m_indices.empty() ? m_indices.front() : 0; }
		const Math::BoundingBox& GetBoundingBox() { return m_boundingBox; }

		bool Construct();
		// Set the buffers to active in the input assembler so they can be rendered.
		bool SetBuffers();

	private:
		//= HELPER FUNCTIONS ===============
		bool ConstructBuffers();
		unsigned int ComputeMemoryUsageKB();
		//==================================

		unsigned int m_gameObjID;
		unsigned int m_modelID;
		std::vector<VertexPosTexTBN> m_vertices;
		std::vector<unsigned int> m_indices;
		unsigned int m_vertexCount;
		unsigned int m_indexCount;
		unsigned int m_triangleCount;
		std::shared_ptr<D3D11VertexBuffer> m_vertexBuffer;
		std::shared_ptr<D3D11IndexBuffer> m_indexBuffer;
		Math::BoundingBox m_boundingBox;
		unsigned int m_memoryUsageKB;
		Context* m_context;
	};
}