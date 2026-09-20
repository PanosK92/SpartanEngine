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

#ifndef SPARTAN_RESTIR_PATH
#define SPARTAN_RESTIR_PATH
#include "restir_surface.hlsl"

PathSurface restir_trace_surface(RayDesc ray)
{
    RayQuery<RAY_FLAG_SKIP_CLOSEST_HIT_SHADER> query;
    query.TraceRayInline(tlas, RAY_FLAG_NONE, 0xFF, ray);
    while (query.Proceed())
    {
        if (query.CandidateType() == CANDIDATE_NON_OPAQUE_TRIANGLE)
            query.CommitNonOpaqueTriangleHit();
    }
    if (query.CommittedStatus() != COMMITTED_TRIANGLE_HIT)
        return (PathSurface)0;
    float2 bary = query.CommittedTriangleBarycentrics();
    uint packed_bary = f32tof16(bary.x) | (f32tof16(bary.y) << 16u);
    return reconstruct_path_surface(query.CommittedRayT(), query.CommittedInstanceIndex(),
        query.CommittedPrimitiveIndex(), packed_bary, ray);
}

// A path tree contributes several light-ending paths. RIS keeps exactly one
// endpoint and its length, outgoing direction and suffix. It never mixes a
// terminal emission term with a different continuation in the same descriptor.
struct RestirPathSelection
{
    float3 reference_radiance;
    float3 emission;
    float3 suffix;
    float3 direction;
    float outgoing_pdf;
    uint length;
    float weight_sum;
    float selected_target;
    float initial_weight;
    uint endpoint_light;
    uint endpoint_emtri_draw;
};

void restir_stream_path(inout RestirPathSelection result, float3 emission, float3 suffix,
    float3 direction, float outgoing_pdf, uint path_length, PathSurface rc, float3 rc_view,
    inout uint selection_seed, float candidate_weight, uint endpoint_light, uint endpoint_emtri_draw,
    bool replay, PathSample requested)
{
    float pdf;
    float3 f_rc = evaluate_brdf(rc.albedo, rc.roughness, rc.metallic, rc.hit_normal,
        rc_view, direction, pdf, 1.0f);
    float mis = restir_rc_endpoint_mis(path_length, endpoint_light, rc.hit_normal, direction, pdf);
    float target = target_scalar(emission + f_rc * suffix * mis);
    if (!(target > 0.0f) || isnan(target) || isinf(target))
        return;
    float weight = target * candidate_weight;
    result.weight_sum += weight;
    float u = random_float(selection_seed);
    bool selected = replay ? (path_length == requested.path_length && endpoint_light == requested.endpoint_light)
                           : (u * result.weight_sum < weight);
    if (selected)
    {
        result.emission = emission;
        result.suffix = suffix;
        result.direction = direction;
        result.outgoing_pdf = outgoing_pdf;
        result.length = path_length;
        result.selected_target = target;
        result.endpoint_light = endpoint_light;
        result.endpoint_emtri_draw = endpoint_emtri_draw;
    }
}

