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
#include "../../Math/MathHelper.h"
//================================

//= NAMESPAECES =======================
using namespace std;
using namespace Spartan::Math::Helper;
//=====================================

namespace Spartan
{
	mutex RHI_Texture::m_mutex;

	RHI_Texture2D::~RHI_Texture2D()
	{
		m_data.clear();
		Vulkan_Common::image_view::destroy(m_rhi_device, m_resource_texture);
		Vulkan_Common::image::destroy(m_rhi_device, m_texture);
		Vulkan_Common::memory::free(m_rhi_device, m_texture_memory);
	}

	VkCommandBuffer BeginSingleTimeCommands(const std::shared_ptr<RHI_Device>& rhi_device, VkCommandPool& command_pool) 
	{
		VkCommandBuffer command_buffer;
		if (!Vulkan_Common::commands::cmd_buffer(rhi_device, command_buffer, command_pool, VK_COMMAND_BUFFER_LEVEL_PRIMARY))
		{
			LOG_ERROR("Failed to create command buffer.");
			return nullptr;
		}

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

	bool EndSingleTimeCommands(const std::shared_ptr<RHI_Device>& rhi_device, VkCommandPool& command_pool, VkQueue& queue, VkCommandBuffer& command_buffer)
	{
		auto result = vkEndCommandBuffer(command_buffer);
		if (result != VK_SUCCESS)
		{
			LOG_ERROR(Vulkan_Common::result_to_string(result));
			return false;
		}

		VkSubmitInfo submit_info		= {};
		submit_info.sType				= VK_STRUCTURE_TYPE_SUBMIT_INFO;
		submit_info.commandBufferCount	= 1;
		submit_info.pCommandBuffers		= &command_buffer;

		result = vkQueueSubmit(queue, 1, &submit_info, VK_NULL_HANDLE);
		if (result != VK_SUCCESS)
		{
			LOG_ERROR(Vulkan_Common::result_to_string(result));
			return false;
		}

		result = vkQueueWaitIdle(queue);
		if (result != VK_SUCCESS)
		{
			LOG_ERROR(Vulkan_Common::result_to_string(result));
			return false;
		}

		vkFreeCommandBuffers(rhi_device->GetContext()->device, command_pool, 1, &command_buffer);

		return true;
	}

	inline bool TransitionImageLayout(
		const std::shared_ptr<RHI_Device>& rhi_device,
		VkCommandPool& command_pool,
		VkCommandBuffer& cmd_buffer,
		VkQueue& queue,
		VkImage& image,
		VkFormat format,
		VkImageLayout oldLayout,
		VkImageLayout newLayout
	) 
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

	inline bool CopyBufferToImage(const std::shared_ptr<RHI_Device>& rhi_device, uint32_t width, uint32_t height, VkFormat format, VkImage& image, VkBuffer& staging_buffer, VkCommandPool& cmd_pool)
	{
		auto queue = rhi_device->GetContext()->queue_copy;

		VkCommandBuffer cmd_buffer = BeginSingleTimeCommands(rhi_device, cmd_pool);
		if (!cmd_buffer)
			return false;

		if (!TransitionImageLayout(rhi_device, cmd_pool, cmd_buffer,queue, image, format, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL))
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
	
		if (!TransitionImageLayout(rhi_device, cmd_pool, cmd_buffer, queue, image, format, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL))
			return false;

		if (!EndSingleTimeCommands(rhi_device, cmd_pool, queue, cmd_buffer))
			return false;

		return true;
	}

	bool RHI_Texture2D::CreateResourceGpu()
	{
		if (m_data.empty())
		{
			LOG_WARNING("Texture has no data, which means that this is a render texture that has to be implemented.");
			return false;
		}

		VkDeviceSize buffer_size = static_cast<uint64_t>(m_width) * static_cast<uint64_t>(m_height) * static_cast<uint64_t>(m_channels);

		// Create image memory
		VkBuffer staging_buffer	= nullptr;
		VkDeviceMemory staging_buffer_memory = nullptr;
		if (!Vulkan_Common::buffer::create(m_rhi_device, staging_buffer, staging_buffer_memory, buffer_size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT))
			return false;

		// Copy bytes to image memory
		void* data = nullptr;
		vkMapMemory(m_rhi_device->GetContext()->device, staging_buffer_memory, 0, buffer_size, 0, &data);
		memcpy(data, m_data.front().data(), static_cast<size_t>(buffer_size));
		vkUnmapMemory(m_rhi_device->GetContext()->device, staging_buffer_memory);

		// Create image
		VkImage image = nullptr;
		VkDeviceMemory image_memory = nullptr;
		if (!Vulkan_Common::image::create
			(
				m_rhi_device,
				image,
				image_memory,
				m_width,
				m_height,
				vulkan_format[m_format],
				VK_IMAGE_TILING_LINEAR, // VK_IMAGE_TILING_OPTIMAL is not supported with VK_FORMAT_R32G32B32_SFLOAT
				VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
				VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
			)
		) return false;
		
		// Create command pool
		void* cmd_pool_void;
		if (!Vulkan_Common::commands::cmd_pool(m_rhi_device, cmd_pool_void))
		{
			LOG_ERROR("Failed to create command pool.");
			return false;
		}
		auto cmd_pool = static_cast<VkCommandPool>(cmd_pool_void);

		// Copy buffer to texture
		lock_guard<mutex> lock(m_mutex); // Mutex prevents this error: THREADING ERROR : object of type VkQueue is simultaneously used in thread 0xfe0 and thread 0xe18
		if (!CopyBufferToImage(m_rhi_device, static_cast<uint32_t>(m_width), static_cast<uint32_t>(m_height), vulkan_format[m_format], image, staging_buffer, cmd_pool))
		{
			LOG_ERROR("Failed to copy buffer to image");
			return false;
		}

		// Create image view
		VkImageView image_view = nullptr;
		if (!Vulkan_Common::image_view::create(m_rhi_device, image, image_view, vulkan_format[m_format]))
		{
			LOG_ERROR("Failed to create image view");
			return false;
		}

		vkDestroyBuffer(m_rhi_device->GetContext()->device, staging_buffer, nullptr);
		vkFreeMemory(m_rhi_device->GetContext()->device, staging_buffer_memory, nullptr);

		m_resource_texture	= static_cast<void*>(image_view);
		m_texture			= static_cast<void*>(image);
		m_texture_memory	= static_cast<void*>(image_memory);

		return true;
	}
}
#endif