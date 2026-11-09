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

float pt_dielectric_fresnel(float cos_i, float eta, out float sin_t2)
{
    sin_t2      = eta * eta * (1.0f - cos_i * cos_i);
    float cos_t = sqrt(max(0.0f, 1.0f - sin_t2));
    float rs    = (eta * cos_i - cos_t) / max(eta * cos_i + cos_t, 1e-6f);
    float rp    = (cos_i - eta * cos_t) / max(cos_i + eta * cos_t, 1e-6f);
    return sin_t2 >= 1.0f ? 1.0f : 0.5f * (rs * rs + rp * rp);
}

// the gltf sheen and clear coat layers over the base lobes, the same lobes as the raster brdf, f times cos,
// the coat lobe itself is left out when the caller sampled the coat on its own
float3 pt_layers_evaluate(MaterialParameters mat, RaySurface surface, float3 v, float3 l, float3 base, bool coat_lobe)
{
    float3 n      = surface.normal;
    float  n_dot_l = dot(n, l);
    float  n_dot_v = dot(n, v);
    if (n_dot_l <= 0.0f || n_dot_v <= 0.0f)
        return base;

    float3 h       = normalize(v + l);
    float  n_dot_h = saturate(dot(n, h));
    float  v_dot_h = saturate(dot(v, h));
    float3 result  = base;

    float sheen_weight = max(mat.sheen_color.r, max(mat.sheen_color.g, mat.sheen_color.b));
    if (mat.sheen > 0.0f && sheen_weight > 0.0f)
    {
        float3 sheen = D_Charlie(max(mat.sheen_color.w, 0.07f), n_dot_h) * V_Neubelt(n_dot_v, n_dot_l) * mat.sheen_color.rgb;
        result       = result * (1.0f - sheen_weight * 0.5f) + sheen * n_dot_l;
    }

    if (mat.clearcoat > 0.0f)
    {
        result *= coat_transmission(mat.clearcoat, n_dot_v, n_dot_l);
        if (coat_lobe)
        {
            float a2 = max(mat.clearcoat_roughness * mat.clearcoat_roughness, 1e-4f);
            result  += D_GGX(n_dot_h, a2) * V_Kelemen(v_dot_h) * F_Schlick(0.04f, 1.0f, v_dot_h) * mat.clearcoat * n_dot_l;
        }
    }
    return result;
}

// light reaches the shading point through blended surfaces and glass, volumes are crossed without their bend,
// a delta sun is never found through refraction and glass would cast black shadows the raster path does not
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
        else if (pt_is_dielectric(mat))
        {
            bool thin = mat.is_thin_walled();
            float sin_t2;
            float fresnel  = pt_dielectric_fresnel(saturate(dot(-direction, surface.normal)), 1.0f / max(mat.ior, 1.0001f), sin_t2);
            transmittance *= 1.0f - fresnel;
            if (thin)
                transmittance *= pt_transmittance(surface.albedo, mat.absorption, mat.thickness);
            else if (!surface.front_face)
                transmittance *= pt_transmittance(mat.color.rgb, mat.absorption, mat.thickness);
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

float3 pt_direct(RaySurface surface, float3 view_dir, inout uint seed)
{
    float3 result = 0.0f;
    float3 shading_pos = surface.position;
    float3 shading_normal = surface.normal;
    float3 ray_origin_light = shading_pos + surface.geometric_normal * compute_ray_offset(shading_pos);
    uint light_count = (uint)buffer_frame.restir_pt_light_count;
    // analytical lights
    for (uint light_idx = 0; light_idx < light_count; light_idx++)
    {
        LightParameters light = light_parameters[light_idx];

        if (light.intensity <= 0.0f)
            continue;

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
            light_dist = 1000.0f;
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
                continue;

            float3 to_light = light_sample_pos - shading_pos;
            light_dist      = length(to_light);
            if (light_dist < 1e-3f)
                continue;
            light_dir       = to_light / light_dist;

            float cos_light = dot(-light_dir, light_normal);
            if (cos_light <= 0.0f)
                continue;

            light_pdf   = 1.0f / solid_angle;
            // the 1/d^2 term is baked into the solid angle pdf, only the artistic range window applies
            attenuation = restir_range_window(light_dist, light.range);
        }
        else if (is_point || is_spot)
        {
            float3 to_light = light.position - shading_pos;
            light_dist      = length(to_light);
            light_dir       = to_light / light_dist;
            light_pdf       = 1.0f;

            if (light.range <= 0.0f || light_dist >= light.range)
            {
                continue;
            }

            attenuation = restir_range_window(light_dist, light.range) / (light_dist * light_dist + 0.0001f);

            if (is_spot)
            {
                attenuation *= light_spot_factor(light_flags, -light_dir, light.direction, light.direction_right, light.angle);
            }
        }
        else
        {
            continue;
        }

        float n_dot_l = dot(shading_normal, light_dir);
        if (n_dot_l <= 0.0f)
            continue;

        float3 visibility = pt_visibility(ray_origin_light, light_dir, light_dist);
        if (all(visibility <= 0.0f))
            continue;

        // The selected incident radiance is independent of the incoming RC view.
        float  brdf_pdf;
        float3 brdf = evaluate_brdf(surface.albedo, surface.roughness, surface.metallic, shading_normal, view_dir, light_dir, brdf_pdf, 1.0f);
        brdf        = pt_layers_evaluate(material_parameters[surface.material_index], surface, view_dir, light_dir, brdf, true);

        // analytical lights are not in the bvh, single strategy mis weight of 1 is correct
        float mis_weight = 1.0f;

        float3 Li = light_color * light.intensity * attenuation * visibility;
        result += brdf * Li / max(light_pdf, 1e-6f);
    }

    return result;
}

