/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

//= INCLUDES ========================
#include "pch.h"
#include "Destructible.h"
#include "Physics.h"
#include "Render.h"
#include "../Entity.h"
#include "../World.h"
#include "../../core/Engine.h"
#include "../../core/ThreadPool.h"
#include "../../core/Timer.h"
#include "../../geometry/Mesh.h"
#include "../../physics/PhysicsWorld.h"
#include "../../io/pugixml.hpp"
#include <algorithm>
#include <cmath>
#include <deque>
//===================================

//= NAMESPACES ===============
using namespace std;
using namespace spartan::math;
//============================

namespace spartan
{
    namespace
    {
        struct PendingFracture
        {
            Destructible* target   = nullptr;
            Vector3 impact_world   = Vector3::Zero;
            Vector3 impact_vel     = Vector3::Zero;
            float radius_override  = -1.0f;
            bool is_explosion      = false;
        };

        vector<Destructible*> active_destructibles;
        deque<PendingFracture> pending_fractures;

        constexpr uint32_t max_world_debris_shards     = 320;
        constexpr float min_secondary_volume           = 0.08f;
        constexpr uint32_t max_primary_per_frame       = 2;
        constexpr uint32_t max_secondary_per_frame     = 1;
        constexpr double secondary_cooldown_sec        = 0.08;

        double budget_frame_time            = -1.0;
        uint32_t primary_fractures_frame    = 0;
        uint32_t secondary_fractures_frame  = 0;
        double last_secondary_time          = -1.0;

        void refresh_frame_budget()
        {
            const double now = Timer::GetTimeSec();
            if (now != budget_frame_time)
            {
                budget_frame_time         = now;
                primary_fractures_frame   = 0;
                secondary_fractures_frame = 0;
            }
        }

        void remove_pending_for(const Destructible* target)
        {
            erase_if(pending_fractures, [target](const PendingFracture& pf)
            {
                return pf.target == target;
            });
        }

        bool is_pending_for(const Destructible* target)
        {
            return any_of(pending_fractures.begin(), pending_fractures.end(), [target](const PendingFracture& pf)
            {
                return pf.target == target;
            });
        }

        void drain_pending_fractures()
        {
            refresh_frame_budget();
            while (!pending_fractures.empty() && primary_fractures_frame < max_primary_per_frame)
            {
                PendingFracture pf = pending_fractures.front();
                pending_fractures.pop_front();
                if (!pf.target)
                {
                    continue;
                }
                if (find(active_destructibles.begin(), active_destructibles.end(), pf.target) == active_destructibles.end())
                {
                    continue;
                }
                Entity* e = pf.target->GetEntity();
                if (!e || !e->GetActive())
                {
                    continue;
                }
                pf.target->Fracture(pf.impact_world, pf.impact_vel, pf.radius_override, pf.is_explosion);
            }
        }

        float hash_unit(uint32_t seed)
        {
            uint32_t x = seed;
            x ^= x >> 16;
            x *= 0x7FEB352Du;
            x ^= x >> 15;
            x *= 0x846CA68Bu;
            x ^= x >> 16;
            return static_cast<float>(x & 0x00FFFFFFu) / static_cast<float>(0x01000000u);
        }

        Vector3 hash_direction(uint32_t seed)
        {
            const float x = hash_unit(seed * 3u + 1u) * 2.0f - 1.0f;
            const float y = hash_unit(seed * 3u + 2u) * 2.0f - 1.0f;
            const float z = hash_unit(seed * 3u + 3u) * 2.0f - 1.0f;
            Vector3 v(x, y, z);
            const float len = v.Length();
            return len > 1.0e-4f ? (v / len) : Vector3::Up;
        }

        float compute_contact_energy(const PhysicsContact& contact, Entity* self, Entity* other, float target_structure_mass, Vector3& out_impact_velocity)
        {
            Physics* ps = self ? self->GetComponent<Physics>() : nullptr;
            Physics* po = other ? other->GetComponent<Physics>() : nullptr;

            // Static-vs-static contacts (e.g. a wall resting inside a frame or on the floor) never produce impact energy
            if (ps && ps->IsStatic() && (!po || po->IsStatic()))
            {
                out_impact_velocity = Vector3::Zero;
                return 0.0f;
            }

            const bool self_is_a = (contact.entity_a == self);
            // contact.relative_velocity is A - B, so velocity of other relative to self is:
            Vector3 rel_vel = self_is_a ? -contact.relative_velocity : contact.relative_velocity;

            // Fallback if pre-solver velocity wasn't populated (e.g. swept-CCD contact from a custom actor)
            if (rel_vel.LengthSquared() < 1.0e-4f)
            {
                const Vector3 vel_other = (po && !po->IsStatic()) ? (po->GetPreStepVelocity().LengthSquared() > po->GetLinearVelocity().LengthSquared() ? po->GetPreStepVelocity() : po->GetLinearVelocity()) : Vector3::Zero;
                const Vector3 vel_self  = (ps && !ps->IsStatic()) ? (ps->GetPreStepVelocity().LengthSquared() > ps->GetLinearVelocity().LengthSquared() ? ps->GetPreStepVelocity() : ps->GetLinearVelocity()) : Vector3::Zero;
                rel_vel = vel_other - vel_self;
            }

            out_impact_velocity = rel_vel;
            const float speed   = rel_vel.Length();

            // Project onto contact normal so tangential scraping/sliding transfers minimal crushing stress
            const float normal_len_sq = contact.normal.LengthSquared();
            float normal_speed        = speed;
            if (normal_len_sq > 1.0e-4f)
            {
                const Vector3 n = contact.normal / sqrtf(normal_len_sq);
                normal_speed    = max(fabsf(Vector3::Dot(rel_vel, n)), speed * 0.25f);
            }

            // Resting weight / slow settling contacts never fracture solid structures
            if (normal_speed < 2.2f)
            {
                return 0.0f;
            }

            float effective_mass = 8.0f;
            if (po)
            {
                effective_mass = po->IsStatic() ? 25.0f : clamp(po->GetMass(), 0.5f, 500.0f);
            }

            // Quadratic kinetic-energy scaling (v_n^2 * sqrt(m) * 0.035):
            // high-speed projectiles & heavy demolition hammers smash masonry cleanly while
            // slow tumbling debris chunks cannot domino-shatter neighboring intact structures.
            float energy = normal_speed * normal_speed * sqrtf(effective_mass) * 0.035f;

            // Irregular tumbling rubble glancing off heavy monolithic stone/masonry transfers much less
            // concentrated crushing stress than a hardened projectile or demolition hammer.
            const bool other_is_rubble = other && other->IsTransient() && po && po->GetBodyType() == BodyType::Mesh;
            if (other_is_rubble && target_structure_mass >= 400.0f)
            {
                energy *= 0.40f;
            }

            return energy;
        }

