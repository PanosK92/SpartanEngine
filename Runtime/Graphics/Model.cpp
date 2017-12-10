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

//= INCLUDES ===========================
#include "Model.h"
#include "Mesh.h"
#include "../Core/GameObject.h"
#include "../Resource/ResourceManager.h"
#include "../Components/MeshFilter.h"
#include "../Components/Transform.h"
#include "../Graphics/Vertex.h"
#include "../Graphics/Material.h"
#include "../Graphics/Animation.h"
#include "../IO/StreamIO.h"
#include "../Core/Stopwatch.h"
//======================================

//= NAMESPACES ================
using namespace std;
using namespace Directus::Math;
//=============================

namespace Directus
{
	Model::Model(Context* context)
	{
		m_context = context;

		//= RESOURCE INTERFACE ============
		InitializeResource(Resource_Model);
		//=================================

		m_normalizedScale = 1.0f;
		m_isAnimated = false;

		if (!m_context)
			return;

		m_resourceManager = m_context->GetSubsystem<ResourceManager>();
		m_memoryUsageKB = 0;
	}

	Model::~Model()
	{
		m_meshes.clear();
		m_meshes.shrink_to_fit();

		m_materials.clear();
		m_materials.shrink_to_fit();

		m_animations.clear();
		m_animations.shrink_to_fit();
	}

	//= RESOURCE INTERFACE ====================================================================
	bool Model::LoadFromFile(const string& filePath)
	{
		Stopwatch timer;
		string modelFilePath = filePath;

		// Check if this is a directory instead of a model file path
		if (FileSystem::IsDirectory(filePath))
		{
			// If it is, try to find a model file in it
			vector<string> modelFilePaths = FileSystem::GetSupportedModelFilesInDirectory(filePath);
			if (!modelFilePaths.empty())
			{
				modelFilePath = modelFilePaths.front();
			}
			else // abort
			{
				LOG_WARNING("Model: Failed to load model. Unable to find supported file in \"" + FileSystem::GetDirectoryFromFilePath(filePath) + "\".");
				return false;
			}
		}

		bool engineFormat = FileSystem::GetExtensionFromFilePath(modelFilePath) == MODEL_EXTENSION;
		bool success = engineFormat ? LoadFromEngineFormat(modelFilePath) : LoadFromForeignFormat(modelFilePath);

		// Compute memory usage
		m_memoryUsageKB = 0;
		for (const auto& mesh : m_meshes)
		{
			m_memoryUsageKB += mesh->GetMemoryUsageKB();
		}

		LOG_INFO("Model: Loading \"" + FileSystem::GetFileNameFromFilePath(filePath) + "\" took " + to_string((int)timer.GetElapsedTime()) + " ms");

		return success;
	}

	bool Model::SaveToFile(const string& filePath)
	{
		unique_ptr<StreamIO> file = make_unique<StreamIO>(filePath, Mode_Write);
		if (!file->IsCreated())
			return false;

		file->Write(GetResourceID());
		file->Write(GetResourceName());
		file->Write(GetResourceFilePath());
		file->Write(m_normalizedScale);
		file->Write((int)m_meshes.size());

		for (const auto& mesh : m_meshes)
		{
			mesh->Serialize(file.get());
		}

		return true;
	}
	//============================================================================================

	weak_ptr<Mesh> Model::AddMeshAsNewResource(unsigned int gameObjID, const string& name, vector<VertexPosTexTBN> vertices, vector<unsigned int> indices)
	{
		// Create a mesh
		auto mesh = make_shared<Mesh>();
		mesh->SetModelID(GetResourceID());
		mesh->SetGameObjectID(gameObjID);
		mesh->SetName(name);
		mesh->SetVertices(vertices);
		mesh->SetIndices(indices);

		AddMeshAsNewResource(mesh);

		return mesh;
	}

	void Model::AddMeshAsNewResource(shared_ptr<Mesh> mesh)
	{
		if (!mesh)
			return;

		// Updates mesh bounding box, center, min, max etc.
		mesh->Update();

		// Calculate the bounding box of the model as well
		ComputeDimensions();

		// Save it
		m_meshes.push_back(mesh);
	}

	weak_ptr<Material> Model::AddMaterialAsNewResource(shared_ptr<Material> material)
	{
		if (!material)
			return weak_ptr<Material>();

		// Add it to our resources
		weak_ptr<Material> weakMat = m_context->GetSubsystem<ResourceManager>()->Add<Material>(material);

		// Save the material in the model directory
		material._Get()->SaveToFile(m_modelDirectoryMaterials + material->GetResourceName());

		// Keep a reference to it
		m_materials.push_back(material);

		// Return it
		return weakMat;
	}

