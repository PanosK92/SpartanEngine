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
        m_object_name   = name;
        m_swap_chain_id = swap_chain_id;
        m_rhi_resources.fill(nullptr);

        VkCommandPoolCreateInfo cmd_pool_info = {};
        cmd_pool_info.sType                   = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        cmd_pool_info.queueFamilyIndex        = rhi_device->GetQueueIndex(RHI_Queue_Type::Graphics);
        cmd_pool_info.flags                   = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT; // specifies that command buffers allocated from the pool will be short-lived

        // Create pools
        for (uint32_t i = 0; i < static_cast<uint32_t>(m_rhi_resources.size()); i++)
        {
            SP_ASSERT_MSG(vulkan_utility::error::check(
                vkCreateCommandPool(m_rhi_device->GetRhiContext()->device, &cmd_pool_info, nullptr, reinterpret_cast<VkCommandPool*>(&m_rhi_resources[i]))),
                "Failed to create command pool"
            );
        }

        // Name
        for (uint32_t i = 0; i < static_cast<uint32_t>(m_rhi_resources.size()); i++)
        {
            vulkan_utility::debug::set_object_name(static_cast<VkCommandPool>(m_rhi_resources[i]), (m_object_name + string("_") + to_string(i)).c_str());
        }
    }

    RHI_CommandPool::~RHI_CommandPool()
    {
        if (!m_rhi_resources[0])
            return;

        // Wait for GPU
        m_rhi_device->QueueWaitAll();

        VkDevice device = m_rhi_device->GetRhiContext()->device;

        // Free command buffers
        for (uint32_t index_pool = 0; index_pool < static_cast<uint32_t>(m_rhi_resources.size()); index_pool++)
        {
            for (uint32_t index_cmd_list = 0; index_cmd_list < GetCommandListCount(); index_cmd_list++)
            {
                VkCommandPool cmd_pool                         = static_cast<VkCommandPool>(m_rhi_resources[index_pool]);
                vector<shared_ptr<RHI_CommandList>>& cmd_lists = m_cmd_lists[index_pool];
                VkCommandBuffer cmd_buffer                     = reinterpret_cast<VkCommandBuffer>(cmd_lists[index_cmd_list]->GetResource());

                vkFreeCommandBuffers(device, cmd_pool, 1, &cmd_buffer);
            }
        }

        // Destroy pools
        for (uint32_t i = 0; i < static_cast<uint32_t>(m_rhi_resources.size()); i++)
        {
            vkDestroyCommandPool(device, static_cast<VkCommandPool>(m_rhi_resources[i]), nullptr);
            m_rhi_resources[i] = nullptr;
        }
    }

    void RHI_CommandPool::Reset()
    {
        SP_ASSERT_MSG(m_rhi_resources[0], "Can't reset an uninitialised command list pool");

        // Advance pool index
        m_pool_index = (m_pool_index + 1) % static_cast<uint32_t>(m_rhi_resources.size());

        // Wait for any command lists to finish executing
        vector<shared_ptr<RHI_CommandList>>& cmd_lists = m_cmd_lists[m_pool_index];
        for (shared_ptr<RHI_CommandList> cmd_list : cmd_lists)
        {
            if (cmd_list->GetState() == RHI_CommandListState::Submitted)
            {
                cmd_list->Wait();
            }
        }

        // Reset the command pool
        VkDevice device    = m_rhi_device->GetRhiContext()->device;
        VkCommandPool pool = static_cast<VkCommandPool>(m_rhi_resources[m_pool_index]);
        SP_ASSERT_MSG(vulkan_utility::error::check(vkResetCommandPool(device, pool, 0)), "Failed to reset command pool");
    }
}
