/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

//= INCLUDES ================================
#include "pch.h"
#include "GeneratedCache.h"
#include "world/World.h"
#include <fstream>
#include <shared_mutex>
#include "Mesh.h"
#include "../rhi/RHI_Buffer.h"
#include "../rhi/RHI_Device.h"
#include "../rhi/RHI_AccelerationStructure.h"
#include "../world/Entity.h"
#include "../core/ThreadPool.h"
#include "../resource/import/ModelImporter.h"
#include "../resource/import/ImageImporter.h"
#include "../rendering/GeometryBuffer.h"
#include "../rendering/Renderer.h"
#include "../animation/AnimationEvaluate.h"
#include "../animation/Skeleton.h"
#include "GeometryProcessing.h"
#include "meshoptimizer/meshoptimizer.h"
//===========================================

//= NAMESPACES ================
using namespace std;
using namespace spartan::math;
//=============================

namespace spartan
{
    namespace
    {
        shared_mutex geometry_page_files;
        atomic<uint64_t> geometry_save_id{0};

        // A native mesh stores many adjacent pages in one file. Keep its handle across
        // a batch and pin the directory while a save could otherwise relocate the pages.
        struct GeometryPageReader
        {
            shared_lock<shared_mutex> lock{geometry_page_files};
            string path;
            ifstream input;

            bool Read(const GeometryPageSource& source, vector<uint8_t>& result)
            {
                if (!source.bytes.empty())
                {
                    result = source.bytes;
                    return true;
                }
                if (!input.is_open() || path != source.file)
                {
                    input = ifstream(source.file, ios::binary);
                    path = source.file;
                }
                input.clear();
                if (!input.seekg(static_cast<streamoff>(source.offset))) return false;
                result.resize(source.size);
                return input.read(reinterpret_cast<char*>(result.data()), source.size) &&
                    generated_cache::HashBytes(result.data(), result.size()) == source.checksum;
            }
        };

        using PageLocations = vector<pair<shared_ptr<const GeometryPageSource>, uint64_t>>;
        void write_directory(ostream& outfile, const vector<SubMesh>& subs, PageLocations* locations)
        {
            vector<uint8_t> bytes;
            auto write = [&](const auto& value) { outfile.write(reinterpret_cast<const char*>(&value), sizeof(value)); };
            for (const SubMesh& sub : subs)
            {
                write(static_cast<uint32_t>(sub.clusters.size()));
                for (const auto& cluster : sub.clusters)
                {
                    write(cluster.meshlet_count);
                    const auto lo = cluster.bounds.GetMin(), hi = cluster.bounds.GetMax();
                    write(lo.x); write(lo.y); write(lo.z); write(hi.x); write(hi.y); write(hi.z);
                    write(cluster.error);
                    write(static_cast<uint32_t>(cluster.refinements.size()));
                    for (const auto& level : cluster.refinements)
                    {
                        write(level.error);
                        write(level.page->size);
                        write(level.page->checksum);
                        if (!level.page->Read(bytes)) throw runtime_error("Cannot serialize geometry page");
                        if (locations) locations->emplace_back(level.page, static_cast<uint64_t>(outfile.tellp()));
                        outfile.write(reinterpret_cast<const char*>(bytes.data()), bytes.size());
                    }
                }
            }
        }

        void read_directory(istream& infile, uint64_t length, const string& file_path, vector<SubMesh>& sub_meshes,
            size_t vertex_count, const vector<uint32_t>& indices, const vector<Sb_MeshletBounds>& meshlets,
            const vector<uint32_t>& meshlet_vertices, const vector<uint32_t>& meshlet_micro_indices)
        {
            auto read = [&](auto& value) { infile.read(reinterpret_cast<char*>(&value), sizeof(value)); };
            auto read_box = [&]()
            {
                float v[6]; read(v);
                const Vector3 lo(v[0], v[1], v[2]), hi(v[3], v[4], v[5]);
                if (!lo.IsFinite() || !hi.IsFinite() || lo.x > hi.x || lo.y > hi.y || lo.z > hi.z)
                    throw runtime_error("Invalid geometry cluster bounds");
                return BoundingBox(lo, hi);
            };
            for (SubMesh& sub : sub_meshes)
            {
                uint32_t count; read(count);
                if (!count) continue;
                if (count > meshlets.size() || count > (length - static_cast<uint64_t>(infile.tellg())) / 36 ||
                    !sub.HasGeometry())
                    throw runtime_error("Invalid virtual geometry directory");
                const GeometryRange& root = sub.root;
                if (uint64_t(root.index_offset) + root.index_count > indices.size() ||
                    uint64_t(root.vertex_offset) + root.vertex_count > vertex_count ||
                    uint64_t(root.meshlet_offset) + root.meshlet_count > meshlets.size())
                    throw runtime_error("Invalid virtual geometry proxy");
                uint32_t first_meshlet = 0, first_index = 0;
                sub.clusters.resize(count);
                for (auto& cluster : sub.clusters)
                {
                    read(cluster.meshlet_count);
                    if (!cluster.meshlet_count || cluster.meshlet_count > root.meshlet_count - first_meshlet)
                        throw runtime_error("Invalid cluster meshlet count");
                    cluster.first_meshlet = first_meshlet;
                    for (uint32_t j = 0; j < cluster.meshlet_count; ++j)
                    {
                        const auto& m = meshlets[root.meshlet_offset + first_meshlet + j];
                        const uint32_t corners = 3 * (m.first_index_tri_count >> MESHLET_TRI_COUNT_SHIFT);
                        const uint32_t verts = m.first_vertex_vert_count >> MESHLET_VERT_COUNT_SHIFT;
                        const uint32_t remap = m.first_vertex_vert_count & MESHLET_FIRST_VERTEX_MASK;
                        if (!corners || corners > 3 * MESHLET_MAX_TRIANGLES || !verts || verts > MESHLET_MAX_VERTICES ||
                            (m.first_index_tri_count & MESHLET_FIRST_INDEX_MASK) != first_index ||
                            uint64_t(first_index) + corners > root.index_count ||
                            uint64_t(remap) + verts > meshlet_vertices.size() ||
                            uint64_t(m.first_micro) + corners > meshlet_micro_indices.size())
                            throw runtime_error("Invalid virtual geometry meshlet");
                        for (uint32_t k = 0; k < verts; ++k)
                            if (meshlet_vertices[remap + k] >= root.vertex_count) throw runtime_error("Invalid proxy vertex");
                        for (uint32_t k = 0; k < corners; ++k)
                            if (meshlet_micro_indices[m.first_micro + k] >= verts ||
                                indices[root.index_offset + first_index + k] >= root.vertex_count)
                                throw runtime_error("Invalid proxy triangle");
                        first_index += corners;
                        cluster.index_count += corners;
                    }
                    first_meshlet += cluster.meshlet_count;
                    cluster.bounds = read_box();
                    read(cluster.error);
                    if (!isfinite(cluster.error) || cluster.error < 0) throw runtime_error("Invalid cluster error");
                    uint32_t levels; read(levels);
                    if (levels > 16) throw runtime_error("Invalid geometry refinement count");
                    float previous_error = 0;
                    for (uint32_t i = 0; i < levels; ++i)
                    {
                        float error; read(error);
                        auto source = make_shared<GeometryPageSource>();
                        read(source->size); read(source->checksum);
                        if (!isfinite(error) || error < previous_error || error > cluster.error || (!i && error != 0))
                            throw runtime_error("Invalid geometry refinement error");
                        previous_error = error;
                        source->file = file_path;
                        source->offset = static_cast<uint64_t>(infile.tellg());
                        if (!source->size || source->size > 128 * 1024 || source->offset > length ||
                            source->size > length - source->offset) throw runtime_error("Invalid geometry page extent");
                        if (file_path.empty())
                        {
                            source->bytes.resize(source->size);
                            infile.read(reinterpret_cast<char*>(source->bytes.data()), source->size);
                            if (generated_cache::HashBytes(source->bytes.data(), source->size) != source->checksum)
                                throw runtime_error("Invalid embedded geometry page");
                        }
                        else infile.seekg(source->size, ios::cur);
                        cluster.refinements.push_back({error, move(source)});
                    }
                }
                if (first_meshlet != root.meshlet_count || first_index != root.index_count)
                    throw runtime_error("Incomplete virtual geometry proxy");
            }
        }

        constexpr uint64_t prepared_virtual_magic = 0x3147505652505353ull;
        span<const uint8_t> prepared_proxy(span<const uint8_t> bytes, span<const uint8_t>& directory)
        {
            uint64_t header[2];
            if (bytes.size() < sizeof(header)) return bytes;
            memcpy(header, bytes.data(), sizeof(header));
            if (header[0] != prepared_virtual_magic) return bytes;
            if (header[1] >= bytes.size() - sizeof(header)) return {};
            directory = bytes.subspan(sizeof(header) + static_cast<size_t>(header[1]));
            return bytes.subspan(sizeof(header), static_cast<size_t>(header[1]));
        }

        // v5 on-disk layout before unique remaps / micro indices existed
        struct MeshletBounds_v5
        {
            uint32_t center_xy             = 0;
            uint32_t center_z_radius       = 0;
            uint32_t cone_axis_cutoff      = 0;
            uint32_t first_index_tri_count = 0;
        };
        static_assert(sizeof(MeshletBounds_v5) == 16, "v5 meshlet bounds must stay 16 bytes");

