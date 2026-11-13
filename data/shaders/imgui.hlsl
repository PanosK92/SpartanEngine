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

#if defined(API_D3D12) || defined(API_WEBGPU)
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

// triangular noise in [-1, 1] per channel, new every frame so the eye averages it
float3 dither_noise(uint2 pixel, uint frame)
{
    uint3 v = uint3(pixel, frame) * 1664525u + 1013904223u;
    v.x += v.y * v.z; v.y += v.z * v.x; v.z += v.x * v.y;
    v   ^= v >> 16u;
    v.x += v.y * v.z; v.y += v.z * v.x; v.z += v.x * v.y;
    v   ^= v >> 16u;
    return (float3(v & 0xFFFFu) + float3(v >> 16u)) / 65535.0f - 1.0f;
}

float4 main_ps(vertex input) : SV_Target
{
    uint flags = pass_uint(pass_imgui::flags);

    // the flags come from the push constants, so they are the same for every pixel of a draw and a branch on them costs
    // nothing, this shader fills the whole editor several times over and only pays for the work a draw asks for
    bool point_sampling   = (flags & (1u << 8))  != 0;
    bool is_visualized    = (flags & (1u << 9))  != 0;
    bool is_frame_texture = (flags & (1u << 10)) != 0;
    bool is_sdr_capture   = (flags & (1u << 11)) != 0;
    bool is_glass         = (flags & (1u << 12)) != 0;
    bool is_dithered      = (flags & (1u << 13)) != 0;

    // sample texture
    float2 uv       = input.uv;
    float mip_level = pass_float(pass_imgui::mip_level);
    float4 color_texture;
    [branch]
    if (point_sampling)
    {
        color_texture = tex.SampleLevel(samplers[sampler_point_clamp], uv, mip_level);
    }
    else
    {
        color_texture = tex.SampleLevel(samplers[sampler_bilinear_clamp], uv, mip_level);
    }

    // visualization, only the texture viewer asks for it
    [branch]
    if (is_visualized)
    {
        float4 channels    = float4((flags & (1u << 0)) != 0 ? 1.0f : 0.0f, (flags & (1u << 1)) != 0 ? 1.0f : 0.0f, (flags & (1u << 2)) != 0 ? 1.0f : 0.0f, (flags & (1u << 3)) != 0 ? 1.0f : 0.0f);
        bool gamma_correct = (flags & (1u << 4)) != 0;
        bool packed        = (flags & (1u << 5)) != 0;
        bool boost         = (flags & (1u << 6)) != 0;
        bool absolute      = (flags & (1u << 7)) != 0;
        bool is_single     = (channels.r + channels.g + channels.b + channels.a) == 1.0f;

        color_texture = is_single ? float4(dot(color_texture, channels).xxx, 1.0f) : float4(color_texture.rgb * channels.rgb, lerp(1.0f, color_texture.a, channels.a));
        if (absolute)
        {
            color_texture = abs(color_texture);
        }
        if (packed)
        {
            color_texture.rgb = pack(color_texture.rgb);
        }
        if (gamma_correct)
        {
            color_texture.rgb = linear_to_srgb(color_texture.rgb);
        }
        if (boost)
        {
            color_texture.rgb *= 10.0f;
        }
    }

    float4 color = input.color * color_texture;

    // hdr encoding, skipped for the frame texture since the output pass already wrote display-encoded values
    // hdr_enabled: 1 = hdr10 pq, 2 = scrgb linear (1.0 = 80 nits)
    float ui_nits = buffer_frame.hdr_sdr_white_nits > 0.0f ? buffer_frame.hdr_sdr_white_nits : 203.0f;
    [branch]
    if (buffer_frame.hdr_enabled != 0.0f && !is_frame_texture && !is_sdr_capture)
    {
        float3 color_linear = srgb_to_linear(color.rgb);
        color.rgb           = buffer_frame.hdr_enabled > 1.5f ? color_linear * (ui_nits / 80.0f) : linear_to_hdr10(color_linear, ui_nits);
    }

    // an 8 bit screenshot wants sdr, so the display-encoded frame texture is brought back to srgb
    [branch]
    if (is_sdr_capture && is_frame_texture && buffer_frame.hdr_enabled != 0.0f)
    {
        float3 frame_linear = buffer_frame.hdr_enabled > 1.5f
            ? color.rgb * (80.0f / ui_nits)
            : hdr10_to_linear(color.rgb, ui_nits);
        color.rgb = linear_to_srgb(saturate(frame_linear));
    }

    // frosted glass backdrop, the ui alpha channel it was copied from is meaningless, and a faint grain makes it read as rough glass
    // blurring washes colors toward grey, so they are pushed back out, glass lit by what is behind it rather than fogged by it
    [branch]
    if (is_glass)
    {
        float luma  = dot(color.rgb, float3(0.2126f, 0.7152f, 0.0722f));
        color.rgb   = max(lerp(luma.xxx, color.rgb, 1.35f), 0.0f);
        float grain = frac(sin(dot(floor(input.position.xy), float2(12.9898f, 78.233f))) * 43758.5453f) - 0.5f;
        color.rgb  += grain * 0.025f;
        color.a     = input.color.a;
    }

    // the frame is fp16 and this write is where it gets rounded to the swapchain's 8 or 10 bits, one step of noise here
    // turns the bands of a smooth gradient into grain too fine to see, it has to be per swapchain pixel, noise added
    // to the frame itself is filtered away when the viewport is not the size of the frame
    float levels = buffer_frame.hdr_enabled > 1.5f ? 0.0f : (buffer_frame.hdr_enabled > 0.5f ? 1023.0f : 255.0f);
    [branch]
    if (is_dithered && levels > 0.0f)
    {
        // fades out under one step so black stays black
        color.rgb += dither_noise(uint2(input.position.xy), buffer_frame.frame) * saturate(color.rgb * levels) / levels;
    }

    return color;
}
