/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

#pragma once

//= INCLUDES =================================
#include "Component.h"
#include "../../rendering/Material.h"
#include <vector>
#include <atomic>
#include <limits>
#include "../../math/Matrix.h"
#include "../../math/BoundingBox.h"
#include "../geometry/Mesh.h"
#include "../rendering/Renderer_Definitions.h"
#include "../../rendering/Instance.h"
#include "../../rendering/Renderer_Buffers.h"
//============================================

namespace spartan
{
    class Material;

    enum RenderFlags : uint32_t
    {
        CastsShadows         = 1U << 0,
        // skips blas builds and tlas registration, a per-blade blas for millions of grass instances buys nothing
        ExcludeFromRayTracing = 1U << 1,
        ExcludeFromTerrainBlend = 1U << 2, // manufactured surfaces stay clean, independent of shared material
        PreserveCollisionGeometry = 1U << 3 // thin authored surfaces must survive physics cooking intact
    };

    // per-render uv overrides, each field defaults to nan meaning inherit from the material asset at draw time
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

    class Render;

    // Authoritative hot state, allocated together independently of cold component data.
    // Getters, setters, cloning and loading all address these same fields.
    struct alignas(64) RenderSceneData : PooledObject<RenderSceneData>
    {
        // The first cache line contains the culling inputs and outputs.
        math::BoundingBox bounding_box = math::BoundingBox::Unit;
        math::Vector3 cull_camera_position = math::Vector3::Zero;
        float max_distance_render = FLT_MAX;
        float max_distance_shadow = FLT_MAX;
        float distance_squared = 0.0f;
        uint32_t flags = RenderFlags::CastsShadows;
        uint32_t lod_index = 0;
        uint32_t sub_mesh_index = 0;
        bool is_visible = false;
        bool has_decals = false;
        std::atomic<bool> active{true};

        Entity* entity = nullptr;
        Render* render = nullptr;
        Mesh* mesh = nullptr;
        Material* material = nullptr;
        std::vector<Instance> instances;
    };
    static_assert(sizeof(RenderSceneData) == 128, "Keep render scene data within two cache lines");

    // makes an entity drawable, it owns the mesh, the material and the per-frame lod and visibility state
    class Render : public Component
    {
    public:
        Render(Entity* entity);
        const RenderSceneData& GetSceneData() const { return *m_scene; }
        ~Render();

        // icomponent
        void Save(pugi::xml_node& node) override;
        void Load(pugi::xml_node& node) override;
        void Tick() override;

        static void RegisterForScripting(sol::state_view State);
        sol::reference AsLua(sol::state_view state) override;

        // Runtime deposits follow this receiver, independently of material sharing and mesh UVs.
        struct Decal { DecalParameters parameters; Material* source_material = nullptr; };
        void AddDecal(const DecalParameters& world_decal, Material* source_material = nullptr);
        void ClearDecals() { m_decals.clear(); m_scene->has_decals = false; }
        uint32_t GetDecalCount() const { return static_cast<uint32_t>(m_decals.size()); }
        const std::vector<Decal>& GetDecals() const { return m_decals; }
        static constexpr uint32_t decal_capacity = decal_max_per_receiver;

        // mesh
        void SetMesh(Mesh* mesh, const uint32_t sub_mesh_index = 0);
        void SetOwnedMesh(const std::shared_ptr<Mesh>& mesh);
        void SetMesh(const MeshType type);
        void ClearMesh();
        void GetGeometry(std::vector<uint32_t>* indices, std::vector<RHI_Vertex_PosTexNorTan>* vertices) const;
        uint32_t GetLodCount() const;
        uint32_t GetLodIndex() const { return m_scene->lod_index; }
        uint32_t GetIndexOffset(const uint32_t lod = 0) const;
        uint32_t GetIndexCount(const uint32_t lod = 0) const;
        uint32_t GetVertexOffset(const uint32_t lod = 0) const;
        uint32_t GetVertexCount(const uint32_t lod = 0) const;
        uint32_t GetMeshletOffset(const uint32_t lod = 0) const;
        uint32_t GetMeshletCount(const uint32_t lod = 0) const;
        uint32_t GetGlobalMeshletOffset() const;
        const math::BoundingBox& GetLodAabb(const uint32_t lod = 0) const;
        Mesh* GetMesh() const { return m_scene->mesh; }
        uint32_t GetSubMeshIndex() const { return m_scene->sub_mesh_index; }
        RHI_Buffer* GetIndexBuffer() const;
        RHI_Buffer* GetVertexBuffer() const;
        const std::string& GetMeshName() const;
        void BuildAccelerationStructure();
        void RefitAccelerationStructure();
        bool HasAccelerationStructure() const
        {
            if (!m_scene->mesh)
            {
                return false;
            }

            return m_scene->mesh->HasBlas(m_scene->sub_mesh_index);
        }
        void InvalidateAccelerationStructure();
        uint64_t GetAccelerationStructureDeviceAddress() const;

