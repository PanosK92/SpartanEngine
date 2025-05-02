/*
Copyright(c) 2016-2025 Panos Karabelas

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

// Dithering settings
static const float g_dither_strength = 0.1f;
static const int g_dither_mode = 0; // 0 = Additive, 1 = Multiplicative
static const bool g_use_luminance = false; // Use luminance for dither pattern

// Bayer matrix (8x8, values 0-63, normalized to [0,1] when used)
static const int g_bayer[64] = {
    0, 48, 12, 60,  3, 51, 15, 63,
    32, 16, 44, 28, 35, 19, 47, 31,
    8, 56,  4, 52, 11, 59,  7, 55,
    40, 24, 36, 20, 43, 27, 39, 23,
    2, 50, 14, 62,  1, 49, 13, 61,
    34, 18, 46, 30, 33, 17, 45, 29,
    10, 58,  6, 54,  9, 57,  5, 53,
    42, 26, 38, 22, 41, 25, 37, 21
};

// Get Bayer matrix value at index
float get_bayer(int2 index)
{
    int idx = index.x + index.y * 8;
    return float(g_bayer[idx]) / 64.0f; // Normalize to [0,1]
}

[numthreads(THREAD_GROUP_COUNT_X, THREAD_GROUP_COUNT_Y, 1)]
void main_cs(uint3 thread_id : SV_DispatchThreadID)
{
    // Get output resolution
    float2 resolution;
    tex_uav.GetDimensions(resolution.x, resolution.y);

    // Compute coordinates
    const int2 coord = thread_id.xy;
    const float2 uv = (coord + 0.5f) / resolution;

    // Read input color (16-bit buffer, assumed post-tone mapping/PQ encoding)
    float4 color = tex[coord];

    // Compute Bayer dither pattern
    int2 bayer_index = coord % 8; // Tile 8x8 matrix
    float pattern = get_bayer(bayer_index); // [0,1]

    // Optionally base dithering on luminance
    float dither_value = pattern;
    if (g_use_luminance)
    {
        float luminance = dot(color.rgb, float3(0.2126, 0.7152, 0.0722));
        dither_value = step(pattern, luminance); // Binary dither based on luminance
    }

    // Apply dithering
    if (g_dither_mode == 0) // Additive
    {
        color.rgb += dither_value * g_dither_strength * color.rgb; // Scale with color to preserve intensity
    }
    else // Multiplicative
    {
        color.rgb *= lerp(1.0f, dither_value, g_dither_strength);
    }

    // Ensure no negative values, preserve HDR highlights
    color.rgb = max(0.0f, color.rgb);

    // Write to output UAV
    tex_uav[coord] = color;
}
