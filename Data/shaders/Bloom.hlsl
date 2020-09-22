/*
Copyright(c) 2016-2020 Panos Karabelas

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
#include "Scaling.hlsl"
//=====================

#if DOWNSAMPLE
[numthreads(thread_group_count_x, thread_group_count_y, 1)]
void mainCS(uint3 thread_id : SV_DispatchThreadID)
{
    if (thread_id.x >= uint(g_resolution.x) || thread_id.y >= uint(g_resolution.y))
        return;

    // g_texel_size refers to the current render target, which is half the size of the input texture, so we multiply by 2.0
    float2 texel_size = g_texel_size * 2.0f;
    const float2 uv = (thread_id.xy + 0.5f) / g_resolution;

    tex_out_rgba[thread_id.xy] = Box_Filter_AntiFlicker(uv, tex, texel_size);
}
#endif

#if DOWNSAMPLE_LUMINANCE
[numthreads(thread_group_count_x, thread_group_count_y, 1)]
void mainCS(uint3 thread_id : SV_DispatchThreadID)
{
    if (thread_id.x >= uint(g_resolution.x) || thread_id.y >= uint(g_resolution.y))
        return;

    // g_texel_size refers to the current render target, which is half the size of the input texture, so we multiply by 2.0
    float2 texel_size = g_texel_size * 2.0f;

    const float2 uv = (thread_id.xy + 0.5f) / g_resolution;
    float4 color = Box_Filter_AntiFlicker(uv, tex, texel_size);
    tex_out_rgba[thread_id.xy] = saturate_16(luminance(color) * color);
}
#endif

#if UPSAMPLE_BLEND_MIP
[numthreads(thread_group_count_x, thread_group_count_y, 1)]
void mainCS(uint3 thread_id : SV_DispatchThreadID)
{
    if (thread_id.x >= uint(g_resolution.x) || thread_id.y >= uint(g_resolution.y))
        return;

    // g_texel_size refers to the current render target, which is twice the size of the input texture.
    // so instead of multiplying it with 0.5, we will use it as is in order to get a "tent" filter, which helps reduce "blockiness".
    float2 texel_size = g_texel_size;

    const float2 uv             = (thread_id.xy + 0.5f) / g_resolution;
    float4 upsampled_color      = Box_Filter(uv, tex, texel_size);
    tex_out_rgba[thread_id.xy]  = saturate_16(tex_out_rgba[thread_id.xy] + upsampled_color);
}
#endif

#if UPSAMPLE_BLEND_FRAME
[numthreads(thread_group_count_x, thread_group_count_y, 1)]
void mainCS(uint3 thread_id : SV_DispatchThreadID)
{
    if (thread_id.x >= uint(g_resolution.x) || thread_id.y >= uint(g_resolution.y))
        return;

    // g_texel_size refers to the current render target, which is twice the size of the input texture.
    // so instead of multiplying it with 0.5, we will use it as is in order to get a "tent" filter, which helps reduce "blockiness".
    float2 texel_size = g_texel_size;

    const float2 uv     = (thread_id.xy + 0.5f) / g_resolution;
    float4 sourceColor2 = Box_Filter(uv, tex2, texel_size);
    float4 sourceColor  = tex[thread_id.xy];
    tex_out_rgba[thread_id.xy] = saturate_16(sourceColor + sourceColor2 * g_bloom_intensity);
}
#endif
