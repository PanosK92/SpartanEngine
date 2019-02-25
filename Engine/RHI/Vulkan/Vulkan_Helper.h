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
#ifdef API_GRAPHICS_VULKAN
//================================

namespace VulkanHelper
{ 
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
	
	inline VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(
		VkDebugUtilsMessageSeverityFlagBitsEXT message_severity,
		VkDebugUtilsMessageTypeFlagsEXT message_type,
		const VkDebugUtilsMessengerCallbackDataEXT* p_callback_data,
		void* p_user_data) 
	{
		auto type		= Directus::Log_Info;
		type			= message_severity == VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT	? Directus::Log_Warning	: type;
		type			= message_severity == VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT		? Directus::Log_Error	: type;
		Directus::Log::Write("Vulkan: " + std::string(p_callback_data->pMessage), type);
	
		return VK_FALSE;
	}

	struct QueueFamilyIndices 
	{
		std::optional<uint32_t> graphics_family;
		std::optional<uint32_t> present_family;
		bool IsComplete() const { return graphics_family.has_value() && present_family.has_value(); }
	};

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
			 if (queue_family.queueCount > 0 && queue_family.queueFlags & VK_QUEUE_GRAPHICS_BIT) 
			 {
			     indices.graphics_family = i;
			 }

			 if (indices.IsComplete()) 
			 {
			     break;
			 }

			 i++;
		}

		return indices;
	}

	inline bool is_device_suitable(const VkPhysicalDevice device) 
	{
		auto indices = find_queue_families(device);		return indices.IsComplete();
	}
}
#endif