        // Compute realistic shard launch velocity:
        // - gravity_collapse: unsupported stone/masonry simply breaks loose and falls under gravity (no fake bomb push)
        // - kinetic impact (is_explosion == false): directional momentum transfer along projectile velocity with modest local scatter
        // - explosion (is_explosion == true): true radial shockwave from blast origin
        void compute_shard_velocity(
            const Vector3& shard_pos,
            const Vector3& impact_world,
            const Vector3& impact_velocity,
            const Vector3& inherited_body_vel,
            float shard_mass,
            float blast_strength,
            float inherit_velocity,
            float radius_norm,
            bool is_explosion,
            bool gravity_collapse,
            uint32_t seed,
            Vector3& out_linear,
            Vector3& out_angular
        )
        {
            const float heaviness = clamp(sqrtf(30.0f / max(shard_mass, 1.0f)), 0.22f, 1.25f);

            if (gravity_collapse)
            {
                // Unsupported piece slips downward and topples gently under gravity with mass-appropriate inertia
                Vector3 horiz = shard_pos - impact_world;
                horiz.y = 0.0f;
                const float hlen = horiz.Length();
                const Vector3 slip_dir = hlen > 1.0e-3f ? (horiz / hlen) : hash_direction(seed + 19u);
                out_linear  = inherited_body_vel + slip_dir * (0.22f * heaviness) + Vector3(0.0f, -0.25f, 0.0f);
                out_angular = hash_direction(seed * 47u + 13u) * (0.38f * heaviness);
                return;
            }

            Vector3 to_shard = shard_pos - impact_world;
            const float dist = to_shard.Length();
            const Vector3 radial = dist > 1.0e-3f ? (to_shard / dist) : hash_direction(seed + 17u);

            if (is_explosion)
            {
                const float falloff = clamp(1.0f - (dist / max(radius_norm * 1.85f, 2.0f)), 0.12f, 1.0f);
                const float inv_m   = clamp(sqrtf(28.0f / max(shard_mass, 1.0f)), 0.25f, 1.35f);
                Vector3 vel = inherited_body_vel + radial * (blast_strength * falloff * inv_m) + Vector3(0.0f, blast_strength * 0.25f * falloff * inv_m, 0.0f);
                if (impact_velocity.LengthSquared() > 1.0e-4f)
                {
                    vel += impact_velocity * (inherit_velocity * 0.35f * falloff);
                }
                const float max_speed = 24.0f;
                const float speed     = vel.Length();
                if (speed > max_speed)
                {
                    vel *= (max_speed / speed);
                }
                out_linear  = vel;
                out_angular = hash_direction(seed * 31u + 7u) * ((1.2f + blast_strength * 0.30f * falloff) * heaviness);
            }
            else
            {
                // Kinetic impact: localized forward chip/punch with heavy-mass inertia so stone blocks feel genuinely weighty
                const float impact_speed  = impact_velocity.Length();
                const Vector3 impact_dir  = impact_speed > 1.0e-3f ? (impact_velocity / impact_speed) : radial;
                const float falloff       = clamp(1.0f - (dist / max(radius_norm * 1.45f, 0.6f)), 0.08f, 1.0f);
                const float mass_transfer = clamp(24.0f / (shard_mass + 24.0f), 0.05f, 0.60f);

                // Forward punch along projectile + modest lateral spall scatter perpendicular to impact
                Vector3 lateral = radial - impact_dir * Vector3::Dot(radial, impact_dir);
                const float lat_len = lateral.Length();
                if (lat_len > 1.0e-4f)
                {
                    lateral /= lat_len;
                }

                Vector3 vel = inherited_body_vel
                            + impact_dir * (impact_speed * inherit_velocity * mass_transfer * falloff)
                            + lateral * (blast_strength * 0.40f * mass_transfer * falloff)
                            + Vector3(0.0f, 0.18f * heaviness * falloff, 0.0f);

                const float max_speed = 14.0f;
                const float speed     = vel.Length();
                if (speed > max_speed)
                {
                    vel *= (max_speed / speed);
                }
                out_linear  = vel;
                out_angular = hash_direction(seed * 31u + 7u) * ((0.55f + min(speed, 10.0f) * 0.22f) * heaviness);
            }
        }
    }

    Destructible::Destructible(Entity* entity) : Component(entity)
    {
        SP_REGISTER_ATTRIBUTE_GET_SET(GetPatternIndex, SetPatternIndex, uint32_t);
        SP_REGISTER_ATTRIBUTE_GET_SET(GetChunkCount, SetChunkCount, uint32_t);
        SP_REGISTER_ATTRIBUTE_GET_SET(GetImpactThreshold, SetImpactThreshold, float);
        SP_REGISTER_ATTRIBUTE_GET_SET(GetHealth, SetHealth, float);
        SP_REGISTER_ATTRIBUTE_GET_SET(GetDebrisMass, SetDebrisMass, float);
        SP_REGISTER_ATTRIBUTE_GET_SET(GetExplosionImpulse, SetExplosionImpulse, float);
        SP_REGISTER_ATTRIBUTE_GET_SET(GetInheritVelocity, SetInheritVelocity, float);
        SP_REGISTER_ATTRIBUTE_GET_SET(GetSupportAnchored, SetSupportAnchored, bool);
        SP_REGISTER_ATTRIBUTE_GET_SET(GetDamageRadius, SetDamageRadius, float);
        SP_REGISTER_ATTRIBUTE_GET_SET(GetMaxDepth, SetMaxDepth, uint32_t);
        SP_REGISTER_ATTRIBUTE_GET_SET(GetDebrisLifetime, SetDebrisLifetime, float);
        SP_REGISTER_ATTRIBUTE_GET_SET(GetInteriorUvScale, SetInteriorUvScale, float);
        SP_REGISTER_ATTRIBUTE_GET_SET(GetSeed, SetSeed, uint32_t);

        active_destructibles.push_back(this);
    }

    Destructible::~Destructible()
    {
        remove_pending_for(this);
        ClearDebris();
        erase(active_destructibles, this);
    }

    void Destructible::Initialize()
    {
        m_current_health = m_health;
        EnsurePhysicsTag();
    }

    void Destructible::Remove()
    {
        remove_pending_for(this);
        ClearDebris();
        if (Entity* entity = GetEntity())
        {
            if (Render* render = entity->GetComponent<Render>())
            {
                render->SetRenderActive(true);
            }
            if (Physics* physics = entity->GetComponent<Physics>())
            {
                if (physics->GetCollisionTag() == physics_collision_destructible)
                {
                    physics->SetCollisionTag(0);
                }
                physics->SetInstanceSuppressed(0, false);
                physics->SetEntityActive(entity->GetActive());
            }
        }
    }

    void Destructible::Start()
    {
        remove_pending_for(this);
        m_current_health   = m_health;
        m_fracture_elapsed = 0.0f;
        m_grace_timer      = 0.45f;
        m_fractured        = false;

        if (Entity* entity = GetEntity())
        {
            m_intact_position      = entity->GetPosition();
            m_intact_rotation      = entity->GetRotation();
            m_intact_scale         = entity->GetScale();
            m_has_intact_snapshot  = true;

            if (Physics* physics = entity->GetComponent<Physics>())
            {
                m_intact_static = physics->IsStatic();
                physics->SetCollisionTag(physics_collision_destructible);
            }
        }
    }

    void Destructible::Stop()
    {
        Reset();
    }

