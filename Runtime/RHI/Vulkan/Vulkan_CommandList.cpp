/*
Copyright(c) 2016-2019 Panos Karabelas

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

//= IMPLEMENTATION ===============
#include "../RHI_Implementation.h"
#ifdef API_GRAPHICS_VULKAN
//================================

//= INCLUDES ========================
#include "../RHI_CommandList.h"
#include "../RHI_Pipeline.h"
#include "../RHI_Device.h"
#include "../RHI_SwapChain.h"
#include "../RHI_Sampler.h"
#include "../RHI_Texture.h"
#include "../RHI_VertexBuffer.h"
#include "../RHI_IndexBuffer.h"
#include "../RHI_ConstantBuffer.h"
#include "../../Profiling/Profiler.h"
#include "../../Logging/Log.h"
#include "../../Rendering/Renderer.h"
//===================================

//= NAMESPACES ================
using namespace std;
using namespace Spartan::Math;
//=============================

#define CMD_BUFFER static_cast<VkCommandBuffer>(m_cmd_buffer)

namespace Spartan
{
    RHI_CommandList::RHI_CommandList(uint32_t index, RHI_SwapChain* swap_chain, Context* context)
	{
        m_swap_chain            = swap_chain;
        m_renderer              = context->GetSubsystem<Renderer>().get();
        m_profiler              = context->GetSubsystem<Profiler>().get();
		m_rhi_device	        = m_renderer->GetRhiDevice().get();
        m_rhi_pipeline_cache    = m_renderer->GetPipelineCache().get();

        RHI_Context* rhi_context = m_rhi_device->GetContextRhi();

        // Command buffer
        vulkan_common::command::create_buffer(rhi_context, m_swap_chain->GetCmdPool(), m_cmd_buffer, VK_COMMAND_BUFFER_LEVEL_PRIMARY);

        // Fence
        vulkan_common::fence::create(rhi_context, m_cmd_list_consumed_fence);
	}

	RHI_CommandList::~RHI_CommandList()
	{
        RHI_Context* rhi_context = m_rhi_device->GetContextRhi();

		// Wait in case the buffer is still in use by the graphics queue
		vkQueueWaitIdle(rhi_context->queue_graphics);

		// Fence
        vulkan_common::fence::destroy(rhi_context, m_cmd_list_consumed_fence);

        // Command buffer
        vulkan_common::command::free(m_rhi_device->GetContextRhi(), m_swap_chain->GetCmdPool(), m_cmd_buffer);
	}

	bool RHI_CommandList::Begin(const string& pass_name, RHI_Cmd_Type type /*= RHI_Cmd_Begin*/)
	{
        RHI_Context* rhi_context = m_rhi_device->GetContextRhi();
        m_begin_types.push_back(type);

        // Profiling - Start
        if (type == RHI_Cmd_Begin || type == RHI_Cmd_Marker)
        {
            if (m_profiler)
            {
                m_profiler->TimeBlockStart(pass_name, true, true);
            }

            if (m_rhi_device->GetContextRhi()->debug_markers_enabled)
            {
                vulkan_common::debug_marker::begin(CMD_BUFFER, pass_name.c_str(), Vector4::One);
            }
        }

        if (type == RHI_Cmd_Begin)
        {
            // Sync CPU to GPU
            if (m_cmd_state == RHI_Cmd_List_Idle_Sync_Cpu_To_Gpu)
            {
                Flush();
                m_pipeline->OnCommandListConsumed();
                m_cmd_state = RHI_Cmd_List_Idle;
            }

            if (m_cmd_state != RHI_Cmd_List_Idle)
            {
                LOG_ERROR("Previous command list is still being used");
                return false;
            }

            if (!m_pipeline_state.IsDefined())
            {
                LOG_ERROR("Pipeline state is not defined");
                return false;
            }

            m_pipeline = m_rhi_pipeline_cache->GetPipeline(m_pipeline_state).get();
            m_pipeline->MakeDirty();
            if (!m_pipeline)
            {
                LOG_ERROR("Failed to get pipeline");
                return false;
            }
	
		    // Acquire next image (in case the render target is a swapchain)
            if (!m_pipeline_state.AcquireNextImage())
                return false;

		    // Begin command buffer
		    VkCommandBufferBeginInfo begin_info	    = {};
		    begin_info.sType						= VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
		    begin_info.flags						= VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT;
		    if (!vulkan_common::error::check_result(vkBeginCommandBuffer(CMD_BUFFER, &begin_info)))
		    	return false;

		    // Begin render pass
		    VkClearValue clear_color					= { 1.0f, 0.0f, 0.0f, 1.0f };
		    VkRenderPassBeginInfo render_pass_info		= {};
		    render_pass_info.sType						= VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
		    render_pass_info.renderPass					= static_cast<VkRenderPass>(m_pipeline_state.GetRenderPass());
		    render_pass_info.framebuffer				= static_cast<VkFramebuffer>(m_pipeline_state.GetFrameBuffer());
		    render_pass_info.renderArea.offset			= { 0, 0 };
            render_pass_info.renderArea.extent.width    = m_pipeline_state.GetWidth();
		    render_pass_info.renderArea.extent.height	= m_pipeline_state.GetHeight();
		    render_pass_info.clearValueCount			= 1;
		    render_pass_info.pClearValues				= &clear_color;
		    vkCmdBeginRenderPass(CMD_BUFFER, &render_pass_info, VK_SUBPASS_CONTENTS_INLINE);

            // Bind pipeline
            if (VkPipeline pipeline = static_cast<VkPipeline>(m_pipeline->GetPipeline()))
            {
                vkCmdBindPipeline(CMD_BUFFER, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
            }
            else
            {
                LOG_ERROR("Invalid pipeline");
                return false;
            }

            // At this point, it's safe to allow for command recording
            m_cmd_state = RHI_Cmd_List_Recording;

            // Temp hack until I implement some method to have one time set global resources
            m_renderer->SetGlobalSamplersAndConstantBuffers(this);
        }

        return true;
	}

	void RHI_CommandList::End()
	{
        RHI_Cmd_Type begin_type = m_begin_types.back();

        // Profiling - End
        if (begin_type == RHI_Cmd_Begin || begin_type == RHI_Cmd_Marker)
        {
            if (m_rhi_device->GetContextRhi()->debug_markers_enabled)
            {
                vulkan_common::debug_marker::end(CMD_BUFFER);
            }

            if (m_profiler)
            {
                m_profiler->TimeBlockEnd();
            }
        }

        if (begin_type == RHI_Cmd_Begin)
        {
            if (m_cmd_state != RHI_Cmd_List_Recording)
            {
                LOG_WARNING("Can't record command");
                return;
            }

            vkCmdEndRenderPass(CMD_BUFFER);

            if (vulkan_common::error::check_result(vkEndCommandBuffer(CMD_BUFFER)))
            {
                m_cmd_state = RHI_Cmd_List_Ended;
            }
        }

        m_begin_types.pop_back();
	}

	void RHI_CommandList::Draw(const uint32_t vertex_count)
	{
        if (m_cmd_state != RHI_Cmd_List_Recording)
        {
            LOG_WARNING("Can't record command");
            return;
        }

        // Update descriptor set (if needed)
        if (void* descriptor = m_pipeline->GetDescriptorSet())
        {
            // Bind descriptor set
            VkDescriptorSet descriptor_sets[1] = { static_cast<VkDescriptorSet>(descriptor) };
            vkCmdBindDescriptorSets
            (
                CMD_BUFFER,                                                     // commandBuffer
                VK_PIPELINE_BIND_POINT_GRAPHICS,                                // pipelineBindPoint
                static_cast<VkPipelineLayout>(m_pipeline->GetPipelineLayout()), // layout
                0,                                                              // firstSet
                1,                                                              // descriptorSetCount
                descriptor_sets,                                                // pDescriptorSets
                0,                                                              // dynamicOffsetCount
                nullptr                                                         // pDynamicOffsets
            );
        }

		vkCmdDraw(
            CMD_BUFFER,     // commandBuffer
            vertex_count,   // vertexCount
            1,              // instanceCount
            0,              // firstVertex
            0               // firstInstance
        );
	}

	void RHI_CommandList::DrawIndexed(const uint32_t index_count, const uint32_t index_offset, const uint32_t vertex_offset)
	{
        if (m_cmd_state != RHI_Cmd_List_Recording)
        {
            LOG_WARNING("Can't record command");
            return;
        }

        // Update descriptor set (if needed)
        if (void* descriptor = m_pipeline->GetDescriptorSet())
        {
            // Bind descriptor set
            VkDescriptorSet descriptor_sets[1] = { static_cast<VkDescriptorSet>(descriptor) };
            vkCmdBindDescriptorSets
            (
                CMD_BUFFER,                                                     // commandBuffer
                VK_PIPELINE_BIND_POINT_GRAPHICS,                                // pipelineBindPoint
                static_cast<VkPipelineLayout>(m_pipeline->GetPipelineLayout()), // layout
                0,                                                              // firstSet
                1,                                                              // descriptorSetCount
                descriptor_sets,                                                // pDescriptorSets
                0,                                                              // dynamicOffsetCount
                nullptr                                                         // pDynamicOffsets
            );
        }

		vkCmdDrawIndexed(
            CMD_BUFFER,     // commandBuffer
            index_count,    // indexCount
            1,              // instanceCount
            index_offset,   // firstIndex
            vertex_offset,  // vertexOffset
            0               // firstInstance
        );
	}

	void RHI_CommandList::SetViewport(const RHI_Viewport& viewport)
	{
        if (m_cmd_state != RHI_Cmd_List_Recording)
        {
            LOG_WARNING("Can't record command");
            return;
        }

		VkViewport vk_viewport	= {};
		vk_viewport.x			= viewport.x;
		vk_viewport.y			= viewport.y;
		vk_viewport.width		= viewport.width;
		vk_viewport.height		= viewport.height;
		vk_viewport.minDepth	= viewport.depth_min;
		vk_viewport.maxDepth	= viewport.depth_max;

		vkCmdSetViewport(
            CMD_BUFFER,     // commandBuffer
            0,              // firstViewport
            1,              // viewportCount
            &vk_viewport    // pViewports
        );
	}

	void RHI_CommandList::SetScissorRectangle(const Math::Rectangle& scissor_rectangle)
	{
        if (m_cmd_state != RHI_Cmd_List_Recording)
        {
            LOG_WARNING("Can't record command");
            return;
        }

		VkRect2D vk_scissor;
		vk_scissor.offset.x			= static_cast<int32_t>(scissor_rectangle.x);
		vk_scissor.offset.y			= static_cast<int32_t>(scissor_rectangle.y);
		vk_scissor.extent.width		= static_cast<uint32_t>(scissor_rectangle.width);
		vk_scissor.extent.height	= static_cast<uint32_t>(scissor_rectangle.height);

		vkCmdSetScissor(
            CMD_BUFFER, // commandBuffer
            0,          // firstScissor
            1,          // scissorCount
            &vk_scissor // pScissors
        );
	}

	void RHI_CommandList::SetPrimitiveTopology(const RHI_PrimitiveTopology_Mode primitive_topology)
	{
        LOG_WARNING("Part of pipeline, ignored.");
	}

	void RHI_CommandList::SetInputLayout(const RHI_InputLayout* input_layout)
	{
        LOG_WARNING("Part of pipeline, ignored.");
	}

	void RHI_CommandList::SetDepthStencilState(const RHI_DepthStencilState* depth_stencil_state)
	{
        LOG_WARNING("Part of pipeline, ignored.");
	}

	void RHI_CommandList::SetRasterizerState(const RHI_RasterizerState* rasterizer_state)
	{
        LOG_WARNING("Part of pipeline, ignored.");
	}

	void RHI_CommandList::SetBlendState(const RHI_BlendState* blend_state)
	{
        LOG_WARNING("Part of pipeline, ignored.");
	}

	void RHI_CommandList::SetBufferVertex(const RHI_VertexBuffer* buffer)
	{
        if (m_cmd_state != RHI_Cmd_List_Recording)
        {
            LOG_WARNING("Can't record command");
            return;
        }

		VkBuffer vertex_buffers[]	= { static_cast<VkBuffer>(buffer->GetResource()) };
		VkDeviceSize offsets[]		= { 0 };

		vkCmdBindVertexBuffers(
            CMD_BUFFER,     // commandBuffer
            0,              // firstBinding
            1,              // bindingCount
            vertex_buffers, // pBuffers
            offsets         // pOffsets
        );
	}

	void RHI_CommandList::SetBufferIndex(const RHI_IndexBuffer* buffer)
	{
        if (m_cmd_state != RHI_Cmd_List_Recording)
        {
            LOG_WARNING("Can't record command");
            return;
        }

		vkCmdBindIndexBuffer(
			CMD_BUFFER,                                                     // commandBuffer
			static_cast<VkBuffer>(buffer->GetResource()),					// buffer
			0,																// offset
			buffer->Is16Bit() ? VK_INDEX_TYPE_UINT16 : VK_INDEX_TYPE_UINT32 // indexType
		);
	}

	void RHI_CommandList::SetShaderVertex(const RHI_Shader* shader)
	{
        // part of pipeline
	}

	void RHI_CommandList::SetShaderPixel(const RHI_Shader* shader)
	{
        // part of pipeline
	}

    void RHI_CommandList::SetConstantBuffer(const uint32_t slot, uint8_t scope, RHI_ConstantBuffer* constant_buffer)
    {
        if (m_cmd_state != RHI_Cmd_List_Recording)
        {
            LOG_WARNING("Can't record command");
            return;
        }

        // Set
        m_pipeline->SetConstantBuffer(slot, constant_buffer);
    }

    void RHI_CommandList::SetSampler(const uint32_t slot, RHI_Sampler* sampler)
    {
        if (m_cmd_state != RHI_Cmd_List_Recording)
        {
            LOG_WARNING("Can't record command");
            return;
        }

        // Set
        m_pipeline->SetSampler(slot, sampler);
    }

    void RHI_CommandList::SetTexture(const uint32_t slot, RHI_Texture* texture)
    {
        if (m_cmd_state != RHI_Cmd_List_Recording)
        {
            LOG_WARNING("Can't record command");
            return;
        }

        // Null textures are allowed, and they are replaced with a black texture here
        if (!texture || !texture->GetResource_Texture())
        {
            texture = m_renderer->GetBlackTexture();
        }

        // Set
        m_pipeline->SetTexture(slot, texture);
    }

	void RHI_CommandList::SetRenderTargets(const void* render_targets, uint32_t render_target_count, void* depth_stencil /*= nullptr*/)
	{
        LOG_WARNING("Part of pipeline, ignored.");
	}

	void RHI_CommandList::ClearRenderTarget(void* render_target, const Vector4& color)
	{
        // https://www.khronos.org/registry/vulkan/specs/1.1-extensions/html/vkspec.html#clears-outside
        //if (m_cmd_state != RHI_Cmd_List_Idle)
        //{
        //    LOG_WARNING("Explicit clearing can only happen outside of a render pass instance");
        //    return;
        //}

        //VkImageSubresourceRange subResourceRange    = {};
        //subResourceRange.aspectMask                 = VK_IMAGE_ASPECT_COLOR_BIT;
        //subResourceRange.baseMipLevel               = 0;
        //subResourceRange.levelCount                 = 1;
        //subResourceRange.baseArrayLayer             = 0;
        //subResourceRange.layerCount                 = 1;

        //VkImageMemoryBarrier presentToClearBarrier  = {};
        //presentToClearBarrier.sType                 = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        //presentToClearBarrier.srcAccessMask         = VK_ACCESS_MEMORY_READ_BIT;
        //presentToClearBarrier.dstAccessMask         = VK_ACCESS_TRANSFER_WRITE_BIT;
        //presentToClearBarrier.oldLayout             = VK_IMAGE_LAYOUT_UNDEFINED;
        //presentToClearBarrier.newLayout             = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        //presentToClearBarrier.srcQueueFamilyIndex   = presentQueueFamily;
        //presentToClearBarrier.dstQueueFamilyIndex   = presentQueueFamily;
        //presentToClearBarrier.image                 = swapChainImages[i];
        //presentToClearBarrier.subresourceRange      = subResourceRange;

        //// Change layout of image to be optimal for presenting
        //VkImageMemoryBarrier clearToPresentBarrier  = {};
        //clearToPresentBarrier.sType                 = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        //clearToPresentBarrier.srcAccessMask         = VK_ACCESS_TRANSFER_WRITE_BIT;
        //clearToPresentBarrier.dstAccessMask         = VK_ACCESS_MEMORY_READ_BIT;
        //clearToPresentBarrier.oldLayout             = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        //clearToPresentBarrier.newLayout             = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        //clearToPresentBarrier.srcQueueFamilyIndex   = presentQueueFamily;
        //clearToPresentBarrier.dstQueueFamilyIndex   = presentQueueFamily;
        //clearToPresentBarrier.image                 = swapChainImages[i];
        //clearToPresentBarrier.subresourceRange      = subResourceRange;

        //vkCmdPipelineBarrier(CMD_BUFFER, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &presentToClearBarrier);
        //VkClearColorValue clear_color = { { color.x, color.x, color.y, color.w } };
        //vkCmdClearColorImage(CMD_BUFFER, static_cast<VkImage>(render_target), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &clear_color, 1, &subResourceRange);
        //vkCmdPipelineBarrier(CMD_BUFFER, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, 0, nullptr, 0, nullptr, 1, &clearToPresentBarrier);
	}

    void RHI_CommandList::ClearDepthStencil(void* depth_stencil, const uint32_t flags, const float depth, const uint8_t stencil /*= 0*/)
    {
        // https://www.khronos.org/registry/vulkan/specs/1.1-extensions/html/vkspec.html#clears-outside
        //if (m_cmd_state != RHI_Cmd_List_Idle)
        //{
        //    LOG_WARNING("Explicit clearing can only happen outside of a render pass instance");
        //    return;
        //}

        // vkCmdClearDepthStencilImage
	}

	bool RHI_CommandList::Submit()
	{
        if (m_cmd_state != RHI_Cmd_List_Ended)
        {
            LOG_ERROR("RHI_CommandList::End() must be called before calling RHI_CommandList::Submit()");
            return false;
        }

		// Wait
		VkSemaphore wait_semaphores[] = { nullptr };
        if (m_pipeline_state.render_target_swapchain)
        {
            wait_semaphores[0] = static_cast<VkSemaphore>(m_pipeline_state.render_target_swapchain->GetSemaphoreImageAcquired());
        }
        VkPipelineStageFlags wait_flags[] = { VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT };

        // Signal
		VkSemaphore signal_semaphores[]		= { nullptr };
		
		VkSubmitInfo submit_info			= {};
		submit_info.sType					= VK_STRUCTURE_TYPE_SUBMIT_INFO;
		submit_info.waitSemaphoreCount		= m_pipeline_state.render_target_swapchain ? 1 : 0;
		submit_info.pWaitSemaphores			= wait_semaphores;
		submit_info.pWaitDstStageMask		= wait_flags;
		submit_info.commandBufferCount		= 1;
		submit_info.pCommandBuffers			= reinterpret_cast<VkCommandBuffer*>(&m_cmd_buffer);
		submit_info.signalSemaphoreCount	= 0;
		submit_info.pSignalSemaphores		= signal_semaphores;

        if (!vulkan_common::error::check_result(vkQueueSubmit(m_rhi_device->GetContextRhi()->queue_graphics, 1, &submit_info, reinterpret_cast<VkFence>(m_cmd_list_consumed_fence))))
            return false;
		
		// Wait for fence on the next Begin(), if we force it now, perfomance will not be as good
        m_cmd_state = RHI_Cmd_List_Idle_Sync_Cpu_To_Gpu;

        return true;
	}

    void RHI_CommandList::Flush()
    {
        vulkan_common::fence::wait_reset(m_rhi_device->GetContextRhi(), m_cmd_list_consumed_fence);
    }
}
#endif
