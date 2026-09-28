/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

//= includes =====================
#include "shared_buffers.h"
#include "common_colorspace.hlsl"
//================================

Texture2D tex                                        : register(t7);
StructuredBuffer<DrawData> draw_data                 : register(t19, space5);
SamplerState samplers[]                              : register(s1, space7);
cbuffer BufferFrame : register(b0) { FrameBufferData buffer_frame; };

#ifdef API_D3D12
cbuffer BufferPass : register(b1) { PassBufferData buffer_pass; };
#else
[[vk::push_constant]]
PassBufferData buffer_pass;
#endif

static const uint sampler_point_clamp    = 0;
static const uint sampler_bilinear_clamp = 3;

float3 pack(float3 value)          { return value * 0.5f + 0.5f; }
float  pass_float(uint slot)       { return buffer_pass.values[slot / 4][slot % 4]; }
uint   pass_uint(uint slot)        { return asuint(pass_float(slot)); }

struct Vertex_Pos2dUvColor
{
    float2 position : POSITION;
    float2 uv       : TEXCOORD;
    float4 color    : COLOR;
};

struct vertex
{
    float4 position : SV_POSITION;
    float4 color    : COLOR;
    float2 uv       : TEXCOORD;
};

vertex main_vs(Vertex_Pos2dUvColor input)
{
    vertex output;

    output.position = mul(draw_data[buffer_pass.draw_index].transform, float4(input.position.x, input.position.y, 0.0f, 1.0f));
    output.color    = input.color;
    output.uv       = input.uv;

    return output;
}

// inverse of linear_to_hdr10, returns rec.709 linear values where 1.0 = white_point nits
float3 hdr10_to_linear(float3 color, float white_point)
{
    {
        static const float m1 = 2610.0 / 4096.0 / 4;
        static const float m2 = 2523.0 / 4096.0 * 128;
        static const float c1 = 3424.0 / 4096.0;
        static const float c2 = 2413.0 / 4096.0 * 32;
        static const float c3 = 2392.0 / 4096.0 * 32;
        float3 ep = pow(abs(color), 1.0f / m2);
        color = pow(max(ep - c1, 0.0f) / (c2 - c3 * ep), 1.0f / m1);
    }

    color *= 10000.0f / white_point;

    {
        static const float3x3 from2020to709 =
        {
            {  1.6604910f, -0.5876411f, -0.0728499f },
            { -0.1245505f,  1.1328999f, -0.0083494f },
            { -0.0181508f, -0.1005789f,  1.1187297f }
        };
        color = mul(from2020to709, color);
    }

    return color;
}

float4 main_ps(vertex input) : SV_Target
{
    uint flags = pass_uint(pass_imgui::flags);

    // extract booleans
    uint channel_r        = (flags & (1 << 0))  != 0 ? 1 : 0;
    uint channel_g        = (flags & (1 << 1))  != 0 ? 1 : 0;
    uint channel_b        = (flags & (1 << 2))  != 0 ? 1 : 0;
    uint channel_a        = (flags & (1 << 3))  != 0 ? 1 : 0;
    uint gamma_correct    = (flags & (1 << 4))  != 0 ? 1 : 0;
    uint packed           = (flags & (1 << 5))  != 0 ? 1 : 0;
    uint boost            = (flags & (1 << 6))  != 0 ? 1 : 0;
    uint absolute         = (flags & (1 << 7))  != 0 ? 1 : 0;
    uint point_sampling   = (flags & (1 << 8))  != 0 ? 1 : 0;
    uint is_visualized    = (flags & (1 << 9))  != 0 ? 1 : 0;
    uint is_frame_texture = (flags & (1 << 10)) != 0 ? 1 : 0;
    uint is_sdr_capture   = (flags & (1 << 11)) != 0 ? 1 : 0;

    float4 channels = float4(channel_r, channel_g, channel_b, channel_a);

    // sample texture
    float4 color_texture;
    float mip_level             = pass_float(pass_imgui::mip_level);
    float array_level           = pass_float(pass_imgui::array_level);
    float is_array              = array_level > 0.0f ? 1.0f : 0.0f; // not needed anymore
    float3 uv_array             = float3(input.uv, array_level);
    float4 sample_point_wrap    = tex.SampleLevel(samplers[sampler_point_clamp], input.uv, mip_level);
    float4 sample_bilinear_wrap = tex.SampleLevel(samplers[sampler_bilinear_clamp], input.uv, mip_level);
    color_texture               = lerp(sample_bilinear_wrap, sample_point_wrap, float(point_sampling));

    // visualization
    float4 color_original = color_texture;
    uint num_channels     = channel_r + channel_g + channel_b + channel_a;
    float f_visualized    = float(is_visualized);
    float val             = dot(color_texture, channels);
    float3 rgb_single     = val.xxx;
    float a_single        = 1.0f;
    float3 rgb_multi      = color_texture.rgb * channels.rgb;
    float a_multi         = lerp(1.0f, color_texture.a, channels.a);
    float is_single       = num_channels == 1u ? 1.0f : 0.0f;
    float3 rgb_vis        = lerp(rgb_multi, rgb_single, is_single);
    float a_vis           = lerp(a_multi, a_single, is_single);
    color_texture         = lerp(color_original, float4(rgb_vis, a_vis), f_visualized);

    color_texture      = lerp(color_texture, abs(color_texture), f_visualized * float(absolute));
    color_texture.rgb  = lerp(color_texture.rgb, pack(color_texture.rgb), f_visualized * float(packed));
    color_texture.rgb  = lerp(color_texture.rgb, linear_to_srgb(color_texture.rgb), f_visualized * float(gamma_correct));
    color_texture.rgb *= lerp(1.0f, 10.0f, f_visualized * float(boost));

    float4 color = input.color * color_texture;

    // hdr encoding, skipped for the frame texture since the output pass already wrote display-encoded values
    // hdr_enabled: 1 = hdr10 pq, 2 = scrgb linear (1.0 = 80 nits)
    float apply_hdr     = (buffer_frame.hdr_enabled != 0.0f ? 1.0f : 0.0f) * (1.0f - is_frame_texture);
    float3 color_linear = srgb_to_linear(color.rgb);
    float ui_nits       = buffer_frame.hdr_sdr_white_nits > 0.0f ? buffer_frame.hdr_sdr_white_nits : 203.0f;
    float3 color_hdr    = buffer_frame.hdr_enabled > 1.5f
        ? color_linear * (ui_nits / 80.0f)
        : linear_to_hdr10(color_linear, ui_nits);
    color.rgb           = lerp(color.rgb, color_hdr, apply_hdr * (1.0f - float(is_sdr_capture)));

    // an 8 bit screenshot wants sdr, so the display-encoded frame texture is brought back to srgb
    if (is_sdr_capture != 0 && is_frame_texture != 0 && buffer_frame.hdr_enabled != 0.0f)
    {
        float3 frame_linear = buffer_frame.hdr_enabled > 1.5f
            ? color.rgb * (80.0f / ui_nits)
            : hdr10_to_linear(color.rgb, ui_nits);
        color.rgb = linear_to_srgb(saturate(frame_linear));
    }

    return color;
}