    void Destructible::Tick()
    {
        if (!Engine::IsPlayingUnpaused())
        {
            return;
        }

        drain_pending_fractures();

        const float dt = static_cast<float>(Timer::GetDeltaTimeSec());

        if (!m_fractured)
        {
            EnsurePhysicsTag();
            if (m_grace_timer > 0.0f)
            {
                m_grace_timer = max(0.0f, m_grace_timer - dt);
                return;
            }
            ProcessIntactContacts();
            return;
        }

        m_fracture_elapsed += dt;

        for (ShardRecord& shard : m_shards)
        {
            if (shard.detached)
            {
                shard.age += dt;
            }
        }

        ProcessShardContacts();

        // Optional lifetime cleanup for detached debris
        if (m_debris_lifetime > 0.0f)
        {
            for (ShardRecord& shard : m_shards)
            {
                if (shard.detached && shard.entity_id != 0 && shard.age >= m_debris_lifetime)
                {
                    if (shard.entity_ptr)
                    {
                        m_shard_lookup.erase(shard.entity_ptr);
                    }
                    if (Entity* e = World::GetEntityById(shard.entity_id))
                    {
                        World::RemoveEntity(e);
                    }
                    shard.entity_id  = 0;
                    shard.entity_ptr = nullptr;
                }
            }
        }
    }

    void Destructible::SetPatternIndex(uint32_t index)
    {
        if (index >= static_cast<uint32_t>(FracturePattern::Max))
        {
            index = 0;
        }
        m_pattern = static_cast<FracturePattern>(index);
    }

    void Destructible::SetChunkCount(uint32_t count)
    {
        m_chunk_count = clamp(count, 4u, 64u);
    }

    void Destructible::SetImpactThreshold(float threshold)
    {
        m_impact_threshold = max(0.5f, threshold);
    }

    void Destructible::SetHealth(float health)
    {
        m_health = max(1.0f, health);
        if (!m_fractured)
        {
            m_current_health = m_health;
        }
    }

    void Destructible::SetDebrisMass(float mass)
    {
        m_debris_mass = max(0.0f, mass);
    }

    void Destructible::SetExplosionImpulse(float impulse)
    {
        m_explosion_impulse = clamp(impulse, 0.0f, 50.0f);
    }

    void Destructible::SetInheritVelocity(float factor)
    {
        m_inherit_velocity = clamp(factor, 0.0f, 1.5f);
    }

    void Destructible::SetDamageRadius(float radius)
    {
        m_damage_radius = clamp(radius, 0.0f, 25.0f);
    }

    void Destructible::SetMaxDepth(uint32_t depth)
    {
        m_max_depth = clamp(depth, 1u, 2u);
    }

    void Destructible::SetDebrisLifetime(float seconds)
    {
        m_debris_lifetime = max(0.0f, seconds);
    }

    void Destructible::SetInteriorUvScale(float scale)
    {
        m_interior_uv_scale = clamp(scale, 0.05f, 10.0f);
    }

    uint32_t Destructible::GetActiveShardCount() const
    {
        uint32_t count = 0;
        for (const ShardRecord& shard : m_shards)
        {
            if (shard.entity_id != 0)
            {
                ++count;
            }
        }
        return count;
    }

    float Destructible::GetStructuralIntegrity() const
    {
        if (!m_fractured)
        {
            return 1.0f;
        }
        if (!m_support_anchored || m_total_cell_volume <= 1.0e-5f)
        {
            return 0.0f;
        }

        float anchored_vol = 0.0f;
        for (size_t i = 0; i < m_cells.size() && i < m_detached.size(); ++i)
        {
            if (!m_detached[i])
            {
                anchored_vol += m_cells[i].volume;
            }
        }
        return clamp(anchored_vol / m_total_cell_volume, 0.0f, 1.0f);
    }

    bool Destructible::HasTopSupport() const
    {
        if (!m_fractured)
        {
            return true;
        }
        if (!m_support_anchored)
        {
            return false;
        }

        const float top_tol = max(0.12f, m_cells_height * 0.14f);
        for (size_t i = 0; i < m_cells.size() && i < m_detached.size(); ++i)
        {
            if (!m_detached[i])
            {
                const float cell_top_y = m_cells[i].centroid.y + m_cells[i].bounds.GetMax().y;
                if (cell_top_y >= m_cells_top_y - top_tol)
                {
                    return true;
                }
            }
        }
        return false;
    }

    uint32_t Destructible::GetTotalActiveShards()
    {
        uint32_t total = 0;
        for (const Destructible* d : active_destructibles)
        {
            if (d)
            {
                total += d->GetActiveShardCount();
            }
        }
        return total;
    }

    void Destructible::EnsurePhysicsTag()
    {
        Entity* entity = GetEntity();
        if (!entity)
        {
            return;
        }
        if (Physics* physics = entity->GetComponent<Physics>())
        {
            if (physics->GetCollisionTag() != physics_collision_destructible)
            {
                physics->SetCollisionTag(physics_collision_destructible);
            }
        }
    }

    void Destructible::ProcessIntactContacts()
    {
        Entity* self = GetEntity();
        if (!self || !self->GetActive())
        {
            return;
        }

        const vector<PhysicsContact>& contacts = PhysicsWorld::GetFrameContacts();
        if (contacts.empty())
        {
            return;
        }

        float best_energy = 0.0f;
        Vector3 best_pos  = self->GetPosition();
        Vector3 best_vel  = Vector3::Zero;

        for (const PhysicsContact& contact : contacts)
        {
            Entity* other = nullptr;
            if (contact.entity_a == self)
            {
                other = contact.entity_b;
            }
            else if (contact.entity_b == self)
            {
                other = contact.entity_a;
            }
            else
            {
                continue;
            }

            Vector3 impact_vel = Vector3::Zero;
            const float energy = compute_contact_energy(contact, self, other, m_debris_mass, impact_vel);
            if (energy > best_energy)
            {
                best_energy = energy;
                best_pos    = contact.position;
                best_vel    = impact_vel;
            }
        }

        if (best_energy < m_impact_threshold)
        {
            return;
        }

        // Impacts above threshold deal damage; heavy hits exceed health and fracture locally at impact point.
        const float damage = (best_energy - m_impact_threshold * 0.5f) * 1.15f;
        m_current_health  -= damage;
        if (m_current_health <= 0.0f)
        {
            Fracture(best_pos, best_vel, -1.0f, false);
        }
    }

