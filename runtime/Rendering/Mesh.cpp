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
    namespace
    {
        bool is_solid(Mesh& mesh, uint32_t sub_mesh_index)
        {
            const MeshLod& lod = mesh.GetSubMesh(sub_mesh_index).lods[0];
            BoundingBox aabb   = lod.aabb;
            Vector3 min        = aabb.GetMin();
            Vector3 max        = aabb.GetMax();
            Vector3 center     = aabb.GetCenter();
    
            // Define ray start points (face centers) and their opposite targets
            static array<Vector3, 6> start_points =
            {
                Vector3(min.x, center.y, center.z), // Left face
                Vector3(max.x, center.y, center.z), // Right face
                Vector3(center.x, min.y, center.z), // Bottom face
                Vector3(center.x, max.y, center.z), // Top face
                Vector3(center.x, center.y, min.z), // Front face
                Vector3(center.x, center.y, max.z)  // Back face
            };
    
            static array<Vector3, 6> end_points =
            {
                Vector3(max.x, center.y, center.z), // Opposite of left (right face)
                Vector3(min.x, center.y, center.z), // Opposite of right (left face)
                Vector3(center.x, max.y, center.z), // Opposite of bottom (top face)
                Vector3(center.x, min.y, center.z), // Opposite of top (bottom face)
                Vector3(center.x, center.y, max.z), // Opposite of front (back face)
                Vector3(center.x, center.y, min.z)  // Opposite of back (front face)
            };
    
            // Offset start points slightly outward to avoid starting on the mesh surface
            static array<Vector3, 6> normals =
            {
                Vector3::Left,     Vector3::Right,
                Vector3::Down,     Vector3::Up,
                Vector3::Backward, Vector3::Forward
            };
    
            for (size_t i = 0; i < start_points.size(); ++i)
            {
                start_points[i] += normals[i] * 0.1f; // small offset to avoid surface
            }
    
            // get geometry
            vector<uint32_t> indices;
            vector<RHI_Vertex_PosTexNorTan> vertices;
            mesh.GetGeometry(sub_mesh_index, &indices, &vertices);
    
            // shoot rays
            uint32_t intersect_count = 0;
            for (size_t i = 0; i < start_points.size(); ++i)
            {
                Vector3 direction = end_points[i] - start_points[i];
                float distance_to_opposite = direction.Length();
                direction.Normalize();
    
                Ray ray(start_points[i], direction);
    
                bool intersects = false;
                for (size_t j = 0; j < indices.size(); j += 3)
                {
                    const Vector3& v0 = vertices[indices[j + 0]].pos;
                    const Vector3& v1 = vertices[indices[j + 1]].pos;
                    const Vector3& v2 = vertices[indices[j + 2]].pos;
    
                    float hit_distance = ray.HitDistance(v0, v1, v2);
                    if (hit_distance != numeric_limits<float>::infinity() && hit_distance <= distance_to_opposite)
                    {
                        intersects = true;
                        break;
                    }
                }
    
                if (intersects)
                {
                    intersect_count++;
                }
            }
    
            // threshold: At least 4 rays must intersect for the mesh to be considered solid
            return intersect_count >= 4;
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

            // determine if it's solid
            m_sub_meshes[current_sub_mesh_index].is_solid = is_solid(*this, current_sub_mesh_index);
        }
    
        // generate additional LODs if requested
        if (generate_lods && !(m_flags & static_cast<uint32_t>(MeshFlags::PostProcessDontGenerateLods)))
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
                    bool preserve_edges = m_flags & static_cast<uint32_t>(MeshFlags::PostProcessPreserveTerrainEdges);
                    geometry_processing::simplify(lod_indices, lod_vertices, target_index_count, preserve_edges);
            
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
