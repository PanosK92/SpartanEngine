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
#ifdef API_GRAPHICS_D3D11
//================================

//= INCLUDES ========================
#include "../RHI_CommandList.h"
#include "../RHI_Pipeline.h"
#include "../RHI_Device.h"
#include "../RHI_Sampler.h"
#include "../RHI_Texture.h"
#include "../RHI_Shader.h"
#include "../RHI_RenderTexture.h"
#include "../RHI_ConstantBuffer.h"
#include "../../Profiling/Profiler.h"
#include "../../Logging/Log.h"
//===================================

//= NAMESPACES ================
using namespace std;
using namespace Spartan::Math;
//=============================

namespace Spartan
{
	RHI_CommandList::RHI_CommandList(RHI_Device* rhi_device, Profiler* profiler)
	{
		m_commands.reserve(m_initial_capacity);
		m_commands.resize(m_initial_capacity);
		m_rhi_device	= rhi_device;
		m_profiler		= profiler;
	}

	RHI_CommandList::~RHI_CommandList()
	{
		
	}

	void RHI_CommandList::Begin(const string& pass_name, void* render_pass, RHI_SwapChain* swap_chain)
	{
		RHI_Command& cmd	= GetCmd();
		cmd.type			= RHI_Cmd_Begin;
		cmd.pass_name		= pass_name;
	}

	void RHI_CommandList::End()
	{
		RHI_Command& cmd	= GetCmd();
		cmd.type			= RHI_Cmd_End;
	}

	void RHI_CommandList::Draw(unsigned int vertex_count)
	{
		RHI_Command& cmd	= GetCmd();
		cmd.type			= RHI_Cmd_Draw;
		cmd.vertex_count	= vertex_count;
	}

	void RHI_CommandList::DrawIndexed(unsigned int index_count, unsigned int index_offset, unsigned int vertex_offset)
	{
		RHI_Command& cmd	= GetCmd();
		cmd.type			= RHI_Cmd_DrawIndexed;
		cmd.index_count		= index_count;
		cmd.index_offset	= index_offset;
		cmd.vertex_offset	= vertex_offset;
	}

	void RHI_CommandList::SetPipeline(const RHI_Pipeline* pipeline)
	{
		SetViewport(pipeline->m_viewport);
		SetBlendState(pipeline->m_blend_state);
		SetDepthStencilState(pipeline->m_depth_stencil_state);
		SetRasterizerState(pipeline->m_rasterizer_state);
		SetInputLayout(pipeline->m_shader_vertex->GetInputLayout());
		SetShaderVertex(pipeline->m_shader_vertex);
		SetShaderPixel(pipeline->m_shader_pixel);	
	}

	void RHI_CommandList::SetViewport(const RHI_Viewport& viewport)
	{
		RHI_Command& cmd	= GetCmd();
		cmd.type			= RHI_Cmd_SetViewport;
		cmd.viewport		= viewport;
	}

	void RHI_CommandList::SetScissorRectangle(const Math::Rectangle& scissor_rectangle)
	{
		RHI_Command& cmd		= GetCmd();
		cmd.type				= RHI_Cmd_SetScissorRectangle;
		cmd.scissor_rectangle	= scissor_rectangle;
	}

	void RHI_CommandList::SetPrimitiveTopology(RHI_PrimitiveTopology_Mode primitive_topology)
	{
		RHI_Command& cmd		= GetCmd();
		cmd.type				= RHI_Cmd_SetPrimitiveTopology;
		cmd.primitive_topology	= primitive_topology;
	}

	void RHI_CommandList::SetInputLayout(const RHI_InputLayout* input_layout)
	{
		RHI_Command& cmd	= GetCmd();
		cmd.type			= RHI_Cmd_SetInputLayout;
		cmd.input_layout	= input_layout;
	}

	void RHI_CommandList::SetDepthStencilState(const RHI_DepthStencilState* depth_stencil_state)
	{
		RHI_Command& cmd		= GetCmd();
		cmd.type				= RHI_Cmd_SetDepthStencilState;
		cmd.depth_stencil_state = depth_stencil_state;
	}

	void RHI_CommandList::SetRasterizerState(const RHI_RasterizerState* rasterizer_state)
	{
		RHI_Command& cmd		= GetCmd();
		cmd.type				= RHI_Cmd_SetRasterizerState;
		cmd.rasterizer_state	= rasterizer_state;
	}

	void RHI_CommandList::SetBlendState(const RHI_BlendState* blend_state)
	{
		RHI_Command& cmd	= GetCmd();
		cmd.type			= RHI_Cmd_SetBlendState;
		cmd.blend_state		= blend_state;
	}

	void RHI_CommandList::SetBufferVertex(const RHI_VertexBuffer* buffer)
	{
		RHI_Command& cmd	= GetCmd();
		cmd.type			= RHI_Cmd_SetVertexBuffer;
		cmd.buffer_vertex	= buffer;
	}

	void RHI_CommandList::SetBufferIndex(const RHI_IndexBuffer* buffer)
	{
		RHI_Command& cmd	= GetCmd();
		cmd.type			= RHI_Cmd_SetIndexBuffer;
		cmd.buffer_index	= buffer;
	}