    void Destructible::ProcessShardContacts()
    {
        const vector<PhysicsContact>& contacts = PhysicsWorld::GetFrameContacts();
        if (contacts.empty() || m_shard_lookup.empty())
        {
            return;
        }

        // O(contacts) dispatch via m_shard_lookup instead of O(shards * contacts)
        int32_t best_anchored_idx  = -1;
        float best_anchored_energy = 0.0f;
        Vector3 best_anchored_pos  = Vector3::Zero;
        Vector3 best_anchored_vel  = Vector3::Zero;

        int32_t best_sub_idx  = -1;
        float best_sub_energy = 0.0f;
        Vector3 best_sub_pos  = Vector3::Zero;
        Vector3 best_sub_vel  = Vector3::Zero;

        auto inspect_side = [&](Entity* shard_entity, Entity* other, const PhysicsContact& contact)
        {
            auto it = m_shard_lookup.find(shard_entity);
            if (it == m_shard_lookup.end())
            {
                return;
            }

            const uint32_t idx = it->second;
            if (idx >= m_shards.size() || m_shards[idx].entity_id == 0)
            {
                return;
            }

            const ShardRecord& rec  = m_shards[idx];
            const bool is_anchored  = !rec.detached;
            const bool can_subbreak = rec.detached && rec.depth < m_max_depth && rec.volume >= min_secondary_volume && rec.age >= 0.18f;
            if (!is_anchored && !can_subbreak)
            {
                return;
            }

            Vector3 impact_vel = Vector3::Zero;
            const float energy = compute_contact_energy(contact, shard_entity, other, is_anchored ? m_debris_mass : rec.mass, impact_vel);
            if (is_anchored && energy > best_anchored_energy)
            {
                best_anchored_energy = energy;
                best_anchored_idx    = static_cast<int32_t>(idx);
                best_anchored_pos    = contact.position;
                best_anchored_vel    = impact_vel;
            }
            else if (can_subbreak && energy > best_sub_energy)
            {
                best_sub_energy = energy;
                best_sub_idx    = static_cast<int32_t>(idx);
                best_sub_pos    = contact.position;
                best_sub_vel    = impact_vel;
            }
        };

        for (const PhysicsContact& contact : contacts)
        {
            if (contact.entity_a)
            {
                inspect_side(contact.entity_a, contact.entity_b, contact);
            }
            if (contact.entity_b)
            {
                inspect_side(contact.entity_b, contact.entity_a, contact);
            }
        }

        if (best_anchored_idx >= 0 && best_anchored_energy >= m_impact_threshold * 0.90f)
        {
            DetachAroundImpact(best_anchored_pos, best_anchored_vel, m_damage_radius, m_explosion_impulse, false, best_anchored_idx);
            return;
        }

        if (best_sub_idx >= 0 && best_sub_energy >= m_impact_threshold * 1.60f && GetTotalActiveShards() < max_world_debris_shards)
        {
            refresh_frame_budget();
            const double now = Timer::GetTimeSec();
            if (secondary_fractures_frame < max_secondary_per_frame && (now - last_secondary_time) >= secondary_cooldown_sec)
            {
                if (FractureShardSecondary(static_cast<uint32_t>(best_sub_idx), best_sub_pos, best_sub_vel))
                {
                    ++secondary_fractures_frame;
                    last_secondary_time = now;
                }
            }
        }
    }

    bool Destructible::Fracture(const Vector3& impact_world, const Vector3& impact_velocity, float damage_radius_override, bool is_explosion)
    {
        if (m_fractured)
        {
            if (m_support_anchored)
            {
                const float radius = damage_radius_override >= 0.0f ? damage_radius_override : m_damage_radius;
                const float blast  = is_explosion ? max(m_explosion_impulse, impact_velocity.Length() * 0.65f) : m_explosion_impulse;
                DetachAroundImpact(impact_world, impact_velocity, radius, blast, is_explosion);
                return true;
            }
            return false;
        }

        // In live play mode, pace simultaneous multi-entity fractures across frames to prevent frame-time spikes
        if (Engine::IsPlayingUnpaused())
        {
            refresh_frame_budget();
            if (primary_fractures_frame >= max_primary_per_frame)
            {
                if (!is_pending_for(this))
                {
                    PendingFracture pf;
                    pf.target          = this;
                    pf.impact_world    = impact_world;
                    pf.impact_vel      = impact_velocity;
                    pf.radius_override = damage_radius_override;
                    pf.is_explosion    = is_explosion;
                    pending_fractures.push_back(pf);
                }
                return true;
            }
            ++primary_fractures_frame;
        }

        return ExecuteFracture(impact_world, impact_velocity, damage_radius_override, is_explosion);
    }

