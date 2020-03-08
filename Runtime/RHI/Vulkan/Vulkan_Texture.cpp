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

//= INCLUDES =====================
#include "../RHI_Device.h"
#include "../RHI_Texture2D.h"
#include "../RHI_TextureCube.h"
#include "../../Math/MathHelper.h"
#include "../RHI_CommandList.h"
//================================

//= NAMESPACES ===============
using namespace std;
using namespace Spartan::Math;
//============================

namespace Spartan
{
    RHI_Texture2D::~RHI_Texture2D()
    {
        if (!m_rhi_device->IsInitialized())
            return;

        m_rhi_device->Queue_WaitAll();
        m_data.clear();
        const auto rhi_context = m_rhi_device->GetContextRhi();
        vulkan_common::image::view::destroy(rhi_context, m_view_texture[0]);
        vulkan_common::image::view::destroy(rhi_context, m_view_texture[1]);
        vulkan_common::image::view::destroy(rhi_context, m_view_attachment_depth_stencil);
        vulkan_common::frame_buffer::destroy(rhi_context, m_view_attachment_color);
        vulkan_common::image::destroy(rhi_context, m_texture);
		vulkan_common::memory::free(m_rhi_device->GetContextRhi(), m_resource_memory);
	}

    void RHI_Texture::SetLayout(const RHI_Image_Layout layout, RHI_CommandList* command_list /*= nullptr*/)
    {
        if (m_layout == layout)
            return;

        if (command_list)
        {
            if (!vulkan_common::image::set_layout(m_rhi_device.get(), static_cast<VkCommandBuffer>(command_list->GetResource_CommandBuffer()), this, layout))
                return;
        }

        m_layout = layout;
    }

