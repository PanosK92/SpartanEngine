/*
Copyright(c) 2015-2025 Panos Karabelas

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

// dithering settings
static const float g_dither_strength = 0.1f;
static const int g_dither_mode       = 0; // 0 = additive, 1 = multiplicative

[numthreads(THREAD_GROUP_COUNT_X, THREAD_GROUP_COUNT_Y, 1)]
void main_cs(uint3 thread_id : SV_DispatchThreadID)
{
    // get output resolution
    float2 resolution;
    tex_uav.GetDimensions(resolution.x, resolution.y);

    // compute coordinates
    const int2 coord = thread_id.xy;
    const float2 uv = (coord + 0.5f) / resolution;

    // read input color (16-bit buffer, assumed post-tone mapping/pq encoding)
    float4 color = tex[coord];

    // sample blue noise (tile the texture)
    float2 blue_noise_size;
    tex2.GetDimensions(blue_noise_size.x, blue_noise_size.y);
    int2 noise_coord = coord % blue_noise_size;
    float pattern    = tex2[noise_coord].r; // [0,1]

    // apply dithering
    if (g_dither_mode == 0) // additive
    {
        color.rgb += pattern * g_dither_strength * color.rgb; // scale with color to preserve intensity
    }
    else // multiplicative
    {
        color.rgb *= lerp(1.0f, pattern, g_dither_strength);
    }

    // ensure no negative values
    color.rgb = max(0.0f, color.rgb);

    tex_uav[coord] = color;
}
