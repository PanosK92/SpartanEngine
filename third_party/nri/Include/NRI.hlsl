// © 2024 NVIDIA Corporation

#ifndef NRI_HLSL
#define NRI_HLSL

/*
USAGE:

Textures, buffers, samplers and acceleration structures:
    NRI_RESOURCE(Texture2D<float4>, gTexture, t, 0, 0);
    NRI_RESOURCE(Buffer<float2>, gBuffer, t, 1, 0);
    NRI_RESOURCE(StructuredBuffer<MyStruct>, gStructuredBuffer, t, 2, 0);
    NRI_RESOURCE(RaytracingAccelerationStructure, gTlas, t, 3, 0);
    NRI_RESOURCE(SamplerState, gLinearMipmapLinearSampler, s, 0, 0);

Storage textures and buffers:
    NRI_RESOURCE(RWTexture2D<float4>, gStorageTexture, u, 0, 1);
    NRI_RESOURCE(RWBuffer<float2>, gStorageBuffer, u, 1, 1);
    NRI_RESOURCE(RWStructuredBuffer<MyStruct>, gStorageStructuredBuffer, u, 2, 1);

Texture and buffer arrays:
    NRI_RESOURCE(Texture2D<float3>, gInputs[], t, 0, 0); // DXIL/SPIRV only
    NRI_RESOURCE(Texture2D<float>, gInputs[8], t, 0, 0); // DXBC compatible

Non-structured storage resources must be used with "NRI_FORMAT" macro. "unknown" is allowed if
"shaderFeatures.storageReadWithoutFormat" and/or "shaderFeatures.storageWriteWithoutFormat"
are supported (any desktop GPU supports it since 2014):
    NRI_FORMAT("unknown") NRI_RESOURCE(RWTexture2D<float>, gOutput, u, 0, 0);

Dual source blending:
    NRI_BLEND_SOURCE(0) out float4 color : SV_Target0,
    NRI_BLEND_SOURCE(1) out float4 blend : SV_Target1

Constants:
    NRI_RESOURCE(cbuffer, Constants, b, 0, 3) {
        uint32_t gConst1;
        uint32_t gConst2;
        uint32_t gConst3;
        uint32_t gConst4;
    };

Push constants:
    struct RootConstants {
        float const1;
        uint32_t const2;
    };

    NRI_ROOT_CONSTANTS(RootConstants, gRootConstants, 7, 0); // a constant buffer in DXBC

Input attachments (reading on-chip memory):
    NRI_INPUT_ATTACHMENT(gGbuffer, 1, 1, 0); // use "NRI_INPUT_ATTACHMENT_LOAD" for loading data

Draw parameters:
  - macros:
    - NRI_VERTEX_ID / NRI_INSTANCE_ID - start from 0
    - NRI_BASE_VERTEX / NRI_BASE_INSTANCE - base vertex / instance from a "Draw" call (requires "shaderFeatures.drawParameters")
    - NRI_VERTEX_ID_OFFSET / NRI_INSTANCE_ID_OFFSET - start from "base" vertex / instance (requires "shaderFeatures.drawParameters")
    - NRI_DRAW_ID - draw index from an indirect "Draw" call, 0 for direct "Draw" calls (requires "shaderFeatures.drawIndex")
  - usage:
    - add "NRI_ENABLE_DRAW_PARAMETERS" to the global scope
    - add "NRI_DECLARE_DRAW_PARAMETERS" to a function input parameters list
    - use one of "NRI_FILL_X_DESC" macros for filling indirect draw commands in a shader
  - D3D12 emulation:
    - required for:
      - SM < 6.8 shaders using "NRI_BASE_VERTEX", "NRI_BASE_INSTANCE", "NRI_VERTEX_ID_OFFSET" and "NRI_INSTANCE_ID_OFFSET"
      - all shaders using "NRI_DRAW_ID"
    - to enable emulation:
      - set "ENABLE_DRAW_PARAMETERS_EMULATION" and/or "ENABLE_DRAW_INDEX_EMULATION" for a corresponding "PipelineLayout"
      - define "NRI_ENABLE_DRAW_PARAMETERS_EMULATION" and/or "NRI_ENABLE_DRAW_INDEX_EMULATION" prior inclusion of "NRI.hlsl"
*/

