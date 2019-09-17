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

//= INCLUDES ========================
#include <string>
#include "../RHI_Device.h"
#include "../../Logging/Log.h"
#include "../../Core/Settings.h"
#include "../../Core/Context.h"
#include "../../Core/Engine.h"
#include "../../Rendering/Renderer.h"
//===================================

//= NAMESPACES ===============
using namespace std;
using namespace Spartan::Math;
//============================

namespace Spartan
{
    namespace _Vulkan_Device
    {
        static VKAPI_ATTR VkBool32 VKAPI_CALL debug_callback(
            VkDebugUtilsMessageSeverityFlagBitsEXT message_severity,
            VkDebugUtilsMessageTypeFlagsEXT message_type,
            const VkDebugUtilsMessengerCallbackDataEXT* p_callback_data,
            void* p_user_data
        ) {
            auto type = Spartan::Log_Info;
            type = message_severity == VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT  ? Log_Warning : type;
            type = message_severity == VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT    ? Log_Error : type;

            Log::m_caller_name = "Vulkan";
            Log::Write(p_callback_data->pMessage, type);
            Log::m_caller_name = "";

            return VK_FALSE;
        }

        inline VkResult debug_create(RHI_Device* rhi_device, const VkDebugUtilsMessengerCreateInfoEXT* create_info)
        {
            if (const auto func = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(vkGetInstanceProcAddr(rhi_device->GetContextRhi()->instance, "vkCreateDebugUtilsMessengerEXT")))
                return func(rhi_device->GetContextRhi()->instance, create_info, nullptr, &rhi_device->GetContextRhi()->callback_handle);

            return VK_ERROR_EXTENSION_NOT_PRESENT;
        }

        inline void debug_destroy(RHI_Context* context)
        {
            if (!context->validation_enabled)
                return;

            if (const auto func = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(vkGetInstanceProcAddr(context->instance, "vkDestroyDebugUtilsMessengerEXT")))
            {
                func(context->instance, context->callback_handle, nullptr);
            }
        }

        inline QueueFamilyIndices get_family_indices(RHI_Device* rhi_device, VkSurfaceKHR& surface, const VkPhysicalDevice& _physical_device)
        {
            QueueFamilyIndices indices;

            uint32_t queue_family_count = 0;
            vkGetPhysicalDeviceQueueFamilyProperties(_physical_device, &queue_family_count, nullptr);

            std::vector<VkQueueFamilyProperties> queue_family_properties(queue_family_count);
            vkGetPhysicalDeviceQueueFamilyProperties(_physical_device, &queue_family_count, queue_family_properties.data());

            int i = 0;
            for (const auto& queue_family_property : queue_family_properties)
            {
                VkBool32 present_support = false;
                vkGetPhysicalDeviceSurfaceSupportKHR(_physical_device, i, surface, &present_support);
                if (queue_family_property.queueCount > 0)
                {
                    indices.present_family = i;
                }

                if (queue_family_property.queueCount > 0 && queue_family_property.queueFlags & VK_QUEUE_GRAPHICS_BIT)
                {
                    indices.graphics_family = i;
                }

                if (queue_family_property.queueCount > 0 && queue_family_property.queueFlags & VK_QUEUE_TRANSFER_BIT)
                {
                    indices.copy_family = i;
                }

                if (indices.IsComplete())
                    break;

                i++;
            }

            return indices;
        }

        inline bool check_extension_support(RHI_Device* rhi_device, const VkPhysicalDevice device)
        {
            uint32_t extensionCount;
            vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, nullptr);

            std::vector<VkExtensionProperties> available_extensions(extensionCount);
            vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, available_extensions.data());

            std::set<std::string> required_extensions(rhi_device->GetContextRhi()->extensions_device.begin(), rhi_device->GetContextRhi()->extensions_device.end());
            for (const auto& extension : available_extensions)
            {
                required_extensions.erase(extension.extensionName);
            }

