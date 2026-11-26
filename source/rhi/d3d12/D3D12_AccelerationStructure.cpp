/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

//= INCLUDES =====================
#include "pch.h"
#include "../RHI_AccelerationStructure.h"
#include "../RHI_Device.h"
#include "../RHI_Implementation.h"
#include "../RHI_CommandList.h"
#include "D3D12_Internal.h"
#include "../../profiling/Profiler.h"
#include "../../memory/GpuMemory.h"
#include <wrl/client.h>
#include <unordered_map>
#include <mutex>
#include <algorithm>
//================================

//= NAMESPACES =====
using namespace std;
//==================

namespace spartan
{
    namespace
    {
        uint64_t align_up(uint64_t value, uint64_t alignment)
        {
            return (value + alignment - 1) & ~(alignment - 1);
        }

        // create a default-heap committed buffer suitable for ray tracing structures via ID3D12Device10::CreateCommittedResource3
        // enhanced barriers replace the legacy RAYTRACING_ACCELERATION_STRUCTURE initial state with D3D12_RESOURCE_FLAG_RAYTRACING_ACCELERATION_STRUCTURE
        ID3D12Resource* create_buffer(uint64_t size, bool is_acceleration_structure, const char* name)
        {
            D3D12_HEAP_PROPERTIES heap_props = {};
            heap_props.Type                  = D3D12_HEAP_TYPE_DEFAULT;
            heap_props.CPUPageProperty       = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
            heap_props.MemoryPoolPreference  = D3D12_MEMORY_POOL_UNKNOWN;
            heap_props.CreationNodeMask      = 1;
            heap_props.VisibleNodeMask       = 1;

            D3D12_RESOURCE_DESC1 desc = {};
            desc.Dimension            = D3D12_RESOURCE_DIMENSION_BUFFER;
            desc.Alignment            = 0;
            desc.Width                = size;
            desc.Height               = 1;
            desc.DepthOrArraySize     = 1;
            desc.MipLevels            = 1;
            desc.Format               = DXGI_FORMAT_UNKNOWN;
            desc.SampleDesc.Count     = 1;
            desc.SampleDesc.Quality   = 0;
            desc.Layout               = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
            desc.Flags                = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
            if (is_acceleration_structure)
            {
                desc.Flags |= D3D12_RESOURCE_FLAG_RAYTRACING_ACCELERATION_STRUCTURE;
            }

            ID3D12Resource* resource = nullptr;
            HRESULT hr = RHI_Context::device->CreateCommittedResource3(
                &heap_props,
                D3D12_HEAP_FLAG_NONE,
                &desc,
                D3D12_BARRIER_LAYOUT_UNDEFINED,
                nullptr,
                nullptr,
                0,
                nullptr,
                IID_PPV_ARGS(&resource)
            );

            if (FAILED(hr))
            {
                d3d12_utility::error::fatal(hr, "Allocate ray tracing buffer (CreateCommittedResource3)", name, size);
            }

            d3d12_state::SetIsBuffer(resource, true);
            d3d12_state::SetSubresourceCount(resource, 1);
            if (name)
            {
                d3d12_utility::debug::set_name(resource, name);
            }

            d3d12_gpu_memory::register_resource(
                resource,
                is_acceleration_structure ?
                    GpuMemoryKind::AccelerationStructure :
                    GpuMemoryKind::Storage,
                name
            );

            return resource;
        }

        // create a readback buffer for postbuild compacted size queries
        ID3D12Resource* create_readback_buffer(uint64_t size, const char* name)
        {
            D3D12_HEAP_PROPERTIES heap_props = {};
            heap_props.Type                  = D3D12_HEAP_TYPE_READBACK;
            heap_props.CPUPageProperty       = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
            heap_props.MemoryPoolPreference  = D3D12_MEMORY_POOL_UNKNOWN;
            heap_props.CreationNodeMask      = 1;
            heap_props.VisibleNodeMask       = 1;

            D3D12_RESOURCE_DESC1 desc = {};
            desc.Dimension            = D3D12_RESOURCE_DIMENSION_BUFFER;
            desc.Alignment            = 0;
            desc.Width                = size;
            desc.Height               = 1;
            desc.DepthOrArraySize     = 1;
            desc.MipLevels            = 1;
            desc.Format               = DXGI_FORMAT_UNKNOWN;
            desc.SampleDesc.Count     = 1;
            desc.SampleDesc.Quality   = 0;
            desc.Layout               = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
            desc.Flags                = D3D12_RESOURCE_FLAG_NONE;

            ID3D12Resource* resource = nullptr;
            HRESULT hr = RHI_Context::device->CreateCommittedResource3(
                &heap_props,
                D3D12_HEAP_FLAG_NONE,
                &desc,
                D3D12_BARRIER_LAYOUT_UNDEFINED,
                nullptr,
                nullptr,
                0,
                nullptr,
                IID_PPV_ARGS(&resource)
            );

            if (FAILED(hr))
            {
                SP_LOG_WARNING("Failed to allocate ray tracing readback buffer (%llu bytes)", size);
                return nullptr;
            }

            d3d12_state::SetIsBuffer(resource, true);
            d3d12_state::SetSubresourceCount(resource, 1);
            if (name)
            {
                d3d12_utility::debug::set_name(resource, name);
            }

            d3d12_gpu_memory::register_resource(resource, GpuMemoryKind::Readback, name);
            return resource;
        }

