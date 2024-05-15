/*
Copyright(c) 2016-2024 Panos Karabelas

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
#include "RHI_Definitions.h"
//================================

namespace Spartan
{
    class SP_CLASS RHI_Fence : public SpartanObject
    {
    public:
        RHI_Fence(const char* name = nullptr);
        ~RHI_Fence();

        // Returns true if the false was signaled.
        bool IsSignaled();

        // Returns true when the fence is signaled and false in case of a timeout.
        bool Wait(uint64_t timeout_nanoseconds = 1000000000 /* one second */);

        // Resets the fence
        void Reset();

        void* GetRhiResource() { return m_rhi_resource; }

        // State
        RHI_Sync_State GetStateCpu()                 const { return n_state_cpu; }
        void SetStateCpu(const RHI_Sync_State state)       { n_state_cpu = state; }

    private:
        void* m_rhi_resource           = nullptr;
        RHI_Sync_State n_state_cpu = RHI_Sync_State::Idle;
    };
}