        // blas refit (for deformable meshes like cloth)
        void SetNeedsBlasRefit(bool v)  { m_needs_blas_refit = v; }
        bool NeedsBlasRefit() const     { return m_needs_blas_refit; }
        void SetAllowBlasUpdate(bool v) { m_allow_blas_update = v; }
        bool GetAllowBlasUpdate() const { return m_allow_blas_update; }

        // bounding box
        const math::BoundingBox& GetBoundingBox() const     { return m_scene->bounding_box; }
        const math::BoundingBox& GetBoundingBoxMesh() const { return m_bounding_box_mesh; }
        // world aabb that ignores entity transform, for ragdoll/cloth etc
        void SetBoundingBoxOverride(const math::BoundingBox& world_box);
        void ClearBoundingBoxOverride();
        bool HasBoundingBoxOverride() const { return m_bounding_box_override; }

        // material
        void SetMaterial(const std::shared_ptr<Material>& material);
        void SetMaterial(const std::string& file_path);
        void SetDefaultMaterial();
        std::string GetMaterialName() const;
        Material* GetMaterial() const           { return m_scene->material; }
        bool IsUsingDefaultMaterial() const     { return m_material_default; }

        // per-render uv transform, nan fields resolve to the material's value at draw time
        const MaterialOverride& GetMaterialOverride() const { return m_material_override; }
        MaterialOverride& GetMaterialOverrideMutable()      { return m_material_override; }
        void ClearMaterialOverride()                        { m_material_override = MaterialOverride{}; }
        // resolves an override field, returning the material default when the override is unset
        float ResolveUvTilingX() const
        {
            if (MaterialOverride::is_set(m_material_override.uv_tiling_x))
            {
                return m_material_override.uv_tiling_x;
            }

            return m_scene->material ? m_scene->material->GetProperty(MaterialProperty::TextureTilingX) : 1.0f;
        }
        float ResolveUvTilingY() const
        {
            if (MaterialOverride::is_set(m_material_override.uv_tiling_y))
            {
                return m_material_override.uv_tiling_y;
            }

            return m_scene->material ? m_scene->material->GetProperty(MaterialProperty::TextureTilingY) : 1.0f;
        }
        float ResolveUvOffsetX() const
        {
            if (MaterialOverride::is_set(m_material_override.uv_offset_x))
            {
                return m_material_override.uv_offset_x;
            }

            return m_scene->material ? m_scene->material->GetProperty(MaterialProperty::TextureOffsetX) : 0.0f;
        }
        float ResolveUvOffsetY() const
        {
            if (MaterialOverride::is_set(m_material_override.uv_offset_y))
            {
                return m_material_override.uv_offset_y;
            }

            return m_scene->material ? m_scene->material->GetProperty(MaterialProperty::TextureOffsetY) : 0.0f;
        }
        float ResolveUvRotation() const
        {
            if (MaterialOverride::is_set(m_material_override.uv_rotation))
            {
                return m_material_override.uv_rotation;
            }

            return m_scene->material ? m_scene->material->GetProperty(MaterialProperty::TextureRotation) : 0.0f;
        }
        float ResolveUvInvertX() const
        {
            if (MaterialOverride::is_set(m_material_override.uv_invert_x))
            {
                return m_material_override.uv_invert_x;
            }

            return m_scene->material ? m_scene->material->GetProperty(MaterialProperty::TextureInvertX) : 0.0f;
        }
        float ResolveUvInvertY() const
        {
            if (MaterialOverride::is_set(m_material_override.uv_invert_y))
            {
                return m_material_override.uv_invert_y;
            }

            return m_scene->material ? m_scene->material->GetProperty(MaterialProperty::TextureInvertY) : 0.0f;
        }
        float ResolveUvWorldSpace() const
        {
            if (MaterialOverride::is_set(m_material_override.uv_world_space))
            {
                return m_material_override.uv_world_space;
            }

            return m_scene->material ? m_scene->material->GetProperty(MaterialProperty::WorldSpaceUv) : 0.0f;
        }

