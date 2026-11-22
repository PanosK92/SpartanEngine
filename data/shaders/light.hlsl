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
//============================

#ifdef RAY_TRACING_ENABLED
// one alpha tested ray from the surface to the light, for local lights without a denoised slot
// and for every light in stereo, the denoised masks only exist for the left eye
float trace_local_light_visibility(Surface surface, Light light)
{
    if (light.is_directional())
    {
        float3 direction = normalize(-light.forward);
        float offset     = max(0.01f + surface.camera_to_pixel_length * 0.0001f, rt_lod_shadow_offset(surface.camera_to_pixel_length));
        return rt_trace_visibility(surface.position + surface.normal * offset, direction, 1000.0f);
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
    float offset     = max(0.001f + min(surface.camera_to_pixel_length * 0.00001f, 0.002f), rt_lod_shadow_offset(surface.camera_to_pixel_length));
    float3 origin    = surface.position + surface.normal * offset;
    return rt_trace_visibility(origin, direction, distance - emitter_safety);
}
#endif

// samples the denoised half-res ray traced shadow with bilinear upsample, rgb so glass casts its colour
float3 sample_ray_traced_shadow(float2 uv)
{
    if (!is_ray_traced_shadows_enabled())
    {
        return 1.0f;
    }

    return shadow_mask_unpack(
        tex4.SampleLevel(
            GET_SAMPLER(sampler_bilinear_clamp),
            uv,
            0
        )
    );
}

float3 sample_nrd_local_shadow(float2 uv, uint slot)
{
    if (!is_ray_traced_shadows_enabled() || slot == 0u)
    {
        return 1.0f;
    }

    uint slice = slot - 1u;
    return shadow_mask_unpack(
        tex_rt_shadows_local.SampleLevel(
            GET_SAMPLER(sampler_bilinear_clamp),
            float3(uv, slice),
            0
        )
    );
}

// depth and normal aware blur, hides the discrete rectangle sample steps
float3 sample_nrd_area_shadow(float2 uv, uint slot)
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

    // Integrate the Gaussian in four bilinear footprints along each axis. Each pair
    // samples at its weighted centroid, with a bilateral guide at the same location.
    static const float offsets[4] = { -2.348645f, -0.468791f, 1.407333f, 3.0f };
    static const float weights[4] = { 0.931183f, 1.882497f, 1.489028f, 0.324652f };
    float4 acc       = 0.0f;
    float weight_sum = 0.0f;

    [unroll]
    for (uint y = 0; y < 4; y++)
    {
        [unroll]
        for (uint x = 0; x < 4; x++)
        {
            float2 tap_uv = uv + float2(offsets[x], offsets[y]) * texel;
            float4 visibility = tex_rt_shadows_local.SampleLevel(
                GET_SAMPLER(sampler_bilinear_clamp),
                float3(tap_uv, slice),
                0
            );
            float tap_z      = get_linear_depth(tap_uv);
            float3 tap_n     = get_normal(tap_uv);
            float weight_z   = saturate(1.0f - abs(center_z - tap_z) * 4.0f);
            float weight_n   = saturate(dot(center_n, tap_n));
            float weight_s   = weights[x] * weights[y];
            float weight     = weight_s * weight_z * weight_n;
            acc             += visibility * weight;
            weight_sum      += weight;
        }
    }

    return shadow_mask_unpack(acc / max(weight_sum, 1e-4f));
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

    float safety = light.is_area() ? min(min(light.area_width, light.area_height) * 0.5f, 0.08f) + 0.01f : 0.01f;
    return light.radiance * entry * INV_PI * rt_subsurface_transmittance(
        surface.position, surface.normal, l, distance_max, safety, surface.subsurface_radius);
}
#endif

