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

//= INCLUDES ==================
#include "ResourceCache.h"
#include "../Core/GameObject.h"
//=============================

//= NAMESPACES ====================
using namespace std;
using namespace Directus::Math;
using namespace Directus::Resource;
//=================================

//= TEMPORARY ==================================
#define MESH_DEFAULT_CUBE_ID "DEFAULT_MESH_CUBE"
#define MESH_DEFAULT_QUAD_ID "DEFAULT_MESH_QUAD"
//==============================================

ResourceCache::ResourceCache(Context* context) : Subsystem(context)
{
	GenerateDefaultMeshes();
}

ResourceCache::~ResourceCache()
{
	Clear();
}

void ResourceCache::Clear()
{
	m_resources.clear();
	m_resources.shrink_to_fit();
}

void ResourceCache::LoadResources(const vector<string>& filePaths)
{
	for (const auto& filePath : filePaths)
		LoadResource<Texture>(filePath);
}

void ResourceCache::SaveResourceMetadata()
{
	for (auto const& resource : m_resources)
		resource->SaveMetadata();
}

//= TEMPORARY ===============================================================
weak_ptr<Mesh> ResourceCache::GetDefaultCube()
{
	return m_defaultCube;
}

weak_ptr<Mesh> ResourceCache::GetDefaultQuad()
{
	return m_defaultQuad;
}

void ResourceCache::GenerateDefaultMeshes()
{
	vector<VertexPositionTextureNormalTangent> vertices;
	vector<unsigned int> indices;

	CreateCube(vertices, indices);

	// construct the mesh
	m_defaultCube = make_shared<Mesh>();
	m_defaultCube->SetID(MESH_DEFAULT_CUBE_ID);
	m_defaultCube->SetName("Cube");
	m_defaultCube->SetVertices(vertices);
	m_defaultCube->SetIndices(indices);
	m_defaultCube->Update();

	vertices.clear();
	vertices.shrink_to_fit();

	CreateQuad(vertices, indices);

	m_defaultQuad = make_shared<Mesh>();
	m_defaultQuad->SetID(MESH_DEFAULT_QUAD_ID);
	m_defaultQuad->SetName("Quad");
	m_defaultQuad->SetVertices(vertices);
	m_defaultQuad->SetIndices(indices);
	m_defaultQuad->Update();

	vertices.clear();
	vertices.shrink_to_fit();
}

