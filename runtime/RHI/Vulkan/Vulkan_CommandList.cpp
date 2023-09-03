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

//= INCLUDES ==========================
#include "pch.h"
#include "../RHI_Device.h"
#include "../RHI_Implementation.h"
#include "../RHI_CommandList.h"
#include "../RHI_Pipeline.h"
#include "../RHI_VertexBuffer.h"
#include "../RHI_IndexBuffer.h"
#include "../RHI_ConstantBuffer.h"
#include "../RHI_StructuredBuffer.h"
#include "../RHI_Sampler.h"
#include "../RHI_DescriptorSet.h"
#include "../RHI_DescriptorSetLayout.h"
#include "../RHI_Semaphore.h"
#include "../RHI_Fence.h"
#include "../RHI_SwapChain.h"
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

        VkPipelineStageFlags layout_to_access_mask(const VkImageLayout layout, const bool is_destination_mask)
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
                access_mask = VK_ACCESS_2_NONE;
                break;

                // transfer
            case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:
                access_mask = VK_ACCESS_TRANSFER_READ_BIT;
                break;

            case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
                access_mask = VK_ACCESS_TRANSFER_WRITE_BIT;
                break;

                // color attachments
            case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:
                access_mask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
                break;

                // depth attachments
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

                // shader reads
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
                SP_LOG_ERROR("Unexpected image layout");
                break;
            }

            return access_mask;
        }

        VkPipelineStageFlags access_flags_to_pipeline_stage(VkAccessFlags access_flags)
        {
            VkPipelineStageFlags stages = 0;
            uint32_t enabled_graphics_stages = RHI_Device::GetEnabledGraphicsStages();

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

                    // shader
                case VK_ACCESS_SHADER_READ_BIT:
                    stages |= enabled_graphics_stages | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
                    break;

                case VK_ACCESS_SHADER_WRITE_BIT:
                    stages |= enabled_graphics_stages | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
                    break;

                    // color attachments
                case VK_ACCESS_COLOR_ATTACHMENT_READ_BIT:
                    stages |= VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
                    break;

                case VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT:
                    stages |= VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
                    break;

                    // depth-stencil attachments
                case VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT:
                    stages |= VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
                    break;

                case VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT:
                    stages |= VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
                    break;

                    // transfer
                case VK_ACCESS_TRANSFER_READ_BIT:
                    stages |= VK_PIPELINE_STAGE_TRANSFER_BIT;
                    break;

                case VK_ACCESS_TRANSFER_WRITE_BIT:
                    stages |= VK_PIPELINE_STAGE_TRANSFER_BIT;
                    break;

                    // host
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

    RHI_CommandList::RHI_CommandList(const RHI_Queue_Type queue_type, const uint32_t swapchain_id, void* cmd_pool, const char* name) : SP_Object()
    {
        m_queue_type  = queue_type;
        m_object_name = name;

        // Command buffer
        {
            VkCommandBufferAllocateInfo allocate_info = {};
            allocate_info.sType                       = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
            allocate_info.commandPool                 = static_cast<VkCommandPool>(cmd_pool);
            allocate_info.level                       = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
            allocate_info.commandBufferCount          = 1;

            // Allocate
            SP_VK_ASSERT_MSG(vkAllocateCommandBuffers(RHI_Context::device, &allocate_info, reinterpret_cast<VkCommandBuffer*>(&m_rhi_resource)),
                "Failed to allocate command buffer");

            // Name
            RHI_Device::SetResourceName(static_cast<void*>(m_rhi_resource), RHI_Resource_Type::CommandList, name);
        }

        // Query pool
        if (RHI_Context::gpu_profiling)
        {
            VkQueryPoolCreateInfo query_pool_create_info = {};
            query_pool_create_info.sType                 = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
            query_pool_create_info.queryType             = VK_QUERY_TYPE_TIMESTAMP;
            query_pool_create_info.queryCount            = m_max_timestamps;

            auto query_pool = reinterpret_cast<VkQueryPool*>(&m_rhi_query_pool);
            SP_VK_ASSERT_MSG(vkCreateQueryPool(RHI_Context::device, &query_pool_create_info, nullptr, query_pool),
                "Failed to created query pool");

            m_timestamps.fill(0);
        }

        // Sync objects
        m_proccessed_fence = make_shared<RHI_Fence>(name);

        // Semaphore
        bool presents_to_swapchain = swapchain_id != 0;
        if (presents_to_swapchain)
        {
            m_proccessed_semaphore = make_shared<RHI_Semaphore>(false, name);
        }
    }

    RHI_CommandList::~RHI_CommandList()
    {
        if (m_rhi_query_pool)
        {
            RHI_Device::DeletionQueueAdd(RHI_Resource_Type::QueryPool, m_rhi_query_pool);
            m_rhi_query_pool = nullptr;
        }
    }

    void RHI_CommandList::Begin()
    {
        SP_ASSERT(m_state == RHI_CommandListState::Idle);

        // Get queries
        if (m_queue_type != RHI_Queue_Type::Copy)
        {
            if (RHI_Context::gpu_profiling)
            {
                if (m_rhi_query_pool)
                {
                    if (m_timestamp_index != 0)
                    {
                        const uint32_t query_count     = m_timestamp_index * 2;
                        const size_t stride            = sizeof(uint64_t);
                        const VkQueryResultFlags flags = VK_QUERY_RESULT_64_BIT;

                        vkGetQueryPoolResults(
                            RHI_Context::device,                    // device
                            static_cast<VkQueryPool>(m_rhi_query_pool), // queryPool
                            0,                                      // firstQuery
                            query_count,                            // queryCount
                            query_count * stride,                   // dataSize
                            m_timestamps.data(),                    // pData
                            stride,                                 // stride
                            flags                                   // flags
                        );
                    }
                }
            }

            m_timestamp_index = 0;
        }

        // Begin command buffer
        VkCommandBufferBeginInfo begin_info = {};
        begin_info.sType                    = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        begin_info.flags                    = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        SP_ASSERT_MSG(vkBeginCommandBuffer(static_cast<VkCommandBuffer>(m_rhi_resource), &begin_info) == VK_SUCCESS, "Failed to begin command buffer");

        // Reset query pool - Has to be done after vkBeginCommandBuffer or a VK_DEVICE_LOST will occur
        if (m_queue_type != RHI_Queue_Type::Copy)
        {
            vkCmdResetQueryPool(static_cast<VkCommandBuffer>(m_rhi_resource), static_cast<VkQueryPool>(m_rhi_query_pool), 0, m_max_timestamps);
        }

        // Update states
        m_state          = RHI_CommandListState::Recording;
        m_pipeline_dirty = true;
    }

    void RHI_CommandList::End()
    {
        SP_ASSERT(m_state == RHI_CommandListState::Recording);

        SP_ASSERT_MSG(
            vkEndCommandBuffer(static_cast<VkCommandBuffer>(m_rhi_resource)) == VK_SUCCESS,
            "Failed to end command buffer"
        );

        m_state = RHI_CommandListState::Ended;
    }

    void RHI_CommandList::Submit()
    {
        SP_ASSERT(m_state == RHI_CommandListState::Ended);

        RHI_Device::QueueSubmit(
            m_queue_type,                                  // queue
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, // wait flags
            static_cast<VkCommandBuffer>(m_rhi_resource),  // cmd buffer
            nullptr,                                       // wait semaphore
            m_proccessed_semaphore.get(),                  // signal semaphore
            m_proccessed_fence.get()                       // signal fence
        );

        m_state = RHI_CommandListState::Submitted;
    }

    void RHI_CommandList::SetPipelineState(RHI_PipelineState& pso)
    {
        SP_ASSERT(m_state == RHI_CommandListState::Recording);

        // get (or create) a pipeline which matches the requested pipeline state
        RHI_Device::GetOrCreatePipeline(pso, m_pipeline, m_descriptor_layout_current);

        uint64_t hash_previous = m_pso.GetHash();
        m_pso                  = pso;

        // Determine if the pipeline is dirty
        if (!m_pipeline_dirty)
        {
            m_pipeline_dirty = hash_previous != m_pso.GetHash();
        }

        // Bind pipeline
        if (m_pipeline_dirty)
        {
            // Get vulkan pipeline object
            SP_ASSERT(m_pipeline != nullptr);
            VkPipeline vk_pipeline = static_cast<VkPipeline>(m_pipeline->GetResource_Pipeline());
            SP_ASSERT(vk_pipeline != nullptr);

            // Bind
            VkPipelineBindPoint pipeline_bind_point = m_pso.IsCompute() ? VK_PIPELINE_BIND_POINT_COMPUTE : VK_PIPELINE_BIND_POINT_GRAPHICS;
            vkCmdBindPipeline(static_cast<VkCommandBuffer>(m_rhi_resource), pipeline_bind_point, vk_pipeline);

            // Profile
            Profiler::m_rhi_bindings_pipeline++;

            m_pipeline_dirty = false;

            // Also, If the pipeline changed, resources have to be set again
            m_vertex_buffer_id = 0;
            m_index_buffer_id  = 0;
        }
    }

    void RHI_CommandList::BeginRenderPass()
    {
        SP_ASSERT(m_state == RHI_CommandListState::Recording);
        SP_ASSERT_MSG(m_pso.IsGraphics(), "You can't use a render pass with a compute pipeline");
        SP_ASSERT_MSG(!m_is_rendering, "The command list is already rendering");

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

        // Color attachments
        vector<VkRenderingAttachmentInfo> attachments_color;
        {
            // Swapchain buffer as a render target
            RHI_SwapChain* swapchain = m_pso.render_target_swapchain;
            if (swapchain)
            {
                // Transition to the appropriate layout
                if (swapchain->GetLayout() != RHI_Image_Layout::Color_Attachment_Optimal)
                {
                    swapchain->SetLayout(RHI_Image_Layout::Color_Attachment_Optimal, this);
                }

                VkRenderingAttachmentInfo color_attachment = {};
                color_attachment.sType                     = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO_KHR;
                color_attachment.imageView                 = static_cast<VkImageView>(swapchain->GetRhiRtv());
                color_attachment.imageLayout               = vulkan_image_layout[static_cast<uint8_t>(swapchain->GetLayout())];
                color_attachment.loadOp                    = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
                color_attachment.storeOp                   = VK_ATTACHMENT_STORE_OP_STORE;

                SP_ASSERT(color_attachment.imageView != nullptr);

                attachments_color.push_back(color_attachment);
            }
            else // Regular render target(s)
            { 
                for (uint32_t i = 0; i < rhi_max_render_target_count; i++)
                {
                    RHI_Texture* rt = m_pso.render_target_color_textures[i];

                    if (rt == nullptr)
                        break;

                    SP_ASSERT_MSG(rt->IsRenderTargetColor(), "The texture wasn't created with the RHI_Texture_RenderTarget flag and/or isn't a color format");

                    // Transition to the appropriate layout
                    if (rt->GetLayout(0) != RHI_Image_Layout::Color_Attachment_Optimal)
                    {
                        rt->SetLayout(RHI_Image_Layout::Color_Attachment_Optimal, this);
                    }

                    VkRenderingAttachmentInfo color_attachment = {};
                    color_attachment.sType                     = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO_KHR;
                    color_attachment.imageView                 = static_cast<VkImageView>(rt->GetRhiRtv(m_pso.render_target_color_texture_array_index));
                    color_attachment.imageLayout               = vulkan_image_layout[static_cast<uint8_t>(rt->GetLayout(0))];
                    color_attachment.loadOp                    = get_color_load_op(m_pso.clear_color[i]);
                    color_attachment.storeOp                   = VK_ATTACHMENT_STORE_OP_STORE;
                    color_attachment.clearValue.color          = { m_pso.clear_color[i].r, m_pso.clear_color[i].g, m_pso.clear_color[i].b, m_pso.clear_color[i].a };

                    SP_ASSERT(color_attachment.imageView != nullptr);

                    attachments_color.push_back(color_attachment);
                }
            }
            rendering_info.colorAttachmentCount = static_cast<uint32_t>(attachments_color.size());
            rendering_info.pColorAttachments    = attachments_color.data();
        }

        // Depth-stencil attachment
        VkRenderingAttachmentInfoKHR attachment_depth_stencil = {};
        if (m_pso.render_target_depth_texture != nullptr)
        {
            RHI_Texture* rt = m_pso.render_target_depth_texture;

            SP_ASSERT_MSG(rt->GetWidth() == rendering_info.renderArea.extent.width, "The depth buffer doesn't match the output resolution");
            SP_ASSERT(rt->IsRenderTargetDepthStencil());

            // Transition to the appropriate layout
            RHI_Image_Layout layout = rt->IsStencilFormat() ? RHI_Image_Layout::Depth_Stencil_Attachment_Optimal : RHI_Image_Layout::Depth_Attachment_Optimal;
            if (m_pso.render_target_depth_texture_read_only)
            {
                layout = RHI_Image_Layout::Depth_Stencil_Read_Only_Optimal;
            }
            rt->SetLayout(layout, this);

            attachment_depth_stencil.sType                           = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO_KHR;
            attachment_depth_stencil.imageView                       = static_cast<VkImageView>(rt->GetRhiDsv(m_pso.render_target_depth_stencil_texture_array_index));
            attachment_depth_stencil.imageLayout                     = vulkan_image_layout[static_cast<uint8_t>(rt->GetLayout(0))];
            attachment_depth_stencil.loadOp                          = get_depth_load_op(m_pso.clear_depth);
            attachment_depth_stencil.storeOp                         = VK_ATTACHMENT_STORE_OP_STORE;
            attachment_depth_stencil.clearValue.depthStencil.depth   = m_pso.clear_depth;
            attachment_depth_stencil.clearValue.depthStencil.stencil = m_pso.clear_stencil;

            rendering_info.pDepthAttachment = &attachment_depth_stencil;

            // We are using the combined depth-stencil approach.
            // This means we can assign the depth attachment as the stencil attachment.
            if (m_pso.render_target_depth_texture->IsStencilFormat())
            {
                rendering_info.pStencilAttachment = rendering_info.pDepthAttachment;
            }
        }

        // Begin dynamic render pass instance
        vkCmdBeginRendering(static_cast<VkCommandBuffer>(m_rhi_resource), &rendering_info);

        // Set viewport
        RHI_Viewport viewport = RHI_Viewport(
            0.0f, 0.0f,
            static_cast<float>(m_pso.GetWidth()),
            static_cast<float>(m_pso.GetHeight())
        );
        SetViewport(viewport);

        m_is_rendering = true;
    }

    void RHI_CommandList::EndRenderPass()
    {
        if (m_is_rendering)
        {
            vkCmdEndRendering(static_cast<VkCommandBuffer>(m_rhi_resource));
            m_is_rendering = false;
        }

        if (m_pso.render_target_swapchain)
        {
            m_pso.render_target_swapchain->SetLayout(RHI_Image_Layout::Present_Src, this);
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

    void RHI_CommandList::ClearRenderTarget(RHI_Texture* texture,
        const uint32_t color_index          /*= 0*/,
        const uint32_t depth_stencil_index  /*= 0*/,
        const bool storage                  /*= false*/,
        const Color& clear_color            /*= rhi_color_load*/,
        const float clear_depth             /*= rhi_depth_load*/,
        const uint32_t clear_stencil        /*= rhi_stencil_load*/
    )
    {
        SP_ASSERT(m_state == RHI_CommandListState::Recording);
        SP_ASSERT_MSG((texture->GetFlags() & RHI_Texture_ClearOrBlit) != 0, "The texture needs the RHI_Texture_ClearOrBlit flag");

        if (!texture || !texture->GetRhiSrv())
        {
            SP_LOG_ERROR("Texture is null.");
            return;
        }

        // One of the required layouts for clear functions
        texture->SetLayout(RHI_Image_Layout::Transfer_Dst_Optimal, this);

        VkImageSubresourceRange image_subresource_range = {};
        image_subresource_range.baseMipLevel            = 0;
        image_subresource_range.levelCount              = 1;
        image_subresource_range.baseArrayLayer          = 0;
        image_subresource_range.layerCount              = 1;

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

            vkCmdClearDepthStencilImage(static_cast<VkCommandBuffer>(m_rhi_resource), static_cast<VkImage>(texture->GetRhiResource()), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &clear_depth_stencil, 1, &image_subresource_range);
        }
    }

    void RHI_CommandList::Draw(const uint32_t vertex_count, uint32_t vertex_start_index /*= 0*/)
    {
        SP_ASSERT(m_state == RHI_CommandListState::Recording);

        // Ensure correct state before attempting to draw
        OnDraw();

        // Draw
        vkCmdDraw(
            static_cast<VkCommandBuffer>(m_rhi_resource), // commandBuffer
            vertex_count,                                 // vertexCount
            1,                                            // instanceCount
            vertex_start_index,                           // firstVertex
            0                                             // firstInstance
        );

        if (Profiler::m_granularity == ProfilerGranularity::Full)
        {
            Profiler::m_rhi_draw++;
        }
    }

    void RHI_CommandList::DrawIndexed(const uint32_t index_count, const uint32_t index_offset, const uint32_t vertex_offset)
    {
        SP_ASSERT(m_state == RHI_CommandListState::Recording);

        // Ensure correct state before attempting to draw
        OnDraw();

        // Draw
        vkCmdDrawIndexed(
            static_cast<VkCommandBuffer>(m_rhi_resource), // commandBuffer
            index_count,                                  // indexCount
            1,                                            // instanceCount
            index_offset,                                 // firstIndex
            vertex_offset,                                // vertexOffset
            0                                             // firstInstance
        );

        // Profile
        if (Profiler::m_granularity == ProfilerGranularity::Full)
        {
            Profiler::m_rhi_draw++;
        }
    }

    void RHI_CommandList::Dispatch(uint32_t x, uint32_t y, uint32_t z /*= 1*/, bool async /*= false*/)
    {
        SP_ASSERT(m_state == RHI_CommandListState::Recording);

        // Ensure correct state before attempting to dispatch
        OnDraw();

        // Dispatch
        vkCmdDispatch(static_cast<VkCommandBuffer>(m_rhi_resource), x, y, z);

        Profiler::m_rhi_dispatch++;
    }

    void RHI_CommandList::Blit(RHI_Texture* source, RHI_Texture* destination, const bool blit_mips)
    {
        SP_ASSERT_MSG((source->GetFlags() & RHI_Texture_ClearOrBlit) != 0,      "The texture needs the RHI_Texture_ClearOrBlit flag");
        SP_ASSERT_MSG((destination->GetFlags() & RHI_Texture_ClearOrBlit) != 0, "The texture needs the RHI_Texture_ClearOrBlit flag");
        if (blit_mips)
        {
            SP_ASSERT_MSG(source->GetMipCount() == destination->GetMipCount(),
                "If the mips are blitted, then the mip count between the source and the destination textures must match");
        }

        // Compute a blit region for each mip
        array<VkOffset3D,  rhi_max_mip_count> blit_offsets_source     = {};
        array<VkOffset3D, rhi_max_mip_count> blit_offsets_destination = {};
        array<VkImageBlit, rhi_max_mip_count> blit_regions            = {};
        uint32_t blit_region_count                                    = blit_mips ? source->GetMipCount() : 1;
        for (uint32_t mip_index = 0; mip_index < blit_region_count; mip_index++)
        {
            VkOffset3D& source_blit_size = blit_offsets_source[mip_index];
            source_blit_size.x           = source->GetWidth()  >> mip_index;
            source_blit_size.y           = source->GetHeight() >> mip_index;
            source_blit_size.z           = 1;

            VkOffset3D& destination_blit_size = blit_offsets_destination[mip_index];
            destination_blit_size.x           = destination->GetWidth()  >> mip_index;
            destination_blit_size.y           = destination->GetHeight() >> mip_index;
            destination_blit_size.z           = 1;

            SP_ASSERT_MSG(source_blit_size.x <= destination_blit_size.x && source_blit_size.y <= destination_blit_size.y,
                "The source texture dimension(s) are larger than the those of the destination texture");

            VkImageBlit& blit_region                  = blit_regions[mip_index];
            blit_region.srcSubresource.mipLevel       = mip_index;
            blit_region.srcSubresource.baseArrayLayer = 0;
            blit_region.srcSubresource.layerCount     = 1;
            blit_region.srcSubresource.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
            blit_region.srcOffsets[0]                 = { 0, 0, 0 };
            blit_region.srcOffsets[1]                 = source_blit_size;
            blit_region.dstSubresource.mipLevel       = mip_index;
            blit_region.dstSubresource.baseArrayLayer = 0;
            blit_region.dstSubresource.layerCount     = 1;
            blit_region.dstSubresource.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
            blit_region.dstOffsets[0]                 = { 0, 0, 0 };
            blit_region.dstOffsets[1]                 = destination_blit_size;
        }

        // Save the initial layouts
        array<RHI_Image_Layout, rhi_max_mip_count> layouts_initial_source      = source->GetLayouts();
        array<RHI_Image_Layout, rhi_max_mip_count> layouts_initial_destination = destination->GetLayouts();

        // Transition to blit appropriate layouts
        source->SetLayout(RHI_Image_Layout::Transfer_Src_Optimal,      this);
        destination->SetLayout(RHI_Image_Layout::Transfer_Dst_Optimal, this);

        // Blit
        vkCmdBlitImage(
            static_cast<VkCommandBuffer>(m_rhi_resource),
            static_cast<VkImage>(source->GetRhiResource()),      VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            static_cast<VkImage>(destination->GetRhiResource()), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            blit_region_count, &blit_regions[0],
            vulkan_filter[static_cast<uint32_t>(RHI_Filter::Linear)]
        );

        // Transition to the initial layouts
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
        SP_ASSERT_MSG((source->GetFlags() & RHI_Texture_ClearOrBlit) != 0, "The texture needs the RHI_Texture_ClearOrBlit flag");
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
        blit_region.srcSubresource.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
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
        source->SetLayout(RHI_Image_Layout::Transfer_Src_Optimal,      this);
        destination->SetLayout(RHI_Image_Layout::Transfer_Dst_Optimal, this);

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

        // Transition to the initial layouts
        source->SetLayout(source_layout_initial, this);
        destination->SetLayout(RHI_Image_Layout::Present_Src, this);
    }

    void RHI_CommandList::Copy(RHI_Texture* source, RHI_Texture* destination, const bool blit_mips)
    {
        SP_ASSERT_MSG((source->GetFlags() & RHI_Texture_ClearOrBlit) != 0, "The texture needs the RHI_Texture_ClearOrBlit flag");
        SP_ASSERT_MSG((destination->GetFlags() & RHI_Texture_ClearOrBlit) != 0, "The texture needs the RHI_Texture_ClearOrBlit flag");
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
        source->SetLayout(RHI_Image_Layout::Transfer_Src_Optimal, this);
        destination->SetLayout(RHI_Image_Layout::Transfer_Dst_Optimal, this);

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
        SP_ASSERT_MSG((source->GetFlags() & RHI_Texture_ClearOrBlit) != 0, "The texture needs the RHI_Texture_ClearOrBlit flag");
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
        source->SetLayout(RHI_Image_Layout::Transfer_Src_Optimal, this);
        destination->SetLayout(RHI_Image_Layout::Transfer_Dst_Optimal, this);

        // Blit
        vkCmdCopyImage(
            static_cast<VkCommandBuffer>(m_rhi_resource),
            static_cast<VkImage>(source->GetRhiResource()), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            static_cast<VkImage>(destination->GetRhiRt()),  VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            1, &copy_region
        );

        // Transition to the initial layout
        source->SetLayout(layout_initial_source, this);
        destination->SetLayout(RHI_Image_Layout::Present_Src, this);
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

    void RHI_CommandList::SetBufferVertex(const RHI_VertexBuffer* buffer)
    {
        SP_ASSERT(m_state == RHI_CommandListState::Recording);

        // Skip if already set
        if (m_vertex_buffer_id == buffer->GetObjectId())
            return;

        VkBuffer vertex_buffers[] = { static_cast<VkBuffer>(buffer->GetRhiResource()) };
        VkDeviceSize offsets[]    = { 0 };

        vkCmdBindVertexBuffers(
            static_cast<VkCommandBuffer>(m_rhi_resource), // commandBuffer
            0,                                            // firstBinding
            1,                                            // bindingCount
            vertex_buffers,                               // pBuffers
            offsets                                       // pOffsets
        );

        m_vertex_buffer_id = buffer->GetObjectId();

        Profiler::m_rhi_bindings_buffer_vertex++;
    }

    void RHI_CommandList::SetBufferIndex(const RHI_IndexBuffer* buffer)
    {
        SP_ASSERT(m_state == RHI_CommandListState::Recording);

        if (m_index_buffer_id == buffer->GetObjectId())
            return;

        vkCmdBindIndexBuffer(
            static_cast<VkCommandBuffer>(m_rhi_resource),                   // commandBuffer
            static_cast<VkBuffer>(buffer->GetRhiResource()),                // buffer
            0,                                                              // offset
            buffer->Is16Bit() ? VK_INDEX_TYPE_UINT16 : VK_INDEX_TYPE_UINT32 // indexType
        );

        m_index_buffer_id = buffer->GetObjectId();

        Profiler::m_rhi_bindings_buffer_index++;
    }

    void RHI_CommandList::SetConstantBuffer(const uint32_t slot, RHI_ConstantBuffer* constant_buffer) const
    {
        SP_ASSERT(m_state == RHI_CommandListState::Recording);

        if (!m_descriptor_layout_current)
        {
            SP_LOG_WARNING("Descriptor layout not set, try setting constant buffer \"%s\" within a render pass", constant_buffer->GetObjectName().c_str());
            return;
        }

        // Set (will only happen if it's not already set)
        m_descriptor_layout_current->SetConstantBuffer(slot, constant_buffer);
    }

    void RHI_CommandList::PushConstants(const uint32_t offset, const uint32_t size, const void* data)
    {
        SP_ASSERT(m_state == RHI_CommandListState::Recording);
        SP_ASSERT(size <= RHI_Device::PropertyGetMaxPushConstantSize());

        uint32_t stages = 0;
        if (m_pso.IsCompute())
        {
            stages = VkShaderStageFlagBits::VK_SHADER_STAGE_COMPUTE_BIT;
        }
        else if (m_pso.IsGraphics())
        {
            stages = VkShaderStageFlagBits::VK_SHADER_STAGE_VERTEX_BIT;

            if (m_pso.shader_pixel != nullptr)
            {
                stages |= VkShaderStageFlagBits::VK_SHADER_STAGE_FRAGMENT_BIT;
            }
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

        // Get some texture info
        const uint32_t mip_count        = texture->GetMipCount();
        const bool mip_specified        = mip_index != rhi_all_mips;
        const uint32_t mip_start        = mip_specified ? mip_index : 0;
        RHI_Image_Layout current_layout = texture->GetLayout(mip_start);

        SP_ASSERT_MSG(texture->GetRhiSrv() != nullptr, "The texture has no srv"); // Vulkan only has SRVs
        SP_ASSERT_MSG(current_layout != RHI_Image_Layout::Undefined && current_layout != RHI_Image_Layout::Preinitialized, "Invalid layout");

        // Transition to appropriate layout (if needed)
        {
            RHI_Image_Layout target_layout = RHI_Image_Layout::Undefined;

            if (uav)
            {
                SP_ASSERT(texture->IsUav());
                
                // According to section 13.1 of the Vulkan spec, storage textures have to be in a general layout.
                // https://www.khronos.org/registry/vulkan/specs/1.1-extensions/html/vkspec.html#descriptorsets-storageimage
                target_layout = RHI_Image_Layout::General;
            }
            else
            {
                SP_ASSERT(texture->IsSrv());

                // Color
                if (texture->IsColorFormat())
                {
                    target_layout = RHI_Image_Layout::Shader_Read_Only_Optimal;
                }

                // Depth
                if (texture->IsDepthFormat())
                {
                    target_layout = RHI_Image_Layout::Depth_Stencil_Read_Only_Optimal;
                }
            }

            // Verify that an appropriate layout has been deduced
            SP_ASSERT(target_layout != RHI_Image_Layout::Undefined);

            // Determine if a layout transition is needed
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

            // Transition
            if (transition_required)
            {
                SP_ASSERT(!m_is_rendering && "Can't transition to a different layout while rendering");
                texture->SetLayout(target_layout, this, mip_index, mip_range);
            }
        }

        // Set (will only happen if it's not already set)
        m_descriptor_layout_current->SetTexture(slot, texture, mip_index, mip_range);
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
    }

    void RHI_CommandList::BeginMarker(const char* name)
    {
        if (RHI_Context::gpu_markers)
        {
            RHI_Device::MarkerBegin(this, name, Vector4::Zero);
        }
    }

    void RHI_CommandList::EndMarker()
    {
        if (RHI_Context::gpu_markers)
        {
            RHI_Device::MarkerEnd(this);
        }
    }
    
    uint32_t RHI_CommandList::BeginTimestamp()
    {
        SP_ASSERT(m_state == RHI_CommandListState::Recording);
        SP_ASSERT(RHI_Context::gpu_profiling);
        SP_ASSERT(m_rhi_query_pool != nullptr);

        uint32_t timestamp_index = m_timestamp_index;
        vkCmdWriteTimestamp(static_cast<VkCommandBuffer>(m_rhi_resource), VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, static_cast<VkQueryPool>(m_rhi_query_pool), timestamp_index);
        m_timestamp_index++;

        return timestamp_index;
    }

    void RHI_CommandList::EndTimestamp()
    {
        SP_ASSERT(m_state == RHI_CommandListState::Recording);
        SP_ASSERT(RHI_Context::gpu_profiling);
        SP_ASSERT(m_rhi_query_pool != nullptr);

        vkCmdWriteTimestamp(static_cast<VkCommandBuffer>(m_rhi_resource), VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, static_cast<VkQueryPool>(m_rhi_query_pool), m_timestamp_index++);
    }

    float RHI_CommandList::GetTimestampDuration(const uint32_t timestamp_index)
    {
        if (timestamp_index + 1 >= m_timestamps.size())
        {
            SP_LOG_ERROR("Pass index out of timestamp array range");
            return 0.0f;
        }

        uint64_t start = m_timestamps[timestamp_index];
        uint64_t end   = m_timestamps[timestamp_index + 1];

        // If end has not been acquired yet (zero), early exit
        if (end < start)
            return 0.0f;

        uint64_t duration = Math::Helper::Clamp<uint64_t>(end - start, 0, numeric_limits<uint64_t>::max());
        float duration_ms = static_cast<float>(duration * RHI_Device::PropertyGetTimestampPeriod() * 1e-6f);

        return duration_ms;
    }

    void RHI_CommandList::BeginTimeblock(const char* name, const bool gpu_marker, const bool gpu_timing)
    {
        SP_ASSERT_MSG(m_timeblock_active == nullptr, "The previous time block is still active");
        SP_ASSERT(name != nullptr);

        // Allowed profiler ?
        if (RHI_Context::gpu_profiling && gpu_timing)
        {
            Profiler::TimeBlockStart(name, TimeBlockType::Cpu, this);
            Profiler::TimeBlockStart(name, TimeBlockType::Gpu, this);
        }

        // Allowed to markers ?
        if (RHI_Context::gpu_markers && gpu_marker)
        {
            RHI_Device::MarkerBegin(this, name, Vector4::Zero);
        }

        m_timeblock_active = name;
    }

    void RHI_CommandList::EndTimeblock()
    {
        SP_ASSERT_MSG(m_timeblock_active != nullptr, "A time block wasn't started");

        // Allowed markers ?
        if (RHI_Context::gpu_markers)
        {
            RHI_Device::MarkerEnd(this);
        }

        // Allowed profiler ?
        if (RHI_Context::gpu_profiling)
        {
            Profiler::TimeBlockEnd(); // cpu
            Profiler::TimeBlockEnd(); // gpu
        }

        m_timeblock_active = nullptr;
    }

    void RHI_CommandList::OnDraw()
    {
        SP_ASSERT(m_state == RHI_CommandListState::Recording);

        Renderer::SetGlobalShaderResources(this);

        // bind descriptor sets - If the descriptor set is null, it means we don't need to bind anything.
        if (RHI_DescriptorSet* descriptor_set = m_descriptor_layout_current->GetDescriptorSet())
        {
            // get descriptor sets
            array<void*, 3> descriptor_sets =
            {
                descriptor_set->GetResource(),
                RHI_Device::GetDescriptorSet(RHI_Device_Resource::sampler_comparison),
                RHI_Device::GetDescriptorSet(RHI_Device_Resource::sampler_regular)
            };

            // get dynamic offsets
            vector<uint32_t> dynamic_offsets;
            m_descriptor_layout_current->GetDynamicOffsets(&dynamic_offsets);

            VkPipelineBindPoint bind_point = m_pso.IsCompute() ?
                VkPipelineBindPoint::VK_PIPELINE_BIND_POINT_COMPUTE :
                VkPipelineBindPoint::VK_PIPELINE_BIND_POINT_GRAPHICS;

            // bind descriptor set
            vkCmdBindDescriptorSets
            (
                static_cast<VkCommandBuffer>(m_rhi_resource),                            // commandBuffer
                bind_point,                                                              // pipelineBindPoint
                static_cast<VkPipelineLayout>(m_pipeline->GetResource_PipelineLayout()), // layout
                0,                                                                       // firstSet
                static_cast<uint32_t>(descriptor_sets.size()),                           // descriptorSetCount
                reinterpret_cast<VkDescriptorSet*>(descriptor_sets.data()),              // pDescriptorSets
                static_cast<uint32_t>(dynamic_offsets.size()),                           // dynamicOffsetCount
                dynamic_offsets.data()                                                   // pDynamicOffsets
            );

            Profiler::m_rhi_bindings_descriptor_set++;
        }
    }

    void RHI_CommandList::InsertMemoryBarrierImage(void* image, const uint32_t aspect_mask,
        const uint32_t mip_index, const uint32_t mip_range, const uint32_t array_length,
        const RHI_Image_Layout layout_old, const RHI_Image_Layout layout_new
    )
    {
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
            static_cast<VkCommandBuffer>(m_rhi_resource), // commandBuffer
            source_stage_mask,                            // srcStageMask
            destination_stage_mask,                       // dstStageMask
            0,                                            // dependencyFlags
            0,                                            // memoryBarrierCount
            nullptr,                                      // pMemoryBarriers
            0,                                            // bufferMemoryBarrierCount
            nullptr,                                      // pBufferMemoryBarriers
            1,                                            // imageMemoryBarrierCount
            &image_barrier                                // pImageMemoryBarriers
        );

        Profiler::m_rhi_pipeline_barriers++;
    }

    void RHI_CommandList::InsertMemoryBarrierImage(RHI_Texture* texture, const uint32_t mip_start, const uint32_t mip_range, const uint32_t array_length, const RHI_Image_Layout layout_old, const RHI_Image_Layout layout_new)
    {
        SP_ASSERT(texture != nullptr);
        InsertMemoryBarrierImage(texture->GetRhiResource(), get_aspect_mask(texture), mip_start, mip_range, array_length, layout_old, layout_new);
    }

    void RHI_CommandList::InsertMemoryBarrierImageWaitForWrite(RHI_Texture* texture)
    {
        SP_ASSERT(texture != nullptr);

        VkImageMemoryBarrier image_barrier            = {};
        image_barrier.sType                           = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        image_barrier.pNext                           = nullptr;
        image_barrier.oldLayout                       = vulkan_image_layout[static_cast<VkImageLayout>(texture->GetLayout(0))];
        image_barrier.newLayout                       = vulkan_image_layout[static_cast<VkImageLayout>(texture->GetLayout(0))];
        image_barrier.srcQueueFamilyIndex             = VK_QUEUE_FAMILY_IGNORED;
        image_barrier.dstQueueFamilyIndex             = VK_QUEUE_FAMILY_IGNORED;
        image_barrier.image                           = static_cast<VkImage>(texture->GetRhiResource());
        image_barrier.subresourceRange.aspectMask     = get_aspect_mask(texture);
        image_barrier.subresourceRange.baseMipLevel   = 0;
        image_barrier.subresourceRange.levelCount     = texture->GetMipCount();
        image_barrier.subresourceRange.baseArrayLayer = 0;
        image_barrier.subresourceRange.layerCount     = texture->GetArrayLength();
        image_barrier.srcAccessMask                   = VK_ACCESS_SHADER_WRITE_BIT;
        image_barrier.dstAccessMask                   = VK_ACCESS_SHADER_READ_BIT;

        VkPipelineStageFlags source_stage_mask      = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
        VkPipelineStageFlags destination_stage_mask = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;

        vkCmdPipelineBarrier
        (
            static_cast<VkCommandBuffer>(m_rhi_resource),
            source_stage_mask,
            destination_stage_mask,
            0,
            0,
            nullptr,
            0,
            nullptr,
            1,
            &image_barrier
        );

        Profiler::m_rhi_pipeline_barriers++;
    }
}
