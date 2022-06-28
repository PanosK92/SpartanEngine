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

//= INCLUDES ========================
#include "Spartan.h"
#include "../RHI_Implementation.h"
#include "../RHI_Semaphore.h"
#include "../RHI_Fence.h"
#include "../../Core/Window.h"
#include "../../Profiling/Profiler.h"
//===================================

//= NAMESPACES ===============
using namespace std;
using namespace Spartan::Math;
//============================

namespace Spartan
{
    static bool is_present_device_extension(const char* extension_name, VkPhysicalDevice device_physical)
    {
        uint32_t extension_count = 0;
        vkEnumerateDeviceExtensionProperties(device_physical, nullptr, &extension_count, nullptr);

        vector<VkExtensionProperties> extensions(extension_count);
        vkEnumerateDeviceExtensionProperties(device_physical, nullptr, &extension_count, extensions.data());

        for (const auto& extension : extensions)
        {
            if (strcmp(extension_name, extension.extensionName) == 0)
                return true;
        }

        return false;
    }

    static bool is_present_instance(const char* extension_name)
    {
        uint32_t extension_count = 0;
        vkEnumerateInstanceExtensionProperties(nullptr, &extension_count, nullptr);

        vector<VkExtensionProperties> extensions(extension_count);
        vkEnumerateInstanceExtensionProperties(nullptr, &extension_count, extensions.data());

        for (const auto& extension : extensions)
        {
            if (strcmp(extension_name, extension.extensionName) == 0)
                return true;
        }

        return false;
    }

    static vector<const char*> get_extension_supporting_physical_device(const vector<const char*>& extensions, VkPhysicalDevice device_physical)
    {
        vector<const char*> extensions_supported;

        for (const auto& extension : extensions)
        {
            if (is_present_device_extension(extension, device_physical))
            {
                extensions_supported.emplace_back(extension);
            }
            else
            {
                LOG_ERROR("Device extension \"%s\" is not supported", extension);
            }
        }

        return extensions_supported;
    }

    static vector<const char*> get_supported_extensions(const vector<const char*>& extensions)
    {
        vector<const char*> extensions_supported;

        for (const auto& extension : extensions)
        {
            if (is_present_instance(extension))
            {
                extensions_supported.emplace_back(extension);
            }
            else
            {
                LOG_ERROR("Instance extension \"%s\" is not supported", extension);
            }
        }

        return extensions_supported;
    }

