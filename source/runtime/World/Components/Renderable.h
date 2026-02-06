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

//= INCLUDES =================================
#include "Component.h"
#include <vector>
#include "../../Math/Matrix.h"
#include "../../Math/BoundingBox.h"
#include "../Geometry/Mesh.h"
#include "../Rendering/Renderer_Definitions.h"
#include "../../Rendering/Instance.h"
//============================================

namespace spartan
{
    class Material;
    class RHI_CommandList;

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
        void Save(pugi::xml_node& node) override;
        void Load(pugi::xml_node& node) override;
        void Tick() override;

        static void RegisterForScripting(sol::state_view State);
        sol::reference AsLua(sol::state_view state) override;

        // mesh
        void SetMesh(Mesh* mesh, const uint32_t sub_mesh_index = 0);
        void SetMesh(const MeshType type);
        void GetGeometry(std::vector<uint32_t>* indices, std::vector<RHI_Vertex_PosTexNorTan>* vertices) const;
        uint32_t GetLodCount() const;
        uint32_t GetLodIndex() const { return m_lod_index; }
        uint32_t GetIndexOffset(const uint32_t lod = 0) const;
        uint32_t GetIndexCount(const uint32_t lod = 0) const;
        uint32_t GetVertexOffset(const uint32_t lod = 0) const;
        uint32_t GetVertexCount(const uint32_t lod = 0) const;
        RHI_Buffer* GetIndexBuffer() const;
        RHI_Buffer* GetVertexBuffer() const;
        const std::string& GetMeshName() const;
        void BuildAccelerationStructure(RHI_CommandList* cmd_list);
        bool HasAccelerationStructure() const;
        uint64_t GetAccelerationStructureDeviceAddress() const;

        // bounding box
        const math::BoundingBox& GetBoundingBox() const { return m_bounding_box;}

        // material
        void SetMaterial(const std::shared_ptr<Material>& material);
        void SetMaterial(const std::string& file_path);
        void SetDefaultMaterial();
        std::string GetMaterialName() const;
        Material* GetMaterial() const { return m_material; }

        // instancing
        bool HasInstancing() const            { return !m_instances.empty(); }
        RHI_Buffer* GetInstanceBuffer() const { return m_instance_buffer.get(); }
        uint32_t GetInstanceCount()  const    { return m_instances.empty() ? 1 : static_cast<uint32_t>(m_instances.size()); }
        math::Matrix GetInstance(const uint32_t index, const bool to_world);
        void SetInstances(const std::vector<Instance>& instances);
        void SetInstances(const std::vector<math::Matrix>& transforms);

        // render distance
        float GetMaxRenderDistance() const                         { return m_max_distance_render; }
        void SetMaxRenderDistance(const float max_render_distance) { m_max_distance_render = max_render_distance; }

        // shadow distance
        float GetMaxShadowDistance() const                         { return m_max_distance_shadow; }
        void SetMaxShadowDistance(const float max_shadow_distance) { m_max_distance_shadow = max_shadow_distance; }

        // distance & visibility
        float GetDistanceSquared() const    { return m_distance_squared; }
        bool IsVisible() const              { return m_is_visible; }
        void SetVisible(const bool visible) { m_is_visible = visible; }

        // flags
        bool HasFlag(const RenderableFlags flag) const { return m_flags & flag; }
        void SetFlag(const RenderableFlags flag, const bool enable = true);

        // previous lights tracking
        uint64_t GetPreviousLights() const      { return m_previous_lights; }
        void SetPreviousLights(uint64_t lights) { m_previous_lights = lights; }

    private:
        void UpdateAabb();
        void UpdateFrustumAndDistanceCulling();
        void UpdateLodIndices();

        // geometry/mesh
        Mesh* m_mesh                          = nullptr;
        uint32_t m_sub_mesh_index             = 0;
        bool m_bounding_box_dirty             = true;
        math::BoundingBox m_bounding_box_mesh = math::BoundingBox::Unit;
        math::BoundingBox m_bounding_box      = math::BoundingBox::Unit;

        // material
        bool m_material_default = false;
        Material* m_material    = nullptr;

        // instancing
        std::vector<Instance> m_instances;
        std::shared_ptr<RHI_Buffer> m_instance_buffer;

        // misc
        math::Matrix m_transform_previous = math::Matrix::Identity;
        uint32_t m_flags                  = RenderableFlags::CastsShadows;

        // deferred default material assignment (renderer may not be ready during load)
        bool m_needs_default_material = false;

        // visibility & lods
        float m_max_distance_render = FLT_MAX;
        float m_max_distance_shadow = FLT_MAX;
        float m_distance_squared    = 0.0f;
        bool m_is_visible           = false;
        uint32_t m_lod_index        = 0;
        uint64_t m_previous_lights  = 0; // lights whose frustums this renderable was in last frame
    };
}
