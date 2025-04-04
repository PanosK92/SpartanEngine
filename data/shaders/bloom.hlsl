/*
Copyright(c) 2016-2025 Panos Karabelas

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is furnished
to do so, subject to the following conditions :

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/

//= INCLUDES =========
#include "common.hlsl"
//====================

[numthreads(THREAD_GROUP_COUNT_X, THREAD_GROUP_COUNT_Y, 1)]
void main_cs(uint3 thread_id : SV_DispatchThreadID)
{
    // Get texture dimensions
    uint width, height;
    tex.GetDimensions(width, height);
    float2 inv_dims = 1.0 / float2(width, height);

    // Parameters
    const float bloom_intensity = pass_get_f3_value().x;
    const float luminance_threshold = 1.0; // Adjustable threshold for "bright" pixels

    // Compute UV coordinates
    float2 uv = (float2(thread_id.xy) + 0.5) * inv_dims;

    // Sample the original color
    float3 base_color = tex.SampleLevel(GET_SAMPLER(sampler_bilinear_clamp), uv, 0).rgb;

    // Compute bloom contribution from bright neighbors
    float3 bloom_color = 0.0;
    float kernel[9] = {
        0.0625, 0.125, 0.0625,  // 1/16, 2/16, 1/16
        0.125,  0.25,  0.125,  // 2/16, 4/16, 2/16
        0.0625, 0.125, 0.0625   // 1/16, 2/16, 1/16
    };
    int idx = 0;

    for (int j = -1; j <= 1; j++)
    {
        for (int i = -1; i <= 1; i++)
        {
            float2 offset = float2(i, j) * inv_dims;
            float3 sample_color = tex.SampleLevel(GET_SAMPLER(sampler_bilinear_clamp), uv + offset, 0).rgb;
            // Only include bright neighbors
            if (luminance(sample_color) > luminance_threshold)
            {
                bloom_color += sample_color * kernel[idx];
            }
            idx++;
        }
    }

    // Combine bloom with base color
    float3 final_color = base_color + bloom_color * bloom_intensity;

    // Write to output texture, clamping to [0,1]
    tex_uav[thread_id.xy] = float4(saturate(final_color), 1.0);
}
