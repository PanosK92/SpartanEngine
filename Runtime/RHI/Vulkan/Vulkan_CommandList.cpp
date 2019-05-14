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

#pragma once

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

#define CMD_LIST							static_cast<VkCommandBuffer>(m_cmd_buffers[m_current_frame])
#define FENCE_CMD_LIST_CONSUMED_VOID_PTR	m_fences_in_flight[m_current_frame]
#define FENCE_CMD_LIST_CONSUMED				static_cast<VkFence>(m_fences_in_flight[m_current_frame])
#define SEMAPHORE_CMD_LIST_CONSUMED			static_cast<VkSemaphore>(m_semaphores_render_finished[m_current_frame])

namespace Spartan
{
	RHI_CommandList::RHI_CommandList(const shared_ptr<RHI_Device>& rhi_device, Profiler* profiler)
	{
		m_rhi_device	= rhi_device;
		m_profiler		= profiler;

		if (!Vulkan_Common::commands::cmd_pool(m_rhi_device, m_cmd_pool))
		{
			LOG_ERROR("Failed to create command pool.");
			return;
		}

		for (unsigned int i = 0; i < m_rhi_device->GetContext()->max_frames_in_flight; i++)
		{
			VkCommandBuffer cmd_buffer;
			auto cmd_pool = static_cast<VkCommandPool>(m_cmd_pool);
			if (!Vulkan_Common::commands::cmd_buffer(rhi_device, cmd_buffer, cmd_pool, VK_COMMAND_BUFFER_LEVEL_PRIMARY))
			{
				LOG_ERROR("Failed to create command buffer.");
				return;
			}
			m_cmd_buffers.push_back(static_cast<void*>(cmd_buffer));

			m_semaphores_render_finished.emplace_back(Vulkan_Common::semaphore::create(rhi_device));
			m_fences_in_flight.emplace_back(Vulkan_Common::fence::create(rhi_device));
		}
	}

	RHI_CommandList::~RHI_CommandList()
	{
		// Wait in case the command buffer is still in use by the graphics queue
		vkQueueWaitIdle(m_rhi_device->GetContext()->queue_graphics);

		auto cmd_pool_vk = static_cast<VkCommandPool>(m_cmd_pool);
		for (unsigned int i = 0; i < m_rhi_device->GetContext()->max_frames_in_flight; i++)
		{
			Vulkan_Common::fence::destroy(m_rhi_device, m_fences_in_flight[i]);
			Vulkan_Common::semaphore::destroy(m_rhi_device, m_semaphores_render_finished[i]);
			auto cmd_buffer = static_cast<VkCommandBuffer>(m_cmd_buffers[i]);
			vkFreeCommandBuffers(m_rhi_device->GetContext()->device, cmd_pool_vk, 1, &cmd_buffer);
		}
		m_cmd_buffers.clear();
		m_semaphores_render_finished.clear();
		m_fences_in_flight.clear();

		vkDestroyCommandPool(m_rhi_device->GetContext()->device, cmd_pool_vk, nullptr);
		m_cmd_pool = nullptr;
	}

	void RHI_CommandList::Begin(const string& pass_name, void* render_pass, RHI_SwapChain* swap_chain)
	{
		if (!render_pass || !swap_chain)
		{
			LOG_ERROR_INVALID_PARAMETER();
			return;
		}
		m_swap_chain = swap_chain;

		// Ensure the command list is not recording
		if (m_is_recording)
			return;

		// Sync CPU to GPU
		if (m_sync_cpu_to_gpu)
		{
			Vulkan_Common::fence::wait_reset(m_rhi_device, FENCE_CMD_LIST_CONSUMED_VOID_PTR);
			OnCmdListConsumed();
			m_sync_cpu_to_gpu = false;
		}

		// Begin command buffer
		VkCommandBufferBeginInfo beginInfo	= {};
		beginInfo.sType						= VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
		beginInfo.flags						= VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT;
		auto result = vkBeginCommandBuffer(CMD_LIST, &beginInfo);
		if (result != VK_SUCCESS) 
		{
			LOGF_ERROR("Failed to begin recording command buffer, %s.", Vulkan_Common::result_to_string(result));
			return;
		}

		// Begin render pass
		VkClearValue clear_color					= { 0.0f, 0.0f, 0.0f, 1.0f };
		VkRenderPassBeginInfo render_pass_info		= {};
		render_pass_info.sType						= VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
		render_pass_info.renderPass					= static_cast<VkRenderPass>(render_pass);
		render_pass_info.framebuffer				= static_cast<VkFramebuffer>(swap_chain->GetFrameBuffer(m_current_frame));
		render_pass_info.renderArea.offset			= { 0, 0 };
		render_pass_info.renderArea.extent.width	= static_cast<uint32_t>(swap_chain->GetWidth());
		render_pass_info.renderArea.extent.height	= static_cast<uint32_t>(swap_chain->GetHeight());
		render_pass_info.clearValueCount			= 1;
		render_pass_info.pClearValues				= &clear_color;
		vkCmdBeginRenderPass(CMD_LIST, &render_pass_info, VK_SUBPASS_CONTENTS_INLINE);

		m_is_recording = true;
	}