    RHI_Device::RHI_Device(Context* context)
    {
        m_context     = context;
        m_rhi_context = make_shared<RHI_Context>();

        // Pass pointer to the widely used utility namespace
        vulkan_utility::globals::rhi_device  = this;
        vulkan_utility::globals::rhi_context = m_rhi_context.get();
        
        // Create instance
        VkApplicationInfo app_info  = {};
        {
            app_info.sType              = VK_STRUCTURE_TYPE_APPLICATION_INFO;
            app_info.pApplicationName   = sp_version;
            app_info.pEngineName        = sp_version;
            app_info.engineVersion      = VK_MAKE_VERSION(1, 0, 0);
            app_info.applicationVersion = VK_MAKE_VERSION(1, 0, 0);

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
                app_info.apiVersion = Helper::Min(sdk_version, driver_version);

                // In case the SDK is not supported by the driver, prompt the user to update
                if (sdk_version > driver_version)
                {
                    // Detect and log version
                    string driver_version_str = to_string(VK_API_VERSION_MAJOR(driver_version)) + "." + to_string(VK_API_VERSION_MINOR(driver_version)) + "." + to_string(VK_API_VERSION_PATCH(driver_version));
                    string sdk_version_str    = to_string(VK_API_VERSION_MAJOR(sdk_version)) + "." + to_string(VK_API_VERSION_MINOR(sdk_version)) + "." + to_string(VK_API_VERSION_PATCH(sdk_version));
                    LOG_WARNING("Falling back to Vulkan %s. Please update your graphics drivers to support Vulkan %s.", driver_version_str.c_str(), sdk_version_str.c_str());
                }

                //  Save API version
                m_rhi_context->api_version_str = to_string(VK_API_VERSION_MAJOR(app_info.apiVersion)) + "." + to_string(VK_API_VERSION_MINOR(app_info.apiVersion)) + "." + to_string(VK_API_VERSION_PATCH(app_info.apiVersion));
            }

            // Get the supported extensions out of the requested extensions
            vector<const char*> extensions_supported = get_supported_extensions(m_rhi_context->extensions_instance);

            VkInstanceCreateInfo create_info    = {};
            create_info.sType                   = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
            create_info.pApplicationInfo        = &app_info;
            create_info.enabledExtensionCount   = static_cast<uint32_t>(extensions_supported.size());
            create_info.ppEnabledExtensionNames = extensions_supported.data();
            create_info.enabledLayerCount       = 0;

            // Validation features
            VkValidationFeaturesEXT validation_features       = {};
            validation_features.sType                         = VK_STRUCTURE_TYPE_VALIDATION_FEATURES_EXT;
            validation_features.enabledValidationFeatureCount = static_cast<uint32_t>(m_rhi_context->validation_extensions.size());
            validation_features.pEnabledValidationFeatures    = m_rhi_context->validation_extensions.data();

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

            SP_ASSERT(vulkan_utility::error::check(vkCreateInstance(&create_info, nullptr, &m_rhi_context->instance)) && "Failed to create instance");
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
            SP_ASSERT_MSG(DetectPhysicalDevices(), "Failed to detect any devices");
            SP_ASSERT_MSG(SelectPrimaryPhysicalDevice(), "Failed to find a suitable device");
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
            VkPhysicalDeviceProperties device_properties = {};
            vkGetPhysicalDeviceProperties(static_cast<VkPhysicalDevice>(m_rhi_context->device_physical), &device_properties);

            // Save some properties
            m_max_texture_1d_dimension            = device_properties.limits.maxImageDimension1D;
            m_max_texture_2d_dimension            = device_properties.limits.maxImageDimension2D;
            m_max_texture_3d_dimension            = device_properties.limits.maxImageDimension3D;
            m_max_texture_cube_dimension          = device_properties.limits.maxImageDimensionCube;
            m_max_texture_array_layers            = device_properties.limits.maxImageArrayLayers;
            m_min_uniform_buffer_offset_alignment = device_properties.limits.minUniformBufferOffsetAlignment;
            m_timestamp_period                    = device_properties.limits.timestampPeriod;
            m_max_bound_descriptor_sets           = device_properties.limits.maxBoundDescriptorSets;

            // Disable profiler if timestamps are not supported
            if (m_rhi_context->gpu_profiling && !device_properties.limits.timestampComputeAndGraphics)
            {
                LOG_ERROR("Device doesn't support timestamps, disabling profiling...");
                m_rhi_context->gpu_profiling = false;
            }

            // Feature: Vulkan 1.3 features
            VkPhysicalDeviceVulkan13Features device_features_1_3 = {};
            device_features_1_3.sType                            = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;

            // Feature: Vulkan 1.2 features
            VkPhysicalDeviceVulkan12Features device_features_1_2 = {};
            device_features_1_2.sType                            = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
            device_features_1_2.pNext                            = &device_features_1_3;

            // Feature: Physical device features
            VkPhysicalDeviceFeatures2 device_features = {};
            device_features.sType                     = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
            device_features.pNext                     = &device_features_1_2;

            // Feature: Dynamic rendering
            VkPhysicalDeviceDynamicRenderingFeaturesKHR dynamic_rendering_features = {};
            dynamic_rendering_features.sType                                       = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_FEATURES_KHR;
            dynamic_rendering_features.dynamicRendering                            = VK_TRUE;
            dynamic_rendering_features.pNext                                       = &device_features;

            // Feature: Partially bound descriptors
            VkPhysicalDeviceDescriptorIndexingFeatures descriptor_indexing_features = {};
            descriptor_indexing_features.sType                                      = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_INDEXING_FEATURES;
            descriptor_indexing_features.descriptorBindingPartiallyBound            = VK_TRUE;
            descriptor_indexing_features.pNext                                      = &dynamic_rendering_features;

            // Enable certain features
            {
                // Check what's supported
                VkPhysicalDeviceFeatures2 device_features_supported            = {};
                VkPhysicalDeviceVulkan12Features device_features_1_2_supported = {};
                VkPhysicalDeviceVulkan13Features device_features_1_3_supported = {};
                {
                    device_features_1_3_supported.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;

                    device_features_1_2_supported.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
                    device_features_1_2_supported.pNext = &device_features_1_3_supported;

                    device_features_supported.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
                    device_features_supported.pNext = &device_features_1_2_supported;

                    vkGetPhysicalDeviceFeatures2(m_rhi_context->device_physical, &device_features_supported);
                }

                // Anisotropic filtering
                SP_ASSERT(device_features_supported.features.samplerAnisotropy == VK_TRUE);
                device_features.features.samplerAnisotropy = VK_TRUE;

                // Line and point rendering
                SP_ASSERT(device_features_supported.features.fillModeNonSolid == VK_TRUE);
                device_features.features.fillModeNonSolid = VK_TRUE;

                // Lines with adjustable thickness
                SP_ASSERT(device_features_supported.features.wideLines == VK_TRUE);
                device_features.features.wideLines = VK_TRUE;

                // Cubemaps
                SP_ASSERT(device_features_supported.features.imageCubeArray == VK_TRUE);
                device_features.features.imageCubeArray = VK_TRUE;

                // Partially bound descriptors
                SP_ASSERT(device_features_1_2_supported.descriptorBindingPartiallyBound == VK_TRUE);
                device_features_1_2.descriptorBindingPartiallyBound = VK_TRUE;

                // Timeline semaphores
                SP_ASSERT(device_features_1_2_supported.timelineSemaphore == VK_TRUE);
                device_features_1_2.timelineSemaphore = VK_TRUE;

                // Synchronization 2
                SP_ASSERT(device_features_1_3_supported.synchronization2 == VK_TRUE);
                device_features_1_3.synchronization2 = VK_TRUE;

                // Rendering without render passes and frame buffer objects
                SP_ASSERT(device_features_1_3_supported.dynamicRendering == VK_TRUE);
                device_features_1_3.dynamicRendering = VK_TRUE;
            }

            // Enable certain graphics shader stages
            {
                m_enabled_graphics_shader_stages = VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
                if (device_features.features.geometryShader)
                {
                    m_enabled_graphics_shader_stages |= VK_PIPELINE_STAGE_GEOMETRY_SHADER_BIT;
                }
                if (device_features.features.tessellationShader)
                {
                    m_enabled_graphics_shader_stages |= VK_PIPELINE_STAGE_TESSELLATION_CONTROL_SHADER_BIT | VK_PIPELINE_STAGE_TESSELLATION_EVALUATION_SHADER_BIT;
                }
            }

            // Get the supported extensions out of the requested extensions
            vector<const char*> extensions_supported = get_extension_supporting_physical_device(m_rhi_context->extensions_device, m_rhi_context->device_physical);

            // Device create info
            VkDeviceCreateInfo create_info = {};
            {
                create_info.sType                   = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
                create_info.queueCreateInfoCount    = static_cast<uint32_t>(queue_create_infos.size());
                create_info.pQueueCreateInfos       = queue_create_infos.data();
                create_info.pNext                   = &device_features;
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
            SP_ASSERT_MSG(vulkan_utility::error::check(vkCreateDevice(m_rhi_context->device_physical, &create_info, nullptr, &m_rhi_context->device)), "Failed to create device");
        }

        // Get a graphics, compute and a copy queue.
        {
            vkGetDeviceQueue(m_rhi_context->device, m_queue_graphics_index, 0, reinterpret_cast<VkQueue*>(&m_queue_graphics));
            vkGetDeviceQueue(m_rhi_context->device, m_queue_compute_index,  0, reinterpret_cast<VkQueue*>(&m_queue_compute));
            vkGetDeviceQueue(m_rhi_context->device, m_queue_copy_index,     0, reinterpret_cast<VkQueue*>(&m_queue_copy));
        }

        // Create memory allocator
        {
            VmaAllocatorCreateInfo allocator_info = {};
            allocator_info.physicalDevice         = m_rhi_context->device_physical;
            allocator_info.device                 = m_rhi_context->device;
            allocator_info.instance               = m_rhi_context->instance;
            allocator_info.vulkanApiVersion       = app_info.apiVersion;

            SP_ASSERT_MSG(vulkan_utility::error::check(vmaCreateAllocator(&allocator_info, &m_rhi_context->allocator)), "Failed to create memory allocator");
        }

        // Set the descriptor set capacity to an initial value
        SetDescriptorSetCapacity(2048);

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
    }

