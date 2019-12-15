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

#pragma once

//= IMPLEMENTATION ===============
#include "../RHI_Implementation.h"
#ifdef API_GRAPHICS_VULKAN
//================================

//= INCLUDES ==================
#include "../RHI_Device.h"
#include "../RHI_Texture.h"
#include "../../Logging/Log.h"
#include "../../Math/Vector4.h"
//=============================

namespace Spartan::vulkan_common
{
    namespace error
    {
        inline const char* to_string(const VkResult result)
        {
            switch (result)
            {
                case VK_SUCCESS:                                            return "VK_SUCCESS";
                case VK_NOT_READY:											return "VK_NOT_READY";
                case VK_TIMEOUT:											return "VK_TIMEOUT";
                case VK_EVENT_SET:											return "VK_EVENT_SET";
                case VK_EVENT_RESET:										return "VK_EVENT_RESET";
                case VK_INCOMPLETE:											return "VK_INCOMPLETE";
                case VK_ERROR_OUT_OF_HOST_MEMORY:							return "VK_ERROR_OUT_OF_HOST_MEMORY";
                case VK_ERROR_OUT_OF_DEVICE_MEMORY:							return "VK_ERROR_OUT_OF_DEVICE_MEMORY";
                case VK_ERROR_INITIALIZATION_FAILED:						return "VK_ERROR_INITIALIZATION_FAILED";
                case VK_ERROR_DEVICE_LOST:									return "VK_ERROR_DEVICE_LOST";
                case VK_ERROR_MEMORY_MAP_FAILED:							return "VK_ERROR_MEMORY_MAP_FAILED";
                case VK_ERROR_LAYER_NOT_PRESENT:							return "VK_ERROR_LAYER_NOT_PRESENT";
                case VK_ERROR_EXTENSION_NOT_PRESENT:						return "VK_ERROR_EXTENSION_NOT_PRESENT";
                case VK_ERROR_FEATURE_NOT_PRESENT:							return "VK_ERROR_FEATURE_NOT_PRESENT";
                case VK_ERROR_INCOMPATIBLE_DRIVER:							return "VK_ERROR_INCOMPATIBLE_DRIVER";
                case VK_ERROR_TOO_MANY_OBJECTS:								return "VK_ERROR_TOO_MANY_OBJECTS";
                case VK_ERROR_FORMAT_NOT_SUPPORTED:							return "VK_ERROR_FORMAT_NOT_SUPPORTED";
                case VK_ERROR_FRAGMENTED_POOL:								return "VK_ERROR_FRAGMENTED_POOL";
                case VK_ERROR_OUT_OF_POOL_MEMORY:							return "VK_ERROR_OUT_OF_POOL_MEMORY";
                case VK_ERROR_INVALID_EXTERNAL_HANDLE:						return "VK_ERROR_INVALID_EXTERNAL_HANDLE";
                case VK_ERROR_SURFACE_LOST_KHR:								return "VK_ERROR_SURFACE_LOST_KHR";
                case VK_ERROR_NATIVE_WINDOW_IN_USE_KHR:						return "VK_ERROR_NATIVE_WINDOW_IN_USE_KHR";
                case VK_SUBOPTIMAL_KHR:										return "VK_SUBOPTIMAL_KHR";
                case VK_ERROR_OUT_OF_DATE_KHR:								return "VK_ERROR_OUT_OF_DATE_KHR";
                case VK_ERROR_INCOMPATIBLE_DISPLAY_KHR:						return "VK_ERROR_INCOMPATIBLE_DISPLAY_KHR";
                case VK_ERROR_VALIDATION_FAILED_EXT:						return "VK_ERROR_VALIDATION_FAILED_EXT";
                case VK_ERROR_INVALID_SHADER_NV:							return "VK_ERROR_INVALID_SHADER_NV";
                case VK_ERROR_INVALID_DRM_FORMAT_MODIFIER_PLANE_LAYOUT_EXT: return "VK_ERROR_INVALID_DRM_FORMAT_MODIFIER_PLANE_LAYOUT_EXT";
                case VK_ERROR_FRAGMENTATION_EXT:							return "VK_ERROR_FRAGMENTATION_EXT";
                case VK_ERROR_NOT_PERMITTED_EXT:							return "VK_ERROR_NOT_PERMITTED_EXT";
                case VK_ERROR_INVALID_DEVICE_ADDRESS_EXT:					return "VK_ERROR_INVALID_DEVICE_ADDRESS_EXT";
                case VK_ERROR_FULL_SCREEN_EXCLUSIVE_MODE_LOST_EXT:			return "VK_ERROR_FULL_SCREEN_EXCLUSIVE_MODE_LOST_EXT";
            }

            return "Unknown error code";
        }

