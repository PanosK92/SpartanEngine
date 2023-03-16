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

//= INCLUDES ==============
#include "../Core/Object.h"
#include "RHI_Definition.h"
//=========================

namespace Spartan
{
    class SP_CLASS RHI_Fence : public Object
    {
    public:
        RHI_Fence() = default;
        RHI_Fence(const char* name = nullptr);
        ~RHI_Fence();

        // Returns true if the false was signaled.
        bool IsSignaled();

        // Returns true when the fence is signaled and false in case of a timeout.
        bool Wait(uint64_t timeout_nanoseconds = 1000000000 /* one second */);

        // Resets the fence
        void Reset();

        void* GetResource() { return m_resource; }

        // Cpu state
        RHI_Sync_State GetCpuState()                 const { return m_cpu_state; }
        void SetCpuState(const RHI_Sync_State state)       { m_cpu_state = state; }

    private:
        void* m_resource = nullptr;
        RHI_Sync_State m_cpu_state = RHI_Sync_State::Idle;
    };
}
