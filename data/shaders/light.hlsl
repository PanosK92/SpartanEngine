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
#include "fog.hlsl"
#include "light_cluster.hlsl"
//============================

// samples the ray traced shadow texture bound on tex4
float sample_ray_traced_shadow(float2 uv)
{
    if (!is_ray_traced_shadows_enabled())
        return 1.0;

    float shadow = tex4.SampleLevel(GET_SAMPLER(sampler_bilinear_clamp), uv, 0).r;

    return shadow;
}

// inline ray traced shadow for any light type, stationary halton set rotated per pixel
#ifdef RAY_TRACING_ENABLED
static const uint k_inline_shadow_spp = 16;

static const float2 k_halton_2_3[16] =
{
    float2(0.500000f, 0.333333f),
    float2(0.250000f, 0.666667f),
    float2(0.750000f, 0.111111f),
    float2(0.125000f, 0.444444f),
    float2(0.625000f, 0.777778f),
    float2(0.375000f, 0.222222f),
    float2(0.875000f, 0.555556f),
    float2(0.062500f, 0.888889f),
    float2(0.562500f, 0.037037f),
    float2(0.312500f, 0.370370f),
    float2(0.812500f, 0.703704f),
    float2(0.187500f, 0.148148f),
    float2(0.687500f, 0.481481f),
    float2(0.437500f, 0.814815f),
    float2(0.937500f, 0.259259f),
    float2(0.031250f, 0.592593f)
};

// stationary per pixel hash used to rotate the halton disk
float spatial_hash_unit(float2 pixel_xy)
{
    return frac(52.9829189f * frac(pixel_xy.x * 0.06711056f + pixel_xy.y * 0.00583715f));
}

// concentric mapping from [-1,1]^2 square to unit disk
float2 concentric_disk(float2 u)
{
    if (u.x == 0.0f && u.y == 0.0f)
        return float2(0.0f, 0.0f);

    float r;
    float theta;
    if (abs(u.x) > abs(u.y))
    {
        r     = u.x;
        theta = (PI * 0.25f) * (u.y / u.x);
    }
    else
    {
        r     = u.y;
        theta = (PI * 0.5f) - (PI * 0.25f) * (u.x / u.y);
    }
    return r * float2(cos(theta), sin(theta));
}

