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
#include "../RHI/RHI_Texture2D.h"
#include "../Resource/ResourceCache.h"
//=========================================

//= NAMESPACES ================
using namespace std;
using namespace Spartan::Math;
//=============================

namespace Spartan
{
	Model::Model(Context* context) : IResource(context, Resource_Model)
	{
		m_normalized_scale	= 1.0f;
		m_is_animated		= false;
		m_resource_manager	= m_context->GetSubsystem<ResourceCache>().get();
		m_rhi_device		= m_context->GetSubsystem<Renderer>()->GetRhiDevice();
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
	bool Model::LoadFromFile(const string& file_path)
	{
		Stopwatch timer;
		auto model_file_path = file_path;

		// Check if this is a directory instead of a model file path
		if (FileSystem::IsDirectory(file_path))
		{
			// If it is, try to find a model file in it
			auto model_file_paths = FileSystem::GetSupportedModelFilesInDirectory(file_path);
			if (!model_file_paths.empty())
			{
				model_file_path = model_file_paths.front();
			}
			else // abort
			{
				LOG_WARNING("Failed to load model. Unable to find supported file in \"" + FileSystem::GetDirectoryFromFilePath(file_path) + "\".");
				return false;
			}
		}

		const auto engine_format = FileSystem::GetExtensionFromFilePath(model_file_path) == EXTENSION_MODEL;
		const auto success = engine_format ? LoadFromEngineFormat(model_file_path) : LoadFromForeignFormat(model_file_path);

		GeometryComputeMemoryUsage();
		LOGF_INFO("Loading \"%s\" took %d ms", FileSystem::GetFileNameFromFilePath(file_path).c_str(), static_cast<int>(timer.GetElapsedTimeMs()));

		return success;
	}

	bool Model::SaveToFile(const string& file_path)
	{
		auto file = make_unique<FileStream>(file_path, FileStream_Write);
		if (!file->IsOpen())
			return false;

		file->Write(GetResourceName());
		file->Write(GetResourceFilePath());
		file->Write(m_normalized_scale);
		file->Write(m_mesh->Indices_Get());
		file->Write(m_mesh->Vertices_Get());	

		return true;
	}
	//=======================================================

	void Model::GeometryAppend(std::vector<unsigned int>& indices, std::vector<RHI_Vertex_PosTexNorTan>& vertices, unsigned int* index_offset, unsigned int* vertex_offset) const
	{
		if (indices.empty() || vertices.empty())
		{
			LOG_ERROR_INVALID_PARAMETER();
			return;
		}

		// Append indices and vertices to the main mesh
		m_mesh->Indices_Append(indices, index_offset);
		m_mesh->Vertices_Append(vertices, vertex_offset);
	}

	void Model::GeometryGet(const unsigned int index_offset, const unsigned int index_count, const unsigned int vertex_offset, const unsigned int vertex_count, vector<unsigned int>* indices, vector<RHI_Vertex_PosTexNorTan>* vertices) const
	{
		m_mesh->Geometry_Get(index_offset, index_count, vertex_offset, vertex_count, indices, vertices);
	}

	void Model::GeometryUpdate()
	{
		if (m_mesh->Indices_Count() == 0 || m_mesh->Vertices_Count() == 0)
		{
			LOG_ERROR_INVALID_PARAMETER();
			return;
		}

		GeometryCreateBuffers();
		m_normalized_scale	= GeometryComputeNormalizedScale();
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
		material->SetResourceFilePath(m_model_directory_materials + material->GetResourceName() + EXTENSION_MATERIAL);

		// Save the material in the model directory		
		material->SaveToFile(material->GetResourceFilePath());

		// Keep a reference to it
		m_resource_manager->Cache(material);
		m_materials.emplace_back(material);

		// Create a Renderable and pass the material to it
		if (entity)
		{
			auto renderable = entity->AddComponent<Renderable>();
			renderable->MaterialSet(material);
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
		m_is_animated = true;
	}

	void Model::AddTexture(shared_ptr<Material>& material, const TextureType texture_type, const string& file_path)
	{
		if (!material)
		{
			LOG_ERROR_INVALID_PARAMETER();
			return;
		}

		// Validate texture file path
		if (file_path == NOT_ASSIGNED)
		{
			LOG_WARNING("Provided texture file path hasn't been provided. Can't execute function");
			return;
		}

		// Try to get the texture
		const auto tex_name = FileSystem::GetFileNameNoExtensionFromFilePath(file_path);
		auto texture = m_context->GetSubsystem<ResourceCache>()->GetByName<RHI_Texture2D>(tex_name);
		if (texture)
		{
			material->SetTextureSlot(texture_type, texture);
		}
		// If we didn't get a texture, it's not cached, hence we have to load it and cache it now
		else if (!texture)
		{
			// Load texture
			bool generate_mipmaps = true;
			texture = make_shared<RHI_Texture2D>(m_context, generate_mipmaps);
			texture->LoadFromFile(file_path);

			// Update the texture with Model directory relative file path. Then save it to this directory
			const auto model_relative_tex_path = m_model_directory_textures + tex_name + EXTENSION_TEXTURE;
			texture->SetResourceFilePath(model_relative_tex_path);
			texture->SetResourceName(FileSystem::GetFileNameNoExtensionFromFilePath(model_relative_tex_path));
			texture->SaveToFile(model_relative_tex_path);		

			// Set the texture to the provided material
			m_resource_manager->Cache(texture);
			material->SetTextureSlot(texture_type, texture);
		}
	}

	void Model::SetWorkingDirectory(const string& directory)
	{
		// Set directories based on new directory
		m_model_directory_model		= directory;
		m_model_directory_materials	= m_model_directory_model + "Materials//";
		m_model_directory_textures	= m_model_directory_model + "Textures//";

		// Create directories
		FileSystem::CreateDirectory_(directory);
		FileSystem::CreateDirectory_(m_model_directory_materials);
		FileSystem::CreateDirectory_(m_model_directory_textures);
	}

	bool Model::LoadFromEngineFormat(const string& file_path)
	{
		// Deserialize
		auto file = make_unique<FileStream>(file_path, FileStream_Read);
		if (!file->IsOpen())
			return false;

		SetResourceName(file->ReadAs<string>());
		SetResourceFilePath(file->ReadAs<string>());
		file->Read(&m_normalized_scale);
		file->Read(&m_mesh->Indices_Get());
		file->Read(&m_mesh->Vertices_Get());

		GeometryUpdate();

		return true;
	}

	bool Model::LoadFromForeignFormat(const string& file_path)
	{
		// Set some crucial data (Required by ModelImporter)
		SetWorkingDirectory(m_context->GetSubsystem<ResourceCache>()->GetProjectDirectory() + FileSystem::GetFileNameNoExtensionFromFilePath(file_path) + "//"); // Assets/Sponza/
		SetResourceFilePath(m_model_directory_model + FileSystem::GetFileNameNoExtensionFromFilePath(file_path) + EXTENSION_MODEL); // Assets/Sponza/Sponza.model
		SetResourceName(FileSystem::GetFileNameNoExtensionFromFilePath(file_path)); // Sponza

		// Load the model
		if (m_resource_manager->GetModelImporter()->Load(std::dynamic_pointer_cast<Model>(GetSharedPtr()), file_path))
		{
			// Set the normalized scale to the root entity's transform
			m_normalized_scale = GeometryComputeNormalizedScale();
			m_root_entity.lock()->GetComponent<Transform>()->SetScale(m_normalized_scale);
			m_root_entity.lock()->GetComponent<Transform>()->UpdateTransform();

			// Save the model in our custom format.
			SaveToFile(GetResourceFilePath());

			return true;
		}

		return false;
	}

	bool Model::GeometryCreateBuffers()
	{
		auto success = true;

		// Get geometry
		auto indices	= m_mesh->Indices_Get();
		auto vertices	= m_mesh->Vertices_Get();

		if (!indices.empty())
		{
			m_index_buffer = make_shared<RHI_IndexBuffer>(m_rhi_device);
			if (!m_index_buffer->Create(indices))
			{
				LOGF_ERROR("Failed to create index buffer for \"%s\".", GetResourceName().c_str());
				success = false;
			}
		}
		else
		{
			LOGF_ERROR("Failed to create index buffer for \"%s\". Provided indices are empty", GetResourceName().c_str());
			success = false;
		}

		if (!vertices.empty())
		{
			m_vertex_buffer = make_shared<RHI_VertexBuffer>(m_rhi_device);
			if (!m_vertex_buffer->Create(vertices))
			{
				LOGF_ERROR("Failed to create vertex buffer for \"%s\".", GetResourceName().c_str());
				success = false;
			}
		}
		else
		{
			LOGF_ERROR("Failed to create vertex buffer for \"%s\". Provided vertices are empty", GetResourceName().c_str());
			success = false;
		}

		return success;
	}

	float Model::GeometryComputeNormalizedScale() const
	{
		// Compute scale offset
		const auto scale_offset = m_aabb.GetExtents().Length();

		// Return normalized scale
		return 1.0f / scale_offset;
	}

	unsigned int Model::GeometryComputeMemoryUsage() const
	{
		// Vertices & Indices
		auto size = !m_mesh ? 0 : m_mesh->Geometry_MemoryUsage();

		// Buffers
		size += static_cast<unsigned int>(m_vertex_buffer->GetSize());
		size += static_cast<unsigned int>(m_index_buffer->GetSize());

		return size;
	}
}
