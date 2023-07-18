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
#include "ffx_core.h"
#endif // #if defined(FFX_GPU)

#if defined(FFX_GPU)
#ifndef FFX_PREFER_WAVE64
#define FFX_PREFER_WAVE64
#endif // #if defined(FFX_GPU)

#if defined(DENOISER_BIND_CB_DENOISER)
    layout (set = 0, binding = DENOISER_BIND_CB_DENOISER, std140) uniform cbDenoiserReflections_t
    {
        FfxFloat32Mat4  invProjection;
        FfxFloat32Mat4  invView;
        FfxFloat32Mat4  prevViewProjection;
        FfxUInt32x2     renderSize;
        FfxFloat32x2    inverseRenderSize;
        FfxFloat32x2    motionVectorScale;
        FfxFloat32      normalsUnpackMul;
        FfxFloat32      normalsUnpackAdd;
        FfxBoolean      isRoughnessPerceptual;
        FfxFloat32      temporalStabilityFactor;
        FfxFloat32      roughnessThreshold;
    } cbDenoiserReflections;

FfxFloat32Mat4 InvProjection()
{
#if defined DENOISER_BIND_CB_DENOISER
    return cbDenoiserReflections.invProjection;
#else
    return FfxFloat32Mat4(0.0f);
#endif
}

FfxFloat32Mat4 InvView()
{
#if defined DENOISER_BIND_CB_DENOISER
    return cbDenoiserReflections.invView;
#else
    return FfxFloat32Mat4(0.0f);
#endif
}

FfxFloat32Mat4 PrevViewProjection()
{
#if defined DENOISER_BIND_CB_DENOISER
    return cbDenoiserReflections.prevViewProjection;
#else
    return FfxFloat32Mat4(0.0f);
#endif
}

FfxUInt32x2 RenderSize()
{
#if defined DENOISER_BIND_CB_DENOISER
    return cbDenoiserReflections.renderSize;
#else
    return FfxUInt32x2(0);
#endif
}

FfxFloat32x2 InverseRenderSize()
{
#if defined DENOISER_BIND_CB_DENOISER
    return cbDenoiserReflections.inverseRenderSize;
#else
    return FfxFloat32x2(0.0f);
#endif
}

FfxFloat32x2 MotionVectorScale()
{
#if defined DENOISER_BIND_CB_DENOISER
    return cbDenoiserReflections.motionVectorScale;
#else
    return FfxFloat32x2(0.0f);
#endif
}

FfxFloat32 NormalsUnpackMul()
{
#if defined DENOISER_BIND_CB_DENOISER
    return cbDenoiserReflections.normalsUnpackMul;
#else
    return 0.0f;
#endif
}

FfxFloat32 NormalsUnpackAdd()
{
#if defined DENOISER_BIND_CB_DENOISER
    return cbDenoiserReflections.normalsUnpackAdd;
#else
    return 0.0f;
#endif
}

FfxBoolean IsRoughnessPerceptual()
{
#if defined DENOISER_BIND_CB_DENOISER
    return cbDenoiserReflections.isRoughnessPerceptual;
#else
    return false;
#endif
}

FfxFloat32 TemporalStabilityFactor()
{
#if defined DENOISER_BIND_CB_DENOISER
    return cbDenoiserReflections.temporalStabilityFactor;
#else
    return 0.0f;
#endif
}

FfxFloat32 RoughnessThreshold()
{
#if defined DENOISER_BIND_CB_DENOISER
    return cbDenoiserReflections.roughnessThreshold;
#else
    return 0.0f;
#endif
}

#endif // #if defined(DENOISER_BIND_CB_DENOISER)

layout (set = 0, binding = 1000) uniform sampler s_LinearSampler;

#if defined DENOISER_BIND_SRV_INPUT_DEPTH_HIERARCHY
    layout (set = 0, binding = DENOISER_BIND_SRV_INPUT_DEPTH_HIERARCHY) uniform texture2D r_input_depth_hierarchy;
#endif
#if defined DENOISER_BIND_SRV_INPUT_MOTION_VECTORS
    layout (set = 0, binding = DENOISER_BIND_SRV_INPUT_MOTION_VECTORS)  uniform texture2D r_input_motion_vectors;
