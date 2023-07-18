// This file is part of the FidelityFX SDK.
//
// Copyright (C)2023 Advanced Micro Devices, Inc.
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files(the “Software”), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and /or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions :
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED “AS IS”, WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.

#include "ffx_denoiser_resources.h"

#if defined(FFX_GPU)
#ifdef __hlsl_dx_compiler
#pragma dxc diagnostic push
#pragma dxc diagnostic ignored "-Wambig-lit-shift"
#endif //__hlsl_dx_compiler
#include "ffx_core.h"
#ifdef __hlsl_dx_compiler
#pragma dxc diagnostic pop
#endif //__hlsl_dx_compiler
#endif // #if defined(FFX_GPU)

#if defined(FFX_GPU)
#ifndef FFX_PREFER_WAVE64
#define FFX_PREFER_WAVE64
#endif // #if defined(FFX_GPU)

#pragma warning(disable: 3205)  // conversion from larger type to smaller

#define FFX_DECLARE_SRV_REGISTER(regIndex)  t##regIndex
#define FFX_DECLARE_UAV_REGISTER(regIndex)  u##regIndex
#define FFX_DECLARE_CB_REGISTER(regIndex)   b##regIndex
#define FFX_DENOISER_SHADOWS_DECLARE_SRV(regIndex)  register(FFX_DECLARE_SRV_REGISTER(regIndex))
#define FFX_DENOISER_SHADOWS_DECLARE_UAV(regIndex)  register(FFX_DECLARE_UAV_REGISTER(regIndex))
#define FFX_DENOISER_SHADOWS_DECLARE_CB(regIndex)   register(FFX_DECLARE_CB_REGISTER(regIndex))

#if defined(DENOISER_SHADOWS_BIND_CB0_DENOISER_SHADOWS)
    cbuffer cb0DenoiserShadows : FFX_DENOISER_SHADOWS_DECLARE_CB(DENOISER_SHADOWS_BIND_CB0_DENOISER_SHADOWS)
    {
        FfxInt32x2      iBufferDimensions;
#define FFX_DENOISER_SHADOWS_CONSTANT_BUFFER_0_SIZE 2
    }
#endif

#if defined(DENOISER_SHADOWS_BIND_CB1_DENOISER_SHADOWS)
    cbuffer cb1DenoiserShadows : FFX_DENOISER_SHADOWS_DECLARE_CB(DENOISER_SHADOWS_BIND_CB1_DENOISER_SHADOWS)
    {
        FfxFloat32x3    fEye;
        FfxInt32        iFirstFrame;
        FfxInt32x2      iBufferDimensions;
        FfxFloat32x2    fInvBufferDimensions;
        FfxFloat32x2    fMotionVectorScale;
        FfxFloat32x2    normalsUnpackMul_unpackAdd;
        FfxFloat32Mat4    fProjectionInverse;
        FfxFloat32Mat4    fReprojectionMatrix;
        FfxFloat32Mat4    fViewProjectionInverse;
#define FFX_DENOISER_SHADOWS_CONSTANT_BUFFER_1_SIZE 56
    }
#endif

#if defined(DENOISER_SHADOWS_BIND_CB2_DENOISER_SHADOWS)
    cbuffer cb2DenoiserShadows : FFX_DENOISER_SHADOWS_DECLARE_CB(DENOISER_SHADOWS_BIND_CB2_DENOISER_SHADOWS)
    {
        FfxFloat32Mat4  fProjectionInverse;
        FfxFloat32x2    fInvBufferDimensions;
        FfxFloat32x2    normalsUnpackMul_unpackAdd;
        FfxInt32x2      iBufferDimensions;
        FfxFloat32      fDepthSimilaritySigma;
#define FFX_DENOISER_SHADOWS_CONSTANT_BUFFER_2_SIZE 24
    }
#endif

#if defined(FFX_GPU)
#define FFX_DENOISER_SHADOWS_ROOTSIG_STRINGIFY(p) FFX_DENOISER_SHADOWS_ROOTSIG_STR(p)
#define FFX_DENOISER_SHADOWS_ROOTSIG_STR(p) #p
#define FFX_DENOISER_SHADOWS_PREPARE_SHADOW_MASK_ROOTSIG [RootSignature( "DescriptorTable(UAV(u0, numDescriptors = " FFX_DENOISER_SHADOWS_ROOTSIG_STRINGIFY(FFX_DENOISER_SHADOWS_RESOURCE_IDENTIFIER_COUNT) ")), " \
                                    "DescriptorTable(SRV(t0, numDescriptors = " FFX_DENOISER_SHADOWS_ROOTSIG_STRINGIFY(FFX_DENOISER_SHADOWS_RESOURCE_IDENTIFIER_COUNT) ")), " \
                                    "RootConstants(num32BitConstants=" FFX_DENOISER_SHADOWS_ROOTSIG_STRINGIFY(FFX_DENOISER_SHADOWS_CONSTANT_BUFFER_0_SIZE) ", b0)")]

