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

//= INCLUDES ========================
#include "pch.h"
#include "../RHI_Implementation.h"
#include "../RHI_Device.h"
#include "../RHI_Texture2D.h"
#include "../RHI_TextureCube.h"
#include "../RHI_CommandList.h"
#include "../../Rendering/Renderer.h"
//===================================

//= NAMESPACES ===============
using namespace std;
using namespace Spartan::Math;
//============================

namespace Spartan
{
    static VkImageTiling get_format_tiling(const RHI_Format format, VkFormatFeatureFlags feature_flags)
    {
        // Get format properties
        VkFormatProperties format_properties;
        vkGetPhysicalDeviceFormatProperties(vulkan_utility::globals::rhi_context->device_physical, vulkan_format[format], &format_properties);

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

    static void create_image(RHI_Device* rhi_device, RHI_Texture* texture)
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
        create_info.flags             = 0;
        if (texture->GetResourceType() == ResourceType::TextureCube)
        {
            create_info.flags = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;
        }
        create_info.usage             = get_usage_flags(texture);
        create_info.extent.width      = texture->GetWidth();
        create_info.extent.height     = texture->GetHeight();
        create_info.extent.depth      = 1;
        create_info.mipLevels         = texture->GetMipCount();
        create_info.arrayLayers       = texture->GetArrayLength();
        create_info.format            = vulkan_format[format];
        create_info.tiling            = VK_IMAGE_TILING_OPTIMAL;
        create_info.initialLayout     = vulkan_image_layout[static_cast<uint8_t>(texture->GetLayout(0))];
        create_info.samples           = VK_SAMPLE_COUNT_1_BIT;
        create_info.sharingMode       = VK_SHARING_MODE_EXCLUSIVE;

        // Create image
        void*& resource = texture->GetRhiResource();
        rhi_device->CreateTexture(static_cast<void*>(&create_info), resource);
    }

    static void set_debug_name(RHI_Texture* texture)
    {
        string name = texture->GetName();

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

        vulkan_utility::debug::set_object_name(static_cast<VkImage>(texture->GetRhiResource()), name.c_str());

        if (texture->IsSrv())
        {
            vulkan_utility::debug::set_object_name(static_cast<VkImageView>(texture->GetRhiSrv()), name.c_str());
        }

        if (texture->HasPerMipViews())
        {
            for (uint32_t i = 0; i < texture->GetMipCount(); i++)
            {
                vulkan_utility::debug::set_object_name(static_cast<VkImageView>(texture->GetRhiSrvMip(i)), name.c_str());
            }
        }
    }

    inline bool copy_to_staging_buffer(RHI_Device* rhi_device, RHI_Texture* texture, vector<VkBufferImageCopy>& regions, void*& staging_buffer)
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
                regions[region_index].imageSubresource.aspectMask     = vulkan_utility::image::get_aspect_mask(texture);
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
        rhi_device->CreateBuffer(staging_buffer, buffer_offset, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

        // Copy array and mip level data to the staging buffer
        void* mapped_data = nullptr;
        buffer_offset = 0;
        rhi_device->MapMemory(staging_buffer, mapped_data);
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

            rhi_device->UnmapMemory(staging_buffer, mapped_data);
        }

