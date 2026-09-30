/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

//= INCLUDES =================
#include "common.hlsl"
#include "brdf.hlsl"
#include "fog_volume.hlsl"
#include "common_ray_surface.hlsl"
//============================

// shades ray traced reflection hits, analytic lights with ray traced visibility, the diffuse bounce
// from the world space radiance cache, a traced second specular bounce on smooth hits, clearcoat and
// sheen layers, and the mist the reflected ray crosses

// the two strongest shadowed local lights get their own shadow ray, the rest share one sampled ray
static const uint  k_exact_shadow_lights    = 2;
// hits smoother than this trace their own reflection, rougher ones read the prefiltered sky
static const float k_second_bounce_start    = 0.25f;
static const float k_second_bounce_end      = 0.35f;
// one pixel in four feeds the radiance cache each frame
static const uint  k_cache_update_divisor   = 4;
// misses cross this much air before reaching the sky
static const float k_fog_miss_distance      = 256.0f;

float3 reflections_compress_luminance(float3 color, float knee, float shoulder)
{
    float luma = luminance(color);
    if (luma <= knee)
    {
        return color;
    }

    float excess          = luma - knee;
    float compressed_luma = knee + excess / (1.0f + excess / max(shoulder, 1e-3f));
    return color * (compressed_luma / max(luma, FLT_MIN));
}

float reflections_random(inout uint seed)
{
    seed = seed * 747796405u + 2891336453u;
    uint word = ((seed >> ((seed >> 28u) + 4u)) ^ seed) * 277803737u;
    return float((word >> 22u) ^ word) * 2.3283064365386963e-10f;
}

// the reflected ray crosses the same mist the camera looks through, the froxel volume holds the
// medium and its in-scattered light at every world point it covers, so the segment is marched through
// it, points outside the frustum take the nearest froxel and the phase stays the camera's
float3 reflections_fog(float3 radiance, float3 origin, float3 direction, float distance)
{
    float3 transmittance = 1.0f;
    float3 scattering    = 0.0f;
    float  previous      = 0.0f;
    [unroll]
    for (uint i = 0; i < 4; i++)
    {
        // quadratic spacing, most short reflections end inside the first slices
        float fraction = float(i + 1u) / 4.0f;
        float t        = distance * fraction * fraction;
        float dt       = t - previous;
        float3 p       = origin + direction * (previous + dt * 0.5f);
        previous       = t;

        float3 p_view = world_to_view(p);
        p_view.z      = max(p_view.z, buffer_frame.camera_near);
        float3 coord  = float3(saturate(view_to_uv(p_view)), fog_distance_to_slice(length(p - get_camera_position())));
        float2 medium = tex_fog_extinction.SampleLevel(GET_SAMPLER(sampler_trilinear_clamp), coord, 0.0f).rg;
        float  air    = max(medium.r, 0.0f) * (1.0f - saturate(medium.g));
        float3 source = tex_fog_air_source.SampleLevel(GET_SAMPLER(sampler_trilinear_clamp), coord, 0.0f).rgb * (1.0f - saturate(medium.g));

        scattering    += transmittance * max(source, 0.0f) * fog_segment_weight(air, dt);
        transmittance *= exp(-air * dt);
    }
    return radiance * transmittance + scattering;
}

// the skysphere holds no local mist, the camera sees its sky through the whole froxel column, so a
// miss whose direction is on screen takes that column's transport, a few metres of origin offset are
// negligible against it, directions off screen fall back to the short march
float3 reflections_fog_sky(float3 radiance, float3 origin, float3 direction)
{
    float3 marched   = reflections_fog(radiance, origin, direction, k_fog_miss_distance);
    float3 dir_view  = world_to_view(direction, false);
    if (dir_view.z <= 0.05f)
        return marched;

    float2 uv        = view_to_uv(dir_view, false);
    float2 edge      = min(uv, 1.0f - uv);
    float  on_screen = smoothstep(0.0f, 0.05f, min(edge.x, edge.y));
    if (on_screen <= 0.0f)
        return marched;

    FogTransport column = sample_fog_volume(uv, fog_far);
    return lerp(marched, radiance * column.transmittance + column.scattering, on_screen);
}

struct LightCandidate
{
    float3 radiance;
    float  weight;
    uint   index;
};

