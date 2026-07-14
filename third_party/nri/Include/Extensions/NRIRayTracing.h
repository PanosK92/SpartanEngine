// © 2021 NVIDIA Corporation

// Goal: ray tracing
// https://microsoft.github.io/DirectX-Specs/d3d/Raytracing.html
// https://microsoft.github.io/DirectX-Specs/d3d/Raytracing2.html

#pragma once

#define NRI_RAY_TRACING_H 1

NriNamespaceBegin

NriForwardStruct(AccelerationStructure); // bottom- or top- level acceleration structure (aka BLAS or TLAS respectively)
NriForwardStruct(Micromap);              // a micromap that encodes sub-triangle opacity (aka OMM, can be attached to a triangle BLAS)

static const NriPtr(Buffer) NriConstant(HAS_BUFFER) = (NriPtr(Buffer))1; // only to indicate buffer presence in "AccelerationStructureDesc"

//============================================================================================================================================================================================
#pragma region [ Pipeline ]
//============================================================================================================================================================================================

NriBits(RayTracingPipelineBits, uint8_t,
    NONE                        = 0,
    SKIP_TRIANGLES              = NriBit(0), // provides knowledge that "triangles" doesn't need to be considered
    SKIP_AABBS                  = NriBit(1), // provides knowledge that "aabbs" doesn't need to be considered
    ALLOW_MICROMAPS             = NriBit(2), // specifies that the ray tracing pipeline can be used with acceleration structures which reference micromaps
    FAIL_ON_CACHE_MISS          = NriBit(3)  // "CreateRayTracingPipeline" returns "FAILURE" if the pipeline is not found in the supplied cache (requires "features.pipelineCacheControl")
);

NriStruct(ShaderLibraryDesc) {
    const NriPtr(ShaderDesc) shaders;
    uint32_t shaderNum;
};

NriStruct(ShaderGroupDesc) {
    // Use cases:
    //  - general: RAYGEN_SHADER, MISS_SHADER or CALLABLE_SHADER
    //  - HitGroup: CLOSEST_HIT_SHADER and/or ANY_HIT_SHADER in any order
    //  - HitGroup with an intersection shader: INTERSECTION_SHADER + CLOSEST_HIT_SHADER and/or ANY_HIT_SHADER in any order
    uint32_t shaderIndices[3]; // in ShaderLibrary, starting from 1 (0 - unused)
};

NriStruct(RayTracingPipelineDesc) {
    const NriPtr(PipelineLayout) pipelineLayout;
    const NriPtr(ShaderLibraryDesc) shaderLibrary;
    const NriPtr(ShaderGroupDesc) shaderGroups;
    uint32_t shaderGroupNum;
    uint32_t recursionMaxDepth;
    uint32_t rayPayloadMaxSize;
    uint32_t rayHitAttributeMaxSize;
    Nri(RayTracingPipelineBits) flags;
    Nri(Robustness) robustness;
    NriOptional const NriPtr(PipelineCache) cache; // if non-NULL, pipeline creation can be served from a cached blob and the result will be added to the cache on a miss
};

#pragma endregion

//============================================================================================================================================================================================
#pragma region [ Opacity Micromap (OMM) ]
//============================================================================================================================================================================================

NriEnum(MicromapFormat, uint16_t,
    OPACITY_2_STATE             = 1,
    OPACITY_4_STATE             = 2
);

NriEnum(MicromapSpecialIndex, int8_t,
    // 2/4 state
    FULLY_TRANSPARENT           = -1,           // specifies that the entire triangle is fully transparent
    FULLY_OPAQUE                = -2,           // specifies that the entire triangle is fully opaque

    // 4 state
    FULLY_UNKNOWN_TRANSPARENT   = -3,           // specifies that the entire triangle is unknown-transparent
    FULLY_UNKNOWN_OPAQUE        = -4            // specifies that the entire triangle is unknown-opaque
);

