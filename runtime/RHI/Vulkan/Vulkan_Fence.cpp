/*
Copyright(c) 2016-2023 Panos Karabelas

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
#include "../RHI_Fence.h"
#include "../RHI_Implementation.h"
#include "../RHI_Device.h"
#include "../Rendering/Renderer.h"
//================================

namespace Spartan
{
    RHI_Fence::RHI_Fence(const char* name /*= nullptr*/)
    {
        // Describe
        VkFenceCreateInfo fence_info = {};
        fence_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;

        // Create
        SP_ASSERT_MSG(vkCreateFence(Renderer::GetRhiDevice()->GetRhiContext()->device, &fence_info, nullptr, reinterpret_cast<VkFence*>(&m_resource)) == VK_SUCCESS, "Failed to create fence");

        // Name
        if (name)
        {
            m_name = name;
            vulkan_utility::debug::set_object_name(static_cast<VkFence>(m_resource), m_name.c_str());
        }
    }

    RHI_Fence::~RHI_Fence()
    {
        if (!m_resource)
            return;

        // Wait in case it's still in use by the GPU
        Renderer::GetRhiDevice()->QueueWaitAll();

        vkDestroyFence(Renderer::GetRhiDevice()->GetRhiContext()->device, static_cast<VkFence>(m_resource), nullptr);
        m_resource = nullptr;
    }

    bool RHI_Fence::IsSignaled()
    {
        return vkGetFenceStatus(Renderer::GetRhiDevice()->GetRhiContext()->device, reinterpret_cast<VkFence>(m_resource)) == VK_SUCCESS;
    }

    bool RHI_Fence::Wait(uint64_t timeout_nanoseconds /*= 1000000000*/)
    {
        return vkWaitForFences(Renderer::GetRhiDevice()->GetRhiContext()->device, 1, reinterpret_cast<VkFence*>(&m_resource), true, timeout_nanoseconds) == VK_SUCCESS;
    }

    void RHI_Fence::Reset()
    {
        SP_ASSERT_MSG(vkResetFences(Renderer::GetRhiDevice()->GetRhiContext()->device, 1, reinterpret_cast<VkFence*>(&m_resource)) == VK_SUCCESS, "Failed to reset fence");
        m_cpu_state = RHI_Sync_State::Idle;
    }
}
