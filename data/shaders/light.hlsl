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

//= INCLUDES =================
#include "common.hlsl"
#include "brdf.hlsl"
#include "shadow_mapping.hlsl"
#include "sky/clouds.hlsl"
#include "light_cluster.hlsl"
//============================

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

// inline fallback when a local light did not get an nrd slot
#ifdef RAY_TRACING_ENABLED
static const uint k_area_inline_shadow_samples = 8;

float2 area_inline_shadow_uv(uint sample_index, float width, float height)
{
    float aspect = height / max(width, 1e-4f);
    if (aspect >= 3.0f || aspect <= (1.0f / 3.0f))
    {
        float t = ((float)sample_index + 0.5f) / (float)k_area_inline_shadow_samples * 2.0f - 1.0f;
        if (aspect >= 3.0f)
        {
            return float2(0.0f, t);
        }
        return float2(t, 0.0f);
    }

    const uint nx = 2;
    const uint ny = 4;
    uint ix = sample_index % nx;
    uint iy = sample_index / nx;
    float u = ((float)ix + 0.5f) / (float)nx * 2.0f - 1.0f;
    float v = ((float)iy + 0.5f) / (float)ny * 2.0f - 1.0f;
    return float2(u, v);
}

float trace_inline_area_shadow(Light light, float3 origin, float3 normal)
{
    float3 area_right;
    float3 area_up;
    light.compute_area_light_basis(area_right, area_up);
    float half_w         = light.area_width * 0.5f;
    float half_h         = light.area_height * 0.5f;
    float emitter_safety = min(min(light.area_width, light.area_height) * 0.5f, 0.08f) + 0.01f;
    float vis_sum        = 0.0f;

    [unroll]
    for (uint s = 0; s < k_area_inline_shadow_samples; s++)
    {
        float2 uv = area_inline_shadow_uv(s, light.area_width, light.area_height);
        float3 sample_point = light.position
            + area_right * uv.x * half_w
            + area_up    * uv.y * half_h;
        float3 to_sample = sample_point - origin;
        float dist       = length(to_sample);
        if (dist < 0.0001f)
        {
            vis_sum += 1.0f;
            continue;
        }

        float3 direction = to_sample / dist;
        if (dot(normal, direction) <= 0.0f)
        {
            vis_sum += 1.0f;
            continue;
        }

        RayDesc ray;
        ray.Origin    = origin;
        ray.Direction = direction;
        ray.TMin      = 0.001f;
        ray.TMax      = max(dist - emitter_safety, 0.001f);

        RayQuery<RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH | RAY_FLAG_SKIP_CLOSEST_HIT_SHADER> query;
        query.TraceRayInline(tlas, RAY_FLAG_NONE, 0x01, ray);
        query.Proceed();
        vis_sum += query.CommittedStatus() == COMMITTED_NOTHING ? 1.0f : 0.0f;
    }

    return vis_sum / (float)k_area_inline_shadow_samples;
}

float trace_inline_shadow_ray(Light light, Surface surface)
{
    float bias    = 0.005f + surface.camera_to_pixel_length * 0.0001f;
    float3 origin = surface.position + surface.normal * bias;

    if (light.is_area())
    {
        return trace_inline_area_shadow(light, origin, surface.normal);
    }

    float3 to_light = light.position - origin;
    float  dist     = length(to_light);
    if (dist < 0.0001f)
    {
        return 1.0f;
    }

    float3 direction = to_light / dist;
    if (dot(surface.normal, direction) <= 0.0f)
    {
        return 1.0f;
    }

    RayDesc ray;
    ray.Origin    = origin;
    ray.Direction = direction;
    ray.TMin      = 0.001f;
    ray.TMax      = max(dist - bias * 2.0f, 0.001f);

    RayQuery<RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH | RAY_FLAG_SKIP_CLOSEST_HIT_SHADER> query;
    query.TraceRayInline(tlas, RAY_FLAG_NONE, 0x01, ray);
    query.Proceed();

    return query.CommittedStatus() == COMMITTED_NOTHING ? 1.0f : 0.0f;
}
#endif