void ResourceCache::CreateCube(vector<VertexPositionTextureNormalTangent>& vertices, vector<unsigned int>& indices)
{
	// front
	vertices.push_back({ Vector3(-0.5f, -0.5f, -0.5f), Vector2(0, 1), Vector3(0, 0, -1), Vector3(0, 1, 0) }); // 0
	vertices.push_back({ Vector3(-0.5f, 0.5f, -0.5f), Vector2(0, 0), Vector3(0, 0, -1), Vector3(0, 1, 0) }); // 1
	vertices.push_back({ Vector3(0.5f, -0.5f, -0.5f), Vector2(1, 1), Vector3(0, 0, -1), Vector3(0, 1, 0) }); // 2
	vertices.push_back({ Vector3(0.5f, 0.5f, -0.5f), Vector2(1, 0), Vector3(0, 0, -1), Vector3(0, 1, 0) }); // 3

																											// bottom
	vertices.push_back({ Vector3(-0.5f, -0.5f, 0.5f), Vector2(0, 1), Vector3(0, -1, 0), Vector3(1, 0, 0) }); // 4
	vertices.push_back({ Vector3(-0.5f, -0.5f, -0.5f), Vector2(0, 0), Vector3(0, -1, 0), Vector3(1, 0, 0) }); // 5
	vertices.push_back({ Vector3(0.5f, -0.5f, 0.5f), Vector2(1, 1), Vector3(0, -1, 0), Vector3(1, 0, 0) }); // 6
	vertices.push_back({ Vector3(0.5f, -0.5f, -0.5f), Vector2(1, 0), Vector3(0, -1, 0), Vector3(1, 0, 0) }); // 7

																											 // back
	vertices.push_back({ Vector3(-0.5f, -0.5f, 0.5f), Vector2(1, 1), Vector3(0, 0, 1), Vector3(0, 1, 0) }); // 8
	vertices.push_back({ Vector3(-0.5f, 0.5f, 0.5f), Vector2(1, 0), Vector3(0, 0, 1), Vector3(0, 1, 0) }); // 9
	vertices.push_back({ Vector3(0.5f, -0.5f, 0.5f), Vector2(0, 1), Vector3(0, 0, 1), Vector3(0, 1, 0) }); // 10
	vertices.push_back({ Vector3(0.5f, 0.5f, 0.5f), Vector2(0, 0), Vector3(0, 0, 1), Vector3(0, 1, 0) }); // 11

																										  // top
	vertices.push_back({ Vector3(-0.5f, 0.5f, 0.5f), Vector2(0, 0), Vector3(0, 1, 0), Vector3(1, 0, 0) }); // 12
	vertices.push_back({ Vector3(-0.5f, 0.5f, -0.5f), Vector2(0, 1), Vector3(0, 1, 0), Vector3(1, 0, 0) }); // 13
	vertices.push_back({ Vector3(0.5f, 0.5f, 0.5f), Vector2(1, 0), Vector3(0, 1, 0), Vector3(1, 0, 0) }); // 14
	vertices.push_back({ Vector3(0.5f, 0.5f, -0.5f), Vector2(1, 1), Vector3(0, 1, 0), Vector3(1, 0, 0) }); // 15

																										   // left
	vertices.push_back({ Vector3(-0.5f, -0.5f, 0.5f), Vector2(0, 1), Vector3(-1, 0, 0), Vector3(0, 1, 0) }); // 16
	vertices.push_back({ Vector3(-0.5f, 0.5f, 0.5f), Vector2(0, 0), Vector3(-1, 0, 0), Vector3(0, 1, 0) }); // 17
	vertices.push_back({ Vector3(-0.5f, -0.5f, -0.5f), Vector2(1, 1), Vector3(-1, 0, 0), Vector3(0, 1, 0) }); // 18
	vertices.push_back({ Vector3(-0.5f, 0.5f, -0.5f), Vector2(1, 0), Vector3(-1, 0, 0), Vector3(0, 1, 0) }); // 19

																											 // right
	vertices.push_back({ Vector3(0.5f, -0.5f, 0.5f), Vector2(1, 1), Vector3(1, 0, 0), Vector3(0, 1, 0) }); // 20
	vertices.push_back({ Vector3(0.5f, 0.5f, 0.5f), Vector2(1, 0), Vector3(1, 0, 0), Vector3(0, 1, 0) }); // 21
	vertices.push_back({ Vector3(0.5f, -0.5f, -0.5f), Vector2(0, 1), Vector3(1, 0, 0), Vector3(0, 1, 0) }); // 22
	vertices.push_back({ Vector3(0.5f, 0.5f, -0.5f), Vector2(0, 0), Vector3(1, 0, 0), Vector3(0, 1, 0) }); // 23

																										   // front
	indices.push_back(0);
	indices.push_back(1);
	indices.push_back(2);
	indices.push_back(2);
	indices.push_back(1);
	indices.push_back(3);

	// bottom
	indices.push_back(4);
	indices.push_back(5);
	indices.push_back(6);
	indices.push_back(6);
	indices.push_back(5);
	indices.push_back(7);

	// back
	indices.push_back(10);
	indices.push_back(9);
	indices.push_back(8);
	indices.push_back(11);
	indices.push_back(9);
	indices.push_back(10);

	// top
	indices.push_back(14);
	indices.push_back(13);
	indices.push_back(12);
	indices.push_back(15);
	indices.push_back(13);
	indices.push_back(14);

	// left
	indices.push_back(16);
	indices.push_back(17);
	indices.push_back(18);
	indices.push_back(18);
	indices.push_back(17);
	indices.push_back(19);

	// left
	indices.push_back(22);
	indices.push_back(21);
	indices.push_back(20);
	indices.push_back(23);
	indices.push_back(21);
	indices.push_back(22);
}

