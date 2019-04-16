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

//= INCLUDES ==================
#include "../RHI_Device.h"
#include "../RHI_IndexBuffer.h"
#include "../../Logging/Log.h"
#include "Vulkan_Helper.h"
//=============================

//= NAMESPACES =====
using namespace std;
//==================

namespace Spartan
{
	inline void _destroy(shared_ptr<RHI_Device>& rhi_device, void*& buffer, void*& buffer_memory)
	{
		if (buffer)
		{
			vkDestroyBuffer(rhi_device->GetContext()->device, static_cast<VkBuffer>(buffer), nullptr);
			buffer = nullptr;
		}

		if (buffer_memory)
		{
			vkFreeMemory(rhi_device->GetContext()->device, static_cast<VkDeviceMemory>(buffer_memory), nullptr);
			buffer_memory = nullptr;
		}
	}

	RHI_IndexBuffer::~RHI_IndexBuffer()
	{
		_destroy(m_rhi_device, m_buffer, m_buffer_memory);
	}

	bool RHI_IndexBuffer::Create(const void* indices)
	{
		if (!m_rhi_device || !m_rhi_device->GetContext()->device)
		{
			LOG_ERROR_INVALID_INTERNALS();
			return false;
		}

		_destroy(m_rhi_device, m_buffer, m_buffer_memory);

		auto device						= m_rhi_device->GetContext()->device;
		VkBuffer buffer					= nullptr;
		VkDeviceMemory buffer_memory	= nullptr;
		VkDeviceSize size				= m_stride * m_index_count;

		VkBufferCreateInfo buffer_info	= {};
		buffer_info.sType				= VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
		buffer_info.size				= size;
		buffer_info.usage				= VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
		buffer_info.sharingMode			= VK_SHARING_MODE_EXCLUSIVE;

		auto result = vkCreateBuffer(device, &buffer_info, nullptr, &buffer);
		if (result != VK_SUCCESS)
		{
			LOGF_ERROR("Failed to create buffer, %s.", vulkan_helper::result_to_string(result));
			return false;
		}

		VkMemoryRequirements memory_requirements;
		vkGetBufferMemoryRequirements(device, buffer, &memory_requirements);
		VkMemoryAllocateInfo alloc_info = {};
		alloc_info.sType				= VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
		alloc_info.allocationSize		= memory_requirements.size;
		alloc_info.memoryTypeIndex		= vulkan_helper::GetMemoryType(m_rhi_device->GetContext()->device_physical, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT, memory_requirements.memoryTypeBits);

		result = vkAllocateMemory(device, &alloc_info, nullptr, &buffer_memory);
		if (result != VK_SUCCESS)
		{
			LOGF_ERROR("Failed to allocate memory, %s.", vulkan_helper::result_to_string(result));
			return false;
		}

		result = vkBindBufferMemory(device, buffer, buffer_memory, 0);
		if (result != VK_SUCCESS)
		{
			LOGF_ERROR("Failed to bind buffer memory, %s.", vulkan_helper::result_to_string(result));
			return false;
		}

		m_buffer		= static_cast<void*>(buffer);
		m_buffer_memory = static_cast<void*>(buffer_memory);

		return true;
	}

	void* RHI_IndexBuffer::Map() const
	{
		void* ptr = nullptr;

		auto result = vkMapMemory(m_rhi_device->GetContext()->device, static_cast<VkDeviceMemory>(m_buffer_memory), 0, m_index_count, 0, (void**)(&ptr));
		if (result != VK_SUCCESS)
		{
			LOGF_ERROR("Failed to map memory, %s.", vulkan_helper::result_to_string(result));
		}

		return ptr;
	}

	bool RHI_IndexBuffer::Unmap() const
	{
		auto buffer_memory = static_cast<VkDeviceMemory>(m_buffer_memory);

		VkMappedMemoryRange range[1]	= {};
		range[0].sType					= VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
		range[0].memory					= buffer_memory;
		range[0].size					= VK_WHOLE_SIZE;

		auto result = vkFlushMappedMemoryRanges(m_rhi_device->GetContext()->device, 1, range);
		if (result != VK_SUCCESS)
		{
			LOGF_ERROR("Failed to flash mapped memory ranges, %s.", vulkan_helper::result_to_string(result));
			return false;
		}

		vkUnmapMemory(m_rhi_device->GetContext()->device, buffer_memory);
		return true;
	}
}
#endif