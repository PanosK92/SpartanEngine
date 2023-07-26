/*
Copyright(c) 2016-2023 Panos Karabelas

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

static const float g_threshold = 0.1f;

float compute_gaussian_weight(int sample_distance)
{
    float sigma  = pass_get_f3_value2().x;
    float sigma2 = sigma * sigma;
    float g      = 1.0f / sqrt(2.0f * 3.14159f * sigma2);
    return (g * exp(-(sample_distance * sample_distance) / (2.0f * sigma2)));
}

// Gaussian blur
// https://github.com/TheRealMJP/MSAAFilter/blob/master/MSAAFilter/PostProcessing.hlsl#L50
float3 gaussian_blur(const uint2 pos)
{
    const float3 f3_value = pass_get_f3_value();

    const float2 uv             = (pos.xy + 0.5f) / pass_get_resolution_in();
    const float2 texel_size     = float2(1.0f / pass_get_resolution_in().x, 1.0f / pass_get_resolution_in().y);
    const float2 blur_direction = f3_value.xy;
    const float2 direction      = texel_size * blur_direction;
    const bool is_vertical_pass = blur_direction.y != 0.0f;

    float weight_sum = 0.0f;
    float3 color     = 0.0f;
    float radius     = f3_value.z;
    for (int i = -radius; i < radius; i++)
    {
        float2 sample_uv = uv + (i * direction);

        // During the vertical pass, the input texture is secondary scratch texture which belongs to the blur pass.
        // It's at least as big as the original input texture (to be blurred), so we have to adapt the sample uv.
        sample_uv = lerp(sample_uv, (trunc(sample_uv * pass_get_resolution_in()) + 0.5f) / pass_get_resolution_out(), is_vertical_pass);
        
        float weight = compute_gaussian_weight(i);
        color        += tex.SampleLevel(samplers[sampler_bilinear_clamp], sample_uv, 0).rgb * weight;
        weight_sum   += weight;
    }

    return color / weight_sum;
}

// Depth aware Gaussian blur
float3 depth_aware_gaussian_blur(const uint2 pos)
{
    const float3 f3_value = pass_get_f3_value();

    const float2 uv             = (pos.xy + 0.5f) / pass_get_resolution_in();
    const float2 texel_size     = float2(1.0f / pass_get_resolution_in().x, 1.0f / pass_get_resolution_in().y);
    const float2 blur_direction = f3_value.xy;
    const float2 direction      = texel_size * blur_direction;
    const bool is_vertical_pass = blur_direction.y != 0.0f;
    
    const float center_depth   = get_linear_depth(uv);
    const float3 center_normal = get_normal(uv);

    float weight_sum = 0.0f;
    float3 color     = 0.0f;
    float radius     = f3_value.z;
    for (int i = -radius; i < radius; i++)
    {
        float2 sample_uv     = uv + (i * direction);
        float sample_depth   = get_linear_depth(sample_uv);
        float3 sample_normal = get_normal(sample_uv);
        
        // Depth-awareness
        float awareness_depth  = saturate(g_threshold - abs(center_depth - sample_depth));
        float awareness_normal = saturate(dot(center_normal, sample_normal)) + FLT_MIN; // FLT_MIN prevents NaN
        float awareness        = awareness_normal * awareness_depth;

        // During the vertical pass, the input texture is secondary scratch texture which belongs to the blur pass.
        // It's at least as big as the original input texture (to be blurred), so we have to adapt the sample uv.
        sample_uv = lerp(sample_uv, (trunc(sample_uv * pass_get_resolution_in()) + 0.5f) / pass_get_resolution_out(), is_vertical_pass);

        float weight = compute_gaussian_weight(i) * awareness;
        color        += tex.SampleLevel(samplers[sampler_bilinear_clamp], sample_uv, 0).rgb * weight;
        weight_sum   += weight; 
    }

    return color / weight_sum;
}

[numthreads(THREAD_GROUP_COUNT_X, THREAD_GROUP_COUNT_Y, 1)]
void mainCS(uint3 thread_id : SV_DispatchThreadID)
{
    // Out of bounds check
    if (any(int2(thread_id.xy) >= pass_get_resolution_out().xy))
        return;

    float3 color = 0.0f;

#if PASS_BLUR_GAUSSIAN
    color = gaussian_blur(thread_id.xy);
#endif

#if PASS_BLUR_BILATERAL_GAUSSIAN
    color = depth_aware_gaussian_blur(thread_id.xy);
#endif
    
    tex_uav[thread_id.xy] = float4(color, tex_uav[thread_id.xy].a);
}
