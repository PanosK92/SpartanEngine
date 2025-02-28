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

//= INCLUDES ==================
#include "../common.hlsl"
#define FXAA_PC 1
#define FXAA_HLSL_5 1
#define FXAA_QUALITY__PRESET 39
#define FXAA_GREEN_AS_LUMA 1
#include "Fxaa3_11.h"
//=============================

static const float g_fxaa_subPix           = 0.75f;   // the amount of sub-pixel aliasing removal. This can effect sharpness.
static const float g_fxaa_edgeThreshold    = 0.12f;   // the minimum amount of local contrast required to apply algorithm.
static const float g_fxaa_edgeThresholdMin = 0.0625f; // trims the algorithm from processing darks

[numthreads(THREAD_GROUP_COUNT_X, THREAD_GROUP_COUNT_Y, 1)]
void main_cs(uint3 thread_id : SV_DispatchThreadID)
{
    float2 resolution_out;
    tex_uav.GetDimensions(resolution_out.x, resolution_out.y);
    if (any(int2(thread_id.xy) >= resolution_out))
        return;

    const float2 pos = (thread_id.xy + 0.5f) / resolution_out;
    FxaaTex fxaa_tex = { samplers[sampler_bilinear_clamp], tex };
    float2 texl_size = 1.0f / resolution_out;

    float3 color = FxaaPixelShader
    (
        pos, 0, fxaa_tex, fxaa_tex, fxaa_tex,
        texl_size, 0, 0, 0,
        g_fxaa_subPix,
        g_fxaa_edgeThreshold,
        g_fxaa_edgeThresholdMin,
        0, 0, 0, 0
    ).rgb;

    tex_uav[thread_id.xy] = float4(color, tex[thread_id.xy].a);
}
