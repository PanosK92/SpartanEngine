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

// = INCLUDES ======
#include "brdf.hlsl"
//==================

static const float g_ssr_fallback_threshold_roughness = 0.7f; // value above which blending with the environment is forced

// From Sebastien Lagarde Moving Frostbite to PBR page 69
float3 get_dominant_specular_direction(float3 normal, float3 reflection, float roughness)
{
    const float smoothness = 1.0f - roughness;
    const float lerpFactor = smoothness * (sqrt(smoothness) + roughness);
    
    return lerp(normal, reflection, lerpFactor);
}

float3 sample_environment(float2 uv, float mip_level)
{
    // We are currently using a spherical environment map which has a smallest mip size of 2x1.
    // So we have to do a bit of blending otherwise we'll get a visible seem in the middle.
    if (mip_level >= g_envrionement_max_mip)
    {
        // Shift by half a texel to the right, so that we are in-between the two pixels
        uv.x += (1.0f / g_resolution_environment.x) * 0.5f;
    }

    return tex_environment.SampleLevel(sampler_trilinear_clamp, uv, mip_level).rgb;
}

float4 mainPS(Pixel_PosUv input) : SV_TARGET
{
    const uint2 pos = input.uv * g_resolution_rt;
    
    // Construct surface
    Surface surface;
    surface.Build(pos, true, true, false);

    // TODO: Use the stencil buffer to avoid transparents or simply add SSR for transparents
    // If this is a transparent pass, ignore all opaque pixels, and vice versa.
    bool early_exit_1 = !g_is_transparent_pass && surface.is_transparent();
    bool early_exit_2 = g_is_transparent_pass && surface.is_opaque();
    if (early_exit_1 || early_exit_2 || surface.is_sky())
        discard;

    // Just a hack to tone down IBL since it comes from a static texture
    float3 light_ambient = saturate(g_directional_light_intensity / 128000.0f) * surface.occlusion;
    
    // Compute specular energy
    const float n_dot_v          = saturate(dot(-surface.camera_to_pixel, surface.normal));
    const float3 F               = F_Schlick_Roughness(surface.F0, n_dot_v, surface.roughness);
    const float2 envBRDF         = tex_lutIbl.SampleLevel(sampler_bilinear_clamp, float2(n_dot_v, surface.roughness), 0.0f).xy;
    const float3 specular_energy = F * envBRDF.x + envBRDF.y;

    // IBL - Diffuse
    float3 diffuse_energy = compute_diffuse_energy(specular_energy, surface.metallic); // Used to town down diffuse such as that only non metals have it
    float3 ibl_diffuse    = sample_environment(direction_sphere_uv(surface.normal), g_envrionement_max_mip) * surface.albedo.rgb * light_ambient * diffuse_energy;
    ibl_diffuse           *= surface.alpha; // Fade out for transparents

    // IBL - Specular
    const float3 reflection            = reflect(surface.camera_to_pixel, surface.normal);
    float3 dominant_specular_direction = get_dominant_specular_direction(surface.normal, reflection, surface.roughness);
    float mip_level                    = lerp(0, g_envrionement_max_mip, surface.roughness);
    float3 ibl_specular_environment    = sample_environment(direction_sphere_uv(dominant_specular_direction), mip_level) * light_ambient;
    
    // Get ssr color
    mip_level               = lerp(0, g_ssr_mip_count, surface.roughness);
    const float4 ssr_sample = (is_ssr_enabled() && !g_is_transparent_pass) ? tex_ssr.SampleLevel(sampler_trilinear_clamp, surface.uv, mip_level) : 0.0f;
    const float3 color_ssr  = ssr_sample.rgb ;
    float ssr_alpha         = ssr_sample.a;

    // Remap alpha above a certain roughness threshold in order to hide blocky reflections (from very small mips)
    if (surface.roughness > g_ssr_fallback_threshold_roughness)
    {
        ssr_alpha = lerp(ssr_alpha, 0.0f, (surface.roughness - g_ssr_fallback_threshold_roughness) / (1.0f - g_ssr_fallback_threshold_roughness));
    }

    // Blend between speculars and account for the specular energy
    float3 ibl_specular = lerp(ibl_specular_environment, color_ssr, ssr_alpha) * specular_energy;

    // Perfection achieved
    return float4(saturate_11(ibl_diffuse + ibl_specular), 0.0f);
}
