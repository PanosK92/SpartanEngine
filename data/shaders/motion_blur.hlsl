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

static const int   SAMPLE_COUNT             = 15;    // samples per direction (total = 2 * count + 1)
static const float MAX_BLUR_RADIUS_PIXELS   = 40.0f; // maximum blur extent in pixels
static const float DEPTH_SCALE              = 100.0f; // depth comparison sensitivity
static const float CENTER_WEIGHT            = 1.0f;  // weight for center sample
static const int   COHERENCE_SAMPLES        = 4;     // samples for velocity coherence check
static const float COHERENCE_RADIUS         = 8.0f;  // radius in pixels for coherence sampling
static const float TARGET_FRAME_TIME        = 1.0f / 60.0f; // fixed reference frame time for stable blur (60fps baseline)

// helper: soft depth comparison (guerrilla games / killzone approach)
float soft_depth_compare(float depth_a, float depth_b)
{
    // returns 1 when depth_a is behind or equal to depth_b, with soft falloff
    float diff = (depth_a - depth_b) * DEPTH_SCALE;
    return saturate(1.0f - diff);
}

// helper: cone weight - samples closer to center contribute more (mcguire approach)
float cone_weight(float distance_from_center, float blur_length)
{
    return saturate(1.0f - distance_from_center / (blur_length + FLT_MIN));
}

// helper: cylinder weight - sample contributes based on its own blur coverage
float cylinder_weight(float sample_blur_length, float distance_from_center)
{
    return saturate(1.0f - abs(distance_from_center - sample_blur_length) / (sample_blur_length + FLT_MIN));
}

// helper: velocity-to-pixel conversion accounting for resolution differences
float2 velocity_to_pixels(float2 velocity_uv, float2 resolution_color)
{
    // velocity is in uv space, convert to pixels
    return velocity_uv * resolution_color;
}

// helper: compute velocity coherence - measures how consistent velocity is in a neighborhood
// returns 1.0 for perfectly coherent motion, lower for erratic/jerky motion
float compute_velocity_coherence(float2 uv, float2 center_velocity, float2 texel_size)
{
    float center_length = length(center_velocity);
    if (center_length < FLT_MIN)
        return 1.0f;
    
    float2 center_dir = center_velocity / center_length;
    float coherence = 0.0f;
    
    // sample velocity at nearby pixels and check direction consistency
    // use a small cross pattern for efficiency
    static const float2 offsets[4] = 
    {
        float2(-1.0f,  0.0f),
        float2( 1.0f,  0.0f),
        float2( 0.0f, -1.0f),
        float2( 0.0f,  1.0f)
    };
    
    [unroll]
    for (int i = 0; i < COHERENCE_SAMPLES; ++i)
    {
        float2 sample_uv       = uv + offsets[i] * texel_size * COHERENCE_RADIUS;
        float2 sample_velocity = tex_velocity.SampleLevel(samplers[sampler_bilinear_clamp], sample_uv, 0).xy;
        float  sample_length   = length(sample_velocity);
        
        if (sample_length > FLT_MIN)
        {
            float2 sample_dir = sample_velocity / sample_length;
            
            // direction similarity (1 = same direction, 0 = perpendicular, -1 = opposite)
            float dir_similarity = dot(center_dir, sample_dir);
            
            // magnitude similarity (penalize very different speeds)
            float mag_ratio      = min(center_length, sample_length) / (max(center_length, sample_length) + FLT_MIN);
            
            // combine: require both similar direction AND similar magnitude
            coherence += saturate(dir_similarity) * mag_ratio;
        }
        else
        {
            // static neighbor next to moving pixel - partial coherence
            coherence += 0.5f;
        }
    }
    
    coherence /= (float)COHERENCE_SAMPLES;
    
    // apply a curve to be more forgiving of slight variations but harsh on erratic motion
    // smoothstep makes the transition gradual
    return smoothstep(0.0f, 0.7f, coherence);
}