	void RHI_CommandList::End()
	{
		if (!m_is_recording)
			return;

		vkCmdEndRenderPass(CMD_LIST);

		auto result = vkEndCommandBuffer(CMD_LIST);
		if (result != VK_SUCCESS)
		{
			LOGF_ERROR("Failed to end command buffer, %s.", Vulkan_Common::result_to_string(result));
			return;
		}

		m_is_recording = false;
	}

	void RHI_CommandList::Draw(unsigned int vertex_count)
	{
		if (!m_is_recording)
			return;

		vkCmdDraw(CMD_LIST, vertex_count, 1, 0, 0);
	}

	void RHI_CommandList::DrawIndexed(unsigned int index_count, unsigned int index_offset, unsigned int vertex_offset)
	{
		if (!m_is_recording)
			return;

		vkCmdDrawIndexed(CMD_LIST, index_count, 1, index_offset, vertex_offset, 0);
	}

	void RHI_CommandList::SetPipeline(RHI_Pipeline* pipeline)
	{
		if (!m_is_recording)
			return;

		m_pipeline = pipeline;
		vkCmdBindPipeline(CMD_LIST, VK_PIPELINE_BIND_POINT_GRAPHICS, static_cast<VkPipeline>(pipeline->GetPipeline()));
	}

	void RHI_CommandList::SetViewport(const RHI_Viewport& viewport)
	{
		if (!m_is_recording)
			return;

		VkViewport vk_viewport	= {};
		vk_viewport.x			= viewport.GetX();
		vk_viewport.y			= viewport.GetY();
		vk_viewport.width		= viewport.GetWidth();
		vk_viewport.height		= viewport.GetHeight();
		vk_viewport.minDepth	= viewport.GetMinDepth();
		vk_viewport.maxDepth	= viewport.GetMaxDepth();
		vkCmdSetViewport(CMD_LIST, 0, 1, &vk_viewport);
	}

	void RHI_CommandList::SetScissorRectangle(const Math::Rectangle& scissor_rectangle)
	{
		if (!m_is_recording)
			return;

		VkRect2D vk_scissor;
		vk_scissor.offset.x			= static_cast<int32_t>(scissor_rectangle.x);
		vk_scissor.offset.y			= static_cast<int32_t>(scissor_rectangle.y);
		vk_scissor.extent.width		= static_cast<uint32_t>(scissor_rectangle.width);
		vk_scissor.extent.height	= static_cast<uint32_t>(scissor_rectangle.height);
		vkCmdSetScissor(CMD_LIST, 0, 1, &vk_scissor);
	}

	void RHI_CommandList::SetPrimitiveTopology(RHI_PrimitiveTopology_Mode primitive_topology)
	{
		if (!m_is_recording)
			return;
	}

	void RHI_CommandList::SetInputLayout(const RHI_InputLayout* input_layout)
	{
		if (!m_is_recording)
			return;
	}

	void RHI_CommandList::SetDepthStencilState(const RHI_DepthStencilState* depth_stencil_state)
	{
		if (!m_is_recording)
			return;
	}

	void RHI_CommandList::SetRasterizerState(const RHI_RasterizerState* rasterizer_state)
	{
		if (!m_is_recording)
			return;
	}

	void RHI_CommandList::SetBlendState(const RHI_BlendState* blend_state)
	{
		if (!m_is_recording)
			return;
	}

	void RHI_CommandList::SetBufferVertex(const RHI_VertexBuffer* buffer)
	{
		if (!m_is_recording)
			return;

		VkBuffer vertex_buffers[]	= { static_cast<VkBuffer>(buffer->GetResource()) };
		VkDeviceSize offsets[]		= { 0 };
		vkCmdBindVertexBuffers(CMD_LIST, 0, 1, vertex_buffers, offsets);
	}

	void RHI_CommandList::SetBufferIndex(const RHI_IndexBuffer* buffer)
	{
		if (!m_is_recording)
			return;

		vkCmdBindIndexBuffer(
			CMD_LIST,
			static_cast<VkBuffer>(buffer->GetResource()),					// buffer
			0,																// offset
			buffer->Is16Bit() ? VK_INDEX_TYPE_UINT16 : VK_INDEX_TYPE_UINT32 // index type
		);
	}

	void RHI_CommandList::SetShaderVertex(const RHI_Shader* shader)
	{
		if (!m_is_recording)
			return;
	}

	void RHI_CommandList::SetShaderPixel(const RHI_Shader* shader)
	{
		if (!m_is_recording)
			return;
	}

