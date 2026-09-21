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
#include "ProgressTracker.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <map>
#include <mutex>

using namespace std;

namespace spartan
{
    namespace
    {
        struct Registry
        {
            mutex access;
            map<uint64_t, ProgressTaskState*> tasks;
            array<atomic<uint32_t>, static_cast<size_t>(ProgressType::Max)> by_type{};
            atomic<uint32_t> active_count{0};
            uint64_t next_id = 1;
        };

        const shared_ptr<Registry>& registry()
        {
            // Tasks keep the registry alive even during static shutdown.
            static const auto instance = make_shared<Registry>();
            return instance;
        }
    }

    struct ProgressTaskState
    {
        shared_ptr<Registry> owner;
        ProgressSnapshot info;
        chrono::steady_clock::time_point started = chrono::steady_clock::now();
        bool blocks_loading = true;
        bool active = false; // Protected by owner->access, like all display data.

        void Finish()
        {
            if (!owner) return;
            lock_guard lock(owner->access);
            if (!active) return;
            active = false;
            owner->tasks.erase(info.id);
            if (blocks_loading)
            {
                owner->by_type[static_cast<size_t>(info.type)].fetch_sub(1, memory_order_release);
                owner->active_count.fetch_sub(1, memory_order_release);
            }
        }
        ~ProgressTaskState() { Finish(); }
    };

    ProgressTask ProgressTracker::Begin(ProgressType type, const string& title, const string& step, ProgressMode mode)
    {
        if (static_cast<size_t>(type) >= static_cast<size_t>(ProgressType::Max)) return {};
        auto state = make_shared<ProgressTaskState>();
        state->blocks_loading = mode == ProgressMode::Loading;
        state->owner = registry();
        state->info.type = type;
        state->info.title = title;
        state->info.step = step;
        {
            lock_guard lock(state->owner->access);
            state->info.id = state->owner->next_id++;
            state->owner->tasks.emplace(state->info.id, state.get());
            state->active = true;
            if (state->blocks_loading)
            {
                state->owner->by_type[static_cast<size_t>(type)].fetch_add(1, memory_order_release);
                state->owner->active_count.fetch_add(1, memory_order_release);
            }
        }
        return ProgressTask(move(state));
    }

    void ProgressTask::SetStep(const string& step, const string& detail) const
    {
        if (!m_state) return;
        lock_guard lock(m_state->owner->access);
        if (!m_state->active) return;
        m_state->info.step = step;
        m_state->info.detail = detail;
        m_state->info.fraction = -1.0f;
        ++m_state->info.step_id;
    }

    void ProgressTask::SetDetail(const string& detail) const
    {
        if (!m_state) return;
        lock_guard lock(m_state->owner->access);
        if (m_state->active) m_state->info.detail = detail;
    }

    void ProgressTask::SetFraction(float fraction) const
    {
        if (!m_state || !isfinite(fraction)) return;
        lock_guard lock(m_state->owner->access);
        if (!m_state->active) return;
        // Parallel callbacks can arrive out of order. Only a new step resets progress.
        m_state->info.fraction = max(m_state->info.fraction, clamp(fraction, 0.0f, 1.0f));
    }

    void ProgressTask::Finish() const
    {
        if (m_state) m_state->Finish();
    }

    bool ProgressTracker::IsLoading()
    {
        return registry()->active_count.load(memory_order_acquire) != 0;
    }

    bool ProgressTracker::IsLoading(ProgressType type)
    {
        return static_cast<size_t>(type) < static_cast<size_t>(ProgressType::Max) && registry()->by_type[static_cast<size_t>(type)].load(memory_order_acquire) != 0;
    }

    ProgressDisplay ProgressTracker::GetDisplay()
    {
        const auto& owner = registry();
        lock_guard lock(owner->access);
        ProgressDisplay display;
        display.active_count = static_cast<uint32_t>(owner->tasks.size());
        // A world/download remains the primary task; active model work takes the
        // supporting row ahead of terrain/texture work. Oldest wins within a type,
        // so rapidly reporting workers cannot flicker or steal the display.
        const auto now = chrono::steady_clock::now();
        for (uint32_t type = 0; type < static_cast<uint32_t>(ProgressType::Max); ++type)
        {
            for (const auto& [id, state] : owner->tasks)
            {
                if (static_cast<uint32_t>(state->info.type) != type) continue;
                auto& row = display.tasks[display.count++];
                row = state->info;
                row.elapsed_seconds = chrono::duration<double>(now - state->started).count();
                if (display.count == display.tasks.size()) return display;
            }
        }
        return display;
    }
}