        // create or grow an upload-heap buffer used to stage tlas instance descriptors
        ID3D12Resource* create_upload_buffer(uint64_t size, const char* name)
        {
            // the gpu reads these descriptors directly during the tlas build, so vram resident and
            // cpu writable beats a pcie read, see the same reasoning in D3D12_Buffer.cpp
            const bool use_gpu_upload = d3d12_caps::IsGpuUploadHeapSupported();

            D3D12_HEAP_PROPERTIES heap_props = {};
            heap_props.Type                  = use_gpu_upload ? D3D12_HEAP_TYPE_GPU_UPLOAD : D3D12_HEAP_TYPE_UPLOAD;
            heap_props.CPUPageProperty       = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
            heap_props.MemoryPoolPreference  = D3D12_MEMORY_POOL_UNKNOWN;
            heap_props.CreationNodeMask      = 1;
            heap_props.VisibleNodeMask       = 1;

            D3D12_RESOURCE_DESC1 desc = {};
            desc.Dimension            = D3D12_RESOURCE_DIMENSION_BUFFER;
            desc.Alignment            = 0;
            desc.Width                = size;
            desc.Height               = 1;
            desc.DepthOrArraySize     = 1;
            desc.MipLevels            = 1;
            desc.Format               = DXGI_FORMAT_UNKNOWN;
            desc.SampleDesc.Count     = 1;
            desc.SampleDesc.Quality   = 0;
            desc.Layout               = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
            desc.Flags                = D3D12_RESOURCE_FLAG_NONE;

            ID3D12Resource* resource = nullptr;
            HRESULT hr = RHI_Context::device->CreateCommittedResource3(
                &heap_props,
                D3D12_HEAP_FLAG_NONE,
                &desc,
                D3D12_BARRIER_LAYOUT_UNDEFINED,
                nullptr,
                nullptr,
                0,
                nullptr,
                IID_PPV_ARGS(&resource)
            );

            // retry on the regular upload heap once the resizable bar window is exhausted
            if (FAILED(hr) && use_gpu_upload)
            {
                heap_props.Type = D3D12_HEAP_TYPE_UPLOAD;

                hr = RHI_Context::device->CreateCommittedResource3(
                    &heap_props,
                    D3D12_HEAP_FLAG_NONE,
                    &desc,
                    D3D12_BARRIER_LAYOUT_UNDEFINED,
                    nullptr,
                    nullptr,
                    0,
                    nullptr,
                    IID_PPV_ARGS(&resource)
                );
            }

            if (FAILED(hr))
            {
                d3d12_utility::error::fatal(hr, "Allocate ray tracing upload buffer (CreateCommittedResource3)", name, size);
            }

            d3d12_state::SetIsBuffer(resource, true);
            d3d12_state::SetSubresourceCount(resource, 1);
            if (name)
            {
                d3d12_utility::debug::set_name(resource, name);
            }

            d3d12_gpu_memory::register_resource(
                resource,
                GpuMemoryKind::Upload,
                name
            );

            return resource;
        }

        // map any rhi format to its dxgi equivalent through the shared backend table instead of a partial switch,
        // this keeps blas vertex and index formats correct for every format the engine can feed the as builder
        DXGI_FORMAT format_to_dxgi(RHI_Format f)
        {
            if (f == RHI_Format::Max)
            {
                return DXGI_FORMAT_UNKNOWN;
            }

            return d3d12_format[rhi_format_to_index(f)];
        }

        // enhanced memory barrier ordering acceleration structure builds/copies against subsequent reads or scratch reuse
        void as_memory_barrier(ID3D12GraphicsCommandList* cmd_list, ID3D12Resource* resource)
        {
            const D3D12_BARRIER_SYNC sync =
                D3D12_BARRIER_SYNC_BUILD_RAYTRACING_ACCELERATION_STRUCTURE |
                D3D12_BARRIER_SYNC_COPY_RAYTRACING_ACCELERATION_STRUCTURE  |
                D3D12_BARRIER_SYNC_RAYTRACING                              |
                D3D12_BARRIER_SYNC_COMPUTE_SHADING;
            const D3D12_BARRIER_ACCESS access =
                D3D12_BARRIER_ACCESS_RAYTRACING_ACCELERATION_STRUCTURE_READ  |
                D3D12_BARRIER_ACCESS_RAYTRACING_ACCELERATION_STRUCTURE_WRITE |
                D3D12_BARRIER_ACCESS_UNORDERED_ACCESS;

            if (!resource)
            {
                d3d12_barriers::SubmitGlobal(cmd_list, sync, sync, access, access);
            }
            else
            {
                d3d12_barriers::SubmitBuffer(cmd_list, resource, sync, sync, access, access);
            }
        }

        // sub-allocates 256-byte-aligned blas slices out of 32 mb RAYTRACING_ACCELERATION_STRUCTURE pages,
        // avoiding thousands of 64 kb-minimum committed heaps and letting temporary uncompacted pages free cleanly
        namespace blas_pool
        {
            constexpr uint64_t page_size_default = 32ull * 1024ull * 1024ull;

            struct Range
            {
                uint64_t offset = 0;
                uint64_t size   = 0;
            };

            struct Page
            {
                ID3D12Resource* resource = nullptr;
                uint64_t gpu_va          = 0;
                uint64_t capacity        = 0;
                uint64_t allocated_bytes = 0;
                bool compacted           = false;
                vector<Range> free_ranges;
            };

            struct Slice
            {
                Page* page      = nullptr;
                uint64_t offset = 0;
                uint64_t size   = 0;
                bool compacted  = false;
            };

            struct DeferredFree
            {
                Slice slice;
                shared_ptr<const RHI_PendingWork> ready;
            };

            vector<unique_ptr<Page>> pages_uncompacted;
            vector<unique_ptr<Page>> pages_compacted;
            unordered_map<const RHI_AccelerationStructure*, Slice> slices;
            vector<DeferredFree> deferred_frees;

            void release_empty_pages(vector<unique_ptr<Page>>& pages)
            {
                for (size_t i = 0; i < pages.size();)
                {
                    if (pages[i]->allocated_bytes == 0)
                    {
                        if (pages[i]->resource)
                        {
                            d3d12_state::RemoveState(pages[i]->resource);
                            GpuMemory::Unregister(pages[i]->resource);
                            pages[i]->resource->Release();
                        }
                        pages[i] = move(pages.back());
                        pages.pop_back();
                    }
                    else
                    {
                        i++;
                    }
                }
            }

            void return_slice_to_page(const Slice& slice)
            {
                Page* page = slice.page;
                if (!page || slice.size == 0)
                {
                    return;
                }

                page->allocated_bytes -= min(page->allocated_bytes, slice.size);

                // insert sorted by offset and coalesce adjacent free blocks
                auto& ranges = page->free_ranges;
                auto it = lower_bound(ranges.begin(), ranges.end(), slice.offset, [](const Range& r, uint64_t off)
                {
                    return r.offset < off;
                });
                it = ranges.insert(it, { slice.offset, slice.size });

                if (it + 1 != ranges.end() && it->offset + it->size == (it + 1)->offset)
                {
                    it->size += (it + 1)->size;
                    ranges.erase(it + 1);
                }
                if (it != ranges.begin() && (it - 1)->offset + (it - 1)->size == it->offset)
                {
                    (it - 1)->size += it->size;
                    ranges.erase(it);
                }
            }

