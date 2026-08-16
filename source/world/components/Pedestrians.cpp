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

#include "pch.h"
#include "Pedestrians.h"
#include "Animator.h"
#include "Camera.h"
#include "Ragdoll.h"
#include "Render.h"
#include "../../core/Engine.h"
#include "../../core/ThreadPool.h"
#include "../../core/Timer.h"
#include "../../geometry/Mesh.h"
#include "../../math/Quaternion.h"
#include "../../physics/PhysicsWorld.h"
#include "../../resource/ResourceCache.h"
#include "../Entity.h"
#include "../World.h"
#include "../../io/pugixml.hpp"

using namespace std;
using namespace spartan::math;

namespace spartan
{
    namespace
    {
        constexpr float sensor_height = 1.0f;
        constexpr float sensor_radius = 0.35f;
        constexpr float look_ahead = 1.8f;
        constexpr float ground_ray_up = 1.5f;
        constexpr float ground_ray_down = 4.0f;
        constexpr float turn_speed = 8.0f;
        constexpr float spawn_clearance = 1.2f;

        Vector3 planar_normalize(Vector3 value)
        {
            value.y = 0.0f;
            if (value.LengthSquared() > 0.0001f)
            {
                value.Normalize();
            }
            else
            {
                value = Vector3(0.0f, 0.0f, -1.0f);
            }
            return value;
        }
    }

    Pedestrians::Pedestrians(Entity* entity) : Component(entity)
    {
        SP_REGISTER_ATTRIBUTE_VALUE_VALUE(m_bounds_min, Vector3);
        SP_REGISTER_ATTRIBUTE_VALUE_VALUE(m_bounds_max, Vector3);
        SP_REGISTER_ATTRIBUTE_VALUE_VALUE(m_model_file, string);
        SP_REGISTER_ATTRIBUTE_VALUE_VALUE(m_count, uint32_t);
        SP_REGISTER_ATTRIBUTE_VALUE_VALUE(m_max_animated, uint32_t);
        SP_REGISTER_ATTRIBUTE_VALUE_VALUE(m_animation_radius, float);
        SP_REGISTER_ATTRIBUTE_VALUE_VALUE(m_walk_speed, float);
    }

    Pedestrians::~Pedestrians()
    {
        // never RemoveEntity here, World::Shutdown/RemoveEntity may already hold entity_access_mutex
        CancelPreload();
        for (Walker& walker : m_walkers)
        {
            if (walker.animator)
            {
                walker.animator->Stop();
            }
            if (walker.entity)
            {
                vector<Entity*> nodes;
                nodes.push_back(walker.entity);
                walker.entity->GetDescendants(&nodes);
                for (Entity* node : nodes)
                {
                    if (Render* render = node ? node->GetComponent<Render>() : nullptr)
                    {
                        if (render->GetMesh() == walker.mesh.get())
                        {
                            render->ClearMesh();
                        }
                    }
                }
            }
            walker.entity = nullptr;
            walker.animator = nullptr;
            walker.ragdoll = nullptr;
            walker.mesh.reset();
            walker.dead = false;
        }
        m_walkers.clear();
        m_source_mesh.reset();
    }

    void Pedestrians::Start()
    {
        // drop live walkers only, keep a world-load mesh preload so play does not hitch
        for (Walker& walker : m_walkers)
        {
            if (walker.animator)
            {
                walker.animator->Stop();
            }
            if (walker.entity)
            {
                World::RemoveEntity(walker.entity);
            }
            walker.entity = nullptr;
            walker.animator = nullptr;
            walker.ragdoll = nullptr;
            walker.mesh.reset();
            walker.dead = false;
        }
        m_walkers.clear();
        m_next_spawn_index = 0;
        m_lod_timer = 0.0f;
        m_physics_ready = false;
        BeginSpawn();
    }

    void Pedestrians::CancelPreload()
    {
        if (!m_preload_state)
        {
            return;
        }

        // never spin wait on the main thread, the shared state keeps the worker safe until it finishes
        m_preload_state->cancelled.store(true, memory_order_release);
        m_preload_state.reset();
    }