// Compiler detection
#if defined __hlsl_dx_compiler
    #ifdef __spirv__
        #define NRI_SPIRV
        #define NRI_PRINTF_AVAILABLE
    #else
        #define NRI_DXIL
    #endif
#elif defined __SLANG__
    #if defined __spirv__
        #define NRI_SPIRV
        #define NRI_PRINTF_AVAILABLE
    #elif defined __dxil__
        #define NRI_DXIL
    #endif
#else
    #if (defined(__cplusplus) || defined(__STDC__) || defined(__STDC_VERSION__))
        #define NRI_C
    #else
        #define NRI_DXBC
    #endif
#endif

// Copied from "NRIMacro.h"
#ifndef NRI_C
    #define _NRI_MERGE_TOKENS(a, b) a##b
    #define NRI_MERGE_TOKENS(a, b) _NRI_MERGE_TOKENS(a, b)
#endif

// Shader model
#if (defined(__hlsl_dx_compiler) || defined(__SLANG__))
    #ifndef __SHADER_TARGET_MAJOR
        #define __SHADER_TARGET_MAJOR 6
    #endif
    #ifndef __SHADER_TARGET_MINOR
        #define __SHADER_TARGET_MINOR 7
    #endif
    #define NRI_SHADER_MODEL (__SHADER_TARGET_MAJOR * 10 + __SHADER_TARGET_MINOR)
#else
    #define NRI_SHADER_MODEL 50
#endif

// Extensions
#ifndef NRI_SHADER_EXT_REGISTER
    #define NRI_SHADER_EXT_REGISTER 63
#else
    // Must match "DeviceCreationDesc::shaderExtRegister"
#endif

// Expected usage:

// NVIDIA
//#if defined(NRI_DXBC) || defined(NRI_DXIL)
//    #define NV_SHADER_EXTN_SLOT NRI_MERGE_TOKENS(u, NRI_SHADER_EXT_REGISTER)
//    #ifdef NRI_DXIL
//        #define NV_SHADER_EXTN_REGISTER_SPACE space0
//    #endif
//    #include "../External/nvapi/nvHLSLExtns.h"
//#endif

// AMD
//#ifdef NRI_DXIL
//    #define AMD_EXT_SHADER_INTRINSIC_UAV_OVERRIDE NRI_MERGE_TOKENS(u, NRI_SHADER_EXT_REGISTER)
//    #include "../External/amdags/ags_lib/hlsl/ags_shader_intrinsics_dx12.hlsl"
//#endif
//#ifdef NRI_DXBC
//    #define AmdDxExtShaderIntrinsicsUAVSlot NRI_MERGE_TOKENS(u, NRI_SHADER_EXT_REGISTER)
//    #include "../External/amdags/ags_lib/hlsl/ags_shader_intrinsics_dx11.hlsl"
//#endif

// Indirect commands filling // TODO: change to StructuredBuffers?
#ifdef NRI_USE_BYTE_ADDRESS_BUFFER
    #define NRI_BUFFER_WRITE(buffer, offset, index, value) buffer.Store(offset * 4 + index * 4, value)
#else
    #define NRI_BUFFER_WRITE(buffer, offset, index, value) buffer[offset + index] = value
#endif

// "DrawDesc"
#define NRI_FILL_DRAW_DESC(buffer, cmdIndex, vertexNum, instanceNum, baseVertex, baseInstance) \
    NRI_BUFFER_WRITE(buffer, cmdIndex * 4, 0, vertexNum); \
    NRI_BUFFER_WRITE(buffer, cmdIndex * 4, 1, instanceNum); \
    NRI_BUFFER_WRITE(buffer, cmdIndex * 4, 2, baseVertex); \
    NRI_BUFFER_WRITE(buffer, cmdIndex * 4, 3, baseInstance)

