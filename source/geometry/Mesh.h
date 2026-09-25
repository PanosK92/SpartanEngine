/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

#pragma once

//= INCLUDES =====================
#include <vector>
#include <mutex>
#include <atomic>
#include <memory>
#include <span>
#include "../rhi/RHI_Vertex.h"
#include "../resource/IResource.h"
#include "../math/BoundingBox.h"
#include "../rendering/Renderer_Buffers.h"
#include "../animation/AnimationClip.h"
#include "../animation/SkeletalMeshBinding.h"
//================================

namespace sol
{
    class state_view;
}

namespace spartan
{
    class Entity;
    class RHI_Buffer;
    class RHI_AccelerationStructure;
    class RHI_CommandList;
    struct Skeleton;

    enum class MeshFlags : uint32_t
    {
        ImportRemoveRedundantData       = 1 << 0,
        ImportLights                    = 1 << 1,
        ImportCombineMeshes             = 1 << 2,
        ImportGenerateSmoothNormals     = 1 << 3,
        PostProcessNormalizeScale       = 1 << 4,
        PostProcessOptimize             = 1 << 5,
        PostProcessGenerateLods         = 1 << 6,
        PostProcessPreserveTerrainEdges = 1 << 7,
        PostProcessPreserveLod0         = 1 << 8, // keep authored detail, optimize layout without reducing geometry
        PostProcessSkipCache            = 1 << 9, // the owning generator caches the complete prepared mesh
    };

    enum class MeshType
    {
        Cube,
        Quad,
        Sphere,
        Cylinder,
        Cone,
        Max
    };

    struct MeshLod
    {
        uint32_t vertex_offset;  // starting offset in m_vertices
        uint32_t vertex_count;   // number of vertices for this LOD
        uint32_t index_offset;   // starting offset in m_indices
        uint32_t index_count;    // number of indices for this LOD
        math::BoundingBox aabb;  // bounding box of this LOD
        uint32_t meshlet_offset; // starting offset in m_meshlets (per-mesh local)
        uint32_t meshlet_count;  // number of meshlets covering this lod
    };
    static const uint32_t mesh_lod_count = 5;
    // a level has to drop at least this much of the previous level to be worth keeping, a level that
    // only sheds a few triangles costs memory and a draw range while looking identical
    static constexpr float mesh_lod_min_reduction = 0.9f;

    struct SubMesh
    {
        std::vector<MeshLod> lods; // list of LOD levels for this sub-mesh
    };

    class Mesh : public IResource
    {
    public:
        Mesh();
        ~Mesh();

        static void RegisterForScripting(sol::state_view State);

        // iresource
        void SaveToFile(const std::string& file_path) override;
        std::function<void()> CreateSaveTask(const std::string& file_path) override;
        void LoadFromFile(const std::string& file_path) override;
        // CPU-only, validated generated geometry. GPU resources are created after scene publication.
        bool LoadPrepared(const std::string& path, uint64_t key);
        bool LoadPrepared(std::span<const uint8_t> bytes);
        std::vector<uint8_t> SerializePrepared() const;
        void SavePrepared(const std::string& path, uint64_t key) const;
        void AppendPrepared(const Mesh& tile, uint32_t sub_mesh_index);

