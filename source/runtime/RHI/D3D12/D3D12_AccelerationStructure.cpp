/*
Copyright(c) 2015-2026 Panos Karabelas

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

//= INCLUDES =====================
#include "pch.h"
#include "../RHI_AccelerationStructure.h"
#include "../RHI_Device.h"
#include "../RHI_Implementation.h"
#include "../RHI_CommandList.h"
#include "D3D12_Internal.h"
#include <wrl/client.h>
//================================

//= NAMESPACES =====
using namespace std;
//==================

namespace spartan
{
    namespace
    {
        // create a default-heap committed buffer suitable for ray tracing structures
        // initial state can be RAYTRACING_ACCELERATION_STRUCTURE for as buffers, or UNORDERED_ACCESS for scratch
        ID3D12Resource* create_buffer(uint64_t size, D3D12_RESOURCE_STATES initial_state, const char* name)
        {
            D3D12_HEAP_PROPERTIES heap_props = {};
            heap_props.Type                  = D3D12_HEAP_TYPE_DEFAULT;
            heap_props.CPUPageProperty       = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
            heap_props.MemoryPoolPreference  = D3D12_MEMORY_POOL_UNKNOWN;
            heap_props.CreationNodeMask      = 1;
            heap_props.VisibleNodeMask       = 1;

            D3D12_RESOURCE_DESC desc = {};
            desc.Dimension          = D3D12_RESOURCE_DIMENSION_BUFFER;
            desc.Alignment          = 0;
            desc.Width              = size;
            desc.Height             = 1;
            desc.DepthOrArraySize   = 1;
            desc.MipLevels          = 1;
            desc.Format             = DXGI_FORMAT_UNKNOWN;
            desc.SampleDesc.Count   = 1;
            desc.SampleDesc.Quality = 0;
            desc.Layout             = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
            desc.Flags              = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

            // d3d12 forces buffers to be created in COMMON, except for the special
            // RAYTRACING_ACCELERATION_STRUCTURE state which must be set explicitly
            const bool is_as_state         = (initial_state == D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE);
            D3D12_RESOURCE_STATES creation = is_as_state ? initial_state : D3D12_RESOURCE_STATE_COMMON;

            ID3D12Resource* resource = nullptr;
            HRESULT hr = RHI_Context::device->CreateCommittedResource(
                &heap_props,
                D3D12_HEAP_FLAG_NONE,
                &desc,
                creation,
                nullptr,
                IID_PPV_ARGS(&resource)
            );

            if (FAILED(hr))
            {
                SP_LOG_ERROR("Failed to create raytracing buffer '%s' (%llu bytes)", name ? name : "?", size);
                return nullptr;
            }

            // track the logical initial state, buffers in COMMON auto-promote to UAV on first access
            d3d12_state::SetState(resource, initial_state);
            d3d12_state::SetDecaysToCommon(resource, true);
            d3d12_state::SetIsBuffer(resource, true);
            d3d12_state::SetSubresourceCount(resource, 1);
            if (name)
            {
                d3d12_utility::debug::set_name(resource, name);
            }

            return resource;
        }

        // create or grow an upload-heap buffer used to stage tlas instance descriptors
        ID3D12Resource* create_upload_buffer(uint64_t size, const char* name)
        {
            D3D12_HEAP_PROPERTIES heap_props = {};
            heap_props.Type                  = D3D12_HEAP_TYPE_UPLOAD;
            heap_props.CPUPageProperty       = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
            heap_props.MemoryPoolPreference  = D3D12_MEMORY_POOL_UNKNOWN;
            heap_props.CreationNodeMask      = 1;
            heap_props.VisibleNodeMask       = 1;

            D3D12_RESOURCE_DESC desc = {};
            desc.Dimension          = D3D12_RESOURCE_DIMENSION_BUFFER;
            desc.Alignment          = 0;
            desc.Width              = size;
            desc.Height             = 1;
            desc.DepthOrArraySize   = 1;
            desc.MipLevels          = 1;
            desc.Format             = DXGI_FORMAT_UNKNOWN;
            desc.SampleDesc.Count   = 1;
            desc.SampleDesc.Quality = 0;
            desc.Layout             = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
            desc.Flags              = D3D12_RESOURCE_FLAG_NONE;

            ID3D12Resource* resource = nullptr;
            HRESULT hr = RHI_Context::device->CreateCommittedResource(
                &heap_props,
                D3D12_HEAP_FLAG_NONE,
                &desc,
                D3D12_RESOURCE_STATE_GENERIC_READ,
                nullptr,
                IID_PPV_ARGS(&resource)
            );

            if (FAILED(hr))
            {
                SP_LOG_ERROR("Failed to create raytracing upload buffer '%s' (%llu bytes)", name ? name : "?", size);
                return nullptr;
            }

            d3d12_state::SetState(resource, D3D12_RESOURCE_STATE_GENERIC_READ);
            d3d12_state::SetDecaysToCommon(resource, true);
            d3d12_state::SetIsBuffer(resource, true);
            d3d12_state::SetSubresourceCount(resource, 1);
            if (name)
            {
                d3d12_utility::debug::set_name(resource, name);
            }

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

        // wrap a uav memory barrier so consecutive as builds writing the shared scratch are ordered
        void uav_barrier(ID3D12GraphicsCommandList* cmd_list, ID3D12Resource* resource)
        {
            D3D12_RESOURCE_BARRIER barrier = {};
            barrier.Type          = D3D12_RESOURCE_BARRIER_TYPE_UAV;
            barrier.UAV.pResource = resource;
            cmd_list->ResourceBarrier(1, &barrier);
        }
    }

    void* RHI_AccelerationStructure::s_blas_scratch_buffer         = nullptr;
    uint64_t RHI_AccelerationStructure::s_blas_scratch_buffer_size = 0;

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
        // on d3d12 the as is just a buffer, m_rhi_resource and m_rhi_resource_results alias the same buffer for blas/tlas
        if (m_rhi_resource_results)
        {
            RHI_Device::DeletionQueueAdd(RHI_Resource_Type::Buffer, m_rhi_resource_results);
            m_rhi_resource_results = nullptr;
        }

        // m_rhi_resource holds the same pointer as the result buffer, do not double-release
        m_rhi_resource = nullptr;

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

        m_size = 0;
    }

    void RHI_AccelerationStructure::BuildBottomLevel(RHI_CommandList* cmd_list, const vector<RHI_AccelerationStructureGeometry>& geometries, const vector<uint32_t>& primitive_counts, bool allow_update)
    {
        SP_ASSERT(m_type == RHI_AccelerationStructureType::Bottom);
        SP_ASSERT(geometries.size() == primitive_counts.size());
        SP_ASSERT(!geometries.empty());

        Destroy();
        m_allow_update = allow_update;

        // we need ID3D12Device5 and ID3D12GraphicsCommandList4 for dxr
        Microsoft::WRL::ComPtr<ID3D12Device5> device5;
        if (FAILED(RHI_Context::device->QueryInterface(IID_PPV_ARGS(&device5))))
        {
            SP_LOG_ERROR("BuildBottomLevel requires ID3D12Device5 (DXR)");
            return;
        }

        Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList4> cmd4;
        ID3D12GraphicsCommandList* base_cmd = static_cast<ID3D12GraphicsCommandList*>(cmd_list->GetRhiResource());
        if (FAILED(base_cmd->QueryInterface(IID_PPV_ARGS(&cmd4))))
        {
            SP_LOG_ERROR("BuildBottomLevel requires ID3D12GraphicsCommandList4 (DXR)");
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

            desc.Triangles.Transform3x4         = 0;
            desc.Triangles.IndexFormat          = format_to_dxgi(geo.index_format);
            desc.Triangles.VertexFormat         = format_to_dxgi(geo.vertex_format);
            desc.Triangles.IndexCount           = primitive_counts[i] * 3;
            desc.Triangles.VertexCount          = geo.max_vertex + 1;
            desc.Triangles.IndexBuffer          = geo.index_buffer_address;
            desc.Triangles.VertexBuffer.StartAddress  = geo.vertex_buffer_address;
            desc.Triangles.VertexBuffer.StrideInBytes = geo.vertex_stride;

            d3d_geometries.emplace_back(desc);
        }

        // build inputs
        D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS inputs = {};
        inputs.Type           = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL;
        inputs.Flags          = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE;
        if (allow_update)
        {
            inputs.Flags |= D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_ALLOW_UPDATE;
        }
        inputs.DescsLayout    = D3D12_ELEMENTS_LAYOUT_ARRAY;
        inputs.NumDescs       = static_cast<UINT>(d3d_geometries.size());
        inputs.pGeometryDescs = d3d_geometries.data();

        // prebuild info
        D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO prebuild = {};
        device5->GetRaytracingAccelerationStructurePrebuildInfo(&inputs, &prebuild);

        // result buffer holds the as data, the GPU virtual address of this buffer is the as reference
        m_rhi_resource_results = create_buffer(prebuild.ResultDataMaxSizeInBytes, D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE, m_object_name.c_str());
        if (!m_rhi_resource_results)
        {
            SP_LOG_WARNING("BLAS result buffer alloc failed (%llu bytes) for %s", prebuild.ResultDataMaxSizeInBytes, m_object_name.c_str());
            return;
        }
        m_rhi_resource = m_rhi_resource_results;
        m_size         = prebuild.ResultDataMaxSizeInBytes;

        // scratch buffer, static blas reuse a global shared scratch that grows monotonically
        const uint64_t alignment = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BYTE_ALIGNMENT;
        uint64_t scratch_size    = max(prebuild.ScratchDataSizeInBytes, prebuild.UpdateScratchDataSizeInBytes);
        scratch_size             = ((scratch_size + alignment - 1) & ~(alignment - 1));

        void** scratch_target         = allow_update ? &m_scratch_buffer        : &s_blas_scratch_buffer;
        uint64_t* scratch_size_target = allow_update ? &m_scratch_buffer_size   : &s_blas_scratch_buffer_size;
        if (!*scratch_target || scratch_size > *scratch_size_target)
        {
            if (*scratch_target)
            {
                RHI_Device::DeletionQueueAdd(RHI_Resource_Type::Buffer, *scratch_target);
                *scratch_target = nullptr;
            }
            *scratch_target = create_buffer(scratch_size, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, (m_object_name + "_scratch").c_str());
            if (!*scratch_target)
            {
                SP_LOG_WARNING("BLAS scratch buffer alloc failed (%llu bytes) for %s", scratch_size, m_object_name.c_str());
                *scratch_size_target = 0;
                return;
            }
            *scratch_size_target = scratch_size;
        }

        // record the build
        D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC build_desc = {};
        build_desc.Inputs                           = inputs;
        build_desc.DestAccelerationStructureData    = static_cast<ID3D12Resource*>(m_rhi_resource_results)->GetGPUVirtualAddress();
        build_desc.SourceAccelerationStructureData  = 0;
        build_desc.ScratchAccelerationStructureData = static_cast<ID3D12Resource*>(*scratch_target)->GetGPUVirtualAddress();

        cmd4->BuildRaytracingAccelerationStructure(&build_desc, 0, nullptr);

        // ensure as build finishes before subsequent reads or before the next blas reuses the shared scratch
        uav_barrier(base_cmd, static_cast<ID3D12Resource*>(m_rhi_resource_results));
        uav_barrier(base_cmd, static_cast<ID3D12Resource*>(*scratch_target));

        if (!allow_update)
        {
            // static blas share the global scratch, do not retain a per-instance pointer
            m_scratch_buffer      = nullptr;
            m_scratch_buffer_size = 0;
        }
    }

    void RHI_AccelerationStructure::RefitBottomLevel(RHI_CommandList* cmd_list, const vector<RHI_AccelerationStructureGeometry>& geometries, const vector<uint32_t>& primitive_counts)
    {
        SP_ASSERT(m_type == RHI_AccelerationStructureType::Bottom);
        SP_ASSERT(m_allow_update && m_rhi_resource && m_scratch_buffer);
        SP_ASSERT(geometries.size() == primitive_counts.size());
        SP_ASSERT(!geometries.empty());

        Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList4> cmd4;
        ID3D12GraphicsCommandList* base_cmd = static_cast<ID3D12GraphicsCommandList*>(cmd_list->GetRhiResource());
        if (FAILED(base_cmd->QueryInterface(IID_PPV_ARGS(&cmd4))))
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
        build_desc.DestAccelerationStructureData    = static_cast<ID3D12Resource*>(m_rhi_resource_results)->GetGPUVirtualAddress();
        build_desc.SourceAccelerationStructureData  = static_cast<ID3D12Resource*>(m_rhi_resource_results)->GetGPUVirtualAddress();
        build_desc.ScratchAccelerationStructureData = static_cast<ID3D12Resource*>(m_scratch_buffer)->GetGPUVirtualAddress();

        cmd4->BuildRaytracingAccelerationStructure(&build_desc, 0, nullptr);

        uav_barrier(base_cmd, static_cast<ID3D12Resource*>(m_rhi_resource_results));
    }

    void RHI_AccelerationStructure::BuildTopLevel(RHI_CommandList* cmd_list, const vector<RHI_AccelerationStructureInstance>& instances)
    {
        SP_ASSERT(m_type == RHI_AccelerationStructureType::Top);
        SP_ASSERT(!instances.empty());

        Microsoft::WRL::ComPtr<ID3D12Device5> device5;
        if (FAILED(RHI_Context::device->QueryInterface(IID_PPV_ARGS(&device5))))
        {
            return;
        }

        Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList4> cmd4;
        ID3D12GraphicsCommandList* base_cmd = static_cast<ID3D12GraphicsCommandList*>(cmd_list->GetRhiResource());
        if (FAILED(base_cmd->QueryInterface(IID_PPV_ARGS(&cmd4))))
        {
            return;
        }

        // double buffer the instance descriptor uploads so frame N gpu reads do not race frame N+1 cpu writes
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

        // staging upload buffer for the instance descriptors
        const size_t data_size = sizeof(D3D12_RAYTRACING_INSTANCE_DESC) * d3d_instances.size();
        if (!m_staging_buffer[buf_idx] || data_size > m_staging_buffer_size[buf_idx])
        {
            if (m_staging_buffer[buf_idx])
            {
                RHI_Device::DeletionQueueAdd(RHI_Resource_Type::Buffer, m_staging_buffer[buf_idx]);
            }

            m_staging_buffer[buf_idx]      = create_upload_buffer(data_size, (m_object_name + "_staging_" + to_string(buf_idx)).c_str());
            m_staging_buffer_size[buf_idx] = m_staging_buffer[buf_idx] ? data_size : 0;
        }

        // abort if the instance descriptors could not be staged, building with a null InstanceDescs address would
        // produce a tlas with no instances and trip the runtime, the previous frame's tlas stays valid instead
        if (!m_staging_buffer[buf_idx])
        {
            SP_LOG_WARNING("TLAS instance staging buffer alloc failed (%llu bytes) for %s", static_cast<uint64_t>(data_size), m_object_name.c_str());
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

        // input descs reference the staging buffer directly, dxr supports gpu virtual addresses on upload heaps
        D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS inputs = {};
        inputs.Type           = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL;
        inputs.Flags          = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE
                              | D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_ALLOW_UPDATE;
        inputs.DescsLayout    = D3D12_ELEMENTS_LAYOUT_ARRAY;
        inputs.NumDescs       = static_cast<UINT>(d3d_instances.size());
        inputs.InstanceDescs  = static_cast<ID3D12Resource*>(m_staging_buffer[buf_idx])->GetGPUVirtualAddress();

        D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO prebuild = {};
        device5->GetRaytracingAccelerationStructurePrebuildInfo(&inputs, &prebuild);

        // grow the result buffer if needed
        if (!m_rhi_resource_results || prebuild.ResultDataMaxSizeInBytes > m_size)
        {
            if (m_rhi_resource_results)
            {
                RHI_Device::DeletionQueueAdd(RHI_Resource_Type::Buffer, m_rhi_resource_results);
                m_rhi_resource_results = nullptr;
                m_rhi_resource         = nullptr;
            }

            m_rhi_resource_results = create_buffer(prebuild.ResultDataMaxSizeInBytes, D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE, m_object_name.c_str());
            if (!m_rhi_resource_results)
            {
                return;
            }

            m_rhi_resource = m_rhi_resource_results;
            m_size         = prebuild.ResultDataMaxSizeInBytes;
        }

        // grow scratch
        const uint64_t alignment        = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BYTE_ALIGNMENT;
        uint64_t required_scratch_size  = max(prebuild.ScratchDataSizeInBytes, prebuild.UpdateScratchDataSizeInBytes);
        required_scratch_size           = ((required_scratch_size + alignment - 1) & ~(alignment - 1));
        if (!m_scratch_buffer || required_scratch_size > m_scratch_buffer_size)
        {
            if (m_scratch_buffer)
            {
                RHI_Device::DeletionQueueAdd(RHI_Resource_Type::Buffer, m_scratch_buffer);
            }

            m_scratch_buffer      = create_buffer(required_scratch_size, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, (m_object_name + "_scratch").c_str());
            m_scratch_buffer_size = m_scratch_buffer ? required_scratch_size : 0;
            if (!m_scratch_buffer)
            {
                return;
            }
        }

        D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC build_desc = {};
        build_desc.Inputs                           = inputs;
        build_desc.DestAccelerationStructureData    = static_cast<ID3D12Resource*>(m_rhi_resource_results)->GetGPUVirtualAddress();
        build_desc.SourceAccelerationStructureData  = 0;
        build_desc.ScratchAccelerationStructureData = static_cast<ID3D12Resource*>(m_scratch_buffer)->GetGPUVirtualAddress();

        // dxr requires the scratch and dest addresses to meet the as scratch alignment, committed buffers start
        // at a 64kb-aligned va so this always holds, the assert guards against a future sub-allocated scratch path
        SP_ASSERT((build_desc.ScratchAccelerationStructureData % D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BYTE_ALIGNMENT) == 0);

        cmd4->BuildRaytracingAccelerationStructure(&build_desc, 0, nullptr);

        uav_barrier(base_cmd, static_cast<ID3D12Resource*>(m_rhi_resource_results));
    }

    uint64_t RHI_AccelerationStructure::GetDeviceAddress()
    {
        if (!m_rhi_resource_results)
        {
            return 0;
        }

        // on d3d12 the as is just a buffer, the gpu virtual address of the result buffer is the reference
        return static_cast<ID3D12Resource*>(m_rhi_resource_results)->GetGPUVirtualAddress();
    }
}
