// © 2021 NVIDIA Corporation

// Goal: wrapping native VK objects into NRI objects

#pragma once

#define NRI_WRAPPER_VK_H 1

#include "NRIDeviceCreation.h"
#include "NRIRayTracing.h"

typedef void* VKHandle;
typedef int32_t VKEnum;
typedef uint32_t VKFlags;
typedef uint64_t VKNonDispatchableHandle;

NriNamespaceBegin

NriForwardStruct(AccelerationStructure);

// A collection of queues of the same type
NriStruct(QueueFamilyVKDesc) {
    uint32_t queueNum;
    Nri(QueueType) queueType;
    uint32_t familyIndex;
};

NriStruct(DeviceCreationVKDesc) {
    NriOptional Nri(CallbackInterface) callbackInterface;
    NriOptional Nri(AllocationCallbacks) allocationCallbacks;
    NriOptional const char* libraryPath;
    Nri(VKBindingOffsets) vkBindingOffsets;
    Nri(VKExtensions) vkExtensions;                     // enabled
    VKHandle vkInstance;
    VKHandle vkDevice;
    VKHandle vkPhysicalDevice;
    const NriPtr(QueueFamilyVKDesc) queueFamilies;
    uint32_t queueFamilyNum;
    uint8_t minorVersion;                               // >= 2

    // Switches (disabled by default)
    bool enableNRIValidation;
    bool enableMemoryZeroInitialization;                // page-clears are fast, but memory is not cleared by default in VK
};

NriStruct(CommandAllocatorVKDesc) {
    VKNonDispatchableHandle vkCommandPool;
    Nri(QueueType) queueType;
};

NriStruct(CommandBufferVKDesc) {
    VKHandle vkCommandBuffer;
    Nri(QueueType) queueType;
};

NriStruct(DescriptorPoolVKDesc) {
    VKNonDispatchableHandle vkDescriptorPool;
    uint32_t descriptorSetMaxNum;
};

NriStruct(BufferVKDesc) {
    VKNonDispatchableHandle vkBuffer;
    uint64_t size;
    NriOptional uint32_t structureStride;               // must be provided if used as a structured or raw buffer
    NriOptional uint8_t* mappedMemory;                  // must be provided if the underlying memory is mapped
    NriOptional VKNonDispatchableHandle vkDeviceMemory; // must be provided *only* if the mapped memory exists and *not* HOST_COHERENT
    NriOptional uint64_t deviceAddress;                 // must be provided for ray tracing
};

NriStruct(TextureVKDesc) {
    VKNonDispatchableHandle vkImage;
    VKEnum vkFormat;
    VKEnum vkImageType;
    VKFlags vkImageUsageFlags;
    Nri(Dim_t) width;
    Nri(Dim_t) height;
    Nri(Dim_t) depth;
    Nri(Dim_t) mipNum;
    Nri(Dim_t) layerNum;
    Nri(Sample_t) sampleNum;
};

NriStruct(MemoryVKDesc) {
    VKNonDispatchableHandle vkDeviceMemory;
    NriOptional uint64_t offset;
    NriOptional void* mappedMemory; // at "offset"
    uint64_t size;
    uint32_t memoryTypeIndex;
};

NriStruct(PipelineVKDesc) {
    VKNonDispatchableHandle vkPipeline;
    VKEnum vkPipelineBindPoint;
};

NriStruct(QueryPoolVKDesc) {
    VKNonDispatchableHandle vkQueryPool;
    VKEnum vkQueryType;
};

NriStruct(FenceVKDesc) {
    VKNonDispatchableHandle vkTimelineSemaphore;
};

NriStruct(AccelerationStructureVKDesc) {
    VKNonDispatchableHandle vkAccelerationStructure;
    VKNonDispatchableHandle vkBuffer;
    uint64_t bufferSize;
    uint64_t buildScratchSize;
    uint64_t updateScratchSize;
    Nri(AccelerationStructureBits) flags;
};

// Threadsafe: yes
NriStruct(WrapperVKInterface) {
    Nri(Result) (NRI_CALL *CreateCommandAllocatorVK)        (NriRef(Device) device, const NriRef(CommandAllocatorVKDesc) commandAllocatorVKDesc, NriOut NriRef(CommandAllocator*) commandAllocator);
    Nri(Result) (NRI_CALL *CreateCommandBufferVK)           (NriRef(Device) device, const NriRef(CommandBufferVKDesc) commandBufferVKDesc, NriOut NriRef(CommandBuffer*) commandBuffer);
    Nri(Result) (NRI_CALL *CreateDescriptorPoolVK)          (NriRef(Device) device, const NriRef(DescriptorPoolVKDesc) descriptorPoolVKDesc, NriOut NriRef(DescriptorPool*) descriptorPool);
    Nri(Result) (NRI_CALL *CreateBufferVK)                  (NriRef(Device) device, const NriRef(BufferVKDesc) bufferVKDesc, NriOut NriRef(Buffer*) buffer);
    Nri(Result) (NRI_CALL *CreateTextureVK)                 (NriRef(Device) device, const NriRef(TextureVKDesc) textureVKDesc, NriOut NriRef(Texture*) texture);
    Nri(Result) (NRI_CALL *CreateMemoryVK)                  (NriRef(Device) device, const NriRef(MemoryVKDesc) memoryVKDesc, NriOut NriRef(Memory*) memory);
    Nri(Result) (NRI_CALL *CreatePipelineVK)                (NriRef(Device) device, const NriRef(PipelineVKDesc) pipelineVKDesc, NriOut NriRef(Pipeline*) pipeline);
    Nri(Result) (NRI_CALL *CreateQueryPoolVK)               (NriRef(Device) device, const NriRef(QueryPoolVKDesc) queryPoolVKDesc, NriOut NriRef(QueryPool*) queryPool);
    Nri(Result) (NRI_CALL *CreateFenceVK)                   (NriRef(Device) device, const NriRef(FenceVKDesc) fenceVKDesc, NriOut NriRef(Fence*) fence);
    Nri(Result) (NRI_CALL *CreateAccelerationStructureVK)   (NriRef(Device) device, const NriRef(AccelerationStructureVKDesc) accelerationStructureVKDesc, NriOut NriRef(AccelerationStructure*) accelerationStructure);

    uint32_t    (NRI_CALL *GetQueueFamilyIndexVK)           (const NriRef(Queue) queue);
    VKHandle    (NRI_CALL *GetPhysicalDeviceVK)             (const NriRef(Device) device);
    VKHandle    (NRI_CALL *GetInstanceVK)                   (const NriRef(Device) device);
    void*       (NRI_CALL *GetInstanceProcAddrVK)           (const NriRef(Device) device);
    void*       (NRI_CALL *GetDeviceProcAddrVK)             (const NriRef(Device) device);
};

NRI_API Nri(Result) NRI_CALL nriCreateDeviceFromVKDevice(const NriRef(DeviceCreationVKDesc) deviceDesc, NriOut NriRef(Device*) device);

NriNamespaceEnd
