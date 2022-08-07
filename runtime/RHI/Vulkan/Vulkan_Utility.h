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

#pragma once

//= INCLUDES ========================
#include "../RHI_Implementation.h"
#include "../RHI_Device.h"
#include "../RHI_Texture.h"
#include "../RHI_SwapChain.h"
#include "../RHI_DepthStencilState.h"
#include "../RHI_Descriptor.h"
#include "../../Logging/Log.h"
#include "../../Math/Vector4.h"
#include "../../Display/Display.h"
#include "../RHI_Fence.h"
//===================================

namespace Spartan::vulkan_utility
{
    namespace error
    {
        inline const char* to_string(const VkResult result)
        {
            switch (result)
            {
                case VK_SUCCESS:                                            return "VK_SUCCESS";
                case VK_NOT_READY:                                          return "VK_NOT_READY";
                case VK_TIMEOUT:                                            return "VK_TIMEOUT";
                case VK_EVENT_SET:                                          return "VK_EVENT_SET";
                case VK_EVENT_RESET:                                        return "VK_EVENT_RESET";
                case VK_INCOMPLETE:                                         return "VK_INCOMPLETE";
                case VK_ERROR_OUT_OF_HOST_MEMORY:                           return "VK_ERROR_OUT_OF_HOST_MEMORY";
                case VK_ERROR_OUT_OF_DEVICE_MEMORY:                         return "VK_ERROR_OUT_OF_DEVICE_MEMORY";
                case VK_ERROR_INITIALIZATION_FAILED:                        return "VK_ERROR_INITIALIZATION_FAILED";
                case VK_ERROR_DEVICE_LOST:                                  return "VK_ERROR_DEVICE_LOST";
                case VK_ERROR_MEMORY_MAP_FAILED:                            return "VK_ERROR_MEMORY_MAP_FAILED";
                case VK_ERROR_LAYER_NOT_PRESENT:                            return "VK_ERROR_LAYER_NOT_PRESENT";
                case VK_ERROR_EXTENSION_NOT_PRESENT:                        return "VK_ERROR_EXTENSION_NOT_PRESENT";
                case VK_ERROR_FEATURE_NOT_PRESENT:                          return "VK_ERROR_FEATURE_NOT_PRESENT";
                case VK_ERROR_INCOMPATIBLE_DRIVER:                          return "VK_ERROR_INCOMPATIBLE_DRIVER";
                case VK_ERROR_TOO_MANY_OBJECTS:                             return "VK_ERROR_TOO_MANY_OBJECTS";
                case VK_ERROR_FORMAT_NOT_SUPPORTED:                         return "VK_ERROR_FORMAT_NOT_SUPPORTED";
                case VK_ERROR_FRAGMENTED_POOL:                              return "VK_ERROR_FRAGMENTED_POOL";
                case VK_ERROR_OUT_OF_POOL_MEMORY:                           return "VK_ERROR_OUT_OF_POOL_MEMORY";
                case VK_ERROR_INVALID_EXTERNAL_HANDLE:                      return "VK_ERROR_INVALID_EXTERNAL_HANDLE";
                case VK_ERROR_SURFACE_LOST_KHR:                             return "VK_ERROR_SURFACE_LOST_KHR";
                case VK_ERROR_NATIVE_WINDOW_IN_USE_KHR:                     return "VK_ERROR_NATIVE_WINDOW_IN_USE_KHR";
                case VK_SUBOPTIMAL_KHR:                                     return "VK_SUBOPTIMAL_KHR";
                case VK_ERROR_OUT_OF_DATE_KHR:                              return "VK_ERROR_OUT_OF_DATE_KHR";
                case VK_ERROR_INCOMPATIBLE_DISPLAY_KHR:                     return "VK_ERROR_INCOMPATIBLE_DISPLAY_KHR";
                case VK_ERROR_VALIDATION_FAILED_EXT:                        return "VK_ERROR_VALIDATION_FAILED_EXT";
                case VK_ERROR_INVALID_SHADER_NV:                            return "VK_ERROR_INVALID_SHADER_NV";
                case VK_ERROR_INVALID_DRM_FORMAT_MODIFIER_PLANE_LAYOUT_EXT: return "VK_ERROR_INVALID_DRM_FORMAT_MODIFIER_PLANE_LAYOUT_EXT";
                case VK_ERROR_FRAGMENTATION_EXT:                            return "VK_ERROR_FRAGMENTATION_EXT";
                case VK_ERROR_NOT_PERMITTED_EXT:                            return "VK_ERROR_NOT_PERMITTED_EXT";
                case VK_ERROR_INVALID_DEVICE_ADDRESS_EXT:                   return "VK_ERROR_INVALID_DEVICE_ADDRESS_EXT";
                case VK_ERROR_FULL_SCREEN_EXCLUSIVE_MODE_LOST_EXT:          return "VK_ERROR_FULL_SCREEN_EXCLUSIVE_MODE_LOST_EXT";
                case VK_ERROR_UNKNOWN:                                      return "VK_ERROR_UNKNOWN";
                case VK_RESULT_MAX_ENUM:                                    return "VK_RESULT_MAX_ENUM";
            }

            return "Unknown error code";
        }