NriBits(MicromapBits, uint8_t,
    NONE                        = 0,
    ALLOW_COMPACTION            = NriBit(1),    // allows to compact the micromap by copying using "COMPACT" mode
    PREFER_FAST_TRACE           = NriBit(2),    // prioritize traversal performance over build time
    PREFER_FAST_BUILD           = NriBit(3)     // prioritize build time over traversal performance
);

NriStruct(MicromapUsageDesc) {
    uint32_t triangleNum;                       // represents "MicromapTriangle" number for "{format, subdivisionLevel}" pair contained in the micromap
    uint16_t subdivisionLevel;                  // micro triangles count = 4 ^ subdivisionLevel
    Nri(MicromapFormat) format;
};

NriStruct(MicromapDesc) {
    NriOptional uint64_t optimizedSize;         // can be retrieved by "CmdWriteMicromapsSizes" and used for compaction via "CmdCopyMicromap"
    const NriPtr(MicromapUsageDesc) usages;
    uint32_t usageNum;
    Nri(MicromapBits) flags;
};

NriStruct(BindMicromapMemoryDesc) {
    NriPtr(Micromap) micromap;
    NriPtr(Memory) memory;
    uint64_t offset;
};

NriStruct(BuildMicromapDesc) {
    NriPtr(Micromap) dst;
    const NriPtr(Buffer) dataBuffer;
    uint64_t dataOffset;
    const NriPtr(Buffer) triangleBuffer;        // contains "MicromapTriangle" entries
    uint64_t triangleOffset;
    NriPtr(Buffer) scratchBuffer;
    uint64_t scratchOffset;
};

NriStruct(BottomLevelMicromapDesc) {
    // For each triangle in the geometry, the acceleration structure build fetches an index from "indexBuffer".
    // If an index is the unsigned cast of one of the values from "MicromapSpecialIndex" then that triangle behaves as described for that special value.
    // Otherwise that triangle uses the micromap information from "micromap" at that index plus "baseTriangle".
    // If an index buffer is not provided, "1:1" mapping between geometry triangles and micromap triangles is assumed.

    NriOptional NriPtr(Micromap) micromap;
    NriOptional const NriPtr(Buffer) indexBuffer;
    uint64_t indexOffset;
    uint32_t baseTriangle;
    Nri(IndexType) indexType;
};

// Data layout
NriStruct(MicromapTriangle) {
    uint32_t dataOffset;
    uint16_t subdivisionLevel;
    Nri(MicromapFormat) format;
};

#pragma endregion

//============================================================================================================================================================================================
#pragma region [ Acceleration Structure: Bottom Level (BLAS) ]
//============================================================================================================================================================================================

NriEnum(BottomLevelGeometryType, uint8_t,
    TRIANGLES,
    AABBS
);

NriBits(BottomLevelGeometryBits, uint8_t,
    NONE                                = 0,
    OPAQUE_GEOMETRY                     = NriBit(0),    // the geometry acts as if no any hit shader is present (can be overriden by "TopLevelInstanceBits" or ray flags)
    NO_DUPLICATE_ANY_HIT_INVOCATION     = NriBit(1)     // the any-hit shader must be called once for each primitive in this geometry
);

NriStruct(BottomLevelTrianglesDesc) {
    // Vertices
    const NriPtr(Buffer) vertexBuffer;
    uint64_t vertexOffset;
    uint32_t vertexNum;
    uint16_t vertexStride;
    Nri(Format) vertexFormat;

    // Indices
    NriOptional const NriPtr(Buffer) indexBuffer;
    NriOptional uint64_t indexOffset;
    NriOptional uint32_t indexNum;
    NriOptional Nri(IndexType) indexType;

    // Transform
    NriOptional const NriPtr(Buffer) transformBuffer;   // contains "TransformMatrix" entries
    NriOptional uint64_t transformOffset;

    // Micromap
    NriOptional NriPtr(BottomLevelMicromapDesc) micromap;
};