RestirPathSelection sample_path_tree_at_rc(PathSurface rc, float3 rc_view, inout uint seed,
    bool replay, PathSample requested)
{
    RestirPathSelection result = (RestirPathSelection)0;
    result.initial_weight = 1.0f;
    uint selection_seed = seed ^ 0x68bc21ebu;
    restir_stream_path(result, rc.emission, 0.0f, rc.hit_normal, 0.0f, 2u, rc, rc_view, selection_seed, 1.0f, 65535u, 0u, replay, requested);
    if (replay && requested.path_length == 2u)
        return result;

    RestirLightSample light = sample_direct_lighting_at_vertex(rc.hit_position, rc.hit_normal,
        rc.geometric_normal, rc_view, rc.albedo, rc.roughness, rc.metallic, 1.0f, seed,
        replay ? int(requested.endpoint_light) : -1, replay ? int(requested.endpoint_emtri_draw) : -1);
    result.reference_radiance = rc.emission + light.radiance_sum;
    float light_pdf;
    evaluate_brdf(rc.albedo, rc.roughness, rc.metallic, rc.hit_normal, rc_view,
        light.direction, light_pdf, 1.0f);
    restir_stream_path(result, 0.0f, light.incident, light.direction, light_pdf,
        3u, rc, rc_view, selection_seed, light.initial_weight, light.light_index, light.emtri_draw, replay, requested);
    if (replay && requested.path_length == 3u && requested.endpoint_light != 65535u)
        return result;

    PathSurface cur = rc;
    float3 view = rc_view;
    float3 throughput = 1.0f;
    float3 first_direction = 0.0f;
    float first_pdf = 0.0f;
    float rr_weight = 1.0f;
    // Vertex count includes the primary surface and the light endpoint.
    for (uint depth = 3u; depth <= get_restir_max_path_length(); ++depth)
    {
        if (depth > 3u + RESTIR_RR_START)
        {
            float rr = random_float(seed);
            if (!replay && rr >= RESTIR_RR_CONTINUATION)
                break;
            if (!replay)
                rr_weight /= RESTIR_RR_CONTINUATION;
        }
        float pdf;
        float3 direction = sample_brdf(cur.albedo, cur.roughness, cur.metallic,
            cur.hit_normal, view, random_float2(seed), pdf, 1.0f);
        if (pdf < RESTIR_MIN_PDF || dot(direction, cur.hit_normal) <= 0.0f || any(isnan(direction)))
            break;
        if (depth == 3u)
        {
            first_direction = direction;
            first_pdf = pdf;
            // The RC outgoing edge uses solid-angle measure. Its sampling
            // density belongs in the initial weight, not the cached integrand.
        }
        else
        {
            float unused_pdf;
            throughput *= evaluate_brdf(cur.albedo, cur.roughness, cur.metallic,
                cur.hit_normal, view, direction, unused_pdf, 1.0f) / pdf;
        }

        RayDesc ray;
        ray.Origin = cur.hit_position + cur.geometric_normal * compute_ray_offset(cur.hit_position);
        ray.Direction = direction;
        ray.TMin = RESTIR_RAY_T_MIN;
        ray.TMax = 1000.0f;
        PathSurface next = restir_trace_surface(ray);
        float mis = power_heuristic(pdf, sky_nee_pdf_at(direction, cur.hit_normal));
        float3 emitted = next.hit ? next.emission : sample_sky(direction);
        float unused_rc_pdf;
        float3 f_rc = evaluate_brdf(rc.albedo, rc.roughness, rc.metallic, rc.hit_normal,
            rc_view, first_direction, unused_rc_pdf, 1.0f);
        result.reference_radiance += f_rc * throughput * emitted * mis * rr_weight / first_pdf;
        restir_stream_path(result, 0.0f, throughput * emitted * (depth == 3u ? 1.0f : mis), first_direction,
            first_pdf, depth, rc, rc_view, selection_seed, rr_weight / first_pdf, 65535u, 0u, replay, requested);
        if (replay && depth == requested.path_length)
            return result;
        if (!next.hit)
            break;
        if (depth < get_restir_max_path_length())
        {
            light = sample_direct_lighting_at_vertex(next.hit_position, next.hit_normal,
                next.geometric_normal, -direction, next.albedo, next.roughness, next.metallic, 1.0f, seed,
                replay ? int(requested.endpoint_light) : -1, replay ? int(requested.endpoint_emtri_draw) : -1);
            result.reference_radiance += f_rc * throughput * light.radiance_sum * rr_weight / first_pdf;
            float unused_pdf;
            float3 brdf = evaluate_brdf(next.albedo, next.roughness, next.metallic,
                next.hit_normal, -direction, light.direction, unused_pdf, 1.0f);
            if (light.light_index == uint(buffer_frame.restir_pt_light_count) + 1u)
                light.incident *= power_heuristic(sky_nee_pdf_at(light.direction, next.hit_normal), unused_pdf);
            restir_stream_path(result, 0.0f, throughput * brdf * light.incident,
                first_direction, first_pdf, depth + 1u, rc, rc_view, selection_seed,
                rr_weight * light.initial_weight / first_pdf, light.light_index, light.emtri_draw, replay, requested);
            if (replay && depth + 1u == requested.path_length && requested.endpoint_light != 65535u)
                return result;
        }
        cur = next;
        view = -direction;
    }
    if (result.selected_target > 0.0f)
        result.initial_weight = replay ? 1.0f : result.weight_sum / result.selected_target;
    return result;
}

