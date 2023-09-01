/*
Copyright(c) 2016-2023 Panos Karabelas

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

//= INCLUDES =================
#include "../Core/SP_Object.h"
#include "RHI_Definitions.h"
//============================

namespace Spartan
{
    class RHI_Semaphore : public SP_Object
    {
    public:
        RHI_Semaphore(bool is_timeline = false, const char* name = nullptr);
        ~RHI_Semaphore();

        // Timeline
        bool IsTimelineSemaphore() const { return m_is_timeline; }
        void Wait(const uint64_t value, const uint64_t timeout = std::numeric_limits<uint64_t>::max());
        void Signal(const uint64_t value);
        uint64_t GetValue();
        void* GetRhiResource() { return m_rhi_resource; }

        // State
        RHI_Sync_State GetStateCpu()                 const { return m_state_cpu; }
        void SetStateCpu(const RHI_Sync_State state)       { m_state_cpu = state; }

    private:
        void* m_rhi_resource           = nullptr;
        bool m_is_timeline         = false;
        RHI_Sync_State m_state_cpu = RHI_Sync_State::Idle;
    };
}
