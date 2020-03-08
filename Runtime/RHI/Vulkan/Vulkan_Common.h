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

#pragma once

//= IMPLEMENTATION ===============
#include "../RHI_Implementation.h"
#ifdef API_GRAPHICS_VULKAN
//================================

//= INCLUDES ==================
#include "../RHI_Device.h"
#include "../RHI_Texture.h"
#include "../RHI_SwapChain.h"
#include "../../Logging/Log.h"
#include "../../Math/Vector4.h"
#include <array>
#include <map>
#include <atomic>
//=============================

namespace Spartan::vulkan_common
{
    namespace error
    {
        inline const char* to_string(const VkResult result)
        {
            switch (result)
            {
                case VK_SUCCESS:                                            return "VK_SUCCESS";
                case VK_NOT_READY:											return "VK_NOT_READY";
                case VK_TIMEOUT:											return "VK_TIMEOUT";
                case VK_EVENT_SET:											return "VK_EVENT_SET";
                case VK_EVENT_RESET:										return "VK_EVENT_RESET";
                case VK_INCOMPLETE:											return "VK_INCOMPLETE";
                case VK_ERROR_OUT_OF_HOST_MEMORY:							return "VK_ERROR_OUT_OF_HOST_MEMORY";
                case VK_ERROR_OUT_OF_DEVICE_MEMORY:							return "VK_ERROR_OUT_OF_DEVICE_MEMORY";
                case VK_ERROR_INITIALIZATION_FAILED:						return "VK_ERROR_INITIALIZATION_FAILED";
                case VK_ERROR_DEVICE_LOST:									return "VK_ERROR_DEVICE_LOST";
                case VK_ERROR_MEMORY_MAP_FAILED:							return "VK_ERROR_MEMORY_MAP_FAILED";
                case VK_ERROR_LAYER_NOT_PRESENT:							return "VK_ERROR_LAYER_NOT_PRESENT";
                case VK_ERROR_EXTENSION_NOT_PRESENT:						return "VK_ERROR_EXTENSION_NOT_PRESENT";
                case VK_ERROR_FEATURE_NOT_PRESENT:							return "VK_ERROR_FEATURE_NOT_PRESENT";
                case VK_ERROR_INCOMPATIBLE_DRIVER:							return "VK_ERROR_INCOMPATIBLE_DRIVER";
                case VK_ERROR_TOO_MANY_OBJECTS:								return "VK_ERROR_TOO_MANY_OBJECTS";
                case VK_ERROR_FORMAT_NOT_SUPPORTED:							return "VK_ERROR_FORMAT_NOT_SUPPORTED";
                case VK_ERROR_FRAGMENTED_POOL:								return "VK_ERROR_FRAGMENTED_POOL";
                case VK_ERROR_OUT_OF_POOL_MEMORY:							return "VK_ERROR_OUT_OF_POOL_MEMORY";
                case VK_ERROR_INVALID_EXTERNAL_HANDLE:						return "VK_ERROR_INVALID_EXTERNAL_HANDLE";
                case VK_ERROR_SURFACE_LOST_KHR:								return "VK_ERROR_SURFACE_LOST_KHR";
                case VK_ERROR_NATIVE_WINDOW_IN_USE_KHR:						return "VK_ERROR_NATIVE_WINDOW_IN_USE_KHR";
                case VK_SUBOPTIMAL_KHR:										return "VK_SUBOPTIMAL_KHR";
                case VK_ERROR_OUT_OF_DATE_KHR:								return "VK_ERROR_OUT_OF_DATE_KHR";
                case VK_ERROR_INCOMPATIBLE_DISPLAY_KHR:						return "VK_ERROR_INCOMPATIBLE_DISPLAY_KHR";
                case VK_ERROR_VALIDATION_FAILED_EXT:						return "VK_ERROR_VALIDATION_FAILED_EXT";
                case VK_ERROR_INVALID_SHADER_NV:							return "VK_ERROR_INVALID_SHADER_NV";
                case VK_ERROR_INVALID_DRM_FORMAT_MODIFIER_PLANE_LAYOUT_EXT: return "VK_ERROR_INVALID_DRM_FORMAT_MODIFIER_PLANE_LAYOUT_EXT";
                case VK_ERROR_FRAGMENTATION_EXT:							return "VK_ERROR_FRAGMENTATION_EXT";
                case VK_ERROR_NOT_PERMITTED_EXT:							return "VK_ERROR_NOT_PERMITTED_EXT";
                case VK_ERROR_INVALID_DEVICE_ADDRESS_EXT:					return "VK_ERROR_INVALID_DEVICE_ADDRESS_EXT";
                case VK_ERROR_FULL_SCREEN_EXCLUSIVE_MODE_LOST_EXT:			return "VK_ERROR_FULL_SCREEN_EXCLUSIVE_MODE_LOST_EXT";
                case VK_ERROR_UNKNOWN:                                      return "VK_ERROR_UNKNOWN";
                case VK_RESULT_RANGE_SIZE:                                  return "VK_RESULT_RANGE_SIZE";
                case VK_RESULT_MAX_ENUM:                                    return "VK_RESULT_MAX_ENUM";
            }

            return "Unknown error code";
        }

        inline bool check(VkResult result)
        {
            if (result == VK_SUCCESS)
                return true;

            LOG_ERROR("%s", to_string(result));
            return false;
        }

        inline void _assert(VkResult result)
        {
            SPARTAN_ASSERT(result == VK_SUCCESS);
        }
    }

    namespace device
    {
        inline uint32_t get_queue_family_index(VkQueueFlagBits queue_flags, const std::vector<VkQueueFamilyProperties>& queue_family_properties, uint32_t* index)
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
        }

        inline bool get_queue_family_indices(RHI_Context* rhi_context, const VkPhysicalDevice& physical_device)
        {
            uint32_t queue_family_count = 0;
            vkGetPhysicalDeviceQueueFamilyProperties(physical_device, &queue_family_count, nullptr);

            std::vector<VkQueueFamilyProperties> queue_families_properties(queue_family_count);
            vkGetPhysicalDeviceQueueFamilyProperties(physical_device, &queue_family_count, queue_families_properties.data());

            if (!get_queue_family_index(VK_QUEUE_GRAPHICS_BIT, queue_families_properties, &rhi_context->queue_graphics_index))
                return false;

            if (!get_queue_family_index(VK_QUEUE_TRANSFER_BIT, queue_families_properties, &rhi_context->queue_transfer_index))
            {
                LOG_WARNING("Transfer queue not suported, using graphics instead.");
                rhi_context->queue_transfer_index = rhi_context->queue_graphics_index;
                return true;
            }

            if (!get_queue_family_index(VK_QUEUE_COMPUTE_BIT, queue_families_properties, &rhi_context->queue_compute_index))
            {
                LOG_WARNING("Compute queue not suported, using graphics instead.");
                rhi_context->queue_compute_index = rhi_context->queue_graphics_index;
                return true;
            }

            return true;
        }