    void Pedestrians::Stop()
    {
        CancelPreload();

        for (Walker& walker : m_walkers)
        {
            if (walker.animator)
            {
                walker.animator->Stop();
            }
            if (walker.entity)
            {
                // drop raw mesh pointers before releasing the shared instance
                vector<Entity*> nodes;
                nodes.push_back(walker.entity);
                walker.entity->GetDescendants(&nodes);
                for (Entity* node : nodes)
                {
                    if (Render* render = node ? node->GetComponent<Render>() : nullptr)
                    {
                        if (render->GetMesh() == walker.mesh.get())
                        {
                            render->ClearMesh();
                        }
                    }
                }

                // deferred, world is still iterating Entity::Stop on the live entity list
                World::RemoveEntity(walker.entity);
            }
            walker.entity = nullptr;
            walker.animator = nullptr;
            walker.ragdoll = nullptr;
            walker.mesh.reset();
            walker.dead = false;
        }
        m_walkers.clear();
        m_source_mesh.reset();
        m_next_spawn_index = 0;
        m_lod_timer = 0.0f;
        m_spawn_ready = false;
        m_physics_ready = false;
    }

    void Pedestrians::Tick()
    {
        // finish editor preload so the mannequin template is hidden under this manager
        if (!m_spawn_ready)
        {
            FinishPreloadOnMainThread();
        }

        if (!Engine::IsFlagSet(EngineMode::Playing) || Engine::IsFlagSet(EngineMode::Paused))
        {
            return;
        }

        SpawnNext();
        if (m_walkers.empty())
        {
            return;
        }

        const float delta_time = clamp(static_cast<float>(Timer::GetDeltaTimeSec()), 0.0f, 0.1f);
        m_lod_timer += delta_time;
        if (m_lod_timer >= 0.15f)
        {
            m_lod_timer = 0.0f;
            UpdateAnimationLod();
        }

        for (Walker& walker : m_walkers)
        {
            if (!walker.entity)
            {
                continue;
            }

            if (walker.ragdoll && walker.ragdoll->IsDead())
            {
                walker.dead = true;
                walker.animating = false;
                continue;
            }

            if (walker.dead)
            {
                continue;
            }

            // near animated walkers get path casts, far ones just glide
            if (walker.animating)
            {
                UpdateWalker(walker, delta_time);
            }
            else
            {
                UpdateWalkerFar(walker, delta_time);
            }
        }
    }

    void Pedestrians::BeginPreload()
    {
        if (m_count == 0 || m_model_file.empty())
        {
            return;
        }

        // already warming or ready from world load
        if (m_preload_state && !m_preload_state->cancelled.load(memory_order_acquire))
        {
            return;
        }
        if (m_spawn_ready && m_source_mesh)
        {
            return;
        }

        uint32_t flags = Mesh::GetDefaultFlags();
        flags &= ~static_cast<uint32_t>(MeshFlags::PostProcessGenerateLods);
        flags &= ~static_cast<uint32_t>(MeshFlags::PostProcessNormalizeScale);
        flags &= ~static_cast<uint32_t>(MeshFlags::PostProcessOptimize);

        // sync claim if the mesh is already cached so it never sits as a top-level orphan
        if (shared_ptr<Mesh> existing = ResourceCache::GetByPath<Mesh>(m_model_file))
        {
            if (existing->GetRootEntity() && existing->GetSkeleton() && existing->GetAnimationClipCount() > 0)
            {
                m_source_mesh = existing;
                AdoptTemplateRoot();
                return;
            }
        }

        m_preload_state = std::make_shared<PreloadState>();
        const shared_ptr<PreloadState> state = m_preload_state;
        const string model_file = m_model_file;
        ThreadPool::AddTask([state, model_file, flags]()
        {
            bool succeeded = false;
            if (!state->cancelled.load(memory_order_acquire))
            {
                const shared_ptr<Mesh> mesh = ResourceCache::Load<Mesh>(model_file, flags);
                succeeded = mesh
                    && mesh->GetRootEntity()
                    && mesh->GetSkeleton()
                    && mesh->GetAnimationClipCount() > 0;
                // hide immediately so the hierarchy never shows a free-floating mannequin
                if (mesh && mesh->GetRootEntity())
                {
                    mesh->GetRootEntity()->SetTransient(true);
                    mesh->GetRootEntity()->SetActive(false);
                }
            }
            state->succeeded.store(succeeded, memory_order_release);
            state->completed.store(true, memory_order_release);
        });
    }

