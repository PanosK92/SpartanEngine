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

#if LUMINANCE

[numthreads(THREAD_GROUP_COUNT_X, THREAD_GROUP_COUNT_Y, 1)]
void main_cs(uint3 thread_id : SV_DispatchThreadID)
{
    float2 resolution_out;
    tex_uav.GetDimensions(resolution_out.x, resolution_out.y);
    if (any(int2(thread_id.xy) >= resolution_out))
        return;

    float3 color = tex[thread_id.xy].rgb;
    tex_uav[thread_id.xy] = float4(saturate_16(luminance(color) * color), tex_uav[thread_id.xy].a);
}

#endif

#if UPSAMPLE_BLEND_MIP

// warning: passing "tex" as a parameter will cause spirv-cross to reflect nothing for this shader.
float3 tent_antiflicker_filter(float2 uv, float2 texel_size)
{
    // tent
    float4 d  = texel_size.xyxy * float4(-1.0f, -1.0f, 1.0f, 1.0f) * 2.0f;
    float3 s1 = tex.SampleLevel(samplers[sampler_bilinear_clamp], uv + d.xy, 0.0f).rgb;
    float3 s2 = tex.SampleLevel(samplers[sampler_bilinear_clamp], uv + d.zy, 0.0f).rgb;
    float3 s3 = tex.SampleLevel(samplers[sampler_bilinear_clamp], uv + d.xw, 0.0f).rgb;
    float3 s4 = tex.SampleLevel(samplers[sampler_bilinear_clamp], uv + d.zw, 0.0f).rgb;
    
    // kuma weighted average
    float s1w = 1.0f / (luminance(s1) + 1.0f);
    float s2w = 1.0f / (luminance(s2) + 1.0f);
    float s3w = 1.0f / (luminance(s3) + 1.0f);
    float s4w = 1.0f / (luminance(s4) + 1.0f);
    float one_div_wsum = 1.0 / (s1w + s2w + s3w + s4w);
    
    return (s1 * s1w + s2 * s2w + s3 * s3w + s4 * s4w) * one_div_wsum;
}

[numthreads(THREAD_GROUP_COUNT_X, THREAD_GROUP_COUNT_Y, 1)]
void main_cs(uint3 thread_id : SV_DispatchThreadID)
{
    float2 resolution_out;
    tex_uav.GetDimensions(resolution_out.x, resolution_out.y);
    if (any(int2(thread_id.xy) >= resolution_out))
        return;

    const float2 texel_size  = 1.0f / resolution_out;
    const float2 uv          = (thread_id.xy + 0.5f) / resolution_out;
    float4 destination_color = tex_uav[thread_id.xy];
    float3 upsampled_color   = tent_antiflicker_filter(uv, texel_size * 0.5f);
    tex_uav[thread_id.xy]    = float4(saturate_16(destination_color.rgb + upsampled_color), destination_color.a);
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

    float4 color_frame    = tex[thread_id.xy];
    float4 color_mip      = tex2[thread_id.xy];
    float bloom_intensity = pass_get_f3_value().x;
    tex_uav[thread_id.xy] = saturate_16(color_frame + color_mip * bloom_intensity);
}

#endif
