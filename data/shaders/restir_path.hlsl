/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

#ifndef SPARTAN_RESTIR_PATH
#define SPARTAN_RESTIR_PATH
#include "restir_surface.hlsl"
#include "restir_bsdf.hlsl"

PathSurface restir_rc_surface(PathSample s)
{
    PathSurface surface = (PathSurface)0;
    surface.hit = true;
    surface.hit_position = s.rc_pos;
    surface.hit_normal = s.rc_normal;
    surface.geometric_normal = unpack_rc_geometric_normal(s.rc_geometric_normal_packed);
    surface.albedo = s.rc_albedo;
    surface.roughness = s.rc_roughness;
    surface.metallic = s.rc_metallic;
    surface.material_index = s.rc_material_index;
    surface.front_facing = s.rc_front_facing;
    MaterialParameters mat = material_parameters[s.rc_material_index];
    surface.material_alpha = mat.color.a;
    surface.alpha_blend = mat.alpha_blend;
    surface.ior = mat.ior;
    surface.absorption = mat.absorption;
    surface.thickness = mat.thickness;
    return surface;
}

float3 restir_rc_bsdf_evaluate(PathSample s, float3 view, float3 direction, out float pdf)
{
    return restir_surface_evaluate(restir_rc_surface(s), view, direction, pdf, 1.0f);
}

uint3 restir_trace_hit(RayDesc ray, out float hit_distance)
{
    RayQuery<RAY_FLAG_SKIP_CLOSEST_HIT_SHADER> query;
    query.TraceRayInline(tlas, RAY_FLAG_NONE, 0xFF, ray);
    while (query.Proceed())
    {
        if (query.CandidateType() == CANDIDATE_NON_OPAQUE_TRIANGLE &&
            rt_candidate_opaque(query.CandidateInstanceIndex(), query.CandidatePrimitiveIndex(),
                query.CandidateTriangleBarycentrics(), ray.Origin + ray.Direction * query.CandidateTriangleRayT()))
            query.CommitNonOpaqueTriangleHit();
    }
    hit_distance = -1.0f;
    if (query.CommittedStatus() != COMMITTED_TRIANGLE_HIT)
        return 0u;
    hit_distance = query.CommittedRayT();
    return uint3(query.CommittedInstanceIndex(), query.CommittedPrimitiveIndex(),
        pack_hit_barycentrics(query.CommittedTriangleBarycentrics()));
}

PathSurface restir_trace_surface(RayDesc ray)
{
    float hit_distance;
    uint3 hit = restir_trace_hit(ray, hit_distance);
    return reconstruct_path_surface(hit_distance, hit.x, hit.y, hit.z, ray, 0.0f);
}

// Match the reference's explicit-LOD reconstruction from retained hit identity.
// The connection supplies the view direction; material lookup must not change
// its LOD merely because a different primary reconnects to the same vertex.
bool restir_reconstruct_rc(inout PathSample s, float3 previous_position)
{
    restir_refresh_environment_endpoint(s);
    if (is_area_light_sample(s)) return restir_reconstruct_area_endpoint(s, previous_position);
    if (!is_triangle_reconnection(s)) return true;
    if (is_nee_sample(s) && s.endpoint_light == RESTIR_ENDPOINT_EMTRI)
    {
        if (!restir_pool_hit_position(s.rc_hit, s.rc_pos)) return false;
        EmissiveTriangle tri = emissive_triangles[s.rc_hit.x];
        s.rc_normal = tri.normal;
        s.rc_geometric_normal_packed = pack_rc_geometric_normal(tri.normal);
        s.rc_L_nee = tri.emission;
        return true;
    }
    float3 position;
    if (!restir_rc_hit_position(s.rc_hit, position)) return false;
    float3 delta = position - previous_position;
    float d2 = dot(delta, delta);
    if (!restir_connection_distance_valid(d2)) return false;
    RayDesc ray;
    ray.Origin = previous_position;
    ray.Direction = delta * rsqrt(d2);
    ray.TMin = 0.0f;
    ray.TMax = sqrt(d2);
    PathSurface rc = reconstruct_path_surface(ray.TMax, s.rc_hit.x, s.rc_hit.y, s.rc_hit.z, ray, 0.0f);
    s.rc_pos = rc.hit_position;
    bool terminal = is_rc_emissive(s) || is_nee_sample(s);
    s.rc_normal = terminal ? rc.geometric_normal : rc.hit_normal;
    s.rc_geometric_normal_packed = pack_rc_geometric_normal(rc.geometric_normal);
    s.rc_albedo = rc.albedo;
    s.rc_roughness = max(rc.roughness, 0.04f);
    s.rc_metallic = rc.metallic;
    s.rc_material_index = rc.material_index;
    s.rc_front_facing = rc.front_facing;
    if (terminal) s.rc_L_nee = rc.emission;
    return all(isfinite(s.rc_normal));
}

