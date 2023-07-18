// This file is part of the FidelityFX SDK.
//
// Copyright (C) 2023 Advanced Micro Devices, Inc.
// 
// Permission is hereby granted, free of charge, to any person obtaining a copy 
// of this software and associated documentation files(the “Software”), to deal 
// in the Software without restriction, including without limitation the rights 
// to use, copy, modify, merge, publish, distribute, sublicense, and /or sell 
// copies of the Software, and to permit persons to whom the Software is 
// furnished to do so, subject to the following conditions :
// 
// The above copyright notice and this permission notice shall be included in 
// all copies or substantial portions of the Software.
// 
// THE SOFTWARE IS PROVIDED “AS IS”, WITHOUT WARRANTY OF ANY KIND, EXPRESS OR 
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, 
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.IN NO EVENT SHALL THE 
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER 
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, 
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN 
// THE SOFTWARE.

#include "ffx_lpm_resources.h"

#if defined(FFX_GPU)
#include "ffx_core.h"
#endif // #if defined(FFX_GPU)

#if defined(FFX_GPU)
#ifndef FFX_PREFER_WAVE64
#define FFX_PREFER_WAVE64
#endif // #if defined(FFX_GPU)

#if defined(LPM_BIND_CB_LPM)
    layout (set = 0, binding = LPM_BIND_CB_LPM, std140) uniform cbLPM_t
    {
        FfxUInt32x4   ctl[24];
        FfxBoolean    shoulder;
        FfxBoolean    con;
        FfxBoolean    soft;
        FfxBoolean    con2;
        FfxBoolean    clip;
        FfxBoolean    scaleOnly;
        FfxUInt32x2   pad;
    } cbLPM;
#else
    #define ctl       0
    #define shoulder  0
    #define con       0
    #define soft      0
    #define con2      0
    #define clip      0
    #define scaleOnly 0
    #define pad       0
#endif

FfxUInt32x4 LpmFilterCtl(FfxUInt32 i)
{
    return cbLPM.ctl[i];
}

FfxBoolean GetShoulder()
{
    return cbLPM.shoulder;
}

FfxBoolean GetCon()
{
    return cbLPM.con;
}

FfxBoolean GetSoft()
{
    return cbLPM.soft;
}

FfxBoolean GetCon2()
{
    return cbLPM.con2;
}

FfxBoolean GetClip()
{
    return cbLPM.clip;
}

FfxBoolean GetScaleOnly()
{
    return cbLPM.scaleOnly;
}

layout (set = 0, binding = 1000) uniform sampler s_LinearClamp;

#if FFX_HALF
#define ColorFormat FfxFloat16x4
#define OutputFormat rgba16f
#else
#define ColorFormat FfxFloat32x4
#define OutputFormat rgba32f
#endif

// SRVs
#if defined LPM_BIND_SRV_INPUT_COLOR
    layout (set = 0, binding = LPM_BIND_SRV_INPUT_COLOR)     uniform texture2D  r_input_color;
#endif

// UAV declarations
#if defined LPM_BIND_UAV_OUTPUT_COLOR
    layout (set = 0, binding = LPM_BIND_UAV_OUTPUT_COLOR, OutputFormat)    uniform image2D  rw_output_color;
#endif

ColorFormat LoadInput(FfxUInt32x2 iPxPos)
{
#if defined(LPM_BIND_SRV_INPUT_COLOR)
    return ColorFormat(texelFetch(r_input_color, FfxInt32x2(iPxPos), 0));
#endif  // defined(LPM_BIND_SRV_INPUT_COLOR)
    return ColorFormat(0.f);
}

void StoreOutput(FfxUInt32x2 iPxPos, ColorFormat fColor)
{
#if defined(LPM_BIND_UAV_OUTPUT_COLOR)
    imageStore(rw_output_color, FfxInt32x2(iPxPos), fColor);
#endif  // defined(LPM_BIND_UAV_OUTPUT_COLOR)
}
#endif // #if defined(FFX_GPU)
