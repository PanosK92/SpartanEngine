/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

//= INCLUDES =========
#include "common.hlsl"
//====================

// A 4x4 box filter
float4 Box_Filter(float2 uv, Texture2D tex, float2 texel_size)
{
    float4 offset = texel_size.xyxy * float4(-1.0f, -1.0f, 1.0f, 1.0f);
    
    float4 samples =
    tex.SampleLevel(sampler_bilinear_clamp, uv + offset.xy, 0) +
    tex.SampleLevel(sampler_bilinear_clamp, uv + offset.zy, 0) +
    tex.SampleLevel(sampler_bilinear_clamp, uv + offset.xw, 0) +
    tex.SampleLevel(sampler_bilinear_clamp, uv + offset.zw, 0);

    return samples / 4.0f;
}
