/*
Copyright(c) 2016-2020 Panos Karabelas

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

//= INCLUDES ========================
#include "Spartan.h"
#include "../RHI_Implementation.h"
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
#include "../RHI_SwapChain.h"
#include "../RHI_PipelineState.h"
#include "../RHI_DescriptorCache.h"
#include "../../Profiling/Profiler.h"
#include "../../Rendering/Renderer.h"
//===================================

//= NAMESPACES ===============
using namespace std;
using namespace Spartan::Math;
//============================

namespace Spartan
{
    bool RHI_CommandList::memory_query_support = true;

	RHI_CommandList::RHI_CommandList(uint32_t index, RHI_SwapChain* swap_chain, Context* context)
	{
        m_swap_chain        = swap_chain;
        m_renderer          = context->GetSubsystem<Renderer>();
        m_profiler          = context->GetSubsystem<Profiler>();
        m_rhi_device        = m_renderer->GetRhiDevice().get();
        m_pipeline_cache    = m_renderer->GetPipelineCache();
        m_descriptor_cache  = m_renderer->GetDescriptorCache();
        m_timestamps.fill(0);
	}

	RHI_CommandList::~RHI_CommandList() = default;

    bool RHI_CommandList::Begin()
    {
        return true;
    }

    bool RHI_CommandList::Stop()
    {
        return true;
    }

    bool RHI_CommandList::Submit()
    {
        return true;
    }

    bool RHI_CommandList::Wait()
    {
        return true;
    }

    bool RHI_CommandList::Reset()
    {
        return true;
    }

    bool RHI_CommandList::BeginRenderPass(RHI_PipelineState& pipeline_state)
    {
        if (!pipeline_state.IsValid())
        {
            LOG_ERROR("Invalid pipeline state");
            return false;
        }

        // Keep a local pointer for convenience 
        m_pipeline_state = &pipeline_state;

        // Start marker and profiler (if enabled)
        Timeblock_Start(m_pipeline_state);

        ID3D11DeviceContext* device_context = m_rhi_device->GetContextRhi()->device_context;

        // Input layout
        {
            // New state
            ID3D11InputLayout* input_layout = static_cast<ID3D11InputLayout*>(pipeline_state.shader_vertex ? pipeline_state.shader_vertex->GetInputLayout()->GetResource() : nullptr);

            // Current state
            ID3D11InputLayout* input_layout_set = nullptr;
            device_context->IAGetInputLayout(&input_layout_set);

            // Set if dirty
            if (input_layout_set != input_layout)
            {
                device_context->IASetInputLayout(input_layout);
            }
        }

        // Vertex shader
        {
            // New state
            ID3D11VertexShader* shader = static_cast<ID3D11VertexShader*>(pipeline_state.shader_vertex ? pipeline_state.shader_vertex->GetResource() : nullptr);

            // Current state
            ID3D11VertexShader* set_shader = nullptr; UINT instance_count = 256; ID3D11ClassInstance* instances[256];
            device_context->VSGetShader(&set_shader, instances, &instance_count);

            // Set if dirty
            if (set_shader != shader)
            {
                device_context->VSSetShader(shader, nullptr, 0);

                m_profiler->m_rhi_bindings_shader_vertex++;
            }
        }

        // Pixel shader
        {
            // New state
            ID3D11PixelShader* shader = static_cast<ID3D11PixelShader*>(pipeline_state.shader_pixel ? pipeline_state.shader_pixel->GetResource() : nullptr);

            // Current state
            ID3D11PixelShader* set_shader = nullptr; UINT instance_count = 256; ID3D11ClassInstance* instances[256];
            device_context->PSGetShader(&set_shader, instances, &instance_count);

            // Set if dirty
            if (set_shader != shader)
            {
                device_context->PSSetShader(shader, nullptr, 0);

                m_profiler->m_rhi_bindings_shader_pixel++;
            }
        }

        // Compute shader
        {
            // New state
            ID3D11ComputeShader* shader = static_cast<ID3D11ComputeShader*>(pipeline_state.shader_compute ? pipeline_state.shader_compute->GetResource() : nullptr);

            // Current state
            ID3D11ComputeShader* set_shader = nullptr; UINT instance_count = 256; ID3D11ClassInstance* instances[256];
            device_context->CSGetShader(&set_shader, instances, &instance_count);

            // Set if dirty
            if (set_shader != shader)
            {
                device_context->CSSetShader(shader, nullptr, 0);

                m_profiler->m_rhi_bindings_shader_compute++;
            }
        }

        // Blend state
        {
            // New state
            ID3D11BlendState* blend_state_set       = nullptr;
            std::array<FLOAT, 4> blend_factor_set   = { 0.0f };
            UINT mask_set                           = 0;
            device_context->OMGetBlendState(&blend_state_set, blend_factor_set.data(), &mask_set);

            // Current state
            ID3D11BlendState* blend_state       = static_cast<ID3D11BlendState*>(pipeline_state.blend_state ? pipeline_state.blend_state->GetResource() : nullptr);
            const float blendFactor             = pipeline_state.blend_state ? pipeline_state.blend_state->GetBlendFactor() : 0.0f;
            std::array<FLOAT, 4> blend_factor   = { blendFactor, blendFactor, blendFactor, blendFactor };
            UINT mask                           = 0;

            // Set if dirty
            if (blend_state_set != blend_state || blend_factor_set != blend_factor || mask_set != mask)
            {
                device_context->OMSetBlendState(blend_state, blend_factor.data(), 0xffffffff);
            }
        }

        // Depth stencil state
        {
            // New state
            ID3D11DepthStencilState* depth_stencil_state = static_cast<ID3D11DepthStencilState*>(pipeline_state.depth_stencil_state ? pipeline_state.depth_stencil_state->GetResource() : nullptr);

            // Current state
            ID3D11DepthStencilState* depth_stencil_state_set = nullptr;
            UINT stencil_ref = 0;
            device_context->OMGetDepthStencilState(&depth_stencil_state_set, &stencil_ref);

            // Set if dirty
            if (depth_stencil_state_set != depth_stencil_state)
            {
                device_context->OMSetDepthStencilState(depth_stencil_state, 1);
            }
        }

        // Rasterizer state
        {
            // New state
            ID3D11RasterizerState* rasterizer_state = static_cast<ID3D11RasterizerState*>(pipeline_state.rasterizer_state ? pipeline_state.rasterizer_state->GetResource() : nullptr);

            // Current state
            ID3D11RasterizerState* rasterizer_state_set = nullptr;

            // Set if dirty
            device_context->RSGetState(&rasterizer_state_set);
            if (rasterizer_state_set != rasterizer_state)
            {
                device_context->RSSetState(rasterizer_state);
            }
        }

        // Primitive topology
        if (pipeline_state.primitive_topology != RHI_PrimitiveTopology_Unknown)
        {
            // New state
            D3D11_PRIMITIVE_TOPOLOGY topology = d3d11_primitive_topology[pipeline_state.primitive_topology];

            // Current state
            D3D11_PRIMITIVE_TOPOLOGY topology_set;
            device_context->IAGetPrimitiveTopology(&topology_set);

            // Set if dirty
            if (topology_set != topology)
            {
                device_context->IASetPrimitiveTopology(d3d11_primitive_topology[pipeline_state.primitive_topology]);
            }
        }

        // Render target(s)
        {
            // Detect depth stencil targets
            ID3D11DepthStencilView* depth_stencil = nullptr;
            if (pipeline_state.render_target_depth_texture)
            {
                if (pipeline_state.render_target_depth_texture_read_only)
                {
                    depth_stencil = static_cast<ID3D11DepthStencilView*>(pipeline_state.render_target_depth_texture->Get_Resource_View_DepthStencilReadOnly(pipeline_state.render_target_depth_stencil_texture_array_index));
                }
                else
                {
                    depth_stencil = static_cast<ID3D11DepthStencilView*>(pipeline_state.render_target_depth_texture->Get_Resource_View_DepthStencil(pipeline_state.render_target_depth_stencil_texture_array_index));
                }
            }

            // Detect color targets
            std::array<ID3D11RenderTargetView*, state_max_render_target_count> render_targets = { nullptr };
            {
                // Swapchain
                if (pipeline_state.render_target_swapchain)
                {
                    render_targets[0] = { static_cast<ID3D11RenderTargetView*>(pipeline_state.render_target_swapchain->Get_Resource_View_RenderTarget()) };
                }
                // Textures
                else
                {
                    for (auto i = 0; i < state_max_render_target_count; i++)
                    {
                        if (pipeline_state.render_target_color_textures[i])
                        {
                            ID3D11RenderTargetView* rt = static_cast<ID3D11RenderTargetView*>(pipeline_state.render_target_color_textures[i]->Get_Resource_View_RenderTarget(pipeline_state.render_target_color_texture_array_index));
                            render_targets[i] = rt;
                        }
                    }
                }
            }

            // Set render targets
            {
                // Current state
                std::array<ID3D11RenderTargetView*, state_max_render_target_count> set_render_target_views = { nullptr };
                ID3D11DepthStencilView* set_depth_stencil_view = nullptr;
                device_context->OMGetRenderTargets(state_max_render_target_count, set_render_target_views.data(), &set_depth_stencil_view);

                // Set if dirty
                if (render_targets != set_render_target_views || depth_stencil != set_depth_stencil_view)
                {
                    UINT render_target_count = 0;
                    for (ID3D11RenderTargetView* rt : render_targets)
                    {
                        if (rt)
                        {
                            render_target_count++;
                        }
                    }

                    device_context->OMSetRenderTargets
                    (
                        render_target_count,
                        reinterpret_cast<ID3D11RenderTargetView* const*>(render_targets.data()),
                        depth_stencil
                    );

                    m_profiler->m_rhi_bindings_render_target++;
                }
            }
        }

        // Unordered access view(s)
        if (pipeline_state.unordered_access_view)
        {
            const void* resource_array[1] = { pipeline_state.unordered_access_view->Get_Resource_View_UnorderedAccess() };
            device_context->CSSetUnorderedAccessViews(0, 1, reinterpret_cast<ID3D11UnorderedAccessView* const*>(&resource_array), nullptr);
            m_profiler->m_rhi_bindings_render_target++;
        }

        // Viewport
        if (pipeline_state.viewport.IsDefined())
        {
            SetViewport(pipeline_state.viewport);
        }

        // Clear render target(s)
        Clear(pipeline_state);

        m_renderer->SetGlobalSamplersAndConstantBuffers(this);

        m_profiler->m_rhi_bindings_pipeline++;

        return true;
	}

	bool RHI_CommandList::EndRenderPass()
	{
        // End marker and profiler (if enabled)
        Timeblock_End(m_pipeline_state);
        return true;
	}

    void RHI_CommandList::Clear(RHI_PipelineState& pipeline_state)
    {
        // Color
        for (uint8_t i = 0; i < state_max_render_target_count; i++)
        {
            if (pipeline_state.clear_color[i] != state_color_load && pipeline_state.clear_color[i] != state_color_dont_care)
            {
                if (pipeline_state.render_target_swapchain)
                {
                    m_rhi_device->GetContextRhi()->device_context->ClearRenderTargetView
                    (
                        static_cast<ID3D11RenderTargetView*>(const_cast<void*>(pipeline_state.render_target_swapchain->Get_Resource_View_RenderTarget())),
                        pipeline_state.clear_color[i].Data()
                    );
                }
                else if (pipeline_state.render_target_color_textures[i])
                {
                    m_rhi_device->GetContextRhi()->device_context->ClearRenderTargetView
                    (
                        static_cast<ID3D11RenderTargetView*>(const_cast<void*>(pipeline_state.render_target_color_textures[i]->Get_Resource_View_RenderTarget(pipeline_state.render_target_color_texture_array_index))),
                        pipeline_state.clear_color[i].Data()
                    );
                }
            }
        }

        // Depth-stencil
        if (pipeline_state.render_target_depth_texture)
        {
            UINT clear_flags = 0;
            clear_flags |= (pipeline_state.clear_depth      != state_depth_load     && pipeline_state.clear_depth   != state_depth_dont_care)   ? D3D11_CLEAR_DEPTH     : 0;
            clear_flags |= (pipeline_state.clear_stencil    != state_stencil_load   && pipeline_state.clear_stencil != state_stencil_dont_care) ? D3D11_CLEAR_STENCIL   : 0;
            if (clear_flags != 0)
            {
                m_rhi_device->GetContextRhi()->device_context->ClearDepthStencilView
                (
                    static_cast<ID3D11DepthStencilView*>(pipeline_state.render_target_depth_texture->Get_Resource_View_DepthStencil(pipeline_state.render_target_depth_stencil_texture_array_index)),
                    clear_flags,
                    static_cast<FLOAT>(pipeline_state.clear_depth),
                    static_cast<UINT8>(pipeline_state.clear_stencil)
                );
            }
        }
    }

    bool RHI_CommandList::Draw(const uint32_t vertex_count)
    {
        m_rhi_device->GetContextRhi()->device_context->Draw(static_cast<UINT>(vertex_count), 0);
        m_profiler->m_rhi_draw_calls++;

        return true;
	}

    bool RHI_CommandList::DrawIndexed(const uint32_t index_count, const uint32_t index_offset, const uint32_t vertex_offset)
    {
        m_rhi_device->GetContextRhi()->device_context->DrawIndexed
        (
            static_cast<UINT>(index_count),
            static_cast<UINT>(index_offset),
            static_cast<INT>(vertex_offset)
        );

        m_profiler->m_rhi_draw_calls++;

        return true;
	}

    void RHI_CommandList::Dispatch(uint32_t x, uint32_t y, uint32_t z /*= 1*/) const
    {
        ID3D11Device5* device                   = m_rhi_device->GetContextRhi()->device;
        ID3D11DeviceContext4* device_context    = m_rhi_device->GetContextRhi()->device_context;

        // Sync objects
        static uint32_t fence_value = 0;
        static ID3D11Fence* fence   = nullptr;
        static HANDLE fence_event   = nullptr;
        if (!fence)
        {
            d3d11_utility::error_check(device->CreateFence(0, D3D11_FENCE_FLAG_NONE, __uuidof(ID3D11Fence), reinterpret_cast<void**>(&fence)));
        }

        // Dispatch
        ++fence_value;
        device_context->Dispatch(x, y, z);
        d3d11_utility::error_check(device_context->Signal(fence, fence_value));
        d3d11_utility::error_check(fence->SetEventOnCompletion(fence_value, fence_event));

        // Wait
        WaitForSingleObject(fence_event, INFINITE);
    }

	void RHI_CommandList::SetViewport(const RHI_Viewport& viewport) const
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

	void RHI_CommandList::SetScissorRectangle(const Math::Rectangle& scissor_rectangle) const
    {
        const D3D11_RECT d3d11_rectangle = { static_cast<LONG>(scissor_rectangle.left), static_cast<LONG>(scissor_rectangle.top), static_cast<LONG>(scissor_rectangle.right), static_cast<LONG>(scissor_rectangle.bottom) };

        m_rhi_device->GetContextRhi()->device_context->RSSetScissorRects(1, &d3d11_rectangle);
	}

	void RHI_CommandList::SetBufferVertex(const RHI_VertexBuffer* buffer, const uint64_t offset /*= 0*/)
    {
		if (!buffer || !buffer->GetResource())
		{
			LOG_ERROR_INVALID_PARAMETER();
			return;
		}

        ID3D11Buffer* vertex_buffer         = static_cast<ID3D11Buffer*>(buffer->GetResource());
        UINT stride                         = buffer->GetStride();
        UINT offsets[]                      = { static_cast<UINT>(offset) };
        ID3D11DeviceContext* device_context = m_rhi_device->GetContextRhi()->device_context;

        // Get currently set buffer
        ID3D11Buffer* set_buffer    = nullptr;
        UINT set_stride             = buffer->GetStride();
        UINT set_offset             = 0;
        device_context->IAGetVertexBuffers(0, 1, &set_buffer, &set_stride, &set_offset);

        // Skip if already set
        if (set_buffer == vertex_buffer && set_offset == offset)
            return;

        // Set
        device_context->IASetVertexBuffers(0, 1, &vertex_buffer, &stride, offsets);
        m_profiler->m_rhi_bindings_buffer_vertex++;
	}

	void RHI_CommandList::SetBufferIndex(const RHI_IndexBuffer* buffer, const uint64_t offset /*= 0*/)
    {
		if (!buffer || !buffer->GetResource())
		{
			LOG_ERROR_INVALID_PARAMETER();
			return;
		}

        ID3D11Buffer* index_buffer          = static_cast<ID3D11Buffer*>(buffer->GetResource());
        const DXGI_FORMAT format            = buffer->Is16Bit() ? DXGI_FORMAT_R16_UINT : DXGI_FORMAT_R32_UINT;
        ID3D11DeviceContext* device_context = m_rhi_device->GetContextRhi()->device_context;

        // Get currently set buffer
        ID3D11Buffer* set_buffer    = nullptr;
        DXGI_FORMAT set_format      = DXGI_FORMAT_UNKNOWN;
        UINT set_offset             = 0;
        device_context->IAGetIndexBuffer(&set_buffer, &set_format, &set_offset);

        // Skip if already set
        if (set_buffer == index_buffer && set_offset == offset)
            return;

        // Set
        device_context->IASetIndexBuffer(index_buffer, format, static_cast<UINT>(offset));
        m_profiler->m_rhi_bindings_buffer_index++;
	}

    bool RHI_CommandList::SetConstantBuffer(const uint32_t slot, const uint8_t scope, RHI_ConstantBuffer* constant_buffer) const
    {
        void* buffer                        = static_cast<ID3D11Buffer*>(constant_buffer ? constant_buffer->GetResource() : nullptr);
        const void* buffer_array[1]         = { buffer };
        const UINT range                    = 1;
        ID3D11DeviceContext* device_context = m_rhi_device->GetContextRhi()->device_context;

        if (scope & RHI_Shader_Vertex)
        {
            // Set only if not set
            ID3D11Buffer* set_buffer = nullptr;
            device_context->VSGetConstantBuffers(slot, range, &set_buffer);
            if (set_buffer != buffer)
            {
                device_context->VSSetConstantBuffers(slot, range, reinterpret_cast<ID3D11Buffer* const*>(range > 1 ? buffer : &buffer_array));
            }
        }

        if (scope & RHI_Shader_Pixel)
        {
            // Set only if not set
            ID3D11Buffer* set_buffer = nullptr;
            device_context->PSGetConstantBuffers(slot, range, &set_buffer);
            if (set_buffer != buffer)
            {
                device_context->PSSetConstantBuffers(slot, range, reinterpret_cast<ID3D11Buffer* const*>(range > 1 ? buffer : &buffer_array));
            }
        }

        if (scope & RHI_Shader_Compute)
        {
            // Set only if not set
            ID3D11Buffer* set_buffer = nullptr;
            device_context->CSGetConstantBuffers(slot, range, &set_buffer);
            if (set_buffer != buffer)
            {
                device_context->CSSetConstantBuffers(slot, range, reinterpret_cast<ID3D11Buffer* const*>(range > 1 ? buffer : &buffer_array));
            }
        }

        m_profiler->m_rhi_bindings_buffer_constant += scope & RHI_Shader_Vertex   ? 1 : 0;
        m_profiler->m_rhi_bindings_buffer_constant += scope & RHI_Shader_Pixel    ? 1 : 0;
        m_profiler->m_rhi_bindings_buffer_constant += scope & RHI_Shader_Compute  ? 1 : 0;

        return true;
    }

    void RHI_CommandList::SetSampler(const uint32_t slot, RHI_Sampler* sampler) const
    {
        const UINT start_slot               = slot;
        const UINT range                    = 1;
        void* resource_sampler              = sampler ? sampler->GetResource() : nullptr;
        ID3D11DeviceContext* device_context = m_rhi_device->GetContextRhi()->device_context;

        // Skip if already set
        ID3D11SamplerState* set_sampler = nullptr;
        device_context->PSGetSamplers(slot, range, &set_sampler);
        if (set_sampler == resource_sampler)
            return;

        if (range > 1)
        {
            device_context->PSSetSamplers(start_slot, range, reinterpret_cast<ID3D11SamplerState* const*>(resource_sampler));
        }
        else
        {
            const void* sampler_array[1] = { resource_sampler };
            device_context->PSSetSamplers(start_slot, range, reinterpret_cast<ID3D11SamplerState* const*>(&sampler_array));
        }

        m_profiler->m_rhi_bindings_sampler++;
    }

    void RHI_CommandList::SetTexture(const uint32_t slot, RHI_Texture* texture, const uint8_t scope /*= RHI_Shader_Pixel*/)
    {
        const UINT start_slot               = slot;
        const UINT range                    = 1;
        void* resource_texture              = texture ? texture->Get_Resource_View() : nullptr;
        ID3D11DeviceContext* device_context = m_rhi_device->GetContextRhi()->device_context;

        // Skip if already set
        ID3D11ShaderResourceView* set_texture = nullptr;
        device_context->PSGetShaderResources(slot, range, &set_texture);
        if (set_texture == resource_texture)
            return;

        if (range > 1)
        {
            if (scope & RHI_Shader_Pixel)
            {
                device_context->PSSetShaderResources(start_slot, range, reinterpret_cast<ID3D11ShaderResourceView* const*>(resource_texture));
            }
            else if (scope & RHI_Shader_Compute)
            {
                device_context->CSSetShaderResources(start_slot, range, reinterpret_cast<ID3D11ShaderResourceView* const*>(resource_texture));
            }
        }
        else
        {
            const void* resource_array[1] = { resource_texture };

            if (scope & RHI_Shader_Pixel)
            {
                device_context->PSSetShaderResources(start_slot, range, reinterpret_cast<ID3D11ShaderResourceView* const*>(&resource_array)); 
            }
            else if (scope & RHI_Shader_Compute)
            {
                device_context->CSSetShaderResources(start_slot, range, reinterpret_cast<ID3D11ShaderResourceView* const*>(&resource_array));
            }
        }

        m_profiler->m_rhi_bindings_texture++;
	}

    bool RHI_CommandList::Timestamp_Start(void* query_disjoint /*= nullptr*/, void* query_start /*= nullptr*/)
    {
        if (!query_disjoint || !query_start)
        {
            LOG_ERROR_INVALID_PARAMETER();
            return false;
        }

        RHI_Context* rhi_context = m_rhi_device->GetContextRhi();
        if (!rhi_context->device_context)
        {
            LOG_ERROR_INVALID_INTERNALS();
            return false;
        }

        rhi_context->device_context->Begin(static_cast<ID3D11Query*>(query_disjoint));
        rhi_context->device_context->End(static_cast<ID3D11Query*>(query_start));

        return true;
    }

    bool RHI_CommandList::Timestamp_End(void* query_disjoint /*= nullptr*/, void* query_end /*= nullptr*/)
    {
        if (!query_disjoint || !query_end)
        {
            LOG_ERROR_INVALID_PARAMETER();
            return false;
        }

        RHI_Context* rhi_context = m_rhi_device->GetContextRhi();

        if (!rhi_context->device_context)
        {
            LOG_ERROR_INVALID_INTERNALS();
            return false;
        }

        rhi_context->device_context->End(static_cast<ID3D11Query*>(query_end));
        rhi_context->device_context->End(static_cast<ID3D11Query*>(query_disjoint));

        return true;
    }

    float RHI_CommandList::Timestamp_GetDuration(void* query_disjoint, void* query_start, void* query_end, const uint32_t pass_index)
    {
        if (!query_disjoint || !query_start || !query_end)
        {
            LOG_ERROR_INVALID_PARAMETER();
            return 0.0f;
        }

        RHI_Context* rhi_context = m_rhi_device->GetContextRhi();

        if (!rhi_context->device_context)
        {
            LOG_ERROR_INVALID_INTERNALS();
            return 0.0f;
        }

        // Check whether timestamps were disjoint during the last frame
        D3D10_QUERY_DATA_TIMESTAMP_DISJOINT disjoint_data = {};
        while (rhi_context->device_context->GetData(static_cast<ID3D11Query*>(query_disjoint), &disjoint_data, sizeof(disjoint_data), 0) != S_OK);
        if (disjoint_data.Disjoint)
            return 0.0f;

        // Get start time
        uint64_t start_time = 0; 
        rhi_context->device_context->GetData(static_cast<ID3D11Query*>(query_start), &start_time, sizeof(start_time), 0);

        // Get end time
        uint64_t end_time = 0;
        rhi_context->device_context->GetData(static_cast<ID3D11Query*>(query_end), &end_time, sizeof(end_time), 0);

        // Compute duration in ms
        const uint64_t delta        = end_time - start_time;
        const double duration_ms    = (delta * 1000.0) / static_cast<double>(disjoint_data.Frequency);

        return static_cast<float>(duration_ms);
    }

    uint32_t RHI_CommandList::Gpu_GetMemory(RHI_Device* rhi_device)
    {
        if (const PhysicalDevice* physical_device = rhi_device->GetPrimaryPhysicalDevice())
        {
            if (auto adapter = static_cast<IDXGIAdapter3*>(physical_device->GetData()))
            {
                DXGI_ADAPTER_DESC adapter_desc = {};
                const auto result = adapter->GetDesc(&adapter_desc);
                if (FAILED(result))
                {
                    LOG_ERROR("Failed to get adapter description, %s", d3d11_utility::dxgi_error_to_string(result));
                    return 0;
                }
                return static_cast<uint32_t>(adapter_desc.DedicatedVideoMemory / 1024 / 1024); // convert to MBs
            }
        }
        return 0;
    }

    uint32_t RHI_CommandList::Gpu_GetMemoryUsed(RHI_Device* rhi_device)
    {
        if (!memory_query_support)
            return 0;

        if (const PhysicalDevice* physical_device = rhi_device->GetPrimaryPhysicalDevice())
        {
            if (IDXGIAdapter3* adapter = static_cast<IDXGIAdapter3*>(physical_device->GetData()))
            {
                // Try to query video memory info
                DXGI_QUERY_VIDEO_MEMORY_INFO info = {};
                HRESULT result = adapter->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &info);

                if (SUCCEEDED(result))
                {
                    return static_cast<uint32_t>(info.CurrentUsage / 1024 / 1024); // convert to MBs
                }
                else
                {
                    // Some integrated or older dedicated GPUs might not support video memory queries, log the error once and don't query again.
                    LOG_ERROR("Failed to get adapter memory info, %s", d3d11_utility::dxgi_error_to_string(result));
                    memory_query_support = false;
                }
            }
        }

        return 0;
    }

    bool RHI_CommandList::Gpu_QueryCreate(RHI_Device* rhi_device, void** query, const RHI_Query_Type type)
    {
        RHI_Context* rhi_context = rhi_device->GetContextRhi();

        if (!rhi_context->device)
        {
            LOG_ERROR_INVALID_INTERNALS();
            return false;
        }

        D3D11_QUERY_DESC desc   = {};
        desc.Query              = (type == RHI_Query_Timestamp_Disjoint) ? D3D11_QUERY_TIMESTAMP_DISJOINT : D3D11_QUERY_TIMESTAMP;
        desc.MiscFlags          = 0;
        const auto result = rhi_context->device->CreateQuery(&desc, reinterpret_cast<ID3D11Query**>(query));
        if (FAILED(result))
        {
            LOG_ERROR("Failed to create ID3D11Query");
            return false;
        }

        return true;
    }

    void RHI_CommandList::Gpu_QueryRelease(void*& query_object)
    {
        if (!query_object)
            return;

        d3d11_utility::release(*reinterpret_cast<ID3D11Query**>(&query_object));
    }

    bool RHI_CommandList::IsRecording() const
    {
        return true;
    }

    bool RHI_CommandList::IsPending() const
    {
        return true;
    }

    bool RHI_CommandList::IsIdle() const
    {
        return true;
    }

    void RHI_CommandList::Timeblock_Start(const RHI_PipelineState* pipeline_state)
    {
        if (!pipeline_state || !pipeline_state->pass_name)
            return;

        RHI_Context* rhi_context = m_rhi_device->GetContextRhi();

        // Allowed to profile ?
        if (rhi_context->profiler && pipeline_state->profile)
        {
            if (m_profiler)
            {
                m_profiler->TimeBlockStart(pipeline_state->pass_name, TimeBlock_Cpu, this);
                m_profiler->TimeBlockStart(pipeline_state->pass_name, TimeBlock_Gpu, this);
            }
        }

        // Allowed to mark ?
        if (rhi_context->markers && pipeline_state->mark)
        {
            m_rhi_device->GetContextRhi()->annotation->BeginEvent(FileSystem::StringToWstring(pipeline_state->pass_name).c_str());
        }
    }

    void RHI_CommandList::Timeblock_End(const RHI_PipelineState* pipeline_state)
    {
        if (!pipeline_state)
            return;

        // Allowed to mark ?
        RHI_Context* rhi_context = m_rhi_device->GetContextRhi();
        if (rhi_context->markers && pipeline_state->mark)
        {
            rhi_context->annotation->EndEvent();
        }

        // Allowed to profile ?
        if (rhi_context->profiler && pipeline_state->profile)
        {
            if (m_profiler)
            {
                m_profiler->TimeBlockEnd(); // cpu
                m_profiler->TimeBlockEnd(); // gpu
            }
        }
    }

    bool RHI_CommandList::Deferred_BeginRenderPass()
    {
        return true;
    }

    bool RHI_CommandList::Deferred_BindPipeline()
    {
        return true;
    }

    bool RHI_CommandList::Deferred_BindDescriptorSet()
    {
        return true;
    }

    bool RHI_CommandList::OnDraw()
    {
        return true;
    }
}