    void Pedestrians::BeginSpawn()
    {
        m_random_state = 0x6d2b79f5;
        m_next_spawn_index = 0;
        m_physics_ready = false;
        m_walkers.reserve(m_count);

        if (m_count == 0 || m_model_file.empty())
        {
            return;
        }

        // keep a finished preload, only kick work if the mesh is not ready yet
        if (!m_spawn_ready || !m_source_mesh)
        {
            m_spawn_ready = false;
            BeginPreload();
        }
    }

    bool Pedestrians::FinishPreloadOnMainThread()
    {
        if (m_spawn_ready && m_source_mesh)
        {
            return true;
        }

        if (!m_preload_state || !m_preload_state->completed.load(memory_order_acquire))
        {
            return false;
        }

        if (!m_preload_state->succeeded.load(memory_order_acquire))
        {
            SP_LOG_ERROR("Pedestrians failed to load model: %s", m_model_file.c_str());
            if (shared_ptr<Mesh> failed = ResourceCache::GetByPath<Mesh>(m_model_file))
            {
                if (Entity* root = failed->GetRootEntity())
                {
                    root->SetTransient(true);
                    root->SetActive(false);
                }
            }
            m_preload_state.reset();
            return false;
        }

        uint32_t flags = Mesh::GetDefaultFlags();
        flags &= ~static_cast<uint32_t>(MeshFlags::PostProcessGenerateLods);
        flags &= ~static_cast<uint32_t>(MeshFlags::PostProcessNormalizeScale);
        flags &= ~static_cast<uint32_t>(MeshFlags::PostProcessOptimize);

        // cache hit from the worker load, root setup stays on the main thread
        m_source_mesh = ResourceCache::Load<Mesh>(m_model_file, flags);
        m_preload_state.reset();
        if (!m_source_mesh || !m_source_mesh->GetRootEntity())
        {
            SP_LOG_ERROR("Pedestrians failed to load model: %s", m_model_file.c_str());
            return false;
        }
        if (!m_source_mesh->GetSkeleton() || m_source_mesh->GetAnimationClipCount() == 0)
        {
            SP_LOG_ERROR("Pedestrians model has no skeleton/clips: %s", m_model_file.c_str());
            Entity* root = m_source_mesh->GetRootEntity();
            root->SetTransient(true);
            root->SetActive(false);
            m_source_mesh.reset();
            return false;
        }

        AdoptTemplateRoot();
        return true;
    }

    void Pedestrians::AdoptTemplateRoot()
    {
        if (!m_source_mesh)
        {
            return;
        }

        Entity* template_root = m_source_mesh->GetRootEntity();
        if (!template_root)
        {
            return;
        }

        template_root->SetTransient(true);
        template_root->SetActive(false);
        template_root->SetParent(GetEntity());
        m_spawn_ready = true;
    }

    void Pedestrians::SpawnNext()
    {
        if (m_next_spawn_index >= m_count)
        {
            return;
        }

        if (!FinishPreloadOnMainThread())
        {
            return;
        }

        // wait until static geometry answers a probe, play-start can outrun physics
        if (!m_physics_ready)
        {
            Vector3 probe;
            const Vector3 city_center(
                (m_bounds_min.x + m_bounds_max.x) * 0.5f,
                m_bounds_max.y,
                (m_bounds_min.z + m_bounds_max.z) * 0.5f
            );
            if (!SampleGround(city_center, probe))
            {
                return;
            }
            m_physics_ready = true;
            SP_LOG_INFO("Pedestrians physics ready, spawning %u walkers", m_count);
        }

        // one walker per tick, crowd fills in after play is already interactive
        constexpr uint32_t max_spawns_per_tick = 1;
        uint32_t spawned = 0;
        while (m_next_spawn_index < m_count && spawned < max_spawns_per_tick)
        {
            if (!SpawnWalker(m_next_spawn_index))
            {
                break;
            }
            m_next_spawn_index++;
            spawned++;
        }
    }

