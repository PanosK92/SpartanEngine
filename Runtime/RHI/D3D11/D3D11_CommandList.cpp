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
#include "../RHI_ConstantBuffer.h"
#include "../../Profiling/Profiler.h"
#include "../../Logging/Log.h"
#include "../RHI_VertexBuffer.h"
#include "../RHI_IndexBuffer.h"
#include "../RHI_BlendState.h"
#include "../RHI_DepthStencilState.h"
#include "../RHI_RasterizerState.h"
#include "../RHI_InputLayout.h"
//===================================

//= NAMESPACES ================
using namespace std;
using namespace Spartan::Math;
//=============================

namespace Spartan
{
	RHI_CommandList::RHI_CommandList(const std::shared_ptr<RHI_Device>& rhi_device, Profiler* profiler)
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

	void RHI_CommandList::SetPipeline(RHI_Pipeline* pipeline)
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
		cmd.constant_buffer_count		= static_cast<unsigned int>(constant_buffers.size());
	}

	void RHI_CommandList::SetConstantBuffer(unsigned int start_slot, RHI_Buffer_Scope scope, const shared_ptr<RHI_ConstantBuffer>& constant_buffer)
	{
		RHI_Command& cmd								= GetCmd();
		cmd.type										= RHI_Cmd_SetConstantBuffers;
		cmd.constant_buffers_start_slot					= start_slot;
		cmd.constant_buffers_scope						= scope;
		cmd.constant_buffers[cmd.constant_buffer_count] = constant_buffer->GetResource();
		cmd.constant_buffer_count++;
	}

	void RHI_CommandList::SetSamplers(unsigned int start_slot, const vector<void*>& samplers)
	{
		RHI_Command& cmd		= GetCmd();
		cmd.type				= RHI_Cmd_SetSamplers;
		cmd.samplers_start_slot = start_slot;
		cmd.samplers			= samplers;
		cmd.sampler_count		= static_cast<unsigned int>(samplers.size());
	}

	void RHI_CommandList::SetSampler(unsigned int start_slot, const shared_ptr<RHI_Sampler>& sampler)
	{
		RHI_Command& cmd				= GetCmd();
		cmd.type						= RHI_Cmd_SetSamplers;
		cmd.samplers_start_slot			= start_slot;
		cmd.samplers[cmd.sampler_count] = sampler->GetResource();
		cmd.sampler_count++;
	}

	void RHI_CommandList::SetTextures(uint32_t start_slot, const vector<void*>& textures)
	{
		RHI_Command& cmd		= GetCmd();
		cmd.type				= RHI_Cmd_SetTextures;
		cmd.textures_start_slot = start_slot;
		cmd.textures			= textures;
		cmd.texture_count		= static_cast<unsigned int>(textures.size());
	}

	void RHI_CommandList::SetTexture(uint32_t start_slot, RHI_Texture* texture)
	{
		RHI_Command& cmd				= GetCmd();
		cmd.type						= RHI_Cmd_SetTextures;
		cmd.textures_start_slot			= start_slot;
		cmd.textures[cmd.texture_count] = texture ? texture->GetResource_Texture() : nullptr;
		cmd.texture_count++;
	}

	void RHI_CommandList::SetRenderTargets(const vector<void*>& render_targets, void* depth_stencil /*= nullptr*/)
	{
		RHI_Command& cmd		= GetCmd();
		cmd.type				= RHI_Cmd_SetRenderTargets;
		cmd.render_targets		= render_targets;
		cmd.render_target_count = static_cast<unsigned int>(render_targets.size());
		cmd.depth_stencil		= depth_stencil;
	}

	void RHI_CommandList::SetRenderTarget(void* render_target, void* depth_stencil /*= nullptr*/)
	{
		RHI_Command& cmd							= GetCmd();
		cmd.type									= RHI_Cmd_SetRenderTargets;	
		cmd.depth_stencil							= depth_stencil;
		cmd.render_targets[cmd.render_target_count] = render_target;
		cmd.render_target_count++;
	}

	void RHI_CommandList::SetRenderTarget(const shared_ptr<RHI_Texture>& render_target, void* depth_stencil /*= nullptr*/)
	{
		SetRenderTarget(render_target->GetResource_RenderTarget(), depth_stencil);
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
		if (!depth_stencil)
		{
			LOG_ERROR("Provided depth stencil is null");
			return;
		}

		RHI_Command& cmd		= GetCmd();
		cmd.type				= RHI_Cmd_ClearDepthStencil;
		cmd.depth_stencil		= depth_stencil;
		cmd.depth_clear_flags	= flags;
		cmd.depth_clear			= depth;
		cmd.depth_clear_stencil = stencil;
	}

	bool RHI_CommandList::Submit()
	{
		auto context		= m_rhi_device->GetContext();
		auto device_context	= m_rhi_device->GetContext()->device_context;

		for (unsigned int cmd_index = 0; cmd_index < m_command_count; cmd_index++)
		{
			RHI_Command& cmd = m_commands[cmd_index];

			switch (cmd.type)
			{
				case RHI_Cmd_Begin:
				{
					m_profiler->TimeBlockStart(cmd.pass_name, true, true);
					#ifdef DEBUG
					context->annotation->BeginEvent(FileSystem::StringToWstring(cmd.pass_name).c_str());
					#endif
					break;
				}

				case RHI_Cmd_End:
				{
					#ifdef DEBUG
					context->annotation->EndEvent();
					#endif
					m_profiler->TimeBlockEnd();
					break;
				}

				case RHI_Cmd_Draw:
				{
					device_context->Draw(static_cast<UINT>(cmd.vertex_count), 0);

					m_profiler->m_rhi_draw_calls++;
					break;
				}

				case RHI_Cmd_DrawIndexed:
				{
					SPARTAN_ASSERT(cmd.index_count != 0);

					device_context->DrawIndexed
					(
						static_cast<UINT>(cmd.index_count),
						static_cast<UINT>(cmd.index_offset),
						static_cast<INT>(cmd.vertex_offset)
					);

					m_profiler->m_rhi_draw_calls++;
					break;
				}

				case RHI_Cmd_SetViewport:
				{
					D3D11_VIEWPORT d3d11_viewport;
					d3d11_viewport.TopLeftX	= cmd.viewport.GetX();
					d3d11_viewport.TopLeftY	= cmd.viewport.GetY();
					d3d11_viewport.Width	= cmd.viewport.GetWidth();
					d3d11_viewport.Height	= cmd.viewport.GetHeight();
					d3d11_viewport.MinDepth	= cmd.viewport.GetMinDepth();
					d3d11_viewport.MaxDepth	= cmd.viewport.GetMaxDepth();

					device_context->RSSetViewports(1, &d3d11_viewport);

					break;
				}

				case RHI_Cmd_SetScissorRectangle:
				{
					const auto left		= cmd.scissor_rectangle.x;
					const auto top		= cmd.scissor_rectangle.y;
					const auto right	= cmd.scissor_rectangle.x + cmd.scissor_rectangle.width;
					const auto bottom	= cmd.scissor_rectangle.y + cmd.scissor_rectangle.height;
					const D3D11_RECT d3d11_rectangle = { static_cast<LONG>(left), static_cast<LONG>(top), static_cast<LONG>(right), static_cast<LONG>(bottom) };

					device_context->RSSetScissorRects(1, &d3d11_rectangle);

					break;
				}

				case RHI_Cmd_SetPrimitiveTopology:
				{
					device_context->IASetPrimitiveTopology(d3d11_primitive_topology[cmd.primitive_topology]);
					break;
				}

				case RHI_Cmd_SetInputLayout:
				{
					device_context->IASetInputLayout(static_cast<ID3D11InputLayout*>(cmd.input_layout->GetResource()));
					break;
				}

				case RHI_Cmd_SetDepthStencilState:
				{
					device_context->OMSetDepthStencilState(
						static_cast<ID3D11DepthStencilState*>(cmd.depth_stencil_state->GetBuffer()), 1
					);
					break;
				}

				case RHI_Cmd_SetRasterizerState:
				{
					device_context->RSSetState(
						static_cast<ID3D11RasterizerState*>(cmd.rasterizer_state->GetBuffer())
					);

					break;
				}

				case RHI_Cmd_SetBlendState:
				{
					FLOAT blend_factor[4] = { 0.0f, 0.0f, 0.0f, 0.0f };

					device_context->OMSetBlendState(
						static_cast<ID3D11BlendState*>(cmd.blend_state->GetBuffer()),
						blend_factor,
						0xffffffff
					);

					break;
				}

				case RHI_Cmd_SetVertexBuffer:
				{
					auto ptr			= static_cast<ID3D11Buffer*>(cmd.buffer_vertex->GetResource());
					auto stride			= cmd.buffer_vertex->GetStride();
					unsigned int offset = 0;
					device_context->IASetVertexBuffers(0, 1, &ptr, &stride, &offset);

					m_profiler->m_rhi_bindings_buffer_vertex++;
					break;
				}

				case RHI_Cmd_SetIndexBuffer:
				{
					device_context->IASetIndexBuffer
					(
						static_cast<ID3D11Buffer*>(cmd.buffer_index->GetResource()),
						cmd.buffer_index->Is16Bit() ? DXGI_FORMAT_R16_UINT : DXGI_FORMAT_R32_UINT,
						0
					);

					m_profiler->m_rhi_bindings_buffer_index++;
					break;
				}

				case RHI_Cmd_SetVertexShader:
				{
					const auto ptr = static_cast<ID3D11VertexShader*>(cmd.shader_vertex->GetResource_VertexShader());
					device_context->VSSetShader(ptr, nullptr, 0);

					m_profiler->m_rhi_bindings_vertex_shader++;
					break;
				}

				case RHI_Cmd_SetPixelShader:
				{
					const auto ptr = static_cast<ID3D11PixelShader*>(cmd.shader_pixel ? cmd.shader_pixel->GetResource_PixelShader() : nullptr);
					device_context->PSSetShader(ptr, nullptr, 0);

					m_profiler->m_rhi_bindings_pixel_shader++;
					break;
				}

				case RHI_Cmd_SetConstantBuffers:
				{
					const auto start_slot	= static_cast<UINT>(cmd.constant_buffers_start_slot);
					const auto buffer_count = static_cast<UINT>(cmd.constant_buffer_count);
					const auto buffer		= reinterpret_cast<ID3D11Buffer*const*>(cmd.constant_buffers.data());
					const auto scope		= cmd.constant_buffers_scope;

					if (scope == Buffer_VertexShader || scope == Buffer_Global)
					{
						device_context->VSSetConstantBuffers(start_slot, buffer_count, buffer);
					}

					if (scope == Buffer_PixelShader || scope == Buffer_Global)
					{
						device_context->PSSetConstantBuffers(start_slot, buffer_count, buffer);
					}

					m_profiler->m_rhi_bindings_buffer_constant += (cmd.constant_buffers_scope == Buffer_Global) ? 2 : 1;
					break;
				}

				case RHI_Cmd_SetSamplers:
				{
					device_context->PSSetSamplers
					(
						static_cast<UINT>(cmd.samplers_start_slot),
						static_cast<UINT>(cmd.sampler_count),
						reinterpret_cast<ID3D11SamplerState* const*>(cmd.samplers.data())
					);

					m_profiler->m_rhi_bindings_sampler++;
					break;
				}

				case RHI_Cmd_SetTextures:
				{
					device_context->PSSetShaderResources
					(
						static_cast<UINT>(cmd.textures_start_slot),
						static_cast<UINT>(cmd.texture_count),
						reinterpret_cast<ID3D11ShaderResourceView* const*>(cmd.textures.data())
					);

					m_profiler->m_rhi_bindings_texture++;
					break;
				}

				case RHI_Cmd_SetRenderTargets:
				{
					device_context->OMSetRenderTargets
					(
						static_cast<UINT>(cmd.render_target_count),
						reinterpret_cast<ID3D11RenderTargetView* const*>(cmd.render_targets.data()),
						static_cast<ID3D11DepthStencilView*>(cmd.depth_stencil)
					);

					m_profiler->m_rhi_bindings_render_target++;
					break;
				}

				case RHI_Cmd_ClearRenderTarget:
				{
					device_context->ClearRenderTargetView
					(
						static_cast<ID3D11RenderTargetView*>(cmd.render_target_clear),
						cmd.render_target_clear_color.Data()
					);
					break;
				}

				case RHI_Cmd_ClearDepthStencil:
				{
					UINT clear_flags = 0;
					clear_flags |= (cmd.depth_clear_flags & Clear_Depth)	? D3D11_CLEAR_DEPTH : 0;
					clear_flags |= (cmd.depth_clear_flags & Clear_Stencil)	? D3D11_CLEAR_STENCIL : 0;

					device_context->ClearDepthStencilView
					(
						static_cast<ID3D11DepthStencilView*>(cmd.depth_stencil),
						clear_flags,
						static_cast<FLOAT>(cmd.depth_clear),
						static_cast<UINT8>(cmd.depth_clear_stencil)
					);

					break;
				}
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