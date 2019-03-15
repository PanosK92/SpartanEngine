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
#include "Vulkan_Helper.h"
#ifdef API_GRAPHICS_VULKAN 
//================================

//= INCLUDES ==================
#include "../RHI_Device.h"
#include "../../Math/Vector4.h"
#include "../../Logging/Log.h"
#include "../../Core/Settings.h"
#include <string>
//=============================

//= NAMESPACES ================
using namespace std;
using namespace Directus::Math;
//=============================

namespace Directus
{
	namespace VulkanInstance
	{
		VkDebugUtilsMessengerEXT callback	= nullptr;
		VkInstance instance					= nullptr;
		VkPhysicalDevice device_physical	= nullptr;
		VkDevice device						= nullptr;
		VkQueue present_queue				= nullptr;
	}

	RHI_Device::RHI_Device()
	{
		// Validation layer
		auto validation_layer_available = false;
		if (VulkanHelper::validation_layer_enabled)
		{
			validation_layer_available = VulkanHelper::acquire_validation_layers(VulkanHelper::validation_layers);
		}
		
		// Create instance
		{
			VkApplicationInfo app_info	= {};
			app_info.sType				= VK_STRUCTURE_TYPE_APPLICATION_INFO;
			app_info.pApplicationName	= ENGINE_VERSION;
			app_info.applicationVersion	= VK_MAKE_VERSION(1, 0, 0);
			app_info.pEngineName		= ENGINE_VERSION;
			app_info.engineVersion		= VK_MAKE_VERSION(1, 0, 0);
			app_info.apiVersion			= VK_API_VERSION_1_1;

			VkInstanceCreateInfo create_info	= {};
			create_info.sType					= VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
			create_info.pApplicationInfo		= &app_info;
			create_info.enabledExtensionCount	= static_cast<uint32_t>(VulkanHelper::extensions_device_physical.size());
			create_info.ppEnabledExtensionNames	= VulkanHelper::extensions_device_physical.data();
			if (validation_layer_available) 
			{
				create_info.enabledLayerCount	= static_cast<uint32_t>(VulkanHelper::validation_layers.size());
				create_info.ppEnabledLayerNames	= VulkanHelper::validation_layers.data();
			}
			else 
			{
				create_info.enabledLayerCount = 0;
			}

			const auto result = vkCreateInstance(&create_info, nullptr, &VulkanInstance::instance);
			if (result != VK_SUCCESS)
			{
				LOG_ERROR("Failed to create instance.");
				return;
			}
		}
		
		// Get available extensions
		{
			uint32_t extension_count = 0;
			vkEnumerateInstanceExtensionProperties(nullptr, &extension_count, nullptr);
			std::vector<VkExtensionProperties> extensions(extension_count);
			vkEnumerateInstanceExtensionProperties(nullptr, &extension_count, extensions.data());
			for (const auto& extension : extensions)
			{
				LOGF_INFO("Available extension: %s", extension.extensionName);
			}
		}

		// Callback
		if (VulkanHelper::validation_layer_enabled)
		{
			VkDebugUtilsMessengerCreateInfoEXT create_info = {};
			create_info.sType			= VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
			create_info.messageSeverity	= VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
			create_info.messageType		= VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
			create_info.pfnUserCallback	= VulkanHelper::debugCallback;
			create_info.pUserData		= nullptr; // Optional

			if (VulkanHelper::create_debug_utils_messenger_ext(VulkanInstance::instance, &create_info, nullptr, &VulkanInstance::callback) != VK_SUCCESS) 
			{
				LOG_ERROR("Failed to setup debug callback");
			}
		}

		// Device Physical
		{
			uint32_t device_count = 0;
			vkEnumeratePhysicalDevices(VulkanInstance::instance, &device_count, nullptr);
			if (device_count == 0) 
			{
				LOG_ERROR("Failed to enumerate physical devices.");
				return;
			}
			std::vector<VkPhysicalDevice> devices(device_count);
			vkEnumeratePhysicalDevices(VulkanInstance::instance, &device_count, devices.data());
			
			for (const auto& device : devices) 
			{
				if (VulkanHelper::is_device_suitable(device))
				{
					VulkanInstance::device_physical = device;
					break;
				}
			}

			if (!VulkanInstance::device_physical) 
			{
				LOG_ERROR("Failed to find a suitable device.");
				return;
			}
		}

		// Device
		VkPhysicalDeviceFeatures device_features = {};
		VkDeviceCreateInfo create_info = {};
		{
			auto indices = VulkanHelper::find_queue_families(VulkanInstance::device_physical);

			VkDeviceQueueCreateInfo queue_create_info	= {};
			queue_create_info.sType						= VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
			queue_create_info.queueFamilyIndex			= indices.graphics_family.value();
			queue_create_info.queueCount				= 1;

			auto queue_priority = 1.0f;
			queue_create_info.pQueuePriorities = &queue_priority;

			create_info.sType					= VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
			create_info.pQueueCreateInfos		= &queue_create_info;
			create_info.queueCreateInfoCount	= 1;
			create_info.pEnabledFeatures		= &device_features;
			create_info.enabledExtensionCount	= static_cast<uint32_t>(VulkanHelper::extensions_device.size());
			create_info.ppEnabledExtensionNames = VulkanHelper::extensions_device.data();

			if (VulkanHelper::validation_layer_enabled)
			{
			    create_info.enabledLayerCount	= static_cast<uint32_t>(VulkanHelper::validation_layers.size());
			    create_info.ppEnabledLayerNames	= VulkanHelper::validation_layers.data();
			}
			else 
			{
			    create_info.enabledLayerCount = 0;
			}

			if (vkCreateDevice(VulkanInstance::device_physical, &create_info, nullptr, &VulkanInstance::device) != VK_SUCCESS) 
			{
				LOG_ERROR("Failed to create device.");
			}
		}

		// Present Queue
		{
			auto indices = VulkanHelper::find_queue_families(VulkanInstance::device_physical);

			vector<VkDeviceQueueCreateInfo> queue_create_infos;
			set<uint32_t> unique_queue_families = { indices.graphics_family.value(), indices.present_family.value() };

			auto queue_priority = 1.0f;
			for (auto queue_family : unique_queue_families) 
			{
				 VkDeviceQueueCreateInfo queue_create_info = {};
				 queue_create_info.sType				= VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
				 queue_create_info.queueFamilyIndex		= queue_family;
				 queue_create_info.queueCount			= 1;
				 queue_create_info.pQueuePriorities		= &queue_priority;
				 queue_create_infos.push_back(queue_create_info);
			}

			create_info.queueCreateInfoCount	= static_cast<uint32_t>(queue_create_infos.size());
			create_info.pQueueCreateInfos		= queue_create_infos.data();

			const auto device = VulkanInstance::device;
			vkGetDeviceQueue(device, indices.present_family.value(), 0, &VulkanInstance::present_queue);
		}

		m_instance			= static_cast<void*>(VulkanInstance::instance);
		m_device_physical	= static_cast<void*>(VulkanInstance::device_physical);
		m_device			= static_cast<void*>(VulkanInstance::device);
		m_present_queue		= static_cast<void*>(VulkanInstance::present_queue);
		
		Settings::Get().m_versionGraphicsAPI = to_string(VK_API_VERSION_1_0);
		LOG_INFO(Settings::Get().m_versionGraphicsAPI);
		m_initialized = true;
	}

