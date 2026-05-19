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

float3 clamp_history(float3 history, float3 mean, float3 sigma, float3 minimum_value, float3 maximum_value, float current_confidence, float history_confidence)
{
    float mean_luma   = dot(mean, luminance_weights);
    float low_light_factor = saturate(1.0f - mean_luma / 0.2f);
    float sigma_scale = lerp(0.9f, 2.0f, saturate(min(current_confidence, history_confidence)));
    // tighten further in dark regions to preserve shadow-edge detail
    sigma_scale      *= lerp(0.9f, 1.0f, 1.0f - low_light_factor);
    float3 lower      = max(minimum_value, mean - sigma * sigma_scale);
    float3 upper      = min(maximum_value, mean + sigma * sigma_scale);
    return clamp(history, lower, upper);
}

// edge-aware 4-tap reprojection, fetches the 4 nearest history samples around prev_uv and
// reweights them by depth + normal compatibility before bilinear blending; this stops history
// leaking across surface edges when motion lands prev_uv between two surfaces of different depth
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

    // all taps rejected, fall back to point sample at the rounded center
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
        tex_uav[pixel] = float4(0.0f, 0.0f, 0.0f, 0.0f);
        return;
    }

    float3 current_color      = sample_input(int2(pixel), resolution);
    float3 current_position   = get_position(uv);
    float3 current_normal     = get_normal(uv);
    float  current_linear_z   = linearize_depth(depth);
    // confidence is the high f16 of tex4.w, see reservoir packing layout
    uint   age_conf           = asuint(tex_reservoir_prev4[pixel].w);
    float  current_confidence = saturate(f16tof32(age_conf >> 16u));

    float3 mean_color;
    float3 sigma_color;
    float3 minimum_color;
    float3 maximum_color;
    compute_local_statistics(pixel, resolution, mean_color, sigma_color, minimum_color, maximum_color);
    float current_luma = dot(current_color, luminance_weights);
    float mean_luma    = dot(mean_color, luminance_weights);
    float sigma_luma   = dot(sigma_color, luminance_weights);
    float low_light_factor = saturate(1.0f - current_luma / 0.2f);
    float relative_variance = sigma_luma / max(mean_luma, 0.01f);

    float2 prev_uv = reproject_to_previous_frame(uv);
    float  temporal_confidence = 0.0f;
    float  history_weight      = 0.0f;
    float3 history_color       = current_color;
    float  history_confidence  = 0.0f;

    if (is_history_valid(uv, prev_uv, current_position, current_normal, current_linear_z, resolution, temporal_confidence))
    {
        float4 history_sample = sample_history_edge_aware(prev_uv, current_normal, current_linear_z, float2(resolution));
        history_confidence    = saturate(history_sample.a);
        history_color         = clamp_history(max(history_sample.rgb, 0.0f), mean_color, sigma_color, minimum_color, maximum_color, current_confidence, history_confidence);

        history_weight = temporal_confidence;
        history_weight *= lerp(0.55f, 1.0f, history_confidence);
        history_weight *= lerp(0.65f, 0.95f, current_confidence);
        history_weight *= lerp(1.0f, 1.35f, saturate(relative_variance * 0.5f));
        history_weight *= lerp(1.0f, 1.45f, low_light_factor);
        history_weight  = saturate(min(history_weight, 0.992f));
    }

    float3 output_color = lerp(current_color, history_color, history_weight);
    float output_confidence = saturate(max(current_confidence, history_confidence * history_weight));
    output_confidence = max(output_confidence, current_confidence * (1.0f - history_weight));

    tex_uav[pixel] = validate_output(float4(output_color, output_confidence));
}
