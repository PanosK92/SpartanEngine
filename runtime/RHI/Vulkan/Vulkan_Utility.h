/*
Copyright(c) 2016-2023 Panos Karabelas

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

#pragma once

//= INCLUDES =====================
#include "../RHI_Implementation.h"
#include "../RHI_Descriptor.h"
#include "../RHI_Texture.h"
//================================

namespace Spartan::vulkan_utility
{
    static VkDescriptorType to_vulkan_desscriptor_type(const RHI_Descriptor& descriptor)
    {
        if (descriptor.type == RHI_Descriptor_Type::Sampler)
            return VkDescriptorType::VK_DESCRIPTOR_TYPE_SAMPLER;

        if (descriptor.type == RHI_Descriptor_Type::Texture)
            return VkDescriptorType::VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;

        if (descriptor.type == RHI_Descriptor_Type::TextureStorage)
            return VkDescriptorType::VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;

        if (descriptor.type == RHI_Descriptor_Type::StructuredBuffer)
            return VkDescriptorType::VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC;

        if (descriptor.type == RHI_Descriptor_Type::ConstantBuffer)
            return VkDescriptorType::VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;

        SP_ASSERT_MSG(false, "Unhandled descriptor type");
        return VkDescriptorType::VK_DESCRIPTOR_TYPE_MAX_ENUM;
    }

    namespace image
    {
        static VkImageAspectFlags get_aspect_mask(const RHI_Texture* texture, const bool only_depth = false, const bool only_stencil = false)
        {
            VkImageAspectFlags aspect_mask = 0;

            if (texture->IsColorFormat())
            {
                aspect_mask |= VK_IMAGE_ASPECT_COLOR_BIT;
            }
            else
            {
                if (texture->IsDepthFormat() && !only_stencil)
                {
                    aspect_mask |= VK_IMAGE_ASPECT_DEPTH_BIT;
                }

                if (texture->IsStencilFormat() && !only_depth)
                {
                    aspect_mask |= VK_IMAGE_ASPECT_STENCIL_BIT;
                }
            }

            return aspect_mask;
        }

        namespace view
        {
            static void create(
                void* image,
                void*& image_view,
                VkImageViewType type,
                const VkFormat format,
                const VkImageAspectFlags aspect_mask,
                const uint32_t array_index,
                const uint32_t array_length,
                const uint32_t mip_index,
                const uint32_t mip_count
            )
            {
                VkImageViewCreateInfo create_info           = {};
                create_info.sType                           = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
                create_info.image                           = static_cast<VkImage>(image);
                create_info.viewType                        = type;
                create_info.format                          = format;
                create_info.subresourceRange.aspectMask     = aspect_mask;
                create_info.subresourceRange.baseMipLevel   = mip_index;
                create_info.subresourceRange.levelCount     = mip_count;
                create_info.subresourceRange.baseArrayLayer = array_index;
                create_info.subresourceRange.layerCount     = array_length;
                create_info.components.r                    = VK_COMPONENT_SWIZZLE_IDENTITY;
                create_info.components.g                    = VK_COMPONENT_SWIZZLE_IDENTITY;
                create_info.components.b                    = VK_COMPONENT_SWIZZLE_IDENTITY;
                create_info.components.a                    = VK_COMPONENT_SWIZZLE_IDENTITY;

                SP_ASSERT_MSG(vkCreateImageView(RHI_Context::device, &create_info, nullptr, reinterpret_cast<VkImageView*>(&image_view)) == VK_SUCCESS, "Failed to create image view");
            }

            static void create(
                void* image,
                void*& image_view,
                const RHI_Texture* texture,
                const ResourceType resource_type,
                const uint32_t array_index,
                const uint32_t array_length,
                const uint32_t mip_index,
                const uint32_t mip_count,
                const bool only_depth,
                const bool only_stencil
            )
            {
                VkImageViewType type = VK_IMAGE_VIEW_TYPE_MAX_ENUM;

                if (resource_type == ResourceType::Texture2d)
                {
                    type = VK_IMAGE_VIEW_TYPE_2D;
                }
                else if (resource_type == ResourceType::Texture2dArray)
                {
                    type = VK_IMAGE_VIEW_TYPE_2D_ARRAY;
                }
                else if (resource_type == ResourceType::TextureCube)
                {
                    type = VK_IMAGE_VIEW_TYPE_CUBE;
                }

                create(image, image_view, type, vulkan_format[rhi_format_to_index(texture->GetFormat())], get_aspect_mask(texture, only_depth, only_stencil), array_index, array_length, mip_index, mip_count);
            }
        }
    }
}
