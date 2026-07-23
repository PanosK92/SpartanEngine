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
#include <limits>
#include "../../Math/Matrix.h"
#include "../../Math/BoundingBox.h"
#include "../Geometry/Mesh.h"
#include "../Rendering/Renderer_Definitions.h"
#include "../../Rendering/Instance.h"
//============================================

namespace spartan
{
    class Material;

    enum RenderableFlags : uint32_t
    {
        CastsShadows         = 1U << 0,
        // exclude the renderable from blas builds and tlas registration, lets foliage with millions of instances skip the ray tracing path entirely
        // grass/flowers don't visibly matter for ray traced reflections/shadows/gi and a per-blade blas would burn gpu memory for no benefit
        ExcludeFromRayTracing = 1U << 1
    };

    // per-renderable material overrides, currently uv only
    // each field defaults to nan, which means inherit from the material asset at draw time,
    // so multiple renderables can share a material and still tweak uv independently
    struct MaterialOverride
    {
        float uv_tiling_x      = std::numeric_limits<float>::quiet_NaN();
        float uv_tiling_y      = std::numeric_limits<float>::quiet_NaN();
        float uv_offset_x      = std::numeric_limits<float>::quiet_NaN();
        float uv_offset_y      = std::numeric_limits<float>::quiet_NaN();
        float uv_rotation      = std::numeric_limits<float>::quiet_NaN();
        float uv_invert_x      = std::numeric_limits<float>::quiet_NaN();
        float uv_invert_y      = std::numeric_limits<float>::quiet_NaN();
        float uv_world_space   = std::numeric_limits<float>::quiet_NaN();

        static bool is_set(float v)  { return v == v; } // nan != nan
        static float unset()         { return std::numeric_limits<float>::quiet_NaN(); }
    };

    class Render : public Component
    {
    public:
        Render(Entity* entity);
        ~Render();

        // icomponent
        void Save(pugi::xml_node& node) override;
        void Load(pugi::xml_node& node) override;
        void Tick() override;

        static void RegisterForScripting(sol::state_view State);
        sol::reference AsLua(sol::state_view state) override;

        // mesh
        void SetMesh(Mesh* mesh, const uint32_t sub_mesh_index = 0);
        void SetMesh(const MeshType type);
        void ClearMesh();
        void GetGeometry(std::vector<uint32_t>* indices, std::vector<RHI_Vertex_PosTexNorTan>* vertices) const;
        uint32_t GetLodCount() const;
        uint32_t GetLodIndex() const { return m_lod_index; }
        uint32_t GetIndexOffset(const uint32_t lod = 0) const;
        uint32_t GetIndexCount(const uint32_t lod = 0) const;
        uint32_t GetVertexOffset(const uint32_t lod = 0) const;
        uint32_t GetVertexCount(const uint32_t lod = 0) const;
        uint32_t GetMeshletOffset(const uint32_t lod = 0) const;
        uint32_t GetMeshletCount(const uint32_t lod = 0) const;
        uint32_t GetGlobalMeshletOffset() const;
        const math::BoundingBox& GetLodAabb(const uint32_t lod = 0) const;
        Mesh* GetMesh() const { return m_mesh; }
        uint32_t GetSubMeshIndex() const { return m_sub_mesh_index; }
        RHI_Buffer* GetIndexBuffer() const;
        RHI_Buffer* GetVertexBuffer() const;
        const std::string& GetMeshName() const;
        void BuildAccelerationStructure(RHI_CommandList* cmd_list);
        void RefitAccelerationStructure(RHI_CommandList* cmd_list);
        bool HasAccelerationStructure() const;
        void InvalidateAccelerationStructure();
        uint64_t GetAccelerationStructureDeviceAddress() const;

        // blas refit (for deformable meshes like cloth)
        void SetNeedsBlasRefit(bool v)  { m_needs_blas_refit = v; }
        bool NeedsBlasRefit() const     { return m_needs_blas_refit; }
        void SetAllowBlasUpdate(bool v) { m_allow_blas_update = v; }
        bool GetAllowBlasUpdate() const { return m_allow_blas_update; }

        // bounding box
        const math::BoundingBox& GetBoundingBox() const     { return m_bounding_box; }
        const math::BoundingBox& GetBoundingBoxMesh() const { return m_bounding_box_mesh; }

        // material
        void SetMaterial(const std::shared_ptr<Material>& material);
        void SetMaterial(const std::string& file_path);
        void SetDefaultMaterial();
        std::string GetMaterialName() const;
        Material* GetMaterial() const           { return m_material; }
        bool IsUsingDefaultMaterial() const     { return m_material_default; }

        // per-renderable material overrides (uv transform)
        // the override defaults to nan and resolves to the material's value at draw time
        const MaterialOverride& GetMaterialOverride() const { return m_material_override; }
        MaterialOverride& GetMaterialOverrideMutable()      { return m_material_override; }
        void ClearMaterialOverride()                        { m_material_override = MaterialOverride{}; }
        // resolves an override field, returning the material default when the override is unset
        float ResolveUvTilingX() const;
        float ResolveUvTilingY() const;
        float ResolveUvOffsetX() const;
        float ResolveUvOffsetY() const;
        float ResolveUvRotation() const;
        float ResolveUvInvertX() const;
        float ResolveUvInvertY() const;
        float ResolveUvWorldSpace() const;

        // instancing
        bool HasInstancing() const                  { return !m_instances.empty(); }
        uint32_t GetInstanceCount()  const          { return m_instances.empty() ? 1 : static_cast<uint32_t>(m_instances.size()); }
        uint32_t GetGlobalInstanceOffset() const    { return m_global_instance_offset; }
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
        MaterialOverride m_material_override;

        // instancing
        std::vector<Instance> m_instances;
        uint32_t m_global_instance_offset = 0; // 0 means non-instanced reads identity from slot 0 of the global instance pool

        // blas refit
        bool m_needs_blas_refit  = false;
        bool m_allow_blas_update = false;

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
