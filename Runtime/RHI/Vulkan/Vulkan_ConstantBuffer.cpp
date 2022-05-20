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

//= INCLUDES ========================
#include "Spartan.h"
#include "../RHI_Implementation.h"
#include "../RHI_ConstantBuffer.h"
#include "../RHI_Device.h"
#include "../RHI_CommandList.h"
#include "../../Rendering/Renderer.h"
//===================================

//= NAMESPACES =====
using namespace std;
//==================

namespace Spartan
{
    void RHI_ConstantBuffer::_destroy()
    {
        if (!m_resource)
            return;

        // Discard the current command list in case it's referencing the buffer.
        if (Renderer* renderer = m_rhi_device->GetContext()->GetSubsystem<Renderer>())
        {
            if (RHI_CommandList* cmd_list = renderer->GetCmdList())
            {
                cmd_list->Discard();
            }
        }

        // Wait in case it's still in use by the GPU
        m_rhi_device->QueueWaitAll();

        // Unmap
        if (m_mapped)
        {
            vmaUnmapMemory(m_rhi_device->GetContextRhi()->allocator, static_cast<VmaAllocation>(m_allocation));
            m_mapped = nullptr;
        }

        // Destroy
        vulkan_utility::buffer::destroy(m_resource);
    }

    RHI_ConstantBuffer::RHI_ConstantBuffer(const std::shared_ptr<RHI_Device>& rhi_device, const string& name, bool is_dynamic /*= false*/)
    {
        m_rhi_device = rhi_device;
        m_object_name = name;
        m_is_dynamic = is_dynamic;
    }

    bool RHI_ConstantBuffer::_create()
    {
        SP_ASSERT(m_rhi_device != nullptr);
        SP_ASSERT(m_rhi_device->GetContextRhi()->device != nullptr);

        // Destroy previous buffer
        _destroy();

        // Calculate required alignment based on minimum device offset alignment
        size_t min_ubo_alignment = m_rhi_device->GetMinUniformBufferOffsetAllignment();
        if (min_ubo_alignment > 0)
        {
            m_stride = static_cast<uint32_t>((m_stride + min_ubo_alignment - 1) & ~(min_ubo_alignment - 1));
        }
        m_object_size_gpu = m_offset_count * m_stride;

        // Create buffer
        VkMemoryPropertyFlags flags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT;
        flags |= !m_persistent_mapping ? VK_MEMORY_PROPERTY_HOST_COHERENT_BIT : 0;
        VmaAllocation allocation = vulkan_utility::buffer::create(m_resource, m_object_size_gpu, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, flags, nullptr);
        if (!allocation)
        {
            LOG_ERROR("Failed to allocate buffer");
            return false;
        }

        m_allocation = static_cast<void*>(allocation);

        // Set debug name
        vulkan_utility::debug::set_name(static_cast<VkBuffer>(m_resource), m_object_name.c_str());

        return true;
    }

    void* RHI_ConstantBuffer::Map()
    {
        SP_ASSERT(m_rhi_device != nullptr);
        SP_ASSERT(m_rhi_device->GetContextRhi()->device != nullptr);
        SP_ASSERT(m_allocation != nullptr);

        if (!m_mapped)
        {
            if (!vulkan_utility::error::check(vmaMapMemory(m_rhi_device->GetContextRhi()->allocator, static_cast<VmaAllocation>(m_allocation), reinterpret_cast<void**>(&m_mapped))))
            {
                LOG_ERROR("Failed to map memory");
                return nullptr;
            }
        }

        return m_mapped;
    }

    bool RHI_ConstantBuffer::Unmap(const uint64_t offset /*= 0*/, const uint64_t size /*= 0*/)
    {
        SP_ASSERT(m_rhi_device != nullptr);
        SP_ASSERT(m_rhi_device->GetContextRhi()->device != nullptr);
        SP_ASSERT(m_allocation != nullptr);

        if (m_persistent_mapping)
        {
            if (!vulkan_utility::error::check(vmaFlushAllocation(m_rhi_device->GetContextRhi()->allocator, static_cast<VmaAllocation>(m_allocation), offset, size != 0 ? size : VK_WHOLE_SIZE)))
            {
                LOG_ERROR("Failed to flush memory");
                return false;
            }
        }
        else
        {
            if (m_mapped)
            {
                vmaUnmapMemory(m_rhi_device->GetContextRhi()->allocator, static_cast<VmaAllocation>(m_allocation));
                m_mapped = nullptr;
            }
        }

        return true;
    }
}
