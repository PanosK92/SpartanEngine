// © 2021 NVIDIA Corporation

#pragma once

#include <stddef.h>
#include <stdint.h>

#if defined(_WIN32)
    #define NRI_CALL __stdcall
#else
    #define NRI_CALL
#endif

#ifndef NRI_API
    #if defined(__cplusplus)
        #define NRI_API extern "C"
    #else
        #define NRI_API extern
    #endif
#endif

#ifdef __cplusplus
    #if !defined(NRI_FORCE_C)
        #define NRI_CPP
    #endif
#else
    #include <stdbool.h>
#endif

#include "NRIMacro.h"

// Tips:
// - designated initializers are highly recommended!
// - always zero initialize structs via "{}" if designated initializers are not used (at least to honor "NriOptional")
// - documentation is embedded (more details can be requested by creating a GitHub issue)
// - data types are grouped into collapsible logical blocks via "#pragma region"
// - in function declarations "NriRef" implies a valid object, "NriPtr" means "NULL" is allowed

NriNamespaceBegin

// Entities
NriForwardStruct(Fence);            // a synchronization primitive that can be used to insert a dependency between queue operations or between a queue operation and the host
NriForwardStruct(Queue);            // a logical queue, providing access to a HW queue
NriForwardStruct(Memory);           // a memory blob allocated on DEVICE or HOST
NriForwardStruct(Buffer);           // a buffer object: linear arrays of data
NriForwardStruct(Device);           // a logical device
NriForwardStruct(Texture);          // a texture object: multidimensional arrays of data
NriForwardStruct(Pipeline);         // a collection of state needed for rendering: shaders + fixed
NriForwardStruct(SwapChain);        // an array of presentable images that are associated with a surface
NriForwardStruct(QueryPool);        // a collection of queries of the same type
NriForwardStruct(Descriptor);       // a handle or pointer to a resource (potentially with a header)
NriForwardStruct(CommandBuffer);    // used to record commands which can be subsequently submitted to a device queue for execution (aka command list)
NriForwardStruct(DescriptorSet);    // a continuous set of descriptors
NriForwardStruct(DescriptorPool);   // maintains a pool of descriptors, descriptor sets are allocated from (aka descriptor heap)
NriForwardStruct(PipelineLayout);   // determines the interface between shader stages and shader resources (aka root signature)
NriForwardStruct(PipelineCache);    // a persistent cache of compiled pipeline state objects (PSOs) to accelerate subsequent PSO creations
NriForwardStruct(CommandAllocator); // an object that command buffer memory is allocated from

// Basic types
typedef uint8_t Nri(Sample_t);
typedef uint16_t Nri(Dim_t);
typedef void Nri(Object);

NriStruct(Uid_t) {
    uint64_t low;
    uint64_t high;
};

NriStruct(Dim2_t) {
    Nri(Dim_t) w, h;
};

NriStruct(Float2_t) {
    float x, y;
};

// Aliases
static const uint32_t NriConstant(BGRA_UNUSED) = 0;     // only for "bgra" color for profiling
static const uint32_t NriConstant(ALL) = 0;             // only for "sampleMask" and "descriptorNum"
static const Nri(Dim_t) NriConstant(WHOLE_SIZE) = 0;    // only for "Dim_t" and "size"
static const Nri(Dim_t) NriConstant(REMAINING) = 0;     // only for "mipNum" and "layerNum"

// Readability
#define NriOptional // i.e. can be 0 (keep an eye on comments)
#define NriOut      // highlights an output argument

// Implicit memory heaps for "CreatePlacedX"
#define NriDeviceHeap 0, 0
#define NriDeviceUploadHeap 0, 1
#define NriHostUploadHeap 0, 2
#define NriHostReadbackHeap 0, 3

//============================================================================================================================================================================================
#pragma region [ Common ]
//============================================================================================================================================================================================

// "AdapterDesc::supportedGraphicsAPIs" is a mask of supported graphics APIs
NriBits(GraphicsAPI, uint8_t,
    NONE    = NriBit(0), // Supports everything, does nothing, returns dummy non-NULL objects and ~0-filled descs, available if "NRI_ENABLE_NONE_SUPPORT = ON" in CMake
    D3D11   = NriBit(1), // Direct3D 11 (feature set 11.1), available if "NRI_ENABLE_D3D11_SUPPORT = ON" in CMake (https://microsoft.github.io/DirectX-Specs/d3d/archive/D3D11_3_FunctionalSpec.htm)
    D3D12   = NriBit(2), // Direct3D 12 (D3D12_SDK_VERSION 4 or 619+), available if "NRI_ENABLE_D3D12_SUPPORT = ON" in CMake (https://microsoft.github.io/DirectX-Specs/)
    VK      = NriBit(3), // Vulkan 1.4+, 1.3++ or 1.2+++ (can be used on MacOS via MoltenVK), available if "NRI_ENABLE_VK_SUPPORT = ON" in CMake (https://registry.khronos.org/vulkan/specs/latest/html/vkspec.html)
    WGPU    = NriBit(4)  // WebGPU via wgpu-native, available if "NRI_ENABLE_WGPU_SUPPORT = ON" in CMake (https://github.com/gfx-rs/wgpu-native)
);

NriEnum(Result, int8_t,
    // All bad, but optionally require an action ("callbackInterface.AbortExecution" is not triggered)
    DEVICE_LOST             = -3,   // may be returned by "QueueSubmit*", "*WaitIdle", "AcquireNextTexture", "QueuePresent", "WaitForPresent"
    OUT_OF_DATE             = -2,   // VK: swap chain is out of date, can be triggered if "features.resizableSwapChain" is not supported; D3D12: shader cache is stale
    INVALID_SDK             = -1,   // D3D12: some interfaces are missing (potential reasons: unable to load "D3D12Core.dll", version or SDK mismatch, developer mode is not enabled)

    // All good
    SUCCESS                 = 0,

    // All bad, most likely a crash or a validation error will happen next ("callbackInterface.AbortExecution" is triggered)
    FAILURE                 = 1,
    INVALID_ARGUMENT        = 2,
    OUT_OF_MEMORY           = 3,
    UNSUPPORTED             = 4     // if enabled, NRI validation can promote some to "INVALID_ARGUMENT"
);

// The viewport origin is top-left (D3D native) by default, but can be changed to bottom-left (VK native)
// https://docs.vulkan.org/refpages/latest/refpages/source/VkViewport.html
// https://learn.microsoft.com/en-us/windows/win32/api/d3d12/ns-d3d12-d3d12_viewport
NriStruct(Viewport) {
    float x;
    float y;
    float width;
    float height;
    float depthMin;
    float depthMax;
    bool originBottomLeft;          // expects "features.viewportOriginBottomLeft"
};

// https://docs.vulkan.org/refpages/latest/refpages/source/VkRect2D.html
NriStruct(Rect) {
    int16_t x;
    int16_t y;
    Nri(Dim_t) width;
    Nri(Dim_t) height;
};

NriStruct(Color32f) {
    float x, y, z, w;
};

NriStruct(Color32ui) {
    uint32_t x, y, z, w;
};

NriStruct(Color32i) {
    int32_t x, y, z, w;
};

NriStruct(DepthStencil) {
    float depth;
    uint8_t stencil;
};

NriUnion(Color) {
    Nri(Color32f) f;
    Nri(Color32ui) ui;
    Nri(Color32i) i;
};

NriUnion(ClearValue) {
    Nri(DepthStencil) depthStencil;
    Nri(Color) color;
};

NriStruct(SampleLocation) {
    int8_t x, y; // [-8; 7]
};

#pragma endregion

//============================================================================================================================================================================================
#pragma region [ Formats ]
//============================================================================================================================================================================================

// https://docs.vulkan.org/refpages/latest/refpages/source/VkFormat.html
// https://learn.microsoft.com/en-us/windows/win32/api/dxgiformat/ne-dxgiformat-dxgi_format
// left -> right : low -> high bits
// Expected (but not guaranteed) "FormatSupportBits" are provided, but "GetFormatSupport" should be used for querying real HW support
// To demote sRGB use the previous format, i.e. "format - 1"
//                                            STORAGE_WRITE_WITHOUT_FORMAT
//                                           STORAGE_READ_WITHOUT_FORMAT |
//                                                       VERTEX_BUFFER | |
//                                            STORAGE_BUFFER_ATOMICS | | |
//                                                  STORAGE_BUFFER | | | |
//                                                        BUFFER | | | | |
//                                         MULTISAMPLE_RESOLVE | | | | | |
//                                            MULTISAMPLE_8X | | | | | | |
//                                          MULTISAMPLE_4X | | | | | | | |
//                                        MULTISAMPLE_2X | | | | | | | | |
//                                               BLEND | | | | | | | | | |
//                          DEPTH_STENCIL_ATTACHMENT | | | | | | | | | | |
//                                COLOR_ATTACHMENT | | | | | | | | | | | |
//                       STORAGE_TEXTURE_ATOMICS | | | | | | | | | | | | |
//                             STORAGE_TEXTURE | | | | | | | | | | | | | |
//                                   TEXTURE | | | | | | | | | | | | | | |
//                                         | | | | | | | | | | | | | | | |
NriEnum(Format, uint8_t,                // |      FormatSupportBits      |
    UNKNOWN,                            // . . . . . . . . . . . . . . . .

    // Plain: 8 bits per channel
    R8_UNORM,                           // + + . + . + + + + + + + . + + +
    R8_SNORM,                           // + + . + . + + + + + + + . + + +
    R8_UINT,                            // + + . + . . + + + . + + . + + +  // SHADING_RATE compatible, see NRI_SHADING_RATE macro
    R8_SINT,                            // + + . + . . + + + . + + . + + +

    RG8_UNORM,                          // + + . + . + + + + + + + . + + +  // "AccelerationStructure" compatible (requires "tiers.rayTracing >= 2")
    RG8_SNORM,                          // + + . + . + + + + + + + . + + +  // "AccelerationStructure" compatible (requires "tiers.rayTracing >= 2")
    RG8_UINT,                           // + + . + . . + + + . + + . + + +
    RG8_SINT,                           // + + . + . . + + + . + + . + + +

    BGRA8_UNORM,                        // + + . + . + + + + + + + . + + +
    BGRA8_SRGB,                         // + . . + . + + + + + . . . . . .

    RGBA8_UNORM,                        // + + . + . + + + + + + + . + + +  // "AccelerationStructure" compatible (requires "tiers.rayTracing >= 2")
    RGBA8_SRGB,                         // + . . + . + + + + + . . . . . .
    RGBA8_SNORM,                        // + + . + . + + + + + + + . + + +  // "AccelerationStructure" compatible (requires "tiers.rayTracing >= 2")
    RGBA8_UINT,                         // + + . + . . + + + . + + . + + +
    RGBA8_SINT,                         // + + . + . . + + + . + + . + + +

    // Plain: 16 bits per channel
    R16_UNORM,                          // + + . + . + + + + + + + . + + +
    R16_SNORM,                          // + + . + . + + + + + + + . + + +
    R16_UINT,                           // + + . + . . + + + . + + . + + +
    R16_SINT,                           // + + . + . . + + + . + + . + + +
    R16_SFLOAT,                         // + + . + . + + + + + + + . + + +

    RG16_UNORM,                         // + + . + . + + + + + + + . + + +  // "AccelerationStructure" compatible (requires "tiers.rayTracing >= 2")
    RG16_SNORM,                         // + + . + . + + + + + + + . + + +  // "AccelerationStructure" compatible
    RG16_UINT,                          // + + . + . . + + + . + + . + + +
    RG16_SINT,                          // + + . + . . + + + . + + . + + +
    RG16_SFLOAT,                        // + + . + . + + + + + + + . + + +  // "AccelerationStructure" compatible

    RGBA16_UNORM,                       // + + . + . + + + + + + + . + + +  // "AccelerationStructure" compatible (requires "tiers.rayTracing >= 2")
    RGBA16_SNORM,                       // + + . + . + + + + + + + . + + +  // "AccelerationStructure" compatible
    RGBA16_UINT,                        // + + . + . . + + + . + + . + + +
    RGBA16_SINT,                        // + + . + . . + + + . + + . + + +
    RGBA16_SFLOAT,                      // + + . + . + + + + + + + . + + +  // "AccelerationStructure" compatible

    // Plain: 32 bits per channel
    R32_UINT,                           // + + + + . . + + + . + + + + + +
    R32_SINT,                           // + + + + . . + + + . + + + + + +
    R32_SFLOAT,                         // + + + + . + + + + + + + + + + +

    RG32_UINT,                          // + + . + . . + + + . + + . + + +
    RG32_SINT,                          // + + . + . . + + + . + + . + + +
    RG32_SFLOAT,                        // + + . + . + + + + + + + . + + +  // "AccelerationStructure" compatible

    RGB32_UINT,                         // + . . . . . . . . . + . . + . .
    RGB32_SINT,                         // + . . . . . . . . . + . . + . .
    RGB32_SFLOAT,                       // + . . . . . . . . + + . . + . .  // "AccelerationStructure" compatible

    RGBA32_UINT,                        // + + . + . . + + + . + + . + + +
    RGBA32_SINT,                        // + + . + . . + + + . + + . + + +
    RGBA32_SFLOAT,                      // + + . + . + + + + + + + . + + +

    // Packed: 16 bits per pixel
    B5_G6_R5_UNORM,                     // + . . + . + + + + + . . . . . .
    B5_G5_R5_A1_UNORM,                  // + . . + . + + + + + . . . . . .
    B4_G4_R4_A4_UNORM,                  // + . . . . . . . . + . . . . . .

    // Packed: 32 bits per pixel
    R10_G10_B10_A2_UNORM,               // + + . + . + + + + + + + . + + +  // "AccelerationStructure" compatible (requires "tiers.rayTracing >= 2")
    R10_G10_B10_A2_UINT,                // + + . + . . + + + . + + . + + +
    R11_G11_B10_UFLOAT,                 // + + . + . + + + + + + + . + + +
    R9_G9_B9_E5_UFLOAT,                 // + . . . . . . . . . . . . . . .

    // Block-compressed (requires "features.textureCompressionBC")
    // https://learn.microsoft.com/en-us/windows/win32/direct3d11/texture-block-compression-in-direct3d-11?source=recommendations
    // https://registry.khronos.org/DataFormat/specs/1.4/dataformat.1.4.html#S3TC
    // https://registry.khronos.org/DataFormat/specs/1.4/dataformat.1.4.html#RGTC
    // https://registry.khronos.org/DataFormat/specs/1.4/dataformat.1.4.html#BPTC
    BC1_RGBA_UNORM,                     // + . . . . . . . . . . . . . . .
    BC1_RGBA_SRGB,                      // + . . . . . . . . . . . . . . .
    BC2_RGBA_UNORM,                     // + . . . . . . . . . . . . . . .
    BC2_RGBA_SRGB,                      // + . . . . . . . . . . . . . . .
    BC3_RGBA_UNORM,                     // + . . . . . . . . . . . . . . .
    BC3_RGBA_SRGB,                      // + . . . . . . . . . . . . . . .
    BC4_R_UNORM,                        // + . . . . . . . . . . . . . . .
    BC4_R_SNORM,                        // + . . . . . . . . . . . . . . .
    BC5_RG_UNORM,                       // + . . . . . . . . . . . . . . .
    BC5_RG_SNORM,                       // + . . . . . . . . . . . . . . .
    BC6H_RGB_UFLOAT,                    // + . . . . . . . . . . . . . . .
    BC6H_RGB_SFLOAT,                    // + . . . . . . . . . . . . . . .
    BC7_RGBA_UNORM,                     // + . . . . . . . . . . . . . . .
    BC7_RGBA_SRGB,                      // + . . . . . . . . . . . . . . .

    // Block-compressed: Ericsson Texture Compression (requires "features.textureCompressionETC2")
    // https://registry.khronos.org/DataFormat/specs/1.4/dataformat.1.4.html#ETC2
    ETC2_RGB8_UNORM,                    // + . . . . . . . . . . . . . . .
    ETC2_RGB8_SRGB,                     // + . . . . . . . . . . . . . . .
    ETC2_RGB8_A1_UNORM,                 // + . . . . . . . . . . . . . . .
    ETC2_RGB8_A1_SRGB,                  // + . . . . . . . . . . . . . . .
    ETC2_RGB8_A8_UNORM,                 // + . . . . . . . . . . . . . . .
    ETC2_RGB8_A8_SRGB,                  // + . . . . . . . . . . . . . . .
    ETC2_R11_UNORM,                     // + . . . . . . . . . . . . . . .
    ETC2_R11_SNORM,                     // + . . . . . . . . . . . . . . .
    ETC2_R11_G11_UNORM,                 // + . . . . . . . . . . . . . . .
    ETC2_R11_G11_SNORM,                 // + . . . . . . . . . . . . . . .

    // Block-compressed: Adaptive Scalable Texture Compression (requires "features.textureCompressionASTC")
    // https://registry.khronos.org/DataFormat/specs/1.4/dataformat.1.4.html#ASTC
    ASTC_4X4_UNORM,                     // + . . . . . . . . . . . . . . .
    ASTC_4X4_SRGB,                      // + . . . . . . . . . . . . . . .
    ASTC_5X4_UNORM,                     // + . . . . . . . . . . . . . . .
    ASTC_5X4_SRGB,                      // + . . . . . . . . . . . . . . .
    ASTC_5X5_UNORM,                     // + . . . . . . . . . . . . . . .
    ASTC_5X5_SRGB,                      // + . . . . . . . . . . . . . . .
    ASTC_6X5_UNORM,                     // + . . . . . . . . . . . . . . .
    ASTC_6X5_SRGB,                      // + . . . . . . . . . . . . . . .
    ASTC_6X6_UNORM,                     // + . . . . . . . . . . . . . . .
    ASTC_6X6_SRGB,                      // + . . . . . . . . . . . . . . .
    ASTC_8X5_UNORM,                     // + . . . . . . . . . . . . . . .
    ASTC_8X5_SRGB,                      // + . . . . . . . . . . . . . . .
    ASTC_8X6_UNORM,                     // + . . . . . . . . . . . . . . .
    ASTC_8X6_SRGB,                      // + . . . . . . . . . . . . . . .
    ASTC_8X8_UNORM,                     // + . . . . . . . . . . . . . . .
    ASTC_8X8_SRGB,                      // + . . . . . . . . . . . . . . .
    ASTC_10X5_UNORM,                    // + . . . . . . . . . . . . . . .
    ASTC_10X5_SRGB,                     // + . . . . . . . . . . . . . . .
    ASTC_10X6_UNORM,                    // + . . . . . . . . . . . . . . .
    ASTC_10X6_SRGB,                     // + . . . . . . . . . . . . . . .
    ASTC_10X8_UNORM,                    // + . . . . . . . . . . . . . . .
    ASTC_10X8_SRGB,                     // + . . . . . . . . . . . . . . .
    ASTC_10X10_UNORM,                   // + . . . . . . . . . . . . . . .
    ASTC_10X10_SRGB,                    // + . . . . . . . . . . . . . . .
    ASTC_12X10_UNORM,                   // + . . . . . . . . . . . . . . .
    ASTC_12X10_SRGB,                    // + . . . . . . . . . . . . . . .
    ASTC_12X12_UNORM,                   // + . . . . . . . . . . . . . . .
    ASTC_12X12_SRGB,                    // + . . . . . . . . . . . . . . .

    // Depth
    D16_UNORM,                          // + . . . + . + + + . . . . . . .
    D32_SFLOAT,                         // + . . . + . + + + . . . . . . .

    // Depth-stencil
    D24_UNORM_S8_UINT,                  // + . . . + . + + + . . . . . . .
    D32_SFLOAT_S8_UINT                  // + . . . + . + + + . . . . . . .
);