// see "DrawIndexedDesc"
#define NRI_FILL_DRAW_INDEXED_DESC(buffer, cmdIndex, indexNum, instanceNum, baseIndex, baseVertex, baseInstance) \
    NRI_BUFFER_WRITE(buffer, cmdIndex * 5, 0, indexNum); \
    NRI_BUFFER_WRITE(buffer, cmdIndex * 5, 1, instanceNum); \
    NRI_BUFFER_WRITE(buffer, cmdIndex * 5, 2, baseIndex); \
    NRI_BUFFER_WRITE(buffer, cmdIndex * 5, 3, baseVertex); \
    NRI_BUFFER_WRITE(buffer, cmdIndex * 5, 4, baseInstance)

// see "DrawBaseDesc"
#define NRI_FILL_DRAW_BASE_DESC(buffer, cmdIndex, vertexNum, instanceNum, baseVertex, baseInstance) \
    NRI_BUFFER_WRITE(buffer, cmdIndex * 6, 0, baseVertex); /* root constant */ \
    NRI_BUFFER_WRITE(buffer, cmdIndex * 6, 1, baseInstance); /* root constant */ \
    NRI_BUFFER_WRITE(buffer, cmdIndex * 6, 2, vertexNum); \
    NRI_BUFFER_WRITE(buffer, cmdIndex * 6, 3, instanceNum); \
    NRI_BUFFER_WRITE(buffer, cmdIndex * 6, 4, baseVertex); \
    NRI_BUFFER_WRITE(buffer, cmdIndex * 6, 5, baseInstance)

// see "DrawIndexedBaseDesc"
#define NRI_FILL_DRAW_INDEXED_BASE_DESC(buffer, cmdIndex, indexNum, instanceNum, baseIndex, baseVertex, baseInstance) \
    NRI_BUFFER_WRITE(buffer, cmdIndex * 7, 0, baseVertex); /* root constant */ \
    NRI_BUFFER_WRITE(buffer, cmdIndex * 7, 1, baseInstance); /* root constant */ \
    NRI_BUFFER_WRITE(buffer, cmdIndex * 7, 2, indexNum); \
    NRI_BUFFER_WRITE(buffer, cmdIndex * 7, 3, instanceNum); \
    NRI_BUFFER_WRITE(buffer, cmdIndex * 7, 4, baseIndex); \
    NRI_BUFFER_WRITE(buffer, cmdIndex * 7, 5, baseVertex); \
    NRI_BUFFER_WRITE(buffer, cmdIndex * 7, 6, baseInstance)

// Shading rate
#define NRI_SHADING_RATE(xLogSize, yLogSize) ((xLogSize << 2) | yLogSize)

// SPIRV
#ifdef NRI_SPIRV
    #ifdef __SLANG__
        #define NRI_RESOURCE(resourceType, name, regName, bindingIndex, setIndex) \
            [[vk::binding(bindingIndex, setIndex)]] resourceType name : register(NRI_MERGE_TOKENS(regName, bindingIndex), NRI_MERGE_TOKENS(space, setIndex))
    #else
        #define NRI_RESOURCE(resourceType, name, regName, bindingIndex, setIndex) \
            resourceType name : register(NRI_MERGE_TOKENS(regName, bindingIndex), NRI_MERGE_TOKENS(space, setIndex))
    #endif

    #define NRI_ROOT_CONSTANTS(structName, name, bindingIndex, setIndex) \
        [[vk::push_constant]] structName name

    #define NRI_BLEND_SOURCE(source) \
        [[vk::location(0)]] [[vk::index(source)]]

    #define NRI_FORMAT(format) \
        [[vk::image_format(format)]]

    // Input attachment
    #define NRI_INPUT_ATTACHMENT(name, attachmentIndex, bindingIndex, setIndex) \
        [[vk::input_attachment_index(attachmentIndex)]] \
        [[vk::binding(bindingIndex, setIndex)]] \
        SubpassInput name

    #define NRI_INPUT_ATTACHMENT_LOAD(inputAttachment, pixelPos) inputAttachment.SubpassLoad()

    // Draw parameters and draw index (native)
    #define NRI_ENABLE_DRAW_PARAMETERS

    #define NRI_DECLARE_DRAW_PARAMETERS \
        int NRI_VERTEX_ID_OFFSET : SV_VertexID, \
        uint NRI_INSTANCE_ID_OFFSET : SV_InstanceID, \
        [[vk::builtin("BaseVertex")]] int NRI_BASE_VERTEX : _SV_Nothing1, \
        [[vk::builtin("BaseInstance")]] uint NRI_BASE_INSTANCE : _SV_Nothing2, \
        [[vk::builtin("DrawIndex")]] uint NRI_DRAW_ID : _SV_Nothing3

    #define NRI_VERTEX_ID (NRI_VERTEX_ID_OFFSET - NRI_BASE_VERTEX)
    #define NRI_INSTANCE_ID (NRI_INSTANCE_ID_OFFSET - NRI_BASE_INSTANCE)
