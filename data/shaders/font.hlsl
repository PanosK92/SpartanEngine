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
    float2 position : POSITION;
    float2 uv       : TEXCOORD;
    float4 color    : COLOR;
};

struct vertex_out
{
    float4 position : SV_POSITION;
    float2 uv       : TEXCOORD;
    float4 color    : COLOR;
};

vertex_out main_vs(vertex_in input)
{
    vertex_out output;
    output.position = mul(float4(input.position, 0.0f, 1.0f), buffer_frame.view_projection_orthographic);
    output.uv       = input.uv;
    output.color    = input.color;
    return output;
}

float4 main_ps(vertex_out input) : SV_TARGET
{
    // a negative u marks a solid quad (panels, bars, graphs) that ignores the atlas
    bool is_solid  = input.uv.x < 0.0f;
    float coverage = tex.Sample(samplers[sampler_bilinear_clamp], input.uv).r;

    // the outline pass passes its color with a non zero alpha, the fill pass passes zero and uses the vertex color
    float4 outline = pass_get_f4_value();
    if (outline.a > 0.0f)
    {
        if (is_solid)
        {
            discard;
        }

        return float4(outline.rgb, coverage * input.color.a);
    }

    return float4(input.color.rgb, input.color.a * (is_solid ? 1.0f : coverage));
}
