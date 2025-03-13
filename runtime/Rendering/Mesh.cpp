/*
Copyright(c) 2016-2025 Panos Karabelas

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
#include "pch.h"
#include "Mesh.h"
#include "../RHI/RHI_Buffer.h"
#include "../World/Entity.h"
#include "../IO/FileStream.h"
#include "../Resource/Import/ModelImporter.h"
#include "../Geometry/GeometryProcessing.h"
//===========================================

//= NAMESPACES ================
using namespace std;
using namespace spartan::math;
//=============================

namespace spartan
{
    Mesh::Mesh() : IResource(ResourceType::Mesh)
    {
        m_flags = GetDefaultFlags();
    }

    Mesh::~Mesh()
    {
        m_index_buffer  = nullptr;
        m_vertex_buffer = nullptr;
    }

    void Mesh::Clear()
    {
        m_indices.clear();
        m_indices.shrink_to_fit();

        m_vertices.clear();
        m_vertices.shrink_to_fit();
    }

    void Mesh::LoadFromFile(const string& file_path)
    {
        const Stopwatch timer;

        if (file_path.empty() || FileSystem::IsDirectory(file_path))
        {
            SP_LOG_WARNING("Invalid file path");
            return;
        }

        // load engine format
        if (FileSystem::GetExtensionFromFilePath(file_path) == EXTENSION_MODEL)
        {
            // deserialize
            auto file = make_unique<FileStream>(file_path, FileStream_Read);
            if (!file->IsOpen())
                return;

            SetResourceFilePath(file->ReadAs<string>());
            file->Read(&m_indices);
            file->Read(&m_vertices);

            CreateGpuBuffers();
        }
        // load foreign format
        else
        {
            SetResourceFilePath(file_path);
            ModelImporter::Load(this, file_path);
        }

        // compute memory usage
        {
            if (m_vertex_buffer && m_index_buffer)
            {
                m_object_size = m_vertex_buffer->GetObjectSize();
                m_object_size += m_index_buffer->GetObjectSize();
            }
        }

        SP_LOG_INFO("Loading \"%s\" took %d ms", FileSystem::GetFileNameFromFilePath(file_path).c_str(), static_cast<int>(timer.GetElapsedTimeMs()));
    }

    void Mesh::SaveToFile(const string& file_path)
    {
        auto file = make_unique<FileStream>(file_path, FileStream_Write);
        if (!file->IsOpen())
            return;

        file->Write(GetResourceFilePath());
        file->Write(m_indices);
        file->Write(m_vertices);

        file->Close();
    }

    uint32_t Mesh::GetMemoryUsage() const
    {
        uint32_t size  = 0;
        size          += uint32_t(m_indices.size()  * sizeof(uint32_t));
        size          += uint32_t(m_vertices.size() * sizeof(RHI_Vertex_PosTexNorTan));

        return size;
    }

    void Mesh::GetGeometry(uint32_t sub_mesh_index, vector<uint32_t>* indices, vector<RHI_Vertex_PosTexNorTan>* vertices)
    {
        SP_ASSERT_MSG(indices != nullptr || vertices != nullptr, "Indices and vertices vectors can't both be null");

        const MeshLod& lod = GetSubMesh(sub_mesh_index).lods[0];

        if (indices)
        {
            SP_ASSERT_MSG(lod.index_count != 0, "Index count can't be 0");

            const auto index_first = m_indices.begin() + lod.index_offset;
            const auto index_last  = m_indices.begin() + lod.index_offset + lod.index_count;
            *indices               = vector<uint32_t>(index_first, index_last);
        }

        if (vertices)
        {
            SP_ASSERT_MSG(lod.vertex_count != 0, "Index count can't be 0");

            const auto vertex_first = m_vertices.begin() + lod.vertex_offset;
            const auto vertex_last  = m_vertices.begin() + lod.vertex_offset + lod.vertex_count;
            *vertices               = vector<RHI_Vertex_PosTexNorTan>(vertex_first, vertex_last);
        }
    }

    void Mesh::AddGeometry(vector<RHI_Vertex_PosTexNorTan>& vertices, vector<uint32_t>& indices, uint32_t* sub_mesh_index)
    {
        lock_guard lock(m_mutex);
    
        // optimize original geometry
        if (m_flags & static_cast<uint32_t>(MeshFlags::PostProcessOptimize))
        {
            geometry_processing::optimize(vertices, indices);
        }
    
        // create a sub-mesh
        SubMesh sub_mesh;
    
        // lod 0: Original geometry
        MeshLod lod_0;
        lod_0.vertex_offset = static_cast<uint32_t>(m_vertices.size());
        lod_0.vertex_count  = static_cast<uint32_t>(vertices.size());
        lod_0.index_offset  = static_cast<uint32_t>(m_indices.size());
        lod_0.index_count   = static_cast<uint32_t>(indices.size());
        sub_mesh.lods.push_back(lod_0);
        m_vertices.insert(m_vertices.end(), vertices.begin(), vertices.end());
        m_indices.insert(m_indices.end(), indices.begin(), indices.end());
    
        // generate additional lods
        {
            static const uint32_t k_lod_count = 3;
            
            // start with the original geometry for lod 0
            vector<RHI_Vertex_PosTexNorTan> prev_vertices = vertices;
            vector<uint32_t> prev_indices                 = indices;
            
            for (uint32_t lod_level = 1; lod_level < k_lod_count; ++lod_level)
            {
                // use the previous lod's geometry for simplification
                vector<RHI_Vertex_PosTexNorTan> lod_vertices = prev_vertices;
                vector<uint32_t> lod_indices                = prev_indices;
                
                // calculate target triangle count (reduce by 50% from the previous lod)
                size_t prev_triangle_count   = prev_indices.size() / 3;
                size_t target_triangle_count = std::max(static_cast<size_t>(1), static_cast<size_t>(prev_triangle_count * 0.5f)); // 50% reduction from the previous lod
            
                // simplify indices based on the previous lod
                geometry_processing::simplify(lod_indices, lod_vertices, target_triangle_count);

                if (shared_ptr<Entity> entity = m_root_entity.lock())
                {
                    if (prev_triangle_count == (lod_indices.size() / 3))
                    {
                        SP_LOG_WARNING("Failed to create lod for %s", entity->GetObjectName().c_str());
                    }
                }

                // adjust vertex count based on simplified indices (assuming simplify keeps vertex order)
                MeshLod lod;
                lod.vertex_offset = static_cast<uint32_t>(m_vertices.size());
                lod.vertex_count  = static_cast<uint32_t>(lod_vertices.size());
                lod.index_offset  = static_cast<uint32_t>(m_indices.size());
                lod.index_count   = static_cast<uint32_t>(lod_indices.size());
                sub_mesh.lods.push_back(lod);
                
                // append simplified geometry
                m_vertices.insert(m_vertices.end(), lod_vertices.begin(), lod_vertices.end());
                m_indices.insert(m_indices.end(), lod_indices.begin(), lod_indices.end());
                
                // update previous geometry for the next iteration
                prev_vertices = std::move(lod_vertices);
                prev_indices  = std::move(lod_indices);
            }
        }
    
        // store the sub-mesh and return its index
        if (sub_mesh_index)
        {
            *sub_mesh_index = static_cast<uint32_t>(m_sub_meshes.size());
        }
        m_sub_meshes.push_back(sub_mesh);
    }

    uint32_t Mesh::GetVertexCount() const
    {
        return static_cast<uint32_t>(m_vertices.size());
    }

    uint32_t Mesh::GetIndexCount() const
    {
        return static_cast<uint32_t>(m_indices.size());
    }

    uint32_t Mesh::GetDefaultFlags()
    {
        return
            static_cast<uint32_t>(MeshFlags::ImportRemoveRedundantData) |
            static_cast<uint32_t>(MeshFlags::PostProcessNormalizeScale) |
            static_cast<uint32_t>(MeshFlags::PostProcessOptimize);
    }

    void Mesh::CreateGpuBuffers()
    {
        m_vertex_buffer = make_shared<RHI_Buffer>(RHI_Buffer_Type::Vertex,
            sizeof(m_vertices[0]),
            static_cast<uint32_t>(m_vertices.size()),
            static_cast<void*>(&m_vertices[0]),
            false,
            (string("mesh_vertex_buffer_") + m_object_name).c_str()
        );

        m_index_buffer = make_shared<RHI_Buffer>(RHI_Buffer_Type::Index,
            sizeof(m_indices[0]),
            static_cast<uint32_t>(m_indices.size()),
            static_cast<void*>(&m_indices[0]),
            false,
            (string("mesh_index_buffer_") + m_object_name).c_str()
        );

        // normalize scale
        if (m_flags & static_cast<uint32_t>(MeshFlags::PostProcessNormalizeScale))
        {
            if (shared_ptr<Entity> entity = m_root_entity.lock())
            {
                BoundingBox bounding_box(m_vertices.data(), static_cast<uint32_t>(m_vertices.size()));
                float scale_offset     = bounding_box.GetExtents().Length();
                float normalized_scale = 1.0f / scale_offset;
                entity->SetScale(normalized_scale);
            }
        }
    }
}
