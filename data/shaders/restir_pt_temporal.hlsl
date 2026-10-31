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

Texture2D<uint2> tex_normal_previous : register(t87);
Texture2D<float4> restir_primary_hit_previous : register(t94);

static const float TEMPORAL_MIN_CONFIDENCE = 0.1f;

float2 reproject_to_previous_frame(float2 current_uv, bool reverse_motion = false)
{
    float2 velocity_ndc = tex_velocity.SampleLevel(GET_SAMPLER(sampler_point_clamp), current_uv, 0).xy;
    float2 velocity_uv  = velocity_ndc * float2(0.5f, -0.5f);
    // The reservoir and G-buffer histories live on jittered pixel grids, while
    // velocity deliberately excludes jitter. Put history UVs on the prior grid.
    float2 jitter_delta = (buffer_frame.taa_jitter_previous - buffer_frame.taa_jitter_current) * float2(0.5f, -0.5f);
    // both deltas are screen uv, current_uv is a render uv in the scaled subrect
    return current_uv + (jitter_delta + (reverse_motion ? velocity_uv : -velocity_uv)) * get_render_uv_scale();
}

// Zeng et al. (2021), Eq. 6, used by Lin 2026 section 6.4:
// y is the old occluder's pixel, z is that triangle's CURRENT projected position.
// Retained hit identity provides z even when the occluder has left the screen.
bool find_dual_history_uv(float2 current_uv, float2 previous_uv, float3 current_position, out float2 dual_uv)
{
    dual_uv = previous_uv;
    uint width, height;
    restir_primary_hit_previous.GetDimensions(width, height);
    float2 size = float2(width, height);
    float2 uv_scale = get_render_uv_scale();
    if (!all(isfinite(previous_uv)) || any(previous_uv < 0.0f) || any(previous_uv >= uv_scale))
        return false;
    uint2 previous_pixel = uint2(previous_uv * size);
    float4 stored_hit = restir_primary_hit_previous[previous_pixel];
    if (!(stored_hit.w > 0.0f))
        return false;
    // Match the reference's isReprojectedToOccluder: measure both world-space
    // points from the current camera. The old primary position is independent
    // of the selected lighting path; every reuse pass re-stamps it at its pixel.
    Reservoir source = unpack_reservoir(tex_reservoir_prev0[previous_pixel], tex_reservoir_prev1[previous_pixel],
        tex_reservoir_prev2[previous_pixel], tex_reservoir_prev3[previous_pixel],
        tex_reservoir_prev4[previous_pixel], tex_reservoir_prev5[previous_pixel]);
    float current_distance = length(current_position - get_camera_position());
    float occluder_distance = length(source.sample.src_pos - get_camera_position());
    if (!isfinite(current_distance) || !isfinite(occluder_distance) ||
        current_distance - occluder_distance <= 0.1f * current_distance)
        return false;

    uint3 hit = asuint(stored_hit.xyz);
    GeometryInfo geometry = geometry_infos[hit.x];
    uint index_base = geometry.index_offset + hit.y * 3u;
    float3 bary = unpack_hit_barycentrics(hit.z);
    float3 p0 = geometry_vertices[geometry.vertex_offset + geometry_indices[index_base]].position;
    float3 p1 = geometry_vertices[geometry.vertex_offset + geometry_indices[index_base + 1u]].position;
    float3 p2 = geometry_vertices[geometry.vertex_offset + geometry_indices[index_base + 2u]].position;
    float3 local_position = p0 * bary.x + p1 * bary.y + p2 * bary.z;
    float3x3 transform = float3x3(geometry.object_to_world_0.xyz,
        geometry.object_to_world_1.xyz, geometry.object_to_world_2.xyz);
    float3 position = mul(local_position, transform) + float3(geometry.object_to_world_0.w,
        geometry.object_to_world_1.w, geometry.object_to_world_2.w);
    float4 clip = mul(float4(position, 1.0f), get_view_projection());
    if (!all(isfinite(clip)) || clip.w <= 0.0f)
        return false;
    float2 occluder_uv = (clip.xy / clip.w * float2(0.5f, -0.5f) + 0.5f) * uv_scale;
    float2 source_uv = restir_primary_uv((float2(previous_pixel) + 0.5f) / size);
    dual_uv = current_uv + source_uv - occluder_uv;
    return all(isfinite(dual_uv)) && all(dual_uv >= 0.0f) && all(dual_uv < uv_scale);
}