            void drain_deferred_frees(bool force = false)
            {
                bool any_freed = false;
                size_t retained = 0;
                for (size_t i = 0; i < deferred_frees.size(); i++)
                {
                    DeferredFree& entry = deferred_frees[i];
                    if (force || !entry.ready || entry.ready->IsComplete())
                    {
                        return_slice_to_page(entry.slice);
                        any_freed = true;
                    }
                    else
                    {
                        if (retained != i)
                        {
                            deferred_frees[retained] = move(entry);
                        }
                        retained++;
                    }
                }
                deferred_frees.resize(retained);

                if (any_freed)
                {
                    release_empty_pages(pages_uncompacted);
                    release_empty_pages(pages_compacted);
                }
            }

            Slice allocate(uint64_t raw_size, bool compacted)
            {
                drain_deferred_frees(false);

                const uint64_t size = align_up(max<uint64_t>(raw_size, D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BYTE_ALIGNMENT), D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BYTE_ALIGNMENT);
                vector<unique_ptr<Page>>& pages = compacted ? pages_compacted : pages_uncompacted;

                for (unique_ptr<Page>& page : pages)
                {
                    for (size_t i = 0; i < page->free_ranges.size(); i++)
                    {
                        Range& range = page->free_ranges[i];
                        if (range.size >= size)
                        {
                            const uint64_t offset = range.offset;
                            if (range.size == size)
                            {
                                page->free_ranges.erase(page->free_ranges.begin() + static_cast<ptrdiff_t>(i));
                            }
                            else
                            {
                                range.offset += size;
                                range.size   -= size;
                            }
                            page->allocated_bytes += size;
                            return { page.get(), offset, size, compacted };
                        }
                    }
                }

                const uint64_t page_bytes = max(page_size_default, align_up(size, 65536ull));
                ID3D12Resource* resource  = create_buffer(page_bytes, true, compacted ? "blas_pool_compacted" : "blas_pool_uncompacted");
                if (!resource)
                {
                    return {};
                }

                auto page             = make_unique<Page>();
                page->resource        = resource;
                page->gpu_va          = resource->GetGPUVirtualAddress();
                page->capacity        = page_bytes;
                page->allocated_bytes = size;
                page->compacted       = compacted;
                if (page_bytes > size)
                {
                    page->free_ranges.push_back({ size, page_bytes - size });
                }

                Page* raw_page = page.get();
                pages.push_back(move(page));
                return { raw_page, 0, size, compacted };
            }

            void free_deferred(const Slice& slice)
            {
                if (!slice.page || slice.size == 0)
                {
                    return;
                }
                deferred_frees.push_back({ slice, RHI_CommandList::CapturePendingWork() });
            }

            void destroy_all()
            {
                drain_deferred_frees(true);
                slices.clear();

                auto destroy_vec = [](vector<unique_ptr<Page>>& pages)
                {
                    for (unique_ptr<Page>& page : pages)
                    {
                        if (page && page->resource)
                        {
                            d3d12_state::RemoveState(page->resource);
                            GpuMemory::Unregister(page->resource);
                            page->resource->Release();
                        }
                    }
                    pages.clear();
                };
                destroy_vec(pages_uncompacted);
                destroy_vec(pages_compacted);
            }
        }

        // compaction postbuild query ring (uav -> persistently mapped readback)
        namespace compaction
        {
            constexpr uint32_t query_capacity   = 16384;
            constexpr uint32_t copies_per_frame = 256;

            mutex sync_mutex;
            ID3D12Resource* size_uav      = nullptr;
            ID3D12Resource* size_readback = nullptr;
            uint64_t* size_mapped         = nullptr;
            vector<uint32_t> free_queries;
            uint32_t next_query           = 0;

            uint64_t bytes_before = 0;
            uint64_t bytes_after  = 0;
            uint32_t compacted    = 0;
            uint32_t not_ready    = 0;
            uint32_t not_smaller  = 0;

            bool ensure_buffers()
            {
                if (size_uav && size_readback && size_mapped)
                {
                    return true;
                }

                const uint64_t buffer_bytes = static_cast<uint64_t>(query_capacity) * sizeof(uint64_t);
                if (!size_uav)
                {
                    size_uav = create_buffer(buffer_bytes, false, "blas_compaction_sizes_uav");
                }
                if (!size_readback)
                {
                    size_readback = create_readback_buffer(buffer_bytes, "blas_compaction_sizes_readback");
                }
                if (size_readback && !size_mapped)
                {
                    void* mapped = nullptr;
                    if (SUCCEEDED(size_readback->Map(0, nullptr, &mapped)) && mapped)
                    {
                        size_mapped = static_cast<uint64_t*>(mapped);
                    }
                }
                return size_uav && size_readback && size_mapped;
            }

            uint32_t acquire_query()
            {
                if (!ensure_buffers())
                {
                    return UINT32_MAX;
                }
                if (!free_queries.empty())
                {
                    const uint32_t q = free_queries.back();
                    free_queries.pop_back();
                    return q;
                }
                if (next_query < query_capacity)
                {
                    return next_query++;
                }
                return UINT32_MAX;
            }

            void destroy()
            {
                if (size_readback)
                {
                    if (size_mapped)
                    {
                        D3D12_RANGE written = { 0, 0 };
                        size_readback->Unmap(0, &written);
                        size_mapped = nullptr;
                    }
                    d3d12_state::RemoveState(size_readback);
                    GpuMemory::Unregister(size_readback);
                    size_readback->Release();
                    size_readback = nullptr;
                }
                if (size_uav)
                {
                    d3d12_state::RemoveState(size_uav);
                    GpuMemory::Unregister(size_uav);
                    size_uav->Release();
                    size_uav = nullptr;
                }
                free_queries.clear();
                next_query   = 0;
                bytes_before = 0;
                bytes_after  = 0;
                compacted    = 0;
                not_ready    = 0;
                not_smaller  = 0;
            }
        }
    }

