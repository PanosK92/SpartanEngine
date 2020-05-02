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

//= INCLUDES ===================
#include "../RHI_Device.h"
#include "../RHI_VertexBuffer.h"
#include "../RHI_Vertex.h"
#include "../RHI_CommandList.h"
#include "../../Logging/Log.h"
//==============================

//= NAMESPACES =====
using namespace std;
//==================

namespace Spartan
{
	RHI_VertexBuffer::~RHI_VertexBuffer()
	{
        // Wait in case the buffer is still in use
        m_rhi_device->Queue_WaitAll();

		vulkan_utility::buffer::destroy(m_buffer);
		vulkan_utility::memory::free(m_buffer_memory);
	}

	bool RHI_VertexBuffer::_Create(const void* vertices)
	{
		if (!m_rhi_device || !m_rhi_device->GetContextRhi()->device)
		{
			LOG_ERROR_INVALID_INTERNALS();
			return false;
		}

        // Wait in case the buffer is still in use
        if (m_buffer)
        {
            m_rhi_device->Queue_WaitAll();
        }

		// Clear previous buffer
		vulkan_utility::buffer::destroy(m_buffer);
		vulkan_utility::memory::free(m_buffer_memory);

        bool use_staging = vertices != nullptr;
        m_mappable = !use_staging;

        // The reason we use staging is because VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT is the most optimal memory property.
        // At the same time, local memory cannot use Map() and Unamap(), so we disable that.

        if (!use_staging)
        {
            if (!vulkan_utility::buffer::create(
                    m_buffer,
                    m_buffer_memory,
                    m_size_gpu,
                    VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,                                          // usage
                    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT  // memory
                )
            ) return false;
        }
        else
        { 
		    // Create staging/source buffer and copy the vertices to it
            void* staging_buffer        = nullptr;
            void* staging_buffer_memory = nullptr;
		    if (!vulkan_utility::buffer::create(
                    staging_buffer,
                    staging_buffer_memory,
                    m_size_gpu,
                    VK_BUFFER_USAGE_TRANSFER_SRC_BIT,                                               // usage
                    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,     // memory
                    vertices
                )
            ) return false;

            // Create destination buffer
            if (!vulkan_utility::buffer::create(
                    m_buffer,
                    m_buffer_memory,
                    m_size_gpu,
                    VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,   // usage
                    VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,                                    // memory
                    nullptr
                )
            ) return false;

            // Copy from staging buffer
            {
                // Create command buffer
                VkCommandBuffer cmd_buffer = vulkan_utility::command_buffer_immediate::begin(RHI_Queue_Transfer);

                VkBuffer* buffer_vk         = reinterpret_cast<VkBuffer*>(&m_buffer);
                VkBuffer* buffer_staging_vk = reinterpret_cast<VkBuffer*>(&staging_buffer);

                // Copy
                VkBufferCopy copy_region = {};
                copy_region.size         = m_size_gpu;
                vkCmdCopyBuffer(cmd_buffer, *buffer_staging_vk, *buffer_vk, 1, &copy_region);

                // Flush and free command buffer
                if (!vulkan_utility::command_buffer_immediate::end(RHI_Queue_Transfer))
                    return false;

                // Destroy staging resources
                vulkan_utility::buffer::destroy(staging_buffer);
                vulkan_utility::memory::free(staging_buffer_memory);
            }
        }

        // Set debug names
        vulkan_utility::debug::set_buffer_name(static_cast<VkBuffer>(m_buffer), "vertex_buffer");
        vulkan_utility::debug::set_device_memory_name(static_cast<VkDeviceMemory>(m_buffer_memory), "vertex_buffer");

		return true;
	}

	void* RHI_VertexBuffer::Map() const
	{
        if (!m_rhi_device || !m_rhi_device->GetContextRhi()->device || !m_buffer_memory)
        {
            LOG_ERROR_INVALID_INTERNALS();
            return nullptr;
        }

        if (!m_mappable)
        {
            LOG_ERROR("This buffer can only be updated via staging");
            return nullptr;
        }

		void* ptr = nullptr;

        // Map
        vulkan_utility::error::check
        (
            vkMapMemory
            (
                m_rhi_device->GetContextRhi()->device,
                static_cast<VkDeviceMemory>(m_buffer_memory),
                0,
                m_size_gpu,
                0,
                reinterpret_cast<void**>(&ptr)
            )
        );
		
		return ptr;
	}

	bool RHI_VertexBuffer::Unmap() const
	{
		if (!m_buffer_memory)
		{
			LOG_ERROR_INVALID_INTERNALS();
			return false;
		}

        if (!m_mappable)
        {
            LOG_ERROR("This buffer can only be updated via staging");
            return false;
        }

		vkUnmapMemory(m_rhi_device->GetContextRhi()->device, static_cast<VkDeviceMemory>(m_buffer_memory));
		return true;
	}

    bool RHI_VertexBuffer::Flush() const
    {
        VkMappedMemoryRange mapped_memory_range = {};
        mapped_memory_range.sType               = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
        mapped_memory_range.memory              = static_cast<VkDeviceMemory>(m_buffer_memory);
        mapped_memory_range.offset              = 0;
        mapped_memory_range.size                = VK_WHOLE_SIZE;
        return vulkan_utility::error::check(vkFlushMappedMemoryRanges(m_rhi_device->GetContextRhi()->device, 1, &mapped_memory_range));
    }
}
#endif
