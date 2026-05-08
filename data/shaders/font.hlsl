/*
Copyright(c) 2015-2026 Panos Karabelas

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

struct vertex_in
{
    float3 position : POSITION;
    float2 uv       : TEXCOORD;
};

struct vertex_out
{
    float4 position : SV_POSITION;
    float2 uv       : TEXCOORD;
};

vertex_out main_vs(vertex_in input)
{
    vertex_out output;
    output.position = mul(float4(input.position, 1.0f), buffer_frame.view_projection_orthographic);
    output.uv       = input.uv;
    return output;
}

float4 main_ps(vertex_out input) : SV_TARGET
{
    float4 color = float4(0.0f, 0.0f, 0.0f, 1.0f);
    
    // sample text from texture atlas
    color.r = tex.Sample(samplers[sampler_bilinear_clamp], input.uv).r;
    color.g = color.r;
    color.b = color.r;
    color.a = color.r;

    // color it
    color *= float4(pass_get_f4_value().rgb, 1.0f);

    return color;
}
