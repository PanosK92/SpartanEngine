/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

// = INCLUDES ========
#include "common.hlsl"
//====================

struct vertex
{
    float4 position : SV_POSITION;
    float4 color    : COLOR0;
};

vertex main_vs(float3 position : POSITION, float4 color : COLOR0)
{
    vertex output;
    output.position = mul(float4(position, 1.0f), buffer_frame.view_projection_unjittered);
    output.color = color;
    return output;
}

float4 main_ps(vertex input) : SV_TARGET
{
    if (is_occluded_by_scene(input.position))
    {
        discard;
    }

    return input.color;
}
