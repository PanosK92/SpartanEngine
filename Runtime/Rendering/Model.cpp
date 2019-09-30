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
#include "../IO/FileStream.h"
#include "../Core/Stopwatch.h"
#include "../Resource/ResourceCache.h"
#include "../World/Entity.h"
#include "../World/Components/Transform.h"
#include "../World/Components/Renderable.h"
#include "../RHI/RHI_VertexBuffer.h"
#include "../RHI/RHI_IndexBuffer.h"
#include "../RHI/RHI_Texture2D.h"
//=========================================

//= NAMESPACES ================
using namespace std;
using namespace Spartan::Math;
//=============================

namespace Spartan
{
	Model::Model(Context* context) : IResource(context, Resource_Model)
	{
		m_resource_manager	= m_context->GetSubsystem<ResourceCache>().get();
		m_rhi_device		= m_context->GetSubsystem<Renderer>()->GetRhiDevice();
		m_mesh				= make_unique<Mesh>();
	}

	Model::~Model()
	{
        Clear();
	}

    void Model::Clear()
    {
        m_root_entity.reset();
        m_vertex_buffer.reset();
        m_index_buffer.reset();
        m_mesh->Geometry_Clear();
        m_aabb.Undefine();
        m_normalized_scale = 1.0f;
        m_is_animated = false;
    }

	bool Model::LoadFromFile(const string& file_path)
	{
		Stopwatch timer;
		string file_path_relative = FileSystem::GetRelativeFilePath(file_path);

		// Check if this is a directory instead of a model file path
		if (FileSystem::IsDirectory(file_path))
		{
			// If it is, try to find a model file in it
			auto model_file_paths = FileSystem::GetSupportedModelFilesInDirectory(file_path);
			if (!model_file_paths.empty())
			{
				file_path_relative = model_file_paths.front();
			}
			else // abort
			{
				LOGF_WARNING("Failed to load model. Unable to find supported file in \"%s\".", FileSystem::GetDirectoryFromFilePath(file_path).c_str());
				return false;
			}
		}

        // Load engine format
        if (FileSystem::GetExtensionFromFilePath(file_path_relative) == EXTENSION_MODEL)
        {
            // Deserialize
            auto file = make_unique<FileStream>(file_path_relative, FileStream_Read);
            if (!file->IsOpen())
                return false;

            SetResourceFilePath(file->ReadAs<string>());
            file->Read(&m_normalized_scale);
            file->Read(&m_mesh->Indices_Get());
            file->Read(&m_mesh->Vertices_Get());

            UpdateGeometry();
        }
        // Load foreign format
        else
        {
            if (m_resource_manager->GetModelImporter()->Load(this, file_path_relative))
            {
                // Set the normalized scale to the root entity's transform
                m_normalized_scale = GeometryComputeNormalizedScale();
                m_root_entity.lock()->GetComponent<Transform>()->SetScale(m_normalized_scale);
                m_root_entity.lock()->GetComponent<Transform>()->UpdateTransform();
            }
            else
            {
                return false;
            }
        }

        // Set a file path so the the model can be used by the resource cache
        SetResourceFilePath(file_path);

		m_size = GeometryComputeMemoryUsage();
		LOGF_INFO("Loading \"%s\" took %d ms", FileSystem::GetFileNameFromFilePath(file_path).c_str(), static_cast<int>(timer.GetElapsedTimeMs()));

		return true;
	}

	bool Model::SaveToFile(const string& file_path)
	{
		auto file = make_unique<FileStream>(file_path, FileStream_Write);
		if (!file->IsOpen())
			return false;

		file->Write(GetResourceFilePath());
		file->Write(m_normalized_scale);
		file->Write(m_mesh->Indices_Get());
		file->Write(m_mesh->Vertices_Get());	

		return true;
	}

	void Model::AppendGeometry(const vector<uint32_t>& indices, const vector<RHI_Vertex_PosTexNorTan>& vertices, uint32_t* index_offset, uint32_t* vertex_offset) const
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

	void Model::GetGeometry(const uint32_t index_offset, const uint32_t index_count, const uint32_t vertex_offset, const uint32_t vertex_count, vector<uint32_t>* indices, vector<RHI_Vertex_PosTexNorTan>* vertices) const
	{
		m_mesh->Geometry_Get(index_offset, index_count, vertex_offset, vertex_count, indices, vertices);
	}

	void Model::UpdateGeometry()
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
		if (!material || !entity)
		{
			LOG_ERROR_INVALID_PARAMETER();
			return;
		}

		// Create a file path for this material
        string spartan_asset_path = FileSystem::GetDirectoryFromFilePath(GetResourceFilePathNative()) + material->GetResourceName() + EXTENSION_MATERIAL;
		material->SetResourceFilePath(spartan_asset_path);

		// Create a Renderable and pass the material to it
        auto renderable = entity->AddComponent<Renderable>();
        renderable->SetMaterial(material);
	}

	void Model::AddAnimation(shared_ptr<Animation>& animation)
	{
		if (!animation)
		{
			LOG_ERROR_INVALID_PARAMETER();
			return;
		}

		m_is_animated = true;
	}

	void Model::AddTexture(shared_ptr<Material>& material, const TextureType texture_type, const string& file_path)
	{
		if (!material || file_path.empty())
		{
			LOG_ERROR_INVALID_PARAMETER();
			return;
		}

		// Try to get the texture
		const auto tex_name = FileSystem::GetFileNameNoExtensionFromFilePath(file_path);
		if (auto texture = m_context->GetSubsystem<ResourceCache>()->GetByName<RHI_Texture2D>(tex_name))
		{
			material->SetTextureSlot(texture_type, texture);
		}
		// If we didn't get a texture, it's not cached, hence we have to load it and cache it now
		else
		{
			// Load texture
			auto generate_mipmaps = true;
            texture = make_shared<RHI_Texture2D>(m_context, generate_mipmaps);
			texture->LoadFromFile(file_path);

			// Set the texture to the provided material
			material->SetTextureSlot(texture_type, texture);
		}
	}

	bool Model::GeometryCreateBuffers()
	{
		auto success = true;

		// Get geometry
		const auto indices	= m_mesh->Indices_Get();
		const auto vertices	= m_mesh->Vertices_Get();

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

	uint32_t Model::GeometryComputeMemoryUsage() const
	{
        if (!m_vertex_buffer || !m_index_buffer)
        {
            LOG_ERROR_INVALID_INTERNALS();
            return 0;
        }

		// Vertices & Indices
		auto size = !m_mesh ? 0 : m_mesh->Geometry_MemoryUsage();

		// Buffers
		size += static_cast<uint32_t>(m_vertex_buffer->GetSize());
		size += static_cast<uint32_t>(m_index_buffer->GetSize());

		return size;
	}
}
