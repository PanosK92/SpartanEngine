/*
Copyright(c) 2016-2025 Panos Karabelas

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

//= INCLUDES =======================
#include "pch.h"
#include "../RHI_Implementation.h"
#include "../RHI_Device.h"
#include "../RHI_Queue.h"
#include "../RHI_SyncPrimitive.h"
#include "../RHI_AMD_FFX.h"
#include "../Core/Debugging.h"
#include "../Core/ProgressTracker.h"
//==================================

//= NAMESPACES =====
using namespace std;
//==================

namespace spartan
{
    namespace
    {
        array<mutex, 3> mutexes;

        mutex& get_mutex(RHI_Queue* queue)
        {
            return mutexes[static_cast<uint32_t>(queue->GetType())];
        }
    }

    RHI_Queue::RHI_Queue(const RHI_Queue_Type queue_type, const char* name) : SpartanObject()
    {
        m_object_name = name;
        m_type        = queue_type;

        // command pool
        {
            VkCommandPoolCreateInfo cmd_pool_info = {};
            cmd_pool_info.sType                   = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
            cmd_pool_info.queueFamilyIndex        = RHI_Device::GetQueueIndex(queue_type);
            cmd_pool_info.flags                   = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT |           // short-lived (reset or freed)
                                                    VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT; // can be reset

            // create the first one
            VkCommandPool cmd_pool = nullptr;
            SP_ASSERT_VK(vkCreateCommandPool(RHI_Context::device, &cmd_pool_info, nullptr, &cmd_pool));
            RHI_Device::SetResourceName(cmd_pool, RHI_Resource_Type::CommandPool, m_object_name.c_str());
            m_rhi_resource = static_cast<void*>(cmd_pool);
        }

        // command lists
        for (uint32_t i = 0; i < static_cast<uint32_t>(m_cmd_lists.size()); i++)
        {
            m_cmd_lists[i] = make_shared<RHI_CommandList>(this, m_rhi_resource, (("cmd_list_") + to_string(i)).c_str());
        }
    }

    RHI_Queue::~RHI_Queue()
    {
        Wait();

        for (uint32_t i = 0; i < static_cast<uint32_t>(m_cmd_lists.size()); i++)
        {
            VkCommandBuffer vk_cmd_buffer = reinterpret_cast<VkCommandBuffer>(m_cmd_lists[i]->GetRhiResource());
            vkFreeCommandBuffers(
                RHI_Context::device,
                static_cast<VkCommandPool>(m_rhi_resource),
                1,
                &vk_cmd_buffer
            );
        }

        vkDestroyCommandPool(RHI_Context::device, static_cast<VkCommandPool>(m_rhi_resource), nullptr);
    }

    RHI_CommandList* RHI_Queue::NextCommandList()
    {
        m_index        = (m_index + 1) % static_cast<uint32_t>(m_cmd_lists.size());
        auto& cmd_list = m_cmd_lists[m_index];

        // submit any pending work (toggling between fullscreen and windowed mode can leave work)
        if (cmd_list->GetState() == RHI_CommandListState::Recording)
        {
           cmd_list->Submit(0);
        }

        // with enough command lists available, there is no wait time
        if (cmd_list->GetState() == RHI_CommandListState::Submitted)
        {
            cmd_list->WaitForExecution();
        }

        SP_ASSERT(cmd_list->GetState() == RHI_CommandListState::Idle);

        return cmd_list.get();
    }

    void RHI_Queue::Wait()
    {
        // when loading textures (other threads) the queue will be used to submit data for staging
        unique_lock<mutex> lock;
        if (ProgressTracker::IsLoading())
        {
            lock = unique_lock<mutex>(get_mutex(this));
        }

        SP_ASSERT_VK(vkQueueWaitIdle(static_cast<VkQueue>(RHI_Device::GetQueueRhiResource(m_type))));
    }

    void RHI_Queue::Submit(void* cmd_buffer, const uint32_t wait_flags, RHI_SyncPrimitive* semaphore, RHI_SyncPrimitive* semaphore_timeline)
    {
        // when loading textures (other threads) the queue will be used to submit data for staging
        unique_lock<mutex> lock;
        if (ProgressTracker::IsLoading())
        {
            lock = unique_lock<mutex>(get_mutex(this));
        }

        VkSemaphoreSubmitInfo semaphores[2] = {};

        // semaphore binary
        semaphores[0].sType     = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO_KHR;
        semaphores[0].semaphore = static_cast<VkSemaphore>(semaphore->GetRhiResource());
        semaphores[1].stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT_KHR; // todo: adjust based on the queue
        semaphores[0].value     = 0; // ignored for binary semaphores

        // semaphore timeline
        semaphores[1].sType     = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO_KHR;
        semaphores[1].semaphore = static_cast<VkSemaphore>(semaphore_timeline->GetRhiResource());
        semaphores[1].stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT_KHR; // todo: adjust based on the queue
        semaphores[1].value     = semaphore_timeline->GetNextSignalValue(); // signal

        // command buffer
        VkCommandBufferSubmitInfo cmd_buffer_info = {};
        cmd_buffer_info.sType                     = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO_KHR;
        cmd_buffer_info.commandBuffer             = *reinterpret_cast<VkCommandBuffer*>(&cmd_buffer);
  
        // submit
        {
            VkSubmitInfo2 submit_info            = {};
            submit_info.sType                    = VK_STRUCTURE_TYPE_SUBMIT_INFO_2;
            submit_info.waitSemaphoreInfoCount   = 0;
            submit_info.pWaitSemaphoreInfos      = nullptr;
            submit_info.signalSemaphoreInfoCount = 2;
            submit_info.pSignalSemaphoreInfos    = semaphores;
            submit_info.commandBufferInfoCount   = 1;
            submit_info.pCommandBufferInfos      = &cmd_buffer_info;

            VkResult result = vkQueueSubmit2(static_cast<VkQueue>(RHI_Device::GetQueueRhiResource(m_type)), 1, &submit_info, nullptr);

            if (result == VK_ERROR_DEVICE_LOST)
            {
                if (Debugging::IsBreadcrumbsEnabled())
                { 
                    RHI_AMD_FFX::Breadcrumbs_OnDeviceRemoved();
                }

                SP_ERROR_WINDOW("GPU crashed");
            }

            SP_ASSERT_VK(result);
        }
    }

    void RHI_Queue::Present(void* swapchain, const uint32_t image_index, vector<RHI_SyncPrimitive*>& wait_semaphores)
    {
        // when loading textures (other threads) the queue will be used to submit data for staging
        unique_lock<mutex> lock;
        if (ProgressTracker::IsLoading())
        {
            lock = unique_lock<mutex>(get_mutex(this));
        }

        // get semaphore vulkan resources
        array<VkSemaphore, 3> vk_wait_semaphores = { nullptr };
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

        SP_ASSERT_VK(vkQueuePresentKHR(static_cast<VkQueue>(RHI_Device::GetQueueRhiResource(m_type)), &present_info));
    }
}
