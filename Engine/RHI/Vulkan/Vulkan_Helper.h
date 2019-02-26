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
#include "../../Logging/Log.h"
#include <optional>
#include <set>
#include "../../Core/Settings.h"
#include "../../Math/MathHelper.h"
#ifdef API_GRAPHICS_VULKAN
//================================

namespace VulkanHelper
{
	static std::vector<const char*> validation_layers = { "VK_LAYER_LUNARG_standard_validation" };
	static std::vector<const char*> extensions_device_physical = 
	{ 
		"VK_KHR_surface",
		"VK_KHR_win32_surface"
		#ifdef DEBUG
		, VK_EXT_DEBUG_UTILS_EXTENSION_NAME
		#endif
	};
	static std::vector<const char*> extensions_device = {  VK_KHR_SWAPCHAIN_EXTENSION_NAME };

	#ifdef DEBUG
	static bool validation_layer_enabled = true;
	#else
	static bool validation_layer_enabled = false;
	#endif

	struct QueueFamilyIndices
	{
		std::optional<uint32_t> graphics_family;
		std::optional<uint32_t> present_family;
		bool IsComplete() const { return graphics_family.has_value() && present_family.has_value(); }
	};

	struct SwapChainSupportDetails 
	{
		VkSurfaceCapabilitiesKHR capabilities;
		std::vector<VkSurfaceFormatKHR> formats;
		std::vector<VkPresentModeKHR> present_modes;
	};

	inline bool acquire_validation_layers(const std::vector<const char*>& validation_layers)
	{
		uint32_t layer_count;
		vkEnumerateInstanceLayerProperties(&layer_count, nullptr);
	
		std::vector<VkLayerProperties> available_layers(layer_count);
		vkEnumerateInstanceLayerProperties(&layer_count, available_layers.data());
	
		for (auto layer_name : validation_layers)
		{
			for (const auto& layer_properties : available_layers)
			{
				if (strcmp(layer_name, layer_properties.layerName) == 0)
				{

					return true;
				}
			}
		}
	
		LOG_ERROR("Validation layer was requested, but not available.");
		return false;
	}
	
	inline VkResult create_debug_utils_messenger_ext(VkInstance instance, const VkDebugUtilsMessengerCreateInfoEXT* p_create_info, const VkAllocationCallbacks* p_allocator, VkDebugUtilsMessengerEXT* p_callback)
	{
		if (const auto func = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(vkGetInstanceProcAddr(instance, "vkCreateDebugUtilsMessengerEXT")))
			return func(instance, p_create_info, p_allocator, p_callback);
	
		return VK_ERROR_EXTENSION_NOT_PRESENT;
	}
	
	inline void destroy_debug_utils_messenger_ext(VkInstance instance, VkDebugUtilsMessengerEXT callback, const VkAllocationCallbacks* p_allocator) 
	{
		if (const auto func = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(vkGetInstanceProcAddr(instance, "vkDestroyDebugUtilsMessengerEXT"))) 
		{
			func(instance, callback, p_allocator);
		}
	}
	
	inline VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(VkDebugUtilsMessageSeverityFlagBitsEXT message_severity, VkDebugUtilsMessageTypeFlagsEXT message_type, const VkDebugUtilsMessengerCallbackDataEXT* p_callback_data, void* p_user_data) 
	{
		auto type	= Directus::Log_Info;
		type		= message_severity == VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT	? Directus::Log_Warning	: type;
		type		= message_severity == VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT		? Directus::Log_Error	: type;

		Directus::Log::m_log_to_file = true;
		Directus::Log::m_caller_name = "Vulkan";
		Directus::Log::Write(p_callback_data->pMessage, type);
		Directus::Log::m_caller_name = "";

		return VK_FALSE;
	}