    void* RHI_AccelerationStructure::s_blas_scratch_buffer         = nullptr;
    uint64_t RHI_AccelerationStructure::s_blas_scratch_buffer_size = 0;
    vector<RHI_AccelerationStructure*> RHI_AccelerationStructure::s_compaction_pending;
    uint64_t RHI_AccelerationStructure::s_blas_bytes            = 0;
    uint64_t RHI_AccelerationStructure::s_compaction_generation = 0;

    void RHI_AccelerationStructure::CancelCompaction()
    {
        // caller holds compaction::sync_mutex
        if (m_compaction_query == UINT32_MAX)
        {
            return;
        }

        compaction::free_queries.push_back(m_compaction_query);
        m_compaction_query = UINT32_MAX;
        m_compaction_ready = nullptr;
        erase(s_compaction_pending, this);
    }

    bool RHI_AccelerationStructure::CompactBottomLevels()
    {
        lock_guard<mutex> lock(compaction::sync_mutex);
        blas_pool::drain_deferred_frees(false);

        RHI_CommandList* cmd_list = RHI_Device::Cmd();
        if (s_compaction_pending.empty() || !cmd_list || !compaction::size_mapped)
        {
            return false;
        }

        ID3D12GraphicsCommandList7* cmd7 = static_cast<ID3D12GraphicsCommandList7*>(cmd_list->GetRhiResource());
        if (!cmd7)
        {
            return false;
        }

        uint32_t copies = 0;
        size_t retained = 0;
        for (size_t i = 0; i < s_compaction_pending.size(); i++)
        {
            RHI_AccelerationStructure* blas = s_compaction_pending[i];
            if (copies >= compaction::copies_per_frame || !blas->m_compaction_ready || !blas->m_compaction_ready->IsComplete())
            {
                s_compaction_pending[retained++] = blas;
                continue;
            }

            const uint64_t compacted_size = compaction::size_mapped[blas->m_compaction_query];
            compaction::free_queries.push_back(blas->m_compaction_query);
            blas->m_compaction_query = UINT32_MAX;
            blas->m_compaction_ready = nullptr;

            if (compacted_size == 0)
            {
                compaction::not_ready++;
                continue;
            }

            // not worth a copy when the driver cannot shrink it meaningfully
            if (compacted_size + compacted_size / 10 >= blas->m_size)
            {
                compaction::not_smaller++;
                continue;
            }

            auto slice_it = blas_pool::slices.find(blas);
            if (slice_it == blas_pool::slices.end() || blas->m_device_address == 0)
            {
                continue;
            }

            blas_pool::Slice new_slice = blas_pool::allocate(compacted_size, true);
            if (!new_slice.page)
            {
                continue;
            }

            const uint64_t dst_va = new_slice.page->gpu_va + new_slice.offset;
            const uint64_t src_va = blas->m_device_address;
            cmd7->CopyRaytracingAccelerationStructure(dst_va, src_va, D3D12_RAYTRACING_ACCELERATION_STRUCTURE_COPY_MODE_COMPACT);

            // the previous tlas and this frame's copy still read the old slice; defer return until the gpu retires both
            const blas_pool::Slice old_slice = slice_it->second;
            slice_it->second                 = new_slice;
            blas_pool::free_deferred(old_slice);

            compaction::bytes_before += blas->m_size;
            compaction::bytes_after  += new_slice.size;
            compaction::compacted++;
            s_blas_bytes -= min(s_blas_bytes, blas->m_size);
            s_blas_bytes += new_slice.size;

            blas->m_rhi_resource         = new_slice.page->resource;
            blas->m_rhi_resource_results = new_slice.page->resource;
            blas->m_size                 = new_slice.size;
            blas->m_device_address       = dst_va;
            copies++;
        }
        s_compaction_pending.resize(retained);

        if (s_compaction_pending.empty() && (compaction::compacted > 0 || compaction::not_ready > 0 || compaction::not_smaller > 0))
        {
            constexpr uint64_t log_threshold = 16ull * 1024 * 1024;
            if (compaction::bytes_before - compaction::bytes_after >= log_threshold || compaction::not_ready > 0)
            {
                SP_LOG_INFO("Ray tracing: compacted %u BLAS from %.1f MB to %.1f MB (%u unreadable, %u not worth it), resident BLAS %.1f MB",
                    compaction::compacted,
                    static_cast<double>(compaction::bytes_before) / (1024.0 * 1024.0),
                    static_cast<double>(compaction::bytes_after) / (1024.0 * 1024.0),
                    compaction::not_ready,
                    compaction::not_smaller,
                    static_cast<double>(s_blas_bytes) / (1024.0 * 1024.0));
            }
            compaction::bytes_before = 0;
            compaction::bytes_after  = 0;
            compaction::compacted    = 0;
            compaction::not_ready    = 0;
            compaction::not_smaller  = 0;
        }

        if (copies == 0)
        {
            return false;
        }

        // ensure compacted blas copies finish before tlas build and ray tracing read them
        as_memory_barrier(cmd7, nullptr);
        s_compaction_generation++;
        return true;
    }

    void RHI_AccelerationStructure::DestroyCompactionResources()
    {
        lock_guard<mutex> lock(compaction::sync_mutex);
        for (RHI_AccelerationStructure* blas : s_compaction_pending)
        {
            blas->m_compaction_query = UINT32_MAX;
            blas->m_compaction_ready = nullptr;
        }
        s_compaction_pending.clear();
        compaction::destroy();
        blas_pool::destroy_all();
        s_blas_bytes = 0;
    }

    void RHI_AccelerationStructure::FreeSharedBlasScratch()
    {
        if (s_blas_scratch_buffer)
        {
            RHI_Device::DeletionQueueAdd(RHI_Resource_Type::Buffer, s_blas_scratch_buffer);
            s_blas_scratch_buffer      = nullptr;
            s_blas_scratch_buffer_size = 0;
        }
    }

    RHI_AccelerationStructure::RHI_AccelerationStructure(const RHI_AccelerationStructureType type, const char* name)
    {
        m_type        = type;
        m_object_name = name ? name : "acceleration_structure";
    }

    RHI_AccelerationStructure::~RHI_AccelerationStructure()
    {
        Destroy();
    }

