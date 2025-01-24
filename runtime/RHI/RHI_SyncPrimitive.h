/*
Copyright(c) 2016-2025 Panos Karabelas

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
#include "../Core/SpartanObject.h"
//================================

namespace spartan
{
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

        // sync
        void Wait(const uint64_t value = 0, const uint64_t timeout_nanoseconds = 5000000000 /* 5 seconds */);
        void Signal(const uint64_t value);

        // value
        uint64_t GetValue();
        uint64_t GetWaitValue() const           { return m_value_wait; }
        void SetWaitValue(const uint64_t value) { m_value_wait = value; }

        // signaling
        bool IsSignaled();
        void Reset();

        // rhi
        void* GetRhiResource() { return m_rhi_resource; }

    private:
        RHI_SyncPrimitive_Type m_type = RHI_SyncPrimitive_Type::Max;
        uint64_t m_value_wait         = 0;

        // rhi
        void* m_rhi_resource = nullptr;
    };
}
