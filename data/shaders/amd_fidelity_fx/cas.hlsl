/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

//= INCLUDES ============
#include "../common.hlsl"
//=======================

#define A_GPU
#define A_HLSL

#include "ffx_a.h"

// functions ffx_cas.h wants defined
float3 CasLoad(float2 pos)
{
    return tex[pos].rgb;
}

// lets you transform input from the load into a linear color space between 0 and 1.
void CasInput(inout float r, inout float g, inout float b)
{

}

#include "ffx_cas.h"

[numthreads(THREAD_GROUP_COUNT_X, THREAD_GROUP_COUNT_Y, 1)]
void main_cs(uint3 thread_id : SV_DispatchThreadID)
{
    float2 resolution_out;
    tex_uav.GetDimensions(resolution_out.x, resolution_out.y);
    if (any(int2(thread_id.xy) >= resolution_out))
        return;

    float4 const0;
    float4 const1;
    float sharpness = pass_get_f3_value().x;
    CasSetup(const0, const1, sharpness, resolution_out.x, resolution_out.y, resolution_out.x, resolution_out.y);

    float3 color = 0.0f;
    CasFilter(color.r, color.g, color.b, thread_id.xy, const0, const1, true);

    const float a = tex[thread_id.xy].a;
    tex_uav[thread_id.xy] = float4(color, a);
}
