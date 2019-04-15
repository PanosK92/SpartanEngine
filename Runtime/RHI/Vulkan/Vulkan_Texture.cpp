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
#include "Vulkan_Helper.h"
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

		VkPipelineStageFlags sourceStage;
		VkPipelineStageFlags destinationStage;

		if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED && newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) 
		{
			barrier.srcAccessMask	= 0;
			barrier.dstAccessMask	= VK_ACCESS_TRANSFER_WRITE_BIT;
			sourceStage				= VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
			destinationStage		= VK_PIPELINE_STAGE_TRANSFER_BIT;
		}
		else if (oldLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL && newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) 
		{
			barrier.srcAccessMask	= VK_ACCESS_TRANSFER_WRITE_BIT;
			barrier.dstAccessMask	= VK_ACCESS_SHADER_READ_BIT;
			sourceStage				= VK_PIPELINE_STAGE_TRANSFER_BIT;
			destinationStage		= VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
		}
		else 
		{
			LOG_ERROR("Unsupported layout transition");
			return false;
		}

		vkCmdPipelineBarrier
		(
			command_buffer,
			sourceStage, destinationStage,
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
		allocInfo.memoryTypeIndex		= vulkan_helper::GetMemoryType(device_physical, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, memory_requirements.memoryTypeBits);

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
		VkDevice& device,
		VkPhysicalDevice& device_physical, 
		uint32_t width, uint32_t height,
		RHI_Format format,
		VkImageTiling tiling,
		VkImageUsageFlags usage,
		VkMemoryPropertyFlags properties,
		VkImage& image,
		VkDeviceMemory& image_memory
	)
	{
		VkImageCreateInfo imageInfo = {};
		imageInfo.sType				= VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
		imageInfo.imageType			= VK_IMAGE_TYPE_2D;
		imageInfo.extent.width		= width;
		imageInfo.extent.height		= height;
		imageInfo.extent.depth		= 1;
		imageInfo.mipLevels			= 1;
		imageInfo.arrayLayers		= 1;
		imageInfo.format			= vulkan_format[format];
		imageInfo.tiling			= tiling;
		imageInfo.initialLayout		= VK_IMAGE_LAYOUT_UNDEFINED;
		imageInfo.usage				= usage;
		imageInfo.samples			= VK_SAMPLE_COUNT_1_BIT;
		imageInfo.sharingMode		= VK_SHARING_MODE_EXCLUSIVE;

		if (vkCreateImage(device, &imageInfo, nullptr, &image) != VK_SUCCESS) 
		{
			LOG_ERROR("Failed to create image");
			return false;
		}

		VkMemoryRequirements memRequirements;
		vkGetImageMemoryRequirements(device, image, &memRequirements);

		VkMemoryAllocateInfo allocInfo	= {};
		allocInfo.sType					= VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
		allocInfo.allocationSize		= memRequirements.size;
		allocInfo.memoryTypeIndex		= vulkan_helper::GetMemoryType(device_physical, properties, memRequirements.memoryTypeBits);

		if (vkAllocateMemory(device, &allocInfo, nullptr, &image_memory) != VK_SUCCESS) 
		{
			LOG_ERROR("Failed to allocate memory");
			return false;
		}

		vkBindImageMemory(device, image, image_memory, 0);
		return true;
	}

	inline bool CreateImageView(VkDevice& device, VkImage& image, VkImageView& image_view, RHI_Format format)
	{
		VkImageViewCreateInfo viewInfo				= {};
		viewInfo.sType								= VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
		viewInfo.image								= image;
		viewInfo.viewType							= VK_IMAGE_VIEW_TYPE_2D;
		viewInfo.format								= vulkan_format[format];
		viewInfo.subresourceRange.aspectMask		= VK_IMAGE_ASPECT_COLOR_BIT;
		viewInfo.subresourceRange.baseMipLevel		= 0;
		viewInfo.subresourceRange.levelCount		= 1;
		viewInfo.subresourceRange.baseArrayLayer	= 0;
		viewInfo.subresourceRange.layerCount		= 1;

		if (vkCreateImageView(device, &viewInfo, nullptr, &image_view) != VK_SUCCESS)
		{
			LOG_ERROR("Failed to create image view");
			return false;
		}

		return true;
	}

	bool RHI_Texture::ShaderResource_Create2D(unsigned int width, unsigned int height, unsigned int channels, RHI_Format format, const vector<vector<std::byte>>& mipmaps)
	{
		auto image_view							= static_cast<VkImageView>(m_texture_view);
		auto image								= static_cast<VkImage>(m_texture);
		auto image_memory						= static_cast<VkDeviceMemory>(m_texture_memory);
		auto device								= m_rhi_device->GetContext()->device;
		auto device_physical					= m_rhi_device->GetContext()->device_physical;
		auto queue								= m_rhi_device->GetContext()->queue_copy;
		VkDeviceSize size						= width * height * channels;
		VkBuffer staging_buffer					= nullptr;
		VkDeviceMemory staging_buffer_memory	= nullptr;

		if (!CreateBuffer(device, device_physical, size, staging_buffer, staging_buffer_memory))
			return false;

		void* buffer_ptr = nullptr;
		vkMapMemory(device, staging_buffer_memory, 0, size, 0, &buffer_ptr);
		memcpy(buffer_ptr, mipmaps.front().data(), static_cast<size_t>(size));
		vkUnmapMemory(device, staging_buffer_memory);

		if (!CreateImage
		(
			device,
			device_physical,
			width,
			height,
			format,
			VK_IMAGE_TILING_OPTIMAL,
			VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
			VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
			image,
			image_memory
		))
			return false;

		
		VkCommandPool cmd_pool;
		if (!vulkan_helper::command_list::create_command_pool(m_rhi_device->GetContext(), &cmd_pool))
		{
			LOG_ERROR("Failed to create command pool.");
			return false;
		}
		TransitionImageLayout(device, cmd_pool, queue, image, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
		// Copy buffer to texture
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
		TransitionImageLayout(device, cmd_pool, queue, image, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

		vkDestroyBuffer(device, staging_buffer, nullptr);
		vkFreeMemory(device, staging_buffer_memory, nullptr);

		if (!CreateImageView(device, image, image_view, format))
			return false;

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