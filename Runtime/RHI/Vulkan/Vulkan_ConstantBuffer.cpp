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
#include "../RHI_Implementation.h"
#ifdef API_GRAPHICS_VULKAN
//================================

//= INCLUDES =====================
#include "../RHI_ConstantBuffer.h"
#include "../RHI_Device.h"
#include "../RHI_CommandList.h"
#include "../../Logging/Log.h"
//================================

//= NAMESPACES =====
using namespace std;
//==================

namespace Spartan
{
	RHI_ConstantBuffer::~RHI_ConstantBuffer()
	{
        // Wait in case the buffer is still in use
        RHI_CommandList::Gpu_Flush(m_rhi_device);

		vulkan_common::buffer::destroy(m_rhi_device->GetContextRhi(), m_buffer);
		vulkan_common::memory::free(m_rhi_device->GetContextRhi(), m_buffer_memory);
	}

	bool RHI_ConstantBuffer::_Create()
	{
		if (!m_rhi_device || !m_rhi_device->GetContextRhi()->device)
		{
			LOG_ERROR_INVALID_PARAMETER();
			return false;
		}

        // Wait in case the buffer is still in use
        RHI_CommandList::Gpu_Flush(m_rhi_device);

		// Clear previous buffer
		vulkan_common::buffer::destroy(m_rhi_device->GetContextRhi(), m_buffer);
		vulkan_common::memory::free(m_rhi_device->GetContextRhi(), m_buffer_memory);

		// Create buffer
		if (!vulkan_common::buffer::create(m_rhi_device->GetContextRhi(), m_buffer, m_buffer_memory, m_size, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT))
			return false;

		return true;
	}

    void* RHI_ConstantBuffer::Map(uint32_t offset_index /*= 0*/)
    {
        if (!m_rhi_device || !m_rhi_device->GetContextRhi()->device || !m_buffer_memory)
        {
            LOG_ERROR_INVALID_INTERNALS();
            return nullptr;
        }

        void* ptr = nullptr;
        vulkan_common::error::check
        (
            vkMapMemory
            (
                m_rhi_device->GetContextRhi()->device,
                static_cast<VkDeviceMemory>(m_buffer_memory),
                static_cast<uint64_t>(offset_index * m_stride), // offset
                m_stride,                                       // size
                0,                                              // flags
                reinterpret_cast<void**>(&ptr)
            )
        );

        m_offset_index = offset_index;

        return ptr;
    }

    bool RHI_ConstantBuffer::Unmap() const
    {
        if (!m_rhi_device || !m_rhi_device->GetContextRhi()->device || !m_buffer_memory)
        {
            LOG_ERROR_INVALID_INTERNALS();
            return false;
        }

        vkUnmapMemory(m_rhi_device->GetContextRhi()->device, static_cast<VkDeviceMemory>(m_buffer_memory));

        return true;
    }

    bool RHI_ConstantBuffer::Flush(uint32_t offset_index /*= 0*/)
    {
        VkMappedMemoryRange mapped_memory_range = {};
        mapped_memory_range.sType               = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
        mapped_memory_range.memory              = static_cast<VkDeviceMemory>(m_buffer_memory);
        mapped_memory_range.offset              = static_cast<uint64_t>(offset_index * m_stride);
        mapped_memory_range.size                = m_stride;
        return vulkan_common::error::check(vkFlushMappedMemoryRanges(m_rhi_device->GetContextRhi()->device, 1, &mapped_memory_range));
    }
}
#endif
