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

// groupshared radix-2 inverse fft, one thread group transforms one row (HORIZONTAL) or column (VERTICAL)
// two complex fields are packed per texture, so each pass transforms four complex channels at once

#include "ocean_common.hlsl"

groupshared float4 sh_a[512];
groupshared float4 sh_b[512];

// multiply a packed pair of complex numbers by the complex twiddle factor
float4 ocean_twiddle_mul(float2 w, float4 v)
{
    return float4(ocean_cmul(v.xy, w), ocean_cmul(v.zw, w));
}

[numthreads(512, 1, 1)]
void main_cs(uint3 group_id : SV_GroupID, uint3 group_thread_id : SV_GroupThreadID)
{
    uint i          = group_thread_id.x;
    uint line_index = group_id.y;
    uint cascade    = group_id.z;

    // load with bit reversal so the natural-order butterflies below produce a natural-order result
    uint rev = reversebits(i) >> (32 - OCEAN_LOG2N);
#if defined(HORIZONTAL)
    uint3 coord_load = uint3(rev, line_index, cascade);
#else
    uint3 coord_load = uint3(line_index, rev, cascade);
#endif
    sh_a[i] = tex_ocean_fft_a_uav[coord_load];
    sh_b[i] = tex_ocean_fft_b_uav[coord_load];
    GroupMemoryBarrierWithGroupSync();

    [loop] for (uint s = 1; s <= OCEAN_LOG2N; ++s)
    {
        uint m      = 1u << s;
        uint half_m = m >> 1;
        uint k      = i & (half_m - 1);
        bool low    = (i & half_m) == 0;
        uint a_idx  = low ? i : i - half_m;
        uint b_idx  = low ? i + half_m : i;

        float angle = 2.0 * OCEAN_PI * (float)k / (float)m; // positive sign for the inverse transform
        float2 w    = float2(cos(angle), sin(angle));

        float4 ua = sh_a[a_idx];
        float4 ta = ocean_twiddle_mul(w, sh_a[b_idx]);
        float4 ub = sh_b[a_idx];
        float4 tb = ocean_twiddle_mul(w, sh_b[b_idx]);

        float4 ra = low ? ua + ta : ua - ta;
        float4 rb = low ? ub + tb : ub - tb;

        GroupMemoryBarrierWithGroupSync();
        sh_a[i] = ra;
        sh_b[i] = rb;
        GroupMemoryBarrierWithGroupSync();
    }

    // no 1/N^2 factor, tessendorf's height field is the plain sum over modes, the physical scale lives in the spectrum's dk term
#if defined(HORIZONTAL)
    uint3 coord_store = uint3(i, line_index, cascade);
#else
    uint3 coord_store = uint3(line_index, i, cascade);
#endif
    tex_ocean_fft_a_uav[coord_store] = sh_a[i];
    tex_ocean_fft_b_uav[coord_store] = sh_b[i];
}