#endif
#if defined DENOISER_BIND_SRV_INPUT_NORMAL
    layout (set = 0, binding = DENOISER_BIND_SRV_INPUT_NORMAL)          uniform texture2D r_input_normal;
#endif
#if defined DENOISER_BIND_SRV_RADIANCE
    layout (set = 0, binding = DENOISER_BIND_SRV_RADIANCE)              uniform texture2D r_radiance;
#endif
#if defined DENOISER_BIND_SRV_RADIANCE_HISTORY
    layout (set = 0, binding = DENOISER_BIND_SRV_RADIANCE_HISTORY)      uniform texture2D r_radiance_history;
#endif
#if defined DENOISER_BIND_SRV_VARIANCE
    layout (set = 0, binding = DENOISER_BIND_SRV_VARIANCE)              uniform texture2D r_variance;
#endif
#if defined DENOISER_BIND_SRV_SAMPLE_COUNT
    layout (set = 0, binding = DENOISER_BIND_SRV_SAMPLE_COUNT)          uniform texture2D r_sample_count;
#endif
#if defined DENOISER_BIND_SRV_AVERAGE_RADIANCE
    layout (set = 0, binding = DENOISER_BIND_SRV_AVERAGE_RADIANCE)      uniform texture2D r_average_radiance;
#endif
#if defined DENOISER_BIND_SRV_EXTRACTED_ROUGHNESS
    layout (set = 0, binding = DENOISER_BIND_SRV_EXTRACTED_ROUGHNESS)   uniform texture2D r_extracted_roughness;
#endif
#if defined DENOISER_BIND_SRV_DEPTH_HISTORY
    layout (set = 0, binding = DENOISER_BIND_SRV_DEPTH_HISTORY)         uniform texture2D r_depth_history;
#endif
#if defined DENOISER_BIND_SRV_NORMAL_HISTORY
    layout (set = 0, binding = DENOISER_BIND_SRV_NORMAL_HISTORY)        uniform texture2D r_normal_history;
#endif
#if defined DENOISER_BIND_SRV_ROUGHNESS_HISTORY
    layout (set = 0, binding = DENOISER_BIND_SRV_ROUGHNESS_HISTORY)     uniform texture2D r_roughness_history;
#endif
#if defined DENOISER_BIND_SRV_REPROJECTED_RADIANCE
    layout (set = 0, binding = DENOISER_BIND_SRV_REPROJECTED_RADIANCE)  uniform texture2D r_reprojected_radiance;
#endif

// UAVs
#if defined DENOISER_BIND_UAV_RADIANCE
        layout (set = 0, binding = DENOISER_BIND_UAV_RADIANCE, rgba16f)                 uniform image2D rw_radiance;
#endif
#if defined DENOISER_BIND_UAV_VARIANCE
        layout (set = 0, binding = DENOISER_BIND_UAV_VARIANCE, r16f)                    uniform image2D rw_variance;
#endif
#if defined DENOISER_BIND_UAV_SAMPLE_COUNT
        layout (set = 0, binding = DENOISER_BIND_UAV_SAMPLE_COUNT, r16f)                uniform image2D rw_sample_count;
#endif
#if defined DENOISER_BIND_UAV_AVERAGE_RADIANCE
        layout (set = 0, binding = DENOISER_BIND_UAV_AVERAGE_RADIANCE, r11f_g11f_b10f)  uniform image2D rw_average_radiance;
#endif
#if defined DENOISER_BIND_UAV_DENOISER_TILE_LIST
        layout (set = 0, binding = DENOISER_BIND_UAV_DENOISER_TILE_LIST, std430)        buffer rw_denoiser_tile_list_t
        {
            FfxUInt32 data[];
        } rw_denoiser_tile_list; 
#endif
#if defined DENOISER_BIND_UAV_REPROJECTED_RADIANCE
        layout (set = 0, binding = DENOISER_BIND_UAV_REPROJECTED_RADIANCE, rgba16f)     uniform image2D rw_reprojected_radiance;
#endif

#if FFX_HALF

