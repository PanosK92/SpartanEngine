/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

// = INCLUDES ========
#include "common.hlsl"
//====================

struct vertex_in
{
    float3 position      : POSITION;
    uint   uv_packed     : TEXCOORD;
    uint   normal_packed : NORMAL;
    uint   tangent_packed: TANGENT;
};

struct vertex
{
    float4 position : SV_POSITION;
    float2 uv       : TEXCOORD0;
};

vertex main_vs(vertex_in input)
{
    vertex output;
    float4 p        = float4(input.position, 1.0f);
    p               = mul(p, draw_data[buffer_pass.draw_index].transform);
    output.position = mul(p, buffer_frame.view_projection_unjittered);
    output.uv       = 0.0f;
    return output;
}
 
float4 main_ps(vertex input) : SV_Target
{
    // just a color
    return pass_get_f4_value();
}

[numthreads(THREAD_GROUP_COUNT_X, THREAD_GROUP_COUNT_Y, 1)]
void main_cs(uint3 thread_id : SV_DispatchThreadID)
{
    float4 silhouette = tex[thread_id.xy];
    float3 color      = silhouette.rgb;
    float alpha       = silhouette.a;

    tex_uav[thread_id.xy] += float4(color * (1.0f - alpha), 0.0f);
}
