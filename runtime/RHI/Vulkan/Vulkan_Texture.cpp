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

//= INCLUDES =====================
#include "pch.h"
#include "../RHI_Implementation.h"
#include "../RHI_Device.h"
#include "../RHI_Texture2D.h"
#include "../RHI_CommandList.h"
#include "../Profiling/Profiler.h"
//================================

//= NAMESPACES ===============
using namespace std;
using namespace Spartan::Math;
//============================

namespace Spartan
{
    namespace
    {
        static VkImageTiling get_format_tiling(const RHI_Format format, VkFormatFeatureFlags feature_flags)
        {
            // Get format properties
            VkFormatProperties format_properties;
            vkGetPhysicalDeviceFormatProperties(RHI_Context::device_physical, vulkan_format[rhi_format_to_index(format)], &format_properties);

            // Check for optimal support
            if (format_properties.optimalTilingFeatures & feature_flags)
                return VK_IMAGE_TILING_OPTIMAL;

            // Check for linear support
            if (format_properties.linearTilingFeatures & feature_flags)
                return VK_IMAGE_TILING_LINEAR;

            return VK_IMAGE_TILING_MAX_ENUM;
        }

        static VkImageUsageFlags get_usage_flags(const RHI_Texture* texture)
        {
            VkImageUsageFlags flags = 0;

            flags |= texture->IsSrv()                      ? VK_IMAGE_USAGE_SAMPLED_BIT                  : 0;
            flags |= texture->IsUav()                      ? VK_IMAGE_USAGE_STORAGE_BIT                  : 0;
            flags |= texture->IsRenderTargetDepthStencil() ? VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT : 0;
            flags |= texture->IsRenderTargetColor()        ? VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT         : 0;

            // If the texture has data, it will be staged, so it needs transfer bits.
            // If the texture participates in clear or blit operations, it needs transfer bits.
            if (texture->HasData() || (texture->GetFlags() & RHI_Texture_ClearOrBlit) != 0)
            {
                flags |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT; // source of a transfer command.
                flags |= VK_IMAGE_USAGE_TRANSFER_DST_BIT; // destination of a transfer command
            }

            return flags;
        }

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

        static void create_image(RHI_Texture* texture)
        {
            // Deduce format flags
            bool is_render_target_depth_stencil = texture->IsRenderTargetDepthStencil();
            VkFormatFeatureFlags format_flags  = is_render_target_depth_stencil ? VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT : VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT;

            // Deduce image tiling
            RHI_Format format           = texture->GetFormat();
            VkImageTiling image_tiling  = get_format_tiling(format, format_flags);
            
            SP_ASSERT_MSG(image_tiling != VK_IMAGE_TILING_MAX_ENUM, "The GPU doesn't support this format");
            SP_ASSERT_MSG(image_tiling == VK_IMAGE_TILING_OPTIMAL,  "This format doesn't support optimal tiling, switch to a more efficient format");

            // Set layout to pre-initialised (required by Vulkan)
            texture->SetLayout(RHI_Image_Layout::Preinitialized, nullptr);

            VkImageCreateInfo create_info = {};
            create_info.sType             = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
            create_info.imageType         = VK_IMAGE_TYPE_2D;
            create_info.flags             = texture->GetResourceType() == ResourceType::TextureCube ? VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT : 0;
            create_info.usage             = get_usage_flags(texture);
            create_info.extent.width      = texture->GetWidth();
            create_info.extent.height     = texture->GetHeight();
            create_info.extent.depth      = 1;
            create_info.mipLevels         = texture->GetMipCount();
            create_info.arrayLayers       = texture->GetArrayLength();
            create_info.format            = vulkan_format[rhi_format_to_index(format)];
            create_info.tiling            = VK_IMAGE_TILING_OPTIMAL;
            create_info.initialLayout     = vulkan_image_layout[static_cast<uint8_t>(texture->GetLayout(0))];
            create_info.samples           = VK_SAMPLE_COUNT_1_BIT;
            create_info.sharingMode       = VK_SHARING_MODE_EXCLUSIVE;

            // Create image
            void*& resource = texture->GetRhiResource();
            RHI_Device::MemoryTextureCreate(static_cast<void*>(&create_info), resource, texture->GetObjectName().c_str());
        }