// subsurface scattering with wrapped diffuse and thickness estimation
float3 subsurface_scattering(Surface surface, Light light, AngularInfo angular_info)
{
    const float wrap_factor        = 0.5f;
    const float sss_scale          = 1.5f; // overall scattering strength
    const float min_scatter        = 0.05f;

    // light.to_pixel and surface.camera_to_pixel are pre-normalized by their builders
    float3 L = -light.to_pixel;
    float3 V = -surface.camera_to_pixel;
    float3 N = surface.normal;

    // wrapped diffuse for the front lit half, lets sun energy bleed past the n_dot_l terminator
    float n_dot_l_wrapped = saturate((dot(N, L) + wrap_factor) / (1.0f + wrap_factor));
    float wrapped_diffuse = n_dot_l_wrapped * n_dot_l_wrapped;

    // back scatter translucency, gdc 2011 penner, light direction distorted toward the normal
    const float distortion = 0.4f;
    float3 L_distorted     = normalize(L + N * distortion);
    float back_scatter     = saturate(dot(V, -L_distorted));
    back_scatter           = back_scatter * back_scatter * back_scatter;

    // combine forward and backward scattering, t crosses 0.5 at the terminator
    float sss_term = lerp(back_scatter, wrapped_diffuse, saturate(dot(N, L) * 0.5f + 0.5f));
    sss_term       = max(sss_term, min_scatter);

    // thickness modulation, thin grazing edges scatter more, back lit surfaces scatter more
    float n_dot_v             = saturate(dot(N, V));
    float one_minus_nv        = 1.0f - n_dot_v;
    float view_thickness      = one_minus_nv * sqrt(one_minus_nv);
    float n_dot_l_clamped     = saturate(dot(N, L));
    float light_thickness     = 1.0f - n_dot_l_clamped;
    float thickness_modulation = saturate(view_thickness + light_thickness * 0.5f);

    float  sss_strength = surface.subsurface_scattering * sss_scale;
    float3 sss_color    = surface.albedo;

    return light.radiance * sss_term * thickness_modulation * sss_strength * sss_color;
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

    float  L_shadow        = 1.0f;
    float3 L_specular_sum  = 0.0f;
    float3 L_diffuse_term  = 0.0f;
    float3 L_subsurface    = 0.0f;
    float  micro_shadow    = 1.0f;

    // brdf needs light.radiance, sss needs only raw energy, the gate admits either path
    // cull the inverse square tail clustering cannot, skip when the exposed contribution is sub perceptual
    const float k_contribution_cull = 1e-3f;
    float contribution_luminance    =
        luminance(radiometric_to_photometric(light_radiance_raw)) *
        get_effective_exposure();
    bool light_can_contribute       = contribution_luminance > k_contribution_cull;
    bool has_brdf                   = any(light.radiance > 0.0f);
    bool has_sss                    = surface.subsurface_scattering > 0.0f;

    if (!surface.is_sky() && light_can_contribute && (has_brdf || has_sss))
    {
        float L_shadow_primary = 1.0f;
        float L_shadow_contact = 1.0f;

        {
            const bool want_shadows          = light.has_shadows();
            const bool use_rt_shadow_texture = want_shadows && is_ray_traced_shadows_enabled() && light.is_directional();
            const bool use_nrd_local_shadow  = want_shadows && is_ray_traced_shadows_enabled() && light.nrd_local_shadow_slot() != 0u;
            const bool use_inline_rt_shadow  = want_shadows && !use_rt_shadow_texture && !use_nrd_local_shadow && is_ray_traced_shadows_enabled();
            const bool use_shadow_maps       = want_shadows && !use_rt_shadow_texture && !use_nrd_local_shadow && !use_inline_rt_shadow;

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
        #ifdef RAY_TRACING_ENABLED
            else if (use_inline_rt_shadow)
            {
                L_shadow_primary = trace_inline_shadow_ray(light, surface);
            }
        #endif
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
            if (light.has_shadows() && light.has_shadows_screen_space() && surface.is_opaque())
            {
                float contact = tex_uav_sss[
                    int3(
                        pixel_xy,
                        light_get_screen_space_shadow_slice(
                            light
                        )
                    )
                ].x;
                float contact_fade = 1.0f - saturate((surface.camera_to_pixel_length - 25.0f) / 50.0f);
                L_shadow_contact   = lerp(1.0f, contact, contact_fade);
            }
        }

        L_shadow = min(L_shadow_primary, L_shadow_contact);

        // brdf gets the combined shadow, sss gets only the primary
        light.radiance     *= L_shadow;
        light_radiance_raw *= L_shadow_primary;

        AngularInfo angular_info;
        angular_info.Build(light, surface);

        // chan 2018 microshadowing, ao driven terminator darkening, occlusion 1 means no effect
        micro_shadow = microw_shadowing_cod(angular_info.n_dot_l, surface.occlusion);

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
                L_specular_lobes += BRDF_Specular_Clearcoat(surface, angular_info);
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
            L_subsurface += subsurface_scattering(surface, light_sss, angular_info);
        }

        L_specular_sum += L_specular_lobes;

        surface.roughness       = original_roughness;
        surface.roughness_alpha = original_roughness_alpha;

        if (has_brdf && !is_transparent)
        {
            L_diffuse_term += BRDF_Diffuse(surface, angular_info);
        }
    }

    // micro_shadow defaults to 1 for sky and transparent paths
    // sss is not folded in since back lit surfaces have meaningless n_dot_l
    out_diffuse  += (L_diffuse_term * light.radiance * diffuse_precomputed * surface.diffuse_energy) * micro_shadow + L_subsurface;
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

    // transparents skip alpha on specular and zero diffuse
    // ao is not applied to direct light, it only modulates indirect, contact comes from micro_shadow
    bool   is_transparent       = surface.is_transparent();
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

    tex_uav[thread_id.xy]  = validate_output(float4(out_diffuse,  1.0f));
    tex_uav2[thread_id.xy] = validate_output(float4(out_specular, 1.0f));
}
