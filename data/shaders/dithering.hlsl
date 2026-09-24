/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
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
