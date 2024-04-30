/*
Copyright(c) 2016-2024 Panos Karabelas

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

//= INCLUDES =====================
#include "pch.h"
#include "../RHI_Implementation.h"
#include "../RHI_ConstantBuffer.h"
#include "../RHI_Device.h"
//================================

//= NAMESPACES =====
using namespace std;
//==================

namespace Spartan
{
    RHI_ConstantBuffer::RHI_ConstantBuffer(const string& name)
    {
        m_object_name = name;
    }

    RHI_ConstantBuffer::~RHI_ConstantBuffer()
    {
        if (m_rhi_resource)
        {
            RHI_Device::DeletionQueueAdd(RHI_Resource_Type::Buffer, m_rhi_resource);
            m_rhi_resource = nullptr;
        }
    }

    void RHI_ConstantBuffer::RHI_CreateResource()
    {
        // destroy previous buffer
        if (m_rhi_resource)
        {
            RHI_Device::DeletionQueueAdd(RHI_Resource_Type::Buffer, m_rhi_resource);
            m_rhi_resource = nullptr;
        }

        // calculate required alignment based on minimum device offset alignment
        size_t min_alignment = RHI_Device::PropertyGetMinUniformBufferOffsetAllignment();
        if (min_alignment > 0)
        {
            m_stride = static_cast<uint32_t>(static_cast<uint64_t>((m_stride + min_alignment - 1) & ~(min_alignment - 1)));
        }
        m_object_size = m_stride * m_element_count;

        // define memory properties
        VkMemoryPropertyFlags flags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT; // mappable

        // create buffer
        RHI_Device::MemoryBufferCreate(m_rhi_resource, m_object_size, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, flags, nullptr, m_object_name.c_str());

        // get mapped data pointer
        m_mapped_data = RHI_Device::MemoryGetMappedDataFromBuffer(m_rhi_resource);

        // set debug name
        RHI_Device::SetResourceName(m_rhi_resource, RHI_Resource_Type::Buffer, m_object_name);
    }

    void RHI_ConstantBuffer::Update(void* data_cpu)
    {
        SP_ASSERT_MSG(data_cpu != nullptr,                  "Invalid update data");
        SP_ASSERT_MSG(m_mapped_data != nullptr,             "Invalid mapped data");
        SP_ASSERT_MSG(m_offset + m_stride <= m_object_size, "Out of memory");

        // advance offset
        if (m_has_updated)
        {
            m_offset += m_stride;
        }

        // we are using persistent mapping, so we can only copy
        memcpy(reinterpret_cast<std::byte*>(m_mapped_data) + m_offset, reinterpret_cast<std::byte*>(data_cpu), m_stride);

        m_has_updated = true;
    }
}
