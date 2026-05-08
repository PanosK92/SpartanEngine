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
#include "../RHI_SyncPrimitive.h"
#include "../RHI_DescriptorSetLayout.h"
#include "../../Profiling/Profiler.h"
#include "../../Rendering/Renderer.h"
#include "../../Core/Debugging.h"
#include "../../Core/Breadcrumbs.h"
#include "D3D12_Internal.h"
#include "D3D12_BlitGraphics.h"
#include <cstring>
#include <unordered_map>
#include <vector>
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

namespace spartan::d3d12_state
{
    // process-wide resource state tracker, keyed on the raw d3d12 resource pointer
    // d3d12 has no implicit transitions outside of common-promotion rules, so we maintain it explicitly
    static std::unordered_map<ID3D12Resource*, D3D12_RESOURCE_STATES> resource_states;
    static std::mutex resource_states_mutex;

    void SetState(ID3D12Resource* resource, D3D12_RESOURCE_STATES state)
    {
        if (!resource) return;
        std::lock_guard<std::mutex> lock(resource_states_mutex);
        resource_states[resource] = state;
    }

    D3D12_RESOURCE_STATES GetState(ID3D12Resource* resource)
    {
        if (!resource) return D3D12_RESOURCE_STATE_COMMON;
        std::lock_guard<std::mutex> lock(resource_states_mutex);
        auto it = resource_states.find(resource);
        if (it == resource_states.end())
        {
            // unseeded resource - assume common, this matches the d3d12 promotion rules well enough for first-pixel
            return D3D12_RESOURCE_STATE_COMMON;
        }
        return it->second;
    }

    void RemoveState(ID3D12Resource* resource)
    {
        if (!resource) return;
        std::lock_guard<std::mutex> lock(resource_states_mutex);
        resource_states.erase(resource);
    }
}

