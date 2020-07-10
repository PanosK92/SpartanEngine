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

float4 SharpenTaa(float2 uv, Texture2D source_texture)
{
    float intensity = 0.2f;

    float2 dx = float2(g_texel_size.x, 0.0f);
    float2 dy = float2(0.0f, g_texel_size.y);

    float4 up       = source_texture.Sample(sampler_bilinear_clamp, uv - dy);
    float4 down     = source_texture.Sample(sampler_bilinear_clamp, uv + dy);
    float4 center   = source_texture.Sample(sampler_bilinear_clamp, uv);
    float4 right    = source_texture.Sample(sampler_bilinear_clamp, uv + dx);
    float4 left     = source_texture.Sample(sampler_bilinear_clamp, uv - dx);
    
    return saturate(center + (4 * center - up - down - left - right) * intensity);
}

float4 mainPS(Pixel_PosUv input) : SV_TARGET
{
    return LumaSharpen(input.uv, tex, g_resolution, g_sharpen_strength, g_sharpen_clamp);
}
