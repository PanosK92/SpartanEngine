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

//= INCLUDES =========================
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
#include "../../Profiling/Profiler.h"
#include "../../Rendering/Renderer.h"
//====================================

//= NAMESPACES ===============
using namespace std;
using namespace Spartan::Math;
//============================

namespace Spartan
{
    RHI_CommandList::RHI_CommandList(Context* context, void* cmd_pool, const char* name)
    {
        m_renderer    = context->GetSubsystem<Renderer>();
        m_profiler    = context->GetSubsystem<Profiler>();
        m_rhi_device  = m_renderer->GetRhiDevice().get();
        m_object_name = name;
        m_timestamps.fill(0);

        //ID3D12CommandAllocator* allocator = static_cast<ID3D12CommandAllocator*>(m_rhi_device->GetCommandPoolGraphics());
        //d3d12_utility::error::check(m_rhi_device->GetContextRhi()->device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator, nullptr, IID_PPV_ARGS(reinterpret_cast<ID3D12GraphicsCommandList**>(&m_resource))));
    }
    
    RHI_CommandList::~RHI_CommandList()
    {
        // Wait in case it's still in use by the GPU
        m_rhi_device->QueueWaitAll();

        // Command list
        d3d12_utility::release<ID3D12CommandQueue>(m_resource);
    }

    void RHI_CommandList::Begin()
    {
        // If the command list is in use, wait for it
        if (m_state == RHI_CommandListState::Submitted)
        {
            Wait();
        }

        // Validate a few things
        SP_ASSERT(m_resource != nullptr);
        SP_ASSERT(m_rhi_device != nullptr);
        SP_ASSERT(m_state == RHI_CommandListState::Idle);

        // Unlike Vulkan, D3D12 wraps both begin and reset under Reset().
        //SP_ASSERT(d3d12_utility::error::check(static_cast<ID3D12GraphicsCommandList*>(m_resource)->Reset(static_cast<ID3D12CommandAllocator*>(m_rhi_device->GetCommandPoolGraphics()), nullptr)) && "Failed to reset command list");

        m_state = RHI_CommandListState::Recording;
    }

    bool RHI_CommandList::End()
    {
        // Verify a few things
        SP_ASSERT(m_resource != nullptr);
        SP_ASSERT(m_state == RHI_CommandListState::Recording);

        if (!d3d12_utility::error::check(static_cast<ID3D12GraphicsCommandList*>(m_resource)->Close()))
            return false;

        m_state = RHI_CommandListState::Ended;
        return true;
    }

    bool RHI_CommandList::Submit()
    {
        return true;
    }

    bool RHI_CommandList::Reset()
    {
        // Verify a few things
        SP_ASSERT(m_resource != nullptr);
        SP_ASSERT(m_rhi_device != nullptr);
        SP_ASSERT(m_state == RHI_CommandListState::Recording);

        lock_guard<mutex> guard(m_mutex_reset);

       // if (!d3d12_utility::error::check(static_cast<ID3D12GraphicsCommandList*>(m_resource)->Reset(static_cast<ID3D12CommandAllocator*>(m_rhi_device->GetCommandPoolGraphics()), nullptr)))
            //return false;

        m_state = RHI_CommandListState::Idle;
        return true;
    }

    void RHI_CommandList::BeginRenderPass()
    {

    }
    
    void RHI_CommandList::EndRenderPass()
    {

    }

    void RHI_CommandList::ClearPipelineStateRenderTargets(RHI_PipelineState& pipeline_state)
    {

    }

    void RHI_CommandList::ClearRenderTarget(RHI_Texture* texture,
        const uint32_t color_index         /*= 0*/,
        const uint32_t depth_stencil_index /*= 0*/,
        const bool storage                 /*= false*/,
        const Math::Vector4& clear_color   /*= rhi_color_load*/,
        const float clear_depth            /*= rhi_depth_load*/,
        const float clear_stencil          /*= rhi_stencil_load*/
    )
    {

    }

    void RHI_CommandList::Draw(const uint32_t vertex_count, uint32_t vertex_start_index /*= 0*/)
    {
        // Validate command list state
        SP_ASSERT(m_state == RHI_CommandListState::Recording);

        // Ensure correct state before attempting to draw
        OnDraw();

        // Draw
        static_cast<ID3D12GraphicsCommandList*>(m_resource)->DrawInstanced(
            vertex_count,       // VertexCountPerInstance
            1,                  // InstanceCount
            vertex_start_index, // StartVertexLocation
            0                   // StartInstanceLocation
        );

        // Profiler
        m_profiler->m_rhi_draw++;
    }
    
