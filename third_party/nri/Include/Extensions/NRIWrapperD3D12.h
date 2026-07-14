// © 2021 NVIDIA Corporation

// Goal: wrapping native D3D12 objects into NRI objects

#pragma once

#define NRI_WRAPPER_D3D12_H 1

#include "NRIDeviceCreation.h"
#include "NRIRayTracing.h"

typedef int32_t DXGIFormat;

NonNriForwardStruct(AGSContext);
NonNriForwardStruct(ID3D12Heap);
NonNriForwardStruct(ID3D12Fence);
NonNriForwardStruct(ID3D12Device);
NonNriForwardStruct(ID3D12Resource);
NonNriForwardStruct(ID3D12CommandQueue);
NonNriForwardStruct(ID3D12DescriptorHeap);
NonNriForwardStruct(ID3D12CommandAllocator);
NonNriForwardStruct(ID3D12GraphicsCommandList);

NriNamespaceBegin

// A collection of queues of the same type
NriStruct(QueueFamilyD3D12Desc) {
    NriOptional ID3D12CommandQueue* const* d3d12Queues; // if not provided, will be created
    uint32_t queueNum;
    Nri(QueueType) queueType;
};

NriStruct(DeviceCreationD3D12Desc) {
    ID3D12Device* d3d12Device;
    const NriPtr(QueueFamilyD3D12Desc) queueFamilies;
    uint32_t queueFamilyNum;
    NriOptional AGSContext* agsContext;
    NriOptional Nri(CallbackInterface) callbackInterface;
    NriOptional Nri(AllocationCallbacks) allocationCallbacks;
    NriOptional uint32_t d3dShaderExtRegister;  // vendor specific shader extensions (default is "NRI_SHADER_EXT_REGISTER", space is always "0")
    NriOptional uint32_t d3dZeroBufferSize;     // no "memset" functionality in D3D, "CmdZeroBuffer" implemented via a bunch of copies (4 Mb by default)

    // Switches (disabled by default)
    bool enableNRIValidation;
    bool enableMemoryZeroInitialization;        // page-clears are fast, not enabled by default to match VK (the extension needed)

    // Switches (enabled by default)
    bool disableD3D12EnhancedBarriers;          // even if AgilitySDK is in use, some apps still use legacy barriers. It can be important for integrations
    bool disableNVAPIInitialization;            // at least NVAPI requires calling "NvAPI_Initialize" in DLL/EXE where the device is created
};

NriStruct(CommandBufferD3D12Desc) {
    ID3D12GraphicsCommandList* d3d12CommandList;
    NriOptional ID3D12CommandAllocator* d3d12CommandAllocator; // needed only for "BeginCommandBuffer"
};

NriStruct(DescriptorPoolD3D12Desc) {
    ID3D12DescriptorHeap* d3d12ResourceDescriptorHeap;
    ID3D12DescriptorHeap* d3d12SamplerDescriptorHeap;

    // Allocation limits (D3D12 unrelated, but must match expected usage)
    uint32_t descriptorSetMaxNum;
};

NriStruct(BufferD3D12Desc) {
    ID3D12Resource* d3d12Resource;
    NriOptional const NriPtr(BufferDesc) desc;  // not all information can be retrieved from the resource if not provided
    NriOptional uint32_t structureStride;       // must be provided if used as a structured or raw buffer
};

NriStruct(TextureD3D12Desc) {
    ID3D12Resource* d3d12Resource;
    NriOptional DXGIFormat format;              // must be provided "as a compatible typed format" if the resource is typeless
};

NriStruct(MemoryD3D12Desc) {
    ID3D12Heap* d3d12Heap;
    uint64_t offset;
};

NriStruct(FenceD3D12Desc) {
    ID3D12Fence* d3d12Fence;
};

NriStruct(AccelerationStructureD3D12Desc) {
    ID3D12Resource* d3d12Resource;
    Nri(AccelerationStructureBits) flags;

    // D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO
    uint64_t size;
    uint64_t buildScratchSize;
    uint64_t updateScratchSize;
};

// Threadsafe: yes
NriStruct(WrapperD3D12Interface) {
    Nri(Result) (NRI_CALL *CreateCommandBufferD3D12)            (NriRef(Device) device, const NriRef(CommandBufferD3D12Desc) commandBufferD3D12Desc, NriOut NriRef(CommandBuffer*) commandBuffer);
    Nri(Result) (NRI_CALL *CreateDescriptorPoolD3D12)           (NriRef(Device) device, const NriRef(DescriptorPoolD3D12Desc) descriptorPoolD3D12Desc, NriOut NriRef(DescriptorPool*) descriptorPool);
    Nri(Result) (NRI_CALL *CreateBufferD3D12)                   (NriRef(Device) device, const NriRef(BufferD3D12Desc) bufferD3D12Desc, NriOut NriRef(Buffer*) buffer);
    Nri(Result) (NRI_CALL *CreateTextureD3D12)                  (NriRef(Device) device, const NriRef(TextureD3D12Desc) textureD3D12Desc, NriOut NriRef(Texture*) texture);
    Nri(Result) (NRI_CALL *CreateMemoryD3D12)                   (NriRef(Device) device, const NriRef(MemoryD3D12Desc) memoryD3D12Desc, NriOut NriRef(Memory*) memory);
    Nri(Result) (NRI_CALL *CreateFenceD3D12)                    (NriRef(Device) device, const NriRef(FenceD3D12Desc) fenceD3D12Desc, NriOut NriRef(Fence*) fence);
    Nri(Result) (NRI_CALL *CreateAccelerationStructureD3D12)    (NriRef(Device) device, const NriRef(AccelerationStructureD3D12Desc) accelerationStructureD3D12Desc, NriOut NriRef(AccelerationStructure*) accelerationStructure);
};

NRI_API Nri(Result) NRI_CALL nriCreateDeviceFromD3D12Device(const NriRef(DeviceCreationD3D12Desc) deviceDesc, NriOut NriRef(Device*) device);

NriNamespaceEnd