        static void create_image_view(
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
            VkImageViewType view_type = VK_IMAGE_VIEW_TYPE_MAX_ENUM;
            {
                if (resource_type == ResourceType::Texture2d)
                {
                    view_type = VkImageViewType::VK_IMAGE_VIEW_TYPE_2D;
                }
                else if (resource_type == ResourceType::Texture2dArray)
                {
                    view_type = VkImageViewType::VK_IMAGE_VIEW_TYPE_2D_ARRAY;
                }
                else if (resource_type == ResourceType::TextureCube)
                {
                    view_type = VkImageViewType::VK_IMAGE_VIEW_TYPE_CUBE;
                }
            }

            VkImageViewCreateInfo create_info           = {};
            create_info.sType                           = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
            create_info.image                           = static_cast<VkImage>(image);
            create_info.viewType                        = view_type;
            create_info.format                          = vulkan_format[rhi_format_to_index(texture->GetFormat())];
            create_info.subresourceRange.aspectMask     = get_aspect_mask(texture, texture->IsDepthFormat(), false);
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

        static void set_debug_name(RHI_Texture* texture)
        {
            string name = texture->GetObjectName();

            // If a name hasn't been defined, try to make a reasonable one.
            if (name.empty())
            {
                if (texture->IsSrv())
                {
                    name += name.empty() ? "sampled" : "-sampled";
                }

                if (texture->IsRenderTargetDepthStencil())
                {
                    name += name.empty() ? "render_target_depth_stencil" : "-render_target_depth_stencil";
                }

                if (texture->IsRenderTargetColor())
                {
                    name += name.empty() ? "render_target_color" : "-render_target_color";
                }
            }

            RHI_Device::SetResourceName(texture->GetRhiResource(), RHI_Resource_Type::Texture, name);

            if (texture->IsSrv())
            {
                RHI_Device::SetResourceName(texture->GetRhiSrv(), RHI_Resource_Type::TextureView, name);
            }

            if (texture->HasPerMipViews())
            {
                for (uint32_t i = 0; i < texture->GetMipCount(); i++)
                {
                    RHI_Device::SetResourceName(texture->GetRhiSrvMip(i), RHI_Resource_Type::TextureView, name);
                }
            }
        }

        static bool copy_to_staging_buffer(RHI_Texture* texture, vector<VkBufferImageCopy>& regions, void*& staging_buffer)
        {
            if (!texture->HasData())
            {
                SP_LOG_WARNING("No data to stage");
                return true;
            }

            const uint32_t width           = texture->GetWidth();
            const uint32_t height          = texture->GetHeight();
            const uint32_t array_length    = texture->GetArrayLength();
            const uint32_t mip_count       = texture->GetMipCount();
            const uint32_t bytes_per_pixel = texture->GetBytesPerPixel();

            const uint32_t region_count = array_length * mip_count;
            regions.resize(region_count);
            regions.reserve(region_count);

            // Fill out VkBufferImageCopy structs describing the array and the mip levels
            VkDeviceSize buffer_offset = 0;
            for (uint32_t array_index = 0; array_index < array_length; array_index++)
            {
                for (uint32_t mip_index = 0; mip_index < mip_count; mip_index++)
                {
                    uint32_t region_index   = mip_index + array_index * mip_count;
                    uint32_t mip_width      = width >> mip_index;
                    uint32_t mip_height     = height >> mip_index;

                    regions[region_index].bufferOffset                    = buffer_offset;
                    regions[region_index].bufferRowLength                 = 0;
                    regions[region_index].bufferImageHeight               = 0;
                    regions[region_index].imageSubresource.aspectMask     = get_aspect_mask(texture);
                    regions[region_index].imageSubresource.mipLevel       = mip_index;
                    regions[region_index].imageSubresource.baseArrayLayer = array_index;
                    regions[region_index].imageSubresource.layerCount     = 1;
                    regions[region_index].imageOffset                     = { 0, 0, 0 };
                    regions[region_index].imageExtent                     = { mip_width, mip_height, 1 };

                    // Update staging buffer memory requirement (in bytes)
                    buffer_offset += static_cast<uint64_t>(mip_width) * static_cast<uint64_t>(mip_height) * static_cast<uint64_t>(bytes_per_pixel);
                }
            }

            // Create staging buffer
            RHI_Device::MemoryBufferCreate(staging_buffer, buffer_offset, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, nullptr, "staging_buffer_texture");

            // Copy array and mip level data to the staging buffer
            void* mapped_data = nullptr;
            buffer_offset = 0;
            RHI_Device::MemoryMap(staging_buffer, mapped_data);
            {
                for (uint32_t array_index = 0; array_index < array_length; array_index++)
                {
                    for (uint32_t mip_index = 0; mip_index < mip_count; mip_index++)
                    {
                        uint64_t buffer_size = static_cast<uint64_t>(width >> mip_index) * static_cast<uint64_t>(height >> mip_index) * static_cast<uint64_t>(bytes_per_pixel);
                        memcpy(static_cast<std::byte*>(mapped_data) + buffer_offset, texture->GetMip(array_index, mip_index).bytes.data(), buffer_size);
                        buffer_offset += buffer_size;
                    }
                }

                RHI_Device::MemoryUnmap(staging_buffer, mapped_data);
            }

            return true;
        }

        static bool stage(RHI_Texture* texture)
        {
            // copy the texture's data to a staging buffer
            void* staging_buffer = nullptr;
            vector<VkBufferImageCopy> regions;
            if (!copy_to_staging_buffer(texture, regions, staging_buffer))
                return false;

            // copy the staging buffer into the image
            if (RHI_CommandList* cmd_list = RHI_Device::CmdImmediateBegin(RHI_Queue_Type::Graphics))
            {
                // optimal layout for images which are the destination of a transfer format
                RHI_Image_Layout layout = RHI_Image_Layout::Transfer_Dst_Optimal;

                // insert memory barrier
                cmd_list->InsertMemoryBarrierImage(texture, 0, texture->GetMipCount(), texture->GetArrayLength(), texture->GetLayout(0), layout);

                // copy the staging buffer to the image
                vkCmdCopyBufferToImage(
                    static_cast<VkCommandBuffer>(cmd_list->GetRhiResource()),
                    static_cast<VkBuffer>(staging_buffer),
                    static_cast<VkImage>(texture->GetRhiResource()),
                    vulkan_image_layout[static_cast<uint8_t>(layout)],
                    static_cast<uint32_t>(regions.size()),
                    regions.data()
                );

                // end/flush
                RHI_Device::CmdImmediateSubmit(cmd_list);

                // free staging buffer
                RHI_Device::MemoryBufferDestroy(staging_buffer);

                // update texture layout
                texture->SetLayout(layout, nullptr);
            }

            return true;
        }

        static RHI_Image_Layout GetAppropriateLayout(RHI_Texture* texture)
        {
            RHI_Image_Layout target_layout = RHI_Image_Layout::Preinitialized;

            if (texture->IsRenderTargetColor())
            {
                target_layout = RHI_Image_Layout::Color_Attachment_Optimal;
            }
            else if (texture->IsRenderTargetDepthStencil())
            {
                target_layout = RHI_Image_Layout::Depth_Stencil_Attachment_Optimal;
            }

            if (texture->IsUav())
                target_layout = RHI_Image_Layout::General;

            if (texture->IsSrv())
                target_layout = RHI_Image_Layout::Shader_Read_Only_Optimal;

            return target_layout;
        }
    }

