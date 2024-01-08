/*
Copyright(c) 2016-2024 Panos Karabelas

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

// = INCLUDES ======
#include "brdf.hlsl"
//==================

float3 get_dominant_specular_direction(float3 normal, float3 reflection, float roughness)
{
    const float smoothness = 1.0f - roughness;
    const float alpha      = smoothness * (sqrt(smoothness) + roughness);
    
    return lerp(normal, reflection, alpha);
}

float3 sample_environment(float2 uv, float mip_level)
{
    return tex_environment.SampleLevel(samplers[sampler_trilinear_clamp], uv, mip_level).rgb;
}

float4 mainPS(Pixel_PosUv input) : SV_TARGET
{
    const uint2 pos = input.uv * pass_get_resolution_out();
    
    // construct surface
    Surface surface;
    bool use_ssgi = pass_is_opaque(); // we don't do ssgi for transparents.
    surface.Build(pos, true, use_ssgi, false);

    bool early_exit_1 = pass_is_opaque() && surface.is_transparent(); // if this is an opaque pass, ignore all transparent pixels
    bool early_exit_2 = pass_is_transparent() && surface.is_opaque(); // if this is an transparent pass, ignore all opaque pixels
    bool early_exit_3 = surface.is_sky();                             // we don't want to do ibl on the sky itself
    if (early_exit_1 || early_exit_2 || early_exit_3)
        discard;

    // compute specular energy
    const float n_dot_v          = saturate(dot(-surface.camera_to_pixel, surface.normal));
    const float3 F               = F_Schlick_Roughness(surface.F0, n_dot_v, surface.roughness);
    const float2 envBRDF         = tex_lut_ibl.SampleLevel(samplers[sampler_bilinear_clamp], float2(n_dot_v, surface.roughness), 0.0f).xy;
    const float3 specular_energy = F * envBRDF.x + envBRDF.y;

    // ibl - diffuse
    float mip_count_environment = pass_get_f3_value().x;
    float3 diffuse_energy       = compute_diffuse_energy(specular_energy, surface.metallic); // used to town down diffuse such as that only non metals have it
    float3 ibl_diffuse          = sample_environment(direction_sphere_uv(surface.normal), mip_count_environment) * surface.albedo.rgb * diffuse_energy;

    // ibl - specular
    const float3 reflection            = reflect(surface.camera_to_pixel, surface.normal);
    float3 dominant_specular_direction = get_dominant_specular_direction(surface.normal, reflection, surface.roughness);
    float mip_level                    = lerp(0, mip_count_environment - 1, surface.roughness);
    float3 ibl_specular_environment    = sample_environment(direction_sphere_uv(dominant_specular_direction), mip_level);
    
    // blend between ssr and the envirnoment
    const float4 ssr_sample = is_ssr_enabled() ? tex_ssr.SampleLevel(samplers[sampler_trilinear_clamp], surface.uv, 0.0f) : 0.0f;
    float3 ibl_specular     = lerp(ibl_specular_environment, ssr_sample.rgb, ssr_sample.a);

    // modulate outcoming energy
    ibl_specular *= specular_energy;

    float3 ibl = ibl_diffuse + ibl_specular;

    // ssgi
    if (is_ssgi_enabled() && use_ssgi)
    {
        ibl *= surface.occlusion;
    }

    // make transparent for transparents
    ibl *= surface.alpha;

    // perfection achieved
    return float4(ibl, 0.0f);
}
