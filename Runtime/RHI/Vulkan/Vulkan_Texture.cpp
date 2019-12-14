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
    RHI_Texture2D::~RHI_Texture2D()
    {
        m_data.clear();
        auto rhi_context = m_rhi_device->GetContextRhi();

        vulkan_common::image_view::destroy(rhi_context, m_resource_texture);
        vulkan_common::image_view::destroy(rhi_context, m_resource_depth_stencils);
        vulkan_common::frame_buffer::destroy(rhi_context, m_resource_render_target);
        vulkan_common::render_pass::destroy(m_rhi_device->GetContextRhi(), m_resource_render_pass);
        vulkan_common::image::destroy(rhi_context, m_texture);
		vulkan_common::memory::free(m_rhi_device->GetContextRhi(), m_texture_memory);
	}

	inline bool TransitionImageLayout(VkCommandBuffer& cmd_buffer, VkImage& image, const RHI_Image_Layout layout_old, const RHI_Image_Layout layout_new)
	{
		VkImageMemoryBarrier barrier			= {};
		barrier.sType							= VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
		barrier.oldLayout						= vulkan_image_layout[layout_old];
		barrier.newLayout						= vulkan_image_layout[layout_new];
		barrier.srcQueueFamilyIndex				= VK_QUEUE_FAMILY_IGNORED;
		barrier.dstQueueFamilyIndex				= VK_QUEUE_FAMILY_IGNORED;
		barrier.image							= image;
		barrier.subresourceRange.aspectMask		= VK_IMAGE_ASPECT_COLOR_BIT;
		barrier.subresourceRange.baseMipLevel	= 0;
		barrier.subresourceRange.levelCount		= 1;
		barrier.subresourceRange.baseArrayLayer = 0;
		barrier.subresourceRange.layerCount		= 1;

		VkPipelineStageFlags source_stage;
		VkPipelineStageFlags destination_stage;

		if (barrier.oldLayout == VK_IMAGE_LAYOUT_PREINITIALIZED && barrier.newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL)
		{
			barrier.srcAccessMask	= 0;
			barrier.dstAccessMask	= VK_ACCESS_TRANSFER_WRITE_BIT;
			source_stage			= VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
			destination_stage		= VK_PIPELINE_STAGE_TRANSFER_BIT;
		}
		else if (barrier.oldLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL && barrier.newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
		{
			barrier.srcAccessMask	= VK_ACCESS_TRANSFER_WRITE_BIT;
			barrier.dstAccessMask	= VK_ACCESS_SHADER_READ_BIT;
			source_stage			= VK_PIPELINE_STAGE_TRANSFER_BIT;
			destination_stage		= VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
		}
		else 
		{
			LOG_ERROR("Unsupported layout transition");
			return false;
		}

		vkCmdPipelineBarrier
		(
			cmd_buffer,
			source_stage, destination_stage,
			0,
			0, nullptr,
			0, nullptr,
			1, &barrier
		);

		return true;
	}

    inline bool CopyBufferToImage(const RHI_Context* rhi_context, const uint32_t width, const uint32_t height, VkImage& image, RHI_Image_Layout& layout, VkBuffer& staging_buffer)
    {
        // Create command buffer
        void* command_pool      = nullptr;
        void* command_buffer[1] = { nullptr };
		if (!vulkan_common::command::begin(rhi_context, rhi_context->queue_graphics_family_index, command_pool, command_buffer[0]))
			return false;

        VkCommandBuffer cmd_buffer = static_cast<VkCommandBuffer>(command_buffer[0]);

        // Transition layout to RHI_Image_Transfer_Dst_Optimal
		if (!TransitionImageLayout(cmd_buffer, image, layout, RHI_Image_Transfer_Dst_Optimal))
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

        // Transition layout to RHI_Image_Shader_Read_Only_Optimal
		vkCmdCopyBufferToImage(cmd_buffer, staging_buffer, image, vulkan_image_layout[RHI_Image_Transfer_Dst_Optimal], 1, &region);
		if (!TransitionImageLayout(cmd_buffer, image, RHI_Image_Transfer_Dst_Optimal, RHI_Image_Shader_Read_Only_Optimal))
			return false;

        layout = RHI_Image_Shader_Read_Only_Optimal;

        // Flush and free command buffer
        bool result = vulkan_common::command::flush(command_buffer[0], rhi_context->queue_graphics);
        vulkan_common::command::free(rhi_context, command_pool, command_buffer[0]);
		return result;
	}

	bool RHI_Texture2D::CreateResourceGpu()
	{
        const RHI_Context* rhi_context = m_rhi_device->GetContextRhi();

        // Get format support
        VkFormatFeatureFlags feature_flag = (m_bind_flags & RHI_Texture_DepthStencil) ? VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT : VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT;
        VkImageTiling image_tiling = vulkan_common::image::is_format_supported(rhi_context, m_format, feature_flag);

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

            // Copy data to a buffer (if there are any)
            VkBuffer staging_buffer = nullptr;
            VkDeviceMemory staging_buffer_memory = nullptr;
            if (!m_data.empty())
            {
                VkDeviceSize buffer_size = static_cast<uint64_t>(m_width)* static_cast<uint64_t>(m_height)* static_cast<uint64_t>(m_channels);

                // Create buffer
                if (!vulkan_common::buffer::create_allocate_bind(rhi_context, staging_buffer, staging_buffer_memory, buffer_size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT))
                    return false;

                // Copy to buffer
                void* data = nullptr;
                if (vulkan_common::error::check_result(vkMapMemory(rhi_context->device, staging_buffer_memory, 0, buffer_size, 0, &data)))
                {
                    memcpy(data, m_data.front().data(), static_cast<size_t>(buffer_size));
                    vkUnmapMemory(rhi_context->device, staging_buffer_memory);
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
                // Mutex prevents this error: THREADING ERROR : object of type VkQueue is simultaneously used in thread A and thread B
                // Todo: Reduces possibility of error, but it can still occur, todo
                static mutex _mutex;
                lock_guard lock(_mutex);
                if (!CopyBufferToImage(rhi_context, static_cast<uint32_t>(m_width), static_cast<uint32_t>(m_height), *image, m_layout, staging_buffer))
                {
                    LOG_ERROR("Failed to copy buffer to image");
                    return false;
                }
            }

            // Release staging buffer
            if (staging_buffer && staging_buffer_memory)
            {
                vkDestroyBuffer(rhi_context->device, staging_buffer, nullptr);
                vkFreeMemory(rhi_context->device, staging_buffer_memory, nullptr);
            }
        }
        else
        {
            // HAVE TO COMPLETE THIS BRANCH
            LOG_ERROR("Non staged images not supported yet");
            return false;

            // Create image
            if (!vulkan_common::image::create(rhi_context, *image, m_width, m_height, vulkan_format[m_format], image_tiling, m_layout, usage_flags))
            {
                LOG_ERROR("Failed to create image");
                return false;
            }

            // Allocate memory
            VkDeviceSize memory_size = 0;
            // Allocate memory and bind it
            if (!vulkan_common::image::allocate_bind(rhi_context, *image, image_memory, &memory_size))
            {
                LOG_ERROR("Failed to allocate and bind image memory");
                return false;
            }

            // Map image memory
            void* data = nullptr;
            if (vulkan_common::error::check_result(vkMapMemory(rhi_context->device, *image_memory, 0, memory_size, 0, &data)))
            {
                VkDeviceSize buffer_size = static_cast<uint64_t>(m_width)* static_cast<uint64_t>(m_height)* static_cast<uint64_t>(m_channels);
                memcpy(data, m_data.front().data(), static_cast<size_t>(buffer_size));
                vkUnmapMemory(rhi_context->device, *image_memory);
            }
        }

        // Create image views
        {
            VkImageAspectFlags aspect_flags = vulkan_common::image::bind_flags_to_aspect_mask(m_bind_flags);

            // SHADER RESOURCE VIEW
            if (m_bind_flags & RHI_Texture_Sampled)
            {
                if (!vulkan_common::image_view::create(rhi_context, *image, m_resource_texture, vulkan_format[m_format], aspect_flags))
                    return false;
            }

            // DEPTH-STENCIL VIEW
            if (m_bind_flags & RHI_Texture_DepthStencil)
            {
                auto depth_stencil = m_resource_depth_stencils.emplace_back(nullptr);
                if (!vulkan_common::image_view::create(rhi_context, *image, depth_stencil, vulkan_format[m_format], aspect_flags))
                    return false;
            }

            // RENDER TARGET VIEW
            if (m_bind_flags & RHI_Texture_RenderTarget)
            {
                // Render pass
                if (!vulkan_common::render_pass::create(rhi_context, vulkan_format[m_format], m_resource_render_pass, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL))
                    return false;

                // Frame buffer
                vector<void*> attachments = { m_resource_texture };
                if (!vulkan_common::frame_buffer::create(rhi_context, m_resource_render_pass, attachments, m_width, m_height, m_resource_render_target))
                    return false;
            }
        }

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
