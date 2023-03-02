/*
Copyright(c) 2016-2023 Panos Karabelas

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

//= INCLUDES ========================
#include "pch.h"
#include "../RHI_Implementation.h"
#include "../RHI_ConstantBuffer.h"
#include "../RHI_Device.h"
#include "../RHI_CommandList.h"
#include "../../Rendering/Renderer.h"
//===================================

//= NAMESPACES =====
using namespace std;
//==================

namespace Spartan
{
    void RHI_ConstantBuffer::_destroy()
    {
        // Wait
        m_rhi_device->QueueWaitAll();

        // Destroy
        m_rhi_device->DestroyBuffer(m_rhi_resource);
    }

    RHI_ConstantBuffer::RHI_ConstantBuffer(RHI_Device* rhi_device, const string& name)
    {
        m_rhi_device = rhi_device;
        m_name       = name;
    }

    void RHI_ConstantBuffer::_create()
    {
        // Destroy previous buffer
        _destroy();

        // Calculate required alignment based on minimum device offset alignment
        size_t min_alignment = m_rhi_device->GetMinUniformBufferOffsetAllignment();
        if (min_alignment > 0)
        {
            m_stride = static_cast<uint64_t>((m_stride + min_alignment - 1) & ~(min_alignment - 1));
        }
        m_object_size_gpu = m_stride * m_element_count;

        // Define memory properties
        VkMemoryPropertyFlags flags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT; // mappable

        // Create buffer
        m_rhi_device->CreateBuffer(m_rhi_resource, m_object_size_gpu, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, flags);

        // Get mapped data pointer
        m_mapped_data = m_rhi_device->get_mapped_data_from_buffer(m_rhi_resource);

        // Set debug name
        vulkan_utility::debug::set_object_name(static_cast<VkBuffer>(m_rhi_resource), (m_name + string("_size_") + to_string(m_object_size_gpu)).c_str());
    }

    void RHI_ConstantBuffer::Update(void* data_cpu)
    {
        SP_ASSERT_MSG(data_cpu != nullptr,                      "Invalid update data");
        SP_ASSERT_MSG(m_mapped_data != nullptr,                 "Invalid mapped data");
        SP_ASSERT_MSG(m_offset + m_stride <= m_object_size_gpu, "Out of memory");

        // Advance offset
        m_offset += m_stride;
        if (m_reset_offset)
        {
            m_offset       = 0;
            m_reset_offset = false;
        }

        // Vulkan is using persistent mapping, so we only need to copy and flush
        memcpy(reinterpret_cast<std::byte*>(m_mapped_data) + m_offset, reinterpret_cast<std::byte*>(data_cpu), m_stride);
        m_rhi_device->FlushAllocation(m_rhi_resource, m_offset, m_stride);
    }
}
