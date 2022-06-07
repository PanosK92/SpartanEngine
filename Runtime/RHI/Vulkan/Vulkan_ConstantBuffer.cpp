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

//= INCLUDES ========================
#include "Spartan.h"
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
        vulkan_utility::vma_allocator::destroy_buffer(m_resource);
    }

    RHI_ConstantBuffer::RHI_ConstantBuffer(const std::shared_ptr<RHI_Device>& rhi_device, const string& name)
    {
        m_rhi_device         = rhi_device;
        m_object_name        = name;
        m_persistent_mapping = true;
    }

    bool RHI_ConstantBuffer::_create()
    {
        // Destroy previous buffer
        _destroy();

        // Calculate required alignment based on minimum device offset alignment
        size_t min_ubo_alignment = m_rhi_device->GetMinUniformBufferOffsetAllignment();
        if (min_ubo_alignment > 0)
        {
            m_stride = static_cast<uint64_t>((m_stride + min_ubo_alignment - 1) & ~(min_ubo_alignment - 1));
        }
        m_object_size_gpu = m_stride * m_element_count;

        // Define memory properties
        VkMemoryPropertyFlags flags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT; // mappable

        // Create buffer
        vulkan_utility::vma_allocator::create_buffer(m_resource, m_object_size_gpu, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, flags);

        // Get mapped data pointer
        m_mapped_data = vulkan_utility::vma_allocator::get_mapped_data_from_buffer(m_resource);

        // Set debug name
        vulkan_utility::debug::set_name(static_cast<VkBuffer>(m_resource), (m_object_name + string("_size_") + to_string(m_object_size_gpu)).c_str());

        return true;
    }

    void* RHI_ConstantBuffer::Map()
    {
        return m_mapped_data;
    }

    void RHI_ConstantBuffer::Unmap()
    {
        // buffer is mapped on creation and unmapped during destruction
    }

    void RHI_ConstantBuffer::Flush(const uint64_t size, const uint64_t offset)
    {
        vulkan_utility::vma_allocator::flush(m_resource, offset, size);

        m_offset       = static_cast<uint32_t>(offset);
        m_reset_offset = false;
    }
}
