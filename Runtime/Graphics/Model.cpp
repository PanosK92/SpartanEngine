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
#include "../Scene/GameObject.h"
#include "../Resource/ResourceManager.h"
#include "../Components/Transform.h"
#include "../Components/MeshFilter.h"
#include "../Components/MeshRenderer.h"
#include "../Components/RigidBody.h"
#include "../Components/Collider.h"
#include "../Graphics/Vertex.h"
#include "../Graphics/Material.h"
#include "../Graphics/Animation.h"
#include "../IO/FileStream.h"
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
		RegisterResource(Resource_Model);
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

	//= RESOURCE ============================================
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

		ComputeMemoryUsage();
		LOG_INFO("Model: Loading \"" + FileSystem::GetFileNameFromFilePath(filePath) + "\" took " + to_string((int)timer.GetElapsedTime()) + " ms");

		return success;
	}

	bool Model::SaveToFile(const string& filePath)
	{
		auto file = make_unique<FileStream>(filePath, FileStreamMode_Write);
		if (!file->IsOpen())
			return false;

		file->Write(GetResourceName());
		file->Write(GetResourceFilePath());
		file->Write(m_normalizedScale);
		file->Write((int)m_meshes.size());
		for (const auto& mesh : m_meshes)
		{
			file->Write(mesh.lock()->GetResourceName());
		}

		return true;
	}
	//=======================================================

	void Model::AddMesh(const string& name, vector<VertexPosTexTBN>& vertices, vector<unsigned int>& indices, weak_ptr<GameObject> gameObject)
	{
		// In case this mesh is already loaded, use that one
		auto existingMesh = m_context->GetSubsystem<ResourceManager>()->GetResourceByName<Mesh>(name);
		if (!existingMesh.expired())
		{
			AddMesh(existingMesh, gameObject);
			return;
		}

		// In case this mesh is new, create one 
		auto mesh = make_shared<Mesh>(m_context);
		mesh->SetModelName(GetResourceName());
		mesh->SetResourceName(name);
		mesh->SetVertices(vertices);
		mesh->SetIndices(indices);

		// Add the mesh to the model
		AddMesh(mesh, gameObject);
	}

	void Model::AddMesh(weak_ptr<Mesh> mesh, weak_ptr<GameObject> gameObject)
	{
		if (mesh.expired())
			return;

		// Don't add mesh if it's already added
		weak_ptr<Mesh> modelCached;
		DetermineMeshUniqueness(mesh, modelCached);
		if (!modelCached.expired())
		{
			// The mesh is cached but we must not forget to add
			// some standard components to the GameObject that uses it.
			AddStandardComponents(gameObject, modelCached);
			return;
		}

		// Update the mesh with Model directory relative file path. Then save it to this directory
		string modelRelativeTexPath = m_modelDirectoryMeshes + mesh.lock()->GetResourceName() + MESH_EXTENSION;
		mesh.lock()->SetResourceFilePath(modelRelativeTexPath);
		mesh.lock()->SetModelName(GetResourceName());
		mesh.lock()->SaveToFile(modelRelativeTexPath);

		// Construct mesh (vertex buffer, index buffer, bounding box, etc...)
		mesh.lock()->Construct();

		// Calculate the bounding box of the model as well
		m_boundingBox.Merge(mesh.lock()->GetBoundingBox());

		// Add it to the resource manager
		auto weakMesh = m_context->GetSubsystem<ResourceManager>()->Add<Mesh>(mesh.lock());

		if (!weakMesh.expired())
		{
			// Save it
			m_meshes.push_back(weakMesh);

			// Add some standard components
			AddStandardComponents(gameObject, weakMesh);

			// Release geometry data now that we are done with it
			weakMesh.lock()->ClearGeometry();
		}
	}

	void Model::AddMaterial(weak_ptr<Material> material, weak_ptr<GameObject> gameObject)
	{
		if (material.expired())
			return;

		// Create a file path for this material
		material.lock()->SetResourceFilePath(m_modelDirectoryMaterials + material.lock()->GetResourceName() + MATERIAL_EXTENSION);

		// Add it to our resources
		weak_ptr<Material> weakMat = m_context->GetSubsystem<ResourceManager>()->Add<Material>(material.lock());

		// Save the material in the model directory		
		weakMat.lock()->SaveToFile(weakMat.lock()->GetResourceFilePath());

		// Keep a reference to it
		m_materials.push_back(weakMat);

		// Create a MeshRenderer and pass the Material to it
		if (!gameObject.expired())
		{
			MeshRenderer* meshRenderer = gameObject.lock()->AddComponent<MeshRenderer>().lock().get();
			meshRenderer->SetMaterialFromMemory(material);
		}
	}

	weak_ptr<Animation> Model::AddAnimation(weak_ptr<Animation> animation)
	{
		if (animation.expired())
			return animation;

		// Add it to our resources
		auto weakAnim = m_context->GetSubsystem<ResourceManager>()->Add<Animation>(animation.lock());

		// Keep a reference to it
		m_animations.push_back(weakAnim);

		m_isAnimated = true;

		// Return it
		return weakAnim;
	}

	void Model::AddTexture(const weak_ptr<Material> material, TextureType textureType, const string& filePath)
	{
		// Validate material
		if (material.expired())
			return;

		// Validate texture file path
		if (filePath == NOT_ASSIGNED)
		{
			LOG_WARNING("Model: Failed to find model requested texture \"" + filePath + "\".");
			return;
		}

		// Check if the texture is already loaded
		auto resourceMng = m_context->GetSubsystem<ResourceManager>();
		string texName = FileSystem::GetFileNameNoExtensionFromFilePath(filePath);
		weak_ptr<Texture> texture = resourceMng->GetResourceByName<Texture>(texName);

		// If the texture is not loaded, load it 
		if (texture.expired())
		{
			// Load texture into memory
			texture = resourceMng->Load<Texture>(filePath);
			if (texture.expired())
				return;

			// Set texture type
			texture.lock()->SetType(textureType);

			// Update the texture with Model directory relative file path. Then save it to this directory
			string modelRelativeTexPath = m_modelDirectoryTextures + texName + TEXTURE_EXTENSION;
			texture.lock()->SetResourceFilePath(modelRelativeTexPath);
			texture.lock()->SetResourceName(FileSystem::GetFileNameNoExtensionFromFilePath(modelRelativeTexPath));
			texture.lock()->SaveToFile(modelRelativeTexPath);

			// Since the texture has been loaded and had it's texture bits saved, clear them to free some memory
			texture.lock()->ClearTextureBits();
		}

		// Set the texture to the provided material
		material.lock()->SetTexture(texture);
	}

	weak_ptr<Mesh> Model::GetMeshByName(const string& name)
	{
		for (const auto& mesh : m_meshes)
		{
			if (mesh.lock()->GetResourceName() == name)
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

	void Model::SetWorkingDirectory(const string& directory)
	{
		// Set directoties based on new directory
		m_modelDirectoryModel = directory;
		m_modelDirectoryMeshes = m_modelDirectoryModel + "Meshes//";
		m_modelDirectoryMaterials = m_modelDirectoryModel + "Materials//";
		m_modelDirectoryTextures = m_modelDirectoryModel + "Textures//";

		// Create directories
		FileSystem::CreateDirectory_(directory);
		FileSystem::CreateDirectory_(m_modelDirectoryMeshes);
		FileSystem::CreateDirectory_(m_modelDirectoryMaterials);
		FileSystem::CreateDirectory_(m_modelDirectoryTextures);
	}

	bool Model::LoadFromEngineFormat(const string& filePath)
	{
		// Deserialize
		unique_ptr<FileStream> file = make_unique<FileStream>(filePath, FileStreamMode_Read);
		if (!file->IsOpen())
			return false;

		int meshCount = 0;

		file->Read(&m_resourceName);
		file->Read(&m_resourceFilePath);
		file->Read(&m_normalizedScale);
		file->Read(&meshCount);
		string meshName = NOT_ASSIGNED;
		for (int i = 0; i < meshCount; i++)
		{
			file->Read(&meshName);
			auto mesh = m_context->GetSubsystem<ResourceManager>()->GetResourceByName<Mesh>(meshName);
			if (mesh.expired())
			{
				LOG_WARNING("Model: Failed to load mesh \"" + meshName + "\"");
				continue;
			}
			m_meshes.push_back(mesh);
		}

		return true;
	}

	bool Model::LoadFromForeignFormat(const string& filePath)
	{
		// Set some crucial data (Required by ModelImporter)
		SetWorkingDirectory(m_context->GetSubsystem<ResourceManager>()->GetProjectDirectory() + FileSystem::GetFileNameNoExtensionFromFilePath(filePath) + "//"); // Assets/Sponza/
		SetResourceFilePath(m_modelDirectoryModel + FileSystem::GetFileNameNoExtensionFromFilePath(filePath) + MODEL_EXTENSION); // Assets/Sponza/Sponza.model
		SetResourceName(FileSystem::GetFileNameNoExtensionFromFilePath(filePath)); // Sponza

		// Load the model
		if (m_resourceManager->GetModelImporter().lock()->Load(this, filePath))
		{
			// Set the normalized scale to the root GameObject's transform
			m_normalizedScale = ComputeNormalizeScale();
			m_rootGameObj.lock()->GetComponent<Transform>().lock()->SetScale(m_normalizedScale);
			m_rootGameObj.lock()->GetComponent<Transform>().lock()->UpdateTransform();

			// Save the model in our custom format.
			SaveToFile(GetResourceFilePath());

			return true;
		}

		return false;
	}

	void Model::AddStandardComponents(weak_ptr<GameObject> gameObject, weak_ptr<Mesh> mesh)
	{
		if (gameObject.expired())
			return;

		// Add a MeshFilter
		MeshFilter* meshFilter = gameObject.lock()->AddComponent<MeshFilter>().lock().get();
		meshFilter->SetMesh(mesh);

		if (meshFilter->GetMeshType() == MeshType_Custom)
		{
			// Add a RigidBody
			gameObject.lock()->AddComponent<RigidBody>();

			// Add a Collider
			Collider* collider = gameObject.lock()->AddComponent<Collider>().lock().get();
			collider->SetShapeType(ColliderShape_Mesh);
		}
	}

	void Model::DetermineMeshUniqueness(weak_ptr<Mesh> mesh, weak_ptr<Mesh> modelCached)
	{
		// Some meshes can come from model formats like .obj
		// Such formats contain pure geometry data, meaning that there is no transformation data.
		// This in turn means that in order to have instances of the same mesh using different transforms,
		// .obj simply re-defines the mesh in all the needed transformations. Because of that we can't simply compare
		// mesh names to decide if they are different or not, we have to do a more extensive testing to determine the uniquness of a mesh.


		// Find all the meshes with the same name
		vector<weak_ptr<Mesh>> sameNameMeshes;
		for (const auto& cachedMesh : m_meshes)
		{
			if (cachedMesh.lock()->GetResourceName() == mesh.lock()->GetResourceName() 
				|| cachedMesh.lock()->GetResourceName().find(mesh.lock()->GetResourceName()) != string::npos)
			{
				sameNameMeshes.push_back(cachedMesh);
			}
		}

		bool isUnique = true;
		for (const auto& cachedMesh : sameNameMeshes)
		{
			// Vertex count matches
			if (cachedMesh.lock()->GetVertexCount() != mesh.lock()->GetVertexCount())
				continue;

			vector<VertexPosTexTBN> meshVertices;
			vector<unsigned int> meshIndices;
			mesh.lock()->GetGeometry(&meshVertices, &meshIndices);

			vector<VertexPosTexTBN> cachedVertices;
			vector<unsigned int> cachedIndices;
			cachedMesh.lock()->GetGeometry(&cachedVertices, &cachedIndices);

			bool geometryMatches = true;
			for (int i = 0; i < meshVertices.size(); i++)
			{
				if (meshVertices[i].position != cachedVertices[i].position)
				{
					geometryMatches = false;
					break;
				}
			}

			if (geometryMatches)
			{
				isUnique = false;
				break;
			}
		}

		// If the mesh is unique, give it a different name (in case other with the same name exist)
		if (isUnique)
		{
			string num = sameNameMeshes.size() == 0 ? string("") : "_" + to_string(sameNameMeshes.size() + 1);
			mesh.lock()->SetResourceName(mesh.lock()->GetResourceName() + num);
		}
		else
		{
			modelCached = sameNameMeshes.front();
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
			if (!mesh.expired())
				continue;

			Vector3 boundingBox = mesh.lock()->GetBoundingBox().GetExtents();
			if (boundingBox.Volume() > largestBoundingBox.Volume())
			{
				largestBoundingBox = boundingBox;
				largestBoundingBoxMesh = mesh;
			}
		}

		return largestBoundingBoxMesh;
	}

	void Model::ComputeMemoryUsage()
	{
		m_memoryUsageKB = 0;
		for (const auto& mesh : m_meshes)
		{
			m_memoryUsageKB += mesh.lock()->GetMemoryUsageKB();
		}
	}
}
