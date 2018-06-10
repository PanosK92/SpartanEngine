/*
Copyright(c) 2016-2018 Panos Karabelas

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

//= INCLUDES ================================
#include "Model.h"
#include "Mesh.h"
#include "../Scene/GameObject.h"
#include "../Scene/Components/Transform.h"
#include "../Scene/Components/Renderable.h"
#include "../Rendering/RI/RI_Vertex.h"
#include "../Rendering/RI/RI_Texture.h"
#include "../Rendering/Material.h"
#include "../Rendering/Animation.h"
#include "../IO/FileStream.h"
#include "../Core/Stopwatch.h"
#include "../Resource/ResourceManager.h"
//===========================================

//= NAMESPACES ================
using namespace std;
using namespace Directus::Math;
//=============================

namespace Directus
{
	Model::Model(Context* context) : IResource(context)
	{
		//= IResource ============
		RegisterResource<Model>();
		//========================

		m_normalizedScale	= 1.0f;
		m_isAnimated		= false;
		m_resourceManager	= m_context->GetSubsystem<ResourceManager>();
		m_memoryUsage		= 0;
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
		LOG_INFO("Model: Loading \"" + FileSystem::GetFileNameFromFilePath(filePath) + "\" took " + to_string((int)timer.GetElapsedTimeMs()) + " ms");

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

	void Model::AddMesh(const string& name, vector<RI_Vertex_PosUVTBN>& vertices, vector<unsigned int>& indices, const weak_ptr<GameObject>& gameObject)
	{
		// In case this mesh is already loaded, use that one
		auto existingMesh = m_context->GetSubsystem<ResourceManager>()->GetResourceByName<Mesh>(name);
		if (!existingMesh.expired())
		{
			AddMesh(existingMesh, gameObject, false);
			return;
		}

		// In case this mesh is new, create one 
		auto mesh = make_shared<Mesh>(m_context);
		mesh->SetModelName(GetResourceName());
		mesh->SetResourceName(name);
		mesh->SetVertices(vertices);
		mesh->SetIndices(indices);

		// Add the mesh to the model
		AddMesh(mesh, gameObject, true);
	}

	void Model::AddMesh(const weak_ptr<Mesh>& mesh, const weak_ptr<GameObject>& gameObject, bool autoCache /* true */)
	{
		if (mesh.expired())
		{
			LOG_WARNING("Model::AddMesh(): Provided mesh is null, can't execute function");
			return;
		}

		// Don't add mesh if it's already added
		if (DetermineMeshUniqueness(mesh.lock().get()))
		{	
			return;
		}
			
		AddStandardComponents(gameObject, mesh);

		// Update the mesh with Model directory relative file path. Then save it to this directory
		string modelRelativeTexPath = m_modelDirectoryMeshes + mesh.lock()->GetResourceName() + MESH_EXTENSION;
		mesh.lock()->SetResourceFilePath(modelRelativeTexPath);
		mesh.lock()->SetModelName(GetResourceName());
		mesh.lock()->SaveToFile(modelRelativeTexPath);

		// Construct mesh (vertex buffer, index buffer, bounding box, etc...)
		mesh.lock()->Construct();

		// Calculate the bounding box of the model as well
		m_boundingBox.Merge(mesh.lock()->GetBoundingBox());

		// Cache it or use the provided reference as is
		auto meshRef = autoCache ? mesh.lock()->Cache<Mesh>() : mesh;

		if (!meshRef.expired())
		{
			// Save it
			m_meshes.push_back(meshRef);

			// Add some standard components
			AddStandardComponents(gameObject, meshRef);

			// Release geometry data now that we are done with it
			meshRef.lock()->ClearGeometry();
		}
	}

	void Model::AddMaterial(const weak_ptr<Material>& material, const weak_ptr<GameObject>& gameObject, bool autoCache /* true */)
	{
		if (material.expired())
		{
			LOG_WARNING("Model::AddMaterial(): Provided material is null, can't execute function");
			return;
		}

		// Create a file path for this material
		material.lock()->SetResourceFilePath(m_modelDirectoryMaterials + material.lock()->GetResourceName() + MATERIAL_EXTENSION);

		// Save the material in the model directory		
		material.lock()->SaveToFile(material.lock()->GetResourceFilePath());

		// Cache it or use the provided reference as is
		auto matRef = autoCache ? material.lock()->Cache<Material>() : material;

		// Keep a reference to it
		m_materials.push_back(matRef);

		// Create a Renderable and pass the material to it
		if (!gameObject.expired())
		{
			auto renderable = gameObject.lock()->AddComponent<Renderable>().lock();
			renderable->SetMaterialFromMemory(matRef, false);
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

	void Model::AddTexture(const weak_ptr<Material>& material, TextureType textureType, const string& filePath)
	{
		// Validate material
		if (material.expired())
			return;

		// Validate texture file path
		if (filePath == NOT_ASSIGNED)
		{
			LOG_WARNING("Model::AddTexture(): Provided texture file path hasn't been provided. Can't execute function");
			return;
		}

		// Try to get the texture
		auto texName = FileSystem::GetFileNameNoExtensionFromFilePath(filePath);
		auto texture = m_context->GetSubsystem<ResourceManager>()->GetResourceByName<RI_Texture>(texName).lock();
		if (texture)
		{
			texture->SetType(textureType); // if this texture was cached from the editor, it has no type, we have to set it
			material.lock()->SetTexture(texture, false);
		}
		// If we didn't get a texture, it's not cached, hence we have to load it and cache it now
		else if (!texture)
		{
			// Load texture
			texture = make_shared<RI_Texture>(m_context);
			texture->LoadFromFile(filePath);
			texture->SetType(textureType);

			// Update the texture with Model directory relative file path. Then save it to this directory
			string modelRelativeTexPath = m_modelDirectoryTextures + texName + TEXTURE_EXTENSION;
			texture->SetResourceFilePath(modelRelativeTexPath);
			texture->SetResourceName(FileSystem::GetFileNameNoExtensionFromFilePath(modelRelativeTexPath));
			texture->SaveToFile(modelRelativeTexPath);
			// Now that the texture is saved, free up it's memory since we already have a shader resource
			texture->ClearTextureBytes();

			// Set the texture to the provided material
			material.lock()->SetTexture(texture->Cache<RI_Texture>(), false);
		}
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
		m_modelDirectoryModel		= directory;
		m_modelDirectoryMeshes		= m_modelDirectoryModel + "Meshes//";
		m_modelDirectoryMaterials	= m_modelDirectoryModel + "Materials//";
		m_modelDirectoryTextures	= m_modelDirectoryModel + "Textures//";

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

	void Model::AddStandardComponents(const weak_ptr<GameObject>& gameObject, const weak_ptr<Mesh>& mesh)
	{
		if (gameObject.expired())
			return;

		Renderable* renderable = gameObject.lock()->AddComponent<Renderable>().lock().get();
		renderable->SetMesh(mesh);
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
		m_memoryUsage = 0;
		for (const auto& mesh : m_meshes)
		{
			m_memoryUsage += mesh.lock()->GetMemory();
		}
	}

	bool Model::DetermineMeshUniqueness(Mesh* mesh)
	{
		// Problem:
		// Some meshes can come from model formats like .obj
		// Such formats contain pure geometry data, meaning that there is no transformation data.
		// This in turn means that in order to have instances of the same mesh using different transforms,
		// .obj simply re-defines the mesh in all the needed transformations using the exact same mesh name.
		// This makes it easy to think that a mesh is already cached, hence ignore it and then see that the model is missing meshes.
		// Solution:
		// We simply go through are meshes, all meshes conflicting with or mesh (by name) are kept.
		// We then compare our mesh against those meshes and to see if it's truly unique or not.
		// If it is unique, we rename it to something that's also unique, so the engine doesn't discard it.

		// Find all the meshes with the same name
		vector<weak_ptr<Mesh>> sameNameMeshes;
		for (const auto& cachedMesh : m_meshes)
		{
			if (cachedMesh.lock()->GetResourceName() == mesh->GetResourceName() 
				|| cachedMesh.lock()->GetResourceName().find(mesh->GetResourceName()) != string::npos)
			{
				sameNameMeshes.push_back(cachedMesh);
			}
		}

		bool isUnique = true;
		for (const auto& cachedMesh : sameNameMeshes)
		{
			// Vertex count matches
			if (cachedMesh.lock()->GetVertexCount() != mesh->GetVertexCount())
				continue;

			vector<RI_Vertex_PosUVTBN> meshVertices;
			vector<unsigned int> meshIndices;
			mesh->GetGeometry(&meshVertices, &meshIndices);

			vector<RI_Vertex_PosUVTBN> cachedVertices;
			vector<unsigned int> cachedIndices;
			cachedMesh.lock()->GetGeometry(&cachedVertices, &cachedIndices);

			bool geometryMatches = true;
			for (unsigned int i = 0; i < meshVertices.size(); i++)
			{
				if (meshVertices[i].pos != cachedVertices[i].pos)
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

		// If the mesh is unique, give it a different name (in case another with the same name exists)
		if (isUnique)
		{
			string num = sameNameMeshes.empty() ? string("") : "_" + to_string(sameNameMeshes.size() + 1);
			mesh->SetResourceName(mesh->GetResourceName() + num);
		}

		return !isUnique;
	}
}