// validates temporal reprojection, delegated to the shared evaluate_disocclusion helper
bool is_temporal_sample_valid(
    float2 current_uv,
    float2 prev_uv,
    float3 current_normal,
    float2 screen_resolution,
    out float confidence)
{
    bool ok = evaluate_disocclusion(
        tex,
        tex_normal_previous,
        current_uv,
        prev_uv,
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

    // Use the same full-resolution texel centre as the primary position and
    // motion vector. The low-resolution cell centre can differ by half a pixel.
    float2 uv = restir_primary_uv((pixel + 0.5f) / resolution);

    float depth = tex_depth.SampleLevel(GET_SAMPLER(sampler_point_clamp), uv, 0).r;
    if (depth <= 0.0f)
        return;

    float3 pos_ws    = restir_primary_position(uv);
    float3 normal_ws = get_normal(uv);
    float3 geometric_normal_ws = get_geometric_normal(uv);
    float3 view_dir  = normalize(get_camera_position() - pos_ws);
    float4 material  = get_material(uv);
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
    bool   have_history = is_temporal_sample_valid(uv, prev_uv, normal_ws, buffer_frame.resolution_render, temporal_confidence);
    bool   dual_history = false;

    // Section 6.4: offer the dual-motion correspondence as a resampling technique.
    // The shift and spatial surface checks below still validate the reused path.
    float2 uv_scale     = get_render_uv_scale();
    bool prev_on_screen = all(prev_uv >= 0.0f) && all(prev_uv < uv_scale);
    if (!have_history)
    {
        float2 dual_uv;
        bool alternate_valid;
        if (prev_on_screen)
        {
            alternate_valid = find_dual_history_uv(uv, prev_uv, pos_ws, dual_uv);
        }
        else
        {
            // The reference's disocclusion-motion pass reverses motion when
            // ordinary reprojection is outside the image. Reverse physical
            // motion only: both lookups still target the same jittered history.
            dual_uv = reproject_to_previous_frame(uv, true);
            alternate_valid = all(isfinite(dual_uv)) && all(dual_uv >= 0.0f) && all(dual_uv < uv_scale);
        }
        if (alternate_valid)
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
        float2 prev_pixel_f      = history_uv * resolution;
        float2 resolution_active = floor(resolution * get_render_uv_scale());
        // Reservoir history uses a single integer load, not a bilinear footprint.
        // Every coordinate in [0, active_size) maps to a valid texel, including
        // the last row/column and the outer half of each edge texel.
        bool in_bounds = all(prev_pixel_f >= 0.0f) && all(prev_pixel_f < resolution_active);
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

            // Reprojection validated the full-resolution pixel, but the selected
            // reservoir was traced at a snapped guide centre. At reduced tracing
            // resolution that guide can lie across a silhouette in the same cell.
            // Validate its previous-frame geometry too, using motion's previous
            // depth so moving surfaces are compared in the same frame.
            if (usable && !dual_history)
            {
                float2 source_uv = restir_primary_uv((float2(prev_pixel) + 0.5f) / resolution);
                float source_depth = tex.SampleLevel(GET_SAMPLER(sampler_point_clamp), source_uv, 0).r;
                float3 source_normal = gbuffer_unpack_normal(tex_normal_previous[gbuffer_texel(source_uv)]);
                float expected_depth = abs(tex_velocity.SampleLevel(GET_SAMPLER(sampler_point_clamp), uv, 0).w);
                float motion_factor = saturate(length((uv - prev_uv) * buffer_frame.resolution_render) / 32.0f);
                float depth_delta = abs(linearize_depth(source_depth) - expected_depth) / max(expected_depth, 1e-3f);
                usable = source_depth > 0.0f &&
                    dot(normal_ws, source_normal) >= lerp(0.9f, 0.97f, motion_factor) &&
                    depth_delta <= lerp(0.12f, 0.04f, motion_factor);
            }

            // Section 6.4 reuses the dual coordinate as another path-sampling
            // technique, not as a claim that the two primary surfaces coincide.
            // Normal maps and object motion can change their shading planes.
            // The bidirectional shifts and visibility tests below validate the
            // paths; a same-plane/normal test would discard valid techniques.

            if (usable)
            {
                have_temporal = true;
                // use the stored source primary g-buffer, correct even for moving objects
                float3 src_primary_pos = temporal.sample.src_pos;
                float3 src_normal_ws   = temporal.sample.src_normal;
                float2 source_guide_uv = restir_primary_uv((float2(prev_pixel) + 0.5f) / resolution);
                float3 src_geometric_normal_ws = gbuffer_unpack_geometric_normal(tex_normal_previous[gbuffer_texel(source_guide_uv)]);
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
                    metallic,
                    src_view_dir,
                    geometric_normal_ws
                );

                if (temporal.W > 0.0f && shift_t_to_c.ok)
                {
                    bool visible = trace_shift_visibility(shift_t_to_c.sample, pos_ws, geometric_normal_ws);
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
                    src_metallic,
                    view_dir,
                    src_geometric_normal_ws,
                    true
                );
                if (current.W > 0.0f && shift_c_to_t.ok &&
                    trace_shift_visibility(shift_c_to_t.sample, src_primary_pos, src_geometric_normal_ws))
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
        float m_cap       = get_restir_m_cap_decorrelated(duplication);

        // a lighting change that kept the reservoirs, stale cached radiance halves every frame
        float history_m_cap = pass_float(pass_restir_temporal::history_m_cap);
        if (history_m_cap > 0.0f)
        {
            m_cap = min(m_cap, history_m_cap);
        }
        clamp_reservoir_M(temporal, m_cap);
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
    // Preserve the initial in-lobe distance; the RIS winner is not a reflection guide.
    float specular_hit_dist = restir_specular_output[pixel].a;
    float3 gi = float3(0, 0, 0);
    ShiftResult canonical = self_shift_evaluate(current.sample, pos_ws, normal_ws, view_dir, albedo, roughness, metallic);
    gi = m_cur * canonical.f_dst * max(current.W, 0.0f);
    float3 specular = gi * restir_primary_specular_fraction(current.sample, pos_ws, normal_ws,
        view_dir, albedo, roughness, metallic);

    if (have_temporal)
    {
        float3 contribution = m_temp * f_temp * max(temporal.W, 0.0f) * jacobian_temp;
        gi += contribution;
        specular += contribution * restir_primary_specular_fraction(temporal_shifted_sample, pos_ws, normal_ws,
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
    restir_specular_output[pixel] = float4(clamp(specular, 0.0f, gi), specular_hit_dist);

}