	void RHI_CommandList::SetConstantBuffers(unsigned int start_slot, RHI_Buffer_Scope scope, const vector<void*>& constant_buffers)
	{
		if (!m_is_recording)
			return;
	}

	void RHI_CommandList::SetConstantBuffer(unsigned int slot, RHI_Buffer_Scope scope, const shared_ptr<RHI_ConstantBuffer>& constant_buffer)
	{
		if (!m_is_recording)
			return;

	}

	void RHI_CommandList::SetSamplers(unsigned int start_slot, const vector<void*>& samplers)
	{
		if (!m_is_recording)
			return;
	}

	void RHI_CommandList::SetSampler(unsigned int slot, const shared_ptr<RHI_Sampler>& sampler)
	{
		if (!m_is_recording)
			return;
	}

	void RHI_CommandList::SetTextures(uint32_t start_slot, const vector<void*>& textures)
	{
		if (!m_is_recording)
			return;

	}

	void RHI_CommandList::SetTexture(uint32_t slot, RHI_Texture* texture)
	{
		if (!m_is_recording)
			return;
		
		if (!texture)
			return;

		m_pipeline->UpdateDescriptorSets(texture);
		if (void* descriptor_set = m_pipeline->GetDescriptorSet(texture->RHI_GetID()))
		{
			VkDescriptorSet descriptor_sets[1] = { static_cast<VkDescriptorSet>(descriptor_set) };
			vkCmdBindDescriptorSets(CMD_LIST, VK_PIPELINE_BIND_POINT_GRAPHICS, static_cast<VkPipelineLayout>(m_pipeline->GetPipelineLayout()), 0, 1, descriptor_sets, 0, nullptr);
		}
	}

	void RHI_CommandList::SetRenderTargets(const vector<void*>& render_targets, void* depth_stencil /*= nullptr*/)
	{
		if (!m_is_recording)
			return;
	}

	void RHI_CommandList::SetRenderTarget(void* render_target, void* depth_stencil /*= nullptr*/)
	{
		if (!m_is_recording)
			return;
	}

	void RHI_CommandList::SetRenderTarget(const shared_ptr<RHI_Texture>& render_target, void* depth_stencil /*= nullptr*/)
	{
		if (!m_is_recording)
			return;
	}

	void RHI_CommandList::ClearRenderTarget(void* render_target, const Vector4& color)
	{
		if (!m_is_recording)
			return;
	}

	void RHI_CommandList::ClearDepthStencil(void* depth_stencil, unsigned int flags, float depth, unsigned int stencil /*= 0*/)
	{
		if (!m_is_recording)
			return;
	}

	bool RHI_CommandList::Submit()
	{
		// Ensure the command list has stopped recording
		if (m_is_recording)
			return false;

		// Acquire next swap chain image
		SPARTAN_ASSERT(m_swap_chain->AcquireNextImage());
		// Ensure the swap chain buffers are in sync with the command lists
		SPARTAN_ASSERT(m_current_frame == m_swap_chain->GetImageIndex());

		// Prepare semaphores
		VkSemaphore wait_semaphores[]		= { static_cast<VkSemaphore>(m_swap_chain->GetSemaphoreImageAcquired()) };
		VkSemaphore signal_semaphores[]		= { SEMAPHORE_CMD_LIST_CONSUMED };
		VkPipelineStageFlags wait_flags[]	= { VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT };

		VkSubmitInfo submit_info			= {};
		submit_info.sType					= VK_STRUCTURE_TYPE_SUBMIT_INFO;
		submit_info.waitSemaphoreCount		= 1;
		submit_info.pWaitSemaphores			= wait_semaphores;
		submit_info.pWaitDstStageMask		= wait_flags;
		submit_info.commandBufferCount		= 1;
		submit_info.pCommandBuffers			= &CMD_LIST;
		submit_info.signalSemaphoreCount	= 1;
		submit_info.pSignalSemaphores		= signal_semaphores;

		auto result = vkQueueSubmit(m_rhi_device->GetContext()->queue_graphics, 1, &submit_info, FENCE_CMD_LIST_CONSUMED);
		if (result != VK_SUCCESS)
		{
			LOGF_ERROR("Failed to submit command buffer, %s.", Vulkan_Common::result_to_string(result));
		}
		
		// Request syncing, don't force wait for fence right now
		m_sync_cpu_to_gpu = true;

		return result == VK_SUCCESS;
	}

	RHI_Command& RHI_CommandList::GetCmd()
	{
		return m_empty_cmd;
	}

	void RHI_CommandList::Clear()
	{

	}

	void RHI_CommandList::OnCmdListConsumed()
	{
		SPARTAN_ASSERT(vkResetCommandPool(m_rhi_device->GetContext()->device, static_cast<VkCommandPool>(m_cmd_pool), 0) == VK_SUCCESS);
		m_pipeline->OnCommandListConsumed();
		m_current_frame	= (m_current_frame + 1) % m_swap_chain->GetBufferCount();
	}
}

#endif