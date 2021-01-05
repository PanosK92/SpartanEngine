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
    RHI_Semaphore::RHI_Semaphore(RHI_Device* rhi_device, const char* name /*= nullptr*/)
    {
        m_rhi_device = rhi_device;

        // Describe
        VkSemaphoreCreateInfo semaphore_info = {};
        semaphore_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

        VkSemaphore* vk_semaphore = reinterpret_cast<VkSemaphore*>(&m_resource);

        // Create
        if (!vulkan_utility::error::check(vkCreateSemaphore(m_rhi_device->GetContextRhi()->device, &semaphore_info, nullptr, vk_semaphore)))
            return;

        // Name
        if (name)
        {
            m_name = name;
            vulkan_utility::debug::set_name(*vk_semaphore, name);
        }
    }

    RHI_Semaphore::~RHI_Semaphore()
    {
        if (!m_resource)
            return;

        VkSemaphore semaphore_vk = static_cast<VkSemaphore>(m_resource);
        vkDestroySemaphore(m_rhi_device->GetContextRhi()->device, semaphore_vk, nullptr);
        m_resource = nullptr;
    }
}
