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

//= INCLUDES =================================
#include "Component.h"
#include <vector>
#include "../../Math/Matrix.h"
#include "../../Math/BoundingBox.h"
#include "../Rendering/Mesh.h"
#include "../Rendering/Renderer_Definitions.h"
//============================================

namespace spartan
{
    class Material;

    enum RenderableFlags : uint32_t
    {
        CastsShadows = 1U << 0
    };

    class Renderable : public Component
    {
    public:
        Renderable(Entity* entity);
        ~Renderable();

        // icomponent
        void Serialize(FileStream* stream) override;
        void Deserialize(FileStream* stream) override;
        void OnTick() override;

        // mesh
        void SetMesh(Mesh* mesh, const uint32_t sub_mesh_index = 0);
        void SetMesh(const MeshType type);
        void GetGeometry(std::vector<uint32_t>* indices, std::vector<RHI_Vertex_PosTexNorTan>* vertices) const;
        uint32_t GetLodCount() const;
        uint32_t GetLodIndex(const uint32_t instance_group_index = 0) const { return m_lod_indices[instance_group_index]; }
        uint32_t GetIndexOffset(const uint32_t lod = 0) const;
        uint32_t GetIndexCount(const uint32_t lod = 0) const;
        uint32_t GetVertexOffset(const uint32_t lod = 0) const;
        uint32_t GetVertexCount(const uint32_t lod = 0) const;
        RHI_Buffer* GetIndexBuffer() const;
        RHI_Buffer* GetVertexBuffer() const;
        const std::string& GetMeshName() const;
        bool HasMesh() const { return m_mesh != nullptr; }
        bool IsSolid() const;

        // bounding box
        const std::vector<uint32_t>& GetBoundingBoxGroupEndIndices() const               { return m_instance_group_end_indices; }
        uint32_t GetInstanceGroupCount() const                                           { return static_cast<uint32_t>(m_instance_group_end_indices.size()); }
        const math::BoundingBox& GetBoundingBox() const                                  { return m_bounding_box;}
        const math::BoundingBox& GetBoundingBoxInstance(const uint32_t index) const      { return m_bounding_box_instances.empty()      ? math::BoundingBox::Unit : m_bounding_box_instances[index]; }
        const math::BoundingBox& GetBoundingBoxInstanceGroup(const uint32_t index) const { return m_bounding_box_instance_group.empty() ? math::BoundingBox::Unit : m_bounding_box_instance_group[index]; }

        // material
        void SetMaterial(const std::shared_ptr<Material>& material);
        void SetMaterial(const std::string& file_path);
        void SetDefaultMaterial();
        std::string GetMaterialName() const;
        Material* GetMaterial() const { return m_material; }

        // instancing
        const std::vector<math::Matrix>& GetInstances() const   { return m_instances; }
        bool HasInstancing() const                              { return !m_instances.empty(); }
        RHI_Buffer* GetInstanceBuffer() const                   { return m_instance_buffer.get(); }
        math::Matrix GetInstanceTransform(const uint32_t index) { return m_instances[index]; }
        uint32_t GetInstanceCount()  const                      { return static_cast<uint32_t>(m_instances.size()); }
        uint32_t GetInstanceGroupStartIndex(uint32_t group_index) const;
        uint32_t GetInstanceGroupCount(uint32_t group_index) const;
        void SetInstances(const std::vector<math::Matrix>& transforms);
        void SetInstance(const uint32_t index, const math::Matrix& transform);

        // render distance
        float GetMaxRenderDistance() const                         { return m_max_distance_render; }
        void SetMaxRenderDistance(const float max_render_distance) { m_max_distance_render = max_render_distance; }

        // shadow distance
        float GetMaxShadowDistance() const                         { return m_max_distance_shadow; }
        void SetMaxShadowDistance(const float max_shadow_distance) { m_max_distance_shadow = max_shadow_distance; }

        // distance & visibility
        float GetDistanceSquared(const uint32_t instance_group_index = 0) const      { return m_distance_squared[instance_group_index]; }
        bool IsVisible(const uint32_t instance_group_index = 0) const                { return m_is_visible[instance_group_index]; }
        void SetVisible(const bool visible, const uint32_t instance_group_index = 0) { m_is_visible[instance_group_index] = visible; }

        // flags
        bool HasFlag(const RenderableFlags flag) const { return m_flags & flag; }
        void SetFlag(const RenderableFlags flag, const bool enable = true);

        // previous lights tracking
        uint64_t GetPreviousLights() const      { return m_previous_lights; }
        void SetPreviousLights(uint64_t lights) { m_previous_lights = lights; }

    private:
        void UpdateFrustumAndDistanceCulling();
        void UpdateLodIndices();

        // geometry/mesh
        Mesh* m_mesh                          = nullptr;
        uint32_t m_sub_mesh_index             = 0;
        bool m_bounding_box_dirty             = true;
        math::BoundingBox m_bounding_box_mesh = math::BoundingBox::Unit;
        math::BoundingBox m_bounding_box      = math::BoundingBox::Unit;
        std::vector<math::BoundingBox> m_bounding_box_instances;
        std::vector<math::BoundingBox> m_bounding_box_instance_group;

        // material
        bool m_material_default = false;
        Material* m_material    = nullptr;

        // instancing
        std::vector<math::Matrix> m_instances;
        std::vector<uint32_t> m_instance_group_end_indices;
        std::shared_ptr<RHI_Buffer> m_instance_buffer;

        // misc
        math::Matrix m_transform_previous = math::Matrix::Identity;
        uint32_t m_flags                  = RenderableFlags::CastsShadows;

        // visibility & lods
        float m_max_distance_render                                 = FLT_MAX;
        float m_max_distance_shadow                                 = FLT_MAX;
        std::array<float, renderer_max_entities> m_distance_squared = { 0.0f };
        std::array<bool, renderer_max_entities> m_is_visible        = { false };
        std::array<uint32_t, renderer_max_entities> m_lod_indices   = { 0 };
        uint64_t m_previous_lights                                  = 0; // lights whose frustums this renderable was in last frame
    };
}
