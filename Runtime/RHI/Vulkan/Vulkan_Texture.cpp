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

	inline VkCommandBuffer BeginSingleTimeCommands(const shared_ptr<RHI_Device>& rhi_device, VkCommandPool& command_pool) 
	{
		VkCommandBuffer command_buffer;
		Vulkan_Common::commands::cmd_buffer(rhi_device, command_buffer, command_pool, VK_COMMAND_BUFFER_LEVEL_PRIMARY);

		VkCommandBufferBeginInfo begin_info	= {};
		begin_info.sType					= VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
		begin_info.flags					= VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
		if (vkBeginCommandBuffer(command_buffer, &begin_info) != VK_SUCCESS)
		{
			LOG_ERROR("Failed to begin command buffer.");
			return nullptr;
		}

		return command_buffer;
	}

	inline bool EndSingleTimeCommands(const shared_ptr<RHI_Device>& rhi_device, VkCommandPool& command_pool, VkQueue& queue, VkCommandBuffer& command_buffer)
	{
		auto result = vkEndCommandBuffer(command_buffer);
		if (result != VK_SUCCESS)
		{
			LOG_ERROR(Vulkan_Common::to_string(result));
			return false;
		}

		VkSubmitInfo submit_info		= {};
		submit_info.sType				= VK_STRUCTURE_TYPE_SUBMIT_INFO;
		submit_info.commandBufferCount	= 1;
		submit_info.pCommandBuffers		= &command_buffer;

		result = vkQueueSubmit(queue, 1, &submit_info, nullptr);
		if (result != VK_SUCCESS)
		{
			LOG_ERROR(Vulkan_Common::to_string(result));
			return false;
		}

		result = vkQueueWaitIdle(queue);
		if (result != VK_SUCCESS)
		{
			LOG_ERROR(Vulkan_Common::to_string(result));
			return false;
		}

		vkFreeCommandBuffers(rhi_device->GetContextRhi()->device, command_pool, 1, &command_buffer);

		return true;
	}

	inline bool TransitionImageLayout(VkCommandBuffer& cmd_buffer, VkImage& image, const VkImageLayout oldLayout, const VkImageLayout newLayout) 
	{
		VkImageMemoryBarrier barrier			= {};
		barrier.sType							= VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
		barrier.oldLayout						= oldLayout;
		barrier.newLayout						= newLayout;
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

		if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED && newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) 
		{
			barrier.srcAccessMask	= 0;
			barrier.dstAccessMask	= VK_ACCESS_TRANSFER_WRITE_BIT;
			source_stage			= VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
			destination_stage		= VK_PIPELINE_STAGE_TRANSFER_BIT;
		}
		else if (oldLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL && newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) 
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

	inline bool CopyBufferToImage(const shared_ptr<RHI_Device>& rhi_device, uint32_t width, uint32_t height, VkImage& image, VkBuffer& staging_buffer, VkCommandPool& cmd_pool)
	{
		auto queue = rhi_device->GetContextRhi()->queue_copy;

		auto cmd_buffer = BeginSingleTimeCommands(rhi_device, cmd_pool);
		if (!cmd_buffer)
			return false;

		if (!TransitionImageLayout(cmd_buffer, image, VK_IMAGE_LAYOUT_UNDEFINED,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL))
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

		vkCmdCopyBufferToImage(cmd_buffer, staging_buffer, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
	
		if (!TransitionImageLayout(cmd_buffer, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL))
			return false;

		if (!EndSingleTimeCommands(rhi_device, cmd_pool, queue, cmd_buffer))
			return false;

		return true;
	}

    inline VkResult CreateFrameBuffer(
        const shared_ptr<RHI_Device>& rhi_device,
        const vector<VkAttachmentDescription>& attachment_descriptions,
        const VkSubpassDescription& subpass_description,
        const vector<VkSubpassDependency>& subpass_dependencies,
        VkRenderPass& _render_pass,
        VkFramebuffer& _frame_buffer,
        const uint32_t width,
        const uint32_t height)
    {
        // Render pass
        VkRenderPassCreateInfo renderPassInfo   = {};
        renderPassInfo.sType                    = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        renderPassInfo.attachmentCount          = static_cast<uint32_t>(attachment_descriptions.size());
        renderPassInfo.pAttachments             = attachment_descriptions.data();
        renderPassInfo.subpassCount             = 1;
        renderPassInfo.pSubpasses               = &subpass_description;
        renderPassInfo.dependencyCount          = static_cast<uint32_t>(subpass_dependencies.size());
        renderPassInfo.pDependencies            = subpass_dependencies.data();

        auto result = vkCreateRenderPass(rhi_device->GetContextRhi()->device, &renderPassInfo, nullptr, &_render_pass);
        if (result != VK_SUCCESS)
            return result;

        // Framebuffer
        VkImageView attachments[2];
        //attachments[0] = offscreenPass.color.view;
        //attachments[1] = offscreenPass.depth.view;

        VkFramebufferCreateInfo create_info = {};
        create_info.renderPass              = _render_pass;
        create_info.attachmentCount         = 2;
        create_info.pAttachments            = attachments;
        create_info.width                   = width;
        create_info.height                  = height;
        create_info.layers                  = 1;

        result = vkCreateFramebuffer(rhi_device->GetContextRhi()->device, &create_info, nullptr, &_frame_buffer);

        return result;
    }

	inline VkResult CreateImage(
		const shared_ptr<RHI_Device>& rhi_device,
		VkImage* _image,
		VkDeviceMemory* image_memory,
		const uint32_t width,
		const uint32_t height,
		const VkFormat format,
		const VkImageTiling tiling,
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
		create_info.initialLayout		= VK_IMAGE_LAYOUT_UNDEFINED;
		create_info.usage				= usage;
		create_info.samples				= VK_SAMPLE_COUNT_1_BIT;
		create_info.sharingMode			= VK_SHARING_MODE_EXCLUSIVE;

		auto result = vkCreateImage(rhi_device->GetContextRhi()->device, &create_info, nullptr, _image);
		if (result != VK_SUCCESS)
			return result;

		VkMemoryRequirements memRequirements;
		vkGetImageMemoryRequirements(rhi_device->GetContextRhi()->device, *_image, &memRequirements);

		VkMemoryAllocateInfo allocate_info	= {};
		allocate_info.sType					= VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
		allocate_info.allocationSize		= memRequirements.size;
		allocate_info.memoryTypeIndex		= Vulkan_Common::memory::get_type(rhi_device->GetContextRhi()->device_physical, properties, memRequirements.memoryTypeBits);

		result = vkAllocateMemory(rhi_device->GetContextRhi()->device, &allocate_info, nullptr, image_memory);
		if (result != VK_SUCCESS)
			return result;

		result = vkBindImageMemory(rhi_device->GetContextRhi()->device, *_image, *image_memory, 0);
		if (result != VK_SUCCESS)
			return result;

		return result;
	}

    inline VkResult CreateImageView(const shared_ptr<RHI_Device>& rhi_device, VkImage* image, VkImageView* image_view, const VkFormat format, const uint16_t bind_flags, const bool swizzle = false)
    {
        // Resolve aspect mask
        VkImageAspectFlags aspect_mask = 0;
        if (bind_flags & RHI_Texture_DepthStencil)
        {
            // Depth-only image formats can have only the VK_IMAGE_ASPECT_DEPTH_BIT set
            aspect_mask = VK_IMAGE_ASPECT_DEPTH_BIT;
        }
        else
        {
            aspect_mask |= (bind_flags & RHI_Texture_Sampled)       ? VK_IMAGE_ASPECT_COLOR_BIT : 0;
            aspect_mask |= (bind_flags & RHI_Texture_RenderTarget)  ? VK_IMAGE_ASPECT_COLOR_BIT : 0; 
        }
        
        VkImageViewCreateInfo create_info           = {};
        create_info.sType                           = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        create_info.image                           = *image;
        create_info.viewType                        = VK_IMAGE_VIEW_TYPE_2D;
        create_info.format                          = format;
        create_info.subresourceRange.aspectMask     = aspect_mask;
        create_info.subresourceRange.baseMipLevel   = 0;
        create_info.subresourceRange.levelCount     = 1;
        create_info.subresourceRange.baseArrayLayer = 0;
        create_info.subresourceRange.layerCount     = 1;
        if (swizzle)
        {
            create_info.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
            create_info.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
            create_info.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
            create_info.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
        }

        return vkCreateImageView(rhi_device->GetContextRhi()->device, &create_info, nullptr, image_view);
    }

	bool RHI_Texture2D::CreateResourceGpu()
	{
        // In case of a render target or a depth-stencil buffer, ensure the requested format is supported by the device
        VkFormat image_format       = vulkan_format[m_format];
        VkImageTiling image_tiling  = VK_IMAGE_TILING_LINEAR; // VK_IMAGE_TILING_OPTIMAL is not supported with VK_FORMAT_R32G32B32_SFLOAT
        {
            if (m_bind_flags & RHI_Texture_RenderTarget)
            {
                LOG_WARNING("Format is not supported as a render target, falling back to supported surface format");
                image_format = m_rhi_device->GetContextRhi()->surface_format.format;
                image_tiling = VK_IMAGE_TILING_OPTIMAL;
            }

            if (m_bind_flags & RHI_Texture_DepthStencil)
            {
                LOG_WARNING("Format is not supported as depth-stencil, falling back to supported surface format");
                image_format = VK_FORMAT_D32_SFLOAT; // fix that shit
                image_tiling = VK_IMAGE_TILING_OPTIMAL;
            }
        }

		// Copy data to a buffer (if there are any)
		VkBuffer staging_buffer = nullptr;
		VkDeviceMemory staging_buffer_memory = nullptr;
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
		auto image = reinterpret_cast<VkImage*>(&m_texture);
        auto image_memory = reinterpret_cast<VkDeviceMemory*>(&m_texture_memory);
		auto result = CreateImage(
				m_rhi_device,
				image,
				image_memory,
				m_width,
				m_height,
                image_format,
                image_tiling,
				usage_flags,
				VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
			);
		if (result != VK_SUCCESS)
		{
			LOG_ERROR("Failed to create image, %s", Vulkan_Common::to_string(result));
			return false;
		}

		// Copy buffer to image
		if (staging_buffer)
		{
			// Create command pool
			void* cmd_pool_void;
			Vulkan_Common::commands::cmd_pool(m_rhi_device, cmd_pool_void);
			auto cmd_pool = static_cast<VkCommandPool>(cmd_pool_void);

			// Copy
			lock_guard<mutex> lock(m_mutex); // Mutex prevents this error: THREADING ERROR : object of type VkQueue is simultaneously used in thread 0xfe0 and thread 0xe18
			if (!CopyBufferToImage(m_rhi_device, static_cast<uint32_t>(m_width), static_cast<uint32_t>(m_height), *image, staging_buffer, cmd_pool))
			{
				LOG_ERROR("Failed to copy buffer to image");
				return false;
			}
		}

        // RENDER TARGET
        if (m_bind_flags & RHI_Texture_RenderTarget)
        {
            result = CreateImageView(m_rhi_device, image, reinterpret_cast<VkImageView*>(&m_resource_render_target), image_format, m_bind_flags);
            if (result != VK_SUCCESS)
            {
                LOG_ERROR("Failed to create render target view, %s", Vulkan_Common::to_string(result));
                return false;
            }
        }

        // DEPTH-STENCIL
        if (m_bind_flags & RHI_Texture_DepthStencil)
        {
            auto depth_stencil = m_resource_depth_stencils.emplace_back(nullptr);
            result = CreateImageView(m_rhi_device, image, reinterpret_cast<VkImageView*>(&depth_stencil), image_format, m_bind_flags);
            if (result != VK_SUCCESS)
            {
                LOG_ERROR("Failed to create depth stencil view, %s", Vulkan_Common::to_string(result));
                return false;
            }
        }

        // SAMPLED
        if (m_bind_flags & RHI_Texture_Sampled)
        {
            result = CreateImageView(m_rhi_device, image, reinterpret_cast<VkImageView*>(&m_resource_texture), image_format, m_bind_flags);
            if (result != VK_SUCCESS)
            {
                LOG_ERROR("Failed to create sampled image view, %s", Vulkan_Common::to_string(result));
                return false;
            }
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
