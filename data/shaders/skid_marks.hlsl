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
    float3 position       : POSITION;
    uint   uv_packed      : TEXCOORD;
    uint   normal_packed  : NORMAL;
    uint   tangent_packed : TANGENT;
};

struct skid_vertex
{
    float4 position                     : SV_POSITION;
    float2 uv                           : TEXCOORD0;
    float fade                          : TEXCOORD1;
    nointerpolation uint material_index : TEXCOORD2;
};

skid_vertex main_vs(vertex_in input)
{
    _draw = draw_data[buffer_pass.draw_index];

    skid_vertex output;
    float4 world    = mul(float4(input.position, 1.0f), _draw.transform);
    output.position = mul(world, get_view_projection());

    float2 uv = unpack_vertex_uv(input.uv_packed);
    uv        = uv * _draw.uv_tiling + _draw.uv_offset;
    output.uv = uv;

    // tangent uint carries ribbon fade, not a lighting tangent
    output.fade           = saturate(unpack_vertex_uv(input.tangent_packed).x);
    output.material_index = _draw.material_index;
    return output;
}

float4 main_ps(skid_vertex input) : SV_Target
{
    pass_load_draw_data_from_vertex(input.material_index);
    MaterialParameters material = GetMaterial();

    float4 albedo = material.color;
    if (material.has_texture_albedo())
    {
        float4 albedo_sample = GET_TEXTURE(material_texture_index_albedo).Sample(
            GET_SAMPLER(sampler_anisotropic_wrap),
            input.uv
        );
        if (material.is_albedo_srgb())
        {
            albedo_sample.rgb = srgb_to_linear(albedo_sample.rgb);
        }
        albedo *= albedo_sample;
    }

    // tex is the lit frame copy, multiply so grass shows through the rubber
    int2 pixel       = int2(input.position.xy);
    float3 dest      = tex.Load(int3(pixel, 0)).rgb;
    float coverage   = saturate(albedo.a * input.fade);
    float3 stained   = dest * lerp(float3(1.0f, 1.0f, 1.0f), albedo.rgb, coverage);
    return float4(stained, 1.0f);
}
