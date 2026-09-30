/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

//= INCLUDES ==================
#include "../common.hlsl"
#define FXAA_PC 1
#define FXAA_HLSL_5 1
#define FXAA_QUALITY__PRESET 39
#define FXAA_GREEN_AS_LUMA 1
#include "fxaa3_11.h"
//=============================

static const float g_fxaa_subPix           = 0.9f;    // the amount of sub-pixel aliasing removal. This can effect sharpness.
static const float g_fxaa_edgeThreshold    = 0.063f;  // the minimum amount of local contrast required to apply algorithm.
static const float g_fxaa_edgeThresholdMin = 0.0312f; // trims the algorithm from processing darks

[numthreads(THREAD_GROUP_COUNT_X, THREAD_GROUP_COUNT_Y, 1)]
void main_cs(uint3 thread_id : SV_DispatchThreadID)
{
    float2 resolution_out;
    tex_uav.GetDimensions(resolution_out.x, resolution_out.y);
    if (any(int2(thread_id.xy) >= resolution_out))
        return;

    // tex is the render resolution frame and tex_uav the output, fxaa runs on render texels inside the
    // scaled subrect and the output pixel picks its spot in it, so this pass is also the linear upscale
    float2 resolution_in;
    tex.GetDimensions(resolution_in.x, resolution_in.y);
    const float2 texl_size = 1.0f / resolution_in;
    const float2 uv_scale  = get_render_uv_scale();
    const float2 pos       = min((thread_id.xy + 0.5f) / resolution_out * uv_scale, uv_scale - texl_size * 0.5f);
    FxaaTex fxaa_tex       = { samplers[sampler_bilinear_clamp], tex };

    float3 color = FxaaPixelShader
    (
        pos, 0, fxaa_tex, fxaa_tex, fxaa_tex,
        texl_size, 0, 0, 0,
        g_fxaa_subPix,
        g_fxaa_edgeThreshold,
        g_fxaa_edgeThresholdMin,
        0, 0, 0, 0
    ).rgb;

    tex_uav[thread_id.xy] = float4(color, tex.SampleLevel(samplers[sampler_point_clamp], pos, 0).a);
}
