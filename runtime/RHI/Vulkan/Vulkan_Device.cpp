/*
Copyright(c) 2016-2025 Panos Karabelas

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

//= INCLUDES ==========================
#include "pch.h"
#include "../../Profiling/Profiler.h"
#include "../Core/Debugging.h"
#include "../Rendering/Renderer.h"
#include "../RHI_Device.h"
#include "../RHI_Implementation.h"
#include "../RHI_Queue.h"
#include "../RHI_DescriptorSet.h"
#include "../RHI_Sampler.h"
#include "../RHI_Shader.h"
#include "../RHI_DescriptorSetLayout.h"
#include "../RHI_Pipeline.h"
#include "../RHI_Buffer.h"
SP_WARNINGS_OFF
#define VMA_IMPLEMENTATION
#include "vk_mem_alloc.h"
SP_WARNINGS_ON
//=====================================

//= NAMESPACES ===============
using namespace std;
using namespace spartan::math;
//============================

namespace spartan
{
    namespace version
    {
        uint32_t used = 0;

        string to_string(uint32_t version)
        {
            return std::to_string(VK_VERSION_MAJOR(version)) + "." + std::to_string(VK_VERSION_MINOR(version)) + "." + std::to_string(VK_VERSION_PATCH(version));
        }
    }

    namespace
    {
        mutex mutex_allocation;
        mutex mutex_deletion_queue;
        unordered_map<RHI_Resource_Type, vector<void*>> deletion_queue;

        VkImageUsageFlags get_image_usage_flags(const RHI_Texture* texture)
        {
            VkImageUsageFlags flags = 0;

            flags |= texture->IsSrv() ? VK_IMAGE_USAGE_SAMPLED_BIT                  : 0;
            flags |= texture->IsUav() ? VK_IMAGE_USAGE_STORAGE_BIT                  : 0;
            flags |= texture->IsVrs() ? VK_IMAGE_USAGE_SHADING_RATE_IMAGE_BIT_NV    : 0;
            flags |= texture->IsDsv() ? VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT : 0;
            flags |= texture->IsRtv() ? VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT         : 0;

            // If the texture has data, it will be staged, so it needs transfer bits
            // If the texture participates in clear or blit operations, it needs transfer bits
            if (texture->HasData() || (texture->GetFlags() & RHI_Texture_ClearBlit) != 0)
            {
                flags |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT; // source of a transfer command
                flags |= VK_IMAGE_USAGE_TRANSFER_DST_BIT; // destination of a transfer command
            }

            return flags;
        }

        VkImageTiling get_format_tiling(const RHI_Texture* texture)
        {
            VkFormatProperties2 format_properties2 = {};
            format_properties2.sType = VK_STRUCTURE_TYPE_FORMAT_PROPERTIES_2;

            VkFormatProperties3 format_properties3 = {};
            format_properties3.sType               = VK_STRUCTURE_TYPE_FORMAT_PROPERTIES_3;
            format_properties2.pNext               = &format_properties3;

            VkFormatFeatureFlagBits2 format_flags  = 0;
            format_flags                          |= texture->IsSrv() ? VK_FORMAT_FEATURE_2_SAMPLED_IMAGE_BIT : 0;
            format_flags                          |= texture->IsUav() ? VK_FORMAT_FEATURE_2_STORAGE_IMAGE_BIT : 0;
            format_flags                          |= texture->IsRtv() ? VK_FORMAT_FEATURE_2_COLOR_ATTACHMENT_BIT : 0;
            format_flags                          |= texture->IsDsv() ? VK_FORMAT_FEATURE_2_DEPTH_STENCIL_ATTACHMENT_BIT : 0;
            format_flags                          |= texture->IsVrs() ? VK_FORMAT_FEATURE_2_FRAGMENT_SHADING_RATE_ATTACHMENT_BIT_KHR : 0;

            vkGetPhysicalDeviceFormatProperties2(RHI_Context::device_physical, vulkan_format[rhi_format_to_index(texture->GetFormat())], &format_properties2);

            // check for optimal support
            VkImageTiling tiling = VK_IMAGE_TILING_MAX_ENUM;
            if (format_properties3.optimalTilingFeatures & format_flags)
            {
                tiling = VK_IMAGE_TILING_OPTIMAL;
            }
            // check for linear support
            else if (format_properties3.linearTilingFeatures & format_flags)
            {
                tiling = VK_IMAGE_TILING_LINEAR;
            }

            SP_ASSERT_MSG(tiling != VK_IMAGE_TILING_MAX_ENUM, "The GPU doesn't support this format");
            SP_ASSERT_MSG(tiling == VK_IMAGE_TILING_OPTIMAL, "This format doesn't support optimal tiling, switch to a more efficient format");

            return tiling;
        }

        VkApplicationInfo create_application_info()
        {
            VkApplicationInfo app_info  = {};
            app_info.sType              = VK_STRUCTURE_TYPE_APPLICATION_INFO;
            app_info.pApplicationName   = sp_info::name;             // for gpu vendors to do game specific driver optimizations
            app_info.pEngineName        = app_info.pApplicationName; // for gpu vendors to do engine specific driver optimizations
            app_info.engineVersion      = VK_MAKE_VERSION(sp_info::version_major, sp_info::version_minor, sp_info::version_revision);
            app_info.applicationVersion = app_info.engineVersion;

            // deduce api version to use based on the SDK and what the driver supports
            {
                uint32_t driver_version = 0;
                {
                    // per LunarG, if vkEnumerateInstanceVersion is not present, we are running on Vulkan 1.0
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

                // choose the version which is supported by both the sdk and the driver
                uint32_t sdk_version = VK_HEADER_VERSION_COMPLETE;
                app_info.apiVersion  = min(sdk_version, driver_version);

                // save the api version we ended up using
                version::used                = app_info.apiVersion;
                RHI_Context::api_version_str = version::to_string(version::used);

                // some checks
                {
                    // if the driver hasn't been updated to the latest SDK, log a warning
                    if (sdk_version > driver_version)
                    {
                        string version_driver = version::to_string(driver_version);
                        string version_sdk    = version::to_string(sdk_version);
                        SP_LOG_WARNING("Using Vulkan %s, update drivers or wait for GPU vendor to support Vulkan %s, engine may still work", version_driver.c_str(), version_sdk.c_str());
                    }

                    // ensure that the machine supports Vulkan 1.4 (as we are using extensions from it)
                    uint32_t driver_major = VK_API_VERSION_MAJOR(driver_version);
                    uint32_t driver_minor = VK_API_VERSION_MINOR(driver_version);
                    uint32_t min_major    = VK_API_VERSION_MAJOR(VK_API_VERSION_1_4);
                    uint32_t min_minor    = VK_API_VERSION_MINOR(VK_API_VERSION_1_4);
                    if (driver_major < min_major || (driver_major == min_major && driver_minor < min_minor))
                    { 
                        SP_ERROR_WINDOW("Your GPU doesn't support Vulkan 1.4");
                    }
                }
            }

            return app_info;
        }
    }

    namespace functions
    {
        PFN_vkCreateDebugUtilsMessengerEXT  create_messenger          = nullptr;
        PFN_vkDestroyDebugUtilsMessengerEXT destroy_messenger         = nullptr;
        PFN_vkSetDebugUtilsObjectTagEXT     set_object_tag            = nullptr;
        PFN_vkSetDebugUtilsObjectNameEXT    set_object_name           = nullptr;
        PFN_vkCmdBeginDebugUtilsLabelEXT    marker_begin              = nullptr;
        PFN_vkCmdEndDebugUtilsLabelEXT      marker_end                = nullptr;
        PFN_vkCmdSetFragmentShadingRateKHR  set_fragment_shading_rate = nullptr;

        void get_pointers()
        {
            #define get_func(var, def)\
            var = reinterpret_cast<PFN_##def>(vkGetInstanceProcAddr(static_cast<VkInstance>(RHI_Context::instance), #def));\
            if (!var) SP_LOG_ERROR("Failed to get function pointer for %s", #def);\

            /* VK_EXT_debug_utils */
            {
                if (Debugging::IsValidationLayerEnabled())
                {
                    get_func(create_messenger, vkCreateDebugUtilsMessengerEXT);
                    get_func(destroy_messenger, vkDestroyDebugUtilsMessengerEXT);

                    SP_ASSERT(create_messenger && destroy_messenger);
                }

                if (Debugging::IsGpuMarkingEnabled())
                {
                    get_func(marker_begin, vkCmdBeginDebugUtilsLabelEXT);
                    get_func(marker_end, vkCmdEndDebugUtilsLabelEXT);

                    SP_ASSERT(marker_begin && marker_end);
                }
            }

            /* VK_EXT_debug_marker */
            if (Debugging::IsValidationLayerEnabled())
            {
                get_func(set_object_tag, vkSetDebugUtilsObjectTagEXT);
                get_func(set_object_name, vkSetDebugUtilsObjectNameEXT);

                SP_ASSERT(set_object_tag && set_object_name);
            }

            get_func(set_fragment_shading_rate, vkCmdSetFragmentShadingRateKHR);
            SP_ASSERT(set_fragment_shading_rate);
        }
    }

    namespace extensions
    {
        // hardware capability viewer: https://vulkan.gpuinfo.org/

        vector<const char*> extensions_instance = { "VK_KHR_surface", "VK_KHR_win32_surface", "VK_EXT_swapchain_colorspace", };
        vector<const char*> extensions_device   = {
            "VK_KHR_swapchain",
            "VK_EXT_memory_budget",           // to obtain precise memory usage information from Vulkan Memory Allocator
            "VK_KHR_fragment_shading_rate",   
            "VK_EXT_hdr_metadata",            
            "VK_EXT_robustness2",             
            "VK_KHR_external_memory",         // to share images with Intel Open Image Denoise
            #if defined(_WIN32)
            "VK_KHR_external_memory_win32",   // external memory handle type, linux alternative: VK_KHR_external_memory_fd
            #endif
            "VK_KHR_synchronization2",        // this is part of Vulkan 1.4 but AMD FidelityFX Breadcrumbs without it (they fetch device pointers from some table)
            "VK_KHR_get_memory_requirements2" // this is part of Vulkan 1.4 but AMD FidelityFX FSR 3 crashes without it (they fetch device pointers from some table)
        };

        bool is_present_device(const char* extension_name, VkPhysicalDevice device_physical)
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

        bool is_present_instance(const char* extension_name)
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

        vector<const char*> get_extensions_device()
        {
            vector<const char*> extensions_supported;
            for (const auto& extension : extensions_device)
            {
                if (is_present_device(extension, RHI_Context::device_physical))
                {
                    extensions_supported.emplace_back(extension);
                }
                else
                {
                    SP_LOG_WARNING("Device extension \"%s\" is not supported", extension);
                }
            }

            return extensions_supported;
        }

        vector<const char*> get_extensions_instance()
        {
            // validation layer messaging/logging
            if (Debugging::IsValidationLayerEnabled())
            {
                extensions_instance.emplace_back("VK_EXT_debug_report");
            }

            // object naming (for the validation messages) and gpu markers
            if (Debugging::IsGpuMarkingEnabled())
            {
                extensions_instance.emplace_back("VK_EXT_debug_utils");
            }

            vector<const char*> extensions_supported;
            for (const auto& extension : extensions_instance)
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
    }

    namespace validation_layer
    {
        // layers configuration: https://vulkan.lunarg.com/doc/view/1.3.296.0/windows/layer_configuration.html
        const char* name = "VK_LAYER_KHRONOS_validation";

        const VkBool32 setting_validate_core          = VK_TRUE;                                                       // enable core validation checks
        const VkBool32 setting_validate_sync          = VK_TRUE;                                                       // enable synchronization validation checks
        const VkBool32 setting_thread_safety          = VK_TRUE;                                                       // enable thread safety checks
        const char* setting_debug_action[]            = { "VK_DBG_LAYER_ACTION_LOG_MSG" };                             // specify action to log messages from validation layers
        const char* setting_report_flags[]            = { "info", "warn", "perf", "error", "debug" };                  // specify types of messages to be reported by validation layers
        const VkBool32 setting_enable_message_limit   = VK_TRUE;                                                       // enable limiting of duplicate validation messages
        const int32_t setting_duplicate_message_limit = 1;                                                             // set the limit for duplicate validation messages
        const char* setting_synchronization           = "VK_VALIDATION_FEATURE_ENABLE_SYNCHRONIZATION_VALIDATION_EXT"; // enable synchronization validation 
        const char* setting_best_practices            = "VK_VALIDATION_FEATURE_ENABLE_BEST_PRACTICES_EXT";             // enable best practices
        const char* setting_vendor_amd                = "VALIDATION_CHECK_ENABLE_VENDOR_SPECIFIC_AMD";                 // enable AMD-specific best practices
        const char* setting_vendor_nvidia             = "VALIDATION_CHECK_ENABLE_VENDOR_SPECIFIC_NVIDIA";              // enable Nvidia-specific best practices

        vector<VkLayerSettingEXT> get_settings()
        {
            SP_ASSERT(Debugging::IsValidationLayerEnabled());

            // check layer availability
            {
                uint32_t layer_count;
                vkEnumerateInstanceLayerProperties(&layer_count, nullptr);

                vector<VkLayerProperties> layers(layer_count);
                vkEnumerateInstanceLayerProperties(&layer_count, layers.data());

                bool validation_layer_unavailable = true;
                for (const VkLayerProperties& layer : layers)
                {
                    if (strcmp(name, layer.layerName) == 0)
                    {
                        validation_layer_unavailable = false;
                        break;
                    }
                }

                SP_ASSERT_MSG(!validation_layer_unavailable, "Please install the Vulkan SDK, ensure correct environment variables and restart your machine: https://vulkan.lunarg.com/sdk/home");
            }

            // create settings
            vector<VkLayerSettingEXT> settings =
            {
                { name, "validate_core",           VK_LAYER_SETTING_TYPE_BOOL32_EXT, 1, &setting_validate_core },           // enable core validation checks
                { name, "validate_sync",           VK_LAYER_SETTING_TYPE_BOOL32_EXT, 1, &setting_validate_sync },           // enable synchronization validation checks
                { name, "thread_safety",           VK_LAYER_SETTING_TYPE_BOOL32_EXT, 1, &setting_thread_safety },           // enable thread safety checks
                { name, "debug_action",            VK_LAYER_SETTING_TYPE_STRING_EXT, 1, setting_debug_action },             // specify action to log messages
                { name, "report_flags",            VK_LAYER_SETTING_TYPE_STRING_EXT, 5, setting_report_flags },             // specify types of messages to report
                { name, "enable_message_limit",    VK_LAYER_SETTING_TYPE_BOOL32_EXT, 1, &setting_enable_message_limit },    // enable limiting duplicate messages
                { name, "duplicate_message_limit", VK_LAYER_SETTING_TYPE_INT32_EXT,  1, &setting_duplicate_message_limit }, // set limit for duplicate messages
                { name, "enables",                 VK_LAYER_SETTING_TYPE_STRING_EXT, 1, &setting_synchronization },         // enable synchronization validation
                { name, "enables",                 VK_LAYER_SETTING_TYPE_STRING_EXT, 1, &setting_best_practices },          // enable best practices
                { name, "enables",                 VK_LAYER_SETTING_TYPE_STRING_EXT, 1, &setting_vendor_amd },              // enable AMD-specific best practices
                { name, "enables",                 VK_LAYER_SETTING_TYPE_STRING_EXT, 1, &setting_vendor_nvidia }            // enable Nvidia-specific best practices
            };

            // enable gpu-assisted validation
            if (Debugging::IsGpuAssistedValidationEnabled())
            {
                const char* setting_enable_gpu_assisted = "VK_VALIDATION_FEATURE_ENABLE_GPU_ASSISTED_EXT";
                settings.push_back({ name, "enables", VK_LAYER_SETTING_TYPE_STRING_EXT, 1, &setting_enable_gpu_assisted });
            }

            return settings;
        }

        namespace logging
        {
            VkDebugUtilsMessengerEXT messenger;

            VKAPI_ATTR VkBool32 VKAPI_CALL log
            (
                VkDebugUtilsMessageSeverityFlagBitsEXT msg_severity,
                VkDebugUtilsMessageTypeFlagsEXT msg_type,
                const VkDebugUtilsMessengerCallbackDataEXT* p_callback_data,
                void* p_user_data
            )
            {
                // ignore some messages
                {
                    // occlusion queries
                    if (p_callback_data->messageIdNumber == 0xd39be754)
                    {
                        // Validation Warning:
                        // [BestPractices - QueryPool - Unavailable] Object 0 :
                        // handle = 0x980b0000000002e, name = query_pool_occlusion, type = VK_OBJECT_TYPE_QUERY_POOL; | MessageID = 0xd39be754 | vkGetQueryPoolResults() :
                        // QueryPool VkQueryPool 0x980b0000000002e[query_pool_occlusion] and query 0 : vkCmdBeginQuery() was never called.
                        return VK_FALSE;
                    }
                }

                string msg = "Vulkan: " + string(p_callback_data->pMessage);

                if (/*(msg_severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT) ||*/ (msg_severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT))
                {
                    Log::Write(msg.c_str(), LogType::Info);
                }
                else if (msg_severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT)
                {
                    Log::Write(msg.c_str(), LogType::Warning);
                }
                else if (msg_severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT)
                {
                    Log::Write(msg.c_str(), LogType::Error);
                }

                return VK_FALSE;
            }

            void enable()
            {
                if (functions::create_messenger)
                {
                    VkDebugUtilsMessengerCreateInfoEXT create_info = {};
                    create_info.sType                              = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
                    create_info.messageSeverity                    = VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
                    create_info.messageType                        = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
                    create_info.pfnUserCallback                    = log;

                    functions::create_messenger(RHI_Context::instance, &create_info, nullptr, &messenger);
                }
            }

            void shutdown(VkInstance instance)
            {
                if (!functions::destroy_messenger)
                    return;

                functions::destroy_messenger(instance, messenger, nullptr);
            }
        }
    }

    namespace queues
    {
        void* graphics = nullptr;
        void* compute  = nullptr;
        void* copy     = nullptr;

        uint32_t index_graphics = numeric_limits<uint32_t>::max();
        uint32_t index_compute  = numeric_limits<uint32_t>::max();
        uint32_t index_copy     = numeric_limits<uint32_t>::max();

        array<shared_ptr<RHI_Queue>, static_cast<uint32_t>(RHI_Queue_Type::Max)> regular;   // graphics, compute, and copy
        array<shared_ptr<RHI_Queue>, static_cast<uint32_t>(RHI_Queue_Type::Max)> immediate; // graphics, compute, and copy

        // sync for immediate execution
        mutex mutex_queue;
        mutex mutex_immediate_execution;
        condition_variable condition_variable_immediate_execution;
        bool is_immediate_executing = false;
        RHI_Queue* queue            = nullptr;

        void destroy()
        {
            regular.fill(nullptr);
            immediate.fill(nullptr);
        }

        uint32_t get_queue_family_index(const vector<VkQueueFamilyProperties>& queue_families, VkQueueFlags queue_flags)
        {
            // compute only queue family index
            if ((queue_flags & VK_QUEUE_COMPUTE_BIT) == queue_flags)
            {
                for (uint32_t i = 0; i < static_cast<uint32_t>(queue_families.size()); i++)
                {
                    if (i == index_graphics)
                        continue;

                    if ((queue_families[i].queueFlags & VK_QUEUE_COMPUTE_BIT) && ((queue_families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) == 0))
                    {
                        return i;
                    }
                }
            }

            // transfer only queue family index
            if ((queue_flags & VK_QUEUE_TRANSFER_BIT) == queue_flags)
            {
                for (uint32_t i = 0; i < static_cast<uint32_t>(queue_families.size()); i++)
                {
                    if (i == index_graphics || i == index_compute)
                        continue;

                    if ((queue_families[i].queueFlags & VK_QUEUE_TRANSFER_BIT) && ((queue_families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) == 0) && ((queue_families[i].queueFlags & VK_QUEUE_COMPUTE_BIT) == 0))
                    {
                        return i;
                    }
                }
            }

            // first available graphics queue family index
            for (uint32_t i = 0; i < static_cast<uint32_t>(queue_families.size()); i++)
            {
                if ((queue_families[i].queueFlags & queue_flags) == queue_flags)
                {
                    return i;
                }
            }

            SP_ASSERT_MSG(false, "Could not find a matching queue family index");
            return numeric_limits<uint32_t>::max();
        }

        void detect_queue_family_indices(VkPhysicalDevice device_physical)
        {
            uint32_t queue_family_count = 0;
            vkGetPhysicalDeviceQueueFamilyProperties(device_physical, &queue_family_count, nullptr);

            vector<VkQueueFamilyProperties> queue_families(queue_family_count);
            vkGetPhysicalDeviceQueueFamilyProperties(device_physical, &queue_family_count, queue_families.data());

            index_graphics = get_queue_family_index(queue_families, VK_QUEUE_GRAPHICS_BIT);
            index_compute  = get_queue_family_index(queue_families, VK_QUEUE_COMPUTE_BIT);
            index_copy     = get_queue_family_index(queue_families, VK_QUEUE_TRANSFER_BIT);
        }

        bool get_queue_family_index(VkQueueFlagBits queue_flags, const vector<VkQueueFamilyProperties>& queue_family_properties, uint32_t* index)
        {
            // try to find a queue that only supports compute (dedicated)
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

            // try to find a queue that only supports copy (dedicated)
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

            // for graphics, just find any queue that supports graphics
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

        bool get_queue_family_indices(const VkPhysicalDevice& physical_device)
        {
            uint32_t queue_family_count = 0;
            vkGetPhysicalDeviceQueueFamilyProperties(physical_device, &queue_family_count, nullptr);
        
            vector<VkQueueFamilyProperties> queue_families_properties(queue_family_count);
            vkGetPhysicalDeviceQueueFamilyProperties(physical_device, &queue_family_count, queue_families_properties.data());
        
            // graphics
            uint32_t index = 0;
            if (get_queue_family_index(VK_QUEUE_GRAPHICS_BIT, queue_families_properties, &index))
            {
                queues::index_graphics = index;
            }
            else
            {
                SP_LOG_ERROR("Graphics queue not suported.");
                return false;
            }
        
            // compute
            if (get_queue_family_index(VK_QUEUE_COMPUTE_BIT, queue_families_properties, &index))
            {
                queues::index_compute = index;
            }
            else
            {
                SP_LOG_ERROR("Compute queue not supported.");
                return false;
            }
        
            // copy
            if (get_queue_family_index(VK_QUEUE_TRANSFER_BIT, queue_families_properties, &index))
            {
                queues::index_copy = index;
            }
            else
            {
                SP_LOG_ERROR("Copy queue not supported.");
                return false;
            }
        
            return true;
        };
    }

    namespace vulkan_memory_allocator
    {
        mutex mutex_allocator;
        VmaAllocator allocator;
        VmaAllocator allocator_external;

        struct AllocationData
        {
            VmaAllocation allocation;
            void* resource;
            bool external_memory;
        };
        vector<AllocationData> allocations;

        void initialize()
        {
            // https://gpuopen-librariesandsdks.github.io/VulkanMemoryAllocator/html/staying_within_budget.html
            // It is recommended to use VK_EXT_memory_budget device extension to obtain information about the budget from Vulkan device.
            // VMA is able to use this extension automatically. When not enabled, the allocator behaves the same way, but then it estimates
            // current usage and available budget based on its internal information and Vulkan memory heap sizes, which may be less precise.

            // allocator
            VmaAllocatorCreateInfo allocator_info = {};
            {
                allocator_info.physicalDevice         = RHI_Context::device_physical;
                allocator_info.device                 = RHI_Context::device;
                allocator_info.instance               = RHI_Context::instance;
                allocator_info.vulkanApiVersion       = version::used;
                allocator_info.flags                  = VMA_ALLOCATOR_CREATE_EXT_MEMORY_BUDGET_BIT;
                SP_ASSERT_VK(vmaCreateAllocator(&allocator_info, &vulkan_memory_allocator::allocator));
            }

            // allocator external
            {
                vector<VkExternalMemoryHandleTypeFlags> external_memory_handle_types;
                VkPhysicalDeviceMemoryProperties physical_device_memory_properties;
                vkGetPhysicalDeviceMemoryProperties(RHI_Context::device_physical, &physical_device_memory_properties);
                #if defined(_WIN32)
                external_memory_handle_types.resize(physical_device_memory_properties.memoryTypeCount, VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_WIN32_BIT_KHR);
                #else
                SP_LOG_ERROR("Not implemented, you need to use the Linux equivalent via VK_KHR_external_memory_fd");
                #endif
                allocator_info.pTypeExternalMemoryHandleTypes = external_memory_handle_types.data();

                SP_ASSERT_VK(vmaCreateAllocator(&allocator_info, &vulkan_memory_allocator::allocator_external));
            }

            // register version (I don't think VMA provides version defines)
            Settings::RegisterThirdPartyLib("AMD Vulkan Memory Allocator", "3.2.0", "https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator");
        }

        void destroy()
        {
            SP_ASSERT(vulkan_memory_allocator::allocator != nullptr);
            SP_ASSERT_MSG(vulkan_memory_allocator::allocations.empty(),  "There are still allocations");

            vmaDestroyAllocator(static_cast<VmaAllocator>(vulkan_memory_allocator::allocator));
            vulkan_memory_allocator::allocator = nullptr;

            vmaDestroyAllocator(static_cast<VmaAllocator>(vulkan_memory_allocator::allocator_external));
            vulkan_memory_allocator::allocator_external = nullptr;
        }

        void save_allocation(void*& resource, bool external_memory, VmaAllocation allocation)
        {
            SP_ASSERT(resource != nullptr);

            lock_guard<mutex> lock(mutex_allocation);

            AllocationData allocation_data  = {};
            allocation_data.allocation      = allocation;
            allocation_data.resource        = resource;
            allocation_data.external_memory = external_memory;
            allocations.emplace_back(allocation_data);
        }

        void destroy_allocation(void*& resource)
        {
            lock_guard<mutex> lock(mutex_allocation);

            auto it = std::find_if(allocations.begin(), allocations.end(), [&](const AllocationData& data)
            {
                return data.resource == resource;
            });

            if (it != allocations.end())
            {
                allocations.erase(it);
                resource = nullptr;
            }
        }

        AllocationData* get_allocation_from_resource(void* resource)
        {
            lock_guard<mutex> lock(mutex_allocation);

            auto it = find_if(allocations.begin(), allocations.end(), [&](const AllocationData& data)
            {
                return data.resource == resource;
            });

            if (it != allocations.end())
                return &(*it);

            return nullptr;
        }
    }

    namespace descriptors
    {
        mutex descriptor_pipeline_mutex;
        uint32_t allocated_descriptor_sets = 0;
        VkDescriptorPool descriptor_pool   = nullptr;

        // cache
        unordered_map<uint64_t, RHI_DescriptorSet> sets;
        unordered_map<uint64_t, shared_ptr<RHI_DescriptorSetLayout>> layouts;
        unordered_map<uint64_t, shared_ptr<RHI_Pipeline>> pipelines;
        unordered_map<uint64_t, vector<RHI_Descriptor>> descriptor_cache;

        void merge_descriptors(vector<RHI_Descriptor>& base_descriptors, const std::vector<RHI_Descriptor>& additional_descriptors)
        {
            for (const RHI_Descriptor& descriptor_additional : additional_descriptors)
            {
                bool updated_existing = false;
                for (RHI_Descriptor& descriptor_base : base_descriptors)
                {
                    if (descriptor_base.slot == descriptor_additional.slot)
                    {
                        descriptor_base.stage |= descriptor_additional.stage;
                        updated_existing = true;
                        break;
                    }
                }

                // if no updating took place, this is an additional shader only resource, add it
                if (!updated_existing)
                {
                    base_descriptors.emplace_back(descriptor_additional);
                }
            }
        }

        void get_descriptors_from_pipeline_state(RHI_PipelineState& pipeline_state, vector<RHI_Descriptor>& descriptors)
        {
            pipeline_state.Prepare();

            // use the hash of the pipeline state as the key for the cache
            uint64_t pipeline_state_hash = pipeline_state.GetHash();

            // check if descriptors for this pipeline state are already cached
            auto cached_descriptors = descriptor_cache.find(pipeline_state_hash);
            if (cached_descriptors != descriptor_cache.end())
            {
                // fetch from cache
                descriptors = cached_descriptors->second;
                return;
            }

            // if not cached, generate descriptors
            descriptors.clear();

            if (pipeline_state.IsCompute())
            {
                SP_ASSERT(pipeline_state.shaders[RHI_Shader_Type::Compute]->GetCompilationState() == RHI_ShaderCompilationState::Succeeded);
                descriptors = pipeline_state.shaders[RHI_Shader_Type::Compute]->GetDescriptors();
            }
            else if (pipeline_state.IsGraphics())
            {
                SP_ASSERT(pipeline_state.shaders[RHI_Shader_Type::Vertex]->GetCompilationState() == RHI_ShaderCompilationState::Succeeded);
                descriptors = pipeline_state.shaders[RHI_Shader_Type::Vertex]->GetDescriptors();

                if (pipeline_state.shaders[RHI_Shader_Type::Pixel])
                {
                    SP_ASSERT(pipeline_state.shaders[RHI_Shader_Type::Pixel]->GetCompilationState() == RHI_ShaderCompilationState::Succeeded);
                    merge_descriptors(descriptors, pipeline_state.shaders[RHI_Shader_Type::Pixel]->GetDescriptors());
                }

                if (pipeline_state.shaders[RHI_Shader_Type::Hull])
                {
                    SP_ASSERT(pipeline_state.shaders[RHI_Shader_Type::Hull]->GetCompilationState() == RHI_ShaderCompilationState::Succeeded);
                    merge_descriptors(descriptors, pipeline_state.shaders[RHI_Shader_Type::Hull]->GetDescriptors());
                }

                if (pipeline_state.shaders[RHI_Shader_Type::Domain])
                {
                    SP_ASSERT(pipeline_state.shaders[RHI_Shader_Type::Domain]->GetCompilationState() == RHI_ShaderCompilationState::Succeeded);
                    merge_descriptors(descriptors, pipeline_state.shaders[RHI_Shader_Type::Domain]->GetDescriptors());
                }
            }

            // sort descriptors by slot
            // this makes things easier to work with, for example dynamic offsets
            // are expected as a list which should be ordered by a slot
            sort(descriptors.begin(), descriptors.end(), [](const RHI_Descriptor& a, const RHI_Descriptor& b)
            {
                return a.slot < b.slot;
            });

            // cache the newly created descriptors
            descriptor_cache[pipeline_state_hash] = descriptors;
        }

        shared_ptr<RHI_DescriptorSetLayout> get_or_create_descriptor_set_layout(RHI_PipelineState& pipeline_state)
        {
            // get descriptors from pipeline state
            vector<RHI_Descriptor> descriptors;
            get_descriptors_from_pipeline_state(pipeline_state, descriptors);

            // compute a hash for the descriptors
            uint64_t hash = 0;
            for (RHI_Descriptor& descriptor : descriptors)
            {
                hash = rhi_hash_combine(hash, static_cast<uint64_t>(descriptor.slot));
                hash = rhi_hash_combine(hash, static_cast<uint64_t>(descriptor.stage));
            }

            // search for a descriptor set layout which matches this hash
            auto it     = layouts.find(hash);
            bool cached = it != layouts.end();

            // if there is no descriptor set layout for this particular hash, create one
            if (!cached)
            {
                // emplace a new descriptor set layout
                it = layouts.emplace(make_pair(hash, make_shared<RHI_DescriptorSetLayout>(descriptors, pipeline_state.name))).first;
            }
            shared_ptr<RHI_DescriptorSetLayout> descriptor_set_layout = it->second;

            if (cached)
            {
                descriptor_set_layout->ClearDescriptorData();
            }

            return descriptor_set_layout;
        }

        namespace bindless
        {
            array<VkDescriptorSet, static_cast<uint32_t>(RHI_Device_Bindless_Resource::Max)> sets;
            array<VkDescriptorSetLayout, static_cast<uint32_t>(RHI_Device_Bindless_Resource::Max)> layouts;

            void create_layout(const RHI_Device_Bindless_Resource type, const uint32_t count, const uint32_t binding, const string& name)
            {
                  VkDescriptorSetLayoutBinding layout_binding = {};
                  layout_binding.binding                      = binding;
                  layout_binding.descriptorType               = VK_DESCRIPTOR_TYPE_MAX_ENUM;
                  if (type == RHI_Device_Bindless_Resource::MaterialTextures)
                  {
                      layout_binding.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
                  }
                  else if (type == RHI_Device_Bindless_Resource::MaterialParameters || type == RHI_Device_Bindless_Resource::LightParameters)
                  {
                      layout_binding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
                  }
                  else if (type == RHI_Device_Bindless_Resource::SamplersComparison || type == RHI_Device_Bindless_Resource::SamplersRegular)
                  {
                      layout_binding.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
                  }
                  layout_binding.descriptorCount    = count;
                  layout_binding.stageFlags         = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT | VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT | VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_COMPUTE_BIT;

                  VkDescriptorBindingFlags binding_flags = VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT | VK_DESCRIPTOR_BINDING_VARIABLE_DESCRIPTOR_COUNT_BIT;

                  VkDescriptorSetLayoutBindingFlagsCreateInfo layout_binding_flags = {};
                  layout_binding_flags.sType                                       = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO;
                  layout_binding_flags.bindingCount                                = 1;
                  layout_binding_flags.pBindingFlags                               = &binding_flags;
                  
                  VkDescriptorSetLayoutCreateInfo layout_info = {};
                  layout_info.sType                           = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
                  layout_info.bindingCount                    = 1;
                  layout_info.pBindings                       = &layout_binding;
                  layout_info.flags                           = VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT_EXT;
                  layout_info.pNext                           = &layout_binding_flags;

                  VkDescriptorSetLayout* layout = &layouts[static_cast<uint32_t>(type)];
                  SP_ASSERT_VK(vkCreateDescriptorSetLayout(RHI_Context::device, &layout_info, nullptr, layout));
                  RHI_Device::SetResourceName(static_cast<void*>(*layout), RHI_Resource_Type::DescriptorSetLayout, name);
            }

            void create_set(const RHI_Device_Bindless_Resource type, const uint32_t count, const string& debug_name)
            {
                // allocate descriptor set with actual descriptor count
                VkDescriptorSetVariableDescriptorCountAllocateInfoEXT real_descriptor_count_info = {};
                real_descriptor_count_info.sType                                                 = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_VARIABLE_DESCRIPTOR_COUNT_ALLOCATE_INFO_EXT;
                real_descriptor_count_info.descriptorSetCount                                    = 1;      // one descriptor set
                real_descriptor_count_info.pDescriptorCounts                                     = &count; // actual number of textures being used

                // allocate descriptor set
                VkDescriptorSetAllocateInfo allocation_info = {};
                allocation_info.sType                       = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
                allocation_info.descriptorPool              = descriptors::descriptor_pool;
                allocation_info.descriptorSetCount          = 1;
                allocation_info.pSetLayouts                 = &layouts[static_cast<uint32_t>(type)];
                allocation_info.pNext                       = &real_descriptor_count_info;

                // create
                VkDescriptorSet* descriptor_set = &sets[static_cast<uint32_t>(type)];
                SP_ASSERT_VK(vkAllocateDescriptorSets(RHI_Context::device, &allocation_info, descriptor_set));
                RHI_Device::SetResourceName(static_cast<void*>(*descriptor_set), RHI_Resource_Type::DescriptorSet, debug_name);
            }

            void update(void* data, const uint32_t count, const uint32_t slot, const RHI_Device_Bindless_Resource type, const string& name)
            {
                // deduce binding from slot (HLSL register style)
                uint32_t binding = 0;
                if (type == RHI_Device_Bindless_Resource::MaterialTextures  || type == RHI_Device_Bindless_Resource::MaterialParameters || type == RHI_Device_Bindless_Resource::LightParameters)
                {
                    binding = rhi_shader_register_shift_t + slot;
                }
                else
                {
                    binding = rhi_shader_register_shift_s + slot;
                }

                // on the first run, create layout and set
                if (layouts[static_cast<uint32_t>(type)] == nullptr)
                {
                    create_layout(type, count, binding, name);
                    create_set(type, count, name);
                }
            
                // update
                if (type == RHI_Device_Bindless_Resource::MaterialTextures || type == RHI_Device_Bindless_Resource::SamplersRegular || type == RHI_Device_Bindless_Resource::SamplersComparison)
                {
                    vector<VkDescriptorImageInfo> image_infos(count);
                    if (type == RHI_Device_Bindless_Resource::MaterialTextures)
                    {
                        const auto* textures = static_cast<const array<RHI_Texture*, rhi_max_array_size>*>(data);
            
                        for (uint32_t i = 0; i < count; ++i)
                        {
                            // get texture with fallback to a default texture
                            RHI_Texture* texture   = (*textures)[i];
                            void* resource_default = Renderer::GetStandardTexture(Renderer_StandardTexture::Checkerboard)->GetRhiSrv();
                            void* resource         = (texture && texture->GetRhiSrv()) ? texture->GetRhiSrv() : resource_default;

                            image_infos[i].imageView   = static_cast<VkImageView>(resource);
                            image_infos[i].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                        }
                    }
                    else if (type == RHI_Device_Bindless_Resource::SamplersRegular || type == RHI_Device_Bindless_Resource::SamplersComparison)
                    {
                        const auto* samplers = static_cast<const shared_ptr<RHI_Sampler>*>(data);
            
                        for (uint32_t i = 0; i < count; ++i)
                        {
                            image_infos[i].sampler = static_cast<VkSampler>(samplers[i]->GetRhiResource());
                        }
                    }
            
                    VkWriteDescriptorSet descriptor_write = {};
                    descriptor_write.sType                = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                    descriptor_write.dstSet               = sets[static_cast<uint32_t>(type)];
                    descriptor_write.dstBinding           = binding;
                    descriptor_write.dstArrayElement      = 0; // starting element in the array
                    descriptor_write.descriptorType       = type == RHI_Device_Bindless_Resource::MaterialTextures ? VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE : VK_DESCRIPTOR_TYPE_SAMPLER;
                    descriptor_write.descriptorCount      = count;
                    descriptor_write.pImageInfo           = image_infos.data();
            
                    vkUpdateDescriptorSets(RHI_Context::device, 1, &descriptor_write, 0, nullptr);
                }
                else if (type == RHI_Device_Bindless_Resource::MaterialParameters || type == RHI_Device_Bindless_Resource::LightParameters)
                {
                    RHI_Buffer* buffer = static_cast<RHI_Buffer*>(data);

                    VkDescriptorBufferInfo buffer_info = {};
                    buffer_info.buffer                 = static_cast<VkBuffer>(buffer->GetRhiResource());
                    buffer_info.offset                 = 0;
                    buffer_info.range                  = buffer->GetObjectSize();
            
                    VkWriteDescriptorSet descriptor_write = {};
                    descriptor_write.sType                = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                    descriptor_write.dstSet               = sets[static_cast<uint32_t>(type)];
                    descriptor_write.dstBinding           = binding;
                    descriptor_write.dstArrayElement      = 0; // starting element in the array
                    descriptor_write.descriptorType       = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
                    descriptor_write.descriptorCount      = count;
                    descriptor_write.pBufferInfo          = &buffer_info;
            
                    vkUpdateDescriptorSets(RHI_Context::device, 1, &descriptor_write, 0, nullptr);
                }
            }
        }

        void release()
        {
            sets.clear();
            layouts.clear();
            pipelines.clear();
            descriptor_cache.clear();

            for (uint32_t i = 0; i < static_cast<uint32_t>(bindless::layouts.size()); i++)
            {
                RHI_Device::DeletionQueueAdd(RHI_Resource_Type::DescriptorSetLayout, bindless::layouts[i]);
            }
        }
    }

    namespace device_features
    {
        VkPhysicalDeviceFeatures2 features                          = {};
        VkPhysicalDeviceRobustness2FeaturesEXT features_robustness  = {};
        VkPhysicalDeviceVulkan14Features features_1_4               = {};
        VkPhysicalDeviceVulkan13Features features_1_3               = {};
        VkPhysicalDeviceVulkan12Features features_1_2               = {};
        VkPhysicalDeviceFragmentShadingRateFeaturesKHR features_vrs = {};

        void detect(bool* is_shading_rate_supported)
        {
            // features that will be enabled
            features_vrs.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FRAGMENT_SHADING_RATE_FEATURES_KHR;
            features_vrs.pNext = nullptr;
            
            features_robustness.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ROBUSTNESS_2_FEATURES_EXT;
            features_robustness.pNext = &features_vrs;
            
            features_1_2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
            features_1_2.pNext = &features_robustness;
            
            features_1_3.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
            features_1_3.pNext = &features_1_2;
            
            features_1_4.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_4_FEATURES;
            features_1_4.pNext = &features_1_3;
            
            features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
            features.pNext = &features_1_4;

            // detect which features are supported
            VkPhysicalDeviceFragmentShadingRateFeaturesKHR support_vrs = {};
            support_vrs.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FRAGMENT_SHADING_RATE_FEATURES_KHR;
            
            VkPhysicalDeviceRobustness2FeaturesEXT support_robustness = {};
            support_robustness.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ROBUSTNESS_2_FEATURES_EXT;
            support_robustness.pNext = &support_vrs;
            
            VkPhysicalDeviceVulkan12Features support_1_2 = {};
            support_1_2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
            support_1_2.pNext = &support_robustness;
            
            VkPhysicalDeviceVulkan13Features support_1_3 = {};
            support_1_3.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
            support_1_3.pNext = &support_1_2;
            
            VkPhysicalDeviceVulkan14Features support_1_4 = {};
            support_1_4.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_4_FEATURES;
            support_1_4.pNext = &support_1_3;
            
            VkPhysicalDeviceFeatures2 support = {};
            support.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
            support.pNext = &support_1_4;
            
            vkGetPhysicalDeviceFeatures2(RHI_Context::device_physical, &support);

            // check if certain features are supported and enable them
            {
                // variable shading rate
                *is_shading_rate_supported = support_vrs.attachmentFragmentShadingRate == VK_TRUE;
                if (*is_shading_rate_supported)
                {
                    // Enable this feature conditionally (no assert) as older GPUs like NV 1080 and Radeon RX Vega do not support it.
                    // Support details: https://vulkan.gpuinfo.org/listdevicescoverage.php?platform=windows&extension=VK_KHR_fragment_shading_rate
                    features_vrs.attachmentFragmentShadingRate = VK_TRUE;
                }

                // misc
                {
                    // tessellation
                    SP_ASSERT(support.features.tessellationShader == VK_TRUE);
                    features.features.tessellationShader = VK_TRUE;

                    // depth clamp
                    SP_ASSERT(support.features.depthClamp == VK_TRUE);
                    features.features.depthClamp = VK_TRUE;

                    // anisotropic filtering
                    SP_ASSERT(support.features.samplerAnisotropy == VK_TRUE);
                    features.features.samplerAnisotropy = VK_TRUE;

                    // line and point rendering
                    SP_ASSERT(support.features.fillModeNonSolid == VK_TRUE);
                    features.features.fillModeNonSolid = VK_TRUE;

                    // lines with adjustable thickness
                    SP_ASSERT(support.features.wideLines == VK_TRUE);
                    features.features.wideLines = VK_TRUE;

                    // cubemaps
                    SP_ASSERT(support.features.imageCubeArray == VK_TRUE);
                    features.features.imageCubeArray = VK_TRUE;

                    // pipeline statistics
                    SP_ASSERT(support.features.pipelineStatisticsQuery == VK_TRUE);
                    features.features.pipelineStatisticsQuery = VK_TRUE;
                }

                // quality of life improvements
                {
                    // dynamic render passes and no frame buffer objects
                    SP_ASSERT(support_1_3.dynamicRendering == VK_TRUE);
                    features_1_3.dynamicRendering = VK_TRUE;

                    // better synchronization
                    SP_ASSERT(support_1_3.synchronization2 == VK_TRUE);
                    features_1_3.synchronization2 = VK_TRUE;

                    // timeline semaphores
                    SP_ASSERT(support_1_2.timelineSemaphore == VK_TRUE);
                    features_1_2.timelineSemaphore = VK_TRUE;

                    // timeline semaphore counter
                    SP_ASSERT(support.features.shaderFloat64 == VK_TRUE);
                    features.features.shaderFloat64 = VK_TRUE;
                }

                // descriptors
                {
                    SP_ASSERT(support_1_2.descriptorBindingVariableDescriptorCount == VK_TRUE);
                    features_1_2.descriptorBindingVariableDescriptorCount = VK_TRUE;

                    SP_ASSERT(support_1_2.descriptorBindingVariableDescriptorCount == VK_TRUE);
                    features_1_2.descriptorBindingVariableDescriptorCount = VK_TRUE;

                    SP_ASSERT(support_1_2.descriptorBindingSampledImageUpdateAfterBind == VK_TRUE);
                    features_1_2.descriptorBindingSampledImageUpdateAfterBind = VK_TRUE;

                    SP_ASSERT(support_1_2.descriptorBindingPartiallyBound == VK_TRUE);
                    features_1_2.descriptorBindingPartiallyBound = VK_TRUE;

                    SP_ASSERT(support_1_2.runtimeDescriptorArray == VK_TRUE);
                    features_1_2.runtimeDescriptorArray = VK_TRUE;

                    SP_ASSERT(support_robustness.nullDescriptor == VK_TRUE);
                    features_robustness.nullDescriptor = VK_TRUE;

                    // AMD doesn't support Vulkan 1.4 yet, so we'll wait on it.
                    //SP_ASSERT(support_1_4.pushDescriptor == VK_TRUE);
                    //features_1_4.pushDescriptor = VK_TRUE;
                }

                // fidelity fx
                {
                    // brixelizer gi
                    SP_ASSERT(support_1_2.shaderStorageBufferArrayNonUniformIndexing == VK_TRUE);
                    features_1_2.shaderStorageBufferArrayNonUniformIndexing = VK_TRUE;

                    // spd
                    SP_ASSERT(support_1_2.shaderSubgroupExtendedTypes == VK_TRUE);
                    features_1_2.shaderSubgroupExtendedTypes = VK_TRUE;

                    // float16 - If supported, fsr will opt for it, so don't assert.
                    if (support_1_2.shaderFloat16 == VK_TRUE)
                    {
                        features_1_2.shaderFloat16 = VK_TRUE;
                    }

                    // int16 - If supported, fsr will opt for it, so don't assert.
                    if (support.features.shaderInt16 == VK_TRUE)
                    {
                        features.features.shaderInt16 = VK_TRUE;
                    }

                    // wave64
                    SP_ASSERT(support_1_3.shaderDemoteToHelperInvocation == VK_TRUE);
                    features_1_3.shaderDemoteToHelperInvocation = VK_TRUE;

                    // wave64 - If supported, fsr will opt for it, so don't assert
                    if (support_1_3.subgroupSizeControl == VK_TRUE)
                    {
                        features_1_3.subgroupSizeControl = VK_TRUE;
                    }
                }

                // directx shader compiler spir-v output automatically enables certain capabilities
                {
                    // geometry
                    SP_ASSERT(support.features.geometryShader == VK_TRUE);
                    features.features.geometryShader = VK_TRUE;
                }
            }
        }
    }

    namespace device_physical
    {
        void detect_all()
        {
            uint32_t device_count = 0;
            if (vkEnumeratePhysicalDevices(RHI_Context::instance, &device_count, nullptr) != VK_SUCCESS)
            {
                SP_ERROR_WINDOW("Ensure you're not using incorrect or experimental drivers. Update your graphics drivers and uninstall Vulkan 'Compatibility Packs'.");
            }

            SP_ASSERT_MSG(device_count != 0, "There are no available physical devices");
            
            vector<VkPhysicalDevice> physical_devices(device_count);
            SP_ASSERT_MSG(
                vkEnumeratePhysicalDevices(RHI_Context::instance, &device_count, physical_devices.data()) == VK_SUCCESS,
                "Failed to enumerate physical devices"
            );
            
            // register all available physical devices
            for (const VkPhysicalDevice& device_physical : physical_devices)
            {
                VkPhysicalDeviceProperties device_properties = {};
                vkGetPhysicalDeviceProperties(device_physical, &device_properties);
            
                VkPhysicalDeviceMemoryProperties device_memory_properties = {};
                vkGetPhysicalDeviceMemoryProperties(device_physical, &device_memory_properties);
            
                RHI_PhysicalDevice_Type type = RHI_PhysicalDevice_Type::Max;
                if (device_properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU) type = RHI_PhysicalDevice_Type::Integrated;
                if (device_properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU)   type = RHI_PhysicalDevice_Type::Discrete;
                if (device_properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU)    type = RHI_PhysicalDevice_Type::Virtual;
                if (device_properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_CPU)            type = RHI_PhysicalDevice_Type::Cpu;

                // find the local device memory heap size
                uint64_t vram_size_bytes = 0;
                for (uint32_t i = 0; i < device_memory_properties.memoryHeapCount; i++)
                {
                    if (device_memory_properties.memoryHeaps[i].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT)
                    {
                        vram_size_bytes = device_memory_properties.memoryHeaps[i].size;
                        break;
                    }
                }
                SP_ASSERT(vram_size_bytes > 0);

                RHI_Device::PhysicalDeviceRegister(PhysicalDevice
                (
                    device_properties.apiVersion,          // api version
                    device_properties.driverVersion,       // driver version
                    device_properties.vendorID,            // vendor id
                    type,                                  // type
                    &device_properties.deviceName[0],      // name
                    vram_size_bytes,                       // memory
                    static_cast<void*>(device_physical)    // data
                ));
            }
        }

        void select_primary()
        {
            // go through all the devices (sorted from best to worst based on their properties)
            for (uint32_t device_index = 0; device_index < RHI_Device::PhysicalDeviceGet().size(); device_index++)
            {
                VkPhysicalDevice device = static_cast<VkPhysicalDevice>(RHI_Device::PhysicalDeviceGet()[device_index].GetData());

                // get the first device which supports graphics, compute and transfer queues
                if (queues::get_queue_family_indices(device))
                {
                    RHI_Device::PhysicalDeviceSetPrimary(device_index);
                    RHI_Context::device_physical = device;
                    break;
                }
            }
        }
    }

    void RHI_Device::Initialize()
    {
        // instance
        {
            VkInstanceCreateInfo info_instance      = {};
            info_instance.sType                     = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
            VkApplicationInfo app_info              = create_application_info();
            info_instance.pApplicationInfo          = &app_info;

            // extensions
            vector<const char*> extensions_instance = extensions::get_extensions_instance();
            info_instance.enabledExtensionCount     = static_cast<uint32_t>(extensions_instance.size());
            info_instance.ppEnabledExtensionNames   = extensions_instance.data();
            info_instance.enabledLayerCount         = Debugging::IsValidationLayerEnabled() ? 1 : 0;
            info_instance.ppEnabledLayerNames       = &validation_layer::name;

            // settings
            VkLayerSettingsCreateInfoEXT info_settings = {};
            info_settings.sType                        = VK_STRUCTURE_TYPE_LAYER_SETTINGS_CREATE_INFO_EXT;
            info_instance.pNext                        = &info_settings;
            vector<VkLayerSettingEXT> settings;
            if (Debugging::IsValidationLayerEnabled())
            { 
                settings = validation_layer::get_settings();
            }
            info_settings.pSettings    = settings.data();
            info_settings.settingCount = static_cast<uint32_t>(settings.size());

            // create the vulkan instance
            SP_ASSERT_VK(vkCreateInstance(&info_instance, nullptr, &RHI_Context::instance));

            functions::get_pointers();
            validation_layer::logging::enable();
        }

        // device
        {
            // detect and select primary
            device_physical::detect_all();
            device_physical::select_primary();

            // queues
            vector<VkDeviceQueueCreateInfo> queue_create_infos;
            {
                queues::detect_queue_family_indices(RHI_Context::device_physical);
                vector<uint32_t> queue_family_indices =
                {
                    queues::index_graphics,
                    queues::index_compute,
                    queues::index_copy
                };

                float queue_priority = 1.0f;
                for (const uint32_t& queue_family_index : queue_family_indices)
                {
                    VkDeviceQueueCreateInfo queue_create_info = {};
                    queue_create_info.sType                   = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
                    queue_create_info.flags                   = 0;
                    queue_create_info.queueFamilyIndex        = queue_family_index;
                    queue_create_info.queueCount              = 1;
                    queue_create_info.pQueuePriorities        = &queue_priority;
                    queue_create_infos.push_back(queue_create_info);
                }
            }

            // properties
            {
                VkPhysicalDeviceFragmentShadingRatePropertiesKHR shading_rate_properties = {};
                shading_rate_properties.sType                                            = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FRAGMENT_SHADING_RATE_PROPERTIES_KHR;

                VkPhysicalDeviceVulkan13Properties device_properties_1_3 = {};
                device_properties_1_3.sType                              = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_PROPERTIES;
                device_properties_1_3.pNext                              = &shading_rate_properties;

                VkPhysicalDeviceProperties2 properties_device = {};
                properties_device.sType                       = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
                properties_device.pNext                       = &device_properties_1_3;

                vkGetPhysicalDeviceProperties2(static_cast<VkPhysicalDevice>(RHI_Context::device_physical), &properties_device);

                // save some properties
                m_timestamp_period                     = properties_device.properties.limits.timestampPeriod;
                m_min_uniform_buffer_offset_alignment  = properties_device.properties.limits.minUniformBufferOffsetAlignment;
                m_min_storage_buffer_offset_alignment  = properties_device.properties.limits.minStorageBufferOffsetAlignment;
                m_max_texture_1d_dimension             = properties_device.properties.limits.maxImageDimension1D;
                m_max_texture_2d_dimension             = properties_device.properties.limits.maxImageDimension2D;
                m_max_texture_3d_dimension             = properties_device.properties.limits.maxImageDimension3D;
                m_max_texture_cube_dimension           = properties_device.properties.limits.maxImageDimensionCube;
                m_max_texture_array_layers             = properties_device.properties.limits.maxImageArrayLayers;
                m_max_push_constant_size               = properties_device.properties.limits.maxPushConstantsSize;
                m_max_shading_rate_texel_size_x        = shading_rate_properties.maxFragmentShadingRateAttachmentTexelSize.width;
                m_max_shading_rate_texel_size_y        = shading_rate_properties.maxFragmentShadingRateAttachmentTexelSize.height;
                m_optimal_buffer_copy_offset_alignment = properties_device.properties.limits.optimalBufferCopyOffsetAlignment;

                // disable profiler if timestamps are not supported
                if (Debugging::IsGpuTimingEnabled())
                {
                    SP_ASSERT_MSG(properties_device.properties.limits.timestampComputeAndGraphics, "Device doesn't support timestamps");
                }
            }
  
            device_features::detect(&m_is_shading_rate_supported);

            // create
            {
                VkDeviceCreateInfo create_info           = {};
                create_info.sType                        = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
                create_info.queueCreateInfoCount         = static_cast<uint32_t>(queue_create_infos.size());
                create_info.pQueueCreateInfos            = queue_create_infos.data();
                create_info.pNext                        = &device_features::features;
                vector<const char*> extensions_supported = extensions::get_extensions_device();
                create_info.enabledExtensionCount        = static_cast<uint32_t>(extensions_supported.size());
                create_info.ppEnabledExtensionNames      = extensions_supported.data();

                SP_ASSERT_VK(vkCreateDevice(RHI_Context::device_physical, &create_info, nullptr, &RHI_Context::device));
                SP_LOG_INFO("Vulkan %s", version::to_string(version::used).c_str());
            }
        }

        // create queues
        {
            vkGetDeviceQueue(RHI_Context::device, queues::index_graphics, 0, reinterpret_cast<VkQueue*>(&queues::graphics));
            SetResourceName(queues::graphics, RHI_Resource_Type::Queue, "graphics");

            vkGetDeviceQueue(RHI_Context::device, queues::index_compute, 0, reinterpret_cast<VkQueue*>(&queues::compute));
            SetResourceName(queues::compute, RHI_Resource_Type::Queue, "compute");

            vkGetDeviceQueue(RHI_Context::device, queues::index_copy, 0, reinterpret_cast<VkQueue*>(&queues::copy));
            SetResourceName(queues::copy, RHI_Resource_Type::Queue, "copy");

            queues::regular[static_cast<uint32_t>(RHI_Queue_Type::Graphics)] = make_shared<RHI_Queue>(RHI_Queue_Type::Graphics, "graphics");
            queues::regular[static_cast<uint32_t>(RHI_Queue_Type::Compute)]  = make_shared<RHI_Queue>(RHI_Queue_Type::Compute,  "compute");
            queues::regular[static_cast<uint32_t>(RHI_Queue_Type::Copy)]     = make_shared<RHI_Queue>(RHI_Queue_Type::Copy,     "copy");

            queues::immediate[static_cast<uint32_t>(RHI_Queue_Type::Graphics)] = make_shared<RHI_Queue>(RHI_Queue_Type::Graphics, "graphics");
            queues::immediate[static_cast<uint32_t>(RHI_Queue_Type::Compute)]  = make_shared<RHI_Queue>(RHI_Queue_Type::Compute,  "compute");
            queues::immediate[static_cast<uint32_t>(RHI_Queue_Type::Copy)]     = make_shared<RHI_Queue>(RHI_Queue_Type::Copy,     "copy");
        }

        vulkan_memory_allocator::initialize();
        CreateDescriptorPool();

        // gpu dependent actions
        {
            if (Debugging::IsBreadcrumbsEnabled())
            { 
                SP_ASSERT_MSG(GetPrimaryPhysicalDevice()->IsAmd(), "Breadcrumbs are only supported on AMD GPUs");
            }

            if (RHI_Device::GetPrimaryPhysicalDevice()->IsBelowMinimumRequirments())
            {
                SP_WARNING_WINDOW("The GPU does not meet the minimum requirements for running the engine. The engine may not function correctly.");
            }
        }

        // register the vulkan sdk version, which can be higher than the version we are using which is driver dependent
        string version_Sdlk = to_string(VK_VERSION_MAJOR(VK_HEADER_VERSION_COMPLETE)) + "." + to_string(VK_VERSION_MINOR(VK_HEADER_VERSION_COMPLETE)) + "." + to_string(VK_VERSION_PATCH(VK_HEADER_VERSION_COMPLETE));
        Settings::RegisterThirdPartyLib("Vulkan", version_Sdlk, "https://vulkan.lunarg.com/");
    }

    void RHI_Device::Tick(const uint64_t frame_count)
    {
        // https://gpuopen-librariesandsdks.github.io/VulkanMemoryAllocator/html/staying_within_budget.html
        // Make sure to call vmaSetCurrentFrameIndex() every frame.
        // Budget is queried from Vulkan inside of it to avoid overhead of querying it with every allocation.
        vmaSetCurrentFrameIndex(vulkan_memory_allocator::allocator, static_cast<uint32_t>(frame_count));

        // queues
        for (uint32_t i = 0; i < static_cast<uint32_t>(queues::regular.size()); i++)
        {
            queues::regular[i]->NextCommandList();
        }
    }

    void RHI_Device::Destroy()
    {
        SP_ASSERT(queues::graphics != nullptr);

        // destroy queues
        QueueWaitAll();
        queues::destroy();

        // descriptor pool
        vkDestroyDescriptorPool(RHI_Context::device, descriptors::descriptor_pool, nullptr);
        descriptors::descriptor_pool = nullptr;

        // debug messenger
        if (Debugging::IsValidationLayerEnabled())
        {
            validation_layer::logging::shutdown(RHI_Context::instance);
        }

        // descriptors
        descriptors::release();

        // the destructor of all the resources enqueues it's vk buffer memory for de-allocation
        // this is where we actually go through them and de-allocate them
        RHI_Device::DeletionQueueParse();

        // destroy the allocator itself and assert if any allocations are left
        vulkan_memory_allocator::destroy();

        // device and instance
        vkDestroyDevice(RHI_Context::device, nullptr);
        vkDestroyInstance(RHI_Context::instance, nullptr);
    }

    // queues

    uint32_t RHI_Device::GetQueueIndex(const RHI_Queue_Type type)
    {
        if (type == RHI_Queue_Type::Graphics)
        {
            return queues::index_graphics;
        }
        else if (type == RHI_Queue_Type::Copy)
        {
            return queues::index_copy;
        }
        else if (type == RHI_Queue_Type::Compute)
        {
            return queues::index_compute;
        }

        return 0;
    }

    RHI_Queue* RHI_Device::GetQueue(const RHI_Queue_Type type)
    {
        if (type == RHI_Queue_Type::Graphics)
            return queues::regular[static_cast<uint32_t>(RHI_Queue_Type::Graphics)].get();

        if (type == RHI_Queue_Type::Compute)
            return queues::regular[static_cast<uint32_t>(RHI_Queue_Type::Compute)].get();

        return nullptr;
    }

    void* RHI_Device::GetQueueRhiResource(const RHI_Queue_Type type)
    {
        if (type == RHI_Queue_Type::Graphics)
            return queues::graphics;

        if (type == RHI_Queue_Type::Copy)
            return queues::copy;

        if (type == RHI_Queue_Type::Compute)
            return queues::compute;

        return nullptr;
    }

    void RHI_Device::QueueWaitAll()
    {
        for (uint32_t i = 0; i < 2; i++)
        {
            queues::regular[i]->Wait();
        }
    }

    // deletion queue

    void RHI_Device::DeletionQueueAdd(const RHI_Resource_Type resource_type, void* resource)
    {
        if (!resource)
            return;

        lock_guard<mutex> guard(mutex_deletion_queue);
        deletion_queue[resource_type].emplace_back(resource);
    }

    void RHI_Device::DeletionQueueParse()
    {
        lock_guard<mutex> guard(mutex_deletion_queue);
       
        for (auto& it : deletion_queue)
        {
            RHI_Resource_Type resource_type = it.first;

            for (uint32_t i = 0; i < static_cast<uint32_t>(it.second.size()); i++)
            {
                void* resource = it.second[i];

                switch (resource_type)
                {
                    case RHI_Resource_Type::Texture:             MemoryTextureDestroy(resource);                                                                           break;
                    case RHI_Resource_Type::TextureView:         vkDestroyImageView(RHI_Context::device, static_cast<VkImageView>(resource), nullptr);                     break;
                    case RHI_Resource_Type::Sampler:             vkDestroySampler(RHI_Context::device, reinterpret_cast<VkSampler>(resource), nullptr);                    break;
                    case RHI_Resource_Type::Buffer:              MemoryBufferDestroy(resource);                                                                            break;
                    case RHI_Resource_Type::Shader:              vkDestroyShaderModule(RHI_Context::device, static_cast<VkShaderModule>(resource), nullptr);               break;
                    case RHI_Resource_Type::Semaphore:           vkDestroySemaphore(RHI_Context::device, static_cast<VkSemaphore>(resource), nullptr);                     break;
                    case RHI_Resource_Type::Fence:               vkDestroyFence(RHI_Context::device, static_cast<VkFence>(resource), nullptr);                             break;
                    case RHI_Resource_Type::DescriptorSetLayout: vkDestroyDescriptorSetLayout(RHI_Context::device, static_cast<VkDescriptorSetLayout>(resource), nullptr); break;
                    case RHI_Resource_Type::QueryPool:           vkDestroyQueryPool(RHI_Context::device, static_cast<VkQueryPool>(resource), nullptr);                     break;
                    case RHI_Resource_Type::Pipeline:            vkDestroyPipeline(RHI_Context::device, static_cast<VkPipeline>(resource), nullptr);                       break;
                    case RHI_Resource_Type::PipelineLayout:      vkDestroyPipelineLayout(RHI_Context::device, static_cast<VkPipelineLayout>(resource), nullptr);           break;
                    default:                                     SP_ASSERT_MSG(false, "Unknown resource");                                                                 break;
                }

                // delete descriptor sets which are now invalid (because they are referring to a deleted resource)
                if (resource_type == RHI_Resource_Type::TextureView || resource_type == RHI_Resource_Type::Buffer || resource_type == RHI_Resource_Type::Sampler)
                {
                    for (auto it = descriptors::sets.begin(); it != descriptors::sets.end();)
                    {
                        if (it->second.IsReferingToResource(resource))
                        {
                            it = descriptors::sets.erase(it);
                            // ideally the descriptor set pool is not oblivious to the fact that we don't use this set anymore
                            // maybe after a certain number of deletions we reset the entire pool to free memory
                        }
                        else
                        {
                            ++it;
                        }
                    }
                }
            }
        }

        deletion_queue.clear();
    }

    bool RHI_Device::DeletionQueueNeedsToParse()
    {
        const  uint32_t frames_selflife            = 10;
        static uint32_t frames_equilibrium         = 0;
        static uint32_t objects_to_delete_previous = 0;
    
        // count deletions in the queue
        uint32_t objects_to_delete = 0;
        for (uint32_t i = 0; i < static_cast<uint32_t>(RHI_Resource_Type::Max); i++)
        {
            objects_to_delete += static_cast<uint32_t>(deletion_queue[static_cast<RHI_Resource_Type>(i)].size());
        }
    
        // check if the number of objects to delete has remained unchanged
        if (objects_to_delete > 0 && objects_to_delete == objects_to_delete_previous)
        {
            frames_equilibrium++;

            // if it’s been stable for frame_selflife frames, reset counter and delete
            if (frames_equilibrium >= frames_selflife)
            {
                frames_equilibrium = 0;
                return true;
            }
        }
        else
        {
            // reset counter if the count changed or if nothing is in the queue
            frames_equilibrium = 0;
        }
    
        // update the previous object count to the current count
        objects_to_delete_previous = objects_to_delete;
    
        return false;
    }

    // descriptors

    void RHI_Device::CreateDescriptorPool()
    {
        static array<VkDescriptorPoolSize, 5> pool_sizes =
        {
            VkDescriptorPoolSize{ VK_DESCRIPTOR_TYPE_SAMPLER,                rhi_max_array_size * rhi_max_descriptor_set_count },
            VkDescriptorPoolSize{ VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,          rhi_max_array_size * rhi_max_descriptor_set_count },
            VkDescriptorPoolSize{ VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,          rhi_max_array_size * rhi_max_descriptor_set_count },
            VkDescriptorPoolSize{ VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, rhi_max_array_size * rhi_max_descriptor_set_count },
            VkDescriptorPoolSize{ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, rhi_max_array_size * rhi_max_descriptor_set_count }
        };

        // describe
        VkDescriptorPoolCreateInfo pool_create_info = {};
        pool_create_info.sType                      = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        pool_create_info.flags                      = VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT_EXT;
        pool_create_info.poolSizeCount              = static_cast<uint32_t>(pool_sizes.size());
        pool_create_info.pPoolSizes                 = pool_sizes.data();
        pool_create_info.maxSets                    = rhi_max_descriptor_set_count;

        // create
        SP_ASSERT(descriptors::descriptor_pool == nullptr);
        SP_ASSERT_VK(vkCreateDescriptorPool(RHI_Context::device, &pool_create_info, nullptr, &descriptors::descriptor_pool));

        Profiler::m_descriptor_set_count = 0;
    }

    void RHI_Device::AllocateDescriptorSet(void*& resource, RHI_DescriptorSetLayout* descriptor_set_layout, const vector<RHI_Descriptor>& descriptors_)
    {
        // describe
        array<void*, 1> descriptor_set_layouts    = { descriptor_set_layout->GetRhiResource() };
        VkDescriptorSetAllocateInfo allocate_info = {};
        allocate_info.sType                       = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        allocate_info.descriptorPool              = static_cast<VkDescriptorPool>(descriptors::descriptor_pool);
        allocate_info.descriptorSetCount          = 1;
        allocate_info.pSetLayouts                 = reinterpret_cast<VkDescriptorSetLayout*>(descriptor_set_layouts.data());

        // allocate
        SP_ASSERT(resource == nullptr);
        SP_ASSERT_VK(vkAllocateDescriptorSets(RHI_Context::device, &allocate_info, reinterpret_cast<VkDescriptorSet*>(&resource)));

        // track allocations
        descriptors::allocated_descriptor_sets++;
        Profiler::m_descriptor_set_count++;
    }

    void* RHI_Device::GetDescriptorSet(const RHI_Device_Bindless_Resource resource_type)
    {
        return static_cast<void*>(descriptors::bindless::sets[static_cast<uint32_t>(resource_type)]);
    }

    void* RHI_Device::GetDescriptorSetLayout(const RHI_Device_Bindless_Resource resource_type)
    {
        return static_cast<void*>(descriptors::bindless::layouts[static_cast<uint32_t>(resource_type)]);
    }

    unordered_map<uint64_t, RHI_DescriptorSet>& RHI_Device::GetDescriptorSets()
    {
        return descriptors::sets;
    }

    uint32_t RHI_Device::GetDescriptorType(const RHI_Descriptor& descriptor)
    {
        if (descriptor.type == RHI_Descriptor_Type::Texture)
            return VkDescriptorType::VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;

        if (descriptor.type == RHI_Descriptor_Type::TextureStorage)
            return VkDescriptorType::VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;

        if (descriptor.type == RHI_Descriptor_Type::StructuredBuffer)
            return VkDescriptorType::VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC;

        if (descriptor.type == RHI_Descriptor_Type::ConstantBuffer)
            return VkDescriptorType::VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;

        SP_ASSERT_MSG(false, "Unhandled descriptor type");
        return VkDescriptorType::VK_DESCRIPTOR_TYPE_MAX_ENUM;
    }

    void RHI_Device::UpdateBindlessResources(
        array<RHI_Texture*, rhi_max_array_size>* material_textures,
        RHI_Buffer* material_parameters,
        RHI_Buffer* light_parameters,
        const std::array<std::shared_ptr<RHI_Sampler>, static_cast<uint32_t>(Renderer_Sampler::Max)>* samplers
    )
    {
        if (samplers)
        {
            // comparison
            {
                vector<shared_ptr<RHI_Sampler>> data =
                {
                    (*samplers)[0], // comparison
                };

                descriptors::bindless::update(&data[0], static_cast<uint32_t>(data.size()), 0, RHI_Device_Bindless_Resource::SamplersComparison, "samplers_comparison");
            }

            // regular
            {
                vector<shared_ptr<RHI_Sampler>> data =
                {
                    (*samplers)[1], // point_clamp_edge
                    (*samplers)[2], // point_clamp_border
                    (*samplers)[3], // point_wrap
                    (*samplers)[4], // bilinear_clamp_edge
                    (*samplers)[5], // bilinear_clamp_border
                    (*samplers)[6], // bilinear_wrap
                    (*samplers)[7], // trilinear_clamp
                    (*samplers)[8]  // anisotropic_wrap
                };

                descriptors::bindless::update(&data[0], static_cast<uint32_t>(data.size()), 1, RHI_Device_Bindless_Resource::SamplersRegular, "samplers_regular");
            }
        }

        // lights
        if (light_parameters)
        {
            uint32_t binding_slot = static_cast<uint32_t>(Renderer_BindingsSrv::light_parameters);
            descriptors::bindless::update(light_parameters, 1, binding_slot, RHI_Device_Bindless_Resource::LightParameters, "light_parameters");
        }

        // textures
        if (material_textures || material_parameters)
        {
            uint32_t binding_slot = static_cast<uint32_t>(Renderer_BindingsSrv::material_textures);
            descriptors::bindless::update(&material_textures[0], rhi_max_array_size, binding_slot, RHI_Device_Bindless_Resource::MaterialTextures, "material_textures");

            binding_slot = static_cast<uint32_t>(Renderer_BindingsSrv::material_parameters);
            descriptors::bindless::update(material_parameters, 1, binding_slot, RHI_Device_Bindless_Resource::MaterialParameters, "material_parameters");
        }
    }

    // pipelines

    void RHI_Device::GetOrCreatePipeline(RHI_PipelineState& pso, RHI_Pipeline*& pipeline, RHI_DescriptorSetLayout*& descriptor_set_layout)
    {
        pso.Prepare();

        lock_guard<mutex> lock(descriptors::descriptor_pipeline_mutex);

        descriptor_set_layout = descriptors::get_or_create_descriptor_set_layout(pso).get();

        // if no pipeline exists, create one
        uint64_t hash = pso.GetHash();
        auto it = descriptors::pipelines.find(hash);
        if (it == descriptors::pipelines.end())
        {
            // create a new pipeline
            it = descriptors::pipelines.emplace(make_pair(hash, make_shared<RHI_Pipeline>(pso, descriptor_set_layout))).first;
        }

        pipeline = it->second.get();
    }

    uint32_t RHI_Device::GetPipelineCount()
    {
        return static_cast<uint32_t>(descriptors::pipelines.size());
    }

    // memory

    void* RHI_Device::MemoryGetMappedDataFromBuffer(void* resource)
    {
        vulkan_memory_allocator::AllocationData* allocation_data = vulkan_memory_allocator::get_allocation_from_resource(resource);
        if (allocation_data->allocation)
            return allocation_data->allocation->GetMappedData();

        return nullptr;
    }

    void RHI_Device::MemoryBufferCreate(void*& resource, const uint64_t size, uint32_t flags_usage, uint32_t flags_memory, const void* data_initial, const char* name)
    {
        // buffer info
        VkBufferCreateInfo buffer_create_info = {};
        buffer_create_info.sType              = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        buffer_create_info.size               = size;
        buffer_create_info.usage              = flags_usage;
        buffer_create_info.sharingMode        = VK_SHARING_MODE_EXCLUSIVE;

        // allocation info
        VmaAllocationCreateInfo allocation_create_info = {};
        allocation_create_info.usage                   = VMA_MEMORY_USAGE_AUTO;
        allocation_create_info.flags                   = 0;            // flags vma
        allocation_create_info.requiredFlags           = flags_memory; // flags vulkan

        bool is_mappable = (flags_memory & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) != 0;
        if (is_mappable)
        {
            allocation_create_info.flags |= VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;
            allocation_create_info.flags |= VMA_ALLOCATION_CREATE_MAPPED_BIT; // mappable
        }

        // create the buffer
        VmaAllocation allocation = nullptr;
        VmaAllocationInfo allocation_info;
        SP_ASSERT_VK(vmaCreateBuffer(
            vulkan_memory_allocator::allocator,
                &buffer_create_info,
                &allocation_create_info,
                reinterpret_cast<VkBuffer*>(&resource),
                &allocation,
                &allocation_info)
        );

        // if a pointer to the buffer data has been passed, map the buffer and copy over the data
        if (data_initial)
        {
            SP_ASSERT_MSG(is_mappable, "Mapping initial data requires the buffer to be created with a VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT memory flag");

            // Memory in Vulkan doesn't need to be unmapped before using it on GPU, but unless a
            // memory type has VK_MEMORY_PROPERTY_HOST_COHERENT_BIT flag set, you need to manually
            // invalidate the cache before reading a mapped pointer and flush cache after writing to
            // it. Map/unmap operations don't do that automatically.

            void* mapped_data = nullptr;
            SP_ASSERT_VK(vmaMapMemory(vulkan_memory_allocator::allocator, allocation, &mapped_data));
            memcpy(mapped_data, data_initial, size);
            SP_ASSERT_VK(vmaFlushAllocation(vulkan_memory_allocator::allocator, allocation, 0, size));
            vmaUnmapMemory(vulkan_memory_allocator::allocator, allocation);
        }

        vulkan_memory_allocator::save_allocation(resource, name, allocation);
    }

    void RHI_Device::MemoryBufferDestroy(void*& resource)
    {
        lock_guard<mutex> lock(vulkan_memory_allocator::mutex_allocator);

        vulkan_memory_allocator::AllocationData* allocation_data = vulkan_memory_allocator::get_allocation_from_resource(resource);
        if (allocation_data->allocation)
        {
            vmaDestroyBuffer(vulkan_memory_allocator::allocator, static_cast<VkBuffer>(resource), allocation_data->allocation);
            vulkan_memory_allocator::destroy_allocation(resource);
        }
    }

    void RHI_Device::MemoryTextureCreate(RHI_Texture* texture)
    {
        // external memory (if needed)
        VkExternalMemoryImageCreateInfo external_memory_image_create_info = {};
        external_memory_image_create_info.sType       = VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO;
        #if defined(_WIN32)
        external_memory_image_create_info.handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_WIN32_BIT_KHR;
        #else
        external_memory_image_create_info.handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT_KHR;
        #endif

        // describe image
        VkImageCreateInfo create_info_image = {};
        create_info_image.sType             = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        create_info_image.pNext             = texture->HasExternalMemory() ? &external_memory_image_create_info : nullptr;
        create_info_image.imageType         = texture->GetType() == RHI_Texture_Type::Type3D ? VK_IMAGE_TYPE_3D : VK_IMAGE_TYPE_2D;
        create_info_image.flags             = texture->GetType() == RHI_Texture_Type::TypeCube ? VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT : 0;
        create_info_image.usage             = get_image_usage_flags(texture);
        create_info_image.extent.width      = texture->GetWidth();
        create_info_image.extent.height     = texture->GetHeight();
        create_info_image.extent.depth      = texture->GetType() == RHI_Texture_Type::Type3D ? texture->GetDepth() : 1;
        create_info_image.mipLevels         = texture->GetMipCount();
        create_info_image.arrayLayers       = texture->GetType() == RHI_Texture_Type::Type3D ? 1 : texture->GetDepth();
        create_info_image.format            = vulkan_format[rhi_format_to_index(texture->GetFormat())];
        create_info_image.tiling            = get_format_tiling(texture);
        create_info_image.initialLayout     = vulkan_image_layout[static_cast<uint8_t>(texture->GetLayout(0))];
        create_info_image.samples           = VK_SAMPLE_COUNT_1_BIT;
        create_info_image.sharingMode       = VK_SHARING_MODE_EXCLUSIVE;

        // check physical device format support
        {
            VkPhysicalDeviceImageFormatInfo2 info = {};
            info.sType                            = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGE_FORMAT_INFO_2;
            info.format                           = create_info_image.format;
            info.type                             = create_info_image.imageType;
            info.tiling                           = create_info_image.tiling;
            info.usage                            = create_info_image.usage;
            info.flags                            = create_info_image.flags;

            VkImageFormatProperties2 properties = {};
            properties.sType                    = VK_STRUCTURE_TYPE_IMAGE_FORMAT_PROPERTIES_2;

            VkResult result = vkGetPhysicalDeviceImageFormatProperties2(RHI_Context::device_physical, &info, &properties);
            SP_ASSERT_MSG(result != VK_ERROR_FORMAT_NOT_SUPPORTED, "The GPU doesn't support this image format with the specified properties");
        }

        // allocate
        VmaAllocationInfo allocation_info;
        VmaAllocation allocation;
        {
            VmaAllocator allocator                          = texture->HasExternalMemory() ? vulkan_memory_allocator::allocator_external : vulkan_memory_allocator::allocator;
            VmaAllocationCreateInfo create_info_allocation  = {};
            create_info_allocation.usage                    = VMA_MEMORY_USAGE_AUTO;
            create_info_allocation.flags                    = (texture->GetFlags() & RHI_Texture_Mappable) ? VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT : 0;
            create_info_allocation.flags                   |= texture->HasExternalMemory() ? VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT : 0;

            void*& resource = texture->GetRhiResource();
            VkResult result = vmaCreateImage
            (
                allocator,
                &create_info_image,
                &create_info_allocation,
                reinterpret_cast<VkImage*>(&resource),
                &allocation,
                &allocation_info
            );

            if (result == VK_ERROR_OUT_OF_DEVICE_MEMORY || result == VK_ERROR_OUT_OF_HOST_MEMORY)
            {
                SP_ERROR_WINDOW
                (
                    "Failed to allocate texture due to insufficient memory. "
                    "This likely occurred while loading a large scene or too many materials/textures."
                );
            }

            SP_ASSERT_VK(result);

            // set allocation name and data
            //vmaSetAllocationUserData(allocator, allocation, resource);
            vmaSetAllocationName(allocator, allocation, texture->GetObjectName().c_str());
            RHI_Device::SetResourceName(allocation->GetMemory(), RHI_Resource_Type::DeviceMemory, texture->GetObjectName().c_str());
        }

        // get mapped data pointer
        if (texture->GetFlags() & RHI_Texture_Mappable)
        {
            void*& mapped_data = texture->GetMappedData();
            mapped_data = allocation_info.pMappedData;
        }

        // get external memory handle
        if (texture->HasExternalMemory())
        {
            #if defined(_WIN32)
            VkMemoryGetWin32HandleInfoKHR get_handle_info = {};
            get_handle_info.sType                         = VK_STRUCTURE_TYPE_MEMORY_GET_WIN32_HANDLE_INFO_KHR;
            get_handle_info.memory                        = allocation_info.deviceMemory;
            get_handle_info.handleType                    = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_WIN32_BIT_KHR;

            HANDLE win32_handle;
            static PFN_vkGetMemoryWin32HandleKHR vkGetMemoryWin32HandleKHR = (PFN_vkGetMemoryWin32HandleKHR)vkGetDeviceProcAddr(RHI_Context::device, "vkGetMemoryWin32HandleKHR");
            SP_ASSERT_VK(vkGetMemoryWin32HandleKHR(RHI_Context::device, &get_handle_info, &win32_handle));

            texture->SetExternalMemoryHandle(static_cast<void*>(win32_handle));
            #else
            SP_LOG_ERROR("Not implemented, you need to use the Linux equivalent via VK_KHR_external_memory_fd");
            #endif
        }

        vulkan_memory_allocator::save_allocation(texture->GetRhiResource(), texture->HasExternalMemory(), allocation);
    }

    void RHI_Device::MemoryTextureDestroy(void*& resource)
    {
        lock_guard<mutex> lock(vulkan_memory_allocator::mutex_allocator);

        vulkan_memory_allocator::AllocationData* allocation_data = vulkan_memory_allocator::get_allocation_from_resource(resource);
        if (allocation_data->allocation)
        {
            VmaAllocator allocator = allocation_data->external_memory ? vulkan_memory_allocator::allocator_external : vulkan_memory_allocator::allocator;
            vmaDestroyImage(allocator, static_cast<VkImage>(resource), allocation_data->allocation);
            vulkan_memory_allocator::destroy_allocation(resource);
        }
    }

    void RHI_Device::MemoryMap(void* resource, void*& mapped_data)
    {
        lock_guard<mutex> lock(vulkan_memory_allocator::mutex_allocator);

        vulkan_memory_allocator::AllocationData* allocation_data = vulkan_memory_allocator::get_allocation_from_resource(resource);
        if (allocation_data->allocation)
        {
            SP_ASSERT_VK(vmaMapMemory(vulkan_memory_allocator::allocator, allocation_data->allocation, reinterpret_cast<void**>(&mapped_data)));
        }
    }

    void RHI_Device::MemoryUnmap(void* resource)
    {
        lock_guard<mutex> lock(vulkan_memory_allocator::mutex_allocator);

        vulkan_memory_allocator::AllocationData* allocation_data = vulkan_memory_allocator::get_allocation_from_resource(resource);
        if (allocation_data->allocation)
        {
            vmaUnmapMemory(vulkan_memory_allocator::allocator, allocation_data->allocation);
        }
    }

    uint32_t RHI_Device::MemoryGetUsageMb()
    {
        VkDeviceSize bytes = 0;

        // Get the physical device memory properties
        VkPhysicalDeviceMemoryProperties memory_properties;
        vkGetPhysicalDeviceMemoryProperties(static_cast<VkPhysicalDevice>(RHI_Context::device_physical), &memory_properties);

        // Get the memory usage
        VmaBudget budgets[VK_MAX_MEMORY_HEAPS];
        vmaGetHeapBudgets(vulkan_memory_allocator::allocator, budgets);
        for (uint32_t i = 0; i < VK_MAX_MEMORY_HEAPS; i++)
        {
            // Only consider device local heaps
            if (memory_properties.memoryHeaps[i].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT)
            {
                if (budgets[i].budget < 1ull << 60)
                {
                    bytes += budgets[i].usage;
                }
            }
        }

        return static_cast<uint32_t>(bytes / 1024 / 1024);
    }

    uint32_t RHI_Device::MemoryGetBudgetMb()
    {
        VkDeviceSize bytes = 0;

        // Get the physical device memory properties
        VkPhysicalDeviceMemoryProperties memory_properties;
        vkGetPhysicalDeviceMemoryProperties(static_cast<VkPhysicalDevice>(RHI_Context::device_physical), &memory_properties);

        // Get available memory
        VmaBudget budgets[VK_MAX_MEMORY_HEAPS];
        vmaGetHeapBudgets(vulkan_memory_allocator::allocator, budgets);
        for (uint32_t i = 0; i < VK_MAX_MEMORY_HEAPS; i++)
        {
            // Only consider device local heaps
            if (memory_properties.memoryHeaps[i].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT)
            {
                if (budgets[i].budget < 1ull << 60)
                {
                    bytes += budgets[i].budget;
                }
            }
        }

        return static_cast<uint32_t>(bytes / 1024 / 1024);
    }

    // immediate command list

    RHI_CommandList* RHI_Device::CmdImmediateBegin(const RHI_Queue_Type queue_type)
    {
        // wait until it's safe to proceed
        unique_lock<mutex> lock(queues::mutex_immediate_execution);
        queues::condition_variable_immediate_execution.wait(lock, [] { return !queues::is_immediate_executing; });
        queues::is_immediate_executing = true;

        // get command pool
        queues::queue = queues::immediate[static_cast<uint32_t>(queue_type)].get();
        queues::queue->NextCommandList();
        queues::queue->GetCommandList()->Begin(queues::queue, true);

        return queues::queue->GetCommandList();
    }

    void RHI_Device::CmdImmediateSubmit(RHI_CommandList* cmd_list)
    {
        cmd_list->Submit(queues::queue, 0);
        cmd_list->WaitForExecution();

        // signal that it's safe to proceed with the next ImmediateBegin()
        queues::is_immediate_executing = false;
        queues::condition_variable_immediate_execution.notify_one();
    }

    // markers

    void RHI_Device::MarkerBegin(RHI_CommandList* cmd_list, const char* name, const math::Vector4& color)
    {
        VkDebugUtilsLabelEXT label = {};
        label.sType                = VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT;
        label.pNext                = nullptr;
        label.pLabelName           = name;
        label.color[0]             = color.x;
        label.color[1]             = color.y;
        label.color[2]             = color.z;
        label.color[3]             = color.w;

        functions::marker_begin(static_cast<VkCommandBuffer>(cmd_list->GetRhiResource()), &label);
    }

    void RHI_Device::MarkerEnd(RHI_CommandList* cmd_list)
    {
        functions::marker_end(static_cast<VkCommandBuffer>(cmd_list->GetRhiResource()));
    }

    // misc

    void RHI_Device::SetResourceName(void* resource, const RHI_Resource_Type resource_type, const std::string name)
    {
        if (Debugging::IsValidationLayerEnabled()) // function pointers are not initialized if validation disabled 
        {
            SP_ASSERT(resource != nullptr);
            SP_ASSERT(functions::set_object_name != nullptr);

            VkDebugUtilsObjectNameInfoEXT name_info = {};
            name_info.sType                         = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT;
            name_info.pNext                         = nullptr;
            name_info.objectType                    = vulkan_object_type[static_cast<uint32_t>(resource_type)];
            name_info.objectHandle                  = reinterpret_cast<uint64_t>(resource);
            name_info.pObjectName                   = name.c_str();

            functions::set_object_name(RHI_Context::device, &name_info);
        }
    }

    void RHI_Device::SetVariableRateShading(const RHI_CommandList* cmd_list, const bool enabled)
    {
        if (!m_is_shading_rate_supported)
            return;

        // set the fragment shading rate state for the current pipeline
        VkExtent2D fragment_size = { 1, 1 };
        VkFragmentShadingRateCombinerOpKHR combiner_operatins[2];

        // The combiners determine how the different shading rate values for the pipeline, primitives and attachment are combined
        if (enabled)
        {
            // If shading rate from attachment is enabled, we set the combiner, so that the values from the attachment are used
            // Combiner for pipeline (A) and primitive (B) - Not used in this sample
            combiner_operatins[0] = VK_FRAGMENT_SHADING_RATE_COMBINER_OP_KEEP_KHR;
            // Combiner for pipeline (A) and attachment (B), replace the pipeline default value (fragment_size) with the fragment sizes stored in the attachment
            combiner_operatins[1] = VK_FRAGMENT_SHADING_RATE_COMBINER_OP_REPLACE_KHR;
        }
        else
        {
            // If shading rate from attachment is disabled, we keep the value set via the dynamic state
            combiner_operatins[0] = VK_FRAGMENT_SHADING_RATE_COMBINER_OP_KEEP_KHR;
            combiner_operatins[1] = VK_FRAGMENT_SHADING_RATE_COMBINER_OP_KEEP_KHR;
        }

        functions::set_fragment_shading_rate(static_cast<VkCommandBuffer>(cmd_list->GetRhiResource()), &fragment_size, combiner_operatins);
    }
}