// https://learn.microsoft.com/en-us/windows/win32/direct3d12/subresources#plane-slice
// https://docs.vulkan.org/refpages/latest/refpages/source/VkImageAspectFlagBits.html
NriBits(PlaneBits, uint8_t,
    ALL                             = 0,            // lazy default
    NONE                            = NriBit(7),    // no accessible planes (needed for a read-only depth-stencil attachment)

    COLOR                           = NriBit(0),    // indicates "color" plane (same as "ALL" for color formats)

    // D3D11: can't be addressed individually in "copy" and "resolve" operations
    DEPTH                           = NriBit(1),    // indicates "depth" plane (same as "ALL" for depth-only formats)
    STENCIL                         = NriBit(2)     // indicates "stencil" plane in depth-stencil formats
);

// A bit represents a feature, supported by a format
// https://learn.microsoft.com/en-us/windows/win32/api/d3d12/ns-d3d12-d3d12_feature_data_format_support
// https://docs.vulkan.org/refpages/latest/refpages/source/VkFormatFeatureFlagBits2.html
// WGPU: typed buffer views are unsupported; storage textures cannot be multisampled
NriBits(FormatSupportBits, uint16_t,
    UNSUPPORTED                     = 0,            // format is unsupported

    // Texture
    TEXTURE                         = NriBit(0),    // sampled texture view
    STORAGE_TEXTURE                 = NriBit(1),    // storage texture view
    STORAGE_TEXTURE_ATOMICS         = NriBit(2),    // storage texture atomics other than Load / Store
    COLOR_ATTACHMENT                = NriBit(3),    // color attachment view
    DEPTH_STENCIL_ATTACHMENT        = NriBit(4),    // depth-stencil attachment view
    BLEND                           = NriBit(5),    // color attachment blending
    MULTISAMPLE_2X                  = NriBit(6),    // 2x multisampled texture
    MULTISAMPLE_4X                  = NriBit(7),    // 4x multisampled texture
    MULTISAMPLE_8X                  = NriBit(8),    // 8x multisampled texture
    MULTISAMPLE_RESOLVE             = NriBit(9),    // resolve source/destination

    // Buffer
    BUFFER                          = NriBit(10),   // typed buffer view
    STORAGE_BUFFER                  = NriBit(11),   // typed storage buffer view
    STORAGE_BUFFER_ATOMICS          = NriBit(12),   // typed storage buffer atomics other than Load / Store
    VERTEX_BUFFER                   = NriBit(13),   // vertex buffer attribute

    // Texture / buffer
    STORAGE_READ_WITHOUT_FORMAT     = NriBit(14),   // storage read with unknown format
    STORAGE_WRITE_WITHOUT_FORMAT    = NriBit(15)    // storage write with unknown format
);

#pragma endregion

//============================================================================================================================================================================================
#pragma region [ Pipeline stages and barriers ]
//============================================================================================================================================================================================

// https://microsoft.github.io/DirectX-Specs/d3d/D3D12EnhancedBarriers.html
// https://docs.vulkan.org/samples/latest/samples/performance/pipeline_barriers/README.html

// A barrier consists of two phases:
// - before (source scope, 1st synchronization scope):
//   - "AccessBits" corresponding with any relevant resource usage since the preceding barrier or the start of "QueueSubmit" scope
//   - "StagesBits" of all preceding GPU work that must be completed before executing the barrier (stages to wait before the barrier)
//   - "Layout" for textures
// - after (destination scope, 2nd synchronization scope):
//   - "AccessBits" corresponding with any relevant resource usage after the barrier completes
//   - "StagesBits" of all subsequent GPU work that must wait until the barrier execution is finished (stages to halt until the barrier is executed)
//   - "Layout" for textures
// If "features.enhancedBarriers" is not supported:
//   - https://microsoft.github.io/DirectX-Specs/d3d/D3D12EnhancedBarriers.html#compatibility-with-legacy-d3d12_resource_states
//   - "AccessBits::NONE" gets mapped to "COMMON" (aka "GENERAL" access), leading to potential discrepancies with VK

// https://docs.vulkan.org/refpages/latest/refpages/source/VkPipelineStageFlagBits2.html
// https://microsoft.github.io/DirectX-Specs/d3d/D3D12EnhancedBarriers.html#d3d12_barrier_sync
NriBits(StageBits, uint32_t,
    // Special
    ALL                             = 0,            // Lazy default for barriers                          Shader stage
    NONE                            = 0x7FFFFFFF,

    // Graphics                                     // Invoked by "CmdDraw*"
    INDEX_INPUT                     = NriBit(0),    //    Index buffer consumption
    VERTEX_SHADER                   = NriBit(1),    //    Vertex shader                                   X (required within GRAPHICS bind point)
    TESS_CONTROL_SHADER             = NriBit(2),    //    Tessellation control (hull) shader              X
    TESS_EVALUATION_SHADER          = NriBit(3),    //    Tessellation evaluation (domain) shader         X
    GEOMETRY_SHADER                 = NriBit(4),    //    Geometry shader                                 X
    TASK_SHADER                     = NriBit(5),    //    Task (amplification) shader                     X
    MESH_SHADER                     = NriBit(6),    //    Mesh shader                                     X (or required within GRAPHICS bind point)
    FRAGMENT_SHADER                 = NriBit(7),    //    Fragment (pixel) shader                         X
    DEPTH_STENCIL_ATTACHMENT        = NriBit(8),    //    Depth-stencil R/W operations
    COLOR_ATTACHMENT                = NriBit(9),    //    Color R/W operations
    SHADING_RATE_ATTACHMENT         = NriBit(10),   //    Shading rate attachment R

    // Compute                                      // Invoked by "CmdDispatch*" (not Rays)
    COMPUTE_SHADER                  = NriBit(11),   //    Compute shader                                  X (required within COMPUTE bind point)

    // Ray tracing                                  // Invoked by "CmdDispatchRays*"
    RAYGEN_SHADER                   = NriBit(12),   //    Ray generation shader                           X (required within RAY_TRACING bind point)
    MISS_SHADER                     = NriBit(13),   //    Miss shader                                     X
    INTERSECTION_SHADER             = NriBit(14),   //    Intersection shader                             X
    CLOSEST_HIT_SHADER              = NriBit(15),   //    Closest hit shader                              X
    ANY_HIT_SHADER                  = NriBit(16),   //    Any hit shader                                  X
    CALLABLE_SHADER                 = NriBit(17),   //    Callable shader                                 X
    ACCELERATION_STRUCTURE          = NriBit(18),   // Invoked by "Cmd*AccelerationStructure*" commands
    MICROMAP                        = NriBit(19),   // Invoked by "Cmd*Micromap*" commands

    // Other
    COPY                            = NriBit(20),   // Invoked by "CmdCopy*", "CmdUpload*" and "CmdReadback*"
    RESOLVE                         = NriBit(21),   // Invoked by "CmdResolveTexture"
    CLEAR_STORAGE                   = NriBit(22),   // Invoked by "CmdClearStorage"

    // Modifiers
    INDIRECT                        = NriBit(23),   // Invoked by "Indirect" commands (used in addition to other bits)

    // Umbrella stages
    TESSELLATION_SHADERS            = NriMember(StageBits, TESS_CONTROL_SHADER)
                                    | NriMember(StageBits, TESS_EVALUATION_SHADER),

    MESH_SHADERS                    = NriMember(StageBits, TASK_SHADER)
                                    | NriMember(StageBits, MESH_SHADER),

    GRAPHICS_SHADERS                = NriMember(StageBits, VERTEX_SHADER)
                                    | NriMember(StageBits, TESSELLATION_SHADERS)
                                    | NriMember(StageBits, GEOMETRY_SHADER)
                                    | NriMember(StageBits, MESH_SHADERS)
                                    | NriMember(StageBits, FRAGMENT_SHADER),

    RAY_TRACING_SHADERS             = NriMember(StageBits, RAYGEN_SHADER)
                                    | NriMember(StageBits, MISS_SHADER)
                                    | NriMember(StageBits, INTERSECTION_SHADER)
                                    | NriMember(StageBits, CLOSEST_HIT_SHADER)
                                    | NriMember(StageBits, ANY_HIT_SHADER)
                                    | NriMember(StageBits, CALLABLE_SHADER),

    ALL_SHADERS                     = NriMember(StageBits, GRAPHICS_SHADERS)
                                    | NriMember(StageBits, COMPUTE_SHADER)
                                    | NriMember(StageBits, RAY_TRACING_SHADERS),

    GRAPHICS                        = NriMember(StageBits, INDEX_INPUT)
                                    | NriMember(StageBits, GRAPHICS_SHADERS)
                                    | NriMember(StageBits, DEPTH_STENCIL_ATTACHMENT)
                                    | NriMember(StageBits, COLOR_ATTACHMENT)
                                    | NriMember(StageBits, SHADING_RATE_ATTACHMENT)
);