float trace_inline_shadow_ray(Light light, Surface surface, float2 pixel_xy)
{
    // self intersection bias scaled by camera distance, reuse cached length
    float bias    = 0.005f + surface.camera_to_pixel_length * 0.0001f;
    float3 origin = surface.position + surface.normal * bias;

    // stationary per pixel rotation to break stratification banding without temporal motion
    float rot_angle = spatial_hash_unit(pixel_xy) * PI2;
    float cos_r     = cos(rot_angle);
    float sin_r     = sin(rot_angle);

    // precompute area light basis once across samples
    float3 area_right = 0.0f;
    float3 area_up    = 0.0f;
    if (light.is_area())
    {
        light.compute_area_light_basis(area_right, area_up);
    }

    // precompute a tangent frame for point/spot/directional source jitter
    float3 to_light_center = light.position - origin;
    float  center_dist     = length(to_light_center);
    float3 light_dir_unit  = light.is_directional() ? normalize(-light.forward) : (center_dist > 0.0001f ? to_light_center / center_dist : float3(0.0f, 1.0f, 0.0f));
    float3 up_axis         = abs(light_dir_unit.y) < 0.999f ? float3(0.0f, 1.0f, 0.0f) : float3(1.0f, 0.0f, 0.0f);
    float3 tangent         = normalize(cross(up_axis, light_dir_unit));
    float3 bitangent       = cross(light_dir_unit, tangent);

    // safety margin so area light rays do not hit the emitter mesh
    float emitter_safety = 0.0f;
    if (light.is_area())
    {
        emitter_safety = min(light.area_width, light.area_height) * 0.5f + 0.005f;
    }

    float visibility_sum = 0.0f;
    float valid_samples  = 0.0f;

    [unroll]
    for (uint s = 0; s < k_inline_shadow_spp; s++)
    {
        // halton point in [0,1)^2 mapped to disk in [-1,1]^2
        float2 u    = k_halton_2_3[s] * 2.0f - 1.0f;
        float2 disk = concentric_disk(u);

        // rotate by stationary per pixel angle
        float2 disk_r = float2(disk.x * cos_r - disk.y * sin_r,
                               disk.x * sin_r + disk.y * cos_r);

        float3 direction;
        float  t_max;

        if (light.is_directional())
        {
            // jittered cone around the sun direction, half angle around 0.5 degrees
            const float angular_radius = 0.0093f;
            direction                  = normalize(light_dir_unit + (tangent * disk_r.x + bitangent * disk_r.y) * angular_radius);
            t_max                      = 10000.0f;
        }
        else if (light.is_area())
        {
            // sample a deterministic point inside the rectangle for soft area shadows
            float3 sample_point = light.position
                                + area_right * disk_r.x * (light.area_width  * 0.5f)
                                + area_up    * disk_r.y * (light.area_height * 0.5f);
            float3 to_light = sample_point - origin;
            float  dist     = length(to_light);
            if (dist < 0.0001f)
            {
                visibility_sum += 1.0f;
                valid_samples  += 1.0f;
                continue;
            }
            direction = to_light / dist;
            t_max     = max(dist - emitter_safety, bias);
        }
        else
        {
            // point or spot, jitter inside a small spherical source for soft penumbra
            if (center_dist < 0.0001f)
            {
                visibility_sum += 1.0f;
                valid_samples  += 1.0f;
                continue;
            }
            const float light_radius = 0.05f;
            float3 jittered_target   = light.position + (tangent * disk_r.x + bitangent * disk_r.y) * light_radius;
            float3 to_jit            = jittered_target - origin;
            float  dist              = length(to_jit);
            direction                = to_jit / dist;
            t_max                    = max(dist - bias * 2.0f, bias);
        }

        // back facing samples carry no light energy, ignore them entirely from the average
        if (dot(surface.normal, direction) <= 0.0f)
            continue;

        valid_samples += 1.0f;

        RayDesc ray;
        ray.Origin    = origin;
        ray.Direction = direction;
        ray.TMin      = 0.001f;
        ray.TMax      = max(t_max, 0.001f);

        // mask 0x01 = opaque only, transparents do not block area/point/spot shadow rays
        RayQuery<RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH | RAY_FLAG_SKIP_CLOSEST_HIT_SHADER> query;
        query.TraceRayInline(tlas, RAY_FLAG_NONE, 0x01, ray);
        query.Proceed();

        visibility_sum += query.CommittedStatus() == COMMITTED_NOTHING ? 1.0f : 0.0f;
    }

    // divide by valid samples so glancing surfaces aren't artificially darkened
    return valid_samples > 0.0f ? (visibility_sum / valid_samples) : 1.0f;
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
    bool    eval_surface,
    bool    eval_volumetric,
    inout float3 out_diffuse,
    inout float3 out_specular,
    inout float3 out_volumetric)
{
    Light light;
    light.Build(light_index, surface);

    // raw light energy without n_dot_l, sss needs it since light.radiance bakes n_dot_l in
    float3 light_radiance_raw = light.color * light.intensity * light.attenuation;

    // restir owns all primary direct lighting, skip analytical surface eval to avoid double counting
    // volumetric still flows through the analytical path since restir does not own fog
    bool restir_owns_light     = is_restir_pt_enabled();
    bool skip_surface_lighting = restir_owns_light;

    float  L_shadow        = 1.0f;
    float3 L_specular_sum  = 0.0f;
    float3 L_diffuse_term  = 0.0f;
    float3 L_subsurface    = 0.0f;
    float3 L_volumetric    = 0.0f;
    float  micro_shadow    = 1.0f;

    // brdf needs light.radiance, sss needs only raw energy, the gate admits either path
    bool light_can_contribute = any(light_radiance_raw > 0.0f);
    bool has_brdf             = any(light.radiance > 0.0f);
    bool has_sss              = surface.subsurface_scattering > 0.0f;

    if (eval_surface && !surface.is_sky() && !skip_surface_lighting && light_can_contribute && (has_brdf || has_sss))
    {
        // shadow splits into primary (ray traced or shadow map) and screen space contact
        // primary applies to brdf and sss, contact applies only to brdf so it cannot crush sss
        bool can_use_rt_shadows = light.has_shadows() && is_ray_traced_shadows_enabled();

        float L_shadow_primary = 1.0f;
        float L_shadow_contact = 1.0f;

        if (can_use_rt_shadows && light.is_directional())
        {
            L_shadow_primary = sample_ray_traced_shadow(surface.uv);
        }
    #ifdef RAY_TRACING_ENABLED
        else if (can_use_rt_shadows)
        {
            L_shadow_primary = trace_inline_shadow_ray(light, surface, float2(pixel_xy));
        }
    #endif
        else if (light.has_shadows())
        {
            L_shadow_primary = compute_shadow(surface, light);
        }

        if (light.has_shadows() && light.has_shadows_screen_space() && surface.is_opaque())
        {
            float contact = tex_uav_sss[int3(pixel_xy, light.screen_space_shadows_slice_index)].x;

            // sss is a near field contact effect, fade it out with distance so far grazing surfaces never show marching stripes
            float contact_fade = 1.0f - saturate((surface.camera_to_pixel_length - 25.0f) / 50.0f);
            L_shadow_contact   = lerp(1.0f, contact, contact_fade);
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

        // diffuse_precomputed is zero for transparents, skip the eval entirely
        if (has_brdf && !is_transparent)
        {
            L_diffuse_term += BRDF_Diffuse(surface, angular_info);
        }
    }

    if (eval_volumetric && light.is_volumetric())
    {
        L_volumetric += compute_volumetric_fog(surface, light, pixel_xy);
    }

    // micro_shadow defaults to 1 for sky, volumetric and transparent paths
    // sss is not folded in since back lit surfaces have meaningless n_dot_l
    out_diffuse    += (L_diffuse_term * light.radiance * diffuse_precomputed * surface.diffuse_energy) * micro_shadow + L_subsurface;
    out_specular   += (L_specular_sum * light.radiance * specular_precomputed) * micro_shadow;
    out_volumetric += L_volumetric;
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
    bool early_exit_2 = pass_is_transparent() && surface.is_opaque();
    if (early_exit_1 || early_exit_2)
        return;

    float3 out_diffuse    = 0.0f;
    float3 out_specular   = 0.0f;
    float3 out_volumetric = 0.0f;

    // transparents skip alpha on specular and zero diffuse
    // ao is not applied to direct light, it only modulates indirect, contact comes from micro_shadow
    bool   is_transparent       = surface.is_transparent();
    float3 specular_precomputed = is_transparent ? float3(1.0f, 1.0f, 1.0f) : float3(surface.alpha, surface.alpha, surface.alpha);
    float3 diffuse_precomputed  = is_transparent ? float3(0.0f, 0.0f, 0.0f) : float3(surface.alpha, surface.alpha, surface.alpha);

    uint total_lights = buffer_frame.cluster_light_count;

    // slot 0 is always the directional sun, evaluated unconditionally because it has no spatial bound
    if (total_lights > 0u)
    {
        evaluate_light(0u, thread_id.xy, surface, is_transparent,
                       diffuse_precomputed, specular_precomputed,
                       true, true,
                       out_diffuse, out_specular, out_volumetric);
    }

    // clustered point, spot and area lights, the loop still runs under restir for volumetric only
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
            evaluate_light(light_idx, thread_id.xy, surface, is_transparent,
                           diffuse_precomputed, specular_precomputed,
                           true, false,
                           out_diffuse, out_specular, out_volumetric);
        }
    }

    // volumetric fog scans a cpu built list since fog rays cross multiple clusters
    uint volumetric_count = buffer_frame.volumetric_light_count;
    for (uint k = 0u; k < volumetric_count; k++)
    {
        uint v = volumetric_light_indices[k];
        evaluate_light(v, thread_id.xy, surface, is_transparent,
                       diffuse_precomputed, specular_precomputed,
                       false, true,
                       out_diffuse, out_specular, out_volumetric);
    }

    tex_uav[thread_id.xy]  = validate_output(float4(out_diffuse,    1.0f));
    tex_uav2[thread_id.xy] = validate_output(float4(out_specular,   1.0f));
    tex_uav3[thread_id.xy] = validate_output(float4(out_volumetric, 1.0f));
}
