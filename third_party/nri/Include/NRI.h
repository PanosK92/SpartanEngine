// © 2021 NVIDIA Corporation

/*
Overview:
 - Generalized common denominator for VK, D3D12 and D3D11
    - VK spec: https://registry.khronos.org/vulkan/specs/latest/html/vkspec.html
       - Best practices: https://developer.nvidia.com/blog/vulkan-dos-donts/
       - Feature support coverage: https://vulkan.gpuinfo.org/
    - D3D12 spec: https://microsoft.github.io/DirectX-Specs/
       - Feature support coverage: https://d3d12infodb.boolka.dev/
    - D3D11 spec: https://microsoft.github.io/DirectX-Specs/d3d/archive/D3D11_3_FunctionalSpec.htm

Goals:
 - a Graphics API, not RHI!
 - generalization and unification of D3D12 and VK
 - explicitness (providing access to low-level features of modern GAPIs)
 - quality-of-life and high-level extensions (e.g., streaming and upscaling)
 - low overhead
 - cross-platform and platform independence (AMD/INTEL friendly)
 - D3D11 support (as much as possible)

Non-goals:
 - exposing entities not existing in GAPIs
 - high-level (D3D11-like) abstraction
 - hidden management of any kind (except for some high-level extensions where it's desired)
 - automatic barriers (better handled in a higher-level abstraction)

Thread safety:
 - Threadsafe: yes - free-threaded access
 - Threadsafe: no  - external synchronization required, i.e. one thread at a time (additional restrictions can apply)
 - Threadsafe: ?   - unclear status

Implicit:
 - Create*         - thread safe
 - Destroy*        - not thread safe (because of VK)
 - Cmd*            - not thread safe
*/

#pragma once

#define NRI_VERSION 180
#define NRI_VERSION_DATE "23 June 2026"

// C/C++ compatible interface (auto-selection or via "NRI_FORCE_C" macro)
#include "NRIDescs.h"

// Can be used with "name", "nri::name" and "NriName"
#define NRI_INTERFACE(name) #name, sizeof(name)

NriNamespaceBegin

// Example: Result result = nriGetInterface(device, NRI_INTERFACE(CoreInterface), &coreInterface)
NRI_API Nri(Result) NRI_CALL nriGetInterface(const NriRef(Device) device, const char* interfaceName, size_t interfaceSize, void* interfacePtr);

// Annotations for profiling tools: host
// - Host annotations currently use NVTX (NVIDIA Nsight Systems)
// - Device (command buffer and queue) annotations use GAPI or PIX (if "WinPixEventRuntime.dll" is nearby)
// - Colorization requires PIX or NVTX
NRI_API void NRI_CALL nriBeginAnnotation(const char* name, uint32_t bgra);  // start a named range
NRI_API void NRI_CALL nriEndAnnotation();                                   // end the last opened range
NRI_API void NRI_CALL nriAnnotation(const char* name, uint32_t bgra);       // emit a named simultaneous event
NRI_API void NRI_CALL nriSetThreadName(const char* name);                   // assign a name to the current thread

