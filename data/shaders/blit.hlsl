/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
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