#define FFX_DENOISER_SHADOWS_TILE_CLASSIFICATION_ROOTSIG [RootSignature( "DescriptorTable(UAV(u0, numDescriptors = " FFX_DENOISER_SHADOWS_ROOTSIG_STRINGIFY(FFX_DENOISER_SHADOWS_RESOURCE_IDENTIFIER_COUNT) ")), " \
                                    "DescriptorTable(SRV(t0, numDescriptors = " FFX_DENOISER_SHADOWS_ROOTSIG_STRINGIFY(FFX_DENOISER_SHADOWS_RESOURCE_IDENTIFIER_COUNT) ")), " \
                                    "RootConstants(num32BitConstants=" FFX_DENOISER_SHADOWS_ROOTSIG_STRINGIFY(FFX_DENOISER_SHADOWS_CONSTANT_BUFFER_1_SIZE) ", b0), " \
                                    "StaticSampler(s0, filter = FILTER_MIN_MAG_MIP_LINEAR, " \
                                                      "addressU = TEXTURE_ADDRESS_CLAMP, " \
                                                      "addressV = TEXTURE_ADDRESS_CLAMP, " \
                                                      "addressW = TEXTURE_ADDRESS_CLAMP, " \
                                                      "MinLOD = 0, " \
                                                      "MaxLOD = 3.402823466e+38f, " \
                                                      "mipLODBias = 0, " \
                                                      "comparisonFunc = COMPARISON_FUNC_LESS_EQUAL, " \
                                                      "maxAnisotropy = 16, " \
                                                      "borderColor = STATIC_BORDER_COLOR_OPAQUE_WHITE)")]

#define FFX_DENOISER_SHADOWS_FILTER_SOFT_SHADOWS_ROOTSIG [RootSignature( "DescriptorTable(UAV(u0, numDescriptors = " FFX_DENOISER_SHADOWS_ROOTSIG_STRINGIFY(FFX_DENOISER_SHADOWS_RESOURCE_IDENTIFIER_COUNT) ")), " \
                                    "DescriptorTable(SRV(t0, numDescriptors = " FFX_DENOISER_SHADOWS_ROOTSIG_STRINGIFY(FFX_DENOISER_SHADOWS_RESOURCE_IDENTIFIER_COUNT) ")), " \
                                    "RootConstants(num32BitConstants=" FFX_DENOISER_SHADOWS_ROOTSIG_STRINGIFY(FFX_DENOISER_SHADOWS_CONSTANT_BUFFER_2_SIZE) ", b0)")]

#if defined(FFX_DENOISER_SHADOWS_EMBED_ROOTSIG)
#define FFX_DENOISER_SHADOWS_EMBED_PREPARE_SHADOW_MASK_ROOTSIG_CONTENT FFX_DENOISER_SHADOWS_PREPARE_SHADOW_MASK_ROOTSIG
#define FFX_DENOISER_SHADOWS_EMBED_TILE_CLASSIFICATION_ROOTSIG_CONTENT FFX_DENOISER_SHADOWS_TILE_CLASSIFICATION_ROOTSIG
#define FFX_DENOISER_SHADOWS_EMBED_FILTER_SOFT_SHADOWS_ROOTSIG_CONTENT FFX_DENOISER_SHADOWS_FILTER_SOFT_SHADOWS_ROOTSIG
#else
#define FFX_DENOISER_SHADOWS_EMBED_PREPARE_SHADOW_MASK_ROOTSIG_CONTENT
#define FFX_DENOISER_SHADOWS_EMBED_TILE_CLASSIFICATION_ROOTSIG_CONTENT
#define FFX_DENOISER_SHADOWS_EMBED_FILTER_SOFT_SHADOWS_ROOTSIG_CONTENT
#endif // #if FFX_DENOISER_SHADOWS_EMBED_ROOTSIG
#endif // #if defined(FFX_GPU)

// Sampler
#if defined(DENOISER_SHADOWS_BIND_SRV_HISTORY)
    SamplerState s_trilinerClamp : register(s0);