NriStruct(BottomLevelAabbsDesc) {
    const NriPtr(Buffer) buffer;                        // contains "BottomLevelAabb" entries
    uint64_t offset;
    uint32_t num;
    uint32_t stride;
};

NriStruct(BottomLevelGeometryDesc) {
    Nri(BottomLevelGeometryBits) flags;
    Nri(BottomLevelGeometryType) type;
    union {
        Nri(BottomLevelTrianglesDesc) triangles;
        Nri(BottomLevelAabbsDesc) aabbs;
    };
};

// Data layout
NriStruct(TransformMatrix) {
    float transform[3][4]; // 3x4 row-major affine transformation matrix, the first three columns of matrix must define an invertible 3x3 matrix
};

NriStruct(BottomLevelAabb)
{
    float minX;
    float minY;
    float minZ;
    float maxX;
    float maxY;
    float maxZ;
};

#pragma endregion

//============================================================================================================================================================================================
#pragma region [ Acceleration Structure: Top Level (TLAS) ]
//============================================================================================================================================================================================

NriBits(TopLevelInstanceBits, uint32_t,
    NONE                        = 0,
    TRIANGLE_CULL_DISABLE       = NriBit(0), // disables face culling for this instance
    TRIANGLE_FLIP_FACING        = NriBit(1), // inverts the facing determination for geometry in this instance (since the facing is determined in object space, an instance transform does not change the winding, but a geometry transform does)
    FORCE_OPAQUE                = NriBit(2), // force enable "OPAQUE_GEOMETRY" bit on all geometries referenced by this instance
    FORCE_NON_OPAQUE            = NriBit(3), // force disable "OPAQUE_GEOMETRY" bit on all geometries referenced by this instance
    FORCE_OPACITY_2_STATE       = NriBit(4), // ignore the "unknown" state and only consider the "transparent" or "opaque" bit for all 4-state micromaps encountered during traversal
    DISABLE_MICROMAPS           = NriBit(5)  // disable micromap test for all triangles and revert to using geometry opaque/non-opaque state instead
);

NriStruct(TopLevelInstance) {
    float transform[3][4];
    uint32_t instanceId                     : 24;
    uint32_t mask                           : 8;
    uint32_t shaderBindingTableLocalOffset  : 24;
    Nri(TopLevelInstanceBits) flags         : 8;
    uint64_t accelerationStructureHandle;
};

#pragma endregion

//============================================================================================================================================================================================
#pragma region [ Acceleration structure (AS) ]
//============================================================================================================================================================================================

NriEnum(AccelerationStructureType, uint8_t,
    TOP_LEVEL,
    BOTTOM_LEVEL
);

NriBits(AccelerationStructureBits, uint8_t,
    NONE                        = 0,
    ALLOW_UPDATE                = NriBit(0),                // allows to do "updates", which are faster than "builds" (may increase memory usage, build time and decrease traversal performance)
    ALLOW_COMPACTION            = NriBit(1),                // allows to compact the acceleration structure by copying using "COMPACT" mode
    ALLOW_DATA_ACCESS           = NriBit(2),                // allows to access vertex data from shaders (requires "features.rayTracingPositionFetch")
    ALLOW_MICROMAP_UPDATE       = NriBit(3),                // allows to update micromaps via acceleration structure update (may increase size and decrease traversal performance)
    ALLOW_DISABLE_MICROMAPS     = NriBit(4),                // allows to have "DISABLE_MICROMAPS" flag for instances referencing this BLAS
    PREFER_FAST_TRACE           = NriBit(5),                // prioritize traversal performance over build time
    PREFER_FAST_BUILD           = NriBit(6),                // prioritize build time over traversal performance
    MINIMIZE_MEMORY             = NriBit(7)                 // minimize the amount of memory used during the build (may increase build time and decrease traversal performance)
);

