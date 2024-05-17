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

//= INCLUDES ===============
#include "pch.h"
#include "RHI_CommandList.h"
#include "RHI_Texture.h"
#include "RHI_Semaphore.h"
//==========================

//= NAMESPACES ========
using namespace std;
using namespace chrono;
//=====================

namespace Spartan
{
    namespace
    {
        bool log_wait_time = false;
        time_point<high_resolution_clock> start_time;
    }

    void RHI_CommandList::WaitForExecution()
    {
        SP_ASSERT_MSG(m_state == RHI_CommandListState::Submitted, "the command list hasn't been submitted, can't wait for it.");

        if (log_wait_time)
        { 
            start_time = high_resolution_clock::now();
        }

        // wait
        uint64_t value      = m_rendering_complete_semaphore_timeline->GetValue();
        uint64_t wait_value = m_rendering_complete_semaphore_timeline->GetWaitValue();
        m_rendering_complete_semaphore_timeline->Wait(wait_value);
        m_state = RHI_CommandListState::Idle;

        if (log_wait_time)
        {
            auto end_time = high_resolution_clock::now();
            auto duration = duration_cast<microseconds>(end_time - start_time).count();
            SP_LOG_INFO("wait time: %lld microseconds\n", duration);
        }
    }

    void RHI_CommandList::Dispatch(RHI_Texture* texture)
    {
        const float thread_group_count      = 8.0f;
        const uint32_t thread_group_count_x = static_cast<uint32_t>(Math::Helper::Ceil(static_cast<float>(texture->GetWidth()) / thread_group_count));
        const uint32_t thread_group_count_y = static_cast<uint32_t>(Math::Helper::Ceil(static_cast<float>(texture->GetHeight()) / thread_group_count));

        Dispatch(thread_group_count_x, thread_group_count_y);
    }
}