FfxFloat16x3 FFX_DENOISER_LoadWorldSpaceNormalH(FfxInt32x2 pixel_coordinate)
{
#if defined(DENOISER_BIND_SRV_INPUT_NORMAL) 
    return normalize(FfxFloat16x3(NormalsUnpackMul() * texelFetch(r_input_normal, pixel_coordinate, 0).xyz + NormalsUnpackAdd()));
#else
    return FfxFloat16x3(0.0f);
#endif
}

FfxFloat16x3 LoadRadianceH(FfxInt32x3 coordinate)
{
#if defined (DENOISER_BIND_SRV_RADIANCE) 
    return FfxFloat16x3(texelFetch(r_radiance, coordinate.xy, coordinate.z).xyz);
#else
    return FfxFloat16x3(0.0f);
#endif
}

FfxFloat16 LoadVarianceH(FfxInt32x3 coordinate)
{
#if defined (DENOISER_BIND_SRV_VARIANCE) 
    return FfxFloat16(texelFetch(r_variance, coordinate.xy, coordinate.z).x);
#else
    return FfxFloat16(0.0f);
#endif
}

FfxFloat16x3 FFX_DNSR_Reflections_SampleAverageRadiance(FfxFloat32x2 uv)
{
#if defined (DENOISER_BIND_SRV_AVERAGE_RADIANCE) 
    return FfxFloat16x3(textureLod(sampler2D(r_average_radiance, s_LinearSampler), uv, 0.0f).xyz);
#else
    return FfxFloat16x3(0.0f);
#endif
}

FfxFloat16 FFX_DNSR_Reflections_LoadRoughness(FfxInt32x2 pixel_coordinate)
{
#if defined (DENOISER_BIND_SRV_EXTRACTED_ROUGHNESS) 
    FfxFloat16 rawRoughness = FfxFloat16(texelFetch(r_extracted_roughness, pixel_coordinate, 0).x);
    if (IsRoughnessPerceptual())
    {
        rawRoughness *= rawRoughness;
    }

    return rawRoughness;
#else
    return FfxFloat16(0.0f);
#endif
}

void StoreRadianceH(FfxInt32x2 coordinate, FfxFloat16x4 radiance)
{
#if defined (DENOISER_BIND_UAV_RADIANCE) 
    imageStore(rw_radiance, coordinate, radiance);
#endif
}

void StoreVarianceH(FfxInt32x2 coordinate, FfxFloat16 variance)
{
#if defined (DENOISER_BIND_UAV_VARIANCE) 
    imageStore(rw_variance, coordinate, FfxFloat16x4(variance));
#endif
}

void FFX_DNSR_Reflections_StorePrefilteredReflections(FfxInt32x2 pixel_coordinate, FfxFloat16x3 radiance, FfxFloat16 variance)
{
    StoreRadianceH(pixel_coordinate, radiance.xyzz);
    StoreVarianceH(pixel_coordinate, variance.x);
}

void FFX_DNSR_Reflections_StoreTemporalAccumulation(FfxInt32x2 pixel_coordinate, FfxFloat16x3 radiance, FfxFloat16 variance)
{
    StoreRadianceH(pixel_coordinate, radiance.xyzz);
    StoreVarianceH(pixel_coordinate, variance.x);
}

FfxFloat16x3 FFX_DNSR_Reflections_LoadRadianceHistory(FfxInt32x2 pixel_coordinate)
{
#if defined (DENOISER_BIND_SRV_RADIANCE_HISTORY) 
    return FfxFloat16x3(texelFetch(r_radiance_history, pixel_coordinate, 0).xyz);
#else
    return FfxFloat16x3(0.0f);
#endif
}

FfxFloat16x3 FFX_DNSR_Reflections_SampleRadianceHistory(FfxFloat32x2 uv)
{
#if defined (DENOISER_BIND_SRV_RADIANCE_HISTORY) 
    return FfxFloat16x3(textureLod(sampler2D(r_radiance_history, s_LinearSampler), uv, 0.0f).xyz);
#else
    return FfxFloat16x3(0.0f);
#endif
}

