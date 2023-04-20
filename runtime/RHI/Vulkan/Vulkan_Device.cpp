/*
Copyright(c) 2016-2023 Panos Karabelas

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
#include "pch.h"
#include "../RHI_Implementation.h"
#include "../RHI_Semaphore.h"
#include "../RHI_Fence.h"
#include "../../Core/Window.h"
#include "../../Profiling/Profiler.h"
SP_WARNINGS_OFF
#define VMA_IMPLEMENTATION
#include "vk_mem_alloc.h"
SP_WARNINGS_ON
//===================================

//= NAMESPACES ===============
using namespace std;
using namespace Spartan::Math;
//============================

namespace
{
    // Threading
    static mutex mutex_queue;
    static mutex mutex_allocation;
}

namespace Spartan
{
    namespace vulkan_memory_allocator
    {
        static VmaAllocator allocator;
        static std::unordered_map<uint64_t, VmaAllocation> allocations;

        static uint64_t resource_to_id(void* resource)
        {
            return reinterpret_cast<uint64_t>(resource);
        }

        static void save_allocation(void*& resource, const char* name, VmaAllocation allocation)
        {
            SP_ASSERT_MSG(resource != nullptr, "Resource can't be null");
            SP_ASSERT_MSG(name != nullptr, "Name can't be empty");

            // Set allocation data
            vmaSetAllocationUserData(allocator, allocation, resource);
            vmaSetAllocationName(allocator, allocation, name);

            // Name the allocation's underlying VkDeviceMemory
            vulkan_utility::debug::set_object_name(allocation->GetMemory(), name);

            lock_guard<mutex> lock(mutex_allocation);
            allocations[resource_to_id(resource)] = allocation;
        }

        static void destroy_allocation(void*& resource)
        {
            lock_guard<mutex> lock(mutex_allocation);

            auto it = allocations.find(resource_to_id(resource));
            if (it != allocations.end())
            {
                allocations.erase(it);
            }

            resource = nullptr;
        }

        static VmaAllocation get_allocation_from_resource(void* resource)
        {
            lock_guard<mutex> lock(mutex_allocation);

            auto it = allocations.find(resource_to_id(resource));
            if (it != allocations.end())
            {
                return it->second;
            }

            return nullptr;
        }
    }

    static bool is_present_instance_layer(const char* layer_name)
    {
        uint32_t layer_count;
        vkEnumerateInstanceLayerProperties(&layer_count, nullptr);

        vector<VkLayerProperties> layers(layer_count);
        vkEnumerateInstanceLayerProperties(&layer_count, layers.data());

        for (const auto& layer : layers)
        {
            if (strcmp(layer_name, layer.layerName) == 0)
                return true;
        }

        return false;
    }

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

    static vector<const char*> get_physical_device_supported_extensions(const vector<const char*>& extensions, VkPhysicalDevice device_physical)
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
                SP_LOG_ERROR("Device extension \"%s\" is not supported", extension);
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
                SP_LOG_ERROR("Instance extension \"%s\" is not supported", extension);
            }
        }

        return extensions_supported;
    }

    RHI_Device::RHI_Device()
    {
        SP_ASSERT_MSG(RHI_Context::IsInitialized(), "RHI context not initialized");

        #ifdef DEBUG
            // Add validation related extensions
            RHI_Context::validation_extensions.emplace_back(VK_VALIDATION_FEATURE_ENABLE_BEST_PRACTICES_EXT);
            RHI_Context::validation_extensions.emplace_back(VK_VALIDATION_FEATURE_ENABLE_SYNCHRONIZATION_VALIDATION_EXT);
            // Add debugging related extensions
            RHI_Context::extensions_instance.emplace_back("VK_EXT_debug_report");
            RHI_Context::extensions_instance.emplace_back("VK_EXT_debug_utils");
        #endif

        // Create instance
        VkApplicationInfo app_info = {};
        {
            app_info.sType              = VK_STRUCTURE_TYPE_APPLICATION_INFO;
            app_info.pApplicationName   = sp_name;
            app_info.pEngineName        = app_info.pApplicationName;
            app_info.engineVersion      = VK_MAKE_VERSION(sp_version_major, sp_version_minor, sp_version_revision);
            app_info.applicationVersion = app_info.engineVersion;

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

                // The following extensions have been promoted to 1.2 and 1.3.
                // VK_KHR_timeline_semaphore                 - 1.2
                // VK_KHR_dynamic_rendering                  - 1.3
                // VK_EXT_subgroup_size_control              - 1.3
                // VK_KHR_shader_float16_int8                - 1.2
                // VK_EXT_shader_demote_to_helper_invocation - 1.3
                // VK_KHR_synchronization2                   - 1.3
                // We make Vulkan 1.3 the minimum required version and we enable those extensions from the core.
                SP_ASSERT_MSG(app_info.apiVersion >= VK_API_VERSION_1_3, "Vulkan 1.3 is not supported");

                // In case the SDK is not supported by the driver, prompt the user to update
                if (sdk_version > driver_version)
                {
                    // Detect and log version
                    string driver_version_str = to_string(VK_API_VERSION_MAJOR(driver_version)) + "." + to_string(VK_API_VERSION_MINOR(driver_version)) + "." + to_string(VK_API_VERSION_PATCH(driver_version));
                    string sdk_version_str    = to_string(VK_API_VERSION_MAJOR(sdk_version)) + "." + to_string(VK_API_VERSION_MINOR(sdk_version)) + "." + to_string(VK_API_VERSION_PATCH(sdk_version));
                    SP_LOG_WARNING("Falling back to Vulkan %s. Please update your graphics drivers to support Vulkan %s.", driver_version_str.c_str(), sdk_version_str.c_str());
                }

                //  Save API version
                RHI_Context::api_version_str = to_string(VK_API_VERSION_MAJOR(app_info.apiVersion)) + "." + to_string(VK_API_VERSION_MINOR(app_info.apiVersion)) + "." + to_string(VK_API_VERSION_PATCH(app_info.apiVersion));
            }

            // Get the supported extensions out of the requested extensions
            vector<const char*> extensions_supported = get_supported_extensions(RHI_Context::extensions_instance);

            VkInstanceCreateInfo create_info    = {};
            create_info.sType                   = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
            create_info.pApplicationInfo        = &app_info;
            create_info.enabledExtensionCount   = static_cast<uint32_t>(extensions_supported.size());
            create_info.ppEnabledExtensionNames = extensions_supported.data();
            create_info.enabledLayerCount       = 0;

            // Validation features
            VkValidationFeaturesEXT validation_features       = {};
            validation_features.sType                         = VK_STRUCTURE_TYPE_VALIDATION_FEATURES_EXT;
            validation_features.enabledValidationFeatureCount = static_cast<uint32_t>(RHI_Context::validation_extensions.size());
            validation_features.pEnabledValidationFeatures    = RHI_Context::validation_extensions.data();

            if (RHI_Context::validation)
            {
                // Enable validation layer
                if (is_present_instance_layer(RHI_Context::validation_layers.front()))
                {
                    // Validation layers
                    create_info.enabledLayerCount   = static_cast<uint32_t>(RHI_Context::validation_layers.size());
                    create_info.ppEnabledLayerNames = RHI_Context::validation_layers.data();
                    create_info.pNext               = &validation_features;
                }
                else
                {
                    SP_LOG_ERROR("Validation layer was requested, but not available.");
                }
            }

            SP_ASSERT_MSG(vkCreateInstance(&create_info, nullptr, &RHI_Context::instance) == VK_SUCCESS, "Failed to create instance");
        }

        // Get function pointers (from extensions)
        vulkan_utility::functions::initialize(RHI_Context::validation, RHI_Context::gpu_markers);

        // Debug
        if (RHI_Context::validation)
        {
            vulkan_utility::debug::initialize(RHI_Context::instance);
        }

        // Find a physical device
        SP_ASSERT_MSG(DetectPhysicalDevices(), "Failed to detect any devices");
        SelectPrimaryPhysicalDevice();

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

            // Detect device properties
            {
                VkPhysicalDeviceVulkan13Properties device_properties_1_3 = {};
                device_properties_1_3.sType                              = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_PROPERTIES;
                device_properties_1_3.pNext                              = nullptr;

                VkPhysicalDeviceProperties2 properties_device = {};
                properties_device.sType                       = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
                properties_device.pNext                       = &device_properties_1_3;

                vkGetPhysicalDeviceProperties2(static_cast<VkPhysicalDevice>(RHI_Context::device_physical), &properties_device);

                // Save some properties
                m_max_texture_1d_dimension            = properties_device.properties.limits.maxImageDimension1D;
                m_max_texture_2d_dimension            = properties_device.properties.limits.maxImageDimension2D;
                m_max_texture_3d_dimension            = properties_device.properties.limits.maxImageDimension3D;
                m_max_texture_cube_dimension          = properties_device.properties.limits.maxImageDimensionCube;
                m_max_texture_array_layers            = properties_device.properties.limits.maxImageArrayLayers;
                m_min_uniform_buffer_offset_alignment = properties_device.properties.limits.minUniformBufferOffsetAlignment;
                m_min_storage_buffer_offset_alignment = properties_device.properties.limits.minStorageBufferOffsetAlignment;
                m_timestamp_period                    = properties_device.properties.limits.timestampPeriod;
                m_max_bound_descriptor_sets           = properties_device.properties.limits.maxBoundDescriptorSets;

                // Disable profiler if timestamps are not supported
                if (RHI_Context::gpu_profiling && !properties_device.properties.limits.timestampComputeAndGraphics)
                {
                    SP_LOG_ERROR("Device doesn't support timestamps, disabling gpu profiling...");
                    RHI_Context::gpu_profiling = false;
                }
            }

            // Enable certain features
            VkPhysicalDeviceVulkan13Features device_features_to_enable_1_3 = {};
            device_features_to_enable_1_3.sType                            = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
            VkPhysicalDeviceVulkan12Features device_features_to_enable_1_2 = {};
            device_features_to_enable_1_2.sType                            = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
            device_features_to_enable_1_2.pNext                            = &device_features_to_enable_1_3;
            VkPhysicalDeviceFeatures2 device_features_to_enable            = {};
            device_features_to_enable.sType                                = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
            device_features_to_enable.pNext                                = &device_features_to_enable_1_2;
            {
                // Check feature support
                VkPhysicalDeviceVulkan13Features features_supported_1_3 = {};
                features_supported_1_3.sType                            = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
                VkPhysicalDeviceVulkan12Features features_supported_1_2 = {};
                features_supported_1_2.sType                            = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
                features_supported_1_2.pNext                            = &features_supported_1_3;
                VkPhysicalDeviceFeatures2 features_supported            = {};
                features_supported.sType                                = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
                features_supported.pNext                                = &features_supported_1_2;
                vkGetPhysicalDeviceFeatures2(RHI_Context::device_physical, &features_supported);

                // Check if certain features are supported and enable them
                {
                    // Anisotropic filtering
                    SP_ASSERT(features_supported.features.samplerAnisotropy == VK_TRUE);
                    device_features_to_enable.features.samplerAnisotropy = VK_TRUE;

                    // Line and point rendering
                    SP_ASSERT(features_supported.features.fillModeNonSolid == VK_TRUE);
                    device_features_to_enable.features.fillModeNonSolid = VK_TRUE;

                    // Lines with adjustable thickness
                    SP_ASSERT(features_supported.features.wideLines == VK_TRUE);
                    device_features_to_enable.features.wideLines = VK_TRUE;

                    // Cubemaps
                    SP_ASSERT(features_supported.features.imageCubeArray == VK_TRUE);
                    device_features_to_enable.features.imageCubeArray = VK_TRUE;

                    // Partially bound descriptors
                    SP_ASSERT(features_supported_1_2.descriptorBindingPartiallyBound == VK_TRUE);
                    device_features_to_enable_1_2.descriptorBindingPartiallyBound = VK_TRUE;

                    // Runtime descriptor array
                    SP_ASSERT(features_supported_1_2.runtimeDescriptorArray == VK_TRUE);
                    device_features_to_enable_1_2.runtimeDescriptorArray = VK_TRUE;

                    // Timeline semaphores
                    SP_ASSERT(features_supported_1_2.timelineSemaphore == VK_TRUE);
                    device_features_to_enable_1_2.timelineSemaphore = VK_TRUE;

                    // Rendering without render passes and frame buffer objects
                    SP_ASSERT(features_supported_1_3.dynamicRendering == VK_TRUE);
                    device_features_to_enable_1_3.dynamicRendering = VK_TRUE;

                    // Float16 - FSR 2.0 will opt for it (for performance), but it's not a requirement, so don't assert on this one.
                    if (features_supported_1_2.shaderFloat16 == VK_TRUE)
                    {
                        device_features_to_enable_1_2.shaderFloat16 = VK_TRUE;
                    }

                    // Int16 - FSR 2.0 will opt for it (for performance), but it's not a requirement, so don't assert on this one.
                    if (features_supported.features.shaderInt16 == VK_TRUE)
                    {
                        device_features_to_enable.features.shaderInt16 = VK_TRUE;
                    }

                    // Wave64 - FSR 2.0 will opt for it (for performance), but it's not a requirement, so don't assert on this one.
                    if (features_supported_1_3.subgroupSizeControl == VK_TRUE)
                    {
                        device_features_to_enable_1_3.subgroupSizeControl = VK_TRUE;
                    }

                    // Wave64 - FSR 2.0 will opt for it (for performance), but it's not a requirement, so don't assert on this one.
                    if (features_supported_1_3.shaderDemoteToHelperInvocation == VK_TRUE)
                    {
                        device_features_to_enable_1_3.shaderDemoteToHelperInvocation = VK_TRUE;
                    }
                }
            }

            // Enable certain graphics shader stages
            {
                m_enabled_graphics_shader_stages = VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
                if (device_features_to_enable.features.geometryShader)
                {
                    m_enabled_graphics_shader_stages |= VK_PIPELINE_STAGE_GEOMETRY_SHADER_BIT;
                }
                if (device_features_to_enable.features.tessellationShader)
                {
                    m_enabled_graphics_shader_stages |= VK_PIPELINE_STAGE_TESSELLATION_CONTROL_SHADER_BIT | VK_PIPELINE_STAGE_TESSELLATION_EVALUATION_SHADER_BIT;
                }
            }

            // Get the supported extensions out of the requested extensions
            vector<const char*> extensions_supported = get_physical_device_supported_extensions(RHI_Context::extensions_device, RHI_Context::device_physical);

            // Device create info
            VkDeviceCreateInfo create_info = {};
            {
                create_info.sType                   = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
                create_info.queueCreateInfoCount    = static_cast<uint32_t>(queue_create_infos.size());
                create_info.pQueueCreateInfos       = queue_create_infos.data();
                create_info.pNext                   = &device_features_to_enable;
                create_info.enabledExtensionCount   = static_cast<uint32_t>(extensions_supported.size());
                create_info.ppEnabledExtensionNames = extensions_supported.data();

                if (RHI_Context::validation)
                {
                    create_info.enabledLayerCount   = static_cast<uint32_t>(RHI_Context::validation_layers.size());
                    create_info.ppEnabledLayerNames = RHI_Context::validation_layers.data();
                }
            }

            // Create
            SP_ASSERT_MSG(vkCreateDevice(RHI_Context::device_physical, &create_info, nullptr, &RHI_Context::device) == VK_SUCCESS, "Failed to create device");
        }

        // Get a graphics, compute and a copy queue.
        {
            vkGetDeviceQueue(RHI_Context::device, m_queue_graphics_index, 0, reinterpret_cast<VkQueue*>(&m_queue_graphics));
            vkGetDeviceQueue(RHI_Context::device, m_queue_compute_index,  0, reinterpret_cast<VkQueue*>(&m_queue_compute));
            vkGetDeviceQueue(RHI_Context::device, m_queue_copy_index,     0, reinterpret_cast<VkQueue*>(&m_queue_copy));
        }

        // Create memory allocator
        {
            VmaAllocatorCreateInfo allocator_info = {};
            allocator_info.physicalDevice         = RHI_Context::device_physical;
            allocator_info.device                 = RHI_Context::device;
            allocator_info.instance               = RHI_Context::instance;
            allocator_info.vulkanApiVersion       = app_info.apiVersion;

            SP_ASSERT_MSG(vmaCreateAllocator(&allocator_info, &vulkan_memory_allocator::allocator) == VK_SUCCESS, "Failed to create memory allocator");
        }

        // Set the descriptor set capacity to an initial value
        SetDescriptorSetCapacity(2048);

        // Detect and log version
        {
            string version_major = to_string(VK_VERSION_MAJOR(app_info.apiVersion));
            string version_minor = to_string(VK_VERSION_MINOR(app_info.apiVersion));
            string version_patch = to_string(VK_VERSION_PATCH(app_info.apiVersion));
            string version       = version_major + "." + version_minor + "." + version_patch;

            SP_LOG_INFO("Vulkan %s", version.c_str());

            Settings::RegisterThirdPartyLib("Vulkan", version_major + "." + version_minor + "." + version_patch, "https://vulkan.lunarg.com/");
        }
    }

    void RHI_Device::Destroy()
    {
        SP_ASSERT(m_queue_graphics != nullptr);

        QueueWaitAll();

        // Destroy command pools
        m_cmd_pools.clear();
        m_cmd_pools_immediate.fill(nullptr);

        // Descriptor pool
        vkDestroyDescriptorPool(RHI_Context::device, static_cast<VkDescriptorPool>(m_descriptor_pool), nullptr);
        m_descriptor_pool = nullptr;

        // Allocator
        if (vulkan_memory_allocator::allocator != nullptr)
        {
            SP_ASSERT_MSG(vulkan_memory_allocator::allocations.empty(), "There are still allocations");
            vmaDestroyAllocator(static_cast<VmaAllocator>(vulkan_memory_allocator::allocator));
            vulkan_memory_allocator::allocator = nullptr;
        }

        // Debug messenger
        if (RHI_Context::validation)
        {
            vulkan_utility::debug::shutdown(RHI_Context::instance);
        }

        // Device and instance
        vkDestroyDevice(RHI_Context::device, nullptr);
        vkDestroyInstance(RHI_Context::instance, nullptr);
    }

    bool RHI_Device::DetectPhysicalDevices()
    {
        uint32_t device_count = 0;
        SP_ASSERT_MSG(
            vkEnumeratePhysicalDevices(RHI_Context::instance, &device_count, nullptr) == VK_SUCCESS,
            "Failed to get physical device count"
        );

        SP_ASSERT_MSG(device_count != 0, "There are no available physical devices");
        
        vector<VkPhysicalDevice> physical_devices(device_count);
        SP_ASSERT_MSG(
            vkEnumeratePhysicalDevices(RHI_Context::instance, &device_count, physical_devices.data()) == VK_SUCCESS,
            "Failed to enumerate physical devices"
        );
        
        // Go through all the devices
        for (const VkPhysicalDevice& device_physical : physical_devices)
        {
            // Get device properties
            VkPhysicalDeviceProperties device_properties = {};
            vkGetPhysicalDeviceProperties(device_physical, &device_properties);
        
            VkPhysicalDeviceMemoryProperties device_memory_properties = {};
            vkGetPhysicalDeviceMemoryProperties(device_physical, &device_memory_properties);
        
            RHI_PhysicalDevice_Type type = RHI_PhysicalDevice_Type::Undefined;
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

    void RHI_Device::SelectPrimaryPhysicalDevice()
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
                SP_LOG_ERROR("Graphics queue not suported.");
                return false;
            }

            // Compute
            if (get_queue_family_index(VK_QUEUE_COMPUTE_BIT, queue_families_properties, &index))
            {
                SetQueueIndex(RHI_Queue_Type::Compute, index);
            }
            else
            {
                SP_LOG_ERROR("Compute queue not supported.");
                return false;
            }

            // Copy
            if (get_queue_family_index(VK_QUEUE_TRANSFER_BIT, queue_families_properties, &index))
            {
                SetQueueIndex(RHI_Queue_Type::Copy, index);
            }
            else
            {
                SP_LOG_ERROR("Copy queue not supported.");
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
                RHI_Context::device_physical = device;
                break;
            }
        }
    }

    void RHI_Device::QueuePresent(void* swapchain, uint32_t* image_index, vector<RHI_Semaphore*>& wait_semaphores)
    {
        static array<VkSemaphore, 3> vk_wait_semaphores = {};

        // Get semaphore Vulkan resource
        uint32_t semaphore_count = static_cast<uint32_t>(wait_semaphores.size());
        for (uint32_t i = 0; i < semaphore_count; i++)
        {
            SP_ASSERT_MSG(wait_semaphores[i]->GetCpuState() == RHI_Sync_State::Submitted, "The wait semaphore hasn't been signaled");
            vk_wait_semaphores[i] = static_cast<VkSemaphore>(wait_semaphores[i]->GetResource());
        }

        VkPresentInfoKHR present_info   = {};
        present_info.sType              = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
        present_info.waitSemaphoreCount = semaphore_count;
        present_info.pWaitSemaphores    = vk_wait_semaphores.data();
        present_info.swapchainCount     = 1;
        present_info.pSwapchains        = reinterpret_cast<VkSwapchainKHR*>(&swapchain);
        present_info.pImageIndices      = image_index;

        SP_VK_ASSERT_MSG(vkQueuePresentKHR(static_cast<VkQueue>(m_queue_graphics), &present_info), "Failed to present");

        // Update semaphore state
        for (uint32_t i = 0; i < semaphore_count; i++)
        {
            wait_semaphores[i]->SetCpuState(RHI_Sync_State::Idle);
        }
    }

    void RHI_Device::QueueSubmit(const RHI_Queue_Type type, const uint32_t wait_flags, void* cmd_buffer, RHI_Semaphore* wait_semaphore /*= nullptr*/, RHI_Semaphore* signal_semaphore /*= nullptr*/, RHI_Fence* signal_fence /*= nullptr*/)
    {
        SP_ASSERT_MSG(cmd_buffer != nullptr, "Invalid command buffer");

        // Validate semaphores
        if (wait_semaphore)   SP_ASSERT_MSG(wait_semaphore->GetCpuState()   != RHI_Sync_State::Idle,      "Wait semaphore is in an idle state and will never be signaled");
        if (signal_semaphore) SP_ASSERT_MSG(signal_semaphore->GetCpuState() != RHI_Sync_State::Submitted, "Signal semaphore is already in a signaled state.");
        if (signal_fence)     SP_ASSERT_MSG(signal_fence->GetCpuState()     != RHI_Sync_State::Submitted, "Signal fence is already in a signaled state.");

        // Get semaphores
        array<VkSemaphore, 1> vk_wait_semaphore   = { wait_semaphore   ? static_cast<VkSemaphore>(wait_semaphore->GetResource())   : nullptr };
        array<VkSemaphore, 1> vk_signal_semaphore = { signal_semaphore ? static_cast<VkSemaphore>(signal_semaphore->GetResource()) : nullptr };

        // Submit info
        VkSubmitInfo submit_info         = {};
        submit_info.sType                = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submit_info.pNext                = nullptr;
        submit_info.waitSemaphoreCount   = wait_semaphore   != nullptr ? 1 : 0;
        submit_info.pWaitSemaphores      = wait_semaphore   != nullptr ? vk_wait_semaphore.data() : nullptr;
        submit_info.signalSemaphoreCount = signal_semaphore != nullptr ? 1 : 0;
        submit_info.pSignalSemaphores    = signal_semaphore != nullptr ? vk_signal_semaphore.data() : nullptr;
        submit_info.pWaitDstStageMask    = &wait_flags;
        submit_info.commandBufferCount   = 1;
        submit_info.pCommandBuffers      = reinterpret_cast<VkCommandBuffer*>(&cmd_buffer);

        // Get signal fence
        void* vk_signal_fence = signal_fence ? signal_fence->GetResource() : nullptr;

        // The actual submit
        lock_guard<mutex> lock(mutex_queue);
        SP_VK_ASSERT_MSG(vkQueueSubmit(static_cast<VkQueue>(GetQueue(type)), 1, &submit_info, static_cast<VkFence>(vk_signal_fence)), "Failed to submit");

        // Update semaphore states
        if (wait_semaphore)   wait_semaphore->SetCpuState(RHI_Sync_State::Idle);
        if (signal_semaphore) signal_semaphore->SetCpuState(RHI_Sync_State::Submitted);
        if (signal_fence)     signal_fence->SetCpuState(RHI_Sync_State::Submitted);
    }

    void RHI_Device::QueueWait(const RHI_Queue_Type type)
    {
        lock_guard<mutex> lock(mutex_queue);
        SP_ASSERT_MSG(vkQueueWaitIdle(static_cast<VkQueue>(GetQueue(type))) == VK_SUCCESS, "Failed to wait for queue");
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

    void RHI_Device::ParseDeletionQueue(const unordered_map<RHI_Resource_Type, vector<void*>>& deletion_queue)
    {
        for (const auto& it : deletion_queue)
        {
            if (it.first == RHI_Resource_Type::texture)
            {
                for (void* resource : it.second)
                {
                    DestroyTexture(resource);
                }
            }
            else if (it.first == RHI_Resource_Type::texture_view)
            {
                for (void* resource : it.second)
                {
                    vkDestroyImageView(RHI_Context::device, static_cast<VkImageView>(resource), nullptr);
                }
            }
            else if (it.first == RHI_Resource_Type::sampler)
            {
                for (void* resource : it.second)
                {
                    vkDestroySampler(RHI_Context::device, reinterpret_cast<VkSampler>(resource), nullptr);
                }
            }
            else if (it.first == RHI_Resource_Type::buffer)
            {
                for (void* resource : it.second)
                {
                    DestroyBuffer(resource);
                }
            }
            else if (it.first == RHI_Resource_Type::shader)
            {
                for (void* resource : it.second)
                {
                    vkDestroyShaderModule(RHI_Context::device, static_cast<VkShaderModule>(resource), nullptr);
                }
            }
        }
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
            SP_LOG_WARNING("Capacity is already %d, is this reset needed ?");
        }

        // Create pool
        {
            static const uint16_t rhi_descriptor_max_textures                 = 16536;
            static const uint16_t rhi_descriptor_max_storage_textures         = 16536;
            static const uint16_t rhi_descriptor_max_storage_buffers          = 32;
            static const uint16_t rhi_descriptor_max_constant_buffers_dynamic = 32;
            static const uint16_t rhi_descriptor_max_samplers                 = 32;

            // Pool sizes
            array<VkDescriptorPoolSize, 5> pool_sizes =
            {
                VkDescriptorPoolSize{ VK_DESCRIPTOR_TYPE_SAMPLER,                rhi_descriptor_max_samplers },
                VkDescriptorPoolSize{ VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,          rhi_descriptor_max_textures },
                VkDescriptorPoolSize{ VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,          rhi_descriptor_max_storage_textures },
                VkDescriptorPoolSize{ VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, rhi_descriptor_max_storage_buffers }, // aka structured buffer
                VkDescriptorPoolSize{ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, rhi_descriptor_max_constant_buffers_dynamic }
            };

            // Create info
            VkDescriptorPoolCreateInfo pool_create_info = {};
            pool_create_info.sType                      = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
            pool_create_info.flags                      = VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT_EXT;
            pool_create_info.poolSizeCount              = static_cast<uint32_t>(pool_sizes.size());
            pool_create_info.pPoolSizes                 = pool_sizes.data();
            pool_create_info.maxSets                    = descriptor_set_capacity;

            SP_VK_ASSERT_MSG(
                vkCreateDescriptorPool(RHI_Context::device, &pool_create_info, nullptr, reinterpret_cast<VkDescriptorPool*>(&m_descriptor_pool)),
                "Failed to create descriptor pool."
            );
        }

        SP_LOG_INFO("Capacity has been set to %d elements", descriptor_set_capacity);
        m_descriptor_set_capacity = descriptor_set_capacity;

        Profiler::m_descriptor_set_count    = 0;
        Profiler::m_descriptor_set_capacity = m_descriptor_set_capacity;
    }

    void* RHI_Device::GetMappedDataFromBuffer(void* resource)
    {
        if (VmaAllocation allocation = static_cast<VmaAllocation>(vulkan_memory_allocator::get_allocation_from_resource(resource)))
        {
            return allocation->GetMappedData();
        }

        return nullptr;
    }

    void RHI_Device::CreateBuffer(void*& resource, const uint64_t size, uint32_t usage, uint32_t memory_property_flags, const void* data_initial, const char* name)
    {
        // Deduce some memory properties
        bool is_buffer_storage       = (usage & VK_BUFFER_USAGE_STORAGE_BUFFER_BIT) != 0; // aka structured buffer
        bool is_buffer_constant      = (usage & VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT) != 0;
        bool is_buffer_index         = (usage & VK_BUFFER_USAGE_INDEX_BUFFER_BIT) != 0;
        bool is_buffer_vertex        = (usage & VK_BUFFER_USAGE_VERTEX_BUFFER_BIT) != 0;
        bool is_buffer_staging       = (usage & VK_BUFFER_USAGE_TRANSFER_SRC_BIT) != 0;
        bool is_mappable             = (memory_property_flags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) != 0;
        bool is_transfer_source      = (usage & VK_BUFFER_USAGE_TRANSFER_SRC_BIT) != 0;
        bool is_transfer_destination = (usage & VK_BUFFER_USAGE_TRANSFER_DST_BIT) != 0;
        bool is_transfer_buffer      = is_transfer_source || is_transfer_destination;
        bool map_on_creation         = is_buffer_storage || is_buffer_constant || is_buffer_index || is_buffer_vertex;

        // Buffer info
        VkBufferCreateInfo buffer_create_info = {};
        buffer_create_info.sType              = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        buffer_create_info.size               = size;
        buffer_create_info.usage              = usage;
        buffer_create_info.sharingMode        = VK_SHARING_MODE_EXCLUSIVE;

        // Allocation info
        VmaAllocationCreateInfo allocation_create_info = {};
        allocation_create_info.usage                   = VMA_MEMORY_USAGE_AUTO;
        allocation_create_info.requiredFlags           = memory_property_flags;
        allocation_create_info.flags                   = 0;

        if (is_buffer_staging)
        {
            allocation_create_info.flags |= VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT;
        }
        else
        {
            // Can it be mapped ? Buffers that use Map()/Unmap() need this, persistent buffers also need this.
            allocation_create_info.flags |= is_mappable ? VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT : 0;

            // Can it be mapped upon creation ? This is what a persistent buffer would use.
            allocation_create_info.flags |= (map_on_creation && !is_transfer_buffer) ? VMA_ALLOCATION_CREATE_MAPPED_BIT : 0;

            // Cached on the CPU ? Our constant buffers are using dynamic offsets and do a lot of updates, so we need fast access.
            allocation_create_info.flags |= (is_buffer_constant || is_buffer_storage) ? VK_MEMORY_PROPERTY_HOST_CACHED_BIT : 0;
        }

        // Create the buffer
        VmaAllocation allocation = nullptr;
        VmaAllocationInfo allocation_info;
        SP_VK_ASSERT_MSG(vmaCreateBuffer(
            vulkan_memory_allocator::allocator,
                &buffer_create_info,
                &allocation_create_info,
                reinterpret_cast<VkBuffer*>(&resource),
                &allocation,
                &allocation_info),
        "Failed to created buffer");

        // If a pointer to the buffer data has been passed, map the buffer and copy over the data
        if (data_initial != nullptr)
        {
            SP_ASSERT(is_mappable && "Mapping initial data requires the buffer to be created with a VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT memory flag.");

            // Memory in Vulkan doesn't need to be unmapped before using it on GPU, but unless a
            // memory type has VK_MEMORY_PROPERTY_HOST_COHERENT_BIT flag set, you need to manually
            // invalidate the cache before reading a mapped pointer and flush cache after writing to
            // it. Map/unmap operations don't do that automatically.

            void* mapped_data = nullptr;
            SP_VK_ASSERT_MSG(vmaMapMemory(vulkan_memory_allocator::allocator, allocation, &mapped_data), "Failed to map allocation");
            memcpy(mapped_data, data_initial, size);
            SP_VK_ASSERT_MSG(vmaFlushAllocation(vulkan_memory_allocator::allocator, allocation, 0, size), "Failed to flush allocation");
            vmaUnmapMemory(vulkan_memory_allocator::allocator, allocation);
        }

        vulkan_memory_allocator::save_allocation(resource, name, allocation);
    }

    void RHI_Device::DestroyBuffer(void*& resource)
    {
        SP_ASSERT_MSG(resource != nullptr, "Resource is null");

        if (VmaAllocation allocation = static_cast<VmaAllocation>(vulkan_memory_allocator::get_allocation_from_resource(resource)))
        {
            vmaDestroyBuffer(vulkan_memory_allocator::allocator, static_cast<VkBuffer>(resource), allocation);
            vulkan_memory_allocator::destroy_allocation(resource);
        }
    }

    void RHI_Device::CreateTexture(void* vk_image_creat_info, void*& resource, const char* name)
    {
        VmaAllocationCreateInfo allocation_info = {};
        allocation_info.usage = VMA_MEMORY_USAGE_AUTO;

        // Create image
        VmaAllocation allocation;
        SP_VK_ASSERT_MSG(vmaCreateImage(
            vulkan_memory_allocator::allocator,
            static_cast<VkImageCreateInfo*>(vk_image_creat_info), &allocation_info,
            reinterpret_cast<VkImage*>(&resource),
            &allocation,
            nullptr),
        "Failed to allocate texture");

        vulkan_memory_allocator::save_allocation(resource, name, allocation);
    }

    void RHI_Device::DestroyTexture(void*& resource)
    {
        SP_ASSERT_MSG(resource != nullptr, "Resource is null");

        if (VmaAllocation allocation = static_cast<VmaAllocation>(vulkan_memory_allocator::get_allocation_from_resource(resource)))
        {
            vmaDestroyImage(vulkan_memory_allocator::allocator, static_cast<VkImage>(resource), allocation);
            vulkan_memory_allocator::destroy_allocation(resource);
        }
    }

    void RHI_Device::MapMemory(void* resource, void*& mapped_data)
    {
        if (VmaAllocation allocation = static_cast<VmaAllocation>(vulkan_memory_allocator::get_allocation_from_resource(resource)))
        {
            SP_ASSERT_MSG(vmaMapMemory(vulkan_memory_allocator::allocator, allocation, reinterpret_cast<void**>(&mapped_data)) == VK_SUCCESS, "Failed to map memory");
        }
    }

    void RHI_Device::UnmapMemory(void* resource, void*& mapped_data)
    {
        SP_ASSERT_MSG(mapped_data, "Memory is already unmapped");

        if (VmaAllocation allocation = static_cast<VmaAllocation>(vulkan_memory_allocator::get_allocation_from_resource(resource)))
        {
            vmaUnmapMemory(vulkan_memory_allocator::allocator, static_cast<VmaAllocation>(allocation));
            mapped_data = nullptr;
        }
    }

    void RHI_Device::FlushAllocation(void* resource, uint64_t offset, uint64_t size)
    {
        if (VmaAllocation allocation = static_cast<VmaAllocation>(vulkan_memory_allocator::get_allocation_from_resource(resource)))
        {
            SP_ASSERT_MSG(vmaFlushAllocation(vulkan_memory_allocator::allocator, allocation, offset, size) == VK_SUCCESS, "Failed to flush");
        }
    }
}
