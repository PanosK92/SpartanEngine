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
#ifdef API_GRAPHICS_D3D11
//================================

//= INCLUDES ========================
#include "../../Profiling/Profiler.h"
#include "../../Logging/Log.h"
#include "../RHI_CommandList.h"
#include "../RHI_Pipeline.h"
#include "../RHI_Device.h"
#include "../RHI_Sampler.h"
#include "../RHI_Texture.h"
#include "../RHI_Shader.h"
#include "../RHI_ConstantBuffer.h"
#include "../RHI_VertexBuffer.h"
#include "../RHI_IndexBuffer.h"
#include "../RHI_BlendState.h"
#include "../RHI_DepthStencilState.h"
#include "../RHI_RasterizerState.h"
#include "../RHI_InputLayout.h"
#include "../../Rendering/Renderer.h"
#include "../RHI_SwapChain.h"
//===================================

//= NAMESPACES ===============
using namespace std;
using namespace Spartan::Math;
//============================

namespace Spartan
{
	RHI_CommandList::RHI_CommandList(uint32_t index, RHI_SwapChain* swap_chain, Context* context)
	{
		m_commands.reserve(m_initial_capacity);
		m_commands.resize(m_initial_capacity);

        m_swap_chain            = swap_chain;
        m_renderer              = context->GetSubsystem<Renderer>().get();
        m_profiler              = context->GetSubsystem<Profiler>().get();
        m_rhi_device            = m_renderer->GetRhiDevice().get();
        m_rhi_pipeline_cache    = m_renderer->GetPipelineCache().get();
	}

	RHI_CommandList::~RHI_CommandList() = default;

	bool RHI_CommandList::Begin(const string& pass_name, RHI_Cmd_Type type /*= RHI_Cmd_Begin*/)
	{
        auto& cmd       = GetCmd();
        cmd.type        = RHI_Cmd_Begin;
        cmd.pass_name   = pass_name;

        if (m_pipeline_state.blend_state)                                           SetBlendState(m_pipeline_state.blend_state);
        if (m_pipeline_state.depth_stencil_state)                                   SetDepthStencilState(m_pipeline_state.depth_stencil_state);
        if (m_pipeline_state.rasterizer_state)                                      SetRasterizerState(m_pipeline_state.rasterizer_state);
        if (m_pipeline_state.shader_vertex)                                         SetInputLayout(m_pipeline_state.shader_vertex->GetInputLayout());
        if (m_pipeline_state.shader_vertex)                                         SetShaderVertex(m_pipeline_state.shader_vertex);
        if (m_pipeline_state.shader_pixel)                                          SetShaderPixel(m_pipeline_state.shader_pixel);
        if (m_pipeline_state.viewport.IsDefined())                                  SetViewport(m_pipeline_state.viewport);
        if (m_pipeline_state.primitive_topology != RHI_PrimitiveTopology_Unknown)   SetPrimitiveTopology(m_pipeline_state.primitive_topology);

        return true;
	}

	void RHI_CommandList::End()
	{
		auto& cmd	= GetCmd();
		cmd.type	= RHI_Cmd_End;
	}

	void RHI_CommandList::Draw(const uint32_t vertex_count)
	{
		auto& cmd			= GetCmd();
		cmd.type			= RHI_Cmd_Draw;
		cmd.vertex_count	= vertex_count;
	}

	void RHI_CommandList::DrawIndexed(const uint32_t index_count, const uint32_t index_offset, const uint32_t vertex_offset)
	{
		auto& cmd			= GetCmd();
		cmd.type			= RHI_Cmd_DrawIndexed;
		cmd.index_count		= index_count;
		cmd.index_offset	= index_offset;
		cmd.vertex_offset	= vertex_offset;
	}

	void RHI_CommandList::SetViewport(const RHI_Viewport& viewport)
	{
		auto& cmd		= GetCmd();
		cmd.type		= RHI_Cmd_SetViewport;
		cmd._viewport	= viewport;
	}