            return required_extensions.empty();
        }

        inline bool choose_physical_device(RHI_Device* rhi_device, void* window_handle, const std::vector<VkPhysicalDevice>& physical_devices)
        {
            // Temporarily create a surface just to check compatibility
            VkSurfaceKHR surface_temp = nullptr;
            {
                VkWin32SurfaceCreateInfoKHR create_info = {};
                create_info.sType                       = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
                create_info.hwnd                        = static_cast<HWND>(window_handle);
                create_info.hinstance                   = GetModuleHandle(nullptr);

                const auto result = vkCreateWin32SurfaceKHR(rhi_device->GetContextRhi()->instance, &create_info, nullptr, &surface_temp);
                if (result != VK_SUCCESS)
                {
                    LOGF_ERROR("Failed to create Win32 surface, %s.", Vulkan_Common::to_string(result));
                    return false;
                }
            }

            for (const auto& device : physical_devices)
            {
                const bool extensions_supported = check_extension_support(rhi_device, device);
                auto _indices = get_family_indices(rhi_device, surface_temp, device);

                const bool is_suitable = _indices.IsComplete() && extensions_supported;
                if (is_suitable)
                {
                    rhi_device->GetContextRhi()->device_physical   = device;
                    rhi_device->GetContextRhi()->indices           = _indices;
                    return true;
                }
            }

            // Destroy the surface
            vkDestroySurfaceKHR(rhi_device->GetContextRhi()->instance, surface_temp, nullptr);

            return false;
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

        inline bool has_validation_layer_support(RHI_Device* rhi_device)
        {
            uint32_t layer_count;
            vkEnumerateInstanceLayerProperties(&layer_count, nullptr);

            std::vector<VkLayerProperties> available_layers(layer_count);
            vkEnumerateInstanceLayerProperties(&layer_count, available_layers.data());

            for (auto layer_name : rhi_device->GetContextRhi()->validation_layers)
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

	RHI_Device::RHI_Device(Context* context)
	{
        m_context       = context;
		m_rhi_context   = make_shared<RHI_Context>();

		// Create instance
		VkApplicationInfo app_info = {};
		{
			app_info.sType				= VK_STRUCTURE_TYPE_APPLICATION_INFO;
			app_info.pApplicationName	= engine_version;		
			app_info.pEngineName		= engine_version;
			app_info.engineVersion		= VK_MAKE_VERSION(1, 0, 0);
			app_info.applicationVersion	= VK_MAKE_VERSION(1, 0, 0);
			app_info.apiVersion			= VK_API_VERSION_1_1;

			VkInstanceCreateInfo create_info	= {};
			create_info.sType					= VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
			create_info.pApplicationInfo		= &app_info;
			create_info.enabledExtensionCount	= static_cast<uint32_t>(m_rhi_context->extensions_instance.size());
			create_info.ppEnabledExtensionNames	= m_rhi_context->extensions_instance.data();
			create_info.enabledLayerCount		= 0;

			if (m_rhi_context->validation_enabled)
			{
				if (_Vulkan_Device::has_validation_layer_support(this))
				{
					create_info.enabledLayerCount	= static_cast<uint32_t>(m_rhi_context->validation_layers.size());
					create_info.ppEnabledLayerNames = m_rhi_context->validation_layers.data();
				}
				else
				{
					LOG_ERROR("Validation layer was requested, but not available.");
				}
			}

			auto result = vkCreateInstance(&create_info, nullptr, &m_rhi_context->instance);
			if (result != VK_SUCCESS)
			{
				LOGF_ERROR("Failed to create instance, %s.", Vulkan_Common::to_string(result));
				return;
			}
		}

		_Vulkan_Device::log_available_extensions();

		// Debug callback
		if (m_rhi_context->validation_enabled)
		{
			VkDebugUtilsMessengerCreateInfoEXT create_info	= {};
			create_info.sType								= VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
			create_info.messageSeverity						= VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
			create_info.messageType							= VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
			create_info.pfnUserCallback						= _Vulkan_Device::debug_callback;

			if (_Vulkan_Device::debug_create(this, &create_info) != VK_SUCCESS)
			{
				LOG_ERROR("Failed to setup debug callback");
			}
		}

		// Device Physical
		{
			uint32_t device_count = 0;
			vkEnumeratePhysicalDevices(m_rhi_context->instance, &device_count, nullptr);
			if (device_count == 0) 
			{
				LOG_ERROR("Failed to enumerate physical devices.");
				return;
			}
			std::vector<VkPhysicalDevice> physical_devices(device_count);
			vkEnumeratePhysicalDevices(m_rhi_context->instance, &device_count, physical_devices.data());
			
			if (!_Vulkan_Device::choose_physical_device(this, context->m_engine->GetWindowData().handle, physical_devices))
			{
				LOG_ERROR("Failed to find a suitable device.");
				return;
			}
		}

		// Device
		{
			// Queue info
			vector<VkDeviceQueueCreateInfo> queue_create_infos;
			{
				set<uint32_t> unique_queue_families =
				{
					m_rhi_context->indices.graphics_family.value(),
					m_rhi_context->indices.present_family.value(),
					m_rhi_context->indices.copy_family.value()
				};
				auto queue_priority = 1.0f;
				for (auto queue_family : unique_queue_families)
				{
					VkDeviceQueueCreateInfo queue_create_info	= {};
					queue_create_info.sType						= VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
					queue_create_info.queueFamilyIndex			= queue_family;
					queue_create_info.queueCount				= 1;
					queue_create_info.pQueuePriorities			= &queue_priority;
					queue_create_infos.push_back(queue_create_info);
				}
			}

			// Describe
			VkPhysicalDeviceFeatures device_features	= {};
			device_features.samplerAnisotropy			= m_context->GetSubsystem<Renderer>()->GetAnisotropy() != 0;

			VkDeviceCreateInfo create_info	= {};
			{
				create_info.sType					= VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
				create_info.queueCreateInfoCount	= static_cast<uint32_t>(queue_create_infos.size());
				create_info.pQueueCreateInfos		= queue_create_infos.data();
				create_info.pEnabledFeatures		= &device_features;
				create_info.enabledExtensionCount	= static_cast<uint32_t>(m_rhi_context->extensions_device.size());
				create_info.ppEnabledExtensionNames = m_rhi_context->extensions_device.data();
				if (m_rhi_context->validation_enabled)
				{
					create_info.enabledLayerCount	= static_cast<uint32_t>(m_rhi_context->validation_layers.size());
					create_info.ppEnabledLayerNames = m_rhi_context->validation_layers.data();
				}
				else
				{
					create_info.enabledLayerCount = 0;
				}
			}

			// Create
			auto result = vkCreateDevice(m_rhi_context->device_physical, &create_info, nullptr, &m_rhi_context->device);
			if (result != VK_SUCCESS)
			{
				LOGF_ERROR("Failed to create device, %s.", Vulkan_Common::to_string(result));
				return;
			}
		}

		// Queues
		{
			vkGetDeviceQueue(m_rhi_context->device, m_rhi_context->indices.graphics_family.value(), 0, &m_rhi_context->queue_graphics);
			vkGetDeviceQueue(m_rhi_context->device, m_rhi_context->indices.present_family.value(), 0, &m_rhi_context->queue_present);
			vkGetDeviceQueue(m_rhi_context->device, m_rhi_context->indices.copy_family.value(), 0, &m_rhi_context->queue_copy);
		}

		// Detect and log version
		auto version_major	= to_string(VK_VERSION_MAJOR(app_info.apiVersion));
		auto version_minor	= to_string(VK_VERSION_MINOR(app_info.apiVersion));
		auto version_path	= to_string(VK_VERSION_PATCH(app_info.apiVersion));
        auto& settings = m_context->GetSubsystem<Settings>();
        settings->m_versionGraphicsAPI = version_major + "." + version_minor + "." + version_path;
		LOG_INFO("Vulkan " + settings->m_versionGraphicsAPI);

		m_initialized = true;
	}

	RHI_Device::~RHI_Device()
	{	
		// Wait for GPU
		const auto result = vkQueueWaitIdle(m_rhi_context->queue_graphics);

		// Release resources
		if (result == VK_SUCCESS)
		{
			_Vulkan_Device::debug_destroy(m_rhi_context.get());
			vkDestroyDevice(m_rhi_context->device, nullptr);
			vkDestroyInstance(m_rhi_context->instance, nullptr);
		}
		else
		{
			LOGF_ERROR("Failed to wait idle, %s.", Vulkan_Common::to_string(result));
		}
	}

	bool RHI_Device::ProfilingCreateQuery(void** query, const RHI_Query_Type type) const
	{
		return true;
	}

	bool RHI_Device::ProfilingQueryStart(void* query_object) const
	{
		return true;
	}

	bool RHI_Device::ProfilingGetTimeStamp(void* query_object) const
	{
		return true;
	}

	float RHI_Device::ProfilingGetDuration(void* query_disjoint, void* query_start, void* query_end) const
	{
		return 0.0f;
	}

	void RHI_Device::ProfilingReleaseQuery(void* query_object)
	{

	}

	uint32_t RHI_Device::ProfilingGetGpuMemory()
	{
		return 0;
	}

	uint32_t RHI_Device::ProfilingGetGpuMemoryUsage()
	{
		return 0;
	}
}
#endif
