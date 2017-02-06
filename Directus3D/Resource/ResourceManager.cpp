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

//= INCLUDES ==================
#include "ResourceManager.h"
#include "../Core/GameObject.h"
//=============================

//= NAMESPACES ====================
using namespace std;
using namespace Directus::Math;
//=================================

namespace Directus
{
	ResourceManager::ResourceManager(Context* context) : Subsystem(context)
	{
		m_resourceCache = make_unique<ResourceCache>();
	}

	/*------------------------------------------------------------------------------
	[MESH PROCESSING]
	------------------------------------------------------------------------------*/
	// Returns the meshes tha belong to the same model
	vector<weak_ptr<Mesh>> ResourceManager::GetModelMeshesByModelName(const string& rootGameObjectID)
	{
		vector<weak_ptr<Mesh>> modelMeshes;

		auto meshes = GetAllByType<Mesh>();
		for (const auto& mesh : meshes)
			if (mesh.lock()->GetRootGameObjectID() == rootGameObjectID)
				modelMeshes.push_back(mesh);

		return modelMeshes;
	}

	// Returns a value that can be used (by multiplying against the original scale)
	// to normalize the scale of a transform
	float ResourceManager::GetNormalizedModelScaleByRootGameObjectID(const string& rootGameObjectID)
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

	void ResourceManager::SetModelScale(const string& rootGameObjectID, float scale)
	{
		// get all the meshes related to this model and scale them
		for (const auto& modelMesh : GetModelMeshesByModelName(rootGameObjectID))
			modelMesh.lock()->SetScale(scale);
	}

	void ResourceManager::NormalizeModelScale(GameObject* rootGameObject)
	{
		if (!rootGameObject)
			return;

		float normalizedScale = GetNormalizedModelScaleByRootGameObjectID(rootGameObject->GetID());
		SetModelScale(rootGameObject->GetID(), normalizedScale);
	}

	// Returns the largest bounding box in an vector of meshes
	weak_ptr<Mesh> ResourceManager::GetLargestBoundingBox(const vector<weak_ptr<Mesh>>& meshes)
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
}