    bool Pedestrians::SpawnWalker(uint32_t index)
    {
        if (!m_source_mesh || !m_source_mesh->GetRootEntity())
        {
            return false;
        }

        Vector3 position;
        Vector3 heading;
        FindSpawnPosition(index, position, heading);

        // skeleton joints are dropped, a walker only needs its root and the skinned mesh nodes,
        // for the mannequiny that is 2 entities instead of 48
        Entity* template_root = m_source_mesh->GetRootEntity();
        Entity* entity = template_root->CloneVisualOnly();
        if (!entity)
        {
            return false;
        }

        shared_ptr<Mesh> instance_mesh = m_source_mesh->CreateSkinnedInstance();
        if (!instance_mesh)
        {
            World::RemoveEntityImmediate(entity);
            return false;
        }

        instance_mesh->SetRootEntity(entity);

        vector<Entity*> nodes;
        nodes.push_back(entity);
        entity->GetDescendants(&nodes);
        for (Entity* node : nodes)
        {
            if (!node)
            {
                continue;
            }

            Render* render = node->GetComponent<Render>();
            if (!render || render->GetMesh() != m_source_mesh.get())
            {
                continue;
            }

            render->SetMesh(instance_mesh.get(), render->GetSubMeshIndex());
        }

        entity->SetObjectName("pedestrian_" + to_string(index + 1));
        entity->SetTransient(true);
        entity->SetActive(true);
        entity->SetScale(Vector3::One);
        entity->SetPosition(position);
        entity->SetRotation(Quaternion::FromLookRotation(
            Vector3(-heading.x, 0.0f, -heading.z),
            Vector3::Up
        ));

        Animator* animator = entity->AddComponent<Animator>();
        if (!animator)
        {
            World::RemoveEntityImmediate(entity);
            return false;
        }

        animator->SetBoneEntitiesEnabled(false);
        animator->SetSpeed(0.9f + NextFloat() * 0.3f);
        animator->SetBlendDuration(0.15f);
        animator->SetFootIkEnabled(false);
        animator->SetLoop(true);
        animator->Play("walk");

        Ragdoll* ragdoll = entity->AddComponent<Ragdoll>();
        if (!ragdoll)
        {
            World::RemoveEntityImmediate(entity);
            return false;
        }

        Walker walker;
        walker.entity = entity;
        walker.animator = animator;
        walker.ragdoll = ragdoll;
        walker.mesh = instance_mesh;
        walker.heading = heading;
        walker.speed = m_walk_speed * (0.85f + NextFloat() * 0.3f);
        walker.ground_y = position.y;
        walker.height_offset = animator->GetFootIkGroundOffset();
        walker.turn_timer = 2.0f + NextFloat() * 6.0f;
        walker.animating = true;
        walker.dead = false;
        m_walkers.push_back(move(walker));
        return true;
    }

