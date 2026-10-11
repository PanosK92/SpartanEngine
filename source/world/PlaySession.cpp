/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

#include "pch.h"
#include "PlaySession.h"
#include "World.h"
#include "Entity.h"
#include "components/Terrain.h"
#include "components/Animator.h"
#include "../io/pugixml.hpp"
using namespace std;
namespace spartan
{
    void PlaySession::Begin(const vector<Entity*>& entities, const WorldCallbacks& callbacks)
    {
        if (IsActive()) return;
        snapshot = make_shared<PlayState>();
        snapshot->document = make_shared<pugi::xml_document>();
        snapshot->environment = Environment::GetSettings();
        auto world = snapshot->document->append_child("World");
        world.append_child("Entities");
        if (callbacks.save) callbacks.save(world);
        capture_roots.clear();
        poses.clear();
        poses.reserve(entities.size());
        start_queue = entities;
        for (Entity* entity : entities)
        {
            if (!entity->IsTransient()) poses[entity->GetObjectId()] = { entity->GetPositionLocal(), entity->GetRotationLocal(), entity->GetScaleLocal() };
            if (!entity->IsTransient() && !entity->GetParent()) capture_roots.push_back(entity);
            if (auto terrain = entity->GetComponent<Terrain>()) snapshot->sculpt[entity->GetObjectId()] = terrain->GetSculptSnapshot();
        }
        stable_partition(start_queue.begin(), start_queue.end(), [](Entity* entity)
        {
            for (const auto& component : entity->GetAllComponents())
                if (component && component->StartsEarly()) return true;
            return false;
        });
        capture_cursor = start_cursor = 0;
        phase = Phase::Capturing;
    }

    void PlaySession::Stop(const WorldCallbacks& callbacks)
    {
        if (!IsActive() || phase == Phase::Stopping) return;
        // No Start callback ran during capture, so an incomplete snapshot is never restored.
        if (phase == Phase::Capturing)
        {
            Environment::SetSettings(snapshot->environment);
            Reset();
            return;
        }
        phase = Phase::Stopping;
        if (callbacks.stop_play) callbacks.stop_play();
        if (StopFast())
        {
            Environment::SetSettings(snapshot->environment);
            Reset();
            return;
        }
        SP_LOG_INFO("play stop: authored entities were deleted or terrain was sculpted, reloading the edit scene");
        auto authored = std::move(snapshot); // ClearScene resets this PlaySession.
        World::RestorePlayState(authored);
    }

    // transforms, component Stop and play-spawned entities cover what play changes, a full
    // reload is only needed when authored entities are gone or the terrain sculpt changed
    bool PlaySession::StopFast()
    {
        const vector<Entity*> entities = World::GetEntities();
        unordered_set<uint64_t> live;
        live.reserve(entities.size());
        for (Entity* entity : entities)
        {
            if (entity) live.insert(entity->GetObjectId());
        }
        for (const auto& [id, pose] : poses)
        {
            if (!live.contains(id)) return false;
        }
        for (const auto& [id, sculpt] : snapshot->sculpt)
        {
            Entity* entity = World::GetEntityById(id);
            Terrain* terrain = entity ? entity->GetComponent<Terrain>() : nullptr;
            if (!terrain || terrain->GetSculptSnapshot() != sculpt) return false;
        }

        for (Entity* entity : entities)
        {
            if (entity) entity->Stop();
        }

        unordered_set<Entity*> spawned;
        for (Entity* entity : World::GetEntities())
        {
            if (auto it = poses.find(entity->GetObjectId()); it != poses.end())
            {
                entity->SetPositionLocal(it->second.position);
                entity->SetRotationLocal(it->second.rotation);
                entity->SetScaleLocal(it->second.scale);
            }
            // transient entities are owned by their component, prefab entities are rebuilt by their owner
            else if (!entity->IsTransient() && !entity->HasPrefabData() && !entity->IsPrefabOwned())
            {
                spawned.insert(entity);
            }
        }

        // the pose restore puts bones back at mid-play values, re-bind so skinned meshes leave play at rest
        for (Entity* entity : World::GetEntities())
        {
            if (Animator* animator = entity->GetComponent<Animator>()) animator->ApplyBindPose();
        }

        // removal takes the descendants too, so only the topmost spawned entity is removed
        for (Entity* entity : spawned)
        {
            if (!spawned.contains(entity->GetParent())) World::RemoveEntity(entity);
        }
        SP_LOG_INFO("play stop: restored %zu poses, removed %zu play spawned entities", poses.size(), spawned.size());
        return true;
    }

    void PlaySession::Tick()
    {
        if (!IsStarting()) return;
        const Stopwatch timer;
        if (phase == Phase::Capturing)
        {
            auto entities = snapshot->document->child("World").child("Entities");
            while (capture_cursor < capture_roots.size())
            {
                if (Entity* entity = capture_roots[capture_cursor++])
                {
                    auto node = entities.append_child("Entity");
                    entity->Save(node);
                }
                if (timer.GetElapsedTimeMs() >= start_budget_ms) return;
            }
            capture_roots.clear();
            phase = Phase::Starting;
        }
        while (start_cursor < start_queue.size())
        {
            if (Entity* entity = start_queue[start_cursor++]) entity->Start();
            if (phase != Phase::Starting) return;
            if (timer.GetElapsedTimeMs() >= start_budget_ms) return;
        }
        start_queue.clear();
        phase = Phase::Ready;
        SP_LOG_INFO("play boot complete, %zu entities started", World::GetEntities().size());
    }

    void PlaySession::Reset()
    {
        snapshot.reset();
        poses.clear();
        capture_roots.clear();
        start_queue.clear();
        capture_cursor = start_cursor = 0;
        phase = Phase::Idle;
    }

    void PlaySession::CancelStarts(const set<uint64_t>& ids)
    {
        for (auto* list : {&start_queue, &capture_roots})
            for (Entity*& entity : *list)
                if (entity && ids.contains(entity->GetObjectId())) entity = nullptr;
    }
}