// https://docs.vulkan.org/refpages/latest/refpages/source/VkAccessFlagBits2.html
// https://microsoft.github.io/DirectX-Specs/d3d/D3D12EnhancedBarriers.html#d3d12_barrier_access
NriBits(AccessBits, uint32_t,
    NONE                            = 0,        // Mapped to "COMMON" (aka "GENERAL" access), if AgilitySDK is not available, leading to potential discrepancies with VK

    // Buffer                                   // Access   Compatible "StageBits" (including ALL)
    INDEX_BUFFER                    = NriBit(0),    // R        INDEX_INPUT
    VERTEX_BUFFER                   = NriBit(1),    // R        VERTEX_SHADER
    CONSTANT_BUFFER                 = NriBit(2),    // R        ALL_SHADERS
    ARGUMENT_BUFFER                 = NriBit(3),    // R        INDIRECT
    SCRATCH_BUFFER                  = NriBit(4),    // RW       ACCELERATION_STRUCTURE, MICROMAP

    // Attachment
    COLOR_ATTACHMENT_READ           = NriBit(5),    // R        COLOR_ATTACHMENT (implicitly by ROP)
    COLOR_ATTACHMENT_WRITE          = NriBit(6),    //  W       COLOR_ATTACHMENT
    DEPTH_STENCIL_ATTACHMENT_READ   = NriBit(7),    // R        DEPTH_STENCIL_ATTACHMENT
    DEPTH_STENCIL_ATTACHMENT_WRITE  = NriBit(8),    //  W       DEPTH_STENCIL_ATTACHMENT
    SHADING_RATE_ATTACHMENT         = NriBit(9),    // R        SHADING_RATE_ATTACHMENT
    INPUT_ATTACHMENT                = NriBit(10),   // R        FRAGMENT_SHADER

    // Acceleration structure
    ACCELERATION_STRUCTURE_READ     = NriBit(11),   // R        COMPUTE_SHADER, RAY_TRACING_SHADERS, ACCELERATION_STRUCTURE
    ACCELERATION_STRUCTURE_WRITE    = NriBit(12),   //  W       ACCELERATION_STRUCTURE

    // Micromap
    MICROMAP_READ                   = NriBit(13),   // R        MICROMAP, ACCELERATION_STRUCTURE
    MICROMAP_WRITE                  = NriBit(14),   //  W       MICROMAP

    // Shader
    SHADER_RESOURCE                 = NriBit(15),   // R        ALL_SHADERS
    SHADER_RESOURCE_STORAGE         = NriBit(16),   // RW       ALL_SHADERS, CLEAR_STORAGE
    SHADER_BINDING_TABLE            = NriBit(17),   // R        RAY_TRACING_SHADERS

    // Copy
    COPY_SOURCE                     = NriBit(18),   // R        COPY
    COPY_DESTINATION                = NriBit(19),   //  W       COPY

    // Resolve
    RESOLVE_SOURCE                  = NriBit(20),   // R        RESOLVE
    RESOLVE_DESTINATION             = NriBit(21),   //  W       RESOLVE

    // Clear storage
    CLEAR_STORAGE                   = NriBit(22),   //  W       CLEAR_STORAGE

    // Umbrella access
    COLOR_ATTACHMENT                = NriMember(AccessBits, COLOR_ATTACHMENT_READ)
                                    | NriMember(AccessBits, COLOR_ATTACHMENT_WRITE),

    DEPTH_STENCIL_ATTACHMENT        = NriMember(AccessBits, DEPTH_STENCIL_ATTACHMENT_READ)
                                    | NriMember(AccessBits, DEPTH_STENCIL_ATTACHMENT_WRITE),

    ACCELERATION_STRUCTURE          = NriMember(AccessBits, ACCELERATION_STRUCTURE_READ)
                                    | NriMember(AccessBits, ACCELERATION_STRUCTURE_WRITE),

    MICROMAP                        = NriMember(AccessBits, MICROMAP_READ)
                                    | NriMember(AccessBits, MICROMAP_WRITE)
);

// "Layout" is ignored if "features.enhancedBarriers" is not supported
// https://docs.vulkan.org/refpages/latest/refpages/source/VkImageLayout.html
// https://microsoft.github.io/DirectX-Specs/d3d/D3D12EnhancedBarriers.html#d3d12_barrier_layout
NriEnum(Layout, uint8_t,            // Compatible "AccessBits":
    // Special
    UNDEFINED,                          // https://microsoft.github.io/DirectX-Specs/d3d/D3D12EnhancedBarriers.html#d3d12_barrier_layout_undefined
    GENERAL,                            // ALL access, required for "SharingMode::SIMULTANEOUS" (but may be suboptimal if "features.unifiedTextureLayouts" is not supported)
    PRESENT,                            // NONE (use "after.stages = StageBits::NONE")

    // Attachment
    COLOR_ATTACHMENT,                   // COLOR_ATTACHMENT_READ/WRITE
    DEPTH_STENCIL_ATTACHMENT,           // DEPTH_STENCIL_ATTACHMENT_READ/WRITE
    DEPTH_READONLY_STENCIL_ATTACHMENT,  // DEPTH_STENCIL_ATTACHMENT_READ/WRITE (accessible "planes" = "STENCIL"), SHADER_RESOURCE (accessible "planes" = "DEPTH")
    DEPTH_ATTACHMENT_STENCIL_READONLY,  // DEPTH_STENCIL_ATTACHMENT_READ/WRITE (accessible "planes" = "DEPTH"), SHADER_RESOURCE (accessible "planes" = "STENCIL")
    DEPTH_STENCIL_READONLY,             // DEPTH_STENCIL_ATTACHMENT_READ  (accessible "planes" = "NONE")
    SHADING_RATE_ATTACHMENT,            // SHADING_RATE_ATTACHMENT
    INPUT_ATTACHMENT,                   // COLOR_ATTACHMENT, INPUT_ATTACHMENT

    // Shader
    SHADER_RESOURCE,                    // SHADER_RESOURCE
    SHADER_RESOURCE_STORAGE,            // SHADER_RESOURCE_STORAGE

    // Copy
    COPY_SOURCE,                        // COPY_SOURCE
    COPY_DESTINATION,                   // COPY_DESTINATION

    // Resolve
    RESOLVE_SOURCE,                     // RESOLVE_SOURCE
    RESOLVE_DESTINATION                 // RESOLVE_DESTINATION
);

NriStruct(AccessStage) {
    Nri(AccessBits) access;
    Nri(StageBits) stages;
};

NriStruct(AccessLayoutStage) {
    Nri(AccessBits) access;
    Nri(Layout) layout;
    Nri(StageBits) stages;
};

NriStruct(GlobalBarrierDesc) {
    Nri(AccessStage) before;
    Nri(AccessStage) after;
};

NriStruct(BufferBarrierDesc) {
    NriPtr(Buffer) buffer;  // use "GetAccelerationStructureBuffer" and "GetMicromapBuffer" for related barriers
    Nri(AccessStage) before;
    Nri(AccessStage) after;
};

NriStruct(TextureBarrierDesc) {
    NriPtr(Texture) texture;
    Nri(AccessLayoutStage) before;
    Nri(AccessLayoutStage) after;
    Nri(Dim_t) mipOffset;
    Nri(Dim_t) mipNum;      // can be "REMAINING"
    Nri(Dim_t) layerOffset;
    Nri(Dim_t) layerNum;    // can be "REMAINING"
    Nri(PlaneBits) planes;

    // Queue ownership transfer is potentially needed only for "SharingMode::EXCLUSIVE" textures
    // https://registry.khronos.org/vulkan/specs/latest/html/vkspec.html#synchronization-queue-transfers
    NriOptional NriPtr(Queue) srcQueue;
    NriOptional NriPtr(Queue) dstQueue;
};

// Using "CmdBarrier" inside a rendering pass is allowed, but only for "Layout::INPUT_ATTACHMENT" access transitions
// D3D12 filters out "transitioning to the same state" barriers if "features.enhancedBarriers" is not supported
NriStruct(BarrierDesc) {
    const NriPtr(GlobalBarrierDesc) globals;
    uint32_t globalNum;
    const NriPtr(BufferBarrierDesc) buffers;
    uint32_t bufferNum;
    const NriPtr(TextureBarrierDesc) textures;
    uint32_t textureNum;
};

#pragma endregion

//============================================================================================================================================================================================
#pragma region [ Resources: creation ]
//============================================================================================================================================================================================

// https://docs.vulkan.org/refpages/latest/refpages/source/VkImageType.html
// https://learn.microsoft.com/en-us/windows/win32/api/d3d12/ne-d3d12-d3d12_resource_dimension
NriEnum(TextureType, uint8_t,
    TEXTURE_1D,
    TEXTURE_2D,
    TEXTURE_3D
);

// NRI tries to ease your life and avoid using "queue ownership transfers" (see "TextureBarrierDesc").
// In most of cases "SharingMode" can be ignored. Where is it needed?
// - VK: use "EXCLUSIVE" for attachments participating into multi-queue activities to preserve DCC (Delta Color Compression) on some HW
// - D3D12: use "SIMULTANEOUS" to concurrently use a texture as a "SHADER_RESOURCE" (or "SHADER_RESOURCE_STORAGE") and as a "COPY_DESTINATION" for non overlapping texture regions
// https://docs.vulkan.org/refpages/latest/refpages/source/VkSharingMode.html
NriEnum(SharingMode, uint8_t,
    CONCURRENT,     // VK: lazy default to avoid dealing with "queue ownership transfers", auto-optimized to "EXCLUSIVE" if all queues have the same type
    EXCLUSIVE,      // VK: may be used for attachments to preserve DCC on some HW in the cost of making a "queue ownership transfer"

    // https://microsoft.github.io/DirectX-Specs/d3d/D3D12EnhancedBarriers.html#single-queue-simultaneous-access
    // https://learn.microsoft.com/en-us/windows/win32/api/d3d12/ne-d3d12-d3d12_resource_flags
    SIMULTANEOUS    // D3D12: strengthened variant of "CONCURRENT", allowing simultaneous multiple readers and one writer for a texture (requires "Layout::GENERAL")
);

// https://docs.vulkan.org/refpages/latest/refpages/source/VkImageUsageFlagBits.html
// https://learn.microsoft.com/en-us/windows/win32/api/d3d12/ne-d3d12-d3d12_resource_flags
NriBits(TextureUsageBits, uint8_t,                  // Min compatible access:                   Usage:
    NONE                                = 0,
    SHADER_RESOURCE                     = NriBit(0),    // SHADER_RESOURCE                          Read-only shader resource view (SRV)
    SHADER_RESOURCE_STORAGE             = NriBit(1),    // SHADER_RESOURCE_STORAGE                  Read/write shader resource view (UAV)
    COLOR_ATTACHMENT                    = NriBit(2),    // COLOR_ATTACHMENT                         Color attachment (render target)
    DEPTH_STENCIL_ATTACHMENT            = NriBit(3),    // DEPTH_STENCIL_ATTACHMENT_READ/WRITE      Depth-stencil attachment (depth-stencil target)
    SHADING_RATE_ATTACHMENT             = NriBit(4),    // SHADING_RATE_ATTACHMENT                  Shading rate attachment (source)
    INPUT_ATTACHMENT                    = NriBit(5)     // INPUT_ATTACHMENT                         Subpass input (read on-chip tile cache)
);

// https://docs.vulkan.org/refpages/latest/refpages/source/VkBufferUsageFlagBits.html
NriBits(BufferUsageBits, uint16_t,                  // Min compatible access:                   Usage:
    NONE                                = 0,
    SHADER_RESOURCE                     = NriBit(0),    // SHADER_RESOURCE                          Read-only shader resource view (SRV)
    SHADER_RESOURCE_STORAGE             = NriBit(1),    // SHADER_RESOURCE_STORAGE                  Read/write shader resource view (UAV)
    VERTEX_BUFFER                       = NriBit(2),    // VERTEX_BUFFER                            Vertex buffer
    INDEX_BUFFER                        = NriBit(3),    // INDEX_BUFFER                             Index buffer
    CONSTANT_BUFFER                     = NriBit(4),    // CONSTANT_BUFFER                          Constant buffer (D3D11: can't be combined with other usages)
    ARGUMENT_BUFFER                     = NriBit(5),    // ARGUMENT_BUFFER                          Argument buffer in "Indirect" commands
    SCRATCH_BUFFER                      = NriBit(6),    // SCRATCH_BUFFER                           Scratch buffer in "CmdBuild*" commands
    SHADER_BINDING_TABLE                = NriBit(7),    // SHADER_BINDING_TABLE                     Shader binding table (SBT) in "CmdDispatchRays*" commands
    ACCELERATION_STRUCTURE_BUILD_INPUT  = NriBit(8),    // SHADER_RESOURCE                          Read-only input in "CmdBuildAccelerationStructures" command
    ACCELERATION_STRUCTURE_STORAGE      = NriBit(9),    // ACCELERATION_STRUCTURE_READ/WRITE        (INTERNAL) acceleration structure storage
    MICROMAP_BUILD_INPUT                = NriBit(10),   // SHADER_RESOURCE                          Read-only input in "CmdBuildMicromaps" command
    MICROMAP_STORAGE                    = NriBit(11)    // MICROMAP_READ/WRITE                      (INTERNAL) micromap storage
);

NriStruct(TextureDesc) {
    Nri(TextureType) type;
    Nri(TextureUsageBits) usage;
    Nri(Format) format;
    Nri(Dim_t) width;
    NriOptional Nri(Dim_t) height;
    NriOptional Nri(Dim_t) depth;
    NriOptional Nri(Dim_t) mipNum;
    NriOptional Nri(Dim_t) layerNum;
    NriOptional Nri(Sample_t) sampleNum;
    NriOptional Nri(SharingMode) sharingMode;
    NriOptional Nri(ClearValue) optimizedClearValue;    // D3D12: not needed on desktop, since any HW can track many clear values
};

// - VK: buffers are always created with sharing mode "CONCURRENT" to match D3D12 spec
// - "structureStride" values:
//   - 0  - allows only "typed" views
//          WGPU: typed buffer views are unsupported
//   - 4  - allows "typed", "byte address" and "structured" views
//          D3D11: allows to create multiple "structured" views for a single resource, disobeying the spec
//   - >4 - allows only "structured" views
//          D3D11: locks this buffer to a single "structured" layout
NriStruct(BufferDesc) {
    uint64_t size;
    uint32_t structureStride;
    Nri(BufferUsageBits) usage;
};

#pragma endregion

//============================================================================================================================================================================================
#pragma region [ Resources: binding to memory ]
//============================================================================================================================================================================================

