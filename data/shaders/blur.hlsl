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
#include "common.hlsl"
//====================

float compute_gaussian_weight(int sample_distance, const float2 sigma2)
{
    float g = 1.0f / sqrt(PI2 * sigma2);
    return (g * exp(-(sample_distance * sample_distance) / (2.0f * sigma2)));
}

float2 adjust_sample_uv(const float2 uv, const float2 direction)
{
    // during the vertical pass, the input texture is secondary scratch texture which belongs to the blur pass
    // it's at least as big as the original input texture (to be blurred), so we have to adapt the sample uv
    return lerp(uv, (trunc(uv * pass_get_resolution_in()) + 0.5f) / pass_get_resolution_out(), direction.y != 0.0f);
}

float3 gaussian_blur(const uint2 pos, const float2 uv, const float radius, const float sigma2, const float2 direction)
{
    #if PASS_BLUR_GAUSSIAN_BILATERAL
    const float center_depth   = get_linear_depth(pos);
    const float3 center_normal = get_normal(pos);
    #endif
    
    float3 color  = 0.0f;
    float weights = 0.0f;
    for (int i = -radius; i < radius; i++)
    {
        float2 sample_uv     = uv + (i * direction);
        float sample_depth   = get_linear_depth(sample_uv);
        float3 sample_normal = get_normal(sample_uv);

        float depth_awareness = 1.0f; 
        #if PASS_BLUR_GAUSSIAN_BILATERAL
        float awareness_depth  = saturate(0.1f - abs(center_depth - sample_depth));
        float awareness_normal = saturate(dot(center_normal, sample_normal)) + FLT_MIN; // FLT_MIN prevents NaN
        depth_awareness        = awareness_normal * awareness_depth;
        #endif

        float weight  = compute_gaussian_weight(i, sigma2) * depth_awareness;
        color        += tex.SampleLevel(samplers[sampler_bilinear_clamp], adjust_sample_uv(sample_uv, direction), 0).rgb * weight;
        weights      += weight;
    }

    return color / weights;
}

[numthreads(THREAD_GROUP_COUNT_X, THREAD_GROUP_COUNT_Y, 1)]
void mainCS(uint3 thread_id : SV_DispatchThreadID)
{
    if (any(int2(thread_id.xy) >= pass_get_resolution_in()) || any(int2(thread_id.xy) >= pass_get_resolution_out()))
        return;

    float4 color = tex_uav[thread_id.xy];
    
    // fetch properties and compute some information
    const float3 f3_value   = pass_get_f3_value();
    #if RADIUS_FROM_TEXTURE 
    const float radius      = clamp(tex_uav2[thread_id.xy].r, 1.0f, 32.0f);
    #else
    const float radius      = f3_value.x;
    #endif
    const float sigma       = radius / 3.0f;
    const float2 direction  = f3_value.y == 1.0f ? float2(0.0f, 1.0f) : float2(1.0f, 0.0f);
    const float2 uv         = (thread_id.xy + 0.5f) / pass_get_resolution_in();
    const float2 texel_size = 1.0f / pass_get_resolution_in();

    color.rgb = gaussian_blur(thread_id.xy, uv, radius, sigma * sigma, direction * texel_size);
    
    tex_uav[thread_id.xy] = color;
}
