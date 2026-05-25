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

#pragma once

//= INCLUDES =====================
#include <vector>
#include <mutex>
#include <atomic>
#include <memory>
#include "../RHI/RHI_Vertex.h"
#include "../Resource/IResource.h"
#include "../Math/BoundingBox.h"
#include "../Rendering/Renderer_Buffers.h"
#include "../Animation/AnimationClip.h"
#include "../Animation/SkeletalMeshBinding.h"
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
        void LoadFromFile(const std::string& file_path) override;

        // geometry
        void Clear();
        void GetGeometry(uint32_t sub_mesh_index, std::vector<uint32_t>* indices, std::vector<RHI_Vertex_PosTexNorTan>* vertices);
        uint32_t GetMemoryUsage() const;
        void AddLod(std::vector<RHI_Vertex_PosTexNorTan>& vertices, std::vector<uint32_t>& indices, const uint32_t sub_mesh_index);
        void AddGeometry(std::vector<RHI_Vertex_PosTexNorTan>& vertices, std::vector<uint32_t>& indices, const bool generate_lods, uint32_t* sub_mesh_index = nullptr);
        // overload that writes into a pre-reserved sub-mesh slot, used by the model importer so the assignment is deterministic
        // when ParseMesh runs in parallel, the auto-allocating overload above races on m_sub_meshes.size() and silently swaps which
        // sub-mesh index ends up on which entity, breaking material assignment for any caller that keys off GetSubMeshIndex()
        void AddGeometry(std::vector<RHI_Vertex_PosTexNorTan>& vertices, std::vector<uint32_t>& indices, const bool generate_lods, const uint32_t sub_mesh_index_in);
        // pre-allocate sub-mesh slots so concurrent AddGeometry calls with explicit indices target stable positions
        void ReserveSubMeshes(const uint32_t count);
        std::vector<RHI_Vertex_PosTexNorTan>& GetVertices()    { return m_vertices; }
        std::vector<uint32_t>& GetIndices()                    { return m_indices; }
        const SubMesh& GetSubMesh(const uint32_t index) const  { return m_sub_meshes[index]; }
        const std::vector<Sb_MeshletBounds>& GetMeshlets() const { return m_meshlets; }

        // get counts
        uint32_t GetVertexCount() const;
        uint32_t GetIndexCount() const;

        // gpu buffers
        void CreateGpuBuffers();
        void BuildAccelerationStructure(RHI_CommandList* cmd_list, bool allow_update = false);
        RHI_Buffer* GetIndexBuffer();
        RHI_Buffer* GetVertexBuffer();

        // global geometry buffer offsets
        uint32_t GetGlobalVertexOffset() const  { return m_global_vertex_offset; }
        uint32_t GetGlobalIndexOffset() const   { return m_global_index_offset; }
        uint32_t GetGlobalMeshletOffset() const { return m_global_meshlet_offset; }

        // root entity
        Entity* GetRootEntity() { return m_root_entity; }
        void SetRootEntity(Entity* entity) { m_root_entity = entity; }

        // mesh type
        MeshType GetType() const          { return m_type; }
        void SetType(const MeshType type) { m_type = type; }

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

        // animation clips
        void AddAnimationClip(AnimationClip clip)                          { m_animation_clips.push_back(std::move(clip)); }
        const std::vector<AnimationClip>& GetAnimationClips() const        { return m_animation_clips; }
        uint32_t GetAnimationClipCount() const                             { return static_cast<uint32_t>(m_animation_clips.size()); }

        // acceleration structure - one blas per sub-mesh to avoid shared geometry issues
        RHI_AccelerationStructure* GetBlas(uint32_t sub_mesh_index) const;
        bool HasBlas(uint32_t sub_mesh_index) const;
        void InvalidateBlas(uint32_t sub_mesh_index);
        void InvalidateAllBlas();
        void RefitBlas(RHI_CommandList* cmd_list, uint32_t sub_mesh_index);
        bool CanRefitBlas(uint32_t sub_mesh_index) const;

    private:
        // geometry
        std::vector<RHI_Vertex_PosTexNorTan> m_vertices; // all vertices of a model file
        std::vector<uint32_t> m_indices;                 // all indices of a model file
        std::vector<SubMesh> m_sub_meshes;               // tracks sub-meshes and lods within the above vectors
        std::vector<Sb_MeshletBounds> m_meshlets;        // per-lod meshlet bounding spheres + index ranges

        // global geometry buffer offsets (base offsets into the shared vertex/index/meshlet buffers)
        uint32_t m_global_vertex_offset  = 0;
        uint32_t m_global_index_offset   = 0;
        uint32_t m_global_meshlet_offset = 0;

        // acceleration structures
        std::vector<std::unique_ptr<RHI_AccelerationStructure>> m_blas; // one blas per sub-mesh

        // set once createGpuBuffers has run and the global buffer offsets are finalized,
        // gates blas building so the renderer never observes a mesh whose sub-meshes/lods are still being filled in by a loader thread
        std::atomic<bool> m_ready_for_blas = false;

        // misc
        std::mutex m_mutex;
        Entity* m_root_entity = nullptr;
        MeshType m_type       = MeshType::Max;
        std::shared_ptr<Skeleton> m_skeleton;
        std::unique_ptr<SkeletalMeshBinding> m_skeletal_mesh_binding;
        std::vector<AnimationClip> m_animation_clips;
    };
}
