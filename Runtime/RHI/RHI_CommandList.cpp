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

//= INCLUDES =====================
#include "RHI_CommandList.h"
#include "RHI_Device.h"
#include "../Profiling/Profiler.h"
//================================

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
	}

	void RHI_CommandList::Begin(const string& pass_name)
	{
		RHI_Command cmd;
		cmd.type		= RHI_Cmd_Begin;
		cmd.pass_name	= pass_name;

		m_commands.emplace_back(cmd);
	}

	void RHI_CommandList::End()
	{
		RHI_Command cmd;
		cmd.type = RHI_Cmd_End;

		m_commands.emplace_back(cmd);
	}

	void RHI_CommandList::Draw(unsigned int vertex_count)
	{
		RHI_Command cmd;
		cmd.type			= RHI_Cmd_Draw;
		cmd.vertex_count	= vertex_count;

		m_commands.emplace_back(cmd);
	}

	void RHI_CommandList::DrawIndexed(unsigned int index_count, unsigned int index_offset, unsigned int vertex_offset)
	{
		RHI_Command cmd;
		cmd.type			= RHI_Cmd_DrawIndexed;
		cmd.index_count		= index_count;
		cmd.index_offset	= index_offset;
		cmd.vertex_offset	= vertex_offset;
		
		m_commands.emplace_back(cmd);
	}

	void RHI_CommandList::ClearRenderTarget(void* render_target, const Vector4& color)
	{
		RHI_Command cmd;
		cmd.type = RHI_Cmd_ClearRenderTarget;
		cmd.render_targets_clear.emplace_back(render_target);
		cmd.render_target_clear_color.emplace_back(color);

		m_commands.emplace_back(cmd);
	}

	void RHI_CommandList::ClearDepthStencil(void* depth_stencil, unsigned int flags, float depth, unsigned int stencil /*= 0*/)
	{
		RHI_Command cmd;
		cmd.type				= RHI_Cmd_ClearDepthStencil;
		cmd.depth_stencil		= depth_stencil;
		cmd.depth_clear_flags	= flags;
		cmd.depth_clear			= depth;
		cmd.depth_clear_stencil	= stencil;
			
		m_commands.emplace_back(cmd);
	}

	void RHI_CommandList::SetViewport(const RHI_Viewport& viewport)
	{
		RHI_Command cmd;
		cmd.type		= RHI_Cmd_SetViewport;
		cmd.viewport	= viewport;

		m_commands.emplace_back(cmd);
	}

	void RHI_CommandList::SetScissorRectangle(const Rectangle& scissor_rectangle)
	{
		RHI_Command cmd;
		cmd.type				= RHI_Cmd_SetScissorRectangle;
		cmd.scissor_rectangle	= scissor_rectangle;

		m_commands.emplace_back(cmd);
	}

	void RHI_CommandList::SetPrimitiveTopology(RHI_PrimitiveTopology_Mode primitive_topology)
	{
		RHI_Command cmd;
		cmd.type				= RHI_Cmd_SetPrimitiveTopology;
		cmd.primitive_topology	= primitive_topology;

		m_commands.emplace_back(cmd);
	}

	void RHI_CommandList::SetInputLayout(const RHI_InputLayout* input_layout)
	{
		RHI_Command cmd;
		cmd.type			= RHI_Cmd_SetInputLayout;
		cmd.input_layout	= input_layout;

		m_commands.emplace_back(cmd);
	}

	void RHI_CommandList::SetInputLayout(const shared_ptr<RHI_InputLayout>& input_layout)
	{
		SetInputLayout(input_layout.get());
	}

	void RHI_CommandList::SetDepthStencilState(const RHI_DepthStencilState* depth_stencil_state)
	{
		RHI_Command cmd;
		cmd.type				= RHI_Cmd_SetDepthStencilState;
		cmd.depth_stencil_state = depth_stencil_state;
		
		m_commands.emplace_back(cmd);
	}

	void RHI_CommandList::SetDepthStencilState(const shared_ptr<RHI_DepthStencilState>& depth_stencil_state)
	{
		SetDepthStencilState(depth_stencil_state.get());
	}

	void RHI_CommandList::SetRasterizerState(const RHI_RasterizerState* rasterizer_state)
	{
		RHI_Command cmd;
		cmd.type				= RHI_Cmd_SetRasterizerState;
		cmd.rasterizer_state	= rasterizer_state;

		m_commands.emplace_back(cmd);
	}

	void RHI_CommandList::SetRasterizerState(const shared_ptr<RHI_RasterizerState>& rasterizer_state)
	{
		SetRasterizerState(rasterizer_state.get());
	}

	void RHI_CommandList::SetBlendState(const RHI_BlendState* blend_state)
	{
		RHI_Command cmd;
		cmd.type		= RHI_Cmd_SetBlendState;
		cmd.blend_state = blend_state;

		m_commands.emplace_back(cmd);
	}

	void RHI_CommandList::SetBlendState(const shared_ptr<RHI_BlendState>& blend_state)
	{
		SetBlendState(blend_state.get());
	}

	void RHI_CommandList::SetBufferVertex(const RHI_VertexBuffer* buffer)
	{
		RHI_Command cmd;
		cmd.type			= RHI_Cmd_SetVertexBuffer;
		cmd.buffer_vertex	= buffer;

		m_commands.emplace_back(cmd);
	}

	void RHI_CommandList::SetBufferVertex(const shared_ptr<RHI_VertexBuffer>& buffer)
	{
		SetBufferVertex(buffer.get());
	}

	void RHI_CommandList::SetBufferIndex(const RHI_IndexBuffer* buffer)
	{
		RHI_Command cmd;
		cmd.type			= RHI_Cmd_SetIndexBuffer;
		cmd.buffer_index	= buffer;

		m_commands.emplace_back(cmd);
	}

	void RHI_CommandList::SetBufferIndex(const std::shared_ptr<RHI_IndexBuffer>& buffer)
	{
		SetBufferIndex(buffer.get());
	}

	void RHI_CommandList::SetShaderVertex(const RHI_Shader* shader)
	{
		RHI_Command cmd;
		cmd.type			= RHI_Cmd_SetVertexShader;
		cmd.shader_vertex	= shader;

		m_commands.emplace_back(cmd);
	}

	void RHI_CommandList::SetShaderVertex(const std::shared_ptr<RHI_Shader>& shader)
	{
		SetShaderVertex(shader.get());
	}

	void RHI_CommandList::SetShaderPixel(const RHI_Shader* shader)
	{
		RHI_Command cmd;
		cmd.type			= RHI_Cmd_SetPixelShader;
		cmd.shader_pixel	= shader;

		m_commands.emplace_back(cmd);
	}

	void RHI_CommandList::SetShaderPixel(const std::shared_ptr<RHI_Shader>& shader)
	{
		SetShaderPixel(shader.get());
	}

	void RHI_CommandList::SetConstantBuffers(unsigned int start_slot, const vector<void*>& constant_buffers, RHI_Buffer_Scope scope)
	{
		RHI_Command cmd;
		cmd.type						= RHI_Cmd_SetConstantBuffers;
		cmd.constant_buffers			= constant_buffers;
		cmd.constant_buffers_start_slot	= start_slot;
		cmd.constant_buffers_scope		= scope;

		m_commands.emplace_back(cmd);
	}

	void RHI_CommandList::SetSamplers(unsigned int start_slot, const vector<void*>& samplers)
	{
		RHI_Command cmd;
		cmd.type				= RHI_Cmd_SetSamplers;
		cmd.samplers			= samplers;
		cmd.samplers_start_slot = start_slot;

		m_commands.emplace_back(cmd);
	}

	void RHI_CommandList::SetTextures(unsigned int start_slot, const vector<void*>& textures)
	{
		RHI_Command cmd;
		cmd.type				= RHI_Cmd_SetTextures;
		cmd.textures			= textures;
		cmd.textures_start_slot = start_slot;

		m_commands.emplace_back(cmd);
	}

	void RHI_CommandList::SetRenderTargets(const vector<void*>& render_targets, void* depth_stencil /*= nullptr*/)
	{
		RHI_Command cmd;
		cmd.type			= RHI_Cmd_SetRenderTargets;
		cmd.render_targets	= render_targets;
		cmd.depth_stencil	= depth_stencil;

		m_commands.emplace_back(cmd);
	}

	void RHI_CommandList::Clear()
	{
		m_commands.clear();
	}

	void RHI_CommandList::Submit()
	{
		string pass_name = "N/A";

		for (const auto& cmd : m_commands)
		{
			switch (cmd.type)
			{
			case RHI_Cmd_Begin:
				pass_name = cmd.pass_name;
				m_profiler->TimeBlockStartGpu(pass_name);
				m_rhi_device->EventBegin(pass_name);
				break;

			case RHI_Cmd_End:
				m_rhi_device->EventEnd();
				m_profiler->TimeBlockEndGpu(pass_name);
				break;

			case RHI_Cmd_Draw:
				m_rhi_device->Draw(cmd.vertex_count);
				m_profiler->m_rhi_draw_calls++;
				break;

			case RHI_Cmd_DrawIndexed:
				m_rhi_device->DrawIndexed(cmd.index_count, cmd.index_offset, cmd.vertex_offset);
				m_profiler->m_rhi_draw_calls++;
				break;

			case RHI_Cmd_ClearRenderTarget:
				for (unsigned int i = 0; i < cmd.render_targets_clear.size(); i++)
				{
					m_rhi_device->ClearRenderTarget(cmd.render_targets_clear[i], cmd.render_target_clear_color[i]);
				}
				break;

			case RHI_Cmd_ClearDepthStencil:
				m_rhi_device->ClearDepthStencil(cmd.depth_stencil, cmd.depth_clear_flags, cmd.depth_clear, cmd.depth_clear_stencil);
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
				{
					auto count		= static_cast<unsigned int>(cmd.constant_buffers.size());
					const void* ptr = count != 0 ? &cmd.constant_buffers[0] : nullptr;
					auto scope		= cmd.constant_buffers_scope;

					m_rhi_device->SetConstantBuffers(cmd.constant_buffers_start_slot, count, ptr, scope);
					m_profiler->m_rhi_bindings_buffer_constant += (scope == Buffer_Global) ? 2 : 1;
				}
				break;

			case RHI_Cmd_SetSamplers:
				{
					auto count		= static_cast<unsigned int>(cmd.samplers.size());
					const void* ptr	= count != 0 ? &cmd.samplers[0] : nullptr;

					m_rhi_device->SetSamplers(cmd.samplers_start_slot, count, ptr);
					m_profiler->m_rhi_bindings_sampler++;
				}
				break;

			case RHI_Cmd_SetTextures:
				{
					auto count		= static_cast<unsigned int>(cmd.textures.size());
					const void* ptr	= count != 0 ? &cmd.textures[0] : nullptr;

					m_rhi_device->SetTextures(cmd.textures_start_slot, count, ptr);
					m_profiler->m_rhi_bindings_texture++;
				}
				break;

			case RHI_Cmd_SetRenderTargets:
				{
					auto count		= static_cast<unsigned int>(cmd.render_targets.size());
					const auto ptr	= &cmd.render_targets[0];

					m_rhi_device->SetRenderTargets(count, ptr, cmd.depth_stencil);
					m_profiler->m_rhi_bindings_render_target++;
				}
				break;
			}
		}
	}
}