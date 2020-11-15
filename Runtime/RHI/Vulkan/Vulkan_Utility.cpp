/*
Copyright(c) 2016-2020 Panos Karabelas

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
    PFN_vkCreateDebugUtilsMessengerEXT                                      functions::create_messenger                         = nullptr;
    VkDebugUtilsMessengerEXT                                                functions::messenger                                = nullptr;
    PFN_vkDestroyDebugUtilsMessengerEXT                                     functions::destroy_messenger                        = nullptr;
    PFN_vkSetDebugUtilsObjectTagEXT                                         functions::set_object_tag                           = nullptr;
    PFN_vkSetDebugUtilsObjectNameEXT                                        functions::set_object_name                          = nullptr;
    PFN_vkCmdBeginDebugUtilsLabelEXT                                        functions::marker_begin                             = nullptr;
    PFN_vkCmdEndDebugUtilsLabelEXT                                          functions::marker_end                               = nullptr;
    PFN_vkGetPhysicalDeviceMemoryProperties2KHR                             functions::get_physical_device_memory_properties_2  = nullptr;
    mutex                                                                   command_buffer_immediate::m_mutex_begin;
    mutex                                                                   command_buffer_immediate::m_mutex_end;
    unordered_map<RHI_Queue_Type, command_buffer_immediate::cmdbi_object>   command_buffer_immediate::m_objects;

    bool image::create(RHI_Texture* texture)
    {
        // Get format support
        RHI_Format format                   = texture->GetFormat();
        bool is_render_target_depth_stencil = texture->IsDepthStencil();
        VkFormatFeatureFlags format_flags   = is_render_target_depth_stencil ? VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT : VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT;
        VkImageTiling image_tiling          = get_format_tiling(format, format_flags);
        
        // Ensure the format is supported by the GPU
        if (image_tiling == VK_IMAGE_TILING_MAX_ENUM)
        {
            LOG_ERROR("GPU does not support the usage of %s as a %s.", rhi_format_to_string(format), is_render_target_depth_stencil ? "depth-stencil attachment" : "color attachment");
            return false;
        }
        
        // Reject any image which has a non-optimal format
        if (image_tiling != VK_IMAGE_TILING_OPTIMAL)
        {
            LOG_ERROR("Format %s does not support optimal tiling, considering switching to a more efficient format.", rhi_format_to_string(format));
            return false;
        }
        
        // Set layout to preinitialised (required by Vulkan)
        texture->SetLayout(RHI_Image_Layout::Preinitialized);
        
        VkImageCreateInfo create_info   = {};
        create_info.sType               = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        create_info.imageType           = VK_IMAGE_TYPE_2D;
        create_info.flags               = (texture->GetResourceType() == ResourceType::TextureCube) ? VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT : 0;
        create_info.extent.width        = texture->GetWidth();
        create_info.extent.height       = texture->GetHeight();
        create_info.extent.depth        = 1;
        create_info.mipLevels           = texture->GetMipCount();
        create_info.arrayLayers         = texture->GetArraySize();
        create_info.format              = vulkan_format[format];
        create_info.tiling              = VK_IMAGE_TILING_OPTIMAL;
        create_info.initialLayout       = vulkan_image_layout[static_cast<uint8_t>(texture->GetLayout())];
        create_info.usage               = get_usage_flags(texture);
        create_info.samples             = VK_SAMPLE_COUNT_1_BIT;
        create_info.sharingMode         = VK_SHARING_MODE_EXCLUSIVE;

        VmaAllocationCreateInfo allocation_info = {};
        allocation_info.usage                   = VMA_MEMORY_USAGE_GPU_ONLY;

        // Create image, allocate memory and bind memory to image
        VmaAllocation allocation;
        void* resource = nullptr;
        if (!error::check(vmaCreateImage(globals::rhi_context->allocator, &create_info, &allocation_info, reinterpret_cast<VkImage*>(&resource), &allocation, nullptr)))
            return false;

        texture->Set_Resource(resource);

        // Keep allocation reference
        globals::rhi_context->allocations[texture->GetId()] = allocation;

        return true;
    }

    void image::destroy(RHI_Texture* texture)
    {
        void* resource          = texture->Get_Resource();
        uint64_t allocation_id  = texture->GetId();

        auto it = globals::rhi_context->allocations.find(allocation_id);
        if (it != globals::rhi_context->allocations.end())
        {
            VmaAllocation allocation = it->second;
            vmaDestroyImage(globals::rhi_context->allocator, static_cast<VkImage>(resource), allocation);
            globals::rhi_context->allocations.erase(allocation_id);
            texture->Set_Resource(nullptr);
        }
    }

    VmaAllocation buffer::create(void*& _buffer, const uint64_t size, VkBufferUsageFlags usage, VkMemoryPropertyFlags memory_property_flags, const bool written_frequently /*= false*/, const void* data /*= nullptr*/)
    {
        VmaAllocator allocator = globals::rhi_context->allocator;

        VkBufferCreateInfo buffer_create_info   = {};
        buffer_create_info.sType                = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        buffer_create_info.size                 = size;
        buffer_create_info.usage                = usage;
        buffer_create_info.sharingMode          = VK_SHARING_MODE_EXCLUSIVE;

        bool used_for_staging = (usage & VK_BUFFER_USAGE_TRANSFER_SRC_BIT) != 0;

        VmaAllocationCreateInfo allocation_create_info  = {};
        allocation_create_info.usage                    = used_for_staging ? VMA_MEMORY_USAGE_CPU_ONLY : (written_frequently ? VMA_MEMORY_USAGE_CPU_TO_GPU : VMA_MEMORY_USAGE_GPU_ONLY);
        allocation_create_info.preferredFlags           = memory_property_flags;

        // Create buffer, allocate memory and bind it to the buffer
        VmaAllocation allocation = nullptr;
        VmaAllocationInfo allocation_info;
        if (!error::check(vmaCreateBuffer(allocator, &buffer_create_info, &allocation_create_info, reinterpret_cast<VkBuffer*>(&_buffer), &allocation, &allocation_info)))
            return false;

        // Keep allocation reference
        globals::rhi_context->allocations[reinterpret_cast<uint64_t>(_buffer)] = allocation;

        // If a pointer to the buffer data has been passed, map the buffer and copy over the data
        if (data != nullptr)
        {
            VkMemoryPropertyFlags memory_flags;
            vmaGetMemoryTypeProperties(allocator, allocation_info.memoryType, &memory_flags);

            bool is_mappable        = (memory_flags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) != 0;
            bool is_host_coherent   = (memory_flags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) != 0;

            // Memory in Vulkan doesn't need to be unmapped before using it on GPU, but unless a
            // memory type has VK_MEMORY_PROPERTY_HOST_COHERENT_BIT flag set, you need to manually
            // invalidate cache before reading of mapped pointer and flush cache after writing to
            // mapped pointer. Map/unmap operations don't do that automatically.

            if (is_mappable)
            {
                if (!is_host_coherent)
                {
                    if (!error::check(vmaInvalidateAllocation(allocator, allocation, 0, size)))
                        return allocation;
                }

                void* mapped = nullptr;
                if (error::check(vmaMapMemory(allocator, allocation, &mapped)))
                {
                    memcpy(mapped, data, size);

                    if (!is_host_coherent)
                    {
                        if (!error::check(vmaFlushAllocation(allocator, allocation, 0, size)))
                            return allocation;
                    }

                    vmaUnmapMemory(allocator, allocation);
                }
            }
            else
            {
                LOG_ERROR("Allocation ended up in non-mappable memory. You need to create CPU-side buffer in VMA_MEMORY_USAGE_CPU_ONLY and make a transfer.");
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