    bool Destructible::ExecuteFracture(const Vector3& impact_world, const Vector3& impact_velocity, float damage_radius_override, bool is_explosion)
    {
        if (m_fractured)
        {
            return false;
        }

        Entity* entity = GetEntity();
        if (!entity)
        {
            return false;
        }

        Render* render = entity->GetComponent<Render>();
        if (!render || !render->HasGeometry())
        {
            return false;
        }

        vector<uint32_t> source_indices;
        vector<RHI_Vertex_PosTexNorTan> source_vertices;
        render->GetGeometry(&source_indices, &source_vertices);
        if (source_vertices.empty() || source_indices.size() < 3)
        {
            return false;
        }

        const Vector3 world_pos    = entity->GetPosition();
        const Quaternion world_rot = entity->GetRotation();
        const Vector3 world_scale  = entity->GetScale();

        if (!m_has_intact_snapshot)
        {
            m_intact_position     = world_pos;
            m_intact_rotation     = world_rot;
            m_intact_scale        = world_scale;
            m_has_intact_snapshot = true;
            if (Physics* physics = entity->GetComponent<Physics>())
            {
                m_intact_static = physics->IsStatic();
            }
        }

        // Convert impact point into scaled metric space relative to entity origin
        const Vector3 impact_metric = world_rot.Conjugate() * (impact_world - world_pos);

        FractureParams params;
        params.seed              = m_seed != 0 ? m_seed : static_cast<uint32_t>(entity->GetObjectId() ^ (entity->GetObjectId() >> 32));
        params.chunk_count       = m_chunk_count;
        params.pattern           = m_pattern;
        params.impact_local      = impact_metric;
        params.has_impact_point  = true;
        params.interior_uv_scale = m_interior_uv_scale;

        m_cells.clear();
        if (!mesh_fracture::Fracture(source_vertices, source_indices, world_scale, params, m_cells))
        {
            return false;
        }

        const uint32_t cell_count = static_cast<uint32_t>(m_cells.size());

        // Build one shared resident mesh holding every shard as its own submesh for a single GPU upload.
        // Skip disk cache and use linear meshlet scan so runtime fracturing never hits the filesystem.
        auto shared_mesh = make_shared<Mesh>();
        shared_mesh->SetObjectName(entity->GetObjectName() + "_shards_" + to_string(entity->GetObjectId()));
        shared_mesh->SetDynamic(true);
        shared_mesh->SetFlag(static_cast<uint32_t>(MeshFlags::PostProcessSkipCache), true);
        shared_mesh->SetFlag(static_cast<uint32_t>(MeshFlags::PostProcessMeshletsScan), true);
        shared_mesh->ReserveSubMeshes(cell_count);

        // Compute cell meshlets AND PhysX QuickHull convex meshes in parallel across all worker threads
        vector<void*> cooked_hulls(cell_count, nullptr);
        auto build_shards_parallel = [&](uint32_t start, uint32_t end)
        {
            vector<Vector3> hull_points;
            for (uint32_t i = start; i < end; ++i)
            {
                vector<RHI_Vertex_PosTexNorTan> verts = m_cells[i].vertices;
                vector<uint32_t> inds                 = m_cells[i].indices;
                shared_mesh->AddGeometry(verts, inds, i);

                hull_points.clear();
                hull_points.reserve(m_cells[i].vertices.size());
                for (const RHI_Vertex_PosTexNorTan& v : m_cells[i].vertices)
                {
                    // Inset convex hull by 4% toward cell origin so tightly-packed Voronoi neighbors never interpenetrate on spawn
                    hull_points.push_back(v.get_position() * 0.96f);
                }
                cooked_hulls[i] = Physics::CookConvexMeshFromPoints(hull_points, 32);
            }
        };

        if (cell_count >= 4 && ThreadPool::GetThreadCount() > 1)
        {
            ThreadPool::ParallelLoop(build_shards_parallel, cell_count);
        }
        else
        {
            build_shards_parallel(0u, cell_count);
        }

        shared_mesh->CreateGpuBuffers();
        m_shard_mesh = shared_mesh;

        float total_volume = 0.0f;
        float min_cell_y   = FLT_MAX;
        float max_cell_y   = -FLT_MAX;
        for (const FractureCell& c : m_cells)
        {
            total_volume += c.volume;
            min_cell_y    = min(min_cell_y, c.centroid.y + c.bounds.GetMin().y);
            max_cell_y    = max(max_cell_y, c.centroid.y + c.bounds.GetMax().y);
        }
        m_total_cell_volume = total_volume;
        m_cells_top_y       = max_cell_y;
        m_cells_height      = max(max_cell_y - min_cell_y, 0.1f);

        // Read physics properties from intact body before hiding it
        Physics* source_physics          = entity->GetComponent<Physics>();
        const float friction             = source_physics ? source_physics->GetFriction() : 0.65f;
        const float friction_rolling     = source_physics ? source_physics->GetFrictionRolling() : 0.65f;
        const float restitution          = source_physics ? source_physics->GetRestitution() : 0.12f;
        const Vector3 inherited_body_vel = (source_physics && !source_physics->IsStatic()) ? source_physics->GetLinearVelocity() : Vector3::Zero;

        float total_mass = m_debris_mass;
        if (total_mass <= 0.0f)
        {
            if (source_physics && !source_physics->IsStatic() && source_physics->GetMass() > 0.1f)
            {
                total_mass = source_physics->GetMass();
            }
            else
            {
                const float density = (m_pattern == FracturePattern::SplinterWood) ? 650.0f : ((m_pattern == FracturePattern::RadialGlass) ? 2500.0f : 2200.0f);
                total_mass = clamp(total_volume * density, 2.0f, 15000.0f);
            }
        }

        // Hide intact visual and suppress its physics actor so distance streaming cannot re-add it on top of spawned shards
        render->SetRenderActive(false);
        if (source_physics)
        {
            source_physics->SetInstanceSuppressed(0, true);
            source_physics->SetEntityActive(false);
        }

        // Determine which cells detach directly from impact vs collapse under gravity vs stay anchored
        const float effective_radius = damage_radius_override >= 0.0f ? damage_radius_override : m_damage_radius;
        m_detached.assign(cell_count, 0);
        vector<uint8_t> gravity_collapsed(cell_count, 0);

        if (!m_support_anchored || effective_radius <= 0.0f)
        {
            fill(m_detached.begin(), m_detached.end(), static_cast<uint8_t>(1));
            // When an unanchored heavy structure fractures with low velocity (e.g. roof collapse under gravity),
            // let it collapse naturally under gravity unless an actual explosion hit it.
            if (!is_explosion && impact_velocity.LengthSquared() < 9.0f)
            {
                fill(gravity_collapsed.begin(), gravity_collapsed.end(), static_cast<uint8_t>(1));
            }
        }
        else
        {
            uint32_t detached_count = 0;
            uint32_t closest_cell   = 0;
            float closest_dist_sq   = FLT_MAX;

            for (size_t i = 0; i < m_cells.size(); ++i)
            {
                const BoundingBox cell_box(m_cells[i].bounds.GetMin() + m_cells[i].centroid, m_cells[i].bounds.GetMax() + m_cells[i].centroid);
                const Vector3 closest     = cell_box.GetClosestPoint(impact_metric);
                const float closest_dist  = Vector3::Distance(closest, impact_metric);
                const float centroid_dist = Vector3::Distance(m_cells[i].centroid, impact_metric);
                // Kinetic hits check blended centroid distance so a hit chips the struck side without slicing straight through thick structures
                const float eval_dist     = is_explosion ? closest_dist : (centroid_dist * 0.65f + closest_dist * 0.35f);
                const float dist_sq       = eval_dist * eval_dist;
                if (dist_sq < closest_dist_sq)
                {
                    closest_dist_sq = dist_sq;
                    closest_cell    = static_cast<uint32_t>(i);
                }
                if (eval_dist <= effective_radius)
                {
                    m_detached[i] = 1;
                    ++detached_count;
                }
            }

            if (detached_count == 0 && !m_cells.empty())
            {
                m_detached[closest_cell] = 1;
            }

            // Structural connectivity collapse: anything cut off from the ground drops naturally under gravity
            vector<uint32_t> unsupported;
            mesh_fracture::FindUnsupportedCells(m_cells, m_detached, unsupported);
            for (uint32_t idx : unsupported)
            {
                m_detached[idx]        = 1;
                gravity_collapsed[idx] = 1;
            }
        }

        const bool casts_shadows = render->HasFlag(RenderFlags::CastsShadows);
        const bool no_terrain    = render->HasFlag(RenderFlags::ExcludeFromTerrainBlend);
        const float blast_speed  = is_explosion ? max(m_explosion_impulse, impact_velocity.Length() * 0.75f) : m_explosion_impulse;
        const float radius_norm  = max(effective_radius, 0.8f);

        m_shards.clear();
        m_shards.reserve(cell_count);
        m_shard_lookup.clear();
        m_shard_lookup.reserve(cell_count);

        const uint32_t base_seed = static_cast<uint32_t>(entity->GetObjectId());
        for (uint32_t i = 0; i < cell_count; ++i)
        {
            const FractureCell& cell = m_cells[i];
            const Vector3 shard_pos  = world_pos + world_rot * cell.centroid;
            const float shard_mass   = clamp(total_mass * (cell.volume / max(total_volume, 1.0e-5f)), 0.4f, 4000.0f);

            Entity* shard = World::CreateEntity();
            shard->SetTransient(true);
            shard->SetObjectName(entity->GetObjectName() + "_shard_" + to_string(i));
            shard->SetPosition(shard_pos);
            shard->SetRotation(world_rot);
            shard->SetScale(Vector3::One);

            Render* sr = shard->AddComponent<Render>();
            sr->SetOwnedMesh(shared_mesh, i);
            sr->CopyMaterialFrom(*render);
            sr->SetFlag(RenderFlags::CastsShadows, casts_shadows);
            sr->SetFlag(RenderFlags::ExcludeFromTerrainBlend, no_terrain);
            sr->SetSpatiallyDynamic(true);

            Physics* sp = shard->AddComponent<Physics>();
            if (cooked_hulls[i])
            {
                sp->SetPreCookedConvexMesh(cooked_hulls[i]);
                cooked_hulls[i] = nullptr;
            }
            PhysicsSettings settings;
            const bool is_heavy_chunk   = shard_mass >= 40.0f;
            settings.distance_streaming = false;
            settings.friction           = is_heavy_chunk ? max(friction, 0.78f) : friction;
            settings.friction_rolling   = is_heavy_chunk ? max(friction_rolling, 0.72f) : friction_rolling;
            settings.restitution        = is_heavy_chunk ? min(restitution, 0.04f) : restitution;
            settings.mass               = shard_mass;
            settings.use_convex_hull    = true;
            settings.is_static          = !m_detached[i];
            settings.body_type          = BodyType::Mesh;
            sp->ApplySettings(settings);
            sp->Rebuild();

            const bool is_detached  = (m_detached[i] != 0);
            const bool track_events = !is_detached || (m_max_depth > 1 && cell.volume >= min_secondary_volume);
            if (track_events)
            {
                sp->SetCollisionTag(physics_collision_destructible);
                m_shard_lookup[shard] = i;
            }

            ShardRecord record;
            record.entity_id  = shard->GetObjectId();
            record.entity_ptr = shard;
            record.cell_index = i;
            record.mass       = shard_mass;
            record.volume     = cell.volume;
            record.age        = 0.0f;
            record.depth      = 1;
            record.detached   = is_detached;
            m_shards.push_back(record);

            if (is_detached)
            {
                Vector3 lin_vel = Vector3::Zero;
                Vector3 ang_vel = Vector3::Zero;
                compute_shard_velocity(
                    shard_pos,
                    impact_world,
                    impact_velocity,
                    inherited_body_vel,
                    shard_mass,
                    blast_speed,
                    m_inherit_velocity,
                    radius_norm,
                    is_explosion,
                    gravity_collapsed[i] != 0,
                    base_seed + i * 31u,
                    lin_vel,
                    ang_vel
                );
                sp->SetLinearVelocity(lin_vel);
                sp->SetAngularVelocity(ang_vel);
            }
        }

        // Release any unadopted hulls in case of early exit
        for (void* hull : cooked_hulls)
        {
            if (hull)
            {
                Physics::ReleaseCookedConvexMesh(hull);
            }
        }

        m_fractured        = true;
        m_fracture_elapsed = 0.0f;
        return true;
    }