// Contains some encoded implementation specific details
typedef uint32_t Nri(MemoryType);

// https://learn.microsoft.com/en-us/windows/win32/api/d3d12/ne-d3d12-d3d12_heap_type
NriEnum(MemoryLocation, uint8_t,
    DEVICE,
    DEVICE_UPLOAD, // soft fallback to "HOST_UPLOAD" if "deviceUploadHeapSize = 0"
    HOST_UPLOAD,
    HOST_READBACK
);

// Memory requirements for a resource (buffer or texture)
NriStruct(MemoryDesc) {
    uint64_t size;
    uint32_t alignment;
    Nri(MemoryType) type;
    bool mustBeDedicated; // must be put into a dedicated "Memory" object, containing only 1 object with offset = 0
};

// A group of non-dedicated "MemoryDesc"s of the SAME "MemoryType" can be merged into a single memory allocation
NriStruct(AllocateMemoryDesc) {
    uint64_t size;
    Nri(MemoryType) type;

    // https://learn.microsoft.com/en-us/windows/win32/direct3d12/residency
    // https://learn.microsoft.com/en-us/windows/win32/api/d3d12/ne-d3d12-d3d12_residency_priority
    // https://docs.vulkan.org/refpages/latest/refpages/source/VkMemoryPriorityAllocateInfoEXT.html
    float priority; // [-1; 1]: low < 0, normal = 0, high > 0

    // Memory allocation goes through "AMD Virtual Memory Allocator"
    //  - most likely a sub-allocation from a larger allocation
    //  - alignment is the maximum of all "memoryDesc.alignment" values for all resources bound to this allocation
    //  - https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator
    //  - https://github.com/GPUOpen-LibrariesAndSDKs/D3D12MemoryAllocator
    struct {
        bool enable;
        NriOptional uint32_t alignment; // by default worst-case alignment applied
    } vma;

    // If "false", may reduce alignment requirements
    bool allowMultisampleTextures;
};

// Binding resources to a memory (resources can overlap, i.e. alias)
NriStruct(BindBufferMemoryDesc) {
    NriPtr(Buffer) buffer;
    NriPtr(Memory) memory;
    uint64_t offset; // in memory
};

NriStruct(BindTextureMemoryDesc) {
    NriPtr(Texture) texture;
    NriPtr(Memory) memory;
    uint64_t offset; // in memory
};

#pragma endregion

//============================================================================================================================================================================================
#pragma region [ Resource views and samplers (descriptors) ]
//============================================================================================================================================================================================

// https://microsoft.github.io/DirectX-Specs/d3d/ResourceBinding.html#creating-descriptors

NriEnum(TextureView, uint8_t,
    // Shader resources         // HLSL type                        Compatible "DescriptorType"     Compatible "TextureType"
    TEXTURE,                        // Texture[1D/2D/3D](MS)            TEXTURE                         1D, 2D, 3D
    TEXTURE_ARRAY,                  // Texture[1D/2D](MS)Array          TEXTURE                         1D, 2D
    TEXTURE_CUBE,                   // TextureCube                      TEXTURE                             2D
    TEXTURE_CUBE_ARRAY,             // TextureCubeArray                 TEXTURE                             2D
    STORAGE_TEXTURE,                // RWTexture[1D/2D/3D](MS)          STORAGE_TEXTURE                 1D, 2D, 3D
    STORAGE_TEXTURE_ARRAY,          // RWTexture[1D/2D](MS)Array        STORAGE_TEXTURE                 1D, 2D
    SUBPASS_INPUT,                  // SubpassInput(MS) (non-array)     INPUT_ATTACHMENT                    2D

    // Host-only
    COLOR_ATTACHMENT,               //                                                                  1D, 2D, 3D
    DEPTH_STENCIL_ATTACHMENT,       //                                                                  1D, 2D
    SHADING_RATE_ATTACHMENT         //                                                                      2D
);

NriEnum(BufferView, uint8_t,
    // Shader resources         // HLSL type                        Compatible "DescriptorType"
    BUFFER,                         // Buffer                           BUFFER
    STRUCTURED_BUFFER,              // StructuredBuffer                 STRUCTURED_BUFFER
    BYTE_ADDRESS_BUFFER,            // ByteAddressBuffer                STRUCTURED_BUFFER
    STORAGE_BUFFER,                 // RWBuffer                         STORAGE_BUFFER
    STORAGE_STRUCTURED_BUFFER,      // RWStructuredBuffer               STORAGE_STRUCTURED_BUFFER
    STORAGE_BYTE_ADDRESS_BUFFER,    // RWByteAddressBuffer              STORAGE_STRUCTURED_BUFFER
    CONSTANT_BUFFER                 // ConstantBuffer                   CONSTANT_BUFFER
);

// https://docs.vulkan.org/refpages/latest/refpages/source/VkFilter.html
// https://docs.vulkan.org/refpages/latest/refpages/source/VkSamplerMipmapMode.html
// https://learn.microsoft.com/en-us/windows/win32/api/d3d12/ne-d3d12-d3d12_filter
NriEnum(Filter, uint8_t,
    NEAREST,
    LINEAR
);

// https://learn.microsoft.com/en-us/windows/win32/api/d3d12/ne-d3d12-d3d12_filter_reduction_type
// https://docs.vulkan.org/refpages/latest/refpages/source/VkSamplerReductionMode.html
NriEnum(FilterOp, uint8_t,
    AVERAGE,    // a weighted average (sum) of values in the footprint (default)
    MIN,        // a component-wise minimum of values in the footprint with non-zero weights, requires "features.filterOpMinMax"
    MAX         // a component-wise maximum of values in the footprint with non-zero weights, requires "features.filterOpMinMax"
);

// https://docs.vulkan.org/refpages/latest/refpages/source/VkSamplerAddressMode.html
// https://learn.microsoft.com/en-us/windows/win32/api/d3d12/ne-d3d12-d3d12_texture_address_mode
NriEnum(AddressMode, uint8_t,
    REPEAT,
    MIRRORED_REPEAT,
    CLAMP_TO_EDGE,

    // WGPU: unsupported
    CLAMP_TO_BORDER,
    MIRROR_CLAMP_TO_EDGE
);

// https://docs.vulkan.org/refpages/latest/refpages/source/VkCompareOp.html
// https://learn.microsoft.com/en-us/windows/win32/api/d3d12/ne-d3d12-d3d12_comparison_func
// R - fragment depth, stencil reference or "SampleCmp" reference
// D - depth or stencil buffer
NriEnum(CompareOp, uint8_t,
    NONE,                       // test is disabled
    ALWAYS,                     // true
    NEVER,                      // false
    EQUAL,                      // R == D
    NOT_EQUAL,                  // R != D
    LESS,                       // R < D
    LESS_EQUAL,                 // R <= D
    GREATER,                    // R > D
    GREATER_EQUAL               // R >= D
);

// https://docs.vulkan.org/refpages/latest/refpages/source/VkComponentSwizzle.html
// https://learn.microsoft.com/en-us/windows/win32/api/d3d12/ne-d3d12-d3d12_shader_component_mapping
NriEnum(ComponentSwizzle, uint8_t,
    IDENTITY,                   // format-specific default

    // Requires "features.componentSwizzle"
    ZERO,                       // 0
    ONE,                        // 1 or 1.0
    R,                          // .x component (red)
    G,                          // .y component (green)
    B,                          // .z component (blue)
    A                           // .w component (alpha)
);

NriStruct(ComponentMapping) {
    // Only for non-"STORAGE" views
    Nri(ComponentSwizzle) r;
    Nri(ComponentSwizzle) g;
    Nri(ComponentSwizzle) b;
    Nri(ComponentSwizzle) a;
};

NriStruct(TextureViewDesc) {
    const NriPtr(Texture) texture;
    Nri(TextureView) type;
    Nri(Format) format;
    Nri(Dim_t) mipOffset;
    Nri(Dim_t) mipNum;                      // can be "REMAINING"
    Nri(Dim_t) layerOffset;
    Nri(Dim_t) layerNum;                    // can be "REMAINING"
    Nri(Dim_t) sliceOffset;
    Nri(Dim_t) sliceNum;                    // can be "REMAINING"
    Nri(PlaneBits) planes;                  // accessible planes (missing planes for a "DEPTH_STENCIL_ATTACHMENT" are considered read-only)
    Nri(ComponentMapping) components;
};

NriStruct(BufferViewDesc) {
    const NriPtr(Buffer) buffer;
    Nri(BufferView) type;
    uint64_t offset;                        // expects "memoryAlignment.bufferShaderResourceOffset" for shader resources
    uint64_t size;                          // can be "WHOLE_SIZE"
    NriOptional Nri(Format) format;         // needed for typed views, i.e. "BUFFER" and "STORAGE_BUFFER"
    NriOptional uint32_t structureStride;   // needed for structured views, i.e. "STRUCTURED_BUFFER" and "STORAGE_STRUCTURED_BUFFER" (= "BufferDesc::structureStride", if not provided)
};

NriStruct(AddressModes) {
    Nri(AddressMode) u, v, w;
};

NriStruct(Filters) {
    Nri(Filter) min, mag, mip;
    Nri(FilterOp) op;
};

// https://docs.vulkan.org/refpages/latest/refpages/source/VkSamplerCreateInfo.html
// https://learn.microsoft.com/en-us/windows/win32/api/d3d12/ns-d3d12-d3d12_sampler_desc
NriStruct(SamplerDesc) {
    Nri(Filters) filters;
    uint8_t anisotropy;
    float mipBias;
    float mipMin;
    float mipMax;
    Nri(AddressModes) addressModes;
    Nri(CompareOp) compareOp;
    Nri(Color) borderColor; // used only with "AddressMode::CLAMP_TO_BORDER"
    bool isInteger;
    bool unnormalizedCoordinates; // requires "shaderFeatures.unnormalizedCoordinates"
};

#pragma endregion

//============================================================================================================================================================================================
#pragma region [ Pipeline layout and descriptors management ]
//============================================================================================================================================================================================

// https://docs.vulkan.org/refpages/latest/refpages/source/VkPipelineBindPoint.html
NriEnum(BindPoint, uint8_t,
    INHERIT, // inherit from the last "CmdSetPipelineLayout" call
    GRAPHICS,
    COMPUTE,
    RAY_TRACING
);

NriBits(PipelineLayoutBits, uint8_t,
    NONE                                    = 0,
    IGNORE_GLOBAL_SPIRV_OFFSETS             = NriBit(0),    // VK: ignore "DeviceCreationDesc::vkBindingOffsets"
    ENABLE_DRAW_PARAMETERS_EMULATION        = NriBit(1),    // D3D12: enable draw parameters emulation, requires "shaderFeatures.drawParameters"
    ENABLE_DRAW_INDEX_EMULATION             = NriBit(2),    // D3D12: enable draw index emulation, requires "shaderFeatures.drawIndex"

    // https://github.com/Microsoft/DirectXShaderCompiler/blob/main/docs/SPIR-V.rst#resourcedescriptorheaps--samplerdescriptorheaps
    // Default VK bindings can be changed via "-fvk-bind-sampler-heap" and "-fvk-bind-resource-heap" DXC options
    SAMPLER_HEAP_DIRECTLY_INDEXED           = NriBit(3),    // requires "shaderModel >= 66"
    RESOURCE_HEAP_DIRECTLY_INDEXED          = NriBit(4)     // requires "shaderModel >= 66"
);

NriBits(DescriptorPoolBits, uint8_t,
    NONE                                    = 0,
    ALLOW_UPDATE_AFTER_SET                  = NriBit(0)     // allows "DescriptorSetBits::ALLOW_UPDATE_AFTER_SET"
);

NriBits(DescriptorSetBits, uint8_t,
    NONE                                    = 0,
    ALLOW_UPDATE_AFTER_SET                  = NriBit(0)     // allows "DescriptorRangeBits::ALLOW_UPDATE_AFTER_SET"
);

// https://docs.vulkan.org/refpages/latest/refpages/source/VkDescriptorBindingFlagBits.html
NriBits(DescriptorRangeBits, uint8_t,
    NONE                                    = 0,
    PARTIALLY_BOUND                         = NriBit(0),    // descriptors in range may not contain valid descriptors at the time the descriptors are consumed (but referenced descriptors must be valid)
    ARRAY                                   = NriBit(1),    // descriptors in range are organized into an array
    VARIABLE_SIZED_ARRAY                    = NriBit(2),    // descriptors in range are organized into a variable-sized array, which size is specified via "variableDescriptorNum" argument of "AllocateDescriptorSets" function

    // https://docs.vulkan.org/samples/latest/samples/extensions/descriptor_indexing/README.html#_update_after_bind_streaming_descriptors_concurrently
    // WGPU: true "update after set" is unsupported because bind groups are immutable; "update + rebind" can work, but previously recorded commands can't be patched
    ALLOW_UPDATE_AFTER_SET                  = NriBit(3)     // descriptors in range can be updated after "CmdSetDescriptorSet" but before "QueueSubmit", also works as "DATA_VOLATILE"
);

// https://docs.vulkan.org/refpages/latest/refpages/source/VkDescriptorType.html
NriEnum(DescriptorType, uint8_t,
                            // Typed   HLSL reg    Compatible resources
    // Sampler heap
    SAMPLER,                    // -        s           sampler

    // Resource heap
    // - a mutable descriptor is a proxy "union" descriptor for all resource descriptor types, i.e. non-sampler
    // - a mutable descriptor can't be created, it can only be allocated from a pool (i.e. used in a "DescriptorRangeDesc")
    // - a mutable descriptor must "mutate" to any resource descriptor via "UpdateDescriptorRanges" or "CopyDescriptorRanges"
    // - a mutable descriptor range may include any non-sampler descriptors, which may be directly indexed in shaders
    MUTABLE,                    // -        -           any non-sampler

    // Optimized resources
    TEXTURE,                    // +        t           TextureView: TEXTURE, TEXTURE_ARRAY, TEXTURE_CUBE, TEXTURE_CUBE_ARRAY
    STORAGE_TEXTURE,            // +        u           TextureView: STORAGE_TEXTURE, STORAGE_TEXTURE_ARRAY
    INPUT_ATTACHMENT,           // +        -           TextureView: SUBPASS_INPUT

    BUFFER,                     // +        t           BufferView: BUFFER
    STORAGE_BUFFER,             // +        u           BufferView: STORAGE_BUFFER
    CONSTANT_BUFFER,            // -        b           BufferView: CONSTANT_BUFFER
    STRUCTURED_BUFFER,          // -        t           BufferView: STRUCTURED_BUFFER, BYTE_ADDRESS_BUFFER
    STORAGE_STRUCTURED_BUFFER,  // -        u           BufferView: STORAGE_STRUCTURED_BUFFER, STORAGE_BYTE_ADDRESS_BUFFER

    ACCELERATION_STRUCTURE      // -        t           acceleration structure, requires "features.rayTracing"
);

