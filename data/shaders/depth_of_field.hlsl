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


/*------------------------------------------------------------------------------
    constants
------------------------------------------------------------------------------*/
// camera/lens parameters
static const float SENSOR_HEIGHT_MM = 24.0f;  // full-frame 35mm sensor
static const float FOCAL_LENGTH_MM  = 50.0f;  // standard lens

// blur parameters
static const float MAX_COC_RADIUS     = 16.0f;  // maximum blur in pixels
static const float COC_CLAMP_FACTOR   = 0.8f;   // prevent excessive blur
static const float IN_FOCUS_THRESHOLD = 0.5f;   // coc below this = sharp
static const int   SAMPLE_COUNT       = 48;     // bokeh quality (higher = smoother)
static const float GOLDEN_ANGLE       = 2.39996323f;

// auto-focus parameters
static const int   FOCUS_SAMPLES      = 16;     // depth samples for focus calculation
static const float FOCUS_REGION       = 0.12f;  // screen fraction for focus area
static const float CENTER_WEIGHT_BIAS = 2.5f;   // prefer center of screen for focus
static const float OUTLIER_THRESHOLD  = 0.4f;   // reject depths this far from median

// depth handling
static const float NEAR_SCALE = 1.2f;           // foreground blur emphasis
static const float FAR_SCALE  = 1.0f;           // background blur scale
static const float BG_LEAK_PREVENTION = 0.5f;   // reduce background bleeding into foreground

/*------------------------------------------------------------------------------
    circle of confusion calculation using thin lens model
    
    formula: coc = |A * f * (s - d) / (d * (s - f))|
    where: A = aperture diameter, f = focal length
           s = focus distance, d = pixel depth
------------------------------------------------------------------------------*/
float compute_coc(float pixel_depth, float focus_distance, float aperture_fstop, float2 resolution)
{
    // convert focal length to meters
    float f = FOCAL_LENGTH_MM * 0.001f;
    
    // clamp distances to valid range
    float s = max(focus_distance, f + 0.01f);  // focus distance
    float d = max(pixel_depth, 0.01f);         // pixel depth
    
    // aperture diameter from f-stop: diameter = focal_length / f-stop
    float aperture_diameter = f / max(aperture_fstop, 1.0f);
    
    // thin lens coc in meters
    float coc_m = abs(aperture_diameter * (s - d) * f) / (d * abs(s - f) + FLT_MIN);
    
    // convert to pixels: pixels = coc_meters * (resolution / sensor_size)
    float sensor_m = SENSOR_HEIGHT_MM * 0.001f;
    float coc_pixels = coc_m * (resolution.y / sensor_m);
    
    // sign indicates near (-) vs far (+) field
    float sign = (d < s) ? -1.0f : 1.0f;
    
    // scale differently for near/far
    float scale = (sign < 0.0f) ? NEAR_SCALE : FAR_SCALE;
    coc_pixels *= scale;
    
    // clamp to reasonable range
    coc_pixels = min(abs(coc_pixels), MAX_COC_RADIUS * COC_CLAMP_FACTOR);
    
    return sign * coc_pixels;
}

/*------------------------------------------------------------------------------
    robust auto-focus with weighted sampling
    
    uses a spiral pattern centered on screen, weights center samples higher,
    and rejects outliers to avoid focusing on background through holes
------------------------------------------------------------------------------*/
float compute_focus_distance(float2 resolution)
{
    float2 center = float2(0.5f, 0.5f);
    
    // collect depth samples
    float depths[FOCUS_SAMPLES];
    float weights[FOCUS_SAMPLES];
    float weight_sum = 0.0f;
    
    // spiral sampling with golden angle
    for (int i = 0; i < FOCUS_SAMPLES; i++)
    {
        float t = (float)i / (float)(FOCUS_SAMPLES - 1);
        float angle = i * GOLDEN_ANGLE;
        float radius = sqrt(t) * FOCUS_REGION;
        
        float2 offset = float2(cos(angle), sin(angle)) * radius;
        float2 uv = center + offset;
        
        depths[i] = get_linear_depth(uv);
        
        // weight by proximity to center (gaussian-ish falloff)
        float dist = length(offset) / FOCUS_REGION;
        weights[i] = exp(-dist * dist * CENTER_WEIGHT_BIAS);
        weight_sum += weights[i];
    }
    
    // first pass: weighted average for approximate median
    float weighted_avg = 0.0f;
    for (int i = 0; i < FOCUS_SAMPLES; i++)
    {
        weighted_avg += depths[i] * weights[i];
    }
    weighted_avg /= max(weight_sum, FLT_MIN);
    
    // second pass: reject outliers and refine
    float refined_sum = 0.0f;
    float refined_weight = 0.0f;
    
    for (int i = 0; i < FOCUS_SAMPLES; i++)
    {
        float deviation = abs(depths[i] - weighted_avg) / max(weighted_avg, 0.1f);
        
        if (deviation < OUTLIER_THRESHOLD)
        {
            // closer to average = higher contribution
            float confidence = 1.0f - (deviation / OUTLIER_THRESHOLD);
            confidence *= confidence; // square for sharper falloff
            
            float w = weights[i] * confidence;
            refined_sum += depths[i] * w;
            refined_weight += w;
        }
    }
    
    // fallback if too many outliers
    return (refined_weight > FLT_MIN) ? (refined_sum / refined_weight) : weighted_avg;
}

