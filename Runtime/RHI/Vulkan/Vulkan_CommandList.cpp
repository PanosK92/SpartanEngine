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
//===================================

//= NAMESPACES ================
using namespace std;
using namespace Spartan::Math;
//=============================

#define CMD_BUFFER							reinterpret_cast<VkCommandBuffer>(m_cmd_buffers[m_buffer_index])
#define CMD_BUFFER_PTR						reinterpret_cast<VkCommandBuffer*>(&m_cmd_buffers[m_buffer_index])
#define FENCE_CMD_BUFFER_CONSUMED_VOID_PTR	m_fences_in_flight[m_buffer_index]
#define FENCE_CMD_BUFFER_CONSUMED			reinterpret_cast<VkFence>(m_fences_in_flight[m_buffer_index])
#define SEMAPHORE_CMD_BUFFER_CONSUMED		reinterpret_cast<VkSemaphore>(m_semaphores_cmd_list_consumed[m_buffer_index])

namespace Spartan
{
	RHI_CommandList::RHI_CommandList(const shared_ptr<RHI_Device>& rhi_device, RHI_PipelineCache* rhi_pipeline_cache, Profiler* profiler)
	{
		m_rhi_device	        = rhi_device;
        m_rhi_pipeline_cache    = rhi_pipeline_cache;
		m_profiler		        = profiler;

		Vulkan_Common::commands::cmd_pool(m_rhi_device, m_cmd_pool);

		for (uint32_t i = 0; i < m_rhi_device->GetContextRhi()->max_frames_in_flight; i++)
		{
			VkCommandBuffer cmd_buffer;
			auto cmd_pool = static_cast<VkCommandPool>(m_cmd_pool);
			Vulkan_Common::commands::cmd_buffer(rhi_device, cmd_buffer, cmd_pool, VK_COMMAND_BUFFER_LEVEL_PRIMARY);
			m_cmd_buffers.push_back(static_cast<void*>(cmd_buffer));

			m_semaphores_cmd_list_consumed.emplace_back(Vulkan_Common::semaphore::create(rhi_device));
			m_fences_in_flight.emplace_back(Vulkan_Common::fence::create(rhi_device));
		}
	}

	RHI_CommandList::~RHI_CommandList()
	{
		// Wait in case the command buffer is still in use by the graphics queue
		vkQueueWaitIdle(m_rhi_device->GetContextRhi()->queue_graphics);

		const auto cmd_pool_vk = static_cast<VkCommandPool>(m_cmd_pool);
		for (uint32_t i = 0; i < m_rhi_device->GetContextRhi()->max_frames_in_flight; i++)
		{
			Vulkan_Common::fence::destroy(m_rhi_device, m_fences_in_flight[i]);
			Vulkan_Common::semaphore::destroy(m_rhi_device, m_semaphores_cmd_list_consumed[i]);
			auto cmd_buffer = static_cast<VkCommandBuffer>(m_cmd_buffers[i]);
			vkFreeCommandBuffers(m_rhi_device->GetContextRhi()->device, cmd_pool_vk, 1, &cmd_buffer);
		}
		m_cmd_buffers.clear();
		m_semaphores_cmd_list_consumed.clear();
		m_fences_in_flight.clear();

		vkDestroyCommandPool(m_rhi_device->GetContextRhi()->device, cmd_pool_vk, nullptr);
		m_cmd_pool = nullptr;
	}

