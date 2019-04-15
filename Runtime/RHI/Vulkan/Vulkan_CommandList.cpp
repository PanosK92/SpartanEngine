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
#include "../RHI_RenderTexture.h"
#include "../RHI_ConstantBuffer.h"
#include "../../Profiling/Profiler.h"
#include "../../Logging/Log.h"
#include "Vulkan_Helper.h"
//===================================

//= NAMESPACES ================
using namespace std;
using namespace Spartan::Math;
//=============================

namespace Spartan
{
	RHI_CommandList::RHI_CommandList(RHI_Device* rhi_device, Profiler* profiler)
	{
		m_rhi_device	= rhi_device;
		m_profiler		= profiler;

		if (!vulkan_helper::command_list::create_command_pool(m_rhi_device->GetContext(), &m_cmd_pool))
		{
			LOG_ERROR("Failed to create command pool.");
			return;
		}

		if (!vulkan_helper::command_list::create_command_buffer(m_rhi_device->GetContext(), &m_cmd_buffer, m_cmd_pool, VK_COMMAND_BUFFER_LEVEL_PRIMARY))
		{
			LOG_ERROR("Failed to create command buffer.");
			return;
		}

		m_semaphore_execution_complete = vulkan_helper::semaphore::create(rhi_device);
		m_fence = vulkan_helper::fence::create(rhi_device);
	}

	RHI_CommandList::~RHI_CommandList()
	{
		vulkan_helper::fence::destroy(m_rhi_device, m_fence);
		vulkan_helper::semaphore::destroy(m_rhi_device, m_semaphore_execution_complete);
		vkFreeCommandBuffers(m_rhi_device->GetContext()->device, m_cmd_pool, 1, &m_cmd_buffer);
		vkDestroyCommandPool(m_rhi_device->GetContext()->device, m_cmd_pool, nullptr);
	}

	void RHI_CommandList::Begin(const string& pass_name, void* render_pass, RHI_SwapChain* swap_chain)
	{
		if (!render_pass || !swap_chain)
		{
			LOG_ERROR_INVALID_PARAMETER();
			return;
		}

		// Wait for fence
		if (m_state == CommandList_Submission_Succeeded)
		{
			vulkan_helper::fence::wait(m_rhi_device, m_fence);
			vulkan_helper::fence::reset(m_rhi_device, m_fence);
			m_state = CommandList_Idle;
		}

		// Only move ahead if we are at an idle state
		if (m_state != CommandList_Idle)
			return;

		// Acquire next swap chain image
		m_swap_chain = swap_chain;
		if (!m_swap_chain->AcquireNextImage())
			return;

		// Begin command buffer
		VkCommandBufferBeginInfo beginInfo	= {};
		beginInfo.sType						= VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
		beginInfo.flags						= VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT;
		auto result = vkBeginCommandBuffer(m_cmd_buffer, &beginInfo);
		if (result != VK_SUCCESS) 
		{
			LOGF_ERROR("Failed to begin recording command buffer, %s.", vulkan_helper::vk_result_to_string(result));
			return;
		}

		// Begin render pass
		VkClearValue clear_color					= { 1.0f, 0.0f, 0.0f, 1.0f };
		VkRenderPassBeginInfo render_pass_info		= {};
		render_pass_info.sType						= VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
		render_pass_info.renderPass					= static_cast<VkRenderPass>(render_pass);
		render_pass_info.framebuffer				= static_cast<VkFramebuffer>(swap_chain->GetFrameBuffer());
		render_pass_info.renderArea.offset			= { 0, 0 };
		render_pass_info.renderArea.extent.width	= static_cast<uint32_t>(swap_chain->GetWidth());
		render_pass_info.renderArea.extent.height	= static_cast<uint32_t>(swap_chain->GetHeight());
		render_pass_info.clearValueCount			= 1;
		render_pass_info.pClearValues				= &clear_color;
		vkCmdBeginRenderPass(m_cmd_buffer, &render_pass_info, VK_SUBPASS_CONTENTS_INLINE);

		m_state = CommandList_Ready;
	}

	void RHI_CommandList::End()
	{
		if (m_state != CommandList_Ready)
			return;

		vkCmdEndRenderPass(m_cmd_buffer);

		auto result = vkEndCommandBuffer(m_cmd_buffer);
		if (result != VK_SUCCESS)
		{
			LOGF_ERROR("Failed to end command buffer, %s.", vulkan_helper::vk_result_to_string(result));
			return;
		}

		m_state = CommandList_Ended;
	}

