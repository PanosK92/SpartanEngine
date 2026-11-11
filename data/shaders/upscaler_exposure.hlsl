/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

#include "common.hlsl"

// the scene buffers are radiometric, a night sky sits around 1e-4, vendor upscalers keep their colour in low precision
// buffers built for values around 1 and round such a frame into bands, so the frame is brought to display range, where
// 1 is paper white, before the upscaler and back after it, both passes read the same exposure so the round trip cancels

// they also hold only so much contrast within one frame, a lamp at night is tens of thousands of paper whites and the
// asphalt next to it a few thousandths, dlss returns what is that much dimmer as coloured rings and xess clips the lamp,
// so above paper white the frame goes in as a square root, hue kept, the lamp becomes a few hundred, and is squared back
float3 highlights_compress(float3 color)
{
    float peak = max(color.r, max(color.g, color.b));
    return peak > 1.0f ? color * ((2.0f * sqrt(peak) - 1.0f) / peak) : color;
}

float3 highlights_expand(float3 color)
{
    float peak     = max(color.r, max(color.g, color.b));
    float expanded = 0.5f * (peak + 1.0f);
    return peak > 1.0f ? color * (expanded * expanded / peak) : color;
}

[numthreads(THREAD_GROUP_COUNT_X, THREAD_GROUP_COUNT_Y, 1)]
void main_cs(uint3 thread_id : SV_DispatchThreadID)
{
    float scale  = max(LUMINOUS_EFFICACY_MAX * get_effective_exposure(), 1e-6f);
    float4 color = tex_uav[thread_id.xy];

    if (pass_bool(pass_upscaler_exposure::inverse))
    {
        color.rgb = highlights_expand(color.rgb) / scale;
    }
    else
    {
        // the half float tops out at 65504, the compressed lamp stays well under it
        color.rgb = highlights_compress(min(color.rgb * scale, FLT_MAX_16U));
    }

    tex_uav[thread_id.xy] = color;
}