        inline bool check(VkResult result)
        {
            if (result != VK_SUCCESS)
            {
                LOG_ERROR("%s", to_string(result));
            }

            return result == VK_SUCCESS;
        }
    }

    struct globals
    {
        static inline RHI_Device* rhi_device;
        static inline RHI_Context* rhi_context;
    };

    namespace timeline_semaphore
    {
        inline bool create(void*& semaphore, const uint64_t intial_value = 0)
        {
            VkSemaphoreTypeCreateInfo timeline_info = {};
            timeline_info.sType         = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO;
            timeline_info.pNext         = nullptr;
            timeline_info.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE;
            timeline_info.initialValue  = intial_value;
        
            VkSemaphoreCreateInfo semaphore_info = {};
            semaphore_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
            semaphore_info.pNext = &timeline_info;
            semaphore_info.flags = 0;
        
            VkSemaphore* semaphore_vk = reinterpret_cast<VkSemaphore*>(&semaphore);
            return error::check(vkCreateSemaphore(globals::rhi_context->device, &semaphore_info, nullptr, semaphore_vk));
        }
        
        inline void destroy(void*& semaphore)
        {
            if (!semaphore)
                return;
        
            VkSemaphore semaphore_vk = static_cast<VkSemaphore>(semaphore);
            vkDestroySemaphore(globals::rhi_context->device, semaphore_vk, nullptr);
            semaphore = nullptr;
        }

        inline bool wait(void*& semaphore, const uint64_t wait_value, uint64_t timeout = std::numeric_limits<uint64_t>::max())
        {
            if (!semaphore)
                return false;

            VkSemaphoreWaitInfo wait_info = {};
            wait_info.sType          = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO;
            wait_info.pNext          = nullptr;
            wait_info.flags          = 0;
            wait_info.semaphoreCount = 1;
            wait_info.pSemaphores    = reinterpret_cast<VkSemaphore*>(&semaphore);
            wait_info.pValues        = &wait_value;

            return error::check(vkWaitSemaphores(globals::rhi_context->device, &wait_info, timeout));
        }

