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
static const float DENOISER_TEMPORAL_MIN_ALPHA = 0.05f;  // minimum temporal blend factor
static const float DENOISER_TEMPORAL_MAX_ALPHA = 0.2f;   // maximum temporal blend factor
static const float DENOISER_VARIANCE_CLIP      = 1.5f;   // variance clipping factor

// reproject current pixel to previous frame using view-projection matrix
float2 reproject_to_previous_frame(float2 current_uv, float depth)
{
    float3 world_pos = get_position(depth, current_uv);
    float4 prev_clip = mul(float4(world_pos, 1.0f), buffer_frame.view_projection_previous);
    float2 prev_ndc  = prev_clip.xy / prev_clip.w;
    float2 prev_uv   = prev_ndc * float2(0.5f, -0.5f) + 0.5f;
    return prev_uv;
}

// compute local color statistics for variance clipping
void compute_local_statistics(int2 pixel, float2 resolution, out float3 mean, out float3 std_dev)
{
    float3 m1 = 0.0f;
    float3 m2 = 0.0f;
    float count = 0.0f;
    
    for (int y = -1; y <= 1; y++)
    {
        for (int x = -1; x <= 1; x++)
        {
            int2 p = pixel + int2(x, y);
            if (p.x >= 0 && p.x < (int)resolution.x && p.y >= 0 && p.y < (int)resolution.y)
            {
                float3 c = tex[p].rgb;
                m1 += c;
                m2 += c * c;
                count += 1.0f;
            }
        }
    }
    
    mean = m1 / count;
    float3 variance = max(m2 / count - mean * mean, 0.0f);
    std_dev = sqrt(variance);
}

// temporal accumulation with variance clipping
float3 temporal_filter(float3 current_color, int2 pixel, float2 resolution, float depth)
{
    float2 uv = (pixel + 0.5f) / resolution;
    
    // reproject to previous frame using world position and previous view-projection
    float2 history_uv = reproject_to_previous_frame(uv, depth);
    
    // check if reprojection is valid
    if (!is_valid_uv(history_uv))
        return current_color;
    
    // sample history (passed via tex2)
    float3 history_color_raw = tex2.SampleLevel(GET_SAMPLER(sampler_bilinear_clamp), history_uv, 0).rgb;
    
    // compute local statistics for variance clipping
    float3 mean, std_dev;
    compute_local_statistics(pixel, resolution, mean, std_dev);
    
    // variance clipping - clamp history to local neighborhood
    float3 box_min = mean - DENOISER_VARIANCE_CLIP * std_dev;
    float3 box_max = mean + DENOISER_VARIANCE_CLIP * std_dev;
    float3 history_color = clamp(history_color_raw, box_min, box_max);
    
    // compute motion for adaptive blending
    float2 motion = (uv - history_uv) * resolution;
    float motion_length = length(motion);
    float temporal_alpha = lerp(DENOISER_TEMPORAL_MIN_ALPHA, DENOISER_TEMPORAL_MAX_ALPHA, saturate(motion_length * 0.1f));
    
    // also increase alpha when history was clamped significantly
    float3 clamp_diff = abs(history_color_raw - history_color);
    float clamp_amount = luminance(clamp_diff) / max(luminance(mean), 0.0001f);
    temporal_alpha = lerp(temporal_alpha, DENOISER_TEMPORAL_MAX_ALPHA, saturate(clamp_amount));
    
    // blend
    return lerp(history_color, current_color, temporal_alpha);
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
    
    float3 current_color = tex[pixel].rgb;
    float3 filtered = temporal_filter(current_color, pixel, resolution, depth);
    
    tex_uav[pixel] = float4(filtered, 1.0f);
}