	void RHI_CommandList::Draw(unsigned int vertex_count)
	{
		if (m_state != CommandList_Ready)
			return;

		vkCmdDraw(m_cmd_buffer, vertex_count, 1, 0, 0);
	}

	void RHI_CommandList::DrawIndexed(unsigned int index_count, unsigned int index_offset, unsigned int vertex_offset)
	{
		if (m_state != CommandList_Ready)
			return;

		vkCmdDrawIndexed(m_cmd_buffer, index_count, 1, index_offset, vertex_offset, 0);
	}

	void RHI_CommandList::SetPipeline(const RHI_Pipeline* pipeline)
	{
		if (m_state != CommandList_Ready)
			return;

		vkCmdBindPipeline(m_cmd_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, static_cast<VkPipeline>(pipeline->GetPipeline()));

		auto descriptor_set = static_cast<VkDescriptorSet>(pipeline->GetDescriptorSet());
		vkCmdBindDescriptorSets
		(
			m_cmd_buffer,
			VK_PIPELINE_BIND_POINT_GRAPHICS,
			static_cast<VkPipelineLayout>(pipeline->GetPipelineLayout()), 
			0, 
			1, 
			&descriptor_set, 
			0,
			nullptr
		);
	}

	void RHI_CommandList::SetViewport(const RHI_Viewport& viewport)
	{
		if (m_state != CommandList_Ready)
			return;

		VkViewport vk_viewport	= {};
		vk_viewport.x			= viewport.GetX();
		vk_viewport.y			= viewport.GetY();
		vk_viewport.width		= viewport.GetWidth();
		vk_viewport.height		= viewport.GetHeight();
		vk_viewport.minDepth	= viewport.GetMinDepth();
		vk_viewport.maxDepth	= viewport.GetMaxDepth();
		vkCmdSetViewport(m_cmd_buffer, 0, 1, &vk_viewport);
	}

	void RHI_CommandList::SetScissorRectangle(const Math::Rectangle& scissor_rectangle)
	{
		if (m_state != CommandList_Ready)
			return;

		VkRect2D vk_scissor;
		vk_scissor.offset.x			= static_cast<int32_t>(scissor_rectangle.x);
		vk_scissor.offset.y			= static_cast<int32_t>(scissor_rectangle.y);
		vk_scissor.extent.width		= static_cast<uint32_t>(scissor_rectangle.width * 0.5f);
		vk_scissor.extent.height	= static_cast<uint32_t>(scissor_rectangle.height * 0.5f);
		vkCmdSetScissor(m_cmd_buffer, 0, 1, &vk_scissor);
	}

	void RHI_CommandList::SetPrimitiveTopology(RHI_PrimitiveTopology_Mode primitive_topology)
	{
		if (m_state != CommandList_Ready)
			return;
	}

	void RHI_CommandList::SetInputLayout(const RHI_InputLayout* input_layout)
	{
		if (m_state != CommandList_Ready)
			return;
	}

	void RHI_CommandList::SetDepthStencilState(const RHI_DepthStencilState* depth_stencil_state)
	{
		if (m_state != CommandList_Ready)
			return;
	}

	void RHI_CommandList::SetRasterizerState(const RHI_RasterizerState* rasterizer_state)
	{
		if (m_state != CommandList_Ready)
			return;
	}

	void RHI_CommandList::SetBlendState(const RHI_BlendState* blend_state)
	{
		if (m_state != CommandList_Ready)
			return;
	}

	void RHI_CommandList::SetBufferVertex(const RHI_VertexBuffer* buffer)
	{
		if (m_state != CommandList_Ready)
			return;

		auto vk_buffer		= static_cast<VkBuffer>(buffer->GetBuffer());
		auto vk_device_size = buffer->GetDeviceSize();
		vkCmdBindVertexBuffers(m_cmd_buffer, 0, 1, &vk_buffer, &vk_device_size);
	}

	void RHI_CommandList::SetBufferIndex(const RHI_IndexBuffer* buffer)
	{
		if (m_state != CommandList_Ready)
			return;

		vkCmdBindIndexBuffer(m_cmd_buffer, static_cast<VkBuffer>(buffer->GetBuffer()), 0, VK_INDEX_TYPE_UINT32);
	}

	void RHI_CommandList::SetShaderVertex(const RHI_Shader* shader)
	{
		if (m_state != CommandList_Ready)
			return;
	}