#endif

// DXIL
#define NRI_BASE_ATTRIBUTES_EMULATION_SPACE 999
#ifdef NRI_DXIL
    #define NRI_RESOURCE(resourceType, name, regName, bindingIndex, setIndex) \
        resourceType name : register(NRI_MERGE_TOKENS(regName, bindingIndex), NRI_MERGE_TOKENS(space, setIndex))

    #define NRI_ROOT_CONSTANTS(structName, name, bindingIndex, setIndex) \
        ConstantBuffer<structName> name : register(NRI_MERGE_TOKENS(b, bindingIndex), NRI_MERGE_TOKENS(space, setIndex))

    #define NRI_BLEND_SOURCE(source)
    #define NRI_FORMAT(format)

    // Input attachment
    #define NRI_INPUT_ATTACHMENT(name, attachmentIndex, bindingIndex, setIndex) \
        NRI_RESOURCE(Texture2D, name, t, bindingIndex, setIndex)

    #define NRI_INPUT_ATTACHMENT_LOAD(inputAttachment, pixelPos) inputAttachment[int2(pixelPos.xy)]

    // Draw index (emulation)
    #ifdef NRI_ENABLE_DRAW_INDEX_EMULATION
        #define _NRI_DECLARE_DRAW_INDEX \
            struct _DrawIndexConstants { \
                uint drawIndex; \
            }; \
            ConstantBuffer<_DrawIndexConstants> _DrawIndex : register(b1, NRI_MERGE_TOKENS(space, NRI_BASE_ATTRIBUTES_EMULATION_SPACE))

        #define NRI_DRAW_ID _DrawIndex.drawIndex
    #else
        #define _NRI_DECLARE_DRAW_INDEX
        #define NRI_DRAW_ID NRI_DRAW_ID_is_unsupported
    #endif

    // Draw parameters
    #if (NRI_SHADER_MODEL < 68)
        #ifdef NRI_ENABLE_DRAW_PARAMETERS_EMULATION
            // Draw parameters (emulation)
            #define NRI_ENABLE_DRAW_PARAMETERS \
                struct _BaseAttributeConstants { \
                    int baseVertex; \
                    uint baseInstance; \
                }; \
                ConstantBuffer<_BaseAttributeConstants> _BaseAttributes : register(b0, NRI_MERGE_TOKENS(space, NRI_BASE_ATTRIBUTES_EMULATION_SPACE)); \
                _NRI_DECLARE_DRAW_INDEX

            #define NRI_DECLARE_DRAW_PARAMETERS \
                uint NRI_VERTEX_ID : SV_VertexID, \
                uint NRI_INSTANCE_ID : SV_InstanceID

            #define NRI_BASE_VERTEX _BaseAttributes.baseVertex
            #define NRI_BASE_INSTANCE _BaseAttributes.baseInstance
            #define NRI_VERTEX_ID_OFFSET (NRI_BASE_VERTEX + NRI_VERTEX_ID)
            #define NRI_INSTANCE_ID_OFFSET (NRI_BASE_INSTANCE + NRI_INSTANCE_ID)

            #undef NRI_FILL_DRAW_DESC
            #define NRI_FILL_DRAW_DESC NRI_FILL_DRAW_BASE_DESC

            #undef NRI_FILL_DRAW_INDEXED_DESC
            #define NRI_FILL_DRAW_INDEXED_DESC NRI_FILL_DRAW_INDEXED_BASE_DESC
        #else
            // Draw parameters (partial support)
            #define NRI_ENABLE_DRAW_PARAMETERS \
                _NRI_DECLARE_DRAW_INDEX

            #define NRI_DECLARE_DRAW_PARAMETERS \
                uint NRI_VERTEX_ID : SV_VertexID, \
                uint NRI_INSTANCE_ID : SV_InstanceID

            #define NRI_BASE_VERTEX NRI_BASE_VERTEX_is_unsupported
            #define NRI_BASE_INSTANCE NRI_BASE_INSTANCE_is_unsupported
            #define NRI_VERTEX_ID_OFFSET NRI_VERTEX_ID_OFFSET_is_unsupported
            #define NRI_INSTANCE_ID_OFFSET NRI_INSTANCE_ID_OFFSET_is_unsupported
        #endif
    #else
        // Draw parameters (native)
        #define NRI_ENABLE_DRAW_PARAMETERS \
            _NRI_DECLARE_DRAW_INDEX

        #define NRI_DECLARE_DRAW_PARAMETERS \
            uint NRI_VERTEX_ID : SV_VertexID, \
            uint NRI_INSTANCE_ID : SV_InstanceID, \
            int NRI_BASE_VERTEX : SV_StartVertexLocation, \
            uint NRI_BASE_INSTANCE : SV_StartInstanceLocation

        #define NRI_VERTEX_ID_OFFSET (NRI_BASE_VERTEX + NRI_VERTEX_ID)
        #define NRI_INSTANCE_ID_OFFSET (NRI_BASE_INSTANCE + NRI_INSTANCE_ID)
    #endif