	inline QueueFamilyIndices find_queue_families(const VkPhysicalDevice device) 
	{
		QueueFamilyIndices indices;

		uint32_t queue_family_count = 0;
		vkGetPhysicalDeviceQueueFamilyProperties(device, &queue_family_count, nullptr);

		std::vector<VkQueueFamilyProperties> queue_families(queue_family_count);
		vkGetPhysicalDeviceQueueFamilyProperties(device, &queue_family_count, queue_families.data());

		auto i = 0;
		for (const auto& queue_family : queue_families) 
		{
			// Graphics support
			if (queue_family.queueCount > 0 && queue_family.queueFlags & VK_QUEUE_GRAPHICS_BIT) 
			{
			    indices.graphics_family = i;
			}

			// Present support - TODO FIX THIS TO WORK NICE WITH SEPARATE SWAPCHAIN CREATION
			//VkBool32 present_support = false;
			//vkGetPhysicalDeviceSurfaceSupportKHR(device, i, surface, &present_support);
			//if (queue_family.queueCount > 0 && present_support) 
			{
				indices.present_family = i;
			}
			
			if (indices.IsComplete()) 
			{
			    break;
			}		
			i++;
		}

		return indices;
	}

	inline bool check_device_extension_support(const VkPhysicalDevice device)
	{
		uint32_t extensionCount;
		vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, nullptr);

		std::vector<VkExtensionProperties> availableExtensions(extensionCount);
		vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, availableExtensions.data());

		std::set<std::string> required_extensions(extensions_device.begin(), extensions_device.end());

		for (const auto& extension : availableExtensions) 
		{
			required_extensions.erase(extension.extensionName);
		}

		return required_extensions.empty();
	}

	inline SwapChainSupportDetails query_swap_chain_support(const VkPhysicalDevice device, const VkSurfaceKHR surface)
	{
		SwapChainSupportDetails details;
		vkGetPhysicalDeviceSurfaceCapabilitiesKHR(device, surface, &details.capabilities);

		uint32_t format_count;
		vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface, &format_count, nullptr);

		if (format_count != 0) 
		{
			details.formats.resize(format_count);
			vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface, &format_count, details.formats.data());
		}

		uint32_t present_mode_count;
		vkGetPhysicalDeviceSurfacePresentModesKHR(device, surface, &present_mode_count, nullptr);

		if (present_mode_count != 0) 
		{
			details.present_modes.resize(present_mode_count);
			vkGetPhysicalDeviceSurfacePresentModesKHR(device, surface, &present_mode_count, details.present_modes.data());
		}

		return details;
	}

	inline bool is_device_suitable(const VkPhysicalDevice device)
	{
		auto indices = find_queue_families(device);
		bool extensions_supported = check_device_extension_support(device);
		return indices.IsComplete() && extensions_supported;
	}

	inline bool is_device_surface_compatible(const VkPhysicalDevice device, const VkSurfaceKHR surface)
	{
		auto swap_chain_support = query_swap_chain_support(device, surface);
		return !swap_chain_support.formats.empty() && !swap_chain_support.present_modes.empty();
	}

	inline VkSurfaceFormatKHR choose_swap_surface_format(const VkFormat prefered_format, const std::vector<VkSurfaceFormatKHR>& availableFormats) 
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

	inline VkPresentModeKHR choose_swap_present_mode(const std::vector<VkPresentModeKHR>& availablePresentModes) 
	{
		VkPresentModeKHR best_mode = VK_PRESENT_MODE_FIFO_KHR;

		for (const auto& available_present_mode : availablePresentModes) 
		{
			if (available_present_mode == VK_PRESENT_MODE_MAILBOX_KHR) 
			{
				return available_present_mode;
			}
			else if (available_present_mode == VK_PRESENT_MODE_IMMEDIATE_KHR) 
			{
				best_mode = available_present_mode;
			}
		}

		return best_mode;
	}

	inline VkExtent2D choose_swap_extent(const VkSurfaceCapabilitiesKHR& capabilities) 
	{
		using namespace std;
		using namespace Directus;
		using namespace Math::Helper;
		
		auto max = (std::numeric_limits<uint32_t>::max)();
		if (capabilities.currentExtent.width != max)
		{
			return capabilities.currentExtent;
		}
		else 
		{
			VkExtent2D actual_extent	= { Settings::Get().GetWindowWidth(), Settings::Get().GetWindowHeight() };
			actual_extent.width			= Max(capabilities.minImageExtent.width,	Min(capabilities.maxImageExtent.width, actual_extent.width));
			actual_extent.height		= Max(capabilities.minImageExtent.height,	Min(capabilities.maxImageExtent.height, actual_extent.height));

			return actual_extent;
		}
	}
}
#endif