// A common criterion for initial selection and both directions of a hybrid shift.
bool restir_prefix_connection_allowed(PathSurface previous, PathSurface rc, float incoming_pdf,
    float outgoing_pdf, float threshold)
{
    if (incoming_pdf * incoming_pdf * RESTIR_RC_MIN_PDF_ROUGHNESS > 1.0f)
        return false;
    if (!rc.hit)
        return true;
    float3 delta = rc.hit_position - restir_ray_origin(previous.hit_position, previous.geometric_normal);
    float d2 = dot(delta, delta);
    if (!restir_connection_distance_valid(d2))
        return false;
    float3 direction = delta * rsqrt(d2);
    // Equation 5 bounds area densities. BSDF PDFs use the shading normal,
    // but their solid-angle/area conversion uses the actual surface normal.
    float forward_pdf_cos = incoming_pdf * abs(dot(rc.geometric_normal, direction));
    float inverse_pdf_cos = outgoing_pdf * abs(dot(previous.geometric_normal, direction));
    return restir_dual_footprint_allowed(d2, forward_pdf_cos, inverse_pdf_cos, threshold);
}

// Initialize a descriptor at a surface or infinite environment endpoint.
PathSample restir_connection_descriptor(PathSurface previous, PathSurface rc, float3 direction,
    uint vertex, uint replay_seed)
{
    PathSample s = (PathSample)0;
    s.seed_path = replay_seed;
    s.rc_length = vertex;
    s.flags = PATH_FLAG_HAS_RC | (rc.hit ? 0u : PATH_FLAG_SKY);
    s.rc_hit = uint3(rc.geometry, rc.hit_barycentrics);
    s.rc_pos = rc.hit ? rc.hit_position : direction;
    s.rc_normal = rc.hit ? rc.hit_normal : -direction;
    // Cache density using the same geometric normal that survives storage;
    // otherwise an identity shift could accumulate quantization drift.
    s.rc_geometric_normal_packed = rc.hit ? pack_rc_geometric_normal(rc.geometric_normal) : 0u;
    s.rc_albedo = rc.albedo;
    s.rc_roughness = max(rc.roughness, 0.04f);
    s.rc_metallic = rc.metallic;
    s.rc_material_index = rc.material_index;
    s.rc_front_facing = rc.front_facing;
    s.rc_outgoing_dir = s.rc_normal;
    float3 delta = rc.hit_position - restir_ray_origin(previous.hit_position, previous.geometric_normal);
    float distance_sq = dot(delta, delta);
    s.rc_pdf = rc.hit ? abs(dot(unpack_rc_geometric_normal(s.rc_geometric_normal_packed), delta * rsqrt(distance_sq))) / distance_sq : 1.0f;
    return s;
}

void restir_stream_leaf(inout PathSample selected, inout float weight_sum, PathSample leaf,
    float inverse_density, inout uint selection_seed, bool replay, PathSample requested)
{
    float target = target_scalar(leaf.F);
    float weight = target * inverse_density;
    if (!(weight > 0.0f) || !isfinite(weight))
        return;
    weight_sum += weight;
    bool choose = replay ? leaf.path_length == requested.path_length && leaf.endpoint_light == requested.endpoint_light
                         : random_float(selection_seed) * weight_sum < weight;
    if (choose)
        selected = leaf;
}