    void RHI_Texture::RHI_SetLayout(const RHI_Image_Layout new_layout, RHI_CommandList* cmd_list, const uint32_t mip_start, const uint32_t mip_range)
    {
        cmd_list->InsertMemoryBarrierImage(this, mip_start, mip_range, m_array_length, m_layout[mip_start], new_layout);
    }

    bool RHI_Texture::RHI_CreateResource()
    {
        SP_ASSERT_MSG(m_width  != 0, "Width can't be zero");
        SP_ASSERT_MSG(m_height != 0, "Height can't be zero");

        create_image(this);

        // if the texture has any data, stage it
        if (HasData())
        {
            SP_ASSERT_MSG(stage(this), "Failed to stage");
        }

        // transition to target layout
        if (RHI_CommandList* cmd_list = RHI_Device::CmdImmediateBegin(RHI_Queue_Type::Graphics))
        {
            RHI_Image_Layout target_layout = GetAppropriateLayout(this);

            // transition to the final layout
            cmd_list->InsertMemoryBarrierImage(this, 0, m_mip_count, m_array_length, m_layout[0], target_layout);
        
            // flush
            RHI_Device::CmdImmediateSubmit(cmd_list);

            // update this texture with the new layout
            for (uint32_t i = 0; i < m_mip_count; i++)
            {
                m_layout[i] = target_layout;
            }
        }

        // create image views
        {
            // shader resource views
            if (IsSrv())
            {
                create_image_view(m_rhi_resource, m_rhi_srv, this, m_resource_type, 0, m_array_length, 0, m_mip_count, IsDepthFormat(), false);

                if (HasPerMipViews())
                {
                    for (uint32_t i = 0; i < m_mip_count; i++)
                    {
                        create_image_view(m_rhi_resource, m_rhi_srv_mips[i], this, m_resource_type, 0, m_array_length, i, 1, IsDepthFormat(), false);
                    }
                }

                // stencil requires a separate view
            }

            // render target views
            for (uint32_t i = 0; i < m_array_length; i++)
            {
                // both cube map slices/faces and array length is encoded into m_array_length.
                // they are rendered on individually, hence why the resource type is ResourceType::Texture2d

                if (IsRenderTargetColor())
                {
                    create_image_view(m_rhi_resource, m_rhi_rtv[i], this, ResourceType::Texture2d, i, 1, 0, 1, false, false);
                }

                if (IsRenderTargetDepthStencil())
                {
                    create_image_view(m_rhi_resource, m_rhi_dsv[i], this, ResourceType::Texture2d, i, 1, 0, 1, true, false);
                }
            }

            // name the image and image view(s)
            set_debug_name(this);
        }

        return true;
    }

    void RHI_Texture::RHI_DestroyResource(const bool destroy_main, const bool destroy_per_view)
    {
        // De-allocate everything
        if (destroy_main)
        {
            RHI_Device::DeletionQueueAdd(RHI_Resource_Type::TextureView, m_rhi_srv);
            m_rhi_srv = nullptr;

            for (uint32_t i = 0; i < rhi_max_render_target_count; i++)
            {
                RHI_Device::DeletionQueueAdd(RHI_Resource_Type::TextureView, m_rhi_dsv[i]);
                m_rhi_dsv[i] = nullptr;

                RHI_Device::DeletionQueueAdd(RHI_Resource_Type::TextureView, m_rhi_rtv[i]);
                m_rhi_rtv[i] = nullptr;
            }
        }

        if (destroy_per_view)
        {
            for (uint32_t i = 0; i < m_mip_count; i++)
            {
                RHI_Device::DeletionQueueAdd(RHI_Resource_Type::TextureView, m_rhi_srv_mips[i]);
                m_rhi_srv_mips[i] = nullptr;
            }
        }

        if (destroy_main)
        {
            RHI_Device::DeletionQueueAdd(RHI_Resource_Type::Texture, m_rhi_resource);
            m_rhi_resource = nullptr;
        }
    }
}
