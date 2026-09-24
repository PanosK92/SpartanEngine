/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

#pragma once

#include <array>
#include <cstdint>
#include <cstddef>

namespace spartan
{
    // Counts describe one World::Tick, independently of the profiler's sampled timeline.
    #define SP_WORLD_WORK_COUNTERS(X) \
        X(entities_total) X(pretick_candidates) X(pretick_sleeping) X(pretick_entities) X(render_entities) X(logic_entities) \
        X(physics_pretick_calls) X(physics_static_clean_skips) X(physics_creation_far_skips) \
        X(physics_created) X(physics_instances_rebuilt) X(physics_dynamic_syncs) \
        X(physics_vehicle_updates) X(physics_controller_updates) X(physics_cloth_updates) X(physics_editor_static_syncs) \
        X(physics_activation_calls) X(physics_activation_empty_skips) X(physics_activation_box_skips) \
        X(physics_activation_cached_skips) X(physics_actors_tested) X(physics_actors_activated) X(physics_actors_deactivated) \
        X(render_tick_calls) X(bounds_checks) X(bounds_rebuilt) X(bounds_instances_rebuilt) \
        X(cull_calls) X(cull_cached_skips) X(cull_tests) X(render_visible) X(visibility_changes) X(lod_updates) \
        X(animation_jobs) X(animation_vertices)

    enum class WorldWork : size_t
    {
        #define X(name) name,
        SP_WORLD_WORK_COUNTERS(X)
        #undef X
        Count
    };

    inline constexpr std::array world_work_names = {
        #define X(name) #name,
        SP_WORLD_WORK_COUNTERS(X)
        #undef X
    };
    #undef SP_WORLD_WORK_COUNTERS

    struct alignas(64) WorldWorkCounters
    {
        std::array<uint64_t, static_cast<size_t>(WorldWork::Count)> values{};
        void Merge(const WorldWorkCounters& other)
        {
            for (size_t i = 0; i < values.size(); ++i) values[i] += other.values[i];
        }
    };

    // Each dispatched batch owns its counters. Joining before merging avoids atomics,
    // locks and shared cache-line writes in the per-object loops.
    inline thread_local WorldWorkCounters* current_world_work = nullptr;
    class ScopedWorldWork
    {
    public:
        explicit ScopedWorldWork(WorldWorkCounters& counters) : previous(current_world_work) { current_world_work = &counters; }
        ~ScopedWorldWork() { current_world_work = previous; }
        ScopedWorldWork(const ScopedWorldWork&) = delete;
        ScopedWorldWork& operator=(const ScopedWorldWork&) = delete;
    private:
        WorldWorkCounters* previous;
    };

    inline void CountWorldWork(WorldWork counter, uint64_t count = 1)
    {
        if (current_world_work) current_world_work->values[static_cast<size_t>(counter)] += count;
    }
}
