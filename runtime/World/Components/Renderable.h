/*
Copyright(c) 2016-2024 Panos Karabelas

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

//= INCLUDES =================================
#include "Component.h"
#include <vector>
#include "../Rendering/Renderer_Definitions.h"
#include "../../Math/Matrix.h"
#include "../../Math/BoundingBox.h"
#include "../Rendering/Mesh.h"
//============================================

namespace Spartan
{
    class Material;
    class RHI_VertexBuffer;
    class RHI_IndexBuffer;

    enum class BoundingBoxType
    {
        Untransformed,            // the bounding box of the mesh
        Transformed,              // the transformed bounding box of the mesh
        TransformedInstances,     // the transformed bounding box of all the instances
        TransformedInstanceGroup, // the transformed bounding box of an instance group
    };

    enum RenderableFlags : uint32_t
    {
        OccludedCpu  = 1U << 0, // frustum culling
        OccludedGpu  = 1U << 1, // occlusion culling (depth culling)
        Occluder     = 1U << 2,
        CastsShadows = 1U << 3
    };

    class SP_CLASS Renderable : public Component
    {
    public:
        Renderable(std::weak_ptr<Entity> entity);
        ~Renderable();

        // icomponent
        void Serialize(FileStream* stream) override;
        void Deserialize(FileStream* stream) override;

        // geometry
        void SetGeometry(
            Mesh* mesh,
            const Math::BoundingBox aabb = Math::BoundingBox::Undefined,
            uint32_t index_offset  = 0, uint32_t index_count  = 0,
            uint32_t vertex_offset = 0, uint32_t vertex_count = 0
        );
        void SetGeometry(const MeshType type);
        void GetGeometry(std::vector<uint32_t>* indices, std::vector<RHI_Vertex_PosTexNorTan>* vertices) const;

        // bounding box
        const std::vector<uint32_t>& GetBoundingBoxGroupEndIndices() const { return m_instance_group_end_indices; }
        uint32_t GetInstancePartitionCount() const                         { return static_cast<uint32_t>(m_instance_group_end_indices.size()); }
        const Math::BoundingBox& GetBoundingBox(const BoundingBoxType type, const uint32_t instance_group_index = 0);

        //= MATERIAL ====================================================================
        // Sets a material from memory (adds it to the resource cache by default)
        std::shared_ptr<Material> SetMaterial(const std::shared_ptr<Material>& material);

        // Loads a material and the sets it
        std::shared_ptr<Material> SetMaterial(const std::string& file_path);

        void SetDefaultMaterial();
        std::string GetMaterialName() const;
        Material* GetMaterial() const { return m_material; }
        auto HasMaterial() const      { return m_material != nullptr; }
        //===============================================================================

        // mesh
        RHI_IndexBuffer* GetIndexBuffer() const;
        RHI_VertexBuffer* GetVertexBuffer() const;
        const std::string& GetMeshName() const;

        // instancing
        bool HasInstancing() const                  { return !m_instances.empty(); }
        RHI_VertexBuffer* GetInstanceBuffer() const { return m_instance_buffer.get(); }
        uint32_t GetInstanceCount()  const          { return static_cast<uint32_t>(m_instances.size()); }
        void SetInstances(const std::vector<Math::Matrix>& instances);

        // misc
        uint32_t GetIndexOffset() const  { return m_geometry_index_offset; }
        uint32_t GetIndexCount() const   { return m_geometry_index_count; }
        uint32_t GetVertexOffset() const { return m_geometry_vertex_offset; }
        uint32_t GetVertexCount() const  { return m_geometry_vertex_count; }
        bool IsVisible() const           { return !(m_flags & RenderableFlags::OccludedCpu) && !(m_flags & RenderableFlags::OccludedGpu); }
        bool HasMesh() const             { return m_mesh != nullptr; }

        // flags
        bool HasFlag(const RenderableFlags flag) { return m_flags & flag; }
        void SetFlag(const RenderableFlags flag, const bool enable = true);

    private:
        // geometry/mesh
        uint32_t m_geometry_index_offset  = 0;
        uint32_t m_geometry_index_count   = 0;
        uint32_t m_geometry_vertex_offset = 0;
        uint32_t m_geometry_vertex_count  = 0;
        Mesh* m_mesh                      = nullptr;
        bool m_bounding_box_dirty         = true;
        Math::BoundingBox m_bounding_box_untransformed;
        Math::BoundingBox m_bounding_box;
        Math::BoundingBox m_bounding_box_instances;
        std::vector<Math::BoundingBox> m_bounding_box_instance_group;

        // material
        bool m_material_default = false;
        Material* m_material    = nullptr;

        // instancing
        std::vector<Math::Matrix> m_instances;
        std::vector<uint32_t> m_instance_group_end_indices;
        std::shared_ptr<RHI_VertexBuffer> m_instance_buffer;

        // misc
        Math::Matrix m_transform_previous = Math::Matrix::Identity;
        uint32_t m_flags                  = RenderableFlags::CastsShadows;
    };
}
