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