    void RHI_AccelerationStructure::Destroy()
    {
        if (m_type == RHI_AccelerationStructureType::Bottom)
        {
            lock_guard<mutex> lock(compaction::sync_mutex);
            CancelCompaction();
            auto it = blas_pool::slices.find(this);
            if (it != blas_pool::slices.end())
            {
                blas_pool::free_deferred(it->second);
                blas_pool::slices.erase(it);
            }
            s_blas_bytes -= min(s_blas_bytes, m_size);
            m_rhi_resource_results = nullptr;
            m_rhi_resource         = nullptr;
            m_device_address       = 0;
        }
        else
        {
            // tlas owns its committed result buffer directly
            if (m_rhi_resource_results)
            {
                RHI_Device::DeletionQueueAdd(RHI_Resource_Type::Buffer, m_rhi_resource_results);
                m_rhi_resource_results = nullptr;
            }
            m_rhi_resource   = nullptr;
            m_device_address = 0;
        }

        if (m_scratch_buffer)
        {
            RHI_Device::DeletionQueueAdd(RHI_Resource_Type::Buffer, m_scratch_buffer);
            m_scratch_buffer      = nullptr;
            m_scratch_buffer_size = 0;
        }

        for (uint32_t i = 0; i < buffer_count; i++)
        {
            if (m_instance_buffer[i])
            {
                RHI_Device::DeletionQueueAdd(RHI_Resource_Type::Buffer, m_instance_buffer[i]);
                m_instance_buffer[i]      = nullptr;
                m_instance_buffer_size[i] = 0;
            }

            if (m_staging_buffer[i])
            {
                RHI_Device::DeletionQueueAdd(RHI_Resource_Type::Buffer, m_staging_buffer[i]);
                m_staging_buffer[i]      = nullptr;
                m_staging_buffer_size[i] = 0;
            }
        }

        m_size                = 0;
        m_tlas_instance_count = 0;
        m_tlas_refit_count    = 0;
    }

    void RHI_AccelerationStructure::BuildBottomLevel(const vector<RHI_AccelerationStructureGeometry>& geometries, const vector<uint32_t>& primitive_counts, bool allow_update)
    {
        BuildBottomLevel(RHI_Device::Cmd(), geometries, primitive_counts, allow_update);
    }