        inline uint64_t get_counter_value(void*& semaphore)
        {
            if (!semaphore)
                return 0;

            uint64_t value;
            vkGetSemaphoreCounterValue(globals::rhi_context->device, static_cast<VkSemaphore>(semaphore), &value);
            return value;
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
            cmdbi_object()
            {
                m_proccesed_fence = std::make_shared<RHI_Fence>(globals::rhi_device, "cmd_immediate");
            }

            ~cmdbi_object()
            {
                VkCommandPool cmd_pool_vk      = static_cast<VkCommandPool>(cmd_pool);
                VkCommandBuffer* cmd_buffer_vk = reinterpret_cast<VkCommandBuffer*>(&cmd_buffer);

                vkFreeCommandBuffers(globals::rhi_context->device, cmd_pool_vk, 1, cmd_buffer_vk);

                vkDestroyCommandPool(globals::rhi_context->device, static_cast<VkCommandPool>(cmd_pool), nullptr);
                cmd_pool = nullptr;
            }

            void begin(const RHI_Queue_Type queue_type)
            {
                // Wait
                while (recording)
                {
                    std::this_thread::sleep_for(std::chrono::milliseconds(16));
                }

                // Initialise
                if (!initialised)
                {
                    // Create command pool
                    {
                        VkCommandPoolCreateInfo cmd_pool_info = {};
                        cmd_pool_info.sType                   = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
                        cmd_pool_info.queueFamilyIndex        = globals::rhi_device->GetQueueIndex(queue_type);
                        cmd_pool_info.flags                   = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;

                        SP_ASSERT(error::check(
                            vkCreateCommandPool(globals::rhi_context->device, &cmd_pool_info, nullptr, reinterpret_cast<VkCommandPool*>(&cmd_pool))) &&
                            "Failed to created command pool"
                        );
                    }

                    // Create command buffer
                    {
                        VkCommandPool cmd_pool_vk = static_cast<VkCommandPool>(cmd_pool);
                        VkCommandBuffer* cmd_buffer_vk = reinterpret_cast<VkCommandBuffer*>(&cmd_buffer);

                        VkCommandBufferAllocateInfo allocate_info = {};
                        allocate_info.sType                       = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
                        allocate_info.commandPool                 = cmd_pool_vk;
                        allocate_info.level                       = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
                        allocate_info.commandBufferCount          = 1;

                        SP_ASSERT_MSG(error::check(
                            vkAllocateCommandBuffers(globals::rhi_context->device, &allocate_info, cmd_buffer_vk)),
                            "Failed to allocate command buffer"
                        );
                    }

                    initialised = true;
                    this->queue_type = queue_type;
                }

                if (initialised)
                { 
                    // Begin
                    VkCommandBufferBeginInfo begin_info = {};
                    begin_info.sType                    = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
                    begin_info.flags                    = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

                    SP_ASSERT_MSG(error::check(
                        vkBeginCommandBuffer(static_cast<VkCommandBuffer>(cmd_buffer), &begin_info)),
                        "Failed to begin command buffer"
                    );

                    recording = true;
                }
            }

            void submit(const uint32_t wait_flags)
            {
                SP_ASSERT(initialised && "Can't submit as the command buffer failed to initialise");
                SP_ASSERT(recording && "Can't submit as the command buffer didn't record anything");

                SP_ASSERT_MSG(error::check(
                    vkEndCommandBuffer(static_cast<VkCommandBuffer>(cmd_buffer))),
                    "Failed to end command buffer"
                );

                globals::rhi_device->QueueSubmit(queue_type, wait_flags, cmd_buffer, nullptr, nullptr, m_proccesed_fence.get());

                if (!m_proccesed_fence->Wait())
                {
                    LOG_ERROR("Timed out while waiting for fence");
                }
                m_proccesed_fence->Reset();

                recording = false;
            }

            void* cmd_pool                = nullptr;
            void* cmd_buffer              = nullptr;
            RHI_Queue_Type queue_type     = RHI_Queue_Type::Undefined;
            std::atomic<bool> initialised = false;
            std::atomic<bool> recording   = false;
            std::shared_ptr<RHI_Fence> m_proccesed_fence;
        };

        static VkCommandBuffer begin(const RHI_Queue_Type queue_type)
        {
            std::lock_guard<std::mutex> lock(m_mutex_begin);

            cmdbi_object& cmbdi = m_objects[queue_type];

            cmbdi.begin(queue_type);

            return static_cast<VkCommandBuffer>(cmbdi.cmd_buffer);
        }

        static void end(const RHI_Queue_Type queue_type)
        {
            uint32_t wait_flags;
            if (queue_type == RHI_Queue_Type::Graphics)
            {
                wait_flags = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
            }
            else if (queue_type == RHI_Queue_Type::Copy)
            {
                wait_flags = VK_PIPELINE_STAGE_TRANSFER_BIT;
            }

            std::lock_guard<std::mutex> lock(m_mutex_end);
            m_objects[queue_type].submit(wait_flags);
        }

    private:
        static std::mutex m_mutex_begin;
        static std::mutex m_mutex_end;
        static std::unordered_map<RHI_Queue_Type, cmdbi_object> m_objects;
    };