	void RHI_CommandList::SetScissorRectangle(const Math::Rectangle& scissor_rectangle)
	{
		auto& cmd		= GetCmd();
		cmd.type		= RHI_Cmd_SetScissorRectangle;
		cmd._rectangle	= scissor_rectangle;
	}

	void RHI_CommandList::SetPrimitiveTopology(const RHI_PrimitiveTopology_Mode primitive_topology)
	{
		auto& cmd	= GetCmd();
		cmd.type	= RHI_Cmd_SetPrimitiveTopology;
		cmd._uint8  = static_cast<uint8_t>(primitive_topology);
	}

	void RHI_CommandList::SetInputLayout(const RHI_InputLayout* input_layout)
	{
		if (!input_layout || !input_layout->GetResource())
		{
			LOG_ERROR_INVALID_PARAMETER();
			return;
		}

		auto& cmd		    = GetCmd();
		cmd.type		    = RHI_Cmd_SetInputLayout;
		cmd.resource_ptr    = input_layout->GetResource();
	}

	void RHI_CommandList::SetDepthStencilState(const RHI_DepthStencilState* depth_stencil_state)
	{
		if (!depth_stencil_state || !depth_stencil_state->GetResource())
		{
			LOG_ERROR_INVALID_PARAMETER();
			return;
		}

		auto& cmd			= GetCmd();
		cmd.type			= RHI_Cmd_SetDepthStencilState;
		cmd.resource_ptr    = depth_stencil_state->GetResource();
	}

	void RHI_CommandList::SetRasterizerState(const RHI_RasterizerState* rasterizer_state)
	{
		if (!rasterizer_state || !rasterizer_state->GetResource())
		{
			LOG_ERROR_INVALID_PARAMETER();
			return;
		}

		auto& cmd			= GetCmd();
		cmd.type		    = RHI_Cmd_SetRasterizerState;
		cmd.resource_ptr    = rasterizer_state->GetResource();
	}

	void RHI_CommandList::SetBlendState(const RHI_BlendState* blend_state)
	{
		if (!blend_state || !blend_state->GetResource())
		{
			LOG_ERROR_INVALID_PARAMETER();
			return;
		}

		auto& cmd			= GetCmd();
		cmd.type			= RHI_Cmd_SetBlendState;
		cmd.resource_ptr    = blend_state->GetResource();
	}

	void RHI_CommandList::SetBufferVertex(const RHI_VertexBuffer* buffer)
	{
		if (!buffer || !buffer->GetResource())
		{
			LOG_ERROR_INVALID_PARAMETER();
			return;
		}

		auto& cmd			= GetCmd();
		cmd.type			= RHI_Cmd_SetVertexBuffer;
		cmd.buffer_vertex	= buffer;
	}

	void RHI_CommandList::SetBufferIndex(const RHI_IndexBuffer* buffer)
	{
		if (!buffer || !buffer->GetResource())
		{
			LOG_ERROR_INVALID_PARAMETER();
			return;
		}

		auto& cmd			= GetCmd();
		cmd.type			= RHI_Cmd_SetIndexBuffer;
		cmd.buffer_index	= buffer;
	}

	void RHI_CommandList::SetShaderVertex(const RHI_Shader* shader)
	{
		// Vertex shader can never be null
		if (!shader || !shader->GetResource())
		{
			LOG_ERROR_INVALID_PARAMETER();
			return;
		}

		auto& cmd			= GetCmd();
		cmd.type			= RHI_Cmd_SetVertexShader;
		cmd.resource_ptr    = shader->GetResource();
	}

	void RHI_CommandList::SetShaderPixel(const RHI_Shader* shader)
	{
		auto& cmd		    = GetCmd();
		cmd.type		    = RHI_Cmd_SetPixelShader;
		cmd.resource_ptr    = shader ? shader->GetResource() : nullptr;
	}

    void RHI_CommandList::SetShaderCompute(const RHI_Shader* shader)
    {
        if (shader && !shader->GetResource())
        {
            LOG_WARNING("%s hasn't compiled", shader->GetName().c_str());
            return;
        }

        auto& cmd           = GetCmd();
        cmd.type            = RHI_Cmd_SetComputeShader;
        cmd.resource_ptr    = shader->GetResource();
    }

