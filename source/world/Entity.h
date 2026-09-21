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

#include <atomic>
#include <array>
#include <bit>
#include <mutex>
#include <unordered_map>
#include "World.h"
#include "components/Component.h"
#include "../math/Quaternion.h"
#include "../math/Matrix.h"

namespace pugi
{
    class xml_node;
}

namespace spartan
{
    class Render;
    class TerrainSculptLayer;
    using TerrainSculptSnapshots = std::unordered_map<uint64_t, std::shared_ptr<const TerrainSculptLayer>>;

    class Entity : public SpartanObject, public PooledObject<Entity>
    {
    public:
        Entity();
        ~Entity();

        Entity* Clone();

        // clone that keeps only nodes carrying components, pure transform nodes such as
        // skeleton joints are dropped and survivors are flattened onto the root with their
        // accumulated transform baked in, for crowds that animate from bone matrices alone
        Entity* CloneVisualOnly();

        static void RegisterForScripting(sol::state_view State);

        // core
        void Start();
        void Stop();
        void PreTick();
        // Pass false after the render component has already run on a worker.
        void Tick(bool tick_render = true);

        // Compact participation state; its address is stable until this entity dies.
        // Physics sleeps only with a distance proof, not a reduced update frequency.
        struct PreTickGate : PooledObject<PreTickGate>
        {
            Entity* entity = nullptr;
            math::Vector3 sleep_origin = math::Vector3::Zero;
            float sleep_radius_squared = 0.0f;
            bool active = true;
            bool physics_running = false;
            bool other_work = false;
            bool ShouldRun(bool playing, bool has_camera, const math::Vector3& camera_position) const
            {
                if (!active) return false;
                if (other_work || !playing) return true;
                if (!physics_running) return false;
                return !has_camera || sleep_radius_squared <= 0.0f ||
                    math::Vector3::DistanceSquared(camera_position, sleep_origin) >= sleep_radius_squared;
            }
        };
        PreTickGate* GetPreTickGate();
        void RefreshPreTickGate();
        void WakePhysicsPreTick() { if (m_pretick_gate) m_pretick_gate->sleep_radius_squared = 0.0f; }
        void SleepPhysicsPreTick(const math::Vector3& camera_position, float radius);

        // io
        void Save(pugi::xml_node& node);
        // load_children false skips nested Entity nodes, used by the flattened world loader
        void Load(pugi::xml_node& node, bool load_children = true, const TerrainSculptSnapshots* sculpt_snapshots = nullptr);

        // active
        bool GetActive() const { return m_effective_active.load(); }
        bool IsActive() const { return m_is_active.load(); }
        // Inherited mobility, independent of current velocity or play state.
        // Script-driven movers without physics can declare the "dynamic" tag.
        bool IsDynamic() const;
        void SetActive(const bool active);

        // components
        Component* GetComponentByType(ComponentType type) const
        {
            return m_components[static_cast<uint32_t>(type)].get();
        }
        void RemoveComponentByType(ComponentType type);

        template <class T>
        T* AddComponent()
        {
            const ComponentType type = Component::TypeToEnum<T>();

            if (T* component = GetComponent<T>())
            {
                return component;
            }

            auto owned = std::make_unique<T>(this);
            T* component = owned.get();

            m_components[static_cast<uint32_t>(type)] = std::move(owned);
            m_component_mask |= uint64_t(1) << static_cast<uint32_t>(type);

            component->SetType(type);
            component->Initialize();
            RefreshPreTickGate();

            return component;
        }

        Component* AddComponent(ComponentType type);

        template <class T>
        T* GetComponent() const
        {
            return static_cast<T*>(GetComponentByType(Component::TypeToEnum<T>()));
        }

        template <class T>
        void RemoveComponent()
        {
            RemoveComponentByType(Component::TypeToEnum<T>());
        }

        void RemoveComponentById(uint64_t id);
        const auto& GetAllComponents() const { return m_components; }
        uint32_t GetComponentCount() const { return std::popcount(m_component_mask); }
        bool CanTickWithParallelRender() const
        {
            constexpr uint64_t independent = (uint64_t(1) << static_cast<uint32_t>(ComponentType::Render)) |
                                             (uint64_t(1) << static_cast<uint32_t>(ComponentType::Physics));
            return (m_component_mask & ~independent) == 0;
        }

