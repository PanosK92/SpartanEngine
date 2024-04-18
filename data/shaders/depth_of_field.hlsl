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

// Constants
static const int BLUR_RADIUS = 10;  // Maximum radius of the blur
static const int BLUR_SAMPLE_COUNT = (2 * BLUR_RADIUS + 1);

float compute_coc(float2 uv, float focus_distance, float aperture)
 {
    float depth           = get_linear_depth(uv);
    float max_blur_radius = lerp(20.0, 5.0, aperture / 16.0);
    float coc             = saturate(abs(depth - focus_distance) / max_blur_radius);
    
    return coc;
}

float gaussian_weight(float x, float sigma)
{
    return exp(-0.5 * (x * x) / (sigma * sigma)) / (sigma * sqrt(2.0 * 3.14159265));
}

float3 gaussian_blur(float2 uv, float coc, float2 resolution_out)
{
    float2 texel_size = 1.0f / resolution_out;
    float sigma       = max(1.0, coc * BLUR_RADIUS);
    float3 color      = float3(0.0, 0.0, 0.0);
    
    float total_weight = 0.0;
    for (int i = -BLUR_RADIUS; i <= BLUR_RADIUS; i++)
    {
        for (int j = -BLUR_RADIUS; j <= BLUR_RADIUS; j++)
        {
            float2 offset  = float2(i, j) * texel_size;
            float weight   = gaussian_weight(length(float2(i, j)), sigma);
            color         += tex.Sample(samplers[sampler_bilinear_clamp], uv + offset).rgb * weight;
            total_weight  += weight;
        }
    }

    return color / total_weight;
}

float get_average_depth_circle(float2 center, float radius, uint sample_count, float2 resolution_out)
{
    float average_depth = 0.0f;
    float2 texel_size   = 1.0f / resolution_out;
    float angle_step    = PI2 / (float)sample_count;

    for (int i = 0; i < sample_count; i++)
    {
        float angle           = i * angle_step;
        float2 sample_offset  = float2(cos(angle), sin(angle)) * radius * texel_size;
        float2 sample_uv      = center + sample_offset;
        average_depth        += get_linear_depth(sample_uv);
    }

    return average_depth / sample_count;
}

[numthreads(THREAD_GROUP_COUNT_X, THREAD_GROUP_COUNT_Y, 1)]
void main_cs(uint3 thread_id : SV_DispatchThreadID)
{
    float2 resolution_out;
    tex_uav.GetDimensions(resolution_out.x, resolution_out.y);
    if (any(int2(thread_id.xy) >= resolution_out))
        return;

    const float2 uv = (thread_id.xy + 0.5f) / resolution_out;

    // get focal depth from camera
    const float circle_radius = 0.1f;
    const uint  sample_count  = 10;
    float focal_depth         = get_average_depth_circle(float2(0.5, 0.5f), circle_radius, sample_count, resolution_out);
    float aperture            = pass_get_f3_value().x;

    // do the actual blurring
    float coc             = compute_coc(uv, focal_depth, aperture);
    float3 blurred_color  = gaussian_blur(uv, coc, resolution_out);
    float4 original_color = tex[thread_id.xy];
    float3 final_color    = lerp(original_color.rgb, blurred_color, coc);

    tex_uav[thread_id.xy] = float4(final_color, original_color.a);
}

