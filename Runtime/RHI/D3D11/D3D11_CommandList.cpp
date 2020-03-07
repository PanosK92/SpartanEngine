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
        m_renderer              = context->GetSubsystem<Renderer>();
        m_profiler              = context->GetSubsystem<Profiler>();
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

        ID3D11DeviceContext* device_context = m_rhi_device->GetContextRhi()->device_context;

        // Vertex shader
        if (pipeline_state.shader_vertex && pipeline_state.shader_vertex->GetResource())
        {
            ID3D11VertexShader* shader = static_cast<ID3D11VertexShader*>(pipeline_state.shader_vertex->GetResource());

            // Set only if not set
            ID3D11VertexShader* set_shader  = nullptr;
            UINT instance_count             = 256;
            ID3D11ClassInstance* instances[256];
            device_context->VSGetShader(&set_shader, instances, &instance_count);
            if (set_shader != shader)
            {
                device_context->VSSetShader(shader, nullptr, 0);

                m_profiler->m_rhi_bindings_shader_vertex++;
            }
        }

        // Pixel shader
        {
            ID3D11PixelShader* shader = static_cast<ID3D11PixelShader*>(pipeline_state.shader_pixel ? pipeline_state.shader_pixel->GetResource() : nullptr);

            // Set only if not set
            ID3D11PixelShader* set_shader   = nullptr;
            UINT instance_count             = 256;
            ID3D11ClassInstance* instances[256];
            device_context->PSGetShader(&set_shader, instances, &instance_count);
            if (set_shader != shader)
            {
                device_context->PSSetShader(shader, nullptr, 0);

                m_profiler->m_rhi_bindings_shader_pixel++;
            }
        }

        // Compute shader
        {
            ID3D11ComputeShader* shader = static_cast<ID3D11ComputeShader*>(pipeline_state.shader_compute ? pipeline_state.shader_compute->GetResource() : nullptr);

            // Set only if not set
            ID3D11ComputeShader* set_shader = nullptr;
            UINT instance_count             = 256;
            ID3D11ClassInstance* instances[256];
            device_context->CSGetShader(&set_shader, instances, &instance_count);
            if (set_shader != shader)
            {
                device_context->CSSetShader(shader, nullptr, 0);

                m_profiler->m_rhi_bindings_shader_compute++;
            }
        }

        // Input layout
        if (pipeline_state.shader_vertex)
        {
            if (RHI_InputLayout* input_layout = pipeline_state.shader_vertex->GetInputLayout().get())
            {
                if (void* resource = input_layout->GetResource())
                {
                    device_context->IASetInputLayout(static_cast<ID3D11InputLayout*>(const_cast<void*>(resource)));
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
                const float blendFactor       = pipeline_state.blend_state->GetBlendFactor();
                FLOAT blend_factor[4]   = { blendFactor, blendFactor, blendFactor, blendFactor };

                device_context->OMSetBlendState(static_cast<ID3D11BlendState*>(const_cast<void*>(resource)), blend_factor, 0xffffffff);
            }
        }

        // Depth stencil state
        if (pipeline_state.depth_stencil_state)
        {
            if (void* resource = pipeline_state.depth_stencil_state->GetResource())
            {
                device_context->OMSetDepthStencilState(static_cast<ID3D11DepthStencilState*>(const_cast<void*>(resource)), 1);
            }
        }

        // Rasterizer state
        if (pipeline_state.rasterizer_state)
        {
            if (void* resource = pipeline_state.rasterizer_state->GetResource())
            {
                device_context->RSSetState(static_cast<ID3D11RasterizerState*>(const_cast<void*>(resource)));
            }
        }

        // Primitive topology
        if (pipeline_state.primitive_topology != RHI_PrimitiveTopology_Unknown)
        {
            device_context->IASetPrimitiveTopology(d3d11_primitive_topology[pipeline_state.primitive_topology]);
        }

        // Render target(s)
        {
            // Get depth stencil (if any)
            void* depth_stencil = nullptr;
            if (pipeline_state.render_target_depth_texture)
            {
                if (pipeline_state.render_target_depth_texture_read_only)
                {
                    depth_stencil = pipeline_state.render_target_depth_texture->GetResource_DepthStencilViewReadOnly(pipeline_state.render_target_depth_stencil_texture_array_index);
                }
                else
                {
                    depth_stencil = pipeline_state.render_target_depth_texture->GetResource_DepthStencilView(pipeline_state.render_target_depth_stencil_texture_array_index);
                }
            }

            // Unordered view
            if (pipeline_state.unordered_access_view)
            {
                const void* resource_array[1] = { pipeline_state.unordered_access_view };
                device_context->CSSetUnorderedAccessViews(0, 1, reinterpret_cast<ID3D11UnorderedAccessView* const*>(&resource_array), nullptr);
            }
            // Swapchain
            else if (pipeline_state.render_target_swapchain)
            {
                const void* resource_array[1] = { pipeline_state.render_target_swapchain->GetResource_RenderTargetView() };

                device_context->OMSetRenderTargets
                (
                    static_cast<UINT>(1),
                    reinterpret_cast<ID3D11RenderTargetView* const*>(&resource_array),
                    static_cast<ID3D11DepthStencilView*>(depth_stencil)
                );

                m_profiler->m_rhi_bindings_render_target++;
            }
            // Textures
            else
            {
                void* render_targets[state_max_render_target_count];
                uint32_t render_target_count = 0;

                // Detect used render targets
                for (auto i = 0; i < state_max_render_target_count; i++)
                {
                    if (pipeline_state.render_target_color_textures[i])
                    {
                        render_targets[render_target_count++] = pipeline_state.render_target_color_textures[i]->GetResource_RenderTargetView(pipeline_state.render_target_color_texture_array_index);
                    }
                }

                // Set them
                device_context->OMSetRenderTargets
                (
                    static_cast<UINT>(render_target_count),
                    reinterpret_cast<ID3D11RenderTargetView* const*>(render_targets),
                    static_cast<ID3D11DepthStencilView*>(depth_stencil)
                );

                m_profiler->m_rhi_bindings_render_target++;
            }
        }

        // Clear render target(s)
        Clear(pipeline_state);

        m_renderer->SetGlobalSamplersAndConstantBuffers(this);

        m_profiler->m_rhi_bindings_pipeline++;

        return true;
	}

	bool RHI_CommandList::End()
	{
        // End marker and profiler (if enabled)
        MarkAndProfileEnd(m_pipeline_state);
        return true;
	}

    void RHI_CommandList::Clear(RHI_PipelineState& pipeline_state) const
    {
        // Color
        for (auto i = 0; i < state_max_render_target_count; i++)
        {
            if (pipeline_state.render_target_color_clear[i] != state_dont_clear_color)
            {
                if (pipeline_state.render_target_swapchain)
                {
                    m_rhi_device->GetContextRhi()->device_context->ClearRenderTargetView
                    (
                        static_cast<ID3D11RenderTargetView*>(const_cast<void*>(pipeline_state.render_target_swapchain->GetResource_RenderTargetView())),
                        pipeline_state.render_target_color_clear[i].Data()
                    );
                }
                else if (pipeline_state.render_target_color_textures[i])
                {
                    m_rhi_device->GetContextRhi()->device_context->ClearRenderTargetView
                    (
                        static_cast<ID3D11RenderTargetView*>(const_cast<void*>(pipeline_state.render_target_color_textures[i]->GetResource_RenderTargetView(pipeline_state.render_target_color_texture_array_index))),
                        pipeline_state.render_target_color_clear[i].Data()
                    );
                }
            }
        }

        // Depth-stencil
        UINT clear_flags = 0;
        clear_flags |= (pipeline_state.render_target_depth_clear    != state_dont_clear_depth)      ? D3D11_CLEAR_DEPTH     : 0;
        clear_flags |= (pipeline_state.render_target_stencil_clear  != state_dont_clear_stencil)    ? D3D11_CLEAR_STENCIL   : 0;
        if (clear_flags != 0)
        {
            m_rhi_device->GetContextRhi()->device_context->ClearDepthStencilView
            (
                static_cast<ID3D11DepthStencilView*>(pipeline_state.render_target_depth_texture->GetResource_DepthStencilView(pipeline_state.render_target_depth_stencil_texture_array_index)),
                clear_flags,
                static_cast<FLOAT>(pipeline_state.render_target_depth_clear),
                static_cast<UINT8>(pipeline_state.render_target_stencil_clear)
            );
        }

        pipeline_state.ResetClearValues();
    }

	void RHI_CommandList::Draw(const uint32_t vertex_count) const
    {
        m_rhi_device->GetContextRhi()->device_context->Draw(static_cast<UINT>(vertex_count), 0);

        m_profiler->m_rhi_draw_calls++;
	}

	void RHI_CommandList::DrawIndexed(const uint32_t index_count, const uint32_t index_offset, const uint32_t vertex_offset) const
    {
        m_rhi_device->GetContextRhi()->device_context->DrawIndexed
        (
            static_cast<UINT>(index_count),
            static_cast<UINT>(index_offset),
            static_cast<INT>(vertex_offset)
        );

        m_profiler->m_rhi_draw_calls++;
	}

    void RHI_CommandList::Dispatch(uint32_t x, uint32_t y, uint32_t z /*= 1*/) const
    {
        m_rhi_device->GetContextRhi()->device_context->Dispatch(x, y, z);
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

	void RHI_CommandList::SetBufferVertex(const RHI_VertexBuffer* buffer) const
    {
		if (!buffer || !buffer->GetResource())
		{
			LOG_ERROR_INVALID_PARAMETER();
			return;
		}

        ID3D11Buffer* vertex_buffer         = static_cast<ID3D11Buffer*>(buffer->GetResource());
        UINT stride                         = buffer->GetStride();
        UINT offset                         = 0;
        ID3D11DeviceContext* device_context = m_rhi_device->GetContextRhi()->device_context;

        // Skip if already set
        ID3D11Buffer* set_buffer    = nullptr;
        UINT set_stride             = buffer->GetStride();
        UINT set_offset             = 0;
        device_context->IAGetVertexBuffers(0, 1, &set_buffer, &set_stride, &set_offset);
        if (set_buffer == vertex_buffer)
            return;

        device_context->IASetVertexBuffers(0, 1, &vertex_buffer, &stride, &offset);

        m_profiler->m_rhi_bindings_buffer_vertex++;
	}

	void RHI_CommandList::SetBufferIndex(const RHI_IndexBuffer* buffer) const
    {
		if (!buffer || !buffer->GetResource())
		{
			LOG_ERROR_INVALID_PARAMETER();
			return;
		}

        ID3D11Buffer* index_buffer          = static_cast<ID3D11Buffer*>(buffer->GetResource());
        const DXGI_FORMAT format                  = buffer->Is16Bit() ? DXGI_FORMAT_R16_UINT : DXGI_FORMAT_R32_UINT;
        ID3D11DeviceContext* device_context = m_rhi_device->GetContextRhi()->device_context;

        // Skip if already set
        ID3D11Buffer* set_buffer    = nullptr;
        DXGI_FORMAT set_format      = DXGI_FORMAT_UNKNOWN;
        UINT set_offset             = 0;
        device_context->IAGetIndexBuffer(&set_buffer, &set_format, &set_offset);
        if (set_buffer == index_buffer)
            return;

        device_context->IASetIndexBuffer(index_buffer, format, 0);

        m_profiler->m_rhi_bindings_buffer_index++;
	}

    void RHI_CommandList::SetShaderCompute(const RHI_Shader* shader) const
    {
        if (shader && !shader->GetResource())
        {
            LOG_WARNING("%s hasn't compiled", shader->GetName().c_str());
            return;
        }

        m_rhi_device->GetContextRhi()->device_context->CSSetShader(static_cast<ID3D11ComputeShader*>(const_cast<void*>(shader->GetResource())), nullptr, 0);
        m_profiler->m_rhi_bindings_shader_compute++;
    }

    void RHI_CommandList::SetConstantBuffer(const uint32_t slot, uint8_t scope, RHI_ConstantBuffer* constant_buffer) const
    {
        void* buffer                        = static_cast<ID3D11Buffer*>(constant_buffer ? constant_buffer->GetResource() : nullptr);
        const void* buffer_array[1]         = { buffer };
        const UINT range                          = 1;
        ID3D11DeviceContext* device_context = m_rhi_device->GetContextRhi()->device_context;

        if (scope & RHI_Buffer_VertexShader)
        {
            // Set only if not set
            ID3D11Buffer* set_buffer = nullptr;
            device_context->VSGetConstantBuffers(slot, range, &set_buffer);
            if (set_buffer != buffer)
            {
                device_context->VSSetConstantBuffers(slot, range, reinterpret_cast<ID3D11Buffer* const*>(range > 1 ? buffer : &buffer_array));
            }
        }

        if (scope & RHI_Buffer_PixelShader)
        {
            // Set only if not set
            ID3D11Buffer* set_buffer = nullptr;
            device_context->PSGetConstantBuffers(slot, range, &set_buffer);
            if (set_buffer != buffer)
            {
                device_context->PSSetConstantBuffers(slot, range, reinterpret_cast<ID3D11Buffer* const*>(range > 1 ? buffer : &buffer_array));
            }
        }

        m_profiler->m_rhi_bindings_buffer_constant += scope & RHI_Buffer_VertexShader   ? 1 : 0;
        m_profiler->m_rhi_bindings_buffer_constant += scope & RHI_Buffer_PixelShader    ? 1 : 0;
    }

    void RHI_CommandList::SetSampler(const uint32_t slot, RHI_Sampler* sampler) const
    {
        const UINT start_slot                     = slot;
        const UINT range                          = 1;
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

    void RHI_CommandList::SetTexture(const uint32_t slot, RHI_Texture* texture) const
    {
        const UINT start_slot                     = slot;
        const UINT range                          = 1;
        void* resource_texture              = texture ? texture->GetResource_ShaderView() : nullptr;
        const bool is_compute                     = m_pipeline_state->unordered_access_view != nullptr;
        ID3D11DeviceContext* device_context = m_rhi_device->GetContextRhi()->device_context;

        // Skip if already set
        ID3D11ShaderResourceView* set_texture = nullptr;
        device_context->PSGetShaderResources(slot, range, &set_texture);
        if (set_texture == resource_texture)
            return;

        if (range > 1)
        {
            if (is_compute)
            {
                device_context->CSSetShaderResources(start_slot, range, reinterpret_cast<ID3D11ShaderResourceView* const*>(resource_texture));
            }
            else
            {
                device_context->PSSetShaderResources(start_slot, range, reinterpret_cast<ID3D11ShaderResourceView* const*>(resource_texture));
            }
        }
        else
        {
            const void* resource_array[1] = { resource_texture };

            if (is_compute)
            {
                device_context->CSSetShaderResources(start_slot, range, reinterpret_cast<ID3D11ShaderResourceView* const*>(&resource_array));
            }
            else
            {
                device_context->PSSetShaderResources(start_slot, range, reinterpret_cast<ID3D11ShaderResourceView* const*>(&resource_array));
            }
        }

        m_profiler->m_rhi_bindings_texture++;
	}

	bool RHI_CommandList::Submit()
	{
		return true;
	}

    bool RHI_CommandList::Flush() const
    {
        m_rhi_device->GetContextRhi()->device_context->Flush();
        return true;
    }

    bool RHI_CommandList::Timestamp_Start(void* query_disjoint /*= nullptr*/, void* query_start /*= nullptr*/) const
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

    bool RHI_CommandList::Timestamp_End(void* query_disjoint /*= nullptr*/, void* query_end /*= nullptr*/) const
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

    float RHI_CommandList::Timestamp_GetDuration(void* query_disjoint /*= nullptr*/, void* query_start /*= nullptr*/, void* query_end /*= nullptr*/) const
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
                const auto result = adapter->GetDesc(&adapter_desc);
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
                const auto result = adapter->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &info);
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