// One BSDF spine with a light-ending leaf at each vertex. Reconnection is
// selected incrementally, so the walk needs no per-vertex array or second trace.
// The prefix uses random-number measure; only the connection and RC outgoing
// edges use solid angle. initial_weight includes the corresponding PDF factors.
PathSample trace_path_from_primary(float3 primary_pos, float3 primary_normal, float3 dir,
    float dir_pdf, uint replay_seed, inout uint seed, out float3 reference_radiance,
    bool replay, PathSample requested, float3 primary_view, float3 primary_albedo,
    float primary_roughness, float primary_metallic, float3 camera_position, float3 primary_geometric_normal)
{
    PathSample selected = (PathSample)0;
    PathSample connection = (PathSample)0;
    float weight_sum = 0.0f;
    uint selection_seed = seed ^ 0x68bc21ebu;
    reference_radiance = 0.0f;
    PathSurface previous = (PathSurface)0;
    previous.hit = true;
    previous.hit_position = primary_pos;
    previous.hit_normal = primary_normal;
    previous.geometric_normal = primary_geometric_normal;
    previous.albedo = primary_albedo;
    previous.roughness = primary_roughness;
    previous.metallic = primary_metallic;
    float unused_pdf;
    float3 throughput = evaluate_reflection_brdf(primary_albedo, primary_roughness, primary_metallic,
        primary_normal, primary_geometric_normal, primary_view, dir, unused_pdf, restir_primary_specular_blend(primary_roughness)) / dir_pdf;
    float3 reference_throughput = 1.0f;
    float3 connection_prefix = 0.0f;
    float3 suffix = 1.0f;
    float connection_density = 1.0f;
    float incoming_pdf = dir_pdf;
    float roulette_weight = 1.0f;
    float threshold = RESTIR_RC_FOOTPRINT_C * restir_primary_footprint_sq(primary_pos, primary_geometric_normal, camera_position);
    float first_hit_distance = 0.0f;
    float3 first_hit_position = 0.0f;
    [loop] for (uint depth = 2u; depth <= RESTIR_MAX_PATH_LENGTH; ++depth)
    {
        RayDesc ray;
        ray.Origin = restir_ray_origin(previous.hit_position, previous.geometric_normal);
        ray.Direction = dir;
        ray.TMin = RESTIR_RAY_T_MIN;
        ray.TMax = RESTIR_RAY_T_MAX;
        PathSurface current = restir_trace_surface(ray);
        if (depth == 2u)
        {
            first_hit_distance = current.hit ? length(current.hit_position - primary_pos) : RESTIR_MISS_HIT_DISTANCE;
            first_hit_position = current.hit ? current.hit_position : dir;
        }
        float3 emitted = current.hit ? current.emission : sample_sky(dir);
        float endpoint_nee_pdf = 0.0f;
        if (current.pooled_emitter)
        {
            EmissiveTriangle emitter;
            if (restir_hit_emitter_triangle(current.geometry, emitter))
                endpoint_nee_pdf = restir_emitter_source_pdf(emitter, ray.Origin, previous.hit_normal, current.hit_position, depth - 1u);
        }
        // Sky NEE has no support on surface emitters. An unpooled emissive
        // hit is sampled only by the BSDF and must keep its full contribution.
        float endpoint_mis = current.pooled_emitter
            ? restir_pooled_endpoint_mis(depth, RESTIR_ENDPOINT_BSDF, endpoint_nee_pdf, incoming_pdf)
            : (current.hit ? 1.0f : restir_environment_endpoint_mis(depth, RESTIR_ENDPOINT_BSDF, previous.hit_normal, dir, incoming_pdf));
        reference_radiance += reference_throughput * emitted * endpoint_mis * roulette_weight;
        PathSample leaf = connection;
        float inverse_density = connection_density;
        // A terminal emitter has no outgoing BRDF. Its normal describes the
        // actual surface for the connection footprint and solid-angle Jacobian,
        // not its normal map. Use the same normal when selecting and shifting
        // this endpoint so the reconnection domain remains reversible.
        PathSurface endpoint = current;
        endpoint.hit_normal = current.geometric_normal;
        if (has_reconnection(connection))
        {
            leaf.rc_L_post = suffix * emitted * (depth == connection.rc_length + 1u ? 1.0f : endpoint_mis);
        }
        else if (restir_prefix_connection_allowed(previous, endpoint, incoming_pdf, RESTIR_RC_EMITTER_PDF, threshold))
        {
            leaf = restir_connection_descriptor(previous, endpoint, dir, depth, replay_seed);
            leaf.flags |= PATH_FLAG_RC_EMIT;
            leaf.rc_L_nee = emitted;
            leaf.F = throughput * incoming_pdf * emitted * endpoint_mis;
            inverse_density = dir_pdf / incoming_pdf;
        }
        else
        {
            // No connection: the entire leaf is parameterized by random numbers.
            leaf = (PathSample)0;
            leaf.seed_path = replay_seed;
            leaf.rc_pos = first_hit_position;
            leaf.rc_normal = primary_normal;
            leaf.rc_outgoing_dir = primary_normal;
            if (depth == 2u && !current.hit) leaf.flags = PATH_FLAG_SKY;
            leaf.F = throughput * emitted * endpoint_mis;
            inverse_density = dir_pdf;
        }
        leaf.endpoint_pooled = current.pooled_emitter;
        leaf.endpoint_environment = !current.hit;
        leaf.endpoint_geometry = current.geometry;
        leaf.rc_nee_pdf = endpoint_nee_pdf;
        leaf.path_length = depth;
        leaf.endpoint_light = RESTIR_ENDPOINT_BSDF;
        if (has_reconnection(connection))
            leaf.F = connection_prefix * rc_outgoing_radiance(leaf, -connection.src_normal);
        restir_stream_leaf(selected, weight_sum, leaf, inverse_density * roulette_weight, selection_seed, replay, requested);
        if ((replay && requested.path_length == depth && requested.endpoint_light == RESTIR_ENDPOINT_BSDF) || !current.hit)
            break;
        if (depth == RESTIR_MAX_PATH_LENGTH)
            break;

        RestirLightSample light = sample_direct_lighting_at_vertex(current.hit_position, current.hit_normal,
            current.geometric_normal, -dir, current.albedo, current.roughness, current.metallic, 1.0f, seed,
            replay, depth);
        reference_radiance += reference_throughput * light.radiance_sum * roulette_weight;
        float light_bsdf_pdf;
        float3 light_f = evaluate_reflection_brdf(current.albedo, current.roughness, current.metallic,
            current.hit_normal, current.geometric_normal, -dir, light.direction, light_bsdf_pdf, 1.0f);
        float light_mis = light.light_index == RESTIR_ENDPOINT_EMTRI
            ? restir_pooled_endpoint_mis(depth + 1u, light.light_index, light.endpoint_pdf, light_bsdf_pdf)
            : restir_rc_endpoint_mis(3u, depth + 1u, light.light_index, light.light_index == RESTIR_ENDPOINT_SKY,
                current.hit_normal, light.direction, light_bsdf_pdf);
        leaf = connection;
        inverse_density = connection_density;
        if (has_reconnection(connection))
        {
            leaf.rc_L_post = suffix * light_f * light.incident * light_mis;
        }
        else if (restir_prefix_connection_allowed(previous, current, incoming_pdf, light_bsdf_pdf, threshold))
        {
            leaf = restir_connection_descriptor(previous, current, dir, depth, replay_seed);
            leaf.rc_outgoing_dir = light.direction;
            if (light.transmission) leaf.flags |= PATH_FLAG_TRANSMISSION_AFTER_RC;
            leaf.rc_L_post = light.incident;
            leaf.F = throughput * incoming_pdf * light_f * light.incident * light_mis;
            inverse_density = dir_pdf / incoming_pdf;
        }
        else
        {
            // Section 6.2.3: force connection to the sampled NEE endpoint.
            leaf = restir_light_endpoint_descriptor(light, restir_light_sample_origin(light, current.hit_position, current.geometric_normal), depth, replay_seed);
            leaf.F = light.endpoint_valid ? throughput * light_f * light.endpoint_radiance * light_mis : 0.0f;
            inverse_density = restir_pdf_valid(light.endpoint_pdf) ? dir_pdf / light.endpoint_pdf : 0.0f;
        }
        leaf.endpoint_pooled = light.light_index == RESTIR_ENDPOINT_EMTRI;
        leaf.endpoint_environment = light.light_index == RESTIR_ENDPOINT_SKY;
        leaf.endpoint_geometry = light.endpoint_dirac || light.endpoint_area > 0.0f ? light.endpoint_identity : uint2(light.emitter_triangle, 0u);
        leaf.rc_nee_pdf = light.endpoint_pdf;
        leaf.path_length = depth + 1u;
        leaf.endpoint_light = light.light_index;
        // Forced NEE reconnection already has the endpoint's persistent type
        // tag. Only continuing surface paths need the suffix proposal index.
        if (!is_nee_sample(leaf))
            leaf.endpoint_emtri_draw = light.emtri_draw;
        if (has_reconnection(connection))
            leaf.F = connection_prefix * rc_outgoing_radiance(leaf, -connection.src_normal);
        restir_stream_leaf(selected, weight_sum, leaf, inverse_density * roulette_weight * light.initial_weight,
            selection_seed, replay, requested);

        if (depth + 1u > 3u + RESTIR_RR_START)
        {
            float rr = random_float(seed);
            if (!replay && rr >= RESTIR_RR_CONTINUATION) break;
            if (!replay) roulette_weight /= RESTIR_RR_CONTINUATION;
        }
        RestirSurfaceSample scatter = restir_surface_sample(current, -dir, float3(random_float2(seed), 0.0f), 1.0f);
        if (!scatter.valid)
            break;
        float3 outgoing = scatter.direction;
        float outgoing_pdf = scatter.pdf;
        if (has_reconnection(connection))
            suffix *= scatter.weight;
        else if (restir_prefix_connection_allowed(previous, current, incoming_pdf, outgoing_pdf, threshold))
        {
            connection = restir_connection_descriptor(previous, current, dir, depth, replay_seed);
            connection.rc_outgoing_dir = outgoing;
            connection.src_normal = -dir; // transient incoming direction, stamped with the G-buffer before storing
            connection_prefix = throughput * incoming_pdf;
            connection_density = dir_pdf / (incoming_pdf * outgoing_pdf);
            suffix = 1.0f;
        }
        throughput *= scatter.weight;
        reference_throughput *= scatter.weight;
        previous = current;
        dir = outgoing;
        incoming_pdf = outgoing_pdf;
    }
    selected.first_hit_distance = first_hit_distance;
    float target = target_scalar(selected.F);
    selected.initial_weight = !replay && target > 0.0f ? weight_sum / target : 1.0f;
    return selected;
}

