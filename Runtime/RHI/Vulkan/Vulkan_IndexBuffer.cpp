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

//= INCLUDES =====================
#include "Runtime/Core/Spartan.h"
#include "../RHI_Implementation.h"
#include "../RHI_Device.h"
#include "../RHI_IndexBuffer.h"
#include "../RHI_CommandList.h"
//================================

//= NAMESPACES =====
using namespace std;
//==================

namespace Spartan
{
    void RHI_IndexBuffer::_destroy()
    {
        // Wait
        m_rhi_device->QueueWaitAll();

        // Destroy
        vulkan_utility::vma_allocator::destroy_buffer(m_resource);
    }

    bool RHI_IndexBuffer::_create(const void* indices)
    {
        // Destroy previous buffer
        _destroy();

        m_is_mappable = indices == nullptr;

        if (m_is_mappable)
        {
            // Define memory properties
            VkMemoryPropertyFlags flags  = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT; // mappable

            // Create
            vulkan_utility::vma_allocator::create_buffer(m_resource, m_object_size_gpu, VK_BUFFER_USAGE_INDEX_BUFFER_BIT, flags, nullptr);

            // Get mapped data pointer
            m_mapped_data = vulkan_utility::vma_allocator::get_mapped_data_from_buffer(m_resource);
        }
        else // The reason we use staging is because memory with VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT is not mappable but it's fast, we want that.
        {
            // Create staging/source buffer and copy the indices to it
            void* staging_buffer = nullptr;
            vulkan_utility::vma_allocator::create_buffer(staging_buffer, m_object_size_gpu, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, indices);

            // Create destination buffer
            vulkan_utility::vma_allocator::create_buffer(m_resource, m_object_size_gpu, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, nullptr);

            // Copy staging buffer to destination buffer
            {
                // Create command buffer
                VkCommandBuffer cmd_buffer = vulkan_utility::command_buffer_immediate::begin(RHI_Queue_Type::Copy);

                VkBuffer* buffer_vk         = reinterpret_cast<VkBuffer*>(&m_resource);
                VkBuffer* buffer_staging_vk = reinterpret_cast<VkBuffer*>(&staging_buffer);

                // Copy
                VkBufferCopy copy_region = {};
                copy_region.size = m_object_size_gpu;
                vkCmdCopyBuffer(cmd_buffer, *buffer_staging_vk, *buffer_vk, 1, &copy_region);

                // Flush and free command buffer
                vulkan_utility::command_buffer_immediate::end(RHI_Queue_Type::Copy);

                // Destroy staging buffer
                vulkan_utility::vma_allocator::destroy_buffer(staging_buffer);
            }
        }

        // Set debug name
        vulkan_utility::debug::set_name(static_cast<VkBuffer>(m_resource), m_object_name.c_str());

        return true;
    }

    void* RHI_IndexBuffer::Map()
    {
        return m_mapped_data;
    }

    void RHI_IndexBuffer::Unmap()
    {
        // buffer is mapped on creation and unmapped during destruction
    }
}
