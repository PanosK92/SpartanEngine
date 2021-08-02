/*
Copyright(c) 2016-2021 Panos Karabelas

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

float compute_gaussian_weight(int sample_distance)
{
    float sigma2 = g_blur_sigma * g_blur_sigma;
    float g = 1.0f / sqrt(2.0f * 3.14159f * sigma2);
    return (g * exp(-(sample_distance * sample_distance) / (2.0f * sigma2)));
}

// Gaussian blur
float4 gaussian_blur(const uint2 pos)
{
    const float2 uv = (pos.xy + 0.5f) / g_resolution_rt;

    // https://github.com/TheRealMJP/MSAAFilter/blob/master/MSAAFilter/PostProcessing.hlsl#L50
    float weight_sum = 0.0f;
    float4 color     = 0;
    for (int i = -5; i < 5; i++)
    {
        float2 sample_uv = uv + (i * g_texel_size * g_blur_direction);
        float weight     = compute_gaussian_weight(i);
        color            += tex.SampleLevel(sampler_bilinear_clamp, sample_uv, 0) * weight;
        weight_sum       += weight;
    }

    return color / weight_sum;
}

// Depth aware gaussian blur
float4 depth_aware_gaussian_blur(const uint2 pos)
{
    const float2 uv = (pos.xy + 0.5f) / g_resolution_rt;
    
    float weight_sum     = 0.0f;
    float4 color         = 0.0f;
    float center_depth   = get_linear_depth(pos);
    float3 center_normal = get_normal(pos);
    float threshold      = 0.1f;

    for (int i = -5; i < 5; i++)
    {
        float2 sample_uv     = uv + (i * g_texel_size * g_blur_direction);
        float sample_depth   = get_linear_depth(sample_uv);
        float3 sample_normal = get_normal(sample_uv);
        
        // Depth-awareness
        float awareness_depth  = saturate(threshold - abs(center_depth - sample_depth));
        float awareness_normal = saturate(dot(center_normal, sample_normal)) + FLT_MIN; // FLT_MIN prevents NaN
        float awareness        = awareness_normal * awareness_depth;

        float weight = compute_gaussian_weight(i) * awareness;
        color        += tex.SampleLevel(sampler_bilinear_clamp, sample_uv, 0) * weight;
        weight_sum   += weight; 
    }

    return color / weight_sum;
}

[numthreads(thread_group_count_x, thread_group_count_y, 1)]
void mainCS(uint3 thread_id : SV_DispatchThreadID)
{
    if (thread_id.x >= uint(g_resolution_rt.x) || thread_id.y >= uint(g_resolution_rt.y))
        return;
    
    float4 color = 0.0f;
    
#if PASS_BLUR_GAUSSIAN
    color = gaussian_blur(thread_id.xy);
#endif

#if PASS_BLUR_BILATERAL_GAUSSIAN
    color = depth_aware_gaussian_blur(thread_id.xy);
#endif
    
    tex_out_rgba[thread_id.xy] = color;
}
