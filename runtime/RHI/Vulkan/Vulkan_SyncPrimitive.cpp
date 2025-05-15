/*
Copyright(c) 2015-2025 Panos Karabelas

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
#include "../RHI_Device.h"
#include "../RHI_SyncPrimitive.h"
#include "../RHI_Implementation.h"
//================================

namespace spartan
{
    namespace fence
    {
        void create(void*& resource)
        {
            VkFenceCreateInfo fence_info = {};
            fence_info.sType             = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;

            SP_ASSERT_VK(vkCreateFence(RHI_Context::device, &fence_info, nullptr, reinterpret_cast<VkFence*>(&resource)));
        }

        bool is_signaled(void*& resource)
        {
            return vkGetFenceStatus(RHI_Context::device, reinterpret_cast<VkFence>(resource)) == VK_SUCCESS;
        }

        void wait(uint64_t timeout, void*& resource)
        {
            SP_ASSERT_VK(vkWaitForFences(RHI_Context::device, 1, reinterpret_cast<VkFence*>(&resource), true, timeout));
        }

        void reset(void*& resource)
        {
            SP_ASSERT_VK(vkResetFences(RHI_Context::device, 1, reinterpret_cast<VkFence*>(&resource)));
        }
    }

    namespace semaphore
    { 
        void create(const RHI_SyncPrimitive_Type type, void*& resource)
        {
            SP_ASSERT(resource == nullptr);

            VkSemaphoreTypeCreateInfo semaphore_type_create_info = {};
            semaphore_type_create_info.sType                     = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO;
            semaphore_type_create_info.pNext                     = nullptr;
            semaphore_type_create_info.semaphoreType             = VK_SEMAPHORE_TYPE_TIMELINE;
            semaphore_type_create_info.initialValue              = 0;

            VkSemaphoreCreateInfo semaphore_create_info = {};
            semaphore_create_info.sType                 = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
            semaphore_create_info.pNext                 = type == RHI_SyncPrimitive_Type::SemaphoreTimeline ? &semaphore_type_create_info : nullptr;
            semaphore_create_info.flags                 = 0;

            SP_ASSERT_VK(vkCreateSemaphore(RHI_Context::device, &semaphore_create_info, nullptr, reinterpret_cast<VkSemaphore*>(&resource)));
        }

        void wait(const uint64_t value, const uint64_t timeout, void*& resource)
        {
            VkSemaphoreWaitInfo semaphore_wait_info = {};
            semaphore_wait_info.sType               = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO;
            semaphore_wait_info.pNext               = nullptr;
            semaphore_wait_info.flags               = 0;
            semaphore_wait_info.semaphoreCount      = 1;
            semaphore_wait_info.pSemaphores         = reinterpret_cast<VkSemaphore*>(&resource);
            semaphore_wait_info.pValues             = &value;

            SP_ASSERT_VK(vkWaitSemaphores(RHI_Context::device, &semaphore_wait_info, timeout));
        }

        void signal(const uint64_t value, void*& resource)
        {
            VkSemaphoreSignalInfo semaphore_signal_info = {};
            semaphore_signal_info.sType                 = VK_STRUCTURE_TYPE_SEMAPHORE_SIGNAL_INFO;
            semaphore_signal_info.pNext                 = nullptr;
            semaphore_signal_info.semaphore             = static_cast<VkSemaphore>(resource);
            semaphore_signal_info.value                 = value;

            SP_ASSERT_VK(vkSignalSemaphore(RHI_Context::device, &semaphore_signal_info));
        }

        uint64_t get_value(void*& resource)
        {
            uint64_t value = 0;
            SP_ASSERT_VK(vkGetSemaphoreCounterValue(RHI_Context::device, static_cast<VkSemaphore>(resource), &value));

            return value;
        }
    }

    RHI_SyncPrimitive::RHI_SyncPrimitive(const RHI_SyncPrimitive_Type type, const char* name)
    {
        m_type        = type;
        m_object_name = name;

        if (m_type == RHI_SyncPrimitive_Type::Fence)
        {
            fence::create(m_rhi_resource);
            RHI_Device::SetResourceName(m_rhi_resource, RHI_Resource_Type::Fence, name);
        }
        else
        { 
            semaphore::create(type, m_rhi_resource);
            RHI_Device::SetResourceName(m_rhi_resource, RHI_Resource_Type::Semaphore, name);
        }
    }

    RHI_SyncPrimitive::~RHI_SyncPrimitive()
    {
        if (!m_rhi_resource)
            return;

        RHI_Device::DeletionQueueAdd(m_type == RHI_SyncPrimitive_Type::Fence ? RHI_Resource_Type::Fence : RHI_Resource_Type::Semaphore, m_rhi_resource);

        m_rhi_resource = nullptr;
    }

    void RHI_SyncPrimitive::Wait(const uint64_t timeout_nanoseconds)
    {
        SP_ASSERT(m_type == RHI_SyncPrimitive_Type::Fence || m_type == RHI_SyncPrimitive_Type::SemaphoreTimeline);

        if (m_type == RHI_SyncPrimitive_Type::Fence)
        {
            fence::wait(timeout_nanoseconds, m_rhi_resource);
        }
        else
        { 
            semaphore::wait(m_value, timeout_nanoseconds, m_rhi_resource);
        }
    }

    void RHI_SyncPrimitive::Signal(const uint64_t value)
    {
        SP_ASSERT(m_type == RHI_SyncPrimitive_Type::SemaphoreTimeline);

        semaphore::signal(value, m_rhi_resource);
    }

    bool RHI_SyncPrimitive::IsSignaled()
    {
        SP_ASSERT(m_type != RHI_SyncPrimitive_Type::Semaphore);

        if (m_type == RHI_SyncPrimitive_Type::Fence)
            return fence::is_signaled(m_rhi_resource);

        return semaphore::get_value(m_rhi_resource) == m_value;
    }

    void RHI_SyncPrimitive::Reset()
    {
        SP_ASSERT(m_type == RHI_SyncPrimitive_Type::Fence);

        fence::reset(m_rhi_resource);
    }
}
