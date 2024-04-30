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
#include "../RHI_Device.h"
#include "../RHI_VertexBuffer.h"
#include "../RHI_CommandList.h"
//================================

//= NAMESPACES =====
using namespace std;
//==================

namespace Spartan
{
    RHI_VertexBuffer::~RHI_VertexBuffer()
    {
        if (m_rhi_resource)
        {
            RHI_Device::DeletionQueueAdd(RHI_Resource_Type::Buffer, m_rhi_resource);
            m_rhi_resource = nullptr;
        }
    }

    void RHI_VertexBuffer::_create(const void* vertices)
    {
        // destroy previous buffer
        if (m_rhi_resource)
        {
            RHI_Device::DeletionQueueAdd(RHI_Resource_Type::Buffer, m_rhi_resource);
            m_rhi_resource = nullptr;
        }

        m_is_mappable = vertices == nullptr;

        if (m_is_mappable)
        {
            // define memory properties
            uint32_t flags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT; // mappable

            // created
            RHI_Device::MemoryBufferCreate(m_rhi_resource, m_object_size, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, flags, nullptr, m_object_name.c_str());

            // get mapped data pointer
            m_mapped_data = RHI_Device::MemoryGetMappedDataFromBuffer(m_rhi_resource);
        }
        else // the reason we use staging is because memory with VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, the buffer is not mappable but it's fast, we want that.
        {
            // create staging/source buffer and copy the vertices to it
            void* staging_buffer = nullptr;
            RHI_Device::MemoryBufferCreate(staging_buffer, m_object_size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, vertices, m_object_name.c_str());

            // create destination buffer
            RHI_Device::MemoryBufferCreate(m_rhi_resource, m_object_size, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, nullptr, m_object_name.c_str());

            // copy staging buffer to destination buffer
            {
                // create command buffer
                RHI_CommandList* cmd_list = RHI_Device::CmdImmediateBegin(RHI_Queue_Type::Copy);

                VkBuffer* buffer_vk         = reinterpret_cast<VkBuffer*>(&m_rhi_resource);
                VkBuffer* buffer_staging_vk = reinterpret_cast<VkBuffer*>(&staging_buffer);

                // copy
                VkBufferCopy copy_region = {};
                copy_region.size         = m_object_size;
                vkCmdCopyBuffer(static_cast<VkCommandBuffer>(cmd_list->GetRhiResource()), *buffer_staging_vk, *buffer_vk, 1, &copy_region);

                // flush and free command buffer
                RHI_Device::CmdImmediateSubmit(cmd_list);

                // destroy staging resources
                RHI_Device::MemoryBufferDestroy(staging_buffer);
            }
        }

        // set debug name
        RHI_Device::SetResourceName(m_rhi_resource, RHI_Resource_Type::Buffer, m_object_name);
    }
}
