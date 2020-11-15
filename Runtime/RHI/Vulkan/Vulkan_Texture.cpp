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

//= INCLUDES ========================
#include "Spartan.h"
#include "../RHI_Implementation.h"
#include "../RHI_Device.h"
#include "../RHI_Texture2D.h"
#include "../RHI_TextureCube.h"
#include "../RHI_CommandList.h"
#include "../../Profiling/Profiler.h"
#include "../../Rendering/Renderer.h"
#include "../RHI_DescriptorCache.h"
//===================================

//= NAMESPACES ===============
using namespace std;
using namespace Spartan::Math;
//============================

namespace Spartan
{
    inline void set_debug_name(RHI_Texture* texture)
    {
        string name = texture->GetName();

        // If a name hasn't been defined, try to make a reasonable one
        if (name.empty())
        {
            if (texture->IsSampled())
            {
                name += name.empty() ? "sampled" : "-sampled";
            }

            if (texture->IsDepthStencil())
            {
                name += name.empty() ? "depth_stencil" : "-depth_stencil";
            }

            if (texture->IsRenderTarget())
            {
                name += name.empty() ? "render_target" : "-render_target";
            }

        }

        vulkan_utility::debug::set_name(static_cast<VkImage>(texture->Get_Resource()), name.c_str());
        vulkan_utility::debug::set_name(static_cast<VkImageView>(texture->Get_Resource_View(0)), name.c_str());
        if (texture->IsSampled() && texture->IsStencilFormat())
        {
            vulkan_utility::debug::set_name(static_cast<VkImageView>(texture->Get_Resource_View(1)), name.c_str());
        }
    }

