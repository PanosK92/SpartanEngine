/*
Copyright(c) 2016-2023 Panos Karabelas

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
#include "common.hlsl"
//====================

struct PS_INPUT
{
    float4 position : SV_POSITION;
    float4 color    : COLOR;
    float2 uv       : TEXCOORD;
};

PS_INPUT mainVS(Vertex_Pos2dUvColor input)
{
    PS_INPUT output;
    output.position = mul(imgui_transform, float4(input.position.xy, 0.f, 1.f));
    output.color    = input.color;
    output.uv       = input.uv;
    return output;
}

float4 mainPS(PS_INPUT input) : SV_Target
{
    float mip            = (float)imgui_mip_level;
    float4 color_texture = texture_sample_point() ? tex.SampleLevel(sampler_point_wrap, input.uv, mip) : tex.SampleLevel(sampler_bilinear_wrap, input.uv, mip);
    float4 color_vertex  = input.color;

    // Set requested channels channels
    if (imgui_texture_flags != 0)
    {
        color_texture.r *= texture_channel_r() ? 1.0f : 0.0f;
        color_texture.g *= texture_channel_g() ? 1.0f : 0.0f;
        color_texture.b *= texture_channel_b() ? 1.0f : 0.0f;
        color_texture.a *= texture_channel_a() ? 1.0f : 0.0f;
    }

    if (texture_gamma_correction())
    {
        color_texture.rgb = gamma(color_texture.rgb);
    }

    if (texture_abs())
    {
        color_texture = abs(color_texture);
    }

    if (texture_pack())
    {
        color_texture.rgb = pack(color_texture.rgb);
    }

    if (texture_boost())
    {
        color_texture.rgb *= 10.0f;
    } 

    return color_vertex * color_texture;

}
