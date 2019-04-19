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
#include "../RHI_Texture.h"
#include "../../Math/MathHelper.h"
//================================

//= NAMESPAECES =======================
using namespace std;
using namespace Spartan::Math::Helper;
//=====================================

namespace Spartan
{
	RHI_Texture::~RHI_Texture()
	{
		ClearTextureBytes();
		Vulkan_Common::image_view::destroy(m_rhi_device.get(), m_texture_view);
		Vulkan_Common::image::destroy(m_rhi_device.get(), m_texture);
		Vulkan_Common::memory::free(m_rhi_device.get(), m_texture_memory);
	}

	VkCommandBuffer BeginSingleTimeCommands(VkDevice& device, VkCommandPool& command_pool) 
	{
		VkCommandBufferAllocateInfo allocInfo	= {};
		allocInfo.sType							= VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
		allocInfo.level							= VK_COMMAND_BUFFER_LEVEL_PRIMARY;
		allocInfo.commandPool					= command_pool;
		allocInfo.commandBufferCount			= 1;
		VkCommandBuffer commandBuffer;
		if (vkAllocateCommandBuffers(device, &allocInfo, &commandBuffer) != VK_SUCCESS)
		{
			LOG_ERROR("Failed to create command buffer.");
			return nullptr;
		}

		VkCommandBufferBeginInfo begin_info	= {};
		begin_info.sType					= VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
		begin_info.flags					= VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
		if (vkBeginCommandBuffer(commandBuffer, &begin_info) != VK_SUCCESS)
		{
			LOG_ERROR("Failed to begin command buffer.");
			return nullptr;
		}

		return commandBuffer;
	}

	void EndSingleTimeCommands(VkDevice& device, VkCommandPool& command_pool, VkQueue& queue, VkCommandBuffer& command_buffer)
	{
		vkEndCommandBuffer(command_buffer);

		VkSubmitInfo submit_info		= {};
		submit_info.sType				= VK_STRUCTURE_TYPE_SUBMIT_INFO;
		submit_info.commandBufferCount	= 1;
		submit_info.pCommandBuffers		= &command_buffer;

		vkQueueSubmit(queue, 1, &submit_info, VK_NULL_HANDLE);
		vkQueueWaitIdle(queue);

		vkFreeCommandBuffers(device, command_pool, 1, &command_buffer);
	}

	inline bool TransitionImageLayout(VkDevice& device, VkCommandPool& command_pool, VkQueue& queue, VkImage& image, VkFormat format, VkImageLayout oldLayout, VkImageLayout newLayout) 
	{
		VkCommandBuffer command_buffer = BeginSingleTimeCommands(device, command_pool);

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
			command_buffer,
			source_stage, destination_stage,
			0,
			0, nullptr,
			0, nullptr,
			1, &barrier
		);

