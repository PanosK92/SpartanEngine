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
#include "Spartan.h"
#define VMA_IMPLEMENTATION
#include "../RHI_Implementation.h"
#include "Vulkan_Utility.h"
//================================

//= NAMESPACES =====
using namespace std;
//==================

namespace Spartan::vulkan_utility
{
    PFN_vkCreateDebugUtilsMessengerEXT                                    functions::create_messenger                        = nullptr;
    VkDebugUtilsMessengerEXT                                              functions::messenger                               = nullptr;
    PFN_vkDestroyDebugUtilsMessengerEXT                                   functions::destroy_messenger                       = nullptr;
    PFN_vkSetDebugUtilsObjectTagEXT                                       functions::set_object_tag                          = nullptr;
    PFN_vkSetDebugUtilsObjectNameEXT                                      functions::set_object_name                         = nullptr;
    PFN_vkCmdBeginDebugUtilsLabelEXT                                      functions::marker_begin                            = nullptr;
    PFN_vkCmdEndDebugUtilsLabelEXT                                        functions::marker_end                              = nullptr;
    PFN_vkGetPhysicalDeviceMemoryProperties2KHR                           functions::get_physical_device_memory_properties_2 = nullptr;
    mutex                                                                 command_buffer_immediate::m_mutex_begin;
    mutex                                                                 command_buffer_immediate::m_mutex_end;
    unordered_map<RHI_Queue_Type, command_buffer_immediate::cmdbi_object> command_buffer_immediate::m_objects;

    VmaAllocation buffer::create(void*& _buffer, const uint64_t size, VkBufferUsageFlags usage, VkMemoryPropertyFlags memory_property_flags, const void* data)
    {
        VmaAllocator allocator = globals::rhi_context->allocator;

        // Buffer info
        VkBufferCreateInfo buffer_create_info = {};
        buffer_create_info.sType              = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        buffer_create_info.size               = size;
        buffer_create_info.usage              = usage;
        buffer_create_info.sharingMode        = VK_SHARING_MODE_EXCLUSIVE;

        bool used_for_staging = (usage & VK_BUFFER_USAGE_TRANSFER_SRC_BIT) != 0;
        bool is_mappable      = (memory_property_flags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) != 0;

        // Allocation info
        VmaAllocationCreateInfo allocation_create_info = {};
        allocation_create_info.usage                   = VMA_MEMORY_USAGE_AUTO;
        allocation_create_info.flags                   = used_for_staging ? VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT : (is_mappable ? VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT : 0);
        allocation_create_info.preferredFlags          = memory_property_flags;

        // Create the buffer
        VmaAllocation allocation = nullptr;
        VmaAllocationInfo allocation_info;
        if (!error::check(vmaCreateBuffer(allocator, &buffer_create_info, &allocation_create_info, reinterpret_cast<VkBuffer*>(&_buffer), &allocation, &allocation_info)))
            return nullptr;

        // Keep allocation reference
        globals::rhi_context->allocations[reinterpret_cast<uint64_t>(_buffer)] = allocation;

        // If a pointer to the buffer data has been passed, map the buffer and copy over the data
        if (data != nullptr)
        {
            SP_ASSERT(is_mappable && "Can't map, you need to create a buffer, with a VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT memory flag.");

            // Memory in Vulkan doesn't need to be unmapped before using it on GPU, but unless a
            // memory type has VK_MEMORY_PROPERTY_HOST_COHERENT_BIT flag set, you need to manually
            // invalidate cache before reading a mapped pointer and flush cache after writing to
            // it. Map/unmap operations don't do that automatically.

            void* mapped = nullptr;
            if (error::check(vmaMapMemory(allocator, allocation, &mapped)))
            {
                memcpy(mapped, data, size);

                if (!error::check(vmaFlushAllocation(allocator, allocation, 0, size)))
                    return allocation;

                vmaUnmapMemory(allocator, allocation);
            }
        }

        return allocation;
    }

    void buffer::destroy(void*& _buffer)
    {
        if (!_buffer)
            return;

        uint64_t allocation_id = reinterpret_cast<uint64_t>(_buffer);
        auto it = globals::rhi_context->allocations.find(allocation_id);
        if (it != globals::rhi_context->allocations.end())
        {
            VmaAllocation allocation = it->second;
            vmaDestroyBuffer(globals::rhi_context->allocator, static_cast<VkBuffer>(_buffer), allocation);
            globals::rhi_context->allocations.erase(allocation_id);
            _buffer = nullptr;
        }
    }
}
