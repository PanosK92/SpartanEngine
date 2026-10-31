/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

//= INCLUDES ===================
#include "common.hlsl"
#include "restir_reservoir.hlsl"
//==============================

// paired spatial resample, lin 2026 3, consumes the shift pre-pass so no shift mapping or
// visibility ray runs here, the forward shift (partner sample at this pixel) is read from the
// partner's texel and the backward shift (own sample at the partner) from this pixel's texel

// pre-pass shift results, one texture per pairing table, rgb = f_dst, a = jacobian (0 = failed)
float4 read_shift(uint table, int2 p)
{
    if (table == 0)
    {
        return tex[p];
    }
    if (table == 1)
    {
        return tex2[p];
    }
    return tex4[p];
}

[numthreads(THREAD_GROUP_COUNT_X, THREAD_GROUP_COUNT_Y, 1)]
void main_cs(uint3 dispatch_id : SV_DispatchThreadID)
{
    uint2 pixel = dispatch_id.xy;
    uint resolution_x, resolution_y;
    tex_uav.GetDimensions(resolution_x, resolution_y);
    float2 resolution = float2(resolution_x, resolution_y);

    if (pixel.x >= resolution_x || pixel.y >= resolution_y)
        return;

    float2 uv = restir_primary_uv((pixel + 0.5f) / resolution);

    float depth = all(pixel < uint2(resolution * get_render_uv_scale()))
        ? tex_depth.SampleLevel(GET_SAMPLER(sampler_point_clamp), uv, 0).r : 0.0f;
    if (depth <= 0.0f)
    {
        // the resample writes into a separate slot that later rotates into the temporal history,
        // leaving sky texels untouched would carry a stale reservoir forward, clear them instead
        float4 e0, e1, e2, e3;
        pack_reservoir(create_empty_reservoir(), e0, e1, e2, e3);
        tex_reservoir0[pixel] = e0;
        tex_reservoir1[pixel] = e1;
        tex_reservoir2[pixel] = e2;
        tex_reservoir3[pixel] = e3;
        return;
    }

    float linear_depth = linearize_depth(depth);
    float3 pos_ws      = restir_primary_position(uv);
    float3 normal_ws   = get_normal(uv);
    float3 geometric_normal_ws = get_geometric_normal(uv);
    float3 view_dir    = normalize(get_camera_position() - pos_ws);
    float4 material    = get_material(uv);
    float3 albedo      = saturate(tex_albedo.SampleLevel(GET_SAMPLER(sampler_point_clamp), uv, 0).rgb);
    float  roughness   = max(material.r, 0.04f);
    float  metallic    = material.g;

    Reservoir center = unpack_reservoir(
        tex_reservoir_prev0[pixel],
        tex_reservoir_prev1[pixel],
        tex_reservoir_prev2[pixel],
        tex_reservoir_prev3[pixel],
        restir_primary_surface(uv),
        restir_primary_material(uv)
    );

    if (!is_reservoir_valid(center))
        center = create_empty_reservoir();

    uint seed = create_seed_for_pass(pixel, buffer_frame.frame, 2);

    // own domain target from the previous pass finalize at this same pixel, re-evaluating
    // would mismatch center.W for replay carried samples since W = weight_sum / stored target
    float target_cur = max(center.target_pdf, 0.0f);

    // defensive pairwise mis, bitterli 2020, lin 2022 algorithm 3
    // each pair uses both shift directions, a defensive canonical share keeps it stable when neighbors are bad
    Reservoir combined  = create_empty_reservoir();
    combined.sample     = center.sample;
    combined.target_pdf = target_cur;

    float center_M = max(center.M, 0.0f);

    uint  valid_neighbors    = 0;

    // collected per neighbor so the canonical defensive share is known before the streaming insert
    PathSample stream_samples [RESTIR_PAIRING_COUNT];
    float      stream_target  [RESTIR_PAIRING_COUNT];
    float      stream_jacobian[RESTIR_PAIRING_COUNT];
    float      stream_W       [RESTIR_PAIRING_COUNT];
    float      stream_share   [RESTIR_PAIRING_COUNT];
    float      stream_M       [RESTIR_PAIRING_COUNT];
    float      stream_own_target[RESTIR_PAIRING_COUNT];
    float      stream_backward_target[RESTIR_PAIRING_COUNT];
    // rgb integrand per neighbor for vector shading weights, lin 2026 6.3
    float3     stream_f       [RESTIR_PAIRING_COUNT];

    float M_total = center_M;

    for (uint t = 0; t < RESTIR_PAIRING_COUNT; t++)
    {
        int2 neighbor_pixel = restir_pairing_partner(pixel, t);

        if (!is_neighbor_gbuffer_compatible(neighbor_pixel, pos_ws, normal_ws, linear_depth, resolution))
            continue;

        // Keep every compatible technique, including a zero or occluded draw. Selecting the
        // neighbor set by its sampled radiance gives lucky bright paths extra MIS mass.
        // forward shift, the partner's path evaluated at this pixel, visibility already traced
        float4 forward = read_shift(t, neighbor_pixel);
        float target_j_at_c = forward.a > 0.0f ? target_scalar(forward.rgb) : 0.0f;

        float2 neighbor_uv = restir_primary_uv((neighbor_pixel + 0.5f) / resolution);
        Reservoir neighbor = unpack_reservoir(
            tex_reservoir_prev0[neighbor_pixel],
            tex_reservoir_prev1[neighbor_pixel],
            tex_reservoir_prev2[neighbor_pixel],
            tex_reservoir_prev3[neighbor_pixel],
            restir_primary_surface(neighbor_uv),
            restir_primary_material(neighbor_uv)
        );

        if (!is_reservoir_valid(neighbor) || neighbor.M <= 0.0f)
            continue;

        // backward shift, own sample evaluated at the partner, for pairwise mis, a failed
        // backward shift collapses the pair share to full canonical weight which stays unbiased
        float4 backward       = read_shift(t, int2(pixel));
        float target_c_at_j   = backward.a > 0.0f ? target_scalar(backward.rgb) : 0.0f;
        float jacobian_c_to_j = backward.a > 0.0f ? backward.a                  : 0.0f;
        float jacobian_j_to_c = max(forward.a, 0.0f);

        // Gather every technique before computing the confidence-weighted
        // defensive pairwise shares. Their denominators need the sum of all
        // neighbor confidences, not this neighbor's M alone.
        stream_samples [valid_neighbors] = neighbor.sample;
        stream_samples [valid_neighbors].src_pos = restir_primary_position((neighbor_pixel + 0.5f) / resolution);
        stream_target  [valid_neighbors] = target_j_at_c;
        stream_jacobian[valid_neighbors] = jacobian_j_to_c;
        stream_W       [valid_neighbors] = neighbor.W;
        stream_M       [valid_neighbors] = neighbor.M;
        stream_own_target[valid_neighbors] = max(neighbor.target_pdf, 0.0f);
        stream_backward_target[valid_neighbors] = target_c_at_j * jacobian_c_to_j;
        stream_f       [valid_neighbors] = forward.rgb;
        valid_neighbors++;

        M_total += neighbor.M;
    }

    // ReSTIR PT Enhanced reference: C.c_sum is all compatible techniques'
    // confidence, C.c_c is the canonical confidence. The defensive canonical
    // share starts at C.c_c / C.c_sum; each pair receives M_j / C.c_sum.
    float neighbor_M = max(M_total - center_M, 0.0f);
    float canonical_share = M_total > 0.0f ? center_M / M_total : 1.0f;
    for (uint p = 0u; p < valid_neighbors; ++p)
    {
        float pair_share = stream_M[p] / M_total;
        float canonical_term = center_M * target_cur;
        float canonical_denom = canonical_term + neighbor_M * stream_backward_target[p];
        float canonical_pair_share = canonical_denom > 0.0f ? canonical_term / canonical_denom : 1.0f;
        canonical_share += pair_share * canonical_pair_share;

        float neighbor_term = neighbor_M * stream_own_target[p];
        float neighbor_denom = neighbor_term + center_M * stream_target[p] * stream_jacobian[p];
        stream_share[p] = neighbor_denom > 0.0f ? pair_share * neighbor_term / neighbor_denom : 0.0f;
    }

    float weight_c = canonical_share * target_cur * center.W;
    combined.weight_sum = max(weight_c, 0.0f);

    // gris streaming weights, w_j = m_j * p_hat * W_j * jacobian, jacobian appears once here
    bool picked_neighbor = false;
    for (uint j = 0; j < valid_neighbors; j++)
    {
        float target_at_c = stream_target[j];
        float m_j         = stream_share[j];
        float weight_j    = max(m_j * target_at_c * stream_W[j] * stream_jacobian[j], 0.0f);

        combined.weight_sum += weight_j;

        if (combined.weight_sum > 0.0f && random_float(seed) * combined.weight_sum < weight_j)
        {
            combined.sample     = stream_samples[j];
            combined.sample.F   = stream_f[j];
            combined.target_pdf = target_at_c;
            picked_neighbor     = true;
        }
    }

    // The prepass stores F/J only. Materialize the winning shift so replayed
    // vertices and the RC outgoing density match the destination integrand.
    if (picked_neighbor)
    {
        ShiftResult shifted = try_reconnection_shift(combined.sample, combined.sample.src_pos,
            pos_ws, normal_ws, view_dir, albedo, roughness, metallic,
            normalize(get_camera_position() - combined.sample.src_pos), geometric_normal_ws);
        combined.sample = shifted.sample;
    }

    combined.M = M_total;
    // Confidence is summed here; only the temporal input is capped (paper section 2.2).

    // Every reused candidate was visibility-tested before RIS. The canonical
    // candidate was traced this frame. A second, post-selection rejection with
    // a canonical fallback would condition the estimator on the selected draw.

    // finalize, W = weight_sum / target, no /M since the m_i factors are already normalized
    // the selection time target is reused instead of a re-evaluation, for replay shifted
    // samples the two evaluation paths differ slightly and a mismatched divide skews W
    combined.W = (combined.target_pdf > 0.0f) ? (combined.weight_sum / combined.target_pdf) : 0.0f;

    // re-stamp the source primary g-buffer, the combine may have copied a neighbor src_*
    combined.sample.src_pos       = pos_ws;
    combined.sample.src_normal    = normal_ws;
    combined.sample.src_normal_roughness = (tex_normal[gbuffer_texel(uv)].x & 0x00ffffffu) |
        ((tex_material[gbuffer_texel(uv)] & 255u) << 24u);
    combined.sample.src_albedo    = albedo;
    combined.sample.src_roughness = roughness;
    combined.sample.src_metallic  = metallic;

    float4 t0, t1, t2, t3;
    pack_reservoir(combined, t0, t1, t2, t3);
    tex_reservoir0[pixel] = t0;
    tex_reservoir1[pixel] = t1;
    tex_reservoir2[pixel] = t2;
    tex_reservoir3[pixel] = t3;

    // vector resampling weights for shading, lin 2026 6.3, gi = sum_i m_i f_i W_i J_i in rgb
    // scalar weights keep driving resampling while the rgb sum averages out the chroma noise
    // that a luminance only target cannot importance sample
    // Preserve the initial in-lobe distance; the RIS winner is not a reflection guide.
    float specular_hit_dist = restir_specular_output[pixel].a;
    float3 gi = float3(0, 0, 0);
    ShiftResult center_self = self_shift_evaluate(center.sample, pos_ws, normal_ws, view_dir, albedo, roughness, metallic);
    gi = canonical_share * center_self.f_dst * max(center.W, 0.0f);
    float3 specular = gi * restir_primary_specular_fraction(center.sample, pos_ws, normal_ws,
        view_dir, albedo, roughness, metallic);

    for (uint v = 0; v < valid_neighbors; v++)
    {
        float3 contribution = stream_share[v] * stream_f[v] * max(stream_W[v], 0.0f) * stream_jacobian[v];
        gi += contribution;
        specular += contribution * restir_primary_specular_fraction(stream_samples[v], pos_ws, normal_ws,
            view_dir, albedo, roughness, metallic);
    }

    // Demodulate once; composition restores the primary albedo.
    gi = gi / restir_gi_demodulator(albedo);

    if (any(isnan(gi)) || any(isinf(gi)))
    {
        gi = float3(0, 0, 0);
    }

    // Reuse changes radiance, not the current primary's first-hit guide.
    float hit_dist = tex_uav[pixel].a;

    // fall back to the temporal stage estimate only when the reservoir is genuinely empty, see
    // the matching comment in restir_pt_temporal, a value keyed fallback lifts every dark pixel
    // and flips estimators frame to frame
    if (combined.M <= 0.0f)
    {
        float4 temporal_stage = tex_uav[pixel];
        gi       = temporal_stage.rgb;
        hit_dist = temporal_stage.a;
    }

    // emissive nee and indirect lighting are already carried by the reservoir
    tex_uav[pixel] = float4(gi, hit_dist);
    restir_specular_output[pixel] = float4(clamp(specular, 0.0f, gi), specular_hit_dist);
}