FfxFloat16 FFX_DNSR_Reflections_SampleVarianceHistory(FfxFloat32x2 uv)
{
#if defined (DENOISER_BIND_SRV_VARIANCE) 
    return FfxFloat16(textureLod(sampler2D(r_variance, s_LinearSampler), uv, 0.0f).x);
#else
    return FfxFloat16(0.0f);
#endif
}

FfxFloat16 FFX_DNSR_Reflections_SampleNumSamplesHistory(FfxFloat32x2 uv)
{
#if defined (DENOISER_BIND_SRV_SAMPLE_COUNT) 
    return FfxFloat16(textureLod(sampler2D(r_sample_count, s_LinearSampler), uv, 0.0f).x);
#else
    return FfxFloat16(0.0f);
#endif
}

void FFX_DNSR_Reflections_StoreRadianceReprojected(FfxInt32x2 pixel_coordinate, FfxFloat16x3 value)
{
#if defined (DENOISER_BIND_UAV_REPROJECTED_RADIANCE) 
    imageStore(rw_reprojected_radiance, pixel_coordinate, FfxFloat16x4(value, 0.0f));
#endif
}

void FFX_DNSR_Reflections_StoreAverageRadiance(FfxInt32x2 pixel_coordinate, FfxFloat16x3 value)
{
#if defined (DENOISER_BIND_UAV_AVERAGE_RADIANCE) 
    imageStore(rw_average_radiance, pixel_coordinate, FfxFloat16x4(value, 0.0f));
#endif
}

FfxFloat16x3 FFX_DNSR_Reflections_LoadWorldSpaceNormal(FfxInt32x2 pixel_coordinate)
{
    return FFX_DENOISER_LoadWorldSpaceNormalH(pixel_coordinate);
}

FfxFloat16 FFX_DNSR_Reflections_SampleRoughnessHistory(FfxFloat32x2 uv)
{
#if defined (DENOISER_BIND_SRV_ROUGHNESS_HISTORY) 
    FfxFloat16 rawRoughness = FfxFloat16(textureLod(sampler2D(r_roughness_history, s_LinearSampler), uv, 0.0f).x);
    if (IsRoughnessPerceptual())
    {
        rawRoughness *= rawRoughness;
    }

    return rawRoughness;
#else
    return FfxFloat16(0.0f);
#endif
}

FfxFloat16x3 FFX_DNSR_Reflections_LoadWorldSpaceNormalHistory(FfxInt32x2 pixel_coordinate)
{
#if defined (DENOISER_BIND_SRV_NORMAL_HISTORY) 
    return normalize(FfxFloat16x3(NormalsUnpackMul() * texelFetch(r_normal_history, pixel_coordinate.xy, 0).xyz + NormalsUnpackAdd()));
#else
    return FfxFloat16x3(0.0f);
#endif
}

FfxFloat16x3 FFX_DNSR_Reflections_SampleWorldSpaceNormalHistory(FfxFloat32x2 uv)
{
#if defined (DENOISER_BIND_SRV_NORMAL_HISTORY) 
    return normalize(FfxFloat16x3(NormalsUnpackMul() * textureLod(sampler2D(r_normal_history, s_LinearSampler), uv, 0.0f).xyz + NormalsUnpackAdd()));
#else
    return FfxFloat16x3(0.0f);
#endif
}

FfxFloat16 FFX_DNSR_Reflections_LoadRayLength(FfxInt32x2 pixel_coordinate)
{
#if defined (DENOISER_BIND_SRV_RADIANCE) 
    return FfxFloat16(texelFetch(r_radiance, pixel_coordinate, 0).w);
#else
    return FfxFloat16(0.0f);
#endif
}

void FFX_DNSR_Reflections_StoreVariance(FfxInt32x2 pixel_coordinate, FfxFloat16 value)
{
    StoreVarianceH(pixel_coordinate, value);
}

void FFX_DNSR_Reflections_StoreNumSamples(FfxInt32x2 pixel_coordinate, FfxFloat16 value)
{
#if defined (DENOISER_BIND_UAV_SAMPLE_COUNT) 
    imageStore(rw_sample_count, pixel_coordinate, FfxFloat16x4(value));
#endif
}

FfxFloat16x3 FFX_DNSR_Reflections_LoadRadiance(FfxInt32x2 pixel_coordinate)
{
    return LoadRadianceH(FfxInt32x3(pixel_coordinate, 0));
}

