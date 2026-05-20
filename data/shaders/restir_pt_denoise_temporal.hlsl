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
//==============================

// svgf style temporal accumulation (schied et al. 2017 spatiotemporal variance guided filter)
// outputs:
//   tex_uav  rgb = denoised color    a = per pixel luminance variance estimate
//   tex_uav2 rg  = (luma_M1, luma_M2) ema   b = effective sample count   a = unused
// the variance in alpha is the input that drives the spatial filter's luma_phi, replacing the
// previous hand tuned global scale, the moments are accumulated with an exponential moving
// average so disocclusions reset the variance estimate to the local 7x7 spatial sigma until
// enough frames have been accumulated for the temporal estimator to take over

static const float3 luminance_weights = float3(0.299f, 0.587f, 0.114f);

float2 reproject_to_previous_frame(float2 current_uv)
{
    float2 velocity_ndc = tex_velocity.SampleLevel(GET_SAMPLER(sampler_point_clamp), current_uv, 0).xy;
    float2 velocity_uv  = velocity_ndc * float2(0.5f, -0.5f);
    return current_uv - velocity_uv;
}

float3 sample_input(int2 pixel, uint2 resolution)
{
    uint2 clamped_pixel = (uint2)clamp(pixel, int2(0, 0), int2(resolution) - 1);
    return max(tex.Load(int3(clamped_pixel, 0)).rgb, 0.0f);
}

void compute_local_statistics(uint2 pixel, uint2 resolution, out float3 mean, out float3 sigma, out float3 minimum_value, out float3 maximum_value)
{
    float3 second_moment = 0.0f;
    mean                 = 0.0f;
    minimum_value        = float3(FLT_MAX_16U, FLT_MAX_16U, FLT_MAX_16U);
    maximum_value        = 0.0f;

    for (int y = -1; y <= 1; y++)
    {
        for (int x = -1; x <= 1; x++)
        {
            int2 sample_pixel = clamp(int2(pixel) + int2(x, y), int2(0, 0), int2(resolution) - 1);
            float3 sample_color = sample_input(sample_pixel, resolution);
            mean          += sample_color;
            second_moment += sample_color * sample_color;
            minimum_value  = min(minimum_value, sample_color);
            maximum_value  = max(maximum_value, sample_color);
        }
    }

    mean /= 9.0f;
    float3 variance = max(second_moment / 9.0f - mean * mean, 0.0f);
    sigma = sqrt(variance);
}

// 7x7 luma variance estimator used during the disocclusion bootstrap, schied 2017 §4.3
// when the temporal accumulator has too few samples (n_eff < 4) the variance estimate from the
// ema is unreliable, the spatial sigma over a wider neighborhood gives a more stable bootstrap
// the kernel is a rough box (we ignore the gaussian weights for the spatial sigma estimate)
void compute_spatial_luma_variance_7x7(uint2 pixel, uint2 resolution, out float spatial_var)
{
    float m1 = 0.0f;
    float m2 = 0.0f;
    float n  = 0.0f;
    [unroll]
    for (int y = -3; y <= 3; y++)
    {
        [unroll]
        for (int x = -3; x <= 3; x++)
        {
            int2 sp = clamp(int2(pixel) + int2(x, y), int2(0, 0), int2(resolution) - 1);
            float3 c = sample_input(sp, resolution);
            float  l = dot(c, luminance_weights);
            m1 += l;
            m2 += l * l;
            n  += 1.0f;
        }
    }
    m1 /= n;
    m2 /= n;
    spatial_var = max(m2 - m1 * m1, 0.0f);
}

