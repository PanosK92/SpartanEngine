/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

//= INCLUDES =================
#include "common.hlsl"
#include "brdf.hlsl"
#include "shadow_mapping.hlsl"
#include "sky/cloud_shadow.hlsl"
#include "light_cluster.hlsl"
#include "subsurface_scattering.hlsl"
#include "rt_visibility.hlsl"
#include "restir_material_factors.hlsl"
//============================

#ifdef RAY_TRACING_ENABLED
// one alpha tested ray from the surface to the light, for local lights without a denoised slot
// and for every light in stereo, the denoised masks only exist for the left eye
float trace_local_light_visibility(Surface surface, Light light)
{
    if (light.is_directional())
    {
        float offset = 0.01f + surface.camera_to_pixel_length * 0.0001f;
        return rt_trace_visibility(surface.position + surface.normal * offset, normalize(-light.forward), 1000.0f);
    }

    float3 target = light.is_area()
        ? light.compute_closest_point_on_area(surface.position)
        : light.position;
    float3 to_light = target - surface.position;
    float distance  = length(to_light);
    // stop before the fixture around the emitter, like the denoised local rays
    float emitter_safety = light.is_area()
        ? min(min(light.area_width, light.area_height) * 0.5f, 0.08f) + 0.01f
        : 0.01f;
    if (distance <= emitter_safety + 0.002f)
    {
        return 1.0f;
    }

    float3 direction = to_light / distance;
    float offset     = 0.001f + min(surface.camera_to_pixel_length * 0.00001f, 0.002f);
    float3 origin    = surface.position + surface.normal * offset;
    return rt_trace_visibility(origin, direction, distance - emitter_safety);
}
#endif

// samples the denoised half-res ray traced shadow with bilinear upsample
float sample_ray_traced_shadow(float2 uv)
{
    if (!is_ray_traced_shadows_enabled())
    {
        return 1.0;
    }

    return saturate(
        tex4.SampleLevel(
            GET_SAMPLER(sampler_bilinear_clamp),
            uv,
            0
        ).r
    );
}

float sample_nrd_local_shadow(float2 uv, uint slot)
{
    if (!is_ray_traced_shadows_enabled() || slot == 0u)
    {
        return 1.0f;
    }

    uint slice = slot - 1u;
    return saturate(
        tex_rt_shadows_local.SampleLevel(
            GET_SAMPLER(sampler_bilinear_clamp),
            float3(uv, slice),
            0
        ).r
    );
}

// depth and normal aware blur, hides the discrete rectangle sample steps
float sample_nrd_area_shadow(float2 uv, uint slot)
{
    if (!is_ray_traced_shadows_enabled() || slot == 0u)
    {
        return 1.0f;
    }

    uint slice = slot - 1u;
    float width;
    float height;
    float layers;
    tex_rt_shadows_local.GetDimensions(width, height, layers);
    float2 texel = float2(1.0f / max(width, 1.0f), 1.0f / max(height, 1.0f));

    float center_z  = get_linear_depth(uv);
    float3 center_n = get_normal(uv);

    const int radius     = 3;
    const float sigma2   = 4.0f;
    float acc            = 0.0f;
    float weight_sum     = 0.0f;

    [unroll]
    for (int y = -radius; y <= radius; y++)
    {
        [unroll]
        for (int x = -radius; x <= radius; x++)
        {
            float2 tap_uv = uv + float2((float)x, (float)y) * texel;
            float visibility = tex_rt_shadows_local.SampleLevel(
                GET_SAMPLER(sampler_bilinear_clamp),
                float3(tap_uv, slice),
                0
            ).r;
            float tap_z      = get_linear_depth(tap_uv);
            float3 tap_n     = get_normal(tap_uv);
            float weight_z   = saturate(1.0f - abs(center_z - tap_z) * 4.0f);
            float weight_n   = saturate(dot(center_n, tap_n));
            float weight_s   = exp(-float(x * x + y * y) / (2.0f * sigma2));
            float weight     = weight_s * weight_z * weight_n;
            acc             += visibility * weight;
            weight_sum      += weight;
        }
    }

    return saturate(acc / max(weight_sum, 1e-4f));
}

// Demodulated diffuse: composition applies the actual leaf/blade albedo once.
float3 subsurface_scattering(Surface surface, Light light)
{
    float3 L = -light.to_pixel;
    float3 V = -surface.camera_to_pixel;
    if (surface.is_foliage())
    {
        float response = foliage_transmission_response(dot(surface.normal, L), dot(V, -L));
        return light.radiance * response * foliage_transmission_tint(surface.albedo.rgb);
    }
    return light.radiance * subsurface_diffuse_response(dot(surface.normal, L));
}

