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

// denoiser configuration
static const float DENOISER_DEPTH_SIGMA     = 0.01f;  // depth sensitivity
static const float DENOISER_NORMAL_SIGMA    = 64.0f;  // normal sensitivity
static const float DENOISER_LUMINANCE_SIGMA = 4.0f;   // luminance sensitivity

// atrous wavelet filter offsets for 3x3 kernel
static const int2 ATROUS_OFFSETS[9] = {
    int2(-1, -1), int2(0, -1), int2(1, -1),
    int2(-1,  0), int2(0,  0), int2(1,  0),
    int2(-1,  1), int2(0,  1), int2(1,  1)
};

// atrous wavelet weights (approximates gaussian)
static const float ATROUS_WEIGHTS[9] = {
    1.0f / 16.0f, 2.0f / 16.0f, 1.0f / 16.0f,
    2.0f / 16.0f, 4.0f / 16.0f, 2.0f / 16.0f,
    1.0f / 16.0f, 2.0f / 16.0f, 1.0f / 16.0f
};

// edge-stopping function based on depth difference
float compute_depth_weight(float center_depth, float sample_depth)
{
    float depth_diff = abs(center_depth - sample_depth) / max(center_depth, 0.0001f);
    return exp(-depth_diff * depth_diff / (2.0f * DENOISER_DEPTH_SIGMA * DENOISER_DEPTH_SIGMA));
}

// edge-stopping function based on normal difference
float compute_normal_weight(float3 center_normal, float3 sample_normal)
{
    float dot_n = max(dot(center_normal, sample_normal), 0.0f);
    float angle_weight = pow(dot_n, DENOISER_NORMAL_SIGMA);
    return angle_weight;
}

// edge-stopping function based on luminance difference
float compute_luminance_weight(float center_lum, float sample_lum, float variance)
{
    float lum_diff = abs(center_lum - sample_lum);
    float sigma_l = DENOISER_LUMINANCE_SIGMA * sqrt(max(variance, 0.0001f));
    return exp(-lum_diff / max(sigma_l, 0.0001f));
}

// compute local variance for a pixel using 3x3 neighborhood
float compute_variance(int2 pixel, float2 resolution)
{
    float3 sum = 0.0f;
    float3 sum_sq = 0.0f;
    float count = 0.0f;
    
    for (int y = -1; y <= 1; y++)
    {
        for (int x = -1; x <= 1; x++)
        {
            int2 p = pixel + int2(x, y);
            if (p.x >= 0 && p.x < (int)resolution.x && p.y >= 0 && p.y < (int)resolution.y)
            {
                float3 c = tex[p].rgb;
                sum += c;
                sum_sq += c * c;
                count += 1.0f;
            }
        }
    }
    
    float3 mean = sum / count;
    float3 var = sum_sq / count - mean * mean;
    
    return max(luminance(var), 0.0f);
}

// spatial filtering pass - edge-preserving atrous wavelet filter
float3 spatial_filter(int2 pixel, float2 resolution, float center_depth, float3 center_normal, float variance, int step_size)
{
    float3 center_color = tex[pixel].rgb;
    float center_lum = luminance(center_color);
    
    float3 filtered_color = 0.0f;
    float total_weight = 0.0f;
    
    for (uint i = 0; i < 9; i++)
    {
        int2 offset = ATROUS_OFFSETS[i] * step_size;
        int2 sample_pixel = pixel + offset;
        
        // boundary check
        if (sample_pixel.x < 0 || sample_pixel.x >= (int)resolution.x ||
            sample_pixel.y < 0 || sample_pixel.y >= (int)resolution.y)
            continue;
        
        float2 sample_uv = (sample_pixel + 0.5f) / resolution;
        
        // sample geometry
        float sample_depth = get_linear_depth(sample_uv);
        float3 sample_normal = get_normal(sample_uv);
        float3 sample_color = tex[sample_pixel].rgb;
        float sample_lum = luminance(sample_color);
        
        // compute edge-stopping weights
        float w_depth = compute_depth_weight(center_depth, sample_depth);
        float w_normal = compute_normal_weight(center_normal, sample_normal);
        float w_lum = compute_luminance_weight(center_lum, sample_lum, variance);
        
        // combined weight
        float w = ATROUS_WEIGHTS[i] * w_depth * w_normal * w_lum;
        
        filtered_color += sample_color * w;
        total_weight += w;
    }
    
    return filtered_color / max(total_weight, 0.0001f);
}

[numthreads(THREAD_GROUP_COUNT_X, THREAD_GROUP_COUNT_Y, 1)]
void main_cs(uint3 dispatch_id : SV_DispatchThreadID)
{
    uint2 pixel = dispatch_id.xy;
    float2 resolution = buffer_frame.resolution_render;
    
    if (pixel.x >= (uint)resolution.x || pixel.y >= (uint)resolution.y)
        return;
    
    float2 uv = (pixel + 0.5f) / resolution;
    
    // early out for sky/background
    float depth = get_depth(uv);
    if (depth <= 0.0f)
    {
        tex_uav[pixel] = tex[pixel];
        return;
    }
    
    // get center geometry
    float center_depth = linearize_depth(depth);
    float3 center_normal = get_normal(uv);
    
    // compute local variance
    float variance = compute_variance(pixel, resolution);
    
    // get step size from pass buffer
    int step_size = (int)pass_get_f3_value().x;
    
    // spatial filtering
    float3 filtered = spatial_filter(pixel, resolution, center_depth, center_normal, variance, step_size);
    
    tex_uav[pixel] = float4(filtered, 1.0f);
}