	void RHI_CommandList::Begin(const string& pass_name)
	{
        // Sync CPU to GPU
        if (m_cmd_state == RHI_Cmd_List_Idle_Sync_Cpu_To_Gpu)
        {
            Vulkan_Common::fence::wait_reset(m_rhi_device, FENCE_CMD_BUFFER_CONSUMED_VOID_PTR);
            Vulkan_Common::assert_result(vkResetCommandPool(m_rhi_device->GetContextRhi()->device, static_cast<VkCommandPool>(m_cmd_pool), 0));
            m_pipeline->OnCommandListConsumed();
            m_cmd_state = RHI_Cmd_List_Idle;
        }

        if (m_cmd_state != RHI_Cmd_List_Idle)
        {
            LOG_ERROR("Previous command list is still being used");
            return;
        }

        // Temp shit
        m_pipeline = m_rhi_pipeline_cache->GetPipeline(m_pipeline_state).get();
	
		// Acquire next swap chain image and update buffer index
		SPARTAN_ASSERT(m_pipeline_state.swap_chain->AcquireNextImage());
		m_buffer_index = m_pipeline_state.swap_chain->GetImageIndex();

		// Let the swapchain know when the this command list is submitted and consumed
        m_pipeline_state.swap_chain->SetSemaphoreRenderFinished(m_semaphores_cmd_list_consumed[m_buffer_index]);

		// Begin command buffer
		VkCommandBufferBeginInfo begin_info	    = {};
		begin_info.sType						= VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
		begin_info.flags						= VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT;
		if (!Vulkan_Common::check_result(vkBeginCommandBuffer(CMD_BUFFER, &begin_info)))
			return;

		// Begin render pass
		VkClearValue clear_color					= { 0.0f, 0.0f, 0.0f, 1.0f };
		VkRenderPassBeginInfo render_pass_info		= {};
		render_pass_info.sType						= VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
		render_pass_info.renderPass					= static_cast<VkRenderPass>(m_pipeline_state.swap_chain->GetRenderPass());
		render_pass_info.framebuffer				= static_cast<VkFramebuffer>(m_pipeline_state.swap_chain->GetFrameBuffer());
		render_pass_info.renderArea.offset			= { 0, 0 };
		render_pass_info.renderArea.extent.width	= static_cast<uint32_t>(m_pipeline_state.swap_chain->GetWidth());
		render_pass_info.renderArea.extent.height	= static_cast<uint32_t>(m_pipeline_state.swap_chain->GetHeight());
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
	}

	void RHI_CommandList::End()
	{
        if (m_cmd_state != RHI_Cmd_List_Recording)
        {
            LOG_ERROR("Can't record command");
            return;
        }

		vkCmdEndRenderPass(CMD_BUFFER);

		if (!Vulkan_Common::check_result(vkEndCommandBuffer(CMD_BUFFER)))
			return;

        m_cmd_state = RHI_Cmd_List_Ended;
	}

