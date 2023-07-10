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
#include "../RHI_Shader.h"
#include "../../Profiling/Profiler.h"
//=====================================

//= NAMESPACES ===============
using namespace std;
using namespace Spartan::Math;
//============================

namespace Spartan
{
    static VkAttachmentLoadOp get_color_load_op(const Color& color)
    {
        if (color == rhi_color_dont_care)
            return VK_ATTACHMENT_LOAD_OP_DONT_CARE;

        if (color == rhi_color_load)
            return VK_ATTACHMENT_LOAD_OP_LOAD;

        return VK_ATTACHMENT_LOAD_OP_CLEAR;
    };

    static VkAttachmentLoadOp get_depth_load_op(const float depth)
    {
        if (depth == rhi_depth_dont_care)
            return VK_ATTACHMENT_LOAD_OP_DONT_CARE;

        if (depth == rhi_depth_load)
            return VK_ATTACHMENT_LOAD_OP_LOAD;

        return VK_ATTACHMENT_LOAD_OP_CLEAR;
    };

    RHI_CommandList::RHI_CommandList(const RHI_Queue_Type queue_type, const uint32_t index, void* cmd_pool, const char* name) : Object()
    {
        m_queue_type = queue_type;
        m_object_name       = name;
        m_index      = index;

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

            auto query_pool = reinterpret_cast<VkQueryPool*>(&m_query_pool);
            SP_VK_ASSERT_MSG(vkCreateQueryPool(RHI_Context::device, &query_pool_create_info, nullptr, query_pool),
                "Failed to created query pool");

            m_timestamps.fill(0);
        }