// the weighted reservoir keeps one of the lights streamed through it with probability weight / total
void reflections_stream_light(inout LightCandidate selected, inout float total, LightCandidate candidate, inout uint seed)
{
    if (candidate.weight <= 0.0f)
        return;
    total += candidate.weight;
    if (reflections_random(seed) * total < candidate.weight)
    {
        selected = candidate;
    }
}

[numthreads(THREAD_GROUP_COUNT_X, THREAD_GROUP_COUNT_Y, 1)]
void main_cs(uint3 thread_id : SV_DispatchThreadID)
{
    float2 resolution_out;
    tex_uav.GetDimensions(resolution_out.x, resolution_out.y);

    if (thread_id.x >= resolution_out.x || thread_id.y >= resolution_out.y)
        return;

    float4 gbuffer_position = tex[thread_id.xy];
    float  hit_distance     = gbuffer_position.w;

    // skip pixels the tracer marked as ibl owned (get_rt_reflection_weight 0) before any texture/material work
    if (hit_distance < 0.0f)
    {
        tex_uav[thread_id.xy] = float4(0, 0, 0, 0);
        return;
    }

    float2 uv_source = (thread_id.xy + 0.5f) / resolution_out;
    float  source_coat;
    float  source_roughness = get_rt_reflection_roughness(uv_source, source_coat);
    float  source_alpha     = min(ggx_alpha_from_roughness(source_roughness), 0.6f);
    float  rough_reflection = smoothstep(0.03f, 0.45f, source_alpha);
    float3 source_pos       = get_position(uv_source);
    float  mip_count        = pass_float(pass_reflections_shade::mip_count);
    uint   light_count      = pass_uint(pass_reflections_shade::light_count);
    uint   seed             = (thread_id.x * 1973u + thread_id.y * 9277u + buffer_frame.frame * 26699u) | 1u;

    // miss returns sky color, prefiltered by source surface roughness so smooth metals get sharp sky
    if (hit_distance == 0.0f)
    {
        float3 ray_dir         = gbuffer_position.xyz;
        float  sky_mip         = source_roughness * source_roughness * (mip_count - 1.0f);
        float3 sky_color       = tex4.SampleLevel(GET_SAMPLER(sampler_trilinear_clamp), direction_sphere_uv(ray_dir), sky_mip).rgb;
        sky_color              = reflections_fog_sky(sky_color, source_pos, ray_dir);
        tex_uav[thread_id.xy]  = validate_output(float4(sky_color, 1000.0f));
        return;
    }

    float4 gbuffer_normal   = tex2[thread_id.xy];
    float4 gbuffer_albedo   = tex3[thread_id.xy];
    float4 gbuffer_emission = tex5[thread_id.xy];

    float3 position       = gbuffer_position.xyz;
    float3 normal         = gbuffer_normal.xyz;
    uint   material_index = unpack_material_index(gbuffer_normal.w);
    float3 albedo         = gbuffer_albedo.rgb;
    float  roughness      = gbuffer_albedo.a;
    float  metallic       = gbuffer_emission.a;

    // hit, the material layers and the view direction back toward the source pixel
    MaterialParameters mat = material_parameters[material_index];
    float3 F0              = lerp(0.04f, albedo, metallic);
    float3 view_dir        = source_pos - position;
    float  view_dist       = length(view_dir);
    view_dir               = view_dist > 0.0001f ? view_dir / view_dist : -normal;
    float  n_dot_v         = saturate(dot(normal, view_dir));
    float  n_dot_v_brdf    = max(n_dot_v, lerp(0.001f, 0.04f, rough_reflection));
    float  edge            = 1.0f - n_dot_v;
    float  edge5           = edge * edge * edge * edge * edge;
    float  pearl_blend     = saturate(mat.pearl_strength * edge5);
    float  coat_blend      = saturate(mat.coat_tint.a * lerp(0.35f, 1.0f, edge5));
    albedo                 = lerp(albedo, mat.pearl_color.rgb, pearl_blend);
    albedo                *= lerp(float3(1.0f, 1.0f, 1.0f), mat.coat_tint.rgb, coat_blend);
    F0                     = lerp(F0, mat.pearl_color.rgb, pearl_blend * 0.35f);
    float  clearcoat       = saturate(mat.clearcoat);
    float  coat_roughness  = saturate(mat.clearcoat_roughness);
    float3 coat_tint       = lerp(float3(1.0f, 1.0f, 1.0f), mat.coat_tint.rgb, saturate(mat.coat_tint.a));
    float  sheen           = saturate(mat.sheen);
    float3 multiscatter    = compute_multiscatter_energy(F0, n_dot_v_brdf, roughness);

    // analytic lights, directional and unshadowed lights are exact, shadowed local lights keep the two
    // strongest (by unshadowed contribution) exact and stream the rest through one weighted reservoir
    float3 out_direct   = 0.0f;
    LightCandidate top0 = (LightCandidate)0;
    LightCandidate top1 = (LightCandidate)0;
    LightCandidate sampled = (LightCandidate)0;
    float sampled_total = 0.0f;
    Surface hit_surface  = (Surface)0;
    hit_surface.position = position;
    hit_surface.normal   = normal;
    for (uint i = 0; i < light_count; i++)
    {
        LightParameters light_p = light_parameters[i];

        bool is_directional = (light_p.flags & uint(1U << 0)) != 0;
        bool is_area        = (light_p.flags & uint(1U << 6)) != 0;
        bool has_shadows    = (light_p.flags & uint(1U << 3)) != 0;

        // Reject lights with zero contribution before constructing area bases,
        // spot trigonometry and attenuation. Area range is measured from the
        // rectangle, so expand the cheap sphere by its half diagonal.
        if (pass_bool(pass_reflections_shade::light_culling))
        {
            float3 center_to_light = is_directional ? -light_p.direction : light_p.position - position;
            if (dot(normal, center_to_light) <= 0.0f)
                continue;
            if (!is_directional)
            {
                float extent = max(light_p.range, 0.0f);
                if (is_area) extent += 0.5f * length(float2(light_p.area_width, light_p.area_height));
                extent += 0.001f; // keep the boundary conservative after float roundoff
                if (light_p.range <= 0.0f || dot(center_to_light, center_to_light) > extent * extent)
                    continue;
            }
        }

        // Share range, cone and area attenuation with primary surface lighting.
        Light hit_light;
        hit_light.Build(i, hit_surface);
        float3 to_light      = -hit_light.to_pixel;
        float attenuation    = hit_light.attenuation;
        float light_distance = hit_light.distance_to_pixel;

        float n_dot_l = saturate(dot(normal, to_light));
        if (n_dot_l <= 0.0f || attenuation <= 0.0f)
            continue;

        float3 h_unorm = to_light + view_dir;
        float  h_len2  = dot(h_unorm, h_unorm);
        if (h_len2 <= 1e-6f)
            continue;
        float3 h       = h_unorm * rsqrt(h_len2);
        float  n_dot_h = saturate(dot(normal, h));
        float  l_dot_h = saturate(dot(to_light, h));

        // the source lobe and the area of the light widen the highlight the reflection resolves
        float area_alpha = 0.0f;
        if (is_area)
        {
            float area_size = max(light_p.area_width, light_p.area_height) * 0.5f;
            area_alpha      = saturate(area_size / (2.0f * max(light_distance, 0.01f)));
        }
        float filter2        = source_alpha * source_alpha * 0.35f + area_alpha * area_alpha;
        float hit_alpha      = ggx_alpha_from_roughness(roughness);
        float filtered_alpha = saturate(sqrt(hit_alpha * hit_alpha + filter2));
        float alpha2         = filtered_alpha * filtered_alpha;

        float3 radiance = light_p.color.rgb * light_p.intensity * attenuation * n_dot_l;

        // base, the same layering light.hlsl runs, coat and sheen take their energy from what is below
        float3 F             = F_Schlick(F0, get_f90(), l_dot_h);
        float3 specular      = D_GGX(n_dot_h, alpha2) * V_SmithGGX(n_dot_v_brdf, n_dot_l, alpha2) * F * multiscatter;
        float3 diffuse       = albedo * INV_PI * (1.0f - metallic) * (1.0f - F);
        if (clearcoat > 0.0f)
        {
            float coat_alpha2 = saturate(coat_roughness * coat_roughness + filter2);
            float coat_f      = F_Schlick(0.04f, 1.0f, l_dot_h) * clearcoat;
            specular          = specular * (1.0f - coat_f) + D_GGX(n_dot_h, coat_alpha2) * V_Kelemen(l_dot_h) * coat_f * coat_tint;
            diffuse          *= 1.0f - coat_f;
        }
        if (sheen > 0.0f)
        {
            float3 sheen_color = albedo * sheen;
            specular          += D_Charlie(max(roughness, 0.3f), n_dot_h) * V_Neubelt(n_dot_v_brdf, n_dot_l) * lerp(sheen_color * 0.2f, sheen_color, edge5);
            diffuse           *= 1.0f - sheen * 0.5f;
        }
        float specular_knee = lerp(4096.0f, 32.0f, rough_reflection);
        specular            = reflections_compress_luminance(specular * radiance, specular_knee, specular_knee * 0.5f);

        LightCandidate candidate;
        candidate.radiance = diffuse * radiance + specular;
        candidate.weight   = luminance(candidate.radiance);
        candidate.index    = i;
        if (candidate.weight <= 0.0f)
            continue;

        if (!has_shadows)
        {
            out_direct += candidate.radiance;
        }
        else if (is_directional)
        {
            out_direct += candidate.radiance * ray_trace_shadow(light_p, position, normal, float2(thread_id.xy));
        }
        else if (candidate.weight > top1.weight)
        {
            LightCandidate displaced = top1;
            if (candidate.weight > top0.weight)
            {
                top1 = top0;
                top0 = candidate;
            }
            else
            {
                top1 = candidate;
            }
            reflections_stream_light(sampled, sampled_total, displaced, seed);
        }
        else
        {
            reflections_stream_light(sampled, sampled_total, candidate, seed);
        }
    }
    if (top0.weight > 0.0f)
    {
        out_direct += top0.radiance * ray_trace_shadow(light_parameters[top0.index], position, normal, float2(thread_id.xy));
    }
    if (top1.weight > 0.0f)
    {
        out_direct += top1.radiance * ray_trace_shadow(light_parameters[top1.index], position, normal, float2(thread_id.xy));
    }
    if (sampled_total > 0.0f)
    {
        out_direct += sampled.radiance * (sampled_total / sampled.weight) * ray_trace_shadow(light_parameters[sampled.index], position, normal, float2(thread_id.xy));
    }

    // diffuse bounce from the radiance cache, the dim sky fallback gated by one visibility ray covers
    // cells that have not converged yet
    float3 cache_jitter = float3(reflections_random(seed), reflections_random(seed), reflections_random(seed)) - 0.5f;
    float  cache_confidence;
    float3 cached         = radiance_cache_lookup(position, normal, cache_jitter, cache_confidence);
    float3 reflect_dir    = reflect(-view_dir, normal);
    float  sky_visibility = -1.0f;
    float3 indirect       = cached;
    if (cache_confidence < 1.0f)
    {
        float3 sky_vis_dir = normalize(lerp(normal, reflect_dir, metallic));
        sky_visibility     = ray_trace_sky_visibility(position, normal, sky_vis_dir);
        indirect           = lerp(ray_sky_ambient(tex4, mip_count, normal) * sky_visibility, cached, cache_confidence);
    }
    float3 diffuse_energy = (1.0f - metallic) * (1.0f - F_Schlick(F0, get_f90(), n_dot_v)) * (1.0f - F_Schlick(0.04f, 1.0f, n_dot_v) * clearcoat);
    float3 ibl_diffuse    = albedo * indirect * diffuse_energy;

    // feed the cache, one cosine distributed bounce from the hit, its far end shaded with the sun, one
    // local light and whatever the cache already knows there, so bounces compound over frames
    uint update_slot = ((thread_id.x & 1u) + (thread_id.y & 1u) * 2u + buffer_frame.frame) % k_cache_update_divisor;
    if (update_slot == 0u)
    {
        float2 xi = float2(reflections_random(seed), reflections_random(seed));
        float3 t, b;
        find_best_axis_vectors(normal, t, b);
        float  radius    = sqrt(xi.x);
        float  phi       = xi.y * PI2;
        float3 bounce    = normalize(t * (radius * cos(phi)) + b * (radius * sin(phi)) + normal * sqrt(max(0.0f, 1.0f - xi.x)));

        RayDesc ray;
        ray.Origin    = position + normal * 0.01f;
        ray.Direction = bounce;
        ray.TMin      = 0.001f;
        ray.TMax      = 500.0f;
        RaySurface far_end = ray_surface_trace(ray, float2(0.0f, 0.5f));
        float3 incoming    = far_end.hit
            ? ray_surface_shade_secondary(far_end, -bounce, tex4, mip_count, float2(thread_id.xy), light_count)
            : tex4.SampleLevel(GET_SAMPLER(sampler_trilinear_clamp), direction_sphere_uv(bounce), mip_count * 0.5f).rgb;
        float3 update_jitter = float3(reflections_random(seed), reflections_random(seed), reflections_random(seed)) - 0.5f;
        radiance_cache_update(position, normal, update_jitter, incoming);
    }

    // specular environment at the hit, without it metal and glossy black hits shade to a silhouette,
    // smooth hits and coats trace it, a reflection inside the reflection, rougher ones read the sky
    float  env_roughness = saturate(max(roughness, source_roughness * rough_reflection));
    float  traced_share  = 1.0f - smoothstep(k_second_bounce_start, k_second_bounce_end, env_roughness);
    bool   trace_bounce  = traced_share > 0.0f || clearcoat > 0.0f;
    float3 bounce_light  = 0.0f;
    if (trace_bounce)
    {
        RayDesc ray;
        ray.Origin    = position + normal * 0.01f;
        ray.Direction = reflect_dir;
        ray.TMin      = 0.001f;
        ray.TMax      = 1000.0f;
        float  pixel_angle = 2.0f * tan(buffer_frame.camera_fov * 0.5f) / get_render_resolution_active().x;
        float2 cone        = float2((length(source_pos - get_camera_position()) + view_dist) * pixel_angle, pixel_angle + source_alpha + ggx_alpha_from_roughness(roughness));
        RaySurface second  = ray_surface_trace(ray, cone);
        bounce_light       = second.hit
            ? ray_surface_shade_secondary(second, -reflect_dir, tex4, mip_count, float2(thread_id.xy), light_count)
            : tex4.SampleLevel(GET_SAMPLER(sampler_trilinear_clamp), direction_sphere_uv(reflect_dir), env_roughness * env_roughness * (mip_count - 1.0f)).rgb;
    }

    float3 sky_specular = 0.0f;
    if (traced_share < 1.0f)
    {
        if (sky_visibility < 0.0f)
        {
            sky_visibility = ray_trace_sky_visibility(position, normal, reflect_dir);
        }
        float spec_mip = env_roughness * env_roughness * (mip_count - 1.0f);
        sky_specular   = tex4.SampleLevel(GET_SAMPLER(sampler_trilinear_clamp), direction_sphere_uv(reflect_dir), spec_mip).rgb * sky_visibility;
    }
    float2 env_brdf     = ray_env_brdf(env_roughness, n_dot_v_brdf);
    float3 env_base     = lerp(sky_specular, bounce_light, traced_share);
    float3 ibl_specular = env_base * (F0 * env_brdf.x + env_brdf.y) * compute_multiscatter_energy_split_sum(F0, env_brdf);
    if (clearcoat > 0.0f)
    {
        float2 coat_brdf = ray_env_brdf(coat_roughness, n_dot_v_brdf);
        float  coat_f    = (0.04f * coat_brdf.x + coat_brdf.y) * clearcoat;
        ibl_specular     = ibl_specular * (1.0f - coat_f) + bounce_light * coat_f * coat_tint;
    }

    // sampled at the actual hit uv, in the same units as primary emission
    float3 emission = gbuffer_emission.rgb;
    // rough lobes undersample small bright emitters, soft compress before the denoiser sees them
    float emission_knee = lerp(FLT_MAX_16U, 48.0f, rough_reflection);
    emission = reflections_compress_luminance(emission, emission_knee, emission_knee * 0.5f);

    float3 final_color = out_direct + ibl_diffuse + ibl_specular + emission;
    final_color        = reflections_fog(final_color, source_pos, -view_dir, view_dist);
    float  final_knee  = lerp(FLT_MAX_16U, 96.0f, rough_reflection);
    final_color        = reflections_compress_luminance(final_color, final_knee, final_knee * 0.5f);

    // a stores hit distance for nrd reblur specular
    tex_uav[thread_id.xy] = validate_output(float4(final_color, max(hit_distance, 0.0f)));
}
