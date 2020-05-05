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

//= IMPLEMENTATION ===============
#ifdef API_GRAPHICS_VULKAN
#include "../RHI_Implementation.h"
//================================

//= INCLUDES ==============
#include "Vulkan_Utility.h"
#define VMA_IMPLEMENTATION
#include "vk_mem_alloc.h"
//=========================

//= NAMESPACES =====
using namespace std;
//==================

namespace Spartan::vulkan_utility
{
    PFN_vkCreateDebugUtilsMessengerEXT                          functions::create_messenger                         = nullptr;
    VkDebugUtilsMessengerEXT                                    functions::messenger                                = nullptr;
    PFN_vkDestroyDebugUtilsMessengerEXT                         functions::destroy_messenger                        = nullptr;
    PFN_vkSetDebugUtilsObjectTagEXT                             functions::set_object_tag                           = nullptr;
    PFN_vkSetDebugUtilsObjectNameEXT                            functions::set_object_name                          = nullptr;
    PFN_vkCmdBeginDebugUtilsLabelEXT                            functions::marker_begin                             = nullptr;
    PFN_vkCmdEndDebugUtilsLabelEXT                              functions::marker_end                               = nullptr;
    PFN_vkGetPhysicalDeviceMemoryProperties2KHR                 functions::get_physical_device_memory_properties_2  = nullptr;
    mutex                                                       command_buffer_immediate::m_mutex_begin;
    mutex                                                       command_buffer_immediate::m_mutex_end;
    map<RHI_Queue_Type, command_buffer_immediate::cmdbi_object> command_buffer_immediate::m_objects;

	bool image::create(RHI_Texture* texture)
	{
        // Get format support
        RHI_Format format                   = texture->GetFormat();
        bool is_render_target_depth_stencil = texture->IsRenderTargetDepthStencil();
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
        texture->SetLayout(RHI_Image_Preinitialized);
        
        VkImageCreateInfo create_info   = {};
        create_info.sType               = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        create_info.imageType           = VK_IMAGE_TYPE_2D;
        create_info.flags               = (texture->GetResourceType() == Resource_TextureCube) ? VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT : 0;
        create_info.extent.width        = texture->GetWidth();
        create_info.extent.height       = texture->GetHeight();
        create_info.extent.depth        = 1;
        create_info.mipLevels           = texture->GetMiplevels();
        create_info.arrayLayers         = texture->GetArraySize();
        create_info.format              = vulkan_format[format];
        create_info.tiling              = VK_IMAGE_TILING_OPTIMAL;
        create_info.initialLayout       = vulkan_image_layout[texture->GetLayout()];
        create_info.usage               = get_usage_flags(texture);
        create_info.samples             = VK_SAMPLE_COUNT_1_BIT;
        create_info.sharingMode         = VK_SHARING_MODE_EXCLUSIVE;

        VmaAllocationCreateInfo allocation_info = {};
        allocation_info.usage                   = VMA_MEMORY_USAGE_UNKNOWN;

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
}
#endif
