/*
Copyright(c) 2016-2020 Panos Karabelas

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

//= INCLUDES ==============
#include "Common.hlsl"
#include "LumaSharpen.hlsl"
//=========================

// 0 - Simple
// 1 - LumaSharpen
// 2 - FidelityFX Contrast Adaptive Sharpening
#define SHARPENING_METHOD 2

#if SHARPENING_METHOD == 0

float3 Sharpen(uint2 thread_id)
{
    const uint2 dx = uint2(1, 0);
    const uint2 dy = uint2(0, 1);

    float4 up       = tex[thread_id - dy];
    float4 down     = tex[thread_id + dy];
    float4 center   = tex[thread_id];
    float4 right    = tex[thread_id + dx];
    float4 left     = tex[thread_id - dx];
    
    return saturate(center + (4 * center - up - down - left - right) * g_sharpen_strength);
}

#elif SHARPENING_METHOD == 1

float3 Sharpen(uint2 thread_id)
{
    const float2 uv             = (thread_id.xy + 0.5f) / g_resolution;
    const float sharpen_clamp   = 0.035f; // Limits maximum amount of sharpening a pixel receives - Default is 0.035;
    return LumaSharpen(uv, tex, g_resolution, g_sharpen_strength, sharpen_clamp);
}

#elif SHARPENING_METHOD == 2

#define A_GPU 1
#define A_HLSL 1
#include "ffx_a.h"
float3 CasLoad(float2 pos) {return tex[pos].rgb;}
// Lets you transform input from the load into a linear color space between 0 and 1. See ffx_cas.h. In this case, our input is already linear and between 0 and 1.
void CasInput(inout float r, inout float g, inout float b){}
#include "ffx_cas.h"
float3 Sharpen(uint2 thread_id)
{
    float4 const0;
    float4 const1;
    CasSetup(const0, const1, g_sharpen_strength, g_resolution.x, g_resolution.y, g_resolution.x, g_resolution.y);
    float3 color = 0.0f;
    CasFilter(color.r, color.g, color.b, thread_id, const0, const1, true);
    return color;
}

#endif

[numthreads(thread_group_count_x, thread_group_count_y, 1)]
void mainCS(uint3 thread_id : SV_DispatchThreadID)
{
    if (thread_id.x >= uint(g_resolution.x) || thread_id.y >= uint(g_resolution.y))
        return;
    
    const float3 color  = Sharpen(thread_id.xy);
    const float a       = tex[thread_id.xy].a;
    
    tex_out_rgba[thread_id.xy] = float4(color, a);
}