    namespace vma_allocator
    {
        void create_buffer(void*& resource, const uint64_t size, VkBufferUsageFlags usage, VkMemoryPropertyFlags memory_property_flags, const void* data_initial = nullptr);
        void destroy_buffer(void*& resource);
        void* get_mapped_data_from_buffer(void* buffer);
        void create_texture(const VkImageCreateInfo& create_info, void*& resource);
        void destroy_texture(void*& resource);
        void map(void* resource, void*& mapped_data);
        void unmap(void* resource, void*& mapped_data);
        void flush(void* resource, uint64_t offset, uint64_t size);
    }

    namespace image
    {
        static VkImageAspectFlags get_aspect_mask(const RHI_Texture* texture, const bool only_depth = false, const bool only_stencil = false)
        {
            VkImageAspectFlags aspect_mask = 0;

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

        static VkPipelineStageFlags layout_to_access_mask(const VkImageLayout layout, const bool is_destination_mask)
        {
            VkPipelineStageFlags access_mask = 0;

            switch (layout)
            {
            case VK_IMAGE_LAYOUT_UNDEFINED:
                SP_ASSERT(!is_destination_mask && "The new layout used in a transition must not be VK_IMAGE_LAYOUT_UNDEFINED.");
                break;

            case VK_IMAGE_LAYOUT_PREINITIALIZED:
                SP_ASSERT(!is_destination_mask && "The new layout used in a transition must not be VK_IMAGE_LAYOUT_PREINITIALIZED.");
                access_mask = VK_ACCESS_HOST_WRITE_BIT;
                break;

            case VK_IMAGE_LAYOUT_PRESENT_SRC_KHR:
                access_mask = VK_ACCESS_MEMORY_READ_BIT;
                break;

            // Transfer
            case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:
                access_mask = VK_ACCESS_TRANSFER_READ_BIT;
                break;

            case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
                access_mask = VK_ACCESS_TRANSFER_WRITE_BIT;
                break;

            // Color attachments
            case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:
                access_mask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
                break;

            // Depth attachments
            case VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL:
                access_mask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
                break;

            case VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL:
                access_mask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
                break;

            case VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_STENCIL_ATTACHMENT_OPTIMAL:
                access_mask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT;
                break;

            case VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_STENCIL_READ_ONLY_OPTIMAL:
                access_mask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT;
                break;

            // Shader reads
            case VK_IMAGE_LAYOUT_GENERAL:
                access_mask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
                break;

            case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
                access_mask = VK_ACCESS_SHADER_READ_BIT;
                break;

            case VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_OPTIMAL:
                access_mask = VK_ACCESS_SHADER_READ_BIT;
                break;

            case VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL:
                access_mask = VK_ACCESS_SHADER_READ_BIT;
                break;

            default:
                LOG_ERROR("Unexpected image layout");
                break;
            }

            return access_mask;
        }

        static VkPipelineStageFlags access_flags_to_pipeline_stage(VkAccessFlags access_flags)
        {
            VkPipelineStageFlags stages = 0;
            uint32_t enabled_graphics_stages = globals::rhi_device->GetEnabledGraphicsStages();

            while (access_flags != 0)
            {
                VkAccessFlagBits access_flag = static_cast<VkAccessFlagBits>(access_flags & (~(access_flags - 1)));
                SP_ASSERT(access_flag != 0 && (access_flag & (access_flag - 1)) == 0);
                access_flags &= ~access_flag;

                switch (access_flag)
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
                    stages |= enabled_graphics_stages | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
                    break;

                case VK_ACCESS_INPUT_ATTACHMENT_READ_BIT:
                    stages |= VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
                    break;

                // Shader
                case VK_ACCESS_SHADER_READ_BIT:
                    stages |= enabled_graphics_stages | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
                    break;

                case VK_ACCESS_SHADER_WRITE_BIT:
                    stages |= enabled_graphics_stages | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
                    break;

                // Color attachments
                case VK_ACCESS_COLOR_ATTACHMENT_READ_BIT:
                    stages |= VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
                    break;

                case VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT:
                    stages |= VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
                    break;

                // Depth stencil attachments
                case VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT:
                    stages |= VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
                    break;

                case VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT:
                    stages |= VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
                    break;

                // Transfer
                case VK_ACCESS_TRANSFER_READ_BIT:
                    stages |= VK_PIPELINE_STAGE_TRANSFER_BIT;
                    break;

                case VK_ACCESS_TRANSFER_WRITE_BIT:
                    stages |= VK_PIPELINE_STAGE_TRANSFER_BIT;
                    break;

                // Host
                case VK_ACCESS_HOST_READ_BIT:
                    stages |= VK_PIPELINE_STAGE_HOST_BIT;
                    break;

                case VK_ACCESS_HOST_WRITE_BIT:
                    stages |= VK_PIPELINE_STAGE_HOST_BIT;
                    break;
                }
            }
            return stages;
        }

