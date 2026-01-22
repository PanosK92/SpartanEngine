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

//= INCLUDES =========
#include "common.hlsl"
//====================

// denoiser-upscaler configuration
static const float UPSCALE_DEPTH_SIGMA     = 0.02f;   // depth edge sensitivity
static const float UPSCALE_NORMAL_SIGMA    = 32.0f;   // normal edge sensitivity
static const float UPSCALE_LUMINANCE_SIGMA = 4.0f;    // luminance sensitivity
static const float TEMPORAL_MIN_ALPHA      = 0.02f;   // aggressive temporal accumulation
static const float TEMPORAL_MAX_ALPHA      = 0.15f;   // max blend for high motion
static const float VARIANCE_CLIP_GAMMA     = 1.0f;    // variance clipping factor

// input: half-res noisy gi (tex), history at full res (tex2)
// output: full-res denoised gi (tex_uav)

// edge-stopping weight based on depth difference
float compute_depth_weight(float center_depth, float sample_depth)
{
    float depth_diff = abs(center_depth - sample_depth) / max(center_depth, 0.0001f);
    return exp(-depth_diff * depth_diff / (2.0f * UPSCALE_DEPTH_SIGMA * UPSCALE_DEPTH_SIGMA));
}

// edge-stopping weight based on normal difference
float compute_normal_weight(float3 center_normal, float3 sample_normal)
{
    float n_dot = max(dot(center_normal, sample_normal), 0.0f);
    return pow(n_dot, UPSCALE_NORMAL_SIGMA);
}

// edge-stopping weight based on luminance difference
float compute_luminance_weight(float center_lum, float sample_lum, float variance)
{
    float lum_diff = abs(center_lum - sample_lum);
    float sigma = UPSCALE_LUMINANCE_SIGMA * sqrt(max(variance, 0.0001f));
    return exp(-lum_diff / max(sigma, 0.0001f));
}

// reproject to previous frame
float2 reproject_to_previous_frame(float2 current_uv, float depth)
{
    float3 world_pos = get_position(depth, current_uv);
    float4 prev_clip = mul(float4(world_pos, 1.0f), buffer_frame.view_projection_previous);
    float2 prev_ndc  = prev_clip.xy / prev_clip.w;
    float2 prev_uv   = prev_ndc * float2(0.5f, -0.5f) + 0.5f;
    return prev_uv;
}

// compute local statistics for variance clipping
void compute_local_statistics_bilinear(float2 uv, float2 input_resolution, out float3 mean, out float3 std_dev)
{
    float3 m1 = 0.0f;
    float3 m2 = 0.0f;
    
    // sample 3x3 neighborhood in input (half-res) space
    float2 texel_size = 1.0f / input_resolution;
    
    [unroll]
    for (int y = -1; y <= 1; y++)
    {
        [unroll]
        for (int x = -1; x <= 1; x++)
        {
            float2 sample_uv = uv + float2(x, y) * texel_size;
            float3 c = tex.SampleLevel(GET_SAMPLER(sampler_bilinear_clamp), sample_uv, 0).rgb;
            m1 += c;
            m2 += c * c;
        }
    }
    
    mean = m1 / 9.0f;
    float3 variance = max(m2 / 9.0f - mean * mean, 0.0f);
    std_dev = sqrt(variance);
}

// geometry-aware bilateral upsampling from half-res to full-res
float3 bilateral_upsample(float2 output_uv, float2 input_resolution, float2 output_resolution,
                          float center_depth, float3 center_normal, out float variance)
{
    // map output uv to input (half-res) space
    float2 input_uv = output_uv;
    float2 input_texel = input_uv * input_resolution;
    float2 input_texel_floor = floor(input_texel - 0.5f);
    float2 frac_offset = input_texel - input_texel_floor - 0.5f;
    
    float3 result = 0.0f;
    float total_weight = 0.0f;
    float3 sum_sq = 0.0f;
    float3 sum = 0.0f;
    
    // 4x4 bilateral filter for high quality upsampling
    [unroll]
    for (int y = -1; y <= 2; y++)
    {
        [unroll]
        for (int x = -1; x <= 2; x++)
        {
            int2 sample_texel = int2(input_texel_floor) + int2(x, y);
            
            // clamp to valid range
            sample_texel = clamp(sample_texel, int2(0, 0), int2(input_resolution) - 1);
            
            float2 sample_uv = (sample_texel + 0.5f) / input_resolution;
            
            // sample at full resolution for geometry
            float sample_depth = get_linear_depth(sample_uv);
            float3 sample_normal = get_normal(sample_uv);
            float3 sample_color = tex[sample_texel].rgb;
            
            // bilateral weights
            float w_depth = compute_depth_weight(center_depth, sample_depth);
            float w_normal = compute_normal_weight(center_normal, sample_normal);
            
            // bicubic-like spatial weight
            float2 d = abs(float2(x, y) - frac_offset);
            float w_spatial = (1.0f - d.x) * (1.0f - d.y);
            w_spatial = max(w_spatial, 0.0f);
            
            float w = w_spatial * w_depth * w_normal;
            
            result += sample_color * w;
            total_weight += w;
            
            sum += sample_color;
            sum_sq += sample_color * sample_color;
        }
    }
    
    // compute variance for temporal filtering
    float3 mean = sum / 16.0f;
    float3 var = sum_sq / 16.0f - mean * mean;
    variance = max(luminance(var), 0.0001f);
    
    return result / max(total_weight, 0.0001f);
}

