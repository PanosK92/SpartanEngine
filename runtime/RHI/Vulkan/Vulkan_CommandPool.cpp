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

//= INCLUDES =====================
#include "pch.h"
#include "../RHI_Implementation.h"
#include "../RHI_Device.h"
#include "../RHI_CommandPool.h"
//================================

//= NAMESPACES =====
using namespace std;
//==================

namespace Spartan
{
    RHI_CommandPool::RHI_CommandPool(const char* name, const uint64_t swap_chain_id, const RHI_Queue_Type queue_type) : SP_Object()
    {
        m_object_name   = name;
        m_swap_chain_id = swap_chain_id;
        m_queue_type    = queue_type;

        // create command pools
        {
            VkCommandPoolCreateInfo cmd_pool_info = {};
            cmd_pool_info.sType                   = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
            cmd_pool_info.queueFamilyIndex        = RHI_Device::QueueGetIndex(queue_type);
            cmd_pool_info.flags                   = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT; // specifies that command buffers allocated from the pool will be short-lived

            // create the first one
            VkCommandPool cmd_pool = nullptr;
            SP_VK_ASSERT_MSG(vkCreateCommandPool(RHI_Context::device, &cmd_pool_info, nullptr, &cmd_pool), "Failed to create command pool");
            RHI_Device::SetResourceName(cmd_pool, RHI_Resource_Type::CommandPool, m_object_name + string("_0"));
            m_rhi_resources[0] = static_cast<void*>(cmd_pool);

            // create the second one
            cmd_pool = nullptr;
            SP_VK_ASSERT_MSG(vkCreateCommandPool(RHI_Context::device, &cmd_pool_info, nullptr, &cmd_pool), "Failed to create command pool");
            RHI_Device::SetResourceName(cmd_pool, RHI_Resource_Type::CommandPool, m_object_name + string("_1"));
            m_rhi_resources[1] = static_cast<void*>(cmd_pool);
        }

        // create command lists
        {
            string name = m_object_name + "_cmd_pool_0_0";
            m_cmd_lists_0[0] = make_shared<RHI_CommandList>(queue_type, m_swap_chain_id, m_rhi_resources[0], name.c_str());

            name = m_object_name + "_cmd_pool_0_1";
            m_cmd_lists_0[1] = make_shared<RHI_CommandList>(queue_type, m_swap_chain_id, m_rhi_resources[0], name.c_str());

            name = m_object_name + "_cmd_pool_1_0";
            m_cmd_lists_1[0] = make_shared<RHI_CommandList>(queue_type, m_swap_chain_id, m_rhi_resources[1], name.c_str());

            name = m_object_name + "_cmd_pool_1_1";
            m_cmd_lists_1[1] = make_shared<RHI_CommandList>(queue_type, m_swap_chain_id, m_rhi_resources[1], name.c_str());
        }
    }

    RHI_CommandPool::~RHI_CommandPool()
    {
        // wait for GPU
        RHI_Device::QueueWait(m_queue_type);

        // free command lists
        {
            for (shared_ptr<RHI_CommandList> cmd_list : m_cmd_lists_0)
            {
                VkCommandBuffer vk_cmd_buffer = reinterpret_cast<VkCommandBuffer>(cmd_list->GetRhiResource());

                vkFreeCommandBuffers(
                    RHI_Context::device,
                    static_cast<VkCommandPool>(m_rhi_resources[0]),
                    1,
                    &vk_cmd_buffer
                );
            }

            for (shared_ptr<RHI_CommandList> cmd_list : m_cmd_lists_1)
            {
                VkCommandBuffer vk_cmd_buffer = reinterpret_cast<VkCommandBuffer>(cmd_list->GetRhiResource());

                vkFreeCommandBuffers(
                    RHI_Context::device,
                    static_cast<VkCommandPool>(m_rhi_resources[1]),
                    1,
                    &vk_cmd_buffer
                );
            }
        }

        // destroy commend pools
        vkDestroyCommandPool(RHI_Context::device, static_cast<VkCommandPool>(m_rhi_resources[0]), nullptr);
        vkDestroyCommandPool(RHI_Context::device, static_cast<VkCommandPool>(m_rhi_resources[1]), nullptr);
    }

    bool RHI_CommandPool::Tick()
    {
        bool has_been_reset = false;

        if (m_first_tick)
        {
            m_first_tick = false;
            return has_been_reset;
        }

        m_index++;

        // if we have no more command lists, switch to the other pool
        if (m_index == 2)
        {
            // switch command pool
            m_index            = 0;
            m_using_pool_a     = !m_using_pool_a;
            auto& cmd_lists    = m_using_pool_a ? m_cmd_lists_0 : m_cmd_lists_1;
            VkCommandPool pool = static_cast<VkCommandPool>(m_rhi_resources[m_using_pool_a ? 0 : 1]);

            // wait
            for (shared_ptr<RHI_CommandList> cmd_list : cmd_lists)
            {
                SP_ASSERT(cmd_list->GetState() != RHI_CommandListState::Recording);

                if (cmd_list->GetState() == RHI_CommandListState::Submitted)
                {
                    cmd_list->WaitForExecution();
                }
            }

            // reset
            SP_VK_ASSERT_MSG(vkResetCommandPool(RHI_Context::device, pool, 0), "Failed to reset command pool");
            has_been_reset = true;
        }

        return has_been_reset;
    }
}
