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
#include "../World/Components/Renderable.h"
#include "../World/Entity.h"
#include "../Resource/ResourceCache.h"
#include "../IO/FileStream.h"
#include "../Resource/Import/ModelImporter.h"
SP_WARNINGS_OFF
#include "meshoptimizer/meshoptimizer.h"
SP_WARNINGS_ON
//===========================================

//= NAMESPACES ================
using namespace std;
using namespace Spartan::Math;
//=============================

namespace Spartan
{
    namespace meshoptimizer
    {
        // documentation: https://meshoptimizer.org/

        void simplify(vector<uint32_t>& indices, const vector<RHI_Vertex_PosTexNorTan>& vertices, size_t index_count, size_t vertex_target)
        {
            float reduction = 0.2f;
            float error     = 0.01f;

            size_t current_vertex_count = indices.size() / 3;
            vector<uint32_t> indices_simplified(indices.size());

            // loop until the current vertex count is less than or equal to the target vertex count
            while (current_vertex_count > vertex_target)
            {
                float threshold           = 1.0f - reduction;
                size_t target_index_count = static_cast<size_t>(index_count * threshold);
        
                index_count = meshopt_simplify(indices_simplified.data(), indices.data(), index_count,
                              &vertices[0].pos[0], static_cast<uint32_t>(vertices.size()), sizeof(RHI_Vertex_PosTexNorTan),
                              target_index_count, error);
        
                indices = indices_simplified;

                // if meshoptimizer taps out, break
                uint32_t vertex_count_new = index_count / 3;
                if (current_vertex_count == vertex_count_new)
                    break;

                current_vertex_count  = vertex_count_new;
                error                += 0.01f;
            }
        }

        void optimize(vector<RHI_Vertex_PosTexNorTan>& vertices, vector<uint32_t>& indices)
        {
            size_t vertex_count = vertices.size();
            size_t index_count  = indices.size();

            // create a remap table
            vector<unsigned int> remap(index_count);
            size_t vertex_count_optimized = meshopt_generateVertexRemap(remap.data(), 
                                                                indices.data(),
                                                                index_count,
                                                                vertices.data(),
                                                                vertex_count,
                                                                sizeof(RHI_Vertex_PosTexNorTan));

            // note: when we import with Assimp, JoinIdenticalVertices is used, so we don't need to remove duplicates here

            // optimization #1: improve the locality of the vertices
            meshopt_optimizeVertexCache(indices.data(), indices.data(), index_count, vertex_count);
        
            // optimization #2: reduce pixel overdraw
            meshopt_optimizeOverdraw(indices.data(), indices.data(), index_count, &(vertices[0].pos[0]), vertex_count, sizeof(RHI_Vertex_PosTexNorTan), 1.05f);
        
            // optimization #3: optimize access to the vertex buffer
            meshopt_optimizeVertexFetch(vertices.data(), indices.data(), index_count, vertices.data(), vertex_count, sizeof(RHI_Vertex_PosTexNorTan));
        
            // optimization #4: create a simplified version of the model
            {
                auto get_vertex_target = [](uint32_t vertex_count)
                {
                    tuple<float, uint32_t> agressivness_table[] =
                    {
                        { 0.3f, 30000 }, // ultra agressive
                        { 0.5f, 20000 }, // agressive
                        { 0.7f, 15000 }, // balanced
                        { 0.9f, 5000  }  // gentle
                    };
                
                    for (const auto& [reduction_percentage, vertex_threshold] : agressivness_table)
                    {
                        if (vertex_count > vertex_threshold)
                        {
                            return static_cast<uint32_t>(vertex_count * reduction_percentage);
                        }
                    }
                    return vertex_count; // native
                };
                
                simplify(indices, vertices, index_count, get_vertex_target(vertex_count));
            }
        }
    }

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

            PostProcess();
        }
        // load foreign format
        else
        {
            SetResourceFilePath(file_path);

            if (!ModelImporter::Load(this, file_path))
                return;
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
        uint32_t size = 0;
        size += uint32_t(m_indices.size()  * sizeof(uint32_t));
        size += uint32_t(m_vertices.size() * sizeof(RHI_Vertex_PosTexNorTan));

        return size;
    }

    void Mesh::GetGeometry(uint32_t index_offset, uint32_t index_count, uint32_t vertex_offset, uint32_t vertex_count, vector<uint32_t>* indices, vector<RHI_Vertex_PosTexNorTan>* vertices)
    {
        SP_ASSERT_MSG(indices != nullptr || vertices != nullptr, "Indices and vertices vectors can't both be null");

        if (indices)
        {
            SP_ASSERT_MSG(index_count != 0, "Index count can't be 0");

            const auto index_first = m_indices.begin() + index_offset;
            const auto index_last  = m_indices.begin() + index_offset + index_count;
            *indices               = vector<uint32_t>(index_first, index_last);
        }

        if (vertices)
        {
            SP_ASSERT_MSG(vertex_count != 0, "Index count can't be 0");

            const auto vertex_first = m_vertices.begin() + vertex_offset;
            const auto vertex_last  = m_vertices.begin() + vertex_offset + vertex_count;
            *vertices               = vector<RHI_Vertex_PosTexNorTan>(vertex_first, vertex_last);
        }
    }

   void Mesh::AddGeometry(vector<RHI_Vertex_PosTexNorTan>& vertices, vector<uint32_t>& indices, uint32_t* vertex_offset_out, uint32_t* index_offset_out)
    {
        if (m_flags & static_cast<uint32_t>(MeshFlags::PostProcessOptimize))
        {
            meshoptimizer::optimize(vertices, indices);
        }

        lock_guard lock(m_mutex);
    
        // set vertex offset if requested
        if (vertex_offset_out)
        {
            *vertex_offset_out = static_cast<uint32_t>(m_vertices.size());
        }
    
        // set index offset if requested
        if (index_offset_out)
        {
            *index_offset_out = static_cast<uint32_t>(m_indices.size());
        }
    
        // add
        m_vertices.insert(m_vertices.end(), vertices.begin(), vertices.end());
        m_indices.insert(m_indices.end(), indices.begin(), indices.end());
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
            //static_cast<uint32_t>(MeshFlags::ImportLights)              |
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
    }

    void Mesh::PostProcess()
    {
        m_aabb = BoundingBox(m_vertices.data(), static_cast<uint32_t>(m_vertices.size()));

        // normalize scale
        if (m_flags & static_cast<uint32_t>(MeshFlags::PostProcessNormalizeScale))
        {
            if (shared_ptr<Entity> root_entity = m_root_entity.lock())
            {
                float scale_offset     = m_aabb.GetExtents().Length();
                float normalized_scale = 1.0f / scale_offset;

                root_entity->SetScale(normalized_scale);
            }
        }

        CreateGpuBuffers();
    }

    void Mesh::SetMaterial(shared_ptr<Material>& material, Entity* entity) const
    {
        SP_ASSERT(material != nullptr);
        SP_ASSERT(entity != nullptr);

        // create a file path for this material (required for the material to be able to be cached by the resource cache)
        const string spartan_asset_path = FileSystem::GetDirectoryFromFilePath(GetResourceFilePath()) + material->GetObjectName() + EXTENSION_MATERIAL;
        material->SetResourceFilePath(spartan_asset_path);

        // create a Renderable and pass the material to it
        entity->AddComponent<Renderable>()->SetMaterial(material);
    }
}
