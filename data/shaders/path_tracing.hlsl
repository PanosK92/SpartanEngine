/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

// Camera rays and all subsequent surface bounces are traced against the scene.
// No G-buffer, screen-space lighting, reservoir reuse or temporal reconstruction.
#include "common.hlsl"
#include "brdf.hlsl"
#include "common_ray_surface.hlsl"
#include "restir_reservoir.hlsl"
#include "rt_visibility.hlsl"

// This pass always binds an RGBA32F history, including for typed UAV loads.
[[vk::image_format("rgba32f")]] RWTexture2D<float4> pt_history : register(u0);

RaySurface pt_trace(RayDesc ray, float2 cone)
{
#ifdef PATH_TRACING_SKY_ONLY
    return (RaySurface)0;
#else
    RayQuery<RAY_FLAG_SKIP_CLOSEST_HIT_SHADER> query;
    query.TraceRayInline(tlas, RAY_FLAG_NONE, 0xFF, ray);
    while (query.Proceed())
    {
        if (query.CandidateType() != CANDIDATE_NON_OPAQUE_TRIANGLE)
            continue;
        // only cutouts discard by texture alpha, opaque materials ignore it and blending is resolved per hit
        MaterialParameters mat = material_parameters[geometry_infos[query.CandidateInstanceIndex()].material_index];
        if (!mat.is_alpha_tested() || rt_candidate_opaque(query.CandidateInstanceIndex(), query.CandidatePrimitiveIndex(),
                query.CandidateTriangleBarycentrics(), ray.Origin + ray.Direction * query.CandidateTriangleRayT()))
            query.CommitNonOpaqueTriangleHit();
    }
    if (query.CommittedStatus() != COMMITTED_TRIANGLE_HIT)
        return (RaySurface)0;
    float2 b = query.CommittedTriangleBarycentrics();
    return ray_surface_reconstruct(query.CommittedRayT(), query.CommittedInstanceIndex(),
        query.CommittedPrimitiveIndex(), float3(1.0f - b.x - b.y, b), ray, cone);
#endif
}

bool pt_is_blended(MaterialParameters mat)
{
    return mat.alpha_blend > 0.5f && !mat.is_alpha_tested();
}

bool pt_is_dielectric(MaterialParameters mat)
{
    return mat.color.a < 1.0f && mat.alpha_blend <= 0.5f && !mat.is_alpha_tested();
}

// the engine authors beer lambert density in units of 1/50 metre, a thin shell tints once over its thickness
float3 pt_transmittance(float3 color, float absorption, float distance)
{
    float exponent = max(absorption, 0.0f) * max(distance, 0.0f) * 50.0f;
    return exponent > 0.0f ? pow(saturate(color), exponent) : 1.0f;
}

// Summing internal reflections at both faces of a thin dielectric sheet.
float pt_sheet_reflectance(float cos_i, float ior)
{
    float cos_t;
    float f = restir_dielectric_fresnel(1.0f / ior, cos_i, cos_t);
    return 2.0f * f / (1.0f + f);
}

// A resolved volume supplies its own chord length. An authored optical shell can be
// much thinner than the mesh used to display it (for example a coated glass sample).
float3 pt_medium_transmittance(RestirMediumState medium, float distance, float cosine)
{
    uint index = restir_medium_material(medium.slots.x);
    if (index == RESTIR_NO_MEDIUM) return 1.0f;
    MaterialParameters mat = material_parameters[index];
    if (mat.thickness > 0.0f && (mat.flags & (1u << 13u)) == 0)
        distance = min(distance, mat.thickness / max(abs(cosine), 1e-4f));
    return restir_medium_transmittance(medium, distance);
}

// A frosted sheet scatters about the forward direction. Folding its GGX reflection
// lobe across the tangent plane preserves solid angle and avoids entering a fictitious volume.
float3 pt_sheet_evaluate(RaySurface surface, MaterialParameters mat, float3 v, float3 l)
{
    float alpha = restir_dielectric_alpha(surface.roughness);
    bool transmitted = dot(surface.geometric_normal, l) < 0.0f;
    if (transmitted) l = reflect(l, surface.normal);
    float nv = dot(surface.normal, v);
    float nl = dot(surface.normal, l);
    if (alpha == 0.0f || min(nv, nl) <= 0.0f) return 0.0f;
    float3 h = normalize(v + l);
    float a2 = alpha * alpha;
    float d = restir_dielectric_ndf(world_to_local(h, surface.normal), alpha);
    float f = pt_sheet_reflectance(saturate(dot(v, h)), max(mat.ior, 1.0f));
    float3 factor = transmitted ? (1.0f - f) * pt_transmittance(surface.albedo, mat.absorption, mat.thickness) : f;
    return factor * d * restir_v_smithggx_cos(nv, nl, a2);
}

