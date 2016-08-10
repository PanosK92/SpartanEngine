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
#include "../IO/Log.h"
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
	// construct the mesh
	Mesh* mesh = new Mesh();
	mesh->SetName(name);
	mesh->SetGameObjectID(gameObjectID);
	mesh->SetRootGameObjectID(rootGameObjectID);
	mesh->SetVertices(vertices);
	mesh->SetIndices(indices);
	mesh->Update();

	// add it to the pool
	m_meshPool.push_back(mesh);

	// return it
	return m_meshPool.back();
}

Mesh* MeshPool::GetMesh(string ID)
{
	for (int i = 0; i < m_meshPool.size(); i++)
	{
		if (m_meshPool[i]->GetID() == ID)
			return m_meshPool[i];
	}

	return nullptr;
}

// Returns the meshes tha belong to the same model
const vector<Mesh*>& MeshPool::GetModelMeshesByModelName(string rootGameObjectID)
{
	vector<Mesh*> modelMeshes;
	for (unsigned int i = 0; i < m_meshPool.size(); i++)
	{
		if (m_meshPool[i]->GetRootGameObjectID() == rootGameObjectID)
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
	Vector3 boundingBox = largestBoundingBoxMesh->GetBoundingBox();
	float scaleOffset = boundingBox.Length();

	return 1.0f / scaleOffset;
}

void MeshPool::SetModelScale(string rootGameObjectID, float scale)
{
	// get all the meshes related to this model
	vector<Mesh*> modelMeshes = GetModelMeshesByModelName(rootGameObjectID);

	for (int i = 0; i < modelMeshes.size(); i++)
		modelMeshes[i]->Scale(scale);
}

void MeshPool::NormalizeModelScale(GameObject* rootGameObject)
{
	float normalizedScale = GetNormalizedModelScaleByRootGameObjectID(rootGameObject->GetID());
	SetModelScale(rootGameObject->GetID(), normalizedScale);
}

// Returns the largest bounding box in an vector of meshes
Mesh* MeshPool::GetLargestBoundingBox(const vector<Mesh*>& meshes)
{
	if (meshes.empty())
		return nullptr;

	Vector3 largestBoundingBox = Vector3::Zero;
	Mesh* largestBoundingBoxMesh = meshes[0];

	for (unsigned int i = 0; i < meshes.size(); i++)
	{
		Vector3 boundingBox = meshes[i]->GetBoundingBox();
		if (boundingBox.Volume() > largestBoundingBox.Volume())
		{
			largestBoundingBox = boundingBox;
			largestBoundingBoxMesh = meshes[i];
		}
	}

	return largestBoundingBoxMesh;
}

/*------------------------------------------------------------------------------
									[I/O]
------------------------------------------------------------------------------*/
void MeshPool::Serialize()
{
	int meshCount = int(m_meshPool.size());
	Serializer::SaveInt(meshCount);

	for (int i = 0; i < meshCount; i++)
		m_meshPool[i]->Serialize();
}

void MeshPool::Deserialize()
{
	DeleteAll();

	int meshDataCount = Serializer::LoadInt();
	for (int i = 0; i < meshDataCount; i++)
	{
		Mesh* mesh = new Mesh();
		mesh->Deserialize();
		m_meshPool.push_back(mesh);
	}
}