/*
Copyright(c) 2016-2021 Panos Karabelas

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
#include "Spartan.h"
#include "../RHI_CommandPool.h"
#include "../RHI_CommandList.h"
#include "Vulkan_Utility.h"
//=============================

//= NAMESPACES =====
using namespace std;
//==================

namespace Spartan
{
    RHI_CommandPool::RHI_CommandPool(RHI_Device* rhi_device, const char* name) : SpartanObject(rhi_device->GetContext())
    {
        m_rhi_device  = rhi_device;
        m_object_name = name;

        VkCommandPoolCreateInfo cmd_pool_info = {};
        cmd_pool_info.sType                   = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        cmd_pool_info.queueFamilyIndex        = rhi_device->GetQueueIndex(RHI_Queue_Type::Graphics);
        cmd_pool_info.flags                   = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT; // specifies that command buffers allocated from the pool will be short-lived

        // Create
        SP_ASSERT(vulkan_utility::error::check(
            vkCreateCommandPool(m_rhi_device->GetContextRhi()->device, &cmd_pool_info, nullptr, reinterpret_cast<VkCommandPool*>(&m_resource))) &&
            "Failed to create command pool"
        );

        // Name
        vulkan_utility::debug::set_name(static_cast<VkCommandPool>(m_resource), name);
    }

    RHI_CommandPool::~RHI_CommandPool()
    {
        if (!m_resource)
            return;

        // Wait in case it's still in use by the GPU
        m_rhi_device->QueueWaitAll();

        VkDevice device = m_rhi_device->GetContextRhi()->device;
        VkCommandPool cmd_pool = static_cast<VkCommandPool>(m_resource);

        // Free command buffers
        for (shared_ptr<RHI_CommandList> cmd_list : m_cmd_lists)
        {
            VkCommandBuffer command_buffer = reinterpret_cast<VkCommandBuffer>(cmd_list->GetResource_CommandBuffer());
            vkFreeCommandBuffers(device, cmd_pool, 1, &command_buffer);
        }

        // Destroy pool
        vkDestroyCommandPool(device, cmd_pool, nullptr);
        m_resource = nullptr;
    }

    void RHI_CommandPool::Reset()
    {
        SP_ASSERT(m_resource && "Can't reset an uninitialised command list pool");

        // See if any of the command lists are executing;
        for (shared_ptr<RHI_CommandList> cmd_list : m_cmd_lists)
        {
            if (cmd_list->GetState() == RHI_CommandListState::Submitted)
            {
                cmd_list->Wait();
            }
        }

        // If no command list is executing, reset the command pool
        SP_ASSERT(vulkan_utility::error::check(vkResetCommandPool(m_rhi_device->GetContextRhi()->device, static_cast<VkCommandPool>(m_resource), 0))
            && "Failed to reset command pool");
    }
}
