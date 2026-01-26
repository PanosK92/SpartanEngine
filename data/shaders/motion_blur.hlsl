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

//= includes =========
#include "common.hlsl"
//====================

// tuning parameters
static const int   SAMPLE_COUNT           = 11;    // samples per direction (total = 2 * count + 1 = 23)
static const float MAX_BLUR_RADIUS_PIXELS = 48.0f; // maximum blur extent in pixels
static const float MIN_BLUR_THRESHOLD     = 0.75f; // minimum velocity in pixels to trigger blur
static const float DEPTH_SCALE            = 50.0f; // depth comparison sensitivity
static const float CENTER_WEIGHT          = 1.5f;  // weight for center sample (higher = sharper center)

// reference frame time - motion blur is normalized to 60fps baseline
// this ensures consistent blur regardless of actual framerate
static const float TARGET_FRAME_TIME = 1.0f / 60.0f;

// artistic control - prevents excessive blur from slow shutter speeds
// without this, dark scenes with 1/30s shutter would have 2x blur which looks bad
static const float MAX_SHUTTER_RATIO = 1.5f;

// velocity filtering parameters
static const int   VELOCITY_FILTER_SAMPLES = 5;   // samples for velocity filtering
static const float VELOCITY_FILTER_RADIUS  = 3.0f; // radius in pixels

// soft depth comparison (guerrilla games / killzone approach)
// returns 1 when depth_a is behind or equal to depth_b
float soft_depth_compare(float depth_a, float depth_b)
{
    return saturate(1.0f - (depth_a - depth_b) * DEPTH_SCALE);
}

// sample weight falloff - samples further from center contribute less
// uses a smooth gaussian-like falloff for natural blur edges
float sample_falloff(float t)
{
    // hermite smoothstep gives nice soft edges
    float inv_t = 1.0f - t;
    return inv_t * inv_t * (3.0f - 2.0f * inv_t);
}

// velocity magnitude weight - determines how much a sample should contribute
// based on its own velocity vs the center velocity
float velocity_weight(float sample_velocity_length, float center_velocity_length, float sample_distance)
{
    // sample contributes if its blur would reach the center pixel
    float coverage = sample_velocity_length - sample_distance;
    return saturate(coverage / (center_velocity_length + FLT_MIN));
}

// filter velocity to reduce noise from jittery mouse/input
// uses a small neighborhood to find the dominant velocity direction
float2 filter_velocity(float2 uv, float2 texel_size, float2 center_velocity)
{
    float center_length = length(center_velocity);

    // for very low velocities, don't bother filtering
    if (center_length < 0.001f)
        return center_velocity;

    float2 velocity_sum = center_velocity;
    float weight_sum = 1.0f;

    // sample pattern: cross + diagonals for good coverage
    static const float2 offsets[8] =
    {
        float2(-1.0f,  0.0f),
        float2( 1.0f,  0.0f),
        float2( 0.0f, -1.0f),
        float2( 0.0f,  1.0f),
        float2(-0.7f, -0.7f),
        float2( 0.7f, -0.7f),
        float2(-0.7f,  0.7f),
        float2( 0.7f,  0.7f)
    };

    float2 center_dir = center_velocity / (center_length + FLT_MIN);

    [unroll]
    for (int i = 0; i < 8; ++i)
    {
        float2 sample_uv = uv + offsets[i] * texel_size * VELOCITY_FILTER_RADIUS;
        float2 sample_vel = tex_velocity.SampleLevel(samplers[sampler_bilinear_clamp], sample_uv, 0).xy;
        float sample_len = length(sample_vel);

        if (sample_len > 0.001f)
        {
            float2 sample_dir = sample_vel / sample_len;

            // weight by direction similarity - prefer velocities pointing same way
            float dir_weight = max(0.0f, dot(center_dir, sample_dir));

            // weight by magnitude similarity - prefer similar speeds
            float mag_ratio = min(center_length, sample_len) / (max(center_length, sample_len) + FLT_MIN);

            // spatial weight - closer samples matter more
            float spatial_weight = 1.0f / (1.0f + length(offsets[i]));

            float weight = dir_weight * mag_ratio * spatial_weight;

            velocity_sum += sample_vel * weight;
            weight_sum += weight;
        }
    }

    return velocity_sum / weight_sum;
}

