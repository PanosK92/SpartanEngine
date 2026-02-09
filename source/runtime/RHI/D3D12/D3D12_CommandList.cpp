/*
Copyright(c) 2016-2024 Panos Karabelas

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
#include "pch.h"
#include "../RHI_Implementation.h"
#include "../RHI_CommandList.h"
#include "../RHI_Device.h"
#include "../RHI_Texture.h"
#include "../RHI_Buffer.h"
#include "../RHI_SwapChain.h"
#include "../RHI_PipelineState.h"
#include "../RHI_Pipeline.h"
#include "../RHI_Queue.h"
#include "../RHI_DescriptorSetLayout.h"
#include "../../Profiling/Profiler.h"
#include "../../Rendering/Renderer.h"
//====================================

//= NAMESPACES ===============
using namespace std;
using namespace spartan::math;
//============================

// forward declarations from d3d12_device.cpp
namespace spartan::d3d12_descriptors
{
    ID3D12DescriptorHeap* GetCbvSrvUavHeap();
    ID3D12Fence* GetGraphicsFence();
    uint64_t& GetGraphicsFenceValue();
    HANDLE GetFenceEvent();
}

// forward declaration from d3d12_swapchain.cpp
namespace spartan
{
    D3D12_CPU_DESCRIPTOR_HANDLE get_swapchain_rtv_handle(const RHI_SwapChain* swapchain);
}

namespace spartan
{
    RHI_CommandList::RHI_CommandList(RHI_Queue* queue, void* cmd_pool, const char* name)
    {
        SP_ASSERT(cmd_pool != nullptr);
        SP_ASSERT(queue != nullptr);

        m_rhi_cmd_pool_resource = cmd_pool;
        m_queue                 = queue;
        m_object_name           = name;

        // determine command list type based on queue type
        D3D12_COMMAND_LIST_TYPE cmd_list_type = D3D12_COMMAND_LIST_TYPE_DIRECT;
        if (queue->GetType() == RHI_Queue_Type::Compute)
            cmd_list_type = D3D12_COMMAND_LIST_TYPE_COMPUTE;
        else if (queue->GetType() == RHI_Queue_Type::Copy)
            cmd_list_type = D3D12_COMMAND_LIST_TYPE_COPY;

        // create command list
        ID3D12GraphicsCommandList* cmd_list = nullptr;
        SP_ASSERT_MSG(d3d12_utility::error::check(RHI_Context::device->CreateCommandList(
            0,
            cmd_list_type,
            static_cast<ID3D12CommandAllocator*>(cmd_pool),
            nullptr,
            IID_PPV_ARGS(&cmd_list)
        )), "Failed to create command list");
        m_rhi_resource = cmd_list;

        // close the command list initially - it will be reset in Begin()
        cmd_list->Close();

        // create fence for this command list
        ID3D12Fence* fence = nullptr;
        SP_ASSERT_MSG(d3d12_utility::error::check(RHI_Context::device->CreateFence(
            0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence))),
            "Failed to create fence for command list");
        m_rhi_fence = fence;

        m_rhi_fence_value = 0;
        m_rhi_fence_event = CreateEvent(nullptr, FALSE, FALSE, nullptr);
        SP_ASSERT_MSG(m_rhi_fence_event != nullptr, "Failed to create fence event");

        m_state = RHI_CommandListState::Idle;
    }

    RHI_CommandList::~RHI_CommandList()
    {
        // wait in case it's still in use by the gpu
        RHI_Device::QueueWaitAll();

        // clean up fence
        if (m_rhi_fence_event)
        {
            CloseHandle(static_cast<HANDLE>(m_rhi_fence_event));
            m_rhi_fence_event = nullptr;
        }

        if (m_rhi_fence)
        {
            static_cast<ID3D12Fence*>(m_rhi_fence)->Release();
            m_rhi_fence = nullptr;
        }

        // clean up command list
        if (m_rhi_resource)
        {
            static_cast<ID3D12GraphicsCommandList*>(m_rhi_resource)->Release();
            m_rhi_resource = nullptr;
        }

        // clean up command allocator
        if (m_rhi_cmd_pool_resource)
        {
            static_cast<ID3D12CommandAllocator*>(m_rhi_cmd_pool_resource)->Release();
            m_rhi_cmd_pool_resource = nullptr;
        }
    }

    void RHI_CommandList::Begin()
    {
        // if the command list is in use, wait for it
        if (m_state == RHI_CommandListState::Submitted)
        {
            WaitForExecution();
        }

        SP_ASSERT(m_rhi_resource != nullptr);
        SP_ASSERT(m_state == RHI_CommandListState::Idle);

        // reset the command allocator
        ID3D12CommandAllocator* allocator = static_cast<ID3D12CommandAllocator*>(m_rhi_cmd_pool_resource);
        SP_ASSERT_MSG(d3d12_utility::error::check(allocator->Reset()), "Failed to reset command allocator");

        // reset the command list
        ID3D12GraphicsCommandList* cmd_list = static_cast<ID3D12GraphicsCommandList*>(m_rhi_resource);
        SP_ASSERT_MSG(d3d12_utility::error::check(cmd_list->Reset(allocator, nullptr)), "Failed to reset command list");

        // set the descriptor heaps
        ID3D12DescriptorHeap* heaps[] = { d3d12_descriptors::GetCbvSrvUavHeap() };
        if (heaps[0])
        {
            cmd_list->SetDescriptorHeaps(1, heaps);
        }

        m_state = RHI_CommandListState::Recording;

        // reset cached state
        m_buffer_id_vertex = 0;
        m_buffer_id_index  = 0;
    }

    void RHI_CommandList::Submit(RHI_SyncPrimitive* semaphore_wait, const bool is_immediate, RHI_SyncPrimitive* semaphore_signal /*= nullptr*/)
    {
        SP_ASSERT(m_rhi_resource != nullptr);
        SP_ASSERT(m_state == RHI_CommandListState::Recording);

        // end render pass if active
        RenderPassEnd();

        // close the command list
        ID3D12GraphicsCommandList* cmd_list = static_cast<ID3D12GraphicsCommandList*>(m_rhi_resource);
        SP_ASSERT_MSG(d3d12_utility::error::check(cmd_list->Close()), "Failed to close command list");

        // execute the command list
        ID3D12CommandQueue* queue = static_cast<ID3D12CommandQueue*>(RHI_Device::GetQueueRhiResource(m_queue->GetType()));
        ID3D12CommandList* cmd_lists[] = { cmd_list };
        queue->ExecuteCommandLists(1, cmd_lists);

        // signal the fence
        m_rhi_fence_value++;
        queue->Signal(static_cast<ID3D12Fence*>(m_rhi_fence), m_rhi_fence_value);

        m_state = RHI_CommandListState::Submitted;

        // if immediate, wait for completion now
        if (is_immediate)
        {
            WaitForExecution();
        }
    }

    void RHI_CommandList::WaitForExecution(const bool log_wait_time /*= false*/)
    {
        if (m_state != RHI_CommandListState::Submitted)
            return;

        ID3D12Fence* fence = static_cast<ID3D12Fence*>(m_rhi_fence);
        HANDLE fence_event = static_cast<HANDLE>(m_rhi_fence_event);

        // check if fence is completed
        if (fence->GetCompletedValue() < m_rhi_fence_value)
        {
            // wait for fence
            fence->SetEventOnCompletion(m_rhi_fence_value, fence_event);
            WaitForSingleObject(fence_event, INFINITE);
        }

        m_state = RHI_CommandListState::Idle;
    }

    void RHI_CommandList::SetPipelineState(RHI_PipelineState& pso)
    {
        pso.Prepare();
        SP_ASSERT(m_state == RHI_CommandListState::Recording);

        // get or create the pipeline
        RHI_Pipeline* pipeline = nullptr;
        RHI_DescriptorSetLayout* descriptor_set_layout = nullptr;
        RHI_Device::GetOrCreatePipeline(pso, pipeline, descriptor_set_layout);

        if (pipeline && pipeline->GetRhiResource())
        {
            ID3D12GraphicsCommandList* cmd_list = static_cast<ID3D12GraphicsCommandList*>(m_rhi_resource);
            
            // set pipeline state
            cmd_list->SetPipelineState(static_cast<ID3D12PipelineState*>(pipeline->GetRhiResource()));

            // set root signature
            if (pipeline->GetRhiResourceLayout())
            {
                cmd_list->SetGraphicsRootSignature(static_cast<ID3D12RootSignature*>(pipeline->GetRhiResourceLayout()));
            }

            // set primitive topology
            cmd_list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        }

        m_pso = pso;

        // begin render pass
        RenderPassBegin();
    }

    void RHI_CommandList::RenderPassBegin()
    {
        if (m_render_pass_active)
            return;

        SP_ASSERT(m_state == RHI_CommandListState::Recording);

        // set render targets and clear
        if (m_pso.render_target_swapchain)
        {
            RHI_SwapChain* swapchain = m_pso.render_target_swapchain;
            
            // get rtv handle for current backbuffer
            D3D12_CPU_DESCRIPTOR_HANDLE rtv_handle = get_swapchain_rtv_handle(swapchain);

            ID3D12GraphicsCommandList* cmd_list = static_cast<ID3D12GraphicsCommandList*>(m_rhi_resource);
            cmd_list->OMSetRenderTargets(1, &rtv_handle, FALSE, nullptr);

            // clear if requested
            if (m_pso.clear_color[0] != rhi_color_dont_care)
            {
                float clear_color[4] = { 
                    m_pso.clear_color[0].r, 
                    m_pso.clear_color[0].g, 
                    m_pso.clear_color[0].b, 
                    m_pso.clear_color[0].a 
                };
                cmd_list->ClearRenderTargetView(rtv_handle, clear_color, 0, nullptr);
            }

            // set viewport
            D3D12_VIEWPORT viewport = {};
            viewport.Width    = static_cast<float>(swapchain->GetWidth());
            viewport.Height   = static_cast<float>(swapchain->GetHeight());
            viewport.MinDepth = 0.0f;
            viewport.MaxDepth = 1.0f;
            cmd_list->RSSetViewports(1, &viewport);

            // set scissor rect
            D3D12_RECT scissor = {};
            scissor.right  = static_cast<LONG>(swapchain->GetWidth());
            scissor.bottom = static_cast<LONG>(swapchain->GetHeight());
            cmd_list->RSSetScissorRects(1, &scissor);
        }

        m_render_pass_active = true;
    }
    
    void RHI_CommandList::RenderPassEnd()
    {
        if (!m_render_pass_active)
            return;

        m_render_pass_active = false;
    }

    void RHI_CommandList::ClearPipelineStateRenderTargets(RHI_PipelineState& pipeline_state)
    {
        // implemented in RenderPassBegin
    }

    void RHI_CommandList::ClearTexture(
        RHI_Texture* texture,
        const Color& clear_color     /*= rhi_color_load*/,
        const float clear_depth      /*= rhi_depth_load*/,
        const uint32_t clear_stencil /*= rhi_stencil_load*/
    )
    {
        // todo: implement texture clearing
    }

    void RHI_CommandList::Draw(const uint32_t vertex_count, uint32_t vertex_start_index /*= 0*/)
    {
        SP_ASSERT(m_state == RHI_CommandListState::Recording);

        static_cast<ID3D12GraphicsCommandList*>(m_rhi_resource)->DrawInstanced(
            vertex_count,
            1,
            vertex_start_index,
            0
        );
        Profiler::m_rhi_draw++;
    }
    
    void RHI_CommandList::DrawIndexed(const uint32_t index_count, const uint32_t index_offset, const uint32_t vertex_offset, const uint32_t instance_start_index, const uint32_t instance_count)
    {
        SP_ASSERT(m_state == RHI_CommandListState::Recording);

        static_cast<ID3D12GraphicsCommandList*>(m_rhi_resource)->DrawIndexedInstanced(
            index_count,
            instance_count,
            index_offset,
            vertex_offset,
            instance_start_index
        );

        Profiler::m_rhi_draw++;
    }

    void RHI_CommandList::DrawIndexedIndirectCount(RHI_Buffer* args_buffer, const uint32_t args_offset, RHI_Buffer* count_buffer, const uint32_t count_offset, const uint32_t max_draw_count)
    {
        SP_ASSERT(m_state == RHI_CommandListState::Recording);
        SP_ASSERT(args_buffer  != nullptr);
        SP_ASSERT(count_buffer != nullptr);

        // lazily create the command signature for indirect indexed draws
        static ID3D12CommandSignature* command_signature = nullptr;
        if (!command_signature)
        {
            D3D12_INDIRECT_ARGUMENT_DESC arg_desc = {};
            arg_desc.Type                         = D3D12_INDIRECT_ARGUMENT_TYPE_DRAW_INDEXED;

            D3D12_COMMAND_SIGNATURE_DESC desc = {};
            desc.ByteStride                  = sizeof(D3D12_DRAW_INDEXED_ARGUMENTS);
            desc.NumArgumentDescs            = 1;
            desc.pArgumentDescs              = &arg_desc;

            RHI_Context::device->CreateCommandSignature(&desc, nullptr, IID_PPV_ARGS(&command_signature));
        }

        static_cast<ID3D12GraphicsCommandList*>(m_rhi_resource)->ExecuteIndirect(
            command_signature,
            max_draw_count,
            static_cast<ID3D12Resource*>(args_buffer->GetRhiResource()),
            static_cast<UINT64>(args_offset),
            static_cast<ID3D12Resource*>(count_buffer->GetRhiResource()),
            static_cast<UINT64>(count_offset)
        );

        Profiler::m_rhi_draw++;
    }

    void RHI_CommandList::Dispatch(uint32_t x, uint32_t y, uint32_t z)
    {
        SP_ASSERT(m_state == RHI_CommandListState::Recording);

        static_cast<ID3D12GraphicsCommandList*>(m_rhi_resource)->Dispatch(x, y, z);
    }

    void RHI_CommandList::TraceRays(const uint32_t width, const uint32_t height)
    {
        // todo: implement ray tracing
    }

    void RHI_CommandList::SetAccelerationStructure(Renderer_BindingsSrv slot, RHI_AccelerationStructure* tlas)
    {
        // todo: implement acceleration structure binding
    }

    void RHI_CommandList::Blit(RHI_Texture* source, RHI_Texture* destination, const bool blit_mips, const float resolution_scale)
    {
        // todo: implement blit
    }

    void RHI_CommandList::Blit(RHI_Texture* source, RHI_SwapChain* destination)
    {
        SP_ASSERT_MSG((source->GetFlags() & RHI_Texture_ClearBlit) != 0, "The texture needs the RHI_Texture_ClearOrBlit flag");
        SP_ASSERT_MSG(source->GetWidth() <= destination->GetWidth() && source->GetHeight() <= destination->GetHeight(),
            "The source texture dimension(s) are larger than the those of the destination texture");
        // todo: implement blit to swapchain
    }

    void RHI_CommandList::BlitToXrSwapchain(RHI_Texture* source)
    {
        // todo: implement xr swapchain blit for d3d12
    }

    void RHI_CommandList::Copy(RHI_Texture* source, RHI_Texture* destination, const bool blit_mips)
    {
        // todo: implement copy
    }

    void RHI_CommandList::Copy(RHI_Texture* source, RHI_SwapChain* destination)
    {
        SP_ASSERT_MSG((source->GetFlags() & RHI_Texture_ClearBlit) != 0, "The texture needs the RHI_Texture_ClearOrBlit flag");
        SP_ASSERT(source->GetWidth() == destination->GetWidth());
        SP_ASSERT(source->GetHeight() == destination->GetHeight());
        // todo: implement copy to swapchain
    }

    void RHI_CommandList::SetViewport(const RHI_Viewport& viewport) const
    {
        SP_ASSERT(m_state == RHI_CommandListState::Recording);

        D3D12_VIEWPORT d3d12_viewport = {};
        d3d12_viewport.TopLeftX       = viewport.x;
        d3d12_viewport.TopLeftY       = viewport.y;
        d3d12_viewport.Width          = viewport.width;
        d3d12_viewport.Height         = viewport.height;
        d3d12_viewport.MinDepth       = viewport.depth_min;
        d3d12_viewport.MaxDepth       = viewport.depth_max;

        static_cast<ID3D12GraphicsCommandList*>(m_rhi_resource)->RSSetViewports(1, &d3d12_viewport);
    }
    
    void RHI_CommandList::SetScissorRectangle(const math::Rectangle& scissor_rectangle) const
    {
        SP_ASSERT(m_state == RHI_CommandListState::Recording);

        const D3D12_RECT d3d12_rectangle =
        {
            static_cast<LONG>(scissor_rectangle.x),
            static_cast<LONG>(scissor_rectangle.y),
            static_cast<LONG>(scissor_rectangle.x + scissor_rectangle.width),
            static_cast<LONG>(scissor_rectangle.y + scissor_rectangle.height)
        };

        static_cast<ID3D12GraphicsCommandList*>(m_rhi_resource)->RSSetScissorRects(1, &d3d12_rectangle);
    }

    void RHI_CommandList::SetCullMode(const RHI_CullMode cull_mode)
    {
        SP_ASSERT(m_state == RHI_CommandListState::Recording);
        // d3d12 cull mode is set via pipeline state
    }

    void RHI_CommandList::SetBufferVertex(const RHI_Buffer* vertex, RHI_Buffer* instance)
    {
        SP_ASSERT(m_state == RHI_CommandListState::Recording);
        SP_ASSERT(vertex && vertex->GetRhiResource());
    
        D3D12_VERTEX_BUFFER_VIEW vertex_buffer_views[2] = {};
        
        // vertex buffer (slot 0)
        vertex_buffer_views[0].BufferLocation = static_cast<ID3D12Resource*>(vertex->GetRhiResource())->GetGPUVirtualAddress();
        vertex_buffer_views[0].StrideInBytes  = static_cast<UINT>(vertex->GetStride());
        vertex_buffer_views[0].SizeInBytes    = static_cast<UINT>(vertex->GetObjectSize());
    
        UINT num_views = 1;
        uint64_t new_buffer_id = vertex->GetObjectId();
        
        if (instance && instance->GetRhiResource())
        {
            vertex_buffer_views[1].BufferLocation = static_cast<ID3D12Resource*>(instance->GetRhiResource())->GetGPUVirtualAddress();
            vertex_buffer_views[1].StrideInBytes  = static_cast<UINT>(instance->GetStride());
            vertex_buffer_views[1].SizeInBytes    = static_cast<UINT>(instance->GetObjectSize());
            num_views = 2;
            new_buffer_id = (new_buffer_id << 16) | instance->GetObjectId();
        }
    
        if (m_buffer_id_vertex != new_buffer_id)
        {
            static_cast<ID3D12GraphicsCommandList*>(m_rhi_resource)->IASetVertexBuffers(0, num_views, vertex_buffer_views);
            m_buffer_id_vertex = new_buffer_id;
            Profiler::m_rhi_bindings_buffer_vertex++;
        }
    }
        
    void RHI_CommandList::SetBufferIndex(const RHI_Buffer* buffer)
    {
        SP_ASSERT(m_state == RHI_CommandListState::Recording);
        SP_ASSERT(buffer && buffer->GetRhiResource());

        if (m_buffer_id_index == buffer->GetObjectId())
            return;

        bool is_16_bit = buffer->GetStride() == sizeof(uint16_t);

        D3D12_INDEX_BUFFER_VIEW index_buffer_view = {};
        index_buffer_view.BufferLocation = static_cast<ID3D12Resource*>(buffer->GetRhiResource())->GetGPUVirtualAddress();
        index_buffer_view.SizeInBytes    = static_cast<UINT>(buffer->GetObjectSize());
        index_buffer_view.Format         = is_16_bit ? DXGI_FORMAT_R16_UINT : DXGI_FORMAT_R32_UINT;

        static_cast<ID3D12GraphicsCommandList*>(m_rhi_resource)->IASetIndexBuffer(&index_buffer_view);

        m_buffer_id_index = buffer->GetObjectId();
        Profiler::m_rhi_bindings_buffer_index++;
    }
    
    void RHI_CommandList::SetConstantBuffer(const uint32_t slot, RHI_Buffer* constant_buffer) const
    {
        // todo: implement constant buffer binding
    }

    void RHI_CommandList::PushConstants(const uint32_t offset, const uint32_t size, const void* data)
    {
        SP_ASSERT(m_state == RHI_CommandListState::Recording);
        SP_ASSERT(data != nullptr);

        ID3D12GraphicsCommandList* cmd_list = static_cast<ID3D12GraphicsCommandList*>(m_rhi_resource);
        
        // d3d12 root constants are set as 32-bit values
        const uint32_t num_32bit_values = size / 4;
        cmd_list->SetGraphicsRoot32BitConstants(0, num_32bit_values, data, offset / 4);
    }

    void RHI_CommandList::SetBuffer(const uint32_t slot, RHI_Buffer* buffer) const
    {
        // todo: implement buffer binding
    }
    
    void RHI_CommandList::SetTexture(const uint32_t slot, RHI_Texture* texture, const uint32_t mip_index /*= rhi_all_mips*/, uint32_t mip_range /*= 0*/, const bool uav /*= false*/)
    {
        SP_ASSERT(m_state == RHI_CommandListState::Recording);

        if (!texture || !texture->GetRhiSrv())
            return;

        ID3D12GraphicsCommandList* cmd_list = static_cast<ID3D12GraphicsCommandList*>(m_rhi_resource);

        // set the srv descriptor table at root parameter 1
        D3D12_GPU_DESCRIPTOR_HANDLE srv_handle = {};
        srv_handle.ptr = reinterpret_cast<uint64_t>(texture->GetRhiSrv());
        cmd_list->SetGraphicsRootDescriptorTable(1, srv_handle);
    }

    uint32_t RHI_CommandList::BeginTimestamp()
    {
        // todo: implement timestamps
        return 0;
    }

    void RHI_CommandList::EndTimestamp()
    {
        // todo: implement timestamps
    }

    float RHI_CommandList::GetTimestampResult(const uint32_t timestamp_index)
    {
        return 0.0f;
    }

    void RHI_CommandList::BeginOcclusionQuery(const uint64_t entity_id)
    {
        // todo: implement occlusion queries
    }

    void RHI_CommandList::EndOcclusionQuery()
    {
        // todo: implement occlusion queries
    }

    bool RHI_CommandList::GetOcclusionQueryResult(const uint64_t entity_id)
    {
        return false;
    }

    void RHI_CommandList::UpdateOcclusionQueries()
    {
        // todo: implement occlusion queries
    }

    void RHI_CommandList::BeginTimeblock(const char* name, const bool gpu_marker, const bool gpu_timing)
    {
        // d3d12 marker/timing implementation would go here
    }

    void RHI_CommandList::EndTimeblock()
    {
        // end of timeblock
    }

    void RHI_CommandList::BeginMarker(const char* name)
    {
        // todo: implement pix markers
    }

    void RHI_CommandList::EndMarker()
    {
        // todo: implement pix markers
    }

    void RHI_CommandList::UpdateBuffer(RHI_Buffer* buffer, const uint64_t offset, const uint64_t size, const void* data)
    {
        // for d3d12, we use mapped memory directly
    }

    void RHI_CommandList::InsertBarrier(const RHI_Barrier& barrier)
    {
        SP_ASSERT(m_state == RHI_CommandListState::Recording);

        ID3D12GraphicsCommandList* cmd_list = static_cast<ID3D12GraphicsCommandList*>(m_rhi_resource);

        if (barrier.texture)
        {
            ID3D12Resource* resource = static_cast<ID3D12Resource*>(barrier.texture->GetRhiResource());
            if (!resource)
                return;

            D3D12_RESOURCE_BARRIER d3d12_barrier = {};
            d3d12_barrier.Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            d3d12_barrier.Transition.pResource   = resource;
            d3d12_barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;

            // map rhi layout to d3d12 states
            switch (barrier.layout)
            {
                case RHI_Image_Layout::General:
                    d3d12_barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COMMON;
                    break;
                case RHI_Image_Layout::Shader_Read:
                    d3d12_barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
                    break;
                case RHI_Image_Layout::Attachment:
                    d3d12_barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
                    break;
                case RHI_Image_Layout::Transfer_Source:
                    d3d12_barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
                    break;
                case RHI_Image_Layout::Transfer_Destination:
                    d3d12_barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;
                    break;
                case RHI_Image_Layout::Present_Source:
                    d3d12_barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
                    break;
                default:
                    d3d12_barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COMMON;
                    break;
            }

            // todo: track previous state properly
            d3d12_barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COMMON;

            cmd_list->ResourceBarrier(1, &d3d12_barrier);
        }
    }

    void RHI_CommandList::FlushBarriers()
    {
        // barriers are flushed immediately in our implementation
    }

    void RHI_CommandList::InsertBarrier(RHI_Texture* texture, RHI_Image_Layout layout, uint32_t mip, uint32_t mip_range)
    {
        InsertBarrier(RHI_Barrier::image_layout(texture, layout, mip, mip_range));
    }

    void RHI_CommandList::InsertBarrier(RHI_Texture* texture, RHI_BarrierType sync_type)
    {
        InsertBarrier(RHI_Barrier::image_sync(texture, sync_type));
    }

    void RHI_CommandList::InsertBarrier(RHI_Buffer* buffer)
    {
        InsertBarrier(RHI_Barrier::buffer_sync(buffer));
    }

    void RHI_CommandList::InsertBarrier(void* image, RHI_Format format, uint32_t mip_index, uint32_t mip_range, uint32_t array_length, RHI_Image_Layout layout)
    {
        InsertBarrier(RHI_Barrier::image_layout(image, format, mip_index, mip_range, array_length, layout));
    }

    RHI_Image_Layout RHI_CommandList::GetImageLayout(void* image, const uint32_t mip_index)
    {
        return RHI_Image_Layout::Max;
    }

    void RHI_CommandList::CopyTextureToBuffer(RHI_Texture* source, RHI_Buffer* destination)
    {
        // todo: implement texture to buffer copy
    }

    RHI_CommandList* RHI_CommandList::ImmediateExecutionBegin(const RHI_Queue_Type queue_type)
    {
        RHI_Queue* queue = RHI_Device::GetQueue(queue_type);
        if (!queue)
            return nullptr;

        RHI_CommandList* cmd_list = queue->NextCommandList();
        cmd_list->Begin();
        return cmd_list;
    }
    
    void RHI_CommandList::ImmediateExecutionEnd(RHI_CommandList* cmd_list)
    {
        if (cmd_list)
        {
            cmd_list->Submit(nullptr, true);
        }
    }

    void RHI_CommandList::ImmediateExecutionShutdown()
    {
        // nothing special needed for d3d12
    }

    void* RHI_CommandList::GetRhiResourcePipeline()
    {
        if (m_pipeline)
        {
            return m_pipeline->GetRhiResource();
        }
        return nullptr;
    }
}
