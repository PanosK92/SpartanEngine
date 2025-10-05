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

#pragma once

//= INCLUDES =====================
#include <vector>
#include <mutex>
#include "../RHI/RHI_Vertex.h"
#include "../Resource/IResource.h"
#include "../Math/BoundingBox.h"
//================================

namespace spartan
{
    class RHI_Buffer;

    enum class MeshFlags : uint32_t
    {
        ImportRemoveRedundantData       = 1 << 0,
        ImportLights                    = 1 << 1,
        ImportCombineMeshes             = 1 << 2,
        PostProcessNormalizeScale       = 1 << 3,
        PostProcessOptimize             = 1 << 4,
        PostProcessGenerateLods         = 1 << 5,
        PostProcessPreserveTerrainEdges = 1 << 6,
    };

    enum class MeshLodDropoff
    {
        Exponential, // slow early, fast late poly reduction (t^2), detail-heavy mid-range
        Linear,      // medium reduction across LODs (t), balanced for general use
        Aggressive,  // fast early, slow late reduction (sqrt(t)), optimizes distant objects
        Max
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
        uint32_t vertex_offset; // starting offset in m_vertices
        uint32_t vertex_count;  // number of vertices for this LOD
        uint32_t index_offset;  // starting offset in m_indices
        uint32_t index_count;   // number of indices for this LOD
        math::BoundingBox aabb; // bounding box of this LOD
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

        // iresource
        void SaveToFile(const std::string& file_path) override;
        void LoadFromFile(const std::string& file_path) override;

        // geometry
        void Clear();
        void GetGeometry(uint32_t sub_mesh_index, std::vector<uint32_t>* indices, std::vector<RHI_Vertex_PosTexNorTan>* vertices);
        uint32_t GetMemoryUsage() const;
        void AddLod(std::vector<RHI_Vertex_PosTexNorTan>& vertices, std::vector<uint32_t>& indices, const uint32_t sub_mesh_index);
        void AddGeometry(std::vector<RHI_Vertex_PosTexNorTan>& vertices, std::vector<uint32_t>& indices, const bool generate_lods, uint32_t* sub_mesh_index = nullptr);
        std::vector<RHI_Vertex_PosTexNorTan>& GetVertices()   { return m_vertices; }
        std::vector<uint32_t>& GetIndices()                   { return m_indices; }
        const SubMesh& GetSubMesh(const uint32_t index) const { return m_sub_meshes[index]; }

        // lod dropoff
        MeshLodDropoff GetLodDropoff() const             { return m_lod_dropoff; }
        void SetLodDropoff(const MeshLodDropoff dropoff) { m_lod_dropoff = dropoff; }

        // get counts
        uint32_t GetVertexCount() const;
        uint32_t GetIndexCount() const;

        // gpu buffers
        void CreateGpuBuffers();
        RHI_Buffer* GetIndexBuffer()  { return m_index_buffer.get();  }
        RHI_Buffer* GetVertexBuffer() { return m_vertex_buffer.get(); }

        // root entity
        Entity* GetRootEntity() { return m_root_entity; }
        void SetRootEntity(Entity* entity) { m_root_entity = entity; }

        // mesh type
        MeshType GetType() const          { return m_type; }
        void SetType(const MeshType type) { m_type = type; }

        // flags
        uint32_t GetFlags() const { return m_flags; }
        static uint32_t GetDefaultFlags();

    private:
        // geometry
        std::vector<RHI_Vertex_PosTexNorTan> m_vertices; // all vertices of a model file
        std::vector<uint32_t> m_indices;                 // all indices of a model file
        std::vector<SubMesh> m_sub_meshes;               // tracks sub-meshes and lods within the above vectors

        // gpu buffers
        std::shared_ptr<RHI_Buffer> m_vertex_buffer;
        std::shared_ptr<RHI_Buffer> m_index_buffer;

        // misc
        std::mutex m_mutex;
        Entity* m_root_entity        = nullptr;
        MeshType m_type              = MeshType::Max;
        MeshLodDropoff m_lod_dropoff = MeshLodDropoff::Linear;
    };
}