		EndSingleTimeCommands(device, command_pool, queue, command_buffer);
		return true;
	}

	inline bool CreateBuffer(VkDevice& device, VkPhysicalDevice& device_physical, VkDeviceSize size, VkBuffer& staging_buffer, VkDeviceMemory& staging_buffer_memory)
	{
		VkBufferCreateInfo bufferInfo	= {};
		bufferInfo.sType				= VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
		bufferInfo.size					= size;
		bufferInfo.usage				= VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
		bufferInfo.sharingMode			= VK_SHARING_MODE_EXCLUSIVE;

		if (vkCreateBuffer(device, &bufferInfo, nullptr, &staging_buffer) != VK_SUCCESS)
		{
			LOG_ERROR("Failed to create buffer");
			return false;
		}

		VkMemoryRequirements memory_requirements;
		vkGetBufferMemoryRequirements(device, staging_buffer, &memory_requirements);

		VkMemoryAllocateInfo allocInfo	= {};
		allocInfo.sType					= VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
		allocInfo.allocationSize		= memory_requirements.size;
		allocInfo.memoryTypeIndex		= Vulkan_Common::memory::get_type(device_physical, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, memory_requirements.memoryTypeBits);

		if (vkAllocateMemory(device, &allocInfo, nullptr, &staging_buffer_memory) != VK_SUCCESS)
		{
			LOG_ERROR("Failed to allocate memory");
			return false;
		}

		vkBindBufferMemory(device, staging_buffer, staging_buffer_memory, 0);
		return true;
	}

	inline bool CreateImage
	(
		RHI_Device* device,
		uint32_t width,
		uint32_t height,
		VkFormat format,
		VkImageTiling tiling,
		VkImageUsageFlags usage,
		VkMemoryPropertyFlags properties,
		VkImage& image,
		VkDeviceMemory& image_memory
	)
	{
		if (!Vulkan_Common::image::create(device, width, height, format, tiling, usage, image)) 
		{
			LOG_ERROR("Failed to create image");
			return false;
		}

		VkMemoryRequirements memRequirements;
		vkGetImageMemoryRequirements(device->GetContext()->device, image, &memRequirements);

		VkMemoryAllocateInfo allocInfo	= {};
		allocInfo.sType					= VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
		allocInfo.allocationSize		= memRequirements.size;
		allocInfo.memoryTypeIndex		= Vulkan_Common::memory::get_type(device->GetContext()->device_physical, properties, memRequirements.memoryTypeBits);

		if (vkAllocateMemory(device->GetContext()->device, &allocInfo, nullptr, &image_memory) != VK_SUCCESS) 
		{
			LOG_ERROR("Failed to allocate memory");
			return false;
		}

		vkBindImageMemory(device->GetContext()->device, image, image_memory, 0);
		return true;
	}

	bool RHI_Texture::ShaderResource_Create2D(unsigned int width, unsigned int height, unsigned int channels, RHI_Format format, const vector<vector<std::byte>>& mipmaps)
	{
		auto device				= m_rhi_device->GetContext()->device;
		auto device_physical	= m_rhi_device->GetContext()->device_physical;
		auto queue				= m_rhi_device->GetContext()->queue_copy;
		VkDeviceSize size		= width * height * channels;

		// Create image memory
		VkBuffer staging_buffer		= nullptr;
		VkDeviceMemory staging_buffer_memory	= nullptr;
		if (!CreateBuffer(device, device_physical, size, staging_buffer, staging_buffer_memory))
			return false;

		// Copy bytes to image memory
		void* buffer_ptr = nullptr;
		vkMapMemory(device, staging_buffer_memory, 0, size, 0, &buffer_ptr);
		memcpy(buffer_ptr, mipmaps.front().data(), static_cast<size_t>(size));
		vkUnmapMemory(device, staging_buffer_memory);

		// Create image
		VkImage image = nullptr;
		VkDeviceMemory image_memory = nullptr;
		if (!CreateImage
		(
			m_rhi_device.get(),
			width,
			height,
			vulkan_format[format],
			VK_IMAGE_TILING_LINEAR, // VK_IMAGE_TILING_OPTIMAL is not supported with VK_FORMAT_R32G32B32_SFLOAT
			VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
			VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
			image,
			image_memory
		))
			return false;
		
		// Create command pool
		void* cmd_pool_void;
		if (!Vulkan_Common::command_list::create_command_pool(m_rhi_device->GetContext(), cmd_pool_void))
		{
			LOG_ERROR("Failed to create command pool.");
			return false;
		}
		auto cmd_pool = static_cast<VkCommandPool>(cmd_pool_void);

		// Copy buffer to texture
		TransitionImageLayout(device, cmd_pool, queue, image, vulkan_format[format], VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);	
		{
			auto command_buffer	= BeginSingleTimeCommands(device, cmd_pool);

			VkBufferImageCopy region				= {};
			region.bufferOffset						= 0;
			region.bufferRowLength					= 0;
			region.bufferImageHeight				= 0;
			region.imageSubresource.aspectMask		= VK_IMAGE_ASPECT_COLOR_BIT;
			region.imageSubresource.mipLevel		= 0;
			region.imageSubresource.baseArrayLayer	= 0;
			region.imageSubresource.layerCount		= 1;
			region.imageOffset						= { 0, 0, 0 };
			region.imageExtent						= { static_cast<uint32_t>(width), static_cast<uint32_t>(height), 1 };

			vkCmdCopyBufferToImage(command_buffer, staging_buffer, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
			EndSingleTimeCommands(device, cmd_pool, queue, command_buffer);
		}
		TransitionImageLayout(device, cmd_pool, queue, image, vulkan_format[format], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

		// Create image view
		VkImageView image_view = nullptr;
		if (!Vulkan_Common::image_view::create(m_rhi_device.get(), image, image_view, vulkan_format[format]))
		{
			LOG_ERROR("Failed to create image view");
			return false;
		}

		vkDestroyBuffer(device, staging_buffer, nullptr);
		vkFreeMemory(device, staging_buffer_memory, nullptr);

		m_texture_view		= static_cast<void*>(image_view);
		m_texture			= static_cast<void*>(image);
		m_texture_memory	= static_cast<void*>(image_memory);

		return true;
	}

	bool RHI_Texture::ShaderResource_CreateCubemap(unsigned int width, unsigned int height, unsigned int channels, RHI_Format format, const vector<vector<vector<std::byte>>>& mipmaps)
	{
		return false;
	}
}
#endif