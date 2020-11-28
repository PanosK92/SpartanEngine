/*
Copyright(c) 2016-2020 Panos Karabelas

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
#include "../RHI_Fence.h"
#include "../RHI_Implementation.h"
#include "../RHI_Device.h"
//================================

namespace Spartan
{
    RHI_Fence::RHI_Fence(RHI_Device* rhi_device, const char* name /*= nullptr*/)
    {
        m_rhi_device = rhi_device;

        // Describe
        VkFenceCreateInfo fence_info = {};
        fence_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;

        // Create
        if (!vulkan_utility::error::check(vkCreateFence(m_rhi_device->GetContextRhi()->device, &fence_info, nullptr, reinterpret_cast<VkFence*>(&m_resource))))
            return;

        // Name
        vulkan_utility::debug::set_name(static_cast<VkFence>(m_resource), name);
    }

    RHI_Fence::~RHI_Fence()
    {
        if (!m_resource)
            return;

        vkDestroyFence(m_rhi_device->GetContextRhi()->device, static_cast<VkFence>(m_resource), nullptr);
        m_resource = nullptr;
    }

    bool RHI_Fence::IsSignaled()
    {
        return vkGetFenceStatus(m_rhi_device->GetContextRhi()->device, reinterpret_cast<VkFence>(m_resource)) == VK_SUCCESS;
    }

    bool RHI_Fence::Wait(uint64_t timeout /*= std::numeric_limits<uint64_t>::max()*/)
    {
        return vulkan_utility::error::check(vkWaitForFences(m_rhi_device->GetContextRhi()->device, 1, reinterpret_cast<VkFence*>(&m_resource), true, timeout));
    }

    bool RHI_Fence::Reset()
    {
        return IsSignaled() ? vulkan_utility::error::check(vkResetFences(m_rhi_device->GetContextRhi()->device, 1, reinterpret_cast<VkFence*>(&m_resource))) : true;
    }
}
