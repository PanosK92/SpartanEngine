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

//= INCLUDES =========
#include "common.hlsl"
//====================

[numthreads(THREAD_GROUP_COUNT_X, THREAD_GROUP_COUNT_Y, 1)]
void main_cs(uint3 thread_id : SV_DispatchThreadID)
{
    uint2 size_out;
    tex_uav.GetDimensions(size_out.x, size_out.y);
    if (any(thread_id.xy >= size_out))
        return;

    uint2 size_in;
    tex.GetDimensions(size_in.x, size_in.y);

    // a same size blit stays an exact texel copy, depth to float relies on it
    if (all(size_in == size_out))
    {
        tex_uav[thread_id.xy] = tex.Load(int3(thread_id.xy, 0));
        return;
    }

    // differing sizes are a rescale, loading by texel would crop to the top left
    const float2 uv    = (thread_id.xy + 0.5f) / (float2)size_out;
    const float2 ratio = (float2)size_in / (float2)size_out;

    // box filter the destination texel footprint, a single bilinear tap aliases hard
    // once the source is several times larger
    const int2 taps = clamp((int2)ceil(ratio), int2(1, 1), int2(4, 4));
    float4 sum      = 0.0f;
    [loop] for (int y = 0; y < taps.y; y++)
    {
        [loop] for (int x = 0; x < taps.x; x++)
        {
            const float2 offset = (float2(x, y) + 0.5f) / (float2)taps - 0.5f;
            sum += tex.SampleLevel(GET_SAMPLER(sampler_bilinear_clamp), uv + offset / (float2)size_out, 0);
        }
    }

    tex_uav[thread_id.xy] = sum / (float)(taps.x * taps.y);
}
