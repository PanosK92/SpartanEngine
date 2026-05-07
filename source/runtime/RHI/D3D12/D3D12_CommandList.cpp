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
#include "../../Core/Debugging.h"
#include "../../Core/Breadcrumbs.h"
#include "D3D12_Internal.h"
#include <cstring>
#include <unordered_map>
#include <mutex>
#include <condition_variable>
//====================================

//= NAMESPACES ===============
using namespace std;
using namespace spartan::math;
//============================

namespace spartan
{
    D3D12_CPU_DESCRIPTOR_HANDLE get_swapchain_rtv_handle(const RHI_SwapChain* swapchain);
}

namespace spartan
{
    // per-cmd-list pending bindings state (srv/uav slots that SetTexture/SetBuffer wrote to)
    namespace cmd_state
    {
        struct PendingBindings
        {
            // source cpu handles (from cpu staging heap) per slot - zero means null
            D3D12_CPU_DESCRIPTOR_HANDLE srv[27] = {};
            D3D12_CPU_DESCRIPTOR_HANDLE uav[43] = {};
            bool srv_dirty                       = false;
            bool uav_dirty                       = false;
            bool is_compute_bound                = false;
            bool is_bindless_pipeline            = false;
            // lazily created null descriptors on first use
            D3D12_CPU_DESCRIPTOR_HANDLE null_srv_tex2d = {};
            D3D12_CPU_DESCRIPTOR_HANDLE null_uav_tex2d = {};
            // swapchain tracking for submit-time transition to present
            ID3D12Resource* swapchain_bb_transitioned = nullptr;
        };

        unordered_map<const RHI_CommandList*, PendingBindings> bindings;
        mutex bindings_mutex;

        PendingBindings& get(const RHI_CommandList* cmd)
        {
            lock_guard<mutex> lock(bindings_mutex);
            return bindings[cmd];
        }

        void reset(const RHI_CommandList* cmd)
        {
            auto& b = get(cmd);
            for (auto& h : b.srv) h.ptr = 0;
            for (auto& h : b.uav) h.ptr = 0;
            b.srv_dirty               = false;
            b.uav_dirty               = false;
            b.is_compute_bound        = false;
            b.is_bindless_pipeline    = false;
            b.swapchain_bb_transitioned = nullptr;
        }

        D3D12_CPU_DESCRIPTOR_HANDLE ensure_null_srv(PendingBindings& b)
        {
            if (b.null_srv_tex2d.ptr == 0)
            {
                uint32_t idx = d3d12_descriptors::AllocateCbvSrvUavCpu();
                b.null_srv_tex2d = d3d12_descriptors::GetCbvSrvUavCpuHandle(idx);

                D3D12_SHADER_RESOURCE_VIEW_DESC desc = {};
                desc.Format                   = DXGI_FORMAT_R8G8B8A8_UNORM;
                desc.ViewDimension            = D3D12_SRV_DIMENSION_TEXTURE2D;
                desc.Shader4ComponentMapping  = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
                desc.Texture2D.MipLevels      = 1;
                RHI_Context::device->CreateShaderResourceView(nullptr, &desc, b.null_srv_tex2d);
            }
            return b.null_srv_tex2d;
        }

        D3D12_CPU_DESCRIPTOR_HANDLE ensure_null_uav(PendingBindings& b)
        {
            if (b.null_uav_tex2d.ptr == 0)
            {
                uint32_t idx = d3d12_descriptors::AllocateCbvSrvUavCpu();
                b.null_uav_tex2d = d3d12_descriptors::GetCbvSrvUavCpuHandle(idx);

                D3D12_UNORDERED_ACCESS_VIEW_DESC desc = {};
                desc.Format         = DXGI_FORMAT_R8G8B8A8_UNORM;
                desc.ViewDimension  = D3D12_UAV_DIMENSION_TEXTURE2D;
                RHI_Context::device->CreateUnorderedAccessView(nullptr, nullptr, &desc, b.null_uav_tex2d);
            }
            return b.null_uav_tex2d;
        }
    }

    static bool pso_is_imgui(const RHI_PipelineState& pso) { return pso.name != nullptr && strcmp(pso.name, "imgui") == 0; }

    // bind all bindless descriptor tables at the fixed bindless zones
    static void bind_bindless_tables(ID3D12GraphicsCommandList* cmd_list, bool is_compute)
    {
        const uint32_t tex_base     = d3d12_descriptors::GetBindlessTexturesBase();
        const uint32_t buf_base     = d3d12_descriptors::GetBindlessBuffersBase();
        const uint32_t compare_base = d3d12_descriptors::GetSamplersCompareBase();

        D3D12_GPU_DESCRIPTOR_HANDLE h_mat_tex   = d3d12_descriptors::GetCbvSrvUavGpuHandle(tex_base);
        D3D12_GPU_DESCRIPTOR_HANDLE h_mat_param = d3d12_descriptors::GetCbvSrvUavGpuHandle(buf_base + 0);
        D3D12_GPU_DESCRIPTOR_HANDLE h_light     = d3d12_descriptors::GetCbvSrvUavGpuHandle(buf_base + 1);
        D3D12_GPU_DESCRIPTOR_HANDLE h_aabb      = d3d12_descriptors::GetCbvSrvUavGpuHandle(buf_base + 2);
        D3D12_GPU_DESCRIPTOR_HANDLE h_draw_data = d3d12_descriptors::GetCbvSrvUavGpuHandle(buf_base + 3);
        D3D12_GPU_DESCRIPTOR_HANDLE h_geo       = d3d12_descriptors::GetCbvSrvUavGpuHandle(buf_base + 4);
        D3D12_GPU_DESCRIPTOR_HANDLE h_samplers  = d3d12_descriptors::GetSamplerGpuHandle(compare_base);

        if (is_compute)
        {
            cmd_list->SetComputeRootDescriptorTable(d3d12_root_slot::srv_material_tex,   h_mat_tex);
            cmd_list->SetComputeRootDescriptorTable(d3d12_root_slot::srv_material_param, h_mat_param);
            cmd_list->SetComputeRootDescriptorTable(d3d12_root_slot::srv_light_param,    h_light);
            cmd_list->SetComputeRootDescriptorTable(d3d12_root_slot::srv_aabb,           h_aabb);
            cmd_list->SetComputeRootDescriptorTable(d3d12_root_slot::srv_draw_data,      h_draw_data);
            cmd_list->SetComputeRootDescriptorTable(d3d12_root_slot::srv_geometry,       h_geo);
            cmd_list->SetComputeRootDescriptorTable(d3d12_root_slot::sampler_table,      h_samplers);
        }
        else
        {
            cmd_list->SetGraphicsRootDescriptorTable(d3d12_root_slot::srv_material_tex,   h_mat_tex);
            cmd_list->SetGraphicsRootDescriptorTable(d3d12_root_slot::srv_material_param, h_mat_param);
            cmd_list->SetGraphicsRootDescriptorTable(d3d12_root_slot::srv_light_param,    h_light);
            cmd_list->SetGraphicsRootDescriptorTable(d3d12_root_slot::srv_aabb,           h_aabb);
            cmd_list->SetGraphicsRootDescriptorTable(d3d12_root_slot::srv_draw_data,      h_draw_data);
            cmd_list->SetGraphicsRootDescriptorTable(d3d12_root_slot::srv_geometry,       h_geo);
            cmd_list->SetGraphicsRootDescriptorTable(d3d12_root_slot::sampler_table,      h_samplers);
        }
    }

