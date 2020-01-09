/*
Copyright(c) 2016-2020 Panos Karabelas

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

            // Get the supported extensions out of the requested extensions
            vector<const char*> extensions_supported = vulkan_common::extension::get_supported_instance(m_rhi_context->extensions_instance);

			VkInstanceCreateInfo create_info	= {};
			create_info.sType					= VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
			create_info.pApplicationInfo		= &app_info;
			create_info.enabledExtensionCount	= static_cast<uint32_t>(extensions_supported.size());
			create_info.ppEnabledExtensionNames	= extensions_supported.data();
			create_info.enabledLayerCount		= 0;

			if (m_rhi_context->debug)
			{
                // Enable validation layer
				if (vulkan_common::layer::is_present(m_rhi_context->validation_layers.front()))
				{
                    // Validation features
                    VkValidationFeatureEnableEXT enabled_validation_features[]  = { VK_VALIDATION_FEATURE_ENABLE_BEST_PRACTICES_EXT };
                    VkValidationFeaturesEXT validation_features                 = {};
                    validation_features.sType                                   = VK_STRUCTURE_TYPE_VALIDATION_FEATURES_EXT;
                    validation_features.enabledValidationFeatureCount           = 1;
                    validation_features.pEnabledValidationFeatures              = enabled_validation_features;

                    // Validation layers
                    create_info.enabledLayerCount   = static_cast<uint32_t>(m_rhi_context->validation_layers.size());
                    create_info.ppEnabledLayerNames = m_rhi_context->validation_layers.data();
                    create_info.pNext               = &validation_features;
				}
				else
				{
					LOG_ERROR("Validation layer was requested, but not available.");
				}
			}

			if (!vulkan_common::error::check(vkCreateInstance(&create_info, nullptr, &m_rhi_context->instance)))
                return;
		}

        // Get function pointers (from extensions)
        vulkan_common::functions::initialize(this);

		// Debug
		if (m_rhi_context->debug)
		{
            vulkan_common::debug::initialize(m_rhi_context->instance);
		}

		// Find a physical device
        if (!vulkan_common::device::choose_physical_device(this, context->m_engine->GetWindowData().handle))
        {
            LOG_ERROR("Failed to find a suitable physical device.");
            return;
        }

		// Device
		{
			// Queue create info
			vector<VkDeviceQueueCreateInfo> queue_create_infos;
			{
				vector<uint32_t> unique_queue_families =
				{
					m_rhi_context->queue_graphics_family_index,
					m_rhi_context->queue_transfer_family_index,
					m_rhi_context->queue_compute_family_index
				};

				float queue_priority = 1.0f;
				for (const uint32_t& queue_family : unique_queue_families)
				{
					VkDeviceQueueCreateInfo queue_create_info	= {};
					queue_create_info.sType						= VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
					queue_create_info.queueFamilyIndex			= queue_family;
					queue_create_info.queueCount				= 1;
					queue_create_info.pQueuePriorities			= &queue_priority;
					queue_create_infos.push_back(queue_create_info);
				}
			}

            // Get device properties
            vkGetPhysicalDeviceProperties(static_cast<VkPhysicalDevice>(m_rhi_context->device_physical), &m_rhi_context->device_properties);

            // Resource limits
            m_rhi_context->max_texture_dimension_2d = m_rhi_context->device_properties.limits.maxImageDimension2D;

            // Disable profiler if timestamps are not supported
            if (m_rhi_context->profiler && !m_rhi_context->device_properties.limits.timestampComputeAndGraphics)
            {
                LOG_WARNING("Device doesn't support timestamps, disabling profiler...");
                m_rhi_context->profiler = false;
            }

			// Get device features
            VkPhysicalDeviceFeatures device_features_enabled = {};
            {
                m_rhi_context->device_features = {};
                vkGetPhysicalDeviceFeatures(m_rhi_context->device_physical, &m_rhi_context->device_features);

                #define ENABLE_FEATURE(feature)                                                                    \
                if (m_rhi_context->device_features.feature)                                                        \
                {                                                                                                  \
                        device_features_enabled.feature = VK_TRUE;                                                 \
                }                                                                                                  \
                else                                                                                               \
                {                                                                                                  \
                    LOG_WARNING("Requested device feature " #feature " is not supported by the physical device");  \
                    device_features_enabled.feature = VK_FALSE;                                                    \
                }

                ENABLE_FEATURE(samplerAnisotropy)
                ENABLE_FEATURE(fillModeNonSolid)
                ENABLE_FEATURE(wideLines)
            }

            // Determine enabled graphics shader stages
            m_enabled_graphics_shader_stages = VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
            if (device_features_enabled.geometryShader)
            {
                m_enabled_graphics_shader_stages = VK_PIPELINE_STAGE_GEOMETRY_SHADER_BIT;
            }
            if (device_features_enabled.tessellationShader)
            {
                m_enabled_graphics_shader_stages = VK_PIPELINE_STAGE_TESSELLATION_CONTROL_SHADER_BIT | VK_PIPELINE_STAGE_TESSELLATION_EVALUATION_SHADER_BIT;
            }

            // Get the supported extensions out of the requested extensions
            vector<const char*> extensions_supported = vulkan_common::extension::get_supported_device(m_rhi_context->extensions_device, m_rhi_context->device_physical);

            // Device create info
			VkDeviceCreateInfo create_info = {};
			{
				create_info.sType					= VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
				create_info.queueCreateInfoCount	= static_cast<uint32_t>(queue_create_infos.size());
				create_info.pQueueCreateInfos		= queue_create_infos.data();
				create_info.pEnabledFeatures		= &device_features_enabled;
				create_info.enabledExtensionCount	= static_cast<uint32_t>(extensions_supported.size());
				create_info.ppEnabledExtensionNames = extensions_supported.data();

				if (m_rhi_context->debug)
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
			if (!vulkan_common::error::check(vkCreateDevice(m_rhi_context->device_physical, &create_info, nullptr, &m_rhi_context->device)))
				return;

            // Create queues
            vkGetDeviceQueue(m_rhi_context->device, m_rhi_context->queue_graphics_family_index, 0, &m_rhi_context->queue_graphics);
            vkGetDeviceQueue(m_rhi_context->device, m_rhi_context->queue_compute_family_index,  0, &m_rhi_context->queue_compute);
            vkGetDeviceQueue(m_rhi_context->device, m_rhi_context->queue_transfer_family_index, 0, &m_rhi_context->queue_transfer);
		}

		// Detect and log version
		auto version_major	= to_string(VK_VERSION_MAJOR(app_info.apiVersion));
		auto version_minor	= to_string(VK_VERSION_MINOR(app_info.apiVersion));
		auto version_path	= to_string(VK_VERSION_PATCH(app_info.apiVersion));
        auto& settings      = m_context->GetSubsystem<Settings>();
        string version = version_major + "." + version_minor + "." + version_path;
        settings->RegisterThirdPartyLib("Vulkan", version_major + "." + version_minor + "." + version_path, "https://vulkan.lunarg.com/");
		LOG_INFO("Vulkan %s", version.c_str());

		m_initialized = true;
	}

	RHI_Device::~RHI_Device()
	{
        if (!m_rhi_context || !m_rhi_context->queue_graphics)
            return;

        // Release resources
		if (vulkan_common::error::check(vkQueueWaitIdle(m_rhi_context->queue_graphics)))
		{
            if (m_rhi_context->debug)
            {
                vulkan_common::debug::shutdown(m_rhi_context->instance);
            }
			vkDestroyDevice(m_rhi_context->device, nullptr);
			vkDestroyInstance(m_rhi_context->instance, nullptr);
		}
	}
}
#endif
