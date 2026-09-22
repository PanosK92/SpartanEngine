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

#pragma once

//= INCLUDES =====================
#include "../core/SpartanObject.h"
#include <atomic>
#include <memory>
#include <vector>
//================================

namespace spartan
{
    class RHI_SyncPrimitive;

    struct RHI_Work
    {
        std::shared_ptr<RHI_SyncPrimitive> timeline;
        uint64_t value = 0;

        bool IsComplete() const;
    };

    enum class RHI_SyncPrimitive_Type
    {
        Fence,
        Semaphore,
        SemaphoreTimeline,
        Max
    };

    class RHI_SyncPrimitive : public SpartanObject
    {
    public:
        RHI_SyncPrimitive(const RHI_SyncPrimitive_Type type, const char* name = nullptr);
        ~RHI_SyncPrimitive();

        void Wait(const uint64_t timeout_nanoseconds);
        void Wait(const uint64_t timeout_nanoseconds, const uint64_t value);
        void Signal(const uint64_t value);
        bool IsSignaled();
        bool IsSignaled(uint64_t value);
        void Reset();
        uint64_t GetNextSignalValue() { return m_value.fetch_add(1, std::memory_order_relaxed) + 1; }
        uint64_t GetValue() const     { return m_value.load(std::memory_order_relaxed); }
        void* GetRhiResource()        { return m_rhi_resource; }

        // Capture the submission consuming this binary semaphore. Command lists
        // can be recycled before the semaphore is reused, so their current work
        // is not evidence of whether this particular wait has completed.
        void SetConsumer(const RHI_Work& work) { m_consumer = work; }
        const RHI_Work& GetConsumer() const   { return m_consumer; }

    private:
        RHI_Work m_consumer;
        RHI_SyncPrimitive_Type m_type    = RHI_SyncPrimitive_Type::Max;
        std::atomic<uint64_t> m_value    = 0;
        void* m_rhi_resource             = nullptr;
    };
    inline bool RHI_Work::IsComplete() const
    {
        return !timeline || timeline->IsSignaled(value);
    }

    struct RHI_PendingWork
    {
        std::vector<RHI_Work> submissions;
        bool IsComplete() const
        {
            if (m_complete.load(std::memory_order_relaxed)) return true;
            for (const RHI_Work& work : submissions)
                if (!work.IsComplete()) return false;
            m_complete.store(true, std::memory_order_relaxed);
            return true;
        }
    private:
        mutable std::atomic<bool> m_complete = false;
    };

}
