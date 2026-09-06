/*
Copyright(c) 2015-2026 Panos Karabelas

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