// Replay just the prefix, reconnect the stored suffix, and require the first
// admissible connection to remain at the same index in both domains.
ShiftResult restir_shift_prefix(PathSample src, float3 pos, float3 normal, float3 view,
    float3 albedo, float roughness, float metallic, float3 camera_position, float3 primary_geometric_normal)
{
    ShiftResult result = (ShiftResult)0;
    result.sample = src;
    PathSurface vertices[RESTIR_MAX_PATH_LENGTH + 1u];
    float3 directions[RESTIR_MAX_PATH_LENGTH + 1u];
    float pdfs[RESTIR_MAX_PATH_LENGTH + 1u];
    PathSurface primary = (PathSurface)0;
    primary.hit = true;
    primary.hit_position = pos;
    primary.hit_normal = normal;
    primary.geometric_normal = primary_geometric_normal;
    primary.albedo = albedo;
    primary.roughness = roughness;
    primary.metallic = metallic;
    vertices[1] = primary;
    uint seed = src.seed_path;
    float3 prefix = 1.0f;
    [loop] for (uint i = 1u; i + 1u < src.rc_length; ++i)
    {
        PathSurface v = vertices[i];
        float3 incoming = i == 1u ? view : -directions[i - 1u];
        if (i > 1u)
        {
            // Advance exactly the initial tree's NEE dimensions. A fixed BSDF
            // leaf skips all light evaluation and visibility work.
            sample_direct_lighting_at_vertex(v.hit_position, v.hit_normal, v.geometric_normal,
                incoming, v.albedo, v.roughness, v.metallic, 1.0f, seed, true, i);
            if (i + 1u > 3u + RESTIR_RR_START)
                random_float(seed); // no roulette termination during replay
        }
        float blend = i == 1u ? restir_primary_specular_blend(roughness) : 1.0f;
        RestirSurfaceSample scatter = restir_surface_sample(v, incoming, float3(random_float2(seed), 0.0f), blend);
        if (!scatter.valid)
            return result;
        float3 direction = scatter.direction;
        directions[i] = direction;
        pdfs[i] = scatter.pdf;
        prefix *= scatter.weight;
        RayDesc ray;
        ray.Origin = restir_ray_origin(v.hit_position, v.geometric_normal);
        ray.Direction = direction;
        ray.TMin = RESTIR_RAY_T_MIN;
        ray.TMax = RESTIR_RAY_T_MAX;
        vertices[i + 1u] = restir_trace_surface(ray);
        if (!vertices[i + 1u].hit)
            return result;
    }
    uint k = src.rc_length;
    PathSurface previous = vertices[k - 1u];
    float3 connection_origin = restir_reconnection_ray_origin(src, previous.hit_position, previous.geometric_normal);
    if (!restir_reconstruct_rc(src, connection_origin)) return result;
    result.sample = src;
    if (is_dirac_light_sample(src))
    {
        uint light_index;
        if (!restir_resolve_dirac_light(src, light_index))
            return result;
        src.endpoint_light = light_index;
        src.rc_pos = light_parameters[light_index].position;
        result.sample = src;
    }
    float3 delta = src.rc_pos - connection_origin;
    if (!is_sky_sample(src) && !restir_connection_distance_valid(dot(delta, delta)))
        return result;
    float3 direction = is_sky_sample(src) ? src.rc_pos : normalize(delta);
    if (dot(previous.hit_normal, direction) <= 0.0f)
        return result;
    directions[k - 1u] = direction;
    float connect_pdf;
    float3 connect_f = evaluate_reflection_brdf(previous.albedo, previous.roughness, previous.metallic,
        previous.hit_normal, previous.geometric_normal, -directions[k - 2u], direction, connect_pdf, 1.0f);
    float threshold = RESTIR_RC_FOOTPRINT_C * restir_primary_footprint_sq(pos, primary_geometric_normal, camera_position);
    [loop] for (uint j = 2u; j < k; ++j)
    {
        PathSurface v = vertices[j];
        float pdf_out;
        evaluate_reflection_brdf(v.albedo, v.roughness, v.metallic, v.hit_normal, v.geometric_normal,
            -directions[j - 1u], directions[j], pdf_out, 1.0f);
        if (restir_prefix_connection_allowed(vertices[j - 1u], v, pdfs[j - 1u], pdf_out, threshold))
            return result;
    }
    PathSurface rc = (PathSurface)0;
    rc.hit = !is_sky_sample(src);
    rc.hit_position = src.rc_pos;
    rc.hit_normal = src.rc_normal;
    rc.geometric_normal = unpack_rc_geometric_normal(src.rc_geometric_normal_packed);
    float pdf_out = is_rc_emissive(src) ? RESTIR_RC_EMITTER_PDF : 0.0f;
    if (k < src.path_length)
        restir_rc_bsdf_evaluate(src, -direction, src.rc_outgoing_dir, pdf_out);
    if (!is_nee_sample(src) && !restir_prefix_connection_allowed(previous, rc, connect_pdf, pdf_out, threshold))
        return result;
    if (!is_sky_sample(src) && !is_dirac_light_sample(src) && dot(src.rc_normal, -direction) <= 0.0f)
        return result;
    float geometry = is_sky_sample(src) || is_dirac_light_sample(src) ? 1.0f : abs(dot(rc.geometric_normal, -direction)) / dot(delta, delta);
    geometry *= restir_endpoint_area_measure(src);
    if (geometry <= 0.0f || src.rc_pdf <= 0.0f)
        return result;
    PathSample connection = src;
    connection.rc_length = 2u; // test the actual connecting edge, not primary-to-RC
    if (!trace_shift_visibility(connection, previous.hit_position, previous.geometric_normal))
        return result;
    float mis = 1.0f;
    if (k == src.path_length)
        mis = src.endpoint_pooled
            ? restir_pooled_endpoint_mis(src.path_length, src.endpoint_light,
                restir_terminal_emitter_pdf(src, connection_origin, previous.hit_normal), connect_pdf)
            : restir_rc_endpoint_mis(3u, src.path_length, src.endpoint_light, src.endpoint_environment,
                previous.hit_normal, direction, connect_pdf);
    float3 radiance = rc_outgoing_radiance(src, direction);
    if (is_dirac_light_sample(src))
    {
        uint light_index;
        if (!restir_resolve_dirac_light(src, light_index) || !restir_dirac_light_radiance(light_index, connection_origin, radiance))
            return result;
        result.sample.rc_L_nee = radiance;
    }
    result.f_dst = prefix * connect_f * radiance * mis;
    result.jacobian = geometry / src.rc_pdf;
    result.sample.rc_pdf = geometry;
    result.sample.F = result.f_dst;
    result.ok = all(isfinite(result.f_dst)) && isfinite(result.jacobian) && result.jacobian > 0.0f;
    return result;
}