    void RHI_CommandList::DrawIndexed(const uint32_t index_count, const uint32_t index_offset, const uint32_t vertex_offset)
    {
        // Validate command list state
        SP_ASSERT(m_state == RHI_CommandListState::Recording);

        // Ensure correct state before attempting to draw
        OnDraw();

        // Draw
        static_cast<ID3D12GraphicsCommandList*>(m_resource)->DrawIndexedInstanced(
            index_count,   // IndexCountPerInstance
            1,             // InstanceCount
            index_offset,  // StartIndexLocation
            vertex_offset, // BaseVertexLocation
            0              // StartInstanceLocation
        );

        // Profile
        m_profiler->m_rhi_draw++;
    }
  
    void RHI_CommandList::Dispatch(uint32_t x, uint32_t y, uint32_t z, bool async /*= false*/)
    {
        // Validate command list state
        SP_ASSERT(m_state == RHI_CommandListState::Recording);

        // Ensure correct state before attempting to draw
        OnDraw();

        // Dispatch
        static_cast<ID3D12GraphicsCommandList*>(m_resource)->Dispatch(x, y, z);

        // Profiler
        m_profiler->m_rhi_dispatch++;
    }

    void RHI_CommandList::Blit(RHI_Texture* source, RHI_Texture* destination)
    {

    }

    void RHI_CommandList::SetViewport(const RHI_Viewport& viewport) const
    {
        // Validate command list state
        SP_ASSERT(m_state == RHI_CommandListState::Recording);

        D3D12_VIEWPORT d3d12_viewport = {};
        d3d12_viewport.TopLeftX       = viewport.x;
        d3d12_viewport.TopLeftY       = viewport.y;
        d3d12_viewport.Width          = viewport.width;
        d3d12_viewport.Height         = viewport.height;
        d3d12_viewport.MinDepth       = viewport.depth_min;
        d3d12_viewport.MaxDepth       = viewport.depth_max;

        static_cast<ID3D12GraphicsCommandList*>(m_resource)->RSSetViewports(1, &d3d12_viewport);
    }
    
    void RHI_CommandList::SetScissorRectangle(const Math::Rectangle& scissor_rectangle) const
    {
        // Validate command list state
        SP_ASSERT(m_state == RHI_CommandListState::Recording);

        const D3D12_RECT d3d12_rectangle =
        {
            static_cast<LONG>(scissor_rectangle.left),
            static_cast<LONG>(scissor_rectangle.top),
            static_cast<LONG>(scissor_rectangle.right),
            static_cast<LONG>(scissor_rectangle.bottom)
        };

        static_cast<ID3D12GraphicsCommandList*>(m_resource)->RSSetScissorRects(1, &d3d12_rectangle);
    }
    
    void RHI_CommandList::SetBufferVertex(const RHI_VertexBuffer* buffer, const uint64_t offset /*= 0*/)
    {
    
    }
    
    void RHI_CommandList::SetBufferIndex(const RHI_IndexBuffer* buffer, const uint64_t offset /*= 0*/)
    {
    
    }
    
    void RHI_CommandList::SetConstantBuffer(const uint32_t slot, const uint8_t scope, RHI_ConstantBuffer* constant_buffer) const
    {

    }

    void RHI_CommandList::SetStructuredBuffer(const uint32_t slot, RHI_StructuredBuffer* structured_buffer) const
    {

    }
    
    void RHI_CommandList::SetSampler(const uint32_t slot, RHI_Sampler* sampler) const
    {

    }

    void RHI_CommandList::SetTexture(const uint32_t slot, RHI_Texture* texture, const int mip /*= -1*/, const bool ranged /*= false*/, const bool uav /*= false*/)
    {

    }

    void RHI_CommandList::Timestamp_Start(void* query)
    {

    }

    void RHI_CommandList::Timestamp_End(void* query)
    {

    }

    float RHI_CommandList::Timestamp_GetDuration(void* query_start, void* query_end, const uint32_t pass_index)
    {
        return 0.0f;
    }

    uint32_t RHI_CommandList::Gpu_GetMemoryUsed(RHI_Device* rhi_device)
    {
        return 0;
    }

    void RHI_CommandList::Timeblock_Start(const char* name, const bool profile, const bool gpu_markers)
    {

    }

    void RHI_CommandList::Timeblock_End()
    {

    }

    void RHI_CommandList::StartMarker(const char* name)
    {

    }

    void RHI_CommandList::EndMarker()
    {

    }

    void RHI_CommandList::OnDraw()
    {

    }

    void RHI_CommandList::UnbindOutputTextures()
    {

    }

    void RHI_CommandList::Descriptors_GetLayoutFromPipelineState(RHI_PipelineState& pipeline_state)
    {

    }
}
