/*
Copyright(c) 2015-2025 Panos Karabelas

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
#include "../Resource/Import/ModelImporter.h"
#include "GeometryProcessing.h"
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

    void Mesh::SaveToFile(const string& file_path)
    {
        std::ofstream outfile(file_path, std::ios::binary);
        if (!outfile)
        {
            SP_LOG_ERROR("Failed to open file for writing: %s", file_path.c_str());
            return;
        }

        uint32_t version = 1;
        outfile.write(reinterpret_cast<const char*>(&version), sizeof(uint32_t));

        uint32_t type = static_cast<uint32_t>(m_type);
        outfile.write(reinterpret_cast<const char*>(&type), sizeof(uint32_t));

        uint32_t dropoff = static_cast<uint32_t>(m_lod_dropoff);
        outfile.write(reinterpret_cast<const char*>(&dropoff), sizeof(uint32_t));

        outfile.write(reinterpret_cast<const char*>(&m_flags), sizeof(uint32_t));

        uint32_t submesh_count = static_cast<uint32_t>(m_sub_meshes.size());
        outfile.write(reinterpret_cast<const char*>(&submesh_count), sizeof(uint32_t));

        for (const auto& sub : m_sub_meshes)
        {
            uint32_t lod_count = static_cast<uint32_t>(sub.lods.size());
            outfile.write(reinterpret_cast<const char*>(&lod_count), sizeof(uint32_t));

            for (const auto& lod : sub.lods)
            {
                outfile.write(reinterpret_cast<const char*>(&lod.vertex_offset), sizeof(uint32_t));
                outfile.write(reinterpret_cast<const char*>(&lod.vertex_count), sizeof(uint32_t));
                outfile.write(reinterpret_cast<const char*>(&lod.index_offset), sizeof(uint32_t));
                outfile.write(reinterpret_cast<const char*>(&lod.index_count), sizeof(uint32_t));

                Vector3 min = lod.aabb.GetMin();
                Vector3 max = lod.aabb.GetMax();
                outfile.write(reinterpret_cast<const char*>(&min.x), sizeof(float));
                outfile.write(reinterpret_cast<const char*>(&min.y), sizeof(float));
                outfile.write(reinterpret_cast<const char*>(&min.z), sizeof(float));
                outfile.write(reinterpret_cast<const char*>(&max.x), sizeof(float));
                outfile.write(reinterpret_cast<const char*>(&max.y), sizeof(float));
                outfile.write(reinterpret_cast<const char*>(&max.z), sizeof(float));
            }
        }

        uint32_t vertex_count = static_cast<uint32_t>(m_vertices.size());
        outfile.write(reinterpret_cast<const char*>(&vertex_count), sizeof(uint32_t));
        outfile.write(reinterpret_cast<const char*>(m_vertices.data()), vertex_count * sizeof(RHI_Vertex_PosTexNorTan));

        uint32_t index_count = static_cast<uint32_t>(m_indices.size());
        outfile.write(reinterpret_cast<const char*>(&index_count), sizeof(uint32_t));
        outfile.write(reinterpret_cast<const char*>(m_indices.data()), index_count * sizeof(uint32_t));

        outfile.close();
    }

    void Mesh::LoadFromFile(const string& file_path)
    {
        const Stopwatch timer;
        SetResourceFilePath(file_path);

        if (FileSystem::IsSupportedModelFile(file_path)) // foreign
        {
            ModelImporter::Load(this, file_path);
        }
        else if (FileSystem::IsEngineMeshFile(file_path)) // native
        {
            std::ifstream infile(file_path, std::ios::binary);
            if (!infile)
            {
                SP_LOG_ERROR("Failed to open file: %s", file_path.c_str());
                return;
            }

            Clear();

            uint32_t version;
            infile.read(reinterpret_cast<char*>(&version), sizeof(uint32_t));
            if (version != 1)
            {
                SP_LOG_ERROR("Version mismatch for file: %s", file_path.c_str());
                return;
            }

            uint32_t type;
            infile.read(reinterpret_cast<char*>(&type), sizeof(uint32_t));
            m_type = static_cast<MeshType>(type);

            uint32_t dropoff;
            infile.read(reinterpret_cast<char*>(&dropoff), sizeof(uint32_t));
            m_lod_dropoff = static_cast<MeshLodDropoff>(dropoff);

            infile.read(reinterpret_cast<char*>(&m_flags), sizeof(uint32_t));

            uint32_t submesh_count;
            infile.read(reinterpret_cast<char*>(&submesh_count), sizeof(uint32_t));
            m_sub_meshes.resize(submesh_count);

            for (auto& sub : m_sub_meshes)
            {
                uint32_t lod_count;
                infile.read(reinterpret_cast<char*>(&lod_count), sizeof(uint32_t));
                sub.lods.resize(lod_count);

                for (auto& lod : sub.lods)
                {
                    infile.read(reinterpret_cast<char*>(&lod.vertex_offset), sizeof(uint32_t));
                    infile.read(reinterpret_cast<char*>(&lod.vertex_count), sizeof(uint32_t));
                    infile.read(reinterpret_cast<char*>(&lod.index_offset), sizeof(uint32_t));
                    infile.read(reinterpret_cast<char*>(&lod.index_count), sizeof(uint32_t));

                    float min_x, min_y, min_z, max_x, max_y, max_z;
                    infile.read(reinterpret_cast<char*>(&min_x), sizeof(float));
                    infile.read(reinterpret_cast<char*>(&min_y), sizeof(float));
                    infile.read(reinterpret_cast<char*>(&min_z), sizeof(float));
                    infile.read(reinterpret_cast<char*>(&max_x), sizeof(float));
                    infile.read(reinterpret_cast<char*>(&max_y), sizeof(float));
                    infile.read(reinterpret_cast<char*>(&max_z), sizeof(float));

                    lod.aabb = BoundingBox(Vector3(min_x, min_y, min_z), Vector3(max_x, max_y, max_z));
                }
            }

            uint32_t vertex_count;
            infile.read(reinterpret_cast<char*>(&vertex_count), sizeof(uint32_t));
            m_vertices.resize(vertex_count);
            infile.read(reinterpret_cast<char*>(m_vertices.data()), vertex_count * sizeof(RHI_Vertex_PosTexNorTan));

            uint32_t index_count;
            infile.read(reinterpret_cast<char*>(&index_count), sizeof(uint32_t));
            m_indices.resize(index_count);
            infile.read(reinterpret_cast<char*>(m_indices.data()), index_count * sizeof(uint32_t));

            infile.close();

            CreateGpuBuffers();
        }
        else
        {
            SP_LOG_ERROR("Failed to load mesh %s: format not supported", file_path.c_str());
            return;
        }

        // compute memory usage
        if (m_vertex_buffer && m_index_buffer)
        {
            m_object_size = m_vertex_buffer->GetObjectSize();
            m_object_size += m_index_buffer->GetObjectSize();
        }

        SP_LOG_INFO("Loading \"%s\" took %d ms", FileSystem::GetFileNameFromFilePath(file_path).c_str(), static_cast<int>(timer.GetElapsedTimeMs()));
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

    void Mesh::AddLod(vector<RHI_Vertex_PosTexNorTan>& vertices, vector<uint32_t>& indices, const uint32_t sub_mesh_index)
    {
        // build lod
        MeshLod lod;
        lod.vertex_offset = static_cast<uint32_t>(m_vertices.size());
        lod.vertex_count  = static_cast<uint32_t>(vertices.size());
        lod.index_offset  = static_cast<uint32_t>(m_indices.size());
        lod.index_count   = static_cast<uint32_t>(indices.size());
        lod.aabb          = BoundingBox(vertices.data(), static_cast<uint32_t>(vertices.size()));

        // append geometry
        {
            lock_guard lock(m_mutex);

            // append geometry to mesh buffers
            m_vertices.insert(m_vertices.end(), vertices.begin(), vertices.end());
            m_indices.insert(m_indices.end(), indices.begin(), indices.end());

            // add lod to the specified sub-mesh
            m_sub_meshes[sub_mesh_index].lods.push_back(lod);
        }
    }

    void Mesh::AddGeometry(vector<RHI_Vertex_PosTexNorTan>& vertices, vector<uint32_t>& indices, const bool generate_lods, uint32_t* sub_mesh_index)
    {
        // create a sub-mesh
        SubMesh sub_mesh;
        uint32_t current_sub_mesh_index = static_cast<uint32_t>(m_sub_meshes.size());
        m_sub_meshes.push_back(sub_mesh); // add it to the list so AddLod() can access it

        // lod 0: original geometry
        {
            // optimize original geometry if flagged
            if (m_flags & static_cast<uint32_t>(MeshFlags::PostProcessOptimize))
            {
                geometry_processing::optimize(vertices, indices);
            }

            // add the original geometry as lod 0
            AddLod(vertices, indices, current_sub_mesh_index);
        }

        // generate additional LODs if requested
        if (generate_lods && (m_flags & static_cast<uint32_t>(MeshFlags::PostProcessGenerateLods)))
        {
            // store the original index count (for reference, but we'll base targets on previous LOD)
            size_t original_index_count = indices.size();
            
            // start with the original geometry for LOD 1 onwards
            vector<RHI_Vertex_PosTexNorTan> prev_vertices = vertices;
            vector<uint32_t> prev_indices                 = indices;
            
            for (uint32_t lod_level = 1; lod_level < mesh_lod_count; lod_level++)
            {
                // use the previous LOD's geometry for simplification
                vector<RHI_Vertex_PosTexNorTan> lod_vertices = prev_vertices;
                vector<uint32_t> lod_indices                 = prev_indices;
            
                // only simplify if the geometry is complex enough
                if (lod_indices.size() > 64)
                {
                    // compute target fraction based on LOD level
                    float t = static_cast<float>(lod_level) / static_cast<float>(mesh_lod_count);
                    if (m_lod_dropoff == MeshLodDropoff::Exponential)
                    {
                        t = pow(t, 2.0f);
                    }
                    float target_fraction = 1.0f - t;
                    
                    // compute target index count based on the previous LOD's actual index count
                    size_t target_index_count = max(static_cast<size_t>(3), static_cast<size_t>(prev_indices.size() * target_fraction));
            
                    // simplify geometry
                    bool preserve_uvs   = true;
                    bool preserve_edges = m_flags & static_cast<uint32_t>(MeshFlags::PostProcessPreserveTerrainEdges);
                    geometry_processing::simplify(lod_indices, lod_vertices, target_index_count, preserve_uvs, preserve_edges);
            
                    // check if simplification reduced the index count; if not, stop
                    if (lod_indices.size() >= prev_indices.size())
                        break;
            
                    // Add the simplified geometry as a new LOD
                    AddLod(lod_vertices, lod_indices, current_sub_mesh_index);
            
                    // Update previous geometry for the next iteration
                    prev_vertices = move(lod_vertices);
                    prev_indices  = move(lod_indices);
                }
                else
                {
                    // If too simple to simplify further, stop generating LODs
                    break;
                }
            }
        }

        // return the sub-mesh index if requested
        if (sub_mesh_index)
        {
            *sub_mesh_index = current_sub_mesh_index;
        }
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
            static_cast<uint32_t>(MeshFlags::PostProcessOptimize)       |
            static_cast<uint32_t>(MeshFlags::PostProcessGenerateLods);
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
