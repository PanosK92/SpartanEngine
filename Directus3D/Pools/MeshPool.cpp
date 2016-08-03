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

//= INCLUDES =======================
#include "MeshPool.h"
#include "../Core/GUIDGenerator.h"
#include "../IO/Serializer.h"
#include "../Core/GameObject.h"
#include "../Components/MeshFilter.h"
#include "../Components/Transform.h"
//==================================

//= NAMESPACES ================
using namespace std;
using namespace Directus::Math;
//=============================

MeshPool::MeshPool()
{
}

MeshPool::~MeshPool()
{
	DeleteAll();
}

/*------------------------------------------------------------------------------
									[MISC]
------------------------------------------------------------------------------*/
void MeshPool::DeleteAll()
{
	for (int i = 0; i < m_meshPool.size(); i++)
		delete m_meshPool[i];

	m_meshPool.clear();
	m_meshPool.shrink_to_fit();
}

Mesh* MeshPool::AddMesh(string name, string rootGameObjectID, string gameObjectID, vector<VertexPositionTextureNormalTangent> vertices, vector<unsigned int> indices)
{
	// construct mesh
	Mesh* mesh = new Mesh();
	mesh->name = name;
	mesh->rootGameObjectID = rootGameObjectID;
	mesh->ID = GENERATE_GUID;
	mesh->gameObjectID = gameObjectID;
	mesh->vertices = vertices;
	mesh->indices = indices;
	mesh->vertexCount = vertices.size();
	mesh->indexCount = indices.size();
	mesh->faceCount = vertices.size() / 3;

	m_meshPool.push_back(mesh);

	// return the mesh
	return m_meshPool.back();
}

Mesh* MeshPool::GetMesh(string ID)
{
	for (int i = 0; i < m_meshPool.size(); i++)
	{
		if (m_meshPool[i]->ID == ID)
			return m_meshPool[i];
	}

	return nullptr;
}

// Returns the meshes tha belong to the same model
vector<Mesh*> MeshPool::GetModelMeshesByModelName(string rootGameObjectID)
{
	vector<Mesh*> modelMeshes;
	for (unsigned int i = 0; i < m_meshPool.size(); i++)
	{
		if (m_meshPool[i]->rootGameObjectID == rootGameObjectID)
			modelMeshes.push_back(m_meshPool[i]);
	}

	return modelMeshes;
}

/*------------------------------------------------------------------------------
							[MESH PROCESSING]
------------------------------------------------------------------------------*/
// Returns a value that can be used (by multiplying against the original scale)
// to normalize the scale of a transform
float MeshPool::GetNormalizedModelScaleByRootGameObjectID(string rootGameObjectID)
{
	// get all the meshes related to this model
	vector<Mesh*> modelMeshes = GetModelMeshesByModelName(rootGameObjectID);

	// find the mesh with the largest bounding box
	Mesh* largestBoundingBoxMesh = GetLargestBoundingBox(modelMeshes);

	// calculate the scale
	Vector3 boundingBox = GetMeshExtent(largestBoundingBoxMesh);
	float scaleOffset = Vector3::Length(boundingBox, Vector3::Zero);

	return 1.0f / scaleOffset;
}

void MeshPool::SetMeshScale(Mesh* meshData, float scale)
{
	for (int j = 0; j < meshData->vertexCount; j++)
		meshData->vertices[j].position *= scale;
}

void MeshPool::SetModelScale(string rootGameObjectID, float scale)
{
	// get all the meshes related to this model
	vector<Mesh*> modelMeshes = GetModelMeshesByModelName(rootGameObjectID);

	for (int i = 0; i < modelMeshes.size(); i++)
		SetMeshScale(modelMeshes[i], scale);
}

void MeshPool::NormalizeModelScale(GameObject* rootGameObject)
{
	float normalizedScale = GetNormalizedModelScaleByRootGameObjectID(rootGameObject->GetID());
	SetModelScale(rootGameObject->GetID(), normalizedScale);

	// Also, refresh it's mesh in the gameobject now that the scale has changed
	// this will cause ot's mesh to recalculate it's bounding box and so on.
	vector<Transform*> descendants = rootGameObject->GetTransform()->GetDescendants();
	for (auto i = 0; i < descendants.size(); i++)
	{
		MeshFilter* mesh = descendants[i]->g_gameObject->GetComponent<MeshFilter>();
		if (mesh) mesh->Refresh();
	}
}

// Returns the largest bounding box in an array of meshes
Mesh* MeshPool::GetLargestBoundingBox(const vector<Mesh*>& meshes)
{
	if (meshes.empty())
		return nullptr;

	Vector3 largestBoundingBox = Vector3::Zero;
	Mesh* largestBoundingBoxMesh = meshes[0];

	for (unsigned int i = 0; i < meshes.size(); i++)
	{
		Vector3 boundingBox = GetMeshExtent(meshes[i]);
		if (boundingBox.Volume() > largestBoundingBox.Volume())
		{
			largestBoundingBox = boundingBox;
			largestBoundingBoxMesh = meshes[i];
		}
	}

	return largestBoundingBoxMesh;
}