        // instancing
        bool HasInstancing() const                  { return !m_scene->instances.empty(); }
        uint32_t GetInstanceCount()  const          { return m_scene->instances.empty() ? 1 : static_cast<uint32_t>(m_scene->instances.size()); }
        uint32_t GetGlobalInstanceOffset() const    { return m_global_instance_offset; }
        math::Matrix GetInstance(const uint32_t index, const bool to_world);
        math::Vector3 GetInstancePosition(uint32_t index, const math::Matrix& world) const;
        const math::BoundingBox& GetInstanceBounds(uint32_t index) const { return m_instance_bounds[index]; }
        float GetInstanceWindPadding(uint32_t index) const { return m_instance_wind_padding[index]; }
        struct InstanceBoundsGroup
        {
            math::BoundingBox bounds;
            uint32_t offset;
            uint32_t count;
        };
        const std::vector<InstanceBoundsGroup>& GetInstanceBoundsGroups() const { return m_instance_bounds_groups; }
        uint32_t GetGroupedInstanceIndex(uint32_t index) const { return m_instance_bounds_order[index]; }
        void SetInstances(const std::vector<Instance>& instances, bool refresh_bounds = true);
        // Main-thread batch: prepare independent bounds on
        // workers, then update visibility on the caller. Entries must be unique.
        static void RefreshBounds(const std::vector<Render*>& renders);
        void SetInstances(const std::vector<math::Matrix>& transforms);

        // render distance
        float GetMaxRenderDistance() const                         { return m_scene->max_distance_render; }

        void SetMaxRenderDistance(float distance) { m_scene->max_distance_render = distance; }

        // shadow distance
        float GetMaxShadowDistance() const                         { return m_scene->max_distance_shadow; }

        void SetMaxShadowDistance(float distance) { m_scene->max_distance_shadow = distance; }

        // distance & visibility
        float GetDistanceSquared() const    { return m_scene->distance_squared; }
        bool IsVisible() const              { return m_scene->is_visible; }

        void SetVisible(bool visible) { m_scene->is_visible = visible; }

        // flags
        bool HasFlag(const RenderFlags flag) const { return m_scene->flags & flag; }
        bool ExcludesTerrainBlend() const;
        void SetFlag(const RenderFlags flag, const bool enable = true);

        // previous lights tracking
        uint64_t GetPreviousLights() const      { return m_previous_lights; }
        void SetPreviousLights(uint64_t lights) { m_previous_lights = lights; }

        void UpdateAabb();
        void UpdateFrustumAndDistanceCulling();
        void UpdateLodIndices();

    private:
        friend class Entity;
        void SetEntityActive(bool active) { m_scene->active.store(active, std::memory_order_relaxed); }
        std::unique_ptr<RenderSceneData> m_scene;
        std::vector<Decal> m_decals;

        // geometry/mesh
        std::shared_ptr<Mesh> m_owned_mesh; // lifetime of transient procedural geometry
        bool m_bounding_box_dirty             = true;
        bool m_bounding_box_override          = false;
        math::BoundingBox m_bounding_box_mesh = math::BoundingBox::Unit;

        // material
        bool m_material_default = false;
        MaterialOverride m_material_override;

        // instancing

        // Updated with the aggregate AABB on instance, mesh or world-transform changes.
        // Shadow slices reuse these exact bounds instead of unpacking every transform each frame.
        std::vector<math::BoundingBox> m_instance_bounds;
        std::vector<float> m_instance_wind_padding;
        std::vector<InstanceBoundsGroup> m_instance_bounds_groups;
        std::vector<uint32_t> m_instance_bounds_order;
        uint32_t m_global_instance_offset = 0; // 0 means non-instanced reads identity from slot 0 of the global instance pool
        // pool range this renderer appended once, later SetInstances calls that fit rewrite it instead of appending again
        uint32_t m_global_instance_slot          = 0;
        uint32_t m_global_instance_slot_capacity = 0;

        // blas refit
        bool m_needs_blas_refit  = false;
        bool m_allow_blas_update = false;

        // misc
        uint64_t m_bounds_transform_revision = uint64_t(-1);
        bool m_bounds_entity_active = false;
        math::Matrix m_transform_previous = math::Matrix::Identity;

        // deferred default material assignment (renderer may not be ready during load)
        bool m_needs_default_material = false;

        uint64_t m_previous_lights  = 0; // lights whose frustums this entity was in last frame
    };
}
