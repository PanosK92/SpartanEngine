/*
Copyright(c) 2016-2021 Panos Karabelas

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

//= INCLUDES ==========
#include "Common.hlsl"
//=====================

float4 tent_antiflicker_filter(float2 uv, Texture2D tex, float2 texel_size)
{
    // Tent
    float4 d  = texel_size.xyxy * float4(-1.0f, -1.0f, 1.0f, 1.0f) * 2.0f;
    float4 s1 = tex.SampleLevel(sampler_bilinear_clamp, uv + d.xy, 0.0f);
    float4 s2 = tex.SampleLevel(sampler_bilinear_clamp, uv + d.zy, 0.0f);
    float4 s3 = tex.SampleLevel(sampler_bilinear_clamp, uv + d.xw, 0.0f);
    float4 s4 = tex.SampleLevel(sampler_bilinear_clamp, uv + d.zw, 0.0f);

    // Luma weighted average
    float s1w = 1 / (luminance(s1) + 1);
    float s2w = 1 / (luminance(s2) + 1);
    float s3w = 1 / (luminance(s3) + 1);
    float s4w = 1 / (luminance(s4) + 1);
    float one_div_wsum = 1.0 / (s1w + s2w + s3w + s4w);

    return (s1 * s1w + s2 * s2w + s3 * s3w + s4 * s4w) * one_div_wsum;
}

#if LUMINANCE
[numthreads(thread_group_count_x, thread_group_count_y, 1)]
void mainCS(uint3 thread_id : SV_DispatchThreadID)
{
    if (thread_id.x >= uint(g_resolution_rt.x) || thread_id.y >= uint(g_resolution_rt.y))
        return;

    float4 color = tex[thread_id.xy];
    tex_out_rgba[thread_id.xy] = saturate_16(luminance(color) * color);
}
#endif

#if UPSAMPLE_BLEND_MIP
[numthreads(thread_group_count_x, thread_group_count_y, 1)]
void mainCS(uint3 thread_id : SV_DispatchThreadID)
{
    if (thread_id.x >= uint(g_resolution_rt.x) || thread_id.y >= uint(g_resolution_rt.y))
        return;

    const float2 uv             = (thread_id.xy + 0.5f) / g_resolution_rt;
    float4 upsampled_color      = tent_antiflicker_filter(uv, tex, g_texel_size * 0.5f);
    tex_out_rgba[thread_id.xy]  = saturate_16(tex_out_rgba[thread_id.xy] + upsampled_color);
}
#endif

#if BLEND_FRAME
[numthreads(thread_group_count_x, thread_group_count_y, 1)]
void mainCS(uint3 thread_id : SV_DispatchThreadID)
{
    if (thread_id.x >= uint(g_resolution_rt.x) || thread_id.y >= uint(g_resolution_rt.y))
        return;

    float4 color_frame  = tex[thread_id.xy];
    float4 color_mip    = tex2[thread_id.xy];
    tex_out_rgba[thread_id.xy] = saturate_16(color_frame + color_mip * g_bloom_intensity);
}
#endif