// reconstruction filter for plausible motion blur (based on mcguire 2012)
float4 motion_blur_reconstruction(
    float2 uv, 
    float2 pixel_coord,
    float2 resolution_color, 
    float2 resolution_velocity,
    float  shutter_ratio,
    float  noise
)
{
    // sample center pixel
    float4 center_color = tex.SampleLevel(samplers[sampler_bilinear_clamp], uv, 0);
    float  center_depth = get_linear_depth(uv);
    
    // sample velocity at color resolution uv (handles resolution mismatch)
    float2 center_velocity_uv     = tex_velocity.SampleLevel(samplers[sampler_bilinear_clamp], uv, 0).xy;
    float2 center_velocity_pixels = velocity_to_pixels(center_velocity_uv, resolution_color);
    float  center_blur_length_raw = length(center_velocity_pixels);
    
    // early exit for static or nearly static pixels (check raw velocity before shutter scaling)
    if (center_blur_length_raw < 0.5f)
    {
        return center_color;
    }
    
    // compute velocity coherence - reduces blur for erratic/jerky motion
    // this prevents ugly smearing when mouse movement is shaky
    float2 velocity_texel_size = 1.0f / resolution_velocity;
    float  coherence           = compute_velocity_coherence(uv, center_velocity_uv, velocity_texel_size);
    
    // apply shutter ratio and coherence after early exit check
    // erratic motion (low coherence) produces less blur
    float center_blur_length = center_blur_length_raw * shutter_ratio * coherence;
    
    // if coherence killed the blur, early exit
    if (center_blur_length < 0.5f)
    {
        return center_color;
    }
    
    // clamp blur length for performance
    float clamped_blur_length = min(center_blur_length, MAX_BLUR_RADIUS_PIXELS);
    
    // normalized velocity direction (use raw velocity for direction, independent of shutter)
    float2 velocity_dir = center_velocity_pixels / (center_blur_length_raw + FLT_MIN);
    
    // accumulation
    float4 color_accum   = center_color * CENTER_WEIGHT;
    float  weight_accum  = CENTER_WEIGHT;
    
    // jittered sample offset for temporal stability (integrates with taa)
    float jitter = (noise - 0.5f) * 2.0f;
    
    // sample along velocity direction (both forward and backward)
    [unroll]
    for (int i = 1; i <= SAMPLE_COUNT; ++i)
    {
        // non-linear sample distribution - more samples near center for smooth gradients
        float t = (float)i / (float)SAMPLE_COUNT;
        t = t * t; // quadratic distribution concentrates samples near center
        
        // add temporal jitter for smooth accumulation across frames
        float sample_distance = t * clamped_blur_length + jitter * (clamped_blur_length / (float)SAMPLE_COUNT);
        sample_distance = max(sample_distance, 0.0f);
        
        // sample positions in both directions
        float2 offset = velocity_dir * sample_distance / resolution_color;
        float2 uv_forward  = uv + offset;
        float2 uv_backward = uv - offset;
        
        // forward sample
        if (is_valid_uv(uv_forward))
        {
            float4 sample_color         = tex.SampleLevel(samplers[sampler_bilinear_clamp], uv_forward, 0);
            float  sample_depth         = get_linear_depth(uv_forward);
            float2 sample_velocity_uv   = tex_velocity.SampleLevel(samplers[sampler_bilinear_clamp], uv_forward, 0).xy;
            float2 sample_velocity_px   = velocity_to_pixels(sample_velocity_uv, resolution_color);
            float  sample_blur_length   = min(length(sample_velocity_px) * shutter_ratio, MAX_BLUR_RADIUS_PIXELS);
            
            // bilateral weights based on depth relationship (guerrilla games approach)
            // foreground samples always contribute, background samples are occluded
            float depth_weight_forward  = soft_depth_compare(center_depth, sample_depth); // sample in front
            float depth_weight_backward = soft_depth_compare(sample_depth, center_depth); // center in front
            
            // velocity-based weights (mcguire reconstruction filter)
            float cone     = cone_weight(sample_distance, sample_blur_length);
            float cylinder = cylinder_weight(sample_blur_length, sample_distance);
            
            // combine weights - sample contributes if it could affect this pixel
            float weight = (depth_weight_forward * cone + depth_weight_backward * cylinder) * 
                           saturate(sample_blur_length / (clamped_blur_length + FLT_MIN));
            
            // soft falloff at the edges for smoother appearance
            weight *= smoothstep(0.0f, 0.1f, 1.0f - t);
            
            color_accum  += sample_color * weight;
            weight_accum += weight;
        }
        
        // backward sample
        if (is_valid_uv(uv_backward))
        {
            float4 sample_color         = tex.SampleLevel(samplers[sampler_bilinear_clamp], uv_backward, 0);
            float  sample_depth         = get_linear_depth(uv_backward);
            float2 sample_velocity_uv   = tex_velocity.SampleLevel(samplers[sampler_bilinear_clamp], uv_backward, 0).xy;
            float2 sample_velocity_px   = velocity_to_pixels(sample_velocity_uv, resolution_color);
            float  sample_blur_length   = min(length(sample_velocity_px) * shutter_ratio, MAX_BLUR_RADIUS_PIXELS);
            
            // bilateral weights
            float depth_weight_forward  = soft_depth_compare(center_depth, sample_depth);
            float depth_weight_backward = soft_depth_compare(sample_depth, center_depth);
            
            // velocity weights
            float cone     = cone_weight(sample_distance, sample_blur_length);
            float cylinder = cylinder_weight(sample_blur_length, sample_distance);
            
            float weight = (depth_weight_forward * cone + depth_weight_backward * cylinder) *
                           saturate(sample_blur_length / (clamped_blur_length + FLT_MIN));
            
            weight *= smoothstep(0.0f, 0.1f, 1.0f - t);
            
            color_accum  += sample_color * weight;
            weight_accum += weight;
        }
    }
    
    // normalize
    float4 result = color_accum / (weight_accum + FLT_MIN);
    
    // preserve alpha channel
    result.a = center_color.a;
    
    return result;
}