// Threadsafe: yes
NriStruct(CoreInterface) {
    // Get
    const NriRef(DeviceDesc)    (NRI_CALL *GetDeviceDesc)           (const NriRef(Device) device);
    const NriRef(BufferDesc)    (NRI_CALL *GetBufferDesc)           (const NriRef(Buffer) buffer);
    const NriRef(TextureDesc)   (NRI_CALL *GetTextureDesc)          (const NriRef(Texture) texture);
    Nri(FormatSupportBits)      (NRI_CALL *GetFormatSupport)        (const NriRef(Device) device, Nri(Format) format);

    // Returns one of the pre-created queues (see "DeviceCreationDesc" or wrapper extensions)
    // Return codes: "UNSUPPORTED" (no queues of "queueType") or "INVALID_ARGUMENT" (if "queueIndex" is out of bounds).
    // Getting "COMPUTE" and/or "COPY" queues switches VK sharing mode to "VK_SHARING_MODE_CONCURRENT" for resources created without "queueExclusive" flag.
    // This approach is used to minimize number of "queue ownership transfers", but also adds a requirement to "get" all async queues BEFORE creation of
    // resources participating into multi-queue activities. Explicit use of "queueExclusive" removes any restrictions.
    Nri(Result)         (NRI_CALL *GetQueue)                        (NriRef(Device) device, Nri(QueueType) queueType, uint32_t queueIndex, NriOut NriRef(Queue*) queue);

    // Create (doesn't assume allocation of big chunks of memory on the device, but it happens for some entities implicitly)
    Nri(Result)         (NRI_CALL *CreateCommandAllocator)          (NriRef(Queue) queue, NriOut NriRef(CommandAllocator*) commandAllocator);
    Nri(Result)         (NRI_CALL *CreateCommandBuffer)             (NriRef(CommandAllocator) commandAllocator, NriOut NriRef(CommandBuffer*) commandBuffer);
    Nri(Result)         (NRI_CALL *CreateFence)                     (NriRef(Device) device, uint64_t initialValue, NriOut NriRef(Fence*) fence);
    Nri(Result)         (NRI_CALL *CreateDescriptorPool)            (NriRef(Device) device, const NriRef(DescriptorPoolDesc) descriptorPoolDesc, NriOut NriRef(DescriptorPool*) descriptorPool);
    Nri(Result)         (NRI_CALL *CreatePipelineLayout)            (NriRef(Device) device, const NriRef(PipelineLayoutDesc) pipelineLayoutDesc, NriOut NriRef(PipelineLayout*) pipelineLayout);
    Nri(Result)         (NRI_CALL *CreateGraphicsPipeline)          (NriRef(Device) device, const NriRef(GraphicsPipelineDesc) graphicsPipelineDesc, NriOut NriRef(Pipeline*) pipeline);
    Nri(Result)         (NRI_CALL *CreateComputePipeline)           (NriRef(Device) device, const NriRef(ComputePipelineDesc) computePipelineDesc, NriOut NriRef(Pipeline*) pipeline);
    Nri(Result)         (NRI_CALL *CreatePipelineCache)             (NriRef(Device) device, const NriRef(PipelineCacheDesc) pipelineCacheDesc, NriOut NriRef(PipelineCache*) pipelineCache); // "OUT_OF_DATE" is returned on stale data, try to start over with an empty cache
    Nri(Result)         (NRI_CALL *CreateQueryPool)                 (NriRef(Device) device, const NriRef(QueryPoolDesc) queryPoolDesc, NriOut NriRef(QueryPool*) queryPool);
    Nri(Result)         (NRI_CALL *CreateSampler)                   (NriRef(Device) device, const NriRef(SamplerDesc) samplerDesc, NriOut NriRef(Descriptor*) sampler);
    Nri(Result)         (NRI_CALL *CreateBufferView)                (const NriRef(BufferViewDesc) bufferViewDesc, NriOut NriRef(Descriptor*) bufferView);
    Nri(Result)         (NRI_CALL *CreateTextureView)               (const NriRef(TextureViewDesc) textureViewDesc, NriOut NriRef(Descriptor*) textureView);

    // Destroy
    void                (NRI_CALL *DestroyCommandAllocator)         (NriPtr(CommandAllocator) commandAllocator);
    void                (NRI_CALL *DestroyCommandBuffer)            (NriPtr(CommandBuffer) commandBuffer);
    void                (NRI_CALL *DestroyDescriptorPool)           (NriPtr(DescriptorPool) descriptorPool);
    void                (NRI_CALL *DestroyBuffer)                   (NriPtr(Buffer) buffer);
    void                (NRI_CALL *DestroyTexture)                  (NriPtr(Texture) texture);
    void                (NRI_CALL *DestroyDescriptor)               (NriPtr(Descriptor) descriptor);
    void                (NRI_CALL *DestroyPipelineLayout)           (NriPtr(PipelineLayout) pipelineLayout);
    void                (NRI_CALL *DestroyPipeline)                 (NriPtr(Pipeline) pipeline);
    void                (NRI_CALL *DestroyPipelineCache)            (NriPtr(PipelineCache) pipelineCache);
    void                (NRI_CALL *DestroyQueryPool)                (NriPtr(QueryPool) queryPool);
    void                (NRI_CALL *DestroyFence)                    (NriPtr(Fence) fence);

    // Memory
    Nri(Result)         (NRI_CALL *AllocateMemory)                  (NriRef(Device) device, const NriRef(AllocateMemoryDesc) allocateMemoryDesc, NriOut NriRef(Memory*) memory);
    void                (NRI_CALL *FreeMemory)                      (NriPtr(Memory) memory);

    // Resources and memory (VK style)
    // - create a resource (buffer or texture)
    // - use "Get[Resource]MemoryDesc" to get "MemoryDesc" ("usageBits" and "MemoryLocation" affect returned "MemoryType")
    // - (optional) group returned "MemoryDesc"s by "MemoryType", but don't group if "mustBeDedicated = true"
    // - (optional) sort returned "MemoryDesc"s by alignment
    // - call "AllocateMemory" (even if "mustBeDedicated = true")
    // - call "Bind[Resource]Memory" to bind resources to "Memory" objects
    // - (optional) "CalculateAllocationNumber" and "AllocateAndBindMemory" from "NRIHelper" interface simplify this process for buffers and textures
    Nri(Result)         (NRI_CALL *CreateBuffer)                    (NriRef(Device) device, const NriRef(BufferDesc) bufferDesc, NriOut NriRef(Buffer*) buffer);
    Nri(Result)         (NRI_CALL *CreateTexture)                   (NriRef(Device) device, const NriRef(TextureDesc) textureDesc, NriOut NriRef(Texture*) texture);
    void                (NRI_CALL *GetBufferMemoryDesc)             (const NriRef(Buffer) buffer, Nri(MemoryLocation) memoryLocation, NriOut NriRef(MemoryDesc) memoryDesc);
    void                (NRI_CALL *GetTextureMemoryDesc)            (const NriRef(Texture) texture, Nri(MemoryLocation) memoryLocation, NriOut NriRef(MemoryDesc) memoryDesc);
    Nri(Result)         (NRI_CALL *BindBufferMemory)                (const NriPtr(BindBufferMemoryDesc) bindBufferMemoryDescs, uint32_t bindBufferMemoryDescNum);
    Nri(Result)         (NRI_CALL *BindTextureMemory)               (const NriPtr(BindTextureMemoryDesc) bindTextureMemoryDescs, uint32_t bindTextureMemoryDescNum);

    // Resources and memory (D3D12 style)
    // - "Get[Resource]MemoryDesc2" requires "maintenance4" support on Vulkan
    // - "memory, offset" pair can be replaced with a "Nri[Device/DeviceUpload/HostUpload/HostReadback]Heap" macro to create a placed resource in the corresponding memory using VMA (AMD Virtual Memory Allocator) implicitly
    void                (NRI_CALL *GetBufferMemoryDesc2)            (const NriRef(Device) device, const NriRef(BufferDesc) bufferDesc, Nri(MemoryLocation) memoryLocation, NriOut NriRef(MemoryDesc) memoryDesc); // requires "features.getMemoryDesc2"
    void                (NRI_CALL *GetTextureMemoryDesc2)           (const NriRef(Device) device, const NriRef(TextureDesc) textureDesc, Nri(MemoryLocation) memoryLocation, NriOut NriRef(MemoryDesc) memoryDesc); // requires "features.getMemoryDesc2"
    Nri(Result)         (NRI_CALL *CreateCommittedBuffer)           (NriRef(Device) device, Nri(MemoryLocation) memoryLocation, float priority, const NriRef(BufferDesc) bufferDesc, NriOut NriRef(Buffer*) buffer);
    Nri(Result)         (NRI_CALL *CreateCommittedTexture)          (NriRef(Device) device, Nri(MemoryLocation) memoryLocation, float priority, const NriRef(TextureDesc) textureDesc, NriOut NriRef(Texture*) texture);
    Nri(Result)         (NRI_CALL *CreatePlacedBuffer)              (NriRef(Device) device, NriPtr(Memory) memory, uint64_t offset, const NriRef(BufferDesc) bufferDesc, NriOut NriRef(Buffer*) buffer);
    Nri(Result)         (NRI_CALL *CreatePlacedTexture)             (NriRef(Device) device, NriPtr(Memory) memory, uint64_t offset, const NriRef(TextureDesc) textureDesc, NriOut NriRef(Texture*) texture);

    // Descriptor set management (entities don't require destroying)
    // - if "ALLOW_UPDATE_AFTER_SET" not used, descriptor sets (and data pointed to by descriptors) must be updated before "CmdSetDescriptorSet"
    // - "ResetDescriptorPool" resets the entire pool and wipes out all allocated descriptor sets. "DescriptorSet" is a tiny struct (<= 48 bytes),
    //   so lots of descriptor sets can be created in advance and reused without calling "ResetDescriptorPool"
    // - if there is a directly indexed descriptor heap:
    //   - D3D12: "GetDescriptorSetOffsets" returns offsets in resource and sampler descriptor heaps
    //     - these offsets are needed in shaders, if the corresponding descriptor set is not the first allocated from the descriptor pool
    //   - VK: "GetDescriptorSetOffsets" returns "0"
    //     - use "-fvk-bind-resource-heap" and "-fvk-bind-sampler-heap" DXC options to define bindings mimicking corresponding heaps
    Nri(Result)         (NRI_CALL *AllocateDescriptorSets)          (NriRef(DescriptorPool) descriptorPool, const NriRef(PipelineLayout) pipelineLayout, uint32_t setIndex, NriOut NriPtr(DescriptorSet)* descriptorSets, uint32_t instanceNum, uint32_t variableDescriptorNum);
    void                (NRI_CALL *UpdateDescriptorRanges)          (const NriPtr(UpdateDescriptorRangeDesc) updateDescriptorRangeDescs, uint32_t updateDescriptorRangeDescNum);
    void                (NRI_CALL *CopyDescriptorRanges)            (const NriPtr(CopyDescriptorRangeDesc) copyDescriptorRangeDescs, uint32_t copyDescriptorRangeDescNum);
    void                (NRI_CALL *ResetDescriptorPool)             (NriRef(DescriptorPool) descriptorPool);
    void                (NRI_CALL *GetDescriptorSetOffsets)         (const NriRef(DescriptorSet) descriptorSet, NriOut NonNriRef(uint32_t) resourceHeapOffset, NriOut NonNriRef(uint32_t) samplerHeapOffset);

    // Command buffer (one time submit)
    Nri(Result)         (NRI_CALL *BeginCommandBuffer)              (NriRef(CommandBuffer) commandBuffer, const NriPtr(DescriptorPool) descriptorPool);
    // {                {
        // Set descriptor pool (initially can be set via "BeginCommandBuffer")
        void                (NRI_CALL *CmdSetDescriptorPool)        (NriRef(CommandBuffer) commandBuffer, const NriRef(DescriptorPool) descriptorPool);

        // Resource binding (expect "CmdSetPipelineLayout" to be called first)
        void                (NRI_CALL *CmdSetPipelineLayout)        (NriRef(CommandBuffer) commandBuffer, Nri(BindPoint) bindPoint, const NriRef(PipelineLayout) pipelineLayout);
        void                (NRI_CALL *CmdSetDescriptorSet)         (NriRef(CommandBuffer) commandBuffer, const NriRef(SetDescriptorSetDesc) setDescriptorSetDesc);
        void                (NRI_CALL *CmdSetRootConstants)         (NriRef(CommandBuffer) commandBuffer, const NriRef(SetRootConstantsDesc) setRootConstantsDesc);
        void                (NRI_CALL *CmdSetRootDescriptor)        (NriRef(CommandBuffer) commandBuffer, const NriRef(SetRootDescriptorDesc) setRootDescriptorDesc);

        // Pipeline
        void                (NRI_CALL *CmdSetPipeline)              (NriRef(CommandBuffer) commandBuffer, const NriRef(Pipeline) pipeline);

        // Barrier (outside of rendering)
        void                (NRI_CALL *CmdBarrier)                  (NriRef(CommandBuffer) commandBuffer, const NriRef(BarrierDesc) barrierDesc);

        // Input assembly
        void                (NRI_CALL *CmdSetIndexBuffer)           (NriRef(CommandBuffer) commandBuffer, const NriRef(Buffer) buffer, uint64_t offset, Nri(IndexType) indexType);
        void                (NRI_CALL *CmdSetVertexBuffers)         (NriRef(CommandBuffer) commandBuffer, uint32_t baseSlot, const NriPtr(VertexBufferDesc) vertexBufferDescs, uint32_t vertexBufferNum);

        // Initial state (mandatory)
        void                (NRI_CALL *CmdSetViewports)             (NriRef(CommandBuffer) commandBuffer, const NriPtr(Viewport) viewports, uint32_t viewportNum);
        void                (NRI_CALL *CmdSetScissors)              (NriRef(CommandBuffer) commandBuffer, const NriPtr(Rect) rects, uint32_t rectNum);

        // Initial state (if enabled)
        void                (NRI_CALL *CmdSetStencilReference)      (NriRef(CommandBuffer) commandBuffer, uint8_t frontRef, uint8_t backRef); // "backRef" requires "features.independentFrontAndBackStencilReferenceAndMasks"
        void                (NRI_CALL *CmdSetDepthBounds)           (NriRef(CommandBuffer) commandBuffer, float boundsMin, float boundsMax); // requires "features.depthBoundsTest"
        void                (NRI_CALL *CmdSetBlendConstants)        (NriRef(CommandBuffer) commandBuffer, const NriRef(Color32f) color);
        void                (NRI_CALL *CmdSetSampleLocations)       (NriRef(CommandBuffer) commandBuffer, const NriPtr(SampleLocation) locations, Nri(Sample_t) locationNum, Nri(Sample_t) sampleNum); // requires "tiers.sampleLocations != 0"
        void                (NRI_CALL *CmdSetShadingRate)           (NriRef(CommandBuffer) commandBuffer, const NriRef(ShadingRateDesc) shadingRateDesc); // requires "tiers.shadingRate != 0"
        void                (NRI_CALL *CmdSetDepthBias)             (NriRef(CommandBuffer) commandBuffer, const NriRef(DepthBiasDesc) depthBiasDesc); // requires "features.dynamicDepthBias", actually it's an override

        // Graphics
        void                (NRI_CALL *CmdBeginRendering)           (NriRef(CommandBuffer) commandBuffer, const NriRef(RenderingDesc) renderingDesc);
        // {                {
            // Clear
            void                (NRI_CALL *CmdClearAttachments)     (NriRef(CommandBuffer) commandBuffer, const NriPtr(ClearAttachmentDesc) clearAttachmentDescs, uint32_t clearAttachmentDescNum, const NriPtr(Rect) rects, uint32_t rectNum);

            // Draw
            void                (NRI_CALL *CmdDraw)                 (NriRef(CommandBuffer) commandBuffer, const NriRef(DrawDesc) drawDesc);
            void                (NRI_CALL *CmdDrawIndexed)          (NriRef(CommandBuffer) commandBuffer, const NriRef(DrawIndexedDesc) drawIndexedDesc);

            // Draw indirect:
            // - drawNum = min(drawNum, countBuffer ? countBuffer[countBufferOffset] : INF)
            // - see "Modified draw command signatures"
            void                (NRI_CALL *CmdDrawIndirect)         (NriRef(CommandBuffer) commandBuffer, const NriRef(Buffer) buffer, uint64_t offset, uint32_t drawNum, uint32_t stride, NriOptional const NriPtr(Buffer) countBuffer, uint64_t countBufferOffset); // "buffer" contains "Draw(Base)Desc" commands
            void                (NRI_CALL *CmdDrawIndexedIndirect)  (NriRef(CommandBuffer) commandBuffer, const NriRef(Buffer) buffer, uint64_t offset, uint32_t drawNum, uint32_t stride, NriOptional const NriPtr(Buffer) countBuffer, uint64_t countBufferOffset); // "buffer" contains "DrawIndexed(Base)Desc" commands
        // }                }
        void                (NRI_CALL *CmdEndRendering)             (NriRef(CommandBuffer) commandBuffer);

        // Compute (outside of rendering)
        void                (NRI_CALL *CmdDispatch)                 (NriRef(CommandBuffer) commandBuffer, const NriRef(DispatchDesc) dispatchDesc);
        void                (NRI_CALL *CmdDispatchIndirect)         (NriRef(CommandBuffer) commandBuffer, const NriRef(Buffer) buffer, uint64_t offset); // buffer contains "DispatchDesc" commands

        // Copy (outside of rendering)
        void                (NRI_CALL *CmdCopyBuffer)               (NriRef(CommandBuffer) commandBuffer, NriRef(Buffer) dstBuffer, uint64_t dstOffset, const NriRef(Buffer) srcBuffer, uint64_t srcOffset, uint64_t size);
        void                (NRI_CALL *CmdCopyTexture)              (NriRef(CommandBuffer) commandBuffer, NriRef(Texture) dstTexture, NriOptional const NriPtr(TextureRegionDesc) dstRegion, const NriRef(Texture) srcTexture, NriOptional const NriPtr(TextureRegionDesc) srcRegion);
        void                (NRI_CALL *CmdUploadBufferToTexture)    (NriRef(CommandBuffer) commandBuffer, NriRef(Texture) dstTexture, const NriRef(TextureRegionDesc) dstRegion, const NriRef(Buffer) srcBuffer, const NriRef(TextureDataLayoutDesc) srcDataLayout);
        void                (NRI_CALL *CmdReadbackTextureToBuffer)  (NriRef(CommandBuffer) commandBuffer, NriRef(Buffer) dstBuffer, const NriRef(TextureDataLayoutDesc) dstDataLayout, const NriRef(Texture) srcTexture, const NriRef(TextureRegionDesc) srcRegion);
        void                (NRI_CALL *CmdZeroBuffer)               (NriRef(CommandBuffer) commandBuffer, NriRef(Buffer) buffer, uint64_t offset, uint64_t size);

        // Resolve (outside of rendering)
        void                (NRI_CALL *CmdResolveTexture)           (NriRef(CommandBuffer) commandBuffer, NriRef(Texture) dstTexture, NriOptional const NriPtr(TextureRegionDesc) dstRegion, const NriRef(Texture) srcTexture, NriOptional const NriPtr(TextureRegionDesc) srcRegion, Nri(ResolveOp) resolveOp); // "features.regionResolve" is needed for region specification

        // Clear (outside of rendering)
        void                (NRI_CALL *CmdClearStorage)             (NriRef(CommandBuffer) commandBuffer, const NriRef(ClearStorageDesc) clearStorageDesc);

        // Query (outside of rendering, except Begin/End query)
        void                (NRI_CALL *CmdResetQueries)             (NriRef(CommandBuffer) commandBuffer, NriRef(QueryPool) queryPool, uint32_t offset, uint32_t num);
        void                (NRI_CALL *CmdBeginQuery)               (NriRef(CommandBuffer) commandBuffer, NriRef(QueryPool) queryPool, uint32_t offset);
        void                (NRI_CALL *CmdEndQuery)                 (NriRef(CommandBuffer) commandBuffer, NriRef(QueryPool) queryPool, uint32_t offset);
        void                (NRI_CALL *CmdCopyQueries)              (NriRef(CommandBuffer) commandBuffer, const NriRef(QueryPool) queryPool, uint32_t offset, uint32_t num, NriRef(Buffer) dstBuffer, uint64_t dstOffset);

        // Annotations for profiling tools: command buffer
        void                (NRI_CALL *CmdBeginAnnotation)          (NriRef(CommandBuffer) commandBuffer, const char* name, uint32_t bgra);
        void                (NRI_CALL *CmdEndAnnotation)            (NriRef(CommandBuffer) commandBuffer);
        void                (NRI_CALL *CmdAnnotation)               (NriRef(CommandBuffer) commandBuffer, const char* name, uint32_t bgra);
    // }                }
    Nri(Result)         (NRI_CALL *EndCommandBuffer)                (NriRef(CommandBuffer) commandBuffer); // D3D11 performs state tracking and resets it there

    // Annotations for profiling tools: command queue - D3D11: NOP
    void                (NRI_CALL *QueueBeginAnnotation)            (NriRef(Queue) queue, const char* name, uint32_t bgra);
    void                (NRI_CALL *QueueEndAnnotation)              (NriRef(Queue) queue);
    void                (NRI_CALL *QueueAnnotation)                 (NriRef(Queue) queue, const char* name, uint32_t bgra);

    // Queries and timestamps
    void                (NRI_CALL *ResetQueries)                    (NriRef(QueryPool) queryPool, uint32_t offset, uint32_t num); // on host
    uint32_t            (NRI_CALL *GetQuerySize)                    (const NriRef(QueryPool) queryPool);
    void                (NRI_CALL *GetCalibratedTimestamps)         (NriRef(Queue) queue, NonNriRef(uint64_t) timestampGPU, NonNriRef(uint64_t) timestampCPU);

    // Work submission and synchronization
    Nri(Result)         (NRI_CALL *QueueSubmit)                     (NriRef(Queue) queue, const NriRef(QueueSubmitDesc) queueSubmitDesc); // to device
    Nri(Result)         (NRI_CALL *QueueWaitIdle)                   (NriPtr(Queue) queue);
    Nri(Result)         (NRI_CALL *DeviceWaitIdle)                  (NriPtr(Device) device);
    void                (NRI_CALL *Wait)                            (NriRef(Fence) fence, uint64_t value); // on host
    uint64_t            (NRI_CALL *GetFenceValue)                   (NriRef(Fence) fence);

    // Command allocator
    void                (NRI_CALL *ResetCommandAllocator)           (NriRef(CommandAllocator) commandAllocator);

    // Host address
    // D3D11: no persistent mapping
    // D3D12: persistent mapping, "Map/Unmap" do nothing
    // VK: persistent mapping, but "Unmap" can do a flush if underlying memory is not "HOST_COHERENT" (unlikely)
    void*               (NRI_CALL *MapBuffer)                       (NriRef(Buffer) buffer, uint64_t offset, uint64_t size);
    void                (NRI_CALL *UnmapBuffer)                     (NriRef(Buffer) buffer);

    // Device address (aka GPU virtual address)
    // D3D11: returns "0"
    uint64_t            (NRI_CALL *GetBufferDeviceAddress)          (const NriRef(Buffer) buffer);

    // Pipeline cache (PSO blob storage, persisted across runs)
    // - Threadsafe: no, external synchronization required, call after all pipeline creations using this cache have completed
    // - 2-call pattern: pass "dst = NULL" to query required "size", then call again with allocated "dst"
    Nri(Result)         (NRI_CALL *GetPipelineCacheData)            (NriRef(PipelineCache) pipelineCache, NriOut void* dst, NonNriRef(uint64_t) size);

    // Debug name for any object declared as "NriForwardStruct" (skipped for buffers & textures in D3D if they are not bound to a memory)
    void                (NRI_CALL *SetDebugName)                    (NriPtr(Object) object, const char* name);

    // Native objects                                                                                            ___D3D11 (latest interface)________|_D3D12 (latest interface)____|_VK_________________________________|_WGPU__________________________________
    void*               (NRI_CALL *GetDeviceNativeObject)           (const NriPtr(Device) device);               // ID3D11Device*                   | ID3D12Device*               | VkDevice                           | WGPUDevice
    void*               (NRI_CALL *GetQueueNativeObject)            (const NriPtr(Queue) queue);                 // -                               | ID3D12CommandQueue*         | VkQueue                            | WGPUQueue
    void*               (NRI_CALL *GetCommandBufferNativeObject)    (const NriPtr(CommandBuffer) commandBuffer); // ID3D11DeviceContext*            | ID3D12GraphicsCommandList*  | VkCommandBuffer                    | WGPUCommandBuffer
    uint64_t            (NRI_CALL *GetBufferNativeObject)           (const NriPtr(Buffer) buffer);               // ID3D11Buffer*                   | ID3D12Resource*             | VkBuffer                           | WGPUBuffer
    uint64_t            (NRI_CALL *GetTextureNativeObject)          (const NriPtr(Texture) texture);             // ID3D11Resource*                 | ID3D12Resource*             | VkImage                            | WGPUTexture
    uint64_t            (NRI_CALL *GetDescriptorNativeObject)       (const NriPtr(Descriptor) descriptor);       // ID3D11View/ID3D11SamplerState*  | D3D12_CPU_DESCRIPTOR_HANDLE | VkImageView/VkBufferView/VkSampler | WGPUTextureView/WGPUBuffer/WGPUSampler
};

NriNamespaceEnd
