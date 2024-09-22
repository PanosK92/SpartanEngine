/*
Copyright(c) 2016-2024 Panos Karabelas

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
#include "Common.hlsl"
//====================

// constants
static const float BLUR_RADIUS                = 10.0f;  // maximum radius of the blur
static const float BASE_FOCAL_LENGTH          = 50.0;   // in mm, base focal length
static const float SENSOR_HEIGHT              = 24.0;   // in mm, assuming full-frame sensor
static const float EDGE_DETECTION_THRESHOLD   = 0.001f; // adjust to control edge sensitivity
static const uint  AVERAGE_DEPTH_SAMPLE_COUNT = 10;
static const float AVERAGE_DEPTH_RADIUS       = 0.5f;

float compute_coc(float2 uv, float2 texel_size, float2 resolution, float focus_distance, float aperture)
{
    float depth = get_linear_depth(uv);
    
    // edge detection
    float depth_diff = 0;
    for (int i = -1; i <= 1; i++)
    {
        for (int j = -1; j <= 1; j++)
        {
            float2 offset         = float2(i, j) * texel_size;
            float neighbor_depth  = get_linear_depth(uv + offset);
            depth_diff           += abs(depth - neighbor_depth);
        }
    }
    float edge_factor = saturate(depth_diff / EDGE_DETECTION_THRESHOLD);
    
    // adjust focal length based on focus distance
    float focal_length = BASE_FOCAL_LENGTH * (1.0 + saturate(focus_distance / 100.0));
    
    float f  = focal_length * 0.001; // convert to meters
    float s1 = focus_distance;
    float s2 = depth;
    
    // calculate the diameter of the circle of confusion
    float coc_diameter = abs(aperture * (s2 - s1) * f) / (s2 * (s1 - f));
    
    // convert the diameter to pixels
    float coc_pixels = coc_diameter * (resolution.y / SENSOR_HEIGHT);
    
    // adjust coc based on focus distance to enhance the effect
    float distance_factor  = saturate(focus_distance / 50.0);
    coc_pixels            *= lerp(2.0, 0.5, distance_factor);
    
    // enhance coc for edges
    coc_pixels *= (1.0 + edge_factor);
    
    return saturate(coc_pixels / 20.0); // normalize CoC
}

float gaussian_weight(float x, float sigma)
{
    return exp(-0.5 * (x * x) / (sigma * sigma)) / (sigma * sqrt(2.0 * 3.14159265));
}

float3 gaussian_blur(float2 uv, float coc, float2 texel_size)
{
    float sigma = max(1.0, coc * BLUR_RADIUS);
    int samples = clamp(int(sigma * 0.5), 1, BLUR_RADIUS); // adaptive sampling

    float3 color       = 0.0f;
    float total_weight = FLT_MIN;
    for (int i = -samples; i <= samples; i++)
    {
        for (int j = -samples; j <= samples; j++)
        {
            float2 offset  = float2(i, j) * texel_size;
            float distance = length(float2(i, j));
           
            if (distance <= sigma)
            {
                float weight  = gaussian_weight(distance, sigma);
                color        += tex.SampleLevel(samplers[sampler_bilinear_clamp], uv + offset, 0.0f).rgb * weight;
                total_weight += weight;
            }
        }
    }

    return color / total_weight;
}

float get_average_depth_circle(float2 center, float2 resolution_out)
{
    float2 texel_size = 1.0f / resolution_out;
    float angle_step  = PI2 / (float)AVERAGE_DEPTH_SAMPLE_COUNT;

    float average_depth = 0.0f;
    for (int i = 0; i < AVERAGE_DEPTH_SAMPLE_COUNT; i++)
    {
        float angle           = i * angle_step;
        float2 sample_offset  = float2(cos(angle), sin(angle)) * AVERAGE_DEPTH_RADIUS * texel_size;
        float2 sample_uv      = center + sample_offset;
        average_depth        += get_linear_depth(sample_uv);
    }

    return average_depth / (float)AVERAGE_DEPTH_SAMPLE_COUNT;
}

[numthreads(THREAD_GROUP_COUNT_X, THREAD_GROUP_COUNT_Y, 1)]
void main_cs(uint3 thread_id : SV_DispatchThreadID)
{
    float2 resolution;
    tex_uav.GetDimensions(resolution.x, resolution.y);
    const float2 uv         = (thread_id.xy + 0.5f) / resolution;
    const float2 texel_size = 1.0 / resolution;
    
    // get focal depth from camera
    float focal_depth = get_average_depth_circle(float2(0.5, 0.5f), resolution);
    float aperture    = pass_get_f3_value().x;

    // do the actual blurring
    float coc             = compute_coc(uv, texel_size, resolution, focal_depth, aperture);
    float3 blurred_color  = gaussian_blur(uv, coc, texel_size);
    float4 original_color = tex[thread_id.xy];
    float3 final_color    = lerp(original_color.rgb, blurred_color, coc);

    tex_uav[thread_id.xy] = float4(final_color, original_color.a);
}