#ifdef RAY_TRACING_ENABLED
// Light that entered the far side of a thin part of a scattering solid and leaves towards the
// viewer, an ear or a nostril lit from behind. One ray from just under the surface measures the
// solid on the way to the light, a second one checks the light reaches the point where it entered.
// light.radiance carries the unshadowed energy without n_dot_l, the result is demodulated diffuse.
float3 subsurface_transmission(Surface surface, Light light)
{
    // the far side is not shaded here, its normal is taken as the reverse of this one, the
    // offset lets the glow start a little before the terminator instead of at a hard line
    float3 l    = -light.to_pixel;
    float entry = saturate(0.3f - dot(surface.normal, l));
    if (entry <= 0.0f)
    {
        return 0.0f;
    }

    float distance_max = 1000.0f;
    if (!light.is_directional())
    {
        float3 target = light.is_area() ? light.compute_closest_point_on_area(surface.position) : light.position;
        distance_max  = length(target - surface.position);
        l             = (target - surface.position) / max(distance_max, 1e-4f);
    }

    // past this the reddest channel is under half a percent, most of a head ends here
    float radius_max    = max(max(surface.subsurface_radius.x, surface.subsurface_radius.y), surface.subsurface_radius.z);
    float thickness_max = radius_max * 16.0f;
    const float inset   = 0.0005f;
    float3 origin       = surface.position - surface.normal * inset;
    float exit          = rt_trace_nearest(origin, l, min(distance_max, thickness_max));
    if (exit < 0.0f)
    {
        return 0.0f;
    }

    // stop before the fixture around the emitter, like the shadow rays
    float emitter_safety = light.is_area() ? min(min(light.area_width, light.area_height) * 0.5f, 0.08f) + 0.01f : 0.01f;
    float thickness      = exit + inset;
    float clearance      = distance_max - exit - emitter_safety;
    if (clearance > 0.002f && rt_trace_visibility(origin + l * (exit + 0.001f), l, clearance) <= 0.0f)
    {
        return 0.0f;
    }

    return light.radiance * entry * INV_PI * subsurface_burley_transmittance(thickness, surface.subsurface_radius);
}
#endif

