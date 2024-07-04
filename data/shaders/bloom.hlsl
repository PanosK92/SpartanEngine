/*
Copyright(c) 2016-2024 Panos Karabelas

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and / or sell
copies of the Software, and to permit persons to whom the Software is furnished
to do so, subject to the following conditions :

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.IN NO EVENT SHALL THE AUTHORS OR
COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/

//= INCLUDES =========
#include "common.hlsl"
//====================

// constants
static const float BLOOM_THRESHOLD = 1.0;
static const float BLOOM_SOFT_KNEE = 0.7;
static const float BLOOM_SCATTER   = 0.8;
static const float MAX_BRIGHTNESS  = 10.0;
static const float BLOOM_SPREAD    = 5.0f;

float3 threshold(float3 color)
{
    color = min(color, MAX_BRIGHTNESS);
    float brightness     = max(color.r, max(color.g, color.b));
    float soft           = brightness - BLOOM_THRESHOLD + BLOOM_SOFT_KNEE;
    soft                 = clamp(soft, 0, 2 * BLOOM_SOFT_KNEE);
    soft                 = soft * soft / (4 * BLOOM_SOFT_KNEE + 1e-4);
    float contribution   = max(soft, brightness - BLOOM_THRESHOLD);
    contribution        /= max(brightness, 1e-4);
    return color * contribution;
}

float3 upsample_filter(Texture2D<float4> src, float2 uv, float2 texel_size)
{
    float4 offset = texel_size.xyxy * float4(-1, -1, 1, 1) * BLOOM_SPREAD;

    // 13-tap filter with extended reach
    float3 a = src.SampleLevel(samplers[sampler_bilinear_clamp], uv + offset.xy * 2, 0).rgb;
    float3 b = src.SampleLevel(samplers[sampler_bilinear_clamp], uv + offset.xy, 0).rgb;
    float3 c = src.SampleLevel(samplers[sampler_bilinear_clamp], uv + float2(offset.x, 0), 0).rgb;
    float3 d = src.SampleLevel(samplers[sampler_bilinear_clamp], uv + float2(offset.x, offset.w), 0).rgb;
    float3 e = src.SampleLevel(samplers[sampler_bilinear_clamp], uv + float2(0, offset.y), 0).rgb;
    float3 f = src.SampleLevel(samplers[sampler_bilinear_clamp], uv, 0).rgb;
    float3 g = src.SampleLevel(samplers[sampler_bilinear_clamp], uv + float2(0, offset.w), 0).rgb;
    float3 h = src.SampleLevel(samplers[sampler_bilinear_clamp], uv + float2(offset.z, offset.y), 0).rgb;
    float3 i = src.SampleLevel(samplers[sampler_bilinear_clamp], uv + float2(offset.z, 0), 0).rgb;
    float3 j = src.SampleLevel(samplers[sampler_bilinear_clamp], uv + offset.zw, 0).rgb;
    float3 k = src.SampleLevel(samplers[sampler_bilinear_clamp], uv + offset.zw * 2, 0).rgb;

    // Apply weights (gaussian-like)
    float3 result = f * 0.25;
    result += (c + e + g + i) * 0.125;
    result += (b + d + h + j) * 0.0625;
    result += (a + k) * 0.03125;

    return result;
}

// shader entry points
#if LUMINANCE
[numthreads(THREAD_GROUP_COUNT_X, THREAD_GROUP_COUNT_Y, 1)]
void main_cs(uint3 thread_id : SV_DispatchThreadID)
{
    float2 resolution_out;
    tex_uav.GetDimensions(resolution_out.x, resolution_out.y);
    if (any(int2(thread_id.xy) >= resolution_out))
        return;
    
    float2 uv            = (thread_id.xy + 0.5) / resolution_out;
    float3 color         = tex.SampleLevel(samplers[sampler_bilinear_clamp], uv, 0).rgb;
    float3 filtered      = threshold(color);
    tex_uav[thread_id.xy] = float4(saturate_16(filtered), 1.0);
}
#endif

#if UPSAMPLE_BLEND_MIP
[numthreads(THREAD_GROUP_COUNT_X, THREAD_GROUP_COUNT_Y, 1)]
void main_cs(uint3 thread_id : SV_DispatchThreadID)
{
    float2 resolution_out;
    tex_uav.GetDimensions(resolution_out.x, resolution_out.y);
    if (any(int2(thread_id.xy) >= resolution_out))
        return;
    
    float2 uv            = (thread_id.xy + 0.5) / resolution_out;
    float2 texel_size    = 1.0 / resolution_out;
    
    float3 low_mip       = upsample_filter(tex, uv, texel_size);
    float3 high_mip      = tex_uav[thread_id.xy].rgb;
    
    // Blend with emphasis on the low mip to encourage spread
    float blend_factor   = 0.7; // Increased weight for low mip
    float3 result        = lerp(high_mip, low_mip, blend_factor);
    
    // Add a small amount of the high mip back to preserve some local detail
    result += high_mip * 0.3;

    // Soft clamp to prevent over-brightening
    float max_value = max(max(result.r, result.g), result.b);
    if (max_value > 1.0)
        result *= 1.0 / max_value;

    tex_uav[thread_id.xy] = float4(saturate_16(result), 1.0);
}
#endif

#if BLEND_FRAME
[numthreads(THREAD_GROUP_COUNT_X, THREAD_GROUP_COUNT_Y, 1)]
void main_cs(uint3 thread_id : SV_DispatchThreadID)
{
    float2 resolution_out;
    tex_uav.GetDimensions(resolution_out.x, resolution_out.y);
    if (any(int2(thread_id.xy) >= resolution_out))
        return;
    
    float4 color_frame = tex[thread_id.xy];
    float4 color_bloom = tex2[thread_id.xy];

    float bloom_intensity = pass_get_f3_value().x;
    float3 result         = color_frame.rgb + color_bloom.rgb * bloom_intensity;
    tex_uav[thread_id.xy] = float4(saturate_16(result), color_frame.a);
}
#endif

