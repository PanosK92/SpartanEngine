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

//= INCLUDES =====================
#include "Spartan.h"
#include "../RHI_Semaphore.h"
#include "../RHI_Implementation.h"
//================================

namespace Spartan
{
    RHI_Semaphore::RHI_Semaphore(RHI_Device* rhi_device, bool is_timeline /*= false*/, const char* name /*= nullptr*/)
    {
        m_is_timeline   = is_timeline;
        m_rhi_device    = rhi_device;

        VkSemaphoreTypeCreateInfo semaphore_type_create_info = {};
        semaphore_type_create_info.sType                     = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO;
        semaphore_type_create_info.pNext                     = nullptr;
        semaphore_type_create_info.semaphoreType             = VK_SEMAPHORE_TYPE_TIMELINE;
        semaphore_type_create_info.initialValue              = 0;

        VkSemaphoreCreateInfo semaphore_create_info = {};
        semaphore_create_info.sType                 = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
        semaphore_create_info.pNext                 = m_is_timeline ? &semaphore_type_create_info : nullptr;
        semaphore_create_info.flags                 = 0;

        // Create
        if (!vulkan_utility::error::check(vkCreateSemaphore(m_rhi_device->GetContextRhi()->device, &semaphore_create_info, nullptr, reinterpret_cast<VkSemaphore*>(&m_resource))))
            return;

        // Name
        if (name)
        {
            m_name = name;
            vulkan_utility::debug::set_name(static_cast<VkSemaphore>(m_resource), name);
        }
    }

    RHI_Semaphore::~RHI_Semaphore()
    {
        if (!m_resource)
            return;

        // Wait in case it's still in use by the GPU
        m_rhi_device->Queue_WaitAll();

        vkDestroySemaphore(m_rhi_device->GetContextRhi()->device, static_cast<VkSemaphore>(m_resource), nullptr);
        m_resource = nullptr;
    }

    bool RHI_Semaphore::Wait(const uint64_t value, uint64_t timeout /*= std::numeric_limits<uint64_t>::max()*/)
    {
        SP_ASSERT(m_is_timeline);

        VkSemaphoreWaitInfo semaphore_wait_info = {};
        semaphore_wait_info.sType               = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO;
        semaphore_wait_info.pNext               = nullptr;
        semaphore_wait_info.flags               = 0;
        semaphore_wait_info.semaphoreCount      = 1;
        semaphore_wait_info.pSemaphores         = reinterpret_cast<VkSemaphore*>(&m_resource);
        semaphore_wait_info.pValues             = &value;

        return vulkan_utility::error::check(vkWaitSemaphores(m_rhi_device->GetContextRhi()->device, &semaphore_wait_info, timeout));
    }

    bool RHI_Semaphore::Signal(const uint64_t value)
    {
        SP_ASSERT(m_is_timeline);

        VkSemaphoreSignalInfo semaphore_signal_info = {};
        semaphore_signal_info.sType                 = VK_STRUCTURE_TYPE_SEMAPHORE_SIGNAL_INFO;
        semaphore_signal_info.pNext                 = nullptr;
        semaphore_signal_info.semaphore             = static_cast<VkSemaphore>(m_resource);
        semaphore_signal_info.value                 = value;

        return vulkan_utility::error::check(vkSignalSemaphore(m_rhi_device->GetContextRhi()->device, &semaphore_signal_info));
    }

    uint64_t RHI_Semaphore::GetValue()
    {
        SP_ASSERT(m_is_timeline);

        uint64_t value = 0;
        vulkan_utility::error::check(vkGetSemaphoreCounterValue(m_rhi_device->GetContextRhi()->device, static_cast<VkSemaphore>(m_resource), &value));
        return value;
    }
}
