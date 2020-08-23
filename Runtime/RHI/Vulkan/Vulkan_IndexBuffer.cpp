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
#include "../RHI_Implementation.h"
#include "../RHI_Device.h"
#include "../RHI_IndexBuffer.h"
#include "../RHI_CommandList.h"
//================================

//= NAMESPACES =====
using namespace std;
//==================

namespace Spartan
{
    void RHI_IndexBuffer::_destroy()
    {
        // Wait in case the buffer is still in use
        m_rhi_device->Queue_WaitAll();

        // Unmap
        if (m_mapped)
        {
            vmaUnmapMemory(m_rhi_device->GetContextRhi()->allocator, static_cast<VmaAllocation>(m_allocation));
            m_mapped = nullptr;
        }

        // Destroy
        vulkan_utility::buffer::destroy(m_buffer);
    }

    bool RHI_IndexBuffer::_create(const void* indices)
    {
        if (!m_rhi_device || !m_rhi_device->GetContextRhi()->device)
        {
            LOG_ERROR_INVALID_INTERNALS();
            return false;
        }

        RHI_Context* rhi_context = m_rhi_device->GetContextRhi();

        // Destroy previous buffer
        _destroy();

        // Memory in Vulkan doesn't need to be unmapped before using it on GPU, but unless a
        // memory type has VK_MEMORY_PROPERTY_HOST_COHERENT_BIT flag set, you need to manually
        // invalidate cache before reading of mapped pointer and flush cache after writing to
        // mapped pointer. Map/unmap operations don't do that automatically.

        bool use_staging = indices != nullptr;
        if (!use_staging)
        {
            VkMemoryPropertyFlags flags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT;
            flags |= !m_persistent_mapping ? VK_MEMORY_PROPERTY_HOST_COHERENT_BIT : 0;
            VmaAllocation allocation = vulkan_utility::buffer::create(m_buffer, m_size_gpu, VK_BUFFER_USAGE_INDEX_BUFFER_BIT, flags, true);
            if (!allocation)
                return false;

            m_allocation    = static_cast<void*>(allocation);
            m_is_mappable   = true;
        }
        else
        {
            // The reason we use staging is because memory with VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT is not mappable but it's fast, we want that.

            // Create staging/source buffer and copy the indices to it
            void* staging_buffer = nullptr;
            VmaAllocation allocation_staging = vulkan_utility::buffer::create(staging_buffer, m_size_gpu, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, false, indices);
            if (!allocation_staging)
                return false;

            // Create destination buffer
            VmaAllocation allocation = vulkan_utility::buffer::create(m_buffer, m_size_gpu, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
            if (!allocation)
                return false;

            // Copy staging buffer to destination buffer
            {
                // Create command buffer
                VkCommandBuffer cmd_buffer = vulkan_utility::command_buffer_immediate::begin(RHI_Queue_Transfer);

                VkBuffer* buffer_vk         = reinterpret_cast<VkBuffer*>(&m_buffer);
                VkBuffer* buffer_staging_vk = reinterpret_cast<VkBuffer*>(&staging_buffer);

                // Copy
                VkBufferCopy copy_region = {};
                copy_region.size = m_size_gpu;
                vkCmdCopyBuffer(cmd_buffer, *buffer_staging_vk, *buffer_vk, 1, &copy_region);

                // Flush and free command buffer
                if (!vulkan_utility::command_buffer_immediate::end(RHI_Queue_Transfer))
                    return false;

                // Destroy staging buffer
                vulkan_utility::buffer::destroy(staging_buffer);
            }

            m_allocation    = static_cast<void*>(allocation);
            m_is_mappable   = false;
        }

        // Set debug name
        vulkan_utility::debug::set_name(static_cast<VkBuffer>(m_buffer), "index_buffer");

        return true;
    }

    void* RHI_IndexBuffer::Map()
    {
        if (!m_is_mappable)
        {
            LOG_ERROR("Not mappable, can only be updated via staging");
            return nullptr;
        }

        if (!m_rhi_device || !m_rhi_device->GetContextRhi()->device)
        {
            LOG_ERROR_INVALID_INTERNALS();
            return nullptr;
        }

        if (!m_allocation)
        {
            LOG_ERROR("Invalid allocation");
            return nullptr;
        }

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

    bool RHI_IndexBuffer::Unmap()
    {
        if (!m_is_mappable)
        {
            LOG_ERROR("Not mappable, can only be updated via staging");
            return false;
        }

        if (!m_allocation)
        {
            LOG_ERROR("Invalid allocation");
            return false;
        }

        if (m_persistent_mapping)
        {
            if (!vulkan_utility::error::check(vmaFlushAllocation(m_rhi_device->GetContextRhi()->allocator, static_cast<VmaAllocation>(m_allocation), 0, m_size_gpu)))
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
