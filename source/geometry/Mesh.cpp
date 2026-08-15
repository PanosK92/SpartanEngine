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
#include "../rhi/RHI_Buffer.h"
#include "../rhi/RHI_Device.h"
#include "../rhi/RHI_AccelerationStructure.h"
#include "../world/Entity.h"
#include "../resource/import/ModelImporter.h"
#include "../rendering/GeometryBuffer.h"
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
    namespace
    {
        // v5 on-disk layout before unique remaps / micro indices existed
        struct MeshletBounds_v5
        {
            uint32_t center_xy             = 0;
            uint32_t center_z_radius       = 0;
            uint32_t cone_axis_cutoff      = 0;
            uint32_t first_index_tri_count = 0;
        };
        static_assert(sizeof(MeshletBounds_v5) == 16, "v5 meshlet bounds must stay 16 bytes");

        // add mesh-local or global offsets into packed first_vertex / first_micro fields
        void offset_meshlet_unique_ranges(Sb_MeshletBounds& bounds, const uint32_t vertex_offset, const uint32_t micro_offset)
        {
            const uint32_t first_vertex = bounds.first_vertex_vert_count & MESHLET_FIRST_VERTEX_MASK;
            const uint32_t vert_count   = (bounds.first_vertex_vert_count >> MESHLET_VERT_COUNT_SHIFT) & MESHLET_VERT_COUNT_MASK;
            const uint32_t new_first    = first_vertex + vertex_offset;

            SP_ASSERT_MSG(new_first <= MESHLET_FIRST_VERTEX_MAX, "Meshlet first_vertex exceeds the 25-bit pack budget after offset");

            bounds.first_vertex_vert_count = (new_first & MESHLET_FIRST_VERTEX_MASK) |
                                             ((vert_count & MESHLET_VERT_COUNT_MASK) << MESHLET_VERT_COUNT_SHIFT);
            bounds.first_micro += micro_offset;
        }

        // rebuild unique remaps + micro indices from v5 absolute index ranges so old caches keep working
        void convert_meshlets_v5_to_v6(
            const vector<uint32_t>& indices,
            const vector<SubMesh>& sub_meshes,
            vector<Sb_MeshletBounds>& meshlets,
            vector<uint32_t>& meshlet_vertices_out,
            vector<uint32_t>& meshlet_micro_indices_out
        )
        {
            meshlet_vertices_out.clear();
            meshlet_micro_indices_out.clear();

            for (const SubMesh& sub : sub_meshes)
            {
                for (const MeshLod& lod : sub.lods)
                {
                    const uint32_t meshlet_end = lod.meshlet_offset + lod.meshlet_count;
                    SP_ASSERT_MSG(meshlet_end <= static_cast<uint32_t>(meshlets.size()), "lod meshlet range exceeds loaded bounds");

                    for (uint32_t mi = lod.meshlet_offset; mi < meshlet_end; ++mi)
                    {
                        Sb_MeshletBounds& bounds = meshlets[mi];
                        const uint32_t first_index    = bounds.first_index_tri_count & MESHLET_FIRST_INDEX_MASK;
                        const uint32_t triangle_count = (bounds.first_index_tri_count >> MESHLET_TRI_COUNT_SHIFT) & MESHLET_TRI_COUNT_MASK;
                        const uint32_t index_base     = lod.index_offset + first_index;
                        const uint32_t index_end      = index_base + triangle_count * 3u;

                        SP_ASSERT_MSG(index_end <= static_cast<uint32_t>(indices.size()), "meshlet index range exceeds loaded indices");

                        const uint32_t first_vertex = static_cast<uint32_t>(meshlet_vertices_out.size());
                        const uint32_t first_micro  = static_cast<uint32_t>(meshlet_micro_indices_out.size());

                        uint32_t unique_remap[MESHLET_MAX_VERTICES];
                        uint32_t unique_count = 0;

                        auto get_or_add_unique = [&](const uint32_t lod_local_vertex) -> uint32_t
                        {
                            for (uint32_t u = 0; u < unique_count; ++u)
                            {
                                if (unique_remap[u] == lod_local_vertex)
                                {
                                    return u;
                                }
                            }

                            SP_ASSERT_MSG(unique_count < MESHLET_MAX_VERTICES, "v5 meshlet exceeds unique vertex cap during conversion");
                            unique_remap[unique_count] = lod_local_vertex;
                            meshlet_vertices_out.push_back(lod_local_vertex);
                            return unique_count++;
                        };

                        for (uint32_t i = index_base; i < index_end; ++i)
                        {
                            meshlet_micro_indices_out.push_back(get_or_add_unique(indices[i]));
                        }

                        SP_ASSERT_MSG(first_vertex <= MESHLET_FIRST_VERTEX_MAX, "converted first_vertex exceeds the 25-bit pack budget");
                        SP_ASSERT_MSG(unique_count <= MESHLET_VERT_COUNT_MASK, "converted vertex_count exceeds the 7-bit pack budget");
                        bounds.first_vertex_vert_count =
                            (first_vertex & MESHLET_FIRST_VERTEX_MASK) |
                            ((unique_count & MESHLET_VERT_COUNT_MASK) << MESHLET_VERT_COUNT_SHIFT);
                        bounds.first_micro = first_micro;
                    }
                }
            }
        }
    }

    Mesh::Mesh() : IResource(ResourceType::Mesh)
    {
        m_flags = GetDefaultFlags();
    }

    Mesh::~Mesh()
    {

    }

    void Mesh::RegisterForScripting(sol::state_view State)
    {
        State.new_enum("MeshFlags",
            "ImportRemoveRedundantData",       MeshFlags::ImportRemoveRedundantData,
            "ImportLights",                    MeshFlags::ImportLights,
            "ImportCombineMeshes",             MeshFlags::ImportCombineMeshes,
            "ImportGenerateSmoothNormals",     MeshFlags::ImportGenerateSmoothNormals,
            "PostProcessNormalizeScale",       MeshFlags::PostProcessNormalizeScale,
            "PostProcessOptimize",             MeshFlags::PostProcessOptimize,
            "PostProcessGenerateLods",         MeshFlags::PostProcessGenerateLods,
            "PostProcessPreserveTerrainEdges", MeshFlags::PostProcessPreserveTerrainEdges
        );

        State.new_usertype<Mesh>("Mesh",
            "SaveToFile",               &Mesh::SaveToFile,
            "LoadFromFile",             &Mesh::LoadFromFile,
            "Clear",                    &Mesh::Clear,
            "GetVertexCount",           &Mesh::GetVertexCount,
            "GetIndexCount",            &Mesh::GetIndexCount,
            "GetRootEntity",            &Mesh::GetRootEntity,
            "GetDefaultFlags",          &Mesh::GetDefaultFlags
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

        m_meshlet_vertices.clear();
        m_meshlet_vertices.shrink_to_fit();

        m_meshlet_micro_indices.clear();
        m_meshlet_micro_indices.shrink_to_fit();

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

        uint32_t version = 6; // meshlet unique-vertex remaps + micro-indices, MeshletBounds at 24 bytes
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

        uint32_t meshlet_vertex_count = static_cast<uint32_t>(m_meshlet_vertices.size());
        outfile.write(reinterpret_cast<const char*>(&meshlet_vertex_count), sizeof(uint32_t));
        outfile.write(reinterpret_cast<const char*>(m_meshlet_vertices.data()), meshlet_vertex_count * sizeof(uint32_t));

        uint32_t meshlet_micro_count = static_cast<uint32_t>(m_meshlet_micro_indices.size());
        outfile.write(reinterpret_cast<const char*>(&meshlet_micro_count), sizeof(uint32_t));
        outfile.write(reinterpret_cast<const char*>(m_meshlet_micro_indices.data()), meshlet_micro_count * sizeof(uint32_t));

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
            if (version != 5 && version != 6)
            {
                SP_LOG_ERROR("Unsupported mesh cache version for file: %s (got %u, supported 5-6)", file_path.c_str(), version);
                return;
            }

            const bool upgrade_from_v5 = (version == 5);

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

            if (upgrade_from_v5)
            {
                vector<MeshletBounds_v5> legacy_meshlets(meshlet_count);
                infile.read(reinterpret_cast<char*>(legacy_meshlets.data()), meshlet_count * sizeof(MeshletBounds_v5));
                for (uint32_t i = 0; i < meshlet_count; ++i)
                {
                    m_meshlets[i]                      = {};
                    m_meshlets[i].center_xy            = legacy_meshlets[i].center_xy;
                    m_meshlets[i].center_z_radius      = legacy_meshlets[i].center_z_radius;
                    m_meshlets[i].cone_axis_cutoff     = legacy_meshlets[i].cone_axis_cutoff;
                    m_meshlets[i].first_index_tri_count = legacy_meshlets[i].first_index_tri_count;
                }

                convert_meshlets_v5_to_v6(
                    m_indices,
                    m_sub_meshes,
                    m_meshlets,
                    m_meshlet_vertices,
                    m_meshlet_micro_indices
                );
            }
            else
            {
                infile.read(reinterpret_cast<char*>(m_meshlets.data()), meshlet_count * sizeof(Sb_MeshletBounds));

                uint32_t meshlet_vertex_count;
                infile.read(reinterpret_cast<char*>(&meshlet_vertex_count), sizeof(uint32_t));
                m_meshlet_vertices.resize(meshlet_vertex_count);
                infile.read(reinterpret_cast<char*>(m_meshlet_vertices.data()), meshlet_vertex_count * sizeof(uint32_t));

                uint32_t meshlet_micro_count;
                infile.read(reinterpret_cast<char*>(&meshlet_micro_count), sizeof(uint32_t));
                m_meshlet_micro_indices.resize(meshlet_micro_count);
                infile.read(reinterpret_cast<char*>(m_meshlet_micro_indices.data()), meshlet_micro_count * sizeof(uint32_t));
            }

            infile.close();

            CreateGpuBuffers();

            // rewrite the cache once so the next load skips conversion
            if (upgrade_from_v5)
            {
                SP_LOG_INFO("Upgraded mesh cache \"%s\" from v5 to v6", FileSystem::GetFileNameFromFilePath(file_path).c_str());
                SaveToFile(file_path);
            }
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

        if (!GetGeometryLod(sub_mesh_index, 0, indices, vertices))
        {
            SP_LOG_ERROR("GetGeometry: sub-mesh %u has no LOD 0", sub_mesh_index);
        }
    }

    uint32_t Mesh::GetLodCount(uint32_t sub_mesh_index) const
    {
        if (sub_mesh_index >= m_sub_meshes.size())
        {
            return 0;
        }

        return static_cast<uint32_t>(m_sub_meshes[sub_mesh_index].lods.size());
    }

    bool Mesh::GetGeometryLod(uint32_t sub_mesh_index, uint32_t lod_index, vector<uint32_t>* indices, vector<RHI_Vertex_PosTexNorTan>* vertices)
    {
        // lock for the duration of the read so concurrent AddGeometry/AddLod calls cannot
        // reallocate m_vertices/m_indices and invalidate the iterators we are reading from
        lock_guard lock(m_mutex);

        if (sub_mesh_index >= m_sub_meshes.size())
        {
            return false;
        }

        const SubMesh& sub_mesh = m_sub_meshes[sub_mesh_index];
        if (lod_index >= sub_mesh.lods.size())
        {
            return false;
        }

        const MeshLod& lod = sub_mesh.lods[lod_index];
        if (
            lod.index_count == 0 ||
            lod.vertex_count == 0 ||
            static_cast<size_t>(lod.index_offset) + lod.index_count > m_indices.size() ||
            static_cast<size_t>(lod.vertex_offset) + lod.vertex_count > m_vertices.size()
        )
        {
            return false;
        }

        if (indices)
        {
            indices->resize(lod.index_count); // allocate once (caller can reuse buffer)
            copy(m_indices.begin() + lod.index_offset,
                      m_indices.begin() + lod.index_offset + lod.index_count,
                      indices->begin());
        }

        if (vertices)
        {
            vertices->resize(lod.vertex_count); // allocate once (caller can reuse buffer)
            copy(m_vertices.begin() + lod.vertex_offset,
                      m_vertices.begin() + lod.vertex_offset + lod.vertex_count,
                      vertices->begin());
        }

        return true;
    }

    void Mesh::AddLod(vector<RHI_Vertex_PosTexNorTan>& vertices, vector<uint32_t>& indices, const uint32_t sub_mesh_index)
    {
        // build per-lod meshlets, this also repacks indices into meshlet-contiguous order and returns the lod aabb the meshlet bounds were quantized against
        // heavy work, kept outside the mesh mutex so concurrent AddLod calls run in parallel
        vector<Sb_MeshletBounds> lod_meshlets;
        vector<uint32_t>         lod_unique_vertices;
        vector<uint32_t>         lod_micro_indices;
        BoundingBox              lod_aabb;
        geometry_processing::build_meshlets(
            vertices,
            indices,
            lod_meshlets,
            lod_unique_vertices,
            lod_micro_indices,
            lod_aabb
        );

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

            // remaps are mesh-local across lods, bump first_vertex / first_micro before packing
            const uint32_t meshlet_vertex_offset = static_cast<uint32_t>(m_meshlet_vertices.size());
            const uint32_t meshlet_micro_offset  = static_cast<uint32_t>(m_meshlet_micro_indices.size());
            for (Sb_MeshletBounds& bounds : lod_meshlets)
            {
                offset_meshlet_unique_ranges(bounds, meshlet_vertex_offset, meshlet_micro_offset);
            }

            m_vertices.insert(m_vertices.end(), vertices.begin(), vertices.end());
            m_indices.insert(m_indices.end(), indices.begin(), indices.end());
            m_meshlets.insert(m_meshlets.end(), lod_meshlets.begin(), lod_meshlets.end());
            m_meshlet_vertices.insert(m_meshlet_vertices.end(), lod_unique_vertices.begin(), lod_unique_vertices.end());
            m_meshlet_micro_indices.insert(m_meshlet_micro_indices.end(), lod_micro_indices.begin(), lod_micro_indices.end());

            m_sub_meshes[sub_mesh_index].lods.push_back(lod);
        }
    }

    void Mesh::ReserveSubMeshes(const uint32_t count)
    {
        lock_guard lock(m_mutex);
        if (m_sub_meshes.size() < count)
        {
            m_sub_meshes.resize(count);
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

        // delegate the heavy work to the explicit-index overload so both code paths stay identical
        AddGeometry(vertices, indices, generate_lods, current_sub_mesh_index);

        if (sub_mesh_index)
        {
            *sub_mesh_index = current_sub_mesh_index;
        }
    }

    void Mesh::AddGeometry(vector<RHI_Vertex_PosTexNorTan>& vertices, vector<uint32_t>& indices, const bool generate_lods, const uint32_t sub_mesh_index_in)
    {
        // caller must have reserved this slot via ReserveSubMeshes or the auto-allocating overload above
        SP_ASSERT(sub_mesh_index_in < m_sub_meshes.size());

        const uint32_t current_sub_mesh_index = sub_mesh_index_in;

        // lod 0: original geometry
        {
            // never reorder skinned verts, bone weights are authored against assimp order
            const bool is_skinned = m_skeleton != nullptr;
            if (!is_skinned && (m_flags & static_cast<uint32_t>(MeshFlags::PostProcessOptimize)))
            {
                geometry_processing::optimize(vertices, indices);
            }

            // add the original geometry as lod 0
            AddLod(vertices, indices, current_sub_mesh_index);
        }

        // generate additional lods if requested
        if (generate_lods && (m_flags & static_cast<uint32_t>(MeshFlags::PostProcessGenerateLods)))
        {
            // screen fraction at which each lod becomes active, the simplification targets below derive from these
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

                // target is coverage / base_coverage, about 2x reduction per lod to match the 2x screen size steps
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

                // stop unless this level is meaningfully cheaper than the one above it, a level that
                // sheds a handful of triangles is a duplicate that still costs memory and a draw range
                if (lod_indices.size() >= static_cast<size_t>(static_cast<float>(prev_indices.size()) * mesh_lod_min_reduction))
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
    }

    bool Mesh::UpdateGeometry(
        vector<RHI_Vertex_PosTexNorTan>& vertices,
        vector<uint32_t>& indices
    )
    {
        vector<Sb_MeshletBounds> meshlets;
        vector<uint32_t> unique_vertices;
        vector<uint32_t> micro_indices;
        BoundingBox aabb;
        geometry_processing::build_meshlets(
            vertices,
            indices,
            meshlets,
            unique_vertices,
            micro_indices,
            aabb
        );

        if (
            vertices.size() > m_global_vertex_capacity ||
            indices.size() > m_global_index_capacity ||
            meshlets.size() > m_global_meshlet_capacity ||
            unique_vertices.size() > m_global_meshlet_vertex_capacity ||
            micro_indices.size() > m_global_meshlet_micro_capacity
        )
        {
            return false;
        }

        {
            lock_guard lock(m_mutex);
            if (
                m_sub_meshes.size() != 1 ||
                m_sub_meshes[0].lods.size() != 1
            )
            {
                return false;
            }
        }

        // gpu bounds store global unique/micro offsets
        vector<Sb_MeshletBounds> gpu_meshlets = meshlets;
        for (Sb_MeshletBounds& bounds : gpu_meshlets)
        {
            offset_meshlet_unique_ranges(bounds, m_global_meshlet_vertex_offset, m_global_meshlet_micro_offset);
        }

        GeometryBuffer::UpdateVertices(
            vertices.data(),
            m_global_vertex_offset,
            static_cast<uint32_t>(vertices.size())
        );
        GeometryBuffer::UpdateIndices(
            indices.data(),
            m_global_index_offset,
            static_cast<uint32_t>(indices.size())
        );
        GeometryBuffer::UpdateMeshletBounds(
            gpu_meshlets.data(),
            m_global_meshlet_offset,
            static_cast<uint32_t>(gpu_meshlets.size())
        );
        GeometryBuffer::UpdateMeshletVertices(
            unique_vertices.data(),
            m_global_meshlet_vertex_offset,
            static_cast<uint32_t>(unique_vertices.size())
        );
        GeometryBuffer::UpdateMeshletMicroIndices(
            micro_indices.data(),
            m_global_meshlet_micro_offset,
            static_cast<uint32_t>(micro_indices.size())
        );

        {
            lock_guard lock(m_mutex);
            m_vertices              = vertices;
            m_indices               = indices;
            m_meshlets              = meshlets;
            m_meshlet_vertices      = unique_vertices;
            m_meshlet_micro_indices = micro_indices;

            MeshLod& lod      = m_sub_meshes[0].lods[0];
            lod.vertex_offset = 0;
            lod.vertex_count  =
                static_cast<uint32_t>(m_vertices.size());
            lod.index_offset = 0;
            lod.index_count  =
                static_cast<uint32_t>(m_indices.size());
            lod.meshlet_offset = 0;
            lod.meshlet_count  =
                static_cast<uint32_t>(m_meshlets.size());
            lod.aabb = aabb;

            m_object_size =
                m_vertices.size() *
                sizeof(RHI_Vertex_PosTexNorTan) +
                m_indices.size() *
                sizeof(uint32_t);
        }

        InvalidateAllBlas();
        return true;
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

    shared_ptr<Mesh> Mesh::CreateSkinnedInstance()
    {
        if (!m_ready_for_blas.load(memory_order_acquire) || m_vertices.empty())
        {
            SP_LOG_WARNING("Mesh::CreateSkinnedInstance: source mesh is not gpu-ready");
            return nullptr;
        }

        shared_ptr<Mesh> instance = make_shared<Mesh>();
        instance->m_flags = m_flags;
        instance->m_type = m_type;
        instance->m_dynamic = true;
        instance->m_vertices              = m_vertices;
        instance->m_indices               = m_indices;
        instance->m_sub_meshes            = m_sub_meshes;
        instance->m_meshlets              = m_meshlets;
        instance->m_meshlet_vertices      = m_meshlet_vertices;
        instance->m_meshlet_micro_indices = m_meshlet_micro_indices;
        instance->m_skeleton              = m_skeleton;
        instance->m_animation_clips       = m_animation_clips;

        if (m_skeletal_mesh_binding)
        {
            auto binding = make_unique<SkeletalMeshBinding>();
            for (const SkeletalMeshSection& section : m_skeletal_mesh_binding->GetSections())
            {
                binding->AddSection(section);
            }
            instance->SetSkeletalMeshBinding(move(binding));
        }

        // share index/meshlet gpu slices, allocate a private vertex slice for skinning
        instance->m_global_index_offset           = m_global_index_offset;
        instance->m_global_index_capacity         = m_global_index_capacity;
        instance->m_global_meshlet_offset         = m_global_meshlet_offset;
        instance->m_global_meshlet_capacity       = m_global_meshlet_capacity;
        instance->m_global_meshlet_vertex_offset  = m_global_meshlet_vertex_offset;
        instance->m_global_meshlet_vertex_capacity = m_global_meshlet_vertex_capacity;
        instance->m_global_meshlet_micro_offset   = m_global_meshlet_micro_offset;
        instance->m_global_meshlet_micro_capacity = m_global_meshlet_micro_capacity;

        auto get_dynamic_capacity = [](const size_t count) -> uint32_t
        {
            if (count == 0)
            {
                return 0u;
            }

            uint32_t capacity = 1;
            while (capacity < count)
            {
                capacity *= 2;
            }

            return capacity;
        };

        instance->m_global_vertex_capacity = get_dynamic_capacity(m_vertices.size());
        vector<RHI_Vertex_PosTexNorTan> vertices = m_vertices;
        vertices.resize(instance->m_global_vertex_capacity);
        instance->m_global_vertex_offset = GeometryBuffer::AppendVertices(
            vertices.data(),
            instance->m_global_vertex_capacity
        );
        instance->m_ready_for_blas.store(true, memory_order_release);

        instance->SetObjectName(GetObjectName() + "_skinned_instance");
        return instance;
    }

    void Mesh::CreateGpuBuffers()
    {
        if (m_ready_for_blas.load(memory_order_acquire))
        {
            return;
        }

        auto get_dynamic_capacity = [](const size_t count)
        {
            if (count == 0)
            {
                return 0u;
            }

            uint32_t capacity = 1;
            while (capacity < count)
            {
                capacity *= 2;
            }

            return capacity;
        };

        m_global_vertex_capacity =
            m_dynamic
            ? get_dynamic_capacity(m_vertices.size())
            : static_cast<uint32_t>(m_vertices.size());
        m_global_index_capacity =
            m_dynamic
            ? get_dynamic_capacity(m_indices.size())
            : static_cast<uint32_t>(m_indices.size());
        m_global_meshlet_capacity =
            m_dynamic
            ? get_dynamic_capacity(m_meshlets.size())
            : static_cast<uint32_t>(m_meshlets.size());
        m_global_meshlet_vertex_capacity =
            m_dynamic
            ? get_dynamic_capacity(m_meshlet_vertices.size())
            : static_cast<uint32_t>(m_meshlet_vertices.size());
        m_global_meshlet_micro_capacity =
            m_dynamic
            ? get_dynamic_capacity(m_meshlet_micro_indices.size())
            : static_cast<uint32_t>(m_meshlet_micro_indices.size());

        // unique/micro first, so bounds can be patched with the returned global offsets
        if (m_dynamic)
        {
            vector<uint32_t> meshlet_vertices = m_meshlet_vertices;
            vector<uint32_t> meshlet_micros   = m_meshlet_micro_indices;
            meshlet_vertices.resize(m_global_meshlet_vertex_capacity);
            meshlet_micros.resize(m_global_meshlet_micro_capacity);

            m_global_meshlet_vertex_offset = GeometryBuffer::AppendMeshletVertices(
                meshlet_vertices.data(),
                m_global_meshlet_vertex_capacity
            );
            m_global_meshlet_micro_offset = GeometryBuffer::AppendMeshletMicroIndices(
                meshlet_micros.data(),
                m_global_meshlet_micro_capacity
            );
        }
        else
        {
            m_global_meshlet_vertex_offset = GeometryBuffer::AppendMeshletVertices(
                m_meshlet_vertices.data(),
                m_global_meshlet_vertex_capacity
            );
            m_global_meshlet_micro_offset = GeometryBuffer::AppendMeshletMicroIndices(
                m_meshlet_micro_indices.data(),
                m_global_meshlet_micro_capacity
            );
        }

        // patch mesh-local first_vertex / first_micro into global offsets for the uploaded bounds
        vector<Sb_MeshletBounds> gpu_meshlets = m_meshlets;
        for (Sb_MeshletBounds& bounds : gpu_meshlets)
        {
            offset_meshlet_unique_ranges(bounds, m_global_meshlet_vertex_offset, m_global_meshlet_micro_offset);
        }

        if (m_dynamic)
        {
            vector<RHI_Vertex_PosTexNorTan> vertices = m_vertices;
            vector<uint32_t> indices                 = m_indices;
            vertices.resize(m_global_vertex_capacity);
            indices.resize(m_global_index_capacity);
            gpu_meshlets.resize(m_global_meshlet_capacity);

            m_global_vertex_offset = GeometryBuffer::AppendVertices(
                vertices.data(),
                m_global_vertex_capacity
            );
            m_global_index_offset = GeometryBuffer::AppendIndices(
                indices.data(),
                m_global_index_capacity
            );
            m_global_meshlet_offset =
                GeometryBuffer::AppendMeshletBounds(
                    gpu_meshlets.data(),
                    m_global_meshlet_capacity
                );
        }
        else
        {
            m_global_vertex_offset = GeometryBuffer::AppendVertices(
                m_vertices.data(),
                m_global_vertex_capacity
            );
            m_global_index_offset = GeometryBuffer::AppendIndices(
                m_indices.data(),
                m_global_index_capacity
            );
            m_global_meshlet_offset =
                GeometryBuffer::AppendMeshletBounds(
                    gpu_meshlets.data(),
                    m_global_meshlet_capacity
                );
        }

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

    void Mesh::BuildAccelerationStructure(bool allow_update)
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
            m_blas[i]->BuildBottomLevel(geometries, primitive_counts, allow_update);
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

    void Mesh::RefitBlas(uint32_t sub_mesh_index)
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

        m_blas[sub_mesh_index]->RefitBottomLevel(geometries, primitive_counts);
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
