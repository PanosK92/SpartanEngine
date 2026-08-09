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

//= INCLUDES ============
#include "../common.hlsl"
//=======================

#define A_GPU
#define A_HLSL
#define SPD_NO_WAVE_OPERATIONS
#define SPD_LINEAR_SAMPLER

#include "ffx_a.h"

groupshared AF4 spd_intermediate[16][16];
groupshared AU1 spd_counter;

AF4 SpdLoadSourceImage(ASU2 p, AU1 slice)
{
    float2 resolution_out = pass_get_f3_value2().xy;
    float2 uv             = (p + 0.5f) / resolution_out;
    return tex.SampleLevel(samplers[sampler_bilinear_clamp], uv, 0);
}

// Load from mip 5
AF4 SpdLoad(ASU2 pos, AU1 slice)
{
    return tex_uav_mips[5][pos];
}

void SpdStore(ASU2 pos, AF4 value, AU1 index, AU1 slice)
{
    tex_uav_mips[index][pos] = value;
}

AF4 SpdLoadIntermediate(AU1 x, AU1 y)
{
    return spd_intermediate[x][y];
}

void SpdStoreIntermediate(AU1 x, AU1 y, AF4 value)
{
    spd_intermediate[x][y] = value;
}

AF4 SpdReduce4(AF4 s1, AF4 s2, AF4 s3, AF4 s4)
{
    AF4 output;
    #if AVERAGE
        output = (s1 + s2 + s3 + s4) * 0.25f;
    #elif MIN
        output = min(min(s1, s2), min(s3, s4));
    #elif MAX
        output = max(max(s1, s2), max(s3, s4));
    #endif
    
    return output;
}

void SpdIncreaseAtomicCounter(AU1 slice)
{
    InterlockedAdd(g_atomic_counter[0], 1, spd_counter);
}

AU1 SpdGetAtomicCounter()
{
    return spd_counter;
}

void SpdResetAtomicCounter(AU1 slice)
{
    g_atomic_counter[0] = 0;
}

#if ONE_MIP
// 2x2 reduce for a single mip when the source exceeds the spd 4096 limit
[numthreads(THREAD_GROUP_COUNT_X, THREAD_GROUP_COUNT_Y, 1)]
void main_cs(uint3 thread_id : SV_DispatchThreadID)
{
    uint2 dst_res;
    tex_uav.GetDimensions(dst_res.x, dst_res.y);
    if (any(thread_id.xy >= dst_res))
    {
        return;
    }

    uint2 src_res;
    tex.GetDimensions(src_res.x, src_res.y);

    int2 s   = int2(thread_id.xy) * 2;
    int2 s00 = s;
    int2 s10 = int2(min(s.x + 1, int(src_res.x) - 1), s.y);
    int2 s01 = int2(s.x, min(s.y + 1, int(src_res.y) - 1));
    int2 s11 = int2(min(s.x + 1, int(src_res.x) - 1), min(s.y + 1, int(src_res.y) - 1));

    AF4 v00 = tex.Load(int3(s00, 0));
    AF4 v10 = tex.Load(int3(s10, 0));
    AF4 v01 = tex.Load(int3(s01, 0));
    AF4 v11 = tex.Load(int3(s11, 0));

    tex_uav[thread_id.xy] = SpdReduce4(v00, v10, v01, v11);
}
#else
#include "ffx_spd.h"

[numthreads(256, 1, 1)]
void main_cs(uint3 work_group_id : SV_GroupID, uint local_thread_index : SV_GroupIndex)
{
    const float3 f3_value  = pass_get_f3_value();
    float mip_count        = f3_value.x;
    float work_group_count = f3_value.y;
    SpdDownsample(work_group_id.xy, local_thread_index, mip_count, work_group_count, work_group_id.z);
}
#endif
