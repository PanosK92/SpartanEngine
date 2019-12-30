/*
Copyright(c) 2016-2019 Panos Karabelas

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
#include "../RHI_Implementation.h"
#ifdef API_GRAPHICS_VULKAN
//================================

//= INCLUDES =====================
#include "../RHI_Device.h"
#include "../RHI_Texture2D.h"
#include "../RHI_TextureCube.h"
#include "../../Math/MathHelper.h"
//================================

//= NAMESPACES ===============
using namespace std;
using namespace Spartan::Math;
//============================

namespace Spartan
{
    // Images can be loaded in parallel but will be consumed by a single queue, so fifo.
    static mutex g_mutex;

    RHI_Texture2D::~RHI_Texture2D()
    {
        if (!m_rhi_device->IsInitialized())
            return;

        m_rhi_device->Flush();
        m_data.clear();
        auto rhi_context = m_rhi_device->GetContextRhi();
        vulkan_common::image_view::destroy(rhi_context, m_resource_texture);
        vulkan_common::image_view::destroy(rhi_context, m_resource_depth_stencils);
        vulkan_common::frame_buffer::destroy(rhi_context, m_resource_render_target);
        vulkan_common::image::destroy(rhi_context, m_texture);
		vulkan_common::memory::free(m_rhi_device->GetContextRhi(), m_texture_memory);
	}

    inline bool CopyBufferToImage(const RHI_Device* rhi_device, const uint32_t width, const uint32_t height, VkImage& image, const RHI_Image_Layout layout, VkBuffer* staging_buffer)
    {
        lock_guard lock(g_mutex);

        RHI_Context* rhi_context = rhi_device->GetContextRhi();

        // Create command buffer
        VkCommandBuffer cmd_buffer = vulkan_common::command_buffer::begin(rhi_context, rhi_context->queue_graphics_family_index);

        // Transition to RHI_Image_Transfer_Dst_Optimal
		if (!vulkan_common::image::transition_layout(rhi_device, cmd_buffer, image, layout, RHI_Image_Transfer_Dst_Optimal))
			return false;
		
		// Copy buffer to texture	
		VkBufferImageCopy region				= {};
		region.bufferOffset						= 0;
		region.bufferRowLength					= 0;
		region.bufferImageHeight				= 0;
		region.imageSubresource.aspectMask		= VK_IMAGE_ASPECT_COLOR_BIT;
		region.imageSubresource.mipLevel		= 0;
		region.imageSubresource.baseArrayLayer	= 0;
		region.imageSubresource.layerCount		= 1;
		region.imageOffset						= { 0, 0, 0 };
		region.imageExtent						= { width, height, 1 };
		vkCmdCopyBufferToImage(cmd_buffer, *staging_buffer, image, vulkan_image_layout[RHI_Image_Transfer_Dst_Optimal], 1, &region);

        // Transition to RHI_Image_Shader_Read_Only_Optimal
		if (!vulkan_common::image::transition_layout(rhi_device, cmd_buffer, image, RHI_Image_Transfer_Dst_Optimal, RHI_Image_Shader_Read_Only_Optimal))
			return false;

        // Flush and free command buffer
		return vulkan_common::command_buffer::flush(rhi_context, rhi_context->queue_graphics);
	}

	bool RHI_Texture2D::CreateResourceGpu()
	{
        const RHI_Context* rhi_context = m_rhi_device->GetContextRhi();

        // Get format support
        VkFormatFeatureFlags feature_flag   = (m_bind_flags & RHI_Texture_DepthStencil) ? VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT : VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT;
        VkImageTiling image_tiling          = vulkan_common::image::is_format_supported(rhi_context, m_format, feature_flag);

        // If the format is not supported, early exit
        if (image_tiling == VK_IMAGE_TILING_MAX_ENUM)
        {
            LOG_ERROR("Format %s is not supported by the GPU.", rhi_format_to_string(m_format));
            return false;
        }

        // If the format is not optimal, let the user know
        if (image_tiling != VK_IMAGE_TILING_OPTIMAL)
        {
            LOG_WARNING("Format %s does not support optimal tiling, considering switching to a more efficient format.", rhi_format_to_string(m_format));
        }

        // Resolve usage flags
        VkImageUsageFlags usage_flags = 0;
        {
            usage_flags |= (m_bind_flags & RHI_Texture_Sampled)         ? VK_IMAGE_USAGE_SAMPLED_BIT                    : 0;
            usage_flags |= (m_bind_flags & RHI_Texture_DepthStencil)    ? VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT   : 0;
            usage_flags |= (m_bind_flags & RHI_Texture_RenderTarget)    ? VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT           : 0;
        }

        // Create image
        bool use_staging    = image_tiling == VK_IMAGE_TILING_OPTIMAL;
        m_layout            = RHI_Image_Preinitialized;
        auto image          = reinterpret_cast<VkImage*>(&m_texture);
        auto image_memory   = reinterpret_cast<VkDeviceMemory*>(&m_texture_memory);
        if (use_staging)
        {
            usage_flags |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT; // specifies that the image can be used as the source of a transfer command.
            usage_flags |= VK_IMAGE_USAGE_TRANSFER_DST_BIT; // specifies that the image can be used as the destination of a transfer command.

            // Copy data to staging buffer (if there are any)
            void* staging_buffer        = nullptr;
            void* staging_buffer_memory = nullptr;
            if (!m_data.empty())
            {
                VkDeviceSize buffer_size = static_cast<uint64_t>(m_width)* static_cast<uint64_t>(m_height)* static_cast<uint64_t>(m_channels);

                // Create buffer
                if (!vulkan_common::buffer::create(rhi_context, static_cast<void*>(staging_buffer), staging_buffer_memory, buffer_size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT))
                    return false;

                // Copy to buffer
                void* data = nullptr;
                if (vulkan_common::error::check_result(vkMapMemory(rhi_context->device, static_cast<VkDeviceMemory>(staging_buffer_memory), 0, buffer_size, 0, &data)))
                {
                    memcpy(data, m_data.front().data(), static_cast<size_t>(buffer_size));
                    vkUnmapMemory(rhi_context->device, static_cast<VkDeviceMemory>(staging_buffer_memory));
                }
            }

            // Create image
            if (!vulkan_common::image::create(rhi_context, *image, m_width, m_height, vulkan_format[m_format], image_tiling, m_layout, usage_flags))
            {
                LOG_ERROR("Failed to create image");
                return false;
            }

            // Allocate memory and bind it
            if (!vulkan_common::image::allocate_bind(rhi_context, *image, image_memory))
            {
                LOG_ERROR("Failed to allocate and bind image memory");
                return false;
            }

            // Copy buffer to image
            if (staging_buffer)
            {
                if (!CopyBufferToImage(m_rhi_device.get(), static_cast<uint32_t>(m_width), static_cast<uint32_t>(m_height), *image, m_layout, reinterpret_cast<VkBuffer*>(&staging_buffer)))
                {
                    LOG_ERROR("Failed to copy buffer to image");
                    return false;
                }
            }

            // Release staging buffer
            if (staging_buffer && staging_buffer_memory)
            {
                vulkan_common::buffer::destroy(rhi_context, staging_buffer);
                vulkan_common::memory::free(rhi_context, staging_buffer_memory);
            }
        }
        else
        {
            // Should I support this ?
            LOG_ERROR("Non staged images not supported yet");
            return false;
        }

        string name = GetResourceName();

        // Create image views
        {
            VkImageAspectFlags aspect_flags = vulkan_common::image::bind_flags_to_aspect_mask(m_bind_flags);

            m_layout = RHI_Image_Shader_Read_Only_Optimal;

            // SHADER RESOURCE VIEW
            if (m_bind_flags & RHI_Texture_Sampled)
            {
                name += name.empty() ? "sampled" : "-sampled";

                if (!vulkan_common::image_view::create(rhi_context, *image, m_resource_texture, vulkan_format[m_format], aspect_flags))
                    return false;
            }

            // DEPTH-STENCIL VIEW
            if (m_bind_flags & RHI_Texture_DepthStencil)
            {
                name += name.empty() ? "depth_stencil" : "-depth_stencil";
                m_layout = RHI_Image_Depth_Stencil_Read_Only_Optimal;

                auto depth_stencil = m_resource_depth_stencils.emplace_back(nullptr);
                if (!vulkan_common::image_view::create(rhi_context, *image, depth_stencil, vulkan_format[m_format], aspect_flags))
                    return false;
            }

            // RENDER TARGET VIEW
            if (m_bind_flags & RHI_Texture_RenderTarget)
            {
                name += name.empty() ? "render_target" : "-render_target";
            }
        }

        // Name the image and image view
        vulkan_common::debug::set_image_name(rhi_context->device, *image, name.c_str());
        vulkan_common::debug::set_image_view_name(rhi_context->device, static_cast<VkImageView>(m_resource_texture), name.c_str());

		return true;
	}

	// TEXTURE CUBE

	RHI_TextureCube::~RHI_TextureCube()
	{
		m_data.clear();
        vkDestroyImageView(m_rhi_device->GetContextRhi()->device, reinterpret_cast<VkImageView>(m_resource_texture), nullptr);
		vkDestroyImage(m_rhi_device->GetContextRhi()->device, reinterpret_cast<VkImage>(m_texture), nullptr);
		vulkan_common::memory::free(m_rhi_device->GetContextRhi(), m_texture_memory);
	}

	bool RHI_TextureCube::CreateResourceGpu()
	{
		return true;
	}
}
#endif