        // Add LOD offsets within one mesh. Global GPU buffer bases live in DrawData.
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
                if (sub.HasGeometry())
                {
                    const auto& lod = sub.root;
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

    void Mesh::Clear()
    {
        m_object_size = 0;
        m_ready_for_blas = false;
        m_cpu_geometry_released = false;

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
        try
        {
            CreateSaveTask(file_path)();
        }
        catch (const exception& error) { SP_LOG_ERROR("Resource save failed: %s", error.what()); }
    }

    function<void()> Mesh::CreateSaveTask(const string& file_path)
    {
        lock_guard snapshot_lock(m_mutex);
        if (!restore_cpu_geometry_locked()) throw runtime_error("Cannot restore mesh for save: " + file_path);
        return [file_path, m_type = m_type, m_flags = m_flags, m_sub_meshes = m_sub_meshes, m_vertices = m_vertices, m_indices = m_indices, m_meshlets = m_meshlets, m_meshlet_vertices = m_meshlet_vertices, m_meshlet_micro_indices = m_meshlet_micro_indices, m_object_name = m_object_name]()
        {
            constexpr uint32_t version = 8;
            const string destination = filesystem::absolute(file_path).string();
            struct PendingFile
            {
                string path;
                ~PendingFile() { if (!path.empty()) { error_code ec; filesystem::remove(path, ec); } }
            };
            PendingFile pending;
            pending.path = destination + ".tmp." + to_string(chrono::steady_clock::now().time_since_epoch().count()) +
                    "." + to_string(geometry_save_id.fetch_add(1));
            PageLocations locations;
            ofstream outfile(pending.path, ios::binary);
            if (!outfile) throw runtime_error("Failed to open mesh: " + file_path);
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
                uint32_t lod_count = static_cast<uint32_t>(sub.HasGeometry());
                outfile.write(reinterpret_cast<const char*>(&lod_count), sizeof(uint32_t));

                if (sub.HasGeometry())
                {
                    const auto& lod = sub.root;
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

            const uint32_t reserved = 0;
            for (size_t i = 0; i < m_sub_meshes.size(); ++i)
                outfile.write(reinterpret_cast<const char*>(&reserved), sizeof(reserved));
            write_directory(outfile, m_sub_meshes, &locations);
            outfile.close();
            if (!outfile) throw runtime_error("Failed to write mesh: " + file_path);
            if (!pending.path.empty())
            {
                unique_lock lock(geometry_page_files);
                filesystem::rename(pending.path, destination);
                pending.path.clear();
                for (const auto& [source, offset] : locations)
                {
                    source->file = destination;
                    source->offset = offset;
                    vector<uint8_t>().swap(source->bytes);
                }
            }
            SP_LOG_INFO("Mesh '%s': saved %u sub-meshes, %u vertices, %u indices", m_object_name.c_str(), submesh_count, vertex_count, index_count);
        };
    }

    bool Mesh::LoadFromFile(const string& file_path)
    {
        const Stopwatch timer;

        if (FileSystem::IsSupportedModelFile(file_path)) // foreign
        {
            if (!ModelImporter::Load(this, file_path)) return false;
        }
        else if (FileSystem::IsEngineMeshFile(file_path)) // native
        {
            uint32_t version;
            if (!LoadNativeCpu(file_path, &version)) return false;
            if (version < 8 || any_of(m_sub_meshes.begin(), m_sub_meshes.end(),
                [](const SubMesh& sub) { return sub.HasGeometry() && sub.clusters.empty(); }))
            {
                Mesh cooked;
                if (!cooked.CookFromHighestDetail(*this)) return false;
                AdoptGeometry(cooked);
                SaveToFile(file_path);
            }
            CreateGpuBuffers();
        }
        else
        {
            SP_LOG_ERROR("Failed to load mesh %s: format not supported", file_path.c_str());
            return false;
        }
        SetResourceFilePath(file_path);
        m_object_size = GetMemoryUsage();
        SP_LOG_INFO("Loading \"%s\" took %d ms", FileSystem::GetFileNameFromFilePath(file_path).c_str(), static_cast<int>(timer.GetElapsedTimeMs()));
        return true;
    }

    bool Mesh::CookFromHighestDetail(Mesh& source, bool tree)
    {
        m_type = source.m_type;
        m_flags = source.m_flags | static_cast<uint32_t>(MeshFlags::PostProcessSkipCache);
        ReserveSubMeshes(source.GetSubMeshCount());
        for (uint32_t i = 0; i < source.GetSubMeshCount(); ++i)
        {
            if (!source.m_sub_meshes[i].HasGeometry()) continue;
            vector<RHI_Vertex_PosTexNorTan> vertices;
            vector<uint32_t> indices;
            if (!source.ReadGeometry(i, &indices, &vertices) || !AddVirtualGeometry(vertices, indices, i, false, tree && i == 1)) return false;
        }
        m_flags = source.m_flags;
        if (tree) m_flags |= static_cast<uint32_t>(MeshFlags::FoliageCardClusters);
        return true;
    }

    void Mesh::AdoptGeometry(Mesh& source)
    {
        m_sub_meshes = move(source.m_sub_meshes);
        m_vertices = move(source.m_vertices);
        m_indices = move(source.m_indices);
        m_meshlets = move(source.m_meshlets);
        m_meshlet_vertices = move(source.m_meshlet_vertices);
        m_meshlet_micro_indices = move(source.m_meshlet_micro_indices);
    }

    // Canonicalize only triangle order and cyclic rotation. This check retains
    // winding, duplicated faces, seams, packed normals, tangents and UVs.
    static vector<array<uint64_t, 3>> surface_triangles(Mesh& mesh, uint32_t sub)
    {
        vector<RHI_Vertex_PosTexNorTan> vertices;
        vector<uint32_t> indices;
        if (!mesh.ReadGeometry(sub, &indices, &vertices) || indices.empty() || indices.size() % 3)
            throw runtime_error("could not recover the authored surface");
        vector<uint64_t> keys;
        keys.reserve(vertices.size());
        for (const auto& v : vertices) keys.push_back(generated_cache::HashBytes(&v, sizeof(v)));
        vector<array<uint64_t, 3>> triangles;
        triangles.reserve(indices.size() / 3);
        for (size_t i = 0; i < indices.size(); i += 3)
        {
            const uint64_t a = keys.at(indices[i]), b = keys.at(indices[i + 1]), c = keys.at(indices[i + 2]);
            triangles.push_back(min({array{a,b,c}, array{b,c,a}, array{c,a,b}}));
        }
        sort(triangles.begin(), triangles.end());
        return triangles;
    }

    int Mesh::UpgradeGeometryDirectory(const string& directory)
    {
        size_t upgraded = 0, deferred = 0, current = 0, failed = 0;
        try
        {
            for (const auto& entry : filesystem::recursive_directory_iterator(directory))
            {
                if (!entry.is_regular_file() || entry.path().extension() != ".mesh") continue;
                const auto path = entry.path();
                try
                {
                    ifstream header(path, ios::binary);
                    header.exceptions(ios::badbit | ios::failbit);
                    uint32_t version;
                    header.read(reinterpret_cast<char*>(&version), sizeof(version));

                    if (version < 5 || version > 8) throw runtime_error("unsupported native version");
                    header.seekg(16);
                    uint32_t sub_count;
                    header.read(reinterpret_cast<char*>(&sub_count), sizeof(sub_count));
                    bool fits_root = sub_count != 0;
                    for (uint32_t i = 0; i < sub_count && fits_root; ++i)
                    {
                        uint32_t count;
                        header.read(reinterpret_cast<char*>(&count), sizeof(count));
                        if (count != 1) { fits_root = false; break; }
                        uint32_t range[6];
                        header.read(reinterpret_cast<char*>(range), sizeof(range));
                        header.seekg(6 * sizeof(float), ios::cur);
                        fits_root = range[3] && range[3] <= GeometryPage::max_corners && range[5];
                    }

                    header.close();
                    Mesh original;
                    if (!original.LoadNativeCpu(path.string())) throw runtime_error("could not load source");
                    // Tree Creator exports bark and folded foliage as separate prefab parts.
                    const bool tree = sub_count == 2 && !(original.m_flags & static_cast<uint32_t>(MeshFlags::FoliageCardClusters)) &&
                        filesystem::exists(path.parent_path() / (path.stem().string() + "_foliage.xml")) &&
                        filesystem::exists(path.parent_path() / (path.stem().string() + ".prefab"));
                    if (version == 8 && !tree && all_of(original.m_sub_meshes.begin(), original.m_sub_meshes.end(),
                        [](const SubMesh& sub)
                        {
                            return !sub.clusters.empty() && all_of(sub.clusters.begin(), sub.clusters.end(),
                                [](const GeometryCluster& cluster) { return cluster.index_count <= GeometryPage::max_corners; });
                        })) { ++current; continue; }
                    if (any_of(original.m_sub_meshes.begin(), original.m_sub_meshes.end(),
                        [](const SubMesh& sub) { return !sub.HasGeometry(); })) { ++deferred; continue; }
                    const bool has_cards = any_of(original.m_sub_meshes.begin(), original.m_sub_meshes.end(),
                        [](const SubMesh& sub) { return sub.clusters.empty(); });

                    if (has_cards || tree) fits_root = false;
                    Mesh cooked;
                    if (!fits_root && !cooked.CookFromHighestDetail(original, tree)) throw runtime_error("could not cook surface");

                    const auto staging = path.string() + ".upgrade." +
                        to_string(chrono::steady_clock::now().time_since_epoch().count());
                    struct StagingFile
                    {
                        filesystem::path path;
                        ~StagingFile() { error_code ec; filesystem::remove(path, ec); }
                    } guard{staging};
                    (fits_root ? original : cooked).CreateSaveTask(staging)();
                    Mesh check;
                    if (!check.LoadNativeCpu(staging) || check.m_sub_meshes.size() != sub_count)
                        throw runtime_error("could not reload cooked mesh");
                    if (fits_root && (check.m_vertices.size() != original.m_vertices.size() ||
                        memcmp(check.m_vertices.data(), original.m_vertices.data(), original.m_vertices.size() * sizeof(original.m_vertices[0])) ||
                        check.m_indices != original.m_indices || check.m_meshlet_vertices != original.m_meshlet_vertices ||
                        check.m_meshlet_micro_indices != original.m_meshlet_micro_indices ||
                        !all_of(check.m_sub_meshes.begin(), check.m_sub_meshes.end(),
                            [](const SubMesh& sub) { return sub.HasResidentRootOnly(); })))
                        throw runtime_error("round-trip verification failed");
                    if (!fits_root)
                        for (uint32_t i = 0; i < sub_count; ++i)
                            if (surface_triangles(check, i) != surface_triangles(original, i))
                                throw runtime_error("finest virtual surface differs from the source");
                    filesystem::rename(staging, path);
                    ++upgraded;
                }
                catch (const exception& error)
                {
                    fprintf(stderr, "%s: %s\n", path.string().c_str(), error.what());
                    ++failed;
                }
            }
        }
        catch (const exception& error) { fprintf(stderr, "%s\n", error.what()); return 1; }
        fprintf(stdout, "Geometry upgrade: %zu upgraded, %zu current-format, %zu require further cooking, %zu failed\n",
            upgraded, current, deferred, failed);
        return failed ? 1 : deferred ? 2 : 0;
    }

    bool Mesh::LoadNativeCpu(const string& file_path, uint32_t* file_version)
    {
        ifstream infile(file_path, ios::binary);
        if (!infile)
        {
            SP_LOG_ERROR("Failed to open file: %s", file_path.c_str());
            return false;
        }

        // Read into temporary CPU storage. A failed reload leaves live geometry intact.
        vector<SubMesh> sub_meshes;
        vector<RHI_Vertex_PosTexNorTan> vertices;
        vector<uint32_t> indices, meshlet_vertices, meshlet_micro_indices;
        vector<Sb_MeshletBounds> meshlets;
        uint32_t flags = 0;
        MeshType mesh_type = MeshType::Max;
        infile.exceptions(ios::failbit | ios::badbit);
        try
        {

            uint32_t version;
            infile.read(reinterpret_cast<char*>(&version), sizeof(uint32_t));
            if (version < 5 || version > 8)
            {
                SP_LOG_ERROR("Unsupported mesh cache version for file: %s (got %u, supported 5-8)", file_path.c_str(), version);
                return false;
            }

            if (file_version) *file_version = version;
            const bool upgrade_from_v5 = (version == 5);

            uint32_t type;
            infile.read(reinterpret_cast<char*>(&type), sizeof(uint32_t));
            mesh_type = static_cast<MeshType>(type);

            // legacy field for backward compatibility (skip)
            uint32_t legacy_field;
            infile.read(reinterpret_cast<char*>(&legacy_field), sizeof(uint32_t));

            infile.read(reinterpret_cast<char*>(&flags), sizeof(uint32_t));

            uint32_t submesh_count;
            infile.read(reinterpret_cast<char*>(&submesh_count), sizeof(uint32_t));
            sub_meshes.resize(submesh_count);
            vector<bool> legacy_ranges(submesh_count, false);

            for (uint32_t sub_idx = 0; sub_idx < submesh_count; sub_idx++)
            {
                SubMesh& sub = sub_meshes[sub_idx];
                uint32_t lod_count;
                infile.read(reinterpret_cast<char*>(&lod_count), sizeof(uint32_t));
                legacy_ranges[sub_idx] = lod_count > 1;

                for (uint32_t level = 0; level < lod_count; ++level)
                {
                    if (level) { infile.seekg(48, ios::cur); continue; }
                    auto& lod = sub.root;
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
            vertices.resize(vertex_count);
            infile.read(reinterpret_cast<char*>(vertices.data()), vertex_count * sizeof(RHI_Vertex_PosTexNorTan));

            uint32_t index_count;
            infile.read(reinterpret_cast<char*>(&index_count), sizeof(uint32_t));
            indices.resize(index_count);
            infile.read(reinterpret_cast<char*>(indices.data()), index_count * sizeof(uint32_t));

            uint32_t meshlet_count;
            infile.read(reinterpret_cast<char*>(&meshlet_count), sizeof(uint32_t));
            meshlets.resize(meshlet_count);

            if (upgrade_from_v5)
            {
                vector<MeshletBounds_v5> legacy_meshlets(meshlet_count);
                infile.read(reinterpret_cast<char*>(legacy_meshlets.data()), meshlet_count * sizeof(MeshletBounds_v5));
                for (uint32_t i = 0; i < meshlet_count; ++i)
                {
                    meshlets[i]                      = {};
                    meshlets[i].center_xy            = legacy_meshlets[i].center_xy;
                    meshlets[i].center_z_radius      = legacy_meshlets[i].center_z_radius;
                    meshlets[i].cone_axis_cutoff     = legacy_meshlets[i].cone_axis_cutoff;
                    meshlets[i].first_index_tri_count = legacy_meshlets[i].first_index_tri_count;
                }

                convert_meshlets_v5_to_v6(
                    indices,
                    sub_meshes,
                    meshlets,
                    meshlet_vertices,
                    meshlet_micro_indices
                );
            }
            else
            {
                infile.read(reinterpret_cast<char*>(meshlets.data()), meshlet_count * sizeof(Sb_MeshletBounds));

                uint32_t meshlet_vertex_count;
                infile.read(reinterpret_cast<char*>(&meshlet_vertex_count), sizeof(uint32_t));
                meshlet_vertices.resize(meshlet_vertex_count);
                infile.read(reinterpret_cast<char*>(meshlet_vertices.data()), meshlet_vertex_count * sizeof(uint32_t));

                uint32_t meshlet_micro_count;
                infile.read(reinterpret_cast<char*>(&meshlet_micro_count), sizeof(uint32_t));
                meshlet_micro_indices.resize(meshlet_micro_count);
                infile.read(reinterpret_cast<char*>(meshlet_micro_indices.data()), meshlet_micro_count * sizeof(uint32_t));
            }

            if (version >= 7)
            {
                for (SubMesh& sub : sub_meshes)
                {
                    uint32_t has_impostor = 0;
                    infile.read(reinterpret_cast<char*>(&has_impostor), sizeof(uint32_t));
                    if (!has_impostor)
                    {
                        continue;
                    }

                    legacy_ranges[&sub - sub_meshes.data()] = true;
                    infile.seekg(84, ios::cur);
                }
            }

            if (version >= 8)
            {
                read_directory(infile, filesystem::file_size(file_path), filesystem::absolute(file_path).string(),
                    sub_meshes, vertices.size(), indices, meshlets, meshlet_vertices, meshlet_micro_indices);
            }
            for (auto& sub : sub_meshes)
            {
                if (sub.clusters.empty() && !legacy_ranges[&sub - sub_meshes.data()] && sub.root.meshlet_count)
                    sub.SetResidentRoot();
            }

            InvalidateAllBlas();
            Clear();
            m_sub_meshes = std::move(sub_meshes);
            m_vertices = std::move(vertices);
            m_indices = std::move(indices);
            m_meshlets = std::move(meshlets);
            m_meshlet_vertices = std::move(meshlet_vertices);
            m_meshlet_micro_indices = std::move(meshlet_micro_indices);
            m_flags = flags;
            m_type = mesh_type;
            infile.close();

            // one line per mesh, a line per sub mesh was a thousand lines on a large world
            SP_LOG_INFO("Mesh '%s': loaded %u sub-meshes, %u vertices, %u indices", m_object_name.c_str(), submesh_count, vertex_count, index_count);

            SetResourceFilePath(file_path);
            return true;
        }
        catch (const exception& error)
        {
            SP_LOG_ERROR("Failed to read mesh %s: %s", file_path.c_str(), error.what());
            return false;
        }
    }

    namespace
    {
        atomic<uint32_t> cpu_geometry_restores = 0;
    }

    void Mesh::SetCpuGeometrySource(function<bool(vector<uint8_t>&)> source)
    {
        lock_guard lock(m_mutex);
        m_cpu_geometry_source = move(source);
    }

    bool Mesh::ReleaseCpuGeometry()
    {
        lock_guard lock(m_mutex);
        if (m_cpu_geometry_released || !m_cpu_geometry_source || m_dynamic || IsSkinned() || !m_ready_for_blas.load(memory_order_acquire))
        {
            return false;
        }

        m_released_vertex_count = static_cast<uint32_t>(m_vertices.size());
        m_released_index_count  = static_cast<uint32_t>(m_indices.size());
        vector<RHI_Vertex_PosTexNorTan>().swap(m_vertices);
        vector<uint32_t>().swap(m_indices);
        vector<Sb_MeshletBounds>().swap(m_meshlets);
        vector<uint32_t>().swap(m_meshlet_vertices);
        vector<uint32_t>().swap(m_meshlet_micro_indices);
        m_cpu_geometry_released = true;
        return true;
    }

    uint32_t Mesh::GetCpuGeometryRestoreCount()
    {
        return cpu_geometry_restores.load(memory_order_relaxed);
    }

    bool Mesh::RestoreCpuGeometry() const
    {
        Mesh* self = const_cast<Mesh*>(this);
        lock_guard lock(self->m_mutex);
        return self->restore_cpu_geometry_locked();
    }

    bool Mesh::restore_cpu_geometry_locked()
    {
        if (!m_cpu_geometry_released)
        {
            return true;
        }

        vector<uint8_t> bytes;
        vector<RHI_Vertex_PosTexNorTan> vertices;
        vector<uint32_t> indices, remaps, micro;
        vector<Sb_MeshletBounds> meshlets;
        vector<GeometryRange> lods;
        span<const uint8_t> directory;
        if (!m_cpu_geometry_source(bytes) ||
            !generated_cache::Decode(prepared_proxy(bytes, directory), vertices, indices, meshlets, remaps, micro, lods) || lods.empty())
            return false;
        if (directory.empty() && (lods.size() > 1 || lods.front().index_count > GeometryPage::max_corners))
        {
            Mesh restored;
            if (!restored.LoadPrepared(bytes)) return false;
            vertices = move(restored.m_vertices);
            indices = move(restored.m_indices);
            meshlets = move(restored.m_meshlets);
            remaps = move(restored.m_meshlet_vertices);
            micro = move(restored.m_meshlet_micro_indices);
        }
        if (vertices.size() != m_released_vertex_count || indices.size() != m_released_index_count)
        {
            SP_LOG_ERROR("Failed to restore cpu geometry of mesh '%s', its source no longer matches", m_object_name.c_str());
            return false;
        }

        m_vertices              = move(vertices);
        m_indices               = move(indices);
        m_meshlets              = move(meshlets);
        m_meshlet_vertices      = move(remaps);
        m_meshlet_micro_indices = move(micro);
        m_cpu_geometry_released = false;
        cpu_geometry_restores.fetch_add(1, memory_order_relaxed);
        return true;
    }

    uint64_t Mesh::GetCpuBytes() const
    {
        shared_lock lock(geometry_page_files);
        uint64_t pages = 0;
        for (const auto& sub : m_sub_meshes)
        {
            pages += sub.clusters.capacity() * sizeof(GeometryCluster);
            for (const auto& cluster : sub.clusters)
            {
                pages += cluster.refinements.capacity() * sizeof(GeometryRefinement);
                for (const auto& level : cluster.refinements) pages += sizeof(GeometryPageSource) + level.page->bytes.capacity();
            }
        }
        return pages + m_vertices.capacity() * sizeof(RHI_Vertex_PosTexNorTan) +
            m_indices.capacity() * sizeof(uint32_t) +
            m_meshlets.capacity() * sizeof(Sb_MeshletBounds) +
            m_meshlet_vertices.capacity() * sizeof(uint32_t) +
            m_meshlet_micro_indices.capacity() * sizeof(uint32_t);
    }

    uint64_t Mesh::GetMemoryUsage() const
    {
        // Geometry remains on the GPU after releasing its CPU mirror.
        if (m_cpu_geometry_released)
            return m_object_size;

        uint64_t size = m_vertices.size() * sizeof(RHI_Vertex_PosTexNorTan) +
            m_indices.size() * sizeof(uint32_t) +
            m_meshlets.size() * sizeof(Sb_MeshletBounds) +
            m_meshlet_vertices.size() * sizeof(uint32_t) +
            m_meshlet_micro_indices.size() * sizeof(uint32_t);
        return size;
    }

    void Mesh::GetGeometry(uint32_t sub_mesh_index, vector<uint32_t>* indices, vector<RHI_Vertex_PosTexNorTan>* vertices)
    {
        SP_ASSERT_MSG(indices != nullptr || vertices != nullptr, "Indices and vertices vectors can't both be null");

        if (!ReadGeometry(sub_mesh_index, indices, vertices))
        {
            SP_LOG_ERROR("GetGeometry: cannot read geometry for sub-mesh %u", sub_mesh_index);
        }
    }

    bool Mesh::HasGeometry(uint32_t sub_mesh_index) const
    {
        if (sub_mesh_index >= m_sub_meshes.size())
        {
            return 0;
        }

        return m_sub_meshes[sub_mesh_index].HasGeometry();
    }

    bool Mesh::ReadGeometry(uint32_t sub_mesh_index, vector<uint32_t>* indices, vector<RHI_Vertex_PosTexNorTan>* vertices)
    {
        // lock for the duration of the read so concurrent AddGeometry/AddResidentGeometry calls cannot
        // reallocate m_vertices/m_indices and invalidate the iterators we are reading from
        lock_guard lock(m_mutex);
        if (!restore_cpu_geometry_locked()) return false;

        if (sub_mesh_index >= m_sub_meshes.size())
        {
            return false;
        }

        const SubMesh& sub_mesh = m_sub_meshes[sub_mesh_index];
        if (!sub_mesh.HasGeometry())
        {
            return false;
        }

        if (!sub_mesh.clusters.empty() && !sub_mesh.HasResidentRootOnly())
        {
            vector<uint32_t> out_indices;
            vector<RHI_Vertex_PosTexNorTan> out_vertices;
            GeometryPageReader reader;
            uint32_t base = 0;
            for (const GeometryCluster& cluster : sub_mesh.clusters)
            {
                GeometryPage page;
                vector<uint8_t> bytes;
                if (!cluster.refinements.empty())
                {
                    if (!reader.Read(*cluster.refinements.front().page, bytes) || !page.Decode(bytes)) return false;
                    for (uint32_t index : page.indices) out_indices.push_back(base + index);
                    out_vertices.insert(out_vertices.end(), page.vertices.begin(), page.vertices.end());
                    base += static_cast<uint32_t>(page.vertices.size());
                }
                else
                {
                    const GeometryRange& lod = sub_mesh.root;
                    const uint32_t start = lod.index_offset +
                        (m_meshlets[lod.meshlet_offset + cluster.first_meshlet].first_index_tri_count & MESHLET_FIRST_INDEX_MASK);
                    unordered_map<uint32_t, uint32_t> compact;
                    for (uint32_t i = 0; i < cluster.index_count; ++i)
                    {
                        const uint32_t v = m_indices[start + i];
                        const auto [it, added] = compact.try_emplace(v, base);
                        if (added)
                        {
                            ++base;
                            out_vertices.push_back(m_vertices[lod.vertex_offset + v]);
                        }
                        out_indices.push_back(it->second);
                    }
                }
            }
            // Neighbouring independently streamable pages duplicate their border
            // vertices. Restore shared topology for collision and mesh editing.
            geometry_processing::weld_and_optimize(out_vertices, out_indices);
            if (vertices) *vertices = move(out_vertices);
            if (indices) *indices = move(out_indices);
            return true;
        }
        const GeometryRange& lod = sub_mesh.root;
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

    bool Mesh::LoadPrepared(const string& path, uint64_t key)
    {
        generated_cache::ReadMeasurement measurement{filesystem::path(path).parent_path().filename().string()};
        vector<uint8_t> bytes;
        if (!generated_cache::ReadPayload(path, key, bytes))
        {
            error_code error;
            measurement.missing = !filesystem::exists(path, error);
            return false;
        }
        measurement.hit = LoadPrepared(bytes);
        return measurement.hit;
    }

    bool Mesh::LoadPrepared(span<const uint8_t> bytes)
    {
        vector<RHI_Vertex_PosTexNorTan> vertices;
        vector<uint32_t> indices, remaps, micro;
        vector<Sb_MeshletBounds> meshlets;
        vector<GeometryRange> lods;
        span<const uint8_t> directory;
        if (!generated_cache::Decode(prepared_proxy(bytes, directory), vertices, indices, meshlets, remaps, micro, lods) ||
            lods.empty() || lods.size() > 5) return false;
        for (const GeometryRange& lod : lods)
        {
            if (!lod.vertex_count || !lod.index_count ||
                uint64_t(lod.vertex_offset) + lod.vertex_count > vertices.size() ||
                uint64_t(lod.index_offset) + lod.index_count > indices.size() ||
                uint64_t(lod.meshlet_offset) + lod.meshlet_count > meshlets.size() ||
                !lod.aabb.GetMin().IsFinite() || !lod.aabb.GetMax().IsFinite()) return false;
            for (size_t i = lod.index_offset; i < uint64_t(lod.index_offset) + lod.index_count; ++i)
                if (indices[i] >= lod.vertex_count) return false;
            for (size_t i = lod.meshlet_offset; i < uint64_t(lod.meshlet_offset) + lod.meshlet_count; ++i)
            {
                const auto& b = meshlets[i];
                const uint32_t first = b.first_vertex_vert_count & MESHLET_FIRST_VERTEX_MASK;
                const uint32_t count = (b.first_vertex_vert_count >> MESHLET_VERT_COUNT_SHIFT) & MESHLET_VERT_COUNT_MASK;
                const uint32_t triangles = (b.first_index_tri_count >> MESHLET_TRI_COUNT_SHIFT) & MESHLET_TRI_COUNT_MASK;
                const uint32_t index = b.first_index_tri_count & MESHLET_FIRST_INDEX_MASK;
                if (!count || count > MESHLET_MAX_VERTICES || !triangles || triangles > MESHLET_MAX_TRIANGLES ||
                    uint64_t(first) + count > remaps.size() || uint64_t(b.first_micro) + triangles * 3 > micro.size() ||
                    uint64_t(index) + triangles * 3 > lod.index_count) return false;
                for (size_t j = b.first_micro; j < uint64_t(b.first_micro) + triangles * 3; ++j)
                    if (micro[j] >= count) return false;
                for (size_t j = first; j < uint64_t(first) + count; ++j)
                    if (remaps[j] >= lod.vertex_count) return false;
            }
        }
        vector<SubMesh> subs(1);
        subs[0].root = lods.front();
        if (!directory.empty())
        {
            try
            {
                istringstream stream(string(reinterpret_cast<const char*>(directory.data()), directory.size()), ios::binary);
                stream.exceptions(ios::failbit | ios::badbit);
                read_directory(stream, directory.size(), {}, subs, vertices.size(), indices, meshlets, remaps, micro);
                if (static_cast<size_t>(stream.tellg()) != directory.size()) return false;
            }
            catch (const exception&) { return false; }
        }
        if (subs.front().clusters.empty() && lods.size() == 1 && subs.front().root.meshlet_count)
            subs.front().SetResidentRoot();
        m_vertices = move(vertices); m_indices = move(indices); m_meshlets = move(meshlets);
        m_meshlet_vertices = move(remaps); m_meshlet_micro_indices = move(micro);
        m_sub_meshes = move(subs);
        if (directory.empty() && (lods.size() > 1 || m_sub_meshes[0].root.index_count > GeometryPage::max_corners))
        {
            Mesh cooked;
            if (!cooked.CookFromHighestDetail(*this)) return false;
            AdoptGeometry(cooked);
        }
        return true;
    }

    void Mesh::SavePrepared(const string& path, uint64_t key) const
    {
        auto bytes = SerializePrepared();
        if (!bytes.empty() && !path.empty()) generated_cache::WritePayload(path, key, bytes);
    }

    vector<uint8_t> Mesh::SerializePrepared() const
    {
        if (m_sub_meshes.size() != 1 || !RestoreCpuGeometry()) return {};
        auto proxy = generated_cache::Encode(m_vertices, m_indices, m_meshlets,
            m_meshlet_vertices, m_meshlet_micro_indices, vector<GeometryRange>{m_sub_meshes[0].root});
        if (proxy.empty() || m_sub_meshes[0].clusters.empty()) return proxy;
        try
        {
            ostringstream directory(ios::binary);
            directory.exceptions(ios::failbit | ios::badbit);
            write_directory(directory, m_sub_meshes, nullptr);
            const auto payload = directory.view();
            const uint64_t header[] = {prepared_virtual_magic, proxy.size()};
            const size_t size = sizeof(header) + proxy.size() + payload.size();
            if (size > generated_cache::maximum_payload_size) return {};
            vector<uint8_t> bytes(size);
            memcpy(bytes.data(), header, sizeof(header));
            memcpy(bytes.data() + sizeof(header), proxy.data(), proxy.size());
            memcpy(bytes.data() + sizeof(header) + proxy.size(), payload.data(), payload.size());
            return bytes;
        }
        catch (const exception&) { return {}; }
    }

    void Mesh::AppendPrepared(const Mesh& tile, uint32_t sub_mesh_index)
    {
        lock_guard lock(m_mutex);
        SubMesh sub = tile.m_sub_meshes[0];
        if (sub.HasGeometry())
        {
            auto& lod = sub.root;
            lod.vertex_offset += static_cast<uint32_t>(m_vertices.size());
            lod.index_offset += static_cast<uint32_t>(m_indices.size());
            lod.meshlet_offset += static_cast<uint32_t>(m_meshlets.size());
        }
        const size_t first = m_meshlets.size();
        m_meshlets.insert(m_meshlets.end(), tile.m_meshlets.begin(), tile.m_meshlets.end());
        for (size_t i = first; i < m_meshlets.size(); ++i)
            offset_meshlet_unique_ranges(m_meshlets[i], static_cast<uint32_t>(m_meshlet_vertices.size()),
                static_cast<uint32_t>(m_meshlet_micro_indices.size()));
        m_vertices.insert(m_vertices.end(), tile.m_vertices.begin(), tile.m_vertices.end());
        m_indices.insert(m_indices.end(), tile.m_indices.begin(), tile.m_indices.end());
        m_meshlet_vertices.insert(m_meshlet_vertices.end(), tile.m_meshlet_vertices.begin(), tile.m_meshlet_vertices.end());
        m_meshlet_micro_indices.insert(m_meshlet_micro_indices.end(), tile.m_meshlet_micro_indices.begin(), tile.m_meshlet_micro_indices.end());
        m_sub_meshes[sub_mesh_index] = move(sub);
    }

    void Mesh::AddResidentGeometry(vector<RHI_Vertex_PosTexNorTan>& vertices, vector<uint32_t>& indices, const uint32_t sub_mesh_index)
    {
        // build per-range meshlets, this also repacks indices into meshlet-contiguous order and returns the range aabb the meshlet bounds were quantized against
        // heavy work, kept outside the mesh mutex so concurrent AddResidentGeometry calls run in parallel
        vector<Sb_MeshletBounds> lod_meshlets;
        vector<uint32_t>         lod_unique_vertices;
        vector<uint32_t>         lod_micro_indices;
        BoundingBox              lod_aabb;
        generated_cache::Hash cache_hash;
        cache_hash.Add(uint32_t(1)); // meshlet generator/cache version
        cache_hash.Add(sizeof(Sb_MeshletBounds));
        const bool scan_meshlets = (m_flags & static_cast<uint32_t>(MeshFlags::PostProcessMeshletsScan)) != 0;
        if (scan_meshlets) cache_hash.Add(uint32_t(0x5343414e));
        const bool use_cache = indices.size() >= 192 && !(m_flags & static_cast<uint32_t>(MeshFlags::PostProcessSkipCache));
        if (use_cache)
        {
            cache_hash.Add(vertices);
            cache_hash.Add(indices);
        }
        const auto cache_path = generated_cache::Path(World::GetResourceDirectory(), "meshlets", cache_hash.value);
        vector<Vector3> cached_bounds;
        const bool cached = use_cache && generated_cache::Load(cache_path, cache_hash.value,
            lod_meshlets, lod_unique_vertices, lod_micro_indices, cached_bounds, indices);
        if (cached && cached_bounds.size() == 2)
        {
            lod_aabb = BoundingBox(cached_bounds[0], cached_bounds[1]);
        }
        else
        {
            geometry_processing::build_meshlets(vertices, indices, lod_meshlets,
                lod_unique_vertices, lod_micro_indices, lod_aabb, scan_meshlets);
            cached_bounds = {lod_aabb.GetMin(), lod_aabb.GetMax()};
            if (use_cache) generated_cache::Save(cache_path, cache_hash.value,
                lod_meshlets, lod_unique_vertices, lod_micro_indices, cached_bounds, indices);
        }

        GeometryRange range;
        range.vertex_count  = static_cast<uint32_t>(vertices.size());
        range.index_count   = static_cast<uint32_t>(indices.size());
        range.aabb          = lod_aabb;
        range.meshlet_count = static_cast<uint32_t>(lod_meshlets.size());

        // append geometry, offsets are computed inside the lock so concurrent appends produce correct values
        {
            lock_guard lock(m_mutex);

            range.vertex_offset  = static_cast<uint32_t>(m_vertices.size());
            range.index_offset   = static_cast<uint32_t>(m_indices.size());
            range.meshlet_offset = static_cast<uint32_t>(m_meshlets.size());

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

            auto& sub = m_sub_meshes[sub_mesh_index];
            sub.root = range;
            if (range.meshlet_count) sub.SetResidentRoot();
            else sub.clusters.clear();
        }
    }

    bool GeometryPageSource::Read(vector<uint8_t>& result) const
    {
        return GeometryPageReader().Read(*this, result);
    }

    bool GeometryPage::Decode(span<const uint8_t> bytes)
    {
        if (!generated_cache::Decode(bytes, vertices, indices, meshlets, remaps, micro) ||
            vertices.empty() || vertices.size() > max_corners || indices.empty() ||
            indices.size() > max_corners || indices.size() % 3 || micro.size() != indices.size() ||
            remaps.size() > max_corners || meshlets.empty() || meshlets.size() > max_triangles) return false;
        for (uint32_t i : indices) if (i >= vertices.size()) return false;
        for (uint32_t i : remaps) if (i >= vertices.size()) return false;
        for (const auto& m : meshlets)
        {
            const uint32_t count = m.first_vertex_vert_count >> MESHLET_VERT_COUNT_SHIFT;
            const uint32_t corners = 3 * (m.first_index_tri_count >> MESHLET_TRI_COUNT_SHIFT);
            if (!count || count > MESHLET_MAX_VERTICES || !corners || corners > 3 * MESHLET_MAX_TRIANGLES ||
                (m.first_vertex_vert_count & MESHLET_FIRST_VERTEX_MASK) + count > remaps.size() ||
                (m.first_index_tri_count & MESHLET_FIRST_INDEX_MASK) + corners > indices.size() ||
                uint64_t(m.first_micro) + corners > micro.size()) return false;
            for (uint32_t j = 0; j < corners; ++j) if (micro[m.first_micro + j] >= count) return false;
        }
        return true;
    }

    // Keep a folded leaf card in a single streaming page. The tree builder emits
    // four triangles per card, but native meshes may have reordered their indices.
    static bool order_leaf_cards(const vector<RHI_Vertex_PosTexNorTan>& vertices, const vector<uint32_t>& indices,
        vector<uint32_t>& ordered)
    {
        vector<uint32_t> parents(vertices.size());
        for (uint32_t i = 0; i < parents.size(); ++i) parents[i] = i;
        auto root = [&](uint32_t v)
        {
            while (parents[v] != v) { parents[v] = parents[parents[v]]; v = parents[v]; }
            return v;
        };
        for (size_t i = 0; i < indices.size(); i += 3)
        {
            const auto a = root(indices[i]);
            parents[root(indices[i + 1])] = a;
            parents[root(indices[i + 2])] = a;
        }
        struct Card { array<uint32_t, 12> corners; uint32_t count = 0; };
        vector<Card> cards;
        vector<uint32_t> ids(vertices.size(), UINT32_MAX);
        for (uint32_t v : indices)
        {
            auto& id = ids[root(v)];
            if (id == UINT32_MAX) { id = static_cast<uint32_t>(cards.size()); cards.emplace_back(); }
            auto& card = cards[id];
            if (card.count == card.corners.size()) return false;
            card.corners[card.count++] = v;
        }
        vector<Vector3> centers;
        for (const auto& card : cards)
        {
            if (card.count != card.corners.size()) return false;
            Vector3 center = Vector3::Zero;
            for (auto v : card.corners) center += vertices[v].get_position();
            centers.push_back(center / static_cast<float>(card.count));
        }
        vector<uint32_t> remap(cards.size());
        meshopt_spatialSortRemap(remap.data(), &centers[0].x, centers.size(), sizeof(Vector3));
        for (size_t i = 0; i < cards.size(); ++i)
            copy(cards[i].corners.begin(), cards[i].corners.end(), ordered.begin() + 12 * remap[i]);
        return true;
    }

    // Aggregate neighbouring cards in the same atlas material. Enlarge the
    // representative around the group's centre to retain projected leaf area.
    static void reduce_leaf_cards(const GeometryPage& page, size_t stride, vector<uint32_t>& indices,
        vector<RHI_Vertex_PosTexNorTan>& vertices, float& error)
    {
        vertices = page.vertices;
        indices.clear();
        for (size_t begin = 0; begin < page.indices.size(); begin += 12 * stride)
        {
            const size_t end = min(begin + 12 * stride, page.indices.size());
            BoundingBox box;
            Vector3 center = Vector3::Zero;
            for (size_t i = begin; i < end; ++i)
            {
                const auto point = page.vertices[page.indices[i]].get_position();
                center += point;
                box.Merge(BoundingBox(point, point));
            }
            center = center / static_cast<float>(end - begin);
            size_t best = begin;
            float nearest = FLT_MAX;
            Vector3 pivot;
            for (size_t i = begin; i < end; i += 12)
            {
                Vector3 c = Vector3::Zero;
                for (size_t j = i; j < i + 12; ++j) c += page.vertices[page.indices[j]].get_position();
                c = c / 12.0f;
                const float distance = Vector3::DistanceSquared(c, center);
                if (distance < nearest) { nearest = distance; best = i; pivot = c; }
            }
            const float scale = sqrt(static_cast<float>(end - begin) / 12.0f);
            for (size_t j = best; j < best + 12; ++j)
            {
                const uint32_t v = page.indices[j];
                auto point = center + (page.vertices[v].get_position() - pivot) * scale;
                point = Vector3::Min(box.GetMax(), Vector3::Max(box.GetMin(), point));
                vertices[v].set_position(point);
                indices.push_back(v);
            }
            // Measure how far the folded cards moved in both directions. The
            // canopy's diameter overstates the error for overlapping leaves.
            array<float, 12> nearest_source;
            nearest_source.fill(FLT_MAX);
            float max_distance_sq = 0;
            for (size_t i = begin; i < end; ++i)
            {
                const auto source = page.vertices[page.indices[i]].get_position();
                float nearest_proxy = FLT_MAX;
                for (size_t j = 0; j < nearest_source.size(); ++j)
                {
                    const auto proxy = vertices[page.indices[best + j]].get_position();
                    const float distance = Vector3::DistanceSquared(source, proxy);
                    nearest_source[j] = min(nearest_source[j], distance);
                    nearest_proxy = min(nearest_proxy, distance);
                }
                max_distance_sq = max(max_distance_sq, nearest_proxy);
            }
            error = max(error, sqrtf(max(max_distance_sq, *max_element(nearest_source.begin(), nearest_source.end()))));
        }
    }

    bool Mesh::AddVirtualGeometry(const vector<RHI_Vertex_PosTexNorTan>& vertices,
        const vector<uint32_t>& indices, uint32_t sub_mesh_index, bool refine_small, bool foliage)
    {
        if (indices.empty() || indices.size() % 3 ||
            vertices.empty() || m_skeleton || m_dynamic) return false;

        // A small surface fits in the resident root. Preserve the caller's vertex
        // order and skip building refinement pages that would save no residency.
        if (!foliage && !refine_small && indices.size() <= GeometryPage::max_corners)
        {
            auto root_vertices = vertices;
            auto root_indices = indices;
            AddResidentGeometry(root_vertices, root_indices, sub_mesh_index);
            return true;
        }

        vector<uint32_t> ordered(indices.size());
        foliage = foliage && order_leaf_cards(vertices, indices, ordered);
        if (!foliage) meshopt_spatialSortTriangles(ordered.data(), indices.data(), indices.size(), vertices[0].pos,
            vertices.size(), sizeof(vertices[0]));
        vector<uint32_t> local_index(vertices.size(), UINT32_MAX);
        vector<RHI_Vertex_PosTexNorTan> proxy_vertices;
        vector<uint32_t> proxy_indices;
        vector<GeometryCluster> clusters;
        vector<Sb_MeshletBounds> bounds;
        vector<uint32_t> remaps, micro;
        const BoundingBox root_bounds(vertices.data(), static_cast<uint32_t>(vertices.size()));
        for (size_t first = 0; first < ordered.size(); first += GeometryPage::max_corners)
        {
            GeometryPage page;
            const size_t end = min(ordered.size(), first + GeometryPage::max_corners);
            for (size_t j = first; j < end; ++j)
            {
                const uint32_t v = ordered[j];
                if (local_index[v] == UINT32_MAX)
                {
                    local_index[v] = static_cast<uint32_t>(page.vertices.size());
                    page.vertices.push_back(vertices[v]);
                }
                page.indices.push_back(local_index[v]);
            }
            for (size_t j = first; j < end; ++j) local_index[ordered[j]] = UINT32_MAX;

            GeometryCluster cluster;
            cluster.bounds = BoundingBox(page.vertices.data(), static_cast<uint32_t>(page.vertices.size()));
            vector<uint32_t> coarse = page.indices;
            vector<array<float, 5>> attributes;
            attributes.reserve(page.vertices.size());
            for (const auto& v : page.vertices)
            {
                const auto uv = v.get_uv();
                const auto n = v.get_normal();
                attributes.push_back({uv.x, uv.y, n.x, n.y, n.z});
            }
            constexpr float weights[] = {0.5f, 0.5f, 0.25f, 0.25f, 0.25f};
            vector<RHI_Vertex_PosTexNorTan> representative_vertices;
            for (size_t step = 2, target = page.indices.size() / 2; target >= 3; step *= 2, target /= 2)
            {
                vector<uint32_t> reduced;
                vector<RHI_Vertex_PosTexNorTan> next_vertices;
                float error = 0;
                if (foliage)
                {
                    if (step > 16 || page.indices.size() <= 12) break;
                    reduce_leaf_cards(page, step, reduced, next_vertices, error);
                }
                else
                {
                    reduced.resize(page.indices.size());
                    // Bound distortion in the permanently resident surface, which also
                    // supplies ray tracing and remains visible under streaming pressure.
                    const bool preserve_shape = !(m_flags & static_cast<uint32_t>(MeshFlags::PostProcessOptimize));
                    const float error_limit = preserve_shape && !refine_small ? cluster.bounds.GetSize().Length() * 0.02f : FLT_MAX;
                    const size_t count = meshopt_simplifyWithAttributes(reduced.data(), page.indices.data(), page.indices.size(),
                        page.vertices[0].pos, page.vertices.size(), sizeof(page.vertices[0]),
                        attributes.front().data(), sizeof(attributes[0]), weights, 5, nullptr, target,
                        error_limit, (refine_small && ordered.size() <= GeometryPage::max_corners ? 0 : meshopt_SimplifyLockBorder) | meshopt_SimplifyErrorAbsolute, &error);
                    reduced.resize(count);
                }
                if (reduced.empty() || reduced.size() >= coarse.size() * 0.85f) break;

                GeometryPage detail;
                detail.indices = coarse;
                detail.vertices.resize(page.vertices.size());
                detail.vertices.resize(meshopt_optimizeVertexFetch(detail.vertices.data(), detail.indices.data(),
                    detail.indices.size(), (representative_vertices.empty() ? page.vertices : representative_vertices).data(), page.vertices.size(), sizeof(page.vertices[0])));
                BoundingBox quantization;
                geometry_processing::build_meshlets(detail.vertices, detail.indices, detail.meshlets,
                    detail.remaps, detail.micro, quantization, false, &cluster.bounds);
                auto source = make_shared<GeometryPageSource>();
                source->bytes = generated_cache::Encode(detail.vertices, detail.indices, detail.meshlets, detail.remaps, detail.micro);
                source->size = static_cast<uint32_t>(source->bytes.size());
                source->checksum = generated_cache::HashBytes(source->bytes.data(), source->size);
                cluster.refinements.push_back({cluster.error, move(source)});
                cluster.error = max(error, cluster.error + cluster.bounds.GetExtents().Length() / 256.0f);
                representative_vertices = move(next_vertices);
                coarse = move(reduced);
            }
            vector<RHI_Vertex_PosTexNorTan> compact(page.vertices.size());
            compact.resize(meshopt_optimizeVertexFetch(compact.data(), coarse.data(), coarse.size(),
                (representative_vertices.empty() ? page.vertices : representative_vertices).data(), page.vertices.size(), sizeof(compact[0])));
            vector<Sb_MeshletBounds> group_meshlets;
            vector<uint32_t> group_remaps, group_micro;
            BoundingBox quantization;
            geometry_processing::build_meshlets(compact, coarse, group_meshlets, group_remaps, group_micro,
                quantization, false, &root_bounds);
            const uint32_t vertex_base = static_cast<uint32_t>(proxy_vertices.size());
            const uint32_t index_base = static_cast<uint32_t>(proxy_indices.size());
            cluster.first_meshlet = static_cast<uint32_t>(bounds.size());
            cluster.meshlet_count = static_cast<uint32_t>(group_meshlets.size());
            cluster.index_count = static_cast<uint32_t>(coarse.size());
            for (auto m : group_meshlets)
            {
                const uint32_t index = (m.first_index_tri_count & MESHLET_FIRST_INDEX_MASK) + index_base;
                if (index > MESHLET_FIRST_INDEX_MAX || remaps.size() + group_remaps.size() > MESHLET_FIRST_VERTEX_MAX)
                    return false;
                m.first_index_tri_count = (m.first_index_tri_count & ~MESHLET_FIRST_INDEX_MASK) | index;
                offset_meshlet_unique_ranges(m, static_cast<uint32_t>(remaps.size()), static_cast<uint32_t>(micro.size()));
                bounds.push_back(m);
            }
            for (uint32_t i : coarse) proxy_indices.push_back(vertex_base + i);
            for (uint32_t i : group_remaps) remaps.push_back(vertex_base + i);
            micro.insert(micro.end(), group_micro.begin(), group_micro.end());
            proxy_vertices.insert(proxy_vertices.end(), compact.begin(), compact.end());
            clusters.push_back(move(cluster));
        }
        // The proxy and all cluster draws share vertices, indices and meshlets.
        // A cluster narrows only the meshlet range, so direct passes can still
        // draw the entire proxy in one call using the same resident allocation.
        lock_guard lock(m_mutex);
        if (m_meshlet_vertices.size() + remaps.size() > MESHLET_FIRST_VERTEX_MAX) return false;
        GeometryRange root = {};
        root.aabb = root_bounds;
        root.vertex_offset = static_cast<uint32_t>(m_vertices.size());
        root.vertex_count = static_cast<uint32_t>(proxy_vertices.size());
        root.index_offset = static_cast<uint32_t>(m_indices.size());
        root.index_count = static_cast<uint32_t>(proxy_indices.size());
        root.meshlet_offset = static_cast<uint32_t>(m_meshlets.size());
        root.meshlet_count = static_cast<uint32_t>(bounds.size());
        for (auto& m : bounds)
            offset_meshlet_unique_ranges(m, static_cast<uint32_t>(m_meshlet_vertices.size()), static_cast<uint32_t>(m_meshlet_micro_indices.size()));
        m_indices.insert(m_indices.end(), proxy_indices.begin(), proxy_indices.end());
        m_vertices.insert(m_vertices.end(), proxy_vertices.begin(), proxy_vertices.end());
        m_meshlets.insert(m_meshlets.end(), bounds.begin(), bounds.end());
        m_meshlet_vertices.insert(m_meshlet_vertices.end(), remaps.begin(), remaps.end());
        m_meshlet_micro_indices.insert(m_meshlet_micro_indices.end(), micro.begin(), micro.end());
        auto& sub = m_sub_meshes[sub_mesh_index];
        sub.root = root;
        sub.clusters = move(clusters);
        return true;
    }

    void Mesh::ReserveSubMeshes(const uint32_t count)
    {
        lock_guard lock(m_mutex);
        if (m_sub_meshes.size() < count)
        {
            m_sub_meshes.resize(count);
        }
    }

    void Mesh::AddGeometry(vector<RHI_Vertex_PosTexNorTan>& vertices, vector<uint32_t>& indices, uint32_t* sub_mesh_index)
    {
        // create a sub-mesh slot, locked because concurrent AddGeometry calls share m_sub_meshes
        uint32_t current_sub_mesh_index;
        {
            lock_guard lock(m_mutex);
            current_sub_mesh_index = static_cast<uint32_t>(m_sub_meshes.size());
            m_sub_meshes.emplace_back();
        }

        // delegate the heavy work to the explicit-index overload so both code paths stay identical
        AddGeometry(vertices, indices, current_sub_mesh_index);

        if (sub_mesh_index)
        {
            *sub_mesh_index = current_sub_mesh_index;
        }
    }

    void Mesh::AddGeometry(vector<RHI_Vertex_PosTexNorTan>& vertices, vector<uint32_t>& indices, const uint32_t sub_mesh_index_in)
    {
        SP_ASSERT(sub_mesh_index_in < m_sub_meshes.size());
        if (!m_dynamic && !m_skeleton)
        {
            if (m_flags & static_cast<uint32_t>(MeshFlags::PostProcessOptimize))
                geometry_processing::weld_and_optimize(vertices, indices);
            if (AddVirtualGeometry(vertices, indices, sub_mesh_index_in)) return;
        }
        AddResidentGeometry(vertices, indices, sub_mesh_index_in);
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
            aabb,
            (m_flags & static_cast<uint32_t>(MeshFlags::PostProcessMeshletsScan)) != 0
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
                !m_sub_meshes[0].HasGeometry()
            )
            {
                return false;
            }
        }

        // Keep packed offsets mesh-local, including dynamic updates.
        const vector<Sb_MeshletBounds>& gpu_meshlets = meshlets;

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
            m_cpu_geometry_released = false;
            m_vertices              = vertices;
            m_indices               = indices;
            m_meshlets              = meshlets;
            m_meshlet_vertices      = unique_vertices;
            m_meshlet_micro_indices = micro_indices;

            m_sub_meshes[0].parent_page.reset();
            GeometryRange& range      = m_sub_meshes[0].root;
            range.vertex_offset = 0;
            range.vertex_count  =
                static_cast<uint32_t>(m_vertices.size());
            range.index_offset = 0;
            range.index_count  =
                static_cast<uint32_t>(m_indices.size());
            range.meshlet_offset = 0;
            range.meshlet_count  =
                static_cast<uint32_t>(m_meshlets.size());
            range.aabb = aabb;
            if (range.meshlet_count) m_sub_meshes[0].SetResidentRoot();
            else m_sub_meshes[0].clusters.clear();

            m_object_size = GetMemoryUsage();
        }

        InvalidateAllBlas();
        return true;
    }

    bool Mesh::UpdateVertices(const vector<RHI_Vertex_PosTexNorTan>& vertices, const bool invalidate_blas)
    {
        lock_guard lock(m_mutex);
        restore_cpu_geometry_locked();
        if (vertices.empty() || vertices.size() != m_vertices.size() ||
            m_sub_meshes.size() != 1 || !m_sub_meshes[0].HasGeometry() ||
            (!m_sub_meshes[0].clusters.empty() && !m_sub_meshes[0].HasResidentRootOnly()))
        {
            return false;
        }

        m_vertices = vertices;
        m_sub_meshes[0].parent_page.reset();
        GeometryRange& range = m_sub_meshes[0].root;
        range.aabb = BoundingBox(m_vertices.data(), static_cast<uint32_t>(m_vertices.size()));
        if (m_sub_meshes[0].HasResidentRootOnly()) m_sub_meshes[0].clusters.front().bounds = range.aabb;
        const Vector3 minimum = range.aabb.GetMin();
        const Vector3 extent = range.aabb.GetMax() - minimum;
        const Vector3 safe_extent(max(extent.x, 1e-8f), max(extent.y, 1e-8f), max(extent.z, 1e-8f));
        const float diagonal = safe_extent.Length();
        auto unorm16 = [](float value)
        {
            return static_cast<uint32_t>(roundf(clamp(value, 0.0f, 1.0f) * 65535.0f));
        };

        // The index and meshlet topology has not changed. Refit conservative
        // spheres from each cluster's referenced vertices instead of clustering again.
        for (Sb_MeshletBounds& bounds : m_meshlets)
        {
            const uint32_t first = bounds.first_vertex_vert_count & MESHLET_FIRST_VERTEX_MASK;
            const uint32_t count = bounds.first_vertex_vert_count >> MESHLET_VERT_COUNT_SHIFT;
            BoundingBox cluster;
            for (uint32_t i = 0; i < count; ++i)
            {
                const auto& vertex = m_vertices[m_meshlet_vertices[first + i]];
                const Vector3 point(vertex.pos[0], vertex.pos[1], vertex.pos[2]);
                cluster.Merge(BoundingBox(point, point));
            }
            const Vector3 center = cluster.GetCenter();
            const uint32_t cx = unorm16((center.x - minimum.x) / safe_extent.x);
            const uint32_t cy = unorm16((center.y - minimum.y) / safe_extent.y);
            const uint32_t cz = unorm16((center.z - minimum.z) / safe_extent.z);
            // Cover center rounding and round radius upwards, matching the GPU decoder.
            const float radius = cluster.GetSize().Length() * 0.5f + diagonal * (1.5f / 65535.0f);
            const uint32_t packed_radius = static_cast<uint32_t>(ceilf(clamp(radius / diagonal, 0.0f, 1.0f) * 65535.0f));
            bounds.center_xy = cx | (cy << 16);
            bounds.center_z_radius = cz | (packed_radius << 16);
            bounds.cone_axis_cutoff = 127u << 24; // old normal cones are invalid after deformation
        }
        GeometryBuffer::UpdateVertices(m_vertices.data(), m_global_vertex_offset, static_cast<uint32_t>(m_vertices.size()));
        GeometryBuffer::UpdateMeshletBounds(m_meshlets.data(), m_global_meshlet_offset, static_cast<uint32_t>(m_meshlets.size()));
        if (invalidate_blas)
        {
            InvalidateAllBlas();
        }
        return true;
    }

    void Mesh::UploadVertexRange(uint32_t vertex_offset, uint32_t vertex_count)
    {
        RestoreCpuGeometry();
        if (vertex_count == 0 || vertex_offset + vertex_count > m_vertices.size())
        {
            return;
        }

        GeometryBuffer::UpdateVertices(
            m_vertices.data() + vertex_offset,
            m_global_vertex_offset + vertex_offset,
            vertex_count
        );
    }

    bool Mesh::EditClusterPages(uint32_t sub_mesh_index, const BoundingBox& region,
        const function<bool(RHI_Vertex_PosTexNorTan&)>& edit)
    {
        lock_guard lock(m_mutex);
        vector<GeometryCluster*> affected;
        for (auto& cluster : m_sub_meshes[sub_mesh_index].clusters)
        {
            if (!cluster.refinements.empty() && cluster.bounds.Intersects(region) != Intersection::Outside)
                affected.push_back(&cluster);
        }
        if (affected.empty()) return false;
        auto edit_cluster = [&](GeometryCluster& cluster, GeometryPageReader& reader)
        {
            vector<GeometryRefinement> levels;
            BoundingBox bounds;
            float displacement = 0.0f;
            for (const auto& level : cluster.refinements)
            {
                vector<uint8_t> bytes;
                GeometryPage page;
                if (!reader.Read(*level.page, bytes) || !page.Decode(bytes))
                {
                    cluster.refinements.clear();
                    cluster.error = 0;
                    SP_LOG_ERROR("Using the edited resident surface after failing to read a geometry page in '%s'", m_object_name.c_str());
                    return true;
                }
                bool touched = false;
                for (auto& vertex : page.vertices)
                {
                    const Vector3 before = vertex.get_position();
                    if (!edit(vertex)) continue;
                    touched = true;
                    displacement = max(displacement, (vertex.get_position() - before).Length());
                }
                // The finest page contains every vertex used by all coarser pages.
                if (levels.empty())
                {
                    if (!touched) break;
                    bounds = BoundingBox(page.vertices.data(), static_cast<uint32_t>(page.vertices.size()));
                }
                // Height edits preserve triangle membership, indices and mesh-shader remaps.
                geometry_processing::refit_meshlet_bounds(page.vertices, page.indices, page.meshlets, bounds);
                auto source = make_shared<GeometryPageSource>();
                source->bytes = generated_cache::Encode(page.vertices, page.indices, page.meshlets, page.remaps, page.micro);
                source->size = static_cast<uint32_t>(source->bytes.size());
                source->checksum = generated_cache::HashBytes(source->bytes.data(), source->size);
                levels.push_back({levels.empty() ? 0.0f : level.error + 2.0f * displacement, move(source)});
            }
            if (levels.size() != cluster.refinements.size()) return false;
            cluster.refinements = move(levels);
            cluster.bounds = bounds;
            cluster.error += 2.0f * displacement;
            return true;
        };
        // Workers own disjoint clusters and read consecutive pages from the same file handle.
        constexpr uint32_t batch_size = 16;
        const uint32_t count = static_cast<uint32_t>(affected.size());
        atomic<bool> changed{false};
        ThreadPool::ParallelLoop([&](uint32_t begin, uint32_t end)
        {
            GeometryPageReader reader;
            bool edited = false;
            for (uint32_t i = begin * batch_size; i < min(end * batch_size, count); ++i)
                edited |= edit_cluster(*affected[i], reader);
            if (edited) changed.store(true, memory_order_relaxed);
        }, (count + batch_size - 1) / batch_size);
        if (changed.load(memory_order_relaxed)) InvalidateBlas(sub_mesh_index);
        return changed.load(memory_order_relaxed);
    }

    void Mesh::RefreshGeometryBounds(uint32_t sub_mesh_index)
    {
        if (sub_mesh_index >= m_sub_meshes.size())
        {
            return;
        }

        lock_guard lock(m_mutex);
        restore_cpu_geometry_locked();
        SubMesh& sub_mesh = m_sub_meshes[sub_mesh_index];
        sub_mesh.parent_page.reset();
        if (sub_mesh.HasGeometry())
        {
            auto& range = sub_mesh.root;
            if (range.vertex_count == 0 || range.vertex_offset + range.vertex_count > m_vertices.size())
            {
                return;
            }

            range.aabb = BoundingBox(m_vertices.data() + range.vertex_offset, range.vertex_count);
            if (sub_mesh.HasResidentRootOnly()) sub_mesh.clusters.front().bounds = range.aabb;
            else for (auto& cluster : sub_mesh.clusters)
            {
                if (cluster.refinements.empty())
                {
                    BoundingBox bounds;
                    for (uint32_t j = 0; j < cluster.meshlet_count; ++j)
                    {
                        const auto& m = m_meshlets[range.meshlet_offset + cluster.first_meshlet + j];
                        const uint32_t first = m.first_vertex_vert_count & MESHLET_FIRST_VERTEX_MASK;
                        const uint32_t count = m.first_vertex_vert_count >> MESHLET_VERT_COUNT_SHIFT;
                        for (uint32_t k = 0; k < count; ++k)
                        {
                            const auto p = m_vertices[range.vertex_offset + m_meshlet_vertices[first + k]].get_position();
                            bounds.Merge(BoundingBox(p, p));
                        }
                    }
                    cluster.bounds = bounds;
                }
                range.aabb.Merge(cluster.bounds);
            }

            if (range.meshlet_count == 0 || range.meshlet_offset + range.meshlet_count > m_meshlets.size())
            {
                return;
            }

            vector<Sb_MeshletBounds> gpu_meshlets(range.meshlet_count);
            for (uint32_t i = 0; i < range.meshlet_count; i++)
            {
                auto& bounds = m_meshlets[range.meshlet_offset + i];
                bounds.center_z_radius |= (0xFFFFu << 16);
                bounds.cone_axis_cutoff = 127u << 24;
                gpu_meshlets[i] = bounds;
            }

            GeometryBuffer::UpdateMeshletBounds(
                gpu_meshlets.data(),
                m_global_meshlet_offset + range.meshlet_offset,
                range.meshlet_count
            );
        }
    }

    uint32_t Mesh::GetVertexCount() const
    {
        return m_cpu_geometry_released ? m_released_vertex_count : static_cast<uint32_t>(m_vertices.size());
    }

    uint32_t Mesh::GetIndexCount() const
    {
        return m_cpu_geometry_released ? m_released_index_count : static_cast<uint32_t>(m_indices.size());
    }

    uint32_t Mesh::GetDefaultFlags()
    {
        return
            static_cast<uint32_t>(MeshFlags::ImportRemoveRedundantData) |
            static_cast<uint32_t>(MeshFlags::ImportGenerateSmoothNormals) |
            static_cast<uint32_t>(MeshFlags::PostProcessNormalizeScale) |
            static_cast<uint32_t>(MeshFlags::PostProcessOptimize);
    }

    shared_ptr<Mesh> Mesh::CreateCrowdGeometry()
    {
        if (!m_skeletal_mesh_binding || none_of(m_sub_meshes.begin(), m_sub_meshes.end(), [](const SubMesh& sub)
            { return sub.parent_page && !sub.parent_page->skin_indices.empty(); }))
            return static_pointer_cast<Mesh>(shared_from_this());

        RestoreCpuGeometry();
        auto crowd = make_shared<Mesh>();
        crowd->m_dynamic = true;
        crowd->m_flags = m_flags;
        crowd->m_skeleton = m_skeleton;
        crowd->m_animation_clips = m_animation_clips;
        crowd->m_skeletal_mesh_binding = make_unique<SkeletalMeshBinding>();
        crowd->ReserveSubMeshes(static_cast<uint32_t>(m_sub_meshes.size()));
        for (uint32_t i = 0; i < m_sub_meshes.size(); ++i)
        {
            const auto& sub = m_sub_meshes[i];
            const auto& range = sub.root;
            if (range.index_count == 0) continue;
            vector<uint32_t> indices;
            if (sub.parent_page && !sub.parent_page->skin_indices.empty())
                indices = sub.parent_page->skin_indices;
            else
                indices.assign(m_indices.begin() + range.index_offset, m_indices.begin() + range.index_offset + range.index_count);

            // Compact the rig with exactly the same remap as the render vertices.
            // Every walker then skins and uploads only the vertices it draws.
            vector<uint32_t> remap(range.vertex_count);
            const size_t count = meshopt_optimizeVertexFetchRemap(remap.data(), indices.data(), indices.size(), range.vertex_count);
            vector<RHI_Vertex_PosTexNorTan> vertices(count);
            meshopt_remapIndexBuffer(indices.data(), indices.data(), indices.size(), remap.data());
            meshopt_remapVertexBuffer(vertices.data(), m_vertices.data() + range.vertex_offset, range.vertex_count,
                sizeof(vertices[0]), remap.data());
            crowd->AddResidentGeometry(vertices, indices, i);
            if (const auto* source = m_skeletal_mesh_binding->GetSectionBySubMeshIndex(i))
            {
                SkeletalMeshSection section = *source;
                section.vertex_input_offset = crowd->m_sub_meshes[i].root.vertex_offset;
                section.vertex_count = static_cast<uint32_t>(count);
                section.influences.resize(count);
                meshopt_remapVertexBuffer(section.influences.data(), source->influences.data(), range.vertex_count,
                    sizeof(section.influences[0]), remap.data());
                crowd->m_skeletal_mesh_binding->AddSection(move(section));
            }
        }
        crowd->SetObjectName(GetObjectName());
        crowd->CreateGpuBuffers();
        // One immutable pose stream serves every instance of the looping walk.
        // IK, transitions and ragdolls continue to write the instance's private vertices.
        for (uint32_t i = 0; i < crowd->m_animation_clips.size(); ++i)
        {
            const auto& clip = crowd->m_animation_clips[i];
            if (clip.name != "walk" || !(clip.duration_seconds > 0.0f)) continue;
            const float samples = ceilf(clip.duration_seconds * 120.0f);
            if (samples * crowd->m_vertices.size() * sizeof(crowd->m_vertices[0]) > 32.0f * 1024 * 1024) break;
            auto cache = make_shared<CrowdClip>();
            cache->duration = clip.duration_seconds;
            cache->clip_index = static_cast<int32_t>(i);
            cache->frame_count = static_cast<uint32_t>(samples);
            cache->vertex_count = static_cast<uint32_t>(crowd->m_vertices.size());
            vector<RHI_Vertex_PosTexNorTan> poses, posed;
            poses.reserve(static_cast<size_t>(cache->frame_count) * cache->vertex_count);
            vector<Matrix> local, global(crowd->m_skeleton->joint_count), palette;
            for (uint32_t frame = 0; frame < cache->frame_count; ++frame)
            {
                float time = clip.duration_seconds * frame / cache->frame_count;
                if (!animation_evaluate::SampleLocals(clip, *crowd->m_skeleton, time, local, true)) break;
                crowd->m_skeleton->ComputeGlobalPose(local, global);
                if (!animation_evaluate::SkinMesh(*crowd->m_skeletal_mesh_binding, global, crowd->m_skeleton->bind_inverse_global_matrices,
                    crowd->m_vertices, posed, palette)) break;
                poses.insert(poses.end(), posed.begin(), posed.end());
            }
            if (poses.size() == static_cast<size_t>(cache->vertex_count) * cache->frame_count)
            {
                cache->first_vertex = GeometryBuffer::AppendVertices(poses.data(), static_cast<uint32_t>(poses.size()));
                crowd->m_crowd_clip = move(cache);
            }
            break;
        }
        SP_LOG_INFO("Crowd rig '%s' uses %u of %u source vertices", GetObjectName().c_str(),
            static_cast<uint32_t>(crowd->m_vertices.size()), static_cast<uint32_t>(m_vertices.size()));
        return crowd;
    }

    bool Mesh::SelectCrowdPose(int32_t clip_index, float time)
    {
        if (!m_crowd_clip) return false;
        uint32_t selected = UINT32_MAX;
        if (clip_index == m_crowd_clip->clip_index && std::isfinite(time))
        {
            const auto& clip = *m_crowd_clip;
            const uint32_t sample = static_cast<uint32_t>(roundf(max(0.0f, time) * clip.frame_count / clip.duration)) % clip.frame_count;
            const uint32_t offset = clip.first_vertex + sample * clip.vertex_count;
            if (GeometryBuffer::IsResident(offset + clip.vertex_count, 0)) selected = offset;
        }
        if (selected != m_pose_offset)
        {
            if (m_pose_changed_frame != Renderer::GetFrameNumber()) m_previous_pose_offset = GetRenderVertexOffset();
            m_pose_changed_frame = Renderer::GetFrameNumber();
            m_pose_offset = selected;
        }
        return selected != UINT32_MAX;
    }

    uint32_t Mesh::GetRenderPreviousDelta(uint32_t vertex_offset) const
    {
        if (m_crowd_clip && m_pose_changed_frame == Renderer::GetFrameNumber())
            return m_previous_pose_offset - GetRenderVertexOffset();
        return m_pose_offset != UINT32_MAX ? 0u : GeometryBuffer::GetPreviousVertexOffset(vertex_offset);
    }

    shared_ptr<Mesh> Mesh::CreateSkinnedInstance()
    {
        RestoreCpuGeometry();
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
        // A deformed instance owns a separate vertex slice. Rigid pages carry rest-pose vertices.
        for (auto& sub : instance->m_sub_meshes)
            if (!sub.HasResidentRootOnly()) sub.clusters.clear();
        instance->m_meshlets              = m_meshlets;
        instance->m_meshlet_vertices      = m_meshlet_vertices;
        instance->m_meshlet_micro_indices = m_meshlet_micro_indices;
        instance->m_skeleton              = m_skeleton;
        instance->m_crowd_clip            = m_crowd_clip;
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
        instance->m_global_index_offset            = m_global_index_offset;
        instance->m_global_index_capacity          = m_global_index_capacity;
        instance->m_global_meshlet_offset          = m_global_meshlet_offset;
        instance->m_global_meshlet_capacity        = m_global_meshlet_capacity;
        instance->m_global_meshlet_vertex_offset   = m_global_meshlet_vertex_offset;
        instance->m_global_meshlet_vertex_capacity = m_global_meshlet_vertex_capacity;
        instance->m_global_meshlet_micro_offset    = m_global_meshlet_micro_offset;
        instance->m_global_meshlet_micro_capacity  = m_global_meshlet_micro_capacity;

        // skinning writes the same count every frame, padding to the next pow2 uploaded 16k dummy
        // verts per pedestrian and hitching the geometry buffer
        instance->m_global_vertex_capacity = static_cast<uint32_t>(m_vertices.size());
        instance->m_global_vertex_offset   = GeometryBuffer::AppendVertices(
            m_vertices.data(),
            instance->m_global_vertex_capacity
        );
        instance->m_object_size = instance->GetMemoryUsage();
        instance->m_ready_for_blas.store(true, memory_order_release);

        instance->SetObjectName(GetObjectName() + "_skinned_instance");
        return instance;
    }

    void Mesh::BuildParentPages()
    {
        if (m_dynamic) return;
        const bool tree = (m_flags & static_cast<uint32_t>(MeshFlags::FoliageCardClusters)) && m_sub_meshes.size() == 2;
        auto upload = [](GeometryParentPage& proxy, const vector<RHI_Vertex_PosTexNorTan>& surface, vector<uint32_t>& topology)
        {
            vector<uint32_t> remaps, micro;
            vector<Sb_MeshletBounds> meshlets;
            BoundingBox unused;
            geometry_processing::build_meshlets(surface, topology, meshlets, remaps, micro, unused, false, &proxy.bounds);
            proxy.vertex_count = static_cast<uint32_t>(surface.size());
            proxy.index_count = static_cast<uint32_t>(topology.size());
            proxy.meshlet_count = static_cast<uint32_t>(meshlets.size());
            proxy.remaps = GeometryBuffer::AppendMeshletVertices(remaps.data(), static_cast<uint32_t>(remaps.size()));
            proxy.micro = GeometryBuffer::AppendMeshletMicroIndices(micro.data(), static_cast<uint32_t>(micro.size()));
            proxy.meshlet = GeometryBuffer::AppendMeshletBounds(meshlets.data(), proxy.meshlet_count);
            if (!proxy.uses_root_vertices)
                proxy.vertices = GeometryBuffer::AppendVertices(surface.data(), proxy.vertex_count);
            proxy.indices = GeometryBuffer::AppendIndices(topology.data(), proxy.index_count);
        };
        if (!tree)
        {
            const bool terrain = (m_flags & static_cast<uint32_t>(MeshFlags::PostProcessPreserveTerrainEdges)) != 0;
            auto build_submesh = [&](uint32_t sub_index)
            {
                auto& sub = m_sub_meshes[sub_index];
                const auto& range = sub.root;
                if (sub.parent_page || range.index_count <= GeometryPage::max_corners || range.vertex_count == 0) return;
                vector<RHI_Vertex_PosTexNorTan> source_vertices;
                vector<uint32_t> source_indices;
                const bool shared_vertices = m_skeletal_mesh_binding || sub.HasResidentRootOnly();
                struct ParentLevel { uint32_t vertices, indices; float error; };
                vector<ParentLevel> levels;
                vector<RHI_Vertex_PosTexNorTan> baked_vertices;
                vector<uint32_t> baked_indices;
                generated_cache::Hash recipe;
                filesystem::path bake_path;
                if (terrain && !shared_vertices && !(m_flags & static_cast<uint32_t>(MeshFlags::PostProcessSkipCache)))
                {
                    // The resident triangles and the finest pages together describe ReadGeometry's input.
                    recipe.Add(uint32_t(1)); // parent simplification policy
                    recipe.Add(uint32_t(MESHOPTIMIZER_VERSION));
                    recipe.Add(GeometryPage::max_corners);
                    recipe.Add(m_flags);
                    recipe.Add(range.aabb.GetMin());
                    recipe.Add(range.aabb.GetMax());
                    recipe.Add(generated_cache::HashBytes(m_vertices.data() + range.vertex_offset,
                        size_t(range.vertex_count) * sizeof(m_vertices[0])));
                    recipe.Add(generated_cache::HashBytes(m_indices.data() + range.index_offset,
                        size_t(range.index_count) * sizeof(uint32_t)));
                    for (const auto& cluster : sub.clusters)
                    {
                        recipe.Add(cluster.index_count);
                        if (cluster.refinements.empty())
                            recipe.Add(m_meshlets[range.meshlet_offset + cluster.first_meshlet].first_index_tri_count);
                        else
                            recipe.Add(cluster.refinements.front().page->checksum);
                    }
                    bake_path = generated_cache::Path(World::GetResourceDirectory(), "parent_pages", recipe.value);
                    if (generated_cache::Load(bake_path, recipe.value, levels, baked_vertices, baked_indices))
                    {
                        size_t v = 0, i = 0;
                        bool valid = levels.size() <= 3;
                        for (const ParentLevel& level : levels)
                        {
                            if (!level.vertices || !level.indices || level.indices % 3 || !isfinite(level.error) ||
                                level.vertices > baked_vertices.size() - v || level.indices > baked_indices.size() - i ||
                                any_of(baked_indices.begin() + i, baked_indices.begin() + i + level.indices,
                                    [&](uint32_t index) { return index >= level.vertices; }))
                            {
                                valid = false;
                                break;
                            }
                            v += level.vertices;
                            i += level.indices;
                        }
                        if (valid && v == baked_vertices.size() && i == baked_indices.size())
                        {
                            auto* next = &sub.parent_page;
                            v = i = 0;
                            for (const ParentLevel& level : levels)
                            {
                                auto parent = make_shared<GeometryParentPage>();
                                parent->bounds = range.aabb;
                                parent->center = range.aabb.GetCenter();
                                parent->radius = range.aabb.GetExtents().Length();
                                parent->error = level.error;
                                vector<RHI_Vertex_PosTexNorTan> surface(baked_vertices.begin() + v, baked_vertices.begin() + v + level.vertices);
                                vector<uint32_t> topology(baked_indices.begin() + i, baked_indices.begin() + i + level.indices);
                                upload(*parent, surface, topology);
                                *next = move(parent);
                                next = &(*next)->coarser;
                                v += level.vertices;
                                i += level.indices;
                            }
                            return;
                        }
                        levels.clear();
                        baked_vertices.clear();
                        baked_indices.clear();
                    }
                }
                // ReadGeometry already welds the borders between independently streamable pages.
                if (!shared_vertices && (!ReadGeometry(sub_index, &source_indices, &source_vertices) || source_indices.empty()))
                    return;
                const auto* first_vertex = shared_vertices ? m_vertices.data() + range.vertex_offset : source_vertices.data();
                const auto* first_index = shared_vertices ? m_indices.data() + range.index_offset : source_indices.data();
                const size_t vertex_count = shared_vertices ? range.vertex_count : source_vertices.size();
                const size_t index_count = shared_vertices ? range.index_count : source_indices.size();
                vector<unsigned char> locked;
                if (m_skeletal_mesh_binding)
                {
                    const auto* section = m_skeletal_mesh_binding->GetSectionBySubMeshIndex(sub_index);
                    if (!section || section->influences.size() != vertex_count) return;
                    // Keep blended skin and the seams between different rigid bones.
                    // Retained vertices always read the live character's pose.
                    locked.assign(vertex_count, 0);
                    for (uint32_t i = 0; i < index_count; i += 3)
                    {
                        const auto bone = section->influences[first_index[i]].bone_indices[0];
                        for (uint32_t j = 0; j < 3; ++j)
                        {
                            const auto& influence = section->influences[first_index[i + j]];
                            if (influence.bone_indices[0] != bone || influence.bone_weights[0] < 0.999f)
                                locked[first_index[i]] = locked[first_index[i + 1]] = locked[first_index[i + 2]] = 1;
                        }
                    }
                }
                const float radius = range.aabb.GetExtents().Length();
                vector<array<float,5>> attributes(vertex_count);
                for (uint32_t i = 0; i < vertex_count; ++i)
                {
                    const auto uv = first_vertex[i].get_uv();
                    const auto normal = first_vertex[i].get_normal();
                    attributes[i] = {uv.x, uv.y, normal.x, normal.y, normal.z};
                }
                const float metric = radius * 0.01f;
                const float weights[] = {metric, metric, metric, metric, metric};
                const size_t targets[] = {GeometryPage::max_corners, 384, 96};
                const float relative_errors[] = {0.002f, 0.016f, 0.075f};
                const unsigned int options = meshopt_SimplifyErrorAbsolute |
                    (m_skeletal_mesh_binding ? 0u : meshopt_SimplifyPermissive) |
                    ((m_flags & static_cast<uint32_t>(MeshFlags::PostProcessPreserveTerrainEdges)) ? meshopt_SimplifyLockBorder : 0u);
                auto* next = &sub.parent_page;
                size_t previous_count = range.index_count;
                for (size_t level = 0; level < (m_skeletal_mesh_binding ? 1u : 3u); ++level)
                {
                    auto parent = make_shared<GeometryParentPage>();
                    parent->bounds = range.aabb;
                    parent->center = range.aabb.GetCenter();
                    parent->radius = radius;
                    parent->uses_root_vertices = shared_vertices;
                    vector<uint32_t> reduced(index_count);
                    const size_t count = meshopt_simplifyWithAttributes(reduced.data(), first_index, index_count,
                        first_vertex[0].pos, vertex_count, sizeof(*first_vertex), attributes[0].data(), sizeof(attributes[0]),
                        weights, 5, locked.empty() ? nullptr : locked.data(), targets[level], radius * relative_errors[level],
                        options, &parent->error);
                    if (count == 0 || count >= previous_count * 0.85) continue;
                    previous_count = count;
                    reduced.resize(count);
                    if (shared_vertices)
                    {
                        vector<RHI_Vertex_PosTexNorTan> surface(first_vertex, first_vertex + vertex_count);
                        upload(*parent, surface, reduced);
                        if (m_skeletal_mesh_binding) parent->skin_indices = move(reduced);
                    }
                    else
                    {
                        vector<RHI_Vertex_PosTexNorTan> compact(vertex_count);
                        compact.resize(meshopt_optimizeVertexFetch(compact.data(), reduced.data(), reduced.size(),
                            source_vertices.data(), vertex_count, sizeof(source_vertices[0])));
                        if (!bake_path.empty())
                        {
                            levels.push_back({static_cast<uint32_t>(compact.size()), static_cast<uint32_t>(reduced.size()), parent->error});
                            baked_vertices.insert(baked_vertices.end(), compact.begin(), compact.end());
                            baked_indices.insert(baked_indices.end(), reduced.begin(), reduced.end());
                        }
                        upload(*parent, compact, reduced);
                    }
                    *next = move(parent);
                    next = &(*next)->coarser;
                }
                if (!bake_path.empty()) generated_cache::Save(bake_path, recipe.value, levels, baked_vertices, baked_indices);
            };
            // Terrain tiles have independent parent chains. The geometry buffer synchronizes each append.
            const uint32_t count = static_cast<uint32_t>(m_sub_meshes.size());
            if (count > 1 && terrain)
            {
                ThreadPool::ParallelLoop([&](uint32_t begin, uint32_t end)
                {
                    for (uint32_t i = begin; i < end; ++i) build_submesh(i);
                }, count);
            }
            else
            {
                for (uint32_t i = 0; i < count; ++i) build_submesh(i);
            }
            return;
        }
        if (m_sub_meshes[1].parent_page) return;
        // Across cluster boundaries the trunk can collapse into a much smaller
        // silhouette. Retain its 3D surface, UVs and normal map.
        vector<uint32_t> bark_indices;
        vector<RHI_Vertex_PosTexNorTan> bark;
        if (!m_sub_meshes[0].parent_page && ReadGeometry(0, &bark_indices, &bark) && bark_indices.size() > 96)
        {
            auto parent = make_shared<GeometryParentPage>();
            parent->bounds = BoundingBox(bark.data(), static_cast<uint32_t>(bark.size()));
            parent->center = parent->bounds.GetCenter();
            parent->radius = parent->bounds.GetExtents().Length();
            vector<uint32_t> reduced(bark_indices.size());
            size_t count = meshopt_simplify(reduced.data(), bark_indices.data(), bark_indices.size(), bark[0].pos,
                bark.size(), sizeof(bark[0]), 96, parent->radius * 0.08f,
                meshopt_SimplifyErrorAbsolute | meshopt_SimplifyPrune, &parent->error);
            if (count && count < m_sub_meshes[0].root.index_count)
            {
                reduced.resize(count);
                vector<RHI_Vertex_PosTexNorTan> compact(bark.size());
                compact.resize(meshopt_optimizeVertexFetch(compact.data(), reduced.data(), reduced.size(),
                    bark.data(), bark.size(), sizeof(bark[0])));
                upload(*parent, compact, reduced);
                m_sub_meshes[0].parent_page = move(parent);
            }
        }
        vector<uint32_t> indices;
        vector<RHI_Vertex_PosTexNorTan> vertices;
        if (!ReadGeometry(1, &indices, &vertices) || indices.size() < 12) return;

        // read optional sidecar material xmls (<stem>_foliage.xml and <stem>_bark.xml) so trees can pre-bake
        // a whole-tree (bark + canopy) direct-rgba impostor that needs zero runtime texture fetches and lets
        // distant trunks cull out completely in phase a
        struct MaterialImpostorMaps
        {
            vector<uint8_t> rgba;
            uint32_t w = 0, h = 0;
            vector<uint8_t> normal_rgba;
            uint32_t nw = 0, nh = 0;
            vector<uint8_t> occlusion_rgba;
            uint32_t ow = 0, oh = 0;
            Vector3 tint = Vector3(1.0f, 1.0f, 1.0f);
            float cutoff = 0.5f;
        };

        auto load_material_maps = [](const filesystem::path& xml_path, MaterialImpostorMaps& out) -> bool
        {
            if (!filesystem::exists(xml_path))
            {
                return false;
            }

            pugi::xml_document doc;
            if (!doc.load_file(xml_path.c_str()))
            {
                return false;
            }

            const pugi::xml_node root = doc.child("Material");
            if (!root)
            {
                return false;
            }

            out.tint = Vector3(
                root.child("color_r").text().as_float(1.0f),
                root.child("color_g").text().as_float(1.0f),
                root.child("color_b").text().as_float(1.0f)
            );
            out.cutoff = root.child("alpha_cutoff").text().as_float(0.5f);

            string albedo_path;
            string normal_path;
            string occlusion_path;
            for (const pugi::xml_node tex : root.child("textures").children())
            {
                const uint32_t type = tex.attribute("texture_type").as_uint(999u);
                const uint32_t slot = tex.attribute("texture_slot").as_uint(999u);
                if (slot != 0u)
                {
                    continue;
                }
                const string path = tex.attribute("texture_path").as_string("");
                if (path.empty())
                {
                    continue;
                }
                if (type == 0u)
                {
                    albedo_path = path;
                }
                else if (type == 3u)
                {
                    normal_path = path;
                }
                else if (type == 4u)
                {
                    occlusion_path = path;
                }
            }
            if (albedo_path.empty())
            {
                return false;
            }

            if (!ImageImporter::LoadSdrRgba8(albedo_path, out.w, out.h, out.rgba))
            {
                return false;
            }
            if (!normal_path.empty())
            {
                ImageImporter::LoadSdrRgba8(normal_path, out.nw, out.nh, out.normal_rgba);
            }
            if (!occlusion_path.empty())
            {
                ImageImporter::LoadSdrRgba8(occlusion_path, out.ow, out.oh, out.occlusion_rgba);
            }
            return true;
        };

        auto proxy = make_shared<GeometryParentPage>();
        MaterialImpostorMaps leaf_maps;
        MaterialImpostorMaps bark_maps;
        bark_maps.tint = Vector3(0.35f, 0.28f, 0.22f);
        bool has_baked_rgba = false;
        if (!GetResourceFilePath().empty())
        {
            const filesystem::path mesh_path(GetResourceFilePath());
            const filesystem::path base = mesh_path.parent_path() / mesh_path.stem();
            has_baked_rgba = load_material_maps(base.string() + "_foliage.xml", leaf_maps);
            if (has_baked_rgba)
            {
                load_material_maps(base.string() + "_bark.xml", bark_maps);
            }
        }

        if (has_baked_rgba)
        {
            if (bark.empty() || bark_indices.empty())
            {
                ReadGeometry(0, &bark_indices, &bark);
            }

            BoundingBox whole_bounds(vertices.data(), static_cast<uint32_t>(vertices.size()));
            if (!bark.empty())
            {
                whole_bounds.Merge(BoundingBox(bark.data(), static_cast<uint32_t>(bark.size())));
            }
            proxy->center = whole_bounds.GetCenter();
            for (const auto& vertex : vertices)
            {
                proxy->radius = max(proxy->radius, (vertex.get_position() - proxy->center).Length());
            }
            for (const auto& vertex : bark)
            {
                proxy->radius = max(proxy->radius, (vertex.get_position() - proxy->center).Length());
            }
            if (proxy->radius <= 0.0f) return;

            constexpr uint32_t baked_res = GeometryParentPage::baked_resolution;
            generated_cache::Hash hash;
            hash.Add(uint32_t{3});
            hash.Add(GeometryParentPage::frames);
            hash.Add(baked_res);
            hash.Add(leaf_maps.w);
            hash.Add(leaf_maps.h);
            hash.Add(leaf_maps.nw);
            hash.Add(leaf_maps.nh);
            hash.Add(leaf_maps.ow);
            hash.Add(leaf_maps.oh);
            hash.Add(bark_maps.w);
            hash.Add(bark_maps.h);
            hash.Add(leaf_maps.cutoff);
            hash.Add(leaf_maps.tint);
            hash.Add(bark_maps.tint);
            hash.Add(vertices);
            hash.Add(indices);
            hash.Add(bark);
            hash.Add(bark_indices);
            const auto path = generated_cache::Path(World::GetResourceDirectory(), "canopy", hash.value);
            vector<uint32_t> atlas;
            constexpr size_t words = GeometryParentPage::frames * GeometryParentPage::frames * baked_res * baked_res * 2;
            const bool cache = !(m_flags & static_cast<uint32_t>(MeshFlags::PostProcessSkipCache));
            if (!cache || !generated_cache::Load(path, hash.value, atlas) || atlas.size() != words)
            {
                geometry_processing::ImpostorTextureInput bark_tex
                {
                    bark_maps.rgba.empty() ? nullptr : bark_maps.rgba.data(), bark_maps.w, bark_maps.h,
                    bark_maps.normal_rgba.empty() ? nullptr : bark_maps.normal_rgba.data(), bark_maps.nw, bark_maps.nh,
                    bark_maps.occlusion_rgba.empty() ? nullptr : bark_maps.occlusion_rgba.data(), bark_maps.ow, bark_maps.oh,
                    bark_maps.tint, bark_maps.cutoff
                };
                geometry_processing::ImpostorTextureInput leaf_tex
                {
                    leaf_maps.rgba.data(), leaf_maps.w, leaf_maps.h,
                    leaf_maps.normal_rgba.empty() ? nullptr : leaf_maps.normal_rgba.data(), leaf_maps.nw, leaf_maps.nh,
                    leaf_maps.occlusion_rgba.empty() ? nullptr : leaf_maps.occlusion_rgba.data(), leaf_maps.ow, leaf_maps.oh,
                    leaf_maps.tint, leaf_maps.cutoff
                };
                geometry_processing::bake_tree_impostor_rgba(
                    bark, bark_indices, bark_tex,
                    vertices, indices, leaf_tex,
                    GeometryParentPage::frames, baked_res,
                    proxy->center, proxy->radius, atlas
                );
                if (cache) generated_cache::Save(path, hash.value, atlas);
            }
            proxy->atlas               = GeometryBuffer::AppendMeshletVertices(atlas.data(), static_cast<uint32_t>(atlas.size()));
            proxy->baked_rgba          = true;
            proxy->impostor_resolution = baked_res;
            proxy->impostor_layers     = 1;
        }
        else
        {
            proxy->center = BoundingBox(vertices.data(), static_cast<uint32_t>(vertices.size())).GetCenter();
            Vector2 lo(FLT_MAX, FLT_MAX), hi(-FLT_MAX, -FLT_MAX);
            for (const auto& vertex : vertices)
            {
                const Vector2 uv = vertex.get_uv();
                lo = Vector2(min(lo.x, uv.x), min(lo.y, uv.y));
                hi = Vector2(max(hi.x, uv.x), max(hi.y, uv.y));
                proxy->radius = max(proxy->radius, (vertex.get_position() - proxy->center).Length());
            }
            if (proxy->radius <= 0) return;
            proxy->uv_min = lo;
            proxy->uv_scale = Vector2(max(hi.x - lo.x, 1e-6f), max(hi.y - lo.y, 1e-6f));
            generated_cache::Hash hash;
            hash.Add(uint32_t{1});
            hash.Add(GeometryParentPage::frames);
            hash.Add(GeometryParentPage::resolution);
            hash.Add(GeometryParentPage::layers);
            hash.Add(vertices);
            hash.Add(indices);
            const auto path = generated_cache::Path(World::GetResourceDirectory(), "canopy", hash.value);
            vector<uint32_t> atlas;
            constexpr size_t words = GeometryParentPage::frames * GeometryParentPage::frames * GeometryParentPage::resolution *
                GeometryParentPage::resolution * GeometryParentPage::layers * 2;
            const bool cache = !(m_flags & static_cast<uint32_t>(MeshFlags::PostProcessSkipCache));
            if (!cache || !generated_cache::Load(path, hash.value, atlas) || atlas.size() != words)
            {
                geometry_processing::bake_impostor(vertices, indices, GeometryParentPage::frames, GeometryParentPage::resolution,
                    GeometryParentPage::layers, proxy->center, proxy->radius, proxy->uv_min, proxy->uv_scale, atlas);
                if (cache) generated_cache::Save(path, hash.value, atlas);
            }
            // Atlas words share the geometry's read-only uint arena. Each species
            // uploads once and all its instances reference the same block.
            proxy->atlas               = GeometryBuffer::AppendMeshletVertices(atlas.data(), static_cast<uint32_t>(atlas.size()));
            proxy->baked_rgba          = false;
            proxy->impostor_resolution = GeometryParentPage::resolution;
            proxy->impostor_layers     = GeometryParentPage::layers;
        }
        const float r = proxy->radius;
        const Vector3 c = proxy->center;
        vector<RHI_Vertex_PosTexNorTan> quad = {
            {c + Vector3(-r,-r,0), Vector2(0,0), Vector3::Forward, Vector3::Right},
            {c + Vector3( r,-r,0), Vector2(1,0), Vector3::Forward, Vector3::Right},
            {c + Vector3( r, r,0), Vector2(1,1), Vector3::Forward, Vector3::Right},
            {c + Vector3(-r, r,0), Vector2(0,1), Vector3::Forward, Vector3::Right}};
        vector<uint32_t> topology = {0,2,1,0,3,2};
        const Vector3 extent(r,r,r);
        proxy->bounds = BoundingBox(c - extent, c + extent);
        upload(*proxy, quad, topology);
        m_sub_meshes[1].parent_page = move(proxy);
    }

    void Mesh::CreateGpuBuffers()
    {
        if (m_ready_for_blas.load(memory_order_acquire))
        {
            return;
        }
        RestoreCpuGeometry();
        BuildParentPages();

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

        // DrawData supplies 32-bit global bases; do not pack them into 25-bit mesh-local ranges.
        vector<Sb_MeshletBounds> gpu_meshlets = m_meshlets;

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

        // publish to the renderer only after global offsets are finalized and every sub-mesh has a resident range,
        // release ordering pairs with the acquire load in BuildAccelerationStructure
        m_object_size = GetMemoryUsage();
        m_ready_for_blas.store(true, std::memory_order_release);
    }

    void Mesh::ReuploadGpuBuffers()
    {
        // a mesh that never reached the buffer has nothing to move
        if (!m_ready_for_blas.exchange(false, std::memory_order_acq_rel))
        {
            return;
        }

        InvalidateAllBlas();
        CreateGpuBuffers();
    }

    RHI_Buffer* Mesh::GetVertexBuffer()
    {
        return GeometryBuffer::GetVertexBuffer();
    }

    RHI_Buffer* Mesh::GetIndexBuffer()
    {
        return GeometryBuffer::GetIndexBuffer();
    }

    bool Mesh::NeedsReferenceGeometry(uint32_t index) const
    {
        const SubMesh& sub = m_sub_meshes[index];
        return !m_dynamic && !IsSkinned() && !sub.clusters.empty() && !sub.HasResidentRootOnly();
    }

    GeometryRange Mesh::GetBlasRange(uint32_t index, bool pending) const
    {
        if (index < m_reference_geometry.size() && (m_reference_geometry[index].built ||
            (pending && m_reference_geometry[index].range.index_count != 0)))
            return m_reference_geometry[index].range;

        GeometryRange range = m_sub_meshes[index].root;
        range.vertex_offset += GetRenderVertexOffset();
        range.index_offset += m_global_index_offset;
        return range;
    }

    bool Mesh::PrepareReferenceGeometry(uint32_t index)
    {
        m_reference_geometry.resize(m_sub_meshes.size());
        auto& detail = m_reference_geometry[index];
        if (detail.failed) return false;
        if (detail.range.index_count != 0)
            return GeometryBuffer::IsResident(detail.range.vertex_offset + detail.range.vertex_count,
                detail.range.index_offset + detail.range.index_count);

        if (!detail.pending.valid())
        {
            auto owner = weak_from_this();
            auto decode = make_shared<packaged_task<ReferenceGeometry::Decoded()>>([owner, index]()
            {
                ReferenceGeometry::Decoded result;
                auto mesh = static_pointer_cast<Mesh>(owner.lock());
                if (!mesh || !mesh->ReadGeometry(index, &result.indices, &result.vertices))
                    return ReferenceGeometry::Decoded{};
                return result;
            });
            detail.pending = decode->get_future();
            ThreadPool::AddTask([decode]() { (*decode)(); });
            return false;
        }
        if (detail.pending.wait_for(chrono::seconds(0)) != future_status::ready) return false;

        ReferenceGeometry::Decoded result;
        try
        {
            result = detail.pending.get();
        }
        catch (const exception& e)
        {
            SP_LOG_ERROR("Reference geometry job failed for %s: %s", m_object_name.c_str(), e.what());
        }
        if (result.indices.empty() || result.vertices.empty())
        {
            SP_LOG_ERROR("Cannot decode reference geometry for %s, submesh %u", m_object_name.c_str(), index);
            detail.failed = true;
            return false;
        }
        auto& range = detail.range;
        range.vertex_count = static_cast<uint32_t>(result.vertices.size());
        range.index_count = static_cast<uint32_t>(result.indices.size());
        range.vertex_offset = GeometryBuffer::AppendVertices(result.vertices.data(), range.vertex_count);
        range.index_offset = GeometryBuffer::AppendIndices(result.indices.data(), range.index_count);
        return false; // BuildIfDirty publishes the new ranges before the next build attempt.
    }

    void Mesh::BuildAccelerationStructure(uint32_t sub_mesh_index, bool allow_update, bool finest)
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

        // Build only the requested submesh so the renderer can budget actual builds.
        // A terrain mesh can contain thousands of tiles.
        if (sub_mesh_index < m_sub_meshes.size())
        {
            const uint32_t i = sub_mesh_index;
            // skip if already built
            if (HasBlas(i, finest && !allow_update))
            {
                return;
            }

            // defensive, a sub-mesh with no geometry means it was published before its first range was filled in,
            // gating on m_ready_for_blas should make this unreachable but we keep the guard to avoid an out-of-range crash on regression
            if (!m_sub_meshes[i].HasGeometry())
            {
                return;
            }

            const bool use_detail = finest && !allow_update && NeedsReferenceGeometry(i) &&
                (i >= m_reference_geometry.size() || !m_reference_geometry[i].failed);
            if (use_detail && !PrepareReferenceGeometry(i)) return;
            const GeometryRange range = use_detail ? m_reference_geometry[i].range : GetBlasRange(i);

            // skip degenerate sub-meshes, passing zero counts to vkGetAccelerationStructureBuildSizesKHR
            // can produce garbage build sizes and crash the driver mid-burst
            if (range.vertex_count == 0 || range.index_count == 0 || (range.index_count % 3) != 0)
            {
                SP_LOG_WARNING("Skipping degenerate sub-mesh blas: mesh=%s sub=%u verts=%u indices=%u", m_object_name.c_str(), i, range.vertex_count, range.index_count);
                return;
            }

            const uint32_t global_vertex_offset = range.vertex_offset;
            const uint32_t global_index_offset = range.index_offset;

            // a loader thread can publish a mesh after this frame's geometry upload, its range may lie past the
            // end of the current gpu buffer, so the build waits until the range is resident and retries next frame
            if (!GeometryBuffer::IsResident(global_vertex_offset + range.vertex_count, global_index_offset + range.index_count))
            {
                return;
            }

            // create geometry for this sub-mesh using global buffer addresses
            RHI_AccelerationStructureGeometry geo;
            geo.transparent           = false;
            geo.vertex_format         = RHI_Format::R32G32B32_Float; // positions
            geo.vertex_buffer_address = RHI_Device::GetBufferDeviceAddress(vertex_buffer->GetRhiResource()) + global_vertex_offset * vertex_buffer->GetStride();
            geo.vertex_stride         = vertex_buffer->GetStride();
            geo.max_vertex            = range.vertex_count - 1;
            geo.index_format          = RHI_Format::R32_Uint;
            geo.index_buffer_address  = RHI_Device::GetBufferDeviceAddress(index_buffer->GetRhiResource()) + global_index_offset * sizeof(uint32_t);

            vector<RHI_AccelerationStructureGeometry> geometries = { geo };
            vector<uint32_t> primitive_counts                    = { range.index_count / 3 };

            // create and build blas for this sub-mesh
            string blas_name = m_object_name + "_blas_" + to_string(i);
            m_blas[i] = make_unique<RHI_AccelerationStructure>(RHI_AccelerationStructureType::Bottom, blas_name.c_str());
            m_blas[i]->BuildBottomLevel(geometries, primitive_counts, allow_update);
            if (use_detail) m_reference_geometry[i].built = true;
        }
    }

    bool Mesh::IsGpuResident() const
    {
        return GeometryBuffer::IsResident(m_global_vertex_offset + m_global_vertex_capacity, m_global_index_offset + m_global_index_capacity);
    }

    void Mesh::InvalidateBlas(uint32_t sub_mesh_index)
    {
        if (sub_mesh_index < m_reference_geometry.size()) m_reference_geometry[sub_mesh_index] = {};
        if (sub_mesh_index < m_blas.size())
        {
            m_blas[sub_mesh_index].reset();
        }
    }

    void Mesh::InvalidateAllBlas()
    {
        // Geometry edits and world teardown invalidate the built geometry. Moving
        // unchanged input buffers alone does not invalidate a completed BLAS.
        m_reference_geometry.clear();
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

        if (sub_mesh_index >= m_sub_meshes.size() || !m_sub_meshes[sub_mesh_index].HasGeometry())
        {
            return;
        }

        const GeometryRange range = GetBlasRange(sub_mesh_index);
        uint32_t global_vertex_offset = range.vertex_offset;
        uint32_t global_index_offset  = range.index_offset;
        if (!GeometryBuffer::IsResident(global_vertex_offset + range.vertex_count, global_index_offset + range.index_count))
        {
            return;
        }

        RHI_AccelerationStructureGeometry geo;
        geo.transparent           = false;
        geo.vertex_format         = RHI_Format::R32G32B32_Float;
        geo.vertex_buffer_address = RHI_Device::GetBufferDeviceAddress(vertex_buffer->GetRhiResource()) + global_vertex_offset * vertex_buffer->GetStride();
        geo.vertex_stride         = vertex_buffer->GetStride();
        geo.max_vertex            = range.vertex_count - 1;
        geo.index_format          = RHI_Format::R32_Uint;
        geo.index_buffer_address  = RHI_Device::GetBufferDeviceAddress(index_buffer->GetRhiResource()) + global_index_offset * sizeof(uint32_t);

        vector<RHI_AccelerationStructureGeometry> geometries = { geo };
        vector<uint32_t> primitive_counts                    = { range.index_count / 3 };

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
