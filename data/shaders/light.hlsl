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

// samples the denoised ray traced shadow texture without crossing geometry edges
float sample_ray_traced_shadow(float2 uv)
{
    if (!is_ray_traced_shadows_enabled())
    {
        return 1.0;
    }

    return saturate(
        tex4.SampleLevel(
            GET_SAMPLER(sampler_point_clamp),
            uv,
            0
        ).r
    );
}

// inline ray traced shadow for any light type, deterministic hammersley disk
#ifdef RAY_TRACING_ENABLED
static const uint k_inline_shadow_spp = 2;

float radical_inverse_vdc(uint bits)
{
    bits = (bits << 16u) | (bits >> 16u);
    bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
    bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
    bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
    bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
    return float(bits) * 2.3283064365386963e-10f;
}

float2 hammersley_2d(uint index, uint count)
{
    return float2((float(index) + 0.5f) / float(count), radical_inverse_vdc(index + 1u));
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

float trace_inline_shadow_ray(Light light, Surface surface)
{
    // self intersection bias scaled by camera distance, reuse cached length
    float bias    = 0.005f + surface.camera_to_pixel_length * 0.0001f;
    float3 origin = surface.position + surface.normal * bias;

    float rot_angle = 0.785398163f;
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

    // cranley patterson rotation per pixel per frame so taa averages the low sample count down,
    // a deterministic 2 spp set would otherwise band, this jitters it into stable soft shadows
    float  frame_index = (float)buffer_frame.frame;
    float2 cp_rot;
    cp_rot.x = frac(hash(surface.uv)         + frame_index * 0.7548776662f);
    cp_rot.y = frac(hash(surface.uv + 17.3f) + frame_index * 0.5698402909f);

    [unroll]
    for (uint s = 0; s < k_inline_shadow_spp; s++)
    {
        float2 sample_square = frac(hammersley_2d(s, k_inline_shadow_spp) + cp_rot) * 2.0f - 1.0f;
        float2 disk          = concentric_disk(sample_square);

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
                                + area_right * sample_square.x * (light.area_width  * 0.5f)
                                + area_up    * sample_square.y * (light.area_height * 0.5f);
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

    float  L_shadow        = 1.0f;
    float3 L_specular_sum  = 0.0f;
    float3 L_diffuse_term  = 0.0f;
    float3 L_subsurface    = 0.0f;
    float3 L_volumetric    = 0.0f;
    float  micro_shadow    = 1.0f;

    // brdf needs light.radiance, sss needs only raw energy, the gate admits either path
    // cull the inverse square tail clustering cannot, skip when the exposed contribution is sub perceptual
    const float k_contribution_cull = 1e-3f;
    float contribution_luminance    = luminance(radiometric_to_photometric(light_radiance_raw)) * buffer_frame.camera_exposure;
    bool light_can_contribute       = contribution_luminance > k_contribution_cull;
    bool has_brdf                   = any(light.radiance > 0.0f);
    bool has_sss                    = surface.subsurface_scattering > 0.0f;

    if (eval_surface && !surface.is_sky() && light_can_contribute && (has_brdf || has_sss))
    {
        float L_shadow_primary = 1.0f;
        float L_shadow_contact = 1.0f;

        {
            const bool want_shadows          = light.has_shadows();
            const bool use_rt_shadow_texture = want_shadows && is_ray_traced_shadows_enabled() && light.is_directional();
            const bool use_inline_rt_shadow  = want_shadows && !use_rt_shadow_texture && is_ray_traced_shadows_enabled();
            const bool use_shadow_maps       = want_shadows && !use_rt_shadow_texture && !use_inline_rt_shadow;

            if (use_rt_shadow_texture)
            {
                L_shadow_primary = sample_ray_traced_shadow(surface.uv);
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

            bool rt_owns_contact = use_rt_shadow_texture || (use_inline_rt_shadow && light.is_directional());
            if (light.has_shadows() && light.has_shadows_screen_space() && surface.is_opaque() && !rt_owns_contact)
            {
                float contact = tex_uav_sss[int3(pixel_xy, light.screen_space_shadows_slice_index)].x;
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
