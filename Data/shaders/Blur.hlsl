/*
Copyright(c) 2016-2020 Panos Karabelas

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

#if PASS_BLUR_BOX
float4 mainPS(Pixel_PosUv input) : SV_TARGET
{
    float4 result   = float4(0.0f, 0.0f, 0.0f, 0.0f);
    float temp      = float(-int(g_blur_sigma)) * 0.5f + 0.5f;
    float2 hlim     = float2(temp, temp);
    
    for (float i = 0; i < g_blur_sigma; i += g_blur_direction.x)
    {
        for (float j = 0; j < g_blur_sigma; j += g_blur_direction.y) 
        {
            float2 offset = (hlim + float2(float(i), float(j))) * g_texel_size;
            result += tex.SampleLevel(sampler_bilinear_clamp, input.uv + offset, 0);
        }
    }

    return result / float(g_blur_sigma * g_blur_sigma);
}
#endif

float4 Blur_Gaussian_Fast(float2 uv, Texture2D tex)
{
    float4 color  = 0.0f;
    float2 off1   = float2(1.3846153846, 1.3846153846) * g_blur_direction;
    float2 off2   = float2(3.2307692308, 3.2307692308) * g_blur_direction;
    color += tex.SampleLevel(sampler_bilinear_clamp, uv, 0) * 0.2270270270;
    color += tex.SampleLevel(sampler_bilinear_clamp, uv + (off1 / g_resolution), 0) * 0.3162162162;
    color += tex.SampleLevel(sampler_bilinear_clamp, uv - (off1 / g_resolution), 0) * 0.3162162162;
    color += tex.SampleLevel(sampler_bilinear_clamp, uv + (off2 / g_resolution), 0) * 0.0702702703;
    color += tex.SampleLevel(sampler_bilinear_clamp, uv - (off2 / g_resolution), 0) * 0.0702702703;
    return color;
}

// Calculates the gaussian blur weight for a given distance and sigmas
float CalcGaussianWeight(int sampleDist)
{
    float sigma2 = g_blur_sigma * g_blur_sigma;
    float g = 1.0f / sqrt(2.0f * 3.14159f * sigma2);
    return (g * exp(-(sampleDist * sampleDist) / (2.0f * sigma2)));
}

// Gaussian blur in one direction
#if PASS_BLUR_GAUSSIAN
float4 mainPS(Pixel_PosUv input) : SV_TARGET
{
    // https://github.com/TheRealMJP/MSAAFilter/blob/master/MSAAFilter/PostProcessing.hlsl#L50
    float weightSum = 0.0f;
    float4 color    = 0;
    for (int i = -5; i < 5; i++)
    {
        float2 sample_uv    = input.uv + (i * g_texel_size * g_blur_direction);    
        float weight        = CalcGaussianWeight(i);
        color               += tex.SampleLevel(sampler_bilinear_clamp, sample_uv, 0) * weight;
        weightSum           += weight;
    }

    color /= weightSum;

    return color;
}
#endif

// Bilateral gaussian blur in one direction
#if PASS_BLUR_BILATERAL_GAUSSIAN
float4 mainPS(Pixel_PosUv input) : SV_TARGET
{
    float weightSum         = 0.0f;
    float4 color            = 0.0f;
    float center_depth      = get_linear_depth(tex_depth.SampleLevel(sampler_point_clamp, input.uv, 0).r);
    float3 center_normal    = normal_decode(tex_normal.SampleLevel(sampler_point_clamp, input.uv, 0).xyz);
    float threshold         = 0.1f;
    

    for (int i = -5; i < 5; i++)
    {
        float2 sample_uv        = input.uv + (i * g_texel_size * g_blur_direction);    
        float sample_depth      = get_linear_depth(tex_depth.SampleLevel(sampler_bilinear_clamp, sample_uv, 0).r);
        float3 sample_normal    = normal_decode(tex_normal.SampleLevel(sampler_bilinear_clamp, sample_uv, 0).xyz);
        
        // Depth-awareness
        float awareness_depth   = saturate(threshold - abs(center_depth - sample_depth));
        float awareness_normal  = saturate(dot(center_normal, sample_normal)) + FLT_MIN; // FLT_MIN prevents NaN
        float awareness         = awareness_normal * awareness_depth;

        float weight        = CalcGaussianWeight(i) * awareness;
        color               += tex.SampleLevel(sampler_bilinear_clamp, sample_uv, 0) * weight;
        weightSum           += weight; 
    }
    color /= weightSum;

    return color;
}
#endif
