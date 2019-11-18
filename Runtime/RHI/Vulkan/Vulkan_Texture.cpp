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
    mutex RHI_Texture::m_mutex;

    RHI_Texture2D::~RHI_Texture2D()
    {
        m_data.clear();

        auto vk_device = m_rhi_device->GetContextRhi()->device;

        if (m_resource_texture)
        {
            vkDestroyImageView(vk_device, reinterpret_cast<VkImageView>(m_resource_texture), nullptr);
        }

        if (m_resource_render_target)
        {
            vkDestroyImageView(vk_device, reinterpret_cast<VkImageView>(m_resource_render_target), nullptr);
        }

        for (auto& depth_stencil : m_resource_depth_stencils)
        {
            vkDestroyImageView(vk_device, reinterpret_cast<VkImageView>(depth_stencil), nullptr);
        }

        if (m_frame_buffer)
        {
            vkDestroyFramebuffer(vk_device, reinterpret_cast<VkFramebuffer>(m_frame_buffer), nullptr);
        }

        if (m_texture)
        {
            vkDestroyImage(vk_device, reinterpret_cast<VkImage>(m_texture), nullptr);
        }

		Vulkan_Common::memory::free(m_rhi_device, m_texture_memory);
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

    inline bool CopyBufferToImage(const shared_ptr<RHI_Device>& rhi_device, const uint32_t width, const uint32_t height, VkImage& image, RHI_Image_Layout& layout, VkBuffer& staging_buffer)
    {
        // Create command buffer
        VkCommandPool command_pool          = nullptr;
        VkCommandBuffer command_buffer[1]   = { nullptr };
		if (!Vulkan_Common::command::begin(rhi_device, command_pool, command_buffer[0]))
			return false;

        // Transition layout to RHI_Image_Transfer_Dst_Optimal
		if (!TransitionImageLayout(command_buffer[0], image, layout, RHI_Image_Transfer_Dst_Optimal))
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
		vkCmdCopyBufferToImage(command_buffer[0], staging_buffer, image, vulkan_image_layout[RHI_Image_Transfer_Dst_Optimal], 1, &region);
		if (!TransitionImageLayout(command_buffer[0], image, RHI_Image_Transfer_Dst_Optimal, RHI_Image_Shader_Read_Only_Optimal))
			return false;

        layout = RHI_Image_Shader_Read_Only_Optimal;

        // Flush and free command buffer
		return Vulkan_Common::command::flush_and_free(rhi_device, command_pool, command_buffer[0]);
	}

	inline bool CreateImage(
		const shared_ptr<RHI_Device>& rhi_device,
		VkImage* _image,
		VkDeviceMemory* image_memory,
		const uint32_t width,
		const uint32_t height,
		const VkFormat format,
		const VkImageTiling tiling,
        const RHI_Image_Layout layout,
		const VkImageUsageFlags usage,
		const VkMemoryPropertyFlags properties
	)
	{
		VkImageCreateInfo create_info	= {};
		create_info.sType				= VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
		create_info.imageType			= VK_IMAGE_TYPE_2D;
		create_info.extent.width		= width;
		create_info.extent.height		= height;
		create_info.extent.depth		= 1;
		create_info.mipLevels			= 1;
		create_info.arrayLayers			= 1;
		create_info.format				= format;
		create_info.tiling				= tiling;
		create_info.initialLayout		= vulkan_image_layout[layout];
		create_info.usage				= usage;
		create_info.samples				= VK_SAMPLE_COUNT_1_BIT;
		create_info.sharingMode			= VK_SHARING_MODE_EXCLUSIVE;

		if (!Vulkan_Common::error::check_result(vkCreateImage(rhi_device->GetContextRhi()->device, &create_info, nullptr, _image)))
			return false;

		VkMemoryRequirements memory_requirements;
		vkGetImageMemoryRequirements(rhi_device->GetContextRhi()->device, *_image, &memory_requirements);

		VkMemoryAllocateInfo allocate_info	= {};
		allocate_info.sType					= VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
		allocate_info.allocationSize		= memory_requirements.size;
		allocate_info.memoryTypeIndex		= Vulkan_Common::memory::get_type(rhi_device->GetContextRhi()->device_physical, properties, memory_requirements.memoryTypeBits);

		if (!Vulkan_Common::error::check_result(vkAllocateMemory(rhi_device->GetContextRhi()->device, &allocate_info, nullptr, image_memory)))
			return false;

		if (!Vulkan_Common::error::check_result(vkBindImageMemory(rhi_device->GetContextRhi()->device, *_image, *image_memory, 0)))
			return false;

		return true;
	}

	bool RHI_Texture2D::CreateResourceGpu()
	{
        // In case of a render target or a depth-stencil buffer, ensure the requested format is supported by the device
        VkFormat image_format       = vulkan_format[m_format];
        m_layout                    = RHI_Image_Preinitialized;
        VkImageTiling image_tiling  = VK_IMAGE_TILING_LINEAR;

        // Work on this later
        //bool use_staging = true;

        // Get format properties
        VkFormatProperties format_properties;
        vkGetPhysicalDeviceFormatProperties(m_rhi_device->GetContextRhi()->device_physical, image_format, &format_properties);

        // Check tiling support
        {
            if (m_bind_flags & RHI_Texture_RenderTarget)
            {
                image_tiling = ((format_properties.optimalTilingFeatures & VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT) != 0) ? VK_IMAGE_TILING_OPTIMAL : VK_IMAGE_TILING_LINEAR;
            }

            if (m_bind_flags & RHI_Texture_DepthStencil)
            {
                image_tiling = ((format_properties.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) != 0) ? VK_IMAGE_TILING_OPTIMAL : VK_IMAGE_TILING_LINEAR;
            }
        }

        if (image_tiling != VK_IMAGE_TILING_OPTIMAL)
        {
            LOG_WARNING("Format %s does not support optimal tiling, considering switching to a more efficient format.", rhi_format_to_string(m_format));
        }

		// Copy data to a buffer (if there are any)
		VkBuffer staging_buffer                 = nullptr;
		VkDeviceMemory staging_buffer_memory    = nullptr;
		if (!m_data.empty())
		{
			VkDeviceSize buffer_size = static_cast<uint64_t>(m_width) * static_cast<uint64_t>(m_height) * static_cast<uint64_t>(m_channels);

			// Create buffer
			if (!Vulkan_Common::buffer::create(m_rhi_device, staging_buffer, staging_buffer_memory, buffer_size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT))
				return false;

			// Copy to buffer
			void* data = nullptr;
			vkMapMemory(m_rhi_device->GetContextRhi()->device, staging_buffer_memory, 0, buffer_size, 0, &data);
			memcpy(data, m_data.front().data(), static_cast<size_t>(buffer_size));
			vkUnmapMemory(m_rhi_device->GetContextRhi()->device, staging_buffer_memory);
		}

        // Resolve usage flags
        VkImageUsageFlags usage_flags = 0;
        {
            usage_flags |= (m_bind_flags & RHI_Texture_Sampled)         ? VK_IMAGE_USAGE_SAMPLED_BIT : 0;
            usage_flags |= (m_bind_flags & RHI_Texture_DepthStencil)    ? VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT : 0;
            usage_flags |= (m_bind_flags & RHI_Texture_RenderTarget)    ? VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT : 0;
        }
        usage_flags |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT; // specifies that the image can be used as the source of a transfer command.
        usage_flags |= VK_IMAGE_USAGE_TRANSFER_DST_BIT; // specifies that the image can be used as the destination of a transfer command.

		// Create image
		auto image          = reinterpret_cast<VkImage*>(&m_texture);
        auto image_memory   = reinterpret_cast<VkDeviceMemory*>(&m_texture_memory);
		if (!CreateImage
        (
            m_rhi_device,
            image,
            image_memory,
            m_width,
            m_height,
            image_format,
            image_tiling,
            m_layout,
            usage_flags,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
		))
		{
			return false;
		}

		// Copy buffer to image
		if (staging_buffer)
		{
			lock_guard<mutex> lock(m_mutex); // Mutex prevents this error: THREADING ERROR : object of type VkQueue is simultaneously used in thread 0xfe0 and thread 0xe18
			if (!CopyBufferToImage(m_rhi_device, static_cast<uint32_t>(m_width), static_cast<uint32_t>(m_height), *image, m_layout, staging_buffer))
			{
				LOG_ERROR("Failed to copy buffer to image");
				return false;
			}
		}

        VkImageAspectFlags aspect_flags = Vulkan_Common::image::bind_flags_to_aspect_mask(m_bind_flags);

        // RENDER TARGET
        if (m_bind_flags & RHI_Texture_RenderTarget)
        {
            if (!Vulkan_Common::image::create_view(m_rhi_device, *image, reinterpret_cast<VkImageView*>(&m_resource_render_target), image_format, aspect_flags))
                return false;
        }

        // DEPTH-STENCIL
        if (m_bind_flags & RHI_Texture_DepthStencil)
        {
            auto depth_stencil = m_resource_depth_stencils.emplace_back(nullptr);
            if (!Vulkan_Common::image::create_view(m_rhi_device, *image, reinterpret_cast<VkImageView*>(&depth_stencil), image_format, aspect_flags))
                return false;
        }

        // SAMPLED
        if (m_bind_flags & RHI_Texture_Sampled)
        {
            if (!Vulkan_Common::image::create_view(m_rhi_device, *image, reinterpret_cast<VkImageView*>(&m_resource_texture), image_format, aspect_flags))
                return false;
        }

		// Release staging buffer
		if (staging_buffer && staging_buffer_memory)
		{
			vkDestroyBuffer(m_rhi_device->GetContextRhi()->device, staging_buffer, nullptr);
			vkFreeMemory(m_rhi_device->GetContextRhi()->device, staging_buffer_memory, nullptr);
		}

		return true;
	}

	// TEXTURE CUBE

	RHI_TextureCube::~RHI_TextureCube()
	{
		m_data.clear();
        vkDestroyImageView(m_rhi_device->GetContextRhi()->device, reinterpret_cast<VkImageView>(m_resource_texture), nullptr);
		vkDestroyImage(m_rhi_device->GetContextRhi()->device, reinterpret_cast<VkImage>(m_texture), nullptr);
		Vulkan_Common::memory::free(m_rhi_device, m_texture_memory);
	}

	bool RHI_TextureCube::CreateResourceGpu()
	{
		return true;
	}
}
#endif
