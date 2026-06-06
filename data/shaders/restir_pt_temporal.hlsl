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

    float target_cur = target_pdf_self(current.sample, pos_ws, normal_ws, view_dir, albedo, roughness, metallic);

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
                // use the stored source primary g-buffer, correct even for moving objects
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
                        target_temp   = target_scalar(shift_t_to_c.f_dst);
                        jacobian_temp = shift_t_to_c.jacobian;

                        // backward shift, canonical sample evaluated at the temporal pixel, for pairwise mis
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
                            ? target_scalar(shift_c_to_t.f_dst)
                            : 0.0f;

                        have_temporal = (target_temp > 0.0f);
                    }
                }
            }
        }
    }

    // cap temporal M so lighting changes are tracked, M is the c_i for the balance heuristic
    if (have_temporal)
    {
        clamp_reservoir_M(temporal, get_restir_m_cap());
    }

    // defensive pairwise mis with the temporal stream as the single neighbor
    float weight_cur = 0.0f;
    float weight_tmp = 0.0f;

    if (have_temporal)
    {
        float pair_denom = max(current.M * target_cur_at_temp + temporal.M * target_temp, 1e-12f);
        float canon_share = (current.M * target_cur_at_temp) / pair_denom;
        float temp_share  = (temporal.M * target_temp)        / pair_denom;

        float m_cur  = 0.5f * (1.0f + canon_share);
        float m_temp = 0.5f * temp_share;

        // gris streaming weights, w = m_i * p_hat * jacobian * W, jacobian appears once here
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
            combined.sample     = temporal.sample;
            combined.target_pdf = target_temp;
        }
    }

    clamp_reservoir_M(combined, get_restir_m_cap());

    // lin 2022 6.4 sample validation, every n frames a subset of pixels re-traces rc visibility
    // and resets the reservoir if rc is no longer reachable, cost amortized to ~1/n pixels per frame
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

    // finalize, W = weight_sum / target, no /M since the m_i factors are already normalized
    float final_target = target_pdf_self(combined.sample, pos_ws, normal_ws, view_dir, albedo, roughness, metallic);
    combined.target_pdf = final_target;
    combined.W          = (final_target > 0.0f) ? (combined.weight_sum / final_target) : 0.0f;

    // soft saturator, see soft_clamp_w in restir_reservoir.hlsl
    float w_clamp = get_w_clamp_for_sample(combined.sample);
    combined.W    = soft_clamp_w(combined.W, w_clamp);

    combined.age        = have_temporal ? (temporal.age + 1.0f) : 0.0f;
    combined.confidence = saturate(max(current.confidence, have_temporal ? temporal.confidence * temporal_confidence : 0.0f));

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

    float3 gi = shade_reservoir_path(combined, pos_ws, normal_ws, view_dir, albedo, roughness, metallic);
    if (any(isnan(gi)) || any(isinf(gi)))
        gi = float3(0, 0, 0);

    tex_uav[pixel] = float4(gi, saturate(combined.confidence));
}