// evaluates a single light against the surface, accumulates into the out parameters
// surface is taken by value so per light tweaks do not leak
void evaluate_light(
    uint    light_index,
    uint2   pixel_xy,
    Surface surface,
    bool    is_transparent,
    float3  diffuse_precomputed,
    float3  specular_precomputed,
    inout float3 out_diffuse,
    inout float3 out_specular)
{
    Light light;
    light.Build(light_index, surface);

    // raw light energy without n_dot_l, sss needs it since light.radiance bakes n_dot_l in
    float3 light_radiance_raw = light.color * light.intensity * light.attenuation;

    float  L_shadow        = 1.0f;
    float3 L_specular_sum  = 0.0f;
    float3 L_diffuse_term  = 0.0f;
    float3 L_subsurface    = 0.0f;
    float  micro_shadow    = 1.0f;
    // a diffusion profile keeps the whole lambert lobe here, the subsurface pass spreads it afterwards,
    // the wrapped lobe below is the cheap stand in for materials without one
    bool   has_profile         = surface.has_subsurface_profile() && surface.metallic < 1.0f;
    float  scattering_fraction = has_profile ? 0.0f : saturate(surface.subsurface_scattering);

    // brdf needs light.radiance, sss needs only raw energy, the gate admits either path
    // cull the inverse square tail clustering cannot, skip when the exposed contribution is sub perceptual
    bool light_can_contribute       = light.can_contribute();
    bool has_brdf                   = any(light.radiance > 0.0f);
    bool has_sss                    = scattering_fraction > 0.0f && surface.metallic < 1.0f;
    // back lit thin parts need rays through the solid, without a tlas they simply stay dark
    bool has_transmission           = has_profile && !is_transparent && is_ray_traced_shadows_enabled();

    if (!surface.is_sky() && light_can_contribute && (has_brdf || has_sss || has_transmission))
    {
    #ifdef RAY_TRACING_ENABLED
        if (has_transmission)
        {
            // the light enters on the far side, this pixel's own shadow says nothing about it
            Light light_transmitted    = light;
            light_transmitted.radiance = light_radiance_raw;
            L_subsurface += subsurface_transmission(surface, light_transmitted) * saturate(surface.subsurface_scattering);
        }
    #endif

        float L_shadow_primary = 1.0f;
        float L_shadow_contact = 1.0f;

        {
            // Ray tracing and raster shadow maps are mutually exclusive.
            // Do not blend a raster near/foliage cascade into the RT result.
            // where restir owns the direct light both precomputed terms are zero, only light that passes through the
            // solid is left and it traces its own way, so there is nothing a shadow lookup could darken
            const bool shadowed_output       = has_sss || any(diffuse_precomputed > 0.0f) || any(specular_precomputed > 0.0f);
            const bool want_shadows          = light.has_shadows() && shadowed_output;
            const bool ray_traced            = want_shadows && is_ray_traced_shadows_enabled();
            // the denoised masks are traced once from the left eye, the right eye would sample them misaligned
            const bool screen_masks          = !is_multiview_active();
            const bool use_rt_shadow_texture = ray_traced && screen_masks && light.is_directional();
            const bool use_nrd_local_shadow  = ray_traced && screen_masks && !light.is_directional() && light.nrd_local_shadow_slot() != 0u;
            const bool use_rt_inline         = ray_traced && !use_rt_shadow_texture && !use_nrd_local_shadow;
            const bool use_shadow_maps       = want_shadows && !ray_traced;

            if (use_rt_shadow_texture)
            {
                L_shadow_primary = sample_ray_traced_shadow(surface.uv);
            }
            else if (use_nrd_local_shadow)
            {
                if (light.is_area())
                {
                    L_shadow_primary = sample_nrd_area_shadow(surface.uv, light.nrd_local_shadow_slot());
                }
                else
                {
                    L_shadow_primary = sample_nrd_local_shadow(surface.uv, light.nrd_local_shadow_slot());
                }
            }
            else if (use_rt_inline)
            {
            #ifdef RAY_TRACING_ENABLED
                // local lights beyond the denoised slots take one hard inline ray
                L_shadow_primary = trace_local_light_visibility(surface, light);
            #endif
            }
            else if (use_shadow_maps)
            {
                L_shadow_primary = compute_shadow(surface, light);
            }

            if (light.is_directional())
            {
                L_shadow_primary *= cloud_shadow_sample(tex5, GET_SAMPLER(sampler_bilinear_clamp), surface.position, normalize(-light.forward), get_camera_position());
            }

            // contact shadows apply on top of whatever produced the primary term, ray traced included.
            // a ray can only hit what is in the acceleration structure, and gpu scatter has no entities
            // behind it, so grass and micro detail cast nothing there. the screen space trace sees them
            // because it reads the depth buffer. min() below takes the darker term, so the pixels the
            // ray already resolved correctly do not change
            if (want_shadows && light.has_shadows_screen_space() && surface.is_opaque())
            {
                float contact = tex_uav_sss[
                    int3(
                        pixel_xy,
                        light_get_screen_space_shadow_slice(
                            light
                        )
                    )
                ].x;
                // Every visible depth sample can cast contact shadows, including foliage
                // impostors. A world-distance fade erased them beyond 75 metres even
                // though the screen-space pass had already paid to compute them.
                L_shadow_contact = contact;
            }
        }

        // Screen-space depth has no leaf thickness and treats every card as
        // opaque. Retain contact detail without erasing the thin-sheet paths
        // resolved by the primary shadow ray. Solid occluders still fully block.
        if (surface.is_foliage())
            L_shadow_contact = lerp(L_shadow_contact, 1.0f, scattering_fraction * 0.70f);
        if (surface.is_grass_blade())
            L_shadow_contact = grass_canopy_visibility(L_shadow_contact);
        L_shadow = min(L_shadow_primary, L_shadow_contact);

        // Transmission still needs a clear path to the light. Contact shadows also
        // contain the vegetation that is absent from the ray tracing structure.
        light.radiance     *= L_shadow;
        light_radiance_raw *= L_shadow;

        if (light.is_directional() && surface.is_opaque() && buffer_frame.ocean_enabled > 0.5f)
        {
            float3 water_transmission = get_ocean_sun_transmission(surface.position, normalize(-light.forward));
            light.radiance *= water_transmission;
            light_radiance_raw *= water_transmission;
        }

        AngularInfo angular_info;
        angular_info.Build(light, surface);

        // chan 2018 microshadowing, ao driven terminator darkening, occlusion 1 means no effect
        // the aperture is centred on the bent normal, so light from the open side of a crevice is kept
        float bent_n_dot_l = saturate(dot(surface.bent_normal, -light.to_pixel));
        micro_shadow       = microw_shadowing_cod(bent_n_dot_l, surface.is_grass_blade() ? grass_canopy_visibility(surface.occlusion) : surface.occlusion);

        // area lights widen specular roughness so the highlight matches the light's angular extent
        float original_roughness       = surface.roughness;
        float original_roughness_alpha = surface.roughness_alpha;
        if (light.is_area())
        {
            surface.roughness_alpha = light.compute_area_roughness_modification(surface.roughness_alpha, light.distance_to_pixel);
            surface.roughness       = sqrt(surface.roughness_alpha);
        }

        float3 L_specular_lobes = 0.0f;
        if (has_brdf)
        {
            if (surface.anisotropic > 0.0f)
            {
                L_specular_lobes += BRDF_Specular_Anisotropic(surface, angular_info);
            }
            else
            {
                L_specular_lobes += BRDF_Specular_Isotropic(surface, angular_info);
            }

            if (surface.clearcoat > 0.0f)
            {
                // the base is lit through the coat, what the coat reflects never reaches it
                L_specular_lobes *= 1.0f - F_Schlick(0.04f, 1.0f, angular_info.v_dot_h) * surface.clearcoat;
                L_specular_lobes += BRDF_Specular_Clearcoat(surface, angular_info);
            }

            if (surface.flake_weight > 0.0f)
            {
                L_specular_lobes += BRDF_Specular_Flakes(surface, angular_info, light);
            }

            if (surface.sheen > 0.0f)
            {
                L_specular_lobes += BRDF_Specular_Sheen(surface, angular_info);
            }
        }

        if (has_sss)
        {
            // sss uses raw radiance so it can fire on back faced surfaces
            Light light_sss    = light;
            light_sss.radiance = light_radiance_raw;
            L_subsurface += subsurface_scattering(surface, light_sss) * scattering_fraction;
        }

        L_specular_sum += L_specular_lobes;

        surface.roughness       = original_roughness;
        surface.roughness_alpha = original_roughness_alpha;

        // a leaf transmits on top of reflecting, only solid scattering media trade one for the other
        if (has_brdf && !is_transparent)
        {
            float reflected_fraction = surface.is_foliage() ? 1.0f : 1.0f - scattering_fraction;
            L_diffuse_term += BRDF_Diffuse(surface, angular_info) * reflected_fraction;
        }
    }

    // micro_shadow defaults to 1 for sky and transparent paths
    // sss is not folded in since back lit surfaces have meaningless n_dot_l
    // Share the dielectric/coat diffuse budget instead of adding an unbounded glow.
    // Backlit surfaces skip the specular BRDF, so explicitly retain its metal/Fresnel rejection.
    float3 scattering_energy = has_brdf ? surface.diffuse_energy : (1.0f - surface.metallic) * (1.0f - surface.F0);
    out_diffuse  += (L_diffuse_term * light.radiance * diffuse_precomputed * surface.diffuse_energy) * micro_shadow
                 + L_subsurface * scattering_energy * surface.alpha;
    out_specular += (L_specular_sum * light.radiance * specular_precomputed) * micro_shadow;
}

