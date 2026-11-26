/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

#pragma once

#include <cstdint>
#include <d3d12.h>
#include "../RHI_Definitions.h"
#include "../RHI_SyncPrimitive.h"
#include "../../memory/GpuMemory.h"

namespace spartan
{
    enum class RHI_Queue_Type;
    class RHI_CommandList;
    class RHI_Texture;
}

// agility sdk redist, returns 0 when the build falls back to the in-box d3d12 runtime
namespace spartan::d3d12_agility
{
    uint32_t requested_sdk_version();
}

// d3d12 only capabilities, queried once during device creation
// features with a vulkan counterpart live on RHI_Device instead, these have no vulkan analogue
namespace spartan::d3d12_caps
{
    uint32_t                  GetLoadedSdkVersion();       // 0 when the in-box runtime is in use
    bool                      IsEnhancedBarriersSupported();
    bool                      IsGpuUploadHeapSupported();
    bool                      IsRelaxedFormatCastingSupported();
    D3D12_MESH_SHADER_TIER    GetMeshShaderTier();
    D3D12_RESOURCE_BINDING_TIER GetResourceBindingTier();
    D3D_ROOT_SIGNATURE_VERSION GetHighestRootSignatureVersion();
    D3D_SHADER_MODEL          GetHighestShaderModel();
    // compute when debug.d3d12_async_compute is on, direct otherwise, used for the queue, its allocators and its lists
    D3D12_COMMAND_LIST_TYPE   GetComputeQueueListType();
}

// enhanced barrier submission through ID3D12GraphicsCommandList7::Barrier
namespace spartan::d3d12_barriers
{
    void Initialize();
    bool IsSupported();

    D3D12_BARRIER_SYNC   LayoutDefaultSync(D3D12_BARRIER_LAYOUT layout);
    D3D12_BARRIER_ACCESS LayoutDefaultAccess(D3D12_BARRIER_LAYOUT layout);
    bool                 IsDirectOnlyLayout(D3D12_BARRIER_LAYOUT layout);

    void Submit(
        ID3D12GraphicsCommandList*   cmd_list,
        const D3D12_GLOBAL_BARRIER*  globals,
        uint32_t                     global_count,
        const D3D12_BUFFER_BARRIER*  buffers,
        uint32_t                     buffer_count,
        const D3D12_TEXTURE_BARRIER* textures,
        uint32_t                     texture_count,
        ID3D12GraphicsCommandList7*  cmd_list_7 = nullptr
    );

    void SubmitGlobal(
        ID3D12GraphicsCommandList* cmd_list,
        D3D12_BARRIER_SYNC         sync_before,
        D3D12_BARRIER_SYNC         sync_after,
        D3D12_BARRIER_ACCESS       access_before,
        D3D12_BARRIER_ACCESS       access_after
    );

    void SubmitBuffer(
        ID3D12GraphicsCommandList* cmd_list,
        ID3D12Resource*            resource,
        D3D12_BARRIER_SYNC         sync_before,
        D3D12_BARRIER_SYNC         sync_after,
        D3D12_BARRIER_ACCESS       access_before,
        D3D12_BARRIER_ACCESS       access_after
    );

    void SubmitTexture(
        ID3D12GraphicsCommandList* cmd_list,
        ID3D12Resource*            resource,
        D3D12_BARRIER_LAYOUT       layout_before,
        D3D12_BARRIER_LAYOUT       layout_after,
        UINT                       subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES
    );
}

// enhanced <-> legacy barrier interop for prebuilt third-party libraries (e.g. NRI inside NRD) that emit legacy ResourceBarrier
namespace spartan::d3d12_legacy_interop
{
    void PrepareCommonLayout(RHI_CommandList* cmd_list, RHI_Texture* texture);
    void ReturnToCommonLayout(RHI_CommandList* cmd_list, RHI_Texture* const* textures, const D3D12_RESOURCE_STATES* states_before, uint32_t count);
}

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
    struct BindlessTables { uint32_t textures; uint32_t samplers; uint64_t revision; };
    BindlessTables GetBindlessSnapshot(const RHI_Work& consumer);
    uint64_t GetBindlessRevision();
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
    void FreeRtv(void* handle);
    void FreeDsv(void* handle);
    uint32_t AllocateCbvSrvUavCpu();          // recyclable static views (texture/buffer init)
    void FreeCbvSrvUavCpu(void* handle);
    uint32_t AllocateCbvSrvUavCpuTransient(); // wraps inside a dedicated transient zone, for per-frame transient views
    uint32_t AllocateSamplerCpu();
    void     FreeSamplerCpu(uint32_t index);
    uint32_t SamplerHandleToIndex(SIZE_T handle_ptr);
    uint32_t AllocateRing(ID3D12GraphicsCommandList* owner, uint32_t count);
    void ReleaseRing(ID3D12GraphicsCommandList* owner); // only after the owner's fence completes

    // queue resources
    ID3D12CommandAllocator* GetGraphicsAllocator();
    ID3D12Fence*            GetGraphicsFence();
    uint64_t&               GetGraphicsFenceValue();
    ID3D12Fence*            GetQueueFence(RHI_Queue_Type type);
    uint64_t&               GetQueueFenceValue(RHI_Queue_Type type);
    HANDLE                  GetFenceEvent();
}