        inline bool check_result(VkResult result)
        {
            if (result == VK_SUCCESS)
                return true;

            LOG_ERROR("%s", to_string(result));
            return false;
        }

        inline void assert_result(VkResult result)
        {
            SPARTAN_ASSERT(result == VK_SUCCESS);
        }
    }

    namespace device
    {
        inline uint32_t get_queue_family_index(VkQueueFlagBits queue_flags, const std::vector<VkQueueFamilyProperties>& queue_family_properties, uint32_t* index)
        {
            // Dedicated queue for compute
            // Try to find a queue family index that supports compute but not graphics
            if (queue_flags & VK_QUEUE_COMPUTE_BIT)
            {
                for (uint32_t i = 0; i < static_cast<uint32_t>(queue_family_properties.size()); i++)
                {
                    if ((queue_family_properties[i].queueFlags & queue_flags) && ((queue_family_properties[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) == 0))
                    {
                        *index = i;
                        return true;
                    }
                }
            }

            // Dedicated queue for transfer
            // Try to find a queue family index that supports transfer but not graphics and compute
            if (queue_flags & VK_QUEUE_TRANSFER_BIT)
            {
                for (uint32_t i = 0; i < static_cast<uint32_t>(queue_family_properties.size()); i++)
                {
                    if ((queue_family_properties[i].queueFlags & queue_flags) && ((queue_family_properties[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) == 0) && ((queue_family_properties[i].queueFlags & VK_QUEUE_COMPUTE_BIT) == 0))
                    {
                        *index = i;
                        return true;
                    }
                }
            }

            // For other queue types or if no separate compute queue is present, return the first one to support the requested flags
            for (uint32_t i = 0; i < static_cast<uint32_t>(queue_family_properties.size()); i++)
            {
                if (queue_family_properties[i].queueFlags & queue_flags)
                {
                    *index = i;
                    return true;
                }
            }

            return false;
        }

        inline bool get_queue_family_indices(RHI_Context* rhi_context, const VkPhysicalDevice& physical_device)
        {
            uint32_t queue_family_count = 0;
            vkGetPhysicalDeviceQueueFamilyProperties(physical_device, &queue_family_count, nullptr);

            std::vector<VkQueueFamilyProperties> queue_families_properties(queue_family_count);
            vkGetPhysicalDeviceQueueFamilyProperties(physical_device, &queue_family_count, queue_families_properties.data());

            if (!get_queue_family_index(VK_QUEUE_GRAPHICS_BIT, queue_families_properties, &rhi_context->queue_graphics_family_index))
                return false;

            if (!get_queue_family_index(VK_QUEUE_TRANSFER_BIT, queue_families_properties, &rhi_context->queue_transfer_family_index))
            {
                LOG_WARNING("Transfer queue not suported, using graphics instead.");
                rhi_context->queue_transfer_family_index = rhi_context->queue_graphics_family_index;
                return true;
            }

            if (!get_queue_family_index(VK_QUEUE_COMPUTE_BIT, queue_families_properties, &rhi_context->queue_compute_family_index))
            {
                LOG_WARNING("Compute queue not suported, using graphics instead.");
                rhi_context->queue_compute_family_index = rhi_context->queue_graphics_family_index;
                return true;
            }

            return true;
        }

        inline bool choose_physical_device(RHI_Context* rhi_context, void* window_handle)
        {
            uint32_t device_count = 0;
            if (!error::check_result(vkEnumeratePhysicalDevices(rhi_context->instance, &device_count, nullptr)))
                return false;

            if (device_count == 0)
            {
                LOG_ERROR("There are no available devices.");
                return false;
            }

            std::vector<VkPhysicalDevice> physical_devices(device_count);
            if (!error::check_result(vkEnumeratePhysicalDevices(rhi_context->instance, &device_count, physical_devices.data())))
                return false;

            for (const auto& device : physical_devices)
            {
                // Get the first device that has a graphics, a compute and a transfer queue
                if (get_queue_family_indices(rhi_context, device))
                {
                    rhi_context->device_physical = device;
                    return true;
                }
            }

            return false;
        }
    }

	namespace memory
	{
		inline uint32_t get_type(const RHI_Context* rhi_context, const VkMemoryPropertyFlags properties, const uint32_t type_bits)
		{
			VkPhysicalDeviceMemoryProperties prop;
			vkGetPhysicalDeviceMemoryProperties(rhi_context->device_physical, &prop);

            for (uint32_t i = 0; i < prop.memoryTypeCount; i++)
            {
                if ((prop.memoryTypes[i].propertyFlags & properties) == properties && type_bits & (1 << i))
                    return i;
            }

            // Unable to find memoryType
			return std::numeric_limits<uint32_t>::max(); 
		}

		inline void free(const RHI_Context* rhi_context, void*& device_memory)
		{
			if (!device_memory)
				return;

			vkFreeMemory(rhi_context->device, static_cast<VkDeviceMemory>(device_memory), nullptr);
			device_memory = nullptr;
		}
	}

	namespace command
	{
        inline bool create_pool(const RHI_Context* rhi_context, void*& cmd_pool, uint32_t queue_family_index)
        {
            VkCommandPoolCreateInfo cmd_pool_info   = {};
            cmd_pool_info.sType                     = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
            cmd_pool_info.queueFamilyIndex          = queue_family_index;
            cmd_pool_info.flags                     = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;

            VkCommandPool* cmd_pool_vk = reinterpret_cast<VkCommandPool*>(&cmd_pool);
            return error::check_result(vkCreateCommandPool(rhi_context->device, &cmd_pool_info, nullptr, cmd_pool_vk));
        }

		inline bool create_buffer(const RHI_Context* rhi_context, void*& cmd_pool, void*& cmd_buffer, const VkCommandBufferLevel level)
		{
            VkCommandPool cmd_pool_vk       = static_cast<VkCommandPool>(cmd_pool);
            VkCommandBuffer* cmd_buffer_vk  = reinterpret_cast<VkCommandBuffer*>(&cmd_buffer);

			VkCommandBufferAllocateInfo allocate_info	= {};
			allocate_info.sType							= VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
			allocate_info.commandPool					= cmd_pool_vk;
			allocate_info.level							= level;
            allocate_info.commandBufferCount            = 1;

            return error::check_result(vkAllocateCommandBuffers(rhi_context->device, &allocate_info, cmd_buffer_vk));
		}

        inline bool flush(void*& cmd_buffer, const VkQueue& queue)
        {
            if (!cmd_buffer)
                return false;

            VkCommandBuffer cmd_buffer_vk = static_cast<VkCommandBuffer>(cmd_buffer);

            if (!error::check_result(vkEndCommandBuffer(cmd_buffer_vk)))
                return false;

            VkSubmitInfo submitInfo         = {};
            submitInfo.sType                = VK_STRUCTURE_TYPE_SUBMIT_INFO;
            submitInfo.commandBufferCount   = 1;
            submitInfo.pCommandBuffers      = &cmd_buffer_vk;

            if (!error::check_result(vkQueueSubmit(queue, 1, &submitInfo, nullptr)))
                return false;

            return error::check_result(vkQueueWaitIdle(queue));
        }

        inline bool begin(const RHI_Context* rhi_context, uint32_t queue_family_index, void*& cmd_pool, void*& cmd_buffer)
        {
            // Create command pool
            if (!create_pool(rhi_context, cmd_pool, queue_family_index))
                return false;

            // Create command buffer
            if (!create_buffer(rhi_context, cmd_pool, cmd_buffer, VK_COMMAND_BUFFER_LEVEL_PRIMARY))
                return false;

            VkCommandBufferBeginInfo begin_info = {};
            begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
            begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

            VkCommandBuffer cmd_buffer_vk = static_cast<VkCommandBuffer>(cmd_buffer);
            return error::check_result(vkBeginCommandBuffer(cmd_buffer_vk, &begin_info));
        }

        inline void free(const RHI_Context* rhi_context, void*& cmd_pool, void*& cmd_buffer)
        {
            VkCommandPool cmd_pool_vk       = static_cast<VkCommandPool>(cmd_pool);
            VkCommandBuffer* cmd_buffer_vk  = reinterpret_cast<VkCommandBuffer*>(&cmd_buffer);
            vkFreeCommandBuffers(rhi_context->device, cmd_pool_vk, 1, cmd_buffer_vk);
        }

        inline void destroy(const RHI_Context* rhi_context, void*& cmd_pool)
        {
            VkCommandPool cmd_pool_vk = static_cast<VkCommandPool>(cmd_pool);
            vkDestroyCommandPool(rhi_context->device, cmd_pool_vk, nullptr);
            cmd_pool = nullptr;
        }
	}

	namespace semaphore
	{
		inline bool create(const RHI_Context* rhi_context, void*& semaphore)
		{
			VkSemaphoreCreateInfo semaphore_info	= {};
			semaphore_info.sType					= VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

            VkSemaphore* semaphore_vk = reinterpret_cast<VkSemaphore*>(&semaphore);
            return error::check_result(vkCreateSemaphore(rhi_context->device, &semaphore_info, nullptr, semaphore_vk));
		}

		inline void destroy(const RHI_Context* rhi_context, void*& semaphore)
		{
			if (!semaphore)
				return;

            VkSemaphore semaphore_vk = static_cast<VkSemaphore>(semaphore);
			vkDestroySemaphore(rhi_context->device, semaphore_vk, nullptr);
            semaphore = nullptr;
		}
	}

	namespace fence
	{
		inline bool create(const RHI_Context* rhi_context, void*& fence)
		{
			VkFenceCreateInfo fence_info	= {};
			fence_info.sType				= VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;

            VkFence* fence_vk = reinterpret_cast<VkFence*>(&fence);
            return error::check_result(vkCreateFence(rhi_context->device, &fence_info, nullptr, fence_vk));
		}

		inline void destroy(const RHI_Context* rhi_context, void*& fence)
		{
			if (!fence)
				return;

            VkFence fence_vk = static_cast<VkFence>(fence);
			vkDestroyFence(rhi_context->device, fence_vk, nullptr);
            fence = nullptr;
		}

		inline void wait(const RHI_Context* rhi_context, void*& fence)
		{
            VkFence* fence_vk = reinterpret_cast<VkFence*>(&fence);
			error::assert_result(vkWaitForFences(rhi_context->device, 1, fence_vk, true, std::numeric_limits<uint64_t>::max()));
		}

		inline void reset(const RHI_Context* rhi_context, void*& fence)
		{
            VkFence* fence_vk = reinterpret_cast<VkFence*>(&fence);
            error::assert_result(vkResetFences(rhi_context->device, 1, fence_vk));
		}

		inline void wait_reset(const RHI_Context* rhi_context, void*& fence)
		{
            VkFence* fence_vk = reinterpret_cast<VkFence*>(&fence);
            error::assert_result(vkWaitForFences(rhi_context->device, 1, fence_vk, true, std::numeric_limits<uint64_t>::max()));
            error::assert_result(vkResetFences(rhi_context->device, 1, fence_vk));
		}
	}

	namespace buffer
	{
		inline bool create_allocate_bind(const RHI_Context* rhi_context, VkBuffer& _buffer, VkDeviceMemory& buffer_memory, VkDeviceSize& size, VkBufferUsageFlags usage)
		{
			VkBufferCreateInfo buffer_info	= {};
			buffer_info.sType				= VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
			buffer_info.size				= size;
			buffer_info.usage				= usage;
			buffer_info.sharingMode			= VK_SHARING_MODE_EXCLUSIVE;

			if (!error::check_result(vkCreateBuffer(rhi_context->device, &buffer_info, nullptr, &_buffer)))
				return false;

			VkMemoryRequirements memory_requirements;
			vkGetBufferMemoryRequirements(rhi_context->device, _buffer, &memory_requirements);

			VkMemoryAllocateInfo alloc_info = {};			
			alloc_info.sType				= VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
			alloc_info.allocationSize		= memory_requirements.size;
			alloc_info.memoryTypeIndex		= memory::get_type(rhi_context, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, memory_requirements.memoryTypeBits);

            if (!error::check_result(vkAllocateMemory(rhi_context->device, &alloc_info, nullptr, &buffer_memory)))
                return false;

            if (!error::check_result(vkBindBufferMemory(rhi_context->device, _buffer, buffer_memory, 0)))
                return false;

			return true;
		}

		inline void destroy(const RHI_Context* rhi_context, void*& _buffer)
		{
			if (!_buffer)
				return;

			vkDestroyBuffer(rhi_context->device, static_cast<VkBuffer>(_buffer), nullptr);
			_buffer = nullptr;
		}
	}

    namespace image
    {
        inline VkImageTiling is_format_supported(const RHI_Context* rhi_context, const RHI_Format format, VkFormatFeatureFlags flag)
        {
            // Get format properties
            VkFormatProperties format_properties;
            vkGetPhysicalDeviceFormatProperties(rhi_context->device_physical, vulkan_format[format], &format_properties);

            // Check for optimal support
            if (format_properties.optimalTilingFeatures & flag)
            {
                return VK_IMAGE_TILING_OPTIMAL;
            }

            // Check for linear support
            if (format_properties.linearTilingFeatures & flag)
            {
                return VK_IMAGE_TILING_LINEAR;
            }

            return VK_IMAGE_TILING_MAX_ENUM;
        }

        inline bool allocate_bind(const RHI_Context* rhi_context, const VkImage& image, VkDeviceMemory* memory, VkDeviceSize* memory_size = nullptr)
        {
            VkMemoryRequirements memory_requirements;
            vkGetImageMemoryRequirements(rhi_context->device, image, &memory_requirements);

            VkMemoryAllocateInfo allocate_info  = {};
            allocate_info.sType                 = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
            allocate_info.allocationSize        = memory_requirements.size;
            allocate_info.memoryTypeIndex       = vulkan_common::memory::get_type(rhi_context, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, memory_requirements.memoryTypeBits);

            if (!error::check_result(vkAllocateMemory(rhi_context->device, &allocate_info, nullptr, memory)))
                return false;

            if (!error::check_result(vkBindImageMemory(rhi_context->device, image, *memory, 0)))
                return false;

            if (memory_size)
            {
                *memory_size = memory_requirements.size;
            }

            return true;
        }

        inline VkImageAspectFlags bind_flags_to_aspect_mask(const uint16_t bind_flags)
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

            return aspect_mask;
        }

        inline bool create(const RHI_Context* rhi_context, VkImage& image, const uint32_t width, const uint32_t height, const VkFormat format, const VkImageTiling tiling, const RHI_Image_Layout layout, const VkImageUsageFlags usage)
        {
            VkImageCreateInfo create_info   = {};
            create_info.sType               = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
            create_info.imageType           = VK_IMAGE_TYPE_2D;
            create_info.extent.width        = width;
            create_info.extent.height       = height;
            create_info.extent.depth        = 1;
            create_info.mipLevels           = 1;
            create_info.arrayLayers         = 1;
            create_info.format              = format;
            create_info.tiling              = tiling;
            create_info.initialLayout       = vulkan_image_layout[layout];
            create_info.usage               = usage;
            create_info.samples             = VK_SAMPLE_COUNT_1_BIT;
            create_info.sharingMode         = VK_SHARING_MODE_EXCLUSIVE;

            return vulkan_common::error::check_result(vkCreateImage(rhi_context->device, &create_info, nullptr, &image));
        }

        inline void destroy(const RHI_Context* rhi_context, void*& image)
        {
            if (!image)
                return;

            vkDestroyImage(rhi_context->device, static_cast<VkImage>(image), nullptr);
            image = nullptr;
        }
    }