    bool Pedestrians::FindSpawnPosition(uint32_t index, Vector3& position, Vector3& heading)
    {
        const float city_x = 934.5f;
        const float city_z = -313.9f;
        const float road_step = 120.0f;

        // deterministic slot on the city road grid, always succeeds
        const bool east_west = (index % 2) == 0;
        const int road = static_cast<int>((index / 2) % 9);
        const float along = -400.0f + static_cast<float>((index * 47) % 801);

        float x = city_x;
        float z = city_z;
        if (east_west)
        {
            x = city_x + along;
            z = city_z + (-480.0f + static_cast<float>(road) * road_step);
            heading = planar_normalize(Vector3((index % 4) < 2 ? 1.0f : -1.0f, 0.0f, 0.0f));
        }
        else
        {
            x = city_x + (-480.0f + static_cast<float>(road) * road_step);
            z = city_z + along;
            heading = planar_normalize(Vector3(0.0f, 0.0f, (index % 4) < 2 ? 1.0f : -1.0f));
        }

        Vector3 ground;
        if (!SampleGround(Vector3(x, m_bounds_max.y, z), ground))
        {
            ground = Vector3(x, 0.0f, z);
        }

        if (!IsPathClear(ground, heading, spawn_clearance))
        {
            heading = planar_normalize(Vector3(-heading.x, 0.0f, -heading.z));
        }

        position = ground;
        return true;
    }

    bool Pedestrians::SampleGround(const Vector3& position, Vector3& ground_position) const
    {
        const float height = std::max(position.y, m_bounds_max.y) + 10.0f;
        const Vector3 origin(position.x, height, position.z);
        const float distance = height - m_bounds_min.y + 10.0f;
        if (!PhysicsWorld::RaycastStatic(origin, Vector3::Down, distance, ground_position))
        {
            return false;
        }

        return true;
    }

    bool Pedestrians::IsPathClear(
        const Vector3& position,
        const Vector3& direction,
        float distance
    ) const
    {
        const Vector3 dir = planar_normalize(direction);
        if (distance <= 0.001f)
        {
            return true;
        }

        Vector3 hit_position;
        float hit_distance = 0.0f;
        Entity* hit_entity = nullptr;
        if (PhysicsWorld::SphereCast(
            position + Vector3::Up * sensor_height,
            dir,
            sensor_radius,
            distance,
            0,
            hit_position,
            hit_distance,
            hit_entity))
        {
            return false;
        }

        return true;
    }

    bool Pedestrians::IsInsideBounds(const Vector3& position, float margin) const
    {
        return position.x >= m_bounds_min.x + margin &&
               position.x <= m_bounds_max.x - margin &&
               position.z >= m_bounds_min.z + margin &&
               position.z <= m_bounds_max.z - margin;
    }

    void Pedestrians::UpdateWalker(Walker& walker, float delta_time)
    {
        Entity* entity = walker.entity;
        Vector3 position = entity->GetPosition();

        walker.turn_timer -= delta_time;
        if (walker.turn_timer <= 0.0f)
        {
            const float yaw = (NextFloat() - 0.5f) * 1.2f;
            const float cos_y = cosf(yaw);
            const float sin_y = sinf(yaw);
            walker.heading = planar_normalize(Vector3(
                walker.heading.x * cos_y - walker.heading.z * sin_y,
                0.0f,
                walker.heading.x * sin_y + walker.heading.z * cos_y
            ));
            walker.turn_timer = 3.0f + NextFloat() * 7.0f;
        }

        if (!IsInsideBounds(position, 4.0f))
        {
            const Vector3 center = (m_bounds_min + m_bounds_max) * 0.5f;
            walker.heading = planar_normalize(Vector3(center.x - position.x, 0.0f, center.z - position.z));
            walker.turn_timer = 2.0f + NextFloat() * 2.0f;
        }

        bool blocked = !IsPathClear(position, walker.heading, look_ahead);
        if (blocked)
        {
            walker.blocked_timer += delta_time;

            // try left then right, fall back to random
            const Vector3 left(-walker.heading.z, 0.0f, walker.heading.x);
            const Vector3 right(walker.heading.z, 0.0f, -walker.heading.x);
            if (IsPathClear(position, left, look_ahead))
            {
                walker.heading = left;
            }
            else if (IsPathClear(position, right, look_ahead))
            {
                walker.heading = right;
            }
            else
            {
                const float yaw = 1.2f + NextFloat() * 2.0f;
                const float sign = (NextUInt() & 1u) ? 1.0f : -1.0f;
                const float cos_y = cosf(yaw * sign);
                const float sin_y = sinf(yaw * sign);
                walker.heading = planar_normalize(Vector3(
                    walker.heading.x * cos_y - walker.heading.z * sin_y,
                    0.0f,
                    walker.heading.x * sin_y + walker.heading.z * cos_y
                ));
            }
            walker.turn_timer = 1.0f + NextFloat() * 2.0f;
        }
        else
        {
            walker.blocked_timer = 0.0f;
        }

        // only step forward when the short path is free
        if (IsPathClear(position, walker.heading, std::max(look_ahead * 0.45f, 0.6f)))
        {
            position.x += walker.heading.x * walker.speed * delta_time;
            position.z += walker.heading.z * walker.speed * delta_time;
        }

        Vector3 ground;
        if (SampleGround(position, ground))
        {
            walker.ground_y = ground.y;
        }
        position.y = walker.ground_y + walker.height_offset;

        const Quaternion target_rot = Quaternion::FromLookRotation(
            Vector3(-walker.heading.x, 0.0f, -walker.heading.z),
            Vector3::Up
        );
        const float t = 1.0f - expf(-turn_speed * delta_time);
        entity->SetRotation(Quaternion::Lerp(entity->GetRotation(), target_rot, t));
        entity->SetPosition(position);
    }

