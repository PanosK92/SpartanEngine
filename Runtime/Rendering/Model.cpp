/*
Copyright(c) 2016-2022 Panos Karabelas

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
#include "Runtime/Core/Spartan.h"
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
        m_resource_manager = m_context->GetSubsystem<ResourceCache>();
        m_rhi_device       = m_context->GetSubsystem<Renderer>()->GetRhiDevice();
        m_mesh             = make_unique<Mesh>();
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
            }
            else
            {
                return false;
            }
        }

        // Compute memory usage
        {
            // Cpu
            m_object_size_cpu = !m_mesh ? 0 : m_mesh->GetMemoryUsage();

            // Gpu
            if (m_vertex_buffer && m_index_buffer)
            {
                m_object_size_gpu = m_vertex_buffer->GetObjectSizeGpu();
                m_object_size_gpu += m_index_buffer->GetObjectSizeGpu();
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
        SP_ASSERT(!indices.empty());
        SP_ASSERT(!vertices.empty());

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
        SP_ASSERT(m_mesh->Indices_Count() != 0);
        SP_ASSERT(m_mesh->Vertices_Count() != 0);

        GeometryCreateBuffers();
        m_normalized_scale = GeometryComputeNormalizedScale();
        m_aabb             = BoundingBox(m_mesh->Vertices_Get().data(), static_cast<uint32_t>(m_mesh->Vertices_Get().size()));
    }

    void Model::AddMaterial(shared_ptr<Material>& material, const shared_ptr<Entity>& entity) const
    {
        SP_ASSERT(material != nullptr);
        SP_ASSERT(entity != nullptr);

        // Create a file path for this material
        const string spartan_asset_path = FileSystem::GetDirectoryFromFilePath(GetResourceFilePathNative()) + material->GetResourceName() + EXTENSION_MATERIAL;
        material->SetResourceFilePath(spartan_asset_path);

        // Create a Renderable and pass the material to it
        entity->AddComponent<Renderable>()->SetMaterial(material);
    }

    void Model::AddTexture(shared_ptr<Material>& material, const Material_Property texture_type, const string& file_path)
    {
        SP_ASSERT(material != nullptr);
        SP_ASSERT(!file_path.empty());

        // Try to get the texture
        const auto tex_name = FileSystem::GetFileNameWithoutExtensionFromFilePath(file_path);
        shared_ptr<RHI_Texture> texture = m_context->GetSubsystem<ResourceCache>()->GetByName<RHI_Texture2D>(tex_name);

        if (texture)
        {
            material->SetTextureSlot(texture_type, texture);
        }
        else // If we didn't get a texture, it's not cached, hence we have to load it and cache it now
        {
            // Load texture
            texture = make_shared<RHI_Texture2D>(m_context, RHI_Texture_Srv | RHI_Texture_Mips | RHI_Texture_PerMipViews | RHI_Texture_Compressed);
            texture->LoadFromFile(file_path);

            // Set the texture to the provided material
            material->SetTextureSlot(texture_type, texture);
        }
    }

    bool Model::GeometryCreateBuffers()
    {
        auto success = true;

        // Get geometry
        const auto indices  = m_mesh->Indices_Get();
        const auto vertices = m_mesh->Vertices_Get();

        if (!indices.empty())
        {
            m_index_buffer = make_shared<RHI_IndexBuffer>(m_rhi_device, false, "model");
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
            m_vertex_buffer = make_shared<RHI_VertexBuffer>(m_rhi_device, false, "model");
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