    namespace image_view
    {
        inline bool create(const RHI_Context* rhi_context, void* image, void*& image_view, const VkFormat format, const VkImageAspectFlags aspect_flags)
        {
            VkImageViewCreateInfo create_info           = {};
            create_info.sType                           = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
            create_info.image                           = static_cast<VkImage>(image);
            create_info.viewType                        = VK_IMAGE_VIEW_TYPE_2D;
            create_info.format                          = format;
            create_info.subresourceRange.aspectMask     = aspect_flags;
            create_info.subresourceRange.baseMipLevel   = 0;
            create_info.subresourceRange.levelCount     = 1;
            create_info.subresourceRange.baseArrayLayer = 0;
            create_info.subresourceRange.layerCount     = 1;
            create_info.components.r                    = VK_COMPONENT_SWIZZLE_IDENTITY;
            create_info.components.g                    = VK_COMPONENT_SWIZZLE_IDENTITY;
            create_info.components.b                    = VK_COMPONENT_SWIZZLE_IDENTITY;
            create_info.components.a                    = VK_COMPONENT_SWIZZLE_IDENTITY;

            VkImageView* image_view_vk = reinterpret_cast<VkImageView*>(&image_view);
            return error::check_result(vkCreateImageView(rhi_context->device, &create_info, nullptr, image_view_vk));
        }

