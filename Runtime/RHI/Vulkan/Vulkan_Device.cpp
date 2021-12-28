/*
Copyright(c) 2016-2021 Panos Karabelas

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
#include "../../Core/Window.h"
//================================

//= NAMESPACES ===============
using namespace std;
using namespace Spartan::Math;
//============================

namespace Spartan
{
    uint32_t RHI_Device::m_max_texture_1d_dimension   = 0;
    uint32_t RHI_Device::m_max_texture_2d_dimension   = 0;
    uint32_t RHI_Device::m_max_texture_3d_dimension   = 0;
    uint32_t RHI_Device::m_max_texture_cube_dimension = 0;
    uint32_t RHI_Device::m_max_texture_array_layers   = 0;

    RHI_Device::RHI_Device(Context* context)
    {
        m_context       = context;
        m_rhi_context   = make_shared<RHI_Context>();

        // Pass pointer to the widely used utility namespace
        vulkan_utility::globals::rhi_device  = this;
        vulkan_utility::globals::rhi_context = m_rhi_context.get();
        
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
                    // Per LunarG, if vkEnumerateInstanceVersion is not present, we are running on Vulkan 1.0
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
            VkValidationFeaturesEXT validation_features         = {};
            validation_features.sType                           = VK_STRUCTURE_TYPE_VALIDATION_FEATURES_EXT;
            validation_features.enabledValidationFeatureCount   = static_cast<uint32_t>(m_rhi_context->validation_extensions.size());
            validation_features.pEnabledValidationFeatures      = m_rhi_context->validation_extensions.data();

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
        {
            if (!DetectPhysicalDevices())
            {
                LOG_ERROR("Failed to detect any devices");
                return;
            }

            if (!SelectPrimaryPhysicalDevice())
            {
                LOG_ERROR("Failed to detect any devices");
                return;
            }
        }

        // Device
        {
            // Queue create info
            vector<VkDeviceQueueCreateInfo> queue_create_infos;
            {
                vector<uint32_t> unique_queue_families =
                {
                    m_queue_graphics_index,
                    m_queue_compute_index,
                    m_queue_copy_index
                };

                float queue_priority = 1.0f;
                for (const uint32_t& queue_family : unique_queue_families)
                {
                    VkDeviceQueueCreateInfo queue_create_info = {};
                    queue_create_info.sType                   = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
                    queue_create_info.queueFamilyIndex        = queue_family;
                    queue_create_info.queueCount              = 1;
                    queue_create_info.pQueuePriorities        = &queue_priority;
                    queue_create_infos.push_back(queue_create_info);
                }
            }

            // Get device properties
            vkGetPhysicalDeviceProperties(static_cast<VkPhysicalDevice>(m_rhi_context->device_physical), &m_rhi_context->device_properties);

            // Detect device limits
            m_max_texture_1d_dimension   = m_rhi_context->device_properties.limits.maxImageDimension1D;
            m_max_texture_2d_dimension   = m_rhi_context->device_properties.limits.maxImageDimension2D;
            m_max_texture_3d_dimension   = m_rhi_context->device_properties.limits.maxImageDimension3D;
            m_max_texture_cube_dimension = m_rhi_context->device_properties.limits.maxImageDimensionCube;
            m_max_texture_array_layers   = m_rhi_context->device_properties.limits.maxImageArrayLayers;

            // Disable profiler if timestamps are not supported
            if (m_rhi_context->profiler && !m_rhi_context->device_properties.limits.timestampComputeAndGraphics)
            {
                LOG_WARNING("Device doesn't support timestamps, disabling profiler...");
                m_rhi_context->profiler = false;
            }

            // Get device features
            VkPhysicalDeviceVulkan12Features device_features_1_2_enabled = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES };
            VkPhysicalDeviceFeatures2 device_features_enabled            = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2, &device_features_1_2_enabled };
            {
                // A macro to make enabling features a little easier
                #define ENABLE_FEATURE(device_features, enabled_features, feature)                                \
                if (device_features.feature)                                                                      \
                {                                                                                                 \
                    enabled_features.feature = VK_TRUE;                                                           \
                }                                                                                                 \
                else                                                                                              \
                {                                                                                                 \
                    LOG_WARNING("Requested device feature " #feature " is not supported by the physical device"); \
                    enabled_features.feature = VK_FALSE;                                                          \
                }

                // Get
                vkGetPhysicalDeviceFeatures2(m_rhi_context->device_physical, &m_rhi_context->device_features);

                // Enable
                ENABLE_FEATURE(m_rhi_context->device_features.features, device_features_enabled.features, samplerAnisotropy)
                ENABLE_FEATURE(m_rhi_context->device_features.features, device_features_enabled.features, fillModeNonSolid)
                ENABLE_FEATURE(m_rhi_context->device_features.features, device_features_enabled.features, wideLines)
                ENABLE_FEATURE(m_rhi_context->device_features.features, device_features_enabled.features, imageCubeArray)
                ENABLE_FEATURE(m_rhi_context->device_features_1_2,      device_features_1_2_enabled,      timelineSemaphore)
            }

            // Determine enabled graphics shader stages
            m_enabled_graphics_shader_stages = VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
            if (device_features_enabled.features.geometryShader)
            {
                m_enabled_graphics_shader_stages |= VK_PIPELINE_STAGE_GEOMETRY_SHADER_BIT;
            }
            if (device_features_enabled.features.tessellationShader)
            {
                m_enabled_graphics_shader_stages |= VK_PIPELINE_STAGE_TESSELLATION_CONTROL_SHADER_BIT | VK_PIPELINE_STAGE_TESSELLATION_EVALUATION_SHADER_BIT;
            }

            // Enable partially bound descriptors
            VkPhysicalDeviceDescriptorIndexingFeatures descriptor_indexing_features = {};
            {
                descriptor_indexing_features.sType                           = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_INDEXING_FEATURES;
                descriptor_indexing_features.descriptorBindingPartiallyBound = true;
                device_features_enabled.pNext = &descriptor_indexing_features;
            }

            // Get the supported extensions out of the requested extensions
            vector<const char*> extensions_supported = vulkan_utility::extension::get_supported_device(m_rhi_context->extensions_device, m_rhi_context->device_physical);

            // Device create info
            VkDeviceCreateInfo create_info = {};
            {
                create_info.sType                   = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
                create_info.queueCreateInfoCount    = static_cast<uint32_t>(queue_create_infos.size());
                create_info.pQueueCreateInfos       = queue_create_infos.data();
                create_info.pNext                   = &device_features_enabled;
                create_info.enabledExtensionCount   = static_cast<uint32_t>(extensions_supported.size());
                create_info.ppEnabledExtensionNames = extensions_supported.data();

                if (m_rhi_context->debug)
                {
                    create_info.enabledLayerCount   = static_cast<uint32_t>(m_rhi_context->validation_layers.size());
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
        }

        // Get a graphics, compute and a copy queue.
        {
            vkGetDeviceQueue(m_rhi_context->device, m_queue_graphics_index, 0, reinterpret_cast<VkQueue*>(&m_queue_graphics));
            vkGetDeviceQueue(m_rhi_context->device, m_queue_compute_index,  0, reinterpret_cast<VkQueue*>(&m_queue_compute));
            vkGetDeviceQueue(m_rhi_context->device, m_queue_copy_index,     0, reinterpret_cast<VkQueue*>(&m_queue_copy));
        }

        // Create command pool
        {
            VkCommandPoolCreateInfo cmd_pool_info = {};
            cmd_pool_info.sType                   = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
            cmd_pool_info.queueFamilyIndex        = GetQueueIndex(RHI_Queue_Type::Graphics);
            cmd_pool_info.flags                   = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;

            if(!vulkan_utility::error::check(vkCreateCommandPool(m_rhi_context->device, &cmd_pool_info, nullptr, reinterpret_cast<VkCommandPool*>(&m_cmd_pool_graphics))))
                return;
        }

        // Initialise the memory allocator
        m_rhi_context->InitialiseAllocator();

        // Detect and log version
        {
            string version_major = to_string(VK_VERSION_MAJOR(app_info.apiVersion));
            string version_minor = to_string(VK_VERSION_MINOR(app_info.apiVersion));
            string version_patch = to_string(VK_VERSION_PATCH(app_info.apiVersion));
            string version       = version_major + "." + version_minor + "." + version_patch;

            LOG_INFO("Vulkan %s", version.c_str());

            if (Settings* settings = m_context->GetSubsystem<Settings>())
            {
                settings->RegisterThirdPartyLib("Vulkan", version_major + "." + version_minor + "." + version_patch, "https://vulkan.lunarg.com/");
            }
        }

        m_initialized = true;
    }

    RHI_Device::~RHI_Device()
    {
        SP_ASSERT(m_rhi_context != nullptr);
        SP_ASSERT(m_queue_graphics != nullptr);

        // Command pool
        vkDestroyCommandPool(m_rhi_context->device, static_cast<VkCommandPool>(m_cmd_pool_graphics), nullptr);
        m_cmd_pool_graphics = nullptr;

        // Release resources
        if (QueueWaitAll())
        {
            m_rhi_context->DestroyAllocator();

            if (m_rhi_context->debug)
            {
                vulkan_utility::debug::shutdown(m_rhi_context->instance);
            }
            vkDestroyDevice(m_rhi_context->device, nullptr);
            vkDestroyInstance(m_rhi_context->instance, nullptr);
        }
    }

    bool RHI_Device::DetectPhysicalDevices()
    {
        uint32_t device_count = 0;
        if (!vulkan_utility::error::check(vkEnumeratePhysicalDevices(m_rhi_context->instance, &device_count, nullptr)))
            return false;
        
        if (device_count == 0)
        {
            LOG_ERROR("There are no available physical devices.");
            return false;
        }
        
        vector<VkPhysicalDevice> physical_devices(device_count);
        if (!vulkan_utility::error::check(vkEnumeratePhysicalDevices(m_rhi_context->instance, &device_count, physical_devices.data())))
            return false;
        
        // Go through all the devices
        for (const VkPhysicalDevice& device_physical : physical_devices)
        {
            // Get device properties
            VkPhysicalDeviceProperties device_properties = {};
            vkGetPhysicalDeviceProperties(device_physical, &device_properties);
        
            VkPhysicalDeviceMemoryProperties device_memory_properties = {};
            vkGetPhysicalDeviceMemoryProperties(device_physical, &device_memory_properties);
        
            RHI_PhysicalDevice_Type type = RHI_PhysicalDevice_Type::Unknown;
            if (device_properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU) type = RHI_PhysicalDevice_Type::Integrated;
            if (device_properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU)   type = RHI_PhysicalDevice_Type::Discrete;
            if (device_properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU)    type = RHI_PhysicalDevice_Type::Virtual;
            if (device_properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_CPU)            type = RHI_PhysicalDevice_Type::Cpu;
        
            // Let the engine know about it as it will sort all of the devices from best to worst
            RegisterPhysicalDevice(PhysicalDevice
            (
                device_properties.apiVersion,                                        // api version
                device_properties.driverVersion,                                     // driver version
                device_properties.vendorID,                                          // vendor id
                type,                                                                // type
                &device_properties.deviceName[0],                                    // name
                static_cast<uint64_t>(device_memory_properties.memoryHeaps[0].size), // memory
                static_cast<void*>(device_physical)                                  // data
            ));
        }

        return true;
    }

    bool RHI_Device::SelectPrimaryPhysicalDevice()
    {
        auto get_queue_family_index = [](VkQueueFlagBits queue_flags, const vector<VkQueueFamilyProperties>& queue_family_properties, uint32_t* index)
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
        };

        auto get_queue_family_indices = [this, &get_queue_family_index](const VkPhysicalDevice& physical_device)
        {
            uint32_t queue_family_count = 0;
            vkGetPhysicalDeviceQueueFamilyProperties(physical_device, &queue_family_count, nullptr);

            vector<VkQueueFamilyProperties> queue_families_properties(queue_family_count);
            vkGetPhysicalDeviceQueueFamilyProperties(physical_device, &queue_family_count, queue_families_properties.data());

            // Graphics
            uint32_t index = 0;
            if (get_queue_family_index(VK_QUEUE_GRAPHICS_BIT, queue_families_properties, &index))
            {
                SetQueueIndex(RHI_Queue_Type::Graphics, index);
            }
            else
            {
                LOG_ERROR("Graphics queue not suported.");
                return false;
            }

            // Compute
            if (get_queue_family_index(VK_QUEUE_COMPUTE_BIT, queue_families_properties, &index))
            {
                SetQueueIndex(RHI_Queue_Type::Compute, index);
            }
            else
            {
                LOG_ERROR("Compute queue not suported.");
                return false;
            }

            // Copy
            if (get_queue_family_index(VK_QUEUE_TRANSFER_BIT, queue_families_properties, &index))
            {
                SetQueueIndex(RHI_Queue_Type::Copy, index);
            }
            else
            {
                LOG_ERROR("Copy queue not suported.");
                return false;
            }

            return true;
        };

        // Go through all the devices (sorted from best to worst based on their properties)
        for (uint32_t device_index = 0; device_index < m_physical_devices.size(); device_index++)
        {
            VkPhysicalDevice device = static_cast<VkPhysicalDevice>(m_physical_devices[device_index].GetData());

            // Get the first device that has a graphics, a compute and a transfer queue
            if (get_queue_family_indices(device))
            {
                SetPrimaryPhysicalDevice(device_index);
                m_rhi_context->device_physical = device;
                break;
            }
        }

        return DetectDisplayModes(GetPrimaryPhysicalDevice(), RHI_Format_R8G8B8A8_Unorm); // TODO: Format should be determined based on what the swap chain supports.
    }

    bool RHI_Device::DetectDisplayModes(const PhysicalDevice* physical_device, const RHI_Format format)
    {
        // VK_KHR_Display is not supported and I don't want to use anything OS specific to acquire the display modes, must think of something.

        const bool update_fps_limit_to_highest_hz = true;
        Display::RegisterDisplayMode(DisplayMode(640, 480, 165, 1),   update_fps_limit_to_highest_hz, m_context);
        Display::RegisterDisplayMode(DisplayMode(720, 576, 165, 1),   update_fps_limit_to_highest_hz, m_context);
        Display::RegisterDisplayMode(DisplayMode(1280, 720, 165, 1),  update_fps_limit_to_highest_hz, m_context);
        Display::RegisterDisplayMode(DisplayMode(1920, 1080, 165, 1), update_fps_limit_to_highest_hz, m_context);
        Display::RegisterDisplayMode(DisplayMode(2560, 1440, 165, 1), update_fps_limit_to_highest_hz, m_context);

        return true;
    }

    bool RHI_Device::QueuePresent(void* swapchain_view, uint32_t* image_index, RHI_Semaphore* wait_semaphore /*= nullptr*/) const
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
        if (!vulkan_utility::error::check(vkQueuePresentKHR(static_cast<VkQueue>(m_queue_graphics), &present_info)))
            return false;

        // Update semaphore state
        if (wait_semaphore)
            wait_semaphore->SetState(RHI_Semaphore_State::Idle);

        return true;
    }

    bool RHI_Device::QueueSubmit(const RHI_Queue_Type type, const uint32_t wait_flags, void* cmd_buffer, RHI_Semaphore* wait_semaphore /*= nullptr*/, RHI_Semaphore* signal_semaphore /*= nullptr*/, RHI_Fence* signal_fence /*= nullptr*/) const
    {
        // Validate input
        SP_ASSERT(cmd_buffer != nullptr);

        // Validate semaphore states
        if (wait_semaphore)   SP_ASSERT(wait_semaphore->GetState() == RHI_Semaphore_State::Signaled);
        if (signal_semaphore) SP_ASSERT(signal_semaphore->GetState() == RHI_Semaphore_State::Idle);

        // Get semaphore Vulkan resources
        void* vk_wait_semaphore   = wait_semaphore   ? wait_semaphore->GetResource()   : nullptr;
        void* vk_signal_semaphore = signal_semaphore ? signal_semaphore->GetResource() : nullptr;

        // Submit info
        VkSubmitInfo submit_info         = {};
        submit_info.sType                = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submit_info.pNext                = nullptr;
        submit_info.waitSemaphoreCount   = wait_semaphore ? 1 : 0;
        submit_info.pWaitSemaphores      = wait_semaphore ? reinterpret_cast<VkSemaphore*>(&vk_wait_semaphore) : nullptr;
        submit_info.signalSemaphoreCount = signal_semaphore ? 1 : 0;
        submit_info.pSignalSemaphores    = signal_semaphore ? reinterpret_cast<VkSemaphore*>(&vk_signal_semaphore) : nullptr;
        submit_info.pWaitDstStageMask    = &wait_flags;
        submit_info.commandBufferCount   = 1;
        submit_info.pCommandBuffers      = reinterpret_cast<VkCommandBuffer*>(&cmd_buffer);

        // Get signal fence
        void* vk_signal_fence = signal_fence ? signal_fence->GetResource() : nullptr;

        lock_guard<mutex> lock(m_queue_mutex);
        if (!vulkan_utility::error::check(vkQueueSubmit(static_cast<VkQueue>(GetQueue(type)), 1, &submit_info, static_cast<VkFence>(vk_signal_fence))))
            return false;

        // Update semaphore states
        if (wait_semaphore)   wait_semaphore->SetState(RHI_Semaphore_State::Idle);
        if (signal_semaphore) signal_semaphore->SetState(RHI_Semaphore_State::Signaled);

        return true;
    }

    bool RHI_Device::QueueWait(const RHI_Queue_Type type) const
    {
        lock_guard<mutex> lock(m_queue_mutex);
        return vulkan_utility::error::check(vkQueueWaitIdle(static_cast<VkQueue>(GetQueue(type))));
    }
}
