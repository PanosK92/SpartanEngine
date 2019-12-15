/*
Copyright(c) 2016-2019 Panos Karabelas

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
#include "../RHI_Implementation.h"
#ifdef API_GRAPHICS_VULKAN
//================================

//= INCLUDES ===================
#include "../RHI_Device.h"
#include "../RHI_VertexBuffer.h"
#include "../RHI_Vertex.h"
#include "../../Logging/Log.h"
//==============================

//= NAMESPACES =====
using namespace std;
//==================

namespace Spartan
{
	RHI_VertexBuffer::~RHI_VertexBuffer()
	{
        // Wait in case the buffer is still in use by the graphics queue
        vkQueueWaitIdle(m_rhi_device->GetContextRhi()->queue_graphics);

		vulkan_common::buffer::destroy(m_rhi_device->GetContextRhi(), m_buffer);
		vulkan_common::memory::free(m_rhi_device->GetContextRhi(), m_buffer_memory);
	}

	bool RHI_VertexBuffer::_Create(const void* vertices)
	{
		if (!m_rhi_device || !m_rhi_device->GetContextRhi()->device)
		{
			LOG_ERROR_INVALID_INTERNALS();
			return false;
		}

        // Wait in case the buffer is still in use by the graphics queue
        vkQueueWaitIdle(m_rhi_device->GetContextRhi()->queue_graphics);

		// Clear previous buffer
		vulkan_common::buffer::destroy(m_rhi_device->GetContextRhi(), m_buffer);
		vulkan_common::memory::free(m_rhi_device->GetContextRhi(), m_buffer_memory);

		// Create buffer
		VkBuffer buffer					= nullptr;
		VkDeviceMemory buffer_memory	= nullptr;
		m_size							= m_stride * m_vertex_count;
		if (!vulkan_common::buffer::create_allocate_bind(m_rhi_device->GetContextRhi(), buffer, buffer_memory, m_size, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT))
			return false;

        // Save
        m_buffer = static_cast<void*>(buffer);
        m_buffer_memory = static_cast<void*>(buffer_memory);

        // Initial data
        if (vertices != nullptr)
        {
            if (void* data = Map())
            {
                memcpy(data, vertices, m_size);
                Unmap();
            }
        }

		return true;
	}

	void* RHI_VertexBuffer::Map() const
	{
		void* ptr = nullptr;

        // Map
        vulkan_common::error::check_result
        (
            vkMapMemory
            (
                m_rhi_device->GetContextRhi()->device,
                static_cast<VkDeviceMemory>(m_buffer_memory),
                0,
                m_size,
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

		vkUnmapMemory(m_rhi_device->GetContextRhi()->device, static_cast<VkDeviceMemory>(m_buffer_memory));
		return true;
	}
}
#endif