    RHI_Device::~RHI_Device()
    {
        SP_ASSERT(m_rhi_context != nullptr);
        SP_ASSERT(m_queue_graphics != nullptr);

        // Wait for all queues
        if (QueueWaitAll())
        {
            m_cmd_pools.clear();

            // Descriptor pool
            vkDestroyDescriptorPool(m_rhi_context->device, static_cast<VkDescriptorPool>(m_descriptor_pool), nullptr);
            m_descriptor_pool = nullptr;

            // Allocator
            if (m_rhi_context->allocator != nullptr)
            {
                vmaDestroyAllocator(m_rhi_context->allocator);
                m_rhi_context->allocator = nullptr;
            }

            // Debug messenger
            if (m_rhi_context->debug)
            {
                vulkan_utility::debug::shutdown(m_rhi_context->instance);
            }

            // Device and instance
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

    void RHI_Device::QueuePresent(void* swapchain, uint32_t* image_index, vector<RHI_Semaphore*>& wait_semaphores) const
    {
        static array<VkSemaphore, 3> vk_wait_semaphores = {};

        // Get semaphore Vulkan resource
        uint32_t semaphore_count = static_cast<uint32_t>(wait_semaphores.size());
        for (uint32_t i = 0; i < semaphore_count; i++)
        {
            SP_ASSERT_MSG(wait_semaphores[i]->GetState() == RHI_Semaphore_State::Signaled, "The wait semaphore hasn't been signaled");
            vk_wait_semaphores[i] = static_cast<VkSemaphore>(wait_semaphores[i]->GetResource());
        }

        VkPresentInfoKHR present_info   = {};
        present_info.sType              = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
        present_info.waitSemaphoreCount = semaphore_count;
        present_info.pWaitSemaphores    = vk_wait_semaphores.data();
        present_info.swapchainCount     = 1;
        present_info.pSwapchains        = reinterpret_cast<VkSwapchainKHR*>(&swapchain);
        present_info.pImageIndices      = image_index;

        SP_ASSERT_MSG(vulkan_utility::error::check(vkQueuePresentKHR(static_cast<VkQueue>(m_queue_graphics), &present_info)), "Failed to present");

        // Update semaphore state
        for (uint32_t i = 0; i < semaphore_count; i++)
        {
            wait_semaphores[i]->SetState(RHI_Semaphore_State::Idle);
        }
    }

    bool RHI_Device::QueueSubmit(const RHI_Queue_Type type, const uint32_t wait_flags, void* cmd_buffer, RHI_Semaphore* wait_semaphore /*= nullptr*/, RHI_Semaphore* signal_semaphore /*= nullptr*/, RHI_Fence* signal_fence /*= nullptr*/) const
    {
        SP_ASSERT_MSG(cmd_buffer != nullptr, "Invalid command buffer");

        // Validate semaphore states
        if (wait_semaphore)   SP_ASSERT_MSG(wait_semaphore->GetState()   != RHI_Semaphore_State::Idle,     "Wait semaphore is in an idle state and will never be signaled");
        if (signal_semaphore) SP_ASSERT_MSG(signal_semaphore->GetState() != RHI_Semaphore_State::Signaled, "Signal semaphore is already in a signaled state.");

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

    void RHI_Device::QueryCreate(void** query, const RHI_Query_Type type)
    {

    }

    void RHI_Device::QueryRelease(void*& query)
    {

    }

    void RHI_Device::QueryBegin(void* query)
    {

    }

    void RHI_Device::QueryEnd(void* query)
    {

    }

    void RHI_Device::QueryGetData(void* query)
    {

    }

    void RHI_Device::SetDescriptorSetCapacity(uint32_t descriptor_set_capacity)
    {
        // If the requested capacity is zero, then only recreate the descriptor pool
        if (descriptor_set_capacity == 0)
        {
            descriptor_set_capacity = m_descriptor_set_capacity;
        }

        if (m_descriptor_set_capacity == descriptor_set_capacity)
        {
            LOG_WARNING("Capacity is already %d, is this reset needed ?");
        }

        // Create pool
        {
            // Pool sizes
            array<VkDescriptorPoolSize, 5> pool_sizes =
            {
                VkDescriptorPoolSize{ VK_DESCRIPTOR_TYPE_SAMPLER,                rhi_descriptor_max_samplers },
                VkDescriptorPoolSize{ VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,          rhi_descriptor_max_textures },
                VkDescriptorPoolSize{ VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,          rhi_descriptor_max_storage_textures },
                VkDescriptorPoolSize{ VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,         rhi_descriptor_max_storage_buffers },
                VkDescriptorPoolSize{ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, rhi_descriptor_max_constant_buffers_dynamic }
            };

            // Create info
            VkDescriptorPoolCreateInfo pool_create_info = {};
            pool_create_info.sType                      = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
            pool_create_info.flags                      = 0;
            pool_create_info.poolSizeCount              = static_cast<uint32_t>(pool_sizes.size());
            pool_create_info.pPoolSizes                 = pool_sizes.data();
            pool_create_info.maxSets                    = descriptor_set_capacity;

            // Create
            bool created = vulkan_utility::error::check(vkCreateDescriptorPool(m_rhi_context->device, &pool_create_info, nullptr, reinterpret_cast<VkDescriptorPool*>(&m_descriptor_pool)));
            SP_ASSERT_MSG(created, "Failed to create descriptor pool.");
        }

        LOG_INFO("Capacity has been set to %d elements", descriptor_set_capacity);
        m_descriptor_set_capacity = descriptor_set_capacity;

        if (Profiler* profiler = m_context->GetSubsystem<Profiler>())
        {
            profiler->m_descriptor_set_count    = 0;
            profiler->m_descriptor_set_capacity = m_descriptor_set_capacity;
        }
    }
}