#endif

// SRVs
#if defined DENOISER_SHADOWS_BIND_SRV_INPUT_HIT_MASK_RESULTS
    Texture2D<FfxUInt32>                    r_hit_mask_results              : FFX_DENOISER_SHADOWS_DECLARE_SRV(DENOISER_SHADOWS_BIND_SRV_INPUT_HIT_MASK_RESULTS);
#endif
#if defined DENOISER_SHADOWS_BIND_SRV_DEPTH
    Texture2D<FfxFloat32>                   r_depth                         : FFX_DENOISER_SHADOWS_DECLARE_SRV(DENOISER_SHADOWS_BIND_SRV_DEPTH);
#endif
#if defined DENOISER_SHADOWS_BIND_SRV_VELOCITY
    Texture2D<FfxFloat32x2>                 r_velocity                      : FFX_DENOISER_SHADOWS_DECLARE_SRV(DENOISER_SHADOWS_BIND_SRV_VELOCITY);
#endif
#if defined DENOISER_SHADOWS_BIND_SRV_NORMAL
    Texture2D<FfxFloat32x3>                 r_normal                        : FFX_DENOISER_SHADOWS_DECLARE_SRV(DENOISER_SHADOWS_BIND_SRV_NORMAL);
#endif
#if defined DENOISER_SHADOWS_BIND_SRV_HISTORY
    Texture2D<FfxFloat32x2>                 r_history                       : FFX_DENOISER_SHADOWS_DECLARE_SRV(DENOISER_SHADOWS_BIND_SRV_HISTORY);
#endif
#if defined DENOISER_SHADOWS_BIND_SRV_PREVIOUS_DEPTH
    Texture2D<FfxFloat32>                   r_previous_depth                : FFX_DENOISER_SHADOWS_DECLARE_SRV(DENOISER_SHADOWS_BIND_SRV_PREVIOUS_DEPTH);
#endif
#if defined DENOISER_SHADOWS_BIND_SRV_PREVIOUS_MOMENTS
    Texture2D<FfxFloat32x3>                 r_previous_moments              : FFX_DENOISER_SHADOWS_DECLARE_SRV(DENOISER_SHADOWS_BIND_SRV_PREVIOUS_MOMENTS);
#endif

#if FFX_HALF
     #if defined DENOISER_SHADOWS_BIND_SRV_FILTER_INPUT
         Texture2D<FfxFloat16x2>                 r_filter_input                  : FFX_DENOISER_SHADOWS_DECLARE_SRV(DENOISER_SHADOWS_BIND_SRV_FILTER_INPUT);
     #endif
#endif

// UAV declarations
#if defined DENOISER_SHADOWS_BIND_UAV_SHADOW_MASK
    RWStructuredBuffer<FfxUInt32>           rw_shadow_mask                  : FFX_DENOISER_SHADOWS_DECLARE_UAV(DENOISER_SHADOWS_BIND_UAV_SHADOW_MASK);
#endif
#if defined DENOISER_SHADOWS_BIND_UAV_RAYTRACER_RESULT
    RWStructuredBuffer<FfxUInt32>           rw_raytracer_result             : FFX_DENOISER_SHADOWS_DECLARE_UAV(DENOISER_SHADOWS_BIND_UAV_RAYTRACER_RESULT);
#endif
#if defined DENOISER_SHADOWS_BIND_UAV_TILE_METADATA
    RWStructuredBuffer<FfxUInt32>           rw_tile_metadata                : FFX_DENOISER_SHADOWS_DECLARE_UAV(DENOISER_SHADOWS_BIND_UAV_TILE_METADATA);
#endif
#if defined DENOISER_SHADOWS_BIND_UAV_REPROJECTION_RESULTS
    RWTexture2D<FfxFloat32x2>               rw_reprojection_results         : FFX_DENOISER_SHADOWS_DECLARE_UAV(DENOISER_SHADOWS_BIND_UAV_REPROJECTION_RESULTS);
#endif
#if defined DENOISER_SHADOWS_BIND_UAV_CURRENT_MOMENTS
    RWTexture2D<FfxFloat32x3>               rw_current_moments               : FFX_DENOISER_SHADOWS_DECLARE_UAV(DENOISER_SHADOWS_BIND_UAV_CURRENT_MOMENTS);