PathSurface pt_bsdf_surface(RaySurface hit, MaterialParameters mat)
{
    PathSurface surface = (PathSurface)0;
    surface.hit_normal = hit.normal;
    surface.geometric_normal = hit.geometric_normal;
    surface.material_index = hit.material_index;
    surface.albedo = hit.albedo;
    surface.roughness = hit.roughness;
    surface.metallic = hit.metallic;
    surface.layered = restir_layered(mat);
    surface.front_facing = hit.front_face;
    surface.ior = max(mat.ior, 1.0f);
    return surface;
}

// A straight shadow connection can cross alpha coverage and thin panes. Solid glass
// changes direction at the boundary and is handled by sampling the dielectric BSDF.
float3 pt_visibility(float3 origin, float3 direction, float distance)
{
    float3 transmittance = 1.0f;
    float  remaining     = max(distance - compute_ray_offset(origin), 0.001f);
    for (uint layer = 0; layer < 8; layer++)
    {
        RayDesc ray;
        ray.Origin    = origin;
        ray.Direction = direction;
        ray.TMin      = 0.001f;
        ray.TMax      = remaining;
        RaySurface surface = pt_trace(ray, float2(0, 0));
        if (!surface.hit)
            return transmittance;

        MaterialParameters mat = material_parameters[surface.material_index];
        if (pt_is_blended(mat))
        {
            transmittance *= 1.0f - surface.alpha;
        }
        else if (pt_is_dielectric(mat) && mat.is_thin_walled())
        {
            float reflectance = pt_sheet_reflectance(saturate(dot(-direction, surface.normal)), max(mat.ior, 1.0f));
            transmittance *= (1.0f - reflectance) * pt_transmittance(surface.albedo, mat.absorption, mat.thickness);
        }
        else
        {
            return 0.0f;
        }

        if (all(transmittance < 1e-4f))
            return 0.0f;
        float offset = compute_ray_offset(surface.position);
        origin       = surface.position - surface.geometric_normal * offset;
        remaining   -= surface.hit_distance + offset;
        if (remaining <= 0.001f)
            return transmittance;
    }
    return 0.0f;
}