[numthreads(THREAD_GROUP_COUNT_X, THREAD_GROUP_COUNT_Y, 1)]
void main_cs(uint3 thread_id : SV_DispatchThreadID, uint3 group_thread_id : SV_GroupThreadID, uint group_index : SV_GroupIndex)
{
    // get buffer dimensions (color/output can be different from velocity)
    float2 resolution_color;
    tex.GetDimensions(resolution_color.x, resolution_color.y);
    
    float2 resolution_output;
    tex_uav.GetDimensions(resolution_output.x, resolution_output.y);
    
    float2 resolution_velocity;
    tex_velocity.GetDimensions(resolution_velocity.x, resolution_velocity.y);
    
    // compute pixel coordinate and uv
    uint2  pixel_coord = thread_id.xy;
    float2 uv          = (pixel_coord + 0.5f) / resolution_output;
    
    // early exit for out of bounds
    if (any(pixel_coord >= uint2(resolution_output)))
    {
        return;
    }
    
    // get shutter parameters for physically-based blur
    // shutter_ratio represents how much of the frame's motion is captured
    // - ratio < 1: fast shutter, freezes motion (e.g. 1/125s at 60fps = 0.5)
    // - ratio = 1: shutter matches frame time, standard blur
    // - ratio > 1: slow shutter, motion spans multiple frames (e.g. 1/30s at 60fps = 2.0)
    // 
    // important: use fixed target frame time instead of actual delta_time for stability
    // velocity buffer already contains per-frame motion, so using variable delta_time
    // causes jittery/unstable blur when frame rate fluctuates
    float shutter_speed = pass_get_f3_value().x;
    float shutter_ratio = clamp(shutter_speed / TARGET_FRAME_TIME, 0.0f, 3.0f);
    
    // generate per-pixel temporal noise for smooth sample distribution across frames
    // this integrates with taa for artifact-free motion blur
    float noise = noise_interleaved_gradient(float2(pixel_coord), true);
    
    // perform motion blur reconstruction
    float4 result = motion_blur_reconstruction(
        uv,
        float2(pixel_coord),
        resolution_color,
        resolution_velocity,
        shutter_ratio,
        noise
    );
    
    tex_uav[pixel_coord] = result;
}
