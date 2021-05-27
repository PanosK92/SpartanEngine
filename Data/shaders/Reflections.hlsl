/*
Copyright(c) 2016-2021 Panos Karabelas

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
#include "Common.hlsl"
#include "BRDF.hlsl"
//====================

float4 mainPS(Pixel_PosUv input) : SV_TARGET
{
    const float2 uv             = input.uv;
    const float4 sample_albedo  = tex_albedo.SampleLevel(sampler_point_clamp, uv, 0);
    const bool is_transparent   = sample_albedo.a != 1.0f;

    if (is_transparent)
        discard;

    const float3 ssr_sample = is_ssr_enabled() ? tex_ssr.SampleLevel(sampler_point_clamp, uv, 0).rgb : 0.0f;
    const float2 ssr_uv     = ssr_sample.rg;
    const float ssr_alpha   = ssr_sample.b;

    float3 color_ssr            = 0.0f;
    float3 color_environment    = 0.0f;

    // Get ssr color
    if (ssr_alpha != 0.0f)
    {
        color_ssr = tex_frame.SampleLevel(sampler_bilinear_clamp, ssr_uv, 0).rgb;
    }

    // Get environment color
    if (ssr_alpha != 1.0f)
    {
        const float4 material_sample    = tex_material.SampleLevel(sampler_point_clamp, uv, 0);
        const float3 normal             = tex_normal.SampleLevel(sampler_point_clamp, uv, 0).xyz;

        Surface surface;
        surface.roughness  = material_sample.r;
        surface.metallic   = material_sample.g;
        surface.emissive   = material_sample.b;
        surface.F0         = lerp(0.04f, sample_albedo.rgb, surface.metallic);

        const float3 camera_to_pixel    = get_view_direction(uv);
        float3 reflection               = reflect(camera_to_pixel, normal);
        float mip_level                 = lerp(0, g_envrionement_max_mip, surface.roughness * surface.roughness);
        float n_dot_v                   = saturate(dot(-camera_to_pixel, normal));
        float3 F                        = F_Schlick_Roughness(surface.F0, n_dot_v, surface.roughness);
        float ambient_light             = saturate(g_directional_light_intensity / 128000.0f); // this is is wrong, SSR needs to sample an already darkened sky

        color_environment = tex_environment.SampleLevel(sampler_trilinear_clamp, direction_sphere_uv(reflection), mip_level).rgb * F * ambient_light;
    }

    return float4(lerp(color_environment, color_ssr, ssr_alpha), 0.0f);
}