        inline bool choose_physical_device(RHI_Device* rhi_device, void* window_handle)
        {
            RHI_Context* rhi_context = rhi_device->GetContextRhi();

            // Register all physical devices
            {
                uint32_t device_count = 0;
                if (!error::check(vkEnumeratePhysicalDevices(rhi_context->instance, &device_count, nullptr)))
                    return false;

                if (device_count == 0)
                {
                    LOG_ERROR("There are no available devices.");
                    return false;
                }

                std::vector<VkPhysicalDevice> physical_devices(device_count);
                if (!error::check(vkEnumeratePhysicalDevices(rhi_context->instance, &device_count, physical_devices.data())))
                    return false;

                // Go through all the devices
                for (const VkPhysicalDevice& device_physical : physical_devices)
                {
                    // Get device properties
                    VkPhysicalDeviceProperties device_properties = {};
                    vkGetPhysicalDeviceProperties(device_physical, &device_properties);

                    VkPhysicalDeviceMemoryProperties device_memory_properties = {};
                    vkGetPhysicalDeviceMemoryProperties(device_physical, &device_memory_properties);

                    RHI_PhysicalDevice_Type type = RHI_PhysicalDevice_Unknown;
                    if (device_properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU) type = RHI_PhysicalDevice_Integrated;
                    if (device_properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU)   type = RHI_PhysicalDevice_Discrete;
                    if (device_properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU)    type = RHI_PhysicalDevice_Virtual;
                    if (device_properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_CPU)            type = RHI_PhysicalDevice_Cpu;

                    // Let the engine know about it as it will sort all of the devices from best to worst
                    rhi_device->RegisterPhysicalDevice(PhysicalDevice
                    (
                        device_properties.apiVersion,                                                       // api version
                        device_properties.driverVersion,                                                    // driver version
                        device_properties.vendorID,                                                         // vendor id
                        type,                                                                               // type
                        &device_properties.deviceName[0],                                                   // name
                        static_cast<uint32_t>(device_memory_properties.memoryHeaps[0].size / 1024 / 1024),  // memory (MBs)
                        static_cast<void*>(device_physical)                                                 // data
                    ));
                }
            }

            // Go through all the devices (sorted from best to worst based on their properties)
            for (uint32_t device_index = 0; device_index < rhi_device->GetPhysicalDevices().size(); device_index++)
            {
                const PhysicalDevice& physical_device_engine = rhi_device->GetPhysicalDevices()[device_index];
                VkPhysicalDevice physical_device_vk = static_cast<VkPhysicalDevice>(physical_device_engine.data);

                // Get the first device that has a graphics, a compute and a transfer queue
                if (get_queue_family_indices(rhi_context, physical_device_vk))
                {
                    rhi_device->SetPrimaryPhysicalDevice(device_index);
                    rhi_context->device_physical = physical_device_vk;
                    break;
                }
            }

            return true;
        }
    }

	namespace memory
	{
		inline uint32_t get_type(const RHI_Context* rhi_context, const VkMemoryPropertyFlags properties, const uint32_t type_bits)
		{
			VkPhysicalDeviceMemoryProperties device_memory_properties;
			vkGetPhysicalDeviceMemoryProperties(rhi_context->device_physical, &device_memory_properties);

            for (uint32_t i = 0; i < device_memory_properties.memoryTypeCount; i++)
            {
                if ((device_memory_properties.memoryTypes[i].propertyFlags & properties) == properties && type_bits & (1 << i))
                    return i;
            }

            // Unable to find memoryType
			return std::numeric_limits<uint32_t>::max(); 
		}

		inline void free(const RHI_Context* rhi_context, void*& device_memory)
		{
			if (!device_memory)
				return;

			vkFreeMemory(rhi_context->device, static_cast<VkDeviceMemory>(device_memory), nullptr);
			device_memory = nullptr;
		}
	}

    namespace semaphore
    {
        inline bool create(const RHI_Context* rhi_context, void*& semaphore)
        {
            VkSemaphoreCreateInfo semaphore_info = {};
            semaphore_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

            VkSemaphore* semaphore_vk = reinterpret_cast<VkSemaphore*>(&semaphore);
            return error::check(vkCreateSemaphore(rhi_context->device, &semaphore_info, nullptr, semaphore_vk));
        }

        inline void destroy(const RHI_Context* rhi_context, void*& semaphore)
        {
            if (!semaphore)
                return;

            VkSemaphore semaphore_vk = static_cast<VkSemaphore>(semaphore);
            vkDestroySemaphore(rhi_context->device, semaphore_vk, nullptr);
            semaphore = nullptr;
        }
    }

    namespace fence
    {
        inline bool create(const RHI_Context* rhi_context, void*& fence)
        {
            VkFenceCreateInfo fence_info = {};
            fence_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;

            VkFence* fence_vk = reinterpret_cast<VkFence*>(&fence);
            return error::check(vkCreateFence(rhi_context->device, &fence_info, nullptr, fence_vk));
        }

        inline void destroy(const RHI_Context* rhi_context, void*& fence)
        {
            if (!fence)
                return;

            VkFence fence_vk = static_cast<VkFence>(fence);
            vkDestroyFence(rhi_context->device, fence_vk, nullptr);
            fence = nullptr;
        }

        inline bool wait(const RHI_Context* rhi_context, void*& fence)
        {
            VkFence* fence_vk = reinterpret_cast<VkFence*>(&fence);
            return error::check(vkWaitForFences(rhi_context->device, 1, fence_vk, true, std::numeric_limits<uint64_t>::max()));
        }

        inline bool reset(const RHI_Context* rhi_context, void*& fence)
        {
            VkFence* fence_vk = reinterpret_cast<VkFence*>(&fence);
            return error::check(vkResetFences(rhi_context->device, 1, fence_vk));
        }

        inline bool wait_reset(const RHI_Context* rhi_context, void*& fence)
        {
            VkFence* fence_vk = reinterpret_cast<VkFence*>(&fence);
            return error::check(vkWaitForFences(rhi_context->device, 1, fence_vk, true, std::numeric_limits<uint64_t>::max())) &&
                error::check(vkResetFences(rhi_context->device, 1, fence_vk));
        }
    }

    namespace command_pool
    {
        inline bool create(const RHI_Device* rhi_device, void*& cmd_pool, const RHI_Queue_Type queue_type)
        {
            VkCommandPoolCreateInfo cmd_pool_info   = {};
            cmd_pool_info.sType                     = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
            cmd_pool_info.queueFamilyIndex          = rhi_device->Queue_Index(queue_type);
            cmd_pool_info.flags                     = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;

            VkCommandPool* cmd_pool_vk = reinterpret_cast<VkCommandPool*>(&cmd_pool);
            return error::check(vkCreateCommandPool(rhi_device->GetContextRhi()->device, &cmd_pool_info, nullptr, cmd_pool_vk));
        }

        inline void destroy(const RHI_Context* rhi_context, void*& cmd_pool)
        {
            VkCommandPool cmd_pool_vk = static_cast<VkCommandPool>(cmd_pool);
            vkDestroyCommandPool(rhi_context->device, cmd_pool_vk, nullptr);
            cmd_pool = nullptr;
        }
    }

    namespace command_buffer
    {
        inline bool create(const RHI_Context* rhi_context, void*& cmd_pool, void*& cmd_buffer, const VkCommandBufferLevel level)
        {
            VkCommandPool cmd_pool_vk       = static_cast<VkCommandPool>(cmd_pool);
            VkCommandBuffer* cmd_buffer_vk  = reinterpret_cast<VkCommandBuffer*>(&cmd_buffer);

            VkCommandBufferAllocateInfo allocate_info   = {};
            allocate_info.sType                         = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
            allocate_info.commandPool                   = cmd_pool_vk;
            allocate_info.level                         = level;
            allocate_info.commandBufferCount            = 1;

            return error::check(vkAllocateCommandBuffers(rhi_context->device, &allocate_info, cmd_buffer_vk));
        }

        inline void free(const RHI_Context* rhi_context, void*& cmd_pool, void*& cmd_buffer)
        {
            VkCommandPool cmd_pool_vk       = static_cast<VkCommandPool>(cmd_pool);
            VkCommandBuffer* cmd_buffer_vk  = reinterpret_cast<VkCommandBuffer*>(&cmd_buffer);
            vkFreeCommandBuffers(rhi_context->device, cmd_pool_vk, 1, cmd_buffer_vk);
        }

        inline bool begin(void* cmd_buffer, VkCommandBufferUsageFlagBits usage = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT)
        {
            VkCommandBufferBeginInfo begin_info = {};
            begin_info.sType                    = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
            begin_info.flags                    = usage;

            return error::check(vkBeginCommandBuffer(static_cast<VkCommandBuffer>(cmd_buffer), &begin_info));
        }

        inline bool end(void* cmd_buffer)
        {
            return error::check(vkEndCommandBuffer(static_cast<VkCommandBuffer>(cmd_buffer)));
        }
    }

    // Thread-safe immediate command buffer
    class command_buffer_immediate
    {
    public:
        command_buffer_immediate() = default;
        ~command_buffer_immediate() = default;

        struct cmdbi_object
        {
            bool Initialize(RHI_Device* rhi_device, const RHI_Queue_Type queue_type)
            {
                if (initialized)
                    return true;

                initialized         = true;
                this->rhi_device    = rhi_device;
                this->queue_type    = queue_type;

                // Create command pool
                if (!command_pool::create(rhi_device, cmd_pool, queue_type))
                    return false;

                // Create command buffer
                if (!command_buffer::create(rhi_device->GetContextRhi(), cmd_pool, cmd_buffer, VK_COMMAND_BUFFER_LEVEL_PRIMARY))
                    return false;

                // Create fence
                if (!fence::create(rhi_device->GetContextRhi(), cmd_buffer_fence))
                    return false;

                return true;
            }

            bool begin()
            {
                used = true;
                return command_buffer::begin(cmd_buffer); static std::map<uint32_t, cmdbi_object> m_objects;
            }

            bool end()
            {
                return command_buffer::end(cmd_buffer);
            }

            bool submit()
            {
                if (!rhi_device->Queue_Submit(queue_type, cmd_buffer))
                    return false;

                if (!rhi_device->Queue_Wait(queue_type))
                    return false;

                used = false;
                return true;
            }

            bool wait_fence()
            {
                if (used)
                {
                    used = false;
                    return fence::wait_reset(rhi_device->GetContextRhi(), cmd_buffer_fence);
                }

                return true;
            }

            void* cmd_pool                  = nullptr;
            void* cmd_buffer                = nullptr;
            void* cmd_buffer_fence          = nullptr;
            RHI_Queue_Type queue_type       = RHI_Queue_Undefined;
            std::atomic<bool> initialized   = false;
            std::atomic<bool> used          = false;
            RHI_Device* rhi_device          = nullptr;
        };

        static VkCommandBuffer begin(RHI_Device* rhi_device, const RHI_Queue_Type queue_type)
        {
            std::lock_guard<std::mutex> lock(m_mutex_begin);

            cmdbi_object& cmbdi = m_objects[queue_type];

            // If the current queue and command buffer are used, wait
            while (cmbdi.used) {}

            // Initialize
            if (!cmbdi.Initialize(rhi_device, queue_type))
                return nullptr;

            // Sync 2: fence ensures the cmd buffer is no longer used
            if (!cmbdi.wait_fence())
                return nullptr;

            // Begin
            if (!cmbdi.begin())
                return nullptr;

            return static_cast<VkCommandBuffer>(cmbdi.cmd_buffer);
        }

        static bool end(const RHI_Queue_Type queue_type)
        {
            std::lock_guard<std::mutex> lock(m_mutex_end);

            cmdbi_object& cmbdi = m_objects[queue_type];

            if (!cmbdi.end())
                return false;

            return cmbdi.submit();
        }

    private:
        static std::mutex m_mutex_begin;
        static std::mutex m_mutex_end;
        static std::map<RHI_Queue_Type, cmdbi_object> m_objects;
    };

	namespace buffer
	{
		inline bool create(const RHI_Context* rhi_context, void*& buffer, void*& device_memory, const uint64_t size, VkBufferUsageFlags usage, VkMemoryPropertyFlags memory_property_flags, const void* data = nullptr)
		{
            VkBuffer* buffer_vk              = reinterpret_cast<VkBuffer*>(&buffer);
            VkDeviceMemory* buffer_memory_vk = reinterpret_cast<VkDeviceMemory*>(&device_memory);

			VkBufferCreateInfo buffer_info	= {};
			buffer_info.sType				= VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
			buffer_info.size				= size;
			buffer_info.usage				= usage;
			buffer_info.sharingMode			= VK_SHARING_MODE_EXCLUSIVE;

			if (!error::check(vkCreateBuffer(rhi_context->device, &buffer_info, nullptr, buffer_vk)))
				return false;

			VkMemoryRequirements memory_requirements;
			vkGetBufferMemoryRequirements(rhi_context->device, *buffer_vk, &memory_requirements);

			VkMemoryAllocateInfo alloc_info = {};			
			alloc_info.sType				= VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
			alloc_info.allocationSize		= memory_requirements.size;
			alloc_info.memoryTypeIndex		= memory::get_type(rhi_context, memory_property_flags, memory_requirements.memoryTypeBits);

            if (!error::check(vkAllocateMemory(rhi_context->device, &alloc_info, nullptr, buffer_memory_vk)))
                return false;

            // If a pointer to the buffer data has been passed, map the buffer and copy over the data
            if (data != nullptr)
            {
                void* mapped;
                if (error::check(vkMapMemory(rhi_context->device, *buffer_memory_vk, 0, size, 0, &mapped)))
                {
                    memcpy(mapped, data, size);

                    // If host coherency hasn't been requested, do a manual flush to make writes visible
                    if ((memory_property_flags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) == 0)
                    {
                        VkMappedMemoryRange mappedRange = {};
                        mappedRange.memory              = *buffer_memory_vk;
                        mappedRange.offset              = 0;
                        mappedRange.size                = size;

                        if (!error::check(vkFlushMappedMemoryRanges(rhi_context->device, 1, &mappedRange)))
                            return false;
                    }

                    vkUnmapMemory(rhi_context->device, *buffer_memory_vk);
                }
            }

            // Attach the memory to the buffer object
            if (!error::check(vkBindBufferMemory(rhi_context->device, *buffer_vk, *buffer_memory_vk, 0)))
                return false;

			return true;
		}

		inline void destroy(const RHI_Context* rhi_context, void*& _buffer)
		{
			if (!_buffer)
				return;

			vkDestroyBuffer(rhi_context->device, static_cast<VkBuffer>(_buffer), nullptr);
			_buffer = nullptr;
		}
	}

    namespace image
    {
        inline VkImageTiling is_format_supported(const RHI_Context* rhi_context, const RHI_Format format, VkFormatFeatureFlags feature_flags)
        {
            // Get format properties
            VkFormatProperties format_properties;
            vkGetPhysicalDeviceFormatProperties(rhi_context->device_physical, vulkan_format[format], &format_properties);

            // Check for optimal support
            if (format_properties.optimalTilingFeatures & feature_flags)
                return VK_IMAGE_TILING_OPTIMAL;

            // Check for linear support
            if (format_properties.linearTilingFeatures & feature_flags)
                return VK_IMAGE_TILING_LINEAR;

            return VK_IMAGE_TILING_MAX_ENUM;
        }

        inline bool allocate_bind(const RHI_Context* rhi_context, const VkImage& image, VkDeviceMemory* memory, VkDeviceSize* memory_size = nullptr)
        {
            VkMemoryRequirements memory_requirements;
            vkGetImageMemoryRequirements(rhi_context->device, image, &memory_requirements);

            VkMemoryAllocateInfo allocate_info  = {};
            allocate_info.sType                 = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
            allocate_info.allocationSize        = memory_requirements.size;
            allocate_info.memoryTypeIndex       = memory::get_type(rhi_context, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, memory_requirements.memoryTypeBits);

            if (!error::check(vkAllocateMemory(rhi_context->device, &allocate_info, nullptr, memory)))
                return false;

            if (!error::check(vkBindImageMemory(rhi_context->device, image, *memory, 0)))
                return false;

            if (memory_size)
            {
                *memory_size = memory_requirements.size;
            }

            return true;
        }

        inline VkImageAspectFlags get_aspect_mask(const RHI_Texture* texture, const bool only_depth = false, const bool only_stencil = false)
        {
            VkImageAspectFlags aspect_mask = 0;

            if (texture->IsColorFormat() && texture->IsDepthStencil())
            {
                LOG_ERROR("Texture can't be both color and depth-stencil");
                return aspect_mask;
            }

            if (texture->IsColorFormat())
            {
                aspect_mask |= VK_IMAGE_ASPECT_COLOR_BIT;
            }
            else
            {
                if (texture->IsDepthFormat() && !only_stencil)
                {
                    aspect_mask |= VK_IMAGE_ASPECT_DEPTH_BIT;
                }

                if (texture->IsStencilFormat() && !only_depth)
                {
                    aspect_mask |= VK_IMAGE_ASPECT_STENCIL_BIT;
                }
            }
            
            return aspect_mask;
        }

        inline bool create(
            const RHI_Context* rhi_context,
            VkImage& image,
            const uint32_t width,
            const uint32_t height,
            const uint32_t mip_levels,
            const uint32_t array_layers,
            const VkFormat format,
            const VkImageTiling tiling,
            const RHI_Image_Layout layout,
            const VkImageUsageFlags usage
        )
        {
            VkImageCreateInfo create_info   = {};
            create_info.sType               = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
            create_info.imageType           = VK_IMAGE_TYPE_2D;
            create_info.extent.width        = width;
            create_info.extent.height       = height;
            create_info.extent.depth        = 1;
            create_info.mipLevels           = mip_levels;
            create_info.arrayLayers         = array_layers;
            create_info.format              = format;
            create_info.tiling              = tiling;
            create_info.initialLayout       = vulkan_image_layout[layout];
            create_info.usage               = usage;
            create_info.samples             = VK_SAMPLE_COUNT_1_BIT;
            create_info.sharingMode         = VK_SHARING_MODE_EXCLUSIVE;

            return error::check(vkCreateImage(rhi_context->device, &create_info, nullptr, &image));
        }

        inline void destroy(const RHI_Context* rhi_context, void*& image)
        {
            if (!image)
                return;

            vkDestroyImage(rhi_context->device, static_cast<VkImage>(image), nullptr);
            image = nullptr;
        }

        inline VkPipelineStageFlags access_flags_to_pipeline_stage(VkAccessFlags access_flags, const VkPipelineStageFlags enabled_graphics_shader_stages)
        {
            VkPipelineStageFlags stages = 0;

            while (access_flags != 0)
            {
                VkAccessFlagBits AccessFlag = static_cast<VkAccessFlagBits>(access_flags & (~(access_flags - 1)));
                SPARTAN_ASSERT(AccessFlag != 0 && (AccessFlag & (AccessFlag - 1)) == 0);
                access_flags &= ~AccessFlag;

                switch (AccessFlag)
                {
                case VK_ACCESS_INDIRECT_COMMAND_READ_BIT:
                    stages |= VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT;
                    break;

                case VK_ACCESS_INDEX_READ_BIT:
                    stages |= VK_PIPELINE_STAGE_VERTEX_INPUT_BIT;
                    break;

                case VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT:
                    stages |= VK_PIPELINE_STAGE_VERTEX_INPUT_BIT;
                    break;

                case VK_ACCESS_UNIFORM_READ_BIT:
                    stages |= enabled_graphics_shader_stages | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
                    break;

                case VK_ACCESS_INPUT_ATTACHMENT_READ_BIT:
                    stages |= VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
                    break;

                case VK_ACCESS_SHADER_READ_BIT:
                    stages |= enabled_graphics_shader_stages | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
                    break;

                case VK_ACCESS_SHADER_WRITE_BIT:
                    stages |= enabled_graphics_shader_stages | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
                    break;

                case VK_ACCESS_COLOR_ATTACHMENT_READ_BIT:
                    stages |= VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
                    break;

                case VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT:
                    stages |= VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
                    break;

                case VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT:
                    stages |= VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
                    break;

                case VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT:
                    stages |= VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
                    break;

                case VK_ACCESS_TRANSFER_READ_BIT:
                    stages |= VK_PIPELINE_STAGE_TRANSFER_BIT;
                    break;

                case VK_ACCESS_TRANSFER_WRITE_BIT:
                    stages |= VK_PIPELINE_STAGE_TRANSFER_BIT;
                    break;

                case VK_ACCESS_HOST_READ_BIT:
                    stages |= VK_PIPELINE_STAGE_HOST_BIT;
                    break;

                case VK_ACCESS_HOST_WRITE_BIT:
                    stages |= VK_PIPELINE_STAGE_HOST_BIT;
                    break;

                case VK_ACCESS_MEMORY_READ_BIT:
                    break;

                case VK_ACCESS_MEMORY_WRITE_BIT:
                    break;

                default:
                    LOG_ERROR("Unknown memory access flag");
                    break;
                }
            }
            return stages;
        }

        inline VkPipelineStageFlags layout_to_access_mask(const VkImageLayout layout, const bool is_destination_mask)
        {
            VkPipelineStageFlags access_mask = 0;

            switch (layout)
            {
            case VK_IMAGE_LAYOUT_UNDEFINED:
                if (is_destination_mask)
                {
                    LOG_ERROR("The new layout used in a transition must not be VK_IMAGE_LAYOUT_UNDEFINED.");
                }
                break;

            case VK_IMAGE_LAYOUT_GENERAL:
                access_mask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
                break;

            case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:
                access_mask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
                break;

            case VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL:
                access_mask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
                break;

            case VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL:
                access_mask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT;
                break;

            case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
                access_mask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_INPUT_ATTACHMENT_READ_BIT;
                break;

            case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:
                access_mask = VK_ACCESS_TRANSFER_READ_BIT;
                break;

            case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
                access_mask = VK_ACCESS_TRANSFER_WRITE_BIT;
                break;

            case VK_IMAGE_LAYOUT_PREINITIALIZED:
                if (!is_destination_mask)
                {
                    access_mask = VK_ACCESS_HOST_WRITE_BIT;
                }
                else
                {
                    LOG_ERROR("The new layout used in a transition must not be VK_IMAGE_LAYOUT_PREINITIALIZED.");
                }
                break;

            case VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_STENCIL_ATTACHMENT_OPTIMAL:
                access_mask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT;
                break;

            case VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_STENCIL_READ_ONLY_OPTIMAL:
                access_mask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT;
                break;

            case VK_IMAGE_LAYOUT_PRESENT_SRC_KHR:
                access_mask = VK_ACCESS_MEMORY_READ_BIT;
                break;

            default:
                LOG_ERROR("Unexpected image layout");
                break;
            }

            return access_mask;
        }

        inline bool set_layout(const RHI_Device* rhi_device, void* cmd_buffer, void* image, const VkImageAspectFlags aspect_mask, const uint32_t level_count, const uint32_t layer_count, const RHI_Image_Layout layout_old, const RHI_Image_Layout layout_new)
	    {
            VkImageMemoryBarrier image_barrier              = {};
            image_barrier.sType                             = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            image_barrier.pNext                             = nullptr;
            image_barrier.oldLayout                         = vulkan_image_layout[layout_old];
            image_barrier.newLayout                         = vulkan_image_layout[layout_new];
            image_barrier.srcQueueFamilyIndex               = VK_QUEUE_FAMILY_IGNORED;
            image_barrier.dstQueueFamilyIndex               = VK_QUEUE_FAMILY_IGNORED;
            image_barrier.image                             = static_cast<VkImage>(image);
            image_barrier.subresourceRange.aspectMask       = aspect_mask;
            image_barrier.subresourceRange.baseMipLevel     = 0;
            image_barrier.subresourceRange.levelCount       = level_count;
            image_barrier.subresourceRange.baseArrayLayer   = 0;
            image_barrier.subresourceRange.layerCount       = layer_count;
            image_barrier.srcAccessMask                     = layout_to_access_mask(image_barrier.oldLayout, false);
            image_barrier.dstAccessMask                     = layout_to_access_mask(image_barrier.newLayout, true);

            VkPipelineStageFlags source_stage = 0;
            {
                if (image_barrier.oldLayout == VK_IMAGE_LAYOUT_PRESENT_SRC_KHR)
                {
                    source_stage = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
                }
                else if (image_barrier.srcAccessMask != 0)
                {
                    source_stage = access_flags_to_pipeline_stage(image_barrier.srcAccessMask, rhi_device->GetEnabledGraphicsStages());
                }
                else
                {
                    source_stage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
                }
            }

            VkPipelineStageFlags destination_stage = 0;
            {
                if (image_barrier.newLayout == VK_IMAGE_LAYOUT_PRESENT_SRC_KHR)
                {
                    destination_stage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
                }
                else if (image_barrier.dstAccessMask != 0)
                {
                    destination_stage = access_flags_to_pipeline_stage(image_barrier.dstAccessMask, rhi_device->GetEnabledGraphicsStages());
                }
                else
                {
                    destination_stage = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
                }
            }

	    	vkCmdPipelineBarrier
	    	(
	    		static_cast<VkCommandBuffer>(cmd_buffer),
	    		source_stage, destination_stage,
	    		0,
	    		0, nullptr,
	    		0, nullptr,
	    		1, &image_barrier
	    	);

	    	return true;
	    }

        inline bool set_layout(const RHI_Device* rhi_device, void* cmd_buffer, const RHI_Texture* texture, const RHI_Image_Layout layout_new)
        {
            return set_layout(rhi_device, cmd_buffer, texture->Get_Texture(), get_aspect_mask(texture), texture->GetMiplevels(), texture->GetArraySize(), texture->GetLayout(), layout_new);
        }

        inline bool set_layout(const RHI_Device* rhi_device, void* cmd_buffer, void* image, const RHI_SwapChain* swapchain, const RHI_Image_Layout layout_new)
        {
            return set_layout(rhi_device, cmd_buffer, image, VK_IMAGE_ASPECT_COLOR_BIT, 1, 1, swapchain->GetLayout(), layout_new);
        }

        namespace view
        {
            inline bool create(const RHI_Context* rhi_context, void* image, void*& image_view, VkImageViewType type, const VkFormat format, const VkImageAspectFlags aspect_mask, const uint32_t level_count, const uint32_t layer_count)
            {
                VkImageViewCreateInfo create_info           = {};
                create_info.sType                           = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
                create_info.image                           = static_cast<VkImage>(image);
                create_info.viewType                        = type;
                create_info.format                          = format;
                create_info.subresourceRange.aspectMask     = aspect_mask;
                create_info.subresourceRange.baseMipLevel   = 0;
                create_info.subresourceRange.levelCount     = level_count;
                create_info.subresourceRange.baseArrayLayer = 0;
                create_info.subresourceRange.layerCount     = layer_count;
                create_info.components.r                    = VK_COMPONENT_SWIZZLE_IDENTITY;
                create_info.components.g                    = VK_COMPONENT_SWIZZLE_IDENTITY;
                create_info.components.b                    = VK_COMPONENT_SWIZZLE_IDENTITY;
                create_info.components.a                    = VK_COMPONENT_SWIZZLE_IDENTITY;

                VkImageView* image_view_vk = reinterpret_cast<VkImageView*>(&image_view);
                return error::check(vkCreateImageView(rhi_context->device, &create_info, nullptr, image_view_vk));
            }

            inline bool create(const RHI_Context* rhi_context, void* image, void*& image_view, const RHI_Texture* texture, const bool only_depth = false, const bool only_stencil = false)
            {
                VkImageViewType type = VK_IMAGE_VIEW_TYPE_MAX_ENUM;

                if (texture->GetResourceType() == Resource_Texture2d)
                {
                    type = (texture->GetArraySize() == 1) ? VK_IMAGE_VIEW_TYPE_2D : VK_IMAGE_VIEW_TYPE_2D_ARRAY;
                }
                else if (texture->GetResourceType() == Resource_TextureCube)
                {
                    type = (texture->GetArraySize() == 1) ? VK_IMAGE_VIEW_TYPE_CUBE : VK_IMAGE_VIEW_TYPE_CUBE_ARRAY;
                }

                return create(rhi_context, image, image_view, type, vulkan_format[texture->GetFormat()], get_aspect_mask(texture, only_depth, only_stencil), texture->GetMiplevels(), texture->GetArraySize());
            }

            inline void destroy(const RHI_Context* rhi_context, void*& image_view)
            {
                if (!image_view)
                    return;

                vkDestroyImageView(rhi_context->device, static_cast<VkImageView>(image_view), nullptr);
                image_view = nullptr;
            }

            inline void destroy(const RHI_Context* rhi_context, std::vector<void*>& image_views)
            {
                for (auto& image_view : image_views)
                {
                    vkDestroyImageView(rhi_context->device, static_cast<VkImageView>(image_view), nullptr);
                }
                image_views.clear();
                image_views.shrink_to_fit();
            }
        }
    }

    namespace render_pass
    {
        inline bool create(
            const RHI_Context* rhi_context,
            RHI_Texture** render_target_color_textures,
            Math::Vector4 render_target_color_clear[],
            uint32_t render_target_color_texture_count,
            RHI_Texture* render_target_depth_texture,
            float render_target_depth_clear,
            bool is_swapchain,
            void*& render_pass
        )
        {
            // Attachment descriptions
            uint32_t attachment_count = render_target_color_texture_count + static_cast<uint32_t>(render_target_depth_texture ? 1 : 0);
            std::vector<VkAttachmentDescription> attachment_descriptions(attachment_count, VkAttachmentDescription());
            {
                // Swapchain
                if (is_swapchain)
                {
                    // Attachment descriptions
                    attachment_descriptions[0].format          = rhi_context->surface_format;
                    attachment_descriptions[0].samples         = VK_SAMPLE_COUNT_1_BIT;
                    attachment_descriptions[0].loadOp          = (render_target_color_clear[0] == state_dont_clear_color) ? VK_ATTACHMENT_LOAD_OP_LOAD : VK_ATTACHMENT_LOAD_OP_CLEAR;
                    attachment_descriptions[0].storeOp         = VK_ATTACHMENT_STORE_OP_STORE;
                    attachment_descriptions[0].stencilLoadOp   = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
                    attachment_descriptions[0].stencilStoreOp  = VK_ATTACHMENT_STORE_OP_DONT_CARE;
                    attachment_descriptions[0].initialLayout   = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
                    attachment_descriptions[0].finalLayout     = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
                }
                else if (render_target_color_textures) // Texture
                {
                    // Color
                    for (uint32_t i = 0; i < render_target_color_texture_count; i++)
                    {
                        attachment_descriptions[i].format           = vulkan_format[render_target_color_textures[i]->GetFormat()];
                        attachment_descriptions[i].samples          = VK_SAMPLE_COUNT_1_BIT;
                        attachment_descriptions[i].loadOp           = (render_target_color_clear[i] == state_dont_clear_color) ? VK_ATTACHMENT_LOAD_OP_DONT_CARE: VK_ATTACHMENT_LOAD_OP_CLEAR;
                        attachment_descriptions[i].storeOp          = VK_ATTACHMENT_STORE_OP_STORE;
                        attachment_descriptions[i].stencilLoadOp    = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
                        attachment_descriptions[i].stencilStoreOp   = VK_ATTACHMENT_STORE_OP_DONT_CARE;
                        attachment_descriptions[i].initialLayout    = vulkan_image_layout[render_target_color_textures[i]->GetLayout()];
                        attachment_descriptions[i].finalLayout      = vulkan_image_layout[render_target_color_textures[i]->GetLayout()];
                    }
                }

                // Depth
                if (render_target_depth_texture)
                {
                    VkAttachmentDescription& attachment_description = attachment_descriptions.back();
                    attachment_description.format                   = vulkan_format[render_target_depth_texture->GetFormat()];
                    attachment_description.samples                  = VK_SAMPLE_COUNT_1_BIT;
                    attachment_description.loadOp                   = (render_target_depth_clear == state_dont_clear_depth) ? VK_ATTACHMENT_LOAD_OP_DONT_CARE : VK_ATTACHMENT_LOAD_OP_CLEAR;
                    attachment_description.storeOp                  = VK_ATTACHMENT_STORE_OP_STORE;
                    attachment_description.stencilLoadOp            = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
                    attachment_description.stencilStoreOp           = VK_ATTACHMENT_STORE_OP_DONT_CARE;
                    attachment_description.initialLayout            = vulkan_image_layout[render_target_depth_texture->GetLayout()];
                    attachment_description.finalLayout              = vulkan_image_layout[render_target_depth_texture->GetLayout()];
                }
            }

            // Color attachment references
            std::vector<VkAttachmentReference> reference_colors(render_target_color_texture_count);
            for (uint32_t i = 0; i < render_target_color_texture_count; i++)
            {
                reference_colors[i] = { i, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };
            }

            // Depth attachment reference
            VkAttachmentReference reference_depth   = {};
            reference_depth.attachment              = render_target_color_texture_count;
            reference_depth.layout                  = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

            // Subpass
            VkSubpassDescription subpass    = {};
            subpass.pipelineBindPoint       = VK_PIPELINE_BIND_POINT_GRAPHICS;
            subpass.pColorAttachments       = reference_colors.data();
            subpass.colorAttachmentCount    = static_cast<uint32_t>(reference_colors.size());
            if (render_target_depth_texture)
            {
                subpass.pDepthStencilAttachment = &reference_depth;
            }

            // Sub-pass dependencies for layout transitions
            std::array<VkSubpassDependency, 0> dependencies
            {
                //VkSubpassDependency
                //{
                //    VK_SUBPASS_EXTERNAL,														// uint32_t srcSubpass;
                //    0,																			// uint32_t dstSubpass;
                //    VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,										// PipelineStageFlags srcStageMask;
                //    VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,								// PipelineStageFlags dstStageMask;
                //    VK_ACCESS_MEMORY_READ_BIT,													// AccessFlags srcAccessMask;
                //    VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,	// AccessFlags dstAccessMask;
                //    VK_DEPENDENCY_BY_REGION_BIT													// DependencyFlags dependencyFlags;
                //},

                //VkSubpassDependency
                //{
                //    0,																			// uint32_t srcSubpass;
                //    VK_SUBPASS_EXTERNAL,														// uint32_t dstSubpass;
                //    VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,								// PipelineStageFlags srcStageMask;
                //    VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,										// PipelineStageFlags dstStageMask;
                //    VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,	// AccessFlags srcAccessMask;
                //    VK_ACCESS_MEMORY_READ_BIT,													// AccessFlags dstAccessMask;
                //    VK_DEPENDENCY_BY_REGION_BIT													// DependencyFlags dependencyFlags;
                //},
            };

            VkRenderPassCreateInfo render_pass_info = {};
            render_pass_info.sType                  = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
            render_pass_info.attachmentCount        = static_cast<uint32_t>(attachment_descriptions.size());
            render_pass_info.pAttachments           = attachment_descriptions.data();
            render_pass_info.subpassCount           = 1;
            render_pass_info.pSubpasses             = &subpass;
            render_pass_info.dependencyCount        = static_cast<uint32_t>(dependencies.size());
            render_pass_info.pDependencies          = dependencies.data();

            VkRenderPass* render_pass_vk = reinterpret_cast<VkRenderPass*>(&render_pass);
            return error::check(vkCreateRenderPass(rhi_context->device, &render_pass_info, nullptr, render_pass_vk));
        }

        inline void destroy(const RHI_Context* rhi_context, void*& render_pass)
        {
            if (!render_pass)
                return;

            VkRenderPass render_pass_vk = static_cast<VkRenderPass>(render_pass);
            vkDestroyRenderPass(rhi_context->device, render_pass_vk, nullptr);
            render_pass = nullptr;
        }
    }

    namespace frame_buffer
    {
        inline bool create(const RHI_Context* rhi_context, void* render_pass, const std::vector<void*>& attachments, const uint32_t width, const uint32_t height, void*& frame_buffer)
        {
            VkFramebufferCreateInfo create_info = {};
            create_info.sType                   = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
            create_info.renderPass              = static_cast<VkRenderPass>(render_pass);
            create_info.attachmentCount         = static_cast<uint32_t>(attachments.size());
            create_info.pAttachments            = reinterpret_cast<const VkImageView*>(attachments.data());
            create_info.width                   = width;
            create_info.height                  = height;
            create_info.layers                  = 1;

            VkFramebuffer* frame_buffer_vk = reinterpret_cast<VkFramebuffer*>(&frame_buffer);
            return error::check(vkCreateFramebuffer(rhi_context->device, &create_info, nullptr, frame_buffer_vk));
        }

        inline void destroy(const RHI_Context* rhi_context, void*& frame_buffer)
        {
            if (!frame_buffer)
                return;

            vkDestroyFramebuffer(rhi_context->device, static_cast<VkFramebuffer>(frame_buffer), nullptr);
            frame_buffer = nullptr;
        }

        inline void destroy(const RHI_Context* rhi_context, std::vector<void*>& frame_buffers)
        {
            for (auto& frame_buffer : frame_buffers)
            {
                if (!frame_buffer)
                    return;

                vkDestroyFramebuffer(rhi_context->device, static_cast<VkFramebuffer>(frame_buffer), nullptr);
            }
            frame_buffers.clear();
        }
    }

    namespace surface
    {
        inline VkSurfaceCapabilitiesKHR capabilities(const RHI_Context* rhi_context, const VkSurfaceKHR surface)
        {
            VkSurfaceCapabilitiesKHR surface_capabilities;
            vkGetPhysicalDeviceSurfaceCapabilitiesKHR(rhi_context->device_physical, surface, &surface_capabilities);
            return surface_capabilities;
        }

        inline std::vector<VkPresentModeKHR> present_modes(const RHI_Context* rhi_context, const VkSurfaceKHR surface)
        {
            uint32_t present_mode_count;
            vkGetPhysicalDeviceSurfacePresentModesKHR(rhi_context->device_physical, surface, &present_mode_count, nullptr);

            std::vector<VkPresentModeKHR> surface_present_modes(present_mode_count);
            vkGetPhysicalDeviceSurfacePresentModesKHR(rhi_context->device_physical, surface, &present_mode_count, &surface_present_modes[0]);
            return surface_present_modes;
        }

        inline std::vector<VkSurfaceFormatKHR> formats(const RHI_Context* rhi_context, const VkSurfaceKHR surface)
        {
            uint32_t format_count;
            vkGetPhysicalDeviceSurfaceFormatsKHR(rhi_context->device_physical, surface, &format_count, nullptr);

            std::vector<VkSurfaceFormatKHR> surface_formats(format_count);
            error::check(vkGetPhysicalDeviceSurfaceFormatsKHR(rhi_context->device_physical, surface, &format_count, &surface_formats[0]));

            return surface_formats;
        }

        inline void detect_format_and_color_space(const RHI_Context* rhi_context, const VkSurfaceKHR surface, VkFormat* format, VkColorSpaceKHR* color_space)
        {
            std::vector<VkSurfaceFormatKHR> surface_formats = formats(rhi_context, surface);

            // If the surface format list only includes one entry with VK_FORMAT_UNDEFINED,
            // there is no preferred format, so we assume VK_FORMAT_B8G8R8A8_UNORM
            if ((surface_formats.size() == 1) && (surface_formats[0].format == VK_FORMAT_UNDEFINED))
            {
                *format = VK_FORMAT_B8G8R8A8_UNORM;
                *color_space = surface_formats[0].colorSpace;
            }
            else
            {
                // iterate over the list of available surface format and
                // check for the presence of VK_FORMAT_B8G8R8A8_UNORM
                bool found_B8G8R8A8_UNORM = false;
                for (auto&& surfaceFormat : surface_formats)
                {
                    if (surfaceFormat.format == VK_FORMAT_B8G8R8A8_UNORM)
                    {
                        *format = surfaceFormat.format;
                        *color_space = surfaceFormat.colorSpace;
                        found_B8G8R8A8_UNORM = true;
                        break;
                    }
                }

                // in case VK_FORMAT_B8G8R8A8_UNORM is not available
                // select the first available color format
                if (!found_B8G8R8A8_UNORM)
                {
                    *format         = surface_formats[0].format;
                    *color_space    = surface_formats[0].colorSpace;
                }
            }
        }

        inline VkPresentModeKHR set_present_mode(const RHI_Context* rhi_context, const VkSurfaceKHR surface, const VkPresentModeKHR prefered_present_mode)
        {
            // The VK_PRESENT_MODE_FIFO_KHR mode must always be present as per spec
            // This mode waits for the vertical blank ("v-sync")
            VkPresentModeKHR present_mode = VK_PRESENT_MODE_FIFO_KHR;

            std::vector<VkPresentModeKHR> surface_present_modes = present_modes(rhi_context, surface);

            // Check if the preferred mode is supported
            for (const auto& supported_present_mode : surface_present_modes)
            {
                if (prefered_present_mode == supported_present_mode)
                {
                    present_mode = prefered_present_mode;
                    break;
                }
            }

            // Select a mode from the supported present modes
            for (const auto& supported_present_mode : surface_present_modes)
            {
                if (supported_present_mode == VK_PRESENT_MODE_MAILBOX_KHR)
                {
                    present_mode = supported_present_mode;
                    break;
                }

                if ((present_mode != VK_PRESENT_MODE_MAILBOX_KHR) && (supported_present_mode == VK_PRESENT_MODE_IMMEDIATE_KHR))
                {
                    present_mode = supported_present_mode;
                }
            }

            return present_mode;
        }
    }

    namespace layer
    {
        inline bool is_present(const char* layer_name)
        {
            uint32_t layer_count;
            vkEnumerateInstanceLayerProperties(&layer_count, nullptr);

            std::vector<VkLayerProperties> layers(layer_count);
            vkEnumerateInstanceLayerProperties(&layer_count, layers.data());

            for (const auto& layer : layers)
            {
                if (strcmp(layer_name, layer.layerName) == 0)
                    return true;
            }

            return false;
        }

        inline std::vector<const char*> get_supported(const std::vector<const char*>& layers)
        {
            std::vector<const char*> layers_supported;

            for (const auto& layer : layers)
            {
                if (is_present(layer))
                {
                    layers_supported.emplace_back(layer);
                }
                else
                {
                    LOG_ERROR("Layer \"%s\" is not supported", layer);
                }
            }

            return layers_supported;
        }
    }

    namespace extension
    {
        inline bool is_present_device(const char* extension_name, VkPhysicalDevice device_physical)
        {
            uint32_t extension_count = 0;
            vkEnumerateDeviceExtensionProperties(device_physical, nullptr, &extension_count, nullptr);

            std::vector<VkExtensionProperties> extensions(extension_count);
            vkEnumerateDeviceExtensionProperties(device_physical, nullptr, &extension_count, extensions.data());

            for (const auto& extension : extensions)
            {
                if (strcmp(extension_name, extension.extensionName) == 0)
                    return true;
            }

            return false;
        }

        inline std::vector<const char*> get_supported_device(const std::vector<const char*>& extensions, VkPhysicalDevice device_physical)
        {
            std::vector<const char*> extensions_supported;

            for (const auto& extension : extensions)
            {
                if (is_present_device(extension, device_physical))
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

        inline bool is_present_instance(const char* extension_name)
        {
            uint32_t extension_count = 0;
            vkEnumerateInstanceExtensionProperties(nullptr, &extension_count, nullptr);

            std::vector<VkExtensionProperties> extensions(extension_count);
            vkEnumerateInstanceExtensionProperties(nullptr, &extension_count, extensions.data());

            for (const auto& extension : extensions)
            {
                if (strcmp(extension_name, extension.extensionName) == 0)
                    return true;
            }

            return false;
        }

        inline std::vector<const char*> get_supported_instance(const std::vector<const char*>& extensions)
        {
            std::vector<const char*> extensions_supported;

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
    }

    class functions
    {
    public:
        functions() = default;
        ~functions() = default;

        static void functions::initialize(RHI_Device* device)
        {
            #define get_func(var, def)\
            var = reinterpret_cast<PFN_##def>(vkGetInstanceProcAddr(static_cast<VkInstance>(device->GetContextRhi()->instance), #def));\
            if (!var) LOG_ERROR("Failed to get function pointer for %s", #def);\

            get_func(get_physical_device_memory_properties_2, vkGetPhysicalDeviceMemoryProperties2);

            if (device->GetContextRhi()->debug)
            { 
                /* VK_EXT_debug_utils */
                get_func(create_messenger,  vkCreateDebugUtilsMessengerEXT);
                get_func(destroy_messenger, vkDestroyDebugUtilsMessengerEXT);
                get_func(marker_begin,      vkCmdBeginDebugUtilsLabelEXT);
                get_func(marker_end,        vkCmdEndDebugUtilsLabelEXT);

                /* VK_EXT_debug_marker */
                get_func(set_object_tag,    vkSetDebugUtilsObjectTagEXT);
                get_func(set_object_name,   vkSetDebugUtilsObjectNameEXT);
            }
        }

        static PFN_vkCreateDebugUtilsMessengerEXT           create_messenger;
        static VkDebugUtilsMessengerEXT                     messenger;
        static PFN_vkDestroyDebugUtilsMessengerEXT          destroy_messenger;
        static PFN_vkSetDebugUtilsObjectTagEXT              set_object_tag;
        static PFN_vkSetDebugUtilsObjectNameEXT             set_object_name;
        static PFN_vkCmdBeginDebugUtilsLabelEXT             marker_begin;
        static PFN_vkCmdEndDebugUtilsLabelEXT               marker_end;
        static PFN_vkGetPhysicalDeviceMemoryProperties2KHR  get_physical_device_memory_properties_2;
    };

    class debug
    {
    public:
        debug() = default;
        ~debug() = default;

        static VKAPI_ATTR VkBool32 VKAPI_CALL callback(VkDebugUtilsMessageSeverityFlagBitsEXT msg_severity, VkDebugUtilsMessageTypeFlagsEXT msg_type, const VkDebugUtilsMessengerCallbackDataEXT* p_callback_data, void* p_user_data)
        {
            if (msg_severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT)
            {
                LOG_INFO(p_callback_data->pMessage);
            }
            else if (msg_severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT)
            {
                LOG_INFO(p_callback_data->pMessage);
            }
            else if (msg_severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT)
            {
                LOG_WARNING(p_callback_data->pMessage);
            }
            else if (msg_severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT)
            {
                LOG_ERROR(p_callback_data->pMessage);
            }

            return VK_FALSE;
        }

        static void initialize(VkInstance instance)
        {
            if (functions::create_messenger)
            {
                VkDebugUtilsMessengerCreateInfoEXT create_info  = {};
                create_info.sType                               = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
                create_info.messageSeverity                     = VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
                create_info.messageType                         = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
                create_info.pfnUserCallback                     = callback;

                functions::create_messenger(instance, &create_info, nullptr, &functions::messenger);
            }
        }

        static void shutdown(VkInstance instance)
        {
            if (!functions::destroy_messenger)
                return;

            functions::destroy_messenger(instance, functions::messenger, nullptr);
        }

        static void set_object_name(VkDevice device, uint64_t object, VkObjectType object_type, const char* name)
        {
            if (!functions::set_object_name)
                return;

            VkDebugUtilsObjectNameInfoEXT name_info = {};
            name_info.sType                         = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT;
            name_info.pNext                         = nullptr;
            name_info.objectType                    = object_type;
            name_info.objectHandle                  = object;
            name_info.pObjectName                   = name;
            functions::set_object_name(device, &name_info);
        }

        static void set_object_tag(VkDevice device, uint64_t object, VkObjectType objectType, uint64_t name, size_t tagSize, const void* tag)
        {
            if (!functions::set_object_tag)
                return;

            VkDebugUtilsObjectTagInfoEXT tag_info    = {};
            tag_info.sType                           = VK_STRUCTURE_TYPE_DEBUG_MARKER_OBJECT_TAG_INFO_EXT;
            tag_info.pNext                           = nullptr;
            tag_info.objectType                      = objectType;
            tag_info.objectHandle                    = object;
            tag_info.tagName                         = name;
            tag_info.tagSize                         = tagSize;
            tag_info.pTag                            = tag;
            functions::set_object_tag(device, &tag_info);
        }

        static void begin(VkCommandBuffer cmd_buffer, const char* name, const Math::Vector4& color)
        {
            if (!functions::marker_begin)
                return;

            VkDebugUtilsLabelEXT label  = {};
            label.sType                 = VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT;
            label.pNext                 = nullptr;
            label.pLabelName            = name;
            label.color[0]              = color.x;
            label.color[1]              = color.y;
            label.color[2]              = color.z;
            label.color[3]              = color.w;
            functions::marker_begin(cmd_buffer, &label);
        }

        static void end(VkCommandBuffer cmd_buffer)
        {
            if (!functions::marker_end)
                return;

            functions::marker_end(cmd_buffer);
        }

        static void set_command_pool_name(VkDevice device, VkCommandPool cmd_pool, const char* name)
        {
            set_object_name(device, (uint64_t)cmd_pool, VK_OBJECT_TYPE_COMMAND_POOL, name);
        }

        static void set_command_buffer_name(VkDevice device, VkCommandBuffer cmd_buffer, const char* name)
        {
            set_object_name(device, (uint64_t)cmd_buffer, VK_OBJECT_TYPE_COMMAND_BUFFER, name);
        }

        static void set_queue_name(VkDevice device, VkQueue queue, const char* name)
        {
            set_object_name(device, (uint64_t)queue, VK_OBJECT_TYPE_QUEUE, name);
        }

        static void set_image_name(VkDevice device, VkImage image, const char* name)
        {
            set_object_name(device, (uint64_t)image, VK_OBJECT_TYPE_IMAGE, name);
        }

        static void set_image_view_name(VkDevice device, VkImageView image_view, const char* name)
        {
            set_object_name(device, (uint64_t)image_view, VK_OBJECT_TYPE_IMAGE_VIEW, name);
        }

        static void set_sampler_name(VkDevice device, VkSampler sampler, const char* name)
        {
            set_object_name(device, (uint64_t)sampler, VK_OBJECT_TYPE_SAMPLER, name);
        }

        static void set_buffer_name(VkDevice device, VkBuffer buffer, const char* name)
        {
            set_object_name(device, (uint64_t)buffer, VK_OBJECT_TYPE_BUFFER, name);
        }

        static void set_buffer_view_name(VkDevice device, VkBufferView bufferView, const char* name)
        {
            set_object_name(device, (uint64_t)bufferView, VK_OBJECT_TYPE_BUFFER_VIEW, name);
        }

        static void set_device_memory_name(VkDevice device, VkDeviceMemory memory, const char* name)
        {
            set_object_name(device, (uint64_t)memory, VK_OBJECT_TYPE_DEVICE_MEMORY, name);
        }

        static void set_shader_module_name(VkDevice device, VkShaderModule shaderModule, const char* name)
        {
            set_object_name(device, (uint64_t)shaderModule, VK_OBJECT_TYPE_SHADER_MODULE, name);
        }

        static void set_pipeline_name(VkDevice device, VkPipeline pipeline, const char* name)
        {
            set_object_name(device, (uint64_t)pipeline, VK_OBJECT_TYPE_PIPELINE, name);
        }

        static void set_pipeline_layout_name(VkDevice device, VkPipelineLayout pipelineLayout, const char* name)
        {
            set_object_name(device, (uint64_t)pipelineLayout, VK_OBJECT_TYPE_PIPELINE_LAYOUT, name);
        }

        static void set_render_pass_name(VkDevice device, VkRenderPass renderPass, const char* name)
        {
            set_object_name(device, (uint64_t)renderPass, VK_OBJECT_TYPE_RENDER_PASS, name);
        }

        static void set_framebuffer_name(VkDevice device, VkFramebuffer framebuffer, const char* name)
        {
            set_object_name(device, (uint64_t)framebuffer, VK_OBJECT_TYPE_FRAMEBUFFER, name);
        }

        static void set_descriptor_set_layout_name(VkDevice device, VkDescriptorSetLayout descriptorSetLayout, const char* name)
        {
            set_object_name(device, (uint64_t)descriptorSetLayout, VK_OBJECT_TYPE_DESCRIPTOR_SET_LAYOUT, name);
        }

        static void set_descriptor_set_name(VkDevice device, VkDescriptorSet descriptorSet, const char* name)
        {
            set_object_name(device, (uint64_t)descriptorSet, VK_OBJECT_TYPE_DESCRIPTOR_SET, name);
        }

        static void set_descriptor_pool_name(VkDevice device, VkDescriptorPool descriptorPool, const char* name)
        {
            set_object_name(device, (uint64_t)descriptorPool, VK_OBJECT_TYPE_DESCRIPTOR_POOL, name);
        }

        static void set_semaphore_name(VkDevice device, VkSemaphore semaphore, const char* name)
        {
            set_object_name(device, (uint64_t)semaphore, VK_OBJECT_TYPE_SEMAPHORE, name);
        }

        static void set_fence_name(VkDevice device, VkFence fence, const char* name)
        {
            set_object_name(device, (uint64_t)fence, VK_OBJECT_TYPE_FENCE, name);
        }

        static void set_event_name(VkDevice device, VkEvent _event, const char* name)
        {
            set_object_name(device, (uint64_t)_event, VK_OBJECT_TYPE_EVENT, name);
        }
    };
}

#endif
