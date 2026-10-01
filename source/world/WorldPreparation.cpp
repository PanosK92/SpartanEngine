/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

#include "pch.h"
#include "WorldPreparation.h"
#include "World.h"
#include "Entity.h"
#include "components/Render.h"
#include "../core/ProgressTracker.h"
using namespace std;
namespace spartan
{
    bool WorldPreparation::Tick(const vector<Entity*>& entities, const ProgressTask& progress,
                                bool (*prepare)(const vector<Entity*>&, const ProgressTask&), void (*commit_removals)())
    {
        if (stage == Stage::Idle) return false;
        if (stage == Stage::Ready) return true;
        if (stage == Stage::Preparing)
        {
            if (prepare && !prepare(entities, progress)) return false;
            stage = Stage::Components;
            commit_removals();
        }
        // Complete model preloads and bake navigation before editor entry. Live agents
        // still spawn in play mode; a failed asset must not wedge the loading screen.
        const Stopwatch slice;
        while (cursor < entities.size())
        {
            Entity* entity = entities[cursor];
            progress.SetDetail("Preparing scene: " + entity->GetObjectName());
            if (entity->GetActive())
            {
                for (const auto& component : entity->GetAllComponents())
                    if (component) component->PrepareGeometry();
                if (Render* render = entity->GetComponent<Render>()) render->Tick();
                for (const auto& component : entity->GetAllComponents())
                    if (component && !component->PrepareWorld()) return false;
            }
            ++cursor;
            if (slice.GetElapsedTimeMs() >= 20.0f) return false;
        }
        World::ProcessPendingAdditions();
        if (cursor < entities.size()) return false;
        stage = Stage::Ready;
        return true;
    }
}