float3 clamp_history_variance(float3 history, float3 current, float current_luma, float variance)
{
    // variance gated history clamp (schied 2017 §4.4) replaces the previous fixed sigma scaler
    // a high variance estimate widens the clamp window so noisy bright pixels are not bias
    // pinned to a tight neighborhood mean, low variance tightens the window so contact shadows
    // and small geometric features are preserved
    float sigma         = sqrt(max(variance, 1e-8f));
    float clamp_radius  = max(sigma * 4.0f, 0.05f);
    float current_low   = max(current_luma - clamp_radius, 0.0f);
    float current_high  = current_luma + clamp_radius;

    // soft remap based on history luma vs current luma window, color is rescaled toward
    // history but clamped to the variance gated band, keeps chroma stable while limiting luma
    float history_luma = dot(history, luminance_weights);
    float clamped_luma = clamp(history_luma, current_low, current_high);
    float scale        = (history_luma > 1e-4f) ? (clamped_luma / history_luma) : 1.0f;
    return history * scale;
}

float4 sample_history_edge_aware(float2 prev_uv, float3 current_normal, float current_depth, float2 history_resolution)
{
    float2 prev_pixel_f = prev_uv * history_resolution - 0.5f;
    float2 base_f       = floor(prev_pixel_f);
    float2 frac_f       = saturate(prev_pixel_f - base_f);

    float4 bilinear_w = float4(
        (1.0f - frac_f.x) * (1.0f - frac_f.y),
        frac_f.x         * (1.0f - frac_f.y),
        (1.0f - frac_f.x) * frac_f.y,
        frac_f.x         * frac_f.y
    );

    int2 offsets[4] =
    {
        int2(0, 0), int2(1, 0), int2(0, 1), int2(1, 1)
    };

    float4 accumulated = 0.0f;
    float  weight_sum  = 0.0f;
    float  depth_phi   = max(current_depth * 0.05f, 1e-3f);

    for (uint i = 0; i < 4; i++)
    {
        int2 tap_pixel = int2(base_f) + offsets[i];
        if (tap_pixel.x < 0 || tap_pixel.x >= (int)history_resolution.x ||
            tap_pixel.y < 0 || tap_pixel.y >= (int)history_resolution.y)
        {
            continue;
        }

        float2 tap_uv     = (tap_pixel + 0.5f) / history_resolution;
        float  tap_depth_raw = tex_depth.SampleLevel(GET_SAMPLER(sampler_point_clamp), tap_uv, 0).r;
        if (tap_depth_raw <= 0.0f)
        {
            continue;
        }

        float  tap_depth   = linearize_depth(tap_depth_raw);
        float  depth_delta = abs(tap_depth - current_depth) / max(current_depth, 1e-3f);
        if (depth_delta > 0.1f)
        {
            continue;
        }

        float3 tap_normal      = get_normal(tap_uv);
        float  normal_similarity = saturate(dot(tap_normal, current_normal));
        if (normal_similarity < 0.7f)
        {
            continue;
        }

        float depth_weight   = exp(-depth_delta / 0.05f);
        float normal_weight  = pow(normal_similarity, 8.0f);
        float w = bilinear_w[i] * depth_weight * normal_weight;
        if (w <= 0.0f)
        {
            continue;
        }

        accumulated += tex2.Load(int3(tap_pixel, 0)) * w;
        weight_sum  += w;
    }

    if (weight_sum > 0.0f)
    {
        return accumulated / weight_sum;
    }

    int2 fallback_pixel = clamp(int2(round(prev_pixel_f)), int2(0, 0), int2(history_resolution) - 1);
    return tex2.Load(int3(fallback_pixel, 0));
}

