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

//= INCLUDES =====================
#include "Spartan.h"
#include "../RHI_Implementation.h"
#include "../RHI_Semaphore.h"
#include "../RHI_Fence.h"
//================================

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

        // Pass pointer to the widely used utility namespace
        vulkan_utility::globals::rhi_device     = this;
        vulkan_utility::globals::rhi_context    = m_rhi_context.get();
        
        // Create instance
        VkApplicationInfo app_info = {};
        {
            // Deduce API version to use
            {
                // Get sdk version
                uint32_t sdk_version = VK_HEADER_VERSION_COMPLETE;

                // Get driver version
                uint32_t driver_version = 0;
                {
                    // Per to LunarG, if vkEnumerateInstanceVersion is not present, we are running on Vulkan 1.0
                    // https://www.lunarg.com/wp-content/uploads/2019/02/Vulkan-1.1-Compatibility-Statement_01_19.pdf
                    auto eiv = reinterpret_cast<PFN_vkEnumerateInstanceVersion>(vkGetInstanceProcAddr(nullptr, "vkEnumerateInstanceVersion"));

                    if (eiv)
                    {
                        eiv(&driver_version);
                    }
                    else
                    {
                        driver_version = VK_API_VERSION_1_0;
                    }
                }

                // Choose the version which is supported by both the sdk and the driver
                m_rhi_context->api_version = Helper::Min(sdk_version, driver_version);

                // In case the SDK is not supported by the driver, prompt the user to update
                if (sdk_version > driver_version)
                {
                    // Detect and log version
                    string driver_version_str   = to_string(VK_VERSION_MAJOR(driver_version)) + "." + to_string(VK_VERSION_MINOR(driver_version)) + "." + to_string(VK_VERSION_PATCH(driver_version));
                    string sdk_version_str      = to_string(VK_VERSION_MAJOR(sdk_version)) + "." + to_string(VK_VERSION_MINOR(sdk_version)) + "." + to_string(VK_VERSION_PATCH(sdk_version));
                    LOG_WARNING("Falling back to Vulkan %s. Please update your graphics drivers to support Vulkan %s.", driver_version_str.c_str(), sdk_version_str.c_str());
                }
            }

            app_info.sType              = VK_STRUCTURE_TYPE_APPLICATION_INFO;
            app_info.pApplicationName   = sp_version;
            app_info.pEngineName        = sp_version;
            app_info.engineVersion      = VK_MAKE_VERSION(1, 0, 0);
            app_info.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
            app_info.apiVersion         = m_rhi_context->api_version;

            // Get the supported extensions out of the requested extensions
            vector<const char*> extensions_supported = vulkan_utility::extension::get_supported_instance(m_rhi_context->extensions_instance);

            VkInstanceCreateInfo create_info    = {};
            create_info.sType                   = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
            create_info.pApplicationInfo        = &app_info;
            create_info.enabledExtensionCount   = static_cast<uint32_t>(extensions_supported.size());
            create_info.ppEnabledExtensionNames = extensions_supported.data();
            create_info.enabledLayerCount       = 0;

            // Validation features
            VkValidationFeatureEnableEXT enabled_validation_features[]  = { VK_VALIDATION_FEATURE_ENABLE_BEST_PRACTICES_EXT };
            VkValidationFeaturesEXT validation_features                 = {};
            validation_features.sType                                   = VK_STRUCTURE_TYPE_VALIDATION_FEATURES_EXT;
            validation_features.enabledValidationFeatureCount           = 1;
            validation_features.pEnabledValidationFeatures              = enabled_validation_features;

            if (m_rhi_context->debug)
            {
                // Enable validation layer
                if (vulkan_utility::layer::is_present(m_rhi_context->validation_layers.front()))
                {
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

            if (!vulkan_utility::error::check(vkCreateInstance(&create_info, nullptr, &m_rhi_context->instance)))
                return;
        }

        // Get function pointers (from extensions)
        vulkan_utility::functions::initialize();

        // Debug
        if (m_rhi_context->debug)
        {
            vulkan_utility::debug::initialize(m_rhi_context->instance);
        }

        // Find a physical device
        if (!vulkan_utility::device::choose_physical_device(context->m_engine->GetWindowData().handle))
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
                    m_rhi_context->queue_graphics_index,
                    m_rhi_context->queue_transfer_index,
                    m_rhi_context->queue_compute_index
                };

                float queue_priority = 1.0f;
                for (const uint32_t& queue_family : unique_queue_families)
                {
                    VkDeviceQueueCreateInfo queue_create_info    = {};
                    queue_create_info.sType                        = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
                    queue_create_info.queueFamilyIndex            = queue_family;
                    queue_create_info.queueCount                = 1;
                    queue_create_info.pQueuePriorities            = &queue_priority;
                    queue_create_infos.push_back(queue_create_info);
                }
            }

            // Get device properties
            vkGetPhysicalDeviceProperties(static_cast<VkPhysicalDevice>(m_rhi_context->device_physical), &m_rhi_context->device_properties);

            // Resource limits
            m_rhi_context->rhi_max_texture_dimension_2d = m_rhi_context->device_properties.limits.maxImageDimension2D;

            // Disable profiler if timestamps are not supported
            if (m_rhi_context->profiler && !m_rhi_context->device_properties.limits.timestampComputeAndGraphics)
            {
                LOG_WARNING("Device doesn't support timestamps, disabling profiler...");
                m_rhi_context->profiler = false;
            }

            // Get device features
            VkPhysicalDeviceVulkan12Features device_features_1_2_enabled    = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES };
            VkPhysicalDeviceFeatures2 device_features_enabled               = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2, &device_features_1_2_enabled };
            {
                // A macro to make enabling features a little easier
                #define ENABLE_FEATURE(device_features, enabled_features, feature)                                  \
                if (device_features.feature)                                                                        \
                {                                                                                                   \
                    enabled_features.feature = VK_TRUE;                                                             \
                }                                                                                                   \
                else                                                                                                \
                {                                                                                                   \
                    LOG_WARNING("Requested device feature " #feature " is not supported by the physical device");   \
                    enabled_features.feature = VK_FALSE;                                                            \
                }

                // Get
                vkGetPhysicalDeviceFeatures2(m_rhi_context->device_physical, &m_rhi_context->device_features);

                // Enable
                ENABLE_FEATURE(m_rhi_context->device_features.features, device_features_enabled.features, samplerAnisotropy)
                ENABLE_FEATURE(m_rhi_context->device_features.features, device_features_enabled.features, fillModeNonSolid)
                ENABLE_FEATURE(m_rhi_context->device_features.features, device_features_enabled.features, wideLines)
                ENABLE_FEATURE(m_rhi_context->device_features.features, device_features_enabled.features, imageCubeArray)
                ENABLE_FEATURE(m_rhi_context->device_features_1_2, device_features_1_2_enabled, timelineSemaphore)
            }

            // Determine enabled graphics shader stages
            m_enabled_graphics_shader_stages = VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
            if (device_features_enabled.features.geometryShader)
            {
                m_enabled_graphics_shader_stages = VK_PIPELINE_STAGE_GEOMETRY_SHADER_BIT;
            }
            if (device_features_enabled.features.tessellationShader)
            {
                m_enabled_graphics_shader_stages = VK_PIPELINE_STAGE_TESSELLATION_CONTROL_SHADER_BIT | VK_PIPELINE_STAGE_TESSELLATION_EVALUATION_SHADER_BIT;
            }

            // Get the supported extensions out of the requested extensions
            vector<const char*> extensions_supported = vulkan_utility::extension::get_supported_device(m_rhi_context->extensions_device, m_rhi_context->device_physical);

            // Device create info
            VkDeviceCreateInfo create_info = {};
            {
                create_info.sType                    = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
                create_info.queueCreateInfoCount    = static_cast<uint32_t>(queue_create_infos.size());
                create_info.pQueueCreateInfos        = queue_create_infos.data();
                create_info.pNext                   = &device_features_enabled;
                create_info.enabledExtensionCount    = static_cast<uint32_t>(extensions_supported.size());
                create_info.ppEnabledExtensionNames = extensions_supported.data();

                if (m_rhi_context->debug)
                {
                    create_info.enabledLayerCount    = static_cast<uint32_t>(m_rhi_context->validation_layers.size());
                    create_info.ppEnabledLayerNames = m_rhi_context->validation_layers.data();
                }
                else
                {
                    create_info.enabledLayerCount = 0;
                }
            }

            // Create
            if (!vulkan_utility::error::check(vkCreateDevice(m_rhi_context->device_physical, &create_info, nullptr, &m_rhi_context->device)))
                return;

            // Get queues
            vkGetDeviceQueue(m_rhi_context->device, m_rhi_context->queue_graphics_index, 0, reinterpret_cast<VkQueue*>(&m_rhi_context->queue_graphics));
            vkGetDeviceQueue(m_rhi_context->device, m_rhi_context->queue_transfer_index, 0, reinterpret_cast<VkQueue*>(&m_rhi_context->queue_transfer));
            vkGetDeviceQueue(m_rhi_context->device, m_rhi_context->queue_compute_index,  0, reinterpret_cast<VkQueue*>(&m_rhi_context->queue_compute));
            
        }

        vulkan_utility::display::detect_display_modes();

        // Initialise the memory allocator
        m_rhi_context->initalise_allocator();

        // Detect and log version
        string version_major    = to_string(VK_VERSION_MAJOR(app_info.apiVersion));
        string version_minor    = to_string(VK_VERSION_MINOR(app_info.apiVersion));
        string version_patch    = to_string(VK_VERSION_PATCH(app_info.apiVersion));
        Settings* settings      = m_context->GetSubsystem<Settings>();
        string version          = version_major + "." + version_minor + "." + version_patch;
        settings->RegisterThirdPartyLib("Vulkan", version_major + "." + version_minor + "." + version_patch, "https://vulkan.lunarg.com/");
        LOG_INFO("Vulkan %s", version.c_str());

        m_initialized = true;
    }

    RHI_Device::~RHI_Device()
    {
        if (!m_rhi_context || !m_rhi_context->queue_graphics)
            return;

        // Release resources
        if (Queue_Wait(RHI_Queue_Graphics))
        {
            m_rhi_context->destroy_allocator();

            if (m_rhi_context->debug)
            {
                vulkan_utility::debug::shutdown(m_rhi_context->instance);
            }
            vkDestroyDevice(m_rhi_context->device, nullptr);
            vkDestroyInstance(m_rhi_context->instance, nullptr);
        }
    }

    bool RHI_Device::Queue_Present(void* swapchain_view, uint32_t* image_index, RHI_Semaphore* wait_semaphore /*= nullptr*/) const
    {
        // Validate semaphore state
        if (wait_semaphore) SP_ASSERT(wait_semaphore->GetState() == RHI_Semaphore_State::Signaled);

        // Get semaphore Vulkan resource
        void* vk_wait_semaphore = wait_semaphore ? wait_semaphore->GetResource() : nullptr;

        VkPresentInfoKHR present_info   = {};
        present_info.sType              = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
        present_info.waitSemaphoreCount = wait_semaphore ? 1 : 0;
        present_info.pWaitSemaphores    = wait_semaphore ? reinterpret_cast<VkSemaphore*>(&vk_wait_semaphore) : nullptr;
        present_info.swapchainCount     = 1;
        present_info.pSwapchains        = reinterpret_cast<VkSwapchainKHR*>(&swapchain_view);
        present_info.pImageIndices      = image_index;

        lock_guard<mutex> lock(m_queue_mutex);
        if (!vulkan_utility::error::check(vkQueuePresentKHR(static_cast<VkQueue>(m_rhi_context->queue_graphics), &present_info)))
            return false;

        // Update semaphore state
        if (wait_semaphore) wait_semaphore->SetState(RHI_Semaphore_State::Idle);

        return true;
    }

    bool RHI_Device::Queue_Submit(const RHI_Queue_Type type, void* cmd_buffer, RHI_Semaphore* wait_semaphore /*= nullptr*/, RHI_Semaphore* signal_semaphore /*= nullptr*/, RHI_Fence* signal_fence /*= nullptr*/, uint32_t wait_flags /*= 0*/) const
    {
        // Validate semaphore states
        if (wait_semaphore)     SP_ASSERT(wait_semaphore->GetState() == RHI_Semaphore_State::Signaled);
        if (signal_semaphore)   SP_ASSERT(signal_semaphore->GetState() == RHI_Semaphore_State::Idle);

        // Get semaphore Vulkan resources
        void* vk_wait_semaphore   = wait_semaphore ? wait_semaphore->GetResource() : nullptr;
        void* vk_signal_semaphore = signal_semaphore ? signal_semaphore->GetResource() : nullptr;

        VkSubmitInfo submit_info            = {};
        submit_info.sType                   = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submit_info.waitSemaphoreCount      = wait_semaphore ? 1 : 0;
        submit_info.pWaitSemaphores         = wait_semaphore ? reinterpret_cast<VkSemaphore*>(&vk_wait_semaphore) : nullptr;
        submit_info.signalSemaphoreCount    = signal_semaphore ? 1 : 0;
        submit_info.pSignalSemaphores       = signal_semaphore ? reinterpret_cast<VkSemaphore*>(&vk_signal_semaphore) : nullptr;
        submit_info.pWaitDstStageMask       = &wait_flags;
        submit_info.commandBufferCount      = 1;
        submit_info.pCommandBuffers         = reinterpret_cast<VkCommandBuffer*>(&cmd_buffer);

        // Get signal fence
        void* vk_signal_fence = signal_fence ? signal_fence->GetResource() : nullptr;

        lock_guard<mutex> lock(m_queue_mutex);
        if (!vulkan_utility::error::check(vkQueueSubmit(static_cast<VkQueue>(Queue_Get(type)), 1, &submit_info, static_cast<VkFence>(vk_signal_fence))))
            return false;

        // Update semaphore states
        if (wait_semaphore)     wait_semaphore->SetState(RHI_Semaphore_State::Idle);
        if (signal_semaphore)   signal_semaphore->SetState(RHI_Semaphore_State::Signaled);

        return true;
    }

    bool RHI_Device::Queue_Wait(const RHI_Queue_Type type) const
    {
        lock_guard<mutex> lock(m_queue_mutex);
        return vulkan_utility::error::check(vkQueueWaitIdle(static_cast<VkQueue>(Queue_Get(type))));
    }
}