        // geometry
        void Clear();
        void GetGeometry(uint32_t sub_mesh_index, std::vector<uint32_t>* indices, std::vector<RHI_Vertex_PosTexNorTan>* vertices);
        // any level of a sub-mesh, the returned indices are local to the returned vertices
        bool GetGeometryLod(uint32_t sub_mesh_index, uint32_t lod_index, std::vector<uint32_t>* indices, std::vector<RHI_Vertex_PosTexNorTan>* vertices);
        uint32_t GetLodCount(uint32_t sub_mesh_index) const;
        uint32_t GetMemoryUsage() const;
        uint64_t GetCpuBytes() const;
        void AddLod(std::vector<RHI_Vertex_PosTexNorTan>& vertices, std::vector<uint32_t>& indices, const uint32_t sub_mesh_index);
        void AddGeometry(std::vector<RHI_Vertex_PosTexNorTan>& vertices, std::vector<uint32_t>& indices, const bool generate_lods, uint32_t* sub_mesh_index = nullptr);
        // writes into a pre-reserved slot, the auto-allocating overload races on size() when ParseMesh runs in parallel
        void AddGeometry(std::vector<RHI_Vertex_PosTexNorTan>& vertices, std::vector<uint32_t>& indices, const bool generate_lods, const uint32_t sub_mesh_index_in, const bool preserve_lod0 = false);
        bool UpdateGeometry(
            std::vector<RHI_Vertex_PosTexNorTan>& vertices,
            std::vector<uint32_t>& indices
        );
        // Single-LOD deformation with unchanged vertex ordering and triangle topology.
        bool UpdateVertices(const std::vector<RHI_Vertex_PosTexNorTan>& vertices);
        // write a slice of m_vertices into the global buffer, cloth and live terrain pads
        void UploadVertexRange(uint32_t vertex_offset, uint32_t vertex_count);
        // grow lod aabbs from current verts and max out meshlet radii so raised pads are not culled
        void RefreshLodBounds(uint32_t sub_mesh_index);
        // pre-allocate sub-mesh slots so concurrent AddGeometry calls with explicit indices target stable positions
        void ReserveSubMeshes(const uint32_t count);
        std::vector<RHI_Vertex_PosTexNorTan>& GetVertices()    { RestoreCpuGeometry(); return m_vertices; }
        std::vector<uint32_t>& GetIndices()                    { RestoreCpuGeometry(); return m_indices; }
        const SubMesh& GetSubMesh(const uint32_t index) const  { return m_sub_meshes[index]; }
        const std::vector<Sb_MeshletBounds>& GetMeshlets() const { RestoreCpuGeometry(); return m_meshlets; }

        // once uploaded the gpu holds the geometry, a mesh with a source can drop its cpu copy and refill it
        // from the source (prepared bytes, see SerializePrepared) the next time a cpu reader asks for it
        void SetCpuGeometrySource(std::function<bool(std::vector<uint8_t>&)> source);
        bool ReleaseCpuGeometry();
        static uint32_t GetCpuGeometryRestoreCount();

        // get counts
        uint32_t GetVertexCount() const;
        uint32_t GetIndexCount() const;
        uint32_t GetSubMeshCount() const { return static_cast<uint32_t>(m_sub_meshes.size()); }

        // gpu buffers
        void CreateGpuBuffers();
        void BuildAccelerationStructure(uint32_t sub_mesh_index, bool allow_update = false);
        RHI_Buffer* GetIndexBuffer();
        RHI_Buffer* GetVertexBuffer();

        // global geometry buffer offsets
        uint32_t GetGlobalVertexOffset() const           { return m_global_vertex_offset; }
        uint32_t GetGlobalIndexOffset() const            { return m_global_index_offset; }
        uint32_t GetGlobalMeshletOffset() const          { return m_global_meshlet_offset; }
        uint32_t GetGlobalMeshletVertexOffset() const    { return m_global_meshlet_vertex_offset; }
        uint32_t GetGlobalMeshletMicroOffset() const     { return m_global_meshlet_micro_offset; }

        // root entity
        Entity* GetRootEntity() { return m_root_entity; }
        void SetRootEntity(Entity* entity) { m_root_entity = entity; }

        // mesh type
        MeshType GetType() const          { return m_type; }
        void SetType(const MeshType type) { m_type = type; }
        void SetDynamic(const bool dynamic) { m_dynamic = dynamic; }

        // flags
        uint32_t GetFlags() const { return m_flags; }
        static uint32_t GetDefaultFlags();

        // skinning data model split
        void SetSkeleton(const std::shared_ptr<Skeleton>& skeleton) { m_skeleton = skeleton; }
        const std::shared_ptr<Skeleton>& GetSkeleton() const { return m_skeleton; }
        void SetSkeletalMeshBinding(std::unique_ptr<SkeletalMeshBinding> binding) { m_skeletal_mesh_binding = std::move(binding); }
        SkeletalMeshBinding* GetSkeletalMeshBinding() { return m_skeletal_mesh_binding.get(); }
        const SkeletalMeshBinding* GetSkeletalMeshBinding() const { return m_skeletal_mesh_binding.get(); }
        bool IsSkinned() const { return m_skeleton != nullptr && m_skeletal_mesh_binding != nullptr; }