    // flush pending srv/uav ring-allocated tables before a draw/dispatch
    static void flush_pending_bindings(ID3D12GraphicsCommandList* cmd_list, const RHI_CommandList* cmd, bool is_compute)
    {
        auto& b = cmd_state::get(cmd);
        if (!b.is_bindless_pipeline)
            return;

        if (b.srv_dirty)
        {
            uint32_t base = d3d12_descriptors::AllocateRing(27);
            for (uint32_t i = 0; i < 27; i++)
            {
                D3D12_CPU_DESCRIPTOR_HANDLE dst = d3d12_descriptors::GetCbvSrvUavGpuVisibleCpuHandle(base + i);
                D3D12_CPU_DESCRIPTOR_HANDLE src = (b.srv[i].ptr != 0) ? b.srv[i] : cmd_state::ensure_null_srv(b);
                RHI_Context::device->CopyDescriptorsSimple(1, dst, src, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
            }
            D3D12_GPU_DESCRIPTOR_HANDLE gpu = d3d12_descriptors::GetCbvSrvUavGpuHandle(base);
            if (is_compute) cmd_list->SetComputeRootDescriptorTable(d3d12_root_slot::srv_table_space0, gpu);
            else            cmd_list->SetGraphicsRootDescriptorTable(d3d12_root_slot::srv_table_space0, gpu);
            b.srv_dirty = false;
        }

        if (b.uav_dirty)
        {
            uint32_t base = d3d12_descriptors::AllocateRing(43);
            for (uint32_t i = 0; i < 43; i++)
            {
                D3D12_CPU_DESCRIPTOR_HANDLE dst = d3d12_descriptors::GetCbvSrvUavGpuVisibleCpuHandle(base + i);
                D3D12_CPU_DESCRIPTOR_HANDLE src = (b.uav[i].ptr != 0) ? b.uav[i] : cmd_state::ensure_null_uav(b);
                RHI_Context::device->CopyDescriptorsSimple(1, dst, src, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
            }
            D3D12_GPU_DESCRIPTOR_HANDLE gpu = d3d12_descriptors::GetCbvSrvUavGpuHandle(base);
            if (is_compute) cmd_list->SetComputeRootDescriptorTable(d3d12_root_slot::uav_table_space0, gpu);
            else            cmd_list->SetGraphicsRootDescriptorTable(d3d12_root_slot::uav_table_space0, gpu);
            b.uav_dirty = false;
        }
    }

    RHI_CommandList::RHI_CommandList(RHI_Queue* queue, void* cmd_pool, const char* name)
    {
        SP_ASSERT(cmd_pool != nullptr);
        SP_ASSERT(queue != nullptr);

        m_rhi_cmd_pool_resource = cmd_pool;
        m_queue                 = queue;
        m_object_name           = name;

        D3D12_COMMAND_LIST_TYPE cmd_list_type = D3D12_COMMAND_LIST_TYPE_DIRECT;
        if (queue->GetType() == RHI_Queue_Type::Compute)
            cmd_list_type = D3D12_COMMAND_LIST_TYPE_COMPUTE;
        else if (queue->GetType() == RHI_Queue_Type::Copy)
            cmd_list_type = D3D12_COMMAND_LIST_TYPE_COPY;

        ID3D12GraphicsCommandList* cmd_list = nullptr;
        SP_ASSERT_MSG(d3d12_utility::error::check(RHI_Context::device->CreateCommandList(
            0, cmd_list_type, static_cast<ID3D12CommandAllocator*>(cmd_pool), nullptr, IID_PPV_ARGS(&cmd_list)
        )), "Failed to create command list");
        m_rhi_resource = cmd_list;

        cmd_list->Close();

        ID3D12Fence* fence = nullptr;
        SP_ASSERT_MSG(d3d12_utility::error::check(RHI_Context::device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence))),
            "Failed to create fence for command list");
        m_rhi_fence = fence;

        m_rhi_fence_value = 0;
        m_rhi_fence_event = CreateEvent(nullptr, FALSE, FALSE, nullptr);
        SP_ASSERT_MSG(m_rhi_fence_event != nullptr, "Failed to create fence event");

        m_state = RHI_CommandListState::Idle;
    }

    RHI_CommandList::~RHI_CommandList()
    {
        RHI_Device::QueueWaitAll();

        {
            lock_guard<mutex> lock(cmd_state::bindings_mutex);
            cmd_state::bindings.erase(this);
        }

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

        if (m_rhi_resource)
        {
            static_cast<ID3D12GraphicsCommandList*>(m_rhi_resource)->Release();
            m_rhi_resource = nullptr;
        }

        if (m_rhi_cmd_pool_resource)
        {
            static_cast<ID3D12CommandAllocator*>(m_rhi_cmd_pool_resource)->Release();
            m_rhi_cmd_pool_resource = nullptr;
        }
    }

    void RHI_CommandList::Begin()
    {
        if (m_state == RHI_CommandListState::Submitted)
        {
            WaitForExecution();
        }

        SP_ASSERT(m_rhi_resource != nullptr);
        SP_ASSERT(m_state == RHI_CommandListState::Idle);

        ID3D12CommandAllocator* allocator = static_cast<ID3D12CommandAllocator*>(m_rhi_cmd_pool_resource);
        SP_ASSERT_MSG(d3d12_utility::error::check(allocator->Reset()), "Failed to reset command allocator");

        ID3D12GraphicsCommandList* cmd_list = static_cast<ID3D12GraphicsCommandList*>(m_rhi_resource);
        SP_ASSERT_MSG(d3d12_utility::error::check(cmd_list->Reset(allocator, nullptr)), "Failed to reset command list");

        // bind shader-visible heaps once per frame per cmd list
        ID3D12DescriptorHeap* heaps[2] = {
            d3d12_descriptors::GetCbvSrvUavHeap(),
            d3d12_descriptors::GetSamplerHeap()
        };
        if (heaps[0] && heaps[1])
            cmd_list->SetDescriptorHeaps(2, heaps);
        else if (heaps[0])
            cmd_list->SetDescriptorHeaps(1, heaps);

        cmd_state::reset(this);

        m_state = RHI_CommandListState::Recording;
        m_buffer_id_vertex = 0;
        m_buffer_id_index  = 0;
    }

