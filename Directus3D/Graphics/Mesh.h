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

//= INCLUDES ================
#include <vector>
#include "Vertex.h"
#include "../IO/Serializer.h"
#include <functional>
#include "../IO/FileSystem.h"
//===========================

#define MESH_EXTENSION ".msh"

class Mesh
{
public:
	Mesh();
	~Mesh();

	//= IO =========================================================================
private:
	void Serialize();
	void Deserialize();
public:
	void SaveToDirectory(const std::string& directory, bool overwrite);
	bool LoadFromFile(const std::string& filePath);
	//==============================================================================

	std::string GetName() const { return m_name; }
	void SetName(const std::string& name) { m_name = name; }

	std::string GetID() const { return m_ID; }
	void SetID(const std::string& ID) { m_ID = ID; }

	std::string GetRootGameObjectID() const { return m_rootGameObjectID; }
	void SetRootGameObjectID(const std::string& rootGameObjectID) { m_rootGameObjectID = rootGameObjectID; }

	void SetDirectory(const std::string& directory) { m_directory = directory; }

	std::string GetFilePath() { return m_filePath; }
	
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

	const Directus::Math::Vector3& GetMin() const { return m_min; }
	const Directus::Math::Vector3& GetMax() const { return m_max; }
	const Directus::Math::Vector3& GetCenter() const { return m_center; }
	const Directus::Math::Vector3& GetBoundingBox() const { return m_boundingBox; }

	//= PROCESSING =================================================================
	void Update();
	void OnUpdate(std::function<void(void)> function);
	void SetScale(float scale);
	//==============================================================================

private:
	//= IO =========================================================================
	static void Mesh::SaveVertex(const VertexPositionTextureNormalTangent& vertex);
	static void Mesh::LoadVertex(VertexPositionTextureNormalTangent& vertex);
	//==============================================================================

	//= HELPER FUNCTIONS ===========================================================
	static void SetScale(Mesh* meshData, float scale);
	static Directus::Math::Vector3 GetBoundingBox(const Directus::Math::Vector3& min, const Directus::Math::Vector3& max);
	static Directus::Math::Vector3 GetCenter(const Directus::Math::Vector3& min, const Directus::Math::Vector3& max);
	static void GetMinMax(Mesh* mesh, Directus::Math::Vector3& min, Directus::Math::Vector3& max);
	//==============================================================================

	std::string m_name;
	std::string m_ID;
	std::string m_rootGameObjectID;
	std::string m_directory;
	std::string m_filePath;

	std::vector<VertexPositionTextureNormalTangent> m_vertices;
	std::vector<unsigned int> m_indices;

	unsigned int m_vertexCount;
	unsigned int m_indexCount;
	unsigned int m_triangleCount;

	Directus::Math::Vector3 m_min;
	Directus::Math::Vector3 m_max;
	Directus::Math::Vector3 m_center;
	Directus::Math::Vector3 m_boundingBox;

	std::function<void(void)> m_onUpdate;
};