[numthreads(THREAD_GROUP_COUNT_X, THREAD_GROUP_COUNT_Y, 1)]
void main_cs(uint3 thread_id : SV_DispatchThreadID)
{
    float2 resolution_out;
    tex_uav.GetDimensions(resolution_out.x, resolution_out.y);
    Surface surface;
    surface.Build(thread_id.xy, resolution_out, true, true);

    // early exit for mismatched pass/surface types
    bool early_exit_1 = pass_is_opaque() && surface.is_transparent() && !surface.is_sky();
    bool early_exit_2 = pass_is_transparent() && !surface.is_transparent();
    if (early_exit_1 || early_exit_2)
    {
        return;
    }

    if (surface.is_sky() && pass_is_opaque())
    {
        tex_uav[thread_id.xy]  = 0.0f;
        tex_uav2[thread_id.xy] = 0.0f;
        return;
    }

    float3 out_diffuse  = 0.0f;
    float3 out_specular = 0.0f;

    // glass skips alpha on specular and zeroes diffuse, alpha blended surfaces are premultiplied like opaque ones
    // ao is not applied to direct light, it only modulates indirect, contact comes from micro_shadow
    bool   is_transparent       = surface.is_glass();
    float3 specular_precomputed = is_transparent ? float3(1.0f, 1.0f, 1.0f) : float3(surface.alpha, surface.alpha, surface.alpha);
    float3 diffuse_precomputed  = is_transparent ? float3(0.0f, 0.0f, 0.0f) : float3(surface.alpha, surface.alpha, surface.alpha);

    // Unified ReSTIR carries direct diffuse and specular. Subsurface transmission
    // stays here; light_image_based composites the combined ReSTIR signal, except
    // on a surface with a diffusion profile, which takes it at the end of this pass.
    if (is_restir_pt_direct_enabled() && !surface.is_transparent() && !surface.is_water() && !surface.is_sky())
    {
        float coverage = restir_coverage(surface.uv);

        // nothing of a fully covered surface is left for this pass unless light also travels through it
        if (coverage >= 1.0f && surface.subsurface_scattering <= 0.0f)
        {
            tex_uav[thread_id.xy]  = 0.0f;
            tex_uav2[thread_id.xy] = float4(0.0f, 0.0f, 0.0f, 1.0f);
            return;
        }

        diffuse_precomputed  *= 1.0f - coverage;
        specular_precomputed *= 1.0f - coverage;
    }

    uint total_lights = buffer_frame.cluster_light_count;

    // slot 0 is always the directional sun, evaluated unconditionally because it has no spatial bound
    if (total_lights > 0u)
    {
        evaluate_light(
            0u,
            thread_id.xy,
            surface,
            is_transparent,
            diffuse_precomputed,
            specular_precomputed,
            out_diffuse,
            out_specular
        );
    }

    // clustered point, spot and area lights
    if (!surface.is_sky() && total_lights > 1u)
    {
        // the cluster grid lives in the left eye view projection space, shared by both vr eyes
        float4 hp_left   = mul(float4(surface.position, 1.0f), buffer_frame.view_projection);
        float3 ndc_left  = hp_left.xyz / hp_left.w;
        float2 uv_lookup = float2(ndc_left.x * 0.5f + 0.5f, 0.5f - ndc_left.y * 0.5f);
        float  view_z    = mul(float4(surface.position, 1.0f), buffer_frame.view).z;
        uint3  cid       = cluster_id_from_screen(uv_lookup, view_z);
        uint   flat_id   = cluster_flat(cid);
        uint2  range     = cluster_light_grid[flat_id];

        for (uint k = 0u; k < range.y; k++)
        {
            uint light_idx = cluster_light_indices[range.x + k];
            evaluate_light(
                light_idx,
                thread_id.xy,
                surface,
                is_transparent,
                diffuse_precomputed,
                specular_precomputed,
                out_diffuse,
                out_specular
            );
        }
    }

    // alpha marks the pixels whose diffuse light the subsurface pass spreads, their neighbours only gather from marked ones
    float diffuses = pass_is_opaque() && surface.is_opaque() && surface.has_subsurface_profile() && surface.metallic < 1.0f ? 1.0f : 0.0f;

    // the path traced light of such a pixel joins here rather than in light_image_based, which skips it, so the subsurface
    // pass spreads its diffuse share together with the transmission above. restir carries one radiance for both lobes, it is
    // split the way its denoiser splits it, by how much each lobe reflects. the analytic lobes of the lights were tried as
    // the ratio instead, without visibility they read a shadowed crease as a highlight and leave it unspread. tex3 holds
    // the signal at render resolution, a negative alpha marks a pixel the reconstruction could not fill, which stays
    // black as it does in light_image_based
    if (is_restir_subsurface_enabled() && diffuses > 0.0f)
    {
        float4 restir = tex3[thread_id.xy];
        if (restir.a >= 0.0f)
        {
            float3 albedo = saturate(tex_albedo.SampleLevel(samplers[sampler_point_clamp], surface.uv, 0).rgb);
            float3 view   = normalize(-surface.camera_to_pixel);
            float3 factor_diffuse;
            float3 factor_specular;
            restir_nrd_material_factors(albedo, surface.metallic, ggx_alpha_from_roughness(max(surface.roughness, 0.04f)), saturate(dot(surface.normal, view)), factor_diffuse, factor_specular);

            // composition multiplies the diffuse target by albedo, so it takes the light ahead of that
            float3 illumination = max(restir.rgb, 0.0f) * restir_coverage(surface.uv) * surface.alpha / (factor_diffuse + factor_specular);
            out_diffuse        += illumination * (1.0f - surface.metallic);
            out_specular       += illumination * factor_specular;
        }
    }

    tex_uav[thread_id.xy]  = validate_output(float4(out_diffuse,  diffuses));
    tex_uav2[thread_id.xy] = validate_output(float4(out_specular, 1.0f));
}