bool is_history_valid(float2 current_uv, float2 prev_uv, float3 current_position, float3 current_normal, float current_depth, float2 history_resolution, out float confidence)
{
    confidence = 0.0f;

    if (!is_valid_uv(prev_uv))
        return false;

    float4 prev_clip        = mul(float4(current_position, 1.0f), get_view_projection_previous());
    float3 prev_ndc         = prev_clip.xyz / max(prev_clip.w, FLT_MIN);
    float2 expected_prev_uv = prev_ndc.xy * float2(0.5f, -0.5f) + 0.5f;
    float2 reproj_diff      = abs(prev_uv - expected_prev_uv) * history_resolution;
    float reproj_dist       = length(reproj_diff);

    float2 motion        = (current_uv - prev_uv) * history_resolution;
    float motion_length  = length(motion);
    float motion_factor  = saturate(motion_length / 24.0f);
    float reproj_limit   = lerp(1.75f, 0.75f, motion_factor);
    if (reproj_dist > reproj_limit)
        return false;

    float3 prev_normal      = get_normal(prev_uv);
    float normal_similarity = dot(current_normal, prev_normal);
    float normal_limit      = lerp(0.88f, 0.96f, motion_factor);
    if (normal_similarity < normal_limit)
        return false;

    float prev_depth_raw = tex_depth.SampleLevel(GET_SAMPLER(sampler_point_clamp), prev_uv, 0).r;
    if (prev_depth_raw <= 0.0f)
        return false;

    float prev_depth        = linearize_depth(prev_depth_raw);
    float relative_depth_delta = abs(prev_depth - current_depth) / max(current_depth, 1e-3f);
    float depth_limit          = lerp(0.12f, 0.04f, motion_factor);
    if (relative_depth_delta > depth_limit)
        return false;

    float reproj_confidence = saturate(1.0f - reproj_dist / reproj_limit);
    float normal_confidence = saturate((normal_similarity - normal_limit) / (1.0f - normal_limit));
    float depth_confidence  = saturate(1.0f - relative_depth_delta / depth_limit);
    float motion_confidence = saturate(1.0f - motion_length / 24.0f);
    confidence = reproj_confidence * normal_confidence * depth_confidence * motion_confidence;

    return confidence > 0.012f;
}

