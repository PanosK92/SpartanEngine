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

//= INCLUDES =====================
#include "pch.h"
#include "../RHI_Implementation.h"
#include "../RHI_Device.h"
#include "../RHI_Queue.h"
#include "../RHI_Semaphore.h"
//================================

//= NAMESPACES =====
using namespace std;
//==================

namespace Spartan
{
    namespace
    {
        array<mutex, 3> mutexes;

        mutex& get_mutex(RHI_Queue* queue)
        {
            return mutexes[static_cast<uint32_t>(queue->GetType())];
        }
    }

    RHI_Queue::RHI_Queue(const RHI_Queue_Type queue_type, const char* name) : SpObject()
    {
        m_object_name = name;
        m_type        = queue_type;

        // command pools
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

        // command lists
        for (uint32_t i = 0; i < cmd_lists_per_pool; i++)
        {
            string name = m_object_name + "_cmd_pool_0_" + to_string(0);
            m_cmd_lists_0[i] = make_shared<RHI_CommandList>(m_rhi_resources[0], name.c_str());

            name = m_object_name + "_cmd_pool_1_" + to_string(0);
            m_cmd_lists_1[i] = make_shared<RHI_CommandList>(m_rhi_resources[1], name.c_str());
        }
    }

    RHI_Queue::~RHI_Queue()
    {
        Wait();

        for (uint32_t i = 0; i < cmd_lists_per_pool; i++)
        {
            VkCommandBuffer vk_cmd_buffer = reinterpret_cast<VkCommandBuffer>(m_cmd_lists_0[i]->GetRhiResource());
            vkFreeCommandBuffers(
                RHI_Context::device,
                static_cast<VkCommandPool>(m_rhi_resources[0]),
                1,
                &vk_cmd_buffer
            );

            vk_cmd_buffer = reinterpret_cast<VkCommandBuffer>(m_cmd_lists_1[i]->GetRhiResource());
            vkFreeCommandBuffers(
                RHI_Context::device,
                static_cast<VkCommandPool>(m_rhi_resources[1]),
                1,
                &vk_cmd_buffer
            );
        }

        vkDestroyCommandPool(RHI_Context::device, static_cast<VkCommandPool>(m_rhi_resources[0]), nullptr);
        vkDestroyCommandPool(RHI_Context::device, static_cast<VkCommandPool>(m_rhi_resources[1]), nullptr);
    }

    bool RHI_Queue::NextCommandList()
    {
        bool has_been_reset = false;

        if (m_first_tick)
        {
            m_first_tick = false;
            return has_been_reset;
        }

        m_index++;

        // if we have no more command lists, switch to the other pool
        if (m_index == cmd_lists_per_pool)
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

    void RHI_Queue::Wait()
    {
        lock_guard<mutex> lock(get_mutex(this));
        SP_VK_ASSERT_MSG(vkQueueWaitIdle(static_cast<VkQueue>(RHI_Device::GetQueueRhiResource(m_type))), "Failed to wait for queue");
    }

    void RHI_Queue::Submit(void* cmd_buffer, const uint32_t wait_flags, RHI_Semaphore* semaphore, RHI_Semaphore* semaphore_timeline)
    {
        // validate
        SP_ASSERT(cmd_buffer != nullptr);
        SP_ASSERT(semaphore != nullptr);
        SP_ASSERT(semaphore_timeline != nullptr);

        // semaphore binary
        VkSemaphoreSubmitInfo signal_semaphore_info = {};
        signal_semaphore_info.sType                 = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO_KHR;
        signal_semaphore_info.semaphore             = static_cast<VkSemaphore>(semaphore->GetRhiResource());
        signal_semaphore_info.value                 = 0; // ignored for binary semaphores

        // semaphore timeline
        VkSemaphoreSubmitInfo timeline_semaphore_info = {};
        timeline_semaphore_info.sType                 = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO_KHR;
        timeline_semaphore_info.semaphore             = static_cast<VkSemaphore>(semaphore_timeline->GetRhiResource());
        timeline_semaphore_info.stageMask             = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT_KHR; // todo: adjust based on the queue
        static uint64_t timeline_value                = 0;
        timeline_semaphore_info.value                 = ++timeline_value;

        lock_guard<mutex> lock(get_mutex(this));
        semaphore_timeline->SetWaitValue(timeline_semaphore_info.value);

        // submit
        {
            VkSubmitInfo2 submit_info                 = {};
            submit_info.sType                         = VK_STRUCTURE_TYPE_SUBMIT_INFO_2;
            submit_info.waitSemaphoreInfoCount        = 0;
            submit_info.pWaitSemaphoreInfos           = nullptr;
            submit_info.signalSemaphoreInfoCount      = 2;
            VkSemaphoreSubmitInfo semaphore_infos[]   = { signal_semaphore_info, timeline_semaphore_info };
            submit_info.pSignalSemaphoreInfos         = semaphore_infos;
            submit_info.commandBufferInfoCount        = 1;
            VkCommandBufferSubmitInfo cmd_buffer_info = {};
            cmd_buffer_info.sType                     = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO_KHR;
            cmd_buffer_info.commandBuffer             = *reinterpret_cast<VkCommandBuffer*>(&cmd_buffer);
            submit_info.pCommandBufferInfos           = &cmd_buffer_info;

            void* queue = RHI_Device::GetQueueRhiResource(m_type);
            SP_VK_ASSERT_MSG(vkQueueSubmit2(static_cast<VkQueue>(queue), 1, &submit_info, nullptr), "Failed to submit");
            semaphore->SetSignaled(true);
        }
    }

    void RHI_Queue::Present(void* swapchain, const uint32_t image_index, vector<RHI_Semaphore*>& wait_semaphores)
    {
        array<VkSemaphore, 3> vk_wait_semaphores = { nullptr, nullptr, nullptr };

        // get semaphore vulkan resources
        uint32_t semaphore_count = static_cast<uint32_t>(wait_semaphores.size());
        for (uint32_t i = 0; i < semaphore_count; i++)
        {
            vk_wait_semaphores[i] = static_cast<VkSemaphore>(wait_semaphores[i]->GetRhiResource());
        }

        VkPresentInfoKHR present_info   = {};
        present_info.sType              = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
        present_info.waitSemaphoreCount = semaphore_count;
        present_info.pWaitSemaphores    = vk_wait_semaphores.data();
        present_info.swapchainCount     = 1;
        present_info.pSwapchains        = reinterpret_cast<VkSwapchainKHR*>(&swapchain);
        present_info.pImageIndices      = &image_index;

        lock_guard<mutex> lock(get_mutex(this));
        void* queue = RHI_Device::GetQueueRhiResource(m_type);
        SP_VK_ASSERT_MSG(vkQueuePresentKHR(static_cast<VkQueue>(queue), &present_info), "Failed to present");
    }
}
