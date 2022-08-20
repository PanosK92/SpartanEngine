/*
Copyright(c) 2016-2022 Panos Karabelas

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
#include "RHI_Definition.h"
#include "RHI_CommandList.h"
#include <vector>
//================================

namespace Spartan
{
    class RHI_CommandPool : public SpartanObject
    {
    public:
        RHI_CommandPool(RHI_Device* rhi_device, const char* name, const uint64_t swap_chain_id);
        ~RHI_CommandPool();

        void AllocateCommandLists(const RHI_Queue_Type queue_type, const uint32_t cmd_list_count = 2, const uint32_t cmd_pool_count = 2);
        bool Step();

        RHI_CommandList* GetCurrentCommandList()       { return m_cmd_lists[m_cmd_pool_index + m_cmd_list_index].get(); }
        uint32_t GetCommandListIndex()           const { return m_cmd_list_index; }
        void*& GetResource()                           { return m_rhi_resources[m_cmd_pool_index]; }
        uint64_t GetSwapchainId()                const { return m_swap_chain_id; }

    private:
        void CreateCommandPool(const RHI_Queue_Type queue_type);
        void Reset(const uint32_t pool_index);

        // Command lists
        std::vector<std::shared_ptr<RHI_CommandList>> m_cmd_lists;
        uint32_t m_cmd_list_index = 0;
        uint32_t m_cmd_list_count = 0;

        // Command pools
        std::vector<void*> m_rhi_resources;
        uint32_t m_cmd_pool_index = 0;
        uint32_t m_cmd_pool_count = 0;

        // The swapchain for which this thread pool's command lists will be presenting to.
        uint64_t m_swap_chain_id = 0;

        // Misc
        bool m_first_step     = true;
        RHI_Device* m_rhi_device = nullptr;
    };
}
