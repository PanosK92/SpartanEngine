/*
Copyright(c) 2015-2025 Panos Karabelas

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

float3 threshold(float3 color)
{
    const float BLOOM_THRESHOLD = 4.5;
    const float BLOOM_SOFT_KNEE = 0.5;
    const float MAX_BRIGHTNESS  = 60.0;

    color               = min(color, MAX_BRIGHTNESS);
    float brightness    = dot(color, float3(0.2126, 0.7152, 0.0722));
    float soft          = brightness - BLOOM_THRESHOLD + BLOOM_SOFT_KNEE;
    soft                = clamp(soft, 0, 2 * BLOOM_SOFT_KNEE);
    soft                = soft * soft / (4 * BLOOM_SOFT_KNEE + FLT_MIN);
    float contribution  = max(soft, brightness - BLOOM_THRESHOLD);
    contribution       /= max(brightness, FLT_MIN);
    color               = color * contribution;

    // karis average for firefly reduction
    float luma          = dot(color, float3(0.2126, 0.7152, 0.0722));
    color               /= (1.0 + luma);

    return color;
}

float3 upsample_filter(Texture2D<float4> src, float2 uv, float2 texel_size)
{
    const float BLOOM_SPREAD = 0.5f;

    // 9-tap tent filter for quality upsampling
    float3 c0 = src.SampleLevel(samplers[sampler_bilinear_clamp], uv + texel_size * float2(-1.0, -1.0) * BLOOM_SPREAD, 0).rgb * (1.0 / 16.0);
    float3 c1 = src.SampleLevel(samplers[sampler_bilinear_clamp], uv + texel_size * float2(-1.0,  1.0) * BLOOM_SPREAD, 0).rgb * (1.0 / 16.0);
    float3 c2 = src.SampleLevel(samplers[sampler_bilinear_clamp], uv + texel_size * float2( 1.0, -1.0) * BLOOM_SPREAD, 0).rgb * (1.0 / 16.0);
    float3 c3 = src.SampleLevel(samplers[sampler_bilinear_clamp], uv + texel_size * float2( 1.0,  1.0) * BLOOM_SPREAD, 0).rgb * (1.0 / 16.0);
    float3 c4 = src.SampleLevel(samplers[sampler_bilinear_clamp], uv + texel_size * float2(-1.0,  0.0) * BLOOM_SPREAD, 0).rgb * (2.0 / 16.0);
    float3 c5 = src.SampleLevel(samplers[sampler_bilinear_clamp], uv + texel_size * float2( 1.0,  0.0) * BLOOM_SPREAD, 0).rgb * (2.0 / 16.0);
    float3 c6 = src.SampleLevel(samplers[sampler_bilinear_clamp], uv + texel_size * float2( 0.0, -1.0) * BLOOM_SPREAD, 0).rgb * (2.0 / 16.0);
    float3 c7 = src.SampleLevel(samplers[sampler_bilinear_clamp], uv + texel_size * float2( 0.0,  1.0) * BLOOM_SPREAD, 0).rgb * (2.0 / 16.0);
    float3 c8 = src.SampleLevel(samplers[sampler_bilinear_clamp], uv + texel_size * float2( 0.0,  0.0) * BLOOM_SPREAD, 0).rgb * (4.0 / 16.0);

    return c0 + c1 + c2 + c3 + c4 + c5 + c6 + c7 + c8;
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
    
    float2 uv             = (thread_id.xy + 0.5) / resolution_out;
    float3 color          = tex.SampleLevel(samplers[sampler_bilinear_clamp], uv, 0).rgb;
    float3 filtered       = threshold(color);
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
    
    float3 result        = high_mip + low_mip; // additive for multi-scale accumulation

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
