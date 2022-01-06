/*
Copyright(c) 2016-2022 Panos Karabelas

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
#include "brdf.hlsl"
//====================

struct Pixel_Input
{
    float4 position    : SV_POSITION;
    float3 position_ws : POSITION;
    float2 uv          : TEXCOORD;
    float3 normal      : NORMAL;
};

Pixel_Input mainVS(Vertex_PosUvNorTan input)
{
    Pixel_Input output;

    input.position.w   = 1.0f;
    output.position    = mul(input.position, g_transform);
    output.position_ws = output.position.xyz;
    output.normal      = normalize(mul(input.normal, (float3x3)g_transform)).xyz;
    output.uv          = input.uv;

    return output;
}

float4 mainPS(Pixel_Input input) : SV_TARGET
{
    Light light;
    light.Build(input.position_ws, input.normal, 0.0f);

    // Compute some vectors and dot products
    float3 l      = -light.to_pixel;
    float3 v      = g_camera_position - input.position_ws;
    float3 h      = normalize(v + l);
    float l_dot_h = saturate(dot(l, h));
    float v_dot_h = saturate(dot(v, h));
    float n_dot_v = saturate(dot(input.normal, v));
    float n_dot_h = saturate(dot(input.normal, h));

    // Albedo
    float4 albedo = g_mat_color;
    if (has_texture_albedo() != 0)
    {
        albedo     *= tex_material_albedo.Sample(sampler_anisotropic_wrap, input.uv);
        albedo.rgb = degamma(albedo.rgb);
    }

    // Roughness
    float roughness = 0.0f;
    if (has_texture_roughness())
    {
        roughness = tex_material_roughness.Sample(sampler_anisotropic_wrap, input.uv).r;
    }

    // Metallic
    float metallic = 0.0f;
    if (has_texture_metallic())
    {
        metallic = tex_material_metallic.Sample(sampler_anisotropic_wrap, input.uv).r;
    }

    float3 F0                = lerp(0.04f, albedo.rgb, metallic);
    float3 diffuse_energy    = 1.0f;
    float3 reflective_energy = 1.0f;
    float3 specular          = BRDF_Specular_Isotropic(roughness, metallic, F0, n_dot_v, light.n_dot_l, n_dot_h, v_dot_h, l_dot_h, diffuse_energy, reflective_energy);
    float3 diffuse           = BRDF_Diffuse(albedo.rgb, roughness, n_dot_v, light.n_dot_l, v_dot_h) * diffuse_energy;

    return float4((diffuse + specular) * light.radiance , albedo.a);
}