    void Pedestrians::UpdateWalkerFar(Walker& walker, float delta_time)
    {
        Entity* entity = walker.entity;
        Vector3 position = entity->GetPosition();

        walker.turn_timer -= delta_time;
        if (walker.turn_timer <= 0.0f)
        {
            const float yaw = (NextFloat() - 0.5f) * 0.8f;
            const float cos_y = cosf(yaw);
            const float sin_y = sinf(yaw);
            walker.heading = planar_normalize(Vector3(
                walker.heading.x * cos_y - walker.heading.z * sin_y,
                0.0f,
                walker.heading.x * sin_y + walker.heading.z * cos_y
            ));
            walker.turn_timer = 4.0f + NextFloat() * 8.0f;
        }

        if (!IsInsideBounds(position, 4.0f))
        {
            const Vector3 center = (m_bounds_min + m_bounds_max) * 0.5f;
            walker.heading = planar_normalize(Vector3(center.x - position.x, 0.0f, center.z - position.z));
            walker.turn_timer = 2.0f + NextFloat() * 2.0f;
        }

        position.x += walker.heading.x * walker.speed * delta_time;
        position.z += walker.heading.z * walker.speed * delta_time;

        // ground less often, stagger by object id so they do not all raycast one frame
        walker.ground_sample_timer -= delta_time;
        if (walker.ground_sample_timer <= 0.0f)
        {
            walker.ground_sample_timer = 0.35f + static_cast<float>(entity->GetObjectId() % 17u) * 0.02f;
            Vector3 ground;
            if (SampleGround(position, ground))
            {
                walker.ground_y = ground.y;
            }
        }
        position.y = walker.ground_y + walker.height_offset;

        const Quaternion target_rot = Quaternion::FromLookRotation(
            Vector3(-walker.heading.x, 0.0f, -walker.heading.z),
            Vector3::Up
        );
        const float t = 1.0f - expf(-turn_speed * delta_time);
        entity->SetRotation(Quaternion::Lerp(entity->GetRotation(), target_rot, t));
        entity->SetPosition(position);
    }