FfxFloat16x3 FFX_DNSR_Reflections_LoadRadianceReprojected(FfxInt32x2 pixel_coordinate)
{
#if defined (DENOISER_BIND_SRV_REPROJECTED_RADIANCE) 
    return FfxFloat16x3(texelFetch(r_reprojected_radiance, pixel_coordinate, 0).xyz);
#else
    return FfxFloat16x3(0.0f);
#endif
}

FfxFloat16 FFX_DNSR_Reflections_LoadVariance(FfxInt32x2 pixel_coordinate)
{
    return LoadVarianceH(FfxInt32x3(pixel_coordinate, 0));
}

FfxFloat16 FFX_DNSR_Reflections_LoadNumSamples(FfxInt32x2 pixel_coordinate)
{
#if defined (DENOISER_BIND_SRV_SAMPLE_COUNT) 
    return FfxFloat16(texelFetch(r_sample_count, pixel_coordinate, 0).x);
#else
    return FfxFloat16(0.0f);
#endif
}

#else   // FFX_HALF

FfxFloat32x3 LoadRadiance(FfxInt32x3 coordinate)
{
#if defined (DENOISER_BIND_SRV_RADIANCE) 
    return texelFetch(r_radiance, coordinate.xy, coordinate.z).xyz;
#else
    return FfxFloat32x3(0.0f);
#endif
}

FfxFloat32 LoadVariance(FfxInt32x3 coordinate)
{
#if defined (DENOISER_BIND_SRV_VARIANCE) 
    return texelFetch(r_variance, coordinate.xy, coordinate.z).x;
#else
    return 0.0f;
#endif
}

FfxFloat32x3 FFX_DENOISER_LoadWorldSpaceNormal(FfxInt32x2 pixel_coordinate)
{
#if defined(DENOISER_BIND_SRV_INPUT_NORMAL) 
    return normalize(NormalsUnpackMul() * texelFetch(r_input_normal, pixel_coordinate, 0).xyz + NormalsUnpackAdd());
#else
    return FfxFloat32x3(0.0f);
#endif
}

FfxFloat32 FFX_DNSR_Reflections_LoadRoughness(FfxInt32x2 pixel_coordinate)
{
#if defined (DENOISER_BIND_SRV_EXTRACTED_ROUGHNESS) 
    FfxFloat32 rawRoughness = FfxFloat32(texelFetch(r_extracted_roughness, pixel_coordinate, 0).x);
    if (IsRoughnessPerceptual())
    {
        rawRoughness *= rawRoughness;
    }

    return rawRoughness;
#else
    return FfxFloat32(0.0f);
#endif
}

void StoreRadiance(FfxInt32x2 coordinate, FfxFloat32x4 radiance)
{
#if defined (DENOISER_BIND_UAV_RADIANCE) 
    imageStore(rw_radiance, coordinate, radiance);
#endif
}

void StoreVariance(FfxInt32x2 coordinate, FfxFloat32 variance)
{
#if defined (DENOISER_BIND_UAV_VARIANCE) 
    imageStore(rw_variance, coordinate, FfxFloat32x4(variance));
#endif
}

void FFX_DNSR_Reflections_StorePrefilteredReflections(FfxInt32x2 pixel_coordinate, FfxFloat32x3 radiance, FfxFloat32 variance)
{
    StoreRadiance(pixel_coordinate, radiance.xyzz);
    StoreVariance(pixel_coordinate, variance.x);
}

void FFX_DNSR_Reflections_StoreTemporalAccumulation(FfxInt32x2 pixel_coordinate, FfxFloat32x3 radiance, FfxFloat32 variance)
{
    StoreRadiance(pixel_coordinate, radiance.xyzz);
    StoreVariance(pixel_coordinate, variance.x);
}

FfxFloat32x3 FFX_DNSR_Reflections_SampleAverageRadiance(FfxFloat32x2 uv)
{
#if defined (DENOISER_BIND_SRV_AVERAGE_RADIANCE) 
    return FfxFloat32x3(textureLod(sampler2D(r_average_radiance, s_LinearSampler), uv, 0.0f).xyz);
#else
    return FfxFloat32x3(0.0f);
#endif
}