	bool RHI_Texture2D::CreateResourceGpu()
	{
        const RHI_Context* rhi_context = m_rhi_device->GetContextRhi();

        // Get format support
        VkFormatFeatureFlags feature_flag   = IsRenderTargetDepthStencil() ? VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT : VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT;
        VkImageTiling image_tiling          = vulkan_common::image::is_format_supported(rhi_context, m_format, feature_flag);

        // If the format is not supported, early exit
        if (image_tiling == VK_IMAGE_TILING_MAX_ENUM)
        {
            LOG_ERROR("GPU does not support the usage of %s as a %s.", rhi_format_to_string(m_format), IsRenderTargetDepthStencil() ? "depth-stencil attachment" : "color attachment");
            return false;
        }

        // If the format is not optimal, let the user know
        if (image_tiling != VK_IMAGE_TILING_OPTIMAL)
        {
            LOG_ERROR("Format %s does not support optimal tiling, considering switching to a more efficient format.", rhi_format_to_string(m_format));
            return false;
        }

        // Initialize
        SetLayout(RHI_Image_Preinitialized);
        bool use_staging    = !m_data.empty();
        auto image          = reinterpret_cast<VkImage*>(&m_texture);
        auto image_memory   = reinterpret_cast<VkDeviceMemory*>(&m_resource_memory);

        // Deduce usage flags
        VkImageUsageFlags usage_flags = 0;
        {
            usage_flags |= (m_flags & RHI_Texture_ShaderView)          ? VK_IMAGE_USAGE_SAMPLED_BIT                    : 0;
            usage_flags |= (m_flags & RHI_Texture_DepthStencilView)    ? VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT   : 0;
            usage_flags |= (m_flags & RHI_Texture_RenderTargetView)    ? VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT           : 0;
            if (use_staging)
            {
                usage_flags |= use_staging ? VK_IMAGE_USAGE_TRANSFER_SRC_BIT : 0; // source of a transfer command.
                usage_flags |= use_staging ? VK_IMAGE_USAGE_TRANSFER_DST_BIT : 0; // destination of a transfer command.
            }
        }

        // Create image
        {
            if (!vulkan_common::image::create(rhi_context, *image, m_width, m_height, m_mip_levels, m_array_size, vulkan_format[m_format], image_tiling, m_layout, usage_flags))
            {
                LOG_ERROR("Failed to create image");
                return false;
            }

            if (!vulkan_common::image::allocate_bind(rhi_context, *image, image_memory))
            {
                LOG_ERROR("Failed to allocate and bind image memory");
                return false;
            }
        }

        // Create command buffer (for later layout transitioning)
        VkCommandBuffer cmd_buffer = vulkan_common::command_buffer_immediate::begin(m_rhi_device.get(), RHI_Queue_Graphics);

        // Use staging if needed
        void* stating_buffer        = nullptr;
        void* staging_buffer_memory = nullptr;
        if (use_staging)
        {
            // Create buffer copy structs for each mip level
            vector<VkBufferImageCopy> buffer_image_copies(m_mip_levels);
            vector<uint64_t> mip_memory(m_array_size * m_mip_levels);
            VkDeviceSize buffer_size = 0;
            uint64_t offset = 0;
            for (uint32_t array_index = 0; array_index < m_array_size; array_index++)
            {
                for (uint32_t mip_index = 0; mip_index < m_mip_levels; mip_index++)
                {
                    uint32_t mip_width  = m_width >> mip_index;
                    uint32_t mip_height = m_height >> mip_index;

                    VkBufferImageCopy region				= {};
                    region.bufferOffset						= offset;
                    region.bufferRowLength					= 0;
                    region.bufferImageHeight				= 0;
                    region.imageSubresource.aspectMask      = vulkan_common::image::get_aspect_mask(this);
                    region.imageSubresource.mipLevel		= mip_index;
                    region.imageSubresource.baseArrayLayer	= array_index;
                    region.imageSubresource.layerCount		= m_array_size;
                    region.imageOffset						= { 0, 0, 0 };
                    region.imageExtent						= { mip_width, mip_height, 1 };

                    buffer_image_copies[mip_index] = region;

                    // Update offset
                    offset += static_cast<uint32_t>(m_data[mip_index].size());

                    // Update memory requirements
                    uint64_t memory_required = mip_width * mip_height * m_channels * (m_bpc / 8);
                    mip_memory[array_index + mip_index] = memory_required;
                    buffer_size += memory_required;
                }
            }
            
            // Create staging buffer
            if (!vulkan_common::buffer::create(
                rhi_context,
                stating_buffer,
                staging_buffer_memory,
                buffer_size,
                VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
            )) return false;

            // Copy mip levels to buffer
            void* data = nullptr;
            offset = 0;
            if (vulkan_common::error::check(vkMapMemory(rhi_context->device, static_cast<VkDeviceMemory>(staging_buffer_memory), 0, buffer_size, 0, &data)))
            {
                for (uint32_t array_index = 0; array_index < m_array_size; array_index++)
                {
                    for (uint32_t mip_level = 0; mip_level < m_mip_levels; mip_level++)
                    {
                        uint32_t index = array_index + mip_level;
                        memcpy(static_cast<byte*>(data) + offset, m_data[index].data(), mip_memory[index]);
                        offset += mip_memory[index];
                    }
                }

                vkUnmapMemory(rhi_context->device, static_cast<VkDeviceMemory>(staging_buffer_memory));
            }

            // Transition to RHI_Image_Transfer_Dst_Optimal
            RHI_Image_Layout copy_layout = RHI_Image_Transfer_Dst_Optimal;
            if (!vulkan_common::image::set_layout(m_rhi_device.get(), cmd_buffer, this, copy_layout))
                return false;

            // Update layout
            m_layout = copy_layout;

            // Copy buffer to texture
            vkCmdCopyBufferToImage(
                cmd_buffer,
                *reinterpret_cast<VkBuffer*>(&stating_buffer),
                static_cast<VkImage>(Get_Texture()),
                vulkan_image_layout[copy_layout],
                static_cast<uint32_t>(buffer_image_copies.size()),
                buffer_image_copies.data()
            );
        }

        // Transition to target layout
        {
            // Deduce target layout
            RHI_Image_Layout target_layout = m_layout;

            if (IsSampled() && IsColorFormat())
                target_layout = RHI_Image_Shader_Read_Only_Optimal;

            if (IsRenderTargetColor())
                target_layout = RHI_Image_Color_Attachment_Optimal;

            if (IsRenderTargetDepthStencil())
                target_layout = RHI_Image_Depth_Stencil_Attachment_Optimal;

            // Transition
            if (!vulkan_common::image::set_layout(m_rhi_device.get(), cmd_buffer, this, target_layout))
                return false;

            // Flush
            if (!vulkan_common::command_buffer_immediate::end(RHI_Queue_Graphics))
                return false;

            // Update layout
            m_layout = target_layout;
        }

        // Free staging resources (must happen after we flush the command buffer
        vulkan_common::buffer::destroy(rhi_context, stating_buffer);
        vulkan_common::memory::free(rhi_context, staging_buffer_memory);

        // Create image views
        {
            string name = GetResourceName();

            // Sampled
            if (IsSampled())
            {
                name += name.empty() ? "sampled" : "-sampled";

                // Unlike D3D11, Vulkan doesn't support a single view for a depth-stencil buffer, so we have to create a separate one for the stencil

                if (IsColorFormat() || IsDepthFormat())
                {
                    if (!vulkan_common::image::view::create(rhi_context, *image, m_view_texture[0], this, true))
                        return false;
                }

                if (IsStencilFormat())
                {
                    if (!vulkan_common::image::view::create(rhi_context, *image, m_view_texture[1], this, false, true))
                        return false;
                }
            }

            // Color and depth-stencil
            if (IsRenderTargetColor() || IsRenderTargetDepthStencil())
            {
                name += name.empty() ? "render_target" : "-render_target";
                // Unlike D3D11, Vulkan uses a framebuffer instead instead of dedicated views for attachments, so nothing to do here
            }

            // Name the image and image view
            vulkan_common::debug::set_image_name(rhi_context->device, *image, name.c_str());
            vulkan_common::debug::set_image_view_name(rhi_context->device, static_cast<VkImageView>(m_view_texture[0]), name.c_str());
            if (IsSampled() && IsStencilFormat())
            {
                vulkan_common::debug::set_image_view_name(rhi_context->device, static_cast<VkImageView>(m_view_texture[1]), name.c_str());
            }
        }

		return true;
	}

	// TEXTURE CUBE

	RHI_TextureCube::~RHI_TextureCube()
	{
		m_data.clear();
        vkDestroyImageView(m_rhi_device->GetContextRhi()->device, reinterpret_cast<VkImageView>(m_view_texture), nullptr);
		vkDestroyImage(m_rhi_device->GetContextRhi()->device, reinterpret_cast<VkImage>(m_texture), nullptr);
		vulkan_common::memory::free(m_rhi_device->GetContextRhi(), m_resource_memory);
	}

	bool RHI_TextureCube::CreateResourceGpu()
	{
		return true;
	}
}
#endif
