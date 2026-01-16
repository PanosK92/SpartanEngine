/*
Copyright(c) 2015-2026 Panos Karabelas

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

//= includes =========
#include "common.hlsl"
//====================

// helper: get luminance
// standard rec.709 luma coefficients
float get_luminance(float3 color)
{
    return dot(color, float3(0.2126f, 0.7152f, 0.0722f));
}

// helper: karis average
// suppresses fireflies by weighting bright pixels less
float get_karis_weight(float3 color)
{
    return 1.0f / (1.0f + get_luminance(color));
}

// helper: 13-tap downsample
// ensures stability for thin geometry like tube lights
float3 downsample_stable(Texture2D<float4> src, float2 uv, float2 texel_size)
{
    float2 d = texel_size.xy;

    // center sample
    float3 s0 = src.SampleLevel(samplers[sampler_bilinear_clamp], uv, 0).rgb;
    
    // inner box
    float3 s1 = src.SampleLevel(samplers[sampler_bilinear_clamp], uv + float2(-d.x, -d.y), 0).rgb;
    float3 s2 = src.SampleLevel(samplers[sampler_bilinear_clamp], uv + float2( d.x, -d.y), 0).rgb;
    float3 s3 = src.SampleLevel(samplers[sampler_bilinear_clamp], uv + float2(-d.x,  d.y), 0).rgb;
    float3 s4 = src.SampleLevel(samplers[sampler_bilinear_clamp], uv + float2( d.x,  d.y), 0).rgb;
    
    // outer box
    float3 s5 = src.SampleLevel(samplers[sampler_bilinear_clamp], uv + float2(-d.x * 2, -d.y * 2), 0).rgb;
    float3 s6 = src.SampleLevel(samplers[sampler_bilinear_clamp], uv + float2( d.x * 2, -d.y * 2), 0).rgb;
    float3 s7 = src.SampleLevel(samplers[sampler_bilinear_clamp], uv + float2(-d.x * 2,  d.y * 2), 0).rgb;
    float3 s8 = src.SampleLevel(samplers[sampler_bilinear_clamp], uv + float2( d.x * 2,  d.y * 2), 0).rgb;

    // karis weights
    float w0 = get_karis_weight(s0);
    float w1 = get_karis_weight(s1);
    float w2 = get_karis_weight(s2);
    float w3 = get_karis_weight(s3);
    float w4 = get_karis_weight(s4);
    
    // weighted sum
    float3 m1 = s0 * w0 * 0.125f;
    float3 m2 = (s1 * w1 + s2 * w2 + s3 * w3 + s4 * w4) * 0.125f;
    float3 m3 = (s5 + s6 + s7 + s8) * 0.03125f;
    
    float w_sum = (w0 * 0.125f) + 
                  ((w1 + w2 + w3 + w4) * 0.125f) + 
                  (4.0f * 0.03125f);

    return (m1 + m2 + m3) / max(w_sum, 0.00001f);
}

float3 threshold(float3 color)
{
    // config: threshold in watts
    const float THRESHOLD = 0.366f; 
    const float KNEE      = THRESHOLD * 0.5f;

    float brightness = get_luminance(color);

    // soft knee curve
    float soft = brightness - THRESHOLD + KNEE;
    soft       = clamp(soft, 0.0f, 2.0f * KNEE);
    soft       = soft * soft / (4.0f * KNEE + 0.00001f);
    
    float contribution = max(soft, brightness - THRESHOLD);
    contribution      /= max(brightness, 0.00001f);
    
    return color * contribution;
}

float3 upsample_filter(Texture2D<float4> src, float2 uv, float2 texel_size)
{
    // config: radius
    // 3.0 creates a smooth cinematic blur
    const float RADIUS = 3.0f;
    float4 d = texel_size.xyxy * float4(-1.0f, -1.0f, 1.0f, 1.0f) * RADIUS;

    // 9-tap tent filter
    float3 s0 = src.SampleLevel(samplers[sampler_bilinear_clamp], uv + d.xy, 0).rgb;
    float3 s1 = src.SampleLevel(samplers[sampler_bilinear_clamp], uv + d.zy, 0).rgb;
    float3 s2 = src.SampleLevel(samplers[sampler_bilinear_clamp], uv + d.xw, 0).rgb;
    float3 s3 = src.SampleLevel(samplers[sampler_bilinear_clamp], uv + d.zw, 0).rgb;
    
    float3 s4 = src.SampleLevel(samplers[sampler_bilinear_clamp], uv + float2(0.0f, d.y), 0).rgb;
    float3 s5 = src.SampleLevel(samplers[sampler_bilinear_clamp], uv + float2(0.0f, d.w), 0).rgb;
    float3 s6 = src.SampleLevel(samplers[sampler_bilinear_clamp], uv + float2(d.x, 0.0f), 0).rgb;
    float3 s7 = src.SampleLevel(samplers[sampler_bilinear_clamp], uv + float2(d.z, 0.0f), 0).rgb;
    
    float3 s8 = src.SampleLevel(samplers[sampler_bilinear_clamp], uv, 0).rgb;

    return (
        (s0 + s1 + s2 + s3) * 1.0f +
        (s4 + s5 + s6 + s7) * 2.0f +
        s8                  * 4.0f
    ) * (1.0f / 16.0f);
}

#if LUMINANCE
[numthreads(THREAD_GROUP_COUNT_X, THREAD_GROUP_COUNT_Y, 1)]
void main_cs(uint3 thread_id : SV_DispatchThreadID)
{
    float2 resolution_out;
    tex_uav.GetDimensions(resolution_out.x, resolution_out.y);
    if (any(int2(thread_id.xy) >= resolution_out))
        return;
    
    float2 resolution_in;
    tex.GetDimensions(resolution_in.x, resolution_in.y);
    
    float2 uv         = (thread_id.xy + 0.5f) / resolution_out;
    float2 texel_size = 1.0f / resolution_in;

    float3 color    = downsample_stable(tex, uv, texel_size);
    float3 filtered = threshold(color);
    
    tex_uav[thread_id.xy] = float4(filtered, 1.0f);
}
#endif

#if DOWNSAMPLE
[numthreads(THREAD_GROUP_COUNT_X, THREAD_GROUP_COUNT_Y, 1)]
void main_cs(uint3 thread_id : SV_DispatchThreadID)
{
    float2 resolution_out;
    tex_uav.GetDimensions(resolution_out.x, resolution_out.y);
    if (any(int2(thread_id.xy) >= resolution_out))
        return;

    float2 resolution_in;
    tex.GetDimensions(resolution_in.x, resolution_in.y);
    
    float2 uv         = (thread_id.xy + 0.5f) / resolution_out;
    float2 texel_size = 1.0f / resolution_in;

    float3 color = downsample_stable(tex, uv, texel_size);
    
    tex_uav[thread_id.xy] = float4(color, 1.0f);
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
    
    float2 uv         = (thread_id.xy + 0.5f) / resolution_out;
    float2 texel_size = 1.0f / resolution_out;
    
    // 1. upsample the lower (blurrier) mip
    float3 low_mip = upsample_filter(tex, uv, texel_size);
    
    // 2. get the current (sharper) mip
    float3 high_mip = tex_uav[thread_id.xy].rgb;
    
    // 3. blend with spread factor
    const float SPREAD_FACTOR = 1.0f;
    float3 result = high_mip * SPREAD_FACTOR + low_mip;
    
    tex_uav[thread_id.xy] = float4(result, 1.0f);
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
    
    float4 color_frame               = tex[thread_id.xy];
    float4 color_bloom               = tex2[thread_id.xy];
    const float INTENSITY_CORRECTION = 0.0005f;
    float bloom_intensity            = pass_get_f3_value().x;
    float3 result                    = color_frame.rgb + (color_bloom.rgb * INTENSITY_CORRECTION * bloom_intensity);
    
    tex_uav[thread_id.xy] = float4(result, color_frame.a);
}
#endif
