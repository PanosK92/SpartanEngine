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

//= INCLUDES =====================
#include <optional>
#include <set>
#include "../../Logging/Log.h"
#include "../RHI_Device.h"
//================================

namespace Spartan::Vulkan_Common
{
	inline const char* to_string(const VkResult result)
	{
		switch (result)
		{
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
		}

		return "Unknown error code";
	}

	namespace memory
	{
		inline uint32_t get_type(const VkPhysicalDevice device, const VkMemoryPropertyFlags properties, const uint32_t type_bits)
		{
			VkPhysicalDeviceMemoryProperties prop;
			vkGetPhysicalDeviceMemoryProperties(device, &prop);
			for (uint32_t i = 0; i < prop.memoryTypeCount; i++)
				if ((prop.memoryTypes[i].propertyFlags & properties) == properties && type_bits & (1 << i))
					return i;

			return 0xFFFFFFFF; // Unable to find memoryType
		}

		inline void free(const std::shared_ptr<RHI_Device>& rhi_device, void*& device_memory)
		{
			if (!device_memory)
				return;

			vkFreeMemory(rhi_device->GetContextRhi()->device, static_cast<VkDeviceMemory>(device_memory), nullptr);
			device_memory = nullptr;
		}
	}

	namespace commands
	{
		inline void cmd_buffer(const std::shared_ptr<RHI_Device>& rhi_device, VkCommandBuffer& cmd_buffer, VkCommandPool& cmd_pool, const VkCommandBufferLevel level)
		{
			VkCommandBufferAllocateInfo allocate_info	= {};
			allocate_info.sType							= VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
			allocate_info.commandPool					= cmd_pool;
			allocate_info.level							= level;
			allocate_info.commandBufferCount			= 1;

			SPARTAN_ASSERT(vkAllocateCommandBuffers(rhi_device->GetContextRhi()->device, &allocate_info, &cmd_buffer) == VK_SUCCESS);
		}

		inline void cmd_pool(const std::shared_ptr<RHI_Device>& rhi_device, void*& cmd_pool)
		{
			VkCommandPoolCreateInfo cmd_pool_info	= {};
			cmd_pool_info.sType						= VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
			cmd_pool_info.queueFamilyIndex			= rhi_device->GetContextRhi()->indices.graphics_family.value();
			cmd_pool_info.flags						= VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;

            const auto cmd_pool_temp = reinterpret_cast<VkCommandPool*>(&cmd_pool);
			SPARTAN_ASSERT(vkCreateCommandPool(rhi_device->GetContextRhi()->device, &cmd_pool_info, nullptr, cmd_pool_temp) == VK_SUCCESS);
		}
	}

	namespace semaphore
	{
		inline void* create(const std::shared_ptr<RHI_Device>& rhi_device)
		{
			VkSemaphoreCreateInfo semaphore_info	= {};
			semaphore_info.sType					= VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

			VkSemaphore semaphore_out;
			SPARTAN_ASSERT(vkCreateSemaphore(rhi_device->GetContextRhi()->device, &semaphore_info, nullptr, &semaphore_out) == VK_SUCCESS);

			return static_cast<void*>(semaphore_out);
		}

		inline void destroy(const std::shared_ptr<RHI_Device>& rhi_device, void*& semaphore_in)
		{
			if (!semaphore_in)
				return;

			vkDestroySemaphore(rhi_device->GetContextRhi()->device, static_cast<VkSemaphore>(semaphore_in), nullptr);
			semaphore_in = nullptr;
		}
	}

	namespace fence
	{
		inline void* create(const std::shared_ptr<RHI_Device>& rhi_device)
		{
			VkFenceCreateInfo fence_info	= {};
			fence_info.sType				= VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
	
			VkFence fence_out;
			SPARTAN_ASSERT(vkCreateFence(rhi_device->GetContextRhi()->device, &fence_info, nullptr, &fence_out) == VK_SUCCESS);

			return static_cast<void*>(fence_out);
		}

		inline void destroy(const std::shared_ptr<RHI_Device>& rhi_device, void*& fence_in)
		{
			if (!fence_in)
				return;

			vkDestroyFence(rhi_device->GetContextRhi()->device, static_cast<VkFence>(fence_in), nullptr);
			fence_in = nullptr;
		}

		inline void wait(const std::shared_ptr<RHI_Device>&rhi_device, void*& fence_in)
		{
            const auto fence_temp = reinterpret_cast<VkFence*>(&fence_in);
			SPARTAN_ASSERT(vkWaitForFences(rhi_device->GetContextRhi()->device, 1, fence_temp, true, 0xFFFFFFFFFFFFFFFF) == VK_SUCCESS);
		}

		inline void reset(const std::shared_ptr<RHI_Device>& rhi_device, void*& fence_in)
		{
            const auto fence_temp = reinterpret_cast<VkFence*>(&fence_in);
			SPARTAN_ASSERT(vkResetFences(rhi_device->GetContextRhi()->device, 1, fence_temp) == VK_SUCCESS);
		}

		inline void wait_reset(const std::shared_ptr<RHI_Device>& rhi_device, void*& fence_in)
		{
			const auto fence_temp = reinterpret_cast<VkFence*>(&fence_in);
			SPARTAN_ASSERT(vkWaitForFences(rhi_device->GetContextRhi()->device, 1, fence_temp, true, 0xFFFFFFFFFFFFFFFF) == VK_SUCCESS);
			SPARTAN_ASSERT(vkResetFences(rhi_device->GetContextRhi()->device, 1, fence_temp) == VK_SUCCESS);
		}
	}

	namespace buffer
	{
		inline bool create(const std::shared_ptr<RHI_Device>& rhi_device, VkBuffer& _buffer, VkDeviceMemory& buffer_memory, VkDeviceSize& size, VkBufferUsageFlags usage)
		{
			VkBufferCreateInfo buffer_info	= {};
			buffer_info.sType				= VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
			buffer_info.size				= size;
			buffer_info.usage				= usage;
			buffer_info.sharingMode			= VK_SHARING_MODE_EXCLUSIVE;

			auto result = vkCreateBuffer(rhi_device->GetContextRhi()->device, &buffer_info, nullptr, &_buffer);
			if (result != VK_SUCCESS)
			{
				LOG_ERROR(to_string(result));
				return false;
			}

			VkMemoryRequirements memory_requirements;
			vkGetBufferMemoryRequirements(rhi_device->GetContextRhi()->device, _buffer, &memory_requirements);

			VkMemoryAllocateInfo alloc_info = {};			
			alloc_info.sType				= VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
			alloc_info.allocationSize		= memory_requirements.size;
			alloc_info.memoryTypeIndex		= memory::get_type(rhi_device->GetContextRhi()->device_physical, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, memory_requirements.memoryTypeBits);

			result = vkAllocateMemory(rhi_device->GetContextRhi()->device, &alloc_info, nullptr, &buffer_memory);
			if (result != VK_SUCCESS)
			{
				LOG_ERROR(to_string(result));
				return false;
			}

			result = vkBindBufferMemory(rhi_device->GetContextRhi()->device, _buffer, buffer_memory, 0);
			if (result != VK_SUCCESS)
			{
				LOG_ERROR(to_string(result));
				return false;
			}

			return true;
		}

		inline void destroy(const std::shared_ptr<RHI_Device>& rhi_device, void*& _buffer)
		{
			if (!_buffer)
				return;

			vkDestroyBuffer(rhi_device->GetContextRhi()->device, static_cast<VkBuffer>(_buffer), nullptr);
			_buffer = nullptr;
		}
	}
}
#endif
