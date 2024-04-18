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

[numthreads(THREAD_GROUP_COUNT_X, THREAD_GROUP_COUNT_Y, 1)]
void main_cs(uint3 thread_id : SV_DispatchThreadID)
{
    float2 resolution_out;
    tex_uav.GetDimensions(resolution_out.x, resolution_out.y);
    if (any(int2(thread_id.xy) >= resolution_out))
        return;

    const float2 uv = (thread_id.xy + 0.5f) / resolution_out;

    // sample the texture at current pixel and neighboring pixels
    float3 s1 = tex[thread_id.xy].rgb;               // current pixel
    float3 s2 = tex[thread_id.xy + int2(1, 0)].rgb;  // right
    float3 s3 = tex[thread_id.xy + int2(-1, 0)].rgb; // left
    float3 s4 = tex[thread_id.xy + int2(0, 1)].rgb;  // up
    float3 s5 = tex[thread_id.xy + int2(0, -1)].rgb; // down

    // calculate weights based on luminance
    float s1w          = 1 / (luminance(s1) + 1);
    float s2w          = 1 / (luminance(s2) + 1);
    float s3w          = 1 / (luminance(s3) + 1);
    float s4w          = 1 / (luminance(s4) + 1);
    float s5w          = 1 / (luminance(s5) + 1);
    float one_div_wsum = 1.0 / (s1w + s2w + s3w + s4w + s5w);

    // compute weighted average
    float3 color = (s1 * s1w + s2 * s2w + s3 * s3w + s4 * s4w + s5 * s5w) * one_div_wsum;

    // write the result to the output texture
    tex_uav[thread_id.xy] = float4(saturate(color), tex_uav[thread_id.xy].a);
}