    void Destructible::DetachCell(
        uint32_t shard_index,
        const Vector3& impact_world,
        const Vector3& impact_velocity,
        float blast_strength,
        float radius_scale,
        bool is_explosion,
        bool gravity_collapse
    )
    {
        if (shard_index >= m_shards.size() || m_shards[shard_index].detached)
        {
            return;
        }

        ShardRecord& record = m_shards[shard_index];
        record.detached = true;
        record.age      = 0.0f;
        if (record.cell_index < m_detached.size())
        {
            m_detached[record.cell_index] = 1;
        }

        Entity* shard = World::GetEntityById(record.entity_id);
        if (!shard)
        {
            return;
        }

        Physics* sp = shard->GetComponent<Physics>();
        if (!sp)
        {
            return;
        }

        sp->SetMass(record.mass);
        sp->SetStatic(false);
        if (m_max_depth <= 1 || record.volume < min_secondary_volume)
        {
            sp->SetCollisionTag(0);
            m_shard_lookup.erase(shard);
        }

        Vector3 lin_vel = Vector3::Zero;
        Vector3 ang_vel = Vector3::Zero;
        compute_shard_velocity(
            shard->GetPosition(),
            impact_world,
            impact_velocity,
            Vector3::Zero,
            record.mass,
            blast_strength,
            m_inherit_velocity,
            radius_scale,
            is_explosion,
            gravity_collapse,
            shard_index * 79u + 11u,
            lin_vel,
            ang_vel
        );
        sp->SetLinearVelocity(lin_vel);
        sp->SetAngularVelocity(ang_vel);
    }

    void Destructible::DetachAroundImpact(const Vector3& impact_world, const Vector3& impact_velocity, float radius, float blast_strength, bool is_explosion, int32_t struck_shard_index)
    {
        const float effective_radius = radius > 0.0f ? radius : max(m_damage_radius, 0.8f);

        uint32_t detached_now = 0;
        int32_t closest_idx   = -1;
        float closest_dist_sq = FLT_MAX;

        // Always detach the exact anchored shard that absorbed the high-energy impact first
        if (struck_shard_index >= 0 && static_cast<size_t>(struck_shard_index) < m_shards.size() && !m_shards[struck_shard_index].detached && m_shards[struck_shard_index].entity_id != 0)
        {
            DetachCell(static_cast<uint32_t>(struck_shard_index), impact_world, impact_velocity, blast_strength, effective_radius, is_explosion, false);
            ++detached_now;
        }

        for (size_t i = 0; i < m_shards.size(); ++i)
        {
            if (m_shards[i].detached || m_shards[i].entity_id == 0)
            {
                continue;
            }
            Entity* shard = World::GetEntityById(m_shards[i].entity_id);
            if (!shard)
            {
                continue;
            }
            const float centroid_dist = Vector3::Distance(shard->GetPosition(), impact_world);
            float box_dist            = centroid_dist;
            if (Render* sr = shard->GetComponent<Render>())
            {
                const BoundingBox& world_box = sr->GetBoundingBox();
                box_dist = Vector3::Distance(world_box.GetClosestPoint(impact_world), impact_world);
            }
            const float eval_dist = is_explosion ? box_dist : (centroid_dist * 0.55f + box_dist * 0.45f);
            const float d2        = eval_dist * eval_dist;
            if (d2 < closest_dist_sq)
            {
                closest_dist_sq = d2;
                closest_idx     = static_cast<int32_t>(i);
            }
            if (eval_dist <= effective_radius)
            {
                DetachCell(static_cast<uint32_t>(i), impact_world, impact_velocity, blast_strength, effective_radius, is_explosion, false);
                ++detached_now;
            }
        }

        if (detached_now == 0 && closest_idx >= 0 && closest_dist_sq <= (effective_radius * 2.2f) * (effective_radius * 2.2f))
        {
            DetachCell(static_cast<uint32_t>(closest_idx), impact_world, impact_velocity, blast_strength, effective_radius, is_explosion, false);
            ++detached_now;
        }

        if (detached_now > 0)
        {
            // Re-evaluate structural connectivity to the ground and collapse anything left floating naturally under gravity
            vector<uint32_t> unsupported;
            mesh_fracture::FindUnsupportedCells(m_cells, m_detached, unsupported);
            for (uint32_t cell_idx : unsupported)
            {
                for (size_t s = 0; s < m_shards.size(); ++s)
                {
                    if (!m_shards[s].detached && m_shards[s].cell_index == cell_idx)
                    {
                        DetachCell(static_cast<uint32_t>(s), impact_world, Vector3::Zero, 0.0f, effective_radius, false, true);
                        break;
                    }
                }
            }
        }
    }

