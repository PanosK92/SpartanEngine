#include "../common.hlsl"

struct VSOUT
{
    float4 pos : SV_Position;
    float2 uv  : TEXCOORD;
};

VSOUT main_vs(Vertex_PosUvNorTan input, uint instance_id : SV_InstanceID)
{
    VSOUT vs_out;

    float3 wpos = mul(input.position, buffer_pass.transform).xyz;
    vs_out.pos  = mul(float4(wpos, 1.0f), buffer_frame.view_projection);
    vs_out.uv   = input.uv;

    return vs_out;
}

float4 main_ps(VSOUT vertex) : SV_Target0
{
    const float4 foam_color = float4(1.0f, 1.0f, 1.0f, 1.0f);
    const float foam        = tex.Sample(samplers[sampler_point_clamp], vertex.uv).a * 300.0f;

    return foam_color;
}