// "DescriptorRange" consists of "Descriptor" entities
NriStruct(DescriptorRangeDesc) {
    uint32_t baseRegisterIndex;         // "VKBindingOffsets" not applied to "MUTABLE" and "INPUT_ATTACHMENT" to avoid confusion
    uint32_t descriptorNum;             // treated as max size if "VARIABLE_SIZED_ARRAY" flag is set
    Nri(DescriptorType) descriptorType;
    Nri(StageBits) shaderStages;
    Nri(DescriptorRangeBits) flags;
};

// "DescriptorSet" consists of "DescriptorRange" entities
NriStruct(DescriptorSetDesc) {
    uint32_t registerSpace;             // must be unique, avoid big gaps
    const NriPtr(DescriptorRangeDesc) ranges;
    uint32_t rangeNum;
    Nri(DescriptorSetBits) flags;
};

// "PipelineLayout" consists of "DescriptorSet" descriptions and root parameters
NriStruct(RootConstantDesc) {           // aka push constants block
    uint32_t registerIndex;
    uint32_t size;
    Nri(StageBits) shaderStages;
};

NriStruct(RootDescriptorDesc) {         // aka push descriptor
    uint32_t registerIndex;
    Nri(DescriptorType) descriptorType; // a non-typed descriptor type
    Nri(StageBits) shaderStages;
};

// https://learn.microsoft.com/en-us/windows/win32/direct3d12/root-signature-limits#static-samplers
NriStruct(RootSamplerDesc) {            // aka static (immutable) sampler
    uint32_t registerIndex;
    Nri(SamplerDesc) desc;
    Nri(StageBits) shaderStages;
};

// https://docs.vulkan.org/refpages/latest/refpages/source/VkPipelineLayoutCreateInfo.html
// https://microsoft.github.io/DirectX-Specs/d3d/ResourceBinding.html#root-signature
// https://microsoft.github.io/DirectX-Specs/d3d/ResourceBinding.html#root-signature-version-11
/*
All indices are local in the currently bound pipeline layout. Pipeline layout example:
    RootConstantDesc                #0          // "rootConstantIndex" - an index in "rootConstants" in the currently bound pipeline layout
    ...

    RootDescriptorDesc              #0          // "rootDescriptorIndex" - an index in "rootDescriptors" in the currently bound pipeline layout
    ...

    RootSamplerDesc                 #0
    ...

    Descriptor set                  #0          // "setIndex" - a descriptor set index in the pipeline layout, provided as an argument or bound to the pipeline
        Descriptor range                #0      // "rangeIndex" - a descriptor range index in the descriptor set
            Descriptor num                  N   // "descriptorIndex" and "baseDescriptor" - a descriptor (base) index in the descriptor range, i.e. sub-range start
        ...
    ...
*/
NriStruct(PipelineLayoutDesc) {
    uint32_t rootRegisterSpace;         // must be unique, avoid big gaps
    const NriPtr(RootConstantDesc) rootConstants;
    uint32_t rootConstantNum;
    const NriPtr(RootDescriptorDesc) rootDescriptors;
    uint32_t rootDescriptorNum;
    const NriPtr(RootSamplerDesc) rootSamplers;
    uint32_t rootSamplerNum;
    const NriPtr(DescriptorSetDesc) descriptorSets;
    uint32_t descriptorSetNum;
    Nri(StageBits) shaderStages;
    Nri(PipelineLayoutBits) flags;
};

// Descriptor pool
// https://learn.microsoft.com/en-us/windows/win32/direct3d12/descriptor-heaps
// https://learn.microsoft.com/en-us/windows/win32/api/d3d12/ns-d3d12-d3d12_descriptor_heap_desc
// https://docs.vulkan.org/refpages/latest/refpages/source/VkDescriptorPoolCreateInfo.html
NriStruct(DescriptorPoolDesc) {
    // Maximum number of descriptor sets that can be allocated from this pool
    uint32_t descriptorSetMaxNum;

    // Resource heap
    // - may be directly indexed in shaders via "RESOURCE_HEAP_DIRECTLY_INDEXED" pipeline layout flag
    // - https://docs.vulkan.org/features/latest/features/proposals/VK_EXT_mutable_descriptor_type.html
    uint32_t mutableMaxNum;                 // number of "MUTABLE" descriptors, requires "features.mutableDescriptorType"

    // Sampler heap
    // - may be directly indexed in shaders via "SAMPLER_HEAP_DIRECTLY_INDEXED" pipeline layout flag
    // - root samplers do not count (not allocated from a descriptor pool)
    uint32_t samplerMaxNum;                 // number of "SAMPLER" descriptors

    // Optimized resources (may have various sizes depending on Vulkan implementation)
    uint32_t constantBufferMaxNum;          // number of "CONSTANT_BUFFER" descriptors
    uint32_t textureMaxNum;                 // number of "TEXTURE" descriptors
    uint32_t storageTextureMaxNum;          // number of "STORAGE_TEXTURE" descriptors
    uint32_t bufferMaxNum;                  // number of "BUFFER" descriptors
    uint32_t storageBufferMaxNum;           // number of "STORAGE_BUFFER" descriptors
    uint32_t structuredBufferMaxNum;        // number of "STRUCTURED_BUFFER" descriptors
    uint32_t storageStructuredBufferMaxNum; // number of "STORAGE_STRUCTURED_BUFFER" descriptors
    uint32_t accelerationStructureMaxNum;   // number of "ACCELERATION_STRUCTURE" descriptors, requires "features.rayTracing"
    uint32_t inputAttachmentMaxNum;         // number of "INPUT_ATTACHMENT" descriptors

    Nri(DescriptorPoolBits) flags;
};

// Updating/initializing descriptors in a descriptor set
NriStruct(UpdateDescriptorRangeDesc) {
    // Destination
    NriPtr(DescriptorSet) descriptorSet;
    uint32_t rangeIndex;
    uint32_t baseDescriptor;
    // Source & count
    const NriPtr(Descriptor) const* descriptors; // all descriptors must have the same type
    uint32_t descriptorNum;
};

// Copying descriptors between descriptor sets
NriStruct(CopyDescriptorRangeDesc) {
    // Destination
    NriPtr(DescriptorSet) dstDescriptorSet;
    uint32_t dstRangeIndex;
    uint32_t dstBaseDescriptor;
    // Source & count
    const NriPtr(DescriptorSet) srcDescriptorSet;
    uint32_t srcRangeIndex;
    uint32_t srcBaseDescriptor;
    uint32_t descriptorNum;         // can be "ALL" (source)
};

// Binding
NriStruct(SetDescriptorSetDesc) {
    uint32_t setIndex;
    const NriPtr(DescriptorSet) descriptorSet;
    NriOptional Nri(BindPoint) bindPoint;
};

NriStruct(SetRootConstantsDesc) {   // requires "pipelineLayoutRootConstantMaxSize > 0"
    uint32_t rootConstantIndex;
    const void* data;
    uint32_t size;
    uint32_t offset;                // requires "features.rootConstantsOffset"
    NriOptional Nri(BindPoint) bindPoint;
};

NriStruct(SetRootDescriptorDesc) {  // requires "pipelineLayoutRootDescriptorMaxNum > 0"
    uint32_t rootDescriptorIndex;
    NriPtr(Descriptor) descriptor;
    uint32_t offset;                // a non-"CONSTANT_BUFFER" descriptor requires "features.nonConstantBufferRootDescriptorOffset"
    NriOptional Nri(BindPoint) bindPoint;
};

#pragma endregion

//============================================================================================================================================================================================
#pragma region [ Graphics pipeline: input assembly ]
//============================================================================================================================================================================================

NriEnum(IndexType, uint8_t,
    UINT16,
    UINT32
);

NriEnum(PrimitiveRestart, uint8_t,
    DISABLED,
    INDICES_UINT16, // index "0xFFFF" enforces primitive restart
    INDICES_UINT32  // index "0xFFFFFFFF" enforces primitive restart
);

// https://docs.vulkan.org/refpages/latest/refpages/source/VkVertexInputRate.html
NriEnum(VertexStreamStepRate, uint8_t,
    PER_VERTEX,
    PER_INSTANCE
);

// https://docs.vulkan.org/refpages/latest/refpages/source/VkPrimitiveTopology.html
// https://learn.microsoft.com/en-us/windows/win32/api/d3dcommon/ne-d3dcommon-d3d_primitive_topology
// https://learn.microsoft.com/en-us/windows/win32/api/d3d12/ne-d3d12-d3d12_primitive_topology_type
NriEnum(Topology, uint8_t,
    POINT_LIST,
    LINE_LIST,
    LINE_STRIP,
    TRIANGLE_LIST,
    TRIANGLE_STRIP,

    // WGPU: unsupported
    LINE_LIST_WITH_ADJACENCY,
    LINE_STRIP_WITH_ADJACENCY,
    TRIANGLE_LIST_WITH_ADJACENCY,
    TRIANGLE_STRIP_WITH_ADJACENCY,
    PATCH_LIST
);

NriStruct(InputAssemblyDesc) {
    Nri(Topology) topology;
    uint8_t tessControlPointNum;
    Nri(PrimitiveRestart) primitiveRestart;
};

NriStruct(VertexAttributeD3D) {
    const char* semanticName;
    uint32_t semanticIndex;
};

NriStruct(VertexAttributeVK) {
    uint32_t location;
};

NriStruct(VertexAttributeDesc) {
    Nri(VertexAttributeD3D) d3d;
    Nri(VertexAttributeVK) vk;
    uint32_t offset;
    Nri(Format) format;
    uint16_t streamIndex;
};

NriStruct(VertexStreamDesc) {
    uint16_t bindingSlot;
    Nri(VertexStreamStepRate) stepRate;
    NriOptional uint16_t stride; // fallback if "features.extendedDynamicState" is not supported
};

NriStruct(VertexInputDesc) {
    const NriPtr(VertexAttributeDesc) attributes;
    uint8_t attributeNum;
    const NriPtr(VertexStreamDesc) streams;
    uint8_t streamNum;
};

NriStruct(VertexBufferDesc) {
    const NriPtr(Buffer) buffer;
    uint64_t offset;
    uint32_t stride; // requires "features.extendedDynamicState", ignored otherwise, use "VertexStreamDesc::stride" instead
};

#pragma endregion

//============================================================================================================================================================================================
#pragma region [ Graphics pipeline: rasterization ]
//============================================================================================================================================================================================

// https://docs.vulkan.org/refpages/latest/refpages/source/VkPolygonMode.html
// https://learn.microsoft.com/en-us/windows/win32/api/d3d12/ne-d3d12-d3d12_fill_mode
NriEnum(FillMode, uint8_t,
    SOLID,
    WIREFRAME
);

// https://docs.vulkan.org/refpages/latest/refpages/source/VkCullModeFlagBits.html
// https://learn.microsoft.com/en-us/windows/win32/api/d3d12/ne-d3d12-d3d12_cull_mode
NriEnum(CullMode, uint8_t,
    NONE,
    FRONT,
    BACK
);

// https://docs.vulkan.org/samples/latest/samples/extensions/fragment_shading_rate_dynamic/README.html
// https://microsoft.github.io/DirectX-Specs/d3d/VariableRateShading.html
NriEnum(ShadingRate, uint8_t,
    FRAGMENT_SIZE_1X1,
    FRAGMENT_SIZE_1X2,
    FRAGMENT_SIZE_2X1,
    FRAGMENT_SIZE_2X2,

    // Require "features.additionalShadingRates"
    FRAGMENT_SIZE_2X4,
    FRAGMENT_SIZE_4X2,
    FRAGMENT_SIZE_4X4
);

// https://docs.vulkan.org/refpages/latest/refpages/source/VkFragmentShadingRateCombinerOpKHR.html
// https://learn.microsoft.com/en-us/windows/win32/api/d3d12/ne-d3d12-d3d12_shading_rate_combiner
//    "primitiveCombiner"      "attachmentCombiner"
// A   Pipeline shading rate    Result of Op1
// B   Primitive shading rate   Attachment shading rate
NriEnum(ShadingRateCombiner, uint8_t,
    KEEP,       // A

    // Requires "tiers.shadingRate >= 2"
    REPLACE,    // B
    MIN,        // min(A, B)
    MAX,        // max(A, B)

    // Requires "features.sumShadingRateCombiner"
    SUM         // (A + B) or (A * B)
);

/*
https://registry.khronos.org/vulkan/specs/latest/html/vkspec.html#primsrast-depthbias-computation
https://learn.microsoft.com/en-us/windows/win32/direct3d11/d3d10-graphics-programming-guide-output-merger-stage-depth-bias
R - minimum resolvable difference
S - maximum slope

bias = constant * R + slopeFactor * S
if (clamp > 0)
    bias = min(bias, clamp)
else if (clamp < 0)
    bias = max(bias, clamp)

enabled if constant != 0 or slope != 0
*/
NriStruct(DepthBiasDesc) {
    float constant;
    float clamp;
    float slope;
};

NriStruct(RasterizationDesc) {
    Nri(DepthBiasDesc) depthBias;
    Nri(FillMode) fillMode;
    Nri(CullMode) cullMode;
    bool frontCounterClockwise;
    bool depthClamp;
    bool lineSmoothing;         // requires "features.lineSmoothing"
    bool conservativeRaster;    // requires "tiers.conservativeRaster != 0"
    bool shadingRate;           // requires "tiers.shadingRate != 0", expects "CmdSetShadingRate" and optionally "RenderingDesc::shadingRate"
};

NriStruct(MultisampleDesc) {
    uint32_t sampleMask;        // can be "ALL"
    Nri(Sample_t) sampleNum;
    bool alphaToCoverage;
    bool sampleLocations;       // requires "tiers.sampleLocations != 0", expects "CmdSetSampleLocations"
};

NriStruct(ShadingRateDesc) {
    Nri(ShadingRate) shadingRate;
    Nri(ShadingRateCombiner) primitiveCombiner;
    Nri(ShadingRateCombiner) attachmentCombiner;
};