[numthreads(THREAD_GROUP_COUNT_X, THREAD_GROUP_COUNT_Y, 1)]
void main_cs(uint3 dispatch_id : SV_DispatchThreadID)
{
    uint2 pixel = dispatch_id.xy;
    uint resolution_x, resolution_y;
    tex_uav.GetDimensions(resolution_x, resolution_y);
    uint2 resolution = uint2(resolution_x, resolution_y);

    if (pixel.x >= resolution.x || pixel.y >= resolution.y)
        return;

    float2 uv = (pixel + 0.5f) / resolution;
    float depth = tex_depth.SampleLevel(GET_SAMPLER(sampler_point_clamp), uv, 0).r;
    if (depth <= 0.0f)
    {
        tex_uav[pixel]  = float4(0.0f, 0.0f, 0.0f, 0.0f);
        tex_uav2[pixel] = float4(0.0f, 0.0f, 0.0f, 0.0f);
        return;
    }

    float3 current_color    = sample_input(int2(pixel), resolution);
    float3 current_position = get_position(uv);
    float3 current_normal   = get_normal(uv);
    float  current_linear_z = linearize_depth(depth);
    float  current_luma     = dot(current_color, luminance_weights);

    float2 prev_uv = reproject_to_previous_frame(uv);
    float  temporal_confidence = 0.0f;
    float  history_weight      = 0.0f;
    float3 history_color       = current_color;
    float3 history_moments     = float3(current_luma, current_luma * current_luma, 0.0f);

    bool history_ok = is_history_valid(uv, prev_uv, current_position, current_normal, current_linear_z, resolution, temporal_confidence);

    if (history_ok)
    {
        float4 history_sample = sample_history_edge_aware(prev_uv, current_normal, current_linear_z, float2(resolution));
        history_color         = max(history_sample.rgb, 0.0f);

        // history moments come from the moments_history texture (tex3 srv binding), keep the
        // raw fp16 value so noise statistics propagate exactly across frames, the bilinear tap
        // around prev_uv would average against neighbors but the existing edge aware history
        // sampler already validates the neighborhood so we re-use the same nearest tap here
        int2 prev_pixel_int = clamp(int2(prev_uv * resolution), int2(0, 0), int2(resolution) - 1);
        float4 prev_moments = tex3.Load(int3(prev_pixel_int, 0));
        history_moments     = prev_moments.xyz;

        // the schied 2017 alpha equivalent, history weight is built from validity factors and
        // dynamically adjusted by sample count so the first ~4 frames after disocclusion blend
        // weakly toward the new measurement, beyond that we converge to a paper-typical alpha
        // = 0.05 (history weight 0.95) for stationary frames
        float n_eff      = history_moments.z;
        float min_alpha  = saturate(1.0f / max(n_eff + 1.0f, 1.0f));
        float ema_alpha  = max(min_alpha, 0.05f);
        history_weight   = (1.0f - ema_alpha) * temporal_confidence;
        history_weight   = saturate(min(history_weight, 0.992f));
    }

    // small 3x3 mean+variance pre blur (schied 2017 §4.2) so isolated bright fireflies do not
    // pin the variance estimate to a single pixel, this stabilizes the spatial filter weights
    // and gives the firefly clamp below a reliable spatial sigma even when history is young
    float3 mean_color, sigma_color, min_color, max_color;
    compute_local_statistics(pixel, resolution, mean_color, sigma_color, min_color, max_color);
    float spatial_sigma_luma = dot(sigma_color, luminance_weights);
    float spatial_var_3x3    = spatial_sigma_luma * spatial_sigma_luma;

    // pre-ema firefly soft clamp, lin 2022 §6.4 / production svgf trick
    // if the current sample's luma is way above the history band, soft-rescale it down before
    // it poisons the moment update, otherwise a single hot pixel can pin the variance estimate
    // for many frames and force the spatial filter to over-blur the neighborhood, the gate
    // fires as soon as we have a temporal partner so disocclusion fireflies do not boil for
    // 4 frames before the clamp kicks in, the band widens via spatial sigma when history is
    // young so the clamp does not over-bias on freshly accumulating pixels
    if (history_ok)
    {
        float history_sigma = sqrt(max(history_moments.y - history_moments.x * history_moments.x, 0.0f));
        float band_sigma    = max(history_sigma, spatial_sigma_luma);
        float band_widen    = lerp(16.0f, 6.0f, saturate(history_moments.z / 4.0f));
        float clamp_high    = history_moments.x + band_widen * max(band_sigma, 0.05f);
        if (current_luma > clamp_high && current_luma > 1e-3f)
        {
            float scale    = clamp_high / current_luma;
            current_color *= scale;
            current_luma   = clamp_high;
        }
    }

    // moment ema, in the schied 2017 formulation alpha is the new sample weight so the moment
    // update is m_new = (1 - alpha) * m_prev + alpha * sample, ema_alpha = 1 - history_weight
    float ema_alpha = 1.0f - history_weight;
    float new_M1    = lerp(history_moments.x, current_luma, ema_alpha);
    float new_M2    = lerp(history_moments.y, current_luma * current_luma, ema_alpha);
    float new_n_eff = min(history_moments.z + 1.0f, 256.0f);

    float temporal_var = max(new_M2 - new_M1 * new_M1, 0.0f);

    // bootstrap fallback when the temporal estimator does not have enough samples yet, schied
    // 2017 §4.3 uses a 7x7 spatial luma variance for the first 4 frames after disocclusion
    float variance_estimate = temporal_var;
    if (new_n_eff < 4.0f)
    {
        float spatial_var;
        compute_spatial_luma_variance_7x7(pixel, resolution, spatial_var);
        // boost spatial variance by 1/sqrt(N) to compensate for the low sample count, the SVGF
        // paper applies a similar boost so disocclusion regions get filtered more aggressively
        float boost = max(4.0f - new_n_eff, 1.0f);
        variance_estimate = max(temporal_var, spatial_var * boost);
    }

    variance_estimate = max(variance_estimate, spatial_var_3x3 * 0.25f);

    // variance gated history clamp keeps chromatic stability even when the temporal accumulator
    // has converged, the previous neighborhood sigma scaler was hand tuned, this ties it to the
    // actual per pixel variance estimate
    if (history_ok)
    {
        history_color = clamp_history_variance(history_color, current_color, current_luma, variance_estimate);
    }

    float3 output_color = lerp(current_color, history_color, history_weight);

    tex_uav[pixel]  = validate_output(float4(output_color, max(variance_estimate, 0.0f)));
    tex_uav2[pixel] = float4(new_M1, new_M2, new_n_eff, 0.0f);
}
