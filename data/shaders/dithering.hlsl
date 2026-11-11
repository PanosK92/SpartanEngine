/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

//= INCLUDES =========
#include "common.hlsl"
//====================

// the frame is fp16 up to here, the swapchain then rounds it to 8 bits per channel in sdr and 10 in hdr10, in a dark
// smooth gradient one such step is a visible contour, noise of one step added before the rounding turns that
// error into grain too fine to see, it has to be sized to the step, not to the color, and added to the encoded signal

// triangular noise in [-1, 1], the r and g channels of the blue noise texture are independent
float dither_noise(uint2 coord, uint2 size)
{
    float2 noise = tex2[coord % size].rg;
    return noise.r + noise.g - 1.0f;
}

[numthreads(THREAD_GROUP_COUNT_X, THREAD_GROUP_COUNT_Y, 1)]
void main_cs(uint3 thread_id : SV_DispatchThreadID)
{
    const uint2 coord = thread_id.xy;

    // read input color (16-bit buffer, post-tone mapping, srgb or pq encoded)
    float4 color = tex[coord];

    // steps the display stores per channel, scrgb is an fp16 swapchain and rounds nothing
    float levels = buffer_frame.hdr_enabled > 1.5f ? 0.0f : (buffer_frame.hdr_enabled > 0.5f ? 1023.0f : 255.0f);
    if (levels > 0.0f)
    {
        uint2 noise_size;
        tex2.GetDimensions(noise_size.x, noise_size.y);

        // the pattern moves every frame so the eye averages it, each channel reads its own tap so the grain stays neutral
        uint2 shift  = coord + uint2(buffer_frame.frame * 37u, buffer_frame.frame * 91u);
        float3 noise = float3(dither_noise(shift, noise_size), dither_noise(shift + uint2(41u, 73u), noise_size), dither_noise(shift + uint2(89u, 17u), noise_size));

        // fades out under one step so black stays black
        color.rgb += noise * saturate(color.rgb * levels) / levels;
    }

    tex_uav[coord] = color;
}