/*------------------------------------------------------------------------------
    depth-aware sample weighting for bokeh blur
    
    handles the scatter/gather mismatch by simulating how out-of-focus
    samples would "scatter" light to cover nearby pixels
------------------------------------------------------------------------------*/
float sample_weight(float sample_coc, float center_coc, float sample_depth, float center_depth, float sample_distance)
{
    // how much of the blur disk covers this sample position
    float effective_coc = max(abs(sample_coc), abs(center_coc));
    float coverage = saturate(1.0f - sample_distance / max(effective_coc, FLT_MIN));
    
    // depth-based occlusion: prevent background from bleeding into foreground
    float depth_weight = 1.0f;
    bool sample_behind = sample_depth > center_depth;
    bool center_is_fg = center_coc < 0.0f; // negative coc = foreground
    
    if (sample_behind && center_is_fg)
    {
        // background sample trying to contribute to foreground pixel
        depth_weight = BG_LEAK_PREVENTION;
    }
    
    // larger coc samples contribute more (they scatter over larger area)
    float scatter_factor = saturate(abs(sample_coc) / (MAX_COC_RADIUS * 0.3f));
    scatter_factor = lerp(0.3f, 1.0f, scatter_factor);
    
    return coverage * depth_weight * scatter_factor;
}

/*------------------------------------------------------------------------------
    main bokeh blur with gather sampling
------------------------------------------------------------------------------*/
float3 bokeh_gather(float2 uv, float center_coc, float center_depth, float focus_dist, float aperture, float2 texel_size, float2 resolution)
{
    float blur_radius = abs(center_coc);
    
    // early out for in-focus pixels
    if (blur_radius < IN_FOCUS_THRESHOLD)
    {
        return tex.SampleLevel(samplers[sampler_bilinear_clamp], uv, 0).rgb;
    }
    
    // accumulate samples
    float3 color_sum = float3(0.0f, 0.0f, 0.0f);
    float weight_sum = 0.0f;
    
    // center sample always included
    float3 center_color = tex.SampleLevel(samplers[sampler_bilinear_clamp], uv, 0).rgb;
    color_sum += center_color;
    weight_sum += 1.0f;
    
    // randomize starting angle per pixel for temporal stability with taa
    float start_angle = noise_interleaved_gradient(uv * resolution, true) * PI2;
    float angle = start_angle;
    
    // golden angle spiral sampling
    for (int i = 0; i < SAMPLE_COUNT; i++)
    {
        angle += GOLDEN_ANGLE;
        
        // sqrt distribution: more samples near center
        float t = (float)(i + 1) / (float)SAMPLE_COUNT;
        float r = sqrt(t) * blur_radius;
        
        float2 offset = float2(cos(angle), sin(angle)) * r * texel_size;
        float2 sample_uv = uv + offset;
        
        if (!is_valid_uv(sample_uv))
            continue;
        
        float3 sample_color = tex.SampleLevel(samplers[sampler_bilinear_clamp], sample_uv, 0).rgb;
        float sample_depth = get_linear_depth(sample_uv);
        float sample_coc = compute_coc(sample_depth, focus_dist, aperture, resolution);
        
        float w = sample_weight(sample_coc, center_coc, sample_depth, center_depth, r);
        
        color_sum += sample_color * w;
        weight_sum += w;
    }
    
    return color_sum / max(weight_sum, FLT_MIN);
}

/*------------------------------------------------------------------------------
    compute shader entry point
------------------------------------------------------------------------------*/
[numthreads(THREAD_GROUP_COUNT_X, THREAD_GROUP_COUNT_Y, 1)]
void main_cs(uint3 thread_id : SV_DispatchThreadID)
{
    float2 resolution;
    tex_uav.GetDimensions(resolution.x, resolution.y);
    
    if (any(thread_id.xy >= uint2(resolution)))
        return;
    
    float2 uv = (thread_id.xy + 0.5f) / resolution;
    float2 texel_size = 1.0f / resolution;
    
    // get aperture from pass constants
    float aperture = pass_get_f3_value().x;
    
    // compute auto-focus distance
    float focus_distance = compute_focus_distance(resolution);
    
    // compute coc for this pixel
    float depth = get_linear_depth(uv);
    float coc = compute_coc(depth, focus_distance, aperture, resolution);
    
    // perform depth-aware bokeh blur
    float3 blurred = bokeh_gather(uv, coc, depth, focus_distance, aperture, texel_size, resolution);
    
    // blend based on blur amount
    float4 original = tex[thread_id.xy];
    float blend = smoothstep(0.0f, 1.0f, abs(coc) / MAX_COC_RADIUS);
    
    float3 result = lerp(original.rgb, blurred, blend);
    
    tex_uav[thread_id.xy] = float4(result, original.a);
}