        inline void destroy(const RHI_Context* rhi_context, void*& image_view)
        {
            if (!image_view)
                return;

            vkDestroyImageView(rhi_context->device, static_cast<VkImageView>(image_view), nullptr);
            image_view = nullptr;
        }

        inline void destroy(const RHI_Context* rhi_context, std::vector<void*>& image_views)
        {
            for (auto& image_view : image_views)
            {
                vkDestroyImageView(rhi_context->device, static_cast<VkImageView>(image_view), nullptr);
            }
            image_views.clear();
            image_views.shrink_to_fit();
        }
    }

    namespace render_pass
    {
        inline bool create(const RHI_Context* rhi_context, const VkFormat format, void*& render_pass, const VkImageLayout layout_final)
        {
            bool clear_on_set = false;

            VkAttachmentDescription color_attachment    = {};
            color_attachment.format                     = format;
            color_attachment.samples                    = VK_SAMPLE_COUNT_1_BIT;
            color_attachment.loadOp                     = clear_on_set ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_LOAD;
            color_attachment.storeOp                    = VK_ATTACHMENT_STORE_OP_STORE;
            color_attachment.stencilLoadOp              = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
            color_attachment.stencilStoreOp             = VK_ATTACHMENT_STORE_OP_DONT_CARE;
            color_attachment.initialLayout              = VK_IMAGE_LAYOUT_UNDEFINED;
            color_attachment.finalLayout                = layout_final;

            VkAttachmentReference color_attachment_ref  = {};
            color_attachment_ref.attachment             = 0;
            color_attachment_ref.layout                 = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

            VkSubpassDescription subpass    = {};
            subpass.pipelineBindPoint       = VK_PIPELINE_BIND_POINT_GRAPHICS;
            subpass.colorAttachmentCount    = 1;
            subpass.pColorAttachments       = &color_attachment_ref;

            // Sub-pass dependencies for layout transitions
            std::vector<VkSubpassDependency> dependencies
            {
                VkSubpassDependency
                {
                    VK_SUBPASS_EXTERNAL,														// uint32_t srcSubpass;
                    0,																			// uint32_t dstSubpass;
                    VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,										// PipelineStageFlags srcStageMask;
                    VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,								// PipelineStageFlags dstStageMask;
                    VK_ACCESS_MEMORY_READ_BIT,													// AccessFlags srcAccessMask;
                    VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,	// AccessFlags dstAccessMask;
                    VK_DEPENDENCY_BY_REGION_BIT													// DependencyFlags dependencyFlags;
                },

                VkSubpassDependency
                {
                    0,																			// uint32_t srcSubpass;
                    VK_SUBPASS_EXTERNAL,														// uint32_t dstSubpass;
                    VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,								// PipelineStageFlags srcStageMask;
                    VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,										// PipelineStageFlags dstStageMask;
                    VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,	// AccessFlags srcAccessMask;
                    VK_ACCESS_MEMORY_READ_BIT,													// AccessFlags dstAccessMask;
                    VK_DEPENDENCY_BY_REGION_BIT													// DependencyFlags dependencyFlags;
                },
            };

            VkRenderPassCreateInfo render_pass_info = {};
            render_pass_info.sType                  = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
            render_pass_info.attachmentCount        = 1;
            render_pass_info.pAttachments           = &color_attachment;
            render_pass_info.subpassCount           = 1;
            render_pass_info.pSubpasses             = &subpass;
            render_pass_info.dependencyCount        = static_cast<uint32_t>(dependencies.size());
            render_pass_info.pDependencies          = dependencies.data();

            VkRenderPass* render_pass_vk = reinterpret_cast<VkRenderPass*>(&render_pass);
            return error::check_result(vkCreateRenderPass(rhi_context->device, &render_pass_info, nullptr, render_pass_vk));
        }