	RHI_Device::~RHI_Device()
	{	
		if (VulkanHelper::validation_layer_enabled)
		{
			VulkanHelper::destroy_debug_utils_messenger_ext(VulkanInstance::instance, VulkanInstance::callback, nullptr);
		}
		vkDestroyInstance(VulkanInstance::instance, nullptr);
		vkDestroyDevice(VulkanInstance::device, nullptr);
	}

	bool RHI_Device::Draw(unsigned int vertex_count) const
	{
		return true;
	}

	bool RHI_Device::DrawIndexed(const unsigned int index_count, const unsigned int index_offset, const unsigned int vertex_offset) const
	{
		return true;
	}

	bool RHI_Device::ClearRenderTarget(void* render_target, const Vector4& color) const
	{
		return true;
	}

	bool RHI_Device::ClearDepthStencil(void* depth_stencil, const unsigned int flags, const float depth, const unsigned int stencil) const
	{
		return true;
	}

	bool RHI_Device::SetVertexBuffer(const std::shared_ptr<RHI_VertexBuffer>& buffer) const
	{
		return true;
	}

	bool RHI_Device::SetIndexBuffer(const std::shared_ptr<RHI_IndexBuffer>& buffer) const
	{
		return true;
	}

	bool RHI_Device::SetVertexShader(const std::shared_ptr<RHI_Shader>& shader) const
	{
		return true;
	}

	bool RHI_Device::SetPixelShader(const std::shared_ptr<RHI_Shader>& shader) const
	{
		return true;
	}

	bool RHI_Device::SetConstantBuffers(const unsigned int start_slot, const unsigned int buffer_count, void* buffer, const RHI_Buffer_Scope scope) const
	{
		return true;
	}

	bool RHI_Device::SetSamplers(const unsigned int start_slot, const unsigned int sampler_count, void* samplers) const
	{
		return true;
	}

	bool RHI_Device::SetRenderTargets(const unsigned int render_target_count, void* render_targets, void* depth_stencil) const
	{
		return true;
	}

	bool RHI_Device::SetTextures(const unsigned int start_slot, const unsigned int resource_count, void* shader_resources) const
	{
		return true;
	}

	bool RHI_Device::SetViewport(const RHI_Viewport& viewport) const
	{
		return true;
	}

	bool RHI_Device::SetScissorRectangle(const Math::Rectangle& rectangle) const
	{
		return true;
	}

	bool RHI_Device::SetDepthStencilState(const std::shared_ptr<RHI_DepthStencilState>& depth_stencil_state) const
	{
		return true;
	}

	bool RHI_Device::SetBlendState(const std::shared_ptr<RHI_BlendState>& blend_state) const
	{
		return true;
	}

	bool RHI_Device::SetPrimitiveTopology(const RHI_PrimitiveTopology_Mode primitive_topology) const
	{
		return true;
	}

	bool RHI_Device::SetInputLayout(const std::shared_ptr<RHI_InputLayout>& input_layout) const
	{
		return true;
	}

	bool RHI_Device::SetRasterizerState(const std::shared_ptr<RHI_RasterizerState>& rasterizer_state) const
	{
		return true;
	}

	void RHI_Device::EventBegin(const std::string& name)
	{

	}

	void RHI_Device::EventEnd()
	{

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
}
#endif