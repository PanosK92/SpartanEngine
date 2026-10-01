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
        start_queue = entities;
        for (Entity* entity : entities)
        {
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
        auto authored = std::move(snapshot); // ClearScene resets this PlaySession.
        if (callbacks.stop_play) callbacks.stop_play();
        World::RestorePlayState(authored);
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