	void RHI_CommandList::Draw(const uint32_t vertex_count)
	{
        if (m_cmd_state != RHI_Cmd_List_Recording)
        {
            LOG_ERROR("Can't record command");
            return;
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

	}

	void RHI_CommandList::SetInputLayout(const RHI_InputLayout* input_layout)
	{

	}

	void RHI_CommandList::SetDepthStencilState(const RHI_DepthStencilState* depth_stencil_state)
	{

	}

	void RHI_CommandList::SetRasterizerState(const RHI_RasterizerState* rasterizer_state)
	{

	}

	void RHI_CommandList::SetBlendState(const RHI_BlendState* blend_state)
	{

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

	}

	void RHI_CommandList::SetShaderPixel(const RHI_Shader* shader)
	{

	}

	void RHI_CommandList::SetConstantBuffers(const uint32_t start_slot, const RHI_Buffer_Scope scope, const vector<void*>& constant_buffers)
	{

	}

	void RHI_CommandList::SetConstantBuffer(const uint32_t slot, const RHI_Buffer_Scope scope, const shared_ptr<RHI_ConstantBuffer>& constant_buffer)
	{

	}

	void RHI_CommandList::SetSamplers(uint32_t start_slot, const void* samplers, uint32_t sampler_count, bool is_array /*= true*/)
	{
        /*
        // Set samplers and update descriptor set
        m_pipeline->SetSamplers(samplers, sampler_count);
        m_pipeline->UpdateDescriptorSet();

        // Bind descriptor set
        if (const auto descriptor_set = m_pipeline->GetDescriptorSet(texture->GetId()))
        {
            VkDescriptorSet descriptor_sets[1] = { static_cast<VkDescriptorSet>(descriptor_set) };
            vkCmdBindDescriptorSets(CMD_BUFFER, VK_PIPELINE_BIND_POINT_GRAPHICS, static_cast<VkPipelineLayout>(m_pipeline->GetPipelineLayout()), 0, 1, descriptor_sets, 0, nullptr);
        }
        */
	}

	void RHI_CommandList::SetTextures(const uint32_t start_slot, const void* textures, uint32_t texture_count, bool is_array /*= true*/)
	{
        /*
        if (m_cmd_state != RHI_Cmd_List_Recording)
        {
            LOG_ERROR("Can't record command");
            return;
        }

		if (!texture)
			return;

        // Set texture and update descriptor set
        m_pipeline->SetTexture(texture);
		m_pipeline->UpdateDescriptorSet();

        // Bind descriptor set
		if (const auto descriptor_set = m_pipeline->GetDescriptorSet(texture->GetId()))
		{
			VkDescriptorSet descriptor_sets[1] = { static_cast<VkDescriptorSet>(descriptor_set) };
			vkCmdBindDescriptorSets(CMD_BUFFER, VK_PIPELINE_BIND_POINT_GRAPHICS, static_cast<VkPipelineLayout>(m_pipeline->GetPipelineLayout()), 0, 1, descriptor_sets, 0, nullptr);
		}
        */
	}

	void RHI_CommandList::SetRenderTargets(const void* render_targets, uint32_t render_target_count, void* depth_stencil /*= nullptr*/, bool is_array /*= true*/)
	{

	}

	void RHI_CommandList::ClearRenderTarget(void* render_target, const Vector4& color)
	{

	}

	void RHI_CommandList::ClearDepthStencil(void* depth_stencil, const uint32_t flags, const float depth, const uint32_t stencil /*= 0*/)
	{

	}

	bool RHI_CommandList::Submit(bool profile/*=true*/)
	{
        if (m_cmd_state != RHI_Cmd_List_Ended)
        {
            LOG_ERROR("RHI_CommandList::End() must be called before calling RHI_CommandList::Submit()");
            return false;
        }

		// Ensure that the swap chain buffer index is what the command list thinks it is
		SPARTAN_ASSERT(m_buffer_index == m_pipeline_state.swap_chain->GetImageIndex() && "The command list's buffer index might not be in sync with the swap chain's image index");

		// Prepare semaphores
		VkSemaphore wait_semaphores[]		= { static_cast<VkSemaphore>(m_pipeline_state.swap_chain->GetSemaphoreImageAcquired()) };
		VkSemaphore signal_semaphores[]		= { SEMAPHORE_CMD_BUFFER_CONSUMED };
		VkPipelineStageFlags wait_flags[]	= { VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT };

		VkSubmitInfo submit_info			= {};
		submit_info.sType					= VK_STRUCTURE_TYPE_SUBMIT_INFO;
		submit_info.waitSemaphoreCount		= 1;
		submit_info.pWaitSemaphores			= wait_semaphores;
		submit_info.pWaitDstStageMask		= wait_flags;
		submit_info.commandBufferCount		= 1;
		submit_info.pCommandBuffers			= CMD_BUFFER_PTR;
		submit_info.signalSemaphoreCount	= 1;
		submit_info.pSignalSemaphores		= signal_semaphores;

        if (!Vulkan_Common::check_result(vkQueueSubmit(m_rhi_device->GetContextRhi()->queue_graphics, 1, &submit_info, FENCE_CMD_BUFFER_CONSUMED)))
            return false;
		
		// Wait for fence on the next Begin(), if we force it now, perfomance will not be as good
        m_cmd_state = RHI_Cmd_List_Idle_Sync_Cpu_To_Gpu;

        return true;
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