    void Pedestrians::UpdateAnimationLod()
    {
        Camera* camera = World::GetCamera();
        if (!camera || !camera->GetEntity())
        {
            for (Walker& walker : m_walkers)
            {
                if (walker.dead)
                {
                    continue;
                }

                if (walker.animator && !walker.animating)
                {
                    walker.animator->Resume();
                    walker.animating = true;
                }
                if (walker.ragdoll)
                {
                    walker.ragdoll->SetHitBodyEnabled(true);
                }
            }
            return;
        }

        const Vector3 camera_pos = camera->GetEntity()->GetPosition();
        vector<pair<float, Walker*>> candidates;
        candidates.reserve(m_walkers.size());

        for (Walker& walker : m_walkers)
        {
            if (!walker.entity || !walker.animator || walker.dead)
            {
                continue;
            }

            Vector3 offset = walker.entity->GetPosition() - camera_pos;
            offset.y = 0.0f;
            const float distance_squared = offset.LengthSquared();
            if (distance_squared <= m_animation_radius * m_animation_radius)
            {
                candidates.emplace_back(distance_squared, &walker);
            }
            else
            {
                if (walker.animating)
                {
                    walker.animator->Pause();
                    walker.animating = false;
                }
                if (walker.ragdoll)
                {
                    walker.ragdoll->SetHitBodyEnabled(false);
                }
            }
        }

        sort(
            candidates.begin(),
            candidates.end(),
            [](const auto& a, const auto& b)
            {
                return a.first < b.first;
            }
        );

        if (candidates.size() > m_max_animated)
        {
            for (size_t i = m_max_animated; i < candidates.size(); ++i)
            {
                Walker* walker = candidates[i].second;
                if (walker->animating)
                {
                    walker->animator->Pause();
                    walker->animating = false;
                }
                if (walker->ragdoll)
                {
                    walker->ragdoll->SetHitBodyEnabled(false);
                }
            }
            candidates.resize(m_max_animated);
        }

        for (auto& candidate : candidates)
        {
            Walker* walker = candidate.second;
            if (!walker->animating)
            {
                walker->animator->Resume();
                walker->animating = true;
            }
            if (walker->ragdoll)
            {
                walker->ragdoll->SetHitBodyEnabled(true);
            }
        }
    }

    float Pedestrians::NextFloat()
    {
        return static_cast<float>(NextUInt() & 0x00ffffffu) / static_cast<float>(0x01000000u);
    }

    uint32_t Pedestrians::NextUInt()
    {
        uint32_t state = m_random_state;
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        m_random_state = state;
        return state;
    }

    void Pedestrians::Save(pugi::xml_node& node)
    {
        node.append_attribute("count") = m_count;
        node.append_attribute("model_file") = m_model_file.c_str();
        node.append_attribute("bounds_min_x") = m_bounds_min.x;
        node.append_attribute("bounds_min_y") = m_bounds_min.y;
        node.append_attribute("bounds_min_z") = m_bounds_min.z;
        node.append_attribute("bounds_max_x") = m_bounds_max.x;
        node.append_attribute("bounds_max_y") = m_bounds_max.y;
        node.append_attribute("bounds_max_z") = m_bounds_max.z;
        node.append_attribute("max_animated") = m_max_animated;
        node.append_attribute("animation_radius") = m_animation_radius;
        node.append_attribute("walk_speed") = m_walk_speed;
    }

    void Pedestrians::Load(pugi::xml_node& node)
    {
        m_count = node.attribute("count").as_uint(m_count);
        m_model_file = node.attribute("model_file").as_string(m_model_file.c_str());
        m_bounds_min.x = node.attribute("bounds_min_x").as_float(m_bounds_min.x);
        m_bounds_min.y = node.attribute("bounds_min_y").as_float(m_bounds_min.y);
        m_bounds_min.z = node.attribute("bounds_min_z").as_float(m_bounds_min.z);
        m_bounds_max.x = node.attribute("bounds_max_x").as_float(m_bounds_max.x);
        m_bounds_max.y = node.attribute("bounds_max_y").as_float(m_bounds_max.y);
        m_bounds_max.z = node.attribute("bounds_max_z").as_float(m_bounds_max.z);
        m_max_animated = clamp(node.attribute("max_animated").as_uint(m_max_animated), 1u, 128u);
        m_animation_radius = clamp(node.attribute("animation_radius").as_float(m_animation_radius), 20.0f, 500.0f);
        m_walk_speed = clamp(node.attribute("walk_speed").as_float(m_walk_speed), 0.5f, 8.0f);
        m_count = min(m_count, 256u);

        // warm the mannequin on a worker while the editor is still open
        BeginPreload();
        FinishPreloadOnMainThread();
    }
}