        // position
        uint64_t GetTransformRevision() const { return m_transform_revision; }
        math::Vector3 GetPosition()             const { return m_matrix.GetTranslation(); }
        const math::Vector3& GetPositionLocal() const { return m_position_local; }
        void SetPosition(const math::Vector3& position);
        void SetPositionAndRotation(const math::Vector3& position, const math::Quaternion& rotation);
        void SetPositionLocal(const math::Vector3& position);

        // rotation
        math::Quaternion GetRotation()             const { return m_matrix.GetRotation(); }
        const math::Quaternion& GetRotationLocal() const { return m_rotation_local; }
        void SetRotation(const math::Quaternion& rotation);
        void SetRotationLocal(const math::Quaternion& rotation);

        // scale
        math::Vector3 GetScale()             const { return m_matrix.GetScale(); }
        const math::Vector3& GetScaleLocal() const { return m_scale_local; }
        void SetScale(const math::Vector3& scale);
        void SetScaleLocal(const math::Vector3& scale);

        // translation/rotation
        void Translate(const math::Vector3& delta);
        void Rotate(const math::Quaternion& delta);

        // directions
        // Most moving descendants are render-only. Compute the requested axis
        // from the current matrix instead of updating all six axes on every pose.
        math::Vector3 GetUp() const       { return m_transform_revision ? math::Vector3::Normalize({m_matrix.m10, m_matrix.m11, m_matrix.m12}) : math::Vector3::Zero; }
        math::Vector3 GetDown() const     { return -GetUp(); }
        math::Vector3 GetForward() const  { return m_transform_revision ? math::Vector3::Normalize({m_matrix.m20, m_matrix.m21, m_matrix.m22}) : math::Vector3::Zero; }
        math::Vector3 GetBackward() const { return -GetForward(); }
        math::Vector3 GetRight() const    { return m_transform_revision ? math::Vector3::Normalize({m_matrix.m00, m_matrix.m01, m_matrix.m02}) : math::Vector3::Zero; }
        math::Vector3 GetLeft() const     { return -GetRight(); }

        // hierarchy
        void SetParent(Entity* new_parent);
        Entity* GetChildByIndex(uint32_t index);
        Entity* GetChildByName(const std::string& name);
        Entity* GetDescendantByPath(const std::string& path);
        void AcquireChildren();
        void RemoveChild(Entity* child, bool update_child_with_null_parent = true);
        // drops the parent link without touching the parent, used when the parent is about to be deleted
        void ClearParent();
        void AddChild(Entity* child);
        void MoveChildToIndex(Entity* child, uint32_t index);
        bool IsDescendantOf(Entity* transform) const;
        void GetDescendants(std::vector<Entity*>* descendants);
        Entity* GetDescendantByName(const std::string& name);
        bool HasChildren() const                  { return GetChildrenCount() > 0; }
        uint32_t GetChildrenCount() const;
        Entity* GetParent()                       { return m_parent; }
        // returns a copy under the children mutex, mutate through AddChild/RemoveChild/MoveChildToIndex
        std::vector<Entity*> GetChildren() const;
        uint64_t GetChildDataRevision() const { return m_child_data_revision.load(std::memory_order_relaxed); }

        const math::Matrix& GetMatrix() const              { return m_matrix; }
        const math::Matrix& GetLocalMatrix() const         { return m_matrix_local; }
        const math::Matrix& GetMatrixPrevious() const      { return m_matrix_previous; }
        void SetMatrixPrevious(const math::Matrix& matrix) { m_matrix_previous = matrix; }
        float GetTimeSinceLastTransform() const;

        // prefab support - if set, this entity saves as a prefab reference instead of its children
        void SetPrefabData(const std::string& type, const std::unordered_map<std::string, std::string>& attributes);
        bool HasPrefabData() const                                                             { return !m_prefab_type.empty() || !m_prefab_file_path.empty(); }
        const std::string& GetPrefabType() const                                               { return m_prefab_type; }
        const std::string& GetPrefabFilePath() const                                           { return m_prefab_file_path; }
        const std::unordered_map<std::string, std::string>& GetPrefabAttributes() const        { return m_prefab_attributes; }
        bool IsCodePrefab() const                                                              { return !m_prefab_type.empty(); }
        bool IsFilePrefab() const                                                              { return !m_prefab_file_path.empty(); }
        void SetPrefabFilePath(const std::string& path);
        void ClearPrefabData();

