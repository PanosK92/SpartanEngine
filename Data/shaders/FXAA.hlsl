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

//= INCLUDES ==================
#include "Common.hlsl"
#if FXAA
#define FXAA_PC 1
#define FXAA_HLSL_5 1
#define FXAA_QUALITY__PRESET 39
#include "Fxaa3_11.h"
#endif
//=============================

static const float g_fxaa_subPix            = 0.75f;    // The amount of sub-pixel aliasing removal. This can effect sharpness.
static const float g_fxaa_edgeThreshold     = 0.166f;   // The minimum amount of local contrast required to apply algorithm.
static const float g_fxaa_edgeThresholdMin  = 0.0833f;  // Trims the algorithm from processing darks

[numthreads(thread_group_count_x, thread_group_count_y, 1)]
void mainCS(uint3 thread_id : SV_DispatchThreadID)
{
    const float2 uv = (thread_id.xy + 0.5f) / g_resolution;
    float4 color    = 0.0f;
    
     // Encode luminance into alpha channel which is optimal for FXAA
#if LUMINANCE
    color.rgb   = tex[thread_id.xy].rgb;
    color.a     = luminance(color.rgb);  
#endif

     // Actual FXAA
#if FXAA
    FxaaTex fxaa_tex = { sampler_bilinear_clamp, tex };
    float2 fxaaQualityRcpFrame = g_texel_size;

    color.rgb = FxaaPixelShader
    (
        uv, 0, fxaa_tex, fxaa_tex, fxaa_tex,
        fxaaQualityRcpFrame, 0, 0, 0,
        g_fxaa_subPix,
        g_fxaa_edgeThreshold,
        g_fxaa_edgeThresholdMin,
        0, 0, 0, 0
    ).rgb;
    color.a = 1.0f; 
#endif
    
    tex_out_rgba[thread_id.xy] = color;
}
