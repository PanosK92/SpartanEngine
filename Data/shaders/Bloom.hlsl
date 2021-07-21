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
#include "Scaling.hlsl"
//=====================

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

    // g_texel_size refers to the current render target, which is twice the size of the input texture.
    // so instead of multiplying it with 0.5, we will use it as is in order to get a "tent" filter, which helps reduce "blockiness".
    float2 texel_size = g_texel_size;

    const float2 uv             = (thread_id.xy + 0.5f) / g_resolution_rt;
    float4 upsampled_color      = Box_Filter(uv, tex, texel_size);
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