        // prefab baseline ownership - marks the current hierarchy as the prefab base,
        // so anything the user adds afterwards is detected as an override and persisted
        void MarkPrefabBaseline();
        bool IsPrefabOwned() const { return m_prefab_owned; }

        // transient entities are not serialized (e.g. dynamically created entities like flashlights)
        void SetTransient(bool transient)  { m_transient = transient; }
        bool IsTransient() const           { return m_transient; }

        // tags
        // free-form labels (e.g. wheel, wheel_front) so systems can find parts by role
        void AddTag(const std::string& tag);
        void RemoveTag(const std::string& tag);
        bool HasTag(const std::string& tag) const;
        const std::vector<std::string>& GetTags() const { return m_tags; }
        std::string GetTagsString() const;
        void SetTagsString(const std::string& comma_separated);

    private:
        friend class Animator;
        void UpdateActiveState();
        // Animator writes a complete pose, then updates the subtree once.
        void SetTransformLocalDeferred(const math::Vector3& position, const math::Quaternion& rotation, const math::Vector3& scale);
        void UpdateTransform();
        void UpdateTransformSelf();
        // Topology is cached independently of poses. A car moving does not invalidate its traversal.
        struct HierarchyTraversal;
        std::shared_ptr<const HierarchyTraversal> GetHierarchyTraversal() const;
        void InvalidateHierarchy();
        struct TransformCache;
        TransformCache& GetTransformCache() const;
        const math::Matrix& GetMatrixInverse() const;
        const math::Quaternion& GetHierarchyRotation() const;

        // walks a prefab base subtree and writes user additions as <prefab_override> blocks onto the instance root node
        void SaveOverrides(pugi::xml_node& root_node, const std::string& path);
        bool HasPrefabTransformChanged() const;

        std::atomic<bool> m_is_active = true;
        std::atomic<bool> m_effective_active = true;
        bool m_transient              = false; // transient entities are not serialized
        std::array<std::unique_ptr<Component>, static_cast<uint32_t>(ComponentType::Max)> m_components;
        static_assert(static_cast<uint32_t>(ComponentType::Max) < 64);
        uint64_t m_component_mask = 0;
        std::unique_ptr<PreTickGate> m_pretick_gate;

        // local
        uint64_t m_transform_revision = 0;
        std::atomic<uint64_t> m_child_data_revision{0};
        math::Vector3 m_position_local    = math::Vector3::Zero;
        math::Quaternion m_rotation_local = math::Quaternion::Identity;
        math::Vector3 m_scale_local       = math::Vector3::One;

        math::Matrix m_matrix          = math::Matrix::Identity;
        math::Matrix m_matrix_previous = math::Matrix::Identity;
        math::Matrix m_matrix_local    = math::Matrix::Identity;
        bool m_local_matrix_dirty = true;
        Entity* m_transform_parent = nullptr;
        uint64_t m_parent_transform_revision = 0;

        Entity* m_parent = nullptr;      // the parent of this entity
        std::vector<Entity*> m_children; // the children of this entity
        std::atomic<uint32_t> m_children_count{0};
        std::atomic<uint64_t> m_hierarchy_revision{1};
        mutable std::atomic<std::shared_ptr<const HierarchyTraversal>> m_hierarchy_traversal;
        // Only entities queried for inverse/composed rotation pay for this storage.
        mutable std::atomic<TransformCache*> m_transform_cache{nullptr};

        // misc
        mutable std::mutex m_mutex_children;
        mutable std::mutex m_mutex_parent;
        double m_last_transform_time_sec = 0.0;

        // free-form labels, serialized as a comma separated attribute
        std::vector<std::string> m_tags;

        // prefab data (if this entity was created from a prefab)
        std::string m_prefab_type;
        std::string m_prefab_file_path;
        std::unordered_map<std::string, std::string> m_prefab_attributes;

        // m_prefab_owned marks an entity from a prefab base, the mask marks which of its components came from that base
        bool m_prefab_owned = false;
        uint64_t m_prefab_component_mask = 0;
        math::Vector3 m_prefab_position_local    = math::Vector3::Zero;
        math::Quaternion m_prefab_rotation_local = math::Quaternion::Identity;
        math::Vector3 m_prefab_scale_local       = math::Vector3::One;
    };
}
