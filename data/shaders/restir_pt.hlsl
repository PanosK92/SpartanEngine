/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

//= INCLUDES ===================
#include "common.hlsl"
#include "common_road.hlsl"
#include "common_decals.hlsl"
#include "common_ray_hit.hlsl"
#include "restir_reservoir.hlsl"
#include "restir_surface.hlsl"
#include "nrd/NRD.hlsli"
//==============================

// upper bounds on the per pixel ris pool sizes, live counts come from the get_restir_* helpers
// rr and sun/sky sampling constants and helpers live in restir_reservoir.hlsl, shared with the
// replay shift so the initial trace and the replay evaluate the same integrand
static const uint  INITIAL_CANDIDATE_SAMPLES_MAX   = 8;
static const uint  LIGHT_RIS_CANDIDATE_SAMPLES_MAX = 64;
static const float MIN_COS_AT_PRIMARY              = 0.0f;

// samples a candidate whose rc sits on an area sampled point of an emissive triangle
// returns the rc position, emitted radiance and the ris weight standing in for 1 / source pdf
PathSample sample_emissive_tri_candidate(
    float3 primary_pos,
    float3 primary_normal,
    inout uint seed,
    out float ris_weight, float3 view, float3 albedo, float roughness, float metallic)
{
    PathSample s = (PathSample)0;
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
    s.src_normal_roughness = gbuffer_pack_normal(s.src_normal, 0u) | 0xff000000u;
    s.src_albedo      = float3(0, 0, 0);
    s.src_roughness   = 1.0f;
    s.src_metallic    = 0.0f;
    s.seed_path       = seed;
    s.path_length     = 2;
    s.rc_length       = 2;
    s.flags           = 0;
    s.endpoint_light  = RESTIR_ENDPOINT_EMTRI;
    ris_weight        = 0.0f;

    // ris over cdf draws, see emtri_ris_pick, the unshadowed geometry term picks the panel
    float3 light_pos, light_normal, emission;
    float  W;
    uint triangle_index;
    if (!emtri_ris_pick(primary_pos, primary_normal, seed, light_pos, light_normal, emission, W, triangle_index,
        view, albedo, roughness, metallic))
    {
        return s;
    }

    float3 to   = light_pos - primary_pos;
    float  dist = length(to);
    if (dist < 1e-3f || dot(to / dist, primary_normal) <= MIN_COS_AT_PRIMARY)
    {
        return s;
    }

    ris_weight  = W;
    s.flags    |= PATH_FLAG_HAS_RC | PATH_FLAG_NEE;
    s.rc_pos    = light_pos;
    s.rc_normal = light_normal;
    float2 geometric_oct = octahedral_encode(light_normal);
    s.rc_pdf = pack_f16x2_to_float(geometric_oct.x, geometric_oct.y);
    s.rc_L_nee  = emission;
    s.endpoint_pooled = true;
    s.endpoint_geometry = uint2(triangle_index, 0u);
    s.rc_L_post = float3(0, 0, 0);
    return s;
}