// compute velocity confidence - how consistent is motion in the neighborhood
// returns 0-1 where 1 = very consistent, 0 = chaotic/jittery
float compute_velocity_confidence(float2 uv, float2 velocity, float2 texel_size)
{
    float vel_length = length(velocity);
    if (vel_length < 0.001f)
        return 1.0f;

    float2 vel_dir = velocity / vel_length;
    float confidence = 0.0f;

    // check velocity consistency at a few points along the motion direction
    [unroll]
    for (int i = 0; i < VELOCITY_FILTER_SAMPLES; ++i)
    {
        float t = (float)(i + 1) / (float)(VELOCITY_FILTER_SAMPLES + 1);
        float2 offset = vel_dir * t * VELOCITY_FILTER_RADIUS * 2.0f;
        float2 sample_uv = uv + offset * texel_size;

        float2 sample_vel = tex_velocity.SampleLevel(samplers[sampler_bilinear_clamp], sample_uv, 0).xy;
        float sample_len = length(sample_vel);

        if (sample_len > 0.001f)
        {
            float2 sample_dir = sample_vel / sample_len;

            // direction consistency
            float dir_match = dot(vel_dir, sample_dir);

            // magnitude consistency (don't penalize too harshly)
            float mag_ratio = min(vel_length, sample_len) / (max(vel_length, sample_len) + FLT_MIN);
            mag_ratio = lerp(mag_ratio, 1.0f, 0.5f); // soften magnitude requirement

            confidence += max(0.0f, dir_match) * mag_ratio;
        }
        else
        {
            // static neighbor - partial confidence
            confidence += 0.3f;
        }
    }

    confidence /= (float)VELOCITY_FILTER_SAMPLES;

    // apply curve - be forgiving of slight variations
    return smoothstep(0.2f, 0.8f, confidence);
}

