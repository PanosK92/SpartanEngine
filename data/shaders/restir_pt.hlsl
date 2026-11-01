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
//==============================

// upper bounds on the per pixel ris pool sizes, live counts come from the get_restir_* helpers
// rr and sun/sky sampling constants and helpers live in restir_reservoir.hlsl, shared with the
// replay shift so the initial trace and the replay evaluate the same integrand
static const uint  INITIAL_CANDIDATE_SAMPLES_MAX   = 8;
static const float MIN_COS_AT_PRIMARY              = 0.0f;

[[vk::image_format("rgba32f")]] RWTexture2D<float4> restir_primary_hit_output : register(u71);
[[vk::image_format("rgba32f")]] RWTexture2D<float4> restir_primary_surface_output : register(u72);

[shader("raygeneration")]
void ray_gen()
{
    uint2 launch_id   = DispatchRaysIndex().xy;
    uint2 launch_size = DispatchRaysDimensions().xy;
    uint2 light_block = launch_id / 8u;
    restir_light_tile_index = xxhash32(light_block.x ^ xxhash32(light_block.y) ^ buffer_frame.frame) % RESTIR_LIGHT_TILE_COUNT;
    float2 uv = restir_primary_uv(launch_id, launch_size);

    restir_primary_hit_output[launch_id] = 0.0f;
    restir_primary_surface_output[launch_id] = 0.0f;

    // early out for sky pixels (no primary surface)
    // Allocation padding is not part of the rendered image. Its depth can be
    // left over from a larger viewport and must never generate paths/history.
    uint2 active_size = uint2(float2(launch_size) * get_render_uv_scale());
    float depth = all(launch_id < active_size)
        ? tex_depth.SampleLevel(GET_SAMPLER(sampler_point_clamp), uv, 0).r : 0.0f;
    if (depth <= 0.0f)
    {
        Reservoir empty = create_empty_reservoir();
        float4 t0, t1, t2, t3;
        pack_reservoir(empty, t0, t1, t2, t3);
        tex_reservoir0[launch_id] = t0;
        tex_reservoir1[launch_id] = t1;
        tex_reservoir2[launch_id] = t2;
        tex_reservoir3[launch_id] = t3;
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

    restir_primary_surface_output[launch_id] = restir_primary_surface(uv);
    uint primary_material = restir_primary_material(uv);
    restir_primary_hit_output[launch_id] = float4(0, 0, 0, asfloat(primary_material));

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
    uint3 primary_hit = restir_trace_hit(primary_ray, primary_hit_distance, true);
    // Raster-only/deformed surfaces can differ from the RT scene. Never label
    // such a surface with a nearby triangle from a different representation.
    if (primary_hit_distance > 0.0f && abs(primary_hit_distance - primary_distance) <= match_tolerance)
        restir_primary_hit_output[launch_id] = float4(asfloat(uint3(primary_hit.x + 1u, primary_hit.yz)), asfloat(primary_material));

    Reservoir reservoir = create_empty_reservoir();
    float3 canonical_gi = float3(0, 0, 0);
    // One BSDF path tree and one primary NEE leaf share the initial RIS.
    const uint n_brdf_count = clamp(get_restir_initial_candidates(), 1u, INITIAL_CANDIDATE_SAMPLES_MAX);
    const float n_brdf = float(n_brdf_count);

    for (uint i = 0; i < n_brdf_count; i++)
    {
        // capture the seed before xi so the replay shift can replay the same primary sample
        // Sample the complete primary diffuse/glossy mixture.
        uint   replay_seed = seed;
        float2 xi          = random_float2(seed);
        RestirSurfaceSample primary = restir_primary_sample(albedo, roughness, metallic,
            normal_ws, geometric_normal_ws, view_dir, xi, restir_primary_specular_blend(roughness));
        float source_pdf = restir_path_sample_pdf(primary);
        float3 dir = primary.direction;

        bool dir_valid = primary.valid && restir_pdf_valid(source_pdf) &&
                         (dot(dir, normal_ws) >= MIN_COS_AT_PRIMARY) &&
                         !any(isnan(dir));

        PathSample candidate = (PathSample)0;
        float weight = 0.0f;

        if (dir_valid)
        {
            float3 reference_radiance;
            candidate = trace_path_from_primary(pos_ws, normal_ws, dir, source_pdf, primary.weight, replay_seed, seed, reference_radiance, false, (PathSample)0,
                view_dir, albedo, roughness, metallic, get_camera_position(), geometric_normal_ws);
            // Independent path-tracing estimate: sum the traced light contributions,
            // without selecting a path endpoint or doing spatiotemporal reuse.
            canonical_gi += primary.weight * reference_radiance;
            // The path walk evaluates F in its selected hybrid measure. A
            // primary-to-RC evaluation would discard a postponed glossy prefix.
            float target_pdf = target_pdf_self(candidate, pos_ws, normal_ws, view_dir, albedo, roughness, metallic);
            if (target_pdf > 0.0f)
            {
                // Endpoint MIS is already in F; the path-tree RIS weight stays separate.
                weight = target_pdf * candidate.initial_weight / (source_pdf * n_brdf);
            }
        }

        update_reservoir(reservoir, candidate, weight, random_float(seed));
    }

    // Section 6.1: all light kinds compete in the same 32-proposal RIS.
    RestirLightSample light = sample_direct_lighting_at_vertex(pos_ws, normal_ws, geometric_normal_ws,
        view_dir, albedo, roughness, metallic, 1.0f, seed, false, 1u);
    PathSample light_candidate = restir_light_endpoint_descriptor(light, restir_light_sample_origin(light, pos_ws, geometric_normal_ws), 1u, seed);
    float light_weight = 0.0f;
    if (light.endpoint_valid && restir_pdf_valid(light.endpoint_pdf))
    {
        float bsdf_pdf;
        float3 brdf = evaluate_reflection_brdf(albedo, roughness, metallic, normal_ws, geometric_normal_ws, view_dir, light.direction, bsdf_pdf, 1.0f);
        float mis = light_candidate.endpoint_pooled ?
            restir_pooled_endpoint_mis(2u, light.light_index, light.endpoint_pdf, bsdf_pdf) :
            (light_candidate.endpoint_environment ?
                restir_environment_endpoint_mis(2u, light.light_index, normal_ws, light.direction, bsdf_pdf) : 1.0f);
        light_candidate.F = brdf * light.endpoint_radiance * mis;
        float W = light.initial_weight / light.endpoint_pdf;
        light_weight = target_scalar(light_candidate.F) * W;
        canonical_gi += light.radiance_sum * n_brdf;
    }
    update_reservoir(reservoir, light_candidate, light_weight, random_float(seed));

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

    float4 t0, t1, t2, t3;
    pack_reservoir(reservoir, t0, t1, t2, t3);
    tex_reservoir0[launch_id] = t0;
    tex_reservoir1[launch_id] = t1;
    tex_reservoir2[launch_id] = t2;
    tex_reservoir3[launch_id] = t3;

    canonical_gi /= n_brdf;
    canonical_gi /= restir_gi_demodulator(albedo);

    float hit_dist = restir_denoiser_hit_distance(reservoir, pos_ws, normal_ws, geometric_normal_ws,
        view_dir, albedo, roughness, metallic);

    tex_uav[launch_id] = float4(canonical_gi, hit_dist);
    restir_specular_output[launch_id] = float4(canonical_gi * restir_primary_specular_fraction(normal_ws, view_dir, albedo, roughness, metallic), hit_dist);
}
