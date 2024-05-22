/*
Copyright(c) 2016-2024 Panos Karabelas

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
#include "../RHI_VertexBuffer.h"
#include "../RHI_IndexBuffer.h"
#include "../RHI_ConstantBuffer.h"
#include "../RHI_StructuredBuffer.h"
#include "../RHI_Sampler.h"
#include "../RHI_DescriptorSet.h"
#include "../RHI_DescriptorSetLayout.h"
#include "../RHI_Semaphore.h"
#include "../RHI_SwapChain.h"
#include "../RHI_RasterizerState.h"
#include "../RHI_DepthStencilState.h"
#include "../Rendering/Renderer.h"
#include "../../Profiling/Profiler.h"
//=====================================

//= NAMESPACES ===============
using namespace std;
using namespace Spartan::Math;
//============================

namespace Spartan
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

        VkAccessFlags2 layout_to_access_mask(const VkImageLayout layout, const bool is_destination_mask, const bool is_depth)
        {
            VkAccessFlags2 access_mask = 0;

            switch (layout)
            {
            case VK_IMAGE_LAYOUT_UNDEFINED:
                // a newly created swapchain will have an undefined layout and if use a 0 mask the validation layer
                // will complain so we add a generic access mask that's harmless for VK_IMAGE_LAYOUT_UNDEFINED transitions
                if (!is_destination_mask)
                {
                    access_mask = VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT;
                }
                else
                {
                    SP_ASSERT(!is_destination_mask && "The new layout used in a transition must not be VK_IMAGE_LAYOUT_UNDEFINED.");
                }
                break;

            case VK_IMAGE_LAYOUT_PREINITIALIZED:
                SP_ASSERT(!is_destination_mask && "The new layout used in a transition must not be VK_IMAGE_LAYOUT_PREINITIALIZED.");
                access_mask = VK_ACCESS_2_HOST_WRITE_BIT;
                break;

            // When transitioning the image to VK_IMAGE_LAYOUT_SHARED_PRESENT_KHR or VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
            // there is no need to delay subsequent processing, or perform any visibility operations (as vkQueuePresentKHR
            // performs automatic visibility operations). To achieve this, the dstAccessMask member of the VkImageMemoryBarrier
            // should be set to 0, and the dstStageMask parameter should be set to VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT.
            case VK_IMAGE_LAYOUT_PRESENT_SRC_KHR:
                access_mask = VK_ACCESS_2_NONE;
                break;

                // transfer
            case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:
                access_mask = VK_ACCESS_2_TRANSFER_READ_BIT;
                break;

            case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
                access_mask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
                break;

             // attachments
            case VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL:
                if (is_depth)
                {
                    access_mask = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT_KHR | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT_KHR;
                }
                else
                { 
                    access_mask = VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT_KHR | VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT_KHR;
                }
                break;

            case VK_IMAGE_LAYOUT_FRAGMENT_SHADING_RATE_ATTACHMENT_OPTIMAL_KHR:
                access_mask = VK_ACCESS_2_FRAGMENT_SHADING_RATE_ATTACHMENT_READ_BIT_KHR;
                break;

                // shader reads
            case VK_IMAGE_LAYOUT_GENERAL:
                access_mask = VK_ACCESS_2_SHADER_READ_BIT | VK_ACCESS_2_SHADER_WRITE_BIT;
                break;

            case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
                access_mask = VK_ACCESS_2_SHADER_READ_BIT;
                break;

            case VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_OPTIMAL:
                access_mask = VK_ACCESS_2_SHADER_READ_BIT;
                break;

            default:
                SP_ASSERT_MSG(false, "Unhandled layout");
                break;
            }

            return access_mask;
        }

        VkPipelineStageFlags2 access_mask_to_pipeline_stage_mask(VkAccessFlags2 access_flags)
        {
            VkPipelineStageFlags2 stages     = 0;
            uint32_t enabled_graphics_stages = RHI_Device::GetEnabledGraphicsStages();

            while (access_flags != 0)
            {
                VkAccessFlagBits2 access_flag = static_cast<VkAccessFlagBits2>(access_flags & (~(access_flags - 1)));
                SP_ASSERT(access_flag != 0 && (access_flag & (access_flag - 1)) == 0);
                access_flags &= ~access_flag;

                switch (access_flag)
                {
                case VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT:
                    stages |= VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT;
                    break;

                case VK_ACCESS_2_INDEX_READ_BIT:
                    stages |= VK_PIPELINE_STAGE_2_VERTEX_INPUT_BIT;
                    break;

                case VK_ACCESS_2_VERTEX_ATTRIBUTE_READ_BIT:
                    stages |= VK_PIPELINE_STAGE_2_VERTEX_INPUT_BIT;
                    break;

                case VK_ACCESS_2_UNIFORM_READ_BIT:
                    stages |= enabled_graphics_stages | VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
                    break;

                case VK_ACCESS_2_INPUT_ATTACHMENT_READ_BIT:
                    stages |= VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
                    break;

                    // shader
                case VK_ACCESS_2_SHADER_READ_BIT:
                    stages |= enabled_graphics_stages | VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
                    break;

                case VK_ACCESS_2_SHADER_WRITE_BIT:
                    stages |= enabled_graphics_stages | VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
                    break;

                    // attachments - color
                case VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT:
                    stages |= VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
                    break;

                case VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT:
                    stages |= VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
                    break;

                    // attachments - depth/stencil
                case VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT:
                    stages |= VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
                    break;

                    // attachments - shading rate
                case VK_ACCESS_2_FRAGMENT_SHADING_RATE_ATTACHMENT_READ_BIT_KHR:
                    stages |= VK_PIPELINE_STAGE_2_FRAGMENT_SHADING_RATE_ATTACHMENT_BIT_KHR;
                    break;

                case VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT:
                    stages |= VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;
                    break;

                    // transfer
                case VK_ACCESS_2_TRANSFER_READ_BIT:
                    stages |= VK_PIPELINE_STAGE_2_TRANSFER_BIT;
                    break;

                case VK_ACCESS_2_TRANSFER_WRITE_BIT:
                    stages |= VK_PIPELINE_STAGE_2_TRANSFER_BIT;
                    break;

                    // host
                case VK_ACCESS_2_HOST_READ_BIT:
                    stages |= VK_PIPELINE_STAGE_2_HOST_BIT;
                    break;

                case VK_ACCESS_2_HOST_WRITE_BIT:
                    stages |= VK_PIPELINE_STAGE_2_HOST_BIT;
                    break;

                    // misc
                case VK_ACCESS_2_MEMORY_READ_BIT:
                    stages |= VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
                    break;

                case VK_ACCESS_2_MEMORY_WRITE_BIT:
                    stages |= VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
                    break;

                default:
                    SP_ASSERT_MSG(false, "Unhandled access flag");
                    break;
                }
            }
            return stages;
        }

        uint32_t get_aspect_mask(const RHI_Texture* texture, const bool only_depth = false, const bool only_stencil = false)
        {
            uint32_t aspect_mask = 0;

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
    }

    namespace descriptor_sets
    {
        bool bind_dynamic = false;

        void set_dynamic(const RHI_PipelineState pso, void* resource, void* pipeline_layout, RHI_DescriptorSetLayout* layout)
        {
            array<void*, 1> resources =
            {
                layout->GetDescriptorSet()->GetResource()
            };

            // get dynamic offsets
            array<uint32_t, 10> dynamic_offsets;
            uint32_t dynamic_offset_count = 0;
            layout->GetDynamicOffsets(&dynamic_offsets, &dynamic_offset_count);

            VkPipelineBindPoint bind_point = pso.IsCompute() ? VkPipelineBindPoint::VK_PIPELINE_BIND_POINT_COMPUTE : VkPipelineBindPoint::VK_PIPELINE_BIND_POINT_GRAPHICS;
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
            Profiler::m_rhi_bindings_descriptor_set++;
        }

        void set_bindless(const RHI_PipelineState pso, void* resource, void* pipeline_layout)
        {
            array<void*, 3> resources =
            {
                RHI_Device::GetDescriptorSet(RHI_Device_Resource::textures_material),
                RHI_Device::GetDescriptorSet(RHI_Device_Resource::sampler_comparison),
                RHI_Device::GetDescriptorSet(RHI_Device_Resource::sampler_regular)
            };

            VkPipelineBindPoint bind_point = pso.IsCompute() ? VkPipelineBindPoint::VK_PIPELINE_BIND_POINT_COMPUTE : VkPipelineBindPoint::VK_PIPELINE_BIND_POINT_GRAPHICS;
            vkCmdBindDescriptorSets
            (
                static_cast<VkCommandBuffer>(resource),               // commandBuffer
                bind_point,                                           // pipelineBindPoint
                static_cast<VkPipelineLayout>(pipeline_layout),       // layout
                1,                                                    // firstSet
                static_cast<uint32_t>(resources.size()),              // descriptorSetCount
                reinterpret_cast<VkDescriptorSet*>(resources.data()), // pDescriptorSets
                0,                                                    // dynamicOffsetCount
                nullptr                                               // pDynamicOffsets
            );

            Profiler::m_rhi_bindings_descriptor_set++;
        }
    }

    namespace queries
    {
        namespace timestamp
        {
            array<uint64_t, rhi_max_queries_timestamps> data;

            void update(void* query_pool, const uint32_t query_count)
            {
                if (Profiler::IsGpuTimingEnabled())
                {
                    vkGetQueryPoolResults(
                        RHI_Context::device,                  // device
                        static_cast<VkQueryPool>(query_pool), // queryPool
                        0,                                    // firstQuery
                        query_count,                          // queryCount
                        query_count * sizeof(uint64_t),       // dataSize
                        queries::timestamp::data.data(),      // pData
                        sizeof(uint64_t),                     // stride
                        VK_QUERY_RESULT_64_BIT                // flags
                    );
                }
            }

            void reset(void* cmd_list, void*& query_pool)
            {
                vkCmdResetQueryPool(static_cast<VkCommandBuffer>(cmd_list), static_cast<VkQueryPool>(query_pool), 0, rhi_max_queries_timestamps);
            }
        }

        namespace occlusion
        {
            array<uint64_t, rhi_max_queries_occlusion> data;
            unordered_map<uint64_t, uint32_t> id_to_index;
            uint32_t index              = 0;
            uint32_t index_active       = 0;
            bool occlusion_query_active = false;

            void update(void* query_pool, const uint32_t query_count)
            {
                vkGetQueryPoolResults(
                    RHI_Context::device,                                 // device
                    static_cast<VkQueryPool>(query_pool),                // queryPool
                    0,                                                   // firstQuery
                    query_count,                                         // queryCount
                    query_count * sizeof(uint64_t),                      // dataSize
                    queries::occlusion::data.data(),                     // pData
                    sizeof(uint64_t),                                    // stride
                    VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_PARTIAL_BIT // flags
                );
            }

            void reset(void* cmd_list, void*& query_pool)
            {
                vkCmdResetQueryPool(static_cast<VkCommandBuffer>(cmd_list), static_cast<VkQueryPool>(query_pool), 0, rhi_max_queries_occlusion);
            }
        }

        void initialize(void*& pool_timestamp, void*& pool_occlusion)
        {
            // timestamps
            if (Profiler::IsGpuTimingEnabled())
            {
                VkQueryPoolCreateInfo query_pool_info = {};
                query_pool_info.sType                 = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
                query_pool_info.queryType             = VK_QUERY_TYPE_TIMESTAMP;
                query_pool_info.queryCount            = rhi_max_queries_timestamps;

                auto query_pool = reinterpret_cast<VkQueryPool*>(&pool_timestamp);
                SP_ASSERT_VK_MSG(vkCreateQueryPool(RHI_Context::device, &query_pool_info, nullptr, query_pool),
                    "Failed to created timestamp query pool");

                RHI_Device::SetResourceName(pool_timestamp, RHI_Resource_Type::QueryPool, "query_pool_timestamp");
            }

            // occlusion
            {
                VkQueryPoolCreateInfo query_pool_info = {};
                query_pool_info.sType                 = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
                query_pool_info.queryType             = VK_QUERY_TYPE_OCCLUSION;
                query_pool_info.queryCount            = rhi_max_queries_occlusion;

                auto query_pool = reinterpret_cast<VkQueryPool*>(&pool_occlusion);
                SP_ASSERT_VK_MSG(vkCreateQueryPool(RHI_Context::device, &query_pool_info, nullptr, query_pool),
                    "Failed to created occlusion query pool");

                RHI_Device::SetResourceName(pool_occlusion, RHI_Resource_Type::QueryPool, "query_pool_occlusion");
            }

            timestamp::data.fill(0);
            occlusion::data.fill(0);
        }

        void shutdown(void*& pool_timestamp, void*& pool_occlusion)
        {
            RHI_Device::DeletionQueueAdd(RHI_Resource_Type::QueryPool, pool_timestamp);
            RHI_Device::DeletionQueueAdd(RHI_Resource_Type::QueryPool, pool_occlusion);
        }
    }

    RHI_CommandList::RHI_CommandList(void* cmd_pool, const char* name)
    {
        // command buffer
        {
            // define
            VkCommandBufferAllocateInfo allocate_info = {};
            allocate_info.sType                       = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
            allocate_info.commandPool                 = static_cast<VkCommandPool>(cmd_pool);
            allocate_info.level                       = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
            allocate_info.commandBufferCount          = 1;

            // allocate
            SP_ASSERT_VK_MSG(vkAllocateCommandBuffers(RHI_Context::device, &allocate_info, reinterpret_cast<VkCommandBuffer*>(&m_rhi_resource)),
                "Failed to allocate command buffer");

            // name
            RHI_Device::SetResourceName(static_cast<void*>(m_rhi_resource), RHI_Resource_Type::CommandList, name);
        }

        // semaphores
        m_rendering_complete_semaphore          = make_shared<RHI_Semaphore>(false, name);
        m_rendering_complete_semaphore_timeline = make_shared<RHI_Semaphore>(true, name);

        queries::initialize(m_rhi_query_pool_timestamps, m_rhi_query_pool_occlusion);
    }

    RHI_CommandList::~RHI_CommandList()
    {
        queries::shutdown(m_rhi_query_pool_timestamps, m_rhi_query_pool_occlusion);
    }

    void RHI_CommandList::Begin(const RHI_Queue* queue)
    {
        if (m_state == RHI_CommandListState::Recording)
        {
            SP_LOG_WARNING("Discarding all previously recorded commands as the command list is already in recording state...");
        }
 
        if (queue->GetType() != RHI_Queue_Type::Copy && m_timestamp_index != 0)
        {
            queries::timestamp::update(m_rhi_query_pool_timestamps, m_timestamp_index);
        }

        // begin command buffer
        VkCommandBufferBeginInfo begin_info = {};
        begin_info.sType                    = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        SP_ASSERT_MSG(vkBeginCommandBuffer(static_cast<VkCommandBuffer>(m_rhi_resource), &begin_info) == VK_SUCCESS, "Failed to begin command buffer");

        // set states
        m_state        = RHI_CommandListState::Recording;
        m_pso          = RHI_PipelineState();
        m_cull_mode    = RHI_CullMode::Max;

        // set dynamic states
        if (queue->GetType() == RHI_Queue_Type::Graphics)
        {
            // cull mode
            SetCullMode(RHI_CullMode::Back);

            // scissor rectangle
            static Math::Rectangle scissor_rect;
            scissor_rect.left   = 0.0f;
            scissor_rect.top    = 0.0f;
            scissor_rect.right  = static_cast<float>(m_pso.GetWidth());
            scissor_rect.bottom = static_cast<float>(m_pso.GetHeight());
            SetScissorRectangle(scissor_rect);
        }

        // queries
        if (queue->GetType() != RHI_Queue_Type::Copy)
        {
            // queries need to be reset before they are first used and they
            // also need to be reset after every use, so we just reset them always
            m_timestamp_index = 0;
            queries::timestamp::reset(m_rhi_resource, m_rhi_query_pool_timestamps);
            queries::occlusion::reset(m_rhi_resource, m_rhi_query_pool_occlusion);
        }
    }

    void RHI_CommandList::Submit(RHI_Queue* queue, const uint64_t swapchain_id)
    {
        SP_ASSERT(m_state == RHI_CommandListState::Recording);

        // end
        RenderPassEnd(); // only happens if needed
        SP_ASSERT_VK_MSG(vkEndCommandBuffer(static_cast<VkCommandBuffer>(m_rhi_resource)), "Failed to end command buffer");

        // when minimized, or when entering/exiting fullscreen mode, the swapchain
        // won't present, and won't wait for this semaphore, so we need to reset it
        if (m_rendering_complete_semaphore->IsSignaled())
        {
            m_rendering_complete_semaphore = make_shared<RHI_Semaphore>(false, m_rendering_complete_semaphore_timeline->GetObjectName().c_str());
        }

        queue->Submit(
            static_cast<VkCommandBuffer>(m_rhi_resource), // cmd buffer
            VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,            // wait flags
            m_rendering_complete_semaphore.get(),         // signal semaphore
            m_rendering_complete_semaphore_timeline.get() // signal semaphore
        );

        m_swapchain_id = swapchain_id;
        m_state        = RHI_CommandListState::Submitted;
    }

    void RHI_CommandList::SetPipelineState(RHI_PipelineState& pso)
    {
        SP_ASSERT(m_state == RHI_CommandListState::Recording);

        // early exit if the pipeline state hasn't changed
        pso.Prepare();
        if (m_pso.GetHash() == pso.GetHash())
            return;

        // get (or create) a pipeline which matches the requested pipeline state
        m_pso = pso;
        RHI_Device::GetOrCreatePipeline(m_pso, m_pipeline, m_descriptor_layout_current);

        // bind pipeline
        {
            // get vulkan pipeline object
            SP_ASSERT(m_pipeline != nullptr);
            VkPipeline vk_pipeline = static_cast<VkPipeline>(m_pipeline->GetResource_Pipeline());
            SP_ASSERT(vk_pipeline != nullptr);

            // bind
            VkPipelineBindPoint pipeline_bind_point = m_pso.IsCompute() ? VK_PIPELINE_BIND_POINT_COMPUTE : VK_PIPELINE_BIND_POINT_GRAPHICS;
            vkCmdBindPipeline(static_cast<VkCommandBuffer>(m_rhi_resource), pipeline_bind_point, vk_pipeline);

            // profile
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
                Math::Rectangle scissor_rect;
                scissor_rect.left   = 0.0f;
                scissor_rect.top    = 0.0f;
                scissor_rect.right  = static_cast<float>(m_pso.GetWidth());
                scissor_rect.bottom = static_cast<float>(m_pso.GetHeight());
                SetScissorRectangle(scissor_rect);

                // vertex and index buffer state
                m_buffer_id_index  = 0;
                m_buffer_id_vertex = 0;
            }
        }

        // bind descriptors
        {
            // set bindless descriptors
            descriptor_sets::set_bindless(m_pso, m_rhi_resource, m_pipeline->GetResource_PipelineLayout());

            // set standard resources (dynamic descriptors)
            Renderer::SetStandardResources(this);
            descriptor_sets::set_dynamic(m_pso, m_rhi_resource, m_pipeline->GetResource_PipelineLayout(), m_descriptor_layout_current);
        }

        RenderPassBegin();
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
        vector<VkRenderingAttachmentInfo> attachments_color;
        {
            // swapchain buffer as a render target
            RHI_SwapChain* swapchain = m_pso.render_target_swapchain;
            if (swapchain)
            {
                // transition to the appropriate layout
                swapchain->SetLayout(RHI_Image_Layout::Attachment, this);

                VkRenderingAttachmentInfo color_attachment = {};
                color_attachment.sType                     = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO_KHR;
                color_attachment.imageView                 = static_cast<VkImageView>(swapchain->GetRhiRtv());
                color_attachment.imageLayout               = vulkan_image_layout[static_cast<uint8_t>(swapchain->GetLayout())];
                color_attachment.loadOp                    = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
                color_attachment.storeOp                   = VK_ATTACHMENT_STORE_OP_STORE;

                SP_ASSERT(color_attachment.imageView != nullptr);

                attachments_color.push_back(color_attachment);
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
                    color_attachment.loadOp                    = m_ignore_clear_values ? VK_ATTACHMENT_LOAD_OP_LOAD : get_color_load_op(m_pso.clear_color[i]);
                    color_attachment.storeOp                   = VK_ATTACHMENT_STORE_OP_STORE;
                    color_attachment.clearValue.color          = { m_pso.clear_color[i].r, m_pso.clear_color[i].g, m_pso.clear_color[i].b, m_pso.clear_color[i].a };

                    SP_ASSERT(color_attachment.imageView != nullptr);

                    attachments_color.push_back(color_attachment);
                }
            }
            rendering_info.colorAttachmentCount = static_cast<uint32_t>(attachments_color.size());
            rendering_info.pColorAttachments    = attachments_color.data();
        }

        // depth-stencil attachment
        VkRenderingAttachmentInfoKHR attachment_depth_stencil = {};
        if (m_pso.render_target_depth_texture != nullptr)
        {
            RHI_Texture* rt = m_pso.render_target_depth_texture;
            if (Renderer::GetOption<float>(Renderer_Option::ResolutionScale) == 1.0f)
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
            attachment_depth_stencil.loadOp                          = m_ignore_clear_values ? VK_ATTACHMENT_LOAD_OP_LOAD : get_depth_load_op(m_pso.clear_depth);
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
        vkCmdBeginRendering(static_cast<VkCommandBuffer>(m_rhi_resource), &rendering_info);

        // set dynamic states
        {
            // variable rate shading
            RHI_Device::SetVariableRateShading(this, m_pso.vrs_input_texture != nullptr);

            // set viewport
            RHI_Viewport viewport = RHI_Viewport(
                0.0f, 0.0f,
                static_cast<float>(m_pso.GetWidth()),
                static_cast<float>(m_pso.GetHeight())
            );
            SetViewport(viewport);
        }

        m_render_pass_active  = true;
        m_ignore_clear_values = true;
    }

    void RHI_CommandList::RenderPassEnd()
    {
        if (!m_render_pass_active)
            return;

        vkCmdEndRendering(static_cast<VkCommandBuffer>(m_rhi_resource));
        m_render_pass_active = false;

        if (m_pso.render_target_swapchain)
        {
            m_pso.render_target_swapchain->SetLayout(RHI_Image_Layout::Present_Source, this);
        }
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

    void RHI_CommandList::ClearRenderTarget(
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

    void RHI_CommandList::DrawIndexed(const uint32_t index_count, const uint32_t index_offset, const uint32_t vertex_offset, const uint32_t instance_start_index, const uint32_t instance_count)
    {
        SP_ASSERT(m_state == RHI_CommandListState::Recording);

        PreDraw();

        vkCmdDrawIndexed(
            static_cast<VkCommandBuffer>(m_rhi_resource), // commandBuffer
            index_count,                                  // indexCount
            instance_count,                               // instanceCount
            index_offset,                                 // firstIndex
            vertex_offset,                                // vertexOffset
            instance_start_index                          // firstInstance
        );
        Profiler::m_rhi_draw++;
    }

    void RHI_CommandList::Dispatch(uint32_t x, uint32_t y, uint32_t z /*= 1*/)
    {
        SP_ASSERT(m_state == RHI_CommandListState::Recording);

        if (descriptor_sets::bind_dynamic)
        {
            descriptor_sets::set_dynamic(m_pso, m_rhi_resource, m_pipeline->GetResource_PipelineLayout(), m_descriptor_layout_current);
        }

        vkCmdDispatch(static_cast<VkCommandBuffer>(m_rhi_resource), x, y, z);
    }

    void RHI_CommandList::Blit(RHI_Texture* source, RHI_Texture* destination, const bool blit_mips, const float source_scaling)
    {
        SP_ASSERT_MSG((source->GetFlags() & RHI_Texture_ClearBlit) != 0,      "The texture needs the RHI_Texture_ClearOrBlit flag");
        SP_ASSERT_MSG((destination->GetFlags() & RHI_Texture_ClearBlit) != 0, "The texture needs the RHI_Texture_ClearOrBlit flag");
        if (blit_mips)
        {
            SP_ASSERT_MSG(source->GetMipCount() == destination->GetMipCount(),
                "If the mips are blitted, then the mip count between the source and the destination textures must match");
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
            blit_region.srcSubresource.aspectMask     = get_aspect_mask(source);
            blit_region.srcOffsets[0]                 = { 0, 0, 0 };
            blit_region.srcOffsets[1]                 = source_blit_size;
            blit_region.dstSubresource.mipLevel       = mip_index;
            blit_region.dstSubresource.baseArrayLayer = 0;
            blit_region.dstSubresource.layerCount     = 1;
            blit_region.dstSubresource.aspectMask     = get_aspect_mask(destination);
            blit_region.dstOffsets[0]                 = { 0, 0, 0 };
            blit_region.dstOffsets[1]                 = destination_blit_size;
        }

        // save the initial layouts
        array<RHI_Image_Layout, rhi_max_mip_count> layouts_initial_source      = source->GetLayouts();
        array<RHI_Image_Layout, rhi_max_mip_count> layouts_initial_destination = destination->GetLayouts();

        // transition to blit appropriate layouts
        source->SetLayout(RHI_Image_Layout::Transfer_Source, this);
        destination->SetLayout(RHI_Image_Layout::Transfer_Destination, this);

        // blit
        vkCmdBlitImage(
            static_cast<VkCommandBuffer>(m_rhi_resource),
            static_cast<VkImage>(source->GetRhiResource()),      VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            static_cast<VkImage>(destination->GetRhiResource()), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            blit_region_count, &blit_regions[0],
            vulkan_filter[static_cast<uint32_t>(destination->IsDepthFormat() ? RHI_Filter::Nearest : RHI_Filter::Linear)]
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
        blit_region.srcSubresource.aspectMask     = get_aspect_mask(source);
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
        destination->SetLayout(RHI_Image_Layout::Transfer_Destination, this);

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
        destination->SetLayout(RHI_Image_Layout::Present_Source, this);
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

        // Transition to blit appropriate layouts
        RHI_Image_Layout layout_initial_source = source->GetLayout(0);
        source->SetLayout(RHI_Image_Layout::Transfer_Source, this);
        destination->SetLayout(RHI_Image_Layout::Transfer_Destination, this);

        // Blit
        vkCmdCopyImage(
            static_cast<VkCommandBuffer>(m_rhi_resource),
            static_cast<VkImage>(source->GetRhiResource()), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            static_cast<VkImage>(destination->GetRhiRt()),  VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            1, &copy_region
        );

        // Transition to the initial layout
        source->SetLayout(layout_initial_source, this);
        destination->SetLayout(RHI_Image_Layout::Present_Source, this);
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

    void RHI_CommandList::SetScissorRectangle(const Math::Rectangle& scissor_rectangle) const
    {
        SP_ASSERT(m_state == RHI_CommandListState::Recording);

        VkRect2D vk_scissor;
        vk_scissor.offset.x      = static_cast<int32_t>(scissor_rectangle.left);
        vk_scissor.offset.y      = static_cast<int32_t>(scissor_rectangle.top);
        vk_scissor.extent.width  = static_cast<uint32_t>(scissor_rectangle.Width());
        vk_scissor.extent.height = static_cast<uint32_t>(scissor_rectangle.Height());

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

    void RHI_CommandList::SetBufferVertex(const RHI_VertexBuffer* buffer, const uint32_t binding /*= 0*/)
    {
        SP_ASSERT(m_state == RHI_CommandListState::Recording);
        SP_ASSERT(buffer != nullptr);
        SP_ASSERT(buffer->GetRhiResource() != nullptr);

        if (m_buffer_id_vertex == buffer->GetObjectId())
            return;

        VkBuffer vertex_buffers[] = { static_cast<VkBuffer>(buffer->GetRhiResource()) };
        VkDeviceSize offsets[]    = { 0 };

        vkCmdBindVertexBuffers(
            static_cast<VkCommandBuffer>(m_rhi_resource), // commandBuffer
            binding,                                      // firstBinding
            1,                                            // bindingCount
            vertex_buffers,                               // pBuffers
            offsets                                       // pOffsets
        );

        m_buffer_id_vertex = buffer->GetObjectId();
        Profiler::m_rhi_bindings_buffer_vertex++;
    }

    void RHI_CommandList::SetBufferIndex(const RHI_IndexBuffer* buffer)
    {
        SP_ASSERT(m_state == RHI_CommandListState::Recording);
        SP_ASSERT(buffer != nullptr);
        SP_ASSERT(buffer->GetRhiResource() != nullptr);

        if (m_buffer_id_index == buffer->GetObjectId())
            return;

        vkCmdBindIndexBuffer(
            static_cast<VkCommandBuffer>(m_rhi_resource),                   // commandBuffer
            static_cast<VkBuffer>(buffer->GetRhiResource()),                // buffer
            0,                                                              // offset
            buffer->Is16Bit() ? VK_INDEX_TYPE_UINT16 : VK_INDEX_TYPE_UINT32 // indexType
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

        vkCmdPushConstants(
            static_cast<VkCommandBuffer>(m_rhi_resource),
            static_cast<VkPipelineLayout>(m_pipeline->GetResource_PipelineLayout()),
            stages,
            offset,
            size,
            data
        );
    }

    void RHI_CommandList::SetConstantBuffer(const uint32_t slot, RHI_ConstantBuffer* constant_buffer) const
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

    void RHI_CommandList::SetSampler(const uint32_t slot, RHI_Sampler* sampler) const
    {
        SP_ASSERT(m_state == RHI_CommandListState::Recording);

        if (!m_descriptor_layout_current)
        {
            SP_LOG_WARNING("Descriptor layout not set, try setting sampler \"%s\" within a render pass", sampler->GetObjectName().c_str());
            return;
        }

        // Set (will only happen if it's not already set)
        m_descriptor_layout_current->SetSampler(slot, sampler);
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

        // If the texture is null or it's still loading, ignore it.
        if (!texture || !texture->IsReadyForUse())
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
                target_layout = texture->IsColorFormat() ? RHI_Image_Layout::Shader_Read : RHI_Image_Layout::Shader_Read_Depth;
            }

            // verify that an appropriate layout has been deduced
            SP_ASSERT(target_layout != RHI_Image_Layout::Max);

            // determine if a layout transition is needed
            bool transition_required = current_layout != target_layout;
            {
                bool rest_mips_have_same_layout = true;
                array<RHI_Image_Layout, rhi_max_mip_count> layouts = texture->GetLayouts();
                for (uint32_t i = mip_start; i < mip_start + mip_count; i++)
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
                RenderPassEnd(); // transitioning to a different layout must happen outside of a render pass
                texture->SetLayout(target_layout, this, mip_index, mip_range);
            }
        }

        // Set (will only happen if it's not already set)
        m_descriptor_layout_current->SetTexture(slot, texture, mip_index, mip_range);

        // todo: detect if there are changes, otherwise don't bother binding
        descriptor_sets::bind_dynamic = true;
    }

    void RHI_CommandList::SetStructuredBuffer(const uint32_t slot, RHI_StructuredBuffer* structured_buffer) const
    {
        SP_ASSERT(m_state == RHI_CommandListState::Recording);

        if (!m_descriptor_layout_current)
        {
            SP_LOG_WARNING("Descriptor layout not set, try setting structured buffer \"%s\" within a render pass", structured_buffer->GetObjectName().c_str());
            return;
        }

        m_descriptor_layout_current->SetStructuredBuffer(slot, structured_buffer);

        // todo: detect if there are changes, otherwise don't bother binding
        descriptor_sets::bind_dynamic = true;
    }

    void RHI_CommandList::BeginMarker(const char* name)
    {
        if (Profiler::IsGpuMarkingEnabled())
        {
            RHI_Device::MarkerBegin(this, name, Vector4::Zero);
        }
    }

    void RHI_CommandList::EndMarker()
    {
        if (Profiler::IsGpuMarkingEnabled())
        {
            RHI_Device::MarkerEnd(this);
        }
    }
    
    uint32_t RHI_CommandList::BeginTimestamp()
    {
        SP_ASSERT(m_state == RHI_CommandListState::Recording);

        uint32_t timestamp_index = m_timestamp_index;

        vkCmdWriteTimestamp(
            static_cast<VkCommandBuffer>(m_rhi_resource),
            VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
            static_cast<VkQueryPool>(m_rhi_query_pool_timestamps),
            m_timestamp_index++
        );

        return timestamp_index;
    }

    void RHI_CommandList::EndTimestamp()
    {
        SP_ASSERT(m_state == RHI_CommandListState::Recording);

        vkCmdWriteTimestamp(
            static_cast<VkCommandBuffer>(m_rhi_resource),
            VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
            static_cast<VkQueryPool>(m_rhi_query_pool_timestamps),
            m_timestamp_index++
        );
    }

    float RHI_CommandList::GetTimestampResult(const uint32_t index_timestamp)
    {
        SP_ASSERT_MSG(index_timestamp + 1 < queries::timestamp::data.size(), "index out of range");

        uint64_t start    = queries::timestamp::data[index_timestamp];
        uint64_t end      = queries::timestamp::data[index_timestamp + 1];
        uint64_t duration = Math::Helper::Clamp<uint64_t>(end - start, 0, numeric_limits<uint64_t>::max());
        float duration_ms = static_cast<float>(duration * RHI_Device::PropertyGetTimestampPeriod() * 1e-6f);

        return Math::Helper::Clamp<float>(duration_ms, 0.0f, numeric_limits<float>::max());
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
        queries::occlusion::update(m_rhi_query_pool_occlusion, rhi_max_queries_occlusion);
    }

    void RHI_CommandList::BeginTimeblock(const char* name, const bool gpu_marker, const bool gpu_timing)
    {
        SP_ASSERT_MSG(m_timeblock_active == nullptr, "The previous time block is still active");
        SP_ASSERT(name != nullptr);

        // allowed timing ?
        {
            // cpu
            Profiler::TimeBlockStart(name, TimeBlockType::Cpu, this);

            // gpu
            if (Profiler::IsGpuTimingEnabled() && gpu_timing)
            {
               
                Profiler::TimeBlockStart(name, TimeBlockType::Gpu, this);
            }
        }

        // allowed marking ?
        if (Profiler::IsGpuMarkingEnabled() && gpu_marker)
        {
            RHI_Device::MarkerBegin(this, name, Vector4::Zero);
        }

        m_timeblock_active = name;
    }

    void RHI_CommandList::EndTimeblock()
    {
        SP_ASSERT_MSG(m_timeblock_active != nullptr, "A time block wasn't started");

        // allowed markers ?
        if (Profiler::IsGpuTimingEnabled())
        {
            RHI_Device::MarkerEnd(this);
        }

        // allowed timing
        {
            if (Profiler::IsGpuTimingEnabled())
            {
                Profiler::TimeBlockEnd(); // gpu
            }

            Profiler::TimeBlockEnd(); // cpu

        }

        m_timeblock_active = nullptr;
    }

    void RHI_CommandList::InsertBarrierTexture(void* image, const uint32_t aspect_mask,
        const uint32_t mip_index, const uint32_t mip_range, const uint32_t array_length,
        const RHI_Image_Layout layout_old, const RHI_Image_Layout layout_new, const bool is_depth
    )
    {
        SP_ASSERT(m_state == RHI_CommandListState::Recording);
        SP_ASSERT(image != nullptr);
        RenderPassEnd(); // you can't have a barrier inside a render pass

        VkImageMemoryBarrier2 image_barrier           = {};
        image_barrier.sType                           = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2_KHR;
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
        image_barrier.srcAccessMask                   = layout_to_access_mask(image_barrier.oldLayout, false, is_depth); // operations that must complete before the barrier
        image_barrier.srcStageMask                    = access_mask_to_pipeline_stage_mask(image_barrier.srcAccessMask); // stage at which the barrier applies, on the source side
        image_barrier.dstAccessMask                   = layout_to_access_mask(image_barrier.newLayout, true, is_depth);  // operations that must wait for the barrier, on the new layout
        image_barrier.dstStageMask                    = access_mask_to_pipeline_stage_mask(image_barrier.dstAccessMask); // stage at which the barrier applies, on the destination side

        VkDependencyInfo dependency_info        = {};
        dependency_info.sType                   = VK_STRUCTURE_TYPE_DEPENDENCY_INFO_KHR;
        dependency_info.imageMemoryBarrierCount = 1;
        dependency_info.pImageMemoryBarriers    = &image_barrier;

        vkCmdPipelineBarrier2(static_cast<VkCommandBuffer>(m_rhi_resource), &dependency_info);
        Profiler::m_rhi_pipeline_barriers++;
    }

    void RHI_CommandList::InsertBarrierTexture(RHI_Texture* texture, const uint32_t mip_start, const uint32_t mip_range, const uint32_t array_length, const RHI_Image_Layout layout_old, const RHI_Image_Layout layout_new)
    {
        SP_ASSERT(texture != nullptr);
        InsertBarrierTexture(texture->GetRhiResource(), get_aspect_mask(texture), mip_start, mip_range, array_length, layout_old, layout_new, texture->IsDsv());
    }

    void RHI_CommandList::InsertBarrierTextureReadWrite(RHI_Texture* texture)
    {
        SP_ASSERT(texture != nullptr);
        InsertBarrierTexture(texture->GetRhiResource(), get_aspect_mask(texture), 0, 1, 1, texture->GetLayout(0), texture->GetLayout(0), texture->IsDsv());
    }

    void RHI_CommandList::PreDraw()
    {
        if (!m_render_pass_active)
        {
            RenderPassBegin();
        }

        if (descriptor_sets::bind_dynamic)
        {
            descriptor_sets::set_dynamic(m_pso, m_rhi_resource, m_pipeline->GetResource_PipelineLayout(), m_descriptor_layout_current);
        }
    }
}