	void RHI_CommandList::SetShaderVertex(const RHI_Shader* shader)
	{
		RHI_Command& cmd	= GetCmd();
		cmd.type			= RHI_Cmd_SetVertexShader;
		cmd.shader_vertex	= shader;
	}

	void RHI_CommandList::SetShaderPixel(const RHI_Shader* shader)
	{
		RHI_Command& cmd	= GetCmd();
		cmd.type			= RHI_Cmd_SetPixelShader;
		cmd.shader_pixel	= shader;
	}

	void RHI_CommandList::SetConstantBuffers(unsigned int start_slot, RHI_Buffer_Scope scope, const vector<void*>& constant_buffers)
	{
		RHI_Command& cmd				= GetCmd();
		cmd.type						= RHI_Cmd_SetConstantBuffers;
		cmd.constant_buffers_start_slot = start_slot;
		cmd.constant_buffers_scope		= scope;
		cmd.constant_buffers			= constant_buffers;
	}

	void RHI_CommandList::SetConstantBuffer(unsigned int start_slot, RHI_Buffer_Scope scope, const shared_ptr<RHI_ConstantBuffer>& constant_buffer)
	{
		RHI_Command& cmd				= GetCmd();
		cmd.type						= RHI_Cmd_SetConstantBuffers;
		cmd.constant_buffers_start_slot = start_slot;
		cmd.constant_buffers_scope		= scope;
		cmd.constant_buffers.emplace_back(constant_buffer->GetBufferView());
	}

	void RHI_CommandList::SetSamplers(unsigned int start_slot, const vector<void*>& samplers)
	{
		RHI_Command& cmd		= GetCmd();
		cmd.type				= RHI_Cmd_SetSamplers;
		cmd.samplers_start_slot = start_slot;
		cmd.samplers			= samplers;
	}

	void RHI_CommandList::SetSampler(unsigned int start_slot, const shared_ptr<RHI_Sampler>& sampler)
	{
		RHI_Command& cmd		= GetCmd();
		cmd.type				= RHI_Cmd_SetSamplers;
		cmd.samplers_start_slot = start_slot;
		cmd.samplers.emplace_back(sampler->GetBufferView());
	}

	void RHI_CommandList::SetTextures(unsigned int start_slot, const vector<void*>& textures)
	{
		RHI_Command& cmd		= GetCmd();
		cmd.type				= RHI_Cmd_SetTextures;
		cmd.textures_start_slot = start_slot;
		cmd.textures			= textures;
	}

	void RHI_CommandList::SetTexture(unsigned int start_slot, void* texture)
	{
		RHI_Command& cmd		= GetCmd();
		cmd.type				= RHI_Cmd_SetTextures;
		cmd.textures_start_slot = start_slot;
		cmd.textures.emplace_back(texture);
	}

	void RHI_CommandList::SetTexture(unsigned int start_slot, const shared_ptr<RHI_Texture>& texture)
	{
		SetTexture(start_slot, texture->GetBufferView());
	}

	void RHI_CommandList::SetTexture(unsigned int start_slot, const shared_ptr<RHI_RenderTexture>& texture)
	{
		SetTexture(start_slot, texture->GetBufferView());
	}

	void RHI_CommandList::SetRenderTargets(const vector<void*>& render_targets, void* depth_stencil /*= nullptr*/)
	{
		RHI_Command& cmd	= GetCmd();
		cmd.type			= RHI_Cmd_SetRenderTargets;
		cmd.render_targets	= render_targets;
		cmd.depth_stencil	= depth_stencil;
	}

	void RHI_CommandList::SetRenderTarget(void* render_target, void* depth_stencil /*= nullptr*/)
	{
		RHI_Command& cmd	= GetCmd();
		cmd.type			= RHI_Cmd_SetRenderTargets;	
		cmd.depth_stencil	= depth_stencil;
		cmd.render_targets.emplace_back(render_target);
	}

	void RHI_CommandList::SetRenderTarget(const shared_ptr<RHI_RenderTexture>& render_target, void* depth_stencil /*= nullptr*/)
	{
		SetRenderTarget(render_target->GetBufferRenderTargetView(), depth_stencil);
	}

	void RHI_CommandList::ClearRenderTarget(void* render_target, const Vector4& color)
	{
		RHI_Command& cmd				= GetCmd();
		cmd.type						= RHI_Cmd_ClearRenderTarget;
		cmd.render_target_clear			= render_target;
		cmd.render_target_clear_color	= color;
	}

	void RHI_CommandList::ClearDepthStencil(void* depth_stencil, unsigned int flags, float depth, unsigned int stencil /*= 0*/)
	{
		RHI_Command& cmd		= GetCmd();
		cmd.type				= RHI_Cmd_ClearDepthStencil;
		cmd.depth_stencil		= depth_stencil;
		cmd.depth_clear_flags	= flags;
		cmd.depth_clear			= depth;
		cmd.depth_clear_stencil = stencil;
	}