FfxFloat32x3 FFX_DNSR_Reflections_LoadRadianceHistory(FfxInt32x2 pixel_coordinate)
{
#if defined (DENOISER_BIND_SRV_RADIANCE_HISTORY) 
    return FfxFloat32x3(texelFetch(r_radiance_history, pixel_coordinate, 0).xyz);
#else
    return FfxFloat32x3(0.0f);
#endif
}

FfxFloat32x3 FFX_DNSR_Reflections_SampleRadianceHistory(FfxFloat32x2 uv)
{
#if defined (DENOISER_BIND_SRV_RADIANCE_HISTORY) 
    return FfxFloat32x3(textureLod(sampler2D(r_radiance_history, s_LinearSampler), uv, 0.0f).xyz);
#else
    return FfxFloat32x3(0.0f);
#endif
}

FfxFloat32 FFX_DNSR_Reflections_SampleVarianceHistory(FfxFloat32x2 uv)
{
#if defined (DENOISER_BIND_SRV_VARIANCE) 
    return FfxFloat32(textureLod(sampler2D(r_variance, s_LinearSampler), uv, 0.0f).x);
#else
    return FfxFloat32(0.0f);
#endif
}

FfxFloat32 FFX_DNSR_Reflections_SampleNumSamplesHistory(FfxFloat32x2 uv)
{
#if defined (DENOISER_BIND_SRV_SAMPLE_COUNT) 
    return FfxFloat32(textureLod(sampler2D(r_sample_count, s_LinearSampler), uv, 0.0f).x);
#else
    return FfxFloat32(0.0f);
#endif
}

void FFX_DNSR_Reflections_StoreRadianceReprojected(FfxInt32x2 pixel_coordinate, FfxFloat32x3 value)
{
#if defined (DENOISER_BIND_UAV_REPROJECTED_RADIANCE) 
    imageStore(rw_reprojected_radiance, pixel_coordinate, FfxFloat32x4(value, 0.0f));
#endif
}

void FFX_DNSR_Reflections_StoreAverageRadiance(FfxInt32x2 pixel_coordinate, FfxFloat32x3 value)
{
#if defined (DENOISER_BIND_UAV_AVERAGE_RADIANCE) 
    imageStore(rw_average_radiance, pixel_coordinate, FfxFloat32x4(value, 0.0f));
#endif
}

FfxFloat32x3 FFX_DNSR_Reflections_LoadWorldSpaceNormal(FfxInt32x2 pixel_coordinate)
{
    return FFX_DENOISER_LoadWorldSpaceNormal(pixel_coordinate);
}

FfxFloat32 FFX_DNSR_Reflections_SampleRoughnessHistory(FfxFloat32x2 uv)
{
#if defined (DENOISER_BIND_SRV_ROUGHNESS_HISTORY) 
    FfxFloat32 rawRoughness = FfxFloat32(textureLod(sampler2D(r_roughness_history, s_LinearSampler), uv, 0.0f).x);
    if (IsRoughnessPerceptual())
    {
        rawRoughness *= rawRoughness;
    }

    return rawRoughness;
#else
    return FfxFloat32(0.0f);
#endif
}

FfxFloat32x3 FFX_DNSR_Reflections_LoadWorldSpaceNormalHistory(FfxInt32x2 pixel_coordinate)
{
#if defined (DENOISER_BIND_SRV_NORMAL_HISTORY) 
    return normalize(FfxFloat32x3(NormalsUnpackMul() * texelFetch(r_normal_history, pixel_coordinate.xy, 0).xyz + NormalsUnpackAdd()));
#else
    return FfxFloat32x3(0.0f);
#endif
}

FfxFloat32x3 FFX_DNSR_Reflections_SampleWorldSpaceNormalHistory(FfxFloat32x2 uv)
{
#if defined (DENOISER_BIND_SRV_NORMAL_HISTORY) 
    return normalize(FfxFloat32x3(NormalsUnpackMul() * textureLod(sampler2D(r_normal_history, s_LinearSampler), uv, 0.0f).xyz + NormalsUnpackAdd()));
#else
    return FfxFloat32x3(0.0f);
#endif
}

