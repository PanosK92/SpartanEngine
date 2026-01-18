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

//= INCLUDES =======================
#include "pch.h"
#include "../RHI_Implementation.h"
#include "../RHI_Device.h"
#include "../RHI_Queue.h"
#include "../RHI_SyncPrimitive.h"
#include "../RHI_VendorTechnology.h"
#include "../Core/Debugging.h"
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
            cmd_list->Submit(0, false);
        }

        // with enough command lists available, there is no wait time
        if (cmd_list->GetState() == RHI_CommandListState::Submitted)
        {
            cmd_list->WaitForExecution();
        }

        SP_ASSERT(cmd_list->GetState() == RHI_CommandListState::Idle);

        return cmd_list.get();
    }

    void RHI_Queue::Wait(const bool flush)
    {
        // ensure that any submitted command lists have completed execution
        for (auto& cmd_list : m_cmd_lists)
        {
            bool got_flushed = false;
            if (cmd_list->GetState() == RHI_CommandListState::Recording && flush)
            {
                cmd_list->Submit(0, false); // submit any pending work
                got_flushed = true;
            }

            if (cmd_list->GetState() == RHI_CommandListState::Submitted)
            {
                cmd_list->WaitForExecution(); // wait for submitted command lists to complete
            }

            // if we flushed, start recording again (so we don't interfere with external code that may be using it)
            if (got_flushed)
            { 
                cmd_list->Begin();
            }
        }

        // lock here, after the potential submit (above), to avoid a deadlock (shared mutex)
        lock_guard<mutex> lock(get_mutex(this));

        SP_ASSERT_VK(vkQueueWaitIdle(static_cast<VkQueue>(RHI_Device::GetQueueRhiResource(m_type))));
    }

    void RHI_Queue::Submit(void* cmd_buffer, const uint32_t wait_flags, RHI_SyncPrimitive* semaphore_wait, RHI_SyncPrimitive* semaphore_signal, RHI_SyncPrimitive* semaphore_timeline_signal)
    {
        lock_guard<mutex> lock(get_mutex(this));
    
        // wait semaphore setup
        VkSemaphoreSubmitInfo semaphores_list_wait[1] = {};
        uint32_t wait_semaphore_count = 0;
        if (semaphore_wait)
        {
            semaphores_list_wait[0].sType     = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO_KHR;
            semaphores_list_wait[0].semaphore = static_cast<VkSemaphore>(semaphore_wait->GetRhiResource());
            semaphores_list_wait[0].stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT_KHR;
            semaphores_list_wait[0].value     = 0; // ignored for binary semaphores
            wait_semaphore_count = 1;
        }
    
        // signal semaphores setup
        VkSemaphoreSubmitInfo semaphores_list_signal[2] = {};
        uint32_t signal_semaphore_count = 0;
        if (semaphore_signal)
        {
            semaphores_list_signal[signal_semaphore_count].sType     = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO_KHR;
            semaphores_list_signal[signal_semaphore_count].semaphore = static_cast<VkSemaphore>(semaphore_signal->GetRhiResource());
            semaphores_list_signal[signal_semaphore_count].stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT_KHR;
            semaphores_list_signal[signal_semaphore_count].value     = 0; // ignored for binary semaphores
            signal_semaphore_count++;
        }
        if (semaphore_timeline_signal)
        {
            semaphores_list_signal[signal_semaphore_count].sType     = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO_KHR;
            semaphores_list_signal[signal_semaphore_count].semaphore = static_cast<VkSemaphore>(semaphore_timeline_signal->GetRhiResource());
            semaphores_list_signal[signal_semaphore_count].stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT_KHR;
            semaphores_list_signal[signal_semaphore_count].value     = semaphore_timeline_signal->GetNextSignalValue(); // signal
            signal_semaphore_count++;
        }
    
        // command buffer
        VkCommandBufferSubmitInfo cmd_buffer_info = {};
        cmd_buffer_info.sType                     = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO_KHR;
        cmd_buffer_info.commandBuffer             = reinterpret_cast<VkCommandBuffer>(cmd_buffer);
    
        // submit
        {
            VkSubmitInfo2 submit_info            = {};
            submit_info.sType                    = VK_STRUCTURE_TYPE_SUBMIT_INFO_2;
            submit_info.waitSemaphoreInfoCount   = wait_semaphore_count;
            submit_info.pWaitSemaphoreInfos      = wait_semaphore_count ? semaphores_list_wait : nullptr;
            submit_info.signalSemaphoreInfoCount = signal_semaphore_count;
            submit_info.pSignalSemaphoreInfos    = signal_semaphore_count ? semaphores_list_signal : nullptr;
            submit_info.commandBufferInfoCount   = 1;
            submit_info.pCommandBufferInfos      = &cmd_buffer_info;
    
            VkResult result = vkQueueSubmit2(static_cast<VkQueue>(RHI_Device::GetQueueRhiResource(m_type)), 1, &submit_info, nullptr);
    
            if (result == VK_ERROR_DEVICE_LOST)
            {
                if (Debugging::IsBreadcrumbsEnabled())
                {
                    bool flush = false; // we don't need to flush and we do, this will call Submit() again, causing stack overflow
                    RHI_Device::QueueWaitAll(flush);
                    RHI_VendorTechnology::Breadcrumbs_OnDeviceRemoved();
                }
                SP_ERROR_WINDOW("GPU crashed");
            }
    
            SP_ASSERT_VK(result);
        }
    }

    bool RHI_Queue::Present(void* swapchain, const uint32_t image_index, RHI_SyncPrimitive* semaphore_wait)
    {
        lock_guard<mutex> lock(get_mutex(this));

        // get semaphore vulkan resources
        array<VkSemaphore, 1> vk_wait_semaphores = { nullptr };
        vk_wait_semaphores[0] = static_cast<VkSemaphore>(semaphore_wait->GetRhiResource());

        VkPresentInfoKHR present_info   = {};
        present_info.sType              = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
        present_info.waitSemaphoreCount = 1;
        present_info.pWaitSemaphores    = vk_wait_semaphores.data();
        present_info.swapchainCount     = 1;
        present_info.pSwapchains        = reinterpret_cast<VkSwapchainKHR*>(&swapchain);
        present_info.pImageIndices      = &image_index;

        VkResult result = vkQueuePresentKHR(static_cast<VkQueue>(RHI_Device::GetQueueRhiResource(m_type)), &present_info);

        // vk_error_out_of_date_khr and vk_suboptimal_khr are not errors, they indicate the swapchain needs recreation
        if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR)
        {
            return false; // signal swapchain needs recreation
        }

        SP_ASSERT_VK(result);
        return true;
    }
}
