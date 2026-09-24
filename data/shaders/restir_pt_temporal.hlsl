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

static const float TEMPORAL_MIN_CONFIDENCE = 0.1f;

float2 reproject_to_previous_frame(float2 current_uv)
{
    float2 velocity_ndc = tex_velocity.SampleLevel(GET_SAMPLER(sampler_point_clamp), current_uv, 0).xy;
    float2 velocity_uv  = velocity_ndc * float2(0.5f, -0.5f);
    // The reservoir and G-buffer histories live on jittered pixel grids, while
    // velocity deliberately excludes jitter. Put history UVs on the prior grid.
    float2 jitter_delta = (buffer_frame.taa_jitter_previous - buffer_frame.taa_jitter_current) * float2(0.5f, -0.5f);
    return current_uv - velocity_uv + jitter_delta;
}

// validates temporal reprojection, delegated to the shared evaluate_disocclusion helper
bool is_temporal_sample_valid(
    float2 current_uv,
    float2 prev_uv,
    float3 current_pos,
    float3 current_normal,
    float2 screen_resolution,
    out float confidence)
{
    bool ok = evaluate_disocclusion(
        tex,
        tex5,
        current_uv,
        prev_uv,
        current_pos,
        current_normal,
        screen_resolution,
        1.5f, 0.75f,  // reproj tol min/max in pixels
        0.9f, 0.97f,  // normal threshold min/max
        0.12f, 0.04f, // relative depth delta min/max
        32.0f,        // motion length reference in pixels
        confidence
    );
    return ok && confidence >= TEMPORAL_MIN_CONFIDENCE;
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

    float3 pos_ws    = restir_primary_position(uv);
    float3 normal_ws = get_normal(uv);
    float3 view_dir  = normalize(get_camera_position() - pos_ws);
    float4 material  = tex_material.SampleLevel(GET_SAMPLER(sampler_point_clamp), uv, 0);
    float3 albedo    = saturate(tex_albedo.SampleLevel(GET_SAMPLER(sampler_point_clamp), uv, 0).rgb);
    float  roughness = max(material.r, 0.04f);
    float  metallic  = material.g;

    Reservoir current = unpack_reservoir(
        tex_reservoir0[pixel],
        tex_reservoir1[pixel],
        tex_reservoir2[pixel],
        tex_reservoir3[pixel],
        tex_reservoir4[pixel],
        tex_reservoir5[pixel]
    );

    if (!is_reservoir_valid(current))
        current = create_empty_reservoir();

    uint seed = create_seed_for_pass(pixel, buffer_frame.frame, 1);

    // own domain target from the initial pass finalize, same pixel and frame so re-evaluating
    // would only reintroduce an evaluation path mismatch for replay carried samples
    float target_cur = max(current.target_pdf, 0.0f);

    Reservoir combined   = create_empty_reservoir();
    combined.weight_sum  = 0.0f;
    combined.M           = 0.0f;
    combined.sample      = current.sample;
    combined.target_pdf  = target_cur;

    float2 prev_uv     = reproject_to_previous_frame(uv);
    float  temporal_confidence = 0.0f;

    // where the history reservoir is read from, prev_uv unless the dual motion vector takes over
    float2 history_uv   = prev_uv;
    bool   have_history = is_temporal_sample_valid(uv, prev_uv, pos_ws, normal_ws, buffer_frame.resolution_render, temporal_confidence);
    bool   dual_history = false;

    // dual motion vectors on disocclusion, lin 2026 6.4 after zeng 2021, a freshly revealed
    // pixel has no history of its own but the surface that hid it kept moving, its velocity in
    // the previous frame, read at prev_uv where it stood, says which way it came from, and the
    // background just past its earlier silhouette in that direction was visible then, so that
    // reservoir is offered as the temporal neighbour, the shift moves the path onto this primary
    // with a proper jacobian so nothing is copy pasted, only the plane gate below is relaxed to
    // the spatial neighbour tolerance since the source primary is a different surface point
    bool prev_on_screen = all(prev_uv > 0.0f) && all(prev_uv < 1.0f);
    if (!have_history && prev_on_screen)
    {
        float2 occluder_motion = tex6.SampleLevel(GET_SAMPLER(sampler_point_clamp), prev_uv, 0).xy * float2(0.5f, -0.5f);
        float2 dual_uv         = prev_uv - occluder_motion;
        // a still occluder reproduces prev_uv, which already failed
        if (dot(occluder_motion, occluder_motion) > 0.0f && all(dual_uv > 0.0f) && all(dual_uv < 1.0f))
        {
            history_uv   = dual_uv;
            have_history = true;
            dual_history = true;
        }
    }

    bool have_temporal = false;
    PathSample temporal_shifted_sample = (PathSample)0;
    Reservoir temporal = create_empty_reservoir();
    float  target_temp          = 0.0f;
    float  jacobian_temp        = 0.0f;
    float  target_cur_at_temp   = 0.0f;
    float  jacobian_cur_at_temp = 0.0f;
    // rgb integrand of the shifted temporal sample, kept for vector shading weights, lin 2026 6.3
    float3 f_temp               = float3(0, 0, 0);

    if (have_history)
    {
        float2 prev_pixel_f = history_uv * resolution;
        bool in_bounds = prev_pixel_f.x >= 0.5f && prev_pixel_f.x < resolution.x - 0.5f &&
                         prev_pixel_f.y >= 0.5f && prev_pixel_f.y < resolution.y - 0.5f;
        if (in_bounds)
        {
            int2 prev_pixel = int2(prev_pixel_f);

            temporal = unpack_reservoir(
                tex_reservoir_prev0[prev_pixel],
                tex_reservoir_prev1[prev_pixel],
                tex_reservoir_prev2[prev_pixel],
                tex_reservoir_prev3[prev_pixel],
                tex_reservoir_prev4[prev_pixel],
                tex_reservoir_prev5[prev_pixel]
            );

            // A valid zero-weight history is still a sampled technique. Dropping it based
            // on its draw biases the blend toward the histories that happened to find light.
            bool usable = is_reservoir_valid(temporal) && temporal.M > 0.0f;

            // the dual candidate skipped the reprojection gate, it is a neighbour on the same
            // surface or nothing, tested against the stored source primary like the spatial pass
            if (usable && dual_history)
            {
                float3 src_offset  = temporal.sample.src_pos - pos_ws;
                float  view_dist   = max(length(get_camera_position() - pos_ws), 1e-3f);
                float  plane_dist  = abs(dot(normal_ws, src_offset));
                bool   same_plane  = plane_dist <= RESTIR_DEPTH_THRESHOLD * view_dist;
                bool   same_facing = dot(normal_ws, temporal.sample.src_normal) >= RESTIR_NORMAL_THRESHOLD;
                usable             = same_plane && same_facing;
            }

            if (usable)
            {
                have_temporal = true;
                // use the stored source primary g-buffer, correct even for moving objects
                float3 src_primary_pos = temporal.sample.src_pos;
                float3 src_normal_ws   = temporal.sample.src_normal;
                float3 src_albedo      = temporal.sample.src_albedo;
                float  src_roughness   = max(temporal.sample.src_roughness, 0.04f);
                float  src_metallic    = temporal.sample.src_metallic;
                float3 src_view_dir    = normalize(buffer_frame.camera_position_previous - src_primary_pos);

                ShiftResult shift_t_to_c = try_reconnection_shift(
                    temporal.sample,
                    src_primary_pos,
                    pos_ws,
                    normal_ws,
                    view_dir,
                    albedo,
                    roughness,
                    metallic
                );

                if (temporal.W > 0.0f && shift_t_to_c.ok)
                {
                    bool visible = trace_shift_visibility(shift_t_to_c.sample, pos_ws, normal_ws);
                    if (visible)
                    {
                        target_temp   = target_scalar(shift_t_to_c.f_dst);
                        jacobian_temp = shift_t_to_c.jacobian;
                        f_temp        = shift_t_to_c.f_dst;
                        temporal_shifted_sample = shift_t_to_c.sample;
                    }
                }

                // Evaluate the backward density independently of the forward draw. It must
                // include visibility too, just like the spatial pre-pass in both directions.
                ShiftResult shift_c_to_t = try_reconnection_shift(
                    current.sample,
                    pos_ws,
                    src_primary_pos,
                    src_normal_ws,
                    src_view_dir,
                    src_albedo,
                    src_roughness,
                    src_metallic
                );
                if (current.W > 0.0f && shift_c_to_t.ok &&
                    trace_shift_visibility(shift_c_to_t.sample, src_primary_pos, src_normal_ws))
                {
                    target_cur_at_temp   = target_scalar(shift_c_to_t.f_dst);
                    jacobian_cur_at_temp = shift_c_to_t.jacobian;
                }
            }
        }
    }

    // cap temporal M so lighting changes are tracked, M is the c_i for the balance heuristic
    // the cap shrinks adaptively with the duplication score of the temporal neighbor, lin 2026 5,
    // correlated regions decay faster which trades a small bias for far fewer correlation blobs
    if (have_temporal)
    {
        float duplication = tex2.SampleLevel(GET_SAMPLER(sampler_point_clamp), history_uv, 0).r;
        clamp_reservoir_M(temporal, get_restir_m_cap_decorrelated(duplication));
    }

    // balance heuristic with confidence weights over the two techniques, lin 2022 5.2
    // each share evaluates one sample under both techniques' densities, own domain target in
    // the numerator, shifted target times the shift jacobian in the other denominator term
    // this is deliberately not the defensive pairwise form the spatial pass uses, defensive
    // weights floor the canonical at 0.5 which is the right trade against three sibling pixels
    // of equal confidence but ruinous here, it hands a fresh single frame estimate half the
    // image every frame no matter how deep the history is, so nothing ever settles, the plain
    // balance heuristic lets accumulated confidence reduce the canonical share
    float weight_cur = 0.0f;
    float weight_tmp = 0.0f;
    // kept in scope for the vector shading sum further down
    float m_cur      = 1.0f;
    float m_temp     = 0.0f;

    if (have_temporal)
    {
        // canonical sample x_c, own target vs the temporal technique's density at x_c
        float canon_denom = current.M * target_cur + temporal.M * target_cur_at_temp * jacobian_cur_at_temp;
        float canon_share = (canon_denom > 0.0f) ? (current.M * target_cur) / canon_denom : 1.0f;

        // temporal sample x_t, own domain target stored in the reservoir from last frame
        float target_temp_own = max(temporal.target_pdf, 0.0f);
        float temp_denom      = temporal.M * target_temp_own + current.M * target_temp * jacobian_temp;
        float temp_share      = (temp_denom > 0.0f) ? (temporal.M * target_temp_own) / temp_denom : 0.0f;

        m_cur  = canon_share;
        m_temp = temp_share;

        // gris streaming weights, w = m_i * p_hat * jacobian * W
        weight_cur = (target_cur > 0.0f) ? (m_cur  * target_cur  * current.W)                : 0.0f;
        weight_tmp = (target_temp > 0.0f) ? (m_temp * target_temp * jacobian_temp * temporal.W) : 0.0f;
        weight_tmp = max(weight_tmp, 0.0f);
    }
    else if (target_cur > 0.0f)
    {
        // no temporal candidate, canonical takes full mis mass
        weight_cur = target_cur * current.W;
    }

    combined.weight_sum = max(weight_cur, 0.0f);
    combined.M          = current.M;

    if (have_temporal)
    {
        combined.weight_sum += max(weight_tmp, 0.0f);
        combined.M          += temporal.M;

        if (combined.weight_sum > 0.0f && random_float(seed) * combined.weight_sum < weight_tmp)
        {
            combined.sample     = temporal_shifted_sample;
            combined.target_pdf = target_temp;
        }
    }

    // Confidence is summed here; only the temporal input is capped (paper section 2.2).

    // Every reused candidate was visibility-tested before RIS. The canonical
    // candidate was traced this frame. A second, post-selection rejection with
    // a canonical fallback would condition the estimator on the selected draw.

    // finalize, W = weight_sum / target, no /M since the m_i factors are already normalized
    // the selection time target is reused instead of a re-evaluation, for replay shifted
    // samples the two evaluation paths differ slightly and a mismatched divide skews W
    combined.W = (combined.target_pdf > 0.0f) ? (combined.weight_sum / combined.target_pdf) : 0.0f;

    // re-stamp the source primary g-buffer, downstream shifts originate from the current pixel
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
    // that a luminance only target cannot importance sample, both integrands are already evaluated
    float3 gi = float3(0, 0, 0);
    ShiftResult canonical = self_shift_evaluate(current.sample, pos_ws, normal_ws, view_dir, albedo, roughness, metallic);
    gi = m_cur * canonical.f_dst * max(current.W, 0.0f);

    if (have_temporal)
    {
        gi += m_temp * f_temp * max(temporal.W, 0.0f) * jacobian_temp;
    }

    // Demodulate once; composition restores the primary albedo.
    gi = gi / restir_gi_demodulator(albedo);

    if (any(isnan(gi)) || any(isinf(gi)))
    {
        gi = float3(0, 0, 0);
    }

    // real reconnection distance in w so reblur can size its kernels, sky gets the far band,
    // keyed on m not w so a clamped or zero weight sample still reports where it landed
    float hit_dist = 0.0f;
    if (combined.M > 0.0f)
    {
        hit_dist = is_sky_sample(combined.sample) ? 10000.0f : min(length(combined.sample.rc_pos - pos_ws), 10000.0f);
    }

    // fall back to the trace pass estimate only when the reservoir is genuinely empty, keying
    // this on a small gi value instead swaps in a different estimator exactly on the pixels
    // where the resampled one came out dark, which is a one sided lift of every shadowed pixel
    // and reads as boiling because the switch flips frame to frame
    if (combined.M <= 0.0f)
    {
        float4 trace_stage = tex_uav[pixel];
        gi       = trace_stage.rgb;
        hit_dist = trace_stage.a;
    }

    tex_uav[pixel] = float4(gi, hit_dist);
}