    bool Destructible::FractureShardSecondary(uint32_t shard_index, const Vector3& impact_world, const Vector3& impact_velocity)
    {
        if (shard_index >= m_shards.size())
        {
            return false;
        }

        ShardRecord parent_rec = m_shards[shard_index];
        Entity* parent_shard   = World::GetEntityById(parent_rec.entity_id);
        if (!parent_shard)
        {
            return false;
        }

        Render* parent_render   = parent_shard->GetComponent<Render>();
        Physics* parent_physics = parent_shard->GetComponent<Physics>();
        if (!parent_render || !parent_render->HasGeometry())
        {
            return false;
        }

        vector<uint32_t> src_indices;
        vector<RHI_Vertex_PosTexNorTan> src_vertices;
        parent_render->GetGeometry(&src_indices, &src_vertices);
        if (src_vertices.empty() || src_indices.size() < 3)
        {
            return false;
        }

        const Vector3 shard_pos    = parent_shard->GetPosition();
        const Quaternion shard_rot = parent_shard->GetRotation();
        const Vector3 parent_vel   = parent_physics ? parent_physics->GetLinearVelocity() : Vector3::Zero;
        const float friction       = parent_physics ? parent_physics->GetFriction() : 0.65f;
        const float friction_roll  = parent_physics ? parent_physics->GetFrictionRolling() : 0.65f;
        const float restitution    = parent_physics ? parent_physics->GetRestitution() : 0.12f;

        FractureParams params;
        params.seed              = static_cast<uint32_t>(parent_rec.entity_id) ^ 0xA5A55A5Au;
        params.chunk_count       = 4;
        params.pattern           = m_pattern;
        params.impact_local      = shard_rot.Conjugate() * (impact_world - shard_pos);
        params.has_impact_point  = true;
        params.interior_uv_scale = m_interior_uv_scale;
        params.min_volume        = 0.004f;

        vector<FractureCell> sub_cells;
        if (!mesh_fracture::Fracture(src_vertices, src_indices, Vector3::One, params, sub_cells) || sub_cells.size() < 2)
        {
            return false;
        }

        const uint32_t sub_count = static_cast<uint32_t>(sub_cells.size());
        auto sub_mesh = make_shared<Mesh>();
        sub_mesh->SetObjectName(parent_shard->GetObjectName() + "_sub");
        sub_mesh->SetDynamic(true);
        sub_mesh->SetFlag(static_cast<uint32_t>(MeshFlags::PostProcessSkipCache), true);
        sub_mesh->SetFlag(static_cast<uint32_t>(MeshFlags::PostProcessMeshletsScan), true);
        sub_mesh->ReserveSubMeshes(sub_count);

        vector<void*> cooked_hulls(sub_count, nullptr);
        float total_vol = 0.0f;
        vector<Vector3> hull_points;
        for (uint32_t i = 0; i < sub_count; ++i)
        {
            vector<RHI_Vertex_PosTexNorTan> v = sub_cells[i].vertices;
            vector<uint32_t> idx              = sub_cells[i].indices;
            sub_mesh->AddGeometry(v, idx, i);
            total_vol += sub_cells[i].volume;

            hull_points.clear();
            hull_points.reserve(sub_cells[i].vertices.size());
            for (const RHI_Vertex_PosTexNorTan& vert : sub_cells[i].vertices)
            {
                hull_points.push_back(vert.get_position() * 0.96f);
            }
            cooked_hulls[i] = Physics::CookConvexMeshFromPoints(hull_points, 24);
        }
        sub_mesh->CreateGpuBuffers();

        const bool casts_shadows = parent_render->HasFlag(RenderFlags::CastsShadows);
        const bool no_terrain    = parent_render->HasFlag(RenderFlags::ExcludeFromTerrainBlend);

        for (uint32_t i = 0; i < sub_count; ++i)
        {
            const FractureCell& cell = sub_cells[i];
            const Vector3 pos        = shard_pos + shard_rot * cell.centroid;
            const float mass         = clamp(parent_rec.mass * (cell.volume / max(total_vol, 1.0e-5f)), 0.25f, 1000.0f);

            Entity* child = World::CreateEntity();
            child->SetTransient(true);
            child->SetObjectName(parent_shard->GetObjectName() + "_c" + to_string(i));
            child->SetPosition(pos);
            child->SetRotation(shard_rot);
            child->SetScale(Vector3::One);

            Render* sr = child->AddComponent<Render>();
            sr->SetOwnedMesh(sub_mesh, i);
            sr->CopyMaterialFrom(*parent_render);
            sr->SetFlag(RenderFlags::CastsShadows, casts_shadows);
            sr->SetFlag(RenderFlags::ExcludeFromTerrainBlend, no_terrain);
            sr->SetSpatiallyDynamic(true);

            Physics* sp = child->AddComponent<Physics>();
            if (cooked_hulls[i])
            {
                sp->SetPreCookedConvexMesh(cooked_hulls[i]);
                cooked_hulls[i] = nullptr;
            }
            PhysicsSettings settings;
            const bool is_heavy_chunk   = mass >= 40.0f;
            settings.distance_streaming = false;
            settings.friction           = is_heavy_chunk ? max(friction, 0.78f) : friction;
            settings.friction_rolling   = is_heavy_chunk ? max(friction_roll, 0.72f) : friction_roll;
            settings.restitution        = is_heavy_chunk ? min(restitution, 0.04f) : restitution;
            settings.mass               = mass;
            settings.use_convex_hull    = true;
            settings.is_static          = false;
            settings.body_type          = BodyType::Mesh;
            sp->ApplySettings(settings);
            sp->Rebuild();

            const float heaviness = clamp(sqrtf(30.0f / max(mass, 1.0f)), 0.25f, 1.25f);
            Vector3 outward = pos - impact_world;
            outward = outward.LengthSquared() > 1.0e-5f ? outward.Normalized() : hash_direction(i + 91u);
            Vector3 vel = parent_vel * 0.55f + outward * (0.95f * heaviness) + impact_velocity * (0.10f * heaviness) + Vector3(0.0f, 0.25f * heaviness, 0.0f);
            sp->SetLinearVelocity(vel);
            sp->SetAngularVelocity(hash_direction(i * 41u + 7u) * (1.8f * heaviness));

            ShardRecord sub_rec;
            sub_rec.entity_id  = child->GetObjectId();
            sub_rec.entity_ptr = child;
            sub_rec.cell_index = UINT32_MAX;
            sub_rec.mass       = mass;
            sub_rec.volume     = cell.volume;
            sub_rec.age        = 0.0f;
            sub_rec.depth      = parent_rec.depth + 1;
            sub_rec.detached   = true;
            m_shards.push_back(sub_rec);
        }

        for (void* hull : cooked_hulls)
        {
            if (hull)
            {
                Physics::ReleaseCookedConvexMesh(hull);
            }
        }

        // Remove the parent shard now that its sub-shards are live
        m_shard_lookup.erase(parent_shard);
        m_shards[shard_index].entity_id  = 0;
        m_shards[shard_index].entity_ptr = nullptr;
        World::RemoveEntity(parent_shard);
        return true;
    }

    void Destructible::ClearDebris()
    {
        for (ShardRecord& shard : m_shards)
        {
            if (shard.entity_id != 0)
            {
                if (Entity* entity = World::GetEntityById(shard.entity_id))
                {
                    World::RemoveEntity(entity);
                }
                shard.entity_id  = 0;
                shard.entity_ptr = nullptr;
            }
        }
        m_shards.clear();
        m_shard_lookup.clear();
        m_cells.clear();
        m_detached.clear();
        m_shard_mesh.reset();
    }

