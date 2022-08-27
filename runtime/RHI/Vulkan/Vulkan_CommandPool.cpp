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

//= INCLUDES ==================
#include "pch.h"
#include "../RHI_CommandPool.h"
#include "../RHI_CommandList.h"
#include "Vulkan_Utility.h"
//=============================

//= NAMESPACES =====
using namespace std;
//==================

namespace Spartan
{
    RHI_CommandPool::RHI_CommandPool(RHI_Device* rhi_device, const char* name, const uint64_t swap_chain_id) : SpartanObject(rhi_device->GetContext())
    {
        m_rhi_device    = rhi_device;
        m_name          = name;
        m_swap_chain_id = swap_chain_id;
    }

    RHI_CommandPool::~RHI_CommandPool()
    {
        if (m_cmd_pool_count == 0)
            return;

        // Wait for GPU
        m_rhi_device->QueueWait(m_queue_type);

        // Free command buffers
        uint32_t cmd_index = 0;
        for (uint32_t index_pool = 0; index_pool < m_cmd_pool_count; index_pool++)
        {
            for (uint32_t index_cmd_list = 0; index_cmd_list < m_cmd_list_count; index_cmd_list++)
            {
                if (shared_ptr<RHI_CommandList>& cmd_list = m_cmd_lists[index_pool + index_cmd_list])
                {
                    SP_ASSERT(m_rhi_resources[index_pool] != nullptr);
                    SP_ASSERT(m_cmd_lists[cmd_index] != nullptr);

                    VkCommandBuffer vk_cmd_buffer = reinterpret_cast<VkCommandBuffer>(cmd_list->GetRhiResource());

                    vkFreeCommandBuffers(
                        m_rhi_device->GetRhiContext()->device,
                        static_cast<VkCommandPool>(m_rhi_resources[index_pool]),
                        1,
                        &vk_cmd_buffer
                    );

                    m_cmd_lists[cmd_index] = nullptr;

                    cmd_index++;
                }
            }
        }

        // Destroy pools
        for (uint32_t i = 0; i < m_cmd_pool_count; i++)
        {
            vkDestroyCommandPool(m_rhi_device->GetRhiContext()->device, static_cast<VkCommandPool>(m_rhi_resources[i]), nullptr);
            m_rhi_resources[i] = nullptr;
        }
    }

    void RHI_CommandPool::CreateCommandPool(const RHI_Queue_Type queue_type)
    {
        m_queue_type = queue_type;

        VkCommandPoolCreateInfo cmd_pool_info = {};
        cmd_pool_info.sType                   = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        cmd_pool_info.queueFamilyIndex        = m_rhi_device->GetQueueIndex(queue_type);
        cmd_pool_info.flags                   = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT; // specifies that command buffers allocated from the pool will be short-lived

        // Create
        m_rhi_resources.emplace_back(nullptr);
        SP_ASSERT_MSG(
            vkCreateCommandPool(m_rhi_device->GetRhiContext()->device, &cmd_pool_info, nullptr, reinterpret_cast<VkCommandPool*>(&m_rhi_resources.back())) == VK_SUCCESS,
            "Failed to create command pool"
        );

        // Name
        uint32_t cmd_pool_count = static_cast<uint32_t>(m_rhi_resources.size());
        vulkan_utility::debug::set_object_name(static_cast<VkCommandPool>(m_rhi_resources.back()), (m_name + string("_") + to_string(cmd_pool_count)).c_str());
    }

    void RHI_CommandPool::Reset(const uint32_t pool_index)
    {
        SP_ASSERT_MSG(m_rhi_resources[0] != nullptr, "Can't reset an uninitialised command list pool");

        VkDevice device    = m_rhi_device->GetRhiContext()->device;
        VkCommandPool pool = static_cast<VkCommandPool>(m_rhi_resources[pool_index]);
        SP_ASSERT_MSG(vkResetCommandPool(device, pool, 0) == VK_SUCCESS, "Failed to reset command pool");
    }
}