#endif
#if defined DENOISER_SHADOWS_BIND_UAV_HISTORY
    RWTexture2D<FfxFloat32x2>               rw_history                      : FFX_DENOISER_SHADOWS_DECLARE_UAV(DENOISER_SHADOWS_BIND_UAV_HISTORY);
#endif
#if defined DENOISER_SHADOWS_BIND_UAV_FILTER_OUTPUT
    RWTexture2D<unorm FfxFloat32x4>         rw_filter_output                : FFX_DENOISER_SHADOWS_DECLARE_UAV(DENOISER_SHADOWS_BIND_UAV_FILTER_OUTPUT);
#endif

#define TILE_SIZE_X 8
#define TILE_SIZE_Y 4

FfxUInt32 LaneIdToBitShift(FfxUInt32x2 localID)
{
    return localID.y * TILE_SIZE_X + localID.x;
}

FfxBoolean WaveMaskToBool(FfxUInt32 mask, FfxUInt32x2 localID)
{
    return (1 << LaneIdToBitShift(localID.xy)) & mask;
}

FfxInt32x2 BufferDimensions()
{
#if defined(DENOISER_SHADOWS_BIND_CB0_DENOISER_SHADOWS) || defined(DENOISER_SHADOWS_BIND_CB1_DENOISER_SHADOWS) || defined(DENOISER_SHADOWS_BIND_CB2_DENOISER_SHADOWS)
    return iBufferDimensions;
#else
    return 0.0f;
#endif
}

FfxBoolean HitsLight(FfxUInt32x2 did, FfxUInt32x2 gtid, FfxUInt32x2 gid)
{
#if defined(DENOISER_SHADOWS_BIND_SRV_INPUT_HIT_MASK_RESULTS)
    return !WaveMaskToBool(r_hit_mask_results[gid], gtid);
#else
    return FFX_FALSE;
#endif
}

FfxFloat32 NormalsUnpackMul()
{
#if defined(DENOISER_SHADOWS_BIND_CB1_DENOISER_SHADOWS) || defined(DENOISER_SHADOWS_BIND_CB2_DENOISER_SHADOWS)
    return normalsUnpackMul_unpackAdd[0];
#endif
    return 0;
}

FfxFloat32 NormalsUnpackAdd()
{
#if defined(DENOISER_SHADOWS_BIND_CB1_DENOISER_SHADOWS) || defined(DENOISER_SHADOWS_BIND_CB2_DENOISER_SHADOWS)
    return normalsUnpackMul_unpackAdd[1];
#endif
    return 0;
}


void StoreShadowMask(FfxUInt32 offset, FfxUInt32 value)
{
#if defined(DENOISER_SHADOWS_BIND_UAV_SHADOW_MASK)
    rw_shadow_mask[offset] = value;
#endif
}

FfxFloat32Mat4 ViewProjectionInverse()
{
#if defined(DENOISER_SHADOWS_BIND_CB1_DENOISER_SHADOWS)
    return fViewProjectionInverse;
#endif
    return 0;
}

FfxFloat32Mat4 ReprojectionMatrix()
{
#if defined(DENOISER_SHADOWS_BIND_CB1_DENOISER_SHADOWS)
    return fReprojectionMatrix;
#endif
    return 0;
}

FfxFloat32Mat4 ProjectionInverse()
{
#if defined(DENOISER_SHADOWS_BIND_CB1_DENOISER_SHADOWS) || defined(DENOISER_SHADOWS_BIND_CB2_DENOISER_SHADOWS)
    return fProjectionInverse;
#endif
    return 0;
}

FfxFloat32x2 InvBufferDimensions()
{
#if defined(DENOISER_SHADOWS_BIND_CB1_DENOISER_SHADOWS) || defined(DENOISER_SHADOWS_BIND_CB2_DENOISER_SHADOWS)
    return fInvBufferDimensions;
#else
    return 0.0f;
#endif
}

FfxFloat32x2 MotionVectorScale()
{
#if defined(DENOISER_SHADOWS_BIND_CB1_DENOISER_SHADOWS)
    return fMotionVectorScale;
#else
    return FfxFloat32x2(0, 0);
#endif
}

FfxInt32 IsFirstFrame()
{
#if defined(DENOISER_SHADOWS_BIND_CB1_DENOISER_SHADOWS)
    return iFirstFrame;
#else
    return 0;
#endif
}

FfxFloat32x3 Eye()
{
#if defined(DENOISER_SHADOWS_BIND_CB1_DENOISER_SHADOWS)
    return fEye;
#else
    return 0.0f;
#endif
}

