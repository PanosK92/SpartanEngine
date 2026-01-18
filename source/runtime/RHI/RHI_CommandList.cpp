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

//= INCLUDES =================
#include "pch.h"
#include "RHI_CommandList.h"
#include "RHI_Texture.h"
#include "RHI_SyncPrimitive.h"
//============================

//= NAMESPACES ========
using namespace std;
using namespace chrono;
//=====================

namespace spartan
{
    namespace
    {
        time_point<high_resolution_clock> start_time;
    }

    void RHI_CommandList::WaitForExecution(const bool log_wait_time)
    {
        SP_ASSERT_MSG(m_state == RHI_CommandListState::Submitted, "the command list hasn't been submitted, can't wait for it.");

        if (log_wait_time)
        { 
            start_time = high_resolution_clock::now();
        }

        // wait
        uint64_t timeout_nanoseconds = 10'000'000'000; // 10 seconds
        m_rendering_complete_semaphore_timeline->Wait(timeout_nanoseconds);
        m_state = RHI_CommandListState::Idle;

        if (log_wait_time)
        {
            auto end_time = high_resolution_clock::now();
            auto duration = duration_cast<microseconds>(end_time - start_time).count();
            SP_LOG_INFO("wait time: %lld microseconds\n", duration);
        }
    }

    void RHI_CommandList::Dispatch(RHI_Texture* texture, float resolution_scale /*= 1.0f*/)
    {
        // clamp scale
        resolution_scale = clamp(resolution_scale, 0.5f, 1.0f);

        const uint32_t thread_group_size = 8;

        // scaled dimensions (round up to ensure coverage)
        const uint32_t scaled_width  = static_cast<uint32_t>(ceil(texture->GetWidth() * resolution_scale));
        const uint32_t scaled_height = static_cast<uint32_t>(ceil(texture->GetHeight() * resolution_scale));
        const uint32_t scaled_depth  = (texture->GetType() == RHI_Texture_Type::Type3D) ? static_cast<uint32_t>(ceil(texture->GetDepth() * resolution_scale)): 1;

        // conservative dispatch counts
        const uint32_t dispatch_x = (scaled_width + thread_group_size - 1) / thread_group_size;
        const uint32_t dispatch_y = (scaled_height + thread_group_size - 1) / thread_group_size;
        const uint32_t dispatch_z = (scaled_depth + thread_group_size - 1) / thread_group_size;

        Dispatch(dispatch_x, dispatch_y, dispatch_z);

        // synchronize writes to the texture
        if (GetImageLayout(texture->GetRhiResource(), 0) == RHI_Image_Layout::General)
        {
            InsertBarrier(texture, RHI_BarrierType::EnsureWriteThenRead);
        }
    }
}
