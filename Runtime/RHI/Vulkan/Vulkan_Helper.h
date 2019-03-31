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
#include "../../Core/Settings.h"
#include "../../Math/MathHelper.h"
//================================

namespace vulkan_helper
{
	namespace debug_callback
	{
		static VKAPI_ATTR VkBool32 VKAPI_CALL callback(
			VkDebugUtilsMessageSeverityFlagBitsEXT message_severity,
			VkDebugUtilsMessageTypeFlagsEXT message_type,
			const VkDebugUtilsMessengerCallbackDataEXT* p_callback_data,
			void* p_user_data
		){
			auto type = Directus::Log_Info;
			type = message_severity == VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT ? Directus::Log_Warning : type;
			type = message_severity == VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT ? Directus::Log_Error : type;

			Directus::Log::m_log_to_file = true;
			Directus::Log::m_caller_name = "Vulkan";
			Directus::Log::Write(p_callback_data->pMessage, type);
			Directus::Log::m_caller_name = "";

			return VK_FALSE;
		}

		inline VkResult create(Directus::RHI_Context* context, const VkDebugUtilsMessengerCreateInfoEXT* create_info)
		{
			if (const auto func = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(vkGetInstanceProcAddr(context->instance, "vkCreateDebugUtilsMessengerEXT")))
				return func(context->instance, create_info, nullptr, &context->callback_handle);

			return VK_ERROR_EXTENSION_NOT_PRESENT;
		}

		inline void destroy(Directus::RHI_Context* context)
		{
			if (const auto func = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(vkGetInstanceProcAddr(context->instance, "vkDestroyDebugUtilsMessengerEXT")))
			{
				func(context->instance, context->callback_handle, nullptr);
			}
		}
	}

	namespace swap_chain
	{
		inline SwapChainSupportDetails check_surface_compatibility(Directus::RHI_Context* context, const VkSurfaceKHR surface)
		{
			SwapChainSupportDetails details;
			vkGetPhysicalDeviceSurfaceCapabilitiesKHR(context->device_physical, surface, &details.capabilities);

			uint32_t format_count;
			vkGetPhysicalDeviceSurfaceFormatsKHR(context->device_physical, surface, &format_count, nullptr);

			if (format_count != 0)
			{
				details.formats.resize(format_count);
				vkGetPhysicalDeviceSurfaceFormatsKHR(context->device_physical, surface, &format_count, details.formats.data());
			}

			uint32_t present_mode_count;
			vkGetPhysicalDeviceSurfacePresentModesKHR(context->device_physical, surface, &present_mode_count, nullptr);

			if (present_mode_count != 0)
			{
				details.present_modes.resize(present_mode_count);
				vkGetPhysicalDeviceSurfacePresentModesKHR(context->device_physical, surface, &present_mode_count, details.present_modes.data());
			}

			return details;
		}

		inline VkPresentModeKHR choose_present_mode(const std::vector<VkPresentModeKHR>& present_modes)
		{
			VkPresentModeKHR best_mode = VK_PRESENT_MODE_FIFO_KHR;

			for (const auto& present_mode : present_modes)
			{
				if (present_mode == VK_PRESENT_MODE_MAILBOX_KHR)
				{
					return present_mode;
				}
				else if (present_mode == VK_PRESENT_MODE_IMMEDIATE_KHR)
				{
					best_mode = present_mode;
				}
			}

			return best_mode;
		}

		inline VkSurfaceFormatKHR choose_format(const VkFormat prefered_format, const std::vector<VkSurfaceFormatKHR>& availableFormats)
		{
			VkColorSpaceKHR color_space = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;

			if (availableFormats.size() == 1 && availableFormats[0].format == VK_FORMAT_UNDEFINED)
			{
				LOG_ERROR("Failed to find format");
				return { prefered_format, color_space };
			}

			for (const auto& availableFormat : availableFormats)
			{
				if (availableFormat.format == prefered_format && availableFormat.colorSpace == color_space)
				{
					return availableFormat;
				}
			}

			return availableFormats[0];
		}


		inline VkExtent2D choose_extent(const VkSurfaceCapabilitiesKHR& capabilities)
		{
			using namespace std;
			using namespace Directus;
			using namespace Math::Helper;

			auto max = (numeric_limits<uint32_t>::max)();
			if (capabilities.currentExtent.width != max)
			{
				return capabilities.currentExtent;
			}
			else
			{
				VkExtent2D actual_extent	= { Settings::Get().GetWindowWidth(), Settings::Get().GetWindowHeight() };
				actual_extent.width			= Max(capabilities.minImageExtent.width, Min(capabilities.maxImageExtent.width, actual_extent.width));
				actual_extent.height		= Max(capabilities.minImageExtent.height, Min(capabilities.maxImageExtent.height, actual_extent.height));

				return actual_extent;
			}
		}
	}

