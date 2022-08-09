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

float4 postprocess_visualisation_options(float4 color_in)
{
    float4 color = float4(0.0f, 0.0f, 0.0f, 1.0f);

    // Set requested channels channels
    {
        if (texture_channel_r())
        {
            color.r = color_in.r;
        }

        if (texture_channel_g())
        {
            color.g = color_in.g;
        }

        if (texture_channel_b())
        {
            color.b = color_in.b;
        }

        if (texture_channel_a())
        {
            color.a = color_in.a;
        }
    }

    if (texture_gamma_correction())
    {
        color.rgb = gamma(color.rgb);
    }

    if (texture_abs())
    {
        color = abs(color);
    }

    if (texture_pack())
    {
        color.rgb = pack(color.rgb);
    }

    if (texture_boost())
    {
        color.rgb *= 10.0f;
    }

    return color;
}

float4 mainPS(PS_INPUT input) : SV_Target
{
    float4 color_vertex = input.color;
    if (imgui_texture_flags == 0)
        return color_vertex;
    
    float4 color_texture = tex.Sample(sampler_bilinear_wrap, input.uv);

    // Render targets can be visualised in various ways.
    if (texture_visualise())
    {
        if (texture_sample_point())
        {
            color_texture = tex.Sample(sampler_point_wrap, input.uv);
        }

        color_texture = postprocess_visualisation_options(color_texture);
    }

    return color_vertex * color_texture;

}