void ResourceCache::CreateQuad(vector<VertexPositionTextureNormalTangent>& vertices, vector<unsigned int>& indices)
{
	vertices.push_back({ Vector3(-0.5f, 0.0f, 0.5f),Vector2(0, 0), Vector3(0, 1, 0), Vector3(1, 0, 0) }); // 0 top-left
	vertices.push_back({ Vector3(0.5f, 0.0f, 0.5f), Vector2(1, 0), Vector3(0, 1, 0), Vector3(1, 0, 0) }); // 1 top-right
	vertices.push_back({ Vector3(-0.5f, 0.0f, -0.5f), Vector2(0, 1), Vector3(0, 1, 0), Vector3(1, 0, 0) }); // 2 bottom-left
	vertices.push_back({ Vector3(0.5f, 0.0f, -0.5f),Vector2(1, 1), Vector3(0, 1, 0), Vector3(1, 0, 0) }); // 3 bottom-right

	indices.push_back(3);
	indices.push_back(2);
	indices.push_back(0);
	indices.push_back(3);
	indices.push_back(0);
	indices.push_back(1);
}

/*------------------------------------------------------------------------------
[MESH PROCESSING]
------------------------------------------------------------------------------*/
// Returns the meshes tha belong to the same model
vector<weak_ptr<Mesh>> ResourceCache::GetModelMeshesByModelName(const string& rootGameObjectID)
{
	vector<weak_ptr<Mesh>> modelMeshes;

	auto meshes = GetResourcesOfType<Mesh>();
	for (const auto& mesh : meshes)
		if (mesh.lock()->GetRootGameObjectID() == rootGameObjectID)
			modelMeshes.push_back(mesh);

	return modelMeshes;
}

// Returns a value that can be used (by multiplying against the original scale)
// to normalize the scale of a transform
float ResourceCache::GetNormalizedModelScaleByRootGameObjectID(const string& rootGameObjectID)
{
	// get all the meshes related to this model
	vector<weak_ptr<Mesh>> modelMeshes = GetModelMeshesByModelName(rootGameObjectID);

	// find the mesh with the largest bounding box
	weak_ptr<Mesh> largestBoundingBoxMesh = GetLargestBoundingBox(modelMeshes);

	if (largestBoundingBoxMesh.expired())
		return 1.0f;

	// calculate the scale
	Vector3 boundingBox = largestBoundingBoxMesh.lock()->GetBoundingBox();
	float scaleOffset = boundingBox.Length();

	return 1.0f / scaleOffset;
}

void ResourceCache::SetModelScale(const string& rootGameObjectID, float scale)
{
	// get all the meshes related to this model and scale them
	for (const auto& modelMesh : GetModelMeshesByModelName(rootGameObjectID))
		modelMesh.lock()->SetScale(scale);
}

void ResourceCache::NormalizeModelScale(GameObject* rootGameObject)
{
	if (!rootGameObject)
		return;

	float normalizedScale = GetNormalizedModelScaleByRootGameObjectID(rootGameObject->GetID());
	SetModelScale(rootGameObject->GetID(), normalizedScale);
}

// Returns the largest bounding box in an vector of meshes
weak_ptr<Mesh> ResourceCache::GetLargestBoundingBox(const vector<weak_ptr<Mesh>>& meshes)
{
	if (meshes.empty())
		return weak_ptr<Mesh>();

	Vector3 largestBoundingBox = Vector3::Zero;
	weak_ptr<Mesh> largestBoundingBoxMesh = meshes.front();

	for (auto mesh : meshes)
	{
		if (mesh.expired())
			continue;

		Vector3 boundingBox = mesh.lock()->GetBoundingBox();
		if (boundingBox.Volume() > largestBoundingBox.Volume())
		{
			largestBoundingBox = boundingBox;
			largestBoundingBoxMesh = mesh;
		}
	}

	return largestBoundingBoxMesh;
}