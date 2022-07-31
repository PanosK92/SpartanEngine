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

//= INCLUDES ==========
#include "Common.hlsl"
//=====================

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
