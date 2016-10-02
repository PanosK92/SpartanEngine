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
	Clear();
}

/*------------------------------------------------------------------------------
									[MISC]
------------------------------------------------------------------------------*/
void MeshPool::Clear()
{
	for (auto i = 0; i < m_meshes.size(); i++)
		delete m_meshes[i];

	m_meshes.clear();
	m_meshes.shrink_to_fit();
}

// Adds a mesh to the pool directly from memory
Mesh* MeshPool::Add(const string& name, const string& rootGameObjectID, const string& gameObjectID, vector<VertexPositionTextureNormalTangent> vertices, vector<unsigned int> indices)
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
	m_meshes.push_back(mesh);

	// return it
	return m_meshes.back();
}

// Adds multiple meshes to the pool by reading them from files
void MeshPool::Add(vector<string> filePaths)
{
	for (auto i = 0; i < filePaths.size(); i++)
	{
		Mesh* mesh = new Mesh();

		if (mesh->LoadFromFile(filePaths[i]))
			m_meshes.push_back(mesh);
		else
			delete mesh;
	}
}

Mesh* MeshPool::GetMesh(const string& ID)
{
	for (auto i = 0; i < m_meshes.size(); i++)
	{
		if (m_meshes[i]->GetID() == ID)
			return m_meshes[i];
	}

	return nullptr;
}

void MeshPool::GetAllMeshFilePaths(vector<string> paths)
{
	for (auto i = 0; i < m_meshes.size(); i++)
		paths.push_back(m_meshes[i]->GetFilePath());
}

// Returns the meshes tha belong to the same model
vector<Mesh*> MeshPool::GetModelMeshesByModelName(const string& rootGameObjectID)
{
	vector<Mesh*> modelMeshes;
	for (auto i = 0; i < m_meshes.size(); i++)
	{
		if (m_meshes[i]->GetRootGameObjectID() == rootGameObjectID)
			modelMeshes.push_back(m_meshes[i]);
	}

	return modelMeshes;
}

/*------------------------------------------------------------------------------
							[MESH PROCESSING]
------------------------------------------------------------------------------*/
// Returns a value that can be used (by multiplying against the original scale)
// to normalize the scale of a transform
float MeshPool::GetNormalizedModelScaleByRootGameObjectID(const string& rootGameObjectID)
{
	// get all the meshes related to this model
	vector<Mesh*> modelMeshes = GetModelMeshesByModelName(rootGameObjectID);

	// find the mesh with the largest bounding box
	Mesh* largestBoundingBoxMesh = GetLargestBoundingBox(modelMeshes);

	if (!largestBoundingBoxMesh)
		return 1.0f;

	// calculate the scale
	Vector3 boundingBox = largestBoundingBoxMesh->GetBoundingBox();
	float scaleOffset = boundingBox.Length();

	return 1.0f / scaleOffset;
}

void MeshPool::SetModelScale(const string& rootGameObjectID, float scale)
{
	// get all the meshes related to this model
	vector<Mesh*> modelMeshes = GetModelMeshesByModelName(rootGameObjectID);

	for (auto i = 0; i < modelMeshes.size(); i++)
		modelMeshes[i]->Scale(scale);
}

void MeshPool::NormalizeModelScale(GameObject* rootGameObject)
{
	if (!rootGameObject)
		return;

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

	for (auto i = 0; i < meshes.size(); i++)
	{
		if (!meshes[i])
			continue;

		Vector3 boundingBox = meshes[i]->GetBoundingBox();
		if (boundingBox.Volume() > largestBoundingBox.Volume())
		{
			largestBoundingBox = boundingBox;
			largestBoundingBoxMesh = meshes[i];
		}
	}

	return largestBoundingBoxMesh;
}