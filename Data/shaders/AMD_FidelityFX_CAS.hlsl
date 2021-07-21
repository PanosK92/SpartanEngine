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

//= INCLUDES =========
#include "Common.hlsl"
//====================

#define A_GPU
#define A_HLSL

#include "ffx_a.h"

// Functions ffx_cas.h wants defined
float3 CasLoad(float2 pos)
{
    return tex[pos].rgb;
}

// Lets you transform input from the load into a linear color space between 0 and 1.
void CasInput(inout float r, inout float g, inout float b)
{
}

#include "ffx_cas.h"

[numthreads(thread_group_count_x, thread_group_count_y, 1)]
void mainCS(uint3 thread_id : SV_DispatchThreadID)
{
    if (thread_id.x >= uint(g_resolution_rt.x) || thread_id.y >= uint(g_resolution_rt.y))
        return;

    float4 const0;
    float4 const1;
    CasSetup(const0, const1, g_sharpen_strength, g_resolution_rt.x, g_resolution_rt.y, g_resolution_rt.x, g_resolution_rt.y);
    
    float3 color = 0.0f;
    CasFilter(color.r, color.g, color.b, thread_id.xy, const0, const1, true);

    const float a = tex[thread_id.xy].a;

    tex_out_rgba[thread_id.xy] = float4(color, a);
}