namespace spartan
{
    // map an rhi image layout to a d3d12 resource state
    // depth attachments resolve to depth_write, regular attachments to render_target, etc.
    static D3D12_RESOURCE_STATES rhi_layout_to_d3d12_state(RHI_Image_Layout layout, bool is_depth)
    {
        switch (layout)
        {
            case RHI_Image_Layout::General:              return D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
            case RHI_Image_Layout::Shader_Read:          return is_depth ? (D3D12_RESOURCE_STATE_DEPTH_READ | D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE)
                                                                         : (D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
            case RHI_Image_Layout::Attachment:           return is_depth ? D3D12_RESOURCE_STATE_DEPTH_WRITE : D3D12_RESOURCE_STATE_RENDER_TARGET;
            case RHI_Image_Layout::Transfer_Source:      return D3D12_RESOURCE_STATE_COPY_SOURCE;
            case RHI_Image_Layout::Transfer_Destination: return D3D12_RESOURCE_STATE_COPY_DEST;
            case RHI_Image_Layout::Present_Source:       return D3D12_RESOURCE_STATE_PRESENT;
            case RHI_Image_Layout::Shading_Rate_Attachment: return D3D12_RESOURCE_STATE_SHADING_RATE_SOURCE;
            default:                                     return D3D12_RESOURCE_STATE_COMMON;
        }
    }

    // per-cmd-list pending bindings state (srv/uav slots that SetTexture/SetBuffer wrote to)
    namespace cmd_state
    {
        // per-subresource tracking, when per_subresource is empty the resource is uniform and uniform_state applies
        // to every subresource, ALL_SUBRESOURCES transitions reset back to uniform, specific-subresource transitions
        // expand into per_subresource on first use to record divergence (e.g. mip filtering passes that bind one
        // mip as UAV while reading lower mips as SRV)
        struct ResourceStateInfo
        {
            D3D12_RESOURCE_STATES uniform_state = D3D12_RESOURCE_STATE_COMMON;
            std::vector<D3D12_RESOURCE_STATES> per_subresource;
            bool initialized = false;
        };

        // states that aren't allowed on a compute command list barrier, transitions made on a compute queue must mask
        // these out since the runtime rejects pixel_shader_resource, render_target and depth states on the compute queue
        static constexpr D3D12_RESOURCE_STATES compute_invalid_states =
            D3D12_RESOURCE_STATE_RENDER_TARGET            |
            D3D12_RESOURCE_STATE_DEPTH_WRITE              |
            D3D12_RESOURCE_STATE_DEPTH_READ               |
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE    |
            D3D12_RESOURCE_STATE_STREAM_OUT               |
            D3D12_RESOURCE_STATE_RESOLVE_DEST             |
            D3D12_RESOURCE_STATE_RESOLVE_SOURCE;

        // graphics-only state bits that, when stripped at submit time, leave the resource readable by both graphics and
        // compute queues, render_target and depth_write are intentionally kept since their attachments are usually
        // re-used by the next graphics frame and stripping them only adds round-trip transitions
        static constexpr D3D12_RESOURCE_STATES submit_strip_states =
            D3D12_RESOURCE_STATE_DEPTH_READ               |
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;

        struct PendingBindings
        {
            // source cpu handles (from cpu staging heap) per slot - zero means null
            D3D12_CPU_DESCRIPTOR_HANDLE srv[27] = {};
            D3D12_CPU_DESCRIPTOR_HANDLE uav[45] = {};
            bool srv_dirty                       = false;
            bool uav_dirty                       = false;
            bool is_compute_bound                = false;
            bool is_bindless_pipeline            = false;
            // recording on a compute queue, set in Begin() so push_transition can drop graphics-only state bits
            bool is_compute_queue                = false;
            // d3d12 tracks graphics and compute root signatures separately, descriptor tables require the matching root sig
            bool has_root_signature_graphics     = false;
            bool has_root_signature_compute      = false;
            // true only after the imgui graphics pso has been bound, gates the imgui path in SetTexture
            bool is_imgui_pipeline_bound         = false;
            // lazily created null descriptors on first use
            D3D12_CPU_DESCRIPTOR_HANDLE null_srv_tex2d = {};
            D3D12_CPU_DESCRIPTOR_HANDLE null_uav_tex2d = {};
            // swapchain tracking for submit-time transition to present
            ID3D12Resource* swapchain_bb_transitioned = nullptr;
            // deferred barrier batch, flushed by FlushBarriers (or before draw/dispatch/render-pass-begin)
            std::vector<D3D12_RESOURCE_BARRIER> pending_barriers;
            // staging buffers acquired during recording, released on next Begin once gpu execution has finished
            std::vector<void*> staging_buffers_in_flight;
            // per-cmd-list resource state, populated lazily on first use of each resource by snapshotting
            // from the global tracker, this isolates concurrent cmd list recording from each other so
            // the StateBefore in barriers reflects what the gpu will actually be in at execution time
            std::unordered_map<ID3D12Resource*, ResourceStateInfo> resource_states;
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
            b.srv_dirty                  = false;
            b.uav_dirty                  = false;
            b.is_compute_bound           = false;
            b.is_bindless_pipeline       = false;
            b.has_root_signature_graphics = false;
            b.has_root_signature_compute  = false;
            b.is_imgui_pipeline_bound    = false;
            b.is_compute_queue           = false;
            b.swapchain_bb_transitioned  = nullptr;
            b.pending_barriers.clear();
            b.resource_states.clear();

            // safe to release staging buffers now, Begin guarantees the previous submission completed on the gpu
            for (void* sb : b.staging_buffers_in_flight)
            {
                RHI_Device::StagingBufferRelease(sb);
            }
            b.staging_buffers_in_flight.clear();
        }

        // total subresource count for a resource, mip count times array length, ignoring stencil planes
        // since spartan currently does not transition depth and stencil planes independently
        static uint32_t get_subresource_count(ID3D12Resource* resource)
        {
            D3D12_RESOURCE_DESC desc = resource->GetDesc();
            uint32_t array_size      = (desc.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE3D) ? 1u : static_cast<uint32_t>(desc.DepthOrArraySize);
            uint32_t mip_count       = static_cast<uint32_t>(desc.MipLevels);
            return mip_count * array_size;
        }

        // get or create the state info for a resource, lazy-initializing from the global tracker on first use
        static ResourceStateInfo& get_or_init_state(PendingBindings& b, ID3D12Resource* resource)
        {
            ResourceStateInfo& info = b.resource_states[resource];
            if (!info.initialized)
            {
                info.uniform_state = d3d12_state::GetState(resource);
                info.initialized   = true;
            }
            return info;
        }

        // publish all per-cmd-list resource state changes to the global tracker, called at submit time
        // resources left non-uniform get unifying barriers emitted before close so the global tracker stays uniform
        // and the actual gpu state matches it after execution
        void commit_states_to_global(ID3D12GraphicsCommandList* cmd_list, PendingBindings& b)
        {
            std::vector<D3D12_RESOURCE_BARRIER> unify;
            for (auto& kv : b.resource_states)
            {
                ResourceStateInfo& info = kv.second;
                if (!info.per_subresource.empty())
                {
                    // collapse to subresource 0's state, this is arbitrary but keeps the global tracker uniform
                    // and produces a known starting state for the next cmd list
                    D3D12_RESOURCE_STATES target = info.per_subresource[0];
                    for (uint32_t i = 1; i < info.per_subresource.size(); i++)
                    {
                        if (info.per_subresource[i] == target)
                            continue;

                        D3D12_RESOURCE_BARRIER barrier = {};
                        barrier.Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
                        barrier.Transition.pResource   = kv.first;
                        barrier.Transition.Subresource = i;
                        barrier.Transition.StateBefore = info.per_subresource[i];
                        barrier.Transition.StateAfter  = target;
                        unify.push_back(barrier);
                    }
                    info.per_subresource.clear();
                    info.uniform_state = target;
                }
                // strip graphics-only shader_read bits on graphics queue submissions, the next cmd list may run on a
                // compute queue which can not transition out of pixel_shader_resource or depth_read, leaving the resource
                // in non_pixel_shader_resource keeps it readable in compute and ps reads on the next graphics cmd list
                // re-add the stripped bits cheaply via the normal push_transition path
                if (!b.is_compute_queue && (info.uniform_state & submit_strip_states))
                {
                    D3D12_RESOURCE_STATES stripped = info.uniform_state & ~submit_strip_states;
                    // if stripping removed every read bit fall back to non_pixel_shader_resource so the resource still
                    // has a valid shader_read state on the compute queue
                    if (stripped == 0)
                    {
                        stripped = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
                    }
                    if (stripped != info.uniform_state)
                    {
                        D3D12_RESOURCE_BARRIER barrier = {};
                        barrier.Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
                        barrier.Transition.pResource   = kv.first;
                        barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
                        barrier.Transition.StateBefore = info.uniform_state;
                        barrier.Transition.StateAfter  = stripped;
                        unify.push_back(barrier);
                        info.uniform_state = stripped;
                    }
                }
                d3d12_state::SetState(kv.first, info.uniform_state);
            }
            if (!unify.empty())
            {
                cmd_list->ResourceBarrier(static_cast<UINT>(unify.size()), unify.data());
            }
        }

        // push a transition for resource into the pending list, updating the per-cmd-list state tracker
        // when subresource is ALL_SUBRESOURCES and the resource is currently divergent, this emits per-subresource
        // barriers for the subresources that differ from state_after, then collapses back to uniform
        // returns true if any barrier was actually queued, false if all subresources were already in state_after
        bool push_transition(PendingBindings& b, ID3D12Resource* resource, D3D12_RESOURCE_STATES state_after, UINT subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES)
        {
            if (!resource) return false;

            // on the compute queue strip graphics-only state bits from state_after, otherwise the runtime rejects the
            // barrier, the (pixel|non_pixel) shader_read combo collapses to non_pixel only which is the valid compute
            // equivalent, graphics-only states like render_target should not appear as targets in compute paths
            if (b.is_compute_queue)
            {
                state_after &= ~compute_invalid_states;
            }

            ResourceStateInfo& info = get_or_init_state(b, resource);

            if (subresource == D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES)
            {
                if (info.per_subresource.empty())
                {
                    if (info.uniform_state == state_after)
                        return false;

                    D3D12_RESOURCE_BARRIER barrier = {};
                    barrier.Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
                    barrier.Transition.pResource   = resource;
                    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
                    barrier.Transition.StateBefore = info.uniform_state;
                    barrier.Transition.StateAfter  = state_after;
                    b.pending_barriers.push_back(barrier);

                    info.uniform_state = state_after;
                    return true;
                }

                // currently divergent, emit per-subresource transitions for those that differ from the target
                bool any = false;
                for (uint32_t i = 0; i < info.per_subresource.size(); i++)
                {
                    if (info.per_subresource[i] == state_after)
                        continue;

                    D3D12_RESOURCE_BARRIER barrier = {};
                    barrier.Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
                    barrier.Transition.pResource   = resource;
                    barrier.Transition.Subresource = i;
                    barrier.Transition.StateBefore = info.per_subresource[i];
                    barrier.Transition.StateAfter  = state_after;
                    b.pending_barriers.push_back(barrier);
                    any = true;
                }

                info.per_subresource.clear();
                info.uniform_state = state_after;
                return any;
            }

            // specific subresource, expand to per-subresource if currently uniform
            if (info.per_subresource.empty())
            {
                if (info.uniform_state == state_after)
                    return false;

                uint32_t count = get_subresource_count(resource);
                if (count == 0)
                    count = subresource + 1;
                info.per_subresource.assign(count, info.uniform_state);
            }

            if (subresource >= info.per_subresource.size())
                return false;

            D3D12_RESOURCE_STATES state_before = info.per_subresource[subresource];
            if (state_before == state_after)
                return false;

            D3D12_RESOURCE_BARRIER barrier = {};
            barrier.Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            barrier.Transition.pResource   = resource;
            barrier.Transition.Subresource = subresource;
            barrier.Transition.StateBefore = state_before;
            barrier.Transition.StateAfter  = state_after;
            b.pending_barriers.push_back(barrier);

            info.per_subresource[subresource] = state_after;
            return true;
        }

        void push_uav_barrier(PendingBindings& b, ID3D12Resource* resource)
        {
            if (!resource) return;
            D3D12_RESOURCE_BARRIER barrier = {};
            barrier.Type          = D3D12_RESOURCE_BARRIER_TYPE_UAV;
            barrier.UAV.pResource = resource;
            b.pending_barriers.push_back(barrier);
        }

        void flush(ID3D12GraphicsCommandList* cmd_list, PendingBindings& b)
        {
            if (b.pending_barriers.empty())
                return;

            // dedup transitions, multiple push_transition calls on the same resource between flushes produce
            // a chain like A->B then B->C in pending_barriers, d3d12 accepts this but warns about it,
            // collapse them into a single A->C barrier per (resource, subresource) pair
            std::vector<D3D12_RESOURCE_BARRIER> deduped;
            deduped.reserve(b.pending_barriers.size());
            for (const auto& barrier : b.pending_barriers)
            {
                if (barrier.Type != D3D12_RESOURCE_BARRIER_TYPE_TRANSITION)
                {
                    deduped.push_back(barrier);
                    continue;
                }

                bool merged = false;
                for (auto& existing : deduped)
                {
                    if (existing.Type == D3D12_RESOURCE_BARRIER_TYPE_TRANSITION &&
                        existing.Transition.pResource   == barrier.Transition.pResource &&
                        existing.Transition.Subresource == barrier.Transition.Subresource)
                    {
                        // chain transitions, keep the original StateBefore and advance StateAfter
                        existing.Transition.StateAfter = barrier.Transition.StateAfter;
                        merged = true;
                        break;
                    }
                }
                if (!merged)
                    deduped.push_back(barrier);
            }

            // drop any resulting no-op transitions where the chained Before == After
            deduped.erase(std::remove_if(deduped.begin(), deduped.end(),
                [](const D3D12_RESOURCE_BARRIER& barrier)
                {
                    return barrier.Type == D3D12_RESOURCE_BARRIER_TYPE_TRANSITION &&
                           barrier.Transition.StateBefore == barrier.Transition.StateAfter;
                }),
                deduped.end());

            if (!deduped.empty())
            {
                cmd_list->ResourceBarrier(static_cast<UINT>(deduped.size()), deduped.data());
            }
            b.pending_barriers.clear();
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

    // flush pending srv/uav ring-allocated tables and pending resource barriers before a draw/dispatch
    static void flush_pending_bindings(ID3D12GraphicsCommandList* cmd_list, const RHI_CommandList* cmd, bool is_compute)
    {
        auto& b = cmd_state::get(cmd);

        // pending barriers always flush, regardless of bindless vs imgui pipelines
        cmd_state::flush(cmd_list, b);

        if (!b.is_bindless_pipeline)
            return;

        // skip table binds if the matching root signature was never bound, happens when a pipeline failed to compile
        if (is_compute && !b.has_root_signature_compute)  return;
        if (!is_compute && !b.has_root_signature_graphics) return;

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
            uint32_t base = d3d12_descriptors::AllocateRing(45);
            for (uint32_t i = 0; i < 45; i++)
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
            auto it = cmd_state::bindings.find(this);
            if (it != cmd_state::bindings.end())
            {
                for (void* sb : it->second.staging_buffers_in_flight)
                {
                    RHI_Device::StagingBufferRelease(sb);
                }
                cmd_state::bindings.erase(it);
            }
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
        // record the queue type so push_transition can mask compute-invalid state bits when recording on a compute queue
        cmd_state::get(this).is_compute_queue = (m_queue && m_queue->GetType() == RHI_Queue_Type::Compute);

        m_state              = RHI_CommandListState::Recording;
        m_buffer_id_vertex   = 0;
        m_buffer_id_index    = 0;
        m_render_pass_active = false;
        m_pso                = RHI_PipelineState();
    }

    void RHI_CommandList::Submit(RHI_SyncPrimitive* semaphore_wait, const bool is_immediate, RHI_SyncPrimitive* semaphore_signal,
                                RHI_SyncPrimitive* semaphore_timeline_wait, uint64_t timeline_wait_value)
    {
        SP_ASSERT(m_rhi_resource != nullptr);
        SP_ASSERT(m_state == RHI_CommandListState::Recording);

        RenderPassEnd();

        ID3D12GraphicsCommandList* cmd_list = static_cast<ID3D12GraphicsCommandList*>(m_rhi_resource);

        // flush any deferred barriers before closing, then publish the per-cmd-list state to the global
        // tracker so the next cmd list sees the post-execution state of resources touched by this one
        // commit emits unifying barriers for resources left in divergent per-subresource state
        {
            auto& b = cmd_state::get(this);
            cmd_state::flush(cmd_list, b);
            cmd_state::commit_states_to_global(cmd_list, b);
        }

        // note: swapchain backbuffer render_target -> present transition is handled by Renderer::SubmitAndPresent

        SP_ASSERT_MSG(d3d12_utility::error::check(cmd_list->Close()), "Failed to close command list");

        ID3D12CommandQueue* queue = static_cast<ID3D12CommandQueue*>(RHI_Device::GetQueueRhiResource(m_queue->GetType()));

        // gpu-side wait on a producer queue's signaled value before this cmd list executes
        if (semaphore_wait && semaphore_wait->GetRhiResource())
        {
            queue->Wait(static_cast<ID3D12Fence*>(semaphore_wait->GetRhiResource()), semaphore_wait->GetValue());
        }
        if (semaphore_timeline_wait && semaphore_timeline_wait->GetRhiResource() && timeline_wait_value > 0)
        {
            queue->Wait(static_cast<ID3D12Fence*>(semaphore_timeline_wait->GetRhiResource()), timeline_wait_value);
        }

        ID3D12CommandList* cmd_lists[] = { cmd_list };
        queue->ExecuteCommandLists(1, cmd_lists);

        m_rhi_fence_value++;
        queue->Signal(static_cast<ID3D12Fence*>(m_rhi_fence), m_rhi_fence_value);

        // signal the consumer's sync primitive so a downstream Submit can Wait on it
        if (semaphore_signal && semaphore_signal->GetRhiResource())
        {
            uint64_t next_value = semaphore_signal->GetNextSignalValue();
            queue->Signal(static_cast<ID3D12Fence*>(semaphore_signal->GetRhiResource()), next_value);
        }

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

        // early exit if the pipeline state hasn't changed
        if (m_pso.GetHash() == pso.GetHash())
            return;

        // determine load flags by comparing render targets with the previous pso
        // matching vertex shader and array index means this pso continues drawing into the same attachments,
        // so we must preserve their contents (load) rather than clearing
        if ((m_pso.shaders[RHI_Shader_Type::Vertex] != nullptr && m_pso.shaders[RHI_Shader_Type::Vertex] == pso.shaders[RHI_Shader_Type::Vertex]) && m_pso.render_target_array_index == pso.render_target_array_index)
        {
            m_load_depth_render_target = (pso.render_target_depth_texture == m_pso.render_target_depth_texture);
            for (uint32_t i = 0; i < rhi_max_render_target_count; i++)
            {
                m_load_color_render_targets[i] = (pso.render_target_color_textures[i] == m_pso.render_target_color_textures[i]);
            }
        }
        else
        {
            m_load_depth_render_target = false;
            for (uint32_t i = 0; i < rhi_max_render_target_count; i++)
            {
                m_load_color_render_targets[i] = false;
            }
        }

        RHI_Pipeline* pipeline = nullptr;
        RHI_DescriptorSetLayout* descriptor_set_layout = nullptr;
        RHI_Device::GetOrCreatePipeline(pso, pipeline, descriptor_set_layout);

        const bool is_compute = pso.IsCompute();
        const bool is_imgui   = pso_is_imgui(pso);

        // route flags must always reflect the requested pso, even if the pipeline failed to bind
        {
            auto& b = cmd_state::get(this);
            b.is_compute_bound        = is_compute;
            b.is_bindless_pipeline    = !is_imgui;
            b.is_imgui_pipeline_bound = false;
        }

        if (pipeline && pipeline->GetRhiResource())
        {
            ID3D12GraphicsCommandList* cmd_list = static_cast<ID3D12GraphicsCommandList*>(m_rhi_resource);

            cmd_list->SetPipelineState(static_cast<ID3D12PipelineState*>(pipeline->GetRhiResource()));

            auto& b = cmd_state::get(this);
            if (pipeline->GetRhiResourceLayout())
            {
                ID3D12RootSignature* rs = static_cast<ID3D12RootSignature*>(pipeline->GetRhiResourceLayout());
                if (is_compute)
                {
                    cmd_list->SetComputeRootSignature(rs);
                    b.has_root_signature_compute = true;
                }
                else
                {
                    cmd_list->SetGraphicsRootSignature(rs);
                    b.has_root_signature_graphics = true;
                }
            }

            if (!is_compute)
            {
                D3D12_PRIMITIVE_TOPOLOGY topo = (pso.primitive_toplogy == RHI_PrimitiveTopology::LineList) ? D3D_PRIMITIVE_TOPOLOGY_LINELIST : D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
                cmd_list->IASetPrimitiveTopology(topo);
            }

            b.is_imgui_pipeline_bound = is_imgui && !is_compute;

            // bind fixed bindless tables only when the matching root signature is live, otherwise the table sets fail
            const bool root_sig_ready = is_compute ? b.has_root_signature_compute : b.has_root_signature_graphics;
            if (!is_imgui && root_sig_ready)
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

        // bind the per-frame constant buffer and standard textures that every scene shader depends on
        // mirrors what Vulkan_CommandList does at the equivalent point in SetPipelineState
        if (!is_imgui && pipeline && pipeline->GetRhiResource())
        {
            Renderer::SetStandardResources(this);
        }
    }

    void RHI_CommandList::RenderPassBegin()
    {
        SP_ASSERT(m_state == RHI_CommandListState::Recording);

        // always end any previous pass first so the new pso's render targets actually get bound
        // d3d12 has no explicit begin/end render pass call, this just clears the active flag
        RenderPassEnd();

        if (!m_pso.IsGraphics())
            return;

        ID3D12GraphicsCommandList* cmd_list = static_cast<ID3D12GraphicsCommandList*>(m_rhi_resource);
        auto& b = cmd_state::get(this);

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
            if (backbuffer)
            {
                cmd_state::push_transition(b, backbuffer, D3D12_RESOURCE_STATE_RENDER_TARGET);
                b.swapchain_bb_transitioned = backbuffer;
            }
        }
        else
        {
            // off-screen color targets, all need to be in render_target before clears or draws
            for (uint32_t i = 0; i < rhi_max_render_target_count; i++)
            {
                RHI_Texture* rt = m_pso.render_target_color_textures[i];
                if (!rt || !rt->GetRhiRtv(0))
                    continue;

                ID3D12Resource* resource = static_cast<ID3D12Resource*>(rt->GetRhiResource());
                cmd_state::push_transition(b, resource, D3D12_RESOURCE_STATE_RENDER_TARGET);
                rt->SetLayoutDirect(0, rt->GetMipCount(), RHI_Image_Layout::Attachment);

                rtv_handles[i].ptr = reinterpret_cast<SIZE_T>(rt->GetRhiRtv(0));
                rtv_count          = i + 1;
                width              = rt->GetWidth();
                height             = rt->GetHeight();
            }
        }

        // depth target, transition to depth_write before clears or draws
        D3D12_CPU_DESCRIPTOR_HANDLE dsv_handle = {};
        D3D12_CPU_DESCRIPTOR_HANDLE* dsv_ptr   = nullptr;
        if (m_pso.render_target_depth_texture && m_pso.render_target_depth_texture->GetRhiDsv(0))
        {
            RHI_Texture* depth = m_pso.render_target_depth_texture;
            ID3D12Resource* depth_resource = static_cast<ID3D12Resource*>(depth->GetRhiResource());
            cmd_state::push_transition(b, depth_resource, D3D12_RESOURCE_STATE_DEPTH_WRITE);
            depth->SetLayoutDirect(0, depth->GetMipCount(), RHI_Image_Layout::Attachment);

            dsv_handle.ptr = reinterpret_cast<SIZE_T>(depth->GetRhiDsv(0));
            dsv_ptr        = &dsv_handle;
            if (width == 0)
            {
                width  = depth->GetWidth();
                height = depth->GetHeight();
            }
        }

        // flush all queued transitions before binding/clearing rtv/dsv
        cmd_state::flush(cmd_list, b);

        cmd_list->OMSetRenderTargets(rtv_count, rtv_count > 0 ? rtv_handles : nullptr, FALSE, dsv_ptr);

        // clear rtvs - skip when the render target is being loaded from the previous pso
        for (uint32_t i = 0; i < rtv_count; i++)
        {
            if (i < rhi_max_render_target_count && !m_load_color_render_targets[i] && m_pso.clear_color[i] != rhi_color_dont_care && m_pso.clear_color[i] != rhi_color_load)
            {
                float c[4] = { m_pso.clear_color[i].r, m_pso.clear_color[i].g, m_pso.clear_color[i].b, m_pso.clear_color[i].a };
                cmd_list->ClearRenderTargetView(rtv_handles[i], c, 0, nullptr);
            }
        }

        // clear dsv - skip when the depth target is being loaded from the previous pso
        if (dsv_ptr && !m_load_depth_render_target)
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
        auto& b = cmd_state::get(this);
        ID3D12Resource* resource = static_cast<ID3D12Resource*>(texture->GetRhiResource());

        if (texture->IsDepthStencilFormat() && texture->GetRhiDsv(0))
        {
            // ClearDepthStencilView requires the resource to be in depth_write state
            cmd_state::push_transition(b, resource, D3D12_RESOURCE_STATE_DEPTH_WRITE);
            texture->SetLayoutDirect(0, texture->GetMipCount(), RHI_Image_Layout::Attachment);
            cmd_state::flush(cmd_list, b);

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
            if (clear_color == rhi_color_load || clear_color == rhi_color_dont_care)
                return;

            // ClearRenderTargetView requires the resource to be in render_target state
            cmd_state::push_transition(b, resource, D3D12_RESOURCE_STATE_RENDER_TARGET);
            texture->SetLayoutDirect(0, texture->GetMipCount(), RHI_Image_Layout::Attachment);
            cmd_state::flush(cmd_list, b);

            D3D12_CPU_DESCRIPTOR_HANDLE rtv = {};
            rtv.ptr = reinterpret_cast<SIZE_T>(texture->GetRhiRtv(0));
            float c[4] = { clear_color.r, clear_color.g, clear_color.b, clear_color.a };
            cmd_list->ClearRenderTargetView(rtv, c, 0, nullptr);
        }
    }

    void RHI_CommandList::Draw(const uint32_t vertex_count, uint32_t vertex_start_index)
    {
        SP_ASSERT(m_state == RHI_CommandListState::Recording);
        auto& b = cmd_state::get(this);
        ID3D12GraphicsCommandList* cmd_list = static_cast<ID3D12GraphicsCommandList*>(m_rhi_resource);
        // always flush queued transitions, otherwise they accumulate across failed psos and diverge from the actual gpu state
        cmd_state::flush(cmd_list, b);
        if (!b.has_root_signature_graphics) return;
        flush_pending_bindings(cmd_list, this, false);
        cmd_list->DrawInstanced(vertex_count, 1, vertex_start_index, 0);
        Profiler::m_rhi_draw++;
    }

    void RHI_CommandList::DrawIndexed(const uint32_t index_count, const uint32_t index_offset, const uint32_t vertex_offset, const uint32_t instance_start_index, const uint32_t instance_count)
    {
        SP_ASSERT(m_state == RHI_CommandListState::Recording);
        auto& b = cmd_state::get(this);
        ID3D12GraphicsCommandList* cmd_list = static_cast<ID3D12GraphicsCommandList*>(m_rhi_resource);
        cmd_state::flush(cmd_list, b);
        if (!b.has_root_signature_graphics) return;
        flush_pending_bindings(cmd_list, this, false);
        cmd_list->DrawIndexedInstanced(index_count, instance_count, index_offset, vertex_offset, instance_start_index);
        Profiler::m_rhi_draw++;
    }

    void RHI_CommandList::DrawIndexedIndirectCount(RHI_Buffer* args_buffer, const uint32_t args_offset, RHI_Buffer* count_buffer, const uint32_t count_offset, const uint32_t max_draw_count)
    {
        SP_ASSERT(m_state == RHI_CommandListState::Recording);
        auto& b = cmd_state::get(this);
        ID3D12GraphicsCommandList* cmd_list = static_cast<ID3D12GraphicsCommandList*>(m_rhi_resource);
        cmd_state::flush(cmd_list, b);

        if (!args_buffer || !count_buffer)
            return;

        if (!b.has_root_signature_graphics) return;

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

        // executeindirect requires args and count buffers in indirect_argument state, they were likely written by a prior compute pass and left as unordered_access
        ID3D12Resource* args_resource  = static_cast<ID3D12Resource*>(args_buffer->GetRhiResource());
        ID3D12Resource* count_resource = static_cast<ID3D12Resource*>(count_buffer->GetRhiResource());
        cmd_state::push_transition(b, args_resource,  D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT);
        cmd_state::push_transition(b, count_resource, D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT);

        flush_pending_bindings(cmd_list, this, false);
        cmd_list->ExecuteIndirect(
            command_signature, max_draw_count,
            args_resource,  static_cast<UINT64>(args_offset),
            count_resource, static_cast<UINT64>(count_offset)
        );
        Profiler::m_rhi_draw++;
    }

    void RHI_CommandList::Dispatch(uint32_t x, uint32_t y, uint32_t z)
    {
        SP_ASSERT(m_state == RHI_CommandListState::Recording);
        auto& b = cmd_state::get(this);
        ID3D12GraphicsCommandList* cmd_list = static_cast<ID3D12GraphicsCommandList*>(m_rhi_resource);
        cmd_state::flush(cmd_list, b);
        if (!b.has_root_signature_compute) return;
        flush_pending_bindings(cmd_list, this, true);
        cmd_list->Dispatch(x, y, z);
    }

    void RHI_CommandList::DrawIndirect(RHI_Buffer* args_buffer, const uint32_t args_offset)
    {
        SP_ASSERT(m_state == RHI_CommandListState::Recording);
        auto& b = cmd_state::get(this);
        ID3D12GraphicsCommandList* cmd_list = static_cast<ID3D12GraphicsCommandList*>(m_rhi_resource);
        cmd_state::flush(cmd_list, b);

        if (!args_buffer)
            return;

        if (!b.has_root_signature_graphics) return;

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

        // executeindirect requires the args buffer in indirect_argument state, it was likely written by a prior compute pass and left as unordered_access
        ID3D12Resource* args_resource = static_cast<ID3D12Resource*>(args_buffer->GetRhiResource());
        cmd_state::push_transition(b, args_resource, D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT);

        flush_pending_bindings(cmd_list, this, false);
        cmd_list->ExecuteIndirect(
            command_signature, 1u,
            args_resource, static_cast<UINT64>(args_offset),
            nullptr, 0
        );
        Profiler::m_rhi_draw++;
    }

    void RHI_CommandList::DispatchIndirect(RHI_Buffer* args_buffer, const uint32_t args_offset)
    {
        SP_ASSERT(m_state == RHI_CommandListState::Recording);
        auto& b = cmd_state::get(this);
        ID3D12GraphicsCommandList* cmd_list = static_cast<ID3D12GraphicsCommandList*>(m_rhi_resource);
        cmd_state::flush(cmd_list, b);

        if (!args_buffer)
            return;

        if (!b.has_root_signature_compute) return;

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

        // executeindirect requires the args buffer in indirect_argument state, it was likely written by a prior compute pass and left as unordered_access
        ID3D12Resource* args_resource = static_cast<ID3D12Resource*>(args_buffer->GetRhiResource());
        cmd_state::push_transition(b, args_resource, D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT);

        flush_pending_bindings(cmd_list, this, true);
        cmd_list->ExecuteIndirect(
            command_signature, 1u,
            args_resource, static_cast<UINT64>(args_offset),
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

        auto& b = cmd_state::get(this);

        // when source and destination dimensions and formats match and no scaling is requested copyresource is the
        // cheapest path, otherwise fall back to a fullscreen-triangle blit that samples the source srv into the destination
        const bool dims_match  = source->GetWidth()  == destination->GetWidth()
                              && source->GetHeight() == destination->GetHeight();
        const bool format_match = source->GetFormat() == destination->GetFormat();
        const bool no_scaling   = resolution_scale >= 1.0f - 1e-6f && resolution_scale <= 1.0f + 1e-6f;

        if (dims_match && format_match && no_scaling)
        {
            cmd_state::push_transition(b, src, D3D12_RESOURCE_STATE_COPY_SOURCE);
            cmd_state::push_transition(b, dst, D3D12_RESOURCE_STATE_COPY_DEST);
            cmd_state::flush(cmd_list, b);

            cmd_list->CopyResource(dst, src);

            source->SetLayoutDirect(0, source->GetMipCount(), RHI_Image_Layout::Transfer_Source);
            destination->SetLayoutDirect(0, destination->GetMipCount(), RHI_Image_Layout::Transfer_Destination);
            return;
        }

        // graphics pipeline blit, mirrors the vkCmdBlitImage scaling semantics that rhi callers expect
        const bool dst_is_depth = destination->IsDepthStencilFormat();
        const bool src_is_depth = source->IsDepthStencilFormat();

        // depth resources additionally allow depth_read to coexist with shader_resource state
        const D3D12_RESOURCE_STATES src_read_state = src_is_depth
            ? (D3D12_RESOURCE_STATE_DEPTH_READ | D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE)
            : (D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

        cmd_state::push_transition(b, src, src_read_state);
        cmd_state::push_transition(b, dst, dst_is_depth ? D3D12_RESOURCE_STATE_DEPTH_WRITE : D3D12_RESOURCE_STATE_RENDER_TARGET);
        cmd_state::flush(cmd_list, b);

        d3d12_blit::BlitParams params = {};
        void* src_srv_ptr = source->GetRhiSrv();
        if (!src_srv_ptr) return;
        params.source_srv_cpu_handle.ptr = reinterpret_cast<SIZE_T>(src_srv_ptr);

        if (dst_is_depth)
        {
            void* dsv_ptr = destination->GetRhiDsv(0);
            if (!dsv_ptr) return;
            params.destination_dsv_handle.ptr = reinterpret_cast<SIZE_T>(dsv_ptr);
        }
        else
        {
            void* rtv_ptr = destination->GetRhiRtv(0);
            if (!rtv_ptr) return;
            params.destination_rtv_handle.ptr = reinterpret_cast<SIZE_T>(rtv_ptr);
        }

        params.destination_format    = d3d12_format[rhi_format_to_index(destination->GetFormat())];
        params.destination_width     = destination->GetWidth();
        params.destination_height    = destination->GetHeight();
        params.is_depth_destination  = dst_is_depth;
        // resolution_scale shrinks the source extent that fills the destination, matches the vulkan semantics where
        // source[0..w*scale, 0..h*scale] is mapped onto destination[0..dst_w, 0..dst_h]
        params.source_uv_scale_x     = resolution_scale;
        params.source_uv_scale_y     = resolution_scale;

        d3d12_blit::blit(cmd_list, params);

        // the blit changed the bound root signature and pipeline, mark cmd_state so the next graphics call rebinds
        // the bindless layout cleanly, b.is_compute_bound stays untouched since this path never affects compute state
        b.has_root_signature_graphics = false;
        b.is_imgui_pipeline_bound     = false;

        source->SetLayoutDirect(0, source->GetMipCount(), RHI_Image_Layout::Shader_Read);
        destination->SetLayoutDirect(0, destination->GetMipCount(), RHI_Image_Layout::Attachment);
    }

    void RHI_CommandList::Blit(RHI_Texture* source, RHI_SwapChain* destination)
    {
        SP_ASSERT_MSG((source->GetFlags() & RHI_Texture_ClearBlit) != 0, "The texture needs the RHI_Texture_ClearOrBlit flag");
        if (!source || !destination) return;

        ID3D12GraphicsCommandList* cmd_list = static_cast<ID3D12GraphicsCommandList*>(m_rhi_resource);
        ID3D12Resource* src = static_cast<ID3D12Resource*>(source->GetRhiResource());
        ID3D12Resource* dst = static_cast<ID3D12Resource*>(destination->GetRhiRt());
        if (!src || !dst) return;

        // transition source to copy_source and backbuffer to copy_dest, then issue the copy
        auto& b = cmd_state::get(this);
        cmd_state::push_transition(b, src, D3D12_RESOURCE_STATE_COPY_SOURCE);
        cmd_state::push_transition(b, dst, D3D12_RESOURCE_STATE_COPY_DEST);
        cmd_state::flush(cmd_list, b);

        cmd_list->CopyResource(dst, src);

        // leave source layout consistent with d3d12 state, the rhi layout map will be re-synced by the next pass
        source->SetLayoutDirect(0, source->GetMipCount(), RHI_Image_Layout::Transfer_Source);

        // backbuffer stays in copy_dest, the present-time barrier in SubmitAndPresent transitions it to present
        b.swapchain_bb_transitioned = dst;
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

        // skip when no matching root signature is bound, can happen if the pipeline failed to bind
        if (b.is_compute_bound && !b.has_root_signature_compute)  return;
        if (!b.is_compute_bound && !b.has_root_signature_graphics) return;

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
            {
                if (!b.has_root_signature_compute) return;
                cmd_list->SetComputeRoot32BitConstants(d3d12_root_slot::push_constants, num_32bit, data, offset / 4);
            }
            else
            {
                if (!b.has_root_signature_graphics) return;
                cmd_list->SetGraphicsRoot32BitConstants(d3d12_root_slot::push_constants, num_32bit, data, offset / 4);
            }
        }
        else
        {
            // imgui root signature has constants at root param 0
            if (!b.is_imgui_pipeline_bound) return;
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

        // engine only declares a uav SetBuffer overload (Renderer_BindingsUav), so this path is uav-only
        // slots beyond what the bindless root signature exposes (u0..u44) are skipped, this matches the editor-level limit
        if (slot >= 45) return;

        D3D12_UNORDERED_ACCESS_VIEW_DESC desc = {};
        desc.Format                     = DXGI_FORMAT_UNKNOWN;
        desc.ViewDimension              = D3D12_UAV_DIMENSION_BUFFER;
        desc.Buffer.NumElements         = buffer->GetElementCount();
        desc.Buffer.StructureByteStride = buffer->GetStride();

        uint32_t idx = d3d12_descriptors::AllocateCbvSrvUavCpu();
        D3D12_CPU_DESCRIPTOR_HANDLE h = d3d12_descriptors::GetCbvSrvUavCpuHandle(idx);
        ID3D12Resource* resource = static_cast<ID3D12Resource*>(buffer->GetRhiResource());
        RHI_Context::device->CreateUnorderedAccessView(resource, nullptr, &desc, h);

        // ensure the buffer is in unordered_access state at draw/dispatch time
        cmd_state::push_transition(b, resource, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

        b.uav[slot]   = h;
        b.uav_dirty   = true;
    }

    // create a transient mip-specific view (uav or srv) for a texture in the cpu staging heap
    // returns a cpu handle that can be copied into the bindless ring; falls back to the all-mips view if creation isn't viable
    static D3D12_CPU_DESCRIPTOR_HANDLE create_transient_mip_view(RHI_Texture* texture, uint32_t mip_index, uint32_t mip_range, bool uav, uint32_t array_layer)
    {
        D3D12_CPU_DESCRIPTOR_HANDLE handle = {};

        if (!texture) return handle;

        ID3D12Resource* resource = static_cast<ID3D12Resource*>(texture->GetRhiResource());
        if (!resource) return handle;

        uint32_t alloc_idx = d3d12_descriptors::AllocateCbvSrvUavCpu();
        handle             = d3d12_descriptors::GetCbvSrvUavCpuHandle(alloc_idx);

        // resolve format from the texture's underlying format, depth formats need their srv-compatible variant
        const RHI_Texture_Type type = texture->GetType();

        D3D12_RESOURCE_DESC desc = resource->GetDesc();
        DXGI_FORMAT view_format  = desc.Format;
        if (texture->IsDepthStencilFormat())
        {
            switch (texture->GetFormat())
            {
                case RHI_Format::D16_Unorm:           view_format = DXGI_FORMAT_R16_UNORM;      break;
                case RHI_Format::D32_Float:           view_format = DXGI_FORMAT_R32_FLOAT;      break;
                case RHI_Format::D32_Float_S8X24_Uint:view_format = DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS; break;
                default: break;
            }
        }

        const uint32_t mip_count    = (mip_range == 0) ? 1u : mip_range;
        const uint32_t array_length = texture->GetArrayLength();
        const bool layer_specified  = array_layer != rhi_all_mips;

        if (uav)
        {
            D3D12_UNORDERED_ACCESS_VIEW_DESC uav_desc = {};
            uav_desc.Format = view_format;
            if (type == RHI_Texture_Type::Type2D)
            {
                uav_desc.ViewDimension      = D3D12_UAV_DIMENSION_TEXTURE2D;
                uav_desc.Texture2D.MipSlice = mip_index;
            }
            else if (type == RHI_Texture_Type::Type2DArray || type == RHI_Texture_Type::TypeCube)
            {
                uav_desc.ViewDimension                  = D3D12_UAV_DIMENSION_TEXTURE2DARRAY;
                uav_desc.Texture2DArray.MipSlice        = mip_index;
                uav_desc.Texture2DArray.FirstArraySlice = layer_specified ? array_layer : 0;
                uav_desc.Texture2DArray.ArraySize       = layer_specified ? 1u : array_length;
            }
            else if (type == RHI_Texture_Type::Type3D)
            {
                uav_desc.ViewDimension         = D3D12_UAV_DIMENSION_TEXTURE3D;
                uav_desc.Texture3D.MipSlice    = mip_index;
                uav_desc.Texture3D.FirstWSlice = 0;
                uav_desc.Texture3D.WSize       = texture->GetDepth();
            }
            else
            {
                D3D12_CPU_DESCRIPTOR_HANDLE empty = {};
                return empty;
            }

            RHI_Context::device->CreateUnorderedAccessView(resource, nullptr, &uav_desc, handle);
        }
        else
        {
            D3D12_SHADER_RESOURCE_VIEW_DESC srv_desc = {};
            srv_desc.Format                  = view_format;
            srv_desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            if (type == RHI_Texture_Type::Type2D)
            {
                srv_desc.ViewDimension             = D3D12_SRV_DIMENSION_TEXTURE2D;
                srv_desc.Texture2D.MostDetailedMip = mip_index;
                srv_desc.Texture2D.MipLevels       = mip_count;
            }
            else if (type == RHI_Texture_Type::Type2DArray)
            {
                srv_desc.ViewDimension                  = D3D12_SRV_DIMENSION_TEXTURE2DARRAY;
                srv_desc.Texture2DArray.MostDetailedMip = mip_index;
                srv_desc.Texture2DArray.MipLevels       = mip_count;
                srv_desc.Texture2DArray.FirstArraySlice = layer_specified ? array_layer : 0;
                srv_desc.Texture2DArray.ArraySize       = layer_specified ? 1u : array_length;
            }
            else if (type == RHI_Texture_Type::TypeCube)
            {
                srv_desc.ViewDimension               = D3D12_SRV_DIMENSION_TEXTURECUBE;
                srv_desc.TextureCube.MostDetailedMip = mip_index;
                srv_desc.TextureCube.MipLevels       = mip_count;
            }
            else if (type == RHI_Texture_Type::Type3D)
            {
                srv_desc.ViewDimension             = D3D12_SRV_DIMENSION_TEXTURE3D;
                srv_desc.Texture3D.MostDetailedMip = mip_index;
                srv_desc.Texture3D.MipLevels       = mip_count;
            }
            else
            {
                D3D12_CPU_DESCRIPTOR_HANDLE empty = {};
                return empty;
            }

            RHI_Context::device->CreateShaderResourceView(resource, &srv_desc, handle);
        }

        return handle;
    }

    void RHI_CommandList::SetTexture(const uint32_t slot, RHI_Texture* texture, const uint32_t mip_index, uint32_t mip_range, const bool uav, const uint32_t array_layer)
    {
        SP_ASSERT(m_state == RHI_CommandListState::Recording);

        auto& b = cmd_state::get(this);

        // imgui path, only valid when an imgui graphics pso is currently bound
        if (!b.is_bindless_pipeline)
        {
            if (!b.is_imgui_pipeline_bound) return;
            if (!texture || !texture->GetRhiSrv()) return;

            ID3D12GraphicsCommandList* cmd_list = static_cast<ID3D12GraphicsCommandList*>(m_rhi_resource);

            // imgui-bound textures may currently be in render_target state from earlier passes, transition them to shader_read
            ID3D12Resource* resource = static_cast<ID3D12Resource*>(texture->GetRhiResource());
            cmd_state::push_transition(b, resource,
                D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
            cmd_state::flush(cmd_list, b);

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
            if (uav) { if (slot < 45) { b.uav[slot].ptr = 0; b.uav_dirty = true; } }
            else     { if (slot < 27) { b.srv[slot].ptr = 0; b.srv_dirty = true; } }
            return;
        }

        ID3D12Resource* resource = static_cast<ID3D12Resource*>(texture->GetRhiResource());
        const bool mip_specified = mip_index != rhi_all_mips;
        const uint32_t total_mips = texture->GetMipCount();
        const uint32_t array_size = texture->GetArrayLength();
        const uint32_t effective_mip_range = mip_range == 0 ? 1u : mip_range;

        // helper, push a transition for either a specific subresource range or all subresources
        // when mip_specified is true and mip_index > 0, only the specified mips for the chosen array layer(s) transition
        // this keeps the rest of the texture in its existing state, required for passes that read other mips as srv
        // while writing one mip as uav (e.g. mipmap filtering)
        auto push_transition_for_view = [&](D3D12_RESOURCE_STATES state)
        {
            if (mip_specified && mip_index > 0)
            {
                const uint32_t array_first = (array_layer == rhi_all_mips) ? 0u : array_layer;
                const uint32_t array_last  = (array_layer == rhi_all_mips) ? array_size : (array_layer + 1u);
                for (uint32_t a = array_first; a < array_last; a++)
                {
                    for (uint32_t m = mip_index; m < mip_index + effective_mip_range && m < total_mips; m++)
                    {
                        uint32_t subresource = m + a * total_mips;
                        cmd_state::push_transition(b, resource, state, subresource);
                    }
                }
            }
            else
            {
                cmd_state::push_transition(b, resource, state);
            }
        };

        if (uav)
        {
            if (slot >= 45) return;

            // ensure the texture is in unordered_access for both reads and writes
            push_transition_for_view(D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            texture->SetLayoutDirect(0, total_mips, RHI_Image_Layout::General);

            D3D12_CPU_DESCRIPTOR_HANDLE h = {};
            if (mip_specified && mip_index > 0)
            {
                // create a transient mip-specific uav, the static all-mips uav at m_rhi_srv_mips[0] is the only one populated by Texture creation
                h = create_transient_mip_view(texture, mip_index, effective_mip_range, true, array_layer);
            }
            else
            {
                void* uav_ptr = texture->GetRhiSrvMip(0); // all-mips uav cpu handle
                if (uav_ptr) h.ptr = reinterpret_cast<SIZE_T>(uav_ptr);
            }

            if (h.ptr == 0) return;
            b.uav[slot] = h;
            b.uav_dirty = true;
        }
        else
        {
            if (slot >= 27) return;

            // shader_read covers both pixel and non-pixel reads, depth textures additionally allow depth_read
            const bool is_depth = texture->IsDepthStencilFormat();
            D3D12_RESOURCE_STATES read_state = is_depth
                ? (D3D12_RESOURCE_STATE_DEPTH_READ | D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE)
                : (D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
            push_transition_for_view(read_state);
            texture->SetLayoutDirect(0, total_mips, RHI_Image_Layout::Shader_Read);

            D3D12_CPU_DESCRIPTOR_HANDLE h = {};
            if (mip_specified && mip_index > 0)
            {
                h = create_transient_mip_view(texture, mip_index, effective_mip_range, false, array_layer);
            }
            else
            {
                void* srv_ptr = texture->GetRhiSrv();
                if (srv_ptr) h.ptr = reinterpret_cast<SIZE_T>(srv_ptr);
            }

            if (h.ptr == 0) return;
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
        if (!buffer || !data || size == 0)
            return;

        // mapped path, persistently mapped upload heap buffers can be memcpyd directly
        if (buffer->GetMappedData())
        {
            memcpy(static_cast<uint8_t*>(buffer->GetMappedData()) + offset, data, static_cast<size_t>(size));
            return;
        }

        // staged path, default-heap buffers are uploaded via a transient staging buffer copied on this cmd list
        ID3D12Resource* dst = static_cast<ID3D12Resource*>(buffer->GetRhiResource());
        if (!dst)
            return;

        void* staging_ptr = RHI_Device::StagingBufferAcquire(size);
        ID3D12Resource* staging = static_cast<ID3D12Resource*>(staging_ptr);
        if (!staging)
            return;

        void* mapped = nullptr;
        D3D12_RANGE rr = { 0, 0 };
        if (FAILED(staging->Map(0, &rr, &mapped)) || !mapped)
        {
            RHI_Device::StagingBufferRelease(staging_ptr);
            return;
        }
        memcpy(mapped, data, static_cast<size_t>(size));
        staging->Unmap(0, nullptr);

        ID3D12GraphicsCommandList* cmd_list = static_cast<ID3D12GraphicsCommandList*>(m_rhi_resource);
        auto& b = cmd_state::get(this);

        // transition dst to copy_dest, copy, then back to a shader-read state so subsequent srv reads work
        // a follow-up SetBuffer(uav) call pushes its own transition to unordered_access if needed
        cmd_state::push_transition(b, dst, D3D12_RESOURCE_STATE_COPY_DEST);
        cmd_state::flush(cmd_list, b);

        cmd_list->CopyBufferRegion(dst, offset, staging, 0, size);

        cmd_state::push_transition(b, dst, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

        // hold the staging buffer until this cmd list completes, Begin/destructor releases them back to the pool
        b.staging_buffers_in_flight.push_back(staging_ptr);
    }

    void RHI_CommandList::InsertBarrier(const RHI_Barrier& barrier)
    {
        SP_ASSERT(m_state == RHI_CommandListState::Recording);

        auto& b = cmd_state::get(this);

        switch (barrier.type)
        {
            case RHI_Barrier::Type::ImageLayout:
            {
                ID3D12Resource* resource = nullptr;
                bool is_depth            = false;

                if (barrier.texture)
                {
                    resource = static_cast<ID3D12Resource*>(barrier.texture->GetRhiResource());
                    is_depth = barrier.texture->IsDepthStencilFormat();
                }
                else if (barrier.image)
                {
                    resource = static_cast<ID3D12Resource*>(barrier.image);
                }

                if (!resource) return;

                D3D12_RESOURCE_STATES state_after = rhi_layout_to_d3d12_state(barrier.layout, is_depth);

                // honor mip_index and mip_range when supplied so per-mip transitions can be requested
                // (e.g. mipmap filtering passes that need to flip a single mip back to shader_read between dispatches)
                if (barrier.texture && barrier.mip_index != rhi_all_mips)
                {
                    const uint32_t total_mips = barrier.texture->GetMipCount();
                    const uint32_t array_size = barrier.texture->GetArrayLength();
                    const uint32_t mip_count  = barrier.mip_range == 0 ? 1u : barrier.mip_range;
                    for (uint32_t a = 0; a < array_size; a++)
                    {
                        for (uint32_t m = barrier.mip_index; m < barrier.mip_index + mip_count && m < total_mips; m++)
                        {
                            uint32_t subresource = m + a * total_mips;
                            cmd_state::push_transition(b, resource, state_after, subresource);
                        }
                    }
                }
                else
                {
                    cmd_state::push_transition(b, resource, state_after);
                }

                if (barrier.texture)
                {
                    barrier.texture->SetLayoutDirect(0, barrier.texture->GetMipCount(), barrier.layout);
                }
                break;
            }
            case RHI_Barrier::Type::ImageSync:
            {
                if (!barrier.texture) return;
                ID3D12Resource* resource = static_cast<ID3D12Resource*>(barrier.texture->GetRhiResource());
                cmd_state::push_uav_barrier(b, resource);
                break;
            }
            case RHI_Barrier::Type::BufferSync:
            {
                if (!barrier.buffer) return;
                ID3D12Resource* resource = static_cast<ID3D12Resource*>(barrier.buffer->GetRhiResource());
                cmd_state::push_uav_barrier(b, resource);
                break;
            }
        }
    }

    void RHI_CommandList::FlushBarriers()
    {
        SP_ASSERT(m_state == RHI_CommandListState::Recording);
        ID3D12GraphicsCommandList* cmd_list = static_cast<ID3D12GraphicsCommandList*>(m_rhi_resource);
        auto& b = cmd_state::get(this);
        cmd_state::flush(cmd_list, b);
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
        static const uint32_t queue_type_count = static_cast<uint32_t>(RHI_Queue_Type::Max);

        // dedicated queues for one-shot uploads etc., kept separate from the renderer's main queues
        // so a one-shot submission cannot rotate through the same ring as m_cmd_list_present and
        // accidentally submit it mid-frame
        array<mutex, queue_type_count>              mutexes;
        array<condition_variable, queue_type_count> condition_vars;
        array<bool, queue_type_count>               is_executing = { false, false, false };
        array<shared_ptr<RHI_Queue>, queue_type_count> queues;
        once_flag init_flag;

        void ensure_initialized()
        {
            call_once(init_flag, []()
            {
                queues[static_cast<uint32_t>(RHI_Queue_Type::Graphics)] = make_shared<RHI_Queue>(RHI_Queue_Type::Graphics, "graphics_immediate");
                queues[static_cast<uint32_t>(RHI_Queue_Type::Compute)]  = make_shared<RHI_Queue>(RHI_Queue_Type::Compute,  "compute_immediate");
                queues[static_cast<uint32_t>(RHI_Queue_Type::Copy)]     = make_shared<RHI_Queue>(RHI_Queue_Type::Copy,     "copy_immediate");
            });
        }
    }

    RHI_CommandList* RHI_CommandList::ImmediateExecutionBegin(const RHI_Queue_Type queue_type)
    {
        if (RHI_Device::IsDeviceLost())
            return nullptr;

        immediate_execution::ensure_initialized();

        const uint32_t qi = static_cast<uint32_t>(queue_type);

        unique_lock<mutex> lock(immediate_execution::mutexes[qi]);
        immediate_execution::condition_vars[qi].wait(lock, [qi] { return !immediate_execution::is_executing[qi]; });
        immediate_execution::is_executing[qi] = true;

        RHI_Queue* queue = immediate_execution::queues[qi].get();
        RHI_CommandList* cmd_list = queue->NextCommandList();
        cmd_list->Begin();
        return cmd_list;
    }

    void RHI_CommandList::ImmediateExecutionEnd(RHI_CommandList* cmd_list)
    {
        if (!cmd_list) return;

        cmd_list->Submit(nullptr, true);

        const uint32_t qi = static_cast<uint32_t>(cmd_list->GetQueue()->GetType());
        immediate_execution::is_executing[qi] = false;
        immediate_execution::condition_vars[qi].notify_one();
    }

    void RHI_CommandList::ImmediateExecutionShutdown()
    {
        for (uint32_t i = 0; i < immediate_execution::queue_type_count; i++)
        {
            unique_lock<mutex> lock(immediate_execution::mutexes[i]);
            immediate_execution::condition_vars[i].wait(lock, [i] { return !immediate_execution::is_executing[i]; });
        }

        immediate_execution::queues.fill(nullptr);
    }

    void* RHI_CommandList::GetRhiResourcePipeline()
    {
        return m_pipeline ? m_pipeline->GetRhiResource() : nullptr;
    }
}
