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
#include "RHI_CommandList.h"
#include <array>
//================================

namespace Spartan
{
    const uint32_t cmd_lists_per_pool = 8;

    class RHI_Queue : public SpartanObject
    {
    public:
        RHI_Queue(const RHI_Queue_Type queue_type, const char* name);
        ~RHI_Queue();

        // core
        void NextCommandList();
        void Wait();
        void Submit(void* cmd_buffer, const uint32_t wait_flags, RHI_Semaphore* semaphore, RHI_Semaphore* semaphore_timeline);
        void Present(void* swapchain, const uint32_t image_index, std::vector<RHI_Semaphore*>& wait_semaphores);

        // misc
        auto& GetCommandListPool()        { return m_using_pool_a ? m_cmd_lists_0 : m_cmd_lists_1; }
        RHI_CommandList* GetCommandList() { return GetCommandListPool()[m_index].get(); }
        RHI_Queue_Type GetType() const    { return m_type; }

    private:
        std::array<std::shared_ptr<RHI_CommandList>, cmd_lists_per_pool> m_cmd_lists_0;
        std::array<std::shared_ptr<RHI_CommandList>, cmd_lists_per_pool> m_cmd_lists_1;
        std::array<void*, 2> m_rhi_resources;

        uint32_t m_index      = 0;
        bool m_using_pool_a   = true;
        bool m_first_tick     = true;
        RHI_Queue_Type m_type = RHI_Queue_Type::Max;
    };
}