        return true;
    }

    inline bool stage(RHI_Device* rhi_device, RHI_Texture* texture)
    {
        // Copy the texture's data to a staging buffer
        void* staging_buffer = nullptr;
        vector<VkBufferImageCopy> regions;
        if (!copy_to_staging_buffer(rhi_device, texture, regions, staging_buffer))
            return false;

        // Copy the staging buffer into the image
        if (RHI_CommandList* cmd_list = rhi_device->ImmediateBegin(RHI_Queue_Type::Graphics))
        {
            // Optimal layout for images which are the destination of a transfer format
            RHI_Image_Layout layout = RHI_Image_Layout::Transfer_Dst_Optimal;

            // Insert memory barrier
            vulkan_utility::image::set_layout(static_cast<VkCommandBuffer>(cmd_list->GetRhiResource()), texture, 0, texture->GetMipCount(), texture->GetArrayLength(), texture->GetLayout(0), layout);

            // Copy the staging buffer to the image
            vkCmdCopyBufferToImage(
                static_cast<VkCommandBuffer>(cmd_list->GetRhiResource()),
                static_cast<VkBuffer>(staging_buffer),
                static_cast<VkImage>(texture->GetRhiResource()),
                vulkan_image_layout[static_cast<uint8_t>(layout)],
                static_cast<uint32_t>(regions.size()),
                regions.data()
            );

            // End/flush
            rhi_device->ImmediateSubmit(cmd_list);

            // Free staging buffer
            rhi_device->DestroyBuffer(staging_buffer);

            // Update texture layout
            texture->SetLayout(layout, nullptr);
        }

        return true;
    }

    inline RHI_Image_Layout GetAppropriateLayout(RHI_Texture* texture)
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

    void RHI_Texture::RHI_SetLayout(const RHI_Image_Layout new_layout, RHI_CommandList* cmd_list, const uint32_t mip_start, const uint32_t mip_range)
    {
        vulkan_utility::image::set_layout(static_cast<VkCommandBuffer>(cmd_list->GetRhiResource()), this, mip_start, mip_range, m_array_length, m_layout[mip_start], new_layout);
    }

    bool RHI_Texture::RHI_CreateResource()
    {
        SP_ASSERT(m_rhi_device != nullptr);
        SP_ASSERT(m_rhi_device->GetRhiContext()->device != nullptr);

        create_image(m_rhi_device.get(), this);

        // If the texture has any data, stage it
        if (HasData())
        {
            SP_ASSERT_MSG(stage(m_rhi_device.get(), this), "Failed to stage");
        }

        // Transition to target layout
        if (RHI_CommandList* cmd_list = m_rhi_device->ImmediateBegin(RHI_Queue_Type::Graphics))
        {
            RHI_Image_Layout target_layout = GetAppropriateLayout(this);

            // Transition to the final layout
            vulkan_utility::image::set_layout(static_cast<VkCommandBuffer>(cmd_list->GetRhiResource()), this, 0, m_mip_count, m_array_length, m_layout[0], target_layout);
        
            // Flush
            m_rhi_device->ImmediateSubmit(cmd_list);

            // Update this texture with the new layout
            for (uint32_t i = 0; i < m_mip_count; i++)
            {
                m_layout[i] = target_layout;
            }
        }

        // Create image views
        {
            // Shader resource views
            if (IsSrv())
            {
                vulkan_utility::image::view::create(m_rhi_resource, m_rhi_srv, this, m_resource_type, 0, m_array_length, 0, m_mip_count, IsDepthFormat(), false);

                if (HasPerMipViews())
                {
                    for (uint32_t i = 0; i < m_mip_count; i++)
                    {
                        vulkan_utility::image::view::create(m_rhi_resource, m_rhi_srv_mips[i], this, m_resource_type, 0, m_array_length, i, 1, IsDepthFormat(), false);
                    }
                }

                // todo: stencil requires a separate view
            }

            // Render target views
            for (uint32_t i = 0; i < m_array_length; i++)
            {
                // Both cube map slices/faces and array length is encoded into m_array_length.
                // They are rendered on individually, hence why the resource type is ResourceType::Texture2d

                if (IsRenderTargetColor())
                {
                    vulkan_utility::image::view::create(m_rhi_resource, m_rhi_rtv[i], this, ResourceType::Texture2d, i, 1, 0, 1, false, false);
                }

                if (IsRenderTargetDepthStencil())
                {
                    vulkan_utility::image::view::create(m_rhi_resource, m_rhi_dsv[i], this, ResourceType::Texture2d, i, 1, 0, 1, true, false);
                }
            }

            // Name the image and image view(s)
            set_debug_name(this);
        }

        return true;
    }

    void RHI_Texture::RHI_DestroyResource(const bool destroy_main, const bool destroy_per_view)
    {
        SP_ASSERT(m_rhi_device != nullptr);

        // Destruction can happen during engine shutdown, in which case, the renderer might not exist, so, if statement.
        if (Context* context = m_rhi_device->GetContext())
        {
            if (Renderer* renderer = context->GetSystem<Renderer>())
            {
                if (RHI_CommandList* cmd_list = renderer->GetCmdList())
                {
                    cmd_list->Discard();
                }
            }
        }

        // Wait for any in-flight frames that might be using it.
        m_rhi_device->QueueWaitAll();

        // De-allocate everything
        if (destroy_main)
        {
            vulkan_utility::image::view::destroy(m_rhi_srv);

            for (uint32_t i = 0; i < rhi_max_render_target_count; i++)
            {
                vulkan_utility::image::view::destroy(m_rhi_dsv[i]);
                vulkan_utility::image::view::destroy(m_rhi_rtv[i]);
            }
        }

        if (destroy_per_view)
        {
            for (uint32_t i = 0; i < m_mip_count; i++)
            {
                vulkan_utility::image::view::destroy(m_rhi_srv_mips[i]);
            }
        }

        if (destroy_main)
        {
            m_rhi_device->DestroyTexture(m_rhi_resource);
        }
    }
}