        inline void destroy(const RHI_Context* rhi_context, void*& render_pass)
        {
            if (!render_pass)
                return;

            VkRenderPass render_pass_vk = static_cast<VkRenderPass>(render_pass);
            vkDestroyRenderPass(rhi_context->device, render_pass_vk, nullptr);
            render_pass = nullptr;
        }
    }

    namespace frame_buffer
    {
        inline bool create(const RHI_Context* rhi_context, void* render_pass, const std::vector<void*>& attachments, const uint32_t width, const uint32_t height, void*& frame_buffer)
        {
            VkFramebufferCreateInfo create_info = {};
            create_info.sType                   = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
            create_info.renderPass              = static_cast<VkRenderPass>(render_pass);
            create_info.attachmentCount         = static_cast<uint32_t>(attachments.size());
            create_info.pAttachments            = reinterpret_cast<const VkImageView*>(attachments.data());
            create_info.width                   = width;
            create_info.height                  = height;
            create_info.layers                  = 1;

            VkFramebuffer* frame_buffer_vk = reinterpret_cast<VkFramebuffer*>(&frame_buffer);
            return error::check_result(vkCreateFramebuffer(rhi_context->device, &create_info, nullptr, frame_buffer_vk));
        }

        inline void destroy(const RHI_Context* rhi_context, void*& frame_buffer)
        {
            if (!frame_buffer)
                return;

            vkDestroyFramebuffer(rhi_context->device, static_cast<VkFramebuffer>(frame_buffer), nullptr);
            frame_buffer = nullptr;
        }
    }

