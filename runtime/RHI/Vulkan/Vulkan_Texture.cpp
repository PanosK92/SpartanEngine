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
#include "../RHI_Texture2D.h"
#include "../RHI_CommandList.h"
//================================

//= NAMESPACES ===============
using namespace std;
using namespace Spartan::Math;
//============================

namespace Spartan
{
    namespace
    {
        VkImageAspectFlags get_aspect_mask(const RHI_Texture* texture, const bool only_depth = false, const bool only_stencil = false)
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

        void create_image_view(
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

        void set_debug_name(RHI_Texture* texture)
        {
            string name = texture->GetObjectName();

            // If a name hasn't been defined, try to make a reasonable one.
            if (name.empty())
            {
                if (texture->IsSrv())
                {
                    name += name.empty() ? "sampled" : "-sampled";
                }

                if (texture->IsDsv())
                {
                    name += name.empty() ? "render_target_depth_stencil" : "-render_target_depth_stencil";
                }

                if (texture->IsRtv())
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

        void copy_to_staging_buffer(RHI_Texture* texture, vector<VkBufferImageCopy>& regions, void*& staging_buffer)
        {
            SP_ASSERT_MSG(texture->HasData(), "No data to stage");

            const uint32_t width        = texture->GetWidth();
            const uint32_t height       = texture->GetHeight();
            const uint32_t array_length = texture->GetArrayLength();
            const uint32_t mip_count    = texture->GetMipCount();

            const uint32_t region_count = array_length * mip_count;
            regions.resize(region_count);
            regions.reserve(region_count);

            VkDeviceSize buffer_offset   = 0;
            VkDeviceSize buffer_alignment = RHI_Device::PropertyGetOptimalBufferCopyOffsetAlignment();

            for (uint32_t array_index = 0; array_index < array_length; array_index++)
            {
                for (uint32_t mip_index = 0; mip_index < mip_count; mip_index++)
                {
                    uint32_t region_index = mip_index + array_index * mip_count;
                    uint32_t mip_width    = width >> mip_index;
                    uint32_t mip_height   = height >> mip_index;

                    SP_ASSERT(mip_width != 0 && mip_height != 0);

                    // align buffer offset
                    buffer_offset = (buffer_offset + buffer_alignment - 1) & ~(buffer_alignment - 1);

                    regions[region_index].bufferOffset                    = buffer_offset;
                    regions[region_index].bufferRowLength                 = 0;
                    regions[region_index].bufferImageHeight               = 0;
                    regions[region_index].imageSubresource.aspectMask     = get_aspect_mask(texture);
                    regions[region_index].imageSubresource.mipLevel       = mip_index;
                    regions[region_index].imageSubresource.baseArrayLayer = array_index;
                    regions[region_index].imageSubresource.layerCount     = 1;
                    regions[region_index].imageOffset                     = { 0, 0, 0 };
                    regions[region_index].imageExtent                     = { mip_width, mip_height, 1 };

                    buffer_offset += RHI_Texture::CalculateMipSize(mip_width, mip_height, texture->GetFormat(), texture->GetBitsPerChannel(), texture->GetChannelCount());
                }
            }

            // create staging buffer with aligned size
            RHI_Device::MemoryBufferCreate(staging_buffer, buffer_offset, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, nullptr, "staging_buffer_texture");

            void* mapped_data = nullptr;
            buffer_offset = 0;
            RHI_Device::MemoryMap(staging_buffer, mapped_data);
            {
                for (uint32_t array_index = 0; array_index < array_length; array_index++)
                {
                    for (uint32_t mip_index = 0; mip_index < mip_count; mip_index++)
                    {
                        size_t size = RHI_Texture::CalculateMipSize(width >> mip_index, height >> mip_index, texture->GetFormat(), texture->GetBitsPerChannel(), texture->GetChannelCount());

                        if (texture->GetMip(array_index, mip_index).bytes.size() != 0)
                        {
                            memcpy(static_cast<std::byte*>(mapped_data) + buffer_offset, texture->GetMip(array_index, mip_index).bytes.data(), size);
                        }

                        buffer_offset += size;
                    }
                }

                RHI_Device::MemoryUnmap(staging_buffer);
            }
        }

        void stage(RHI_Texture* texture)
        {
            // copy the texture's data to a staging buffer
            void* staging_buffer = nullptr;
            vector<VkBufferImageCopy> regions;
            copy_to_staging_buffer(texture, regions, staging_buffer);

            // copy the staging buffer into the image
            if (RHI_CommandList* cmd_list = RHI_Device::CmdImmediateBegin(RHI_Queue_Type::Graphics))
            {
                // optimal layout for images which are the destination of a transfer format
                RHI_Image_Layout layout = RHI_Image_Layout::Transfer_Destination;

                // insert memory barrier
                cmd_list->InsertBarrierTexture(texture, 0, texture->GetMipCount(), texture->GetArrayLength(), texture->GetLayout(0), layout);

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
        }

        RHI_Image_Layout GetAppropriateLayout(RHI_Texture* texture)
        {
            RHI_Image_Layout target_layout = RHI_Image_Layout::Preinitialized;

            if (texture->IsRt())
            {
                target_layout = RHI_Image_Layout::Attachment;
            }

            if (texture->IsUav())
                target_layout = RHI_Image_Layout::General;

            if (texture->IsSrv())
                target_layout = RHI_Image_Layout::Shader_Read;

            return target_layout;
        }
    }

    bool RHI_Texture::RHI_CreateResource()
    {
        SP_ASSERT_MSG(m_width  != 0, "Width can't be zero");
        SP_ASSERT_MSG(m_height != 0, "Height can't be zero");

        // as per vulkan
        SetLayout(RHI_Image_Layout::Preinitialized, nullptr);

        // create image
        RHI_Device::MemoryTextureCreate(this);

        // if the texture has any data, stage it
        if (HasData())
        {
            stage(this);
            if ((m_flags & RHI_Texture_KeepData) == 0)
            { 
                m_slices.clear();
            }
        }

        // transition to target layout
        if (RHI_CommandList* cmd_list = RHI_Device::CmdImmediateBegin(RHI_Queue_Type::Graphics))
        {
            RHI_Image_Layout target_layout = GetAppropriateLayout(this);

            // transition to the final layout
            cmd_list->InsertBarrierTexture(this, 0, m_mip_count, m_array_length, m_layout[0], target_layout);
        
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

                if (IsRtv())
                {
                    create_image_view(m_rhi_resource, m_rhi_rtv[i], this, ResourceType::Texture2d, i, 1, 0, 1, false, false);
                }

                if (IsDsv())
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
        // de-allocate everything
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