// The same light without rays. The shadow slices of a local light hold where the light entered the solid, the
// distance from there to the pixel is the thickness. A directional cascade is too coarse to measure millimetres.
float3 subsurface_transmission_shadow_map(Surface surface, Light light)
{
    if (light.is_directional())
    {
        return 0.0f;
    }

    // only the side that faces away from the light. a point the light reaches skims along under its own surface and comes out
    // with next to nothing, and around the terminator the slices hold the outline of the body, where a texel is half solid and
    // half the wall behind it. a head is centimetres thick by then, so the fade only costs the rim of something thin
    float n_dot_l = dot(surface.normal, -light.to_pixel);
    float facing  = saturate((-n_dot_l - 0.05f) / 0.15f);
    if (facing <= 0.0f)
    {
        return 0.0f;
    }

    float radius_max    = max(max(surface.subsurface_radius.x, surface.subsurface_radius.y), surface.subsurface_radius.z);
    float thickness_max = radius_max * 16.0f;
    float thickness     = local_shadow_thickness(light, surface.position);
    if (thickness < 0.0f || thickness > thickness_max)
    {
        return 0.0f;
    }

    float entry = saturate(0.3f - n_dot_l);
    return light.radiance * entry * facing * INV_PI * subsurface_burley_transmittance(thickness, surface.subsurface_radius);
}

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

    // ReSTIR already supplies reflected radiance. Only integrate the missing transmission lobe.
    if (is_restir_pt_enabled())
    {
#ifdef RAY_TRACING_ENABLED
        light.radiance = light_radiance_raw;
        out_diffuse += subsurface_transmission(surface, light) * saturate(surface.subsurface_scattering) *
            (1.0f - surface.metallic) * (1.0f - surface.F0);
#endif
        return;
    }

    float3 L_shadow        = 1.0f;
    float3 L_specular_sum  = 0.0f;
    float3 L_diffuse_term  = 0.0f;
    float3 L_subsurface    = 0.0f;
    float  micro_shadow    = 1.0f;
    // a diffusion profile keeps the whole lambert lobe here, the subsurface pass spreads it afterwards,
    // the wrapped lobe below is the cheap stand in for materials without one
    bool   has_profile         = surface.has_subsurface_profile() && surface.metallic < 1.0f;
    float  scattering_fraction = has_profile || surface.is_water() ? 0.0f : saturate(surface.subsurface_scattering);

    // brdf needs light.radiance, sss needs only raw energy, the gate admits either path
    // cull the inverse square tail clustering cannot, skip when the exposed contribution is sub perceptual
    bool light_can_contribute       = light.can_contribute();
    bool has_brdf                   = any(light.radiance > 0.0f);
    bool has_sss                    = scattering_fraction > 0.0f && surface.metallic < 1.0f;
    // back lit thin parts measure the solid with rays, without a tlas the shadow slices of a local light stand in for them
    bool transmission_ray_traced    = is_ray_traced_shadows_enabled();
    bool has_transmission           = has_profile && !is_transparent && (transmission_ray_traced || (light.has_shadows() && !light.is_directional()));

    if (!surface.is_sky() && light_can_contribute && (has_brdf || has_sss || has_transmission))
    {
        if (has_transmission)
        {
            // the light enters on the far side, this pixel's own shadow says nothing about it
            Light light_transmitted    = light;
            light_transmitted.radiance = light_radiance_raw;
            float3 transmitted         = 0.0f;
            if (transmission_ray_traced)
            {
            #ifdef RAY_TRACING_ENABLED
                transmitted = subsurface_transmission(surface, light_transmitted);
            #endif
            }
            else
            {
                transmitted = subsurface_transmission_shadow_map(surface, light_transmitted);
            }
            L_subsurface += transmitted * saturate(surface.subsurface_scattering);
        }

        float3 L_shadow_primary = 1.0f;
        float L_shadow_contact = 1.0f;

        {
            // Ray tracing and raster shadow maps are mutually exclusive.
            // Do not blend a raster near/foliage cascade into the RT result.
            // where restir owns the direct light both precomputed terms are zero, only light that passes through the
            // solid is left and it traces its own way, so there is nothing a shadow lookup could darken
            const bool shadowed_output       = has_sss || any(diffuse_precomputed > 0.0f) || any(specular_precomputed > 0.0f);
            const bool want_shadows          = light.has_shadows() && shadowed_output;
            const bool ray_traced            = want_shadows && is_ray_traced_shadows_enabled();
            // The masks were traced at opaque depth in the left eye. Other receivers trace from their own position.
            const bool screen_masks          = !pass_is_transparent() && !is_multiview_active();
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
        {
            L_shadow_contact = lerp(L_shadow_contact, 1.0f, scattering_fraction * 0.78f);
            if (!is_ray_traced_shadows_enabled())
            {
                // Shadow maps only store the outermost crown surface; soften canopy self-shadowing so SSS still reads
                L_shadow_primary = lerp(L_shadow_primary, max(L_shadow_primary, 0.28f), scattering_fraction * 0.65f);
            }
        }
        if (surface.is_grass_blade())
            L_shadow_contact = grass_canopy_visibility(L_shadow_contact);
        L_shadow = min(L_shadow_primary, L_shadow_contact);
        if (!has_transmission && !any(L_shadow > 0.0f))
            return;

        // Transmission still needs a clear path to the light. Contact shadows also
        // contain the vegetation that is absent from the ray tracing structure.
        light.radiance     *= L_shadow;
        light_radiance_raw *= L_shadow;

        if (light.is_directional() && surface.is_opaque() && buffer_frame.ocean_enabled > 0.5f)
        {
            float footprint = ocean_foam_footprint(surface.camera_to_pixel_length, surface.position);
            float3 water_transmission = get_ocean_sun_transmission(surface.position, normalize(-light.forward), footprint, false);
            light.radiance *= water_transmission;
            light_radiance_raw *= water_transmission;
        }

        // The transparency pass applies foam coverage and albedo to this unit-albedo return.
        if (surface.is_water())
            out_diffuse += light.radiance * INV_PI;

        AngularInfo angular_info;
        angular_info.Build(light, surface);

        // chan 2018 microshadowing, ao driven terminator darkening, occlusion 1 means no effect
        // the aperture is centred on the bent normal, so light from the open side of a crevice is kept
        float bent_n_dot_l = saturate(dot(surface.bent_normal, -light.to_pixel));
        float micro_occ    = surface.is_grass_blade()
            ? grass_canopy_visibility(surface.occlusion)
            : (surface.is_foliage() ? foliage_canopy_visibility(surface.occlusion, surface.subsurface_scattering) : surface.occlusion);
        micro_shadow       = microw_shadowing_cod(bent_n_dot_l, micro_occ);

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

            if (surface.is_foliage())
            {
                // Fine needles and leaflets scatter direct specular across micro-facets rather than acting like a flat dielectric sheet
                L_specular_lobes *= 0.35f;
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
    // Alpha alone selects the pass. In particular the transparent dispatch
    // can reject opaque pixels before fetching their material and SSAO guides.
    float2 uv = (thread_id.xy + 0.5f) / resolution_out;
    float alpha = tex_albedo.SampleLevel(samplers[sampler_point_clamp], uv, 0).a;
    bool transparent = alpha > 0.0f && alpha < 1.0f;
    if ((pass_is_opaque() && transparent) || (pass_is_transparent() && !transparent))
        return;
    if (alpha == 0.0f && pass_is_opaque())
    {
        tex_uav[thread_id.xy]  = 0.0f;
        tex_uav2[thread_id.xy] = 0.0f;
        return;
    }

    Surface surface;
    surface.Build(thread_id.xy, resolution_out, true, true);
    float3 out_diffuse  = 0.0f;
    float3 out_specular = 0.0f;

    if (is_restir_pt_enabled() && (!surface.has_subsurface_profile() || !surface.is_opaque()))
    {
        tex_uav[thread_id.xy] = 0.0f;
        return;
    }

    // glass skips alpha on specular and zeroes diffuse, alpha blended surfaces are premultiplied like opaque ones
    // ao is not applied to direct light, it only modulates indirect, contact comes from micro_shadow
    bool   is_transparent       = surface.is_glass();
    float3 specular_precomputed = is_transparent ? float3(1.0f, 1.0f, 1.0f) : float3(surface.alpha, surface.alpha, surface.alpha);
    float3 diffuse_precomputed  = is_transparent ? float3(0.0f, 0.0f, 0.0f) : float3(surface.alpha, surface.alpha, surface.alpha);

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

    tex_uav[thread_id.xy]  = validate_output(float4(out_diffuse,  diffuses));
    tex_uav2[thread_id.xy] = validate_output(float4(out_specular, 1.0f));
}
