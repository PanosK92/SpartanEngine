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
static const uint  LIGHT_RIS_CANDIDATE_SAMPLES_MAX = 64;
static const float MIN_COS_AT_PRIMARY              = 1e-3f;

// power proportional light pick weight, lin 2022 6.1, all four light types are eligible
float light_pick_weight(LightParameters l)
{
    if (l.intensity <= 0.0f)
        return 0.0f;

    bool is_directional = (l.flags & (1u << 0)) != 0;
    bool is_point       = (l.flags & (1u << 1)) != 0;
    bool is_spot        = (l.flags & (1u << 2)) != 0;
    bool is_area        = (l.flags & (1u << 6)) != 0;

    float lum = max(luminance(l.color.rgb), 1e-3f);
    if (is_directional)
    {
        return l.intensity * lum;
    }
    if (is_area)
    {
        float emitter_area = max(l.area_width * l.area_height, 0.0001f);
        return l.intensity * lum * emitter_area;
    }
    if (is_point || is_spot)
    {
        // dirac local light, weight by intensity, luminance and cone fraction
        float cone_factor = 1.0f;
        if (is_spot)
        {
            float cos_outer = cos(l.angle);
            cone_factor     = max(1.0f - cos_outer, 0.05f);
        }
        return l.intensity * lum * cone_factor;
    }
    return 0.0f;
}

// total nee pick weight, the loop is o(light_count)
float compute_total_light_weight()
{
    uint light_count = (uint)buffer_frame.restir_pt_light_count;
    float total = 0.0f;
    for (uint i = 0; i < light_count; i++)
    {
        total += light_pick_weight(light_parameters[i]);
    }
    return total;
}

// importance pick pdf for a known light index, used by the brdf strategy mis denominator
float light_pick_pdf_for_index(uint light_idx, float total_weight)
{
    if (total_weight <= 0.0f)
        return 0.0f;
    return light_pick_weight(light_parameters[light_idx]) / total_weight;
}

// samples a candidate whose rc sits on an area sampled point of an emissive triangle
// returns the rc position, emitted radiance and the ris weight standing in for 1 / source pdf
PathSample sample_emissive_tri_candidate(
    float3 primary_pos,
    float3 primary_normal,
    inout uint seed,
    out float ris_weight)
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
    s.src_albedo      = float3(0, 0, 0);
    s.src_roughness   = 1.0f;
    s.src_metallic    = 0.0f;
    s.seed_path       = seed;
    s.path_length     = 2;
    s.rc_length       = 2;
    s.flags           = 0;
    ris_weight        = 0.0f;

    // ris over cdf draws, see emtri_ris_pick, the unshadowed geometry term picks the panel
    float3 light_pos, light_normal, emission;
    float  W;
    if (!emtri_ris_pick(primary_pos, primary_normal, seed, light_pos, light_normal, emission, W))
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
    s.rc_L_nee  = emission;
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

    // power proportional pick, lin 2022 6.1, consistent with light_pick_pdf_for_index
    float total_weight = compute_total_light_weight();
    if (total_weight <= 0.0f)
        return s;

    float xi  = random_float(seed) * total_weight;
    float cum = 0.0f;
    uint  light_idx = light_count - 1;
    for (uint i = 0; i < light_count; i++)
    {
        cum += light_pick_weight(light_parameters[i]);
        if (xi <= cum)
        {
            light_idx = i;
            break;
        }
    }

    LightParameters light = light_parameters[light_idx];
    if (light.intensity <= 0.0f)
        return s;

    bool is_directional = (light.flags & (1u << 0)) != 0;
    bool is_area        = (light.flags & (1u << 6)) != 0;
    float pick_pdf      = light_pick_pdf_for_index(light_idx, total_weight);
    if (pick_pdf <= 0.0f)
        return s;

    if (is_directional)
    {
        // continuous sun cone, sole owner of the sun since sky reads exclude the disc
        float3 sun_dir = -light.direction;
        if (dot(sun_dir, primary_normal) <= MIN_COS_AT_PRIMARY)
            return s;

        float  sun_cos_max  = cos(SUN_CONE_HALF_ANGLE);
        float  sun_cone_pdf = 1.0f / (2.0f * PI * (1.0f - sun_cos_max));

        float2 xi      = random_float2(seed);
        float  phi     = 2.0f * PI * xi.x;
        float  cos_th  = lerp(sun_cos_max, 1.0f, xi.y);
        float  sin_th  = sqrt(max(0.0f, 1.0f - cos_th * cos_th));
        float3 local   = float3(cos(phi) * sin_th, sin(phi) * sin_th, cos_th);
        float3 sampled = local_to_world(local, sun_dir);

        if (dot(sampled, primary_normal) <= MIN_COS_AT_PRIMARY)
            return s;

        // no continuation past rc, rc_L_post stays zero
        // directional intensity is irradiance, scaling by 1/solid_angle turns it into cone radiance so the estimator integrates back to the authored energy, matches the delta sun in direct_lighting_at_vertex
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
        float3 light_right, light_up;
        build_orthonormal_basis_fast(light_normal, light_right, light_up);

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

        if (is_spot)
        {
            float cos_angle = dot(-dir, light.direction);
            float cos_outer = cos(light.angle);
            float cos_inner = cos(light.angle * 0.9f);
            float spot      = saturate((cos_angle - cos_outer) / max(cos_inner - cos_outer, 1e-4f));
            attenuation    *= spot * spot;
            if (attenuation <= 0.0f)
            {
                return s;
            }
        }

        s.rc_pos      = light.position;
        s.rc_normal   = -dir;
        s.rc_L_nee    = light.color.rgb * light.intensity * attenuation;
        s.rc_L_post   = float3(0, 0, 0);
        s.flags      |= PATH_FLAG_HAS_RC | PATH_FLAG_NEE;

        // unit solid angle pdf keeps dirac candidates on the same scale as the area branch
        source_pdf = pick_pdf;
        return s;
    }

    return s;
}

