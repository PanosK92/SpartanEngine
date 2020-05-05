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

//= IMPLEMENTATION ===============
#ifdef API_GRAPHICS_VULKAN
#include "../RHI_Implementation.h"
//================================

//= INCLUDES ==================
#include "../RHI_Device.h"
#include "../RHI_IndexBuffer.h"
#include "../RHI_CommandList.h"
#include "../../Logging/Log.h"
//=============================

//= NAMESPACES =====
using namespace std;
//==================

namespace Spartan
{
	RHI_IndexBuffer::~RHI_IndexBuffer()
	{
        // Wait in case the buffer is still in use
        m_rhi_device->Queue_WaitAll();

		vulkan_utility::buffer::destroy(m_buffer);
	}

	bool RHI_IndexBuffer::_Create(const void* indices)
	{
		if (!m_rhi_device || !m_rhi_device->GetContextRhi()->device)
		{
			LOG_ERROR_INVALID_INTERNALS();
			return false;
		}

        RHI_Context* rhi_context = m_rhi_device->GetContextRhi();

        // Wait in case the buffer is still in use
        if (m_buffer)
        {
            m_rhi_device->Queue_WaitAll();
        }

		// Clear previous buffer
		vulkan_utility::buffer::destroy(m_buffer);

        // Memory in Vulkan doesn't need to be unmapped before using it on GPU, but unless a
        // memory type has VK_MEMORY_PROPERTY_HOST_COHERENT_BIT flag set, you need to manually
        // invalidate cache before reading of mapped pointer and flush cache after writing to
        // mapped pointer. Map/unmap operations don't do that automatically.

        bool use_staging = indices != nullptr;
        if (!use_staging)
        {
            VmaAllocation allocation = vulkan_utility::buffer::create(m_buffer, m_size_gpu, VK_BUFFER_USAGE_INDEX_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
            if (!allocation)
                return false;

            m_allocation        = static_cast<void*>(allocation);
            m_is_mappable       = true;
            m_is_host_coherent  = true;
        }
        else
        {
            // The reason we use staging is because memory with VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT is not mappable but it's fast, we want that.

            // Create staging/source buffer and copy the indices to it
            void* staging_buffer = nullptr;
            VmaAllocation allocation_staging = vulkan_utility::buffer::create(staging_buffer, m_size_gpu, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, indices);
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

            m_allocation        = static_cast<void*>(allocation);
            m_is_mappable       = false;
            m_is_host_coherent  = false;
        }

        // Set debug name
        vulkan_utility::debug::set_buffer_name(static_cast<VkBuffer>(m_buffer), "index_buffer");

		return true;
	}

	void* RHI_IndexBuffer::Map() const
	{
		if (!m_rhi_device || !m_rhi_device->GetContextRhi()->device || !m_allocation)
		{
			LOG_ERROR_INVALID_INTERNALS();
			return nullptr;
		}

        if (!m_is_mappable)
        {
            LOG_ERROR("Not mappable, can only be updated via staging");
            return nullptr;
        }

		void* ptr = nullptr;

        if (m_is_mappable)
        {
            if (!m_is_host_coherent)
            {
                if (!vulkan_utility::error::check(vmaInvalidateAllocation(m_rhi_device->GetContextRhi()->allocator, static_cast<VmaAllocation>(m_allocation), 0, m_size_gpu)))
                    return nullptr;
            }

            vulkan_utility::error::check(vmaMapMemory(m_rhi_device->GetContextRhi()->allocator, static_cast<VmaAllocation>(m_allocation), reinterpret_cast<void**>(&ptr)));
        }

		return ptr;
	}

	bool RHI_IndexBuffer::Unmap() const
	{
        if (!m_allocation)
            return true;

        if (!m_is_mappable)
        {
            LOG_ERROR("Not mappable, can only be updated via staging");
            return false;
        }

        if (!m_is_host_coherent)
        {
            if (!vulkan_utility::error::check(vmaFlushAllocation(m_rhi_device->GetContextRhi()->allocator, static_cast<VmaAllocation>(m_allocation), 0, m_size_gpu)))
                return false;
        }

        vmaUnmapMemory(m_rhi_device->GetContextRhi()->allocator, static_cast<VmaAllocation>(m_allocation));

        return true;
	}
}
#endif