    void RHI_AccelerationStructure::BuildBottomLevel(RHI_CommandList* cmd_list, const vector<RHI_AccelerationStructureGeometry>& geometries, const vector<uint32_t>& primitive_counts, bool allow_update)
    {
        SP_ASSERT(m_type == RHI_AccelerationStructureType::Bottom);
        SP_ASSERT(geometries.size() == primitive_counts.size());
        SP_ASSERT(!geometries.empty());

        Destroy();
        m_allow_update = allow_update;

        ID3D12GraphicsCommandList7* cmd7 = static_cast<ID3D12GraphicsCommandList7*>(cmd_list->GetRhiResource());
        if (!cmd7)
        {
            return;
        }

        // describe geometry
        vector<D3D12_RAYTRACING_GEOMETRY_DESC> d3d_geometries;
        d3d_geometries.reserve(geometries.size());
        for (size_t i = 0; i < geometries.size(); ++i)
        {
            const RHI_AccelerationStructureGeometry& geo = geometries[i];

            D3D12_RAYTRACING_GEOMETRY_DESC desc = {};
            desc.Type  = D3D12_RAYTRACING_GEOMETRY_TYPE_TRIANGLES;
            desc.Flags = geo.transparent ? D3D12_RAYTRACING_GEOMETRY_FLAG_NONE : D3D12_RAYTRACING_GEOMETRY_FLAG_OPAQUE;

            desc.Triangles.Transform3x4               = 0;
            desc.Triangles.IndexFormat                = format_to_dxgi(geo.index_format);
            desc.Triangles.VertexFormat               = format_to_dxgi(geo.vertex_format);
            desc.Triangles.IndexCount                 = primitive_counts[i] * 3;
            desc.Triangles.VertexCount                = geo.max_vertex + 1;
            desc.Triangles.IndexBuffer                = geo.index_buffer_address;
            desc.Triangles.VertexBuffer.StartAddress  = geo.vertex_buffer_address;
            desc.Triangles.VertexBuffer.StrideInBytes = geo.vertex_stride;

            d3d_geometries.emplace_back(desc);
        }

        // build inputs
        D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS inputs = {};
        inputs.Type           = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL;
        inputs.Flags          = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE
                              | D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_ALLOW_COMPACTION;
        if (allow_update)
        {
            inputs.Flags |= D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_ALLOW_UPDATE;
        }
        inputs.DescsLayout    = D3D12_ELEMENTS_LAYOUT_ARRAY;
        inputs.NumDescs       = static_cast<UINT>(d3d_geometries.size());
        inputs.pGeometryDescs = d3d_geometries.data();

        // prebuild info
        D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO prebuild = {};
        RHI_Context::device->GetRaytracingAccelerationStructurePrebuildInfo(&inputs, &prebuild);

        // sub-allocate 256-byte-aligned slice from the blas page pool
        blas_pool::Slice slice;
        uint32_t query_slot = UINT32_MAX;
        {
            lock_guard<mutex> lock(compaction::sync_mutex);
            slice = blas_pool::allocate(prebuild.ResultDataMaxSizeInBytes, false);
            if (!slice.page)
            {
                SP_LOG_WARNING("BLAS pool slice alloc failed (%llu bytes) for %s", prebuild.ResultDataMaxSizeInBytes, m_object_name.c_str());
                return;
            }
            blas_pool::slices[this] = slice;
            m_rhi_resource_results  = slice.page->resource;
            m_rhi_resource          = slice.page->resource;
            m_size                  = slice.size;
            m_device_address        = slice.page->gpu_va + slice.offset;
            s_blas_bytes           += m_size;

            query_slot = compaction::acquire_query();
        }

        // scratch buffer, static blas reuse a global shared scratch that grows monotonically
        const uint64_t alignment = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BYTE_ALIGNMENT;
        uint64_t scratch_size    = max(prebuild.ScratchDataSizeInBytes, prebuild.UpdateScratchDataSizeInBytes);
        scratch_size             = align_up(scratch_size, alignment);

        void** scratch_target         = allow_update ? &m_scratch_buffer      : &s_blas_scratch_buffer;
        uint64_t* scratch_size_target = allow_update ? &m_scratch_buffer_size : &s_blas_scratch_buffer_size;
        if (!*scratch_target || scratch_size > *scratch_size_target)
        {
            if (*scratch_target)
            {
                RHI_Device::DeletionQueueAdd(RHI_Resource_Type::Buffer, *scratch_target);
                *scratch_target = nullptr;
            }
            *scratch_target = create_buffer(scratch_size, false, (m_object_name + "_scratch").c_str());
            if (!*scratch_target)
            {
                SP_LOG_WARNING("BLAS scratch buffer alloc failed (%llu bytes) for %s", scratch_size, m_object_name.c_str());
                *scratch_size_target = 0;
                if (query_slot != UINT32_MAX)
                {
                    lock_guard<mutex> lock(compaction::sync_mutex);
                    compaction::free_queries.push_back(query_slot);
                }
                Destroy();
                return;
            }
            *scratch_size_target = scratch_size;
        }

        // record the build and emit compacted size postbuild info in the same call
        D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC build_desc = {};
        build_desc.Inputs                           = inputs;
        build_desc.DestAccelerationStructureData    = m_device_address;
        build_desc.SourceAccelerationStructureData  = 0;
        build_desc.ScratchAccelerationStructureData = static_cast<ID3D12Resource*>(*scratch_target)->GetGPUVirtualAddress();

        if (query_slot != UINT32_MAX)
        {
            D3D12_RAYTRACING_ACCELERATION_STRUCTURE_POSTBUILD_INFO_DESC postbuild_desc = {};
            postbuild_desc.DestBuffer = compaction::size_uav->GetGPUVirtualAddress() + static_cast<uint64_t>(query_slot) * sizeof(uint64_t);
            postbuild_desc.InfoType   = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_POSTBUILD_INFO_COMPACTED_SIZE;

            cmd7->BuildRaytracingAccelerationStructure(&build_desc, 1, &postbuild_desc);

            // batch AS build completion, scratch reuse, and postbuild size readback ordering into one Barrier() call
            D3D12_BUFFER_BARRIER barriers[3] = {};
            barriers[0].SyncBefore   = D3D12_BARRIER_SYNC_BUILD_RAYTRACING_ACCELERATION_STRUCTURE;
            barriers[0].SyncAfter    = D3D12_BARRIER_SYNC_COPY;
            barriers[0].AccessBefore = D3D12_BARRIER_ACCESS_UNORDERED_ACCESS;
            barriers[0].AccessAfter  = D3D12_BARRIER_ACCESS_COPY_SOURCE;
            barriers[0].pResource    = compaction::size_uav;
            barriers[0].Size         = UINT64_MAX;

            barriers[1].SyncBefore   = D3D12_BARRIER_SYNC_BUILD_RAYTRACING_ACCELERATION_STRUCTURE;
            barriers[1].SyncAfter    = D3D12_BARRIER_SYNC_BUILD_RAYTRACING_ACCELERATION_STRUCTURE | D3D12_BARRIER_SYNC_COPY_RAYTRACING_ACCELERATION_STRUCTURE | D3D12_BARRIER_SYNC_RAYTRACING | D3D12_BARRIER_SYNC_COMPUTE_SHADING;
            barriers[1].AccessBefore = D3D12_BARRIER_ACCESS_RAYTRACING_ACCELERATION_STRUCTURE_WRITE;
            barriers[1].AccessAfter  = D3D12_BARRIER_ACCESS_RAYTRACING_ACCELERATION_STRUCTURE_READ | D3D12_BARRIER_ACCESS_RAYTRACING_ACCELERATION_STRUCTURE_WRITE;
            barriers[1].pResource    = static_cast<ID3D12Resource*>(m_rhi_resource_results);
            barriers[1].Size         = UINT64_MAX;

            barriers[2].SyncBefore   = D3D12_BARRIER_SYNC_BUILD_RAYTRACING_ACCELERATION_STRUCTURE;
            barriers[2].SyncAfter    = D3D12_BARRIER_SYNC_BUILD_RAYTRACING_ACCELERATION_STRUCTURE;
            barriers[2].AccessBefore = D3D12_BARRIER_ACCESS_UNORDERED_ACCESS;
            barriers[2].AccessAfter  = D3D12_BARRIER_ACCESS_UNORDERED_ACCESS;
            barriers[2].pResource    = static_cast<ID3D12Resource*>(*scratch_target);
            barriers[2].Size         = UINT64_MAX;

            d3d12_barriers::Submit(cmd7, nullptr, 0, barriers, 3, nullptr, 0, cmd7);

            cmd7->CopyBufferRegion(
                compaction::size_readback, static_cast<uint64_t>(query_slot) * sizeof(uint64_t),
                compaction::size_uav,      static_cast<uint64_t>(query_slot) * sizeof(uint64_t),
                sizeof(uint64_t)
            );

            lock_guard<mutex> lock(compaction::sync_mutex);
            m_compaction_query = query_slot;
            m_compaction_ready = RHI_CommandList::CapturePendingWork();
            s_compaction_pending.push_back(this);
        }
        else
        {
            cmd7->BuildRaytracingAccelerationStructure(&build_desc, 0, nullptr);

            // batch AS build completion and scratch reuse into one Barrier() call
            D3D12_BUFFER_BARRIER barriers[2] = {};
            barriers[0].SyncBefore   = D3D12_BARRIER_SYNC_BUILD_RAYTRACING_ACCELERATION_STRUCTURE;
            barriers[0].SyncAfter    = D3D12_BARRIER_SYNC_BUILD_RAYTRACING_ACCELERATION_STRUCTURE | D3D12_BARRIER_SYNC_COPY_RAYTRACING_ACCELERATION_STRUCTURE | D3D12_BARRIER_SYNC_RAYTRACING | D3D12_BARRIER_SYNC_COMPUTE_SHADING;
            barriers[0].AccessBefore = D3D12_BARRIER_ACCESS_RAYTRACING_ACCELERATION_STRUCTURE_WRITE;
            barriers[0].AccessAfter  = D3D12_BARRIER_ACCESS_RAYTRACING_ACCELERATION_STRUCTURE_READ | D3D12_BARRIER_ACCESS_RAYTRACING_ACCELERATION_STRUCTURE_WRITE;
            barriers[0].pResource    = static_cast<ID3D12Resource*>(m_rhi_resource_results);
            barriers[0].Size         = UINT64_MAX;

            barriers[1].SyncBefore   = D3D12_BARRIER_SYNC_BUILD_RAYTRACING_ACCELERATION_STRUCTURE;
            barriers[1].SyncAfter    = D3D12_BARRIER_SYNC_BUILD_RAYTRACING_ACCELERATION_STRUCTURE;
            barriers[1].AccessBefore = D3D12_BARRIER_ACCESS_UNORDERED_ACCESS;
            barriers[1].AccessAfter  = D3D12_BARRIER_ACCESS_UNORDERED_ACCESS;
            barriers[1].pResource    = static_cast<ID3D12Resource*>(*scratch_target);
            barriers[1].Size         = UINT64_MAX;

            d3d12_barriers::Submit(cmd7, nullptr, 0, barriers, 2, nullptr, 0, cmd7);
        }

        if (!allow_update)
        {
            // static blas share the global scratch, do not retain a per-instance pointer
            m_scratch_buffer      = nullptr;
            m_scratch_buffer_size = 0;
        }
    }

