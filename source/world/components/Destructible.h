/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

#pragma once

//= INCLUDES ======================
#include "Component.h"
#include "../../geometry/MeshFracture.h"
#include "../../math/Quaternion.h"
#include "../../math/Vector3.h"
#include <cstdint>
#include <memory>
#include <unordered_map>
#include <vector>
//=================================

namespace spartan
{
    class Mesh;

    class Destructible : public Component
    {
    public:
        Destructible(Entity* entity);
        ~Destructible() override;

        // component
        void Initialize() override;
        void Remove() override;
        void Start() override;
        void Stop() override;
        void Tick() override;
        void Save(pugi::xml_node& node) override;
        void Load(pugi::xml_node& node) override;

        // configuration
        FracturePattern GetPattern() const { return m_pattern; }
        void SetPattern(FracturePattern pattern) { m_pattern = pattern; }
        uint32_t GetPatternIndex() const { return static_cast<uint32_t>(m_pattern); }
        void SetPatternIndex(uint32_t index);

        uint32_t GetChunkCount() const { return m_chunk_count; }
        void SetChunkCount(uint32_t count);

        float GetImpactThreshold() const { return m_impact_threshold; }
        void SetImpactThreshold(float threshold);

        float GetHealth() const { return m_health; }
        void SetHealth(float health);

        float GetCurrentHealth() const { return m_current_health; }

        float GetDebrisMass() const { return m_debris_mass; }
        void SetDebrisMass(float mass);

        float GetExplosionImpulse() const { return m_explosion_impulse; }
        void SetExplosionImpulse(float impulse);

        float GetInheritVelocity() const { return m_inherit_velocity; }
        void SetInheritVelocity(float factor);

        bool GetSupportAnchored() const { return m_support_anchored; }
        void SetSupportAnchored(bool anchored) { m_support_anchored = anchored; }

        float GetDamageRadius() const { return m_damage_radius; }
        void SetDamageRadius(float radius);

        uint32_t GetMaxDepth() const { return m_max_depth; }
        void SetMaxDepth(uint32_t depth);

        float GetDebrisLifetime() const { return m_debris_lifetime; }
        void SetDebrisLifetime(float seconds);

        float GetInteriorUvScale() const { return m_interior_uv_scale; }
        void SetInteriorUvScale(float scale);

        uint32_t GetSeed() const { return m_seed; }
        void SetSeed(uint32_t seed) { m_seed = seed; }

        // runtime state & actions
        bool IsFractured() const { return m_fractured; }
        uint32_t GetActiveShardCount() const;
        float GetStructuralIntegrity() const;
        bool HasTopSupport() const;

        bool Fracture(
            const math::Vector3& impact_world,
            const math::Vector3& impact_velocity = math::Vector3::Zero,
            float damage_radius_override         = -1.0f,
            bool is_explosion                    = false
        );
        void Reset();

        // world-wide helpers
        static void ApplyExplosion(const math::Vector3& world_origin, float radius, float force);
        static void ResetAll();
        static uint32_t GetTotalActiveShards();

    private:
        struct ShardRecord
        {
            uint64_t entity_id  = 0;
            Entity* entity_ptr  = nullptr;
            uint32_t cell_index = 0;
            float mass          = 1.0f;
            float volume        = 0.0f;
            float age           = 0.0f;
            uint32_t depth      = 1;
            bool detached       = false;
        };

        void EnsurePhysicsTag();
        void ProcessIntactContacts();
        void ProcessShardContacts();
        bool ExecuteFracture(
            const math::Vector3& impact_world,
            const math::Vector3& impact_velocity,
            float damage_radius_override,
            bool is_explosion
        );
        void DetachCell(
            uint32_t shard_index,
            const math::Vector3& impact_world,
            const math::Vector3& impact_velocity,
            float blast_strength,
            float radius_scale,
            bool is_explosion,
            bool gravity_collapse
        );
        void DetachAroundImpact(
            const math::Vector3& impact_world,
            const math::Vector3& impact_velocity,
            float radius,
            float blast_strength,
            bool is_explosion,
            int32_t struck_shard_index = -1
        );
        bool FractureShardSecondary(
            uint32_t shard_index,
            const math::Vector3& impact_world,
            const math::Vector3& impact_velocity
        );
        void ClearDebris();

        // authored parameters
        FracturePattern m_pattern = FracturePattern::Uniform;
        uint32_t m_chunk_count    = 16;
        float m_impact_threshold  = 10.0f;
        float m_health            = 35.0f;
        float m_debris_mass       = 0.0f; // 0 = auto from Physics mass or volume
        float m_explosion_impulse = 1.5f;
        float m_inherit_velocity  = 0.35f;
        bool m_support_anchored   = false;
        float m_damage_radius     = 1.1f;
        uint32_t m_max_depth      = 1;
        float m_debris_lifetime   = 0.0f; // 0 = persist until Reset
        float m_interior_uv_scale = 0.5f;
        uint32_t m_seed           = 0;

        // runtime state
        bool m_fractured                        = false;
        float m_current_health                  = 35.0f;
        float m_fracture_elapsed                = 0.0f;
        float m_grace_timer                     = 0.0f;
        bool m_has_intact_snapshot              = false;
        math::Vector3 m_intact_position         = math::Vector3::Zero;
        math::Quaternion m_intact_rotation      = math::Quaternion::Identity;
        math::Vector3 m_intact_scale            = math::Vector3::One;
        bool m_intact_static                    = true;
        float m_cells_top_y                     = 0.0f;
        float m_cells_height                    = 1.0f;
        float m_total_cell_volume               = 0.0f;

        std::shared_ptr<Mesh> m_shard_mesh;
        std::vector<FractureCell> m_cells;
        std::vector<uint8_t> m_detached;
        std::vector<ShardRecord> m_shards;
        std::unordered_map<Entity*, uint32_t> m_shard_lookup;
    };
}