    void RHI_CommandList::SetConstantBuffer(const uint32_t slot, uint8_t scope, RHI_ConstantBuffer* constant_buffer)
    {
        auto& cmd               = GetCmd();
        cmd.type                = RHI_Cmd_SetConstantBuffers;
        cmd.resource_start_slot = slot;
        cmd._uint8              = scope;
        cmd.resource_ptr        = constant_buffer ? constant_buffer->GetResource() : nullptr;
        cmd.resource_count      = 1;
    }

    void RHI_CommandList::SetSampler(const uint32_t slot, RHI_Sampler* sampler)
    {
        auto& cmd               = GetCmd();
        cmd.type                = RHI_Cmd_SetSamplers;
        cmd.resource_start_slot = slot;
        cmd.resource_ptr        = sampler ? sampler->GetResource() : nullptr;
        cmd.resource_count      = 1;
    }

    void RHI_CommandList::SetTexture(const uint32_t slot, RHI_Texture* texture)
    {
		auto& cmd				= GetCmd();
		cmd.type				= RHI_Cmd_SetTextures;
		cmd.resource_start_slot = slot;
		cmd.resource_ptr        = texture ? texture->GetResource_Texture() : nullptr;
		cmd.resource_count      = 1;
	}

    void RHI_CommandList::SetRenderTargets(const void* render_targets, uint32_t render_target_count, void* depth_stencil /*= nullptr*/)
    {
        auto& cmd                   = GetCmd();
        cmd.type                    = RHI_Cmd_SetRenderTargets;
        cmd.resource_ptr            = render_targets;
        cmd.resource_count          = render_target_count;
        cmd.depth_stencil           = depth_stencil;
    }

	void RHI_CommandList::ClearRenderTarget(void* render_target, const Vector4& color)
	{
		auto& cmd			= GetCmd();
		cmd.type			= RHI_Cmd_ClearRenderTarget;
		cmd.resource_ptr    = render_target;
		cmd._vector4	    = color;
	}

	void RHI_CommandList::ClearDepthStencil(void* depth_stencil, const uint32_t flags, const float depth, const uint8_t stencil /*= 0*/)
	{
		if (!depth_stencil)
		{
			LOG_ERROR("Provided depth stencil is null");
			return;
		}

		auto& cmd			= GetCmd();
		cmd.type			= RHI_Cmd_ClearDepthStencil;
		cmd.depth_stencil	= depth_stencil;
		cmd._uint32         = flags;
		cmd._float			= depth;
		cmd._uint8          = stencil;
	}