#endif

// DXBC
#ifdef NRI_DXBC
    #define NRI_RESOURCE(resourceType, name, regName, bindingIndex, setIndex) \
        resourceType name : register(NRI_MERGE_TOKENS(regName, bindingIndex))

    #define NRI_ROOT_CONSTANTS(structName, name, bindingIndex, setIndex) \
        cbuffer structName##_##name : register(NRI_MERGE_TOKENS(b, bindingIndex)) { \
            structName name; \
        }

    #define NRI_BLEND_SOURCE(source)
    #define NRI_FORMAT(format)

    // Input attachment
    #define NRI_INPUT_ATTACHMENT(name, attachmentIndex, bindingIndex, setIndex) \
        NRI_RESOURCE(Texture2D, g_Normals, t, bindingIndex, setIndex)

    #define NRI_INPUT_ATTACHMENT_LOAD(inputAttachment, pixelPos) inputAttachment[int2(pixelPos.xy)]

    // Draw parameters (partial support)
    #define NRI_ENABLE_DRAW_PARAMETERS

    #define NRI_DECLARE_DRAW_PARAMETERS \
        uint NRI_VERTEX_ID : SV_VertexID, \
        uint NRI_INSTANCE_ID : SV_InstanceID

    #define NRI_BASE_VERTEX NRI_BASE_VERTEX_is_unsupported
    #define NRI_BASE_INSTANCE NRI_BASE_INSTANCE_is_unsupported
    #define NRI_DRAW_ID NRI_DRAW_ID_is_unsupported
    #define NRI_VERTEX_ID_OFFSET NRI_VERTEX_ID_OFFSET_is_unsupported
    #define NRI_INSTANCE_ID_OFFSET NRI_INSTANCE_ID_OFFSET_is_unsupported

    // Missing data types
    #define uint32_t uint
    #define uint32_t2 uint2
    #define uint32_t3 uint3
    #define uint32_t4 uint4

    #define int32_t int
    #define int32_t2 int2
    #define int32_t3 int3
    #define int32_t4 int4

    #define float16_t float
    #define float16_t2 float2
    #define float16_t3 float3
    #define float16_t4 float4
#endif

// C/C++
#ifdef NRI_C
    #define NRI_RESOURCE(resourceType, name, regName, bindingIndex, setIndex) \
        struct name
#endif

// Misc (assumes D3D-style viewport)
#define NRI_UV_TO_CLIP(uv) (uv * float2(2, -2) + float2(-1, 1))
#define NRI_CLIP_TO_UV(clip) (clip * float2(0.5, -0.5) + 0.5)

#endif