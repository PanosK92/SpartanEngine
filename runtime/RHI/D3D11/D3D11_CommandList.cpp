/*
Copyright(c) 2016-2022 Panos Karabelas

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
#include "pch.h"
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
#include "../RHI_StructuredBuffer.h"
#include "../RHI_BlendState.h"
#include "../RHI_DepthStencilState.h"
#include "../RHI_RasterizerState.h"
#include "../RHI_InputLayout.h"
#include "../RHI_SwapChain.h"
#include "../RHI_PipelineState.h"
#include "../../Profiling/Profiler.h"
#include "../../Rendering/Renderer.h"
//===================================

//= NAMESPACES =====
using namespace std;
//==================

namespace Spartan
{
    bool RHI_CommandList::m_memory_query_support = true;

    RHI_CommandList::RHI_CommandList(Context* context, void* cmd_pool, const char* name) : SpartanObject(context)
    {
        m_renderer    = context->GetSubsystem<Renderer>();
        m_profiler    = context->GetSubsystem<Profiler>();
        m_rhi_device  = m_renderer->GetRhiDevice().get();
        m_object_name = name;
        m_timestamps.fill(0);
    }

    RHI_CommandList::~RHI_CommandList() = default;

    void RHI_CommandList::Begin()
    {
        m_state = RHI_CommandListState::Recording;
    }

    bool RHI_CommandList::End()
    {
        m_state = RHI_CommandListState::Ended;
        return true;
    }

    void RHI_CommandList::Submit()
    {
        m_state = RHI_CommandListState::Submitted;
    }

    void RHI_CommandList::SetPipelineState(RHI_PipelineState& pso)
    {
        SP_ASSERT(pso.IsValid() && "Pipeline state is invalid");
        m_pso = pso;

        UnbindOutputTextures();

        ID3D11DeviceContext* device_context = m_rhi_device->GetContextRhi()->device_context;

        // Input layout
        {
            // New state
            ID3D11InputLayout* input_layout = static_cast<ID3D11InputLayout*>(m_pso.shader_vertex ? m_pso.shader_vertex->GetInputLayout()->GetRhiResource() : nullptr);

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
            ID3D11VertexShader* shader = static_cast<ID3D11VertexShader*>(m_pso.shader_vertex ? m_pso.shader_vertex->GetRhiResource() : nullptr);

            // Current state
            ID3D11VertexShader* set_shader = nullptr; UINT instance_count = 256; ID3D11ClassInstance* instances[256];
            device_context->VSGetShader(&set_shader, instances, &instance_count);

            // Set if dirty
            if (set_shader != shader)
            {
                device_context->VSSetShader(shader, nullptr, 0);

                if (m_profiler)
                {
                    m_profiler->m_rhi_bindings_shader_vertex++;
                }
            }
        }

        // Pixel shader
        {
            // New state
            ID3D11PixelShader* shader = static_cast<ID3D11PixelShader*>(m_pso.shader_pixel ? m_pso.shader_pixel->GetRhiResource() : nullptr);

            // Current state
            ID3D11PixelShader* set_shader = nullptr; UINT instance_count = 256; ID3D11ClassInstance* instances[256];
            device_context->PSGetShader(&set_shader, instances, &instance_count);

            // Set if dirty
            if (set_shader != shader)
            {
                device_context->PSSetShader(shader, nullptr, 0);

                if (m_profiler)
                {
                    m_profiler->m_rhi_bindings_shader_pixel++;
                }
            }
        }

        // Compute shader
        {
            // New state
            ID3D11ComputeShader* shader = static_cast<ID3D11ComputeShader*>(m_pso.shader_compute ? m_pso.shader_compute->GetRhiResource() : nullptr);

            // Current state
            ID3D11ComputeShader* set_shader = nullptr; UINT instance_count = 256; ID3D11ClassInstance* instances[256];
            device_context->CSGetShader(&set_shader, instances, &instance_count);

            // Set if dirty
            if (set_shader != shader)
            {
                device_context->CSSetShader(shader, nullptr, 0);

                if (m_profiler)
                {
                    m_profiler->m_rhi_bindings_shader_compute++;
                }
            }
        }

        // Blend state
        {
            // New state
            ID3D11BlendState* blend_state_set = nullptr;
            std::array<FLOAT, 4> blend_factor_set = { 0.0f };
            UINT mask_set = 0;
            device_context->OMGetBlendState(&blend_state_set, blend_factor_set.data(), &mask_set);

            // Current state
            ID3D11BlendState* blend_state = static_cast<ID3D11BlendState*>(m_pso.blend_state ? m_pso.blend_state->GetRhiResource() : nullptr);
            const float blendFactor = m_pso.blend_state ? m_pso.blend_state->GetBlendFactor() : 0.0f;
            std::array<FLOAT, 4> blend_factor = { blendFactor, blendFactor, blendFactor, blendFactor };
            const UINT mask = 0;

            // Set if dirty
            if (blend_state_set != blend_state || blend_factor_set != blend_factor || mask_set != mask)
            {
                device_context->OMSetBlendState(blend_state, blend_factor.data(), 0xffffffff);
            }
        }

        // Depth stencil state
        {
            // New state
            ID3D11DepthStencilState* depth_stencil_state = static_cast<ID3D11DepthStencilState*>(m_pso.depth_stencil_state ? m_pso.depth_stencil_state->GetRhiResource() : nullptr);

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
            ID3D11RasterizerState* rasterizer_state = static_cast<ID3D11RasterizerState*>(m_pso.rasterizer_state ? m_pso.rasterizer_state->GetRhiResource() : nullptr);

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
        if (m_pso.primitive_topology != RHI_PrimitiveTopology_Mode::Undefined)
        {
            // New state
            const D3D11_PRIMITIVE_TOPOLOGY topology = d3d11_primitive_topology[static_cast<uint32_t>(m_pso.primitive_topology)];

            // Current state
            D3D11_PRIMITIVE_TOPOLOGY topology_set;
            device_context->IAGetPrimitiveTopology(&topology_set);

            // Set if dirty
            if (topology_set != topology)
            {
                device_context->IASetPrimitiveTopology(d3d11_primitive_topology[static_cast<uint32_t>(m_pso.primitive_topology)]);
            }
        }

        // Render target(s)
        {
            // Detect depth stencil targets
            ID3D11DepthStencilView* depth_stencil = nullptr;
            if (m_pso.render_target_depth_texture)
            {
                SP_ASSERT(m_pso.render_target_depth_texture->IsRenderTargetDepthStencil());

                if (m_pso.render_target_depth_texture_read_only)
                {
                    depth_stencil = static_cast<ID3D11DepthStencilView*>(m_pso.render_target_depth_texture->GetRhiDsvReadOnly(m_pso.render_target_depth_stencil_texture_array_index));
                }
                else
                {
                    depth_stencil = static_cast<ID3D11DepthStencilView*>(m_pso.render_target_depth_texture->GetRhiDsv(m_pso.render_target_depth_stencil_texture_array_index));
                }
            }

            // Detect color targets
            std::array<ID3D11RenderTargetView*, rhi_max_render_target_count> render_targets = { nullptr };
            {
                // Swapchain
                if (m_pso.render_target_swapchain)
                {
                    render_targets[0] = { static_cast<ID3D11RenderTargetView*>(m_pso.render_target_swapchain->GetRhiRtv()) };
                }
                // Textures
                else
                {
                    for (uint8_t i = 0; i < rhi_max_render_target_count; i++)
                    {
                        if (m_pso.render_target_color_textures[i])
                        {
                            SP_ASSERT(m_pso.render_target_color_textures[i]->IsRenderTargetColor());

                            ID3D11RenderTargetView* rt = static_cast<ID3D11RenderTargetView*>(m_pso.render_target_color_textures[i]->GetRhiRtv(m_pso.render_target_color_texture_array_index));
                            render_targets[i] = rt;
                        }
                    }
                }
            }

            // Set render targets
            {
                // Current state
                std::array<ID3D11RenderTargetView*, rhi_max_render_target_count> set_render_target_views = { nullptr };
                ID3D11DepthStencilView* set_depth_stencil_view = nullptr;
                device_context->OMGetRenderTargets(rhi_max_render_target_count, set_render_target_views.data(), &set_depth_stencil_view);

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

                    if (m_profiler)
                    {
                        m_profiler->m_rhi_bindings_render_target++;
                    }
                }
            }
        }

        // Viewport
        if (m_pso.viewport.IsDefined())
        {
            SetViewport(m_pso.viewport);
        }

        // Clear render target(s)
        ClearPipelineStateRenderTargets(m_pso);

        m_renderer->SetGlobalShaderResources(this);

        if (m_profiler)
        {
            m_profiler->m_rhi_bindings_pipeline++;
        }
    }

    void RHI_CommandList::BeginRenderPass()
    {

    }

    void RHI_CommandList::EndRenderPass()
    {

    }

    void RHI_CommandList::ClearPipelineStateRenderTargets(RHI_PipelineState& m_pso)
    {
        // Color
        for (uint8_t i = 0; i < rhi_max_render_target_count; i++)
        {
            if (m_pso.clear_color[i] != rhi_color_load && m_pso.clear_color[i] != rhi_color_dont_care)
            {
                if (m_pso.render_target_swapchain)
                {
                    m_rhi_device->GetContextRhi()->device_context->ClearRenderTargetView
                    (
                        static_cast<ID3D11RenderTargetView*>(const_cast<void*>(m_pso.render_target_swapchain->GetRhiRtv())),
                        m_pso.clear_color[i].Data()
                    );
                }
                else if (m_pso.render_target_color_textures[i])
                {
                    m_rhi_device->GetContextRhi()->device_context->ClearRenderTargetView
                    (
                        static_cast<ID3D11RenderTargetView*>(const_cast<void*>(m_pso.render_target_color_textures[i]->GetRhiRtv(m_pso.render_target_color_texture_array_index))),
                        m_pso.clear_color[i].Data()
                    );
                }
            }
        }

        // Depth-stencil
        if (m_pso.render_target_depth_texture)
        {
            UINT clear_flags = 0;
            clear_flags |= (m_pso.clear_depth   != rhi_depth_stencil_load && m_pso.clear_depth   != rhi_depth_stencil_dont_care) ? D3D11_CLEAR_DEPTH   : 0;
            clear_flags |= (m_pso.clear_stencil != rhi_depth_stencil_load && m_pso.clear_stencil != rhi_depth_stencil_dont_care) ? D3D11_CLEAR_STENCIL : 0;
            if (clear_flags != 0)
            {
                m_rhi_device->GetContextRhi()->device_context->ClearDepthStencilView
                (
                    static_cast<ID3D11DepthStencilView*>(m_pso.render_target_depth_texture->GetRhiDsv(m_pso.render_target_depth_stencil_texture_array_index)),
                    clear_flags,
                    static_cast<FLOAT>(m_pso.clear_depth),
                    static_cast<UINT8>(m_pso.clear_stencil)
                );
            }
        }
    }

    void RHI_CommandList::ClearRenderTarget(RHI_Texture* texture,
        const uint32_t color_index          /*= 0*/,
        const uint32_t depth_stencil_index  /*= 0*/,
        const bool storage                  /*= false*/,
        const Math::Vector4& clear_color    /*= rhi_color_load*/,
        const float clear_depth             /*= rhi_depth_load*/,
        const float clear_stencil           /*= rhi_stencil_load*/
    )
    {
        if (storage)
        {
            SP_ASSERT(texture->IsUav());

            if (clear_color == rhi_color_load || clear_color == rhi_color_dont_care)
                return;

            // TODO: Assuming the UAV is a float, which almost always is, but I should fix it anyway
            m_rhi_device->GetContextRhi()->device_context->ClearUnorderedAccessViewFloat(static_cast<ID3D11UnorderedAccessView*>(texture->GetRhiUav()), clear_color.Data());

            if (texture->HasPerMipViews())
            {
                for (uint32_t i = 0; i < texture->GetMipCount(); i++)
                {
                    m_rhi_device->GetContextRhi()->device_context->ClearUnorderedAccessViewFloat(static_cast<ID3D11UnorderedAccessView*>(texture->GetRhiUavMip(i)), clear_color.Data());
                }
            }
        }
        else if (texture->IsRenderTargetColor() || texture->IsRenderTargetDepthStencil())
        {
            if (texture->IsRenderTargetColor())
            {
                if (clear_color == rhi_color_load || clear_color == rhi_color_dont_care)
                    return;

                m_rhi_device->GetContextRhi()->device_context->ClearRenderTargetView
                (
                    static_cast<ID3D11RenderTargetView*>(const_cast<void*>(texture->GetRhiRtv(color_index))),
                    clear_color.Data()
                );
            }
            else if (texture->IsRenderTargetDepthStencil())
            {
                if ((clear_depth == rhi_depth_stencil_load || clear_depth == rhi_depth_stencil_dont_care) && (clear_stencil == rhi_depth_stencil_load || clear_stencil == rhi_depth_stencil_dont_care))
                    return;

                UINT clear_flags = 0;
                clear_flags |= (clear_depth   != rhi_depth_stencil_load && clear_depth   != rhi_depth_stencil_dont_care) ? D3D11_CLEAR_DEPTH : 0;
                clear_flags |= (clear_stencil != rhi_depth_stencil_load && clear_stencil != rhi_depth_stencil_dont_care) ? D3D11_CLEAR_STENCIL : 0;
                if (clear_flags != 0)
                {
                    m_rhi_device->GetContextRhi()->device_context->ClearDepthStencilView
                    (
                        static_cast<ID3D11DepthStencilView*>(texture->GetRhiDsv(depth_stencil_index)),
                        clear_flags,
                        static_cast<FLOAT>(clear_depth),
                        static_cast<UINT8>(clear_stencil)
                    );
                }
            }
        }
    }

    void RHI_CommandList::Draw(const uint32_t vertex_count, uint32_t vertex_start_index /*= 0*/)
    {
        m_rhi_device->GetContextRhi()->device_context->Draw(static_cast<UINT>(vertex_count), static_cast<UINT>(vertex_start_index));

        if (m_profiler)
        {
            m_profiler->m_rhi_draw++;
        }
    }

    void RHI_CommandList::DrawIndexed(const uint32_t index_count, const uint32_t index_offset, const uint32_t vertex_offset)
    {
        m_rhi_device->GetContextRhi()->device_context->DrawIndexed
        (
            static_cast<UINT>(index_count),
            static_cast<UINT>(index_offset),
            static_cast<INT>(vertex_offset)
        );

        if (m_profiler)
        {
            m_profiler->m_rhi_draw++;
        }
    }

    void RHI_CommandList::Dispatch(uint32_t x, uint32_t y, uint32_t z, bool async /*= false*/)
    {
        ID3D11Device5* device = m_rhi_device->GetContextRhi()->device;
        ID3D11DeviceContext4* device_context = m_rhi_device->GetContextRhi()->device_context;

        device_context->Dispatch(x, y, z);

        if (m_profiler)
        {
            m_profiler->m_rhi_dispatch++;
        }
        // Make sure to clean the compute shader UAV slots after dispatching.
        // If we try to bind the resource but it's still bound as a computer shader output the runtime will automatically set the it to null.
        const void* resource_array[8] = { nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr };
        device_context->CSSetUnorderedAccessViews(0, 8, reinterpret_cast<ID3D11UnorderedAccessView* const*>(&resource_array), nullptr);
    }

    void RHI_CommandList::Blit(RHI_Texture* source, RHI_Texture* destination)
    {
        // Ensure restrictions based on: https://docs.microsoft.com/en-us/windows/win32/api/d3d11/nf-d3d11-id3d11devicecontext-copyresource
        SP_ASSERT(source != nullptr);
        SP_ASSERT(destination != nullptr);
        SP_ASSERT(source->GetRhiResource() != nullptr);
        SP_ASSERT(destination->GetRhiResource() != nullptr);
        SP_ASSERT(source->GetObjectId() != destination->GetObjectId());
        SP_ASSERT(source->GetFormat() == destination->GetFormat());
        SP_ASSERT(source->GetWidth() == destination->GetWidth());
        SP_ASSERT(source->GetHeight() == destination->GetHeight());
        SP_ASSERT(source->GetArrayLength() == destination->GetArrayLength());
        SP_ASSERT(source->GetMipCount() == destination->GetMipCount());

        m_rhi_device->GetContextRhi()->device_context->CopyResource(static_cast<ID3D11Resource*>(destination->GetRhiResource()), static_cast<ID3D11Resource*>(source->GetRhiResource()));
    }

    void RHI_CommandList::SetViewport(const RHI_Viewport& viewport) const
    {
        // Validate command list state
        SP_ASSERT(m_state == RHI_CommandListState::Recording);

        D3D11_VIEWPORT d3d11_viewport = {};
        d3d11_viewport.TopLeftX       = viewport.x;
        d3d11_viewport.TopLeftY       = viewport.y;
        d3d11_viewport.Width          = viewport.width;
        d3d11_viewport.Height         = viewport.height;
        d3d11_viewport.MinDepth       = viewport.depth_min;
        d3d11_viewport.MaxDepth       = viewport.depth_max;

        m_rhi_device->GetContextRhi()->device_context->RSSetViewports(1, &d3d11_viewport);
    }

    void RHI_CommandList::SetScissorRectangle(const Math::Rectangle& scissor_rectangle) const
    {
        const D3D11_RECT d3d11_rectangle =
        {
            static_cast<LONG>(scissor_rectangle.left),
            static_cast<LONG>(scissor_rectangle.top),
            static_cast<LONG>(scissor_rectangle.right),
            static_cast<LONG>(scissor_rectangle.bottom)
        };

        m_rhi_device->GetContextRhi()->device_context->RSSetScissorRects(1, &d3d11_rectangle);
    }

    void RHI_CommandList::SetBufferVertex(const RHI_VertexBuffer* buffer)
    {
        SP_ASSERT(buffer != nullptr);
        SP_ASSERT(buffer->GetRhiResource() != nullptr);

        ID3D11Buffer* vertex_buffer         = static_cast<ID3D11Buffer*>(buffer->GetRhiResource());
        UINT stride                         = buffer->GetStride();
        UINT offsets[]                      = { 0 };
        ID3D11DeviceContext* device_context = m_rhi_device->GetContextRhi()->device_context;

        // Get currently set buffer
        ID3D11Buffer* set_buffer    = nullptr;
        UINT set_stride             = buffer->GetStride();
        UINT set_offset             = 0;
        device_context->IAGetVertexBuffers(0, 1, &set_buffer, &set_stride, &set_offset);

        // Skip if already set
        if (set_buffer == vertex_buffer)
            return;

        // Set
        device_context->IASetVertexBuffers(0, 1, &vertex_buffer, &stride, offsets);

        if (m_profiler)
        {
            m_profiler->m_rhi_bindings_buffer_vertex++;
        }
    }

    void RHI_CommandList::SetBufferIndex(const RHI_IndexBuffer* buffer)
    {
        SP_ASSERT(buffer != nullptr);
        SP_ASSERT(buffer->GetRhiResource() != nullptr);

        ID3D11Buffer* index_buffer          = static_cast<ID3D11Buffer*>(buffer->GetRhiResource());
        const DXGI_FORMAT format            = buffer->Is16Bit() ? DXGI_FORMAT_R16_UINT : DXGI_FORMAT_R32_UINT;
        ID3D11DeviceContext* device_context = m_rhi_device->GetContextRhi()->device_context;

        // Get currently set buffer
        ID3D11Buffer* set_buffer = nullptr;
        DXGI_FORMAT set_format   = DXGI_FORMAT_UNKNOWN;
        UINT set_offset          = 0;
        device_context->IAGetIndexBuffer(&set_buffer, &set_format, &set_offset);

        // Skip if already set
        if (set_buffer == index_buffer)
            return;

        // Set
        device_context->IASetIndexBuffer(index_buffer, format, 0);

        if (m_profiler)
        {
            m_profiler->m_rhi_bindings_buffer_index++;
        }
    }

    void RHI_CommandList::SetConstantBuffer(const uint32_t slot, const uint8_t scope, RHI_ConstantBuffer* constant_buffer) const
    {
        void* buffer                        = static_cast<ID3D11Buffer*>(constant_buffer ? constant_buffer->GetRhiResource() : nullptr);
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

                if (m_profiler)
                {
                    m_profiler->m_rhi_bindings_buffer_constant++;
                }
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

                if (m_profiler)
                {
                    m_profiler->m_rhi_bindings_buffer_constant++;
                }
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

                if (m_profiler)
                {
                    m_profiler->m_rhi_bindings_buffer_constant++;
                }
            }
        }
    }

    void RHI_CommandList::SetSampler(const uint32_t slot, RHI_Sampler* sampler) const
    {
        const UINT start_slot               = slot;
        const UINT range                    = 1;
        const void* sampler_array[1]        = { sampler ? sampler->GetResource() : nullptr };
        ID3D11DeviceContext* device_context = m_rhi_device->GetContextRhi()->device_context;

        if (m_pso.IsCompute())
        {
            // Set only if not already set
            ID3D11SamplerState* set_sampler = nullptr;
            device_context->CSGetSamplers(start_slot, range, &set_sampler);
            if (set_sampler != sampler_array[0])
            {
                device_context->CSSetSamplers(start_slot, range, reinterpret_cast<ID3D11SamplerState* const*>(&sampler_array));

                if (m_profiler)
                {
                    m_profiler->m_rhi_bindings_sampler++;
                }
            }
        }
        else
        {
            // Set only if not already set
            ID3D11SamplerState* set_sampler = nullptr;
            device_context->PSGetSamplers(start_slot, range, &set_sampler);
            if (set_sampler != sampler_array[0])
            {
                device_context->PSSetSamplers(start_slot, range, reinterpret_cast<ID3D11SamplerState* const*>(&sampler_array));

                if (m_profiler)
                {
                    m_profiler->m_rhi_bindings_sampler++;
                }
            }
        }
    }

    void RHI_CommandList::SetTexture(const uint32_t slot, RHI_Texture* texture, const uint32_t mip_index /*= rhi_all_mips*/, uint32_t mip_range /*= 0*/, const bool uav /*= false*/)
    {
        // Validate texture
        if (texture)
        {
            if (uav)
            {
                SP_ASSERT(texture->IsUav());
            }
            else
            {
                SP_ASSERT(texture->IsSrv());
            }
        }

        bool mip_requested                  = mip_index != rhi_all_mips;
        const bool ranged                   = mip_index != rhi_all_mips && mip_range != 0;
        const UINT range                    = ranged ? (texture->GetMipCount() - (mip_requested ? mip_index : 0)): 1;
        ID3D11DeviceContext* device_context = m_rhi_device->GetContextRhi()->device_context;

        // Build resource array
        array<void*, m_resource_array_length_max> resources;
        resources.fill(nullptr);
        if (texture)
        {
            if (!ranged)
            {
                if (uav)
                {
                    resources[0] = mip_requested ? texture->GetRhiUavMip(mip_index) : texture->GetRhiUav();
                }
                else
                {
                    resources[0] = mip_requested ? texture->GetRhiSrvMip(mip_index) : texture->GetRhiSrv();
                }
            }
            else
            {
                for (uint32_t i = 0; i < range; i++)
                {
                    uint32_t mip_offset = mip_index + i;
                    resources[i] = uav ? texture->GetRhiUavMip(mip_offset) : texture->GetRhiSrvMip(mip_offset);
                }
            }
        }

        // UAV
        if (uav)
        {
            array<void*, m_resource_array_length_max> set_resources;
            set_resources.fill(nullptr);

            // Set if not already set
            device_context->CSGetUnorderedAccessViews(slot, range, reinterpret_cast<ID3D11UnorderedAccessView**>(&set_resources[0]));
            if (set_resources != resources)
            {
                device_context->CSSetUnorderedAccessViews(slot, range, reinterpret_cast<ID3D11UnorderedAccessView* const*>(resources.data()), nullptr);

                if (m_profiler)
                {
                    m_profiler->m_rhi_bindings_texture_storage++;
                }

                // Keep track of output textures
                m_output_textures[m_output_textures_index].texture = texture;
                m_output_textures[m_output_textures_index].slot    = slot;
                m_output_textures[m_output_textures_index].mip     = mip_index;
                m_output_textures[m_output_textures_index].ranged  = ranged;
                m_output_textures_index++;
            }
        }
        // SRV
        else
        {
            const uint8_t scope = m_pso.IsCompute() ? RHI_Shader_Compute : RHI_Shader_Pixel;

            array<void*, m_resource_array_length_max> set_resources;
            set_resources.fill(nullptr);

            if (scope & RHI_Shader_Pixel)
            {
                // Set if not already set
                device_context->PSGetShaderResources(slot, range, reinterpret_cast<ID3D11ShaderResourceView**>(&set_resources[0]));
                if (set_resources != resources)
                {
                    device_context->PSSetShaderResources(slot, range, reinterpret_cast<ID3D11ShaderResourceView* const*>(resources.data()));

                    if (m_profiler)
                    {
                        m_profiler->m_rhi_bindings_texture_sampled++;
                    }
                }
            }
            else if (scope & RHI_Shader_Compute)
            {
                // Set if not already set
                device_context->CSGetShaderResources(slot, range, reinterpret_cast<ID3D11ShaderResourceView**>(&set_resources[0]));
                if (set_resources != resources)
                {
                    device_context->CSSetShaderResources(slot, range, reinterpret_cast<ID3D11ShaderResourceView* const*>(resources.data()));

                    if (m_profiler)
                    {
                        m_profiler->m_rhi_bindings_texture_sampled++;
                    }
                }
            }
        }
    }

    void RHI_CommandList::SetStructuredBuffer(const uint32_t slot, RHI_StructuredBuffer* structured_buffer) const
    {
        array<void*, 1> view_array          = { structured_buffer ? structured_buffer->GetRhiUav() : nullptr };
        const UINT range                    = 1;
        ID3D11DeviceContext* device_context = m_rhi_device->GetContextRhi()->device_context;

        // Set only if not set
        ID3D11UnorderedAccessView* set_uav = nullptr;
        device_context->CSGetUnorderedAccessViews(slot, range, &set_uav);
        if (set_uav != view_array[0])
        {
            device_context->CSSetUnorderedAccessViews(slot, range, reinterpret_cast<ID3D11UnorderedAccessView* const*>(&view_array), nullptr);

            if (m_profiler)
            {
                m_profiler->m_rhi_bindings_buffer_structured++;
            }
        }
    }

    void RHI_CommandList::BeginTimestamp(void* query)
    {
        SP_ASSERT(m_rhi_device);
        m_rhi_device->QueryEnd(query);
    }

    void RHI_CommandList::EndTimestamp(void* query)
    {
        SP_ASSERT(m_rhi_device);
        m_rhi_device->QueryEnd(query);
    }

    float RHI_CommandList::GetTimestampDuration(void* query_start, void* query_end, const uint32_t pass_index)
    {
        SP_ASSERT(query_start != nullptr);
        SP_ASSERT(query_end != nullptr);
        SP_ASSERT(m_rhi_device != nullptr);

        RHI_Context* rhi_context = m_rhi_device->GetContextRhi();

        // Get start time
        uint64_t start_time = 0; 
        rhi_context->device_context->GetData(static_cast<ID3D11Query*>(query_start), &start_time, sizeof(start_time), 0);

        // Get end time
        uint64_t end_time = 0;
        rhi_context->device_context->GetData(static_cast<ID3D11Query*>(query_end), &end_time, sizeof(end_time), 0);

        // Compute duration in ms
        const uint64_t delta        = end_time - start_time;
        const double duration_ms    = (delta * 1000.0) / static_cast<double>(m_rhi_device->GetTimestampPeriod());

        return static_cast<float>(duration_ms);
    }

    uint32_t RHI_CommandList::GetGpuMemoryUsed(RHI_Device* rhi_device)
    {
        if (!m_memory_query_support)
            return 0;

        if (const PhysicalDevice* physical_device = rhi_device->GetPrimaryPhysicalDevice())
        {
            if (IDXGIAdapter3* adapter = static_cast<IDXGIAdapter3*>(physical_device->GetData()))
            {
                // Try to query video memory info
                DXGI_QUERY_VIDEO_MEMORY_INFO info = {};
                const HRESULT result = adapter->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &info);

                if (SUCCEEDED(result))
                {
                    return static_cast<uint32_t>(info.CurrentUsage / 1024 / 1024); // convert to MBs
                }
                else
                {
                    // Some integrated or older dedicated GPUs might not support video memory queries, log the error once and don't query again.
                    LOG_ERROR("Failed to get adapter memory info, %s", d3d11_utility::dxgi_error_to_string(result));
                    m_memory_query_support = false;
                }
            }
        }

        return 0;
    }

    void RHI_CommandList::BeginTimeblock(const char* name, const bool gpu_marker, const bool gpu_timing)
    {
        SP_ASSERT(name != nullptr);

        RHI_Context* rhi_context = m_rhi_device->GetContextRhi();

        // Allowed to profile ?
        if (rhi_context->gpu_profiling && gpu_timing && m_profiler)
        {
            m_profiler->TimeBlockStart(name, TimeBlockType::Cpu, this);
            m_profiler->TimeBlockStart(name, TimeBlockType::Gpu, this);
        }

        // Allowed to mark ?
        if (rhi_context->gpu_markers && gpu_marker)
        {
            m_rhi_device->GetContextRhi()->annotation->BeginEvent(FileSystem::StringToWstring(name).c_str());
        }
    }

    void RHI_CommandList::EndTimeblock()
    {
        // Allowed to mark ?
        RHI_Context* rhi_context = m_rhi_device->GetContextRhi();
        if (rhi_context->gpu_markers)
        {
            rhi_context->annotation->EndEvent();
        }

        // Allowed to profile ?
        if (rhi_context->gpu_profiling && m_profiler)
        {
            m_profiler->TimeBlockEnd(); // cpu
            m_profiler->TimeBlockEnd(); // gpu
        }
    }

    void RHI_CommandList::BeginMarker(const char* name)
    {
        if (m_rhi_device->GetContextRhi()->gpu_markers)
        {
            m_rhi_device->GetContextRhi()->annotation->BeginEvent(FileSystem::StringToWstring(name).c_str());
        }
    }

    void RHI_CommandList::EndMarker()
    {
        if (m_rhi_device->GetContextRhi()->gpu_markers)
        {
            m_rhi_device->GetContextRhi()->annotation->EndEvent();
        }
    }

    void RHI_CommandList::OnDraw()
    {
 
    }

    void RHI_CommandList::UnbindOutputTextures()
    {
        ID3D11DeviceContext* device_context = m_rhi_device->GetContextRhi()->device_context;

        array<void*, m_resource_array_length_max> resources;
        resources.fill(nullptr);

        for (OutputTexture& texture : m_output_textures)
        {
            if (texture.texture)
            {
                bool mip_requested = texture.mip != rhi_all_mips;
                const UINT range   = texture.ranged ? (texture.texture->GetMipCount() - (mip_requested ? texture.mip : 0)) : 1;

                device_context->CSSetUnorderedAccessViews(texture.slot, range, reinterpret_cast<ID3D11UnorderedAccessView* const*>(resources.data()), nullptr);

                if (m_profiler)
                {
                    m_profiler->m_rhi_bindings_texture_storage++;
                }

                texture.texture = nullptr;
            }
        }

        m_output_textures_index = 0;
    }

    void RHI_CommandList::GetDescriptorSetLayoutFromPipelineState(RHI_PipelineState& m_pso)
    {
        
    }
}
