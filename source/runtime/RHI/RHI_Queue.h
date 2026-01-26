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
#include "../Core/SpartanObject.h"
#include "RHI_Definitions.h"
#include "RHI_CommandList.h"
#include <array>
//================================

namespace spartan
{
    class RHI_Queue : public SpartanObject
    {
    public:
        RHI_Queue(const RHI_Queue_Type queue_type, const char* name);
        ~RHI_Queue();

        void Wait(const bool flush = false);
        void Submit(void* cmd_buffer, const uint32_t wait_flags, RHI_SyncPrimitive* semaphore_wait, RHI_SyncPrimitive* semaphore, RHI_SyncPrimitive* semaphore_timeline);
        bool Present(void* swapchain, const uint32_t image_index, RHI_SyncPrimitive* semaphore_wait);
        RHI_CommandList* NextCommandList();
        RHI_Queue_Type GetType() const { return m_type; }

    private:
        std::array<std::shared_ptr<RHI_CommandList>, 2> m_cmd_lists = { nullptr };
        void* m_rhi_resource                                        = nullptr;
        std::atomic<uint32_t> m_index                               = 0;
        RHI_Queue_Type m_type                                       = RHI_Queue_Type::Max;
    };
}