    namespace surface
    {
        inline VkSurfaceCapabilitiesKHR capabilities(const RHI_Context* rhi_context, const VkSurfaceKHR surface)
        {
            VkSurfaceCapabilitiesKHR surface_capabilities;
            vkGetPhysicalDeviceSurfaceCapabilitiesKHR(rhi_context->device_physical, surface, &surface_capabilities);
            return surface_capabilities;
        }

        inline std::vector<VkPresentModeKHR> present_modes(const RHI_Context* rhi_context, const VkSurfaceKHR surface)
        {
            uint32_t present_mode_count;
            vkGetPhysicalDeviceSurfacePresentModesKHR(rhi_context->device_physical, surface, &present_mode_count, nullptr);

            std::vector<VkPresentModeKHR> surface_present_modes(present_mode_count);
            vkGetPhysicalDeviceSurfacePresentModesKHR(rhi_context->device_physical, surface, &present_mode_count, &surface_present_modes[0]);
            return surface_present_modes;
        }

        inline std::vector<VkSurfaceFormatKHR> formats(const RHI_Context* rhi_context, const VkSurfaceKHR surface)
        {
            uint32_t format_count;
            vkGetPhysicalDeviceSurfaceFormatsKHR(rhi_context->device_physical, surface, &format_count, nullptr);

            std::vector<VkSurfaceFormatKHR> surface_formats(format_count);
            error::check_result(vkGetPhysicalDeviceSurfaceFormatsKHR(rhi_context->device_physical, surface, &format_count, &surface_formats[0]));

            return surface_formats;
        }