    void RHI_CommandList::Submit(RHI_SyncPrimitive* semaphore_wait, const bool is_immediate, RHI_SyncPrimitive* semaphore_signal,
                                RHI_SyncPrimitive* semaphore_timeline_wait, uint64_t timeline_wait_value)
    {
        SP_ASSERT(m_rhi_resource != nullptr);
        SP_ASSERT(m_state == RHI_CommandListState::Recording);

        RenderPassEnd();

        ID3D12GraphicsCommandList* cmd_list = static_cast<ID3D12GraphicsCommandList*>(m_rhi_resource);

        // note: swapchain backbuffer render_target -> present transition is handled by Renderer::SubmitAndPresent

        SP_ASSERT_MSG(d3d12_utility::error::check(cmd_list->Close()), "Failed to close command list");

        ID3D12CommandQueue* queue = static_cast<ID3D12CommandQueue*>(RHI_Device::GetQueueRhiResource(m_queue->GetType()));
        ID3D12CommandList* cmd_lists[] = { cmd_list };
        queue->ExecuteCommandLists(1, cmd_lists);

        m_rhi_fence_value++;
        queue->Signal(static_cast<ID3D12Fence*>(m_rhi_fence), m_rhi_fence_value);

        m_state = RHI_CommandListState::Submitted;

        if (is_immediate)
        {
            WaitForExecution();
        }
    }

    void RHI_CommandList::WaitForExecution(const bool log_wait_time)
    {
        if (m_state != RHI_CommandListState::Submitted)
            return;

        ID3D12Fence* fence = static_cast<ID3D12Fence*>(m_rhi_fence);
        HANDLE fence_event = static_cast<HANDLE>(m_rhi_fence_event);

        if (fence->GetCompletedValue() < m_rhi_fence_value)
        {
            fence->SetEventOnCompletion(m_rhi_fence_value, fence_event);
            WaitForSingleObject(fence_event, INFINITE);
        }

        m_state = RHI_CommandListState::Idle;
    }

    void RHI_CommandList::SetPipelineState(RHI_PipelineState& pso)
    {
        pso.Prepare();
        SP_ASSERT(m_state == RHI_CommandListState::Recording);

        RHI_Pipeline* pipeline = nullptr;
        RHI_DescriptorSetLayout* descriptor_set_layout = nullptr;
        RHI_Device::GetOrCreatePipeline(pso, pipeline, descriptor_set_layout);

        const bool is_compute = pso.IsCompute();
        const bool is_imgui   = pso_is_imgui(pso);

        if (pipeline && pipeline->GetRhiResource())
        {
            ID3D12GraphicsCommandList* cmd_list = static_cast<ID3D12GraphicsCommandList*>(m_rhi_resource);

            cmd_list->SetPipelineState(static_cast<ID3D12PipelineState*>(pipeline->GetRhiResource()));

            if (pipeline->GetRhiResourceLayout())
            {
                ID3D12RootSignature* rs = static_cast<ID3D12RootSignature*>(pipeline->GetRhiResourceLayout());
                if (is_compute) cmd_list->SetComputeRootSignature(rs);
                else            cmd_list->SetGraphicsRootSignature(rs);
            }

            if (!is_compute)
            {
                D3D12_PRIMITIVE_TOPOLOGY topo = (pso.primitive_toplogy == RHI_PrimitiveTopology::LineList) ? D3D_PRIMITIVE_TOPOLOGY_LINELIST : D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
                cmd_list->IASetPrimitiveTopology(topo);
            }

            auto& b = cmd_state::get(this);
            b.is_compute_bound     = is_compute;
            b.is_bindless_pipeline = !is_imgui;

            // bind all fixed bindless tables (they point at fixed zones; contents are updated by Device::UpdateBindless*)
            if (!is_imgui)
            {
                bind_bindless_tables(cmd_list, is_compute);
                // mark srv/uav tables dirty so they get flushed before the next draw/dispatch
                b.srv_dirty = true;
                b.uav_dirty = true;
            }
        }

        m_pso      = pso;
        m_pipeline = pipeline;

        if (!is_compute)
        {
            RenderPassBegin();
        }
    }

