/*
Copyright(c) 2016-2019 Panos Karabelas

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

//= INCLUDES ==============================
#include "Model.h"
#include "Mesh.h"
#include "Animation.h"
#include "Renderer.h"
#include "Material.h"
#include "../IO/FileStream.h"
#include "../Core/Stopwatch.h"
#include "../World/Entity.h"
#include "../World/Components/Transform.h"
#include "../World/Components/Renderable.h"
#include "../RHI/RHI_Implementation.h"
#include "../RHI/RHI_VertexBuffer.h"
#include "../RHI/RHI_IndexBuffer.h"
#include "../RHI/RHI_Texture.h"
#include "../Resource/ResourceCache.h"
//=========================================

//= NAMESPACES ================
using namespace std;
using namespace Directus::Math;
//=============================

namespace Directus
{
	Model::Model(Context* context) : IResource(context, Resource_Model)
	{
		m_normalizedScale	= 1.0f;
		m_isAnimated		= false;
		m_resourceManager	= m_context->GetSubsystem<ResourceCache>().get();
		m_rhiDevice			= m_context->GetSubsystem<Renderer>()->GetRHIDevice();
		m_memoryUsage		= 0;
		m_mesh				= make_unique<Mesh>();
	}

	Model::~Model()
	{
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
				LOG_WARNING("Failed to load model. Unable to find supported file in \"" + FileSystem::GetDirectoryFromFilePath(filePath) + "\".");
				return false;
			}
		}

		bool engineFormat = FileSystem::GetExtensionFromFilePath(modelFilePath) == EXTENSION_MODEL;
		bool success = engineFormat ? LoadFromEngineFormat(modelFilePath) : LoadFromForeignFormat(modelFilePath);

		Geometry_ComputeMemoryUsage();
		LOGF_INFO("Loading \"%s\" took %d ms", FileSystem::GetFileNameFromFilePath(filePath).c_str(), (int)timer.GetElapsedTimeMs());

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
		file->Write(m_mesh->Indices_Get());
		file->Write(m_mesh->Vertices_Get());	

		return true;
	}
	//=======================================================

	void Model::Geometry_Append(std::vector<unsigned int>& indices, std::vector<RHI_Vertex_PosUvNorTan>& vertices, unsigned int* indexOffset, unsigned int* vertexOffset)
	{
		if (indices.empty() || vertices.empty())
		{
			LOG_ERROR_INVALID_PARAMETER();
			return;
		}

		// Append indices and vertices to the main mesh
		m_mesh->Indices_Append(indices, indexOffset);
		m_mesh->Vertices_Append(vertices, vertexOffset);
	}

	void Model::Geometry_Get(unsigned int indexOffset, unsigned int indexCount, unsigned int vertexOffset, unsigned int vertexCount, vector<unsigned int>* indices, vector<RHI_Vertex_PosUvNorTan>* vertices)
	{
		m_mesh->Geometry_Get(indexOffset, indexCount, vertexOffset, vertexCount, indices, vertices);
	}

	void Model::Geometry_Update()
	{
		if (m_mesh->Indices_Count() == 0 || m_mesh->Vertices_Count() == 0)
		{
			LOG_ERROR_INVALID_PARAMETER();
			return;
		}

		Geometry_CreateBuffers();
		m_normalizedScale	= Geometry_ComputeNormalizedScale();
		m_memoryUsage		= Geometry_ComputeMemoryUsage();
		m_aabb				= BoundingBox(m_mesh->Vertices_Get());
	}

	void Model::AddMaterial(shared_ptr<Material>& material, const shared_ptr<Entity>& entity)
	{
		if (!material)
		{
			LOG_ERROR_INVALID_PARAMETER();
			return;
		}

		// Create a file path for this material
		material->SetResourceFilePath(m_modelDirectoryMaterials + material->GetResourceName() + EXTENSION_MATERIAL);

		// Save the material in the model directory		
		material->SaveToFile(material->GetResourceFilePath());

		// Keep a reference to it
		m_resourceManager->Cache(material);
		m_materials.emplace_back(material);

		// Create a Renderable and pass the material to it
		if (entity)
		{
			auto renderable = entity->AddComponent<Renderable>();
			renderable->Material_Set(material);
		}
	}

	void Model::AddAnimation(shared_ptr<Animation>& animation)
	{
		if (!animation)
		{
			LOG_ERROR_INVALID_PARAMETER();
			return;
		}

		m_context->GetSubsystem<ResourceCache>()->Cache<Animation>(animation);
		m_animations.emplace_back(animation);
		m_isAnimated = true;
	}

	void Model::AddTexture(shared_ptr<Material>& material, TextureType textureType, const string& filePath)
	{
		if (!material)
		{
			LOG_ERROR_INVALID_PARAMETER();
			return;
		}

		// Validate texture file path
		if (filePath == NOT_ASSIGNED)
		{
			LOG_WARNING("Provided texture file path hasn't been provided. Can't execute function");
			return;
		}

		// Try to get the texture
		auto texName = FileSystem::GetFileNameNoExtensionFromFilePath(filePath);
		auto texture = m_context->GetSubsystem<ResourceCache>()->GetByName<RHI_Texture>(texName);
		if (texture)
		{
			material->SetTextureSlot(textureType, texture);
		}
		// If we didn't get a texture, it's not cached, hence we have to load it and cache it now
		else if (!texture)
		{
			// Load texture
			texture = make_shared<RHI_Texture>(m_context);
			texture->LoadFromFile(filePath);

			// Update the texture with Model directory relative file path. Then save it to this directory
			string modelRelativeTexPath = m_modelDirectoryTextures + texName + EXTENSION_TEXTURE;
			texture->SetResourceFilePath(modelRelativeTexPath);
			texture->SetResourceName(FileSystem::GetFileNameNoExtensionFromFilePath(modelRelativeTexPath));
			texture->SaveToFile(modelRelativeTexPath);		
			texture->ClearTextureBytes(); // Now that the texture is saved, free up it's memory since we already have a shader resource

			// Set the texture to the provided material
			m_resourceManager->Cache(texture);
			material->SetTextureSlot(textureType, texture);
		}
	}

	void Model::SetWorkingDirectory(const string& directory)
	{
		// Set directories based on new directory
		m_modelDirectoryModel		= directory;
		m_modelDirectoryMaterials	= m_modelDirectoryModel + "Materials//";
		m_modelDirectoryTextures	= m_modelDirectoryModel + "Textures//";

		// Create directories
		FileSystem::CreateDirectory_(directory);
		FileSystem::CreateDirectory_(m_modelDirectoryMaterials);
		FileSystem::CreateDirectory_(m_modelDirectoryTextures);
	}

	bool Model::LoadFromEngineFormat(const string& filePath)
	{
		// Deserialize
		auto file = make_unique<FileStream>(filePath, FileStreamMode_Read);
		if (!file->IsOpen())
			return false;

		int meshCount = 0;

		file->Read(&m_resourceName);
		file->Read(&m_resourceFilePath);
		file->Read(&m_normalizedScale);
		file->Read(&m_mesh->Indices_Get());
		file->Read(&m_mesh->Vertices_Get());

		Geometry_Update();

		return true;
	}

	bool Model::LoadFromForeignFormat(const string& filePath)
	{
		// Set some crucial data (Required by ModelImporter)
		SetWorkingDirectory(m_context->GetSubsystem<ResourceCache>()->GetProjectDirectory() + FileSystem::GetFileNameNoExtensionFromFilePath(filePath) + "//"); // Assets/Sponza/
		SetResourceFilePath(m_modelDirectoryModel + FileSystem::GetFileNameNoExtensionFromFilePath(filePath) + EXTENSION_MODEL); // Assets/Sponza/Sponza.model
		SetResourceName(FileSystem::GetFileNameNoExtensionFromFilePath(filePath)); // Sponza

		// Load the model
		if (m_resourceManager->GetModelImporter()->Load(std::dynamic_pointer_cast<Model>(GetSharedPtr()), filePath))
		{
			// Set the normalized scale to the root entity's transform
			m_normalizedScale = Geometry_ComputeNormalizedScale();
			m_rootentity.lock()->GetComponent<Transform>()->SetScale(m_normalizedScale);
			m_rootentity.lock()->GetComponent<Transform>()->UpdateTransform();

			// Save the model in our custom format.
			SaveToFile(GetResourceFilePath());

			return true;
		}

		return false;
	}

	bool Model::Geometry_CreateBuffers()
	{
		bool success = true;

		// Get geometry
		vector<unsigned int> indices			= m_mesh->Indices_Get();
		vector<RHI_Vertex_PosUvNorTan> vertices	= m_mesh->Vertices_Get();

		if (!indices.empty())
		{
			m_indexBuffer = make_shared<RHI_IndexBuffer>(m_rhiDevice);
			if (!m_indexBuffer->Create(indices))
			{
				LOGF_ERROR("Failed to create index buffer for \"%s\".", m_resourceName.c_str());
				success = false;
			}
		}
		else
		{
			LOGF_ERROR("Failed to create index buffer for \"%s\". Provided indices are empty", m_resourceName.c_str());
			success = false;
		}

		if (!vertices.empty())
		{
			m_vertexBuffer = make_shared<RHI_VertexBuffer>(m_rhiDevice);
			if (!m_vertexBuffer->Create(vertices))
			{
				LOGF_ERROR("Failed to create vertex buffer for \"%s\".", m_resourceName.c_str());
				success = false;
			}
		}
		else
		{
			LOGF_ERROR("Failed to create vertex buffer for \"%s\". Provided vertices are empty", m_resourceName.c_str());
			success = false;
		}

		return success;
	}

	float Model::Geometry_ComputeNormalizedScale()
	{
		// Compute scale offset
		float scaleOffset = m_aabb.GetExtents().Length();

		// Return normalized scale
		return 1.0f / scaleOffset;
	}

	unsigned int Model::Geometry_ComputeMemoryUsage()
	{
		// Vertices & Indices
		unsigned int size = !m_mesh ? 0 : m_mesh->Geometry_MemoryUsage();

		// Buffers
		size += m_vertexBuffer->GetMemoryUsage();
		size += m_indexBuffer->GetMemoryUsage();

		return size;
	}
}
