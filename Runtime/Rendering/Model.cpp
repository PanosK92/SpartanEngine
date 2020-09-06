/*
Copyright(c) 2016-2020 Panos Karabelas

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
#include "Spartan.h"
#include "Model.h"
#include "Mesh.h"
#include "Renderer.h"
#include "../IO/FileStream.h"
#include "../Core/Stopwatch.h"
#include "../Resource/ResourceCache.h"
#include "../Resource/Import/ModelImporter.h"
#include "../World/Entity.h"
#include "../World/Components/Transform.h"
#include "../World/Components/Renderable.h"
#include "../RHI/RHI_VertexBuffer.h"
#include "../RHI/RHI_IndexBuffer.h"
#include "../RHI/RHI_Texture2D.h"
#include "../RHI/RHI_Vertex.h"
//===========================================

//= NAMESPACES ================
using namespace std;
using namespace Spartan::Math;
//=============================

namespace Spartan
{
    Model::Model(Context* context) : IResource(context, ResourceType::Model)
    {
        m_resource_manager    = m_context->GetSubsystem<ResourceCache>();
        m_rhi_device        = m_context->GetSubsystem<Renderer>()->GetRhiDevice();
        m_mesh                = make_unique<Mesh>();
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
        m_mesh->Clear();
        m_aabb.Undefine();
        m_normalized_scale = 1.0f;
        m_is_animated = false;
    }

    bool Model::LoadFromFile(const string& file_path)
    {
        const Stopwatch timer;

        if (file_path.empty() || FileSystem::IsDirectory(file_path))
        {
            LOG_WARNING("Invalid file path");
            return false;
        }

        // Load engine format
        if (FileSystem::GetExtensionFromFilePath(file_path) == EXTENSION_MODEL)
        {
            // Deserialize
            auto file = make_unique<FileStream>(file_path, FileStream_Read);
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
            SetResourceFilePath(file_path);

            if (m_resource_manager->GetModelImporter()->Load(this, file_path))
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

        // Compute memory usage
        {
            // Cpu
            m_size_cpu = !m_mesh ? 0 : m_mesh->GetMemoryUsage();

            // Gpu
            if (m_vertex_buffer && m_index_buffer)
            {
                m_size_gpu = m_vertex_buffer->GetSizeGpu();
                m_size_gpu += m_index_buffer->GetSizeGpu();
            }
        }

        LOG_INFO("Loading \"%s\" took %d ms", FileSystem::GetFileNameFromFilePath(file_path).c_str(), static_cast<int>(timer.GetElapsedTimeMs()));

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

        file->Close();

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
        m_mesh->GetGeometry(index_offset, index_count, vertex_offset, vertex_count, indices, vertices);
    }

    void Model::UpdateGeometry()
    {
        if (m_mesh->Indices_Count() == 0 || m_mesh->Vertices_Count() == 0)
        {
            LOG_ERROR_INVALID_PARAMETER();
            return;
        }

        GeometryCreateBuffers();
        m_normalized_scale    = GeometryComputeNormalizedScale();
        m_aabb                = BoundingBox(m_mesh->Vertices_Get().data(), static_cast<uint32_t>(m_mesh->Vertices_Get().size()));
    }

    void Model::AddMaterial(shared_ptr<Material>& material, const shared_ptr<Entity>& entity) const
    {
        if (!material || !entity)
        {
            LOG_ERROR_INVALID_PARAMETER();
            return;
        }

        // Create a file path for this material
        const string spartan_asset_path = FileSystem::GetDirectoryFromFilePath(GetResourceFilePathNative()) + material->GetResourceName() + EXTENSION_MATERIAL;
        material->SetResourceFilePath(spartan_asset_path);

        // Create a Renderable and pass the material to it
        entity->AddComponent<Renderable>()->SetMaterial(material);
    }

    void Model::AddTexture(shared_ptr<Material>& material, const Material_Property texture_type, const string& file_path)
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
        const auto indices    = m_mesh->Indices_Get();
        const auto vertices    = m_mesh->Vertices_Get();

        if (!indices.empty())
        {
            m_index_buffer = make_shared<RHI_IndexBuffer>(m_rhi_device);
            if (!m_index_buffer->Create(indices))
            {
                LOG_ERROR("Failed to create index buffer for \"%s\".", GetResourceName().c_str());
                success = false;
            }
        }
        else
        {
            LOG_ERROR("Failed to create index buffer for \"%s\". Provided indices are empty", GetResourceName().c_str());
            success = false;
        }

        if (!vertices.empty())
        {
            m_vertex_buffer = make_shared<RHI_VertexBuffer>(m_rhi_device);
            if (!m_vertex_buffer->Create(vertices))
            {
                LOG_ERROR("Failed to create vertex buffer for \"%s\".", GetResourceName().c_str());
                success = false;
            }
        }
        else
        {
            LOG_ERROR("Failed to create vertex buffer for \"%s\". Provided vertices are empty", GetResourceName().c_str());
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
}