    void RHI_CommandList::RenderPassBegin()
    {
        if (m_render_pass_active)
            return;

        SP_ASSERT(m_state == RHI_CommandListState::Recording);
        ID3D12GraphicsCommandList* cmd_list = static_cast<ID3D12GraphicsCommandList*>(m_rhi_resource);

        D3D12_CPU_DESCRIPTOR_HANDLE rtv_handles[rhi_max_render_target_count] = {};
        uint32_t rtv_count = 0;

        uint32_t width  = 0;
        uint32_t height = 0;

        // swapchain target
        if (m_pso.render_target_swapchain)
        {
            RHI_SwapChain* swapchain = m_pso.render_target_swapchain;
            rtv_handles[0]            = get_swapchain_rtv_handle(swapchain);
            rtv_count                 = 1;
            width                     = swapchain->GetWidth();
            height                    = swapchain->GetHeight();

            ID3D12Resource* backbuffer = static_cast<ID3D12Resource*>(swapchain->GetRhiRt());
            auto& bb = cmd_state::get(this);
            if (backbuffer && bb.swapchain_bb_transitioned != backbuffer)
            {
                D3D12_RESOURCE_BARRIER barrier = {};
                barrier.Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
                barrier.Transition.pResource   = backbuffer;
                barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
                barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
                barrier.Transition.StateAfter  = D3D12_RESOURCE_STATE_RENDER_TARGET;
                cmd_list->ResourceBarrier(1, &barrier);
                bb.swapchain_bb_transitioned = backbuffer;
            }
        }
        else
        {
            // color textures
            for (uint32_t i = 0; i < rhi_max_render_target_count; i++)
            {
                RHI_Texture* rt = m_pso.render_target_color_textures[i];
                if (!rt || !rt->GetRhiRtv(0))
                    continue;

                rtv_handles[i].ptr = reinterpret_cast<SIZE_T>(rt->GetRhiRtv(0));
                rtv_count          = i + 1;
                width              = rt->GetWidth();
                height             = rt->GetHeight();
            }
        }

        // depth
        D3D12_CPU_DESCRIPTOR_HANDLE dsv_handle = {};
        D3D12_CPU_DESCRIPTOR_HANDLE* dsv_ptr   = nullptr;
        if (m_pso.render_target_depth_texture && m_pso.render_target_depth_texture->GetRhiDsv(0))
        {
            dsv_handle.ptr = reinterpret_cast<SIZE_T>(m_pso.render_target_depth_texture->GetRhiDsv(0));
            dsv_ptr        = &dsv_handle;
            if (width == 0)
            {
                width  = m_pso.render_target_depth_texture->GetWidth();
                height = m_pso.render_target_depth_texture->GetHeight();
            }
        }

        cmd_list->OMSetRenderTargets(rtv_count, rtv_count > 0 ? rtv_handles : nullptr, FALSE, dsv_ptr);

        // clear rtvs
        for (uint32_t i = 0; i < rtv_count; i++)
        {
            if (i < rhi_max_render_target_count && m_pso.clear_color[i] != rhi_color_dont_care && m_pso.clear_color[i] != rhi_color_load)
            {
                float c[4] = { m_pso.clear_color[i].r, m_pso.clear_color[i].g, m_pso.clear_color[i].b, m_pso.clear_color[i].a };
                cmd_list->ClearRenderTargetView(rtv_handles[i], c, 0, nullptr);
            }
        }

        // clear dsv
        if (dsv_ptr)
        {
            D3D12_CLEAR_FLAGS flags = static_cast<D3D12_CLEAR_FLAGS>(0);
            if (m_pso.clear_depth != rhi_depth_load && m_pso.clear_depth != rhi_depth_dont_care) flags |= D3D12_CLEAR_FLAG_DEPTH;
            if (m_pso.clear_stencil != rhi_stencil_load && m_pso.clear_stencil != rhi_stencil_dont_care) flags |= D3D12_CLEAR_FLAG_STENCIL;
            if (flags != 0)
            {
                float depth     = (m_pso.clear_depth != rhi_depth_load && m_pso.clear_depth != rhi_depth_dont_care) ? m_pso.clear_depth : 1.0f;
                uint8_t stencil = static_cast<uint8_t>((m_pso.clear_stencil != rhi_stencil_load && m_pso.clear_stencil != rhi_stencil_dont_care) ? m_pso.clear_stencil : 0);
                cmd_list->ClearDepthStencilView(dsv_handle, flags, depth, stencil, 0, nullptr);
            }
        }

        if (width > 0 && height > 0)
        {
            D3D12_VIEWPORT viewport = {};
            viewport.Width    = static_cast<float>(width);
            viewport.Height   = static_cast<float>(height);
            viewport.MinDepth = 0.0f;
            viewport.MaxDepth = 1.0f;
            cmd_list->RSSetViewports(1, &viewport);

            D3D12_RECT scissor = {};
            scissor.right  = static_cast<LONG>(width);
            scissor.bottom = static_cast<LONG>(height);
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
        // handled in RenderPassBegin
    }

    void RHI_CommandList::ClearTexture(RHI_Texture* texture, const Color& clear_color, const float clear_depth, const uint32_t clear_stencil)
    {
        if (!texture)
            return;

        ID3D12GraphicsCommandList* cmd_list = static_cast<ID3D12GraphicsCommandList*>(m_rhi_resource);

        if (texture->IsDepthStencilFormat() && texture->GetRhiDsv(0))
        {
            D3D12_CPU_DESCRIPTOR_HANDLE dsv = {};
            dsv.ptr = reinterpret_cast<SIZE_T>(texture->GetRhiDsv(0));
            D3D12_CLEAR_FLAGS flags = D3D12_CLEAR_FLAG_DEPTH;
            if (texture->IsStencilFormat()) flags |= D3D12_CLEAR_FLAG_STENCIL;
            cmd_list->ClearDepthStencilView(dsv, flags,
                (clear_depth == rhi_depth_load || clear_depth == rhi_depth_dont_care) ? 1.0f : clear_depth,
                static_cast<UINT8>((clear_stencil == rhi_stencil_load || clear_stencil == rhi_stencil_dont_care) ? 0 : clear_stencil),
                0, nullptr);
        }
        else if (texture->GetRhiRtv(0))
        {
            D3D12_CPU_DESCRIPTOR_HANDLE rtv = {};
            rtv.ptr = reinterpret_cast<SIZE_T>(texture->GetRhiRtv(0));
            if (clear_color != rhi_color_load && clear_color != rhi_color_dont_care)
            {
                float c[4] = { clear_color.r, clear_color.g, clear_color.b, clear_color.a };
                cmd_list->ClearRenderTargetView(rtv, c, 0, nullptr);
            }
        }
    }

    void RHI_CommandList::Draw(const uint32_t vertex_count, uint32_t vertex_start_index)
    {
        SP_ASSERT(m_state == RHI_CommandListState::Recording);
        ID3D12GraphicsCommandList* cmd_list = static_cast<ID3D12GraphicsCommandList*>(m_rhi_resource);
        flush_pending_bindings(cmd_list, this, false);
        cmd_list->DrawInstanced(vertex_count, 1, vertex_start_index, 0);
        Profiler::m_rhi_draw++;
    }

    void RHI_CommandList::DrawIndexed(const uint32_t index_count, const uint32_t index_offset, const uint32_t vertex_offset, const uint32_t instance_start_index, const uint32_t instance_count)
    {
        SP_ASSERT(m_state == RHI_CommandListState::Recording);
        ID3D12GraphicsCommandList* cmd_list = static_cast<ID3D12GraphicsCommandList*>(m_rhi_resource);
        flush_pending_bindings(cmd_list, this, false);
        cmd_list->DrawIndexedInstanced(index_count, instance_count, index_offset, vertex_offset, instance_start_index);
        Profiler::m_rhi_draw++;
    }

    void RHI_CommandList::DrawIndexedIndirectCount(RHI_Buffer* args_buffer, const uint32_t args_offset, RHI_Buffer* count_buffer, const uint32_t count_offset, const uint32_t max_draw_count)
    {
        SP_ASSERT(m_state == RHI_CommandListState::Recording);
        if (!args_buffer || !count_buffer)
            return;

        static ID3D12CommandSignature* command_signature = nullptr;
        if (!command_signature)
        {
            D3D12_INDIRECT_ARGUMENT_DESC arg_desc = {};
            arg_desc.Type                         = D3D12_INDIRECT_ARGUMENT_TYPE_DRAW_INDEXED;

            D3D12_COMMAND_SIGNATURE_DESC desc = {};
            desc.ByteStride        = sizeof(D3D12_DRAW_INDEXED_ARGUMENTS);
            desc.NumArgumentDescs  = 1;
            desc.pArgumentDescs    = &arg_desc;
            RHI_Context::device->CreateCommandSignature(&desc, nullptr, IID_PPV_ARGS(&command_signature));
        }

        ID3D12GraphicsCommandList* cmd_list = static_cast<ID3D12GraphicsCommandList*>(m_rhi_resource);
        flush_pending_bindings(cmd_list, this, false);
        cmd_list->ExecuteIndirect(
            command_signature, max_draw_count,
            static_cast<ID3D12Resource*>(args_buffer->GetRhiResource()),  static_cast<UINT64>(args_offset),
            static_cast<ID3D12Resource*>(count_buffer->GetRhiResource()), static_cast<UINT64>(count_offset)
        );
        Profiler::m_rhi_draw++;
    }

    void RHI_CommandList::Dispatch(uint32_t x, uint32_t y, uint32_t z)
    {
        SP_ASSERT(m_state == RHI_CommandListState::Recording);
        ID3D12GraphicsCommandList* cmd_list = static_cast<ID3D12GraphicsCommandList*>(m_rhi_resource);
        flush_pending_bindings(cmd_list, this, true);
        cmd_list->Dispatch(x, y, z);
    }

    void RHI_CommandList::DrawIndirect(RHI_Buffer* args_buffer, const uint32_t args_offset)
    {
        SP_ASSERT(m_state == RHI_CommandListState::Recording);
        if (!args_buffer)
            return;

        static ID3D12CommandSignature* command_signature = nullptr;
        if (!command_signature)
        {
            D3D12_INDIRECT_ARGUMENT_DESC arg_desc = {};
            arg_desc.Type                         = D3D12_INDIRECT_ARGUMENT_TYPE_DRAW;

            D3D12_COMMAND_SIGNATURE_DESC desc = {};
            desc.ByteStride        = sizeof(D3D12_DRAW_ARGUMENTS);
            desc.NumArgumentDescs  = 1;
            desc.pArgumentDescs    = &arg_desc;
            RHI_Context::device->CreateCommandSignature(&desc, nullptr, IID_PPV_ARGS(&command_signature));
        }

        ID3D12GraphicsCommandList* cmd_list = static_cast<ID3D12GraphicsCommandList*>(m_rhi_resource);
        flush_pending_bindings(cmd_list, this, false);
        cmd_list->ExecuteIndirect(
            command_signature, 1u,
            static_cast<ID3D12Resource*>(args_buffer->GetRhiResource()), static_cast<UINT64>(args_offset),
            nullptr, 0
        );
        Profiler::m_rhi_draw++;
    }

    void RHI_CommandList::DispatchIndirect(RHI_Buffer* args_buffer, const uint32_t args_offset)
    {
        SP_ASSERT(m_state == RHI_CommandListState::Recording);
        if (!args_buffer)
            return;

        static ID3D12CommandSignature* command_signature = nullptr;
        if (!command_signature)
        {
            D3D12_INDIRECT_ARGUMENT_DESC arg_desc = {};
            arg_desc.Type                         = D3D12_INDIRECT_ARGUMENT_TYPE_DISPATCH;

            D3D12_COMMAND_SIGNATURE_DESC desc = {};
            desc.ByteStride        = sizeof(D3D12_DISPATCH_ARGUMENTS);
            desc.NumArgumentDescs  = 1;
            desc.pArgumentDescs    = &arg_desc;
            RHI_Context::device->CreateCommandSignature(&desc, nullptr, IID_PPV_ARGS(&command_signature));
        }

        ID3D12GraphicsCommandList* cmd_list = static_cast<ID3D12GraphicsCommandList*>(m_rhi_resource);
        flush_pending_bindings(cmd_list, this, true);
        cmd_list->ExecuteIndirect(
            command_signature, 1u,
            static_cast<ID3D12Resource*>(args_buffer->GetRhiResource()), static_cast<UINT64>(args_offset),
            nullptr, 0
        );
    }

    void RHI_CommandList::TraceRays(const uint32_t width, const uint32_t height) { }

    void RHI_CommandList::SetAccelerationStructure(Renderer_BindingsSrv slot, RHI_AccelerationStructure* tlas) { }

    void RHI_CommandList::Blit(RHI_Texture* source, RHI_Texture* destination, const bool blit_mips, const float resolution_scale)
    {
        if (!source || !destination) return;
        ID3D12GraphicsCommandList* cmd_list = static_cast<ID3D12GraphicsCommandList*>(m_rhi_resource);
        ID3D12Resource* src = static_cast<ID3D12Resource*>(source->GetRhiResource());
        ID3D12Resource* dst = static_cast<ID3D12Resource*>(destination->GetRhiResource());
        if (!src || !dst) return;
        // naive copy (assumes same format/extent and correct states)
        cmd_list->CopyResource(dst, src);
    }

    void RHI_CommandList::Blit(RHI_Texture* source, RHI_SwapChain* destination)
    {
        SP_ASSERT_MSG((source->GetFlags() & RHI_Texture_ClearBlit) != 0, "The texture needs the RHI_Texture_ClearOrBlit flag");
        if (!source || !destination) return;

        ID3D12GraphicsCommandList* cmd_list = static_cast<ID3D12GraphicsCommandList*>(m_rhi_resource);
        ID3D12Resource* src = static_cast<ID3D12Resource*>(source->GetRhiResource());
        ID3D12Resource* dst = static_cast<ID3D12Resource*>(destination->GetRhiRt());
        if (!src || !dst) return;

        // transition backbuffer to copy_dest
        auto& bb = cmd_state::get(this);
        if (bb.swapchain_bb_transitioned != dst)
        {
            D3D12_RESOURCE_BARRIER to_copy = {};
            to_copy.Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            to_copy.Transition.pResource   = dst;
            to_copy.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
            to_copy.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
            to_copy.Transition.StateAfter  = D3D12_RESOURCE_STATE_COPY_DEST;
            cmd_list->ResourceBarrier(1, &to_copy);
        }
        else
        {
            D3D12_RESOURCE_BARRIER to_copy = {};
            to_copy.Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            to_copy.Transition.pResource   = dst;
            to_copy.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
            to_copy.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
            to_copy.Transition.StateAfter  = D3D12_RESOURCE_STATE_COPY_DEST;
            cmd_list->ResourceBarrier(1, &to_copy);
        }

        cmd_list->CopyResource(dst, src);

        // transition back to render_target for any subsequent rendering; submit will still handle present
        D3D12_RESOURCE_BARRIER to_rt = {};
        to_rt.Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        to_rt.Transition.pResource   = dst;
        to_rt.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        to_rt.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
        to_rt.Transition.StateAfter  = D3D12_RESOURCE_STATE_RENDER_TARGET;
        cmd_list->ResourceBarrier(1, &to_rt);
        bb.swapchain_bb_transitioned = dst;
    }

    void RHI_CommandList::BlitToArrayLayer(RHI_Texture* source, RHI_Texture* destination, uint32_t dst_layer) { }

    void RHI_CommandList::BlitToXrSwapchain(RHI_Texture* source) { }

    void RHI_CommandList::Copy(RHI_Texture* source, RHI_Texture* destination, const bool blit_mips)
    {
        Blit(source, destination, blit_mips, 1.0f);
    }

    void RHI_CommandList::Copy(RHI_Texture* source, RHI_SwapChain* destination)
    {
        SP_ASSERT_MSG((source->GetFlags() & RHI_Texture_ClearBlit) != 0, "The texture needs the RHI_Texture_ClearOrBlit flag");
    }

    void RHI_CommandList::SetViewport(const RHI_Viewport& viewport) const
    {
        SP_ASSERT(m_state == RHI_CommandListState::Recording);
        D3D12_VIEWPORT vp = {};
        vp.TopLeftX = viewport.x;
        vp.TopLeftY = viewport.y;
        vp.Width    = viewport.width;
        vp.Height   = viewport.height;
        vp.MinDepth = viewport.depth_min;
        vp.MaxDepth = viewport.depth_max;
        static_cast<ID3D12GraphicsCommandList*>(m_rhi_resource)->RSSetViewports(1, &vp);
    }

    void RHI_CommandList::SetScissorRectangle(const math::Rectangle& scissor_rectangle) const
    {
        SP_ASSERT(m_state == RHI_CommandListState::Recording);
        const D3D12_RECT r =
        {
            static_cast<LONG>(scissor_rectangle.x),
            static_cast<LONG>(scissor_rectangle.y),
            static_cast<LONG>(scissor_rectangle.x + scissor_rectangle.width),
            static_cast<LONG>(scissor_rectangle.y + scissor_rectangle.height)
        };
        static_cast<ID3D12GraphicsCommandList*>(m_rhi_resource)->RSSetScissorRects(1, &r);
    }

    void RHI_CommandList::SetCullMode(const RHI_CullMode cull_mode)
    {
        SP_ASSERT(m_state == RHI_CommandListState::Recording);
        // cull mode is baked into pipeline state in d3d12
    }

    void RHI_CommandList::SetBufferVertex(const RHI_Buffer* vertex, RHI_Buffer* instance)
    {
        SP_ASSERT(m_state == RHI_CommandListState::Recording);
        SP_ASSERT(vertex && vertex->GetRhiResource());

        D3D12_VERTEX_BUFFER_VIEW views[2] = {};
        views[0].BufferLocation = static_cast<ID3D12Resource*>(vertex->GetRhiResource())->GetGPUVirtualAddress();
        views[0].StrideInBytes  = static_cast<UINT>(vertex->GetStride());
        views[0].SizeInBytes    = static_cast<UINT>(vertex->GetStride() * vertex->GetElementCount());

        UINT num_views = 1;
        uint64_t new_buffer_id = vertex->GetObjectId();

        if (instance && instance->GetRhiResource())
        {
            views[1].BufferLocation = static_cast<ID3D12Resource*>(instance->GetRhiResource())->GetGPUVirtualAddress();
            views[1].StrideInBytes  = static_cast<UINT>(instance->GetStride());
            views[1].SizeInBytes    = static_cast<UINT>(instance->GetStride() * instance->GetElementCount());
            num_views               = 2;
            new_buffer_id           = (new_buffer_id << 16) | instance->GetObjectId();
        }

        if (m_buffer_id_vertex != new_buffer_id)
        {
            static_cast<ID3D12GraphicsCommandList*>(m_rhi_resource)->IASetVertexBuffers(0, num_views, views);
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

        D3D12_INDEX_BUFFER_VIEW view = {};
        view.BufferLocation = static_cast<ID3D12Resource*>(buffer->GetRhiResource())->GetGPUVirtualAddress();
        view.SizeInBytes    = static_cast<UINT>(buffer->GetStride() * buffer->GetElementCount());
        view.Format         = is_16_bit ? DXGI_FORMAT_R16_UINT : DXGI_FORMAT_R32_UINT;

        static_cast<ID3D12GraphicsCommandList*>(m_rhi_resource)->IASetIndexBuffer(&view);
        m_buffer_id_index = buffer->GetObjectId();
        Profiler::m_rhi_bindings_buffer_index++;
    }

    void RHI_CommandList::SetConstantBuffer(const uint32_t slot, RHI_Buffer* constant_buffer)
    {
        SP_ASSERT(m_state == RHI_CommandListState::Recording);
        if (!constant_buffer || !constant_buffer->GetRhiResource())
            return;

        auto& b = cmd_state::get(this);
        if (!b.is_bindless_pipeline)
            return;

        ID3D12GraphicsCommandList* cmd_list = static_cast<ID3D12GraphicsCommandList*>(m_rhi_resource);
        D3D12_GPU_VIRTUAL_ADDRESS addr = static_cast<ID3D12Resource*>(constant_buffer->GetRhiResource())->GetGPUVirtualAddress() + constant_buffer->GetOffset();

        if (b.is_compute_bound) cmd_list->SetComputeRootConstantBufferView(d3d12_root_slot::cbv_frame, addr);
        else                    cmd_list->SetGraphicsRootConstantBufferView(d3d12_root_slot::cbv_frame, addr);
    }

    void RHI_CommandList::PushConstants(const uint32_t offset, const uint32_t size, const void* data)
    {
        SP_ASSERT(m_state == RHI_CommandListState::Recording);
        SP_ASSERT(data != nullptr);

        ID3D12GraphicsCommandList* cmd_list = static_cast<ID3D12GraphicsCommandList*>(m_rhi_resource);
        const uint32_t num_32bit = size / 4;

        auto& b = cmd_state::get(this);
        if (b.is_bindless_pipeline)
        {
            if (b.is_compute_bound)
                cmd_list->SetComputeRoot32BitConstants(d3d12_root_slot::push_constants, num_32bit, data, offset / 4);
            else
                cmd_list->SetGraphicsRoot32BitConstants(d3d12_root_slot::push_constants, num_32bit, data, offset / 4);
        }
        else
        {
            // imgui root signature has constants at root param 0
            cmd_list->SetGraphicsRoot32BitConstants(0, num_32bit, data, offset / 4);
        }
    }

    void RHI_CommandList::SetBuffer(const uint32_t slot, RHI_Buffer* buffer)
    {
        SP_ASSERT(m_state == RHI_CommandListState::Recording);
        if (!buffer || !buffer->GetRhiResource())
            return;

        auto& b = cmd_state::get(this);
        if (!b.is_bindless_pipeline)
            return;

        // create a transient SRV for this buffer in the cpu staging heap, stash in srv/uav slot
        // assume storage buffer is exposed as an SRV at the matching srv slot; if it's a uav, user should use a uav slot enum
        // here we route into uav by slot range convention (0-29 srv, matches t0..t26 actually up to t26 = 26, plus a bit)
        // for simplicity treat first arg as uav slot (most SetBuffer calls are for uavs in the engine)
        if (slot >= 43) return;

        D3D12_UNORDERED_ACCESS_VIEW_DESC desc = {};
        desc.Format                     = DXGI_FORMAT_UNKNOWN;
        desc.ViewDimension              = D3D12_UAV_DIMENSION_BUFFER;
        desc.Buffer.NumElements         = buffer->GetElementCount();
        desc.Buffer.StructureByteStride = buffer->GetStride();

        uint32_t idx = d3d12_descriptors::AllocateCbvSrvUavCpu();
        D3D12_CPU_DESCRIPTOR_HANDLE h = d3d12_descriptors::GetCbvSrvUavCpuHandle(idx);
        RHI_Context::device->CreateUnorderedAccessView(static_cast<ID3D12Resource*>(buffer->GetRhiResource()), nullptr, &desc, h);

        b.uav[slot]   = h;
        b.uav_dirty   = true;
    }

    void RHI_CommandList::SetTexture(const uint32_t slot, RHI_Texture* texture, const uint32_t mip_index, uint32_t mip_range, const bool uav, const uint32_t array_layer)
    {
        SP_ASSERT(m_state == RHI_CommandListState::Recording);

        auto& b = cmd_state::get(this);

        // imgui path: pso uses the imgui root signature, bind a single table at root param 1
        if (!b.is_bindless_pipeline)
        {
            if (!texture || !texture->GetRhiSrv()) return;

            ID3D12GraphicsCommandList* cmd_list = static_cast<ID3D12GraphicsCommandList*>(m_rhi_resource);

            // copy the cpu srv into the ring so the table is shader-visible
            uint32_t ring_idx = d3d12_descriptors::AllocateRing(1);
            D3D12_CPU_DESCRIPTOR_HANDLE dst = d3d12_descriptors::GetCbvSrvUavGpuVisibleCpuHandle(ring_idx);
            D3D12_CPU_DESCRIPTOR_HANDLE src = {};
            src.ptr = reinterpret_cast<SIZE_T>(texture->GetRhiSrv());
            RHI_Context::device->CopyDescriptorsSimple(1, dst, src, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

            D3D12_GPU_DESCRIPTOR_HANDLE gpu = d3d12_descriptors::GetCbvSrvUavGpuHandle(ring_idx);
            cmd_list->SetGraphicsRootDescriptorTable(1, gpu);
            return;
        }

        // bindless path
        if (!texture)
        {
            if (uav) { if (slot < 43) { b.uav[slot].ptr = 0; b.uav_dirty = true; } }
            else     { if (slot < 27) { b.srv[slot].ptr = 0; b.srv_dirty = true; } }
            return;
        }

        if (uav)
        {
            if (slot >= 43) return;
            void* uav_ptr = texture->GetRhiSrvMip(0);
            if (!uav_ptr) return;
            D3D12_CPU_DESCRIPTOR_HANDLE h = {};
            h.ptr = reinterpret_cast<SIZE_T>(uav_ptr);
            b.uav[slot] = h;
            b.uav_dirty = true;
        }
        else
        {
            if (slot >= 27) return;
            void* srv_ptr = texture->GetRhiSrv();
            if (!srv_ptr) return;
            D3D12_CPU_DESCRIPTOR_HANDLE h = {};
            h.ptr = reinterpret_cast<SIZE_T>(srv_ptr);
            b.srv[slot] = h;
            b.srv_dirty = true;
        }
    }

    uint32_t RHI_CommandList::BeginTimestamp() { return 0; }
    uint32_t RHI_CommandList::EndTimestamp()   { return 0; }
    float RHI_CommandList::GetTimestampResult(const uint32_t timestamp_index) { return 0.0f; }
    float RHI_CommandList::GetTimestampStartMs(const uint32_t timestamp_index) { return 0.0f; }
    void  RHI_CommandList::ReadbackTimestampsForProfiler() { }

    void RHI_CommandList::BeginOcclusionQuery(const uint64_t entity_id) { }
    void RHI_CommandList::EndOcclusionQuery() { }
    bool RHI_CommandList::GetOcclusionQueryResult(const uint64_t entity_id) { return false; }
    void RHI_CommandList::UpdateOcclusionQueries() { }

    void RHI_CommandList::BeginTimeblock(const char* name, const bool gpu_marker, const bool gpu_timing)
    {
        if (Debugging::IsBreadcrumbsEnabled())
        {
            Breadcrumbs::BeginMarker(name);
            RHI_Queue_Type queue_type = m_queue ? m_queue->GetType() : RHI_Queue_Type::Max;
            int32_t gpu_slot          = Breadcrumbs::GpuMarkerBegin(name, queue_type);
            if (gpu_slot >= 0)
            {
                m_breadcrumb_gpu_slots.push(gpu_slot);
                RHI_Buffer* buffer = Breadcrumbs::GetGpuBuffer(queue_type);
                if (buffer) WriteGpuBreadcrumb(buffer, static_cast<uint32_t>(gpu_slot), static_cast<uint32_t>(gpu_slot + 1));
            }
        }
    }

    void RHI_CommandList::EndTimeblock()
    {
        if (Debugging::IsBreadcrumbsEnabled())
        {
            Breadcrumbs::EndMarker();
            if (!m_breadcrumb_gpu_slots.empty())
            {
                int32_t gpu_slot = m_breadcrumb_gpu_slots.top();
                m_breadcrumb_gpu_slots.pop();
                RHI_Queue_Type queue_type = m_queue ? m_queue->GetType() : RHI_Queue_Type::Max;
                RHI_Buffer* buffer        = Breadcrumbs::GetGpuBuffer(queue_type);
                if (buffer && gpu_slot >= 0) WriteGpuBreadcrumb(buffer, static_cast<uint32_t>(gpu_slot), Breadcrumbs::gpu_marker_completed);
            }
        }
    }

    void RHI_CommandList::BeginMarker(const char* name)
    {
        if (Debugging::IsBreadcrumbsEnabled())
        {
            Breadcrumbs::BeginMarker(name);
            RHI_Queue_Type queue_type = m_queue ? m_queue->GetType() : RHI_Queue_Type::Max;
            int32_t gpu_slot          = Breadcrumbs::GpuMarkerBegin(name, queue_type);
            if (gpu_slot >= 0)
            {
                m_breadcrumb_gpu_slots.push(gpu_slot);
                RHI_Buffer* buffer = Breadcrumbs::GetGpuBuffer(queue_type);
                if (buffer) WriteGpuBreadcrumb(buffer, static_cast<uint32_t>(gpu_slot), static_cast<uint32_t>(gpu_slot + 1));
            }
        }
    }

    void RHI_CommandList::EndMarker()
    {
        if (Debugging::IsBreadcrumbsEnabled())
        {
            Breadcrumbs::EndMarker();
            if (!m_breadcrumb_gpu_slots.empty())
            {
                int32_t gpu_slot = m_breadcrumb_gpu_slots.top();
                m_breadcrumb_gpu_slots.pop();
                RHI_Queue_Type queue_type = m_queue ? m_queue->GetType() : RHI_Queue_Type::Max;
                RHI_Buffer* buffer        = Breadcrumbs::GetGpuBuffer(queue_type);
                if (buffer && gpu_slot >= 0) WriteGpuBreadcrumb(buffer, static_cast<uint32_t>(gpu_slot), Breadcrumbs::gpu_marker_completed);
            }
        }
    }

    void RHI_CommandList::WriteGpuBreadcrumb(RHI_Buffer* buffer, uint32_t slot, uint32_t value)
    {
        SP_ASSERT(buffer && buffer->GetRhiResource());
        SP_ASSERT(m_state == RHI_CommandListState::Recording);

        ID3D12GraphicsCommandList* cmd_list = static_cast<ID3D12GraphicsCommandList*>(m_rhi_resource);
        ID3D12Resource* resource            = static_cast<ID3D12Resource*>(buffer->GetRhiResource());

        ID3D12GraphicsCommandList2* cmd_list2 = nullptr;
        if (SUCCEEDED(cmd_list->QueryInterface(IID_PPV_ARGS(&cmd_list2))))
        {
            D3D12_WRITEBUFFERIMMEDIATE_PARAMETER param = {};
            param.Dest  = resource->GetGPUVirtualAddress() + slot * sizeof(uint32_t);
            param.Value = value;

            D3D12_WRITEBUFFERIMMEDIATE_MODE mode = D3D12_WRITEBUFFERIMMEDIATE_MODE_DEFAULT;
            cmd_list2->WriteBufferImmediate(1, &param, &mode);
            cmd_list2->Release();
        }
    }

    void RHI_CommandList::UpdateBuffer(RHI_Buffer* buffer, const uint64_t offset, const uint64_t size, const void* data)
    {
        // d3d12 path: upload heap buffers are mapped; write directly
        if (buffer && buffer->GetMappedData() && data)
        {
            memcpy(static_cast<uint8_t*>(buffer->GetMappedData()) + offset, data, static_cast<size_t>(size));
        }
    }

    void RHI_CommandList::InsertBarrier(const RHI_Barrier& barrier)
    {
        SP_ASSERT(m_state == RHI_CommandListState::Recording);
        if (!barrier.texture) return;

        ID3D12GraphicsCommandList* cmd_list = static_cast<ID3D12GraphicsCommandList*>(m_rhi_resource);
        ID3D12Resource* resource            = static_cast<ID3D12Resource*>(barrier.texture->GetRhiResource());
        if (!resource) return;

        D3D12_RESOURCE_BARRIER d3d12_barrier = {};
        d3d12_barrier.Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        d3d12_barrier.Transition.pResource   = resource;
        d3d12_barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;

        auto to_d3d = [](RHI_Image_Layout l) -> D3D12_RESOURCE_STATES
        {
            switch (l)
            {
                case RHI_Image_Layout::General:              return D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
                case RHI_Image_Layout::Shader_Read:          return D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
                case RHI_Image_Layout::Attachment:           return D3D12_RESOURCE_STATE_RENDER_TARGET;
                case RHI_Image_Layout::Transfer_Source:      return D3D12_RESOURCE_STATE_COPY_SOURCE;
                case RHI_Image_Layout::Transfer_Destination: return D3D12_RESOURCE_STATE_COPY_DEST;
                case RHI_Image_Layout::Present_Source:       return D3D12_RESOURCE_STATE_PRESENT;
                default:                                     return D3D12_RESOURCE_STATE_COMMON;
            }
        };

        d3d12_barrier.Transition.StateAfter  = to_d3d(barrier.layout);
        d3d12_barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COMMON;

        // cheap state tracking: if caller asks to transition to the same effective state, skip
        if (d3d12_barrier.Transition.StateAfter == d3d12_barrier.Transition.StateBefore)
            return;

        cmd_list->ResourceBarrier(1, &d3d12_barrier);
    }

    void RHI_CommandList::FlushBarriers() { }

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

    RHI_Image_Layout RHI_CommandList::GetImageLayout(void* image, const uint32_t mip_index) { return RHI_Image_Layout::Max; }

    void RHI_CommandList::CopyTextureToBuffer(RHI_Texture* source, RHI_Buffer* destination) { }
    void RHI_CommandList::CopyBufferToBuffer(void* source, RHI_Buffer* destination, uint64_t size) { }
    void RHI_CommandList::CopyBufferToBuffer(RHI_Buffer* source, RHI_Buffer* destination, uint64_t size)
    {
        if (!source || !destination) return;
        ID3D12GraphicsCommandList* cmd_list = static_cast<ID3D12GraphicsCommandList*>(m_rhi_resource);
        ID3D12Resource* src = static_cast<ID3D12Resource*>(source->GetRhiResource());
        ID3D12Resource* dst = static_cast<ID3D12Resource*>(destination->GetRhiResource());
        if (!src || !dst) return;
        cmd_list->CopyBufferRegion(dst, 0, src, 0, size);
    }

    namespace immediate_execution
    {
        static mutex mutex_execution;
        static condition_variable condition_var;
        static bool is_executing = false;
    }

    RHI_CommandList* RHI_CommandList::ImmediateExecutionBegin(const RHI_Queue_Type queue_type)
    {
        if (RHI_Device::IsDeviceLost())
            return nullptr;

        unique_lock<mutex> lock(immediate_execution::mutex_execution);
        immediate_execution::condition_var.wait(lock, [] { return !immediate_execution::is_executing; });
        immediate_execution::is_executing = true;

        RHI_Queue* queue = RHI_Device::GetQueue(queue_type);
        if (!queue)
        {
            immediate_execution::is_executing = false;
            immediate_execution::condition_var.notify_one();
            return nullptr;
        }

        RHI_CommandList* cmd_list = queue->NextCommandList();
        cmd_list->Begin();
        return cmd_list;
    }

    void RHI_CommandList::ImmediateExecutionEnd(RHI_CommandList* cmd_list)
    {
        if (cmd_list) cmd_list->Submit(nullptr, true);
        immediate_execution::is_executing = false;
        immediate_execution::condition_var.notify_one();
    }

    void RHI_CommandList::ImmediateExecutionShutdown() { }

    void* RHI_CommandList::GetRhiResourcePipeline()
    {
        return m_pipeline ? m_pipeline->GetRhiResource() : nullptr;
    }
}
