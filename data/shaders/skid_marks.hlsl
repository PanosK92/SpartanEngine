/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

#include "common.hlsl"

float4 main_ps(gbuffer_vertex vertex) : SV_Target0
{
    pass_load_draw_data_from_vertex(vertex.material_index);
    MaterialParameters material = GetMaterial();
    float coverage = saturate(vertex.uv_misc.z) * saturate(material.color.a);
    float4 stain = GET_TEXTURE(material_texture_index_albedo).Sample(GET_SAMPLER(sampler_anisotropic_wrap), vertex.uv_misc.xy);

    // Analytic shoulders remain soft at every mip, including the texture's 1x1 mip.
    float across = saturate(vertex.uv_misc.y);
    float shoulder = smoothstep(0.0f, 0.16f, across) * smoothstep(0.0f, 0.16f, 1.0f - across);
    coverage *= stain.a * shoulder;

    // Rubber is a matte surface layer, lit with the ground after this pass. A finite
    // reflectance prevents repeated passes converging to the old pitch-black multiply.
    return float4(float3(0.012f, 0.013f, 0.014f), saturate(coverage));
}
