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
        m_swap_chain            = swap_chain;
        m_renderer              = context->GetSubsystem<Renderer>().get();
        m_profiler              = context->GetSubsystem<Profiler>().get();
        m_rhi_device            = m_renderer->GetRhiDevice().get();
        m_rhi_pipeline_cache    = m_renderer->GetPipelineCache().get();
	}

	RHI_CommandList::~RHI_CommandList() = default;

	bool RHI_CommandList::Begin(const string& pass_name, RHI_Cmd_Type type /*= RHI_Cmd_Begin*/)
	{
        // Profile
        if (m_profiler)
        {
            m_profiler->TimeBlockStart(pass_name, true, true);
        }

        // Mark
        RHI_Context* rhi_context = m_rhi_device->GetContextRhi();
        if (rhi_context->debug)
        {
            m_rhi_device->GetContextRhi()->annotation->BeginEvent(FileSystem::StringToWstring(pass_name).c_str());
        }

        if (m_pipeline_state.blend_state)                                           SetBlendState(m_pipeline_state.blend_state);
        if (m_pipeline_state.depth_stencil_state)                                   SetDepthStencilState(m_pipeline_state.depth_stencil_state);
        if (m_pipeline_state.rasterizer_state)                                      SetRasterizerState(m_pipeline_state.rasterizer_state);
        if (m_pipeline_state.shader_vertex)                                         SetInputLayout(m_pipeline_state.shader_vertex->GetInputLayout());
        if (m_pipeline_state.shader_vertex)                                         SetShaderVertex(m_pipeline_state.shader_vertex);
        if (m_pipeline_state.shader_pixel)                                          SetShaderPixel(m_pipeline_state.shader_pixel);
        if (m_pipeline_state.viewport.IsDefined())                                  SetViewport(m_pipeline_state.viewport);
        if (m_pipeline_state.primitive_topology != RHI_PrimitiveTopology_Unknown)   SetPrimitiveTopology(m_pipeline_state.primitive_topology);
        if (m_pipeline_state.render_target_swapchain)                               SetRenderTarget(m_pipeline_state.render_target_swapchain->GetRenderTargetView(), m_pipeline_state.render_target_depth_texture ? m_pipeline_state.render_target_depth_texture->GetResource_DepthStencil() : nullptr);
        if (m_pipeline_state.render_target_color_texture)                           SetRenderTarget(m_pipeline_state.render_target_color_texture->GetResource_RenderTarget(), m_pipeline_state.render_target_depth_texture ? m_pipeline_state.render_target_depth_texture->GetResource_DepthStencil() : nullptr);

        return true;
	}

	void RHI_CommandList::End()
	{
        // Mark
        RHI_Context* rhi_context = m_rhi_device->GetContextRhi();
        if (rhi_context->debug)
        {
            rhi_context->annotation->EndEvent();
        }

        // Profile
        if (m_profiler)
        {
            m_profiler->TimeBlockEnd();
        }
	}

	void RHI_CommandList::Draw(const uint32_t vertex_count)
	{
        m_rhi_device->GetContextRhi()->device_context->Draw(static_cast<UINT>(vertex_count), 0);
        m_profiler->m_rhi_draw_calls++;
	}

	void RHI_CommandList::DrawIndexed(const uint32_t index_count, const uint32_t index_offset, const uint32_t vertex_offset)
	{
        m_rhi_device->GetContextRhi()->device_context->DrawIndexed
        (
            static_cast<UINT>(index_count),
            static_cast<UINT>(index_offset),
            static_cast<INT>(vertex_offset)
        );

        m_profiler->m_rhi_draw_calls++;
	}

	void RHI_CommandList::SetViewport(const RHI_Viewport& viewport)
	{
        D3D11_VIEWPORT d3d11_viewport   = {};
        d3d11_viewport.TopLeftX         = viewport.x;
        d3d11_viewport.TopLeftY         = viewport.y;
        d3d11_viewport.Width            = viewport.width;
        d3d11_viewport.Height           = viewport.height;
        d3d11_viewport.MinDepth         = viewport.depth_min;
        d3d11_viewport.MaxDepth         = viewport.depth_max;

        m_rhi_device->GetContextRhi()->device_context->RSSetViewports(1, &d3d11_viewport);
	}

	void RHI_CommandList::SetScissorRectangle(const Math::Rectangle& scissor_rectangle)
	{
        const auto left                     = scissor_rectangle.x;
        const auto top                      = scissor_rectangle.y;
        const auto right                    = scissor_rectangle.x + scissor_rectangle.width;
        const auto bottom                   = scissor_rectangle.y + scissor_rectangle.height;
        const D3D11_RECT d3d11_rectangle    = { static_cast<LONG>(left), static_cast<LONG>(top), static_cast<LONG>(right), static_cast<LONG>(bottom) };

        m_rhi_device->GetContextRhi()->device_context->RSSetScissorRects(1, &d3d11_rectangle);
	}

	void RHI_CommandList::SetPrimitiveTopology(const RHI_PrimitiveTopology_Mode primitive_topology)
	{
        m_rhi_device->GetContextRhi()->device_context->IASetPrimitiveTopology(d3d11_primitive_topology[primitive_topology]);
	}

	void RHI_CommandList::SetInputLayout(const RHI_InputLayout* input_layout)
	{
		if (!input_layout || !input_layout->GetResource())
		{
			LOG_ERROR_INVALID_PARAMETER();
			return;
		}

        m_rhi_device->GetContextRhi()->device_context->IASetInputLayout(static_cast<ID3D11InputLayout*>(const_cast<void*>(input_layout->GetResource())));
	}

	void RHI_CommandList::SetDepthStencilState(const RHI_DepthStencilState* depth_stencil_state)
	{
		if (!depth_stencil_state || !depth_stencil_state->GetResource())
		{
			LOG_ERROR_INVALID_PARAMETER();
			return;
		}

        m_rhi_device->GetContextRhi()->device_context->OMSetDepthStencilState(static_cast<ID3D11DepthStencilState*>(const_cast<void*>(depth_stencil_state->GetResource())), 1);
	}

	void RHI_CommandList::SetRasterizerState(const RHI_RasterizerState* rasterizer_state)
	{
		if (!rasterizer_state || !rasterizer_state->GetResource())
		{
			LOG_ERROR_INVALID_PARAMETER();
			return;
		}

        m_rhi_device->GetContextRhi()->device_context->RSSetState(static_cast<ID3D11RasterizerState*>(const_cast<void*>(rasterizer_state->GetResource())));
	}

	void RHI_CommandList::SetBlendState(const RHI_BlendState* blend_state)
	{
		if (!blend_state || !blend_state->GetResource())
		{
			LOG_ERROR_INVALID_PARAMETER();
			return;
		}

        float blendFactor = blend_state->GetBlendFactor();
        FLOAT blend_factor[4] = { blendFactor, blendFactor, blendFactor, blendFactor };

        m_rhi_device->GetContextRhi()->device_context->OMSetBlendState
        (
            static_cast<ID3D11BlendState*>(const_cast<void*>(blend_state->GetResource())),
            blend_factor,
            0xffffffff
        );
	}

	void RHI_CommandList::SetBufferVertex(const RHI_VertexBuffer* buffer)
	{
		if (!buffer || !buffer->GetResource())
		{
			LOG_ERROR_INVALID_PARAMETER();
			return;
		}

        auto ptr        = static_cast<ID3D11Buffer*>(buffer->GetResource());
        auto stride     = buffer->GetStride();
        uint32_t offset = 0;

        m_rhi_device->GetContextRhi()->device_context->IASetVertexBuffers(0, 1, &ptr, &stride, &offset);
        m_profiler->m_rhi_bindings_buffer_vertex++;
	}

	void RHI_CommandList::SetBufferIndex(const RHI_IndexBuffer* buffer)
	{
		if (!buffer || !buffer->GetResource())
		{
			LOG_ERROR_INVALID_PARAMETER();
			return;
		}

        m_rhi_device->GetContextRhi()->device_context->IASetIndexBuffer
        (
            static_cast<ID3D11Buffer*>(buffer->GetResource()),
            buffer->Is16Bit() ? DXGI_FORMAT_R16_UINT : DXGI_FORMAT_R32_UINT,
            0
        );

        m_profiler->m_rhi_bindings_buffer_index++;
	}

	void RHI_CommandList::SetShaderVertex(const RHI_Shader* shader)
	{
		if (!shader || !shader->GetResource())
		{
			LOG_ERROR_INVALID_PARAMETER();
			return;
		}

        m_rhi_device->GetContextRhi()->device_context->VSSetShader(static_cast<ID3D11VertexShader*>(const_cast<void*>(shader->GetResource())), nullptr, 0);
        m_profiler->m_rhi_bindings_shader_vertex++;
	}

	void RHI_CommandList::SetShaderPixel(const RHI_Shader* shader)
	{
        void* resource = shader ? shader->GetResource() : nullptr;
        m_rhi_device->GetContextRhi()->device_context->PSSetShader(static_cast<ID3D11PixelShader*>(const_cast<void*>(resource)), nullptr, 0);
        m_profiler->m_rhi_bindings_shader_pixel++;
	}

    void RHI_CommandList::SetShaderCompute(const RHI_Shader* shader)
    {
        if (shader && !shader->GetResource())
        {
            LOG_WARNING("%s hasn't compiled", shader->GetName().c_str());
            return;
        }

        m_rhi_device->GetContextRhi()->device_context->CSSetShader(static_cast<ID3D11ComputeShader*>(const_cast<void*>(shader->GetResource())), nullptr, 0);
        m_profiler->m_rhi_bindings_shader_compute++;
    }

    void RHI_CommandList::SetConstantBuffer(const uint32_t slot, uint8_t scope, RHI_ConstantBuffer* constant_buffer)
    {
        void* resource_ptr              = constant_buffer ? constant_buffer->GetResource() : nullptr;
        const void* resource_array[1]   = { resource_ptr };
        uint32_t resource_count         = 1;

        if (scope & RHI_Buffer_VertexShader)
        {
            m_rhi_device->GetContextRhi()->device_context->VSSetConstantBuffers(
                static_cast<UINT>(slot),
                static_cast<UINT>(resource_count),
                reinterpret_cast<ID3D11Buffer* const*>(resource_count > 1 ? resource_ptr : &resource_array)
            );
        }

        if (scope & RHI_Buffer_PixelShader)
        {
            m_rhi_device->GetContextRhi()->device_context->PSSetConstantBuffers(
                static_cast<UINT>(slot),
                static_cast<UINT>(resource_count),
                reinterpret_cast<ID3D11Buffer* const*>(resource_count > 1 ? resource_ptr : &resource_array)
            );
        }

        m_profiler->m_rhi_bindings_buffer_constant += scope & RHI_Buffer_VertexShader   ? 1 : 0;
        m_profiler->m_rhi_bindings_buffer_constant += scope & RHI_Buffer_PixelShader    ? 1 : 0;
    }

    void RHI_CommandList::SetSampler(const uint32_t slot, RHI_Sampler* sampler)
    {
        uint32_t resource_start_slot    = slot;
        void* resource_ptr              = sampler ? sampler->GetResource() : nullptr;
        uint32_t resource_count         = 1;

        if (resource_count > 1)
        {
            m_rhi_device->GetContextRhi()->device_context->PSSetSamplers
            (
                static_cast<UINT>(resource_start_slot),
                static_cast<UINT>(resource_count),
                reinterpret_cast<ID3D11SamplerState* const*>(resource_ptr)
            );
        }
        else
        {
            const void* resource_array[1] = { resource_ptr };
            m_rhi_device->GetContextRhi()->device_context->PSSetSamplers
            (
                static_cast<UINT>(resource_start_slot),
                static_cast<UINT>(resource_count),
                reinterpret_cast<ID3D11SamplerState* const*>(&resource_array)
            );
        }

        m_profiler->m_rhi_bindings_sampler++;
    }

    void RHI_CommandList::SetTexture(const uint32_t slot, RHI_Texture* texture)
    {
		uint32_t resource_start_slot    = slot;
		void* resource_ptr              = texture ? texture->GetResource_Texture() : nullptr;
		uint32_t resource_count         = 1;

        if (resource_count > 1)
        {
            m_rhi_device->GetContextRhi()->device_context->PSSetShaderResources
            (
                static_cast<UINT>(resource_start_slot),
                static_cast<UINT>(resource_count),
                reinterpret_cast<ID3D11ShaderResourceView* const*>(resource_ptr)
            );
        }
        else
        {
            const void* resource_array[1] = { resource_ptr };
            m_rhi_device->GetContextRhi()->device_context->PSSetShaderResources
            (
                static_cast<UINT>(resource_start_slot),
                static_cast<UINT>(resource_count),
                reinterpret_cast<ID3D11ShaderResourceView* const*>(&resource_array)
            );
        }

        m_profiler->m_rhi_bindings_texture++;
	}

    void RHI_CommandList::SetRenderTargets(const void* render_targets, uint32_t render_target_count, void* depth_stencil /*= nullptr*/)
    {
        if (render_target_count > 1)
        {
            m_rhi_device->GetContextRhi()->device_context->OMSetRenderTargets
            (
                static_cast<UINT>(render_target_count),
                reinterpret_cast<ID3D11RenderTargetView* const*>(render_targets),
                static_cast<ID3D11DepthStencilView*>(depth_stencil)
            );
        }
        else
        {
            const void* resource_array[1] = { render_targets };
            m_rhi_device->GetContextRhi()->device_context->OMSetRenderTargets
            (
                static_cast<UINT>(render_target_count),
                reinterpret_cast<ID3D11RenderTargetView* const*>(&resource_array),
                static_cast<ID3D11DepthStencilView*>(depth_stencil)
            );
        }

        m_profiler->m_rhi_bindings_render_target++;
    }

	void RHI_CommandList::ClearRenderTarget(void* render_target, const Vector4& color)
	{
        if (!render_target)
        {
            LOG_ERROR("Provided render_target is null");
            return;
        }

        m_rhi_device->GetContextRhi()->device_context->ClearRenderTargetView
        (
            static_cast<ID3D11RenderTargetView*>(const_cast<void*>(render_target)),
            color.Data()
        );
	}

	void RHI_CommandList::ClearDepthStencil(void* depth_stencil, const uint32_t flags, const float depth, const uint8_t stencil /*= 0*/)
	{
		if (!depth_stencil)
		{
			LOG_ERROR("Provided depth stencil is null");
			return;
		}

        UINT clear_flags = 0;
        clear_flags |= (flags & RHI_Clear_Depth)    ? D3D11_CLEAR_DEPTH     : 0;
        clear_flags |= (flags & RHI_Clear_Stencil)  ? D3D11_CLEAR_STENCIL   : 0;

        m_rhi_device->GetContextRhi()->device_context->ClearDepthStencilView
        (
            static_cast<ID3D11DepthStencilView*>(depth_stencil),
            clear_flags,
            static_cast<FLOAT>(depth),
            static_cast<UINT8>(stencil)
        );
	}

	bool RHI_CommandList::Submit()
	{
        m_pipeline_state.Clear();
		return true;
	}

    void RHI_CommandList::Flush()
    {
        
    }
}

#endif