[shader("raygeneration")]
void ray_gen()
{
    uint2 launch_id   = DispatchRaysIndex().xy;
    uint2 launch_size = DispatchRaysDimensions().xy;
    float2 uv = (launch_id + 0.5f) / launch_size;

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
        return;
    }

    uint seed = create_seed_for_pass(launch_id, buffer_frame.frame, 0);

    // gather primary surface properties from the g-buffer
    float3 pos_ws    = restir_primary_position(uv);
    float3 normal_ws = get_normal(uv);
    float4 material  = tex_material.SampleLevel(GET_SAMPLER(sampler_point_clamp), uv, 0);
    float3 albedo    = saturate(tex_albedo.SampleLevel(GET_SAMPLER(sampler_point_clamp), uv, 0).rgb);
    float  roughness = max(material.r, 0.04f);
    float  metallic  = material.g;
    float3 view_dir  = normalize(get_camera_position() - pos_ws);

    Reservoir reservoir = create_empty_reservoir();
    float3 canonical_gi = float3(0, 0, 0);

    // direct lighting stays in the clustered renderer, restir adds indirect paths only
    // per sample weight is lin 2022 algorithm 1, w_i = target / sum_t(N_t * p_t(y))
    const uint  n_brdf_count  = clamp(get_restir_initial_candidates(), 1u, INITIAL_CANDIDATE_SAMPLES_MAX);
    const uint  n_light_count = 0u;
    const uint  n_emtri_count = is_emtri_pool_active() ? clamp(get_restir_emtri_candidates(), 1u, LIGHT_RIS_CANDIDATE_SAMPLES_MAX) : 0u;
    const float n_brdf        = float(n_brdf_count);
    const float n_light       = float(n_light_count);
    const float n_emtri       = float(n_emtri_count);

    for (uint i = 0; i < n_brdf_count; i++)
    {
        // capture the seed before xi so the replay shift can replay the same primary sample
        // primary specular is owned by rt reflections, restir samples only the diffuse lobe
        uint   replay_seed = seed;
        float2 xi          = random_float2(seed);
        float  source_pdf;
        float3 dir         = sample_brdf(albedo, roughness, metallic, normal_ws, view_dir, xi, source_pdf, restir_primary_specular_blend(roughness));

        bool dir_valid = (source_pdf >= RESTIR_MIN_PDF) &&
                         (dot(dir, normal_ws) >= MIN_COS_AT_PRIMARY) &&
                         !any(isnan(dir));

        PathSample candidate = (PathSample)0;
        float weight = 0.0f;

        if (dir_valid)
        {
            float3 reference_radiance;
            candidate = trace_path_from_primary(pos_ws, normal_ws, dir, source_pdf, replay_seed, seed, reference_radiance, false, (PathSample)0);
            // Independent path-tracing estimate: sum the traced light contributions,
            // without selecting a path endpoint or doing spatiotemporal reuse.
            canonical_gi += eval_surface_brdf_cos(albedo, roughness, metallic, normal_ws,
                view_dir, dir, restir_primary_specular_blend(roughness)) * reference_radiance / source_pdf;
            candidate.F = evaluate_path_integrand(candidate, pos_ws, normal_ws, view_dir, albedo, roughness, metallic).f_dst;
            float target_pdf = target_pdf_self(candidate, pos_ws, normal_ws, view_dir, albedo, roughness, metallic);
            if (target_pdf > 0.0f)
            {
                // single strategy weight, the sun free sky is only reachable through brdf sampling
                weight = target_pdf * candidate.initial_weight / (n_brdf * source_pdf);
            }
        }

        update_reservoir(reservoir, candidate, weight, random_float(seed));
    }

    // visibility belongs in the nee target, an occluded sun candidate has a cone radiance that
    // dwarfs every bounce target so without it the sun wins the reservoir on shadowed pixels and
    // a post ris visibility kill would then discard the valid brdf bounce samples with it,
    // zeroing all indirect light in shadow, brdf candidates carry visibility by construction
    // (their rc was found by an actual ray) so the target stays a single function of the path,
    // the sun ray is traced once since its direction is pixel constant across cone candidates
    bool sun_checked = false;
    bool sun_visible = false;

    // additional ris stream over direct light samples, sun cone and area lights
    for (uint li = 0; li < n_light_count; li++)
    {
        float light_source_pdf;
        PathSample light_candidate = sample_light_candidate(pos_ws, normal_ws, seed, light_source_pdf);

        float light_weight = 0.0f;
        if (light_source_pdf >= RESTIR_MIN_PDF)
        {
            light_candidate.F = evaluate_path_integrand(light_candidate, pos_ws, normal_ws, view_dir, albedo, roughness, metallic).f_dst;
            float target_pdf = target_pdf_self(light_candidate, pos_ws, normal_ws, view_dir, albedo, roughness, metallic);
            if (target_pdf > 0.0f)
            {
                bool visible;
                if (is_sky_sample(light_candidate))
                {
                    if (!sun_checked)
                    {
                        sun_visible = trace_shift_visibility(light_candidate, pos_ws, normal_ws);
                        sun_checked = true;
                    }
                    visible = sun_visible;
                }
                else
                {
                    visible = trace_shift_visibility(light_candidate, pos_ws, normal_ws);
                }

                if (visible)
                {
                    // single strategy weight, analytic lights are not in the bvh and the sun disc
                    // is excluded from every sky read so no other strategy reaches this integrand
                    light_weight = target_pdf / (n_light * light_source_pdf);
                    ShiftResult evaluated = self_shift_evaluate(light_candidate, pos_ws, normal_ws,
                        view_dir, albedo, roughness, metallic);
                    canonical_gi += evaluated.f_dst * n_brdf / (n_light * light_source_pdf);
                }
            }
        }

        update_reservoir(reservoir, light_candidate, light_weight, random_float(seed));
    }

    // emissive triangle nee strategy, area sampling of the global emissive pool
    // single strategy weight, while the pool is active brdf paths zero their rc emission in
    // the path tree so emtri is the only technique carrying this contribution,
    // mixing in the brdf density here would shrink the weights and lose emissive energy
    for (uint ei = 0; ei < n_emtri_count; ei++)
    {
        float emtri_ris_weight;
        PathSample emtri_candidate = sample_emissive_tri_candidate(pos_ws, normal_ws, seed, emtri_ris_weight);

        float emtri_weight = 0.0f;
        if (emtri_ris_weight > 0.0f)
        {
            emtri_candidate.F = evaluate_path_integrand(emtri_candidate, pos_ws, normal_ws, view_dir, albedo, roughness, metallic).f_dst;
            float target_pdf = target_pdf_self(emtri_candidate, pos_ws, normal_ws, view_dir, albedo, roughness, metallic);
            if (target_pdf > 0.0f && trace_shift_visibility(emtri_candidate, pos_ws, normal_ws))
            {
                // the ris weight is the unbiased stand in for 1 / source pdf of the pick
                emtri_weight = target_pdf * emtri_ris_weight / n_emtri;
                ShiftResult evaluated = self_shift_evaluate(emtri_candidate, pos_ws, normal_ws,
                    view_dir, albedo, roughness, metallic);
                canonical_gi += evaluated.f_dst * emtri_ris_weight * n_brdf / n_emtri;
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

    // real reconnection distance in w so reblur can size its kernels, sky gets the far band,
    // rc_pos holds a unit direction for sky samples so it must not be treated as a position
    float hit_dist = 0.0f;
    if (reservoir.M > 0.0f)
    {
        hit_dist = is_sky_sample(reservoir.sample) ? 10000.0f : min(length(reservoir.sample.rc_pos - pos_ws), 10000.0f);
    }

    tex_uav[launch_id] = float4(canonical_gi, hit_dist);
}
