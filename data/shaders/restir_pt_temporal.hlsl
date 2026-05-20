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

static const float TEMPORAL_MIN_CONFIDENCE = 0.1f;

float2 reproject_to_previous_frame(float2 current_uv)
{
    float2 velocity_ndc = tex_velocity.SampleLevel(GET_SAMPLER(sampler_point_clamp), current_uv, 0).xy;
    float2 velocity_uv  = velocity_ndc * float2(0.5f, -0.5f);
    return current_uv - velocity_uv;
}

// validates temporal reprojection via surface similarity + reprojection distance + depth gate
// the depth gate compares the reprojected surface's depth at prev_uv against the expected depth
// obtained by transforming current_pos through the previous view-projection, catching disocclusions
// that screen-space normal / motion checks miss
bool is_temporal_sample_valid(
    float2 current_uv,
    float2 prev_uv,
    float3 current_pos,
    float3 current_normal,
    float2 screen_resolution,
    out float confidence)
{
    confidence = 0.0f;

    if (!is_valid_uv(prev_uv))
        return false;

    // reproject the current surface through the previous frame's transform and compare
    float4 prev_clip        = mul(float4(current_pos, 1.0f), get_view_projection_previous());
    float3 prev_ndc         = prev_clip.xyz / max(prev_clip.w, FLT_MIN);
    float2 expected_prev_uv = prev_ndc.xy * float2(0.5f, -0.5f) + 0.5f;
    float2 reproj_diff      = abs(prev_uv - expected_prev_uv) * screen_resolution;
    float  reproj_dist      = length(reproj_diff);

    float2 motion       = (current_uv - prev_uv) * screen_resolution;
    float  motion_len   = length(motion);
    float  motion_factor = saturate(motion_len / 32.0f);

    float reproj_tol = lerp(1.5f, 0.75f, motion_factor);
    if (reproj_dist > reproj_tol)
        return false;

    float normal_threshold   = lerp(0.9f, 0.97f, motion_factor);
    float3 prev_normal       = get_normal(prev_uv);
    float  normal_similarity = dot(current_normal, prev_normal);
    if (normal_similarity < normal_threshold)
        return false;

    // disocclusion gate, samples the actual previous frame depth at prev_uv (bound on tex slot
    // as gbuffer_depth_previous), reproject current_pos through the previous view-projection to
    // get the expected prior-frame depth and compare, this correctly distinguishes a moving
    // dynamic surface (matching prior depth at prev_uv) from a true disocclusion (background
    // depth behind the previous occluder), eliminating the motion ghosting that came from the
    // current-frame-against-current-frame approximation
    float prev_depth_raw = tex.SampleLevel(GET_SAMPLER(sampler_point_clamp), prev_uv, 0).r;
    if (prev_depth_raw <= 0.0f)
        return false;

    float expected_prev_depth = linearize_depth(prev_ndc.z);
    float prev_depth_linear   = linearize_depth(prev_depth_raw);
    float depth_delta         = abs(prev_depth_linear - expected_prev_depth) / max(expected_prev_depth, 1e-3f);
    float depth_limit         = lerp(0.12f, 0.04f, motion_factor);
    if (depth_delta > depth_limit)
        return false;

    float reproj_confidence = saturate(1.0f - reproj_dist / reproj_tol);
    float normal_confidence = saturate((normal_similarity - normal_threshold) / max(1.0f - normal_threshold, 1e-4f));
    float motion_confidence = saturate(1.0f - motion_len / 32.0f);
    float depth_confidence  = saturate(1.0f - depth_delta / depth_limit);
    confidence = reproj_confidence * normal_confidence * motion_confidence * depth_confidence;

    return confidence >= TEMPORAL_MIN_CONFIDENCE;
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

    float3 pos_ws    = get_position(uv);
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

    // target_pdf of the current stream at the shading pixel (self-shift, invalid-rc allowed)
    float target_cur = target_pdf_self(current.sample, pos_ws, normal_ws, view_dir, albedo, roughness, metallic);

    // combined reservoir seeded with the current stream
    Reservoir combined   = create_empty_reservoir();
    combined.weight_sum  = 0.0f;
    combined.M           = 0.0f;
    combined.sample      = current.sample;
    combined.target_pdf  = target_cur;

    float2 prev_uv     = reproject_to_previous_frame(uv);
    float  temporal_confidence = 0.0f;

    bool have_temporal = false;
    Reservoir temporal = create_empty_reservoir();
    float  target_temp          = 0.0f;
    float  jacobian_temp        = 0.0f;
    float  target_cur_at_temp   = 0.0f;

    if (is_temporal_sample_valid(uv, prev_uv, pos_ws, normal_ws, buffer_frame.resolution_render, temporal_confidence))
    {
        float2 prev_pixel_f = prev_uv * resolution;
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

            if (is_reservoir_valid(temporal) && temporal.M > 0.0f && temporal.W > 0.0f)
            {
                // use the stored source primary g-buffer that was captured at the time the
                // reservoir was generated, this is the actual previous-frame primary surface
                // and is correct even for moving objects, sampling the current g-buffer at
                // prev_uv was wrong on motion and caused ghosting / inflated reconnection
                // jacobians on dynamic scenes
                float3 src_primary_pos = temporal.sample.src_pos;
                float3 src_normal_ws   = temporal.sample.src_normal;
                float3 src_albedo      = temporal.sample.src_albedo;
                float  src_roughness   = max(temporal.sample.src_roughness, 0.04f);
                float  src_metallic    = temporal.sample.src_metallic;
                float3 src_view_dir    = normalize(get_camera_position() - src_primary_pos);

                ShiftResult shift_t_to_c = try_hybrid_shift(
                    temporal.sample,
                    src_primary_pos,
                    src_normal_ws,
                    src_view_dir,
                    src_albedo,
                    src_roughness,
                    src_metallic,
                    pos_ws,
                    normal_ws,
                    view_dir,
                    albedo,
                    roughness,
                    metallic
                );

                if (shift_t_to_c.ok)
                {
                    bool visible = trace_shift_visibility(temporal.sample, pos_ws, normal_ws);
                    if (visible)
                    {
                        target_temp   = max(dot(shift_t_to_c.f_dst, float3(0.299f, 0.587f, 0.114f)), 0.0f);
                        jacobian_temp = shift_t_to_c.jacobian;

                        // backward shift (canonical sample evaluated at temporal pixel) for
                        // defensive pairwise mis. canonical's primary g-buffer is the current
                        // frame's g-buffer at uv, and temporal pixel's g-buffer is the current
                        // frame's at prev_uv (approximated, see comment above)
                        ShiftResult shift_c_to_t = try_hybrid_shift(
                            current.sample,
                            pos_ws,
                            normal_ws,
                            view_dir,
                            albedo,
                            roughness,
                            metallic,
                            src_primary_pos,
                            src_normal_ws,
                            src_view_dir,
                            src_albedo,
                            src_roughness,
                            src_metallic
                        );
                        target_cur_at_temp = shift_c_to_t.ok
                            ? max(dot(shift_c_to_t.f_dst, float3(0.299f, 0.587f, 0.114f)), 0.0f)
                            : 0.0f;

                        have_temporal = (target_temp > 0.0f);
                    }
                }
            }
        }
    }

    // scale temporal M by a confidence-gated decay: on a stable surface the temporal sample is
    // fully trusted and we converge toward the M cap quickly, but as reprojection confidence
    // drops we increase the decay so unstable history fades fast. the validity gate already
    // rejects hard mismatches outright so we never decay past the trusted band here
    if (have_temporal)
    {
        float stability  = saturate(temporal_confidence);
        float M_scale    = lerp(0.85f, RESTIR_TEMPORAL_DECAY, stability);
        temporal.M       = max(temporal.M * M_scale, 0.0f);
        clamp_reservoir_M(temporal, get_restir_m_cap());
    }

    // defensive pairwise mis with k=1 neighbor (the temporal stream):
    //   pair_denom = M_c * t_c_at_t + M_t * t_t_at_c
    //   canonical share of pair = M_c * t_c_at_t / pair_denom
    //   temporal share of pair = M_t * t_t_at_c / pair_denom
    //   m_c = (1/(k+1)) * (1 + canonical share) = (1/2) * (1 + canonical share)
    //   m_t = (1/(k+1)) * (temporal share) = (1/2) * (temporal share)
    //   this collapses to the generalized balance when canonical_share + temporal_share == 1,
    //   but the defensive base makes the estimator stable when t_c_at_t is unreliable
    float weight_cur = 0.0f;
    float weight_tmp = 0.0f;

    if (have_temporal)
    {
        float pair_denom = max(current.M * target_cur_at_temp + temporal.M * target_temp, 1e-12f);
        float canon_share = (current.M * target_cur_at_temp) / pair_denom;
        float temp_share  = (temporal.M * target_temp)        / pair_denom;

        float m_cur  = 0.5f * (1.0f + canon_share);
        float m_temp = 0.5f * temp_share;

        weight_cur = (target_cur > 0.0f) ? (m_cur  * target_cur  * current.W)                : 0.0f;
        weight_tmp = (target_temp > 0.0f) ? (m_temp * target_temp * jacobian_temp * temporal.W) : 0.0f;
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
            combined.sample     = temporal.sample;
            combined.target_pdf = target_temp;
        }
    }

    clamp_reservoir_M(combined, get_restir_m_cap());

    // lin 2022 §6.4 sample validation: every N frames a fixed subset of pixels re-traces the
    // chosen sample's primary->rc visibility ray, if rc is no longer reachable from the current
    // primary (light moved, geometry changed, occluder appeared) the reservoir is reset so we
    // do not drag a stale path across many frames, the period comes from get_restir_validation_period
    // (hardcoded to 8 frames) and the pixel hash cycles deterministically with frame index so
    // the cost is amortized to ~1/N pixels per frame
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
                combined.sample     = current.sample;
                combined.target_pdf = target_cur;
                combined.weight_sum = 0.0f;
                combined.M          = 0.0f;
                combined.W          = 0.0f;
                have_temporal       = false;
            }
        }
    }

    // finalize: W = weight_sum / p_hat_dst(Y) (no /M, m_i factors already normalized)
    float final_target = target_pdf_self(combined.sample, pos_ws, normal_ws, view_dir, albedo, roughness, metallic);
    combined.target_pdf = final_target;
    combined.W          = (final_target > 0.0f) ? (combined.weight_sum / final_target) : 0.0f;

    float w_clamp = get_w_clamp_for_sample(combined.sample);
    combined.W    = min(combined.W, w_clamp);

    combined.age        = have_temporal ? (temporal.age + 1.0f) : 0.0f;
    combined.confidence = saturate(max(current.confidence, have_temporal ? temporal.confidence * temporal_confidence : 0.0f));

    // re-stamp source primary g-buffer onto the chosen sample, the temporal combine may have
    // copied a reservoir whose chosen sample came from this pixel (canonical) or from the
    // previous frame, either way the source primary for downstream shifts is the current pixel
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

    float3 gi = shade_reservoir_path(combined, pos_ws, normal_ws, view_dir, albedo, roughness, metallic);
    if (any(isnan(gi)) || any(isinf(gi)))
        gi = float3(0, 0, 0);

    tex_uav[pixel] = float4(gi, saturate(combined.confidence));
}
