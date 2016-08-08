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

#pragma once

//= INCLUDES =============
#include <vector>
#include "Vertex.h"
#include "GUIDGenerator.h"
#include "../IO/Serializer.h"

//========================

class Mesh
{
public:
	Mesh()
	{
		m_ID = GENERATE_GUID;
		m_vertexCount = 0;
		m_indexCount = 0;
		m_triangleCount = 0;
	}

	~Mesh()
	{
		m_vertices.clear();
		m_indices.clear();
		m_name.clear();
		m_rootGameObjectID.clear();
		m_ID.clear();
		m_gameObjectID.clear();
		m_vertexCount = 0;
		m_indexCount = 0;
		m_triangleCount = 0;
	}

	void Serialize()
	{
		Serializer::SaveSTR(m_ID);
		Serializer::SaveSTR(m_gameObjectID);
		Serializer::SaveSTR(m_rootGameObjectID);	
		Serializer::SaveInt(m_vertexCount);
		Serializer::SaveInt(m_indexCount);
		Serializer::SaveInt(m_triangleCount);

                for (auto i = 0; i < m_vertexCount; i++)
			SaveVertex(m_vertices[i]);

                for (auto i = 0; i < m_indexCount; i++)
			Serializer::SaveInt(m_indices[i]); 
	}

	void Deserialize()
	{
		m_ID = Serializer::LoadSTR();
		m_gameObjectID = Serializer::LoadSTR();
		m_rootGameObjectID = Serializer::LoadSTR();
		m_vertexCount = Serializer::LoadInt();
		m_indexCount = Serializer::LoadInt();
		m_triangleCount = Serializer::LoadInt();

                for (auto i = 0; i < m_vertexCount; i++)
			m_vertices.push_back(LoadVertex());

                for (auto i = 0; i < m_indexCount; i++)
			m_indices.push_back(Serializer::LoadInt());
	}

	std::string GetName() const { return m_name; }
	void SetName(std::string name) { m_name = name; }

	std::string GetID() const { return m_ID; }

	std::string GetGameObjectID() const { return m_gameObjectID; }
	void SetGameObjectID(std::string ID) { m_gameObjectID = ID; }

	std::string GetRootGameObjectID() const { return m_rootGameObjectID; }
	void SetRootGameObjectID(std::string ID) { m_rootGameObjectID = ID; }

	std::vector<VertexPositionTextureNormalTangent>& GetVertices() { return m_vertices; }
	void SetVertices(std::vector<VertexPositionTextureNormalTangent> vertices)
	{
		m_vertices = vertices;
		m_vertexCount = (unsigned int)vertices.size();
	}

	std::vector<unsigned int>& GetIndices() { return m_indices; }
	void SetIndices(std::vector<unsigned int> indices)
	{
		m_indices = indices;
		m_indexCount = (unsigned int)indices.size();
		m_triangleCount = m_indexCount / 3;
	}

	unsigned int GetVertexCount() const { return m_vertexCount; }
	unsigned int GetIndexCount() const { return m_indexCount; }
	unsigned int GetTriangleCount() const { return m_triangleCount; }
	unsigned int GetIndexStart() { return !m_indices.empty() ? m_indices.front() : 0; }

private:
	void Mesh::SaveVertex(const VertexPositionTextureNormalTangent& vertex)
	{
		Serializer::SaveFloat(vertex.position.x);
		Serializer::SaveFloat(vertex.position.y);
		Serializer::SaveFloat(vertex.position.z);

		Serializer::SaveFloat(vertex.texture.x);
		Serializer::SaveFloat(vertex.texture.y);

		Serializer::SaveFloat(vertex.normal.x);
		Serializer::SaveFloat(vertex.normal.y);
		Serializer::SaveFloat(vertex.normal.z);

		Serializer::SaveFloat(vertex.tangent.x);
		Serializer::SaveFloat(vertex.tangent.y);
		Serializer::SaveFloat(vertex.tangent.z);
	}

	VertexPositionTextureNormalTangent Mesh::LoadVertex()
	{
		VertexPositionTextureNormalTangent vertex;

		vertex.position.x = Serializer::LoadFloat();
		vertex.position.y = Serializer::LoadFloat();
		vertex.position.z = Serializer::LoadFloat();

		vertex.texture.x = Serializer::LoadFloat();
		vertex.texture.y = Serializer::LoadFloat();

		vertex.normal.x = Serializer::LoadFloat();
		vertex.normal.y = Serializer::LoadFloat();
		vertex.normal.z = Serializer::LoadFloat();

		vertex.tangent.x = Serializer::LoadFloat();
		vertex.tangent.y = Serializer::LoadFloat();
		vertex.tangent.z = Serializer::LoadFloat();

		return vertex;
	}


	std::string m_name;
	std::string m_ID;
	std::string m_gameObjectID;
	std::string m_rootGameObjectID;
	
	std::vector<VertexPositionTextureNormalTangent> m_vertices;
	std::vector<unsigned int> m_indices;

	unsigned int m_vertexCount;
	unsigned int m_indexCount;
	unsigned int m_triangleCount;
};
