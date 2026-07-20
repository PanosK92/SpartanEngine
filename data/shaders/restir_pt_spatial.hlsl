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

    float2 uv = (pixel + 0.5f) / resolution;

    float depth = tex_depth.SampleLevel(GET_SAMPLER(sampler_point_clamp), uv, 0).r;
    if (depth <= 0.0f)
        return;

    float linear_depth = linearize_depth(depth);
    float3 pos_ws      = get_position(uv);
    float3 normal_ws   = get_normal(uv);
    float3 view_dir    = normalize(get_camera_position() - pos_ws);
    float4 material    = tex_material.SampleLevel(GET_SAMPLER(sampler_point_clamp), uv, 0);
    float3 albedo      = saturate(tex_albedo.SampleLevel(GET_SAMPLER(sampler_point_clamp), uv, 0).rgb);
    float  roughness   = max(material.r, 0.04f);
    float  metallic    = material.g;

    Reservoir center = unpack_reservoir(
        tex_reservoir_prev0[pixel],
        tex_reservoir_prev1[pixel],
        tex_reservoir_prev2[pixel],
        tex_reservoir_prev3[pixel],
        tex_reservoir_prev4[pixel],
        tex_reservoir_prev5[pixel]
    );

    if (!is_reservoir_valid(center))
        center = create_empty_reservoir();

    uint seed = create_seed_for_pass(pixel, buffer_frame.frame, 2);
    float center_confidence = saturate(center.confidence);

    // own domain target from the previous pass finalize at this same pixel, re-evaluating
    // would mismatch center.W for replay carried samples since W = weight_sum / stored target
    float target_cur = max(center.target_pdf, 0.0f);

    // defensive pairwise mis, bitterli 2020, lin 2022 algorithm 3
    // each pair uses both shift directions, a defensive canonical share keeps it stable when neighbors are bad
    Reservoir combined  = create_empty_reservoir();
    combined.sample     = center.sample;
    combined.target_pdf = target_cur;

    float center_M = max(center.M, 0.0f);

    float canonical_pair_acc = 0.0f;
    uint  valid_neighbors    = 0;

    // collected per neighbor so the canonical defensive share is known before the streaming insert
    PathSample stream_samples [RESTIR_PAIRING_COUNT];
    float      stream_target  [RESTIR_PAIRING_COUNT];
    float      stream_jacobian[RESTIR_PAIRING_COUNT];
    float      stream_W       [RESTIR_PAIRING_COUNT];
    float      stream_share   [RESTIR_PAIRING_COUNT];
    // rgb integrand per neighbor for vector shading weights, lin 2026 6.3
    float3     stream_f       [RESTIR_PAIRING_COUNT];

    float confidence_acc = center_M * center_confidence;
    float confidence_w   = center_M;
    float M_total        = center_M;

    for (uint t = 0; t < RESTIR_PAIRING_COUNT; t++)
    {
        int2 neighbor_pixel = restir_pairing_partner(pixel, t);

        if (!is_neighbor_gbuffer_compatible(neighbor_pixel, pos_ws, normal_ws, linear_depth, resolution))
            continue;

        // forward shift, the partner's path evaluated at this pixel, visibility already traced
        float4 forward = read_shift(t, neighbor_pixel);
        if (forward.a <= 0.0f)
            continue;

        float target_j_at_c = target_scalar(forward.rgb);
        if (target_j_at_c <= 0.0f)
            continue;

        Reservoir neighbor = unpack_reservoir(
            tex_reservoir_prev0[neighbor_pixel],
            tex_reservoir_prev1[neighbor_pixel],
            tex_reservoir_prev2[neighbor_pixel],
            tex_reservoir_prev3[neighbor_pixel],
            tex_reservoir_prev4[neighbor_pixel],
            tex_reservoir_prev5[neighbor_pixel]
        );

        if (!is_reservoir_valid(neighbor) || neighbor.M <= 0.0f || neighbor.W <= 0.0f)
            continue;

        // confidence feeds the m-weighted merge but does not gate participation
        float neighbor_confidence = saturate(neighbor.confidence);

        // backward shift, own sample evaluated at the partner, for pairwise mis, a failed
        // backward shift collapses the pair share to full canonical weight which stays unbiased
        float4 backward       = read_shift(t, int2(pixel));
        float target_c_at_j   = backward.a > 0.0f ? target_scalar(backward.rgb) : 0.0f;
        float jacobian_c_to_j = backward.a > 0.0f ? backward.a                  : 0.0f;
        float jacobian_j_to_c = forward.a;

        // pairwise mis shares, lin 2022 5.2, each share evaluates one sample under both
        // techniques, own domain target in the numerator, shifted target times the shift
        // jacobian in the other denominator term

        // canonical sample x_c under canonical and neighbor densities
        float canon_denom = center_M * target_cur + neighbor.M * target_c_at_j * jacobian_c_to_j;
        canonical_pair_acc += (canon_denom > 0.0f) ? (center_M * target_cur) / canon_denom : 1.0f;

        // neighbor sample x_j under neighbor and canonical densities, own domain target is
        // stored in the neighbor reservoir from its own finalize
        float target_j_own = max(neighbor.target_pdf, 0.0f);
        float neigh_denom  = neighbor.M * target_j_own + center_M * target_j_at_c * jacobian_j_to_c;
        float neigh_share  = (neigh_denom > 0.0f) ? (neighbor.M * target_j_own) / neigh_denom : 0.0f;

        stream_samples [valid_neighbors] = neighbor.sample;
        stream_target  [valid_neighbors] = target_j_at_c;
        stream_jacobian[valid_neighbors] = jacobian_j_to_c;
        stream_W       [valid_neighbors] = neighbor.W;
        stream_share   [valid_neighbors] = neigh_share;
        stream_f       [valid_neighbors] = forward.rgb;
        valid_neighbors++;

        M_total        += neighbor.M;
        confidence_acc += neighbor.M * neighbor_confidence;
        confidence_w   += neighbor.M;
    }

    // defensive pairwise mis weights, k == 0 gives the canonical the full weight
    float k_inv = 1.0f / float(valid_neighbors + 1);

    float weight_c = k_inv * (1.0f + canonical_pair_acc) * target_cur * center.W;
    combined.weight_sum = max(weight_c, 0.0f);

    // gris streaming weights, w_j = m_j * p_hat * W_j * jacobian, jacobian appears once here
    for (uint j = 0; j < valid_neighbors; j++)
    {
        float target_at_c = stream_target[j];
        float m_j         = k_inv * stream_share[j];
        float weight_j    = max(m_j * target_at_c * stream_W[j] * stream_jacobian[j], 0.0f);

        combined.weight_sum += weight_j;

        if (combined.weight_sum > 0.0f && random_float(seed) * combined.weight_sum < weight_j)
        {
            combined.sample     = stream_samples[j];
            combined.target_pdf = target_at_c;
        }
    }

    combined.M = M_total;
    clamp_reservoir_M(combined, get_restir_m_cap());

    // lin 2022 6.4 sample validation, kills stale paths that survive purely through spatial reuse
    bool validation_reset  = false;
    uint validation_period = get_restir_validation_period();
    if (validation_period > 0u && combined.M > 0.0f && combined.W > 0.0f)
    {
        uint hash = (pixel.x * 73856093u) ^ (pixel.y * 19349663u);
        uint slot = (buffer_frame.frame + hash) % validation_period;
        if (slot == 0u)
        {
            bool reachable = trace_shift_visibility(combined.sample, pos_ws, normal_ws);
            if (!reachable)
            {
                combined            = create_empty_reservoir();
                combined.sample     = center.sample;
                combined.target_pdf = target_cur;
                combined.weight_sum = 0.0f;
                combined.M          = 0.0f;
                combined.W          = 0.0f;
                validation_reset    = true;
            }
        }
    }

    // finalize, W = weight_sum / target, no /M since the m_i factors are already normalized
    // the selection time target is reused instead of a re-evaluation, for replay shifted
    // samples the two evaluation paths differ slightly and a mismatched divide skews W
    combined.W = (combined.target_pdf > 0.0f) ? (combined.weight_sum / combined.target_pdf) : 0.0f;

    // soft saturator, see soft_clamp_w in restir_reservoir.hlsl
    float w_clamp   = get_w_clamp_for_sample(combined.sample);
    float w_unclamped = combined.W;
    combined.W      = soft_clamp_w(combined.W, w_clamp);

    // m-weighted confidence across merged streams, blended with the center
    float merged_confidence = (confidence_w > 0.0f) ? (confidence_acc / confidence_w) : center_confidence;
    combined.confidence = saturate(max(center_confidence, merged_confidence));
    combined.age        = center.age;

    // re-stamp the source primary g-buffer, the combine may have copied a neighbor src_*
    combined.sample.src_pos       = pos_ws;
    combined.sample.src_normal    = normal_ws;
    combined.sample.src_albedo    = albedo;
    combined.sample.src_roughness = roughness;
    combined.sample.src_metallic  = metallic;

    float4 t0, t1, t2, t3, t4, t5;
    pack_reservoir(combined, t0, t1, t2, t3, t4, t5);
    tex_reservoir0[pixel] = t0;
    tex_reservoir1[pixel] = t1;
    tex_reservoir2[pixel] = t2;
    tex_reservoir3[pixel] = t3;
    tex_reservoir4[pixel] = t4;
    tex_reservoir5[pixel] = t5;

    // vector resampling weights for shading, lin 2026 6.3, gi = sum_i m_i f_i W_i J_i in rgb
    // scalar weights keep driving resampling while the rgb sum averages out the chroma noise
    // that a luminance only target cannot importance sample
    float3 gi = float3(0, 0, 0);
    if (!validation_reset)
    {
        ShiftResult center_self = self_shift_evaluate(center.sample, pos_ws, normal_ws, view_dir, albedo, roughness, metallic);
        gi = k_inv * (1.0f + canonical_pair_acc) * center_self.f_dst * max(center.W, 0.0f);

        for (uint v = 0; v < valid_neighbors; v++)
        {
            gi += (k_inv * stream_share[v]) * stream_f[v] * max(stream_W[v], 0.0f) * stream_jacobian[v];
        }

        // apply the same firefly suppression ratio the scalar W received from the soft clamp
        if (w_unclamped > 1e-8f)
        {
            gi *= combined.W / w_unclamped;
        }

        // diffuse albedo demodulation and firefly ceiling, matches shade_reservoir_path so the composition re-modulation applies albedo exactly once
        gi = gi / max(albedo, 0.1f);
        gi = soft_saturate_radiance(gi, get_restir_w_clamp() * 0.05f);
    }

    if (any(isnan(gi)) || any(isinf(gi)))
    {
        gi = float3(0, 0, 0);
    }

    if (all(gi <= 1e-6f))
    {
        gi = tex_uav[pixel].rgb;
    }

    // real reconnection distance in w so reblur can size its kernels, sky gets the far band
    float hit_dist = 0.0f;
    if (combined.W > 0.0f)
    {
        hit_dist = is_sky_sample(combined.sample) ? 10000.0f : min(length(combined.sample.rc_pos - pos_ws), 10000.0f);
    }

    // emissive nee and indirect lighting are already carried by the reservoir
    tex_uav[pixel] = float4(gi, hit_dist);
}
