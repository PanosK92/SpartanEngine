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

//= INCLUDES ==========================
#include "Spartan.h"
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
#include "../RHI_Shader.h"
#include "../RHI_CommandPool.h"
#include "../../Profiling/Profiler.h"
#include "../../Rendering/Renderer.h"
//=====================================

//= NAMESPACES ===============
using namespace std;
using namespace Spartan::Math;
//============================

namespace Spartan
{
    unordered_map<uint32_t, shared_ptr<RHI_Pipeline>> RHI_CommandList::m_pipelines;

    static VkAttachmentLoadOp get_color_load_op(const Math::Vector4& color)
    {
        if (color == rhi_color_dont_care)
            return VK_ATTACHMENT_LOAD_OP_DONT_CARE;

        if (color == rhi_color_load)
            return VK_ATTACHMENT_LOAD_OP_LOAD;

        return VK_ATTACHMENT_LOAD_OP_CLEAR;
    };

    static VkAttachmentLoadOp get_depth_load_op(const float depth)
    {
        if (depth == rhi_depth_stencil_dont_care)
            return VK_ATTACHMENT_LOAD_OP_DONT_CARE;

        if (depth == rhi_depth_stencil_load)
            return VK_ATTACHMENT_LOAD_OP_LOAD;

        return VK_ATTACHMENT_LOAD_OP_CLEAR;
    };

    RHI_CommandList::RHI_CommandList(Context* context, void* cmd_pool_resource, const char* name) : SpartanObject(context)
    {
        m_renderer    = context->GetSubsystem<Renderer>();
        m_profiler    = context->GetSubsystem<Profiler>();
        m_rhi_device  = m_renderer->GetRhiDevice().get();
        m_object_name = name;

        RHI_Context* rhi_context = m_rhi_device->GetContextRhi();

        // Command buffer
        {
            VkCommandBufferAllocateInfo allocate_info = {};
            allocate_info.sType                       = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
            allocate_info.commandPool                 = static_cast<VkCommandPool>(cmd_pool_resource);
            allocate_info.level                       = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
            allocate_info.commandBufferCount          = 1;

            // Allocate
            SP_ASSERT(vulkan_utility::error::check(
                vkAllocateCommandBuffers(m_rhi_device->GetContextRhi()->device, &allocate_info, reinterpret_cast<VkCommandBuffer*>(&m_resource))) &&
                "Failed to allocate command buffer"
            );

            // Name
            vulkan_utility::debug::set_name(static_cast<VkCommandBuffer>(m_resource), name);
        }

        // Query pool
        if (rhi_context->gpu_profiling)
        {
            VkQueryPoolCreateInfo query_pool_create_info = {};
            query_pool_create_info.sType                 = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
            query_pool_create_info.queryType             = VK_QUERY_TYPE_TIMESTAMP;
            query_pool_create_info.queryCount            = m_max_timestamps;

            auto query_pool = reinterpret_cast<VkQueryPool*>(&m_query_pool);
            vulkan_utility::error::check(vkCreateQueryPool(rhi_context->device, &query_pool_create_info, nullptr, query_pool));

            m_timestamps.fill(0);
        }

        // Sync objects
        m_proccessed_fence     = make_shared<RHI_Fence>(m_rhi_device, name);
        m_proccessed_semaphore = make_shared<RHI_Semaphore>(m_rhi_device, false, name);
    }

    RHI_CommandList::~RHI_CommandList()
    {
        // Wait in case it's still in use by the GPU
        m_rhi_device->QueueWaitAll();

        // Query pool
        if (m_query_pool)
        {
            vkDestroyQueryPool(m_rhi_device->GetContextRhi()->device, static_cast<VkQueryPool>(m_query_pool), nullptr);
            m_query_pool = nullptr;
        }
    }