	void RHI_CommandList::SetShaderPixel(const RHI_Shader* shader)
	{
		if (m_state != CommandList_Ready)
			return;
	}

	void RHI_CommandList::SetConstantBuffers(unsigned int start_slot, RHI_Buffer_Scope scope, const vector<void*>& constant_buffers)
	{
		if (m_state != CommandList_Ready)
			return;
	}

	void RHI_CommandList::SetConstantBuffer(unsigned int start_slot, RHI_Buffer_Scope scope, const shared_ptr<RHI_ConstantBuffer>& constant_buffer)
	{
		if (m_state != CommandList_Ready)
			return;
	}

	void RHI_CommandList::SetSamplers(unsigned int start_slot, const vector<void*>& samplers)
	{
		if (m_state != CommandList_Ready)
			return;
	}

	void RHI_CommandList::SetSampler(unsigned int start_slot, const shared_ptr<RHI_Sampler>& sampler)
	{
		if (m_state != CommandList_Ready)
			return;
	}

	void RHI_CommandList::SetTextures(unsigned int start_slot, const vector<void*>& textures)
	{
		if (m_state != CommandList_Ready)
			return;
	}

	void RHI_CommandList::SetTexture(unsigned int start_slot, void* texture)
	{
		if (m_state != CommandList_Ready)
			return;
	}

	void RHI_CommandList::SetTexture(unsigned int start_slot, const shared_ptr<RHI_Texture>& texture)
	{
		if (m_state != CommandList_Ready)
			return;

		SetTexture(start_slot, texture->GetBufferView());
	}

	void RHI_CommandList::SetTexture(unsigned int start_slot, const shared_ptr<RHI_RenderTexture>& texture)
	{
		if (m_state != CommandList_Ready)
			return;

		SetTexture(start_slot, texture->GetBufferView());
	}

	void RHI_CommandList::SetRenderTargets(const vector<void*>& render_targets, void* depth_stencil /*= nullptr*/)
	{
		if (m_state != CommandList_Ready)
			return;
	}

	void RHI_CommandList::SetRenderTarget(void* render_target, void* depth_stencil /*= nullptr*/)
	{
		if (m_state != CommandList_Ready)
			return;
	}

	void RHI_CommandList::SetRenderTarget(const shared_ptr<RHI_RenderTexture>& render_target, void* depth_stencil /*= nullptr*/)
	{
		if (m_state != CommandList_Ready)
			return;
	}

	void RHI_CommandList::ClearRenderTarget(void* render_target, const Vector4& color)
	{
		if (m_state != CommandList_Ready)
			return;
	}

	void RHI_CommandList::ClearDepthStencil(void* depth_stencil, unsigned int flags, float depth, unsigned int stencil /*= 0*/)
	{
		if (m_state != CommandList_Ready)
			return;
	}

	bool RHI_CommandList::Submit()
	{
		if (m_state != CommandList_Ended)
			return true;

		vector<VkSemaphore> wait_semaphores		= { static_cast<VkSemaphore>(m_swap_chain->GetImageAcquiredSemaphore()) };
		vector<VkSemaphore> signal_semaphores	= { static_cast<VkSemaphore>(m_semaphore_execution_complete) };
		VkPipelineStageFlags wait_flags[]		= { VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT };

		VkSubmitInfo submit_info			= {};
		submit_info.sType					= VK_STRUCTURE_TYPE_SUBMIT_INFO;
		submit_info.waitSemaphoreCount		= static_cast<uint32_t>(wait_semaphores.size());
		submit_info.pWaitSemaphores			= wait_semaphores.data();
		submit_info.pWaitDstStageMask		= wait_flags;
		submit_info.commandBufferCount		= 1;
		submit_info.pCommandBuffers			= &m_cmd_buffer;
		submit_info.signalSemaphoreCount	= static_cast<uint32_t>(signal_semaphores.size());
		submit_info.pSignalSemaphores		= signal_semaphores.data();

		auto result = vkQueueSubmit(m_rhi_device->GetContext()->queue_graphics, 1, &submit_info, static_cast<VkFence>(m_fence));
		if (result != VK_SUCCESS)
		{
			LOGF_ERROR("Failed to submit command buffer, %s.", vulkan_helper::vk_result_to_string(result));
			m_state = CommandList_Submission_Failed;
			return false;
		}

		m_state = CommandList_Submission_Succeeded;
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