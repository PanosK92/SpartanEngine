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
#include <functional>
#include "Vertex.h"
#include "../Math/BoundingBox.h"
//==============================

namespace Directus
{
	class Mesh
	{
	public:
		Mesh();
		~Mesh();

		void Serialize();
		void Deserialize();

		std::string GetID() { return m_id; }

		std::string GetGameObjectID() { return m_gameObjID; }
		void SetGameObjectID(const std::string& gameObjID) { m_gameObjID = gameObjID; }

		std::string GetModelID() { return m_modelID; }
		void SetModelID(const std::string& modelID) { m_modelID = modelID; }

		std::string GetName() { return m_name; }
		void SetName(const std::string& name) { m_name = name; }

		std::vector<VertexPosTexNorTan>& GetVertices() { return m_vertices; }
		void SetVertices(const std::vector<VertexPosTexNorTan>& vertices);

		std::vector<unsigned int>& GetIndices() { return m_indices; }
		void SetIndices(const std::vector<unsigned int>& indices);

		unsigned int GetVertexCount() const { return m_vertexCount; }
		unsigned int GetIndexCount() const { return m_indexCount; }
		unsigned int GetTriangleCount() const { return m_triangleCount; }
		unsigned int GetIndexStart() { return !m_indices.empty() ? m_indices.front() : 0; }

		Math::BoundingBox GetBoundingBox() const { return m_boundingBox; }

		//= PROCESSING =================================================================
		void Update();
		void SubscribeToUpdate(std::function<void(void)> function);
		void SetScale(float scale);
		//==============================================================================

	private:
		//= IO =========================================================================
		static void SaveVertex(const VertexPosTexNorTan& vertex);
		static void LoadVertex(VertexPosTexNorTan& vertex);
		//==============================================================================

		//= HELPER FUNCTIONS =============================
		static void SetScale(Mesh* meshData, float scale);
		//================================================

		std::string m_id;
		std::string m_gameObjID;
		std::string m_modelID;
		std::string m_name;	

		std::vector<VertexPosTexNorTan> m_vertices;
		std::vector<unsigned int> m_indices;

		unsigned int m_vertexCount;
		unsigned int m_indexCount;
		unsigned int m_triangleCount;

		Math::BoundingBox m_boundingBox;

		std::function<void(void)> m_onUpdate;
	};
}