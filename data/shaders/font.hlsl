/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
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
