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

//= IMPLEMENTATION ===============
#include "../RHI_Implementation.h"
#ifdef API_GRAPHICS_D3D11
//================================

//= INCLUDES ========================
#include "../../Profiling/Profiler.h"
#include "../../Logging/Log.h"
#include "../../Rendering/Renderer.h"
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
        m_passes_active.reserve(100);
        m_passes_active.resize(100);
	}

	RHI_CommandList::~RHI_CommandList() = default;

    bool RHI_CommandList::Begin(RHI_PipelineState& pipeline_state)
    {
        if (!pipeline_state.IsValid())
        {
            LOG_ERROR("Invalid pipeline state");
            return false;
        }

        // Keep a local pointer for convenience 
        m_pipeline_state = &pipeline_state;

        // Start marker and profiler (if enabled)
        MarkAndProfileStart(m_pipeline_state);

        // Vertex shader
        if (pipeline_state.shader_vertex)
        {
            if (void* resource = pipeline_state.shader_vertex->GetResource())
            {
                m_rhi_device->GetContextRhi()->device_context->VSSetShader(static_cast<ID3D11VertexShader*>(const_cast<void*>(resource)), nullptr, 0);
                m_profiler->m_rhi_bindings_shader_vertex++;

                // D3D11 assumes vertex & index buffers are part of the vertex shader context.
                // So we have to reset the ideas of the set buffers here.
                m_set_id_vertex_buffer  = 0;
                m_set_id_index_buffer   = 0;
            }
        }

        // Pixel shader
        void* pixel_shader = pipeline_state.shader_pixel ? pipeline_state.shader_pixel->GetResource() : nullptr;
        m_rhi_device->GetContextRhi()->device_context->PSSetShader(static_cast<ID3D11PixelShader*>(const_cast<void*>(pixel_shader)), nullptr, 0);
        m_profiler->m_rhi_bindings_shader_pixel++;

        // Input layout
        if (pipeline_state.shader_vertex)
        {
            if (RHI_InputLayout* input_layout = pipeline_state.shader_vertex->GetInputLayout().get())
            {
                if (void* resource = input_layout->GetResource())
                {
                    m_rhi_device->GetContextRhi()->device_context->IASetInputLayout(static_cast<ID3D11InputLayout*>(const_cast<void*>(resource)));
                }
            }
        }

        // Viewport
        if (pipeline_state.viewport.IsDefined())
        {
            SetViewport(pipeline_state.viewport);
        }
        
        // Blend state
        if (pipeline_state.blend_state)
        {
            if (void* resource = pipeline_state.blend_state->GetResource())
            {
                float blendFactor       = pipeline_state.blend_state->GetBlendFactor();
                FLOAT blend_factor[4]   = { blendFactor, blendFactor, blendFactor, blendFactor };

                m_rhi_device->GetContextRhi()->device_context->OMSetBlendState
                (
                    static_cast<ID3D11BlendState*>(const_cast<void*>(resource)),
                    blend_factor,
                    0xffffffff
                );
            }
        }

        // Depth stencil state
        if (pipeline_state.depth_stencil_state)
        {
            if (void* resource = pipeline_state.depth_stencil_state->GetResource())
            {
                m_rhi_device->GetContextRhi()->device_context->OMSetDepthStencilState(static_cast<ID3D11DepthStencilState*>(const_cast<void*>(resource)), 1);
            }
        }

        // Rasterizer state
        if (pipeline_state.rasterizer_state)
        {
            if (void* resource = pipeline_state.rasterizer_state->GetResource())
            {
                m_rhi_device->GetContextRhi()->device_context->RSSetState(static_cast<ID3D11RasterizerState*>(const_cast<void*>(resource)));
            }
        }

        // Primitive topology
        if (pipeline_state.primitive_topology != RHI_PrimitiveTopology_Unknown)
        {
            m_rhi_device->GetContextRhi()->device_context->IASetPrimitiveTopology(d3d11_primitive_topology[pipeline_state.primitive_topology]);
        }

        // Render target(s)
        if (pipeline_state.render_target_swapchain)
        {
            const void* resource_array[1] = { pipeline_state.render_target_swapchain->GetResource_RenderTarget() };
            m_rhi_device->GetContextRhi()->device_context->OMSetRenderTargets
            (
                static_cast<UINT>(1),
                reinterpret_cast<ID3D11RenderTargetView* const*>(&resource_array),
                static_cast<ID3D11DepthStencilView*>(pipeline_state.render_target_depth_texture ? pipeline_state.render_target_depth_texture->GetResource_DepthStencil(pipeline_state.render_target_depth_array_index) : nullptr)
            );
        }
        else
        {
            static vector<void*> render_targets(state_max_render_target_count);
            uint32_t render_target_count = 0;

            // Detect used render targets
            for (auto i = 0; i < state_max_render_target_count; i++)
            {
                if (pipeline_state.render_target_color_textures[i])
                {
                    render_targets[render_target_count++] = pipeline_state.render_target_color_textures[i]->GetResource_RenderTarget();
                }
            }

            // Set them
            m_rhi_device->GetContextRhi()->device_context->OMSetRenderTargets
            (
                static_cast<UINT>(render_target_count),
                reinterpret_cast<ID3D11RenderTargetView* const*>(render_targets.data()),
                static_cast<ID3D11DepthStencilView*>(pipeline_state.render_target_depth_texture ? pipeline_state.render_target_depth_texture->GetResource_DepthStencil(pipeline_state.render_target_depth_array_index) : nullptr)
            );
        }

        // Clear render target(s)
        Clear(pipeline_state);

        return true;
	}

	bool RHI_CommandList::End()
	{
        // End marker and profiler (if enabled)
        MarkAndProfileEnd(m_pipeline_state);
        return true;
	}

    void RHI_CommandList::Clear(RHI_PipelineState& pipeline_state)
    {
        for (auto i = 0; i < state_max_render_target_count; i++)
        {
            if (pipeline_state.render_target_color_clear[i] != state_dont_clear_color)
            {
                if (pipeline_state.render_target_swapchain)
                {
                    m_rhi_device->GetContextRhi()->device_context->ClearRenderTargetView
                    (
                        static_cast<ID3D11RenderTargetView*>(const_cast<void*>(pipeline_state.render_target_swapchain->GetResource_RenderTarget())),
                        pipeline_state.render_target_color_clear[i].Data()
                    );
                }
                else if (pipeline_state.render_target_color_textures[i])
                {
                    m_rhi_device->GetContextRhi()->device_context->ClearRenderTargetView
                    (
                        static_cast<ID3D11RenderTargetView*>(const_cast<void*>(pipeline_state.render_target_color_textures[i]->GetResource_RenderTarget())),
                        pipeline_state.render_target_color_clear[i].Data()
                    );
                }
            }
        }

        if (pipeline_state.render_target_depth_clear != state_dont_clear_depth)
        {
            uint32_t flags  = RHI_Clear_Depth;
            uint8_t stencil = 0;

            UINT clear_flags = 0;
            clear_flags |= (flags & RHI_Clear_Depth)    ? D3D11_CLEAR_DEPTH : 0;
            clear_flags |= (flags & RHI_Clear_Stencil)  ? D3D11_CLEAR_STENCIL : 0;

            m_rhi_device->GetContextRhi()->device_context->ClearDepthStencilView
            (
                static_cast<ID3D11DepthStencilView*>(pipeline_state.render_target_depth_texture->GetResource_DepthStencil(pipeline_state.render_target_depth_array_index)),
                clear_flags,
                static_cast<FLOAT>(pipeline_state.render_target_depth_clear),
                static_cast<UINT8>(stencil)
            );
        }

        pipeline_state.ResetClearValues();
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

	void RHI_CommandList::SetBufferVertex(const RHI_VertexBuffer* buffer)
	{
		if (!buffer || !buffer->GetResource())
		{
			LOG_ERROR_INVALID_PARAMETER();
			return;
		}

        if (m_set_id_vertex_buffer == buffer->GetId())
            return;

        auto ptr        = static_cast<ID3D11Buffer*>(buffer->GetResource());
        auto stride     = buffer->GetStride();
        uint32_t offset = 0;
        m_rhi_device->GetContextRhi()->device_context->IASetVertexBuffers(0, 1, &ptr, &stride, &offset);

        m_profiler->m_rhi_bindings_buffer_vertex++;
        m_set_id_vertex_buffer = buffer->GetId();
	}

	void RHI_CommandList::SetBufferIndex(const RHI_IndexBuffer* buffer)
	{
		if (!buffer || !buffer->GetResource())
		{
			LOG_ERROR_INVALID_PARAMETER();
			return;
		}

        if (m_set_id_index_buffer == buffer->GetId())
            return;

        m_rhi_device->GetContextRhi()->device_context->IASetIndexBuffer
        (
            static_cast<ID3D11Buffer*>(buffer->GetResource()),
            buffer->Is16Bit() ? DXGI_FORMAT_R16_UINT : DXGI_FORMAT_R32_UINT,
            0
        );

        m_profiler->m_rhi_bindings_buffer_index++;
        m_set_id_index_buffer = buffer->GetId();
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
		void* resource_ptr              = texture ? texture->GetResource_View() : nullptr;
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

	bool RHI_CommandList::Submit()
	{
		return true;
	}

    bool RHI_CommandList::Flush()
    {
        m_rhi_device->GetContextRhi()->device_context->Flush();
        return true;
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

    float RHI_CommandList::Timestamp_GetDuration(void* query_disjoint /*= nullptr*/, void* query_start /*= nullptr*/, void* query_end /*= nullptr*/)
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
            if (auto adapter = static_cast<IDXGIAdapter3*>(physical_device->data))
            {
                DXGI_ADAPTER_DESC adapter_desc = {};
                auto result = adapter->GetDesc(&adapter_desc);
                if (FAILED(result))
                {
                    LOG_ERROR("Failed to get adapter description, %s", d3d11_common::dxgi_error_to_string(result));
                    return 0;
                }
                return static_cast<uint32_t>(adapter_desc.DedicatedVideoMemory / 1024 / 1024); // convert to MBs
            }
        }
        return 0;
    }

    uint32_t RHI_CommandList::Gpu_GetMemoryUsed(RHI_Device* rhi_device)
    {
        if (const PhysicalDevice* physical_device = rhi_device->GetPrimaryPhysicalDevice())
        {
            if (auto adapter = static_cast<IDXGIAdapter3*>(physical_device->data))
            {
                DXGI_QUERY_VIDEO_MEMORY_INFO info = {};
                auto result = adapter->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &info);
                if (FAILED(result))
                {
                    LOG_ERROR("Failed to get adapter memory info, %s", d3d11_common::dxgi_error_to_string(result));
                    return 0;
                }
                return static_cast<uint32_t>(info.CurrentUsage / 1024 / 1024); // convert to MBs
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

        safe_release(*reinterpret_cast<ID3D11Query**>(&query_object));
    }

    void RHI_CommandList::MarkAndProfileStart(const RHI_PipelineState* pipeline_state)
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

        m_passes_active[m_pass_index++] = true;
    }

    void RHI_CommandList::MarkAndProfileEnd(const RHI_PipelineState* pipeline_state)
    {
        if (!pipeline_state || !m_passes_active[m_pass_index - 1])
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

        m_passes_active[--m_pass_index] = false;
    }

    bool RHI_CommandList::OnDraw()
    {
        return true;
    }
}
#endif