float3 pt_direct(RaySurface surface, PathSurface bsdf, float3 view_dir, inout uint seed, bool dielectric, float eta, RestirMediumState medium)
{
    float3 shading_pos = surface.position;
    uint light_count = (uint)buffer_frame.restir_pt_light_count;
    if (light_count == 0)
        return 0.0f;
    // Select one analytic light and divide by its discrete selection probability.
    uint light_idx = min(uint(random_float(seed) * light_count), light_count - 1);
    LightParameters light = light_parameters[light_idx];

    if (light.intensity <= 0.0f)
        return 0.0f;

    uint light_flags    = light.flags;
    bool is_directional = (light_flags & (1u << 0)) != 0;
    bool is_point       = (light_flags & (1u << 1)) != 0;
    bool is_spot        = (light_flags & (1u << 2)) != 0;
    bool is_area        = (light_flags & (1u << 6)) != 0;

    float3 light_color = light.color.rgb;
    float3 light_dir;
    float  light_dist;
    float  light_pdf    = 1.0f;
    float  attenuation  = 1.0f;

    if (is_directional)
    {
        light_dir  = -light.direction;
        light_dist = 1e15f;
        light_pdf  = 1.0f;
    }
    else if (is_area && light.area_width > 0.0f && light.area_height > 0.0f)
    {
        float3 light_normal = light.direction;
        // Use the authored roll, matching the primary rectangle and shadows.
        float3 light_right = normalize(light.direction_right);
        float3 light_up = normalize(cross(light_normal, light_right));

        // urena 2013 spherical rectangle solid angle sampling
        float3 ex          = light_right * light.area_width;
        float3 ey          = light_up    * light.area_height;
        float3 rect_origin = light.position - 0.5f * ex - 0.5f * ey;

        float2 xi = random_float2(seed);
        float3 light_sample_pos;
        float  solid_angle;
        sample_spherical_rectangle(shading_pos, rect_origin, ex, ey, xi, light_sample_pos, solid_angle);

        if (solid_angle < MIN_AREA_LIGHT_SOLID_ANGLE)
            return 0.0f;

        float3 to_light = light_sample_pos - shading_pos;
        light_dist      = length(to_light);
        if (light_dist < 1e-3f)
            return 0.0f;
        light_dir       = to_light / light_dist;

        float cos_light = dot(-light_dir, light_normal);
        if (cos_light <= 0.0f)
            return 0.0f;

        light_pdf   = 1.0f / solid_angle;
        // the 1/d^2 term is baked into the solid angle pdf, only the artistic range window applies
        attenuation = restir_range_window(light_dist, light.range);
    }
    else if (is_point || is_spot)
    {
        float3 to_light = light.position - shading_pos;
        light_dist      = length(to_light);
        if (light_dist <= 0.0f) return 0.0f;
        light_dir       = to_light / light_dist;
        light_pdf       = 1.0f;

        if (light.range <= 0.0f || light_dist >= light.range)
        {
            return 0.0f;
        }

        attenuation = restir_range_window(light_dist, light.range) / (light_dist * light_dist + 0.0001f);

        if (is_spot)
        {
            attenuation *= light_spot_factor(light_flags, -light_dir, light.direction, light.direction_right, light.angle);
        }
    }
    else
    {
        return 0.0f;
    }

    float pdf;
    MaterialParameters mat = material_parameters[bsdf.material_index];
    bool sheet = dielectric && mat.is_thin_walled();
    float3 brdf = sheet ? pt_sheet_evaluate(surface, mat, view_dir, light_dir) :
        restir_surface_evaluate(bsdf, view_dir, light_dir, pdf, 1.0f, dielectric, eta);
    float3 incoming = light_color * light.intensity * attenuation * (float(light_count) / light_pdf);
    float3 scattered = 0.0f;
    if (!dielectric && is_subsurface_diffusion_enabled() && mat.has_subsurface_profile())
    {
        float entry = saturate(0.3f - dot(surface.normal, light_dir));
        if (entry > 0.0f)
        {
            float safety = is_area ? min(min(light.area_width, light.area_height) * 0.5f, 0.08f) + 0.01f : 0.01f;
            scattered = incoming * rt_subsurface_transmittance(shading_pos, surface.geometric_normal, light_dir,
                light_dist, safety, mat.subsurface_radius) * surface.albedo * (1.0f - surface.metallic) *
                (0.96f * INV_PI * entry * saturate(mat.subsurface_scattering));
        }
    }
    if (!any(brdf > 0.0f)) return scattered;
    bool transmission = dot(surface.geometric_normal, light_dir) < 0.0f;
    if (dielectric && !sheet && transmission)
        restir_medium_cross(medium, bsdf.material_index, 0u, bsdf.front_facing);
    float side = transmission ? -1.0f : 1.0f;
    float3 origin = shading_pos + side * surface.geometric_normal * compute_ray_offset(shading_pos);
    float3 visibility = pt_visibility(origin, light_dir, light_dist) * pt_medium_transmittance(medium, light_dist, dot(surface.geometric_normal, light_dir));
    return scattered + brdf * visibility * incoming;
}