        static void set_layout(void* cmd_buffer, void* image, const VkImageAspectFlags aspect_mask, const uint32_t mip_index, const uint32_t mip_range, const uint32_t array_length, const RHI_Image_Layout layout_old, const RHI_Image_Layout layout_new)
        {
            SP_ASSERT(cmd_buffer != nullptr);
            SP_ASSERT(image != nullptr);

            VkImageMemoryBarrier image_barrier            = {};
            image_barrier.sType                           = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            image_barrier.pNext                           = nullptr;
            image_barrier.oldLayout                       = vulkan_image_layout[static_cast<VkImageLayout>(layout_old)];
            image_barrier.newLayout                       = vulkan_image_layout[static_cast<VkImageLayout>(layout_new)];
            image_barrier.srcQueueFamilyIndex             = VK_QUEUE_FAMILY_IGNORED;
            image_barrier.dstQueueFamilyIndex             = VK_QUEUE_FAMILY_IGNORED;
            image_barrier.image                           = static_cast<VkImage>(image);
            image_barrier.subresourceRange.aspectMask     = aspect_mask;
            image_barrier.subresourceRange.baseMipLevel   = mip_index;
            image_barrier.subresourceRange.levelCount     = mip_range;
            image_barrier.subresourceRange.baseArrayLayer = 0;
            image_barrier.subresourceRange.layerCount     = array_length;
            image_barrier.srcAccessMask                   = layout_to_access_mask(image_barrier.oldLayout, false);
            image_barrier.dstAccessMask                   = layout_to_access_mask(image_barrier.newLayout, true);

            VkPipelineStageFlags source_stage_mask = 0;
            {
                if (image_barrier.oldLayout == VK_IMAGE_LAYOUT_PRESENT_SRC_KHR)
                {
                    source_stage_mask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
                }
                else if (image_barrier.oldLayout == VK_IMAGE_LAYOUT_UNDEFINED)
                {
                    source_stage_mask = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
                }
                else
                {
                    source_stage_mask = access_flags_to_pipeline_stage(image_barrier.srcAccessMask);
                }
            }

            VkPipelineStageFlags destination_stage_mask = 0;
            {
                if (image_barrier.newLayout == VK_IMAGE_LAYOUT_PRESENT_SRC_KHR)
                {
                    destination_stage_mask = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
                }
                else
                {
                    destination_stage_mask = access_flags_to_pipeline_stage(image_barrier.dstAccessMask);
                }
            }

            vkCmdPipelineBarrier
            (
                static_cast<VkCommandBuffer>(cmd_buffer), // commandBuffer
                source_stage_mask,                        // srcStageMask
                destination_stage_mask,                   // dstStageMask
                0,                                        // dependencyFlags
                0,                                        // memoryBarrierCount
                nullptr,                                  // pMemoryBarriers
                0,                                        // bufferMemoryBarrierCount
                nullptr,                                  // pBufferMemoryBarriers
                1,                                        // imageMemoryBarrierCount
                &image_barrier                            // pImageMemoryBarriers
            );
        }

        static void set_layout(void* cmd_buffer, RHI_Texture* texture, const uint32_t mip_start, const uint32_t mip_range, const uint32_t array_length, const RHI_Image_Layout layout_old, const RHI_Image_Layout layout_new)
        {
            SP_ASSERT(cmd_buffer != nullptr);
            SP_ASSERT(texture != nullptr);

            set_layout(cmd_buffer, texture->GetRhiResource(), get_aspect_mask(texture), mip_start, mip_range, array_length, layout_old, layout_new);
        }

