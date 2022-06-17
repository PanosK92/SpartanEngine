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
#define VMA_IMPLEMENTATION
#include "../RHI_Implementation.h"
#include "Vulkan_Utility.h"
//================================

//= NAMESPACES =====
using namespace std;
//==================

namespace Spartan::vulkan_utility
{
    PFN_vkCreateDebugUtilsMessengerEXT                                    functions::create_messenger = nullptr;
    VkDebugUtilsMessengerEXT                                              functions::messenger = nullptr;
    PFN_vkDestroyDebugUtilsMessengerEXT                                   functions::destroy_messenger = nullptr;
    PFN_vkSetDebugUtilsObjectTagEXT                                       functions::set_object_tag = nullptr;
    PFN_vkSetDebugUtilsObjectNameEXT                                      functions::set_object_name = nullptr;
    PFN_vkCmdBeginDebugUtilsLabelEXT                                      functions::marker_begin = nullptr;
    PFN_vkCmdEndDebugUtilsLabelEXT                                        functions::marker_end = nullptr;
    PFN_vkGetPhysicalDeviceMemoryProperties2KHR                           functions::get_physical_device_memory_properties_2 = nullptr;
    mutex                                                                 command_buffer_immediate::m_mutex_begin;
    mutex                                                                 command_buffer_immediate::m_mutex_end;
    unordered_map<RHI_Queue_Type, command_buffer_immediate::cmdbi_object> command_buffer_immediate::m_objects;
    static mutex mutex_vma_allocation_buffer;
    static mutex mutex_vma_allocation_texture;

    uint64_t get_allocation_id_from_resource(void* resource)
    {
        return reinterpret_cast<uint64_t>(resource);
    }

    VmaAllocation get_allocation_from_resource(void* resource)
    {
        return globals::rhi_context->allocations.find(get_allocation_id_from_resource(resource))->second;
    }

    void vma_allocator::create_buffer(void*& resource, const uint64_t size, VkBufferUsageFlags usage, VkMemoryPropertyFlags memory_property_flags, const void* data_initial)
    {
        // Deduce some memory properties
        bool is_buffer_constant      = (usage & VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT)                  != 0;
        bool is_buffer_index         = (usage & VK_BUFFER_USAGE_INDEX_BUFFER_BIT)                    != 0;
        bool is_buffer_vertex        = (usage & VK_BUFFER_USAGE_VERTEX_BUFFER_BIT)                   != 0;
        bool is_buffer_staging       = (usage & VK_BUFFER_USAGE_TRANSFER_SRC_BIT)                    != 0;
        bool is_mappable             = (memory_property_flags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) != 0;
        bool is_transfer_source      = (usage & VK_BUFFER_USAGE_TRANSFER_SRC_BIT) != 0;
        bool is_transfer_destination = (usage & VK_BUFFER_USAGE_TRANSFER_DST_BIT) != 0;
        bool is_transfer_buffer      = is_transfer_source || is_transfer_destination;
        bool map_on_creation         = is_buffer_constant || is_buffer_index || is_buffer_vertex;

        // Buffer info
        VkBufferCreateInfo buffer_create_info = {};
        buffer_create_info.sType              = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        buffer_create_info.size               = size;
        buffer_create_info.usage              = usage;
        buffer_create_info.sharingMode        = VK_SHARING_MODE_EXCLUSIVE;

        // Allocation info
        VmaAllocationCreateInfo allocation_create_info = {};
        allocation_create_info.usage                   = VMA_MEMORY_USAGE_AUTO;
        allocation_create_info.requiredFlags           = memory_property_flags;
        allocation_create_info.flags                   = 0;

        if (is_buffer_staging)
        {
            allocation_create_info.flags |= VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT;
        }
        else
        {
            // Can it be mapped ? Buffers that use Map()/Unmap() need this, persistent buffers also need this.
            allocation_create_info.flags |= is_mappable ? VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT : 0;

            // Can it be mapped upon creation ? This is what a persistent buffer would use.
            allocation_create_info.flags |= (map_on_creation && !is_transfer_buffer) ? VMA_ALLOCATION_CREATE_MAPPED_BIT : 0;

            // Allocate dedicated memory ? Our constant buffers can re-allocate to accommodate more dynamic offsets, dedicated memory can reduce fragmentation.
            bool big_enough = size >= 1048576; // go dedicated above this threshold
            allocation_create_info.flags |= (is_buffer_constant && big_enough) ? VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT : 0;

            // Cached on the CPU ? Our constant buffers are using dynamic offsets and do a lot of updates, so we need fast access.
            allocation_create_info.flags |= is_buffer_constant ? VK_MEMORY_PROPERTY_HOST_CACHED_BIT : 0;
        }

        // Create the buffer
        VmaAllocator allocator   = globals::rhi_context->allocator;
        VmaAllocation allocation = nullptr;
        VmaAllocationInfo allocation_info;
        SP_ASSERT(
            error::check(vmaCreateBuffer(
                allocator,
                &buffer_create_info,
                &allocation_create_info,
                reinterpret_cast<VkBuffer*>(&resource),
                &allocation,
                &allocation_info))
        && "Failed to created buffer");

        // If a pointer to the buffer data has been passed, map the buffer and copy over the data
        if (data_initial != nullptr)
        {
            SP_ASSERT(is_mappable && "Can't map, you need to create a buffer, with a VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT memory flag.");

            // Memory in Vulkan doesn't need to be unmapped before using it on GPU, but unless a
            // memory type has VK_MEMORY_PROPERTY_HOST_COHERENT_BIT flag set, you need to manually
            // invalidate the cache before reading a mapped pointer and flush cache after writing to
            // it. Map/unmap operations don't do that automatically.

            void* mapped_data = nullptr;
            SP_ASSERT(error::check(vmaMapMemory(allocator, allocation, &mapped_data)) && "Failed to map allocation");
            memcpy(mapped_data, data_initial, size);
            SP_ASSERT(error::check(vmaFlushAllocation(allocator, allocation, 0, size)) && "Failed to flush allocation");
            vmaUnmapMemory(allocator, allocation);
        }

        // Allocations can come both from the main as well as worker threads (loading), so lock this context.
        lock_guard<mutex> lock(mutex_vma_allocation_buffer);

        // Keep allocation reference
        globals::rhi_context->allocations[reinterpret_cast<uint64_t>(resource)] = allocation;
    }