// traces a path from the primary, captures the reconnection vertex and the suffix radiance
PathSample trace_path_from_primary(
    float3 primary_pos,
    float3 primary_normal,
    float3 dir,
    float dir_pdf,
    uint replay_seed,
    inout uint seed,
    out float3 reference_radiance, bool replay, PathSample requested)
{
    reference_radiance = 0.0f;
    PathSample s = (PathSample)0;
    s.initial_weight = 1.0f;
    s.endpoint_light = 65535u;
    s.rc_pos          = float3(0, 0, 0);
    s.rc_normal       = float3(0, 1, 0);
    s.rc_outgoing_dir = float3(0, 1, 0);
    s.rc_L_post       = float3(0, 0, 0);
    s.rc_L_nee        = float3(0, 0, 0);
    s.rc_albedo       = float3(0, 0, 0);
    s.rc_roughness    = 1.0f;
    s.rc_metallic     = 0.0f;
    s.src_pos         = float3(0, 0, 0);
    s.src_normal      = float3(0, 1, 0);
    s.src_albedo      = float3(0, 0, 0);
    s.src_roughness   = 1.0f;
    s.src_metallic    = 0.0f;
    // store the seed used for xi so the random replay shift can re-derive the same prefix
    s.seed_path       = replay_seed;
    s.path_length     = 0;
    s.rc_length       = 0;
    s.flags           = 0;

    if (dot(dir, primary_normal) <= 0.0f)
        return s;

    float primary_offset = compute_ray_offset(primary_pos);
    RayDesc ray;
    ray.Origin    = primary_pos + primary_normal * primary_offset;
    ray.Direction = dir;
    ray.TMin      = RESTIR_RAY_T_MIN;
    ray.TMax      = 1000.0f;

    PathSurface hit = restir_trace_surface(ray);

    if (!hit.hit)
    {
        // the ray escaped, the sky dome is the reconnection vertex and rc_pos carries the
        // direction, restir replaces diffuse ibl wholesale in light_image_based so dropping
        // this candidate would remove every bit of sky lighting from the primary bounce
        // the sun disc is excluded inside sample_sky, the analytic directional light owns it
        s.flags      |= PATH_FLAG_SKY;
        s.rc_pos      = dir;
        s.rc_normal   = -dir;
        s.rc_L_nee    = sample_sky(dir);
        reference_radiance = s.rc_L_nee;
        s.rc_L_post   = float3(0, 0, 0);
        s.path_length = 2;
        s.rc_length   = 2;
        return s;
    }

    s.rc_pos       = hit.hit_position;
    // shading normal, the suffix sampled rc_outgoing_dir around it and f_rc is re-evaluated
    // against it at shift time, the geometric normal would reject directions the path took
    s.rc_normal    = hit.hit_normal;
    s.rc_albedo    = hit.albedo;
    s.rc_roughness = max(hit.roughness, 0.04f);
    s.rc_metallic  = hit.metallic;
    s.rc_length    = 2;

    RestirPathSelection path = sample_path_tree_at_rc(hit, -normalize(hit.hit_position - primary_pos), seed, replay, requested);
    reference_radiance = path.reference_radiance;
    s.rc_L_nee        = path.emission;
    s.rc_L_post       = path.suffix;
    s.rc_outgoing_dir = path.direction;
    s.path_length     = path.length;
    s.initial_weight  = path.initial_weight;
    s.endpoint_light  = path.endpoint_light;
    s.endpoint_emtri_draw = path.endpoint_emtri_draw;
    s.rc_pdf = (path.length > 2u && !(path.length == 3u && path.endpoint_light != 65535u)) ? path.outgoing_pdf : 0.0f;
    float first_pdf = 0.0f;
    float3 geometric_dir = normalize(hit.hit_position - primary_pos);
    if (any(path.suffix > 0.0f))
        evaluate_brdf(s.rc_albedo, s.rc_roughness, s.rc_metallic, s.rc_normal,
            -geometric_dir, s.rc_outgoing_dir, first_pdf, 1.0f);
    if (any(path.emission > 0.0f))
        s.flags |= PATH_FLAG_RC_EMIT;

    // scene independent reconnection criteria, lin 2026 4
    // dual ray footprint thresholds bound the area density change at rc and the angular density
    // change of the rc outgoing lobe, the primary lobe is cosine only so its roughness never
    // invalidates reconnection
    float dist_sq             = dot(hit.hit_position - primary_pos, hit.hit_position - primary_pos);
    float footprint_threshold = RESTIR_RC_FOOTPRINT_C * restir_primary_footprint_sq(primary_pos, primary_normal);

    // forward footprint, reciprocal area density of rc when traced from the primary
    float cos_at_rc    = abs(dot(s.rc_normal, geometric_dir));
    float geometric_pdf = max(dot(primary_normal, geometric_dir), 0.0f) / PI;
    float fp_forward   = dist_sq / max(geometric_pdf * cos_at_rc, 1e-6f);

    // inverse footprint, reciprocal area density of the primary when traced back from rc,
    // skipped for terminal paths and for diffuse rc, where reconnection cannot meaningfully
    // change the outgoing density, lin 2026 4, keeping the test there only rejects reconnections
    // that were safe and leaves those paths unreusable
    bool  rc_is_diffuse = s.rc_roughness >= RESTIR_RC_DIFFUSE_ROUGHNESS;
    float fp_inverse    = 1e30f;
    if (first_pdf > RESTIR_MIN_PDF && !rc_is_diffuse)
    {
        float cos_at_primary = abs(dot(primary_normal, geometric_dir));
        fp_inverse           = dist_sq / max(first_pdf * cos_at_primary, 1e-6f);
    }

    bool rc_valid = (min(fp_forward, fp_inverse) >= footprint_threshold)
                 && (dist_sq >= RESTIR_RC_MIN_DISTANCE * RESTIR_RC_MIN_DISTANCE);
    if (rc_valid)
        s.flags |= PATH_FLAG_HAS_RC;

    return s;
}


