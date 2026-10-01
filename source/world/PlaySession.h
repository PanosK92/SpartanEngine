/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

#pragma once
#include "Environment.h"
#include <memory>
#include <vector>
#include <set>
#include <unordered_map>
#include <cstdint>
namespace pugi { class xml_document; }
namespace spartan
{
    class Entity;
    class TerrainSculptLayer;
    struct WorldCallbacks;
    // Authored scene only. External asset files edited during play are not rolled back.
    struct PlayState
    {
        std::shared_ptr<pugi::xml_document> document;
        std::unordered_map<uint64_t, std::shared_ptr<const TerrainSculptLayer>> sculpt;
        EnvironmentSettings environment;
    };
    class PlaySession
    {
    public:
        void Begin(const std::vector<Entity*>& entities, const WorldCallbacks& callbacks);
        void Stop(const WorldCallbacks& callbacks);
        void Tick();
        void Reset();
        void CancelStarts(const std::set<uint64_t>& ids);
        bool IsStarting() const { return phase == Phase::Capturing || phase == Phase::Starting; }
        bool IsActive() const { return phase != Phase::Idle; }
    private:
        enum class Phase : uint8_t { Idle, Capturing, Starting, Ready, Stopping };
        std::shared_ptr<PlayState> snapshot;
        Phase phase = Phase::Idle;
        std::vector<Entity*> capture_roots;
        std::vector<Entity*> start_queue;
        size_t capture_cursor = 0, start_cursor = 0;
        static constexpr double start_budget_ms = 4.0;
    };
}
