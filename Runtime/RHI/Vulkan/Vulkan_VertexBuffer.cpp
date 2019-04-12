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

//= INCLUDES =====================
#include "../RHI_Device.h"
#include "../RHI_VertexBuffer.h"
#include "../RHI_Vertex.h"
#include "../../Logging/Log.h"
#include "Vulkan_Helper.h"
//================================

//= NAMESPACES =====
using namespace std;
//==================

namespace Spartan
{
	RHI_VertexBuffer::~RHI_VertexBuffer()
	{
		auto buffer			= static_cast<VkBuffer>(m_buffer);
		auto buffer_memory	= static_cast<VkDeviceMemory>(m_buffer_memory);

		if (buffer != VK_NULL_HANDLE)
		{
			vkDestroyBuffer( m_rhi_device->GetContext()->device, buffer, nullptr);
			m_buffer =  nullptr;
		}

		if (buffer_memory)
		{
			vkFreeMemory( m_rhi_device->GetContext()->device, buffer_memory, nullptr);
			m_buffer_memory = nullptr;
		}
	}

	bool RHI_VertexBuffer::Create(const void* vertices)
	{
		if (!m_rhi_device || !m_rhi_device->GetContext()->device)
		{
			LOG_ERROR_INVALID_INTERNALS();
			return false;
		}

		auto device			= m_rhi_device->GetContext()->device;
		auto buffer			= static_cast<VkBuffer>(m_buffer);
		auto buffer_memory	= static_cast<VkDeviceMemory>(m_buffer_memory);

		if (buffer != VK_NULL_HANDLE)
		{
			vkDestroyBuffer(device, buffer, nullptr);
		}

		if (buffer_memory)
		{
			vkFreeMemory(device, buffer_memory, nullptr);
		}

		auto new_size							= m_stride * m_vertex_count;
		VkDeviceSize vertex_buffer_size_aligned	= ((new_size - 1) / m_device_size + 1) * m_device_size;
		VkBufferCreateInfo buffer_info			= {};
		buffer_info.sType						= VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
		buffer_info.size						= vertex_buffer_size_aligned;
		buffer_info.usage						= VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
		buffer_info.sharingMode					= VK_SHARING_MODE_EXCLUSIVE;

		if (vkCreateBuffer(device, &buffer_info, nullptr, &buffer) != VK_SUCCESS)
		{
			LOG_ERROR("Failed to create buffer.");
			return false;
		}

		VkMemoryRequirements memory_requirements;
		vkGetBufferMemoryRequirements(device, buffer, &memory_requirements);
		m_device_size					= (m_device_size > memory_requirements.alignment) ? m_device_size : memory_requirements.alignment;
		VkMemoryAllocateInfo alloc_info = {};
		alloc_info.sType				= VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
		alloc_info.allocationSize		= memory_requirements.size;
		alloc_info.memoryTypeIndex		= vulkan_helper::GetMemoryType(m_rhi_device->GetContext()->device_physical, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT, memory_requirements.memoryTypeBits);
		if (vkAllocateMemory(device, &alloc_info, nullptr, &buffer_memory) != VK_SUCCESS)
		{
			LOG_ERROR("Failed to allocate memory.");
			return false;
		}
	
		if (vkBindBufferMemory(device, buffer, buffer_memory, 0) != VK_SUCCESS)
		{
			LOG_ERROR("Failed to bind");
			return false;
		}

		m_buffer		= static_cast<void*>(buffer);
		m_buffer_memory	= static_cast<void*>(buffer_memory);
		m_device_size	= new_size;

		return true;
	}

	void* RHI_VertexBuffer::Map() const
	{
		auto device_memory	= static_cast<VkDeviceMemory>(m_buffer_memory);
		void* ptr			= nullptr;

		if (vkMapMemory(m_rhi_device->GetContext()->device, device_memory, 0, m_vertex_count, 0, (void**)(&ptr)) != VK_SUCCESS)
		{
			LOG_ERROR("Failed to map.");
		}
		
		return ptr;
	}

	bool RHI_VertexBuffer::Unmap() const
	{
		auto buffer_memory = static_cast<VkDeviceMemory>(m_buffer_memory);

		VkMappedMemoryRange range[1]	= {};
		range[0].sType					= VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
		range[0].memory					= buffer_memory;
		range[0].size					= VK_WHOLE_SIZE;

		if (vkFlushMappedMemoryRanges(m_rhi_device->GetContext()->device, 1, range) != VK_SUCCESS)
		{
			LOG_ERROR("Failed to flush.");
			return false;
		}

		vkUnmapMemory(m_rhi_device->GetContext()->device, buffer_memory);
		return true;
	}
}
#endif