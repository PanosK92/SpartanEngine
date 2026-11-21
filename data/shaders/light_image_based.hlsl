/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

// = INCLUDES ========================
#include "brdf.hlsl"
#include "spherical_harmonics.hlsl"
#include "subsurface_scattering.hlsl"
//====================================

// last frame's opaque gbuffer normals, bound when the gi is last frame's async output
Texture2D<uint2> tex_normal_previous : register(t87);

// approx of multi-bounce (inter-reflection) for ao
float3 gtao_multi_bounce(float visibility, float3 albedo)
{
    // Jimenez et al. 2016 approximation
    float3 a = 2.0404f * albedo - 0.3324f;
    float3 b = -4.7951f * albedo + 0.6417f;
    float3 c = 2.7552f * albedo + 0.6903f;
    
    return max(visibility, ((visibility * a + b) * visibility + c) * visibility);
}

float3 fresnel_schlick_roughness(float cos_theta, float3 F0, float roughness)
{
    // roughness modified fresnel
    return F0 + (max(1.0f - roughness.xxx, F0) - F0) * pow(saturate(1.0f - cos_theta), 5.0f);
}

[numthreads(THREAD_GROUP_COUNT_X, THREAD_GROUP_COUNT_Y, 1)]
void main_cs(uint3 thread_id : SV_DispatchThreadID)
{
    // surface setup
    float2 resolution_out;
    tex_uav.GetDimensions(resolution_out.x, resolution_out.y);
    Surface surface;
    surface.Build(thread_id.xy, resolution_out, true, false);

    // sky check
    if (surface.is_sky())
        return;

    // the opaque pass lit everything else before the refraction background was copied
    if (pass_is_transparent() && !surface.is_transparent())
        return;

    // view and energy calculations
    const float3 view_dir = normalize(-surface.camera_to_pixel);
    const float n_dot_v   = saturate(dot(surface.normal, view_dir));
    
    // split-sum approximation: use F0 (not full fresnel) as the LUT already encodes fresnel response
    const float2 envBRDF = surface.is_water() ? reflection_env_brdf(surface.roughness, n_dot_v) : tex2.SampleLevel(samplers[sampler_bilinear_clamp], float2(n_dot_v, surface.roughness), 0.0f).xy;
    float3 specular_energy = (surface.F0 * envBRDF.x + envBRDF.y) * compute_multiscatter_energy_split_sum(surface.F0, envBRDF);
    
    if (surface.is_water()) specular_energy = saturate(specular_energy);

    // diffuse energy uses roughness-modified fresnel for proper energy conservation
    const float3 F              = fresnel_schlick_roughness(n_dot_v, surface.F0, surface.roughness);
    const float3 diffuse_energy = compute_diffuse_energy(F, surface.metallic);

    // specular reflection setup
    float3 dominant_specular_direction = get_dominant_specular_direction(surface.normal, view_dir, surface.roughness);
    float mip_count_environment        = pass_float(pass_light_image_based::environment_mip_count);
    float mip_level                    = surface.roughness * surface.roughness * (mip_count_environment - 1.0f);
    
    // water has analytic normals and an unoccluded sky view, the ao and bent normal terms run on placeholder data and wrongly crush its reflection, so the open ocean keeps only the horizon fade
    float specular_occlusion = 1.0f;
    if (!surface.is_water())
    {
        specular_occlusion = specular_occlusion_bent(surface.bent_normal, surface.occlusion, dominant_specular_direction, n_dot_v, surface.roughness, surface.roughness_alpha);
    }

    // horizon fade, lagarde 2014, stops sampling the panorama below the surface plane
    float horizon       = saturate(1.0f + dot(dominant_specular_direction, surface.normal));
    specular_occlusion *= horizon * horizon;

    // environment sampling, specular stays on the prefiltered skysphere
    float3 specular_skysphere = tex3.SampleLevel(
        samplers[sampler_trilinear_clamp],
        direction_sphere_uv(dominant_specular_direction),
        mip_level
    ).rgb;

    // diffuse directional gtao on sh, the sky is integrated over the visibility cone around the
    // bent normal, multi-bounce is applied as a relative boost so occlusion is not squared on top of the cone
    float3 sky_sh[9];
    sh_load_l2(tex5, sky_sh);
    const float ibl_visibility_floor = 0.1f;
    float  raw_visibility = surface.is_grass_blade()
        ? grass_canopy_visibility(surface.occlusion)
        : (surface.is_foliage() ? foliage_canopy_visibility(surface.occlusion, surface.subsurface_scattering) : surface.occlusion);
    float  ibl_visibility = max(raw_visibility, ibl_visibility_floor);
    float3 diffuse_skysphere = sh_irradiance_l2(
        surface.bent_normal,
        sky_sh,
        ibl_visibility
    );
    // Thin leaves also receive sky through their reverse side, with the same deeper green the
    // sun picks up on the way through, lifting shaded undersides out of harsh black silhouettes.
    float3 transmitted_sky = 0.0f;
    if (surface.is_foliage() && !surface.is_transparent())
    {
        float transmit_vis = lerp(ibl_visibility, 1.0f, 0.35f);
        transmitted_sky = sh_irradiance_l2(-surface.normal, sky_sh, transmit_vis) *
                          saturate(surface.subsurface_scattering) * 1.35f *
                          foliage_transmission_tint(surface.albedo.rgb);
    }
    float3 multi_bounce = gtao_multi_bounce(ibl_visibility, surface.albedo.rgb);
    float3 bounce_boost = multi_bounce / ibl_visibility;
    // SH convolution returns irradiance, so the Lambert BRDF still needs 1/pi.
    float3 diffuse_ibl          = diffuse_skysphere * INV_PI * bounce_boost * diffuse_energy * surface.albedo.rgb;
    float3 diffuse_ibl_transmit = transmitted_sky * INV_PI * diffuse_energy * surface.albedo.rgb;
    float3 specular_ibl = specular_skysphere * specular_energy * specular_occlusion;
    if (surface.is_foliage())
    {
        // Macro foliage cards represent hundreds of scattered needles/leaflets; suppress flat-card grazing sky mirror glare
        specular_ibl *= 0.28f;
    }

    // transparents have no diffuse lobe, transmission is composited in reflections_apply, a sky
    // lambert layer on top reads as an opaque milky sheet, the specular sky reflection stays
    if (surface.is_water() || surface.is_glass())
    {
        diffuse_ibl = 0.0f;
    }

    diffuse_ibl += diffuse_ibl_transmit;

    // metallic flakes mirror the surroundings through a lobe far wider than the traced reflection, the
    // sky irradiance stands in for it, half of what they reflect is trapped by the coat's total internal reflection
    if (surface.flake_weight > 0.0f && !surface.is_transparent())
    {
        float flake_exit = 0.5f * coat_transmission(surface.clearcoat, n_dot_v, n_dot_v);
        diffuse_ibl     *= 1.0f - surface.flake_weight * 0.6f;
        diffuse_ibl     += diffuse_skysphere * INV_PI * surface.flake_color * surface.flake_weight * flake_exit;
    }

    // Traced reflections own specular at every roughness. The sky probe has no
    // scene visibility, so blending it back in makes sheltered rough surfaces glow.
    if (is_ray_traced_reflections_enabled())
    {
        specular_ibl = 0.0f;
    }

    // transparents take full ibl, fresnel inside the split sum already governs the reflection split
    float3 ibl  = diffuse_ibl + specular_ibl;
    ibl        *= surface.is_glass() ? 1.0f : surface.alpha;

    // output
    tex_uav[thread_id.xy] += validate_output(float4(ibl, 0.0f));
}
