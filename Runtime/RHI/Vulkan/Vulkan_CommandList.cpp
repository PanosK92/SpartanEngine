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

#define CMD_BUFFER reinterpret_cast<VkCommandBuffer>(m_cmd_buffer)

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
        auto cmd_buffer_vk = static_cast<VkCommandBuffer>(m_cmd_buffer);
        if (Vulkan_Common::command::create_buffer(rhi_context, reinterpret_cast<VkCommandPool>(m_swap_chain->GetCmdPool()), cmd_buffer_vk, VK_COMMAND_BUFFER_LEVEL_PRIMARY))
        {
            m_cmd_buffer = static_cast<void*>(cmd_buffer_vk);
        }

        // Fence
        VkFence fence;
        Vulkan_Common::fence::create(rhi_context, fence);
        m_cmd_list_consumed_fence = static_cast<void*>(fence);
	}

	RHI_CommandList::~RHI_CommandList()
	{
        RHI_Context* rhi_context = m_rhi_device->GetContextRhi();

		// Wait in case the command buffer is still in use by the graphics queue
		vkQueueWaitIdle(rhi_context->queue_graphics);

		// Fence
        VkFence fence = reinterpret_cast<VkFence>(m_cmd_list_consumed_fence);
        Vulkan_Common::fence::destroy(rhi_context, fence);
        m_cmd_list_consumed_fence = nullptr;

        // Command buffer
        vkFreeCommandBuffers(m_rhi_device->GetContextRhi()->device, static_cast<VkCommandPool>(m_swap_chain->GetCmdPool()), 1, reinterpret_cast<VkCommandBuffer*>(&m_cmd_buffer));
	}

	void RHI_CommandList::Begin(const string& pass_name)
	{
        RHI_Context* rhi_context = m_rhi_device->GetContextRhi();

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
            return;
        }

        bool has_pipeline = !m_pipeline_state.shader_vertex;
        if (has_pipeline)
            return;

        m_pipeline = m_rhi_pipeline_cache->GetPipeline(m_pipeline_state).get();
	
		// Acquire next swap chain image and update buffer index
        if (!m_swap_chain->AcquireNextImage())
        {
            LOG_ERROR("Failed to acquire next swap chain image");
            return;
        }

		// Begin command buffer
		VkCommandBufferBeginInfo begin_info	    = {};
		begin_info.sType						= VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
		begin_info.flags						= VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT;
		if (!Vulkan_Common::error::check_result(vkBeginCommandBuffer(CMD_BUFFER, &begin_info)))
			return;

		// Begin render pass
		VkClearValue clear_color					= { 0.0f, 0.0f, 0.0f, 1.0f };
		VkRenderPassBeginInfo render_pass_info		= {};
		render_pass_info.sType						= VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
		render_pass_info.renderPass					= static_cast<VkRenderPass>(m_swap_chain->GetRenderPass());
		render_pass_info.framebuffer				= static_cast<VkFramebuffer>(m_swap_chain->GetFrameBuffer());
		render_pass_info.renderArea.offset			= { 0, 0 };
		render_pass_info.renderArea.extent.width	= static_cast<uint32_t>(m_swap_chain->GetWidth());
		render_pass_info.renderArea.extent.height	= static_cast<uint32_t>(m_swap_chain->GetHeight());
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
            return;
        }

        // At this point, it's safe to allow for command recording
        m_cmd_state = RHI_Cmd_List_Recording;

        // Debug marker - Begin
        #ifdef DEBUG
        Vulkan_Common::debug_marker::begin(CMD_BUFFER, pass_name.c_str(), Vector4::One);
        #endif
	}

	void RHI_CommandList::End()
	{
        if (m_cmd_state != RHI_Cmd_List_Recording)
        {
            LOG_ERROR("Can't record command");
            return;
        }

		vkCmdEndRenderPass(CMD_BUFFER);

        if (Vulkan_Common::error::check_result(vkEndCommandBuffer(CMD_BUFFER)))
        {
            m_cmd_state = RHI_Cmd_List_Ended;
        }

        // Debug marker - End
        #ifdef DEBUG
        Vulkan_Common::debug_marker::end(CMD_BUFFER);
        #endif
	}

	void RHI_CommandList::Draw(const uint32_t vertex_count)
	{
        if (m_cmd_state != RHI_Cmd_List_Recording)
        {
            LOG_ERROR("Can't record command");
            return;
        }

        // Update descriptor set (if needed)
        if (void* descriptor = m_pipeline->GetDescriptorPendingUpdate())
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
            LOG_ERROR("Can't record command");
            return;
        }

        // Update descriptor set (if needed)
        if (void* descriptor = m_pipeline->GetDescriptorPendingUpdate())
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
            LOG_ERROR("Can't record command");
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
            LOG_ERROR("Can't record command");
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
        // part of pipeline
	}

	void RHI_CommandList::SetInputLayout(const RHI_InputLayout* input_layout)
	{
        // part of pipeline
	}

	void RHI_CommandList::SetDepthStencilState(const RHI_DepthStencilState* depth_stencil_state)
	{
        // part of pipeline
	}

	void RHI_CommandList::SetRasterizerState(const RHI_RasterizerState* rasterizer_state)
	{
        // part of pipeline
	}

	void RHI_CommandList::SetBlendState(const RHI_BlendState* blend_state)
	{
        // part of pipeline
	}

	void RHI_CommandList::SetBufferVertex(const RHI_VertexBuffer* buffer)
	{
        if (m_cmd_state != RHI_Cmd_List_Recording)
        {
            LOG_ERROR("Can't record command");
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
            LOG_ERROR("Can't record command");
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
            LOG_ERROR("Can't record command");
            return;
        }

        // Set
        m_pipeline->SetConstantBuffer(slot, constant_buffer);
    }

    void RHI_CommandList::SetSampler(const uint32_t slot, RHI_Sampler* sampler)
    {
        if (m_cmd_state != RHI_Cmd_List_Recording)
        {
            LOG_ERROR("Can't record command");
            return;
        }

        // Set
        m_pipeline->SetSampler(slot, sampler);
    }

    void RHI_CommandList::SetTexture(const uint32_t slot, RHI_Texture* texture)
    {
        if (m_cmd_state != RHI_Cmd_List_Recording)
        {
            LOG_ERROR("Can't record command");
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
        // part of pipeline
	}

	void RHI_CommandList::ClearRenderTarget(void* render_target, const Vector4& color)
	{
        // part of pipeline
	}

    void RHI_CommandList::ClearDepthStencil(void* depth_stencil, const uint32_t flags, const float depth, const uint8_t stencil /*= 0*/)
    {
        // part of pipeline
	}

	bool RHI_CommandList::Submit(bool profile /*= true*/)
	{
        if (m_cmd_state != RHI_Cmd_List_Ended)
        {
            LOG_ERROR("RHI_CommandList::End() must be called before calling RHI_CommandList::Submit()");
            return false;
        }

		// Prepare semaphores
		VkSemaphore wait_semaphores[]		= { static_cast<VkSemaphore>(m_swap_chain->GetSemaphoreImageAcquired()) };
		VkSemaphore signal_semaphores[]		= { nullptr };
		VkPipelineStageFlags wait_flags[]	= { VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT };

		VkSubmitInfo submit_info			= {};
		submit_info.sType					= VK_STRUCTURE_TYPE_SUBMIT_INFO;
		submit_info.waitSemaphoreCount		= 1;
		submit_info.pWaitSemaphores			= wait_semaphores;
		submit_info.pWaitDstStageMask		= wait_flags;
		submit_info.commandBufferCount		= 1;
		submit_info.pCommandBuffers			= reinterpret_cast<VkCommandBuffer*>(&m_cmd_buffer);
		submit_info.signalSemaphoreCount	= 0;
		submit_info.pSignalSemaphores		= signal_semaphores;

        if (!Vulkan_Common::error::check_result(vkQueueSubmit(m_rhi_device->GetContextRhi()->queue_compute, 1, &submit_info, reinterpret_cast<VkFence>(m_cmd_list_consumed_fence))))
            return false;
		
		// Wait for fence on the next Begin(), if we force it now, perfomance will not be as good
        m_cmd_state = RHI_Cmd_List_Idle_Sync_Cpu_To_Gpu;

        return true;
	}

    void RHI_CommandList::Flush()
    {
        VkFence fence = static_cast<VkFence>(m_cmd_list_consumed_fence);
        Vulkan_Common::fence::wait_reset(m_rhi_device->GetContextRhi(), fence);
    }

	RHI_Command& RHI_CommandList::GetCmd()
	{
		return m_empty_cmd;
	}

	void RHI_CommandList::Clear()
	{

	}
}
#endif