// Returns the bounding box of a mesh
Vector3 MeshPool::GetMeshExtent(Mesh* mesh)
{
	if (!mesh)
		return Vector3::Zero;

	Vector3 min, max;

	// find min, max
	GetMinMax(mesh, min, max);

	// return the bounding box
	return GetMeshExtent(min, max);
}

// Returns the bounding box of a mesh based on it's minimum and maximum points
Vector3 MeshPool::GetMeshExtent(const Vector3& min, const Vector3& max)
{
	return (max - min) * 0.5f;
}

// Returns the center of the mesh based on it's minimum and maximum points
Vector3 MeshPool::GetMeshCenter(const Vector3& min, const Vector3& max)
{
	return (min + max) * 0.5f;
}

// Returns the minimum and maximum point in an array of meshes
void MeshPool::GetMinMax(Mesh* meshData, Vector3& min, Vector3& max)
{
	if (!meshData)
		return;

	min = Vector3::Infinity;
	max = Vector3::InfinityNeg;

	vector<VertexPositionTextureNormalTangent> vertices = meshData->vertices;
	for (unsigned int i = 0; i < meshData->vertexCount; i++)
	{
		float x = vertices[i].position.x;
		float y = vertices[i].position.y;
		float z = vertices[i].position.z;

		if (x > max.x) max.x = x;
		if (y > max.y) max.y = y;
		if (z > max.z) max.z = z;

		if (x < min.x) min.x = x;
		if (y < min.y) min.y = y;
		if (z < min.z) min.z = z;
	}

	vertices.clear();
	vertices.shrink_to_fit();
}

/*------------------------------------------------------------------------------
									[I/O]
------------------------------------------------------------------------------*/
void MeshPool::Serialize()
{
	int meshDataCount = m_meshPool.size();

	Serializer::SaveInt(meshDataCount); // 1st - meshDataCount
	for (int i = 0; i < meshDataCount; i++)
	{
		// save simple data
		Serializer::SaveSTR(m_meshPool[i]->rootGameObjectID); // 2nd - root GameObject id
		Serializer::SaveSTR(m_meshPool[i]->ID); // 3rd - ID
		Serializer::SaveSTR(m_meshPool[i]->gameObjectID); // 4th - GameObjectID
		Serializer::SaveInt(m_meshPool[i]->vertexCount); // 5th - vertexCount
		Serializer::SaveInt(m_meshPool[i]->indexCount); // 6th - indexCount
		Serializer::SaveInt(m_meshPool[i]->faceCount); // 7th - indexCount

		// save vertices
		int vertexCount = m_meshPool[i]->vertexCount;
		for (int j = 0; j < vertexCount; j++)
			SaveVertex(m_meshPool[i]->vertices[j]); // 7th - vertices

		// save indices
		int indexCount = m_meshPool[i]->indexCount;
		for (int j = 0; j < indexCount; j++)
			Serializer::SaveInt(m_meshPool[i]->indices[j]); // 8th - indices
	}
}

void MeshPool::Deserialize()
{
	DeleteAll();

	int meshDataCount = Serializer::LoadInt(); // 1st - meshDataCount
	for (int i = 0; i < meshDataCount; i++)
	{
		Mesh* mesh = new Mesh();

		// load simple data
		mesh->rootGameObjectID = Serializer::LoadSTR(); // 2nd - root GameObject id
		mesh->ID = Serializer::LoadSTR(); // 3rd - ID
		mesh->gameObjectID = Serializer::LoadSTR(); // 4th - GameObjectID
		mesh->vertexCount = Serializer::LoadInt(); // 5th - vertexCount
		mesh->indexCount = Serializer::LoadInt(); // 6th - indexCount
		mesh->faceCount = Serializer::LoadInt(); // 7th - indexCount

		// load vertices
		for (unsigned int j = 0; j < mesh->vertexCount; j++)
			mesh->vertices.push_back(LoadVertex()); // 7th - vertices

		// load indices
		for (unsigned int j = 0; j < mesh->indexCount; j++)
			mesh->indices.push_back(Serializer::LoadInt()); // 8th - indices

		m_meshPool.push_back(mesh);
	}
}

/*------------------------------------------------------------------------------
							[HELPER FUNCTIONS]
------------------------------------------------------------------------------*/
void MeshPool::SaveVertex(const VertexPositionTextureNormalTangent& vertex)
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

VertexPositionTextureNormalTangent MeshPool::LoadVertex()
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