	weak_ptr<Animation> Model::AddAnimationAsNewResource(shared_ptr<Animation> animation)
	{
		if (!animation)
			return weak_ptr<Animation>();

		// Add it to our resources
		auto weakAnim = m_context->GetSubsystem<ResourceManager>()->Add<Animation>(animation);

		// Keep a reference to it
		m_animations.push_back(weakAnim);

		m_isAnimated = true;

		// Return it
		return weakAnim;
	}

	weak_ptr<Mesh> Model::GetMeshByID(unsigned int id)
	{
		for (const auto& mesh : m_meshes)
		{
			if (mesh->GetID() == id)
			{
				return mesh;
			}
		}

		return weak_ptr<Mesh>();
	}

	weak_ptr<Mesh> Model::GetMeshByName(const string& name)
	{
		for (const auto& mesh : m_meshes)
		{
			if (mesh->GetName() == name)
			{
				return mesh;
			}
		}

		return weak_ptr<Mesh>();
	}

	float Model::GetBoundingSphereRadius()
	{
		Vector3 extent = m_boundingBox.GetExtents().Absolute();
		return Max(Max(extent.x, extent.y), extent.z);
	}

	bool Model::LoadFromEngineFormat(const string& filePath)
	{
		// Deserialize
		unique_ptr<StreamIO> file = make_unique<StreamIO>(filePath, Mode_Read);
		if (!file->IsCreated())
			return false;

		int meshCount = 0;

		file->Read(&m_resourceID);
		file->Read(&m_resourceName);
		file->Read(&m_resourceFilePath);
		file->Read(&m_normalizedScale);
		file->Read(&meshCount);

		for (int i = 0; i < meshCount; i++)
		{
			auto mesh = make_shared<Mesh>();
			mesh->Deserialize(file.get());
			AddMeshAsNewResource(mesh);
		}

		return true;
	}

	bool Model::LoadFromForeignFormat(const string& filePath)
	{
		// Set some crucial data (Required by ModelImporter)
		string projectDir = m_context->GetSubsystem<ResourceManager>()->GetProjectDirectory();
		string modelDir = projectDir + FileSystem::GetFileNameNoExtensionFromFilePath(filePath) + "//"; // Assets/Sponza/
		SetResourceFilePath(modelDir + FileSystem::GetFileNameNoExtensionFromFilePath(filePath) + MODEL_EXTENSION); // Assets/Sponza/Sponza.model
		SetResourceName(FileSystem::GetFileNameNoExtensionFromFilePath(filePath)); // Sponza

		// Create asset directory (if it doesn't exist)]
		m_modelDirectoryMaterials = modelDir + "Materials//";
		m_modelDirectoryTextures = modelDir + "Textures//";
		FileSystem::CreateDirectory_(m_modelDirectoryMaterials);
		FileSystem::CreateDirectory_(m_modelDirectoryTextures);

		// Load the model
		if (m_resourceManager->GetModelImporter()._Get()->Load(this, filePath))
		{
			// Set the normalized scale to the root GameObject's transform
			m_normalizedScale = ComputeNormalizeScale();
			m_rootGameObj._Get()->GetComponent<Transform>()._Get()->SetScale(m_normalizedScale);
			m_rootGameObj._Get()->GetComponent<Transform>()._Get()->UpdateTransform();

			// Save the model in our custom format.
			SaveToFile(GetResourceFilePath());

			return true;
		}

		return false;
	}

	void Model::SetScale(float scale)
	{
		for (const auto& mesh : m_meshes)
		{
			mesh->SetScale(scale);
		}
	}

	float Model::ComputeNormalizeScale()
	{
		// Find the mesh with the largest bounding box
		auto largestBoundingBoxMesh = ComputeLargestBoundingBox().lock();

		// Calculate the scale offset
		float scaleOffset = !largestBoundingBoxMesh ? 1.0f : largestBoundingBoxMesh->GetBoundingBox().GetExtents().Length();

		// Return the scale
		return 1.0f / scaleOffset;
	}

	weak_ptr<Mesh> Model::ComputeLargestBoundingBox()
	{
		if (m_meshes.empty())
			return weak_ptr<Mesh>();

		Vector3 largestBoundingBox = Vector3::Zero;
		weak_ptr<Mesh> largestBoundingBoxMesh = m_meshes.front();

		for (auto& mesh : m_meshes)
		{
			if (!mesh)
				continue;

			Vector3 boundingBox = mesh->GetBoundingBox().GetExtents();
			if (boundingBox.Volume() > largestBoundingBox.Volume())
			{
				largestBoundingBox = boundingBox;
				largestBoundingBoxMesh = mesh;
			}
		}

		return largestBoundingBoxMesh;
	}

	void Model::ComputeDimensions()
	{
		for (auto& mesh : m_meshes)
		{
			if (!mesh)
				continue;

			if (!m_boundingBox.Defined())
			{
				m_boundingBox.ComputeFromMesh(mesh);
			}

			m_boundingBox.Merge(mesh->GetBoundingBox());
		}
	}
}