[numthreads(8, 8, 1)]
void main_cs(uint3 tid : SV_DispatchThreadID)
{
    uint2 size;
    tex_uav.GetDimensions(size.x, size.y);
    if (any(tid.xy >= size)) return;
    uint samples = pass_uint(pass_path_tracing::samples);
    uint seed = pcg_hash(tid.x + tid.y * size.x) ^ pcg_hash(samples + 1u);
    float2 uv = (float2(tid.xy) + random_float2(seed)) / float2(size);
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
    for (uint bounce = 0; bounce <= max_bounces; ++bounce)
    {
        RaySurface surface = pt_trace(ray, cone);
        if (!surface.hit)
        {
            radiance += throughput * sample_sky(ray.Direction);
            break;
        }
        radiance += throughput * surface.emission;
        MaterialParameters mat = material_parameters[surface.material_index];
        bool is_dielectric = pt_is_dielectric(mat);

        float3 direction;
        if (pt_is_blended(mat) && random_float(seed) >= surface.alpha)
        {
            if (bounce == max_bounces) break;
            direction = ray.Direction;
        }
        // glass ignores metalness like the raster glass does, authored car glass carries high metalness
        else if (is_dielectric)
        {
            if (bounce == max_bounces) break;
            bool thin        = mat.is_thin_walled();
            float3 n         = surface.normal;
            float alpha      = restir_dielectric_alpha(surface.roughness);
            float3 h         = alpha > 0.0f ? local_to_world(sample_ggx_vndf(world_to_local(-ray.Direction, n), random_float2(seed), alpha), n) : n;
            float ior        = max(mat.ior, 1.0001f);
            float eta        = (thin || surface.front_face) ? 1.0f / ior : ior;
            float sin_t2;
            float fresnel    = pt_dielectric_fresnel(saturate(dot(-ray.Direction, h)), eta, sin_t2);
            float3 reflected = reflect(ray.Direction, h);
            if (random_float(seed) < fresnel)
            {
                direction = reflected;
                if (dot(direction, surface.geometric_normal) <= 0.0f) break;
            }
            else if (thin)
            {
                // both interfaces of a thin shell cancel the bend, roughness still spreads the ray
                direction   = reflect(reflected, surface.geometric_normal);
                throughput *= pt_transmittance(surface.albedo, mat.absorption, mat.thickness);
                if (dot(direction, surface.geometric_normal) >= 0.0f) break;
            }
            else
            {
                // no eta squared radiance scale, every emitter is outside so entry and exit cancel, and the scaled
                // down throughput inside dense glass would make russian roulette kill most paths
                direction = normalize(refract(ray.Direction, h, eta));
                if (dot(direction, surface.geometric_normal) >= 0.0f) break;
                // a volume tints once per crossing over its authored thickness, like the raster glass and the gltf reference,
                // the traced distance would ignore that the gltf thickness and the mesh size disagree
                if (!surface.front_face)
                    throughput *= pt_transmittance(mat.color.rgb, mat.absorption, mat.thickness);
            }
        }
        else
        {
            radiance += throughput * pt_direct(surface, -ray.Direction, seed);
            if (bounce == max_bounces) break;
            float3 v = -ray.Direction;
            // the coat is picked by its fresnel, the vndf sample of it carries a weight of about one
            float coat_pick = mat.clearcoat > 0.0f ? mat.clearcoat * F_Schlick(0.04f, 1.0f, saturate(dot(surface.normal, v))) : 0.0f;
            if (random_float(seed) < coat_pick)
            {
                float3 h  = local_to_world(sample_ggx_vndf(world_to_local(v, surface.normal), random_float2(seed), max(mat.clearcoat_roughness, 0.01f)), surface.normal);
                direction = reflect(ray.Direction, h);
                if (dot(direction, surface.geometric_normal) <= 0.0f || dot(direction, surface.normal) <= 0.0f) break;
            }
            else
            {
                float pdf;
                direction = sample_brdf(surface.albedo, surface.roughness, surface.metallic, surface.normal, v, random_float2(seed), pdf, 1.0f);
                if (pdf <= 1e-8f || dot(direction, surface.geometric_normal) <= 0.0f) break;
                float unused_pdf;
                float3 base = evaluate_brdf(surface.albedo, surface.roughness, surface.metallic, surface.normal, v, direction, unused_pdf, 1.0f);
                throughput *= pt_layers_evaluate(mat, surface, v, direction, base, false) / (pdf * (1.0f - coat_pick));
            }
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
    }
    if (any(isnan(radiance)) || any(isinf(radiance))) radiance = 0.0f;
    // Never read uninitialised history after a reset. FP32 running means retain dim indirect light.
    float3 average = radiance;
    if (samples > 0)
        average = lerp(tex[tid.xy].rgb, radiance, 1.0f / (float(samples) + 1.0f));
    tex_uav[tid.xy] = float4(average, 1.0f);
}
