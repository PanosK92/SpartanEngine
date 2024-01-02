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

[numthreads(THREAD_GROUP_COUNT_X, THREAD_GROUP_COUNT_Y, 1)]
void mainCS(uint3 thread_id : SV_DispatchThreadID)
{
    if (any(int2(thread_id.xy) >= pass_get_resolution_out()))
        return;

     // coordinates of the current pixel
    int2 coord = thread_id.xy;

    // read the original noisy pixel
    float4 noisyPixel = tex_uav2[coord];

    // initialize variables for denoising calculations
    float4 denoisedPixel = noisyPixel;
    float weightSum      = 1.0;
    float3 averageColor  = noisyPixel.rgb;

    // define the radius for the denoising kernel
    int radius = 1; // You can adjust this based on your needs

    // iterate over neighboring pixels within the radius
    for (int dy = -radius; dy <= radius; dy++)
    {
        for (int dx = -radius; dx <= radius; dx++)
        {
            // skip the center pixel
            if (dx == 0 && dy == 0)
                continue;

            // get the neighboring pixel's coordinates
            int2 neighborCoord = coord + int2(dx, dy);

            // read the neighboring pixel
            float4 neighborPixel = tex_uav2[neighborCoord];

            // calculate a weight for blending (simple average in this case)
            float weight = 1.0 / ((abs(dx) + abs(dy)) + 1);
            weightSum += weight;

            // accumulate the weighted color
            averageColor += neighborPixel.rgb * weight;
        }
    }

    // compute the denoised pixel color
    denoisedPixel.rgb = averageColor / weightSum;

    // write the denoised pixel back to the texture
    tex_uav[coord] = denoisedPixel;
}