// temporal accumulation with variance clipping
float3 temporal_accumulate(float3 current_color, float2 output_uv, float2 output_resolution,
                           float depth, float3 mean, float3 std_dev)
{
    // reproject to previous frame
    float2 history_uv = reproject_to_previous_frame(output_uv, depth);
    
    if (!is_valid_uv(history_uv))
        return current_color;
    
    // sample history (full resolution)
    float3 history_raw = tex2.SampleLevel(GET_SAMPLER(sampler_bilinear_clamp), history_uv, 0).rgb;
    
    // variance clipping - constrain history to local neighborhood
    float3 box_min = mean - VARIANCE_CLIP_GAMMA * std_dev;
    float3 box_max = mean + VARIANCE_CLIP_GAMMA * std_dev;
    float3 history_clipped = clamp(history_raw, box_min, box_max);
    
    // adaptive blend based on motion and clipping
    float2 motion = (output_uv - history_uv) * output_resolution;
    float motion_length = length(motion);
    float temporal_alpha = lerp(TEMPORAL_MIN_ALPHA, TEMPORAL_MAX_ALPHA, saturate(motion_length * 0.1f));
    
    // increase blend when history was clamped
    float3 clamp_diff = abs(history_raw - history_clipped);
    float clamp_amount = luminance(clamp_diff) / max(luminance(mean), 0.0001f);
    temporal_alpha = lerp(temporal_alpha, TEMPORAL_MAX_ALPHA, saturate(clamp_amount * 2.0f));
    
    return lerp(history_clipped, current_color, temporal_alpha);
}

[numthreads(THREAD_GROUP_COUNT_X, THREAD_GROUP_COUNT_Y, 1)]
void main_cs(uint3 dispatch_id : SV_DispatchThreadID)
{
    // output is at full resolution
    uint2 output_pixel = dispatch_id.xy;
    float2 output_resolution = buffer_frame.resolution_render;
    
    if (output_pixel.x >= (uint)output_resolution.x || output_pixel.y >= (uint)output_resolution.y)
        return;
    
    float2 output_uv = (output_pixel + 0.5f) / output_resolution;
    
    // early out for sky
    float depth_raw = get_depth(output_uv);
    if (depth_raw <= 0.0f)
    {
        tex_uav[output_pixel] = float4(0, 0, 0, 1);
        return;
    }
    
    float center_depth = linearize_depth(depth_raw);
    float3 center_normal = get_normal(output_uv);
    
    // get input (half-res) dimensions from pass constants
    // pass_f3_value.xy = input resolution
    float3 pass_values = pass_get_f3_value();
    float2 input_resolution = pass_values.xy;
    
    // bilateral upsample from half-res input
    float variance;
    float3 upsampled = bilateral_upsample(output_uv, input_resolution, output_resolution,
                                          center_depth, center_normal, variance);
    
    // compute local statistics for temporal filtering
    float3 mean, std_dev;
    compute_local_statistics_bilinear(output_uv, input_resolution, mean, std_dev);
    
    // temporal accumulation
    float3 result = temporal_accumulate(upsampled, output_uv, output_resolution,
                                        depth_raw, mean, std_dev);
    
    // firefly suppression
    float lum = luminance(result);
    if (lum > 50.0f)
        result *= 50.0f / lum;
    
    tex_uav[output_pixel] = float4(result, 1.0f);
}