FfxFloat32 FFX_DNSR_Reflections_LoadRayLength(FfxInt32x2 pixel_coordinate)
{
#if defined (DENOISER_BIND_SRV_RADIANCE) 
    return FfxFloat32(texelFetch(r_radiance, pixel_coordinate, 0).w);
#else
    return FfxFloat32(0.0f);
#endif
}

void FFX_DNSR_Reflections_StoreVariance(FfxInt32x2 pixel_coordinate, FfxFloat32 value)
{
    StoreVariance(pixel_coordinate, value);
}

void FFX_DNSR_Reflections_StoreNumSamples(FfxInt32x2 pixel_coordinate, FfxFloat32 value)
{
#if defined (DENOISER_BIND_UAV_SAMPLE_COUNT) 
    imageStore(rw_sample_count, pixel_coordinate, FfxFloat32x4(value));
#endif
}

FfxFloat32x3 FFX_DNSR_Reflections_LoadRadiance(FfxInt32x2 pixel_coordinate)
{
    return LoadRadiance(FfxInt32x3(pixel_coordinate, 0));
}

FfxFloat32x3 FFX_DNSR_Reflections_LoadRadianceReprojected(FfxInt32x2 pixel_coordinate)
{
#if defined (DENOISER_BIND_SRV_REPROJECTED_RADIANCE) 
    return FfxFloat32x3(texelFetch(r_reprojected_radiance, pixel_coordinate, 0).xyz);
#else
    return FfxFloat32x3(0.0f);
#endif
}

FfxFloat32 FFX_DNSR_Reflections_LoadVariance(FfxInt32x2 pixel_coordinate)
{
    return LoadVariance(FfxInt32x3(pixel_coordinate, 0));
}

FfxFloat32 FFX_DNSR_Reflections_LoadNumSamples(FfxInt32x2 pixel_coordinate)
{
#if defined (DENOISER_BIND_SRV_SAMPLE_COUNT) 
    return FfxFloat32(texelFetch(r_sample_count, pixel_coordinate, 0).x);
#else
    return FfxFloat32(0.0f);
#endif
}

#endif // #if defined(FFX_HALF)

FfxFloat32 FFX_DENOISER_LoadDepth(FfxInt32x2 pixel_coordinate, FfxInt32 mip)
{
#if defined(DENOISER_BIND_SRV_INPUT_DEPTH_HIERARCHY) 
    return texelFetch(r_input_depth_hierarchy, pixel_coordinate, mip).x;
#else
    return 0.0f;
#endif
}

FfxUInt32 GetDenoiserTile(FfxUInt32 group_id)
{
#if defined (DENOISER_BIND_UAV_DENOISER_TILE_LIST) 
    return rw_denoiser_tile_list.data[group_id];
#else
    return 0;
#endif
}

FfxFloat32x2 FFX_DNSR_Reflections_LoadMotionVector(FfxInt32x2 pixel_coordinate)
{
#if defined (DENOISER_BIND_SRV_INPUT_MOTION_VECTORS) 
    return MotionVectorScale() * texelFetch(r_input_motion_vectors, pixel_coordinate, 0).xy;
#else
    return FfxFloat32x2(0.0f);
#endif
}

FfxFloat32 FFX_DNSR_Reflections_LoadDepth(FfxInt32x2 pixel_coordinate)
{
    return FFX_DENOISER_LoadDepth(pixel_coordinate, 0);
}

FfxFloat32 FFX_DNSR_Reflections_LoadDepthHistory(FfxInt32x2 pixel_coordinate)
{
#if defined (DENOISER_BIND_SRV_DEPTH_HISTORY) 
    return texelFetch(r_depth_history, pixel_coordinate, 0).x;
#else
    return 0.0f;
#endif
}

FfxFloat32 FFX_DNSR_Reflections_SampleDepthHistory(FfxFloat32x2 uv)
{
#if defined (DENOISER_BIND_SRV_DEPTH_HISTORY) 
    return textureLod(sampler2D(r_depth_history, s_LinearSampler), uv, 0.0f).x;
#else
    return 0.0f;
#endif
}

#endif // #if defined(FFX_GPU)
