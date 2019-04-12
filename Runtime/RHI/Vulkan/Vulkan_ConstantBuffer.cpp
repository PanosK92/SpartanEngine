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
#include "../RHI_ConstantBuffer.h"
#include "../RHI_Device.h"
#include "../../Logging/Log.h"
#include "Vulkan_Helper.h"
//================================

//= NAMESPACES =====
using namespace std;
//==================

namespace Spartan
{
	RHI_ConstantBuffer::RHI_ConstantBuffer(const shared_ptr<RHI_Device>& rhi_device, const uint64_t size)
	{
		m_rhi_device	= rhi_device;
		m_size			= size;

		if (!m_rhi_device || !m_rhi_device->GetContext()->device)
		{
			LOG_ERROR_INVALID_PARAMETER();
			return;
		}

		auto buffer			= static_cast<VkBuffer>(m_buffer);
		auto buffer_memory	= static_cast<VkDeviceMemory>(m_buffer_memory);

		VkBufferCreateInfo bufferInfo	= {};
		bufferInfo.sType				= VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
		bufferInfo.size					= size;
		bufferInfo.usage				= VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
		bufferInfo.sharingMode			= VK_SHARING_MODE_EXCLUSIVE;

		if (vkCreateBuffer(m_rhi_device->GetContext()->device, &bufferInfo, nullptr, &buffer) != VK_SUCCESS)
		{
			LOG_ERROR("Failed to create buffer");
			return;
		}

		VkMemoryRequirements memRequirements;
		vkGetBufferMemoryRequirements(m_rhi_device->GetContext()->device, buffer, &memRequirements);

		VkMemoryAllocateInfo allocInfo	= {};
		allocInfo.sType					= VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
		allocInfo.allocationSize		= memRequirements.size;
		allocInfo.memoryTypeIndex		= vulkan_helper::GetMemoryType(m_rhi_device->GetContext()->device_physical, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, memRequirements.memoryTypeBits);

		if (vkAllocateMemory(m_rhi_device->GetContext()->device, &allocInfo, nullptr, &buffer_memory) != VK_SUCCESS)
		{
			LOG_ERROR("Failed to allocate memory");
			return;
		}

		vkBindBufferMemory(m_rhi_device->GetContext()->device, buffer, buffer_memory, 0);

		m_buffer		= static_cast<void*>(buffer);
		m_buffer_memory	= static_cast<void*>(buffer_memory);
	}

	RHI_ConstantBuffer::~RHI_ConstantBuffer()
	{
		if (m_buffer != VK_NULL_HANDLE)
		{
			vkDestroyBuffer(m_rhi_device->GetContext()->device, static_cast<VkBuffer>(m_buffer), nullptr);
			m_buffer = nullptr;
		}

		if (m_buffer_memory)
		{
			vkFreeMemory(m_rhi_device->GetContext()->device, static_cast<VkDeviceMemory>(m_buffer_memory), nullptr);
			m_buffer_memory = nullptr;
		}
	}

	void* RHI_ConstantBuffer::Map() const
	{
		return nullptr;
	}

	bool RHI_ConstantBuffer::Unmap() const
	{
		return false;
	}
}
#endif