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
#include "output.hlsl"
//====================

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

    output.position = mul(buffer_pass.transform, float4(input.position.x, input.position.y, 0.0f, 1.0f));
    output.color    = input.color;
    output.uv       = input.uv;

    return output;
}

float4 main_ps(vertex input) : SV_Target
{
    // extract push constant data
    float3 flags_packed = pass_get_f3_value();
    uint flags          = asuint(flags_packed.x); // m00 contains bitfield
    float2 mip_array    = pass_get_f2_value();    // mip_level, array_level

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

    float4 channels = float4(channel_r, channel_g, channel_b, channel_a);

    // sample texture
    float4 color_texture;
    float mip_level   = mip_array.x;
    float array_level = mip_array.y;
    if (array_level > 0.0f)
    {
        float3 uv_array = float3(input.uv, array_level);
        color_texture = point_sampling
            ? tex_light_depth.SampleLevel(samplers[sampler_point_wrap], uv_array, mip_level)
            : tex_light_depth.SampleLevel(samplers[sampler_bilinear_wrap], uv_array, mip_level);
    }
    else
    {
        color_texture = point_sampling
            ? tex.SampleLevel(samplers[sampler_point_wrap], input.uv, mip_level)
            : tex.SampleLevel(samplers[sampler_bilinear_wrap], input.uv, mip_level);
    }

    // visualization
    color_texture.rgb *= lerp(float3(1.0f, 1.0f, 1.0f), channels.rgb, is_visualized);
    color_texture.a    = lerp(color_texture.a, lerp(1.0f, color_texture.a, channels.w), is_visualized);
    color_texture      = lerp(color_texture, abs(color_texture), is_visualized * absolute);
    color_texture.rgb  = lerp(color_texture.rgb, pack(color_texture.rgb), is_visualized * packed);
    color_texture.rgb  = lerp(color_texture.rgb, linear_to_srgb(color_texture.rgb), is_visualized * gamma_correct);
    color_texture.rgb *= lerp(1.0f, 10.0f, is_visualized * boost);

    // final
    float4 color = input.color * color_texture;

    // hdr
    float apply_hdr     = buffer_frame.hdr_enabled * (1.0f - is_frame_texture);
    float3 color_linear = srgb_to_linear(color.rgb);
    float3 color_hdr    = linear_to_hdr10(color_linear, 400.0f);
    color.rgb           = lerp(color.rgb, color_hdr, apply_hdr);

    return color;
}
