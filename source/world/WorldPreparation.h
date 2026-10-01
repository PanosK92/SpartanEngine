/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

#pragma once
#include "../core/Stopwatch.h"
#include <vector>
namespace spartan
{
    class Entity;
    class ProgressTask;
    class WorldPreparation
    {
    public:
        void Reset() { cursor = 0; stage = Stage::Idle; }
        void Begin() { timer.Start(); cursor = 0; stage = Stage::Preparing; }
        bool Tick(const std::vector<Entity*>& entities, const ProgressTask& progress,
                  bool (*prepare)(const std::vector<Entity*>&, const ProgressTask&), void (*commit_removals)());
        float ElapsedMs() const { return timer.GetElapsedTimeMs(); }
    private:
        enum class Stage { Idle, Preparing, Components, Ready };
        Stage stage = Stage::Idle;
        size_t cursor = 0;
        Stopwatch timer;
    };
}
