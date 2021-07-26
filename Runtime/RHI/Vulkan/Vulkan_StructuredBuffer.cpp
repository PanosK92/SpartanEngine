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

//= INCLUDES =======================
#include "Spartan.h"
#include "../RHI_Implementation.h"
#include "../RHI_StructuredBuffer.h"
//==================================

//= NAMESPACES =====
using namespace std;
//==================

namespace Spartan
{
    RHI_StructuredBuffer::RHI_StructuredBuffer(const shared_ptr<RHI_Device>& rhi_device, const uint32_t stride, const uint32_t element_count, const void* data /*= nullptr*/)
    {
        m_rhi_device        = rhi_device;
        m_stride            = stride;
        m_element_count     = element_count;
        m_object_size_gpu   = stride * element_count;

        // Create buffer
        {
            VkMemoryPropertyFlags flags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT;
            bool written_frequently = false;

            VmaAllocation allocation = vulkan_utility::buffer::create(m_resource, m_object_size_gpu, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, flags, written_frequently, data);
            if (!allocation)
                return;

            // Set debug name
            vulkan_utility::debug::set_name(static_cast<VkBuffer>(m_resource), "structured_buffer");
        }
    }

    RHI_StructuredBuffer::~RHI_StructuredBuffer()
    {
        // Wait in case it's still in use by the GPU
        m_rhi_device->Queue_WaitAll();

        // Destroy buffer
        vulkan_utility::buffer::destroy(m_resource);
    }

    void* RHI_StructuredBuffer::Map()
    {
        SP_ASSERT(m_rhi_device != nullptr);
        SP_ASSERT(m_resource != nullptr);

        LOG_ERROR("Not implemented");

        return nullptr;
    }

    void RHI_StructuredBuffer::Unmap()
    {
        SP_ASSERT(m_rhi_device != nullptr);
        SP_ASSERT(m_resource != nullptr);

        LOG_ERROR("Not implemented");
    }
}