NriStruct(AccelerationStructureDesc) {
    NriOptional uint64_t optimizedSize;                     // can be retrieved by "CmdWriteAccelerationStructuresSizes" and used for compaction via "CmdCopyAccelerationStructure"
    const NriPtr(BottomLevelGeometryDesc) geometries;       // needed only for "BOTTOM_LEVEL", "HAS_BUFFER" can be used to indicate a buffer presence (no real entities needed at initialization time)
    uint32_t geometryOrInstanceNum;
    Nri(AccelerationStructureBits) flags;
    Nri(AccelerationStructureType) type;
};

NriStruct(BindAccelerationStructureMemoryDesc) {
    NriPtr(AccelerationStructure) accelerationStructure;
    NriPtr(Memory) memory;
    uint64_t offset;
};

NriStruct(BuildTopLevelAccelerationStructureDesc) {
    NriPtr(AccelerationStructure) dst;
    NriOptional const NriPtr(AccelerationStructure) src;    // implies "update" instead of "build" if provided (requires "ALLOW_UPDATE")
    uint32_t instanceNum;
    const NriPtr(Buffer) instanceBuffer;                    // contains "TopLevelInstance" entries
    uint64_t instanceOffset;
    NriPtr(Buffer) scratchBuffer;                           // use "GetAccelerationStructureBuildScratchBufferSize" or "GetAccelerationStructureUpdateScratchBufferSize" to determine the required size
    uint64_t scratchOffset;
};

NriStruct(BuildBottomLevelAccelerationStructureDesc) {
    NriPtr(AccelerationStructure) dst;
    NriOptional const NriPtr(AccelerationStructure) src;    // implies "update" instead of "build" if provided (requires "ALLOW_UPDATE")
    const NriPtr(BottomLevelGeometryDesc) geometries;
    uint32_t geometryNum;
    NriPtr(Buffer) scratchBuffer;
    uint64_t scratchOffset;
};

#pragma endregion

//============================================================================================================================================================================================
#pragma region [ Other ]
//============================================================================================================================================================================================

NriEnum(CopyMode, uint8_t,
    CLONE,
    COMPACT
);

NriStruct(StridedBufferRegion) {
    const NriPtr(Buffer) buffer;
    uint64_t offset;
    uint64_t size;
    uint64_t stride;
};

NriStruct(DispatchRaysDesc) {
    Nri(StridedBufferRegion) raygenShader;
    Nri(StridedBufferRegion) missShaders;
    Nri(StridedBufferRegion) hitShaderGroups;
    Nri(StridedBufferRegion) callableShaders;

    uint32_t x, y, z;
};

NriStruct(DispatchRaysIndirectDesc) {
    uint64_t raygenShaderRecordAddress;
    uint64_t raygenShaderRecordSize;

    uint64_t missShaderBindingTableAddress;
    uint64_t missShaderBindingTableSize;
    uint64_t missShaderBindingTableStride;

    uint64_t hitShaderBindingTableAddress;
    uint64_t hitShaderBindingTableSize;
    uint64_t hitShaderBindingTableStride;

    uint64_t callableShaderBindingTableAddress;
    uint64_t callableShaderBindingTableSize;
    uint64_t callableShaderBindingTableStride;

    uint32_t x, y, z;
};

#pragma endregion