#pragma endregion

//============================================================================================================================================================================================
#pragma region [ Graphics pipeline: output merger ]
//============================================================================================================================================================================================

NriEnum(Multiview, uint8_t,
    // Destination "viewport" and/or "layer" must be set in shaders explicitly, "viewMask" for rendering can be < than the one used for pipeline creation (D3D12 style)
    FLEXIBLE,       // requires "features.flexibleMultiview"

    // View instances go to statically assigned corresponding attachment layers, "viewMask" for rendering must match the one used for pipeline creation (VK style)
    LAYER_BASED,    // requires "features.layerBasedMultiview"

    // View instances go to statically assigned corresponding viewports, "viewMask" for pipeline creation is unused (D3D11 style)
    VIEWPORT_BASED  // requires "features.viewportBasedMultiview"
);

// https://docs.vulkan.org/refpages/latest/refpages/source/VkLogicOp.html
// https://learn.microsoft.com/en-us/windows/win32/api/d3d12/ne-d3d12-d3d12_logic_op
// S - source color 0
// D - destination color
NriEnum(LogicOp, uint8_t,
    NONE,
    CLEAR,                      // 0
    AND,                        // S & D
    AND_REVERSE,                // S & ~D
    COPY,                       // S
    AND_INVERTED,               // ~S & D
    XOR,                        // S ^ D
    OR,                         // S | D
    NOR,                        // ~(S | D)
    EQUIVALENT,                 // ~(S ^ D)
    INVERT,                     // ~D
    OR_REVERSE,                 // S | ~D
    COPY_INVERTED,              // ~S
    OR_INVERTED,                // ~S | D
    NAND,                       // ~(S & D)
    SET                         // 1
);

// https://docs.vulkan.org/refpages/latest/refpages/source/VkStencilOp.html
// https://learn.microsoft.com/en-us/windows/win32/api/d3d12/ne-d3d12-d3d12_stencil_op
// R - reference, set by "CmdSetStencilReference"
// D - stencil buffer
NriEnum(StencilOp, uint8_t,
    KEEP,                       // D = D
    ZERO,                       // D = 0
    REPLACE,                    // D = R
    INCREMENT_AND_CLAMP,        // D = min(D++, 255)
    DECREMENT_AND_CLAMP,        // D = max(D--, 0)
    INVERT,                     // D = ~D
    INCREMENT_AND_WRAP,         // D++
    DECREMENT_AND_WRAP          // D--
);

// https://docs.vulkan.org/refpages/latest/refpages/source/VkBlendFactor.html
// https://learn.microsoft.com/en-us/windows/win32/api/d3d12/ne-d3d12-d3d12_blend
// S0 - source color 0
// S1 - source color 1
// D - destination color
// C - blend constants, set by "CmdSetBlendConstants"
NriEnum(BlendFactor, uint8_t,   // RGB                               ALPHA
    ZERO,                       // 0                                 0
    ONE,                        // 1                                 1
    SRC_COLOR,                  // S0.r, S0.g, S0.b                  S0.a
    ONE_MINUS_SRC_COLOR,        // 1 - S0.r, 1 - S0.g, 1 - S0.b      1 - S0.a
    DST_COLOR,                  // D.r, D.g, D.b                     D.a
    ONE_MINUS_DST_COLOR,        // 1 - D.r, 1 - D.g, 1 - D.b         1 - D.a
    SRC_ALPHA,                  // S0.a                              S0.a
    ONE_MINUS_SRC_ALPHA,        // 1 - S0.a                          1 - S0.a
    DST_ALPHA,                  // D.a                               D.a
    ONE_MINUS_DST_ALPHA,        // 1 - D.a                           1 - D.a
    CONSTANT_COLOR,             // C.r, C.g, C.b                     C.a
    ONE_MINUS_CONSTANT_COLOR,   // 1 - C.r, 1 - C.g, 1 - C.b         1 - C.a
    CONSTANT_ALPHA,             // C.a                               C.a
    ONE_MINUS_CONSTANT_ALPHA,   // 1 - C.a                           1 - C.a
    SRC_ALPHA_SATURATE,         // min(S0.a, 1 - D.a)                1
    SRC1_COLOR,                 // S1.r, S1.g, S1.b                  S1.a
    ONE_MINUS_SRC1_COLOR,       // 1 - S1.r, 1 - S1.g, 1 - S1.b      1 - S1.a
    SRC1_ALPHA,                 // S1.a                              S1.a
    ONE_MINUS_SRC1_ALPHA        // 1 - S1.a                          1 - S1.a
);

// https://docs.vulkan.org/refpages/latest/refpages/source/VkBlendOp.html
// https://learn.microsoft.com/en-us/windows/win32/api/d3d12/ne-d3d12-d3d12_blend_op
// S - source color
// D - destination color
// Sf - source factor, produced by "BlendFactor"
// Df - destination factor, produced by "BlendFactor"
NriEnum(BlendOp, uint8_t,
    ADD,                        // S * Sf + D * Df
    SUBTRACT,                   // S * Sf - D * Df
    REVERSE_SUBTRACT,           // D * Df - S * Sf
    MIN,                        // min(S, D)
    MAX                         // max(S, D)
);

// https://docs.vulkan.org/refpages/latest/refpages/source/VkColorComponentFlagBits.html
NriBits(ColorWriteBits, uint8_t,
    NONE    = 0,
    R       = NriBit(0),
    G       = NriBit(1),
    B       = NriBit(2),
    A       = NriBit(3),

    RGB     = NriMember(ColorWriteBits, R) // "wingdi.h" must not be included after
            | NriMember(ColorWriteBits, G)
            | NriMember(ColorWriteBits, B),

    RGBA    = NriMember(ColorWriteBits, RGB)
            | NriMember(ColorWriteBits, A)
);

// https://docs.vulkan.org/refpages/latest/refpages/source/VkStencilOpState.html
// https://learn.microsoft.com/en-us/windows/win32/api/d3d12/ns-d3d12-d3d12_depth_stencil_desc
NriStruct(StencilDesc) {
    Nri(CompareOp) compareOp; // "compareOp != NONE", expects "CmdSetStencilReference"
    Nri(StencilOp) failOp;
    Nri(StencilOp) passOp;
    Nri(StencilOp) depthFailOp;
    uint8_t writeMask;
    uint8_t compareMask;
};

// https://docs.vulkan.org/refpages/latest/refpages/source/VkPipelineDepthStencilStateCreateInfo.html
NriStruct(DepthAttachmentDesc) {
    Nri(CompareOp) compareOp;
    bool write;
    bool boundsTest; // requires "features.depthBoundsTest", expects "CmdSetDepthBounds"
};

NriStruct(StencilAttachmentDesc) {
    Nri(StencilDesc) front;
    Nri(StencilDesc) back; // requires "features.independentFrontAndBackStencilReferenceAndMasks" for "back.writeMask"
};

// https://docs.vulkan.org/refpages/latest/refpages/source/VkPipelineColorBlendAttachmentState.html
// https://learn.microsoft.com/en-us/windows/win32/api/d3d12/ns-d3d12-d3d12_render_target_blend_desc
NriStruct(BlendDesc) {
    Nri(BlendFactor) srcFactor;
    Nri(BlendFactor) dstFactor;
    Nri(BlendOp) op;
};

NriStruct(ColorAttachmentDesc) {
    Nri(Format) format;
    Nri(BlendDesc) colorBlend;
    Nri(BlendDesc) alphaBlend;
    Nri(ColorWriteBits) colorWriteMask;
    bool blendEnabled;
};

NriStruct(OutputMergerDesc) {
    const NriPtr(ColorAttachmentDesc) colors;
    uint32_t colorNum;
    Nri(DepthAttachmentDesc) depth;
    Nri(StencilAttachmentDesc) stencil;
    Nri(Format) depthStencilFormat;
    Nri(LogicOp) logicOp;                   // requires "features.logicOp"
    NriOptional uint32_t viewMask;          // if non-0, requires "viewMaxNum > 1"
    NriOptional Nri(Multiview) multiview;   // if "viewMask != 0", requires "features.(xxx)Multiview"
};

#pragma endregion

//============================================================================================================================================================================================
#pragma region [ Pipelines ]
//============================================================================================================================================================================================

// https://docs.vulkan.org/guide/latest/robustness.html
NriEnum(Robustness, uint8_t,
    DEFAULT,        // don't care, follow device settings (VK level when used on a device)
    OFF,            // no overhead, no robust access (out-of-bounds access is not allowed)
    VK,             // minimal overhead, partial robust access
    D3D12           // moderate overhead, D3D12-level robust access (requires "VK_EXT_robustness2", soft fallback to VK mode)
);

NriBits(GraphicsPipelineBits, uint8_t,
    NONE                = 0,
    FAIL_ON_CACHE_MISS  = NriBit(0) // "CreateGraphicsPipeline" returns "FAILURE" if the pipeline is not found in the supplied cache (requires "features.pipelineCacheControl")
);

NriBits(ComputePipelineBits, uint8_t,
    NONE                = 0,
    FAIL_ON_CACHE_MISS  = NriBit(0) // "CreateComputePipeline" returns "FAILURE" if the pipeline is not found in the supplied cache (requires "features.pipelineCacheControl")
);

NriStruct(PipelineCacheDesc) {
    NriOptional const void* data; // "data = NULL" means empty cache
    NriOptional uint64_t size;
};

// It's recommended to use "NRI.hlsl" in the shader code
NriStruct(ShaderDesc) {
    Nri(StageBits) stage;
    const void* bytecode; // see "features.shaderBytecodeXXX"
    uint64_t size;
    NriOptional const char* entryPointName;
};

NriStruct(GraphicsPipelineDesc) {
    const NriPtr(PipelineLayout) pipelineLayout;
    NriOptional const NriPtr(VertexInputDesc) vertexInput;
    Nri(InputAssemblyDesc) inputAssembly;
    Nri(RasterizationDesc) rasterization;
    NriOptional const NriPtr(MultisampleDesc) multisample;
    Nri(OutputMergerDesc) outputMerger;
    const NriPtr(ShaderDesc) shaders;
    uint32_t shaderNum;
    Nri(GraphicsPipelineBits) flags;
    Nri(Robustness) robustness;
    NriOptional const NriPtr(PipelineCache) cache; // if non-NULL, pipeline creation can be served from a cached blob and the result will be added to the cache on a miss
};

NriStruct(ComputePipelineDesc) {
    const NriPtr(PipelineLayout) pipelineLayout;
    Nri(ShaderDesc) shader;
    Nri(ComputePipelineBits) flags;
    Nri(Robustness) robustness;
    NriOptional const NriPtr(PipelineCache) cache; // if non-NULL, pipeline creation can be served from a cached blob and the result will be added to the cache on a miss
};

#pragma endregion

//============================================================================================================================================================================================
#pragma region [ Rendering (render pass) ]
//============================================================================================================================================================================================

// https://docs.vulkan.org/refpages/latest/refpages/source/VK_KHR_dynamic_rendering.html
// https://github.com/KhronosGroup/Vulkan-Docs/blob/main/proposals/VK_KHR_dynamic_rendering_local_read.adoc

// https://learn.microsoft.com/en-us/windows/win32/api/d3d12/ne-d3d12-d3d12_render_pass_beginning_access_type
// https://docs.vulkan.org/refpages/latest/refpages/source/VkAttachmentLoadOp.html
NriEnum(LoadOp, uint8_t,
    LOAD,
    CLEAR
);

// https://learn.microsoft.com/en-us/windows/win32/api/d3d12/ne-d3d12-d3d12_render_pass_ending_access_type
// https://docs.vulkan.org/refpages/latest/refpages/source/VkAttachmentStoreOp.html
NriEnum(StoreOp, uint8_t,
    STORE,
    DISCARD
);

// https://learn.microsoft.com/en-us/windows/win32/api/d3d12/ne-d3d12-d3d12_resolve_mode
// https://docs.vulkan.org/refpages/latest/refpages/source/VkResolveModeFlagBits.html
NriEnum(ResolveOp, uint8_t,
    AVERAGE,    // resolves the source samples to their average value
    MIN,        // resolves the source samples to their minimum value, requires "features.resolveOpMinMax"
    MAX         // resolves the source samples to their maximum value, requires "features.resolveOpMinMax"
);

NriStruct(AttachmentDesc) {
    NriPtr(Descriptor) descriptor;
    Nri(ClearValue) clearValue;
    Nri(LoadOp) loadOp;
    Nri(StoreOp) storeOp;
    Nri(ResolveOp) resolveOp;
    NriOptional NriPtr(Descriptor) resolveDst;          // must be in "COLOR_ATTACHMENT" state and valid during "CmdEndRendering"
};

// If "VK_KHR_dynamic_rendering" is not supported:
// - "VkRenderPass" is used under the hood
// - input attachments must be transitioned to "Layout::INPUT_ATTACHMENT" in the same command buffer before "CmdBeginRendering"
// - matching pipeline input attachment indices are inferred from these transitions
NriStruct(RenderingDesc) {
    const NriPtr(AttachmentDesc) colors;
    uint32_t colorNum;
    Nri(AttachmentDesc) depth;                          // may be treated as "depth-stencil"
    Nri(AttachmentDesc) stencil;                        // (optional) separation is needed for multisample resolve
    NriOptional const NriPtr(Descriptor) shadingRate;   // requires "tiers.shadingRate >= 2"
    NriOptional uint32_t viewMask;                      // if non-0, requires "viewMaxNum > 1"
};

#pragma endregion

//============================================================================================================================================================================================
#pragma region [ Queries ]
//============================================================================================================================================================================================

// https://microsoft.github.io/DirectX-Specs/d3d/CountersAndQueries.html
// https://docs.vulkan.org/refpages/latest/refpages/source/VkQueryType.html
NriEnum(QueryType, uint8_t,
    TIMESTAMP,                              // uint64_t, requires "features.timestamp" (for "GRAPHICS" and "COMPUTE" queues)
    TIMESTAMP_COPY_QUEUE,                   // uint64_t, requires "features.timestampCopyQueue" (for a "COPY" queue)
    OCCLUSION,                              // uint64_t, requires "features.occlusion"
    PIPELINE_STATISTICS,                    // see "PipelineStatisticsDesc", requires "features.pipelineStatistics"
    ACCELERATION_STRUCTURE_SIZE,            // uint64_t, requires "features.rayTracing"
    ACCELERATION_STRUCTURE_COMPACTED_SIZE,  // uint64_t, requires "features.rayTracing"
    MICROMAP_COMPACTED_SIZE                 // uint64_t, requires "features.micromap"
);