    void RHI_AccelerationStructure::RefitBottomLevel(const vector<RHI_AccelerationStructureGeometry>& geometries, const vector<uint32_t>& primitive_counts)
    {
        RefitBottomLevel(RHI_Device::Cmd(), geometries, primitive_counts);
    }

    void RHI_AccelerationStructure::RefitBottomLevel(RHI_CommandList* cmd_list, const vector<RHI_AccelerationStructureGeometry>& geometries, const vector<uint32_t>& primitive_counts)
    {
        SP_ASSERT(m_type == RHI_AccelerationStructureType::Bottom);
        SP_ASSERT(m_allow_update && m_rhi_resource && m_scratch_buffer && m_device_address != 0);
        SP_ASSERT(geometries.size() == primitive_counts.size());
        SP_ASSERT(!geometries.empty());

        ID3D12GraphicsCommandList7* cmd7 = static_cast<ID3D12GraphicsCommandList7*>(cmd_list->GetRhiResource());
        if (!cmd7)
        {
            return;
        }

        vector<D3D12_RAYTRACING_GEOMETRY_DESC> d3d_geometries;
        d3d_geometries.reserve(geometries.size());
        for (size_t i = 0; i < geometries.size(); ++i)
        {
            const RHI_AccelerationStructureGeometry& geo = geometries[i];

            D3D12_RAYTRACING_GEOMETRY_DESC desc = {};
            desc.Type  = D3D12_RAYTRACING_GEOMETRY_TYPE_TRIANGLES;
            desc.Flags = geo.transparent ? D3D12_RAYTRACING_GEOMETRY_FLAG_NONE : D3D12_RAYTRACING_GEOMETRY_FLAG_OPAQUE;

            desc.Triangles.Transform3x4               = 0;
            desc.Triangles.IndexFormat                = format_to_dxgi(geo.index_format);
            desc.Triangles.VertexFormat               = format_to_dxgi(geo.vertex_format);
            desc.Triangles.IndexCount                 = primitive_counts[i] * 3;
            desc.Triangles.VertexCount                = geo.max_vertex + 1;
            desc.Triangles.IndexBuffer                = geo.index_buffer_address;
            desc.Triangles.VertexBuffer.StartAddress  = geo.vertex_buffer_address;
            desc.Triangles.VertexBuffer.StrideInBytes = geo.vertex_stride;

            d3d_geometries.emplace_back(desc);
        }

        D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS inputs = {};
        inputs.Type           = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL;
        inputs.Flags          = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE
                              | D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_ALLOW_UPDATE
                              | D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PERFORM_UPDATE;
        inputs.DescsLayout    = D3D12_ELEMENTS_LAYOUT_ARRAY;
        inputs.NumDescs       = static_cast<UINT>(d3d_geometries.size());
        inputs.pGeometryDescs = d3d_geometries.data();

        D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC build_desc = {};
        build_desc.Inputs                           = inputs;
        build_desc.DestAccelerationStructureData    = m_device_address;
        build_desc.SourceAccelerationStructureData  = m_device_address;
        build_desc.ScratchAccelerationStructureData = static_cast<ID3D12Resource*>(m_scratch_buffer)->GetGPUVirtualAddress();

        cmd7->BuildRaytracingAccelerationStructure(&build_desc, 0, nullptr);

        as_memory_barrier(cmd7, static_cast<ID3D12Resource*>(m_rhi_resource_results));
    }

    void RHI_AccelerationStructure::BuildTopLevel(const vector<RHI_AccelerationStructureInstance>& instances)
    {
        BuildTopLevel(RHI_Device::Cmd(), instances);
    }

