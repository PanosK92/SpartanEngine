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
#include "../RHI_Buffer.h"
#include "../RHI_Implementation.h"
#include "../RHI_Device.h"
#include "../RHI_CommandList.h"
//================================

//= NAMESPACES =====
using namespace std;
//==================

namespace Spartan
{
    void RHI_Buffer::RHI_DestroyResource()
    {
        if (m_rhi_resource)
        {
            RHI_Device::DeletionQueueAdd(RHI_Resource_Type::Buffer, m_rhi_resource);
            m_rhi_resource = nullptr;
        }
    }

    void RHI_Buffer::RHI_CreateResource(const void* indices)
    {
        RHI_DestroyResource();

        if (m_type != RHI_Buffer_Type::Storage)
        {
            bool vertex                = m_type == RHI_Buffer_Type::Vertex || m_type == RHI_Buffer_Type::Instance;
            VkBufferUsageFlagBits type = vertex ? VK_BUFFER_USAGE_VERTEX_BUFFER_BIT : VK_BUFFER_USAGE_INDEX_BUFFER_BIT;

            if (m_is_mappable)
            {
                uint32_t flags_memory = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT; // mappable and no need to flush
                RHI_Device::MemoryBufferCreate(m_rhi_resource, m_object_size, type, flags_memory, nullptr, m_object_name.c_str());
            }
            else
            {
                // create staging buffer, it's slower but we can copy data in and out of it
                void* staging_buffer = nullptr;
                RHI_Device::MemoryBufferCreate(staging_buffer, m_object_size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, indices, m_object_name.c_str());

                // create destination buffer, it's faster but we can only copy data into it
                RHI_Device::MemoryBufferCreate(m_rhi_resource, m_object_size, VK_BUFFER_USAGE_TRANSFER_DST_BIT | type, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, nullptr, m_object_name.c_str());

                // copy staging buffer to destination buffer
                VkBuffer* buffer_vk         = reinterpret_cast<VkBuffer*>(&m_rhi_resource);
                VkBuffer* buffer_staging_vk = reinterpret_cast<VkBuffer*>(&staging_buffer);
                VkBufferCopy copy_region    = {};
                copy_region.size            = m_object_size;
                RHI_CommandList* cmd_list   = RHI_Device::CmdImmediateBegin(RHI_Queue_Type::Copy);
                vkCmdCopyBuffer(static_cast<VkCommandBuffer>(cmd_list->GetRhiResource()), *buffer_staging_vk, *buffer_vk, 1, &copy_region);
                RHI_Device::CmdImmediateSubmit(cmd_list);
                RHI_Device::MemoryBufferDestroy(staging_buffer);
            }
        }
        else // storage
        {
            // calculate required alignment based on minimum device offset alignment
            size_t min_alignment = RHI_Device::PropertyGetMinStorageBufferOffsetAllignment();
            if (min_alignment > 0 && min_alignment != m_stride)
            {
                m_stride      = static_cast<uint32_t>(static_cast<uint64_t>((m_stride + min_alignment - 1) & ~(min_alignment - 1)));
                m_object_size = m_stride * m_element_count;
            }

            // create
            VkBufferUsageFlags flags_usage     = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
            VkMemoryPropertyFlags flags_memory = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT; // mappable and no need to flush
            RHI_Device::MemoryBufferCreate(m_rhi_resource, m_object_size, flags_usage, flags_memory, nullptr, m_object_name.c_str());
        }

        if (m_is_mappable)
        {
            m_data = RHI_Device::MemoryGetMappedDataFromBuffer(m_rhi_resource);
        }

        RHI_Device::SetResourceName(m_rhi_resource, RHI_Resource_Type::Buffer, m_object_name);
    }

    void RHI_Buffer::Update(void* data_cpu, const uint32_t size)
    {
        SP_ASSERT_MSG(m_data != nullptr,                    "Invalid mapped data");
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

        // persistently mapped so no need to map/unmap/flush
        uint32_t size_final = size != 0 ? size : m_stride;
        memcpy(reinterpret_cast<std::byte*>(m_data) + m_offset, reinterpret_cast<std::byte*>(data_cpu), size);
    }
}
