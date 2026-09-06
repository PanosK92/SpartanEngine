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
