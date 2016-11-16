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

//= INCLUDES ========================
#include "../Graphics/Mesh.h"
#include "../Logging/Log.h"
#include "../Core/GUIDGenerator.h"
#include "../FileSystem/FileSystem.h"
//===================================

//= NAMESPACES ================
using namespace std;
using namespace Directus::Math;
//=============================

Mesh::Mesh()
{
	m_ID = GENERATE_GUID;
	m_rootGameObjectID = DATA_NOT_ASSIGNED;
	m_directory = DATA_NOT_ASSIGNED;
	m_filePath = DATA_NOT_ASSIGNED;
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
	m_ID.clear();
	m_rootGameObjectID.clear();
	m_vertexCount = 0;
	m_indexCount = 0;
	m_triangleCount = 0;
}

//= IO =========================================================================
void Mesh::Serialize()
{
	Serializer::WriteSTR(m_ID);
	Serializer::WriteSTR(m_rootGameObjectID);
	Serializer::WriteSTR(m_name);
	Serializer::WriteSTR(m_directory);
	Serializer::WriteSTR(m_filePath);
	Serializer::WriteInt(m_vertexCount);
	Serializer::WriteInt(m_indexCount);
	Serializer::WriteInt(m_triangleCount);

	for (auto i = 0; i < m_vertexCount; i++)
		SaveVertex(m_vertices[i]);

	for (auto i = 0; i < m_indexCount; i++)
		Serializer::WriteInt(m_indices[i]);

	Serializer::WriteVector3(m_min);
	Serializer::WriteVector3(m_max);
	Serializer::WriteVector3(m_center);
	Serializer::WriteVector3(m_boundingBox);
}

void Mesh::Deserialize()
{
	m_ID = Serializer::ReadSTR();
	m_rootGameObjectID = Serializer::ReadSTR();
	m_name = Serializer::ReadSTR();
	m_directory = Serializer::ReadSTR();
	m_filePath = Serializer::ReadSTR();
	m_vertexCount = Serializer::ReadInt();
	m_indexCount = Serializer::ReadInt();
	m_triangleCount = Serializer::ReadInt();

	for (auto i = 0; i < m_vertexCount; i++)
	{
		m_vertices.push_back(VertexPositionTextureNormalTangent());
		LoadVertex(m_vertices.back());
	}

	for (auto i = 0; i < m_indexCount; i++)
		m_indices.push_back(Serializer::ReadInt());

	m_min = Serializer::ReadVector3();
	m_max = Serializer::ReadVector3();
	m_center = Serializer::ReadVector3();
	m_boundingBox = Serializer::ReadVector3();
}

bool Mesh::SaveMetadata()
{
	return true;
}

void Mesh::SaveToDirectory(const string& directory, bool overwrite)
{
	// I used to use GetName() to save the mesh but when mesh duplicates are genarated
	// upon loading a model, they also get different IDs, the file will not be overwritten
	// of course and thus when loading the meshes, only one will be loaded properly.
	m_filePath = directory + GetID() + MESH_EXTENSION;

	if (FileSystem::FileExists(m_filePath) && !overwrite)
		return;

	Serializer::StartWriting(m_filePath);
	Serialize();
	Serializer::StopWriting();
}

bool Mesh::LoadFromFile(const string& filePath)
{
	// Make sure the file exists
	if (!FileSystem::FileExists(filePath))
		return false;

	// Make sure it's actually a mesh file
	if (FileSystem::GetExtensionFromPath(filePath) != MESH_EXTENSION)
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

	if (m_directory != DATA_NOT_ASSIGNED)
		SaveToDirectory(m_directory, true);

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

void Mesh::SetScale(float scale)
{
	SetScale(this, scale);
	Update();
}
//==============================================================================

//= IO =========================================================================
void Mesh::SaveVertex(const VertexPositionTextureNormalTangent& vertex)
{
	Serializer::WriteFloat(vertex.position.x);
	Serializer::WriteFloat(vertex.position.y);
	Serializer::WriteFloat(vertex.position.z);

	Serializer::WriteFloat(vertex.uv.x);
	Serializer::WriteFloat(vertex.uv.y);

	Serializer::WriteFloat(vertex.normal.x);
	Serializer::WriteFloat(vertex.normal.y);
	Serializer::WriteFloat(vertex.normal.z);

	Serializer::WriteFloat(vertex.tangent.x);
	Serializer::WriteFloat(vertex.tangent.y);
	Serializer::WriteFloat(vertex.tangent.z);
}

void Mesh::LoadVertex(VertexPositionTextureNormalTangent& vertex)
{
	vertex.position.x = Serializer::ReadFloat();
	vertex.position.y = Serializer::ReadFloat();
	vertex.position.z = Serializer::ReadFloat();

	vertex.uv.x = Serializer::ReadFloat();
	vertex.uv.y = Serializer::ReadFloat();

	vertex.normal.x = Serializer::ReadFloat();
	vertex.normal.y = Serializer::ReadFloat();
	vertex.normal.z = Serializer::ReadFloat();

	vertex.tangent.x = Serializer::ReadFloat();
	vertex.tangent.y = Serializer::ReadFloat();
	vertex.tangent.z = Serializer::ReadFloat();
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