// root parameter slots for the unified bindless root signature
namespace spartan::d3d12_root_slot
{
    // buffers bound through Renderer_BindingsUav are declared as t registers in the stages that only read them, so the srv table must span the same index range as the uav table
    constexpr uint32_t srv_space0_count   = rhi_max_resource_slots;
    constexpr uint32_t uav_space0_count   = rhi_max_resource_slots;
    constexpr uint32_t cbv_frame          = 0;  // CBV b0 space0
    constexpr uint32_t push_constants     = 1;  // 32-bit root constants b1 space0
    constexpr uint32_t srv_table_space0   = 2;  // per-pass SRVs in space0
    constexpr uint32_t uav_table_space0   = 3;  // per-pass UAVs in space0
    constexpr uint32_t srv_material_tex   = 4;  // t15 space1 unbounded
    constexpr uint32_t srv_material_param = 5;  // t16 space2
    constexpr uint32_t srv_light_param    = 6;  // t17 space3
    constexpr uint32_t srv_aabb           = 7;  // t18 space4
    constexpr uint32_t srv_draw_data      = 8;  // t19 space5
    constexpr uint32_t srv_geometry       = 9;  // t20 space8 + t22 space9 + t23 space10
    constexpr uint32_t sampler_table      = 10; // s0 space6 + s1 space7
    constexpr uint32_t count              = 11;
}

// per-resource layout and metadata tracker for enhanced barriers
// textures track their D3D12_BARRIER_LAYOUT; buffers have no layout under enhanced barriers
namespace spartan::d3d12_state
{
    void SetLayout(ID3D12Resource* resource, D3D12_BARRIER_LAYOUT layout);
    D3D12_BARRIER_LAYOUT GetLayout(ID3D12Resource* resource);
    void SetIsBuffer(ID3D12Resource* resource, bool is_buffer);
    bool IsBuffer(ID3D12Resource* resource);
    void SetIsDepth(ID3D12Resource* resource, bool is_depth);
    // textures shared between graphics and compute; graphics submit hands them over out of direct-only layouts
    void SetCrossQueue(ID3D12Resource* resource);
    bool IsCrossQueue(ID3D12Resource* resource, bool* is_depth, bool* is_read_only = nullptr);
    void SetReadOnly(ID3D12Resource* resource, bool read_only);
    // created without allow_unordered_access, a uav layout transition is illegal and gets dropped
    void SetAllowsUav(ID3D12Resource* resource, bool allows_uav);
    bool AllowsUav(ID3D12Resource* resource);
    void SetSubresourceCount(ID3D12Resource* resource, uint32_t count);
    uint32_t GetSubresourceCount(ID3D12Resource* resource);
    void RemoveState(ID3D12Resource* resource);
}

// true when pipeline_cache_d3d12.bin was loaded, skip Load*Pipeline when false to avoid nameless cache spam
namespace spartan::d3d12_pipeline_library
{
    bool can_load();
}

// shader bytecode lifetime, the IDxcBlob backing a compiled shader is kept alive in a registry
// keyed by its buffer pointer so the deletion queue can release it when the shader is destroyed
namespace spartan::d3d12_shader
{
    void release_bytecode_blob(void* bytecode_ptr);
}

// reports committed resources to the api-agnostic gpu memory tracker
namespace spartan::d3d12_gpu_memory
{
    void register_resource(
        ID3D12Resource* resource,
        GpuMemoryKind kind,
        const char* name,
        const GpuMemoryDetail& detail = {}
    );
}