    void vma_allocator::destroy_buffer(void*& resource)
    {
        if (!resource)
            return;

        // Deallocations can come both from the main as well as worker threads, so lock this context.
        lock_guard<mutex> lock(mutex_vma_allocation_buffer);

        if (VmaAllocation allocation = get_allocation_from_resource(resource))
        {
            vmaDestroyBuffer(globals::rhi_context->allocator, static_cast<VkBuffer>(resource), allocation);

            globals::rhi_context->allocations.erase(get_allocation_id_from_resource(resource));
            resource = nullptr;
        }
    }

    void* vma_allocator::get_mapped_data_from_buffer(void* resource)
    {
        if (VmaAllocation allocation = get_allocation_from_resource(resource))
        {
            return allocation->GetMappedData();
        }

        return nullptr;
    }

    void vma_allocator::create_texture(const VkImageCreateInfo& create_info, void*& resource)
    {
        VmaAllocationCreateInfo allocation_info = {};
        allocation_info.usage                   = VMA_MEMORY_USAGE_AUTO;

        // Create image
        VmaAllocation allocation;
        SP_ASSERT(
                vulkan_utility::error::check(
                vmaCreateImage(
                    vulkan_utility::globals::rhi_context->allocator,
                    &create_info, &allocation_info,
                    reinterpret_cast<VkImage*>(&resource),
                    &allocation,
                    nullptr))
        && "Failed to allocate texture");

        // Allocations can come both from the main as well as worker threads (loading), so lock this context.
        lock_guard<mutex> lock(mutex_vma_allocation_texture);

        // Keep allocation reference
        globals::rhi_context->allocations[reinterpret_cast<uint64_t>(resource)] = allocation;
    }

    void vma_allocator::destroy_texture(void*& resource)
    {
        if (!resource)
            return;

        // Deallocations can come both from the main as well as worker threads, so lock this context.
        lock_guard<mutex> lock(mutex_vma_allocation_buffer);

        if (VmaAllocation allocation = get_allocation_from_resource(resource))
        {
            vmaDestroyImage(globals::rhi_context->allocator, static_cast<VkImage>(resource), allocation);

            globals::rhi_context->allocations.erase(get_allocation_id_from_resource(resource));
            resource = nullptr;
        }
    }

    void vma_allocator::map(void* resource, void*& mapped_data)
    {
        if (VmaAllocation allocation = get_allocation_from_resource(resource))
        {
            SP_ASSERT(
                vulkan_utility::error::check(
                    vmaMapMemory(globals::rhi_context->allocator, allocation, reinterpret_cast<void**>(&mapped_data)))
            && "Failed to map memory");
        }
    }

    void vma_allocator::unmap(void* resource, void*& mapped_data)
    {
        SP_ASSERT(mapped_data && "Memory is already unmapped");

        if (VmaAllocation allocation = get_allocation_from_resource(resource))
        {
            vmaUnmapMemory(globals::rhi_context->allocator, static_cast<VmaAllocation>(allocation));
            mapped_data = nullptr;
        }
    }

    void vma_allocator::flush(void* resource, uint64_t offset, uint64_t size)
    {
        if (VmaAllocation allocation = get_allocation_from_resource(resource))
        {
            SP_ASSERT(
                vulkan_utility::error::check(
                    vmaFlushAllocation(globals::rhi_context->allocator, allocation, offset, size))
                && "Failed to flush");
        }
    }
}
