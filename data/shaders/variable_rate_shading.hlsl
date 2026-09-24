/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

//= INCLUDES =========
#include "common.hlsl"
//====================

// vulkan fragment shading rate encoding:
// bits 0-1: log2(width)  - 0=1x, 1=2x, 2=4x
// bits 2-3: log2(height) - 0=1x, 1=2x, 2=4x
// common values: 0=1x1, 5=2x2, 10=4x4
static const uint VRS_1X1 = 0;  // full rate
static const uint VRS_2X2 = 5;  // quarter rate
static const uint VRS_4X4 = 10; // sixteenth rate

[numthreads(THREAD_GROUP_COUNT_X, THREAD_GROUP_COUNT_Y, 1)]
void main_cs(uint3 thread_id : SV_DispatchThreadID)
{
    uint2 resolution_out;
    tex_uav_uint.GetDimensions(resolution_out.x, resolution_out.y);
    if (any(thread_id.xy >= resolution_out))
        return;

    // sample from previous frame output to determine shading rate
    float2 uv        = (thread_id.xy + 0.5f) / float2(resolution_out);
    float3 color     = tex.SampleLevel(GET_SAMPLER(sampler_point_clamp_border), uv, 0.0f).rgb;
    float luminance_ = luminance(color);

    // determine shading rate based on luminance
    // bright areas get full rate, dark areas can use reduced rate
    uint shading_rate = VRS_1X1;
    if (luminance_ < 0.01f)
    {
        shading_rate = VRS_4X4; // very dark: use lowest rate
    }
    else if (luminance_ < 0.1f)
    {
        shading_rate = VRS_2X2; // dark: use quarter rate
    }

    tex_uav_uint[thread_id.xy] = shading_rate;
}