NriStruct(QueryPoolDesc) {
    Nri(QueryType) queryType;
    uint32_t capacity;
};

// Data layout for QueryType::PIPELINE_STATISTICS
// https://docs.vulkan.org/refpages/latest/refpages/source/VkQueryPipelineStatisticFlagBits.html
// https://learn.microsoft.com/en-us/windows/win32/api/d3d12/ns-d3d12-d3d12_query_data_pipeline_statistics
NriStruct(PipelineStatisticsDesc) {
    // Common part
    uint64_t inputVertexNum;
    uint64_t inputPrimitiveNum;
    uint64_t vertexShaderInvocationNum;
    uint64_t geometryShaderInvocationNum;
    uint64_t geometryShaderPrimitiveNum;
    uint64_t rasterizerInPrimitiveNum;
    uint64_t rasterizerOutPrimitiveNum;
    uint64_t fragmentShaderInvocationNum;
    uint64_t tessControlShaderInvocationNum;
    uint64_t tessEvaluationShaderInvocationNum;
    uint64_t computeShaderInvocationNum;

    // If "features.meshShaderPipelineStats"
    uint64_t taskShaderInvocationNum;
    uint64_t meshShaderInvocationNum;

    // D3D12: if "features.meshShaderPipelineStats"
    uint64_t meshShaderPrimitiveNum;
};

#pragma endregion

//============================================================================================================================================================================================
#pragma region [ Command signatures ]
//============================================================================================================================================================================================

// To fill commands for indirect drawing in a shader use one of "NRI_FILL_X_DESC" macros

// Command signatures (default)

NriStruct(DrawDesc) {                   // see NRI_FILL_DRAW_DESC
    uint32_t vertexNum;
    uint32_t instanceNum;
    uint32_t baseVertex;                    // vertex buffer offset = CmdSetVertexBuffers.offset + baseVertex * VertexStreamDesc::stride
    uint32_t baseInstance;
};

NriStruct(DrawIndexedDesc) {            // see NRI_FILL_DRAW_INDEXED_DESC
    uint32_t indexNum;
    uint32_t instanceNum;
    uint32_t baseIndex;                     // index buffer offset = CmdSetIndexBuffer.offset + baseIndex * sizeof(CmdSetIndexBuffer.indexType)
    int32_t baseVertex;                     // index += baseVertex
    uint32_t baseInstance;
};

NriStruct(DispatchDesc) {
    uint32_t x, y, z;
};

// Modified draw command signatures, if the bound pipeline layout has "PipelineLayoutBits::ENABLE_DRAW_PARAMETERS_EMULATION"
// "PipelineLayoutBits::ENABLE_DRAW_INDEX_EMULATION" does not change the command layout

NriStruct(DrawBaseDesc) {               // see NRI_FILL_DRAW_DESC
    uint32_t shaderEmulatedBaseVertex;      // root constant
    uint32_t shaderEmulatedBaseInstance;    // root constant
    uint32_t vertexNum;
    uint32_t instanceNum;
    uint32_t baseVertex;                    // vertex buffer offset = CmdSetVertexBuffers.offset + baseVertex * VertexStreamDesc::stride
    uint32_t baseInstance;
};

NriStruct(DrawIndexedBaseDesc) {        // see NRI_FILL_DRAW_INDEXED_DESC
    int32_t shaderEmulatedBaseVertex;       // root constant
    uint32_t shaderEmulatedBaseInstance;    // root constant
    uint32_t indexNum;
    uint32_t instanceNum;
    uint32_t baseIndex;                     // index buffer offset = CmdSetIndexBuffer.offset + baseIndex * sizeof(CmdSetIndexBuffer.indexType)
    int32_t baseVertex;                     // index += baseVertex
    uint32_t baseInstance;
};

#pragma endregion

//============================================================================================================================================================================================
#pragma region [ Other ]
//============================================================================================================================================================================================

// Copy
NriStruct(TextureRegionDesc) {
    Nri(Dim_t) x;
    Nri(Dim_t) y;
    Nri(Dim_t) z;
    Nri(Dim_t) width;       // can be "WHOLE_SIZE" (mip)
    Nri(Dim_t) height;      // can be "WHOLE_SIZE" (mip)
    Nri(Dim_t) depth;       // can be "WHOLE_SIZE" (mip)
    Nri(Dim_t) mipOffset;
    Nri(Dim_t) layerOffset;
    Nri(PlaneBits) planes;
};

NriStruct(TextureDataLayoutDesc) {
    uint64_t offset;        // a buffer offset must be a multiple of "uploadBufferTextureSliceAlignment" (data placement alignment)
    uint32_t rowPitch;      // must be a multiple of "uploadBufferTextureRowAlignment"
    uint32_t slicePitch;    // must be a multiple of "uploadBufferTextureSliceAlignment"
};

// Work submission
NriStruct(FenceSubmitDesc) {
    NriPtr(Fence) fence;
    uint64_t value;
    Nri(StageBits) stages;
};

NriStruct(QueueSubmitDesc) {
    const NriPtr(FenceSubmitDesc) waitFences;
    uint32_t waitFenceNum;
    const NriPtr(CommandBuffer) const* commandBuffers;
    uint32_t commandBufferNum;
    const NriPtr(FenceSubmitDesc) signalFences;
    uint32_t signalFenceNum;
    NriOptional const NriPtr(SwapChain) swapChain; // required if "NRILowLatency" is enabled in the swap chain
};

// Clear
NriStruct(ClearAttachmentDesc) {
    Nri(ClearValue) value;
    Nri(PlaneBits) planes;
    uint8_t colorAttachmentIndex;
};

// Required synchronization
// - variant 1: "SHADER_RESOURCE_STORAGE" access ("SHADER_RESOURCE_STORAGE" layout) and "CLEAR_STORAGE" stage + any shader stage (or "ALL")
// - variant 2: "CLEAR_STORAGE" access ("SHADER_RESOURCE_STORAGE" layout) and "CLEAR_STORAGE" stage
NriStruct(ClearStorageDesc) {
    // For any buffers and textures with integer formats:
    //  - Clears a storage descriptor with bit-precise values, copying the lower "N" bits from "value.[f/ui/i].channel"
    //    to the corresponding channel, where "N" is the number of bits in the "channel" of the resource format
    // For textures with non-integer formats:
    //  - Clears a storage descriptor with float values with format conversion from "FLOAT" to "UNORM/SNORM" where appropriate
    // For buffers:
    //  - To avoid discrepancies in behavior between GAPIs use "R32f/ui/i" formats for views
    //  - D3D: structured buffers are unsupported!
    NriPtr(Descriptor) descriptor;  // a "STORAGE" descriptor
    Nri(Color) value;               // avoid overflow
    uint32_t setIndex;
    uint32_t rangeIndex;
    uint32_t descriptorIndex;
};

#pragma endregion

//============================================================================================================================================================================================
#pragma region [ Device description and capabilities ]
//============================================================================================================================================================================================

NriEnum(Vendor, uint8_t,
    UNKNOWN,
    NVIDIA,
    AMD,
    INTEL
);

// https://docs.vulkan.org/refpages/latest/refpages/source/VkPhysicalDeviceType.html
NriEnum(Architecture, uint8_t,
    UNKNOWN,
    SOFTWARE,   // CPU
    VIRTUAL,    // remote desktop?
    INTEGRATED, // UMA
    DISCRETE    // yes, please!
);

// https://docs.vulkan.org/refpages/latest/refpages/source/VkQueueFlagBits.html
// https://learn.microsoft.com/en-us/windows/win32/api/d3d12/ne-d3d12-d3d12_command_list_type
NriEnum(QueueType, uint8_t,
    GRAPHICS,
    COMPUTE,
    COPY
);

NriStruct(AdapterDesc) {
    char name[256];
    Nri(Uid_t) uid; // "LUID" (preferred) if "uid.high = 0", or "UUID" otherwise
    uint64_t videoMemorySize;
    uint64_t sharedSystemMemorySize;
    uint32_t deviceId;
    uint32_t driverVersion; // GAPI and OS dependent
    uint32_t queueNum[(uint32_t)NriScopedMember(QueueType, MAX_NUM)];
    Nri(Vendor) vendor;
    Nri(Architecture) architecture;
    Nri(GraphicsAPI) supportedGraphicsAPIs;
};

#define NriShaderModel(major, minor) (major * 100 + minor)