    void RHI_CommandList::Begin()
    {
        m_discard = false;

        // Validate command list state
        SP_ASSERT(m_state == RHI_CommandListState::Idle);

        // Get queries
        {
            if (m_rhi_device->GetContextRhi()->gpu_profiling)
            {
                if (m_query_pool)
                {
                    if (m_timestamp_index != 0)
                    {
                        const uint32_t query_count     = m_timestamp_index * 2;
                        const size_t stride            = sizeof(uint64_t);
                        const VkQueryResultFlags flags = VK_QUERY_RESULT_64_BIT;

                        vkGetQueryPoolResults(
                            m_rhi_device->GetContextRhi()->device,  // device
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
        SP_ASSERT_MSG(vulkan_utility::error::check(vkBeginCommandBuffer(static_cast<VkCommandBuffer>(m_resource), &begin_info)), "Failed to begin command buffer");

        // Reset query pool - Has to be done after vkBeginCommandBuffer or a VK_DEVICE_LOST will occur
        vkCmdResetQueryPool(static_cast<VkCommandBuffer>(m_resource), static_cast<VkQueryPool>(m_query_pool), 0, m_max_timestamps);

        // Update states
        m_state          = RHI_CommandListState::Recording;
        m_pipeline_dirty = true;
    }

    bool RHI_CommandList::End()
    {
        // Validate command list state
        SP_ASSERT(m_state == RHI_CommandListState::Recording);

        if (!vulkan_utility::error::check(vkEndCommandBuffer(static_cast<VkCommandBuffer>(m_resource))))
            return false;

        m_state = RHI_CommandListState::Ended;
        return true;
    }

    bool RHI_CommandList::Submit()
    {
        // Validate command list state
        SP_ASSERT(m_state == RHI_CommandListState::Ended);

        if (m_discard)
        {
            m_state = RHI_CommandListState::Submitted;
            return true;
        }

        if (!m_rhi_device->QueueSubmit(
            RHI_Queue_Type::Graphics,                      // queue
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, // wait flags
            static_cast<VkCommandBuffer>(m_resource),      // cmd buffer
            nullptr,                                       // wait semaphore
            m_proccessed_semaphore.get(),                  // signal semaphore
            m_proccessed_fence.get()                       // signal fence
            ))
        {
            LOG_ERROR("Failed to submit the command list.");
            return false;
        }

        m_state = RHI_CommandListState::Submitted;

        return true;
    }

    bool RHI_CommandList::Reset()
    {
        // Validate command list state
        SP_ASSERT(m_state == RHI_CommandListState::Submitted);

        lock_guard<mutex> guard(m_mutex_reset);

        if (!vulkan_utility::error::check(vkResetCommandBuffer(static_cast<VkCommandBuffer>(m_resource), VK_COMMAND_BUFFER_RESET_RELEASE_RESOURCES_BIT)))
            return false;

        m_state = RHI_CommandListState::Idle;

        return true;
    }

    void RHI_CommandList::SetPipelineState(RHI_PipelineState& pso)
    {
        SP_ASSERT(pso.IsValid() && "Pipeline state is invalid");
        SP_ASSERT(m_state == RHI_CommandListState::Recording);

        // Update the descriptor cache with the pipeline state
        Descriptors_GetLayoutFromPipelineState(pso);

        // If no pipeline exists for this state, create one
        uint32_t hash_previous = m_pso.ComputeHash();
        uint32_t hash = pso.ComputeHash();
        auto it = m_pipelines.find(hash);
        if (it == m_pipelines.end())
        {
            // Create a new pipeline
            it = m_pipelines.emplace(make_pair(hash, move(make_shared<RHI_Pipeline>(m_rhi_device, pso, m_descriptor_layout_current)))).first;
            LOG_INFO("A new pipeline has been created.");
        }

        m_pipeline = it->second.get();
        m_pso = pso;

        // Determine if the pipeline is dirty
        if (!m_pipeline_dirty)
        {
            m_pipeline_dirty = hash_previous != hash;
        }

        // If the pipeline changed, resources have to be set again
        if (m_pipeline_dirty)
        {
            m_vertex_buffer_id = 0;
            m_index_buffer_id = 0;
        }
    }

    void RHI_CommandList::BeginRenderPass()
    {
        SP_ASSERT(m_state == RHI_CommandListState::Recording);
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
                color_attachment.imageView                 = static_cast<VkImageView>(swapchain->Get_Resource_View());
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

                    SP_ASSERT(rt->IsRenderTargetColor());

                    // Transition to the appropriate layout
                    if (rt->GetLayout(0) != RHI_Image_Layout::Color_Attachment_Optimal)
                    {
                        rt->SetLayout(RHI_Image_Layout::Color_Attachment_Optimal, this);
                    }

                    VkRenderingAttachmentInfo color_attachment = {};
                    color_attachment.sType                     = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO_KHR;
                    color_attachment.imageView                 = static_cast<VkImageView>(rt->GetResource_View_RenderTarget(m_pso.render_target_color_texture_array_index));
                    color_attachment.imageLayout               = vulkan_image_layout[static_cast<uint8_t>(rt->GetLayout(0))];
                    color_attachment.loadOp                    = get_color_load_op(m_pso.clear_color[i]);
                    color_attachment.storeOp                   = VK_ATTACHMENT_STORE_OP_STORE;
                    color_attachment.clearValue.color          = { m_pso.clear_color[i].x, m_pso.clear_color[i].y, m_pso.clear_color[i].z, m_pso.clear_color[i].w };

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

            SP_ASSERT(rt->IsRenderTargetDepthStencil());

            // Transition to the appropriate layout
            RHI_Image_Layout layout = rt->IsStencilFormat() ? RHI_Image_Layout::Depth_Stencil_Attachment_Optimal : RHI_Image_Layout::Depth_Attachment_Optimal;
            if (m_pso.render_target_depth_texture_read_only)
            {
                layout = RHI_Image_Layout::Depth_Stencil_Read_Only_Optimal;
            }
            rt->SetLayout(layout, this);

            attachment_depth_stencil.sType                           = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO_KHR;
            attachment_depth_stencil.imageView                       = static_cast<VkImageView>(rt->GetResource_View_DepthStencil(m_pso.render_target_depth_stencil_texture_array_index));
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
        vkCmdBeginRendering(static_cast<VkCommandBuffer>(m_resource), &rendering_info);

        m_is_rendering = true;
    }

    void RHI_CommandList::EndRenderPass()
    {
        if (m_is_rendering)
        {
            vkCmdEndRendering(static_cast<VkCommandBuffer>(m_resource));
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
                attachment.clearValue.color.float32[0]  = pipeline_state.clear_color[i].x;
                attachment.clearValue.color.float32[1]  = pipeline_state.clear_color[i].y;
                attachment.clearValue.color.float32[2]  = pipeline_state.clear_color[i].z;
                attachment.clearValue.color.float32[3]  = pipeline_state.clear_color[i].w;
            }
        }

        bool clear_depth   = pipeline_state.clear_depth   != rhi_depth_stencil_load && pipeline_state.clear_depth   != rhi_depth_stencil_dont_care;
        bool clear_stencil = pipeline_state.clear_stencil != rhi_depth_stencil_load && pipeline_state.clear_stencil != rhi_depth_stencil_dont_care;

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

        vkCmdClearAttachments(static_cast<VkCommandBuffer>(m_resource), attachment_count, attachments.data(), 1, &clear_rect);
    }

    void RHI_CommandList::ClearRenderTarget(RHI_Texture* texture,
        const uint32_t color_index          /*= 0*/,
        const uint32_t depth_stencil_index  /*= 0*/,
        const bool storage                  /*= false*/,
        const Math::Vector4& clear_color    /*= rhi_color_load*/,
        const float clear_depth             /*= rhi_depth_load*/,
        const float clear_stencil           /*= rhi_stencil_load*/
    )
    {
        SP_ASSERT(m_state == RHI_CommandListState::Recording);
        SP_ASSERT(texture->CanBeCleared());

        if (!texture || !texture->GetResource_View_Srv())
        {
            LOG_ERROR("Texture is null.");
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
            VkClearColorValue _clear_color = { clear_color.x, clear_color.y, clear_color.z, clear_color.w };

            image_subresource_range.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;

            vkCmdClearColorImage(static_cast<VkCommandBuffer>(m_resource), static_cast<VkImage>(texture->GetResource()), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &_clear_color, 1, &image_subresource_range);
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

            vkCmdClearDepthStencilImage(static_cast<VkCommandBuffer>(m_resource), static_cast<VkImage>(texture->GetResource()), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &clear_depth_stencil, 1, &image_subresource_range);
        }
    }

    void RHI_CommandList::Draw(const uint32_t vertex_count, uint32_t vertex_start_index /*= 0*/)
    {
        SP_ASSERT(m_state == RHI_CommandListState::Recording);

        // Ensure correct state before attempting to draw
        OnDraw();

        // Draw
        vkCmdDraw(
            static_cast<VkCommandBuffer>(m_resource), // commandBuffer
            vertex_count,                             // vertexCount
            1,                                        // instanceCount
            vertex_start_index,                       // firstVertex
            0                                         // firstInstance
        );

        if (m_profiler)
        {
            m_profiler->m_rhi_draw++;
        }
    }

    void RHI_CommandList::DrawIndexed(const uint32_t index_count, const uint32_t index_offset, const uint32_t vertex_offset)
    {
        SP_ASSERT(m_state == RHI_CommandListState::Recording);

        // Ensure correct state before attempting to draw
        OnDraw();

        // Draw
        vkCmdDrawIndexed(
            static_cast<VkCommandBuffer>(m_resource), // commandBuffer
            index_count,                              // indexCount
            1,                                        // instanceCount
            index_offset,                             // firstIndex
            vertex_offset,                            // vertexOffset
            0                                         // firstInstance
        );

        // Profile
        m_profiler->m_rhi_draw++;
    }

    void RHI_CommandList::Dispatch(uint32_t x, uint32_t y, uint32_t z /*= 1*/, bool async /*= false*/)
    {
        SP_ASSERT(m_state == RHI_CommandListState::Recording);

        // Ensure correct state before attempting to dispatch
        OnDraw();

        // Dispatch
        vkCmdDispatch(static_cast<VkCommandBuffer>(m_resource), x, y, z);

        if (m_profiler)
        {
            m_profiler->m_rhi_dispatch++;
        }
    }

    void RHI_CommandList::Blit(RHI_Texture* source, RHI_Texture* destination)
    {
        // D3D11 baggage: https://docs.microsoft.com/en-us/windows/win32/api/d3d11/nf-d3d11-id3d11devicecontext-copyresource
        SP_ASSERT(source != nullptr);
        SP_ASSERT(destination != nullptr);
        SP_ASSERT(source->GetResource() != nullptr);
        SP_ASSERT(destination->GetResource() != nullptr);
        SP_ASSERT(source->GetObjectId() != destination->GetObjectId());
        SP_ASSERT(source->GetFormat() == destination->GetFormat());
        SP_ASSERT(source->GetWidth() == destination->GetWidth());
        SP_ASSERT(source->GetHeight() == destination->GetHeight());
        SP_ASSERT(source->GetArrayLength() == destination->GetArrayLength());
        SP_ASSERT(source->GetMipCount() == destination->GetMipCount());

        VkOffset3D blit_size = {};
        blit_size.x          = source->GetWidth();
        blit_size.y          = source->GetHeight();
        blit_size.z          = 1;

        VkImageBlit blit_region               = {};
        blit_region.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        blit_region.srcSubresource.layerCount = source->GetMipCount();
        blit_region.srcOffsets[1]             = blit_size;
        blit_region.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        blit_region.dstSubresource.layerCount = destination->GetMipCount();
        blit_region.dstOffsets[1]             = blit_size;

        // Save the initial layouts
        std::array<RHI_Image_Layout, 12> layouts_initial_source      = source->GetLayouts();
        std::array<RHI_Image_Layout, 12> layouts_initial_destination = destination->GetLayouts();

        // Transition to blit appropriate layouts
        source->SetLayout(RHI_Image_Layout::Transfer_Src_Optimal, this);
        destination->SetLayout(RHI_Image_Layout::Transfer_Dst_Optimal, this);

        // Blit
        vkCmdBlitImage(
            static_cast<VkCommandBuffer>(m_resource),
            static_cast<VkImage>(source->GetResource()),      VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            static_cast<VkImage>(destination->GetResource()), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            1,
            &blit_region,
            VK_FILTER_NEAREST
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
        // Validate command list state
        SP_ASSERT(m_state == RHI_CommandListState::Recording);

        VkViewport vk_viewport = {};
        vk_viewport.x          = viewport.x;
        vk_viewport.y          = viewport.y;
        vk_viewport.width      = viewport.width;
        vk_viewport.height     = viewport.height;
        vk_viewport.minDepth   = viewport.depth_min;
        vk_viewport.maxDepth   = viewport.depth_max;

        vkCmdSetViewport(
            static_cast<VkCommandBuffer>(m_resource), // commandBuffer
            0,                                          // firstViewport
            1,                                          // viewportCount
            &vk_viewport                                // pViewports
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
            static_cast<VkCommandBuffer>(m_resource), // commandBuffer
            0,          // firstScissor
            1,          // scissorCount
            &vk_scissor // pScissors
        );
    }

    void RHI_CommandList::SetBufferVertex(const RHI_VertexBuffer* buffer, const uint64_t offset /*= 0*/)
    {
        // Validate command list state
        SP_ASSERT(m_state == RHI_CommandListState::Recording);

        if (m_vertex_buffer_id == buffer->GetObjectId() && m_vertex_buffer_offset == offset)
            return;

        VkBuffer vertex_buffers[] = { static_cast<VkBuffer>(buffer->GetResource()) };
        VkDeviceSize offsets[]    = { offset };

        vkCmdBindVertexBuffers(
            static_cast<VkCommandBuffer>(m_resource), // commandBuffer
            0,                                        // firstBinding
            1,                                        // bindingCount
            vertex_buffers,                           // pBuffers
            offsets                                   // pOffsets
        );

        m_vertex_buffer_id     = buffer->GetObjectId();
        m_vertex_buffer_offset = offset;

        if (m_profiler)
        {
            m_profiler->m_rhi_bindings_buffer_vertex++;
        }
    }

    void RHI_CommandList::SetBufferIndex(const RHI_IndexBuffer* buffer, const uint64_t offset /*= 0*/)
    {
        // Validate command list state
        SP_ASSERT(m_state == RHI_CommandListState::Recording);

        if (m_index_buffer_id == buffer->GetObjectId() && m_index_buffer_offset == offset)
            return;

        vkCmdBindIndexBuffer(
            static_cast<VkCommandBuffer>(m_resource),                       // commandBuffer
            static_cast<VkBuffer>(buffer->GetResource()),                   // buffer
            offset,                                                         // offset
            buffer->Is16Bit() ? VK_INDEX_TYPE_UINT16 : VK_INDEX_TYPE_UINT32 // indexType
        );

        m_index_buffer_id     = buffer->GetObjectId();
        m_index_buffer_offset = offset;

        if (m_profiler)
        {
            m_profiler->m_rhi_bindings_buffer_index++;
        }
    }

    void RHI_CommandList::SetConstantBuffer(const uint32_t slot, const uint8_t scope, RHI_ConstantBuffer* constant_buffer) const
    {
        // Validate command list state
        SP_ASSERT(m_state == RHI_CommandListState::Recording);

        if (!m_descriptor_layout_current)
        {
            LOG_WARNING("Descriptor layout not set, try setting constant buffer \"%s\" within a render pass", constant_buffer->GetObjectName().c_str());
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
            LOG_WARNING("Descriptor layout not set, try setting sampler \"%s\" within a render pass", sampler->GetObjectName().c_str());
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
            LOG_WARNING("Descriptor layout not set, try setting texture \"%s\" within a render pass", texture->GetObjectName().c_str());
            return;
        }

        // Null textures are allowed, and we replace them with a transparent texture.
        if (!texture || !texture->GetResource_View_Srv())
        {
            texture = m_renderer->GetDefaultTextureTransparent();
        }

        // Get some texture info
        const uint32_t mip_count        = texture->GetMipCount();
        const bool mip_specified        = mip_index != rhi_all_mips;
        const uint32_t mip_start        = mip_specified ? mip_index : 0;
        RHI_Image_Layout current_layout = texture->GetLayout(mip_start);

        // If the image has an invalid layout (can happen for a few frames during staging), replace with a default texture
        if (current_layout == RHI_Image_Layout::Undefined || current_layout == RHI_Image_Layout::Preinitialized)
        {
            LOG_ERROR("Can't set texture without a layout, replacing with a default texture");
            texture = m_renderer->GetDefaultTextureTransparent();
            current_layout = texture->GetLayout(0);
        }

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
                array<RHI_Image_Layout, 12> layouts = texture->GetLayouts();
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
            LOG_WARNING("Descriptor layout not set, try setting structured buffer \"%s\" within a render pass", structured_buffer->GetObjectName().c_str());
            return;
        }

        m_descriptor_layout_current->SetStructuredBuffer(slot, structured_buffer);
    }

    uint32_t RHI_CommandList::GetGpuMemoryUsed(RHI_Device* rhi_device)
    {
        if (!rhi_device || !rhi_device->GetContextRhi() || !vulkan_utility::functions::get_physical_device_memory_properties_2)
            return 0;

        VkPhysicalDeviceMemoryBudgetPropertiesEXT device_memory_budget_properties = {};
        device_memory_budget_properties.sType                                     = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_BUDGET_PROPERTIES_EXT;
        device_memory_budget_properties.pNext                                     = nullptr;

        VkPhysicalDeviceMemoryProperties2 device_memory_properties = {};
        device_memory_properties.sType                             = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_PROPERTIES_2;
        device_memory_properties.pNext                             = &device_memory_budget_properties;

        vulkan_utility::functions::get_physical_device_memory_properties_2(static_cast<VkPhysicalDevice>(rhi_device->GetContextRhi()->device_physical), &device_memory_properties);

        return static_cast<uint32_t>(device_memory_budget_properties.heapUsage[0] / 1024 / 1024); // MBs
    }

    void RHI_CommandList::BeginMarker(const char* name)
    {
        if (m_rhi_device->GetContextRhi()->gpu_markers)
        {
            vulkan_utility::debug::marker_begin(static_cast<VkCommandBuffer>(m_resource), name, Vector4::Zero);
        }
    }

    void RHI_CommandList::EndMarker()
    {
        if (m_rhi_device->GetContextRhi()->gpu_markers)
        {
            vulkan_utility::debug::marker_end(static_cast<VkCommandBuffer>(m_resource));
        }
    }

    void RHI_CommandList::BeginTimestamp(void* query)
    {
        // Validate command list state
        SP_ASSERT(m_state == RHI_CommandListState::Recording);

        if (!m_rhi_device->GetContextRhi()->gpu_profiling)
            return;

        if (!m_query_pool)
            return;

        vkCmdWriteTimestamp(static_cast<VkCommandBuffer>(m_resource), VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, static_cast<VkQueryPool>(m_query_pool), m_timestamp_index++);
    }

    void RHI_CommandList::EndTimestamp(void* query)
    {
        SP_ASSERT(m_state == RHI_CommandListState::Recording);
        SP_ASSERT(m_query_pool != nullptr);

        if (!m_rhi_device->GetContextRhi()->gpu_profiling)
            return;

        vkCmdWriteTimestamp(static_cast<VkCommandBuffer>(m_resource), VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, static_cast<VkQueryPool>(m_query_pool), m_timestamp_index++);
    }

    float RHI_CommandList::GetTimestampDuration(void* query_start, void* query_end, const uint32_t pass_index)
    {
        if (pass_index + 1 >= m_timestamps.size())
        {
            LOG_ERROR("Pass index out of timestamp array range");
            return 0.0f;
        }

        uint64_t start = m_timestamps[pass_index];
        uint64_t end   = m_timestamps[pass_index + 1];

        // If end has not been acquired yet (zero), early exit
        if (end < start)
            return 0.0f;

        uint64_t duration = Math::Helper::Clamp<uint64_t>(end - start, 0, std::numeric_limits<uint64_t>::max());
        float duration_ms = static_cast<float>(duration * m_rhi_device->GetTimestampPeriod() * 1e-6f);

        return duration_ms;
    }

    void RHI_CommandList::BeginTimeblock(const char* name, const bool gpu_marker, const bool gpu_timing)
    {
        SP_ASSERT_MSG(!m_timeblock_is_active, "The previous time block is still active");
        SP_ASSERT(name != nullptr);

        // Allowed profiler ?
        if (m_rhi_device->GetContextRhi()->gpu_profiling && gpu_timing && m_profiler)
        {
            m_profiler->TimeBlockStart(name, TimeBlockType::Cpu, this);
            m_profiler->TimeBlockStart(name, TimeBlockType::Gpu, this);
        }

        // Allowed to markers ?
        if (m_rhi_device->GetContextRhi()->gpu_markers && gpu_marker)
        {
            vulkan_utility::debug::marker_begin(static_cast<VkCommandBuffer>(m_resource), name, Vector4::Zero);
        }

        m_timeblock_is_active = true;
    }

    void RHI_CommandList::EndTimeblock()
    {
        SP_ASSERT_MSG(m_timeblock_is_active, "A time block wasn't started");

        // Allowed markers ?
        if (m_rhi_device->GetContextRhi()->gpu_markers)
        {
            vulkan_utility::debug::marker_end(static_cast<VkCommandBuffer>(m_resource));
        }

        // Allowed profiler ?
        if (m_rhi_device->GetContextRhi()->gpu_profiling && m_profiler)
        {
            m_profiler->TimeBlockEnd(); // cpu
            m_profiler->TimeBlockEnd(); // gpu
        }

        m_timeblock_is_active = false;
    }

    void RHI_CommandList::OnDraw()
    {
        SP_ASSERT(m_state == RHI_CommandListState::Recording);
        SP_ASSERT(m_pipeline);

        // Bind pipeline
        if (m_pipeline_dirty)
        {
            // Get
            VkPipeline vk_pipeline = static_cast<VkPipeline>(m_pipeline->GetResource_Pipeline());
            SP_ASSERT(vk_pipeline != nullptr);

            // Bind
            VkPipelineBindPoint pipeline_bind_point = m_pso.IsCompute() ? VK_PIPELINE_BIND_POINT_COMPUTE : VK_PIPELINE_BIND_POINT_GRAPHICS;
            vkCmdBindPipeline(static_cast<VkCommandBuffer>(m_resource), pipeline_bind_point, vk_pipeline);

            // Profile
            if (m_profiler)
            {
                m_profiler->m_rhi_bindings_pipeline++;
            }

            m_pipeline_dirty = false;
        }

        // Bind descriptor sets
        {
            m_renderer->SetGlobalShaderResources(this);

            // If the descriptor set is null, it means we don't need to bind anything.
            if (RHI_DescriptorSet* descriptor_set = m_descriptor_layout_current->GetDescriptorSet())
            {
                // Get descriptor sets
                array<void*, 1> descriptor_sets = { descriptor_set->GetResource() };

                // Bind descriptor set
                vkCmdBindDescriptorSets
                (
                    static_cast<VkCommandBuffer>(m_resource),                                // commandBuffer
                    m_pso.IsCompute() ?                                                      // pipelineBindPoint
                    VkPipelineBindPoint::VK_PIPELINE_BIND_POINT_COMPUTE :                    
                    VkPipelineBindPoint::VK_PIPELINE_BIND_POINT_GRAPHICS,                    
                    static_cast<VkPipelineLayout>(m_pipeline->GetResource_PipelineLayout()), // layout
                    0,                                                                       // firstSet
                    static_cast<uint32_t>(descriptor_sets.size()),                           // descriptorSetCount
                    reinterpret_cast<VkDescriptorSet*>(descriptor_sets.data()),              // pDescriptorSets
                    m_descriptor_layout_current->GetConstantBufferCount(),                   // dynamicOffsetCount
                    m_descriptor_layout_current->GetDynamicOffsets()                         // pDynamicOffsets
                );

                if (m_profiler)
                {
                    m_profiler->m_rhi_bindings_descriptor_set++;
                }
            }
        }
    }

    void RHI_CommandList::UnbindOutputTextures()
    {

    }

    void RHI_CommandList::Descriptors_GetLayoutFromPipelineState(RHI_PipelineState& pipeline_state)
    {
        // Get pipeline
        vector<RHI_Descriptor> descriptors;
        Descriptors_GetDescriptorsFromPipelineState(pipeline_state, descriptors);

        // Compute a hash for the descriptors
        uint32_t hash = 0;
        for (const RHI_Descriptor& descriptor : descriptors)
        {
            Utility::Hash::hash_combine(hash, descriptor.ComputeHash());
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
            it = m_descriptor_set_layouts.emplace(make_pair(hash, make_shared<RHI_DescriptorSetLayout>(m_rhi_device, descriptors, name.c_str()))).first;
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