	namespace physical_device
	{
		inline QueueFamilyIndices get_family_indices(const VkPhysicalDevice device)
		{
			uint32_t queue_family_count = 0;
			vkGetPhysicalDeviceQueueFamilyProperties(device, &queue_family_count, nullptr);

			std::vector<VkQueueFamilyProperties> queue_family_properties(queue_family_count);
			vkGetPhysicalDeviceQueueFamilyProperties(device, &queue_family_count, queue_family_properties.data());

			QueueFamilyIndices _indices;
			int i = 0;
			for (const auto& queue_family_property : queue_family_properties)
			{
				if (queue_family_property.queueCount > 0 && queue_family_property.queueFlags & VK_QUEUE_GRAPHICS_BIT)
				{
					_indices.graphics_family = i;
				}

				if (queue_family_property.queueCount > 0)
				{
					_indices.present_family = i;
				}

				if (_indices.IsComplete())
					break;

				i++;
			}

			return _indices;
		}

		inline bool check_extension_support(Directus::RHI_Context* context, const VkPhysicalDevice device)
		{
			uint32_t extensionCount;
			vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, nullptr);

			std::vector<VkExtensionProperties> available_extensions(extensionCount);
			vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, available_extensions.data());

			std::set<std::string> required_extensions(context->extensions_device.begin(), context->extensions_device.end());
			for (const auto& extension : available_extensions)
			{
				required_extensions.erase(extension.extensionName);
			}

			return required_extensions.empty();
		}

		inline bool choose(Directus::RHI_Context* context, const std::vector<VkPhysicalDevice>& physical_devices)
		{
			for (const auto& device : physical_devices)
			{		
				bool extensions_supported	= check_extension_support(context, device);		
				auto _indices				= get_family_indices(device);

				bool is_suitable = _indices.IsComplete() && extensions_supported;
				if (is_suitable)
				{			
					context->device_physical	= device;
					context->indices			= _indices;
					return true;
				}
			}

			return false;
		}
	}

	namespace command_list
	{
		inline bool create_command_buffer(Directus::RHI_Context* context, VkCommandBuffer* cmd_buffer, VkCommandPool cmd_pool, VkCommandBufferLevel level)
		{
			VkCommandBufferAllocateInfo allocInfo = {};
			allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
			allocInfo.commandPool			= cmd_pool;
			allocInfo.level					= level;
			allocInfo.commandBufferCount	= 1;

			return vkAllocateCommandBuffers(context->device, &allocInfo, cmd_buffer) == VK_SUCCESS;
		}

		inline bool create_command_pool(Directus::RHI_Context* context, VkCommandPool* cmd_pool)
		{
			VkCommandPoolCreateInfo cmdPoolInfo = {};
			cmdPoolInfo.sType				= VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
			cmdPoolInfo.queueFamilyIndex	= context->indices.present_family.value();
			cmdPoolInfo.flags				= VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;

			return vkCreateCommandPool(context->device, &cmdPoolInfo, nullptr, cmd_pool) == VK_SUCCESS;
		}
	}

	inline void log_available_extensions()
	{
		uint32_t extension_count = 0;
		vkEnumerateInstanceExtensionProperties(nullptr, &extension_count, nullptr);
		std::vector<VkExtensionProperties> extensions(extension_count);
		vkEnumerateInstanceExtensionProperties(nullptr, &extension_count, extensions.data());
		for (const auto& extension : extensions)
		{
			LOGF_INFO("%s", extension.extensionName);
		}
	}

	inline bool check_validation_layers(Directus::RHI_Context* context)
	{
		uint32_t layer_count;
		vkEnumerateInstanceLayerProperties(&layer_count, nullptr);

		std::vector<VkLayerProperties> available_layers(layer_count);
		vkEnumerateInstanceLayerProperties(&layer_count, available_layers.data());

		for (auto layer_name : context->validation_layers)
		{
			for (const auto& layer_properties : available_layers)
			{
				if (strcmp(layer_name, layer_properties.layerName) == 0)
					return true;
			}
		}

		return false;
	}
}
#endif