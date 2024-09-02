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

//= INCLUDES =======================
#include "pch.h"
#include "../RHI_Device.h"
#include "../RHI_Implementation.h"
#include "../RHI_Buffer.h"
//==================================

//= NAMESPACES =====
using namespace std;
//==================

namespace Spartan
{
    RHI_Buffer::RHI_Buffer(const uint32_t stride, const uint32_t element_count, const uint32_t usage, const char* name)
    {
        m_object_name   = name;
        m_stride        = stride;
        m_element_count = element_count;
        m_usage         = usage;
        m_object_size   = stride * element_count;

        // Calculate required alignment based on minimum device offset alignment
        size_t min_alignment = RHI_Device::PropertyGetMinStorageBufferOffsetAllignment();
        if (min_alignment > 0)
        {
            m_stride = static_cast<uint32_t>(static_cast<uint64_t>((m_stride + min_alignment - 1) & ~(min_alignment - 1)));
        }
        m_object_size = m_stride * m_element_count;

        // determine Vulkan buffer usage flags
        VkBufferUsageFlags vk_usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
        if (m_usage & RHI_Buffer_Transfer_Src)
            vk_usage |= VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
        if (m_usage & RHI_Buffer_Transfer_Dst)
            vk_usage |= VK_BUFFER_USAGE_TRANSFER_DST_BIT;

        SP_ASSERT(vk_usage != 0);

        // create buffer
        VkMemoryPropertyFlags memory_flags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT; // mappable and no need to flush
        RHI_Device::MemoryBufferCreate(m_rhi_resource, m_object_size, vk_usage, memory_flags, nullptr, name);
        RHI_Device::SetResourceName(m_rhi_resource, RHI_Resource_Type::Buffer, name);

        // get mapped data pointer
        m_mapped_data = RHI_Device::MemoryGetMappedDataFromBuffer(m_rhi_resource);
    }

    RHI_Buffer::~RHI_Buffer()
    {
        RHI_Device::DeletionQueueAdd(RHI_Resource_Type::Buffer, m_rhi_resource);
        m_rhi_resource = nullptr;
    }

    void RHI_Buffer::Update(void* data_cpu, const uint32_t update_size)
    {
        SP_ASSERT_MSG(data_cpu != nullptr, "Invalid update data");
        SP_ASSERT_MSG(m_mapped_data != nullptr, "Invalid mapped data");
        SP_ASSERT_MSG(m_offset + m_stride <= m_object_size, "Out of memory");

        // advance offset
        if (first_update)
        {
            first_update = false;
        }
        else
        {
            m_offset += m_stride;
        }

        uint32_t size = update_size != 0 ? update_size : m_stride;

        // we are using persistent mapping, so we only copy (no need for map/unmap)
        memcpy(reinterpret_cast<std::byte*>(m_mapped_data) + m_offset, reinterpret_cast<std::byte*>(data_cpu), size);
    }
}