// Replay the complete random-number sample only in the no-reconnection domain.
// Both sides must remain in that domain; otherwise the two mappings overlap.
// Keep the selected leaf fixed. Initial RIS and roulette weights stay in the
// reservoir weight; rerunning either selection would change the shift domain.
ShiftResult try_random_replay_shift(PathSample src, float3 src_pos, float3 dst_pos,
    float3 dst_normal, float3 dst_view, float3 dst_albedo, float roughness, float metallic, float3 src_view,
    float3 camera_position, float3 dst_geometric_normal)
{
    ShiftResult result = (ShiftResult)0;
    result.sample = src;
    uint seed = src.seed_path;
    float pdf_dst;
    float3 dir = sample_reflection_brdf(dst_albedo, roughness, metallic, dst_normal, dst_geometric_normal, dst_view,
        random_float2(seed), pdf_dst, restir_primary_specular_blend(roughness));
    if (!restir_pdf_valid(pdf_dst) || dot(dst_normal, dir) <= RESTIR_RC_COS_FRONT || any(isnan(dir)))
        return result;
    float3 unused_reference;
    PathSample replayed = trace_path_from_primary(dst_pos, dst_normal, dir, pdf_dst, src.seed_path, seed, unused_reference, true, src,
        dst_view, dst_albedo, roughness, metallic, camera_position, dst_geometric_normal);
    if (has_reconnection(replayed) || replayed.path_length != src.path_length || replayed.endpoint_light != src.endpoint_light)
        return result;
    result.sample = replayed;
    result.f_dst = replayed.F;
    result.jacobian = 1.0f; // both complete paths use the same random-number coordinates
    result.ok = all(isfinite(result.f_dst));
    return result;
}
#endif