// builds a candidate by directly sampling an analytical light or the sun cone
// rc is the sampled light point, source_pdf is in solid angle at the primary
PathSample sample_light_candidate(
    float3 primary_pos,
    float3 primary_normal,
    inout uint seed,
    out float source_pdf)
{
    PathSample s = (PathSample)0;
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
    s.src_normal_roughness = gbuffer_pack_normal(s.src_normal, 0u) | 0xff000000u;
    s.src_albedo      = float3(0, 0, 0);
    s.src_roughness   = 1.0f;
    s.src_metallic    = 0.0f;
    s.seed_path       = seed;
    s.path_length     = 2;
    s.rc_length       = 2;
    s.flags           = 0;
    source_pdf        = 0.0f;

    uint light_count = (uint)buffer_frame.restir_pt_light_count;
    if (light_count == 0)
        return s;

    uint4 tile_entry = restir_light_tile_entry(random_float(seed));
    uint light_idx = tile_entry.x;
    float pick_pdf = asfloat(tile_entry.y);
    if (light_idx >= light_count || pick_pdf <= 0.0f)
        return s;
    LightParameters light = light_parameters[light_idx];
    bool is_directional = (light.flags & (1u << 0)) != 0;
    bool is_area        = (light.flags & (1u << 6)) != 0;
    s.endpoint_light = light_idx;

    if (is_directional)
    {
        // continuous sun cone, sole owner of the sun since sky reads exclude the disc
        float sun_cone_pdf;
        float3 sampled = sample_restir_sun_cone(-light.direction, random_float2(seed), sun_cone_pdf);

        if (dot(sampled, primary_normal) <= MIN_COS_AT_PRIMARY)
            return s;

        // no continuation past rc, rc_L_post stays zero
        // The same disk radiance and PDF are used at secondary vertices.
        s.flags      |= PATH_FLAG_SKY | PATH_FLAG_NEE;
        s.rc_pos      = sampled;
        s.rc_normal   = -sampled;
        s.rc_L_nee    = light.color.rgb * light.intensity * sun_cone_pdf;
        s.rc_L_post   = float3(0, 0, 0);
        source_pdf    = pick_pdf * sun_cone_pdf;
        return s;
    }

    if (is_area && light.area_width > 0.0f && light.area_height > 0.0f)
    {
        // urena 2013 solid angle sampling, source pdf is in solid angle directly
        float3 light_normal = light.direction;
        float3 light_right  = normalize(light.direction_right);
        float3 light_up     = normalize(cross(light_normal, light_right));

        float3 ex          = light_right * light.area_width;
        float3 ey          = light_up    * light.area_height;
        float3 rect_origin = light.position - 0.5f * ex - 0.5f * ey;

        float2 xi = random_float2(seed);
        float3 sampled_pos;
        float  solid_angle;
        sample_spherical_rectangle(primary_pos, rect_origin, ex, ey, xi, sampled_pos, solid_angle);

        if (solid_angle < MIN_AREA_LIGHT_SOLID_ANGLE)
            return s;

        float3 to   = sampled_pos - primary_pos;
        float  dist = length(to);
        if (dist < 1e-3f)
            return s;

        float3 dir       = to / dist;
        float  cos_light = dot(-dir, light_normal);
        if (cos_light <= 0.0f || dot(dir, primary_normal) <= MIN_COS_AT_PRIMARY)
            return s;

        // area light, rc is the emitter surface, no continuation past rc
        s.rc_pos      = sampled_pos;
        s.rc_normal   = light_normal;
        float2 geometric_oct = octahedral_encode(light_normal);
        s.rc_pdf = pack_f16x2_to_float(geometric_oct.x, geometric_oct.y);
        s.rc_L_nee    = light.color.rgb * light.intensity * restir_range_window(dist, light.range);
        s.rc_L_post   = float3(0, 0, 0);
        s.flags      |= PATH_FLAG_HAS_RC | PATH_FLAG_NEE;

        float sa_pdf = 1.0f / solid_angle;
        source_pdf   = pick_pdf * sa_pdf;
        return s;
    }

    bool is_point = (light.flags & (1u << 1)) != 0;
    bool is_spot  = (light.flags & (1u << 2)) != 0;
    if (is_point || is_spot)
    {
        // dirac local light, attenuation and spot cone folded into rc_L_nee, single strategy mis
        float3 to        = light.position - primary_pos;
        float  light_dist = length(to);
        if (light_dist < 1e-3f)
            return s;

        float3 dir = to / light_dist;
        if (dot(dir, primary_normal) <= MIN_COS_AT_PRIMARY)
            return s;

        if (light.range <= 0.0f || light_dist >= light.range)
        {
            return s;
        }

        float attenuation = restir_range_window(light_dist, light.range) / (light_dist * light_dist + 0.0001f);
        attenuation      *= restir_spot_factor(light, dir);
        if (attenuation <= 0.0f)
        {
            return s;
        }

        s.rc_pos              = light.position;
        s.rc_normal           = -dir;
        s.rc_L_nee            = light.color.rgb * light.intensity * attenuation;
        s.rc_L_post           = float3(0, 0, 0);
        s.flags              |= PATH_FLAG_HAS_RC | PATH_FLAG_NEE;
        s.endpoint_emtri_draw = RESTIR_ENDPOINT_DIRAC_TAG;
        s.endpoint_geometry = uint2(light.identity_low, light.identity_high);

        // unit solid angle pdf keeps dirac candidates on the same scale as the area branch
        source_pdf = pick_pdf;
        return s;
    }

    return s;
}