	bool RHI_CommandList::Submit(bool profile /*=true*/)
	{
		const auto& context		    = m_rhi_device->GetContextRhi();
		const auto& device_context	= m_rhi_device->GetContextRhi()->device_context;

		for (uint32_t cmd_index = 0; cmd_index < m_command_count; cmd_index++)
		{
			auto& cmd = m_commands[cmd_index];

			switch (cmd.type)
			{
				case RHI_Cmd_Begin:
				{
                    if (profile) m_profiler->TimeBlockStart(cmd.pass_name, true, true);
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
                    if (profile) m_profiler->TimeBlockEnd();
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
					d3d11_viewport.TopLeftX	= cmd._viewport.x;
					d3d11_viewport.TopLeftY	= cmd._viewport.y;
					d3d11_viewport.Width	= cmd._viewport.width;
					d3d11_viewport.Height	= cmd._viewport.height;
					d3d11_viewport.MinDepth	= cmd._viewport.depth_min;
					d3d11_viewport.MaxDepth	= cmd._viewport.depth_max;

					device_context->RSSetViewports(1, &d3d11_viewport);

					break;
				}

				case RHI_Cmd_SetScissorRectangle:
				{
					const auto left		= cmd._rectangle.x;
					const auto top		= cmd._rectangle.y;
					const auto right	= cmd._rectangle.x + cmd._rectangle.width;
					const auto bottom	= cmd._rectangle.y + cmd._rectangle.height;
					const D3D11_RECT d3d11_rectangle = { static_cast<LONG>(left), static_cast<LONG>(top), static_cast<LONG>(right), static_cast<LONG>(bottom) };

					device_context->RSSetScissorRects(1, &d3d11_rectangle);

					break;
				}

				case RHI_Cmd_SetPrimitiveTopology:
				{
					device_context->IASetPrimitiveTopology(d3d11_primitive_topology[static_cast<RHI_PrimitiveTopology_Mode>(cmd._uint8)]);
					break;
				}

				case RHI_Cmd_SetInputLayout:
				{
					device_context->IASetInputLayout(static_cast<ID3D11InputLayout*>(const_cast<void*>(cmd.resource_ptr)));
					break;
				}

				case RHI_Cmd_SetDepthStencilState:
				{
					device_context->OMSetDepthStencilState(static_cast<ID3D11DepthStencilState*>(const_cast<void*>(cmd.resource_ptr)), 1);
					break;
				}

				case RHI_Cmd_SetRasterizerState:
				{
					device_context->RSSetState(static_cast<ID3D11RasterizerState*>(const_cast<void*>(cmd.resource_ptr)));

					break;
				}

				case RHI_Cmd_SetBlendState:
				{
					FLOAT blend_factor[4] = { cmd._float, cmd._float, cmd._float, cmd._float };

					device_context->OMSetBlendState
                    (
						static_cast<ID3D11BlendState*>(const_cast<void*>(cmd.resource_ptr)),
						blend_factor,
						0xffffffff
					);

					break;
				}

				case RHI_Cmd_SetVertexBuffer:
				{
					auto ptr			= static_cast<ID3D11Buffer*>(cmd.buffer_vertex->GetResource());
					auto stride			= cmd.buffer_vertex->GetStride();
					uint32_t offset     = 0;

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
					device_context->VSSetShader(static_cast<ID3D11VertexShader*>(const_cast<void*>(cmd.resource_ptr)), nullptr, 0);
					m_profiler->m_rhi_bindings_shader_vertex++;
					break;
				}

				case RHI_Cmd_SetPixelShader:
				{
					device_context->PSSetShader(static_cast<ID3D11PixelShader*>(const_cast<void*>(cmd.resource_ptr)), nullptr, 0);
					m_profiler->m_rhi_bindings_shader_pixel++;
					break;
				}

                case RHI_Cmd_SetComputeShader:
                {
                    device_context->CSSetShader(static_cast<ID3D11ComputeShader*>(const_cast<void*>(cmd.resource_ptr)), nullptr, 0);
                    m_profiler->m_rhi_bindings_shader_compute++;
                    break;
                }

				case RHI_Cmd_SetConstantBuffers:
				{
                    const void* resource_array[1] = { cmd.resource_ptr };

                    if (cmd._uint8 & RHI_Buffer_VertexShader)
                    {
                        device_context->VSSetConstantBuffers(
                            static_cast<UINT>(cmd.resource_start_slot),
                            static_cast<UINT>(cmd.resource_count),
                            reinterpret_cast<ID3D11Buffer* const*>(cmd.resource_count > 1 ? cmd.resource_ptr : &resource_array)
                        );
                    }

                    if (cmd._uint8 & RHI_Buffer_PixelShader)
                    {
                        device_context->PSSetConstantBuffers(
                            static_cast<UINT>(cmd.resource_start_slot),
                            static_cast<UINT>(cmd.resource_count),
                            reinterpret_cast<ID3D11Buffer* const*>(cmd.resource_count > 1 ? cmd.resource_ptr : &resource_array)
                        );
                    }

                    m_profiler->m_rhi_bindings_buffer_constant += cmd._uint8 & RHI_Buffer_VertexShader  ? 1 : 0;
                    m_profiler->m_rhi_bindings_buffer_constant += cmd._uint8 & RHI_Buffer_PixelShader   ? 1 : 0;
					break;
				}

				case RHI_Cmd_SetSamplers:
				{
                    if (cmd.resource_count > 1)
                    {
                        device_context->PSSetSamplers
                        (
                            static_cast<UINT>(cmd.resource_start_slot),
                            static_cast<UINT>(cmd.resource_count),
                            reinterpret_cast<ID3D11SamplerState* const*>(cmd.resource_ptr)
                        );
                    }
                    else
                    {
                        const void* resource_array[1] = { cmd.resource_ptr };
                        device_context->PSSetSamplers
                        (
                            static_cast<UINT>(cmd.resource_start_slot),
                            static_cast<UINT>(cmd.resource_count),
                            reinterpret_cast<ID3D11SamplerState* const*>(&resource_array)
                        );
                    }

					m_profiler->m_rhi_bindings_sampler++;
					break;
				}

				case RHI_Cmd_SetTextures:
				{
					if (cmd.resource_count > 1)
					{
						device_context->PSSetShaderResources
						(
							static_cast<UINT>(cmd.resource_start_slot),
							static_cast<UINT>(cmd.resource_count),
							reinterpret_cast<ID3D11ShaderResourceView* const*>(cmd.resource_ptr)
						);
					}
					else
					{
						const void* resource_array[1] = { cmd.resource_ptr };
						device_context->PSSetShaderResources
						(
							static_cast<UINT>(cmd.resource_start_slot),
							static_cast<UINT>(cmd.resource_count),
							reinterpret_cast<ID3D11ShaderResourceView* const*>(&resource_array)
						);
					}

					m_profiler->m_rhi_bindings_texture++;
					break;
				}

				case RHI_Cmd_SetRenderTargets:
				{
                    if (cmd.resource_count > 1)
                    {
                        device_context->OMSetRenderTargets
                        (
                            static_cast<UINT>(cmd.resource_count),
                            reinterpret_cast<ID3D11RenderTargetView* const*>(cmd.resource_ptr),
                            static_cast<ID3D11DepthStencilView*>(cmd.depth_stencil)
                        );
                    }
                    else
                    {
                        const void* resource_array[1] = { cmd.resource_ptr };
                        device_context->OMSetRenderTargets
                        (
                            static_cast<UINT>(cmd.resource_count),
                            reinterpret_cast<ID3D11RenderTargetView* const*>(&resource_array),
                            static_cast<ID3D11DepthStencilView*>(cmd.depth_stencil)
                        );
                    }

					m_profiler->m_rhi_bindings_render_target++;
					break;
				}

				case RHI_Cmd_ClearRenderTarget:
				{
					device_context->ClearRenderTargetView
					(
						static_cast<ID3D11RenderTargetView*>(const_cast<void*>(cmd.resource_ptr)),
						cmd._vector4.Data()
					);
					break;
				}

				case RHI_Cmd_ClearDepthStencil:
				{
					UINT clear_flags = 0;
					clear_flags |= (cmd._uint32 & RHI_Clear_Depth)	    ? D3D11_CLEAR_DEPTH : 0;
					clear_flags |= (cmd._uint32 & RHI_Clear_Stencil)	? D3D11_CLEAR_STENCIL : 0;

					device_context->ClearDepthStencilView
					(
						static_cast<ID3D11DepthStencilView*>(cmd.depth_stencil),
						clear_flags,
						static_cast<FLOAT>(cmd._float),
						static_cast<UINT8>(cmd._uint8)
					);

					break;
				}
			}
		}

		Clear();
		return true;
	}

    void RHI_CommandList::Flush()
    {
        
    }

	RHI_Command& RHI_CommandList::GetCmd()
	{
		// Grow capacity if needed
		if (m_command_count >= m_commands.size())
		{
			const auto new_size = m_command_count + 100;
			m_commands.reserve(new_size);
			m_commands.resize(new_size);
			LOG_WARNING("Command list has grown to fit %d commands. Consider making the capacity larger to avoid re-allocations.", m_command_count + 1);
		}

		m_command_count++;
		return m_commands[m_command_count - 1];	
	}

	void RHI_CommandList::Clear()
	{
		for (uint32_t cmd_index = 0; cmd_index < m_command_count; cmd_index++)
		{
			auto& cmd = m_commands[cmd_index];
			cmd.Clear();
		}

        m_pipeline_state.Clear();
		m_command_count = 0;
	}
}

#endif
