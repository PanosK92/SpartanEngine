#include "../common.hlsl"

struct VSOUT
{
    float4 pos : SV_Position;
    float2 uv  : TEXCOORD;
};

VSOUT main_vs(Vertex_PosUvNorTan input, uint instance_id : SV_InstanceID)
{
    //input.position.xyz += displacement_map.SampleLevel(samplers[sampler_point_clamp], input.uv, 0).rgb;
    //float4 slope = slope_map.SampleLevel(samplers[sampler_point_clamp], input.uv, 0);
    //input.normal = normalize(float3(-slope.x, 1.0f, -slope.y));
    
    //gbuffer_vertex vertex = transform_to_world_space(input, instance_id, buffer_pass.transform);

    //// transform world space position to clip space
    //Surface surface;
    //surface.flags = GetMaterial().flags;
    //if (!surface.is_tessellated())
    //{
    //    vertex = transform_to_clip_space(vertex);
    //}

    //return vertex;

    VSOUT vs_out;

    float3 wpos = mul(input.position, buffer_pass.transform).xyz;
    vs_out.pos = mul(float4(wpos, 1.0f), buffer_frame.view_projection);
    vs_out.uv = input.uv;

    return vs_out;
}

Texture2D<float4> output : register(t0);
Texture2D<float4> slope_map : register(t1);

//[earlydepthstencil]
float4 main_ps(VSOUT vertex) : SV_Target0
{
    const float4 foam_color = float4(1.0f, 1.0f, 1.0f, 1.0f);
    const float foam = slope_map.Sample(samplers[sampler_point_clamp], vertex.uv).a * 300.0f;

    const float4 output_sample = output.Sample(samplers[sampler_point_clamp], vertex.uv);

    return float4(lerp(output_sample.rgb, foam_color.rgb, foam), output_sample.a);
}