        // Sync objects
        m_proccessed_fence     = make_shared<RHI_Fence>(name);
        m_proccessed_semaphore = make_shared<RHI_Semaphore>(false, name);
    }

    RHI_CommandList::~RHI_CommandList()
    {
        if (m_query_pool)
        {
            RHI_Device::AddToDeletionQueue(RHI_Resource_Type::QueryPool, m_query_pool);
            m_query_pool = nullptr;
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
                if (m_query_pool)
                {
                    if (m_timestamp_index != 0)
                    {
                        const uint32_t query_count     = m_timestamp_index * 2;
                        const size_t stride            = sizeof(uint64_t);
                        const VkQueryResultFlags flags = VK_QUERY_RESULT_64_BIT;

                        vkGetQueryPoolResults(
                            RHI_Context::device,                    // device
                            static_cast<VkQueryPool>(m_query_pool), // queryPool
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
            vkCmdResetQueryPool(static_cast<VkCommandBuffer>(m_rhi_resource), static_cast<VkQueryPool>(m_query_pool), 0, m_max_timestamps);
        }

        // Update states
        m_state          = RHI_CommandListState::Recording;
        m_pipeline_dirty = true;
    }

    void RHI_CommandList::End()
    {
        SP_ASSERT(m_state == RHI_CommandListState::Recording);

        if (swapchain_to_transition)
        {
            swapchain_to_transition->SetLayout(RHI_Image_Layout::Present_Src, this);
            swapchain_to_transition = nullptr;
        }

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
        SP_ASSERT(pso.IsValid());
        SP_ASSERT(m_state == RHI_CommandListState::Recording);

        if (pso.NeedsToUpdateHash())
        {
            pso.ComputeHash();
        }

        // Update the descriptor cache with the pipeline state
        GetDescriptorSetLayoutFromPipelineState(pso);

        // If no pipeline exists for this state, create one
        uint64_t hash_previous = m_pso.GetHash();
        uint64_t hash          = pso.GetHash();
        auto it = RHI_Device::GetPipelines().find(hash);
        if (it == RHI_Device::GetPipelines().end())
        {
            // Create a new pipeline
            it = RHI_Device::GetPipelines().emplace(make_pair(hash, move(make_shared<RHI_Pipeline>(pso, m_descriptor_layout_current)))).first;
            SP_LOG_INFO("A new pipeline has been created.");
        }

        m_pipeline = it->second.get();
        m_pso      = pso;

        // Determine if the pipeline is dirty
        if (!m_pipeline_dirty)
        {
            m_pipeline_dirty = hash_previous != hash;
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
                swapchain_to_transition = swapchain;

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
    }

    void RHI_CommandList::ClearPipelineStateRenderTargets(RHI_PipelineState& pipeline_state)
    {
        // Validate state
        SP_ASSERT(m_state == RHI_CommandListState::Recording);

        uint32_t attachment_count = 0;
        array<VkClearAttachment, rhi_max_render_target_count + 1> attachments; // +1 for depth-stencil

        for (uint8_t i = 0; i < rhi_max_render_target_count; i++)
        { 
            if (pipeline_state.clear_color[i] != rhi_color_load)
            {
                VkClearAttachment& attachment = attachments[attachment_count++];

                attachment.aspectMask                   = VK_IMAGE_ASPECT_COLOR_BIT;
                attachment.colorAttachment              = 0;
                attachment.clearValue.color.float32[0]  = pipeline_state.clear_color[i].r;
                attachment.clearValue.color.float32[1]  = pipeline_state.clear_color[i].g;
                attachment.clearValue.color.float32[2]  = pipeline_state.clear_color[i].b;
                attachment.clearValue.color.float32[3]  = pipeline_state.clear_color[i].a;
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

    void RHI_CommandList::Blit(RHI_Texture* source, RHI_Texture* destination, const RHI_Filter filter, const bool blit_mips)
    {
        // Validate transfer bits
        SP_ASSERT_MSG((source->GetFlags() & RHI_Texture_ClearOrBlit) != 0, "The texture needs the RHI_Texture_ClearOrBlit flag");
        SP_ASSERT_MSG((destination->GetFlags() & RHI_Texture_ClearOrBlit) != 0, "The texture needs the RHI_Texture_ClearOrBlit flag");

        // Compute a blit region for each mip
        array<VkOffset3D,  rhi_max_mip_count> blit_region_sizes = {};
        array<VkImageBlit, rhi_max_mip_count> blit_regions      = {};
        uint32_t blit_region_count                              = source->GetMipCount();
        for (uint32_t mip_index = 0; mip_index < blit_region_count; mip_index++)
        {
            VkOffset3D& blit_size = blit_region_sizes[mip_index];
            blit_size.x           = source->GetWidth() >> mip_index;
            blit_size.y           = source->GetHeight() >> mip_index;
            blit_size.z           = 1;

            VkImageBlit& blit_region                  = blit_regions[mip_index];
            blit_region.srcSubresource.mipLevel       = mip_index;
            blit_region.srcSubresource.baseArrayLayer = 0;
            blit_region.srcSubresource.layerCount     = 1;
            blit_region.srcSubresource.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
            blit_region.srcOffsets[1]                 = blit_size;
            blit_region.dstSubresource.mipLevel       = mip_index;
            blit_region.dstSubresource.baseArrayLayer = 0;
            blit_region.dstSubresource.layerCount     = 1;
            blit_region.dstSubresource.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
            blit_region.dstOffsets[1]                 = blit_size;
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
            blit_region_count,
            &blit_regions[0],
            vulkan_filter[static_cast<uint32_t>(filter)]
        );

        // Transition to the initial layouts
        if (source->GetMipCount() > 1)
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
        // Validate command list state
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
        // Validate command list state
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
        // Validate command list state
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

    void RHI_CommandList::SetConstantBuffer(const uint32_t slot, const uint8_t scope, RHI_ConstantBuffer* constant_buffer) const
    {
        // Validate command list state
        SP_ASSERT(m_state == RHI_CommandListState::Recording);

        if (!m_descriptor_layout_current)
        {
            SP_LOG_WARNING("Descriptor layout not set, try setting constant buffer \"%s\" within a render pass", constant_buffer->GetObjectName().c_str());
            return;
        }

        // Set (will only happen if it's not already set)
        m_descriptor_layout_current->SetConstantBuffer(slot, constant_buffer);
    }

    void RHI_CommandList::SetSampler(const uint32_t slot, RHI_Sampler* sampler) const
    {
        // Validate command list state
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
        // Validate command list state
        SP_ASSERT(m_state == RHI_CommandListState::Recording);

        if (!m_descriptor_layout_current)
        {
            SP_LOG_WARNING("Descriptor layout not set, try setting structured buffer \"%s\" within a render pass", structured_buffer->GetObjectName().c_str());
            return;
        }

        m_descriptor_layout_current->SetStructuredBuffer(slot, structured_buffer);
    }

    uint32_t RHI_CommandList::GetGpuMemoryUsed()
    {
        if (!vulkan_utility::functions::get_physical_device_memory_properties_2)
            return 0;

        VkPhysicalDeviceMemoryBudgetPropertiesEXT device_memory_budget_properties = {};
        device_memory_budget_properties.sType                                     = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_BUDGET_PROPERTIES_EXT;
        device_memory_budget_properties.pNext                                     = nullptr;

        VkPhysicalDeviceMemoryProperties2 device_memory_properties = {};
        device_memory_properties.sType                             = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_PROPERTIES_2;
        device_memory_properties.pNext                             = &device_memory_budget_properties;

        vulkan_utility::functions::get_physical_device_memory_properties_2(static_cast<VkPhysicalDevice>(RHI_Context::device_physical), &device_memory_properties);

        return static_cast<uint32_t>(device_memory_budget_properties.heapUsage[0] / 1024 / 1024); // MBs
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

    void RHI_CommandList::BeginTimestamp(void* query)
    {
        // Validate command list state
        SP_ASSERT(m_state == RHI_CommandListState::Recording);

        if (!RHI_Context::gpu_profiling)
            return;

        if (!m_query_pool)
            return;

        vkCmdWriteTimestamp(static_cast<VkCommandBuffer>(m_rhi_resource), VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, static_cast<VkQueryPool>(m_query_pool), m_timestamp_index++);
    }

    void RHI_CommandList::EndTimestamp(void* query)
    {
        SP_ASSERT(m_state == RHI_CommandListState::Recording);
        SP_ASSERT(m_query_pool != nullptr);

        if (!RHI_Context::gpu_profiling)
            return;

        vkCmdWriteTimestamp(static_cast<VkCommandBuffer>(m_rhi_resource), VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, static_cast<VkQueryPool>(m_query_pool), m_timestamp_index++);
    }

    float RHI_CommandList::GetTimestampDuration(void* query_start, void* query_end, const uint32_t pass_index)
    {
        if (pass_index + 1 >= m_timestamps.size())
        {
            SP_LOG_ERROR("Pass index out of timestamp array range");
            return 0.0f;
        }

        uint64_t start = m_timestamps[pass_index];
        uint64_t end   = m_timestamps[pass_index + 1];

        // If end has not been acquired yet (zero), early exit
        if (end < start)
            return 0.0f;

        uint64_t duration = Math::Helper::Clamp<uint64_t>(end - start, 0, numeric_limits<uint64_t>::max());
        float duration_ms = static_cast<float>(duration * RHI_Device::GetTimestampPeriod() * 1e-6f);

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

        // Bind descriptor sets - If the descriptor set is null, it means we don't need to bind anything.
        if (RHI_DescriptorSet* descriptor_set = m_descriptor_layout_current->GetDescriptorSet())
        {
            // Get descriptor sets
            array<void*, 3> descriptor_sets =
            {
                descriptor_set->GetResource(),
                RHI_Device::GetDescriptorSet(RHI_Device_Resource::sampler_comparison),
                RHI_Device::GetDescriptorSet(RHI_Device_Resource::sampler_regular)
            };

            // Get dynamic offsets
            static vector<uint32_t> dynamic_offsets;
            m_descriptor_layout_current->GetDynamicOffsets(&dynamic_offsets);

            VkPipelineBindPoint bind_point = m_pso.IsCompute() ?
                VkPipelineBindPoint::VK_PIPELINE_BIND_POINT_COMPUTE :
                VkPipelineBindPoint::VK_PIPELINE_BIND_POINT_GRAPHICS;

            // Bind descriptor set
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

    void RHI_CommandList::GetDescriptorSetLayoutFromPipelineState(RHI_PipelineState& pipeline_state)
    {
        // Get pipeline
        vector<RHI_Descriptor> descriptors;
        GetDescriptorsFromPipelineState(pipeline_state, descriptors);

        // Compute a hash for the descriptors
        uint64_t hash = 0;
        for (RHI_Descriptor& descriptor : descriptors)
        {
            hash = rhi_hash_combine(hash, descriptor.GetHash());
        }

        // Search for a descriptor set layout which matches this hash
        auto it     = m_descriptor_set_layouts.find(hash);
        bool cached = it != m_descriptor_set_layouts.end();

        // If there is no descriptor set layout for this particular hash, create one
        if (!cached)
        {
            // Create a name for the descriptor set layout, very useful for Vulkan debugging
            string name  = "CS:" + (pipeline_state.shader_compute ? pipeline_state.shader_compute->GetObjectName() : "null");
            name        += "-VS:" + (pipeline_state.shader_vertex ? pipeline_state.shader_vertex->GetObjectName()  : "null");
            name        += "-PS:" + (pipeline_state.shader_pixel  ? pipeline_state.shader_pixel->GetObjectName()   : "null");

            // Emplace a new descriptor set layout
            it = m_descriptor_set_layouts.emplace(make_pair(hash, make_shared<RHI_DescriptorSetLayout>(descriptors, name.c_str()))).first;
        }

        // Get the descriptor set layout we will be using
        m_descriptor_layout_current = it->second.get();

        // Clear any data data the the descriptors might contain from previous uses (and hence can possibly be invalid by now)
        if (cached)
        {
            m_descriptor_layout_current->ClearDescriptorData();
        }

        // Make it bind
        m_descriptor_layout_current->NeedsToBind();
    }
}
