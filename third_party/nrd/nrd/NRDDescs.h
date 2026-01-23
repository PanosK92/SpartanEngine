/*
Copyright (c) 2022, NVIDIA CORPORATION. All rights reserved.

NVIDIA CORPORATION and its licensors retain all intellectual property
and proprietary rights in and to this software, related documentation
and any modifications thereto. Any use, reproduction, disclosure or
distribution of this software and related documentation without an express
license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

#pragma once

#define NRD_DESCS_VERSION_MAJOR 4
#define NRD_DESCS_VERSION_MINOR 16

static_assert(NRD_VERSION_MAJOR == NRD_DESCS_VERSION_MAJOR && NRD_VERSION_MINOR == NRD_DESCS_VERSION_MINOR, "Please, update all NRD SDK files");

namespace nrd
{
    typedef uint32_t Identifier;

    struct Instance;

    enum class Result : uint32_t
    {
        SUCCESS,
        FAILURE,
        INVALID_ARGUMENT,
        UNSUPPORTED,
        NON_UNIQUE_IDENTIFIER,

        MAX_NUM
    };

    // Only resources referenced by "Denoiser" must be provided from the application side
    // See NRD.hlsli for more details
    enum class ResourceType : uint32_t
    {
        //=============================================================================================================================
        // NON-NOISY INPUTS
        //=============================================================================================================================

        // 3D world-space motion (RGBA16f+) or 2D screen-space motion (RG16f+), MVs must be non-jittered, MV = previous - current
        IN_MV,

        // Data must match encoding in "NRD_FrontEnd_PackNormalAndRoughness" and "NRD_FrontEnd_UnpackNormalAndRoughness" (RGBA8+)
        IN_NORMAL_ROUGHNESS,

        // Linear view depth for primary rays (R16f+)
        IN_VIEWZ,

        // (Optional) User-provided history confidence in range 0-1, i.e. antilag (R8+)
        // It must be computed for the previous frame in the current frame ( the only one trivial solution in any case ).
        // These textures can be at lower resolution, they are linearly upscaled.
        // Used only if "CommonSettings::isHistoryConfidenceAvailable = true" and "NRD_SUPPORTS_HISTORY_CONFIDENCE = 1"
        IN_DIFF_CONFIDENCE,
        IN_SPEC_CONFIDENCE,

        // (Optional) User-provided disocclusion threshold selector in range 0-1 (R8+)
        // Disocclusion threshold is mixed between "disocclusionThreshold" and "disocclusionThresholdAlternate"
        // Used only if "CommonSettings::isDisocclusionThresholdMixAvailable = true" and "NRD_SUPPORTS_DISOCCLUSION_THRESHOLD_MIX = 1"
        IN_DISOCCLUSION_THRESHOLD_MIX,

        // (Optional) Base color (can be decoupled to diffuse and specular albedo based on metalness) and metalness (RGBA8+)
        // Used only if "CommonSettings::isBaseColorMetalnessAvailable = true" and "NRD_SUPPORTS_BASECOLOR_METALNESS = 1".
        // Currently used only by REBLUR (if Temporal Stabilization pass is available and "stabilizationStrength != 0")
        // to patch MV if specular (virtual) motion prevails on diffuse (surface) motion
        IN_BASECOLOR_METALNESS,

        //=============================================================================================================================
        // NOISY INPUTS
        //=============================================================================================================================

        // Radiance and hit distance (RGBA16f+)
        //      REBLUR: use "REBLUR_FrontEnd_PackRadianceAndNormHitDist" for encoding
        //      RELAX: use "RELAX_FrontEnd_PackRadianceAndHitDist" for encoding
        IN_DIFF_RADIANCE_HITDIST,
        IN_SPEC_RADIANCE_HITDIST,

        // Hit distance (R8+)
        //      REBLUR: use "REBLUR_FrontEnd_GetNormHitDist" for encoding
        IN_DIFF_HITDIST,
        IN_SPEC_HITDIST,

        // Sampling direction and normalized hit distance (RGBA8+)
        //      REBLUR: use "REBLUR_FrontEnd_PackDirectionalOcclusion" for encoding
        IN_DIFF_DIRECTION_HITDIST,

        // SH data (2x RGBA16f+)
        //      REBLUR: use "REBLUR_FrontEnd_PackSh" for encoding
        //      RELAX: use "RELAX_FrontEnd_PackSh" for encoding
        IN_DIFF_SH0,
        IN_DIFF_SH1,
        IN_SPEC_SH0,
        IN_SPEC_SH1,

        // Penumbra and optional translucency (R16f+ and RGBA8+ for translucency)
        //      SIGMA: use "SIGMA_FrontEnd_PackPenumbra" for penumbra properties encoding
        //      SIGMA: use "SIGMA_FrontEnd_PackTranslucency" for translucency encoding
        IN_PENUMBRA,
        IN_TRANSLUCENCY,

        // Some signal (R8+)
        IN_SIGNAL,

        //=============================================================================================================================
        // OUTPUTS
        //=============================================================================================================================

        // IMPORTANT: Most of denoisers do not write into output pixels outside of "CommonSettings::denoisingRange"!

        // Radiance and normalized hit distance (occlusion) or history length
        //      REBLUR: use "REBLUR_BackEnd_UnpackRadianceAndNormHitDist" for decoding (R11G11B10f+)
        //          .w = diffuse or specular occlusion (default) or history length in frames if "ReblurSettings::returnHistoryLengthInsteadOfOcclusion = true"
        //      RELAX: use "RELAX_BackEnd_UnpackRadiance" for decoding (R11G11B10f+)
        //          .w = diffuse history length in frames
        OUT_DIFF_RADIANCE_HITDIST,
        OUT_SPEC_RADIANCE_HITDIST,

        // SH data
        //      REBLUR: use "REBLUR_BackEnd_UnpackSh" for decoding (2x RGBA16f+)
        //          .normHitDist = diffuse or specular occlusion (default) or history length in frames if "ReblurSettings::returnHistoryLengthInsteadOfOcclusion = true"
        //      RELAX: use "RELAX_BackEnd_UnpackSh" for decoding (2x RGBA16f+)
        //          .normHitDist = diffuse history length in frames
        OUT_DIFF_SH0,
        OUT_DIFF_SH1,
        OUT_SPEC_SH0,
        OUT_SPEC_SH1,

        // Normalized hit distance (R8+)
        OUT_DIFF_HITDIST,
        OUT_SPEC_HITDIST,

        // Bent normal and normalized hit distance (RGBA8+)
        //      REBLUR: use "REBLUR_BackEnd_UnpackDirectionalOcclusion" for decoding
        OUT_DIFF_DIRECTION_HITDIST,

        // Shadow and optional transcluceny (R8+ or RGBA8+)
        //      SIGMA: use "SIGMA_BackEnd_UnpackShadow" for decoding
        OUT_SHADOW_TRANSLUCENCY, // IMPORTANT: used as history if "stabilizationStrength != 0"

        // Denoised signal (R8+)
        OUT_SIGNAL,

        // (Optional) Debug output (RGBA8+), .w = transparency
        // Used if "CommonSettings::enableValidation = true"
        OUT_VALIDATION,

        //=============================================================================================================================
        // POOLS
        //=============================================================================================================================

        // Can be reused after denoising
        TRANSIENT_POOL,

        // Dedicated to NRD, can't be reused
        PERMANENT_POOL,

        MAX_NUM,
    };

    enum class Denoiser : uint32_t
    {
        /*
        IMPORTANT:
          - IN_MV, IN_NORMAL_ROUGHNESS, IN_VIEWZ are used by any denoiser, but these denoisers DON'T use:
              - SIGMA_SHADOW & SIGMA_SHADOW_TRANSLUCENCY - IN_MV, if "stabilizationStrength = 0"
              - REFERENCE - IN_MV, IN_NORMAL_ROUGHNESS, IN_VIEWZ
          - Optional inputs are in ()
        */

        //=============================================================================================================================
        // REBLUR
        //=============================================================================================================================

        // INPUTS - IN_DIFF_RADIANCE_HITDIST (IN_DIFF_CONFIDENCE, IN_DISOCCLUSION_THRESHOLD_MIX)
        // OUTPUTS - OUT_DIFF_RADIANCE_HITDIST
        REBLUR_DIFFUSE,

        // INPUTS - IN_DIFF_HITDIST (IN_DIFF_CONFIDENCE, IN_DISOCCLUSION_THRESHOLD_MIX)
        // OUTPUTS - OUT_DIFF_HITDIST
        REBLUR_DIFFUSE_OCCLUSION,

        // INPUTS - IN_DIFF_SH0, IN_DIFF_SH1 (IN_DIFF_CONFIDENCE, IN_DISOCCLUSION_THRESHOLD_MIX)
        // OUTPUTS - OUT_DIFF_SH0, OUT_DIFF_SH1
        REBLUR_DIFFUSE_SH,

        // INPUTS - IN_SPEC_RADIANCE_HITDIST (IN_SPEC_CONFIDENCE, IN_DISOCCLUSION_THRESHOLD_MIX, IN_BASECOLOR_METALNESS)
        // OUTPUTS - OUT_SPEC_RADIANCE_HITDIST
        REBLUR_SPECULAR,

        // INPUTS - IN_SPEC_HITDIST (IN_SPEC_CONFIDENCE, IN_DISOCCLUSION_THRESHOLD_MIX)
        // OUTPUTS - OUT_SPEC_HITDIST
        REBLUR_SPECULAR_OCCLUSION,

        // INPUTS - IN_SPEC_SH0, IN_SPEC_SH1 (IN_SPEC_CONFIDENCE, IN_DISOCCLUSION_THRESHOLD_MIX, IN_BASECOLOR_METALNESS)
        // OUTPUTS - OUT_SPEC_SH0, OUT_SPEC_SH1
        REBLUR_SPECULAR_SH,

        // INPUTS - IN_DIFF_RADIANCE_HITDIST, IN_SPEC_RADIANCE_HITDIST (IN_DIFF_CONFIDENCE, IN_SPEC_CONFIDENCE, IN_DISOCCLUSION_THRESHOLD_MIX, IN_BASECOLOR_METALNESS)
        // OUTPUTS - OUT_DIFF_RADIANCE_HITDIST, OUT_SPEC_RADIANCE_HITDIST
        REBLUR_DIFFUSE_SPECULAR,

        // INPUTS - IN_DIFF_HITDIST, IN_SPEC_HITDIST (IN_DIFF_CONFIDENCE, IN_SPEC_CONFIDENCE, IN_DISOCCLUSION_THRESHOLD_MIX)
        // OUTPUTS - OUT_DIFF_HITDIST, OUT_SPEC_HITDIST
        REBLUR_DIFFUSE_SPECULAR_OCCLUSION,

        // INPUTS - IN_DIFF_SH0, IN_DIFF_SH1, IN_SPEC_SH0, IN_SPEC_SH1 (IN_DIFF_CONFIDENCE, IN_SPEC_CONFIDENCE, IN_DISOCCLUSION_THRESHOLD_MIX, IN_BASECOLOR_METALNESS)
        // OUTPUTS - OUT_DIFF_SH0, OUT_DIFF_SH1, OUT_SPEC_SH0, OUT_SPEC_SH1
        REBLUR_DIFFUSE_SPECULAR_SH,

        // INPUTS - IN_DIFF_DIRECTION_HITDIST (IN_DIFF_CONFIDENCE, IN_DISOCCLUSION_THRESHOLD_MIX)
        // OUTPUTS - OUT_DIFF_DIRECTION_HITDIST
        REBLUR_DIFFUSE_DIRECTIONAL_OCCLUSION,

        //=============================================================================================================================
        // RELAX
        //=============================================================================================================================

        // INPUTS - IN_DIFF_RADIANCE_HITDIST (IN_DIFF_CONFIDENCE, IN_DISOCCLUSION_THRESHOLD_MIX)
        // OUTPUTS - OUT_DIFF_RADIANCE_HITDIST
        RELAX_DIFFUSE,

        // INPUTS - IN_DIFF_SH0, IN_DIFF_SH1 (IN_DIFF_CONFIDENCE, IN_DISOCCLUSION_THRESHOLD_MIX)
        // OUTPUTS - OUT_DIFF_SH0, OUT_DIFF_SH1
        RELAX_DIFFUSE_SH,

        // INPUTS - IN_SPEC_RADIANCE_HITDIST (IN_SPEC_CONFIDENCE, IN_DISOCCLUSION_THRESHOLD_MIX)
        // OUTPUTS - OUT_SPEC_RADIANCE_HITDIST
        RELAX_SPECULAR,

        // INPUTS - IN_SPEC_SH0, IN_SPEC_SH1 (IN_SPEC_CONFIDENCE, IN_DISOCCLUSION_THRESHOLD_MIX)
        // OUTPUTS - OUT_SPEC_SH0, OUT_SPEC_SH1
        RELAX_SPECULAR_SH,

        // INPUTS - IN_DIFF_RADIANCE_HITDIST, IN_SPEC_RADIANCE_HITDIST (IN_DIFF_CONFIDENCE, IN_SPEC_CONFIDENCE, IN_DISOCCLUSION_THRESHOLD_MIX)
        // OUTPUTS - OUT_DIFF_RADIANCE_HITDIST, OUT_SPEC_RADIANCE_HITDIST
        RELAX_DIFFUSE_SPECULAR,

        // INPUTS - IN_DIFF_SH0, IN_DIFF_SH1, IN_SPEC_SH0, IN_SPEC_SH1 (IN_DIFF_CONFIDENCE, IN_SPEC_CONFIDENCE, IN_DISOCCLUSION_THRESHOLD_MIX)
        // OUTPUTS - OUT_DIFF_SH0, OUT_DIFF_SH1, OUT_SPEC_SH0, OUT_SPEC_SH1
        RELAX_DIFFUSE_SPECULAR_SH,

        //=============================================================================================================================
        // SIGMA
        //=============================================================================================================================

        // INPUTS - IN_PENUMBRA, OUT_SHADOW_TRANSLUCENCY
        // OUTPUTS - OUT_SHADOW_TRANSLUCENCY
        SIGMA_SHADOW,

        // INPUTS - IN_PENUMBRA, IN_TRANSLUCENCY, OUT_SHADOW_TRANSLUCENCY
        // OUTPUTS - OUT_SHADOW_TRANSLUCENCY
        SIGMA_SHADOW_TRANSLUCENCY,

        //=============================================================================================================================
        // REFERENCE
        //=============================================================================================================================

        // INPUTS - IN_SIGNAL
        // OUTPUTS - OUT_SIGNAL
        REFERENCE,

        MAX_NUM
    };

    enum class Format : uint32_t
    {
        R8_UNORM,
        R8_SNORM,
        R8_UINT,
        R8_SINT,

        RG8_UNORM,
        RG8_SNORM,
        RG8_UINT,
        RG8_SINT,

        RGBA8_UNORM,
        RGBA8_SNORM,
        RGBA8_UINT,
        RGBA8_SINT,
        RGBA8_SRGB,

        R16_UNORM,
        R16_SNORM,
        R16_UINT,
        R16_SINT,
        R16_SFLOAT,

        RG16_UNORM,
        RG16_SNORM,
        RG16_UINT,
        RG16_SINT,
        RG16_SFLOAT,

        RGBA16_UNORM,
        RGBA16_SNORM,
        RGBA16_UINT,
        RGBA16_SINT,
        RGBA16_SFLOAT,

        R32_UINT,
        R32_SINT,
        R32_SFLOAT,

        RG32_UINT,
        RG32_SINT,
        RG32_SFLOAT,

        RGB32_UINT,
        RGB32_SINT,
        RGB32_SFLOAT,

        RGBA32_UINT,
        RGBA32_SINT,
        RGBA32_SFLOAT,

        R10_G10_B10_A2_UNORM,
        R10_G10_B10_A2_UINT,
        R11_G11_B10_UFLOAT,
        R9_G9_B9_E5_UFLOAT,

        MAX_NUM
    };

    enum class DescriptorType : uint32_t
    {
        // read-only, SRV
        TEXTURE,

        // read-write, UAV
        STORAGE_TEXTURE,

        MAX_NUM
    };

    enum class Sampler : uint32_t
    {
        NEAREST_CLAMP,
        LINEAR_CLAMP,

        MAX_NUM
    };

    // NRD_NORMAL_ENCODING variants
    enum class NormalEncoding : uint8_t
    {
        // Worst IQ on curved (not bumpy) surfaces
        RGBA8_UNORM,
        RGBA8_SNORM,

        // Close to best IQ on curved (not bumpy) surfaces, but offers optional materialID support (normals are oct-packed, 2 bits for material ID)
        R10_G10_B10_A2_UNORM, // RECOMMENDED

        // Best IQ on curved (not bumpy) surfaces
        RGBA16_UNORM,
        RGBA16_SNORM, // can be used with FP formats

        MAX_NUM
    };

    // NRD_ROUGHNESS_ENCODING variants
    enum class RoughnessEncoding : uint8_t
    {
        // Alpha (m)
        SQ_LINEAR,

        // Linear roughness
        LINEAR, // RECOMMENDED

        // Sqrt(linear roughness)
        SQRT_LINEAR,

        MAX_NUM
    };

    struct AllocationCallbacks
    {
        void* (NRD_CALL *Allocate)(void* userArg, size_t size, size_t alignment);
        void* (NRD_CALL *Reallocate)(void* userArg, void* memory, size_t size, size_t alignment);
        void (NRD_CALL *Free)(void* userArg, void* memory);
        void* userArg;
    };

    struct SPIRVBindingOffsets
    {
        uint32_t samplerOffset;
        uint32_t textureOffset;
        uint32_t constantBufferOffset;
        uint32_t storageTextureAndBufferOffset;
    };

    struct LibraryDesc
    {
        SPIRVBindingOffsets spirvBindingOffsets;
        const Denoiser* supportedDenoisers;
        uint32_t supportedDenoisersNum;
        uint8_t versionMajor;
        uint8_t versionMinor;
        uint8_t versionBuild;
        NormalEncoding normalEncoding;
        RoughnessEncoding roughnessEncoding;
    };

    struct DenoiserDesc
    {
        Identifier identifier;
        Denoiser denoiser;
    };

    struct InstanceCreationDesc
    {
        AllocationCallbacks allocationCallbacks;
        const DenoiserDesc* denoisers;
        uint32_t denoisersNum;
    };

    struct TextureDesc
    {
        Format format;
        uint16_t downsampleFactor;
    };

    struct ResourceDesc
    {
        DescriptorType descriptorType;
        ResourceType type;
        uint16_t indexInPool;
    };

    struct ResourceRangeDesc
    {
        DescriptorType descriptorType;
        uint32_t descriptorsNum;
    };

    struct ComputeShaderDesc
    {
        const void* bytecode;
        uint64_t size;
    };

    struct PipelineDesc
    {
        ComputeShaderDesc computeShaderDXBC;
        ComputeShaderDesc computeShaderDXIL;
        ComputeShaderDesc computeShaderSPIRV;
        const ResourceRangeDesc* resourceRanges;
        uint32_t resourceRangesNum; // up to 2 ranges: "TEXTURE" inputs (optional) and "TEXTURE_STORAGE" outputs

        // Hint that pipeline has a constant buffer with shared parameters from "InstanceDesc"
        bool hasConstantData;

        // Format: "fileName|macro1=value1|macro2=value2..." (useful for custom integrations)
        char shaderIdentifier[256];
    };

    struct DescriptorPoolDesc
    {
        // (Recommended) use a root CBV (push descriptor) for constants
        // (Recommended) use static (immutable) samplers

        // (Recommended) if a shared pipeline layout (root signature) is used:
        //  - represents maximum number of resources in a pipeline
        //  - always 1 constant buffer
        //  - always "Sampler::MAX_NUM" samplers
        uint32_t perSetTexturesMaxNum;
        uint32_t perSetStorageTexturesMaxNum;

        // If tight (per pipeline) pipeline layouts are used:
        // - summed up across all dispatches
        uint32_t totalTexturesNum;
        uint32_t totalStorageTexturesNum;

        // Maximum number of descriptor sets in a descriptor pool
        uint32_t setsMaxNum;
    };

    struct InstanceDesc
    {
        // Register spaces
        uint32_t constantBufferAndSamplersSpaceIndex;   // constant buffer and samplers (= "NRD_CONSTANT_BUFFER_AND_SAMPLERS_SPACE_INDEX")
        uint32_t resourcesSpaceIndex;                   // SRVs and UAVs (= "NRD_RESOURCES_SPACE_INDEX")

        // Base registers
        uint32_t constantBufferRegisterIndex;           // = "NRD_CONSTANT_BUFFER_REGISTER_INDEX"
        uint32_t samplersBaseRegisterIndex;             // = 0
        uint32_t resourcesBaseRegisterIndex;            // = 0

        // Constant buffer (a root/push descriptor recommended)
        uint32_t constantBufferMaxDataSize;

        // Samplers (root/immutable samplers recommended)
        const Sampler* samplers;
        uint32_t samplersNum;                           // = "Sampler::MAX_NUM"

        // Pipelines
        const char* shaderEntryPoint;                   // = "NRD_CS_MAIN"
        const PipelineDesc* pipelines;
        uint32_t pipelinesNum;

        // Textures
        const TextureDesc* permanentPool;
        uint32_t permanentPoolSize;
        const TextureDesc* transientPool;
        uint32_t transientPoolSize;

        // (Optional) Limits
        DescriptorPoolDesc descriptorPoolDesc;
    };

    struct DispatchDesc
    {
        // (Optional)
        const char* name;
        Identifier identifier; // denoiser this dispatch belongs to

        // Concatenated resources for all "resourceRanges" in "DenoiserDesc::pipelines[pipelineIndex]"
        const ResourceDesc* resources;
        uint32_t resourcesNum;

        // Constants
        const uint8_t* constantBufferData;
        uint32_t constantBufferDataSize;
        bool constantBufferDataMatchesPreviousDispatch; // i.e. no update needed

        // Other
        uint16_t pipelineIndex;
        uint16_t gridWidth;
        uint16_t gridHeight;
    };
}
