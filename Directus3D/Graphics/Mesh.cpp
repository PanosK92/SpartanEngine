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

//= INCLUDES =====================
#include "../Graphics/Mesh.h"
#include "../IO/Log.h"
#include "../Core/GUIDGenerator.h"
#include "../IO/FileSystem.h"
//================================

//= NAMESPACES ================
using namespace std;
using namespace Directus::Math;
//=============================

Mesh::Mesh()
{
	m_ID = GENERATE_GUID;
	m_vertexCount = 0;
	m_indexCount = 0;
	m_triangleCount = 0;
	m_min = Vector3::InfinityNeg;
	m_min = Vector3::Infinity;
	m_center = Vector3::Zero;
	m_boundingBox = Vector3::One;
	m_onUpdate = nullptr;
}

Mesh::~Mesh()
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

//= IO =========================================================================
void Mesh::Serialize()
{
	Serializer::SaveSTR(m_ID);
	Serializer::SaveSTR(m_gameObjectID);
	Serializer::SaveSTR(m_rootGameObjectID);
	Serializer::SaveSTR(m_name);
	Serializer::SaveInt(m_vertexCount);
	Serializer::SaveInt(m_indexCount);
	Serializer::SaveInt(m_triangleCount);

	for (auto i = 0; i < m_vertexCount; i++)
		SaveVertex(m_vertices[i]);

	for (auto i = 0; i < m_indexCount; i++)
		Serializer::SaveInt(m_indices[i]);

	Serializer::SaveVector3(m_min);
	Serializer::SaveVector3(m_max);
	Serializer::SaveVector3(m_center);
	Serializer::SaveVector3(m_boundingBox);
}

void Mesh::Deserialize()
{
	m_ID = Serializer::LoadSTR();
	m_gameObjectID = Serializer::LoadSTR();
	m_rootGameObjectID = Serializer::LoadSTR();
	m_name = Serializer::LoadSTR();
	m_vertexCount = Serializer::LoadInt();
	m_indexCount = Serializer::LoadInt();
	m_triangleCount = Serializer::LoadInt();

	for (auto i = 0; i < m_vertexCount; i++)
		m_vertices.push_back(LoadVertex());

	for (auto i = 0; i < m_indexCount; i++)
		m_indices.push_back(Serializer::LoadInt());

	m_min = Serializer::LoadVector3();
	m_max = Serializer::LoadVector3();
	m_center = Serializer::LoadVector3();
	m_boundingBox = Serializer::LoadVector3();
}

void Mesh::SaveToFile(const string& path)
{
	m_filePath = path + GetName() + MESH_EXTENSION;

	Serializer::StartWriting(m_filePath);
	Serialize();
	Serializer::StopWriting();
}

bool Mesh::LoadFromFile(const string& filePath)
{
	if (!FileSystem::FileExists(filePath))
		return false;

	Serializer::StartReading(filePath);
	Deserialize();
	Serializer::StopReading();

	return true;
}

//==============================================================================

//= PROCESSING =================================================================
void Mesh::Update()
{
	GetMinMax(this, m_min, m_max);
	m_center = GetCenter(m_min, m_max);
	m_boundingBox = GetBoundingBox(m_min, m_max);

	if (m_onUpdate)
		m_onUpdate();
}

// This is usually attached CreateBuffers function of the MeshFilter component.
// Whenever something changes, the buffers are auto-updated.
void Mesh::OnUpdate(function<void()> function)
{
	m_onUpdate = function;

	if (m_onUpdate)
		m_onUpdate();
}

void Mesh::Scale(float scale)
{
	SetScale(this, scale);
	Update();
}
//==============================================================================

//= IO =========================================================================
void Mesh::SaveVertex(const VertexPositionTextureNormalTangent& vertex)
{
	Serializer::SaveFloat(vertex.position.x);
	Serializer::SaveFloat(vertex.position.y);
	Serializer::SaveFloat(vertex.position.z);

	Serializer::SaveFloat(vertex.uv.x);
	Serializer::SaveFloat(vertex.uv.y);

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

	vertex.uv.x = Serializer::LoadFloat();
	vertex.uv.y = Serializer::LoadFloat();

	vertex.normal.x = Serializer::LoadFloat();
	vertex.normal.y = Serializer::LoadFloat();
	vertex.normal.z = Serializer::LoadFloat();

	vertex.tangent.x = Serializer::LoadFloat();
	vertex.tangent.y = Serializer::LoadFloat();
	vertex.tangent.z = Serializer::LoadFloat();

	return vertex;
}
//==============================================================================

//= HELPER FUNCTIONS ===========================================================
void Mesh::SetScale(Mesh* meshData, float scale)
{
	for (auto i = 0; i < meshData->GetVertexCount(); i++)
		meshData->GetVertices()[i].position *= scale;
}

// Returns the bounding box of a mesh
Vector3 Mesh::GetBoundingBox(const Vector3& min, const Vector3& max)
{
	return (max - min) * 0.5f;
}

// Returns the center of the mesh based
Vector3 Mesh::GetCenter(const Vector3& min, const Vector3& max)
{
	return (min + max) * 0.5f;
}

// Returns the minimum and maximum point in a mesh
void Mesh::GetMinMax(Mesh* mesh, Vector3& min, Vector3& max)
{
	if (!mesh)
		return;

	min = Vector3::Infinity;
	max = Vector3::InfinityNeg;

	for (unsigned int i = 0; i < mesh->GetVertexCount(); i++)
	{
		float x = mesh->GetVertices()[i].position.x;
		float y = mesh->GetVertices()[i].position.y;
		float z = mesh->GetVertices()[i].position.z;

		if (x > max.x) max.x = x;
		if (y > max.y) max.y = y;
		if (z > max.z) max.z = z;

		if (x < min.x) min.x = x;
		if (y < min.y) min.y = y;
		if (z < min.z) min.z = z;
	}
}
//==============================================================================