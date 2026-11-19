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
#include <future>
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
        PostProcessPreserveTerrainEdges = 1 << 7,
        PreserveSourceGeometry         = 1 << 8, // keep authored detail, optimize layout without reducing geometry
        PostProcessSkipCache            = 1 << 9, // the owning generator caches the complete prepared mesh
        FoliageCardClusters             = 1 << 11, // tree creator foliage uses coverage-preserving cluster proxies
        PostProcessMeshletsScan         = 1 << 10, // linear clustering for spatially ordered procedural grids
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

    struct GeometryRange
    {
        uint32_t vertex_offset;  // starting offset in m_vertices
        uint32_t vertex_count;   // number of vertices in this range
        uint32_t index_offset;   // starting offset in m_indices
        uint32_t index_count;    // number of indices in this range
        math::BoundingBox aabb;  // bounding box of this range
        uint32_t meshlet_offset; // starting offset in m_meshlets (per-mesh local)
        uint32_t meshlet_count;  // number of meshlets covering this range
    };
    // A cluster page uses the same local addressing on both raster paths.
    struct GeometryPage
    {
        static constexpr uint32_t max_triangles = 512;
        static constexpr uint32_t max_corners = max_triangles * 3;
        std::vector<RHI_Vertex_PosTexNorTan> vertices;
        std::vector<uint32_t> indices, remaps, micro;
        std::vector<Sb_MeshletBounds> meshlets;
        bool Decode(std::span<const uint8_t> bytes);
    };

    struct GeometryPageSource
    {
        mutable size_t request_index = SIZE_MAX; // render-thread index, never serialized
        mutable std::vector<uint8_t> bytes; // newly imported, before the first native save
        mutable std::string file;
        mutable uint64_t offset = 0;
        uint32_t size = 0;
        uint64_t checksum = 0;
        bool Read(std::vector<uint8_t>& result) const;
    };

    struct GeometryRefinement
    {
        float error;
        std::shared_ptr<const GeometryPageSource> page;
    };

    struct GeometryCluster
    {
        uint32_t first_meshlet = 0; // relative to the submesh's resident proxy
        uint32_t meshlet_count = 0;
        uint32_t index_count = 0;
        math::BoundingBox bounds;
        float error = 0;
        std::vector<GeometryRefinement> refinements; // finest first, monotonically increasing error
    };

    struct GeometryParentPage
    {
        math::Vector3 center;
        float radius = 0.0f;
        float error = 0.0f;
        bool uses_root_vertices = false;
        std::vector<uint32_t> skin_indices;
        std::shared_ptr<GeometryParentPage> coarser;
        uint32_t vertex_count = 0, index_count = 0, meshlet_count = 0;
        math::BoundingBox bounds;
        math::Vector2 uv_min, uv_scale;
        uint32_t atlas = 0, vertices = 0, indices = 0, meshlet = 0, remaps = 0, micro = 0;
        static constexpr uint32_t frames = 8, resolution = 64, layers = 2;
    };

    struct SubMesh
    {
        GeometryRange root{};
        std::shared_ptr<GeometryParentPage> parent_page;

        bool HasGeometry() const { return root.vertex_count && root.index_count; }
        std::vector<GeometryCluster> clusters;
        bool HasResidentRootOnly() const
        {
            return clusters.size() == 1 && clusters.front().refinements.empty();
        }
        void SetResidentRoot()
        {
            clusters.resize(1);
            clusters.front() = {0, root.meshlet_count, root.index_count, root.aabb, 0, {}};
        }
    };

    class Mesh : public IResource
    {
    public:
        Mesh();
        ~Mesh();

        // iresource
        void SaveToFile(const std::string& file_path) override;
        std::function<void()> CreateSaveTask(const std::string& file_path) override;
        bool LoadFromFile(const std::string& file_path) override;
        bool LoadNativeCpu(const std::string& file_path, uint32_t* version = nullptr);
        static int UpgradeGeometryDirectory(const std::string& directory);
        // CPU-only, validated generated geometry. GPU resources are created after scene publication.
        bool LoadPrepared(const std::string& path, uint64_t key);
        bool LoadPrepared(std::span<const uint8_t> bytes);
        std::vector<uint8_t> SerializePrepared() const;
        void SavePrepared(const std::string& path, uint64_t key) const;
        void AppendPrepared(const Mesh& tile, uint32_t sub_mesh_index);
        bool EditClusterPages(uint32_t sub_mesh_index, const math::BoundingBox& region,
            const std::function<bool(RHI_Vertex_PosTexNorTan&)>& edit);

        // geometry
        void Clear();
        void GetGeometry(uint32_t sub_mesh_index, std::vector<uint32_t>* indices, std::vector<RHI_Vertex_PosTexNorTan>* vertices);
        // any level of a sub-mesh, the returned indices are local to the returned vertices
        bool ReadGeometry(uint32_t sub_mesh_index, std::vector<uint32_t>* indices, std::vector<RHI_Vertex_PosTexNorTan>* vertices);
        bool HasGeometry(uint32_t sub_mesh_index) const;
        uint64_t GetMemoryUsage() const;
        uint64_t GetCpuBytes() const;
        bool AddVirtualGeometry(const std::vector<RHI_Vertex_PosTexNorTan>& vertices, const std::vector<uint32_t>& indices, uint32_t sub_mesh_index, bool refine_small = false, bool foliage = false);
        void AddResidentGeometry(std::vector<RHI_Vertex_PosTexNorTan>& vertices, std::vector<uint32_t>& indices, const uint32_t sub_mesh_index);
        void AddGeometry(std::vector<RHI_Vertex_PosTexNorTan>& vertices, std::vector<uint32_t>& indices, uint32_t* sub_mesh_index = nullptr);
        // writes into a pre-reserved slot, the auto-allocating overload races on size() when ParseMesh runs in parallel
        void AddGeometry(std::vector<RHI_Vertex_PosTexNorTan>& vertices, std::vector<uint32_t>& indices, const uint32_t sub_mesh_index_in);
        bool UpdateGeometry(
            std::vector<RHI_Vertex_PosTexNorTan>& vertices,
            std::vector<uint32_t>& indices
        );
        // Resident geometry deformation with unchanged vertex ordering and triangle topology.
        // a refittable blas (cloth) passes false and refits it instead of paying for a rebuild every frame
        bool UpdateVertices(const std::vector<RHI_Vertex_PosTexNorTan>& vertices, bool invalidate_blas = true);
        // write a slice of m_vertices into the global buffer, cloth and live terrain pads
        void UploadVertexRange(uint32_t vertex_offset, uint32_t vertex_count);
        // grow cluster bounds from current verts and max out meshlet radii so raised pads are not culled
        void RefreshGeometryBounds(uint32_t sub_mesh_index);
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
        // the global geometry buffer was emptied, upload again so the offsets point into the new one
        void ReuploadGpuBuffers();
        void BuildAccelerationStructure(uint32_t sub_mesh_index, bool allow_update = false, bool finest = false);
        RHI_Buffer* GetIndexBuffer();
        RHI_Buffer* GetVertexBuffer();

        // global geometry buffer offsets
        uint32_t GetGlobalVertexOffset() const           { return m_global_vertex_offset; }
        uint32_t GetGlobalIndexOffset() const            { return m_global_index_offset; }
        uint32_t GetGlobalMeshletOffset() const          { return m_global_meshlet_offset; }
        uint32_t GetGlobalMeshletVertexOffset() const    { return m_global_meshlet_vertex_offset; }
        uint32_t GetGlobalMeshletMicroOffset() const     { return m_global_meshlet_micro_offset; }

        // the whole global vertex and index slice is on the gpu, draws and blas builds must wait for it
        bool IsGpuResident() const;

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
        std::shared_ptr<Mesh> CreateCrowdGeometry();
        bool SelectCrowdPose(int32_t clip_index, float time);
        bool HasCrowdPoses() const { return m_crowd_clip != nullptr; }
        uint32_t GetRenderVertexOffset() const { return m_pose_offset == UINT32_MAX ? m_global_vertex_offset : m_pose_offset; }
        uint32_t GetRenderPreviousDelta(uint32_t vertex_offset) const;

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
        // Global vertex/index offsets of the triangles currently in the BLAS.
        GeometryRange GetBlasRange(uint32_t sub_mesh_index, bool pending = false) const;
        bool HasBlas(uint32_t sub_mesh_index, bool finest = false) const
        {
            if (sub_mesh_index >= m_blas.size())
            {
                return false;
            }

            return m_blas[sub_mesh_index] && (!finest || !NeedsReferenceGeometry(sub_mesh_index) ||
                (sub_mesh_index < m_reference_geometry.size() && (m_reference_geometry[sub_mesh_index].built || m_reference_geometry[sub_mesh_index].failed)));
        }
        void InvalidateBlas(uint32_t sub_mesh_index);
        void InvalidateAllBlas();
        void RefitBlas(uint32_t sub_mesh_index);
        bool CanRefitBlas(uint32_t sub_mesh_index) const;

    private:
        void BuildParentPages();
        bool CookFromHighestDetail(Mesh& source, bool tree = false);
        void AdoptGeometry(Mesh& source);

        // geometry
        std::vector<RHI_Vertex_PosTexNorTan> m_vertices; // all vertices of a model file
        std::vector<uint32_t> m_indices;                 // all indices of a model file
        std::vector<SubMesh> m_sub_meshes;               // tracks sub-mesh ranges within the above vectors
        std::vector<Sb_MeshletBounds> m_meshlets;        // meshlet bounding spheres + index ranges
        std::vector<uint32_t> m_meshlet_vertices;        // packed unique-vertex remaps across all sub-meshes
        std::vector<uint32_t> m_meshlet_micro_indices;   // packed micro-indices across all sub-meshes

        // released cpu geometry, the counts keep answering while the arrays are empty
        bool RestoreCpuGeometry() const;
        bool restore_cpu_geometry_locked();
        std::function<bool(std::vector<uint8_t>&)> m_cpu_geometry_source;
        bool m_cpu_geometry_released     = false;
        uint32_t m_released_vertex_count = 0;
        uint32_t m_released_index_count  = 0;

        // global geometry buffer offsets (base offsets into the shared vertex/index/meshlet buffers)
        struct CrowdClip
        {
            uint32_t first_vertex = 0, frame_count = 0, vertex_count = 0;
            int32_t clip_index = -1;
            float duration = 0.0f;
        };
        std::shared_ptr<CrowdClip> m_crowd_clip;
        uint32_t m_pose_offset = UINT32_MAX;
        uint32_t m_previous_pose_offset = 0;
        uint64_t m_pose_changed_frame = UINT64_MAX;
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

        // Decoded finest pages have their own addresses in the world geometry arena.
        // The root remains available to rasterization and deformable BLAS builds.
        struct ReferenceGeometry
        {
            GeometryRange range{};
            bool built = false;
            bool failed = false;
            struct Decoded
            {
                std::vector<RHI_Vertex_PosTexNorTan> vertices;
                std::vector<uint32_t> indices;
            };
            std::future<Decoded> pending;
        };
        bool NeedsReferenceGeometry(uint32_t index) const;
        bool PrepareReferenceGeometry(uint32_t index);
        std::vector<ReferenceGeometry> m_reference_geometry;

        // acceleration structures
        std::vector<std::unique_ptr<RHI_AccelerationStructure>> m_blas; // one blas per sub-mesh

        // set once createGpuBuffers has run and the global buffer offsets are finalized,
        // gates blas building so the renderer never observes a mesh whose sub-meshes are still being filled in by a loader thread
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