        inline void detect_format_and_color_space(const RHI_Context* rhi_context, const VkSurfaceKHR surface, VkFormat* format, VkColorSpaceKHR* color_space)
        {
            std::vector<VkSurfaceFormatKHR> surface_formats = formats(rhi_context, surface);

            // If the surface format list only includes one entry with VK_FORMAT_UNDEFINED,
            // there is no preferred format, so we assume VK_FORMAT_B8G8R8A8_UNORM
            if ((surface_formats.size() == 1) && (surface_formats[0].format == VK_FORMAT_UNDEFINED))
            {
                *format = VK_FORMAT_B8G8R8A8_UNORM;
                *color_space = surface_formats[0].colorSpace;
            }
            else
            {
                // iterate over the list of available surface format and
                // check for the presence of VK_FORMAT_B8G8R8A8_UNORM
                bool found_B8G8R8A8_UNORM = false;
                for (auto&& surfaceFormat : surface_formats)
                {
                    if (surfaceFormat.format == VK_FORMAT_B8G8R8A8_UNORM)
                    {
                        *format = surfaceFormat.format;
                        *color_space = surfaceFormat.colorSpace;
                        found_B8G8R8A8_UNORM = true;
                        break;
                    }
                }

                // in case VK_FORMAT_B8G8R8A8_UNORM is not available
                // select the first available color format
                if (!found_B8G8R8A8_UNORM)
                {
                    *format         = surface_formats[0].format;
                    *color_space    = surface_formats[0].colorSpace;
                }
            }
        }

        inline VkPresentModeKHR set_present_mode(const RHI_Context* rhi_context, const VkSurfaceKHR surface, const VkPresentModeKHR prefered_present_mode)
        {
            // The VK_PRESENT_MODE_FIFO_KHR mode must always be present as per spec
            // This mode waits for the vertical blank ("v-sync")
            VkPresentModeKHR present_mode = VK_PRESENT_MODE_FIFO_KHR;

            std::vector<VkPresentModeKHR>& surface_present_modes = present_modes(rhi_context, surface);

            // Check if the preferred mode is supported
            for (const auto& supported_present_mode : surface_present_modes)
            {
                if (prefered_present_mode == supported_present_mode)
                {
                    present_mode = prefered_present_mode;
                    break;
                }
            }

            // Select a mode from the supported present modes
            for (const auto& supported_present_mode : surface_present_modes)
            {
                if (supported_present_mode == VK_PRESENT_MODE_MAILBOX_KHR)
                {
                    present_mode = supported_present_mode;
                    break;
                }

                if ((present_mode != VK_PRESENT_MODE_MAILBOX_KHR) && (supported_present_mode == VK_PRESENT_MODE_IMMEDIATE_KHR))
                {
                    present_mode = supported_present_mode;
                }
            }

            return present_mode;
        }
    }

    namespace extension
    {
        inline bool is_present(const char* extension_name)
        {
            uint32_t layer_count;
            vkEnumerateInstanceLayerProperties(&layer_count, nullptr);

            std::vector<VkLayerProperties> available_layers(layer_count);
            vkEnumerateInstanceLayerProperties(&layer_count, available_layers.data());

            for (const auto& layer_properties : available_layers)
            {
                if (strcmp(extension_name, layer_properties.layerName) == 0)
                    return true;
            }

            return false;
        }
    }