[[vk::image_format("rgba32f")]] RWTexture2D<float4> restir_primary_hit_output : register(u71);

[shader("raygeneration")]
void ray_gen()
{
    uint2 launch_id   = DispatchRaysIndex().xy;
    uint2 launch_size = DispatchRaysDimensions().xy;
    uint2 light_block = launch_id / 8u;
    restir_light_tile_index = xxhash32(light_block.x ^ xxhash32(light_block.y) ^ buffer_frame.frame) % RESTIR_LIGHT_TILE_COUNT;
    float2 uv = restir_primary_uv((launch_id + 0.5f) / launch_size);

    restir_primary_hit_output[launch_id] = 0.0f;

    // early out for sky pixels (no primary surface)
    float depth = tex_depth.SampleLevel(GET_SAMPLER(sampler_point_clamp), uv, 0).r;
    if (depth <= 0.0f)
    {
        Reservoir empty = create_empty_reservoir();
        float4 t0, t1, t2, t3, t4, t5;
        pack_reservoir(empty, t0, t1, t2, t3, t4, t5);
        tex_reservoir0[launch_id] = t0;
        tex_reservoir1[launch_id] = t1;
        tex_reservoir2[launch_id] = t2;
        tex_reservoir3[launch_id] = t3;
        tex_reservoir4[launch_id] = t4;
        tex_reservoir5[launch_id] = t5;
        tex_uav[launch_id] = float4(0, 0, 0, 1);
        restir_specular_output[launch_id] = 0.0f;
        return;
    }

    uint seed = create_seed_for_pass(launch_id, buffer_frame.frame, 0);

    // gather primary surface properties from the g-buffer
    float3 pos_ws    = restir_primary_position(uv);
    float3 normal_ws = get_normal(uv);
    float3 geometric_normal_ws = get_geometric_normal(uv);
    float4 material  = get_material(uv);
    float3 albedo    = saturate(tex_albedo.SampleLevel(GET_SAMPLER(sampler_point_clamp), uv, 0).rgb);
    float  roughness = max(material.r, 0.04f);
    float  metallic  = material.g;
    float3 view_dir  = normalize(get_camera_position() - pos_ws);

    // Retain the primary triangle independently of the RIS winner. Dual motion
    // needs this old occluder even when it is not visible in the current image.
    RayDesc primary_ray;
    primary_ray.Origin = get_camera_position();
    primary_ray.Direction = -view_dir;
    primary_ray.TMin = RESTIR_RAY_T_MIN;
    float primary_distance = length(pos_ws - primary_ray.Origin);
    float match_tolerance = max(0.001f, primary_distance * 0.0001f);
    primary_ray.TMax = primary_distance + match_tolerance;
    float primary_hit_distance;
    uint3 primary_hit = restir_trace_hit(primary_ray, primary_hit_distance);
    // Raster-only/deformed surfaces can differ from the RT scene. Never label
    // such a surface with a nearby triangle from a different representation.
    if (primary_hit_distance > 0.0f && abs(primary_hit_distance - primary_distance) <= match_tolerance)
        restir_primary_hit_output[launch_id] = float4(asfloat(primary_hit), linearize_depth(depth));

    Reservoir reservoir = create_empty_reservoir();
    float3 canonical_gi = float3(0, 0, 0);
    float3 canonical_specular = 0.0f;
    float diffuse_hit_dist = 0.0f;
    uint diffuse_hit_count = 0u;
    // NRD reflection tracking needs an in-lobe first hit, independent of RIS.
    float specular_hit_dist = NRD_FrontEnd_SpecHitDistAveraging_Begin();
    float specular_probability = saturate(compute_spec_probability(albedo, roughness, metallic,
        dot(normal_ws, view_dir)) * restir_primary_specular_blend(roughness));

    // unified direct and indirect, lin 2026 6.1, analytic lights join the same reservoir as a nee
    // path of length two, including both primary diffuse and glossy scattering
    // per sample weight is lin 2022 algorithm 1, w_i = target / sum_t(N_t * p_t(y))
    const uint  n_brdf_count  = clamp(get_restir_initial_candidates(), 1u, INITIAL_CANDIDATE_SAMPLES_MAX);
    const uint  n_light_count = is_restir_pt_direct_enabled() ? clamp(get_restir_light_candidates(), 1u, LIGHT_RIS_CANDIDATE_SAMPLES_MAX) : 0u;
    const uint  n_emtri_count = is_emtri_pool_active() ? clamp(get_restir_emtri_candidates(), 1u, LIGHT_RIS_CANDIDATE_SAMPLES_MAX) : 0u;
    const float n_brdf        = float(n_brdf_count);
    const float n_light       = float(n_light_count);
    const float n_emtri       = float(n_emtri_count);

    for (uint i = 0; i < n_brdf_count; i++)
    {
        // capture the seed before xi so the replay shift can replay the same primary sample
        // Sample the complete primary diffuse/glossy mixture.
        uint   replay_seed = seed;
        float2 xi          = random_float2(seed);
        float  source_pdf;
        float3 dir         = sample_brdf(albedo, roughness, metallic, normal_ws, view_dir, xi, source_pdf, restir_primary_specular_blend(roughness));

        bool dir_valid = restir_pdf_valid(source_pdf) &&
                         (dot(dir, normal_ws) >= MIN_COS_AT_PRIMARY) &&
                         !any(isnan(dir));

        PathSample candidate = (PathSample)0;
        float weight = 0.0f;

        if (dir_valid)
        {
            float3 reference_radiance;
            candidate = trace_path_from_primary(pos_ws, normal_ws, dir, source_pdf, replay_seed, seed, reference_radiance, false, (PathSample)0,
                view_dir, albedo, roughness, metallic, get_camera_position(), geometric_normal_ws);
            // Each denoising guide must follow its own primary lobe, not the
            // endpoint selected by RIS or a ray drawn from the other lobe.
            if (xi.x < 1.0f - specular_probability)
            {
                diffuse_hit_dist += candidate.first_hit_distance;
                diffuse_hit_count++;
            }
            // Reuse only primary rays drawn from the specular component, before
            // light/path resampling can select an unrelated diffuse endpoint.
            if (xi.x >= 1.0f - specular_probability)
            {
                float distance = candidate.first_hit_distance;
                NRD_FrontEnd_SpecHitDistAveraging_Add(specular_hit_dist, distance);
            }
            // Independent path-tracing estimate: sum the traced light contributions,
            // without selecting a path endpoint or doing spatiotemporal reuse.
            float3 brdf_all = eval_surface_brdf_cos(albedo, roughness, metallic, normal_ws,
                view_dir, dir, 1.0f);
            float3 brdf_diffuse = eval_surface_brdf_cos(albedo, roughness, metallic, normal_ws,
                view_dir, dir, 0.0f);
            canonical_gi += brdf_all * reference_radiance / source_pdf;
            canonical_specular += max(brdf_all - brdf_diffuse, 0.0f) * reference_radiance / source_pdf;
            // The path walk evaluates F in its selected hybrid measure. A
            // primary-to-RC evaluation would discard a postponed glossy prefix.
            float target_pdf = target_pdf_self(candidate, pos_ws, normal_ws, view_dir, albedo, roughness, metallic);
            if (target_pdf > 0.0f)
            {
                // single strategy weight, the sun free sky is only reachable through brdf sampling
                weight = target_pdf * candidate.initial_weight / (source_pdf * n_brdf);
            }
        }

        update_reservoir(reservoir, candidate, weight, random_float(seed));
    }

    // A glossy-only draw set still needs a diffuse guide. This independent
    // cosine ray measures geometry only and never contributes radiance to RIS.
    if (diffuse_hit_count == 0u && metallic < 1.0f && any(albedo > 0.0f))
    {
        uint guide_seed = create_seed_for_pass(launch_id, buffer_frame.frame, 5);
        float unused_pdf;
        float3 direction = local_to_world(sample_cosine_hemisphere(random_float2(guide_seed), unused_pdf), normal_ws);
        RayDesc guide_ray;
        guide_ray.Origin = pos_ws + geometric_normal_ws * compute_ray_offset(pos_ws);
        guide_ray.Direction = direction;
        guide_ray.TMin = RESTIR_RAY_T_MIN;
        guide_ray.TMax = RESTIR_RAY_T_MAX;
        PathSurface guide = restir_trace_surface(guide_ray);
        diffuse_hit_dist = guide.hit ? length(guide.hit_position - pos_ws) : RESTIR_RAY_T_MAX;
        diffuse_hit_count = 1u;
    }

    // A diffuse-only draw set still needs a reflection guide. Trace one VNDF
    // first hit only in that case; it does not contribute radiance or alter RIS.
    if (specular_hit_dist == NRD_FrontEnd_SpecHitDistAveraging_Begin())
    {
        uint guide_seed = create_seed_for_pass(launch_id, buffer_frame.frame, 4);
        float3 local_view = world_to_local(view_dir, normal_ws);
        local_view.z = max(local_view.z, 0.0f);
        float3 half_vector = local_to_world(sample_ggx_vndf(local_view,
            random_float2(guide_seed), restir_d_ggx_alpha(roughness)), normal_ws);
        float3 direction = reflect(-view_dir, half_vector);
        if (dot(normal_ws, direction) > 0.0f)
        {
            RayDesc guide_ray;
            guide_ray.Origin = pos_ws + geometric_normal_ws * compute_ray_offset(pos_ws);
            guide_ray.Direction = direction;
            guide_ray.TMin = RESTIR_RAY_T_MIN;
            guide_ray.TMax = RESTIR_RAY_T_MAX;
            PathSurface guide = restir_trace_surface(guide_ray);
            NRD_FrontEnd_SpecHitDistAveraging_Add(specular_hit_dist,
                guide.hit ? length(guide.hit_position - pos_ws) : 10000.0f);
        }
    }
    NRD_FrontEnd_SpecHitDistAveraging_End(specular_hit_dist);

    // analytic light pick, lin 2026 6.1, ris over unshadowed candidates then one visibility ray
    // for the winner, the sub reservoir contribution weight stands in for 1 / source pdf so the
    // main stream stays unbiased, visibility must still enter the main target, an occluded sun
    // would otherwise outweigh every bounce and evict the valid indirect samples in shadow
    if (n_light_count > 0u)
    {
        PathSample light_pick    = (PathSample)0;
        float light_weight_sum   = 0.0f;
        float light_pick_target  = 0.0f;
        for (uint li = 0; li < n_light_count; li++)
        {
            float light_source_pdf;
            PathSample light_candidate = sample_light_candidate(pos_ws, normal_ws, seed, light_source_pdf);
            if (!restir_pdf_valid(light_source_pdf))
            {
                continue;
            }

            light_candidate.F  = evaluate_path_integrand(light_candidate, pos_ws, normal_ws, view_dir, albedo, roughness, metallic).f_dst;
            float target       = target_scalar(light_candidate.F);
            if (!(target > 0.0f) || !isfinite(target))
            {
                continue;
            }

            float w           = target / (n_light * light_source_pdf);
            light_weight_sum += w;
            if (random_float(seed) * light_weight_sum < w)
            {
                light_pick        = light_candidate;
                light_pick_target = target;
            }
        }

        float light_weight = 0.0f;
        if (light_pick_target > 0.0f && trace_shift_visibility(light_pick, pos_ws, geometric_normal_ws))
        {
            // single strategy weight, analytic lights are not in the bvh and the sun disc is
            // excluded from every sky read so no other strategy reaches this integrand
            float light_W  = light_weight_sum / light_pick_target;
            light_weight   = light_pick_target * light_W;
            canonical_gi  += light_pick.F * light_W * n_brdf;
            canonical_specular += light_pick.F * light_W * n_brdf *
                restir_primary_specular_fraction(light_pick, pos_ws, normal_ws, view_dir, albedo, roughness, metallic);
        }

        update_reservoir(reservoir, light_pick, light_weight, random_float(seed));
    }

    // emissive triangle nee strategy, area sampling of the global emissive pool
    // Complementary BSDF/NEE MIS is part of F. The RIS UCW remains separate
    // so reconnection can re-evaluate MIS with the destination BSDF and light PDF.
    for (uint ei = 0; ei < n_emtri_count; ei++)
    {
        float emtri_ris_weight;
        PathSample emtri_candidate = sample_emissive_tri_candidate(pos_ws, normal_ws, seed, emtri_ris_weight,
            view_dir, albedo, roughness, metallic);

        float emtri_weight = 0.0f;
        if (emtri_ris_weight > 0.0f)
        {
            emtri_candidate.F = evaluate_path_integrand(emtri_candidate, pos_ws, normal_ws, view_dir, albedo, roughness, metallic).f_dst;
            float target_pdf = target_pdf_self(emtri_candidate, pos_ws, normal_ws, view_dir, albedo, roughness, metallic);
            if (target_pdf > 0.0f && trace_shift_visibility(emtri_candidate, pos_ws, geometric_normal_ws))
            {
                // the ris weight is the unbiased stand in for 1 / source pdf of the pick
                emtri_weight = target_pdf * emtri_ris_weight / n_emtri;
                ShiftResult evaluated = self_shift_evaluate(emtri_candidate, pos_ws, normal_ws,
                    view_dir, albedo, roughness, metallic);
                canonical_gi += evaluated.f_dst * emtri_ris_weight * n_brdf / n_emtri;
                canonical_specular += evaluated.f_dst * emtri_ris_weight * n_brdf / n_emtri *
                    restir_primary_specular_fraction(emtri_candidate, pos_ws, normal_ws, view_dir, albedo, roughness, metallic);
            }
        }

        update_reservoir(reservoir, emtri_candidate, emtri_weight, random_float(seed));
    }

    // no 1/M divide, the balance mis weights already encode the 1/N_s normalization
    // the winner is visible by construction so the finalize target needs no visibility factor
    float final_target = target_pdf_self(reservoir.sample, pos_ws, normal_ws, view_dir, albedo, roughness, metallic);
    reservoir.target_pdf = final_target;
    reservoir.W = (final_target > 0.0f) ? (reservoir.weight_sum / final_target) : 0.0f;

    // the canonical technique gets a confidence of one no matter how many candidates scored,
    // the mis shares downstream are functions of the technique, not of what it happened to draw,
    // counting only the non zero candidates made the canonical vanish from the denominators on
    // exactly the pixels where the target is hard to hit, corners and the dark stretches between
    // panels, so the reused sample kept its full weight there frame after frame and the temporal
    // loop had nothing left to dilute it, that surplus is what grew into the bright blobs
    reservoir.M = 1.0f;

    // stamp the source primary g-buffer onto the chosen sample, all candidates from this pixel
    // share the same primary surface so we only need to write it once after ris finalization
    // downstream passes read these instead of sampling the current g-buffer at a reprojected
    // pixel, which fixes ghosting on moving objects
    reservoir.sample.src_pos       = pos_ws;
    reservoir.sample.src_normal    = normal_ws;
    reservoir.sample.src_normal_roughness = (tex_normal[gbuffer_texel(uv)].x & 0x00ffffffu) |
        ((tex_material[gbuffer_texel(uv)] & 255u) << 24u);
    reservoir.sample.src_albedo    = albedo;
    reservoir.sample.src_roughness = roughness;
    reservoir.sample.src_metallic  = metallic;

    float4 t0, t1, t2, t3, t4, t5;
    pack_reservoir(reservoir, t0, t1, t2, t3, t4, t5);
    tex_reservoir0[launch_id] = t0;
    tex_reservoir1[launch_id] = t1;
    tex_reservoir2[launch_id] = t2;
    tex_reservoir3[launch_id] = t3;
    tex_reservoir4[launch_id] = t4;
    tex_reservoir5[launch_id] = t5;

    canonical_gi /= n_brdf;
    canonical_gi /= restir_gi_demodulator(albedo);

    // Keep the primary hit guide independent of the selected light endpoint.
    float hit_dist = diffuse_hit_count > 0u ? diffuse_hit_dist / float(diffuse_hit_count) : 0.0f;

    tex_uav[launch_id] = float4(canonical_gi, hit_dist);
    restir_specular_output[launch_id] = float4(canonical_specular / n_brdf, specular_hit_dist);
}