// Feature support coverage: https://vulkan.gpuinfo.org/ and https://d3d12infodb.boolka.dev/
NriStruct(DeviceDesc) {
    // Common
    Nri(AdapterDesc) adapterDesc; // "queueNum" reflects available number of queues per "QueueType"
    Nri(GraphicsAPI) graphicsAPI;
    uint16_t nriVersion;
    uint16_t shaderModel; // see "NriShaderModel"

    // Viewport
    struct {
        uint32_t maxNum;
        int32_t boundsMin;
        int32_t boundsMax;
    } viewport;

    // Dimensions
    struct {
        uint32_t typedBufferMaxDim;
        Nri(Dim_t) attachmentMaxDim;
        Nri(Dim_t) attachmentLayerMaxNum;
        Nri(Dim_t) texture1DMaxDim;
        Nri(Dim_t) texture2DMaxDim;
        Nri(Dim_t) texture3DMaxDim;
        Nri(Dim_t) textureLayerMaxNum;
    } dimensions;

    // Precision bits
    struct {
        uint32_t viewportBits;
        uint32_t subPixelBits;
        uint32_t subTexelBits;
        uint32_t mipmapBits;
    } precision;

    // Memory
    struct {
        uint64_t deviceUploadHeapSize;      // ReBAR
        uint64_t bufferMaxSize;
        uint64_t allocationMaxSize;
        uint32_t allocationMaxNum;
        uint32_t samplerAllocationMaxNum;
        uint32_t constantBufferMaxRange;
        uint32_t storageBufferMaxRange;
        uint32_t bufferTextureGranularity;  // specifies a page-like granularity at which linear and non-linear resources must be placed in adjacent memory locations to avoid aliasing
        uint32_t alignmentDefault;          // (INTERNAL) worst-case alignment for a memory allocation respecting all possible placed resources, excluding multisample textures
        uint32_t alignmentMultisample;      // (INTERNAL) worst-case alignment for a memory allocation respecting all possible placed resources, including multisample textures
    } memory;

    // Memory alignment requirements
    struct {
        uint32_t uploadBufferTextureRow;
        uint32_t uploadBufferTextureSlice;
        uint32_t bufferShaderResourceOffset;
        uint32_t constantBufferOffset;
        uint32_t scratchBufferOffset;
        uint32_t shaderBindingTable;
        uint32_t accelerationStructureOffset;
        uint32_t micromapOffset;
    } memoryAlignment;

    // Pipeline layout (see "FitPipelineLayoutSettingsIntoDeviceLimits")
    // D3D12 only: "rootConstantSize" + "descriptorSetNum" * 4 + "rootDescriptorNum" * 8 + "reservedSize" <= 256, where
    // "reservedSize" is 8 bytes for "ENABLE_DRAW_PARAMETERS_EMULATION" and 4 bytes for "ENABLE_DRAW_INDEX_EMULATION"
    struct {
        uint32_t descriptorSetMaxNum;
        uint32_t rootConstantMaxSize;
        uint32_t rootDescriptorMaxNum;
    } pipelineLayout;

    // Descriptor set
    struct {
        uint32_t samplerMaxNum;
        uint32_t constantBufferMaxNum;
        uint32_t storageBufferMaxNum;
        uint32_t textureMaxNum;
        uint32_t storageTextureMaxNum;

        struct {
            uint32_t samplerMaxNum;
            uint32_t constantBufferMaxNum;
            uint32_t storageBufferMaxNum;
            uint32_t textureMaxNum;
            uint32_t storageTextureMaxNum;
        } updateAfterSet;
    } descriptorSet;

    // Shader stages
    struct {
        // Per stage resources
        uint32_t descriptorSamplerMaxNum;
        uint32_t descriptorConstantBufferMaxNum;
        uint32_t descriptorStorageBufferMaxNum;
        uint32_t descriptorTextureMaxNum;
        uint32_t descriptorStorageTextureMaxNum;
        uint32_t resourceMaxNum;

        struct {
            uint32_t descriptorSamplerMaxNum;
            uint32_t descriptorConstantBufferMaxNum;
            uint32_t descriptorStorageBufferMaxNum;
            uint32_t descriptorTextureMaxNum;
            uint32_t descriptorStorageTextureMaxNum;
            uint32_t resourceMaxNum;
        } updateAfterSet;

        // Vertex
        struct {
            uint32_t attributeMaxNum;
            uint32_t streamMaxNum;
            uint32_t outputComponentMaxNum;
        } vertex;

        // Tessellation control
        struct {
            float generationMaxLevel;
            uint32_t patchPointMaxNum;
            uint32_t perVertexInputComponentMaxNum;
            uint32_t perVertexOutputComponentMaxNum;
            uint32_t perPatchOutputComponentMaxNum;
            uint32_t totalOutputComponentMaxNum;
        } tesselationControl;

        // Tessellation evaluation
        struct {
            uint32_t inputComponentMaxNum;
            uint32_t outputComponentMaxNum;
        } tesselationEvaluation;

        // Geometry
        struct {
            uint32_t invocationMaxNum;
            uint32_t inputComponentMaxNum;
            uint32_t outputComponentMaxNum;
            uint32_t outputVertexMaxNum;
            uint32_t totalOutputComponentMaxNum;
        } geometry;

        // Fragment
        struct {
            uint32_t inputComponentMaxNum;
            uint32_t attachmentMaxNum;
            uint32_t dualSourceAttachmentMaxNum;
        } fragment;

        // Compute
        //  - a "dispatch" consists of "work groups" (aka "thread groups")
        //  - a "work group" consists of "waves" (aka "subgroups" or "warps")
        //  - a "wave" consists of "lanes", which can can be active, inactive or a helper:
        //    - active: the "lane" is performing its computations
        //    - inactive: the "lane" is part of the "wave" but is currently masked out
        //    - helper: these "lanes" are executed to provide auxiliary information (like derivatives) for active threads in the same 2x2 quad
        //  - "invocation" (or "thread") is a single shader instance
        //  - "lane" specifically refers to the position of a "thread" within a hardware "wave"
        //  - the concept of "wave/lane" execution applies to all shader stages
        struct {
            uint32_t dispatchMaxDim[3];
            uint32_t workGroupInvocationMaxNum;
            uint32_t workGroupMaxDim[3];
            uint32_t sharedMemoryMaxSize;
        } compute;

        // Task
        struct {
            uint32_t dispatchWorkGroupMaxNum;
            uint32_t dispatchMaxDim[3];
            uint32_t workGroupInvocationMaxNum;
            uint32_t workGroupMaxDim[3];
            uint32_t sharedMemoryMaxSize;
            uint32_t payloadMaxSize;
        } task;

        // Mesh
        struct {
            uint32_t dispatchWorkGroupMaxNum;
            uint32_t dispatchMaxDim[3];
            uint32_t workGroupInvocationMaxNum;
            uint32_t workGroupMaxDim[3];
            uint32_t sharedMemoryMaxSize;
            uint32_t outputVerticesMaxNum;
            uint32_t outputPrimitiveMaxNum;
            uint32_t outputComponentMaxNum;
        } mesh;

        // Ray tracing
        struct {
            uint32_t shaderGroupIdentifierSize;
            uint32_t shaderBindingTableMaxStride;
            uint32_t recursionMaxDepth;
        } rayTracing;
    } shaderStage;

    // Acceleration structure
    struct {
        uint64_t primitiveMaxNum; // per BLAS
        uint64_t geometryMaxNum;  // per BLAS
        uint64_t instanceMaxNum;  // per TLAS
        uint32_t micromapSubdivisionMaxLevel;
    } accelerationStructure;

    // Wave (subgroup)
    // https://github.com/microsoft/directxshadercompiler/wiki/wave-intrinsics
    // https://microsoft.github.io/DirectX-Specs/d3d/HLSL_SM_6_6_Derivatives.html
    struct {
        uint32_t laneMinNum;
        uint32_t laneMaxNum;
        Nri(StageBits) waveOpsStages;       // SM 6.0+ (see "shaderFeatures.waveX")
        Nri(StageBits) quadOpsStages;       // SM 6.0+ (see "shaderFeatures.waveQuad")
        Nri(StageBits) derivativeOpsStages; // SM 6.6+ (https://microsoft.github.io/DirectX-Specs/d3d/HLSL_SM_6_6_Derivatives.html#derivative-functions)
    } wave;

    // Other
    struct {
        uint64_t timestampFrequencyHz;
        uint32_t drawIndirectMaxNum;
        float samplerLodBiasMax;
        float samplerAnisotropyMax;
        int8_t texelGatherOffsetMin;
        int8_t texelOffsetMin;
        uint8_t texelOffsetMax;
        uint8_t texelGatherOffsetMax;
        uint8_t clipDistanceMaxNum;
        uint8_t cullDistanceMaxNum;
        uint8_t combinedClipAndCullDistanceMaxNum;
        uint8_t viewMaxNum;                         // multiview is supported if > 1
        uint8_t shadingRateAttachmentTileSize;      // square size
    } other;

    // Tiers (0 - unsupported)
    struct {
        // https://microsoft.github.io/DirectX-Specs/d3d/ConservativeRasterization.html#tiered-support
        // 1 - 1/2 pixel uncertainty region and does not support post-snap degenerates
        // 2 - reduces the maximum uncertainty region to 1/256 and requires post-snap degenerates not be culled
        // 3 - maintains a maximum 1/256 uncertainty region and adds support for inner input coverage, aka "SV_InnerCoverage"
        uint8_t conservativeRaster;

        // https://microsoft.github.io/DirectX-Specs/d3d/ProgrammableSamplePositions.html#hardware-tiers
        // 1 - a single sample pattern can be specified to repeat for every pixel ("locationNum / sampleNum" ratio must be 1 in "CmdSetSampleLocations"),
        //     1x and 16x sample counts do not support programmable locations
        // 2 - four separate sample patterns can be specified for each pixel in a 2x2 grid ("locationNum / sampleNum" ratio can be 1 or 4 in "CmdSetSampleLocations"),
        //     all sample counts support programmable positions
        uint8_t sampleLocations;

        // https://microsoft.github.io/DirectX-Specs/d3d/Raytracing.html#checkfeaturesupport-structures
        // 1 - DXR 1.0: full raytracing functionality, except features below
        // 2 - DXR 1.1: adds - ray query, "CmdDispatchRaysIndirect", "GeometryIndex()" intrinsic, additional ray flags & vertex formats
        // 3 - DXR 1.2: adds - micromap, shader execution reordering
        uint8_t rayTracing;

        // https://microsoft.github.io/DirectX-Specs/d3d/VariableRateShading.html#feature-tiering
        // 1 - shading rate can be specified only per draw
        // 2 - adds: per primitive shading rate, per "shadingRateAttachmentTileSize" shading rate, combiners, "SV_ShadingRate" support
        uint8_t shadingRate;

        // https://microsoft.github.io/DirectX-Specs/d3d/ResourceBinding.html#limitations-on-static-samplers
        // 0 - ALL descriptors in range must be valid by the time the command list executes
        // 1 - only "CONSTANT_BUFFER" and "STORAGE" descriptors in range must be valid
        // 2 - only referenced descriptors must be valid
        uint8_t resourceBinding;

        // 1 - unbound arrays with dynamic indexing
        // 2 - D3D12 dynamic resources: https://microsoft.github.io/DirectX-Specs/d3d/HLSL_SM_6_6_DynamicResources.html
        uint8_t bindless;

        // https://learn.microsoft.com/en-us/windows/win32/api/d3d12/ne-d3d12-d3d12_resource_heap_tier
        // 1 - a "Memory" can support resources from all 3 categories: buffers, attachments, all other textures
        uint8_t memory;
    } tiers;

    // Features
    struct {
        // Swap chain
        bool swapChain;                                           // NRISwapChain
        bool presentFromCompute;                                  // see "SwapChainDesc::queue"
        bool waitableSwapChain;                                   // see "SwapChainDesc::waitable"
        bool resizableSwapChain;                                  // swap chain can be resized without triggering an "OUT_OF_DATE" error

        // Multi view
        bool flexibleMultiview;                                   // see "Multiview::FLEXIBLE"
        bool layerBasedMultiview;                                 // see "Multiview::LAYRED_BASED"
        bool viewportBasedMultiview;                              // see "Multiview::VIEWPORT_BASED"

        // Texture compression
        bool textureCompressionBC;                                // all "BC" texture formats are supported
        bool textureCompressionETC2;                              // all "ETC2" texture formats are supported
        bool textureCompressionASTC;                              // all "ASTC" texture formats are supported

        // Shader bytecode
        bool shaderBytecodeDXBC;                                  // DXBC can be passed to "ShaderDesc::bytecode"
        bool shaderBytecodeDXIL;                                  // DXIL can be passed to "ShaderDesc::bytecode"
        bool shaderBytecodeSPIRV;                                 // SPIRV can be passed to "ShaderDesc::bytecode", WGPU expects Vulkan 1.2 environment
        bool shaderBytecodeWGSL;                                  // WGSL can be passed to "ShaderDesc::bytecode"

        // Queries
        bool occlusion;                                           // see "QueryType::OCCLUSION"
        bool timestamp;                                           // see "QueryType::TIMESTAMP"
        bool timestampCopyQueue;                                  // see "QueryType::TIMESTAMP_COPY_QUEUE"
        bool calibratedTimestamps;                                // see "GetCalibratedTimestamps"

        // Shading rate
        bool additionalShadingRates;                              // see "ShadingRate"
        bool sumShadingRateCombiner;                              // see "ShadingRateCombiner::SUM"

        // Resolve
        bool regionResolve;                                       // see "CmdResolveTexture"
        bool resolveOpMinMax;                                     // see "ResolveOp"

        // Pipeline cache
        bool pipelineCache;                                       // "PipelineCache" support (NOP fallback if unsupported, except on error)
        bool pipelineCacheControl;                                // "FAIL_ON_CACHE_MISS" enforces "FAILURE", useful for platforms that prohibit runtime PSO compilation (e.g., Xbox GDK)

        // Other
        bool getMemoryDesc2;                                      // "GetXxxMemoryDesc2" support (VK: requires "maintenance4", D3D: supported)
        bool enhancedBarriers;                                    // VK: supported, D3D12: requires "AgilitySDK", D3D11: unsupported
        bool tessellationShader;                                  // Tessellation control and evaluation shader stages
        bool geometryShader;                                      // Geometry shader stage
        bool meshShader;                                          // NRIMeshShader
        bool lowLatency;                                          // NRILowLatency
        bool componentSwizzle;                                    // see "ComponentSwizzle" (unsupported only in D3D11)
        bool independentFrontAndBackStencilReferenceAndMasks;     // see "StencilAttachmentDesc::back"
        bool filterOpMinMax;                                      // see "FilterOp"
        bool logicOp;                                             // see "LogicOp"
        bool depthBoundsTest;                                     // see "DepthAttachmentDesc::boundsTest"
        bool drawIndirectCount;                                   // see "countBuffer" and "countBufferOffset"
        bool lineSmoothing;                                       // see "RasterizationDesc::lineSmoothing"
        bool meshShaderPipelineStats;                             // see "PipelineStatisticsDesc"
        bool dynamicDepthBias;                                    // see "CmdSetDepthBias"
        bool viewportOriginBottomLeft;                            // see "Viewport"
        bool pipelineStatistics;                                  // see "QueryType::PIPELINE_STATISTICS"
        bool rootConstantsOffset;                                 // see "SetRootConstantsDesc" (unsupported only in D3D11)
        bool nonConstantBufferRootDescriptorOffset;               // see "SetRootDescriptorDesc" (unsupported only in D3D11)
        bool mutableDescriptorType;                               // see "DescriptorType::MUTABLE"
        bool extendedDynamicState;                                // VK: allows to use "VertexBufferDesc::stride" (dynamic) instead of "VertexStreamDesc::stride" (static). Widely supported
        bool unifiedTextureLayouts;                               // VK: allows to use "GENERAL" everywhere: https://docs.vulkan.org/refpages/latest/refpages/source/VK_KHR_unified_image_layouts.html
    } features;

    // Shader features
    // https://github.com/Microsoft/DirectXShaderCompiler/blob/main/docs/SPIR-V.rst
    struct {
        // Native types (I32 and F32 are always supported)
        // https://learn.microsoft.com/en-us/windows/win32/direct3dhlsl/dx-graphics-hlsl-scalar
        bool nativeI8;                                             // "(u)int8_t"
        bool nativeI16;                                            // "(u)int16_t"
        bool nativeF16;                                            // "float16_t"
        bool nativeI64;                                            // "(u)int64_t"
        bool nativeF64;                                            // "double"

        // Atomics on native types (I32 atomics are always supported, for others it can be partial support of SMEM, texture or buffer atomics)
        // https://learn.microsoft.com/en-us/windows/win32/direct3d11/direct3d-11-advanced-stages-cs-atomic-functions
        // https://microsoft.github.io/DirectX-Specs/d3d/HLSL_SM_6_6_Int64_and_Float_Atomics.html
        bool atomicsI16;                                           // "(u)int16_t" atomics
        bool atomicsF16;                                           // "float16_t" atomics
        bool atomicsF32;                                           // "float" atomics
        bool atomicsI64;                                           // "(u)int64_t" atomics
        bool atomicsF64;                                           // "double" atomics

        // Storage without format
        // https://learn.microsoft.com/en-us/windows/win32/direct3d12/typed-unordered-access-view-loads#using-unorm-and-snorm-typed-uav-loads-from-hlsl
        bool storageReadWithoutFormat;                             // NRI_FORMAT("unknown") is allowed for storage reads
        bool storageWriteWithoutFormat;                            // NRI_FORMAT("unknown") is allowed for storage writes

        // Wave intrinsics
        // https://github.com/microsoft/directxshadercompiler/wiki/wave-intrinsics
        bool waveQuery;                                            // WaveIsFirstLane, WaveGetLaneCount, WaveGetLaneIndex
        bool waveVote;                                             // WaveActiveAllTrue, WaveActiveAnyTrue, WaveActiveAllEqual
        bool waveShuffle;                                          // WaveReadLaneFirst, WaveReadLaneAt
        bool waveArithmetic;                                       // WaveActiveSum, WaveActiveProduct, WaveActiveMin, WaveActiveMax, WavePrefixProduct, WavePrefixSum
        bool waveReduction;                                        // WaveActiveCountBits, WaveActiveBitAnd, WaveActiveBitOr, WaveActiveBitXor, WavePrefixCountBits
        bool waveQuad;                                             // QuadReadLaneAt, QuadReadAcrossX, QuadReadAcrossY, QuadReadAcrossDiagonal

        // Other
        bool viewportIndex;                                        // SV_ViewportArrayIndex, always can be used in geometry shaders
        bool layerIndex;                                           // SV_RenderTargetArrayIndex, always can be used in geometry shaders
        bool unnormalizedCoordinates;                              // https://microsoft.github.io/DirectX-Specs/d3d/VulkanOn12.html#non-normalized-texture-sampling-coordinates
        bool clock;                                                // https://github.com/Microsoft/DirectXShaderCompiler/blob/main/docs/SPIR-V.rst#readclock
        bool rasterizedOrderedView;                                // https://microsoft.github.io/DirectX-Specs/d3d/RasterOrderViews.html (aka fragment shader interlock)
        bool barycentric;                                          // https://github.com/microsoft/DirectXShaderCompiler/wiki/SV_Barycentrics
        bool rayTracingPositionFetch;                              // https://docs.vulkan.org/features/latest/features/proposals/VK_KHR_ray_tracing_position_fetch.html
        bool integerDotProduct;                                    // https://github.com/microsoft/DirectXShaderCompiler/wiki/Shader-Model-6.4
        bool inputAttachments;                                     // https://github.com/Microsoft/DirectXShaderCompiler/blob/main/docs/SPIR-V.rst#subpass-inputs

        bool drawParameters;                                       // GAPI-independent "NRI_BASE_VERTEX", "NRI_BASE_INSTANCE", "NRI_VERTEX_ID_OFFSET" and "NRI_INSTANCE_ID_OFFSET" (see "NRI.hlsl" for expected usage)
        bool drawIndex;                                            // GAPI-independent "NRI_DRAW_ID" (see "NRI.hlsl" for expected usage)
    } shaderFeatures;
};

#pragma endregion

NriNamespaceEnd
