/*
Copyright(c) 2015-2026 Panos Karabelas

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
#include <fstream>
#include "Mesh.h"
#include "../RHI/RHI_Buffer.h"
#include "../RHI/RHI_Device.h"
#include "../RHI/RHI_AccelerationStructure.h"
#include "../World/Entity.h"
#include "../Resource/Import/ModelImporter.h"
#include "../Rendering/GeometryBuffer.h"
#include "GeometryProcessing.h"
SP_WARNINGS_OFF
#include <sol/sol.hpp>
SP_WARNINGS_ON
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

    }

    void Mesh::RegisterForScripting(sol::state_view State)
    {
        State.new_usertype<Mesh>("Mesh",
            "SaveToFile",               &Mesh::SaveToFile,
            "LoadFromFile",             &Mesh::LoadFromFile,
            "Clear",                    &Mesh::Clear,
            "GetVertexCount",           &Mesh::GetVertexCount,
            "GetIndexCount",            &Mesh::GetIndexCount
            );
    }

    void Mesh::Clear()
    {
        m_ready_for_blas = false;

        m_indices.clear();
        m_indices.shrink_to_fit();

        m_vertices.clear();
        m_vertices.shrink_to_fit();

        m_meshlets.clear();
        m_meshlets.shrink_to_fit();

        m_sub_meshes.clear();
        m_sub_meshes.shrink_to_fit();
    }

    void Mesh::SaveToFile(const string& file_path)
    {
        ofstream outfile(file_path, ios::binary);
        if (!outfile)
        {
            SP_LOG_ERROR("Failed to open file for writing: %s", file_path.c_str());
            return;
        }

        uint32_t version = 5; // meshlet bounds compressed to 16 bytes (center/radius quantized into the lod aabb, first_index/triangle_count packed)
        outfile.write(reinterpret_cast<const char*>(&version), sizeof(uint32_t));

        uint32_t type = static_cast<uint32_t>(m_type);
        outfile.write(reinterpret_cast<const char*>(&type), sizeof(uint32_t));

        // legacy field for backward compatibility (previously stored lod curve type)
        uint32_t legacy_field = 0;
        outfile.write(reinterpret_cast<const char*>(&legacy_field), sizeof(uint32_t));

        outfile.write(reinterpret_cast<const char*>(&m_flags), sizeof(uint32_t));

        uint32_t submesh_count = static_cast<uint32_t>(m_sub_meshes.size());
        outfile.write(reinterpret_cast<const char*>(&submesh_count), sizeof(uint32_t));

        for (uint32_t sub_idx = 0; sub_idx < submesh_count; sub_idx++)
        {
            const SubMesh& sub = m_sub_meshes[sub_idx];
            uint32_t lod_count = static_cast<uint32_t>(sub.lods.size());
            outfile.write(reinterpret_cast<const char*>(&lod_count), sizeof(uint32_t));
            SP_LOG_INFO("Mesh '%s' sub-mesh %u: saving %u LODs", m_object_name.c_str(), sub_idx, lod_count);

            for (const auto& lod : sub.lods)
            {
                outfile.write(reinterpret_cast<const char*>(&lod.vertex_offset), sizeof(uint32_t));
                outfile.write(reinterpret_cast<const char*>(&lod.vertex_count), sizeof(uint32_t));
                outfile.write(reinterpret_cast<const char*>(&lod.index_offset), sizeof(uint32_t));
                outfile.write(reinterpret_cast<const char*>(&lod.index_count), sizeof(uint32_t));
                outfile.write(reinterpret_cast<const char*>(&lod.meshlet_offset), sizeof(uint32_t));
                outfile.write(reinterpret_cast<const char*>(&lod.meshlet_count), sizeof(uint32_t));

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

        uint32_t meshlet_count = static_cast<uint32_t>(m_meshlets.size());
        outfile.write(reinterpret_cast<const char*>(&meshlet_count), sizeof(uint32_t));
        outfile.write(reinterpret_cast<const char*>(m_meshlets.data()), meshlet_count * sizeof(Sb_MeshletBounds));

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
            ifstream infile(file_path, ios::binary);
            if (!infile)
            {
                SP_LOG_ERROR("Failed to open file: %s", file_path.c_str());
                return;
            }

            Clear();

            uint32_t version;
            infile.read(reinterpret_cast<char*>(&version), sizeof(uint32_t));
            if (version != 5)
            {
                SP_LOG_ERROR("Version mismatch for file: %s (expected 5, got %u, please re-import the source asset)", file_path.c_str(), version);
                return;
            }

            uint32_t type;
            infile.read(reinterpret_cast<char*>(&type), sizeof(uint32_t));
            m_type = static_cast<MeshType>(type);

            // legacy field for backward compatibility (skip)
            uint32_t legacy_field;
            infile.read(reinterpret_cast<char*>(&legacy_field), sizeof(uint32_t));

            infile.read(reinterpret_cast<char*>(&m_flags), sizeof(uint32_t));

            uint32_t submesh_count;
            infile.read(reinterpret_cast<char*>(&submesh_count), sizeof(uint32_t));
            m_sub_meshes.resize(submesh_count);

            for (uint32_t sub_idx = 0; sub_idx < submesh_count; sub_idx++)
            {
                SubMesh& sub = m_sub_meshes[sub_idx];
                uint32_t lod_count;
                infile.read(reinterpret_cast<char*>(&lod_count), sizeof(uint32_t));
                sub.lods.resize(lod_count);
                SP_LOG_INFO("Mesh '%s' sub-mesh %u: loaded %u LODs", m_object_name.c_str(), sub_idx, lod_count);

                for (auto& lod : sub.lods)
                {
                    infile.read(reinterpret_cast<char*>(&lod.vertex_offset), sizeof(uint32_t));
                    infile.read(reinterpret_cast<char*>(&lod.vertex_count), sizeof(uint32_t));
                    infile.read(reinterpret_cast<char*>(&lod.index_offset), sizeof(uint32_t));
                    infile.read(reinterpret_cast<char*>(&lod.index_count), sizeof(uint32_t));
                    infile.read(reinterpret_cast<char*>(&lod.meshlet_offset), sizeof(uint32_t));
                    infile.read(reinterpret_cast<char*>(&lod.meshlet_count), sizeof(uint32_t));

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

            uint32_t meshlet_count;
            infile.read(reinterpret_cast<char*>(&meshlet_count), sizeof(uint32_t));
            m_meshlets.resize(meshlet_count);
            infile.read(reinterpret_cast<char*>(m_meshlets.data()), meshlet_count * sizeof(Sb_MeshletBounds));

            infile.close();

            CreateGpuBuffers();
        }
        else
        {
            SP_LOG_ERROR("Failed to load mesh %s: format not supported", file_path.c_str());
            return;
        }

        // compute memory usage
        m_object_size  = m_vertices.size() * sizeof(RHI_Vertex_PosTexNorTan);
        m_object_size += m_indices.size() * sizeof(uint32_t);

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

        // lock for the duration of the read so concurrent AddGeometry/AddLod calls cannot
        // reallocate m_vertices/m_indices and invalidate the iterators we are reading from
        lock_guard lock(m_mutex);

        // validate sub-mesh index
        if (sub_mesh_index >= m_sub_meshes.size())
        {
            SP_LOG_ERROR("GetGeometry: sub_mesh_index %u out of bounds (mesh has %zu sub-meshes)", sub_mesh_index, m_sub_meshes.size());
            return;
        }

        const SubMesh& sub_mesh = m_sub_meshes[sub_mesh_index];
        if (sub_mesh.lods.empty())
        {
            SP_LOG_ERROR("GetGeometry: sub-mesh %u has no LODs", sub_mesh_index);
            return;
        }

        const MeshLod& lod = sub_mesh.lods[0];

        if (indices)
        {
            SP_ASSERT_MSG(lod.index_count != 0, "Index count can't be 0");
            SP_ASSERT_MSG(static_cast<size_t>(lod.index_offset) + lod.index_count <= m_indices.size(), "Index range out of bounds");

            indices->resize(lod.index_count); // allocate once (caller can reuse buffer)
            copy(m_indices.begin() + lod.index_offset,
                      m_indices.begin() + lod.index_offset + lod.index_count,
                      indices->begin());
        }

        if (vertices)
        {
            SP_ASSERT_MSG(lod.vertex_count != 0, "Vertex count can't be 0");
            SP_ASSERT_MSG(static_cast<size_t>(lod.vertex_offset) + lod.vertex_count <= m_vertices.size(), "Vertex range out of bounds");

            vertices->resize(lod.vertex_count); // allocate once (caller can reuse buffer)
            copy(m_vertices.begin() + lod.vertex_offset,
                      m_vertices.begin() + lod.vertex_offset + lod.vertex_count,
                      vertices->begin());
        }
    }

    void Mesh::AddLod(vector<RHI_Vertex_PosTexNorTan>& vertices, vector<uint32_t>& indices, const uint32_t sub_mesh_index)
    {
        // build per-lod meshlets, this also repacks indices into meshlet-contiguous order and returns the lod aabb the meshlet bounds were quantized against
        // heavy work, kept outside the mesh mutex so concurrent AddLod calls run in parallel
        vector<Sb_MeshletBounds> lod_meshlets;
        BoundingBox              lod_aabb;
        geometry_processing::build_meshlets(vertices, indices, lod_meshlets, lod_aabb);

        MeshLod lod;
        lod.vertex_count  = static_cast<uint32_t>(vertices.size());
        lod.index_count   = static_cast<uint32_t>(indices.size());
        lod.aabb          = lod_aabb;
        lod.meshlet_count = static_cast<uint32_t>(lod_meshlets.size());

        // append geometry, offsets are computed inside the lock so concurrent appends produce correct values
        {
            lock_guard lock(m_mutex);

            lod.vertex_offset  = static_cast<uint32_t>(m_vertices.size());
            lod.index_offset   = static_cast<uint32_t>(m_indices.size());
            lod.meshlet_offset = static_cast<uint32_t>(m_meshlets.size());

            m_vertices.insert(m_vertices.end(), vertices.begin(), vertices.end());
            m_indices.insert(m_indices.end(), indices.begin(), indices.end());
            m_meshlets.insert(m_meshlets.end(), lod_meshlets.begin(), lod_meshlets.end());

            m_sub_meshes[sub_mesh_index].lods.push_back(lod);
        }
    }

    void Mesh::AddGeometry(vector<RHI_Vertex_PosTexNorTan>& vertices, vector<uint32_t>& indices, const bool generate_lods, uint32_t* sub_mesh_index)
    {
        // create a sub-mesh slot, locked because concurrent AddGeometry calls share m_sub_meshes
        uint32_t current_sub_mesh_index;
        {
            lock_guard lock(m_mutex);
            current_sub_mesh_index = static_cast<uint32_t>(m_sub_meshes.size());
            m_sub_meshes.emplace_back();
        }

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

        // generate additional lods if requested
        if (generate_lods && (m_flags & static_cast<uint32_t>(MeshFlags::PostProcessGenerateLods)))
        {
            // screen coverage thresholds from renderable::update_lod_indices()
            // these define at what screen fraction each lod becomes active
            // lod generation targets are derived directly from these to ensure
            // simplification is matched to runtime selection - the user should never
            // see low quality geometry up close, yet we render minimum triangles
            static constexpr array<float, mesh_lod_count> screen_thresholds =
            {
                0.05f,   // lod0: object covers >= 5% of screen height
                0.025f,  // lod1: object covers >= 2.5%
                0.012f,  // lod2: object covers >= 1.2%
                0.006f,  // lod3: object covers >= 0.6%
                0.003f   // lod4: object covers >= 0.3%
            };

            size_t original_index_count = indices.size();

            // start with lod0 geometry for progressive simplification
            vector<RHI_Vertex_PosTexNorTan> prev_vertices = vertices;
            vector<uint32_t> prev_indices                 = indices;

            for (uint32_t lod_level = 1; lod_level < mesh_lod_count; lod_level++)
            {
                // use previous lod as starting point for simplification
                vector<RHI_Vertex_PosTexNorTan> lod_vertices = prev_vertices;
                vector<uint32_t> lod_indices                 = prev_indices;

                // geometry too simple to benefit from further simplification
                if (lod_indices.size() <= 64)
                {
                    break;
                }

                // compute optimal simplification target from screen coverage ratio
                // since visible detail scales with screen coverage, and we want
                // imperceptible quality loss, we use: target = coverage / base_coverage
                // this gives ~2x reduction per lod, matching the ~2x screen size steps
                float coverage      = screen_thresholds[lod_level];
                float base_coverage = screen_thresholds[0];
                float target_ratio  = coverage / base_coverage;

                // apply target relative to original mesh (not previous lod)
                // this ensures consistent quality targets regardless of actual achieved reduction
                size_t target_index_count = max(static_cast<size_t>(64), static_cast<size_t>(original_index_count * target_ratio));

                // simplify geometry
                bool preserve_uvs   = true;
                bool preserve_edges = m_flags & static_cast<uint32_t>(MeshFlags::PostProcessPreserveTerrainEdges);
                geometry_processing::simplify(lod_indices, lod_vertices, target_index_count, preserve_uvs, preserve_edges);

                // stop if simplification couldn't reduce complexity further
                if (lod_indices.size() >= prev_indices.size())
                {
                    break;
                }

                // add simplified geometry as new lod
                AddLod(lod_vertices, lod_indices, current_sub_mesh_index);

                // update for next iteration
                prev_vertices = move(lod_vertices);
                prev_indices  = move(lod_indices);
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
            static_cast<uint32_t>(MeshFlags::ImportGenerateSmoothNormals) |
            static_cast<uint32_t>(MeshFlags::PostProcessNormalizeScale) |
            static_cast<uint32_t>(MeshFlags::PostProcessOptimize)       |
            static_cast<uint32_t>(MeshFlags::PostProcessGenerateLods);
    }

    void Mesh::CreateGpuBuffers()
    {
        // append this mesh's geometry into the global vertex/index/meshlet buffers
        m_global_vertex_offset  = GeometryBuffer::AppendVertices(m_vertices.data(), static_cast<uint32_t>(m_vertices.size()));
        m_global_index_offset   = GeometryBuffer::AppendIndices(m_indices.data(), static_cast<uint32_t>(m_indices.size()));
        m_global_meshlet_offset = GeometryBuffer::AppendMeshletBounds(m_meshlets.data(), static_cast<uint32_t>(m_meshlets.size()));

        // normalize scale
        if (m_flags & static_cast<uint32_t>(MeshFlags::PostProcessNormalizeScale))
        {
            if (m_root_entity)
            {
                BoundingBox bounding_box(m_vertices.data(), static_cast<uint32_t>(m_vertices.size()));
                float scale_offset     = bounding_box.GetExtents().Length();
                float normalized_scale = 1.0f / scale_offset;
                m_root_entity->SetScale(normalized_scale);
            }
        }

        // publish to the renderer only after global offsets are finalized and every sub-mesh has at least lod 0,
        // release ordering pairs with the acquire load in BuildAccelerationStructure
        m_ready_for_blas.store(true, std::memory_order_release);
    }

    RHI_Buffer* Mesh::GetVertexBuffer()
    {
        return GeometryBuffer::GetVertexBuffer();
    }

    RHI_Buffer* Mesh::GetIndexBuffer()
    {
        return GeometryBuffer::GetIndexBuffer();
    }

    void Mesh::BuildAccelerationStructure(RHI_CommandList* cmd_list, bool allow_update)
    {
        SP_ASSERT(RHI_Device::IsSupportedRayTracing());

        // wait until the mesh has been fully published by the loader,
        // sub_meshes and global buffer offsets are only consistent after CreateGpuBuffers has run
        if (!m_ready_for_blas.load(std::memory_order_acquire))
        {
            return;
        }

        // nothing to build
        if (m_sub_meshes.empty())
        {
            return;
        }

        // the global geometry buffer must be built before acceleration structures
        RHI_Buffer* vertex_buffer = GeometryBuffer::GetVertexBuffer();
        RHI_Buffer* index_buffer  = GeometryBuffer::GetIndexBuffer();
        if (!vertex_buffer || !index_buffer)
        {
            return;
        }

        // resize blas vector to match sub-mesh count if needed
        if (m_blas.size() != m_sub_meshes.size())
        {
            m_blas.resize(m_sub_meshes.size());
        }

        // build one blas per sub-mesh
        for (uint32_t i = 0; i < static_cast<uint32_t>(m_sub_meshes.size()); i++)
        {
            // skip if already built
            if (m_blas[i])
            {
                continue;
            }

            // defensive, a sub-mesh with no lods means it was published before its first lod was filled in,
            // gating on m_ready_for_blas should make this unreachable but we keep the guard to avoid an out-of-range crash on regression
            if (m_sub_meshes[i].lods.empty())
            {
                continue;
            }

            const auto& lod = m_sub_meshes[i].lods[0]; // use lod 0 for blas

            // skip degenerate sub-meshes, passing zero counts to vkGetAccelerationStructureBuildSizesKHR
            // can produce garbage build sizes and crash the driver mid-burst
            if (lod.vertex_count == 0 || lod.index_count == 0 || (lod.index_count % 3) != 0)
            {
                SP_LOG_WARNING("Skipping degenerate sub-mesh blas: mesh=%s sub=%u verts=%u indices=%u", m_object_name.c_str(), i, lod.vertex_count, lod.index_count);
                continue;
            }

            // compute global offsets: mesh base offset + lod-relative offset
            uint32_t global_vertex_offset = m_global_vertex_offset + lod.vertex_offset;
            uint32_t global_index_offset  = m_global_index_offset + lod.index_offset;

            // create geometry for this sub-mesh using global buffer addresses
            RHI_AccelerationStructureGeometry geo;
            geo.transparent           = false;
            geo.vertex_format         = RHI_Format::R32G32B32_Float; // positions
            geo.vertex_buffer_address = RHI_Device::GetBufferDeviceAddress(vertex_buffer->GetRhiResource()) + global_vertex_offset * vertex_buffer->GetStride();
            geo.vertex_stride         = vertex_buffer->GetStride();
            geo.max_vertex            = lod.vertex_count - 1;
            geo.index_format          = RHI_Format::R32_Uint;
            geo.index_buffer_address  = RHI_Device::GetBufferDeviceAddress(index_buffer->GetRhiResource()) + global_index_offset * sizeof(uint32_t);

            vector<RHI_AccelerationStructureGeometry> geometries = { geo };
            vector<uint32_t> primitive_counts                    = { lod.index_count / 3 };

            // create and build blas for this sub-mesh
            string blas_name = m_object_name + "_blas_" + to_string(i);
            m_blas[i] = make_unique<RHI_AccelerationStructure>(RHI_AccelerationStructureType::Bottom, blas_name.c_str());
            m_blas[i]->BuildBottomLevel(cmd_list, geometries, primitive_counts, allow_update);
        }
    }

    RHI_AccelerationStructure* Mesh::GetBlas(uint32_t sub_mesh_index) const
    {
        if (sub_mesh_index >= m_blas.size())
        {
            return nullptr;
        }

        return m_blas[sub_mesh_index].get();
    }

    bool Mesh::HasBlas(uint32_t sub_mesh_index) const
    {
        if (sub_mesh_index >= m_blas.size())
        {
            return false;
        }

        return m_blas[sub_mesh_index] != nullptr;
    }

    void Mesh::InvalidateBlas(uint32_t sub_mesh_index)
    {
        if (sub_mesh_index < m_blas.size())
        {
            m_blas[sub_mesh_index].reset();
        }
    }

    void Mesh::InvalidateAllBlas()
    {
        // called when the global geometry buffer is rebuilt, every blas references the old vertex/index buffer
        // device address and must be rebuilt against the new buffers, the caller is responsible for ensuring the gpu is idle
        for (auto& blas : m_blas)
        {
            blas.reset();
        }
    }

    void Mesh::RefitBlas(RHI_CommandList* cmd_list, uint32_t sub_mesh_index)
    {
        if (!m_ready_for_blas.load(std::memory_order_acquire))
        {
            return;
        }

        if (sub_mesh_index >= m_blas.size() || !m_blas[sub_mesh_index] || !m_blas[sub_mesh_index]->CanRefit())
        {
            return;
        }

        RHI_Buffer* vertex_buffer = GeometryBuffer::GetVertexBuffer();
        RHI_Buffer* index_buffer  = GeometryBuffer::GetIndexBuffer();
        if (!vertex_buffer || !index_buffer)
        {
            return;
        }

        if (sub_mesh_index >= m_sub_meshes.size() || m_sub_meshes[sub_mesh_index].lods.empty())
        {
            return;
        }

        const auto& lod = m_sub_meshes[sub_mesh_index].lods[0];

        uint32_t global_vertex_offset = m_global_vertex_offset + lod.vertex_offset;
        uint32_t global_index_offset  = m_global_index_offset + lod.index_offset;

        RHI_AccelerationStructureGeometry geo;
        geo.transparent           = false;
        geo.vertex_format         = RHI_Format::R32G32B32_Float;
        geo.vertex_buffer_address = RHI_Device::GetBufferDeviceAddress(vertex_buffer->GetRhiResource()) + global_vertex_offset * vertex_buffer->GetStride();
        geo.vertex_stride         = vertex_buffer->GetStride();
        geo.max_vertex            = lod.vertex_count - 1;
        geo.index_format          = RHI_Format::R32_Uint;
        geo.index_buffer_address  = RHI_Device::GetBufferDeviceAddress(index_buffer->GetRhiResource()) + global_index_offset * sizeof(uint32_t);

        vector<RHI_AccelerationStructureGeometry> geometries = { geo };
        vector<uint32_t> primitive_counts                    = { lod.index_count / 3 };

        m_blas[sub_mesh_index]->RefitBottomLevel(cmd_list, geometries, primitive_counts);
    }

    bool Mesh::CanRefitBlas(uint32_t sub_mesh_index) const
    {
        if (sub_mesh_index >= m_blas.size() || !m_blas[sub_mesh_index])
        {
            return false;
        }

        return m_blas[sub_mesh_index]->CanRefit();
    }
}