[numthreads(8, 8, 1)]
void main_cs(uint3 tid : SV_DispatchThreadID)
{
    uint2 size;
    pt_history.GetDimensions(size.x, size.y);
    uint stride = pass_uint(pass_path_tracing::stride);
    uint phase = pass_uint(pass_path_tracing::phase);
    uint2 block = tid.xy * stride;
    uint2 pixel = block + uint2(phase % stride, phase / stride);
    if (any(pixel >= size)) return;
    uint samples = pass_uint(pass_path_tracing::samples);
    uint seed = create_seed_for_pass(pixel, samples, 0u);
    float2 uv = (float2(pixel) + random_float2(seed)) / float2(size);
    RayDesc ray;
    // Reverse-Z near/far points also generate parallel rays for orthographic cameras.
    ray.Origin = get_position(1.0f, uv);
    ray.Direction = normalize(get_position(0.0001f, uv) - ray.Origin);
    ray.TMin = 0.001f;
    ray.TMax = 1e15f;
    float2 cone = float2(0.0f, 2.0f * tan(buffer_frame.camera_fov * 0.5f) / size.y);
    float3 radiance = 0.0f;
    float3 throughput = 1.0f;
    uint max_bounces = pass_uint(pass_path_tracing::bounces);
    RestirMediumState medium = (RestirMediumState)0;
    uint bounce = 0;
    uint transparent_hits = 0;
    while (bounce <= max_bounces)
    {
        RaySurface surface = pt_trace(ray, cone);
        if (!surface.hit)
        {
            // The camera sees the visible sun disc. Its lighting at scattering vertices is sampled analytically.
            float3 sky = bounce == 0 ? tex3.SampleLevel(GET_SAMPLER(sampler_trilinear_clamp), direction_sphere_uv(ray.Direction), 0).rgb
                                     : sample_sky(ray.Direction);
            radiance += throughput * sky;
            break;
        }
        MaterialParameters mat = material_parameters[surface.material_index];
        bool is_dielectric = pt_is_dielectric(mat);

        throughput *= pt_medium_transmittance(medium, surface.hit_distance, dot(surface.geometric_normal, ray.Direction));
        if (pt_is_blended(mat) && random_float(seed) >= surface.alpha)
        {
            if (++transparent_hits > 32) break;
            cone = ray_cone_propagate(cone, surface.hit_distance);
            ray.Origin = surface.position - surface.geometric_normal * compute_ray_offset(surface.position);
            continue;
        }
        radiance += throughput * surface.emission;
        PathSurface bsdf = pt_bsdf_surface(surface, mat);
        float3 direction;
        if (is_dielectric && mat.is_thin_walled())
        {
            float alpha = restir_dielectric_alpha(surface.roughness);
            if (alpha > 0.0f)
                radiance += throughput * pt_direct(surface, bsdf, -ray.Direction, seed, true, 1.0f, medium);
            if (bounce == max_bounces) break;
            float3 wi = world_to_local(-ray.Direction, surface.normal);
            if (wi.z <= 0.0f) break;
            float3 h = alpha > 0.0f ? sample_ggx_vndf(wi, random_float2(seed), alpha) : float3(0, 0, 1);
            float3 wo = reflect(-wi, h);
            if (wo.z <= 0.0f) break;
            if (alpha > 0.0f)
            {
                float a2 = alpha * alpha;
                float gi = sqrt(a2 + (1.0f - a2) * wi.z * wi.z);
                float go = sqrt(a2 + (1.0f - a2) * wo.z * wo.z);
                throughput *= wo.z * (wi.z + gi) / (wo.z * gi + wi.z * go);
            }
            bool transmit = random_float(seed) >= pt_sheet_reflectance(saturate(dot(wi, h)), bsdf.ior);
            if (transmit)
            {
                wo.z = -wo.z;
                throughput *= pt_transmittance(surface.albedo, mat.absorption, mat.thickness);
            }
            direction = local_to_world(wo, surface.normal);
            if ((dot(direction, surface.geometric_normal) < 0.0f) != transmit) break;
        }
        else if (is_dielectric)
        {
            float eta = restir_surface_eta(bsdf, medium);
            radiance += throughput * pt_direct(surface, bsdf, -ray.Direction, seed, true, eta, medium);
            if (bounce == max_bounces) break;
            RestirSurfaceSample scatter = restir_surface_sample(bsdf, -ray.Direction, random_float3(seed), 1.0f, true, eta);
            if (!scatter.valid) break;
            direction = scatter.direction;
            throughput *= scatter.weight;
            restir_surface_cross_medium(medium, bsdf, scatter);
        }
        else
        {
            radiance += throughput * pt_direct(surface, bsdf, -ray.Direction, seed, false, 1.0f, medium);
            if (bounce == max_bounces) break;
            float pdf;
            direction = sample_reflection_brdf(surface.albedo, surface.roughness, surface.metallic,
                surface.normal, surface.geometric_normal, -ray.Direction, random_float2(seed), pdf, 1.0f);
            if (!(pdf > 0.0f)) break;
            float eval_pdf;
            throughput *= restir_surface_evaluate(bsdf, -ray.Direction, direction, eval_pdf, 1.0f) / pdf;
        }
        if (any(isnan(throughput)) || any(isinf(throughput))) break;
        if (bounce >= 3)
        {
            float survival = clamp(max(throughput.x, max(throughput.y, throughput.z)), 0.05f, 0.95f);
            if (random_float(seed) >= survival) break;
            throughput /= survival;
        }
        cone = ray_cone_propagate(cone, surface.hit_distance);
        cone.y = max(cone.y, surface.roughness * 0.1f);
        float side = dot(direction, surface.geometric_normal) >= 0.0f ? 1.0f : -1.0f;
        ray.Origin = surface.position + side * surface.geometric_normal * compute_ray_offset(surface.position);
        ray.Direction = direction;
        ++bounce;
    }
    if (any(isnan(radiance)) || any(isinf(radiance))) radiance = 0.0f;
    if (samples == 0 && phase == 0)
    {
        // Navigation preview. Each following phase overwrites its own pixel before any averaging.
        for (uint y = block.y; y < min(block.y + stride, size.y); ++y)
            for (uint x = block.x; x < min(block.x + stride, size.x); ++x)
                pt_history[uint2(x, y)] = float4(radiance, 1.0f);
    }
    else
    {
        if (samples > 0)
            radiance = lerp(pt_history[pixel].rgb, radiance, 1.0f / (float(samples) + 1.0f));
        pt_history[pixel] = float4(radiance, 1.0f);
    }
}
