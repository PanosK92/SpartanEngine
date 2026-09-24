/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

#include "common.hlsl"
#include "vhs_signal.hlsl"

float vhs_hdr_mode()
{
    // Screenshots and the HMD have already been forced to SDR by the tone mapper.
    return pass_get_f3_value().x > 0.5f ? 0.0f : buffer_frame.hdr_enabled;
}

float vhs_white_nits()
{
    return buffer_frame.hdr_sdr_white_nits > 0.0f ? buffer_frame.hdr_sdr_white_nits : 203.0f;
}

float3 vhs_read_rgb(float2 uv)
{
    float3 rgb = tex.SampleLevel(samplers[sampler_bilinear_clamp], uv, 0).rgb;
    return vhs_decode_display(rgb, vhs_hdr_mode(), vhs_white_nits());
}

[numthreads(THREAD_GROUP_COUNT_X, THREAD_GROUP_COUNT_Y, 1)]
void main_cs(uint3 thread_id : SV_DispatchThreadID)
{
    uint width, height;
    tex_uav.GetDimensions(width, height);
    if (thread_id.x >= width || thread_id.y >= height) return;
    float2 resolution = float2(width, height);
    float2 uv = (float2(thread_id.xy) + 0.5f) / resolution;
    float3 color = vhs_process(uv, resolution, (float)buffer_frame.time);
    tex_uav[thread_id.xy] = float4(vhs_encode_display(color, vhs_hdr_mode(), vhs_white_nits()), 1.0f);
}
