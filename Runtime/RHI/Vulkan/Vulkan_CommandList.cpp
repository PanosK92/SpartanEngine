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
#include "../RHI_Device.h"
#include "../RHI_Sampler.h"
#include "../RHI_Texture.h"
#include "../RHI_RenderTexture.h"
#include "../RHI_ConstantBuffer.h"
#include "../../Profiling/Profiler.h"
#include "../../Logging/Log.h"
#include "Vulkan_Helper.h"
//===================================

//= NAMESPACES ================
using namespace std;
using namespace Directus::Math;
//=============================

namespace Directus
{
	RHI_CommandList::RHI_CommandList(RHI_Device* rhi_device, Profiler* profiler)
	{
		m_rhi_device	= rhi_device;
		m_profiler		= profiler;

		/*if (!vulkan_helper::command_list::create_command_pool(m_rhi_device->GetContext(), m_cmd_pool))
		{
			LOG_ERROR("Failed to create command pool.");
			return;
		}*/

		if (!vulkan_helper::command_list::create_command_buffer(m_rhi_device->GetContext(), &m_cmd_buffer, m_cmd_pool, VK_COMMAND_BUFFER_LEVEL_PRIMARY))
		{
			LOG_ERROR("Failed to create command buffer.");
			return;
		}
	}

	RHI_CommandList::~RHI_CommandList()
	{
		vkDestroyCommandPool(m_rhi_device->GetContext()->device, m_cmd_pool, nullptr);
	}

	void RHI_CommandList::Clear()
	{

	}

	void RHI_CommandList::Begin(const string& pass_name)
	{
		VkCommandBufferBeginInfo beginInfo	= {};
		beginInfo.sType						= VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
		beginInfo.flags						= VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT;

		if (vkBeginCommandBuffer(m_cmd_buffer, &beginInfo) != VK_SUCCESS) 
		{
			LOG_ERROR("Failed to begin recording command buffer.");
		}

		/*VkRenderPassBeginInfo renderPassInfo	= {};
		renderPassInfo.sType					= VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
		renderPassInfo.renderPass				= renderPass;
		renderPassInfo.framebuffer				= swapChainFramebuffers[i];
		renderPassInfo.renderArea.offset		= { 0, 0 };
		renderPassInfo.renderArea.extent		= swapChainExtent;

		VkClearValue clearColor = { 0.0f, 0.0f, 0.0f, 1.0f };
		renderPassInfo.clearValueCount = 1;
		renderPassInfo.pClearValues = &clearColor;

		vkCmdBeginRenderPass(m_cmd_buffer, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);*/
	}

	void RHI_CommandList::End()
	{
		vkCmdEndRenderPass(m_cmd_buffer);

		if (vkEndCommandBuffer(m_cmd_buffer) != VK_SUCCESS)
		{
			LOG_ERROR("Failed to end command buffer.");
		}
	}

	void RHI_CommandList::Draw(unsigned int vertex_count)
	{
		vkCmdDraw(m_cmd_buffer, vertex_count, 1, 0, 0);
	}

	void RHI_CommandList::DrawIndexed(unsigned int index_count, unsigned int index_offset, unsigned int vertex_offset)
	{
		vkCmdDrawIndexed(m_cmd_buffer, index_count, 1, index_offset, vertex_offset, 0);
	}

	void RHI_CommandList::SetViewport(const RHI_Viewport& viewport)
	{
		VkViewport vk_viewport;
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
		VkRect2D vk_scissor;
		vk_scissor.offset.x			= static_cast<int32_t>(scissor_rectangle.x);
		vk_scissor.offset.y			= static_cast<int32_t>(scissor_rectangle.y);
		vk_scissor.extent.width		= static_cast<uint32_t>(scissor_rectangle.width * 0.5f);
		vk_scissor.extent.height	= static_cast<uint32_t>(scissor_rectangle.height * 0.5f);
		vkCmdSetScissor(m_cmd_buffer, 0, 1, &vk_scissor);
	}

	void RHI_CommandList::SetPrimitiveTopology(RHI_PrimitiveTopology_Mode primitive_topology)
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
		//vkCmdBindVertexBuffers(m_cmd_buffer, 0, 1, &models.models.vertices.buffer, offsets);
	}


	void RHI_CommandList::SetBufferIndex(const RHI_IndexBuffer* buffer)
	{
		//vkCmdBindIndexBuffer(m_cmd_buffer, models.models.indices.buffer, 0, VK_INDEX_TYPE_UINT32);
	}


	void RHI_CommandList::SetShaderVertex(const RHI_Shader* shader)
	{
		
	}

	void RHI_CommandList::SetShaderPixel(const RHI_Shader* shader)
	{

	}

	void RHI_CommandList::SetConstantBuffers(unsigned int start_slot, RHI_Buffer_Scope scope, const vector<void*>& constant_buffers)
	{
		
	}

	void RHI_CommandList::SetConstantBuffer(unsigned int start_slot, RHI_Buffer_Scope scope, const shared_ptr<RHI_ConstantBuffer>& constant_buffer)
	{
		
	}

	void RHI_CommandList::SetSamplers(unsigned int start_slot, const vector<void*>& samplers)
	{
		
	}

	void RHI_CommandList::SetSampler(unsigned int start_slot, const shared_ptr<RHI_Sampler>& sampler)
	{
		
	}

	void RHI_CommandList::SetTextures(unsigned int start_slot, const vector<void*>& textures)
	{
		
	}

	void RHI_CommandList::SetTexture(unsigned int start_slot, void* texture)
	{
		
	}

	void RHI_CommandList::SetTexture(unsigned int start_slot, const shared_ptr<RHI_Texture>& texture)
	{
		SetTexture(start_slot, texture->GetShaderResource());
	}

	void RHI_CommandList::SetTexture(unsigned int start_slot, const shared_ptr<RHI_RenderTexture>& texture)
	{
		SetTexture(start_slot, texture->GetShaderResource());
	}

	void RHI_CommandList::SetRenderTargets(const vector<void*>& render_targets, void* depth_stencil /*= nullptr*/)
	{
		
	}

	void RHI_CommandList::SetRenderTarget(void* render_target, void* depth_stencil /*= nullptr*/)
	{
	
	}

	void RHI_CommandList::SetRenderTarget(const shared_ptr<RHI_RenderTexture>& render_target, void* depth_stencil /*= nullptr*/)
	{
		
	}

	void RHI_CommandList::ClearRenderTarget(void* render_target, const Vector4& color)
	{
		
	}

	void RHI_CommandList::ClearDepthStencil(void* depth_stencil, unsigned int flags, float depth, unsigned int stencil /*= 0*/)
	{
		
	}

	void RHI_CommandList::Flush()
	{
		VkSubmitInfo submitInfo			= {};
		submitInfo.sType				= VK_STRUCTURE_TYPE_SUBMIT_INFO;
		submitInfo.commandBufferCount	= 1;
		submitInfo.pCommandBuffers		= &m_cmd_buffer;

		if (vkQueueSubmit(m_queue, 1, &submitInfo, VK_NULL_HANDLE) != VK_SUCCESS)
		{
			LOG_ERROR("Failed to submit command buffer.");
		}

		if (vkQueueWaitIdle(m_queue) != VK_SUCCESS)
		{
			LOG_ERROR("Failed to wait until idle.");
		}

		/*if (free)
		{
			vkFreeCommandBuffers(m_rhi_device->GetContext()->device, m_cmd_pool, 1, &m_cmd_buffer);
		}*/
	}

	RHI_Command& RHI_CommandList::GetCmd()
	{
		return m_empty_cmd;
	}
}

#endif