        // own vertex gpu region, shared index/meshlet regions + skeleton/clips
        std::shared_ptr<Mesh> CreateSkinnedInstance();

        // animation clips
        void AddAnimationClip(AnimationClip clip)                          { m_animation_clips.push_back(std::move(clip)); }
        const std::vector<AnimationClip>& GetAnimationClips() const        { return m_animation_clips; }
        uint32_t GetAnimationClipCount() const                             { return static_cast<uint32_t>(m_animation_clips.size()); }

        // acceleration structure - one blas per sub-mesh to avoid shared geometry issues
        RHI_AccelerationStructure* GetBlas(uint32_t sub_mesh_index) const
        {
            if (sub_mesh_index >= m_blas.size())
            {
                return nullptr;
            }

            return m_blas[sub_mesh_index].get();
        }
        bool HasBlas(uint32_t sub_mesh_index) const
        {
            if (sub_mesh_index >= m_blas.size())
            {
                return false;
            }

            return m_blas[sub_mesh_index] != nullptr;
        }
        void InvalidateBlas(uint32_t sub_mesh_index);
        void InvalidateAllBlas();
        void RefitBlas(uint32_t sub_mesh_index);
        bool CanRefitBlas(uint32_t sub_mesh_index) const;

    private:
        // geometry
        std::vector<RHI_Vertex_PosTexNorTan> m_vertices; // all vertices of a model file
        std::vector<uint32_t> m_indices;                 // all indices of a model file
        std::vector<SubMesh> m_sub_meshes;               // tracks sub-meshes and lods within the above vectors
        std::vector<Sb_MeshletBounds> m_meshlets;        // per-lod meshlet bounding spheres + index ranges
        std::vector<uint32_t> m_meshlet_vertices;        // packed unique-vertex remaps across all lods
        std::vector<uint32_t> m_meshlet_micro_indices;   // packed micro-indices across all lods

        // released cpu geometry, the counts keep answering while the arrays are empty
        bool RestoreCpuGeometry() const;
        bool restore_cpu_geometry_locked();
        std::function<bool(std::vector<uint8_t>&)> m_cpu_geometry_source;
        bool m_cpu_geometry_released     = false;
        uint32_t m_released_vertex_count = 0;
        uint32_t m_released_index_count  = 0;

        // global geometry buffer offsets (base offsets into the shared vertex/index/meshlet buffers)
        uint32_t m_global_vertex_offset         = 0;
        uint32_t m_global_index_offset          = 0;
        uint32_t m_global_meshlet_offset        = 0;
        uint32_t m_global_meshlet_vertex_offset = 0;
        uint32_t m_global_meshlet_micro_offset  = 0;
        uint32_t m_global_vertex_capacity         = 0;
        uint32_t m_global_index_capacity          = 0;
        uint32_t m_global_meshlet_capacity        = 0;
        uint32_t m_global_meshlet_vertex_capacity = 0;
        uint32_t m_global_meshlet_micro_capacity  = 0;

        // acceleration structures
        std::vector<std::unique_ptr<RHI_AccelerationStructure>> m_blas; // one blas per sub-mesh

        // set once createGpuBuffers has run and the global buffer offsets are finalized,
        // gates blas building so the renderer never observes a mesh whose sub-meshes/lods are still being filled in by a loader thread
        std::atomic<bool> m_ready_for_blas = false;

        // misc
        std::mutex m_mutex;
        Entity* m_root_entity = nullptr;
        MeshType m_type       = MeshType::Max;
        bool m_dynamic        = false;
        std::shared_ptr<Skeleton> m_skeleton;
        std::unique_ptr<SkeletalMeshBinding> m_skeletal_mesh_binding;
        std::vector<AnimationClip> m_animation_clips;
    };
}