        namespace view
        {
            inline bool create(
                void* image,
                void*& image_view,
                VkImageViewType type,
                const VkFormat format,
                const VkImageAspectFlags aspect_mask,
                const uint32_t array_index,
                const uint32_t array_length,
                const uint32_t mip_index,
                const uint32_t mip_count
            )
            {
                VkImageViewCreateInfo create_info           = {};
                create_info.sType                           = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
                create_info.image                           = static_cast<VkImage>(image);
                create_info.viewType                        = type;
                create_info.format                          = format;
                create_info.subresourceRange.aspectMask     = aspect_mask;
                create_info.subresourceRange.baseMipLevel   = mip_index;
                create_info.subresourceRange.levelCount     = mip_count;
                create_info.subresourceRange.baseArrayLayer = array_index;
                create_info.subresourceRange.layerCount     = array_length;
                create_info.components.r                    = VK_COMPONENT_SWIZZLE_IDENTITY;
                create_info.components.g                    = VK_COMPONENT_SWIZZLE_IDENTITY;
                create_info.components.b                    = VK_COMPONENT_SWIZZLE_IDENTITY;
                create_info.components.a                    = VK_COMPONENT_SWIZZLE_IDENTITY;

                return error::check(vkCreateImageView(globals::rhi_context->device, &create_info, nullptr, reinterpret_cast<VkImageView*>(&image_view)));
            }

            inline bool create(
                void* image,
                void*& image_view,
                const RHI_Texture* texture,
                const uint32_t array_index,
                const uint32_t array_length,
                const uint32_t mip_index,
                const uint32_t mip_count,
                const bool only_depth,
                const bool only_stencil
            )
            {
                VkImageViewType type = VK_IMAGE_VIEW_TYPE_MAX_ENUM;

                if (texture->GetResourceType() == ResourceType::Texture2d)
                {
                    type = VK_IMAGE_VIEW_TYPE_2D;
                }
                else if (texture->GetResourceType() == ResourceType::Texture2dArray)
                {
                    type = VK_IMAGE_VIEW_TYPE_2D_ARRAY;
                }
                else if (texture->GetResourceType() == ResourceType::TextureCube)
                {
                    type = VK_IMAGE_VIEW_TYPE_CUBE;
                }

                return create(image, image_view, type, vulkan_format[texture->GetFormat()], get_aspect_mask(texture, only_depth, only_stencil), array_index, array_length, mip_index, mip_count);
            }

            inline void destroy(void*& image_view)
            {
                if (!image_view)
                    return;

                vkDestroyImageView(globals::rhi_context->device, static_cast<VkImageView>(image_view), nullptr);
                image_view = nullptr;
            }

            inline void destroy(std::array<void*, 3>& image_views)
            {
                for (void*& image_view : image_views)
                {
                    if (image_view)
                    {
                        vkDestroyImageView(globals::rhi_context->device, static_cast<VkImageView>(image_view), nullptr);
                    }
                }
                image_views.fill(nullptr);
            }
        }
    }

    class functions
    {
    public:
        functions() = default;
        ~functions() = default;