    inline bool copy_to_staging_buffer(RHI_Texture* texture, std::vector<VkBufferImageCopy>& buffer_image_copies, void*& staging_buffer)
    {
        if (!texture->HasData())
        {
            LOG_WARNING("No data to stage");
            return true;
        }

        const uint32_t width            = texture->GetWidth();
        const uint32_t height           = texture->GetHeight();
        const uint32_t array_size       = texture->GetArraySize();
        const uint32_t mip_levels       = texture->GetMipCount();
        const uint32_t bytes_per_pixel  = texture->GetBytesPerPixel();

        // Fill out VkBufferImageCopy structs describing the array and the mip levels   
        VkDeviceSize buffer_offset = 0;
        for (uint32_t array_index = 0; array_index < array_size; array_index++)
        {
            for (uint32_t mip_index = 0; mip_index < mip_levels; mip_index++)
            {
                uint32_t mip_width  = width >> mip_index;
                uint32_t mip_height = height >> mip_index;

                VkBufferImageCopy region                = {};
                region.bufferOffset                     = buffer_offset;
                region.bufferRowLength                  = 0;
                region.bufferImageHeight                = 0;
                region.imageSubresource.aspectMask      = vulkan_utility::image::get_aspect_mask(texture);
                region.imageSubresource.mipLevel        = mip_index;
                region.imageSubresource.baseArrayLayer  = array_index;
                region.imageSubresource.layerCount      = array_size;
                region.imageOffset                      = { 0, 0, 0 };
                region.imageExtent                      = { mip_width, mip_height, 1 };

                buffer_image_copies[mip_index] = region;

                // Update staging buffer memory requirement (in bytes)
                buffer_offset += mip_width * mip_height * bytes_per_pixel;
            }
        }

        // Create staging buffer
        VmaAllocation allocation = vulkan_utility::buffer::create(staging_buffer, buffer_offset, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

        // Copy array and mip level data to the staging buffer
        void* data = nullptr;
        buffer_offset = 0;
        if (vulkan_utility::error::check(vmaMapMemory(vulkan_utility::globals::rhi_context->allocator, allocation, &data)))
        {
            for (uint32_t array_index = 0; array_index < array_size; array_index++)
            {
                for (uint32_t mip_index = 0; mip_index < mip_levels; mip_index++)
                {
                    uint64_t buffer_size = (width >> mip_index) * (height >> mip_index) * bytes_per_pixel;
                    memcpy(static_cast<std::byte*>(data) + buffer_offset, texture->GetMip(array_index + mip_index).data(), buffer_size);
                    buffer_offset += buffer_size;
                }
            }

            vmaUnmapMemory(vulkan_utility::globals::rhi_context->allocator, allocation);
        }

        return true;
    }

    inline bool stage(RHI_Texture* texture, RHI_Image_Layout& texture_layout)
    {
        // Copy the texture's data to a staging buffer
        void* staging_buffer = nullptr;
        std::vector<VkBufferImageCopy> buffer_image_copies(texture->GetMipCount());
        if (!copy_to_staging_buffer(texture, buffer_image_copies, staging_buffer))
            return false;

        // Copy the staging buffer into the image
        if (VkCommandBuffer cmd_buffer = vulkan_utility::command_buffer_immediate::begin(RHI_Queue_Graphics))
        {
            // Optimal layout for images which are the destination of a transfer format
            RHI_Image_Layout layout = RHI_Image_Layout::Transfer_Dst_Optimal;

            // Transition to layout
            if (!vulkan_utility::image::set_layout(cmd_buffer, texture, layout))
                return false;

            // Copy the staging buffer to the image
            vkCmdCopyBufferToImage(
                cmd_buffer,
                static_cast<VkBuffer>(staging_buffer),
                static_cast<VkImage>(texture->Get_Resource()),
                vulkan_image_layout[static_cast<uint8_t>(layout)],
                static_cast<uint32_t>(buffer_image_copies.size()),
                buffer_image_copies.data()
            );

            // End/flush
            if (!vulkan_utility::command_buffer_immediate::end(RHI_Queue_Graphics))
                return false;

            // Free staging buffer
            vulkan_utility::buffer::destroy(staging_buffer);

            // Let the texture know about it's new layout
            texture_layout = layout;
        }

        return true;
    }

    inline RHI_Image_Layout GetAppropriateLayout(RHI_Texture* texture)
    {
        RHI_Image_Layout target_layout = RHI_Image_Layout::Preinitialized;

        if (texture->IsSampled() && texture->IsColorFormat())
            target_layout = RHI_Image_Layout::Shader_Read_Only_Optimal;

        if (texture->IsRenderTarget())
            target_layout = RHI_Image_Layout::Color_Attachment_Optimal;

        if (texture->IsDepthStencil())
            target_layout = RHI_Image_Layout::Depth_Stencil_Attachment_Optimal;

        if (texture->IsStorage())
            target_layout = RHI_Image_Layout::General;

        return target_layout;
    }

    RHI_Texture2D::~RHI_Texture2D()
    {
        if (!m_rhi_device || !m_rhi_device->IsInitialized())
        {
            LOG_ERROR("Invalid RHI Device.");
        }

        // Ensure the GPU is not using this texture
        m_rhi_device->Queue_WaitAll();
        
        // Make sure that no descriptor sets refer to this texture.
        // Right now I just reset the descriptor cache, which works but it's not ideal.
        // Todo: Get only the referring descriptor sets, and simply update the slot this texture is bound to.
        if (Renderer* renderer = m_rhi_device->GetContext()->GetSubsystem<Renderer>())
        {
            if (RHI_DescriptorCache* descriptor_cache = renderer->GetDescriptorCache())
            {
                descriptor_cache->Reset();
            }
        }

        // De-allocate everything
        m_data.clear();
        vulkan_utility::image::view::destroy(m_resource_view[0]);
        vulkan_utility::image::view::destroy(m_resource_view[1]);
        for (uint32_t i = 0; i < rhi_max_render_target_count; i++)
        {
            vulkan_utility::image::view::destroy(m_resource_view_depthStencil[i]);
            vulkan_utility::image::view::destroy(m_resource_view_renderTarget[i]);
        }
        vulkan_utility::image::destroy(this);
    }

    void RHI_Texture::SetLayout(const RHI_Image_Layout new_layout, RHI_CommandList* command_list /*= nullptr*/)
    {
        // The texture is most likely still initialising
        if (m_layout == RHI_Image_Layout::Undefined)
            return;

        if (m_layout == new_layout)
            return;

         // If a command list is provided, this means we should insert a pipeline barrier
        if (command_list)
        {
            if (!vulkan_utility::image::set_layout(static_cast<VkCommandBuffer>(command_list->GetResource_CommandBuffer()), this, new_layout))
                return;

            m_context->GetSubsystem<Profiler>()->m_rhi_pipeline_barriers++;
        }

        m_layout = new_layout;
    }

    bool RHI_Texture2D::CreateResourceGpu()
    {
        // Create image
        if (!vulkan_utility::image::create(this))
        {
            LOG_ERROR("Failed to create image");
            return false;
        }

        // If the texture has any data, stage it
        if (HasData())
        {
            if (!stage(this, m_layout))
            {
                LOG_ERROR("Failed to stage");
                return false;
            }
        }

        // Transition to target layout
        if (VkCommandBuffer cmd_buffer = vulkan_utility::command_buffer_immediate::begin(RHI_Queue_Graphics))
        {    
            RHI_Image_Layout target_layout = GetAppropriateLayout(this);
                
            // Transition to the final layout
            if (!vulkan_utility::image::set_layout(cmd_buffer, this, target_layout))
            {
                LOG_ERROR("Failed to transition layout");
                return false;
            }
        
            // Flush
            if (!vulkan_utility::command_buffer_immediate::end(RHI_Queue_Graphics))
            {
                LOG_ERROR("Failed to end command buffer");
                return false;
            }

            // Update this texture with the new layout
            m_layout = target_layout;
        }

        // Create image views
        {
            // Shader resource views
            if (IsSampled())
            {
                if (IsColorFormat())
                {
                    if (!vulkan_utility::image::view::create(m_resource, m_resource_view[0], this))
                        return false;
                }

                if (IsDepthFormat())
                {
                    if (!vulkan_utility::image::view::create(m_resource, m_resource_view[0], this, 0, m_array_size, true, false))
                        return false;
                }

                if (IsStencilFormat())
                {
                    if (!vulkan_utility::image::view::create(m_resource, m_resource_view[1], this, 0, m_array_size, false, true))
                        return false;
                }
            }

            // Render target views
            for (uint32_t i = 0; i < m_array_size; i++)
            {
                if (IsRenderTarget())
                {
                    if (!vulkan_utility::image::view::create(m_resource, m_resource_view_renderTarget[i], this, i, 1))
                        return false;
                }

                if (IsDepthStencil())
                {
                    if (!vulkan_utility::image::view::create(m_resource, m_resource_view_depthStencil[i], this, i, 1, true))
                        return false;
                }
            }

            // Name the image and image view(s)
            set_debug_name(this);
        }

        return true;
    }

    // TEXTURE CUBE

    RHI_TextureCube::~RHI_TextureCube()
    {
        if (!m_rhi_device->IsInitialized())
            return;

        m_rhi_device->Queue_WaitAll();
        m_data.clear();

        vulkan_utility::image::view::destroy(m_resource_view[0]);
        vulkan_utility::image::view::destroy(m_resource_view[1]);
        for (uint32_t i = 0; i < rhi_max_render_target_count; i++)
        {
            vulkan_utility::image::view::destroy(m_resource_view_depthStencil[i]);
            vulkan_utility::image::view::destroy(m_resource_view_renderTarget[i]);
        }
        vulkan_utility::image::destroy(this);
    }

    bool RHI_TextureCube::CreateResourceGpu()
    {
        // Create image
        if (!vulkan_utility::image::create(this))
        {
            LOG_ERROR("Failed to create image");
            return false;
        }

        // If the texture has any data, stage it
        if (HasData())
        {
            if (!stage(this, m_layout))
                return false;
        }

        // Transition to target layout
        if (VkCommandBuffer cmd_buffer = vulkan_utility::command_buffer_immediate::begin(RHI_Queue_Graphics))
        {
            RHI_Image_Layout target_layout = GetAppropriateLayout(this);

            // Transition to the final layout
            if (!vulkan_utility::image::set_layout(cmd_buffer, this, target_layout))
                return false;

            // Flush
            if (!vulkan_utility::command_buffer_immediate::end(RHI_Queue_Graphics))
                return false;

            // Update this texture with the new layout
            m_layout = target_layout;
        }

        // Create image views
        {
            // Shader resource views
            if (IsSampled())
            {
                if (IsColorFormat())
                {
                    if (!vulkan_utility::image::view::create(m_resource, m_resource_view[0], this))
                        return false;
                }

                if (IsDepthFormat())
                {
                    if (!vulkan_utility::image::view::create(m_resource, m_resource_view[0], this, 0, m_array_size, true, false))
                        return false;
                }

                if (IsStencilFormat())
                {
                    if (!vulkan_utility::image::view::create(m_resource, m_resource_view[1], this, 0, m_array_size, false, true))
                        return false;
                }
            }

            // Render target views
            for (uint32_t i = 0; i < m_array_size; i++)
            {
                if (IsRenderTarget())
                {
                    if (!vulkan_utility::image::view::create(m_resource, m_resource_view_renderTarget[i], this, i, 1))
                        return false;
                }

                if (IsDepthStencil())
                {
                    if (!vulkan_utility::image::view::create(m_resource, m_resource_view_depthStencil[i], this, i, 1, true))
                        return false;
                }
            }

            // Name the image and image view(s)
            set_debug_name(this);
        }

        return true;
    }
}