	bool RHI_CommandList::Submit()
	{
		for (unsigned int cmd_index = 0; cmd_index < m_command_count; cmd_index++)
		{
			RHI_Command& cmd = m_commands[cmd_index];

			switch (cmd.type)
			{
			case RHI_Cmd_Begin:
				m_profiler->TimeBlockStart(cmd.pass_name, true, true);
				m_rhi_device->BeginMarker(cmd.pass_name);
				break;

			case RHI_Cmd_End:
				m_rhi_device->EndMarker();
				m_profiler->TimeBlockEnd();
				break;

			case RHI_Cmd_Draw:
				m_rhi_device->Draw(cmd.vertex_count);
				m_profiler->m_rhi_draw_calls++;
				break;

			case RHI_Cmd_DrawIndexed:
				m_rhi_device->DrawIndexed(cmd.index_count, cmd.index_offset, cmd.vertex_offset);
				m_profiler->m_rhi_draw_calls++;
				break;

			case RHI_Cmd_SetViewport:
				m_rhi_device->SetViewport(cmd.viewport);
				break;

			case RHI_Cmd_SetScissorRectangle:
				m_rhi_device->SetScissorRectangle(cmd.scissor_rectangle);
				break;

			case RHI_Cmd_SetPrimitiveTopology:
				m_rhi_device->SetPrimitiveTopology(cmd.primitive_topology);
				break;

			case RHI_Cmd_SetInputLayout:
				m_rhi_device->SetInputLayout(cmd.input_layout);
				break;

			case RHI_Cmd_SetDepthStencilState:
				m_rhi_device->SetDepthStencilState(cmd.depth_stencil_state);
				break;

			case RHI_Cmd_SetRasterizerState:
				m_rhi_device->SetRasterizerState(cmd.rasterizer_state);
				break;

			case RHI_Cmd_SetBlendState:
				m_rhi_device->SetBlendState(cmd.blend_state);
				break;

			case RHI_Cmd_SetVertexBuffer:
				m_rhi_device->SetVertexBuffer(cmd.buffer_vertex);
				m_profiler->m_rhi_bindings_buffer_vertex++;
				break;

			case RHI_Cmd_SetIndexBuffer:
				m_rhi_device->SetIndexBuffer(cmd.buffer_index);
				m_profiler->m_rhi_bindings_buffer_index++;
				break;

			case RHI_Cmd_SetVertexShader:
				m_rhi_device->SetVertexShader(cmd.shader_vertex);
				m_profiler->m_rhi_bindings_vertex_shader++;
				break;

			case RHI_Cmd_SetPixelShader:
				m_rhi_device->SetPixelShader(cmd.shader_pixel);
				m_profiler->m_rhi_bindings_pixel_shader++;
				break;

			case RHI_Cmd_SetConstantBuffers:
				m_rhi_device->SetConstantBuffers(cmd.constant_buffers_start_slot, static_cast<unsigned int>(cmd.constant_buffers.size()), cmd.constant_buffers.data(), cmd.constant_buffers_scope);
				m_profiler->m_rhi_bindings_buffer_constant += (cmd.constant_buffers_scope == Buffer_Global) ? 2 : 1;
				break;

			case RHI_Cmd_SetSamplers:
				m_rhi_device->SetSamplers(cmd.samplers_start_slot, static_cast<unsigned int>(cmd.samplers.size()), cmd.samplers.data());
				m_profiler->m_rhi_bindings_sampler++;
				break;

			case RHI_Cmd_SetTextures:
				m_rhi_device->SetTextures(cmd.textures_start_slot, static_cast<unsigned int>(cmd.textures.size()), cmd.textures.data());
				m_profiler->m_rhi_bindings_texture++;
				break;

			case RHI_Cmd_SetRenderTargets:
				m_rhi_device->SetRenderTargets(static_cast<unsigned int>(cmd.render_targets.size()), cmd.render_targets.data(), cmd.depth_stencil);
				m_profiler->m_rhi_bindings_render_target++;
				break;

			case RHI_Cmd_ClearRenderTarget:
				m_rhi_device->ClearRenderTarget(cmd.render_target_clear, cmd.render_target_clear_color);
				break;

			case RHI_Cmd_ClearDepthStencil:
				m_rhi_device->ClearDepthStencil(cmd.depth_stencil, cmd.depth_clear_flags, cmd.depth_clear, cmd.depth_clear_stencil);
				break;
			}
		}

		Clear();
		return true;
	}

	RHI_Command& RHI_CommandList::GetCmd()
	{
		// Grow capacity if needed
		if (m_command_count >= m_commands.size())
		{
			unsigned int new_size = m_command_count + 100;
			m_commands.reserve(new_size);
			m_commands.resize(new_size);
			LOGF_WARNING("Command list has grown to fit %d commands. Consider making the capacity larger to avoid re-allocations.", m_command_count + 1);
		}

		m_command_count++;
		return m_commands[m_command_count - 1];	
	}

	void RHI_CommandList::Clear()
	{
		for (unsigned int cmd_index = 0; cmd_index < m_command_count; cmd_index++)
		{
			RHI_Command& cmd = m_commands[cmd_index];
			cmd.Clear();
		}

		m_command_count = 0;
	}
}

#endif