FfxFloat32 LoadDepth(FfxInt32x2 p)
{
#if defined(DENOISER_SHADOWS_BIND_SRV_DEPTH)
    return r_depth.Load(FfxInt32x3(p, 0)).x;
#else
    return 0.f;
#endif
}

FfxFloat32 LoadPreviousDepth(FfxInt32x2 p)
{
#if defined(DENOISER_SHADOWS_BIND_SRV_PREVIOUS_DEPTH)
    return r_previous_depth.Load(FfxInt32x3(p, 0)).x;
#else
    return 0.f;
#endif
}

FfxFloat32x3 LoadNormals(FfxInt32x2 p)
{
#if defined(DENOISER_SHADOWS_BIND_SRV_NORMAL)
    FfxFloat32x3 normal = r_normal.Load(FfxInt32x3(p, 0)).xyz;
    normal = normal * NormalsUnpackMul().xxx + NormalsUnpackAdd().xxx;
    return normalize(normal);
#else
    return 0.f;
#endif
}

FfxFloat32x2 LoadVelocity(FfxInt32x2 p)
{
#if defined(DENOISER_SHADOWS_BIND_SRV_VELOCITY)
    FfxFloat32x2 velocity = r_velocity.Load(FfxInt32x3(p, 0)).rg;
    return velocity * MotionVectorScale();
#else
    return FfxFloat32x2(0, 0);
#endif
}

FfxFloat32 LoadHistory(FfxFloat32x2 p)
{
#if defined(DENOISER_SHADOWS_BIND_SRV_HISTORY)
    return r_history.SampleLevel(s_trilinerClamp, p, 0).x;
#endif
    return 0;
}

FfxFloat32x3 LoadPreviousMomentsBuffer(FfxInt32x2 p)
{
#if defined(DENOISER_SHADOWS_BIND_SRV_PREVIOUS_MOMENTS)
    return r_previous_moments.Load(FfxInt32x3(p, 0)).xyz;
#else
    return 0.f;
#endif
}

FfxUInt32 LoadRaytracedShadowMask(FfxUInt32 p)
{
#if defined(DENOISER_SHADOWS_BIND_UAV_RAYTRACER_RESULT)
    return rw_raytracer_result[p];
#else
    return 0;
#endif
}

void StoreMetadata(FfxUInt32 p, FfxUInt32 val)
{
#if defined(DENOISER_SHADOWS_BIND_UAV_TILE_METADATA)
    rw_tile_metadata[p] = val;
#endif
}

void StoreMoments(FfxUInt32x2 p, FfxFloat32x3 val)
{
#if defined(DENOISER_SHADOWS_BIND_UAV_CURRENT_MOMENTS)
    rw_current_moments[p] = val;
#endif
}

void StoreReprojectionResults(FfxUInt32x2 p, FfxFloat32x2 val)
{
#if defined(DENOISER_SHADOWS_BIND_UAV_REPROJECTION_RESULTS)
    rw_reprojection_results[p] = val;
#endif
}

#if defined(DENOISER_SHADOWS_BIND_CB2_DENOISER_SHADOWS)
FfxFloat32 DepthSimilaritySigma()
{
    return fDepthSimilaritySigma;
}
#endif

#if FFX_HALF
    FfxFloat16x2 LoadFilterInput(FfxUInt32x2 p)
    {
    #if defined(DENOISER_SHADOWS_BIND_SRV_FILTER_INPUT)
        return (FfxFloat16x2)r_filter_input.Load(FfxInt32x3(p, 0)).xy;
    #else
        return 0;
    #endif
    }
#endif

FfxBoolean IsShadowReciever(FfxUInt32x2 p)
{
    FfxFloat32 depth = LoadDepth(p);
    return (depth > 0.0f) && (depth < 1.0f);
}

FfxUInt32 LoadTileMetaData(FfxUInt32 p)
{
#if defined(DENOISER_SHADOWS_BIND_UAV_TILE_METADATA)
    return rw_tile_metadata[p];
#else
    return 0;
#endif
}

void StoreHistory(FfxUInt32x2 p, FfxFloat32x2 val)
{
#if defined(DENOISER_SHADOWS_BIND_UAV_HISTORY)
    rw_history[p] = val;
#endif
}

void StoreFilterOutput(FfxUInt32x2 p, FfxFloat32 val)
{
#if defined(DENOISER_SHADOWS_BIND_UAV_FILTER_OUTPUT)
    rw_filter_output[p].x = val;
#endif
}

#endif // #if defined(FFX_GPU)