    void Destructible::Reset()
    {
        remove_pending_for(this);
        ClearDebris();

        m_fractured        = false;
        m_fracture_elapsed = 0.0f;
        m_grace_timer      = 0.35f;
        m_current_health   = m_health;

        Entity* entity = GetEntity();
        if (!entity)
        {
            return;
        }

        if (m_has_intact_snapshot)
        {
            entity->SetPosition(m_intact_position);
            entity->SetRotation(m_intact_rotation);
            entity->SetScale(m_intact_scale);
        }

        if (Render* render = entity->GetComponent<Render>())
        {
            render->SetRenderActive(entity->GetActive());
        }

        if (Physics* physics = entity->GetComponent<Physics>())
        {
            if (m_has_intact_snapshot)
            {
                physics->SetStatic(m_intact_static);
                physics->SetBodyTransform(m_intact_position, m_intact_rotation, true);
            }
            physics->SetCollisionTag(physics_collision_destructible);
            physics->SetInstanceSuppressed(0, false);
            physics->SetEntityActive(entity->GetActive());
            if (m_has_intact_snapshot && !m_intact_static)
            {
                physics->SetLinearVelocity(Vector3::Zero);
                physics->SetAngularVelocity(Vector3::Zero);
            }
        }
    }

    void Destructible::ApplyExplosion(const Vector3& world_origin, float radius, float force)
    {
        if (radius <= 0.01f)
        {
            return;
        }

        // Collect affected destructibles sorted nearest-first so the closest targets shatter immediately
        // and outer ones cascade smoothly via the per-frame budget without stalling the main thread.
        struct BlastTarget
        {
            Destructible* destructible = nullptr;
            Vector3 closest            = Vector3::Zero;
            float dist_sq              = 0.0f;
        };

        vector<BlastTarget> blast_targets;
        const float radius_sq = radius * radius;

        for (Destructible* destructible : active_destructibles)
        {
            if (!destructible)
            {
                continue;
            }
            Entity* entity = destructible->GetEntity();
            if (!entity || !entity->GetActive())
            {
                continue;
            }

            Vector3 closest = entity->GetPosition();
            if (!destructible->IsFractured())
            {
                if (Render* render = entity->GetComponent<Render>())
                {
                    closest = render->GetBoundingBox().GetClosestPoint(world_origin);
                }
            }
            const float dist_sq = Vector3::DistanceSquared(closest, world_origin);
            if (dist_sq <= radius_sq)
            {
                blast_targets.push_back({ destructible, closest, dist_sq });
            }
        }

        sort(blast_targets.begin(), blast_targets.end(), [](const BlastTarget& a, const BlastTarget& b)
        {
            return a.dist_sq < b.dist_sq;
        });

        for (const BlastTarget& bt : blast_targets)
        {
            const float dist    = sqrtf(bt.dist_sq);
            const float falloff = clamp(1.0f - dist / radius, 0.2f, 1.0f);
            Vector3 dir         = bt.closest - world_origin;
            dir                 = dir.LengthSquared() > 1.0e-5f ? dir.Normalized() : Vector3::Up;
            const Vector3 blast_vel = dir * (force * falloff) + Vector3(0.0f, force * 0.25f * falloff, 0.0f);

            if (!bt.destructible->IsFractured())
            {
                bt.destructible->Fracture(world_origin, blast_vel, radius * 0.70f, true);
            }
            else if (bt.destructible->GetSupportAnchored())
            {
                bt.destructible->DetachAroundImpact(world_origin, blast_vel, radius * 0.70f, force * falloff, true);
            }
        }

        // Apply radial blast velocity to existing dynamic rigid bodies and shards within range
        for (Entity* entity : World::GetEntities())
        {
            if (!entity || !entity->GetActive())
            {
                continue;
            }
            if (Destructible* d = entity->GetComponent<Destructible>(); d && d->IsFractured())
            {
                continue;
            }
            Physics* physics = entity->GetComponent<Physics>();
            if (!physics || physics->IsStatic() || physics->IsKinematic() || physics->GetBodyType() == BodyType::Controller)
            {
                continue;
            }

            const Vector3 pos   = entity->GetPosition();
            const Vector3 delta = pos - world_origin;
            const float dist_sq = delta.LengthSquared();
            if (dist_sq > radius_sq || dist_sq < 1.0e-6f)
            {
                continue;
            }

            const float dist    = sqrtf(dist_sq);
            const float falloff = clamp(1.0f - dist / radius, 0.1f, 1.0f);
            const Vector3 dir   = delta / dist;
            const float inv_m   = clamp(12.0f / max(physics->GetMass(), 1.0f), 0.15f, 1.4f);
            Vector3 vel         = physics->GetLinearVelocity() + (dir * (force * falloff * inv_m)) + Vector3(0.0f, force * 0.22f * falloff * inv_m, 0.0f);
            if (vel.Length() > 26.0f)
            {
                vel = vel.Normalized() * 26.0f;
            }
            physics->SetLinearVelocity(vel);
        }
    }

    void Destructible::ResetAll()
    {
        pending_fractures.clear();
        vector<Destructible*> targets = active_destructibles;
        for (Destructible* destructible : targets)
        {
            if (destructible)
            {
                destructible->Reset();
            }
        }
    }

    void Destructible::Save(pugi::xml_node& node)
    {
        node.append_attribute("pattern")           = static_cast<uint32_t>(m_pattern);
        node.append_attribute("chunk_count")       = m_chunk_count;
        node.append_attribute("impact_threshold")  = m_impact_threshold;
        node.append_attribute("health")            = m_health;
        node.append_attribute("debris_mass")       = m_debris_mass;
        node.append_attribute("explosion_impulse") = m_explosion_impulse;
        node.append_attribute("inherit_velocity")  = m_inherit_velocity;
        node.append_attribute("support_anchored")  = m_support_anchored;
        node.append_attribute("damage_radius")     = m_damage_radius;
        node.append_attribute("max_depth")         = m_max_depth;
        node.append_attribute("debris_lifetime")   = m_debris_lifetime;
        node.append_attribute("interior_uv_scale") = m_interior_uv_scale;
        node.append_attribute("seed")              = m_seed;
    }

    void Destructible::Load(pugi::xml_node& node)
    {
        SetPatternIndex(node.attribute("pattern").as_uint(0));
        SetChunkCount(node.attribute("chunk_count").as_uint(16));
        SetImpactThreshold(node.attribute("impact_threshold").as_float(10.0f));
        SetHealth(node.attribute("health").as_float(35.0f));
        SetDebrisMass(node.attribute("debris_mass").as_float(0.0f));
        SetExplosionImpulse(node.attribute("explosion_impulse").as_float(1.5f));
        SetInheritVelocity(node.attribute("inherit_velocity").as_float(0.35f));
        SetSupportAnchored(node.attribute("support_anchored").as_bool(false));
        SetDamageRadius(node.attribute("damage_radius").as_float(1.1f));
        SetMaxDepth(node.attribute("max_depth").as_uint(1));
        SetDebrisLifetime(node.attribute("debris_lifetime").as_float(0.0f));
        SetInteriorUvScale(node.attribute("interior_uv_scale").as_float(0.5f));
        SetSeed(node.attribute("seed").as_uint(0));
    }
}