// Threadsafe: yes
NriStruct(RayTracingInterface) {
    // Create
    Nri(Result)     (NRI_CALL *CreateRayTracingPipeline)                        (NriRef(Device) device, const NriRef(RayTracingPipelineDesc) rayTracingPipelineDesc, NriOut NriRef(Pipeline*) pipeline);
    Nri(Result)     (NRI_CALL *CreateAccelerationStructureDescriptor)           (const NriRef(AccelerationStructure) accelerationStructure, NriOut NriRef(Descriptor*) descriptor);

    // Get
    uint64_t        (NRI_CALL *GetAccelerationStructureHandle)                  (const NriRef(AccelerationStructure) accelerationStructure);
    uint64_t        (NRI_CALL *GetAccelerationStructureUpdateScratchBufferSize) (const NriRef(AccelerationStructure) accelerationStructure);
    uint64_t        (NRI_CALL *GetAccelerationStructureBuildScratchBufferSize)  (const NriRef(AccelerationStructure) accelerationStructure);
    uint64_t        (NRI_CALL *GetMicromapBuildScratchBufferSize)               (const NriRef(Micromap) micromap);

    // For barriers
    NriPtr(Buffer)  (NRI_CALL *GetAccelerationStructureBuffer)                  (const NriRef(AccelerationStructure) accelerationStructure);
    NriPtr(Buffer)  (NRI_CALL *GetMicromapBuffer)                               (const NriRef(Micromap) micromap);

    // Destroy
    void            (NRI_CALL *DestroyAccelerationStructure)                    (NriPtr(AccelerationStructure) accelerationStructure);
    void            (NRI_CALL *DestroyMicromap)                                 (NriPtr(Micromap) micromap);

    // Resources and memory (VK style)
    Nri(Result)     (NRI_CALL *CreateAccelerationStructure)                     (NriRef(Device) device, const NriRef(AccelerationStructureDesc) accelerationStructureDesc, NriOut NriRef(AccelerationStructure*) accelerationStructure);
    Nri(Result)     (NRI_CALL *CreateMicromap)                                  (NriRef(Device) device, const NriRef(MicromapDesc) micromapDesc, NriOut NriRef(Micromap*) micromap);
    void            (NRI_CALL *GetAccelerationStructureMemoryDesc)              (const NriRef(AccelerationStructure) accelerationStructure, Nri(MemoryLocation) memoryLocation, NriOut NriRef(MemoryDesc) memoryDesc);
    void            (NRI_CALL *GetMicromapMemoryDesc)                           (const NriRef(Micromap) micromap, Nri(MemoryLocation) memoryLocation, NriOut NriRef(MemoryDesc) memoryDesc);
    Nri(Result)     (NRI_CALL *BindAccelerationStructureMemory)                 (const NriPtr(BindAccelerationStructureMemoryDesc) bindAccelerationStructureMemoryDescs, uint32_t bindAccelerationStructureMemoryDescNum);
    Nri(Result)     (NRI_CALL *BindMicromapMemory)                              (const NriPtr(BindMicromapMemoryDesc) bindMicromapMemoryDescs, uint32_t bindMicromapMemoryDescNum);

    // Resources and memory (D3D12 style)
    void            (NRI_CALL *GetAccelerationStructureMemoryDesc2)             (const NriRef(Device) device, const NriRef(AccelerationStructureDesc) accelerationStructureDesc, Nri(MemoryLocation) memoryLocation, NriOut NriRef(MemoryDesc) memoryDesc); // requires "features.getMemoryDesc2"
    void            (NRI_CALL *GetMicromapMemoryDesc2)                          (const NriRef(Device) device, const NriRef(MicromapDesc) micromapDesc, Nri(MemoryLocation) memoryLocation, NriOut NriRef(MemoryDesc) memoryDesc); // requires "features.getMemoryDesc2"
    Nri(Result)     (NRI_CALL *CreateCommittedAccelerationStructure)            (NriRef(Device) device, Nri(MemoryLocation) memoryLocation, float priority, const NriRef(AccelerationStructureDesc) accelerationStructureDesc, NriOut NriRef(AccelerationStructure*) accelerationStructure);
    Nri(Result)     (NRI_CALL *CreateCommittedMicromap)                         (NriRef(Device) device, Nri(MemoryLocation) memoryLocation, float priority, const NriRef(MicromapDesc) micromapDesc, NriOut NriRef(Micromap*) micromap);
    Nri(Result)     (NRI_CALL *CreatePlacedAccelerationStructure)               (NriRef(Device) device, NriOptional NriPtr(Memory) memory, uint64_t offset, const NriRef(AccelerationStructureDesc) accelerationStructureDesc, NriOut NriRef(AccelerationStructure*) accelerationStructure);
    Nri(Result)     (NRI_CALL *CreatePlacedMicromap)                            (NriRef(Device) device, NriOptional NriPtr(Memory) memory, uint64_t offset, const NriRef(MicromapDesc) micromapDesc, NriOut NriRef(Micromap*) micromap);

    // Shader table
    // "dst" size must be >= "shaderGroupNum * rayTracingShaderGroupIdentifierSize" bytes
    // VK doesn't have a "local root signature" analog, thus stride = "rayTracingShaderGroupIdentifierSize", i.e. tight packing
    Nri(Result)     (NRI_CALL *WriteShaderGroupIdentifiers)                     (const NriRef(Pipeline) pipeline, uint32_t baseShaderGroupIndex, uint32_t shaderGroupNum, NriOut void* dst);

    // Command buffer
    // {
        // Micromap
        void        (NRI_CALL *CmdBuildMicromaps)                               (NriRef(CommandBuffer) commandBuffer, const NriPtr(BuildMicromapDesc) buildMicromapDescs, uint32_t buildMicromapDescNum);
        void        (NRI_CALL *CmdWriteMicromapsSizes)                          (NriRef(CommandBuffer) commandBuffer, const NriPtr(Micromap) const* micromaps, uint32_t micromapNum, NriRef(QueryPool) queryPool, uint32_t queryPoolOffset);
        void        (NRI_CALL *CmdCopyMicromap)                                 (NriRef(CommandBuffer) commandBuffer, NriRef(Micromap) dst, const NriRef(Micromap) src, Nri(CopyMode) copyMode);

        // Acceleration structure
        void        (NRI_CALL *CmdBuildTopLevelAccelerationStructures)          (NriRef(CommandBuffer) commandBuffer, const NriPtr(BuildTopLevelAccelerationStructureDesc) buildTopLevelAccelerationStructureDescs, uint32_t buildTopLevelAccelerationStructureDescNum);
        void        (NRI_CALL *CmdBuildBottomLevelAccelerationStructures)       (NriRef(CommandBuffer) commandBuffer, const NriPtr(BuildBottomLevelAccelerationStructureDesc) buildBotomLevelAccelerationStructureDescs, uint32_t buildBotomLevelAccelerationStructureDescNum);
        void        (NRI_CALL *CmdWriteAccelerationStructuresSizes)             (NriRef(CommandBuffer) commandBuffer, const NriPtr(AccelerationStructure) const* accelerationStructures, uint32_t accelerationStructureNum, NriRef(QueryPool) queryPool, uint32_t queryPoolOffset);
        void        (NRI_CALL *CmdCopyAccelerationStructure)                    (NriRef(CommandBuffer) commandBuffer, NriRef(AccelerationStructure) dst, const NriRef(AccelerationStructure) src, Nri(CopyMode) copyMode);

        // Ray tracing
        void        (NRI_CALL *CmdDispatchRays)                                 (NriRef(CommandBuffer) commandBuffer, const NriRef(DispatchRaysDesc) dispatchRaysDesc);
        void        (NRI_CALL *CmdDispatchRaysIndirect)                         (NriRef(CommandBuffer) commandBuffer, const NriRef(Buffer) buffer, uint64_t offset); // buffer contains "DispatchRaysIndirectDesc" commands
    // }

    // Native object
    uint64_t        (NRI_CALL* GetAccelerationStructureNativeObject)            (const NriPtr(AccelerationStructure) accelerationStructure); // ID3D12Resource* or VkAccelerationStructureKHR
    uint64_t        (NRI_CALL* GetMicromapNativeObject)                         (const NriPtr(Micromap) micromap);                           // ID3D12Resource* or VkMicromapEXT
};

NriNamespaceEnd