// main reconstruction filter
float4 motion_blur_reconstruction(
    float2 uv,
    float2 pixel_coord,
    float2 resolution,
    float  shutter_ratio,
    float  noise
)
{
    float2 texel_size = 1.0f / resolution;

    // sample center
    float4 center_color = tex.SampleLevel(samplers[sampler_bilinear_clamp], uv, 0);
    float  center_depth = get_linear_depth(uv);

    // get and filter velocity for smoother results
    float2 raw_velocity = tex_velocity.SampleLevel(samplers[sampler_bilinear_clamp], uv, 0).xy;
    float2 velocity = filter_velocity(uv, texel_size, raw_velocity);

    // convert to pixels
    float2 velocity_pixels = velocity * resolution;
    float  blur_length_raw = length(velocity_pixels);

    // early exit for static pixels
    if (blur_length_raw < MIN_BLUR_THRESHOLD)
        return center_color;

    // compute velocity confidence to reduce blur for jittery motion
    float confidence = compute_velocity_confidence(uv, velocity, texel_size);

    // apply shutter ratio and confidence
    // confidence scales the blur - jittery motion = less blur
    float blur_length = blur_length_raw * shutter_ratio * lerp(0.3f, 1.0f, confidence);

    // exit if blur became too small after adjustments
    if (blur_length < MIN_BLUR_THRESHOLD)
        return center_color;

    // clamp to max radius
    float clamped_blur = min(blur_length, MAX_BLUR_RADIUS_PIXELS);

    // normalized direction
    float2 blur_dir = velocity_pixels / (blur_length_raw + FLT_MIN);

    // accumulation with center sample
    float4 color_sum = center_color * CENTER_WEIGHT;
    float  weight_sum = CENTER_WEIGHT;

    // per-pixel noise for temporal stability (integrates with TAA)
    float jitter = (noise - 0.5f) * 0.5f;

    // sample in both directions along velocity
    [unroll]
    for (int i = 1; i <= SAMPLE_COUNT; ++i)
    {
        // normalized sample position [0, 1]
        float t = (float)i / (float)SAMPLE_COUNT;

        // apply jitter for temporal smoothing
        float t_jittered = saturate(t + jitter / (float)SAMPLE_COUNT);

        // sample distance in pixels
        float sample_dist = t_jittered * clamped_blur;

        // uv offset
        float2 offset = blur_dir * sample_dist * texel_size;

        // forward and backward sample positions
        float2 uv_fwd = uv + offset;
        float2 uv_bwd = uv - offset;

        // base falloff weight
        float falloff = sample_falloff(t);

        // forward sample
        if (is_valid_uv(uv_fwd))
        {
            float4 sample_color = tex.SampleLevel(samplers[sampler_bilinear_clamp], uv_fwd, 0);
            float  sample_depth = get_linear_depth(uv_fwd);
            float2 sample_vel   = tex_velocity.SampleLevel(samplers[sampler_bilinear_clamp], uv_fwd, 0).xy;
            float  sample_blur  = length(sample_vel * resolution) * shutter_ratio;

            // depth-aware weighting
            float depth_weight_fg = soft_depth_compare(center_depth, sample_depth); // sample in front
            float depth_weight_bg = soft_depth_compare(sample_depth, center_depth); // center in front

            // velocity-based weight
            float vel_weight = velocity_weight(min(sample_blur, MAX_BLUR_RADIUS_PIXELS), clamped_blur, sample_dist);

            // combine weights
            float weight = falloff * (depth_weight_fg + depth_weight_bg * vel_weight);
            weight = max(weight, 0.01f); // minimum weight to prevent harsh cutoffs

            color_sum += sample_color * weight;
            weight_sum += weight;
        }

        // backward sample
        if (is_valid_uv(uv_bwd))
        {
            float4 sample_color = tex.SampleLevel(samplers[sampler_bilinear_clamp], uv_bwd, 0);
            float  sample_depth = get_linear_depth(uv_bwd);
            float2 sample_vel   = tex_velocity.SampleLevel(samplers[sampler_bilinear_clamp], uv_bwd, 0).xy;
            float  sample_blur  = length(sample_vel * resolution) * shutter_ratio;

            // depth-aware weighting
            float depth_weight_fg = soft_depth_compare(center_depth, sample_depth);
            float depth_weight_bg = soft_depth_compare(sample_depth, center_depth);

            // velocity-based weight
            float vel_weight = velocity_weight(min(sample_blur, MAX_BLUR_RADIUS_PIXELS), clamped_blur, sample_dist);

            // combine weights
            float weight = falloff * (depth_weight_fg + depth_weight_bg * vel_weight);
            weight = max(weight, 0.01f);

            color_sum += sample_color * weight;
            weight_sum += weight;
        }
    }

    // final result
    float4 result = color_sum / weight_sum;
    result.a = center_color.a;

    return result;
}

[numthreads(THREAD_GROUP_COUNT_X, THREAD_GROUP_COUNT_Y, 1)]
void main_cs(uint3 thread_id : SV_DispatchThreadID)
{
    // get dimensions
    float2 resolution_color;
    tex.GetDimensions(resolution_color.x, resolution_color.y);

    float2 resolution_output;
    tex_uav.GetDimensions(resolution_output.x, resolution_output.y);

    // compute uv
    uint2  pixel_coord = thread_id.xy;
    float2 uv = (pixel_coord + 0.5f) / resolution_output;

    // bounds check
    if (any(pixel_coord >= uint2(resolution_output)))
        return;

    // get shutter speed from camera
    // shutter_ratio = shutter_speed / frame_time
    // - ratio < 1: fast shutter, freezes motion (less blur)
    // - ratio = 1: shutter matches frame time, standard blur
    // - ratio > 1: slow shutter, motion trails (more blur)
    float shutter_speed = pass_get_f3_value().x;
    float shutter_ratio = shutter_speed / TARGET_FRAME_TIME;

    // clamp shutter ratio to prevent excessive blur in dark scenes
    // physically, dark scenes need slower shutter for exposure, but that
    // results in unreasonably long motion trails that look bad
    shutter_ratio = clamp(shutter_ratio, 0.0f, MAX_SHUTTER_RATIO);

    // per-pixel temporal noise for TAA integration
    float noise = noise_interleaved_gradient(float2(pixel_coord), true);

    // perform blur
    float4 result = motion_blur_reconstruction(
        uv,
        float2(pixel_coord),
        resolution_color,
        shutter_ratio,
        noise
    );

    tex_uav[pixel_coord] = result;
}