// Replay the complete random-number sample only in the no-reconnection domain.
// Both sides must remain in that domain; otherwise the two mappings overlap.
// Keep the selected leaf fixed. Initial RIS and roulette weights stay in the
// reservoir weight; rerunning either selection would change the shift domain.
ShiftResult try_random_replay_shift(PathSample src, float3 src_pos, float3 dst_pos,
    float3 dst_normal, float3 dst_view, float3 dst_albedo, float roughness, float metallic)
{
    ShiftResult result = (ShiftResult)0;
    result.sample = src;
    uint seed = src.seed_path;
    float pdf_dst;
    float3 dir = sample_brdf(dst_albedo, roughness, metallic, dst_normal, dst_view,
        random_float2(seed), pdf_dst, restir_primary_specular_blend(roughness));
    if (pdf_dst < RESTIR_MIN_PDF || dot(dst_normal, dir) <= RESTIR_RC_COS_FRONT || any(isnan(dir)))
        return result;
    float3 unused_reference;
    PathSample replayed = trace_path_from_primary(dst_pos, dst_normal, dir, pdf_dst, src.seed_path, seed, unused_reference, true, src);
    if (has_reconnection(replayed) || is_sky_sample(replayed) || replayed.path_length != src.path_length || replayed.endpoint_light != src.endpoint_light)
        return result;
    // Recover the density of the actual primary random draw. The hit-to-primary
    // vector includes the ray-origin offset and is NOT the sampled direction.
    // At close hits this made J > 1 even for an identity replay, amplifying energy
    // on every reuse. For the diffuse primary both replay densities are identical.
    uint source_seed = src.seed_path;
    float2 source_xi = random_float2(source_seed);
    float pdf_src;
    sample_brdf(src.src_albedo, src.src_roughness, src.src_metallic, src.src_normal,
        normalize(get_camera_position() - src_pos), source_xi, pdf_src,
        restir_primary_specular_blend(src.src_roughness));
    if (pdf_src < RESTIR_MIN_PDF)
        return result;
    result = evaluate_path_integrand(replayed, dst_pos, dst_normal, dst_view, dst_albedo, roughness, metallic);
    replayed.F = result.f_dst;
    result.sample = replayed;
    // Primary and RC outgoing edges use solid-angle measure. The remaining
    // suffix is already expressed in random-number measure by its f/pdf terms.
    float rc_ratio = 1.0f;
    if (src.rc_pdf > 0.0f)
    {
        if (replayed.rc_pdf < RESTIR_MIN_PDF)
            return (ShiftResult)0;
        rc_ratio = src.rc_pdf / replayed.rc_pdf;
    }
    result.jacobian = (pdf_src / pdf_dst) * rc_ratio;
    return result;
}
#endif