    namespace debug_message
    {
        static VKAPI_ATTR VkBool32 VKAPI_CALL callback(
            VkDebugUtilsMessageSeverityFlagBitsEXT message_severity,
            VkDebugUtilsMessageTypeFlagsEXT message_type,
            const VkDebugUtilsMessengerCallbackDataEXT* p_callback_data,
            void* p_user_data
        ) {
            if (message_severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT)
            {
                LOG_INFO(p_callback_data->pMessage);
            }
            else if (message_severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT)
            {
                LOG_INFO(p_callback_data->pMessage);
            }
            else if (message_severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT)
            {
                LOG_WARNING(p_callback_data->pMessage);
            }
            else if (message_severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT)
            {
                LOG_ERROR(p_callback_data->pMessage);
            }

            return VK_FALSE;
        }

        inline VkResult create(RHI_Device* rhi_device, const VkDebugUtilsMessengerCreateInfoEXT* create_info)
        {
            if (const auto func = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(vkGetInstanceProcAddr(rhi_device->GetContextRhi()->instance, "vkCreateDebugUtilsMessengerEXT")))
                return func(rhi_device->GetContextRhi()->instance, create_info, nullptr, &rhi_device->GetContextRhi()->callback_handle);

            return VK_ERROR_EXTENSION_NOT_PRESENT;
        }

        inline void destroy(RHI_Context* context)
        {
            if (!context->validation_enabled)
                return;

            if (const auto func = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(vkGetInstanceProcAddr(context->instance, "vkDestroyDebugUtilsMessengerEXT")))
            {
                func(context->instance, context->callback_handle, nullptr);
            }
        }
    }

    namespace debug_marker
    {
        static bool active = false;

        // The debug marker extension is not part of the core, so function pointers need to be loaded manually
        static PFN_vkDebugMarkerSetObjectNameEXT vkDebugMarkerSetObjectName = VK_NULL_HANDLE;
        static PFN_vkCmdDebugMarkerBeginEXT pfnCmdDebugMarkerBegin          = VK_NULL_HANDLE;
        static PFN_vkCmdDebugMarkerEndEXT pfnCmdDebugMarkerEnd              = VK_NULL_HANDLE;
        
        inline void setup(VkDevice device)
        {
            bool is_extension_present = extension::is_present(VK_EXT_DEBUG_MARKER_EXTENSION_NAME);

            if (is_extension_present)
            {
                vkDebugMarkerSetObjectName  = reinterpret_cast<PFN_vkDebugMarkerSetObjectNameEXT>(vkGetDeviceProcAddr(device, "vkDebugMarkerSetObjectNameEXT"));
                pfnCmdDebugMarkerBegin      = reinterpret_cast<PFN_vkCmdDebugMarkerBeginEXT>(vkGetDeviceProcAddr(device, "vkCmdDebugMarkerBeginEXT"));
                pfnCmdDebugMarkerEnd        = reinterpret_cast<PFN_vkCmdDebugMarkerEndEXT>(vkGetDeviceProcAddr(device, "vkCmdDebugMarkerEndEXT"));
            }
            else
            {
                LOG_WARNING("Extension \"%s\" not present, debug markers are disabled.", VK_EXT_DEBUG_MARKER_EXTENSION_NAME);
                LOG_INFO("Try running from inside a Vulkan graphics debugger (e.g. RenderDoc)");
            }

            active =
                (vkDebugMarkerSetObjectName != VK_NULL_HANDLE)  &&
                (pfnCmdDebugMarkerBegin != VK_NULL_HANDLE)      &&
                (pfnCmdDebugMarkerEnd != VK_NULL_HANDLE);
        }

        inline void begin(VkCommandBuffer cmd_buffer, const char* name, const Math::Vector4& color)
        {
            if (!active)
                return;

            VkDebugMarkerMarkerInfoEXT marker_info = {};
            marker_info.sType = VK_STRUCTURE_TYPE_DEBUG_MARKER_MARKER_INFO_EXT;
            memcpy(marker_info.color, color.Data(), sizeof(float) * 4);
            marker_info.pMarkerName = name;
            pfnCmdDebugMarkerBegin(cmd_buffer, &marker_info);
        }

        inline void end(VkCommandBuffer cmdBuffer)
        {
            if (!active)
                return;

            pfnCmdDebugMarkerEnd(cmdBuffer);
        }

        inline void set_object_name(VkDevice device, uint64_t object, VkDebugReportObjectTypeEXT object_type, const char* name)
        {
            if (!active)
                return;

            VkDebugMarkerObjectNameInfoEXT name_info    = {};
            name_info.sType                             = VK_STRUCTURE_TYPE_DEBUG_MARKER_OBJECT_NAME_INFO_EXT;
            name_info.objectType                        = object_type;
            name_info.object                            = object;
            name_info.pObjectName                       = name;
            vkDebugMarkerSetObjectName(device, &name_info);
        }
    };
}

#endif
