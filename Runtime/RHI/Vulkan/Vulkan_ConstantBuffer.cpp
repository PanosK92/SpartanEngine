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
    RHI_ConstantBuffer::RHI_ConstantBuffer(const std::shared_ptr<RHI_Device>& rhi_device, const string& name, bool is_dynamic /*= false*/)
    {
        m_rhi_device    = rhi_device;
        m_name          = name;
        m_is_dynamic    = is_dynamic;
    }

	RHI_ConstantBuffer::~RHI_ConstantBuffer()
	{
        // Wait in case the buffer is still in use
        m_rhi_device->Queue_WaitAll();

		vulkan_utility::buffer::destroy(m_buffer);
	}

	bool RHI_ConstantBuffer::_Create()
	{
		if (!m_rhi_device || !m_rhi_device->GetContextRhi()->device)
		{
			LOG_ERROR_INVALID_PARAMETER();
			return false;
		}

        // Wait in case the buffer is still in use
        if (m_buffer)
        {
            m_rhi_device->Queue_WaitAll();
        }

		// Clear previous buffer
		vulkan_utility::buffer::destroy(m_buffer);

        // Calculate required alignment based on minimum device offset alignment
        size_t min_ubo_alignment = m_rhi_device->GetContextRhi()->device_properties.limits.minUniformBufferOffsetAlignment;
        if (min_ubo_alignment > 0)
        {
            m_stride = static_cast<uint32_t>((m_stride + min_ubo_alignment - 1) & ~(min_ubo_alignment - 1));
        }
        m_size_gpu = m_offset_count * m_stride;

		// Create buffer
        VmaAllocation allocation = vulkan_utility::buffer::create(m_buffer, m_size_gpu, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, true);
        if (!allocation)
            return false;

        m_allocation = static_cast<void*>(allocation);

        // Set debug name
        vulkan_utility::debug::set_buffer_name(static_cast<VkBuffer>(m_buffer), "constant_buffer");

		return true;
	}

    void* RHI_ConstantBuffer::Map() const
    {
        if (!m_rhi_device || !m_rhi_device->GetContextRhi()->device || !m_allocation)
        {
            LOG_ERROR_INVALID_INTERNALS();
            return nullptr;
        }

        void* ptr = nullptr;

        vulkan_utility::error::check(vmaMapMemory(m_rhi_device->GetContextRhi()->allocator, static_cast<VmaAllocation>(m_allocation), reinterpret_cast<void**>(&ptr)));

        return ptr;
    }

    bool RHI_ConstantBuffer::Unmap() const
    {
        if (!m_rhi_device || !m_rhi_device->GetContextRhi()->device || !m_allocation)
        {
            LOG_ERROR_INVALID_INTERNALS();
            return false;
        }

        vmaUnmapMemory(m_rhi_device->GetContextRhi()->allocator, static_cast<VmaAllocation>(m_allocation));

        return true;
    }
}
#endif
