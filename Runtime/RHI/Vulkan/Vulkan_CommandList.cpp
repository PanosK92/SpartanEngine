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

#define CMD_LIST							reinterpret_cast<VkCommandBuffer>(m_cmd_buffers[m_buffer_index])
#define CMD_LIST_PTR						reinterpret_cast<VkCommandBuffer*>(&m_cmd_buffers[m_buffer_index])
#define FENCE_CMD_LIST_CONSUMED_VOID_PTR	m_fences_in_flight[m_buffer_index]
#define FENCE_CMD_LIST_CONSUMED				reinterpret_cast<VkFence>(m_fences_in_flight[m_buffer_index])
#define SEMAPHORE_CMD_LIST_CONSUMED			reinterpret_cast<VkSemaphore>(m_semaphores_render_finished[m_buffer_index])

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

		for (uint32_t i = 0; i < m_rhi_device->GetContext()->max_frames_in_flight; i++)
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

		const auto cmd_pool_vk = static_cast<VkCommandPool>(m_cmd_pool);
		for (uint32_t i = 0; i < m_rhi_device->GetContext()->max_frames_in_flight; i++)
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

	void RHI_CommandList::Begin(const string& pass_name, RHI_Pipeline* pipeline)
	{
		// Ensure the command list is not recording
		if (m_is_recording)
			return;

		// Sync CPU to GPU
		if (m_sync_cpu_to_gpu)
		{
			Vulkan_Common::fence::wait_reset(m_rhi_device, FENCE_CMD_LIST_CONSUMED_VOID_PTR);
			SPARTAN_ASSERT(vkResetCommandPool(m_rhi_device->GetContext()->device, static_cast<VkCommandPool>(m_cmd_pool), 0) == VK_SUCCESS);
			m_pipeline->OnCommandListConsumed();
			m_buffer_index = (m_buffer_index + 1) % m_pipeline->GetState()->swap_chain->GetBufferCount();
			m_sync_cpu_to_gpu = false;
		}

		// If this is pipeline contains a new swap chain, update the current buffer index
		auto swap_chain = pipeline->GetState()->swap_chain;
		if (m_pipeline && m_pipeline->GetState()->swap_chain->GetId() != swap_chain->GetId())
		{
			m_buffer_index = swap_chain->GetImageIndex();
		}

		// Keep pipeline reference
		m_pipeline = pipeline;

		// Begin command buffer
		VkCommandBufferBeginInfo beginInfo	= {};
		beginInfo.sType						= VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
		beginInfo.flags						= VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT;
		const auto result = vkBeginCommandBuffer(CMD_LIST, &beginInfo);
		if (result != VK_SUCCESS) 
		{
			LOGF_ERROR("Failed to begin recording command buffer, %s.", Vulkan_Common::result_to_string(result));
			return;
		}

		// Begin render pass
		VkClearValue clear_color					= { 0.0f, 0.0f, 0.0f, 1.0f };
		VkRenderPassBeginInfo render_pass_info		= {};
		render_pass_info.sType						= VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
		render_pass_info.renderPass					= static_cast<VkRenderPass>(swap_chain->GetRenderPass());
		render_pass_info.framebuffer				= static_cast<VkFramebuffer>(swap_chain->GetFrameBuffer());
		render_pass_info.renderArea.offset			= { 0, 0 };
		render_pass_info.renderArea.extent.width	= static_cast<uint32_t>(swap_chain->GetWidth());
		render_pass_info.renderArea.extent.height	= static_cast<uint32_t>(swap_chain->GetHeight());
		render_pass_info.clearValueCount			= 1;
		render_pass_info.pClearValues				= &clear_color;
		vkCmdBeginRenderPass(CMD_LIST, &render_pass_info, VK_SUBPASS_CONTENTS_INLINE);

		// Bind pipeline
		vkCmdBindPipeline(CMD_LIST, VK_PIPELINE_BIND_POINT_GRAPHICS, static_cast<VkPipeline>(pipeline->GetPipeline()));

		m_is_recording = true;
	}

	void RHI_CommandList::End()
	{
		SPARTAN_ASSERT(m_is_recording);

		vkCmdEndRenderPass(CMD_LIST);

		const auto result = vkEndCommandBuffer(CMD_LIST);
		if (result != VK_SUCCESS)
		{
			LOGF_ERROR("Failed to end command buffer, %s.", Vulkan_Common::result_to_string(result));
			return;
		}

		m_is_recording = false;
	}

	void RHI_CommandList::Draw(const uint32_t vertex_count)
	{
		SPARTAN_ASSERT(m_is_recording);

		vkCmdDraw(CMD_LIST, vertex_count, 1, 0, 0);
	}

	void RHI_CommandList::DrawIndexed(const uint32_t index_count, const uint32_t index_offset, const uint32_t vertex_offset)
	{
		SPARTAN_ASSERT(m_is_recording);

		vkCmdDrawIndexed(CMD_LIST, index_count, 1, index_offset, vertex_offset, 0);
	}

	void RHI_CommandList::SetViewport(const RHI_Viewport& viewport)
	{
		SPARTAN_ASSERT(m_is_recording);

		VkViewport vk_viewport	= {};
		vk_viewport.x			= viewport.x;
		vk_viewport.y			= viewport.y;
		vk_viewport.width		= viewport.width;
		vk_viewport.height		= viewport.height;
		vk_viewport.minDepth	= viewport.depth_min;
		vk_viewport.maxDepth	= viewport.depth_max;
		vkCmdSetViewport(CMD_LIST, 0, 1, &vk_viewport);
	}

	void RHI_CommandList::SetScissorRectangle(const Math::Rectangle& scissor_rectangle)
	{
		SPARTAN_ASSERT(m_is_recording);

		VkRect2D vk_scissor;
		vk_scissor.offset.x			= static_cast<int32_t>(scissor_rectangle.x);
		vk_scissor.offset.y			= static_cast<int32_t>(scissor_rectangle.y);
		vk_scissor.extent.width		= static_cast<uint32_t>(scissor_rectangle.width);
		vk_scissor.extent.height	= static_cast<uint32_t>(scissor_rectangle.height);
		vkCmdSetScissor(CMD_LIST, 0, 1, &vk_scissor);
	}

	void RHI_CommandList::SetPrimitiveTopology(const RHI_PrimitiveTopology_Mode primitive_topology)
	{
		SPARTAN_ASSERT(m_is_recording);
	}

	void RHI_CommandList::SetInputLayout(const RHI_InputLayout* input_layout)
	{
		SPARTAN_ASSERT(m_is_recording);
	}

	void RHI_CommandList::SetDepthStencilState(const RHI_DepthStencilState* depth_stencil_state)
	{
		SPARTAN_ASSERT(m_is_recording);
	}

	void RHI_CommandList::SetRasterizerState(const RHI_RasterizerState* rasterizer_state)
	{
		SPARTAN_ASSERT(m_is_recording);
	}

	void RHI_CommandList::SetBlendState(const RHI_BlendState* blend_state)
	{
		SPARTAN_ASSERT(m_is_recording);
	}

	void RHI_CommandList::SetBufferVertex(const RHI_VertexBuffer* buffer)
	{
		SPARTAN_ASSERT(m_is_recording);

		VkBuffer vertex_buffers[]	= { static_cast<VkBuffer>(buffer->GetResource()) };
		VkDeviceSize offsets[]		= { 0 };
		vkCmdBindVertexBuffers(CMD_LIST, 0, 1, vertex_buffers, offsets);
	}

	void RHI_CommandList::SetBufferIndex(const RHI_IndexBuffer* buffer)
	{
		SPARTAN_ASSERT(m_is_recording);

		vkCmdBindIndexBuffer(
			CMD_LIST,
			static_cast<VkBuffer>(buffer->GetResource()),					// buffer
			0,																// offset
			buffer->Is16Bit() ? VK_INDEX_TYPE_UINT16 : VK_INDEX_TYPE_UINT32 // index type
		);
	}

	void RHI_CommandList::SetShaderVertex(const RHI_Shader* shader)
	{
		SPARTAN_ASSERT(m_is_recording);
	}

	void RHI_CommandList::SetShaderPixel(const RHI_Shader* shader)
	{
		SPARTAN_ASSERT(m_is_recording);
	}

	void RHI_CommandList::SetConstantBuffers(const uint32_t start_slot, const RHI_Buffer_Scope scope, const vector<void*>& constant_buffers)
	{
		SPARTAN_ASSERT(m_is_recording);
	}

	void RHI_CommandList::SetConstantBuffer(const uint32_t slot, const RHI_Buffer_Scope scope, const shared_ptr<RHI_ConstantBuffer>& constant_buffer)
	{
		SPARTAN_ASSERT(m_is_recording);
	}

	void RHI_CommandList::SetSamplers(const uint32_t start_slot, const vector<void*>& samplers)
	{
		SPARTAN_ASSERT(m_is_recording);
	}

	void RHI_CommandList::SetSampler(const uint32_t slot, const shared_ptr<RHI_Sampler>& sampler)
	{
		SPARTAN_ASSERT(m_is_recording);
	}

	void RHI_CommandList::SetTextures(const uint32_t start_slot, const void* textures, uint32_t texture_count, bool is_array)
	{
		SPARTAN_ASSERT(m_is_recording);
	}

	void RHI_CommandList::SetTexture(const uint32_t slot, RHI_Texture* texture)
	{
		SPARTAN_ASSERT(m_is_recording);

		if (!texture)
			return;

		m_pipeline->UpdateDescriptorSets(texture);
		if (const auto descriptor_set = m_pipeline->GetDescriptorSet(texture->GetId()))
		{
			VkDescriptorSet descriptor_sets[1] = { static_cast<VkDescriptorSet>(descriptor_set) };
			vkCmdBindDescriptorSets(CMD_LIST, VK_PIPELINE_BIND_POINT_GRAPHICS, static_cast<VkPipelineLayout>(m_pipeline->GetPipelineLayout()), 0, 1, descriptor_sets, 0, nullptr);
		}
	}

	void RHI_CommandList::SetRenderTargets(const vector<void*>& render_targets, void* depth_stencil /*= nullptr*/)
	{
		SPARTAN_ASSERT(m_is_recording);
	}

	void RHI_CommandList::SetRenderTarget(void* render_target, void* depth_stencil /*= nullptr*/)
	{
		SPARTAN_ASSERT(m_is_recording);
	}

	void RHI_CommandList::SetRenderTarget(const shared_ptr<RHI_Texture>& render_target, void* depth_stencil /*= nullptr*/)
	{
		SPARTAN_ASSERT(m_is_recording);
	}

	void RHI_CommandList::ClearRenderTarget(void* render_target, const Vector4& color)
	{
		SPARTAN_ASSERT(m_is_recording);
	}

	void RHI_CommandList::ClearDepthStencil(void* depth_stencil, const uint32_t flags, const float depth, const uint32_t stencil /*= 0*/)
	{
		SPARTAN_ASSERT(m_is_recording);
	}

	bool RHI_CommandList::Submit()
	{
		auto swap_chain = m_pipeline->GetState()->swap_chain;

		// Ensure the command list has stopped recording
		SPARTAN_ASSERT(!m_is_recording);
		// Acquire next swap chain image
		SPARTAN_ASSERT(swap_chain->AcquireNextImage());
		// Ensure that the swap chain buffer index is what the command list thinks it is
		SPARTAN_ASSERT(m_buffer_index == swap_chain->GetImageIndex());

		// Prepare semaphores
		VkSemaphore wait_semaphores[]		= { static_cast<VkSemaphore>(swap_chain->GetSemaphoreImageAcquired()) };
		VkSemaphore signal_semaphores[]		= { SEMAPHORE_CMD_LIST_CONSUMED };
		VkPipelineStageFlags wait_flags[]	= { VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT };

		VkSubmitInfo submit_info			= {};
		submit_info.sType					= VK_STRUCTURE_TYPE_SUBMIT_INFO;
		submit_info.waitSemaphoreCount		= 1;
		submit_info.pWaitSemaphores			= wait_semaphores;
		submit_info.pWaitDstStageMask		= wait_flags;
		submit_info.commandBufferCount		= 1;
		submit_info.pCommandBuffers			= CMD_LIST_PTR;
		submit_info.signalSemaphoreCount	= 1;
		submit_info.pSignalSemaphores		= signal_semaphores;

		const auto result = vkQueueSubmit(m_rhi_device->GetContext()->queue_graphics, 1, &submit_info, FENCE_CMD_LIST_CONSUMED);
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
}

#endif