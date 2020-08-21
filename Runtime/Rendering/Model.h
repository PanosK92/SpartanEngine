/*
Copyright(c) 2016-2020 Panos Karabelas

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
#include <memory>
#include <vector>
#include "Material.h"
#include "../RHI/RHI_Definition.h"
#include "../Resource/IResource.h"
#include "../Math/BoundingBox.h"
//================================

namespace Spartan
{
    class ResourceCache;
    class Entity;
    class Mesh;
    namespace Math{ class BoundingBox; }

    class SPARTAN_CLASS Model : public IResource, public std::enable_shared_from_this<Model>
    {
    public:
        Model(Context* context);
        ~Model();

        void Clear();

        //= IResource ===========================================
        bool LoadFromFile(const std::string& file_path) override;
        bool SaveToFile(const std::string& file_path) override;
        //=======================================================

        // Geometry
        void AppendGeometry(
            const std::vector<uint32_t>& indices,
            const std::vector<RHI_Vertex_PosTexNorTan>& vertices,
            uint32_t* index_offset  = nullptr,
            uint32_t* vertex_offset = nullptr
        ) const;
        void GetGeometry(
            uint32_t index_offset,
            uint32_t index_count,
            uint32_t vertex_offset,
            uint32_t vertex_count,
            std::vector<uint32_t>* indices,
            std::vector<RHI_Vertex_PosTexNorTan>* vertices
        ) const;
        void UpdateGeometry();
        const auto& GetAabb() const { return m_aabb; }
        const auto& GetMesh() const { return m_mesh; }

        // Add resources to the model
        void SetRootEntity(const std::shared_ptr<Entity>& entity) { m_root_entity = entity; }
        void AddMaterial(std::shared_ptr<Material>& material, const std::shared_ptr<Entity>& entity) const;
        void AddTexture(std::shared_ptr<Material>& material, Material_Property texture_type, const std::string& file_path);

        // Misc
        bool IsAnimated()                           const { return m_is_animated; }
        void SetAnimated(const bool is_animated)          { m_is_animated = is_animated; }
        const RHI_IndexBuffer* GetIndexBuffer()     const { return m_index_buffer.get(); }
        const RHI_VertexBuffer* GetVertexBuffer()   const { return m_vertex_buffer.get(); }
        auto GetSharedPtr()                                  { return shared_from_this(); }

    private:
        // Geometry
        bool GeometryCreateBuffers();
        float GeometryComputeNormalizedScale() const;

        // Misc
        std::weak_ptr<Entity> m_root_entity;
        std::shared_ptr<RHI_VertexBuffer> m_vertex_buffer;
        std::shared_ptr<RHI_IndexBuffer> m_index_buffer;
        std::shared_ptr<Mesh> m_mesh;
        Math::BoundingBox m_aabb;
        float m_normalized_scale    = 1.0f;
        bool m_is_animated            = false;

        // Dependencies
        ResourceCache* m_resource_manager;
        std::shared_ptr<RHI_Device> m_rhi_device;    
    };
}