        static void initialize()
        {
            #define get_func(var, def)\
            var = reinterpret_cast<PFN_##def>(vkGetInstanceProcAddr(static_cast<VkInstance>(globals::rhi_context->instance), #def));\
            if (!var) LOG_ERROR("Failed to get function pointer for %s", #def);\

            get_func(get_physical_device_memory_properties_2, vkGetPhysicalDeviceMemoryProperties2);

            if (globals::rhi_context->validation)
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

        static PFN_vkCreateDebugUtilsMessengerEXT          create_messenger;
        static VkDebugUtilsMessengerEXT                    messenger;
        static PFN_vkDestroyDebugUtilsMessengerEXT         destroy_messenger;
        static PFN_vkSetDebugUtilsObjectTagEXT             set_object_tag;
        static PFN_vkSetDebugUtilsObjectNameEXT            set_object_name;
        static PFN_vkCmdBeginDebugUtilsLabelEXT            marker_begin;
        static PFN_vkCmdEndDebugUtilsLabelEXT              marker_end;
        static PFN_vkGetPhysicalDeviceMemoryProperties2KHR get_physical_device_memory_properties_2;
    };

    class debug
    {
    public:
        debug() = default;
        ~debug() = default;

        static VKAPI_ATTR VkBool32 VKAPI_CALL callback(VkDebugUtilsMessageSeverityFlagBitsEXT msg_severity, VkDebugUtilsMessageTypeFlagsEXT msg_type, const VkDebugUtilsMessengerCallbackDataEXT* p_callback_data, void* p_user_data)
        {
            std::string msg = "Vulkan: " + std::string(p_callback_data->pMessage);

            if (msg_severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT)
            {
                Log::Write(msg.c_str(), LogType::Info);
            }
            else if (msg_severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT)
            {
                Log::Write(msg.c_str(), LogType::Info);
            }
            else if (msg_severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT)
            {
                //Log::Write(msg.c_str(), LogType::Warning);
            }
            else if (msg_severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT)
            {
                //Log::Write(msg.c_str(), LogType::Error);
            }

            return VK_FALSE;
        }

        static void initialize(VkInstance instance)
        {
            if (functions::create_messenger)
            {
                VkDebugUtilsMessengerCreateInfoEXT create_info = {};
                create_info.sType                              = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
                create_info.messageSeverity                    = VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
                create_info.messageType                        = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
                create_info.pfnUserCallback                    = callback;

                functions::create_messenger(instance, &create_info, nullptr, &functions::messenger);
            }
        }

        static void shutdown(VkInstance instance)
        {
            if (!functions::destroy_messenger)
                return;

            functions::destroy_messenger(instance, functions::messenger, nullptr);
        }

        static void set_object_name(uint64_t object, VkObjectType object_type, const char* name)
        {
            if (!functions::set_object_name)
                return;

            VkDebugUtilsObjectNameInfoEXT name_info = {};
            name_info.sType                         = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT;
            name_info.pNext                         = nullptr;
            name_info.objectType                    = object_type;
            name_info.objectHandle                  = object;
            name_info.pObjectName                   = name;

            functions::set_object_name(globals::rhi_context->device, &name_info);
        }

        static void set_object_tag(uint64_t object, VkObjectType objectType, uint64_t name, size_t tagSize, const void* tag)
        {
            if (!functions::set_object_tag)
                return;

            VkDebugUtilsObjectTagInfoEXT tag_info = {};
            tag_info.sType                        = VK_STRUCTURE_TYPE_DEBUG_MARKER_OBJECT_TAG_INFO_EXT;
            tag_info.pNext                        = nullptr;
            tag_info.objectType                   = objectType;
            tag_info.objectHandle                 = object;
            tag_info.tagName                      = name;
            tag_info.tagSize                      = tagSize;
            tag_info.pTag                         = tag;

            functions::set_object_tag(globals::rhi_context->device, &tag_info);
        }

        static void marker_begin(VkCommandBuffer cmd_buffer, const char* name, const Math::Vector4& color)
        {
            if (!functions::marker_begin)
                return;

            VkDebugUtilsLabelEXT label = {};
            label.sType                = VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT;
            label.pNext                = nullptr;
            label.pLabelName           = name;
            label.color[0]             = color.x;
            label.color[1]             = color.y;
            label.color[2]             = color.z;
            label.color[3]             = color.w;

            functions::marker_begin(cmd_buffer, &label);
        }

        static void marker_end(VkCommandBuffer cmd_buffer)
        {
            if (!functions::marker_end)
                return;

            functions::marker_end(cmd_buffer);
        }

        static void set_object_name(VkCommandPool object, const char* name)
        {
            set_object_name((uint64_t)object, VK_OBJECT_TYPE_COMMAND_POOL, name);
        }

        static void set_object_name(VkCommandBuffer object, const char* name)
        {
            set_object_name((uint64_t)object, VK_OBJECT_TYPE_COMMAND_BUFFER, name);
        }

        static void set_object_name(VkQueue object, const char* name)
        {
            set_object_name((uint64_t)object, VK_OBJECT_TYPE_QUEUE, name);
        }

        static void set_object_name(VkImage object, const char* name)
        {
            set_object_name((uint64_t)object, VK_OBJECT_TYPE_IMAGE, name);
        }

        static void set_object_name(VkImageView object, const char* name)
        {
            set_object_name((uint64_t)object, VK_OBJECT_TYPE_IMAGE_VIEW, name);
        }

        static void set_object_name(VkSampler object, const char* name)
        {
            set_object_name((uint64_t)object, VK_OBJECT_TYPE_SAMPLER, name);
        }

        static void set_object_name(VkBuffer object, const char* name)
        {
            set_object_name((uint64_t)object, VK_OBJECT_TYPE_BUFFER, name);
        }

        static void set_object_name(VkBufferView object, const char* name)
        {
            set_object_name((uint64_t)object, VK_OBJECT_TYPE_BUFFER_VIEW, name);
        }

        static void set_object_name(VkDeviceMemory object, const char* name)
        {
            set_object_name((uint64_t)object, VK_OBJECT_TYPE_DEVICE_MEMORY, name);
        }

        static void set_object_name(VkShaderModule object, const char* name)
        {
            set_object_name((uint64_t)object, VK_OBJECT_TYPE_SHADER_MODULE, name);
        }

        static void set_object_name(VkPipeline object, const char* name)
        {
            set_object_name((uint64_t)object, VK_OBJECT_TYPE_PIPELINE, name);
        }

        static void set_object_name(VkPipelineLayout object, const char* name)
        {
            set_object_name((uint64_t)object, VK_OBJECT_TYPE_PIPELINE_LAYOUT, name);
        }

        static void set_object_name(VkRenderPass object, const char* name)
        {
            set_object_name((uint64_t)object, VK_OBJECT_TYPE_RENDER_PASS, name);
        }

        static void set_object_name(VkFramebuffer object, const char* name)
        {
            set_object_name((uint64_t)object, VK_OBJECT_TYPE_FRAMEBUFFER, name);
        }

        static void set_object_name(VkDescriptorSetLayout object, const char* name)
        {
            set_object_name((uint64_t)object, VK_OBJECT_TYPE_DESCRIPTOR_SET_LAYOUT, name);
        }

        static void set_object_name(VkDescriptorSet object, const char* name)
        {
            set_object_name((uint64_t)object, VK_OBJECT_TYPE_DESCRIPTOR_SET, name);
        }

        static void set_object_name(VkDescriptorPool object, const char* name)
        {
            set_object_name((uint64_t)object, VK_OBJECT_TYPE_DESCRIPTOR_POOL, name);
        }

        static void set_object_name(VkSemaphore object, const char* name)
        {
            set_object_name((uint64_t)object, VK_OBJECT_TYPE_SEMAPHORE, name);
        }

        static void set_object_name(VkFence object, const char* name)
        {
            set_object_name((uint64_t)object, VK_OBJECT_TYPE_FENCE, name);
        }

        static void set_object_name(VkEvent object, const char* name)
        {
            set_object_name((uint64_t)object, VK_OBJECT_TYPE_EVENT, name);
        }
    };

    static VkDescriptorType ToVulkanDescriptorType(const RHI_Descriptor& descriptor)
    {
        if (descriptor.type == RHI_Descriptor_Type::Sampler)
            return VkDescriptorType::VK_DESCRIPTOR_TYPE_SAMPLER;

        if (descriptor.type == RHI_Descriptor_Type::Texture)
            return VkDescriptorType::VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;

        if (descriptor.type == RHI_Descriptor_Type::TextureStorage)
            return VkDescriptorType::VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;

        if (descriptor.type == RHI_Descriptor_Type::StructuredBuffer)
            return VkDescriptorType::VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;

        if (descriptor.type == RHI_Descriptor_Type::ConstantBuffer)
            return VkDescriptorType::VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;

        LOG_ERROR("Invalid descriptor type");
        return VkDescriptorType::VK_DESCRIPTOR_TYPE_MAX_ENUM;
    }
}