    void RHI_AccelerationStructure::BuildTopLevel(RHI_CommandList* cmd_list, const vector<RHI_AccelerationStructureInstance>& instances)
    {
        SP_ASSERT(m_type == RHI_AccelerationStructureType::Top);
        SP_ASSERT(!instances.empty());

        ID3D12GraphicsCommandList7* cmd7 = static_cast<ID3D12GraphicsCommandList7*>(cmd_list->GetRhiResource());
        if (!cmd7)
        {
            return;
        }

        // one instance descriptor upload per frame in flight so gpu reads never race later cpu writes
        uint32_t buf_idx = m_buffer_index;
        m_buffer_index   = (m_buffer_index + 1) % buffer_count;

        // pack instance descriptors
        static vector<D3D12_RAYTRACING_INSTANCE_DESC> d3d_instances;
        d3d_instances.resize(instances.size());
        for (size_t i = 0; i < instances.size(); ++i)
        {
            const RHI_AccelerationStructureInstance& src = instances[i];
            D3D12_RAYTRACING_INSTANCE_DESC& dst          = d3d_instances[i];

            // transform is row-major 3x4
            memcpy(dst.Transform, src.transform.data(), sizeof(float) * 12);
            dst.InstanceID                          = src.instance_custom_index & 0xFFFFFF;
            dst.InstanceMask                        = src.mask & 0xFF;
            dst.InstanceContributionToHitGroupIndex = src.instance_shader_binding_table_record_offset & 0xFFFFFF;
            dst.Flags                               = src.flags;
            dst.AccelerationStructure               = src.device_address;
        }

        // size for the next power of two instance count so small fluctuations do not reallocate the tlas
        const uint32_t primitive_count = static_cast<uint32_t>(d3d_instances.size());
        uint32_t sized_count           = 64;
        while (sized_count < primitive_count && sized_count < (1u << 24))
        {
            sized_count <<= 1;
        }
        sized_count = max(sized_count, primitive_count);

        // staging upload buffer for the instance descriptors
        const size_t data_size     = sizeof(D3D12_RAYTRACING_INSTANCE_DESC) * d3d_instances.size();
        const size_t capacity_size = sizeof(D3D12_RAYTRACING_INSTANCE_DESC) * sized_count;
        if (!m_staging_buffer[buf_idx] || data_size > m_staging_buffer_size[buf_idx])
        {
            if (m_staging_buffer[buf_idx])
            {
                RHI_Device::DeletionQueueAdd(RHI_Resource_Type::Buffer, m_staging_buffer[buf_idx]);
            }

            m_staging_buffer[buf_idx]      = create_upload_buffer(capacity_size, (m_object_name + "_staging_" + to_string(buf_idx)).c_str());
            m_staging_buffer_size[buf_idx] = m_staging_buffer[buf_idx] ? capacity_size : 0;
        }

        // abort if the instance descriptors could not be staged, building with a null InstanceDescs address would
        // produce a tlas with no instances and trip the runtime, the previous frame's tlas stays valid instead
        if (!m_staging_buffer[buf_idx])
        {
            SP_LOG_WARNING("TLAS instance staging buffer alloc failed (%llu bytes) for %s", static_cast<uint64_t>(capacity_size), m_object_name.c_str());
            return;
        }

        {
            ID3D12Resource* staging = static_cast<ID3D12Resource*>(m_staging_buffer[buf_idx]);
            void* mapped            = nullptr;
            D3D12_RANGE read_range  = { 0, 0 };
            if (FAILED(staging->Map(0, &read_range, &mapped)) || !mapped)
            {
                SP_LOG_WARNING("TLAS instance staging buffer map failed for %s", m_object_name.c_str());
                return;
            }

            memcpy(mapped, d3d_instances.data(), data_size);
            staging->Unmap(0, nullptr);
        }

        // query build sizes using the power-of-two capacity
        D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS prebuild_inputs = {};
        prebuild_inputs.Type          = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL;
        prebuild_inputs.Flags         = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE
                                      | D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_ALLOW_UPDATE;
        prebuild_inputs.DescsLayout   = D3D12_ELEMENTS_LAYOUT_ARRAY;
        prebuild_inputs.NumDescs      = sized_count;
        prebuild_inputs.InstanceDescs = static_cast<ID3D12Resource*>(m_staging_buffer[buf_idx])->GetGPUVirtualAddress();

        D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO prebuild = {};
        RHI_Context::device->GetRaytracingAccelerationStructurePrebuildInfo(&prebuild_inputs, &prebuild);

        // grow the result buffer if needed
        if (!m_rhi_resource_results || prebuild.ResultDataMaxSizeInBytes > m_size)
        {
            m_tlas_instance_count = 0;
            m_tlas_refit_count    = 0;

            if (m_rhi_resource_results)
            {
                RHI_Device::DeletionQueueAdd(RHI_Resource_Type::Buffer, m_rhi_resource_results);
                m_rhi_resource_results = nullptr;
                m_rhi_resource         = nullptr;
                m_device_address       = 0;
            }

            m_rhi_resource_results = create_buffer(prebuild.ResultDataMaxSizeInBytes, true, m_object_name.c_str());
            if (!m_rhi_resource_results)
            {
                return;
            }

            m_rhi_resource   = m_rhi_resource_results;
            m_size           = prebuild.ResultDataMaxSizeInBytes;
            m_device_address = static_cast<ID3D12Resource*>(m_rhi_resource_results)->GetGPUVirtualAddress();
        }

        // grow scratch
        const uint64_t alignment       = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BYTE_ALIGNMENT;
        uint64_t required_scratch_size = max(prebuild.ScratchDataSizeInBytes, prebuild.UpdateScratchDataSizeInBytes);
        required_scratch_size          = align_up(required_scratch_size, alignment);
        if (!m_scratch_buffer || required_scratch_size > m_scratch_buffer_size)
        {
            if (m_scratch_buffer)
            {
                RHI_Device::DeletionQueueAdd(RHI_Resource_Type::Buffer, m_scratch_buffer);
            }

            m_scratch_buffer      = create_buffer(required_scratch_size, false, (m_object_name + "_scratch").c_str());
            m_scratch_buffer_size = m_scratch_buffer ? required_scratch_size : 0;
            if (!m_scratch_buffer)
            {
                return;
            }
        }

        // refit when instance count and blas compaction generation are unchanged, rebuild periodically to keep tree quality
        const bool refit        = m_tlas_instance_count == primitive_count && m_tlas_refit_count < 60 && m_compaction_generation == s_compaction_generation;
        m_compaction_generation = s_compaction_generation;
        m_tlas_instance_count   = primitive_count;
        m_tlas_refit_count      = refit ? m_tlas_refit_count + 1 : 0;

        D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS inputs = prebuild_inputs;
        inputs.NumDescs = primitive_count;
        if (refit)
        {
            inputs.Flags |= D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PERFORM_UPDATE;
        }

        D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC build_desc = {};
        build_desc.Inputs                           = inputs;
        build_desc.DestAccelerationStructureData    = m_device_address;
        build_desc.SourceAccelerationStructureData  = refit ? m_device_address : 0;
        build_desc.ScratchAccelerationStructureData = static_cast<ID3D12Resource*>(m_scratch_buffer)->GetGPUVirtualAddress();

        // dxr requires the scratch and dest addresses to meet the as scratch alignment, committed buffers start
        // at a 64kb-aligned va so this always holds, the assert guards against a future sub-allocated scratch path
        SP_ASSERT((build_desc.ScratchAccelerationStructureData % D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BYTE_ALIGNMENT) == 0);

        cmd7->BuildRaytracingAccelerationStructure(&build_desc, 0, nullptr);

        as_memory_barrier(cmd7, static_cast<ID3D12Resource*>(m_rhi_resource_results));
    }

    uint64_t RHI_AccelerationStructure::GetDeviceAddress()
    {
        if (m_device_address != 0)
        {
            return m_device_address;
        }

        if (!m_rhi_resource_results)
        {
            return 0;
        }

        return static_cast<ID3D12Resource*>(m_rhi_resource_results)->GetGPUVirtualAddress();
    }
}
