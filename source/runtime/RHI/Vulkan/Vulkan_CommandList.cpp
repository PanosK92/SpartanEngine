/*
Copyright(c) 2015-2026 Panos Karabelas

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
#include "../RHI_Device.h"
#include "../RHI_Queue.h"
#include "../RHI_Implementation.h"
#include "../RHI_Pipeline.h"
#include "../RHI_Buffer.h"
#include "../RHI_DescriptorSet.h"
#include "../RHI_DescriptorSetLayout.h"
#include "../RHI_SyncPrimitive.h"
#include "../RHI_SwapChain.h"
#include "../RHI_RasterizerState.h"
#include "../RHI_DepthStencilState.h"
#include "../RHI_VendorTechnology.h"
#include "../Rendering/Renderer.h"
#include "../../Profiling/Profiler.h"
#include "../Core/Debugging.h"
//=====================================

//= NAMESPACES ===============
using namespace std;
using namespace spartan::math;
//============================

namespace spartan
{
    namespace
    {
        VkAttachmentLoadOp get_color_load_op(const Color& color)
        {
            if (color == rhi_color_dont_care)
                return VK_ATTACHMENT_LOAD_OP_DONT_CARE;

            if (color == rhi_color_load)
                return VK_ATTACHMENT_LOAD_OP_LOAD;

            return VK_ATTACHMENT_LOAD_OP_CLEAR;
        };

        VkAttachmentLoadOp get_depth_load_op(const float depth)
        {
            if (depth == rhi_depth_dont_care)
                return VK_ATTACHMENT_LOAD_OP_DONT_CARE;

            if (depth == rhi_depth_load)
                return VK_ATTACHMENT_LOAD_OP_LOAD;

            return VK_ATTACHMENT_LOAD_OP_CLEAR;
        };

        uint32_t get_aspect_mask(const RHI_Format format)
        {
            switch (format)
            {
            case RHI_Format::D32_Float_S8X24_Uint:
                return VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
            case RHI_Format::D16_Unorm:
            case RHI_Format::D32_Float:
                return VK_IMAGE_ASPECT_DEPTH_BIT;
            default:
                return VK_IMAGE_ASPECT_COLOR_BIT;
            }
        }
    }

    namespace barrier_helpers
    {
        unordered_map<void*, array<RHI_Image_Layout, rhi_max_mip_count>> image_layouts;
        mutex image_layouts_mutex;

        RHI_Image_Layout get_layout(void* image, uint32_t mip_index)
        {
            SP_ASSERT(image != nullptr);
            lock_guard<mutex> lock(image_layouts_mutex);

            auto it = image_layouts.find(image);
            if (it == image_layouts.end())
                return RHI_Image_Layout::Max;

            SP_ASSERT(mip_index < rhi_max_mip_count);
            return it->second[mip_index];
        }

        void set_layout(void* image, uint32_t mip_index, uint32_t mip_range, RHI_Image_Layout layout)
        {
            SP_ASSERT(image != nullptr);
            SP_ASSERT(mip_index < rhi_max_mip_count);
            SP_ASSERT(mip_index + mip_range <= rhi_max_mip_count);
            lock_guard<mutex> lock(image_layouts_mutex);

            auto it = image_layouts.find(image);
            if (it == image_layouts.end())
            {
                array<RHI_Image_Layout, rhi_max_mip_count> layouts;
                layouts.fill(RHI_Image_Layout::Max);
                image_layouts[image] = layouts;
                it = image_layouts.find(image);
            }

            uint32_t mip_end = min(mip_index + mip_range, rhi_max_mip_count);
            for (uint32_t i = mip_index; i < mip_end; ++i)
            {
                it->second[i] = layout;
            }
        }

        void remove_layout(void* image)
        {
            lock_guard<mutex> lock(image_layouts_mutex);
            image_layouts.erase(image);
        }

        // convert scope enum to vulkan pipeline stages
        VkPipelineStageFlags2 scope_to_stages(RHI_Barrier_Scope scope, bool is_depth = false)
        {
            switch (scope)
            {
                case RHI_Barrier_Scope::Graphics:
                    return VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT |
                           VK_PIPELINE_STAGE_2_TESSELLATION_CONTROL_SHADER_BIT |
                           VK_PIPELINE_STAGE_2_TESSELLATION_EVALUATION_SHADER_BIT |
                           VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT |
                           (is_depth ? (VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT) : VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT);
                case RHI_Barrier_Scope::Compute:
                    return VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
                case RHI_Barrier_Scope::Transfer:
                    return VK_PIPELINE_STAGE_2_TRANSFER_BIT;
                case RHI_Barrier_Scope::Fragment:
                    return VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
                case RHI_Barrier_Scope::All:
                    return VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
                case RHI_Barrier_Scope::Auto:
                default:
                    return VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT; // auto handled by layout-based deduction
            }
        }

        // get sync info from layout (used when scope is Auto)
        tuple<VkPipelineStageFlags2, VkAccessFlags2> get_layout_sync_info(const VkImageLayout layout, const bool is_destination_mask, const bool is_depth)
        {
            switch (layout)
            {
                case VK_IMAGE_LAYOUT_UNDEFINED:
                    if (!is_destination_mask)
                    {
                        return make_tuple(
                            VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT,
                            VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT
                        );
                    }
                    else
                    {
                        SP_ASSERT_MSG(false, "new layout must not be VK_IMAGE_LAYOUT_UNDEFINED");
                        return make_tuple(VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT, VK_ACCESS_2_NONE);
                    }

                case VK_IMAGE_LAYOUT_PREINITIALIZED:
                    SP_ASSERT_MSG(!is_destination_mask, "new layout must not be VK_IMAGE_LAYOUT_PREINITIALIZED");
                    return make_tuple(VK_PIPELINE_STAGE_2_HOST_BIT, VK_ACCESS_2_HOST_WRITE_BIT);

                case VK_IMAGE_LAYOUT_PRESENT_SRC_KHR:
                    if (!is_destination_mask)
                        return make_tuple(VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT, VK_ACCESS_2_NONE);
                    else
                        return make_tuple(VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT, VK_ACCESS_2_NONE);

                case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:
                    return make_tuple(VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_READ_BIT);

                case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
                    return make_tuple(VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT);

                case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
                    return make_tuple(
                        VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT |
                        VK_PIPELINE_STAGE_2_TESSELLATION_CONTROL_SHADER_BIT |
                        VK_PIPELINE_STAGE_2_TESSELLATION_EVALUATION_SHADER_BIT |
                        VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT |
                        VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                        VK_ACCESS_2_SHADER_READ_BIT | VK_ACCESS_2_SHADER_SAMPLED_READ_BIT
                    );

                case VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL:
                    if (is_depth)
                    {
                        return make_tuple(
                            VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
                            VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT_KHR | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT_KHR
                        );
                    }
                    else
                    {
                        return make_tuple(
                            VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                            VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT
                        );
                    }

                case VK_IMAGE_LAYOUT_FRAGMENT_SHADING_RATE_ATTACHMENT_OPTIMAL_KHR:
                    return make_tuple(
                        VK_PIPELINE_STAGE_2_FRAGMENT_SHADING_RATE_ATTACHMENT_BIT_KHR,
                        VK_ACCESS_2_FRAGMENT_SHADING_RATE_ATTACHMENT_READ_BIT_KHR
                    );

                case VK_IMAGE_LAYOUT_GENERAL:
                    return make_tuple(
                        VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                        VK_ACCESS_2_SHADER_READ_BIT | VK_ACCESS_2_SHADER_WRITE_BIT
                    );

                default:
                    SP_ASSERT_MSG(false, "unhandled layout transition");
                    return make_tuple(
                        VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                        VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT
                    );
            }
        }

        VkImageMemoryBarrier2 create_image_barrier(
            const RHI_Image_Layout layout_old,
            const RHI_Image_Layout layout_new,
            void* image,
            uint32_t aspect_mask,
            uint32_t mip_index,
            uint32_t mip_range,
            uint32_t array_length,
            bool is_depth,
            RHI_Barrier_Scope scope_src = RHI_Barrier_Scope::Auto,
            RHI_Barrier_Scope scope_dst = RHI_Barrier_Scope::Auto
        )
        {
            VkImageMemoryBarrier2 barrier           = {};
            barrier.sType                           = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2_KHR;
            barrier.pNext                           = nullptr;
            barrier.oldLayout                       = vulkan_image_layout[static_cast<uint32_t>(layout_old)];
            barrier.newLayout                       = vulkan_image_layout[static_cast<uint32_t>(layout_new)];
            barrier.srcQueueFamilyIndex             = VK_QUEUE_FAMILY_IGNORED;
            barrier.dstQueueFamilyIndex             = VK_QUEUE_FAMILY_IGNORED;
            barrier.image                           = static_cast<VkImage>(image);
            barrier.subresourceRange.aspectMask     = aspect_mask;
            barrier.subresourceRange.baseMipLevel   = mip_index;
            barrier.subresourceRange.levelCount     = mip_range;
            barrier.subresourceRange.baseArrayLayer = 0;
            barrier.subresourceRange.layerCount     = array_length;

            // use explicit scope if provided, otherwise deduce from layout
            if (scope_src != RHI_Barrier_Scope::Auto)
            {
                barrier.srcStageMask  = scope_to_stages(scope_src, is_depth);
                barrier.srcAccessMask = VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT;
            }
            else
            {
                auto [src_stages, src_access] = get_layout_sync_info(barrier.oldLayout, false, is_depth);
                barrier.srcStageMask  = src_stages;
                barrier.srcAccessMask = src_access;
            }

            if (scope_dst != RHI_Barrier_Scope::Auto)
            {
                barrier.dstStageMask  = scope_to_stages(scope_dst, is_depth);
                barrier.dstAccessMask = VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT;
            }
            else
            {
                auto [dst_stages, dst_access] = get_layout_sync_info(barrier.newLayout, true, is_depth);
                barrier.dstStageMask  = dst_stages;
                barrier.dstAccessMask = dst_access;
            }

            return barrier;
        }
    }

    namespace descriptor_sets
    {
        bool bind_dynamic = false;

        void set_dynamic(const RHI_PipelineState pso, void* resource, void* pipeline_layout, RHI_DescriptorSetLayout* layout)
        {
            array<void*, 1> resources =
            {
                layout->GetOrCreateDescriptorSet()
            };

            // get dynamic offsets
            array<uint32_t, 10> dynamic_offsets;
            uint32_t dynamic_offset_count = 0;
            layout->GetDynamicOffsets(&dynamic_offsets, &dynamic_offset_count);

            VkPipelineBindPoint bind_point = VK_PIPELINE_BIND_POINT_COMPUTE;
            bind_point                     = pso.IsGraphics()   ? VK_PIPELINE_BIND_POINT_GRAPHICS        : bind_point;
            bind_point                     = pso.IsRayTracing() ? VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR : bind_point;

            vkCmdBindDescriptorSets
            (
                static_cast<VkCommandBuffer>(resource),               // commandBuffer
                bind_point,                                           // pipelineBindPoint
                static_cast<VkPipelineLayout>(pipeline_layout),       // layout
                0,                                                    // firstSet
                static_cast<uint32_t>(resources.size()),              // descriptorSetCount
                reinterpret_cast<VkDescriptorSet*>(resources.data()), // pDescriptorSets
                dynamic_offset_count,                                 // dynamicOffsetCount
                dynamic_offsets.data()                                // pDynamicOffsets
            );

            bind_dynamic = false;
        }

        void set_bindless(const RHI_PipelineState pso, void* resource, void* pipeline_layout)
        {
            array<void*, static_cast<size_t>(RHI_Device_Bindless_Resource::Max)> resources;
            for (size_t i = 0; i < static_cast<size_t>(resources.size()); i++)
            {
                resources[i] = RHI_Device::GetDescriptorSet(static_cast<RHI_Device_Bindless_Resource>(i));
            }

            VkPipelineBindPoint bind_point = VK_PIPELINE_BIND_POINT_COMPUTE;
            bind_point                     = pso.IsGraphics()   ? VK_PIPELINE_BIND_POINT_GRAPHICS        : bind_point;
            bind_point                     = pso.IsRayTracing() ? VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR : bind_point;

            vkCmdBindDescriptorSets
            (
                static_cast<VkCommandBuffer>(resource),               // commandBuffer
                bind_point,                                           // pipelineBindPoint
                static_cast<VkPipelineLayout>(pipeline_layout),       // layout
                1,                                                    // firstSet - 0 is reserved for the old-school/dynamic/bind based descriptor sets
                static_cast<uint32_t>(resources.size()),              // descriptorSetCount
                reinterpret_cast<VkDescriptorSet*>(resources.data()), // pDescriptorSets
                0,                                                    // dynamicOffsetCount
                nullptr                                               // pDynamicOffsets
            );
        }
    }

    namespace queries
    {
        namespace timestamp
        {
            const uint32_t query_count = 256;
            array<uint64_t, query_count> data;

            void update(void* query_pool)
            {
                if (Debugging::IsGpuTimingEnabled())
                {
                    vkGetQueryPoolResults(
                        RHI_Context::device,                  // device
                        static_cast<VkQueryPool>(query_pool), // queryPool
                        0,                                    // firstQuery
                        query_count,                          // queryCount
                        query_count * sizeof(uint64_t),       // dataSize
                        data.data(),                          // pData
                        sizeof(uint64_t),                     // stride
                        VK_QUERY_RESULT_64_BIT                // flags
                    );
                }
            }

            void reset(void* cmd_list, void*& query_pool)
            {
                if (Debugging::IsGpuTimingEnabled())
                {
                    vkCmdResetQueryPool(static_cast<VkCommandBuffer>(cmd_list), static_cast<VkQueryPool>(query_pool), 0, query_count);
                }
            }
        }

        namespace occlusion
        {
            uint32_t index              = 0;
            uint32_t index_active       = 0;
            bool occlusion_query_active = false;
            const uint32_t query_count  = 4096;
            array<uint64_t, query_count> data;
            unordered_map<uint64_t, uint32_t> id_to_index;

            void update(void* query_pool)
            {
                vkGetQueryPoolResults(
                    RHI_Context::device,                                 // device
                    static_cast<VkQueryPool>(query_pool),                // queryPool
                    0,                                                   // firstQuery
                    query_count,                                         // queryCount
                    query_count * sizeof(uint64_t),                      // dataSize
                    data.data(),                                         // pData
                    sizeof(uint64_t),                                    // stride
                    VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_PARTIAL_BIT // flags
                );
            }

            void reset(void* cmd_list, void*& query_pool)
            {
                vkCmdResetQueryPool(static_cast<VkCommandBuffer>(cmd_list), static_cast<VkQueryPool>(query_pool), 0, query_count);
            }
        }

        void initialize(void*& pool_timestamp, void*& pool_occlusion, void*& pool_pipeline_statistics)
        {
            // timestamps
            if (Debugging::IsGpuTimingEnabled())
            {
                VkQueryPoolCreateInfo query_pool_info = {};
                query_pool_info.sType                 = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
                query_pool_info.queryType             = VK_QUERY_TYPE_TIMESTAMP;
                query_pool_info.queryCount            = timestamp::query_count;

                auto query_pool = reinterpret_cast<VkQueryPool*>(&pool_timestamp);
                SP_ASSERT_VK(vkCreateQueryPool(RHI_Context::device, &query_pool_info, nullptr, query_pool));
                RHI_Device::SetResourceName(pool_timestamp, RHI_Resource_Type::QueryPool, "query_pool_timestamp");

                timestamp::data.fill(0);
            }

            // occlusion
            {
                VkQueryPoolCreateInfo query_pool_info = {};
                query_pool_info.sType                 = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
                query_pool_info.queryType             = VK_QUERY_TYPE_OCCLUSION;
                query_pool_info.queryCount            = occlusion::query_count;

                auto query_pool = reinterpret_cast<VkQueryPool*>(&pool_occlusion);
                SP_ASSERT_VK(vkCreateQueryPool(RHI_Context::device, &query_pool_info, nullptr, query_pool));
                RHI_Device::SetResourceName(pool_occlusion, RHI_Resource_Type::QueryPool, "query_pool_occlusion");

                occlusion::data.fill(0);
            }
        }

        void shutdown(void*& pool_timestamp, void*& pool_occlusion, void*& pool_pipeline_statistics)
        {
            RHI_Device::DeletionQueueAdd(RHI_Resource_Type::QueryPool, pool_timestamp);
            RHI_Device::DeletionQueueAdd(RHI_Resource_Type::QueryPool, pool_occlusion);
            RHI_Device::DeletionQueueAdd(RHI_Resource_Type::QueryPool, pool_pipeline_statistics);
        }
    }

    namespace immediate_execution
    {
        mutex mutex_execution;
        condition_variable condition_var;
        bool is_executing = false;
        array<shared_ptr<RHI_Queue>, static_cast<uint32_t>(RHI_Queue_Type::Max)> queues; // graphics, compute, and copy
        once_flag init_flag;
    
        // initialize queues on first use
        void ensure_initialized()
        {
            call_once(init_flag, []()
            {
                queues[static_cast<uint32_t>(RHI_Queue_Type::Graphics)] = make_shared<RHI_Queue>(RHI_Queue_Type::Graphics, "graphics");
                queues[static_cast<uint32_t>(RHI_Queue_Type::Compute)]  = make_shared<RHI_Queue>(RHI_Queue_Type::Compute,  "compute");
                queues[static_cast<uint32_t>(RHI_Queue_Type::Copy)]     = make_shared<RHI_Queue>(RHI_Queue_Type::Copy,     "copy");
            });
        }
    }

    RHI_CommandList::RHI_CommandList(RHI_Queue* queue, void* cmd_pool, const char* name)
    {
        m_queue = queue;

        // command buffer
        {
            // define
            VkCommandBufferAllocateInfo allocate_info = {};
            allocate_info.sType                       = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
            allocate_info.commandPool                 = static_cast<VkCommandPool>(cmd_pool);
            allocate_info.level                       = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
            allocate_info.commandBufferCount          = 1;

            // allocate
            SP_ASSERT_VK(vkAllocateCommandBuffers(RHI_Context::device, &allocate_info, reinterpret_cast<VkCommandBuffer*>(&m_rhi_resource)));

            // name
            RHI_Device::SetResourceName(static_cast<void*>(m_rhi_resource), RHI_Resource_Type::CommandList, name);
            m_object_name = name;
        }

        // semaphores
        m_rendering_complete_semaphore          = make_shared<RHI_SyncPrimitive>(RHI_SyncPrimitive_Type::Semaphore, (string(name) + "_binary").c_str());
        m_rendering_complete_semaphore_timeline = make_shared<RHI_SyncPrimitive>(RHI_SyncPrimitive_Type::SemaphoreTimeline, (string(name) + "timeline").c_str());

        queries::initialize(m_rhi_query_pool_timestamps, m_rhi_query_pool_occlusion, m_rhi_query_pool_pipeline_statistics);
    }

    RHI_CommandList::~RHI_CommandList()
    {
        queries::shutdown(m_rhi_query_pool_timestamps, m_rhi_query_pool_occlusion, m_rhi_query_pool_pipeline_statistics);
    }

    void RHI_CommandList::Begin()
    {
        SP_ASSERT(m_state == RHI_CommandListState::Idle);
     
        // begin command buffer
        VkCommandBufferBeginInfo begin_info = {};
        begin_info.sType                    = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        SP_ASSERT_MSG(vkBeginCommandBuffer(static_cast<VkCommandBuffer>(m_rhi_resource), &begin_info) == VK_SUCCESS, "Failed to begin command buffer");
    
        // enable breadcrumbs for this command list
        if (Debugging::IsBreadcrumbsEnabled())
        {
            RHI_VendorTechnology::Breadcrumbs_RegisterCommandList(this, m_queue, m_object_name.c_str());
        }
    
        // set states
        m_state     = RHI_CommandListState::Recording;
        m_pso       = RHI_PipelineState();
        m_cull_mode = RHI_CullMode::Max;
    
        // set dynamic states
        if (m_queue->GetType() == RHI_Queue_Type::Graphics)
        {
            // cull mode
            SetCullMode(RHI_CullMode::Back);
    
            // scissor rectangle
            math::Rectangle scissor_rect;
            scissor_rect.x      = 0.0f;
            scissor_rect.y      = 0.0f;
            scissor_rect.width  = static_cast<float>(m_pso.GetWidth());
            scissor_rect.height = static_cast<float>(m_pso.GetHeight());
            SetScissorRectangle(scissor_rect);
        }
    
        // queries
        if (m_queue->GetType() != RHI_Queue_Type::Copy)
        {
            if (m_timestamp_index != 0)
            {
                queries::timestamp::update(m_rhi_query_pool_timestamps);
            }
    
            // queries need to be reset before they are first used and they
            // also need to be reset after every use, so we just reset them always
            m_timestamp_index = 0;
            queries::timestamp::reset(m_rhi_resource, m_rhi_query_pool_timestamps);
            queries::occlusion::reset(m_rhi_resource, m_rhi_query_pool_occlusion);
        }
    }

    void RHI_CommandList::Submit(RHI_SyncPrimitive* semaphore_wait, const bool is_immediate, RHI_SyncPrimitive* semaphore_signal /*= nullptr*/)
    {
        SP_ASSERT(m_state == RHI_CommandListState::Recording);

        // end
        RenderPassEnd();
        SP_ASSERT_VK(vkEndCommandBuffer(static_cast<VkCommandBuffer>(m_rhi_resource)));

        // determine which binary semaphore to signal:
        // - if external semaphore provided (e.g. per-swapchain-image), use that
        // - if immediate mode, no binary semaphore (timeline only)
        // - otherwise use the command list's binary semaphore
        RHI_SyncPrimitive* semaphore_binary = semaphore_signal ? semaphore_signal : (is_immediate ? nullptr : m_rendering_complete_semaphore.get());

        m_queue->Submit(
            static_cast<VkCommandBuffer>(m_rhi_resource), // cmd buffer
            VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,            // wait flags
            semaphore_wait,                               // wait semaphore
            semaphore_binary,                             // signal semaphore
            m_rendering_complete_semaphore_timeline.get() // signal semaphore
        );

        if (semaphore_wait)
        {
            semaphore_wait->SetUserCmdList(this);
        }

        m_state = RHI_CommandListState::Submitted;
    }

    void RHI_CommandList::WaitForExecution(const bool log_wait_time /*= false*/)
    {
        SP_ASSERT_MSG(m_state == RHI_CommandListState::Submitted, "the command list hasn't been submitted, can't wait for it.");

        static std::chrono::time_point<std::chrono::high_resolution_clock> start_time;
        if (log_wait_time)
        { 
            start_time = std::chrono::high_resolution_clock::now();
        }

        // wait
        uint64_t timeout_nanoseconds = 10'000'000'000; // 10 seconds
        m_rendering_complete_semaphore_timeline->Wait(timeout_nanoseconds);
        m_state = RHI_CommandListState::Idle;

        if (log_wait_time)
        {
            auto end_time = std::chrono::high_resolution_clock::now();
            auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time).count();
            SP_LOG_INFO("wait time: %lld microseconds\n", duration);
        }
    }

    void RHI_CommandList::SetPipelineState(RHI_PipelineState& pso)
    {
        SP_ASSERT(m_state == RHI_CommandListState::Recording);

        // early exit if the pipeline state hasn't changed
        pso.Prepare();
        if (m_pso.GetHash() == pso.GetHash())
            return;

        // determine if the new render pass should clear the render targets or not
        if ((m_pso.shaders[RHI_Shader_Type::Vertex] != nullptr && m_pso.shaders[RHI_Shader_Type::Vertex] == pso.shaders[RHI_Shader_Type::Vertex]) && m_pso.render_target_array_index == pso.render_target_array_index)
        {
            m_load_depth_render_target = (pso.render_target_depth_texture == m_pso.render_target_depth_texture);
            for (uint32_t i = 0; i < rhi_max_render_target_count; i++)
            {
                m_load_color_render_targets[i] = (pso.render_target_color_textures[i] == m_pso.render_target_color_textures[i]);
            }
        }
        else
        {
            m_load_depth_render_target = false;
            for (uint32_t i = 0; i < rhi_max_render_target_count; i++)
            {
                m_load_color_render_targets[i] = false;
            }
        }

        // get (or create) a pipeline which matches the requested pipeline state
        m_pso = pso;
        RHI_Device::GetOrCreatePipeline(m_pso, m_pipeline, m_descriptor_layout_current);

        RenderPassBegin();

        // set pipeline
        {
            // get vulkan pipeline object
            SP_ASSERT(m_pipeline != nullptr);
            VkPipeline vk_pipeline = static_cast<VkPipeline>(m_pipeline->GetRhiResource());
            SP_ASSERT(vk_pipeline != nullptr);

            // bind
            VkPipelineBindPoint pipeline_bind_point = VK_PIPELINE_BIND_POINT_COMPUTE;
            pipeline_bind_point                     = m_pso.IsGraphics()   ? VK_PIPELINE_BIND_POINT_GRAPHICS        : pipeline_bind_point;
            pipeline_bind_point                     = m_pso.IsRayTracing() ? VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR : pipeline_bind_point;
            vkCmdBindPipeline(static_cast<VkCommandBuffer>(m_rhi_resource), pipeline_bind_point, vk_pipeline);
            Profiler::m_rhi_bindings_pipeline++;

            // set some dynamic states
            if (m_pso.IsGraphics())
            {
                // cull mode
                if (m_pso.rasterizer_state->GetPolygonMode() == RHI_PolygonMode::Wireframe)
                {
                    SetCullMode(RHI_CullMode::None);
                }

                // scissor rectangle
                math::Rectangle scissor_rect;
                scissor_rect.x      = 0.0f;
                scissor_rect.y      = 0.0f;
                scissor_rect.width  = static_cast<float>(m_pso.GetWidth());
                scissor_rect.height = static_cast<float>(m_pso.GetHeight());
                SetScissorRectangle(scissor_rect);

                // vertex and index buffer state
                m_buffer_id_index    = 0;
                m_buffer_id_vertex   = 0;
                m_buffer_id_instance = 0;
            }

            if (Debugging::IsBreadcrumbsEnabled())
            { 
                RHI_VendorTechnology::Breadcrumbs_SetPipelineState(this, m_pipeline);
            }
        }

        // bind descriptors
        {
            // set bindless descriptors
            descriptor_sets::set_bindless(m_pso, m_rhi_resource, m_pipeline->GetRhiResourceLayout());

            // set standard resources (dynamic descriptors)
            Renderer::SetStandardResources(this);
            descriptor_sets::set_dynamic(m_pso, m_rhi_resource, m_pipeline->GetRhiResourceLayout(), m_descriptor_layout_current);
        }
    }

    RHI_CommandList* RHI_CommandList::ImmediateExecutionBegin(const RHI_Queue_Type queue_type)
    {
        immediate_execution::ensure_initialized();
    
        // wait until it's safe to proceed
        unique_lock<mutex> lock(immediate_execution::mutex_execution);
        immediate_execution::condition_var.wait(lock, [] { return !immediate_execution::is_executing; });
        immediate_execution::is_executing = true;
    
        // get command list
        RHI_Queue* queue          = immediate_execution::queues[static_cast<uint32_t>(queue_type)].get();
        RHI_CommandList* cmd_list = queue->NextCommandList();
        cmd_list->Begin();
        return cmd_list;
    }
    
    void RHI_CommandList::ImmediateExecutionEnd(RHI_CommandList* cmd_list)
    {
        cmd_list->Submit(nullptr, true);
        cmd_list->WaitForExecution();
    
        // signal that it's safe to proceed with the next ImmediateBegin
        immediate_execution::is_executing = false;
        immediate_execution::condition_var.notify_one();
    }

    void RHI_CommandList::ImmediateExecutionShutdown()
    {
        // wait for ongoing operations to complete
        unique_lock<mutex> lock(immediate_execution::mutex_execution);
        immediate_execution::condition_var.wait(lock, [] { return !immediate_execution::is_executing; });

        // now release memory
        immediate_execution::queues.fill(nullptr);
    }

    void RHI_CommandList::RenderPassBegin()
    {
        SP_ASSERT(m_state == RHI_CommandListState::Recording);
        RenderPassEnd();
    
        if (!m_pso.IsGraphics())
            return;
    
        VkRenderingInfo rendering_info      = {};
        rendering_info.sType                = VK_STRUCTURE_TYPE_RENDERING_INFO_KHR;
        rendering_info.renderArea           = { 0, 0, m_pso.GetWidth(), m_pso.GetHeight() };
        rendering_info.layerCount           = 1;
        rendering_info.colorAttachmentCount = 0;
        rendering_info.pColorAttachments    = nullptr;
        rendering_info.pDepthAttachment     = nullptr;
        rendering_info.pStencilAttachment   = nullptr;
    
        // color attachments
        array<VkRenderingAttachmentInfo, rhi_max_render_target_count> attachments_color;
        uint32_t attachment_index = 0;
        {
            // swapchain buffer as a render target
            RHI_SwapChain* swapchain = m_pso.render_target_swapchain;
            if (swapchain)
            {
                // transition to the appropriate layout
                InsertBarrier(swapchain->GetRhiRt(), swapchain->GetFormat(), 0, 1, 1, RHI_Image_Layout::Attachment);
    
                VkRenderingAttachmentInfo color_attachment = {};
                color_attachment.sType                     = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO_KHR;
                color_attachment.imageView                 = static_cast<VkImageView>(swapchain->GetRhiRtv());
                color_attachment.imageLayout               = vulkan_image_layout[static_cast<uint8_t>(RHI_Image_Layout::Attachment)];
                color_attachment.loadOp                    = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
                color_attachment.storeOp                   = VK_ATTACHMENT_STORE_OP_STORE;
    
                SP_ASSERT(color_attachment.imageView != nullptr);
    
                attachments_color[attachment_index++] = color_attachment;
            }
            else // regular render target(s)
            {

                for (uint32_t i = 0; i < rhi_max_render_target_count; i++)
                {
                    RHI_Texture* rt = m_pso.render_target_color_textures[i];
                    if (rt == nullptr)
                        break;
    
                    SP_ASSERT_MSG(rt->IsRtv(), "The texture wasn't created with the RHI_Texture_RenderTarget flag and/or isn't a color format");
    
                    // transition to the appropriate layout
                    rt->SetLayout(RHI_Image_Layout::Attachment, this);
    
                    VkRenderingAttachmentInfo color_attachment = {};
                    color_attachment.sType                     = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO_KHR;
                    color_attachment.imageView                 = static_cast<VkImageView>(rt->GetRhiRtv(m_pso.render_target_array_index));
                    color_attachment.imageLayout               = vulkan_image_layout[static_cast<uint8_t>(rt->GetLayout(0))];
                    color_attachment.loadOp                    = m_load_color_render_targets[i] ? VK_ATTACHMENT_LOAD_OP_LOAD : get_color_load_op(m_pso.clear_color[i]);
                    color_attachment.storeOp                   = VK_ATTACHMENT_STORE_OP_STORE;
                    color_attachment.clearValue.color          = { m_pso.clear_color[i].r, m_pso.clear_color[i].g, m_pso.clear_color[i].b, m_pso.clear_color[i].a };
    
                    SP_ASSERT(color_attachment.imageView != nullptr);
    
                    attachments_color[attachment_index++] = color_attachment;
                }
            }
            rendering_info.colorAttachmentCount = attachment_index;
            rendering_info.pColorAttachments    = attachments_color.data();
        }
    
        // depth-stencil attachment
        VkRenderingAttachmentInfoKHR attachment_depth_stencil = {};
        if (m_pso.render_target_depth_texture != nullptr)
        {
            RHI_Texture* rt = m_pso.render_target_depth_texture;
            if (cvar_resolution_scale.GetValue() == 1.0f)
            { 
                SP_ASSERT_MSG(rt->GetWidth() == rendering_info.renderArea.extent.width, "The depth buffer doesn't match the output resolution");
            }
            SP_ASSERT(rt->IsDsv());
    
            // transition to the appropriate layout
            RHI_Image_Layout layout = RHI_Image_Layout::Attachment;
            rt->SetLayout(layout, this);
    
            attachment_depth_stencil.sType                           = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO_KHR;
            attachment_depth_stencil.imageView                       = static_cast<VkImageView>(rt->GetRhiDsv(m_pso.render_target_array_index));
            attachment_depth_stencil.imageLayout                     = vulkan_image_layout[static_cast<uint8_t>(rt->GetLayout(0))];
            attachment_depth_stencil.loadOp                          = m_load_depth_render_target ? VK_ATTACHMENT_LOAD_OP_LOAD : get_depth_load_op(m_pso.clear_depth);
            attachment_depth_stencil.storeOp                         = m_pso.depth_stencil_state->GetDepthWriteEnabled() ? VK_ATTACHMENT_STORE_OP_STORE : VK_ATTACHMENT_STORE_OP_NONE;
            attachment_depth_stencil.clearValue.depthStencil.depth   = m_pso.clear_depth;
            attachment_depth_stencil.clearValue.depthStencil.stencil = m_pso.clear_stencil;
    
            rendering_info.pDepthAttachment = &attachment_depth_stencil;
    
            // we are using the combined depth-stencil approach
            // this means we can assign the depth attachment as the stencil attachment
            if (m_pso.render_target_depth_texture->IsStencilFormat())
            {
                rendering_info.pStencilAttachment = rendering_info.pDepthAttachment;
            }
        }
    
        // variable rate shading
        VkRenderingFragmentShadingRateAttachmentInfoKHR attachment_shading_rate = {};
        if (m_pso.vrs_input_texture)
        {
            m_pso.vrs_input_texture->SetLayout(RHI_Image_Layout::Shading_Rate_Attachment, this);
    
            attachment_shading_rate.sType                          = VK_STRUCTURE_TYPE_RENDERING_FRAGMENT_SHADING_RATE_ATTACHMENT_INFO_KHR;
            attachment_shading_rate.imageView                      = static_cast<VkImageView>(m_pso.vrs_input_texture->GetRhiRtv());
            attachment_shading_rate.imageLayout                    = vulkan_image_layout[static_cast<uint8_t>(m_pso.vrs_input_texture->GetLayout(0))];
            attachment_shading_rate.shadingRateAttachmentTexelSize = { RHI_Device::PropertyGetMaxShadingRateTexelSizeX(), RHI_Device::PropertyGetMaxShadingRateTexelSizeY() };
    
            rendering_info.pNext = &attachment_shading_rate;
        }
    
        // begin dynamic render pass
        FlushBarriers();
        vkCmdBeginRendering(static_cast<VkCommandBuffer>(m_rhi_resource), &rendering_info);
    
        // set dynamic states
        {
            // variable rate shading
            RHI_Device::SetVariableRateShading(this, m_pso.vrs_input_texture != nullptr);
    
            // set viewport
            RHI_Viewport viewport;
            viewport.width  = static_cast<float>(m_pso.GetWidth());
            viewport.height = static_cast<float>(m_pso.GetHeight());
            SetViewport(viewport);
        }
    
        // reset
        m_load_depth_render_target = false;
        for (uint32_t i = 0; i < rhi_max_render_target_count; i++)
        {
            m_load_color_render_targets[i] = false;
        }
        m_render_pass_active = true;
    }

    void* RHI_CommandList::GetRhiResourcePipeline()
    {
        return m_pipeline->GetRhiResource();
    }

    void RHI_CommandList::RenderPassEnd()
    {
        if (!m_render_pass_active)
            return;
    
        vkCmdEndRendering(static_cast<VkCommandBuffer>(m_rhi_resource));
        m_render_pass_active = false;
    }

    void RHI_CommandList::ClearPipelineStateRenderTargets(RHI_PipelineState& pipeline_state)
    {
        SP_ASSERT(m_state == RHI_CommandListState::Recording);

        uint32_t attachment_count = 0;
        array<VkClearAttachment, rhi_max_render_target_count + 1> attachments; // +1 for depth-stencil

        for (uint8_t i = 0; i < rhi_max_render_target_count; i++)
        { 
            if (pipeline_state.clear_color[i] != rhi_color_load)
            {
                VkClearAttachment& attachment = attachments[attachment_count++];

                attachment.aspectMask                  = VK_IMAGE_ASPECT_COLOR_BIT;
                attachment.colorAttachment             = 0;
                attachment.clearValue.color.float32[0] = pipeline_state.clear_color[i].r;
                attachment.clearValue.color.float32[1] = pipeline_state.clear_color[i].g;
                attachment.clearValue.color.float32[2] = pipeline_state.clear_color[i].b;
                attachment.clearValue.color.float32[3] = pipeline_state.clear_color[i].a;
            }
        }

        bool clear_depth   = pipeline_state.clear_depth   != rhi_depth_load   && pipeline_state.clear_depth   != rhi_depth_dont_care;
        bool clear_stencil = pipeline_state.clear_stencil != rhi_stencil_load && pipeline_state.clear_stencil != rhi_stencil_dont_care;

        if (clear_depth || clear_stencil)
        {
            VkClearAttachment& attachment = attachments[attachment_count++];

            attachment.aspectMask = 0;

            if (clear_depth)
            {
                attachment.aspectMask |= VK_IMAGE_ASPECT_DEPTH_BIT;
            }

            if (clear_stencil)
            {
                attachment.aspectMask |= VK_IMAGE_ASPECT_STENCIL_BIT;
            }
        
            attachment.clearValue.depthStencil.depth   = pipeline_state.clear_depth;
            attachment.clearValue.depthStencil.stencil = static_cast<uint32_t>(pipeline_state.clear_stencil);
        }

        VkClearRect clear_rect        = {};
        clear_rect.baseArrayLayer     = 0;
        clear_rect.layerCount         = 1;
        clear_rect.rect.extent.width  = pipeline_state.GetWidth();
        clear_rect.rect.extent.height = pipeline_state.GetHeight();

        if (attachment_count == 0)
            return;

        vkCmdClearAttachments(static_cast<VkCommandBuffer>(m_rhi_resource), attachment_count, attachments.data(), 1, &clear_rect);
    }

    void RHI_CommandList::ClearTexture(
        RHI_Texture* texture,
        const Color& clear_color     /*= rhi_color_load*/,
        const float clear_depth      /*= rhi_depth_load*/,
        const uint32_t clear_stencil /*= rhi_stencil_load*/
    )
    {
        SP_ASSERT(m_state == RHI_CommandListState::Recording);
        SP_ASSERT_MSG((texture->GetFlags() & RHI_Texture_ClearBlit) != 0, "The texture needs the RHI_Texture_ClearBlit flag");
        SP_ASSERT(texture && texture->GetRhiSrv());

        // one of the required layouts for clear functions
        texture->SetLayout(RHI_Image_Layout::Transfer_Destination, this);

        VkImageSubresourceRange image_subresource_range = {};
        image_subresource_range.baseMipLevel            = 0;
        image_subresource_range.levelCount              = VK_REMAINING_MIP_LEVELS;
        image_subresource_range.baseArrayLayer          = 0;
        image_subresource_range.layerCount              = VK_REMAINING_ARRAY_LAYERS;

        if (texture->IsColorFormat())
        {
            VkClearColorValue _clear_color = { clear_color.r, clear_color.g, clear_color.b, clear_color.a };

            image_subresource_range.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;

            vkCmdClearColorImage(static_cast<VkCommandBuffer>(m_rhi_resource), static_cast<VkImage>(texture->GetRhiResource()), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &_clear_color, 1, &image_subresource_range);
        }
        else if (texture->IsDepthStencilFormat())
        {
            VkClearDepthStencilValue clear_depth_stencil = { clear_depth, static_cast<uint32_t>(clear_stencil) };

            if (texture->IsDepthFormat())
            {
                image_subresource_range.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
            }

            if (texture->IsStencilFormat())
            {
                image_subresource_range.aspectMask |= VK_IMAGE_ASPECT_DEPTH_BIT;
            }

            vkCmdClearDepthStencilImage(
                static_cast<VkCommandBuffer>(m_rhi_resource),
                static_cast<VkImage>(texture->GetRhiResource()),
                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                &clear_depth_stencil,
                1,
                &image_subresource_range);
        }
    }

    void RHI_CommandList::Draw(const uint32_t vertex_count, const uint32_t vertex_start_index /*= 0*/)
    {
        SP_ASSERT(m_state == RHI_CommandListState::Recording);

        PreDraw();

        vkCmdDraw(
            static_cast<VkCommandBuffer>(m_rhi_resource), // commandBuffer
            vertex_count,                                 // vertexCount
            1,                                            // instanceCount
            vertex_start_index,                           // firstVertex
            0                                             // firstInstance
        );
        Profiler::m_rhi_draw++;
    }

    void RHI_CommandList::DrawIndexed(const uint32_t index_count, const uint32_t index_offset, const uint32_t vertex_offset, const uint32_t instance_index, const uint32_t instance_count)
    {
        SP_ASSERT(m_state == RHI_CommandListState::Recording);

        PreDraw();

        vkCmdDrawIndexed(
            static_cast<VkCommandBuffer>(m_rhi_resource), // commandBuffer
            index_count,                                  // indexCount
            instance_count,                               // instanceCount
            index_offset,                                 // firstIndex
            vertex_offset,                                // vertexOffset
            instance_index                                // firstInstance
        );
        Profiler::m_rhi_draw++;
        Profiler::m_rhi_instance_count += instance_count == 1 ? 0 : instance_count;
    }

    void RHI_CommandList::Dispatch(uint32_t x, uint32_t y, uint32_t z /*= 1*/)
    {
        SP_ASSERT(m_state == RHI_CommandListState::Recording);

        PreDraw();

        vkCmdDispatch(static_cast<VkCommandBuffer>(m_rhi_resource), x, y, z);
    }

    void RHI_CommandList::TraceRays(const uint32_t width, const uint32_t height, RHI_Buffer* shader_binding_table)
    {
        SP_ASSERT(m_state == RHI_CommandListState::Recording);
        SP_ASSERT(shader_binding_table && shader_binding_table->GetType() == RHI_Buffer_Type::ShaderBindingTable);

        // bind descriptor sets (same as draw/dispatch)
        PreDraw();

        // load extension func once
        static PFN_vkCmdTraceRaysKHR pfn_vk_cmd_trace_rays_khr = nullptr;
        if (!pfn_vk_cmd_trace_rays_khr)
        {
            pfn_vk_cmd_trace_rays_khr = (PFN_vkCmdTraceRaysKHR)vkGetDeviceProcAddr(RHI_Context::device, "vkCmdTraceRaysKHR");
            SP_ASSERT(pfn_vk_cmd_trace_rays_khr != nullptr);
        }

        // get regions
        RHI_StridedDeviceAddressRegion raygen_region = shader_binding_table->GetRegion(RHI_Shader_Type::RayGeneration);
        RHI_StridedDeviceAddressRegion miss_region   = shader_binding_table->GetRegion(RHI_Shader_Type::RayMiss);
        RHI_StridedDeviceAddressRegion hit_region    = shader_binding_table->GetRegion(RHI_Shader_Type::RayHit);

        // convert to vulkan regions
        VkStridedDeviceAddressRegionKHR vk_raygen   = { raygen_region.device_address, raygen_region.stride, raygen_region.size };
        VkStridedDeviceAddressRegionKHR vk_miss     = { miss_region.device_address, miss_region.stride, miss_region.size };
        VkStridedDeviceAddressRegionKHR vk_hit      = { hit_region.device_address, hit_region.stride, hit_region.size };
        VkStridedDeviceAddressRegionKHR vk_callable = {};
    
        pfn_vk_cmd_trace_rays_khr(
            static_cast<VkCommandBuffer>(m_rhi_resource), // commandBuffer
            &vk_raygen,                                   // pRaygenShaderBindingTable
            &vk_miss,                                     // pMissShaderBindingTable
            &vk_hit,                                      // pHitShaderBindingTable
            &vk_callable,                                 // pCallableShaderBindingTable
            width,                                        // width
            height,                                       // height
            1                                             // depth
        );
    }

    void RHI_CommandList::Blit(RHI_Texture* source, RHI_Texture* destination, const bool blit_mips, const float source_scaling)
    {
        SP_ASSERT_MSG(source && destination,                                                                                                        "Source and destination textures cannot be null");
        SP_ASSERT_MSG((source->GetFlags() & RHI_Texture_ClearBlit) != 0,                                                                            "Blit requires the texture to be created with the RHI_Texture_ClearOrBlit flag");
        SP_ASSERT_MSG((destination->GetFlags() & RHI_Texture_ClearBlit) != 0,                                                                       "Blit requires the texture to be created with the RHI_Texture_ClearOrBlit flag");
        SP_ASSERT_MSG(source->GetChannelCount() == destination->GetChannelCount(),                                                                  "Source and destination must have matching channel counts for blit compatibility");
        SP_ASSERT_MSG(source->GetBitsPerChannel() == destination->GetBitsPerChannel() || (source->IsColorFormat() && destination->IsColorFormat()), "Source and destination bit depths must match or be convertible color formats");
        SP_ASSERT_MSG(!source->IsDepthFormat() || !destination->IsDepthFormat() || source->GetFormat() == destination->GetFormat(),                 "Depth formats must be identical for blit");
        if (blit_mips)
        {
            SP_ASSERT_MSG(source->GetMipCount() == destination->GetMipCount(), "If the mips are blitted, then the mip count between the source and the destination textures must match");
        }

        // compute a blit region for each mip
        array<VkOffset3D,  rhi_max_mip_count> blit_offsets_source     = {};
        array<VkOffset3D, rhi_max_mip_count> blit_offsets_destination = {};
        array<VkImageBlit, rhi_max_mip_count> blit_regions            = {};
        uint32_t blit_region_count                                    = blit_mips ? source->GetMipCount() : 1;
        for (uint32_t mip_index = 0; mip_index < blit_region_count; mip_index++)
        {
            VkOffset3D& source_blit_size = blit_offsets_source[mip_index];
            source_blit_size.x           = static_cast<int32_t>(source->GetWidth()  * source_scaling) >> mip_index;
            source_blit_size.y           = static_cast<int32_t>(source->GetHeight() * source_scaling) >> mip_index;
            source_blit_size.z           = 1;

            VkOffset3D& destination_blit_size = blit_offsets_destination[mip_index];
            destination_blit_size.x           = destination->GetWidth()  >> mip_index;
            destination_blit_size.y           = destination->GetHeight() >> mip_index;
            destination_blit_size.z           = 1;

            VkImageBlit& blit_region                  = blit_regions[mip_index];
            blit_region.srcSubresource.mipLevel       = mip_index;
            blit_region.srcSubresource.baseArrayLayer = 0;
            blit_region.srcSubresource.layerCount     = 1;
            blit_region.srcSubresource.aspectMask     = get_aspect_mask(source->GetFormat());
            blit_region.srcOffsets[0]                 = { 0, 0, 0 };
            blit_region.srcOffsets[1]                 = source_blit_size;
            blit_region.dstSubresource.mipLevel       = mip_index;
            blit_region.dstSubresource.baseArrayLayer = 0;
            blit_region.dstSubresource.layerCount     = 1;
            blit_region.dstSubresource.aspectMask     = get_aspect_mask(destination->GetFormat());
            blit_region.dstOffsets[0]                 = { 0, 0, 0 };
            blit_region.dstOffsets[1]                 = destination_blit_size;
        }

        // save the initial layouts
        array<RHI_Image_Layout, rhi_max_mip_count> layouts_initial_source      = source->GetLayouts();
        array<RHI_Image_Layout, rhi_max_mip_count> layouts_initial_destination = destination->GetLayouts();

        // transition to blit appropriate layouts
        source->SetLayout(RHI_Image_Layout::Transfer_Source, this);
        destination->SetLayout(RHI_Image_Layout::Transfer_Destination, this);

        VkFilter filter = (source->IsDepthFormat() || destination->IsDepthFormat() || 
                          (source->GetWidth() == destination->GetWidth() && source->GetHeight() == destination->GetHeight())) 
        ? VK_FILTER_NEAREST 
        : VK_FILTER_LINEAR;

        // blit
        vkCmdBlitImage(
            static_cast<VkCommandBuffer>(m_rhi_resource),
            static_cast<VkImage>(source->GetRhiResource()),      VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            static_cast<VkImage>(destination->GetRhiResource()), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            blit_region_count, &blit_regions[0],
            filter
        );

        // transition to the initial layouts
        if (blit_mips)
        {
            for (uint32_t i = 0; i < source->GetMipCount(); i++)
            {
                source->SetLayout(layouts_initial_source[i], this, i, 1);
                destination->SetLayout(layouts_initial_destination[i], this, i, 1);
            }
        }
        else
        {
            source->SetLayout(layouts_initial_source[0], this);
            destination->SetLayout(layouts_initial_destination[0], this);
        }
    }

    void RHI_CommandList::Blit(RHI_Texture* source, RHI_SwapChain* destination)
    {
        SP_ASSERT_MSG((source->GetFlags() & RHI_Texture_ClearBlit) != 0, "The texture needs the RHI_Texture_ClearOrBlit flag");
        SP_ASSERT_MSG(source->GetWidth() <= destination->GetWidth() && source->GetHeight() <= destination->GetHeight(),
            "The source texture dimension(s) are larger than the those of the destination texture");

        VkOffset3D source_blit_size = {};
        source_blit_size.x          = source->GetWidth();
        source_blit_size.y          = source->GetHeight();
        source_blit_size.z          = 1;

        VkOffset3D destination_blit_size = {};
        destination_blit_size.x          = destination->GetWidth();
        destination_blit_size.y          = destination->GetHeight();
        destination_blit_size.z          = 1;

        VkImageBlit blit_region                   = {};
        blit_region.srcSubresource.mipLevel       = 0;
        blit_region.srcSubresource.baseArrayLayer = 0;
        blit_region.srcSubresource.layerCount     = 1;
        blit_region.srcSubresource.aspectMask     = get_aspect_mask(source->GetFormat());
        blit_region.srcOffsets[0]                 = { 0, 0, 0 };
        blit_region.srcOffsets[1]                 = source_blit_size;
        blit_region.dstSubresource.mipLevel       = 0;
        blit_region.dstSubresource.baseArrayLayer = 0;
        blit_region.dstSubresource.layerCount     = 1;
        blit_region.dstSubresource.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
        blit_region.dstOffsets[0]                 = { 0, 0, 0 };
        blit_region.dstOffsets[1]                 = destination_blit_size;

        // save the initial layout
        RHI_Image_Layout source_layout_initial = source->GetLayout(0);

        // transition to blit appropriate layouts
        source->SetLayout(RHI_Image_Layout::Transfer_Source,           this);
        InsertBarrier(destination->GetRhiRt(), destination->GetFormat(), 0, 1, 1, RHI_Image_Layout::Transfer_Destination);

        // deduce filter
        bool width_equal  = source->GetWidth() == destination->GetWidth();
        bool height_equal = source->GetHeight() == destination->GetHeight();
        RHI_Filter filter = width_equal && height_equal ? RHI_Filter::Nearest : RHI_Filter::Linear;

        // blit
        vkCmdBlitImage(
            static_cast<VkCommandBuffer>(m_rhi_resource),
            static_cast<VkImage>(source->GetRhiResource()), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            static_cast<VkImage>(destination->GetRhiRt()),  VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            1, &blit_region,
            vulkan_filter[static_cast<uint32_t>(filter)]
        );

        // transition to the initial layouts
        source->SetLayout(source_layout_initial, this);
        InsertBarrier(destination->GetRhiRt(), destination->GetFormat(), 0, 1, 1, RHI_Image_Layout::Present_Source);
    }

    void RHI_CommandList::Copy(RHI_Texture* source, RHI_Texture* destination, const bool blit_mips)
    {
        SP_ASSERT_MSG((source->GetFlags() & RHI_Texture_ClearBlit) != 0, "The texture needs the RHI_Texture_ClearOrBlit flag");
        SP_ASSERT_MSG((destination->GetFlags() & RHI_Texture_ClearBlit) != 0, "The texture needs the RHI_Texture_ClearOrBlit flag");
        SP_ASSERT(source->GetWidth() == destination->GetWidth());
        SP_ASSERT(source->GetHeight() == destination->GetHeight());
        SP_ASSERT(source->GetFormat() == destination->GetFormat());
        if (blit_mips)
        {
            SP_ASSERT_MSG(source->GetMipCount() == destination->GetMipCount(),
                "If the mips are blitted, then the mip count between the source and the destination textures must match");
        }

        array<VkImageCopy, rhi_max_mip_count> copy_regions = {};
        uint32_t copy_region_count                         = blit_mips ? source->GetMipCount() : 1;
        for (uint32_t mip_index = 0; mip_index < copy_region_count; mip_index++)
        {
            VkImageCopy& copy_region              = copy_regions[mip_index];
            copy_region.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            copy_region.srcSubresource.mipLevel   = mip_index;
            copy_region.srcSubresource.layerCount = 1;
            copy_region.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            copy_region.dstSubresource.mipLevel   = mip_index;
            copy_region.dstSubresource.layerCount = 1;
            copy_region.extent.width              = source->GetWidth()  >> mip_index;
            copy_region.extent.height             = source->GetHeight() >> mip_index;
            copy_region.extent.depth              = 1;
        }

        // save the initial layouts
        array<RHI_Image_Layout, rhi_max_mip_count> layouts_initial_source = source->GetLayouts();
        array<RHI_Image_Layout, rhi_max_mip_count> layouts_initial_destination = destination->GetLayouts();

        // transition to blit appropriate layouts
        source->SetLayout(RHI_Image_Layout::Transfer_Source, this);
        destination->SetLayout(RHI_Image_Layout::Transfer_Destination, this);

        vkCmdCopyImage(
            static_cast<VkCommandBuffer>(m_rhi_resource),
            static_cast<VkImage>(source->GetRhiResource()), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            static_cast<VkImage>(destination->GetRhiResource()), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            copy_region_count, &copy_regions[0]
        );

        // transition to the initial layouts
        if (blit_mips)
        {
            for (uint32_t i = 0; i < source->GetMipCount(); i++)
            {
                source->SetLayout(layouts_initial_source[i], this, i, 1);
                destination->SetLayout(layouts_initial_destination[i], this, i, 1);
            }
        }
        else
        {
            source->SetLayout(layouts_initial_source[0], this);
            destination->SetLayout(layouts_initial_destination[0], this);
        }
    }

    void RHI_CommandList::Copy(RHI_Texture* source, RHI_SwapChain* destination)
    {
        SP_ASSERT_MSG((source->GetFlags() & RHI_Texture_ClearBlit) != 0, "The texture needs the RHI_Texture_ClearOrBlit flag");
        SP_ASSERT(source->GetWidth() == destination->GetWidth());
        SP_ASSERT(source->GetHeight() == destination->GetHeight());
        SP_ASSERT(source->GetFormat() == destination->GetFormat());

        VkImageCopy copy_region               = {};
        copy_region.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        copy_region.srcSubresource.mipLevel   = 0;
        copy_region.srcSubresource.layerCount = 1;
        copy_region.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        copy_region.dstSubresource.mipLevel   = 0;
        copy_region.dstSubresource.layerCount = 1;
        copy_region.extent.width              = source->GetWidth();
        copy_region.extent.height             = source->GetHeight();
        copy_region.extent.depth              = 1;

        // transition to blit appropriate layouts
        RHI_Image_Layout layout_initial_source = source->GetLayout(0);
        source->SetLayout(RHI_Image_Layout::Transfer_Source, this);
        InsertBarrier(destination->GetRhiRt(), destination->GetFormat(), 0, 1, 1, RHI_Image_Layout::Transfer_Destination);

        // blit
        vkCmdCopyImage(
            static_cast<VkCommandBuffer>(m_rhi_resource),
            static_cast<VkImage>(source->GetRhiResource()), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            static_cast<VkImage>(destination->GetRhiRt()),  VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            1, &copy_region
        );

        // transition to the initial layout
        source->SetLayout(layout_initial_source, this);
        InsertBarrier(destination->GetRhiRt(), destination->GetFormat(), 0, 1, 1, RHI_Image_Layout::Present_Source);
    }

    void RHI_CommandList::SetViewport(const RHI_Viewport& viewport) const
    {
        SP_ASSERT(m_state == RHI_CommandListState::Recording);
        SP_ASSERT(viewport.width != 0);
        SP_ASSERT(viewport.height != 0);

        VkViewport vk_viewport = {};
        vk_viewport.x          = viewport.x;
        vk_viewport.y          = viewport.y;
        vk_viewport.width      = viewport.width;
        vk_viewport.height     = viewport.height;
        vk_viewport.minDepth   = viewport.depth_min;
        vk_viewport.maxDepth   = viewport.depth_max;

        vkCmdSetViewport(
            static_cast<VkCommandBuffer>(m_rhi_resource), // commandBuffer
            0,                                            // firstViewport
            1,                                            // viewportCount
            &vk_viewport                                  // pViewports
        );
    }

    void RHI_CommandList::SetScissorRectangle(const math::Rectangle& scissor_rectangle) const
    {
        SP_ASSERT(m_state == RHI_CommandListState::Recording);

        VkRect2D vk_scissor;
        vk_scissor.offset.x      = static_cast<int32_t>(scissor_rectangle.x);
        vk_scissor.offset.y      = static_cast<int32_t>(scissor_rectangle.y);
        vk_scissor.extent.width  = static_cast<uint32_t>(scissor_rectangle.width);
        vk_scissor.extent.height = static_cast<uint32_t>(scissor_rectangle.height);

        vkCmdSetScissor(
            static_cast<VkCommandBuffer>(m_rhi_resource), // commandBuffer
            0,          // firstScissor
            1,          // scissorCount
            &vk_scissor // pScissors
        );
    }

    void RHI_CommandList::SetCullMode(const RHI_CullMode cull_mode)
    {
        SP_ASSERT(m_state == RHI_CommandListState::Recording);
        if (m_cull_mode == cull_mode)
            return;

        m_cull_mode = cull_mode;
        vkCmdSetCullMode(
            static_cast<VkCommandBuffer>(m_rhi_resource),
            vulkan_cull_mode[static_cast<uint32_t>(m_cull_mode)]
        );
    }

    void RHI_CommandList::SetBufferVertex(const RHI_Buffer* vertex, RHI_Buffer* instance)
    {
        SP_ASSERT(m_state == RHI_CommandListState::Recording);

        // the instance buffer is optional but always part of the pipeline therefore it can't be null
        if (!instance)
        {
            instance = Renderer::GetBuffer(Renderer_Buffer::DummyInstance);
        }
    
        // prepare buffers and offsets arrays
        VkBuffer vertex_buffers[2] = {
    
            static_cast<VkBuffer>(vertex->GetRhiResource()),  // slot 0: vertex buffer
            static_cast<VkBuffer>(instance->GetRhiResource()) // slot 1: instance buffer
        };
        SP_ASSERT(vertex_buffers[0] != nullptr && vertex_buffers[1] != nullptr);

        VkDeviceSize offsets[2] = { 0, 0 };
    
        // check if vertex buffer id has changed to trigger binding
        if (m_buffer_id_vertex != vertex->GetObjectId() || m_buffer_id_instance != instance->GetObjectId())
        {
            vkCmdBindVertexBuffers(
                static_cast<VkCommandBuffer>(m_rhi_resource), // commandbuffer
                0,                                            // firstbinding
                2,                                            // bindingcount
                vertex_buffers,                               // pbuffers
                offsets                                       // poffsets
            );
    
            // track currently bound buffers
            m_buffer_id_vertex   = vertex->GetObjectId();
            m_buffer_id_instance = instance->GetObjectId();
            Profiler::m_rhi_bindings_buffer_vertex++;
        }
    }

    void RHI_CommandList::SetBufferIndex(const RHI_Buffer* buffer)
    {
        SP_ASSERT(m_state == RHI_CommandListState::Recording);
        SP_ASSERT(buffer != nullptr);
        SP_ASSERT(buffer->GetRhiResource() != nullptr);

        if (m_buffer_id_index == buffer->GetObjectId())
            return;

        bool is_16bit = buffer->GetStride() == sizeof(uint16_t);

        vkCmdBindIndexBuffer(
            static_cast<VkCommandBuffer>(m_rhi_resource),          // commandBuffer
            static_cast<VkBuffer>(buffer->GetRhiResource()),       // buffer
            0,                                                     // offset
            is_16bit ? VK_INDEX_TYPE_UINT16 : VK_INDEX_TYPE_UINT32 // indexType
        );

        m_buffer_id_index = buffer->GetObjectId();
        Profiler::m_rhi_bindings_buffer_index++;
    }

    void RHI_CommandList::PushConstants(const uint32_t offset, const uint32_t size, const void* data)
    {
        SP_ASSERT(m_state == RHI_CommandListState::Recording);
        SP_ASSERT(size <= RHI_Device::PropertyGetMaxPushConstantSize());
    
        uint32_t stages = 0;
        if (m_pso.shaders[RHI_Shader_Type::Compute])
        {
            stages |= VkShaderStageFlagBits::VK_SHADER_STAGE_COMPUTE_BIT;
        }
        if (m_pso.shaders[RHI_Shader_Type::Vertex])
        {
            stages |= VkShaderStageFlagBits::VK_SHADER_STAGE_VERTEX_BIT;
        }
        if (m_pso.shaders[RHI_Shader_Type::Hull])
        {
            stages |= VkShaderStageFlagBits::VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT;
        }
        if (m_pso.shaders[RHI_Shader_Type::Domain])
        {
            stages |= VkShaderStageFlagBits::VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT;
        }
        if (m_pso.shaders[RHI_Shader_Type::Pixel])
        {
            stages |= VkShaderStageFlagBits::VK_SHADER_STAGE_FRAGMENT_BIT;
        }
        if (m_pso.shaders[RHI_Shader_Type::RayGeneration])
        {
            stages |= VkShaderStageFlagBits::VK_SHADER_STAGE_RAYGEN_BIT_KHR;
        }
        if (m_pso.shaders[RHI_Shader_Type::RayMiss])
        {
            stages |= VkShaderStageFlagBits::VK_SHADER_STAGE_MISS_BIT_KHR;
        }
        if (m_pso.shaders[RHI_Shader_Type::RayHit])
        {
            stages |= VkShaderStageFlagBits::VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR;
        }
    
        vkCmdPushConstants(
            static_cast<VkCommandBuffer>(m_rhi_resource),
            static_cast<VkPipelineLayout>(m_pipeline->GetRhiResourceLayout()),
            stages,
            offset,
            size,
            data
        );
    }

    void RHI_CommandList::SetConstantBuffer(const uint32_t slot, RHI_Buffer* constant_buffer) const
    {
        SP_ASSERT(m_state == RHI_CommandListState::Recording);

        if (!m_descriptor_layout_current)
        {
            SP_LOG_WARNING("Descriptor layout not set, try setting constant buffer \"%s\" within a render pass", constant_buffer->GetObjectName().c_str());
            return;
        }

        // set (will only happen if it's not already set)
        m_descriptor_layout_current->SetConstantBuffer(slot, constant_buffer);

        // todo: detect if there are changes, otherwise don't bother binding
        descriptor_sets::bind_dynamic = true;
    }

    void RHI_CommandList::SetTexture(const uint32_t slot, RHI_Texture* texture, const uint32_t mip_index /*= all_mips*/, uint32_t mip_range /*= 0*/, const bool uav /*= false*/)
    {
        SP_ASSERT(m_state == RHI_CommandListState::Recording);

        if (mip_index != rhi_all_mips)
        {
            SP_ASSERT_MSG(mip_range != 0, "If a mip was specified, then mip_range can't be 0");
        }

        if (!m_descriptor_layout_current)
        {
            SP_LOG_WARNING("Descriptor layout not set, try setting texture \"%s\" within a render pass", texture->GetObjectName().c_str());
            return;
        }

        // if the texture is null or it's still loading, ignore it
        if (!texture || texture->GetResourceState() != ResourceState::PreparedForGpu)
            return;

        // get some texture info
        const uint32_t mip_count        = texture->GetMipCount();
        const bool mip_specified        = mip_index != rhi_all_mips;
        const uint32_t mip_start        = mip_specified ? mip_index : 0;
        RHI_Image_Layout current_layout = texture->GetLayout(mip_start);

        SP_ASSERT_MSG(current_layout != RHI_Image_Layout::Max && current_layout != RHI_Image_Layout::Preinitialized, "Invalid layout");

        // transition to appropriate layout (if needed)
        {
            RHI_Image_Layout target_layout = RHI_Image_Layout::Max;
            if (uav)
            {
                SP_ASSERT(texture->IsUav());
                
                // according to section 13.1 of the Vulkan spec, storage textures have to be in a general layout.
                // https://www.khronos.org/registry/vulkan/specs/1.1-extensions/html/vkspec.html#descriptorsets-storageimage
                target_layout = RHI_Image_Layout::General;
            }
            else
            {
                SP_ASSERT(texture->IsSrv());
                target_layout = RHI_Image_Layout::Shader_Read;
            }

            // verify that an appropriate layout has been deduced
            SP_ASSERT(target_layout != RHI_Image_Layout::Max);

            // determine if a layout transition is needed
            bool transition_required = current_layout != target_layout;
            {
                bool rest_mips_have_same_layout = true;
                array<RHI_Image_Layout, rhi_max_mip_count> layouts = texture->GetLayouts();
                for (uint32_t i = mip_start; i < mip_count; i++)
                {
                    if (target_layout != layouts[i])
                    {
                        rest_mips_have_same_layout = false;
                        break;
                    }
                }

                transition_required = !rest_mips_have_same_layout ? true : transition_required;
            }

            // transition
            if (transition_required)
            {
                texture->SetLayout(target_layout, this, mip_index, mip_range);
            }
        }

        // set (will only happen if it's not already set)
        m_descriptor_layout_current->SetTexture(slot, texture, mip_index, mip_range);

        // todo: detect if there are changes, otherwise don't bother binding
        descriptor_sets::bind_dynamic = true;
    }

    void RHI_CommandList::SetAccelerationStructure(Renderer_BindingsSrv slot, RHI_AccelerationStructure* tlas)
    {
        SP_ASSERT(m_state == RHI_CommandListState::Recording);
        m_descriptor_layout_current->SetAccelerationStructure(static_cast<uint32_t>(slot), tlas);
        
        // mark descriptor set as needing to be bound
        descriptor_sets::bind_dynamic = true;
    }

    void RHI_CommandList::SetBuffer(const uint32_t slot, RHI_Buffer* buffer) const
    {
        SP_ASSERT(m_state == RHI_CommandListState::Recording);

        if (!m_descriptor_layout_current)
        {
            //SP_LOG_WARNING("Descriptor layout not set, try setting buffer \"%s\" within a render pass", buffer->GetObjectName().c_str());
            return;
        }

        m_descriptor_layout_current->SetBuffer(slot, buffer);

        // todo: detect if there are changes, otherwise don't bother binding
        descriptor_sets::bind_dynamic = true;
    }

    void RHI_CommandList::BeginMarker(const char* name)
    {
        if (Debugging::IsGpuMarkingEnabled())
        {
            RHI_Device::MarkerBegin(this, name, Vector4::Zero);
        }

        if (Debugging::IsBreadcrumbsEnabled())
        {
            RHI_VendorTechnology::Breadcrumbs_MarkerBegin(this, AMD_FFX_Marker::Pass, name);
        }
    }

    void RHI_CommandList::EndMarker()
    {
        if (Debugging::IsGpuMarkingEnabled())
        {
            RHI_Device::MarkerEnd(this);
        }

        if (Debugging::IsBreadcrumbsEnabled())
        {
            RHI_VendorTechnology::Breadcrumbs_MarkerEnd(this);
        }
    }
    
    uint32_t RHI_CommandList::BeginTimestamp()
    {
        SP_ASSERT(m_state == RHI_CommandListState::Recording);

        uint32_t timestamp_index = m_timestamp_index;

        vkCmdWriteTimestamp2(
            static_cast<VkCommandBuffer>(m_rhi_resource),
            VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT,
            static_cast<VkQueryPool>(m_rhi_query_pool_timestamps),
            m_timestamp_index++
        );

        return timestamp_index;
    }

    void RHI_CommandList::EndTimestamp()
    {
        SP_ASSERT(m_state == RHI_CommandListState::Recording);

        vkCmdWriteTimestamp2(
            static_cast<VkCommandBuffer>(m_rhi_resource),
            VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT,
            static_cast<VkQueryPool>(m_rhi_query_pool_timestamps),
            m_timestamp_index++
        );
    }

    float RHI_CommandList::GetTimestampResult(const uint32_t index_timestamp)
    {
        SP_ASSERT_MSG(index_timestamp + 1 < queries::timestamp::data.size(), "index out of range");

        uint64_t start    = queries::timestamp::data[index_timestamp];
        uint64_t end      = queries::timestamp::data[index_timestamp + 1];
        uint64_t duration = clamp<uint64_t>(end - start, 0, numeric_limits<uint64_t>::max());
        float duration_ms = static_cast<float>(duration * RHI_Device::PropertyGetTimestampPeriod() * 1e-6f);

        return clamp(duration_ms, 0.0f, numeric_limits<float>::max());
    }

    void RHI_CommandList::BeginOcclusionQuery(const uint64_t entity_id)
    {
        SP_ASSERT_MSG(m_pso.IsGraphics(), "Occlusion queries are only supported in graphics pipelines");

        queries::occlusion::index_active = queries::occlusion::id_to_index[entity_id];
        if (queries::occlusion::index_active == 0)
        {
            queries::occlusion::index_active           = ++queries::occlusion::index;
            queries::occlusion::id_to_index[entity_id] = queries::occlusion::index;
        }

        if (!m_render_pass_active)
        {
            RenderPassBegin();
        }

        vkCmdBeginQuery(
            static_cast<VkCommandBuffer>(m_rhi_resource),
            static_cast<VkQueryPool>(m_rhi_query_pool_occlusion),
            queries::occlusion::index_active,
            0
        );

        queries::occlusion::occlusion_query_active = true;
    }

    void RHI_CommandList::EndOcclusionQuery()
    {
        if (!queries::occlusion::occlusion_query_active)
            return;

        vkCmdEndQuery(
            static_cast<VkCommandBuffer>(m_rhi_resource),
            static_cast<VkQueryPool>(m_rhi_query_pool_occlusion),
            queries::occlusion::index_active
        );

        queries::occlusion::occlusion_query_active = false;
    }

    bool RHI_CommandList::GetOcclusionQueryResult(const uint64_t entity_id)
    {
        if (queries::occlusion::id_to_index.find(entity_id) == queries::occlusion::id_to_index.end())
            return false;

        uint32_t index  = queries::occlusion::id_to_index[entity_id];
        uint64_t result = queries::occlusion::data[index]; // visible pixel count

        return result == 0;
    }

    void RHI_CommandList::UpdateOcclusionQueries()
    {
        queries::occlusion::update(m_rhi_query_pool_occlusion);
    }

    void RHI_CommandList::BeginTimeblock(const char* name, const bool gpu_marker, const bool gpu_timing)
    {
        SP_ASSERT(name != nullptr);
    
        // timing
        Profiler::TimeBlockStart(name, TimeBlockType::Cpu, this);
        if (Debugging::IsGpuTimingEnabled() && gpu_timing)
        {
            Profiler::TimeBlockStart(name, TimeBlockType::Gpu, this);
        }
    
        // markers (support nesting)
        if (Debugging::IsGpuMarkingEnabled() && gpu_marker)
        {
            RHI_Device::MarkerBegin(this, name, Vector4::Zero);
            m_debug_label_stack.push(name);
        }
    
        // track active time blocks (for nesting)
        m_active_timeblocks.push(name);
    }

    void RHI_CommandList::EndTimeblock()
    {
        SP_ASSERT(!m_active_timeblocks.empty());
    
        // markers (only end if one was started)
        if (Debugging::IsGpuMarkingEnabled() && !m_debug_label_stack.empty())
        {
            RHI_Device::MarkerEnd(this);
            m_debug_label_stack.pop();
        }
    
        // timing
        if (Debugging::IsGpuTimingEnabled())
        {
            Profiler::TimeBlockEnd(); // gpu
        }
        Profiler::TimeBlockEnd(); // cpu
    
        // pop the active time block
        m_active_timeblocks.pop();
    }

    void RHI_CommandList::UpdateBuffer(RHI_Buffer* buffer, const uint64_t offset, const uint64_t size, const void* data)
    {
        SP_ASSERT(buffer);
        SP_ASSERT(size);
        SP_ASSERT(data);
        SP_ASSERT(offset + size <= buffer->GetObjectSize());

        // check for vkCmdUpdateBuffer compliance
        bool synchronized_update  = true;
        synchronized_update      &= (offset % 4 == 0);                    // offset must be a multiple of 4
        synchronized_update      &= (size % 4 == 0);                      // size must be a multiple of 4
        synchronized_update      &= (size <= rhi_max_buffer_update_size); // size must not exceed 65536 bytes

        if (synchronized_update)
        {
            // end any active render pass before updating the buffer
            RenderPassEnd();
        
            // first barrier: ensure prior reads complete before the write
            VkBufferMemoryBarrier2 barrier_before = {};
            barrier_before.sType                  = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2;
            barrier_before.srcStageMask           = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
            barrier_before.srcAccessMask          = VK_ACCESS_2_MEMORY_READ_BIT;
            barrier_before.dstStageMask           = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
            barrier_before.dstAccessMask          = VK_ACCESS_2_TRANSFER_WRITE_BIT;
            barrier_before.srcQueueFamilyIndex    = VK_QUEUE_FAMILY_IGNORED;
            barrier_before.dstQueueFamilyIndex    = VK_QUEUE_FAMILY_IGNORED;
            barrier_before.buffer                 = static_cast<VkBuffer>(buffer->GetRhiResource());
            barrier_before.offset                 = offset;
            barrier_before.size                   = size;
        
            VkDependencyInfo dependency_info_before = {};
            dependency_info_before.sType                    = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
            dependency_info_before.bufferMemoryBarrierCount = 1;
            dependency_info_before.pBufferMemoryBarriers    = &barrier_before;
        
            vkCmdPipelineBarrier2(static_cast<VkCommandBuffer>(m_rhi_resource), &dependency_info_before);
            Profiler::m_rhi_pipeline_barriers++;
        
            // update the buffer
            vkCmdUpdateBuffer
            (
                static_cast<VkCommandBuffer>(m_rhi_resource),
                static_cast<VkBuffer>(buffer->GetRhiResource()),
                offset,
                size,
                data
            );
        
            // second barrier: ensure the write completes before all subsequent stages
            VkBufferMemoryBarrier2 barrier_after = {};
            barrier_after.sType                  = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2;
            barrier_after.srcStageMask           = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
            barrier_after.srcAccessMask          = VK_ACCESS_2_TRANSFER_WRITE_BIT;
            barrier_after.dstStageMask           = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
            barrier_after.dstAccessMask          = 0;
            barrier_after.srcQueueFamilyIndex    = VK_QUEUE_FAMILY_IGNORED;
            barrier_after.dstQueueFamilyIndex    = VK_QUEUE_FAMILY_IGNORED;
            barrier_after.buffer                 = static_cast<VkBuffer>(buffer->GetRhiResource());
            barrier_after.offset                 = offset;
            barrier_after.size                   = size;
        
            // adjust dstAccessMask for subsequent accesses based on buffer type
            switch (buffer->GetType())
            {
                case RHI_Buffer_Type::Vertex:
                case RHI_Buffer_Type::Instance:
                    barrier_after.dstAccessMask |= VK_ACCESS_2_VERTEX_ATTRIBUTE_READ_BIT;
                    break;
                case RHI_Buffer_Type::Index:
                    barrier_after.dstAccessMask |= VK_ACCESS_2_INDEX_READ_BIT;
                    break;
                case RHI_Buffer_Type::Storage:
                    barrier_after.dstAccessMask |= VK_ACCESS_2_SHADER_READ_BIT;
                    break;
                case RHI_Buffer_Type::Constant:
                    barrier_after.dstAccessMask |= VK_ACCESS_2_SHADER_READ_BIT | VK_ACCESS_2_UNIFORM_READ_BIT;
                    break;
                case RHI_Buffer_Type::ShaderBindingTable:
                    barrier_after.dstAccessMask |= VK_ACCESS_2_SHADER_BINDING_TABLE_READ_BIT_KHR;
                    break;
                default:
                    SP_ASSERT_MSG(false, "Unknown buffer type");
                    break;
            }
        
            VkDependencyInfo dependency_info_after          = {};
            dependency_info_after.sType                     = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
            dependency_info_after.bufferMemoryBarrierCount  = 1;
            dependency_info_after.pBufferMemoryBarriers     = &barrier_after;
        
            vkCmdPipelineBarrier2(static_cast<VkCommandBuffer>(m_rhi_resource), &dependency_info_after);
            Profiler::m_rhi_pipeline_barriers++;
        }
        else // big bindless arrays (updating these is up to the renderer)
        {
            void* mapped_data = static_cast<char*>(buffer->GetMappedData()) + offset;
            memcpy(mapped_data, data, size);
        }
    }

    void RHI_CommandList::InsertBarrier(const RHI_Barrier& barrier)
    {
        SP_ASSERT(m_state == RHI_CommandListState::Recording);

        switch (barrier.type)
        {
            case RHI_Barrier::Type::ImageLayout:
            {
                // get image and format from either texture or raw handle
                void* image           = barrier.texture ? barrier.texture->GetRhiResource() : barrier.image;
                RHI_Format format     = barrier.texture ? barrier.texture->GetFormat() : barrier.format;
                uint32_t array_length = barrier.texture ? barrier.texture->GetArrayLength() : barrier.array_length;
                uint32_t mip_count    = barrier.texture ? barrier.texture->GetMipCount() : rhi_max_mip_count;

                SP_ASSERT(image != nullptr);

                // handle mip specification
                bool mip_specified = barrier.mip_index != rhi_all_mips;
                uint32_t mip_index = mip_specified ? barrier.mip_index : 0;
                uint32_t mip_range = mip_specified ? barrier.mip_range : mip_count;

                SP_ASSERT(mip_index < rhi_max_mip_count);
                SP_ASSERT(mip_index + mip_range <= rhi_max_mip_count);

                bool is_depth        = format == RHI_Format::D16_Unorm || format == RHI_Format::D32_Float || format == RHI_Format::D32_Float_S8X24_Uint;
                uint32_t aspect_mask = get_aspect_mask(format);

                // get layouts for all mips in the range
                static thread_local vector<RHI_Image_Layout> layouts;
                layouts.clear();
                layouts.resize(mip_range);
                bool all_mips_same_layout     = true;
                RHI_Image_Layout first_layout = barrier_helpers::get_layout(image, mip_index);
                for (uint32_t i = 0; i < mip_range; i++)
                {
                    layouts[i] = barrier_helpers::get_layout(image, mip_index + i);
                    if (layouts[i] != first_layout || layouts[i] == barrier.layout)
                        all_mips_same_layout = false;
                }

                // early exit if all mips match target layout
                bool all_mips_match = true;
                for (const auto& layout : layouts)
                {
                    if (layout != barrier.layout)
                    {
                        all_mips_match = false;
                        break;
                    }
                }
                if (all_mips_match)
                    return;

                // create vulkan barriers
                static thread_local vector<VkImageMemoryBarrier2> vk_barriers;
                vk_barriers.clear();
                if (all_mips_same_layout)
                {
                    vk_barriers.push_back(barrier_helpers::create_image_barrier(
                        first_layout, barrier.layout, image, aspect_mask, mip_index, mip_range, array_length, is_depth,
                        barrier.scope_src, barrier.scope_dst
                    ));
                }
                else
                {
                    for (uint32_t i = 0; i < mip_range; i++)
                    {
                        if (layouts[i] != barrier.layout)
                        {
                            vk_barriers.push_back(barrier_helpers::create_image_barrier(
                                layouts[i], barrier.layout, image, aspect_mask, mip_index + i, 1, array_length, is_depth,
                                barrier.scope_src, barrier.scope_dst
                            ));
                        }
                    }
                }
                if (vk_barriers.empty())
                    return;

                // defer barriers and batch them (if eligible)
                if (!m_render_pass_active)
                {
                    bool immediate = first_layout == RHI_Image_Layout::Max                  ||
                                     first_layout == RHI_Image_Layout::Preinitialized       ||
                                     first_layout == RHI_Image_Layout::Transfer_Source      || barrier.layout == RHI_Image_Layout::Transfer_Source      ||
                                     first_layout == RHI_Image_Layout::Transfer_Destination || barrier.layout == RHI_Image_Layout::Transfer_Destination ||
                                     first_layout == RHI_Image_Layout::Present_Source       || barrier.layout == RHI_Image_Layout::Present_Source;

                    if (!immediate)
                    {
                        for (const auto& vk_barrier : vk_barriers)
                        {
                            RHI_Image_Layout old_layout = layouts[vk_barrier.subresourceRange.baseMipLevel - mip_index];
                            PendingBarrierInfo pending  = {};
                            pending.barrier             = barrier;
                            pending.image               = image;
                            pending.aspect_mask         = aspect_mask;
                            pending.mip_index           = vk_barrier.subresourceRange.baseMipLevel;
                            pending.mip_range           = vk_barrier.subresourceRange.levelCount;
                            pending.array_length        = array_length;
                            pending.layout_old          = old_layout;
                            pending.layout_new          = barrier.layout;
                            pending.is_depth            = is_depth;
                            m_pending_barriers.push_back(pending);
                        }
                        barrier_helpers::set_layout(image, mip_index, mip_range, barrier.layout);
                        return;
                    }
                }

                // immediate execution
                VkDependencyInfo dependency_info        = {};
                dependency_info.sType                   = VK_STRUCTURE_TYPE_DEPENDENCY_INFO_KHR;
                dependency_info.imageMemoryBarrierCount = static_cast<uint32_t>(vk_barriers.size());
                dependency_info.pImageMemoryBarriers    = vk_barriers.data();

                RenderPassEnd();
                vkCmdPipelineBarrier2(static_cast<VkCommandBuffer>(m_rhi_resource), &dependency_info);
                Profiler::m_rhi_pipeline_barriers++;
                barrier_helpers::set_layout(image, mip_index, mip_range, barrier.layout);
                break;
            }

            case RHI_Barrier::Type::ImageSync:
            {
                SP_ASSERT(barrier.texture != nullptr);

                VkPipelineStageFlags2 stages = (barrier.scope_src != RHI_Barrier_Scope::Auto)
                    ? barrier_helpers::scope_to_stages(barrier.scope_src)
                    : (VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT);

                VkImageMemoryBarrier2 vk_barrier        = {};
                vk_barrier.sType                        = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
                vk_barrier.srcStageMask                 = stages;
                vk_barrier.dstStageMask                 = (barrier.scope_dst != RHI_Barrier_Scope::Auto)
                    ? barrier_helpers::scope_to_stages(barrier.scope_dst) : stages;
                vk_barrier.srcQueueFamilyIndex          = VK_QUEUE_FAMILY_IGNORED;
                vk_barrier.dstQueueFamilyIndex          = VK_QUEUE_FAMILY_IGNORED;
                vk_barrier.image                        = static_cast<VkImage>(barrier.texture->GetRhiResource());
                vk_barrier.subresourceRange.aspectMask  = get_aspect_mask(barrier.texture->GetFormat());
                vk_barrier.subresourceRange.baseArrayLayer = 0;
                vk_barrier.subresourceRange.layerCount  = barrier.texture->GetType() == RHI_Texture_Type::Type3D ? 1 : barrier.texture->GetDepth();

                // set access masks based on sync type
                switch (barrier.sync_type)
                {
                    case RHI_BarrierType::EnsureWriteThenRead:
                        vk_barrier.srcAccessMask = VK_ACCESS_2_SHADER_WRITE_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT;
                        vk_barrier.dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_READ_BIT;
                        break;
                    case RHI_BarrierType::EnsureReadThenWrite:
                        vk_barrier.srcAccessMask = VK_ACCESS_2_SHADER_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_READ_BIT;
                        vk_barrier.dstAccessMask = VK_ACCESS_2_SHADER_WRITE_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT;
                        break;
                    case RHI_BarrierType::EnsureWriteThenWrite:
                        vk_barrier.srcAccessMask = VK_ACCESS_2_SHADER_WRITE_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT;
                        vk_barrier.dstAccessMask = VK_ACCESS_2_SHADER_WRITE_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT;
                        break;
                }

                VkDependencyInfo dependency_info = {};
                dependency_info.sType            = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;

                VkImageMemoryBarrier2 barriers[rhi_max_mip_count];
                if (barrier.texture->HasPerMipViews())
                {
                    for (uint32_t mip = 0; mip < barrier.texture->GetMipCount(); ++mip)
                    {
                        RHI_Image_Layout layout                   = barrier_helpers::get_layout(barrier.texture->GetRhiResource(), mip);
                        vk_barrier.oldLayout                      = vulkan_image_layout[static_cast<uint32_t>(layout)];
                        vk_barrier.newLayout                      = vulkan_image_layout[static_cast<uint32_t>(layout)]; // no transition
                        vk_barrier.subresourceRange.baseMipLevel  = mip;
                        vk_barrier.subresourceRange.levelCount    = 1;
                        barriers[mip]                             = vk_barrier;
                    }
                    dependency_info.imageMemoryBarrierCount = barrier.texture->GetMipCount();
                    dependency_info.pImageMemoryBarriers    = barriers;
                }
                else
                {
                    RHI_Image_Layout layout                  = barrier_helpers::get_layout(barrier.texture->GetRhiResource(), 0);
                    vk_barrier.oldLayout                     = vulkan_image_layout[static_cast<uint32_t>(layout)];
                    vk_barrier.newLayout                     = vulkan_image_layout[static_cast<uint32_t>(layout)]; // no transition
                    vk_barrier.subresourceRange.baseMipLevel = 0;
                    vk_barrier.subresourceRange.levelCount   = barrier.texture->GetMipCount();
                    dependency_info.imageMemoryBarrierCount  = 1;
                    dependency_info.pImageMemoryBarriers     = &vk_barrier;
                }

                RenderPassEnd();
                vkCmdPipelineBarrier2(static_cast<VkCommandBuffer>(m_rhi_resource), &dependency_info);
                Profiler::m_rhi_pipeline_barriers++;
                break;
            }

            case RHI_Barrier::Type::BufferSync:
            {
                SP_ASSERT(barrier.buffer != nullptr);

                VkBufferMemoryBarrier2 vk_barrier = {};
                vk_barrier.sType                  = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2;
                vk_barrier.srcStageMask           = (barrier.scope_src != RHI_Barrier_Scope::Auto)
                    ? barrier_helpers::scope_to_stages(barrier.scope_src)
                    : VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
                vk_barrier.srcAccessMask          = VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT;
                vk_barrier.dstStageMask           = (barrier.scope_dst != RHI_Barrier_Scope::Auto)
                    ? barrier_helpers::scope_to_stages(barrier.scope_dst)
                    : VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
                vk_barrier.dstAccessMask          = VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT;
                vk_barrier.buffer                 = static_cast<VkBuffer>(barrier.buffer->GetRhiResource());
                vk_barrier.offset                 = barrier.offset;
                vk_barrier.size                   = (barrier.size == 0) ? VK_WHOLE_SIZE : barrier.size;

                VkDependencyInfo dependency_info         = {};
                dependency_info.sType                    = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
                dependency_info.bufferMemoryBarrierCount = 1;
                dependency_info.pBufferMemoryBarriers    = &vk_barrier;

                RenderPassEnd();
                vkCmdPipelineBarrier2(static_cast<VkCommandBuffer>(m_rhi_resource), &dependency_info);
                Profiler::m_rhi_pipeline_barriers++;
                break;
            }
        }
    }

    void RHI_CommandList::FlushBarriers()
    {
        if (m_pending_barriers.empty())
            return;

        array<VkImageMemoryBarrier2, 32> vk_barriers;
        for (uint32_t i = 0; i < static_cast<uint32_t>(m_pending_barriers.size()); i++)
        {
            const PendingBarrierInfo& pending = m_pending_barriers[i];

            vk_barriers[i] = barrier_helpers::create_image_barrier(
                pending.layout_old,
                pending.layout_new,
                pending.image,
                pending.aspect_mask,
                pending.mip_index,
                pending.mip_range,
                pending.array_length,
                pending.is_depth,
                pending.barrier.scope_src,
                pending.barrier.scope_dst
            );
        }

        VkDependencyInfo dependency_info        = {};
        dependency_info.sType                   = VK_STRUCTURE_TYPE_DEPENDENCY_INFO_KHR;
        dependency_info.imageMemoryBarrierCount = static_cast<uint32_t>(m_pending_barriers.size());
        dependency_info.pImageMemoryBarriers    = vk_barriers.data();

        RenderPassEnd();
        vkCmdPipelineBarrier2(static_cast<VkCommandBuffer>(m_rhi_resource), &dependency_info);
        Profiler::m_rhi_pipeline_barriers++;
        m_pending_barriers.clear();
    }

    // convenience overloads
    void RHI_CommandList::InsertBarrier(RHI_Texture* texture, RHI_Image_Layout layout, uint32_t mip, uint32_t mip_range)
    {
        InsertBarrier(RHI_Barrier::image_layout(texture, layout, mip, mip_range));
    }

    void RHI_CommandList::InsertBarrier(RHI_Texture* texture, RHI_BarrierType sync_type)
    {
        InsertBarrier(RHI_Barrier::image_sync(texture, sync_type));
    }

    void RHI_CommandList::InsertBarrier(RHI_Buffer* buffer)
    {
        InsertBarrier(RHI_Barrier::buffer_sync(buffer));
    }

    void RHI_CommandList::InsertBarrier(void* image, RHI_Format format, uint32_t mip_index, uint32_t mip_range, uint32_t array_length, RHI_Image_Layout layout)
    {
        InsertBarrier(RHI_Barrier::image_layout(image, format, mip_index, mip_range, array_length, layout));
    }

    void RHI_CommandList::RemoveLayout(void* image)
    {
        barrier_helpers::remove_layout(image);
    }

    RHI_Image_Layout RHI_CommandList::GetImageLayout(void* image, const uint32_t mip_index)
    {
        return barrier_helpers::get_layout(image, mip_index);
    }

    void RHI_CommandList::CopyTextureToBuffer(RHI_Texture* source, RHI_Buffer* destination)
    {
        SP_ASSERT_MSG(source && destination, "Invalid source/destination");
        SP_ASSERT_MSG(source->GetWidth() && source->GetHeight(), "Source must have valid dimensions");

        // barrier to transfer src
        InsertBarrier(
            source->GetRhiResource(),
            source->GetFormat(),
            0,  // mip start
            1,  // mip count
            1,  // array length
            RHI_Image_Layout::Transfer_Source
        );

        // copy region (single mip/full extent)
        VkBufferImageCopy region{};
        region.bufferOffset                    = 0;
        region.bufferRowLength                 = 0;
        region.bufferImageHeight               = 0;
        region.imageSubresource.aspectMask     = get_aspect_mask(source->GetFormat());
        region.imageSubresource.mipLevel       = 0;
        region.imageSubresource.baseArrayLayer = 0;
        region.imageSubresource.layerCount     = 1;
        region.imageOffset                     = { 0, 0, 0 };
        region.imageExtent                     = { static_cast<uint32_t>(source->GetWidth()), static_cast<uint32_t>(source->GetHeight()), 1 };

        vkCmdCopyImageToBuffer(
            static_cast<VkCommandBuffer>(GetRhiResource()),
            static_cast<VkImage>(source->GetRhiResource()),
            VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            static_cast<VkBuffer>(destination->GetRhiResource()),
            1, &region
        );

        // barrier back to shader read (or your tracked layout)
        InsertBarrier(
            source->GetRhiResource(),
            source->GetFormat(),
            0,
            1,
            1,
            RHI_Image_Layout::Shader_Read
        );
    }

    void RHI_CommandList::PreDraw()
    {
        FlushBarriers();

        if (!m_render_pass_active && m_pso.IsGraphics())
        {
            RenderPassBegin();
        }

        if (descriptor_sets::bind_dynamic)
        {
            descriptor_sets::set_dynamic(m_pso, m_rhi_resource, m_pipeline->GetRhiResourceLayout(), m_descriptor_layout_current);
        }
    }
}
