Texture2D<float4> source_image : register(t0);
RWTexture2D<float4> result_image : register(u0);
SamplerState linear_clamp : register(s0);
cbuffer Parameters : register(b0)
{
    float time;
    float hdr_mode;
    float white_nits;
    uint test_mode;
};

#include "../../data/shaders/vhs_signal.hlsl"

float3 vhs_read_rgb(float2 uv)
{
    return vhs_decode_display(source_image.SampleLevel(linear_clamp, uv, 0).rgb, hdr_mode, white_nits);
}

[numthreads(8, 8, 1)]
void main_cs(uint3 id : SV_DispatchThreadID)
{
    uint w, h;
    result_image.GetDimensions(w, h);
    if (id.x >= w || id.y >= h) return;
    float2 resolution = float2(w, h);
    float2 uv = (float2(id.xy) + 0.5f) / resolution;
    if (test_mode == 1u)
    {
        vhs_transport tape = vhs_transport_at(min(uint(uv.y * 480.0f), 479u), uint(floor(time * vhs_field_rate)));
        result_image[id.xy] = float4(tape.offset, tape.tracking, tape.switching, 1.0f);
    }
    else if (test_mode == 2u)
    {
        result_image[id.xy] = float4(vhs_encode_display(source_image.Load(int3(id.xy, 0)).rgb, hdr_mode, white_nits), 1);
    }
    else if (test_mode == 3u)
    {
        result_image[id.xy] = float4(vhs_read_rgb(uv), 1);
    }
    else
    {
        result_image[id.xy] = float4(vhs_encode_display(vhs_process(uv, resolution, time), hdr_mode, white_nits), 1.0f);
    }
}
