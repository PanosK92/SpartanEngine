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

#pragma once

#include <cstdint>
#include <d3d12.h>

// shared descriptor-heap + queue access used by the various D3D12_*.cpp files
namespace spartan::d3d12_descriptors
{
    // heaps
    ID3D12DescriptorHeap* GetRtvHeap();
    ID3D12DescriptorHeap* GetDsvHeap();
    ID3D12DescriptorHeap* GetCbvSrvUavHeap();
    ID3D12DescriptorHeap* GetSamplerHeap();
    ID3D12DescriptorHeap* GetCbvSrvUavHeapCpu();
    ID3D12DescriptorHeap* GetSamplerHeapCpu();

    // descriptor sizes
    uint32_t GetRtvDescriptorSize();
    uint32_t GetDsvDescriptorSize();
    uint32_t GetCbvSrvUavDescriptorSize();
    uint32_t GetSamplerDescriptorSize();

    // zone info
    uint32_t GetBindlessTexturesBase();
    uint32_t GetBindlessTexturesCount();
    uint32_t GetBindlessBuffersBase();
    uint32_t GetBindlessBuffersCount();
    uint32_t GetSamplersCompareBase();
    uint32_t GetSamplersCompareCount();
    uint32_t GetSamplersBase();
    uint32_t GetSamplersCount();

    // handles
    D3D12_CPU_DESCRIPTOR_HANDLE GetRtvHandle(uint32_t index);
    D3D12_CPU_DESCRIPTOR_HANDLE GetDsvHandle(uint32_t index);
    D3D12_CPU_DESCRIPTOR_HANDLE GetCbvSrvUavCpuHandle(uint32_t index);          // cpu-only staging heap
    D3D12_CPU_DESCRIPTOR_HANDLE GetSamplerCpuHandle(uint32_t index);            // cpu-only staging heap
    D3D12_CPU_DESCRIPTOR_HANDLE GetCbvSrvUavGpuVisibleCpuHandle(uint32_t index);// shader-visible heap (for writes)
    D3D12_GPU_DESCRIPTOR_HANDLE GetCbvSrvUavGpuHandle(uint32_t index);
    D3D12_CPU_DESCRIPTOR_HANDLE GetSamplerGpuVisibleCpuHandle(uint32_t index);
    D3D12_GPU_DESCRIPTOR_HANDLE GetSamplerGpuHandle(uint32_t index);

    // allocators
    uint32_t AllocateRtv();
    uint32_t AllocateDsv();
    uint32_t AllocateCbvSrvUavCpu();          // monotonic, for static views (texture/buffer init)
    uint32_t AllocateCbvSrvUavCpuTransient(); // wraps inside a dedicated transient zone, for per-frame transient views
    uint32_t AllocateSamplerCpu();
    void     FreeSamplerCpu(uint32_t index);
    uint32_t SamplerHandleToIndex(SIZE_T handle_ptr);
    uint32_t AllocateRing(uint32_t count); // index in shader-visible cbv/srv/uav heap

    // queue resources
    ID3D12CommandAllocator* GetGraphicsAllocator();
    ID3D12Fence*            GetGraphicsFence();
    uint64_t&               GetGraphicsFenceValue();
    HANDLE                  GetFenceEvent();
}

// root parameter slots for the unified bindless root signature
namespace spartan::d3d12_root_slot
{
    constexpr uint32_t cbv_frame          = 0;  // CBV b0 space0
    constexpr uint32_t push_constants     = 1;  // 32-bit root constants b1 space0
    constexpr uint32_t srv_table_space0   = 2;  // t0..t26 space0
    constexpr uint32_t uav_table_space0   = 3;  // u0..u44 space0
    constexpr uint32_t srv_material_tex   = 4;  // t15 space1 unbounded
    constexpr uint32_t srv_material_param = 5;  // t16 space2
    constexpr uint32_t srv_light_param    = 6;  // t17 space3
    constexpr uint32_t srv_aabb           = 7;  // t18 space4
    constexpr uint32_t srv_draw_data      = 8;  // t19 space5
    constexpr uint32_t srv_geometry       = 9;  // t20 space8 + t22 space9 + t23 space10
    constexpr uint32_t sampler_table      = 10; // s0 space6 + s1 space7
    constexpr uint32_t count              = 11;
}

// per-resource state tracker, used to compute the StateBefore field of d3d12 transition barriers
// resources are seeded by their owners (texture creation, swapchain acquire) and freed on destroy
namespace spartan::d3d12_state
{
    void SetState(ID3D12Resource* resource, D3D12_RESOURCE_STATES state);
    D3D12_RESOURCE_STATES GetState(ID3D12Resource* resource);
    void RemoveState(ID3D12Resource* resource);
}

// shader bytecode lifetime, the IDxcBlob backing a compiled shader is kept alive in a registry
// keyed by its buffer pointer so the deletion queue can release it when the shader is destroyed
namespace spartan::d3d12_shader
{
    void release_bytecode_blob(void* bytecode_ptr);
}
